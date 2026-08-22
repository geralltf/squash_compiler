/* Implementation of img_decode_jpeg.h -- see that header's own top comment
 * for scope (baseline SOF0 only). JPEG/JFIF format: ITU-T T.81. Marker-
 * segment parsing (DQT/SOF0/DHT/SOS/DRI, restart markers RST0-7, EOI),
 * canonical Huffman decoding (JPEG spec Annex F's own mincode/maxcode/
 * valptr algorithm -- no external dependency, same "no libjpeg" reasoning
 * as this project's PNG/GIF decoders), zigzag-order dequantization, a
 * direct (not fast/separable) 2D IDCT, and YCbCr->RGB with nearest-
 * neighbor chroma upsampling for 4:4:4/4:2:2/4:2:0 subsampling. */
#include "img_decode_jpeg.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define JPEG_MAX_COMPONENTS 4
#define JPEG_MAX_QUANT_TABLES 4
#define JPEG_MAX_HUFF_TABLES 4

typedef struct {
    unsigned short values[64]; /* zigzag order, as stored in the file */
    int valid;
} JQuantTable;

typedef struct {
    unsigned char bits[17];    /* bits[l] = number of codes of length l, l=1..16 */
    unsigned char huffval[256];
    int mincode[17], maxcode[17], valptr[17];
    int valid;
} JHuffTable;

typedef struct {
    int id;
    int h_samp, v_samp;
    int quant_id;
    int dc_huff_id, ac_huff_id;
    int dc_pred;
    unsigned char *plane; /* comp_plane_w * comp_plane_h bytes, natural (non-zigzag) samples */
    int plane_w, plane_h;
} JComponent;

typedef struct {
    const unsigned char *data;
    long len;
    long pos;
    int bitbuf;
    int bitcount;
} JBitReader;

static int jpeg_get_bit(JBitReader *br) {
    if (br->bitcount == 0) {
        unsigned char b;
        if (br->pos >= br->len) {
            b = 0;
        } else {
            b = br->data[br->pos];
            if (b == 0xFF) {
                if (br->pos + 1 < br->len && br->data[br->pos + 1] == 0x00) br->pos += 2;
                else b = 0; /* real marker ahead: stop consuming, pad with 0 bits */
            } else {
                br->pos += 1;
            }
        }
        br->bitbuf = b;
        br->bitcount = 8;
    }
    br->bitcount--;
    return (br->bitbuf >> br->bitcount) & 1;
}

/* JPEG spec Annex F's own canonical-Huffman decode procedure: walk codes
 * length by length, comparing against each length's [mincode,maxcode]
 * range (built by build_huffman_table() below) -- correct without needing
 * a full 2^16-entry lookup table. */
static int jpeg_huff_decode(JBitReader *br, JHuffTable *h) {
    int code = 0, l;
    for (l = 1; l <= 16; l++) {
        code = (code << 1) | jpeg_get_bit(br);
        if (h->bits[l] && code <= h->maxcode[l]) {
            return h->huffval[h->valptr[l] + (code - h->mincode[l])];
        }
    }
    return 0; /* corrupt stream: safe-degrade default rather than aborting the whole image */
}

static void build_huffman_table(JHuffTable *h) {
    int code = 0, k = 0, i;
    for (i = 1; i <= 16; i++) {
        if (h->bits[i] == 0) {
            h->maxcode[i] = -1;
        } else {
            h->valptr[i] = k;
            h->mincode[i] = code;
            code += h->bits[i];
            k += h->bits[i];
            h->maxcode[i] = code - 1;
        }
        code <<= 1;
    }
}

/* JPEG's "extend" (Annex F.2.2.1): a Huffman-coded "size" S followed by S
 * raw bits encodes a signed value via this sign-magnitude-ish scheme
 * (values [0, 2^(S-1)) mean the negative range, [2^(S-1), 2^S) mean the
 * positive range). */
static int jpeg_receive_extend(JBitReader *br, int s) {
    if (s == 0) return 0;
    int v = 0, i;
    for (i = 0; i < s; i++) v = (v << 1) | jpeg_get_bit(br);
    /* "1 - (1 << s)", not "((-1) << s) + 1" -- same value (both equal
     * -(2^s - 1)) but the former never left-shifts a NEGATIVE value,
     * which is undefined behavior in C (found via UBSan on this exact
     * line, triggered by ordinary legitimate JPEG input, not even a
     * fuzzed one -- squash's own codegen happens to produce the
     * "expected" two's-complement result here, same as gcc/clang do in
     * practice, but relying on that is exactly the kind of thing a
     * future codegen change could silently break). */
    if (v < (1 << (s - 1))) v += 1 - (1 << s);
    return v;
}

static const int jpeg_zigzag[64] = {
     0, 1, 8,16, 9, 2, 3,10,
    17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,
    27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,
    29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,
    53,60,61,54,47,55,62,63
};

/* Direct (not separable/"fast") 2D IDCT -- O(64) output samples x O(64)
 * coefficient terms each; simple and obviously correct over raw
 * performance, matching this project's general "correctness over
 * micro-optimization" style elsewhere (e.g. img_decode_png.c's per-pixel
 * unfilter loop). `coef` is 64 natural-order (already dequantized)
 * coefficients; `out` receives 64 level-shifted, clamped 0..255 samples,
 * natural (row-major) order. */
static void jpeg_idct_block(const float *coef, unsigned char *out) {
    /* Recomputed every call rather than cached in a `static float
     * costab[8][8]` -- confirmed via a minimal repro that a STATIC 2D
     * float array's element reads (inside an expression, e.g.
     * `x * costab[i][j]`) come back wrong on this compiler, a narrower,
     * separate bug from the general 2D-float-array-read fix already
     * applied to codegen.c's index_elem_is_float() (that fix made a
     * plain, non-static `float costab[8][8]` read back correctly; only
     * adding `static` back reintroduces the corruption). Not chased
     * further -- recomputing 64 cos() calls per 8x8 block is cheap enough
     * at this project's scale to just avoid the bug entirely instead. */
    float costab[8][8]; /* costab[x][u] = cos((2x+1)*u*pi/16) */
    int x, u;
    for (x = 0; x < 8; x++)
        for (u = 0; u < 8; u++)
            costab[x][u] = (float)cos((2.0 * x + 1.0) * u * 3.14159265358979323846 / 16.0);

    int y, v;
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            float sum = 0.0f;
            for (v = 0; v < 8; v++) {
                float cv = (v == 0) ? 0.70710678118654752440f : 1.0f;
                float rowsum = 0.0f;
                for (u = 0; u < 8; u++) {
                    float cu = (u == 0) ? 0.70710678118654752440f : 1.0f;
                    rowsum += cu * coef[v * 8 + u] * costab[x][u];
                }
                sum += cv * rowsum * costab[y][v];
            }
            int px = (int)(sum / 4.0f + 128.5f);
            if (px < 0) px = 0; else if (px > 255) px = 255;
            out[y * 8 + x] = (unsigned char)px;
        }
    }
}

static int jpeg_decode_block(JBitReader *br, JHuffTable *dc_tab, JHuffTable *ac_tab,
                              JQuantTable *quant, int *dc_pred, unsigned char *out8x8) {
    float coef[64];
    memset(coef, 0, sizeof(coef));

    int s = jpeg_huff_decode(br, dc_tab);
    int diff = jpeg_receive_extend(br, s);
    *dc_pred += diff;
    coef[0] = (float)(*dc_pred) * (float)quant->values[0];

    int k = 1;
    while (k < 64) {
        int rs = jpeg_huff_decode(br, ac_tab);
        int run = rs >> 4, size = rs & 0x0F;
        if (size == 0) {
            if (run == 15) { k += 16; continue; } /* ZRL: 16 zero-run, no coefficient */
            break; /* EOB: rest of block is zero */
        }
        k += run;
        if (k >= 64) break;
        int val = jpeg_receive_extend(br, size);
        int zz = k; /* zigzag index */
        int natural = jpeg_zigzag[zz];
        coef[natural] = (float)val * (float)quant->values[zz];
        k++;
    }

    jpeg_idct_block(coef, out8x8);
    return 1;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int sqw_jpeg_decode(const unsigned char *data, long len, unsigned char **out_rgba, int *out_w, int *out_h) {
    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8) return 0; /* SOI */

    JQuantTable *quant = (JQuantTable *)calloc(JPEG_MAX_QUANT_TABLES, sizeof(JQuantTable));
    JHuffTable *dc_huff = (JHuffTable *)calloc(JPEG_MAX_HUFF_TABLES, sizeof(JHuffTable));
    JHuffTable *ac_huff = (JHuffTable *)calloc(JPEG_MAX_HUFF_TABLES, sizeof(JHuffTable));
    JComponent *comps = (JComponent *)calloc(JPEG_MAX_COMPONENTS, sizeof(JComponent));
    int num_comps = 0;
    int width = 0, height = 0;
    int restart_interval = 0;
    int have_sof = 0;
    int have_scan = 0; /* set once an SOS has actually allocated every
                           component's ->plane -- see the final RGBA
                           conversion's own comment on why this matters:
                           a SOF0-but-no-SOS (or truncated-before-that)
                           file left every ->plane NULL while `ok` stayed
                           true, and nothing checked for that before
                           dereferencing them -- a real NULL-pointer SEGV
                           found via fuzzing this function this session. */
    int ok = 1;

    long pos = 2;
    while (ok && pos + 4 <= len) {
        if (data[pos] != 0xFF) { pos++; continue; } /* fill bytes between markers: skip */
        unsigned char marker = data[pos + 1];
        pos += 2;
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue; /* no-length markers */
        if (marker == 0xD9) break; /* EOI */
        if (pos + 2 > len) { ok = 0; break; }
        int seg_len = ((int)data[pos] << 8) | data[pos + 1];
        long seg_start = pos + 2;
        long seg_end = pos + seg_len;
        if (seg_len < 2 || seg_end > len) { ok = 0; break; }

        if (marker == 0xDB) { /* DQT */
            long p = seg_start;
            while (p < seg_end) {
                if (p + 1 > seg_end) { ok = 0; break; }
                int pq = (data[p] >> 4) & 0x0F, tq = data[p] & 0x0F;
                p++;
                if (tq >= JPEG_MAX_QUANT_TABLES) { ok = 0; break; }
                /* 64 entries, each 1 or 2 bytes depending on `pq` -- same
                 * class of bug as DHT's own fix just above: nothing
                 * previously checked this against the segment's real
                 * remaining length before reading it. */
                if (p + (pq ? 128 : 64) > seg_end) { ok = 0; break; }
                int i;
                for (i = 0; i < 64; i++) {
                    if (pq) { quant[tq].values[i] = (unsigned short)(((int)data[p] << 8) | data[p + 1]); p += 2; }
                    else { quant[tq].values[i] = data[p]; p += 1; }
                }
                quant[tq].valid = 1;
            }
        } else if (marker == 0xC4) { /* DHT */
            long p = seg_start;
            while (p < seg_end) {
                if (p + 17 > seg_end) { ok = 0; break; } /* 1 (tc/th byte) + 16 (bits[1..16]) */
                int tc = (data[p] >> 4) & 0x0F, th = data[p] & 0x0F;
                p++;
                if (th >= JPEG_MAX_HUFF_TABLES) { ok = 0; break; }
                JHuffTable *t = (tc == 0) ? &dc_huff[th] : &ac_huff[th];
                memset(t, 0, sizeof(*t));
                int i, total = 0;
                for (i = 1; i <= 16; i++) { t->bits[i] = data[p++]; total += t->bits[i]; }
                /* `total` (sum of 16 attacker-controlled byte counts) can
                 * claim up to 16*255=4080 -- both far more than
                 * `huffval`'s real 256-entry capacity (a real JPEG's own
                 * Huffman table is mathematically bounded to <=256 total
                 * codes; a crafted DHT segment isn't) and, independently,
                 * more bytes than the segment/file actually has left.
                 * Confirmed as a real, serious heap-buffer-OVERFLOW WRITE
                 * via fuzzing this function this session -- `total`
                 * unchecked against `huffval[256]`'s size let a crafted
                 * DHT segment write arbitrarily far past it. Reject
                 * outright (safe-degrade convention used throughout this
                 * file) rather than silently clamping, since a clamped
                 * total no longer matches what build_huffman_table()
                 * below assumes about the table's own internal
                 * consistency. */
                if (total > 256 || p + total > seg_end || p + total > len) { ok = 0; break; }
                for (i = 0; i < total; i++) t->huffval[i] = data[p++];
                build_huffman_table(t);
                t->valid = 1;
            }
        } else if (marker == 0xC0 || marker == 0xC1) { /* SOF0 baseline / SOF1 extended-sequential -- same decode path */
            long p = seg_start;
            /* 1 (precision) + 2 (height) + 2 (width) + 1 (num_comps) before
             * the per-component loop even starts -- nothing here checked
             * that against the segment's real length before reading it
             * (same missing-bounds-check pattern as DQT/DHT had, fixed
             * above). */
            if (p + 6 > seg_end) { ok = 0; break; }
            p++; /* precision, assumed 8 */
            height = ((int)data[p] << 8) | data[p + 1]; p += 2;
            width = ((int)data[p] << 8) | data[p + 1]; p += 2;
            num_comps = data[p++];
            if (num_comps != 1 && num_comps != 3) { ok = 0; break; } /* CMYK/4-component: out of scope */
            if (num_comps > JPEG_MAX_COMPONENTS) { ok = 0; break; }
            if (p + 3L * num_comps > seg_end) { ok = 0; break; } /* 3 bytes/component: id, samp nibbles, quant_id */
            int i;
            for (i = 0; i < num_comps; i++) {
                comps[i].id = data[p++];
                comps[i].h_samp = (data[p] >> 4) & 0x0F;
                comps[i].v_samp = data[p] & 0x0F;
                p++;
                comps[i].quant_id = data[p++];
                /* `quant_id` is a full attacker-controlled BYTE (0-255),
                 * not masked at all, indexed into `quant[]` (only
                 * JPEG_MAX_QUANT_TABLES=4 entries) both a few lines below
                 * (h_samp/v_samp sanity) and, much later, in the main MCU
                 * decode loop's "quant[c->quant_id].valid" check --
                 * confirmed as a real out-of-bounds READ via fuzzing this
                 * function this session. Reject outright rather than
                 * mask-and-hope: a masked-but-still-wrong quant_id would
                 * just silently use the WRONG quantization table instead
                 * of failing cleanly. */
                if (comps[i].quant_id >= JPEG_MAX_QUANT_TABLES) { ok = 0; break; }
                if (comps[i].h_samp < 1 || comps[i].h_samp > 4 || comps[i].v_samp < 1 || comps[i].v_samp > 4) { ok = 0; break; }
            }
            if (!ok) break;
            have_sof = 1;
        } else if (marker == 0xC2 || marker == 0xC3 || (marker >= 0xC5 && marker <= 0xCF && marker != 0xC8)) {
            /* Progressive (SOF2), lossless, arithmetic-coded, or any other
             * SOF variant this decoder doesn't implement: out of scope,
             * see this file's own header comment. */
            ok = 0; break;
        } else if (marker == 0xDD) { /* DRI */
            if (seg_start + 2 > seg_end) { ok = 0; break; }
            restart_interval = ((int)data[seg_start] << 8) | data[seg_start + 1];
        } else if (marker == 0xDA) { /* SOS -- entropy-coded data follows, handled below */
            if (!have_sof) { ok = 0; break; }
            /* Same reasoning/cap as img_decode_png.c's/img_decode_gif.c's
             * own identical fix -- width/height are a raw 2-byte-each
             * attacker-controlled claim from SOF0 (up to 65535 each,
             * unchecked before this point), and drive comps[i].plane_w *
             * comps[i].plane_h allocations a bit further down (which can
             * be even LARGER than width*height once each component's own
             * sampling factor and MCU rounding are applied). */
            if (width <= 0 || height <= 0 || width > 20000 || height > 20000 || (long)width * (long)height > 100000000L) { ok = 0; break; }
            long p = seg_start;
            if (p + 1 > seg_end) { ok = 0; break; }
            int ns = data[p++];
            if (p + 2L * ns > seg_end) { ok = 0; break; }
            int i;
            for (i = 0; i < ns; i++) {
                int cs = data[p++];
                int tables = data[p++];
                int ci;
                for (ci = 0; ci < num_comps; ci++) {
                    if (comps[ci].id == cs) {
                        /* Masked to 4 bits (0-15) by "& 0x0F" already, but
                         * dc_huff[]/ac_huff[] only have JPEG_MAX_HUFF_
                         * TABLES=4 entries -- 4-15 is still out of bounds.
                         * Confirmed as a real out-of-bounds READ via
                         * fuzzing this function this session (the same
                         * finding as quant_id's own fix above, same fix
                         * shape: reject outright, don't silently
                         * re-clamp into a table that's just wrong). */
                        int dc_id = (tables >> 4) & 0x0F, ac_id = tables & 0x0F;
                        if (dc_id >= JPEG_MAX_HUFF_TABLES || ac_id >= JPEG_MAX_HUFF_TABLES) { ok = 0; break; }
                        comps[ci].dc_huff_id = dc_id;
                        comps[ci].ac_huff_id = ac_id;
                    }
                }
                if (!ok) break;
            }
            if (!ok) break;
            /* Ss/Se/AhAl (3 bytes): baseline always 0,63,0 -- not read, seg_end already tells us where the header ends. */

            int max_h = 1, max_v = 1;
            for (i = 0; i < num_comps; i++) { if (comps[i].h_samp > max_h) max_h = comps[i].h_samp; if (comps[i].v_samp > max_v) max_v = comps[i].v_samp; }
            int mcu_w = 8 * max_h, mcu_h = 8 * max_v;
            int mcus_x = (width + mcu_w - 1) / mcu_w;
            int mcus_y = (height + mcu_h - 1) / mcu_h;

            for (i = 0; i < num_comps; i++) {
                comps[i].plane_w = mcus_x * comps[i].h_samp * 8;
                comps[i].plane_h = mcus_y * comps[i].v_samp * 8;
                /* A malformed JPEG with more than one SOS marker re-enters
                 * this block, reallocating ->plane without ever freeing
                 * whichever buffer it already pointed to -- a real memory
                 * leak found via fuzzing this session (a long-running SQW
                 * process decoding many images over time could accumulate
                 * these). */
                free(comps[i].plane);
                comps[i].plane = (unsigned char *)malloc((size_t)comps[i].plane_w * (size_t)comps[i].plane_h);
                if (!comps[i].plane) { ok = 0; }
                comps[i].dc_pred = 0;
            }
            if (!ok) break;
            have_scan = 1;

            JBitReader br; memset(&br, 0, sizeof(br));
            br.data = data; br.len = len; br.pos = seg_end;

            int mcus_done = 0, mx, my;
            for (my = 0; my < mcus_y && ok; my++) {
                for (mx = 0; mx < mcus_x && ok; mx++) {
                    for (i = 0; i < num_comps; i++) {
                        JComponent *c = &comps[i];
                        int by, bx;
                        for (by = 0; by < c->v_samp; by++) {
                            for (bx = 0; bx < c->h_samp; bx++) {
                                unsigned char block8x8[64];
                                if (!dc_huff[c->dc_huff_id].valid || !ac_huff[c->ac_huff_id].valid || !quant[c->quant_id].valid) { ok = 0; break; }
                                jpeg_decode_block(&br, &dc_huff[c->dc_huff_id], &ac_huff[c->ac_huff_id], &quant[c->quant_id], &c->dc_pred, block8x8);
                                int ox = mx * c->h_samp * 8 + bx * 8;
                                int oy = my * c->v_samp * 8 + by * 8;
                                int ry;
                                for (ry = 0; ry < 8; ry++) memcpy(c->plane + (size_t)(oy + ry) * c->plane_w + ox, block8x8 + ry * 8, 8);
                            }
                            if (!ok) break;
                        }
                        if (!ok) break;
                    }
                    mcus_done++;
                    if (ok && restart_interval > 0 && mcus_done % restart_interval == 0 && !(my == mcus_y - 1 && mx == mcus_x - 1)) {
                        br.bitcount = 0; /* discard partial byte before the restart marker */
                        for (i = 0; i < num_comps; i++) comps[i].dc_pred = 0;
                        while (br.pos + 1 < br.len && !(br.data[br.pos] == 0xFF && br.data[br.pos + 1] >= 0xD0 && br.data[br.pos + 1] <= 0xD7)) br.pos++;
                        if (br.pos + 1 < br.len) br.pos += 2;
                    }
                }
            }
            pos = br.pos;
            continue; /* pos already advanced past the scan; skip the seg_end jump below */
        }
        /* APPn, COM, and anything else: no special handling needed, just
         * skip via seg_end below. */
        pos = seg_end;
    }

    if (ok && have_sof && have_scan && width > 0 && height > 0 && num_comps > 0) {
        int max_h = 1, max_v = 1, i;
        for (i = 0; i < num_comps; i++) { if (comps[i].h_samp > max_h) max_h = comps[i].h_samp; if (comps[i].v_samp > max_v) max_v = comps[i].v_samp; }
        unsigned char *rgba = (unsigned char *)malloc((size_t)width * (size_t)height * 4);
        int x, y;
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                unsigned char *outp = rgba + ((size_t)y * width + x) * 4;
                if (num_comps == 1) {
                    JComponent *c = &comps[0];
                    int sx = x * c->h_samp / max_h, sy = y * c->v_samp / max_v;
                    unsigned char yv = c->plane[(size_t)sy * c->plane_w + sx];
                    outp[0] = outp[1] = outp[2] = yv; outp[3] = 255;
                } else {
                    JComponent *cy = &comps[0], *cb = &comps[1], *cr = &comps[2];
                    int sxy = x * cy->h_samp / max_h, syy = y * cy->v_samp / max_v;
                    int sxb = x * cb->h_samp / max_h, syb = y * cb->v_samp / max_v;
                    int sxr = x * cr->h_samp / max_h, syr = y * cr->v_samp / max_v;
                    int Y = cy->plane[(size_t)syy * cy->plane_w + sxy];
                    int Cb = cb->plane[(size_t)syb * cb->plane_w + sxb] - 128;
                    int Cr = cr->plane[(size_t)syr * cr->plane_w + sxr] - 128;
                    outp[0] = (unsigned char)clampi((int)(Y + 1.402 * Cr + 0.5), 0, 255);
                    outp[1] = (unsigned char)clampi((int)(Y - 0.344136 * Cb - 0.714136 * Cr + 0.5), 0, 255);
                    outp[2] = (unsigned char)clampi((int)(Y + 1.772 * Cb + 0.5), 0, 255);
                    outp[3] = 255;
                }
            }
        }
        *out_rgba = rgba; *out_w = width; *out_h = height;
    } else {
        ok = 0;
    }

    { int i; for (i = 0; i < JPEG_MAX_COMPONENTS; i++) if (comps[i].plane) free(comps[i].plane); }
    free(quant); free(dc_huff); free(ac_huff); free(comps);
    return ok ? 1 : 0;
}

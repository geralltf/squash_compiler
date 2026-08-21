/* Implementation of img_decode_gif.h -- see that header's own top comment
 * for scope. GIF format: https://www.w3.org/Graphics/GIF/spec-gif89a.txt --
 * a Logical Screen Descriptor (+ optional Global Color Table), then a
 * sequence of blocks (Graphic Control Extension / other extensions / Image
 * Descriptor + Local Color Table + LZW-compressed pixel data) until a
 * trailer byte. No external dependency for the LZW decompression itself
 * (unlike PNG's zlib bind) -- GIF's own LZW variant (variable code width,
 * 2..12 bits, growing by 1 whenever the dictionary fills the current
 * width, with an explicit Clear Code the encoder can also emit early) is
 * simple enough to hand-roll directly and is NOT the same algorithm as
 * zlib/DEFLATE's LZ77+Huffman, so there's nothing to reuse from
 * img_decode_png.c here. */
#include "img_decode_gif.h"
#include <stdlib.h>
#include <string.h>

static unsigned int le16(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }

typedef struct { unsigned char *buf; long len, cap; } GifByteBuf;

static void gbuf_append(GifByteBuf *b, const unsigned char *data, long n) {
    if (b->len + n > b->cap) {
        long newcap = b->cap ? b->cap * 2 : 256;
        while (newcap < b->len + n) newcap *= 2;
        b->buf = (unsigned char *)realloc(b->buf, (size_t)newcap);
        b->cap = newcap;
    }
    memcpy(b->buf + b->len, data, (size_t)n);
    b->len += n;
}

/* Reads GIF sub-blocks starting at data[pos] (pointing at a size byte),
 * concatenating their payloads into `out` until a zero-size terminator.
 * Returns the byte position right after the terminator, or -1 if the data
 * runs out first (truncated file). */
static long gif_read_subblocks(const unsigned char *data, long pos, long len, GifByteBuf *out) {
    for (;;) {
        if (pos >= len) return -1;
        int n = data[pos++];
        if (n == 0) return pos;
        if (pos + n > len) return -1;
        gbuf_append(out, data + pos, n);
        pos += n;
    }
}

/* Same sub-block walk as gif_read_subblocks(), but discards the payload --
 * for extension blocks (comment/plain-text/application) this decoder
 * doesn't otherwise use. */
static long gif_skip_subblocks(const unsigned char *data, long pos, long len) {
    for (;;) {
        if (pos >= len) return -1;
        int n = data[pos++];
        if (n == 0) return pos;
        pos += n;
        if (pos > len) return -1;
    }
}

/* GIF's own LZW: decodes `packed` (the concatenated sub-block bytes of one
 * image's data) into `out` (a pre-allocated width*height-byte index
 * buffer). Dictionary entries are stored as a prefix-chain (dict_prefix/
 * dict_suffix, both sized for the full 12-bit code space) rather than
 * literal byte strings -- the standard technique: a code's full pixel-index
 * string is reconstructed by walking prefix pointers back to a literal
 * root code (< clear_code) and reversing, done here via `stack`. Tolerant
 * of a stream that runs out or hits End-Of-Information before filling
 * `out` completely (leaves the remainder at whatever memset() left it,
 * typically 0) -- some real-world encoders' output is imperfect and every
 * major browser still renders it rather than discarding the whole image,
 * so this decoder does the same instead of failing the entire frame. */
static int gif_lzw_decode(const unsigned char *packed, long packed_len, int min_code_size,
                           unsigned char *out, long out_len) {
    if (min_code_size < 2 || min_code_size > 8) return 0;
    int clear_code = 1 << min_code_size;
    int end_code = clear_code + 1;
    int code_size = min_code_size + 1;
    int next_code = end_code + 1;

    int *dict_prefix = (int *)malloc(sizeof(int) * 4096);
    unsigned char *dict_suffix = (unsigned char *)malloc(4096);
    unsigned char *stack = (unsigned char *)malloc(4096);
    if (!dict_prefix || !dict_suffix || !stack) { free(dict_prefix); free(dict_suffix); free(stack); return 0; }

    long bitpos = 0;
    long total_bits = packed_len * 8;
    long out_pos = 0;
    int old_code = -1;
    int ok = 1;

    while (out_pos < out_len) {
        if (code_size > 12 || bitpos + code_size > total_bits) break;

        int code = 0, bit;
        for (bit = 0; bit < code_size; bit++) {
            long byte_idx = bitpos >> 3;
            int bit_idx = (int)(bitpos & 7);
            int b = (packed[byte_idx] >> bit_idx) & 1;
            code |= (b << bit);
            bitpos++;
        }

        if (code == clear_code) {
            next_code = end_code + 1;
            code_size = min_code_size + 1;
            old_code = -1;
            continue;
        }
        if (code == end_code) break;

        if (old_code == -1) {
            if (code >= clear_code) { ok = 0; break; } /* first code after a clear must be a literal */
            out[out_pos++] = (unsigned char)code;
            old_code = code;
            continue;
        }

        int expand_code, append_first = 0;
        if (code < next_code) {
            expand_code = code;
        } else if (code == next_code) {
            expand_code = old_code; /* KwKwK case: oldcode's string + oldcode's own first byte */
            append_first = 1;
        } else {
            ok = 0; break; /* invalid code: corrupt stream */
        }

        int stack_top = 0;
        int c = expand_code;
        while (c >= clear_code + 2) {
            if (stack_top >= 4096) { ok = 0; break; }
            stack[stack_top++] = dict_suffix[c];
            c = dict_prefix[c];
        }
        if (!ok) break;
        if (stack_top >= 4096) { ok = 0; break; }
        stack[stack_top++] = (unsigned char)c; /* literal root, pushed last -> emitted first below */

        unsigned char first_byte = stack[stack_top - 1];

        int si;
        for (si = stack_top - 1; si >= 0 && out_pos < out_len; si--) out[out_pos++] = stack[si];
        if (append_first && out_pos < out_len) out[out_pos++] = first_byte;

        if (next_code < 4096) {
            dict_prefix[next_code] = old_code;
            dict_suffix[next_code] = first_byte;
            next_code++;
            if (next_code == (1 << code_size) && code_size < 12) code_size++;
        }
        old_code = code;
    }

    free(dict_prefix); free(dict_suffix); free(stack);
    return (ok && out_pos > 0) ? 1 : 0;
}

int sqw_gif_decode(const unsigned char *data, long len, SqwGifFrame **out_frames, int *out_frame_count, int *out_w, int *out_h) {
    if (len < 13) return 0;
    if (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0) return 0;

    int screen_w = (int)le16(data + 6);
    int screen_h = (int)le16(data + 8);
    unsigned char lsd_packed = data[10];
    int gct_flag = (lsd_packed & 0x80) != 0;
    int gct_entries = gct_flag ? (1 << ((lsd_packed & 0x07) + 1)) : 0;
    if (screen_w <= 0 || screen_h <= 0) return 0;

    long pos = 13;
    unsigned char *gct = NULL;
    if (gct_flag) {
        long gct_bytes = (long)gct_entries * 3;
        if (pos + gct_bytes > len) return 0;
        gct = (unsigned char *)malloc((size_t)gct_bytes);
        memcpy(gct, data + pos, (size_t)gct_bytes);
        pos += gct_bytes;
    }

    size_t canvas_bytes = (size_t)screen_w * (size_t)screen_h * 4;
    unsigned char *canvas = (unsigned char *)malloc(canvas_bytes);
    memset(canvas, 0, canvas_bytes); /* fully transparent -- see this file's own comment above sqw_gif_decode's body */
    unsigned char *canvas_backup = NULL; /* disposal method 3 (restore-to-previous) only */

    SqwGifFrame *frames = (SqwGifFrame *)malloc(sizeof(SqwGifFrame) * SQW_GIF_MAX_FRAMES);
    int frame_count = 0;

    /* Pending Graphic Control Extension state -- applies to exactly the
     * NEXT Image Descriptor, then reset (see the bottom of the 0x2C case). */
    int gce_delay_cs = 0, gce_transparent_flag = 0, gce_transparent_index = -1, gce_disposal = 0, have_gce = 0;

    while (pos < len && frame_count < SQW_GIF_MAX_FRAMES) {
        unsigned char block = data[pos++];
        if (block == 0x3B) break; /* trailer */

        if (block == 0x21) {
            if (pos >= len) break;
            unsigned char label = data[pos++];
            if (label == 0xF9) {
                if (pos >= len) break;
                int bsize = data[pos++];
                if (bsize < 4 || pos + bsize > len) break;
                unsigned char gpacked = data[pos];
                gce_disposal = (gpacked >> 2) & 0x07;
                gce_transparent_flag = gpacked & 0x01;
                gce_delay_cs = (int)le16(data + pos + 1);
                gce_transparent_index = data[pos + 3];
                have_gce = 1;
                pos += bsize;
                long np = gif_skip_subblocks(data, pos, len);
                if (np < 0) break;
                pos = np;
            } else {
                long np = gif_skip_subblocks(data, pos, len);
                if (np < 0) break;
                pos = np;
            }
            continue;
        }

        if (block == 0x2C) {
            if (pos + 9 > len) break;
            int img_left = (int)le16(data + pos);
            int img_top = (int)le16(data + pos + 2);
            int img_w = (int)le16(data + pos + 4);
            int img_h = (int)le16(data + pos + 6);
            unsigned char ipacked = data[pos + 8];
            pos += 9;

            int lct_flag = (ipacked & 0x80) != 0;
            int interlace_flag = (ipacked & 0x40) != 0;
            int lct_entries = lct_flag ? (1 << ((ipacked & 0x07) + 1)) : 0;
            unsigned char *lct = NULL;
            if (lct_flag) {
                long lct_bytes = (long)lct_entries * 3;
                if (pos + lct_bytes > len) break;
                lct = (unsigned char *)malloc((size_t)lct_bytes);
                memcpy(lct, data + pos, (size_t)lct_bytes);
                pos += lct_bytes;
            }
            const unsigned char *color_table = lct ? lct : gct;
            int color_table_entries = lct ? lct_entries : gct_entries;

            if (pos >= len || img_w <= 0 || img_h <= 0) { free(lct); break; }
            int min_code_size = data[pos++];

            GifByteBuf packed_buf; memset(&packed_buf, 0, sizeof(packed_buf));
            long np = gif_read_subblocks(data, pos, len, &packed_buf);
            if (np < 0) { free(lct); free(packed_buf.buf); break; }
            pos = np;

            unsigned char *indices = (unsigned char *)malloc((size_t)img_w * (size_t)img_h);
            memset(indices, 0, (size_t)img_w * (size_t)img_h);
            int lok = gif_lzw_decode(packed_buf.buf, packed_buf.len, min_code_size, indices, (long)img_w * img_h);
            free(packed_buf.buf);

            if (lok && color_table) {
                unsigned char *rows = indices;
                unsigned char *deint = NULL;
                if (interlace_flag) {
                    /* GIF interlacing: 4 passes, rows 0/4/8.., 4/12/20..,
                     * 2/6/10.., 1/3/5/7.. -- the LZW output is in THAT
                     * order, redistribute into real top-to-bottom order. */
                    deint = (unsigned char *)malloc((size_t)img_w * (size_t)img_h);
                    static const int starts[4] = { 0, 4, 2, 1 };
                    static const int steps[4]  = { 8, 8, 4, 2 };
                    int pass, y, src_row = 0;
                    for (pass = 0; pass < 4; pass++) {
                        for (y = starts[pass]; y < img_h; y += steps[pass]) {
                            memcpy(deint + (size_t)y * img_w, indices + (size_t)src_row * img_w, (size_t)img_w);
                            src_row++;
                        }
                    }
                    rows = deint;
                }

                if (gce_disposal == 3) {
                    if (!canvas_backup) canvas_backup = (unsigned char *)malloc(canvas_bytes);
                    memcpy(canvas_backup, canvas, canvas_bytes);
                }

                int yy, xx;
                for (yy = 0; yy < img_h; yy++) {
                    int cy = img_top + yy;
                    if (cy < 0 || cy >= screen_h) continue;
                    for (xx = 0; xx < img_w; xx++) {
                        int cx = img_left + xx;
                        if (cx < 0 || cx >= screen_w) continue;
                        unsigned char idx = rows[(size_t)yy * img_w + xx];
                        if (gce_transparent_flag && idx == gce_transparent_index) continue;
                        if (idx >= color_table_entries) continue;
                        unsigned char *dst = canvas + ((size_t)cy * screen_w + cx) * 4;
                        dst[0] = color_table[idx * 3 + 0];
                        dst[1] = color_table[idx * 3 + 1];
                        dst[2] = color_table[idx * 3 + 2];
                        dst[3] = 255;
                    }
                }

                unsigned char *snap = (unsigned char *)malloc(canvas_bytes);
                memcpy(snap, canvas, canvas_bytes);
                frames[frame_count].rgba = snap;
                frames[frame_count].delay_cs = have_gce ? gce_delay_cs : 0;
                frame_count++;

                /* Disposal applied AFTER the snapshot, as the base the
                 * NEXT frame composites onto. */
                if (gce_disposal == 2) {
                    int ry, rx;
                    for (ry = 0; ry < img_h; ry++) {
                        int cy = img_top + ry;
                        if (cy < 0 || cy >= screen_h) continue;
                        for (rx = 0; rx < img_w; rx++) {
                            int cx = img_left + rx;
                            if (cx < 0 || cx >= screen_w) continue;
                            unsigned char *dst = canvas + ((size_t)cy * screen_w + cx) * 4;
                            dst[0] = dst[1] = dst[2] = dst[3] = 0;
                        }
                    }
                } else if (gce_disposal == 3 && canvas_backup) {
                    memcpy(canvas, canvas_backup, canvas_bytes);
                }

                free(deint);
            }

            free(indices);
            free(lct);
            have_gce = 0; gce_delay_cs = 0; gce_transparent_flag = 0; gce_transparent_index = -1; gce_disposal = 0;
            continue;
        }

        /* Unrecognized block type: stop rather than risk misinterpreting
         * the rest of the file as garbage -- keep whatever frames were
         * already decoded (safe partial-decode degrade). */
        break;
    }

    free(gct);
    free(canvas);
    free(canvas_backup);

    if (frame_count == 0) { free(frames); return 0; }

    *out_frames = frames;
    *out_frame_count = frame_count;
    *out_w = screen_w;
    *out_h = screen_h;
    return 1;
}

void sqw_gif_frames_free(SqwGifFrame *frames, int count) {
    int i;
    for (i = 0; i < count; i++) free(frames[i].rgba);
    free(frames);
}

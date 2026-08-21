/* Implementation of img_decode_png.h -- see that header's own top comment
 * for scope. PNG chunk format: https://www.w3.org/TR/png/ -- each chunk is
 * a 4-byte big-endian length, a 4-byte ASCII type, `length` bytes of data,
 * then a 4-byte CRC (not verified here: a corrupt/truncated chunk either
 * fails to decompress/unfilter cleanly below, which this function already
 * treats as a plain decode failure, or it doesn't and the image just
 * renders with whatever bytes were actually sent -- no different from any
 * other lossy-on-corruption image decoder, and CRC verification buys
 * nothing this project's threat model needs beyond that). */
#include "img_decode_png.h"
#include "include/zlib.h"
#include <stdlib.h>
#include <string.h>

static unsigned int be32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

typedef struct {
    unsigned char *buf;
    long len, cap;
} SqwByteBuf;

static void bytebuf_append(SqwByteBuf *b, const unsigned char *data, long n) {
    if (b->len + n > b->cap) {
        long newcap = b->cap ? b->cap * 2 : 4096;
        while (newcap < b->len + n) newcap *= 2;
        b->buf = (unsigned char *)realloc(b->buf, (size_t)newcap);
        b->cap = newcap;
    }
    memcpy(b->buf + b->len, data, (size_t)n);
    b->len += n;
}

static unsigned char paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return (unsigned char)a;
    if (pb <= pc) return (unsigned char)b;
    return (unsigned char)c;
}

/* Undoes PNG's per-scanline filtering IN PLACE on `raw` (height rows of
 * 1 filter-type byte + `stride` data bytes each, exactly what inflate()
 * produced). `bpp` is bytes-per-pixel (1/2/3/4 -- used for the Sub/Up/
 * Average/Paeth "byte `bpp` positions back" reference, per spec: NOT the
 * bit depth). */
static void unfilter(unsigned char *raw, int width, int height, int bpp, int stride) {
    unsigned char *prev = NULL;
    int y;
    for (y = 0; y < height; y++) {
        unsigned char *row = raw + (long)y * (stride + 1);
        int filter_type = row[0];
        unsigned char *data = row + 1;
        int x;
        for (x = 0; x < stride; x++) {
            int a = (x >= bpp) ? data[x - bpp] : 0;
            int b = prev ? prev[x] : 0;
            int c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            switch (filter_type) {
                case 0: break; /* None */
                case 1: data[x] = (unsigned char)(data[x] + a); break; /* Sub */
                case 2: data[x] = (unsigned char)(data[x] + b); break; /* Up */
                case 3: data[x] = (unsigned char)(data[x] + ((a + b) / 2)); break; /* Average */
                case 4: data[x] = (unsigned char)(data[x] + paeth(a, b, c)); break; /* Paeth */
                default: break; /* unknown filter type: leave bytes as-is, safe degrade */
            }
        }
        prev = data;
    }
    (void)width;
}

int sqw_png_decode(const unsigned char *data, long len, unsigned char **out_rgba, int *out_w, int *out_h) {
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (len < 8 || memcmp(data, sig, 8) != 0) return 0;

    int width = 0, height = 0, bit_depth = 0, color_type = -1, interlace = 0;
    /* Heap-allocated, not stack arrays -- this project has a known squash
     * codegen bug with large stack-resident locals in a single function
     * (see e.g. dom.c's own comment on pp_expand's char[8192] locals);
     * palette+trns alone is 1KB, and this function already has plenty of
     * other locals (SqwByteBuf, z_stream, several pointers) stacked on top
     * of that, confirmed to actually trigger it here (a real, reproducible
     * segfault with these two as plain stack arrays instead). */
    unsigned char *palette = (unsigned char *)malloc(256 * 3);
    int palette_count = 0;
    unsigned char *trns = (unsigned char *)malloc(256);
    int trns_count = 0;
    int have_ihdr = 0;

    SqwByteBuf idat;
    memset(&idat, 0, sizeof(idat));

    long pos = 8;
    while (pos + 8 <= len) {
        unsigned int clen = be32(data + pos);
        if (pos + 8 + (long)clen + 4 > len) break; /* truncated chunk: stop, use what we have */
        const unsigned char *ctype = data + pos + 4;
        const unsigned char *cdata = data + pos + 8;

        if (memcmp(ctype, "IHDR", 4) == 0 && clen >= 13) {
            width = (int)be32(cdata + 0);
            height = (int)be32(cdata + 4);
            bit_depth = cdata[8];
            color_type = cdata[9];
            interlace = cdata[12];
            have_ihdr = 1;
        } else if (memcmp(ctype, "PLTE", 4) == 0) {
            palette_count = (int)(clen / 3);
            if (palette_count > 256) palette_count = 256;
            memcpy(palette, cdata, (size_t)palette_count * 3);
        } else if (memcmp(ctype, "tRNS", 4) == 0 && color_type == 3) {
            trns_count = (int)clen;
            if (trns_count > 256) trns_count = 256;
            memcpy(trns, cdata, (size_t)trns_count);
        } else if (memcmp(ctype, "IDAT", 4) == 0) {
            bytebuf_append(&idat, cdata, (long)clen);
        } else if (memcmp(ctype, "IEND", 4) == 0) {
            break;
        }
        pos += 8 + (long)clen + 4;
    }

    if (!have_ihdr || width <= 0 || height <= 0 || idat.len == 0) { free(idat.buf); free(palette); free(trns); return 0; }
    if (bit_depth != 8 || interlace != 0) { free(idat.buf); free(palette); free(trns); return 0; } /* out of scope, see header */

    int channels;
    switch (color_type) {
        case 0: channels = 1; break; /* gray */
        case 2: channels = 3; break; /* rgb */
        case 3: channels = 1; break; /* palette (index) */
        case 4: channels = 2; break; /* gray+alpha */
        case 6: channels = 4; break; /* rgba */
        default: free(idat.buf); free(palette); free(trns); return 0;
    }
    if (color_type == 3 && palette_count == 0) { free(idat.buf); free(palette); free(trns); return 0; }

    int stride = width * channels;
    long raw_size = (long)height * (stride + 1);

    unsigned char *raw = (unsigned char *)malloc((size_t)raw_size);
    if (!raw) { free(idat.buf); free(palette); free(trns); return 0; }

    z_stream *strm = (z_stream *)malloc(sizeof(z_stream));
    memset(strm, 0, sizeof(*strm));
    int init_r = inflateInit(strm);
    if (init_r != Z_OK) { free(idat.buf); free(raw); free(palette); free(trns); free(strm); return 0; }
    strm->next_in = idat.buf;
    strm->avail_in = (uInt)idat.len;
    strm->next_out = raw;
    strm->avail_out = (uInt)raw_size;

    int inflate_ok = 0;
    for (;;) {
        int r = inflate(strm, Z_NO_FLUSH);
        if (r == Z_STREAM_END) { inflate_ok = 1; break; }
        if (r != Z_OK) break; /* Z_DATA_ERROR / Z_BUF_ERROR / etc: corrupt or truncated stream */
        if (strm->avail_out == 0) { inflate_ok = (strm->avail_in == 0); break; } /* filled exactly raw_size: done */
        if (strm->avail_in == 0) break; /* out of input before Z_STREAM_END: truncated */
    }
    inflateEnd(strm);
    free(strm);
    free(idat.buf);
    if (!inflate_ok) { free(raw); free(palette); free(trns); return 0; }

    unfilter(raw, width, height, channels, stride);

    unsigned char *rgba = (unsigned char *)malloc((size_t)width * (size_t)height * 4);
    if (!rgba) { free(raw); free(palette); free(trns); return 0; }

    int y, x;
    for (y = 0; y < height; y++) {
        const unsigned char *row = raw + (long)y * (stride + 1) + 1;
        unsigned char *out = rgba + (long)y * width * 4;
        for (x = 0; x < width; x++) {
            unsigned char r, g, b, a;
            switch (color_type) {
                case 0: r = g = b = row[x]; a = 255; break;
                case 2: r = row[x * 3 + 0]; g = row[x * 3 + 1]; b = row[x * 3 + 2]; a = 255; break;
                case 3: {
                    int idx = row[x];
                    if (idx >= palette_count) idx = 0;
                    r = palette[idx * 3 + 0]; g = palette[idx * 3 + 1]; b = palette[idx * 3 + 2];
                    a = (idx < trns_count) ? trns[idx] : 255;
                    break;
                }
                case 4: r = g = b = row[x * 2 + 0]; a = row[x * 2 + 1]; break;
                case 6: r = row[x * 4 + 0]; g = row[x * 4 + 1]; b = row[x * 4 + 2]; a = row[x * 4 + 3]; break;
                default: r = g = b = 0; a = 255; break;
            }
            out[x * 4 + 0] = r; out[x * 4 + 1] = g; out[x * 4 + 2] = b; out[x * 4 + 3] = a;
        }
    }

    free(raw);
    free(palette);
    free(trns);
    *out_rgba = rgba;
    *out_w = width;
    *out_h = height;
    return 1;
}

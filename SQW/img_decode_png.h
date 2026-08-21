#ifndef SQW_IMG_DECODE_PNG_H
#define SQW_IMG_DECODE_PNG_H

/* Hand-written PNG decoder (chunk parsing + PNG defiltering), decompressing
 * via the real system zlib's inflate() (include/zlib.h, "-lz") rather than
 * libpng -- see SQW/image_cache.h's own top comment and this project's plan
 * doc for why: libpng's error-handling API is setjmp-based, and the squash
 * compiler has no evidence of supporting setjmp/longjmp anywhere in this
 * codebase, while zlib's raw inflate() is plain return-code error handling,
 * completely independent of that concern.
 *
 * Scope (see this file's own top comment in img_decode_png.c for the exact
 * per-color-type/bit-depth support): 8-bit-per-channel, non-interlaced PNGs
 * only (grayscale, grayscale+alpha, RGB, RGBA, and palette+tRNS) -- the
 * overwhelmingly common case for real web images. 16-bit depth, <8-bit
 * (1/2/4-bit palette) and Adam7-interlaced PNGs are out of scope for now
 * (sqw_png_decode returns 0, same as any other malformed/unsupported input
 * -- the caller's image cache treats that as FAILED, not a crash). */

/* Decodes `len` bytes at `data` (a full PNG file's bytes, exactly what a
 * successful HTTP(S) fetch handed back) into a freshly malloc'd RGBA8
 * buffer (`*out_rgba`, width*height*4 bytes, row-major top-to-bottom, no
 * padding) and `*out_w`/`*out_h`. Returns 1 on success (caller owns
 * *out_rgba, must free() it) or 0 on any parse/decompress/unsupported-
 * feature failure (out_rgba/out_w/out_h left untouched). */
int sqw_png_decode(const unsigned char *data, long len,
                    unsigned char **out_rgba, int *out_w, int *out_h);

#endif /* SQW_IMG_DECODE_PNG_H */

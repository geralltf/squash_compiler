#ifndef SQW_IMG_DECODE_JPEG_H
#define SQW_IMG_DECODE_JPEG_H

/* Hand-written baseline (SOF0, sequential DCT, Huffman-coded) JPEG decoder
 * -- no external dependency (no libjpeg), matching this project's PNG/GIF
 * decoders (see img_decode_png.h/img_decode_gif.h's own top comments for
 * why: this project hand-rolls its own formats rather than binding
 * external C libraries whose error-handling APIs don't fit squash's
 * compiler feature set, e.g. libjpeg's own setjmp-based error API). Covers
 * marker parsing (DQT/SOF0/DHT/SOS/DRI + restart markers), baseline
 * Huffman entropy decoding, dequantization, a direct (non-fast) 2D IDCT,
 * and YCbCr->RGB with box-filter chroma upsampling for 4:4:4/4:2:2/4:2:0
 * subsampling -- the overwhelming majority of real-world JPEGs.
 *
 * Explicitly OUT of scope (returns 0, same safe-degrade-to-FAILED as any
 * other unsupported input): progressive JPEG (SOF2), arithmetic coding,
 * 12-bit samples, and CMYK/4-component images. Progressive is a real,
 * known gap for some web images (see the project plan's own note on this)
 * -- a documented follow-up, not silently mis-decoded. */

int sqw_jpeg_decode(const unsigned char *data, long len,
                     unsigned char **out_rgba, int *out_w, int *out_h);

#endif /* SQW_IMG_DECODE_JPEG_H */

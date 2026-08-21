#ifndef SQW_IMG_DECODE_GIF_H
#define SQW_IMG_DECODE_GIF_H

/* Hand-written GIF87a/GIF89a decoder: logical screen descriptor, global/
 * local color tables, the Graphic Control Extension (per-frame delay +
 * disposal method + transparent color index), and GIF's own variable-
 * code-width LZW compression -- no external dependency (unlike PNG's zlib
 * bind, GIF's LZW needs no third-party library at all, see this file's own
 * top comment in img_decode_gif.c for the algorithm). Every frame is
 * decoded AND composited onto a running logical-screen-sized canvas per
 * the GIF89a spec's own disposal-method rules (see img_decode_gif.c's
 * sqw_gif_decode() for exactly how), so every returned frame is already a
 * full, ready-to-display RGBA8 image at the same (canvas) width/height --
 * callers never need to know about a frame's own (possibly smaller,
 * possibly offset) sub-rectangle.
 *
 * Scope: baseline GIF87a/89a, including interlaced frames and all three
 * disposal methods that matter in practice (none/do-not-dispose, restore-
 * to-background, restore-to-previous). Per-frame local color tables are
 * supported. Frames past a generous cap are dropped (SQW_GIF_MAX_FRAMES)
 * rather than growing without bound on a pathological/malicious file. */

#define SQW_GIF_MAX_FRAMES 256

typedef struct {
    unsigned char *rgba; /* width*height*4 bytes (canvas size), caller-owned */
    int delay_cs;         /* frame display duration in 1/100 sec; 0 -> treat as a sane default (see image_cache.c) */
} SqwGifFrame;

/* Decodes `len` bytes at `data` (a full GIF file's bytes). On success,
 * returns 1, fills *out_frames (malloc'd array of *out_frame_count
 * SqwGifFrame, each with its own malloc'd ->rgba -- caller must free each
 * ->rgba and then the array itself, or use sqw_gif_frames_free()) and
 * *out_w/*out_h (the logical screen / canvas size, same for every frame).
 * Returns 0 on any parse failure or if the file has zero image frames
 * (out params untouched). */
int sqw_gif_decode(const unsigned char *data, long len,
                    SqwGifFrame **out_frames, int *out_frame_count, int *out_w, int *out_h);

void sqw_gif_frames_free(SqwGifFrame *frames, int count);

#endif /* SQW_IMG_DECODE_GIF_H */

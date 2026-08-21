/* Implementation of image_cache.h -- see that header's own top comment for
 * the overall PENDING -> DECODED -> UPLOADED/FAILED state machine and why
 * this is process-wide static state rather than a struct instance. #include-d
 * directly from sqw_main.c (after net_client.c, img_decode_png.c, and
 * img_decode_gif.c, all of which this file depends on) rather than compiled
 * as its own translation unit -- same single-TU convention as every other
 * SQW/*.c file, see Makefile.SQW.linux's own comment on why.
 *
 * Every decoded image -- whether a single-frame PNG or a multi-frame GIF --
 * is stored the same way: as an array of frames (SqwImgFrame), each with
 * its own delay (0/never-advances for a static image). This is simpler
 * than special-casing "static" vs "animated" entries throughout the rest
 * of this file, at the cost of a 1-entry array for the overwhelmingly
 * common static-image case. */
#include "image_cache.h"
#include "net_client.h"
#include "img_decode_png.h"
#include "img_decode_gif.h"
#include "img_decode_jpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum { SQW_IMGST_EMPTY = 0, SQW_IMGST_PENDING, SQW_IMGST_DECODED, SQW_IMGST_UPLOADED, SQW_IMGST_FAILED } SqwImgState;

typedef struct {
    unsigned char *pixels; /* owned until uploaded, then freed and left NULL */
    int delay_cs;           /* GIF delay in 1/100s; 0 for a static (single-frame) image */
    SqwTexture tex;
    VkDescriptorSet descriptorSet;
} SqwImgFrame;

typedef struct {
    char url[SQW_IMAGE_URL_MAX];
    SqwImgState state;
    SqwNetResult *fetch;
    int w, h;
    SqwImgFrame *frames;
    int frame_count;
    int uploaded_count; /* frames[0..uploaded_count) have a live texture; drives the DECODED->UPLOADED transition */
    int cur_frame;       /* which frame sqw_image_cache_get_handle() currently returns */
    double frame_started_at; /* CLOCK_MONOTONIC seconds when cur_frame became current; frame_count<=1 -> unused */
} SqwImgEntry;

static SqwVkContext *g_img_vk = NULL;
static SqwImageRenderer *g_img_renderer = NULL;
static SqwImgEntry g_img_entries[SQW_IMAGE_CACHE_MAX];
static int g_img_count = 0;

static double sqw_monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void sqw_img_entry_free_frames(SqwImgEntry *e) {
    int i;
    for (i = 0; i < e->frame_count; i++) {
        SqwImgFrame *f = &e->frames[i];
        if (f->pixels) free(f->pixels);
        if (i < e->uploaded_count) sqw_vk_destroy_texture(g_img_vk, &f->tex);
    }
    free(e->frames);
    e->frames = NULL;
    e->frame_count = 0;
    e->uploaded_count = 0;
}

void sqw_image_cache_init(SqwVkContext *vk, SqwImageRenderer *renderer) {
    g_img_vk = vk;
    g_img_renderer = renderer;
    g_img_count = 0;
    memset(g_img_entries, 0, sizeof(g_img_entries));
}

void sqw_image_cache_reset(void) {
    int i;
    for (i = 0; i < g_img_count; i++) {
        SqwImgEntry *e = &g_img_entries[i];
        if (e->state == SQW_IMGST_PENDING && e->fetch) sqw_net_result_abandon(e->fetch);
        sqw_img_entry_free_frames(e);
    }
    /* One bulk vkResetDescriptorPool() rather than freeing each entry's
     * set individually -- see sqw_image_renderer_reset_pool()'s own
     * comment; every set any entry held is now invalid, matching the
     * textures/pixels this loop just freed above. */
    sqw_image_renderer_reset_pool(g_img_vk, g_img_renderer);
    g_img_count = 0;
    memset(g_img_entries, 0, sizeof(g_img_entries));
}

void sqw_image_cache_request(const char *url) {
    if (!url || !url[0]) return;
    int i;
    for (i = 0; i < g_img_count; i++) {
        if (strcmp(g_img_entries[i].url, url) == 0) return; /* already known: idempotent */
    }
    /* Silently drop past the cap, same "generous, not tight, no crash on
     * overflow" convention as e.g. SQW_TEXT_MAX_GLYPHS/SQW_RENDERER_MAX_IMM_RECTS. */
    if (g_img_count >= SQW_IMAGE_CACHE_MAX) return;
    SqwImgEntry *e = &g_img_entries[g_img_count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->url, url, sizeof(e->url) - 1);
    e->state = SQW_IMGST_PENDING;
    fprintf(stderr, "SQW: fetching image %s ...\n", url); fflush(stdout);
    e->fetch = sqw_net_fetch_async(url);
}

int sqw_image_cache_poll(void) {
    int changed = 0;
    double now = sqw_monotonic_seconds();
    int i;
    for (i = 0; i < g_img_count; i++) {
        SqwImgEntry *e = &g_img_entries[i];
        if (e->state == SQW_IMGST_PENDING && e->fetch) {
            pthread_mutex_lock(&e->fetch->mutex);
            int ready = e->fetch->ready;
            int success = e->fetch->success;
            char *body = e->fetch->body;
            long body_len = e->fetch->body_len;
            pthread_mutex_unlock(&e->fetch->mutex);
            if (!ready) continue;

            int ok = 0, w = 0, h = 0;
            SqwImgFrame *frames = NULL;
            int frame_count = 0;

            if (success && body && body_len >= 8) {
                static const unsigned char png_sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
                if (memcmp(body, png_sig, 8) == 0) {
                    unsigned char *rgba = NULL;
                    if (sqw_png_decode((const unsigned char *)body, body_len, &rgba, &w, &h)) {
                        frames = (SqwImgFrame *)malloc(sizeof(SqwImgFrame));
                        memset(frames, 0, sizeof(SqwImgFrame));
                        frames[0].pixels = rgba;
                        frames[0].delay_cs = 0;
                        frame_count = 1;
                        ok = 1;
                    }
                } else if (memcmp(body, "GIF87a", 6) == 0 || memcmp(body, "GIF89a", 6) == 0) {
                    SqwGifFrame *gframes = NULL;
                    int gcount = 0;
                    if (sqw_gif_decode((const unsigned char *)body, body_len, &gframes, &gcount, &w, &h)) {
                        frames = (SqwImgFrame *)malloc(sizeof(SqwImgFrame) * (size_t)gcount);
                        memset(frames, 0, sizeof(SqwImgFrame) * (size_t)gcount);
                        int fi;
                        for (fi = 0; fi < gcount; fi++) {
                            frames[fi].pixels = gframes[fi].rgba; /* ownership moves to frames[] */
                            /* A 0 (or absurdly small) GIF delay is common in
                             * real-world files and means "as fast as
                             * possible" per spec, but every major browser
                             * clamps it to a sane minimum instead of
                             * spinning -- same clamp here. */
                            frames[fi].delay_cs = gframes[fi].delay_cs > 2 ? gframes[fi].delay_cs : 10;
                        }
                        free(gframes); /* array only -- each ->rgba ownership already moved above */
                        frame_count = gcount;
                        ok = 1;
                    }
                } else if (body_len >= 3 && (unsigned char)body[0] == 0xFF && (unsigned char)body[1] == 0xD8 && (unsigned char)body[2] == 0xFF) {
                    unsigned char *rgba = NULL;
                    if (sqw_jpeg_decode((const unsigned char *)body, body_len, &rgba, &w, &h)) {
                        frames = (SqwImgFrame *)malloc(sizeof(SqwImgFrame));
                        memset(frames, 0, sizeof(SqwImgFrame));
                        frames[0].pixels = rgba;
                        frames[0].delay_cs = 0;
                        frame_count = 1;
                        ok = 1;
                    }
                }
            }

            if (ok) {
                e->frames = frames;
                e->frame_count = frame_count;
                e->uploaded_count = 0;
                e->cur_frame = 0;
                e->frame_started_at = now;
                e->w = w; e->h = h;
                e->state = SQW_IMGST_DECODED;
                changed = 1;
                fprintf(stderr, "SQW: decoded image %s (%dx%d, %d frame%s)\n", e->url, w, h, frame_count, frame_count == 1 ? "" : "s"); fflush(stdout);
            } else {
                e->state = SQW_IMGST_FAILED;
                fprintf(stderr, "SQW: image fetch/decode failed: %s\n", e->url); fflush(stdout);
            }
            sqw_net_result_free(e->fetch);
            e->fetch = NULL;
            continue;
        }

        if (e->state == SQW_IMGST_DECODED) {
            /* Upload every not-yet-uploaded frame -- for a small/typical
             * animated GIF this is cheap enough to do all at once rather
             * than spreading across frames; see image_cache.h's own
             * comment on why "decode all frames, render animated" (the
             * user's own choice) implies having every frame's texture
             * ready before the first one is ever drawn. */
            int all_ok = 1;
            while (e->uploaded_count < e->frame_count) {
                SqwImgFrame *f = &e->frames[e->uploaded_count];
                if (!sqw_vk_create_texture_rgba8(g_img_vk, f->pixels, (uint32_t)e->w, (uint32_t)e->h, &f->tex)) { all_ok = 0; break; }
                f->descriptorSet = sqw_image_renderer_bind_texture(g_img_vk, g_img_renderer, &f->tex);
                if (!f->descriptorSet) { sqw_vk_destroy_texture(g_img_vk, &f->tex); all_ok = 0; break; }
                free(f->pixels);
                f->pixels = NULL;
                e->uploaded_count++;
            }
            if (!all_ok) {
                sqw_img_entry_free_frames(e);
                e->state = SQW_IMGST_FAILED;
            } else if (e->uploaded_count == e->frame_count) {
                e->state = SQW_IMGST_UPLOADED;
                e->frame_started_at = now;
            }
            continue;
        }

        if (e->state == SQW_IMGST_UPLOADED && e->frame_count > 1) {
            /* Animate: advance cur_frame by real elapsed time against each
             * frame's own delay -- a while loop (not a single if) so a
             * long stall (window drag, a slow frame) catches up through
             * several short-delay frames instead of visibly freezing on
             * one. */
            while (now - e->frame_started_at >= (double)e->frames[e->cur_frame].delay_cs / 100.0) {
                e->frame_started_at += (double)e->frames[e->cur_frame].delay_cs / 100.0;
                e->cur_frame = (e->cur_frame + 1) % e->frame_count;
            }
        }
    }
    return changed;
}

int sqw_image_cache_get_size(const char *url, float *w, float *h) {
    if (!url || !url[0]) return 0;
    int i;
    for (i = 0; i < g_img_count; i++) {
        if (strcmp(g_img_entries[i].url, url) == 0) {
            SqwImgEntry *e = &g_img_entries[i];
            if (e->state == SQW_IMGST_DECODED || e->state == SQW_IMGST_UPLOADED) {
                *w = (float)e->w; *h = (float)e->h;
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

int sqw_image_cache_get_handle(const char *url, SqwImageHandle *out) {
    if (!url || !url[0]) return 0;
    int i;
    for (i = 0; i < g_img_count; i++) {
        SqwImgEntry *e = &g_img_entries[i];
        if (strcmp(e->url, url) == 0 && e->state == SQW_IMGST_UPLOADED) {
            SqwImgFrame *f = &e->frames[e->cur_frame];
            out->tex = &f->tex;
            out->descriptorSet = f->descriptorSet;
            return 1;
        }
    }
    return 0;
}

void sqw_image_cache_shutdown(void) {
    sqw_image_cache_reset();
    g_img_vk = NULL;
}

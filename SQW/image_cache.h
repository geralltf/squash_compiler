#ifndef SQW_IMAGE_CACHE_H
#define SQW_IMAGE_CACHE_H
#include "vk_context.h"
#include "image_renderer_vk.h"

/* Max distinct image URLs held at once -- generous for any real page this
 * project targets, silently drops requests past it (see
 * sqw_image_cache_request()'s own comment). Keep in sync with
 * image_renderer_vk.h's SQW_IMAGE_MAX_TEXTURES (one descriptor set per
 * entry here that reaches UPLOADED). */
#define SQW_IMAGE_CACHE_MAX 128
#define SQW_IMAGE_URL_MAX 384

/* Page-image cache: fetch (net_client.c) -> decode (img_decode_png.c and
 * friends) -> upload (sqw_vk_create_texture_rgba8, vk_context.c) pipeline
 * for <img src="..."> and CSS "background-image: url(...)". A single
 * process-wide cache (static state in image_cache.c, not a struct the
 * caller allocates) -- SQW is a single-window, single-page-at-a-time app,
 * so this matches the project's existing convention of plain file-static
 * globals for exactly-one-instance state (e.g. font_atlas.h's
 * g_font_atlas_alpha, text_spirv.h's g_text_vert_spv). Callers refer to
 * entries purely by URL string, never by pointer/handle, so DomNode (see
 * dom.h's img_url/css_bg_image_url) only ever needs to carry the URL.
 *
 * PENDING (fetch in flight) -> DECODED (pixels in memory, real size known)
 * -> UPLOADED (has a live SqwTexture) -- or FAILED at any point (bad
 * network fetch, or bytes that don't decode as a supported format), which
 * is a terminal, silent-degrade state: sqw_image_cache_get_* just keep
 * returning "not available" for it forever, same as this project's other
 * "safe degradation on bad input" conventions (see css.h's own comment on
 * unsupported selectors). */

void sqw_image_cache_init(SqwVkContext *vk, SqwImageRenderer *renderer);

/* Frees every uploaded texture and decoded/in-flight entry and forgets all
 * URLs -- call once per page navigation (before scanning the new DOM for
 * <img>/background-image URLs) so images from the PREVIOUS page don't leak
 * forever. Safe to call even with fetches still in flight (uses
 * sqw_net_result_abandon(), not sqw_net_result_free() -- see net_client.h's
 * own comment on why that distinction matters). */
void sqw_image_cache_reset(void);

/* Idempotent: if `url` is already known (any state), does nothing; else
 * starts a new async fetch for it. `url` must already be an ABSOLUTE URL
 * (http:// or https://) -- see sqw_resolve_image_urls() in sqw_main.c for
 * how <img src>/CSS url() are turned into one before reaching here. A
 * non-http(s) URL (e.g. a bare local file path, which this project's
 * <img>/background-image support does not fetch) is simply never
 * requested by the caller in the first place. */
void sqw_image_cache_request(const char *url);

/* Polls every PENDING fetch and DECODED-but-not-yet-uploaded entry, AND
 * advances every UPLOADED animated (multi-frame GIF) entry's current
 * frame by real elapsed wall-clock time against each frame's own GIF
 * delay -- call once per frame. Returns 1 if any entry's real (decoded)
 * size became known for the first time this call -- the caller should
 * re-run layout_compute() in that case, since layout.c sizes <img> boxes
 * from sqw_image_cache_get_size() and the placeholder size it used before
 * this point is now stale. Returns 0 otherwise (nothing changed, only
 * texture uploads happened, or only an animation frame advanced -- none of
 * those change any box's size, a GIF's every frame shares one canvas size
 * by construction, see img_decode_gif.h). */
int sqw_image_cache_poll(void);

/* Fills *w/*h with the decoded pixel size and returns 1 if `url` is known
 * and at least DECODED (real size available); returns 0 (leaves *w/*h
 * untouched) if `url` is empty, unknown, still PENDING, or FAILED. */
int sqw_image_cache_get_size(const char *url, float *w, float *h);

/* A texture plus the descriptor set already bound to it (see
 * image_renderer_vk.h's own comment on why each texture needs its own
 * set) -- everything sqw_image_draw_quad() needs for one draw call. */
typedef struct {
    SqwTexture *tex;
    VkDescriptorSet descriptorSet;
} SqwImageHandle;

/* Fills *out and returns 1 if `url` is known and UPLOADED, else returns 0
 * (leaves *out untouched). Callers must not hold `out->tex`/
 * `out->descriptorSet` across a sqw_image_cache_reset() (a new page
 * navigation), which frees/resets both. */
int sqw_image_cache_get_handle(const char *url, SqwImageHandle *out);

void sqw_image_cache_shutdown(void);

#endif /* SQW_IMAGE_CACHE_H */

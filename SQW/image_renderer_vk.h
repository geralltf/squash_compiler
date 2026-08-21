#ifndef SQW_IMAGE_RENDERER_VK_H
#define SQW_IMAGE_RENDERER_VK_H
/* Vulkan textured-quad renderer for decoded <img>/CSS background-image
 * pixels (SQW/image_cache.c) -- modeled closely on
 * SQW/text_renderer_vk.c's glyph renderer (same descriptor-set-layout/
 * pipeline/persistently-mapped-vertex-buffer technique), but samples an
 * RGBA texture DIRECTLY (image_spirv.h's shader, not text_spirv.h's -- see
 * that header's own comment: the glyph shader instead tints a
 * single-channel alpha mask by a push/vertex color, wrong for full-color
 * images) and, unlike the one-atlas-for-everything font case, must bind a
 * DIFFERENT texture per draw (one per decoded image). That's why this is
 * an immediate "bind descriptor set + draw 6 vertices now" API (matching
 * renderer_vk.c's sqw_renderer_draw_rect() ad-hoc-rect pattern) rather than
 * a "queue everything, one flush draw call" API like the glyph renderer:
 * updating ONE shared descriptor set's binding partway through a command
 * buffer's recording does NOT retroactively change what an
 * already-recorded-but-not-yet-submitted vkCmdDraw earlier in that same
 * buffer will sample at GPU-execution time (a real Vulkan footgun, not
 * just a style choice) -- so each distinct texture gets its OWN
 * descriptor set (sqw_image_renderer_bind_texture()), allocated from a
 * pool sized for this project's SQW_IMAGE_CACHE_MAX images, once per
 * texture at upload time, not once per draw. */
#include "vk_context.h"

#define SQW_IMAGE_MAX_QUADS_PER_FRAME 512
/* Descriptor pool capacity: one set per distinct texture ever bound via
 * sqw_image_renderer_bind_texture() since the last sqw_image_renderer_
 * reset_pool() -- keep in sync with SQW_IMAGE_CACHE_MAX (image_cache.h),
 * the max number of images the cache itself will ever hold at once. */
#define SQW_IMAGE_MAX_TEXTURES 128

typedef struct {
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool      descriptorPool;
    VkPipelineLayout      pipelineLayout;
    VkPipeline            pipeline;
    VkBuffer              vertexBuffer;
    VkDeviceMemory        vertexMemory;
    void                 *mapped;
    int                   cursor; /* quads drawn so far this frame, each own 6-vertex slot */
} SqwImageRenderer;

int sqw_image_renderer_init(SqwVkContext *vk, SqwImageRenderer *ir);

/* Call once per frame before any sqw_image_draw_quad() calls (resets the
 * per-frame vertex-slot cursor -- same reason sqw_renderer_draw() resets
 * SqwRenderer::imm_cursor at the top of every frame, see its own comment). */
void sqw_image_renderer_begin_frame(SqwImageRenderer *ir);

/* Allocates and writes ONE descriptor set bound to `tex` (view+sampler) --
 * call once per texture, right after sqw_vk_create_texture_rgba8()
 * succeeds (see image_cache.c's poll loop), never per draw/per frame.
 * Returns VK_NULL_HANDLE on failure (pool exhausted -- SQW_IMAGE_CACHE_MAX
 * images' worth of sets is the pool's fixed size, see image_cache.h's own
 * cap). */
VkDescriptorSet sqw_image_renderer_bind_texture(SqwVkContext *vk, SqwImageRenderer *ir, SqwTexture *tex);

/* Frees every descriptor set allocated by sqw_image_renderer_bind_texture()
 * since the last reset (vkResetDescriptorPool) -- call from
 * sqw_image_cache_reset() on page navigation, alongside that function's own
 * texture teardown, so stale sets from the previous page's images don't
 * accumulate in the pool. */
void sqw_image_renderer_reset_pool(SqwVkContext *vk, SqwImageRenderer *ir);

/* Draws one (x,y,w,h) quad (screen pixel coordinates, top-left origin)
 * sampling `descriptorSet`'s bound texture, modulated by (r,g,b,a) --
 * (1,1,1,1) for a plain opaque image; alpha < 1 for a future CSS opacity
 * hook. Immediate: binds pipeline + this specific descriptor set and
 * issues its own vkCmdDraw right away (see this header's own top comment
 * for why, unlike the glyph renderer's queue-then-flush). Silently drops
 * the quad past SQW_IMAGE_MAX_QUADS_PER_FRAME, same generous-cap
 * convention as SQW_TEXT_MAX_GLYPHS. */
void sqw_image_draw_quad(SqwVkContext *vk, SqwImageRenderer *ir, VkCommandBuffer cmd, VkDescriptorSet descriptorSet,
    float x, float y, float w, float h, float r, float g, float b, float a,
    float viewport_w, float viewport_h);

#endif /* SQW_IMAGE_RENDERER_VK_H */

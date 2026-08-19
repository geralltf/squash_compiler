#ifndef SQW_TEXT_RENDERER_VK_H
#define SQW_TEXT_RENDERER_VK_H
/* Vulkan bitmap-font glyph renderer: samples the atlas baked by
 * font_atlas.h (DejaVu Sans, ASCII 32..126) as textured quads, one draw
 * call per frame for every glyph queued since the last flush -- same
 * "rebuild the whole persistently-mapped vertex buffer, one vkCmdDraw"
 * technique renderer_vk.c already uses for flat-colored boxes, just with
 * UV + alpha-blended sampling instead of a flat push-constant color. */
#include "vk_context.h"

#define SQW_TEXT_MAX_GLYPHS 8192

typedef struct {
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool      descriptorPool;
    VkDescriptorSet       descriptorSet;
    VkPipelineLayout      pipelineLayout;
    VkPipeline            pipeline;
    VkBuffer              vertexBuffer;
    VkDeviceMemory        vertexMemory;
    void                 *mapped;
    int                   vcount;   /* vertices queued since last flush */
    SqwTexture            atlas;
} SqwTextRenderer;

int  sqw_text_renderer_init(SqwVkContext *vk, SqwTextRenderer *tr);

/* sqw_text_glyph_advance()/sqw_text_measure() (scale is a multiplier on
 * the atlas's own baked pixel size -- 1.0 renders at the baked size) now
 * live in text_metrics.h, shared with layout.c's word-wrap and with no
 * Vulkan dependency of their own -- #include "text_metrics.h" directly
 * wherever they're needed instead of through this header. */

/* Queues one glyph quad (screen pixel coordinates, top-left origin,
 * (0,0)..(viewport_w,viewport_h)) into the pending vertex buffer -- call
 * sqw_text_renderer_flush() once per frame after all sqw_text_draw_*
 * calls to actually submit them. Silently drops glyphs past
 * SQW_TEXT_MAX_GLYPHS (a generous cap for this project's scale, not
 * expected to be hit by any real test page). */
void sqw_text_draw_char(SqwTextRenderer *tr, float x, float y, char ch, float scale,
    float r, float g, float b, float a, float viewport_w, float viewport_h);
void sqw_text_draw_string(SqwTextRenderer *tr, float x, float y, const char *s, int len, float scale,
    float r, float g, float b, float a, float viewport_w, float viewport_h);

void sqw_text_renderer_flush(SqwVkContext *vk, SqwTextRenderer *tr, VkCommandBuffer cmd, float viewport_w, float viewport_h);

#endif /* SQW_TEXT_RENDERER_VK_H */

#ifndef SQW_RENDERER_VK_H
#define SQW_RENDERER_VK_H
#include "vk_context.h"
#include "layout.h"

#define SQW_RENDERER_MAX_BOXES 512

typedef struct {
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexMemory;
    void *mapped;
    int max_vertices;
} SqwRenderer;

/* Colored-quad renderer: reuses the exact vertex-color triangle shader
 * pair already proven working with squash's Vulkan codegen
 * (SDL3_Build/scratch/triangle.vert/.frag, precompiled to
 * triangle_spirv.h) -- it takes a per-vertex NDC position + RGB color and
 * a push-constant rotation angle, which is all a flat-shaded 2D box needs
 * (angle pinned to 0). No new shader compilation required. */
int sqw_renderer_init(SqwVkContext *vk, SqwRenderer *r);
void sqw_renderer_draw(SqwVkContext *vk, SqwRenderer *r, VkCommandBuffer cmd,
                        LayoutList *boxes, float viewport_w, float viewport_h,
                        float scroll_x, float scroll_y);

/* One ad-hoc flat-colored rect, drawn with its own bind+draw call --
 * used for decoration that isn't part of the LayoutList (anchor
 * underlines, scrollbar track/thumb), where the color can depend on
 * state (e.g. DomNode->visited) that changes frame to frame without a
 * re-layout. Not batched with sqw_renderer_draw()'s single big draw call
 * -- fine at this project's scale (a handful of these per frame, not
 * hundreds). */
void sqw_renderer_draw_rect(SqwVkContext *vk, SqwRenderer *r, VkCommandBuffer cmd,
                             float x, float y, float w, float h,
                             float red, float green, float blue,
                             float viewport_w, float viewport_h);

#endif /* SQW_RENDERER_VK_H */

#ifndef SQW_RENDERER_VK_H
#define SQW_RENDERER_VK_H
#include "vk_context.h"
#include "layout.h"

#define SQW_RENDERER_MAX_BOXES 512
/* Ad-hoc rects (sqw_renderer_draw_rect -- toolbar chrome, scrollbar
 * track/thumb, anchor underlines) get their OWN reserved region of the
 * vertex buffer, entirely separate from the SQW_RENDERER_MAX_BOXES region
 * sqw_renderer_draw() uses for the page's own layout boxes -- see that
 * function's own comment on why: writing every ad-hoc rect to the same 6
 * vertex slots and issuing vkCmdDraw per call, with no fence/barrier
 * between calls, meant only the LAST such rect drawn in the whole frame
 * ever actually appeared once the GPU caught up (every earlier draw call
 * in the same command buffer ended up reading the same, since-overwritten
 * memory) -- a real bug that predates the Back button, just never
 * surfaced clearly before because the one rect that "won" (whichever was
 * drawn last) happened to be plausible-looking toolbar chrome. */
#define SQW_RENDERER_MAX_IMM_RECTS 128

typedef struct {
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexMemory;
    void *mapped;
    int max_vertices;
    /* Write cursor into the ad-hoc-rect region (see SQW_RENDERER_MAX_IMM_RECTS
     * above), 0..SQW_RENDERER_MAX_IMM_RECTS -- reset to 0 once per frame by
     * sqw_renderer_draw(), which every frame calls before any
     * sqw_renderer_draw_rect() calls (see main()'s own draw-call order). */
    int imm_cursor;
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

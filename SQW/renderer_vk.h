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
                        LayoutList *boxes, float viewport_w, float viewport_h);

#endif /* SQW_RENDERER_VK_H */

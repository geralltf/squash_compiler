#ifndef SQW_STYLED_RENDERER_VK_H
#define SQW_STYLED_RENDERER_VK_H
#include "vk_context.h"

/* Renders ONE rectangle per draw call (unlike renderer_vk.c's batched
 * sqw_renderer_draw(), which draws every plain box in one vertex buffer/
 * one vkCmdDraw) -- real per-box parameters (radius/shadow/transform)
 * naturally want their own push-constant block per call, and this
 * project's own real-CSS-feature scale (a handful of elements on a page
 * actually use border-radius/box-shadow/transform, not hundreds) makes
 * "own draw call per styled box" the same acceptable tradeoff
 * sqw_renderer_draw_rect() already documents for anchor underlines/
 * scrollbars. A real hand-written GLSL shader (styled_rect.vert/.frag,
 * compiled via glslang -- see styled_rect_spirv.h's own comment), not a
 * CPU-side approximation: rounded corners and the drop shadow are both
 * evaluated per-fragment via a signed-distance-field rounded-rect formula,
 * and CSS "transform" (2D affine: translate/scale/rotate/skew/matrix) is
 * applied to vertex positions in the vertex shader around the box's own
 * center (real CSS's default transform-origin), while the fragment
 * shader's own SDF math runs in UNTRANSFORMED local box space (passed
 * through as a separate varying) so a rotated rounded-rect's corners stay
 * correctly round instead of distorting with the rotation. */
typedef struct {
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexMemory;
    void *mapped;
    /* Write cursor, 0..SQW_STYLED_MAX_BOXES -- reset to 0 once per frame
     * by sqw_styled_renderer_begin_frame(). Each sqw_styled_renderer_draw()
     * call gets its OWN reserved 6-vertex slot, NOT vertex 0 every time --
     * see SQW_STYLED_MAX_BOXES's own comment for why reusing one shared
     * slot across multiple draw calls in the same frame is a real,
     * previously-confirmed bug in this exact codebase (renderer_vk.c's own
     * SQW_RENDERER_MAX_IMM_RECTS comment), not a hypothetical one. */
    int cursor;
} SqwStyledRenderer;

/* Generous, not tight -- a real page might have a handful of border-
 * radius/box-shadow/transform elements, not hundreds; matches this
 * project's own established sizing convention for similar per-frame caps
 * (SQW_RENDERER_MAX_IMM_RECTS, SQW_IMAGE_CACHE_MAX). */
#define SQW_STYLED_MAX_BOXES 128

int sqw_styled_renderer_init(SqwVkContext *vk, SqwStyledRenderer *r);
/* Resets the per-frame draw cursor -- MUST be called once per frame,
 * before any sqw_styled_renderer_draw() call that frame (see
 * sqw_draw_frame()'s own call site), the same "reset once per frame"
 * pattern sqw_renderer_draw() and sqw_image_renderer_begin_frame() both
 * already use for their own per-frame draw-call pools. */
void sqw_styled_renderer_begin_frame(SqwStyledRenderer *r);

typedef struct {
    /* Box rect, screen pixel space (scroll already applied by the
     * caller), same convention every other draw-pass in sqw_main.c uses. */
    float x, y, w, h;
    /* border-radius, px. Clamped internally to min(w,h)/2 (real CSS's own
     * "can't exceed half the box" rule) -- 0 draws a plain sharp-cornered
     * rect. */
    float radius;
    float fill_r, fill_g, fill_b, fill_a;
    /* box-shadow: a single shadow (real CSS allows a comma-separated
     * list; this engine models exactly one, the common case) -- offset/
     * blur/spread all in px, matching "box-shadow: dx dy blur spread
     * color" order. has_shadow=0 skips the shadow term entirely (shadow
     * alpha forced to 0), not just zeroed parameters, so a box with no
     * shadow costs nothing extra in the fragment shader's blend math
     * beyond the one multiply. */
    int has_shadow;
    float shadow_r, shadow_g, shadow_b, shadow_a;
    float shadow_dx, shadow_dy, shadow_blur, shadow_spread;
    /* CSS "transform": a real 2D affine (2x2 matrix, column-major --
     * xa,xb is column 0, xc,xd is column 1 -- plus an extra translate
     * component for transform's own translate()/matrix() e/f terms).
     * has_transform=0 is treated as the identity matrix with zero extra
     * translate (drawn exactly as if no transform were requested at
     * all). */
    int has_transform;
    float xa, xb, xc, xd;
    float tx, ty;
} SqwStyledBox;

void sqw_styled_renderer_draw(SqwVkContext *vk, SqwStyledRenderer *r, VkCommandBuffer cmd,
                               const SqwStyledBox *box, float viewport_w, float viewport_h);

#endif /* SQW_STYLED_RENDERER_VK_H */

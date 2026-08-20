#include "renderer_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "triangle_spirv.h"

typedef struct { float x, y; float r, g, b; } SqwVertex;
typedef struct { float angle; } SqwPushConstants;

static void box_color(SqwBoxKind kind, float *r, float *g, float *b) {
    switch (kind) {
        case SQW_BOX_DIV:    *r = 0.20f; *g = 0.35f; *b = 0.65f; break; /* blue */
        case SQW_BOX_CENTER: *r = 0.55f; *g = 0.30f; *b = 0.65f; break; /* purple */
        case SQW_BOX_P:      *r = 0.25f; *g = 0.55f; *b = 0.30f; break; /* green */
        case SQW_BOX_SPAN:   *r = 0.85f; *g = 0.55f; *b = 0.15f; break; /* orange */
        case SQW_BOX_A:      *r = 0.80f; *g = 0.25f; *b = 0.35f; break; /* red/pink */
        case SQW_BOX_IMG:    *r = 0.85f; *g = 0.80f; *b = 0.20f; break; /* yellow */
        case SQW_BOX_BUTTON: *r = 0.75f; *g = 0.75f; *b = 0.78f; break; /* light gray button face */
        case SQW_BOX_PRE:    *r = 0.93f; *g = 0.93f; *b = 0.90f; break; /* pale code-block background */
        /* Real form widgets: white editable field face (text input,
         * textarea) and a plain white checkbox/radio face -- both get a
         * darker border drawn separately (see sqw_renderer_draw()'s own
         * comment) so they read as "boxes to interact with" against the
         * page background, matching default browser widget chrome. */
        case SQW_BOX_INPUT_TEXT:  *r = 1.00f; *g = 1.00f; *b = 1.00f; break;
        case SQW_BOX_TEXTAREA:    *r = 1.00f; *g = 1.00f; *b = 1.00f; break;
        case SQW_BOX_INPUT_CHECK: *r = 1.00f; *g = 1.00f; *b = 1.00f; break;
        default:             *r = 0.45f; *g = 0.45f; *b = 0.45f; break; /* gray (other) */
    }
}

int sqw_renderer_init(SqwVkContext *vk, SqwRenderer *r) {
    memset(r, 0, sizeof(*r));
    init_triangle_spirv();

    VkShaderModuleCreateInfo vsInfo;
    memset(&vsInfo, 0, sizeof(vsInfo));
    vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsInfo.codeSize = sizeof(g_triangle_vert_spv);
    vsInfo.pCode = g_triangle_vert_spv;
    VkShaderModule vsModule = NULL;
    VkResult vr = vkCreateShaderModule(vk->device, &vsInfo, NULL, &vsModule);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_renderer: vkCreateShaderModule(vs) failed vr=%d\n", (int)vr); return 0; }

    VkShaderModuleCreateInfo fsInfo;
    memset(&fsInfo, 0, sizeof(fsInfo));
    fsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fsInfo.codeSize = sizeof(g_triangle_frag_spv);
    fsInfo.pCode = g_triangle_frag_spv;
    VkShaderModule fsModule = NULL;
    vr = vkCreateShaderModule(vk->device, &fsInfo, NULL, &fsModule);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_renderer: vkCreateShaderModule(fs) failed vr=%d\n", (int)vr); return 0; }

    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding;
    binding.binding = 0;
    binding.stride = sizeof(SqwVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2];
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = 8;

    VkPipelineVertexInputStateCreateInfo vinState;
    memset(&vinState, 0, sizeof(vinState));
    vinState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vinState.vertexBindingDescriptionCount = 1;
    vinState.pVertexBindingDescriptions = &binding;
    vinState.vertexAttributeDescriptionCount = 2;
    vinState.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo iaState;
    memset(&iaState, 0, sizeof(iaState));
    iaState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    /* Viewport/scissor COUNTS only here (no actual VkViewport/VkRect2D
     * values) -- both are VK_DYNAMIC_STATE now, set once per frame in
     * sqw_vk_begin_frame() from the current swapchain extent, so this
     * pipeline survives sqw_vk_recreate_swapchain() resizes unchanged. */
    VkPipelineViewportStateCreateInfo vpState;
    memset(&vpState, 0, sizeof(vpState));
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.scissorCount = 1;

    VkDynamicState dynStates[2];
    dynStates[0] = VK_DYNAMIC_STATE_VIEWPORT;
    dynStates[1] = VK_DYNAMIC_STATE_SCISSOR;
    VkPipelineDynamicStateCreateInfo dynState;
    memset(&dynState, 0, sizeof(dynState));
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dynStates;

    VkPipelineRasterizationStateCreateInfo rsState;
    memset(&rsState, 0, sizeof(rsState));
    rsState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsState.polygonMode = VK_POLYGON_MODE_FILL;
    rsState.cullMode = 0;
    rsState.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rsState.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msState;
    memset(&msState, 0, sizeof(msState));
    msState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttach;
    memset(&blendAttach, 0, sizeof(blendAttach));
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cbState;
    memset(&cbState, 0, sizeof(cbState));
    cbState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbState.attachmentCount = 1;
    cbState.pAttachments = &blendAttach;

    VkPushConstantRange pcRange;
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(SqwPushConstants);

    VkPipelineLayoutCreateInfo plInfo;
    memset(&plInfo, 0, sizeof(plInfo));
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;

    vr = vkCreatePipelineLayout(vk->device, &plInfo, NULL, &r->pipelineLayout);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_renderer: vkCreatePipelineLayout failed vr=%d\n", (int)vr); return 0; }

    VkGraphicsPipelineCreateInfo gpInfo;
    memset(&gpInfo, 0, sizeof(gpInfo));
    gpInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpInfo.stageCount = 2;
    gpInfo.pStages = stages;
    gpInfo.pVertexInputState = &vinState;
    gpInfo.pInputAssemblyState = &iaState;
    gpInfo.pViewportState = &vpState;
    gpInfo.pRasterizationState = &rsState;
    gpInfo.pMultisampleState = &msState;
    gpInfo.pColorBlendState = &cbState;
    gpInfo.pDynamicState = &dynState;
    gpInfo.layout = r->pipelineLayout;
    gpInfo.renderPass = vk->renderPass;
    gpInfo.subpass = 0;
    gpInfo.basePipelineIndex = -1;

    vr = vkCreateGraphicsPipelines(vk->device, NULL, 1, &gpInfo, NULL, &r->pipeline);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_renderer: vkCreateGraphicsPipelines failed vr=%d\n", (int)vr); return 0; }

    /* Host-visible+coherent vertex buffer, mapped once and rewritten every
     * frame (2 triangles = 6 vertices per box) -- same technique as
     * triangle_vulkan.c's own vertex buffer, just persistently mapped
     * instead of map/write/unmap per upload. */
    r->max_vertices = (SQW_RENDERER_MAX_BOXES + SQW_RENDERER_MAX_IMM_RECTS) * 6;
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = (VkDeviceSize)(r->max_vertices * sizeof(SqwVertex));
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vr = vkCreateBuffer(vk->device, &bufInfo, NULL, &r->vertexBuffer);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_renderer: vkCreateBuffer failed vr=%d\n", (int)vr); return 0; }

    VkMemoryRequirements memReq;
    memset(&memReq, 0, sizeof(memReq));
    vkGetBufferMemoryRequirements(vk->device, r->vertexBuffer, &memReq);

    uint32_t memTypeIdx = sqw_vk_find_memory_type(vk, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memTypeIdx == 0xFFFFFFFFu) return 0;

    VkMemoryAllocateInfo allocInfo;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIdx;
    vr = vkAllocateMemory(vk->device, &allocInfo, NULL, &r->vertexMemory);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_renderer: vkAllocateMemory failed vr=%d\n", (int)vr); return 0; }
    vkBindBufferMemory(vk->device, r->vertexBuffer, r->vertexMemory, 0);
    vkMapMemory(vk->device, r->vertexMemory, 0, bufInfo.size, 0, &r->mapped);

    fprintf(stderr, "sqw_renderer: init complete (max %d boxes)\n", SQW_RENDERER_MAX_BOXES);
    return 1;
}

void sqw_renderer_draw(SqwVkContext *vk, SqwRenderer *r, VkCommandBuffer cmd,
                        LayoutList *boxes, float viewport_w, float viewport_h,
                        float scroll_x, float scroll_y) {
    /* Reset the ad-hoc-rect write cursor for the new frame -- this is
     * always the first draw call issued each frame (see main()'s own
     * draw-call order), so this is the one correct place to do it; every
     * sqw_renderer_draw_rect() call this frame (toolbar, scrollbars,
     * anchor underlines) then gets its own fresh vertex slot instead of
     * fighting over slot 0 -- see SQW_RENDERER_MAX_IMM_RECTS's own
     * comment in renderer_vk.h for the bug this fixes. */
    r->imm_cursor = 0;

    int n = boxes->count;
    if (n > SQW_RENDERER_MAX_BOXES) n = SQW_RENDERER_MAX_BOXES;

    SqwVertex *verts = (SqwVertex *)r->mapped;
    int vcount = 0;
    int i;
    for (i = 0; i < n; i++) {
        LayoutBox *b = &boxes->boxes[i];
        /* SQW_BOX_TEXT boxes are drawn separately by the glyph text
         * renderer (text_renderer_vk.c) -- no flat-color rect here, or
         * every word/line would get an opaque background box behind its
         * actual glyphs. SQW_BOX_A/SPAN are plain inline text wrappers --
         * real browsers give them no background by default either (the
         * old solid-color placeholder rects made sense before real text
         * existed; now that anchor text is real glyphs colored/underlined
         * by sqw_main.c's own draw pass, a background box here would just
         * paint over/behind it). */
        if (b->kind == SQW_BOX_TEXT || b->kind == SQW_BOX_A || b->kind == SQW_BOX_SPAN) continue;
        /* Layout boxes live in CONTENT space (unaffected by scrolling --
         * see layout.h); subtract the current scroll offset here, once,
         * right at the point of converting to screen-space NDC, so
         * scrolling never requires a re-layout. */
        float bx = b->x - scroll_x;
        float by = b->y - scroll_y;
        /* pixel (top-left origin) -> Vulkan NDC (already y-down, so no flip) */
        float x0 = (bx / viewport_w) * 2.0f - 1.0f;
        float y0 = (by / viewport_h) * 2.0f - 1.0f;
        float x1 = ((bx + b->w) / viewport_w) * 2.0f - 1.0f;
        float y1 = ((by + b->h) / viewport_h) * 2.0f - 1.0f;

        float cr, cg, cb;
        /* A real CSS background-color (see css.h/css.c) always wins over
         * the built-in per-tag placeholder palette -- that palette exists
         * purely as a fallback for elements/pages with no matching CSS at
         * all (including every one of this project's own local test
         * pages, none of which carry a <style> block), not as a real
         * browser's default appearance. */
        if (b->node->css_has_bg) { cr = b->node->css_bg[0]; cg = b->node->css_bg[1]; cb = b->node->css_bg[2]; }
        else box_color(b->kind, &cr, &cg, &cb);

        SqwVertex tl, tr, bl, br;
        tl.x = x0; tl.y = y0; tl.r = cr; tl.g = cg; tl.b = cb;
        tr.x = x1; tr.y = y0; tr.r = cr; tr.g = cg; tr.b = cb;
        bl.x = x0; bl.y = y1; bl.r = cr; bl.g = cg; bl.b = cb;
        br.x = x1; br.y = y1; br.r = cr; br.g = cg; br.b = cb;

        verts[vcount++] = tl; verts[vcount++] = tr; verts[vcount++] = bl;
        verts[vcount++] = tr; verts[vcount++] = br; verts[vcount++] = bl;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline);
    VkDeviceSize offset0 = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &r->vertexBuffer, &offset0);

    SqwPushConstants pc;
    pc.angle = 0.0f;
    vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    if (vcount > 0) vkCmdDraw(cmd, vcount, 1, 0, 0);
}

void sqw_renderer_draw_rect(SqwVkContext *vk, SqwRenderer *r, VkCommandBuffer cmd,
                             float x, float y, float w, float h,
                             float red, float green, float blue,
                             float viewport_w, float viewport_h) {
    (void)vk;
    /* Own reserved slot in the ad-hoc-rect region (see
     * SQW_RENDERER_MAX_IMM_RECTS's own comment in renderer_vk.h) -- NOT
     * vertex 0 every time. Silently drops the rect (matching
     * sqw_renderer_draw()'s own clamp-not-crash convention for too many
     * layout boxes) if a single frame somehow issues more than
     * SQW_RENDERER_MAX_IMM_RECTS of these; every real caller in this
     * project draws well under a dozen per frame, so this is a generous,
     * not a tight, cap. */
    if (r->imm_cursor >= SQW_RENDERER_MAX_IMM_RECTS) return;
    int base_vertex = (SQW_RENDERER_MAX_BOXES + r->imm_cursor) * 6;
    r->imm_cursor++;

    float x0 = (x / viewport_w) * 2.0f - 1.0f;
    float y0 = (y / viewport_h) * 2.0f - 1.0f;
    float x1 = ((x + w) / viewport_w) * 2.0f - 1.0f;
    float y1 = ((y + h) / viewport_h) * 2.0f - 1.0f;

    SqwVertex *verts = (SqwVertex *)r->mapped;
    SqwVertex tl, trv, bl, br;
    tl.x = x0; tl.y = y0; tl.r = red; tl.g = green; tl.b = blue;
    trv.x = x1; trv.y = y0; trv.r = red; trv.g = green; trv.b = blue;
    bl.x = x0; bl.y = y1; bl.r = red; bl.g = green; bl.b = blue;
    br.x = x1; br.y = y1; br.r = red; br.g = green; br.b = blue;
    verts[base_vertex + 0] = tl; verts[base_vertex + 1] = trv; verts[base_vertex + 2] = bl;
    verts[base_vertex + 3] = trv; verts[base_vertex + 4] = br; verts[base_vertex + 5] = bl;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline);
    VkDeviceSize offset0 = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &r->vertexBuffer, &offset0);
    SqwPushConstants pc;
    pc.angle = 0.0f;
    vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 6, 1, (uint32_t)base_vertex, 0);
}

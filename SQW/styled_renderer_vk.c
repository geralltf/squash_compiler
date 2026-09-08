/* Implementation of styled_renderer_vk.h -- see that header's own top
 * comment. #include-d directly into sqw_main.c, same single-TU convention
 * as every other SQW/*.c file. */
#include "styled_renderer_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "styled_rect_spirv.h"

typedef struct { float x, y; } SqwStyledVertex;

/* Laid out field-by-field (all plain floats, no larger types) so the
 * offset of every member is exactly what it looks like -- no implicit
 * compiler padding to reason about -- matching styled_rect.vert/.frag's
 * own "push_constant uniform PC { ... }" block byte-for-byte (GLSL's
 * default push-constant layout rules: vec2 8-byte aligned, vec4 16-byte
 * aligned, so a float immediately before a vec4 needs explicit padding to
 * reach that 16-byte boundary -- see _pad0 below). Any accidental
 * mismatch here would silently feed the shader garbage parameters, not a
 * compile/link error, so this layout must be kept in exact sync with the
 * GLSL source (/tmp/shaders/styled_rect.vert/.frag at the time this was
 * generated -- styled_rect_spirv.h is the compiled, checked-in result). */
typedef struct {
    float centerScreenX, centerScreenY;
    float ndcScaleX, ndcScaleY;
    float xformCol0X, xformCol0Y;
    float xformCol1X, xformCol1Y;
    float extraTranslateX, extraTranslateY;
    float halfSizeX, halfSizeY;
    float radius;
    float _pad0[3];
    float fillR, fillG, fillB, fillA;
    float shadowR, shadowG, shadowB, shadowA;
    float shadowOffsetX, shadowOffsetY;
    float shadowBlur;
    float shadowSpread;
} SqwStyledPushConstants;

int sqw_styled_renderer_init(SqwVkContext *vk, SqwStyledRenderer *r) {
    memset(r, 0, sizeof(*r));
    init_styled_rect_spirv();

    VkShaderModuleCreateInfo vsInfo;
    memset(&vsInfo, 0, sizeof(vsInfo));
    vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsInfo.codeSize = sizeof(g_styled_rect_vert_spv);
    vsInfo.pCode = g_styled_rect_vert_spv;
    VkShaderModule vsModule = NULL;
    VkResult vr = vkCreateShaderModule(vk->device, &vsInfo, NULL, &vsModule);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_styled_renderer: vkCreateShaderModule(vs) failed vr=%d\n", (int)vr); return 0; }

    VkShaderModuleCreateInfo fsInfo;
    memset(&fsInfo, 0, sizeof(fsInfo));
    fsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fsInfo.codeSize = sizeof(g_styled_rect_frag_spv);
    fsInfo.pCode = g_styled_rect_frag_spv;
    VkShaderModule fsModule = NULL;
    vr = vkCreateShaderModule(vk->device, &fsInfo, NULL, &fsModule);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_styled_renderer: vkCreateShaderModule(fs) failed vr=%d\n", (int)vr); return 0; }

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
    binding.stride = sizeof(SqwStyledVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr;
    attr.location = 0; attr.binding = 0; attr.format = VK_FORMAT_R32G32_SFLOAT; attr.offset = 0;

    VkPipelineVertexInputStateCreateInfo vinState;
    memset(&vinState, 0, sizeof(vinState));
    vinState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vinState.vertexBindingDescriptionCount = 1;
    vinState.pVertexBindingDescriptions = &binding;
    vinState.vertexAttributeDescriptionCount = 1;
    vinState.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo iaState;
    memset(&iaState, 0, sizeof(iaState));
    iaState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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

    /* Real alpha blending (unlike renderer_vk.c's flat-box pipeline,
     * which is fully opaque) -- both the rounded-rect edge antialiasing
     * and the box-shadow's soft falloff are only visible if this draw
     * blends against whatever the page already painted underneath it,
     * same blend factors image_renderer_vk.c already uses for its own
     * (already proven working) alpha-blended image quads. */
    VkPipelineColorBlendAttachmentState blendAttach;
    memset(&blendAttach, 0, sizeof(blendAttach));
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable = VK_TRUE;
    blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cbState;
    memset(&cbState, 0, sizeof(cbState));
    cbState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbState.attachmentCount = 1;
    cbState.pAttachments = &blendAttach;

    VkPushConstantRange pcRange;
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(SqwStyledPushConstants);

    VkPipelineLayoutCreateInfo plInfo;
    memset(&plInfo, 0, sizeof(plInfo));
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;

    vr = vkCreatePipelineLayout(vk->device, &plInfo, NULL, &r->pipelineLayout);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_styled_renderer: vkCreatePipelineLayout failed vr=%d\n", (int)vr); return 0; }

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
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_styled_renderer: vkCreateGraphicsPipelines failed vr=%d\n", (int)vr); return 0; }

    /* SQW_STYLED_MAX_BOXES reserved 6-vertex slots, one per draw call
     * this frame -- NOT a single shared 6-vertex slot rewritten before
     * every draw call, see SqwStyledRenderer::cursor's own comment for
     * why that was a real, confirmed bug (every draw call in a frame
     * ended up reading whichever box was drawn LAST, since all the CPU-
     * side vertex writes happen before the command buffer is ever
     * submitted to the GPU). */
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = (VkDeviceSize)(SQW_STYLED_MAX_BOXES * 6 * sizeof(SqwStyledVertex));
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vr = vkCreateBuffer(vk->device, &bufInfo, NULL, &r->vertexBuffer);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_styled_renderer: vkCreateBuffer failed vr=%d\n", (int)vr); return 0; }

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
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_styled_renderer: vkAllocateMemory failed vr=%d\n", (int)vr); return 0; }
    vkBindBufferMemory(vk->device, r->vertexBuffer, r->vertexMemory, 0);
    vkMapMemory(vk->device, r->vertexMemory, 0, bufInfo.size, 0, &r->mapped);
    r->cursor = 0;

    fprintf(stderr, "sqw_styled_renderer: init complete\n"); fflush(stdout);
    return 1;
}

void sqw_styled_renderer_begin_frame(SqwStyledRenderer *r) {
    r->cursor = 0;
}

void sqw_styled_renderer_draw(SqwVkContext *vk, SqwStyledRenderer *r, VkCommandBuffer cmd,
                               const SqwStyledBox *box, float viewport_w, float viewport_h) {
    (void)vk;
    /* Own reserved slot for this call -- see SqwStyledRenderer::cursor's
     * own comment. Silently drops the box past the cap (matching every
     * other per-frame-pool cap in this project's own "generous, not
     * tight, no crash on overflow" convention) rather than wrapping back
     * to slot 0, which would reintroduce the exact overwrite bug this
     * whole cursor scheme exists to avoid. */
    if (r->cursor >= SQW_STYLED_MAX_BOXES) return;
    int base_vertex = r->cursor * 6;
    r->cursor++;

    float halfW = box->w / 2.0f;
    float halfH = box->h / 2.0f;
    float radius = box->radius;
    float maxR = halfW < halfH ? halfW : halfH;
    if (radius > maxR) radius = maxR;
    if (radius < 0.0f) radius = 0.0f;

    /* The drawn quad must cover the box PLUS however far the shadow
     * extends past it, or the shadow would just get silently clipped at
     * the box's own edge -- margin is a generous (not tight) bound: blur
     * softens the edge outward by roughly its own width, spread grows the
     * shadow rect directly, and the offset shifts the whole shadow shape;
     * +4px on top is the same antialiasing margin every box gets (the
     * SDF's own smoothstep falloff needs a little room even with no
     * shadow at all, for the rounded-corner edge itself). */
    float margin = 4.0f;
    if (box->has_shadow) {
        float ox = box->shadow_dx < 0.0f ? -box->shadow_dx : box->shadow_dx;
        float oy = box->shadow_dy < 0.0f ? -box->shadow_dy : box->shadow_dy;
        float extra = box->shadow_blur + box->shadow_spread + (ox > oy ? ox : oy);
        margin = margin + extra;
    }
    float extentX = halfW + margin;
    float extentY = halfH + margin;

    SqwStyledVertex *verts = (SqwStyledVertex *)r->mapped;
    verts[base_vertex + 0].x = -extentX; verts[base_vertex + 0].y = -extentY;
    verts[base_vertex + 1].x =  extentX; verts[base_vertex + 1].y = -extentY;
    verts[base_vertex + 2].x = -extentX; verts[base_vertex + 2].y =  extentY;
    verts[base_vertex + 3].x =  extentX; verts[base_vertex + 3].y = -extentY;
    verts[base_vertex + 4].x =  extentX; verts[base_vertex + 4].y =  extentY;
    verts[base_vertex + 5].x = -extentX; verts[base_vertex + 5].y =  extentY;

    SqwStyledPushConstants pc;
    memset(&pc, 0, sizeof(pc));
    pc.centerScreenX = box->x + halfW;
    pc.centerScreenY = box->y + halfH;
    pc.ndcScaleX = 2.0f / viewport_w;
    pc.ndcScaleY = 2.0f / viewport_h;
    if (box->has_transform) {
        pc.xformCol0X = box->xa; pc.xformCol0Y = box->xb;
        pc.xformCol1X = box->xc; pc.xformCol1Y = box->xd;
        pc.extraTranslateX = box->tx; pc.extraTranslateY = box->ty;
    } else {
        pc.xformCol0X = 1.0f; pc.xformCol0Y = 0.0f;
        pc.xformCol1X = 0.0f; pc.xformCol1Y = 1.0f;
        pc.extraTranslateX = 0.0f; pc.extraTranslateY = 0.0f;
    }
    pc.halfSizeX = halfW; pc.halfSizeY = halfH;
    pc.radius = radius;
    pc.fillR = box->fill_r; pc.fillG = box->fill_g; pc.fillB = box->fill_b; pc.fillA = box->fill_a;
    if (box->has_shadow) {
        pc.shadowR = box->shadow_r; pc.shadowG = box->shadow_g; pc.shadowB = box->shadow_b; pc.shadowA = box->shadow_a;
        pc.shadowOffsetX = box->shadow_dx; pc.shadowOffsetY = box->shadow_dy;
        pc.shadowBlur = box->shadow_blur; pc.shadowSpread = box->shadow_spread;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline);
    VkDeviceSize offset0 = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &r->vertexBuffer, &offset0);
    vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 6, 1, (uint32_t)base_vertex, 0);
}

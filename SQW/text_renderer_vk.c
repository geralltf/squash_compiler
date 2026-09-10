#include "text_renderer_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text_spirv.h"
#include "text_metrics.h"

typedef struct { float x, y, u, v; float r, g, b, a; } SqwGlyphVertex;

/* SQW_FONT_PIXEL_SIZE isn't emitted into font_atlas.h today (only the
 * baked atlas dimensions/metrics are) -- glyph advance/size values in
 * g_font_glyph_metrics[] are already absolute pixels at the size the atlas
 * was baked at, so `scale` here is a plain multiplier on those, not a
 * ratio against some nominal size. A scale of 1.0 renders each glyph at
 * exactly its baked pixel dimensions. sqw_glyph_lookup()/sqw_text_glyph_
 * advance()/sqw_text_measure() now live in text_metrics.h, shared with
 * layout.c's word-wrap. */

int sqw_text_renderer_init(SqwVkContext *vk, SqwTextRenderer *tr) {
    memset(tr, 0, sizeof(*tr));

    sqw_font_atlas_decode();
    if (!sqw_vk_create_texture_r8(vk, g_font_atlas_alpha, SQW_FONT_ATLAS_W, SQW_FONT_ATLAS_H, &tr->atlas)) {
        fprintf(stderr, "sqw_text_renderer: atlas texture creation failed\n");
        return 0;
    }

    /* --- Descriptor set layout: one combined-image-sampler, fragment stage --- */
    VkDescriptorSetLayoutBinding binding;
    memset(&binding, 0, sizeof(binding));
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslInfo;
    memset(&dslInfo, 0, sizeof(dslInfo));
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 1;
    dslInfo.pBindings = &binding;
    VkResult vr = vkCreateDescriptorSetLayout(vk->device, &dslInfo, NULL, &tr->descriptorSetLayout);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_text_renderer: vkCreateDescriptorSetLayout failed vr=%d\n", (int)vr); return 0; }

    VkDescriptorPoolSize poolSize;
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpInfo;
    memset(&dpInfo, 0, sizeof(dpInfo));
    dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpInfo.maxSets = 1;
    dpInfo.poolSizeCount = 1;
    dpInfo.pPoolSizes = &poolSize;
    vr = vkCreateDescriptorPool(vk->device, &dpInfo, NULL, &tr->descriptorPool);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_text_renderer: vkCreateDescriptorPool failed vr=%d\n", (int)vr); return 0; }

    VkDescriptorSetAllocateInfo dsaInfo;
    memset(&dsaInfo, 0, sizeof(dsaInfo));
    dsaInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsaInfo.descriptorPool = tr->descriptorPool;
    dsaInfo.descriptorSetCount = 1;
    dsaInfo.pSetLayouts = &tr->descriptorSetLayout;
    vr = vkAllocateDescriptorSets(vk->device, &dsaInfo, &tr->descriptorSet);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_text_renderer: vkAllocateDescriptorSets failed vr=%d\n", (int)vr); return 0; }

    VkDescriptorImageInfo imgInfo;
    imgInfo.sampler = tr->atlas.sampler;
    imgInfo.imageView = tr->atlas.view;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write;
    memset(&write, 0, sizeof(write));
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = tr->descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(vk->device, 1, &write, 0, NULL);

    /* --- Shaders --- */
    init_text_spirv();
    VkShaderModuleCreateInfo vsInfo;
    memset(&vsInfo, 0, sizeof(vsInfo));
    vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsInfo.codeSize = sizeof(g_text_vert_spv);
    vsInfo.pCode = g_text_vert_spv;
    VkShaderModule vsModule = NULL;
    vr = vkCreateShaderModule(vk->device, &vsInfo, NULL, &vsModule);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_text_renderer: vkCreateShaderModule(vs) failed vr=%d\n", (int)vr); return 0; }

    VkShaderModuleCreateInfo fsInfo;
    memset(&fsInfo, 0, sizeof(fsInfo));
    fsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fsInfo.codeSize = sizeof(g_text_frag_spv);
    fsInfo.pCode = g_text_frag_spv;
    VkShaderModule fsModule = NULL;
    vr = vkCreateShaderModule(vk->device, &fsInfo, NULL, &fsModule);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_text_renderer: vkCreateShaderModule(fs) failed vr=%d\n", (int)vr); return 0; }

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

    VkVertexInputBindingDescription binding2;
    binding2.binding = 0;
    binding2.stride = sizeof(SqwGlyphVertex);
    binding2.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3];
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = 0;   /* pos */
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32_SFLOAT; attrs[1].offset = 8;   /* uv */
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[2].offset = 16; /* color */

    VkPipelineVertexInputStateCreateInfo vinState;
    memset(&vinState, 0, sizeof(vinState));
    vinState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vinState.vertexBindingDescriptionCount = 1;
    vinState.pVertexBindingDescriptions = &binding2;
    vinState.vertexAttributeDescriptionCount = 3;
    vinState.pVertexAttributeDescriptions = attrs;

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

    /* Standard alpha blending: glyph edges (anti-aliased by the atlas'
     * baked coverage values) and any translucent overlay drawn through
     * this pipeline need real src-over compositing, unlike the flat box
     * pipeline (renderer_vk.c), which draws only fully opaque rects. */
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

    VkPipelineLayoutCreateInfo plInfo;
    memset(&plInfo, 0, sizeof(plInfo));
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &tr->descriptorSetLayout;
    vr = vkCreatePipelineLayout(vk->device, &plInfo, NULL, &tr->pipelineLayout);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_text_renderer: vkCreatePipelineLayout failed vr=%d\n", (int)vr); return 0; }

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
    gpInfo.layout = tr->pipelineLayout;
    gpInfo.renderPass = vk->renderPass;
    gpInfo.subpass = 0;
    gpInfo.basePipelineIndex = -1;
    vr = vkCreateGraphicsPipelines(vk->device, NULL, 1, &gpInfo, NULL, &tr->pipeline);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_text_renderer: vkCreateGraphicsPipelines failed vr=%d\n", (int)vr); return 0; }

    /* --- Persistently-mapped vertex buffer, whole-buffer rebuild + one
     * vkCmdDraw per flush -- same technique as renderer_vk.c's box
     * buffer. --- */
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = (VkDeviceSize)(SQW_TEXT_MAX_GLYPHS * 6 * sizeof(SqwGlyphVertex));
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vr = vkCreateBuffer(vk->device, &bufInfo, NULL, &tr->vertexBuffer);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_text_renderer: vkCreateBuffer failed vr=%d\n", (int)vr); return 0; }

    VkMemoryRequirements memReq;
    memset(&memReq, 0, sizeof(memReq));
    vkGetBufferMemoryRequirements(vk->device, tr->vertexBuffer, &memReq);
    uint32_t memTypeIdx = sqw_vk_find_memory_type(vk, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memTypeIdx == 0xFFFFFFFFu) return 0;

    VkMemoryAllocateInfo allocInfo;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIdx;
    vr = vkAllocateMemory(vk->device, &allocInfo, NULL, &tr->vertexMemory);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_text_renderer: vkAllocateMemory failed vr=%d\n", (int)vr); return 0; }
    vkBindBufferMemory(vk->device, tr->vertexBuffer, tr->vertexMemory, 0);
    vkMapMemory(vk->device, tr->vertexMemory, 0, bufInfo.size, 0, &tr->mapped);

    fprintf(stderr, "sqw_text_renderer: init complete (atlas %ux%u, max %d glyphs)\n",
        tr->atlas.width, tr->atlas.height, SQW_TEXT_MAX_GLYPHS);
    return 1;
}

void sqw_text_draw_char(SqwTextRenderer *tr, float x, float y, char ch, float scale,
                         float r, float g, float b, float a, float viewport_w, float viewport_h) {
    if (tr->vcount + 6 > SQW_TEXT_MAX_GLYPHS * 6) {
        return;
    }
    const SqwGlyphMetrics *gm = sqw_glyph_lookup(ch);
    if (!gm || gm->glyph_w <= 0 || gm->glyph_h <= 0) return; /* space / unsupported char: nothing to draw */

    float px0 = x + (float)gm->bearing_x * scale;
    float py0 = y + (float)gm->bearing_y * scale;
    float pw = (float)gm->glyph_w * scale;
    float ph = (float)gm->glyph_h * scale;
    float px1 = px0 + pw;
    float py1 = py0 + ph;

    float u0 = (float)(gm->atlas_x) / (float)SQW_FONT_ATLAS_W;
    float v0 = (float)(gm->atlas_y) / (float)SQW_FONT_ATLAS_H;
    float u1 = (float)(gm->atlas_x + gm->glyph_w) / (float)SQW_FONT_ATLAS_W;
    float v1 = (float)(gm->atlas_y + gm->glyph_h) / (float)SQW_FONT_ATLAS_H;

    /* pixel (top-left origin) -> Vulkan NDC, same mapping renderer_vk.c
     * already uses for boxes (Vulkan NDC is already y-down). */
    float x0 = (px0 / viewport_w) * 2.0f - 1.0f;
    float y0 = (py0 / viewport_h) * 2.0f - 1.0f;
    float x1 = (px1 / viewport_w) * 2.0f - 1.0f;
    float y1 = (py1 / viewport_h) * 2.0f - 1.0f;

    /* Field-by-field writes into verts[tr->vcount], not a whole-
     * SqwGlyphVertex-struct assignment through a post-incremented index
     * -- see renderer_vk.c's sqw_renderer_draw() for the full story: that
     * pattern is a real, confirmed squash codegen bug (silently drops/
     * corrupts the write instead of erroring), and this is the same
     * pattern in the glyph renderer. */
    SqwGlyphVertex *verts = (SqwGlyphVertex *)tr->mapped;
    verts[tr->vcount].x = x0; verts[tr->vcount].y = y0; verts[tr->vcount].u = u0; verts[tr->vcount].v = v0; verts[tr->vcount].r = r; verts[tr->vcount].g = g; verts[tr->vcount].b = b; verts[tr->vcount].a = a; tr->vcount++;
    verts[tr->vcount].x = x1; verts[tr->vcount].y = y0; verts[tr->vcount].u = u1; verts[tr->vcount].v = v0; verts[tr->vcount].r = r; verts[tr->vcount].g = g; verts[tr->vcount].b = b; verts[tr->vcount].a = a; tr->vcount++;
    verts[tr->vcount].x = x0; verts[tr->vcount].y = y1; verts[tr->vcount].u = u0; verts[tr->vcount].v = v1; verts[tr->vcount].r = r; verts[tr->vcount].g = g; verts[tr->vcount].b = b; verts[tr->vcount].a = a; tr->vcount++;
    verts[tr->vcount].x = x1; verts[tr->vcount].y = y0; verts[tr->vcount].u = u1; verts[tr->vcount].v = v0; verts[tr->vcount].r = r; verts[tr->vcount].g = g; verts[tr->vcount].b = b; verts[tr->vcount].a = a; tr->vcount++;
    verts[tr->vcount].x = x1; verts[tr->vcount].y = y1; verts[tr->vcount].u = u1; verts[tr->vcount].v = v1; verts[tr->vcount].r = r; verts[tr->vcount].g = g; verts[tr->vcount].b = b; verts[tr->vcount].a = a; tr->vcount++;
    verts[tr->vcount].x = x0; verts[tr->vcount].y = y1; verts[tr->vcount].u = u0; verts[tr->vcount].v = v1; verts[tr->vcount].r = r; verts[tr->vcount].g = g; verts[tr->vcount].b = b; verts[tr->vcount].a = a; tr->vcount++;
}

void sqw_text_draw_string(SqwTextRenderer *tr, float x, float y, const char *s, int len, float scale,
                           float r, float g, float b, float a, float viewport_w, float viewport_h) {
    float cx = x;
    int i;
    for (i = 0; i < len; i++) {
        sqw_text_draw_char(tr, cx, y, s[i], scale, r, g, b, a, viewport_w, viewport_h);
        cx += sqw_text_glyph_advance(s[i], scale);
    }
}

void sqw_text_renderer_flush(SqwVkContext *vk, SqwTextRenderer *tr, VkCommandBuffer cmd, float viewport_w, float viewport_h) {
    (void)vk; (void)viewport_w; (void)viewport_h;
    if (tr->vcount > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tr->pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tr->pipelineLayout, 0, 1, &tr->descriptorSet, 0, NULL);
        VkDeviceSize offset0 = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &tr->vertexBuffer, &offset0);
        vkCmdDraw(cmd, tr->vcount, 1, 0, 0);
    }
    tr->vcount = 0; /* reset for next frame */
}

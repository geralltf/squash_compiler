#include "image_renderer_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image_spirv.h"

typedef struct { float x, y, u, v; float r, g, b, a; } SqwImageVertex;

int sqw_image_renderer_init(SqwVkContext *vk, SqwImageRenderer *ir) {
    memset(ir, 0, sizeof(*ir));

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
    VkResult vr = vkCreateDescriptorSetLayout(vk->device, &dslInfo, NULL, &ir->descriptorSetLayout);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_image_renderer: vkCreateDescriptorSetLayout failed vr=%d\n", (int)vr); return 0; }

    /* --- Descriptor pool: one set per texture ever bound (see header) --- */
    VkDescriptorPoolSize poolSize;
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = SQW_IMAGE_MAX_TEXTURES;
    VkDescriptorPoolCreateInfo dpInfo;
    memset(&dpInfo, 0, sizeof(dpInfo));
    dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpInfo.maxSets = SQW_IMAGE_MAX_TEXTURES;
    dpInfo.poolSizeCount = 1;
    dpInfo.pPoolSizes = &poolSize;
    vr = vkCreateDescriptorPool(vk->device, &dpInfo, NULL, &ir->descriptorPool);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_image_renderer: vkCreateDescriptorPool failed vr=%d\n", (int)vr); return 0; }

    /* --- Shaders --- */
    init_image_spirv();
    VkShaderModuleCreateInfo vsInfo;
    memset(&vsInfo, 0, sizeof(vsInfo));
    vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsInfo.codeSize = sizeof(g_image_vert_spv);
    vsInfo.pCode = g_image_vert_spv;
    VkShaderModule vsModule = NULL;
    vr = vkCreateShaderModule(vk->device, &vsInfo, NULL, &vsModule);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_image_renderer: vkCreateShaderModule(vs) failed vr=%d\n", (int)vr); return 0; }

    VkShaderModuleCreateInfo fsInfo;
    memset(&fsInfo, 0, sizeof(fsInfo));
    fsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fsInfo.codeSize = sizeof(g_image_frag_spv);
    fsInfo.pCode = g_image_frag_spv;
    VkShaderModule fsModule = NULL;
    vr = vkCreateShaderModule(vk->device, &fsInfo, NULL, &fsModule);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_image_renderer: vkCreateShaderModule(fs) failed vr=%d\n", (int)vr); return 0; }

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
    binding2.stride = sizeof(SqwImageVertex);
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

    /* Standard alpha blending -- PNG/GIF transparency, same as
     * text_renderer_vk.c's own glyph blending. */
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
    plInfo.pSetLayouts = &ir->descriptorSetLayout;
    vr = vkCreatePipelineLayout(vk->device, &plInfo, NULL, &ir->pipelineLayout);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_image_renderer: vkCreatePipelineLayout failed vr=%d\n", (int)vr); return 0; }

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
    gpInfo.layout = ir->pipelineLayout;
    gpInfo.renderPass = vk->renderPass;
    gpInfo.subpass = 0;
    gpInfo.basePipelineIndex = -1;
    vr = vkCreateGraphicsPipelines(vk->device, NULL, 1, &gpInfo, NULL, &ir->pipeline);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_image_renderer: vkCreateGraphicsPipelines failed vr=%d\n", (int)vr); return 0; }

    /* --- Persistently-mapped vertex buffer: each sqw_image_draw_quad()
     * call gets its own reserved 6-vertex slot (SqwImageRenderer::cursor),
     * same "ad-hoc rect" technique as renderer_vk.c's
     * sqw_renderer_draw_rect(), not the glyph renderer's queue-then-flush
     * (see this file's header comment for why: descriptor set differs per
     * draw here). --- */
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = (VkDeviceSize)(SQW_IMAGE_MAX_QUADS_PER_FRAME * 6 * sizeof(SqwImageVertex));
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vr = vkCreateBuffer(vk->device, &bufInfo, NULL, &ir->vertexBuffer);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_image_renderer: vkCreateBuffer failed vr=%d\n", (int)vr); return 0; }

    VkMemoryRequirements memReq;
    memset(&memReq, 0, sizeof(memReq));
    vkGetBufferMemoryRequirements(vk->device, ir->vertexBuffer, &memReq);
    uint32_t memTypeIdx = sqw_vk_find_memory_type(vk, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memTypeIdx == 0xFFFFFFFFu) return 0;

    VkMemoryAllocateInfo allocInfo;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIdx;
    vr = vkAllocateMemory(vk->device, &allocInfo, NULL, &ir->vertexMemory);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_image_renderer: vkAllocateMemory failed vr=%d\n", (int)vr); return 0; }
    vkBindBufferMemory(vk->device, ir->vertexBuffer, ir->vertexMemory, 0);
    vkMapMemory(vk->device, ir->vertexMemory, 0, bufInfo.size, 0, &ir->mapped);

    fprintf(stderr, "sqw_image_renderer: init complete (max %d textures, %d quads/frame)\n",
        SQW_IMAGE_MAX_TEXTURES, SQW_IMAGE_MAX_QUADS_PER_FRAME);
    return 1;
}

void sqw_image_renderer_begin_frame(SqwImageRenderer *ir) {
    ir->cursor = 0;
}

VkDescriptorSet sqw_image_renderer_bind_texture(SqwVkContext *vk, SqwImageRenderer *ir, SqwTexture *tex) {
    VkDescriptorSetAllocateInfo dsaInfo;
    memset(&dsaInfo, 0, sizeof(dsaInfo));
    dsaInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsaInfo.descriptorPool = ir->descriptorPool;
    dsaInfo.descriptorSetCount = 1;
    dsaInfo.pSetLayouts = &ir->descriptorSetLayout;
    VkDescriptorSet dset = NULL;
    VkResult vr = vkAllocateDescriptorSets(vk->device, &dsaInfo, &dset);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_image_renderer: vkAllocateDescriptorSets failed vr=%d\n", (int)vr); return NULL; }

    VkDescriptorImageInfo imgInfo;
    imgInfo.sampler = tex->sampler;
    imgInfo.imageView = tex->view;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write;
    memset(&write, 0, sizeof(write));
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = dset;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(vk->device, 1, &write, 0, NULL);
    return dset;
}

void sqw_image_renderer_reset_pool(SqwVkContext *vk, SqwImageRenderer *ir) {
    vkResetDescriptorPool(vk->device, ir->descriptorPool, 0);
}

void sqw_image_draw_quad(SqwVkContext *vk, SqwImageRenderer *ir, VkCommandBuffer cmd, VkDescriptorSet descriptorSet,
                          float x, float y, float w, float h, float r, float g, float b, float a,
                          float viewport_w, float viewport_h) {
    (void)vk;
    if (!descriptorSet) return;
    if (ir->cursor >= SQW_IMAGE_MAX_QUADS_PER_FRAME) return;
    int base_vertex = ir->cursor * 6;
    ir->cursor++;

    float x0 = (x / viewport_w) * 2.0f - 1.0f;
    float y0 = (y / viewport_h) * 2.0f - 1.0f;
    float x1 = ((x + w) / viewport_w) * 2.0f - 1.0f;
    float y1 = ((y + h) / viewport_h) * 2.0f - 1.0f;

    /* Field-by-field writes, not a whole-struct assignment through an
     * index -- see renderer_vk.c's sqw_renderer_draw() for the full story
     * on why (a real, confirmed squash codegen bug with the other form). */
    SqwImageVertex *verts = (SqwImageVertex *)ir->mapped;
    verts[base_vertex + 0].x = x0; verts[base_vertex + 0].y = y0; verts[base_vertex + 0].u = 0.0f; verts[base_vertex + 0].v = 0.0f; verts[base_vertex + 0].r = r; verts[base_vertex + 0].g = g; verts[base_vertex + 0].b = b; verts[base_vertex + 0].a = a;
    verts[base_vertex + 1].x = x1; verts[base_vertex + 1].y = y0; verts[base_vertex + 1].u = 1.0f; verts[base_vertex + 1].v = 0.0f; verts[base_vertex + 1].r = r; verts[base_vertex + 1].g = g; verts[base_vertex + 1].b = b; verts[base_vertex + 1].a = a;
    verts[base_vertex + 2].x = x0; verts[base_vertex + 2].y = y1; verts[base_vertex + 2].u = 0.0f; verts[base_vertex + 2].v = 1.0f; verts[base_vertex + 2].r = r; verts[base_vertex + 2].g = g; verts[base_vertex + 2].b = b; verts[base_vertex + 2].a = a;
    verts[base_vertex + 3].x = x1; verts[base_vertex + 3].y = y0; verts[base_vertex + 3].u = 1.0f; verts[base_vertex + 3].v = 0.0f; verts[base_vertex + 3].r = r; verts[base_vertex + 3].g = g; verts[base_vertex + 3].b = b; verts[base_vertex + 3].a = a;
    verts[base_vertex + 4].x = x1; verts[base_vertex + 4].y = y1; verts[base_vertex + 4].u = 1.0f; verts[base_vertex + 4].v = 1.0f; verts[base_vertex + 4].r = r; verts[base_vertex + 4].g = g; verts[base_vertex + 4].b = b; verts[base_vertex + 4].a = a;
    verts[base_vertex + 5].x = x0; verts[base_vertex + 5].y = y1; verts[base_vertex + 5].u = 0.0f; verts[base_vertex + 5].v = 1.0f; verts[base_vertex + 5].r = r; verts[base_vertex + 5].g = g; verts[base_vertex + 5].b = b; verts[base_vertex + 5].a = a;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ir->pipeline);
    VkDeviceSize offset0 = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &ir->vertexBuffer, &offset0);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ir->pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
    vkCmdDraw(cmd, 6, 1, (uint32_t)base_vertex, 0);
}

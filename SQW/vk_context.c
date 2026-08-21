#include "vk_context.h"
#include <stdio.h>

/* Shared by sqw_vk_context_init() (first-time creation) and
 * sqw_vk_recreate_swapchain() (resize): queries the current surface
 * capabilities/format, creates the swapchain, image views, and
 * framebuffers into `vk`. Assumes vk->renderPass already exists (created
 * once, reused across resizes -- its format never changes). Returns 1 on
 * success, 0 on failure. A 0x0 extent (window minimized) is treated as
 * "nothing to do yet", not a hard failure, so the caller can just skip
 * that frame's rendering instead of tearing everything down. */
static int create_swapchain_and_deps(SqwVkContext *vk, uint32_t width, uint32_t height) {
    VkResult vr;
    uint32_t i;

    VkSurfaceCapabilitiesKHR caps;
    memset(&caps, 0, sizeof(caps));
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->phys, vk->surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk->phys, vk->surface, &fmtCount, NULL);
    VkSurfaceFormatKHR fmts[32];
    if (fmtCount > 32) fmtCount = 32;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk->phys, vk->surface, &fmtCount, fmts);
    VkFormat chosenFormat = (fmtCount > 0) ? fmts[0].format : VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR chosenColorSpace = (fmtCount > 0) ? fmts[0].colorSpace : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (i = 0; i < fmtCount; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) { chosenFormat = fmts[i].format; chosenColorSpace = fmts[i].colorSpace; break; }
    }
    vk->swapFormat = chosenFormat;

    vk->extent = caps.currentExtent;
    if (vk->extent.width == 0xFFFFFFFFu) { vk->extent.width = width; vk->extent.height = height; }
    if (vk->extent.width == 0 || vk->extent.height == 0) {
        fprintf(stderr, "sqw_vk: 0x0 extent (minimized?), skipping swapchain (re)creation\n"); fflush(stdout);
        return 0;
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;
    if (imageCount > SQW_MAX_SWAP_IMAGES) imageCount = SQW_MAX_SWAP_IMAGES;

    VkSwapchainCreateInfoKHR scInfo;
    memset(&scInfo, 0, sizeof(scInfo));
    scInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scInfo.surface = vk->surface;
    scInfo.minImageCount = imageCount;
    scInfo.imageFormat = chosenFormat;
    scInfo.imageColorSpace = chosenColorSpace;
    scInfo.imageExtent = vk->extent;
    scInfo.imageArrayLayers = 1;
    /* VK_IMAGE_USAGE_TRANSFER_SRC_BIT (in addition to COLOR_ATTACHMENT):
     * lets a swapchain image be vkCmdCopyImageToBuffer'd for a debug
     * screenshot (see sqw_main.c's SQW_TEST_SCREENSHOT_FRAME) -- harmless
     * to request unconditionally (every real GPU/ICD supports it on a
     * presentable image; this project doesn't probe imageUsage support
     * before requesting it, matching this whole function's existing
     * "don't over-engineer for capabilities every real target already
     * has" style elsewhere). */
    scInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scInfo.preTransform = caps.currentTransform;
    scInfo.compositeAlpha = 0x1;
    scInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    scInfo.clipped = VK_TRUE;
    /* oldSwapchain deliberately left VK_NULL_HANDLE (zero): the caller
     * (sqw_vk_recreate_swapchain) already destroyed the previous swapchain
     * before calling here -- see that function's own comment for why
     * destroy-then-recreate was chosen over the usual "pass oldSwapchain,
     * destroy after" pattern. */

    vr = vkCreateSwapchainKHR(vk->device, &scInfo, NULL, &vk->swapchain);
    fprintf(stderr, "sqw_vk: vkCreateSwapchainKHR vr=%d extent=%ux%u\n", (int)vr, vk->extent.width, vk->extent.height); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;

    vk->swapImageCount = 0;
    vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->swapImageCount, NULL);
    if (vk->swapImageCount > SQW_MAX_SWAP_IMAGES) vk->swapImageCount = SQW_MAX_SWAP_IMAGES;
    vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->swapImageCount, vk->swapImages);

    for (i = 0; i < vk->swapImageCount; i++) {
        VkImageViewCreateInfo ivInfo;
        memset(&ivInfo, 0, sizeof(ivInfo));
        ivInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivInfo.image = vk->swapImages[i];
        ivInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivInfo.format = chosenFormat;
        ivInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivInfo.subresourceRange.levelCount = 1;
        ivInfo.subresourceRange.layerCount = 1;
        vr = vkCreateImageView(vk->device, &ivInfo, NULL, &vk->swapViews[i]);
        if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: vkCreateImageView[%u] failed vr=%d\n", i, (int)vr); fflush(stdout); return 0; }
    }

    for (i = 0; i < vk->swapImageCount; i++) {
        VkFramebufferCreateInfo fbInfo;
        memset(&fbInfo, 0, sizeof(fbInfo));
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = vk->renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &vk->swapViews[i];
        fbInfo.width = vk->extent.width;
        fbInfo.height = vk->extent.height;
        fbInfo.layers = 1;
        vr = vkCreateFramebuffer(vk->device, &fbInfo, NULL, &vk->framebuffers[i]);
        if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: vkCreateFramebuffer[%u] failed vr=%d\n", i, (int)vr); fflush(stdout); return 0; }
    }
    return 1;
}

/* Destroys everything create_swapchain_and_deps() made (but not the
 * render pass, which is size-independent and reused). Safe to call with
 * zeroed/never-created handles (every *Destroy* Vulkan call is a documented
 * no-op on VK_NULL_HANDLE). */
static void destroy_swapchain_and_deps(SqwVkContext *vk) {
    uint32_t i;
    for (i = 0; i < vk->swapImageCount; i++) {
        vkDestroyFramebuffer(vk->device, vk->framebuffers[i], NULL);
        vkDestroyImageView(vk->device, vk->swapViews[i], NULL);
        vk->framebuffers[i] = 0;
        vk->swapViews[i] = 0;
    }
    vkDestroySwapchainKHR(vk->device, vk->swapchain, NULL);
    vk->swapchain = 0;
}

int sqw_vk_recreate_swapchain(SqwVkContext *vk, uint32_t width_height_packed) {
    uint32_t width = width_height_packed >> 16;
    uint32_t height = width_height_packed & 0xFFFFu;
    /* Wait for the GPU to finish with every resource we're about to
     * destroy -- vkDeviceWaitIdle is the blunt-but-correct choice here
     * (a resize is a rare, latency-insensitive event, not a hot path). */
    vkDeviceWaitIdle(vk->device);
    destroy_swapchain_and_deps(vk);
    return create_swapchain_and_deps(vk, width, height);
}

int sqw_vk_create_texture_r8(SqwVkContext *vk, const unsigned char *pixels, uint32_t w, uint32_t h, SqwTexture *out) {
    VkResult vr;
    memset(out, 0, sizeof(*out));
    out->width = w;
    out->height = h;
    VkDeviceSize size = (VkDeviceSize)w * (VkDeviceSize)h;

    /* --- Staging buffer (host-visible, holds the raw pixels briefly) --- */
    VkBuffer stagingBuf;
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vr = vkCreateBuffer(vk->device, &bufInfo, NULL, &stagingBuf);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: staging vkCreateBuffer failed vr=%d\n", (int)vr); fflush(stdout); return 0; }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(vk->device, stagingBuf, &memReq);
    VkMemoryAllocateInfo allocInfo;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = sqw_vk_find_memory_type(vk, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMem;
    vr = vkAllocateMemory(vk->device, &allocInfo, NULL, &stagingMem);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: staging vkAllocateMemory failed vr=%d\n", (int)vr); fflush(stdout); return 0; }
    vkBindBufferMemory(vk->device, stagingBuf, stagingMem, 0);

    void *mapped = NULL;
    vkMapMemory(vk->device, stagingMem, 0, size, 0, &mapped);
    memcpy(mapped, pixels, (size_t)size);
    vkUnmapMemory(vk->device, stagingMem);

    /* --- Device-local image --- */
    VkImageCreateInfo imgInfo;
    memset(&imgInfo, 0, sizeof(imgInfo));
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8_UNORM;
    imgInfo.extent.width = w;
    imgInfo.extent.height = h;
    imgInfo.extent.depth = 1;
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vr = vkCreateImage(vk->device, &imgInfo, NULL, &out->image);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: vkCreateImage failed vr=%d\n", (int)vr); fflush(stdout); return 0; }

    VkMemoryRequirements imgMemReq;
    vkGetImageMemoryRequirements(vk->device, out->image, &imgMemReq);
    VkMemoryAllocateInfo imgAllocInfo;
    memset(&imgAllocInfo, 0, sizeof(imgAllocInfo));
    imgAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imgAllocInfo.allocationSize = imgMemReq.size;
    imgAllocInfo.memoryTypeIndex = sqw_vk_find_memory_type(vk, imgMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vr = vkAllocateMemory(vk->device, &imgAllocInfo, NULL, &out->memory);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: image vkAllocateMemory failed vr=%d\n", (int)vr); fflush(stdout); return 0; }
    vkBindImageMemory(vk->device, out->image, out->memory, 0);

    /* --- One-shot command buffer: layout transitions + buffer->image copy --- */
    VkCommandBufferAllocateInfo cbInfo;
    memset(&cbInfo, 0, sizeof(cbInfo));
    cbInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbInfo.commandPool = vk->commandPool;
    cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(vk->device, &cbInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo;
    memset(&beginInfo, 0, sizeof(beginInfo));
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier toDst;
    memset(&toDst, 0, sizeof(toDst));
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = 0xFFFFFFFFu; /* VK_QUEUE_FAMILY_IGNORED */
    toDst.dstQueueFamilyIndex = 0xFFFFFFFFu;
    toDst.image = out->image;
    toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toDst.subresourceRange.levelCount = 1;
    toDst.subresourceRange.layerCount = 1;
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &toDst);

    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = w;
    region.imageExtent.height = h;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmd, stagingBuf, out->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShaderRead = toDst;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &toShaderRead);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo;
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(vk->queue, 1, &submitInfo, NULL);
    vkQueueWaitIdle(vk->queue);
    vkFreeCommandBuffers(vk->device, vk->commandPool, 1, &cmd);

    vkDestroyBuffer(vk->device, stagingBuf, NULL);
    vkFreeMemory(vk->device, stagingMem, NULL);

    /* --- View + sampler --- */
    VkImageViewCreateInfo ivInfo;
    memset(&ivInfo, 0, sizeof(ivInfo));
    ivInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivInfo.image = out->image;
    ivInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivInfo.format = VK_FORMAT_R8_UNORM;
    ivInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivInfo.subresourceRange.levelCount = 1;
    ivInfo.subresourceRange.layerCount = 1;
    vr = vkCreateImageView(vk->device, &ivInfo, NULL, &out->view);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: texture vkCreateImageView failed vr=%d\n", (int)vr); fflush(stdout); return 0; }

    VkSamplerCreateInfo sampInfo;
    memset(&sampInfo, 0, sizeof(sampInfo));
    sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter = VK_FILTER_LINEAR;
    sampInfo.minFilter = VK_FILTER_LINEAR;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.maxLod = 0.0f;
    sampInfo.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    vr = vkCreateSampler(vk->device, &sampInfo, NULL, &out->sampler);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: vkCreateSampler failed vr=%d\n", (int)vr); fflush(stdout); return 0; }

    fprintf(stderr, "sqw_vk: texture %ux%u created\n", w, h); fflush(stdout);
    return 1;
}

/* Same staging-buffer/one-shot-command-buffer upload as
 * sqw_vk_create_texture_r8() above (that function's own comment explains
 * the technique), just VK_FORMAT_R8G8B8A8_UNORM/4-bytes-per-pixel instead
 * of R8_UNORM/1 -- for decoded <img>/CSS background-image pixels (see
 * SQW/image_cache.c), never the font atlas. Deliberately not factored into
 * a shared helper with sqw_vk_create_texture_r8(): this project generally
 * prefers a little duplication over an abstraction with only two call
 * sites (see e.g. renderer_vk.c/text_renderer_vk.c being two whole
 * separate pipelines rather than one parameterized one). */
int sqw_vk_create_texture_rgba8(SqwVkContext *vk, const unsigned char *pixels, uint32_t w, uint32_t h, SqwTexture *out) {
    VkResult vr;
    memset(out, 0, sizeof(*out));
    out->width = w;
    out->height = h;
    VkDeviceSize size = (VkDeviceSize)w * (VkDeviceSize)h * 4;

    /* --- Staging buffer (host-visible, holds the raw pixels briefly) --- */
    VkBuffer stagingBuf;
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vr = vkCreateBuffer(vk->device, &bufInfo, NULL, &stagingBuf);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: staging vkCreateBuffer failed vr=%d\n", (int)vr); fflush(stdout); return 0; }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(vk->device, stagingBuf, &memReq);
    VkMemoryAllocateInfo allocInfo;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = sqw_vk_find_memory_type(vk, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMem;
    vr = vkAllocateMemory(vk->device, &allocInfo, NULL, &stagingMem);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: staging vkAllocateMemory failed vr=%d\n", (int)vr); fflush(stdout); return 0; }
    vkBindBufferMemory(vk->device, stagingBuf, stagingMem, 0);

    void *mapped = NULL;
    vkMapMemory(vk->device, stagingMem, 0, size, 0, &mapped);
    memcpy(mapped, pixels, (size_t)size);
    vkUnmapMemory(vk->device, stagingMem);

    /* --- Device-local image --- */
    VkImageCreateInfo imgInfo;
    memset(&imgInfo, 0, sizeof(imgInfo));
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent.width = w;
    imgInfo.extent.height = h;
    imgInfo.extent.depth = 1;
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vr = vkCreateImage(vk->device, &imgInfo, NULL, &out->image);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: vkCreateImage failed vr=%d\n", (int)vr); fflush(stdout); return 0; }

    VkMemoryRequirements imgMemReq;
    vkGetImageMemoryRequirements(vk->device, out->image, &imgMemReq);
    VkMemoryAllocateInfo imgAllocInfo;
    memset(&imgAllocInfo, 0, sizeof(imgAllocInfo));
    imgAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imgAllocInfo.allocationSize = imgMemReq.size;
    imgAllocInfo.memoryTypeIndex = sqw_vk_find_memory_type(vk, imgMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vr = vkAllocateMemory(vk->device, &imgAllocInfo, NULL, &out->memory);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: image vkAllocateMemory failed vr=%d\n", (int)vr); fflush(stdout); return 0; }
    vkBindImageMemory(vk->device, out->image, out->memory, 0);

    /* --- One-shot command buffer: layout transitions + buffer->image copy --- */
    VkCommandBufferAllocateInfo cbInfo;
    memset(&cbInfo, 0, sizeof(cbInfo));
    cbInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbInfo.commandPool = vk->commandPool;
    cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(vk->device, &cbInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo;
    memset(&beginInfo, 0, sizeof(beginInfo));
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier toDst;
    memset(&toDst, 0, sizeof(toDst));
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = 0xFFFFFFFFu; /* VK_QUEUE_FAMILY_IGNORED */
    toDst.dstQueueFamilyIndex = 0xFFFFFFFFu;
    toDst.image = out->image;
    toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toDst.subresourceRange.levelCount = 1;
    toDst.subresourceRange.layerCount = 1;
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &toDst);

    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = w;
    region.imageExtent.height = h;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmd, stagingBuf, out->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShaderRead = toDst;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &toShaderRead);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo;
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(vk->queue, 1, &submitInfo, NULL);
    vkQueueWaitIdle(vk->queue);
    vkFreeCommandBuffers(vk->device, vk->commandPool, 1, &cmd);

    vkDestroyBuffer(vk->device, stagingBuf, NULL);
    vkFreeMemory(vk->device, stagingMem, NULL);

    /* --- View + sampler --- */
    VkImageViewCreateInfo ivInfo;
    memset(&ivInfo, 0, sizeof(ivInfo));
    ivInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivInfo.image = out->image;
    ivInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ivInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivInfo.subresourceRange.levelCount = 1;
    ivInfo.subresourceRange.layerCount = 1;
    vr = vkCreateImageView(vk->device, &ivInfo, NULL, &out->view);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: texture vkCreateImageView failed vr=%d\n", (int)vr); fflush(stdout); return 0; }

    VkSamplerCreateInfo sampInfo;
    memset(&sampInfo, 0, sizeof(sampInfo));
    sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter = VK_FILTER_LINEAR;
    sampInfo.minFilter = VK_FILTER_LINEAR;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.maxLod = 0.0f;
    sampInfo.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    vr = vkCreateSampler(vk->device, &sampInfo, NULL, &out->sampler);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: vkCreateSampler failed vr=%d\n", (int)vr); fflush(stdout); return 0; }

    fprintf(stderr, "sqw_vk: rgba8 texture %ux%u created\n", w, h); fflush(stdout);
    return 1;
}

void sqw_vk_destroy_texture(SqwVkContext *vk, SqwTexture *tex) {
    vkDestroySampler(vk->device, tex->sampler, NULL);
    vkDestroyImageView(vk->device, tex->view, NULL);
    vkDestroyImage(vk->device, tex->image, NULL);
    vkFreeMemory(vk->device, tex->memory, NULL);
    memset(tex, 0, sizeof(*tex));
}

uint32_t sqw_vk_find_memory_type(SqwVkContext *vk, uint32_t typeBits, VkFlags props) {
    uint32_t i;
    for (i = 0; i < vk->memProps.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (vk->memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    fprintf(stderr, "sqw_vk_find_memory_type: no suitable memory type found!\n"); fflush(stdout);
    return 0xFFFFFFFFu;
}

#ifdef __linux__
int sqw_vk_context_init(SqwVkContext *vk, Display *dpy, Window win, uint32_t width_height_packed) {
#else
int sqw_vk_context_init(SqwVkContext *vk, HINSTANCE hinstance, HWND hwnd, uint32_t width_height_packed) {
#endif
    uint32_t width = width_height_packed >> 16;
    uint32_t height = width_height_packed & 0xFFFFu;
    memset(vk, 0, sizeof(*vk));

    /* --- Instance --- */
    VkApplicationInfo appInfo;
    memset(&appInfo, 0, sizeof(appInfo));
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SQW";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "none";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = (1u << 22);

#ifdef __linux__
    const char *instExts[2] = { "VK_KHR_surface", "VK_KHR_xlib_surface" };
#else
    const char *instExts[2] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
#endif
    VkInstanceCreateInfo instInfo;
    memset(&instInfo, 0, sizeof(instInfo));
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledExtensionCount = 2;
    instInfo.ppEnabledExtensionNames = instExts;

    VkResult vr = vkCreateInstance(&instInfo, NULL, &vk->instance);
    fprintf(stderr, "sqw_vk: vkCreateInstance vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;

    /* --- Physical device (first one) --- */
    uint32_t physCount = 0;
    vkEnumeratePhysicalDevices(vk->instance, &physCount, NULL);
    if (physCount == 0) { fprintf(stderr, "sqw_vk: no physical devices\n"); fflush(stdout); return 0; }
    VkPhysicalDevice physDevices[8];
    if (physCount > 8) physCount = 8;
    vkEnumeratePhysicalDevices(vk->instance, &physCount, physDevices);
    vk->phys = physDevices[0];
    vkGetPhysicalDeviceMemoryProperties(vk->phys, &vk->memProps);

    /* --- Surface --- */
#ifdef __linux__
    VkXlibSurfaceCreateInfoKHR sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    sci.dpy = dpy;
    sci.window = win;
    vr = vkCreateXlibSurfaceKHR(vk->instance, &sci, NULL, &vk->surface);
    fprintf(stderr, "sqw_vk: vkCreateXlibSurfaceKHR vr=%d\n", (int)vr); fflush(stdout);
#else
    VkWin32SurfaceCreateInfoKHR sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = hinstance;
    sci.hwnd = hwnd;
    vr = vkCreateWin32SurfaceKHR(vk->instance, &sci, NULL, &vk->surface);
    fprintf(stderr, "sqw_vk: vkCreateWin32SurfaceKHR vr=%d\n", (int)vr); fflush(stdout);
#endif
    if (vr != VK_SUCCESS) return 0;

    /* --- Queue family: graphics + present --- */
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &qfCount, NULL);
    VkQueueFamilyProperties qfProps[16];
    if (qfCount > 16) qfCount = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &qfCount, qfProps);
    vk->queueFamily = 0xFFFFFFFFu;
    uint32_t i;
    for (i = 0; i < qfCount; i++) {
        if (!(qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        VkBool32 presentOk = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(vk->phys, i, vk->surface, &presentOk);
        if (presentOk) { vk->queueFamily = i; break; }
    }
    if (vk->queueFamily == 0xFFFFFFFFu) { fprintf(stderr, "sqw_vk: no graphics+present queue family\n"); fflush(stdout); return 0; }

    /* --- Logical device + queue --- */
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo dqInfo;
    memset(&dqInfo, 0, sizeof(dqInfo));
    dqInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dqInfo.queueFamilyIndex = vk->queueFamily;
    dqInfo.queueCount = 1;
    dqInfo.pQueuePriorities = &queuePriority;

    const char *devExts[1] = { "VK_KHR_swapchain" };
    VkPhysicalDeviceFeatures feats;
    memset(&feats, 0, sizeof(feats));
    VkDeviceCreateInfo devInfo;
    memset(&devInfo, 0, sizeof(devInfo));
    devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devInfo.queueCreateInfoCount = 1;
    devInfo.pQueueCreateInfos = &dqInfo;
    devInfo.enabledExtensionCount = 1;
    devInfo.ppEnabledExtensionNames = devExts;
    devInfo.pEnabledFeatures = &feats;

    vr = vkCreateDevice(vk->phys, &devInfo, NULL, &vk->device);
    fprintf(stderr, "sqw_vk: vkCreateDevice vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;
    vkGetDeviceQueue(vk->device, vk->queueFamily, 0, &vk->queue);

    /* --- Surface format (queried once here, ahead of the swapchain itself,
     * purely so the render pass below can be created with the right
     * attachment format -- create_swapchain_and_deps() re-queries the same
     * thing into vk->swapFormat right after; a cheap, read-only Vulkan
     * query, not worth threading through as a parameter instead). --- */
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk->phys, vk->surface, &fmtCount, NULL);
    VkSurfaceFormatKHR fmts[32];
    if (fmtCount > 32) fmtCount = 32;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk->phys, vk->surface, &fmtCount, fmts);
    VkFormat chosenFormat = (fmtCount > 0) ? fmts[0].format : VK_FORMAT_B8G8R8A8_UNORM;
    for (i = 0; i < fmtCount; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) { chosenFormat = fmts[i].format; break; }
    }

    /* --- Render pass (format-dependent only, not swapchain-extent-
     * dependent -- created once here and reused as-is across every future
     * sqw_vk_recreate_swapchain() resize). --- */
    VkAttachmentDescription colorAttach;
    memset(&colorAttach, 0, sizeof(colorAttach));
    colorAttach.format = chosenFormat;
    colorAttach.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttach.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef;
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass;
    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep;
    memset(&dep, 0, sizeof(dep));
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo;
    memset(&rpInfo, 0, sizeof(rpInfo));
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttach;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;

    vr = vkCreateRenderPass(vk->device, &rpInfo, NULL, &vk->renderPass);
    fprintf(stderr, "sqw_vk: vkCreateRenderPass vr=%d\n", (int)vr); fflush(stdout);
    if (vr != VK_SUCCESS) return 0;

    /* --- Command pool (extent-independent, created before the swapchain
     * itself so command-buffer allocation right after has vk->commandPool
     * ready) --- */
    VkCommandPoolCreateInfo cpInfo;
    memset(&cpInfo, 0, sizeof(cpInfo));
    cpInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpInfo.queueFamilyIndex = vk->queueFamily;
    vr = vkCreateCommandPool(vk->device, &cpInfo, NULL, &vk->commandPool);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: vkCreateCommandPool failed vr=%d\n", (int)vr); fflush(stdout); return 0; }

    /* --- Swapchain + image views + framebuffers (shared with resize) --- */
    if (!create_swapchain_and_deps(vk, width, height)) return 0;

    /* --- Command buffers (one per swap image, now that swapImageCount is
     * known) --- */
    VkCommandBufferAllocateInfo cbAllocInfo;
    memset(&cbAllocInfo, 0, sizeof(cbAllocInfo));
    cbAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAllocInfo.commandPool = vk->commandPool;
    cbAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAllocInfo.commandBufferCount = vk->swapImageCount;
    vr = vkAllocateCommandBuffers(vk->device, &cbAllocInfo, vk->cmdBufs);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: vkAllocateCommandBuffers failed vr=%d\n", (int)vr); fflush(stdout); return 0; }

    /* --- Sync objects --- */
    VkSemaphoreCreateInfo semInfo;
    memset(&semInfo, 0, sizeof(semInfo));
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(vk->device, &semInfo, NULL, &vk->imageAvailable);
    vkCreateSemaphore(vk->device, &semInfo, NULL, &vk->renderFinished);

    VkFenceCreateInfo fenceInfo;
    memset(&fenceInfo, 0, sizeof(fenceInfo));
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(vk->device, &fenceInfo, NULL, &vk->inFlightFence);

    fprintf(stderr, "sqw_vk: context init complete (%u swap images)\n", vk->swapImageCount); fflush(stdout);
    return 1;
}

VkCommandBuffer sqw_vk_begin_frame(SqwVkContext *vk, float r, float g, float b, float a, uint32_t *outImageIndex) {
    vkWaitForFences(vk->device, 1, &vk->inFlightFence, VK_TRUE, ~0ull);
    vkResetFences(vk->device, 1, &vk->inFlightFence);

    uint32_t imageIndex = 0;
    VkResult vr = vkAcquireNextImageKHR(vk->device, vk->swapchain, ~0ull, vk->imageAvailable, NULL, &imageIndex);
    if (vr != VK_SUCCESS && vr != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "sqw_vk: vkAcquireNextImageKHR failed vr=%d\n", (int)vr); fflush(stdout);
        return NULL;
    }
    *outImageIndex = imageIndex;

    VkCommandBuffer cmd = vk->cmdBufs[imageIndex];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo;
    memset(&beginInfo, 0, sizeof(beginInfo));
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkClearValue clearValue;
    memset(&clearValue, 0, sizeof(clearValue));
    clearValue.color.float32[0] = r;
    clearValue.color.float32[1] = g;
    clearValue.color.float32[2] = b;
    clearValue.color.float32[3] = a;

    VkRenderPassBeginInfo rpBegin;
    memset(&rpBegin, 0, sizeof(rpBegin));
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = vk->renderPass;
    rpBegin.framebuffer = vk->framebuffers[imageIndex];
    rpBegin.renderArea.offset.x = 0; rpBegin.renderArea.offset.y = 0;
    rpBegin.renderArea.extent = vk->extent;
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    /* Every pipeline in this project uses VK_DYNAMIC_STATE_VIEWPORT/SCISSOR
     * (see renderer_vk.c / text_renderer_vk.c) precisely so a resize never
     * needs a pipeline rebuild -- set both here, once per frame, to the
     * CURRENT swapchain extent (kept fresh by sqw_vk_recreate_swapchain())
     * rather than each renderer having to do it redundantly. */
    VkViewport vp;
    vp.x = 0; vp.y = 0;
    vp.width = (float)vk->extent.width; vp.height = (float)vk->extent.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D sc;
    sc.offset.x = 0; sc.offset.y = 0;
    sc.extent = vk->extent;
    vkCmdSetScissor(cmd, 0, 1, &sc);

    return cmd;
}

void sqw_vk_end_frame(SqwVkContext *vk, VkCommandBuffer cmd, uint32_t imageIndex) {
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo;
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &vk->imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &vk->renderFinished;
    vkQueueSubmit(vk->queue, 1, &submitInfo, vk->inFlightFence);

    VkPresentInfoKHR presentInfo;
    memset(&presentInfo, 0, sizeof(presentInfo));
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &vk->renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &vk->swapchain;
    presentInfo.pImageIndices = &imageIndex;
    vkQueuePresentKHR(vk->queue, &presentInfo);
}

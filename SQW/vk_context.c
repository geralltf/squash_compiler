#include "vk_context.h"
#include <stdio.h>

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

    /* --- Swapchain --- */
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
    scInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scInfo.preTransform = caps.currentTransform;
    scInfo.compositeAlpha = 0x1;
    scInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    scInfo.clipped = VK_TRUE;

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

    /* --- Render pass --- */
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

    /* --- Framebuffers --- */
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

    /* --- Command pool + buffers --- */
    VkCommandPoolCreateInfo cpInfo;
    memset(&cpInfo, 0, sizeof(cpInfo));
    cpInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpInfo.queueFamilyIndex = vk->queueFamily;
    vr = vkCreateCommandPool(vk->device, &cpInfo, NULL, &vk->commandPool);
    if (vr != VK_SUCCESS) { fprintf(stderr, "sqw_vk: vkCreateCommandPool failed vr=%d\n", (int)vr); fflush(stdout); return 0; }

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

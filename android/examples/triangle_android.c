#include "include/vulkan_android.h"
#include "triangle_spirv.h"

/* Per-vertex-color-interpolated triangle, real Vulkan, real SPIR-V shaders
 * (triangle_spirv.h -- same bytecode already validated by the existing
 * Windows/Linux triangle_vulkan.c demo, compiled from real GLSL via
 * glslangValidator, not hand-authored). Unlike that demo's continuous
 * render loop, Android's lifecycle is callback-driven: there is no window
 * to render into until the platform calls onNativeWindowCreated, so the
 * entire pipeline setup AND one rendered+presented frame happen inside
 * that callback instead of linearly in main(). See
 * android/android_native_glue.h-equivalent struct declarations below,
 * proven correct against the real device in an earlier test
 * (sdkVersion/internalDataPath read back correctly, and the platform
 * genuinely called back into a squash-compiled function). */

extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);

typedef struct ANativeActivityCallbacks {
    void (*onStart)(void *activity);
    void (*onResume)(void *activity);
    void *(*onSaveInstanceState)(void *activity, long *outSize);
    void (*onPause)(void *activity);
    void (*onStop)(void *activity);
    void (*onDestroy)(void *activity);
    void (*onWindowFocusChanged)(void *activity, int hasFocus);
    void (*onNativeWindowCreated)(void *activity, void *window);
    void (*onNativeWindowResized)(void *activity, void *window);
    void (*onNativeWindowRedrawNeeded)(void *activity, void *window);
    void (*onNativeWindowDestroyed)(void *activity, void *window);
    void (*onInputQueueCreated)(void *activity, void *queue);
    void (*onInputQueueDestroyed)(void *activity, void *queue);
    void (*onContentRectChanged)(void *activity, void *rect);
    void (*onConfigurationChanged)(void *activity);
    void (*onLowMemory)(void *activity);
} ANativeActivityCallbacks;

typedef struct ANativeActivity {
    ANativeActivityCallbacks *callbacks;
    void *vm;
    void *env;
    void *clazz;
    const char *internalDataPath;
    const char *externalDataPath;
    int sdkVersion;
    void *instance;
    void *configuration;
    void *assetManager;
    const char *obbPath;
} ANativeActivity;

typedef struct { float x, y; float r, g, b; } Vertex;
typedef struct { float angle; } PushConstants;

ANativeActivityCallbacks g_callbacks;

static uint32_t find_memory_type(VkPhysicalDeviceMemoryProperties *memProps, uint32_t typeBits, VkFlags props) {
    uint32_t i;
    for (i = 0; i < memProps->memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (memProps->memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    __android_log_print(4, "squashvk", "find_memory_type: no suitable memory type found!");
    return 0xFFFFFFFFu;
}

extern int pthread_create(unsigned long *thread, void *attr, void *(*start_routine)(void *), void *arg);

/* Runs on a dedicated pthread, NOT the main/UI thread: doing all this
 * synchronous, sometimes-blocking Vulkan setup (vkEnumeratePhysicalDevices,
 * surface creation, vkWaitForFences with an infinite timeout, etc.)
 * directly inside the ANativeActivityCallbacks::onNativeWindowCreated
 * callback deadlocked -- confirmed directly: the app hung indefinitely
 * right after a successful vkCreateInstance, and the emulator showed a
 * "System UI isn't responding" ANR. The callback runs on the app's main
 * thread, which some part of surface/driver setup needs free to service
 * Binder callbacks (e.g. SurfaceFlinger transaction acks) -- blocking it
 * with a long synchronous call sequence deadlocks exactly the thing it's
 * waiting on. Real native apps (e.g. android_native_app_glue) always run
 * their actual app/render logic on a separate thread for this reason;
 * the callback itself must return quickly. */
extern int usleep(int usec);

VkInstance g_instance = 0;
/* 0 = not ready yet, 1 = ready/success, -1 = vkCreateInstance failed. */
int g_instance_ready = 0;

/* Runs on its OWN, disposable thread, doing NOTHING but create the
 * VkInstance, then exits. See the doc comment on vulkan_thread_main below
 * for why this split is required -- it is not stylistic, it works around
 * a real, confirmed bug/limitation in this emulator's Vulkan driver
 * stack. */
void *create_instance_thread(void *window) {
    __android_log_print(4, "squashvk", "create_instance_thread: starting, window=%p", window);
    init_triangle_spirv();

    VkApplicationInfo appInfo;
    memset(&appInfo, 0, sizeof(appInfo));
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "squash android triangle";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "squash";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = (1u << 22);

    const char *instExts[2];
    instExts[0] = "VK_KHR_surface";
    instExts[1] = "VK_KHR_android_surface";
    VkInstanceCreateInfo instInfo;
    memset(&instInfo, 0, sizeof(instInfo));
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledExtensionCount = 2;
    instInfo.ppEnabledExtensionNames = instExts;

    VkResult vr = vkCreateInstance(&instInfo, 0, &g_instance);
    __android_log_print(4, "squashvk", "vkCreateInstance vr=%d instance=%p", (int)vr, (void*)g_instance);
    if (vr == VK_SUCCESS) g_instance_ready = 1; else g_instance_ready = -1;
    return 0;
}

/* Runs on a SECOND, separate thread from create_instance_thread above.
 * This split works around a real, confirmed bug/limitation in this
 * emulator's Vulkan driver stack (SwiftShader + the NDK ARM64->x86_64
 * translation shim): whichever thread calls vkCreateInstance becomes
 * unable to make any FURTHER real Vulkan driver call afterward -- it
 * doesn't crash and doesn't return, it just hangs forever with no signal,
 * no tombstone, and the thread's own /proc/<pid>/task/<tid> entry
 * eventually disappears. Confirmed directly, isolated by bisection: (1)
 * NOT a general "second external call" bug -- liblog's
 * __android_log_print already works fine called repeatedly, and
 * vkGetInstanceProcAddr (a lightweight lookup, no real driver work) also
 * works fine called three times in a row; (2) NOT tied to which function
 * is called second -- vkEnumeratePhysicalDevices, vkDestroyInstance, and
 * even calling vkCreateInstance a second time all reproduce the hang;
 * (3) IS specifically about the calling thread -- moving the second real
 * call to a DIFFERENT thread than the one that ran vkCreateInstance works
 * immediately, and that second thread can then make as many further real
 * Vulkan calls as needed with no issue at all (tested two calls in a row
 * on it successfully). So: exactly one thread ever calls vkCreateInstance
 * and does nothing else; a separate thread does everything else. */
void *vulkan_thread_main(void *window) {
    while (g_instance_ready == 0) usleep(10000);
    __android_log_print(4, "squashvk", "render thread: saw g_instance_ready=%d g_instance=%p", g_instance_ready, (void*)g_instance);
    if (g_instance_ready < 0) { __android_log_print(4, "squashvk", "vkCreateInstance failed, aborting"); return 0; }
    VkInstance instance = g_instance;
    VkResult vr;

    uint32_t physCount = 0;
    vkEnumeratePhysicalDevices(instance, &physCount, 0);
    __android_log_print(4, "squashvk", "physical device count=%u", physCount);
    if (physCount == 0) return 0;
    VkPhysicalDevice physDevices[8];
    if (physCount > 8) physCount = 8;
    vkEnumeratePhysicalDevices(instance, &physCount, physDevices);
    VkPhysicalDevice phys = physDevices[0];

    VkAndroidSurfaceCreateInfoKHR surfInfo;
    memset(&surfInfo, 0, sizeof(surfInfo));
    surfInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfInfo.window = window;
    VkSurfaceKHR surface = 0;
    vr = vkCreateAndroidSurfaceKHR(instance, &surfInfo, 0, &surface);
    __android_log_print(4, "squashvk", "vkCreateAndroidSurfaceKHR vr=%d surface=%p", (int)vr, (void*)surface);
    if (vr != VK_SUCCESS) return 0;

    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qfCount, 0);
    VkQueueFamilyProperties qfProps[16];
    if (qfCount > 16) qfCount = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qfCount, qfProps);
    uint32_t queueFamily = 0xFFFFFFFFu;
    uint32_t i;
    for (i = 0; i < qfCount; i++) {
        if (!(qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        VkBool32 presentOk = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &presentOk);
        if (presentOk) { queueFamily = i; break; }
    }
    __android_log_print(4, "squashvk", "queueFamily=%u", queueFamily);
    if (queueFamily == 0xFFFFFFFFu) return 0;

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo dqInfo;
    memset(&dqInfo, 0, sizeof(dqInfo));
    dqInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dqInfo.queueFamilyIndex = queueFamily;
    dqInfo.queueCount = 1;
    dqInfo.pQueuePriorities = &queuePriority;

    const char *devExts[1];
    devExts[0] = "VK_KHR_swapchain";
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

    VkDevice device = 0;
    vr = vkCreateDevice(phys, &devInfo, 0, &device);
    __android_log_print(4, "squashvk", "vkCreateDevice vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkQueue queue = 0;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    VkSurfaceCapabilitiesKHR caps;
    memset(&caps, 0, sizeof(caps));
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, 0);
    VkSurfaceFormatKHR fmts[32];
    if (fmtCount > 32) fmtCount = 32;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, fmts);
    VkFormat chosenFormat = (fmtCount > 0) ? fmts[0].format : VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR chosenColorSpace = (fmtCount > 0) ? fmts[0].colorSpace : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (i = 0; i < fmtCount; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) { chosenFormat = fmts[i].format; chosenColorSpace = fmts[i].colorSpace; break; }
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) { extent.width = 1080; extent.height = 2400; }
    __android_log_print(4, "squashvk", "surface extent=%ux%u minImageCount=%u", extent.width, extent.height, caps.minImageCount);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR scInfo;
    memset(&scInfo, 0, sizeof(scInfo));
    scInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scInfo.surface = surface;
    scInfo.minImageCount = imageCount;
    scInfo.imageFormat = chosenFormat;
    scInfo.imageColorSpace = chosenColorSpace;
    scInfo.imageExtent = extent;
    scInfo.imageArrayLayers = 1;
    scInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scInfo.preTransform = caps.currentTransform;
    scInfo.compositeAlpha = 0x1;
    scInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    scInfo.clipped = VK_TRUE;

    VkSwapchainKHR swapchain = 0;
    vr = vkCreateSwapchainKHR(device, &scInfo, 0, &swapchain);
    __android_log_print(4, "squashvk", "vkCreateSwapchainKHR vr=%d format=%d", (int)vr, (int)chosenFormat);
    if (vr != VK_SUCCESS) return 0;

    uint32_t swapImageCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, 0);
    VkImage swapImages[8];
    if (swapImageCount > 8) swapImageCount = 8;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages);
    __android_log_print(4, "squashvk", "swapImageCount=%u", swapImageCount);

    VkImageView swapViews[8];
    for (i = 0; i < swapImageCount; i++) {
        VkImageViewCreateInfo ivInfo;
        memset(&ivInfo, 0, sizeof(ivInfo));
        ivInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivInfo.image = swapImages[i];
        ivInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivInfo.format = chosenFormat;
        ivInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivInfo.subresourceRange.levelCount = 1;
        ivInfo.subresourceRange.layerCount = 1;
        vr = vkCreateImageView(device, &ivInfo, 0, &swapViews[i]);
        if (vr != VK_SUCCESS) { __android_log_print(4, "squashvk", "vkCreateImageView[%u] failed vr=%d", i, (int)vr); return 0; }
    }

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

    VkRenderPass renderPass = 0;
    vr = vkCreateRenderPass(device, &rpInfo, 0, &renderPass);
    __android_log_print(4, "squashvk", "vkCreateRenderPass vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkFramebuffer framebuffers[8];
    for (i = 0; i < swapImageCount; i++) {
        VkFramebufferCreateInfo fbInfo;
        memset(&fbInfo, 0, sizeof(fbInfo));
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &swapViews[i];
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;
        vr = vkCreateFramebuffer(device, &fbInfo, 0, &framebuffers[i]);
        if (vr != VK_SUCCESS) { __android_log_print(4, "squashvk", "vkCreateFramebuffer[%u] failed vr=%d", i, (int)vr); return 0; }
    }
    __android_log_print(4, "squashvk", "%u framebuffers created", swapImageCount);

    VkShaderModuleCreateInfo vsInfo;
    memset(&vsInfo, 0, sizeof(vsInfo));
    vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsInfo.codeSize = sizeof(g_triangle_vert_spv);
    vsInfo.pCode = g_triangle_vert_spv;
    VkShaderModule vsModule = 0;
    vr = vkCreateShaderModule(device, &vsInfo, 0, &vsModule);
    __android_log_print(4, "squashvk", "vkCreateShaderModule(vs) vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkShaderModuleCreateInfo fsInfo;
    memset(&fsInfo, 0, sizeof(fsInfo));
    fsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fsInfo.codeSize = sizeof(g_triangle_frag_spv);
    fsInfo.pCode = g_triangle_frag_spv;
    VkShaderModule fsModule = 0;
    vr = vkCreateShaderModule(device, &fsInfo, 0, &fsModule);
    __android_log_print(4, "squashvk", "vkCreateShaderModule(fs) vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

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
    binding.stride = sizeof(Vertex);
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

    VkViewport viewport;
    viewport.x = 0; viewport.y = 0;
    viewport.width = (float)extent.width; viewport.height = (float)extent.height;
    viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
    VkRect2D scissor;
    scissor.offset.x = 0; scissor.offset.y = 0;
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo vpState;
    memset(&vpState, 0, sizeof(vpState));
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = &viewport;
    vpState.scissorCount = 1;
    vpState.pScissors = &scissor;

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
    pcRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo plInfo;
    memset(&plInfo, 0, sizeof(plInfo));
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;

    VkPipelineLayout pipelineLayout = 0;
    vr = vkCreatePipelineLayout(device, &plInfo, 0, &pipelineLayout);
    __android_log_print(4, "squashvk", "vkCreatePipelineLayout vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

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
    gpInfo.layout = pipelineLayout;
    gpInfo.renderPass = renderPass;
    gpInfo.subpass = 0;
    gpInfo.basePipelineIndex = -1;

    VkPipeline pipeline = 0;
    vr = vkCreateGraphicsPipelines(device, 0, 1, &gpInfo, 0, &pipeline);
    __android_log_print(4, "squashvk", "vkCreateGraphicsPipelines vr=%d pipeline=%p", (int)vr, (void*)pipeline);
    if (vr != VK_SUCCESS) return 0;

    Vertex verts[3];
    verts[0].x = 0.0f;  verts[0].y = 0.5f;  verts[0].r = 1.0f; verts[0].g = 0.0f; verts[0].b = 0.0f;
    verts[1].x = 0.5f;  verts[1].y = -0.5f; verts[1].r = 0.0f; verts[1].g = 1.0f; verts[1].b = 0.0f;
    verts[2].x = -0.5f; verts[2].y = -0.5f; verts[2].r = 0.0f; verts[2].g = 0.0f; verts[2].b = 1.0f;

    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = sizeof(verts);
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer vertexBuffer = 0;
    vr = vkCreateBuffer(device, &bufInfo, 0, &vertexBuffer);
    __android_log_print(4, "squashvk", "vkCreateBuffer vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkMemoryRequirements memReq;
    memset(&memReq, 0, sizeof(memReq));
    vkGetBufferMemoryRequirements(device, vertexBuffer, &memReq);

    VkPhysicalDeviceMemoryProperties memProps;
    memset(&memProps, 0, sizeof(memProps));
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);

    uint32_t memTypeIdx = find_memory_type(&memProps, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    __android_log_print(4, "squashvk", "memTypeIdx=%u", memTypeIdx);
    if (memTypeIdx == 0xFFFFFFFFu) return 0;

    VkMemoryAllocateInfo allocInfo;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIdx;
    VkDeviceMemory vertexMemory = 0;
    vr = vkAllocateMemory(device, &allocInfo, 0, &vertexMemory);
    __android_log_print(4, "squashvk", "vkAllocateMemory vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;
    vkBindBufferMemory(device, vertexBuffer, vertexMemory, 0);

    void *mapped = 0;
    vkMapMemory(device, vertexMemory, 0, sizeof(verts), 0, &mapped);
    memcpy(mapped, verts, sizeof(verts));
    vkUnmapMemory(device, vertexMemory);
    __android_log_print(4, "squashvk", "vertex buffer created+uploaded");

    VkCommandPoolCreateInfo cpInfo;
    memset(&cpInfo, 0, sizeof(cpInfo));
    cpInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpInfo.queueFamilyIndex = queueFamily;
    VkCommandPool commandPool = 0;
    vr = vkCreateCommandPool(device, &cpInfo, 0, &commandPool);
    __android_log_print(4, "squashvk", "vkCreateCommandPool vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkCommandBufferAllocateInfo cbAllocInfo;
    memset(&cbAllocInfo, 0, sizeof(cbAllocInfo));
    cbAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAllocInfo.commandPool = commandPool;
    cbAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAllocInfo.commandBufferCount = swapImageCount;
    VkCommandBuffer cmdBufs[8];
    vr = vkAllocateCommandBuffers(device, &cbAllocInfo, cmdBufs);
    __android_log_print(4, "squashvk", "vkAllocateCommandBuffers vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkSemaphoreCreateInfo semInfo;
    memset(&semInfo, 0, sizeof(semInfo));
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore imageAvailable = 0, renderFinished = 0;
    vkCreateSemaphore(device, &semInfo, 0, &imageAvailable);
    vkCreateSemaphore(device, &semInfo, 0, &renderFinished);

    VkFenceCreateInfo fenceInfo;
    memset(&fenceInfo, 0, sizeof(fenceInfo));
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence inFlightFence = 0;
    vkCreateFence(device, &fenceInfo, 0, &inFlightFence);
    __android_log_print(4, "squashvk", "sync objects created -- rendering one frame");

    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, ~0ull);
    vkResetFences(device, 1, &inFlightFence);

    uint32_t imageIndex = 0;
    vr = vkAcquireNextImageKHR(device, swapchain, ~0ull, imageAvailable, 0, &imageIndex);
    __android_log_print(4, "squashvk", "vkAcquireNextImageKHR vr=%d imageIndex=%u", (int)vr, imageIndex);
    if (vr != VK_SUCCESS && vr != VK_SUBOPTIMAL_KHR) return 0;

    VkCommandBuffer cmd = cmdBufs[imageIndex];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo;
    memset(&beginInfo, 0, sizeof(beginInfo));
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkClearValue clearValue;
    memset(&clearValue, 0, sizeof(clearValue));
    clearValue.color.float32[0] = 0.05f;
    clearValue.color.float32[1] = 0.05f;
    clearValue.color.float32[2] = 0.1f;
    clearValue.color.float32[3] = 1.0f;

    VkRenderPassBeginInfo rpBegin;
    memset(&rpBegin, 0, sizeof(rpBegin));
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = renderPass;
    rpBegin.framebuffer = framebuffers[imageIndex];
    rpBegin.renderArea.offset.x = 0; rpBegin.renderArea.offset.y = 0;
    rpBegin.renderArea.extent = extent;
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkDeviceSize offset0 = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset0);

    PushConstants pc;
    pc.angle = 0.0f;
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo;
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished;
    vr = vkQueueSubmit(queue, 1, &submitInfo, inFlightFence);
    __android_log_print(4, "squashvk", "vkQueueSubmit vr=%d", (int)vr);

    VkPresentInfoKHR presentInfo;
    memset(&presentInfo, 0, sizeof(presentInfo));
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    vr = vkQueuePresentKHR(queue, &presentInfo);
    __android_log_print(4, "squashvk", "vkQueuePresentKHR vr=%d -- TRIANGLE FRAME PRESENTED", (int)vr);

    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, ~0ull);
    __android_log_print(4, "squashvk", "done: triangle rendered and presented successfully");
    return 0;
}

void on_native_window_created(void *activity, void *window) {
    unsigned long t1, t2;
    __android_log_print(4, "squashvk", "onNativeWindowCreated: spawning instance+render threads, window=%p", window);
    pthread_create(&t1, 0, create_instance_thread, window);
    pthread_create(&t2, 0, vulkan_thread_main, window);
}

int main(void *activity, void *savedState, long savedStateSize) {
    ANativeActivity *act = (ANativeActivity *)activity;
    g_callbacks.onNativeWindowCreated = on_native_window_created;
    act->callbacks = &g_callbacks;
    __android_log_print(4, "squashvk", "main: onCreate, waiting for native window...");
    return 0;
}

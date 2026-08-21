#ifndef _VULKAN_CORE_H
#define _VULKAN_CORE_H
/* Minimal Vulkan 1.0 core surface -- just enough to create an instance,
 * pick a physical device, create a logical device + queue, build a render
 * pass/pipeline (with a push-constant-driven vertex shader), record command
 * buffers, and present a swapchain image, for a single-window animated
 * triangle demo. Vulkan (unlike D3D11/DXGI) is NOT COM/vtable-based: every
 * function is a flat, ordinary C export from vulkan-1.dll, so squash's
 * existing .lib-backed import resolution handles this exactly like any
 * other Windows API call -- no special dispatch machinery needed. Hand-
 * written to match the real ABI (struct layout, enum values, function
 * signatures) since this calls into the genuine system vulkan-1.dll. */
#include "include/windows.h"

typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkSampleMask;

#define VK_TRUE 1
#define VK_FALSE 0
#define VK_NULL_HANDLE 0

/* Opaque dispatchable handles (real pointer-sized handles on 64-bit). */
#define VK_DEFINE_HANDLE(name) typedef struct name##_T *name
VK_DEFINE_HANDLE(VkInstance);
VK_DEFINE_HANDLE(VkPhysicalDevice);
VK_DEFINE_HANDLE(VkDevice);
VK_DEFINE_HANDLE(VkQueue);
VK_DEFINE_HANDLE(VkCommandBuffer);

/* Non-dispatchable handles -- also plain pointer-sized on 64-bit builds
 * (VK_USE_64_BIT_PTR_DEFINES is implied on any 64-bit target). */
#define VK_DEFINE_NON_DISPATCHABLE_HANDLE(name) typedef struct name##_T *name
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSurfaceKHR);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSwapchainKHR);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImage);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImageView);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkRenderPass);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFramebuffer);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipelineLayout);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipeline);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipelineCache);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkShaderModule);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkCommandPool);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSemaphore);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFence);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkBuffer);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDeviceMemory);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorSetLayout);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorPool);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorSet);
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSampler);

typedef enum VkResult {
    VK_SUCCESS = 0,
    VK_NOT_READY = 1,
    VK_TIMEOUT = 2,
    VK_SUBOPTIMAL_KHR = 1000001003
} VkResult;

typedef enum VkStructureType {
    VK_STRUCTURE_TYPE_APPLICATION_INFO = 0,
    VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1,
    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2,
    VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3,
    VK_STRUCTURE_TYPE_SUBMIT_INFO = 4,
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5,
    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO = 8,
    VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO = 9,
    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO = 16,
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO = 18,
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO = 19,
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO = 22,
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO = 23,
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO = 24,
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO = 26,
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO = 30,
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO = 39,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO = 40,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO = 42,
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO = 43,
    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO = 18000, /* placeholder overwritten below */
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO = 28,
    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO = 37,
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO = 38,
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12,
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO = 15,
    VK_STRUCTURE_TYPE_PRESENT_INFO_KHR = 1000001001,
    VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR = 1000001000,
    VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR = 1000009000,
    VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR = 1000004000,
    VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO = 29,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO = 32,
    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO = 33,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO = 34,
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET = 35,
    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER = 44,
    VK_STRUCTURE_TYPE_MEMORY_BARRIER = 46
} VkStructureType;
/* Real value for PIPELINE_SHADER_STAGE_CREATE_INFO is 18 -- fix the
 * placeholder above (can't reuse the literal 18 twice in one enum). */
#undef VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO ((VkStructureType)18)

typedef enum VkFormat {
    VK_FORMAT_UNDEFINED = 0,
    VK_FORMAT_R32G32_SFLOAT = 103,
    VK_FORMAT_R32G32B32_SFLOAT = 106,
    VK_FORMAT_R32G32B32A32_SFLOAT = 109,
    VK_FORMAT_B8G8R8A8_UNORM = 44,
    VK_FORMAT_B8G8R8A8_SRGB = 50
} VkFormat;

/* VkBlendFactor / VkBlendOp: VkPipelineColorBlendAttachmentState's
 * srcColorBlendFactor/dstColorBlendFactor/colorBlendOp/... fields above are
 * plain uint32_t (this project's existing convention for enum-typed struct
 * fields, see that struct's own definition), so these are #defines rather
 * than a named enum type -- added for the text renderer's alpha blending
 * (text_renderer_vk.c), the first pipeline in this project to enable it. */
#define VK_BLEND_FACTOR_ZERO 0
#define VK_BLEND_FACTOR_ONE 1
#define VK_BLEND_FACTOR_SRC_ALPHA 6
#define VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA 7
#define VK_BLEND_OP_ADD 0

typedef enum VkImageLayout {
    VK_IMAGE_LAYOUT_UNDEFINED = 0,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL = 2,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR = 1000001002
} VkImageLayout;

typedef enum VkSharingMode { VK_SHARING_MODE_EXCLUSIVE = 0, VK_SHARING_MODE_CONCURRENT = 1 } VkSharingMode;
typedef enum VkPresentModeKHR {
    VK_PRESENT_MODE_IMMEDIATE_KHR = 0,
    VK_PRESENT_MODE_FIFO_KHR = 2
} VkPresentModeKHR;
typedef enum VkColorSpaceKHR { VK_COLOR_SPACE_SRGB_NONLINEAR_KHR = 0 } VkColorSpaceKHR;

typedef enum VkImageViewType { VK_IMAGE_VIEW_TYPE_2D = 1 } VkImageViewType;
typedef enum VkComponentSwizzle { VK_COMPONENT_SWIZZLE_IDENTITY = 0 } VkComponentSwizzle;
typedef enum VkPrimitiveTopology {
    VK_PRIMITIVE_TOPOLOGY_POINT_LIST = 0,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 3
} VkPrimitiveTopology;
typedef enum VkPolygonMode { VK_POLYGON_MODE_FILL = 0 } VkPolygonMode;
typedef enum VkFrontFace { VK_FRONT_FACE_CLOCKWISE = 1 } VkFrontFace;
typedef enum VkVertexInputRate { VK_VERTEX_INPUT_RATE_VERTEX = 0 } VkVertexInputRate;
typedef enum VkShaderStageFlagBits {
    VK_SHADER_STAGE_VERTEX_BIT = 0x1,
    VK_SHADER_STAGE_FRAGMENT_BIT = 0x10,
    VK_SHADER_STAGE_COMPUTE_BIT = 0x20
} VkShaderStageFlagBits;
typedef enum VkAttachmentLoadOp { VK_ATTACHMENT_LOAD_OP_CLEAR = 1, VK_ATTACHMENT_LOAD_OP_DONT_CARE = 2 } VkAttachmentLoadOp;
typedef enum VkAttachmentStoreOp { VK_ATTACHMENT_STORE_OP_STORE = 0, VK_ATTACHMENT_STORE_OP_DONT_CARE = 1 } VkAttachmentStoreOp;
typedef enum VkPipelineBindPoint { VK_PIPELINE_BIND_POINT_GRAPHICS = 0, VK_PIPELINE_BIND_POINT_COMPUTE = 1 } VkPipelineBindPoint;
typedef enum VkDescriptorType { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER = 7 } VkDescriptorType;
typedef enum VkCommandBufferLevel { VK_COMMAND_BUFFER_LEVEL_PRIMARY = 0 } VkCommandBufferLevel;
typedef enum VkSubpassContents { VK_SUBPASS_CONTENTS_INLINE = 0 } VkSubpassContents;

#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 0x10
#define VK_QUEUE_GRAPHICS_BIT 0x1
#define VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 0x400
#define VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT 0x2
#define VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT 0x4
#define VK_COLOR_COMPONENT_R_BIT 0x1
#define VK_COLOR_COMPONENT_G_BIT 0x2
#define VK_COLOR_COMPONENT_B_BIT 0x4
#define VK_COLOR_COMPONENT_A_BIT 0x8
#define VK_SAMPLE_COUNT_1_BIT 0x1
#define VK_BUFFER_USAGE_VERTEX_BUFFER_BIT 0x80
#define VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 0x20
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 0x2
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 0x4
#define VK_FENCE_CREATE_SIGNALED_BIT 0x1

/* Compute-shader / storage-buffer access + pipeline-stage flags, needed to
 * barrier a compute shader's SSBO writes against the graphics pipeline's
 * vertex-input reads of that same buffer (see particles_physics.c's
 * GPU-resident compute-writes-directly-into-the-vertex-buffer design). */
#define VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT 0x4
#define VK_ACCESS_SHADER_READ_BIT 0x20
#define VK_ACCESS_SHADER_WRITE_BIT 0x40
#define VK_PIPELINE_STAGE_VERTEX_INPUT_BIT 0x4
#define VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT 0x800

typedef struct VkExtent2D { uint32_t width, height; } VkExtent2D;
typedef struct VkOffset2D { int32_t x, y; } VkOffset2D;
typedef struct VkRect2D { VkOffset2D offset; VkExtent2D extent; } VkRect2D;
typedef struct VkViewport { float x, y, width, height, minDepth, maxDepth; } VkViewport;

typedef struct VkExtent3D { uint32_t width, height, depth; } VkExtent3D;

typedef struct VkApplicationInfo {
    VkStructureType sType;
    const void *pNext;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    const VkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkQueueFamilyProperties {
    VkFlags queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    VkExtent3D minImageTransferGranularity;
} VkQueueFamilyProperties;

typedef struct VkDeviceQueueCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
    uint32_t queueCount;
    const float *pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkPhysicalDeviceFeatures {
    /* Real struct has ~55 VkBool32 feature flags; this demo never enables
     * any of them (passes an all-zero struct), so only the total size
     * needs to be right for the CreateInfo it's embedded/pointed into --
     * declared as a byte array of the real struct's exact size (220 bytes
     * = 55 * 4-byte VkBool32 fields) instead of naming each one. */
    uint32_t _reserved[55];
} VkPhysicalDeviceFeatures;

typedef struct VkDeviceCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queueCreateInfoCount;
    const VkDeviceQueueCreateInfo *pQueueCreateInfos;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
    const VkPhysicalDeviceFeatures *pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkSurfaceCapabilitiesKHR {
    uint32_t minImageCount;
    uint32_t maxImageCount;
    VkExtent2D currentExtent;
    VkExtent2D minImageExtent;
    VkExtent2D maxImageExtent;
    uint32_t maxImageArrayLayers;
    VkFlags supportedTransforms;
    VkFlags currentTransform;
    VkFlags supportedCompositeAlpha;
    VkFlags supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;

typedef struct VkSurfaceFormatKHR { VkFormat format; VkColorSpaceKHR colorSpace; } VkSurfaceFormatKHR;

typedef struct VkSwapchainCreateInfoKHR {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkSurfaceKHR surface;
    uint32_t minImageCount;
    VkFormat imageFormat;
    VkColorSpaceKHR imageColorSpace;
    VkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    VkFlags imageUsage;
    VkSharingMode imageSharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
    VkFlags preTransform;
    VkFlags compositeAlpha;
    VkPresentModeKHR presentMode;
    VkBool32 clipped;
    VkSwapchainKHR oldSwapchain;
} VkSwapchainCreateInfoKHR;

typedef struct VkComponentMapping {
    VkComponentSwizzle r, g, b, a;
} VkComponentMapping;

typedef struct VkImageSubresourceRange {
    VkFlags aspectMask;
    uint32_t baseMipLevel, levelCount, baseArrayLayer, layerCount;
} VkImageSubresourceRange;
#define VK_IMAGE_ASPECT_COLOR_BIT 0x1

typedef struct VkImageViewCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkImage image;
    VkImageViewType viewType;
    VkFormat format;
    VkComponentMapping components;
    VkImageSubresourceRange subresourceRange;
} VkImageViewCreateInfo;

typedef struct VkAttachmentDescription {
    VkFlags flags;
    VkFormat format;
    uint32_t samples; /* VkSampleCountFlagBits */
    VkAttachmentLoadOp loadOp;
    VkAttachmentStoreOp storeOp;
    VkAttachmentLoadOp stencilLoadOp;
    VkAttachmentStoreOp stencilStoreOp;
    VkImageLayout initialLayout;
    VkImageLayout finalLayout;
} VkAttachmentDescription;

typedef struct VkAttachmentReference {
    uint32_t attachment;
    VkImageLayout layout;
} VkAttachmentReference;

typedef struct VkSubpassDescription {
    VkFlags flags;
    VkPipelineBindPoint pipelineBindPoint;
    uint32_t inputAttachmentCount;
    const VkAttachmentReference *pInputAttachments;
    uint32_t colorAttachmentCount;
    const VkAttachmentReference *pColorAttachments;
    const VkAttachmentReference *pResolveAttachments;
    const VkAttachmentReference *pDepthStencilAttachment;
    uint32_t preserveAttachmentCount;
    const uint32_t *pPreserveAttachments;
} VkSubpassDescription;

typedef struct VkSubpassDependency {
    uint32_t srcSubpass, dstSubpass;
    VkFlags srcStageMask, dstStageMask;
    VkFlags srcAccessMask, dstAccessMask;
    VkFlags dependencyFlags;
} VkSubpassDependency;
#define VK_SUBPASS_EXTERNAL (~0u)
#define VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT 0x100

typedef struct VkRenderPassCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t attachmentCount;
    const VkAttachmentDescription *pAttachments;
    uint32_t subpassCount;
    const VkSubpassDescription *pSubpasses;
    uint32_t dependencyCount;
    const VkSubpassDependency *pDependencies;
} VkRenderPassCreateInfo;

typedef struct VkFramebufferCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkRenderPass renderPass;
    uint32_t attachmentCount;
    const VkImageView *pAttachments;
    uint32_t width, height, layers;
} VkFramebufferCreateInfo;

typedef struct VkShaderModuleCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    SIZE_T codeSize;
    const uint32_t *pCode;
} VkShaderModuleCreateInfo;

typedef struct VkPipelineShaderStageCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkShaderStageFlagBits stage;
    VkShaderModule module;
    const char *pName;
    const void *pSpecializationInfo;
} VkPipelineShaderStageCreateInfo;

typedef struct VkVertexInputBindingDescription {
    uint32_t binding, stride;
    VkVertexInputRate inputRate;
} VkVertexInputBindingDescription;

typedef struct VkVertexInputAttributeDescription {
    uint32_t location, binding;
    VkFormat format;
    uint32_t offset;
} VkVertexInputAttributeDescription;

typedef struct VkPipelineVertexInputStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t vertexBindingDescriptionCount;
    const VkVertexInputBindingDescription *pVertexBindingDescriptions;
    uint32_t vertexAttributeDescriptionCount;
    const VkVertexInputAttributeDescription *pVertexAttributeDescriptions;
} VkPipelineVertexInputStateCreateInfo;

typedef struct VkPipelineInputAssemblyStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkPrimitiveTopology topology;
    VkBool32 primitiveRestartEnable;
} VkPipelineInputAssemblyStateCreateInfo;

typedef struct VkPipelineViewportStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t viewportCount;
    const VkViewport *pViewports;
    uint32_t scissorCount;
    const VkRect2D *pScissors;
} VkPipelineViewportStateCreateInfo;

typedef struct VkPipelineRasterizationStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkBool32 depthClampEnable;
    VkBool32 rasterizerDiscardEnable;
    VkPolygonMode polygonMode;
    VkFlags cullMode;
    VkFrontFace frontFace;
    VkBool32 depthBiasEnable;
    float depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor;
    float lineWidth;
} VkPipelineRasterizationStateCreateInfo;

typedef struct VkPipelineMultisampleStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t rasterizationSamples; /* VkSampleCountFlagBits */
    VkBool32 sampleShadingEnable;
    float minSampleShading;
    const VkSampleMask *pSampleMask;
    VkBool32 alphaToCoverageEnable;
    VkBool32 alphaToOneEnable;
} VkPipelineMultisampleStateCreateInfo;

typedef struct VkPipelineColorBlendAttachmentState {
    VkBool32 blendEnable;
    uint32_t srcColorBlendFactor, dstColorBlendFactor, colorBlendOp;
    uint32_t srcAlphaBlendFactor, dstAlphaBlendFactor, alphaBlendOp;
    VkFlags colorWriteMask;
} VkPipelineColorBlendAttachmentState;

typedef struct VkPipelineColorBlendStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkBool32 logicOpEnable;
    uint32_t logicOp;
    uint32_t attachmentCount;
    const VkPipelineColorBlendAttachmentState *pAttachments;
    float blendConstants[4];
} VkPipelineColorBlendStateCreateInfo;

typedef struct VkPushConstantRange {
    VkFlags stageFlags;
    uint32_t offset, size;
} VkPushConstantRange;

typedef struct VkPipelineLayoutCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t setLayoutCount;
    const VkDescriptorSetLayout *pSetLayouts;
    uint32_t pushConstantRangeCount;
    const VkPushConstantRange *pPushConstantRanges;
} VkPipelineLayoutCreateInfo;

typedef struct VkDescriptorSetLayoutBinding {
    uint32_t binding;
    VkDescriptorType descriptorType;
    uint32_t descriptorCount;
    VkFlags stageFlags;
    const void *pImmutableSamplers;
} VkDescriptorSetLayoutBinding;

typedef struct VkDescriptorSetLayoutCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t bindingCount;
    const VkDescriptorSetLayoutBinding *pBindings;
} VkDescriptorSetLayoutCreateInfo;

typedef struct VkDescriptorPoolSize {
    VkDescriptorType type;
    uint32_t descriptorCount;
} VkDescriptorPoolSize;

typedef struct VkDescriptorPoolCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t maxSets;
    uint32_t poolSizeCount;
    const VkDescriptorPoolSize *pPoolSizes;
} VkDescriptorPoolCreateInfo;

typedef struct VkDescriptorSetAllocateInfo {
    VkStructureType sType;
    const void *pNext;
    VkDescriptorPool descriptorPool;
    uint32_t descriptorSetCount;
    const VkDescriptorSetLayout *pSetLayouts;
} VkDescriptorSetAllocateInfo;

typedef struct VkDescriptorBufferInfo {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize range;
} VkDescriptorBufferInfo;

typedef struct VkWriteDescriptorSet {
    VkStructureType sType;
    const void *pNext;
    VkDescriptorSet dstSet;
    uint32_t dstBinding;
    uint32_t dstArrayElement;
    uint32_t descriptorCount;
    VkDescriptorType descriptorType;
    const void *pImageInfo;
    const VkDescriptorBufferInfo *pBufferInfo;
    const void *pTexelBufferView;
} VkWriteDescriptorSet;

typedef struct VkGraphicsPipelineCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t stageCount;
    const VkPipelineShaderStageCreateInfo *pStages;
    const VkPipelineVertexInputStateCreateInfo *pVertexInputState;
    const VkPipelineInputAssemblyStateCreateInfo *pInputAssemblyState;
    const void *pTessellationState;
    const VkPipelineViewportStateCreateInfo *pViewportState;
    const VkPipelineRasterizationStateCreateInfo *pRasterizationState;
    const VkPipelineMultisampleStateCreateInfo *pMultisampleState;
    const void *pDepthStencilState;
    const VkPipelineColorBlendStateCreateInfo *pColorBlendState;
    const void *pDynamicState;
    VkPipelineLayout layout;
    VkRenderPass renderPass;
    uint32_t subpass;
    VkPipeline basePipelineHandle;
    int32_t basePipelineIndex;
} VkGraphicsPipelineCreateInfo;

typedef struct VkComputePipelineCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkPipelineShaderStageCreateInfo stage;
    VkPipelineLayout layout;
    VkPipeline basePipelineHandle;
    int32_t basePipelineIndex;
} VkComputePipelineCreateInfo;

typedef struct VkCommandPoolCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
} VkCommandPoolCreateInfo;

typedef struct VkCommandBufferAllocateInfo {
    VkStructureType sType;
    const void *pNext;
    VkCommandPool commandPool;
    VkCommandBufferLevel level;
    uint32_t commandBufferCount;
} VkCommandBufferAllocateInfo;

typedef struct VkCommandBufferBeginInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    const void *pInheritanceInfo;
} VkCommandBufferBeginInfo;

typedef union VkClearColorValue { float float32[4]; int32_t int32[4]; uint32_t uint32[4]; } VkClearColorValue;
typedef union VkClearValue { VkClearColorValue color; } VkClearValue;

typedef struct VkRenderPassBeginInfo {
    VkStructureType sType;
    const void *pNext;
    VkRenderPass renderPass;
    VkFramebuffer framebuffer;
    VkRect2D renderArea;
    uint32_t clearValueCount;
    const VkClearValue *pClearValues;
} VkRenderPassBeginInfo;

typedef struct VkSemaphoreCreateInfo { VkStructureType sType; const void *pNext; VkFlags flags; } VkSemaphoreCreateInfo;
typedef struct VkFenceCreateInfo { VkStructureType sType; const void *pNext; VkFlags flags; } VkFenceCreateInfo;

typedef struct VkSubmitInfo {
    VkStructureType sType;
    const void *pNext;
    uint32_t waitSemaphoreCount;
    const VkSemaphore *pWaitSemaphores;
    const VkFlags *pWaitDstStageMask;
    uint32_t commandBufferCount;
    const VkCommandBuffer *pCommandBuffers;
    uint32_t signalSemaphoreCount;
    const VkSemaphore *pSignalSemaphores;
} VkSubmitInfo;

typedef struct VkPresentInfoKHR {
    VkStructureType sType;
    const void *pNext;
    uint32_t waitSemaphoreCount;
    const VkSemaphore *pWaitSemaphores;
    uint32_t swapchainCount;
    const VkSwapchainKHR *pSwapchains;
    const uint32_t *pImageIndices;
    VkResult *pResults;
} VkPresentInfoKHR;

typedef struct VkBufferCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkDeviceSize size;
    VkFlags usage;
    VkSharingMode sharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
} VkBufferCreateInfo;

typedef struct VkMemoryRequirements {
    VkDeviceSize size, alignment;
    uint32_t memoryTypeBits;
} VkMemoryRequirements;

typedef struct VkMemoryType { VkFlags propertyFlags; uint32_t heapIndex; } VkMemoryType;
typedef struct VkMemoryHeap { VkDeviceSize size; VkFlags flags; } VkMemoryHeap;
typedef struct VkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[32];
    uint32_t memoryHeapCount;
    VkMemoryHeap memoryHeaps[16];
} VkPhysicalDeviceMemoryProperties;

typedef struct VkMemoryAllocateInfo {
    VkStructureType sType;
    const void *pNext;
    VkDeviceSize allocationSize;
    uint32_t memoryTypeIndex;
} VkMemoryAllocateInfo;

/* --- Core functions (flat exports from vulkan-1.dll) --- */
VkResult WINAPI vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const void *pAllocator, VkInstance *pInstance);
void WINAPI vkDestroyInstance(VkInstance instance, const void *pAllocator);
VkResult WINAPI vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices);
void WINAPI vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties);
void WINAPI vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties);
VkResult WINAPI vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const void *pAllocator, VkDevice *pDevice);
void WINAPI vkDestroyDevice(VkDevice device, const void *pAllocator);
void WINAPI vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue);
void WINAPI vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const void *pAllocator);
VkResult WINAPI vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported);
VkResult WINAPI vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities);
VkResult WINAPI vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats);
VkResult WINAPI vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes);
VkResult WINAPI vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const void *pAllocator, VkSwapchainKHR *pSwapchain);
void WINAPI vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const void *pAllocator);
VkResult WINAPI vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages);
VkResult WINAPI vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex);
VkResult WINAPI vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo);
VkResult WINAPI vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const void *pAllocator, VkImageView *pView);
void WINAPI vkDestroyImageView(VkDevice device, VkImageView imageView, const void *pAllocator);
VkResult WINAPI vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const void *pAllocator, VkRenderPass *pRenderPass);
void WINAPI vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const void *pAllocator);
VkResult WINAPI vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo, const void *pAllocator, VkFramebuffer *pFramebuffer);
void WINAPI vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const void *pAllocator);
VkResult WINAPI vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const void *pAllocator, VkShaderModule *pShaderModule);
void WINAPI vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const void *pAllocator);
VkResult WINAPI vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const void *pAllocator, VkPipelineLayout *pPipelineLayout);
void WINAPI vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const void *pAllocator);
VkResult WINAPI vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const void *pAllocator, VkPipeline *pPipelines);
VkResult WINAPI vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo *pCreateInfos, const void *pAllocator, VkPipeline *pPipelines);
void WINAPI vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const void *pAllocator);
VkResult WINAPI vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, const void *pAllocator, VkDescriptorSetLayout *pSetLayout);
void WINAPI vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const void *pAllocator);
VkResult WINAPI vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo, const void *pAllocator, VkDescriptorPool *pDescriptorPool);
void WINAPI vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const void *pAllocator);
/* Bulk-frees every set allocated from `descriptorPool` at once, without
 * needing VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT (that flag is
 * only required for vkFreeDescriptorSets, freeing individual sets) -- see
 * SQW/image_renderer_vk.c's sqw_image_renderer_reset_pool(), called once
 * per page navigation instead of freeing each <img>'s own set one at a
 * time. `flags` is a reserved VkDescriptorPoolResetFlags, always 0 today. */
VkResult WINAPI vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, uint32_t flags);
VkResult WINAPI vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets);
void WINAPI vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount, const void *pDescriptorCopies);
void WINAPI vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets);
void WINAPI vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
VkResult WINAPI vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const void *pAllocator, VkCommandPool *pCommandPool);
void WINAPI vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const void *pAllocator);
VkResult WINAPI vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers);
VkResult WINAPI vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkFlags flags);
VkResult WINAPI vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo);
VkResult WINAPI vkEndCommandBuffer(VkCommandBuffer commandBuffer);
void WINAPI vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents);
void WINAPI vkCmdEndRenderPass(VkCommandBuffer commandBuffer);
void WINAPI vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
void WINAPI vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets);
void WINAPI vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports);
void WINAPI vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors);
void WINAPI vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkFlags stageFlags, uint32_t offset, uint32_t size, const void *pValues);
void WINAPI vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
VkResult WINAPI vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo, const void *pAllocator, VkSemaphore *pSemaphore);
void WINAPI vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, const void *pAllocator);
VkResult WINAPI vkCreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo, const void *pAllocator, VkFence *pFence);
void WINAPI vkDestroyFence(VkDevice device, VkFence fence, const void *pAllocator);
VkResult WINAPI vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout);
VkResult WINAPI vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences);
VkResult WINAPI vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence);
VkResult WINAPI vkDeviceWaitIdle(VkDevice device);
VkResult WINAPI vkQueueWaitIdle(VkQueue queue);
VkResult WINAPI vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo, const void *pAllocator, VkBuffer *pBuffer);
void WINAPI vkDestroyBuffer(VkDevice device, VkBuffer buffer, const void *pAllocator);
void WINAPI vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements);
VkResult WINAPI vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo, const void *pAllocator, VkDeviceMemory *pMemory);
void WINAPI vkFreeMemory(VkDevice device, VkDeviceMemory memory, const void *pAllocator);
VkResult WINAPI vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);
VkResult WINAPI vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkFlags flags, void **ppData);
void WINAPI vkUnmapMemory(VkDevice device, VkDeviceMemory memory);

/* --- Image barriers + buffer copy: needed to read back a rendered
 * swapchain image as ground truth (bypassing GDI/DWM screen capture, which
 * proved unreliable for D3D11's equivalent check) -- transition the image
 * to TRANSFER_SRC_OPTIMAL, copy to a host-visible buffer, map, inspect. */
#define VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL 6
#define VK_ACCESS_TRANSFER_READ_BIT 0x800
#define VK_ACCESS_MEMORY_READ_BIT 0x8000
#define VK_PIPELINE_STAGE_TRANSFER_BIT 0x1000
#define VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT 0x2000
#define VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT 0x1
#define VK_BUFFER_USAGE_TRANSFER_DST_BIT 0x2
#define VK_QUEUE_FAMILY_IGNORED (~0u)

typedef struct VkImageMemoryBarrier {
    VkStructureType sType;
    const void *pNext;
    VkFlags srcAccessMask;
    VkFlags dstAccessMask;
    VkImageLayout oldLayout;
    VkImageLayout newLayout;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
    VkImage image;
    VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier;
#define VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER ((VkStructureType)45)

typedef struct VkBufferMemoryBarrier {
    VkStructureType sType;
    const void *pNext;
    VkFlags srcAccessMask;
    VkFlags dstAccessMask;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize size;
} VkBufferMemoryBarrier;

typedef struct VkOffset3D { int32_t x, y, z; } VkOffset3D;
typedef struct VkImageSubresourceLayers {
    VkFlags aspectMask;
    uint32_t mipLevel;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} VkImageSubresourceLayers;
typedef struct VkBufferImageCopy {
    VkDeviceSize bufferOffset;
    uint32_t bufferRowLength;
    uint32_t bufferImageHeight;
    VkImageSubresourceLayers imageSubresource;
    VkOffset3D imageOffset;
    VkExtent3D imageExtent;
} VkBufferImageCopy;

void WINAPI vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkFlags srcStageMask, VkFlags dstStageMask, VkFlags dependencyFlags,
    uint32_t memoryBarrierCount, const void *pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount, const void *pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers);
void WINAPI vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
    VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions);

/* --- Sampled-texture support (VkImage/VkSampler + upload), added for
 * SQW's Vulkan bitmap-font glyph-atlas text renderer (text_renderer_vk.c)
 * -- the first thing in this project to actually sample a texture rather
 * than just draw flat-colored/vertex-colored triangles. Real Vulkan 1.0
 * core enum/sType values throughout (this is a real loader + real ICD at
 * the other end, not squash's own ABI, so these have to be exactly right,
 * not just internally consistent). */
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO ((VkStructureType)14)
#define VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO ((VkStructureType)31)

/* VK_FORMAT_R8_UNORM: extends the VkFormat enum above (already-declared
 * enumerators can't be re-added to that typedef, so this is a #define
 * alias of the correct enumerator value, same trick already used for
 * VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER above). */
#define VK_FORMAT_R8_UNORM ((VkFormat)9)
/* VK_FORMAT_R8G8B8A8_UNORM: same extend-the-enum trick, for decoded
 * <img>/CSS background-image RGBA8 pixels (SQW/image_cache.c), real
 * Vulkan 1.0 core enumerator value 37. */
#define VK_FORMAT_R8G8B8A8_UNORM ((VkFormat)37)
#define VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ((VkImageLayout)5)
#define VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ((VkImageLayout)7)

typedef enum VkImageType { VK_IMAGE_TYPE_2D = 1 } VkImageType;
typedef enum VkImageTiling { VK_IMAGE_TILING_OPTIMAL = 0 } VkImageTiling;
typedef enum VkFilter { VK_FILTER_NEAREST = 0, VK_FILTER_LINEAR = 1 } VkFilter;
typedef enum VkSamplerMipmapMode { VK_SAMPLER_MIPMAP_MODE_NEAREST = 0 } VkSamplerMipmapMode;
typedef enum VkSamplerAddressMode { VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE = 2 } VkSamplerAddressMode;
typedef enum VkBorderColor { VK_BORDER_COLOR_INT_TRANSPARENT_BLACK = 1 } VkBorderColor;
#define VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ((VkDescriptorType)1)

#define VK_IMAGE_USAGE_TRANSFER_SRC_BIT 0x1
#define VK_IMAGE_USAGE_TRANSFER_DST_BIT 0x2
#define VK_IMAGE_USAGE_SAMPLED_BIT 0x4
#define VK_BUFFER_USAGE_TRANSFER_SRC_BIT 0x1
#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 0x1
#define VK_ACCESS_TRANSFER_WRITE_BIT 0x1000
#define VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT 0x80
#define VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 0x1

typedef struct VkImageCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkImageType imageType;
    VkFormat format;
    VkExtent3D extent;
    uint32_t mipLevels;
    uint32_t arrayLayers;
    uint32_t samples; /* VkSampleCountFlagBits */
    VkImageTiling tiling;
    VkFlags usage;
    VkSharingMode sharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
    VkImageLayout initialLayout;
} VkImageCreateInfo;

typedef struct VkSamplerCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkFilter magFilter;
    VkFilter minFilter;
    VkSamplerMipmapMode mipmapMode;
    VkSamplerAddressMode addressModeU;
    VkSamplerAddressMode addressModeV;
    VkSamplerAddressMode addressModeW;
    float mipLodBias;
    VkBool32 anisotropyEnable;
    float maxAnisotropy;
    VkBool32 compareEnable;
    uint32_t compareOp; /* VkCompareOp */
    float minLod;
    float maxLod;
    VkBorderColor borderColor;
    VkBool32 unnormalizedCoordinates;
} VkSamplerCreateInfo;

/* VkWriteDescriptorSet.pImageInfo (declared as "const void *" above, since
 * it's a real Vulkan union-by-convention field shared with buffer/texel-
 * buffer descriptor writes) gets cast to this when writing a combined-
 * image-sampler descriptor. */
typedef struct VkDescriptorImageInfo {
    VkSampler sampler;
    VkImageView imageView;
    VkImageLayout imageLayout;
} VkDescriptorImageInfo;

VkResult WINAPI vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo, const void *pAllocator, VkImage *pImage);
void WINAPI vkDestroyImage(VkDevice device, VkImage image, const void *pAllocator);
void WINAPI vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements);
VkResult WINAPI vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset);
VkResult WINAPI vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo, const void *pAllocator, VkSampler *pSampler);
void WINAPI vkDestroySampler(VkDevice device, VkSampler sampler, const void *pAllocator);
void WINAPI vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout,
    uint32_t regionCount, const VkBufferImageCopy *pRegions);
void WINAPI vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers);

/* --- Dynamic viewport/scissor state: lets a pipeline be created once and
 * reused across sqw_vk_recreate_swapchain() resizes (vkCmdSetViewport/
 * vkCmdSetScissor set the real values per-frame instead) rather than
 * needing every pipeline rebuilt on every resize. VkGraphicsPipelineCreateInfo.
 * pDynamicState above is already "const void *" (a real Vulkan union-by-
 * convention field), so no signature change needed there -- just cast. */
#define VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO ((VkStructureType)27)
typedef enum VkDynamicState { VK_DYNAMIC_STATE_VIEWPORT = 0, VK_DYNAMIC_STATE_SCISSOR = 1 } VkDynamicState;
typedef struct VkPipelineDynamicStateCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint32_t dynamicStateCount;
    const VkDynamicState *pDynamicStates;
} VkPipelineDynamicStateCreateInfo;
/* vkCmdSetViewport/vkCmdSetScissor are already declared above (used
 * elsewhere in this header already, near the other vkCmd* prototypes). */

#endif /* _VULKAN_CORE_H */

#ifndef SQW_VK_CONTEXT_H
#define SQW_VK_CONTEXT_H
/* Thin Vulkan device/swapchain/framebuffer wrapper for SQW, extracted from
 * the proven-working setup sequence in
 * SDL3_Build/scratch/triangle_vulkan.c (instance -> physical device ->
 * surface -> device/queue -> swapchain -> render pass -> framebuffers ->
 * command pool/buffers -> sync objects), but taking an already-created
 * window (Win32 HWND/HINSTANCE on Windows, an Xlib Display and Window on
 * Linux -- handed to us by SDL3 via SQW_GetWindowHWND / SQW_GetWindowX11*,
 * see SDL3_Build/sdl_core.inc) instead of creating its own window. Kept
 * independent of any particular rendering pipeline (colored quads, textured
 * quads, ...) so later phases can build pipelines on top without touching
 * this file. Platform split follows the same #ifdef __linux__ convention as
 * SDL3_Build/scratch/platform_shim.h. */
#ifdef __linux__
#include <vulkan_xlib.h>
#else
#include <windows.h>
#include <vulkan_win32.h>
#endif

#define SQW_MAX_SWAP_IMAGES 8

typedef struct {
    VkInstance       instance;
    VkPhysicalDevice phys;
    VkSurfaceKHR     surface;
    VkDevice         device;
    VkQueue          queue;
    uint32_t         queueFamily;

    VkSwapchainKHR   swapchain;
    VkFormat         swapFormat;
    VkExtent2D       extent;
    uint32_t         swapImageCount;
    VkImage          swapImages[SQW_MAX_SWAP_IMAGES];
    VkImageView      swapViews[SQW_MAX_SWAP_IMAGES];
    VkFramebuffer    framebuffers[SQW_MAX_SWAP_IMAGES];

    VkRenderPass     renderPass;
    VkCommandPool    commandPool;
    VkCommandBuffer  cmdBufs[SQW_MAX_SWAP_IMAGES];

    VkSemaphore      imageAvailable;
    VkSemaphore      renderFinished;
    VkFence          inFlightFence;

    VkPhysicalDeviceMemoryProperties memProps;
} SqwVkContext;

/* A single combined-image-sampler texture (the glyph atlas today; general
 * enough to reuse for a future <img> decode-and-upload path). */
typedef struct {
    VkImage        image;
    VkDeviceMemory memory;
    VkImageView    view;
    VkSampler      sampler;
    uint32_t       width, height;
} SqwTexture;

/* Returns 1 on success, 0 on failure (matching triangle_vulkan.c's own
 * VkResult-checked-inline style, collapsed to a single bool here since
 * every step failing is equally fatal at this stage). */
/* NOTE: kept to <=4 params deliberately -- squash's cross-object call
 * codegen (this function lives in a separate .sqo, see Makefile.SQW) has a
 * known weak spot with stack-passed args (5th+ param on Win64) on
 * cross-object calls; width/height are packed into one uint32 to stay
 * under that limit instead of risking it. */
#ifdef __linux__
int sqw_vk_context_init(SqwVkContext *vk, Display *dpy, Window win, uint32_t width_height_packed);
#else
int sqw_vk_context_init(SqwVkContext *vk, HINSTANCE hinstance, HWND hwnd, uint32_t width_height_packed);
#endif

/* Begins a render pass on the next swapchain image, clearing it to
 * (r,g,b,a); returns the recording VkCommandBuffer and image index so the
 * caller can record further draw commands before sqw_vk_end_frame(). */
VkCommandBuffer sqw_vk_begin_frame(SqwVkContext *vk, float r, float g, float b, float a, uint32_t *outImageIndex);
void sqw_vk_end_frame(SqwVkContext *vk, VkCommandBuffer cmd, uint32_t imageIndex);

uint32_t sqw_vk_find_memory_type(SqwVkContext *vk, uint32_t typeBits, VkFlags props);

/* Creates a single-channel (R8_UNORM) sampled texture from `pixels`
 * (width*height bytes), uploaded via a temporary host-visible staging
 * buffer + one-shot command buffer (allocated from vk->commandPool,
 * submitted and waited on synchronously -- this only ever runs a handful
 * of times at startup, so a dedicated transfer-queue/async path isn't
 * warranted). Returns 1 on success, 0 on failure. */
int sqw_vk_create_texture_r8(SqwVkContext *vk, const unsigned char *pixels, uint32_t w, uint32_t h, SqwTexture *out);
void sqw_vk_destroy_texture(SqwVkContext *vk, SqwTexture *tex);

/* Rebuilds the swapchain, image views, and framebuffers in place (same
 * render pass, same command pool/buffers -- those don't depend on
 * swapchain extent) for a new window size. Callers using a pipeline with
 * VK_DYNAMIC_STATE_VIEWPORT/SCISSOR (both new pipelines added alongside
 * this do) never need to touch their VkPipeline on resize. Returns 1 on
 * success, 0 on failure (e.g. a 0x0 minimized-window extent, in which case
 * the caller should just skip rendering that frame rather than treat it as
 * fatal). */
int sqw_vk_recreate_swapchain(SqwVkContext *vk, uint32_t width_height_packed);

#endif /* SQW_VK_CONTEXT_H */

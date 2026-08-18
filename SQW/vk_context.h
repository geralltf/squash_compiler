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

#endif /* SQW_VK_CONTEXT_H */

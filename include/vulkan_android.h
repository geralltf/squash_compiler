#ifndef _VULKAN_ANDROID_H
#define _VULKAN_ANDROID_H
/* VK_KHR_android_surface: the platform-specific extension needed to create
 * a VkSurfaceKHR from a real ANativeWindow* (obtained via the
 * ANativeActivityCallbacks::onNativeWindowCreated callback -- see
 * android_native_glue.h). Mirrors include/vulkan_xlib.h's structure;
 * VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR's value (1000008000)
 * is confirmed against the real, vendored Khronos header at
 * SDL3/SDL/src/video/khronos/vulkan/vulkan_core.h. */
#include "include/vulkan_core.h"

/* Not present in this repo's trimmed include/vulkan_core.h (a curated
 * subset of the real Khronos header) -- defined here instead of adding it
 * to the shared header, to avoid touching a file other existing Vulkan
 * demos (triangle_vulkan.c, particles_physics.c) already depend on. Value
 * confirmed against the real, vendored Khronos header at
 * SDL3/SDL/src/video/khronos/vulkan/vulkan_core.h:487. VkStructureType is
 * a plain int-backed enum, so a #define of the same value is assignment-
 * compatible with it, same as real-world extension code commonly does. */
#define VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR 1000008000

typedef struct VkAndroidSurfaceCreateInfoKHR {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    void *window; /* struct ANativeWindow* */
} VkAndroidSurfaceCreateInfoKHR;

VkResult vkCreateAndroidSurfaceKHR(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR *pCreateInfo, const void *pAllocator, VkSurfaceKHR *pSurface);

#endif /* _VULKAN_ANDROID_H */

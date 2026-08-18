#ifndef _VULKAN_WIN32_H
#define _VULKAN_WIN32_H
/* VK_KHR_win32_surface: the one platform-specific extension this demo
 * needs, to create a VkSurfaceKHR from an existing HWND. */
#include "include/vulkan_core.h"

typedef struct VkWin32SurfaceCreateInfoKHR {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    HINSTANCE hinstance;
    HWND hwnd;
} VkWin32SurfaceCreateInfoKHR;

VkResult WINAPI vkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR *pCreateInfo, const void *pAllocator, VkSurfaceKHR *pSurface);

#endif /* _VULKAN_WIN32_H */

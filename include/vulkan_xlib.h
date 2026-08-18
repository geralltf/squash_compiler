#ifndef _VULKAN_XLIB_H
#define _VULKAN_XLIB_H
/* VK_KHR_xlib_surface: the one platform-specific extension the Linux demos
 * need, to create a VkSurfaceKHR from an existing Xlib Display+Window --
 * the Linux/X11 sibling of vulkan_win32.h's VK_KHR_win32_surface. Reuses
 * the same __XLIB_H-guarded Display/Window forward declarations already
 * established by include/GL/glx.h, so both headers can be included in the
 * same translation unit (e.g. a demo that also links against libGL) without
 * redefinition errors. */
#include "include/vulkan_core.h"

#ifndef __XLIB_H
typedef void*          Display;
typedef unsigned long  Window;
#endif

typedef struct VkXlibSurfaceCreateInfoKHR {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    Display *dpy;
    Window window;
} VkXlibSurfaceCreateInfoKHR;

VkResult vkCreateXlibSurfaceKHR(VkInstance instance, const VkXlibSurfaceCreateInfoKHR *pCreateInfo, const void *pAllocator, VkSurfaceKHR *pSurface);

#endif /* _VULKAN_XLIB_H */

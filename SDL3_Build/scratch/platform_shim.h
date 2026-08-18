#ifndef PLATFORM_SHIM_H
#define PLATFORM_SHIM_H
/* Thin windowing/timing shim shared by the squash Vulkan scratch demos
 * (triangle_vulkan.c, particles_physics.c) so the ~500-line Vulkan device/
 * swapchain/pipeline/render-loop body in each stays byte-identical between
 * Windows and Linux -- only the handful of call sites that create/pump a
 * window, create a VkSurfaceKHR, read the clock, or exit need to differ,
 * and they do so through this one header instead of scattered #ifdefs.
 *
 * Linux branch hand-declares just the Xlib/libX11 functions and structs
 * actually used here (matching this project's existing pattern in
 * include/GL/glx.h of hand-declaring minimal ABI-matching prototypes
 * instead of pulling in the real, much larger system X11/Xlib.h -- squash's
 * own C frontend is not a full libclang and has known limits on real-world
 * header trees). Struct layouts below are written to match glibc/libX11's
 * real ABI exactly, since they're passed BY VALUE/BY POINTER to the real
 * system libX11.so at runtime. */

/* Format a double for use as a %s argument to fprintf(), instead of
 * passing it directly as a %f/%.Nf vararg. On Linux, squash's simplified
 * va_list model (a flat "char*" walking one contiguous integer-register
 * region -- see stdarg.h) has no separate floating-point register-save
 * area the real SysV ABI's va_list struct has, so a double FORWARDED
 * through this project's fprintf shim (include/stdio.h's
 * __squash_fprintf_impl, which itself receives its args via va_arg) reads
 * back garbage (confirmed directly: forwarding a real elapsed-time double
 * through fprintf's "%.2f" produced a nonsense value). Formatting it here
 * via a DIRECT sprintf() call instead -- a real inline double argument at
 * the call site, not one forwarded through another function's va_list --
 * uses the same call-site argument marshalling that already works
 * correctly (verified) for any direct, non-forwarded variadic call. */
int sprintf(char *buf, const char *fmt, ...);
/* A rotating pool of buffers (not one static buffer each): a single
 * fprintf() call site routinely formats more than one double in the same
 * argument list (e.g. "%s,%s", platform_fmt2f(x), platform_fmt2f(y)) --
 * C leaves argument evaluation order unspecified, but both calls complete
 * before fprintf() reads either result, so a single shared buffer would
 * have the second call's output clobber the first's before either is
 * printed. 8 slots is enough for any one real call site in these demos. */
static char g_platform_fmt_bufs[8][32];
static int  g_platform_fmt_next = 0;
static const char *platform_fmt2f(double v) {
    char *buf = g_platform_fmt_bufs[g_platform_fmt_next];
    g_platform_fmt_next = (g_platform_fmt_next + 1) % 8;
    sprintf(buf, "%.2f", v);
    return buf;
}
static const char *platform_fmt3f(double v) {
    char *buf = g_platform_fmt_bufs[g_platform_fmt_next];
    g_platform_fmt_next = (g_platform_fmt_next + 1) % 8;
    sprintf(buf, "%.3f", v);
    return buf;
}

#ifdef __linux__
#include "include/vulkan_xlib.h"

typedef unsigned long XAtom; /* avoid colliding with any other "Atom" */

typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    Window window;
    XAtom message_type;
    int format;
    union { char b[20]; short s[10]; long l[5]; } data;
} XClientMessageEvent;

typedef union {
    int type;
    XClientMessageEvent xclient;
    long pad[24]; /* matches real Xlib.h's XEvent size-guaranteeing member */
} XEvent;

Display *XOpenDisplay(const char *display_name);
int XCloseDisplay(Display *dpy);
int XDefaultScreen(Display *dpy);
Window XRootWindow(Display *dpy, int screen_number);
unsigned long XBlackPixel(Display *dpy, int screen_number);
unsigned long XWhitePixel(Display *dpy, int screen_number);
Window XCreateSimpleWindow(Display *dpy, Window parent, int x, int y,
    unsigned int width, unsigned int height, unsigned int border_width,
    unsigned long border, unsigned long background);
int XMapWindow(Display *dpy, Window w);
int XStoreName(Display *dpy, Window w, const char *window_name);
int XSelectInput(Display *dpy, Window w, long event_mask);
int XNextEvent(Display *dpy, XEvent *event_return);
int XPending(Display *dpy);
int XDestroyWindow(Display *dpy, Window w);
int XFlush(Display *dpy);
XAtom XInternAtom(Display *dpy, const char *atom_name, int only_if_exists);
int XSetWMProtocols(Display *dpy, Window w, XAtom *protocols, int count);

struct sq_timespec { long tv_sec; long tv_nsec; };
int clock_gettime(int clk_id, struct sq_timespec *tp);
#define SQ_CLOCK_MONOTONIC 1

typedef struct {
    Display *dpy;
    Window   win;
    XAtom    wm_delete;
} PlatformWindow;

static int platform_create_window(PlatformWindow *pw, const char *title, int w, int h) {
    pw->dpy = XOpenDisplay(NULL);
    if (!pw->dpy) return 0;
    int screen = XDefaultScreen(pw->dpy);
    pw->win = XCreateSimpleWindow(pw->dpy, XRootWindow(pw->dpy, screen),
        100, 100, (unsigned int)w, (unsigned int)h, 1,
        XBlackPixel(pw->dpy, screen), XWhitePixel(pw->dpy, screen));
    XStoreName(pw->dpy, pw->win, title);
    pw->wm_delete = XInternAtom(pw->dpy, "WM_DELETE_WINDOW", 0);
    XSetWMProtocols(pw->dpy, pw->win, &pw->wm_delete, 1);
    XSelectInput(pw->dpy, pw->win, 0);
    XMapWindow(pw->dpy, pw->win);
    XFlush(pw->dpy);
    return 1;
}

/* Non-blocking pump, matching the Windows side's PeekMessageA loop --
 * returns 0 when the window's close button (WM_DELETE_WINDOW) was clicked. */
static int platform_pump_events(PlatformWindow *pw) {
    while (XPending(pw->dpy) > 0) {
        XEvent ev;
        XNextEvent(pw->dpy, &ev);
        if (ev.type == 33 /* ClientMessage */ && (unsigned long)ev.xclient.data.l[0] == pw->wm_delete)
            return 0;
    }
    return 1;
}

static void platform_destroy_window(PlatformWindow *pw) {
    XDestroyWindow(pw->dpy, pw->win);
    XCloseDisplay(pw->dpy);
}

static const char *platform_surface_extension(void) { return "VK_KHR_xlib_surface"; }

static VkResult platform_create_vk_surface(VkInstance inst, PlatformWindow *pw, VkSurfaceKHR *out) {
    VkXlibSurfaceCreateInfoKHR sci;
    memset(&sci, 0, sizeof sci);
    sci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    sci.dpy = pw->dpy;
    sci.window = pw->win;
    return vkCreateXlibSurfaceKHR(inst, &sci, NULL, out);
}

static double platform_now_seconds(void) {
    struct sq_timespec ts;
    clock_gettime(SQ_CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

/* _exit() (not exit()): skips atexit handlers and any shared-library
 * destructors (e.g. the Vulkan ICD's own), mirroring the Windows side's
 * TerminateProcess rationale exactly -- see its own comment. */
void _exit(int code);
static void platform_exit(int code) { _exit(code); }

#else /* Windows */
#include <windows.h>
#include <vulkan_win32.h>

typedef struct {
    HINSTANCE hinstance;
    HWND      hwnd;
} PlatformWindow;

static int g_platform_running = 1;

static LRESULT CALLBACK platform_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        g_platform_running = 0;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static int platform_create_window(PlatformWindow *pw, const char *title, int w, int h) {
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = platform_wnd_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "SquashVulkanDemoClass";
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)32512);
    RegisterClassExA(&wc);

    RECT wr = {0, 0, w, h};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    pw->hinstance = wc.hInstance;
    pw->hwnd = CreateWindowExA(0, "SquashVulkanDemoClass", title,
        WS_OVERLAPPEDWINDOW, 100, 100, wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, wc.hInstance, NULL);
    if (!pw->hwnd) return 0;
    ShowWindow(pw->hwnd, 1);
    return 1;
}

static int platform_pump_events(PlatformWindow *pw) {
    (void)pw;
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, 1)) {
        if (msg.message == WM_QUIT) { g_platform_running = 0; break; }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return g_platform_running;
}

static void platform_destroy_window(PlatformWindow *pw) {
    DestroyWindow(pw->hwnd);
}

static const char *platform_surface_extension(void) { return "VK_KHR_win32_surface"; }

static VkResult platform_create_vk_surface(VkInstance inst, PlatformWindow *pw, VkSurfaceKHR *out) {
    VkWin32SurfaceCreateInfoKHR sci;
    memset(&sci, 0, sizeof sci);
    sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = pw->hinstance;
    sci.hwnd = pw->hwnd;
    return vkCreateWin32SurfaceKHR(inst, &sci, NULL, out);
}

static double platform_now_seconds(void) {
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}

/* See triangle_vulkan.c's original comment (preserved at this call site):
 * a plain return/ExitProcess() never terminates the process on this
 * machine because the Intel Vulkan ICD's DLL_PROCESS_DETACH handler hangs;
 * TerminateProcess skips that notification entirely. Linux has no DLL-
 * unload equivalent, so its platform_exit (above) is a plain exit(). */
static void platform_exit(int code) { TerminateProcess(GetCurrentProcess(), code); }

#endif
#endif /* PLATFORM_SHIM_H */

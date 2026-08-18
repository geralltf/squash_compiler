/* SQW: minimal HTML5 browser skeleton. Opens an SDL3 window, bridges to a
 * real Vulkan swapchain (see vk_context.c and SQW_GetWindowHWND /
 * SQW_GetWindowX11Display+SQW_GetWindowX11Window in SDL3_Build/sdl_core.inc),
 * parses a small embedded HTML document into a DOM (dom.c), computes a
 * trivial block layout (layout.c), and renders the resulting boxes as
 * flat-colored quads (renderer_vk.c) every frame. Links against the cached
 * SDL3_Build/sdl_common.sqo instead of recompiling SDL3 from source (see
 * Makefile.SQW / Makefile.SQW.linux). SDL3 itself is never modified by this
 * project. Platform split (Win32 HWND vs Xlib Display/Window) follows the
 * same #ifdef __linux__ convention as SDL3_Build/scratch/platform_shim.h. */
#ifdef __linux__
#include <stdlib.h>
/* Direct single-TU include of the shared SDL3 subsystem body, NOT a link
 * against the cached SDL3_Build/sdl_common.sqo (the path Makefile.SQW uses
 * on Windows and every other file in this project defaults to) -- this
 * build hits a real, separate squash objfile_merge cross-object symbol
 * resolution bug where a symbol the .sqo's own export table genuinely
 * contains (SDL_fabsf, pulled in transitively via SDL3/SDL_rect.h's inline
 * SDL_RectsEqualEpsilon) still resolves as an unresolved dynamic import at
 * final link. sdl_core.inc already handles its own SDL3/SDL.h /
 * SDL3/SDL_main.h includes and "#undef main" (see its own comment there),
 * so neither is needed here. See SDL3_Build/clear_linux_unity.c's
 * identical comment for the first reproduction of this exact bug and
 * Makefile.SDL3.linux.clear for the established single-TU workaround this
 * follows -- left as a standing follow-up for whoever fixes the underlying
 * objfile_merge bug so the shared .sqo cache path can be used here too. */
#include "sdl_core.inc"
#else
#include <windows.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#undef main
#endif

/* #include, not separate `squash -c`/.sqo compilation+link: every one of
 * these crashed when compiled to its own .sqo and linked in (heap
 * corruption / access violation, reproduced with minimal repros that
 * differ ONLY in going through the .sqo object-merge path vs. a direct
 * single-TU compile) -- a real, currently-unresolved squash compiler bug
 * in cross-object linking for larger functions, not anything specific to
 * this code. #include sidesteps it entirely; see each file's own Makefile
 * comment. dom_walk.c pulls in dom.c and html_lexer.c the same way. */
#include "vk_context.c"
#include "dom_walk.c"
#include "layout.c"
#include "renderer_vk.c"

#ifdef __linux__
/* squash_init_private_bootstrap()/SQW_GetWindowX11Display()/
 * SQW_GetWindowX11Window() are real, already-defined functions in this
 * same translation unit by this point (sdl_core.inc was #included above,
 * not linked in as a separate .sqo -- see that #include's own comment), so
 * no forward declaration is needed for them here, unlike the Windows
 * .sqo-linked branch below. Deliberately NOT declaring them "extern": per
 * unistd.h's own comment, "extern" on a bodyless prototype routes squash's
 * codegen through its cross-object SYM_IMPORT path, which is wrong for a
 * symbol that already has a real local definition earlier in this TU.
 * _exit() is the one genuine exception -- a real libc.so.6 export with no
 * body anywhere squash compiles, so it does need "extern" for the same
 * reason unistd.h's own syscall wrappers do. */
extern void _exit(int code);
#else
/* Defined in SDL3_Build/sdl_core.inc (baked into sdl_common.sqo); see
 * example_shim.inc's identical forward declaration for why this is needed
 * as a cross-object call. */
void squash_init_private_bootstrap(void);
extern void *SQW_GetWindowHWND(SDL_Window *window);
#endif

#define SQW_VIEWPORT_W 1024.0f
#define SQW_VIEWPORT_H 768.0f

static const char *SQW_TEST_HTML =
    "<html><body>"
    "<div class=\"outer\">"
      "<center><p>Hello <span>world</span></p></center>"
      "<a href=\"http://example.com\">link</a>"
      "<img src=\"pic.png\">"
    "</div>"
    "</body></html>";

int main(void) {
    squash_init_private_bootstrap();

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SQW: SDL_Init failed: %s\n", SDL_GetError()); fflush(stdout);
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("SQW", (int)SQW_VIEWPORT_W, (int)SQW_VIEWPORT_H, 0);
    if (!window) {
        fprintf(stderr, "SQW: SDL_CreateWindow failed: %s\n", SDL_GetError()); fflush(stdout);
        return 1;
    }
    SDL_ShowWindow(window);

#ifdef __linux__
    Display *dpy = (Display *)SQW_GetWindowX11Display(window);
    Window win = (Window)SQW_GetWindowX11Window(window);
    if (!dpy || !win) {
        fprintf(stderr, "SQW: SQW_GetWindowX11Display/Window returned NULL\n"); fflush(stdout);
        return 1;
    }
#else
    HWND hwnd = (HWND)SQW_GetWindowHWND(window);
    if (!hwnd) {
        fprintf(stderr, "SQW: SQW_GetWindowHWND returned NULL\n"); fflush(stdout);
        return 1;
    }
    HINSTANCE hinstance = GetModuleHandleA(NULL);
#endif

    /* Heap-allocated, not a stack local: keeps these ~KB-sized structs off
     * the stack (see project memory on squash's past issues with large
     * stack-resident locals). */
    SqwVkContext *vk = (SqwVkContext *)malloc(sizeof(SqwVkContext));
#ifdef __linux__
    if (!sqw_vk_context_init(vk, dpy, win, ((uint32_t)SQW_VIEWPORT_W << 16) | (uint32_t)SQW_VIEWPORT_H)) {
#else
    if (!sqw_vk_context_init(vk, hinstance, hwnd, ((uint32_t)SQW_VIEWPORT_W << 16) | (uint32_t)SQW_VIEWPORT_H)) {
#endif
        fprintf(stderr, "SQW: Vulkan init failed\n"); fflush(stdout);
        return 1;
    }

    SqwRenderer *renderer = (SqwRenderer *)malloc(sizeof(SqwRenderer));
    if (!sqw_renderer_init(vk, renderer)) {
        fprintf(stderr, "SQW: renderer init failed\n"); fflush(stdout);
        return 1;
    }

    DomNode *root = dom_parse(SQW_TEST_HTML);
    LayoutList boxes;
    layout_compute(root, SQW_VIEWPORT_W, SQW_VIEWPORT_H, &boxes);
    fprintf(stderr, "SQW: DOM parsed, layout computed (%d boxes)\n", boxes.count); fflush(stdout);

    fprintf(stderr, "SQW: entering render loop\n"); fflush(stdout);
    int running = 1;
    int frame_count = 0;
    const int max_frames = 300; /* bounded, matching the proven triangle_vulkan.c pattern */
    while (running && frame_count < max_frames) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = 0;
            }
        }
        if (!running) break;

        uint32_t imageIndex = 0;
        VkCommandBuffer cmd = sqw_vk_begin_frame(vk, 0.95f, 0.95f, 0.95f, 1.0f, &imageIndex);
        if (!cmd) break;
        sqw_renderer_draw(vk, renderer, cmd, &boxes, SQW_VIEWPORT_W, SQW_VIEWPORT_H);
        sqw_vk_end_frame(vk, cmd, imageIndex);

        frame_count++;
        if (frame_count % 60 == 0) { fprintf(stderr, "SQW: frame=%d\n", frame_count); fflush(stdout); }
    }

    fprintf(stderr, "SQW: render loop finished, frame_count=%d\n", frame_count); fflush(stdout);

    /* Skip Vulkan/SDL teardown and exit directly (_exit on Linux,
     * TerminateProcess on Windows) -- see triangle_vulkan.c's own comment
     * / platform_shim.h's platform_exit() for why: on Windows, the Vulkan
     * ICD's DLL unload path hangs on this machine after real device/
     * swapchain work has been done, an environment/driver quirk unrelated
     * to squash or SQW; _exit() on Linux mirrors that by skipping atexit
     * handlers and any shared-library destructors (the Vulkan ICD's own
     * included) for the same reason. All real work above (window, device,
     * swapchain, N rendered frames) already completed successfully by this
     * point. */
#ifdef __linux__
    _exit(0);
#else
    TerminateProcess(GetCurrentProcess(), 0);
#endif
    return 0;
}

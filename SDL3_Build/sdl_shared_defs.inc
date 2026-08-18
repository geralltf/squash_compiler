/* Shared preamble for the per-subsystem split of sdl_core.inc (see
 * sdl_part_core.c / sdl_part_audio.c / sdl_part_video.c) -- these macros
 * must be defined BEFORE each part's first real SDL3 #include, since they
 * gate conditional compilation inside those files (SDL_build_config_private.h
 * and friends). Extracted verbatim from sdl_core.inc's own top-of-file
 * preamble so every part sees identical config, exactly as when this was
 * one single combined translation unit. */
#define SDL_PLATFORM_PRIVATE 1
#define SDL_PLATFORM_PRIVATE_NAME "squash"
#define SDL_DYNAMIC_API 0
/* SDL_malloc.c's default allocator is a full embedded dlmalloc
 * implementation, which auto-detects _WIN32 (which squash always defines)
 * and switches to a VirtualAlloc()-based memory-mapping path needing real
 * windows.h — squash doesn't have that. Defining HAVE_MALLOC makes it use
 * a thin wrapper around the plain malloc/calloc/realloc/free squash's own
 * libc shim already provides instead of building its own allocator. */
#define HAVE_MALLOC 1
/* SDL_stack_alloc() defaults to alloca() — a real compiler intrinsic that
 * adjusts the current stack frame in place, not an ordinary callable
 * function. squash's codegen treats it as a normal external symbol
 * reference instead, which fails at the assembler/linker stage
 * ("asm_resolve: undefined label ... name=alloca"). Forcing the
 * SDL_malloc()-based fallback avoids needing alloca() codegen support. */
#define SDL_DISABLE_ALLOCA 1
/* Custom minimal Win32 video backend (see the PRIVATE_* block below, right
 * before "video/SDL_video.c" is included) plus the portable software
 * renderer — enough real windowing + rasterization for SDL_CreateWindow/
 * SDL_CreateWindowAndRenderer-based examples (not just headless subsystem
 * round trips) to actually put pixels on screen. */
#define SDL_VIDEO_DRIVER_PRIVATE 1
#define SDL_VIDEO_RENDER_SW 1

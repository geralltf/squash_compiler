#ifndef SDL_build_config_private_h_
#define SDL_build_config_private_h_
/* Squash-compiler build: force the dummy/minimal backend set regardless of
 * _WIN32 being defined, since squash's tiny include/ shim has none of the
 * real Windows SDK headers (windows.h, dinput.h, xinput.h, d3d11.h, ...)
 * that SDL_build_config_windows.h's real backends need. This mirrors
 * upstream SDL's own SDL_build_config_minimal.h. */
#include "build_config/SDL_build_config_minimal.h"

/* RESOLVED: SDL_build_config_minimal.h sets SDL_AUDIO_DRIVER_DUMMY
 * (expecting a real dummy-backend .c file), but this build never includes
 * one -- it uses its own real PRIVATEAUDIO_bootstrap (a minimal WinMM
 * waveOut backend, see sdl_core.inc) instead, the same pattern already used
 * for video's PRIVATE_bootstrap. SDL_AUDIO_DISABLED used to be defined here
 * because the audio subsystem was entirely excluded on the assumption that
 * SDL_audiocvt.c/SDL_audioresample.c/SDL_audiotypecvt.c's real x86 SIMD
 * intrinsics were required -- turned out to be wrong: every SIMD block in
 * those three files is already gated behind "#ifdef SDL_SSE_INTRINSICS" (or
 * SSE2/SSE3/NEON), and squash's preprocessor never defines __SSE__ or
 * _MSC_VER, so SDL_SSE_INTRINSICS itself is never defined and the existing
 * portable scalar C fallback in each of those files is what actually gets
 * compiled -- no compiler change needed, just including the files. */
#undef SDL_AUDIO_DRIVER_DUMMY
#define SDL_AUDIO_DRIVER_PRIVATE 1

/* SDL_build_config_minimal.h sets SDL_THREADS_DISABLED, which SDL_thread_c.h
 * checks BEFORE SDL_THREAD_WINDOWS, forcing SYS_ThreadHandle to a plain
 * "int" (generic/SDL_systhread_c.h) — too small to hold a real Win32 HANDLE
 * (a pointer) on 64-bit, and SDL_GetErrBuf()/SDL_CreateMutex()/etc.'s real
 * bodies (guarded by "#ifndef SDL_THREADS_DISABLED") never execute, silently
 * degrading to single-buffer/no-op stand-ins instead. Real thread creation
 * (SDL_CreateThread, needed for the audio worker thread — see
 * PRIVATETHREAD_* below) needs the real HANDLE-typed SYS_ThreadHandle from
 * windows/SDL_systhread_c.h, which only happens when SDL_THREAD_WINDOWS is
 * defined AND SDL_THREADS_DISABLED is not. */
#undef SDL_THREADS_DISABLED
#define SDL_THREAD_WINDOWS 1

/* SDL_malloc.c (dlmalloc port)'s own header comment: "Thread-safety: NOT
 * thread-safe unless USE_LOCKS defined non-zero" — with real threads now
 * enabled above, the allocator's shared internal state needs real locking,
 * or the first genuine concurrent SDL_malloc/SDL_calloc call from a second
 * OS thread corrupts it (confirmed via a crash reading Windows' own
 * "BAADF00D" uninitialized-heap marker inside a freshly-calloc'd struct).
 * "WIN32" (bare, no underscore — an older macro spelling dlmalloc.c checks
 * specifically, distinct from this project's usual SDL_PLATFORM_WINDOWS/
 * _WIN32) selects its CRITICAL_SECTION-based lock implementation instead of
 * the pthread path (taken when WIN32 is undefined) — see
 * include/windows.h's CRITICAL_SECTION/Interlocked* additions, needed for
 * this to actually compile. */
#define WIN32 1
#define USE_LOCKS 1

#endif

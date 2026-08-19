/* Squash-compiler build of a subset of SDL3.
 *
 * squash compiles a single translation unit straight to an executable (no
 * separate object-file/archive step), so this file exists purely to name,
 * in one place, exactly which of SDL3's own .c files make up this build —
 * none of SDL3's own sources are modified.
 *
 * Real windowing (video surface+blit, software render) AND real audio
 * playback (WinMM waveOut) are both included, via this project's own
 * custom PRIVATE_bootstrap / PRIVATEAUDIO_bootstrap backends further down
 * this file — see [[project-squash-sdl3-build]] memory notes for the full
 * history. GPU and hidapi remain out of scope. Everything else uses SDL3's
 * portable "dummy"/"generic" backends.
 *
 * ROOT CAUSE FOUND AND FIXED (codegen.c, sizeof_type_sym()): a real,
 * general squash bug — NOT SDL3-specific — where "typedef enum Foo {...}
 * Foo;" (tag name identical to the typedef alias, an extremely common C
 * idiom used throughout SDL3) made squash's codegen recurse into itself
 * forever computing the type's size (a genuine STATUS_STACK_OVERFLOW),
 * because an enum's TypeInfo::base is the bare tag name with no "enum "
 * prefix (unlike struct/union, which get one and were already immune).
 * This was previously misdiagnosed as several unrelated "scale" issues
 * (a large source file alone crashing, or only when combined with enough
 * other files) — it's actually this one bug, triggered by whichever
 * same-named enum typedef codegen happens to reach first. Confirmed fixed
 * by reproducing each of SDL_hints.c, SDL_log.c, SDL_properties.c,
 * SDL_thread.c, SDL.c, and joystick/controller_type.c standalone before
 * and after the fix.
 *
 * See the commit history for the full list of preprocessor/parser bugs
 * fixed earlier in this same effort (lexer.c/parser_new4.c). */

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
/* SDL_internal.h auto-defines SDL_HAVE_BLIT_0/1/A/N/AUTO (and SDL_HAVE_RLE)
 * to 1 whenever SDL_LEAN_AND_MEAN isn't defined — a real, officially
 * supported SDL3 build option, not a squash-specific workaround. Defining
 * it disables all five accelerated-blit code paths (video/SDL_blit_0.c/
 * SDL_blit_1.c/SDL_blit_A.c/SDL_blit_N.c/SDL_blit_auto.c) plus RLE colorkey
 * acceleration, none of which are included in this build (SDL_blit_1.c hits
 * a real squash parser error and SDL_blit_A.c a real squash codegen hang
 * when tried standalone — see video/SDL_blit.c's own include comment below
 * — and SDL_RLEaccel.c hits a separate parser error, see its own stub
 * comment below), leaving just the plain, always-correct
 * SDL_BlitCopy/SDL_Blit_Slow paths from SDL_blit_copy.c/SDL_blit_slow.c. */
#define SDL_LEAN_AND_MEAN 1

static void dbgmark(const char *s);
static void dbgmark_ptr(const char *label, unsigned long long v);
#include "SDL.c"
/* SDL_assert.c: re-examined now that squash has real inline-asm codegen.
 * Its actual SDL_AbortAssertion() only calls SDL_Quit()+SDL_abort(), never
 * SDL_TriggerBreakpoint() directly, so that's not the blocker it was once
 * assumed to be, and the file's only __GNUC__-gated construct (a
 * debug_print() __attribute__((format(...))) declaration) stays inactive
 * since __GNUC__ is undefined here. HOWEVER: tried including it, and the
 * build silently picked up a reference to SDL_TriggerBreakpoint (probably
 * via the SDL_assert()/SDL_AssertBreakpoint() macro machinery in
 * SDL_internal.h, reachable from something in this file even though
 * SDL_assert.c's own visible code never calls it) with NO real definition
 * anywhere in this TU. squash's linker guessed "msvcrt.dll" for the
 * unresolved import and added a real IAT entry for it — unlike its usual
 * "no IAT entry" warning (deferred to first call, harmless if never
 * reached), a bogus *IAT* entry is resolved by the Windows loader at
 * process-launch time, before main() ever runs — msvcrt.dll doesn't
 * actually export a function called that, so the loader refuses to start
 * the process at all (STATUS_DLL_NOT_FOUND), confirmed by actually
 * running the built exe. Fixed below by giving SDL_TriggerBreakpoint a
 * real no-op definition in this TU (same pattern as the other stubs near
 * main(), and correct here since no assertion actually fires in this
 * demo) instead of leaving it an unresolved import for squash to guess at. */
#include "SDL_assert.c"
#include "SDL_error.c"
#include "SDL_guid.c"
#include "SDL_hashtable.c"
#include "SDL_hints.c"
#include "SDL_list.c"
/* SDL_log.c's own SDL_CheckInitLog() has a real, narrow, reproducible
 * squash codegen bug: its "SDL_log_init.thread == SDL_GetCurrentThreadID()"
 * comparison (guarding against re-entering SDL_InitLog() from the SAME
 * thread while already mid-INITIALIZING) evaluates false even when both
 * sides are confirmed byte-identical via debug fprintf — the exact same
 * "struct-field-vs-function-result Uint64 comparison" shape as the
 * mutex->owner recursion bug fixed above (see SDL_CreateMutex's own
 * comment), just triggered via a different, deeply nested call path:
 * SDL_InitLog() -> SDL_AddHintCallback() (which synchronously invokes the
 * just-registered SDL_LoggingChanged callback) -> SDL_ResetLogPriorities()
 * -> SDL_CheckInitLog(), re-entering SDL_InitLog() -> SDL_ShouldInit(),
 * which then spins forever in "wait for the other thread to finish
 * transitioning" (there is no other thread). Root-caused via a long chain
 * of standalone repros of the same comparison shape that all evaluated
 * correctly in isolation — matches this project's own well-documented
 * "only reproduces at full-build call-chain depth/scale" bug class (see
 * this file's own top-of-file enum-typedef-recursion comment for another
 * instance). A first attempt replaced the "==" with an equivalent
 * subtraction ("(SDL_log_init.thread - this_thread) == 0"), on the theory
 * that "==" itself was the miscompiled shape — confirmed via dbgmark
 * instrumentation that the SUBTRACTION itself is just as broken at this
 * exact call depth (byte-identical operands, e.g. both
 * 0x7ff700xxxxxxx-shaped thread IDs, produced a nonzero garbage
 * difference like 0x00007ff700000000 — looks like the upper 32 bits of one
 * operand surviving a 32-bit-only subtraction instead of a real 64-bit
 * one), so this doesn't depend on thread-ID equality math at all: a plain
 * re-entrancy flag, set only across SDL_InitLog()'s own body on THIS
 * (single, since this build never spawns a real second OS thread that
 * reaches logging) thread, sidesteps the miscompiled comparison entirely
 * regardless of its ultimate root cause.
 *
 * Fixed via a FUNCTION-LIKE macro, not a plain object-like rename: a
 * bare "#define SDL_CheckInitLog SDL_CheckInitLog_fixed" would rename
 * BOTH the call sites (SDL_GetLogPriority/SDL_ResetLogPriorities, which is
 * what we want redirected) AND the "static void SDL_CheckInitLog(void)"
 * definition line itself (which we do NOT want renamed away from under
 * us — that would leave nothing at all named SDL_CheckInitLog for our
 * replacement to occupy). A zero-arg function-like macro
 * "SDL_CheckInitLog()" only matches genuine zero-argument call-site
 * invocations like "SDL_CheckInitLog();" — squash's preprocessor (unlike
 * strict cpp, which hard-errors on the arg-count mismatch) simply leaves
 * the "SDL_CheckInitLog(void)" definition line untouched instead, since it
 * looks like a 1-argument invocation ("void" as the argument) to a
 * declared-0-argument macro. Confirmed with a standalone repro before
 * relying on it here. Net effect: the call sites get redirected to our
 * corrected SDL_CheckInitLog_fixed() below, while the original buggy
 * definition survives under its own unchanged name as harmless dead code
 * (nothing calls it anymore). */
#define SDL_CheckInitLog() SDL_CheckInitLog_fixed()
#include "SDL_log.c"
#undef SDL_CheckInitLog
static bool sq_in_init_log = false;
static void SDL_CheckInitLog_fixed(void)
{
    int status = SDL_GetAtomicInt(&SDL_log_init.status);
    if (status == SDL_INIT_STATUS_INITIALIZED) {
        return;
    }
    if (status == SDL_INIT_STATUS_INITIALIZING && sq_in_init_log) {
        return;
    }
    sq_in_init_log = true;
    SDL_InitLog();
    sq_in_init_log = false;
}
#include "SDL_properties.c"
#include "SDL_utils.c"
/* SDL_IsUbuntuTouch() is called unconditionally from SDL.c, SDL_properties.c,
 * SDL_video.c, and filesystem/unix/SDL_sysfilesystem.c, but neither its
 * generic non-Linux stub (core/SDL_core_unsupported.c, guarded #ifndef
 * SDL_PLATFORM_LINUX) nor its real Linux implementation
 * (core/linux/SDL_ubuntu_touch.c, guarded #ifdef SDL_PLATFORM_LINUX) was
 * ever included in this TU, leaving it declared but never defined: squash's
 * -c/link path doesn't hard-error on an unresolved reference like this the
 * way a normal linker would, so it silently became a runtime-only dynamic
 * symbol lookup that failed the moment SDL_Init() called it. Including both
 * files is safe since exactly one half of each's #ifdef is active for a
 * given platform. */
#include "core/SDL_core_unsupported.c"
#include "core/linux/SDL_ubuntu_touch.c"

#include "stdlib/SDL_crc16.c"
#include "stdlib/SDL_crc32.c"
#include "stdlib/SDL_getenv.c"
#include "stdlib/SDL_iconv.c"
#include "stdlib/SDL_malloc.c"
#include "stdlib/SDL_memcpy.c"
#include "stdlib/SDL_memmove.c"
/* stdlib/SDL_memset.c is NOT included: SDL_memset4() in that file uses
 * Duff's device (case labels nested inside a do-while body, falling
 * through mid-loop) — squash's switch/case only supports case labels as
 * direct children of the switch block, not arbitrary labels nested inside
 * other statements, so this is a hard parse error ("unexpected token
 * 'case'"), confirmed via a standalone repro. Real support would need a
 * structural rework of switch/case codegen (labels anywhere, not a flat
 * case-list), out of scope here.
 *
 * SDL_memset() itself has no such issue (its own portable fallback is an
 * ordinary flat switch) and doesn't actually need reimplementing here at
 * all: SDL_stdinc.h unconditionally "#define SDL_memset memset" (gated
 * behind "#ifndef SDL_SLOW_MEMSET", which only src/dynapi/SDL_dynapi.c
 * would ever set, and that file isn't part of this build), so every
 * SDL_memset(...) call site is already textually just memset(...) by the
 * time squash sees it — squash's own built-in memset intrinsic (see
 * symtable.c's SF("memset")) already handles all of those correctly.
 * SDL_memset4 has no such alias (there's no libc equivalent for a 32-bit-
 * word fill) and genuinely needs a real definition, reimplemented below
 * with its Duff's device rewritten as an equivalent plain loop (same net
 * iteration behavior — Duff's device is purely a manual loop-unrolling
 * optimization, not an observable semantic difference).
 *
 * UPDATE (video/SDL_rotate.c, added later in this file, calls SDL_memset()
 * directly): that call site produced a genuine unresolved "SDL_memset"
 * dynamic symbol at runtime instead of expanding to the aliased memset()
 * intrinsic the paragraph above describes — the "#define SDL_memset
 * memset" alias evidently doesn't reach every later #include in this
 * unity build the way it should. Rather than chase why, a real (trivial)
 * SDL_memset() definition below sidesteps the alias entirely, the same
 * fix already applied to SDL_memset4 for a different reason. */
void *SDL_memset4(void *dst, Uint32 val, size_t dwords)
{
    Uint32 *p = (Uint32 *)dst;
    while (dwords--) {
        *p++ = val;
    }
    return dst;
}
#ifdef SDL_memset
#undef SDL_memset
#endif
void *SDL_memset(void *dst, int c, size_t len)
{
    unsigned char *p = (unsigned char *)dst;
    while (len--) {
        *p++ = (unsigned char)c;
    }
    return dst;
}
#include "stdlib/SDL_murmur3.c"
/* SDL_qsort.c (real, unmodified) does "#undef malloc"/"#define malloc
 * SDL_malloc" and the same for free, with no matching #undef at the end of
 * the file — harmless in a normal build where SDL_qsort.c is compiled as
 * its own separate translation unit, but in THIS unity build every
 * subsequent #include shares the same preprocessor macro namespace, so the
 * redefinition silently leaks forward into every later file. It textually
 * mangled SDL_video.c's real, unmodified "_this->free(_this);" (in
 * SDL_VideoQuit — a call through the SDL_VideoDevice::free function-pointer
 * field) into "_this->SDL_free(_this);", which looks up a field literally
 * named "SDL_free" that doesn't exist on the struct — squash's field
 * lookup then fell through to a wrong/default offset, dereferencing
 * unrelated struct memory as a function pointer and crashing the instant
 * SDL_VideoQuit() ran (i.e. on any clean shutdown, right after the video
 * frame loop). Restoring the plain names immediately after the file that
 * introduced them keeps every other included file's own "malloc"/"free"
 * calls meaning what they say. */
#include "stdlib/SDL_qsort.c"
#undef malloc
#undef free
#include "stdlib/SDL_stdlib.c"
#include "stdlib/SDL_string.c"
#include "stdlib/SDL_strtokr.c"
/* SDL_random.c: self-contained portable PRNG (SDL_rand/SDL_randf/etc.), no
 * dependencies beyond SDL_GetPerformanceCounter (already provided above) —
 * simply never added to this build before. Its absence meant SDL_rand()/
 * SDL_randf() had no body anywhere in the unity build, so any example
 * calling them (e.g. examples/renderer/04-points) hit squash's "unresolved
 * call" fallback and crashed with the same "MZ header" garbage-jump
 * signature documented elsewhere in this file for other missing-function
 * gaps (SDL_UpdateAudio, SDL_SYS_DelayNS). */
#include "stdlib/SDL_random.c"

#if defined(__APPLE__)
/* SDL_build_config_macos.h (real upstream SDL3, untouched -- see this
 * file's own header comment) unconditionally sets HAVE_GCC_ATOMICS 1,
 * which routes SDL_atomic.c/SDL_spinlock.c through __sync_bool_compare_and_swap
 * / __sync_lock_test_and_set -- real clang/gcc COMPILER BUILTINS, not
 * functions with any callable body, that squash's frontend doesn't
 * implement. squash silently compiled these as ordinary (never resolving)
 * external calls instead of erroring, so SDL_CompareAndSwapAtomicInt()
 * always returned false -- this went undetected as a "crash" because
 * nothing dereferences a bad pointer; SDL_ShouldInit() (SDL_thread.c),
 * used by EVERY SDL_InitSubSystem() call including a plain SDL_Init(0),
 * treats a false CAS as "some other thread must be mid-transition" and
 * retries via SDL_Delay(1) forever, since no other thread was ever really
 * racing it -- confirmed via dbgmark instrumentation: SDL_Init(0) never
 * returned, looping SDL_SYS_DelayNS(1000000) (1ms) hundreds of times a
 * second indefinitely. Undefining it here (AFTER SDL_build_config_macos.h
 * has already run once via whichever earlier #include first pulled in
 * SDL_internal.h, BEFORE atomic/SDL_atomic.c's own #include below) falls
 * through to the SDL_PLATFORM_MACOS branch in both files instead, which
 * calls the real OSAtomicCompareAndSwap32Barrier libSystem export (see
 * include/OSAtomic.h) -- a real, callable function. */
#undef HAVE_GCC_ATOMICS
#endif
#if defined(HAVE_GCC_ATOMICS)
static int GCC_ATOMICS_STILL_DEFINED_DEBUG_CHECK[-1];
#endif
#include "atomic/SDL_atomic.c"
/* squash now has real inline-asm codegen support (x86/x64/arm64), verified
 * standalone against real GCC output byte-for-byte (see scratchpad
 * test_asm_spinlock.c). SDL_spinlock.c's own body only checks __GNUC__ in
 * the ARM/x86 __asm__ branches below (HAVE_GCC_ATOMICS, which would
 * otherwise route to the __sync_lock_test_and_set/__sync_lock_release
 * GCC BUILTINS squash can't codegen, is never defined anywhere in this
 * build — there's no CMake detection step here — so that branch can never
 * be selected regardless of __GNUC__). The defines are scoped tightly
 * around just this one #include: SDL_internal.h and every other header
 * SDL_spinlock.c pulls in were already fully processed once, earlier in
 * this unity TU, under include guards, with __GNUC__ undefined — so this
 * can't reopen any other file's __GNUC__-gated behavior. __x86_64__ is
 * defined here (not globally in squash's preprocessor) because this unity
 * build is always invoked as "-windows -64" per the Makefile; squash
 * itself still predefines no architecture macros. */
#define __GNUC__ 1
#define __x86_64__ 1
#include "atomic/SDL_spinlock.c"
#undef __x86_64__
#undef __GNUC__

/* SDL_cpuinfo.c: re-examined now that squash has real inline-asm codegen.
 * Its setjmp/longjmp + SIGILL-handler AltiVec-detection fallback (squash
 * has no <setjmp.h>/<signal.h>) is entirely gated behind
 * "defined(SDL_ALTIVEC_BLITTERS) && defined(HAVE_SETJMP)" — a CMake-driven
 * build option pair never defined anywhere in this build (no CMake step
 * here), so that whole code path is dead regardless of platform; it's not
 * actually a live blocker. Everything else platform-specific (getauxval,
 * sysconf, sysctlbyname, RISC OS/3DS/PS2/Vita/Haiku system calls) is
 * likewise gated behind HAVE_-prefixed or SDL_PLATFORM_-prefixed macros
 * this build never defines. The real x86 CPUID detection needs the same
 * scoped __GNUC__+__x86_64__ treatment as SDL_spinlock.c above (verified
 * byte-for-byte against real GCC in scratchpad test_asm_cpuid.c: exact
 * has_CPUID/cpuid()/vendor-string match on real AMD hardware). */
#define __GNUC__ 1
#define __x86_64__ 1
#include "cpuinfo/SDL_cpuinfo.c"
#undef __x86_64__
#undef __GNUC__

#include "libm/e_atan2.c"
#include "libm/e_exp.c"
#include "libm/e_fmod.c"
#include "libm/e_log.c"
#include "libm/e_log10.c"
#include "libm/e_pow.c"
#include "libm/e_rem_pio2.c"
#include "libm/e_sqrt.c"
#include "libm/k_cos.c"
#include "libm/k_rem_pio2.c"
#include "libm/k_sin.c"
#include "libm/k_tan.c"
#include "libm/s_atan.c"
#include "libm/s_copysign.c"
#include "libm/s_cos.c"
#include "libm/s_fabs.c"
#include "libm/s_floor.c"
#include "libm/s_isinf.c"
#include "libm/s_isinff.c"
#include "libm/s_isnan.c"
#include "libm/s_isnanf.c"
#include "libm/s_modf.c"
#include "libm/s_scalbn.c"
#include "libm/s_sin.c"
#include "libm/s_tan.c"

/* thread/SDL_thread.c's own generic TLS fallback (SDL_Generic_GetTLSData/
 * SetTLSData, used since this build has no real per-platform TLS backend)
 * matches entries by comparing SDL_ThreadID (a Uint64) values -- confirmed,
 * via extensive dbgmark instrumentation, broken at multiple different
 * layers simultaneously in this exact call path: entry->thread==thread
 * comparisons evaluating false on byte-identical values (the same
 * struct-field-vs-local Uint64 comparison bug already documented and
 * worked around elsewhere in this file, e.g. SDL_CheckInitLog_fixed and
 * SDL_CreateMutex's own comments), a LOCAL SDL_ThreadID variable
 * capturing SDL_GetCurrentThreadID()'s 64-bit return value truncated to
 * 32 bits in one call site but not another structurally-identical one,
 * and even plain pthread_self() itself (a REAL external pthread call, no
 * squash-compiled logic involved at all) returning a value exactly 2^32
 * off between two calls on the same never-multithreaded process -- each
 * confirmed in isolation via dbgmark before concluding none of them were
 * individually fixable with a targeted rewrite. Rather than keep chasing
 * this deep into squash's call-return-value handling, SDL_GetErrBuf
 * itself (thread/SDL_thread.c, the ONLY real caller of this generic TLS
 * path that actually matters to any of this build's examples -- nothing
 * else routes error state through a per-thread lookup) is overridden to
 * always return the plain non-thread-specific static buffer
 * (SDL_GetStaticErrBuf(), thread/SDL_thread.c's own SDL_THREADS_DISABLED
 * fallback), bypassing the generic TLS thread-matching path entirely.
 * This trades genuine per-thread error isolation (irrelevant here: no
 * example in this build calls SDL_GetError concurrently from two
 * different threads) for actually working, which the thread-keyed
 * version never reliably did in the first place. */
extern SDL_error *SDL_GetStaticErrBuf(void);
#define SDL_GetErrBuf sq_real_GetErrBuf
#include "thread/SDL_thread.c"
#undef SDL_GetErrBuf
SDL_error *SDL_GetErrBuf(bool create)
{
    (void)create;
    return SDL_GetStaticErrBuf();
}

#include "thread/generic/SDL_syscond.c"
#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
/* thread/generic/SDL_sysmutex.c (mutex-via-semaphore, with its own manual
 * "mutex->owner == this_thread" recursion tracking) is NOT included on
 * Linux: a same-thread recursive SDL_LockMutex() call (reached during
 * SDL_Init's TLS bootstrap) hung forever, root-caused via a hand-rolled
 * repro to squash evaluating "mutex->owner == this_thread" as false on the
 * second (recursive) call despite both being confirmed byte-identical
 * Uint64 values one line earlier (dumped via debug fprintf) — a real
 * squash codegen bug in that specific comparison shape, not yet isolated
 * to a minimal standalone repro (every simplified version of the same
 * comparison tried in isolation evaluated correctly; this looks like
 * another instance of the "only reproduces at full-build scale" class of
 * bug already documented elsewhere in this file, e.g. the enum-typedef
 * infinite-recursion bug in this file's own top-of-file comment).
 * Sidestepped entirely here by using a REAL recursive pthread_mutex_t
 * directly (PTHREAD_MUTEX_RECURSIVE) instead of re-deriving recursion
 * tracking by hand — pthread's own libc-internal recursion counter isn't
 * exposed to the buggy comparison at all. SDL_Mutex is an opaque type
 * (this struct is private to whichever backend file defines it), so
 * swapping its internal representation is safe: SDL_sysrwlock.c below
 * only calls the public SDL_CreateMutex/SDL_LockMutex/SDL_UnlockMutex/
 * SDL_DestroyMutex API, never touches these fields directly. */
struct SDL_Mutex {
    pthread_mutex_t m;
};

SDL_Mutex *SDL_CreateMutex(void)
{
    SDL_Mutex *mutex = (SDL_Mutex *)SDL_malloc(sizeof(*mutex));
    if (!mutex) {
        return ((void*)0);
    }
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mutex->m, &attr);
    pthread_mutexattr_destroy(&attr);
    return mutex;
}

void SDL_DestroyMutex(SDL_Mutex *mutex)
{
    if (mutex) {
        pthread_mutex_destroy(&mutex->m);
        SDL_free(mutex);
    }
}

void SDL_LockMutex(SDL_Mutex *mutex)
{
    if (mutex) {
        pthread_mutex_lock(&mutex->m);
    }
}

bool SDL_TryLockMutex(SDL_Mutex *mutex)
{
    if (!mutex) {
        return true;
    }
    return pthread_mutex_trylock(&mutex->m) == 0;
}

void SDL_UnlockMutex(SDL_Mutex *mutex)
{
    if (mutex) {
        pthread_mutex_unlock(&mutex->m);
    }
}
#else
#include "thread/generic/SDL_sysmutex.c"
#endif
#include "thread/generic/SDL_sysrwlock.c"
/* thread/generic/SDL_syssem.c intentionally excluded: it implements
 * semaphores using a mutex+condition variable ("An implementation of
 * semaphores using mutexes and condition variables" per its own header
 * comment), while thread/generic/SDL_sysmutex.c above implements mutexes
 * using a semaphore ("An implementation of mutexes using semaphores").
 * Paired together, SDL_CreateMutex() -> SDL_CreateSemaphore() ->
 * SDL_CreateMutex() -> ... recurses forever (confirmed: this was the real
 * root cause of an ACCESS_VIOLATION crash inside SDL_Init(), reached via
 * SDL_InitMainThread -> SDL_InitTLSData -> SDL_CreateMutex). Real platforms
 * always pair the generic version of at most one of the two with a native
 * implementation of the other; we do the same below with a direct Win32
 * kernel-semaphore implementation instead of pulling in the full
 * thread/windows backend (which needs core/windows/SDL_windows.h and
 * runtime SRW-lock/WaitOnAddress detection well beyond this build's scope).
 */
#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
/* Linux side of the same "one native semaphore + generic mutex-on-top"
 * design as the Win32 branch below, but built directly on real pthread
 * primitives (pthread_mutex_t/pthread_cond_t + a counter) instead of a
 * kernel semaphore object — squash's -linux target has no <semaphore.h>
 * shim, and pthread_mutex/cond are already proven to resolve correctly
 * against the system libc.so.6 at runtime (see include/pthread.h and the
 * standalone pthread_create/clock_gettime/nanosleep repro that verified
 * this before it was wired in here). No recursion risk with
 * thread/generic/SDL_sysmutex.c above: that file's SDL_Mutex is built on
 * SDL_Semaphore, but THIS SDL_Semaphore is built on raw pthread primitives,
 * not on SDL_Mutex. */
struct SDL_Semaphore {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    unsigned int    count;
};

SDL_Semaphore *SDL_CreateSemaphore(Uint32 initial_value)
{
    SDL_Semaphore *sem = (SDL_Semaphore *)SDL_malloc(sizeof(*sem));
    if (!sem) {
        return ((void*)0);
    }
    pthread_mutex_init(&sem->lock, ((void*)0));
    pthread_cond_init(&sem->cond, ((void*)0));
    sem->count = initial_value;
    return sem;
}

void SDL_DestroySemaphore(SDL_Semaphore *sem)
{
    if (sem) {
        pthread_cond_destroy(&sem->cond);
        pthread_mutex_destroy(&sem->lock);
        SDL_free(sem);
    }
}

bool SDL_WaitSemaphoreTimeoutNS(SDL_Semaphore *sem, Sint64 timeoutNS)
{
    if (!sem) {
        return true;
    }
    pthread_mutex_lock(&sem->lock);
    if (timeoutNS < 0) {
        while (sem->count == 0) {
            pthread_cond_wait(&sem->cond, &sem->lock);
        }
    } else {
        /* No timed pthread_cond wait shim available — this build's call
         * graph only ever waits with timeoutNS < 0 (infinite) or 0 (poll),
         * matching the Win32 branch's own scope. A bounded spin covers the
         * poll case (timeoutNS == 0) and any small positive timeout without
         * needing a real deadline clock here. */
        Uint64 spins = 0;
        while (sem->count == 0) {
            if (spins++ > 1000) {
                pthread_mutex_unlock(&sem->lock);
                return false;
            }
            pthread_mutex_unlock(&sem->lock);
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000;
            nanosleep(&ts, ((void*)0));
            pthread_mutex_lock(&sem->lock);
        }
    }
    sem->count--;
    pthread_mutex_unlock(&sem->lock);
    return true;
}

Uint32 SDL_GetSemaphoreValue(SDL_Semaphore *sem)
{
    if (!sem) {
        return 0;
    }
    pthread_mutex_lock(&sem->lock);
    Uint32 v = sem->count;
    pthread_mutex_unlock(&sem->lock);
    return v;
}

void SDL_SignalSemaphore(SDL_Semaphore *sem)
{
    if (!sem) {
        return;
    }
    pthread_mutex_lock(&sem->lock);
    sem->count++;
    pthread_cond_signal(&sem->cond);
    pthread_mutex_unlock(&sem->lock);
}

/* Minimal Linux thread creation — deliberately not the real
 * thread/pthread/SDL_systhread.c (dlopen-probed pthread_setname_np,
 * sched_setscheduler/SYS_gettid thread-priority plumbing, SDL_dbus.h —
 * well beyond this build's scope, same rationale as the Win32 branch's own
 * "not the full thread/windows backend" comment above). Just enough to
 * create/join/detach a real pthread and report a per-thread ID. */
static void *squash_linux_run_thread(void *data)
{
    SDL_RunThread((SDL_Thread *)data);
    return ((void*)0);
}

bool SDL_SYS_CreateThread(SDL_Thread *thread, SDL_FunctionPointer pfnBeginThread, SDL_FunctionPointer pfnEndThread)
{
    (void)pfnBeginThread; (void)pfnEndThread;
    if (pthread_create(&thread->handle, ((void*)0), squash_linux_run_thread, thread) != 0) {
        return SDL_SetError("pthread_create() failed");
    }
    thread->threadid = (SDL_ThreadID)thread->handle;
    return true;
}

void SDL_SYS_SetupThread(const char *name)
{
    (void)name;
}

bool SDL_SYS_SetThreadPriority(SDL_ThreadPriority priority)
{
    /* Thread scheduling priority is out of this build's scope (see
     * core/SDL_core_unsupported.c's SDL_SetLinuxThreadPriority stub, which
     * this mirrors) — every thread simply runs at the default OS priority. */
    (void)priority;
    return true;
}

SDL_ThreadID SDL_GetCurrentThreadID(void)
{
    return (SDL_ThreadID)pthread_self();
}

void SDL_SYS_WaitThread(SDL_Thread *thread)
{
    pthread_join(thread->handle, ((void*)0));
}

void SDL_SYS_DetachThread(SDL_Thread *thread)
{
    pthread_detach(thread->handle);
}
#else
struct SDL_Semaphore {
    HANDLE h;
};

SDL_Semaphore *SDL_CreateSemaphore(Uint32 initial_value)
{
    SDL_Semaphore *sem = (SDL_Semaphore *)SDL_malloc(sizeof(*sem));
    if (!sem) {
        return ((void*)0);
    }
    sem->h = CreateSemaphoreW(((void*)0), (LONG)initial_value, 0x7fffffff, ((void*)0));
    if (!sem->h) {
        SDL_free(sem);
        return ((void*)0);
    }
    return sem;
}

void SDL_DestroySemaphore(SDL_Semaphore *sem)
{
    if (sem) {
        CloseHandle(sem->h);
        SDL_free(sem);
    }
}

bool SDL_WaitSemaphoreTimeoutNS(SDL_Semaphore *sem, Sint64 timeoutNS)
{
    DWORD ms;
    if (!sem) {
        return true;
    }
    if (timeoutNS < 0) {
        ms = INFINITE;
    } else {
        ms = (DWORD)(timeoutNS / 1000000);
    }
    return WaitForSingleObject(sem->h, ms) == 0;
}

Uint32 SDL_GetSemaphoreValue(SDL_Semaphore *sem)
{
    /* Win32 kernel semaphores have no "peek current count" API; nothing in
     * this build's call graph actually needs the real value. */
    (void)sem;
    return 0;
}

void SDL_SignalSemaphore(SDL_Semaphore *sem)
{
    if (sem) {
        ReleaseSemaphore(sem->h, 1, ((void*)0));
    }
}

#include "thread/windows/SDL_systhread.c"
#endif
static void dbgmark(const char *s);
#include "thread/generic/SDL_systls.c"

#if defined(__linux__) || defined(__APPLE__)
#include <time.h>
/* Minimal Linux performance-counter/delay implementation — real
 * clock_gettime(CLOCK_MONOTONIC)/nanosleep() (see include/time.h's __linux__
 * branch), not the full timer/unix/SDL_systimer.c (HAVE_* feature-macro
 * plumbing, select()-based fallback path — beyond this build's scope, same
 * rationale as the rest of this file's custom minimal stubs). */
Uint64 SDL_GetPerformanceCounter(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (Uint64)now.tv_sec * 1000000000ULL + (Uint64)now.tv_nsec;
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return 1000000000ULL;
}

void SDL_SYS_DelayNS(Uint64 ns)
{
    struct timespec ts;
    ts.tv_sec = (long)(ns / 1000000000ULL);
    ts.tv_nsec = (long)(ns % 1000000000ULL);
    nanosleep(&ts, ((void*)0));
}
#else
/* timer/windows/SDL_systimer.c intentionally not included — beyond the two
 * performance-counter functions SDL_InitTicks() actually needs, it also
 * implements waitable-timer-based delays using GetProcAddress-loaded
 * Windows APIs (CreateWaitableTimerExW, SetWaitableTimerEx, PTIMERAPCROUTINE,
 * REASON_CONTEXT) well beyond this build's scope. Without ANY windows timer
 * file, SDL_GetPerformanceCounter/Frequency were simply undefined symbols
 * ("no IAT entry" — squash tolerates this as a link warning rather than a
 * hard error, but calling them at runtime is a genuine
 * STATUS_ACCESS_VIOLATION), which is what SDL_InitTicks() calls directly. */
Uint64 SDL_GetPerformanceCounter(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (Uint64)counter.QuadPart;
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return (Uint64)frequency.QuadPart;
}

/* SDL_Delay()/SDL_DelayNS() (timer/SDL_timer.c, real/unmodified) call
 * SDL_SYS_DelayNS() directly with no build-config guard, same as the
 * SDL_UpdateAudio() gap above — since the real timer/windows/SDL_systimer.c
 * is excluded (waitable-timer setup well beyond this build's scope), this
 * was an undefined symbol, crashing the instant the frame loop's first
 * SDL_Delay() call executed. This mirrors the plain Sleep(delay) fallback
 * path the real SDL_SYS_DelayNS() itself falls back to when no waitable
 * timer/event is available. */
void SDL_SYS_DelayNS(Uint64 ns)
{
    DWORD delay = (DWORD)(ns / 1000000ULL);
    Sleep(delay);
}
#endif

#include "timer/SDL_timer.c"
/* time/dummy/SDL_systime.c's entire body (SDL_GetSystemTimeLocalePreferences/
 * SDL_GetCurrentTime/SDL_TimeToDateTime) is guarded behind "#ifdef
 * SDL_TIME_DUMMY", which was never defined here on EITHER platform this
 * file targets — a real, pre-existing gap, not something newly introduced
 * by the Linux port: SDL_GetSystemTimeLocalePreferences is called
 * unconditionally from time/SDL_time.c with no build-config guard, so
 * without this define it was an undefined symbol waiting to crash the
 * first real caller (reached here via SDL_Init on Linux; nothing in the
 * examples exercised on Windows so far happened to call it either). */
#define SDL_TIME_DUMMY 1
#include "time/SDL_time.c"
#include "time/dummy/SDL_systime.c"

#include "io/SDL_asyncio.c"
/* SDL_iostream.c: re-enabled after fixing five real, general squash
 * codegen bugs found while chasing this file's crash:
 * (1) pointer "++"/"--" not scaled by sizeof(pointee) for struct pointers
 *     (broke SDL_HashTable's own iteration);
 * (2) typedef'd struct/union globals with a brace initializer were
 *     silently skipped entirely (missing typedef fallback in the
 *     "struct "/"union " prefix check);
 * (3) 64-bit-mode struct-field reads only handled 4-byte/8-byte fields,
 *     silently reading adjacent memory for 1-byte/2-byte fields;
 * (4) including this file as bare "SDL_iostream.c" (unlike its "io/"
 *     sibling above) caused a same-file static function to resolve as an
 *     unknown external, producing a bogus DLL import — fixed by using the
 *     "io/"-prefixed spelling consistently;
 * (5) THE ACTUAL ROOT CAUSE of the remaining crash: "sizeof(*(&expr))"
 *     (dereference of an address-of — exactly what pointer-generic C
 *     macros expand to, e.g. SDL3's own "#define SDL_zerop(x)
 *     SDL_memset((x), 0, sizeof(*(x)))" called as "SDL_zerop(&iface)")
 *     silently returned the POINTER size (8) instead of the real
 *     expression's size, because squash's AST_SIZEOF_EXPR codegen only
 *     recognized a bare-variable dereference ("sizeof(*ptr)" where ptr is
 *     a plain pointer VARIABLE), not this "dereference of an address-of"
 *     shape — falling through to the generic pointer-size default. This
 *     made SDL_INIT_INTERFACE's "iface->version = sizeof(*(iface))" stamp
 *     a bogus, too-small version number into every SDL_IOStreamInterface,
 *     which then failed its own "version < sizeof(*iface)" validity check
 *     inside SDL_OpenIO(), and depending on what got wrongly evaluated
 *     next this manifested as either a wrong-branch-taken logic bug or an
 *     outright wild-jump crash. Confirmed via a from-scratch standalone
 *     repro (`sizeof(*(&x))` for a plain local struct — no SDL involved at
 *     all) reproducing the exact same wrong "8" result. Fixed in
 *     codegen.c by unwrapping any number of "*(&...)" layers (AST_DEREF
 *     wrapping AST_ADDR) down to the real inner expression before applying
 *     the existing sizeof-expression logic.
 * All five confirmed fixed via standalone repros; see
 * [[project-squash-sdl3-build]] for the full diagnostic history. */
#if !defined(__linux__) && !defined(__APPLE__)
/* Only ever called from this file's OTHER Windows-only code (the video
 * PRIVATE_ backend's CreateWindowExA/CreateDIBSection failure paths and
 * WIN_GetModulePath, both gated behind their own #ifndef __linux__ /
 * #else elsewhere in this file) — not compiled at all on Linux, which
 * matters because FormatMessageW/GetLastError are real Win32 kernel32
 * APIs with no meaning here: even though nothing WOULD call them on
 * Linux, squash's ELF output is BIND_NOW (every dynamic symbol resolved
 * eagerly at process load, not lazily on first call), so merely
 * REFERENCING an unresolvable symbol anywhere in the final binary is
 * enough to make ld.so refuse to start the process at all — dead code
 * still has to be dead at compile time here, not just at runtime. */
bool WIN_SetErrorFromHRESULT(const char *prefix, HRESULT hr)
{
    wchar_t wbuf[256];
    DWORD wlen = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, (DWORD)hr, 0, wbuf, 255, NULL);
    char buf[256];
    DWORD i;
    for (i = 0; i < wlen && i < 255; i++) buf[i] = (char)wbuf[i];
    buf[i] = 0;
    for (DWORD j = 0; j < i; j++) { if (buf[j] == '\r') { buf[j] = 0; break; } }
    SDL_SetError("%s%s%s", prefix ? prefix : "", prefix ? ": " : "", buf);
    return false;
}
bool WIN_SetError(const char *prefix)
{
    return WIN_SetErrorFromHRESULT(prefix, (HRESULT)GetLastError());
}
#endif
#include "io/SDL_iostream.c"
#include "io/generic/SDL_asyncio_generic.c"

#include "storage/SDL_storage.c"
#include "storage/generic/SDL_genericstorage.c"

/* SDL_POWER_DISABLED: no real power backend is compiled in here (desktop-
 * only build, no battery/AC-status query needed), so SDL_power.c's own
 * "implementations[]" driver-pointer-array would be genuinely EMPTY
 * (every entry is #ifdef'd behind a platform macro we don't define) —
 * squash crashed (STATUS_ACCESS_VIOLATION) on this zero-element array
 * declaration/SDL_arraysize() use. Defining SDL_POWER_DISABLED removes
 * that array (and the loop over it) entirely, which is the officially
 * supported way SDL3 itself expects a "genuinely no power backend"
 * build to be configured — not a squash-specific workaround. */
#define SDL_POWER_DISABLED 1
#include "power/SDL_power.c"

#include "filesystem/SDL_filesystem.c"
#if defined(__linux__) || defined(__APPLE__)
/* filesystem/unix/SDL_sysfilesystem.c (real /proc/self/exe + XDG paths)
 * would need a fair amount of new libc shim surface this build doesn't
 * have (dirent.h, fcntl.h, real unistd.h readlink()/getcwd()) — out of
 * scope for what "clear" and the rest of this build's examples actually
 * need (none of them call SDL_GetBasePath/GetPrefPath/GetUserFolder). The
 * dummy backend (SDL_Unsupported() no-ops) is a real, officially supported
 * SDL3 filesystem configuration, same rationale as SDL_POWER_DISABLED
 * above — not a squash-specific workaround. */
#define SDL_FILESYSTEM_DUMMY 1
#include "filesystem/dummy/SDL_sysfilesystem.c"
#define SDL_FSOPS_DUMMY 1
#include "filesystem/dummy/SDL_sysfsops.c"
#else
/* filesystem/dummy/SDL_sysfilesystem.c replaced with the REAL
 * filesystem/windows/SDL_sysfilesystem.c below — SDL_SYS_GetBasePath()/
 * GetExeName()/GetCurrentDirectory() (plain GetModuleFileNameW/
 * GetCurrentDirectoryW calls) AND SDL_SYS_GetPrefPath()/GetUserFolder()
 * (SHGetFolderPathW + dynamically-loaded SHGetKnownFolderPath, via the new
 * include/shlobj.h + include/initguid.h shims). WIN_GetModulePath is the
 * one helper that file needs from us, since squash doesn't build the real
 * core/windows/SDL_windows.c (COM-heavy, out of scope). */
char *WIN_GetModulePath(HMODULE handle)
{
    WCHAR path[MAX_PATH];
    DWORD len = GetModuleFileNameW(handle, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH - 1) {
        WIN_SetError("Couldn't locate module");
        return NULL;
    }
    char *retval = (char *)SDL_malloc((size_t)len + 1);
    if (!retval) {
        return NULL;
    }
    DWORD i;
    for (i = 0; i < len; i++) {
        retval[i] = (char)path[i]; /* ASCII-only simplification, matches WIN_SetErrorFromHRESULT above */
    }
    retval[i] = 0;
    return retval;
}

#define SDL_FILESYSTEM_WINDOWS 1
#include "filesystem/windows/SDL_sysfilesystem.c"

/* filesystem/windows/SDL_sysfsops.c: the real (non-dummy) directory/file
 * management backend — plain Win32 CreateDirectoryW/RemoveDirectoryW/
 * DeleteFileW/MoveFileExW/CopyFileExW/GetFileAttributesExW/FindFirstFileExW
 * calls. Declarations for the Win32 APIs this file calls were added to
 * include/windows.h (WIN32_FIND_DATAW, WIN32_FILE_ATTRIBUTE_DATA,
 * CreateDirectoryW, GetFileAttributesExW, etc.) since squash's tiny
 * windows.h shim doesn't have a real winbase.h to draw from. */
#define SDL_FSOPS_WINDOWS 1
#include "filesystem/windows/SDL_sysfsops.c"
#endif

#include "loadso/dummy/SDL_sysloadso.c"

#include "locale/SDL_locale.c"
#include "locale/dummy/SDL_syslocale.c"

#include "misc/SDL_url.c"
#include "misc/dummy/SDL_sysurl.c"

#include "notification/SDL_notification.c"
#include "notification/dummy/SDL_dummynotification.c"

#include "process/SDL_process.c"
#if defined(__linux__) || defined(__APPLE__)
/* process/posix/SDL_posixprocess.c (real fork/exec/pipe backend) is out of
 * scope for the same reason as the filesystem dummy backend above — none
 * of this build's examples spawn subprocesses. The dummy backend
 * (SDL_Unsupported() no-ops) is a real, officially supported SDL3 process
 * configuration. */
#define SDL_PROCESS_DUMMY 1
#include "process/dummy/SDL_dummyprocess.c"
#else
/* process/windows/SDL_windowsprocess.c: the real (non-dummy) CreateProcessW-
 * based backend — real subprocess spawning with pipe I/O. Needed a fair
 * amount of new Win32 surface in include/windows.h (STARTUPINFOW/
 * PROCESS_INFORMATION/SECURITY_ATTRIBUTES structs, CreateProcessW/
 * CreatePipe/DuplicateHandle/SetHandleInformation/SetNamedPipeHandleState/
 * GetExitCodeProcess/EnumWindows/PostMessage/PostThreadMessage/
 * GenerateConsoleCtrlEvent/GetWindowThreadProcessId declarations) since
 * squash's tiny windows.h shim has none of this. */
#define SDL_PROCESS_WINDOWS 1
#include "process/windows/SDL_windowsprocess.c"
#endif

#include "dialog/SDL_dialog.c"
#include "dialog/SDL_dialog_utils.c"
#include "dialog/dummy/SDL_dummydialog.c"

#include "tray/SDL_tray_utils.c"
#include "tray/dummy/SDL_tray.c"

#include "sensor/SDL_sensor.c"
#include "sensor/dummy/SDL_dummysensor.c"

#include "haptic/SDL_haptic.c"
#include "haptic/dummy/SDL_syshaptic.c"

/* squash has no separate-translation-unit model — this whole build is one
 * flat TU, so two unrelated files' file-scope "static" arrays with the exact
 * same bare name collide in squash's single flat symbol table (in real,
 * separately-compiled SDL3 this is harmless: each "static" is scoped to its
 * own .o). SDL_camera.c's own "static const CameraBootStrap *const
 * bootstrap[]" collides with video/SDL_video.c's "static VideoBootStrap
 * *bootstrap[]" (see [[project-squash-sdl3-build]] for the full
 * investigation this uncovered — the collision made SDL_VideoInit()'s own
 * "bootstrap[i]->create()" resolve "create" against CameraBootStrap's field
 * list instead of VideoBootStrap's, since squash's codegen looks up a plain
 * identifier by name with no per-file scoping, and the two matching names
 * make whichever was registered LAST in the whole unity build shadow the
 * other for every use anywhere in the file, regardless of which file the
 * *use* is textually in). Renamed via a plain textual macro substitution
 * (limited to camera's own #include, exactly like every other build-config
 * override in this file) rather than touching SDL3 source — no compiler
 * change needed, and it fully separates the two identically-named globals
 * at the text level before parsing ever sees them. */
#define bootstrap camera_driver_bootstrap
#include "camera/SDL_camera.c"
#include "camera/dummy/SDL_camera_dummy.c"
#undef bootstrap

#include "joystick/SDL_joystick.c"
#include "joystick/SDL_gamepad.c"
#include "joystick/SDL_steam_virtual_gamepad.c"
#include "joystick/controller_type.c"
#include "joystick/dummy/SDL_sysjoystick.c"

/* ============================================================================
 * Custom SDL_AUDIO_DRIVER_PRIVATE backend: a real (not dummy) minimal Win32
 * audio-output implementation, hand-written directly in this TU rather than
 * including SDL3's own audio/directsound or audio/wasapi backends (both are
 * COM-based, pulling in a lot of interface/vtable-call machinery well beyond
 * this build's scope). Uses the plain WinMM waveOut API instead — two
 * double-buffered PCM16 buffers, polled (not callback-driven) exactly like
 * the video backend's PeekMessageA polling loop. Always opens the device as
 * 44.1kHz/stereo/S16 regardless of what the app asked for; SDL_AudioStream
 * (SDL_audiocvt.c/SDL_audioresample.c/SDL_audiotypecvt.c, included below)
 * handles converting from the app's requested format — this is the same
 * "the backend may change any of these values" contract real SDL3 backends
 * use (see SDL_audio.c's own OpenPhysicalAudioDevice() comment).
 *
 * FIXED (previously: no audible output / hang on the first
 * SDL_PutAudioStreamData call — see SDL3_Build/repro_audio_only_hang.c,
 * still kept as a standing regression repro). Root cause was FOUR separate,
 * real squash codegen bugs, not this backend or SDL3 itself, all in
 * codegen.c:
 *   1. "(*ptr).field" (explicit-dereference-then-dot, as opposed to
 *      "ptr->field") always computed byte offset 0 regardless of which
 *      field was named — resolve_node_type()/field_byte_offset() had no
 *      AST_DEREF case at all. Hit by SDL_AUDIO_FRAMESIZE(*spec)-style
 *      macro expansions throughout real SDL3 code.
 *   2. 2D array element addressing ("T x[N][M]") for a typedef'd POINTER
 *      element type (elem_size_of()) used typeinfo_size() directly, which
 *      can't resolve typedefs and defaults to 4 bytes for anything it
 *      doesn't recognize — silently halving the real 8-byte stride for
 *      both dimensions. Hit by real SDL3's
 *      channel_converters[8][8] (SDL_audio_channel_converters.h), corrupting
 *      the whole table and eventually calling through NULL/garbage function
 *      pointers.
 *   3. Struct-field type resolution for TYPEDEF CHAINS (a typedef of a
 *      typedef, e.g. "typedef SDL_AudioStreamDataCompleteCallback
 *      SDL_ReleaseAudioBufferCallback;") only unwrapped ONE typedef hop
 *      before checking pointer_depth, so a 2-hop chain still looked
 *      non-pointer and loaded with a 32-bit sign-extending movsxd instead
 *      of a real 64-bit load — truncated SDL_AudioTrack::callback's upper
 *      32 bits, crashing DestroyAudioTrack().
 *   4. codegen_branch()'s CMP+Jcc fast path for if/while conditions never
 *      checked whether the operands were float-typed, always doing a raw
 *      INTEGER compare of the operands' bit patterns — "if (gain != 1.0f)"
 *      could evaluate true even when gain held 1.0f's exact bits. Also
 *      exposed a FIFTH bug once fixed: "++"/"--" on a float variable was
 *      pure integer codegen (added 1 to the raw bit pattern, and worse,
 *      was silently intercepted into codegen_float_expr()'s AST_UNARY-less
 *      switch and no-opped entirely) — this made real SDL3's BesselI0()
 *      (SDL_audioresample.c, generates the Kaiser-window resampler filter)
 *      loop forever: its `++i` never advanced, so its series diverged to
 *      +infinity instead of converging.
 * All five confirmed via minimal from-scratch repros (not just re-running
 * the SDL3 build) before being fixed in codegen.c. The WinMM backend and
 * squash's basic threading primitives were correctly ruled out early in the
 * investigation (see git history) and were never the problem. */
#include "audio/SDL_sysaudio.h"

#if defined(__APPLE__)
#include <dlfcn.h>
/* Real macOS audio backend, via AudioToolbox's AudioQueue API -- CoreAudio's
 * own real coreaudio.m backend is Objective-C and pulls in AudioUnit/HAL
 * machinery well beyond this build's scope; AudioQueue is plain,
 * struct-by-POINTER-only C (verified against the real system header:
 * AudioQueueNewOutput/AllocateBuffer/EnqueueBuffer/Start/Stop/Dispose all
 * take pointers/scalars, never a struct by value -- unlike AppKit, this
 * needed no codegen fix at all), loaded via dlopen()/dlsym() the same way
 * AppKit is (squash's macOS backend still only links libSystem.B.dylib at
 * compile time -- see compiler.c's "-l is ignored for -macos" diagnostic).
 *
 * Mirrors the Win32 WinMM backend's double-buffer polling shape below
 * (OpenDevice/CloseDevice/WaitDevice/PlayDevice/GetDeviceBuf, driven by
 * SDL3's own real PlaybackAudioThread in audio/SDL_audio.c) instead of
 * AudioQueue's natural callback-driven style: WaitDevice blocks on a real
 * SDL_Semaphore (this build's own pthread-mutex/cond-based implementation,
 * genuinely cross-thread-safe since it's built on real OS primitives, not
 * app-level scheduling), which the AudioQueueOutputCallback -- invoked on
 * AudioQueue's own internal thread (inCallbackRunLoop=NULL below), entirely
 * independent of this driver's own hand-rolled Cocoa event pump -- signals
 * every time a buffer finishes playing and is free to refill. Semaphore
 * starts at count=2 (both buffers already free) so the first two
 * WaitDevice calls return immediately. */
typedef struct {
    double       mSampleRate;
    unsigned int mFormatID;
    unsigned int mFormatFlags;
    unsigned int mBytesPerPacket;
    unsigned int mFramesPerPacket;
    unsigned int mBytesPerFrame;
    unsigned int mChannelsPerFrame;
    unsigned int mBitsPerChannel;
    unsigned int mReserved;
} SQAudioStreamBasicDescription;

typedef struct {
    unsigned int mAudioDataBytesCapacity;
    void        *mAudioData;
    unsigned int mAudioDataByteSize;
    void        *mUserData;
    unsigned int mPacketDescriptionCapacity;
    void        *mPacketDescriptions;
    unsigned int mPacketDescriptionCount;
} SQAudioQueueBuffer;

#define SQ_kAudioFormatLinearPCM           0x6C70636Du /* 'lpcm' */
#define SQ_kAudioFormatFlagIsSignedInteger 0x4u
#define SQ_kAudioFormatFlagIsPacked        0x8u

/* Real layout (AudioHardwareBase.h): 3 consecutive UInt32 fields, no
 * padding. Used only to query the default output device's real hardware
 * sample rate below -- see sq_get_output_sample_rate's own comment for why
 * this matters. */
typedef struct {
    unsigned int mSelector;
    unsigned int mScope;
    unsigned int mElement;
} SQAudioObjectPropertyAddress;

#define SQ_kAudioObjectSystemObject                 1u
#define SQ_kAudioObjectPropertyScopeGlobal           0x676C6F62u /* 'glob' */
#define SQ_kAudioObjectPropertyElementMain           0u
#define SQ_kAudioHardwarePropertyDefaultOutputDevice 0x644F7574u /* 'dOut' */
#define SQ_kAudioDevicePropertyNominalSampleRate     0x6E737274u /* 'nsrt' */

static void *g_audiotoolbox_handle;
static int (*g_AudioQueueNewOutput)(const SQAudioStreamBasicDescription*, void*, void*, void*, void*, unsigned int, void*);
static int (*g_AudioQueueAllocateBuffer)(void*, unsigned int, void*);
static int (*g_AudioQueueEnqueueBuffer)(void*, void*, unsigned int, const void*);
static int (*g_AudioQueueStart)(void*, const void*);
static int (*g_AudioQueueStop)(void*, int);
static int (*g_AudioQueueDispose)(void*, int);

static void *g_coreaudio_handle;
static int (*g_AudioObjectGetPropertyData)(unsigned int, const SQAudioObjectPropertyAddress*, unsigned int, const void*, unsigned int*, void*);

/* AudioQueueStart(), confirmed via a standalone real-clang repro run
 * against this machine's actual hardware, FAILS (OSStatus 35) when the
 * requested AudioStreamBasicDescription's mSampleRate doesn't match the
 * default output device's real nominal rate -- this build's audio format
 * used to hardcode 44100Hz (matching the WinMM branch's own convention),
 * but this Mac's built-in speakers report a real nominal rate of 48000Hz;
 * requesting 44100 made every AudioQueueStart call fail. Querying the
 * actual device (AudioObjectGetPropertyData, CoreAudio.framework -- a
 * SEPARATE framework/dlopen from AudioToolbox, both plain pointer/scalar C
 * APIs like the rest of this backend) instead of hardcoding a guess makes
 * this work regardless of what rate a given Mac's hardware actually runs
 * at, rather than just papering over this one machine's specific value. */
static double sq_get_output_sample_rate(void)
{
    if (!g_coreaudio_handle) {
        g_coreaudio_handle = dlopen("/System/Library/Frameworks/CoreAudio.framework/CoreAudio", RTLD_NOW);
        if (g_coreaudio_handle) {
            g_AudioObjectGetPropertyData = (int (*)(unsigned int, const SQAudioObjectPropertyAddress*, unsigned int, const void*, unsigned int*, void*))
                dlsym(g_coreaudio_handle, "AudioObjectGetPropertyData");
        }
    }
    if (!g_AudioObjectGetPropertyData) {
        return 44100.0;
    }

    unsigned int device_id = 0;
    SQAudioObjectPropertyAddress addr;
    addr.mSelector = SQ_kAudioHardwarePropertyDefaultOutputDevice;
    addr.mScope = SQ_kAudioObjectPropertyScopeGlobal;
    addr.mElement = SQ_kAudioObjectPropertyElementMain;
    unsigned int sz = (unsigned int)sizeof(device_id);
    if (g_AudioObjectGetPropertyData(SQ_kAudioObjectSystemObject, &addr, 0, ((void*)0), &sz, &device_id) != 0) {
        return 44100.0;
    }

    double rate = 0;
    addr.mSelector = SQ_kAudioDevicePropertyNominalSampleRate;
    sz = (unsigned int)sizeof(rate);
    if (g_AudioObjectGetPropertyData(device_id, &addr, 0, ((void*)0), &sz, &rate) != 0 || rate <= 0) {
        return 44100.0;
    }
    return rate;
}

struct SDL_PrivateAudioData {
    void           *queue;      /* AudioQueueRef */
    SQAudioQueueBuffer *bufs[2]; /* AudioQueueBufferRef */
    int             cur;
};

/* This callback runs on CoreAudio's own "AQClient" thread (inCallbackRunLoop
 * =NULL below means "call on an internal thread"). It originally crashed
 * (EXC_BAD_ACCESS in pthread_mutex_lock) whenever it called into any of this
 * build's own synchronization machinery (e.g. SDL_SignalSemaphore) -- that
 * was squash's general callee-saved-register (RBX/R12-R15) corruption bug
 * for SysV callback targets (see SQ_SYSV_CALLEE_SAVE_WORDS in symtable.h),
 * now fixed at the compiler level, so any synchronization primitive would
 * be safe here again. Kept as a lock-free counter anyway since it's simpler
 * and this backend only ever has one audio device open at a time
 * (OnlyHasDefaultPlaybackDevice = true) -- PRIVATEAUDIO_WaitDevice polls it
 * with a bounded sleep loop, the same shape already used by the WinMM
 * backend's own WHDR_DONE poll further below. */
static volatile int sq_aq_global_free_count = 0;
static void sq_audioqueue_output_callback(void *inUserData, void *inAQ, void *inBuffer)
{
    (void)inUserData; (void)inAQ; (void)inBuffer;
    sq_aq_global_free_count = sq_aq_global_free_count + 1;
}

static bool sq_ensure_audiotoolbox(void)
{
    if (g_audiotoolbox_handle) {
        return true;
    }
    g_audiotoolbox_handle = dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox", RTLD_NOW);
    if (!g_audiotoolbox_handle) {
        return false;
    }
    g_AudioQueueNewOutput = (int (*)(const SQAudioStreamBasicDescription*, void*, void*, void*, void*, unsigned int, void*))
        dlsym(g_audiotoolbox_handle, "AudioQueueNewOutput");
    g_AudioQueueAllocateBuffer = (int (*)(void*, unsigned int, void*))
        dlsym(g_audiotoolbox_handle, "AudioQueueAllocateBuffer");
    g_AudioQueueEnqueueBuffer = (int (*)(void*, void*, unsigned int, const void*))
        dlsym(g_audiotoolbox_handle, "AudioQueueEnqueueBuffer");
    g_AudioQueueStart = (int (*)(void*, const void*))
        dlsym(g_audiotoolbox_handle, "AudioQueueStart");
    g_AudioQueueStop = (int (*)(void*, int))
        dlsym(g_audiotoolbox_handle, "AudioQueueStop");
    g_AudioQueueDispose = (int (*)(void*, int))
        dlsym(g_audiotoolbox_handle, "AudioQueueDispose");
    return g_AudioQueueNewOutput && g_AudioQueueAllocateBuffer && g_AudioQueueEnqueueBuffer &&
           g_AudioQueueStart && g_AudioQueueStop && g_AudioQueueDispose;
}

static bool PRIVATEAUDIO_WaitDevice(SDL_AudioDevice *device)
{
    (void)device;
    int spins = 0;
    while (sq_aq_global_free_count <= 0) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 1000000; /* 1ms */
        nanosleep(&ts, ((void*)0));
        spins++;
        if (spins > 20000) { /* ~20s; real buffer completion is well under 1s */
            return false;
        }
    }
    sq_aq_global_free_count--;
    return true;
}

static Uint8 *PRIVATEAUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    SQAudioQueueBuffer *buf = device->hidden->bufs[device->hidden->cur];
    if (buffer_size) {
        *buffer_size = (int)buf->mAudioDataBytesCapacity;
    }
    return (Uint8 *)buf->mAudioData;
}

static bool PRIVATEAUDIO_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
    (void)buffer;
    SQAudioQueueBuffer *buf = device->hidden->bufs[device->hidden->cur];
    buf->mAudioDataByteSize = (unsigned int)buflen;
    g_AudioQueueEnqueueBuffer(device->hidden->queue, buf, 0, ((void*)0));
    device->hidden->cur = (device->hidden->cur + 1) % 2;
    return true;
}

static bool PRIVATEAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    if (device->recording) {
        return SDL_Unsupported();
    }
    if (!sq_ensure_audiotoolbox()) {
        return SDL_SetError("Couldn't load AudioToolbox.framework");
    }

    /* Force a known-good PCM format regardless of what was requested —
     * SDL_AudioStream converts on our behalf (same convention as the WinMM
     * branch below), except the sample rate: that one has to match the
     * real output device's actual nominal rate or AudioQueueStart fails
     * outright (see sq_get_output_sample_rate's own comment). */
    double hw_rate = sq_get_output_sample_rate();
    device->spec.format = SDL_AUDIO_S16;
    device->spec.channels = 2;
    device->spec.freq = (int)hw_rate;
    device->sample_frames = SDL_GetDefaultSampleFramesFromFreq(device->spec.freq);

    device->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) {
        return false;
    }

    sq_aq_global_free_count = 2; /* both buffers start free */

    int framesize = 2 * (int)sizeof(Sint16);
    int bufsize = device->sample_frames * framesize;

    SQAudioStreamBasicDescription fmt;
    SDL_zero(fmt);
    fmt.mSampleRate = hw_rate;
    fmt.mFormatID = SQ_kAudioFormatLinearPCM;
    fmt.mFormatFlags = SQ_kAudioFormatFlagIsSignedInteger | SQ_kAudioFormatFlagIsPacked;
    fmt.mBytesPerPacket = (unsigned int)framesize;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = (unsigned int)framesize;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 16;

    void *queue = ((void*)0);
    if (g_AudioQueueNewOutput(&fmt, (void*)sq_audioqueue_output_callback, ((void*)0), ((void*)0), ((void*)0), 0, &queue) != 0 || !queue) {
        return SDL_SetError("AudioQueueNewOutput failed");
    }
    device->hidden->queue = queue;

    int i;
    for (i = 0; i < 2; i++) {
        void *bufref = ((void*)0);
        if (g_AudioQueueAllocateBuffer(queue, (unsigned int)bufsize, &bufref) != 0 || !bufref) {
            return SDL_SetError("AudioQueueAllocateBuffer failed");
        }
        device->hidden->bufs[i] = (SQAudioQueueBuffer *)bufref;
    }

    if (g_AudioQueueStart(queue, ((void*)0)) != 0) {
        return SDL_SetError("AudioQueueStart failed");
    }

    return true;
}

static void PRIVATEAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    if (!device->hidden) {
        return;
    }
    if (device->hidden->queue) {
        g_AudioQueueStop(device->hidden->queue, 1);
        g_AudioQueueDispose(device->hidden->queue, 1);
    }
    SDL_free(device->hidden);
    device->hidden = NULL;
}

static bool PRIVATEAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = PRIVATEAUDIO_OpenDevice;
    impl->CloseDevice = PRIVATEAUDIO_CloseDevice;
    impl->WaitDevice = PRIVATEAUDIO_WaitDevice;
    impl->PlayDevice = PRIVATEAUDIO_PlayDevice;
    impl->GetDeviceBuf = PRIVATEAUDIO_GetDeviceBuf;
    impl->OnlyHasDefaultPlaybackDevice = true;
    impl->OnlyHasDefaultRecordingDevice = true;
    impl->HasRecordingSupport = false;
    return true;
}
#elif defined(__linux__)
/* No real Linux audio backend (ALSA/PulseAudio/PipeWire) is wired in here —
 * out of scope for what this Linux port set out to fix (none of the
 * examples exercised so far, including "clear", open an audio device).
 * PRIVATEAUDIO_Init still needs to exist and be wired into
 * PRIVATEAUDIO_bootstrap (SDL_Init(SDL_INIT_AUDIO) calls it unconditionally
 * to build the audio_driver_bootstrap[] table), but every device operation
 * simply reports unsupported rather than referencing any WinMM API — the
 * Win32 branch's HWAVEOUT/waveOut* calls below are real kernel32/winmm
 * symbols that don't exist on Linux, and squash's ELF output is BIND_NOW
 * (every dynamic symbol resolved eagerly at process load), so merely
 * compiling a reference to them into the binary — even in code no caller
 * on Linux would ever reach — is already enough to make ld.so refuse to
 * start the process. */
struct SDL_PrivateAudioData {
    int unused;
};

static bool PRIVATEAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    (void)device;
    return SDL_Unsupported();
}

static void PRIVATEAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    (void)device;
}

static bool PRIVATEAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = PRIVATEAUDIO_OpenDevice;
    impl->CloseDevice = PRIVATEAUDIO_CloseDevice;
    impl->OnlyHasDefaultPlaybackDevice = true;
    impl->OnlyHasDefaultRecordingDevice = true;
    impl->HasRecordingSupport = false;
    return true;
}
#else
struct SDL_PrivateAudioData {
    HWAVEOUT hwo;
    WAVEHDR  hdrs[2];
    Uint8   *bufs[2];
    int      cur;
};

static bool PRIVATEAUDIO_WaitDevice(SDL_AudioDevice *device)
{
    WAVEHDR *h = &device->hidden->hdrs[device->hidden->cur];
    int spins = 0;
    /* This loop previously had no exit condition at all: if WHDR_DONE never
     * gets set (a stalled/misbehaving waveOut device, or any other reason),
     * it spun forever. SDL3's own shutdown path (ClosePhysicalAudioDevice)
     * calls SDL_WaitThread(device->thread, NULL) -- an INFINITE wait -- on
     * this exact thread, and only calls CloseDevice() (the one thing that
     * could force-complete pending buffers, via waveOutReset) AFTER that
     * thread has already joined. So an unbounded wait here made the entire
     * process permanently unkillable through any normal means. Returning
     * false after a generous cap lets SDL_AudioDeviceDisconnected mark the
     * device a zombie instead, which is the sanctioned way to fail out of
     * WaitDevice (see PlaybackAudioThread in audio/SDL_audio.c). */
    while (!(h->dwFlags & WHDR_DONE)) {
        Sleep(1);
        spins++;
        if (spins > 20000) { /* ~20s; real buffer completion is well under 1s */
            fprintf(stderr, "PRIVATEAUDIO_WaitDevice: stuck waiting on cur=%d dwFlags=0x%08lx after %d spins -- treating device as disconnected\n",
                    device->hidden->cur, (unsigned long)h->dwFlags, spins);
            return false;
        }
    }
    return true;
}

static Uint8 *PRIVATEAUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    (void)buffer_size;
    return device->hidden->bufs[device->hidden->cur];
}

static bool PRIVATEAUDIO_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
    (void)buffer;
    WAVEHDR *h = &device->hidden->hdrs[device->hidden->cur];
    h->dwBufferLength = (DWORD)buflen;
    h->dwFlags &= ~(DWORD)WHDR_DONE;
    waveOutWrite(device->hidden->hwo, h, (UINT)sizeof(WAVEHDR));
    device->hidden->cur = (device->hidden->cur + 1) % 2;
    return true;
}

static bool PRIVATEAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    if (device->recording) {
        return SDL_Unsupported();
    }

    /* Force a known-good PCM format regardless of what was requested —
     * SDL_AudioStream converts on our behalf. */
    device->spec.format = SDL_AUDIO_S16;
    device->spec.channels = 2;
    device->spec.freq = 44100;
    device->sample_frames = SDL_GetDefaultSampleFramesFromFreq(device->spec.freq);

    device->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) {
        return false;
    }

    int framesize = 2 * (int)sizeof(Sint16);
    int bufsize = device->sample_frames * framesize;

    WAVEFORMATEX wfx;
    SDL_zero(wfx);
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = 44100;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)framesize;
    wfx.nAvgBytesPerSec = 44100u * (DWORD)framesize;

    if (waveOutOpen(&device->hidden->hwo, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        return SDL_SetError("waveOutOpen failed");
    }

    int i;
    for (i = 0; i < 2; i++) {
        device->hidden->bufs[i] = (Uint8 *)SDL_calloc(1, bufsize);
        if (!device->hidden->bufs[i]) {
            return false;
        }
        device->hidden->hdrs[i].lpData = (char *)device->hidden->bufs[i];
        device->hidden->hdrs[i].dwBufferLength = (DWORD)bufsize;
        waveOutPrepareHeader(device->hidden->hwo, &device->hidden->hdrs[i], (UINT)sizeof(WAVEHDR));
        device->hidden->hdrs[i].dwFlags |= WHDR_DONE;
    }

    return true;
}

static void PRIVATEAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    if (!device->hidden) {
        return;
    }
    if (device->hidden->hwo) {
        int i;
        waveOutReset(device->hidden->hwo);
        for (i = 0; i < 2; i++) {
            waveOutUnprepareHeader(device->hidden->hwo, &device->hidden->hdrs[i], (UINT)sizeof(WAVEHDR));
            SDL_free(device->hidden->bufs[i]);
        }
        waveOutClose(device->hidden->hwo);
    }
    SDL_free(device->hidden);
    device->hidden = NULL;
}

static bool PRIVATEAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = PRIVATEAUDIO_OpenDevice;
    impl->CloseDevice = PRIVATEAUDIO_CloseDevice;
    impl->WaitDevice = PRIVATEAUDIO_WaitDevice;
    impl->PlayDevice = PRIVATEAUDIO_PlayDevice;
    impl->GetDeviceBuf = PRIVATEAUDIO_GetDeviceBuf;
    impl->OnlyHasDefaultPlaybackDevice = true;
    impl->OnlyHasDefaultRecordingDevice = true;
    impl->HasRecordingSupport = false;
    return true;
}
#endif

/* NOT brace-initialized — see squash_init_private_bootstrap()'s own comment
 * (video's PRIVATE_bootstrap, right below): a global struct-with-function-
 * pointer-field brace initializer at file scope was observed to read back as
 * garbage after Pass 0.5's rewrite in an earlier investigation; assigning
 * the fields as ordinary runtime statements sidesteps it entirely. */
AudioBootStrap PRIVATEAUDIO_bootstrap;

/* See camera's own identical comment a few lines above: squash's single flat
 * symbol table has no per-file "static" scoping, so audio/SDL_audio.c's own
 * "static const AudioBootStrap *const bootstrap[]" would otherwise collide
 * with video/SDL_video.c's and camera/SDL_camera.c's same-named arrays. */
#define bootstrap audio_driver_bootstrap
#include "audio/SDL_audio.c"
#include "audio/SDL_audiocvt.c"
#include "audio/SDL_audioqueue.c"
#include "audio/SDL_audioresample.c"
#include "audio/SDL_audiotypecvt.c"
#include "audio/SDL_mixer.c"
#include "audio/SDL_wave.c"
#undef bootstrap

#include "events/SDL_categories.c"
/* SDL_clipboardevents.c calls SDL_SaveClipboardMimeTypes()/
 * SDL_CancelClipboardData(), both defined in video/SDL_clipboard.c — not
 * part of this build (real windowing support is out of scope here). A
 * call to a same-TU function that's never defined is a hard
 * "asm_resolve: undefined label" error (unlike a genuinely external/
 * imported symbol, which squash's linker tolerates as a "no IAT entry"
 * warning), so real bodies are provided below, matching the pattern
 * already used for the other excluded-subsystem stubs near main(). No
 * real clipboard/video state exists in this build, so these are no-ops. */
void SDL_CancelClipboardData(Uint32 sequence) { (void)sequence; }
bool SDL_SaveClipboardMimeTypes(const char *const *mime_types, size_t num_mime_types) {
    (void)mime_types; (void)num_mime_types;
    return true;
}
#include "events/SDL_clipboardevents.c"
#include "events/SDL_displayevents.c"
#include "events/SDL_dropevents.c"
#include "events/SDL_events.c"
#include "events/SDL_eventwatch.c"
#include "events/SDL_keyboard.c"
#include "events/SDL_keymap.c"
#include "events/SDL_keysym_to_keycode.c"
#include "events/SDL_keysym_to_scancode.c"
#include "events/SDL_mouse.c"
#include "events/SDL_notificationevents.c"
#include "events/SDL_pen.c"
#include "events/SDL_quit.c"
#include "events/SDL_scancode_tables.c"
#include "events/SDL_touch.c"
#include "events/SDL_windowevents.c"
#include "events/imKStoUCS.c"

/* ============================================================================
 * Custom SDL_VIDEO_DRIVER_PRIVATE backend: a real (not dummy) minimal
 * windowing implementation, hand-written directly in this TU rather than
 * including SDL3's own video/windows or video/x11 backends (which pull in
 * IME, raw input, drag-and-drop OLE / XRandR, XInput2, GLX, clipboard
 * managers etc. well beyond this build's scope). Two independent halves
 * live here side by side, selected by #ifdef __linux__ / #else, exactly
 * like every other native primitive in this file (semaphore/thread/timer
 * above): the Win32 half (GDI DIB-section framebuffer) is unchanged from
 * before; the Linux half is a real (not dummy) Xlib window + XImage/XPutImage
 * framebuffer, hand-declaring just the libX11 ABI surface actually used
 * (matching this project's existing include/GL/glx.h and
 * SDL3_Build/scratch/platform_shim.h convention of not pulling in the full
 * system Xlib.h — squash's C frontend has known limits on real-world header
 * trees). This is what lets the portable SDL_VIDEO_RENDER_SW software
 * renderer (render/software/SDL_render_sw.c, included below) actually put
 * pixels on screen via SDL_GetWindowSurface()/SDL_UpdateWindowSurface() on
 * either platform. No keyboard/mouse/OpenGL/Vulkan hookup yet — just enough
 * to create/show/resize/close a window and blit a software framebuffer to
 * it. See [[project-squash-sdl3-build]] memory notes.
 * ============================================================================ */
static void dbgmark(const char *s);
static void dbgmark_ptr(const char *label, unsigned long long v);

#if defined(__linux__)
/* Minimal hand-declared libX11 ABI surface — real system libX11.so.6
 * (confirmed present and already used by SDL3_Build/scratch/platform_shim.h's
 * identical hand-declaration approach for the Vulkan demos). XImage's field
 * layout below matches real X11/Xlib.h exactly (same field order/types),
 * which is sufficient for it to line up byte-for-byte with what
 * XCreateImage/XPutImage/XDestroyImage expect — standard C struct layout
 * rules reproduce the real ABI automatically given identical declarations,
 * without needing to hand-compute any padding. The trailing function-pointer
 * table (XImageFuncs) is never read/written by code in this file, only
 * passed through to libX11 itself (via XDestroyImage), so its members are
 * left as opaque void* rather than fully-prototyped function pointers. */
typedef void   Display;
typedef unsigned long Window;
typedef unsigned long XAtom;
typedef void   Visual;
typedef void  *GC;

struct XImageFuncs {
    void *create_image;
    void *destroy_image;
    void *get_pixel;
    void *put_pixel;
    void *sub_image;
    void *add_pixel;
};

typedef struct _XImage {
    int width, height;
    int xoffset;
    int format;
    char *data;
    int byte_order;
    int bitmap_unit;
    int bitmap_bit_order;
    int bitmap_pad;
    int depth;
    int bytes_per_line;
    int bits_per_pixel;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    void *obdata;
    struct XImageFuncs f;
} XImage;

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

typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    Window event;
    Window window;
    int x, y;
    int width, height;
    int border_width;
    Window above;
    int override_redirect;
} XConfigureEvent;

/* Real Xlib.h ABI (Time = unsigned long, Bool = int) -- these events are
 * produced entirely by the real libX11.so's own XNextEvent, never
 * constructed here, so field order/types must match the genuine struct
 * exactly, same convention as XConfigureEvent/XClientMessageEvent above. */
typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    unsigned long time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned int button;
    int same_screen;
} XButtonEvent;

typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    unsigned long time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    char is_hint;
    int same_screen;
} XMotionEvent;

/* Real Xlib.h ABI, same field layout as XButtonEvent (both have an
 * "unsigned int" 12th member -- "button" there, "keycode" here). */
typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    unsigned long time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned int keycode;
    int same_screen;
} XKeyEvent;

typedef union {
    int type;
    XClientMessageEvent xclient;
    XConfigureEvent xconfigure;
    XButtonEvent xbutton;
    XMotionEvent xmotion;
    XKeyEvent xkey;
    long pad[24]; /* matches real Xlib.h's XEvent size-guaranteeing member */
} XEvent;

Display *XOpenDisplay(const char *display_name);
int XCloseDisplay(Display *dpy);
int XDefaultScreen(Display *dpy);
Window XRootWindow(Display *dpy, int screen_number);
unsigned long XBlackPixel(Display *dpy, int screen_number);
unsigned long XWhitePixel(Display *dpy, int screen_number);
int XDisplayWidth(Display *dpy, int screen_number);
int XDisplayHeight(Display *dpy, int screen_number);
Visual *XDefaultVisual(Display *dpy, int screen_number);
int XDefaultDepth(Display *dpy, int screen_number);
Window XCreateSimpleWindow(Display *dpy, Window parent, int x, int y,
    unsigned int width, unsigned int height, unsigned int border_width,
    unsigned long border, unsigned long background);
int XMapWindow(Display *dpy, Window w);
int XUnmapWindow(Display *dpy, Window w);
int XStoreName(Display *dpy, Window w, const char *window_name);
int XSelectInput(Display *dpy, Window w, long event_mask);
int XNextEvent(Display *dpy, XEvent *event_return);
int XPending(Display *dpy);
int XDestroyWindow(Display *dpy, Window w);
int XFlush(Display *dpy);
XAtom XInternAtom(Display *dpy, const char *atom_name, int only_if_exists);
int XSetWMProtocols(Display *dpy, Window w, XAtom *protocols, int count);
GC XCreateGC(Display *dpy, Window d, unsigned long valuemask, void *values);
int XFreeGC(Display *dpy, GC gc);
XImage *XCreateImage(Display *dpy, Visual *visual, unsigned int depth, int format, int offset,
    char *data, unsigned int width, unsigned int height, int bitmap_pad, int bytes_per_line);
int XDestroyImage(XImage *ximage);
int XPutImage(Display *dpy, Window d, GC gc, XImage *image, int src_x, int src_y,
    int dest_x, int dest_y, unsigned int width, unsigned int height);
/* KeySym is really "typedef XID KeySym" (an unsigned long) in real Xlib.h --
 * only used here to translate a small, fixed set of navigation keys (see
 * PRIVATE_PumpEvents' own comment), never stored/compared as a distinct
 * type, so the raw unsigned long is used directly rather than introducing
 * a KeySym typedef purely for its own sake. */
unsigned long XLookupKeysym(XKeyEvent *key_event, int index);
int XSetInputFocus(Display *dpy, Window focus, int revert_to, unsigned long time);

#define SQ_ZPixmap        2
#define SQ_StructureNotifyMask (1L << 17)
#define SQ_KeyPressMask        (1L << 0)
#define SQ_KeyReleaseMask      (1L << 1)
#define SQ_ButtonPressMask     (1L << 2)
#define SQ_ButtonReleaseMask   (1L << 3)
#define SQ_PointerMotionMask   (1L << 6)
#define SQ_KeyPress        2
#define SQ_KeyRelease      3
#define SQ_ButtonPress     4
#define SQ_ButtonRelease   5
#define SQ_MotionNotify    6
#define SQ_ConfigureNotify 22
#define SQ_ClientMessage   33
/* X11 has no dedicated wheel-event type -- a scroll click arrives as an
 * ordinary ButtonPress with button 4 (up) or 5 (down), the decades-old
 * X11 convention every toolkit (including real SDL3's own X11 backend)
 * follows. Buttons 1-3 are the real left/middle/right buttons. */
#define SQ_BUTTON_WHEEL_UP   4
#define SQ_BUTTON_WHEEL_DOWN 5

/* Real X11/keysymdef.h values (stable, public ABI constants -- these are
 * literal numbers baked into every X server/keyboard driver, not something
 * that needs a real header to obtain). Only the handful this project's own
 * page-scrolling keys need -- see PRIVATE_PumpEvents' own comment. */
#define SQ_XK_Home      0xff50
#define SQ_XK_Up        0xff52
#define SQ_XK_Page_Up   0xff55
#define SQ_XK_Down      0xff54
#define SQ_XK_Page_Down 0xff56
#define SQ_XK_End       0xff57

/* Real X.h values -- used only by the XSetInputFocus call in
 * PRIVATE_ShowWindow (see its own comment for why that call exists). */
#define SQ_RevertToParent 2
#define SQ_CurrentTime     0L

struct SDL_WindowData {
    Window win;
    GC     gc;
    int    w, h; /* cached size, updated on create + ConfigureNotify */
};

static Display *g_x11_display = ((void*)0);
static XAtom    g_x11_wm_delete;
/* Set once by the first real ConfigureNotify -- see PRIVATE_PumpEvents'
 * own comment on why XSetInputFocus is requested there, not eagerly in
 * PRIVATE_ShowWindow. */
static int g_input_focus_requested;

static void PRIVATE_PumpEvents(SDL_VideoDevice *_this);

static bool PRIVATE_VideoInit(SDL_VideoDevice *_this)
{
    (void)_this;
    g_x11_display = XOpenDisplay(((void*)0));
    if (!g_x11_display) {
        return SDL_SetError("XOpenDisplay failed");
    }

    int screen = XDefaultScreen(g_x11_display);

    SDL_DisplayMode mode;
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_XRGB8888;
    mode.w = XDisplayWidth(g_x11_display, screen);
    mode.h = XDisplayHeight(g_x11_display, screen);
    if (mode.w <= 0) mode.w = 1920;
    if (mode.h <= 0) mode.h = 1080;
    if (SDL_AddBasicVideoDisplay(&mode) == 0) {
        return false;
    }

    g_x11_wm_delete = XInternAtom(g_x11_display, "WM_DELETE_WINDOW", 0);
    return true;
}

static void PRIVATE_VideoQuit(SDL_VideoDevice *_this)
{
    (void)_this;
    if (g_x11_display) {
        XCloseDisplay(g_x11_display);
        g_x11_display = ((void*)0);
    }
}

static bool PRIVATE_CreateSDLWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props)
{
    (void)_this; (void)create_props;
    SDL_WindowData *data = (SDL_WindowData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return false;
    }

    int screen = XDefaultScreen(g_x11_display);
    Window win = XCreateSimpleWindow(g_x11_display, XRootWindow(g_x11_display, screen),
        0, 0, (unsigned int)window->w, (unsigned int)window->h, 0,
        XBlackPixel(g_x11_display, screen), XBlackPixel(g_x11_display, screen));
    if (!win) {
        SDL_free(data);
        return SDL_SetError("XCreateSimpleWindow failed");
    }

    XStoreName(g_x11_display, win, window->title ? window->title : "");
    XSetWMProtocols(g_x11_display, win, &g_x11_wm_delete, 1);
    /* StructureNotifyMask alone (the original value here) means the X
     * server never sends this window ANY pointer events at all -- not
     * real physical clicks, not XTest-synthesized ones either, regardless
     * of anything SDL_PollEvent()'s caller does with them. This was the
     * actual root cause of SQW's anchors/buttons/scrollbars never
     * responding to real mouse input (confirmed by the fact that
     * SDL_PushEvent()-injected synthetic events, which skip X11 entirely,
     * always worked correctly -- see sqw_main.c's own SQW_TEST_CLICK_X/Y
     * comment). Button press/release + motion must be explicitly
     * requested per window, same as StructureNotifyMask already was. */
    XSelectInput(g_x11_display, win, SQ_StructureNotifyMask | SQ_ButtonPressMask | SQ_ButtonReleaseMask | SQ_PointerMotionMask | SQ_KeyPressMask | SQ_KeyReleaseMask);

    data->win = win;
    data->gc = XCreateGC(g_x11_display, win, 0, ((void*)0));
    data->w = window->w;
    data->h = window->h;
    window->internal = data;
    return true;
}

/* SQW (the SDL3+Vulkan browser subproject) needs the real X11 Display and
 * Window to build a VkSurfaceKHR via vkCreateXlibSurfaceKHR -- this custom PRIVATE_*
 * video driver has no Vulkan hookup of its own (software/XImage framebuffer
 * only), so SQW creates its own Vulkan instance/device/surface directly
 * against this window's real Display/Window instead, bypassing SDL3's own
 * Vulkan path entirely. Not part of any real SDL3 API -- an SQW-specific
 * accessor, the Linux sibling of SQW_GetWindowHWND in the Windows half of
 * this file. */
void *SQW_GetWindowX11Display(SDL_Window *window)
{
    (void)window;
    return (void *)g_x11_display;
}

unsigned long SQW_GetWindowX11Window(SDL_Window *window)
{
    if (!window || !window->internal) return 0;
    return (unsigned long)window->internal->win;
}

static void PRIVATE_SetWindowTitle(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    XStoreName(g_x11_display, window->internal->win, window->title ? window->title : "");
}

static void PRIVATE_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    XMapWindow(g_x11_display, window->internal->win);
    /* XSetInputFocus is deliberately NOT called here -- XMapWindow only
     * queues an asynchronous map REQUEST; the window isn't actually
     * viewable (mapped, with every ancestor also mapped -- XSetInputFocus's
     * own documented precondition) until the server (and, for a
     * reparenting WM, the WM itself) has actually processed it, which can
     * easily still be pending at this exact point. Calling XSetInputFocus
     * immediately after XMapWindow hits exactly that race and raises a
     * real, fatal-by-default X11 protocol error (confirmed directly:
     * "X Error ... BadMatch (invalid parameter attributes) ... Major
     * opcode ... X_SetInputFocus", killing the whole process before a
     * single frame ever rendered). See PRIVATE_PumpEvents' own comment
     * (SQ_ConfigureNotify handling) for where this call actually happens
     * instead, once the window is confirmed real and placed. */
    /* Real SDL_SendMouseMotion()/SDL_SendMouseButton() (src/events/
     * SDL_mouse.c, unmodified real SDL3 source -- never hand-patched, see
     * SDL3/SDL/CLAUDE.md's own "AI must not be used to generate code for
     * contributions to this project" notice) both gate on SDL_Mouse's own
     * internal "focus window" via SDL_UpdateMouseFocus(): a MotionNotify
     * or ButtonPress delivered before mouse->focus is ever established
     * needs SDL_MousePositionInWindow() to independently confirm the
     * pointer is inside the window before SetMouseFocus ever runs, and if
     * that first real event's coordinates don't satisfy it for any reason,
     * mouse->focus permanently stays NULL and every subsequent real
     * MotionNotify/ButtonPress silently no-ops forever (SDL_SendMouseButton
     * in particular reads its OWN event.button.x/y from mouse->x/y, which
     * only SDL_SendMouseMotion ever updates -- so a motion event that never
     * gets through means every later click event reports a stale/zero
     * position too, independent of whatever real position was clicked).
     * SDL_SendMouseWheel(), by contrast, calls SDL_SetMouseFocus()
     * unconditionally with no position check at all -- exactly why real
     * mouse-wheel scrolling kept working the whole time this bug existed
     * while real clicks and motion silently didn't. Establishing focus
     * here, once, unconditionally, the moment the window becomes visible
     * (matching what a real WM's own click-to-focus/focus-follows-mouse
     * would eventually do anyway) removes the dependency on that first
     * real event's coordinates ever satisfying SDL_MousePositionInWindow. */
    SDL_SetMouseFocus(window);
    XFlush(g_x11_display);
}

static void PRIVATE_HideWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    XUnmapWindow(g_x11_display, window->internal->win);
    XFlush(g_x11_display);
}

static void PRIVATE_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_WindowData *data = window->internal;
    if (!data) {
        return;
    }
    if (data->gc) XFreeGC(g_x11_display, data->gc);
    if (data->win) XDestroyWindow(g_x11_display, data->win);
    SDL_free(data);
    window->internal = ((void*)0);
}

static void PRIVATE_GetWindowSizeInPixels(SDL_VideoDevice *_this, SDL_Window *window, int *w, int *h)
{
    (void)_this;
    *w = window->internal->w;
    *h = window->internal->h;
}

static void PRIVATE_PumpEvents(SDL_VideoDevice *_this)
{
    (void)_this;
    if (!g_x11_display) {
        return;
    }
    while (XPending(g_x11_display) > 0) {
        XEvent ev;
        XNextEvent(g_x11_display, &ev);
        if (ev.type == SQ_ClientMessage && (unsigned long)ev.xclient.data.l[0] == g_x11_wm_delete) {
            /* SDL_Window lookup: userdata plumbing (GWLP_USERDATA's X11
             * equivalent) is out of this build's scope — with exactly one
             * window ever open at a time in every example this build
             * targets, broadcasting the close request to whichever window
             * SDL currently considers focused/only is equivalent and much
             * simpler. */
            SDL_Window *window = SDL_GetKeyboardFocus();
            if (!window) {
                window = _this->windows;
            }
            if (window) {
                SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
            }
        } else if (ev.type == SQ_ConfigureNotify) {
            SDL_Window *window = _this->windows;
            while (window) {
                if (window->internal && window->internal->win == ev.xconfigure.window) {
                    int w = ev.xconfigure.width;
                    int h = ev.xconfigure.height;
                    if (w > 0 && h > 0 && (w != window->internal->w || h != window->internal->h)) {
                        window->internal->w = w;
                        window->internal->h = h;
                        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, w, h);
                        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, w, h);
                    }
                    /* Real X11 keyboard input (KeyPress/KeyRelease) needs this
                     * window to actually hold the server's input focus -- see
                     * PRIVATE_ShowWindow's own comment for why that call was
                     * moved here instead of firing immediately after
                     * XMapWindow (a real, fatal BadMatch protocol error from
                     * calling XSetInputFocus before the window was actually
                     * viewable). A ConfigureNotify for this window is a
                     * reliable "it's real and placed now" signal (the window
                     * manager only reparents/positions/sizes a window it has
                     * actually mapped), so request focus once, the first
                     * time one arrives. */
                    if (!g_input_focus_requested) {
                        g_input_focus_requested = 1;
                        XSetInputFocus(g_x11_display, window->internal->win, SQ_RevertToParent, SQ_CurrentTime);
                    }
                    break;
                }
                window = window->next;
            }
        } else if (ev.type == SQ_MotionNotify) {
            SDL_Window *window = _this->windows;
            while (window) {
                if (window->internal && window->internal->win == ev.xmotion.window) {
                    SDL_SendMouseMotion(0, window, 0, false, (float)ev.xmotion.x, (float)ev.xmotion.y);
                    break;
                }
                window = window->next;
            }
        } else if (ev.type == SQ_ButtonPress || ev.type == SQ_ButtonRelease) {
            SDL_Window *window = _this->windows;
            while (window) {
                if (window->internal && window->internal->win == ev.xbutton.window) {
                    unsigned int xb = ev.xbutton.button;
                    if (xb == SQ_BUTTON_WHEEL_UP || xb == SQ_BUTTON_WHEEL_DOWN) {
                        /* X11 reports the wheel as a press+release pair on
                         * a synthetic button (see SQ_BUTTON_WHEEL_UP/DOWN's
                         * own comment) -- only fire once per physical
                         * notch, on the press half, same as real SDL3's
                         * own X11 backend. */
                        if (ev.type == SQ_ButtonPress) {
                            float wy = (xb == SQ_BUTTON_WHEEL_UP) ? 1.0f : -1.0f;
                            SDL_SendMouseWheel(0, window, 0, 0.0f, wy, SDL_MOUSEWHEEL_NORMAL);
                        }
                    } else {
                        SDL_SendMouseButton(0, window, 0, (Uint8)xb, ev.type == SQ_ButtonPress);
                    }
                    break;
                }
                window = window->next;
            }
        } else if (ev.type == SQ_KeyPress || ev.type == SQ_KeyRelease) {
            /* Only the handful of navigation keys SQW's own scrolling
             * needs (Page Up/Down, Home/End, arrow Up/Down) get translated
             * -- a full X11 keycode->keysym->SDL_Scancode table for every
             * key is real SDL3 X11 backend territory (XkbKeycodeToKeysym +
             * a ~256-entry table), well beyond what this minimal driver
             * needs. XLookupKeysym (not XkbKeycodeToKeysym) is used
             * deliberately: it needs no XkbDescPtr/XOpenDisplay Xkb setup,
             * just the raw XKeyEvent already in hand, and index 0 (no
             * Shift/group modifiers) is exactly right for these keys --
             * none of them have a distinct shifted form worth handling. */
            unsigned long ks = XLookupKeysym(&ev.xkey, 0);
            int scancode = -1;
            if      (ks == SQ_XK_Page_Up)   scancode = SDL_SCANCODE_PAGEUP;
            else if (ks == SQ_XK_Page_Down) scancode = SDL_SCANCODE_PAGEDOWN;
            else if (ks == SQ_XK_Home)      scancode = SDL_SCANCODE_HOME;
            else if (ks == SQ_XK_End)       scancode = SDL_SCANCODE_END;
            else if (ks == SQ_XK_Up)        scancode = SDL_SCANCODE_UP;
            else if (ks == SQ_XK_Down)      scancode = SDL_SCANCODE_DOWN;
            if (scancode >= 0) {
                SDL_SendKeyboardKey(0, 0, 0, (SDL_Scancode)scancode, ev.type == SQ_KeyPress);
            }
        }
    }
}

static bool PRIVATE_CreateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, SDL_PixelFormat *format, void **pixels, int *pitch)
{
    SDL_WindowData *data = window->internal;
    int w, h;
    PRIVATE_GetWindowSizeInPixels(_this, window, &w, &h);
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    *format = SDL_PIXELFORMAT_XRGB8888;
    *pitch = w * 4;
    *pixels = SDL_malloc((size_t)(*pitch) * (size_t)h);
    if (!*pixels) {
        return false;
    }
    (void)data;
    return true;
}

static bool PRIVATE_UpdateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    (void)_this;
    SDL_WindowData *data = window->internal;
    int screen = XDefaultScreen(g_x11_display);
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);

    /* SDL's software-renderer surface (window->surface) already holds the
     * up-to-date pixel buffer this call is asked to present — reuse it
     * directly (its ->pixels/->pitch) rather than re-deriving a pointer,
     * matching how the Win32 branch's CreateWindowFramebuffer/
     * UpdateWindowFramebuffer pair is invoked by SDL_windowsframebuffer.c's
     * real counterpart. */
    SDL_Surface *surface = SDL_GetWindowSurface(window);
    if (!surface) {
        return false;
    }
    XImage *img = XCreateImage(g_x11_display, XDefaultVisual(g_x11_display, screen),
        (unsigned int)XDefaultDepth(g_x11_display, screen), SQ_ZPixmap, 0,
        (char *)surface->pixels, (unsigned int)w, (unsigned int)h, 32, surface->pitch);
    if (!img) {
        return false;
    }
    int i;
    for (i = 0; i < numrects; i++) {
        XPutImage(g_x11_display, data->win, data->gc, img,
                  rects[i].x, rects[i].y, rects[i].x, rects[i].y,
                  (unsigned int)rects[i].w, (unsigned int)rects[i].h);
    }
    /* img->data points at surface->pixels, which SDL itself owns — clear it
     * before destroying so XDestroyImage's default destroy_image doesn't
     * free memory it doesn't own. */
    img->data = ((void*)0);
    XDestroyImage(img);
    XFlush(g_x11_display);
    return true;
}

static void PRIVATE_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this; (void)window;
    /* Nothing to release here: the pixel buffer handed back by
     * CreateWindowFramebuffer becomes window->surface->pixels, which SDL's
     * own SDL_DestroyWindowSurface() frees. */
}

static void PRIVATE_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *PRIVATE_CreateDevice(void)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(*device));
    if (!device) {
        return NULL;
    }

    device->VideoInit = PRIVATE_VideoInit;
    device->VideoQuit = PRIVATE_VideoQuit;
    device->PumpEvents = PRIVATE_PumpEvents;
    device->CreateSDLWindow = PRIVATE_CreateSDLWindow;
    device->SetWindowTitle = PRIVATE_SetWindowTitle;
    device->ShowWindow = PRIVATE_ShowWindow;
    device->HideWindow = PRIVATE_HideWindow;
    device->DestroyWindow = PRIVATE_DestroyWindow;
    device->GetWindowSizeInPixels = PRIVATE_GetWindowSizeInPixels;
    device->CreateWindowFramebuffer = PRIVATE_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = PRIVATE_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = PRIVATE_DestroyWindowFramebuffer;
    device->free = PRIVATE_DeleteDevice;
    return device;
}
#elif defined(__APPLE__)
#include <dlfcn.h>
/* macOS gets neither the X11 branch above nor the Win32 GDI branch below:
 * squash's macOS backend only links against /usr/lib/libSystem.B.dylib at
 * COMPILE time (see compiler.c's "-l is ignored for -macos" diagnostic and
 * macho_builder.c) -- it has no support for linking additional
 * dylibs/frameworks into the Mach-O import table. Real windowing instead
 * uses dlopen()/dlsym() (both real libSystem exports, already resolvable)
 * to load libobjc.A.dylib and AppKit.framework at RUNTIME, then drives
 * Cocoa purely through the Objective-C runtime's plain C entry points
 * (objc_msgSend, objc_getClass, sel_registerName) -- the well-known
 * "Cocoa without a .m file" technique. This needed one real squash
 * compiler fix first: objc_msgSend's calling convention passes structs
 * like NSRect (4 doubles, 32 bytes) BY VALUE, and squash's call codegen had
 * no support at all for struct-by-value arguments (confirmed via a minimal
 * repro: summing a 4-double struct's fields through a by-value parameter
 * returned a truncated wrong answer) -- see codegen.c's
 * classify_sysv_call_args/emit_sysv_struct_call_args for that fix. Verified
 * end-to-end against a REAL Apple-compiled function before writing any of
 * this: calling AppKit's NSStringFromRect(NSRect) via dlsym + a cast
 * function pointer, passing a real by-value NSRect, produced the exact
 * expected "{{1, 2}, {3, 4}}" string. */

typedef struct { double x, y; } NSPoint;
typedef struct { double width, height; } NSSize;
typedef struct { NSPoint origin; NSSize size; } NSRect;

#define NSWindowStyleMaskTitled          1
#define NSWindowStyleMaskClosable        2
#define NSWindowStyleMaskResizable       8
#define NSWindowStyleMaskMiniaturizable  4
#define NSBackingStoreBuffered           2
#define NSApplicationActivationPolicyRegular 0
/* -[NSApplication nextEventMatchingMask:...] takes NSUInteger(-1) ("any
 * event") as its mask -- NSEventMaskAny, defined as ULONG_MAX in
 * AppKit's own headers. */
#define SQ_NSEventMaskAny (~0UL)

static void *g_objc_handle;
static void *g_appkit_handle;
static void *g_objc_msgSend;
static void *g_objc_getClass_fn;
static void *g_sel_registerName_fn;
static void *g_ns_app;               /* [NSApplication sharedApplication] */
static void *g_ns_default_run_loop_mode; /* NSDefaultRunLoopMode (a global NSString* symbol) */

static void *sq_getClass(const char *name) {
    typedef void *(*fn)(const char*);
    return ((fn)g_objc_getClass_fn)(name);
}
static void *sq_sel(const char *name) {
    typedef void *(*fn)(const char*);
    return ((fn)g_sel_registerName_fn)(name);
}
static void *sq_send0(void *self, void *sel) {
    typedef void *(*fn)(void*,void*);
    return ((fn)g_objc_msgSend)(self, sel);
}
static void sq_send0_void(void *self, void *sel) {
    typedef void (*fn)(void*,void*);
    ((fn)g_objc_msgSend)(self, sel);
}
/* Real BOOL/char-sized returns aren't narrowed reliably through an
 * arbitrary function-pointer cast (a known general limitation, not
 * specific to Cocoa), so this always calls through a "long"-returning
 * prototype (a full 8-byte register, always correctly loaded) and masks
 * to the meaningful low byte itself in plain, ordinary C. */
static int sq_send0_bool(void *self, void *sel) {
    typedef long (*fn)(void*,void*);
    long r = ((fn)g_objc_msgSend)(self, sel);
    return (int)(r & 0xff);
}
static void *sq_send1(void *self, void *sel, void *a1) {
    typedef void *(*fn)(void*,void*,void*);
    return ((fn)g_objc_msgSend)(self, sel, a1);
}
static void sq_send1_void(void *self, void *sel, void *a1) {
    typedef void (*fn)(void*,void*,void*);
    ((fn)g_objc_msgSend)(self, sel, a1);
}
static void *sq_send1_long(void *self, void *sel, long a1) {
    typedef void *(*fn)(void*,void*,long);
    return ((fn)g_objc_msgSend)(self, sel, a1);
}
static void sq_send1_long_void(void *self, void *sel, long a1) {
    typedef void (*fn)(void*,void*,long);
    ((fn)g_objc_msgSend)(self, sel, a1);
}
/* NOTE: no sq_send_initrect()-style helper taking an NSRect PARAMETER here
 * on purpose -- squash's call-site struct-by-value fix (see codegen.c's
 * classify_sysv_call_args) only covers CALLERS passing a struct by value;
 * it does not extend to a squash-DEFINED function's own prologue
 * RECEIVING one as a parameter (the same class of gap as
 * struct_copy_size_of's pre-existing "structs on the left of an
 * assignment" scope, just for parameters instead). A prior version of
 * this driver had exactly such a helper and every window it created came
 * out zero-sized (confirmed via CGWindowListCopyWindowInfo showing a
 * real, correctly-titled, on-screen window with bounds (x,y,0,0) -- the
 * wrapper's own "r" parameter never received more than the first 8 bytes
 * of the real 32-byte NSRect the caller passed). Every NSRect-by-value
 * message send below instead builds the NSRect as a genuine LOCAL
 * variable and calls the cast objc_msgSend function pointer directly in
 * the SAME function that declares it -- the exact shape already verified
 * end-to-end against a real Apple-compiled function (NSStringFromRect). */
static void *sq_send_nextevent(void *self, void *sel, unsigned long mask, void *untilDate, void *mode, int dequeue) {
    typedef void *(*fn)(void*,void*,unsigned long,void*,void*,int);
    return ((fn)g_objc_msgSend)(self, sel, mask, untilDate, mode, dequeue);
}
static void *sq_nsstring(const char *utf8) {
    void *cls = sq_getClass("NSString");
    typedef void *(*fn)(void*,void*,const char*);
    return ((fn)g_objc_msgSend)(cls, sq_sel("stringWithUTF8String:"), utf8);
}

struct SDL_WindowData {
    void *nswindow;    /* NSWindow* */
    void *layer;        /* CALayer* backing the content NSView -- see PRIVATE_UpdateWindowFramebuffer */
    int w, h;
    int last_visible;  /* tracks isVisible across PumpEvents calls, to detect the close-button (see PRIVATE_PumpEvents) */
};

static bool PRIVATE_VideoInit(SDL_VideoDevice *_this)
{
    (void)_this;
    g_objc_handle = dlopen("/usr/lib/libobjc.A.dylib", RTLD_NOW);
    if (!g_objc_handle) {
        return SDL_SetError("dlopen libobjc.A.dylib failed");
    }
    g_appkit_handle = dlopen("/System/Library/Frameworks/AppKit.framework/AppKit", RTLD_NOW);
    if (!g_appkit_handle) {
        return SDL_SetError("dlopen AppKit.framework failed");
    }
    g_objc_msgSend = dlsym(g_objc_handle, "objc_msgSend");
    g_objc_getClass_fn = dlsym(g_objc_handle, "objc_getClass");
    g_sel_registerName_fn = dlsym(g_objc_handle, "sel_registerName");
    if (!g_objc_msgSend || !g_objc_getClass_fn || !g_sel_registerName_fn) {
        return SDL_SetError("dlsym of core objc runtime entry points failed");
    }
    void *mode_slot = dlsym(g_appkit_handle, "NSDefaultRunLoopMode");
    if (mode_slot) {
        g_ns_default_run_loop_mode = *(void **)mode_slot;
    }

    void *nsapp_cls = sq_getClass("NSApplication");
    g_ns_app = sq_send0(nsapp_cls, sq_sel("sharedApplication"));
    sq_send1_long_void(g_ns_app, sq_sel("setActivationPolicy:"), NSApplicationActivationPolicyRegular);
    sq_send0_void(g_ns_app, sq_sel("finishLaunching"));
    sq_send1_long_void(g_ns_app, sq_sel("activateIgnoringOtherApps:"), 1);

    SDL_DisplayMode mode;
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_XRGB8888;
    mode.w = 1920;
    mode.h = 1080;
    if (SDL_AddBasicVideoDisplay(&mode) == 0) {
        return false;
    }
    return true;
}

static void PRIVATE_VideoQuit(SDL_VideoDevice *_this)
{
    (void)_this;
}

static bool PRIVATE_CreateSDLWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props)
{
    (void)_this; (void)create_props;
    SDL_WindowData *data = (SDL_WindowData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return false;
    }

    NSRect rect;
    rect.origin.x = 0; rect.origin.y = 0;
    rect.size.width = (double)window->w;
    rect.size.height = (double)window->h;

    void *nswindow = sq_send0(sq_getClass("NSWindow"), sq_sel("alloc"));
    unsigned long style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
    /* NSRect passed by value straight through a cast function-pointer call
     * in THIS function, not via a separate helper that would take it as
     * its own parameter -- see the header comment above sq_send_nextevent
     * for why: a squash-defined function RECEIVING a struct-by-value
     * parameter is a real, separate gap from the (fixed) caller-side
     * struct-by-value-argument path, and building/passing the rect here
     * matches the exact shape already verified end-to-end against a real
     * Apple-compiled function (NSStringFromRect). */
    {
        typedef void *(*initrect_fn)(void*, void*, NSRect, unsigned long, unsigned long, int);
        nswindow = ((initrect_fn)g_objc_msgSend)(nswindow, sq_sel("initWithContentRect:styleMask:backing:defer:"),
                                                   rect, style, NSBackingStoreBuffered, 0);
    }
    if (!nswindow) {
        SDL_free(data);
        return SDL_SetError("NSWindow alloc/init failed");
    }

    void *view = sq_send0(sq_getClass("NSView"), sq_sel("alloc"));
    {
        typedef void *(*initframe_fn)(void*, void*, NSRect);
        view = ((initframe_fn)g_objc_msgSend)(view, sq_sel("initWithFrame:"), rect);
    }
    sq_send1_void(nswindow, sq_sel("setContentView:"), view);

    /* setWantsLayer:YES + -layer (rather than an NSImageView + NSImage) so
     * PRIVATE_UpdateWindowFramebuffer can hand the CALayer a CGImageRef
     * directly via -setContents: (a single pointer argument) -- avoiding
     * -[NSImage initWithCGImage:size:], which needs an NSSize (16 bytes) BY
     * VALUE. A struct that size needs SysV's per-eightbyte SSE/INTEGER
     * register classification, which this build's struct-by-value fix
     * deliberately does NOT implement (only the >16-byte "always MEMORY
     * class" case, all NSRect needs) -- see codegen.c's
     * classify_sysv_call_args comment for the full scope of that fix. */
    sq_send1_long_void(view, sq_sel("setWantsLayer:"), 1);
    void *layer = sq_send0(view, sq_sel("layer"));

    data->nswindow = nswindow;
    data->layer = layer;
    data->w = window->w;
    data->h = window->h;
    data->last_visible = 0;
    window->internal = data;

    if (window->title) {
        void *title = sq_nsstring(window->title);
        sq_send1_void(nswindow, sq_sel("setTitle:"), title);
    }
    return true;
}

static void PRIVATE_SetWindowTitle(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    void *title = sq_nsstring(window->title ? window->title : "");
    sq_send1_void(window->internal->nswindow, sq_sel("setTitle:"), title);
}

static void PRIVATE_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    sq_send1_void(window->internal->nswindow, sq_sel("makeKeyAndOrderFront:"), ((void*)0));
    window->internal->last_visible = 1;
}

static void PRIVATE_HideWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    sq_send1_void(window->internal->nswindow, sq_sel("orderOut:"), ((void*)0));
    window->internal->last_visible = 0;
}

static void PRIVATE_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_WindowData *data = window->internal;
    if (!data) {
        return;
    }
    if (data->nswindow) {
        sq_send0_void(data->nswindow, sq_sel("close"));
        sq_send0_void(data->nswindow, sq_sel("release"));
    }
    SDL_free(data);
    window->internal = ((void*)0);
}

static void PRIVATE_GetWindowSizeInPixels(SDL_VideoDevice *_this, SDL_Window *window, int *w, int *h)
{
    (void)_this;
    *w = window->internal->w;
    *h = window->internal->h;
}

static void PRIVATE_PumpEvents(SDL_VideoDevice *_this)
{
    if (!g_ns_app) {
        return;
    }
    void *distant_past = sq_send0(sq_getClass("NSDate"), sq_sel("distantPast"));
    for (;;) {
        void *event = sq_send_nextevent(g_ns_app, sq_sel("nextEventMatchingMask:untilDate:inMode:dequeue:"),
                                         SQ_NSEventMaskAny, distant_past, g_ns_default_run_loop_mode, 1);
        if (!event) {
            break;
        }
        sq_send1_void(g_ns_app, sq_sel("sendEvent:"), event);
    }

    /* No delegate/custom subclass is registered to intercept the title
     * bar's close button (see this driver's own header comment for why
     * objc_allocateClassPair-based custom classes are out of scope here),
     * so clicking it just runs NSWindow's own default "close" action,
     * which orders the window out (hides it) without terminating the app.
     * Poll isVisible each frame and synthesize the SDL close-request event
     * on the YES->NO transition -- equivalent in effect to the X11 driver's
     * WM_DELETE_WINDOW handling and the Win32 driver's WM_CLOSE handling
     * above. */
    SDL_Window *window = _this->windows;
    while (window) {
        SDL_WindowData *data = window->internal;
        if (data && data->nswindow) {
            int visible = sq_send0_bool(data->nswindow, sq_sel("isVisible"));
            if (data->last_visible && !visible) {
                SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
            }
            data->last_visible = visible;
        }
        window = window->next;
    }
}

static bool PRIVATE_CreateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, SDL_PixelFormat *format, void **pixels, int *pitch)
{
    int w, h;
    PRIVATE_GetWindowSizeInPixels(_this, window, &w, &h);
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    *format = SDL_PIXELFORMAT_XRGB8888;
    *pitch = w * 4;
    *pixels = SDL_malloc((size_t)(*pitch) * (size_t)h);
    if (!*pixels) {
        return false;
    }
    return true;
}

/* Real presentation: build a CGImage from the software surface's raw
 * XRGB8888 pixels (CoreGraphics/CGColorSpace/CGDataProvider are plain C
 * functions, dlopen'd from CoreGraphics.framework the same way AppKit was),
 * wrap it as an NSImage, and hand it to the content NSImageView --
 * NSImageView redraws itself automatically on setImage:, so this needs no
 * custom NSView subclass (which would need objc_allocateClassPair, out of
 * scope here -- see this driver's header comment). */
static void *g_coregraphics_handle;
static void *g_cs_device_rgb;
static void *(*g_CGColorSpaceCreateDeviceRGB)(void);
static void *(*g_CGDataProviderCreateWithData)(void*, const void*, unsigned long, void*);
static void *(*g_CGImageCreate)(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                                 void*, unsigned int, void*, void*, int, int);
static void (*g_CGDataProviderRelease)(void*);
static void (*g_CGImageRelease)(void*);

static void sq_ensure_coregraphics(void)
{
    if (g_coregraphics_handle) {
        return;
    }
    g_coregraphics_handle = dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_NOW);
    if (!g_coregraphics_handle) {
        return;
    }
    g_CGColorSpaceCreateDeviceRGB = (void *(*)(void))dlsym(g_coregraphics_handle, "CGColorSpaceCreateDeviceRGB");
    g_CGDataProviderCreateWithData = (void *(*)(void*,const void*,unsigned long,void*))dlsym(g_coregraphics_handle, "CGDataProviderCreateWithData");
    g_CGImageCreate = (void *(*)(unsigned long,unsigned long,unsigned long,unsigned long,unsigned long,void*,unsigned int,void*,void*,int,int))dlsym(g_coregraphics_handle, "CGImageCreate");
    g_CGDataProviderRelease = (void (*)(void*))dlsym(g_coregraphics_handle, "CGDataProviderRelease");
    g_CGImageRelease = (void (*)(void*))dlsym(g_coregraphics_handle, "CGImageRelease");
}

static bool PRIVATE_UpdateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    (void)rects; (void)numrects;
    sq_ensure_coregraphics();
    if (!g_coregraphics_handle || !g_CGColorSpaceCreateDeviceRGB || !g_CGDataProviderCreateWithData ||
        !g_CGImageCreate) {
        return true;
    }
    SDL_Surface *surface = SDL_GetWindowSurface(window);
    if (!surface) {
        return false;
    }
    int w, h;
    PRIVATE_GetWindowSizeInPixels(_this, window, &w, &h);

    if (!g_cs_device_rgb) {
        g_cs_device_rgb = g_CGColorSpaceCreateDeviceRGB();
    }
    /* kCGImageAlphaNoneSkipFirst(6) | kCGBitmapByteOrder32Little(0x2000):
     * matches XRGB8888's real in-memory byte layout (B,G,R,X on a
     * little-endian machine) exactly, same convention SDL_PIXELFORMAT_XRGB8888
     * already uses elsewhere in this build. */
    void *provider = g_CGDataProviderCreateWithData(((void*)0), surface->pixels,
                                                      (unsigned long)surface->pitch * (unsigned long)h, ((void*)0));
    if (!provider) {
        return false;
    }
    void *cgimage = g_CGImageCreate((unsigned long)w, (unsigned long)h, 8, 32, (unsigned long)surface->pitch,
                                     g_cs_device_rgb, 0x2000u | 6u, provider, ((void*)0), 0, 0);
    if (g_CGDataProviderRelease) {
        g_CGDataProviderRelease(provider);
    }
    if (!cgimage) {
        return false;
    }

    /* CALayer's -setContents: takes a single CGImageRef argument (a plain
     * pointer, id-toll-free-bridged) -- no struct-by-value argument at all,
     * which is exactly why PRIVATE_CreateSDLWindow set this content view up
     * as a plain layer-backed NSView instead of an NSImageView (whose
     * -[NSImage initWithCGImage:size:] would need an NSSize by value; see
     * that function's own comment). The layer retains its own reference to
     * the image, so releasing this call's reference right after is correct
     * ordinary Core Foundation ownership, not a use-after-free.
     *
     * Assigning -contents alone only QUEUES the change in the current
     * implicit CATransaction -- normally flushed automatically the next
     * time the real Cocoa run loop turns (AppKit ties this to its own
     * event-loop/display-link machinery), which our own hand-rolled
     * PumpEvents loop (a bare -nextEventMatchingMask:untilDate:inMode:dequeue:
     * poll, not the real -[NSApplication run]) never triggers on its own --
     * the window shows and activates (confirmed via lsappinfo registering
     * it as the frontmost real GUI app), but its content never actually
     * reaches the screen. -setDisableActions:YES skips the default
     * "contents" cross-fade (which would otherwise blend across several
     * queued frames instead of showing each one immediately), and
     * +[CATransaction flush] forces the queued change to composite RIGHT
     * NOW instead of waiting for a run-loop turn this driver never
     * naturally provides. */
    void *catx = sq_getClass("CATransaction");
    if (catx) {
        sq_send1_long_void(catx, sq_sel("setDisableActions:"), 1);
    }
    sq_send1_void(window->internal->layer, sq_sel("setContents:"), cgimage);
    if (catx) {
        sq_send0_void(catx, sq_sel("flush"));
    }
    if (g_CGImageRelease) {
        g_CGImageRelease(cgimage);
    }
    /* A real video backend's RenderPresent blocks until the next vsync,
     * which is what naturally paces every example's main loop -- none of
     * them call SDL_Delay themselves (see e.g. simple-playback.c's
     * SDL_AppIterate, which just streams audio then clears/presents).
     * This CALayer path has no real display link, so without this it
     * spins as fast as the CPU allows (confirmed: ~100% of a core,
     * continuously -- not a hang, just completely unthrottled). A plain
     * ~60fps sleep here is the same "just enough to not peg a core"
     * pacing already used elsewhere in this build (see SDL_SYS_DelayNS). */
    struct timespec sq_present_pace_ts;
    sq_present_pace_ts.tv_sec = 0;
    sq_present_pace_ts.tv_nsec = 16000000; /* ~16ms, ~60fps */
    nanosleep(&sq_present_pace_ts, ((void*)0));
    return true;
}

static void PRIVATE_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this; (void)window;
}

static void PRIVATE_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *PRIVATE_CreateDevice(void)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(*device));
    if (!device) {
        return ((void*)0);
    }
    device->VideoInit = PRIVATE_VideoInit;
    device->VideoQuit = PRIVATE_VideoQuit;
    device->PumpEvents = PRIVATE_PumpEvents;
    device->CreateSDLWindow = PRIVATE_CreateSDLWindow;
    device->SetWindowTitle = PRIVATE_SetWindowTitle;
    device->ShowWindow = PRIVATE_ShowWindow;
    device->HideWindow = PRIVATE_HideWindow;
    device->DestroyWindow = PRIVATE_DestroyWindow;
    device->GetWindowSizeInPixels = PRIVATE_GetWindowSizeInPixels;
    device->CreateWindowFramebuffer = PRIVATE_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = PRIVATE_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = PRIVATE_DestroyWindowFramebuffer;
    device->free = PRIVATE_DeleteDevice;
    return device;
}
#else
struct SDL_WindowData {
    HWND   hwnd;
    HDC    hdc;  /* window DC, valid for the window's lifetime (CS_OWNDC) */
    HDC    mdc;  /* memory DC selected with the framebuffer DIB section */
    HANDLE hbm;  /* the DIB section bitmap backing the framebuffer */
};

static const char *SQUASH_WNDCLASS_NAME = "SquashSDLWindowClass";

static LRESULT CALLBACK squash_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SDL_Window *window = (SDL_Window *)(size_t)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_CLOSE:
            if (window) {
                SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
            }
            return 0;
        case WM_DESTROY:
            return 0;
        case WM_SIZE: {
            if (window) {
                int w = (int)(lp & 0xFFFF);
                int h = (int)((lp >> 16) & 0xFFFF);
                if (w > 0 && h > 0) {
                    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, w, h);
                    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, w, h);
                }
            }
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static bool PRIVATE_VideoInit(SDL_VideoDevice *_this)
{
    SDL_DisplayMode mode;
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_XRGB8888;
    mode.w = GetSystemMetrics(SM_CXSCREEN);
    mode.h = GetSystemMetrics(SM_CYSCREEN);
    if (mode.w <= 0) mode.w = 1920;
    if (mode.h <= 0) mode.h = 1080;
    if (SDL_AddBasicVideoDisplay(&mode) == 0) {
        return false;
    }

    WNDCLASSEXA wc;
    SDL_zero(wc);
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = squash_wnd_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = SQUASH_WNDCLASS_NAME;
    RegisterClassExA(&wc); /* failure is harmless here (e.g. re-Init) */

    return true;
}

static void PRIVATE_VideoQuit(SDL_VideoDevice *_this)
{
    (void)_this;
}

static bool PRIVATE_CreateSDLWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props)
{
    (void)_this; (void)create_props;
    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT r;
    r.left = 0; r.top = 0; r.right = window->w; r.bottom = window->h;
    AdjustWindowRect(&r, style, FALSE);

    SDL_WindowData *data = (SDL_WindowData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return false;
    }

    HWND hwnd = CreateWindowExA(0, SQUASH_WNDCLASS_NAME, window->title ? window->title : "",
                                 style, CW_USEDEFAULT, CW_USEDEFAULT,
                                 r.right - r.left, r.bottom - r.top,
                                 NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hwnd) {
        SDL_free(data);
        return WIN_SetError("CreateWindowExA failed");
    }

    data->hwnd = hwnd;
    data->hdc = GetDC(hwnd);
    SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)(size_t)window);
    window->internal = data;
    return true;
}

/* SQW (the SDL3+Vulkan browser subproject) needs the real HWND to build a
 * VkSurfaceKHR via vkCreateWin32SurfaceKHR — this custom PRIVATE_* video
 * driver has no Vulkan hookup of its own (software/GDI framebuffer only),
 * so SQW creates its own Vulkan instance/device/surface directly against
 * this window's real HWND instead, bypassing SDL3's own Vulkan path
 * entirely. Not part of any real SDL3 API — an SQW-specific accessor. */
void *SQW_GetWindowHWND(SDL_Window *window)
{
    if (!window || !window->internal) return NULL;
    return (void *)window->internal->hwnd;
}

static void PRIVATE_SetWindowTitle(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SetWindowTextA(window->internal->hwnd, window->title ? window->title : "");
}

static void PRIVATE_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    ShowWindow(window->internal->hwnd, SW_SHOW);
    UpdateWindow(window->internal->hwnd);
}

static void PRIVATE_HideWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    ShowWindow(window->internal->hwnd, SW_HIDE);
}

static void PRIVATE_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_WindowData *data = window->internal;
    if (!data) {
        return;
    }
    if (data->mdc) DeleteDC(data->mdc);
    if (data->hbm) DeleteObject(data->hbm);
    if (data->hdc) ReleaseDC(data->hwnd, data->hdc);
    if (data->hwnd) DestroyWindow(data->hwnd);
    SDL_free(data);
    window->internal = NULL;
}

static void PRIVATE_GetWindowSizeInPixels(SDL_VideoDevice *_this, SDL_Window *window, int *w, int *h)
{
    (void)_this;
    RECT r;
    GetClientRect(window->internal->hwnd, &r);
    *w = r.right - r.left;
    *h = r.bottom - r.top;
}

static void PRIVATE_PumpEvents(SDL_VideoDevice *_this)
{
    (void)_this;
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

static bool PRIVATE_CreateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, SDL_PixelFormat *format, void **pixels, int *pitch)
{
    (void)_this;
    SDL_WindowData *data = window->internal;
    int w, h;
    PRIVATE_GetWindowSizeInPixels(_this, window, &w, &h);
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    if (data->mdc) { DeleteDC(data->mdc); data->mdc = NULL; }
    if (data->hbm) { DeleteObject(data->hbm); data->hbm = NULL; }

    BITMAPINFO bi;
    SDL_zero(bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; /* negative = top-down DIB, matches SDL's own row order */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    *format = SDL_PIXELFORMAT_XRGB8888;
    *pitch = w * 4;

    data->mdc = CreateCompatibleDC(data->hdc);
    data->hbm = CreateDIBSection(data->hdc, &bi, DIB_RGB_COLORS, pixels, NULL, 0);
    if (!data->hbm) {
        return WIN_SetError("CreateDIBSection failed");
    }
    SelectObject(data->mdc, data->hbm);
    return true;
}

static bool PRIVATE_UpdateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    (void)_this;
    SDL_WindowData *data = window->internal;
    int i;
    for (i = 0; i < numrects; i++) {
        BitBlt(data->hdc, rects[i].x, rects[i].y, rects[i].w, rects[i].h,
               data->mdc, rects[i].x, rects[i].y, SRCCOPY);
    }
    return true;
}

static void PRIVATE_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_WindowData *data = window->internal;
    if (!data) {
        return;
    }
    if (data->mdc) { DeleteDC(data->mdc); data->mdc = NULL; }
    if (data->hbm) { DeleteObject(data->hbm); data->hbm = NULL; }
}

static void PRIVATE_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *PRIVATE_CreateDevice(void)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(*device));
    if (!device) {
        return NULL;
    }

    device->VideoInit = PRIVATE_VideoInit;
    device->VideoQuit = PRIVATE_VideoQuit;
    device->PumpEvents = PRIVATE_PumpEvents;
    device->CreateSDLWindow = PRIVATE_CreateSDLWindow;
    device->SetWindowTitle = PRIVATE_SetWindowTitle;
    device->ShowWindow = PRIVATE_ShowWindow;
    device->HideWindow = PRIVATE_HideWindow;
    device->DestroyWindow = PRIVATE_DestroyWindow;
    device->GetWindowSizeInPixels = PRIVATE_GetWindowSizeInPixels;
    device->CreateWindowFramebuffer = PRIVATE_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = PRIVATE_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = PRIVATE_DestroyWindowFramebuffer;
    device->free = PRIVATE_DeleteDevice;
    return device;
}
#endif

/* NOT brace-initialized here (see squash_init_private_bootstrap(), called
 * first thing in main() below) — a global struct-with-function-pointer-field
 * brace initializer at this exact spot was observed (via a raw byte dump) to
 * read back as unrelated garbage after Pass 0.5's rewrite, still under
 * investigation as a genuine squash bug; assigning the fields as ordinary
 * runtime statements sidesteps it entirely and is just as correct. */
VideoBootStrap PRIVATE_bootstrap;

/* Not static: called from example_shim.inc / sdl_unity.c, which are
 * compiled as a SEPARATE translation unit from this file when linked
 * against squash_build/sdl_common.sqo (the cached-object build path) — a
 * static function has no visibility across that object-file boundary. */
void squash_init_private_bootstrap(void)
{
    PRIVATE_bootstrap.name = "private";
#if defined(__linux__)
    PRIVATE_bootstrap.desc = "squash custom minimal X11 video driver";
#elif defined(__APPLE__)
    PRIVATE_bootstrap.desc = "squash custom minimal offscreen video driver";
#else
    PRIVATE_bootstrap.desc = "squash custom minimal Windows video driver";
#endif
    PRIVATE_bootstrap.create = PRIVATE_CreateDevice;
    PRIVATE_bootstrap.ShowMessageBox = NULL;
    PRIVATE_bootstrap.is_preferred = false;

    PRIVATEAUDIO_bootstrap.name = "private";
    PRIVATEAUDIO_bootstrap.desc = "squash custom minimal Windows audio driver";
    PRIVATEAUDIO_bootstrap.init = PRIVATEAUDIO_Init;
    PRIVATEAUDIO_bootstrap.demand_only = false;
    PRIVATEAUDIO_bootstrap.is_preferred = false;
}

/* SDL_build_config_minimal.h (forced by SDL_build_config_private.h) turns on
 * SDL_VIDEO_DRIVER_DUMMY unconditionally, which adds "&DUMMY_bootstrap," to
 * SDL_video.c's own bootstrap[] array (src/video/SDL_video.c, ~line 146) —
 * but this build never includes video/dummy/SDL_nullvideo.c (the file that
 * actually DEFINES DUMMY_bootstrap), so that array element referenced a
 * global with no body anywhere in the translation unit. Undefining the
 * macro here (same per-file config-override pattern as SDL_FSOPS_WINDOWS
 * elsewhere in this file) removes that #ifdef'd entry entirely, leaving
 * bootstrap[] as the intended {&PRIVATE_bootstrap, NULL}. */
#undef SDL_VIDEO_DRIVER_DUMMY
#include "video/SDL_pixels.c"
#include "video/SDL_rect.c"
#include "video/SDL_fillrect.c"
#include "video/SDL_surface.c"
/* video/SDL_stretch.c: SDL_StretchSurface(), called from SDL_surface.c's own
 * SDL_BlitSurfaceScaled()/SDL_ConvertSurfaceAndColorspace() paths — a real,
 * pre-existing, platform-independent gap (not Windows- or Linux-specific),
 * simply never hit by any example exercised before "clear" (whose renderer
 * uses a scaled logical-presentation blit that reaches this). */
#include "video/SDL_stretch.c"
/* video/SDL_RLEaccel.c (RLE-accelerated colorkey blits) is NOT included:
 * its CHOOSE_BLIT/RLECLIPBLIT/RLEBLIT macro machinery (heavy nested
 * token-pasting generating per-bpp blitter functions) hits a real squash
 * preprocessor parse error ("expected ';' but got '('") — a genuine
 * squash limitation on this file's macro complexity, not something to work
 * around by editing real SDL3 source. RLE acceleration is a pure
 * performance optimization: SDL_RLESurface() returning "not accelerated"
 * (SDL_Unsupported()) just means every surface stays in its normal,
 * fully-correct unaccelerated blit path, and SDL_UnRLESurface() (revert an
 * RLE-encoded surface back to normal) has nothing to do if a surface was
 * never RLE-encoded in the first place — both called unconditionally from
 * SDL_pixels.c/SDL_surface.c with no build-config guard, so they need a
 * real (if trivial) body, same pattern as the other excluded-subsystem
 * stubs near main() below. */
bool SDL_RLESurface(SDL_Surface *surface)
{
    (void)surface;
    return SDL_Unsupported();
}
void SDL_UnRLESurface(SDL_Surface *surface)
{
    (void)surface;
}
/* video/SDL_blit.c: SDL_CalculateBlit(), the software-blit dispatcher
 * SDL_BlitSurface() needs — required for real rendering (the software
 * renderer's RenderPresent blits its target surface onto the window
 * surface). Its per-pixel-format-family accelerated paths
 * (SDL_CalculateBlit0/1/A/N, from SDL_blit_0.c/SDL_blit_1.c/SDL_blit_A.c/
 * SDL_blit_N.c) are each already gated behind their own
 * "#ifdef SDL_HAVE_BLIT_0/1/A/N" feature macro in this real, unmodified
 * file — left undefined here, so none of those files need to be built at
 * all. That matters because SDL_blit_1.c hits a real squash parser error
 * ("unexpected token 'case'", a switch/case shape squash's parser doesn't
 * accept) and SDL_blit_A.c hits a real squash codegen hang (compilation
 * simply never completes) when tried standalone — both are the same class
 * of "genuine squash limitation on generated-code complexity" as
 * SDL_RLEaccel.c above, not something to fix by editing real SDL3 source.
 * What's left after disabling those four (SDL_BlitCopy for identical
 * formats, SDL_Blit_Slow/SDL_Blit_Slow_Float for everything else, both
 * from SDL_blit_copy.c/SDL_blit_slow.c) is a complete, correct, just
 * unaccelerated blitter — sufficient for every example this build targets
 * (none of which need the accelerated per-format fast paths for
 * correctness, only for speed). */
#include "video/SDL_blit.c"
#include "video/SDL_blit_copy.c"
#include "video/SDL_blit_slow.c"
/* video/SDL_yuv.c: SDL_CalculateYUVSize()/YUV<->RGB conversion, called
 * unconditionally from SDL_surface.c's pixel-format-conversion path with no
 * build-config guard (real SDL3 has no "YUV disabled" configuration) — a
 * real, pre-existing, platform-independent gap, same rationale as
 * SDL_stretch.c/SDL_blit.c above. Compiles cleanly standalone (unlike
 * SDL_blit_1.c/SDL_blit_A.c), so included in full rather than stubbed. */
#include "video/SDL_yuv.c"
/* video/SDL_rotate.c: SDLgfx_rotozoomSurfaceSizeTrig(), called from
 * SDL_surface.c's SDL_ConvertSurfaceAndColorspace() sizing path and from
 * render/software/SDL_render_sw.c — same "real, pre-existing,
 * platform-independent gap" rationale as SDL_stretch.c/SDL_blit.c/
 * SDL_yuv.c above. Compiles cleanly standalone. */
#include "video/SDL_rotate.c"
/* video/SDL_bmp.c: SDL_IsBMP()/SDL_LoadBMP_IO()/SDL_SaveBMP_IO(), called
 * unconditionally from SDL_surface.c's SDL_LoadBMP()/SDL_SaveBMP() wrappers
 * — same "real, pre-existing, platform-independent gap" rationale as the
 * other video/*.c files added above. Compiles cleanly standalone. */
#include "video/SDL_bmp.c"
#include "video/SDL_video.c"

/* PNG/JPEG decode (SDL_LoadPNG/SDL_LoadJPG/SDL_SavePNG) via stb_image.h +
 * miniz.h (both real, unmodified upstream single-header libs bundled by
 * SDL3 under src/video/) — see squash_build's own scratch-tested repros
 * for the squash compiler bugs this took (constant-expression grammar
 * missing sizeof/==/?: for the "typedef char check[COND?1:-1];"
 * compile-time-assert idiom, array-typedef declarators never being parsed
 * at all, a cast inside a constant expression such as a FourCC-style
 * packed-integer case label, a brace initializer on a non-first
 * declarator in a comma-declaration list, and the comma operator not
 * being accepted in the parser's identifier-led statement fast path).
 * STB_IMAGE_STATIC keeps every stb_image.h symbol static (no export
 * clashes); SDL_stb.c itself provides the SDL_malloc/SDL_memcpy/etc
 * macro redirection stb_image.h and miniz.h both expect. */
#include "video/SDL_stb.c"

/* render/software/SDL_blendfillrect.c intentionally NOT included: its
 * per-pixel-format blend routines (RGB555/565/XRGB8888/ARGB8888/RGB/RGBA)
 * use render/software/SDL_draw.h's FILLRECT(type, op) macro, which
 * hand-rolls a Duff's-device switch(width & 3) unroll DIRECTLY (not via
 * the shared, separately-overridable DUFFS_LOOP4 macro) — and SDL_draw.h
 * has no #include guard at all, so a from-here macro override doesn't
 * stick: every consuming .c file's own "#include \"SDL_draw.h\"" re-runs
 * the plain #define and clobbers any earlier redefinition before FILLRECT
 * is actually used, a few lines later, in that SAME unmodifiable file (no
 * seam to inject a fix into, unlike DUFFS_LOOP's own file-local override
 * pattern used elsewhere in this build). squash's switch/case parser
 * requires "case" labels to be direct children of the switch block, not
 * interleaved inside a do-while body — confirmed via -dump: this is a real
 * general parser gap (case/default labels can only appear as statements
 * directly inside a switch's own top-level body, not nested inside a
 * do-while the way Duff's device relies on), flagged for a future session
 * as a genuine switch/case codegen rework (labels-anywhere semantics), same
 * class of gap already flagged for SDL_memset4's Duff's device (see
 * [[project-squash-sdl3-build]]). Since our own PRIVATE_CreateWindowFramebuffer
 * always produces SDL_PIXELFORMAT_XRGB8888 (see above), a hand-written,
 * XRGB8888-only SDL_BlendFillRect/SDL_BlendFillRects pair — reusing the
 * already-correct SDL_BlendMode math from SDL_blendfillrect.c's real
 * XRGB8888 case, just without the per-scanline unroll — covers every case
 * this build can actually reach. */
static bool squash_BlendFillRect_XRGB8888(SDL_Surface *dst, const SDL_Rect *rect,
                                           SDL_BlendMode blendMode, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    int x, y;
    int pitch = dst->pitch / 4;
    Uint32 *base = (Uint32 *)dst->pixels + rect->y * pitch + rect->x;
    unsigned inva = 0xff - a;

    for (y = 0; y < rect->h; y++) {
        Uint32 *pixels = base + y * pitch;
        for (x = 0; x < rect->w; x++) {
            unsigned dr = (pixels[x] >> 16) & 0xFF;
            unsigned dg = (pixels[x] >> 8) & 0xFF;
            unsigned db = pixels[x] & 0xFF;
            unsigned sr = r, sg = g, sb = b;
            switch (blendMode) {
                case SDL_BLENDMODE_BLEND:
                    sr = ((inva * dr) / 255) + r;
                    sg = ((inva * dg) / 255) + g;
                    sb = ((inva * db) / 255) + b;
                    break;
                case SDL_BLENDMODE_ADD:
                    sr = dr + r; if (sr > 0xff) sr = 0xff;
                    sg = dg + g; if (sg > 0xff) sg = 0xff;
                    sb = db + b; if (sb > 0xff) sb = 0xff;
                    break;
                case SDL_BLENDMODE_MOD:
                    sr = (dr * r) / 255;
                    sg = (dg * g) / 255;
                    sb = (db * b) / 255;
                    break;
                case SDL_BLENDMODE_MUL:
                    sr = ((r * dr) + (dr * (255 - a))) / 255; if (sr > 0xff) sr = 0xff;
                    sg = ((g * dg) + (dg * (255 - a))) / 255; if (sg > 0xff) sg = 0xff;
                    sb = ((b * db) + (db * (255 - a))) / 255; if (sb > 0xff) sb = 0xff;
                    break;
                default:
                    break;
            }
            pixels[x] = (0xFFu << 24) | (sr << 16) | (sg << 8) | sb;
        }
    }
    return true;
}

bool SDL_BlendFillRect(SDL_Surface *dst, const SDL_Rect *rect, SDL_BlendMode blendMode, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_Rect clipped;
    if (!dst || !rect) {
        return SDL_InvalidParamError("dst/rect");
    }
    if (!SDL_GetRectIntersection(rect, &dst->clip_rect, &clipped)) {
        return true;
    }
    return squash_BlendFillRect_XRGB8888(dst, &clipped, blendMode, r, g, b, a);
}

bool SDL_BlendFillRects(SDL_Surface *dst, const SDL_Rect *rects, int count, SDL_BlendMode blendMode, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    int i;
    if (!dst || !rects) {
        return SDL_InvalidParamError("dst/rects");
    }
    for (i = 0; i < count; i++) {
        if (!SDL_BlendFillRect(dst, &rects[i], blendMode, r, g, b, a)) {
            return false;
        }
    }
    return true;
}

#include "render/software/SDL_blendline.c"
#include "render/software/SDL_blendpoint.c"
#include "render/software/SDL_drawline.c"
#include "render/software/SDL_drawpoint.c"
#include "render/software/SDL_triangle.c"
#include "render/software/SDL_render_sw.c"
#include "render/SDL_render.c"

/* SDL_internal.h (included by every SDL .c file above) pulls in
 * <SDL3/SDL_main.h>, which "#define main SDL_main" so a real app's main()
 * gets renamed and wrapped by SDL's own platform-specific launcher. We
 * don't build that launcher here (it's not part of this subset), so
 * without this #undef our own main() below would silently become
 * "SDL_main" in the compiled output — squash's entry-point RVA is found
 * by name-searching for a function literally called "main", so it would
 * fail to find one and default to offset 0 (the very start of .text),
 * making the OS execute whatever unrelated function happens to be first
 * instead of main() at all. */
#undef main

/* Stubs for subsystems (video/audio/render/tray/notifications) that are
 * intentionally excluded from this reduced unity build (see the file
 * header comment) but whose cleanup functions SDL_Quit() unconditionally
 * calls regardless of which subsystems were actually initialized. These
 * are genuine SDL3 functions with no real body reachable in this build —
 * not a squash bug — so no-op stubs matching their real signatures let
 * SDL_Quit() complete instead of calling into a bogus fallback import. */
void SDL_HelperWindowDestroy(void) {}
void SDL_CleanupTrays(void) {}
void SDL_CleanupNotifications(void) {}
/* SDL_QuitSubSystem() is written so every subsystem's quit function is
 * safely callable even if that subsystem was never initialized — when
 * called from SDL_Quit() itself (SDL_bInMainQuit==true),
 * SDL_ShouldQuitSubsystem() unconditionally returns true for every
 * subsystem regardless of its real refcount, specifically so a program
 * that never touched video/audio/render still gets a clean full
 * shutdown. Since this build excludes the real render/gpu subsystems
 * entirely (see file header comment), their quit functions are genuinely
 * undefined here — same "no real body reachable in this build" situation
 * as SDL_HelperWindowDestroy above, not a squash bug.
 * SDL_QuitAudio() is no longer stubbed here: audio/SDL_audio.c (now
 * included, see the PRIVATEAUDIO_* backend above) provides the real
 * definition.
 * SDL_AssertionsQuit() and SDL_QuitCPUInfo() are no longer stubbed here:
 * SDL_assert.c and SDL_cpuinfo.c (both included above) now provide the
 * real definitions. SDL_VideoQuit()/SDL_QuitRender()/SDL_QuitPixelFormatDetails()
 * are likewise no longer stubbed: video/SDL_video.c, render/SDL_render.c, and
 * video/SDL_pixels.c (all now included, see the PRIVATE_* video backend below)
 * provide the real definitions. */
/* SDL_TriggerBreakpoint is normally a macro (GCC inline-asm/MSVC intrinsic/
 * raise(SIGTRAP)), never a real function, on every platform SDL3 itself
 * supports — our target platform hits SDL_assert.h's final "intentionally
 * left undefined" case, so any reference to it needs a real body. It's
 * reachable transitively (assert-failure paths) but never actually
 * exercised by this demo, so a no-op is correct. */
void SDL_TriggerBreakpoint(void) {}
/* SDL_HasMainCallbacks() is defined in main/SDL_main_callbacks.c, which
 * isn't part of this build (its whole purpose is integrating with SDL's
 * own platform-specific main()/WinMain() launcher via SDL_main.h, which
 * this build deliberately bypasses — see the "#undef main" note above and
 * this build's own hand-rolled callback-loop test in main() further down).
 * video/SDL_video.c's SDL_OnWindowLiveResizeUpdate() checks it during
 * interactive window-resize dragging; returning false makes it take the
 * plain "send an expose event" branch instead, which is exactly correct
 * for a program that isn't using SDL's own callback-registration API. */
bool SDL_HasMainCallbacks(void) { return false; }
/* Never actually called (guarded by SDL_HasMainCallbacks() returning false
 * above), but still needs a real body: squash's codegen requires a
 * same-translation-unit function reference to resolve to an actual label at
 * link time, since it has no way to tell "declared but never defined,
 * genuinely external" (tolerated as a soft warning) apart from "declared,
 * no body in this TU, but not obviously an external DLL import either"
 * (a hard asm_resolve error) for a plain extern C function like this one. */
SDL_AppResult SDL_IterateMainCallbacks(bool pump_events) { (void)pump_events; return SDL_APP_CONTINUE; }

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif
/* Raw debug marker — bypasses C stdio buffering entirely, so it survives a
 * crash (unlike printf, which is fully-buffered once redirected and loses
 * everything on a non-graceful exit). See [[feedback-squash-debug-prints]].
 * Win32 side uses GetStdHandle+WriteFile (real KERNEL32 imports on that
 * target, see symtable.c); Linux/macOS use the write(2) syscall directly —
 * both were always meant to stay unconditional/no-op-free, but this file's
 * original Win32-only version was left uncalled behind __linux__-gated
 * subsystems everywhere else and never got its own platform split. */
static void dbgmark(const char *s)
{
#if defined(__linux__) || defined(__APPLE__)
    write(1, s, SDL_strlen(s));
    write(1, "\n", 1);
#else
    HANDLE h = GetStdHandle((DWORD)-11);
    DWORD written;
    WriteFile(h, s, (DWORD)SDL_strlen(s), &written, NULL);
    WriteFile(h, "\n", 1, &written, NULL);
#endif
}

/* Debug-only: print "<label>=0x<16 hex digits>" via the same raw-WriteFile
 * technique as dbgmark() (see [[feedback-squash-debug-prints]]) — used to
 * compare a pointer's actual runtime value against an expected address
 * while chasing the still-open SDL_VideoInit() bootstrap[] crash. */
static void dbgmark_ptr(const char *label, unsigned long long v)
{
    char buf[64];
    int p = 0;
    const char *lbl = label;
    while (*lbl) buf[p++] = *lbl++;
    buf[p++] = '=';
    buf[p++] = '0';
    buf[p++] = 'x';
    for (int shift = 60; shift >= 0; shift -= 4) {
        int nib = (int)((v >> shift) & 0xf);
        buf[p++] = (nib < 10) ? ('0' + nib) : ('a' + (nib - 10));
    }
    buf[p] = 0;
    dbgmark(buf);
}

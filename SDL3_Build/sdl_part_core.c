/* Part 1/3 of the split SDL3 build (see sdl_shared_defs.inc's own comment):
 * core/stdlib/atomic/cpuinfo/libm/thread/timer/io/storage/power/filesystem/
 * locale/misc/notification/process/dialog/tray/sensor/haptic/camera/joystick
 * + events. Compiled once via "squash -c" to sdl_part_core.sqo and linked
 * against by sdl_part_audio.sqo/sdl_part_video.sqo's sibling parts plus
 * every driver (sdl_unity.c, examples_gen/*.c) -- see Makefile.SDL3. GENERATED
 * by splitting sdl_core.inc verbatim; do not hand-edit the #include list
 * without updating sdl_core.inc (kept as the single from-source reference
 * build, still used directly by png_load_test.c/png_roundtrip_test.c) to
 * match. */
#include "sdl_shared_defs.inc"

/* SDL_TriggerBreakpoint's real no-op definition lives in sdl_part_video.c
 * (see its own identical comment) -- some path reachable from SDL_assert.c
 * below references it by plain name with no prototype in scope, which in
 * the original single-TU build resolved directly (same-TU symbol lookup)
 * but now needs an explicit bodyless "extern" declaration to trigger
 * squash's cross-object RELOC_STATIC_REL32 resolution instead of being
 * silently guessed as a msvcrt.dll import. */
extern void SDL_TriggerBreakpoint(void);

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
#include "SDL_log.c"
#include "SDL_properties.c"
#include "SDL_utils.c"

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
 * optimization, not an observable semantic difference). */
void *SDL_memset4(void *dst, Uint32 val, size_t dwords)
{
    Uint32 *p = (Uint32 *)dst;
    while (dwords--) {
        *p++ = val;
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

#include "thread/SDL_thread.c"
#include "thread/generic/SDL_syscond.c"
#include "thread/generic/SDL_sysmutex.c"
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
#include "thread/generic/SDL_systls.c"

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

#include "timer/SDL_timer.c"
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

#include "loadso/dummy/SDL_sysloadso.c"

#include "locale/SDL_locale.c"
#include "locale/dummy/SDL_syslocale.c"

#include "misc/SDL_url.c"
#include "misc/dummy/SDL_sysurl.c"

#include "notification/SDL_notification.c"
#include "notification/dummy/SDL_dummynotification.c"

/* process/windows/SDL_windowsprocess.c: the real (non-dummy) CreateProcessW-
 * based backend — real subprocess spawning with pipe I/O. Needed a fair
 * amount of new Win32 surface in include/windows.h (STARTUPINFOW/
 * PROCESS_INFORMATION/SECURITY_ATTRIBUTES structs, CreateProcessW/
 * CreatePipe/DuplicateHandle/SetHandleInformation/SetNamedPipeHandleState/
 * GetExitCodeProcess/EnumWindows/PostMessage/PostThreadMessage/
 * GenerateConsoleCtrlEvent/GetWindowThreadProcessId declarations) since
 * squash's tiny windows.h shim has none of this. */
#include "process/SDL_process.c"
#define SDL_PROCESS_WINDOWS 1
#include "process/windows/SDL_windowsprocess.c"

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

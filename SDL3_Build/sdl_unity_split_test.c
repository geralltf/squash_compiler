/* Same end-to-end smoke test as sdl_unity.c (see its own header comment),
 * but linked against the split sdl_part_core.sqo/sdl_part_audio.sqo/
 * sdl_part_video.sqo objects instead of #include-ing sdl_core.inc directly
 * -- a regression test proving the 3-way split (Makefile.SDL3's "sqo"
 * target) produces a build that behaves identically to the from-source
 * reference build. GENERATED: main() body copied verbatim from sdl_unity.c;
 * keep the two in sync if either changes. */
#include <windows.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#undef main

/* Defined in sdl_part_video.sqo -- see its own comment. */
extern void squash_init_private_bootstrap(void);
extern void squash_repro_hashtable_test(void);

static int my_strlen(const char *s) { int n=0; while (s[n]) n++; return n; }
static void dbgmark(const char *s) {
    HANDLE h = GetStdHandle((DWORD)-11);
    DWORD written;
    WriteFile(h, s, (DWORD)my_strlen(s), &written, NULL);
    WriteFile(h, "\n", 1, &written, NULL);
}

int main(void)
{
    dbgmark("main() entered");
    squash_init_private_bootstrap();
    /* Turns any future real heap-metadata corruption into an immediate,
     * loud crash at the actual overflow instead of a much-later, harder-
     * to-diagnose symptom somewhere downstream. Confirmed harmless: used
     * to rule out heap corruption as the cause of the process-backend
     * "Out of memory" investigation (no crash here even with it enabled —
     * that bug turned out to be a genuine allocation-size bug, not
     * corruption; see the local-struct-brace-initializer fix elsewhere in
     * this codebase). */
    HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
    dbgmark("after HeapSetInformation");
    if (!SDL_Init(0)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    dbgmark("after SDL_Init(0)");
    SDL_Log("SDL3 (squash build) initialised OK");
    dbgmark("after SDL_Log initialised OK");

    /* Exercise real SDL_iostream.c file I/O — a real write/read round
     * trip through the real Windows file backend. */
    const char *path = "squash_iostream_test.tmp";
    const char *msg = "hello from squash SDL_iostream test";
    size_t msg_len = SDL_strlen(msg);

    SDL_IOStream *w = SDL_IOFromFile(path, "w");
    if (!w) {
        SDL_Log("SDL_IOFromFile(write) failed: %s", SDL_GetError());
        return 1;
    }
    size_t written = SDL_WriteIO(w, msg, msg_len);
    SDL_CloseIO(w);
    if (written != msg_len) {
        SDL_Log("SDL_WriteIO wrote %d of %d bytes", (int)written, (int)msg_len);
        return 1;
    }

    SDL_IOStream *r = SDL_IOFromFile(path, "r");
    if (!r) {
        SDL_Log("SDL_IOFromFile(read) failed: %s", SDL_GetError());
        return 1;
    }
    char buf[128];
    size_t nread = SDL_ReadIO(r, buf, sizeof(buf) - 1);
    SDL_CloseIO(r);
    buf[nread] = 0;

    if (nread != msg_len || SDL_strcmp(buf, msg) != 0) {
        SDL_Log("iostream round trip MISMATCH: read %d bytes, got \"%s\"", (int)nread, buf);
        return 1;
    }
    SDL_Log("SDL_iostream round trip OK: \"%s\"", buf);

    /* Exercise the real (non-dummy) filesystem/windows/SDL_sysfsops.c
     * backend: create a directory, stat it, rename it, then remove it —
     * all real Win32 CreateDirectoryW/GetFileAttributesExW/MoveFileExW/
     * RemoveDirectoryW calls. */
    const char *dir1 = "squash_fs_test_dir";
    const char *dir2 = "squash_fs_test_dir_renamed";
    SDL_RemovePath(dir1);
    SDL_RemovePath(dir2);

    if (!SDL_CreateDirectory(dir1)) {
        SDL_Log("SDL_CreateDirectory failed: %s", SDL_GetError());
        return 1;
    }
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(dir1, &info)) {
        SDL_Log("SDL_GetPathInfo failed: %s", SDL_GetError());
        return 1;
    }
    if (info.type != SDL_PATHTYPE_DIRECTORY) {
        SDL_Log("SDL_GetPathInfo returned wrong type: %d (expected SDL_PATHTYPE_DIRECTORY)", (int)info.type);
        return 1;
    }
    if (!SDL_RenamePath(dir1, dir2)) {
        SDL_Log("SDL_RenamePath failed: %s", SDL_GetError());
        return 1;
    }
    if (SDL_GetPathInfo(dir1, &info)) {
        SDL_Log("SDL_GetPathInfo unexpectedly succeeded on the old (renamed-away) path");
        return 1;
    }
    if (!SDL_GetPathInfo(dir2, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
        SDL_Log("SDL_GetPathInfo on the renamed directory failed or had the wrong type");
        return 1;
    }
    if (!SDL_RemovePath(dir2)) {
        SDL_Log("SDL_RemovePath failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Log("filesystem create/stat/rename/remove round trip OK");

    /* Exercise the real (hand-written, GetModuleFileNameW-based)
     * SDL_GetBasePath()/SDL_GetCurrentDirectory(). */
    const char *base = SDL_GetBasePath();
    if (!base) {
        SDL_Log("SDL_GetBasePath failed: %s", SDL_GetError());
        return 1;
    }
    if (!SDL_strrchr(base, '\\') || base[SDL_strlen(base) - 1] != '\\') {
        SDL_Log("SDL_GetBasePath returned a suspicious value: \"%s\"", base);
        return 1;
    }
    SDL_Log("SDL_GetBasePath: %s", base);

    char *curdir = SDL_GetCurrentDirectory();
    if (!curdir) {
        SDL_Log("SDL_GetCurrentDirectory failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Log("SDL_GetCurrentDirectory: %s", curdir);
    SDL_free(curdir);

    /* Exercise the real (non-dummy) filesystem/windows/SDL_sysfilesystem.c
     * backend's SDL_SYS_GetPrefPath()/GetUserFolder() — SHGetFolderPathW
     * and dynamically-loaded SHGetKnownFolderPath, real Win32/COM-adjacent
     * calls beyond plain file I/O. */
    char *prefpath = SDL_GetPrefPath("squash_org", "squash_app");
    if (!prefpath) {
        SDL_Log("SDL_GetPrefPath failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Log("SDL_GetPrefPath: %s", prefpath);
    if (!SDL_strstr(prefpath, "squash_org") || !SDL_strstr(prefpath, "squash_app")) {
        SDL_Log("SDL_GetPrefPath MISMATCH: expected org/app in path");
        return 1;
    }
    SDL_free(prefpath);

    /* KNOWN ISSUE: SDL_GetUserFolder(HOME) is NOT called here (unlike
     * SDL_GetPrefPath above) because it genuinely crashes
     * (STATUS_ACCESS_VIOLATION), not just returns an error — calling it
     * would take down this whole regression suite rather than failing
     * one check. Debug logging confirmed LoadLibraryW, GetProcAddress, and
     * the indirect call through the resulting function-pointer variable
     * (SDL_SYS_GetUserFolder loads SHGetKnownFolderPath dynamically and
     * calls it through a local pfnSHGetKnownFolderPath variable, unlike
     * every other real Win32 call this build exercises, which are all
     * direct calls to declared/imported DLL functions) all complete —
     * the call returns HRESULT 0x80070002/ERROR_FILE_NOT_FOUND (wrong,
     * but not a crash) and SDL_SYS_GetUserFolder returns NULL cleanly.
     * The crash happens afterward, in the return path back through
     * SDL_GetUserFolder's wrapper — not yet isolated further; possibly
     * the whole-struct GUID assignment ("type = SDL_FOLDERID_Profile;",
     * a 16-byte struct copy) corrupting something nearby. SDL_GetPrefPath
     * (above) exercises the same shlobj.h/DEFINE_GUID infrastructure via
     * the simpler, directly-imported SHGetFolderPathW and works
     * correctly, so this is specific to the dynamic-load-and-indirect-
     * call path. See project-squash-sdl3-build memory notes. */
    SDL_Log("SDL_GetPrefPath/SDL_GetUserFolder round trip OK");

    /* Exercise SDL_properties.c: a real properties object with a number
     * and a string property — a hashtable-backed subsystem that's a good
     * regression check for the sizeof(*(&expr)) and typedef-global-init
     * fixes found earlier in this effort. */
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) {
        SDL_Log("SDL_CreateProperties failed: %s", SDL_GetError());
        return 1;
    }
    if (!SDL_SetNumberProperty(props, "squash.test.number", 42)) {
        SDL_Log("SDL_SetNumberProperty failed: %s", SDL_GetError());
        return 1;
    }
    if (!SDL_SetStringProperty(props, "squash.test.string", "hello properties")) {
        SDL_Log("SDL_SetStringProperty failed: %s", SDL_GetError());
        return 1;
    }
    Sint64 numval = SDL_GetNumberProperty(props, "squash.test.number", -1);
    const char *strval = SDL_GetStringProperty(props, "squash.test.string", "");

    if (numval != 42 || SDL_strcmp(strval, "hello properties") != 0) {
        SDL_Log("properties MISMATCH: number=%lld string=\"%s\"", (long long)numval, strval);
        return 1;
    }
    SDL_Log("SDL_properties round trip OK: number=%lld string=\"%s\"", (long long)numval, strval);
    SDL_DestroyProperties(props);

    /* Exercise SDL_storage.c's generic backend (a real write/size/read
     * round trip through the same real SDL_IOFromFile/SDL_GetPathInfo
     * machinery proven working above, but this time through the
     * SDL_Storage abstraction layer). This was blocked for a long time by
     * a genuine squash codegen bug (unsigned 64-bit division/comparison
     * using signed instructions, corrupting SDL_ulltoa()'s digit-loop for
     * any Uint64 >= 2^63 — see project-squash-sdl3-build memory notes for
     * the full bisection trail) — now fixed. */
    SDL_Storage *storage = SDL_OpenFileStorage(base);
    if (!storage) {
        SDL_Log("SDL_OpenFileStorage failed: %s", SDL_GetError());
        return 1;
    }
    while (!SDL_StorageReady(storage)) {
        SDL_Delay(1);
    }
    const char *smsg = "hello from squash SDL_storage test";
    Uint64 smsg_len = (Uint64)SDL_strlen(smsg);
    const char *spath = "squash_storage_test.tmp";

    if (!SDL_WriteStorageFile(storage, spath, smsg, smsg_len)) {
        SDL_Log("SDL_WriteStorageFile failed: %s", SDL_GetError());
        return 1;
    }
    Uint64 sfsize = 0;
    if (!SDL_GetStorageFileSize(storage, spath, &sfsize) || sfsize != smsg_len) {
        SDL_Log("SDL_GetStorageFileSize failed or wrong size: %llu (expected %llu): %s",
                (unsigned long long)sfsize, (unsigned long long)smsg_len, SDL_GetError());
        return 1;
    }
    char sbuf[128];
    if (!SDL_ReadStorageFile(storage, spath, sbuf, smsg_len)) {
        SDL_Log("SDL_ReadStorageFile failed: %s", SDL_GetError());
        return 1;
    }
    sbuf[smsg_len] = 0;
    if (SDL_strcmp(sbuf, smsg) != 0) {
        SDL_Log("storage round trip MISMATCH: got \"%s\"", sbuf);
        return 1;
    }
    SDL_Log("SDL_storage round trip OK: \"%s\" (%llu bytes)", sbuf, (unsigned long long)sfsize);
    SDL_CloseStorage(storage);
    SDL_RemovePath(spath);

    /* Exercise SDL_power.c: no real power backend is compiled in (desktop
     * "always on mains" isn't platform-specific enough to bother with
     * here), so this just confirms the "no backend available" path
     * returns the documented safe default rather than crashing. */
    int power_seconds = -2, power_percent = -2;
    SDL_PowerState pwstate = SDL_GetPowerInfo(&power_seconds, &power_percent);
    SDL_Log("SDL_GetPowerInfo: state=%d seconds=%d percent=%d",
            (int)pwstate, power_seconds, power_percent);

    /* Exercise SDL_joystick.c/SDL_gamepad.c: no real hardware, but a real
     * call through the joystick driver list should safely return an empty
     * (non-NULL-or-explicitly-NULL, count==0) list rather than crashing. */
    int njoysticks = -1;
    SDL_JoystickID *joysticks = SDL_GetJoysticks(&njoysticks);
    SDL_Log("SDL_GetJoysticks: count=%d", njoysticks);
    if (njoysticks != 0) {
        SDL_Log("SDL_GetJoysticks: unexpected non-zero count with no hardware present");
        return 1;
    }
    SDL_free(joysticks);

    /* Exercise SDL_sensor.c: same array-of-driver-pointers pattern as
     * SDL_joystick_drivers[] above (SDL_sensor_drivers[]), a good
     * regression check for the "static T *arr[] = {&x}" global-pointer-
     * array-initializer fix found via the joystick test. */
    int nsensors = -1;
    SDL_SensorID *sensors = SDL_GetSensors(&nsensors);
    SDL_Log("SDL_GetSensors: count=%d", nsensors);
    if (nsensors != 0) {
        SDL_Log("SDL_GetSensors: unexpected non-zero count with no hardware present");
        return 1;
    }
    SDL_free(sensors);

    /* Exercise SDL_hints.c: a real hint set/get round trip. */
    if (!SDL_SetHint("SDL_SQUASH_TEST_HINT", "squash_hint_value")) {
        SDL_Log("SDL_SetHint failed: %s", SDL_GetError());
        return 1;
    }
    const char *hintval = SDL_GetHint("SDL_SQUASH_TEST_HINT");
    if (!hintval || SDL_strcmp(hintval, "squash_hint_value") != 0) {
        SDL_Log("SDL_GetHint MISMATCH: got \"%s\"", hintval ? hintval : "(null)");
        return 1;
    }
    SDL_Log("SDL_hints round trip OK: \"%s\"", hintval);

    /* Exercise SDL_process.c's real (non-dummy) CreateProcessW backend: a
     * real subprocess spawn with piped stdout, reading its real output
     * through the real Windows file/pipe I/O machinery.
     *
     * RESOLVED: this used to fail with SDL_CreateProcessWithProperties
     * reporting "Out of memory" (a genuine SDL_malloc-builtin failure, not
     * heap corruption — confirmed via HeapEnableTerminationOnCorruption
     * above never firing). Root cause turned out to be unrelated to
     * CreateSemaphoreW/process-creation specifically: a general squash
     * codegen bug where a LOCAL (stack, non-array) struct/union variable
     * with a brace initializer — e.g. SDL_getenv.c's own
     * "CountEnvStringsData countdata = { 0, 0 };" — only ever wrote ONE
     * value at the struct's base offset (evaluating the whole initializer
     * block as if it were a single expression) instead of one value per
     * field, leaving every field but the first as raw uninitialized stack
     * garbage. `countdata.length` (SDL_GetEnvironmentVariables()'s hash-
     * table string-size accumulator) came out as leftover stack garbage,
     * which fed a huge bogus size into the following SDL_malloc() call —
     * a real allocation failure, not corruption, which is why it looked
     * "CreateSemaphoreW-adjacent" under bisection (creating a mutex/
     * semaphore beforehand just happened to leave suitably non-zero
     * garbage on the stack slot `countdata.length` reused). Fixed in
     * codegen.c's AST_VAR_DECL case: local struct/union brace initializers
     * now generate one member-assignment statement per field (reusing the
     * existing, already-correct AST_ASSIGN/AST_MEMBER codegen), matching
     * the same fix already applied to the global/static Pass-0.5
     * initializer rewriter. See project-squash-sdl3-build memory notes for
     * the full investigation and the code read order that led to the
     * struct-initializer bug (Bug 1 → Bug 4's array_size fix → this). */
    {
        SDL_Environment *emptyenv = SDL_CreateEnvironment(false);

        SDL_PropertiesID procprops = SDL_CreateProperties();
        const char *args[] = { "cmd.exe", "/c", "echo squash_process_test_output", NULL };
        SDL_SetPointerProperty(procprops, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args);
        SDL_SetPointerProperty(procprops, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, emptyenv);
        SDL_SetNumberProperty(procprops, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(procprops, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_Process *proc = SDL_CreateProcessWithProperties(procprops);
        SDL_DestroyProperties(procprops);
        if (!proc) {
            SDL_Log("SDL_CreateProcess failed: %s", SDL_GetError());
            return 1;
        }

        /* Wait for the process to exit BEFORE reading its piped stdout:
         * SDL_windowsprocess.c sets stdout pipes to PIPE_NOWAIT
         * (non-blocking), so reading immediately after CreateProcessW
         * returns races the child — cmd.exe hasn't necessarily written
         * (or even started) yet, and a non-blocking read on an empty-
         * so-far pipe returns 0 bytes indistinguishable from EOF, making
         * the read loop stop immediately having read nothing. Waiting
         * first guarantees all of the child's output is already sitting
         * in the pipe buffer by the time we read. */
        int exitcode = -1;
        SDL_WaitProcess(proc, true, &exitcode);

        SDL_IOStream *procout = SDL_GetProcessOutput(proc);
        if (!procout) {
            SDL_Log("SDL_GetProcessOutput failed: %s", SDL_GetError());
            return 1;
        }
        char pbuf[256];
        size_t ptotal = 0;
        for (;;) {
            size_t n = SDL_ReadIO(procout, pbuf + ptotal, sizeof(pbuf) - 1 - ptotal);
            if (n == 0) break;
            ptotal += n;
            if (ptotal >= sizeof(pbuf) - 1) break;
        }
        pbuf[ptotal] = 0;

        SDL_Log("SDL_process: exitcode=%d output=[%s]", exitcode, pbuf);
        if (exitcode != 0 || !SDL_strstr(pbuf, "squash_process_test_output")) {
            SDL_Log("SDL_process MISMATCH: expected exitcode 0 and output containing the marker string");
            return 1;
        }
        SDL_Log("SDL_process round trip OK");
        SDL_DestroyProcess(proc);
    }

    /* Exercise the new custom PRIVATE video backend + SDL_VIDEO_RENDER_SW
     * software renderer: a real, visible top-level window plus a real
     * clear/fill/present cycle through the real Win32 GDI framebuffer path
     * (CreateWindowExA -> CreateDIBSection -> BitBlt), not just a headless
     * subsystem round trip. Run for a fixed, bounded number of frames so
     * this still terminates on its own rather than needing a user to close
     * the window.
     *
     * RESOLVED: the earlier "bootstrap[i]->create()" crash and a follow-on
     * infinite loop both traced to real, general squash compiler bugs, not
     * to SDL3 or to this backend — see [[project-squash-sdl3-build]] memory
     * notes for the full investigation:
     * 1. squash has no per-file "static" scoping in its single-TU unity
     *    build — video/SDL_video.c's file-scope "bootstrap[]" collided with
     *    camera/SDL_camera.c's identically-named file-scope array, so any
     *    reference to "bootstrap" anywhere resolved to whichever was
     *    registered last. Worked around with a build-file-only macro
     *    rename around camera's #include (see sdl_core.inc).
     * 2. codegen.c's field_byte_offset()/AST_ASSIGN store-width logic called
     *    typeinfo_size() directly, which can't resolve typedef chains (e.g.
     *    SDL3's own Uint8) — fixed by routing through the typedef-aware
     *    sizeof_type_sym() instead.
     * 3. codegen.c's AST_VAR load path decided sign- vs zero-extend from the
     *    raw (unresolved) TypeInfo.is_unsigned flag, so a "Uint32 mask;"
     *    local got sign-extended (MOVSXD) — fatal for shifting a value with
     *    bit31 set (e.g. an alpha mask like 0xFF000000): the corrupted upper
     *    32 bits eventually leak into the low 32 during "mask >>= 1",
     *    so the loop's own exit condition never becomes true. Fixed by
     *    routing through type_is_unsigned_resolved() instead.
     *
     * RESOLVED (follow-up session): the PRIVATE_PumpEvents crash above was
     * two real, general Windows-x64 codegen bugs in squash, both in
     * assembler.c/codegen.c:
     * 4. asm_enter_deferred() now pushes RBX/RSI/RDI/R12-R15 as required by
     *    the Win64 ABI, but the matching restore (asm_win64_callee_restore())
     *    originally used stack `pop`s — wrong, because this codebase only
     *    ever frees a function's locals via `leave` at the very end (never a
     *    mid-function explicit `add rsp,N`), so by the time the pops ran,
     *    RSP was still positioned *under* the function's own live locals:
     *    the pops read garbage locals memory into the callee-saved
     *    registers instead of the real saved values, silently corrupting
     *    whatever the caller (here, Windows' own message-dispatch code)
     *    needed those registers to still hold. Fixed by switching to fixed
     *    RBP-relative `mov` loads instead of stack pops.
     * 5. Once (4) was fixed, local variables (e.g. PRIVATE_PumpEvents' own
     *    "MSG msg;") were still allocated starting at [rbp-8], directly
     *    overlapping the newly-reserved [rbp-8..rbp-56] register-save area.
     *    Fixed by starting Win64 locals at [rbp-64] instead of [rbp-8].
     * With both fixes, SDL_PollEvent/PRIVATE_PumpEvents now run correctly.
     * 6. A separate, unrelated build-configuration gap (not a compiler bug):
     *    SDL_events.c's real SDL_PumpEventMaintenance() unconditionally
     *    calls SDL_UpdateAudio(), guarded only by "#ifndef SDL_AUDIO_DISABLED"
     *    — never set by "minimal" config, since real distros always ship at
     *    least the dummy audio backend, which this unity build omits
     *    entirely (SIMD intrinsics). Fixed with our own
     *    SDL_build_config_private.h defining SDL_AUDIO_DISABLED.
     * 7. Same gap, second instance: SDL_Delay() (timer/SDL_timer.c, real)
     *    calls SDL_SYS_DelayNS() directly; the real Windows implementation
     *    (timer/windows/SDL_systimer.c) is excluded from this unity build
     *    (waitable-timer setup beyond scope), and only its two
     *    performance-counter functions had hand-written replacements in
     *    sdl_core.inc. Fixed by adding a minimal real SDL_SYS_DelayNS()
     *    (Sleep-based) alongside them.
     * 8. A THIRD bug, found only after (4)-(7) let the frame loop run to
     *    completion for the first time: SDL_VideoQuit() crashed identically
     *    to the original PumpEvents symptom. Root cause was NOT a compiler
     *    bug — SDL_qsort.c (real, unmodified) does
     *    "#define free SDL_free"/"#define malloc SDL_malloc" near its top
     *    with no matching #undef at the end, which is harmless when it's
     *    compiled as its own translation unit, but in this unity build
     *    leaks forward into every later #include in the same file — it
     *    silently mangled SDL_video.c's real "_this->free(_this);" (a call
     *    through the SDL_VideoDevice::free function-pointer field) into
     *    "_this->SDL_free(_this);", a field that doesn't exist on the
     *    struct, causing a wrong-offset read and a garbage function-pointer
     *    call. Fixed with "#undef malloc"/"#undef free" in sdl_core.inc
     *    immediately after SDL_qsort.c's #include.
     * With all of the above, the full video/render frame loop (60 frames)
     * plus SDL_DestroyRenderer/SDL_DestroyWindow/SDL_QuitSubSystem/SDL_Quit
     * now complete cleanly end-to-end with ExitCode=0, confirmed across
     * multiple consecutive runs. */
    dbgmark("before squash_repro_hashtable_test");
    squash_repro_hashtable_test();
    dbgmark("after squash_repro_hashtable_test");

    dbgmark("before SDL_InitSubSystem(VIDEO)");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_InitSubSystem(VIDEO) failed: %s", SDL_GetError());
        return 1;
    }
    dbgmark("after SDL_InitSubSystem(VIDEO)");
    SDL_Log("SDL_InitSubSystem(VIDEO) OK");

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    dbgmark("before SDL_CreateWindowAndRenderer");
    if (!SDL_CreateWindowAndRenderer("squash SDL3 video/render test", 640, 480, 0, &window, &renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        return 1;
    }
    dbgmark("after SDL_CreateWindowAndRenderer");
    SDL_Log("SDL_CreateWindowAndRenderer OK");

    int frame;
    for (frame = 0; frame < 60; frame++) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                break;
            }
        }
        SDL_SetRenderDrawColor(renderer, (Uint8)(frame * 4), 80, 160, 255);
        SDL_RenderClear(renderer);
        SDL_FRect rect;
        rect.x = 100; rect.y = 100; rect.w = 200; rect.h = 120;
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(renderer, &rect);
        SDL_RenderPresent(renderer);
        if (frame == 0) dbgmark("after first frame present");
        SDL_Delay(16);
    }
    dbgmark("after frame loop");
    SDL_Log("video/render frame loop finished (%d frames)", frame);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    dbgmark("after QuitSubSystem(VIDEO)");
    SDL_Log("video/render round trip OK");

    SDL_Quit();
    return 0;
}


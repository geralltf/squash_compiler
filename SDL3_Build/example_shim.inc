/* Shared SDL_main-callback-loop shim for building an examples subdirectory's program.
 *
 * SDL3's examples are all written against the "callback" style of main:
 * they set SDL_MAIN_USE_CALLBACKS to 1 and define SDL_AppInit, SDL_AppEvent,
 * SDL_AppIterate, and SDL_AppQuit instead of a real main function — see any
 * file under ../../examples/.
 * The real SDL_main.c/SDL_main_callbacks.c machinery that normally drives
 * this isn't part of this build (see sdl_core.inc's own "#undef main" note:
 * it's tied to SDL's platform-specific WinMain/entry-point launcher, which
 * this build deliberately bypasses in favor of squash's own plain-main
 * PE entry point). This file provides a real main() that does the
 * equivalent job directly: call SDL_AppInit once, then loop pumping events
 * into SDL_AppEvent and calling SDL_AppIterate once per frame until either
 * callback returns anything other than SDL_APP_CONTINUE, then call
 * SDL_AppQuit. Every example's own SDL_AppInit/AppEvent/AppIterate/AppQuit
 * are the same real, unmodified functions a normal SDL_main-based build
 * would call — only the driving loop is hand-written here instead of
 * coming from SDL3's own main/ sources. */

#undef main

/* Forward declaration needed so codegen sees this as "declared but not
 * defined in this TU" (SYM_FUNC, no body) rather than a totally unknown
 * identifier. When this driver file is compiled standalone and linked
 * against squash_build/sdl_common.sqo (the cached-object build path — see
 * build_example.sh), squash_init_private_bootstrap()'s real definition
 * lives in the OTHER translation unit (sdl_core.inc, baked into the .sqo).
 * Without this prototype, codegen has no symbol-table entry at all for the
 * call and silently falls back to treating it as an unresolvable external
 * DLL import (the same "unknown function" failure class as the alloca()
 * bug — see [[project-squash-sdl3-build]] memory notes) instead of emitting
 * the deferred RELOC_STATIC_REL32 call objfile_merge() knows how to resolve
 * against the .sqo's export table. */
void squash_init_private_bootstrap(void);

int main(void)
{
    /* sdl_core.inc's PRIVATE_bootstrap (our own minimal Windows video
     * driver) is a plain global with no static initializer — its
     * function-pointer fields (create/ShowMessageBox/etc.) are only ever
     * assigned by squash_init_private_bootstrap(), which sdl_unity.c calls
     * itself at the top of its own hand-written main(). This generic
     * example main() is the only entry point for every example build, so
     * without this call here too, SDL_VideoInit()'s "bootstrap[i]->create()"
     * call goes through an uninitialized (zero) function pointer the
     * instant any example calls SDL_Init(SDL_INIT_VIDEO) — confirmed via
     * objdump: the crash is the literal "call r11" implementing that exact
     * call expression, with r11 loaded from PRIVATE_bootstrap's never-
     * initialized "create" field. */
    squash_init_private_bootstrap();

    void *appstate = NULL;
    SDL_AppResult result = SDL_AppInit(&appstate, 0, NULL);

    while (result == SDL_APP_CONTINUE) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            SDL_AppResult evr = SDL_AppEvent(appstate, &ev);
            if (evr != SDL_APP_CONTINUE) {
                result = evr;
            }
        }
        if (result == SDL_APP_CONTINUE) {
            result = SDL_AppIterate(appstate);
        }
    }

    SDL_AppQuit(appstate, result);
    return result == SDL_APP_SUCCESS ? 0 : 1;
}

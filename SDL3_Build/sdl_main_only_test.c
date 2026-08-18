/* Small "driver" file: links against squash_build/sdl_common.sqo (see
 * sdl_common_obj.c) instead of #include-ing sdl_core.inc directly, so this
 * file compiles in a couple seconds instead of the ~60+ it takes to
 * recompile all of SDL3 from source every time. */
#include <windows.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
/* SDL_main.h "#define main SDL_main" (so a real app's main() gets renamed
 * and wrapped by SDL's platform-specific launcher, which this build doesn't
 * use — see sdl_core.inc's identical #undef for the full rationale). */
#undef main

static int my_strlen(const char *s) { int n=0; while (s[n]) n++; return n; }
static void dbgmark(const char *s) {
    HANDLE h = GetStdHandle((DWORD)-11);
    DWORD written;
    WriteFile(h, s, (DWORD)my_strlen(s), &written, NULL);
    WriteFile(h, "\n", 1, &written, NULL);
}

int main(void) {
    SDL_SetMainReady();
    dbgmark("before SDL_Init");
    if (!SDL_Init(0)) {
        dbgmark("SDL_Init failed:");
        dbgmark(SDL_GetError());
        return 1;
    }
    dbgmark("SDL3 (squash object-file build) initialised OK");
    SDL_Quit();
    return 0;
}

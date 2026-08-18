/* Calls miniz's tdefl_write_image_to_png_file_in_memory_ex() directly
 * (bypassing SDL_Surface/SDL_SavePNG) against the REAL sdl_core.inc build
 * (real SDL_malloc/SDL_free, real miniz.h baked in via video/SDL_stb.c) to
 * isolate whether the crash is inside miniz's own PNG writer or somewhere
 * in SDL_stb.c's/SDL_Surface's glue code around it. */
#include "sdl_core.inc"

int main(void)
{
    dbgmark("main() entered");
    if (!SDL_Init(0)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    dbgmark("after SDL_Init");

    int w = 4, h = 4, chans = 4;
    Uint8 img[4*4*4];
    int i;
    for (i = 0; i < 4*4*4; i++) img[i] = (Uint8)(i * 3);
    dbgmark("before tdefl_write_image_to_png_file_in_memory_ex");
    size_t png_len = 0;
    void *png = tdefl_write_image_to_png_file_in_memory_ex(img, w, h, chans, w*chans, &png_len, 6, MZ_FALSE, NULL, 0, NULL, 0);
    dbgmark("after tdefl_write_image_to_png_file_in_memory_ex");
    if (!png) {
        SDL_Log("tdefl_write_image_to_png_file_in_memory_ex returned NULL");
        return 2;
    }
    SDL_Log("PNG written in memory: %d bytes", (int)png_len);
    mz_free(png);
    dbgmark("after mz_free");

    SDL_Quit();
    return 0;
}

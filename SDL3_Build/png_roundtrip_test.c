/* Real PNG encode/decode round trip through the newly-wired stb_image.h +
 * miniz.h (see sdl_core.inc's "video/SDL_stb.c" include) -- builds a small
 * surface with known per-pixel RGBA values, SDL_SavePNG's it to a real file
 * on disk, then SDL_LoadPNG's that exact file back and compares every pixel
 * against the original. Not a synthetic parser-only smoke test: this
 * exercises the real miniz deflate/inflate compressor and the real
 * stb_image PNG decoder end-to-end, exactly the codepath
 * examples/renderer's texture-loading examples depend on. */
#include "sdl_core.inc"

int main(void)
{
    dbgmark("main() entered");
    if (!SDL_Init(0)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    dbgmark("after SDL_Init");

    const int W = 17, H = 13;
    SDL_Surface *surf = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        SDL_Log("SDL_CreateSurface failed: %s", SDL_GetError());
        return 1;
    }
    dbgmark("after SDL_CreateSurface");

    int x, y;
    for (y = 0; y < H; y++) {
        Uint8 *row = (Uint8 *)surf->pixels + y * surf->pitch;
        for (x = 0; x < W; x++) {
            Uint8 *px = row + x * 4;
            px[0] = (Uint8)(x * 13 + y * 7);
            px[1] = (Uint8)(x * 3 + y * 17);
            px[2] = (Uint8)(255 - x - y);
            px[3] = 255;
        }
    }
    dbgmark("after filling pixels");
    dbgmark_ptr("surf->w", (unsigned long long)surf->w);
    dbgmark_ptr("surf->h", (unsigned long long)surf->h);
    dbgmark_ptr("surf->pitch", (unsigned long long)surf->pitch);
    dbgmark_ptr("surf->format", (unsigned long long)surf->format);
    dbgmark_ptr("surf->pixels", (unsigned long long)(size_t)surf->pixels);
    dbgmark_ptr("SDL_BYTESPERPIXEL", (unsigned long long)SDL_BYTESPERPIXEL(surf->format));

    dbgmark("before direct tdefl_write_image_to_png_file_in_memory_ex call");
    size_t direct_len = 0;
    void *direct_png = tdefl_write_image_to_png_file_in_memory_ex(surf->pixels, surf->w, surf->h, SDL_BYTESPERPIXEL(surf->format), surf->pitch, &direct_len, 6, MZ_FALSE, NULL, 0, NULL, 0);
    dbgmark("after direct tdefl_write_image_to_png_file_in_memory_ex call");
    if (!direct_png) {
        SDL_Log("direct tdefl call failed");
        return 1;
    }
    SDL_Log("direct tdefl call OK: %d bytes", (int)direct_len);
    mz_free(direct_png);
    dbgmark("after mz_free(direct_png)");

    const char *path = "squash_png_roundtrip_test.png";
    if (!SDL_SavePNG(surf, path)) {
        SDL_Log("SDL_SavePNG failed: %s", SDL_GetError());
        return 1;
    }
    dbgmark("after SDL_SavePNG");
    SDL_Log("SDL_SavePNG OK -> %s", path);

    SDL_Surface *loaded = SDL_LoadPNG(path);
    if (!loaded) {
        SDL_Log("SDL_LoadPNG failed: %s", SDL_GetError());
        return 1;
    }
    dbgmark("after SDL_LoadPNG");
    SDL_Log("SDL_LoadPNG OK: %dx%d format=%d", loaded->w, loaded->h, loaded->format);

    if (loaded->w != W || loaded->h != H) {
        SDL_Log("MISMATCH: expected %dx%d, got %dx%d", W, H, loaded->w, loaded->h);
        return 1;
    }

    SDL_Surface *cmp = loaded;
    int free_cmp = 0;
    if (loaded->format != SDL_PIXELFORMAT_RGBA32) {
        cmp = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
        if (!cmp) {
            SDL_Log("SDL_ConvertSurface failed: %s", SDL_GetError());
            return 1;
        }
        free_cmp = 1;
    }

    int mismatches = 0;
    for (y = 0; y < H; y++) {
        Uint8 *want_row = (Uint8 *)surf->pixels + y * surf->pitch;
        Uint8 *got_row  = (Uint8 *)cmp->pixels + y * cmp->pitch;
        for (x = 0; x < W; x++) {
            Uint8 *want = want_row + x * 4;
            Uint8 *got  = got_row + x * 4;
            if (want[0] != got[0] || want[1] != got[1] || want[2] != got[2] || want[3] != got[3]) {
                if (mismatches < 5) {
                    SDL_Log("pixel (%d,%d) mismatch: want=%d,%d,%d,%d got=%d,%d,%d,%d",
                            x, y, want[0], want[1], want[2], want[3], got[0], got[1], got[2], got[3]);
                }
                mismatches++;
            }
        }
    }
    dbgmark("after pixel comparison");

    if (free_cmp) {
        SDL_DestroySurface(cmp);
    }
    SDL_DestroySurface(loaded);
    SDL_DestroySurface(surf);

    if (mismatches != 0) {
        SDL_Log("PNG round trip FAILED: %d/%d pixels mismatched", mismatches, W * H);
        return 1;
    }

    SDL_Log("PNG round trip PASSED: %dx%d, all pixels match", W, H);
    dbgmark("before SDL_Quit");
    SDL_Quit();
    return 0;
}

#include <stddef.h>
#include <stdio.h>
typedef unsigned char Uint8;
void *SDL_malloc(size_t size);
void *SDL_realloc(void *mem, size_t size);
void SDL_free(void *mem);
#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER SDL_LIL_ENDIAN
#define MZ_ASSERT(x) ((void)0)
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define MINIZ_LITTLE_ENDIAN 1
#else
#define MINIZ_LITTLE_ENDIAN 0
#endif
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#define MINIZ_SDL_NOUNUSED
#include "miniz.h"
void *SDL_malloc(size_t size) { return malloc(size); }
void *SDL_realloc(void *mem, size_t size) { return realloc(mem, size); }
void SDL_free(void *mem) { free(mem); }

int main(void) {
    int w = 17, h = 13, chans = 4;
    Uint8 img[17*13*4];
    int x, y;
    for (y=0;y<13;y++) for (x=0;x<17;x++) {
        Uint8 *px=&img[(y*17+x)*4];
        px[0]=(Uint8)(x*13+y*7); px[1]=(Uint8)(x*3+y*17); px[2]=(Uint8)(255-x-y); px[3]=255;
    }
    size_t png_len = 0;
    void *png = tdefl_write_image_to_png_file_in_memory_ex(img, w, h, chans, w*chans, &png_len, 6, MZ_FALSE, 0, 0, 0, 0);
    if (!png) return 1;
    FILE *f = fopen("squash_png_roundtrip_test.png", "wb");
    fwrite(png, 1, png_len, f);
    fclose(f);
    mz_free(png);
    return 0;
}

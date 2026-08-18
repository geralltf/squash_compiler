#include <stddef.h>
typedef unsigned char Uint8;
typedef unsigned short Uint16;
typedef unsigned int Uint32;
typedef unsigned long long Uint64;
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

int main(void) {
    int i;
    /* Test 2: PNG image writer, small 4x4 RGBA image */
    int w = 4, h = 4, chans = 4;
    unsigned char img[4*4*4];
    for (i = 0; i < 4*4*4; i++) img[i] = (unsigned char)(i * 3);
    size_t png_len = 0;
    void *png = tdefl_write_image_to_png_file_in_memory_ex(img, w, h, chans, w*chans, &png_len, 6, MZ_FALSE, 0, 0, 0, 0);
    if (!png) return 2;
    mz_free(png);

    return 0;
}

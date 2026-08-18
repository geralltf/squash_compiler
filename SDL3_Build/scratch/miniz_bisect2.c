#include <stddef.h>
typedef unsigned char Uint8;
typedef unsigned short Uint16;
typedef unsigned int Uint32;
typedef unsigned long long Uint64;

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

#include <windows.h>
static void dbgmark(const char *s) {
    HANDLE h = GetStdHandle((DWORD)-11);
    DWORD written;
    int len = 0;
    while (s[len]) len++;
    WriteFile(h, s, (DWORD)len, &written, NULL);
    WriteFile(h, "\n", 1, &written, NULL);
}

#include <stdio.h>
static void dbgmark_hex(const char *label, unsigned long long v) {
    HANDLE h = GetStdHandle((DWORD)-11);
    DWORD written;
    char buf[128];
    int n = sprintf(buf, "%s=0x%llx\n", label, v);
    WriteFile(h, buf, (DWORD)n, &written, NULL);
}

void *SDL_malloc(size_t size) { return malloc(size); }
void *SDL_realloc(void *mem, size_t size) { return realloc(mem, size); }
void SDL_free(void *mem) { free(mem); }

#include "miniz_instrumented.h"

int main(void) {
    dbgmark("main entered");
    int w = 17, h = 13, chans = 4;
    Uint8 img[17*13*4];
    int ii;
    for (ii = 0; ii < 17*13*4; ii++) img[ii] = (Uint8)(ii * 3);
    size_t png_len = 0;
    dbgmark("before tdefl_write_image_to_png_file_in_memory_ex");
    void *png = tdefl_write_image_to_png_file_in_memory_ex(img, w, h, chans, w*chans, &png_len, 6, MZ_FALSE, 0, 0, 0, 0);
    dbgmark("after tdefl_write_image_to_png_file_in_memory_ex");
    if (!png) return 2;
    mz_free(png);
    dbgmark("done");
    return 0;
}

#include <stddef.h>
typedef unsigned char Uint8;
typedef unsigned short Uint16;
typedef unsigned int Uint32;
typedef unsigned long long Uint64;
typedef signed char Sint8;
typedef signed short Sint16;
typedef signed int Sint32;
typedef signed long long Sint64;
#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER SDL_LIL_ENDIAN

void *SDL_malloc(size_t size);
void *SDL_realloc(void *mem, size_t size);
void SDL_free(void *mem);
void *SDL_memcpy(void *dst, const void *src, size_t len);
void *SDL_memset(void *dst, int c, size_t len);
int SDL_strcmp(const char *a, const char *b);
int SDL_strncmp(const char *a, const char *b, size_t n);
long SDL_strtol(const char *s, char **endp, int base);
int SDL_abs(int x);
double SDL_pow(double x, double y);
double SDL_scalbn(double x, int n);
#define SDL_assert(x) ((void)0)

/* STB image conversion */
#ifndef SDL_DISABLE_STB
#define SDL_HAVE_STB
#endif

#ifdef SDL_HAVE_STB
#define malloc SDL_malloc
#define realloc SDL_realloc
#define free SDL_free
#undef memcpy
#define memcpy SDL_memcpy
#undef memset
#define memset SDL_memset
#undef strcmp
#define strcmp SDL_strcmp
#undef strncmp
#define strncmp SDL_strncmp
#define strtol SDL_strtol

#define abs SDL_abs
#define pow SDL_pow
#define ldexp SDL_scalbn

#define STB_IMAGE_STATIC
#define STBI_NO_THREAD_LOCALS
#define STBI_FAILURE_USERMSG
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STBI_ASSERT SDL_assert
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define MZ_ASSERT(x) SDL_assert(x)
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define MINIZ_LITTLE_ENDIAN 1
#else
#define MINIZ_LITTLE_ENDIAN 0
#endif
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#define MINIZ_SDL_NOUNUSED
#include "miniz.h"

#undef memset
#endif // SDL_HAVE_STB

int main(void) { return 0; }

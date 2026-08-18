#ifndef _STDDEF_H
#define _STDDEF_H

typedef unsigned long long size_t;
typedef long long          ptrdiff_t;
/* wchar_t's real width is implementation-defined but NOT arbitrary once a
 * target ABI is chosen: real Windows (MSVC, and every compiler that's
 * actually ABI-compatible with it) defines wchar_t as 16-bit, matching
 * WCHAR/UTF-16 code units — code that calls a real Windows API returning
 * wide strings (GetEnvironmentStringsW, FindFirstFileExW's cFileName,
 * etc.) and then walks the result with wchar_t-typed pointer arithmetic
 * (e.g. stdlib/SDL_string.c's "while (*string++) ++len;" in SDL_wcslen())
 * relies on this matching. Unix/glibc-family targets use a 32-bit
 * wchar_t instead — a real, different ABI, not a mistake — so this must
 * stay conditional on the target, not just always be 2 or always be 4.
 * Getting this wrong for -windows builds specifically went unnoticed for
 * a long time in this project because most Windows-path code here talks
 * in WCHAR (already correctly 16-bit) directly rather than wchar_t — it
 * only surfaced once SDL_getenv.c's real GetEnvironmentStringsW() parsing
 * loop (which genuinely uses SDL_wcslen on real 16-bit-per-character
 * Windows API output) got exercised for the first time: scanning 4 bytes
 * at a time through real 2-byte-per-character data walked straight past
 * the true null terminator into unrelated heap memory. */
#ifdef _WIN32
typedef unsigned short     wchar_t;
#else
typedef unsigned int       wchar_t;
#endif

/* nullptr_t (C23) */
typedef void *nullptr_t;

#define NULL    ((void*)0)
#define nullptr ((void*)0)
#define offsetof(type, member) ((size_t)&((type*)0)->member)

/* max_align_t */
typedef double max_align_t;

#endif /* _STDDEF_H */

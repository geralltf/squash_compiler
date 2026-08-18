#ifndef _WCHAR_H
#define _WCHAR_H

/* Wide character type — already in stddef.h, but repeat for standalone use.
 * Must match stddef.h's own _WIN32-conditional choice exactly (see its
 * comment) — real Windows wchar_t is 16-bit (matches WCHAR/UTF-16 code
 * units), other targets use 32-bit. */
#ifndef _STDDEF_H
#ifdef _WIN32
typedef unsigned short wchar_t;
#else
typedef unsigned int wchar_t;
#endif
typedef unsigned long long size_t;
#endif

/* Wide string functions (implemented via platform APIs; stubs here) */
typedef int wint_t;
typedef int mbstate_t;

#define WCHAR_MIN 0
#define WCHAR_MAX 65535
#define WEOF      ((wint_t)(-1))

/* These map to printf/strlen equivalents */
int    wprintf(const wchar_t *fmt, ...);
int    wcslen(const wchar_t *s);
wchar_t *wcscpy(wchar_t *dst, const wchar_t *src);
wchar_t *wcscat(wchar_t *dst, const wchar_t *src);
int    wcscmp(const wchar_t *a, const wchar_t *b);
wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n);
int    wcsncmp(const wchar_t *a, const wchar_t *b, size_t n);

#endif /* _WCHAR_H */

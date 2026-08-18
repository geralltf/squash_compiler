#ifndef _UCHAR_H
#define _UCHAR_H

/* C11 Unicode character types */
typedef unsigned char      char8_t;   /* UTF-8 code unit   (C23) */
typedef unsigned short     char16_t;  /* UTF-16 code unit  (C11) */
typedef unsigned int       char32_t;  /* UTF-32 code unit  (C11) */

/* mbstate_t placeholder */
typedef int mbstate_t;

/* Conversion function stubs (full implementation would call OS APIs) */
/* char32_t/char16_t <-> multibyte conversion */
typedef unsigned int size_t;

#endif /* _UCHAR_H */

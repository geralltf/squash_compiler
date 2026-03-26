#ifndef _STDIO_H
#define _STDIO_H
#include "include/stddef.h"

/* FILE is an opaque pointer to msvcrt FILE struct */
typedef void FILE;

#define EOF    (-1)
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

#ifndef NULL
#define NULL ((void*)0)
#endif

#endif /* _STDIO_H */

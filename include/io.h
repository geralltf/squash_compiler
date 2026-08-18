#ifndef _IO_H
#define _IO_H
#include "include/stdio.h"

/* MSVC's io.h: low-level/CRT file-descriptor helpers. Only the handful
 * actually used anywhere in squash's own sources (diag.c's isatty/fileno
 * adaptive-color check) are declared here. */
int _isatty(int fd);
int _fileno(FILE *stream);

#endif

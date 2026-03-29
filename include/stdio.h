#ifndef _STDIO_H
#define _STDIO_H
#include "include/stddef.h"

/* FILE is an opaque pointer to msvcrt FILE struct */
typedef void FILE;

#define EOF      (-1)
#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2

#ifndef NULL
#define NULL ((void*)0)
#endif

/* Standard I/O function declarations */
FILE   *fopen  (const char *path, const char *mode);
int     fclose (FILE *f);
int     fgetc  (FILE *f);
int     fputc  (int c, FILE *f);
char   *fgets  (char *buf, int n, FILE *f);
int     fputs  (const char *s, FILE *f);
size_t  fread  (void *ptr, size_t sz, size_t n, FILE *f);
size_t  fwrite (const void *ptr, size_t sz, size_t n, FILE *f);
int     fseek  (FILE *f, long off, int whence);
long    ftell  (FILE *f);
void    rewind (FILE *f);
int     feof   (FILE *f);
int     ferror (FILE *f);
void    clearerr(FILE *f);
int     fflush (FILE *f);
int     remove (const char *path);
int     rename (const char *old, const char *newname);
int     ungetc (int c, FILE *f);

int     printf (const char *fmt, ...);
int     fprintf(FILE *f, const char *fmt, ...);
int     sprintf(char *buf, const char *fmt, ...);
int     snprintf(char *buf, size_t n, const char *fmt, ...);
int     puts   (const char *s);
int     putchar(int c);

#endif /* _STDIO_H */

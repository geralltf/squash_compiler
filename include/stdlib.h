#ifndef _STDLIB_H
#define _STDLIB_H

#define NULL ((void*)0)
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767

#include "include/stddef.h"

/* These are provided as internal shims by the compiler */
void *malloc(unsigned int sz);
void  free(void *p);
void *calloc(unsigned int n, unsigned int s);
void *realloc(void *p, unsigned int sz);
void  exit(int code);
void  abort(void);
int   atoi(const char *s);
long  atol(const char *s);
double atof(const char *s);
int   abs(int x);
long  labs(long x);
int   rand(void);
void  srand(unsigned int seed);

#endif

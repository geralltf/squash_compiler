#ifndef _STRING_H
#define _STRING_H

#include "include/stddef.h"

/* These are provided as internal shims by the compiler */
unsigned int strlen(const char *s);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, unsigned int n);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, unsigned int n);
char *strcat(char *d, const char *s);
char *strncat(char *d, const char *s, unsigned int n);
char *strchr(const char *s, int c);
char *strstr(const char *h, const char *n);
char *strdup(const char *s);
#ifdef _WIN32
#define strdup _strdup
#endif

char *strrchr(const char *s, int c);
void *memcpy(void *d, const void *s, unsigned int n);
void *memmove(void *d, const void *s, unsigned int n);
void *memset(void *d, int c, unsigned int n);
int   memcmp(const void *a, const void *b, unsigned int n);

#endif

/* Windows-specific case-insensitive comparison */
int _stricmp(const char *a, const char *b);
int _strnicmp(const char *a, const char *b, int n);

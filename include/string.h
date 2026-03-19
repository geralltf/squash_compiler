#ifndef _STRING_H
#define _STRING_H

static inline unsigned int strlen(const char *s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}
static inline char *strcpy(char *d, const char *s) {
    char *r = d;
    while (*s) { *d = *s; d++; s++; }
    *d = 0;
    return r;
}
static inline char *strncpy(char *d, const char *s, unsigned int n) {
    char *r = d;
    while (n && *s) { *d = *s; d++; s++; n--; }
    while (n) { *d = 0; d++; n--; }
    return r;
}
static inline int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)((unsigned char)*a - (unsigned char)*b);
}
static inline int strncmp(const char *a, const char *b, unsigned int n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (int)((unsigned char)*a - (unsigned char)*b) : 0;
}
static inline char *strcat(char *d, const char *s) {
    char *r = d;
    while (*d) d++;
    while (*s) { *d = *s; d++; s++; }
    *d = 0;
    return r;
}
static inline char *strncat(char *d, const char *s, unsigned int n) {
    char *r = d;
    while (*d) d++;
    while (n && *s) { *d = *s; d++; s++; n--; }
    *d = 0;
    return r;
}
static inline char *strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return (c == 0) ? (char*)s : ((char*)0);
}
static inline char *strrchr(const char *s, int c) {
    const char *p = ((char*)0);
    while (*s) { if (*s == (char)c) p = s; s++; }
    return (char*)p;
}
static inline char *strstr(const char *h, const char *n) {
    if (!*n) return (char*)h;
    while (*h) {
        const char *a = h;
        const char *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char*)h;
        h++;
    }
    return ((char*)0);
}
static inline void *memcpy(void *d, const void *s, unsigned int n) {
    char *dd = (char*)d;
    const char *ss = (const char*)s;
    while (n) { *dd = *ss; dd++; ss++; n--; }
    return d;
}
static inline void *memmove(void *d, const void *s, unsigned int n) {
    char *dd = (char*)d;
    const char *ss = (const char*)s;
    if (dd < ss) { while (n) { *dd = *ss; dd++; ss++; n--; } }
    else { dd += n; ss += n; while (n) { dd--; ss--; *dd = *ss; n--; } }
    return d;
}
static inline void *memset(void *s, int c, unsigned int n) {
    char *p = (char*)s;
    while (n) { *p = (char)c; p++; n--; }
    return s;
}
static inline int memcmp(const void *a, const void *b, unsigned int n) {
    const unsigned char *aa = (const unsigned char*)a;
    const unsigned char *bb = (const unsigned char*)b;
    while (n) { if (*aa != *bb) return *aa - *bb; aa++; bb++; n--; }
    return 0;
}
static inline void *memchr(const void *s, int c, unsigned int n) {
    const unsigned char *p = (const unsigned char*)s;
    while (n) { if (*p == (unsigned char)c) return (void*)p; p++; n--; }
    return ((void*)0);
}
static inline unsigned int strspn(const char *s, const char *a) {
    unsigned int n = 0;
    while (*s) {
        const char *p = a;
        int found = 0;
        while (*p) { if (*s == *p) { found = 1; break; } p++; }
        if (!found) break;
        s++; n++;
    }
    return n;
}
static inline unsigned int strcspn(const char *s, const char *a) {
    unsigned int n = 0;
    while (*s) {
        const char *p = a;
        while (*p) { if (*s == *p) return n; p++; }
        s++; n++;
    }
    return n;
}
#endif

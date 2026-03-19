#ifndef _STDLIB_H
#define _STDLIB_H

#define NULL ((void*)0)
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767

static inline void *malloc(unsigned int sz) { return HeapAlloc(GetProcessHeap(),0,sz); }
static inline void  free(void *p)           { HeapFree(GetProcessHeap(),0,p); }
static inline void *calloc(unsigned int n, unsigned int s) {
    unsigned int tot = n*s;
    void *p = malloc(tot);
    if (p) { unsigned int i=0; char *c=(char*)p; while(i<tot){c[i]=0;i++;} }
    return p;
}
static inline void *realloc(void *p, unsigned int sz) {
    void *n=malloc(sz); if(n&&p){free(p);} return n;
}
static inline void exit(int code)  { ExitProcess((unsigned int)code); }
static inline void abort(void)     { ExitProcess(3); }

static inline int atoi(const char *s) {
    int n=0; int neg=0;
    while(*s==' ')s++;
    if(*s=='-'){neg=1;s++;}else if(*s=='+')s++;
    while(*s>='0'&&*s<='9'){n=n*10+(*s-'0');s++;}
    return neg?-n:n;
}
static inline long atol(const char *s){return (long)atoi(s);}
static inline double atof(const char *s){
    double n=0; double f=1; int neg=0;
    while(*s==' ')s++;
    if(*s=='-'){neg=1;s++;}else if(*s=='+')s++;
    while(*s>='0'&&*s<='9'){n=n*10+(*s-'0');s++;}
    if(*s=='.'){s++;while(*s>='0'&&*s<='9'){f*=0.1;n+=(*s-'0')*f;s++;}}
    return neg?-n:n;
}
static inline int  abs(int x)  { return x<0?-x:x; }
static inline long labs(long x){ return x<0?-x:x; }
static inline int  max(int a, int b){ return a>b?a:b; }
static inline int  min(int a, int b){ return a<b?a:b; }
#define rand() (12345)
static inline void srand(unsigned int s){(void)s;}
#endif

#ifndef _TIME_H
#define _TIME_H
#include "include/stddef.h"
typedef long long time_t;
typedef long long clock_t;
#define CLOCKS_PER_SEC 1000

struct tm {
    int tm_sec;   int tm_min;  int tm_hour;
    int tm_mday;  int tm_mon;  int tm_year;
    int tm_wday;  int tm_yday; int tm_isdst;
};

/* Windows API */
typedef struct { unsigned lo; unsigned hi; } FILETIME;
typedef struct { short year,month,dow,day,hour,min,sec,ms; } SYSTEMTIME;
void GetSystemTime(SYSTEMTIME *st);
void GetLocalTime(SYSTEMTIME *st);
unsigned GetTickCount(void);
void GetSystemTimeAsFileTime(FILETIME *ft);

static inline time_t time(time_t *t) {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    /* Convert Windows FILETIME (100ns since 1601) to Unix time (seconds since 1970) */
    unsigned long long w = ((unsigned long long)ft.hi<<32)|ft.lo;
    unsigned long long unix_t = (w - 116444736000000000ULL) / 10000000ULL;
    time_t r = (time_t)unix_t;
    if(t) *t=r;
    return r;
}
static inline clock_t clock(void) { return (clock_t)GetTickCount(); }
static inline double difftime(time_t end, time_t start){ return (double)(end-start); }

static inline struct tm *gmtime(const time_t *t) {
    static struct tm tm_buf;
    time_t tt = *t;
    tm_buf.tm_sec  = (int)(tt%60); tt/=60;
    tm_buf.tm_min  = (int)(tt%60); tt/=60;
    tm_buf.tm_hour = (int)(tt%24); tt/=24;
    /* simplified — doesn't handle months correctly */
    tm_buf.tm_wday = (int)((tt+4)%7);
    tm_buf.tm_year = 70; /* approximate */
    return &tm_buf;
}
static inline struct tm *localtime(const time_t *t){ return gmtime(t); }
static inline size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm){
    /* minimal implementation */
    (void)fmt; (void)tm;
    if(max>0)s[0]=0;
    return 0;
}
#endif

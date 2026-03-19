#ifndef LOG_H
#define LOG_H
/* Compiler logging system — levels: 0=off,1=error,2=warn,3=info,4=debug,5=trace */
#include <stdio.h>
#include <stdarg.h>

extern int g_log_level;

static inline void log_msg(int level, const char *tag, const char *fmt, ...) {
    if (level > g_log_level) return;
    const char *colors[] = {"","\033[31m","\033[33m","\033[32m","\033[36m","\033[35m"};
    const char *reset = "\033[0m";
    if (g_log_level >= 4) { /* color only in verbose mode */
        fprintf(stderr, "%s[%s]%s ", colors[level>5?5:level], tag, reset);
    } else {
        fprintf(stderr, "[%s] ", tag);
    }
    va_list ap; va_start(ap,fmt); vfprintf(stderr,fmt,ap); va_end(ap);
    fprintf(stderr,"\n");
}

#define LOG_ERROR(...)  log_msg(1,"ERROR",__VA_ARGS__)
#define LOG_WARN(...)   log_msg(2,"WARN", __VA_ARGS__)
#define LOG_INFO(...)   log_msg(3,"INFO", __VA_ARGS__)
#define LOG_DEBUG(...)  log_msg(4,"DEBUG",__VA_ARGS__)
#define LOG_TRACE(...)  log_msg(5,"TRACE",__VA_ARGS__)

/* Source location for error messages */
typedef struct { const char *file; int line; } SrcLoc;
#define SRCLOC(line) ((SrcLoc){__FILE__,(line)})

void compiler_error(SrcLoc loc, const char *fmt, ...);
void compiler_warn (SrcLoc loc, const char *fmt, ...);
int  get_error_count(void);

#endif

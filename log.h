#ifndef LOG_H
#define LOG_H
/* Compiler logging system */
#include <stdio.h>
#include <stdarg.h>

extern int g_log_level;

static void log_msg(int level, const char *tag, const char *fmt, ...) {
    char msg[2048];
    int n = snprintf(msg, sizeof(msg), "[%s] ", tag);
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg+n, sizeof(msg)-n-2, fmt, ap);
    va_end(ap);
    msg[sizeof(msg)-2] = '\0';
    int len = (int)strlen(msg);
    if (len > 0 && msg[len-1] != '\n') { msg[len] = '\n'; msg[len+1] = '\0'; }
    printf("%s", msg);
}

#define LOG_ERROR(tag,fmt)   log_msg(1,(tag),(fmt))
#define LOG_WARN(tag,fmt)    log_msg(2,(tag),(fmt))
#define LOG_INFO(tag,fmt)    log_msg(3,(tag),(fmt))
#define LOG_DEBUG(tag,fmt)   log_msg(4,(tag),(fmt))
#define LOG_TRACE(tag,fmt)   log_msg(5,(tag),(fmt))

/* Source location for error messages */
typedef struct { const char *file; int line; } SrcLoc;

void compiler_error(SrcLoc loc, const char *fmt, ...);
void compiler_warn (SrcLoc loc, const char *fmt, ...);
int  get_error_count(void);

#endif

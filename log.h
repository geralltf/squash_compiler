#ifndef LOG_H
#define LOG_H
#include <stdio.h>

extern int g_log_level;

/* Non-variadic logging - callers use snprintf to pre-format if needed */
static void log_msg(int level, const char *tag, const char *msg) {
    if (level > g_log_level) return;
    printf("[%s] %s\n", tag, msg);
}

#define LOG_ERROR(tag,msg)  log_msg(1,(tag),(msg))
#define LOG_WARN(tag,msg)   log_msg(2,(tag),(msg))
#define LOG_INFO(tag,msg)   log_msg(3,(tag),(msg))
#define LOG_DEBUG(tag,msg)  log_msg(4,(tag),(msg))
#define LOG_TRACE(tag,msg)  log_msg(5,(tag),(msg))

typedef struct { const char *file; int line; } SrcLoc;

void compiler_error(SrcLoc loc, const char *msg);
void compiler_warn (SrcLoc loc, const char *msg);
int  get_error_count(void);

#endif

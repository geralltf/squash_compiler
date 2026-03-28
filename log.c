#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

int g_log_level = 2;
static int g_error_count = 0;

/* Format a string using vsnprintf and print via our printf shim */
static void log_vprint(const char *prefix, const char *fmt, va_list ap) {
    char msg[2048];
    int n = 0;
    if (prefix) {
        strncpy(msg, prefix, sizeof(msg)-1);
        n = (int)strlen(msg);
    }
    /* Use vsnprintf from msvcrt - this works even without proper FILE* setup */
    vsnprintf(msg+n, sizeof(msg)-n-2, fmt, ap);
    msg[sizeof(msg)-2] = '\0';
    /* Append newline if not present */
    int len = (int)strlen(msg);
    if (len > 0 && msg[len-1] != '\n') { msg[len] = '\n'; msg[len+1] = '\0'; }
    /* printf goes through our WriteFile shim - always works */
    printf("%s", msg);
}

void compiler_error(SrcLoc loc, const char *fmt, ...) {
    g_error_count++;
    char prefix[256] = "";
    if (loc.file && loc.line > 0)
        snprintf(prefix, sizeof prefix, "%s:%d: error: ", loc.file, loc.line);
    else
        snprintf(prefix, sizeof prefix, "error: ");
    va_list ap; va_start(ap,fmt);
    log_vprint(prefix, fmt, ap);
    va_end(ap);
}

void compiler_warn(SrcLoc loc, const char *fmt, ...) {
    char prefix[256] = "";
    if (loc.file && loc.line > 0)
        snprintf(prefix, sizeof prefix, "%s:%d: warning: ", loc.file, loc.line);
    else
        snprintf(prefix, sizeof prefix, "warning: ");
    va_list ap; va_start(ap,fmt);
    log_vprint(prefix, fmt, ap);
    va_end(ap);
}

int get_error_count(void) { return g_error_count; }

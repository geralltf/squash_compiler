#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

int g_log_level = 2; /* default: warn+error */
static int g_error_count = 0;

void compiler_error(SrcLoc loc, const char *fmt, ...) {
    g_error_count++;
    if (loc.file && loc.line > 0)
        fprintf(stderr, "%s:%d: error: ", loc.file, loc.line);
    else
        fprintf(stderr, "error: ");
    va_list ap; va_start(ap,fmt); vfprintf(stderr,fmt,ap); va_end(ap);
    fprintf(stderr,"\n");
}

void compiler_warn(SrcLoc loc, const char *fmt, ...) {
    if (loc.file && loc.line > 0)
        fprintf(stderr, "%s:%d: warning: ", loc.file, loc.line);
    else
        fprintf(stderr, "warning: ");
    va_list ap; va_start(ap,fmt); vfprintf(stderr,fmt,ap); va_end(ap);
    fprintf(stderr,"\n");
}

int get_error_count(void) { return g_error_count; }

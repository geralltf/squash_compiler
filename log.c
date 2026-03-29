#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int g_log_level = 2;
static int g_error_count = 0;

void compiler_error(SrcLoc loc, const char *msg) {
    if (loc.file && loc.line > 0)
        printf("%s:%d: error: %s\n", loc.file, loc.line, msg);
    else
        printf("error: %s\n", msg);
    g_error_count++;
}

void compiler_warn(SrcLoc loc, const char *msg) {
    if (loc.file && loc.line > 0)
        printf("%s:%d: warning: %s\n", loc.file, loc.line, msg);
    else
        printf("warning: %s\n", msg);
}

int get_error_count(void) { return g_error_count; }

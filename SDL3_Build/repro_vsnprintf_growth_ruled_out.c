#include <windows.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#undef main
void squash_init_private_bootstrap(void);
static int my_strlen(const char *s) { int n=0; while (s[n]) n++; return n; }
static void dbgmark(const char *s) {
    HANDLE h = GetStdHandle((DWORD)-11);
    DWORD written;
    WriteFile(h, s, (DWORD)my_strlen(s), &written, NULL);
    WriteFile(h, "\n", 1, &written, NULL);
}
static void dbgnum(const char *label, long long v) {
    char buf[64]; int n = 0;
    for (int i = 0; label[i]; i++) buf[n++] = label[i];
    buf[n++]='='; buf[n++]='0'; buf[n++]='x';
    unsigned long long uv = (unsigned long long)v;
    char digits[32]; int nd = 0;
    if (uv == 0) digits[nd++] = '0';
    while (uv) { digits[nd++] = "0123456789abcdef"[uv % 16]; uv /= 16; }
    for (int i = nd-1; i >= 0; i--) buf[n++] = digits[i];
    buf[n] = 0;
    dbgmark(buf);
}

/* Exact replica of SDL_SetErrorV's growth logic, using REAL SDL_vsnprintf,
   but with a plain local buffer (not the TLS error struct) to isolate
   whether SDL_vsnprintf itself misbehaves on the size-0-then-grow pattern. */
static void test_growth(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);

    char *str = NULL;
    size_t len = 0;
    int result;

    va_copy(ap2, ap);
    result = SDL_vsnprintf(str, len, fmt, ap2);
    va_end(ap2);
    dbgnum("first vsnprintf result", result);

    if (result >= 0 && (size_t)result >= len) {
        size_t newlen = (size_t)result + 1;
        dbgnum("newlen for realloc", (long long)newlen);
        char *newstr = (char *)SDL_malloc(newlen);
        dbgnum("newstr ptr", (long long)(size_t)newstr);
        va_copy(ap2, ap);
        int result2 = SDL_vsnprintf(newstr, newlen, fmt, ap2);
        va_end(ap2);
        dbgnum("second vsnprintf result", result2);
        str = newstr;
        len = newlen;
    }
    va_end(ap);

    dbgnum("final len", (long long)len);
    dbgmark("final str content:");
    dbgmark(str);
    dbgnum("strlen(str) actual", (long long)SDL_strlen(str));
}

int main(void) {
    squash_init_private_bootstrap();
    SDL_Init(0);
    test_growth("Couldn't open %s", "C:\Users\geral\AppData\Local\Temp\claude\c--projects-AST-CS-gi-squash-compiler\7d1a1b1e-a5cf-4a46-a355-faa8c06f17d9\scratchpad\sample.wav");
    dbgmark("DONE - no crash");
    return 0;
}

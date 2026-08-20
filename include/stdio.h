#ifndef _STDIO_H
#define _STDIO_H
#include "include/stddef.h"
#include "include/stdarg.h"

/* FILE is an opaque pointer to msvcrt FILE struct */
typedef void FILE;

#define EOF      (-1)
#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2

#ifndef NULL
#define NULL ((void*)0)
#endif

/* Standard I/O function declarations */
FILE   *fopen  (const char *path, const char *mode);
int     fclose (FILE *f);
int     fgetc  (FILE *f);
int     fputc  (int c, FILE *f);
char   *fgets  (char *buf, int n, FILE *f);
int     fputs  (const char *s, FILE *f);
size_t  fread  (void *ptr, size_t sz, size_t n, FILE *f);
size_t  fwrite (const void *ptr, size_t sz, size_t n, FILE *f);
int     fseek  (FILE *f, long off, int whence);
long    ftell  (FILE *f);
void    rewind (FILE *f);
int     feof   (FILE *f);
int     ferror (FILE *f);
void    clearerr(FILE *f);
/* fflush() as a real function call crashes when passed stdin/stdout/stderr:
 * those are just sentinel pointer values ((void*)0/1/2 below), not real
 * msvcrt FILE* structures, and msvcrt's actual fflush() dereferences
 * whatever it's given expecting real FILE* layout — a real
 * STATUS_ACCESS_VIOLATION, not a hypothetical one (reproduced directly:
 * plain "printf(...); fflush(stdout);" segfaults, "printf(...);" alone
 * does not). Since squash's printf/puts write straight through without
 * app-level buffering, flushing is a no-op in this model; skip the real
 * call entirely rather than risk it being handed one of these sentinels
 * (the overwhelmingly common case in real code is exactly
 * "fflush(stdout)"). */
#define fflush(f) ((void)(f), 0)
int     remove (const char *path);
int     rename (const char *old, const char *newname);
int     ungetc (int c, FILE *f);

int     printf (const char *fmt, ...);
int     sprintf(char *buf, const char *fmt, ...);
int     snprintf(char *buf, size_t n, const char *fmt, ...);
int     sscanf(const char *s, const char *fmt, ...);
int     puts   (const char *s);
int     putchar(int c);

#ifdef _WIN32
/* fprintf(stdin/stdout/stderr, ...) crashes for the same reason fflush()
 * does (see its comment above): those are just sentinel pointer values
 * ((void*)0/1/2), not real msvcrt FILE* structures, and msvcrt's real
 * fprintf() dereferences whatever FILE* it's given — a genuine
 * STATUS_ACCESS_VIOLATION (reproduced directly: plain "fprintf(stdout,
 * \"x\");" segfaults). This was previously unreachable in practice because
 * fprintf never resolved to a real import at all; it does now (a real
 * Windows SDK .lib import-library lookup added elsewhere in this
 * compiler), which surfaces it. Real code overwhelmingly calls
 * fprintf(stdout, ...)/fprintf(stderr, ...) for diagnostic output —
 * redirect those two specific sentinels to real Win32 console output
 * (GetStdHandle+WriteFile, bypassing msvcrt's FILE* machinery entirely,
 * the same way this compiler's fflush already does), and forward anything
 * else (a genuine fopen()'d FILE*) through fwrite() instead of recursing
 * into this same macro. */
extern void *GetStdHandle(int nStdHandle);
extern int WriteFile(void *hFile, const void *lpBuffer, unsigned long nNumberOfBytesToWrite, unsigned long *lpNumberOfBytesWritten, void *lpOverlapped);
static int __squash_fprintf_impl(FILE *f, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return n;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    if (f == stdout || f == stderr) {
        void *h = GetStdHandle(f == stdout ? -11 : -12);
        unsigned long written;
        WriteFile(h, buf, (unsigned long)n, &written, 0);
        return n;
    }
    return (int)fwrite(buf, 1, (size_t)n, f);
}
#define fprintf __squash_fprintf_impl
#else
/* Linux: same underlying problem fprintf(stdin/stdout/stderr, ...) has on
 * Windows (see the _WIN32 comment above) -- stdout/stderr are just
 * sentinel pointer values ((void*)1/(void*)2, see lexer.c's built-in
 * "stdout"/"stderr" macros), not real glibc FILE* structures, and a real
 * fprintf() would dereference whatever it's given -- a genuine segfault
 * (reproduced directly: plain "fprintf(stderr, \"x\");" crashes).
 *
 * Unlike the _WIN32 branch above, this can't just forward this function's
 * own va_list into the real system vsnprintf(): Windows x64's real ABI
 * va_list genuinely IS a flat pointer walking a contiguous shadow-space
 * region (matching stdarg.h's simplified model exactly), but Linux/SysV's
 * real ABI va_list is a 24-byte {gp_offset,fp_offset,overflow_arg_area,
 * reg_save_area} struct passed by reference -- handing glibc's real
 * vsnprintf a raw stack address that just points at an int instead of that
 * struct produces garbage (reproduced directly: forwarding a va_list to
 * vsnprintf here read back byte-garbage for every argument). So this
 * implements a small formatter directly against va_arg (which squash's own
 * codegen does support correctly for a locally-declared variadic function
 * -- see codegen.c's SysV variadic-prologue register-spill fix) instead of
 * delegating to any real libc variadic function. Covers the specifiers
 * this project's own diagnostic fprintf() calls actually use: %d %u %x %p
 * %s %c %f/%.Nf %%; anything else is copied through literally. */
extern long write(int fd, const void *buf, unsigned long count);
static void __squash_fmt_utoa(unsigned long v, int base, char *out, int *len) {
    char tmp[32]; int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { int d = (int)(v % (unsigned long)base); tmp[n++] = (char)(d < 10 ? '0'+d : 'a'+d-10); v /= (unsigned long)base; }
    for (int i=0;i<n;i++) out[i] = tmp[n-1-i];
    *len = n;
}
/* Shared by __squash_fprintf_impl (owns its own va_start/va_end) and
 * __squash_vfprintf_impl (receives an already-started va_list from its
 * caller, e.g. diag.c's diag_emit — see squash's vfprintf shim below for
 * why that needs its own entry point rather than reusing this file's
 * fprintf one). Writes formatted output into `buf` (capacity `bufsize`,
 * same "%d %u %x %p %s %c %f/%.Nf %%" coverage as before) and returns the
 * byte count; does NOT call va_end (that's the caller's, since the caller
 * is also who called va_start) and does NOT write anywhere (that's the
 * caller's too, since only the caller knows which real fd/FILE* `f` maps
 * to). */
extern char *getenv(const char *name);
static int __squash_vformat_impl(char *buf, int bufsize, const char *fmt, va_list ap) {
    int bl = 0;
    for (const char *p = fmt; *p && bl < bufsize-32; p++) {
        if (*p != '%') { buf[bl++] = *p; continue; }
        p++;
        /* Field width: either a literal digit run ("%4d") or "*" (the width
         * is itself a variadic int argument, e.g. diag.c's own gutter-
         * padding "%*s" for the caret-drawing row). Only %s actually pads
         * with this (the one real caller); for every other conversion the
         * width is consumed from the va_list (so subsequent args don't
         * shift) but otherwise ignored. Previously "*" fell all the way to
         * the "unrecognized specifier" default case below, which emitted
         * the two characters "%*" literally and — critically — never
         * called va_arg for the width, so every va_arg after it read one
         * slot behind where the caller expected: an int (e.g. the gutter
         * width, 2) came back reinterpreted as a %s's string pointer,
         * segfaulting (or reading `2` as an address) the moment that
         * later %s tried to dereference it. */
        int width = 0;
        if (*p == '*') { width = va_arg(ap, int); p++; }
        else { while (*p>='0'&&*p<='9') { width = width*10 + (*p-'0'); p++; } }
        int prec = -1;
        if (*p == '.') { p++; prec = 0; while (*p>='0'&&*p<='9') { prec = prec*10 + (*p-'0'); p++; } }
        while (*p=='l') p++; /* skip length modifiers (%ld etc.) */
        char tmp[32]; int tn;
        switch (*p) {
        case 'd': case 'i': {
            long v = va_arg(ap, int);
            if (v < 0) { buf[bl++] = '-'; v = -v; }
            __squash_fmt_utoa((unsigned long)v, 10, tmp, &tn);
            for (int i=0;i<tn && bl<bufsize-1;i++) buf[bl++] = tmp[i];
            break;
        }
        case 'u': {
            unsigned long v = (unsigned int)va_arg(ap, unsigned int);
            __squash_fmt_utoa(v, 10, tmp, &tn);
            for (int i=0;i<tn && bl<bufsize-1;i++) buf[bl++] = tmp[i];
            break;
        }
        case 'x': {
            unsigned long v = (unsigned int)va_arg(ap, unsigned int);
            __squash_fmt_utoa(v, 16, tmp, &tn);
            for (int i=0;i<tn && bl<bufsize-1;i++) buf[bl++] = tmp[i];
            break;
        }
        case 'p': {
            unsigned long v = (unsigned long)va_arg(ap, void*);
            buf[bl++] = '0'; buf[bl++] = 'x';
            __squash_fmt_utoa(v, 16, tmp, &tn);
            for (int i=0;i<tn && bl<bufsize-1;i++) buf[bl++] = tmp[i];
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int slen = 0; for (const char *sp=s; *sp; sp++) slen++;
            for (int pad=width-slen; pad>0 && bl<bufsize-1; pad--) buf[bl++] = ' ';
            while (*s && bl < bufsize-1) buf[bl++] = *s++;
            break;
        }
        case 'c': {
            int c = va_arg(ap, int);
            buf[bl++] = (char)c;
            break;
        }
        case 'f': {
            double v = va_arg(ap, double);
            if (v < 0) { buf[bl++] = '-'; v = -v; }
            if (prec < 0) prec = 6;
            unsigned long ip = (unsigned long)v;
            double frac = v - (double)ip;
            for (int k=0;k<prec;k++) frac *= 10.0;
            unsigned long fp = (unsigned long)(frac + 0.5);
            __squash_fmt_utoa(ip, 10, tmp, &tn);
            for (int i=0;i<tn && bl<bufsize-1;i++) buf[bl++] = tmp[i];
            if (prec > 0) {
                buf[bl++] = '.';
                char fbuf[16]; int fn;
                __squash_fmt_utoa(fp, 10, fbuf, &fn);
                for (int k=fn;k<prec;k++) buf[bl++] = '0'; /* left-pad fraction with zeros */
                for (int i=0;i<fn && bl<bufsize-1;i++) buf[bl++] = fbuf[i];
            }
            break;
        }
        case '%': buf[bl++] = '%'; break;
        default: buf[bl++] = '%'; if (*p && bl<bufsize-1) buf[bl++] = *p; break;
        }
    }
    return bl;
}
static int __squash_fprintf_impl(FILE *f, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int bl = __squash_vformat_impl(buf, (int)sizeof(buf), fmt, ap);
    va_end(ap);
    if (f == stdout || f == stderr) {
        write(f == stdout ? 1 : 2, buf, (unsigned long)bl);
        return bl;
    }
    return (int)fwrite(buf, 1, (size_t)bl, f);
}
#define fprintf __squash_fprintf_impl
/* vfprintf(f, fmt, ap): same sentinel-aware routing as fprintf above, for
 * the va_list-already-in-hand case (this project's own diag.c's diag_emit
 * calls it directly). Without this, "vfprintf(stdout, ...)" was left
 * undeclared -- routed through the generic "assume libc.so.6" fallback
 * straight to REAL system vfprintf, which dereferences whatever it's
 * given expecting a genuine FILE* -- and stdout/stderr here are the same
 * sentinel pointer values ((void*)1/(void*)2) as everywhere else in this
 * file, not real ones, so that segfaulted deep inside libSystem's
 * flockfile the moment any code path called it with the "stdout" macro
 * still in scope -- confirmed exactly this way via squash self-hosting
 * itself for -macos (diag.c's own diag_emit calls vfprintf(stdout,...)
 * for every diagnostic message). */
static int __squash_vfprintf_impl(FILE *f, const char *fmt, va_list ap) {
    char buf[1024];
    int bl = __squash_vformat_impl(buf, (int)sizeof(buf), fmt, ap);
    if (f == stdout || f == stderr) {
        write(f == stdout ? 1 : 2, buf, (unsigned long)bl);
        return bl;
    }
    return (int)fwrite(buf, 1, (size_t)bl, f);
}
#define vfprintf __squash_vfprintf_impl

/* fputc(c, stdout)/fputc(c, stderr): same sentinel problem as fprintf/
 * vfprintf above, for the single-character case -- diag.c's diag_emit uses
 * this to draw its GCC-style "^~~~~" caret line one column at a time
 * (needed because the leading whitespace must reproduce the source line's
 * own tabs-vs-spaces exactly). fputc has no dedicated shim of its own here
 * at all (unlike fprintf/vfprintf), so it was left as a bare bodyless
 * prototype -- routed through the generic "assume libc.so.6" fallback
 * straight to REAL system fputc, which dereferences whatever FILE* it's
 * given. Confirmed exactly this way via squash self-hosting itself for
 * -macos: a warning whose flagged identifier text is found verbatim in its
 * own source line (column tracking succeeds, e.g. any real "implicit
 * declaration of function 'NAME'" diagnostic where NAME literally appears
 * on the reported line) takes the caret-line branch and calls
 * "fputc(ch, stdout)" -- segfaulting deep inside libSystem's flockfile,
 * the same failure this file's fprintf/vfprintf comments already describe
 * in more detail. fputs is fixed alongside it for the same reason, even
 * though nothing in this codebase happens to call it on stdout/stderr
 * today -- it has the exact same bodyless-prototype gap and would fail
 * identically the moment something did. */
static int __squash_fputc_impl(int c, FILE *f) {
    unsigned char ch = (unsigned char)c;
    if (f == stdout || f == stderr) {
        write(f == stdout ? 1 : 2, &ch, 1);
        return c;
    }
    return (int)fwrite(&ch, 1, 1, f) == 1 ? c : EOF;
}
#define fputc __squash_fputc_impl
static int __squash_fputs_impl(const char *s, FILE *f) {
    int len = 0;
    while (s[len]) len++;
    if (f == stdout || f == stderr) {
        write(f == stdout ? 1 : 2, s, (unsigned long)len);
        return len;
    }
    int written = (int)fwrite(s, 1, (size_t)len, f);
    return written == len ? len : EOF;
}
#define fputs __squash_fputs_impl
#endif

#endif /* _STDIO_H */

/* Unified diagnostic reporting — see diag.h for the rationale and API.
 *
 * Before this file existed, every stage of the compiler printed its own
 * ad hoc diagnostics: the parser/lexer had "file:line:col: error: msg", but
 * codegen (which only ever sees ASTNodes, built long after preprocessing
 * flattened every #include into one buffer) just did
 * `printf("codegen: undefined '%s' at line %d\n", name, n->line)` — a line
 * number counted across the ENTIRE collated translation unit, with no file,
 * no enclosing function, and no hint about what the user might have meant.
 * For a real build (SDL3's own unity build, or anything #including real
 * system headers) that line number is close to meaningless on its own —
 * you have no idea whether it's your own code or three levels deep in a
 * header. This file fixes that for every stage at once. */
#include "diag.h"
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#if defined(_WIN32)
#include <io.h>
#define DIAG_ISATTY(fd) _isatty(fd)
#else
#include <unistd.h>
#define DIAG_ISATTY(fd) isatty(fd)
#endif

static int g_diag_inited = 0;
static int g_diag_color = 0;

static void diag_ensure_init(void) {
    if (g_diag_inited) return;
    g_diag_inited = 1;
    /* Adaptive like GCC's default (-fdiagnostics-color=auto): color only
     * when stdout is a real terminal, plain when redirected/piped/logged.
     * NO_COLOR (https://no-color.org/) always wins if set to anything. */
    const char *no_color = getenv("NO_COLOR");
    /* Deliberately isatty(1), NOT isatty(fileno(stdout)): squash's own
     * preprocessor (lexer.c's preprocess()) unconditionally defines
     * "stdout" as the sentinel "((void*)1)" for every file it compiles —
     * a convention diag.c's fprintf/printf shims rely on to route output
     * without needing a real FILE* — but that same substitution also
     * shadows THIS file's own "stdout" the moment squash compiles itself
     * (self-hosting): fileno() then receives the literal pointer value 1,
     * not a real FILE*, and real libc's fileno()/isatty() dereferences it,
     * segfaulting deep inside libSystem's flockfile (confirmed via a
     * from-scratch repro tracing squash self-hosting itself for -macos).
     * Stdout's file descriptor is always 1 on both POSIX (STDOUT_FILENO)
     * and the Windows CRT by convention, so fileno() was only ever
     * computing this same constant — skip the indirection entirely rather
     * than trying to make "stdout" mean two different things depending on
     * who's compiling this file. */
    g_diag_color = DIAG_ISATTY(1) && !(no_color && no_color[0]);
}

#define C_RESET   (g_diag_color ? "\033[0m"    : "")
#define C_BOLD    (g_diag_color ? "\033[1m"    : "")
#define C_ITALIC  (g_diag_color ? "\033[3m"    : "")
#define C_ERR     (g_diag_color ? "\033[1;31m" : "")
#define C_WARN    (g_diag_color ? "\033[1;35m" : "")
#define C_NOTE    (g_diag_color ? "\033[1;36m" : "")
/* GCC-style source-context decoration: the line-number gutter ("  42 | ")
 * is grey so it reads as decoration, not code; the actual source line
 * (the language construct the diagnostic is about) is cyan so it stands
 * out from the plain-colored message text above it; the caret/underline
 * takes the SAME color as the diagnostic's own severity (red for an
 * error, magenta for a warning) — matching real GCC, and giving a direct
 * visual link between "this is what's wrong" and "here's exactly where". */
#define C_GUTTER  (g_diag_color ? "\033[90m"   : "")
#define C_SRCLINE (g_diag_color ? "\033[96m"   : "")

/* Best-effort: reads the target file directly off disk to fetch the
 * exact text of one line for display — squash doesn't keep every
 * original source file's text resident after preprocessing flattens
 * everything into one buffer, but diagnostics are rare enough that a
 * lazy re-read here is negligible. */
static int diag_fetch_line(const char *file, int line_no, char *buf, size_t bufsz) {
    if (!file || line_no <= 0) return 0;
    FILE *fp = fopen(file, "rb");
    if (!fp) return 0;
    char tmp[4096];
    int cur = 0;
    int found = 0;
    while (fgets(tmp, sizeof tmp, fp)) {
        cur++;
        if (cur == line_no) {
            size_t l = strlen(tmp);
            while (l > 0 && (tmp[l-1]=='\n' || tmp[l-1]=='\r')) tmp[--l] = '\0';
            snprintf(buf, bufsz, "%s", tmp);
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}

static int g_diag_error_count = 0;

int diag_error_count(void) { return g_diag_error_count; }

void diag_emit(DiagSeverity sev, int merged_line, const char *function_name,
               const char *near_text, const char *fmt, ...) {
    diag_ensure_init();
    if (sev == DIAG_ERROR) g_diag_error_count++;

    const char *sev_color = (sev==DIAG_ERROR) ? C_ERR : (sev==DIAG_WARNING) ? C_WARN : C_NOTE;
    const char *sev_word  = (sev==DIAG_ERROR) ? "error" : (sev==DIAG_WARNING) ? "warning" : "note";

    int local_line = -1;
    const char *file = (merged_line >= 0) ? linemap_resolve(merged_line, &local_line) : NULL;

    /* Fetch the source line and locate near_text within it BEFORE printing
     * the header — a real column number (GCC's "file:line:col:", not just
     * "file:line:") depends on knowing where near_text starts, so this has
     * to happen first. col0 is 0-based; the header prints col0+1 (GCC's
     * columns are 1-based, like its lines). */
    char srcline[4096];
    int have_srcline = 0;
    int col0 = -1;
    if (file) have_srcline = diag_fetch_line(file, local_line, srcline, sizeof srcline);
    if (have_srcline && near_text && near_text[0]) {
        const char *pos = strstr(srcline, near_text);
        if (pos) col0 = (int)(pos - srcline);
    }

    if (file && col0 >= 0) {
        fprintf(stdout, "%s%s:%d:%d:%s %s%s:%s ", C_BOLD, file, local_line, col0+1, C_RESET, sev_color, sev_word, C_RESET);
    } else if (file) {
        fprintf(stdout, "%s%s:%d:%s %s%s:%s ", C_BOLD, file, local_line, C_RESET, sev_color, sev_word, C_RESET);
    } else if (merged_line >= 0) {
        /* No line map yet (or this line predates preprocessing) — still
         * better than nothing, but flag it as an unresolved/raw position. */
        fprintf(stdout, "%s<input>:%d:%s %s%s:%s ", C_BOLD, merged_line, C_RESET, sev_color, sev_word, C_RESET);
    } else {
        fprintf(stdout, "%ssquash:%s %s%s:%s ", C_BOLD, C_RESET, sev_color, sev_word, C_RESET);
    }

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");

    if (function_name && function_name[0]) {
        fprintf(stdout, "  %sin function '%s'%s\n", C_ITALIC, function_name, C_RESET);
    }

    if (have_srcline) {
        /* GCC-style gutter: the real line number (grey — decoration, not
         * code) then "| " then the actual source line (cyan — the
         * language construct this diagnostic is about). The caret row
         * below re-uses the same gutter width so the two line up exactly,
         * with the caret and its trailing tildes spanning near_text's full
         * width, colored to match the diagnostic's own severity. */
        char linebuf[16];
        snprintf(linebuf, sizeof linebuf, "%d", local_line);
        int gutter_w = (int)strlen(linebuf);
        fprintf(stdout, " %s%s%s |%s %s%s%s\n", C_GUTTER, linebuf, C_RESET, C_RESET, C_SRCLINE, srcline, C_RESET);
        if (col0 >= 0) {
            size_t taillen = strlen(near_text);
            fprintf(stdout, " %s%*s |%s ", C_GUTTER, gutter_w, "", C_RESET);
            for (int i=0;i<col0;i++) fputc(srcline[i]=='\t' ? '\t' : ' ', stdout);
            fprintf(stdout, "%s^", sev_color);
            for (size_t i=1;i<taillen;i++) fputc('~', stdout);
            fprintf(stdout, "%s\n", C_RESET);
        }
    }
    /* Diagnostics must appear promptly and in the right relative order next
     * to any other stdout/stderr output the compiler produces (progress
     * messages, [funcaddr]/debug traces, etc.) — without this, stdout's
     * default full buffering when redirected to a file/pipe (unlike
     * stderr's line buffering) silently reorders diagnostics to the end of
     * the captured output instead of appearing where they actually occurred. */
    fflush(stdout);
}

/* Classic O(len_a*len_b) edit-distance, two rolling rows — plenty fast for
 * comparing one bad identifier against a scope's worth of symbol names. */
static int diag_levenshtein(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la == 0) return lb;
    if (lb == 0) return la;
    int *prev = malloc((size_t)(lb+1) * sizeof(int));
    int *cur  = malloc((size_t)(lb+1) * sizeof(int));
    for (int j=0;j<=lb;j++) prev[j] = j;
    for (int i=1;i<=la;i++) {
        cur[0] = i;
        for (int j=1;j<=lb;j++) {
            int cost = (a[i-1]==b[j-1]) ? 0 : 1;
            int del = prev[j]+1, ins = cur[j-1]+1, sub = prev[j-1]+cost;
            int m = (del<ins) ? del : ins;
            cur[j] = (m<sub) ? m : sub;
        }
        int *t = prev; prev = cur; cur = t;
    }
    int result = prev[lb];
    free(prev); free(cur);
    return result;
}

const char *diag_suggest_name(SymTable *st, const char *bad_name) {
    if (!st || !bad_name || !bad_name[0]) return NULL;
    const char *best = NULL;
    int best_dist = 1000000;
    for (Scope *sc = st->current; sc; sc = sc->parent) {
        for (Symbol *s = sc->head; s; s = s->next) {
            if (!s->name || strcmp(s->name, bad_name)==0) continue;
            int d = diag_levenshtein(bad_name, s->name);
            if (d < best_dist) { best_dist = d; best = s->name; }
        }
    }
    /* badlen/3 alone is too strict for a common real-world typo class: a
     * single adjacent-letter transposition ("totla" for "total") costs 2
     * substitutions under plain Levenshtein distance, not 1, and a short-
     * to-medium identifier's badlen/3 rounds down to 1 — rejecting exactly
     * the kind of typo this feature exists to catch. badlen/2 (floor 1,
     * ceiling 4) still refuses wildly-unrelated names for short
     * identifiers while comfortably covering single transpositions. */
    int badlen = (int)strlen(bad_name);
    int threshold = badlen/2; if (threshold<1) threshold=1; if (threshold>4) threshold=4;
    return (best && best_dist <= threshold) ? best : NULL;
}

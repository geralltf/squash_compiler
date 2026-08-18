#ifndef DIAG_H
#define DIAG_H
#include "symtable.h"

/* Unified diagnostic reporting for the whole compiler (preprocessor,
 * lexer, parser, codegen) — see diag.c's header comment for the full
 * rationale. Every diagnostic gets:
 *   - the REAL originating file + line (via lexer.h's linemap_resolve()),
 *     instead of a raw line number into the entire flattened/#include-
 *     merged translation unit;
 *   - the enclosing function name, when known;
 *   - the actual source line, with a best-effort caret under the token
 *     text that triggered it;
 *   - a "did you mean 'x'?" suggestion for undefined-identifier errors,
 *     using the live symbol table;
 *   - GCC-style coloring (bold red "error:", bold magenta "warning:", bold
 *     cyan "note:"), auto-detected from whether stdout is a terminal (and
 *     respecting the NO_COLOR convention) — plain/uncolored otherwise, e.g.
 *     when piped into a log file. */

typedef enum { DIAG_ERROR, DIAG_WARNING, DIAG_NOTE } DiagSeverity;

/* merged_line: an ASTNode's/Token's ->line, or -1 for a diagnostic with no
 *   source position at all (CLI/usage errors).
 * function_name: enclosing function, or NULL if unknown/not applicable.
 * near_text: the identifier/token spelling to locate (best-effort, via a
 *   plain substring search) within the resolved source line, to draw a
 *   caret under — NULL to just show the line with no caret, or skip the
 *   line entirely if merged_line resolves to nothing.
 * fmt/...: printf-style message (no trailing newline). */
void diag_emit(DiagSeverity sev, int merged_line, const char *function_name,
               const char *near_text, const char *fmt, ...);

/* Look up the closest-spelled name to `bad_name` among every symbol
 * currently visible in `st` (walks the live Scope chain from st->current
 * up through every enclosing scope) and return it if within a reasonable
 * edit distance, else NULL. The returned pointer is owned by the symbol
 * table (a live Symbol's ->name) — never free it. */
const char *diag_suggest_name(SymTable *st, const char *bad_name);

/* Total number of DIAG_ERROR diagnostics emitted so far this process
 * (warnings/notes don't count). The compiler is designed to keep
 * recovering and compiling past a codegen-level error (substituting 0,
 * skipping a bad statement, etc.) so a single build can surface every
 * problem at once instead of stopping at the first one — but the
 * resulting binary is broken, so the driver checks this count after
 * codegen finishes and refuses to actually write an executable (or
 * object file) if it's nonzero. See compiler.c's use of this right
 * before every output-writing path. */
int diag_error_count(void);

#endif

/*
 * test_diagnostics_syntax_error.c
 *
 * Deliberately has a real syntax error (a missing semicolon) -- unlike
 * every case in test_diagnostics_errors.c (non-fatal codegen-level
 * errors, which squash recovers from and keeps compiling past), a genuine
 * PARSER-level error is fatal: squash reports it and aborts immediately,
 * so this needs its own file/compile rather than sharing one with cases
 * that expect to finish compiling.
 *
 * Expected: one "error:" diagnostic, with the real file:line, the actual
 * source line, and a caret under the token the parser choked on --
 * followed by squash exiting without producing an executable.
 */
#include <stdio.h>

int main(void) {
    int x = 5
    printf("x=%d\n", x);
    return 0;
}

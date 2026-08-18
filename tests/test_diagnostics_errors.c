/*
 * test_diagnostics_errors.c
 *
 * Deliberately broken test file — every construct below triggers a real,
 * NON-fatal codegen-level "error:" diagnostic (squash recovers by
 * substituting 0 / skipping the bad statement and keeps compiling, so all
 * of these can be demonstrated in a single build instead of needing a
 * separate run per case). Exercises the diagnostics overhaul from
 * [[project_diagnostics_overhaul]]:
 *
 *  1. A typo'd identifier close enough to a real one in scope to trigger
 *     the "did you mean 'x'?" spelling suggestion.
 *  2. An undefined identifier used as an ASSIGNMENT TARGET (lvalue), a
 *     separate code path from a plain undefined-identifier READ.
 *  3. `break` outside any loop/switch.
 *  4. `continue` outside any loop.
 *  5. An undefined identifier referenced from INSIDE an included header
 *     (test_diagnostics_errors.h) — demonstrates real file+line
 *     attribution across a #include boundary, not just within this file.
 *
 * Expected: squash reports FIVE separate "error:" diagnostics (one per
 * numbered case below), each with the real file:line, the enclosing
 * function name, the offending source line, and a caret — then still
 * finishes compiling and writes a (deliberately non-functional) .exe,
 * since none of these are fatal parser-level errors.
 *
 * Compare against test_diagnostics_warning.c (a real "warning:", not an
 * "error:") and test_diagnostics_syntax_error.c (a FATAL parser-level
 * error, which aborts compilation immediately instead of recovering) —
 * kept as separate files since a fatal error can't be demonstrated
 * alongside anything else in the same compile.
 */
#include <stdio.h>
#include "test_diagnostics_errors.h"

/* Case 1: typo'd identifier -- "did you mean 'total'?" */
static int demo_typo(void) {
    int total = 0;
    total = total + 1;
    printf("typo case: %d\n", totla); /* 'totla' undefined, real typo of 'total' */
    return total;
}

/* Case 2: undefined identifier used as an lvalue (assignment target) */
static int demo_undefined_lvalue(void) {
    undeclared_counter = 5; /* 'undeclared_counter' never declared, assigned to */
    return 0;
}

/* Case 3 & 4: break/continue outside any loop or switch */
static void demo_break_continue_outside_loop(void) {
    printf("before stray break\n");
    break;    /* not inside a loop or switch */
    printf("before stray continue\n");
    continue; /* not inside a loop */
}

int main(void) {
    demo_typo();
    demo_undefined_lvalue();
    demo_break_continue_outside_loop();
    /* Case 5: undefined identifier inside an INCLUDED HEADER */
    printf("from header: %d\n", compute_from_header(10));
    return 0;
}

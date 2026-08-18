/*
 * test_diagnostics_missing_include.c
 *
 * A #include that can't be resolved anywhere (built-in include/, -I search
 * dirs, or relative to this file) used to just print a bare
 * "preprocessor: cannot find '...'" message and silently `continue` past
 * it — invisible to diag_error_count(), so it never counted toward the
 * final "compilation failed: N errors" tally (compiler.c's bail-out check,
 * see [[project_diagnostics_overhaul]]) even though it's a genuine,
 * primary compile error. A build could report a small/zero error count
 * despite a missing header being the real root cause of everything that
 * went wrong downstream.
 *
 * Expected: one real "error:" diagnostic with the correct file:line (this
 * file, the #include line below) and the exact requested name/bracket
 * style preserved, AND it counts toward "compilation failed: N errors" —
 * no executable gets written, exit code 1.
 */
#include <stdio.h>
#include "this_header_does_not_exist_anywhere.h"

int main(void) {
    printf("this should never actually run\n");
    return 0;
}

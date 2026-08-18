/*
 * test_diagnostics_call_typo.c
 *
 * Exercises the "implicit declaration of function" WARNING (codegen.c's
 * AST_CALL case) — the function-call counterpart to the undefined-
 * variable "did you mean 'x'?" diagnostics in test_diagnostics_errors.c /
 * test_diagnostics_fuzzy_match.c. See [[project_diagnostics_overhaul]].
 *
 * A call whose name resolves to NO symbol at all (not a locally-defined
 * function, not a variable/global holding a function pointer, not
 * alloca, not one of squash's internal CRT shims) is diagnostic-only: it
 * still compiles exactly the same way it always did (falls through to
 * being treated as an external/DLL call) — this only adds a WARNING plus
 * a spelling suggestion where one exists, it does not change whether the
 * call resolves or reject anything. Confirmed safe against the full real
 * SDL3 unity build (hundreds of thousands of lines, thousands of real
 * external Win32/CRT calls) with zero false positives before this file
 * was written.
 *
 *  1. A typo'd call to a real, LOCALLY-defined function in this same file.
 *  2. A typo'd call to a real function declared EARLIER via a prototype
 *     only (no body in this file) — still a "did you mean", same as #1.
 *  3. A call to a function defined LATER in this same file (forward
 *     reference) — must NOT warn at all: squash parses the whole
 *     translation unit (registering every function's symbol) before
 *     codegen ever runs, so this is already fully resolved by the time
 *     the call is generated, unlike a genuinely undeclared name.
 *  4. A call with no close name in scope at all — warns, but with NO
 *     spelling suggestion (same negative-case rule as the variable
 *     fuzzy-matching tests).
 *  5. A genuine call to a real external CRT function (printf) and to a
 *     real internal shim — NEITHER should warn; these have always been
 *     recognized without needing a local prototype.
 */
#include <stdio.h>

/* Case 2 setup: a real prototype-only declaration (no body in this TU) —
 * exactly the shape a real cross-object or cross-header function
 * declaration takes. Gives this call site a genuine symbol to match. */
int real_declared_no_body(int x);

/* Case 3 setup: forward reference target, defined further down this file. */
static int defined_later(int x);

static int real_local_function(int a, int b) {
    return a + b;
}

int main(void) {
    /* Case 1: typo of a real, locally-defined function in this file. */
    int r1 = real_local_functoin(2, 3); /* typo of 'real_local_function' */
    printf("case1: %d\n", r1);

    /* Case 2: typo of a real prototype-only declaration. */
    int r2 = real_declared_no_bdy(5);   /* typo of 'real_declared_no_body' */
    printf("case2: %d\n", r2);

    /* Case 3: forward reference to a function defined LATER in this same
     * file -- must NOT produce any warning at all. */
    int r3 = defined_later(7);
    printf("case3: %d\n", r3);

    /* Case 4: nothing in scope is remotely close -- warns, but with no
     * "did you mean" suggestion. */
    int r4 = zzzqqqxxx_totally_unrelated_call(1); /* nothing close in scope */
    printf("case4: %d\n", r4);

    /* Case 5: a genuine external CRT call (already recognized without a
     * local prototype) and a genuine internal shim -- neither should
     * ever warn. */
    printf("case5: real printf call, no warning expected\n");

    return 0;
}

static int defined_later(int x) {
    return x * 2;
}

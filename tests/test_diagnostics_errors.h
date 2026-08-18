/*
 * test_diagnostics_errors.h
 *
 * Deliberately references an undefined identifier from INSIDE a header, so
 * test_diagnostics_errors.c's compile demonstrates that squash's diagnostics
 * correctly attribute the error to THIS file (test_diagnostics_errors.h)
 * and the real line below — not to the top-level .c file that included it,
 * and not to a meaningless line count into the whole flattened/#include-
 * merged translation unit (see [[project_diagnostics_overhaul]]).
 */
#ifndef TEST_DIAGNOSTICS_ERRORS_H
#define TEST_DIAGNOSTICS_ERRORS_H

static int compute_from_header(int base) {
    int result = base;
    result = result + value_never_declared_anywhere; /* undefined identifier, real bug, deliberate */
    return result;
}

#endif

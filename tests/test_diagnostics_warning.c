/*
 * test_diagnostics_warning.c
 *
 * Deliberately has NO function named "main" (or any other recognizable
 * entry point) — triggers compiler.c's real "warning:" diagnostic (not an
 * "error:") about the entry point silently defaulting to offset 0 in
 * .text, which is almost certainly not what the caller wants. This is the
 * most common REAL way this warning fires in practice: a header (e.g.
 * SDL3's own <SDL3/SDL_main.h>, "#define main SDL_main") renames a real
 * main() to something else and the rename isn't #undef'd back before the
 * real definition, so the linker/entry-point logic never finds a function
 * literally named "main".
 *
 * Compare against test_diagnostics_errors.c (five separate non-fatal
 * "error:" diagnostics) and test_diagnostics_syntax_error.c (a fatal
 * parser-level "error:" that aborts compilation immediately).
 */
#include <stdio.h>

/* Mimics SDL3's own SDL_main.h convention -- renames "main" away, and
 * (deliberately, for this demo) never #undefs it back before defining a
 * real entry function under that renamed name. */
#define main not_actually_main

int main(void) {
    printf("this never runs under its literal name -- it's really 'not_actually_main'\n");
    return 0;
}

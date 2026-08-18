/*
 * test_linker_error_helper.c
 *
 * Compiled separately (via "squash -c ...") into a ".sqo" object file
 * (see objfile.h) with a DIFFERENT target than the final build that
 * later tries to link it — see tests/test_linker_diagnostics.sh, which
 * builds this as -32 and then links it into a -64 build of
 * test_linker_error_main.c, to demonstrate compiler.c's real "'%s' was
 * compiled for a different target" LINKER ERROR (and, since squash now
 * refuses to write an executable when any error occurs, confirms no
 * .exe gets produced either).
 */
int linker_error_helper(int x) {
    return x * 2;
}

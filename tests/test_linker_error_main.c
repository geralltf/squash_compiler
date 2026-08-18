/*
 * test_linker_error_main.c
 *
 * Calls a function only DECLARED here, expected to be supplied by a
 * separately-compiled ".sqo" object at link time (see
 * test_linker_error_helper.c and tests/test_linker_diagnostics.sh, which
 * drives the actual squash invocations demonstrating:
 *   - linking a .sqo built for a different target (-32 vs -64)   -> error
 *   - linking a .sqo path that doesn't exist on disk             -> error
 *   - passing -c together with a .sqo link input                -> error
 * all three are genuine LINKER-stage errors, not parser/codegen ones —
 * none of them are triggered by anything in the .c source itself, only
 * by the specific combination of CLI flags/inputs used to invoke squash.
 */
int linker_error_helper(int x);

int main(void) {
    return linker_error_helper(21) == 42 ? 0 : 1;
}

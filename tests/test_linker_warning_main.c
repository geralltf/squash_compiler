/*
 * test_linker_warning_main.c
 *
 * Trivial program used to demonstrate the LINKER-stage "library not
 * found" WARNING (winlinker.c / linker.c) — the warning itself comes
 * entirely from a CLI flag (-l some_missing_library), not from anything
 * in this file's content, so this just needs to be any file that compiles
 * cleanly. See tests/test_linker_diagnostics.sh for the actual invocation
 * ("squash -windows -64 -l totally_nonexistent_library_xyz
 * test_linker_warning_main.c -o out.exe") and expected output.
 *
 * Unlike a codegen/parser ERROR, this is only a WARNING: the build still
 * succeeds and writes a real executable — a missing library only becomes
 * a hard problem if the program actually needed a symbol from it, which
 * this trivial program deliberately does not.
 */
int main(void) {
    return 0;
}

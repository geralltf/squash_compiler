#!/usr/bin/env bash
#
# test_linker_diagnostics.sh
#
# Demonstrates and verifies squash's LINKER-stage diagnostics — distinct
# from the parser/codegen diagnostics in test_diagnostics_*.c: these only
# fire during the final link/output-writing stage, driven by a specific
# combination of CLI flags and link inputs rather than by anything in a
# single source file's content, so they need a small driver script
# instead of a plain "squash <file>" invocation. See
# [[project_diagnostics_overhaul]] for the overall diagnostics rework
# this belongs to (unified through diag_emit(), consistent GCC-style
# formatting, adaptive color).
#
# Run from the repo root:  bash tests/test_linker_diagnostics.sh
# Requires squash.exe already built at the repo root (../squash.exe
# relative to this script).
#
#  1. WARNING: -l references a library that doesn't exist on disk
#     (winlinker.c/linker.c) -- non-fatal, the build still succeeds and
#     writes a real executable.
#  2. ERROR: linking a real ".sqo" object file that was compiled for a
#     DIFFERENT target (-32 vs this build's -64) -- compiler.c's target-
#     mismatch check. Confirms no executable gets written (squash now
#     refuses to write output once ANY error has been reported).
#  3. ERROR: linking a ".sqo" path that doesn't exist on disk at all
#     (objfile.c's objfile_read).
#  4. ERROR: passing both "-c" (compile to object file) and a ".sqo" link
#     input in the same invocation -- mutually exclusive by construction.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SQUASH="$REPO_ROOT/squash.exe"
TMP="${TMPDIR:-/tmp}/squash_linker_test_$$"
mkdir -p "$TMP"
fail=0

if [ ! -f "$SQUASH" ]; then
    echo "squash.exe not found at $SQUASH -- build it first"
    exit 1
fi

check_exit() {
    local desc="$1" expect="$2" actual="$3"
    if [ "$actual" != "$expect" ]; then
        echo "FAIL: $desc -- expected exit $expect, got $actual"
        fail=1
        return 1
    fi
    return 0
}

check_contains() {
    local desc="$1" outfile="$2" needle="$3"
    if ! grep -qF "$needle" "$outfile"; then
        echo "FAIL: $desc -- expected output to contain: $needle"
        echo "--- actual output ---"
        cat "$outfile"
        echo "---------------------"
        fail=1
        return 1
    fi
    return 0
}

echo "=== Case 1: linker WARNING -- library not found (-l), build still succeeds ==="
"$SQUASH" -windows -64 -l totally_nonexistent_library_xyz \
    "$SCRIPT_DIR/test_linker_warning_main.c" -o "$TMP/case1.exe" > "$TMP/case1.out" 2>&1
c1_exit=$?
if check_exit "case1 exit code" 0 "$c1_exit" \
   && check_contains "case1 warning text" "$TMP/case1.out" \
        "library 'totally_nonexistent_library_xyz' not found"; then
    if [ -f "$TMP/case1.exe" ]; then
        echo "PASS: case1 -- warning shown, executable still written"
    else
        echo "FAIL: case1 -- executable was NOT written despite only a warning"
        fail=1
    fi
fi

echo "=== Case 2: linker ERROR -- .sqo compiled for a different target ==="
"$SQUASH" -c -windows -32 "$SCRIPT_DIR/test_linker_error_helper.c" \
    -o "$TMP/helper32.sqo" > "$TMP/case2_build.out" 2>&1
"$SQUASH" -windows -64 "$SCRIPT_DIR/test_linker_error_main.c" "$TMP/helper32.sqo" \
    -o "$TMP/case2.exe" > "$TMP/case2.out" 2>&1
c2_exit=$?
if check_exit "case2 exit code" 1 "$c2_exit" \
   && check_contains "case2 error text" "$TMP/case2.out" "was compiled for a different target"; then
    if [ -f "$TMP/case2.exe" ]; then
        echo "FAIL: case2 -- executable WAS written despite a target-mismatch error"
        fail=1
    else
        echo "PASS: case2 -- error shown, no executable written"
    fi
fi

echo "=== Case 3: linker ERROR -- missing .sqo file ==="
"$SQUASH" -windows -64 "$SCRIPT_DIR/test_linker_error_main.c" "$TMP/does_not_exist.sqo" \
    -o "$TMP/case3.exe" > "$TMP/case3.out" 2>&1
c3_exit=$?
if check_exit "case3 exit code" 1 "$c3_exit" \
   && check_contains "case3 error text" "$TMP/case3.out" "cannot open object file"; then
    if [ -f "$TMP/case3.exe" ]; then
        echo "FAIL: case3 -- executable WAS written despite a missing-object error"
        fail=1
    else
        echo "PASS: case3 -- error shown, no executable written"
    fi
fi

echo "=== Case 4: linker ERROR -- -c and .sqo link input are mutually exclusive ==="
"$SQUASH" -c -windows -64 "$SCRIPT_DIR/test_linker_error_main.c" "$TMP/helper32.sqo" \
    -o "$TMP/case4.sqo" > "$TMP/case4.out" 2>&1
c4_exit=$?
if check_exit "case4 exit code" 1 "$c4_exit" \
   && check_contains "case4 error text" "$TMP/case4.out" "mutually exclusive"; then
    echo "PASS: case4 -- mutually-exclusive error shown"
fi

rm -rf "$TMP"

if [ "$fail" = "1" ]; then
    echo "=== SOME LINKER DIAGNOSTIC TESTS FAILED ==="
    exit 1
else
    echo "=== all linker diagnostic tests passed ==="
    exit 0
fi

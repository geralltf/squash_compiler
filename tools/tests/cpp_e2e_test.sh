#!/usr/bin/env bash
# End-to-end test suite for squash's C++ frontend (CPP/cpp_lexer.c,
# cpp_parser.c, cpp_lower.c -- see CPP/cpp_lower.h for the frontend's
# documented scope). Compiles every CPP/tests/*.cpp program with squash,
# runs the resulting executable, and diffs its actual stdout against the
# matching CPP/tests/*.expected file -- proving the whole pipeline (lex,
# parse, lower to C, preprocess, codegen, link) end to end, not just that
# the lowering pass runs without crashing. Mirrors
# tools/tests/objfile_merge_test.sh's own shape ([OK]/[FAIL] lines, a
# summary PASS/FAIL, matching exit code).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SQUASH="$REPO_ROOT/squash"
TESTS_DIR="$REPO_ROOT/CPP/tests"
INCLUDE_DIR="$REPO_ROOT/include"

if [ ! -x "$SQUASH" ]; then
    echo "cpp_e2e_test: $SQUASH not found or not executable -- build it first (make -f Makefile.linux all)" >&2
    exit 1
fi

RT_SQO="$REPO_ROOT/CPPR/cpp_rt.linux64.sqo"
if [ ! -f "$RT_SQO" ]; then
    echo "cpp_e2e_test: $RT_SQO not found -- build the C++ runtime first:" >&2
    echo "  \"$SQUASH\" -c -linux -64 -I \"$INCLUDE_DIR\" -I \"$REPO_ROOT/CPPR\" \"$REPO_ROOT/CPPR/cpp_rt.c\" -o \"$RT_SQO\"" >&2
    exit 1
fi

WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/sq_cpp_e2e.XXXXXX")"
trap 'rm -rf "$WORKDIR"' EXIT

fail=0
count=0

for src in "$TESTS_DIR"/*.cpp; do
    name="$(basename "$src" .cpp)"
    expected="$TESTS_DIR/$name.expected"
    if [ ! -f "$expected" ]; then
        echo "[SKIP] $name (no .expected file)"
        continue
    fi
    count=$((count + 1))
    bin="$WORKDIR/$name.bin"
    build_log="$WORKDIR/$name.build.log"
    if ! "$SQUASH" -linux -64 -I "$INCLUDE_DIR" "$src" -o "$bin" > "$build_log" 2>&1; then
        echo "[FAIL] $name (compile failed -- see below)"
        sed 's/^/       /' "$build_log"
        fail=1
        continue
    fi
    actual="$("$bin" 2>&1)"
    got_exit=$?
    want="$(cat "$expected")"
    if [ "$actual" = "$want" ] && [ "$got_exit" = "0" ]; then
        echo "[OK]   $name"
    else
        echo "[FAIL] $name"
        echo "       --- expected ---"
        echo "$want" | sed 's/^/       /'
        echo "       --- actual (exit=$got_exit) ---"
        echo "$actual" | sed 's/^/       /'
        fail=1
    fi
done

echo
if [ "$fail" = "0" ]; then
    echo "=== RESULT: PASS ($count test(s)) ==="
    exit 0
else
    echo "=== RESULT: FAIL ==="
    exit 1
fi

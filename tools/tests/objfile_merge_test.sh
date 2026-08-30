#!/usr/bin/env bash
# Regression guard for the objfile_merge()/codegen.c cross-object-linking
# bug fixed in codegen.c's Linux x86-64 call-emission path: a call to a
# function with NO visible prototype in the calling translation unit
# (sym==NULL in codegen_expr's AST_CALL handling -- an "implicit
# declaration", e.g. real-world code calling a helper like SDL_fabsf that's
# genuinely exported by a linked-in ".sqo" object) used to unconditionally
# fall through to emit_linux_libc_call(), treating it as an external
# libc.so.6 symbol no matter what -- producing a real "undefined symbol"
# dynamic-linker failure at runtime even though the ".sqo" file's own
# export table genuinely contained the name. See Makefile.SQW.linux's own
# comment ("a symbol the .sqo's own export table genuinely contains, e.g.
# SDL_fabsf, still resolves as an unresolved dynamic import at final
# link") for the original real-world symptom this reproduces.
#
# Fix: codegen.c's sym==NULL fallback (the final "else" in the is_64bit &&
# is_linux SysV call-emission block) now checks codegen_is_sqo_export()
# before assuming an external call, mirroring the check already applied a
# few branches up for the bodyless-SYM_FUNC case, and matching
# codegen_arm64.c's a64_emit_linux_extern_call (which already did this
# correctly for ARM64 -- only the x86-64 path had the gap).
#
# Two cases below: a trivial one-line function, and a ~300-statement
# function (guards against the bug reappearing specifically for
# "larger" functions, the original bug report's own description).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SQUASH="$REPO_ROOT/squash"

if [ ! -x "$SQUASH" ]; then
    echo "objfile_merge_test: $SQUASH not found or not executable -- build it first (make -f Makefile.linux all)" >&2
    exit 1
fi

WORKDIR="$(mktemp -d /tmp/sq_objmerge_test.XXXXXX)"
trap 'rm -rf "$WORKDIR"' EXIT
cd "$WORKDIR"

fail=0

# --- Case 1: trivial function, no prototype in the calling TU ---
cat > lib_small.c <<'EOF'
int my_fabs_like(int x) {
    if (x < 0) return -x;
    return x;
}
EOF
cat > main_small.c <<'EOF'
/* deliberately no prototype -- implicit declaration, sym==NULL in codegen */
int main() {
    int r = my_fabs_like(-42);
    return (r == 42) ? 0 : 1;
}
EOF
"$SQUASH" -c -linux -64 lib_small.c -o lib_small.sqo >/dev/null 2>&1
"$SQUASH" -linux -64 main_small.c lib_small.sqo -o small_test >/dev/null 2>&1
if ./small_test; then
    echo "[OK]   objfile_merge: trivial implicit-declaration cross-.sqo call"
else
    echo "[FAIL] objfile_merge: trivial implicit-declaration cross-.sqo call (exit=$?)"
    fail=1
fi

# --- Case 2: larger function (~300 statements), no prototype ---
{
    echo "int big_noproto(int seed) {"
    echo "    int acc = seed;"
    for i in $(seq 0 299); do
        echo "    acc = acc + $i * 2 - $((i % 5));"
    done
    echo "    return acc;"
    echo "}"
} > lib_big.c
cat > main_big.c <<'EOF'
int main() {
    int r = big_noproto(1);
    return r & 0xff;
}
EOF
# Reference value computed independently (not via squash) so this test
# doesn't just check "didn't crash" -- it checks the CALL ACTUALLY RAN
# the real function and returned the real, correct value.
expected=$(python3 -c "
acc=1
for i in range(300):
    acc = acc + i*2 - (i % 5)
print(acc & 0xff)
" 2>/dev/null || echo "13")
"$SQUASH" -c -linux -64 lib_big.c -o lib_big.sqo >/dev/null 2>&1
"$SQUASH" -linux -64 main_big.c lib_big.sqo -o big_test >/dev/null 2>&1
./big_test
got=$?
if [ "$got" = "$expected" ]; then
    echo "[OK]   objfile_merge: larger (~300-statement) implicit-declaration cross-.sqo call"
else
    echo "[FAIL] objfile_merge: larger implicit-declaration cross-.sqo call (got=$got expected=$expected)"
    fail=1
fi

if [ "$fail" = "0" ]; then
    echo "=== RESULT: PASS ==="
    exit 0
else
    echo "=== RESULT: FAIL ==="
    exit 1
fi

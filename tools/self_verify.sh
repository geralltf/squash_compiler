#!/usr/bin/env bash
# Self-hosting idempotency verification for squash, plus a signed
# provenance manifest -- the "diverse double-compiling" countermeasure
# against a compromised bootstrap compiler (the classic Ken Thompson
# "Reflections on Trusting Trust" concern): a squash built by squash
# itself, across several generations, should converge to a stable output
# rather than silently drifting -- and every generation should actually
# WORK (compile and run real programs correctly), not just "not crash".
#
# What this checks, per generation N (0 = the gcc-built bootstrap):
#   1. genN successfully self-compiles into gen(N+1) (as a single
#      translation unit -- see squash_unity.c's own comment on why: a
#      real, documented squash bug in cross-object linking makes the
#      normal per-file .sqo compile+link approach unreliable for a
#      program this size, exactly like SQW/SQS's own build convention).
#   2. gen(N+1) actually WORKS: it can compile and correctly run a real
#      battery of test programs (not just produce *a* binary -- a binary
#      that "compiles" but is silently wrong is a real failure mode this
#      project has already hit once, see git history around this file).
#   3. gen(N+1)'s own SHA-256 is compared against gen(N)'s -- if two
#      consecutive generations produce byte-identical output, self-
#      compilation has reached a fixed point (true idempotency).
#
# This script reports the REAL result, including failure -- it will
# never claim success it didn't actually observe. If self-hosting isn't
# fully solid yet, the manifest says so explicitly rather than silently
# stopping early or fudging a "pass".
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

ROUNDS="${1:-4}"
WORKDIR="$(mktemp -d /tmp/squash_selfverify.XXXXXX)"
trap 'rm -rf "$WORKDIR"' EXIT

GEN0="$WORKDIR/gen0"       # the gcc-built bootstrap
UNITY_SRC="squash_unity.c" # single-TU #include of every compiler source file

echo "=== squash self-hosting verification (target: $ROUNDS rounds) ==="
echo "Workdir: $WORKDIR"
echo ""

# --- Provenance: what are we even testing? ---
GIT_COMMIT="$(git rev-parse HEAD 2>/dev/null || echo 'unknown (not a git checkout)')"
GIT_DESCRIBE="$(git describe --tags --always --dirty 2>/dev/null || echo 'unknown')"
GIT_BRANCH="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo 'unknown')"
PLATFORM_OS="$(uname -s)"
PLATFORM_ARCH="$(uname -m)"
PLATFORM_KERNEL="$(uname -r)"
BUILD_TIMESTAMP="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
BOOTSTRAP_CC="$(gcc --version 2>/dev/null | head -1 || echo 'gcc (version unknown)')"

echo "Git commit:   $GIT_COMMIT"
echo "Git describe: $GIT_DESCRIBE"
echo "Git branch:   $GIT_BRANCH"
echo "Platform:     $PLATFORM_OS $PLATFORM_ARCH (kernel $PLATFORM_KERNEL)"
echo "Bootstrap CC: $BOOTSTRAP_CC"
echo "Timestamp:    $BUILD_TIMESTAMP"
echo ""

# --- A real battery of test inputs, not just one trivial program ---
mkdir -p "$WORKDIR/testsrc"
cat > "$WORKDIR/testsrc/trivial.c" <<'EOF'
int main(){ return 0; }
EOF
cat > "$WORKDIR/testsrc/arith.c" <<'EOF'
#include <stdio.h>
int fib(int n){ if(n<2) return n; return fib(n-1)+fib(n-2); }
int main(){
    int i, sum = 0;
    for (i = 0; i < 10; i++) sum += fib(i);
    printf("sum=%d\n", sum);
    return sum == 88 ? 0 : 1;
}
EOF
cat > "$WORKDIR/testsrc/strings.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct { char name[32]; int value; } Item;
int main(){
    Item items[3];
    int i;
    for (i = 0; i < 3; i++) { snprintf(items[i].name, sizeof items[i].name, "item%d", i); items[i].value = i*i; }
    int total = 0;
    for (i = 0; i < 3; i++) { printf("%s=%d\n", items[i].name, items[i].value); total += items[i].value; }
    return total == 5 ? 0 : 1;
}
EOF

run_battery() {
    local compiler_bin="$1"
    local outdir="$2"
    local ok=1
    mkdir -p "$outdir"
    for src in "$WORKDIR"/testsrc/*.c; do
        local name; name="$(basename "$src" .c)"
        if ! timeout 15 "$compiler_bin" -linux -64 "$src" -o "$outdir/$name" > "$outdir/$name.compile.log" 2>&1; then
            echo "    [FAIL] $name: compile failed (or crashed) -- see $outdir/$name.compile.log"
            ok=0
            continue
        fi
        if ! timeout 5 "$outdir/$name" > "$outdir/$name.run.log" 2>&1; then
            echo "    [FAIL] $name: compiled but the OUTPUT binary crashed or returned non-zero"
            ok=0
            continue
        fi
        echo "    [OK]   $name"
    done
    # A real, large source file from squash's own codebase -- small
    # synthetic programs above didn't catch a real bug this project hit
    # (a generation that compiles small programs fine but silently
    # mis-compiles large/complex ones, only surfacing as a broken NEXT
    # generation rather than an obvious failure at the generation that
    # actually has the bug). Compiling ast.c itself as a .sqo is a
    # meaningful stand-in: real size, real complexity, and its own exit
    # code confirms the compile step itself didn't just silently succeed
    # while producing nothing useful.
    if ! timeout 30 "$compiler_bin" -c -linux -64 -I"$REPO_ROOT" "$REPO_ROOT/ast.c" -o "$outdir/ast_selftest.sqo" > "$outdir/large_program.compile.log" 2>&1; then
        echo "    [FAIL] large_program (ast.c): compile failed (or crashed) -- see $outdir/large_program.compile.log"
        ok=0
    elif [ ! -s "$outdir/ast_selftest.sqo" ]; then
        echo "    [FAIL] large_program (ast.c): produced an empty/missing object file"
        ok=0
    else
        local got_size; got_size="$(stat -c%s "$outdir/ast_selftest.sqo" 2>/dev/null || stat -f%z "$outdir/ast_selftest.sqo")"
        # A "successful" compile that's suspiciously small compared to a
        # known-good reference is exactly the failure mode a plain
        # exit-code/non-empty check misses -- this project hit it for
        # real (a generation whose small test programs all compiled
        # correctly, but which silently truncated large ones, only
        # crashing the NEXT generation built from that truncated output
        # rather than failing visibly at the generation actually at
        # fault). REF_AST_SQO_SIZE is gen0's own (gcc-built, known-good)
        # size for this exact same compile, set once below.
        if [ -n "${REF_AST_SQO_SIZE:-}" ] && [ "$got_size" -lt $((REF_AST_SQO_SIZE * 60 / 100)) ]; then
            echo "    [FAIL] large_program (ast.c): output is $got_size bytes, suspiciously small vs the gen0 reference ($REF_AST_SQO_SIZE bytes) -- likely silently truncated codegen, not a real success."
            ok=0
        else
            echo "    [OK]   large_program (ast.c, $got_size bytes)"
            if [ -z "${REF_AST_SQO_SIZE:-}" ]; then REF_AST_SQO_SIZE="$got_size"; fi
        fi
    fi
    return $((1-ok))
}

MANIFEST="$WORKDIR/manifest.txt"
{
    echo "squash self-hosting verification manifest"
    echo "git_commit=$GIT_COMMIT"
    echo "git_describe=$GIT_DESCRIBE"
    echo "git_branch=$GIT_BRANCH"
    echo "platform=$PLATFORM_OS $PLATFORM_ARCH kernel=$PLATFORM_KERNEL"
    echo "bootstrap_cc=$BOOTSTRAP_CC"
    echo "timestamp=$BUILD_TIMESTAMP"
    echo "rounds_requested=$ROUNDS"
} > "$MANIFEST"

echo "--- Generation 0 (gcc bootstrap) ---"
UNITY_FILES="assembler.c ast.c codegen.c arm64_asm.c codegen_arm64.c lexer.c parser_new4.c pe_builder.c elf_builder.c macho_builder.c symtable.c linker.c winlinker.c objfile.c implib.c diag.c compiler.c"
if ! gcc -o "$GEN0" $UNITY_FILES -I. -w 2>"$WORKDIR/gen0_build.log"; then
    echo "FATAL: gcc itself failed to build gen0 -- cannot proceed."
    cat "$WORKDIR/gen0_build.log"
    exit 1
fi
GEN0_HASH="$(sha256sum "$GEN0" | cut -d' ' -f1)"
echo "gen0 built. sha256=$GEN0_HASH"
echo "gen0_sha256=$GEN0_HASH" >> "$MANIFEST"

echo "  Running test battery against gen0:"
if ! run_battery "$GEN0" "$WORKDIR/gen0_out"; then
    echo "FATAL: gen0 (the gcc-built bootstrap) fails the test battery. Something is"
    echo "badly wrong before self-hosting even enters the picture -- stopping."
    echo "status=FATAL_GEN0_BROKEN" >> "$MANIFEST"
    cat "$MANIFEST"
    exit 1
fi
echo "gen0_battery=PASS" >> "$MANIFEST"

PREV_BIN="$GEN0"
PREV_HASH="$GEN0_HASH"
CONVERGED=0
FAILED_AT=""

for n in $(seq 1 "$ROUNDS"); do
    echo ""
    echo "--- Generation $n (built by generation $((n-1))) ---"
    GEN_N="$WORKDIR/gen$n"
    if ! timeout 60 "$PREV_BIN" -linux -64 -I. "$UNITY_SRC" -o "$GEN_N" > "$WORKDIR/gen${n}_build.log" 2>&1; then
        echo "  [FAIL] generation $((n-1)) could not self-compile into generation $n."
        echo "         See $WORKDIR/gen${n}_build.log"
        FAILED_AT="gen$n build"
        break
    fi
    chmod +x "$GEN_N" 2>/dev/null
    GEN_N_HASH="$(sha256sum "$GEN_N" | cut -d' ' -f1)"
    echo "  Built. sha256=$GEN_N_HASH"
    echo "gen${n}_sha256=$GEN_N_HASH" >> "$MANIFEST"

    echo "  Running test battery against gen$n:"
    if ! run_battery "$GEN_N" "$WORKDIR/gen${n}_out"; then
        echo "  [FAIL] generation $n does not pass the test battery -- it \"compiled\""
        echo "         (produced a binary) but that binary is not correct."
        echo "gen${n}_battery=FAIL" >> "$MANIFEST"
        FAILED_AT="gen$n battery"
        break
    fi
    echo "gen${n}_battery=PASS" >> "$MANIFEST"

    if [ "$GEN_N_HASH" = "$PREV_HASH" ]; then
        echo "  *** FIXED POINT REACHED: generation $n is byte-identical to generation $((n-1)). ***"
        CONVERGED=1
        echo "converged_at_generation=$n" >> "$MANIFEST"
        break
    fi

    PREV_BIN="$GEN_N"
    PREV_HASH="$GEN_N_HASH"
done

echo ""
echo "=== RESULT ==="
if [ "$CONVERGED" -eq 1 ]; then
    echo "PASS: self-compilation reached a stable fixed point within $ROUNDS rounds."
    echo "status=PASS_CONVERGED" >> "$MANIFEST"
    RESULT=0
elif [ -n "$FAILED_AT" ]; then
    echo "FAIL: self-hosting is not currently reliable (failed at: $FAILED_AT)."
    echo "This is a REAL finding, not a script bug -- do not treat this as passing."
    echo "status=FAIL_AT_${FAILED_AT// /_}" >> "$MANIFEST"
    RESULT=1
else
    echo "INCOMPLETE: ran all $ROUNDS rounds without crashing, but hashes never"
    echo "stabilized -- every generation produces a DIFFERENT binary. This alone"
    echo "isn't necessarily a compromise (e.g. embedded timestamps/pointers in debug"
    echo "info could cause this even with fully correct codegen) but it means true"
    echo "idempotency is NOT yet demonstrated. Investigate before relying on this."
    echo "status=INCOMPLETE_NO_CONVERGENCE" >> "$MANIFEST"
    RESULT=2
fi

# --- Sign the manifest, if a signing key is set up ---
# The key is passphrase-protected (see tools/setup_signing_keys.sh) --
# SQUASH_SIGNING_PASSPHRASE must be set to sign non-interactively (the
# normal case: this script is meant to run unattended in CI). Run
# interactively with it unset and this prompts once, same as any other
# gpg operation on a protected key. Signing is always best-effort: a
# missing/wrong passphrase produces an HONEST "not signed" manifest, not
# a fake signature and not a hard failure of the whole verification run
# (the self-hosting result itself, which is the actual point of this
# script, is already decided by this point either way).
KEY_HOME="${SQUASH_GNUPGHOME:-$HOME/.squash-signing-keys/gnupg}"
OUT_MANIFEST="$REPO_ROOT/tools/keys/last_self_verify_manifest.txt"
mkdir -p "$(dirname "$OUT_MANIFEST")"
cp "$MANIFEST" "$OUT_MANIFEST"
# Remove any signature left over from a PREVIOUS run before attempting a
# new one -- if this run doesn't (re-)sign for any reason, a stale .asc
# must never be left sitting next to manifest content it doesn't actually
# cover. A leftover valid-looking signature file next to unsigned/changed
# content is worse than no signature file at all: "gpg --verify" on it
# reports a real, correctly-detected "BAD signature" rather than "no
# signature", which is easy to mistake for a transient/tooling problem
# instead of what it actually means (this manifest was never signed).
rm -f "$OUT_MANIFEST.asc"
if [ -d "$KEY_HOME" ] && gpg --homedir "$KEY_HOME" --list-secret-keys >/dev/null 2>&1; then
    SIGN_OK=0
    if [ -n "${SQUASH_SIGNING_PASSPHRASE:-}" ]; then
        if gpg --homedir "$KEY_HOME" --batch --yes --pinentry-mode loopback --passphrase "$SQUASH_SIGNING_PASSPHRASE" \
               --detach-sign --armor -o "$OUT_MANIFEST.asc" "$OUT_MANIFEST" 2>"$WORKDIR/sign.log"; then
            SIGN_OK=1
        fi
    elif [ -t 0 ]; then
        if gpg --homedir "$KEY_HOME" --batch --yes --pinentry-mode loopback \
               --detach-sign --armor -o "$OUT_MANIFEST.asc" "$OUT_MANIFEST" 2>"$WORKDIR/sign.log"; then
            SIGN_OK=1
        fi
    fi
    echo ""
    if [ "$SIGN_OK" -eq 1 ]; then
        echo "Manifest signed: $OUT_MANIFEST.asc"
        echo "Verify with: gpg --verify $OUT_MANIFEST.asc $OUT_MANIFEST"
    else
        echo "(Manifest NOT signed -- set SQUASH_SIGNING_PASSPHRASE, or run this"
        echo "interactively, to sign it. See $WORKDIR/sign.log for details.)"
    fi
else
    echo ""
    echo "(No signing key set up -- run tools/setup_signing_keys.sh first to get a signed manifest.)"
fi
echo "Manifest saved: $OUT_MANIFEST"

exit $RESULT

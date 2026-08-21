#!/usr/bin/env bash
# Build + hash + sign a provenance manifest for SDL3, built via squash,
# across the three Makefiles explicitly in scope for this:
#   Makefile.SDL3           (Windows target)
#   Makefile.SDL3.linux     (Linux x86_64 target)
#   Makefile.SDL3.linux.arm64 (Linux arm64 target)
# Deliberately SEPARATE from tools/self_verify.sh, not wired into the core
# squash Makefiles' own "verify" targets: SDL3 is a large third-party
# codebase squash COMPILES, not squash's own source, so "self-hosting
# idempotency" (self_verify.sh's whole point) doesn't apply to it at all
# -- what's meaningful here is just "did each of these three build
# configurations actually succeed, and here's a signed record of exactly
# what was built, from what commit, hashing to what."
#
# What this does per Makefile, honestly scoped to what's actually
# executable from THIS host:
#   - Makefile.SDL3.linux (native x86_64 Linux target, matches this
#     host): builds AND runs the real "run" target as a smoke test.
#   - Makefile.SDL3.linux.arm64 (a different CPU architecture than this
#     host): BUILD ONLY. squash can cross-compile arm64 machine code
#     fine without needing to execute it, but an arm64 binary can't run
#     on an x86_64 host without an emulator (this script doesn't attempt
#     one) -- so this is a real compile-succeeds check, not a "the
#     output actually works" check, and says so plainly in its own
#     result line rather than implying more than it verified.
#   - Makefile.SDL3 (Windows target): BUILD ONLY, same reasoning as the
#     arm64 case but for the OS instead of the CPU -- a Windows PE can't
#     run on this host at all.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"
# shellcheck source=lib_history_clear.sh
. "$SCRIPT_DIR/lib_history_clear.sh"

WORKDIR="$(mktemp -d /tmp/sdl3_verify.XXXXXX)"
trap 'rm -rf "$WORKDIR"' EXIT

HOST_OS="$(uname -s)"
HOST_ARCH="$(uname -m)"
GIT_COMMIT="$(git rev-parse HEAD 2>/dev/null || echo 'unknown')"
GIT_DESCRIBE="$(git describe --tags --always --dirty 2>/dev/null || echo 'unknown')"
BUILD_TIMESTAMP="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

echo "=== SDL3 build verification (squash-compiled, 3 configurations) ==="
echo "Git commit:   $GIT_COMMIT"
echo "Git describe: $GIT_DESCRIBE"
echo "Host:         $HOST_OS $HOST_ARCH"
echo "Timestamp:    $BUILD_TIMESTAMP"
echo ""

MANIFEST="$WORKDIR/manifest.txt"
{
    echo "squash SDL3 build verification manifest"
    echo "git_commit=$GIT_COMMIT"
    echo "git_describe=$GIT_DESCRIBE"
    echo "host=$HOST_OS $HOST_ARCH"
    echo "timestamp=$BUILD_TIMESTAMP"
} > "$MANIFEST"

OVERALL_OK=0

# check_build NAME MAKEFILE ARTIFACT CAN_RUN(0/1) RUN_TARGET
check_build() {
    local name="$1" makefile="$2" artifact="$3" can_run="$4" run_target="$5"
    echo "--- $name ($makefile) ---"
    if [ ! -f "$makefile" ]; then
        echo "  [SKIP] $makefile not found."
        echo "${name}_status=SKIP_NO_MAKEFILE" >> "$MANIFEST"
        return
    fi
    rm -f "$artifact"
    if ! timeout 300 make -f "$makefile" all > "$WORKDIR/${name}_build.log" 2>&1; then
        echo "  [FAIL] build failed -- see $WORKDIR/${name}_build.log"
        tail -15 "$WORKDIR/${name}_build.log"
        echo "${name}_status=FAIL_BUILD" >> "$MANIFEST"
        OVERALL_OK=1
        return
    fi
    if [ ! -f "$artifact" ]; then
        echo "  [FAIL] build reported success but $artifact doesn't exist."
        echo "${name}_status=FAIL_NO_ARTIFACT" >> "$MANIFEST"
        OVERALL_OK=1
        return
    fi
    local sha; sha="$(sha256sum "$artifact" 2>/dev/null | cut -d' ' -f1)"
    echo "  [OK] build succeeded. $artifact sha256=$sha"
    echo "${name}_status=BUILD_OK" >> "$MANIFEST"
    echo "${name}_sha256=$sha" >> "$MANIFEST"

    if [ "$can_run" -eq 1 ]; then
        if timeout 20 make -f "$makefile" "$run_target" > "$WORKDIR/${name}_run.log" 2>&1; then
            echo "  [OK] ran successfully (smoke test)."
            echo "${name}_run=OK" >> "$MANIFEST"
        else
            echo "  [FAIL] built but the run/smoke test failed -- see $WORKDIR/${name}_run.log"
            echo "${name}_run=FAIL" >> "$MANIFEST"
            OVERALL_OK=1
        fi
    else
        echo "  [BUILD-ONLY] cannot execute this artifact from a $HOST_OS $HOST_ARCH host --"
        echo "               not attempting to run it. Build success only."
        echo "${name}_run=NOT_ATTEMPTED_WRONG_HOST" >> "$MANIFEST"
    fi
    echo ""
}

if [ "$HOST_OS" = "Linux" ] && [ "$HOST_ARCH" = "x86_64" ]; then
    check_build "sdl3_linux_x86_64" "Makefile.SDL3.linux" "SDL3_Build/sdl_unity" 1 "run"
else
    echo "--- sdl3_linux_x86_64 (Makefile.SDL3.linux) ---"
    echo "  [SKIP] this host is $HOST_OS $HOST_ARCH, not Linux x86_64 -- the native target"
    echo "         this Makefile is meant for. Not attempting it here."
    echo "sdl3_linux_x86_64_status=SKIP_WRONG_HOST" >> "$MANIFEST"
    echo ""
fi

check_build "sdl3_linux_arm64" "Makefile.SDL3.linux.arm64" "SDL3_Build/sdl_unity" 0 ""
check_build "sdl3_windows" "Makefile.SDL3" "SDL3_Build/sdl_unity.exe" 0 ""

echo "=== RESULT ==="
if [ "$OVERALL_OK" -eq 0 ]; then
    echo "All attempted builds succeeded (build-only configurations were NOT run --"
    echo "see each section above for exactly what was and wasn't verified)."
    echo "status=PASS" >> "$MANIFEST"
else
    echo "At least one build or run FAILED -- see the [FAIL] lines above. This is a"
    echo "real finding, not a script bug."
    echo "status=FAIL" >> "$MANIFEST"
fi

# --- Sign, same convention/key as tools/self_verify.sh ---
KEY_HOME="${SQUASH_GNUPGHOME:-$HOME/.squash-signing-keys/gnupg}"
OUT_MANIFEST="$REPO_ROOT/tools/keys/last_sdl3_verify_manifest.txt"
mkdir -p "$(dirname "$OUT_MANIFEST")"
cp "$MANIFEST" "$OUT_MANIFEST"
rm -f "$OUT_MANIFEST.asc"
# See self_verify.sh's identical comment for the full story: gpg lists
# (and defaults to) secret keys OLDEST-FIRST, so once a key has ever been
# rotated, a bare "gpg --detach-sign" with no -u silently signs with the
# RETIRED key instead of the current one -- entering the current key's
# real passphrase then fails for the (different) old key gpg actually
# picked, with no indication of why. tools/keys/CURRENT_SIGNING_FINGERPRINT
# is the same source of truth setup_signing_keys.sh itself uses.
SIGN_KEY_FPR=""
FPR_FILE="$REPO_ROOT/tools/keys/CURRENT_SIGNING_FINGERPRINT"
if [ -f "$FPR_FILE" ]; then
    SIGN_KEY_FPR="$(tr -d ' \t\n\r' < "$FPR_FILE")"
fi
SIGN_KEY_ARGS=()
if [ -n "$SIGN_KEY_FPR" ]; then
    SIGN_KEY_ARGS=(--local-user "$SIGN_KEY_FPR")
fi
if [ -d "$KEY_HOME" ] && gpg --homedir "$KEY_HOME" --list-secret-keys >/dev/null 2>&1; then
    SIGN_OK=0
    if [ -n "${SQUASH_SIGNING_PASSPHRASE:-}" ]; then
        if gpg --homedir "$KEY_HOME" --batch --yes --pinentry-mode loopback --passphrase "$SQUASH_SIGNING_PASSPHRASE" \
               "${SIGN_KEY_ARGS[@]}" \
               --detach-sign --armor -o "$OUT_MANIFEST.asc" "$OUT_MANIFEST" 2>"$WORKDIR/sign.log"; then
            SIGN_OK=1
        fi
    elif [ -t 0 ]; then
        # See self_verify.sh's own identical branch for the full story:
        # --batch here (with no --passphrase supplied) makes gpg refuse
        # to prompt at all, unconditionally -- a real, confirmed bug, not
        # a tty-detection issue. Dropped here too, plus GPG_TTY exported
        # as standing best practice.
        export GPG_TTY="${GPG_TTY:-$(tty 2>/dev/null || true)}"
        if gpg --homedir "$KEY_HOME" --yes --pinentry-mode loopback \
               "${SIGN_KEY_ARGS[@]}" \
               --detach-sign --armor -o "$OUT_MANIFEST.asc" "$OUT_MANIFEST" 2>"$WORKDIR/sign.log"; then
            SIGN_OK=1
        fi
    fi
    if [ "$SIGN_OK" -eq 1 ]; then
        echo ""
        echo "Manifest signed: $OUT_MANIFEST.asc"
    else
        rm -f "$OUT_MANIFEST.asc"
        echo ""
        echo "(Manifest NOT signed -- set SQUASH_SIGNING_PASSPHRASE, or run this interactively.)"
    fi
else
    echo ""
    echo "(No signing key set up -- run tools/setup_signing_keys.sh first to get a signed manifest.)"
fi
echo "Manifest saved: $OUT_MANIFEST"

offer_clear_history

[ "$OVERALL_OK" -eq 0 ]

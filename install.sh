#!/usr/bin/env bash
# squash compiler installer (Linux/macOS).
#
# What this does, in order:
#   1. Builds squash from source with the real system compiler (gcc/clang)
#      -- never with a previous squash binary, so a fresh install always
#      has a known-good, non-self-hosted provenance chain to start from.
#   2. If a signed provenance manifest exists (tools/self_verify.sh's own
#      output) and a public key is available, verifies it -- warns (does
#      NOT silently continue past a bad signature) rather than install
#      blind.
#   3. Prints real version/platform info for whatever gets installed, so
#      "what did I just install" is never a mystery.
#   4. Copies the binary to an install directory (default ~/.local/bin,
#      the standard unprivileged per-user location -- no sudo needed and
#      nothing overwritten system-wide) and offers to add it to PATH via
#      your shell profile, WITHOUT silently editing anything you didn't
#      confirm.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="${SQUASH_INSTALL_DIR:-$HOME/.local/bin}"
BIN_NAME="squash"

echo "=== squash compiler installer ==="

# --- Version / provenance info up front ---
GIT_COMMIT="$(git -C "$SCRIPT_DIR" rev-parse --short HEAD 2>/dev/null || echo 'unknown')"
GIT_DESCRIBE="$(git -C "$SCRIPT_DIR" describe --tags --always --dirty 2>/dev/null || echo 'unknown')"
PLATFORM_OS="$(uname -s)"
PLATFORM_ARCH="$(uname -m)"
echo "Source:   $SCRIPT_DIR"
echo "Version:  $GIT_DESCRIBE (commit $GIT_COMMIT)"
echo "Platform: $PLATFORM_OS $PLATFORM_ARCH"
echo ""

# --- Build with the REAL system compiler, never a prior squash binary ---
CC="${CC:-}"
if [ -z "$CC" ]; then
    if command -v gcc >/dev/null 2>&1; then CC=gcc
    elif command -v clang >/dev/null 2>&1; then CC=clang
    else echo "ERROR: no C compiler found (need gcc or clang). Install one and re-run."; exit 1
    fi
fi
echo "Building with: $CC ($($CC --version 2>/dev/null | head -1))"

cd "$SCRIPT_DIR"
SRC_FILES="compiler.c assembler.c ast.c codegen.c arm64_asm.c codegen_arm64.c lexer.c parser_new4.c pe_builder.c elf_builder.c macho_builder.c symtable.c linker.c winlinker.c objfile.c implib.c diag.c"
BUILD_TMP="$(mktemp /tmp/squash_install_build.XXXXXX)"
if ! "$CC" -o "$BUILD_TMP" $SRC_FILES -I. -w 2>"$BUILD_TMP.log"; then
    echo "ERROR: build failed. See $BUILD_TMP.log"
    exit 1
fi
echo "Build OK. sha256=$(sha256sum "$BUILD_TMP" 2>/dev/null | cut -d' ' -f1 || shasum -a 256 "$BUILD_TMP" | cut -d' ' -f1)"

# --- Signature verification, if a manifest+key are present (best-effort, non-fatal) ---
MANIFEST="$SCRIPT_DIR/tools/keys/last_self_verify_manifest.txt"
PUBKEY="$SCRIPT_DIR/tools/keys/squash-release-signing-pubkey.asc"
if [ -f "$MANIFEST.asc" ] && [ -f "$PUBKEY" ] && command -v gpg >/dev/null 2>&1; then
    TMP_GNUPGHOME="$(mktemp -d)"
    trap 'rm -rf "$TMP_GNUPGHOME"' EXIT
    gpg --homedir "$TMP_GNUPGHOME" --batch --quiet --import "$PUBKEY" 2>/dev/null
    if gpg --homedir "$TMP_GNUPGHOME" --batch --verify "$MANIFEST.asc" "$MANIFEST" 2>/dev/null; then
        echo "Provenance manifest signature: VALID"
    else
        echo "*** WARNING: provenance manifest signature check FAILED. ***"
        echo "*** This does not stop the install (the binary was just built from source"
        echo "*** you already have), but it means the self-hosting verification record"
        echo "*** for this checkout is not trustworthy. Investigate before relying on it."
    fi
else
    echo "(No signed provenance manifest found -- run tools/self_verify.sh to generate one.)"
fi
echo ""

# --- Install ---
mkdir -p "$INSTALL_DIR"
cp "$BUILD_TMP" "$INSTALL_DIR/$BIN_NAME"
chmod +x "$INSTALL_DIR/$BIN_NAME"
rm -f "$BUILD_TMP" "$BUILD_TMP.log"
echo "Installed: $INSTALL_DIR/$BIN_NAME"

# --- PATH ---
case ":$PATH:" in
    *":$INSTALL_DIR:"*)
        echo "$INSTALL_DIR is already on your PATH."
        ;;
    *)
        echo ""
        echo "$INSTALL_DIR is NOT currently on your PATH."
        PROFILE=""
        if [ -n "${ZSH_VERSION:-}" ] || [ "${SHELL:-}" = "$(command -v zsh 2>/dev/null)" ]; then PROFILE="$HOME/.zshrc"
        elif [ -f "$HOME/.bashrc" ]; then PROFILE="$HOME/.bashrc"
        else PROFILE="$HOME/.profile"
        fi
        if [ -t 0 ]; then
            read -r -p "Add it to PATH via $PROFILE? [y/N] " ans
        else
            ans="n"
            echo "(non-interactive shell -- skipping the PATH prompt; answer manually below)"
        fi
        if [ "${ans:-n}" = "y" ] || [ "${ans:-n}" = "Y" ]; then
            {
                echo ""
                echo "# added by squash's install.sh"
                echo "export PATH=\"$INSTALL_DIR:\$PATH\""
            } >> "$PROFILE"
            echo "Added to $PROFILE. Run 'source $PROFILE' or open a new shell."
        else
            echo "Skipped. Add this yourself if you want it on PATH:"
            echo "  export PATH=\"$INSTALL_DIR:\$PATH\""
        fi
        ;;
esac

echo ""
echo "Done. Try: $INSTALL_DIR/$BIN_NAME -linux -64 yourfile.c -o yourprogram"

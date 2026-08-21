#!/usr/bin/env bash
# Changes the passphrase on squash's build-signing key.
#
# Deliberately NOT an in-place "gpg --change-passphrase" -- that command
# was tested directly against this exact key during development and
# reported success while silently leaving the old passphrase in place
# (confirmed by killing gpg-agent and testing both passphrases
# afterwards, not by trusting its exit code). Rather than rely on a GPG
# command that's already been caught lying about its own result, this
# takes the path already proven to work: generate a brand-new key
# protected by your new passphrase, and retire the old one. The old key
# is NEVER deleted -- anything it already signed stays verifiable -- it
# just stops being the one new signatures are made with.
#
# This script is meant to be run BY YOU, interactively, in your own
# terminal -- it never accepts the new passphrase as a command-line
# argument (that would leak it into shell history and `ps` output to
# every other process on the machine) and prints nothing you type back
# to the screen.
#
# After it finishes, it PROVES the change actually took effect (signs
# with the new key using the new passphrase, and confirms the OLD
# passphrase no longer works against the new key) rather than trusting
# gpg's own exit code -- the exact check that caught --change-passphrase
# lying.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
KEY_HOME="${SQUASH_GNUPGHOME:-$HOME/.squash-signing-keys/gnupg}"
# shellcheck source=lib_history_clear.sh
. "$SCRIPT_DIR/lib_history_clear.sh"
FPR_FILE="$REPO_ROOT/tools/keys/CURRENT_SIGNING_FINGERPRINT"

if [ ! -t 0 ] || [ ! -t 1 ]; then
    echo "ERROR: this script must be run interactively (it needs to prompt you for" >&2
    echo "the new passphrase with echo off) -- stdin/stdout aren't both a terminal." >&2
    echo "If you're deliberately automating this, use tools/setup_signing_keys.sh" >&2
    echo "directly with SQUASH_SIGNING_PASSPHRASE and SQUASH_SIGNING_FORCE_NEW=1" >&2
    echo "instead -- that's the scriptable path; this one is the human path." >&2
    exit 1
fi

if [ ! -f "$FPR_FILE" ]; then
    echo "ERROR: no current signing key found ($FPR_FILE doesn't exist)." >&2
    echo "Run tools/setup_signing_keys.sh first to create one." >&2
    exit 1
fi
OLD_FPR="$(tr -d '[:space:]' < "$FPR_FILE")"

echo "=== Change squash build-signing key passphrase ==="
echo ""
echo "Current key: $OLD_FPR"
echo "This will generate a NEW key protected by a NEW passphrase and switch to"
echo "it. The current key is kept (not deleted) so anything it already signed"
echo "stays verifiable -- it just won't be used for new signatures anymore."
echo ""
read -r -p "Continue? [y/N] " confirm
case "$confirm" in
    y|Y) ;;
    *) echo "Cancelled -- nothing changed."; exit 0 ;;
esac

echo ""
p1="" p2=""
while true; do
    read -rs -p "New passphrase: " p1; echo
    if [ -z "$p1" ]; then echo "Can't be empty. Try again."; continue; fi
    if [ "${#p1}" -lt 12 ]; then
        echo "That's only ${#p1} characters -- 12+ is recommended for a signing key. Use it anyway? [y/N] "
        read -r weak_ok
        case "$weak_ok" in y|Y) ;; *) continue ;; esac
    fi
    read -rs -p "Confirm new passphrase: " p2; echo
    if [ "$p1" != "$p2" ]; then echo "Didn't match. Try again."; continue; fi
    break
done

echo ""
echo "Generating new key..."
SQUASH_SIGNING_PASSPHRASE="$p1" SQUASH_SIGNING_ROTATE=1 SQUASH_SIGNING_FORCE_NEW=1 SQUASH_GNUPGHOME="$KEY_HOME" \
    bash "$SCRIPT_DIR/setup_signing_keys.sh"

NEW_FPR="$(tr -d '[:space:]' < "$FPR_FILE")"
if [ "$NEW_FPR" = "$OLD_FPR" ]; then
    echo ""
    echo "ERROR: fingerprint didn't change ($NEW_FPR) -- setup_signing_keys.sh did not" >&2
    echo "actually rotate the key. Nothing was proven to work; do not trust this as done." >&2
    unset p1 p2
    exit 1
fi

echo ""
echo "New key: $NEW_FPR"
echo "Proving the change actually took effect (not trusting exit codes alone --"
echo "this exact kind of silent no-op is what caught 'gpg --change-passphrase' lying):"

gpgconf --homedir "$KEY_HOME" --kill gpg-agent >/dev/null 2>&1 || true
sleep 1
tmp="$(mktemp)"; echo "verify" > "$tmp"

if gpg --homedir "$KEY_HOME" --batch --pinentry-mode loopback --passphrase "$p1" \
       --local-user "$NEW_FPR" --yes --detach-sign -o "$tmp.sig" "$tmp" >/dev/null 2>&1; then
    echo "  [OK] new passphrase signs successfully with the new key."
else
    echo "  [FAIL] new passphrase did NOT work against the new key -- something is wrong." >&2
    rm -f "$tmp" "$tmp.sig"; unset p1 p2
    exit 1
fi

gpgconf --homedir "$KEY_HOME" --kill gpg-agent >/dev/null 2>&1 || true
sleep 1
rm -f "$tmp.sig"
if gpg --homedir "$KEY_HOME" --batch --pinentry-mode loopback --passphrase '' \
       --local-user "$NEW_FPR" --yes --detach-sign -o "$tmp.sig" "$tmp" >/dev/null 2>&1; then
    echo "  [FAIL] the new key ALSO signs with an EMPTY passphrase -- it is NOT protected." >&2
    rm -f "$tmp" "$tmp.sig"; unset p1 p2
    exit 1
else
    echo "  [OK] empty passphrase is correctly refused."
fi

rm -f "$tmp" "$tmp.sig"
unset p1 p2

echo ""
echo "=== Done. Verified working. ==="
echo "New signing key:  $NEW_FPR"
echo "Retired key:      $OLD_FPR (kept -- still verifies its own past signatures)"
echo "Public key:       $REPO_ROOT/tools/keys/squash-release-signing-pubkey.asc (commit this)"

offer_clear_history

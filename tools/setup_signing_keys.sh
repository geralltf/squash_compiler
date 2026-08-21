#!/usr/bin/env bash
# Creates (or checks the health of) a dedicated, PASSPHRASE-PROTECTED GPG
# keypair used ONLY to sign squash build-provenance manifests
# (tools/self_verify.sh's own output), never anything else.
#
# WHY NOT ~/.ssh: SSH keys and PGP/GPG keys are different formats for a
# different protocol -- ssh-keygen's private keys aren't something gpg
# (or any real signing workflow) can use, and stuffing a GPG keyring into
# ~/.ssh would confuse SSH's own tooling (ssh-agent, known_hosts checks,
# etc. all assume ~/.ssh contains only SSH material). GPG has always had
# its own dedicated, correct answer to "keep the private key out of the
# versioned project folder": a GPG *home directory* (the --homedir flag /
# GNUPGHOME env var), completely separate from any repo. This script uses
# a DEDICATED homedir just for this one signing identity (NOT your
# personal default ~/.gnupg, which may hold your own real PGP identity
# and other keys this project has no business touching), so this key's
# lifecycle never gets mixed up with anything else on your machine.
#
# WHERE THE KEY ACTUALLY LIVES: ~/.squash-signing-keys/gnupg (outside any
# git-versioned folder, exactly as asked) -- back that whole directory up
# however you'd back up any other secret (encrypted volume, password
# manager attachment, offline copy -- deliberately NOT this script's job:
# a build script should never be the thing deciding how your backups are
# encrypted/stored). The PUBLIC key alone is exported into the repo
# itself (tools/keys/squash-release-signing-pubkey.asc) so anyone can
# verify a signed manifest without needing any secret material at all.
#
# PASSPHRASE: the private key file's own filesystem permissions (0700
# homedir, gpg's own 0600 key files) are the FIRST layer; a passphrase is
# the second, and matters specifically against anyone who gets read
# access to the key file itself (a stolen backup, a misconfigured
# permission, a compromised account with file access but not this
# passphrase). Read from SQUASH_SIGNING_PASSPHRASE if set (the
# unattended/CI path -- see that variable's own warning below), otherwise
# prompted for interactively with echo off. There is no "empty
# passphrase" option any more; if you truly want unattended signing with
# no human able to type a passphrase, that's what SQUASH_SIGNING_PASSPHRASE
# is for -- set it from whatever secret store your CI already trusts, not
# hardcoded in a script.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
KEY_HOME="${SQUASH_GNUPGHOME:-$HOME/.squash-signing-keys/gnupg}"
KEY_UID="squash-compiler-build-signing"
KEY_EMAIL="${SQUASH_SIGNING_EMAIL:-squash-build@localhost}"
PUBKEY_OUT="$REPO_ROOT/tools/keys/squash-release-signing-pubkey.asc"
MIN_RSA_BITS=3072
MAX_KEY_AGE_DAYS=730   # 2 years -- force a look/rotation prompt past this, doesn't force expiry itself

# Reads a signing passphrase into $PASSPHRASE, either from
# SQUASH_SIGNING_PASSPHRASE (unattended/CI path -- the env var is visible
# to anything that can read this process's environment, e.g. /proc/PID/environ
# for another process running as the same user, or your CI provider's own
# logs/dashboard if it's ever accidentally printed -- only use it from a
# real secret store, never a hardcoded value in a script or committed
# file) or by prompting twice with echo off and requiring the two entries
# to match (the standard "confirm passphrase" pattern, catches typos
# before they get baked into a key you can't recover).
read_passphrase() {
    if [ -n "${SQUASH_SIGNING_PASSPHRASE:-}" ]; then
        PASSPHRASE="$SQUASH_SIGNING_PASSPHRASE"
        echo "Using SQUASH_SIGNING_PASSPHRASE from the environment." >&2
        return
    fi
    if [ ! -t 0 ]; then
        echo "ERROR: no SQUASH_SIGNING_PASSPHRASE set and stdin isn't a terminal" >&2
        echo "(can't prompt interactively). Set SQUASH_SIGNING_PASSPHRASE or run this" >&2
        echo "from an interactive shell." >&2
        exit 1
    fi
    local p1 p2
    while true; do
        read -rs -p "Enter a passphrase for the squash signing key: " p1; echo >&2
        if [ -z "$p1" ]; then echo "Passphrase can't be empty. Try again." >&2; continue; fi
        read -rs -p "Confirm passphrase: " p2; echo >&2
        if [ "$p1" != "$p2" ]; then echo "Passphrases didn't match. Try again." >&2; continue; fi
        break
    done
    PASSPHRASE="$p1"
}

mkdir -p "$(dirname "$KEY_HOME")"
if [ ! -d "$KEY_HOME" ]; then
    mkdir -m 700 "$KEY_HOME"
    echo "Created dedicated GPG homedir: $KEY_HOME (mode 700)"
fi
# Belt-and-suspenders: gpg itself refuses to operate on a homedir with
# loose permissions, but fix it proactively rather than fail confusingly.
chmod 700 "$KEY_HOME" 2>/dev/null || true

export GNUPGHOME="$KEY_HOME"

echo "=== squash build-signing key setup ==="
echo "Key homedir: $KEY_HOME"

FPR_FILE="$REPO_ROOT/tools/keys/CURRENT_SIGNING_FINGERPRINT"
# The fingerprint FILE, not a generic "list secret keys and take the
# first one", is the source of truth for which key is current. A plain
# re-scan can't tell "the" active key apart from an intentionally-kept
# retired one once the keyring holds more than one (which it always will
# after even a single rotation -- old keys are kept, never deleted, so
# their past signatures stay verifiable) -- gpg lists keys oldest-first,
# so a naive "first match" scan would silently keep picking the ORIGINAL
# key forever, even after generating a brand new one to replace it.
existing_fpr=""
if [ -f "$FPR_FILE" ]; then
    candidate="$(tr -d '[:space:]' < "$FPR_FILE")"
    if [ -n "$candidate" ] && gpg --homedir "$KEY_HOME" --list-secret-keys --with-colons "$candidate" >/dev/null 2>&1; then
        existing_fpr="$candidate"
    else
        echo "  [WARNING] $FPR_FILE names a fingerprint not present in this keyring -- ignoring it."
    fi
elif gpg --homedir "$KEY_HOME" --list-secret-keys --with-colons 2>/dev/null | grep -q '^sec:'; then
    # No fingerprint file yet, but a keyring already exists (e.g. someone
    # ran gpg directly, or this is an old setup from before this file
    # existed) -- fall back to the single key if there's exactly one,
    # rather than guessing among several.
    n_keys="$(gpg --homedir "$KEY_HOME" --list-secret-keys --with-colons 2>/dev/null | grep -c '^sec:')"
    if [ "$n_keys" -eq 1 ]; then
        existing_fpr="$(gpg --homedir "$KEY_HOME" --list-secret-keys --with-colons 2>/dev/null | awk -F: '/^fpr:/ {print $10; exit}')"
    else
        echo "  [WARNING] $n_keys secret keys in this keyring and no $FPR_FILE to say which is current."
        echo "            Not guessing -- set it manually or remove keys you don't want considered."
    fi
fi

rotate_needed=0
if [ -n "$existing_fpr" ]; then
    echo "Existing signing key found: $existing_fpr"

    # -- Check 1: algorithm/size strength --
    key_info="$(gpg --homedir "$KEY_HOME" --list-secret-keys --with-colons "$existing_fpr" 2>/dev/null | awk -F: '/^sec:/ {print; exit}')"
    algo="$(echo "$key_info" | cut -d: -f4)"
    bits="$(echo "$key_info" | cut -d: -f3)"
    # GPG pubkey algo IDs: 1=RSA, 17=DSA, 22=EdDSA, 18=ECDH, 19=ECDSA
    case "$algo" in
        1)
            if [ "${bits:-0}" -lt "$MIN_RSA_BITS" ]; then
                echo "  [SECURITY RISK] RSA-$bits is below the minimum ($MIN_RSA_BITS bits) -- flagging for rotation."
                rotate_needed=1
            else
                echo "  [OK] RSA-$bits (>= $MIN_RSA_BITS)"
            fi
            ;;
        22|19)
            echo "  [OK] modern EdDSA/ECDSA key (algo $algo)"
            ;;
        17)
            echo "  [SECURITY RISK] DSA key -- deprecated for new signing use, flagging for rotation."
            rotate_needed=1
            ;;
        *)
            echo "  [WARNING] unrecognized algo id $algo -- review manually."
            ;;
    esac

    # -- Check 2: expiry --
    expiry_epoch="$(echo "$key_info" | cut -d: -f7)"
    now_epoch="$(date +%s)"
    if [ -n "$expiry_epoch" ] && [ "$expiry_epoch" != "0" ]; then
        if [ "$expiry_epoch" -lt "$now_epoch" ]; then
            echo "  [SECURITY RISK] key EXPIRED on $(date -d "@$expiry_epoch" '+%Y-%m-%d' 2>/dev/null || date -r "$expiry_epoch" '+%Y-%m-%d') -- flagging for rotation."
            rotate_needed=1
        else
            days_left=$(( (expiry_epoch - now_epoch) / 86400 ))
            if [ "$days_left" -lt 30 ]; then
                echo "  [WARNING] key expires in $days_left day(s) -- rotate soon."
            else
                echo "  [OK] key valid, expires in $days_left day(s)"
            fi
        fi
    else
        echo "  [WARNING] key has NO expiry set -- a signing key that never expires is a standing risk if the private key is ever silently compromised. Consider 'gpg --homedir \"$KEY_HOME\" --quick-set-expire $existing_fpr 2y'."
    fi

    # -- Check 3: creation age (informational, doesn't force rotation on its own) --
    created_epoch="$(echo "$key_info" | cut -d: -f6)"
    if [ -n "$created_epoch" ]; then
        age_days=$(( (now_epoch - created_epoch) / 86400 ))
        if [ "$age_days" -gt "$MAX_KEY_AGE_DAYS" ]; then
            echo "  [WARNING] key is $age_days days old (> $MAX_KEY_AGE_DAYS) -- consider rotating even if not expired."
        fi
    fi

    # -- Check 4: passphrase protection -- does the key actually sign
    # with an EMPTY passphrase? If so it has no real passphrase, no
    # matter how it was created. Restart the agent first so this isn't
    # just testing a cached unlock from something earlier in the session.
    gpgconf --homedir "$KEY_HOME" --kill gpg-agent >/dev/null 2>&1 || true
    sleep 1
    tmp_probe="$(mktemp)"; echo "probe" > "$tmp_probe"
    if gpg --homedir "$KEY_HOME" --batch --pinentry-mode loopback --passphrase '' \
           --local-user "$existing_fpr" --yes --detach-sign -o "$tmp_probe.sig" "$tmp_probe" >/dev/null 2>&1; then
        echo "  [SECURITY RISK] key has NO passphrase -- it signs with an empty one. Flagging for rotation."
        rotate_needed=1
    else
        echo "  [OK] key is passphrase-protected (empty passphrase was correctly refused)"
    fi
    gpgconf --homedir "$KEY_HOME" --kill gpg-agent >/dev/null 2>&1 || true
    rm -f "$tmp_probe" "$tmp_probe.sig"

    # SQUASH_SIGNING_ROTATE=1 is a VOLUNTARY rotation request (e.g. "I
    # just want a new passphrase on an otherwise perfectly healthy key")
    # -- distinct from rotate_needed, which only gets set by an actual
    # detected problem above. Without this, SQUASH_SIGNING_FORCE_NEW had
    # no effect at all on a healthy key: it only ever meant "yes, replace
    # the key you already flagged", never "replace this key even though
    # nothing's wrong with it" -- exactly the case
    # tools/change_signing_passphrase.sh needs. tools/keys/*.sh scripts
    # that want a rotation regardless of health should set this rather
    # than relying on SQUASH_SIGNING_FORCE_NEW alone.
    if [ "${SQUASH_SIGNING_ROTATE:-0}" = "1" ] && [ "$rotate_needed" -eq 0 ]; then
        echo "  [INFO] voluntary rotation requested (SQUASH_SIGNING_ROTATE=1) -- key is"
        echo "         otherwise healthy, but replacing it as asked."
        rotate_needed=1
    fi

    if [ "$rotate_needed" -eq 1 ]; then
        echo ""
        echo "This key is being replaced. It will NOT be deleted automatically -- old"
        echo "signatures it already made must stay verifiable. Generate a NEW key by"
        echo "re-running with SQUASH_SIGNING_FORCE_NEW=1, then update"
        echo "$PUBKEY_OUT with the new public key (the old one can stay published"
        echo "alongside it for verifying past signatures)."
        # Always clear it here -- a flagged key is never "usable" as-is.
        # Whether that means "stop and tell the user" or "generate a
        # replacement now" is decided by the exit-3 guard right below,
        # based on SQUASH_SIGNING_FORCE_NEW.
        existing_fpr=""
    fi
fi

if [ -z "$existing_fpr" ] && [ "${rotate_needed:-0}" -eq 1 ] && [ "${SQUASH_SIGNING_FORCE_NEW:-0}" != "1" ]; then
    echo ""
    echo "No ACTION taken (flagged key kept as-is for verifying old signatures)."
    echo "Re-run with SQUASH_SIGNING_FORCE_NEW=1 to generate a replacement."
    exit 3
fi

if [ -z "$existing_fpr" ]; then
    echo "No usable signing key found -- generating a new, passphrase-protected Ed25519 key (modern, strong, small)."
    read_passphrase
    # UID includes a unique suffix -- gpg refuses to create a second key
    # under an identical name+email (an existing FLAGGED key, kept around
    # on purpose so its old signatures stay verifiable, is still
    # "identical" as far as that check is concerned), so a bare
    # "$KEY_UID <$KEY_EMAIL>" would collide with itself on every
    # rotation. A first attempt at this used just the calendar DATE,
    # which seemed fine in testing but broke for real: rotating twice on
    # the same day (exactly what a "wait, that didn't work, let me try
    # again" retry looks like) collided with the SAME day's own earlier
    # key and the whole run aborted with "gpg: A key ... already exists"
    # -- confirmed as the actual cause of a real failed run, not a
    # hypothetical. Full timestamp (to the second) plus this process's
    # own PID makes a same-second double-rotation the only remaining
    # collision risk, astronomically unlikely for a human running this
    # by hand.
    GEN_UID="$KEY_UID $(date -u '+%Y-%m-%dT%H%M%SZ')-$$"
    gpg --homedir "$KEY_HOME" --batch --quiet --pinentry-mode loopback --passphrase-fd 0 \
        --quick-generate-key "$GEN_UID <$KEY_EMAIL>" ed25519 sign 2y <<< "$PASSPHRASE"
    unset PASSPHRASE
    # Look up the fingerprint by searching for the EXACT UID just used,
    # not a bare "first key in the keyring" scan -- gpg lists keys
    # oldest-first, so with more than one key already present (true on
    # every rotation, since the old one is deliberately kept) a plain
    # first-match scan silently returns the WRONG (old) key's
    # fingerprint here, exactly the bug this project already hit once in
    # the health-check lookup above. Confirmed by testing an actual
    # rotation end-to-end, not by inspection alone -- the naive version
    # of this line reported "success" while reusing the old fingerprint.
    existing_fpr="$(gpg --homedir "$KEY_HOME" --list-secret-keys --with-colons "$GEN_UID <$KEY_EMAIL>" 2>/dev/null \
        | awk -F: '/^fpr:/ {print $10; exit}')"
    if [ -z "$existing_fpr" ]; then
        echo "ERROR: generated a key but couldn't find its fingerprint by UID lookup -- something is wrong, not proceeding." >&2
        exit 1
    fi
    echo "Generated new signing key: $existing_fpr"
fi

mkdir -p "$(dirname "$PUBKEY_OUT")"
gpg --homedir "$KEY_HOME" --armor --export "$existing_fpr" > "$PUBKEY_OUT"
echo ""
echo "Public key exported to (safe to commit): $PUBKEY_OUT"
echo "Private key lives only in: $KEY_HOME (NOT in this repo -- back it up separately)"
echo "$existing_fpr" > "$REPO_ROOT/tools/keys/CURRENT_SIGNING_FINGERPRINT"
echo "Fingerprint recorded in: $REPO_ROOT/tools/keys/CURRENT_SIGNING_FINGERPRINT"

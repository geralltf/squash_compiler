#!/usr/bin/env bash
# Creates (or checks the health of) a dedicated GPG keypair used ONLY to
# sign squash build-provenance manifests (tools/self_verify.sh's output),
# never anything else.
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
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
KEY_HOME="${SQUASH_GNUPGHOME:-$HOME/.squash-signing-keys/gnupg}"
KEY_UID="squash-compiler-build-signing"
KEY_EMAIL="${SQUASH_SIGNING_EMAIL:-squash-build@localhost}"
PUBKEY_OUT="$REPO_ROOT/tools/keys/squash-release-signing-pubkey.asc"
MIN_RSA_BITS=3072
MAX_KEY_AGE_DAYS=730   # 2 years -- force a look/rotation prompt past this, doesn't force expiry itself

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

existing_fpr="$(gpg --homedir "$KEY_HOME" --list-secret-keys --with-colons 2>/dev/null \
    | awk -F: '/^fpr:/ {print $10; exit}')"

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

    if [ "$rotate_needed" -eq 1 ]; then
        echo ""
        echo "This key is flagged as a security risk (see above). It will NOT be deleted"
        echo "automatically -- old signatures it already made must stay verifiable. Generate"
        echo "a NEW key by re-running with SQUASH_SIGNING_FORCE_NEW=1, then update"
        echo "$PUBKEY_OUT with the new public key (the old one can stay published"
        echo "alongside it for verifying past signatures)."
        if [ "${SQUASH_SIGNING_FORCE_NEW:-0}" != "1" ]; then
            existing_fpr=""  # fall through to "no usable key" messaging below, but don't generate yet
        fi
    fi
fi

if [ -z "$existing_fpr" ] && [ "${rotate_needed:-0}" -eq 1 ] && [ "${SQUASH_SIGNING_FORCE_NEW:-0}" != "1" ]; then
    echo ""
    echo "No ACTION taken (flagged key kept as-is for verifying old signatures)."
    echo "Re-run with SQUASH_SIGNING_FORCE_NEW=1 to generate a replacement."
    exit 3
fi

if [ -z "$existing_fpr" ]; then
    echo "No signing key found -- generating a new Ed25519 key (modern, strong, small)."
    gpg --homedir "$KEY_HOME" --batch --quiet --pinentry-mode loopback --passphrase '' \
        --quick-generate-key "$KEY_UID <$KEY_EMAIL>" ed25519 sign 2y
    existing_fpr="$(gpg --homedir "$KEY_HOME" --list-secret-keys --with-colons 2>/dev/null \
        | awk -F: '/^fpr:/ {print $10; exit}')"
    echo "Generated new signing key: $existing_fpr"
fi

mkdir -p "$(dirname "$PUBKEY_OUT")"
gpg --homedir "$KEY_HOME" --armor --export "$existing_fpr" > "$PUBKEY_OUT"
echo ""
echo "Public key exported to (safe to commit): $PUBKEY_OUT"
echo "Private key lives only in: $KEY_HOME (NOT in this repo -- back it up separately)"
echo "$existing_fpr" > "$REPO_ROOT/tools/keys/CURRENT_SIGNING_FINGERPRINT"
echo "Fingerprint recorded in: $REPO_ROOT/tools/keys/CURRENT_SIGNING_FINGERPRINT"

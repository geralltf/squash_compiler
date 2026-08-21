#!/usr/bin/env bash
# Shared "optionally clear shell history" step, offered after operations
# that touch secrets (tools/change_signing_passphrase.sh) or that a
# security-conscious user might just want a clean slate after
# (tools/self_verify.sh). Meant to be sourced, not executed:
#   . "$(dirname "${BASH_SOURCE[0]}")/lib_history_clear.sh"
#   offer_clear_history
#
# HONEST LIMITATION, stated up front rather than glossed over: this
# function almost always runs inside a CHILD process (you invoked it via
# "bash tools/whatever.sh", which starts a new bash process). A child
# process cannot reach into its PARENT interactive shell's in-memory
# history list -- that list lives in the parent's own memory, not
# anywhere a child can touch. So "history -c" run from inside here only
# ever clears the (usually empty, non-interactive shells don't keep
# history) list belonging to THIS script's own process, not the real
# terminal history you've been typing into. What this function CAN
# actually do for real, because it's a plain file on disk, is clear the
# on-disk history FILE (~/.bash_history or ~/.zsh_history) -- which is
# also the part that persists after you close the terminal, so arguably
# the part that matters more anyway. It tells you the exact command to
# run yourself for the in-memory part.
offer_clear_history() {
    if [ ! -t 0 ]; then
        # Non-interactive (e.g. CI running self_verify.sh unattended) --
        # a read prompt here would just hang the job. Silently skip
        # rather than block; there's no human present to ask anyway.
        return 0
    fi
    echo ""
    read -r -p "Clear your shell history now? This wipes ~/.bash_history / ~/.zsh_history (e.g. in case you ever typed a passphrase directly into a command instead of a prompt). [y/N] " _clear_ans
    case "$_clear_ans" in
        y|Y)
            echo "Clearing your shell history now."
            local cleared_any=0
            local histfile="${HISTFILE:-$HOME/.bash_history}"
            if [ -f "$histfile" ]; then
                : > "$histfile"
                echo "  Cleared: $histfile"
                cleared_any=1
            fi
            if [ -f "$HOME/.zsh_history" ] && [ "$HOME/.zsh_history" != "$histfile" ]; then
                : > "$HOME/.zsh_history"
                echo "  Cleared: $HOME/.zsh_history"
                cleared_any=1
            fi
            if [ "$cleared_any" -eq 0 ]; then
                echo "  No history file found at $histfile (nothing to clear on disk)."
            fi
            echo ""
            echo "  This process can't reach your actual terminal's IN-MEMORY history"
            echo "  (a subprocess can't clear its parent shell's own memory) -- run this"
            echo "  yourself in the terminal you're typing into, to clear that too:"
            echo "    history -c"
            ;;
        *)
            echo "Skipped -- history left as-is."
            ;;
    esac
}

#!/usr/bin/env bash
# auto-push.sh
# Triggered by .claude/settings.json (Stop hook) at the end of every
# Claude turn. Pushes the current branch when its number of unpushed
# commits has reached THRESHOLD. Silent when there's nothing to push;
# prints a one-line notice on success or failure.

THRESHOLD=5

# The Stop hook runs from the project root, but be defensive in case
# Claude's cwd ever changes.
cd "$(dirname "$0")/.." || exit 0

# Bail quietly if the branch isn't tracking an upstream (detached HEAD,
# unpushed local-only branch, etc.).
upstream=$(git rev-parse --abbrev-ref --symbolic-full-name @{u} 2>/dev/null) || exit 0
[ -n "$upstream" ] || exit 0

ahead=$(git rev-list --count "@{u}..HEAD" 2>/dev/null) || exit 0

if [ "${ahead:-0}" -ge "$THRESHOLD" ]; then
    if output=$(git push 2>&1); then
        echo "[auto-push] pushed $ahead commit(s) to $upstream"
    else
        echo "[auto-push] FAILED to push $ahead commit(s) to $upstream"
        echo "$output" | sed 's/^/[auto-push]   /'
    fi
fi

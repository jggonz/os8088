#!/bin/bash
# Rename this cloud session's generated branch after its request (fork rule 9;
# tools/session-sync.sh says when).
#   tools/session-branch.sh <slug>
# claude/charming-goodall-plll24 + "wireball-serve-fix"
#   -> claude/wireball-serve-fix-plll24   (the suffix keeps the session link)
# Only renames a branch that has never been pushed. Does not push.
set -euo pipefail
slug=$(printf '%s' "${1:-}" | tr 'A-Z' 'a-z' | sed 's/[^a-z0-9]\{1,\}/-/g; s/^-//; s/-$//' | cut -c1-48 | sed 's/-$//')
[ -n "$slug" ] || { echo "usage: $0 <short-slug-describing-the-request>" >&2; exit 2; }
old=$(git branch --show-current)
[ -n "$old" ] || { echo "HEAD is detached" >&2; exit 1; }
if git ls-remote --exit-code --heads origin "$old" >/dev/null 2>&1; then
    echo "$old is already on GitHub; not renaming it." >&2; exit 1
fi
suffix=${old##*-}
case "$old" in claude/*-*-*) ;; *) suffix= ;; esac
new="claude/$slug${suffix:+-$suffix}"
[ "$new" != "$old" ] || { echo "already named $new"; exit 0; }
git branch -m "$old" "$new"
echo "renamed $old -> $new"
echo "push with: git push -u origin $new"

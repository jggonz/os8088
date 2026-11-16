#!/bin/bash
# First command of every Claude Code cloud session in this fork (fork rule 9,
# CLAUDE.md / AGENTS.md):  tools/session-sync.sh
# Deliberately NOT a SessionStart hook: upstream refuses start hooks, and the
# fork section of CLAUDE.md is lifted out for upstream PRs - take this file too.
#
# 1. SYNC. A cloud container's checkout can be a cached snapshot, older than
#    the branch on GitHub. Fetch the session's branch (or, for a brand-new
#    session branch, the default branch it was cut from) and fast-forward
#    when that is safe. It never resets, rebases or merges: a branch that
#    has diverged is reported, loudly, and left alone.
# 2. NAME. A session started from the default branch is handed a generated
#    name (claude/<adjective>-<name>-<id>). While that branch is still
#    empty, tell the agent to rename it after the request with
#    tools/session-branch.sh.
#
# Run it at the start of a session and again after a resume. It only fetches
# and fast-forwards, so it is cheap to run whenever in doubt.
set -uo pipefail

cd "$(git rev-parse --show-toplevel 2>/dev/null || echo .)" || exit 0
git rev-parse --git-dir >/dev/null 2>&1 || exit 0

branch=$(git branch --show-current)
[ -n "$branch" ] || { echo "session-sync: HEAD is detached; no sync done."; exit 0; }

default=$(git symbolic-ref -q --short refs/remotes/origin/HEAD 2>/dev/null)
default=${default#origin/}
[ -n "$default" ] || default=elendilon

say() { echo "session-sync: $*"; }

# Fetch. A shallow clone fetches just what connects to what it already has.
if git ls-remote --exit-code --heads origin "$branch" >/dev/null 2>&1; then
    remote_has_branch=1
    git fetch --quiet origin "$branch:refs/remotes/origin/$branch" "$default:refs/remotes/origin/$default" 2>/dev/null \
        || git fetch --quiet origin "$branch:refs/remotes/origin/$branch" 2>/dev/null \
        || { say "WARNING: could not fetch origin/$branch. The checkout may be STALE; fetch it before starting work."; exit 0; }
    target="origin/$branch"
else
    remote_has_branch=0
    git fetch --quiet origin "$default:refs/remotes/origin/$default" 2>/dev/null \
        || { say "WARNING: could not fetch origin/$default. The checkout may be STALE; fetch it before starting work."; exit 0; }
    target="origin/$default"
fi

head=$(git rev-parse HEAD)
tip=$(git rev-parse "$target")
local_only=$(git rev-list --count HEAD --not --remotes 2>/dev/null || echo 0)

if [ "$head" = "$tip" ]; then
    say "$branch is up to date with $target ($(git rev-parse --short HEAD))."
elif git merge-base --is-ancestor HEAD "$target" 2>/dev/null; then
    behind=$(git rev-list --count "HEAD..$target")
    if [ "$remote_has_branch" = 0 ] && [ "$local_only" != 0 ]; then
        say "$branch is $behind commit(s) behind $target but has local commits; left alone."
    elif git merge --ff-only --quiet "$target" >/dev/null 2>&1; then
        say "the checkout was STALE: fast-forwarded $branch by $behind commit(s) to $target ($(git rev-parse --short HEAD)). Work starts from there."
    else
        say "WARNING: $branch is $behind commit(s) behind $target and the fast-forward failed (uncommitted changes in the way?). Tell the user before doing any work."
    fi
elif git merge-base --is-ancestor "$target" HEAD 2>/dev/null; then
    ahead=$(git rev-list --count "$target..HEAD")
    say "$branch is $ahead commit(s) AHEAD of $target (unpushed work). Push it."
else
    if [ "$remote_has_branch" = 1 ]; then
        say "WARNING: $branch has DIVERGED from $target ($(git rev-list --count "$target..HEAD") local, $(git rev-list --count "HEAD..$target") remote). Nothing was changed. Tell the user before doing any work; do not reset or force-push."
    else
        say "$branch has its own commits and $target has moved on; left alone."
    fi
fi

[ "$remote_has_branch" = 1 ] && git branch --quiet --set-upstream-to="origin/$branch" "$branch" 2>/dev/null

# The generated-name check: a fresh session branch, never pushed, nothing on it.
if [ "$remote_has_branch" = 0 ] && [ "$local_only" = 0 ] \
   && [[ "$branch" =~ ^claude/[a-z]+-[a-z]+-[a-z0-9]{4,8}$ ]]; then
    say "this session's branch '$branch' has a GENERATED name. Fork rule 9: before the first commit, rename it after the request with  tools/session-branch.sh <short-slug>  (e.g. tools/session-branch.sh wireball-serve-fix), then push to the new name."
fi
exit 0

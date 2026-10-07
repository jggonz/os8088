---
name: refresh-stale-pr
description: Bring one of the maintainer's own stale pull requests (a branch on jggonz/os8088 that main has moved past) back to mergeable - merge main into it in a scratch worktree, decide whether it is still valid and still needed against what main now contains, fix what main's drift broke or what the PR got wrong, run the gates and an end-to-end check against real build artefacts, push to the PR's own branch, rewrite the PR body's validation, and (only when asked) squash-merge it. Use when the user asks whether a PR is "still valid", to "refresh", "revive", "update" or "bring main into" an older PR, or to make a stale PR ready to merge. For a PR from someone else's fork use review-fork-pr instead.
---

# Refresh a stale pull request

The PRs this is for are the maintainer's own: a branch on `jggonz/os8088`,
usually one focused change, cut from a `main` that has since moved by tens of
commits. Nobody else is going to push to it. The question is never "is this
code good in the abstract" - it was good enough to open - but **"is it still
right on the `main` that exists today, and does anyone still need it"**.

It exists because PR #200 (the imager keeping `SYSTEM.CFG`) was 28 commits
stale, merged with no conflict at all, passed every test it shipped - and was
still wrong on today's `main`: #203 had taken FAT16 volumes to 2GB and the
in-OS installer writes partition type 06h past 32MB, which the PR's reader
refused. Git, the PR's tests and the fast tier all reported nothing. Finding
it took reading what `main` gained *around* the PR, not *in* its files.
**Read [LESSONS.md](LESSONS.md) before step 3.**

The user is the maintainer. Decisions that are theirs - close a PR, change
its scope, merge it - go through `AskUserQuestion` or wait for their word;
everything else you decide and record in the PR body.

## 1. Which PR, and is this the right skill

The number(s) may arrive as the argument (`/refresh-stale-pr 200`, or
`200 201` for several, done one at a time). With none, list the candidates -
the open PRs whose base has moved - and ask:

```sh
gh pr list --state open --json number,title,headRefName,headRepositoryOwner,updatedAt,isDraft
```

Then the facts:

```sh
gh pr view <N> --json number,title,state,isDraft,headRefName,headRepositoryOwner,\
baseRefName,mergeable,mergeStateStatus,additions,deletions,changedFiles,body,commits
gh pr view <N> --json comments,reviews --jq '.comments[].body, .reviews[].body'
git fetch -q origin main <headRef>
BASE=$(git merge-base origin/main origin/<headRef>)
git rev-list --count $BASE..origin/main                     # how stale
gh pr diff <N> --name-only
```

- **`headRepositoryOwner` is not `jggonz`** ⇒ stop and use `review-fork-pr`:
  pushing to `origin` would not update a fork's PR (that skill's LESSONS §1).
- **`state` is not `OPEN`** ⇒ say so and stop.
- **Draft** ⇒ ask whether it is meant to be finished now; a draft is often
  parked on purpose.

Tell the user the shape in two lines: what it does, how far behind, which
files, conflicting or not.

## 2. Preflight - a worktree of its own

```sh
git rev-parse --is-shallow-repository     # true => git fetch --unshallow first (docs/UPSTREAM.md rule 0)
git worktree list | grep <headRef>        # the branch is often ALREADY checked out elsewhere
```

**The PR's branch is usually still checked out in the worktree it was written
in** (`../os8088-<feature>`), so `git worktree add <path> <headRef>` refuses.
Do not touch that worktree - it may hold the user's uncommitted work. Make a
local branch off the remote head instead, and push it to the PR's branch by
name at the end:

```sh
git worktree add -b pr<N>-refresh /tmp/pr<N> origin/<headRef>
```

`/tmp/pr<N>`, not the scratchpad: QMP and the zharness use `AF_UNIX` sockets
with a ~104-byte path limit, and a scratchpad path is too long for them. The
user's main checkout stays untouched for the whole run - other sessions write
to it.

## 3. Merge `main` in

```sh
cd /tmp/pr<N> && git merge --no-edit origin/main
```

A conflict is resolved by `docs/UPSTREAM.md`'s defaults (ours for what the
PR deliberately changed, theirs for what it merely lacks). Then the checks a
clean merge reports nothing about:

```sh
grep -oE '^#+ [0-9.]+' SPEC.md | sort | uniq -d    # a § number both sides took
python3 tools/checkdocs.py
python3 tools/os88index.py --check                 # docs/INDEX.md is generated
```

A `§` number `main` took while the PR sat open is the common collision: the
PR's section renumbers (it is the newcomer now), with every reference to it.

Run the PR's own tests on the merged tree **before editing anything** - that
separates "main's drift broke it" from "my fix broke it".

## 4. Is it still valid, and still needed?

This is the step the skill exists for. Two questions, answered with evidence:

**Still needed?** - has `main` already done this, or removed the thing it
changes?

```sh
git log --oneline $BASE..origin/main -- <every file the PR touches>
git log --oneline $BASE..origin/main --grep '<the feature's key words>'
```

Grep `main` for the PR's new function names, flags and SPEC wording. If
`main` superseded it, the answer is to **recommend closing**, with the
commit that superseded it - ask before closing, never close unasked.

**Still valid?** - the PR's files merging cleanly says nothing about the code
they call or the formats they read. For every module, tool, constant and
on-disk format the PR *depends on* (not just the ones it edits):

```sh
git diff $BASE origin/main --stat -- <dependencies>
git diff $BASE origin/main -- <dependency> | less       # read it
```

Then read the commit titles of everything `main` gained and ask, of each one
in the PR's subject area, *does the PR's assumption still hold?* Usual
suspects: a format version bump (`PKG_FMT`, `DRV_VER`, `SC_VER`), a file
that moved (`SYSTEM/`, `APPDATA/`), a limit that grew (volume size, cluster
size, entry count), a new geometry, an API slot relayout, a knob made
default. Check the kernel's own acceptance rules (what it mounts, what it
reads, what version it refuses) against what the PR accepts - a host tool
stricter than the kernel refuses real users' disks.

Say the verdict to the user plainly - **valid and needed / needs fixes /
superseded** - with the commit or file that decides it, before fixing.

## 5. Fix and improve

Fixes go in their own commit after the merge commit, with a message that
names what `main` changed and why the PR's old assumption broke. Each fix
gets a test that fails without it. Update the PR's SPEC.md section and docs
in the same commit - SPEC.md is updated before or with the code, never after
(CLAUDE.md).

Keep improvements inside the PR's scope. A tempting adjacent cleanup is a
separate PR; a refresh that grows is a review nobody asked for.

## 6. Verify

Cheapest first, and every one on the MERGED tree:

```sh
python3 -m unittest tests/unit/<the PR's tests>.py
make -j8 > /tmp/pr<N>-make.log 2>&1; echo "exit $?"      # the fast tier rides it
make test-full > /tmp/pr<N>-full.log 2>&1; echo "exit $?" # the pre-merge gate
```

Read the exit code and the `os88test:` summary line, not a wrapper's tail -
`make` failing after a green-looking last line is a known trap. A `SKIP
(needs marty|cc)` row is environment, and is reported as skipped with its
reason, never as passed.

**Then an end-to-end check against a real artefact**, because unit tests
build their own fixtures and fixtures encode the author's assumptions (that
is exactly how #200's 04h-only reader passed its own tests). Use what the
change actually consumes: the main checkout's `build/` images, a volume laid
out the way the *kernel or installer* lays one out, `os88disk.py
--verify-hdd` / `--verify` on whatever the change writes. A change with a UI
claim gets the `functional-check` skill's on-the-glass pass.

**Every check you write must be able to fail.** A comparison of a value
with itself prints "ok" forever (LESSONS.md §3).

## 7. Push and rewrite the PR

```sh
git push origin HEAD:<headRef>
gh pr view <N> --json commits --jq '.commits[-1].oid'   # the PR moved?
```

Append to the PR body - never replace the author's text - a section headed
**Brought up to date with main (<date>)**: what was merged (how many commits,
conflicts or not), each fix with the `main` change that made it necessary,
and the validation with real numbers (tests, tiers with pass/fail/skip,
the end-to-end check and what it ran on). Keep the body's "still
outstanding" line honest - a hardware test nobody ran is still outstanding.
End with the Claude Code attribution line.

```sh
gh pr view <N> --json body -q .body > /tmp/pr<N>-body.md
# ...append...
gh pr edit <N> --body-file /tmp/pr<N>-body.md
```

## 8. Merge - only on the user's word

Ready-to-merge is the deliverable. Merge only when the invocation asked for
it ("...then merge it") or the user says so now; otherwise ask once, with
the verdict and the evidence in the question.

```sh
gh pr merge <N> --squash --admin
gh pr view <N> --json state,mergeCommit -q '.state + " " + .mergeCommit.oid'
```

`--admin` because the ruleset wants a review the sole maintainer cannot
give. **One `gh` mutation per command** - never chain merge, retarget and
branch delete with `&&`; a stacked PR's base vanishing closes it.

## 9. Clean up and hand back

```sh
git worktree remove /tmp/pr<N> && git branch -D pr<N>-refresh
git -C <main checkout> pull --ff-only       # only if that checkout is clean and on main
```

Leave the PR's original worktree and remote branch alone unless the user
asks - it may hold work in progress, and a merged branch is theirs to delete.

The report: the verdict (valid / fixed / superseded) and what decided it,
each fix in a sentence, the gates with their numbers, what was NOT tested
and why, and the merge commit or the PR link.

## What this skill does not do

- **A fork's PR** - `review-fork-pr` (pushing elsewhere, hand-holding comment).
- **A multi-agent review** - a stale PR of the maintainer's own is one
  change; if the merge reveals it is really large or risky, say so and offer
  `review-fork-pr`'s workflow shape or `/code-review` rather than inventing one.
- **Close a PR unasked**, delete a branch unasked, or force-push. A refresh
  only ever adds commits.

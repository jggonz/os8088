---
name: review-fork-pr
description: Review an incoming pull request that comes from someone else's fork of os8088 - fetch it, merge main into it, review it with a team of agents for memory safety, lost-from-main regressions, redraw cost and disk/data integrity, present a plan, apply the fixes, verify them, push them back to the contributor's branch, and post a hand-holding comment on the PR. Use when the user says a PR came from a fork or another repo, names a PR number to review, or asks to prepare an incoming PR for merge.
---

# Review a pull request from a fork

A contributor works in their own fork of os8088 and opens a PR against
`jggonz/os8088:main`. It is usually **large** (98 files and +27,500 lines was
PR #92; 193 files and +73,000 was #80), usually **long-lived** — cut from a
`main` that has since moved — and often **conflicting**. Your job is the
maintainer's: understand it, merge `main` into it, find what would break real
hardware, fix that, verify it, push the fixes back **to the contributor's
branch on their fork** so the PR itself becomes mergeable, and explain every
change on the PR in language a semi-technical reader can follow.

Invoking this skill **authorises the multi-agent orchestration below** — the
`Agent` fan-out and the two `Workflow` scripts (what the user calls
"ultracode"). Every decision that is the user's comes back through
`AskUserQuestion`; everything else you decide and record.

Three files travel with this skill:

| file | what |
|---|---|
| [`LESSONS.md`](LESSONS.md) | what seven of these reviews cost to learn — the git mechanics that silently do the wrong thing, and the defect classes this repo's forks actually ship. **Read it in full before step 2.** |
| [`workflows/review.js`](workflows/review.js) | the review workflow: the PR split into logical units, one reviewer per unit, every finding adversarially verified |
| [`workflows/fix.js`](workflows/fix.js) | the fix workflow: verified findings in sequential batches, each fixer building and verifying its own batch |

Also binding, and already in the tree: **[`docs/UPSTREAM.md`](../../../docs/UPSTREAM.md)**
— the fork topology, the squash cycle, the shallow-clone trap and the
merge-resolution rules. It is written from the *contributor's* side; this
skill is the same cycle seen from `main`. Read its "Merging upstream in" and
"The PR back to main" sections before step 4. Then `CLAUDE.md` (hard rules,
performance table), `SPEC.md` (the binding contract), `CONTRIBUTING.md`
(what the repo asks of a contribution).

Reviews were sized on **Opus 5** and **Fable 5**. On a smaller model, run the
review workflow with fewer, narrower units rather than trusting a wide one.

---

## 1. Which PR, and what is it

The PR number may arrive as the argument (`/review-fork-pr 92`). If not, list
what is open and ask:

```sh
gh pr list --repo jggonz/os8088 --state open \
   --json number,title,author,headRepositoryOwner,headRefName,changedFiles
```

Then get the facts before forming any opinion — **the topology decides what
you are even able to do**:

```sh
gh pr view <N> --json title,body,author,headRepository,headRepositoryOwner,headRefName,\
baseRefName,state,mergeable,mergeStateStatus,maintainerCanModify,additions,deletions,changedFiles,commits
gh api repos/jggonz/os8088/pulls/<N> --jq \
   '{maintainer_can_modify, head_repo:.head.repo.full_name, head_ref:.head.ref, head_sha:.head.sha}'
gh pr view <N> --json comments --jq '.comments[] | "=== " + .author.login + " ===\n" + .body'
```

| field | why you need it |
|---|---|
| `headRepositoryOwner` + `headRefName` | **the push target**: `git@github.com:<owner>/os8088.git HEAD:<headRef>`. Pushing anywhere else does not touch the PR |
| `maintainer_can_modify` | false ⇒ you cannot push to their branch at all; take the fallback in step 9 |
| `mergeable` / `mergeStateStatus` | `CONFLICTING` is normal and is step 4's work |
| `changedFiles`, `additions` | the size gate in step 5 — inline agents or the workflow |
| the comments | the contributor may have already answered half of what you are about to ask |

State the shape back to the user in two or three lines (who, what, how big,
how far behind `main`, what conflicts) before you go further.

## 2. Preflight

```sh
git rev-parse --is-shallow-repository        # true ⇒ every ancestry answer below is a lie
git fetch --unshallow                        # ...so do this FIRST (docs/UPSTREAM.md rule 0)
git status --short                           # the user's tree must stay untouched
pgrep -fl qemu-system                        # a stale QEMU answers on build/qmp.sock with an OLD image
gh auth status
```

Read `LESSONS.md` now, in full.

**Fetch the PR and work in a scratch worktree, never in the user's checkout:**

```sh
git fetch origin pull/<N>/head:pr-<N>
git worktree add -q "$SCRATCH/wt" pr-<N>          # $SCRATCH = the session scratchpad
git remote add <owner> git@github.com:<owner>/os8088.git 2>/dev/null || true
git fetch <owner> <headRef>
```

The worktree is where every build, boot, edit and commit happens. The user
keeps a working tree they can use while the review runs, and step 10 hands
them the branch when it is over. **One caveat that has cost a session:** a
scratchpad path is long, and QEMU's QMP socket is an `AF_UNIX` path with a
~104-byte limit — boot with a short socket path (`/tmp/q<N>.sock`), or build
in the worktree and boot from a short `BUILD=` directory.

Then measure the gap:

```sh
BASE=$(git merge-base main pr-<N>)
git log --oneline $BASE..main                       # what main gained since the fork point
git diff --stat $BASE pr-<N> | tail -40
git diff --name-status $BASE pr-<N> | awk '{print $1}' | sort | uniq -c   # A/M/D counts
git diff --diff-filter=D --name-only $BASE...pr-<N>  # DELETIONS - read every one
```

Deletions are the highest-yield list on the whole PR. A long-lived fork branch
loses things `main` added, and **git reports no conflict for a file the branch
simply never received** (LESSONS.md §2).

## 3. Ask the user what shape this run takes

One `AskUserQuestion` call, four questions, recommendation first:

1. **Where do the fixes land?**
   - *Push to the contributor's branch on their fork* `(Recommended)` — the PR
     itself updates; requires `maintainer_can_modify`
   - *A new integration PR on our repo, which I merge myself* — the PR #47
     shape: their work plus your fixes, one PR you own
   - *Local branch only* — review and fix, push nothing; the user tests by hand
2. **Merge `main` into the PR branch first?**
   - *Yes* `(Recommended)` when `main` has moved — otherwise you are reviewing
     code against a base that no longer exists
   - *No* — review as submitted
3. **What should the review weigh hardest?** (multiSelect; default all)
   memory safety · lost-from-`main` regressions · redraw/CPU cost against the
   4.77 MHz budget · disk and file-format integrity · the package/driver ABI
4. **Stop for your approval after the review?**
   - *Yes — plan first, change nothing until I say go* `(Recommended)`
   - *No — review, fix and push in one pass*

Default to plan-first. It is what the last several runs asked for, and on a
PR this size the plan is the deliverable the user actually reads.

## 4. Merge `main` in — and find what git cannot see

In the worktree:

```sh
git merge --no-commit --no-ff main
git diff --name-only --diff-filter=U        # the textual conflicts
```

Resolve per file with `docs/UPSTREAM.md`'s defaults: **ours** for what the
branch deliberately changed, **theirs** for what it merely lacks, **theirs**
for gratuitous divergence. Never blanket `--ours`; for a big file use that
document's difflib script to list every line `main` added that your side lacks.

Then the four checks git merges cleanly and reports nothing about:

```sh
grep -oE '^#+ [0-9.]+' SPEC.md | sort | uniq -d      # DUPLICATE § headings - checkdocs cannot see these
git ls-files build | wc -l                           # must be 0 (SPEC.md 16)
git ls-files | grep -E '^\.claude/|\.(bin|o88|img|mod|drv)$'   # what rode in that should not have
python3 tools/checkdocs.py
```

- **Duplicate `§` numbers are the standard collision.** `main`'s §67 C
  toolchain met the branch's §67 Cyclone 88 in #92; `main`'s §52 ModPlug met
  the branch's §52 HDD before that. The branch's numbering wins at the squash,
  so the *incoming-from-main* section renumbers, and every reference to it
  moves with it — scope the rewrite to the lines `main` added
  (`git diff $BASE <main-commit>`), not to the whole file. Record the
  renumbering in `docs/UPSTREAM.md` beside the existing precedents.
- **Compare API slot addresses mechanically** — a slot that changed meaning
  assembles cleanly and calls the wrong routine (LESSONS.md §3).
- Commit the merge on its own, with a message that says what collided and how
  it was resolved. Fixes go in a separate commit.

Now build both kernels and boot once, **before** reviewing, so the reviewers
argue about a tree that works: `make`, `make KERN_SMALL=1` (or `make small`),
`make test` + a screendump, plus whatever gate the feature owns
(`tests/heapfrag`, `make zcheck`, `make rczex`, …).

## 5. Review — split it into units, verify every finding

**Under ~30 changed files:** spawn 3–5 read-only `Agent`s in one message, one
per subsystem, each pointed at the worktree with "do NOT modify any files",
each told to read `CLAUDE.md` first and to ground every finding in a line it
actually read. Then confirm the top findings yourself by reading the code.

**Larger:** run the workflow.

```
Workflow({ scriptPath: "<abs repo>/.claude/skills/review-fork-pr/workflows/review.js",
           args: { wt: "<abs worktree>", pr: <N>, base: "<merge-base sha>",
                   emphasis: ["memory", "regression", "redraw", "disk", "abi"],
                   units: [ { key: "kernel-core", title: "...", paths: "kernel/memory.inc ...",
                              focus: "..." }, ... ] } })
```

You write the `units` — that is the part no agent can do for you. Split by
**subsystem and by lens**, not by file count: `kernel/wm.inc` alone has been
4,200 changed lines and wants two reviewers, one for window *state* and one
for *painting*, each told the other exists so they do not duplicate. Every
unit's `focus` should name what the PR body itself admits is unfinished, what
it deleted, and which § it claims to obey. Aim for 8–14 units; the workflow
adversarially verifies each finding and returns only what survived, with a
`CONFIRMED` / `REFUTED` / `UNCERTAIN` verdict and a corrected fix.

While the agents run, do the mechanical checks yourself: the slot table
address↔name cross-check, `git log main -- <file>` on every deleted hunk, the
kernel size guards, and a boot. Two independent reviewers landing on the same
defect is the strongest signal you will get; read the code yourself for
anything you are about to call a blocker.

## 6. The plan, and the gate

Present, in the chat and in this order:

1. **What the PR is** — one paragraph, in the user's terms.
2. **Merge state** — what conflicted, what you resolved, and the semantic
   collisions git did not report.
3. **Findings**, ranked, each as: what breaks, when it breaks, and how you
   would fix it. Say which were confirmed by two reviewers and which you read
   yourself. Keep refuted findings out of the list; say how many there were.
4. **What you will not change** — the contributor's design decisions, style,
   and anything whose fix is a decision rather than a repair (raising a
   budget, spending an API slot, changing an ABI).
5. **The plan**: fix these, verify like this, push there, comment that.

Then `AskUserQuestion`. Nothing is written to the fork, the PR, or the user's
repo before that answer. If the user says "fix all high and medium", that is
the authorisation for step 7 and no further asking is needed unless a fix
turns into a design decision.

## 7. Fix

The rules that make a fix acceptable in **someone else's** PR:

- **Minimal and surgical.** No refactors, no reformatting, no improving
  neighbouring code, nothing outside the finding. Match the surrounding style
  exactly — comment density, the trailing-comment column, how `§` is written.
- **Keep the contributor's intent.** If their design says the pool is movable,
  the fix guards the copy; it does not pin the pool because pinning is easier.
- **A finding that turns out not to be real is declined, not "fixed".** A bad
  edit to working code is worse than a missed fix.
- **SPEC.md is updated in the same edit** as anything it pins — a constant, a
  register contract, a struct offset, a slot number, an author rule.
- **Never relieve a guard to make the build pass.** `KERN_BUDGET` and
  `KERN_CODE_MAX` are the user's call (CLAUDE.md), not a build fix. Align a
  *stale* symbol to the real value; never raise the real value.
- `python3 tools/kernsize.py --build build --bless` in the same commit as any
  kernel size change, both variants.

Up to about eight fixes, do them inline, one at a time, each with its SPEC
line. More than that, batch them:

```
Workflow({ scriptPath: "<abs repo>/.claude/skills/review-fork-pr/workflows/fix.js",
           args: { wt: "<abs worktree>", pr: <N>, batches: [ { key: "B1-...", title: "...",
                   findings: [...], extra: "batch-specific verification commands" } ] } })
```

Batches run **sequentially** — they share one worktree, and two agents editing
one tree do not merge. Each fixer builds, runs its own verification, and
commits only its own batch. Never let a fixer push.

## 8. Verify

Nothing is pushed until every one of these has run in the worktree and you can
quote its output:

```sh
make                                   # nasm -w+error, and checkdocs is a prerequisite
make KERN_SMALL=1                      # both kernels, both under their guards
python3 tools/kernsize.py --build build          # and --build build/smallk -DKERN_SMALL
python3 tools/os88disk.py --verify build/*.img   # structural fsck, all three geometries
make test                              # boot; screendump the desktop
make test VIDEO=cga                    # the 1bpp look at anything that draws or greys
```

Plus, driven over QMP (`tools/mouse.py`, `tools/shot.py`), **the feature the
PR is about, exercised end to end**: the gate the PR ships if it ships one,
the file it saves opened again, the app it adds launched and closed. #92's
compaction fixes were only believable because Paint saved a BMP that fsck'd
clean afterwards. Crop and zoom before concluding anything about a small
change.

## 9. Push, and say what you did

**Re-fetch before pushing — the contributor has been working while you were.**

```sh
git fetch <owner> <headRef>
git merge-base --is-ancestor <owner>/<headRef> HEAD && echo "fast-forward OK"
# if not: rebase onto their new head, never force-push
git rebase --onto <owner>/<headRef> <the sha you started from> pr-<N>
git push <owner> pr-<N>:<headRef>
```

- **Push to the fork's remote, not `origin`.** `git push origin HEAD:<headRef>`
  creates a stray branch on *your* repo and leaves the PR untouched — it has
  happened, and the cleanup is `git push origin --delete <headRef>`.
- **Never `--force`** a branch you do not own.
- If `maintainer_can_modify` is false: take the #47 shape instead — a branch
  on `origin` carrying their commits plus yours, and a PR whose body says
  plainly that it contains all of theirs and reverts nothing.
- Confirm the PR moved: `gh pr view <N> --json mergeable,mergeStateStatus,commits`.
  `MERGEABLE` + `BLOCKED` means blocked on review approval, which is the user's.

Then the comment. Its reader is **the contributor, semi-technical, reading in
a browser** — `gh pr comment <N> --body-file <file>`:

- Open with what you did in two lines: merged `main`, pushed N commits, the PR
  is now mergeable.
- **One item per fix**: what could have gone wrong *in the user's world* ("a
  save on a marginal drive would have told you the disk was full, sending you
  off deleting files that were never the problem"), then the mechanism in one
  sentence, then what you changed, with the commit sha.
- A **flagged, not fixed** section — anything you left alone and why.
- No blame anywhere in it. The defects are in code that took real work.

## 10. Hand back

```sh
git worktree remove --force "$SCRATCH/wt"
git worktree prune
git fetch origin pull/<N>/head:pr-<N> --force && git checkout pr-<N>   # if the user wants to test
pgrep -fl qemu-system                       # leave nothing running
git -C <the user's checkout> status --short  # must be exactly what it was in step 2
```

Leave the local branch pointing at the reviewed code when the user has said
they want to test by hand — that is the usual request — and say in one line
which branch and what to run. Then summarise: what the PR is, what you merged,
what you fixed, what you flagged, what is left for the user (the approval, the
squash, the merge).

## What this skill does not do

- **It does not merge the PR.** That is the user's, always.
- **It does not squash or rewrite the contributor's history.**
- **It does not raise a budget, spend an API slot, or change an ABI** to make
  something fit; those come back as questions.
- **It does not vendor anything** or re-add `build/`.

# Working with upstream — the squash cycle, and how not to lose things in it

**This document is written to be true wherever it is read** — on `main`, on an
integration branch, or on a branch freshly cut from `main` — so it names
`main` and "the integration branch" explicitly and never says "here". The two
are the same tree at the moment of a squash and drift apart afterwards; almost
everything below is about that gap.

It exists because the shape has cost real work twice: once when a branch
drifted far enough from `main` that it had to be re-cut, and once when a
session concluded from a *shallow clone* that the two histories were unrelated
and re-created three commits it could have merged.

## The topology

| | |
|---|---|
| `jggonz/os8088` | **upstream.** `main` is the published history: linear, one commit per feature, by policy |
| `Elendilon/os8088` | the fork. Its integration branch — `elendilon` at the time of writing — is where work lands and what gets tested on the iron (`docs/FIELD-MACHINES.md`) |

`main` is **squash-only**. The last merge commit on it is PR #14; every commit
since has a single parent. That is a deliberate choice about how `main` reads,
not an accident, and nothing in this document proposes changing it.

## The cycle

```
   cut a branch from main
        │
        ▼
   work on it, many commits, tested on QEMU and on the 5150
        │
        ▼
   one PR  ──────►  jggonz/os8088:main, SQUASH-merged
        │                    │
        │                    ▼
        └──────────  cut a FRESH branch from main, and go again
```

Two consequences follow, and they are the whole reason this document exists.

**The integration branch is disposable.** It is cut from `main` at a squash,
lived in for one round, and replaced by a fresh cut. A squash carries the
branch's *content* into a brand-new commit with no ancestry link to the
branch's *commits*, so the gap it opens **self-heals at the cycle boundary**
and needs no maintenance merge in between.

**Anything not in the PR is lost at the re-cut.** The fresh branch starts from
`main`, which holds only what the squash carried.

## Rule 0 — unshallow before believing anything about ancestry

A fresh agent session gets a **shallow clone**, and on a shallow clone git does
not error about truncated history: `git merge-base`, `git log A..B` and
`git merge-base --is-ancestor` return confidently wrong answers, because the
graft boundary is indistinguishable from a set of root commits.

```sh
git rev-parse --is-shallow-repository        # true => every answer below is a lie
git fetch --unshallow                        # ...so do this FIRST
git rev-list --max-parents=0 <ref> | wc -l   # >1 root = SHALLOW, not unrelated
```

That last line is the tell. **This repository has one root commit**; a shallow
clone showed six, and six roots is not a thing a real repository usually has.
A session saw that, read `git merge-base` returning nothing as "unrelated
histories", and ported three upstream commits as new SHAs it could have merged.

**Never reach for `git merge --allow-unrelated-histories` on these two
repositories.** If the histories look unrelated, the clone is shallow. That
flag would duplicate the entire tree.

## "N commits behind main" — read them before acting

```sh
git remote add upstream https://github.com/jggonz/os8088   # once
git fetch upstream main
git log --oneline HEAD..upstream/main
```

Three different situations hide behind that number, and they want different
things:

| what the log shows | what it is | what to do |
|---|---|---|
| `Elendilon: …` / `Elendilon's experimental branch, integrated` | the integration branch's **own work**, squashed home | nothing. The content is already on the branch; the next re-cut absorbs the commit |
| the same, but the checkout predates it | a **stale cut** | re-cut from `main`, or merge if the branch has live work on it |
| an ordinary feature title (`ModPlug Player: …`, `tools/setup-macos.sh: …`) | **genuine upstream work** | go and get it — merge, and expect to *adapt* it (below) |

The titles are reliable because upstream writes them, but confirm rather than
trust: ask whether the tree already has the content.

Mid-cycle upstream work is the case that does **not** self-heal. Waiting for
the next re-cut is a legitimate answer if the branch is about to be retired —
the work arrives free. Merging is the answer if the branch has months left in
it. PRs #56/#57/#58 were this case, and were merged.

## Adapting upstream work to a diverged branch

Upstream work arrives built against **`main`'s kernel — the state as of the
last squash** — while the integration branch has moved on since. Its SDK is
therefore normally a *superset* of the one the incoming code was written
against, and that is exactly the danger: **the code assembles cleanly and every
difference is silent.**

ModPlug Player is the worked example (SPEC.md §56, and §56.13 for the port
itself). The checklist it produced, with each item stated as the general
question rather than the particular answer:

- **SPEC section numbers collide.** The incoming code's section number may mean
  something else on the branch. ModPlug was §52 on `main`; the branch already
  had a hard-disk driver at §52 and ran to §55, so it became §56 and 63
  references across five sources moved with it. Check what the number means
  before keeping it.
- **Compare slot ADDRESSES**, mechanically:
  ```sh
  git show upstream/main:apps/os88api.inc > /tmp/api_main.inc
  grep -ohE 'OSAPI_[A-Z0-9_]+' apps/<pkg>/* | sort -u | while read s; do
      grep -q "^%define $s " apps/os88api.inc || echo "NOT IN THIS SDK: $s"
  done
  ```
- **…and then look for two NAMES at one ADDRESS**, which the check above
  cannot see. It asks "is this slot in the SDK" and answers yes for both
  halves of a collision. When both trees APPEND to the same tail in the same
  round — which is the normal case, `main`'s next free number being the
  branch's next free number too — the merge is **clean**: two `%define`s with
  different names, one address, no conflict marker, and nothing says a word
  until a package calls one and gets the other. That is how
  `OSAPI_DRV_CALL` and `OSAPI_DRV_DLG` both came to sit at `0x0428`.
  ```sh
  python3 - <<'EOF'
  import re, collections
  s = open("apps/os88api.inc").read()
  d = collections.defaultdict(list)
  for m in re.finditer(r'^%define\s+(OSAPI_\w+)\s+KERNEL_SEG:(0x[0-9a-fA-F]+)', s, re.M):
      d[int(m.group(2), 16)].append(m.group(1))
  for a, n in sorted(d.items()):
      if len(n) > 1:
          print("COLLISION 0x%04X: %s" % (a, ", ".join(n)))
  EOF
  ```
  **Run it after every merge from `main`, not only when a conflict points at
  the file** — a conflict is the one signal this failure does not give you.
  The same applies one layer down to `drivers/os88drv.inc`'s `DSV_*` offsets,
  where the collision is a service table whose cells have all shifted: this
  merge's `DSV_PKGCALL` (28 on the branch) met `DSV_CPKEY` (28 upstream), and
  the pad every table carries to `DSV_SIZE` is what kept it to a renumbering
  rather than a driver publishing its file lister as a mouse handler.
- **A renumbered slot invalidates every `.o88` and `.drv`, and `make` rebuilds
  only the ones it builds at all.** The shipped packages follow. The gate and
  benchmark packages under `tests/` are not in `all` — they have their own
  on-demand targets (`make drvcalltest`, `make socktest`, `make bench`) — so a
  binary built against the old number in some earlier session simply *stays on
  its scratch image* until that target is asked for. Their rules do list
  `apps/os88api.inc`, so the rebuild is correct once invoked; nothing invokes
  it. A gate then far-calls whatever now lives at the old address — SPEC.md
  §20.8 rule 4's failure exactly, assembles cleanly and runs wrong — and
  reports the FEATURE as broken. `tests/drvcall.py` did, for a `DRVCALL.O88`
  calling what had become the file dialog, and the hour went into the kernel's
  publication path, which was correct throughout. **Run each gate's own build
  target before believing a post-merge gate failure**; each test's docstring
  names it in its first line.
- **Compare slot CONTRACTS where the address matches** — the same number can
  mean something else. Each of these was, at the time, something the branch had
  and `main` did not: `OSAPI_FONT_GLYPHS` answering `DX:SI` rather than `SI`
  (the glyph table is not in `KERNEL_SEG`); the file-dialog completion proc
  gaining `DX:CX` = the chosen file's size; worker task stacks halving from 512
  bytes to 256.
- **Greying must go through `OSAPI_GFX_PEN`** (SPEC.md §47 rule 1) wherever
  that slot exists. Code written before it greys with `CDGRAY` alone — a real
  grey on VGA and **solid black on Hercules and CGA**, pixel-identical to a
  live control. Every greyed caption in ModPlug's three windows arrived broken
  this way, and it is invisible on the adapter most people test on.
- **Look at it on a 1bpp adapter** before believing it works: `make test
  VIDEO=cga`, and `docs/HERCULES-TESTING.md` for the other one.
- **Check the worker's stack** if the package claims one. The slice is 256
  bytes (SPEC.md §8, §20.6 rule 6). A static worst-case walk of the worker's
  call tree compared against a known-good peer is the cheap check — Tracker's
  worker measures 92 bytes, ModPlug's 98 — and `tests/stackprobe` on real iron
  is the only thing that settles the margin, because SeaBIOS hides a real
  BIOS's interrupt stack use.

## Merging upstream in — resolving without losing things

Expect **add/add** conflicts on anything the branch has already ported, and
content conflicts on the shared prose (`CLAUDE.md`, `SPEC.md`, `Makefile`,
`README.md`, `docs/TESTING.md`, `docs/HERCULES-TESTING.md`).

**`git checkout --ours` silently drops anything `main` has that the branch
lacks.** That is not hypothetical: one such merge recovered a whole CLAUDE.md
paragraph — the fractal restore cache, SPEC.md §40.1 — that an earlier botched
conflict resolution had deleted along with some leftover markers. A blanket
`--ours` would have made that loss permanent.

So verify per file, and prefer a mechanical check to eyeballing when the file
is large (SPEC.md conflicted in sixteen places that day):

```sh
# every non-blank line main added since the merge base that is NOT on our side
BASE=$(git merge-base upstream/main HEAD)
python3 - "$BASE" <<'EOF'
import subprocess, sys, difflib
sh=lambda s: subprocess.run(['git','show',s],capture_output=True,text=True).stdout.split('\n')
base, theirs, ours = sh(sys.argv[1]+':SPEC.md'), sh(':3:SPEC.md'), sh(':2:SPEC.md')
added=[l for t,i1,i2,j1,j2 in difflib.SequenceMatcher(None,base,theirs,autojunk=False).get_opcodes()
       if t in ('insert','replace') for l in theirs[j1:j2] if l.strip()]
ourset={x.strip() for x in ours}
print('\n'.join(l for l in added if l.strip() not in ourset))
EOF
```

Every line it prints is either something to bring across or a paragraph the
branch deliberately rewrote. Decide which, one at a time.

Defaults that have held up:

- **ours** for anything the branch deliberately changed — adaptations, the
  720KB geometry, image counts, branch-only build knobs;
- **theirs** for anything the branch merely *lacks*;
- **theirs for differences with no reason behind them** — a URL, a wording.
  Gratuitous divergence re-conflicts at every future merge, which is the thing
  this document is trying to stop.

Two things such a merge can most easily undo, so check both afterwards:
`git ls-files build | wc -l` must be **0** (SPEC.md §16), and
`kernel/taskmgr.inc` must stay deleted (SPEC.md §28 moved the Task Manager out
to `apps/taskmgr`).

## The PR back to main

Base `jggonz/os8088:main`, head the fork's integration branch. It will be
squashed, and that shapes what to write:

- **The PR title becomes `main`'s entire one-line history entry.** Write it as
  the feature summary, in the house style — `git log upstream/main` is the
  reference.
- **The body is auto-generated**: GitHub concatenates every commit message on
  the branch as `* `-bulleted text. Observed sizes are 7,217 lines (PR #54) and
  4,658 (#51). Nobody edits that down; it is an archive rather than a
  narrative. So the *title* carries the meaning and the individual commit
  messages fill the archive — both are worth writing well, for different
  reasons.
- **Never re-add `build/`.** It is gitignored outright and no artifact in it is
  tracked (SPEC.md §16).
- **The squash replaces `main`'s tree wholesale**, so the branch's SPEC
  numbering becomes `main`'s. When ModPlug landed, `main`'s §52 ModPlug became
  §52 HDD plus §56 ModPlug. That is expected, not a conflict to resolve.
  The second time it happened was PR #92: `main`'s §67 C toolchain met the
  branch's §65 Calculator / §66 Heap compaction / §67 Cyclone 88, so the C
  toolchain became **§70** and its Word/TeXPad references followed Word and
  TeXPad to §68/§69. Git merges that cleanly and `checkdocs` cannot see the
  duplicate `§67` heading, so after any merge in either direction check
  `grep -oE '^#+ [0-9.]+' SPEC.md | sort | uniq -d` by hand.

## Quick reference

```sh
git rev-parse --is-shallow-repository && git fetch --unshallow   # ALWAYS first
git fetch upstream main
git log --oneline HEAD..upstream/main     # what am I behind by, and which kind?
git merge upstream/main                   # only for genuine upstream work
git ls-files build | wc -l                # must be 0 after any merge
make clean && make                        # zero warnings
python3 tools/checkdocs.py                # no NEW problems
```

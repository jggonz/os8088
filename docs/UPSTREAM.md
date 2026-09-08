# Working with upstream — the squash cycle, and how not to lose things in it

This document names `main` and "the integration branch" explicitly and never
says "here", so that it is true wherever it is read — on `main`, on the
integration branch, or on a branch freshly cut from `main`. The two are the
same tree at the moment of a squash and drift apart afterwards; almost
everything below is about that gap.

## The topology

| | |
|---|---|
| `jggonz/os8088` | **upstream.** `main` is the published history: linear, one commit per feature, by policy |
| `Elendilon/os8088` | the fork. Its integration branch — `elendilon` — is where work lands and what gets tested on the iron (`docs/FIELD-MACHINES.md`). The previous cut is kept as `elendilon-old` |

`main` is **squash-only**. The last merge commit on it is PR #14; every commit
since has a single parent and a `(#N)` suffix. That is a deliberate choice
about how `main` reads, and nothing in this document proposes changing it.

## The cycle

```
   cut a branch from main
        │
        ▼
   work on it, many commits, tested on MartyPC and on the 5150
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
clone shows several. An empty `git merge-base` between `main` and the
integration branch means the clone is shallow, not that the histories are
unrelated — the real base is where the branch was cut.

**Never reach for `git merge --allow-unrelated-histories` on these two
repositories.** If the histories look unrelated, the clone is shallow. That
flag would duplicate the entire tree.

## Rule 0b — a squashed branch keeps a huge merge-base diff

Rule 0's sibling, in the opposite direction: there the clone hid history, here
the history is intact and the *branch* is the illusion.

Because `main` squash-merges, a branch whose work has fully landed keeps
**every one of its own commits** and therefore keeps a large diff against
`git merge-base`. `--is-ancestor` says "not merged" about content that is
completely merged, because the squash carried the content and not the commits.

**So never ask what a branch changed against its merge-base. Ask what it
changes against the branch you are actually on.**

```sh
git diff --stat origin/elendilon <branch> -- <paths>   # the real question
git log --oneline -5 origin/elendilon -- <path>        # ...and who last touched it
```

The tell is the sign: a branch whose diff against the integration branch is
net-**negative** is *behind* it, not ahead of it, and content that far behind
is content that already landed. **A net-negative diff is merged work, not
pending work.**

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
| `Elendilon -> Main (…)` (older rounds: `Elendilon: …`, `Elendilon's experimental branch, integrated`) | the integration branch's **own work**, squashed home | nothing. The content is already on the branch; the next re-cut absorbs the commit |
| the same, but the checkout predates it | a **stale cut** | re-cut from `main`, or merge if the branch has live work on it |
| an ordinary feature title (`ModPlug Player: …`, `tools/setup-macos.sh: …`) | **genuine upstream work** | go and get it — merge, and expect to *adapt* it (below) |

The titles are reliable because upstream writes them, but confirm rather than
trust: ask whether the tree already has the content.

Mid-cycle upstream work is the case that does **not** self-heal. Waiting for
the next re-cut is a legitimate answer if the branch is about to be retired —
the work arrives free. Merging is the answer if the branch has months left in
it.

## Adapting upstream work to a diverged branch

Upstream work arrives built against **`main`'s kernel — the state as of the
last squash** — while the integration branch has moved on since. Its SDK is
therefore normally a *superset* of the one the incoming code was written
against, and that is exactly the danger: **the code assembles cleanly and every
difference is silent.**

ModPlug Player is the worked example (SPEC.md §56, and §56.13 for the port
itself). The checklist, each item as the general question:

- **SPEC section numbers collide.** The incoming code's section number may mean
  something else on the branch (ModPlug was §52 on `main` and became §56,
  because the branch already had a hard-disk driver at §52). Check what the
  number means before keeping it.
- **Check every slot the package names is in THIS SDK**, mechanically:
  ```sh
  grep -ohE 'OSAPI_[A-Z0-9_]+' apps/<pkg>/* | sort -u | while read s; do
      grep -qE "^(%define +)?$s\b" apps/os88api.inc || echo "NOT IN THIS SDK: $s"
  done
  ```
  (`OSAPI_FIND_SZ` and `OSAPI_FT_DIR` are `equ` constants rather than
  `%define` slots, which is what the optional prefix is for.)
- **…and then look for two NAMES at one ADDRESS**, which the check above
  cannot see. It asks "is this slot in the SDK" and answers yes for both
  halves of a collision. When both trees APPEND to the same tail in the same
  round — the normal case, `main`'s next free number being the branch's next
  free number too — the merge is **clean**: two `%define`s with different
  names, one address, no conflict marker, and nothing says a word until a
  package calls one and gets the other. It has happened once (`OSAPI_DRV_CALL`
  and `OSAPI_DRV_DLG` both landed at `0x0428`).
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
  The same applies one layer down to `drivers/os88drv.inc`'s `DSV_*` offsets:
  two verbs appended at the same offset on both sides is a service table whose
  cells have shifted, and the pad every driver carries to `DSV_SIZE` is what
  keeps that to a renumbering rather than a driver publishing one verb as
  another.
- **A renumbered slot invalidates every `.o88` and `.drv`, and `make` rebuilds
  only the ones it builds at all.** The shipped packages follow. The gate and
  benchmark packages under `tests/` are not in `all` — they have their own
  on-demand targets (`make drvcalltest`, `make socktest`, `make bench`) — so a
  binary built against the old number in an earlier session stays on its
  scratch image until that target is asked for. A gate then far-calls whatever
  now lives at the old address, assembles cleanly, runs wrong, and reports the
  FEATURE as broken (`tests/drvcall.py` once reported the driver-call path
  broken for a `DRVCALL.O88` that was calling the file dialog). **Run each
  gate's own build target before believing a post-merge gate failure**; each
  test's docstring names it in its usage line.
- **Compare slot CONTRACTS where the address matches** — the same number can
  mean something else. The shape to look for: a slot answering `DX:SI` where
  the incoming code expects `SI` (`OSAPI_FONT_GLYPHS`, the glyph table not
  being in `KERNEL_SEG`); a completion proc gaining an output (the
  file-dialog proc's `DX:CX` = the chosen file's size); a constant such as
  `SCH_STACK` moving. Check the constant, never a sentence in a doc.
- **Greying must go through `OSAPI_GFX_PEN`** (SPEC.md §47 rule 1). Code
  written before that slot greys with `CDGRAY` alone — a real grey on VGA and
  **solid black on Hercules and CGA**, pixel-identical to a live control. It
  is invisible on the adapter most people test on.
- **Look at it on a 1bpp adapter** before believing it works: `make test
  VIDEO=cga`, and `docs/HERCULES-TESTING.md` for the other one.
- **Check the worker's stack** if the package claims one. A worker gets the
  stack CLASS its package header declares (SPEC.md §8.7, §20.6 rule 6), and
  `SCH_STACK` = **384** bytes is the largest class — the constant is in
  `kernel/sched.inc` and mirrored in `apps/os88api.inc`; read it there, not
  here. `python3 tools/stkdepth.py apps/<pkg>/<pkg>.asm --from <worker>` is
  the static worst-case walk, and comparing against a known-good peer is the
  cheap check (Tracker's worker walks to 80 bytes, ModPlug's to 98).
  `make stackprobe` on real iron is the only thing that settles the margin,
  because SeaBIOS hides a real BIOS's interrupt stack use.

## Merging upstream in — resolving without losing things

Expect **add/add** conflicts on anything the branch has already ported, and
content conflicts on the shared prose (`CLAUDE.md`, `SPEC.md`, `Makefile`,
`README.md`, `docs/TESTING.md`, `docs/HERCULES-TESTING.md`).

**`git checkout --ours` silently drops anything `main` has that the branch
lacks.** A whole CLAUDE.md paragraph (the fractal restore cache, SPEC.md
§40.1) was lost that way once and only recovered by a later merge; a blanket
`--ours` would have made the loss permanent.

So verify per file, and prefer a mechanical check to eyeballing when the file
is large (SPEC.md has conflicted in sixteen places in one merge):

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

After any merge in either direction, three checks git merges cleanly and
reports nothing about:

```sh
grep -oE '^#+ [0-9.]+' SPEC.md | sort | uniq -d   # duplicate § headings: checkdocs resolves a
                                                  # citation to the FIRST match and is happy
git ls-files build | wc -l                        # must be 0 (SPEC.md §16)
ls kernel/taskmgr.inc                             # must NOT exist (SPEC.md §28 moved it to apps/taskmgr)
```

## The PR back to main

Base `jggonz/os8088:main`, head the fork's integration branch. It will be
squashed, and that shapes what to write:

- **The PR title becomes `main`'s entire one-line history entry.** Write it as
  the feature summary, in the house style — `git log upstream/main` is the
  reference.
- **The body is auto-generated**: GitHub concatenates every commit message on
  the branch as `* `-bulleted text, thousands of lines, and nobody edits it
  down. It is an archive rather than a narrative, so the *title* carries the
  meaning and the individual commit messages fill the archive — both are worth
  writing well, for different reasons.
- **Never re-add `build/`.** It is gitignored outright and no artifact in it is
  tracked (SPEC.md §16).
- **The squash replaces `main`'s tree wholesale**, so the branch's SPEC
  numbering becomes `main`'s and it is the section coming *from `main`* that
  renumbers. That is expected, not a conflict to resolve; scope the rewrite to
  the lines `main` added (`git diff $BASE <main-commit>`), not to the whole
  file. Precedents:
  - #58: `main`'s §52 ModPlug met the branch's §52 HDD — ModPlug became §56.
  - #92: `main`'s §67 C toolchain met the branch's §65 Calculator / §66 Heap
    compaction / §67 Cyclone 88 — the C toolchain became §70 and its
    Word/TeXPad references followed those to §68/§69 (it has since moved on
    to §73).
  - #144: `main`'s §86 Hibernate (#143) met the branch's §86 Audio Player —
    Hibernate became §87, 99 lines across `kernel/hiber.inc`,
    `kernel/ui.inc`, `tests/hibernate.py` and the rest.
  - #172: `main`'s §88 The Wire (#158) met the branch's §88 Clear Skies — The
    Wire became **§92**, 272 citations, seven of them outside the conflict
    region and so resolving silently onto Clear Skies' subsections;
    PERFORMANCE.md's Set 116 collided the same way and PACCMAN's became Set
    118. **The API table collided in the same round and merged CLEANLY**:
    both sides appended to the same tail, so `apps/os88api.inc` came out with
    no conflict marker and two names at each of 0x04F8 and 0x0500 —
    `OSAPI_DECOMP`/`OSAPI_PKG_RUN` and `OSAPI_FILE_FIND_RAW`/`OSAPI_DESK_SVC`.
    The branch kept 0x04F8–0x0518 and `main`'s two moved to 0x0520 and 0x0528.
    This is the failure the collision check above exists for, and it is the
    check that found it.

## Quick reference

```sh
git rev-parse --is-shallow-repository && git fetch --unshallow   # ALWAYS first
git fetch upstream main
git log --oneline HEAD..upstream/main     # what am I behind by, and which kind?
git merge upstream/main                   # only for genuine upstream work
grep -oE '^#+ [0-9.]+' SPEC.md | sort | uniq -d   # no duplicate § headings
git ls-files build | wc -l                # must be 0 after any merge
make clean && make                        # zero warnings
python3 tools/checkdocs.py                # no NEW problems
```

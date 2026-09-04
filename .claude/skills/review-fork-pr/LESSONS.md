# What eight of these reviews cost to learn

Read before step 2 of [`SKILL.md`](SKILL.md). Every item below happened on a
real incoming PR to this repository — #47, #54, #61, #66, #73, #80, #92 and #112 —
and every one of them was silent: the build passed, git reported nothing, and
the emulator looked right.

`docs/UPSTREAM.md` is the companion to this file and is not repeated here: it
owns the fork topology, the squash cycle, the shallow-clone trap and the
merge-resolution defaults. This file is the maintainer's side — reviewing,
fixing and pushing someone else's branch.

---

## 1. The git mechanics that quietly do the wrong thing

**Pushing to `origin` does not update the PR.** The PR's head lives on the
contributor's fork. `git push origin HEAD:elendilon-pr` succeeds, creates a
branch of that name on *your* repository, and leaves PR #66 exactly as it was.
The session that did it noticed only because it checked `gh pr view --json
commits` afterwards, and the cleanup was `git push origin --delete`. The
target is always the fork:

```sh
git push git@github.com:<owner>/os8088.git HEAD:<headRefName>
# or, with the remote added once:
git remote add <owner> git@github.com:<owner>/os8088.git
git push <owner> pr-<N>:<headRef>
```

**Check `maintainer_can_modify` before planning to push at all.** It is the
"allow edits by maintainers" box on the contributor's side. `gh pr view` calls
it `maintainerCanModify`; `gh api repos/jggonz/os8088/pulls/<N>` calls it
`maintainer_can_modify`. If it is false, the whole push path is closed and the
work goes into an integration PR on `origin` instead (#47's shape: their
commits plus yours, one PR the user merges themselves).

**Re-fetch immediately before pushing.** Contributors work while you review.
Twice a branch moved mid-review; once the fix was `git rebase FETCH_HEAD`
(#61) and once `git rebase --onto <owner>/<headRef> <your base> pr-<N>` (#80).
Test first, and never resolve a divergence with `--force`:

```sh
git fetch <owner> <headRef>
git merge-base --is-ancestor <owner>/<headRef> HEAD && echo "fast-forward OK"
git log --oneline <your base>..<owner>/<headRef>     # what they added
```

**Work in a worktree, not the user's checkout.** `git worktree add -q
"$SCRATCH/wt" pr-<N>` keeps the user's tree usable for the hours this takes,
and keeps a failed merge out of it. Remove it with `--force` at the end and
`git worktree prune`; the local `pr-<N>` branch survives for the user to test.

**A scratchpad path is too long for a QMP socket.** `AF_UNIX` caps the path at
~104 bytes and the session scratchpad eats most of that. Boot with
`/tmp/q<N>.sock`, or build in the worktree and point `BUILD=` somewhere short.
The failure mode is QEMU refusing to start in a way that reads like a broken
image.

**A stale QEMU from a previous session answers on `build/qmp.sock`** with the
*old* kernel. Every screendump succeeds and shows unchanged behaviour, which
reads exactly like a fix that did nothing. `pgrep -fl qemu-system` before you
believe a negative result, and compare its start time to `build/kernel.bin`.

## 2. The defect class unique to a long-lived fork: things `main` had and the branch does not

This is the highest-yield review lens in this repository, and **git reports no
conflict for it**. A branch cut months ago never received the fix `main`
landed last week; the merge is clean; the fix is simply gone from the result.

- #47 deleted `wm_destroy_seg` — `main`'s sweep that closes a dying package's
  unbound windows — and documented the resulting use-after-free as an author
  rule in SPEC.md. The branch's own SPEC section said, in effect, "an orphaned
  window carries `W_SEG`/`W_DISP` into a freed region and the next repaint far
  calls whatever claimed it." A rule in the SPEC is not a substitute for the
  guard it replaced; the sweep was restored.
- #47 also lost `snd_unhook`'s obligation when the card code moved into
  `SOUND.DRV`: `int 19h` resets no hardware, so a hooked Sound Blaster rode
  the reboot with its vector pointing into a heap segment the next boot reuses.
- #73's second pass found `cache_for()` reading `wm_su_seg` and `wm_su_win` —
  two variables **the same branch had deleted**.

How to find them, mechanically:

```sh
BASE=$(git merge-base main pr-<N>)
git diff --diff-filter=D --name-only $BASE...pr-<N>       # files deleted outright
git log --oneline $BASE..main                             # every commit the branch has not got
git log --oneline $BASE..main -- <file>                   # per file, for anything the PR rewrites
git show main:<file>                                      # what main's version actually says
```

Read every hunk that *deletes* rather than adds. For each, ask what it was
guarding and whether the guard exists anywhere else in the merged tree.

## 3. The API slot table breaks the ABI in silence

`apps/os88api.inc` maps a slot number to a far-call cell. A package built
against `main`'s SDK carries **numbers**, not names.

#47 retired the paragraph arena and moved every entry from `0x01B8` upward
down three cells. Everything assembled. Everything ran. From `0x01B8` up, a
package built against `main` called `wm_resize` where it meant `cm_alloc`.
The resolution: **every slot `main` has published keeps `main`'s number**;
retired ones stay live at their address, and new work starts at a fresh block.

Cross-check mechanically, by address and by name, and check the *contract* as
well as the number — `docs/UPSTREAM.md` has the loop. The same address meaning
something different is the version of this that survives a name check:
`OSAPI_FONT_GLYPHS` answering `DX:SI` rather than `SI`, the file-dialog
completion proc gaining `DX:CX`, worker stacks halving from 512 to 256 bytes
(both trees are 384 now - that one converged, and the lesson is the SHAPE).

The driver tables have the same shape one level down: #92 grew `FSV_SIZE` from
28 to 30 verbs without bumping the driver header version, so an older `.DRV`
and a newer kernel disagreed about the length of a table they both index.
SPEC's own table still said 28.

## 4. SPEC section numbers collide, and `checkdocs` cannot see it

Both sides add sections; git merges both cleanly; the file now has two `§67`
headings and `tools/checkdocs.py` — which checks that a citation *resolves* —
is happy, because it resolves to the first one.

```sh
grep -oE '^#+ [0-9.]+' SPEC.md | sort | uniq -d     # run after EVERY merge, both directions
```

The branch's numbering wins at the squash (`docs/UPSTREAM.md`), so the section
coming *from `main`* is the one that renumbers. In #92 `main`'s §67 C
toolchain became §70 and its Word/TeXPad references followed those to §68/§69
— 323 lines. Scope the rewrite to the lines `main` added
(`git diff $BASE <main-commit>`), never to the whole file, and inventory the
reference forms before writing the script: `§67`, `SPEC.md 67`, `section 67`,
`(67.3)`, and headings. Check for false positives — bare `67`s that are menu
indices or counts. Record the renumbering in `docs/UPSTREAM.md`; that file
already carries two precedents and is where the next session will look.

`git show c257e9f:SPEC.md | grep -oE '^#+ [0-9.]+' | sort | uniq -d` on the
*base* tells you which duplicates predate the PR and are not yours to fix.

## 5. What rides in that should not

- **Binaries.** #47 carried a Protracker `.MOD` ("OS8088 TEST") that arrived
  with an unrelated commit. `git ls-files build | wc -l` must be `0`
  (SPEC.md §16), and `git ls-files | grep -E '\.(bin|o88|img|mod|drv)$'`
  finds the rest.
- **`.claude/`.** #54 checked in a settings file and a session-start hook. Ask
  the user; the answer has been "no" both times, and the resolution is to
  take `main`'s version of the directory rather than delete blindly.
- **`.gitignore` regressions.** #47 lost three `vm/*/nvr/` ignores in a merge,
  so an emulator's non-volatile state was one boot away from being committed.
- **Host paths hard-coded in tools.** #92's `tests/heapcheck.py` drives
  MartyPC with `/home/user/os8088` baked in. Check whether the pattern
  predates the PR (`git show main:<file>`) before reporting it — that one did,
  so it was a nit and not a finding.

## 6. The memory-safety defects these PRs actually shipped

The hard rules in `CLAUDE.md` say what the invariants are. This is what
breaking them has looked like in practice, and every item was found by
reading, not by running:

- **A pointer banked across a call that can move memory.** `clip_put` held the
  caller's segment in `BP` across `mem_claim`, then copied from it. With heap
  compaction (#92) that segment can move inside the call, and the copy reads
  whatever now lives there. Anything holding a segment or an offset across a
  claim, a free, a yield or a callback is suspect.
- **A register promise broken inside a wait.** `inst_park_wait` clobbered `CL`
  while only `AX`/`BX`/`DI` were pushed above it — quietly breaking
  `TASK_ALIVE`'s documented register contract for every caller.
- **A callback clobbering more than the caller banked.** `mem_reloc_call` must
  save `AX/BX/CX/DX/SI/DI/ES`, because a package's relocation callback may use
  all of them. "The loop keeps its cursor in SI across the callback" is the
  same bug written optimistically.
- **A movable buffer under a `rep movsb` from another task.** The Sound
  Blaster staging pool stayed movable while a client task copied into it. The
  fix kept the contributor's design (the pool stays movable) and chunked the
  copy under the existing guard, rather than pinning the pool because pinning
  was easier.
- **State not cleared on slot reuse.** `inst_parksafe[]` survived into the
  next instance to occupy the slot.
- **`ES` and `DS` at a boundary.** A callback is entered with
  `ES = KERNEL_SEG`, so it is `[es:bx+W_W]`; `SS ≠ DS`, so kernel code holding
  a pointer in `BP` needs `[ds:bp+…]`. #47's `ui_dispatch` read a
  package-owned `AM_ONCMD` through kernel `DS` and took the right branch only
  while a random word of kernel `.text` happened to be non-zero.
- **A critical section that does not cover its own parameters.**
  `mem_claim`/`_hi`/`_dma` stored their parameter globals *before* the
  `pushf`/`cli`, so a pre-empted claim could run with another task's
  constraint — and a lost DMA-page constraint means the 8237 wraps and moves
  the wrong memory.
- **A teardown wait measured in the wrong units.** A driver's 40-yield wait
  completed in microseconds against a worker that sleeps a 55 ms tick, so
  unload-during-playback freed an image with the worker's `CS` still in it.
  The kernel counts the *death*, inside `task_exit`'s `cli`.

## 7. Disk, files and formats

- **A failure that does not propagate.** A failed FAT write reported success
  up the stack (#61).
- **The right refusal with the wrong reason.** When those writes were made to
  propagate, both callers reported `FERR_FULL` — "Disk full" — on a marginal
  drive with thousands of free clusters, which sends the user deleting files
  that were never the problem. A wrong error message is a real defect; the
  follow-up comment on #61 exists because of it.
- **Names from the wire.** #92's `NET.DRV` server took a name from the client
  and joined it to a path with no validation, so `..\` reached a recursive
  delete. The fix went into `path_join` — the single choke point every
  name-taking verb already passed through — and each caller routed the refusal
  into the `FERR_NOENT` path it already had.
- **A size that only fits in a word.** A synthesized directory entry clamped
  the file size to `0xFFFF`, so a 116 KB file listed as 65535 bytes.
- **512-byte alignment** for anything disk-visible, or `int 13h` answers `09h`
  on a transfer crossing a 64 KB boundary. The symptom is "Disk error" on a
  large save.

## 8. Cost, not pixels

Review against `CLAUDE.md`'s table and `PERFORMANCE.md` Part 5's standing
budget. **A change that reintroduces a full repaint is a regression against a
documented number, not a neutral refactor.** The three defects an emulator
cannot show you — a visible redraw, a double-draw flash, input overrun — are
found by counting primitive calls in the diff, so count them: a `gfx_*` call
is 756 µs whatever it draws, an `int 13h` ~400 ms whatever it moves.

A PR that says of itself "damage rects in progress but not finished" (#80's
body did) is telling you where its visible defects are. Read that sentence as
a review assignment.

## 9. Verification that has actually caught things

- **Re-run the PR's own claimed gate at its HEAD, before reviewing a line.**
  A commit message that says "`make test-full` green, 16/16" is a claim about
  the commit it is attached to, not about the branch. #112's was honest at the
  squash and false two commits later: `kern_small` measured **106,496 of
  106,496 — zero spare, one byte left inside the image rung** — and the boot
  fix that followed added six bytes, crossed a whole 512-byte rung, and put it
  512 over. `make small` then did not assemble at all and `buildmatrix` failed,
  which is the documented pre-merge gate failing on the tree you are asked to
  merge. Two minutes of `python3 tools/os88test.py full` answers it. And when
  the answer is a size guard, `git checkout <their earlier commit>` and measure
  there too: *which* commit crossed the rung is the difference between a
  contributor who did not run the gate and one whose gate was green when they
  ran it.
- **A guard is only worth what its rows actually build.** #112 broke four
  documented knob builds — `FLOPPY1=1`, `DISKAL=1`, `BOOTDIAG=1`, `BOOTSTOP=1`
  — every one dying with `boot/boot.asm: TIMES value -N is negative` because
  the sector had reached 510 of 510. `make field` was dead with them, and that
  takes `cqdiag.img` with it: the diagnostic floppy for a machine that will not
  start, killed by the commit whose whole subject is machines that will not
  start. `t_buildmatrix` could not see any of it, because its rows ask make for
  `<out>/kernel.bin` and never for a boot sector. So: when a PR touches
  `boot/boot.asm`, assemble **every** knob that reaches `BOOTDEF` yourself —
  and check what the gate's rows actually name before believing its green.
- `make` **and** `make KERN_SMALL=1`. A change can fit one kernel and not the
  other, and a symbol can exist only in the big one — a fix that used
  `vid_disp_find` broke `kern_small`, which has no second display, until it
  went under the same guard as its neighbour.
- `python3 tools/kernsize.py --build … --bless` for **both** variants, in the
  same commit as the change, as `docs/KERNEL-MEMORY.md` asks.
- `tools/os88disk.py --verify` on all three geometries.
- **Boot it.** Then exercise the feature the PR is about: #92's compaction
  fixes were only believable once Paint drew a stroke, saved `PICTURE.BMP`,
  and the disk fsck'd clean with a sane 448×280 header afterwards.
- **Run the PR's own gate** if it ships one (`tests/heapfrag` for compaction,
  `make zcheck`/`make zgfx` for Frotz, `make rczex` for the Z80 core).
- **Look at it on a 1bpp adapter.** Grey rounds to black on Hercules and CGA,
  so a greying change that looks right on VGA can be pixel-identical to a live
  control there.
- A driver needs a card to attach to: `make test ADLIB=1` (QEMU's SB16 OPL
  stub does not answer the boot probe on its own — add the AdLib device too).

## 10. Reviewing at scale

- **Split by subsystem *and* by lens.** `kernel/wm.inc` has arrived with 4,200
  changed lines; one reviewer takes window state and events, another takes
  painting and redraw cost, and each is told the other exists so they do not
  duplicate. 8–14 units for a PR of 100+ files.
- **Give every reviewer the hard rules verbatim**, and the performance table,
  and tell it that violations of those rules are the highest-value findings.
  Tell it what *not* to report: style, naming, "could be refactored",
  speculation it cannot point at a line for. Three solid findings beat twelve
  soft ones, and a cap of ~8 per unit keeps the tail honest.
- **Verify adversarially.** A second agent tries to *refute* each finding and
  returns `CONFIRMED`/`REFUTED`/`UNCERTAIN` with a corrected fix. Roughly a
  third do not survive. Two independent reviewers landing on the same defect
  is the strongest signal available — both the `CL` clobber and the SB pool
  hazard came in twice.
- **Read the top findings yourself** before calling anything a blocker. Both
  #92 blockers were confirmed by eye in a couple of minutes.
- Do the mechanical work while the agents run: the slot cross-check, the
  deletion sweep, the size guards, a boot.

## 11. Fixing inside someone else's PR

- Minimal and surgical; match the surrounding style exactly, down to the
  trailing-comment column and the way `§` is written.
- **Keep their intent.** Fix the hazard the way their design already handles
  that class of hazard.
- **Decline what is not real.** A bad edit to working code is worse than a
  missed fix, and the report should say why you declined.
- **Update SPEC.md in the same edit** as anything it pins, and add the author
  rule where the design already keeps its author rules.
- **Never raise `KERN_BUDGET` or `KERN_CODE_MAX` as a build fix.** Align a
  stale symbol to the real value; widen an instrumented-build exemption; but
  the shipped guard is the user's decision, with whoever asked for the feature.
- One commit for the merge, one for the fixes, each message naming the § it
  touches. Do not amend, rebase or squash the contributor's commits.

## 12. The comment on the PR

The reader is the contributor, in a browser, semi-technical. What has worked:

- Two opening lines: what you merged, how many commits you pushed, and that
  the PR is now mergeable.
- **One item per fix**, in this order: what would have gone wrong *in the
  user's world*, then the mechanism in one sentence, then the change and its
  sha. The disk-full example is the model — "a machine with thousands of free
  clusters would have told you the disk was full, which sends you off deleting
  files that were never the problem."
- A **flagged, not fixed** section, and a line about what you checked and
  cleared — "I looked and it was fine" is only worth reading if it says where.
- No blame. These defects live in code that took real work, and the review is
  a second pair of eyes, not a verdict.

## 13. The checklist

```
[ ] gh pr view: headRepositoryOwner, headRefName, maintainer_can_modify, mergeable
[ ] not a shallow clone; PR fetched; scratch worktree; user's tree untouched
[ ] merge base found; main-side commits read; DELETED files read one by one
[ ] main merged in; conflicts resolved per docs/UPSTREAM.md defaults
[ ] duplicate § headings checked by hand; renumber scoped to main-added lines
[ ] git ls-files build == 0; no .claude/, no binaries, no lost .gitignore lines
[ ] API slots cross-checked by address AND contract; driver table versions bumped
[ ] review units written; every finding adversarially verified; blockers read by eye
[ ] plan presented; user approved before anything was written
[ ] fixes minimal, SPEC updated with them, no guard relieved
[ ] the PR's own claimed gate re-run at ITS HEAD, not trusted from the message
[ ] make; make KERN_SMALL=1; kernsize --bless both; disk --verify; boot; VIDEO=cga
[ ] every knob build the changed files reach - a gate's rows may name the wrong target
[ ] the PR's own gate run; the feature exercised end to end
[ ] re-fetched the fork; fast-forward proven; pushed to the FORK, not origin
[ ] PR comment posted: per-fix consequence, mechanism, sha; flagged section
[ ] worktree removed, no QEMU left running, local branch left for the user
```

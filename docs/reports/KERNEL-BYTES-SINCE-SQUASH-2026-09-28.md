# Kernel bytes since the #199 squash, by concept, with `main`'s arm separated

**A measurement, not a description.** Taken 2026-09-28 on a four-core cloud
container: `nasm` 2.16.01, Python 3.11.15. Every figure comes from
`tools/kernsize.py --json` and `--modules`, which RE-ASSEMBLE the kernel
rather than reading a build. Each point was measured in a clean worktree of
its own commit, with that commit's own `kernsize` and its own generated
includes, and no figure is carried between points. It is true of the commits
it names and of no other tree.

It is the fifth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-07.md`,
`docs/reports/PR-CYCLE-ACCOUNTING-2026-09-11.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-17.md` and
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-25.md`, and is an edit of
none of them. It is taken to open the next `elendilon -> main` PR, so the
question is the PR's: **what does this squash cost `main`'s kernel?**

**It is taken before the cycle is closed.** Work from the Video Player
session has still to land on `elendilon`. B below is the branch as it stands;
if what lands touches a kernel input, the bill moves, and the answer is a new
report rather than an edit of this one.

## The points

| | commit | what it is |
|---|---|---|
| **A** | `6212352d` | **the last squash with `main`**: *Elendilon -> Main (Kernel -4.5KB, Word Optimized, Tracker and MogPlug Merged, DOS Bugfixes, Blit1 30% faster, Sound Driver -8 to 10kb ram usage)* (#199), 2026-09-26 |
| **B** | `7b54c2d2` | `elendilon` at its tip, **+59 commits** over A (48 of them not merges) |
| **C** | `b6e1a6d6` | upstream `jggonz/os8088` `main` at its tip, **+1 commit** over A: #201 |
| **D** | `fbe01cf8` | a TRIAL merge of B and C, built for this report and pushed nowhere: what the PR carries |

**A→B is the branch's own arm, A→C is everything `main` did, and A→D is the
bill.** `git merge-base` of B and C is A, so the arms share no commit and
there is no overlap to discount. The fork's own `origin/main` is still at A;
C was fetched from upstream, which is one squash ahead of it.

**D does not merge cleanly, and neither conflict is a kernel input.** The
trial merge stops in `SPEC.md` and `docs/INDEX.md`, and the reason is below
(*A section-number collision*). For the measurement D takes `elendilon`'s
side of those two files; `git diff B D -- kernel apps/os88ui.inc boot` is
empty, so the resolution cannot move a kernel byte either way.

**The build number contributes nothing to any delta here.** `BUILD_STR` is
the commit count (SPEC.md 14.2), and the four counts are 166, 225, 167 and
227: three digits everywhere.

**`associco.inc` is byte-identical at all four points** (`676544b2...`), on
both kernels, and at every one of the 64 intermediate points measured below.
It is the one generated include a PACKAGE commit could move a kernel byte
through (it is built from Paint's, Note Pad's, Tracker's, Artful's and the
DOS box's icons). `font8x8.inc` is generated only under the `FONT=` knob and
no shipped kernel reads it. So every byte below came out of the 58 files a
walk of `%include` from `kernel/kernel.asm` finds, plus `apps/os88ui.inc` and
`boot/boot2.asm`.

**`KERN_BUDGET` is 129,536 and `KERN_SMALL_BUDGET` 107,520 at all four
points.** Nothing below is a budget move.

## Headline: `kern_big`, the shipped default

| section | A base | B elendilon | C main | D merged | **B−A** | **C−A** | **D−A** |
|---|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 47,383 | 47,767 | 47,383 | 47,767 | **+384** | **0** | **+384** |
| `.bss` | 5,525 | 5,545 | 5,525 | 5,545 | **+20** | **0** | **+20** |
| `.cold` | 40,447 | 40,831 | 40,447 | 40,831 | **+384** | **0** | **+384** |
| `.ovl` | 1,837 | 1,837 | 1,837 | 1,837 | 0 | 0 | 0 |
| `.ovlw` | 5,104 | 5,105 | 5,104 | 5,105 | +1 | 0 | +1 |
| `.lowbss` | 6,366 | 6,366 | 6,366 | 6,366 | 0 | 0 | 0 |
| `.vgabuf` | 336 | 336 | 336 | 336 | 0 | 0 | 0 |
| **sum** | | | | | **+789** | **0** | **+789** |
| **`KERN_SIZE`** | 105,984 | 107,008 | 105,984 | 107,008 | **+1,024** | **0** | **+1,024** |
| spare of `KERN_BUDGET` | 23,552 | 22,528 | 23,552 | 22,528 | | | |

**+788 resident** (`.text`, `.bss`, `.cold`, `.lowbss`, `.vgabuf`) and +1
overlay. **Two rungs crossed**: the image rung once and the cold rung once.
A is exactly the previous report's endpoint (105,984), so the two files
chain.

`.boot2`, the stage-2 blob and not the kernel, is 2,249 at all four points.

## Headline: `kern_small`, the 128KB floor machine

| section | A base | B elendilon | C main | D merged | **B−A** | **C−A** | **D−A** |
|---|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 35,692 | 35,574 | 35,692 | 35,574 | **−118** | **0** | **−118** |
| `.bss` | 3,485 | 3,481 | 3,485 | 3,481 | **−4** | **0** | **−4** |
| `.cold` | 26,073 | 25,900 | 26,073 | 25,900 | **−173** | **0** | **−173** |
| `.ovl` | 1,942 | 1,942 | 1,942 | 1,942 | 0 | 0 | 0 |
| `.ovlw` | 1,502 | 1,502 | 1,502 | 1,502 | 0 | 0 | 0 |
| `.lowbss` | 3,636 | 3,636 | 3,636 | 3,636 | 0 | 0 | 0 |
| **sum** | | | | | **−295** | **0** | **−295** |
| **`KERN_SIZE`** | 71,168 | 71,168 | 71,168 | 71,168 | **0** | **0** | **0** |
| spare of `KERN_SMALL_BUDGET` | 36,352 | 36,352 | 36,352 | 36,352 | | | |

**−295 resident, no rung moved.** The floor machine's kernel is smaller in
every resident section it has and the same size in rungs, which is the banner
in CLAUDE.md read from the other side: 295 bytes of slack went back to
whoever is next, and the heap figure did not move.

## `main`'s arm: zero, measured

**#201 costs the kernel NOTHING, on both kernels, in every section and every
module row of `kernsize --modules`.** C is A to the byte and D is B to the
byte.

| commit | what it touched | kernel bytes |
|---|---|---:|
| #201 `b6e1a6d6` *Add native Gorillas with solo play, animated matches, and final scores* | its own package (`apps/gorillas/`), its tests, two tools, the Makefile, the docs, and 124,000 lines of reference material under `reference/` | **0** |

Gorillas' cost is a package and not the kernel: `GORILLAS.O88` is **14,261
bytes** on disk (24,787-byte image, 29,553 of `.bss`), in `$(APPS_GAMES)`,
the games category disk (SPEC.md 24.6). It is billed to a floppy, never to a
machine's resident RAM.

This is the second cycle running in which `main`'s arm is exactly zero on
both kernels.

## Our arm, by concept

**The attribution is exact, not apportioned.** Every non-merge commit in
A..B, all 48 of them, was measured against its own first parent on both
kernels, 55 distinct points each in a clean worktree. 11 move a byte.
**The brackets sum to the A→B total in every section on both kernels with
one residual, and it is explained to the byte**: +6 of overlay, because the
same change (*"`sched_init` zeroes `[ticks]` again"*, `b6410f33` and
`7e558089`) was made on two branches and the merge carries it once. The
arm's 11 merges contributed no byte of their own.

### Two of the commits are squashes, and were taken apart

**`927c5aab` is a re-squash.** Its message says what it is: *"Everything
elendilon did after the commit #199 was squashed from (8f5dafe9), plus kernel
size pass 5, as one commit on top of main."* One bracket over it says only
−32 / −297, which is two opposite things netted. The old line is still on the
remote as `origin/elendilon-old`, so it was measured too: `8f5dafe9`,
`dfbe2b3b` (the tip pass 5 was cut from), and each of the nine commits
between them that touch a kernel input, against its own first parent.

| | `kern_big` resident | `kern_small` resident |
|---|---:|---:|
| the old line's work, `8f5dafe9` → `dfbe2b3b` (nine brackets, sum exact) | **+894** | **+80** |
| main's #199 bugfix round, `8f5dafe9` → A | +65 | −6 |
| `dfbe2b3b` → `927c5aab` | −861 | −383 |
| **so pass 5 itself**: (`dfbe2b3b` → `927c5aab`) − (the bugfix round) | **−926** | **−377** |
| **check**: old work + pass 5 = `927c5aab` − A | +894 − 926 = **−32** | +80 − 377 = **−297** |

**Pass 5's record is confirmed to the byte.**
`docs/plans/completed/HANDOFF-KERNEL-SIZE-P6.md` quotes kern_big −926 and
kern_small −377 against `dfbe2b3b`, and the base "+894 resident bytes over
pass 4's blessed baseline". The pass's own branch (`kernel-size-p5`) is not on
the remote, so its close could not be measured directly. The identity above
measures it anyway, and it also shows that the bugfix round's bytes survived
being ported by hand onto the rewritten code with nothing lost and nothing
duplicated. The overlay's −5 / −6 in `927c5aab` is pass 5's too.

**`5e88d453` is the Video Player's second squash** (*"Everything on
video-player that 927c5aab did not carry... 36 commits, as one commit"*). Its
message quotes no total, so it was split by file with `kernsize --modules`
on it and its parent: on `kern_big`, `snd.inc` +271 (`.text` +265, `.bss`
+6), `sched.inc` +24, `fsx.inc` +13 and the API table +10 are the PC-speaker
PCM path (SPEC.md 34.11), and `vga12.inc` +112 (`.text` +45, `.cold` +65,
`.bss` +2) is `OSAPI_GFX_BLITP` walking a clip for Live in colour (5.4.3.6).
On `kern_small`, `snd.inc` +5 and the API table +6.

### What it added

| concept | commits | **`kern_big` resident** | overlay | **`kern_small` resident** | overlay |
|---|---|---:|---:|---:|---:|
| **the Video Player's kernel side, first wave**: `FSXF_RATE`, the progress-box fence, `OSAPI_FILE_READ_SEQ` (`5ab53f92` +408/+26), fullscreen play (`ba4c678c` +75/0), the paused-fullscreen layout (`b6684779` +16/+16), `OSAPI_WM_RESIZE` taking the gfx lock (`ea7c1bf0` +20/+20), `FERR_BIG` naming what a read needs (`e3bce881` +11/+11) | via `927c5aab` | **+530** | 0 | **+73** | 0 |
| **the Video Player, second squash**: PC-speaker PCM (+318/+11) and `GFX_BLITP`'s clip walk for Live in colour (+112/0) | `5e88d453` | **+430** | 0 | **+11** | 0 |
| **volumes past 32MB**, to FAT16's own 2GB ceiling (SPEC.md 18.7.5, 52.3.1), all behind `OS88_BIGVOL` | `dd89dfc9` | **+259** | 0 | **−3** | 0 |
| **`gfx_blit1` and 1bpp `font_run` no longer draw over a window that cuts them** | `bf6a2e8e` via `927c5aab` | **+256** | 0 | 0 | 0 |
| **associations**: a stale hint asks each disk's own `ASSOC.DAT` (+56/+7), an unknown extension is looked for (+52/0), a runtime claim marks Disk windows stale (+31/+8) | `cfd5854a` `abe5daea` via `927c5aab`, `74258a5d` | **+139** | 0 | **+15** | 0 |
| **Disk window sizes**: K past 10KB and M past 10MB (+73), with two decimals (+39), both `kern_big` only | `5e1b8203` `5c12c7a0` | **+112** | 0 | 0 | 0 |
| a paid refresh debt no longer leaves the Disk window stale | `ddaddc63` | +4 | 0 | +4 | 0 |
| `sched_init` zeroes `[ticks]` again (boot overlay only; counted once, see above) | `b6410f33` = `7e558089` | 0 | +6 | 0 | +6 |
| kernel-touching and measured at 0: a harness-matching fix | `6ac6a8ae` | 0 | 0 | 0 | 0 |
| **added** | | **+1,730** | **+6** | **+100** | **+6** |

### What it removed

| concept | commits | **`kern_big` resident** | overlay | **`kern_small` resident** | overlay |
|---|---|---:|---:|---:|---:|
| **kernel size pass 5** (docs/plans/completed/HANDOFF-KERNEL-SIZE-P6.md), derived above | via `927c5aab` | **−926** | −5 | **−377** | −6 |
| `READ_SEQ`: a failed read leaves the cursor unmoved (a fix that came out smaller) | `2effb131` | −13 | 0 | −18 | 0 |
| `gfx_blit1`'s fragment walk: the right edge exact, not floored | `b723db53` | −3 | 0 | 0 | 0 |
| **removed** | | **−942** | **−5** | **−395** | **−6** |

### The arm, whole

| | `kern_big` resident | overlay | `kern_small` resident | overlay |
|---|---:|---:|---:|---:|
| added | +1,730 | +6 | +100 | +6 |
| removed | −942 | −5 | −395 | −6 |
| **net** | **+788** | **+1** | **−295** | **0** |

**The two kernels went opposite ways, and for one reason.** Almost everything
this cycle added is `kern_big`-only by design: the Video Player's fullscreen
and speaker paths, `OS88_BIGVOL`, the Disk window's byte-exact sizes and the
window-clip fix are all `%ifdef`'d off the floor machine (SPEC.md 39.27.4's
diet), which took **100 bytes** of the 1,730. Pass 5 took 377 out of it
regardless, so `kern_small` came out smaller.

**Rungs do not add, and this cycle shows it.** The brackets' `KERN_SIZE`
moves sum to +1,536 on `kern_big` (`5e88d453` +1,024, `dd89dfc9` +512), but
the kernel moved +1,024. The two were made on different branches, each
crossed from its own parent, and together they needed two rungs, not three.

### The concepts that have not had a pass

Pass 5 reached the old line's work (its §1 is the per-topic account: video
37%, associations 36%, gfx 9% under a hot-path rule). **What landed after it
has not had one**: `5e88d453` +430, `dd89dfc9` +259, `5e1b8203` +73,
`5c12c7a0` +39, `74258a5d` +31 and `ddaddc63` +4, **836 bytes of `kern_big`
and 23 of `kern_small`**, all committed 2026-09-27 and 2026-09-28. Against
the 50% target pass 5 was briefed with, that is ~420 bytes still owed on
`kern_big`, which is less than the rung either crossing cost and more than
the cold rung has left (below).

## The on-demand modules

A module is read into a heap claim when its feature is used and freed after,
so none of this is in `KERN_SIZE`. Images (unpacked), off each point's own
build.

| module | A image | B image | Δ | claim, whole KB |
|---|---:|---:|---:|---|
| `CTRL.DRV` | 8,500 | 8,498 | −2 | 9 → 9 |
| `FORMAT.DRV` | 1,227 | 1,227 | 0 | 2 → 2 |
| `CLONE.DRV` | 7,914 | 7,914 | 0 | 8 → 8 |
| `HIBER.DRV` | 6,463 | 6,471 | +8 | 7 → 7 |
| `DOCK.DRV` | 2,550 | 2,550 | 0 | 3 → 3 |
| `EXTD.DRV` | 1,567 | 1,567 | 0 | 2 → 2 |
| **`kern_small`** `CTRL.DRV` | 4,722 | 4,722 | 0 | 5 → 5 |
| `kern_small` `FORMAT.DRV` | 1,227 | 1,227 | 0 | 2 → 2 |
| `kern_small` `CLONE.DRV` | 7,916 | 7,916 | 0 | 8 → 8 |
| `kern_small` `FILECP.DRV` | 2,294 | 2,294 | 0 | 3 → 3 |
| `kern_small` `FDLG.DRV` | 3,235 | 3,235 | 0 | 4 → 4 |

**No claim moved.** The modules were not where this cycle's work went.

## Where the bytes are, by file

`kernsize --modules`, A → B, the rows that moved (`.text` / `.cold` / `.bss`):

| file | `kern_big` | `kern_small` |
|---|---|---|
| `snd.inc` | +256 / 0 / +6 | +2 / 0 / 0 |
| `vga12.inc` | +41 / +251 / +2 | −3 / −8 / 0 |
| `diskw.inc` | 0 / +150 / −2 | 0 / −65 / −4 |
| `disk.inc` | +20 / +107 / +2 | 0 / −48 / 0 |
| `sched.inc` | +119 / 0 / +9 | +4 / 0 / 0 |
| `files.inc` | 0 / +85 / +16 | 0 / −11 / 0 |
| `fsx.inc` | +33 / 0 / 0 | −22 / 0 / 0 |
| `font.inc` | +25 / 0 / 0 | — |
| `mouse.inc` | +20 / 0 / 0 | +22 / 0 / 0 |
| `filecp.inc` | 0 / +6 / 0 | — |
| `assoc.inc` | −1 / −145 / −13 | — |
| `wm.inc` | −95 / 0 / 0 | −91 / 0 / 0 |
| `driver.inc` | 0 / −29 / 0 | 0 / −4 / 0 |
| `lz.inc` | 0 / −27 / 0 | 0 / −27 / 0 |
| `kernel.asm` | −16 / 0 / 0 | −14 / 0 / 0 |
| `menu.inc` | −14 / 0 / 0 | −14 / 0 / 0 |
| `memory.inc`, `desk.inc`, `loader.inc`, `dock.inc`, `instance.inc`, `apps.inc` | −6, −5, −2, −2, −2, −1 | −2, −5, −2, −2, —, −1 |

`assoc.inc` is the file that grew by 139 bytes of feature and still came out
145 smaller: pass 5 took 288 out of it, which that pass's record counts as the
only topic to clear 50% on its own file.

## The final bill

**What the PR asks `main` to take, on the kernel:**

| | `kern_big` | `kern_small` |
|---|---:|---:|
| `KERN_SIZE` at the #199 squash | 105,984 | 71,168 |
| `KERN_SIZE` at the PR tip (D) | **107,008** | **71,168** |
| **change** | **+1,024** (two rungs) | **0** |
| of which `main`'s own arm | **0** | **0** |
| resident bytes, net | **+788** | **−295** |
| overlay bytes, net | +1 | 0 |
| heap a machine gives up | **1,024** | **0** |
| spare of the budget | 23,552 → **22,528** (46 → 44 steps) | 36,352 → **36,352** (71 steps) |
| `.text`+`.bss` of `KERN_CODE_MAX` (65,536) | 52,908 → **53,312**: 12,628 → **12,224 left** | 39,177 → **39,055**: 26,359 → **26,481 left** |

**How close B stands to the next rung**, which is what the next addition is
billed against even though it is not what this cycle cost:

| | image rung (`.text`+`.bss`) | cold rung | low rung |
|---|---:|---:|---:|
| `kern_big` at A | 340 left | **1 left** | 290 left |
| `kern_big` at B | 448 left | **129 left** | 290 left |
| `kern_small` at B | 369 left | 212 left | 460 left |

A stood **one byte** from the cold rung's edge, so the cold rung was always
going to be the first thing any `.cold` addition crossed. B has 129.

**In one line each:**

- **`main`** spent **nothing** on either kernel. Gorillas is a 14,261-byte
  package on the games disk.
- **We** added **1,730 resident bytes on `kern_big` and 100 on `kern_small`**:
  the Video Player's kernel side in two waves (960 of it), FAT16 volumes past
  32MB, the window-clip fix for `gfx_blit1` and `font_run`, association
  search, and the Disk window's sizes. Kernel size pass 5 took **926 and 377**
  back out.
- **Together**, the PR tip is **1,024 bytes and two rungs larger than the
  #199 squash on `kern_big`** and **exactly the same size on `kern_small`,
  which is 295 bytes smaller inside its rungs**. After two squashes that could, this
  one cannot say *both kernels smaller*. Of the 836 `kern_big` bytes
  no pass has reached, the Video Player's second squash is half.

## Found on the way

- **A section-number collision, which is what the trial merge stopped on.**
  Upstream #201 published Gorillas as **SPEC.md 98**; `elendilon`'s §98 is
  the Video Player (SPEC.md 98, `apps/video/`), cited as SPEC.md 98 from 43
  files of `apps/`, `tests/`, `tools/`, `drivers/` and `kernel/` on
  `elendilon`, where #201's Gorillas cites its §98 from its own source,
  README, Makefile rule and suite rows. Each side
  cites its own §98 from code, so a textual resolution of `SPEC.md` alone
  would leave one of the two packages pointing at the other's section.
  One of the two has to be renumbered before the PR can merge, and that is
  a decision for the owner, not a merge detail. It moves no kernel byte
  either way.
- **The same fix, twice.** `b6410f33` and `7e558089` both restore
  `sched_init`'s zeroing of `[ticks]` (+6 bytes of the boot overlay each);
  the merge keeps one copy, which is why the brackets over-count the overlay
  by exactly six.
- **Every commit message that quotes a figure agrees with its bracket**:
  `dd89dfc9` (kern_big +259, kern_small −3), `b723db53` (−3), `74258a5d`
  (+31), `5e1b8203` (+73), `ddaddc63` (+4), `5c12c7a0` (16 of `.bss`, with
  +23 of `.cold` beside it), and both `[ticks]` commits (+6 of `.ovlw`).
  Pass 5's record agrees too, above.

## Appendix: every kernel-moving commit, against its own first parent

*Resident* is `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`; *overlay*
is `.ovl` + `.ovlw`. The other 37 non-merge commits in A..B measure 0 in
every section on both kernels. The resident columns sum to the headline
tables exactly; the overlay column over-counts by the duplicate +6; the
`KERN_SIZE` column does not sum, which is the point made above.

| commit | subject | big `.text` | big `.bss` | big `.cold` | big overlay | **big resident** | big `KERN_SIZE` | **small resident** | small overlay | small `KERN_SIZE` |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `927c5aab` | Elendilon since the #199 squash: the Video Player, association search, ker... | +25 | −14 | −43 | −5 | **−32** | 0 | **−297** | −6 | 0 |
| `ddaddc63` | fm_focus_x: a paid refresh debt no longer leaves the Disk window stale for... | 0 | 0 | +4 | 0 | **+4** | 0 | **+4** | 0 | 0 |
| `b6410f33` | sched_init zeroes [ticks] again; SOUND.DRV refuses ADPCM4 on a DSP 4.xx | 0 | 0 | 0 | +6 | **0** | 0 | **0** | +6 | 0 |
| `2effb131` | READ_SEQ: a failed read leaves the cursor unmoved; relold flushes on both ... | 0 | 0 | −13 | 0 | **−13** | 0 | **−18** | 0 | 0 |
| `b723db53` | gfx_blit1's fragment walk: the right edge is exact, not floored (SPEC.md 5... | 0 | 0 | −3 | 0 | **−3** | 0 | **0** | 0 | 0 |
| `74258a5d` | assoc: a runtime claim marks Disk windows stale; a mount's own window is not | 0 | 0 | +31 | 0 | **+31** | 0 | **+8** | 0 | 0 |
| `7e558089` | sched_init zeroes [ticks] again; tickzero plants junk to prove it | 0 | 0 | 0 | +6 | **0** | 0 | **0** | +6 | 0 |
| `5e88d453` | Video Player since the elendilon squash: speaker PCM, TEXT, Live in colour | +357 | +8 | +65 | 0 | **+430** | +1,024 | **+11** | 0 | 0 |
| `dd89dfc9` | Volumes past 32MB, to FAT16's own 2GB ceiling (SPEC.md 18.7.5, 52.3.1) | +2 | +10 | +247 | 0 | **+259** | +512 | **−3** | 0 | 0 |
| `5e1b8203` | Disk window sizes in K past 10KB and M past 10MB (SPEC.md 22.7.1) | 0 | 0 | +73 | 0 | **+73** | 0 | **0** | 0 | 0 |
| `5c12c7a0` | Disk window sizes with two decimals: 113.37K, 40.00M (SPEC.md 22.7.1) | 0 | +16 | +23 | 0 | **+39** | 0 | **0** | 0 | 0 |

**And the old line inside `927c5aab`**, `8f5dafe9` → `dfbe2b3b`, the nine
commits that touch a kernel input, against their own first parents:

| commit | subject | **big resident** | big `KERN_SIZE` | **small resident** |
|---|---|---:|---:|---:|
| `6ac6a8ae` | Format 6/7 review: four harnesses still matched a package by the literal 3 | **0** | 0 | **0** |
| `5ab53f92` | Video Player wave 2: FSXF_RATE, the progress-box fence, OSAPI_FILE_READ_SEQ | **+408** | +512 | **+26** |
| `ba4c678c` | Video Player wave 3: VIDEO.O88, fullscreen and silent | **+75** | 0 | **0** |
| `b6684779` | Video Player: F/Alt+Enter paused full screen, kept position, dynamic layout... | **+16** | 0 | **+16** |
| `ea7c1bf0` | OSAPI_WM_RESIZE takes the gfx lock when the caller has none | **+20** | 0 | **+20** |
| `bf6a2e8e` | kernel: gfx_blit1 and 1bpp font_run no longer draw over a window that cuts them | **+256** | 0 | **0** |
| `cfd5854a` | assoc: a stale hint asks each disk's own ASSOC.DAT | **+56** | +512 | **+7** |
| `abe5daea` | Associations: an unknown extension is looked for, not refused (SPEC.md 54.4.3) | **+52** | 0 | **0** |
| `e3bce881` | FERR_BIG says what the read needs; Tracker claims again (SPEC.md 20.14.6.3) | **+11** | 0 | **+11** |

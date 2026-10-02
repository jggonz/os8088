# Kernel bytes since the #203 squash, by concept, with `main`'s arm separated

**A measurement, not a description.** Taken 2026-10-01 and **re-taken
2026-10-02 at the tip the PR is cut from** (below), on a four-core cloud
container: `nasm` 2.16.01, Python 3.11.15. Every resident figure comes from
`tools/kernsize.py --json` and `--modules`, which RE-ASSEMBLE the kernel
rather than reading a build. Each of the **271 trees** below was measured in a
clean worktree of its own commit, with that commit's own `kernsize` and its
own generated includes (`buildnum.inc`, and `associco.inc` built the way
`make` builds it for `kern_big` and `make small` for `kern_small`). Module
images come from each commit's own build of the module. No figure is carried
between points. It is true of the commits it names and of no other tree.

It is the sixth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-07.md`,
`docs/reports/PR-CYCLE-ACCOUNTING-2026-09-11.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-17.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-25.md` and
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-28.md`, and is an edit of
none of them. It is taken to open the next `elendilon -> main` PR, so the
question is the PR's: **what does this squash cost `main`'s kernel?**

## The points

| | commit | what it is |
|---|---|---|
| **A** | `48178e44` | **the last squash with `main`**: *Elendilon -> Main (Video Player, FAT16 Volumes to 2GB, Installer Keeps Your Files, Bugfixes)* (#203), 2026-09-28 |
| **B** | `e165f9d8` | `elendilon` at its tip, 2026-10-02, **+270 commits** over A: 206 of its own that are not merges, 55 merges, and C's 9 |
| **C** | `e03a052a` | `main` at its tip, 2026-09-30, **+9 commits** over A: #204, #205, #202, #207, #208, #209, #206, #211, #210 |
| **D** | = B | what the PR carries: it is cut from `elendilon` at B. If the PR tip ends up carrying anything beyond B that reaches a kernel input, this file is re-taken at that tip before it is sent |

**A→C is everything `main` did, A→B is the branch's arm with C merged into
it, and A→D is the bill.** C is merged into B whole (`cee6c961`), so A→B
contains `main`'s arm, and this cycle that matters: **`main`'s arm is not
zero** (below), and the branch then reverts it.

**The build number contributes nothing to any delta here.** `BUILD_STR` is the
commit count (SPEC.md 14.2), and the counts are 168 at A, 177 at C, 438 at B,
and between 168 and 438 at every tree measured: three digits everywhere.

**`associco.inc` is byte-identical in all 271 trees**, on both kernels
(`676544b2...`, the same hash as the previous report's four points). It is the
one generated include a PACKAGE commit could move a kernel byte through. So
every byte below came out of the files a walk of `%include` from
`kernel/kernel.asm` finds, plus `apps/os88ui.inc` and `boot/boot2.asm`.

**`KERN_BUDGET` is 129,536 and `KERN_SMALL_BUDGET` 107,520 at every point.**
Nothing below is a budget move.

**A chains to the previous report, in rungs, exactly**: 107,008 and 71,168.
In resident bytes A is **54 heavier on `kern_big`** (`.text` +38, `.cold`
+16) and **11 on `kern_small`** (`.cold` +11) than that report's D: #203 as
merged carries six fix commits beyond the tip that report measured
(`e33338f8`'s message names them where pass 6 met them). That report said it
would not be edited again, and it was not.

## Re-taken at the PR's tip: a desktop regression instead of a pass

This file was first taken at `6044c015`, `elendilon`'s tip on 2026-10-01, with
an optimisation round expected to follow it. **That round did not happen.** A
critical regression in the new desktop (SPEC.md 26.9) was found instead, and
fixing it cost bytes where the round would have saved them. 29 commits
landed between `6044c015` and B; three move a kernel byte, and every one of
them is the desktop's:

| commit | what it fixed | `kern_big` resident | overlay | `KERN_SIZE` | `kern_small` resident |
|---|---|---:|---:|---:|---:|
| `fca72313` | *wm: a desktop zone damages the windows over IT, not the damage's box.* A drag whose damage clipped the corner of a desktop cell FOLDED the cell into the damage rect, so windows nowhere near the cell were owed a repaint (the Task Manager's graph, two rows of it) | +27 | 0 | **+512** (image rung) | +58 |
| `37507bb2` | *desk: a cell is drawn only where the damage pass reveals it* (SPEC.md 11.91.6, docs/plans/completed/DESK-CLIP-PLAN.md). A cell was drawn WHOLE wherever damage touched it, so every window lying on it, and every window above those, was repainted: 296 ms of the Task Manager's `W_PAINT` behind a drag that never touched it. A drag over the cells is 233.3 → 151.1 ms on Hercules after it | +322 | 0 | 0 | +58 |
| `bf13f736` | the boot overlay's shortcut load, 22 bytes back | 0 | −22 | 0 | 0 |
| **since `6044c015`** | | **+349** | **−22** | **+512** | **+116** |

So against the first take, `kern_big` is 98,364 → **98,713** resident and
`KERN_SIZE` 103,936 → **104,448**, and `kern_small` 65,440 → **65,556** with
its `KERN_SIZE` unmoved. The other 26 commits (the Video Player, test rows,
tools) move no kernel byte and no module image. Every figure below is B's.

## Headline: `kern_big`, the shipped default

| section | A base | C main | B elendilon = D | **C−A** | **B−A** | **B−C** |
|---|---:|---:|---:|---:|---:|---:|
| `.text` | 47,820 | 48,169 | 46,160 | **+349** | **−1,660** | −2,009 |
| `.bss` | 5,545 | 5,589 | 5,297 | **+44** | **−248** | −292 |
| `.cold` | 40,847 | 40,958 | 41,322 | **+111** | **+475** | +364 |
| `.lowbss` | 6,366 | 6,366 | 5,598 | 0 | **−768** | −768 |
| `.vgabuf` | 336 | 336 | 336 | 0 | 0 | 0 |
| **resident** | 100,914 | 101,418 | 98,713 | **+504** | **−2,201** | **−2,705** |
| `.ovl` | 1,837 | 1,837 | 2,473 | 0 | +636 | +636 |
| `.ovlw` | 5,105 | 5,105 | 4,985 | 0 | −120 | −120 |
| **overlay** | 6,942 | 6,942 | 7,458 | 0 | **+516** | +516 |
| **`KERN_SIZE`** | 107,008 | 107,008 | 104,448 | **0** | **−2,560** | **−2,560** |
| spare of `KERN_BUDGET` | 22,528 | 22,528 | 25,088 | | | |

**−2,201 resident, five rungs back net**: four image rungs and two low rungs
returned, and one cold rung spent (`.cold` is the one resident section that
grew: the stream writer and the desktop both live there). Resident is
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`; overlay is `.ovl` +
`.ovlw`, which `mem_unblob` gives back once the desktop is up.

`.boot2`, the stage-2 blob and not the kernel, is 2,249 at A and C and 2,252
at B; the blob itself grew a sector at `4bacfb22` (pass 8's timed font
pick), which is why guard 5's ceiling is 512 lower at B on both kernels.

## Headline: `kern_small`, the 128KB floor machine

| section | A base | C main | B elendilon = D | **C−A** | **B−A** |
|---|---:|---:|---:|---:|---:|
| `.text` | 35,574 | 35,574 | 34,051 | 0 | **−1,523** |
| `.bss` | 3,481 | 3,481 | 3,218 | 0 | **−263** |
| `.cold` | 25,911 | 25,911 | 25,419 | 0 | **−492** |
| `.lowbss` | 3,636 | 3,636 | 2,868 | 0 | **−768** |
| **resident** | 68,602 | 68,602 | 65,556 | **0** | **−3,046** |
| `.ovl` | 1,942 | 1,942 | 2,086 | 0 | +144 |
| `.ovlw` | 1,502 | 1,502 | 1,514 | 0 | +12 |
| **overlay** | 3,444 | 3,444 | 3,600 | 0 | **+156** |
| **`KERN_SIZE`** | 71,168 | 71,168 | 67,584 | **0** | **−3,584** |
| spare of `KERN_SMALL_BUDGET` | 36,352 | 36,352 | 39,936 | | |

**−3,046 resident, seven rungs back**: four image rungs, one cold and two
low. **Both kernels are smaller than at the last squash: `kern_small` in
every resident section it has, `kern_big` in every one but `.cold`** (and
`.vgabuf`, unchanged). The floor machine gets **3,584 bytes of heap** back.

## The attribution is exact

Every commit in A..B was measured against its first parent on both kernels.
A merge M gets its own figure too, its RESIDUAL: what M moved against its
first parent, less what the commits it brought in moved against theirs. With
A on B's first-parent chain, the commits' figures and the merges' residuals
sum to B−A by construction, so the check that matters is the residuals:

**All 55 merges have a residual of 0 bytes in every section on both
kernels.** Five of them move `KERN_SIZE` by a rung and the bytes by nothing:
rungs do not add (below). No merge resolved a conflict into a kernel byte, and
no change was made twice and kept once, which the previous cycle had. 87 of the
215 non-merge commits move a byte; the other 128 measure 0 in every section on
both kernels.

## `main`'s arm: +504 resident on `kern_big` and a retained 4KB module, and the PR takes both back out

| commit | what it touched | `kern_big` resident | `kern_small` |
|---|---|---:|---:|
| #211 `40a017f3` *Add desktop shortcuts with no static kernel memory growth* | `kernel/links.inc` (new), `linkcfg.inc`, `ui.inc`, `menu.inc`, `mod.inc`, `files.inc`, `desk.inc`, `kernel.asm`, `wm.inc`, `memory.inc`, and a new module, `DESKTOP.DRV` | **+504** (`.text` +349, `.bss` +44, `.cold` +111), **plus `DESKTOP.DRV`'s 4,038 bytes held for the whole session** (below) | 0 |
| #204, #205, #202, #207, #206 | packages: DrMarco, Gorillas' music, 1942, DrMarco standalone, Excitebike | 0 | 0 |
| #208, #209, #210 | the everything-disk set, the native-game-port skill (twice) | 0 | 0 |

**#211's title is true of rungs and not of bytes**: `KERN_SIZE` did not
move, but it spent 504 resident bytes, leaving C's image rung 2 bytes from
its edge and the cold rung 2 bytes from its own. **And `KERN_SIZE` is not
the whole of what #211 keeps resident.** Its behaviour is a module,
`DESKTOP.DRV` (4,038-byte image, 3,192 on disk, a 4KB claim), and the module
is never freed: `ui_task` loads it before its first pass, on every `kern_big`
boot whether or not the desktop has a shortcut, and keeps it (C's SPEC.md 2.8:
*"`DESKTOP.DRV` (§26.8) is retained when the UI starts on big/emu builds"*,
claimed bottom-up). A module that is never dropped costs RAM like the kernel
and is counted with it here, as the on-demand rule (CLAUDE.md) and
docs/plans/completed/O88-MULTISEG-PLAN.md §1 count one. On top of that, the
shortcut record store (`DL_MAX` 16 records of 256 bytes plus a 512-byte
header) is a **5KB claim**, taken lazily at the first shortcut or the first
`DESKTOP.CFG` and then held. On `kern_small` #211 costs 0, as it said.

**The branch reverts #211** (`e9da2dcd`, −504 to the byte) and puts its own
desktop in its place (SPEC.md 26.8 and 26.9, below), so that one tree does not
carry two desktop designs. **That is a decision about upstream's merged
feature, and the PR should say so in its description** rather than leave it
to be found in the diff: the PR removes `kernel/links.inc`,
`kernel/linkcfg.inc` and `DESKTOP.DRV`, and `main`'s desktop and desktopcga
suite rows with them. Side by side, on `kern_big`:

| | `main`'s #211 | `elendilon`'s shortcuts + one desktop |
|---|---:|---:|
| resident kernel sections | +504 | **+1,309** (+960 as first built, +349 for the regression fix) |
| a module held from the UI's start to power-off | **`DESKTOP.DRV`, 4,038 (a 4KB claim)** | **none** |
| **always resident, total** | **4,542 (4,600 with the claim's rounding)** | **1,309** |
| the shortcut store, claimed lazily | **5KB** at the first shortcut or `DESKTOP.CFG`, 16 records | **1KB per 8 shortcuts** on the desktop (128-byte rows, `SC_PERKB` 8), grown as they are added and declared movable |
| what a gesture reads off the system disk | nothing: the module is already in | `CTRL.DRV`'s settings core, 2,134 bytes, freed again after the gesture |
| boot overlay (given back after boot) | 0 | +302 |
| `CTRL.DRV` image (on demand) | 0 | +1,613 |
| `kern_small` resident | 0 | +158 |

**So replacing #211 takes about 3.2KB of always-resident RAM back off
`main`'s `kern_big`** (4,542 → 1,309 by image, 4,600 → 1,309 by claim), and
the shortcut store goes from 5KB on the first shortcut to 1KB for every eight.
Its kernel sections alone read the other way (+805), and that figure is not the
cost: it counts the resident dispatch #211 kept in the kernel and leaves out
the behaviour it kept in a module that is never freed. It also buys
SPEC.md 26.9.8's list: every desktop item (drive, Wire, shortcut) in one grid
that the user rearranges, and a package-facing `OSAPI_DESK_ITEM`, and since
the fix a cell is redrawn only where a window reveals it. What it costs is
`CTRL.DRV` +1,613 on demand, +302 of boot overlay, +158 on `kern_small`, and a 2,134-byte read off the system disk per gesture where
#211 held its module.

`main`'s packages are billed to floppies, never to resident RAM: `DRMARCO.O88`
25,550 bytes on disk (49,685 image), `1942.O88` 31,448 (46,962), and
`GORILLAS.O88` 14,261 → 16,206 with its music.

## Our arm, by concept

Per concept, the sum of its commits' brackets. Every row is exact; the
appendix has each commit.

### What it added

| concept | commits | **`kern_big` resident** | overlay | **`kern_small` resident** | overlay |
|---|---|---:|---:|---:|---:|
| **the stream writer as it came in** (SPEC.md 18.4.7–18.4.9): stage 1 (+81/+81), `OSAPI_FILE_WRITE_SEQ` plain or held (+941/+6), the append as one body (−245), the split-set join as a held stream (+198), the copy and FTPD's STOR on it (+50), a copy's room check that stops counting (+57), a paste that re-lists less (−5/−5); a hard-disk boot's cylinder run added and dropped again (`c005afbb` +235, `8ed32fa2` −235) | `e884972f` `160f38a2` `ebbf8c60` `8826d0fb` `614fad9b` `2988cf81` `f1d650e3` `c005afbb` `8ed32fa2` | **+1,077** | 0 | **+82** | 0 |
| **shortcuts and one desktop** (SPEC.md 26.8, 26.9): shortcuts (+1,107: `7158c446` +1,002, the badge +76, the toast +29), one grid of cells (−167/+25), `OSAPI_DESK_ITEM` and the optimisation branch's ideas (−129/−15), every slot on every adapter (+8), a selection that lets go (+38/+32), `CTRL.DRV` in part (+103); **then the regression fix** (above): a zone damages only the windows over it (+27/+58), a cell drawn only where it is revealed (+322/+58), the overlay's shortcut load (−22 of overlay) | `7158c446` `25cb048d` `4218a031` `a70551a9` `30b1f700` `54e7372d` `8d4381fc` `c9928210` `fca72313` `37507bb2` `bf13f736` | **+1,309** | +302 | **+158** | +22 |
| **split sets and File > Uncompress To** (SPEC.md 20.17), almost all of it in `CLONE.DRV` | `f6280f2c` | **+124** | 0 | 0 | 0 |
| **a failing disk looks alive**, Write Img's two messages | `1774795f` `85514c4a` `2a205da3` | **+40** | 0 | **+41** | 0 |
| **the Control Panel's Floppy page** (SPEC.md 31.14), most of it in `CTRL.DRV` and the boot overlay | `52e77031` | **+17** | +127 | 0 | 0 |
| **the DOS box on `READ_SEQ` and held `WRITE_SEQ`** (SPEC.md 96.53) | `d112890e` | **+13** | 0 | 0 | 0 |
| among the kernel-touching commits that measure 0: Format lays the tracks down (all of it in `FORMAT.DRV`), the clone's failure report, the join's error path, every chunked reader on `READ_SEQ`, STACK-SLOTS-PLAN 13 | `a294a88` `f57ef3a` `fd3bd19` `ce37bef` `ed611e2` | 0 | 0 | 0 | 0 |
| **added** | | **+2,580** | **+429** | **+281** | **+22** |

### What it removed

| concept | commits | **`kern_big` resident** | overlay | **`kern_small` resident** | overlay |
|---|---|---:|---:|---:|---:|
| **kernel size pass 8** (docs/plans/completed/HANDOFF-KERNEL-SIZE-P9.md), 68 commits on six agents' branches, the timed font pick inside it | merged at `c459871f` | **−3,339** | +90 | **−2,812** | +135 |
| **kernel size pass 6** (docs/plans/completed/HANDOFF-KERNEL-SIZE-P7.md), squashed with the HDD page | `e33338f8` | **−875** | −3 | **−450** | −1 |
| **kernel size pass 7, the stream writer's own** (docs/reports/STREAM-WRITER-SIZE-2026-09-30.md): 1,077 → 485, then stage 1 back (+16) and `WSEQF_SYS` (+8) | `5f318df1` `74a01d17` `b0d68559` `c62df14a` | **−566** | 0 | **−65** | 0 |
| **`main`'s #211, reverted** (above) | `e9da2dcd` | **−504** | 0 | 0 | 0 |
| the Timer runs without a task (SPEC.md 14.7) | `7e490cbc` | −1 | 0 | 0 | 0 |
| **removed** | | **−5,285** | **+87** | **−3,327** | **+134** |

### The arm, whole

| | `kern_big` resident | overlay | `kern_small` resident | overlay |
|---|---:|---:|---:|---:|
| added | +2,580 | +429 | +281 | +22 |
| removed | −5,285 | +87 | −3,327 | +134 |
| **our net** | **−2,705** | **+516** | **−3,046** | **+156** |
| `main`'s #211 | +504 | 0 | 0 | 0 |
| **B − A** | **−2,201** | **+516** | **−3,046** | **+156** |

**Three size passes took 4,780 resident bytes out of `kern_big` and 3,327 out
of `kern_small`; this cycle's features put 2,580 and 281 back.** Every pass's
own record is confirmed to the byte:

- **Pass 6** quotes `kern_big` 100,914 → 100,039 (−875) and `kern_small`
  68,602 → 68,152 (−450), `KERN_SIZE` 107,008 → 105,984 and 71,168 → 70,656.
  `e33338f8` measures exactly that. It is a re-squash (*"Everything elendilon
  did after the commit #203 was squashed from (bba1369c...)"*), and the line
  it was cut from is not on the remote, so the HDD page inside it could not
  be bracketed separately; the agreement with the pass's record says it moved
  no kernel byte.
- **Pass 7** (the stream writer's report) quotes `kern_big` +1,077 before and
  **+511** after against `4164ecd`. Measured: +1,077 and +511. On
  `kern_small` it quotes **+22**; this measures **+17**, because the paste's
  narrower re-list (`f1d650e3`, −5 on both) rode in with the stream writer
  and the report's figure leaves it out.
- **Pass 8** quotes `kern_big` 100,723 → 97,384 (−3,339) and `kern_small`
  68,210 → 65,398 (−2,812) against `4d104041`. The merge measures −3,339 and
  −2,812. Its module figures agree too (below).

**Rungs do not add, and this cycle shows it again.** The commits' own
`KERN_SIZE` moves sum to −2,048 on `kern_big` and −3,584 on `kern_small`, and
five merges move a rung with no byte of their own; the kernel moved −2,560
and −3,584.

### What has not had a pass

Pass 8 was cut at `4d104041` and merged at `c459871f`. **What landed after
that merge has not had a pass**, measured `c459871f` → B: on `kern_big`
**+1,322 resident** (the desktop +1,309 with its fix, the DOS box +13) and on
`kern_small` **+158** (the desktop), plus **+302 / +22 of overlay** and
**+1,613 bytes of `CTRL.DRV`** (the shortcuts +1,522, one desktop +103, the
core −12), all committed 2026-10-01 and 2026-10-02. Against the 50% target the
passes have been briefed with, that is **~661 bytes owed on `kern_big`**, ~79
on `kern_small`, and ~807 of `CTRL.DRV`. It is the next cycle's first pass:
the round meant to take it went on the regression instead (above).

**`main`'s #211 is not on that list** because B does not carry it. If the PR
were to keep both designs, its +504 and `DESKTOP.DRV`'s retained 4KB would
be.

## How close B stands to the next rung

This is what the next addition is billed against, not what this cycle cost:

| | image rung (`.text`+`.bss`) | cold rung | low rung |
|---|---:|---:|---:|
| `kern_big` at A | 395 left | 113 left | 290 left |
| `kern_big` at C | **2 left** | **2 left** | 290 left |
| `kern_big` at `6044c015` (the first take) | **14 left** | 228 left | **34 left** |
| `kern_big` at B | 255 left | 150 left | **34 left** |
| `kern_small` at A | 369 left | 201 left | 460 left |
| `kern_small` at B | 107 left | 181 left | 204 left |

**The regression fix crossed the image rung the first take stood 14 bytes
from** (`fca72313`), so B's image rung is half spent (257/512). **The low
rung is the near one now, 478/512 spent**: the next 35 bytes of `.lowbss` on
`kern_big` cost a whole rung. Per CLAUDE.md's banner this decides WHEN the
machine pays and never what a change cost.

## The on-demand modules

A module is read into a heap claim when its feature is used and freed after,
so none of this is in `KERN_SIZE`. Images (unpacked) and files (as shipped),
off each point's own build. Nothing after `6044c015` moved a module image.
C's images are A's to the byte, and C has one
more, #211's `DESKTOP.DRV` (4,038-byte image, 3,192 on disk), which is held
from the UI's start (above) and which B does not have.

| module | A image | B image | Δ | A file | B file | claim, whole KB |
|---|---:|---:|---:|---:|---:|---|
| `CTRL.DRV` | 8,498 | 10,394 | **+1,896** | 7,008 | **10,394** | 9 → **11** |
| `FORMAT.DRV` | 1,227 | 1,304 | +77 | 1,116 | 1,222 | 2 → 2 |
| `CLONE.DRV` | 7,914 | 10,073 | **+2,159** | 6,606 | 8,529 | 8 → **10** |
| `HIBER.DRV` | 6,477 | 6,662 | +185 | 4,927 | 5,102 | 7 → 7 |
| `DOCK.DRV` | 2,550 | 2,515 | −35 | 2,270 | 2,232 | 3 → 3 |
| `EXTD.DRV` | 1,567 | 1,566 | −1 | 1,377 | 1,378 | 2 → 2 |
| **`kern_small`** `CTRL.DRV` | 4,722 | 4,441 | −281 | 3,898 | 4,071 | 5 → 5 |
| `kern_small` `FORMAT.DRV` | 1,227 | 1,304 | +77 | 1,116 | 1,222 | 2 → 2 |
| `kern_small` `CLONE.DRV` | 7,916 | 9,204 | +1,288 | 6,612 | 7,791 | 8 → 9 |
| `kern_small` `FILECP.DRV` | 2,294 | 2,269 | −25 | 2,090 | 2,061 | 3 → 3 |
| `kern_small` `FDLG.DRV` | 3,235 | 3,214 | −21 | 2,948 | 2,922 | 4 → 4 |

**This cycle's growth went into the modules**, which is where the
on-demand rule (CLAUDE.md) sends it. Along B's first-parent chain, each step
measured at its own build:

| step | `kern_big` | `kern_small` |
|---|---|---|
| pass 6 (`e33338f8`) | CTRL −9, FORMAT −30, CLONE −28, HIBER −3, DOCK −3, EXTD −1 | CTRL −8, FORMAT −30, CLONE −28, FILECP −20, FDLG −6 |
| the Floppy page | CTRL **+615** | |
| split sets and Uncompress To, its error path, the V20/test-combo round | CLONE **+2,671** | CLONE +1,645, FDLG −1 |
| Format lays the tracks down | FORMAT +302 | FORMAT +302 |
| the stream writer and its readers | CLONE +169 | CLONE +2 |
| **pass 8** | CTRL −323, FORMAT −195, CLONE −653, HIBER +188, DOCK −32 | CTRL −275, FORMAT −195, CLONE −331, FILECP −5, FDLG −14 |
| shortcuts, then one desktop, then its drop fixes | CTRL **+1,625** (+1,522, +87, +16) | |
| `CTRL.DRV` in part | CTRL −12 | CTRL +2 |

Pass 8's row is its record's own (`CTRL.DRV` −323, `CLONE.DRV` −653,
`FORMAT.DRV` −195, `HIBER.DRV` +188, `DOCK.DRV` −32), to the byte.

**`CTRL.DRV` ships UNCOMPRESSED on `kern_big` now** (`c9928210`: a packed
stream's prefix decodes to nothing, so the settings core could not be read by
itself). That is **+1,061 bytes of system disk** against the packed file the
step before it (9,333), and 3,386 against A's. Its commit measures what that
buys on a 4.77 MHz XT: a shortcut drag-out's module load 1.85-1.99 s ->
1.12-1.16 s.

## Where the bytes are, by file

`kernsize --modules`, A → B, the rows that moved (`.text` / `.cold` / `.bss` /
`.lowbss`):

| file | `kern_big` | `kern_small` |
|---|---|---|
| `snd.inc` | −504 / 0 / −256 / 0 | −449 / 0 / −263 / 0 |
| `font.inc` | +54 / 0 / +3 / **−768** | +11 / 0 / +3 / **−768** |
| `wm.inc` | −552 / 0 / −8 / 0 | −504 / 0 / −8 / 0 |
| `driver.inc` | −167 / −55 / −46 / 0 | −27 / 0 / 0 / 0 |
| `vga12.inc` | −197 / −56 / 0 / 0 | −71 / −23 / 0 / 0 |
| `files.inc` | +19 / −211 / −4 / 0 | −7 / −215 / 0 / 0 |
| `disk.inc` | +2 / −176 / −6 / 0 | 0 / −133 / −6 / 0 |
| `ctrl.inc` | −175 / −1 / 0 / 0 | −140 / −1 / 0 / 0 |
| `mouse.inc` | −120 / 0 / 0 / 0 | −60 / 0 / 0 / 0 |
| `sched.inc` | −77 / 0 / −4 / 0 | −60 / 0 / −4 / 0 |
| `memory.inc` | 0 / −66 / 0 / 0 | 0 / −17 / 0 / 0 |
| `kernel.asm` | −30 / 0 / 0 / 0 | −6 / 0 / 0 / 0 |
| `fprog.inc`, `fsx.inc`, `ui.inc` | −34, −33, −22 | −34, −33, −41 |
| `assoc.inc`, `fdlg.inc`, `vidsel.inc`, `softgfx.inc` | −24, −21, −16, −13 | —, −2, −4, −9 |
| `viddet.inc`, `instance.inc`, `clock.inc`, `loader.inc`, `clip.inc`, `blank.inc` | −9, −7, −5, −4, −3, −2 | −11, −8, −5, −5, −3, — |
| `xmem.inc` | — | −2 / 0 / 0 / 0 |
| `hiber.inc`, `menu.inc`, `apps.inc`, `filecp.inc` | +2, +4, +5, +7 | —, +2, —, −1 |
| `mod.inc` | +4 / +38 / 0 / 0 | 0 / −3 / 0 / 0 |
| `diskw.inc` | −58 / **+271** / +20 / 0 | −62 / −124 / 0 / 0 |
| `icons.inc` | **+186** / 0 / +4 / 0 | −1 / 0 / 0 / 0 |
| `desksc.inc` (new: the shortcuts) | +88 / **+305** / +4 / 0 | — |
| `desk.inc` | +2 / **+473** / +38 / 0 | 0 / +33 / +15 / 0 |

The four files that grew are the cycle's two features: `diskw.inc` is the
stream writer (`kern_big` only, beyond its API cell), and `desk.inc`,
`desksc.inc` and `icons.inc` the desktop (`icons.inc`'s +190 is the fix's
`ico_clip`, an icon drawn one clip fragment at a time). `.lowbss` −768 on both is pass 8's glyph table
read in the ROM rather than copied (SPEC.md 6), the largest single item.

## The final bill

**What the PR asks `main` to take, on the kernel**, with D = B:

| | `kern_big` | `kern_small` |
|---|---:|---:|
| `KERN_SIZE` at the #203 squash (A) | 107,008 | 71,168 |
| `KERN_SIZE` at `main`'s tip (C) | 107,008 | 71,168 |
| `KERN_SIZE` at the PR tip (D) | **104,448** | **67,584** |
| **change, A → D** | **−2,560** (five rungs net) | **−3,584** (seven rungs) |
| resident bytes, A → D | **−2,201** | **−3,046** |
| resident bytes, C → D (against `main` as it stands, #211 included) | **−2,705**, and **−6,743** counting `DESKTOP.DRV`'s retained 4,038 | **−3,046** |
| overlay bytes, A → D | +516 | +156 |
| heap a machine gets back, against A | **2,560** | **3,584** |
| heap a machine gets back, against `main` as it stands (C) | **6,656**: `KERN_SIZE` 2,560 and `DESKTOP.DRV`'s 4KB claim | **3,584** |
| spare of the budget | 22,528 → **25,088** (44 → 49 steps) | 36,352 → **39,936** (71 → 78 steps) |
| `.text`+`.bss` of `KERN_CODE_MAX` (65,536) | 53,365 → **51,457**: 12,171 → **14,079 left** | 39,055 → **37,269**: 26,481 → **28,267 left** |
| guard 5 (boots on `MIN_RAM_KB`) | 84,992 → **87,040** before it cannot boot on 196KB | 51,200 → **54,272** on 128KB |

**In one line each:**

- **`main`** spent **504 resident bytes on `kern_big` plus a 4KB module it
  never frees** (#211's desktop shortcuts, ~4.5KB always resident) and a 5KB
  lazy store; nothing on `kern_small`; the rest of its arm is packages.
  **The PR reverts #211** and replaces it with the branch's own desktop:
  1,309 bytes always resident with its regression fix and 1KB per 8
  shortcuts, about **3.2KB less** than #211 on every `kern_big` machine.
- **We** added **2,580 resident bytes on `kern_big` and 281 on
  `kern_small`**: the stream writer (1,077 as it came in), shortcuts and one
  desktop (1,309, of which 349 is the regression fix), split sets (124), a failing disk that looks alive (40), the
  Floppy page (17) and the DOS box on the stream calls (13). **Three size
  passes took 4,780 and 3,327 back out**, every one confirmed against its own
  record.
- **Together**, the PR tip is **2,560 bytes and five rungs smaller than the
  #203 squash on `kern_big`** and **3,584 bytes and seven rungs smaller on
  `kern_small`**. Both kernels smaller, again. What grew is the modules:
  `CTRL.DRV` +1,896 and `CLONE.DRV` +2,159, on demand and on disk.

## Found on the way

- **`docs/KERNEL-MEMORY.md`'s blessed baseline is still pass 8's close, and B
  is past it**: every `make` at B prints *"the image rung CROSSED: 100 -> 101"*
  and *"the cold rung CROSSED: 79 -> 81"* on `kern_big` (sum +1,631 with the
  overlay) and +180 on `kern_small`. Nothing is wrong with the kernel; the
  desktop's commits did not re-bless. **Closed before the PR was cut**: the
  commit after this file's re-blesses both kernels at B, and `kernsize` then
  reads +0 against the tree on both.
- **`kernsize --modules` still cannot describe `desksc.inc`**: `e7efcc90`
  gave it a THEME, so the theme table counts its 393 bytes with the desktop
  now, but the module table still reads `**(undescribed)**`. **Closed in the
  same commit as the bless**: its row in docs/KERNEL-MEMORY.md is described. `extmod.inc`, `mouproto.inc` and
  `dockmod.inc`, which an earlier report named, are described now.
- **The stream writer's report says `kern_small` +22 where the tree measures
  +17** (above): the difference is the paste's −5, which came in on the same
  branch.
- **No `make -j2 small` race this time**: every point built `all` and
  `small` first time, where the 2026-09-25 report met one on
  `build/smallk/artful.o88`.

## Appendix: every kernel-moving commit, against its own first parent

*Resident* is `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`; *overlay*
is `.ovl` + `.ovlw`. The other 128 non-merge commits in A..B measure 0 in
every section on both kernels, and every merge's residual is 0 bytes (five
move only a rung: `ff6e803c`, `08bb3201`, `fb2f1b1b`, `c459871f`,
`cee6c961`). The resident and overlay columns sum to the headline tables
exactly; the `KERN_SIZE` columns do not, which is the point made above.

| commit | subject | big `.text` | big `.bss` | big `.cold` | big `.lowbss` | big overlay | **big resident** | big `KERN_SIZE` | **small resident** | small overlay | small `KERN_SIZE` |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `e33338f8` | Elendilon since the #203 squash: kernel size pass 6, the HDD page | −386 | −4 | −485 | 0 | −3 | **−875** | −1,024 | **−450** | −1 | −512 |
| `52e77031` | Control Panel: a Floppy page that overrides the drive detection (SPEC.md 31.14... | +15 | +2 | 0 | 0 | +127 | **+17** | 0 | **0** | 0 | 0 |
| `f6280f2c` | File > Uncompress To...: join a split set onto another volume, asking for each... | +26 | 0 | +98 | 0 | 0 | **+124** | +512 | **0** | 0 | 0 |
| `1774795f` | A failing disk looks alive: the pointer comes back up, and a retry arms the bu... | +14 | 0 | +25 | 0 | 0 | **+39** | 0 | **+39** | 0 | 0 |
| `85514c4a` | Write Img said 'Not a disk image' for every image: the dialog lost the size | 0 | 0 | −1 | 0 | 0 | **−1** | 0 | **0** | 0 | 0 |
| `2a205da3` | Write Img without the system disk says so, not 'No disk' | +2 | 0 | 0 | 0 | 0 | **+2** | 0 | **+2** | 0 | 0 |
| `e884972f` | Stream writer, stage 1: an append writes its FAT once when one sector is the w... | 0 | 0 | +81 | 0 | 0 | **+81** | 0 | **+81** | 0 | 0 |
| `160f38a2` | Stream writer, stages 2 and 3: OSAPI_FILE_WRITE_SEQ, plain or HELD | +27 | +39 | +875 | 0 | 0 | **+941** | +1,024 | **+6** | 0 | 0 |
| `ebbf8c60` | WRITE_SEQ is the append: one body, and a held call's failure loses only itself | 0 | −1 | −244 | 0 | 0 | **−245** | 0 | **0** | 0 | 0 |
| `8826d0fb` | The split-set join writes as one HELD stream, and its FAT stays banked across ... | +4 | 0 | +194 | 0 | 0 | **+198** | 0 | **0** | 0 | 0 |
| `614fad9b` | The file manager's copy and FTPD's STOR write through WRITE_SEQ, and the A/B | 0 | +16 | +34 | 0 | 0 | **+50** | 0 | **0** | 0 | 0 |
| `c005afbb` | A hard-disk boot earns the floppy cylinder run too (18.93.4, since withdrawn) | +2 | 0 | +233 | 0 | +5 | **+235** | +512 | **0** | 0 | 0 |
| `f1d650e3` | A paste re-lists only the Disk windows it could have changed (SPEC.md 22.3.1) | 0 | 0 | −5 | 0 | 0 | **−5** | 0 | **−5** | 0 | 0 |
| `2988cf81` | A copy's room check stops counting once the file fits (SPEC.md 22.5.2.1) | +2 | 0 | +55 | 0 | 0 | **+57** | 0 | **0** | 0 | 0 |
| `8ed32fa2` | Drop the hard-disk boot's floppy cylinder-run probe (was 18.93.4) | −2 | 0 | −233 | 0 | −5 | **−235** | 0 | **0** | 0 | 0 |
| `5f318df1` | Stream writer size pass: 1,077 -> 485 resident bytes on kern_big (WIP, docs to... | −2 | −33 | −557 | 0 | 0 | **−592** | −1,024 | **−81** | 0 | 0 |
| `74a01d17` | Stream writer size pass: 1,077 -> 487 resident bytes on kern_big | 0 | 0 | +2 | 0 | 0 | **+2** | 0 | **0** | 0 | 0 |
| `b0d68559` | Stage 1 back, in 16 bytes: the stream writer at +503 resident on kern_big | 0 | 0 | +16 | 0 | 0 | **+16** | 0 | **+16** | 0 | 0 |
| `c62df14a` | Five more writers on the stream writer, and WSEQF_SYS for system files | 0 | 0 | +8 | 0 | 0 | **+8** | 0 | **0** | 0 | 0 |
| `7e490cbc` | Timer: no task - keep time and draw from its window timer (SPEC.md 14.7) | −6 | 0 | +5 | 0 | 0 | **−1** | 0 | **0** | 0 | 0 |
| `56e5cda8` | The speaker door as a THIN door: -108 bytes of kern_big .text | −108 | 0 | 0 | 0 | 0 | **−108** | 0 | **0** | 0 | 0 |
| `6a567a63` | The PWM clip as a library on a thin door: -493 bytes on BOTH kernels | −239 | −254 | 0 | 0 | −26 | **−493** | −512 | **−493** | −26 | 0 |
| `ab9e2943` | memory.inc and three kern_small refusals: -45 kern_big, -12 kern_small | 0 | 0 | −45 | 0 | 0 | **−45** | 0 | **−12** | 0 | 0 |
| `8823a6cb` | ui_lcall, [ui_post] by inc, and two kernel.asm stubs: -56 big, -44 small | −55 | 0 | −1 | 0 | 0 | **−56** | 0 | **−44** | 0 | −512 |
| `161daa54` | The death panel's evidence on the stack, and a row for it: -13 both | −9 | −5 | 0 | 0 | 0 | **−14** | 0 | **−13** | 0 | 0 |
| `d641c05a` | kern_small plays no PWM clip: -173 kern_small, kern_big unchanged | 0 | 0 | 0 | 0 | 0 | **0** | 0 | **−173** | 0 | 0 |
| `a169506b` | Three jumps that land on a ret or on the next instruction: -3 big, -7 small | −3 | 0 | 0 | 0 | 0 | **−3** | 0 | **−7** | 0 | 0 |
| `1e9deb19` | Size pass 8 (split): the image opens its own Save boxes; Uncompress To's compl... | −9 | 0 | −116 | 0 | 0 | **−125** | 0 | **−57** | 0 | 0 |
| `eb502fa5` | Size pass 8 (split): jcc reach in the Disk window's two key ladders, and four ... | 0 | 0 | −40 | 0 | 0 | **−40** | 0 | **−33** | 0 | −512 |
| `ff6017ec` | Size pass 8 (split): CLONE.DRV's shared epilogue and geometry copies; fm_vp_se... | 0 | 0 | −79 | 0 | 0 | **−79** | 0 | **−79** | 0 | 0 |
| `ef836220` | Size pass 8 (split): two [ui_post] stores in files.inc | 0 | 0 | −3 | 0 | 0 | **−3** | 0 | **−3** | 0 | 0 |
| `550396da` | Relaxed jccs in my files: -6 resident on both kernels, -3 HIBER.DRV | −3 | 0 | −3 | 0 | 0 | **−6** | 0 | **−6** | 0 | 0 |
| `824dce9e` | Control Panel: the item table and its list names move into CTRL.DRV | −119 | 0 | 0 | 0 | −19 | **−119** | 0 | **−75** | 0 | 0 |
| `d69c0b05` | drv_cp_name moves into CTRL.DRV; cp_repost; a driver-row name gate | 0 | −12 | −50 | 0 | 0 | **−62** | 0 | **0** | 0 | 0 |
| `f5096298` | DRVC_POINT loses its drv_svc slot, behind a CF gate (LAST-DROP-BYTES 7.7.8) | 0 | −36 | +7 | 0 | 0 | **−29** | 0 | **0** | 0 | 0 |
| `8acd027c` | More of what only CTRL.DRV reads leaves the kernel | −237 | 0 | 0 | 0 | −4 | **−237** | −512 | **−91** | −4 | 0 |
| `aa2fef5e` | CTRL.DRV back under half its growth: shared shapes, computed tables | −2 | 0 | 0 | 0 | 0 | **−2** | 0 | **−2** | 0 | 0 |
| `b5d082f1` | Delete three dead bodies: cp_ipap, dkx_px_rect, dkx_hole_winb | −1 | 0 | 0 | 0 | 0 | **−1** | 0 | **−1** | 0 | 0 |
| `04e4cc2e` | Sites the speaker agent found in the floppy files, verified by hand | −3 | 0 | −6 | 0 | −2 | **−9** | 0 | **−4** | 0 | 0 |
| `bf03bc4b` | Relaxed jccs made short, and a folded clock carry | −5 | 0 | −4 | 0 | 0 | **−9** | 0 | **−5** | 0 | 0 |
| `91cb2d4b` | gfx_unlock: one AX save, zero-register stores, one-word release; cur_busy_on r... | −29 | 0 | 0 | 0 | 0 | **−29** | 0 | **−14** | 0 | 0 |
| `ec929d91` | mouse.inc: one show/hide body, one serial ISR door, a shared EOI tail | −85 | 0 | 0 | 0 | 0 | **−85** | 0 | **−32** | 0 | 0 |
| `58c2f5f2` | vga12/viddet/splash/mouse: shared pen tail, word stores, one splash door | −57 | 0 | 0 | 0 | +8 | **−57** | 0 | **−22** | +8 | 0 |
| `978e9efb` | gfx: one bank-copy loop, shared epilogues, a cmc; gfx_unlock keeps its [dws_ho... | −26 | 0 | 0 | 0 | 0 | **−26** | 0 | **−30** | 0 | 0 |
| `42f03752` | vga12: the VGA fill arms the GC inline; vga_set_color and vga_set_xor go | −16 | 0 | 0 | 0 | 0 | **−16** | 0 | **0** | 0 | 0 |
| `d9bac068` | vga12/splash: two dead bodies - gfx_bitclr and splf_fill | −8 | 0 | 0 | 0 | 0 | **−8** | 0 | **−8** | 0 | 0 |
| `4f1f35e6` | mouse: the probe reaches the resident writers through spw_near, not five shims | −20 | 0 | 0 | 0 | −15 | **−20** | 0 | **−8** | +1 | 0 |
| `afb129cc` | cur_busy_on: bank the shape at the door; the worn test leaves .swap | −5 | 0 | 0 | 0 | 0 | **−5** | 0 | **−5** | 0 | 0 |
| `61c3858d` | vga12/softgfx/vidsel: three flag pairs, one word store each | −12 | 0 | 0 | 0 | −4 | **−12** | 0 | **0** | 0 | 0 |
| `df0a292a` | mou_hotplug: the terminal state is the first compare | −6 | 0 | 0 | 0 | 0 | **−6** | 0 | **−6** | 0 | 0 |
| `afa281c7` | vidsel/softgfx: two epilogues shared where the callers are rare | −8 | 0 | 0 | 0 | 0 | **−8** | 0 | **−3** | 0 | 0 |
| `1a9971df` | gfx/mouse: jumps onto the next instruction, relaxed jccs, and three inc | −32 | 0 | 0 | 0 | 0 | **−32** | 0 | **−40** | 0 | 0 |
| `10bad72d` | FORMAT.DRV 1,499 -> 1,311, and the format table leaves the resident kernel | −62 | 0 | 0 | 0 | 0 | **−62** | 0 | **−62** | 0 | 0 |
| `5e0942f3` | A failing disk arms the chrome at every failed attempt: 7 bytes of .cold | 0 | 0 | −7 | 0 | 0 | **−7** | 0 | **−7** | 0 | 0 |
| `20fdac1e` | disk.inc/diskw.inc: three shared shapes, -40 .cold on kern_big | 0 | 0 | −40 | 0 | 0 | **−40** | 0 | **−23** | 0 | 0 |
| `55a681ed` | fprog.inc: CURBAR_ON once as a routine, and two shared epilogues: -37 .text | −37 | 0 | 0 | 0 | 0 | **−37** | 0 | **−37** | 0 | 0 |
| `d3d6959b` | dsk_spc, dsk_fatlba, dsk_nfats and dsk_fatsz ARE the staged BPB: -24 .cold, -6... | 0 | −6 | −24 | 0 | 0 | **−30** | 0 | **−30** | 0 | 0 |
| `211fcc5c` | dskw_isempty and dskw_rt_scan are one walk: -37 .cold on both kernels | 0 | 0 | −37 | 0 | 0 | **−37** | 0 | **−37** | 0 | −512 |
| `37af0077` | diskw.inc: a redirected transfer's four arguments, loaded once: -9 .cold | 0 | 0 | −9 | 0 | 0 | **−9** | 0 | **0** | 0 | 0 |
| `8d83f0bb` | fdlg.inc: the row count is [disk_nfiles], read in place: -13 | 0 | 0 | −13 | 0 | 0 | **−13** | 0 | **0** | 0 | 0 |
| `cbd4cd0e` | dskw_now packs the FAT date and time by Horner's rule: -10 .cold | 0 | 0 | −10 | 0 | 0 | **−10** | 0 | **−10** | 0 | 0 |
| `77947e67` | disk/diskw/fdlg: relaxed jccs and shared refusal tails, -15 on kern_big, -14 o... | 0 | 0 | −15 | 0 | 0 | **−15** | 0 | **−14** | 0 | 0 |
| `9ca25385` | wm.inc: one copier for the kernel's rects, the title bar's boxes drawn by one ... | −298 | 0 | 0 | 0 | 0 | **−298** | 0 | **−296** | 0 | 0 |
| `c01f969a` | wm.inc: the clip fragment in registers, one overlap test, the resize union thr... | −206 | −8 | 0 | 0 | 0 | **−214** | −512 | **−211** | 0 | −512 |
| `158452be` | font.inc: the glyph table is read in the ROM, not copied - 768 resident bytes ... | +55 | +3 | 0 | −768 | −28 | **−710** | −1,024 | **−743** | −28 | −512 |
| `c4e58f6d` | wm.inc: wm_hit's range tests as one unsigned compare each; clip.inc drops two ... | −22 | 0 | 0 | 0 | 0 | **−22** | 0 | **−22** | 0 | −512 |
| `53fd7e32` | font.inc: fnt_rn_edge's VGA column takes the one-xchg latch idiom | −3 | 0 | 0 | 0 | 0 | **−3** | 0 | **0** | 0 | 0 |
| `c02bbf3e` | wm.inc: three banked rects live on the stack, not in .text | −41 | 0 | 0 | 0 | 0 | **−41** | 0 | **−41** | 0 | 0 |
| `7fa3cc8b` | wm/font/clip: register flag stores, inc [ui_post], one reachable jc short | −13 | 0 | 0 | 0 | 0 | **−13** | 0 | **−10** | 0 | 0 |
| `1cdc7e0e` | Pass 8 close: three cross-owner leftovers | 0 | 0 | −2 | 0 | −3 | **−2** | 0 | **0** | 0 | 0 |
| `5ded3e24` | vga12: the VGA latch pairs are one xchg; cur_shape_pass through ui_lcall | −32 | 0 | 0 | 0 | 0 | **−32** | 0 | **−3** | 0 | 0 |
| `41e71469` | font: the planar 8x8 table when it is the BIOS's glyphs, a heap copy when it i... | 0 | 0 | 0 | 0 | +72 | **0** | 0 | **0** | +72 | 0 |
| `4bacfb22` | font: the 8x8 table picked by the PIT - the blob grows a sector to hold it | 0 | 0 | 0 | 0 | +111 | **0** | 0 | **0** | +112 | 0 |
| `d112890e` | DOS box on READ_SEQ and HELD WRITE_SEQ, both hosts (SPEC.md 96.53) | 0 | 0 | +13 | 0 | 0 | **+13** | 0 | **0** | 0 | 0 |
| `40a017f3` | **main:** Add desktop shortcuts with no static kernel memory growth (#211) | +349 | +44 | +111 | 0 | 0 | **+504** | 0 | **0** | 0 | 0 |
| `e9da2dcd` | Revert main's desktop shortcuts (#211), ahead of unified-desktop | −349 | −44 | −111 | 0 | 0 | **−504** | −512 | **0** | 0 | 0 |
| `7158c446` | Desktop shortcuts (SPEC.md 26.8) | +68 | +10 | +924 | 0 | +293 | **+1,002** | +512 | **0** | 0 | 0 |
| `25cb048d` | Desktop shortcuts: the badge beside the picture; SCBIG removed | +42 | 0 | +34 | 0 | 0 | **+76** | 0 | **0** | 0 | 0 |
| `4218a031` | Desktop shortcuts: one toast for a missing target, naming its drive | +24 | 0 | +5 | 0 | 0 | **+29** | 0 | **0** | 0 | 0 |
| `a70551a9` | One desktop: every item a zone in one grid of cells (SPEC.md 26.9) | +2 | +35 | −204 | 0 | +27 | **−167** | 0 | **+25** | +22 | 0 |
| `30b1f700` | desktop: OSAPI_DESK_ITEM for packages, and the optimisation branch's ideas | −18 | −7 | −104 | 0 | +4 | **−129** | 0 | **−15** | 0 | 0 |
| `54e7372d` | desktop: every visible slot on every adapter; the CGA link picture fixed | 0 | 0 | +8 | 0 | 0 | **+8** | 0 | **0** | 0 | 0 |
| `8d4381fc` | desktop: a selected item lets go when a window takes the focus | +9 | 0 | +29 | 0 | 0 | **+38** | 0 | **+32** | 0 | 0 |
| `c9928210` | CTRL.DRV loads in part: a desktop gesture reads only the settings core | +6 | +9 | +88 | 0 | 0 | **+103** | 0 | **0** | 0 | 0 |
| `bf13f736` | boot overlay: ovl_sc_load's exits ahead of its entry, 22 bytes back | 0 | 0 | 0 | 0 | −22 | **0** | 0 | **0** | 0 | 0 |
| `fca72313` | wm: a desktop zone damages the windows over IT, not the damage's box | +34 | 0 | −7 | 0 | 0 | **+27** | +512 | **+58** | 0 | 0 |
| `37507bb2` | desk: a cell is drawn only where the damage pass reveals it (SPEC.md 11.91.6) | +233 | +4 | +85 | 0 | 0 | **+322** | 0 | **+58** | 0 | 0 |

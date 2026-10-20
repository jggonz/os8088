# Kernel bytes since the #197 squash, by concept, with `main`'s arm separated

**A measurement, not a description.** Taken 2026-09-25 on a four-core cloud
container from a cold checkout: `nasm` 2.16.01, Python 3.11.15. Every figure
comes from `tools/kernsize.py --json` and `--modules`, which RE-ASSEMBLE the
kernel rather than reading a build. Each point was measured in a clean
worktree of its own commit with that commit's own `kernsize`, and no figure is
carried between points. It is true of the commits it names and of no other
tree.

It is the fourth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-07.md`,
`docs/reports/PR-CYCLE-ACCOUNTING-2026-09-11.md` and
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-17.md`, and is an edit of none
of them. It is taken to prepare the next `elendilon -> main` PR, so the
question is the PR's: **what does this squash cost `main`'s kernel?**

## The points

| | commit | what it is |
|---|---|---|
| **A** | `86f92ad8` | **the last squash with `main`**: *Elendilon -> Main (DOS: .COM and .EXE ... Both Kernels Smaller than at the last Squash)* (#197), 2026-09-21. `git merge-base origin/main 736ff428` |
| **B** | `736ff428` | `elendilon` at its tip before `main` was merged in, **+168 commits** over A |
| **C** | `8605dafd` | `main` at its tip, **+2 commits** over A: #198 and #183 |
| **D** | `fde3d898` | the merge of B and C, which is what the PR carries |

**A→B is the branch's own arm, A→C is everything `main` did, and A→D is the
bill.** B and C are siblings with A as their only common point. **The arms
share no commit**: `git merge-base origin/main 736ff428` is A itself, so,
unlike the 2026-09-17 report, there is no overlap to discount.

A is not that report's last point. Its G (`72fdd5c2`, 2026-09-20) had
`kern_big` at 110,080 and `kern_small` at 74,240, while the #197 squash is
**110,592** and 74,240. The squash carried one `kern_big` rung more than G
did, and A is where this file starts.

**The build number contributes nothing to any delta here.** `BUILD_STR` is the
commit count (SPEC.md 14.2), and the four counts are 163, 331, 165 and 334:
three digits everywhere. **D is 666 commits from the fourth digit.**

**`associco.inc` is byte-identical at all four points**
(`c4b3e546...`), and at every one of the 64 intermediate points measured
below. It is the only generated input that could move a kernel byte without a
kernel source file changing. `buildnum.inc` is the other, and it is covered by
the paragraph above. So every byte below came out of `kernel/` or
`apps/os88ui.inc`: a walk of the `%include` graph from `kernel/kernel.asm` at A,
B and D finds 58 kernel files, that one SDK file, and the three generated
includes, and nothing else.

**`KERN_BUDGET` is 129,536 and `KERN_SMALL_BUDGET` 107,520 at all four
points.** Nothing below is a budget move; every figure is a size move.

## Headline: `kern_big`, the shipped default

| section | A base | B elendilon | C main | D merged | **B−A** | **C−A** | **D−A** |
|---|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 50,163 | 47,333 | 50,163 | 47,333 | **−2,830** | **0** | **−2,830** |
| `.bss` | 6,113 | 5,525 | 6,113 | 5,525 | **−588** | **0** | **−588** |
| `.cold` | 40,962 | 40,429 | 40,962 | 40,429 | **−533** | **0** | **−533** |
| `.ovl` | 1,511 | 1,837 | 1,511 | 1,837 | +326 | 0 | +326 |
| `.ovlw` | 5,050 | 5,104 | 5,050 | 5,104 | +54 | 0 | +54 |
| `.lowbss` | 6,366 | 6,366 | 6,366 | 6,366 | 0 | 0 | 0 |
| `.vgabuf` | 848 | 336 | 848 | 336 | **−512** | 0 | **−512** |
| **sum** | | | | | **−4,083** | **0** | **−4,083** |
| **`KERN_SIZE`** | 110,592 | 105,984 | 110,592 | 105,984 | **−4,608** | **0** | **−4,608** |
| spare of `KERN_BUDGET` | 18,944 | 23,552 | 18,944 | 23,552 | | | |

**Nine rungs off, and every section that is resident went DOWN.** Of the
−4,083, **−4,463 is resident** (`.text`, `.bss`, `.cold`, `.lowbss`, `.vgabuf`)
and **+380 is overlay**, which is boot-time code in memory the machine reuses
once it is up. The overlay grew because batch 13 of the size pass (below)
moved boot-only code OUT of the resident sections and into the blob and
`.ovl`, which is where it should live.

**The `.vgabuf` line is worth 512 bytes to a VGA machine and nothing to a
monochrome one.** A mono machine already declines that rung
(`mem_floor_ax` seeds its heap floor under it, docs/KERNEL-MEMORY.md), so
batch 8 halving it gives VGA its 512 back and leaves Hercules and CGA where
they were. **The heap a machine gains from this squash is 4,608 bytes on VGA
and 4,096 on a 1bpp adapter.**

`.boot2`, the stage-2 blob and not the kernel, moves 2,250 → 2,249
(`splash.inc` −1).

## Headline: `kern_small`, the 128KB floor machine

| section | A base | B elendilon | C main | D merged | **B−A** | **C−A** | **D−A** |
|---|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 37,332 | 35,698 | 37,332 | 35,698 | **−1,634** | **0** | **−1,634** |
| `.bss` | 4,189 | 3,485 | 4,189 | 3,485 | **−704** | **0** | **−704** |
| `.cold` | 26,339 | 26,073 | 26,339 | 26,073 | **−266** | **0** | **−266** |
| `.ovl` | 1,867 | 1,942 | 1,867 | 1,942 | +75 | 0 | +75 |
| `.ovlw` | 1,342 | 1,502 | 1,342 | 1,502 | +160 | 0 | +160 |
| `.lowbss` | 3,636 | 3,636 | 3,636 | 3,636 | 0 | 0 | 0 |
| **sum** | | | | | **−2,369** | **0** | **−2,369** |
| **`KERN_SIZE`** | 74,240 | 71,168 | 74,240 | 71,168 | **−3,072** | **0** | **−3,072** |
| spare of `KERN_SMALL_BUDGET` | 33,280 | 36,352 | 33,280 | 36,352 | | | |

**Six rungs off the floor machine**: **−2,604 resident**, +235 overlay. That
is 3,072 more bytes of heap on a 128KB machine, which has no VGA and so no
`.vgabuf` question.

## `main`'s arm: zero, measured

**`main`'s two commits cost the kernel NOTHING, on both kernels, in every
section and in every module row of `kernsize --modules`.** C is A to the byte,
and D is B to the byte, so the merge added nothing either.

| commit | what it touched | kernel bytes |
|---|---|---:|
| #198 `901e22b6` *NASM 3.02: the size hint on a FORWARD reference has to go on the destination* | `tests/dosxms/xmsq.asm` only | **0** |
| #183 `8605dafd` *PIXELSTEIN 3D: a Wolfenstein 3D clone in 8086 assembly (SPEC.md 97)* | its own package, and two COMMENTS in `apps/os88api.inc` (the `OSAPI_GFX_BLIT1` refusal note and the fsx flags note) | **0** |

PIXELSTEIN's cost is a package and not the kernel: `PXSTEIN.O88` is 50,874
bytes, on `games360.img` and not on `apps360.img` (SPEC.md 97.9). It is
billed to the disks, not to any machine's resident RAM.

The two previous reports found `main`'s arm crossing no `kern_small` rung
(256 bytes of section on 2026-09-07, 12 on 2026-09-17). This is the first
cycle where it is exactly zero, and on `kern_big` too.

## Our arm, by concept

**The attribution is exact, not apportioned.** Every non-merge commit in A..B
that touches a kernel input, which is 45 of the 168, was measured against its
own first parent on both kernels. That is 64 distinct points, each in a clean
worktree. **The 45 brackets sum to the A→B total in every section on both
kernels with a residual of ZERO**, so the arm's 38 merges (the size pass,
Tracker, Word, dotdel, and 27 of them `kernel-size-p4` syncing itself)
contributed no byte of their own. The concept table below is therefore a decomposition,
not an estimate. The per-commit table is the appendix.

**Rungs do not add, and the table shows it.** Summed over the brackets,
`kern_small`'s `KERN_SIZE` moves −2,560, but it actually moved −3,072. The
bytes add up; the rungs depend on the order the bytes arrived in. That is the
banner in CLAUDE.md, measured.

### What it removed

| concept | commits | **`kern_big` resident** | overlay | **`kern_small` resident** | overlay |
|---|---|---:|---:|---:|---:|
| **kernel size pass 4**, batches 1–14 and 16–18 (plus three commits of docs and a fix, each 0) | 20 | **−4,177** | +305 | **−2,967** | +165 |
| **the extended desktop becomes `EXTD.DRV`**, batches 15 and 19 (SPEC.md 39.19.6) | 2 | **−1,076** | +5 | **−2** | 0 |
| the `Disk bufs` row retired (SPEC.md 20.9) | 1 | −4 | 0 | −4 | 0 |
| **removed** | 23 | **−5,257** | **+310** | **−2,973** | **+165** |

**The size pass, by batch**, `kern_big` resident, largest first: batch 9, the
window record absorbs its side tables, **−600**; batch 8, the VGA decode
table shares the mono pair tables, −505 (all of it `.vgabuf`, so VGA only);
batch 4, the API table's rare cells go to six bytes, −402; batch 10, cold-side
trampolines, −388; batch 13, boot-only code into the blob, −334 resident and
+315 overlay; batch 1, three `.bss` sizes larger than their use, −288;
batch 3, dead routines, −274; batch 17, near-duplicate blocks share one body,
−242; batch 11, a failed load says two things, −216; batch 14, a missing
program is a toast and `ui_note` goes, −197; batch 2, `font_run`'s table in
the icon scratch cell, −196; batch 12, `rect_get`/`rect_put` at the cold rect
sites, −142; batch 6, a shared `.cold` push prologue, −135; batch 7, only the
BPB fields the mount reads, −125; batch 16, fifteen inline API cells, −89;
batch 18, `MOD_NENT` gone, −36; batch 5, dead far shims, −8.

**The size pass's commit messages were audited against its brackets, and
all but one figure holds.** Every batch's message quotes its own before and
after, and each was re-measured here against its own parent. All nineteen
agree to the byte on `kern_big`. Batch 1 itemises `kern_small`'s three cuts as
−160, −192 and −72, and the bracket is their sum, −424; batch 3's five items
sum to its −274. **The one disagreement is batch 9 on `kern_small`**: its
message says `.text` −535 and the bracket measures **−557**, 22 bytes more than
it claimed, with `.bss` +24 as quoted.

**`EXTD.DRV` is a trade, and it is the right one for this machine
population.** 1,076 resident bytes leave `kern_big` on every machine for good.
In their place a 1,567-byte image, a 2 KB claim, is loaded only on a machine
whose desktop is extended, and dropped on Single. So a two-monitor XT pays
about 1 KB MORE heap while extended, and every other machine gains 1,076
bytes. `kern_small` has no extended desktop and moves −2 (a `push di`).

### What it added

| concept | commits | **`kern_big` resident** | overlay | **`kern_small` resident** | overlay |
|---|---|---:|---:|---:|---:|
| Compress / Uncompress of any size, streamed, and a kernel truncate in 106 bytes (SPEC.md 20.15) | `a5a7368d` `392aa13f` `6af5dc5a` | **+200** | 0 | **+94** | 0 |
| the drive icons: a 5.25" drive drawn as a 5.25" diskette, the 3.5" turned to match (SPEC.md 26.4.1), and that concept's own −57 pass | `21e7d604` `f844d36c` | **+143** | +70 | **+144** | +70 |
| `gfx_blitp`'s plane-major path (SPEC.md 5.4.3.5), 131 → 110 by its own pass | `08abfd71` `53065855` | **+110** | 0 | 0 | 0 |
| a document's program: an empty drive asked once, the boot volume first, fast volumes before floppies (SPEC.md 54.4) | `152b1e89` `d554a875` | **+95** | 0 | 0 | 0 |
| a program may TAKE the mouse's IRQ and the kernel takes it back (SPEC.md 9.13) | `dd27ccb0` | **+94** | 0 | 0 | 0 |
| `gfx_blit1`: a fast path for a band that needs no bookkeeping (SPEC.md 5.4.2.6) | `2e7320f9` | **+87** | 0 | **+85** | 0 |
| `ASSOC.DAT` inside one track, and no read-ahead fill across a 64KB page | `5f73d613` | +24 | 0 | +24 | 0 |
| a save-under is never a wall, and a heap plan assumes the park | `01c21840` | +17 | 0 | +6 | 0 |
| a stopped floppy motor counts as warm, so the widget and busy pointer go up before the spin-up | `8f681659` | +16 | 0 | +16 | 0 |
| a far-entered driver dispatch needs a far tail (`drv_svc_call_x`) | `f6a961c5` | +4 | 0 | 0 | 0 |
| the sound layer's kernel side: a ring played in place, `OSAPI_SND_FM` reaching its channel (SPEC.md 34.5.2, 34.2.2) | `b5489d07` `2cc9a5f3` `5e347bc4` `65732a4f` | +4 | 0 | 0 | 0 |
| kernel-touching and measured at 0: Tracker's image buttons in `os88ui.inc`, `ldcost`, `DRVDIAG=1` (a knob, off in every shipped kernel) | `668517d8` `62756f46` `8db815b4` | 0 | 0 | 0 | 0 |
| **added** | 22 | **+794** | **+70** | **+369** | **+70** |

**Five concepts measure ZERO on `kern_small`**: the plane-major blit, the
document's-program search, the mouse IRQ, the far tail and the sound side.
The truncate is a sixth half-way: `6af5dc5a` is +106 on `kern_big` and 0 on
small. So the floor machine takes 369 of the 794 bytes.

**Two rows are what the banner in CLAUDE.md is about, and they are the
same size in opposite directions.** `21e7d604`, the 5.25" diskette, is +200
resident bytes, and on `kern_big` it crossed **two** rungs, `KERN_SIZE`
+1,024. `82a127a1`, the `Disk bufs` retirement, is −4 bytes, and it UNcrossed
one, `KERN_SIZE` −512. Neither figure is what the change cost. The bytes are.

### The arm, whole

| | `kern_big` resident | overlay | `kern_small` resident | overlay |
|---|---:|---:|---:|---:|
| removed | −5,257 | +310 | −2,973 | +165 |
| added | +794 | +70 | +369 | +70 |
| **net** | **−4,463** | **+380** | **−2,604** | **+235** |

**For every byte this cycle added to the resident kernel, it took 6.6 out**
on `kern_big` and 8.1 on `kern_small`.

## The on-demand modules: not resident, and moving the other way

A module is read into a heap claim when its feature is used and freed after,
so none of this is in `KERN_SIZE`. It is where some of the resident bytes
went, and it is billed to a machine only while that feature is open. These
are images (unpacked), measured off each point's own build; the file on the
floppy is LZ4-packed and smaller.

| module | A image | B image | Δ | claim, whole KB |
|---|---:|---:|---:|---|
| `CTRL.DRV` | 8,408 | 8,500 | +92 | 9 → 9 |
| `FORMAT.DRV` | 1,233 | 1,227 | −6 | 2 → 2 |
| `CLONE.DRV` | 5,817 | 7,880 | **+2,063** | **6 → 8** |
| `HIBER.DRV` | 6,463 | 6,463 | 0 | 7 → 7 |
| `DOCK.DRV` | 2,560 | 2,550 | −10 | 3 → 3 |
| `EXTD.DRV` | — | 1,567 | **new** | **— → 2** |
| **`kern_small`** `CTRL.DRV` | 4,726 | 4,722 | −4 | 5 → 5 |
| `kern_small` `FORMAT.DRV` | 1,233 | 1,227 | −6 | 2 → 2 |
| `kern_small` `CLONE.DRV` | 5,817 | 7,880 | **+2,063** | **6 → 8** |
| `kern_small` `FILECP.DRV` | 2,300 | 2,294 | −6 | 3 → 3 |
| `kern_small` `FDLG.DRV` | 3,232 | 3,235 | +3 | 4 → 4 |

**`CLONE.DRV` +2,063 is the streamed compressor** (SPEC.md 20.15.3: *the
compressor with both verbs rides in `CLONE.DRV`*), paid only while a
Compress, Uncompress or clone is running. That is the design working: a
feature this size would have been two KB of every machine's RAM for ever as
resident code, and it is two KB of heap for the length of one operation.

## The final bill

**What the PR asks `main` to take, on the kernel:**

| | `kern_big` | `kern_small` |
|---|---:|---:|
| `KERN_SIZE` at the #197 squash | 110,592 | 74,240 |
| `KERN_SIZE` at the PR tip (D) | **105,984** | **71,168** |
| **change** | **−4,608** (nine rungs) | **−3,072** (six rungs) |
| of which `main`'s own arm | **0** | **0** |
| resident bytes, net | **−4,463** | **−2,604** |
| overlay bytes, net | +380 | +235 |
| heap a machine gains | **4,608 on VGA, 4,096 on 1bpp** | **3,072** |
| spare of the budget | 18,944 → **23,552** (37 → 46 steps) | 33,280 → **36,352** (65 → 71 steps) |
| `.text`+`.bss` of `KERN_CODE_MAX` (65,536) | 56,276 → **52,858**: 9,260 → **12,678 left** | 41,521 → **39,183**: 24,015 → **26,353 left** |

**`KERN_CODE_MAX` is the number that matters most, and it moved the right way
for once.** It is the one limit that cannot be raised, because offsets are 16
bits. The 2026-09-17 report's lane 4 took three rungs off `KERN_SIZE` while
moving it 169 bytes the WRONG way, because the win there was `.lowbss`. This
cycle's win is `.text` and `.bss` themselves: **3,418 bytes more room in the
64KB window on `kern_big`**, and 2,338 on `kern_small`.

**In one line each:**

- **`main`** spent **nothing** on either kernel. PIXELSTEIN is a 50,874-byte
  package on the games disk, and #198 is a test fixture.
- **We** added **794 resident bytes on `kern_big` and 369 on `kern_small`**
  for eleven concepts: streamed Compress, the drive icons, two blitter fast
  paths, a smarter document's-program search, the mouse IRQ hand-back, and
  five small fixes. We took out **5,257 and 2,973** with kernel size pass 4
  and `EXTD.DRV`.
- **Together**, the PR tip is **4,608 bytes and nine rungs smaller than the
  #197 squash on `kern_big`**, and **3,072 bytes and six rungs smaller on
  `kern_small`**. #197 itself was titled *Both Kernels Smaller than at the
  last Squash*, so this is the second squash running that can say so.

## Which of this cycle's additions have had a pass of their own

Two of the eleven have: the drive icons (`f844d36c`, −57, inside the same
concept) and `gfx_blitp` (`53065855`, 131 → 110). **The other nine have not,
and together they are 541 bytes of `kern_big` and 225 of `kern_small`**, all
committed between 2026-09-22 and 2026-09-25:

| concept | `kern_big` | `kern_small` | committed | status |
|---|---:|---:|---|---|
| Compress, streamed, and the truncate | 200 | 94 | 09-24 | too new |
| the document's-program search | 95 | 0 | 09-24/25 | too new |
| the mouse IRQ hand-back | 94 | 0 | 09-22 | **before size pass 4, and not recorded as reached by it** |
| `gfx_blit1`'s fast path | 87 | 85 | 09-24 | too new |
| `ASSOC.DAT` in one track | 24 | 24 | 09-25 | too new |
| the save-under | 17 | 6 | 09-23 | **before size pass 4, and not recorded as reached by it** |
| the motor counts as warm | 16 | 16 | 09-25 | too new |
| the far tail | 4 | 0 | 09-24 | too new |
| the sound side | 4 | 0 | 09-23/24 | too small to owe a pass |

The pass was organised by TECHNIQUE across the whole kernel (shared bodies,
cell shapes, `.bss` sizes), not by concept, so a concept that predates it is
not recorded as passed merely because a batch may have touched its file.

This file does not re-check the previous report's outstanding rows (the mouse
wire work, lane 3's six, `gfx_points`) against size pass 4. Whether the
pass's technique sweeps reached them needs a per-concept bracket that was not
taken here.

## Two things found on the way

- **`make -j2 small` races.** In fresh worktrees of both A and B it failed with
  `os88pkg: error: file is 0 bytes; header alone is 32` on
  `build/smallk/artful.o88`: the packer read `artful.bin` while nasm still
  had it at 0 bytes. A second `make -j2 small` in the same tree then
  succeeds. It is not this cycle's, since A has it too. The failing rule is a
  package disk's; the module images quoted above are files that run did write,
  and B's are the same after the clean re-run.
- **`kernsize --modules` has three files it cannot describe**: `extmod.inc`
  (new this cycle, the `EXTD.DRV` resident half), `mouproto.inc` and
  `dockmod.inc` (both already undescribed at A). Each reads
  `**(undescribed)**` in the module table that docs/KERNEL-MEMORY.md carries.

## Appendix: every kernel-touching commit, against its own first parent

*Resident* is `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`; *overlay* is
`.ovl` + `.ovlw`. The columns sum to the headline tables exactly. The
`KERN_SIZE` columns do not, which is the point made above.

| commit | subject | big `.text` | big `.bss` | big `.cold` | big `.vgabuf` | big overlay | **big resident** | big `KERN_SIZE` | **small resident** | small overlay | small `KERN_SIZE` |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `82a127a1` | The `Disk bufs` row is empty, so retire it - term and all (SPEC.md 20.9) | 0 | 0 | −4 | 0 | 0 | **−4** | −512 | **−4** | 0 | 0 |
| `21e7d604` | desk: a 5.25" drive is drawn as a 5.25" diskette (SPEC.md 26.4.1) | +112 | +1 | +87 | 0 | +70 | **+200** | +1,024 | **+201** | +70 | 0 |
| `f844d36c` | icons: turn the 3.5" diskette to match the 5.25", and a size pass (-57) | −28 | −1 | −28 | 0 | 0 | **−57** | 0 | **−57** | 0 | 0 |
| `dd27ccb0` | kernel: a program may TAKE the mouse's IRQ, and the kernel takes it back (S... | +94 | 0 | 0 | 0 | 0 | **+94** | 0 | **0** | 0 | 0 |
| `668517d8` | tracker: ModPlug Player's windowed face, a playlist, and image buttons | 0 | 0 | 0 | 0 | 0 | **0** | 0 | **0** | 0 | 0 |
| `01c21840` | kernel: a save-under is never a wall, and a plan assumes the park | +13 | +1 | +3 | 0 | 0 | **+17** | 0 | **+6** | 0 | 0 |
| `b5489d07` | tracker: opening is adding, the card's own position, a half in pieces | +4 | 0 | 0 | 0 | 0 | **+4** | 0 | **0** | 0 | 0 |
| `2cc9a5f3` | sound, tracker: the card plays a ring where it lies (SPEC.md 34.5.2) | 0 | 0 | 0 | 0 | 0 | **0** | 0 | **0** | 0 | 0 |
| `5e347bc4` | Tracker: visualiser Off, SPACE pause, 22 kHz on 286+, spectrum peak markers | 0 | 0 | 0 | 0 | 0 | **0** | 0 | **0** | 0 | 0 |
| `a2c530b7` | kernel size pass 4, batch 1: three .bss sizes that were larger than their use | 0 | −288 | 0 | 0 | 0 | **−288** | −512 | **−424** | 0 | −512 |
| `386c3720` | kernel size pass 4, batch 2: font_run's glyph table shares the icon rendere... | 0 | −196 | 0 | 0 | 0 | **−196** | 0 | **−180** | 0 | 0 |
| `912309ab` | kernel size pass 4, batch 3: dead routines and two small folds | −16 | 0 | −258 | 0 | 0 | **−274** | −512 | **−100** | 0 | 0 |
| `e7842e3e` | kernel size pass 4, batch 4: the API table's rare cells shrink to six bytes | −292 | 0 | −110 | 0 | 0 | **−402** | −512 | **−402** | 0 | −512 |
| `951fd317` | kernel size pass 4, batch 5: dead far shims, and kern_small's kern_big-only... | −8 | 0 | 0 | 0 | 0 | **−8** | 0 | **−48** | 0 | 0 |
| `15032b40` | kernel size pass 4, batch 6: a shared push prologue for .cold routines | 0 | 0 | −135 | 0 | 0 | **−135** | 0 | **−42** | 0 | 0 |
| `e73869c7` | kernel size pass 4, batch 7: stage and bank only the BPB fields the mount r... | 0 | −126 | +1 | 0 | 0 | **−125** | 0 | **−69** | 0 | 0 |
| `6cfdfe98` | kernel size pass 4: SPEC.md 20.3 and the tree describe the two-size API table | 0 | 0 | 0 | 0 | 0 | **0** | 0 | **0** | 0 | 0 |
| `c3cd465c` | kernel size pass 4, batch 8: the VGA decode table shares the mono pair tables | +7 | 0 | 0 | −512 | 0 | **−505** | −512 | **0** | 0 | 0 |
| `418df0fd` | kernel size pass 4, batch 9: the window record absorbs its side tables; one... | −648 | +48 | 0 | 0 | −10 | **−600** | −512 | **−533** | −10 | −512 |
| `e0e6a28d` | kernel size pass 4: docs follow batch 9's window record, and the boot overl... | 0 | 0 | 0 | 0 | 0 | **0** | 0 | **0** | 0 | 0 |
| `7e50a94f` | kernel size pass 4: FIX - three modules far-called the API table at pre-bat... | 0 | 0 | 0 | 0 | 0 | **0** | 0 | **0** | 0 | 0 |
| `0fc1c465` | kernel size pass 4, batch 10: cold-side trampolines, and kernel windows cal... | −207 | 0 | −181 | 0 | 0 | **−388** | −1,024 | **−206** | 0 | −1,024 |
| `bc83ebc4` | kernel size pass 4, batch 11: a failed load says two things, and the Task M... | −190 | −26 | 0 | 0 | 0 | **−216** | 0 | **−182** | 0 | 0 |
| `4e804a33` | kernel size pass 4, batch 12: rect_get/rect_put at the 19 measured-cold rec... | −142 | 0 | 0 | 0 | 0 | **−142** | 0 | **−99** | 0 | 0 |
| `26b437fb` | kernel size pass 4, batch 13: boot-only code moves into the stage-2 blob, a... | −273 | 0 | −61 | 0 | +315 | **−334** | −512 | **−248** | +175 | −512 |
| `a3b63c92` | kernel size pass 4, batch 14: a document's missing program is a toast, "Nee... | −141 | 0 | −56 | 0 | 0 | **−197** | 0 | **−113** | 0 | 0 |
| `123650a9` | kernel size pass 4, batch 15: the extended desktop is an on-demand module,... | −914 | +28 | +35 | 0 | +5 | **−851** | −1,024 | **0** | 0 | 0 |
| `a5a7368d` | Compress and Uncompress take files of any size the heap can hold | 0 | 0 | +94 | 0 | 0 | **+94** | 0 | **+94** | 0 | +512 |
| `392aa13f` | Compress streams files too big to hold twice; the pointer tracks and the ba... | 0 | 0 | 0 | 0 | 0 | **0** | 0 | **0** | 0 | 0 |
| `77006c82` | kernel size pass 4, batch 16: fifteen API cells ARE their routine (the inli... | −89 | 0 | 0 | 0 | 0 | **−89** | 0 | **−82** | 0 | 0 |
| `6af5dc5a` | A kernel truncate in 106 bytes, and streamed Compress in one pass | 0 | 0 | +106 | 0 | 0 | **+106** | 0 | **0** | 0 | 0 |
| `33aa6529` | kernel size pass 4, batch 17: near-duplicate blocks in the file layer and t... | −23 | −2 | −217 | 0 | 0 | **−242** | 0 | **−201** | 0 | 0 |
| `85331622` | kernel size pass 4, batch 18: MOD_NENT is gone - each module's slot block i... | +14 | −52 | +2 | 0 | 0 | **−36** | 0 | **−38** | 0 | 0 |
| `7053d5bc` | kernel size pass 4, batch 19: EXTD wave 2 - the second display's user-rate... | −213 | +24 | −36 | 0 | 0 | **−225** | 0 | **−2** | 0 | 0 |
| `62756f46` | ldcost: arm A's bar is the package's own read-ahead fills, read off the guest | 0 | 0 | 0 | 0 | 0 | **0** | 0 | **0** | 0 | 0 |
| `2e7320f9` | gfx_blit1: a fast path for a band that needs no bookkeeping (SPEC.md 5.4.2.6) | 0 | 0 | +87 | 0 | 0 | **+87** | 0 | **+85** | 0 | 0 |
| `f6a961c5` | Kernel: a far-entered driver dispatch needs a far tail (drv_svc_call_x) | 0 | 0 | +4 | 0 | 0 | **+4** | 0 | **0** | 0 | 0 |
| `65732a4f` | OSAPI_SND_FM: a patch-load reaches the channel it names (SPEC.md 34.2.2) | 0 | 0 | 0 | 0 | 0 | **0** | 0 | **0** | 0 | 0 |
| `8db815b4` | DRVDIAG=1: a loading-screen line that says where a stopped driver load is | 0 | 0 | 0 | 0 | 0 | **0** | 0 | **0** | 0 | 0 |
| `152b1e89` | A document's program: an empty drive is asked once, the boot volume first,... | 0 | +1 | +64 | 0 | 0 | **+65** | 0 | **0** | 0 | 0 |
| `5f73d613` | ASSOC.DAT inside one track as early as it fits, and no read-ahead fill acro... | 0 | 0 | +24 | 0 | 0 | **+24** | 0 | **+24** | 0 | 0 |
| `8f681659` | A stopped floppy motor counts as warm: the widget and busy pointer go up be... | 0 | 0 | +16 | 0 | 0 | **+16** | 0 | **+16** | 0 | 0 |
| `d554a875` | Document's program: fast volumes first, then the floppies with B: before A:... | 0 | 0 | +30 | 0 | 0 | **+30** | 0 | **0** | 0 | 0 |
| `08abfd71` | dotdel: a wall in an actor's band gets a plane of its own (SPEC.md 93.5.19) | +131 | 0 | 0 | 0 | 0 | **+131** | 0 | **0** | 0 | 0 |
| `53065855` | gfx_blitp: the plane-major path in 110 bytes, not 131 (SPEC.md 5.4.3.5) | −21 | 0 | 0 | 0 | 0 | **−21** | 0 | **0** | 0 | 0 |

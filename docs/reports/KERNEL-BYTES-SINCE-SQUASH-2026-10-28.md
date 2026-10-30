# Kernel bytes since the #313 squash, by concept

**A measurement, not a description.** Taken 2026-10-28 on a four-core cloud
container: `nasm` 2.16.01, Python 3.13. Every resident figure comes from
`tools/kernsize.py --json` (and `--modules` for the per-file table), which
RE-ASSEMBLES the kernel rather than reading a build. Every commit of the
round, every parent it names and every merge base, was measured in a clean
tree of its own: `git archive` of that commit's `kernel/`, `boot/`, `tools/`,
`drivers/`, Makefile and the top-level files of `apps/`, `apps/stiklib.asm`
assembled there into the `stiklib.bin` that `deskmod.inc` includes, and that
commit's own `kernsize` measuring **`kern_big`, `kern_small`, `kern_emu` and
`kern_mega`**. A tree whose kernel inputs hash the same as one already
measured takes that tree's figures: **402 points, 226 distinct kernel
trees**. `associco.inc` is B's in every tree. Module and driver images come
from `make`, `make small` and `make mega` at the two endpoints only. It is
true of the commits it names and of no other tree.

It is the fourteenth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-26.md`, and is an edit of none
of them. It is taken for the next `elendilon -> main` round, so the question
is that PR's: **what does this round cost `main`'s kernel?**

## The points

| | commit | what it is |
|---|---|---|
| **A** = **C** | `dccaad5b` | **`main`**: #313 (*Elendilon -> Main, Network Install, HTTP Drive, …*, `a9611731`) plus #314, the version file. `main` has not moved since, so the cut and the tip are one point |
| **B** | `b7633bb4` | **`elendilon-pr`**, as `origin/elendilon` plus one test fix. Its kernel inputs are byte-identical to `a5e69406`, which `elendilon` now points at, so the figures are the same for either branch |

The round is cut differently from the last one, and the attribution has to
follow it. `elendilon` did not go to `main` as itself: `elendilon-pr` (cut
from `elendilon` at `290ddaf1`, plus twelve bugfixes and #310) was
squashed as #313, and `elendilon` then went on from `290ddaf1` by itself.
`92340637` is the commit that brings it home - *elendilon squashed onto
main (after #313, #314)*, the diff `290ddaf1..19c15554` three-way merged onto
`dccaad5b` - and on the new history it is ONE commit. So the round is two
ranges measured separately:

- **`290ddaf1..19c15554`** on `origin/elendilon-old`, the work the squash
  carries: **226 commits and 45 merges**.
- **`dccaad5b..b7633bb4`** on `elendilon-pr`, the squash and what came after
  it: **106 commits** besides the squash, and **12 merges**.

**The squash's delta is exactly the range's.** `92340637 − dccaad5b` and
`19c15554 − 290ddaf1` agree to the byte in every section on all four
kernels, so the three-way merge onto `main` moved no kernel byte of its own
and the range's per-commit figures ARE the squash's. (`main`'s own bugfixes,
`290ddaf1 → dccaad5b`, are +64 `.text` and +6 `.cold` on `kern_big`; they
were the last round's and are in A.)

**332 commits and 57 merges, then.** **285 of the 332 move no byte on any
kernel**, and on `kern_big`, `kern_small` and `kern_emu` **three** move a
resident byte at all.

**The build number is held at 388 for every tree**: `BUILD_STR` is the
commit count (SPEC.md 14.2), and a digit is not a cost anybody spent.

**The budgets did not move.** `KERN_BUDGET` is 100,864 on `kern_big` and
`kern_emu` and 64,000 on `kern_small`, with `KERN_GRACE` 2,048.
**`make budget-check` passes at B on all three shipped kernels** - 3,584,
4,096 and 3,072 spare, the same as at A - so B could go to `main` as it
stands. `kern_mega` is a `KERN_KNOB` build: no `KERN_BUDGET` binds it.

## Headline: `kern_big`, `kern_small` and `kern_emu`

**The whole round is +46 resident bytes on each of the three shipped
kernels, and it is one row: `OSAPI_SEG`** (`55ccbe4a`, *kern_mega MC7 step 1:
OSAPI_SEG, segments and selectors without arithmetic*; SPEC.md 50.6.11).

| section | `kern_big` A | B | B − A | `kern_small` A | B | B − A | `kern_emu` A | B | B − A |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 41,128 | 41,134 | **+6** | 30,796 | 30,802 | **+6** | 41,384 | 41,390 | **+6** |
| `.bss` | 5,398 | 5,398 | 0 | 2,969 | 2,969 | 0 | 5,398 | 5,398 | 0 |
| `.cold` | 37,650 | 37,690 | **+40** | 21,541 | 21,581 | **+40** | 37,650 | 37,690 | **+40** |
| `.vga` | 2,032 | 2,032 | 0 | - | - | | 2,032 | 2,032 | 0 |
| `.lowbss` | 5,358 | 5,358 | 0 | 2,868 | 2,868 | 0 | 5,358 | 5,358 | 0 |
| `.vgabuf` | 336 | 336 | 0 | - | - | | 336 | 336 | 0 |
| **resident** | 91,902 | **91,948** | **+46** | 58,174 | **58,220** | **+46** | 92,158 | **92,204** | **+46** |
| `.ovl` (window) | 5,110 | 5,110 | 0 | 1,482 | 1,482 | 0 | 5,110 | 5,110 | 0 |
| `.boot2` (blob) | 6,444 | 6,447 | +3 | 4,562 | 4,565 | +3 | 6,559 | 6,562 | +3 |
| **`KERN_SIZE`** | 97,280 | **97,280** | 0 | 59,904 | **59,904** | 0 | 97,792 | **97,792** | 0 |
| spare of the budget | 3,584 | 3,584 | | 4,096 | 4,096 | | 3,072 | 3,072 | |

**The +46 is a new published slot and its real-mode body.** The API table
grows one rare cell (`OSAPI_RCXCELL osapi_seg` at `0x04E3`, 6 bytes of
`.text`), and `osapi_seg` in `.cold` is the 40 bytes that answer its verbs
on a real-mode kernel - `SEG_LIN`'s shift, `SEG_SUB`'s add, `SEG_REAL` and
`SEG_WIN` returning the value they were given, `SEG_FREE`'s clear. It is on
every kernel because a package asks it whichever kernel it runs on: that is
what lets the same `.o88` hold a segment on `kern_big` and a selector on
`kern_mega` without an `%ifdef` (docs/plans/KERN-MEGA-PLAN.md, and
KERN-MEGA-MEMORY-PLAN MC7). The Wire's level-6 fence (`d350b234`) is the
same cell seen from the catalog.

**The blob's +3 is the boot canary** (`ca2fc2c1`, SPEC.md 18.93.1): stage 2
compares the 16-bit SUM of the canary sector instead of one word, because
`kern_mega`'s word was 0 and so was the same word of the wrong sector a 286
ROM delivered. None of it is resident.

**The other two resident movers cancel**: `gfx_blitp`'s plane-major copy in
words (`26b839d0`, `.vga` +13) and back to `rep movsb` (`98637750`, −13),
because the word copy cost the XT 3.4 ms a Gorillas frame.

## Headline: `kern_mega`, the 286+ protected-mode kernel

| section | A | B | **B − A** |
|---|---:|---:|---:|
| `.text` | 46,944 | 47,510 | **+566** |
| `.bss` | 5,396 | 5,435 | **+39** |
| `.cold` | 42,290 | 44,849 | **+2,559** |
| `.lowbss` | 7,257 | 7,679 | **+422** |
| `.vgabuf` | 336 | 336 | 0 |
| conventional resident (the five above) | 102,223 | **105,809** | **+3,586** |
| `.mega` | 14,785 | 18,203 | **+3,418** |
| `.mega`'s scratch | 1,493 | 1,495 | +2 |
| `.boot2` (blob) | 6,585 | 6,620 | +35 |
| **`KERN_SIZE` at boot** | 124,928 | **131,584** | **+6,656** |
| **resident once switched** | 66,048 | **66,560** | **+512** |

This round IS `kern_mega`'s memory round
(docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md, MC1 to MC8), and it reads as
one: `.cold` and `.mega` are what moves above 1MB at the switch, so most of
the +6,656 at boot - thirteen rungs - goes back to the heap once the machine
is in protected mode, and what a running `kern_mega` keeps is **+512, one
rung**. What binds it:

- **the 64 KB segment**: `tools/os88megaseg.py` at B reads `.text`+`.bss`
  52,945, **12,591 left** (13,196 at A), and `kern_mega` carrying **4,381
  bytes beyond `kern_big`** there (3,782 at A). `tests/megaseg.txt`'s
  ratchet moved with it, 4,029 -> 4,381, in the squash;
- **above 1MB**: `.cold` 44,849 + `.mega` 18,203 + its scratch 1,495 =
  64,754 of the code area's **261,120-byte cap, 196,366 left**. The cap is
  new: MC2 (`42288dfe`) put the tables and the code at the TOP of RAM with
  the pool from 1MB, where A had a 110,592-byte gap between `MEGA_COLD_LIN`
  and `MEGA_XPOOL` (51,818 left). The two figures are not the same measure;
- its rungs at B: image 303 left (396 at A), cold 207 (206), **low 1**
  (423), vgabuf 176 (176), `.mega` 270 (106).

## The attribution is exact

Every non-merge commit was measured against its first parent. A merge is
charged whatever makes the running total equal its own measurement: `M − A
− Σ(every in-range commit M reaches)`. The previous report's `M − P1 − (P2 −
base)` is not exact here, because the memory branches forked BEFORE
`290ddaf1` and took `elendilon` in on the way, so `P2 − base` counted
elendilon's own commits a second time - it read a −2,818 merge on
`kern_mega` that is not one. **The commits and the merges sum to `B − A`
exactly, in every section, on all four kernels** (checked, not assumed), on
both ranges separately and together.

**The merges move nothing.** Four of the 57 carry a figure, and three of
them are one piece of bookkeeping: the memory branch forked before
`elendilon` had the `int 15h` keyboard (+20 `.text`) and the sector cache
(+77 `.cold`), and `2b68a8e3` (+20), `ad6da96c` (+77) and `242905c4` (−97)
are those bytes passing through and cancelling - **0 on every kernel**. The
one merge that makes a byte is `3356cd6c`'s **−2** of `kern_mega` `.cold`.

One tree does not assemble: `kern_mega` at `ad6da96c` (*Merge origin/elendilon
into claude/protected-mode-memory-consolidation*) fails nasm, and its only
child `0dd0e698` (*osapi_sys_kb widens vga_skkern unsigned*) is the fix. That
pair is measured as one step on `kern_mega`: `0dd0e698`'s own change is +1
of `.cold`, and the merge's +77 is in the bookkeeping above.

## By concept

Resident bytes (`.text`+`.bss`+`.cold`+`.vga`+`.lowbss`+`.vgabuf`); `.mega`
is `kern_mega`'s fifth code section, above 1MB once switched, and is a column
of its own.

| concept | commits | **`kern_big`** | **`kern_small`** | **`kern_emu`** | **`kern_mega`** | `kern_mega` `.mega` |
|---|---:|---:|---:|---:|---:|---:|
| **`kern_mega`'s memory round**: MC1 a kernel cache above 1MB is purgeable, MC2 the tables and code at the top of RAM, MC3 nothing in the pool pinned, MC4 the HMA is pool and the hibernate streams through the BIOS, MC5 `OSAPI_MEM_HUGE`'s one claim, MC7 steps 5.1-6 (a code/data pair per conventional claim, callers given the selector, frame buffers, ROMs and the font through selectors, file doors past one window, `SEG_SUBC`/`SEG_BWIN`/`SEG_AIM`), MC8's folds, D5, and the 14.121 follow-ons (`OSAPI_XMEM_WALK`'s long records, `OSAPI_XMEM_CAPS`, the boot's tables and the glyph copy above 1MB) | 29 | 0 | 0 | 0 | **+2,815** | +2,205 |
| **`OSAPI_SEG`** (MC7 step 1) - the one row that reaches every kernel | 1 | **+46** | **+46** | **+46** | +187 | +788 |
| **VESA.DRV under `kern_mega`** (KERN-MEGA-PLAN 14.115): `VXV_PM`, `int 10h` on the driver's own stack | 2 | 0 | 0 | 0 | +382 (blob +217) | +94 |
| **`kern_mega` input**: USBMOUSE's reflected `int 33h AX=0060h`, the key-state map through a reflection, Ctrl+] and the ROM's control codes, the pointer through a reflected `int 13h` | 4 | 0 | 0 | 0 | +138 | +234 |
| **`kern_mega` 14.77 / 14.90**: a move walks the IVT rows, Sticky Apps after `mega_enter`, `mod_drop` above 1MB | 3 | 0 | 0 | 0 | +41 | 0 |
| **`kern_mega` network ports** and the HTTP drive not bounced 512 bytes a GET (`FSCAP_SEL`) | 3 | 0 | 0 | 0 | +21 | 0 |
| **`kern_mega`'s DOS trip**: the last screen reaches DOS.O88's console, the stage's real segment | 2 | 0 | 0 | 0 | +4 | +97 |
| **`gfx_blitp`'s word copy**, in and back out | 2 | 0 (`.vga` ±13) | 0 | 0 | 0 | 0 |
| **The boot canary's sum** (SPEC.md 18.93.1) | 1 | 0 (blob +3) | 0 (blob +3) | 0 (blob +3) | 0 (blob +3) | 0 |
| the merges | 57 | 0 | 0 | 0 | −2 | 0 |
| the other 285 | | 0 | 0 | 0 | 0 | 0 |
| **the round** | 47 of 332 | **+46** | **+46** | **+46** | **+3,586** | **+3,418** |

The memory round's largest single rows on `kern_mega` are MC5's one claim
for all of it (`4db5ecff`, +470), MC7 5.2's callers receiving the selector
(`d7ab4b38`, +406), MC1 (`e97a1a70`, +317), MC7 5.6's packed files past one
window (`cc7c96df`, +309) and 5.7's `OSAPI_MEM_HUGE` below 1MB as a run of
selectors (`57146f60`, +244); `0a7b4024`'s three new `OSAPI_SEG` verbs are
+485 of `.mega` and no conventional byte. MC7 step 6 (`cb05914b`, tables
sized per machine) and MC4 (b) (`cf7ed437`) are the two that SAVED, −42 and
−13. MC8 (`3f6a218d`) took 199 bytes out of the blob.

## Where `kern_mega`'s bytes are, by file

`kernsize --modules -DKERN_BIG -DKERN_MEGA -DKERN_KNOB`, A → B, every row
that moved. (`kern_big` and `kern_small` move in `kernel.asm` alone: the
cell and `osapi_seg`.)

| file | `.text` | `.cold` | `.bss` | `.lowbss` | `.boot2` |
|---|---:|---:|---:|---:|---:|
| `mega.inc` | +187 | **+1,021** | 0 | +420 | +12 |
| `memory.inc` | +36 | **+772** | 0 | +2 | 0 |
| `diskw.inc` | 0 | **+393** | 0 | 0 | 0 |
| `desksc.inc` | 0 | +225 | 0 | 0 | −195 |
| `vga12.inc` | +95 | +16 | 0 | 0 | 0 |
| `kernel.asm` | +34 | +53 | 0 | 0 | +3 |
| `vidext.inc` | +37 | 0 | 0 | 0 | +199 |
| `viddet.inc` / `mouse.inc` | +52 / +22 | 0 | 0 | **−696 / +696** | 0 |
| `menu.inc` | +18 | 0 | +38 | 0 | 0 |
| `loader.inc`, `mod.inc`, `lz.inc` | 0 | +36, +31, +22 | 0 | 0 | 0 |
| `font.inc`, `fsx.inc`, `snd.inc` | +35, +28, +13 | 0 | 0 | 0 | +2, 0, 0 |
| `driver.inc` | +10 | +6 | +1 | 0 | +14 |
| `vidsel.inc`, `icons.inc` | +9, +9 | 0 | 0 | 0 | 0 |
| `disk.inc`, `hiber.inc` | +1, 0 | −17, +1 | 0 | 0 | 0 |
| `sched.inc`, `wm.inc` | −15, −5 | 0 | 0 | 0 | 0 |

`viddet.inc`'s −696 and `mouse.inc`'s +696 of `.lowbss` are one buffer
changing owner, as they were on `kern_big` last round (`ksp14 vesa`,
`cb0484c3`): `kern_mega` now shares the mouse ISR's stack with the row table
the same way.

## How close B stands to the next rung

| | image rung | top / cold rung |
|---|---:|---:|
| `kern_big` at A | 66 left | top 192 left |
| `kern_big` at B | **60 left** | top **152 left** |
| `kern_small` at A | 27 left | top 167 left |
| `kern_small` at B | **21 left** | top **127 left** |
| `kern_emu` at A | 386 left | top 198 left |
| `kern_emu` at B | 316 left | top 152 left |
| `kern_mega` at B | 303 left | cold 207, **low 1**, vgabuf 176, `.mega` 270 |

Per CLAUDE.md's banner, that decides WHEN a machine pays and never what a
change cost - but **`kern_small`'s next 22 bytes of `.text` or `.bss` are
billed 512**, and `kern_mega`'s low rung is full to the byte.

**Guard 5**, before a kernel cannot BOOT on 128 KB: `kern_big` 23,040,
`kern_small` 61,440, `kern_emu` 22,016 - all three as at A.

## The on-demand modules

Image unpacked plus bss, `make` / `make small` / `make mega` at the
endpoints. The claim is the image rounded up to whole KB.

**`kern_big`'s and `kern_small`'s modules are byte-identical at A and B.**
`kern_mega`'s are not:

| `kern_mega` module | A | B | **B − A** | claim, KB |
|---|---:|---:|---:|---|
| `HIBER.DRV` | 11,889 | 12,913 | **+1,024** | 12 → **13** |
| `DISKOPS.DRV` | 13,237 | 14,104 | **+867** | 13 → **14** |
| `CTRL.DRV` | 15,142 | 15,713 | **+571** | 15 → **16** |
| `EXTD.DRV` | 1,855 | 1,862 | +7 | 2 → 2 |

All three of the large ones cross a KB on their claim while they are loaded.
They are the memory round's on-demand halves: the hibernate's resume
streaming through the BIOS and saving what stands in the HMA (MC4), and
DISKOPS and `cpc_fdtest` keeping their paragraph arithmetic through
selectors (MC7.6).

## The loadable drivers

Image + bss, `make` at the endpoints; a driver is the same file on every
kernel. Every driver not listed is the same.

| driver | A | B | **B − A** | claim, KB |
|---|---:|---:|---:|---|
| `VESA.DRV` | 7,903 | 8,103 | **+200** | 8 → 8 |
| `RAMDISK.DRV` | 5,292 | 5,332 | +40 | 6 → 6 |
| `NET.DRV` | 4,163 | 4,199 | +36 | 5 → 5 |
| `SOUND.DRV` | 12,046 | 12,078 | +32 | 12 → 12 |
| `EMS.DRV` | 705 | 736 | +31 | 1 → 1 |
| `CDROM.DRV` | 9,033 | 9,050 | +17 | 9 → 9 |
| `HTTPFS.DRV` | 5,557 | 5,567 | +10 | 6 → 6 |
| `USBMOUSE.DRV` | 1,426 | 1,435 | +9 | 2 → 2 |
| `XMEM.DRV` | 2,042 | 2,041 | −1 | 2 → 2 |

`VESA.DRV`'s +200 is it becoming selector-aware for `kern_mega` (`786ae91c`,
`c462f96c`, 14.115). The small rows are each driver's `kern_mega` port,
by the commits that touch them: `RAMDISK`, `NET` and `CDROM` asking
`CPU_F_PM` per call instead of banking it at attach (`82b4f22e`, 14.112) and
MC8's folds (`3f6a218d`); `EMS` the `EMSV_GONE` verb (`27f3d119`);
`SOUND` a direct ring's block IRQ reading the 8237 only on `kern_mega`
(`c78b0edd`) and an OPL3-class chip's short write delays (`5734441e`) - the
PicoMEM ring change in this round was reverted (`627acdcd`); `HTTPFS` the
network port and `FSCAP_SEL` (`2ba554ce`, `d2114e7f`); `USBMOUSE` its
PicoMEM path refused under protected mode (`932e7784`). **No driver crosses
a KB.** `XMEM.DRV`'s −1 is it back inside 2 KB (`68d495e2`).

## The final bill

| | `kern_big` | `kern_small` | `kern_emu` | `kern_mega` |
|---|---:|---:|---:|---:|
| `KERN_SIZE` at `main` (A) | 97,280 | 59,904 | 97,792 | 124,928 at boot |
| `KERN_SIZE` at B | **97,280** | **59,904** | **97,792** | **131,584** at boot, **66,560 resident** |
| resident bytes, ours against `main` | **+46** | **+46** | **+46** | **+3,586** conventional, `.mega` +3,418 |
| ...of which `OSAPI_SEG` | +46 | +46 | +46 | +187, `.mega` +788 |
| `KERN_BUDGET` | 100,864 | 64,000 | 100,864 | none (a knob) |
| spare | **3,584** (7 steps) | **4,096** (8 steps) | **3,072** (6 steps) | segment 12,591, above 1MB 196,366 |
| image rung left | 60 | **21** | 316 | 303 |

**In one line each:**

- **`main`** has not moved since #314.
- **We** spent **+46 bytes on each shipped kernel** and crossed no rung on
  any of them: one published slot, `OSAPI_SEG`, and its real-mode body.
  The modules and the shipped kernels' footprints are what they were.
- **`kern_mega`** took its memory round: **+3,586** conventional and
  **+3,418** of `.mega` - thirteen rungs at boot, **one** once switched -
  plus three of its modules a KB larger on their claims.

## Found on the way

- **`main`'s tip builds here.** The last report found `b7fb2a37` red in
  `tools/x3xtgen.py` on nasm 2.16.01; `dccaad5b` builds `make`, `make small`
  and `make mega` clean, the fast tier 63/63.
- **One merge on the memory branch does not assemble on `kern_mega`**
  (`ad6da96c`), fixed by the next commit. Nothing ships from that tree; it is
  noted because a bisect over this range will land on it.
- **`kern_mega`'s low rung has ONE byte left** (423 at A). `MC1`'s 192 and
  `MC7 5.1`'s 64 of `.lowbss` and the mouse stack's +696 against the row
  table's −696 are where it went.
- **`docs/KERNEL-MEMORY.md`'s `kern_emu` baseline described neither A nor
  B** - `.text` 41,320 and `.cold` 37,644, from a tree before `main`'s last
  fixes - so `kernsize[emu]` read *"spent 116 bytes"* at B for a round that
  spent 46. The `kern_big` and `kern_small` baselines were A's. **All three
  re-blessed at B in the same commit as this file.**

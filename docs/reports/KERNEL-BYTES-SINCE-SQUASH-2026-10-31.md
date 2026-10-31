# Kernel bytes since the #318 squash, by concept

**A measurement, not a description.** Taken 2026-10-31 on a four-core cloud
container: `nasm` 2.16.01, Python 3.13. Every resident figure comes from
`tools/kernsize.py --json` (and `--modules` for the per-file table), which
RE-ASSEMBLES the kernel rather than reading a build. Every commit of the
round, and every parent it names, was measured in a clean tree of its own:
`git archive` of that commit's `kernel/`, `boot/`, `tools/`, `drivers/`,
Makefile and the top-level files of `apps/`, `apps/stiklib.asm` assembled
there into the `stiklib.bin` that `deskmod.inc` includes, and that commit's
own `kernsize` measuring **`kern_big`, `kern_small`, `kern_emu` and
`kern_mega`**. A tree whose kernel inputs hash the same as one already
measured takes that tree's figures: **200 points, 89 distinct kernel
trees**. `associco.inc` is B's in every tree. Module and driver images come
from `make`, `make small`, `make emu` and `make mega` at the two endpoints
only. It is true of the commits it names and of no other tree.

It is the fifteenth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-28.md`, and is an edit of none
of them. It is taken for the next `elendilon -> main` round, so the question
is that PR's: **what does this round cost `main`'s kernel?**

## The points

| | commit | what it is |
|---|---|---|
| **C** | `c2e139eb` | #318, *Elendilon -> Main (kern_mega Memory Refactor, RealDMG Speed, Video Narrow Rows/Flips, Bugfixes)* - the squash this round starts from |
| **A** | `7b4b441d` | **`main`**: C plus #320 (WIN16), #323, #324 and #325. **None of the four moves a kernel byte**: A and C measure identically in every section on all four kernels, so the round is measured from C and compared against A |
| **B** | `elendilon-pr` | `origin/elendilon` (`c844ccc0`) plus `main`'s CLAUDE.md and AGENTS.md. Its kernel inputs are byte-identical to `c844ccc0`'s |

The round is cut the way the last one was, so the attribution follows the
same shape. `elendilon` went on from `aee2631a`, the commit `elendilon-pr`
was cut from for #318, and `f8391471` brings that work home - *elendilon
squashed onto main (after #318)*, three-way merged onto C - as ONE commit on
the new history. So the round is two ranges measured separately:

- **`aee2631a..9a81a160`** on `origin/elendilon-old`, the work the squash
  carries: **65 commits, 7 of them merges**.
- **`c2e139eb..elendilon-pr`** less `main`'s four, the squash and what came
  after it: **131 commits, 20 of them merges**.

**The squash's delta is exactly the range's.** `f8391471 − c2e139eb` and
`9a81a160 − aee2631a` agree to the byte in every section on all four
kernels, so the three-way merge moved no kernel byte of its own (its one
hand-resolved conflict, `drivers/sound/sb.inc`, is a driver's) and the
range's per-commit figures ARE the squash's.

**196 commits and 27 merges, then.** On `kern_big` **19** of them move a
resident byte; on `kern_small` 14; on `kern_mega` 32.

**The build number is held at 412 for every tree**: `BUILD_STR` is the
commit count (SPEC.md 14.2), and a digit is not a cost anybody spent.

**The budgets did not move.** `KERN_BUDGET` is 100,864 on `kern_big` and
`kern_emu`, 64,000 on `kern_small` and - new this round - **68,096 on
`kern_mega`** (rule 4, kern_mega size pass 1), with `KERN_GRACE` 2,048.
**`make budget-check` passes at B on all four budgeted kernels** - 3,584,
4,096, 3,072 and 3,072 spare - so B could go to `main` as it stands.

## Headline: `kern_big`, `kern_small` and `kern_emu`

**The round SAVES 442 resident bytes on `kern_big` and `kern_emu` and 383 on
`kern_small`.** Three rows make it, and the whole of the saving is kernel
size pass 15.

| section | `kern_big` A | B | B − A | `kern_small` A | B | B − A | `kern_emu` A | B | B − A |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 41,134 | 40,951 | **−183** | 30,802 | 30,675 | **−127** | 41,390 | 41,207 | **−183** |
| `.bss` | 5,398 | 5,390 | −8 | 2,969 | 2,965 | −4 | 5,398 | 5,390 | −8 |
| `.cold` | 37,690 | 37,439 | **−251** | 21,581 | 21,329 | **−252** | 37,690 | 37,439 | **−251** |
| `.vga` | 2,032 | 2,032 | 0 | - | - | | 2,032 | 2,032 | 0 |
| `.lowbss` | 5,358 | 5,358 | 0 | 2,868 | 2,868 | 0 | 5,358 | 5,358 | 0 |
| `.vgabuf` | 336 | 336 | 0 | - | - | | 336 | 336 | 0 |
| **resident** | 91,948 | **91,506** | **−442** | 58,220 | **57,837** | **−383** | 92,204 | **91,762** | **−442** |
| `.ovl` (window) | 5,110 | 5,119 | +9 | 1,482 | 1,477 | −5 | 5,110 | 5,119 | +9 |
| `.boot2` (blob) | 6,447 | 6,455 | +8 | 4,565 | 4,745 | +180 | 6,562 | 6,570 | +8 |
| **`KERN_SIZE`** | 97,280 | **97,280** | 0 | 59,904 | **59,904** | 0 | 97,792 | **97,792** | 0 |
| spare of the budget | 3,584 | 3,584 | | 4,096 | 4,096 | | 3,072 | 3,072 | |

| concept | commits | **`kern_big`** | **`kern_small`** | **`kern_emu`** |
|---|---:|---:|---:|---:|
| **Kernel size pass 15** (`HANDOFF-KERNEL-SIZE-P16.md`; five agents and a close-out) | 37 | **−537** | **−386** | **−537** |
| **The icon store grows, and `ASSOC.DAT`'s reader claims the file's size** (`9bbf5612`, in the squash; SPEC.md 25.9.6, 54.7.7) | 1 | **+92** | 0 | **+92** |
| **`FSXF_REAL`** - every kernel takes the real-mode bracket's flag bit and drops it (`c6f805ea`, SPEC.md 53.2.3) | 1 | +3 | +3 | +3 |
| the other 157 and the 27 merges | | 0 | 0 | 0 |
| **the round** | | **−442** | **−383** | **−442** |

**Size pass 15's −537** is its own record's figure to the byte: `.text`
−188, `.cold` −341, `.bss` −8. The blob's +8 (and `kern_small`'s +180) and
the overlay's +9 are where it put code that had been resident -
`desk_rowcalc` (`dec1aef4`) and `vid_ctx_capture` (`fdb3ba59`) expanded in
the boot and the overlay instead. Its largest rows on `kern_big`:
`desk_rowcalc` out of resident −100, the close-out −75, `disk_mount_x`'s
single-caller helpers −67, the WM/UI prologues onto `kent_di` −66, the
single-caller helpers in `disk`/`driver`/`mod`/`files` −56, the lone
relocated words as one `mem_rr_tab` row −36. **No commit in the pass adds a
resident byte on `kern_big`** but `7d6dc26a` (+12 `.text`, −12 `.cold`, 0
net).

**The icon store's +92** is `.text` +2 and `.cold` +90: `ico_add` growing
the machine-wide store a KB at a time through `mem_regrow` (+79), and
`asc_use` claiming `ASSOC.DAT`'s own size after the root walk (+13). It is
the one feature byte the round spends on the shipped kernels, and it is what
put the generic icon off a standard two-disk 1.44MB install (`tests/icohd.py`).
`kern_small` is byte-identical: it has no growing store.

**`FSXF_REAL`'s +3** is one `and cl, ~FSXF_REAL & 0xFF` in `fsx_run`: a
kernel that has no real-mode bracket takes the bit and ignores it, so the
same `VIDEO.O88` asks for it on every kernel.

## Headline: `kern_mega`, the 286+ protected-mode kernel

| section | A | B | **B − A** |
|---|---:|---:|---:|
| `.text` | 47,510 | 46,573 | **−937** |
| `.bss` | 5,435 | 5,427 | −8 |
| `.cold` | 44,867 | 45,920 | **+1,053** |
| `.lowbss` | 7,679 | 6,751 | **−928** |
| `.vgabuf` | 336 | 336 | 0 |
| conventional sections (the five above) | 105,827 | **105,007** | **−820** |
| `.mega` | 18,234 | 21,440 | **+3,206** |
| `.mega`'s scratch | 1,496 | 1,490 | −6 |
| `.boot2` (blob) | 6,620 | 6,708 | +88 |
| **`KERN_SIZE` at boot** | 131,584 | **134,144** | **+2,560** |
| **resident once switched** (`MEGA_RESIDENT`, the budgeted figure) | 66,560 | **65,024** | **−1,536** |

| concept | commits | conventional | `.mega` | `KERN_SIZE` | **resident once switched** |
|---|---:|---:|---:|---:|---:|
| **kern_mega size pass 1** (`docs/plans/completed/msp1/`) | 31 | **−1,358** | −1,249 | −2,048 | **−2,560** |
| **Kernel size pass 15**'s `kern_mega` rows (its `recent` agent) and the shared code | 37 | −574 | −400 | −512 | 0 |
| **The real-mode fsx bracket** (`c6f805ea`, MEGA-VIDEO-REALMODE-PLAN W0-W7, SPEC.md 53.2.3) | 1 | **+971** | **+4,793** | +6,144 | **+1,024** |
| **The squash**: the icon store (+92), a service reflection catching the option ROM's software IRQs (+22) and a hardware IRQ cell left as a selector (+14) | 3 | +128 | +3 | 0 | 0 |
| the merges | 20 | +13 | +59 | −1,024 | 0 |
| **the round** | | **−820** | **+3,206** | **+2,560** | **−1,536** |

What a running `kern_mega` keeps is **1,536 bytes less** - three rungs out
by size pass 1 (`21aeb12b`'s knob pad out of `.lowbss`, −1,024; `4c938dbc`'s
boot-once switch and protected-mode-only helpers out of `.text` into
`.cold`, −1,024 of resident and +29 bytes in all; `c5b334bc`'s debug port
silenced in the shipped kernel, −512) and one back in for the real-mode
bracket (+1,024). At boot it is **+2,560**, five rungs, because the bracket's
4,793 bytes of `.mega` and 366 of `.cold` are read in conventional memory
before they move above 1MB at the switch.

**The bracket is the one feature row**, and it is `kern_mega`'s whole: the
Video Player playing at parity with `kern_big` on the Victory 286
(`docs/reports/VIDPROF-W6-2026-10-30.md`). Its +96 of `.lowbss` and the
real-mode half of its `.text` (`fsx_rland`, `fsx_rtick`, `fsx_rcall`'s head,
`mega_rland`) have to be low; `tests/megaseg.txt` records each one.

**The merges move no resident byte.** Their −1,024 of `KERN_SIZE` is rung
arithmetic - the size passes rounded on their own bases and the merged tree
rounds once - and `fa44600f` (*Merge elendilon (kernel size pass 15) into
mega-size-1*) carries +13 `.cold` and +59 `.mega`: the two passes' savings
on `kern_mega`, each measured against its own base, overlap by that much, and
the merge gives it back.

What binds `kern_mega` at B:

- **the budget**: resident 65,024 of 68,096, **3,072 spare**;
- **the 64 KB segment**: `tools/os88megaseg.py` reads `.text`+`.bss`+`.vga`
  52,000, **13,536 left** (12,591 at A), `kern_mega` carrying **3,627 bytes
  beyond `kern_big`** there (4,381 at A). `tests/megaseg.txt` is ratcheted to
  3,627;
- **above 1MB**: `.cold` 45,920 + `.mega` 21,440 + its scratch 1,490 =
  69,010 of the code area's 261,120-byte cap, **192,110 left** (196,334 at A);
- its rungs: image 224 left (303 at A), cold 160 (189), low **417** (1),
  vgabuf 176 (176), `.mega` 110 (238).

## The attribution is exact

Every non-merge commit was measured against its first parent. A merge is
charged whatever makes the running total equal its own measurement: `M − C −
Σ(every in-range commit M reaches)`, with `main`'s four commits charged as
one step `A − C` (zero). **The commits and the merges sum to `B − C`
exactly, in every section, on all four kernels** (checked, not assumed), on
both ranges separately.

**84 of the 89 trees' `kern_mega` assembles only with `-DKERN_KNOB`**: it was
a knob build until `fc8c0b08` made it a budgeted one, and those trees were
measured with the knob. `msp1/megabudget.md` checked that the change itself
is byte-identical, and this measurement agrees - `fc8c0b08` moves no section.
A knob tree's `kernsize --json` reports no `MEGA_RESIDENT`, so the resident
column here is `KERN_SIZE − (.cold + .mega rungs)` for every tree, which is
the assembler's own definition and reads 65,024 at B as `kernsize` does.

## Where the bytes are, by file

`kernsize --modules`, A → B, every row that moved more than 4 bytes of code
(`.text` / `.cold` / `.boot2`).

| file | `kern_big` | `kern_small` | `kern_mega` |
|---|---:|---:|---:|
| `mega.inc` | - | - | **−1,062 / +773 / −53** |
| `fsx.inc` | +3 / 0 | +3 / 0 | **+329 / +444**, `.lowbss` +96 |
| `dskwin.inc` | 0 | 0 | `.lowbss` **−1,024** |
| `desk.inc` | 0 / −103 | 0 / −123 / +179 | 0 / −103 |
| `wm.inc` | −72 / −8 | −72 / 0 | −72 / −8 |
| `driver.inc` | −4 / −39 / +7 | 0 | −4 / +26 / +140 |
| `snd.inc` | 0 | 0 | −42 / 0 |
| `files.inc` | 0 / −38 | 0 / −36 | 0 / −38 |
| `disk.inc` | +1 / −5 | 0 / −70 | +2 / −5 |
| `vidsel.inc` | −29 / 0 | 0 | −29 / 0 |
| `diskw.inc` | 0 / −26 | 0 / −5 | 0 / −26 |
| `ui.inc` | −22 / 0 | −22 / 0 | −22 / 0 |
| `menu.inc` | −14 / −4 | −14 / −4 | −14 / −4 |
| `fdlg.inc` | 0 / −14 | 0 / −3 | 0 / −14 |
| `mod.inc` | 0 / −3 | 0 / −3 | 0 / −13 |
| `kernel.asm` | −4 / −2 / +1 | −4 / 0 / +1 | −13 / +34 / +1 |
| `vga12.inc` | −9 / 0 | −5 / 0 | −9 / 0 |
| `memory.inc` | −2 / −4 | +4 / −4 | +14 / −4 |

## How close B stands to the next rung

| | image rung | top / cold rung |
|---|---:|---:|
| `kern_big` at A | 60 left | top 152 left |
| `kern_big` at B | **251 left** | top **403 left** |
| `kern_small` at A | 21 left | top 127 left |
| `kern_small` at B | **152 left** | top **379 left** |
| `kern_emu` at A | 316 left | top 152 left |
| `kern_emu` at B | 507 left | top 403 left |
| `kern_mega` at A | 303 left | cold 189, **low 1**, vgabuf 176, `.mega` 238 |
| `kern_mega` at B | 224 left | cold 160, low 417, vgabuf 176, `.mega` 110 |

Per CLAUDE.md's banner, that decides WHEN a machine pays and never what a
change cost. What it records here is that the round paid down the two
places the last report found tight: `kern_small`'s image rung (21 → 152)
and `kern_mega`'s low rung (1 → 417).

**Guard 5**, before a kernel cannot BOOT: `kern_big` 23,040, `kern_small`
61,440, `kern_emu` 22,016 on 128 KB - all three as at A - and `kern_mega`
509,952 in 640 KB (512,512 at A).

## The on-demand modules

Image unpacked plus bss, at the endpoints. The claim is the image rounded up
to whole KB. Every module not listed is byte-identical.

| module | kernel | A | B | **B − A** | claim, KB |
|---|---|---:|---:|---:|---|
| `CTRL.DRV` | `kern_big` | 15,274 | 15,428 | **+154** | 15 → **16** |
| `CTRL.DRV` | `kern_emu` | 15,292 | 15,446 | **+154** | 15 → **16** |
| `CTRL.DRV` | `kern_small` | 5,498 | 5,619 | +121 | 6 → 6 |
| `CTRL.DRV` | `kern_mega` | 15,713 | 15,778 | +65 | 16 → 16 |
| `EXTD.DRV` | `kern_big`, `kern_emu` | 1,837 | 1,872 | +35 | 2 → 2 |
| `EXTD.DRV` | `kern_mega` | 1,862 | 1,897 | +35 | 2 → 2 |
| `DISKOPS.DRV` | `kern_mega` | 14,104 | 13,695 | **−409** | 14 → 14 |
| `FDLG.DRV`, `FILECP.DRV` | `kern_small` | 2,868, 5,070 | 2,867, 5,069 | −1, −1 | 3, 5 |

`CTRL.DRV`'s and `EXTD.DRV`'s growth is size pass 15 moving code OUT of
resident: `desk_rowcalc` and `vid_ctx_capture` became macros expanded in the
modules that call them (`dec1aef4`, `fdb3ba59`). That is the trade the
on-demand rule asks for - a byte present only while the Control Panel is
open instead of from boot to power-off - and it crosses a KB on
`kern_big`'s `CTRL.DRV` claim while the panel is up. `DISKOPS.DRV`'s −409 on
`kern_mega` is the net of kern_mega size pass 1 (which also moved `KSEG`'s
near helpers into it, `21aeb12b`) and the real-mode bracket.

## The loadable drivers

Image + bss, `make` at the endpoints; a driver is the same file on every
kernel. **One driver moved:**

| driver | A | B | **B − A** | claim, KB |
|---|---:|---:|---:|---|
| `SOUND.DRV` | 12,263 | 10,497 | **−1,766** | 12 → **11** |

What it KEEPS after its attach is the figure a machine lives with
(`tests/unit/t_sndonce.py` reads both off the listing):

| `SOUND.DRV` keeps | A | B |
|---|---:|---:|
| no mixer | 7,686 (8 KB) | **5,834 (6 KB)** |
| no mixer, a PicoMEM | - | 5,950 (6 KB) |
| no mixer, an MPU-401 | - | 6,135 (6 KB) |
| a mixer (SB Pro, SB16) | 11,231 (11 KB) | **8,659 (9 KB)** |

Three things made it: the Sound Blaster's DSP scan and IRQ discovery made
attach-only (`1ce9e606`, in the squash, SPEC.md 34.5.7), recording made one
path - the caller's external ring - with the Recorder moved onto it
(`4038879d`, SPEC.md 34.6.5), and size pass 15's `snd` agent, whose two
targets (under 6 KB without a mixer, 9 KB with one) are both met. A
PicoMEM's DSP is switched off before the port walk (`214ed383`, +7).

## The final bill

| | `kern_big` | `kern_small` | `kern_emu` | `kern_mega` |
|---|---:|---:|---:|---:|
| `KERN_SIZE` at `main` (A) | 97,280 | 59,904 | 97,792 | 131,584 at boot, 66,560 resident |
| `KERN_SIZE` at B | **97,280** | **59,904** | **97,792** | **134,144** at boot, **65,024 resident** |
| resident bytes, ours against `main` | **−442** | **−383** | **−442** | **−820** conventional, `.mega` +3,206, **−1,536** once switched |
| ...of which size passes | −537 | −386 | −537 | −1,932 conventional (pass 1 and pass 15), −2,560 once switched |
| ...of which features | +95 | +3 | +95 | +1,099 conventional (the merges +13), +1,024 once switched |
| `KERN_BUDGET` | 100,864 | 64,000 | 100,864 | 68,096 (new) |
| spare | **3,584** (7 steps) | **4,096** (8 steps) | **3,072** (6 steps) | **3,072** (6 steps) |
| image rung left | 251 | 152 | 507 | 224 |

**In one line each:**

- **`main`** moved no kernel byte since #318: WIN16 is a package.
- **We** saved **442 bytes on `kern_big`** and **383 on `kern_small`**,
  with one feature row on them (+92, the growing icon store) inside a size
  pass worth −537, and crossed no rung on any shipped kernel.
- **`kern_mega`** became a budgeted build, had its first size pass, and
  took the real-mode bracket: **−1,536** resident once switched, +2,560 at
  boot.
- **`SOUND.DRV`** keeps **6 KB** without a mixer and **9 KB** with one,
  from 8 and 11.

## Found on the way

- **`docs/KERNEL-MEMORY.md`'s `kern_mega` baseline described neither A nor
  B** - it was kern_mega size pass 1's tip (64,000), so `kernsize[mega]`
  read *"was 4,096 [+1,024]"* at B: that +1,024 is the real-mode bracket,
  which landed after the pass. The `kern_big`, `kern_small` and `kern_emu`
  baselines were A's. **All four re-blessed at B in the same commit as this
  file.**
- **No API cell was added and the ABI lock did not bump**: `FSXF_REAL` and
  its `FSXR_*` answers are a flag bit and an output on an existing cell, and
  `tools/abi.lock.json` records four decisions, each against both guards
  (`PKG_FMT` stays 10, `DRV_VER` 11).
- **`main`'s tip builds here**, and so does B: `make`, `make small`,
  `make emu` and `make mega` clean. `make budget-check` passes at A on the
  three kernels `main` budgets and at B on all four.

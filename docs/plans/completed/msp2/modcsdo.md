# msp2 agent "modcsdo" - the CSDO/KSEG sites in the on-demand modules

kern_mega size pass 2, branch `msp2-modcsdo`, cut from `ead16c61` (build
477). Scope (BRIEF.md 0.2): every CSDO/KSEG site in an on-demand module image
and `.modl`, and the whole of `compress.inc`, `stikmenu.inc`, `ascw.inc`,
`serial.inc`, `shutdown.inc`, `winmod.inc` and `dockmod.inc`. The start was
msp1/shared.md DEFERRED 1 - *load DS once per routine, a near door per module
for what is left* - and its TAKEN 2 (the KSEG near helpers, which already
covered `.modl`'s KSEG sites and were left as they are).

**The finding that shaped the pass**: a near door shrinks a module IMAGE and
GROWS its packed FILE. Every module but CTRL.DRV ships packed, and the six
bytes of `push ds / call KERNEL_SEG:mega_dsimg` are the SAME six bytes at
every site - the packer stores them for next to nothing - while a near
call's displacement is different at every site and does not pack at all. So
the door was built for every module, measured, and kept only in CTRL.DRV,
the one image shipped unpacked; the RUN (`CSDO_ON` ... `CSDO_OFF`) shrinks
both the image and the file, and is the lever everywhere else.

## BASE AND TIP

Both measured in this worktree. kern_mega, `kernsize --build build/megak
--json`: **every field identical, base and tip** - `.text` 46,579, `.bss`
5,427, `.cold` 46,165, `.lowbss` 6,751, `.vgabuf` 336, `.mega` 21,440
(+1,490 scratch), `.boot2` 6,709, `.ovl` 5,118, `KERN_SIZE` 134,656, footprint
after the switch (`mres`) 65,024 of 68,096. Conventional-resident total
(`.text`+`.bss`+`.lowbss`+`.vgabuf`) 59,093, unchanged. This pass moves no
resident byte: the work is all in module images. The resident kernel's bytes
differ only where `.cold`/`.mega` name an offset INSIDE a module image
(CTRL.DRV's and HIBER.DRV's bss labels, which moved with the images).

The ten modules on kern_mega. Image = the module's file before packing
(image + bss, `os88mod`); claim = `mod_need`'s, the file to the 512-byte
granule; packed = the file on the system disk.

| module | image base -> tip | claim base -> tip | packed file base -> tip |
|---|---:|---:|---:|
| CTRL.DRV | 15,611 -> **15,475** (-136) | 15,872 -> 15,872 | unpacked: = image |
| FORMAT.DRV | 3,551 -> **3,432** (-119) | 3,584 -> 3,584 | 3,036 -> 3,037 (+1) |
| DISKOPS.DRV | 11,647 -> **11,301** (-346) | 11,776 -> 11,776 | 9,597 -> 9,555 (-42) |
| HIBER.DRV | 11,584 -> **11,489** (-95) | 11,776 -> 11,776 | 9,376 -> 9,362 (-14) |
| DOCK.DRV | 2,330 -> **2,323** (-7) | 2,560 -> 2,560 | 2,094 -> 2,091 (-3) |
| EXTD.DRV | 1,897 -> **1,890** (-7) | 2,048 -> 2,048 | 1,843 -> 1,839 (-4) |
| WIN95.DRV | 1,642 -> **1,607** (-35) | 2,048 -> 2,048 | 1,382 -> 1,374 (-8) |
| DESK.DRV | 4,776 -> 4,776 | 5,120 -> 5,120 | 4,273 -> 4,273 |
| STIKMENU.DRV | 15,549 -> **15,286** (-263) | 15,872 -> **15,360** (-512) | 4,235 -> 4,238 (+3) |
| SERIAL.DRV | 1,021 -> **1,000** (-21) | 1,024 -> 1,024 | 879 -> 875 (-4) |
| **total** | **-1,029** | -512 | -71 |

**After merging `mega-size-2`** (pmcore, megados, pmmem; `68e5bb10`) the
branch's modules read the same but for HIBER.DRV, 11,365 bytes (megados and
pmmem changed it too; its claim stays 11,776); `make -j1 mega` and `make -j1`
(fast tier, 68 passed) green on the merged tree.

The claim column is a granule column, which is a rung by another name: the
bytes are the image column.

**kern_big, kern_small, kern_emu: byte-identical**, checked at ONE build
number (`OS88_BUILDNUM=477`, the base commit's count) rather than across
commits: kern_big's `KERNEL.SYS` md5 `00ec586b...` and all ten modules
`cmp`-equal to the base build's; kern_small's `21eabeb4...` and its six
modules equal; kern_emu built from this tree and from a worktree of
`ead16c61`, `a807da06...` both, all ten modules equal. Every change is a
macro whose kern_mega arm alone emits anything, a local label renamed (no
bytes), or a `%ifdef KERN_MEGA` arm (`hbm_xwrite`).

## TAKEN

1. **`CSDO_ON` / `CSDO_OFF`: DS = the image once for a RUN** (`kernel.asm`).
   `push ds / call KERNEL_SEG:mega_dsimg` at the head, `pop ds` at the end,
   on kern_mega; nothing at all on any other build, where `[CSEG x]` is
   `[cs:x]`. FLAGS kept across both ends, as CSDO keeps them. Not nestable,
   and a CSDO inside a run is an assembly error (a `%define CSDO_OPEN` the
   macros track on every build, zero bytes). Each site folded into a run is
   7 bytes and one far call fewer. **403 CSDO sites became 273 far calls**
   (`compress.inc` 121 -> 78, `stikmenu.inc` 105 -> 68, `ascw.inc` 51 -> 34,
   `hiber.inc` 42 -> 28, `serial.inc` 16 -> 13, `shutdown.inc` 16 -> 13,
   `desksc.inc` 13 -> 9, `winmod.inc` 10 -> 5, `dockmod.inc` 9 -> 8,
   `ctrl.inc` 8 -> 7, `diskops.inc` 6 -> 5, `extmod.inc` 3 -> 2; `driver.inc`
   3, all singles). Found mechanically (consecutive CSDO lines, then runs
   joined across gaps of instructions that touch no DS memory, no stack and
   no control flow) and then by hand for the LOOPS: a run may hold a loop
   whose labels nothing outside it names, so `stm_cpy`, `stm_cat`,
   `stm_frame`, `stm_box`, `stm_num`, `stm_text`'s glyph rows, the card's
   picture, tick, invert and polarity passes (STIKMENU.DRV), `wn_title`'s
   caption copy (WIN95.DRV) and `dkx_order`'s two fills (DOCK.DRV) take ONE
   far call for the loop where they took one per byte.
   `CSDO_SP` is a run now too (one far call where it made two), the same SP
   banked: SP - 2 under the run's `push ds`, plus 2.
2. **`hbm_xwrite`'s five stores, one run** (`hiber.inc`, in its `%ifdef
   KERN_MEGA` arm, so kern_big cannot see it): the four kernel words are read
   into AX BX CX SI first (the routine clobbers all four by contract;
   `hbm_xfree` after it takes DX), then one run stores all five. -28 bytes of
   HIBER.DRV, four far calls fewer per hibernate.
3. **CTRL.DRV's swap door** (`CSDO_SDOOR csd_modc`, in the settings core
   before `mods_end` so either load has it): a CSDO or CSDO_ON in `.modc` or
   `.modu` is `call csd_modc` - three bytes where `push ds / call far` is six.
   The door pushes DS UNDER its own return (`push ds / push bp / mov bp, sp /
   push ax / mov ax, [bp+4] / xchg ax, [bp+2] / mov [bp+4], ax / pop ax / pop
   bp`, no flag written), then far-calls `mega_dsimg`, which reads the CS off
   that far call - CTRL.DRV's own - exactly as it reads a site's. 22 bytes
   once; CTRL.DRV's ~32 remaining sites (ctrl, shutdown, desksc, driver.inc's
   settings macros, and `.modu`'s KSW_SECT copies) pay it back. Only CTRL.DRV
   because it is the one module shipped unpacked (see REFUSED 1). Stack: two
   bytes deeper than the inline form at the moment `mega_dsimg` runs (14
   against 12), on the UI task's stack, which is where the Control Panel runs.
4. **`CSDOF`**: CSDO that never takes a door, for the compressor's
   per-position and per-bit sites (`cmz_probe`'s `cmz_pos`/`cmz_most`,
   `cmz_bit`'s two `cmz_tbit`). With no door in DISKOPS.DRV after REFUSED 1 it
   is the same bytes as CSDO there; it stays as the mark, so a door added
   later cannot quietly reach the hot path.
5. **The gate** (`tests/unit/t_cswrite.py`, fast tier, so `make` runs it):
   a run is held to its rules on the arms kern_mega assembles - entered only
   at its CSDO_ON and left only at its CSDO_OFF (a label inside it named by
   nothing outside it, a jump inside it to a label inside it; `.x` labels
   scoped as nasm scopes them), no call, return, int, string instruction,
   macro or nested CSDO, pushes and pops balanced inside it, DS neither read
   nor written, every memory operand naming its segment (`[CSEG x]`, `cs:`,
   `es:`, `ss:`) or a BP base. All of these assemble cleanly on every build
   and break only kern_mega, which is the case a gate is for. **Broken on
   purpose**: a scratch file with one of each - a plain `[y]`, `mov ax, ds`,
   a `call`, a `KSEG`, an unbalanced push, a pop of what was never pushed, a
   jump out of a run, a jump into one - went red on all nine, and green
   again without it. `[CSEG x]` inside a run is not counted as a CS write
   (it is converted, as on a CSDO line), so `tests/cswrites.txt` is
   unchanged. `tools/stkbalance.py` counts CSDO_ON +1 and CSDO_OFF -1
   (kern_mega's depth, the build on which a jump out of a run is a defect)
   and knows CSDOF; over the thirteen touched files it reports the same six
   unbalanced paths at base and tip, all six pre-existing tail jumps.

**Cycles** (kern_mega; estimates from the 286's instruction timings, not
measured): one CSDO is `push ds` + a protected-mode far call into
`mega_dsimg` (three pushes, a compare through CS, a DS load) + a far return +
`pop ds`, which reloads DS again - ~140 cycles, two descriptor loads among
them. Every site folded into a run saves that and nothing in a run is slower,
so **every path these files have is cycle-neutral or faster**; the ones that
gain most are the compressor's per-symbol `.newmx` (six far calls -> one),
`cmz_probe`'s best-match store (two -> one), `cmz_room` and `cmz_sub1`/
`cmz_add1` (two -> one), and STIKMENU.DRV's glyph rows (eight per character
-> one). CTRL.DRV's door costs its ~32 sites ~50 cycles each over the inline
form (a near call and return and the swap) - the Control Panel and Shut Down,
UI paths none of which is in BRIEF.md's hot list. No hot routine `hotpar`
knows was touched; `hotpar` is green.

**The 286 reset path**: HIBER.DRV's DOS trip (`hbm_dosrun`, and the resume it
returns into) runs CSDO sites around the 286's CMOS-shutdown reset. Nothing
on that path changed its order or what it computes - a run is the same stores
with the same DS, one far call instead of several - so equivalence is by
reading; HIBER.DRV has no door. Named for the 86Box run in ROWS TO RUN.

## REFUSED

1. **A near door in every module (msp1/shared DEFERRED 1's second half)**:
   BUILT AND MEASURED, kept in CTRL.DRV alone. A swap door in DISKOPS,
   STIKMENU, FORMAT, HIBER (and a plain 6-byte one in SERIAL, DOCK, WIN95)
   took the images a further -611 bytes (DISKOPS 11,301 -> 11,074, STIKMENU
   15,286 -> 15,103, FORMAT 3,432 -> 3,343, HIBER 11,489 -> 11,421) and the
   PACKED FILES +433 (DISKOPS 9,555 -> 9,771, STIKMENU 4,238 -> 4,377, FORMAT
   3,037 -> 3,111, HIBER 9,362 -> 9,427, SERIAL 875 -> 917): the far call is
   six identical bytes the packer stores for almost nothing, a near call's
   displacement differs at every site. It also costs every remaining site the
   door's ~50 cycles. The trade is heap-while-loaded (above 1MB on kern_mega
   for most claims) against floppy bytes and load time, and it is the
   owner's - WANTS THE OWNER 1. With the doors DISKOPS.DRV's claim fell a
   granule (11,776 -> 11,264), which is a rung and was not the reason either
   way.
2. **A whole-routine image DS** (the literal reading of "load DS once per
   routine"): the module routines with the most writes read KERNEL data
   through plain `[x]` between them (`fm_*`, `dsk_*`, `gfx_*`, `thm_*`), and
   the compressor's parse runs with DS = the SOURCE, so no routine of any
   size can hold DS = image throughout. The run is that idea at the size the
   code allows, and the loops are where it was worth most.
3. **`CSDO pop` / `CSDO push` in a run**: never. The gate refuses an
   unbalanced push or pop inside a run, because the run's `pop ds` would take
   the wrong word (DEFECTS FOUND 1 is the same mistake on a CSDO line).

## DEFERRED

Estimates are labelled.

1. **The remaining 273 far calls, by register reordering: ~-200 bytes of
   module images (estimate).** The commonest gap between two CSDO sites is
   `mov reg, [kernel_var]` - a read through the kernel's DS - which a run
   cannot hold. Reading every kernel word into registers FIRST and then
   storing them in one run (what TAKEN 2 did in `hbm_xwrite`) folds most of
   them, ~5-7 bytes a site, where a register is free. Outside a `%ifdef
   KERN_MEGA` arm it changes kern_big's code, so each is a measured change on
   every kernel. Seen: `stm_layout`'s `stm_sflag` chain, `stm_open`'s
   `stm_hole`/`stm_clipn`, `hbm_dosrun`'s KDL_* fields (two dozen of them,
   the largest single cluster), `ascw_flush_x`'s `asw_udrv`/`asw_ucwd`.
2. **`PUSH_CSDATA` through a door: ~-8 bytes of DISKOPS.DRV (estimate)**, 12
   sites at `push cs / call KERNEL_SEG:mega_csdata` (6 bytes): `push cs /
   call door` (4) with a 16-byte door that copies the pushed word under a far
   call to `mega_csdata` and back. Same packing problem as REFUSED 1, so
   likely negative on the file. Not built.
3. **KSEG with src = AX and AX dead after it** (`compress.inc`'s 38 `.modl`
   sites): the site's `push ax / pop ax` could go, -2 a site where it
   applies (~10 sites, ~-20 bytes, estimate) - each needs AX proved dead,
   which is the per-site reading this pass did not spend.
4. **The doors again if the packer learns them**: an LZB/LZ4 that saw `E8
   rel16` as an absolute target (a call filter, what x86 packers do) would
   make REFUSED 1's doors pay on the file too. A format change -
   WANTS AN ABI BREAK 2.
5. **DESK.DRV and EXTD.DRV**: 0 and 2 far calls left; nothing to take.

## WANTS THE OWNER

1. **Module images or packed files** (REFUSED 1): doors in every module would
   give a further -611 bytes of images (heap while loaded) for +433 bytes of
   packed files on the system disk and ~50 cycles a remaining site. This pass
   took the files' side, i.e. no doors in packed modules.

## WANTS AN ABI BREAK

1. **A module header word for the image's own DATA selector**: `mod_check`
   (or `mega_mods_fix` for the boot's) stores the selector `mega_dsimg`
   computes into a fixed header word, and CSDO becomes `push ds / mov ds,
   [cs:MOD_H_DSEL] / insn / pop ds` - a read through CS, legal on a code
   alias. Same 7 bytes a site and **~5x faster** (one DS load, no far call),
   and a run's head is 6 bytes either way - so bytes ~0, cycles at every
   site. Breaks: the module header (`MOD_VER`, `tools/os88mod.py`'s
   validator, every module rebuilt - they are assembled with the kernel, so
   no shipped package is touched). A module never moves (its base is its
   CS), so the word never goes stale.
2. **A call filter in the packed formats** (DEFERRED 4): `E8 rel16` stored
   as an absolute target before LZ, as x86 packers do, so near calls repeat
   and pack. Breaks the 'CZ' container's decoder on every kernel and every
   packed file (SPEC.md 20.13); buys an unknown share of every packed
   package's and driver's call-heavy code, not just these doors.

## CROSS-FILE

* **pmcore** (`mega.inc` 4712-4760): `mega_dsimg` and `mega_csdata` are ~70
  bytes of conventional `.text`, and every one of their callers is already a
  FAR call (273 CSDO far calls, the PUSH_CSDATA sites, and CTRL.DRV's door),
  so they could live in `.mega`/`.cold` with only the segment named changing
  - est. -70 `.text` (conventional), +70 above 1MB. What has to move with
  them is the real-mode test: they ask `[cs:k_dsel]`, which a `.cold` body
  cannot, and `[ss:mega_pm]` is right only if every caller's SS is LOW_SEG
  (the boot overlay and any module path reached on a foreign stack need
  checking first).

## ROWS TO RUN

Named, not run (BRIEF.md 2). kern_big, kern_small and kern_emu are
byte-identical, so no row on them can see this; the kern_mega rows about each
module:

* CTRL.DRV (the swap door, every CTRL CSDO site): `mega-cpnames`,
  `mega-cpup`, `mega-btncp`, `mega-desksc`, `mega-dockmodule`,
  `mega-dockpos`, `mega-vesashut` (shutdown.inc), `mega-deskdock`.
* DISKOPS.DRV (the compressor and the cloner): `mega-lzcomp` (File >
  Compress byte for byte against the host's LZB - the parse's runs),
  `mega-czjoin`, `mega-czto`, `mega-diskclone`, `mega-modstr`; suite
  `megacz`, `megamod`.
* FORMAT.DRV (`ascw.inc`): `mega-assocflush`, `mega-assocfind`,
  `mega-assocopen`, `mega-assocwake`, `mega-modstr`.
* STIKMENU.DRV: `mega-stikmenu`, `mega-stikmiss`, `mega-stikmove`,
  `mega-stiknodesk`, `mega-stikhiber`.
* HIBER.DRV (the DOS trip and `hbm_xwrite`): suite `megahiber`,
  `megahibfull`, `megap2hiber`, `megakbtrip`, `megadosp`; `mega-doscom`,
  `mega-dosexe`, `mega-dosfile`.
* **The 286 reset route, on 86Box** (the coordinator's run, BRIEF.md 1's
  update): `mega-dosirq` (`OS88_MEGA_EMU=86box`, the DOS trip through
  HIBER.DRV on the 286's reset) and suite `megarmb286` (86Box's 286-victory).
* WIN95.DRV: `mega-thm95`, `mega-thm95/86box`.
* DOCK.DRV / EXTD.DRV: `mega-dockmark`, `mega-dockpos`.
* SERIAL.DRV: suite `megarmbref` (a real lease on COM1 on kern_mega).

## DEFECTS FOUND

1. **`compress.inc:631` `CSDO pop word [CSEG cmz_tsh + 2]` pops the DS
   CSDO just pushed** (into the variable) and then pops the intended word
   into DS - on kern_mega. It is LATENT: the line is inside `%ifdef
   ROM_ADOPT`, which `kernel.asm:989` defines only when KERN_MEGA is not, so
   no kern_mega build assembles it. Left as it is (fixing it changes kern_big,
   which ships ROM_ADOPT); a run would refuse the pattern.
2. `docs/INDEX.md` was stale at the base (the brief's own `BRIEF.md`), so
   `make`'s doc gate failed on a fresh tree; regenerated in the first commit,
   as msp1/shared found at its base.

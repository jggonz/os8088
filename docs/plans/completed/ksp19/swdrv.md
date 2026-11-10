# ksp19 `swdrv` - wave C sweep: drivers, FSX, scheduler, sound, desk, files, kern_mega

Kernel size pass 19 (the flag day), wave C, agent `swdrv`, branch
`ksp19-swdrv` cut from `kernel-size-19` at `f49e64a3`. Non-ABI. Files:
`driver.inc`, `drvvol.inc`, `fsx.inc`, `sched.inc`, `snd.inc`, `desk.inc`,
`desksc.inc`, `files.inc`, `fdlg.inc`, `mega.inc`. Every figure below is
MEASURED: `tools/kernsize.py --json` run in a worktree of the base and in
this one against the same `build/`, `build/smallk`, `build/emuk`,
`build/megak`; routine spans off `tools/os88rdiff.py kernel f49e64a3 .`.

## Base and tip (resident = text+bss+cold+lowbss+vgabuf+vga+mega)

| kernel | base | tip | delta | where |
|---|---:|---:|---:|---|
| kern_big | 90,081 | **90,076** | **-5** | `.text` -5 |
| kern_small | 56,257 | **56,256** | **-1** | `.text` -1 |
| kern_emu | 90,327 | **90,322** | **-5** | `.text` -5 |
| kern_mega | 129,422 | **129,018** | **-404** | `.text` -5 (conventional), `.cold` -95 and `.mega` -304 above 1MB |

No package, driver or module byte moved. No ABI change; `abi.lock.json`
untouched.

## TAKEN

**kern_mega: the KB DMA doors' pool-head machinery, dead since C1**
(memdoors' WANTS THE OWNER item; nogrow names it). Nothing set `MX_FDMA`
once C1 withdrew the KB doors: `mega_claimdf` had no caller, and
`mx_dmah` was written only by `mega_dmahset`, whose two callers were
`mega_claimdf` and `mega_memregf`'s grow (which passed `MX_AUX` only for an
`MX_FDMA` block, so 0). Removed: `mega_claimdf` and `mega_claimhf`'s head
arm (`jcxz`/driver test/`mega_dmahset` call), `mega_dmahset`,
`mega_dmamovf` and its call in `osapi_mem_movable_x` (memory.inc, CROSS-FILE:
`jnc .seg` not taken already leaves CF = 1, its whole answer), `mx_alloc`'s
floor-up `.bot` arm, its `MX_FDMA` stamp and its `mx_dmah` clear,
`mx_dmafit` and its calls in `mx_compactf` and `mx_packupf` (in the latter
the following `cmp bp, cx / je` went too: the `jbe .ceil` above it already
proves BP > CX), the regrow's head carry, `mx_dmah` and `MX_FDMA`.
`snd_ringhif` (snd.inc) reduces to "base above 1MB: refused" - its pool
walk could only ever refuse once no block is `MX_FDMA`. Behaviour
identical on every path. SPEC.md 34 (the router's gate), 50.3, 50.3.7 and
50.6.10 say what the arm was and that it went.

**kern_mega byte idioms** (all `cpu 286` code; protected mode only, or
`mega_pmpre`, an IRQ reflection's): seven `clc` after an `or`/`xor`/`add`
that already clears CF (in or near `mega_hugef`, `mega_hugek`,
`mega_ldt2ds`, `mega_ownerof`, `mega_imghuge`, `mega_imghif`,
`mega_blkof`; an `add ax, 8` to a code selector cannot carry, its data
twin being a descriptor that exists), `mx_dfree`'s caller falling into it instead of
`call/ret` (-4), `adc si, dx / or si, si / jns` (the `or` dropped,
`.mega`), `shl bx,1 x2` -> `shl bx, 2` (`mega_pmpre`, conventional
`.text` -1), `rol al,1 x2` -> `rol al, 2` (`mega_fdgo`), and in fsx.inc's
286 bodies `mov cl,5 / shr bx,cl` -> `shr bx, 5` (twice) and
`ror bl,1 x2` -> `ror bl, 2` (`rms_xfer`).

**`osapi_snd_stream` and its real-mode twin `rms_sstr`**: `cmp al,0 / je /
cmp al,4 / jne` -> `test al, ~4 & 0xFF / jnz` (ZF = 1 for verbs 0 and 4
alone; -4 each, kern_big/emu `.text` and kern_mega `.text` + `.mega`).
Faster on every verb (one compare and one branch fewer; ~-8 to -16 clocks),
not a hot path.

**`fsx_caps`'s `clc`** (-1, every kernel): `shl bx, 1` of a kind 0..4
already leaves CF = 0.

**Parity**: `t_hotpar` green on kern_big and kern_small. The shift runs
start at `osapi_snd_tone` (kern_big, -4) and `fsx_capstab` (-5 / -1), and
no pinned hot routine sits after either.

## REFUSED

- **`push PKG_DISP`** in kern_mega's VESA PM hand-off (-2): AH at the
  driver's `VXV_PM` would change from 0 to the selector's high byte.
- **`jcxz`** for two `or cx,cx / jz` in `rms_rseq`: out of short range.
- **The single-caller routines in these files** (`fm_relayout`,
  `fdlg_bank`, `fm_cfill`, `fm_pen8_di`, `desk_gfill`, `fm_stat_row` ...):
  each is a fall-through head shared with a sibling or a tail into one, so
  writing it at its call nets 0 - passes 16-18 already took the ones that
  paid.

## WANTS THE OWNER / WANTS AN ABI BREAK

None. **The RCS door for `call COLD_SEG:x`** (door.md's idea) is not taken:
it needs new `rd_tab` entries, which `t5` owns in this wave. Counted for
whoever takes it after the renumber: 395 `call COLD_SEG:` sites; the most
called targets are `wm_cbd` (20, but takes BP: not eligible),
`mmf_mem_free` (16), `mega_s2segc` (14, kern_mega), `dwf_dskw_stat` (14),
`dskf_dsk_goto` (12), `dskf_dsk_vol_row` (10), `drvf_drv_vol_back` (10).
-2 a site, +2 a target, plus the RCS kind's first body (~15) on each
kernel; +~190 clocks a call, so the disk-goto and stat sites want their
cycles judged per site.

## ROWS (the final pass's union)

kern_mega's pool and the router's gate: `megajw`, `megaxmove`, `megahigh`,
`heaphi`, `megaown`, `megarmb*`, `megapxhuge`, `megapxhugez`, `megadosdir`
(the DOS park packs the pool), `megahddtick`, `megacdrom`, `megahiber`,
`megahibfull`, `pxsmega`, `vidrmbems`, `midirack` / `waverackrec` on
kern_mega (external rings); the FSX real-mode bracket (`rms_*`, the tick
read): the `tests/megaport.txt` FSXF_REAL rows; the stream verbs:
`mixervga`, `mixersb16`, `vidsound` family, `recorder`; `fsx_caps`:
`fsxdisp`, `vidxdisp`, `excitebikeega`.

**`megajw` NEEDS UPDATING, and was already red at the base**: its check 1
asserts the Audio Player's look-ahead is an `MX_FDMA | MX_FPIN` POOL block,
which no kern_mega has made since C1 (the head claim is conventional now).
Its own text says so; the fix is the coordinator's or the row owner's
(drop the pool-pin half, keep the conventional ring).

Ran here: `make` (fast tier 78 passed, 1 skipped, `hotpar` both kernels,
`stkbalance` - the same four declared paths as the base), `make small`,
`make emu`, `make mega`; no emulator row.

# Kernel size pass 19 - `memdoors`: the memory and file doors

Rows (BRIEF.md section 1): **BR-105**, the kernel's own callers of the KB
door bodies, **C1** (the six KB memory doors), **C2** (`OSAPI_FILE_APPEND`),
**C4'** (`OSAPI_FILE_APPEND_SYS` alone), **R1** (`OSAPI_PKG_REHOME`'s bytes
form). R13 not taken (the Task Manager was not touched). Branch
`ksp19-memdoors`, cut from `174b80bf`.

## BASE AND TIP

`tools/kernsize.py --json` on each tree, resident = `.text` + `.bss` +
`.cold` + `.lowbss` + `.vgabuf` + `.vga` (+ `.mega`). Base is BRIEF.md 0.2.

| kernel | `.text` | `.bss` | `.cold` | resident base | resident tip | delta |
|---|---:|---:|---:|---:|---:|---:|
| kern_big | 39,976 -> 39,912 (-64) | 5,378 -> 5,376 (-2) | 37,821 -> 37,746 (-75) | 90,899 | 90,758 | **-141** |
| kern_small | 29,680 -> 29,636 (-44) | 2,948 (0) | 21,037 -> 20,984 (-53) | 56,533 | 56,436 | **-97** |
| kern_emu | 40,222 -> 40,158 (-64) | 5,378 -> 5,376 (-2) | 37,821 -> 37,746 (-75) | 91,145 | 91,004 | **-141** |
| kern_mega | 45,728 -> 45,652 (-76) | 5,416 -> 5,414 (-2) | 48,583 -> 48,456 (-127, above 1MB) | 130,367 | 130,162 | **-78 conventional** (-205 all) |

No rung moved on kern_big or kern_small (bytes, not rungs: the figures above
are the spans). kern_emu's image rung uncrossed (90 -> 89 steps).

## TAKEN

Per row, kern_big, off `tools/os88rdiff.py kernel 174b80bf <tip>` (routine
spans) and the cell count:

| row | `.text` | `.bss` | `.cold` | kern_big | kern_small | kern_mega conv. |
|---|---:|---:|---:|---:|---:|---:|
| **C1** six cells; `mmf_osapi_mem_claim` 4, `_claim_dma` 18, `_claim_hi` 5, `_claim_dma_hi` 12, `osapi_mem_regrow_x` 11 (kept on kern_mega for the pool), `mem_claim_dma_x` 19, `mem_claim_dma_hi_x` 7; `mmf_mem_claim_own` +18; `fcp_claim` +4 | -36 | 0 | -54 | **-90** | -92 (FILECP.DRV +4) | -36 |
| **C2** the cell (kern_small: its write-at stub answers the append, +3) | -6 | 0 | 0 | **-6** | -3 | -6 |
| **C4'** the JCELL 3, `api_file_append_sys` 9, the setter 7, `api_sysfp` 4, a direct far call +1; `dwf_dskw_append_sys` 5 | -22 | 0 | -5 | **-27** | -3 | ~-34 (its kern_mega setters are longer) |
| **R1** `ld_rehsz` 2, `ld_start`'s bytes arm 13, the cell's store 3 | 0 | -2 | -16 | **-18** | 0 (no re-home there) | -2 |
| parity pad (kern_small, t_hotpar: the table's 45 is odd) | | | | 0 | +1 | 0 |
| **total** | -64 | -2 | -75 | **-141** | **-97** | **-78** |

Against BRIEF/flagday's estimates: C1 was -69 under T5 / -93 without it (we
are pre-T5: -90); C4' -17 measured there, -27 here because `api_sysfp` and its
setter go too once there is one verb; R1 -16 estimated, -18.

- **BR-105** - fixed by DELETION on both kernels. The KB head shift lived in
  `mem_claim_dma_x`, whose only live callers were the package DMA doors (now
  gone) and FILECP's copy buffer, which now claims in paragraphs through
  `mem_claimp_dma_x` (head bounded by `mem_claim_1`'s `cmp si, 0x1000`). The
  kern_dos twin (`kerndos/kdshim.inc` `mem_claim_x`, a `jc` after a 6-bit
  shift): its one caller (`kdgate.inc`) claims `KD_GATE_KB * 64` paragraphs
  through `mem_claimp_x`, and the KB entry is deleted (-7 kern_dos).
- **The kernel's own callers of the door bodies**, moved: FILECP
  (paragraphs), kern_mega's bridge (`mmf_mem_claim_ownhi`), HUGE and the
  regrow move (`mmf_mem_claim_own`), DISKOPS's clone buffer (it far-called
  the AVAIL *cell*; now `COLD_SEG:mmf_osapi_mem_avail`), kern_dos's gate.
  **Not moved, on purpose**: the ~20 internal callers of `mem_claim_x`,
  `mem_regrow`, `mem_avail_x`/`_lvl_x` (caches, band, menus, clip, icons,
  hibernate). `OSAPI_MEM_HUGE` is a KB cell and is served by `mem_claim_x`'s
  KB arm, so that arm stays whatever they do; converting them is neutral
  (each computes a KB count today and would shift by 4 instead of 10), as
  flagday.md predicted. Recorded, not taken.
- **C1, the package side.** About 300 call sites converted. Packages call the
  `MEMKB_*` macros (the old fifty-dot-three-dot-seven-dot-one, removed by `kbpara`), the withdrawn doors' registers written
  once per package as near procs over `OSAPI_MEM_P`, assembled by
  `OS88KB_BODIES` at `OS88_IMAGE_END` (or `CC_IMAGE_END`, or by hand in a
  part) and only for the entries the package named. Every helper claim is
  the DISK class (what the KB doors gave; the CPU class is a per-site sweep's
  business). Converted INLINE, registers read: the nine driver sites
  (HDD.DRV x4 - kept at 4,096 bytes by a `mov al, 64` and a one-paragraph
  head that is also the read count, RAMDISK x3, SOUND, XMEM), WIN16's loader
  part (SPEC.md 124.19.3 forbids it the near call), WireFleet (its image + bss
  was at APP_MAX_SIZE), PiXEL's GIF part, Telnet, Skies, StikNote. The C SDK's
  thunks call `OSAPI_MEM_P` directly (`os88_mem_claim`, `os88_mem_regrow`,
  the KB avail answers; same C signatures).
- **C2** - an append is `OSAPI_FILE_WRITE_AT` with `DX = 0xFFFF` (the body
  already took that sentinel). 12 sites + the C thunk; DOS's backend keeps
  `dos_k_append` as a 5-byte DX-saving wrapper, and its WRITE_SEQ fallback
  jumps to it (-3 there).
- **C4'** - as the table says; `api_file_write_sys` is a direct
  `call COLD_SEG:dwf_dskw_write_sys` now.
- **R1** - the cell no longer reads AX; `OS88_REHOME` lost its `xor ax, ax`
  (-2 in every re-homing loader: csload, dosload's doswin, scload, wdload,
  word.p1).
- **The table renumbered**: 45 bytes from 0x01A4 up. Deleted cells (old
  address:length) **01A4:6 01B0:6 01CD:6 01DF:6 0286:6 02C2:3 03AC:6 03B2:6**;
  every old slot `a` above them is `a - (lengths deleted below a)`. Applied to
  `apps/os88api.inc`, `drivers/os88drv.inc` (`OSAPI_DRV_TASK` 0x01D9 ->
  0x01C7), every `slot 0x...`/`API 0x...` citation (checkdocs), the kernel
  table's comments, and the tests that hardcode a cell (`os88huge.py`,
  `deskcalremind.py`, `colorpresets_qemu.py`, `picomem.py`). The script that
  did it is `memdoors/renum.py` in the session scratchpad: give it the old
  SDK and the deleted list. **The coordinator will meet every other wave-A
  row's deletions in the same numbers**; recompute from the merged kernel
  rather than resolving the SDK hunks by hand.

### Packages

Sum of image sizes, the kern_big set (`build/*.bin` at the top level): **+1,055
bytes over 54 images** of 2.79MB (-2 in five loaders, +3..+54 elsewhere; the
largest Video +54, XTMP3 +50, PiXEL +41, Browser +41). The helper body is 20
bytes for the claim and 5..21 per further entry; a site is 2 bytes smaller
than the far call it replaced. So a package with one or two sites pays ~+18,
one with many pays nothing extra per site. C packages: a few bytes per thunk
used (+8 claim, +6 regrow, +6/+8 avail, +3 append).

**Helper or inline - the brief's question, answered per package, measured.**
An exact inline conversion (BX, CX and AX kept) is +11..+13 a site; a minimal
one (+4) needs every path after the call proved to leave BX, CX and AX dead,
which an automatic liveness pass over the tree could decide for about one
site in ten (calls, rets and macros end it). At +1,055 the helper sits where
flagday.md priced the INLINED conversion (+0.9 to +1.7KB), not at its +0..150
helper estimate - that figure assumed a body shared ACROSS packages, which no
package can far-call. Single-site packages were inlined where reading the
site was cheap (Telnet, Skies, StikNote, PiXEL's GIF); the rest is a wave-C
sweep, one package at a time: inline a site whose registers are proved dead,
and drop the body when the package's last site goes.

## REFUSED

- **Converting the remaining internal KB callers** - neutral (above).
- **ftpd's two sites, REDLINE's avail, os88type.inc's two, os88xgap's one
  inline** - each nets -4..+2 against the body once BX/CX are banked; left on
  the helper.

## WANTS THE OWNER

- **kern_mega: an OS88_HIGHMEM package's DMA HEAD claim is conventional
  now.** The KB DMA doors gave it a pool block (`mega_claimdf`, KERN-MEGA-PLAN
  14.160); `OSAPI_MEM_P` keeps a head conventional by contract (SPEC.md
  50.3.7). The only such site in the tree is WIN16's `w16mm.inc` ring (one
  ring, `W16MM_IRL`). `mega_claimdf` and the MX_FDMA head machinery behind it
  (`mega_dmahset`, `mx_dmafit`, `mega_dmamovf`'s head arm) are now DEAD on
  kern_mega, all `.cold`/`.mega` above 1MB: a removal is mega.inc's owner's
  (an estimated -60..-120 above 1MB, nothing conventional), or a MEM_P verb
  bit restores the pool head. Recorded, not decided.

## WANTS AN ABI BREAK (found, not taken)

- `OSAPI_MEM_COMPACT`'s `MEMC_WHATIF` answers in KB beside `MEMC_WHATIF_P`
  in paragraphs: one unit would drop a shift (~6 `.cold`) and convert ten
  sites. Small; recorded.
- `OSAPI_MEM_HUGE` keeps the kernel's KB claim arm alive (its AX is KB).

## CROSS-FILE

`kernel/kernel.asm` (table, `api_file_write_sys`, the size assert, kern_small
parity pad), `kernel/memory.inc`, `diskw.inc`, `loader.inc`, `filecp.inc`,
`mega.inc` (the bridge's one call), `diskops.inc` (one call), `kerndos/`;
`apps/os88api.inc` (memory section, MEM_P, WRITE_AT, REHOME, the macros),
`drivers/os88drv.inc`, `apps/cc/os88thunk.asm`, `apps/cc/crt0.asm`
(CC_IMAGE_END), `tools/os88index.py`, `tools/dsegaudit.py`, the abilock,
and ~150 package sources (call sites). Gates taught: `t_pins` (MEMKB claim
macros are claims), `t_drvclaim` (the door is OSAPI_MEM_P), `t_w16cens`,
`t_api_abi` (APPEND's alias); `tests/pins.txt` lost RAMDISK's `rd_claimp`
X line (it declares now). Rows reworked to the one cell: `isotronsmall`
(traces OSAPI_MEM_P by verb), `pxanimemu` (answers MEM_P's claim).

## ROWS RUN

Host: `make` (fast tier) on kern_big, `make small` (t_hotpar both kernels),
`make emu`, `make mega`, the C packages (`make cword runcpm c64 apple2
paccman lemmings weave loom`), and the full tier's host rows `pins`,
`megahigh`, `sysfree`, `noovl`, `guidelock-full`, `ctoolchain` - all pass.
Emulator: `memp` (OSAPI_MEM_P's verbs and the KB claim at 4KB, now through
MEMKB) - ok.

## ROWS (for the final union)

Every package that claims memory rebuilt, so the claim-heavy rows first:
memp regrowshed heapgran drvmem pins heapcheck heapmap heapdrv msegnomem
clipgrow pkgmove-* dosmem os88huge megahigh megahuge megapxhuge megarmbref
heaphi dirwshed ddsmall megamod mseglzslack trkcompact trkbigmod cmemmove;
FILECP: fcpcopy fcpapi fcproom fcpsmall lzfence; the append: dosfile wseq
ftpd telzm thewire viddisk; R1: rehome; drivers: hddgeom megahddtick
rdpreserve sndcap sndmove drvclaim installabi; reworked rows: isotronsmall
pxanimemu w16cens w16xsec; renumbered constants: deskcalremind picomem
colorpresets_qemu os88huge; the inlined packages: stiknote skies fontview
pxsmega recorder; and api-abi mirror hotpar.

## For the next wave

- `t5` renumbers again: the deleted list above is the only hand-made shift
  in this branch; `tools/os88wire.py`'s `WIRE_CELL_LEVELS`/`WIRE_TABLE_END`
  still hold the OLD numbers (they name past releases' tables, which the
  format bump makes unloadable anyway) and are t5's.
- The MEMKB call sites are `call memkb_*`, near - T5's door changes nothing
  for them; only the one `call OSAPI_MEM_P` per package body is a rare cell.
- A size sweep (wave C) can take the helper sites one package at a time.

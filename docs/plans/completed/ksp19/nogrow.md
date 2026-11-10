# Kernel size pass 19 - `nogrow`: the merged tip against BRIEF 0.0

Branch `ksp19-nogrow`, cut from `kernel-size-19` at `44880615` (wave A
merged, the brief's 0.0 added). The job: make the tip obey 0.0 - *no row grows
bytes* - by reverting what grew, reworking what could be made net-negative,
and pricing the rest. ABI-BREAK-LIST section 14 is the record.

## How it was measured

* Kernels: `tools/kernsize.py --json` on all four trees (`build`,
  `build/smallk`, `build/emuk`, `build/megak`) against the same at the tip.
  Resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`
  (+ `.mega`).
* Packages: the sum of the IMAGE of every `.o88` and `.drv` at the top of
  `build/` (97 images: packages, tests built as packages, drivers, kernel
  modules and the eight C packages), each through `os88pkg.image_unwrap` /
  `os88drv.image_unwrap`; a parted package is its image plus every part's
  unpacked `len` (the FILE of an uncompressed parted package carries
  compressed parts, whose size moves with any byte - Skies read +32 for a
  -2). Three trees built: before memdoors (`a6fc912a`), the tip
  (`44880615`), and this branch. The script is the session scratchpad's
  `pkgsum.py`; it is forty lines and its rules are the sentence above.

## Per row, against the tip (`44880615`)

| row | kern_big | kern_small | kern_emu | kern_mega | packages |
|---|---:|---:|---:|---:|---:|
| R2 reverted | -6 `.cold` | -6 | -6 | -6 (above 1MB) | -14 |
| snapshots' AX answers withdrawn | -6 `.cold` | -6 | -6 | -6 (above 1MB) | +1 (megaown) |
| R12 reverted (WireFleet fix kept) | +5 `.text` | +2 | +5 | +5 | -78, EXTD.DRV +1 |
| `fsx_caps`/EXTF_CAPS BH test | -1 `.text` | 0 | -1 | -1 | EXTD.DRV -1 |
| R2-3 reverted (`MEGA_WTN` 32) | - | - | - | -128 tables' block above 1MB | - |
| C1's KB arm (`mem_p_kb`) | +50 `.cold` | +50 | +50 | +51 (above 1MB) | **-707** |
| **branch total** | **+42** (`.text` +4, `.cold` +38) | **+40** (+2, +38) | **+42** | **+43** (conventional **+4**) | **-786** |

The R2+R12 commit alone measured -8/-10/-8/-8 resident and -79 packages.
The kernel figure grows on the branch only because of C1's arm, which is
C1's row, not a row of its own (below).

## TAKEN / REVERTED

**R2 - reverted** to the old `SYS_SNAPSHOT`/`CLAIM_SNAPSHOT` contract (no CX
input; Task Manager, RedLine, WIN16, WaveRack, Audio, Sticky Notes, megaown,
SPEC.md 11.102, 20.9, 20.3's list). recon's DX clobber was part of R2's form
and went with it; `t_hotpar` is green without it at this base. **Kept as a
saving instead**: `SYS_SNAPSHOT`'s AX = MAX_TASKS (no reader; `SS_NTASK`
carries it) and `CLAIM_SNAPSHOT`'s AX = MEM_MAX (megaown alone read it) are
withdrawn, -3 each on every kernel, -6 together so parity holds. R2 is
recorded as deferred to the additions pass with its price.

**R12 - reverted** (the BX = 1..15 overload back, 39 `mov ch, 0` sites out,
the walkers back on BX). No net-negative form exists without proving CH = 0
at each window-asking site; the inverted contracts (CH = FFh, a BH index)
still cost the walkers or the askers an instruction, and the askers are 39 of
44. WireFleet's fix stays (-5). In its place, a no-ABI byte: `fsx_caps` and
EXTD.DRV's half test `or bh, bh` instead of `cmp bx, 16`.

**R2-3 - reverted** (`MEGA_WTN` 64 -> 32): 128 bytes of pool for no saving.

**C1 - reworked, not reverted** (the coordinator's). memdoors' MEMKB bodies
did the KB shift and banked three registers in every package (20 bytes for a
claim, 41-62 for a package with four entries, ~1,600 bytes of bodies in all).
`OSAPI_MEM_P` now takes `MEMP_KB` (8) added to a verb as the withdrawn doors'
registers - AX in KB and kept, CX kept, every register but DX and BL kept, no
head read - with `MEMP_KBH` (80h, tested by the verb byte's sign, which is the
byte that kept `t_hotpar`'s parity) for a head in KB, and `MEMP_KB +
MEMP_AVAIL` answering in KB through `mmf_osapi_mem_avail` (the old AVAIL
door's body, already resident for DISKOPS). The arm is a top-level
`mem_p_kb` that far-calls the paragraph door (`push word [api_coldseg]` /
`call osapi_mem_p.p`: `push cs` is refused in `.cold` by ovlchk and
`COLD_SEG` by os88cfx; kern_mega names its selector), marked
`STKBALANCE-OK`. Each MEMKB entry is now `push bx` / `mov bl` / `jmp` (5),
the tail 7, `memkb_avail` 8. The C SDK thunks (`os88_mem_claim`,
`_regrow`, the KB avail answers) and the inline sites that were shifting
by hand (RAMDISK's avail, Sticky Notes, Skies' shadow, WaveRack's I/O claim,
the Video Player's PKG_MEGA ring) use the arm. Not converted: HDD.DRV (its
image is held at 4,096 by layout), SysCheck (a head that may exceed a byte of
KB), XTMP3 (a paragraph sum). One thing the arm does NOT do, as memdoors'
bodies did not: refuse a KB count of 1,024 or more before the shift.

### C1's total, which is still positive

| | kern_big | kern_small | kern_emu | kern_mega | packages | total |
|---|---:|---:|---:|---:|---:|---:|
| memdoors' C1 | -90 | -92 (+4 FILECP.DRV) | -90 | -90 (-36 conv.) | +1,037 (with C2/C4'/R1's few) | **about +680** |
| with the arm | -40 | -38 | -40 | -39 (-36 conv.) | about +330 | **about +170** |

So C1 is still about **+170 net** before T5, and about **+770 under T5**: a
withdrawn KB door would have been a rare cell, a 3-byte `CD 81 nn` site, so
the 2 bytes a site the near helper call buys today vanish, while every
package still carries a body (10 bytes at least for a one-entry package). The
cheapest C1 under T5 is no C1: the six KB doors as door indices (2 bytes of
`rd_tab` each) over bodies the kernel already has (`mmf_mem_claim_own`,
`mem_claim_hi_x`, `mmf_osapi_mem_avail`, the regrow). **For the owner**; not
reverted here, as instructed.

## Other rows' growth, classified

| item | where | class |
|---|---|---|
| door: kern_small `.text` +19 | the door body against the shims | part of a net-negative row (kern_small resident -27): keep |
| door: kern_mega conventional +1, `.mega` +16 | `mov ds,[cs:k_dsel]`; the bracket's bound writes | same row, kern_mega resident -59: keep |
| door: FLAGS restore (+3 over `sti`) | `push [bp+6] / popf` | correctness the IF audit requires (three deliberate IF = 0 callers); inside the net-negative door: keep |
| drvtab: EMS.DRV image +34 | a real 36-byte service table of zeros | bought `.text` -9 kern_big/emu/mega and kern_mega's `fsx_rdrvf` -9 (about -36 against +34, same 1KB claim): keep |
| drvtab: R2-3 `MEGA_WTN` +128 above 1MB | contract number | growth with no saving: **reverted** |
| recon: C10's CTRL.DRV `.modc` +17 | the remove-by-record matcher | inside a row of resident -7 kern_big/emu/mega, WIN16's 16-zone loop and every Sticky App -1: keep |
| memdoors: C2's kern_small +3 stub | WRITE_AT's append answer | C2 is -3 on kern_small net: keep |
| memdoors: FILECP.DRV +4 | paragraph claim | inside C1 |
| l1, gates | no resident growth | - |

## Checks

`make` (fast tier 78 passed, 1 skipped, incl. `hotpar`, `stkbalance`,
`stkapps`, `mirror`, `abilock`, `api-abi`), `make small`, `make emu`,
`make mega`, the C packages (`make cword runcpm c64 apple2 paccman lemmings
weave loom`) - all green; `tools/checkdocs.py` 0 problems;
`abilock.py --update` three times without a bump. One emulator row, the one
ABOUT the arm: `memp` **ok** (OSAPI_MEM_P's verbs and a KB claim through
MEMKB).

## ROWS (for the final union)

* the KB arm (every MEMKB package, the C SDK): `memp`, `regrowshed`,
  `heapgran`, `drvmem`, `clipgrow`, `dosmem`, `megahigh`, `heaphi`,
  `ctoolchain`, `stiknote`, `stiknotevga`, `stiknotesmall`, `skies`,
  `waverackrec`, `rdpreserve`, `vidrmbems`, `pxsmega`, `recorder`,
  `fontview`;
* R2 back and the AX answers: `tmsmall`, `tmload`, `tmowner`, `tmmaxrun`,
  `tmgraph`, `megaown`, `redlineinspcga`, `redlineinspherc`,
  `redlineinspvga`, `waverackrec`, the WIN16 jump and Audio sibling rows;
* R12 back: `fsxdisp`, `vidxdisp`, `isotrontwodisp`, `dosconcga`,
  `excitebikeega`, `vesafsx`, `pxsmd`, `wirefleetsystem`, `wirefleetdeploy`,
  `fsxclip`, `extdmod`;
* R2-3: kern_mega's OSAPI_SEG rows (`megarmb*`, `megapxhuge*`).

# ksp12 agent "hma" - the HMA work (SPEC.md 2.10.7, 2.10.8, docs/plans/HMA-PLAN.md)

Kernel size pass 12. Concept: `6e5d2a1c` (kern_big runs `.cold` from the HMA,
the heap gets the cold rung), `12f48233` (HMA wave 2: SOUND, HDD and NET run
from the HMA, SOUND's double buffer, `OSAPI_MEM_HMA`), `2d5dbdf6` (HDD.DRV
from the HMA fixes) and `9690c3bb` (a knob kernel's `.cold` at 0x4000). None
of it is on `main`, so the `ROMF_H*` door between the kernel and COLDHMA.SYS's
tail was free to change, and did. `apps/os88api.inc`, `drivers/os88drv.inc`
and `tools/abi.lock.json` are untouched: no published interface moved and
`abilock` has nothing to record. Branch `ksp12-hma`, cut from the brief commit
`df8603a1`. Figures are ASSEMBLED bytes, `tools/kernsize.py --json`
absolutes; the base was measured on an exported copy of `df8603a1` against
the same generated includes, and its ROM and HMA arms built there.

## BASE AND TIP

| | base `df8603a1` | tip | delta |
|---|---:|---:|---:|
| kern_big `.text` / `.cold` | 43,738 / 38,690 | 43,736 / 38,598 | **-2 / -92** |
| kern_big `.bss` / `.lowbss` / `.vgabuf` | 5,212 / 5,598 / 336 | same | 0 |
| kern_big resident | 93,574 | 93,480 | **-94** |
| kern_small `.cold` (`.text`/`.bss`/`.lowbss` 31,967 / 3,063 / 2,868) | 22,515 | 22,513 | **-2** |
| kern_small resident | 60,413 | 60,411 | **-2** |
| kern_emu `.text` / `.cold` | 43,997 / 38,690 | 43,995 / 38,598 | **-2 / -92** |
| kern_emu resident | 93,833 | 93,739 | **-94** |
| `KERN_SIZE` big / small / emu | 99,328 / 62,464 / 99,840 | same | no rung moved |
| **the concept's own cost**: kern_big against `-DNO_HMA_DRV`, the A/B its bytes are priced against | `.text` +32, `.cold` +213 = **245** | `.text` +30, `.cold` +121 = **151** | **-94, 38%** |
| HIBER.DRV module image (`.modh`) | 5,529 (file 4,858) | 5,528 (4,857) | -1 |
| **HMA arm**: COLDHMA.SYS file / its resident half in the HMA | 2,920 / 2,056 | 2,900 / 2,036 | **-20 / -20** |
| HMA arm: `.cold` + tail against FFFF | 200 spare | 316 spare | +116 |
| HMA pool, FFFF:0010-5FFF | 24,560 | 24,560 | 0 (`HMA_COLD_AT` sets it) |
| **ROM arm**, `rom-big`: `.cold` in the 40KB window | 38,690, 778 spare | 38,598, 870 spare | **+92 spare** |
| ROM arm, `rom-small` (FDLG, FILECP, CTRL, FORMAT and TASKMGR beside it) | 22,515, 4,061 spare | 22,513, 4,063 spare | **+2 spare** |
| SOUND.DRV unpacked image (image + bss) | 7,183 (7,263) | 7,176 (7,256) | **-7**; its HMA block and its 8KB claim do not move (paragraph and KB rounding) |
| HDD.DRV unpacked image (image + bss) | 4,080 (4,096) | same | 0: the -5 of code went into the `align 512` pad in front of `hd_mbr` (486 bytes at base, 491 now) |
| NET.DRV / XMEM.DRV / HDDTOOL.DRV | 4,105 / 1,899 / 17,507 | same | 0 (untouched) |

`-DNO_HMA_DRV` still assembles and is byte-identical to the base's knob build
but for the build number, so the 245 -> 151 line is the concept's whole
`HMA_DRV` cost measured the same way on both sides.

## TAKEN

Seven commits. Bytes are kern_big unless named; nothing here is on a hot
path except `dsk_runcap` (per RUN of a transfer, not per sector), which got
12 clocks FASTER.

* **`drv_hma_door`: one guard for the tail's door** (+12, replacing three
  copies of `cmp word [api_coldseg], HMA_SEG / jne` + `call far [rom_mfp]`).
  `cmp` against FFFF is CF = 1 for every other segment, so the refusal needs
  no `stc`. drv_hma_try, drv_release and OSAPI_MEM_HMA call it.
* **ROMF_HPLACE takes no CX** (kernel -16, tail -2): the tail's table is
  keyed by a hash of the org-0 image, and the hash covers the header's bss
  byte, so the matching entry's image + bss IS the length to copy. The
  kernel's `[DRV_H_BSSP] * 16 + [DRV_H_IMG]` and its `push cx/pop cx` went.
* **drv_hma_try banks nothing** (-11): its one caller is drv_load_row, the
  body both load entries bank AX..BP and ES around, and nothing after the
  call reads AX, DX or ES; ES is ALREADY the claim (drv_load_row loads it for
  the read, and dskw_read_x, drv_check and drv_bss all keep it). `and word`,
  `add byte` (DRVR_ENT's low byte does not change) for the immediates. 67 ->
  32 bytes. **It stays a call**: written at its site it pushed six of
  drv_load_row's refusals out of short range, +18 against the 4 a call costs
  (measured, then reverted).
* **ROMF_HCLAIM with CX = 0 is the give-back**; 6 (HFREE) is gone. So
  OSAPI_MEM_HMA's two forms are one call: osapi_mem_hma_x 38 -> 18.
* **drv_publish writes a class pair's OFFSET half from the row's DRVR_DISP,
  ahead of the segment, for every class** (-5; EMS's door goes in the EMS
  row's DRVR_DISP, -1), so **drv_release no longer resets it** (-11). This is
  also the defect fix below. drv_release resets DRVR_DISP with
  `mov [bx+DRVR_DISP+1], al`, AL being the zero it just stored (-1).
* **ROMF_HDROP answers CF = 0 when the owner is itself a pool segment**, so
  drv_release's "is there a heap claim to free as well" is the door's carry:
  kernel -3, tail +3 (the tail is resident only on a machine with the HMA,
  where `.cold` is up there too).
* **mem_own: two HMA arms became one, on the way out of a refusal** (-15:
  23 -> 8). `drv_owns_seg` says yes for a segment drv_tab names or a claim
  one of them owns; every heap case of either has already said yes by the
  time mem_own refuses, so what it adds there is exactly the HMA driver and
  its second image (HDDTOOL.DRV), the two arms it replaces. Proved by cases
  in the comment, and both are reached by `hmadrv` (SOUND.DRV's own claims
  while playing, and the whole Install).
* **wm_create and mem_reloc_call build the dispatcher in a register**
  (`.text` -2, `.cold` -4): `mov ax, PKG_DISP / ... / mov ah, E0h`, with an
  assertion that PKG_DISP has no high byte.
* **dsk_runcap's HMA arm is out of line** (0 bytes): a heap buffer - every
  run on most machines - takes the `jae` untaken, 4 clocks where the `jb`
  taken was 16.
* **hbm_sweep** byte compare (HIBER.DRV -1).
* **kern_small's OSAPI_MEM_HMA is a label on driver.inc's shared `stc/retf`**
  (kern_small -2).
* **The tail** (HMA arm, all in `boot/osrom.asm`'s ROM_KIND_HMA code):
  HRESET zeroes the table in one `rep stosw` (-4); `hb_yesax` was a second
  copy of the epilogue `hb_yes` jumps to, and AX is in no frame slot (-10);
  hb_claim stores into the saved DX from AX with no redundant `ss:` (-3).
* **SOUND.DRV's sbl_dma_put asks the HMA first** (-7): OSAPI_MEM_HMA's
  give-back refuses anything that is not a block (a heap claim's segment is
  below F000, no block's ever is), and the heap's free is then the answer, so
  the `[sbl_sbase]` test goes.
* **HDD.DRV** (-5 of code, 0 of image): hd_raw's bounce stores `[hd_bofs]`
  from the DI it has just zeroed and drops a `cld` (DF = 0 is the system
  contract, SPEC.md 1 rule 3); hd_raw and hd_bios_run test the segment's
  high byte.

## REFUSED

* **drv_hma_try written at its one site**: +18 bytes of relaxed jumps for
  the 4 a call costs. Measured.
* **The tail updating the row itself** (xchg DRVR_SEG, DRVR_KB = 0,
  DRVR_DISP, DRVR_ENT, the claim back in DX): kernel -16 `.cold`, tail ~+22
  (ESTIMATED). On a machine with the HMA both are up there, so it is +6 there
  for -16 on every other kern_big machine and the ROM window - but it teaches
  the tail the DRVR_* layout, which `tools/os88rom.py` would have to export.
  Worth weighing in a pass that is breaking the HMA's contract anyway.
* **drv_pkg_disp's HMA arm** (`.text`, 21 bytes, on every call into a
  driver). The far target is DS:PKG_DISP or DS:E00C and nothing reachable
  without a register says which; every argument register is the driver's.
  A BP-banked patch of the pushed offset is 26 against 28 but puts a taken
  jump (+12 clocks) on the heap path. Dropping the `pushf/popf` is refused:
  drv_stamped hands a driver deterministic flags (CF = 0, ZF = 1 out of
  `snd_disp_set`), and a no-op verb that returns them would read as a
  refusal with CF = 1 from the compare.
* **drv_fs_has's header read** (+11): banking the dispatcher in drv_fs_seg
  so the header is `[es:bx+DRV_H_FSV-PKG_DISP]` is 10 against 15, but the bank
  is +6 of code and +2 of `.bss`: +3.
* **drv_blk_call_x dispatching through the class pair at ES:DI** (would have
  deleted `drv_blkfp`/`drv_blkseg` and the staging): DSV_GEOM, DSV_BLK's
  AL = 2, answers in DI (SPEC.md 51.8), so DI has to come back from the
  driver untouched.
* **dsk_runcap's arm folded into `.capped` with a skip byte**: -1 byte for a
  `db 3Dh` nobody should have to read.
* **`[gfx_bpfn]`'s two immediates as byte compares** (wave 1, vga12.inc,
  -2): needs an assertion that gfx_bppiece's low byte differs from
  gfx_blit1_x's, which any unrelated layout change trips one time in 256.
* **xm_hma** (XMEM.DRV, attach-only code under the scratch): the once-region
  is already padded to `XM_SCRATCH` with 130 bytes of zeros, so a byte off
  xm_hma is a byte of pad. 0 to gain.
* **Folding OSAPI_MEM_HMA into OSAPI_WM_QUIET** (the other unfrozen cell at
  the table's tail, -6 `.text` on both kernels): two subjects in one cell.

## WANTS AN ABI BREAK

* **OSAPI_MEM_HMA as an arm of OSAPI_MEM_CLAIM_DMA_HI and OSAPI_MEM_FREE**
  (both frozen on `main`): a DMA claim from a LOADED DRIVER tries the pool
  first and answers AX = its first offset (0 for a heap claim), and a free of
  a segment at or above F000 goes to the tail. The 6-byte cell goes on BOTH
  kernels (and kern_small's label), osapi_mem_hma_x's 18 bytes become ~10
  inside the existing bodies, and SOUND.DRV's two-step claim and two-step
  free collapse (~-12 of driver). ESTIMATED: ~-14 kern_big, -6 kern_small.
  What breaks: CLAIM_DMA_HI's AX out (it is not an output today) and every
  caller that assumes offset 0 - SOUND.DRV, which is the only DMA_HI caller
  that would be a driver.

## CROSS-FILE

* **rom** (`ae906414`'s lines, renamed by `6e5d2a1c`): the five
  `cmp word [api_coldseg], COLD_HOME` (memory.inc `mem_floor_ax` and
  `osapi_sys_kb_x`, mod.inc twice, ui.inc) can be `cmp byte [api_coldseg+1],
  COLD_HOME >> 8`, -1 each, behind an assertion that `ROM_COLDSEG >> 8` and
  `HMA_SEG >> 8` differ from it (EE/FF against a RAM rung's high byte on
  kern_big, F4 on kern_small).
* **coordinator**: my `.cold` deltas put the run `mem_owner_of_x ..
  drv_check` - dsk_xfer among it - at an ODD shift (-39; os88rdiff). It is
  irrelevant on the 8088 and the final parity is the merge's, not any one
  branch's: worth one `os88rdiff --hot dsk_xfer` after the merge.

## ROWS RUN

All on the box shared with seven agents, `--marty-jobs 1`.

* fast tier after every batch: 43/43.
* `make -j2`, `make -j2 small` at every batch; `make emu` and `make rom` at
  the tip.
* `tools/stkbalance.py` over every file touched, base against tip: 39
  unbalanced paths both, none new (one more "name defined twice": kern_small's
  `osapi_mem_hma_x` label beside kern_big's body).
* `hmacold` (QEMU): ok at every batch (4 runs).
* `drvmove`, `sndmove`, `rombig`, `ems`, `hibernatedrv`, `heapdrv`
  (MartyPC): ok, twice each (after batch 1 and at the tip).
* `hmadrv` (QEMU, 286): **fails at the base too, identically.** Its check
  *"its two halves are the demo's samples ... in the stream's order"* reads
  `[4096, -1]` in one or both arms on 4 of 6 base arms and 4 of 6 tip arms
  run alone (3 runs each, alternating), and every other check - placement,
  the pool, the org-E000 images, C: mounted from the HMA, HDDTOOL.DRV at
  E00Ch, the double buffer in the pool and given back, the whole Install, and
  the heap arm - passes on both. Run beside a MartyPC lane it also timed out
  opening C: (2 of 4 tip runs); alone it never did, on either side.

## DEFECTS FOUND

* **FIXED - an IRQ0 could reach an HMA driver at the BIOS ROM.**
  drv_publish wrote the class pair's SEGMENT and then, for a driver in the
  HMA, its offset - so between the two stores the pair read
  HMAseg:000Ch, linear F2xx0 + C, which is the ROM. The segment is what arms
  the pair (`drv_svc_call_x` tests `[drv_fseg]`), and snd_tick far-calls
  through it inside IRQ0 once `[snd_live]` is up: so ticking SOUND.DRV on in
  the Drivers page of a 286 with the HMA had a two-instruction window per
  load. Found by reading; no row can hit a window that narrow. The offset is
  now written first, for every class.
* **NOT FIXED, test**: `hmadrv`'s two-halves check (above) - a race in the
  ROW, which reads both halves of a short demo's double buffer while the
  refill worker and the card are both moving; it fails at the base.

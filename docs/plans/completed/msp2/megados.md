# msp2 agent "megados" - the DOS park, its stub and the streaming resume

kern_mega size pass 2 (`docs/plans/completed/msp2/BRIEF.md`). Scope:
`kernel/megados.inc`, `kernel/megapark.inc`, `kernel/megares.inc`. Branch
`msp2-megados`, base `ead16c61` (the brief), with `msp2-pmcore` merged in at
`77491951` at the coordinator's word so that `mega_pic_at` could be used.
Every figure is ASSEMBLED (`tools/kernsize.py --build build/megak --json`),
measured against a base tree built in a scratch worktree at the same build
number; the figures quoted as MINE exclude pmcore's merged work (they are
measured against the merge commit for the last batch).

## BASE AND TIP

| kern_mega | base `ead16c61` | tip (mine only) | delta |
|---|---:|---:|---:|
| `.text` | 46,579 | 46,445 | **-134** |
| `.bss` | 5,427 | 5,427 | 0 |
| `.lowbss` | 6,751 | 6,751 | 0 |
| `.vgabuf` | 336 | 336 | 0 |
| **conventional resident** | **59,093** | **58,959** | **-134** |
| `.cold` (above 1MB) | 46,165 | 46,165 | 0 |
| `.mega` (above 1MB) | 21,440 | 21,280 | **-160** |
| `.mega` scratch (MPS_SIZE) | 1,490 | 1,510 | +20 |
| `.boot2` (given back) | 6,709 | 6,695 | -14 |
| `.ovl` | 5,118 | 5,118 | 0 |
| `KERN_SIZE` at boot | 134,656 | 134,656 | 0 (no rung crossed) |
| footprint after the switch (`mres`) | 65,024 | 65,024 | 0 (no rung crossed) |
| `HIBER.DRV` image (unpacked) | 11,584 | 11,460 | **-124** |

With pmcore merged, the branch tip measures `.text` 46,389, `.cold` 46,088,
`.mega` 21,248 (pmcore's share: `.text` -56, `.cold` -77, `.mega` -32).

**kern_big, kern_small, kern_emu: not assembled** - all three files are inside
`%ifdef KERN_MEGA` (`megados.inc` through `mega.inc`, the other two through
`hiber.inc:5480`). kern_big's `kernsize --json` sections equal
`/tmp/msp2/base-big.json` at every commit. `make -j1` (fast tier, `hotpar`
included) green at each commit; `make -j1 megadiag` assembles (the MEGADIAG=1
and MEGA_E9 arms); `stkbalance` over the three files reports only the
`mega_dtell` tail jump the base has too. No hot path is touched: the park, the
wake and the resume run once per trip.

## TAKEN

1. **The nine `.mega` far round trips into `mega_lincopyf` are `call
   mx_lcopy`** (msp1/pmmem CROSS-FILE), the last one a tail `jmp`;
   `mega_dstubup`'s `call MEGA_SEL_CODE:mega_selbasef` is `call
   mega_selbase`; `mega_drom_scan` calls `COLD_SEG:mega_irqromc` itself
   (msp1/pmcore CROSS-FILE). `.mega` -21. Same body, same registers, no far
   frame (MEGATRAP=1's `mx_lcchk` line prints different words above the frame
   - a diagnostic).
2. **Three `.text` cells that only stage one trip moved into the park's
   scratch** (`MPS_PCHK` 6, `MPS_NAME` 12, `MPS_TKB` 2): `.text` -20, scratch
   +20, and `mega_pscrsrc` (the scratch offset -> `[mega_xsrc]`) replaces the
   selector-base-and-add three sites wrote out. The name is written whole (12
   bytes, padded) before it is copied, so the scratch's lack of an initial
   value is never read.
3. **`mega_dsave`'s rows into `.mega`, read through CS, 6 bytes a row**:
   `.text` -30 (32 -> the 2-byte `mega_dsidt` that `mega_tset` writes);
   a `from` of 0 is `[mega_dsidt]`'s (it was a dd whose high word nothing
   wrote). `MEGA_DSAVE_LEN` is a `%define` because `os88ovlchk` reads an
   `equ` naming `.mega` data as a reference without CS.
4. **`mega_hbgdtr` deleted** - six bytes nothing read. `.text` -6.
5. **The stage's real segments are computed in protected mode**
   (`mega_dstgs`): `mega_dropr` read `[mem_top]`/`[mega_dpkb]` and shifted in
   `.text`; now it loads `[mega_dsto]`, and the same routine sets
   `[mega_dstg]`, which `mega_dosgo` and `mega_hbgor` each wrote out. Nothing
   between the call and the landing changes `[mem_top]`, `[mega_dpkb]` or
   `[mega_drmode]` (read: `mega_dosgo` -> `mega_ivtrom`/anchor/masks ->
   `mega_dosgo_lo` -> `mega_down` -> reset or CR0 -> `mega_rland`; the 286
   reset keeps RAM). `.text` -15 (2 of it the new cell), `.mega` +~19.
6. **`mega_down`'s 286 reset is `jmp mega_svc_lo.r286`**: the same body
   (SS:SP banked, 40:67, shutdown 0Ah, MEGADIAG's 40:3E, the null IDT and
   `int 3`), differing only in keeping ES and AX, which the reset loses.
   `.text` -39 (body 42, a 3-byte jump). **Reading only: the 286 route runs
   on neither QEMU nor Bochs** - see ROWS.
7. **`mega_dropr`'s ICW1-ICW4 are `mov ah, 0x08 / call mega_pic_at`**
   (pmcore's new entry, msp1/shared DEFERRED 5): `.text` -12 (assembled). The
   IMR `mega_pic_at` puts back is the FFh `mega_dropr` wrote just before;
   the program's masks follow as before.
8. **The trip's timing is MEGADIAG=1's**: `mega_dpit` (6) and `mega_dout`
   (2), `mega_dpitck`, every call to it, the ms-out division, the stub's
   `.pit`, its five calls and its ms-back division are read by nothing but
   MEGADIAG=1's `DOSP` menu-bar line (`mega_dtell`) - no test, tool or other
   kernel code reads them (grep'd). `mega_dback` (2) the same. `MEGA_DHCRUN`
   copies POUT into the header only on that build. `.text` -10, `.mega`
   ~-96, `HIBER.DRV` -87. The shipped header's POUT/PACC are unwritten; the
   shipped stub passes BX/CX that `mega_dwake` no longer reads.
9. **Cells reordered to the layouts they are copied into** (asserted at
   assembly): `mega_dxbase, dxkb, dpbase, dsum, dpkb[, dout]` is KDL_XBASE ..
   KDL_PKB and MPK_C_PBASE .. MPK_C_POUT, so the launch block's tail (eight
   load/store pairs) and `mega_dhcfill`'s cells (seven) are each one `rep
   movsw`, and `mega_dstubup`'s NREG/NCPY two `movsw`. DF = 0 is what
   `mega_dhcfill`'s existing `rep movsw` already took on the same path, and
   `mx_lcopy` ends `cld` before `mega_dstubup`'s. `.mega` ~-70.
10. **Small `.mega` ones**: `mega_dkbup` drains through `mega_dkflush`
    (-10); `mega_hbgor` ends through `mega_dosgo.down` (-12); `mega_dsumall`
    and the stub's sum loop push/pop a BX that `mov bx, sp` overwrites (-2
    each).
11. **`megares.inc` is `cpu 286`** (it runs only on mega_hbgor's way down, a
    protected-mode machine): immediate shifts, `ror cl, 2`, pusha/popa in
    `mr_read` and `mr_move`, no CX save around a shift. `HIBER.DRV` -37 with
    the stub's three shift sites. The tail is still `cpu 8086` at the end.
12. **`mega_wrprobe` (`.boot2`) on 286 code past its own CPU_286 test**:
    pusha/popa in `.try` and `.mv`, immediate shifts. `.boot2` -14 (the blob
    is the full one; HANDOFF-KERNEL-SIZE-P16). `cpu 8086` moved down to the
    `.cold` loader check, which an 8088 runs.

## REFUSED

* **`mega_s_kbdos` (24 `.text`) into `.cold`**: `os88ovlchk` rule 1 (no data
  in `.cold`). Into `.mega`: kern_mega boots and RUNS on an 8088 or a refused
  switch, `mega_pkgok` is reached there, and `.mega` does not exist then.
* **`.mv` taking its CX from the pusha frame** instead of a `push cx`: +2.

## DEFERRED (pass 3's list)

1. **`mega_hbxl` .. `mega_hbf`, 22 bytes of `.text`** that only `hiber.inc`'s
   `hbm_xwrite`/`hbm_xres` read (scratch for the extended section's write and
   resume). Into HIBER.DRV's own data or the park's scratch: est. -22
   conventional, +~10 module (each access learns a segment). hiber.inc is
   shared's/modcsdo's.
2. **`mega_dsidt` (2) and `mega_tset`'s store (3)**: the IDT gate 20h offset
   is `[mega_ldtend] + 100h` exactly, so the `.sv` loop could add it. est.
   -5 `.text`, +3 `.mega`; `mega_tset` is bootonce's/pmmem's.
3. **`mega_dhfree` (2)** lives across one `mega_dosclaimf` only; the pusha
   frame's SP slot holds it for +4 `.mega`. est. -2 conventional.
4. **`mega_dmapb` with ES = PSCR for its whole body**: `.mark` and `.bt` stop
   loading it (the count loop calls `.bt` per KB), the external `.bt`
   callers already have it bar one. est. -6 `.mega`, faster.
5. **`mega_dpit`-style cells left**: `mega_dmsw`, `mega_dss`/`dsp`,
   `mega_d8042` must survive the trip (the scratch is not summed) - not
   movable. What is left of `.text` here is ~95 bytes of cells and ~110 of
   real-mode landing code (`mega_rland`, `mega_dropr`, `mega_down`'s 386
   arm), all of which runs where only `.text` is reachable.
6. **`mega_down`'s 386 arm and `mega_svc_lo`'s** differ only in the landing
   (`mega_rland` against `.rm`); a landing in a register would share them.
   est. -10 `.text`, pmcore's code.

## WANTS THE OWNER

* **`mega_parkcap`** (2 `.text`, ~9 `.mega`): a test's cap on the park,
  written only by a debugger's `setpmem` (KERN-MEGA-DOS-PLAN 12.1 item 5);
  no row in `tests/` names it. Behind MEGADIAG=1, or deleted?
* Item 8 above takes the trip's ms out/ms back out of the shipped kern_mega.
  They were only ever shown on MEGADIAG=1's menu bar, so nothing a user sees
  changes - noted in case a field run expected them in a shipped header.

## WANTS AN ABI BREAK

None found in these files (the park header, the launch block's tail and the
hibernate image are kernel-internal and versioned with it).

## CROSS-FILE

* **coordinator / pmcore (`mega.inc`)**: `mega_irqromf` (`.text`, 6 bytes)
  **has no caller now** - delete it, and `mega_irqromc`'s comment at
  `mega.inc:551` (*"for `.text`'s mega_irqromf"*).
* **pmmem (`mega.inc`)**: `mega_lincopyf` keeps ONE caller, `fsx.inc:2059`
  (shared's); when that goes, the door goes (-6 `.text`).
* **pmcore (`mega.inc`)**: `megados.inc`'s `mega_down` now jumps to
  `mega_svc_lo.r286`; that label must stay a complete 286 reset entry (SS:SP
  banked, 40:67 = `mega_r286`, shutdown 0Ah, never returns).
* **shared / modcsdo (`hiber.inc`)**: DEFERRED 1.
* **bootonce / pmmem**: DEFERRED 2 (`mega_tset`'s `mov [mega_dsidt], ax`).

## ROWS TO RUN (named, not run)

* Bochs/QEMU kern_mega rows: `megadosp`, `megadosroom`, `megakbtrip`,
  `megahma`, `megapiclose`, `megahiber`, `megahibfull` (the streaming resume:
  `mega_hbgor`, `megares.inc`), `megap2hiber`, `megafont`, `megarmb`,
  `megarmbext`, `megarmbref`, `megatrap0`; and `buildmatrix` for the
  MEGADIAG=1 arm (assembled here by `make megadiag`).
* **86Box, the 286 route** (reading only here): `megarmb286` (86Box
  286-victory: `mega_down` with `[mega_drop]` = 2 through
  `mega_svc_lo.r286`), and on `vm/286-mega` **a DOS program run and returned
  (the park, `mega_dosgo` -> `mega_down`'s reset -> `mega_dropr` with
  `mega_pic_at` and `[mega_dsto]` -> the stub -> `mega_dwake`)**, plus a
  Ctrl-Alt-Del inside the DOS program (`mega_wrprobe`, W7).

## DEFECTS FOUND

* The brief's commit `ead16c61` left `docs/INDEX.md` stale, so `make` failed
  at `checkdocs` on a fresh worktree; regenerated here (and again after the
  pmcore merge).

## COORDINATOR, AT THE MERGE

* `mega_down`'s 286 arm entering `mega_svc_lo.r286` put both resets on ONE
  fault address, and `tests/megarmb286.py` counts a bracket's drop by that
  address ("exactly one reset at mega_down's int 3 per taken bracket") - it
  failed with `no mega_down.hang`. The drop now enters at `.r286c` with
  CF = 1 and the shared body ends `jc .rdown`, `.rdown` being the drop's own
  `int 3`; the reflection's entry is `clc`. +8 `.text` on this item's -39,
  so -31 net; `tests/megaseg.txt` 3,359. `megarmb286`, `vidrmb286`,
  `vidrmbsnd286`, `megafdnat`, `hma286` (86Box) and `megarmb`, `megadosp`,
  `megapiclose`, `megakbtrip`, `mega-dosirq` green after it.


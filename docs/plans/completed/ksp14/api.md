# ksp14 agent "api" - the API table and the driver-table additions

Kernel size pass 14 (`docs/plans/completed/ksp14/BRIEF.md`). Concept (BRIEF
0.1, row `api`): the API table and driver-table additions since pass 13 - the
new cells in `kernel.asm` (`OSAPI_SERIAL` to `OSAPI_ARG_TEXT`), the bodies
behind them (`osapi_img_name`, `osapi_drv_use_x`, `drv_cp_closed`,
`osapi_xmem_walk`/`_sel`/`_run`, `osapi_mem_codesel`, `osapi_mem_subseg`,
`mmf_osapi_mem_huge`), `driver.inc`'s new rows (`CDROM.DRV`, `HTTPFS.DRV`,
`VESA.DRV`) and `drv_svc`, `wm_wins`' wheel word, `mod_fp` and `hb_ok_x`.
Commits `8a509992` (#287), `9cbbb93b`'s kern_big footprint, `553f2a21`'s
`OSAPI_IMG_NAME`/`OSAPI_ARG_TEXT`. Branch `ksp14-api`, base `e1e7cebf`
(kernel-size-14 after the menubar, vesa and serial merges). Figures are
ASSEMBLED bytes, `tools/kernsize.py --json` absolutes; resident = `.text` +
`.bss` + `.cold` + `.lowbss` + `.vgabuf`.

## BASE AND TIP

| | base `e1e7cebf` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 92,598 | **92,439** | **-159** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 43,503 / 5,388 / 38,013 / 5,358 / 336 | 43,374 / 5,388 / 37,983 / 5,358 / 336 | -129 / 0 / -30 / 0 / 0 |
| kern_big `KERN_SIZE` | 98,816 | 98,816 | 0 |
| kern_big blob (`.boot2`, the shipped KZIP one) | 6,307 of 6,656 | 6,386 of 6,656 | +79 (the titles' copy; 13 sectors either way) |
| **kern_small resident** | 59,009 | **58,986** | **-23** (`.text` 31,103 -> 31,082, `.cold` 22,047 -> 22,045) |
| kern_small blob / `KERN_SIZE` | 4,592 / 61,440 | 4,579 / 61,440 | -13 (`spl_mds`) / 0 |
| **kern_emu resident** | 92,867 | **92,693** | **-174** (`.text` 43,772 -> 43,628, `.cold` -30) |
| kern_emu `KERN_SIZE` | 99,328 | **98,816** | **-512** (a rung) |
| kern_emu blob | 6,407 of 6,656 | 6,501 of 6,656 | +94 (it carries 'Absolute mouse' too) |
| **kern_mega resident** | 102,901 | **102,797** | **-104** (`.text` 47,181 -> 47,105, `.cold` 42,741 -> 42,713; `.mega` 14,698 unchanged) |
| `CTRL.DRV` image (unpacked, kern_big) | 14,346 + 164 bss | 14,446 + 164 bss | +100, inside the same 15 KB claim (750 spare) |

Against the concept's ~+400 on kern_big that is 40%, and most of it is not
the concept's own hunks but what they sat beside. No `.bss`, `.lowbss` or
`.ovl` byte moved; every hot routine is at the parity `t_hotpar` pins, on
both gated kernels, at every commit.

## TAKEN

Bytes are kern_big / kern_emu / kern_small / kern_mega, each read off the
assembled build (`tools/os88rdiff.py`, the listings, `kernsize --json`).
Clocks are 8088.

### 1. Three cells are INLINE off kern_mega, with no memory operand - -12 / -12 / -6 / 0

`OSAPI_XMEM_SEL` (0x04B3) and `OSAPI_XMEM_RUN` (0x04CB) are kern_mega's
protected-mode doors and every other kernel's answer is the refusal AX = 1,
CF = 1; `OSAPI_MEM_CODESEL` (0x04B9) answers AX = DX, CF iff DX = 0 off
kern_mega. Each was a rare cell (`push bp / call api_rs / dw x`) to a six-byte
`.text` body reached by nothing else. The cell IS that body now - `mov ax, 1 /
cmp al, 2 / retf` (`OSAPI_XREFUSE`) and `mov ax, dx / cmp ax, 1 / retf`, six
bytes each, the length the cells already had - and is ~90 clocks faster per
call (no `api_rs`, no DS switch, no near call). The cells are frozen
(`main`'s), and their ADDRESS, LENGTH and CONTRACT are unchanged; the shape
inside a frozen cell is not ABI (pass 13's `OSAPI_FJCELL` precedent). On
kern_small `OSAPI_XMEM_SEL` borrowed `xm_copy`, which stays for 0x0166, so
only `CODESEL`'s body goes there. kern_mega keeps all three as rare cells to
bodies of its own. `tests/unit/t_api_abi.py` learns a **FIXED** ledger: an
inline cell with no operand to name is checked against its bytes. Broken on
purpose (`cmp al, 1` in `OSAPI_XREFUSE`): red, 12 checks, and green on
restore. SPEC.md 20.3.

### 2. `OSAPI_WM_ONMOUSEUP` and `OSAPI_WM_ONCLOSE` are INLINE off kern_mega - -9 / -9 / -9 / -1 (outside the concept: the table's neighbours)

SPEC.md 20.3 rule 1 named both as "that shape, with kernel callers, so not
inline". Each had ONE kernel caller - `cp_kinit` and `ui_task`'s close
request - and that caller now writes the store itself (`mov word
[bx+W_ONMOUSEUP], cpf_cp_onup`, 5 bytes for 6; `mov [bx+W_ONCL], ax`, 3 for 3),
so the four-byte bodies have no caller but their cells and the cells became
them, OSAPI_WM_ONWHEEL's word-displacement six bytes. kern_mega keeps rare
cells and bodies: its cell cannot store through CS, and a `MEGA_ICWB` stub is
eleven bytes for a four-byte body.

### 3. `OSAPI_WM_ONDRAG` and `OSAPI_WM_ONTIMER` are INLINE on kern_big - -21 / -21 / -2 / -3 (outside the concept)

The routine was the store, behind a `wm_*_c` shim that only added the `clc`
the big build answers with (SPEC.md 13.8.2, 13.9). The cell is the store and
the `clc` (`t_api_abi` accepts a `clc` before the `retf`, as it accepts `cbw`);
`wm_ondrag`, `wm_ontimer` and both shims go, and `cp_kinit` writes `W_ONDRAG`
in place and its `[cp_down]` from the AX it had just zeroed. kern_small's three
cells still share its refusing `stc / ret` (its -2 is `cp_kinit` no longer
banking an AX it does not touch there). `OSAPI_WM_TIMER`'s body is more than a
store and keeps its shim (REFUSED 2).

### 4. The drivers' TITLES are not resident - -77 / -92 / 0 / -64 (adjacent: the rows the concept added)

A `drv_tab` row's display name has two readers and neither is resident: the
loading screen's `Loading Driver n/N (Title)` (`drv_boot_x`, in the blob) and
the Drivers page (`cp_drv_paint`, `cp_drv_mempar`, in `CTRL.DRV`). So the names
are one macro, `DRV_TITLES`, expanded in CTRL.DRV's image beside `drv_memk`
and in the blob beside `drv_boot_x`, and a row's `DRVR_TITLE` is an OFFSET into
it (`DRVT_*`) - the same in both copies because they are one macro. The panel
stages its copy through `cp_stage` like any image string (+11 of code) and
counts the name's length through `cs:`; the blob reads its copy with
`spl_mcs`, so `spl_mds` - the only walker that read a title through DS - goes
(-13 of blob on every kernel). `fdlg_s_drive` ('Drive') and the Disk window's
root title `fm_s_root` ('Disk') were the tails of two of the titles and carry
their own bytes again (net 0: the tails were already counted). kern_mega has no
VESA row, so its block is 13 bytes shorter. `tests/pmdma` (a field instrument,
not shipped) printed a row's title out of the kernel and prints its FILE name
now. `tests/unit/t_drvmem.py` reads the rows' `DRVT_*`. SPEC.md 51.3.3, 15.6.1,
2.8.6.1.

The move needs nothing it did not need: the Drivers page is CTRL.DRV's, and the
boot message is the blob's; no feature reads a disk it did not read before.

### 5. `cp_live` / `cp_unpin` - -16 / -16 / 0 / -16

"Is a Control Panel open?" was `mov al, KIND_CTRL / call
KERNEL_SEG:cw_inst_find_kind` at five `.cold` sites (`drv_use_panel` and four
copies), and "give CTRL.DRV back unless a panel still owns it" was written out
twice (`OSAPI_DRV_USE`'s UP, `cpf_shutdown`). `cp_live` (8) and `cp_unpin` (10,
falling into `cp_live`'s `ret` and tail-jumping `mod_drop`) replace them on
OS88_DRIVERS builds; kern_small keeps the inline seven bytes through the
`CP_LIVE` macro and is byte-for-byte the same size (its `cpf_shutdown` pops DI
after the drop now rather than before it, which `mod_drop`'s every-register
contract makes the same thing). `cp_tick_due_x`, once a second from `ui_task`,
pays one near call and return (~35 clocks a second).

### 6. `osapi_img_name` - -6 / -6 / 0 / -4

Not frozen (0x04D7). kern_big's BX = 1 goes straight to the refusal instead of
through a `.hi` trampoline; one `mov si, [si+2]` serves a driver row and a
module row (`DRVR_FILE - DRVR_SEG` = `MODR_FILE - MODR_SEG`, asserted at the
foot of kernel.asm); `mod_tab` is walked by count. EQUIVALENCE, in unicorn
against the base kernel's own bytes: 6,744 cases (random `drv_tab`, `ss_row`,
`xm_row` and `mod_tab` contents, every loaded segment and some not, BX 0..2):
every register, CF, SP and the 32 bytes round the caller's buffer identical,
and the run named all 22 files it could (898 hits, drivers and modules both).
The routine is first in `.cold`, so an even total keeps `gfx_blit1_x`.

### 7. `drv_cp_closed` - -6 / -6 / 0 / -6

`drv_cp_call_x` refuses BP = 0 itself, first thing, and `drv_cls_svc_x` banks
AX, so the walk's own `or bp, bp / jz .next` and its second `mov al, cl` go.
For a class with no `DSV_CPCLOSE` the two `xor`s now run and the call returns
at once; KENT banks both registers.

### 8. Two parity pads that cancel, in `.cold` - -2 on every kernel

`mem_claim_1`'s range test (the 1 MB fix) and `mmf_osapi_mem_huge`'s body are
each ODD and each carried a never-executed `nop`; nothing hot lies between
them, so the two cancel and both go.

### 9. `osapi_xmem_walk` and `xm_release_rec` - -6 / -6 / 0 / -4

The walk's refusal sets AX before the branch (AX is no input) and returns
through `xm_free`'s `stc / ret` (-2, not kern_mega, whose walk is its own).
`xm_release_rec` banks with `kent_bp` and leaves through `kret_es` (-4): once
per instance teardown, ~40 clocks.

### 10. `OSAPI_FILE_GOTO_QM` returns through `GOTO_Q`'s `ret` - -2 / -2 / 0 / -2 (outside the concept)

-1, and the parity pad after `osapi_video` (kern_big's, pass 13) goes with it.
On kern_small the same byte is odd, and it cancels viddet.inc's kern_small
pad ahead of `gfx_rowbase`/`gfx_blit4`, which goes too (batch 4 below) - so
kern_small nets -2 through item 11.

### 11. Two more pairs of parity pads - -2 / -2 / -4 / -2

`instance.inc`'s two (serial's after `inst_rel_rec`, this concept's after
`inst_pkg_spawn`) lie between `wm_paint_all` and `ui_task` on both kernels;
kern_small's two ahead of `gfx_blit4` (the one item 10 had moved there, and
viddet.inc's) likewise. Each pair goes together.

### AFTER THE MERGE - `reach`'s cross-file items in this concept's files

`kernel-size-14` (`5fc79571`, reach and files merged: kern_big 92,018) merged
in at `84064932`; the merge reproduced the branch's -159 to the byte (91,859).
Then the coordinator's list from `ksp14/reach.md`'s CROSS-FILE, measured
against `kernel-size-14` with `os88rdiff` on all four kernels:

| | `kernel-size-14` | tip | delta (this branch whole) |
|---|---:|---:|---:|
| **kern_big resident** | 92,018 | **91,776** | **-242** (`.text` -129, `.cold` -113) |
| kern_big blob (13 sectors, 6,656) | 6,673 with the titles (over by 17) | **6,577** | 79 spare |
| **kern_small resident** | 58,810 | **58,787** | **-23** |
| **kern_emu resident** | 92,289 | **92,032** | **-257**; `KERN_SIZE` 98,304 both |
| **kern_mega resident** | 102,319 | **102,132** | **-187** (`.text` -76, `.cold` -111) |
| `CTRL.DRV` image + bss | | 14,770 + 164 | 15 KB claim, 426 spare |
| `HIBER.DRV` image (unpadded) + bss | | ~6,750 + 594 | padded to 7,169 as before; 8 KB claim unchanged |

### 12. `ovc_buf` is not blob bytes - 0 resident, blob -164 (kern_big, emu, mega)

**Taken first, because the merge did not assemble without it**: reach's moves
had left kern_big's blob 61 bytes and this branch's `DRV_TITLES` copy is 79,
so the merged blob was 6,673 of 6,656. The SYSTEM.CFG file buffer was 164
zero bytes inside the blob, read off the disk on every boot. It is now the
paragraphs just PAST the blob - `ovc_buf equ BOOT2_PAD` - and `mem_init`
reserves `BOOT2_RES` (the blob plus `CFG_FBUF` rounded to a SECTOR - it was a
paragraph, and that was DEFECT 3 below) where it
reserved `BOOT2_PAD` (kern_small, which has no `ovc_buf`, keeps the old
line), and `mem_unblob` already gives back down to the floor. Nothing reads it
before it is written: `%1_load`'s read or the shortcut stager fills it, and
`%1_deser` is bounded by the count the read returned (read, `driver.inc`).
Nothing else sits past the blob during the boot: the kernel's packed tail is
read below `HEAP_SEG`, `.ovl` is claimed from the heap top after `mem_init`,
and the first use (`drv_boot_x`) is after `mem_init`. Guard 5 (the 128KB boot)
still sums `BOOT2_PAD`: by the time `ovc_buf` is written, stage 1's copy and
its stack at the top of RAM are dead. ksp13 xfile's WANTS 3.

### 13. `exf_sync` into CTRL.DRV and the blob - -46 / -46 / 0 / -46

`EXF_SYNC prefix, hop` (`extmod.inc`): expanded as `cxf_sync` in CTRL.DRV's
panel (two near calls in `cp_vid_click`, 5 -> 3 bytes each) and `bxf_sync` in
the blob (`drv_boot_x`'s one call), each reaching `mod_need`/`mod_drop` through
its image's own `KGO_PAIR` hop (`CPXC`, `BSWC`). Blob +~49, CTRL.DRV +~45.
`extmod.inc` now has no resident bytes but the `EXTCALL` sites. SPEC.md
39.19.6.1.

### 14. `vgcf_boot` at its one call site in the blob - -21 / -21 / 0 / -21

`drv_boot_x` tests `[drv_cfg+CFG_VGC]` and runs the core's `SC_OP_VGC` itself,
`mod_need`/`mod_drop` through `bvs_cgo`. Same refusal: a core that cannot be
read leaves `SETFP` on `mod_gone` and the machine boots grey. SPEC.md 39.28.

### 15. Four far shims gone - -16 / -16 / 0 / -16

`drvf_drv_release`, `drvf_drv_row`, `drvf_drv_unload` and `drvf_drv_load_at`
had no resident caller: every caller is the blob (through `bvs_cgo`), CTRL.DRV's
panel (`CPXC`) or HIBER.DRV, which gains a hop of its own (`KGO_PAIR hbg`,
~54 image bytes, inside the padding that already makes its claim 8 KB).
`DRV_ATTACH` takes the image's hop as its second argument. Each site is the
five bytes the far call was; each call is ~300 clocks dearer, once per
driver load, panel click or hibernate. kern_small's `cp_drvrow` keeps the
refusing stub.

## REFUSED

1. **`drv_cp_closed_x` as the routine's only entry** (-2): `OSAPI_DRV_USE`'s
   DONE would far-call it, and `.cold` may neither name `COLD_SEG` (a ROM or
   the HMA re-bases it: `os88rom` refused the build) nor `push cs` (os88ovlchk
   SPEC.md 2.6 rule 2). The four-byte shim stays.
2. **`OSAPI_WM_TIMER`'s shim folded into `wm_timer`** (-4): `wm_timer` sits
   ahead of `wm_paint_all` and `ui_task`, so its `clc` puts both at an odd
   shift, and the shared-exit spelling that pays for the `clc` stores
   `W_TIMER` AFTER `[wm_tarm]` - a timer scan landing between the two clears
   `[wm_tarm]` and the timer never fires.
3. **The four setters inline on kern_mega too**: a `MEGA_ICWB` stub (11) per
   four-byte body.
4. **`clip_size`, `wm_clip_clear`, `wm_close_req` inline**: 8, 8 and 11 bytes
   against cells of 6, 8 and 6 - and the first two have other callers.
5. **`osapi_img_name`'s copy as `lodsb`/`stosb`** (-2): it needs a `cld` and
   hands the caller DF = 0; and the `db 3Dh` skip over the module arm's
   `mov al, 1` (-1 more): an encoding trick for one byte, taken as an even
   total another way instead.
6. **A rare-cell kind for "enter the instance's folder, then far-call"**
   (`osapi_file_dfree`, `osapi_vol_stat`): an 18-byte body for two 9-byte
   shims.
7. **`OSAPI_DRV_USE`'s no-disk refusal folded into UP's tail**: AL is
   `CPE_DRVUP`'s input, so the refusal's AX cannot be set first.
8. **`drv_tab`'s rows built in the blob from a short table**: the rows would
   be `.bss`, which is resident too - 0.
9. **`drv_svc` +72, `wm_wins` +24**: two copied service tables for the two new
   file classes, which `drv_fs_*` read; one word per window for the wheel. A
   side table is the same bytes.
10. **`OSAPI_MEM_SUBSEG`, `IMG_NAME`, `ARG_TEXT` inline on kern_small**: they
    share `mod_gone`, so there is no body to delete - speed only.
11. **`hb_ok_x`'s VESA arm**: ten bytes and minimal (the vesa note agrees).
12. **The other eight far shims of reach's list** (`drvf_drv_call`,
    `drvf_drv_mounted`, `drvf_drv_vol_bank`/`_back`, `dskf_disk_read`/`_write`,
    `dskf_dsk_vol_row`, `dkf_dsk_vol_fixed`, 32 bytes): each has a caller in
    CTRL.DRV's SETTINGS CORE (`.modc`, a module of its own that os88ovlchk
    refuses a call into the panel's hop from - tried for `drvf_drv_mounted`,
    refused at the build), or in DISKOPS.DRV, FORMAT.DRV or STIKMENU.DRV,
    none of which has a hop: ~54 image bytes per image for 4 to 12 resident.
13. **HIBER.DRV-only `.bss`** (`hb_*`, ~19 bytes): the block is "part of the
    image, which is the point" - what `hbm_perform` banks before the write is
    what `hbm_wake` reads after the restore, and the wake reloads the module
    fresh; `hb_susp`/`hb_drvmask` cross a DOS suspend's two loads and a
    hibernate taken during one; `hb_sp` crosses the restore. `hb_down` (one
    byte, the armed button) alone looks module-local, and is not worth a
    proof on its own.

## WANTS THE OWNER

None open. The one item this note carried - class 3 keeps no copied table -
was approved and is TAKEN, below.

## TAKEN AT THE OWNER'S WORD

**Class 3 keeps no copied service table.** The owner: *"Can drop this. We
don't run retired drivers."* `drv_svcix`'s class-3 byte is 0 where it was
class 4's slot, so `drv_cls_svc_x` refuses class 3 the way it refuses
`DRVC_POINT` and `DRVC_EMS`, and `drv_cp_class_x`'s
`cmp al, DRVC_RETIRED3 / je .skip` goes. **`ETHER.DRV` now hears
`DSV_CPCLOSE` once per Control Panel close, not twice** (DEFECT 1, fixed):
`drv_cp_closed`'s walk visited class 4's table under class 3 as well.
Behaviour for packages is unchanged. An `OSAPI_DRV_CALL` with BH = 3 was
refused at `drv_fseg3` = 0, and is now refused one step earlier at the
table: AX = 0 and CF = 1 either way. A class-3 row cannot overwrite
`DRVC_NET`'s table any more either.

Bytes, assembled (`tools/os88rdiff.py kernel 3e98b6b1 .`, after the merge
of `kernel-size-14` at 3e98b6b1):

| kernel | section | before | after | delta |
|---|---|---:|---:|---:|
| kern_big | `.cold` | 37,487 | 37,483 | **-4** |
| kern_emu | `.cold` | 37,487 | 37,483 | **-4** |
| kern_mega | `.cold` | 42,211 | 42,207 | **-4** |
| kern_small | - | - | - | **byte-identical** |

kern_small is identical because its `drv_cls_svc_x` arm is the compaction,
and that build has neither `drv_cp_closed` nor `drv_cp_class`. Resident at
the tip: kern_big 91,585 and kern_emu 91,841. `drv_cp_class_x` is 50 -> 46
bytes.

Rows: `drvcall`, `drvup`, `drvscroll`, `cpnames` (MartyPC; the panel list
is `drv_cp_class`'s count and ordinal walk) and `ethcfg` (QEMU) - **5/5
green**. `ethcfg` is the row that covers `ETHER.DRV`'s close hook: its
assertion 5 closes the Control Panel, and that close is what writes
`ETHER.CFG` through `DSV_CPCLOSE`, read back across a reboot. `make -j2`
(fast tier 61/61), `make small`, `make emu`, `make mega` and `t_hotpar`
(big 18, small 16 at parity) are all green.

## WANTS AN ABI BREAK

None. Every cell changed inside its frozen address and length; the only
unfrozen interface touched (`OSAPI_IMG_NAME`) kept its contract.

## CROSS-FILE

1. **Parity pads that cancel in pairs, kernel-wide** (the general pass, best
   at the CLOSE when the layout is final). A pad is a never-executed byte that
   puts the hot routines after it back at their pinned parity; two pads with
   no hot routine between them, on the same arms, can both go. At `519139f5`
   (`tools/os88rdiff.py`'s listings against `t_hotpar`'s list):
   * kern_big and kern_small, `.cold` between `gfx_blit1_x` and `dsk_xfer`:
     `memory.inc`'s pad at `mem_bytes_kb_x` (the compaction session's file)
     and `mod.inc`'s at `mod_disarm` - **-2 on both**;
   * kern_big `.text` between `gfx_blit4` and `font_run`: three pads outside
     `gfx_blit4`'s own body - `gfx_spans`, `gfx_scroll` (vga12.inc) and
     `font_char` (font.inc) - **-2**;
   * kern_big between `sch_switch` and `wm_paint_all`: `sched.inc`'s at
     `sch_s_sp` and `wm.inc`'s at `wm_nat_take_wh` - **-2**;
   * kern_big between `wm_paint_all` and `ui_task`: `instance.inc`'s after
     `app_close_win` (kern_big's alone) and `menu.inc`'s at `menu_track` -
     **-2** (`wm_lf_pad` there is a LABELLED data byte; read it first);
   * kern_small, check `blank.inc`'s kern_small arm and `dsk_fatw_claim`'s
     the same way.
   About -8 on kern_big and -2 to -4 on kern_small; each pair must be checked
   for its `%ifdef` arm on both kernels, and `t_hotpar` is the gate.
2. **`osapi_drv_dlg_x` (106 `.cold`) is reachable only while a Control Panel
   is open** (its own first test refuses otherwise, and it is a driver PAGE's
   door - `RAMDISK.DRV` its one caller), so it is a CTRL.DRV-module candidate
   behind a resident thunk that refuses when CTRL.DRV is not resident; its
   completion `drv_dlg_done_x` is a window callback and must stay resident (or
   be a thunk). The `reach` agent's shape, left to it.
3. `SPL_MSG_CELLS` (vidsel.inc) says the longest composed line is 31 cells;
   see DEFECT 2.

## ROWS RUN

One pass after the last code commit of batches 1-4 (`519139f5` + the pads),
`python3 tools/os88test.py soak -j 2 --marty-jobs 1 -k ...`, MartyPC on the
GLaBIOS twins (nothing needed the IBM ROM, nothing skipped; `mtools`
installed), the tree not being edited: **23/23 ok** in 675s.

| row | what of this pass it reaches | result |
|---|---|---|
| `bootstatus` | the loading screen's `Loading Driver n/N (Title)` off the blob's `DRV_TITLES` | ok |
| `splashbar`, `kzboot`, `blobruns` | the blob 79 bytes longer, same 13 sectors and `int 13h` runs | ok |
| `drvscroll`, `cplistrow`, `cpnames`, `drvup` | the Drivers page's titles staged out of CTRL.DRV, its rows and heading | ok |
| `btncp` | `cp_kinit`'s in-place `W_ONMOUSEUP`/`W_ONDRAG` stores, the panel's button gesture | ok |
| `wdcombo` | `OSAPI_WM_ONDRAG` inline (Word's three combos are drag gestures) | ok |
| `timerrace`, `stiksmall` | `OSAPI_WM_ONTIMER` inline; kern_small's refusing cells | ok |
| `dispclose` | `OSAPI_WM_ONCLOSE` inline, `ui_task`'s in-place store | ok |
| `dtfield`, `shutdown` | `cp_tick_due_x`'s `CP_LIVE`, `cpf_shutdown`'s `cp_unpin` | ok |
| `install` | `OSAPI_DRV_USE` UP / DONE / CALL, `cp_unpin`, `drv_cp_closed` | ok |
| `drvcall`, `hdauto`, `heapdrv` | the driver layer around the rows | ok |
| `xmcheck` | `xm_release_rec` on the KENT ladder (QEMU, XMS) | ok |
| `rombig`, `romsmall` | `.cold`'s relocations for a ROM/HMA after every move | ok |
| `buildmatrix` | every knob kernel still assembles (the blob grew) | ok |

**After the merge and items 12-15**, a second pass the same way on the tip:
**21/21 ok** in 756s - `bootstatus`, `bootfloor`, `blobruns`, `kzboot` (the
blob 164 bytes shorter and `ovc_buf` past it; SYSTEM.CFG read at boot),
`vgacolor` (Force Color at boot, item 14), `extdmod`, `dispzoom`, `dispthm`
(EXTD.DRV through `cxf_sync`/`bxf_sync`, item 13), `hibernate`, `kdreturn`,
`kdreturnm` (HIBER.DRV's new hop: the sweep's `drv_row`, `drv_unload` and
`hdl_load`'s `drv_load_at`, item 15), `xmcheck`, `vesa`, `vesa8`, `usbmouse`
(the blob's attach and refusal paths through `bvs_cgo`), `drvcall`,
`drvscroll`, `install`, `hdauto` (the panel's `CPXC` sites), `rombig` (the new
`COLD_SEG` sites in HIBER.DRV relocated for a ROM) and `buildmatrix` (every
knob's blob).

Equivalence where no row reaches: `osapi_img_name` in unicorn (TAKEN 6). The
fast tier ran in every `make` (61/61 at each commit); `tools/stkbalance.py`
over `kernel/kernel.asm kernel/*.inc`: 0 unbalanced at base and tip; `make
emu` and `make mega` build at the tip.

## DEFECTS FOUND

1. **`ETHER.DRV`'s `DSV_CPCLOSE` runs TWICE at every Control Panel close**
   (by reading): `drv_cp_closed` walks classes 1..`DRVC_MAX` and class 3, the
   retired one, reads class 4's copied table (`drv_svcix`), so `DRVC_NET`'s
   close proc is called for class 3 and again for class 4. `drv_cp_class_x`
   skips class 3 for exactly this reason ("a walk that visits it counts
   ETHER.DRV twice"); the close walk never did. Harmless today as far as can
   be read. **FIXED** at the owner's word: see TAKEN AT THE OWNER'S WORD.
2. **The loading screen truncates a driver's line on kern_emu and with ten
   rows** (by reading): `SPL_MSG_CELLS` is 32 and its comment names 'Loading
   Driver 1/5 (Hard Drive)' (31) as the longest line, but `Loading Driver
   1/10 (Absolute mouse)` is 36 and `Loading Driver 10/10 (VESA Display)` 35;
   `spl_mput` bounds the line, so the closing `)` and the end of the name are
   cut. Not this pass's to change (a look).
3. **`BOOT2_RES` took every boot-time claim off the 512-byte grid - THIS
   BRANCH'S, introduced by TAKEN 12 in merge 84064932 and FIXED.** It
   showed as `vesasu` (*"the card's format drop-down has no rect"*) and
   `vesablitd` (*"canvas is not at depth"*) red on kernel-size-14 from
   84064932 on, both parents green. The cause is that `BOOT2_RES` rounded
   `CFG_FBUF` to a PARAGRAPH, and `mem_init` makes it the heap's floor while
   the blob lives - so every bottom-up claim made at boot lay at an odd
   phase. The heap map showed it at `224B0`, `238B0`, `24CB0`, `2B8B0`, all
   `0x..B0`: `0x1A0` paragraphs of blob plus `0xB` of buffer. That broke
   SPEC.md 1's rule that every disk-visible base is 512-aligned. Most of
   those claims are movable, but the FAT window's is PINNED, so the phase
   outlived `mem_unblob` and every claim above it inherited it. PiXEL's
   `px_dmaclaim` refuses an `OSAPI_MEM_CLAIM_DMA` base with `DL & 1Fh`
   non-zero. Its head claim was refused, so every open ended
   `-: not enough memory` with 402 KB free, and the view claim was never
   made: `px_vdepth` stayed 0 and the canvas stayed empty.

   How it was isolated:
   - Reproduced red on the tip.
   - A variant tree with `BOOT2_RES` 8 KB larger stayed red. So nothing
     was writing past the reservation; the trouble was its size modulo 512.
   - A QEMU probe read PiXEL's own state: the document did arrive
     (`px_argname` `CITY.PCX`, `px_ndone` 1), `px_lastref` was 7
     (`PXD_MEM`) with format `-`, which pointed at `px_head`, and
     `tools/heapmap.py` showed the bases.

   **The fix is one constant**: `CFG_FBUF` rounded to 512. An assembly
   assertion now refuses a `BOOT2_RES` or `BOOT2_PAD` that is not a multiple
   of 512. The cost is **0 bytes in every section on all four kernels**:
   the `add ax, BOOT2_RES / 16` immediate changes in `mem_init` and
   nothing moves. At boot the heap floor sits 336 bytes higher until
   `mem_unblob`, then gives them back with the blob. The saving TAKEN 12
   was taken for (164 blob bytes) stands. Gates: `make -j2` (fast 61/61),
   `make small`, `make emu`, `make mega`, `make vesatest`, `t_hotpar`.
   Rows, `--marty-jobs 1`: `vesasu`, `vesablitd` (red -> green), `vesa`,
   `vesa8`, `vesacp`, `vesafsx`, `extdmod`, `dispdrag`, `dragui`,
   `pxopen`, `heapcheck` and `bootfloor-ab` (the floor moved) - **12/12**.

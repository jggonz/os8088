# ksp11 / voltake - notes (agent "voltake")

Concept: the KERNEL side of merge `e002a0e9` ("video-xms"): `OSAPI_VOL_TAKE`
(SPEC.md 52.1.1), the `DRVC_EMS` class (SPEC.md 107), `xm_release_rec`'s
`EMSV_GONE` call, and `ui_timer_pass`'s re-test under the lock (13.9.2).
Files: kernel/disk.inc (the volume plumbing - NOT picomem's "B: from the
card" hunk nor diskwrite's dsk_xfer/FAT16/WSEQF hunks), driver.inc,
kernel.asm, ui.inc, xmem.inc, apps/os88api.inc, drivers/os88drv.inc.

Branch `ksp11-voltake`, cut from `elendilon` at `2a05e31f`. Bytes are
`tools/kernsize.py` section figures, assembled, against the base's ABSOLUTE
sections (not kernsize's blessed-baseline "+N").

## Base `2a05e31f`

| | text | bss | cold | lowbss | vgabuf | resident |
|---|---:|---:|---:|---:|---:|---:|
| kern_big | 43,428 | 5,190 | 37,943 | 5,598 | 336 | 92,495 |
| kern_small | 32,022 | 3,073 | 23,479 | 2,868 | 0 | 61,442 |

## TAKEN

### batch 1 - the concept's own hunks (big -30: text -18, cold -12; small -18: text -6, cold -12)

* **`OSAPI_VOL_TAKE` (cell 0x0467) is gone: a take is `OSAPI_VOL_ADD` with
  DX != 0.** DX was "RESERVED, pass 0" in main's driver SDK since SPEC.md
  22.6 retired the listing claim it carried, and every caller in the tree
  (HDD, NET, RAMDISK) passes 0, so a non-zero DX is free to mean something.
  DL = the volume index OSAPI_VOL_AT answered, AL = the driver's handle. The
  slot was unpublished (not on main `95f7e971`). What goes: the 6-byte rare X
  cell (`.text`, both kernels), the second `osapi_vol_fence` call and its
  retf, and the far call back into `osapi_vol_at_x` the old body made to
  find a row the driver had already found. The take body is behind the fence
  `osapi_vol_add_x` already runs. **text -6, cold -6 on both kernels.**
* **Kind and unit in ONE word store** (DV_KIND 0, DV_UNIT 1 - asserted). The
  take and `dsk_vol_del`'s give-back both bracketed four byte stores with
  `pushf/cli ... popf` so a transfer could never see a half-changed row; one
  `mov [bx], ax` is a single instruction and so needs no bracket, and the
  class and DV_BUNIT bytes are only read on a row of the other kind. The
  give-back is 26 -> 20 bytes, the take smaller by the bracket. Part of the
  cold -12 above (take -6 with the fold, give-back -6).
* **`xm_release_rec` converts the record ONCE** (kern_big .text -5): it
  called `inst_idx` for the EMS door and again for the XMEM dispatch, with
  two separate push/pop banks. One bank of five, AL kept across the EMS call
  on the stack (the driver eats AX and BX). `xm_dsp` sets its own ES, so
  arriving with ES = KERNEL_SEG is harmless. Teardown, not hot.
* **`ui_timer_pass` reads W_ONTIMER only under the lock** (kern_big .text
  -7): 13.9.2's fix re-reads the handler word after `gfx_lock`, which made the
  pre-lock read redundant - its only job was to skip a due timer with no
  handler, a legal no-op that now costs one lock round trip instead. The
  walk's cheap test (W_FLAGS, W_TIMER) is untouched. SPEC.md 13.9.2 says so.
* HDD.DRV (`drivers/hdd/mount.inc`, the caller - **drivers agent's file, a
  minimal edit**): `hd_mount_one`'s `.take` asks `hd_kvol` again for the
  index and calls OSAPI_VOL_ADD with DL = it; it no longer reads DI (which
  was hd_mount's device row, an undocumented register contract across two
  routines). Code -4 bytes (hd_mount_one's span 0x742 -> 0x73E in the
  listing); the image stays 3,764 because the code is followed by an `align
  512` window.

At batch 1: kern_big text 43,410, bss 5,190, cold 37,931 -> resident
92,465 (-30); kern_small text 32,016, cold 23,467 -> 61,424 (-18).

### batch 2 - kern_small: the driver-volume plumbing is stubs (small -422: cold -418, bss -4; big byte-identical)

Not the concept's own hunks but the code around them, in the same two
files (secondary, per the brief).
* On kern_small `osapi_vol_fence` walks `drv_cls_fp_x`, whose whole body
  there is `xor di,di / stc / ret`: no class is ever published, so the fence
  refuses every caller for the life of the machine, and everything behind it
  is code the IP cannot reach. Gated `%ifdef OS88_DRIVERS`: `osapi_vol_fence`,
  `osapi_vol_add_x` (with the take), `osapi_vol_del_x`, `osapi_vol_mount_x`,
  `dsk_vol_add`, `dsk_vol_del`, `dsk_vol_drop_drv_x`, `dsk_vcls`; and
  `%ifdef OS88_REDIR` `osapi_fs_ent_x`, `dsk_fslist`, `dsk_fsn` (the FSV_LIST
  arm that raises `[dsk_fslist]` is OS88_REDIR already).
* The cells keep their offsets (SPEC.md 20.4) and point at driver.inc's
  existing kern_small stubs, which give the same answers: the three fenced
  slots on `osapi_drv_cfg_x`'s `stc / retf` (AL untouched, as the fence's
  refusal left it), `osapi_fs_ent_x` on `osapi_drv_classk_x`'s `xor ax,ax /
  stc / retf` (its own `.no`), `osapi_vol_fence` on `drv_fs_has`'s `stc /
  ret` in `.cold` - the section `osapi_desk_item_x`, its other caller, is in.
  No new bytes for any stub: they are labels on bodies already there.
* `osapi_vol_at_x`'s DV_BUNIT arm (a TAKEN row) is `%ifdef OS88_DRIVERS`:
  no driver, no take, every kern_small row is BIOS or free (-7 of the 418).
* Verified kern_big is byte-identical (two archive copies of the tree, HEAD
  and HEAD + this batch, built side by side and `cmp`'d).
* SPEC.md 51.0.2 says so.

At batch 2: kern_small text 32,016, bss 3,069, cold 23,049 -> resident
61,002 (-440 from base). kern_big unchanged at 92,465.

### batch 3 - DRVC_EMS keeps no table copy (big -25: text +1, bss -36, cold +10; small and emu as their arms)

* **The .bss +38 was EMS's owner word and a 36-byte copy of its service
  table - for ONE word.** EMS.DRV publishes DSV_PKGCALL and nothing else
  (`em_svc` is 34 zero bytes and the door), and every other reader of a
  class copy - the Control Panel page walks (DSV_CPNAME, DSV_CPCLOSE), a
  volume's DSV_BLK, snd.inc's fixed class-1 offsets - finds nothing in it.
  `drv_publish` now puts EMS's DSV_PKGCALL in **drv_fptr7's OFFSET half**,
  which nothing reads for this class (`drv_pkg_disp` far-calls through class
  1's PKG_DISP for every class, and the memory relocation walks the segment
  halves), written before the segment publishes it. `drv_pkg_call_x` reads
  it there (`ja .ems` after the DRVC_POINT test). `drv_cls_svc_x` goes back
  to its pre-EMS `cmp al, DRVC_POINT / cmc / jc` (both no-copy classes are
  the last two, asserted). `drv_svc` is `DSV_SIZE*(DRVC_MAX-3)`.
  - No length check on the publish read: a DRVC_EMS driver is new, so it
    cannot be a pre-PKGCALL driver with a short table - which is the only
    thing DRV_H_DSV's test guards against - and EMS.DRV's table is full.
  - Faster on every class: `drv_pkg_call_x` loses `cmp al, DRVC_MAX / ja`
    and `cmp al, DRVC_POINT-1 / jb / dec` from the copy classes' path (two
    compare-and-branch pairs, ~16 cycles, on every ETHER socket verb), and
    EMS skips the class arithmetic and the byte `mul` (~70 cycles) for one
    `mov bp, [drv_fptr7]`.
  - SPEC.md 107.1 and 107.5 say so; 107.1's "inst_rel_rec calls the EMS
    door" corrected to xm_release_rec (it has a fourth caller).
* kern_small and kern_emu: the change is inside the live driver block, so
  kern_small is byte-identical (built side by side, `cmp`'d) and kern_emu is
  kern_big's arithmetic.

At batch 3: kern_big text 43,411, bss 5,154, cold 37,941 -> resident
92,440 (-55 from base). kern_small unchanged at 61,002 (-440).

## TIP

| | text | bss | cold | lowbss | vgabuf | resident | vs base |
|---|---:|---:|---:|---:|---:|---:|---:|
| kern_big | 43,411 | 5,154 | 37,941 | 5,598 | 336 | **92,440** | **-55** |
| kern_small | 32,016 | 3,069 | 23,049 | 2,868 | 0 | **61,002** | **-440** |
| kern_emu (archive builds, build number 0 both) | 43,659 | 5,154 | 38,059 | 5,598 | 336 | 92,806 | -55 |

`KERN_SIZE` unchanged on both (98,304 / 63,488): no rung moved, which is
not the question. `.ovl`/`.ovlw` unchanged. Drivers: `HDD.DRV` code -4,
image 3,764 unchanged (an `align 512` window follows the code); `EMS.DRV`
untouched.

Against the merge's own cost (big +209, small +84): the VOL_TAKE half
(+84 both) is now +66 of `.cold` on both, the cell gone; the EMS half (+113
big) is +83; `ui_timer_pass`'s fix (+12) is +5. And kern_small paid 422 bytes
below what it carried before the merge, for code it could never run.

## REFUSED

* **`drv_pkg_call_x` calling `drvf_drv_cls_svc` (far) for the class
  arithmetic** instead of its inline ladder: about -22 of `.text`, and
  ~100 cycles more on every OSAPI_DRV_CALL (every ETHER socket verb, every
  EMSV_MAP). Pass 9 inlined it on purpose; the header says why.
* **A class->copy byte table** (`db FF,0,1,FF,2,3,FF,..`) shared by
  `drv_cls_svc_x` and `drv_pkg_call_x`: about -8 before batch 3, ~0 after
  it - with EMS out of the copies the inline ladder is two compares.
* **BH = the caller's instance for EVERY class** (drops `cmp bh, DRVC_EMS /
  jne`, -5 `.text`): main's os88drv.inc publishes "BH = your class" to every
  driver's DSV_PKGCALL. Not free.
* **The take without its `jc` after `dsk_vol_row_x`** (-2): a bad index
  then reads byte 0 of the kernel as a row kind, and is refused only because
  that byte happens to be non-zero.
* **`osapi_vol_at`'s taken-row arm as one compare** by making every BIOS row
  carry DV_BUNIT = DV_UNIT (-7 there): every BIOS row's creation then writes
  it (floppy add, boot adoption), and the give-back's "BUNIT != 0" test
  stops meaning "taken" for B:. More bytes elsewhere than it saves.
* **The EMS teardown call in `.cold`**: `drv_pkg_call_x` is a near `.text`
  routine, so `.cold` would need a far shim to reach it - more bytes.
* **A length check on `drv_publish`'s read of EMS's DSV_PKGCALL** (+8):
  DRV_H_DSV guards against a driver written before a cell existed, and no
  DRVC_EMS driver can predate DSV_PKGCALL.
* **Reproducing the `mem_rr_tab` defect on MartyPC**: nothing moves EMS.DRV
  in any row - it needs a hole ABOVE a booted EMS.DRV (another top-down
  claim freed) and then a compaction - so the fix stands on the reading.


## DEFECTS

* **`mem_rr_tab` relocated five class segments of seven** (`93287205`,
  its own commit, 0 bytes). `MEM_RR_ROW drv_fseg, 5, 4` walked drv_fseg ..
  drv_fseg5; DRVC_POINT (6, on main since the CH375 mouse) and DRVC_EMS (7,
  this merge) were appended without it, so a driver image that MOVES
  (SPEC.md 66.6.3) left drv_fseg6/7 naming the old segment. EMS.DRV hooks no
  vector, so `mem_can_move` lets it move: after that compaction every
  OSAPI_DRV_CALL to it - and xm_release_rec's EMSV_GONE at every instance
  teardown - far-calls PKG_DISP in freed memory; the USB mouse's packets are
  refused by osapi_mou_feed's segment fence. The row counts DRVC_MAX now.
  tests/drvmove.py's stale-word check names drv_fseg6/7 and all seven rows.
  Found by reading; NOT reproduced (no row moves EMS.DRV or USBMOUSE.DRV).

## CROSS-FILE

* **drivers agent - `drivers/hdd/mount.inc`**: I changed `hd_mount_one`'s
  `.take` (the call site of the retired OSAPI_VOL_TAKE: OSAPI_VOL_ADD with
  DL = `hd_kvol`'s index), and one comment in `hd_mount`. Nothing else in
  drivers/hdd/. Reconcile with your size work there.
* **drivers agent - `drivers/ems/ems.asm`**: since batch 3 the kernel reads
  EXACTLY ONE word of EMS.DRV's service table, [SI+DSV_PKGCALL] at
  `drv_publish`, and never DRV_H_DSV for this class. `em_svc` is 34 zero
  bytes and `dw em_pkg`; DRVV_ATTACH could answer SI = (a word holding
  em_pkg) - DSV_PKGCALL and drop the zeros: about -34 of image, IF
  OS88_DRIVER's table-length argument (`em_svc_end - em_svc`, checked EVEN,
  2..DSV_SIZE by OS88_DRV_END) is given something honest - e.g. 2 with the
  label placed so the macro's arithmetic holds. Not taken: your file.
* **coordinator / multisel - `kernel/memory.inc`**: the one-line defect fix
  (`MEM_RR_ROW drv_fseg, DRVC_MAX, 4`), commit `93287205`. Not multi-select
  code; it is the relocation table.
* **coordinator - `kernel/desk.inc`**: on kern_small `osapi_vol_fence` is
  `stc / ret` now, so `osapi_desk_item_x`'s DRIVER arm (the service item:
  the record copy into `desk_svc_rec`, `desk_svc_seg`, `desk_svc_cap`, the
  withdraw) is unreachable there, as are whatever else reads
  `desk_svc_seg` on that kernel. An `%ifdef OS88_DRIVERS` round them is
  kern_small bytes I did not measure (not my file).
* **diskwrite / picomem - `kernel/disk.inc`**: my hunks are the volume
  plumbing only (`osapi_vol_at_x` .. `osapi_vol_mount_x`, `dsk_vol_add`,
  `dsk_vol_del`, `dsk_vol_drop_drv_x`, the DV_BUNIT equate, and three bss
  lines - `dsk_vcls`, `dsk_fslist`, `dsk_fsn` - now inside `%ifdef`s).
  `dsk_vol_del`'s DSK_STREAM head (diskwrite's WSEQF hold commit) is
  untouched and now sits inside the OS88_DRIVERS gate, which is correct:
  DSK_STREAM is kern_big's.

## ROWS RUN

Every row one call of `tools/os88test.py soak -k ...`, on this branch:
* batch 1: `hdtake`, `hdtakeboot`, `hdtakeblank` (QEMU, the take and the
  give-back), `timerrace`, `xmcheck`, `ems`, `emspico`, `videms`,
  `rdmount`, `drvup` - **10/10 ok**.
* batch 2 + the defect: `small128`, `smallboot`, `smalllaunch` (kern_small),
  `drvmove` (with the widened stale-word check), `deskitem` - **5/5 ok**.
* batch 3: `ems`, `emspico`, `videms`, `videmshyb`, `xmcheck`, `drvup`,
  `socktest` (QEMU, ETHER's OSAPI_DRV_CALL path) - **7/7 ok**.
* After every batch: `make -j2` (the fast tier inside it, 61/61), the
  kern_small kernel (`make small` in full at batch 2), `tools/stkbalance.py`
  over every kernel file touched, base against tip: the same unbalanced
  paths (pre-existing) and no new one. `make emu` at the tip.

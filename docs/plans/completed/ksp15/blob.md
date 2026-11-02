# ksp15 agent "blob" - where code lives: resident code only the boot or a module reaches

Kernel size pass 15 (`docs/plans/completed/ksp15/BRIEF.md`, row `blob`).
Concept: a resident routine whose every caller is the boot blob (`.boot2`),
the boot overlay (`.ovl`) or an on-demand module can live in those callers'
images instead - written once as a macro and expanded where its callers are,
pass 13's `DRV_ATTACH` shape. Seeded by ksp14 `reach.md` CROSS-FILE and
`api.md` CROSS-FILE 2. Branch `ksp15-blob`, base `49ad5d67` (the brief on
`e4896481`). Figures are ASSEMBLED bytes off each kernel's own `kernel.map`;
resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`; module images
are `.modc` + `.modu` (CTRL.DRV) and `.modx` (EXTD.DRV), unpacked.

**What the seed list had already become.** Four of the five named items were
taken in pass 14 by the api agent after reach wrote them down: `exf_sync` is
`EXF_SYNC` (blob `bxf_sync`, CTRL.DRV `cxf_sync`), `ovc_buf` is not blob
bytes, `vgcf_boot` is `drv_boot_x`'s own lines and `osapi_drv_dlg_x` is
CTRL.DRV's `modc_e_dlg` behind a refusing far jump. `desk_rowcalc_x` (and
kern_small's `desk_reflow`/`desk_live`) was the one left - and it was left
because **the blob was full**: 6,647 of 6,656 bytes on kern_big at the base
(`kernel.map`'s `.boot2`; `kernsize`'s `boot2` line reads 6,447, which is not
the section), and the owner declined a 14th sector in pass 14.

## THE FINDING: `.cfx` is boot RAM nothing had counted

`.cfx` (MOVABLE-COLD-PLAN 4, kern_big and kern_emu - `COLD_SLIDE`) is the
section after `.cold` in the FILE, so stage 2's one read lands it on the RAM
just past the kernel, under the blob's lift, and **`mem_init` sets
`[mem_base]` above the blob** (memory.inc: `[spl_fseg]` + `BOOT2_RES`) - the
only other writer of `[mem_base]` is `mem_unblob`, after `kmain_o` returns.
So everything in `.cfx` lives exactly as long as the blob does, costs no
blob sector, and is reached with a far call to the constant `CFX_RAM`.
It held only `cfx_slide` and its table; it now also holds the boot's copies
of two bodies the blob had no room for. Two rules for anything put there,
both kept by construction:

* **past `cfx_tab`, never before it**: the first `KVGABUF_SIZE` bytes of
  `.cfx` are `.vgabuf`'s RAM too (`VGABUF_RAM` = `COLD_RAM` + `COLD_PARA` =
  `CFX_RAM`), which the slide is done with before the planar decoder's first
  use - a body called later must not be there. An `%error` beside `cfx_grid`
  guards it;
* **name no segment**: os88cfx stamps `.text`, `.ovl` and `.boot2` only.

Neither kernel's file grew a sector (`KZ_SECS` 97 / 155 unchanged), and
`t_blobruns` is unchanged.

## BASE AND TIP

| | base `49ad5d67` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 90,008 | **89,876** | **-132** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 41,136 / 5,398 / 37,780 / 5,358 / 336 | 41,108 / 5,398 / 37,676 / 5,358 / 336 | -28 / 0 / -104 / 0 / 0 |
| kern_big `.boot2` (map) / `.ovl` / `.cfx` | 6,647 / 5,110 / 353 | 6,655 / 5,119 / 494 | +8 / +9 / +141 |
| kern_big `BOOT2_SECS` / `KERN_SIZE` | 13 / 97,280 | 13 / 97,280 | 0 |
| **kern_small resident** | 58,220 | **58,097** | **-123** (`.cold`) |
| kern_small `.boot2` / `.ovl` / `BOOT2_SECS` / `KERN_SIZE` | 4,776 / 1,482 / 11 / 59,904 | 4,956 / 1,477 / 11 / 59,904 | +180 / -5 / 0 / 0 |
| **kern_emu resident** | 90,264 | **90,132** | **-132** |
| kern_emu `.boot2` / `.ovl` / `.cfx` / `BOOT2_SECS` | 6,762 / 5,110 / 353 / 14 | 6,770 / 5,119 / 494 / 14 | +8 / +9 / +141 / 0 |
| **kern_mega resident** (`.mega` 18,237 unchanged) | 105,955 | **105,823** | **-132** |
| kern_mega `.boot2` / `.ovl` / `BOOT2_SECS` | 6,820 / 5,109 / 14 | 6,961 / 5,118 / 14 | +141 / +9 / 0 |

| image | base | tip | claim |
|---|---:|---:|---|
| `CTRL.DRV` kern_big (+164 bss) | 14,954 | 15,092 | 15 KB, unchanged (15,256 of 15,360: **104 left**) |
| `CTRL.DRV` kern_emu (+164) | 14,972 | 15,110 | 15 KB, unchanged |
| `CTRL.DRV` kern_mega (+164) | 15,361 | 15,426 | 16 KB, unchanged (it was already over 15) |
| `CTRL.DRV` kern_small | 5,498 | 5,619 | 6 KB, unchanged (525 left) |
| `EXTD.DRV` (big, emu) / (mega) | 1,837 / 1,862 | 1,872 / 1,897 | 2 KB, unchanged |

**The boot overlay is now FULL on kern_big and kern_emu: 5,119 of 5,120.**
The blob has one byte on kern_big. Both are recorded so the next agent that
reaches for either knows before it builds.

## TAKEN

Hot paths: `vid_ctx_act` only, and it got FASTER (a near `call` and its `ret`
gone from the display switch). `hotpar` green on both kernels at every commit, with
one new parity pad (item 3). `tools/stkbalance.py` over every file touched,
base against tip: the same findings, none new.

### 1. `desk_rowcalc_x` leaves resident on all four kernels (`dec1aef4`) - big/emu/mega `.cold` -100, kern_small `.cold` -123

Nothing resident called it. Its callers, each read: `desk_init` (`.ovl`, its
last step), `kmain_o` (blob, after `drv_boot`), the blob's `bvs_switch`
(`VSW_SWITCH`) and `bdk_reflow` (`DOCK_APPLY`), and CTRL.DRV's `vid_switch`,
`dock_reflow` and the arrangement row. Nothing falls into it, nothing jumps
into its interior, no table holds it.

So it is desk.inc's `DESK_ROWCALC` macro (the geometry is `DESK_GRID`, a
macro of its own), expanded as `wm_rowcalc` in CTRL.DRV and `bvw_rowcalc` for
the boot - named for the `WM_REFIT` prefix `VSW_SWITCH` and `DOCK_APPLY`
already carry, so both macros call `%3`/`%4` `_rowcalc` with no new
parameter. `desk_init`'s far call moves to `kmain_o`, the instruction after
`OVLCALL desk_init` and before `MARKW 21` - the two stores `desk_init` makes
after it (`[desk_clkt]`, `[desk_sel]`) are read by nothing on the rowcalc's
path, so the order change is invisible (`.ovl` -5).

* kern_big and kern_emu: the boot's GEOMETRY is `cfx_grid`, in `.cfx`, and
  `bvw_rowcalc` is `call CFX_RAM:cfx_grid / BSWC desk_reflow / ret` - the
  routine's two halves in its own order. 11 blob bytes; the blob nets +8.
* kern_mega: no `.cfx`, 348 blob bytes spare: the whole copy (`BSWC`).
* kern_small: there `desk_reflow` and `desk_live` had no other caller (no
  driver, no shortcut, no service item adds a volume after the boot - every
  other `desk_reflow` call site is `OS88_DRIVERS`/`OS88_SHORTCUTS`), so the
  one-pass reflow and `desk_live`'s one line are written into the macro and
  all three leave. Its blob (676 spare) takes the copy through a `KGO_PAIR
  bvw` of its own (the `bvw_kgo` half is unused there and is blob bytes).

The bodies are the resident ones instruction for instruction but for the
prologue/epilogue (`kentc_di`/`kretfc_di` become five pushes and pops, six on
kern_small) and the three near calls into `.cold` (`desk_reflow`,
`dsk_vol_slot_x`, `desk_cmark`), which go through each image's hop - KGO_PAIR
preserves every register and flag both ways, which is what `dsk_vol_slot_x`'s
BX and `desk_cmark`'s preserved DL need.

### 2. `drvf_drv_call` goes (`57753c0e`) - big/emu/mega `.cold` -4

Its callers are the blob (`DRV_ATTACH bdl`'s `bdl_call`, `vx_boot_x` x3,
`xm_boot_x`, kern_emu's `vmm_boot_x`), CTRL.DRV (`cdl_call`, `cp_thmv_open`
x2, `cp_vid_vxcall`) and HIBER.DRV (`hdl_call`, `hbm_sweep`) - every one an
image with a KGO_PAIR hop, at the five bytes the far call was. `DRV_ATTACH`'s
`%1_call` takes `%2`, the hop the macro already had. KGO_PAIR keeps BP, which
is `drv_call`'s input. The shim was in driver.inc's far-entry block (fsys's
file, not its row's routines - SAID HERE as outside the row).

### 3. `vid_ctx_capture` leaves resident; `vid_ctx_pre` written into `vid_ctx_act` (`fdb3ba59`) - big/emu/mega `.text` -28

reach REFUSED this in pass 14 "blocked by room, not by reach" - the blob and
`.ovl` were both full. `.cfx` is the room. Callers: CTRL.DRV's
`vid_disp_init` (`VSW_DISP vid`), EXTD.DRV's `ext_extend_b` (two), the blob's
`bvs_disp_init`, and the overlay's `vid_ctx_init`. vidsel.inc's `VID_CTXCAP
label, return` expands as `vid_ctxcap` (CTRL.DRV), `ext_ctxcap` (EXTD.DRV),
`cfx_ctxcap` (`.cfx`, `retf`: the blob's site is `call CFX_RAM:cfx_ctxcap`,
five bytes where `BSWK` was five) and `bvs_ctxcap` (kern_mega's blob);
`vid_ctx_init` writes it in (DI is display 0's record there already, ES = DS).
`vid_ctx_ptr` (four instructions) is written in too, so a copy needs no hop.
DS = KERNEL_SEG at every caller (the data alias on kern_mega, which is what
the resident body's `KDATAR` loaded): the copy takes ES from DS.
`vid_ctx_pre` then had one caller, `vid_ctx_act`, and is written into it.
`.text` -29, one odd hole ahead of `cur_move` and `font_run`: **one parity
`nop`** after `vid_ctx_act`'s `ret` (never run), so -28.
kern_small: byte-identical (`cmp` of `build/smallk/kernel.bin`).

## REFUSED

* **`hbk_drv_blk_call`** (hiber.inc, 4) and **`os88ui_btn_f`** (apps/os88ui.inc,
  4): every caller has a hop, but the shims are fsys's (`hiber.inc`'s resident
  half) and gui's (ctrl-adjacent thunks) - CROSS-FILE.
* **`dock_geom`/`dock_band`** (4 each): `jmp far [dkv + n]` from a NEAR
  call, so the target's return depends on landing in KERNEL_SEG; an image
  cannot reproduce that frame for four bytes.
* **`drv_dlg_done_x`** (47) + **`fm_ncpye`** (13) into CTRL.DRV: the
  completion is a window callback the dialog calls after the panel may have
  closed and dropped its image - a resident thunk refusing through `mod_gone`
  would lose the driver's answer. A behaviour question, and not worth one.
* **`stik_boot`** (60) and **`drv_notice_x`** into the blob or `.cfx`: both
  run after `mem_unblob`, and moving them before it changes what the panel
  launch at boot sees.
* **`.cfx` for anything run after `mem_unblob`**: `mem_unblob` lowers the
  floor below the blob and COMPACTS onto it, so `.cfx` is gone at once.
* The rest of the fixpoint, read and dismissed as pass 14 did: the ISRs
  `.ovl` installs, `fsx_v*` (bytes), `mod_fp` (an `equ` family the walker
  cannot see), the far shims whose callers include `.modc`/`.modl`/`.modf`/
  `.modn` (no hop in those images: each would need a ~50-byte KGO_PAIR).

## WANTS THE OWNER

None. No behaviour moved: every body is the resident one, run at the same
moment by the same caller.

## WANTS AN ABI BREAK

None. No cell, header, format or SYSTEM.CFG key moved.

## CROSS-FILE

* **fsys - `hbk_drv_blk_call`, -4**: its one caller is `hbm_geom` (HIBER.DRV),
  which has `hbg_cgo`: `call hbg_cgo / dw drv_blk_call_x`.
* **gui - `os88ui_btn_f`, -4 on kern_big**: callers CTRL.DRV (`cp_bemit`,
  `sd_button`) and HIBER.DRV (`hbm_btn`), both with hops, `os88ui_btn` being
  `.cold` there.
* **anyone - the boot overlay is full** (5,119 of 5,120 on kern_big/emu), and
  `.cfx` is where a boot-only body that does not fit the blob goes now (the
  two rules above).

## ROWS RUN

`python3 tools/os88test.py soak -j 2 --marty-jobs 1 -k ...` over 32 rows,
ONCE, after the last code commit (`fdb3ba59`), the tree not being edited:
**32/32 ok** in 1,204 s. Nothing skipped (mtools and QEMU present; no row
needed the IBM ROM or Bochs).

| rows | what of this pass they reach |
|---|---|
| `dispfit`, `dispherc1`, `dispreboot`, `dispthm`, `dispcheck`, `cpup` | Set Primary through CTRL.DRV's `wm_rowcalc` and `vid_ctxcap`; the reboot and a mono boot through `bvw_rowcalc` -> `cfx_grid` |
| `dockpos`, `dockmodule` | `DOCK_APPLY`'s `_rowcalc` in the blob (`bdk`) and CTRL.DRV (`dock`) |
| `desksc`, `deskitem`, `deskwhole`, `stikboot` | the boot's grid and reflow (items 1); `stik_boot`'s reflow over the cells the boot laid |
| `small128`, `smallboot`, `desksmall` | kern_small's blob copy, with `desk_reflow`/`desk_live` written in |
| `coldslide`, `rombig`, `bootsmoke`, `kzboot`, `blobsum` | `.cfx` after the slide on a mono machine, `.cfx` beside a ROM, the full blob booting, the packed kernel |
| `dualcheck`, `dispfsx`, `dispdrag`, `dispmcfs`, `fsxdisp`, `dispstrad`, `extdmod` | `ext_ctxcap` (Extend), `vid_ctxcap` (Single), the blob's `cfx_ctxcap` (item 3) |
| `vesa8`, `vesacp` (QEMU) | `vx_boot_x`'s three `drv_call`s through `bvs_cgo`, CTRL.DRV's `cp_vid_vxcall` (item 2) |
| `xmcheck`, `hibernatedrv` | `xm_boot_x`'s and HIBER.DRV's `drv_call` hops (item 2) |
| `vmmouse` (QEMU, kern_emu) | `vmm_boot_x`'s hop, and kern_emu's 14-sector blob with `.cfx` |

* **Broken on purpose**: `cfx_grid` made to `retf` at its first byte (the
  boot never computes the grid) - **`deskitem` went red**: the kernel never
  reached the desktop in 180 guest seconds. Restored, rebuilt, green.
* Every batch: `make -j1` (the fast tier, 66/66 incl. `hotpar`, `t_canary`,
  `blobruns`, `ovlchk`, `asmrules`), `make -j1 small`, `make -j1 emu`,
  `make -j1 mega`. `cmp` of kern_small's `kernel.bin` across item 3:
  identical.

## DEFECTS FOUND

None.

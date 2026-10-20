# ksp13 agent "xfile" - the cross-file list, after the merge

Kernel size pass 13, cross-file agent. The eight concept and whole-kernel
agents each left a *CROSS-FILE* list of items that sat in another agent's
lines. This agent took every item still valid on the MERGED tree, kept the
kern_big boot blob inside its eleven sectors, and put every hot routine back
at an even shift against the pass's base. Branch `ksp13-xfile`, cut at
`a7f9ac2e` (all eight branches merged). The pass's base is `aedadd25`.

Figures are ASSEMBLED bytes: `tools/kernsize.py --json` absolutes, with
resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`. A module figure
is its UNPACKED image as `os88mod` prints it. Its claim is the image rounded
up to whole KB (`mem_bytes_kb_x`), and its nobits section has to fit inside
that rounding.

## BASE AND TIP

| | `aedadd25` (pass base) | `a7f9ac2e` (merge) | tip | tip - merge | tip - pass base |
|---|---:|---:|---:|---:|---:|
| **kern_big resident** | 92,757 | 91,712 | **91,552** | **-160** | **-1,205** |
| kern_big `.text` / `.bss` / `.cold` | | 43,084 / 5,218 / 37,716 | 42,994 / 5,218 / 37,646 | -90 / 0 / -70 | |
| kern_big `KERN_SIZE` | 98,816 | 97,792 | 97,792 | 0 | -1,024 |
| kern_big `.boot2` (kernsize, NOKZIP) | | 5,387 | 5,407 | +20 | |
| kern_big REAL blob (`boot2_end`, KZIP) | | 5,601 | **5,621 of 5,632** | +20 | |
| `BOOT2_SECS` | 11 | 11 | 11 | 0 (`KSIG_OFF` unmoved; `t_canary` and `t_blobruns` green) | |
| kern_big `.ovl` | | 5,103 | 5,118 of 5,120 | +15 | |
| **kern_small resident** | 60,260 | 59,624 | **58,909** | **-715** | **-1,351** |
| kern_small `.text` / `.bss` / `.cold` | | 31,391 / 3,004 / 22,361 | 31,121 / 2,978 / 21,942 | -270 / -26 / -419 | |
| kern_small `KERN_SIZE` | 62,464 | 61,952 | **60,928** | **-1,024** | -1,536 |
| kern_small `.boot2` / `.ovl` | | 4,379 / 1,477 | 4,387 / 1,489 | +8 / +12 | |
| **kern_emu resident** | 93,016 | 91,969 | **91,809** | **-160** | **-1,207** |
| kern_emu `.text` | | 43,341 | 43,251 | -90 | |
| kern_emu blob (kernsize) | | 5,487 | 5,507 | +20 | |

The modules, unpacked image (+ module bss) against the claim:

| module | merge | tip | claim | spare in the claim |
|---|---:|---:|---:|---:|
| `CTRL.DRV` kern_big | 13,049 + 163 | 13,128 + 163 | 13 KB | **21** |
| `FORMAT.DRV` kern_big | 3,017 | 3,022 | 3 KB | 50 |
| `DISKOPS.DRV` kern_big | 10,099 | 10,104 | 10 KB | 136 |
| `HIBER.DRV` | 6,145 + 592 | 6,145 + 592 | 7 KB | 428 |
| `DOCK.DRV` | 2,247 | 2,252 | 3 KB | 820 |
| `EXTD.DRV` | 1,406 | 1,406 | 2 KB | 642 |
| `WIN95.DRV` | 1,430 + 126 | 1,430 + 126 | 2 KB | 490 |
| `DESK.DRV` | 241 | 241 | 1 KB | 783 |
| `CTRL.DRV` kern_small | 4,395 | 4,878 | 5 KB | 242 |
| `FORMAT.DRV` kern_small | 1,136 | 1,141 | 2 KB | 907 |
| `DISKOPS.DRV` kern_small | 7,168 | 7,168 | 7 KB | **0** (exactly full, before and after) |
| `FILECP.DRV` kern_small | 1,855 + 80 | 1,855 + 80 | 2 KB | 112 |
| `FDLG.DRV` kern_small | 1,232 | 1,284 + **16** | 2 KB | 748 |

No module crossed a KB, so every module's heap claim is unchanged.

## TAKEN

Six commits. Each was gated by `make -j2`, `make -j2 small` and the fast
tier (47/47), with `make emu` at the tip. The `stkbalance` row is green at
every commit, and so is `tools/checkdocs.py`.

### 1. The small items on the list - kern_big -37, kern_small -224 (`ef93481a`)

* **kern_small's dead `.cold` and `.bss`** (wkgui's list for wkfs and
  wkcore): `fm_bar_gate_x` (its only caller is under `%ifndef KERN_SMALL`)
  and `fm_ncpye` are out of the small arm. So are the dead `clk_rb`,
  `clk_rbin`, `clk_rpep`, `clk_rp24` (gated `OS88_RTC`), `inst_parkreq`
  (`OS88_COMPACT`), `ico_rowadv`'s store with `ico_rowadv` and `ico_col`, and
  `vga_xorm` (`GFX_VGA`).
* **The system-write tier, gated** (`diskw.inc`): a new `DSKW_SYSW`
  (`DSK_STREAM` or `OS88_DRIVERS`) covers `dskw_syswr`, `dskw_pmask`, the
  clear in `dskw_wrp`, the `.notsys1` attribute block and the two `dwf_*_sys`
  far entries. A build without it never writes a system file. Its one
  protection test reads `[dskw_raw+DSK_R_ATTR]` against `DSKW_PROT` directly,
  which is the same answer.
* **`dsk_fatw_drop`** is `%ifdef OS88_DRIVERS`. It has two callers,
  `dsk_vol_del` (`OS88_DRIVERS`) and HIBER's sweep (kern_big). wkgui said to
  check its far references first: there are none on kern_small or kern_dos.
* **`fm_newwin`'s dead `mov al, KIND_FILES`** (misc's): -2 `.cold`, both
  kernels.
* **Strings that share a tail** (wkcore's and wkfs's): `sch_s_in` is
  `fm_s_colsp + 1` (-3, both kernels), and `fm_s_root` is `drv_t_ram + 4` on
  kern_big (-5).
* **`osapi_video`** is `mov dh, [vid_planes]` / `ret`, -7 on every kernel.
  wkgui suggested `mov dh, 1` for kern_small, but that changes nothing more:
  the plane count is the bpp on both arms, and one body serves both.
* **`ui_svc_open` and `cw_ui_svc_open` are one routine**: the far body is
  the routine, and the shim is gone from `kernel.asm`.
* **Three `ct_` trampolines priced per build**:
  * `ct_cw_wm_clip_test` exists on kern_big only, and `ct_cw_snd_beep` on
    kern_small only. Each kernel keeps the trampoline where it pays for its
    sites, and the other kernel's sites become plain far calls.
  * `ct_cw_task_yield` is gone, because `cw_task_yield` is already an `equ`
    of its cell.
* **`desk.inc`'s `push ds / pop es` before `cw_drv_pkg_call`** (-2): the X
  cell does it.
* **`desk_need` is QUERY's body** (desk's item): `osapi_wm_desk_x` is `or
  al, al / jz desk_need / jmp far [...]`, and `desk_need` ends in `retf`. The
  loader's step 5 calls it with `COLDCALL desk_need`, because `push cs` in
  `.cold` is refused by `os88ovlchk`.
* **`thmv_reap` folded into `drv_cp_closed_x`** (theme's item 1): the reap
  sits at the end of the panel's close, with every register already banked
  there. `thmv_reap_x` and `instance.inc`'s call to it are gone, and
  `cp_thmv_open`'s refusal makes its own two far calls. The order is
  unchanged: the drivers are told first, then the Customize window goes,
  then the settings write. CTRL.DRV +13.

### 2. The module-only far shims leave resident - kern_big -125, kern_small -83 (`15bfdf09`)

A `cw_*` shim (`call x / retf`, 4 bytes) that only an image or the boot calls
is resident for the benefit of code that is not. Each of three doors already
in the tree brings an image into a near routine without one:

* **`CTRL.DRV`'s `cp_kgo` / `cp_cgo`**: `call cp_kgo / dw target`, a 54-byte
  device whose shared tail builds the far frame and lands the target's near
  `ret` on `cw_kretf` or `cold_kretf`. Behind it are the macros `CPXK`
  (`.text` targets), `CPXC` (`.cold`), `CPXN` (a near call, for a target
  that is in the image on one kernel) and `CPXF`. The sites converted are
  the clock fields (`clk_fld_str`, `clk_fld_adj` and the four helpers),
  `cp_penlive`, `cp_vid_rowok`, `cw_vid_dual_ok` x2, the three `os88ui_*`
  far entries, `drv_cp_class_x` and `drv_cp_call_x` (`OS88_DRIVERS` only;
  kern_small keeps its far stubs), `vid_disp_relayout` (written out at its
  one site) and `ssf_cfg`.
* **`FDLG.DRV`'s `fdt_go`** shares `fdx_go`'s tail. It took five kern_small
  shims: `cw_app_close_win`, `cw_inst_fhome_idx`, `cw_menu_activate`,
  `cw_menu_draw_bar` and `cw_wm_pkgcall`.
* **`spw_near` and `wm_cbd`** (`call bp / retf` in `.text` and in `.cold`).
  With them, the boot's and `.ovl`'s single sites lost ten shims: menu
  relayout, ctx ptr, mem floor, vol slot, dock geom, `font_run_x`,
  `vid_disp_init`, `drv_find`, `ss_mins2idle`, and the kern_small idle
  task's `task_spawn`. So did one site in each of FORMAT, DISKOPS (kern_big
  only, because the small image is exactly 7,168 bytes) and DOCK (kern_big
  only). `cw_kretf` now labels the `retf` after `cw_app_close_win`'s call on
  both kernels.

This cost the blob +20 (5,621 of 5,632) and `.ovl` +15 (5,118 of 5,120).
CTRL.DRV grew +66 and is still in 13 KB.

### 3. kern_small's check-box and radio glyph live in `CTRL.DRV` - kern_small -207 (`a231bc0e`)

`os88ui_glyph` and its record-drawing helpers are resident `.cold` on
kern_small for the panel and FDLG's two check boxes. Two optional knobs in
`apps/os88ui.inc` move them into the image:

* `OS88UI_KGLYPH <section>` assembles the glyph block into that section. It
  is a macro-named section, so `os88ovlchk` does not read the block as
  crossing.
* `OS88UI_KFAR` makes the kernel arm's `UI_FRAME` / `UI_FILL` / `UI_PEN`
  emit far calls.

`fdlg.inc` sets them to `.modu` on kern_small. Packages never define either
knob, and `audio`, `artful`, `bounce`, `arkanoid` and `skies` were checked
byte-identical. kern_small CTRL.DRV +229. kern_big is byte-identical.

### 4. `FDLG.DRV`'s dialog state is the image's bss - kern_small -16 (`f0be8d41`)

wkfs's item for wkcore: `section .moddb nobits vfollows=.modd`, with
`MODD_BSS` and the KB-rounding assertion beside `.modpb`'s. There is also a
guard that `FDLG_MOD` is defined wherever `MOD_BSS` is, because `mod_need`'s
zeroing of a module's bss is that knob's.

Ten state variables moved, through `FDV(x)` (`[cs:x]` on kern_small and
`[x]` on kern_big), at 38 sites. `fdlg_ttl` and `fdlg_name` stay in `.bss`,
because a kernel routine reads them through DS. SPEC.md 38's table says so.

### 5. kern_small's adapter switch lives in `CTRL.DRV` - kern_small -187 (`417e585c`)

wkcore's largest item, the display-switch cluster, priced on kern_small
alone (kern_big is WANTS THE OWNER 1). On kern_small the only caller of
`vid_switch` is the Display page's Set Primary. On that kernel the walk out
of it reaches five routines no one else calls: `vid_avail_test`,
`vid_equip`, `vid_blank`, `wm_refit` and `wm_su_drop_all`.

Each is wrapped in `section KSW_SECT`, which is `.modu` on kern_small. Its
resident callees go through `KSWK x` (`call cp_kgo / dw x`; on kern_big it
is `call x`, so kern_big is byte-identical). Those callees are
`vid_blank_kind`, `cur_unlazy`, `vid_apply`, `vid_setmode`, `menu_force`,
`dock_force`, `wm_apply_size`, `wm_fs_setrect`, `wm_sz_notify` and
`wm_su_drop`. `desk_rowcalc` becomes a direct `call COLD_SEG:desk_rowcalc_x`
there, and its thunk is kern_big-only.

CTRL.DRV kern_small is 4,878, 242 bytes inside its 5 KB. SPEC.md 39.11.2
carries the paragraph.

**Equivalence**: no row drives Set Primary on kern_small; every Display-page
row is a kern_big row. So a scratch harness drove it on
`os8088_5150_both_gla` (Hercules + CGA), using the small system disk with
`build/apps360.img`. It opened a Disk window on B:, opened the Control Panel
on its Display page (record 2 on kern_small, not kern_big's `CP_IVID`), and
pressed Set Primary.

* CGA to Hercules: `[vid_kind]` 2 -> 1, both windows inside 720x348, and the
  Hercules raster lit (164,212 px).
* Hercules back to CGA: `[vid_kind]` 1 -> 2, and the Disk window's rect back
  to (103,20) 322x155 exactly. That is SPEC.md 39.11.2.1's identity, and it
  is `wm_refit`'s replay running from the image.

Screenshots of both adapters show a whole desktop with no corruption.

### 6. Parity pads - kern_big +2, kern_small +2 (`17cd36c4`)

These are one-byte pads that never run:

* on kern_big, after `osapi_video`'s `ret`, at `sch_s_in`'s old place, and
  after `app_close_win`'s `ret` (the last one shared with kern_small);
* on kern_small, a `nop` in `.cold` where `dsk_fatw_drop`'s body was
  (`os88ovlchk` refuses data in `.cold`).

On kern_big the three pads are +3, and `.text` grew by +2. The other byte
is `dsk_img16`, 22 -> 21: one of its jumps converged short under the new
layout (rdiff of the commit against its parent).

## PARITY

`tools/os88rdiff.py kernel aedadd25 --hot` over the brief's list (25 names on
kern_big and kern_emu, 22 on kern_small) leaves only routines whose BYTES
differ from the base. Each of those is the merge's, not this branch's:

* kern_big and kern_emu: `gfx_blit4` and `ui_task` changed, and
  `wm_paint_all` and `gfx_blit1` differ only through moved layout equs.
* kern_small: `font_run_x`, `cur_move_1` and `ui_task` changed, and
  `wm_paint_all` and `gfx_blit1` differ only through equs.

Every identical hot routine sits at an even shift on all three kernels.
Against `a7f9ac2e`, all 25 kern_big hot routines are identical at an even
shift. On kern_small, the three that differ through equs (`ui_task` -194,
`wm_paint_all`, `gfx_blit1`) do so only because `KERN_SIZE` and `COLD_SEG`
moved, and they still shift by an even amount. Nothing on a hot path was
edited, so cycles are unchanged.

## REFUSED

* **`ovc_buf` as nobits** (theme's 163 zero bytes in the blob, from
  wkcore's list): nobits buys no blob room. The area still has to fit in the
  5,632 bytes stage 1 reads, unless `mem_init` extends its reserve past the
  blob, and that extension is the enabler for WANTS THE OWNER 2. Not taken
  alone.
* **`icon_draw_ix` into `.cold`** (wkcore's, -4 for its shim): its tail
  `jmp` enters `.text`'s `ico_draw_common`, so a `.cold` copy needs a far
  frame that costs more than the shim.
* **`cw_vid_disp_of` into EXTD.DRV's hop**: kept for `OSAPI_WM_DISPLAY`'s
  cycles. It is a package-facing cell, and the hop is ~20 clocks a call.
* **`hbk_drv_blk_call` and `drvf_drv_call` through `spw_near`/`wm_cbd`**:
  BP is an input to both, and those doors spend BP.
* **`hb_pick_x`**: it has a resident caller (`hb_ok`, through `COLDCALL`),
  so it is not image-only.
* **`fm_dgdrop`'s `jne short .out`** (theme 2, wkfs's for misc): it is
  already short on the merged tree. The merge's re-layout converged it.
* **`os88ui_btn` into CTRL.DRV on kern_small**: the panel and FDLG would each
  need a copy, because the include cannot be assembled twice into one image
  pair. Two copies cost more than the resident one.
* **`os88ui_bhit` into FDLG.DRV on kern_small**: FDLG is `.cold` on
  kern_big, where the scan already accepts the shape. On kern_small the
  macro-named section that hides it would hide a real crossing too.
* **wkgui's `mov dh, 1` for kern_small's `osapi_video`**: superseded by
  TAKEN 1's one body for all kernels.

Already taken on the merged tree by their owners, so nothing was left here:

* `assoc_dfold` -> a compare, and `ico_need`'s redundant `mov byte [ico_n],
  0` (assoc's list, taken by wkfs);
* `cw_task_yield` and `cw_wm_timer` as `equ`s of their cells (misc's,
  taken by wkcore);
* `fm_c_uncto` and `fmv_icostale_but` on kern_small (wkgui's, taken by
  wkfs);
* the `ct_cw_mem_disp` site-count comment (theme 3).

## WANTS THE OWNER

1. **kern_big's display-switch cluster into CTRL.DRV**: ~450-580 bytes of
   resident `.text`/`.cold` on kern_big and kern_emu. This is TAKEN 5's
   shape: `vid_switch`, `vid_disp_init`, `vid_desk_union`, `wm_pref_take`,
   `vid_equip`, `wm_refit` and the rest of wkcore's list.
   * **Why not taken**: on kern_big this cluster is also reached from the
     blob, from `.ovl` (the boot's `vid_disp_init`) and from EXTD.DRV. So it
     needs two things at once, and neither is a build fix:
     * CTRL.DRV's claim going from **13 KB to 14 KB**. It has 21 bytes
       spare, so +1,024 bytes of heap is held for as long as the panel is
       open.
     * a boot-time copy or a twelfth blob sector, because the blob has 11
       bytes left.
   * ESTIMATED from the symbol spans; not built.
2. **kern_big's check-box/radio glyph into CTRL.DRV** (TAKEN 3's shape): -207
   resident, and the same 13 KB -> 14 KB CTRL claim. The knobs are already
   in the tree, so it is a define in `ctrl.inc`/`fdlg.inc`'s kern_big arm
   plus the claim. MEASURED on kern_small; the kern_big figure is the same
   code.
3. **Room in the blob**: `ovc_buf` as nobits plus a `mem_init` reserve that
   covers it would return ~163 bytes of blob (theme's figure) and ~-36
   resident in the shims that could then move to `spw_near`. It is an
   enabler for 1, not a saving on its own. ESTIMATED.

## WANTS AN ABI BREAK

None. Every change is kernel-internal: the `cw_*` shims, module entry
tables that `os88mod.py` checks on both halves, and `.bss` layout. The
`apps/os88ui.inc` knobs are opt-in, packages do not define them, and every
package checked is byte-identical.

## CROSS-FILE

This branch's whole subject was other agents' lines, so for the coordinator:

* **`ctrl.inc`** carries this branch's `CPXK`/`CPXC`/`CPXN`/`CPXF` sites and
  the `cp_kgo`/`cp_cgo` device. Any merge that also touched the clock page,
  the Display page or the Drivers page's class column should read them.
* **`fdlg.inc`**: every one of its ten state variables is spelt `FDV(x)` now.
  A new state byte belongs in `.moddb` (kern_small) unless a kernel routine
  reads it through DS.
* **`vidsel.inc`/`wm.inc`**: the `KSW_SECT` wrappers. A new caller of
  `vid_equip`, `vid_blank`, `wm_refit` or `wm_su_drop_all` from RESIDENT
  code on kern_small would be a near call into CTRL.DRV, and
  `os88ovlchk.py` refuses it at the build.
* **desk's `tools/os88rdiff.py` include-path fix** is still not in the
  tree. Every rdiff number above was taken without it, and none needed it.

## ROWS RUN

One scoped pass after the last code commit:
`python3 tools/os88test.py soak -k <row> ... --marty-jobs 1` over the 32 rows
that reach what changed:

**29 passed, 3 failed** (1,505 s):

* **ok**: kdreturn, kdreturnf, smallboot, cpup, dtfield, saver, wirezone,
  dispfit, thm95 (145.8 s against a declared 75), curshape, dispmode,
  cpnames, lzdrv, kzboot, drvcall, drvup, msel, fdlgchoose, fmthumb,
  smalllaunch, tmsmall, deskcalcards, paint1small, hdauto, fmtlow, fmtreach,
  imgmount, wimgtrip, colorpresets.
* **FAIL hibernate, hibernatem, desksc**: all three fail at `a7f9ac2e` too,
  as `os88bisect.py classify --base a7f9ac2e -n 2` shows (hibernate 2/2 failed at the tip and 2/2 at `a7f9ac2e`, so it is pre-existing;
  desksc the same, 2/2 and 2/2; `hibernatem` is `hibernate.py` with a flag, so it fails the same way). Each is a stale mirror in the ROW, not a kernel fault; see
  DEFECTS FOUND.

Plus the kern_small Set Primary harness in TAKEN 5, which is not a row.

## DEFECTS FOUND

None in the kernel. Two rows mirror a kernel fact that moved before this
branch. Neither is this branch's file; both are for the coordinator:

* **`tests/hibernate.py` mirrors `KIND_HIBER = 5`**, and the kernel's is
  **3**. `a123653b` (#264, Timer and Bounce became packages) renumbered the
  kinds, which is before even the pass base `aedadd25`. So `button1()` finds
  no Hibernate instance and raises `IndexError`, although the window opened.
  That breaks `hibernate` and `hibernatem` on every tree since #264. The fix
  is one line, or better, reading the equate through `os88geom` the way
  `WIN_SIZE` already is.
* **`tests/desksc.py` leg G waits for `FS_CWD == 4`**, MEDIA's first cluster
  on `apps360.img` as it once was. On the merged tree `ASSOC.DAT` is 2,236
  bytes, so it takes three 1 KB clusters and MEDIA is at **5**. The
  coordinator's own `build/apps360.img` at `a7f9ac2e` reads the same. The
  row already computes `media = ui.entry("MEDIA", root)` two lines above and
  never uses it; comparing against that instead of the literal is the fix.
  The guest's own report is the Disk window titled MEDIA with `FS_CWD` 5,
  which is the feature working.

The harness surprise: `tests/dispfit.py` pointed at kern_small on
`os8088_xt_vga` fails its boot gate. The gate expects a VGA desktop, and
kern_small runs that card in mode 6; the machine boots to a whole desktop,
which a screenshot confirms. That is the row's machine choice, not a
regression, and no kern_small row uses it.

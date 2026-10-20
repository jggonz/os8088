# ksp12 agent "xfile" - the cross-file follow-up on the merged tree

Kernel size pass 12. Branch `ksp12-xfile`, cut from `kernel-size-12` at
`26c45e1d` (six of the seven concept agents merged), and merged with
`4cb1b15d` (the round agent's branch, the seventh) half way through, which
lifted the barred list. Figures are `tools/kernsize.py --json` sections,
ASSEMBLED bytes; resident = `.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf`. The 4cb1b15d figures are `tools/os88rdiff.py`'s section sizes at
this branch's build number (a `git archive` tree builds with no build number
and reads 10-12 bytes of `.text` short, so it is not used for any figure).

## BASE AND TIP

| | text | bss | cold | lowbss | vgabuf | resident |
|---|---:|---:|---:|---:|---:|---:|
| kern_big at `26c45e1d` | 43,692 | 5,204 | 38,361 | 5,598 | 336 | **93,191** |
| kern_big at `935ddbf1` (this branch, before the merge) | 43,694 | 5,204 | 38,316 | 5,598 | 336 | 93,148 (-43) |
| kern_big at `4cb1b15d` | 43,660 | 5,204 | 38,355 | 5,598 | 336 | **93,153** |
| **kern_big at the tip** | 43,640 | 5,204 | 38,307 | 5,598 | 336 | **93,085** (-68 from 4cb1b15d) |
| kern_small at `26c45e1d` | 31,947 | 3,063 | 22,445 | 2,868 | 0 | **60,323** |
| kern_small at `935ddbf1` | 31,947 | 3,063 | 22,418 | 2,868 | 0 | 60,296 (-27) |
| kern_small at `4cb1b15d` | 31,901 | 3,063 | 22,445 | 2,868 | 0 | **60,277** |
| **kern_small at the tip** | 31,870 | 3,063 | 22,414 | 2,868 | 0 | **60,215** (-62 from 4cb1b15d) |
| kern_emu at `26c45e1d` | 43,951 | 5,204 | 38,361 | 5,598 | 336 | 93,450 |
| kern_emu at `4cb1b15d` | 43,919 | 5,204 | 38,355 | 5,598 | 336 | 93,412 |
| **kern_emu at the tip** | 43,899 | 5,204 | 38,307 | 5,598 | 336 | **93,344** (-68) |

All of this branch's own work is the 4cb1b15d -> tip column: **kern_big -68,
kern_small -62, kern_emu -68** (26c45e1d -> tip, the round agent's merge
included, is -106 and -108). Outside the resident count, kern_big: `.boot2`
4,886 -> 4,873 (-13), `.ovl` 5,102 -> 5,114 (+12, the boot store below);
kern_small's are unchanged. `KERN_SIZE` unchanged (98,816 / 62,464). ROM
arm: `make rom` at the tip - `.cold` 38,307 in the 40,944-byte window, and
`osrom-big.bin` reports 62 bytes of the window spare with FORMAT.DRV in it.

Module images (unpacked, `os88drv.image_unwrap`): **CTRL.DRV (kern_big)
12,113 -> 12,089 (-24)**, its settings core `.modc` 2,639 -> 2,615 - no KB
of claim crossed (12 KB either way). kern_small's CTRL.DRV 4,383,
DISKOPS.DRV **7,168 exactly** (not grown), FDLG/FILECP/FORMAT: unchanged.
No other image moved.

## TAKEN

### 1. SK_KERN stored at boot (kern_big -8 as a word, -7 as shipped)

rom's cross-file item. `osapi_sys_kb_x` took `SK_VGAB_KB` off `KERN_KB` with
`test [vid_avail] / jnz / sub` - 10 bytes of `.cold` on every call, for an
answer `[vid_avail]` fixes at boot (mem_floor_ax's own argument). The note
said `SK_VGAB_KB` was 0 and the arm did nothing; **at 26c45e1d it is 1** - it
flips with every 512-byte rung of `KERN_SIZE` (it is 1 exactly when the
cumulative boundary is an odd number of rungs) - so the arm is needed and
cannot be `%if`'d (the constant is defined after memory.inc, from the
layout). `mem_init_x` (the boot overlay) now STORES the figure once, and
`osapi_sys_kb_x` reads it. Shipped as the byte `vga_skkern` widened with
`cbw` (+1 against a word, see item 4); kernel.asm asserts `KERN_KB < 128` on
a `GFX_PLANE` kernel. kern_small has no rung and reads the constant.
`tests/vgadrop.py` now asserts the byte on all three adapters (97 / 96 / 96)
and went RED with the store made one low. SPEC.md 39.22 says so.

### 2. `fm_fmt_ok` answers the image question (big -5, small -2)

imgmount's item. `fm_fmt_ok` banked an AX nothing in it writes (-2, both
kernels). On kern_big a "no" now carries ZF = 1 for a mounted image's volume
(`cmp [bx+DV_CLASS], DRVC_IMG` on the non-BIOS arm, `inc bx` making ZF = 0
on the other two noes - BX is 0 on a free row's refusal and a row pointer
on a BIOS hard disk), so `fm_bar_gate_x`'s second `dsk_vol_row_x` of the
same row for Unmount Image's greying went (-3). Equivalence: an image row is
`DVK_DRV` of `DRVC_IMG` (diskops.inc), so "kind not BIOS and class IMG" is
the old "lookup ok and class IMG". `imgmount` leg 8 (Unmount grey on the
host's window, live on the image's) is the row.

### 3. Single-caller routines written at their call (big -56, small -51)

A scan of every top-level label referenced exactly once, by a near `call`,
from the same file, with one `ret` at its end and no fall-through into it
(three of the first candidates, `app_tmr_stamp`, `fm_mfocus` and
`dskw_fatclean`, ARE fall-through targets and were dropped), no name in
`tests/` or `tools/`, and - with two exceptions whose SPEC.md and comment
mentions were rewritten - no name in the documents:

| routine | into | big | small |
|---|---|---:|---:|
| `dsk_sanit` | `dsk_synth_name`'s copy loop (a call a character less) | -4 | -4 |
| `dsk_ncmp` | `dsk_sortdir`'s inner scan (a call a comparison less) | -4 | -4 |
| `dsk_bpb_slot` | `dsk_bpb_bank`, its refusal the bank's own jump | -6 (item 4's byte) | -7 |
| `dskw_cmp` | `dskw_find`'s entry loop | -4 | -4 |
| `dskw_take1` | `dskw_xclus`'s head, both exits straight to the caller's | -6 | -6 |
| `fcp_lbegin` | `fcp_paste` (FM_MSEL: kern_big) | -6 | - |
| `fm_selfold` | `fm_c_pastein` | -4 | -4 |
| `menu_tbar_k` | the toast strip; its CX bank was already there | -6 | -6 |
| `kbm_paint` | `kbm_move`: BX is kbm_key's bank and AX was spent on the step | -8 | -8 |
| `wm_win_hit` | `wm_dmg_wins`: `mov di, bx` after the pops is the restore | -8 | -8 |

Each loop that held one pays a call and a return less a turn. `kbm_move`
has no row: a scratch harness (seven keypad arrows on MartyPC's CGA 5150,
4cb1b15d against the tip) read identical `mouse_x`/`mouse_y` and identical
framebuffer hashes after every press. **Found on the way, not changed**:
`kbm_key`'s header says every register is preserved, and the move path has
never kept AX (`kbm_move`'s `imul`); both callers - `kbm_poll` and ui_task -
bank or reload it, so nothing depends on the header.

### 4. Parity: the hot code back where pass 12 found it (big +2, small 0 - both already inside items 1 and 3)

`os88rdiff kernel 47578d58 .` at 26c45e1d had **every hot `.text`
primitive at an ODD shift** - gfx_blit4 and gfx_fill (-11), font_run, both
mouse ISRs, kbm_isr, cur_draw, sch_isr, sch_switch, evq_pop (-29) - and
dsk_xfer, dsk_runcap and the FAT walk in `.cold` (-97..-107). The `.text`
one is theme's 7-byte theme row (the pad byte went, at `thm_tab`); the
`.cold` one is spread over hma's and rom's. What was done, all against
47578d58:

* `vga_skkern` is a BYTE placed before `gfx_color` (+1 `.text`), and the
  `cbw` it costs is a byte of `.cold` before dsk_xfer: both runs even.
  disk.inc's `align 2` took back the pad the word had used, so this costs
  +1 net against item 1's word.
* `menu_dropd` moves on kern_big from the end of `menu_drop` to just above
  `wm_hit_ttl_b` (0 bytes): the 288 routines between - wm_paint_all, the menu
  bar's clock - had been odd since wm_hit's `call far` to the theme door.
* After the merge and item 3, `dsk_bpb_bank` keeps `cmc / jc .no` on
  kern_big (+1 against the `jae` kern_small uses): dsk_xfer, dsk_runcap,
  dsk_next_clus_x even on kern_big; kern_small's are even with the `jae`.

**At the tip, kern_big**: gfx_blit4 -24, font_run_x -42, mou_p2_isr -42,
kbm_isr / mou_isr_body / mou_apply / cur_draw / sch_isr / sch_switch / evq_pop
-50, ui_task -74, dsk_xfer -118, dsk_runcap -132, dsk_next_clus_x -144 - all
even. **kern_small**: gfx_blit4 / font_run_x -10, the ISRs and the scheduler
-18, ui_task -70, dsk_xfer / dsk_runcap -54, dsk_next_clus_x -62 - all
even. What is still odd and left: on kern_big, `blk_act .. wm_hit` and
`wm_dock_under .. fpg_arm` (wm_paint_all -59) since the round agent's
`[blk_on]` byte moved from blank.inc into viddet.inc; `api_coldseg .. kmain`
(+3, the API's own helpers and data words - which are now at EVEN addresses
where 47578d58 had them odd); `ui_bill .. ui_lit_go` (7 routines). None is
on the brief's hot list; a byte moved from after fpg_arm to before blk_act
would take the wm one for 0 bytes and is the next pass's if it wants it.

### 5. `wm_noanim` without the zoom (small -9)

The round agent's item. `OSAPI_WM_NOANIM`'s bit has one reader, the zoom
(`wm_an_ok`), so on a kernel without `WM_ANIM` - kern_small, and an
`ANIMOFF` build - the body is `ret`, exactly as `OSAPI_WM_QUIET`'s is on
kern_small: the cell stays (SPEC.md 20.8 rule 4), every register and the
flags are kept. SPEC.md 11.99.2.1 says so. `alertanim` green.

### 6. CTRL.DRV and the boot blob (CTRL.DRV -24, `.boot2` -13)

* **desksc.inc `sc_m_new`** (ctrl's item, -11): `.bad` and `.noroom` moved
  between the path and the caption code, so both `jc .bad` and `jc .noroom`
  are short, the second `jc .bad` is a `jnc .path` over them, and the caption
  falls into `.done` without its `jmp short`.
* **`CFG_SHARED`'s two bit walks as one** (files' item; -13 an instance,
  not the -1 estimated): `_pack` builds the WANT and ANS bitmaps in one
  inline walk and the two routines went. The instances are CTRL.DRV's and the
  boot blob's (`.boot2`), both kern_big's.

### 7. Defects

* **FIXED - `.img`'s past-the-end refusal left `[dsk_ioerr]` stale**
  (imgmount's report). It now stores 04h, int 13h's SECTOR NOT FOUND,
  through the store the write-protect refusal already had: 0 bytes, and the
  READ path's `jz .ird` taken (16 clocks) became a `jnz` not taken (4).
* **FIXED - `tests/shutdown.py`'s write-protect injection** (ctrl's report):
  it wrote 5 bytes over the 3-byte `cp_cfg_save` jump at the end of the
  settings core. It patches `cpc_save`'s body now (six one-byte pushes, no
  label among them) and asserts that no `cpc_save.*` label is inside the
  patch. `shutdown` green.
* **SPEC.md 2.8.7's `MODS_SIZE`**: 2,615 bytes, 6 sectors at the tip (2,650
  at 26c45e1d); the whole image 12,089.

## REFUSED

* **The five `cmp word [api_coldseg], ...` as byte compares** (hma's item):
  the three `COLD_HOME` ones were already taken by rom/hma; the two left
  compare against `HMA_SEG` = FFFF, which NASM encodes with a sign-extended
  imm8 - `cmp word [m], 0xFFFF` is 5 bytes, the same as the byte form. 0.
* **`dkf_apply` / `thf_apply` / `exf_sync` through one mod.inc helper**
  (theme's item, ~8-12 estimated): the three are not one shape. dkf hooks
  through `[DKFP]` and unhooks through `[DKFP+4]`, thf through ONE entry
  with the direction in CF, and exf has no hook at all (need or drop). A
  helper taking the FP address in BX (`jc .off / call mod_need / jc / call
  far [bx] / ret / .off: call far [bx+4] / call mod_drop / clc / ret`) is
  ~17 bytes and saves ~10 in dkf and ~8 in thf, and thf would need WIN95.DRV
  to grow an unhook entry - +4 bytes of resident slot `.bss` and its header.
  Net about +4. exf_sync's own shape was re-laid twice and came out equal.
* **`osapi_sys_kb`'s ROM arm folded into the boot store** (~-8): `[api_coldseg]`
  is final before `mem_init_x` on a ROM machine but NOT on an HMA one -
  `xm_boot_x` puts `.cold` in the HMA from `drv_boot_x`, after it. The tail
  re-points words through `tools/os88rom.py`'s tables; teaching it this byte
  is the ROM concept's to weigh.
* **`cpc_fdtest`'s relaxed `jc .out`** (ctrl's item, "up to -3"): every
  arrangement is 9 bytes - the exit is `pop es / jmp kretm_bp` and the free
  at `.done` needs the claim, so neither end can be shared.
* **`fm_fmt_ok` returning the row instead of the ZF**: BX = 0 on a refused
  lookup, and the class compare would then read `ds:DV_CLASS` of the kernel's
  own first bytes.
* **More single-caller routines**: the scan's other resident candidates are
  named in SPEC.md or a plan (cur_mvrow/cur_mvcols, gfx_blit_run,
  gfx_pt_resolve, wm_su_owed, ui_drag_dead, drv_nostamp, the ico_* family,
  fm_sb_thumb, fm_isdown, ...), are reached through a MACRO expanded at more
  than one site (`dskw_clba_inc`/`_set`, `gfx_disp_enter_cd`,
  `gfx_denter_rect`), share a header with a twin (`dsk_fatw_pinned`), or sit
  in a layout a previous pass tuned for short reach (`wm_su_piece`, whose
  trampoline `wm_su_tno` is placed after its `ret` on purpose). About 4
  bytes each; the documented ones are the next pass's if it rewrites the
  mentions.
* **Relaxed jumps**: at the tip the listing scan finds 68 on kern_big and 22
  on kern_small (71 / 22 at 26c45e1d - the three were desksc.inc's). The
  resident ones are pass 11's remainder table unchanged (vga12.inc's adapter
  dispatch and gfx_blit4's loops, font_run, wm.inc's caption refusals,
  ui.inc's two, loader.inc's 0-net one, os88ui_btn) plus three in files.inc
  (`je dsk_get_icon_x` - a target in another file; `je .swap` and
  `je .fs_st_swap` - status ladders a previous pass laid out, where moving
  the arm pushes `.clone`/`.commit`/`.cancel` past reach) and filecp.inc's
  `jc .jerr` (0 net). The others are module images (HIBER.DRV 16, DISKOPS.DRV
  9 with compress.inc's 7, WIN95.DRV 4, CTRL.DRV's boot-overlay twin
  `ovc_deser`'s `jb .bad`, 7 bytes out of reach, in `.boot2`), where a saving
  is disk bytes: HIBER.DRV is 5,528 against a 6 KB claim, DISKOPS.DRV 10,079
  against 10 KB on kern_big and 7,168 exactly on kern_small - none is near a
  KB boundary. Not swept.

## WANTS AN ABI BREAK

None found.

## CROSS-FILE

* **The wm/ui parity runs** in item 4 (0 bytes by moving a `.text` byte;
  the candidates are a byte variable defined after `fpg_arm` moved to before
  `blk_act`).
* **`kbm_key`'s header** promises every register on a path that has never
  kept AX (item 3). Either the header says so or the path banks it (+2).
* **The documented single-caller routines** (REFUSED) - each wants its
  SPEC.md mention rewritten with it.

## ROWS RUN

All MartyPC unless named, `--marty-jobs 1`, on this branch's own build with
the tree not being edited.

| after | rows | result |
|---|---|---|
| batch 1 | vgadrop, shutdown, cfgtrip, drvup, desksc, hdauto, imgmount, wimgtrip | 8/8 ok |
| batch 1 | vgadrop with the store made one low (broken on purpose) | RED, as it should be |
| batch 2 | vgadrop, imgmount, wimgtrip, diskclone, fmtlow, fmtreach, thm95, fddpage, dispreboot, uiblock, ps2mouse (QEMU) | 11/11 ok |
| batch 3 | fcpcopy, fcpapi, fcpsmall, fcproom, msel, wseq, dosseq, czseq, dirwsize, fatwpin, smallboot, small128 | 12/12 ok |
| batch 4 (after the merge) | alertanim, toastbar, toast, zonedmg, dmgcull, tmdmg, msel, smallboot, dispclose, dispclose-small, deskpen | 11/11 ok |
| tip | rombig, romsmall, romnodisk, hmacold (QEMU), coldpic, vgadrop, kzboot, cpup | 8/8 ok |

Gates at every batch: `make -j2` (its fast tier 43/43), `make -j2 small`,
`make -j2 emu`, `tools/checkdocs.py` clean; `make rom` at the tip.
`tools/stkbalance.py kernel/kernel.asm kernel/*.inc`: **0 unbalanced** at
26c45e1d (4,012 entries) and at every batch (4,001 at the tip - eleven
routines fewer).

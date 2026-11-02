# ksp15 agent "gui" - the window, menu, drawing and scheduler side

Kernel size pass 15 (`docs/plans/completed/ksp15/BRIEF.md`), section 1's `gui`
row: a general finder over `wm.inc`, `ui.inc`, `menu.inc`, `mouse.inc`
(the cursor is in it), `font.inc`, `vga12.inc`, `softgfx.inc`, `toast.inc`,
`fprog.inc`, `sched.inc` and `instance.inc`. Branch `ksp15-gui`, base
`49ad5d67` (the brief, on `e4896481`). Figures are ASSEMBLED bytes,
`tools/kernsize.py --json` absolutes (kern_mega off `make mega`'s
`kernsize[mega]` line); resident = `.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf` (+ `.mega` on kern_mega).

## THE INSTRUMENT

Scratch scripts over `tools/os88rdiff.py`'s `Build` and `parse_map`, on a
`-Le -Lf` listing assembled WITH its own `[map all]` (a listing read against
`build/kernel.map` misattributes 3,743 routines - the map is pass 2's, the
listing is not). Not committed; the shapes are what is worth keeping:

* **prologue/epilogue census**: every routine in the area whose entry push
  run is in the ladder's canonical order (gaps allowed) and how each exit
  leaves - pops and a `ret`, or a `kret_*` rung. A gap register is only
  safe to add when it is not an OUTPUT, so every candidate was read.
* **skip-byte census**: every `jmp short` over one or two bytes
  (`EB 01`/`EB 02`), and every `clc/ret, .x: stc/ret` and
  `clc/jmp short .out, .x: stc` pair. Each becomes an opcode whose
  immediate IS the skipped instruction - `test al, imm8` (A8) where the
  flags are dead or CF = 0 is the answer anyway, `test ax, imm16` (A9) over
  two bytes, `mov al/cl, imm8` (B0/B1) or `mov cx, imm16` (B9) where the
  register is dead or comes back off the stack, which writes NO flag.
* **known-carry census**: a `clc`/`stc` every one of whose predecessors
  (fall-through and each branch to it) already leaves CF at that value -
  `jnc`/`jae`/`ja` arrive with CF = 0, `jz` after `test`/`or`/`and`/`xor`
  or an equal `cmp` likewise. It found the area's two (TAKEN 4) and eight
  outside it (CROSS-FILE).
* **reload census**: the same `mov reg, [mem]` twice within a few lines,
  read for a reorder that keeps the first load.

## BASE AND TIP

| | base `49ad5d67` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 90,008 | **89,862** | **-146** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 41,136 / 5,398 / 37,780 / 5,358 / 336 | 41,002 / 5,398 / 37,768 / 5,358 / 336 | -134 / 0 / -12 / 0 / 0 |
| kern_big `.boot2` / `.ovl` / `.vga` / `KERN_SIZE` | 6,447 / 5,110 / 2,032 / 97,280 | 6,447 / 5,110 / 2,032 / 97,280 | 0 |
| **kern_small resident** | 58,220 | **58,086** | **-134** (`.text` 30,802 -> 30,672, `.cold` 21,581 -> 21,577) |
| **kern_emu resident** | 90,264 | **90,118** | **-146** (`.text` 41,392 -> 41,258, `.cold` 37,780 -> 37,768) |
| **kern_mega resident** (with `.mega`) | 124,192 | **124,046** | **-146** (`.text` 47,548 -> 47,414, `.cold` 44,957 -> 44,945; `.mega` 18,237 both) |

No module, driver, `.bss`, `.lowbss`, blob or `.ovl` byte moved. `.text` is
even at every commit on both gated kernels; `hotpar` is green at every
commit (kern_big and kern_small), and the fast tier passed at every commit
(`make -j1`, which runs it, with `stkbalance` in it). `make emu` and
`make mega` build at the tip. **The parity rule bit three times and each was
answered by pairing two odd changes inside one hot-free interval** (the
intervals that matter in my area are `[gfx_sub_st, font_run)`,
`[sched_stop, sch_isr)`, `[wm_db_r, wm_paint_all)`, `[wm_paint_all, ui_task)`
and `.cold`'s `[gfx_blit1_x, dsk_xfer)`), or by one pad (below). **Note for
the coordinator: `disk.inc`'s `align 2` after `dsk_img16` swallows an odd
`.text` delta from everything above it** - an odd total before it reads as
one byte less saved than the routines add up to.

## TAKEN

Bytes are kern_big's per routine, as `os88rdiff`-style map deltas read them
(kern_emu and kern_mega are kern_big's on every item; kern_small takes the
same items where it assembles the routine - not `thm_pen_live`,
`wm_chrome_rect_x`/`_relit_x` or `wm_hit_ttl_b`, which are kern_big's, and
it has `wm_hit`'s inline copy of the last instead). No item touches a routine on the brief's hot list; where an
item adds cycles the count is given, and where it is on the damage-repaint
path it is cycle-neutral or faster (see WANTS THE OWNER for the ones that
were not, and were backed out).

### 1. Prologues onto `kent_di` and the shared ladder

`call kent_di` (3 bytes) for a five- or six-push run and `jmp kret_di` (3)
for a pop run and `ret`, where the routine runs once per OPERATION -
kernel.asm's own rule for `kent_*`. ~90 clocks a call over the inline
pushes (`call`, the five pushes, the return-address shuffle, `ret`).
A register the old run did not save is saved now; each such gap was checked
not to be an output.

| routine | kern_big | per |
|---|---:|---|
| `wm_fit` (+ three push/pop pairs its frame made dead, and a second `sub cx, ax`) | -13 | placement |
| `wm_zoom` | -3 | zoom |
| `wm_flush_ck` (and a B0 over its clc, item 3: -4 in all) | -3 | snap test |
| `wm_hide_b` (+ the push/pop DI round wm_su_take) | -4 | hide |
| `wm_rz_paint` | -2 | resize/zoom commit |
| `wm_band` | -2 | `OSAPI_WM_BAND` |
| `menu_ab_cell` (+ a second load of `[menu_win]` already in BX) | -8 | relayout |
| `menu_item_xor`, `menu_title_xor` | -1, -2 | highlight change |
| `toast_place` | -2 | bar composition |
| `ui_drag_phase` | -2 | drop |
| `inst_set_name_x` | -3 | launch |
| `gfx_scroll` | -3 | scroll call |
| `sw_pairbuild` | -3 | once a boot |
| `mouse_unhook` | -2 | restart |

`wm_obscured` and `wm_covered` shared `wm_fit`'s and `wm_zoom`'s pop tails;
they push the ladder's frame now (+1 each, ~27 clocks) rather than taking
`kent_di`, being per-window on the damage path.

### 2. Four-word copies through `wm_bank4`

`ui_grow` banked W_X..W_H into `ui_rzx` with `push cx / lea / mov / mov cx,4
/ push ds / pop es / cld / rep movsw / pop cx` (16 bytes): `mov di, ui_rzx /
call wm_bank4` (6), which keeps CX and SI; SI and DI were dead and ES is
DS on both arms (`ui_track` set it). -10. `ui_track`'s second copy, with DI
already at `ui_curx`: `call wm_bank4` for `lea / mov cx,4 / rep movsw`, -5.

### 3. Skip bytes in place of short jumps

| routine | opcode | over | why it is safe |
|---|---|---|---|
| `wm_dmg_bands` (both arms), `wm_dmg_shadowed`, `wm_su_frag`, `wm_dc_ok`, `fpg_arm`, `wm_pref_take`, `wm_dmg_stale` (`.mark`) | `CLC_OR_STC` | the refusal's `stc` | every caller reads CF alone |
| `wm_su_vset`, `gfx_sub_arm`, `app_launch` | `CLC_OR_STC` | `stc`, and the `clc` + jump before it | ditto (-2 each) |
| `wm_su_bytes`, `wm_su_scrset`, `wm_su_ck`, `wm_su_try`, `clip_put`, `wm_ownseg` | A8 | `stc` | CF = 0 already from an `or`/`xor`/`cmp`, kept by `test`; CF is the answer (`OSAPI_WM_OWNSEG`'s and `OSAPI_CLIP_PUT`'s too) |
| `evq_pop`, `wm_wake` | A9 | `popf / stc` | `test` clears CF, the answer; the `clc` goes too (-2 each). `OSAPI_WM_WAKE`'s contract is CF |
| `wm_dock_snap`, `wm_dmg_stale` (`.exposed`), `wm_flush_ck` | B0 | `clc` | `mov al` writes no flag; AL comes back off the stack |
| `app_launch` | B9 | `call bp` | CX comes back at `kret_es` |
| `menu_relayout` | A8 | `pop cx` | flags dead at `.done` |
| `menu_bt_raw` | A8 | `lodsb` | AL kept; the `or al, 0x80` that follows rewrites every flag |
| `wm_min_ax` | A9 | `xor ax, ax` | every caller sets its own flags off AX |
| `wm_fs_zpos` | A8 | `pop cx` | drops the banked SI into CX, which `kret_cx` restores; replaces `add sp, 2` and a jump (-3) |

Each is the same or fewer clocks than the jump it replaces (a taken short
jump is 15; `test al, imm8` 4).

### 4. Carries already known

* `wm_fs_vis`: every jump to `.no` follows an `or` or a `test`, which clear
  CF, so `.no`'s `clc` and the second epilogue went: `.yes: stc / .no: pop
  bx / ret` (-3).
* `menu_activate`: `.same` is reached only by `je` off an equal `cmp`, CF
  = 0 (-1).

### 5. Reuse what a register already holds

* `wm_dmg_union`: x then y, so each origin is loaded once and the far edge is
  an `add` on it - the four updates are independent (-6, faster on the
  damage path).
* `wm_hit_ttl_b` (kern_big) and `wm_hit`'s inline copy (kern_small):
  `.minbox` is reached only from the close-box test, whose AX is already
  `W_X + 18 - x`, so `add ax, [bx+W_W] / sub ax, 27` is the minimize box's
  (-5).
* `wm_draw_title(_b)`: `wm_tbox` hands back AX = W_X+8, so the minimize box
  is W_W less 29 on it (-3).
* `ui_grow`'s damage union: x1 and x2 off one load each of W_X and
  `ui_curx` (DI is dead there; -4).
* `wm_destroy`'s empty rect: `xor ax,ax / cwd / mov bx,ax / mov cx,ax / inc
  ax` (-1).

### 6. Smaller bodies

* `thm_pen_live` (kern_big): `mov byte [gfx_dis], 0 / jmp short thm_ink`,
  thm_ink's own door for the pen (-4; ~40 clocks a menu item more).
* `wm_chrome_rect_x` (`.cold`, kern_big): the two boxes share one tail -
  the minimize arm subtracts 27 and falls into the close arm's `+9` (-6).
* `menu_kbnav` (`.cold`): `kentc_di` and `jmp kretfc_di` (-5); AX was
  "clobbered" and comes back unchanged now. A `nop` pad after its `retf`
  puts `dsk_xfer` back on its parity (+1; `.cold` refuses a `db`).
* `wm_chrome_relit_x` (`.cold`, kern_big): `kentc_di` (-2; the armed
  window only).
* `ui_sys_find`: the match `ret`s where it jumped to a `ret` (-1).
* `wm_clip_split`: SI pushed with the rest so `.full` is `kret_di` and the
  inner push/pop SI goes (-4, +3 clocks).

## REFUSED

* **`kent_*` on the drawing primitives and the cursor** - `gfx_spans`,
  `gfx_xor_strips` (twice a drag frame; it would also put `gfx_sub_st`'s
  words at an odd shift), `gfx_save`/`gfx_restore`, `cur_lazyrect`,
  `cur_shape_pass`, `cur_lazyend`, `fpg_begin`/`fpg_busy`/`fpg_step` (once
  per `int 13h` and per cache hit), `wm_dmg_gray` (S2's), every
  `wm_su_*` (S3's, held by the owner).
* **`wm_clip_walk`'s `stc / jmp / clc`**: the B0 trick needs a dead AL and
  the walk preserves AX..DX for its callers.
* **`gfx_b4piece`'s `pushf / add si, di / popf` as `lea si, [si+di]`**:
  not an 8086 effective address.
* **`wm_dmg_bit` and `wm_dmg_wins`' two `mov cl,al / mov bx,1 / shl bx,cl`
  as one helper**: -2 at best, and a call per window per repaint.
* **`ui_rzx..ui_rzh` folded into `wm_rz_o*`** (-8 `.bss`): `wm_ask_size`
  between the bank and its read is a package callback, which may itself
  resize through `wm_rz_bank`.
* **`inst_tplbuf`** (-16 `.bss`, the launch template copied to be offset):
  creating from the template and offsetting after would bypass `wm_fit` and
  the natural-size bank, which see the offset rect today.
* **`menu_drop`'s chrome `thm_ink` as `thm_pen_live`** (-3, the per-item
  `thm_pen_live` then hoisted): it would clear a stale `[gfx_dis]` before
  the frame is drawn, which the frame does not do today - a behaviour
  change in an edge case.
* **`sched_stop` on `kent_di`** (-1): it sits in `[sched_stop, sch_isr)`
  and nothing else odd in that interval was found to pair with it.
* **`wm_ownseg`, `evq_pop`, `wm_wake`, `clip_put` were checked for callers
  that read a flag other than CF** - none in the kernel, and each API
  contract names CF alone; listed here so a reviewer can disagree.

## WANTS THE OWNER

1. **`kent_di` on the damage-repaint path - -16 bytes on both kernels.**
   Built, measured and BACKED OUT (`7d6dc26a`): `wm_draw_win` (-3),
   `wm_chrome_clip` (-2), `wm_covered` (-2 against what shipped), `wm_obscured`
   (-3 against what shipped), `wm_ttl_seen` (-1) and `wm_clip_split`'s
   `kent_di` and `mov cl,3` (-5 against what shipped). Each is asked per
   window (or per fragment) of every damage repaint, ~6 a window: ~90 clocks
   a call, so a close over four windows is ~2,200 clocks (~0.46 ms) - the
   shape of trade the owner declined for `rect_get`/`rect_put` S2.

## WANTS AN ABI BREAK

None. No cell, header, `SYSTEM.CFG` key or module interface moved.

## CROSS-FILE

For **fsys** and **recent** - found by the censuses, unverified (each needs
its callers read for flag use before it is taken, as TAKEN 3 and 4 were):

* known carries, a `clc`/`stc` already true at every predecessor: `lz_lz4`
  and `lz_decomp_big` (`lz.inc`), `fm_find_view`, `fm_drag`, `fm_dgwait`,
  `fm_scrollpaint` (`files.inc`), `ico_add` (`disk.inc`'s `ico_*`,
  recent's), and `os88ui_sbgrab`/`os88ui_sbtrack` (`apps/os88ui.inc` - shared
  with every package that includes it). ~-1 each.
* `jmp short` over a lone `stc`/`clc` (A8 / B0 candidates): `dsk_bpb_bank`,
  `dsk_bpb_check`, `dsk_clus2lba_x`, `dsk_dirw_next_x`, `dsk_byclus_x`,
  `dskw_name83`, `fmv_icorefs`, `fm_brect`, `fm_status_only`,
  `fm_scroll_by`, `fm_focus_x`, `assoc_tryfold`, `osapi_desk_item_x`,
  `db_click` (`dock.inc`), `os88ui_sbthumb`; and over two bytes (A9 / B8+):
  `dsk_here_ok`, `dsk_dirw_get_x`, `pth_leaf`, `fcp_paste`, `fcp_run2`,
  `desk_reflow`, `drv_cp_class_x`, `drv_load_at`, `osapi_img_name`,
  `mem_free_x`, `mem_regrow` (both `popf / stc`, `evq_pop`'s shape). ~-1
  each where the register or flags are dead.
* `kernel/disk.inc`'s `align 2` after `dsk_img16` (BASE AND TIP, above).

## ROWS RUN

ONE pass at the tip (`8de1700e`), nothing edited meanwhile,
`python3 tools/os88test.py soak --marty-jobs 1 -k ...`, MartyPC on the
GLaBIOS twins (nothing needed the IBM ROM; `mtools` present): **35 / 35 ok**
in 1,308 s. No row was broken on purpose this pass (time went to the bytes), so a
row here that cannot see its subject would not have been caught.

| rows | what of this pass they reach |
|---|---|
| `dispdrag`, `dragui`, `wmchrome`, `paintanchor`, `sizesnap` | `ui_track`/`ui_grow`'s banks and union, `ui_drag_phase`, `wm_flush_ck`, `wm_dmg_union` |
| `zoomsave`, `dispzoom`, `deskzoom` | `wm_zoom`, `wm_rz_paint`, `wm_covered`, `wm_clip_split` |
| `dispclose`, `dispfit`, `dispprefer`, `dispmine`, `dispcorner`, `win1bpp` | `wm_fit`, `wm_pref_take`, `wm_min_ax`, `wm_destroy`, `wm_hit`'s boxes |
| `disptitle`, `dispthm`, `dispband` | `wm_draw_title(_b)`, `wm_band`, `thm_pen_live` |
| `toastbar`, `toastfpg`, `fpgnotch` | `toast_place`, `fpg_arm`, `menu_bt_raw` |
| `stikmenu`, `wdmenusu`, `mouseup`, `curshape` | `menu_relayout`, `menu_ab_cell`, `menu_item_xor`/`menu_title_xor`, `menu_activate` |
| `clipkeep`, `evqfull`, `wakedrain`, `timerrace` | `clip_put`, `evq_pop`, `wm_wake` |
| `tmselfsu`, `smalllaunch`, `dispapp` | `ui_sys_find`, `app_launch`, `inst_set_name_x`, `wm_su_*` |
| `shutdown`, `dispreboot` | `mouse_unhook` on the restart path |
| `dispblit`, `npscroll` | `sw_pairbuild`, `gfx_scroll` |

No row exercises `menu_kbnav` (the keypad's item step) or `wm_hit_ttl_b`'s
minimize-box arithmetic directly: both were checked by reading (TAKEN 5 and
6 give the arithmetic).

## DEFECTS FOUND

None in shipped behaviour.

## Close-out round

The coordinator's close-out started at kernel-size-15 `e5e201bf`, where the
pass stood at -460 on kern_big. The commits are `93150611` and `fa84d472`.
Resident is `.text`+`.bss`+`.cold`+`.lowbss`+`.vgabuf`, plus `.mega` on
kern_mega, read off each kernel's map:

| kernel | base `e5e201bf` | tip | delta |
|---|---|---|---|
| kern_big | 89,548 | **89,471** | **-77** |
| kern_small | 57,871 | **57,834** | -37 |
| kern_emu | 89,804 | **89,727** | -77 |
| kern_mega | 123,287 | **123,218** | -69 |

`hotpar` is green on both kernels and the fast tier passes.
`tests/megaseg.txt` went up by 8 to 4,443. kern_mega did not grow here:
kern_big folded two MEM_RR_ONE rows into `mem_rr_lone`, and kern_mega keeps
those rows because it does not walk that run.

Package images that moved:

| images | change |
|---|---|
| browser, notepad, scribe, sheet, sheet.trim, taskmgr, texpad, thewire, word | -9 each |
| midirack | -4 |
| redline, syscheck, the small-arm notepad | -3 each |
| `kerndos.bin` | -10 |
| kern_small's FDLG.DRV and FILECP.DRV images | -1 each |
| kern_big's CTRL, HIBER, DOCK, DESK and DISKOPS images | unchanged |

### TAKEN

1. **Skip bytes became macros.** `SKIPF1`/`SKIPF2` are `test al`/`test ax`
   and write the flags with CF=0. `SKIPAL1`/`SKIPDL1`/`SKIPCX2` are
   `mov al`/`dl`/`cx` and write no flag. They are defined in `kernel.asm` and
   `kerndos/kdshim.inc`, with an `%ifnmacro` fallback in `lz.inc` for
   `OS88CZ.COM`. They are macros because os88ovlchk rule 1 refuses a `db`
   line in `.cold`, and because `tools/stkbalance.py` must read them as data.
   Each site is a `jmp short` (2 bytes) turned into one skip byte. The flag
   and register each caller reads were checked:
   - `dsk_clus2lba_x`, `dsk_byclus_x`, `dsk_bpb_bank_put`, `dsk_fatw_slotd`
   - `dsk_dirw_get_x` (bit 0 keeps ZF=0)
   - `dsk_here_ok` (into `dsk_media_ok`'s refusal)
   - `dsk_read_chain_x` (two sites), `ico_ref_try`
   - `fm_focus_x`, `fm_scroll_by`, `fm_c_drva`, `fm_c_cut`, `fcp_run2`, `fdlg_post_cancel`
   - `mem_free_x`, `mem_regrow` (over `popf/stc`)
   - `drv_cp_class_x`, `drv_load_at`, `desk_reflow`, `icon_draw_ix`
   - `lz_decomp_big` (this one also fixes the parity of `[gfx_blit1_x, dsk_xfer)`)
   - kern_big only: `osapi_seg` and `db_force`, which stay as they were on kern_small for its parity
   - `osapi_img_name`
   - `apps/os88ui.inc`: `sbtrack`, `sbthumb`, `sbhit`, which is the 9 bytes every scroll-bar package lost
2. **Known carry.** Each of these `clc`/`stc` had every predecessor already
   leaving CF at that value, so it was removed or turned into `CLC_OR_STC`:
   - `ico_add` (kern_big)
   - `fm_find_view`, `fm_dgwait`, `fm_drag` (`.click` deleted), `fm_scrollpaint`
   - `fmv_icorefs`, `fm_brect`, `fm_status_only`
   - `dskw_name83`, `osapi_desk_item_x`
   - `os88ui_sbgrab`
   - `api_fdlg_open` (SI is already 0 on its path, so `jmp`+`xor` went, -4)
3. **recent.md CROSS-FILE.** `fdlg_rqsp` (on kern_big, where it is not a
   module) and `drv_blkseg` joined `mem_rr_lone`, and their MEM_RR_ONE rows
   stay only on kern_mega. `osapi_seg` -1 was taken in its paired form
   (item 1).
4. **blob.md CROSS-FILE.** `hbk_drv_blk_call` and `os88ui_btn_f` were
   deleted. HIBER.DRV reaches `drv_blk_call_x` and `os88ui_kbtn` through a
   new `HBGC` hop (`hbg_cgo`), and CTRL.DRV and `shutdown.inc` use `CPXC`.
   `fdlg.inc` calls `os88ui_kbtn` near. `tests/btnsites.txt` went from 6 to
   5 for os88ui.inc.
5. **Parity pads.** On kern_big, viddet.inc's pass-14 pad and vidsel.inc's
   pass-15 pad fold as a pair, -2. `vid_ox`/`vid_oy` lie between them and
   move from odd to even, which helps their readers. Both pads stay on
   kern_small: there, removing either one moves `font_run` or `cur_move`.

### REFUSED

- **`lz_lz4`'s known carry.** The scanner missed a `jne`, so the carry is not known.
- **`assoc_tryfold`.** CF comes from a call.
- **`dsk_dirw_next_x`.** The `adc` edge case leaves CF=1 reachable.
- **`pth_leaf`.** Its callers were not traced, and they were not worth tracing for one byte.
- **The `wm_lf_pad` and clip.inc pad pair.** The save-under words, `wm_cov_*` and `wm_an_n` lie between them and would land odd (fold.md's hot-data warning).
- **vga12.inc's VID_EXT `db 0` and font.inc's.** Each is a local alignment inside its own routine (`%if ($ - .vram) & 1`), not half of a pair.
- **My `menu_kbnav` `.cold` pad.** It stays until an odd `.cold` saving lands in `[gfx_blit1_x, dsk_xfer)`. None was left this round.
- **The damage-repaint kent_di WANTS and LZB.** Not this round, per the brief.

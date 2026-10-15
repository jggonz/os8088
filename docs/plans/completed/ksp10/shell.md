# ksp10 / shell - notes (agent "shell")

Files: kernel/ui.inc, driver.inc, assoc.inc, dock.inc, dockmod.inc,
desksc.inc, clock.inc, clockw.inc, blank.inc, shutdown.inc.
Base `833f13e4`. Bytes are `kernsize` section deltas, kern_big / kern_small.

## TAKEN

### batch 1 - ui.inc (big .text -86, small .text -85)

* `ui_bill`: the start stamp is banked in the payload's own two stack slots
  (`xchg ax,[bp+8]` / `xchg dx,[bp+4]`) instead of two pushes, and the pops
  come back in one run with no `add sp, 6`. -4, and 4 bytes shallower at the
  moment a package callback runs. Per callback, not per pixel.
* `ui_drag_ph8` inlined into `ui_drag_phase`, its one caller, as
  `jns / add ax,7 / and ax,-8` - truncation toward zero by biasing a
  negative delta by 7, where it was negate-mask-negate. -11. Plus the
  `mov ax,[si+W_X]` that reloaded the value just stored (big only) -3.
* `ui_dispatch`: `cmp ax,0xFFFF` -> `inc/jz/dec` (-1); the handler resolve
  reads the set's segment only on the AM_ONCMD arm and drops a `push di` that
  ui_bill clobbers anyway (-3).
* `ui_cmd .close` runs through `ui_lcall` and shares `.launch`'s
  `jmp snd_beep` (-5). ui_cmd and ui_dispatch now say they clobber BP
  (ui_dispatch already did, through ui_bill's handler).
* `ui_reboot_post`: one store of `NOFLUSH - borrow` (-3).
* `ui_tm_open`/`ui_svc_open` store `[ui_desc]` themselves; the SI save
  wrapper and `ui_so_call` are gone (-5 big, -7 small).
* `ui_sys_open`: `[ld_said]` read-and-cleared with `xchg`, leaving AH = 0 for
  the index, and `jnz .back` (-6). `ui_tm_back` and `ui_sys_find` drop
  register saves their one caller (ui_sys_open, which banks everything) does
  not need, and ui_tm_back tail-jumps (-5, -7).
* `.evloop` loads CX/DX = EV_A/EV_B once for every event species; the three
  handler copies are gone and `.wake` takes `mov si, cx` (-18).
* `ui_raise`: the "front it unless it is frontmost" question the content
  press, the title press and the right button each spelled out (17 bytes a
  copy) is one routine (-15 net).

### batch 2 - driver.inc, assoc.inc, clock.inc, desksc.inc (big .cold -47, .bss -1, .text -1; small .text -1)

driver.inc (.cold -25, kern_big only - OS88_DRIVERS):
* `drv_row_x`: `mov ah, DRVR_SIZE / mul ah / xchg bx, ax` instead of a
  CX-banked `shl ax, cl` (-5).
* `drv_pub_seg`, `drv_row_ix_of`: the hit is an EQUAL compare, so CF = 0 is
  already the answer - `je .out` replaces `je .yes / ... jmp / .yes: clc`
  (-3 each).
* `drv_cp_class_x .hit`, `osapi_drv_cfg_x .ok`, `drv_load_x` (two): a `clc`
  the arriving branch had already guaranteed (-1 each, -4).
* `drv_tier_x`: through `kentc_bp`/`kretc_es`, AL written over the banked
  AX (`mov [bp+14], al`) so only AH comes back (-7).
* `drv_task_x .spfail`: `jmp short .fail` shares `.fail`'s stc (-1).

assoc.inc (.cold -22, kern_big only - OS88_ASSOC):
* `assoc_glyph_di`: `mov al, 8 / mul bl` (-5), and AX is now preserved too.
* `assoc_scan`: `repe cmpsb` with ES = DS banked, for the hand loop and its
  `cmp ax, ax` (-5).
* `assoc_reduce`: majority as `mov bh, -2 / adc x4 / cmp bh, 0x80 / rcl bl, 1`
  - the carry IS the output bit, so the `xor bl,bl`, the `shl bl,1`, the
  `jb` and the `or bl,1` go (-7). Per icon reduce (64 cells), not per pixel
  drawn; about the same cycle count.
* `assoc_locate`: the stem pointer is `assoc_glyph_di`'s answer at a fixed
  distance (same 8-byte stride, asserted) (-3).
* `assoc_app_new`: `mov [di], ah` for `mov byte [di], 0` (-1).
* `asc_use_x`: `shr dx, cl` already sets ZF; the `or dx, dx` went (-2).

clock.inc (.text -1, both): the AM/PM 'M' rides in AH of one word store.

desksc.inc (.bss -1, big only): `sc_vol` was a dead byte - named nowhere.

### batch 3 - ui.inc again (big .text -58, small .text -57)

* `ui_drag`: the "did it move" flag is `xchg ax,[bx+W_X] / xor ax,[bx+W_X]
  / xchg di, ax`, and `or di, ax` for y - old XOR new instead of a
  compare/branch/inc per axis (-5). Coordinates stay under 0x8000, so the
  later `inc di` cannot wrap.
* `ui_grow_clamp`: ui_grow's width and height release clamps were one body
  twice; it is indexed by DI = 0/2 over three asserted word pairs
  (ui_dragw/h, vid_w/h, W_X/Y) and DL = DI selects wm_min_axd's axis (-18).
* `ui_trk_ax`: the `orig + (mouse - start)` sum both tracker steps spelled
  out on both axes, indexed by BX = 0/2 (-10). Per drag pass (per tick).
* `.mup`: one gfx_lock and one liveness test for the chrome and the package
  release (-4).
* the region dispatch after wm_hit is one `cmp al, 1` three ways (`je`
  title, `ja` the boxes, fall-through content) with `cmp al, 4` at the
  boxes (-6). wm_hit answers 0..4 only.
* `ui_activate`: menu_activate + "redraw the bar if the owner changed",
  written at both presses, FALLS INTO ui_lcall (asserted) (-7).
* the chrome arm stores `[ui_post]`/`[ui_armr]` as one word, AH = AL = the
  region, which is non-zero (-2).
* step 0's `[ui_rebootq]` is read-and-cleared with xchg (-5).
* `ui_timer_pass`: `cmp word [si+W_TIMER], 0` (-1).

### batch 4 - small items (big .text -10, small .text -10, small .cold -2)

* ui_task's key dispatch reads W_ONKEY into BP and tests the register (-2).
* `.title_bar`'s `mov bx, si` before the zoom: ui_tdbl clobbers AX alone,
  so BX is still the window (-2).
* `.chk_pcmd` calls ui_cmd with no `jz` in front: ui_cmd ignores 0 itself,
  and the step sits behind [ui_post] (-4).
* `.clock`: clk_tick's last flag-writer is `or al, ah`, so ZF already says
  AL = 0 - the `or al, al` went, and clk_tick's header now promises it (-2).
* kern_small's driver stubs: `drv_fs_has` and `drv_owns_seg_x` were two
  copies of `stc / ret` - two labels over one now (small .cold -2).

## REFUSED

* `drv_cp_count_x` falling into `drvf_drv_cp_class` (-4): it leaves the
  routine with NO return in its own extent, so os88ovlchk's return-kind rule
  stops classifying it, and a future NEAR call to it (it ends in a far frame)
  would no longer be caught. Built, measured, reverted.
* `drv_blk_call_x` through `drv_pkg_disp` instead of the staged
  `drv_blkfp`/`drv_blkseg` far pointer (-9 with the 4 data bytes): it is the
  per-transfer dispatch into a block driver (dsk_xfer's path), and the
  synthesised frame is ~40 cycles more per call. A variant that keeps the
  staged pointer and only loads DS from DI is -1 net - not worth the churn.
* A `db 0x3D` skip-byte ladder for drv_load's four error codes (-3): no
  precedent in the tree, and stkbalance/ovlchk read instructions.
* `str_len` restructured: no saving once counted (both 14).
* `ui_timer_pass`'s `push si`/`pop si` round ui_bill (-2): ui_bill keeps
  SI only as far as the HANDLER does, and W_ONTIMER is not a callback SPEC.md
  binds to keep SI (W_ONCLICK and W_PAINT are; AM_ONCMD explicitly may
  clobber it). Left.
* `ui_track` taking the step in BP instead of SI to drop its `push si`/
  `pop si` (-2): every callee in the loop is documented to keep SI and not
  all to keep BP. Left.
* A shared "is this press on a menu bar" predicate for `.mdown` and
  `ui_rdown` (wm_fs_vis, MBAR_H, [vid_pw]): kern_big -5 but kern_small +2,
  because the [vid_pw] half that makes it pay is kern_big's alone. Not worth
  an %ifdef for 5.
* `ui_tdbl` reuse of AX across the two `.first` arrivals: 0 bytes.
* `db_xor_ring`/`db_xor_body` merged on a CF selector: +2.

## CROSS-FILE

* **A `.text` KENT** (`kent_di` / `kent_bp`, kernel.asm beside the
  `kret_*` ladder): `kentc_di`/`kentc_bp` exist only in `.cold`, so a `.text`
  routine banks AX..DI with six (or seven) pushes. There are ~70 such runs in
  `.text` (a scan of `push ax / bx / cx / dx / si / di` at a routine head);
  `kentc_di` is 15 bytes, so each non-hot site converted saves 3 (4 with BP),
  and the break-even is five sites. Candidates that are NOT per-pixel or
  per-glyph: wm.inc (~20 sites - repaint, geometry, z-order), menu.inc (6),
  instance.inc (2), fsx.inc (3), toast.inc, ui.inc's `ui_sys_open`,
  dock.inc's `db_paint`. ESTIMATED -60..-90 on kern_big after the helper,
  ~60-80 cycles a call where converted (pass 6's figure for the cold one).
  The drawing primitives (vga12, softgfx, font, icons, mouse) stay as they
  are. This is the coordinator's: kernel.asm owns the ladder.

* `docs/INDEX.md` is regenerated in this branch only because adding
  `docs/plans/ksp10/shell.md` made `os88index` call it stale (the build
  gate). Every agent's notes file will do the same: regenerate once after
  the merge (`python3 tools/os88index.py`), and again when the directory is
  deleted.

## WHAT RAN

* `make` (fast tier inside it) after every batch: 61/61 each time.
* `make small`, `make emu` at the tip: both assemble.
* `tools/stkbalance.py kernel/kernel.asm kernel/*.inc`: 0 unbalanced at the
  base and at every batch.
* Soak rows, one at a time: `mouseup` (twice - after batch 3 and at the tip),
  `tmload`, `assocopen`, `drvup`. All green.

## TOTALS AT THE TIP

* kern_big: text 44,153 -155, bss 5,132 -1, cold 37,874 -47 (sum -203); the
  cold rung uncrosses 75 -> 74.
* kern_small: text 32,751 -153, cold 23,948 -2 (sum -155).
* kern_emu: text 44,418 -155, bss -1, cold -47 (sum -203).
* Per file, kern_big (`kernsize --modules`, code = .text + .cold): ui.inc
  2,826 -> 2,672 (-154), assoc.inc 2,360 -> 2,336 (-24), driver.inc 2,269 ->
  2,246 (-23), clock.inc 491 -> 490 (-1), desksc.inc bss 4 -> 3 (-1). dock,
  blank, shutdown, clockw, dockmod: 0.

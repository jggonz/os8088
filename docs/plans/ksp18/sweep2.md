# ksp18 agent "sweep2" - a second look at the `wmui` row's files

Kernel size pass 18 (`docs/plans/ksp18/BRIEF.md`), second wave: a fresh reader
over the `wmui` row's files as they stand after that agent merged -
`kernel/wm.inc`, `instance.inc`, `clip.inc`, `events.inc`, `dock.inc`,
`apps.inc`, `ui.inc`, `menu.inc`, `toast.inc`, `fprog.inc`, `snd.inc`,
`ctrl.inc`'s resident half and `kernel.asm` (the API shims, `kmain`), except
the routines `docs/plans/ksp18/fresh-routines.md` gives to `hot` (`ui_task`,
`menu_draw_bar`, `wm_draw_win`, `wm_su_bank`, `wm_su_merge`,
`wm_dg_precover`, `wm_rz_swap`, `clip_drop`) or `dir` (`toast_now`, the
`cp_*` strings) - none of which was edited. Branch `ksp18-sweep2`, base
`727376d1` (kernel-size-18 with `main`, `dir` and `wmui` merged). Figures are
ASSEMBLED; resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` +
`.vga` (+ `.mega` on kern_mega).

## HOW IT WAS READ

A census of every routine in the thirteen files off the base map (span to the
next non-local symbol, reference count across `kernel/`, `boot/` and
`drivers/`, call versus table reference), a list of every routine whose
prologue banks registers with its reference count, and the files read
code-only (comments stripped) top to bottom. The hot routines, the clip
region, the damage repaint and the raise cache (S3) were read but only for
what could be priced against callers; the bytes came from the shapes pass 16
and 17 named - a callee-save no caller reads, a reload of what a register
still holds, a wrapper the body can be - and one this pass adds: **a door
pattern repeated four times**, written once.

## BASE AND TIP

| kernel | base (`727376d1`) | tip | delta | where |
|---|---:|---:|---:|---|
| **kern_big** | 91,606 | **91,530** | **-76** | `.text` 40,568 -> 40,494 (-74), `.bss` 5,368 -> 5,366 (-2) |
| kern_small | 57,261 | **57,217** | **-44** | `.text` 30,192 -> 30,150 (-42), `.bss` 2,938 -> 2,936 (-2) |
| kern_emu | 91,850 | **91,774** | **-76** | `.text` 40,812 -> 40,738 (-74), `.bss` -2 |
| kern_mega (with `.mega` 23,056, unchanged) | 127,101 | **127,025** | **-76** | `.text` 46,244 -> 46,170, `.bss` 5,406 -> 5,404; conventional 62,976 of 68,096, unchanged |

kern_big and kern_small tips are `tools/kernsize.py --json` on this tree's
`make` / `make small`; kern_emu's and kern_mega's are `make emu` / `make
mega` here. The bases: kern_big's `kernsize --json` on the base build;
kern_small's, kern_emu's and kern_mega's are the tip plus
`tools/os88rdiff.py kernel 727376d1 .` with `-D KERN_SMALL` / `-D KERN_EMU` /
`-D KERN_MEGA`, which reports the `.text` and `.bss` deltas above and no
other section moved (this branch touches no `.cold`, `.lowbss`, `.vga` or
`.mega` byte). `.boot2`, `.ovl`, `.cfx`, every module and driver are
unchanged; `KERN_SIZE` 97,280 / 59,392 unchanged. `make`, `make small`,
`make emu` and `make mega` build at the tip (`megaseg` within
`tests/megaseg.txt`), the fast tier is green at both commits (73 passed, 1
skip), and `tools/stkbalance.py` over the eight touched files reports the
same 10 unbalanced paths at base and tip (the `kret*` ladder tails and
`inst_pkg_alive`'s tail into `task_exit`).

**Both `.text` totals are EVEN against the base** (-74, -42), so `.bss`,
which follows `.text` with `valign=1`, starts at the base's parity and every
`.bss` word (`[ticks]` and the scheduler's among them) keeps its alignment.
That took choosing: `disk.inc`'s `align 2` after `dsk_img16` (BRIEF section
3) rounds every shift in front of it to even, so the parity of the total is
decided by what lies after it (`dock.inc`, `snd.inc`), and the items there
were picked to sum even (-8 kern_big, -4 kern_small); in front of the align,
after `ui_task`, the items sum even too (-14), so the align pads nothing.

## TAKEN

Bytes are kern_big (= kern_emu = kern_mega) / kern_small. Every item is
byte-identical behaviour; register values that differ are ones no caller
reads, each caller read.

### 1. The four locking doors written once (`wm.inc`) - -7 / -7

`osapi_wm_resize`, `wm_hide`, `wm_front` and `wm_show` were each `push ax /
call wm_lk / push ax / call <body> / jmp short wm_lk_tail` (10 bytes, the
last 8). Each is now `push ax / mov ax, <body> / jmp short wm_lk_go` (6, the
last 4) and `wm_lk_go` banks BP, takes the body into it with `xchg ax, bp`,
asks `wm_lk`, and calls through BP. The body sees AL = `wm_lk`'s answer as
before, and an AH that is the caller's BP's high byte instead of the caller's
AH: none of the four bodies (nor DESK.DRV's `dkd_show`, reached from
`wm_show_b`) reads AX before writing it - each was read. ~+25 clocks a door;
every door is H3 (a show, a hide, a raise, a resize - a raise is ~150-370 ms
on the 5150, khp1 LOG section L), so well under 0.01%.

### 2. Banks and reloads no caller reads

| routine | file | change | big / small |
|---|---|---|---:|
| `inst_seg_parked` | instance.inc | ONE `push bx` at the entry for both arms (the instance arm and `.drv` each pushed it), so the instance arm's `.free`/`.busy` answers ARE `.drv`'s `.svcok`/`.svcbusy` tail and their 4 bytes went. OS88_COMPACT (kern_big) | -5 / 0 |
| `menu_drop` | menu.inc | the OS88_COMPACT `mmf_mem_movable` call reloaded `BX = MEM_P_MSAVE` that `mem_claim` keeps ("every register but DX") and banked an AX the `rect_get` below overwrites | -5 / 0 |
| `ui_dispatch` | ui.inc | the menu selection stays in AX across `gfx_lock` (flags) and `wm_strseg` (`push cs / pop es`); it parked in `ui_menusel` while `task_cycles`, which answers in DX:AX, was called there - that is `ui_bill`'s now, which banks AX. The store, the reload and the `.bss` word went | -6 + -2 `.bss` / same |
| `ui_loc_gate` | ui.inc | banks no AX/BX: its one caller, `ui_task`'s press arm, then runs `fm_bar_gate_x` (`kentc_di`), `ui_lcall`/`menu_track` (writes AX before reading it, banks BX) and `ui_dispatch` (writes BX). The header now says so; CX/DX, the press's x/y, are still untouched | -4 / -4 |
| `inst_parksafe_set`, `inst_restart_set` | instance.inc | no AX bank: neither writes AX and `inst_es_slot` keeps it (`inst_of_seg` walks DI, `inst_fhome_idx` banks AX round `inst_idx`) | -3 / 0 |
| `osapi_snd_play` | snd.inc | the refusal tail's `xor ah, ah`: AH is 0 on all three arms (`mov ax, 4` wrote it; `snd_req_inst` writes DH, `snd_str_busy` and `snd_town_off` bank AX, the priority is built in AL) | -2 / 0 |
| `ui_grow` | ui.inc | the height axis by `inc di / inc di` on the 0 `ui_grow_clamp` and `wm_min_axd` keep, for `mov di, 2` | -1 / -1 |
| `ui_sys_open` | ui.inc | `inst_where` (AX/DL) for `osapi_file_here` (the slot's DX/BL shuffle of the same answer); nothing after reads DH or BX | -1 / -1 |

### 3. A wrapper the body can be, a load another routine already makes

* **`wm_display`'s primary arm calls `osapi_video`** (kern_big, -12):
  `mov ax, [vid_pw] / mov bx, [vid_ph] / mov cx, [vid_dock_y0] / mov dl,
  [vid_kind]` are OSAPI_VIDEO's four loads exactly (kern_small's arm already
  shares the body with a `jmp`), and the DH it answers is computed again by
  the `.bpp` block the call falls back into. A call and a `ret` on a rare
  slot.
* **`ui_cmd`'s table names `ui_tm_open`** (-3 / -3): `.tasks: jmp
  ui_tm_open` was the whole arm.
* **`menu_item_xor` takes 0FFFFh as a no-op** (-4 / -4): `menu_drop`'s hover
  loop asked `inc ax / jz / dec ax` before each of its two calls (8 bytes);
  the test is in the routine once (4), reaching `menu_item_dis`'s `ret`. AX
  comes back 0 for the no-op, and both sites reload or drop AX straight
  after; WIN95.DRV's `wn_sel` (the one other caller) never passes it.
* **`inst_alloc_di` falls into `inst_alloc`'s tail** (kern_big, -2): it
  stands between the scan's refusal and the tail, where its `jmp short
  inst_alloc.hit` was; the scan's refusal writes its `.out` (`pop cx / ret`)
  in place of the jump to it, so `stkbalance` sees no cross-routine tail.
* **`api_rn`'s tail onto `api_rs.tail`** (-1 / -1, wmui's held-for-parity
  item, paired here with `kmain`'s -1 below): `pop ds / pop bp / retf`
  became `jmp short`; a file call is ~400 ms.

### 4. Smaller bodies

| routine | file | change | big / small |
|---|---|---|---:|
| `rect_put` | wm.inc | `push si / push bp` first, then ONE `add word [bp+4], 2` for the return fix-up and `mov si, [si]` (was `lea` / `mov [bp+2], si` / `mov si, [si-2]`) | -3 / -3 |
| `rect_get` | wm.inc | `pop bx / inc bx / inc bx / push bx / mov bx, [bx-2]` | -2 / -2 |
| `db_hit` | dock.inc | the left-edge test is the `sub ax, DOCK_X0`'s borrow; the `cmp cx` went (AX is a clobber and `.in` sets CF itself) | -3 / -3 |
| `wm_pref_now` | wm.inc | the kind indexed by `cbw / shl ax ...` (AX is reloaded from the pair) for CX with `mov ch, 0` | -3 / -3 |
| `osapi_snd_caps` | snd.inc | the driver's caps word read once into DX; DX's 1-or-3 answer by `neg / sbb / and / inc` (CF = 0 out as before) | -2 / 0 |
| `menu_bt_raw` | menu.inc | the cell index as `mov bl, 1`: BX is `[menu_abcell]` (a bar index) or 0 there, and `menu_bent`/`menu_bstr` keep it | -1 / -1 |
| `fpg_arm` | fprog.inc | the armed test as `shr dl, 1 / jc` (FPG_PARK): DL is then `[fpg_on] >> 1`, non-zero exactly when `[fpg_on]` is on the path that reads it again | -1 / 0 |
| `ui_sys_open` | ui.inc | the name compare by `lodsb` (+1 `cld` outside the loop) | -1 / -1 |
| `kmain` | kernel.asm | the boot-tick test as `mov ax, [boot_ticks] / inc ax / jz` (AX dead on both arms) for the `cmp word` immediate | -1 / -1 |
| `db_draw_tile` | dock.inc | `lea dx, [bx+2]` for `mov dx, bx / inc dx / inc dx` | -1 / -1 |

`rect_get`/`rect_put` were flag-clean (`lea`-only) for "a site that carries a
compare across the four moves". **None of the fourteen sites does** - each
was read (the two in `instance.inc`, two in `menu.inc`, ten in `wm.inc`); the
header now says both clobber the flags (`rect_get` keeps CF). Both bodies are
a few clocks FASTER than before (fewer fetch bytes; one read-modify-write
where the put had three moves), and the S3 site (`wm_su_take`'s get) loses
nothing.

### Hot paths and parity

No routine on the brief's hot list changed a byte - `wm_paint_all`, the
damage repaint, the API cell, `ui_task`'s idle pass, `t_hotpar.py`'s table -
and every pinned routine sits at its measured parity on both gated kernels
(`hotpar` green at both commits) **with no pad added and none removed**. How
it was kept, kern_big:

* `[start, gfx_blit4)`: `api_rn` -1 + `kmain` -1.
* `[sch_switch, wm_paint_all)`: the doors -7, `wm_pref_now` -3, `wm_display`
  -12 (kern_big only) - even on both kernels.
* `[wm_paint_all, ui_task)`: kern_big -26 (`inst_seg_parked` -5,
  `inst_parksafe/restart` -3, `inst_alloc_di` -2, `menu_drop` -5 and -8 +4 for
  `menu_item_xor`, `menu_bt_raw` -1, `rect_*` -5, `fpg_arm` -1); kern_small
  -10. The first build had kern_big's `ui_task` odd (-17), and the
  kern_big-only `fpg_arm`, `inst_parksafe` and `menu_drop` items are what
  evened it.
* after `ui_task`: free, but see BASE AND TIP on `.bss`.

Speed, by path: `wm_display` +1 call on a rare slot; the doors ~+25 clocks
(H3); `rect_*`, `db_hit`, `wm_pref_now`, `fpg_arm`, `ui_sys_open`'s compare,
`ui_dispatch`, `ui_loc_gate`, `inst_*`, `menu_drop`'s claim and the snd items
only lost work; `menu_item_xor`'s no-op now costs a call and a `ret` where
the site's test did not (~+35 clocks a hover change that leaves or enters
"none", inside a menu poll loop that yields every pass - H3).

## REFUSED

* **`inst_ptr` by `mov ah, I_RECSZ / mul ah`** (-5, both kernels, the
  shape `serial.inc` already uses): ~+15 clocks a call, and `inst_ptr` is
  under `ui_bill`'s `inst_win_owner` on every package callback - per event,
  H2 by khp1's scale, which must be cycle-neutral.
* **`wm_chrome_relit`'s guard as `je .r`** (-1, kern_big): its common
  answer (nothing armed) would take the jump, +12 clocks on every title bar
  draw including the damage repaint's.
* **`wm_sizable`'s and `wm_keeph`'s and-then-or** (wmui's reason holds: the
  bit reads clear for an instant to another task).
* **`toast_arm`'s want/on/dirty as two word stores** (-1): the `want = 0`
  store must stay FIRST - a `toast_show` from a worker landing mid-arm sets
  it again and must not be lost.
* **`ui_drag`'s `mov byte [wm_dmg_npro], 1` as `inc`** (-1): the flag is
  only known 0 if every earlier setter's `wm_paint_dmg` reached `.promote`;
  an empty clamped rect leaves it at 1 through `.chrome`, and a 2 then
  promotes.
* **`wm_dc_take` written into `ui_drag`** (~-7 kern_small, ~-1 kern_big):
  135 bytes into `ui_drag` relax its `jc/jnz .nocache` (+3 each) and move an
  odd span out of `[wm_paint_all, ui_task)`; it is also the drag cache's
  (S3-adjacent).
* **`cp_open_need`'s BX/CX bank** (kern_small only, -4): its callers
  (`ui_task`, `ui_cmd`) were not all provably indifferent.
* **`osapi_img_name`'s `.mod`/`.drv` CH as a carry** (0: every shape found
  is the same five bytes).
* Pass 17's and `wmui`'s refusals were not re-proposed.

## HELD FOR PARITY

* **`wm_isrec` by `lea ax, [bx - wm_wins]`** (-1, both kernels; it is
  `wm_saveu`'s, `wm_noanim`'s and `wm_quiet`'s range test): built in reading,
  not taken - it is a common odd byte in `[wm_paint_all, ui_task)` and this
  branch had no common odd partner left there.
* **`wm_dmg_rebands`' `xor ah, ah` as `cbw`** (-1, kern_big, FASTER - 2
  clocks for 3): a kern_big-only odd byte in `[sch_switch, wm_paint_all)`.
  Pairs with any other kern_big-only odd byte there, or with `wm_close_req`'s
  parity `nop` if a kern_small-only odd byte also turns up in that interval.
* `wmui`'s **`osapi_seg` `CLC_OR_STC`** (-1 `.cold`, kern_big) still has no
  odd `.cold` partner ahead of `gfx_blit1_x` in these files.

## WANTS THE OWNER

None. Every item is byte-identical behaviour.

## WANTS AN ABI BREAK

Nothing new. Pass 17's list (P18 handoff section 4) and `wmui`'s two
observations stand.

## CROSS-FILE

* **`toast_place` writing `str_len` at its one call** (`clock.inc`, `sys`'s,
  where `sys` has already shrunk `str_len` to 11 bytes): +8 in `toast_place`
  (`mov si, toast_buf / mov cx, -1 - toast_buf / .l: cmp byte [si], 1 / inc
  si / jnc .l / add cx, si`), -11 in `clock.inc`: **-3 net**, and `.text` in
  both `[wm_paint_all, ui_task)` (toast.inc) and `[sch_switch,
  wm_paint_all)` (clock.inc) - both odd, so it needs a partner in each.
* **WIN95.DRV's `wn_sel`** (`winmod.inc`, no row): its two `cmp ax, 0xFFFF /
  je` guards are redundant now that `menu_item_xor` ignores 0FFFFh (module
  bytes only, -10).
* **The `cw_*` shims called only from `.cold`** (18 of them on kern_big, 4
  bytes each, callers in `vga12.inc`, `files.inc`, `desk.inc`, `blank.inc`,
  `loader.inc` and `menu.inc`): a resident `.cold` trampoline would pay ~-40
  net but adds ~+100 clocks a call and several are clipped drawing (H2) -
  priced, not proposed.
* **`mod_needi`**: `ctrl.inc`'s `cpf_need` is the only `mod_need` site of
  that shape in these files (`cpf_cp_flush_close`'s is `mod_drop`), so the
  routine cannot pay here alone; it stays `sys`'s (`mod.inc`).
* `wmui`'s `hb_kinit`/`fm_kinit` item and `clip_drop`'s bank (`hot`'s)
  still stand as that note priced them.

## ROWS RUN

ONE pass after the last code commit (`79841610`), nothing edited meanwhile,
`python3 tools/os88test.py soak -k ... --marty-jobs 1`, MartyPC on the
GLaBIOS twins (nothing needed the IBM ROM): **37 passed, 0 failed, 0
skipped** in 857 s.

| rows | what of this branch they reach |
|---|---|
| `dispapp`, `dispclose`, `dispzoom`, `deskzoom`, `disptitle`, `dispdrag`, `dragui`, `paintsu`, `tmrepair` | the four locking doors through `wm_lk_go` (show, hide, front, resize), `rect_get`/`rect_put` on the raise cache and the drag's vacate |
| `dispsize`, `dispfit`, `dispprefer` | `wm_pref_now`'s index, `wm_display`'s primary arm |
| `alertanim`, `dockmark`, `dockpos`, `deskdock` | the minimize/restore animation's `rect_put`, `db_hit`, `db_draw_tile` |
| `atmenusu`, `sheetmsep`, `stikmenu`, `desksc` | `menu_drop`'s claim and hover (`menu_item_xor`'s no-op), `menu_bt_raw`, `ui_dispatch`'s selection in AX, `ui_loc_gate` |
| `trkcompact`, `pkgmove-dotdel`, `zrestart` | `inst_seg_parked`, `inst_parksafe_set`/`inst_restart_set`, `menu_drop`'s movable claim |
| `fpgnotch`, `fpgop`, `toastfpg` | `fpg_arm`'s armed test (FPG_PARK) |
| `tmload`, `tmsmall` | `ui_cmd`'s table naming `ui_tm_open`, `ui_sys_open` (`inst_where`, the `lodsb` compare) |
| `paintrz`, `sizesnap`, `paintshrink` | `ui_grow`'s height axis |
| `sndplay`, `apspkcard` | `osapi_snd_play`'s refusal tail, `osapi_snd_caps` |
| `kzboot`, `bootstatus` | `kmain`'s boot-tick test |
| `dosren`, `wseq` | `api_rn`'s tail (the N-stub file calls) |

No row was confirmed to reach `inst_alloc_di` (the loader's top-down slot
pick for an OS88_F_DESK package); its fall-through into `inst_alloc`'s tail
was checked by reading the assembled listing (`os88rdiff`: the entry is one
`push cx` and the tail follows it).

## DEFECTS FOUND

None. (One stale comment corrected: `ui_dispatch`'s note that the selection
"parks in .bss ... because task_cycles answers in DX:AX" described a call
that moved into `ui_bill` long ago - the reason the park could go.)

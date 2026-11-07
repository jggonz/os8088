# ksp18 agent "wmui" - the window manager, the UI task's helpers, the menu bar and the API shims

Kernel size pass 18 (`docs/plans/completed/ksp18/BRIEF.md`), section 2's `wmui` row: a
general, whole-file finder over `kernel/wm.inc`, `instance.inc`, `clip.inc`,
`events.inc`, `dock.inc`, `apps.inc`, `ui.inc`, `menu.inc`, `toast.inc`,
`fprog.inc`, `snd.inc`, `ctrl.inc`'s resident half and `kernel.asm` (the API
table's shims, `kmain`), except the routines
`docs/plans/completed/ksp18/fresh-routines.md` gives to `hot`, `dir` or `main`
(`ui_task`, `menu_draw_bar`, `wm_draw_win`, `wm_su_bank`, `wm_su_merge`,
`wm_dg_precover`, `wm_rz_swap`, `clip_drop`, `toast_now`, the `cp_*` strings
and handlers among them - none of which was edited). Branch `ksp18-wmui`,
base `c5a7b0f9` (the brief, on `aede17b1`). Figures are ASSEMBLED,
`tools/kernsize.py --json` absolutes on each kernel's own build; resident =
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga` (+ `.mega` on
kern_mega). Per-routine figures are `tools/os88rdiff.py kernel c5a7b0f9 .`
(and `-D KERN_SMALL`).

## HOW IT WAS READ

Every top-level routine of the thirteen files was listed with its assembled
span off the base map and its reference count across `kernel/`, `boot/` and
`drivers/` (a scratch census), and the files were then read code-only, top to
bottom, for pass 16/17's shapes and two this pass added: **a second call that
asks a question whose answer is still in a register** (the side border, four
sites), and **a constant re-stored on every call** (the wake record's type, a
visible bit set where the branch has just proved it set). Five mechanical
scans ran first and found almost nothing - the files have had seventeen
passes: `call X / ret`, `cmp reg, 0`, `mov reg16, 0`, a reload of a register
from the memory it was just loaded from, and a `jmp` to a lone `ret` came
back empty; `mov word [m], 0` with dead flags gave seven, all taken below.

## BASE AND TIP

| kernel | base (`c5a7b0f9`) | tip | delta | where |
|---|---:|---:|---:|---|
| **kern_big** | 91,767 | **91,704** | **-63** | `.text` 40,633 -> 40,580 (-53), `.bss` 5,376 -> 5,368 (-8), `.cold` 38,034 -> 38,032 (-2) |
| kern_small | 57,333 | **57,285** | **-48** | `.text` 30,232 -> 30,194 (-38), `.bss` 2,946 -> 2,938 (-8), `.cold` 21,287 -> 21,285 (-2) |
| kern_emu | 92,011 | **91,948** | **-63** | `.text` 40,877 -> 40,824 (-53), `.bss` -8, `.cold` -2 |
| kern_mega (with `.mega` 23,072, unchanged) | 127,276 | **127,213** | **-63** | `.text` 46,307 -> 46,254, `.bss` 5,414 -> 5,406, `.cold` 46,573 -> 46,571; conventional 62,976 of 68,096, unchanged |
| kern_mega segment overhead (`os88megaseg`) | 3,682 | 3,682 | 0 | within `tests/megaseg.txt` |

kern_emu's and kern_mega's base is the brief's section 0.1 table (`aede17b1`;
`c5a7b0f9` adds two documents and no source); their tips are this tree's
`make emu` / `make mega`. `.lowbss`, `.vgabuf`, `.vga`, `.boot2`, `.ovl`,
`.cfx`, every module and every driver are unchanged; `KERN_SIZE` 97,280 /
59,392 unchanged (no rung crossed - and per the banner, not the point).
`make`, `make small`, `make emu` and `make mega` build at the tip, the fast
tier is green at both commits (73 passed, 1 skip), and `tools/stkbalance.py`
over the six touched files reports the same 10 unbalanced paths at base and
tip (the `kret*` ladder tails and the pre-existing `inst_pkg_alive` tail into
`task_exit`).

**`disk.inc`'s `align 2` after `dsk_img16`** (BRIEF section 3) moved by a
byte between the two batches, so the first batch's `kernsize` and its spans
differ by one; at the tip they agree on both kernels (-53 and -38 `.text`).

## TAKEN

Bytes are kern_big (= kern_emu = kern_mega) / kern_small, routine spans from
`os88rdiff`; they add to the totals above (-63 / -48). Every item is
byte-identical behaviour. No file outside this row was edited.

### 1. An answer asked twice: the side border (`wm.inc`) - -11 / -11

`wm_bord` (SPEC.md 11.95.3) answers "does this window have a side border";
`wm_bordr` was the same answer behind a `jmp`, existing only so `make
NOFLUSHR=1` can put the right border back. Four sites asked both, one after
the other, with AX untouched between:

| site | what it held | bytes |
|---|---|---:|
| `wm_bordr` itself | `wm_bordr equ wm_bord` off the knob; its `jmp wm_bord` went | -2 |
| `wm_geom` (OSAPI_WM_GEOM) | `sub cx, ax` twice off one call | -3 |
| `wm_clip_set` (OSAPI_WM_CLIP_SET) | the right edge's AX is the left edge's | -3 |
| `wm_bar_span` (every title bar's fill and rule, `wm_su_rect`) | the same | -3 |
| `wm_title_band` (`BANDCOMP` knob only) | the same | (knob) |

Two macros, `WM_BORDR`/`WM_BORDL`, are the second call under `NOFLUSHR` and
nothing otherwise, so the knob builds exactly what it built. **Faster on
every site**: each removed call was `wm_bord` -> `wm_flush` -> `wm_snap_want`
-> `wm_flush_ck` (`kent_di`, `wm_disp_xw`, a compare, `kret_di`) - by hand
~300-450 clocks on the 8088 for a window that snaps (the common case), at
least ~60 for one that does not. `wm_clip_set` is every package paint's first
call and `wm_bar_span` two calls of every title bar; neither is on the
pinned list, and both only lost work.

### 2. A constant re-stored on every call

* **The wake record initialised** (`wm_wake`, -6 / -6: `.text` +2, `.bss`
  -8). `wm_wk_rec` was `resb 8` and `wm_wake` stored `EVT_WAKE` into its
  type with a six-byte immediate on every post. It is `dw EVT_WAKE, 0, 0, 0`
  in `.text` beside `wm_wake` now; only `EV_A` is per wake. `EV_B`/`EV_C` are
  unused by a wake and were the zeros `.bss` arrives with (SPEC.md 2.5), so
  the records `evq_push` copies are identical. An `%if` pins the layout.
* **`wm_an_open` spends the arm through AL** (-8 / 0, kern_big: `WM_ANIM`).
  `mov al, 0 / xchg al, [wm_an_armed]` at the entry, `or al, al` where the
  `cmp byte` was, and the two `mov byte [wm_an_armed], 0` (one per arm) and
  `.drop`'s own `ret` went. The arm is cleared at entry instead of at each
  exit; nothing between reads the byte (`wm_an_ok` and `wm_an_setw` keep
  every register, and the only writer, `inst_unmin`, is the UI task's, which
  is busy here). AX is `wm_show_b`'s to bank, as `.half` already required.
* **`wm_show_b`'s `.onglass` `or` of the visible bit** (-3 / -3): the `jnz`
  into that arm is taken because the bit is set, BX is the window again out
  of `wm_top_dbp`, and `wm_front_b.go` reads no flag - a store of what was
  there.

### 3. A wrapper whose work the body can do (`kernel.asm`, `wm.inc`) - -4 / 0

`wm_timer_c: call wm_timer / clc / ret` was OSAPI_WM_TIMER's routine on
kern_big (the `clc` because the arm path's CF is the carry out of `[ticks] +
delay`). `wm_timer` ends its arm path with the `clc` itself (+1) and
`wm_timer_c equ wm_timer` (-5); the cancel path's CF is already the `or`'s 0.
A call and a `ret` faster per OSAPI_WM_TIMER. kern_small's refusal arm is
unchanged.

### 4. Smaller bodies

| routine | file | change | big / small |
|---|---|---|---:|
| `osapi_video` (OSAPI_VIDEO) | kernel.asm | `[vid_pw]`/`[vid_ph]` for `[vid_pwm1]+1`/`[vid_phm1]+1`: `vid_apply` writes all four in ONE copy from `vid_w..vid_hm1`, and nothing else writes them | -2 / -2 |
| `wm_create` | wm.inc | `mov ch, 0` before the z-order append: the zero run's `rep stosb` left CX 0 and `wm_nat_bank`, `wm_fit`, `wm_kind_bank` (EXT_KINDNOW's `wm_disp_now` included) and `wm_ptr2idx` all keep it | -2 / -2 |
| `inst_set_name_x` | instance.inc | the I_NAME+15 terminator stored after the loop from the spent count's CL (2 bytes) instead of first as an immediate (4); the record is unpublished, so the order is nobody's to see | -2 / -2 |
| `menu_bent` | menu.inc | moved below the pads so it FALLS INTO `menu_bstr`; its `jmp short` went (an `%if` pins the fall) | -2 / -2 |
| `ui_tdbl` | ui.inc | a spent pair stores the measured delta (below UI_TDBLT, so no window pointer) in `[ui_tclkw]` - a 3-byte accumulator store for the 6-byte immediate 0 | -3 / -3 |
| `wm_band` (OSAPI_WM_BAND) | wm.inc | the refusal is the exit's `CLC_OR_STC` byte, and `.no`'s `stc / jmp short .ok` went; the answer is CF alone (SPEC.md 11.96.11.1) | -2 / -2 |
| `ui_sys_open` | ui.inc | the running-instance scan over `inst_next` (the CX count, `add` and `loop` went); the LD_E* code's AH zero as `cbw` (its `[ld_pwin]` store is in the `and` row) | -2 / -2 |
| `ui_dispatch` | ui.inc | `cbw` for `xor ah, ah`: AL is an item index below 16 | -1 / -1 |
| `menu_kbnav` (`.cold`) | menu.inc | `.noj`'s `jmp short .no` IS the refusal (`stc / retf`) now, and the foot's copy went; pass 15's parity `nop` after it stays (the change is even) | -2 / -2 `.cold` |
| `menu_relayout` | menu.inc | the Locator set's name read through DS, not `cs:` - DS is the kernel's there, as the routine's own `[menu_win]` read assumes | -1 / -1 |
| `wm_resize_nb` | wm.inc | `mov ax, cx` for a reload of `[vid_w]`: `wm_min_floor` keeps CX | -1 / -1 |
| `inst_park_req` | instance.inc | the deadline built in DX (`mov dx, [ticks] / add dx, ...`) | -1 / 0 (OS88_COMPACT) |
| `and word [m], 0` for `mov word [m], 0` where no flag survives to a reader | menu.inc, ui.inc | `menu_popup`'s `[menu_dseg]`, `menu_relayout`'s `[menu_seg]` and `MB_SEG`, `ui_grow`'s two `[wm_dmg_rzwin]` (one as `xor ax, ax` / `mov`, AX being banked) and `[wm_dmg_stwin]`, `ui_sys_open`'s `[ld_pwin]`; `menu_drop`'s `[menu_sel]` as `or word ..., -1` | -8 / -8 |

### 5. Two kern_big-only parity pads, retired as pairs

| pad | where | its pair |
|---|---|---|
| `nop` after `wm_nat_take_wh` (pass 13) | wm.inc, `[sch_switch, wm_paint_all)` | `wm_timer`'s kern_big-only `clc` (+1) |
| `db 0` after `clip_reloc equ mem_rr_none` (pass 15) | clip.inc, `[wm_paint_all, ui_task)` | `wm_an_open` (-8) and `inst_park_req` (-1), kern_big-only |

-2 / 0. (`os88rdiff` reports `wm_nat_take_wh` and `clip_drop` one byte
shorter: each was the pad's host span. `clip_drop` is `hot`'s and its code is
byte-identical.)

### Hot paths and parity

No routine on the brief's hot list (`wm_paint_all`, the damage repaint, the
API cell, `ui_task`'s idle pass, `t_hotpar.py`'s table) changed a byte; every
one sits at an even shift from the base on both gated kernels (`hotpar`
green at both commits) with **no new pad and two pads fewer**. The choices
that made it so:

* kern_big `.text`: `[start, gfx_blit4)` -2 (`osapi_video`);
  `[sch_switch, wm_paint_all)` -6; `[wm_paint_all, ui_task)` -30; everything
  after `ui_task` is free (no pinned `.text` routine follows it).
* kern_small: `[wm_paint_all, ui_task)` came out odd until `menu_relayout`'s
  `cs:` byte (common) joined it; that made kern_big's odd too, and kern_big's
  `clip.inc` pad went for it.
* `.cold`: `menu_kbnav`'s -2 is the only `.cold` change ahead of `dsk_xfer`,
  `dsk_runcap` and `dsk_next_clus_x`; nothing moved ahead of `gfx_blit1_x`.

What the items do to paths near the list: every package paint loses a
`wm_bord` call (`wm_clip_set`), OSAPI_WM_GEOM one, every title bar draw two
(`wm_bar_span`), every OSAPI_WM_TIMER a call and a `ret`, every wake a
six-byte store, every OSAPI_VIDEO two `inc`s and two fetch bytes. The `and`
stores are +~7 clocks each against a `mov`, and every one is on an H3/H4
path (a menu popup, a menu relayout, a resize commit, a Task Manager open) -
well under 1% of the operation (a popup is ~47 ms on CGA, khp1 wm.md 2.4).
None is on the damage-repaint path (pass 15's DECLINED 3): its own
`wm_dmg_*` clears (`wm_paint_dmg`, `wm_dmg_wins`, `wm_dmg_gray`) were left as
`mov`.

## REFUSED

* **`osapi_seg`'s refusal on `CLC_OR_STC`** (-1 `.cold`, kern_big): built,
  correct, and backed out on PARITY - it is the one change possible ahead of
  `gfx_blit1_x` in `.cold`, so that routine moved odd. **Held to pair** with
  any other odd `.cold` byte in kernel.asm's early `.cold` (before the
  includes).
* **`osapi_img_name`'s refusal as the exit's swallowed `stc`**: +1, not -1 -
  the scan's fall-through then needs a jump to it.
* **`api_rn`'s tail as `jmp short api_rs.tail`** (-1): built; it was the one
  odd byte ahead of `gfx_blit4` and the other half of that interval
  (`osapi_video`) is even. Free to whoever moves an odd byte in kernel.asm's
  API stubs.
* **`wm_wake`'s six-byte store kept and `wm_wk_rec` moved into the zero
  run**: no - the run is `wm_init`'s, and the record would still need its
  type written once.
* **The `.cold` `ct_*` trampolines' site counts**: re-counted against the
  current tree; every one still pays or is a wash as its comment says.
* **`cw_*` shims onto existing cells**: only `gfx_fill` and `gfx_frame` have
  a cell, and kernel.asm's own note keeps those two off it for the repaints
  that call them by the dozen.
* **`app_launch` far-calling the KD_INIT bodies** so `fm_kinit`'s and
  `hb_kinit`'s resident thunks go: the call costs 6 more bytes at the site
  (`push bp` / `mov bp, ax` / `push cs` / `call api_far` / `pop bp`) for the
  12 the two thunks are, but `hb_kinit` is in `hiber.inc` (sys's row) - see
  CROSS-FILE. With only `fm_kinit`'s it is 0.
* **The six- and seven-push prologues on `kent_di`/`kent_bp`** (`wm_su_vset`,
  `wm_su_bytes`, `wm_su_scrset`, `wm_su_flay`, `wm_clip_split`,
  `wm_clip_rows`): -3 or -4 each but ~+90 clocks a call, and every one is the
  raise cache the owner holds for cycles (S3) or the clip region (H2).
* **`wm_su_edge` written at its one call** (-4): relaxes `wm_su_try`'s piece
  loop's backward branch (+3), and it is S3's.
* **`wm_timer`'s two exits as one**, **`snd_town_off`'s second `xor ax, ax`**
  (the card path's `drv_svc_call` may return AX), **`fpg_begin`'s
  `[fpg_frac]` seed from CX**, **`menu_popup`/`menu_pull`'s menu height as a
  helper** (-1 for a call on a menu open), **`wm_an_open`'s arm cleared after
  `wm_anim`** (-6, superseded by TAKEN 2's -8): 0, unsafe or less.
* **The About box's strings composed** (`app_about_v0..v3` share
  `" - 16 colors"` and a layout but no tail): the code to compose them is
  larger than what they would share.
* Pass 17's refusals were not re-proposed: `toast_say` into `.cold`,
  `sched_stop`'s interrupt windows, `ui_tdbl` into `ui_task` (this pass
  shrank `ui_tdbl` where it stands instead), `wm_su_try`'s `wm_cpy4` copies,
  `wm_dmg_gray`/`wm_su_owed` at their calls, `W_FLAGS` byte forms.

## WANTS THE OWNER

None. Every item is byte-identical behaviour.

## WANTS AN ABI BREAK

Nothing new beyond pass 17's list (P18 handoff section 4), which still
stands as priced there. Two observations for the gatherer:

1. **OSAPI_WM_TIMER's kern_big target is the body now** (TAKEN 3): if the
   window-field setter break (handoff 4 items 2 and 3) is taken, the timer
   setter can join it with no wrapper to retire.
2. **`OSAPI_SEG`'s refusal shares its exit only on kern_big**; a break that
   renumbers the table could also give kern_small's arm the same shape
   (-1 `.cold`) without the parity question (REFUSED 1).

## CROSS-FILE

* **`hb_kinit` (`kernel/hiber.inc`, sys's) and `fm_kinit` (kernel.asm)**:
  6-byte `call COLD_SEG:x_x / ret` thunks that exist only because
  `app_launch` near-calls `KD_INIT`. With `app_launch` far-calling the
  bodies through `api_far` (+6 at the site) and `inst_kinds` naming
  `fm_kinit_x` / `hbf_kinit` / a `.cold` `cp_kinit` directly, kern_big is
  **-6** net (-12 + 6), kern_small 0. Needs `hiber.inc`'s two lines; not
  taken from this row.
* **`clip_drop`** (`hot`'s): its `push dx / pop dx` bank no caller reads -
  both call sites are `clip_put`'s, which reloads DX from `mem_claim` or
  leaves through `kret_bp` (~-2). Not edited.
* **`toast_now`** (`dir`'s): read, nothing found.
* **`mod_needi`** (P18 handoff 6): `ctrl.inc`'s `cpf_need` and
  `cpf_cp_flush_close` are two more `push ax / mov al, MOD_CTRL / call
  mod_need` sites for it.

## ROWS RUN

ONE pass at the tip, nothing edited meanwhile,
`python3 tools/os88test.py soak -k ... --marty-jobs 1`, MartyPC on the
GLaBIOS twins (nothing needed the IBM ROM), tip
`c516e02e`: **26 passed, 0 failed, 0 skipped** in 859 s.

| rows | what of this pass they reach |
|---|---|
| `wmchrome`, `sizesnap`, `clipkeep`, `disptitle`, `dispthm`, `dispdrag` | the side border asked once (`wm_geom`, `wm_clip_set`, `wm_bar_span`; `sizesnap` drives the spanning window SPEC.md 11.95.3 is about), `wm_resize_nb` |
| `paintsu`, `tmrepair`, `dispband` | `wm_band` (a band set, changed and refused), the raise cache through `wm_bar_span` |
| `wakedrain`, `evqfull`, `timerbounce`, `timerrace` | the initialised wake record, `wm_timer` as the cell's routine |
| `alertanim`, `dispzoom`, `dockmark`, `deskzoom` | `wm_an_open`'s arm, `wm_show_b`'s `.onglass`, the title-bar double-click (`ui_tdbl`) |
| `dispapp`, `dispsize`, `dispfit`, `dispclose`, `paintrz`, `paintshrink` | `wm_create`, `osapi_video`, `ui_grow`'s stores, `wm_resize_nb` |
| `tmload`, `desksc` | `ui_sys_open` (by name, running and not), `inst_set_name_x`, `menu_popup` and the menu bar's relayout |
| `trkcompact` | `inst_park_req` |

No row drives `menu_kbnav` (the keyboard mouse inside a dropped menu): its
change is a branch target and a deleted copy, checked against the listing
(`os88rdiff --show menu_kbnav`: the `jmp short .no` became the `stc / retf`
it jumped to). `ui_dispatch`'s `cbw` and `ui_sys_open`'s were checked
against every producer of AL there (menu item indices below 16; LD_E* codes
1..6).

## DEFECTS FOUND

None. (One reading corrected on the way: `menu_kbnav`'s `.noj` trampoline was
already a SHORT jump - NASM had shortened it - so turning it into the refusal
saved nothing by itself; the bytes are the foot's deleted copy, measured with
`os88rdiff --show menu_kbnav`.)

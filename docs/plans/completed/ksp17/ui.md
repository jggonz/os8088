# ksp17 agent "ui" - the UI task, the menu bar, toasts, the widget, sound, the scheduler and the API shims

Kernel size pass 17 (`docs/plans/completed/ksp17/BRIEF.md`), section 1's
`ui` row: a general finder over `kernel/ui.inc`, `menu.inc`, `toast.inc`,
`fprog.inc`, `snd.inc`, `sched.inc`, `ctrl.inc`'s resident half and
`kernel/kernel.asm` (the API table's shims, `kmain`, everything the fresh
agent's hunks did not add). Branch `ksp17-ui`, base `00348bdc` (the brief,
on `af5579d2`). Figures are ASSEMBLED bytes, `tools/kernsize.py --json`
absolutes; resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` +
`.vga` (+ `.mega` on kern_mega).

## WHAT THIS PASS READ, AND THE SHAPE THAT PAID

Pass 16's gui agent wrote ~20 single-caller routines in at their one call.
This pass looked one step further out: **a helper whose callers are all one
routine, or a family that only ever runs inside one composition, banks
registers for callers that bank them already.** The menu bar's composition
is the clearest case - `menu_draw_bar` banks AX..DI, `menu_bt_raw` banks
AX..DI and ES, and every helper below them (`menu_bent`, `menu_bpad`,
`menu_bpadc`, `menu_bstr`, `menu_bemit`, `toast_place`, `menu_sound`) banked
its own registers a second time, for callers that reload every one of them
before reading it. The finder was a scratch census (every top-level label in
the area that opens with pushes, with every reference to it), read caller by
caller for what each holds live across the call.

## BASE AND TIP

| kernel | base `00348bdc` | tip | delta |
|---|---:|---:|---:|
| **kern_big** resident | 91,879 | **91,751** | **-128** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` / `.vga` | 40,925 / 5,435 / 37,793 / 5,358 / 336 / 2,032 | 40,799 / 5,435 / 37,791 / 5,358 / 336 / 2,032 | -126 / 0 / -2 / 0 / 0 / 0 |
| kern_big `KERN_SIZE` / `.boot2` / `.ovl` | 97,280 / 6,455 / 5,116 | 97,280 / 6,455 / 5,116 | 0 / 0 / 0 |
| **kern_small** resident | 57,718 | **57,601** | **-117** (`.text` 30,494 -> 30,379, `.cold` -2) |
| **kern_emu** resident | 92,133 | **92,005** | **-128** (`.text` 41,179 -> 41,053, `.cold` -2) |
| **kern_mega** resident (with `.mega`) | 125,183 | **125,055** | **-128** (`.text` 46,167 -> 46,041, `.cold` -2, `.mega` 21,712 unchanged) |

`.bss`, `.lowbss`, `.vga`, the blob, `.ovl`, every module and every driver
are unchanged. `make`, `make small`, `make emu` and `make mega` build at the
tip (the `megaseg` gate green: kern_mega's `.text` only shrank), and the fast
tier is green at both commits, `hotpar` and `stkbalance` included.
`tools/os88rdiff.py kernel 00348bdc HEAD` is the per-routine record below.

## TAKEN

kern_big bytes off `os88rdiff` routine spans (they add to the `kernsize`
figure exactly: `.text` -126, `.cold` -2). kern_emu and kern_mega take every
item; kern_small takes those whose code it assembles (not `osapi_fs_op`,
`toast_show`'s queue arm or `ui_drag`'s two-display arm), and pays +1 for
`menu_draw_bar`'s wider bank (item 1c) where it has no `menu_sound` to give
it back - so -117 there.

### 1. The menu bar's composition (`menu.inc`, `toast.inc`): -91

| item | kern_big | what went |
|---|---:|---|
| a. `menu_check` written into `menu_draw_bar` (its one caller) | -6 | its call, `ret` and BX bank (kent_di has BX) |
| b. `menu_logo_glyph` written into `menu_draw_bar` | -11 | call, `kent_di`/`kret_di`; the left x goes straight into DI (`mov cx` + `mov di, cx` was one `mov di`) |
| c. `menu_sound`'s bank moved to `menu_draw_bar` | -2 | `menu_sound` (one call and one fall, both `menu_bt_ent`'s) banked AX..DI, BP and ES for `menu_draw_bar`, which banks AX..DI itself; it banks BP and ES now too (`kent_bp`/`push es`/`kret_es`, +1, one shape on both kernels because `stkbalance` reads the source and not the `%ifdef`) and `menu_sound` keeps BP and ES only (-3) |
| d. `toast_place`'s `kent_di`/`kret_di` | -5 | both calls are the composition's: `menu_bt_ent` (whose callers are `menu_draw_bar`, banked, and `ui_task`'s clock step, which holds nothing across it - BL is read before the call) and `menu_bt_raw` (banked at its entry) |
| e. `menu_bent` banks nothing | -12 | five pushes and pops; its two calls are `menu_bt_raw`'s, which keeps only BX across them - and `menu_bent` never writes BX. The logo cell's `je` goes to `menu_bpadc`'s `ret`, and the `call menu_bstr / ret` tail is a `jmp` |
| f. `menu_bpad` falls into `menu_bpadc`; neither banks | -12 | `menu_bpadc`'s target moves BX -> DX (`xchg ax, dx`), because `menu_bent`'s callers keep BX across it; every caller's DX is dead (`.diff` loads it) |
| g. `menu_bstr` banks nothing | -4 | AX and SI: both callers read neither after |
| h. `menu_bemit` banks nothing | -5 | its one call (`.pemit`) banks the two words its walk wants round it |
| i. `menu_relayout` falls into `menu_ab_cell` | -11 | the call, and `menu_ab_cell`'s `kent_di`/`push es` and its own copy of the exit; it is a part of `menu_relayout` now (its labels are `menu_relayout`'s locals) |
| j. `menu_ab_cat` banks nothing | -6 | `menu_ab_cell` reloads AX, CX and SI before reading them |
| k. `menu_widest` takes the count in AX | -6 | both callers had just called `menu_setup`, whose AX IS the clamped count, and moved it to CX (`xchg`, and `push cx / mov cx, ax / pop cx`); BX not banked (both callers reload it). `menu_track`'s `mov cl, 1` leaves CH as the press x's high byte where it was 0 - `menu_pull`/`menu_drop` read CL alone (checked: CX is written before it is read in `menu_drop`) |
| l. `menu_title_xor` banks AX alone | -3 | `menu_track` banks BX..DI and reloads all but SI, which IS `[menu_ent]` on both calls; the second call hands back the pick in AX |
| m. `menu_trunc` banks nothing | -6 | its one call (`menu_drop`'s item loop) banks AX round it, reloads CX next and holds nothing in DI |
| n. `menu_kbnav`'s row: `sub ax, [menu_y1] / dec ax` | -2 `.cold` | for `mov bx, [menu_y1] / inc bx / sub ax, bx`; the `dec` sets the SF the `js` reads, and BX is `.place`'s to load |

### 2. Single callers written at the call (`ui.inc`): -24

* `ui_sys_find` and `ui_tm_sysdir` written into `ui_sys_open` (-15): the
  name scan's miss FALLS INTO `.load` (no `stc`, no `jc`), and SYSTEM's four
  refusals join the existing `.nofile` arm, so the caller's own
  `mov si, ui_s_nofile` and the routine's `stc`/`ret` go. `kent_bp` banked
  every register both spent.
* `ui_drag_phase` written into `ui_drag` (-9): its call and its
  `kent_di`/`kret_di`. `ui_drag` holds only BX across it, which it never
  wrote; `EXT_PHASE` keeps every register as before.

### 3. Smaller bodies

* `toast_show` (kern_big): the queue arm and the retire arm share ONE store
  of `[toast_qwant]` - AL (kent_di's) is 0 or 1 and `SKIPF2` steps over the
  queue's `mov al, 1`; the `jmp short .kq` over the block goes (-4).
* `snd_tone_req` banks BX and SI only (-4): nothing in it writes CX, and DX
  comes back as it came (its one write, DL for the sink, is banked round the
  call by its own inner push).
* `osapi_fs_op` (kern_big): `CLC_OR_STC .no` for `jmp short .out / .no: stc`
  (-1); only CF is the cell's answer.

### 4. The rare cold cells' stubs (`kernel.asm`): -4

`api_rxc`, `api_rsc` and `api_rn` reached `api_far` with
`call KERNEL_SEG:api_far` (5 bytes); `push cs / call api_far` (4) builds the
same far frame (`push cs` is KERNEL_SEG in this code on every kernel - on
kern_mega it is the identity selector `0x60`) (-3), and `api_rsc` jumps to
`api_rs`'s three-instruction tail (-1). Rare cells by definition; the
`push cs` + near call is ~1 clock dearer than the far call on an 8088.
`api_far`'s label carries `ovlchk: builds its own far frame` - its `retf` is
the jump into COLD_SEG:BP on a frame it pushed itself, which is what that
marker declares, and `os88ovlchk` refuses a near call to a `retf` body
without it.

### Hot paths and parity

Nothing on the hot list changed: `ui_task`'s idle pass, `sch_isr`,
`sch_switch` and the API cell are byte-identical. What moved near them:
`menu_draw_bar` (every window operation, not hot) and the bar composition
got cheaper (no `kent_di` call/`ret` pairs - a `kent_di` call and its
`kret_di` jump are ~70 clocks - and `menu_logo_glyph`'s, `menu_check`'s
calls gone); `ui_drag`'s release and the Task Manager open lost a call each.

**No new parity pad.** `hotpar` green on both gated kernels at both
commits. The pass's items were chosen to keep the shifts in front of the
hot routines even - two candidates were REFUSED for that alone (below). The
shift runs at the tip: `[api_fdlg_open, menu_win_set)` -4 (gfx_blit4,
font_run*, the mouse ISRs, kbm_isr, cur_move*, sch_isr, sch_switch,
wm_paint_all), `ui_task` -98, `.cold` -2 from `menu_kbnav` on (dsk_xfer,
dsk_runcap, dsk_next_clus_x).

## REFUSED

* **`toast_say` into `.cold`, near, with `ct_toast_say` an `equ` of it**
  (-4: the 6-byte thunk goes, +2 for a far `toast_show` through the cell):
  built, measured, and backed out on PARITY - the move is -35 of `.text`
  ahead of `ui_task` and +37 of `.cold` ahead of `dsk_xfer`, both odd, and
  pairing both needed two odd bytes this pass did not have (the one `.cold`
  pad in reach, `menu_kbnav`'s, is the coordinator's). **Worth retaking at
  the close if the pad pairing has two odd bytes to spend.**
* **`sched_stop`'s two IF = 0 windows as one** (-3: the PIT reset joins the
  vector restore, before the unhooks): parity - it is the only odd change
  possible in `[sched_stop, sch_isr)`, so `sch_isr`, `sch_switch` and
  `wm_paint_all` moved odd. Retakeable with any other odd byte there.
* **`ui_tdbl` written into `ui_task`'s title-bar press** (-8): +29 bytes,
  ODD, inside `ui_task` ahead of the idle pass (`.yield` onward), so every
  idle-pass instruction after it would change parity.
* **`toast_say`'s range test as one unsigned compare** (`dec ax / dec cx`,
  -1): pass 16 sweep's refusal (wrong for CX = 0) still holds; every caller
  passes a constant length, but the contract does not say so.
* **`cp_tick_due_x`'s AX bank** (-2): `cpf_cp_tick` reaches CTRL.DRV's tick
  with whatever registers `ui_task` holds, and nothing states the module's
  tick reads none.
* **`task_cycles`' BX bank**: `wm_draw_win` (the damage-repaint path,
  DECLINED 3) reads BX after it.
* **`snd_town_off`'s banks**: five callers, one of them the IRQ0 tail.
* **`sch_account` through `sch_currec`** (-5): a call/`ret` on every IRQ0.
* **ui_cmd's six `call COLD_SEG:x / ret` arms through one far table**:
  ~-3 for a second table, not worth it.
* **The defensive `cld`s**: pass 13's DECLINED list.

## WANTS THE OWNER

None. Every item is byte-identical behaviour.

## WANTS AN ABI BREAK

All three are FLAG DAYS on the API table (SPEC.md 20.8 rule 4): every cell
after the first one changed moves, so every package in the tree rebuilds,
`apps/os88api.inc` is regenerated, `PKG_FMT` bumps and `tools/abilock.py`
records it. None is taken.

1. **The five-byte rare cell** (`call api_rX / dw target`, the cell's
   `push bp` moved into the stubs): **-119 bytes of table** (119 rare cells
   at 6 today, `OSAPI_FS_OP`/`OSAPI_VOL_CHECK` included), less ~+6 a stub in
   the five stubs (`api_rs`, `api_rx`, `api_rxc`, `api_rsc`, `api_rn`):
   the stub must now bank BP itself and drop the near return the cell's
   call left above the far frame without touching the flags a CF answer
   rides in - `push bp / mov bp, sp / xchg bp, [bp+2]` and a flag-safe
   discard (`pop` into a banked register) - **~-90 resident net, ESTIMATED**,
   for ~+20 clocks on every rare call. Pass 13 priced it at ~-105 and the
   owner declined it then as an ABI break on `main`; it is re-priced here
   because ABI breaks are being gathered.
2. **Rare-ify the SLOT cells that are not hot**: an 8-byte `OSAPI_SLOT` as a
   6-byte `OSAPI_RSLOT` is **-2 a cell** for ~+85 clocks a call (SPEC.md
   20.3's figure). Candidates by what they are, not by measurement:
   `osapi_rand` (0x00C5), `osapi_snd_tone` (0x00D3), `osapi_snd_play`
   (0x00DB), `osapi_video` (0x0127), `inst_pkg_alive` (0x0135), `xm_copy`
   (0x016C), `wm_damage` (0x02CB), `inst_task_park` (0x0318), `wm_timer_c`
   (0x033A), `wm_wake` (0x034F), `api_gfx_save`/`api_gfx_rest`
   (0x03DC/0x03E4), `cur_busy` (0x040B) - **-26 for all thirteen**. Each
   wants a per-cell call-frequency reading first (a counter in the cell, or
   a grep of every package's per-frame paths): a game that calls one per
   frame is the case that refuses it.
3. **`drv_pkg_call_x` has two cells**, the SLOT at 0x038B and the XCELL at
   0x0348 - one routine, two doors. Retiring the SLOT is **-8** and moves its
   callers to the XCELL (ES = the caller's DS on top of everything the SLOT
   did, which a caller that passes nothing in ES does not notice).

## CROSS-FILE

None edited outside this row's files. `menu_widest`'s and `menu_trunc`'s
callers, `toast_place`'s and every helper above are all in `menu.inc` /
`toast.inc` / `ui.inc`.

## DEFECTS FOUND

None.

## ROWS RUN

Once, after the last code commit (`02de58f0`), nothing edited while they
ran: `python3 tools/os88test.py soak -k ... --marty-jobs 1`, MartyPC on the
GLaBIOS twins. **27 / 27 ok** in 1,346 s.

| rows | what of this pass they reach |
|---|---|
| `dispdrag`, `dragui`, `dispfit`, `sizesnap`, `zoomsave` | `ui_drag` with `ui_drag_phase` written in, the drag cache's phase |
| `tmload`, `tmsmall`, `tmhover`, `romnodisk` | `ui_sys_open` with the name scan and SYSTEM's step written in (a load, a re-front of the running one, kern_small, and kern_small's ROM arm) |
| `stikmenu`, `wdmenusu`, `mouseup`, `desksc`, `kbd4f`, `dispapp`, `dispclose`, `dispcheck`, `disptitle`, `alertanim` | the bar's composition, `menu_relayout`/`menu_ab_cell`, `menu_track`/`menu_title_xor`, `menu_popup`/`menu_widest`, `menu_drop`/`menu_trunc`, the keyboard's menu walk |
| `toastbar`, `toastfpg`, `fpgop` | `toast_place`, `toast_show`'s queue, `osapi_fs_op` |
| `mixervga`, `mixerherc`, `sndcap`, `bigvol` | `menu_sound` and the speaker's cell; `snd_tone_req` (the beep and the tone) |
| `instimg` | a rare cold cell through `api_rsc`'s new tail (`OSAPI_IMG_NAME`) |

The fast tier (`toast`, `seg0`, `hotpar`, `stkbalance`, `ovlchk`, `apiabi`
among it) is green at both commits. No row was broken on purpose this pass.

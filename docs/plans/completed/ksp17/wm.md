# ksp17 agent "wm" - the window manager, the instance table, the dock, the event ring

Kernel size pass 17 (`docs/plans/completed/ksp17/BRIEF.md`), section 1's `wm`
row: a general, in-depth finder over `kernel/wm.inc`, `instance.inc`,
`clip.inc`, `events.inc`, `dock.inc` and `apps.inc`. Branch `ksp17-wm`, base
`00348bdc` (the brief, on `af5579d2`). Figures are ASSEMBLED,
`tools/kernsize.py --json` absolutes on each kernel's own build; resident =
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga` (+ `.mega` on
kern_mega). Per-routine figures are `tools/os88rdiff.py kernel 00348bdc .`.

## HOW IT WAS READ

Every top-level label in the six files was counted by reference across
`kernel/`, `drivers/` and `boot/` (a scratch census, comments stripped), and
every routine was then READ - the single callers for what the call site holds
live, and the rest for the shapes pass 16's lessons name: compares made
twice, a value loaded that a register already holds, a store whose flags no
one reads, a tail two routines share. Three things were checked for every
item before it was built: who else names the label (the tests and tools
break on some of these by name), whether the inlined body pushes a short
branch of its host out of reach (measured, not predicted: one was), and
whether the code sits on the damage-repaint path (pass 15's DECLINED 3).

## BASE AND TIP

| kernel | base `00348bdc` | tip | delta | where |
|---|---:|---:|---:|---|
| **kern_big** | 91,879 | **91,722** | **-157** | `.text` 40,925 -> 40,794 (-131), `.bss` 5,435 -> 5,419 (-16), `.cold` 37,793 -> 37,783 (-10) |
| kern_small | 57,718 | **57,579** | **-139** | `.text` 30,494 -> 30,377 (-117), `.bss` 2,963 -> 2,947 (-16), `.cold` 21,393 -> 21,387 (-6) |
| kern_emu | 92,133 | **91,976** | **-157** | as kern_big (`.text` 41,179 -> 41,048) |
| kern_mega (with `.mega` 21,712) | 125,183 | **125,026** | **-157** | `.text` 46,167 -> 46,036, `.bss` 5,473 -> 5,457, `.cold` 45,925 -> 45,915 |
| kern_mega segment overhead (`os88megaseg`) | 3,248 | 3,248 | 0 | within `tests/megaseg.txt`'s 3,250 |

No `.lowbss`, `.vgabuf`, `.vga`, `.boot2`, `.ovl`, `.cfx` or module byte
moved; `BOOT2_SECS` unchanged; `KERN_SIZE` 97,280 / 59,904 unchanged (no
rung crossed either way, and per the banner not the point). Every item is
on all four kernels except the ones marked kern_big (which kern_emu and
kern_mega share), so kern_big and kern_mega move together and the megaseg
overhead does not move. `make`, `make small`, `make emu` and `make mega`
build at the tip; the fast tier is green (70 passed, 1 skip) and
`tools/stkbalance.py` over the six files reports 32 unbalanced paths at the
tip against 36 at the base - every one a ladder tail (`kret_*`), the four
fewer being routines that are now written at their calls.

## TAKEN

Bytes are kern_big (= kern_emu) / kern_small / kern_mega, routine spans from
`os88rdiff`. `disk.inc`'s `align 2` after `dsk_img16` swallows one byte of an
odd `.text` total on kern_big, so the spans add to one more than `kernsize`
reports there.

### 1. Written at the one call

| routine | into | kern_big | what went with the call |
|---|---|---:|---|
| `wm_pref_take` | `wm_pref_now` | -6 | its own five banks were the outer one's: `wm_pref_now` IS the take now, with `call wm_kind_now` after the pushes; AX, the CF answer and every other register as before |
| `wm_wake_eaten` | `evq_mup` | -4 | the call and `ret`; the `jne .out` is `jne .dl`. `t_wakedrain` still sees `EVT_WAKE` after the `evq_pop` |
| `wm_su_occl` | `wm_su_try` | -11 | five pushes and `jmp kret_si` - wm_su_try banks AX..ES itself and reloads all of them after. `.occl` is a zero-byte local the `tmrepair` and `zoomsave` rows now break on (`wm_su_try.occl`, DI = the window as before) |
| `wm_fs_zpos` | `wm_paint_all` | -9 | three pushes and `jmp kret_cx`: AX..CX are dead at `.win` (wm_znext reloads AL/BX, BP is the top). Faster on every fullscreen repaint |
| `wm_ttl_seen` | `wm_draw_win` | -10 | four pushes and `jmp kret_dx`, and the `je wm_clip_free` is a `je` to the label below with CF = 0 off the `cmp`. W_PAINT's contract is SI alone (SPEC.md 11), and AX..DX were already the shadow's leftovers. A call and eight stack operations quicker per window drawn |
| `wm_an_seed` | `wm_an_open` | -6 | `push ax`/`pop ax`, the call and the `ret`; `.half` stays a local subroutine. wm_show_b, the one caller, banks AX |
| `wm_an_arm` | `inst_unmin` | -4 | `rect_put` + the arm byte at the call |
| `wmz_wm_chrome_relit` | is `wm_chrome_relit_x` | -4 `.cold` | the body ends in `retf` (and `kretfc_di`), so the far entry is an `equ` and the `call`/`retf` layer went. The name `t_chromesites` reads stays |

### 2. Banks no caller reads

* `wm_su_piece` (-4): its one caller (`wm_su_try`'s piece loop) holds
  nothing in AX or SI there - `wm_su_srect` reloads both next. Written at
  the call it saved 8 and pushed the fragment walk's three backward jumps
  out of short reach (+9 measured), so it stays a routine.
* `wm_su_srect` (-6): no SI bank (its caller's, as above), and its three
  exits are one: `jnc .out`, `rect_get`, `CLC_OR_STC .none`, `ret`.
* `wm_ask_size` (-2): the handler loaded and tested in one (`mov bp` /
  `or bp, bp`), as `wm_ask_close` already does.
* `wm_wake_call` (-2): `mov si, bx` restored SI to itself.
* `db_rclick` (-5) and `db_hit` (-2, kern_big): the slot was banked across
  the popup and the record re-derived with `inst_ptr`, but the record is a
  slot of the static `inst_tab` and `menu_popup` preserves DI - so DI is
  carried, the bank and the call went, and `db_hit`'s push/pop of AX around
  `db_key` (its only purpose was the slot nobody reads now) went with them.

### 3. Data

* **`inst_tplbuf`, 16 bytes of `.bss`, gone** (-7 `.text`, -16 `.bss`):
  `app_launch` cascades the kind's template IN PLACE for the one
  `wm_create` call (`add [si],ax` / `add [si+2],ax`, then `pushf` / `sub` /
  `sub` / `popf`). `wm_create` copies the record's first sixteen bytes out
  before it reads anything else and preserves AX and SI. All four kinds'
  templates (`app_tpl_about`, `fm_tpl`, `cp_tpl`, `hb_tpl`) are `.text`,
  written by nothing else and read by nothing but this, and `app_launch` is
  UI-task only, so nothing ever sees a template moved. Pass 15 refused the
  -16 for a different shape - create from the template, offset AFTER - which
  bypassed `wm_fit` and the natural bank; this one offsets before, exactly
  as the copy did, and `wm_fit` sees the same rect.
* **`wm_cov_c1..c4` are `wm_clip_split`'s `wm_cl_rx1..ry2`** (-8 `.text`):
  written and read only inside `wm_cov_rect`, which calls nothing that
  reaches the region code (`wm_su_rect_bx`, `wm_rect_empty`, `wm_zabove`,
  `wm_znext`, `wm_win_rect_bx`, `gfx_rect_isectcf`, `wm_runion` - all
  arithmetic, read), and split's bank lives from its first instruction to
  its last and calls only `wm_cpy4` and `wm_clip_add`. Both run under the
  gfx lock, so no other task is between them.
* `wm_su_bget` (-9): the four extent bytes as two words folded to one byte
  (`wm_su_flay`'s own test of `wm_su_kex`), no SI bank, and the refusal
  inverted so the common no-band answer is the fall-through - faster on the
  damage path, where `wm_draw_win` asks it per window.

### 4. Smaller bodies

* `wm_destroy` (-5): the three zero stores off one `xor ax, ax` (AX is
  banked and spent by `wm_ptr2idx` next), and `xchg si, ax`.
* `app_launch` (-2): BL = the kind off AL (inst_alloc preserves AX) instead
  of a reload of `inst_knd`.
* `app_about_center_x` steps the 16px line pitch itself (-6 `.cold`): it
  has one caller now (the header's claim that `ui_note_paint_x` used it was
  stale), so the three `add dx, 16` after its calls are one at its head.
* `and word [x], 0` for `mov word [x], 0` where the flags are dead (-1
  each, 13): `wm_su_pcrp` (wm_rz_precover), `wm_dmg_mine` x2 (wm_raise),
  `wm_fs` (wm_fs_drop), `[si]` (wm_su_drop), `wm_su_von` x2 (wm_dc_take,
  wm_su_try), `wm_dc_win` x2, `wm_su_fn` (wm_su_take, wm_su_try),
  `wm_su_pi` (wm_su_try), `I_WIN` (inst_unwin); and `wm_su_scrset` stores
  the SI it just zeroed (-2). None on the damage-repaint path: the
  `wm_dmg_*` clears there were left as `mov`.
* `inc byte` for a store of the tested value + 1 (-1 each): `I_STATE` 1 -> 2
  in `app_close_win`, `W_WKQ` RING -> EATEN in `evq_mup` (an `%if` pins
  `WKQ_EATEN = WKQ_RING + 1`).
* `xchg si, ax` for `mov si, ax` after `wm_ptr2idx` where AX is reloaded
  next: `inst_of_win`, `inst_bind_win` (-1 each).
* `inst_wchk` (-2): the `jc` after `sub ax, wm_wins` - BELOW the table
  wraps high, so the one unsigned compare is both ends (wm_isrec's test,
  same input class).
* `wm_create` (-1): the z-order append through `[di+wm_zord]`.
* `wm_min_axd` (-1): the width's floor loaded before the DL test and the
  height's over it, so the two answers share one `ret` (every path's flags
  as before: `mov` writes none).
* `wm_damage` (-1): the empty rect's DX by `cwd` of the AX = 1 just loaded
  (no flag; CF = 0 still the `xor`'s).
* `inst_park_mk` (-2, kern_big): the second `xor bh, bh` - BH is still the
  0 the first wrote (`mov bl` leaves it). On every `gfx_lock`, and faster.
* `clip_get` (-1): the empty answer is `mov cx, ax` - clip_size's 0 - with
  CF still clip_size's 1, for `xor cx, cx / stc`.
* `wm_hide` and `wm_front` (-1 each): their four-line doors stand beside
  `wm_show` now, so `jmp wm_lk_tail` is short.

### Hot paths and parity

No routine on the brief's hot list changed a byte but `wm_paint_all`, which
is on `t_hotpar`'s list and gained `wm_fs_zpos`'s walk (faster: a call, a
`ret` and six stack operations fewer under a fullscreen window). `hotpar` is
green on both gated kernels at both commits with NO pad - but the second
batch's first build was red on THREE (kern_big `ui_task` and `wm_paint_all`
on both kernels, odd by one), and the answer was chosen per region rather
than padded: an odd common byte in front of `wm_paint_all` (`wm_min_axd`),
an odd common byte between it and `ui_task` (`wm_damage`'s `cwd`), and the
kern_big-only bytes in that stretch made even (`inst_park_mk`'s -2 in, and
`wm_su_flay`'s `inc bx` for `not bl` (-1) taken back out - see REFUSED). Every
hot entry is now at an even shift from the base on both kernels
(`os88rdiff --all`), and the build after confirms it. What the items do to the
paths near the list:

* The damage-repaint path (DECLINED 3) only got cheaper: `wm_draw_win`
  loses a call, a `ret` and eight stack operations per window where the
  region is armed (a call and a `ret` where it is not), `wm_su_bget` takes
  the fall-through on the common no-band answer, `inst_of_win` (via
  `inst_win_owner`, per window) a one-byte `xchg`. No `kent_*` was added
  anywhere, and the `wm_dmg_*` zero stores stayed `mov`.
* The raise cache (S3's territory - the owner holds `rect_get`/`rect_put`
  there for CYCLES): every save-under item here removes instructions
  (`wm_su_occl`'s banks, `wm_su_piece`'s, `wm_su_srect`'s, `wm_su_bget`'s);
  none converts a four-`mov` group.
* `evq_mup` per eaten wake, `wm_paint_all` under a fullscreen window: a call
  and a `ret` cheaper.

## REFUSED

* **`wm_su_piece` written at its call** (-8): relaxes `wm_su_try`'s three
  backward branches over the fragment walk (+9 measured). Its banks went
  instead (TAKEN 2).
* **`wm_su_try`'s two four-word copies through `wm_cpy4`** (-13 each, -26):
  `wm_su_bx1..by2` <-> `wm_su_sx1..sy2` at the bank and at `.out`. The call
  costs ~+150 clocks a restore on the raise cache, which is S3's trade the
  owner holds (LAST-DROP-BYTES 7.10).
* **`W_FLAGS` high-byte forms** (`and/or/test byte [bx+W_FLAGS+1], ...` for
  the word forms of `WF_1BPP`, `WF_STALE`, `WF_NOANIM`): built and measured
  at 0 - `W_FLAGS` is 0, so `[bx]` has no displacement and the byte form
  needs one.
* **`wm_su_flay`'s `not bl` as `inc bx`** (-1, kern_big): built and
  correct, taken back out for parity - it was the one kern_big-only odd
  byte between `wm_paint_all` and `ui_task`, and with it no choice of the
  common bytes put both kernels' `ui_task` even. Free to whoever next moves
  a kern_big-only byte there.
* **`wm_geom`'s refusal inverted** (-1): the visible window, the common
  answer, would take the jump (+12 clocks on every package paint that asks).
* **`wm_dmg_gray` written at its call** (-1): it must still bank AX..SI for
  `wm_paint_dmg`, and its `jmp short .fill` reaches across the dither.
* **`wm_su_owed` written at its call** (~-4): `wm_dmg_wins`' draw loop's
  `jz .dnext` branches cross it and would relax - gui pass 16's
  `wm_dmg_shadowed` finding, one routine along.
* **`inst_tplbuf` aliased onto `inst_icobuf`, `wm_an_d`/`wm_an_c` or
  `wm_db_*`** (-16 `.bss`): not provable - `inst_icon_ptr` runs on a WORKER
  (its own comment: `inst_task_die` -> `wm_destroy` -> the dock paint), and
  `wm_anim` yields with the gfx lock held. The in-place cascade (TAKEN 3)
  takes the bytes with no alias at all.
* **`wm_db_r`/`wm_db_b` (16) and `wm_su_bx*` (8) aliased** onto another
  scratch: each is live across a call that can reach a package's W_PAINT or
  the region code, so no partner's lifetime can be proved disjoint.
* **`inst_pkg_alive`'s `inst_idx` range test** (~-10): dead on the live
  path (DI is a record `inst_wchk` vouched for) but it is the guard on the
  UI-task `.die -> .live` path, where DI is the caller's; splitting the two
  paths costs more than it saves.
* **`wm_keeph`'s `and`-then-`or`** (-2): the bit would read clear for an
  instant on the set path, and `wm_fit`/`wm_resize` read it from other
  tasks. (`wm_quiet` does this, but its bit has one reader, on the UI task.)
* **A shared "bill and call" helper** for `inst_win_owner` / `snd_inst` /
  `wm_pkgcall` (`wm_ask_close`, `wm_draw_win`): the two sites interleave
  different work (`task_cycles`, the charge), so there is no common body.

## WANTS THE OWNER

None. Every item is byte-identical behaviour (undocumented register values
at `W_PAINT` - CX, which already differed by path - excepted, as SPEC.md 11
gives W_PAINT SI alone).

## WANTS AN ABI BREAK

1. **The seven window-flag setters as one cell** - `OSAPI_WM_SIZABLE`
   (0x00F1), `_SNAP` (0x01F2), `_SAVEU` (0x02A4), `_OWNBG` (0x02C5),
   `_KEEPH` (0x02F0), `_NOANIM` (0x03CD), `_QUIET` (0x048D). Their bodies
   are 12 + 18 + 36 + 15 + 18 + 10 + 20 = **129 bytes** of `.text`, each a
   `pushf`/test-AL/set-or-clear-a-bit/`popf` with three tails (`wm_snap_win`,
   `wm_su_drop`, `wm_apply_wh`). One `OSAPI_WM_FLAG` (BX = window, AH = which
   bit, AL = 0 clear / 1 set) through a 7-entry mask table with a tail
   index is ~55-65 bytes, so **~-65 to -75 of `.text`, ESTIMATED**, plus the
   six retired 6-byte rare cells (-36) if the break renumbers rather than
   leaving stubs. Callers: 113 source files under `apps/` name one of the
   seven (SIZABLE 13, SNAP 32, SAVEU 18, OWNBG 31, KEEPH 14, NOANIM 4,
   QUIET 1), the C SDK's `os88.h` wrappers with them.

## CROSS-FILE

* **`wm_dg_precover`** (`wm.inc`, its one caller `ui_drag` in `ui.inc`,
  the ui agent's): written at the call it saves the call, `ret` and the
  `pushf`/`popf` + `push bx`/`pop bx` - ui_drag already reloads AX..DX and
  SI after it (gui pass 16's DEFECT note) - ~-8. Not taken: the routine is
  mine, the call site is not.
* `wm_wake_disp` (`ui.inc` caller) and `wm_wake_call` (`assoc.inc` caller)
  are single-caller too; neither saves a bank written at its call.

## DEFECTS FOUND

* `app_about_center_x`'s header said `ui_note_paint_x` centred its notes
  through it; nothing outside `apps.inc` calls it (corrected). `ui.inc`
  line ~2675's comment says the same thing and is the ui agent's.

## LABELS GATES READ

`tests/tmrepair.py` and `tests/zoomsave.py` break on `wm_su_occl`, which is
now `wm_su_try.occl` (a zero-byte local at the inlined block, DI = the
window as before) - both updated. `tests/unit/t_chromesites.py` names
`wm_chrome_relit_x`, which stays. `tests/unit/t_wakedrain.py` still finds
`EVT_WAKE` after `evq_mup`'s `evq_pop`. A grep of `tests/` and `tools/` for
every other removed name found none. SPEC.md was updated where it named a
routine as current (10.3, 74.1.1, 11.2's band paragraph, 11.96.15,
11.97.4, 11.99, 11.100.5, 120.5, 39.7, the KFZ table).

## ROWS RUN

ONE pass at the tip (`99b538f4`), nothing edited meanwhile,
`python3 tools/os88test.py soak -k ... --marty-jobs 1`, MartyPC on the
GLaBIOS twins (nothing needed the IBM ROM): **36 / 36 ok** in 2,332 s.

| rows | what of this pass they reach |
|---|---|
| `tmrepair`, `zoomsave`, `clipkeep`, `su1bpp`, `win1bpp` | `wm_su_try` (`.occl` - both rows break on the new label and counted hits), `wm_su_piece`, `wm_su_srect`, `wm_su_bget`, `wm_su_scrset`/`_take`'s stores, `wm_dc_take`/`_done` |
| `dmgcull`, `paintcull`, `disptitle`, `dispthm`, `deskclip`, `deskzoom`, `deskhover`, `desksc` | `wm_draw_win`'s title veto, `wm_cov_rect`'s shared scratch (`wm_raise`), `wm_damage`, the damage pass's callees |
| `dispdrag`, `dragui`, `sizesnap`, `dispfit`, `dispzoom`, `dispprefer` | `wm_pref_now`, `wm_ask_size`, `wm_min_axd`, `wm_rz_precover`, `wm_destroy`, `wm_hide`/`wm_front`'s doors |
| `dispclose`, `dispclose-small`, `dispapp`, `regapp`, `smalllaunch`, `tmsmall`, `tmload` | `app_launch`'s in-place cascade (every kind: About, Disk, Control Panel, a package), `app_close_win`, `inst_of_win`/`inst_bind_win`/`inst_wchk`, `inst_unwin` |
| `dispfsx` | `wm_paint_all`'s fullscreen walk, `wm_fs_drop` |
| `dockmark`, `dockpos`, `deskdock` | `db_hit`, the dock paint, `inst_unmin`/`inst_minimize` |
| `alertanim` | `wm_an_open`'s seed, the arm |
| `timerrace`, `timerbounce`, `wakedrain` | `evq_mup`'s wake bookkeeping (and `t_wakedrain`'s lint over it) |
| `wmchrome` | `wm_chrome_relit_x` as its own far entry |
| `atclip` | the clipboard (`clip_get`) |

No row reaches `db_rclick`'s context menu (no row drives a right-click on a
dock tile), `inst_park_mk` (a compaction while a parksafe worker holds the
lock) or the About box's lines; those were checked by reading - `db_rclick`
against `menu_popup`'s "everything else preserved" and `inst_ptr`'s being
pure arithmetic, the park against `sch_currec`'s `mov bl` / `xor bh, bh` /
three shifts, About against `fm_brun` -> `font_run` preserving DX (which the
old code already relied on between calls). No row was broken on purpose
this pass: `tmrepair` and `zoomsave` were the two new to this pass in the
sense that matters - they name a label it moved - and both found it.

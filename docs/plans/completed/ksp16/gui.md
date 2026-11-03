# ksp16 agent "gui" - the window, menu, drawing and scheduler side

Kernel size pass 16 (`docs/plans/completed/ksp16/BRIEF.md`), section 1's `gui`
row: a general finder over `wm.inc`, `ui.inc` (not `ui_grow`'s counting
hunk), `menu.inc`, `mouse.inc`, `font.inc`, `vga12.inc`, `softgfx.inc`,
`sched.inc`, `instance.inc`, `clip.inc`, on what pass 15's gui.md did not
read. Branch `ksp16-gui`, base `714e9d64` (the brief, on `23791571`).
Figures are ASSEMBLED bytes, `tools/kernsize.py --json` absolutes; resident =
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga` (+ `.mega` on
kern_mega). kern_mega is measured off `build/megak` (`make mega` fails at the
base on the `os88megaseg` gate, see below).

## WHAT PASS 15 DID NOT READ

Pass 15 ran four mechanical censuses over this area (prologues onto
`kent_*`, skip bytes, known carries, reloads) and took what they found. This
pass's area had moved by six lines since (`sched_stop`'s `xor`, the mouse and
winmod hunks), so there was no new code to pass; the finder was the two
shapes those censuses cannot see:

* **SINGLE-CALLER routines.** A scratch census over a `-Le -Lf` listing named
  every top-level label in the area referenced exactly once in the source,
  with its body size, its `ret` count and how it ends. ~120 candidates; each
  was read for what the call site holds live, so that a routine written at
  its call drops the call, the `ret` AND every push/pop that only existed
  because a callee must preserve what its one caller never reads.
* **A COMPARE MADE TWICE.** A caller that tests a byte before calling a
  routine whose first instruction tests the same byte (the ladder's "the
  compare is HERE" discipline, applied after the callee had its own). The
  census over the whole kernel found five; three were this area's
  (`kbm_ui`, `wm_close_pass`, `cur_shape_pass`) and the other two
  (`cur_unlazy`'s many callers, `spl_mend` in `.boot2`) are deliberate.

## BASE AND TIP

| kernel | base `714e9d64` | tip | delta |
|---|---:|---:|---:|
| **kern_big** resident | 92,136 | **91,950** | **-186** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` / `.vga` | 41,289 / 5,418 / 37,703 / 5,358 / 336 / 2,032 | 41,107 / 5,414 / 37,703 / 5,358 / 336 / 2,032 | -182 / -4 / 0 / 0 / 0 / 0 |
| kern_big `KERN_SIZE` / `.boot2` / `.ovl` | 97,792 / 6,456 / 5,119 | 97,280 / 6,456 / 5,119 | -512 (one rung) / 0 / 0 |
| **kern_small** resident | 57,984 | **57,839** | **-145** (`.text` 30,687 -> 30,542) |
| **kern_emu** resident | 92,392 | **92,204** | **-188** (`.text` 41,545 -> 41,361, `.bss` -4) |
| **kern_mega** resident (with `.mega`) | 125,598 | **125,410** | **-188** (`.text` 46,592 -> 46,408, `.bss` 5,456 -> 5,452) |

`.cold`, `.lowbss`, `.vga`, the blob, `.ovl`, every module and every driver
are unchanged (no item touches a module or a `.cold` body).
`os88megaseg`'s figure - kern_mega's surplus in the kernel segment, failing
at the base at **+3,309** against the 3,231 `tests/megaseg.txt` allows - is
**+3,307** at the tip: this pass did not add to it (the ops agent owns the
gate). `make`, `make small`, `make emu` build at the tip and the fast tier
passes; `make mega` builds the kernel and stops at that pre-existing gate.

## TAKEN

Per item, kern_big bytes off `os88rdiff` routine spans (kern_emu and
kern_mega take every kern_big item; kern_small takes the ones whose code it
assembles - not `wm_anim`, `ui_timer_pass` or `sw_pairbuild`'s word store,
which are kern_big's). `disk.inc`'s `align 2` after `dsk_img16` swallowed one
byte of an odd total twice, so the routine spans add up to a little more
than `kernsize` reports; the table is the spans.

### 1. Written at the one call

| routine | into | kern_big | what else went with the call |
|---|---|---:|---|
| `cur_shape_pass` | `ui_task` `.chk_curs` | -19 | its head compare (the caller's already) and four banks: AX..DX are dead at that step |
| `wm_an_deadline`, `wm_an_lerp`, `wm_an_pace` | `wm_anim` | -24 `.text`, -4 `.bss` | every bank (`kent_bp` owns the registers); the deadline lives on the stack for the frame, so `wm_an_dl` is gone |
| `wm_nat_bank_xy` | `ui_drag` | -7 | the position copied through a dead AX instead of push/bank/pop of the size |
| `wm_wake_redo` | `ui_task` `.events` | -8 | the push/pop of BX and CX, dead at that step |
| `wm_dw_cull` | `wm_draw_win` `.paint` | -8 | `pushf/push bx/pop bx/popf`: BX is reloaded from DI next and no flag is read |
| `wm_close_pass` | `ui_task` `.chk_close` | -6 (and -9 before that: its own compare, and AX banked for an `xor`, are gone) | `push/pop bx` |
| `sw_parity` | `sw_ink`'s dither arm | -8 | shares the solid arm's `mov [sw_pat], al / ret` |
| `wm_snap_far` | `wm_snap_w` | -6 | answers in CX, which is what the compare reads |
| `wm_about_get` | `menu_ab_cell`, `ui_dispatch` | -4, and -7 in `ui_dispatch` | the About read before the arm test, so `.oncmd`'s `mov bx, [menu_set]` is shared |
| `task_debit` | `inst_charge` | -4 | |
| `wm_unlk` | `wm_lk_tail` | -4 | |
| `ui_arm_chk`, `ui_timer_pass` | `ui_task` | -4 each | |
| `gfx_ls_ink` | `gfx_points`' pass head | -4 | |
| `wm_paint_chrome` | `wm_paint_dmg` `.chrome` | -3 | |

### 2. A compare made twice

* `kbm_ui`: ui_task tests `[mou_ptr]` before the call; the routine's own test
  and its `.out: ret` went (-9: `jmp kbm_p5spend` relaxed to short with it).
* `wm_close_pass`, `cur_shape_pass`: as above, before they were written in.

### 3. Smaller bodies

* `menu_drop`: the rect is loaded ONCE for the save, the fill and the frame
  (gfx_save, thm_bg, gfx_fill and thm_ink all preserve AX..DX; the buffer
  test and the dock's latch go through SI), and once for the restore and the
  repaint (-13).
* `menu_bt_raw`: the normal piece's pair is `thm_rpair` plus `xchg al, ah`,
  CF carried through both (-3).
* `menu_widest`: adds the 16px of frame its two callers both added, and
  clamps to `MENU_MAXW` (-5); `menu_hover`'s two answers share one
  `pop cx / ret` (-2); `menu_popup`'s two outcomes share one
  `cmp ax, 0FFFFh / cmc` (-3).
* `app_close_win`: the die flag is stored ahead of the `I_TASK` test, so the
  tasked arm's second store goes (-3).
* `app_launch`: the second `push ds / pop es` (ES = DS from the template copy,
  and `wm_create` preserves it) (-2).
* `ui_drag`: the `pushf` after `EXT_LAND` and its `popf` were dead - the `jc`
  reads CF directly and every flag after the `popf` is written before it is
  read (-2).
* `ui_tm_sysdir`: `xchg ax, bx` (BX is reloaded by the caller) (-1);
  `sw_pairbuild`: the built word from BX, which is 0 after the loop (-1,
  kern_big).

### 4. `sch_isr`'s ROM chain - one call for both arms (the ops agent's CROSS-FILE)

`fsx_rtick`'s shape (ops.md item 0): the task's SP rides in BX across the
ROM's IRQ0 (an ISR keeps every register; BX is dead at `.full` and after
`.chained`), the two arms share ONE `pushf / call far [sch_old08]`, and
`dec byte [sch_chbusy]` is `mov byte [sch_chbusy], 0`. After the call
`mov sp, bx` and the store are no-ops on the plain arm, where the flag was 0
at the test (a nested IRQ0 inside a ROM service's stack takes the plain arm
too and leaves it 0). **-10 on every kernel.** **CYCLES, IRQ0 (hot list):**
the task-stack arm - every tick that does not land inside a ROM service -
loses two memory moves of SP (~28 clocks) for two register moves (4), a
read-modify-write `dec` (~21) for a store (~16) and one taken `jmp short`
(15): **~45 clocks a tick faster**. The plain arm (inside `int 13h` on a ROM
that hops stacks) gains `mov sp, bx` and the store less one jump, ~+3.
`sch_isr` keeps its entry parity and `sch_switch` moves by -10, even.
`[sch_chsave]` is now written and read by `fsx_rtick` alone in this tree;
on kernel-size-16 ops has taken that reader too, **so at the merge the word
has no reader and can go (-2 `.bss`) - left for the coordinator**, it being
in `sched.inc` but read from `fsx.inc`.

### Hot paths

No routine on the brief's hot list changed. What the items do to the paths
near them:

* `ui_task`'s idle pass (on the list by its entry): every inlined step sits
  behind the compare that already gated its call, so the idle pass is
  unchanged where a step does not fire and one call and one `ret` (~35
  clocks) cheaper where it does - and `wm_wake_redo`, which was CALLED on
  every pass to answer no, is now one compare and no call (~35 clocks a
  pass, ~1,100 passes a second while busy). **No branch in `ui_task`
  relaxed**: the listing's near jumps are the same two before and after
  (`jz .posted_done` was already near; `jmp .loop` is near). Every insertion
  is an even number of bytes, so the code after it keeps its parity.
* The damage-repaint path (DECLINED 3): only `wm_draw_win`'s cull got
  cheaper (no call, `ret`, `pushf/popf` or BX bank, armed or not: ~60 clocks
  a window) and `wm_paint_dmg`'s chrome arm one call/`ret` cheaper. Nothing
  on it slowed. `wm_dmg_shadowed` was written in, measured, and BACKED OUT -
  see REFUSED.
* `gfx_points`, `sw_ink`: once per call or per pass, now a call/`ret`
  cheaper; no per-point or per-pixel instruction moved.

### Parity

`hotpar` is green on both gated kernels at every commit. Two of the pass's
own odd totals were answered without a pad byte, by MOVING a one-byte `.text`
variable from in front of the hot run to `ui.inc` after `ui_task`, the shape
`menu_dropd` already has on kern_big:

* kern_small's `menu_dropd` (was at `menu_drop`'s foot; now beside
  `ui_pcmd`), for kern_small's `ui_task`;
* `kbd_ovb` (was beside `kbd_ovn` in `mouse.inc`; now beside `ui_pcmd`), for
  `mou_isr .. ui_task` on both kernels.

Neither is a pad and neither costs a byte, but each pins a layout the way a
pad does: **for the coordinator's pad pairing, treat both as pads** - a later
odd change in `[kbm_ui, ui_task)` on either kernel is answered by moving
`kbd_ovb` back.

## REFUSED

* **`wm_dmg_shadowed` written at its call** (-2 measured, -8 predicted):
  inline, it pushes `wm_dmg_wins`' mark loop's `jz .mnext` and `je .mnext`
  out of short reach, and the inverted pairs nasm makes of them are a TAKEN
  jump on every visible window of every damage pass (~+12 clocks a window).
  DECLINED 3's reason, one level down. Left with a note at the routine.
* **`mou_hotplug` written into `ui_task`**: its 123 bytes would push the
  tick section's `je .idle` - taken on nearly every idle pass - out of reach
  (+3 bytes and ~+3 clocks a pass).
* **`kbm_ui` written into `ui_task`**: its tail is `jmp kbm_p5spend`, whose
  `ret` would return out of ui_task.
* **A shared `wm_lk` wrapper** for `wm_show`/`wm_hide`/`wm_front`/
  `osapi_wm_resize` (`call wm_lkrun / dw target`, rect_get's shape): the
  helper costs what the four sites save (50 against 51).
* **`ui_drag`'s second load of `[ui_origx]` etc. after `wm_dg_precover`**:
  the routine's banner says "every register preserved", but `wm_rz_precover`
  clobbers AX, CX and SI (DEFECTS FOUND).
* **`sw_fill_pat`'s three `mov di, [gfx_roff]`**: `sw_patcol` advances DI
  (a pass-14 contract change, documented there).
* **`kbm_ui`'s `and byte [mouse_btn], 0FEh` as `dec`** (-1): the test before
  it proves bit 0 set only if nothing changes the byte in between, and a
  hotplugged mouse's ISR could.
* **`inst_parksafe_set`/`inst_restart_set`'s shared head** as a helper: no
  net byte.
* **The `wm_su_*` single callers** (`wm_su_slot`, `wm_su_arm`, ...): left
  with S3, which the owner holds.

## WANTS THE OWNER

None. Every item is byte-identical behaviour.

## WANTS AN ABI BREAK

None. No cell, header, `SYSTEM.CFG` key, module interface or `.bss` layout a
module reads moved.

## CROSS-FILE

* `mouse.inc` (`cur_shape_pass`, `kbm_ui`, `kbd_ovb`), `sched.inc`
  (`task_debit`) and `softgfx.inc` are this row's; none of the other four
  agents' rows name a routine this pass edited. SPEC.md was updated where it
  named a routine that is now written at its call (§4's table, 8.1's debit,
  11.99's frame wait, 12.7's About cell, 5.6.9's ink, 5.7's dither byte,
  the `EXT_STRADS` table, `wm_paint_chrome`'s row).

## DEFECTS FOUND

* `wm_dg_precover`'s banner says *"every register and the flags preserved"*;
  it calls `wm_rz_precover`, whose own banner says AX, CX and SI are
  clobbered. Its one caller (`ui_drag`) already reloads SI (the comment
  there records that `dockmark` went red without it) and reloads AX..DX, so
  nothing is broken today; the banner is wrong. Not fixed (no byte in it).

## LABELS GATES READ

Two gates named a symbol this pass removed, and both were fixed in the
pass: `tests/timerrace.py` breaks at `ui_task.tp_locked` / `ui_task.tp_gone`
(were `ui_timer_pass.locked` / `.gone`), and `tools/deskclip.py` (the
`desk*` and `zonedmg` rows) marks `wm_draw_win.cull`, a zero-byte label at
the inlined cull (was `wm_dw_cull`). `timerrace` failed on the first pass on
exactly that `KeyError`, which is how it was found; a grep of `tests/` and
`tools/` for every removed name found the second.

## ROWS RUN

All `python3 tools/os88test.py soak -k ... --marty-jobs 1`, MartyPC on the
GLaBIOS twins, nothing edited while they ran.

1. After the inlining commits (`815b9082`, then `wm_snap_far`): **32 / 33
   ok** in 771 s - `dispdrag dragui sizesnap dispfit dispzoom zoomsave
   dispclose smalllaunch tmsmall tmload schacct uiblock stikmenu wdmenusu
   mouseup curshape toastbar wakedrain timerrace timerbounce alertanim dmgcull
   paintcull win1bpp su1bpp gfxpoints disptitle desksc deskhover dispthm
   regapp dispapp kbd4f`. The one FAIL was `timerrace`'s renamed label
   (above).
2. After the label fixes (kernel byte-identical, `md5sum` checked): `timerrace
   zonedmg deskclip deskflash deskzoom deskwhole deskpen` **7 / 7 ok**.
3. After the `sch_isr` commit (`e3df1fef`): `tickzero dskwstage stk0water
   stkpanel schacct uiblock curdisk curshape timerrace` **9 / 9 ok**.

| rows | what of this pass they reach |
|---|---|
| `dispdrag`, `dragui`, `sizesnap`, `dispfit` | `ui_drag`'s bank and dead flags, `wm_snap_w`'s right edge |
| `dispzoom`, `zoomsave`, `dispclose`, `dispapp`, `smalllaunch`, `tmsmall` | `wm_lk_tail`, `app_close_win`, `app_launch`, `wm_close_pass` |
| `stikmenu`, `wdmenusu`, `mouseup`, `desksc`, `toastbar` | `menu_drop`, `menu_popup`, `menu_widest`, `menu_hover`, `menu_bt_raw`, `menu_ab_cell`, `ui_dispatch` |
| `curshape`, `deskhover`, `kbd4f` | `cur_shape_pass` in ui_task, `kbm_ui` |
| `wakedrain`, `timerrace`, `timerbounce`, `uiblock` | `wm_wake_redo`, `ui_timer_pass`, ui_task's ladder |
| `dmgcull`, `paintcull`, `desk*`, `zonedmg`, `disptitle`, `dispthm` | `wm_draw_win`'s cull, `wm_paint_dmg`'s chrome |
| `win1bpp`, `su1bpp`, `gfxpoints` | `sw_ink`'s dither, `gfx_points`' ink |
| `tmload`, `schacct`, `regapp` | `inst_charge`'s debit |
| `alertanim` | `wm_anim` |
| `tickzero`, `dskwstage`, `stk0water`, `stkpanel`, `curdisk` | `sch_isr`'s chain (`curdisk`: IRQ0 inside `int 13h`) |

No row reaches `menu_kbnav`'s menu, `ui_tdbl` or the kern_small-only arms
directly beyond `smalllaunch`/`tmsmall`; those items were checked by
reading. No row was broken on purpose this pass.

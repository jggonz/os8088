# ksp20 agent "wmui" - the window manager, the UI task's helpers, the menu bar, the Dock, fprog, the Control Panel's resident half

Kernel size pass 20 (`docs/plans/ksp20/BRIEF.md`), section 2's `wmui` row:
`wm.inc`, `ui.inc`, `menu.inc`, `dock.inc`, `dockmod.inc`, `fprog.inc`,
`ctrl.inc`, `winmod.inc`, `extmod.inc`, `deskmod.inc`, `spinner.inc` and the
kernel's use of `apps/os88ui.inc`. Branch `ksp20-wmui`, base `9c785cf3` (the
brief, on `8ef75fa1`). Figures are ASSEMBLED, `tools/kernsize.py --json`
absolutes on each kernel's own build; resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf` + `.vga` (+ `.mega` on kern_mega). Per-routine figures
are `tools/os88rdiff.py kernel 9c785cf3 .` (and `-D KERN_SMALL`).

## BASE AND TIP

| kernel | base (`9c785cf3`) | tip | delta | where |
|---|---:|---:|---:|---|
| **kern_big** | 90,086 | **90,023** | **-63** | `.text` 39,345 -> 39,297 (-48), `.bss` 4,892 -> 4,888 (-4), `.cold` 37,933 -> 37,922 (-11) |
| **kern_small** | 55,976 | **55,912** | **-64** | `.text` 29,112 -> 29,063 (-49), `.cold` 21,153 -> 21,138 (-15) |
| kern_emu | 90,332 | **90,269** | **-63** | `.text` 39,591 -> 39,543, `.bss` -4, `.cold` -11 |
| kern_mega (`.mega` 24,688, unchanged) | 128,895 | **128,832** | **-63** | `.text` 44,929 -> 44,881, `.bss` 4,930 -> 4,926, `.cold` 48,412 -> 48,401; conventional sections 55,795 -> 55,743 (-52); footprint 61,440 of 68,096, unchanged |
| EXTD.DRV image (`.modx`) | 1,929 | 1,927 | -2 | disk bytes, not resident |

**kern_small's `accrued` line at the tip: `kernel 104/512 (20%)`** (168/512
at the base: -64). kern_big's: 423/512 (486 at the base). `KERN_SIZE`
unchanged on every kernel (95,232 / 57,856 / 95,744 / mega conventional
61,440). `.lowbss`, `.vgabuf`, `.vga`, `.boot2`, `.ovl` and every module but
EXTD.DRV are unchanged; every package image is unchanged (the `os88ui.inc`
edits are inside `%ifdef OS88UI_KERNEL`).

`make` (kern_big + kern_small + the fast tier: 44 passed, hotpar green on
both gated kernels), `make emu` and `make mega` build at the tip;
`tools/stkbalance.py` (whole kernel) reports 99 unbalanced paths at the tip
against 100 at the base - the one gone is the old `wm_paint_all`'s
`kret_dx` tail - and no new one; `tools/os88ovlchk.py` passes;
`python3 tools/checkdocs.py` passes.

## TAKEN

Bytes are kern_big / kern_small (kern_emu and kern_mega = kern_big unless
said). Every item is byte-identical behaviour.

### 1. `wm_paint_all` enters the damage pass past its bank (`wm.inc`) - -5 / -5

The fresh routine of section 0.3: since `4f94d792` it was four pushes, the
whole-desktop rect, a near call into `wm_paint_dmg` - whose `kent_bp` banked
all seven again - and `jmp kret_dx`. It now sits in front of `wm_paint_dmg`,
banks with `kent_bp` itself, loads the rect and `jmp short
wm_paint_dmg.go`, the label just past `wm_paint_dmg`'s own `call kent_bp`
(which moved ahead of the two `[cur_shchk]`/`[ui_post]` stores - they touch
no register). 19 -> 14 bytes, and a full repaint saves a call, a `ret`, four
pushes and four pops. **PERFORMANCE.md Part 5's bar holds**: nothing about
what is drawn changed - the same `wm_paint_dmg` body runs over the same rect
- and no double draw or full repaint is added anywhere. `wm_paint_dmg`
itself is byte-identical apart from the label.

### 2. The menu bar through STOSW and BP (`menu.inc`) - -40 / -36

`menu_bar` is `.lowbss`, so every `[ss:si + MB_*]` paid an override byte.

| routine | change | big / small |
|---|---|---:|
| `menu_relayout` `.cell` | after the three `movsw` (ES already the bar's), MB_XL, MB_XR and MB_SEG go in with three `stosw` - DI lands on the next record, so the `add di` goes too; the package ES is pushed first so DS can come back between | -10 / -10 |
| `menu_ab_cell` (in `menu_relayout`) | the name cell's six words as six `stosw` in field order after `push ss / pop es` (DF = 0 from the routine's own `cld`; `kret_es` restores ES) | -9 / -9 |
| `menu_track` | the cell pointer is BP: `[bp+disp]` is SS-relative with no override. BP is banked (+2) inside the five-register frame `menu_popup` shares, and popped at a new `.none` before the shared `.out` | -7 / -4 |
| `menu_title_xor` | reads XL/XR off BP; its `mov si, [menu_ent]` went | -6 / -6 |
| `menu_bent` | BP as the cell (`xchg ax, bp`): MB_XL and MB_SEG lose their override. `menu_bt_raw` banks BP for it - `kent_bp` for `kent_di`, the same 3 bytes, `jmp kret_bp` for `jmp kret_di` | -2 / -2 |
| `menu_kbnav` (`.cold`) | DI = -1 set ONCE after the direction - both arms that start from it (`.beside`, `.inx`) reach their test with DI untouched - and pass 15's parity `nop` after the routine goes with the odd -3 | -4 / -4 `.cold` |

**`[menu_ent]` is KEPT**, written once at `.found` with BP: the kernel no
longer reads it, but `tests/uilat.py` and `tests/weavelat.py` stop a memory
breakpoint on that store, `a2uilat` polls it, and SPEC.md 7.3's latency
figures were taken with it. Dropping it is -6 more (REFUSED 1).

Speed: every change removes override prefixes or a reload, or swaps an
SS-prefixed store for `stosw`: faster on each path (a relayout, a bar press,
a bar recompose, a menu keystroke). `menu_bt_raw` pays one more push/pop pair
in `kent_bp` (~25 clocks a recompose) against the two prefix fetches it buys.

### 3. `ui_dispatch`'s kernel cells as arithmetic (`ui.inc`) - -9 / -9

`.kernel` built `cmd = ui_loc_base[cell] + item` from a two-word table and
an index (`mov bl, ah / xor bh, bh / cmp bx, UI_LOC_N / jae / shl / cbw /
add ax, [table+bx]`). There are two kernel-dispatched cells, so it is `cmp
ah, 1 / ja .ret / jb .sys / add al, CMD_CLOSE - CMD_ABOUT / .sys: cbw / inc
ax` - CMD_ABOUT is the `inc`. Cell 2 (kern_big's Sticky Apps cell, which has
no items) still refuses at `ja`. An `%if` pins `CMD_ABOUT = 1` and that the
offset fits AL. 19 + 4 bytes of table -> 14. BX is no longer written there
(`ui_cmd` loads its own).

### 4. EXT_PHASE takes the retired slot 10 (`kernel.asm`, `extmod.inc`) - -4 `.bss` / 0

EXTD.DRV's slot 10 (`EXT_DESK`) retired with the layered `wm_paint_all` and
was kept as a never-called `dw ext_e_apply.r` "so the numbering never moves".
The numbering is kernel-internal (the module is stamped with the build), and
the slot block is 4 bytes of `.bss` an entry (`MODFP`), so the LAST entry,
`EXT_PHASE` (15), moved into 10 and `EXT_NENT` is 15. One `EXTCALL` site
(`ui_drag`), the same size. SPEC.md 39.19.6's table and 11.91.7's note
follow. -2 bytes of EXTD.DRV's image.

### 5. `os88ui_sbar`'s kernel arm keeps what the kernel's line keeps (`apps/os88ui.inc`) - -4 / -4 `.cold`

`os88ui_sbar` 128 -> 141 at `8ef75fa1` (section 0.3) was a correctness fix
for PACKAGES: a LINE cell clobbers AX..DX (SPEC.md 5.7.3), so the second
arrow-cell rule reloads AX and BX and `.arrow` banks CX and DX. The kernel
arm's `UI_HLINE` is `ct_cw_gfx_hline` -> `gfx_hline_n`, whose header says it
clobbers **BX and CX** only. Under `OS88UI_KERNEL` the `mov ax, [di+0]` reload
and `.arrow`'s `push dx`/`pop dx` are not assembled (141 -> 137 on both
kernels). The package arm is byte-identical, so no package changed. Only the
documented clobber list is relied on (the `mov bx, [di+4]` stays rather than
`mov bx, cx`, which would lean on `gfx_hline_n` leaving CX = x2).

### 6. The Control Panel's resident far entries (`ctrl.inc`) - -3 / -7 `.cold`

* **`cp_tick_due_x` is `cpf_cp_tick_due`**: it ends in `retf`, the
  `call / retf` thunk went, and its one near caller, `cpf_cp_tick`, reaches
  it with `COLDCALL` (`push cs` + a near call - os88ovlchk's declared far
  frame). -3 both; ui_task's once-a-second call saves a near call and `ret`.
* **kern_small: `cp_open_x` is `cpf_cp_open`** (`retf`, thunk gone, -4).
  kern_big keeps the thunk: `drv_notice` (`driver.inc`, sys's file)
  near-calls `cp_open_x` there (CROSS-FILE 2).

### 7. Small ones and the parity pairs

| item | file | big / small |
|---|---|---:|
| `wm_dock_clear` (non-DOCK_OPT, kern_small): `cmc / jc .out` for `jae .dirty` to an `stc / jmp short .out` | wm.inc | 0 / -2 |
| `wm_dmg_bands` / `wm_dmg_rebands`: `cbw` for `xor ah, ah` after `mov al, [vid_ndisp]` (at most 2) | wm.inc | -1 / 0 |
| `menu_draw_bar`: kern_small's `mov dx, MENU_LOGO_Y + 9` as kern_big's `mov dl` (DH = 0) - pass 18 kept the word as kern_small's parity byte | menu.inc | 0 / -1 |
| `wm_hit`: kern_small's `mov ah, 0` as kern_big's `cbw` ([wm_zn] <= MAX_WIN) - pass 13 kept it for the same reason | wm.inc | 0 / -1 |

### Hot paths and parity

No routine on `t_hotpar.py`'s lists changed a byte, and `hotpar` is green on
both gated kernels at both commits with **no new pad and one pad fewer**
(`menu_kbnav`'s). The `.text` run ahead of `ui_task` (wm.inc, menu.inc) is
-40 on both kernels at the tip; the three rows of item 7 are what made both
even (kern_big's `cbw` is the odd byte; kern_small's two cancel each other). `.cold` ahead of `dsk_xfer`/`dsk_runcap`/`dsk_next_clus_x`
moved by `menu_kbnav`'s -4 only; `os88ui_sbar` (fdlg.inc) and `ctrl.inc` are
after them. `wm_paint_dmg` gained a label and lost nothing; its body is
byte-identical. Nothing here is on the mount, listing, read-ahead,
FAT-window, directory-write or loader path: **no `int 13h` call changed**.

## REFUSED

1. **`[menu_ent]` dropped** (-6 more: the store and its `.bss` word): the
   latency rows' instrument (above). Taken only with `uilat`, `weavelat`,
   `a2uilat` and SPEC.md 7.3's text moved to another stop (`menu_cnt`, which
   `menu_setup` writes a title-highlight later).
2. **`wm_band`'s `xor ah, ah` as `cbw`** (-1, AL <= 3): built, correct, and
   backed out on PARITY - it is common to both kernels ahead of `ui_task`, and
   kern_big then needs a second odd byte of its own. Free to pair with any
   kern_big-only odd byte in wm.inc/menu.inc/fprog.inc.
3. **`wm_su_try`'s two four-word copies through `wm_cpy4`** (-13 each way):
   the save-under cache, ~+170 clocks a copy - S3 of LAST-DROP-BYTES 7.10,
   held by the owner for cycles.
4. **`ui_sys_open` (207 bytes of kern_small `.text`) into a module**: it
   would add a CTRL-sized `int 13h` read to every Task Manager open (BRIEF 1:
   no added `int 13h` call), and break the ROM kernel's Task Manager without
   a system disk. Not proposed.
5. **`ui_grow` sharing `wm_resize_nb`'s `wm_rz_o*` bank and shrink test**:
   unsafe - `ui_grow`'s `EXT_LAND` and `wm_ask_size` can run a package's
   handler that calls `OSAPI_WM_RESIZE`, which rebanks `wm_rz_o*` under it.
6. **`ui_cmd`'s cold tails (`.ctrl`, `.files`, `.shutdown`, `.hiber`,
   `.stik`, `.scdel`) through `api_far`**: a wash at five sites.
7. **`menu_popup`/`menu_pull`'s height as a helper**: pass 18's refusal
   stands (-1 for a call on a menu open).
8. Pass 18/19's refusals were not re-proposed (`W_DISP`'s offset, the rect
   helpers' S2/S3 sets, `wm_draw_win` billing through `ui_bill`, the
   `kent_*` prologues on the save-under and clip routines).

## WANTS THE OWNER

None. Every item is byte-identical behaviour.

## WANTS AN ABI BREAK

None taken or needed: nothing here moved `apps/os88api.inc`,
`drivers/os88drv.inc` or the C SDK (`abilock` untouched). The EXT slot
renumbering (TAKEN 4) is kernel-internal.

## CROSS-FILE

1. **`kernel/disk.inc`'s `align 2` after `dsk_img16`** (disk's): this
   branch's `.text` ahead of it comes out ODD on kern_big (-49: -40 ahead of
   `ui_task`, `ui_dispatch`'s -9 after it), so the pad costs +1 there (-48
   net). One more odd byte
   anywhere in that stretch is worth -2 at the merge, or another agent's odd
   change there cancels it for free. The coordinator sees the sum.
2. **`kernel/driver.inc`'s `drv_notice`** (sys's): `call cp_open_x` as
   `TEXTCALL`/`COLDCALL`-style far frame would let kern_big's `cpf_cp_open`
   thunk go as kern_small's did (-4 + 1 = -3 `.cold`); drv_notice is `.cold`
   or `.text` - whichever, `push cs` + near call, or `call COLD_SEG:`.
3. **`kernel/stikmenu.inc`'s `smn_track`** (files'): read for BP - it banks
   BP itself, which `menu_track`'s BP-held cell relies on across
   `stm_track_x`.

## ROWS (named; none run - BRIEF section 3)

| rows | what of this pass they reach |
|---|---|
| `dispapp`, `dispapps`, `dispcalc`, `alertbtn`, `tmload`, `desksc`, `stikmenu`, `uilat`, `weavelat`, `mouwheel`, `deskclose`, `dispclose` | `menu_track`/`menu_title_xor` (a bar press), `menu_relayout` and the name cell (every focus change), `menu_bent`/`menu_bt_raw` (every bar recompose), `ui_dispatch`'s System and Locator File cells (About, Task Manager, Close Window), `[menu_ent]`'s store (uilat, weavelat) |
| `deskbench`, `dmgcull`, `dispfsx`, `dispdrag`, `dispsize`, `dispfit`, `dispzoom`, `paintsu`, `tmrepair`, `deskwhole` | `wm_paint_all` through the damage pass (fullscreen leave, `.slow` raises, the Control Panel's display apply) |
| `extdmod`, `dualcheck`, `dispseam`, `dispstrad` | EXT_PHASE in slot 10 (a drag released on a second display), EXT_NENT 15 |
| `sbar`, `sbardlg` | `os88ui_sbar`'s kernel arm (the Disk window's and the Standard File dialog's scroll bars) |
| `cpcost`, `cpup`, `cppromise` | `cpf_cp_tick_due`/`cpf_cp_tick` (the Date/Time page's minute tick), `cpf_cp_open` |
| `dockpos`, `dockmark`, `deskdock`, `paintdocksmall` | `wm_dock_clear` (kern_small), `wm_dmg_bands` |

No row drives `menu_kbnav` (the keyboard mouse inside a dropped menu): its
change is checked by reading - DI = -1 is set before the x test and nothing
on either path to `.beside` or `.inx`'s `js .step` writes DI first.

## DEFECTS FOUND

None.

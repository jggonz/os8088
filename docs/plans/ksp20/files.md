# ksp20 agent "files" - the file manager, the desk, the chooser, Sticky Apps' thunks

Kernel size pass 20 (`docs/plans/ksp20/BRIEF.md`, row `files`): `files.inc`,
`fdlg.inc`, `filecp.inc`, `desk.inc`, `desksc.inc`, `stikmenu.inc`,
`assoc.inc`, `ascw.inc`, `dskwin.inc`. Branch `ksp20-files`, base `9c785cf3`
(the brief; its kernel is `8ef75fa1`'s). Figures are ASSEMBLED,
`tools/kernsize.py --json` absolutes on each kernel's own build, and
`tools/os88rdiff.py kernel 8ef75fa1 .` (with `-D KERN_SMALL`, `-D KERN_BIG
-D KERN_EMU`, `-D KERN_BIG -D KERN_MEGA`) for the sections. Resident =
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`.

**TAKEN outside the row**: two `RDENT` lines in `kernel/kernel.asm`'s private
rd_tab block (`wm_rect_empty`, `wm_runion` - sys's file), which item 6 needs;
nothing else. `SPEC.md` follows three internals that moved (`[fm_by2]`,
`desk_zone_whole`).

**The finding**: the largest share was **NASM's one-way relaxation**. A jump
that is out of short reach in any optimiser pass is emitted long and never
shrunk back, so a routine that loses bytes keeps a five-byte `jcc` that would
now fit in two. `fm_editkey`'s `je .swap` and `fm_draw_status`'s
`je .fs_st_swap` both fitted after this pass's other cuts and stayed long
until written `short` (-3 each, and the assembler now refuses the build if
either stops fitting); `asc_use_x`'s `ja .out` was out of reach forward and
in reach BACK to `.rfno`'s `jmp .out`. Second, **two copies of one thing**:
kern_small's two header buttons framed and hit-tested by hand twice,
`fm_draw_status`'s format and clone prompts, `fm_paste_res`'s own rect union
beside wm.inc's. Third, **state in memory that a register or the stack holds
for its whole life** (`fm_scrollpaint`'s blit band, three words of `.bss`).

## BASE AND TIP

| kernel | base `8ef75fa1` | tip | delta | where | `KERN_SIZE` |
|---|---:|---:|---:|---|---|
| **kern_big** | 90,086 | **89,956** | **-130** | `.cold` -128, `.bss` -6, `.text` +4 | 95,232, unchanged (accrued 486 -> 356) |
| **kern_small** | 55,976 | **55,793** | **-183** | `.cold` -179, `.bss` -8, `.text` +4 | **57,856 -> 57,344: the rung is DOWN** (accrued 497/512 at the new rung - 15 bytes of slack) |
| kern_emu | 90,332 | **90,202** | **-130** | as kern_big | 95,744, unchanged |
| kern_mega (with `.mega` 24,688) | 128,895 | **128,765** | **-130** | `.cold` -128 (above 1MB), `.bss` -6, `.text` +4 | conventional 61,440 of 68,096, unchanged (`.text`+`.bss` -2); `.mega` 0 |
| kern_small FILECP.DRV (`.modp`) | 1,877 | **1,876** | -1 | not resident | - |

`.lowbss`, `.vgabuf`, `.vga`, `.boot2`, `.ovl` and `.mega` are unchanged on
every kernel. `.text` +4 is the two rd_tab words (even, past every hot
`.text` routine). `.bss` -6/-8 are even, so no `.bss` word moved parity. No
other module image moved.

## TAKEN

Bytes are kern_big (= kern_emu = kern_mega `.cold`) / kern_small. Commits:
`18531d65`, `0e1d0e66`, `6a396e83`, `5e8ddba5`, `3da904f3`.

### 1. `fm_editkey` (-27 / -27) - `18531d65`

* **`.confirm` is mode 3's alone**: Format's Space arm (`.confirmf`) jumps
  straight to the Enter test, so the `cmp byte [di+FS_EDIT], 5 / je` that told
  modes 3 and 5 apart went (-6); Delete's `cmp ax, 5300h` SKIPCX2s over the
  Enter compare with ZF kept, where it was `jne .cancel / jmp short .commit`
  (-3). CX is dead on every way out (fm_edit_commit, fm_edit_end, the ladder).
* **The cancel's two arms are one** (-5 with `.lineonly`): `fm_edit_end` keeps every register
  (its ladder), so the mode is read, the edit ended once, and `sub al, 5 /
  cmp al, 2` leaves the carry that IS the answer (CF = 1 for the two-line
  modes 5 and 6). `.swapno` jumps to `.cancel` with DI and FS_EDIT = 6 intact
  (mod_need and toast_say keep every register). `.lineonly` is `CLC_OR_STC`.
* **Backspace's empty test is `dec bx / js`**, and `.store` adds two and falls
  into it rather than `jmp short .term`; the tail ends `jmp short .nochg`
  where it was a near `jmp .lineonly` (-4 between them).
* **`je short .swap`** (-3): see the finding.
* **The swap prompt's module is `MOD_FMT + [fm_swapwho]`** (-5; `%if`-pinned
  MOD_DISKOPS = MOD_FMT + 1; `[fm_swapwho]` is only ever 0 or 1) and its step
  goes 0 -> 1 by `inc` (-1).

H4, a keystroke: an accepted character is clock-for-clock what it was (a
taken `jmp short .term` became an untaken `js`, and one more `jmp short` at
the end), a refused one and Enter are faster, Backspace +~12 clocks, Escape
faster (one fm_edit_end call path, no taken jump into it).

### 2. `fm_edit_end`'s mode-6 owner, one word read (-3 / -3) - `18531d65`

`mov ax, [fm_swapwho] / dec al / jnz .fmtgone / dec ah / jnz .clrit / jmp
short .clonegone` for two byte compares: `[fm_fmtstep]` is the byte after
`[fm_swapwho]` (already `%if`-pinned) and both are 0 or 1. AX is reloaded at
`.clr` for the next block.

### 3. `fm_draw_status` (-19 / -19, the relaxed `je` included) - `18531d65`

* **The format and clone two-line prompts are one body** with the composer in
  BX (`dskw_fmt_line` or `fm_cloline`; both keep BX, as does `fm_staty2`, a
  ladder) - -9.
* **`.fs_st_space` keeps the top's BX = `[fm_vp]`** (the window's FS_2UP store
  now goes through BX; nothing writes BX before the `js`) - -4.
* **The replace prompt's keys are the string after `fm_s_repq`**, which
  `fm_scat` leaves SI on and `fm_staty2` keeps (`%if`-pinned) - -3.
* **`je short .fs_st_swap`** - -3 on kern_big too once the above made room
  (exactly 127 there).

### 4. kern_small's header buttons (- / -48) - `0e1d0e66`

`fm_draw_core` drew Refresh and the view toggle with two copies of one 63x14
frame 68px apart: one `.sbtn` (AX = the inset from `[fm_rgt]`; out AX = CX =
the left edge, BX = cy+2, DX = cy+4 for the label) - -35. `fm_onclick_x`
asked both buttons' rows twice and a miss right of Refresh fell into the
toggle's whole test: the rows are asked once, a miss right of Refresh is a
miss, and the toggle's x comes from Refresh's (AX = cw-68 still) - -13. Two
calls a full Disk-window paint (~90 clocks) - under 0.01% of it.

### 5. `fm_onkey_x`'s Delete key (-2 / -2) - `0e1d0e66`

`mov bl, FMC_DELETE / SKIPF2` into the Cut/Copy/Paste skip chain, for
`mov bx, FMC_DELETE / jmp short .cmd` (BH = 0 at `.cmd`'s `mov bh, 0`).

### 6. `fm_paste_res`'s union through wm.inc's (-32 / -32 net) - `0e1d0e66`

The paste folded each re-listed window into `fmv_ux1..uy2` with its own
four unsigned compare-and-stores (30 bytes) from its own seed (15): now
`KDOOR wm_rect_empty` and `KDOOR wm_runion` (two private rd_tab entries, +4
`.text`, published indices untouched), and the empty test is `jl` on a -1
bottom edge. H4 - a paste: two door calls a window it re-lists (~+450
clocks) against its disk I/O. **Behaviour note**: wm.inc's union is SIGNED,
as every damage rect is. For windows on the screen it is the same rect; for
one dragged partly off the LEFT edge (SPEC.md allows a negative W_X) the old
unsigned min ignored its negative x1, so the damage rect could stop short of
the screen's left columns and a window above it there was not marked - the
new rect covers it. Repaint the same or more correct, never less.

### 7. `fm_scrollpaint`'s band in registers (-28 .cold -6 .bss / -28 -6) - `6a396e83`

x1/x2 (fm_tiles') ride the stack to the blit, y2 lives in DX from its
computation to the blit (`fm_cxrgt`, the clip test and `fm_lsel_bar` keep BX
and DX), and `fm_by1` alone stays in `.bss` as `fm_row16`'s base. A clip
refusal pops the two words first (`.refp`). `.expfill`'s two `mov`s are
`xchg`. **H3, faster**: nine memory moves out, two pushes, two pops and an
xchg in - ~-120 clocks a blit-tier scroll (PREDICTED, 8088 counts).

### 8. `fm_rclick_x`'s command byte by `xlatb` (-4 / -4) - `6a396e83`

The index is AL < MC_NITEM and AX is the ladder's to restore.

### 9. kern_small's zone mask is one word (- / -8 .cold, -2 .bss) - `6a396e83`

`desk_dmgm` was a dword because kern_big has 24 zones; kern_small has
DVOL_MAX = 4, which the top of one word holds: its rotate in
`desk_dmg_zones_x` and its shift in `desk_paint_mask_x` are one word each,
`%if DESK_NZ > 16` keeps kern_big's dword (and `desk_zones_r_x`, kern_big's
33-bit ring, is not assembled on kern_small).

### 10. `desk_zone_whole` written at its one call (-4 / -) - `5e8ddba5`

kern_big's `desk_paint_mask_x` calls `desk_draw_zone` and then, under its
own `%ifdef`, the rect and the box grow. Faster: a call and ret a zone.

### 11. assoc.inc: -6 in all, EVEN (- / -) - `5e8ddba5`

`assoc_ownex` answers with `xor al, bl / add al, 0FFh` (-1), which took the
PARITY PAD after its `ret` with it (-1); `asc_use_x`'s oversized-cache
refusal goes BACK to `.rfno`, short (-3); `assoc_sweep` tests DVK_BIOS (0,
pinned in disk.inc) with `test ah, ah` (-1). dsk_xfer and its neighbours
behind assoc.inc in `.cold` move by -6: `hotpar` green.

### 12. Banks no caller reads (-2 / -2)

`stm_act_x` banked AX for ui_cmd, which clobbers AX (-2 kern_big);
`fdlg_reap_x` banked AX round its drop for ui_task's ladder, whose next rung
writes AX first (-2 kern_small). Headers say so.

### 13. Small ones

* `fcp_paste`'s list arm SKIPF2s `.empty`'s load (-1 / FILECP.DRV -1).
* `fm_measure` (kern_small): the used-KB store was skipped when the sum came
  to 0xFFFF - leaving the 0xFFFF the routine stored at its top, the same
  value - so the test went (-5), and the shift's first move is `xchg` (-1).
  **This is the byte that took kern_small's rung**: `.cold` needed to come
  under 20,976 for the COLD rung's paragraph to go.

## HOT PATHS, PARITY AND THE DISK

* **No H1/H2 routine is touched.** H3 changed: the blit-tier scroll (faster,
  item 7), a full Disk-window paint on kern_small (two calls, item 4), the
  status line (item 3, same or faster), desk_paint_mask (faster). H4: the
  editor's keys, the paste, the context menu, the buttons' clicks - each
  under 1% of its operation as itemised.
* **No `int 13h` added.** Nothing on the mount, listing, read-ahead,
  FAT-window, directory-write or loader path moved: the paste's reload makes
  the same `fmv_hload` calls, `fm_measure` reads no disk, and the swap
  prompt's `mod_need` is the same call with the same AL. `navcalls`, `fdswap`.
* **Parity**: `hotpar` green at every commit on kern_big and kern_small. The
  only bytes in front of the pinned `.cold` routines are assoc.inc's: -6.
  `.text` +4 is past every hot `.text` routine (rd_tab follows the modules).
* `tools/stkbalance.py` over every file touched, base against tip: files.inc
  2 (the same `fm_szfig_k`/`fm_ultoa_x` ladder tails), assoc.inc 3, desk.inc
  2, fdlg.inc, filecp.inc, stikmenu.inc 0 - none new.
* `make`, `make small` (through `make`'s sub-make), `make emu`, `make mega`
  at the tip; the fast tier green at every commit (44 rows).

## EQUIVALENCE

By reading, per item: for every bank removed, every caller to the next write
of the register; for every jump retargeted, the target's instruction stream
(`.rfno` IS `jmp .out`; `.confirme` IS where `.confirm` sent mode 5); for the
moved values (items 7, 3), every call between the definition and the use
against its header and body. `os88rdiff` at each commit: no relaxed row.
One boot to LOOK (kern_small, `os8088_5150_cga_gla`, small360.img): A:'s
Disk window draws both header frames and labels where they were; a click on
the toggle flips `[fm_lview]` and a second flips it back; clicks right of
Refresh and left of the toggle, in the button row, change nothing; Refresh
re-lists; PgDn/PgUp take FS_SCRL 0 -> 1 -> 0 (the blit tier).

## REFUSED

* **`fm_draw_rcore`'s icon loop on a running index** (-2, a `mul` a cell
  faster): the stack-held index needs a second exit; the bytes do not pay.
* **`fm_onkey_x`'s `.rdI/.outI` against `.redraw/.out`** (-6 if merged, +3
  for the editing block's entry jump going long): -3, and on kern_big the
  editing block sits behind ~300 bytes of key ladder.
* **`fcp_xfer`'s three relaxed jumps** (kern_big): moving `.err` into the
  middle shortens one (-3) and leaves the other two long; not taken.
* **`fdlg_grab_x`/`fdlg_top_x` sharing a tail**: no register is free to carry
  the entry.
* **`stm_track_x`, `fm_open_sel` and `fcp_scan`'s `clc/jmp/stc` exits**:
  reordered or CLC_OR_STC'd, the same byte count.
* **'Compress' as the tail of 'Uncompress'**: the case differs. **`fm_s_swapnd`
  as the tail of 'Drive A:'**: it is stamped at run time (disk.inc).
* **`fmv_bcast` holding `[fm_vp]` in BX** (-4): needs `dsk_dest_b0`'s BX
  contract proved; not read to the end.

## WANTS THE OWNER

None. Every item is behaviour-identical; item 6's signed union is noted
there (a window partly off the left edge is repainted the same or better).

## WANTS AN ABI BREAK

None found: everything changed is kernel-internal.

## CROSS-FILE

* **`ui_task`'s `jmp .yield` (ui.inc, wmui), kern_big**: in short reach
  (127) and emitted near, NASM's one-way relaxation - `jmp short .yield` is
  -1. `ui_task` is a `hotpar` routine (kern_big parity 0), so it wants a
  paired byte or a pad.
* **The relaxation scan, for the coordinator after each merge**: a relaxed
  `jcc` (`7x 03 E9 rel16`) or near `jmp` (`E9`) whose target would be in
  short reach once it shrinks is a free 3 or 1 bytes, and only `jmp short` /
  `jcc short` in the source collects it. At this tip the only one left in
  either kernel is ui_task's above (the `jmp strict near` cells excepted).
* **`desk_dmg_zones_x` on kern_small banks SI round `cw_wm_zone_r`** for
  wm.inc's two callers (`wm_paint_dmg`): if SI is dead after both, the
  contract can say so and kern_small loses 2.
* `kernel/kernel.asm` (sys): the two rd_tab lines of item 6.

## ROWS NAMED (none run - the coordinator's union)

The editor, items 1-3: `fmcommit` (typed, refused and Backspace keys, the
status-only repaint), `fmtpair` and `fmtreach` (Format's prompt and Space),
`diskclone` and `czto` (the clone's prompts, `fm_edit_end`'s owners),
`fcpcopy`, `fcpsmall`, `fcproom` (the replace question), `bigvol` (the Size
and Free line). Items 4, 7, 8: `fdlgsmall`, `fdlgdrop`, `fdlgchsmall`,
`smalllaunch` (kern_small's Disk window), `fmarrows`, `fmthumb`, `sbar`,
`sbardlg`, `msel` (fm_lsel_bar round the blit), `glyphcost`, `icoclip`,
`desksc` (the context menu). Item 5: `fmcommit`, `fcpapi`. Item 6:
`fcpcopy`, `fcpapi`, `clipkeep`, `dragui`, `fcpsmall`. Items 9, 10: `zonedmg`,
`deskwhole`, `deskdrag`, `deskfdd`, `deskclip`, `deskclipsmall`, `desksmall`.
Item 11: `assocopen`, `assocfind`, `assocsweep`, `assocvol`, `assocglyph`,
`ascabsorb`, `dosglyph`, `instassoc`. Item 12: `stikmenu`, `modcache`,
`fdlgdrop`. Item 13: `fcpcopy`, `bigvol`, `fdlgsmall`. The disk bar:
`navcalls`, `fdswap`.

## DEFECTS FOUND

None in the code. A method note: `os88marty`'s debug server takes one client,
so a look script must take its screenshots through the instance it holds
(`m.vram()` and `os88marty.write_png`), not the `shot` CLI.

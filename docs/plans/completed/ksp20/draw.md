# ksp20 agent "draw" - the drawing, cursor, mouse, video and fsx files

Kernel size pass 20 (`docs/plans/completed/ksp20/BRIEF.md`), section 2's `draw` row:
`vga12.inc`, `softgfx.inc`, `font.inc`, `mouse.inc`, `mouproto.inc`,
`vmmouse.inc`, `viddet.inc`, `vidsel.inc`, `vidext.inc`, `blank.inc`,
`fsx.inc`, `band.inc`, `icons.inc`, `splash.inc`. Branch `ksp20-draw`, base
`9c785cf3` (the brief). Figures are ASSEMBLED bytes, `tools/kernsize.py
--json` absolutes; resident = `.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf` + `.vga` (+ `.mega` on kern_mega). Per-routine figures are
`tools/os88rdiff.py kernel` spans.

TAKEN (in this row's files only): nothing outside the row was edited.

## BASE AND TIP

| kernel | base `9c785cf3` | tip | delta |
|---|---:|---:|---:|
| **kern_big** resident | 90,086 | **90,029** | **-57** |
| kern_big `.text` / `.vga` | 39,345 / 2,222 | 39,292 / 2,218 | -53 / -4 |
| kern_big `KERN_SIZE` / accrued | 95,232 / 486 of 512 | 95,232 / **429** of 512 | no rung / -57 |
| **kern_small** resident | 55,976 | **55,902** | **-74** (`.text` 29,112 -> 29,038) |
| kern_small `KERN_SIZE` / accrued | 57,856 / **168** of 512 | 57,856 / **94** of 512 | no rung / -74 |
| **kern_emu** resident | 90,332 | **90,275** | **-57** (`.text` -53, `.vga` -4) |
| **kern_mega** resident (with `.mega`) | 128,895 | **128,838** | **-57** (`.text` 44,929 -> 44,872; `.mega` 24,688 unchanged) |
| kern_mega conventional | 61,440 of 68,096 | 61,440 | no step |

`.bss`, `.cold`, `.lowbss`, `.vgabuf`, `.boot2` and every module image are
unchanged on all four. `make`, `make small`, `make emu` and `make mega` build
at the tip; the fast tier passes at every commit (44/44, `hotpar` green on
kern_big and kern_small with no new pad, `stkbalance`, `cswrite`, `megaseg`
among it). `tools/stkbalance.py` over the seven edited files, base against
tip: no new unbalanced path (one fewer - gfx_sub_arm's bank is kent_bp's now).

## TAKEN

| # | item | kern_big | kern_small | kern_emu | kern_mega | hotness, cycles |
|---:|---|---:|---:|---:|---:|---|
| 1 | **gfx_sub_arm: `call kent_bp` for the seven pushes, and the rows first** - the rows need no shift, so BX/DX are spent on them before CL is the count; the skipped-rows term is (S.y2 - y2) + (y1 - S.y1), so neither `inc` nor the pushed y1 is needed; `xchg` for the two `mul` operands | -17 | -17 | -17 | -17 | H3: once a sub-rect restore (wm_su_*'s named rect). kent_bp's ~95 clocks against a restore of a fragment, well under 1% |
| 2 | **gfx_scroll: rows-to-copy formed ONCE in DI**, the refusal on the `inc`'s own flags, stored before the clip test (scratch under the lock that nothing reads on a refusal) - it was a compare against the height and the same sum again below | -6 | -6 | -6 | -6 | H3, faster (one sum where there were two and a push/pop) |
| 3 | **gfx_ls_box, kern_small**: gfx_ls_d1/d3 are 0 on that build - the unarmed box's two zeros from a register, and the two max-with-0 clamps as `or / jns / xor`; `gfx_ls_zero` (the word they read) goes | 0 | -12 | 0 | 0 | H2 (gfx_points' box, once a call and once a miss): faster on every path - a register zero for a load, 6-byte sign test for a 9-byte compare-and-load |
| 4 | **blk_set, kern_small**: one card, so it is vid_bk's own tail entered with vid_blank_kind's frame (DX banked, DH = 0/8 built from AL by `cmp/sbb/and`). AX is spent on kern_small; both callers (blk_wake, blk_pass) bank it, and the header says so | 0 | -4 | 0 | 0 | per blank / unblank |
| 5 | **cur_saveu / cur_restoreu (KHP_VGASAVE): ES spent, not banked.** Every caller's contract already clobbers ES (cur_put_1, cur_move_1 reload it in cur_draw; cur_vis banks it; cur_move_mono, the same doors' 1bpp arm, spends it) | -4 (`.vga`) | 0 | -4 | -4 | **H2, every planar mouse packet: a push and a pop fewer (~27 clocks) on both walks** |
| 6 | **font_char_bb: `xchg ax, di`** for the row base (AX is dead after) and **the clear loop falls through** where ink 0 took a `jmp short` | -3 | -3 (and -1: the font_ink PARITY nop, below) | -3 | -3 | H2 per 1bpp glyph cell: -1 clock (xchg) and ~15 clocks a black-ink glyph; no path slower |
| 7 | **sw_patcol: KHP_MONOPAT's one-store masked byte on every kernel** (`xor al,[es:di] / and al,cl / xor [es:di],al`) | -4 | -4 | -4 | -4 | H1 per row of a pattern fill's edge column. 12 bytes for 16; on the 8088's byte bus 15 bus cycles a row (12 fetched + 3 data) for 18 (16 + 2) - ~12 clocks a row faster, and khp1's mono-4 measured the whole knob faster. The knob-on body is unchanged |
| 8 | **cur_lazyck, kern_small**: the region-misses-the-arrow exit is `jmp kret_si` (the same five pops and `ret`) | 0 | -2 | 0 | 0 | H3: ~15 clocks on a wm_clip_set whose region misses the arrow, once a window paint, never per rect - far under 1% |
| 9 | **icon_draw_x, kern_small**: the stage copy's count (2 + 4h) as `mov al, 80h / mov cl, 6 / shr ax, cl / xchg ax, cx` off the header word (AH = h, AL = the kind, 1, replaced) | 0 | -3 | 0 | 0 | H3, once a record: ~30 clocks dearer against an icon's draw |
| 10 | **kbd_bit: `and bx, 7Fh`** (sign-extended imm8, as short as the byte form) clears BH, so `mov bh, 0` goes | -2 | -2 | -2 | -2 | H2 per scancode: 4 clocks faster |
| 11 | **gfx_lock: `mov [cur_lazy], al`** (AL is the 1 just stored) inside the cli window, for the 5-byte immediate after `sti` | -2 | -2 | -2 | -2 | H2 per lock: 4 clocks faster; the promise now lands in the same critical section as the flag |
| 12 | **fsx_mode, kern_small**: the caps mask is fsx_caps' own answer (BX = 0 asks; DL is kent_di's to restore), called where its four lines were repeated | 0 | -4 | 0 | 0 | once a mode set: a call dearer |
| 13 | **osapi_mouse's re-hook arm (kern_big)**: DX holds our ISR and DI the vector once the row index is spent - DX and CX are this call's outputs, loaded at `.live` - so SI's push and pop go | -2 | 0 | -2 | -2 | inside an fsx bracket only: ~10 clocks a poll faster |
| 14 | **cur_move_multi (extended desktop)**: the arrow's display in DL, so `xchg ax, dx` hands it to vid_ctx_act (which keeps AX and DX) where `push ax / mov al, dh / pop ax` was | -2 | 0 | -2 | -2 | H2 on a two-display machine: a push/pop fewer a packet |
| 15 | **gfx_disp_run (extended desktop)**: `xchg ax, bp` round vid_ctx_act (keeps AX and BP) for `push ax / mov ax, bp / pop ax` | -2 | 0 | -2 | -2 | H2 per display per primitive on two displays: faster |
| 16 | **ico_core: CX/DX are not reloaded after the clip test** (nothing from `.noclip` on reads either before writing it; the packed driver's VX_ICON reads the `ico_*` cells, not registers); **`.top_ok` reuses AX = [ico_y]** (both ways in hold it); **the bottom overhang is `sub ax, dx / sub bx, ax`** (h - (y + h - ch)) | -13 | -13 | -13 | -13 | H3 per icon: faster on every path |
| | **total** | **-57** | **-74** | **-57** | **-57** | |

kern_small's font_ink `%ifdef KERN_SMALL` PARITY nop went with item 6's odd
-3: font_run, font_run_x and font_run_cell are back at their measured parity
with it gone (it is counted in item 6's kern_small -4 and the total). kern_big
needed no pad and lost none.

### Hot paths

* **No khp1 pick was reverted or made slower.** The KHP_VGA* bodies in `.vga`
  were read for setup, slow-path and tail savings at their speed: item 5 is
  the one found (the planar cursor walks), and it is faster. vga_save_vram_v,
  vga_restore_vram_v, gfx_fill_gray_raw_v, vga_solid_rect_v and gfx_blitp_v
  were read again after `f1f35307`..`866ccc31`: their plane loops hold every
  register (BX banked for the plane bit precisely because none is free), and
  hoisting the per-plane DS/ES banks or the step out of the plane loop costs
  more `cs:` overrides than it saves (+5 on the restore) - REFUSED below.
* Every H2 item above is faster or equal on the path it sits on, counted in
  the table in the 8088's `max(clocks, 4 x bytes)` terms.
* `hotpar` green at every commit on both gated kernels, no new pad. One
  candidate (mou_p2_off's `mov byte [mou_p2], 0` as `dec byte [mou_p2]`, -1:
  the flag is 0 or 1 by construction) was built and **withdrawn**: its odd
  byte moved the mouse ISRs, kbm_isr, sch_isr and sch_switch to the wrong
  parity on kern_big (disk.inc's self-parity pad after dsk_img16 took the
  byte back further down), and a pad to fix it costs the byte it saved.
* No change touches a disk path: no `int 13h` call added anywhere.

### Equivalence

Scratch unicorn harnesses against the BASE and TIP kernels' own bytes
(kernel-full.bin + kernel.map, `.text`/`.vga` at KERNEL_SEG), every register,
the flags' CF, the touched cells and the framebuffer compared:

| test | kernels | cases | result |
|---|---|---:|---|
| `subarm` - gfx_sub_arm, random saved and sub rects, refusals included | big, small | 1,500 each | identical; a planted `inc di` -> `nop` in the tip goes red (151/300) |
| `scroll` - gfx_scroll end to end on CGA and Hercules geometry (vgas_bankcopy), clip region armed in 10% | big, small | 1,500 each (~57% accepted) | identical framebuffers, cells and flags |
| `lsbox` - gfx_ls_box with 0..5 clip rects | big, small | 3,000 each | identical box words and registers (BX is the scan pointer, spent by contract and reloaded by GFXPT_RTAB, so not compared) |
| `blkset` - blk_set, both directions, CGA and Hercules | small | 200 | identical ports, `[blk_on]`, every register but AL (spent by the new contract) |
| `icondrawx` - icon_draw_x -> icon_draw -> ico_core -> the 1bpp passes, kinds 0..2, h 0..17, y off the top and bottom, clip region in 40% | big, small | 3,000 each | identical framebuffers, registers, CF, the pen |
| `kbdbit` - every AL | big, small | 1,500 each | identical |
| `fsxmode` - fsx_mode, every id 0..11, CGA and Hercules, wrong task | small | 800 | identical ports, BIOS calls, info buffer, `[fsx_cur]` |
| `osmouse` - osapi_mouse with and without a bracket, a stolen vector or not, every port row | big | 1,000 | identical (vectors compared by which of mou_isrs they name) |
| `disprun` - gfx_disp_run over two displays | big | 1,500 | identical body entries (registers, ctx pointer relative to vid_ctx, the active display) |
| khp1's `mono_uni.py --only=pat,save,restore` (item 7; sw_xfer reading the sub-rect cells item 1 writes) | big, small | 1,200 each | 0 failed |

Items 5 and 14 were proved by reading: item 5 removes a bank of a register
every caller's contract already lists as clobbered (cur_put_1, cur_get_1,
cur_move_1 say `es`; the three callers are cur_vis, which banks ES, and
cur_move_multi and the ISR's cur_move, whose 1bpp arm has always spent it),
and item 14 swaps through DX, which vid_ctx_act (and the vid_ctx_ptr and
vid_depth_set it calls) never writes.

## REFUSED

* **vid_setmode's eight pops as `jmp kret_es`** (-6): the splash calls
  vid_setmode while the image is still arriving and the kret ladder sits at
  the END of `.text` (kernel.asm's own warning beside kent_bp).
* **gfx_xor_strips' six pushes as `call kent_di`** (-3), **sw_fill_pat's
  eight as kent_bp + `push es`** (-4): both are per primitive (H2), and
  kent_*'s ~95 clocks is a cost on a path that must be cycle-neutral.
* **cur_lazyrect's clear exit as `jmp kret_si`** (-2, kern_big): per band
  blit (H2), the jump is a cost there; kern_small's cur_lazyck (item 8) is
  per clip arm (H3) and takes it.
* **The VGA restore's per-plane DS/ES banks hoisted out of the plane loop**:
  the kernel words read inside the loop would need five `cs:` overrides, +5.
* **icon_draw_x's count by `aad 4`** (-6 against the original): AAD's
  immediate is ignored by the NEC V20/V30, a common XT upgrade.
* **gfx_disp_enter / gfx_disp_enter_n sharing their nest test** (-1 at best
  in the form that keeps the nested path free of vid_disp_of): a nested
  enter would pay a display walk it does not pay now.
* **mou_p2_off's `dec byte [mou_p2]`** (-1): parity, see *Hot paths*.
* Pass 18's refusals (sw_* `[sw_mode]` through AL, cursor_hide/show's CF,
  cur_move_multi's `push word`, gfx_disp_run's activation order,
  gfx_fill_gray_raw_v's AA/55, vid_tab compression) still hold and were not
  re-proposed. (Pass 18's WANTS THE OWNER 1, kern_big's vid_tab move, was
  taken by its sweep3; the blob has 181 bytes spare at this base.)

## WANTS THE OWNER

None. No item changes a picture, a result, a refusal, a limit or a message.

## WANTS AN ABI BREAK

None found. No package-visible contract moved (blk_set, fsx_mode's internals,
the cursor walks and gfx_sub_arm are kernel-private; OSAPI_FSX_MODE,
OSAPI_MOUSE and OSAPI_ICON_DRAW answer exactly as before).

## CROSS-FILE

* `kernel/disk.inc` (disk's): the auto-parity pad after dsk_img16 absorbs an
  odd shift in front of it and so moves the hot mouse ISRs instead - worth
  knowing for anyone taking an odd byte between mou_p2_off and dsk_img16 on
  kern_big.
* `kernel/kernel.asm` (sys's): nothing edited; kret_si / kent_bp are reused.

## ROWS (named, not run - the coordinator's union)

| rows | what of this pass they reach |
|---|---|
| `paintsu`, `wdmenusu`, `atmenusu`, `su1bpp`, `tmselfsu`, `vesasu` | gfx_sub_arm's named-rect restores (1) |
| `npscroll`, `drvscroll` | gfx_scroll (2) on the 1bpp and planar arms |
| `gfxptsmall`, `small128`, `gfxpoints`, `ptsext` | gfx_ls_box (3, kern_small) and gfx_points around it |
| `saver`, `savervga`, `dispsaver`, `small128` | blk_set (4) - kern_small's blank arm boots in small128 |
| `curshape`, `curbusy`, `mouseup`, `mousess`, `savervga`, `vgadirty`, `pxdrawvga` | the planar cursor walks (5) |
| `deskflash`, `win1bpp`, `glyphcost`, `fonts` | font_char_bb (6) |
| `fillpat` | sw_patcol (7) |
| `icoclip`, `icohd`, `icoshed`, `icostore`, `deskflash` | icon_draw_x and ico_core (9, 16) |
| `kbd4f`, `kbdburst`, `kbdpreempt`, `dosaltenter` | kbd_bit (10) |
| `mouresume`, `mouerrq`, `serlease` | gfx_lock's promise (11) and the pointer under a hold |
| `dispfsx`, `dispfsxcga`, `dispfsxherc`, `dispmodex`, `fsxdisp`, `fsxclip`, `wireballmono` | fsx_mode (12) and osapi_mouse's re-hook (13) |
| `dispstrad`, `dispnp`, `dispdepth`, `isotrontwodisp` | cur_move_multi (14) and gfx_disp_run (15) |
| `ps2mouse`, `p2restore` | the PS/2 side of osapi_mouse (unchanged, its fall-through) |

No row was run by this agent (BRIEF section 3). One boot to look was not
taken: every drawing change is a reorder or register choice proved
identical on the framebuffer by the harnesses above, on both 1bpp adapters'
geometry (CGA, Hercules) for the 1bpp paths.

## DEFECTS FOUND

1. **`tests/unit/t_hotpar.py` cannot read a fresh `make build/kernel.bin`**:
   os88cfx stamps kernel-full.bin after the map is written, so the map is
   >1 s older than the binary and is refused, and the os88sym fallback then
   refuses the image ("first difference at 0x50"; cause not chased). The full `make` runs
   it green; for a kernel-only rebuild this agent read the two maps directly
   through t_hotpar's own `check()` and table. Not fixed (a gate outside this
   row).
2. `blk_set`'s header said "clobbers: flags"; on kern_small it now spends AX
   too, and the header says so (both callers bank AX).

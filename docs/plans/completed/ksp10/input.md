# Kernel size pass 10 - agent "input" (mouse, mouproto, menu, events, icons, clip, toast)

Base `833f13e4`. Bytes are `kernsize` section deltas against the blessed base.

## TAKEN

### Batch 1 - mouse.inc, mouproto.inc, menu.inc (events.inc: a guard only)

kern_big `.text -136 .cold -11` (sum -147); kern_small `.text -131 .cold -11` (sum -142).

mouse.inc / mouproto.inc (.text -68 big):
* `mou_pout`: `add dl, ah` for the register offset - both UART bases are
  8-aligned and AH is 0..7, so the byte add cannot carry; the CX bank goes. -6
* `mou_byte` (ISR): the phase dispatch counts AH down (`dec ah` x3 for `cmp
  ah, n`), the phase-3 store writes AH = 0, and the stray `.bout: ret` joins
  `.jbout`. -5, faster
* `MOU_DECODE_MS` (shared with kern_dos): dy first so dx is built straight in
  AX (no SI bank), the redundant `and al, 3` before `shl al, 6` dropped, and
  DL masked in place. -8 per copy, 4 instructions fewer; SI no longer clobbered
* `mou_p2_byte` .b2: same reordering, `xchg ax, bx`. -5
* `mou_apply`: left event as `and ax, 1` (kbm_btn's shape, guarded in
  events.inc), right press as `and bh, bl / test bh, 2`. -5
* `cur_geom`: `[cur_b1ok]` by `cmp/sbb/inc/mov`, branch-free. -5
* `cur_shape_set`: `cbw / add ax, ax / xchg ax, bx`. -2
* `cur_move_mono` pass 1: the row-covered answer branched on through `lodsw`
  (which writes no flag) instead of carried in DL; `[cur_rows]` widened once
  outside the loop. -8, 5 instructions a row fewer
* `cur_mvcols`: the two column loops are one (j = 1, 0), each answer computed
  into AL and stored once. -24

menu.inc (.text -68, .cold -11):
* `menu_relayout`: dead DI bank round `wm_strseg`. -2
* `menu_ab_cell`: item count = Close's index + 1 (guarded `MENU_ABMAX == 2`),
  `inc bx`, zero from AX. -6
* `menu_ab_cat`, `menu_bstr`: `es lodsb`. -4
* `menu_furniture`: the cell wipe is `menu_inval` over 0..7FFFh. -13
* `menu_bar_text`: `[menu_bfirst]`/`[menu_blast]` seeded from DI = 0. -2
* `menu_bemit`: a dead `mov cx, ax`; `+1` in the displacement for two `inc si`. -4
* `menu_draw_clock`: three `jcxz` round REPs dropped (REP with CX = 0 is a
  no-op), `cbw/xchg`, the lead's x2 off CX. -10
* `menu_drop`: entry zero from AX; `.sel` takes-and-zeroes `[menu_sseg]` with
  one `xchg`. -5
* `menu_trunc`: `repne scasb` for the fit test, one `rep movsb` with the
  segments swapped for the copy. -15
* `menu_logo_glyph`: the two blank rows are not stored, a row ends at its
  last lit pixel. -6
* `menu_kbnav` (.cold): the "inside the menu?" x test asked once, DX carrying
  the placement x. -11

### Batch 2 - icons.inc, toast.inc, mouse.inc's keyboard mouse

kern_big `.text -53` (running sum -200); kern_small `.text -53` (running -195).

icons.inc (-34):
* `ico_core`: `[ico_rb]` computed before the clip test, which wants it x 8 -
  `xchg ax, cx` and three shifts where it was a load and four; and a
  redundant `mov ax, [ico_rb]` reload. -7
* `ico_core_bb`: `ico_bbop_of` + store + call became `ico_bbpass`, one entry
  that computes [ico_bbop] (`cmp/sbb`, 0 / 0FFh) and falls into
  `ico_pass_bb`. -15
* `ico_pass_bb`: `mov bp / or bp, bp` for `cmp word / je / mov bp`. -3
* `icon_draw16`: joins `icon_draw` at a new `.hdr` with its fixed header in
  AX instead of two immediate word stores. -9
* `icon_draw_ix`: `cbw`. -1

toast.inc (-5): `toast_pass` reads on/want as one word, and takes-and-clears
`[toast_dirty]` with one `xchg` (which also closes a lost-set window).

mouse.inc keyboard mouse (-14):
* `kbm_shf` (AL = KB_FLAG) replaces `kbm_slock` and the ISR's own ES bank:
  three readers of 0040:0017, one body; `kbm_ui` takes bit 4 as its level.
  SPEC.md's three `kbm_slock` mentions renamed. -11
* `kbm_poll0` / `kbm_ui`: tail jumps into kbd_ovspend / kbm_p5spend. -3

### Batch 3 - menu.inc again, mouse_unhook, evq_drain

kern_big `.text -65` (running sum -265); kern_small `.text -70` (running -265).

* `menu_setup`: menu_track's and menu_popup's shared head (count clamp to
  `[vid_popmax]`, `[menu_cnt]`, `[menu_iptr]`). -10
* `menu_save_kb` written into `menu_drop`, its one caller (no call/ret, no
  BX/CX/DX bank - all reloaded before use), planes as `mul word [mem]`. -12
* `menu_drop` .poll: `xchg ax, [menu_sel]` and `inc/jz/dec` for 0FFFFh. -3
* `menu_relayout` .cell: the AMENU entry's three words copied with three
  `movsw` and the segments swapped (layout asserted); SI steps by the copy. -17
* `menu_bent`: `mov al, 12 / mul bl / add ax, menu_bar / xchg ax, si`. -2
* `menu_bpadc`: the clamp of the target to `[menu_bn]` was subsumed by the
  per-pass re-read below it. -10
* `menu_draw_bar`: `[menu_bdirty]` take-and-clear by `xchg`. -2
* `menu_bput`: `[menu_bcell+di]` directly - no BX copy or bank. -4
* `menu_bar_text`: `lodsb` in the strip loop; the highlight-run scan as
  `xor ah, al / jns`. -5
* `mouse_unhook`: `or al, [bx+mou_masks]`. -2
* `evq_drain` is `evq_mup` until the ring is empty (an MUP record is a no-op
  to `wm_wake_eaten`). -5

### Batch 4 - a shared buffer, two single stores

kern_big `.text -11 .bss -25` (running sum -301); kern_small `.text -6
.bss -25` (running -296).

* `menu_tbuf equ menu_clkbuf`: menu_trunc's staging buffer is carved out of
  the clock's field. Both are transient - the clock recomposes its whole
  field every call (`[menu_ckck]` is what persists) and the truncated item
  lives four instructions, on task 0 with the gfx lock held. .bss -25.
* `kbm_paint`: `[cur_dirty]` answered in AL and stored once. -5
* `menu_relayout`: `[menu_nbar]` = 1 from the CX the rep left at 0. -1

### Batch 5 - clip_get

* `clip_get`: BX and DX were banked and nothing in it writes either. -4

### At the tip

kern_big `text 44,039 -269  bss 5,108 -25  cold 37,910 -11  (sum -305)`;
kern_small `text 32,640 -264  bss 3,079 -25  cold 23,939 -11  (sum -300)`;
kern_emu `(sum -305)`. kern_big's image rung uncrosses (97 -> 96 steps).

Per file, kern_big (code / bss): mouse.inc + mouproto.inc -89, menu.inc
-143 / -25, icons.inc -34, toast.inc -5, clip.inc -4, events.inc -5.

## REFUSED

* `cur_lazyrect` / `cur_lazyck`: the hit arm could `call cur_unlazy` before the
  shared pops instead of popping and jumping (-4 each arm, ~-12 over three
  arms) - REFUSED: it nests cursor_hide's whole frame 12 bytes deeper on
  whatever task is drawing, and the drawing chain is the deepest a slice
  carries (STACK-SLOTS-PLAN).
* `cur_move_mono`'s save-buffer flip as `xor bx, cur_save ^ cur_savex` (-5):
  NASM refuses XOR of two relocatable labels in `-f bin`; the sum form too.
* `evq_pop`'s `jmp .done` as a `mov ax, imm16` skip-byte over `popf/stc`
  (-1): no skip-byte idiom exists in this kernel and AX would be live-looking
  garbage; not worth the precedent for one byte.
* `toast_now`'s colour bank as `push word [gfx_color]`: same bytes, and it
  would restore the neighbouring byte too.
* `clip_put`'s `.toobig` ladder: every rearrangement is the same 5 bytes.
* `MOU_DECODE_MS`'s button decode (18 bytes): every rcl/sbb/swap spelling
  found was 17-18 bytes and slower.
* `ico_pass_bb`'s three `or al, al / jz / cmp dl, dh / jae` guards folded
  into `ico_bbop_byte` (-16): REFUSED, it would pay a call+ret for every
  EMPTY byte of every 1bpp icon - a renderer inner loop.
* icons.inc's 25 bytes of per-draw state (`ico_ww`..`ico_bbop`) into the
  primitive union `gfx_u` (~-20 net): not taken - `gfx_u` is 22 bytes (it
  would have to grow, vga12.inc's), and `fnt_unlazy` -> cursor_hide and the
  desk band path both reach other `gfx_u` users from inside an icon draw.
* osapi_mouse's ivec index as shifts of `[mou_line]` (-1): obscure for a byte.
* `evq_push`/`evq_pop`'s three `add al, 8 / and al, 7Fh / mov [..], al`
  advances as a helper (-7): REFUSED, `evq_push` is on the mouse ISR's path
  on the 128-byte private stack and this would make that path 2 bytes
  deeper; on the 1bpp adapters the event path is near the ISR's deepest.
* `cur_saveu`/`cur_restoreu` as one body with the VGA routine in BP (-10):
  REFUSED, it is the VGA cursor's save/restore, run on every pointer move,
  and a `mov bp` plus a `jmp` is a slower hot path for 10 bytes.
* `mou_apply`'s `[cur_dirty]` stores as one (-1): the shape that avoids a
  jump on the draw path is one byte, and the other spelling adds a jump to
  the ISR.
* `menu_track`'s `[menu_cell]` counted from DI at the find (-1): no.
* `menu_furniture`'s `[menu_bovr]` clear moved to the end to share AX = 0
  (-2): a `menu_force` from a pre-empting task during the draw would be lost.

## CROSS-FILE

* `apps/telnet/telnet.asm:1874` - a comment names `kbm_slock`, which is
  `kbm_shf` now (comment only, no bytes).
* `kernel/viddet.inc:554,562` - comments (and an %error string) name
  `menu_save_kb`'s header, which is now a block comment in `menu_drop`;
  wording only.
* `docs/INDEX.md` is regenerated in this branch because this notes file is
  a new `docs/plans/` entry; every agent's branch will carry the same one-line
  change - take any of them, or regenerate after deleting `docs/plans/ksp10/`.

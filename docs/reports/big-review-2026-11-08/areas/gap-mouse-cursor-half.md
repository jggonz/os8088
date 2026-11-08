# kernel/mouse.inc's cursor half (cur_*): save-under, lazy cursor, shapes, busy cursor, multi-display move

Gap review, 2026-11-08. It fills what `input.md`, `gfx.md` and `video.md`
left unread: the cursor renderer between the mouse ISR and the drawing layer.
Byte figures come from `build/kernel.map` (kern_big at this tree) unless they
say ESTIMATED or PREDICTED. Cycle figures are the tree's own MartyPC
measurements, quoted with their source. Nothing was assembled or booted.

## Scope read

| file | lines | what |
|---|---|---|
| `kernel/mouse.inc` | 4,170-4,360 | `mou_apply`'s `.move` gate (the ISR's caller side) |
| `kernel/mouse.inc` | 4,360-6,160 in full | `cur_logput` through the shape tables and their asserts |
| `kernel/mouse.inc` | 6,160-6,290, 6,560-6,699 | cursor state, `.bss`/`.lowbss`, the `gfx_u` union members, `gfx_u_mv` |
| `kernel/mouse.inc` | 6,284-6,360 | `mou_dbg_blk` / `mou_dbg_cur` / `mou_dbg_p2` |
| `kernel/mouse.inc` | 1-30, 60-90, 245-265, 1,550-1,610, 2,560-2,760, 3,420-3,470 | header, `CURBAR_*`, cell constants, `osapi_mouse`, `kbm_move`'s `.paint`, `mou_clamp`, `kbm_poll` |
| `kernel/kernel.asm` | 236-300, 6,920-6,945, 7,838-7,862 | the `KHP_*` arms, `CURUNLAZY`, `cw_cur_unlazy` / `cw_cur_lazyrect` / `cw_cursor_hide` / `cw_cursor_show` |
| `kernel/vga12.inc` | 360-410, 490-580, 4,980-5,270, 5,975-6,010 | `gfx_aud`, `GFXCLIP`/`GFXCLIP_ARM`, `gfx_lock` / `gfx_unlock`, `gfx_blit1_x`'s `cur_lazyrect` bracket |
| `kernel/wm.inc` | 6,118-6,150, 6,228-6,262 | `wm_clip_set`'s `cur_lazyck`, `wm_dmg_gray`'s `cur_lazyrect` |
| `kernel/font.inc` | 1,130-1,200 | `fnt_unlazy`, `font_run_x` |
| `kernel/vidsel.inc` | 995-1,050, 1,558-1,770 | `vid_switch`'s order, `vid_ctx_act`, `vid_disp_of` / `vid_disp_find` |
| `kernel/vidext.inc` | 71-112, 339-349 | `VIDEXTM` on both kernels |
| `kernel/fsx.inc`, `kernel/hiber.inc`, `kernel/fprog.inc`, `kernel/ui.inc` | spot reads | `fsx_run`'s `cur_unlazy`, resume's mode set and repaint, `fpg_arm`'s hide, `ui_volume_service` |
| `SPEC.md` | 7 (10,650-10,700), 7.1-7.1.5 (10,708-11,435), 7.5.0 (12,068), 9 (13,632-13,735), 27,655-27,670 | the contracts this code claims |
| `PERFORMANCE.md` | Part 5 rows 1,353-1,357 | pointer-motion and lock-pair rows |
| `docs/plans/LAST-DROP-PERF.md` | 7.1 | the `KHP_*` ranking (row 3 `KHP_VGASAVE`, row 18 `KHP_LAZYEND`) |
| `docs/plans/ABI-BREAK-LIST.md`, `docs/plans/KERNEL-BYTE-QUEUE.md` | grep | **no cursor row in either** |

Resident size of what was read (kern_big, map spans): `cursor_hide` to
`cur_shapes` is **1,196 bytes of `.text`**. The three shapes, the hot-spot
table and the state words after them are **112 bytes**. The planar half
(`cur_restoreu`..`cur_draw`) is about **170 bytes of `.vga`**. `cur_save`
is **96 bytes of `.bss`**. The largest single routine is `cur_move_mono`, at
389 bytes.

## Verdict

The 1bpp half is first-rate. It writes every byte exactly once and banks the
geometry once. Three assembly-time asserts check the shape tables. It was
proved against the previous kernel under unicorn. Of the cursor code in any
system this review compares against, it is the best fitted to its machine.

The **lazy-cursor protocol is safe in the shipped arms**. I found no path in
`kernel/` that writes a lit arrow's cell without spending the promise.
Opt-out is still the protocol's design: every writer must remember. The tree
records six times one did not (`font_run_x`, `gfx_xor_rect`, the one-plane
arm under `gfx_save`, `vga_pn`, the gfx-13 class). Nothing standing would
catch a seventh: the instrument that proved the protocol (SPEC.md 7.1.1 and
7.1.4) was scratch code and is not in the tree.

The weak half is **VGA on kern_big**. There a pointer move is the generic rect
machinery, run at IF=0 inside the serial ISR: 21,450 cycles, about 4.5 ms a
packet. It is still erase-then-draw, so the overlap bytes are written twice.
That is the effect docs/FIELD-NOTES.md 6 fixed on Hercules, left in place on
the adapter most kern_big machines have.

There are also two small bugs:

- `[cur_dirty]` is a variable nothing reads (29 bytes).
- The keyboard mouse's draw gate predates the progress widget's
  (FIELD-NOTES 34).

There is also a cluster of comments, SDK lines and SPEC text that describe
`gfx_lock` as hiding the cursor, and `cur_lazyck` as testing the region. In
the default build it does neither: `gfx_lock` only promises the hide, and
`cur_lazyck` tests the frame.

### Answers to the brief's questions

**IF=0 / ISR footprint per move.** All of it runs at IF=0, from
`mou_apply`'s `.go`, before the EOI.

| display | path | instructions (ESTIMATED) | port writes | cycles (MEASURED, source) |
|---|---|---|---|---|
| CGA / Hercules, all kernels | `cur_move_mono` | ~600: bank 8, bg walk 12x8, meet ~40, pass 1 12x18, draw 12x20 | **0** | 11,500 (PERFORMANCE.md 1,355) or 12,000 (SPEC.md 7.1.2) on the 5150's CGA, ~2.4-2.5 ms; 11,700 Hercules |
| VGA/EGA, **kern_big** (no `KHP_VGASAVE`) | `cur_restoreu` -> `cur_rect` -> `vga_restore_vram`, `cur_geom`, `cur_saveu` -> `vga_save_vram`, `cur_draw` | generic rect setup twice plus the walks | ~10 (Map Mask / Read Map per plane and their resets) + up to 24 Bit Mask + 1 = **~35** | **21,450**, ~4.5 ms (LAST-DROP-PERF.md 7.1 row 3, the "before") |
| VGA/EGA, kern_mega (`KHP_VGASAVE`) | the cell's own walks | | same ports | 15,100, ~3.2 ms |
| packed (VESA, `VID_EXT`) | `VX_CURMOVE`, the driver's | - | the driver's | unmeasured |
| two displays (kern_big) | `cur_move_multi` | + up to two `vid_ctx_act` a packet (three on a seam jump) | - | + ~1,000-1,500 PREDICTED |

isr-7's correction quoted 15,100 for VGA. That is kern_mega's number. The
**shipped** VGA kernel pays 21,450 (mc-3).

**Does the ISR draw while a foreground draw holds the screen?** Yes, in one
case and by design: SPEC.md 7.4's `cur_inxfer` arm (mouse.inc 4,220-4,272).
The lock is held by the interrupted task, but that task is parked inside
`int 13h`, so no primitive is in flight. The arm also refuses when a region
is armed (`[wm_clip_n]`) and when the arrow could reach the bar while the
widget is armed (4,324-4,338). I found no hole in it.

The **other** cursor drawer is `kbm_move`'s `.paint` (2,672-2,709), on task
context. It has the older two-condition gate and no `FPG_ARMED` test (mc-2).

**Is the lazy protocol airtight?** Who must call what:

| who | must call | where |
|---|---|---|
| `gfx_lock` | sets the promise | vga12.inc 5,128 |
| every unclipped writer | spends it: `cur_unlazy` / `CURUNLAZY` | `GFXCLIP`'s unclipped branch (vga12.inc 508), `gfx_blit4`, `gfx_scroll`, `gfx_line`, `gfx_lstepv` (1,332, 3,607, 4,204), the VRAM XOR twins (4,278, 4,294), `gfx_save` / `gfx_restore` (4,332, 4,695), `fnt_unlazy` (font.inc 1,133), `ico_core`, `vid_switch` (vidsel.inc 1,020), `fsx_run` (fsx.inc 269), `fprog` (812), shutdown, dock, the winmod and stikmenu modules |
| `wm_clip_set` | `cur_lazyck`: the frame test by default, the region walk under `CURFIX` | wm.inc 6,148 |
| bands and damage | `cur_lazyrect`, the rect they draw | vga12.inc 5,999, wm.inc 6,249 |
| `gfx_unlock` | `cursor_show` if spent, `cur_lazyend` if kept | vga12.inc 5,251-5,260 |

I followed every writer named in SPEC.md 7.1.1's nine miss paths. Each now
spends the promise above its first pixel, and above the `[vid_mono]`
dispatch, as 7.1.5 requires. The one primitive with no way to spend it is
the one SPEC.md 7.1.1 named: **a package writing the framebuffer itself**
under `OSAPI_GFX_LOCK`. The SDK still tells that package the lock "hides the
cursor" (mc-5).

**Multi-display and `[vid_w]` / `[vid_h]` / `[vid_stride]`.** Correct.
`cur_geom` clips with `[vid_stride]` and `[vid_ch]` from the live context
(4,897-4,916) and subtracts `[vid_ox]` / `[vid_oy]` in the one place a
position becomes an address. `cur_move_multi` never straddles: erase whole,
then draw whole.

`ROW_BYTES` (80) is hard-coded in the planar half (`cur_vspan` 4,806,
`cur_draw` 5,918), and so is `VGA_SEG`. That is right for every planar mode
the kernel draws (640x480 and 640x350). It is the same assumption the whole
of `vga12.inc` makes. A wider planar mode would arrive as a `VID_EXT` driver
with its own `VX_CUR*`, so it is recorded, not raised.

**Double-draw flash.** On 1bpp there is none (7.1.2, proved). On VGA the move
is erase-then-draw (mouse.inc 5,618-5,628). Each overlap byte shows its
background, plane by plane, from its restore until the draw: about 2-3.5 ms
of a 14.3 ms frame, PREDICTED (mc-3). `ui_volume_service`'s poll moves the
arrow as `cursor_hide` + `cursor_show`, the pre-7.1.2 two-walk move, on
every adapter (mc-6).

**Stale save-under after a mode change, FSX or hibernate?**

- **Mode change:** `vid_switch` spends the promise before the geometry moves
  (vidsel.inc 995-1,020).
- **FSX:** `fsx_run` spends it, and the held lock keeps the ISR off for the
  bracket.
- **Hibernate:** the image is written with the lock held. Resume sets the
  mode, then `menu_force`, `dock_force` and `wm_paint_all` repaint every
  pixel after any stale restore (hiber.inc 5,058-5,113).

All three are safe. Each rests on an argument rather than a check, and
mc-9 is the check.

## Strengths

1. **Each byte written once on 1bpp, decided once a move**
   (`cur_move_mono`, 5,631-5,859). The overlap is reduced to one row run and
   a 6-bit code in BH before either walk, so neither walk asks a per-byte
   question. Two save buffers are swapped by `neg`/`add`, with no compare.
   The equivalence proof (36,000 random moves under unicorn, every write
   counted) is the right standard for a save-under.
2. **The geometry is derived in one place.** `cur_geom` is the only place a
   position becomes an address. Restores replay `[cur_off]` / `[cur_rows]` /
   `[cur_b1ok]` rather than re-deriving them, and the reason is stated as a
   correctness rule (a disagreement smears).
3. **The tables assert their own invariants.** `CUR_ROW` /
   `CUR_SHAPE_END` (5,957-6,160) fail the build for a shape that is too
   wide, too tall, or has a black bit outside its white row. Each of those
   would otherwise be visible only on the glass.
4. **The promise is cheap.** One-way within a hold: `CURUNLAZY` inlines the
   "already spent" test at 19 sites, so a not-owed hide costs a compare
   (kernel.asm 6,920-6,945).
5. **Hide and show are one body** (`cur_vis`, 4,408-4,472). The direction
   rides in AH, and the caller's flags come back through `popf`.
6. **The busy clock belongs to the hold.** `gfx_unlock` is the one exit
   (vga12.inc 5,234), so no caller can leave a clock up. The three pointer
   states are named and each has its own arm (5,397-5,490).
7. **Multi-display is solved by not straddling.** The arrow leaves one card
   whole and arrives on the other whole, so the fused loop never sees a
   seam. `mou_clamp` keeps the point inside some display, so `cur_geom`'s
   unsigned row count cannot borrow.
8. **`cur_lazyrect` is already half of the Win 3.x / Mac model** (rethink
   R1). The band blit and the damage pass ask about their own rectangle,
   which is exactly per-output cursor exclusion.

## Findings

### mc-1 - `[cur_dirty]` is written in six places and read in none (size, low, now)

**Evidence.**

- Writes: `mou_apply` 4,342 (`mov byte [cur_dirty], 0`) and 4,352
  (`.dirty: mov byte [cur_dirty], 1`); `cur_vis` 4,438; `cur_lazyend`
  5,279; `kbm_move.paint` 2,684 / 2,700 / 2,702 (`mov al, 1` ... `mov al, 0`
  / `mov [cur_dirty], al`).
- Declaration: 6,254, one byte of `.text` (map: `4F11`).
- Readers: `grep cur_dirty` over `kernel/`, `drivers/`, `apps/`, `tools/` and
  `tests/` finds no reader: no compare, no `mov reg, [cur_dirty]`, no host
  tool.

The catch-up it was meant to drive is done by comparing positions instead:
`cursor_show` draws at the live `[mouse_x]` / `[mouse_y]`, and
`cur_lazyend` compares them against `[cur_drawn_x]` / `[cur_drawn_y]`.

**Recommendation.** Delete the variable and its stores. In detail:

- `mou_apply`'s `.go` becomes `jmp cur_move` (from `call` + store + `ret`).
- The `.dirty` arms jump to `.out`.
- `kbm_move.paint` drops `mov al, 1`, `mov al, 0` and the store.

Bytes: 5+5+5 (three immediate stores), 6 (`.go`), 7 (`kbm_move`), 1 (the
byte) = **-29 resident** on kern_big and kern_small, ESTIMATED from the
encodings. Re-run `tests/unit/t_hotpar.py`: this moves `sch_isr` and
everything after it, and the `nop` parity pads at 4,357 and 5,927-5,933
exist for exactly that. SPEC.md 7, 9 and 27,664 change with it (Doc/code
mismatches 14, 15 and 21).

### mc-2 - `kbm_move`'s draw gate is the pre-12.8.4 two-condition gate (hazard, low, now)

**Evidence.** `mou_apply` draws only with the lock free **and** `FPG_ARMED`
clear (4,275-4,291). Without the second test, an IRQ and the lock-free
progress-widget painter share `gfx_rect_setup`'s scratch, the GC state and
the glass. The result was an arrow written into window content for good
(docs/FIELD-NOTES.md 34).

`kbm_move.paint` (2,684-2,700) is a second copy of that gate. It tests the
lock flag and `[cur_level]` only, then calls `cur_move`. `cur_move_mono`'s
working set lives in the primitive union `gfx_u` (6,673-6,697), which a
`gfx_fill` in flight also owns.

The route there:

- `osapi_mouse` calls `kbm_poll` whenever `[mou_ptr]` = 0, i.e. no real
  pointer has proved itself (1,551-1,610). It does so for **any** caller,
  including a package worker polling `OSAPI_MOUSE`.
- `kbm_poll` -> `kbm_key` -> `kbm_move` on a keypad arrow.

`fprog` paints with the lock free between transfers of a launch or an assoc
open, and a tick can pre-empt it there: only the `int 13h` itself is under
`[sch_lock]`. So the window is a mouseless machine (keyboard pointer), a
worker that polls the mouse, and a keypad press during a launch. That is
narrow, and the consequence is FIELD-NOTES 34's permanent smear plus a
corrupted fill.

**Recommendation.** Give `kbm_move.paint` the ISR's `FPG_ARMED` test, about
+7 bytes. Better, make the gate one routine both callers use: a
`cur_gate` that answers CF from the lock, `FPG_ARMED` and `[cur_level]`. The
ISR's `cur_inxfer` arm stays the ISR's. Net about +2 bytes, and the next
gate condition lands once.

Separately (input's area, an open question below): `kbm_poll` issues
`int 16h`, and SPEC.md 7 says "only the UI task calls int 10h/16h after
boot". `osapi_mouse` reaches it from a worker.

### mc-3 - kern_big moves a VGA pointer through the generic rect machinery at IF=0: 4.5 ms a packet, erase-then-draw (weakness, medium, now)

**Evidence.** `KHP_VGASAVE` is defined only under `%ifdef KERN_MEGA`
(kernel.asm 284-295). On kern_big, `cur_restoreu` / `cur_saveu` are
`cur_rect` + `vga_restore_vram` / `vga_save_vram` (4,650-4,670 and
4,773-4,779): `gfx_rect_setup`'s generic clip and a `rep movsb` row loop per
plane, called from the serial ISR. LAST-DROP-PERF.md 7.1 row 3 measured that
`cur_move` at **21,450 cycles** (~4.5 ms) against **15,100** with the knob.

Two inaccuracies follow from this:

- isr-7's corrected figure (15,100) is kern_mega's.
- PERFORMANCE.md Part 5 (1,355) lists "21,450 -> 15,100 on the XT's VGA" as
  the current state, with no kernel named.

And the planar move is still **erase-then-draw** (5,618-5,628).
SPEC.md 7.1.2 says so ("VGA keeps erase-then-draw"). It notes that the fused
draw closed the larger of the two windows, but no flicker reading of the
remaining one has been taken. At ~44 packets/s (1200 baud, 3-byte packets),
the serial ISR spends ~20% of the CPU on the pointer while the hand sweeps
on kern_big VGA, PREDICTED from 21,450 cycles x 44.

**Recommendation.**

- **Now:** take `KHP_VGASAVE` on kern_big. It costs +122 bytes of `.vga`,
  which is resident only on VGA/EGA machines, since a mono machine slides
  `.cold` over the rung. It buys -6,350 cycles a packet, and a VGA raise
  goes 133 -> 99 ms (LAST-DROP-PERF.md 7.1 row 3, measured).
- **Measure:** take one `m.flicker()` reading of a pointer sweep on
  `os8088_xt_vga`. That sizes the erase-then-draw window instead of leaving
  it argued.
- **Structural:** rethink R2 removes both the cost and the double write.

listed_as: LAST-DROP-PERF.md 7.1 row 3.

### mc-4 - The `cur_rows` = 0 guards protect one impossible value and miss the dangerous one (size, low, now)

**Evidence.** `cur_geom` computes rows = `[vid_ch]` - `[cur_celly]`
*unsigned* (4,905-4,916). Its own comment says the result is ">= 1" because
`vid_apply` and `mou_clamp` keep the point on its display (2,737-2,775).

Nevertheless six walks on the shipped kernels open with
`or ch, ch / jz` (4 bytes each):

- `cur_put_mono` 4,535
- `cur_get_mono` 4,690
- `cur_draw` 5,881
- `cur_move_mono` 5,671, 5,773 and 5,824

That is 24 bytes, plus 8 more in the `KHP_VGASAVE` arm.

The case that would actually hurt is the **borrow**. `[cur_celly]` >
`[vid_ch]` makes AX huge, `cmp ax, CUR_GH / jbe` clamps it to **12**, and the
arrow is written 12 rows past the end of the display. On a two-card machine,
Hercules page 1 is the colour card's aperture at B8000h. None of the guards
sees that.

**Recommendation.** Make the cell land on the display by construction:

- Clamp `[cur_celly]` to `[vid_ch]` - 1 in `cur_geom` before `gfx_rowbase`
  (`mov dx,[vid_ch] / dec dx / cmp ax,dx / jbe / mov ax,dx`, ~10 bytes).
- Delete the six guards. With the clamp, rows >= 1 is a fact rather than a
  hope, so removing the guards cannot turn a zero into 256 rows.

Net **~-14 resident**, ESTIMATED. The impossible case becomes a misplaced
arrow rather than a write off the framebuffer.

### mc-5 - The SDK and SPEC.md 7 say `OSAPI_GFX_LOCK` hides the cursor; since 7.1.4 it only promises to (hazard, medium, now / record)

**Evidence.**

- `apps/os88api.inc` 216-217: `OSAPI_GFX_LOCK ; acquire drawing mutex + hide
  cursor`, and `OSAPI_GFX_UNLOCK ; show cursor + release mutex`.
- vga12.inc 5,072 / 5,135 carry the same header.
- SPEC.md 7's first bullet (10,652-10,656): "set flag, `sti`, then `call
  cursor_hide`".

The code sets `[cur_lazy]` and returns (vga12.inc 5,128). Every kernel
primitive spends that promise. A package writing the framebuffer itself,
inside the lock and outside an FSX bracket, has no call that spends it. If
it believed the comment, the arrow stays lit under its writes, and the save
then puts back pixels from before. This is 7.1.1's "cannot be hooked" path,
now with documentation pointing package authors into it.

I found no shipped package that does this. Fractal stages into its own
buffer, and `tests/gfxbench` is a test. So it is a contract defect rather
than a live bug.

**Recommendation.**

- **Now:** correct the four texts (Doc/code mismatches 13, 14 and 19).
  `os88api.inc` is in the ABI fingerprint, so this is
  `tools/abilock.py --update` **without** a bump: a contract clarified, no
  cell moved. The C SDK's `os88_gfx_lock` comment wants the same.
- **Record:** an appended slot, `OSAPI_CUR_EXCLUDE` (AX..DX = an inclusive
  rect), that is `cw_cur_lazyrect` exactly. The far shim already exists on
  kern_big (kernel.asm 7,851), so the cost is one cell, ~+5-8 bytes. It is
  INT 33h function 10h ("conditional off") and the Mac's ShieldCursor for
  this kernel. Appending a cell bumps nothing (SPEC.md 20.2.0.1).

### mc-6 - `ui_volume_service` moves the pointer as hide-then-show (consistency, low, now)

**Evidence.** ui.inc 3,360-3,368, `.move: call cursor_hide / jmp short
.show` (where `.show` is `call cursor_show`). These are the two walks 7.1.2
retired: the overlap bytes are written as background, then as arrow, on
every adapter. That is FIELD-NOTES 6's wash-out, here in the volume popup,
which polls under a held lock.

**Recommendation.** Enter `cur_lazyend`'s move tail. Give the push-cli-load-
`cur_move` run a label of its own below the `[cur_lazy]` clear, so this
caller does not kill a kept promise and send `gfx_unlock` to a second show.
~0 bytes (a `call` replaces a `call` + `jmp`).

### mc-7 - `mov si, bx` before `cur_lazyck` is dead in every arm (size, low, now)

**Evidence.** wm.inc 6,147 loads SI for `cur_lazyck`. Neither arm reads SI:

- The default arm pushes it and loads `mov si, [wm_clipwin]` (mouse.inc
  5,021-5,022).
- The `CURFIX` arm walks `wm_clip_tab`.

`wm_clip_set` returns through `kret_si`, which restores SI anyway.

**Recommendation.** Delete it: **-2 resident**. SPEC.md 7.1.4's "It takes the
window in SI" changes with it (Doc/code mismatch 18).

### mc-8 - The multi-display cursor pays a full context swap from the ISR (perf, low, record)

**Evidence.** `cur_move_multi` (5,566-5,603) calls `vid_ctx_act` for the
arrow's display and again for the one to come back to, and three times on a
seam jump. `cur_vis` does the same pair on every show (4,459-4,466).

Each call that changes display is a 16-word `rep movsw` plus
`vid_depth_set`, plus the row-table switch-off (vidsel.inc 1,558-1,632). The
display left current is "whatever the last primitive left" (39.15.2), so on
a two-card desktop where the pointer is on the card that did not draw last,
every packet pays two swaps: ~1,000-1,500 cycles at IF=0, PREDICTED.

The secondary display also loses `vid_rowtab`, so its `cur_geom` computes
the row base.

**Recommendation.** None now: correct, and paid only on two-card machines.
Recorded so that a later "the pointer is slower on the Hercules side" field
report has its explanation.

### mc-9 - The protocol is opt-out and the instrument that proved it is not in the tree (weakness, medium, now)

**Evidence.** SPEC.md 7.1.4: "The safety argument is a measurement, not an
audit. A debug build snapshots the drawn cell at `gfx_lock` and compares it
at `gfx_unlock`." 7.1.1 calls the same instrument "Scratch
instrumentation, never committed".

`GFXAUDIT`'s `gfx_aud` (vga12.inc 369-409) counts unlocked draws and
collisions, but not a cell written under a kept promise. `CURLOG` records
events without judging them, and no host tool reads `cur_log`
(`grep cur_log tools tests` is empty).

Meanwhile the defect class has recurred six times, each time found on the
glass:

- `font_run_x` (font.inc 1,184-1,190)
- `gfx_xor_rect` (7.1.5)
- the one-plane arm (mouse.inc 4,651-4,667)
- the four gfx-13 sites

**Recommendation.** Make the arrow check itself, under `GFXAUDIT` only.
`cursor_hide` and `cur_move_mono` already hold everything needed: the save,
`[cur_tabp]` and the banked geometry. They recompute
`(save | white) & ~black` for each banked byte and compare it with VRAM
before restoring. A mismatch means somebody drew over a lit arrow, and
`CURLOG` logs it with the tick and the interrupted CS:IP. Do it on 1bpp
only, which is enough to run in every MartyPC CGA/Hercules row.

Cost: **0 shipped bytes**, ~+40 in the audit build. Pair it with
`tests/gfxlk.py` or a new soak row that fails on a non-zero count. This
turns 7.1.4's one-off measurement into a standing guard on the protocol's
weak point.

## Rethinks

### R1 - Per-output exclusion at the unclipped entries (rethink, medium, record)

Win 3.x display drivers and Mac QuickDraw both exclude the pointer **per
output call, against that call's rectangle**:

- The Win 3.x driver's output routines test their destination rect against
  the pointer and exclude it only on overlap.
- QuickDraw shields the cursor against each screen primitive's bounds.

os8088 excludes **per hold**, and proves "not owed" only through an armed
region (`cur_lazyck`) or, since 7.1.4.5, a band or damage rect. Every
unclipped foreground primitive anywhere spends the hide, wherever the arrow
is, so the arrow is off the glass for the rest of that hold.

The machinery to do better is already resident:

- `cur_lazyrect` takes an inclusive rect and preserves every register.
- `GFXCLIP`'s four rect entries hold x1/y1/x2/y2 in AX..DX at the point
  `CURUNLAZY` runs (vga12.inc 505-509).
- `font_run_x` knows x, y and the cell count.

Turning `CURUNLAZY` into a rect test at those sites is about +0-4 bytes a
site on kern_big (a `call cur_lazyrect` in place of `call cur_unlazy`,
inside the same inline guard), plus ~12 for `font_run_x`'s rect, ~+20 to +30
in all. It keeps one hide and one show per hold.

What decides it is not bytes. It is SPEC.md 7.1.4.3 / 7.1.4.4: a promise
kept while the hand moves freezes the arrow, then teleports it. The owner's
eye ranked that, and `CURFIX`'s motion gate (`[cur_mvt]`) is the condition
that must come with it. Record it with that dependency stated. It is what
SPEC.md 7.1.4.5.1 already calls "a change worth costing".

### R2 - The planar save-under in off-screen VRAM, moved by the latches (rethink, medium, record)

Today a VGA cursor costs four planes through Read Map Select and four through
Map Mask, twice a move, plus a CPU buffer of 96 bytes. Mode 12h uses 38,400
of each plane's 65,536 bytes, and 640x350 uses 28,000. Write mode 1 copies all
four planes per byte through the latches, so the whole fused move can run in
VRAM:

1. Latch-copy the new cell's background to a save area off-screen.
2. Patch the overlap from the old save (latch copies).
3. Latch-copy the old cell's non-overlap bytes home.
4. Copy the new save to a scratch cell and draw the arrow into the scratch.
   `cur_draw` unchanged: the latches are now the true background.
5. Latch-copy the scratch onto the screen.

Every screen byte is written **once, all four planes in one store**. So the
erase-then-draw window (mc-3) and the plane-by-plane colour transients both
go. Copies are ~120 `movsb` plus today's draw: ~6,500 cycles a move
PREDICTED, against 21,450 (kern_big) and 15,100 (kern_mega).

Bytes, ESTIMATED: ~+50 `.vga` for the new walk. `cur_save` shrinks from 96
to 48 (the 1bpp pair alone): -48 `.bss`. Net ~0 resident, with `.vga`
billed only on VGA/EGA machines.

To check first:

- Nothing else claims the off-screen VRAM on the desktop. FSX pages run with
  the arrow down; hibernate's image and the menu save-under need a look.
- The EGA probe guarantees 128KB.

Prior art: the Windows 3.x EGA/VGA display driver kept its pointer save
area in off-screen display memory and moved it through the latches, from
memory of the DDK sources.

### R3 - Draw the cursor after the EOI, coalesced (rethink, record; isr-7 R)

The Mac drew the cursor from the VBL task at most once a tick: `CrsrNew` set
by the mouse interrupt, `CrsrBusy` held by drawing code. A burst of packets
therefore cost one draw, never one per packet, and never at serial priority.

isr-7 R already proposes moving `cur_move` behind the EOI with a busy byte.
The cursor-side half of that proposal should carry two things:

- A loop that **re-reads the position until it stops changing**, which is the
  coalescing.
- The rule that `cur_move`'s working set in `gfx_u` is safe there only
  because no primitive is in flight. The comment at 6,673-6,681 states that
  rule as "lock free", which the `cur_inxfer` arm already makes untrue (Doc
  mismatch 12).

Nothing to add in bytes beyond isr-7 R's +30 to +40.

## Prior art

| system | mechanism | what it does better | where os8088 is better |
|---|---|---|---|
| Windows 3.0/3.1 | display driver owns the pointer (the DDI's `SetCursor` / `MoveCursor`, called at interrupt time); each output routine excludes the pointer only when its own rect overlaps; `ShowCursor` is a counter; pointer save area in off-screen VRAM on EGA/VGA (from memory) | per-output exclusion (R1); off-screen latch save-under (R2); a driver interface for an app's own drawing | one hide and one show per hold, not per output call; a background painter behind the pointer keeps it lit by region proof; on 1bpp each byte is written exactly once, proved |
| Mac System 1-7 | `HideCursor` / `ShowCursor` counter; `ShieldCursor(rect)`; `ObscureCursor` (hidden until the next move); the VBL cursor task, with `CrsrBusy` / `CrsrNew` | the cursor drawn at most once a tick and below serial priority (R3); a public rect-shield call (mc-5's slot) | `cur_inxfer`'s reasoning about when the ISR may draw is explicit and gated; the busy clock is tied to the hold and cannot leak |
| DOS INT 33h drivers | the cursor drawn in the mouse IRQ (os8088's model); fn 1/2 a show/hide counter; fn 10h "conditional off" in a rect until the next show | fn 10h is the API a raw-framebuffer program uses to protect itself; os8088 has no equivalent (mc-5) | INT 33h drivers drew over anything at any time; os8088 refuses while a primitive could be in flight |
| OS/2 1.x PM | the display driver's pointer with per-output exclusion, drawn at interrupt time (as Win 3.x) | same as Windows | same as Windows |

## Doc/code mismatches

1. **kernel/mouse.inc:8-9.**
   - Text: "moves the cursor itself only when the gfx lock is free and the
     cursor is refcount-visible (SPEC 7); otherwise it just sets cur_dirty."
   - Code: `[cur_dirty]` is never read (mc-1). There are also the
     `FPG_ARMED` and `cur_inxfer` arms (4,220-4,338).
   - Proposed: "moves the cursor itself only when nothing can be drawing
     under it - the gfx lock free and no progress widget armed, or this task
     parked in `int 13h` (SPEC.md 7, 7.4) - and the arrow refcount-visible;
     otherwise the next `cursor_show` or `cur_lazyend` catches up from the
     position."
2. **kernel/mouse.inc:19-20.**
   - Text: "Both are called with the gfx lock held (from gfx_lock/gfx_unlock)
     or at boot."
   - Code: `gfx_lock` calls neither. The hide comes from `cur_unlazy`, from
     `fpg_arm` with the lock free (fprog.inc 733), from the saver
     (`cw_cursor_hide`), from shutdown and from the volume service.
   - Proposed: "The hide is reached through `cur_unlazy` (gfx_lock's
     deferred hide, SPEC.md 7.1.4) and from the few lock-free owners of the
     glass - fprog's bar, the screen saver, shutdown; the show from
     `gfx_unlock` and those same owners."
3. **kernel/mouse.inc:22-24.**
   - Text: "The hotspot is the drawn position (0,0 of the cell) ...
     mouse_x/mouse_y themselves range over the full 0..639 / 0..479."
   - Code: there is a hot spot per shape, subtracted and clamped in
     `cur_geom` (4,867-4,884). The range is `mou_clamp`'s,
     `[vid_wm1]` / `[vid_hm1]`, or the virtual desktop on two displays.
   - Proposed: "The hot spot is the shape's own (SPEC.md 7.2.2): the cell is
     placed off it and clamped at the display's top and left, clipped at its
     right and bottom; mouse_x/mouse_y range over the live screen, or the
     virtual desktop on two displays (mou_clamp)."
4. **kernel/mouse.inc:6164-6165.**
   - Text: "public: cursor position, 0..639" / "0..479".
   - Code: same as 3.
   - Proposed: "public: cursor position, 0..[vid_wm1] (virtual on two
     displays)" / "0..[vid_hm1]".
5. **kernel/mouse.inc:4454-4456.**
   - Text: "a hide is reached from gfx_lock's deferred hide AND from the
     mouse ISR".
   - Code: no ISR calls `cursor_hide`. The ISR reaches `cur_move`, whose
     multi-display arm owns its own bracket (5,566).
   - Proposed: "a hide is reached from gfx_lock's deferred hide and from
     lock-free owners of the glass (fprog, the saver) - neither inside a
     39.14 drawing hook".
6. **kernel/mouse.inc:4682.**
   - Text: "[cur_off], [cur_shift], [cur_rows] and [cur_b1ok] are not
     recomputed".
   - Code: there is no `cur_shift`. The shift is `[cur_cellx]` & 7.
   - Proposed: "[cur_off], [cur_rows] and [cur_b1ok] are not recomputed".
7. **kernel/mouse.inc:4977-4996** (`cur_lazyck`'s header).
   - Text: "THE TEST IS THE REGION, NOT THE WINDOW'S FRAME ... The frame
     test called that reachable and spent the hide".
   - Code: the default build (`CURFIX` undefined) runs the **frame** test
     (5,016-5,060). The region walk is the `CURFIX=1` arm (SPEC.md
     7.1.4.4).
   - Proposed: prefix the banner with "UNDER CURFIX=1 (SPEC.md 7.1.4.4; the
     default build tests the window's frame, below):".
8. **kernel/wm.inc:6138-6141.**
   - Text: "The region and not the window: the fragments are the content
     rect minus the windows ON TOP of it".
   - Code: as 7.
   - Proposed: "Under CURFIX=1 the region and not the window (SPEC.md
     7.1.4.2); the default build asks about the window's frame (7.1.4.4)."
9. **kernel/mouse.inc:5306.**
   - Text: "in: AL = CUR_*SH; the gfx lock held by the caller".
   - Code: `cur_busy_on`'s lock-free LIT arm calls it after `cursor_hide`
     (5,477-5,485).
   - Proposed: "in: AL = CUR_*SH; the arrow off the glass, or a gfx-lock
     promise for cur_unlazy to spend".
10. **kernel/mouse.inc:5867-5873** (`cur_draw`).
    - Text: "white pass with the cur_and outline mask, black pass with the
      cur_data body. Set/Reset supplies the color".
    - Code: one masked store a byte, Set/Reset **off**, the plane data
      white & ~black (5,886-5,916; SPEC.md 7.1).
    - Proposed: "ONE masked store a byte: Bit Mask = the white row, CPU data
      = white & ~black with Set/Reset off, so halo bits land 15 and body bits
      0; a read of each target byte loads the latches first."
11. **kernel/mouse.inc:6600-6601, 6614-6616.**
    - Text: "a WRAPPING ring of cursor events" and "tools/os88snap.py and the
      CURLOG readers need no change".
    - Code: `cur_logput` "STOPS WHEN FULL rather than wrapping" (4,368), and
      no host tool reads `cur_log`.
    - Proposed: "a ring of cursor events that STOPS when full (cur_logput
      says why)" and "os88sym.linear already answers LOW_SEG for it".
12. **kernel/mouse.inc:6673-6681.**
    - Text: "cur_move runs from the mouse ISR and kbm_move only with the gfx
      lock FREE".
    - Code: SPEC.md 7.4's arm draws with the lock held by a task parked in
      `int 13h` (4,255-4,272).
    - Proposed: "...only when no primitive is in flight - the lock free, or
      held by a task parked in int 13h (SPEC.md 7.4) - and from gfx_unlock's
      cur_lazyend after every primitive of the hold has returned".
13. **kernel/vga12.inc:5072, 5135.**
    - Text: "gfx_lock - acquire the drawing mutex, then hide the cursor" and
      "gfx_unlock - show the cursor, then release the mutex".
    - Code: 5,128 only sets `[cur_lazy]`, and 5,251-5,260 shows only if the
      promise was spent.
    - Proposed: "gfx_lock - acquire the drawing mutex and PROMISE the cursor
      hide (SPEC.md 7.1.4)" and "gfx_unlock - put the cursor back (a show if
      the hide was spent, a catch-up move if not), then release the mutex".
14. **SPEC.md 10,652-10,656** (§7).
    - Text: "else set flag, `sti`, then `call cursor_hide`. `gfx_unlock`:
      `call cursor_show`, then clear the flag."
    - Code: as 13.
    - Proposed: "else set flag, `sti`, and promise the hide in `[cur_lazy]`
      (§7.1.4). `gfx_unlock`: `cursor_show` if the promise was spent,
      `cur_lazyend`'s catch-up move if it survived, then clear the flag."
15. **SPEC.md 10,660-10,664** (§7).
    - Text: "it only updates `mouse_x/mouse_y`, sets `cur_dirty`, and
      returns. `cursor_show` ... redraws at the latest position, clearing
      `cur_dirty`."
    - Code: mc-1.
    - Proposed: "it only updates `mouse_x/mouse_y` and returns; `cursor_show`
      (or `cur_lazyend`) redraws at the latest position."
16. **SPEC.md 13,706-13,714** (§9).
    - Text: "Two 16-row × 16-bit tables ... **The tables are 16×16** ...
      Draw on VGA: white pass = Set/Reset white, Bit Mask = mask row bits;
      black pass in the SAME store".
    - Code: three shapes of two 12-byte tables each (6,099-6,131;
      §7.1, "12x8"). The draw is one store with Set/Reset off.
    - Proposed: "Three shapes (§7.2), each two 12-row tables of one byte a
      row ... Draw on VGA: ONE store a byte, Bit Mask = the white row and
      plane data = white & ~black with Set/Reset off".
17. **SPEC.md 13,727-13,729** (§9).
    - Text: "guard their critical sections with cli/sti".
    - Code: `pushf` / `cli` ... `popf` (4,423, 4,471), §1's rule.
    - Proposed: "guard their critical sections with `pushf`/`cli` ...
      `popf`".
18. **SPEC.md 11,028-11,030** (§7.1.4).
    - Text: "It takes the window in **SI**, which is `wm_win_rect`'s own
      register".
    - Code: `cur_lazyck` loads SI from `[wm_clipwin]` itself (5,022).
    - Proposed: "It reads the window from `[wm_clipwin]` and hands it to
      `wm_win_rect` in SI - `wm_clip_set` banks it there at entry because BX
      does not survive the fragment loop."
19. **apps/os88api.inc:216-217** (and `apps/cc/os88.h`'s lock comment).
    - Text: "acquire drawing mutex + hide cursor" and "show cursor + release
      mutex".
    - Code: as 13.
    - Proposed: "acquire drawing mutex (the cursor hide is DEFERRED to the
      first drawing call that needs it, SPEC.md 7.1.4 - raw framebuffer
      writes do not spend it)" and "restore cursor + release mutex".
    - Note: `tools/abilock.py --update`, no bump.
20. **PERFORMANCE.md 1,355.**
    - Text: "19,400 -> 11,500 cycles a move on the 5150's CGA ... and the
      planar save/restore the cell's own walks, 21,450 -> 15,100 on the XT's
      VGA".
    - Code: the planar walks are `KHP_VGASAVE`, kern_mega only (kernel.asm
      284-295). SPEC.md 7.1.2 and mouse.inc 5,539 say 19,400 -> **12,000**.
    - Proposed: "...19,400 -> 11,500-12,000 cycles a move on the 5150's CGA
      (SPEC.md 7.1.2), and on kern_mega (`KHP_VGASAVE`) the planar
      save/restore the cell's own walks, 21,450 -> 15,100 on the XT's VGA;
      kern_big's VGA is still 21,450".
    - Which of 11,500 and 12,000 is the measurement is an open question.
21. **SPEC.md 13,696-13,698 and 27,664.**
    - Text: "otherwise just update position and set `cur_dirty`" and "it
      sets `[cur_dirty]` and returns".
    - Code: mc-1.
    - Proposed: "otherwise just update the position" and "it returns,
      leaving the move to whoever next shows the arrow".
22. **kernel/mouse.inc:6040 and SPEC.md 12,074.**
    - Text: "Twelve of the cell's ninety-six pixels are left transparent".
    - Code: `CUR_BUSY`'s white rows (6,060-6,071) set 52 pixels, so 44 are
      transparent: 12 at the dial's corners and the 32 of the four empty
      rows.
    - Proposed: "The twelve pixels round the dial's corners are left
      transparent - forty-four of the cell's ninety-six, with the four empty
      rows below it".

## Open questions

1. Is `OSAPI_MOUSE` legal from a worker, given that it reaches `kbm_poll`'s
   `int 16h` on a mouseless machine (mc-2)? SPEC.md 20.6's worker rules,
   and SPEC.md 7's "only the UI task calls int 10h/16h", should say which.
2. 11,500 (PERFORMANCE.md) or 12,000 (SPEC.md 7.1.2, mouse.inc) cycles for
   the 1bpp move: which is the measurement of record?
3. Does anything on the desktop use VGA/EGA off-screen memory: the menu
   save-under, hibernate's capture, a theme? And does the EGA probe refuse a
   64KB card? Both decide R2.
4. Has an `m.flicker()` reading of a VGA pointer sweep ever been taken? If
   not, mc-3's "visible" is predicted. It is the one measurement that ranks
   `KHP_VGASAVE` against R2.

## Verification

An adversarial re-read on 2026-11-08 at `4b9f1715`, against the current files.
Line numbers have moved by about 12 since the review.

**Findings: 11 confirmed, 1 corrected, 0 refuted.**

- **mc-3, confirmed** (already queued as LAST-DROP-PERF row 3).
  `KHP_VGASAVE` is defined under `KERN_MEGA` only (kernel.asm 293-294).
  `cur_move_1`'s planar arm is restore, then save, then draw. SPEC 7.1.2
  already names kern_mega as the kernel behind 15,100, and its 21.5% ISR share
  supports the ~20% prediction. PERFORMANCE Part 5's row now names the
  kernel. The 11,500 vs 12,000 question (open question 2) is left alone.
- **mc-5, confirmed.** gfx_lock only stores `[cur_lazy]`, and no OSAPI cell
  spends it. **The `apps/os88api.inc` fix is withheld from doc_fixes**:
  `tools/abilock.py`'s `surface()` hashes an `OSAPI_*` define's trailing
  comment, so that edit has to land with `abilock.py --update` (no bump), or
  `make` fails. The vga12.inc and SPEC 7 texts are fixed.
- **mc-9, confirmed.** SPEC 10847 does say "never committed", and nothing in
  `tools/` or `tests/` reads `cur_log`.
- **mc-1, confirmed.** There are stores only, about -28 bytes. SPEC 10691,
  13745 and 27726 say `cur_dirty` is SET, which is true of the code, so they
  are not doc mismatches. They change when mc-1 deletes the variable.
- **mc-2, confirmed** (+7: `cmp byte [fpg_on],0` + `jne`). fprog.inc 771-790
  admits a lock-free bar composition on a non-UI task and counts IRQ4 as the
  only painter to shut out. kbm_move is a second one. The UI task's own
  keypad path is a likelier way in than a worker.
- **mc-4, corrected.** The guards are 6 shipped plus 2 KHP, 4 bytes each.
  The "dangerous borrow" is excluded by the same vid_apply/mou_clamp/display
  activation invariant that excludes rows = 0, so this is a size item
  (-24, or -14 with an optional defensive clamp), not a hazard.
  cur_geom's `out: 0..CUR_GH` should become `1..CUR_GH` with the deletion.
- **mc-6, mc-7 confirmed.** **mc-8, R1, R2 and R3 were spot-checked and are
  plausible as records.** `vid_ctx_act` short-circuits on the current
  display, and the off-screen arithmetic (27,136 bytes a plane) checks.

**Doc fixes: 21 confirmed.**

- **Withheld:** the os88api.inc fix (abilock); SPEC 10691 and 27726 (accurate
  until mc-1); SPEC 13706 and 13727, which the doc pass had already rewritten.
- **Refined:** the file header's gate now names FPG_ARMED and the int 13h
  arm; the hot spot is placed off the shape's own point and clamped (a
  shift) at the top and left; and "a hide is reached ... from the mouse ISR"
  became "lock-free owners of the glass".
- **Added by the verifier:** cur_put_1's header ("the draw is Set/Reset
  through the Bit Mask"), the same drift as cur_draw's; and the gfx_unlock
  header beside gfx_lock's.
- **Checked:** every old_text occurs exactly once in its file, and the code
  changes touch only text after `;`.

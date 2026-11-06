# Kernel hot-paths pass 1: wm

Window and menu operations: `kernel/wm.inc`, `kernel/instance.inc`,
`kernel/menu.inc`, `kernel/dock.inc`, `kernel/toast.inc`, `kernel/blank.inc`,
`kernel/ui.inc` (all but `ui_task`'s idle pass) and the consumer side of
`kernel/events.inc`. The brief is `docs/plans/khp1/BRIEF.md`.

Branch `khp1-wm`, cut from `6d6c23ae`, merged with `kernel-hotpaths` up to
`d9cda820` (the pointer merge). **Headline, MartyPC 5150/CGA, deskbench's
four-window scene, entry to return of the routine that does the operation:
leaving a fullscreen bracket 767 -> 573 ms (Hercules 967 -> 664, VGA 1,193 ->
662), a raise 4-10% faster on every adapter (Note Pad over Paint: CGA 171 ->
154, Hercules 191 -> 163, VGA 157 -> 138), and a drag whose vacated strip lies
under another window 12-32% faster (Paint in the scene's own z-order on CGA
242 -> 212; the Control Panel on VGA 335 -> 229) - for +196 bytes of kern_big
`.text` in six owner's picks and one free item. Every settled screen is the
base's: after each of the fourteen operations the incremental screen was
compared, in the same run, against a forced full repaint, and tip and base
show the same residue (section 8).**

Every figure is MartyPC guest cycles at 4.772727 MHz unless it says otherwise.
Two instruments were written for this pass and kept in this agent's scratch,
not the tree; both are described so they can be rebuilt:

* **`optime.py`** - tests/deskbench.py's scene (its `build`), then each
  operation timed EXACTLY: a stop breakpoint on the routine that does it
  (`wm_front_b` for a raise, `wm_paint_dmg` for a drag's commit, `menu_drop`,
  `blk_relit`), the return address read off the stack and armed as the second
  stop, every drawing primitive and wm routine armed as MartyPC LOGGING
  breakpoints in between (so each call is counted and time-stamped without
  stopping the guest), and an exclusive sampling profile every 800 cycles. It
  writes the call timeline, which is how every item below was found and
  priced, and after every operation a screen capture followed by the
  dispcorner check (WF_SAVEU off, `[cp_dirty]`, settle, diff). The scene
  differs a few pixels run to run (deskbench says so: `new_window` waits on
  host time), so **cross-run pixel comparisons are not evidence**; the
  in-run incremental-versus-full comparison is.
* **`tools/os88hot.py`** (bench's) for the shared `raise`, `drag` and `menu`
  scenarios, base `d9cda820` against the tip.

## 1. BASE AND TIP

This branch's bytes, per commit, from `make`'s `kernsize` at each commit. The
base is `6d6c23ae` for the first items and the merged `kernel-hotpaths` after,
so the deltas are the items' own, not the merges'.

| kernel | `.text` | `.bss` | resident delta (this branch) | `KERN_SIZE` at tip |
|---|---:|---:|---:|---|
| kern_big | **+196** | 0 | **+196**, all before `.cold` | 97,792 of 100,864 (with every merged agent) |
| kern_small | **+165** | 0 | **+165** | 59,904 of 64,000 |
| kern_emu | +196 (the same code; no item is `KERN_EMU`-gated) | 0 | +196 | 97,792 of 100,864 |
| kern_mega | +196 (the same code; no item is `KERN_MEGA`-gated) | 0 | +196 (conventional) | resident 62,976 of 68,096 |

kern_big per item: +4, +10, +24, +34, -4, +38, +90. kern_small: +4, +10, 0,
+28, -6, +38, +91. **All of it is before `.cold`** - BRIEF section 8's
COLD_HOME limit: this branch alone is under the ~300-byte flag (196), and the
one candidate that would have taken it past (the grow box blit, +189) was not
committed for that reason (section 4). `make`, `make small` (inside `all`),
`make emu` and `make mega` build at the tip; `hotpar` is green (four commits
carry a deliberate one-byte encoding to keep `ui_task`'s and kern_small's
`wm_paint_all`'s parity, each said in its commit).

## 2. WHERE THE TIME GOES

Measured on the base (`6d6c23ae`) with optime, CGA unless named.

### 2.1 A raise (Note Pad over Paint, CGA, 172.9 ms, 155 primitive calls)

| phase | ms | what |
|---|---:|---|
| the outgoing front's title bar (inactive) | 15 | interior fill, a gap fill over it, 5 `font_char`s, the rule |
| `wm_su_take` of the outgoing front (Paint) | **50** | one `gfx_save` of its whole content - `sw_xfer`'s `rep movsb`, 27 cycles a byte with CGA's wait states |
| `menu_draw_bar`, `wm_dock_under` | 11 | the bar's text, two dock-mark `gfx_xor_rect`s |
| the incoming frame and shadow | 18 | `gfx_frame` (4 fills) and the shadow (2): three window-high 1-px columns, **5.3 ms each** (sw_col, ~160 cycles a row) |
| the incoming title bar (active) | 29 | fill, 6 stripes, two boxes (fill + frame each), the collapse mark, the gap, 8 `font_char`s, the rule |
| `gfx_restore` of the covered box | 23 | the raise cache |
| `wm_grow_paint` | 10 | 2 fills + 3 frames = 14 calls for 169 pixels |

By routine: `sw_xfer` 41%, `sw_col` 21%, `sw_plane_op` 6%, `gfx_rect_setup`
3.4% - three quarters of a raise is in the 1bpp primitives (the mono agent's),
and the wm-side lever is how many calls and how large a save/restore. Hercules
is the same shape (191 ms). **On VGA a raise of the Disk window is 285 ms of
which the save of Paint is 87 and the restore 119** (`vga_save_vram_v` and
`vga_restore_vram_v`, the vga agent's); bench's baseline says 70%.

### 2.2 A drag's commit (Paint moved 48 right and 12 down, CGA, 240 ms)

Dither 12 ms; the bar 2; then each window marked: the Control Panel 35 ms,
the Disk window 30, Note Pad 44, Paint (the mover) 110 - its drag-cache
replay 47, title 29, chrome 19, grow box 11. In the scene's own z-order the
Control Panel's whole contribution was a column the Disk window already
covered (section 3, wm-6).

### 2.3 Leaving Paint's fullscreen bracket (`wm_paint_all`, CGA 778 ms, 809 calls)

The desktop dither whole (45 ms); the bar's logo **78 `gfx_pixel` calls, 39
ms**; then every window back to front WHOLE: each covered window restored its
whole content from its cache (~28 ms) **and re-saved it straight back (~31
ms)**, for the windows above to paint over; Paint's `W_PAINT` 200 ms. VGA
1,203 ms, Hercules 984.

### 2.4 A menu (`menu_drop`, CGA 47 ms open / 22 close)

The save-under (`gfx_save`), one fill, one frame, a `font_run_x` an item, and
on close one `gfx_restore`. Already one call per element; nothing was taken.

## 3. TAKEN

M = MEASURED (MartyPC guest cycles), P = PREDICTED.

| id | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---:|---:|---|---|---|---|---|---|
| wm-1 | `wm_draw_win` -> `wm_su_bank` | H3 | a covered window put back WHOLE from its cache is not re-banked (SPEC.md 11.96.4.1) | +4 | +4 | 3,712,424 -> 3,206,000 (`wm_paint_all`, deskbench fullscreen exit, item alone) | fullscreen exit, CGA: 777.8 -> 671.7 | M | same (a save is a copy) | 9c64eaea | **[+4] OWNER'S PICK** |
| wm-2 | `wm_draw_title` | H3 | an inactive bar does not fill the caption's gap (`kernel-hotpaths`' SPEC section 11.92.1) | +10 | +10 | the gap fill, ~10k cycles a caption | ~2 ms an inactive caption, CGA: every raise (1), every wm_paint_all (3) | M | same | db2e9670 | **[+10] OWNER'S PICK** |
| wm-3 | `wm_draw_title` | H3 | an unclipped caption is one `font_run_x`, not a `font_char` a glyph (kern_big) (`kernel-hotpaths`' SPEC section 11.92.2) | +24 | 0 | a caption 7.2 -> ~1.5 ms (8 cells, CGA) | with wm-2, raise CGA: Note Pad 172.9 -> 168.0, Disk 158.6 -> 152.9, CP 155.3 -> 147.3, Paint 179.9 -> 170.9 | M | same | cd852c89 | **[+24] OWNER'S PICK** |
| wm-4 | `blk_relit` | H3 | a screen given back is the damage pass over the whole band (`kernel-hotpaths`' SPEC section 11.91.7) | +34 | +28 | 3,726,000 -> 3,003,000 with wm-1 (`blk_relit`, CGA) | fullscreen exit, with wm-1: CGA 780.7 -> 629.2, Hercules 983.9 -> 727.4, VGA 1,203.0 -> 705.5; the saver's wake takes the same path. **CHANGES TRANSIENTS**: a pixel is written about once, not once per window layer (deskbench measured 7-8 transient pixels per changed one at the base) | M | same | 233ccd04 | **[+34] OWNER'S PICK** |
| wm-5 | `menu_draw_bar` | H3 | the logo is seven rects, not 78 `gfx_pixel`s (SPEC.md 12.05.1) | -4 | -6 | 78 calls -> 7 | fullscreen exit: CGA 629.2 -> 593.4, VGA 705.5 -> 671.3; every saver wake and the first paint | M | same | f67da13c | **FREE** |
| wm-6 | `wm_dmg_stale` | H3 | a window whose vacated part is under somebody else is not marked (`kernel-hotpaths`' SPEC section 11.91.8) | +38 | +38 | the Control Panel's draw, ~165k cycles, gone; +~2 ms of region walk a drag where nothing is hidden | drag, scene z-order, CGA: Paint away 241.9 -> 212.0, back 257.7 -> 220.8; Hercules Paint away 306.1 -> 250.6, Control Panel 260.6 -> 182.1 / 251.7 -> 171.0; VGA Control Panel 334.6 -> 228.9 / 331.1 -> 234.7; a drag with nothing hidden +0.3..+1.6% | M | same | ce2df232 | **[+38] OWNER'S PICK** |
| wm-7 | `wm_cov_rect`, `wm_raise`, `wm_draw_win` | H3 | a partial raise redraws its outline and shadow only where they were covered (`kernel-hotpaths`' SPEC section 11.96.10.2) | +90 | +91 | three window-high 1-px columns -> those that were covered | raise, CGA (with wm-2/3): Note Pad 167.7 -> 153.6, Disk 152.7 -> 147.6, CP 147.3 -> 142.4; full tip vs base on Hercules Note Pad 190.5 -> 162.6, VGA 157.3 -> 138.0 | M | same | 4dd8b6d6 | **[+90] OWNER'S PICK** |

**The whole branch, base `d9cda820` -> tip, optime (entry to return):**

| operation | CGA | Hercules | VGA |
|---|---|---|---|
| raise Note Pad (over Paint) | 171.2 -> 153.6 (-10.3%) | 190.5 -> 162.6 (-14.6%) | 157.3 -> 138.0 (-12.3%) |
| raise Disk window | 157.0 -> 147.6 (-6.0%) | 222.9 -> 210.0 (-5.8%) | 281.0 -> 275.8 (-1.8%) |
| raise Control Panel | 153.9 -> 142.4 (-7.5%) | 159.8 -> 148.5 (-7.1%) | 278.1 -> 271.9 (-2.2%) |
| drag Paint, scene z-order (CP under Disk) | 241.9 -> 212.0 (-12.3%) | 306.1 -> 250.6 (-18.1%) | 471.7 -> 444.2 (-5.8%) |
| drag the Control Panel (front) | 191.3 -> 188.4 (-1.5%) | 260.6 -> 182.1 (-30.1%) | 334.6 -> 228.9 (-31.6%) |
| drag Paint, nothing hidden | 241.5 -> 242.1 (+0.3%) | 294.0 -> 292.5 (-0.5%) | 503.9 -> 504.4 (+0.1%) |
| menu drop, hold, dismiss | 88.3 -> 88.3 | 87.4 -> 86.6 | 127.3 -> 126.5 |
| fullscreen exit | **767.0 -> 573.4 (-25.2%)** | **966.9 -> 664.1 (-31.3%)** | **1,193.2 -> 661.9 (-44.5%)** |

**And os88hot** (`ab --base d9cda820`, medians of 3; menu VGA of 7): raise
Disk CGA 177.9 -> 166.5 ms (-6.4%; busy -5.2%), Hercules 226.2 -> 214.6
(-5.1%), VGA 362.7 -> 357.5 (-1.4%); drag (8 packets, busy ms) CGA 500.2 ->
498.1, Hercules 524.9 -> 524.1, VGA 647.2 -> 596.6 (-7.8%); menu open busy
unchanged on all three; menu close CGA 22.3 -> 20.3, Hercules 20.2 -> 20.6,
VGA 28.8 -> 29.3 (n = 7; a profile A/B of the same scenario read 29.7 -> 28.1,
so this is the close's packet-arrival phase, not code - no routine on the
close's path changed).

The 286 column: nothing here is an instruction-timing change; every item
removes primitive calls or pixels, which a 286 runs proportionally faster but
in the same ratio. None was measured on 86Box.

## 4. TRIED AND REFUSED

* **The grow box as one `gfx_blit1` (kern_big).** Built (patch kept in this
  agent's scratch, `growbox-blit1.patch`): a 13-word bitmap shifted to the
  box's phase into a 39-byte band, one blit in `thm_pair`'s pair, the band
  turned over on 1bpp when the ground is lit, fourteen calls kept for a
  disabled pen or a two-colour window. **+150 `.text` +39 `.bss`** for a
  predicted ~39k cycles (~8 ms on CGA) a grow paint - every raise or repaint
  of a front sizable window, which the files agent priced at 10 ms of a view
  toggle. Not committed: it would take this branch past BRIEF section 8's
  300-byte COLD_HOME flag on its own, and it is the worst ms-per-byte item
  found. It also tripped `t_vidext` (a new `[vid_mono]` dispatch, which needs
  a `tests/vidext.txt` line: a packed display takes the pen arm, which is
  right). If the owner wants it, it is a clean +189.
* **The grow box with fewer fills in painter's order** (black square, white
  interior, black squares, white interiors: 6 calls for 14): pixel-identical
  settled, but each interior is written black then white, ~120 more transient
  pixels a box. Not built.
* **The six pinstripes as one `gfx_fill_pat`**: 6 calls -> 1 but writes the
  five ground rows between the stripes again, and needs the 0/15 pair (Color's
  presets can stripe in colour, so the hlines stay as a fallback). Predicted
  ~2.5 ms of a front title on CGA for ~45 bytes. Not built.
* **Banking only the part of the outgoing front window the incoming one will
  cover.** The save is 50 ms on CGA and 87 on VGA, but the cache is the
  window's for every LATER uncover too, and a later raise over it does not
  re-bank (only the outgoing front is banked). A regional cache that is
  topped up on each covering is the real design; it is a WANTS THE OWNER
  (section 6).
* **Sleeping instead of spinning in `wm_anim`'s frame wait** (sched's find):
  the deadline is half a tick off the PIT (SPEC.md 11.99.4), and the
  scheduler wakes on ticks, so a sleep would overshoot by up to 55 ms a frame.
  The spin costs busy time, not latency. Left alone.
* **`wm_dmg_stale`'s region walk where nothing is hidden** costs ~1-4 ms a
  drag (rows "nothing hidden" above). Taken anyway, as wm-6's price.

## 5. WANTS AN ABI BREAK

None found. Every item is kernel-internal; `OSAPI_WM_GROW` (the grow-box cell
packages call after a self-repaint) would carry the grow box item unchanged.

## 6. WANTS THE OWNER

* **The drag outline when the pointer is still.** `ui_track` erases and
  redraws the XOR outline every tick, moved or not, because the lock is
  dropped between the two so background painters can draw. On CGA a Disk-
  window outline is ~7 ms a rect, so a held, still drag spends ~14 ms of
  every 55 ms tick (24% of the machine, measured by sampling) and the outline
  is dark for the whole unlocked window - visible flicker. Skipping both
  XORs and the unlock when the pointer has not moved removes both, and
  freezes background painting for as long as the hand is still. A behaviour
  change; not taken.
* **`wm_anim`'s duration.** Six frames of half a tick, ~137 ms of every window
  open, close and minimize, by design (SPEC.md 11.99); the files agent
  measured the zoom outline at ~200 ms of a 1.5 s open on CGA. Four frames
  would give back ~55 ms each time. A look question.
* **A regional raise cache** (section 4): bank only what the incoming window
  covers, and top the cache up when a later raise covers more. Worth up to
  the outgoing save's share of the covered area - on deskbench's Note Pad
  raise ~58% of 50 ms on CGA and of 87 ms on VGA - for a design change in the
  cache's validity rules (SPEC.md 11.96.x).

## 7. CROSS-FILE

* **softgfx.inc `sw_xfer` (mono):** `rep movsb` a row, 17 clocks a byte plus
  CGA's wait states - 41% of a CGA raise (46 ms), every save-under and every
  raise-cache restore. `shr cx, 1` / `rep movsw` / `adc`-`movsb` is 12.5 a
  byte (gfx_blit1 already does it, SPEC.md 5.4.2). Predicted ~10 ms a raise
  on CGA. P.
* **softgfx.inc `sw_col` (mono):** ~160 cycles a row for a 1-px column on
  CGA (5.3 ms for a 158-row frame side, measured off the call timeline); a
  window's frame and shadow are three of them. The row loop tests the bank
  wrap every row; walking each bank with a constant stride (two on CGA,
  four on Hercules) removes the test. Predicted ~40% of `sw_col`, ~6 ms of a
  whole window's chrome on CGA. P.
* **vga12.inc `vga_save_vram_v` / `vga_restore_vram_v` (vga):** 70% of a VGA
  raise (87 + 119 ms of the Disk window's 285). M (share).
* **font.inc `font_char` (text):** ~0.8 ms a cell on CGA; captions drawn
  under a region (a covered window's, on every damage pass) still letter
  through it.

## 8. ROWS RUN

Equivalence, measured: after each of fourteen operations on each adapter
(two drags in the scene's order, four raises, six drags, a menu, a fullscreen
exit), the incremental screen against a forced full repaint in the same run
(tests/dispcorner.py's method). **Tip and base read the same residue
everywhere**: 0 pixels after all but three drags, and there the same 324-px
(CGA) or 2,642-px (Hercules) inverted-checkerboard swatches - Paint's
dithered palette replayed by the drag cache at an odd row (SPEC.md
11.96.13.1, which dispcorner accepts by name). On VGA one drag read 133 px at
the base and 48 at the tip, in Note Pad's scroll track, the same accepted
class.

Rows covering these items, run ONCE, scoped, after the last code commit
(`python3 tools/os88test.py soak -k ... --marty-jobs 1`, tip `218065e1`, the
`d9cda820` merge): **26 passed, 0 failed, 0 skipped in 657 s** -
`dragui`, `clipkeep`, `saver`, `deskbench`, `dockmark`, `dispapp`,
`dispband`, `dispsaver`, `dispzoom`, `dispcheck`, `dispclose`, `dispcorner`,
`dispdrag`, `disptitle`, `dispfsx`, `dispherc1`, `fsxclip`, `dispfsxherc`,
`dispfsxcga`, `dispnp`, `dispsave`, `deskflash`, `paintsu`, `runclip`,
`clipgrow`, `wmchrome`. By item: wm-1 `paintsu`, `dispsave`, `dispcorner`;
wm-2/wm-3 `disptitle`, `wmchrome`, `dispherc1`; wm-4 `dispfsx`,
`dispfsxcga`, `dispfsxherc`, `fsxclip`, `saver`, `dispsaver`; wm-5 the same
four; wm-6 `dispdrag`, `dragui`, `dispclose`, `dispcorner`, `clipkeep`,
`dockmark`; wm-7 `dispcheck`, `dispapp`, `wmchrome`, `deskflash`. Not run,
and owed by the coordinator's union: the whole `disp*` family beyond these.

## 9. DEFECTS FOUND

* **tests/deskbench.py's scene did not build on Hercules or VGA** ("cannot
  place paint - title bar wholly buried"): Note Pad is wider there and the
  Disk window covers the rest of Paint's bar. Fixed in 888c81e9 (a buried
  bar is raised through its dock tile first; the z-order ends the same),
  cherry-picked to `kernel-hotpaths` as 763f4437.
* **deskbench's flicker spans are not timings** for the larger repaints: its
  fullscreen-exit row read 50 ms over 3 frames on CGA against the 778 ms the
  routine takes entry to return (the burst rule cuts it; its own header warns
  of exactly this). The figures above are entry to return.

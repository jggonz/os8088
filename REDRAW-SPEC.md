# os8088 redraw plan

**Standing plan for how os8088 puts pixels back on the screen.** SPEC.md is
the binding contract for what the kernel *is*; this document is the plan.
The two defects it was filed for have both landed — the contracts now live
in **SPEC.md §11.3** (the clip region) and **SPEC.md §40** (the fractal's
restore cache), which are the authority. What is left here is the record of
why each fix is shaped the way it is, and the two optimisations that were
scoped out.

Both parts were the same underlying gap: **os8088 had no way to reproduce a
window's content except by asking the application to compute it again, and
no way to draw part of a window.**

| part | defect | status |
|------|--------|--------|
| 1 | A repaint destroyed the content; the app had to recompute it | **landed** — option A, SPEC.md §40.1 |
| 2 | A window that was even 1px covered could not draw at all | **landed** — SPEC.md §11.3 |
| 1B | `FT_SYM` declared but not exploited | not started |
| 1C | Non-destructive window moves, kernel-wide | not started |

---

## Part 1 — a repaint threw the picture away (landed)

### What happened

Moving the Fractal window blanked the image and re-rendered from row 0.
Under QEMU that is ~0.5 s and reads as a flicker; on a 4.77 MHz XT a full
frame is ~115 s, and losing it is brutal.

### Why — two facts, neither of them the app's choice

1. **The kernel erases before every paint.** `wm_draw_win` fills the whole
   content area with white and *then* calls `W_PAINT`. This is
   unconditional, for every window, and there is no "do not erase me" flag.
   By the time the app is asked to paint, its pixels are already gone. This
   is still true — Part 1 did not change it.
2. **The app had nothing to restore from.** It kept no frame buffer, so the
   only way to put pixels back was to compute them again.

What was already preserved is the view state — type, centre, zoom, palette.
The user never lost their place. They lost the *work*.

### Why there is no frame buffer — measured, not assumed

The canvas is 320×170. The package pool is **19,968 bytes total, shared by
every resident package**, and `fractal`'s image already took 2,287 of it.

| storage strategy | bytes | verdict |
|---|---|---|
| raw, 4bpp packed | 27,200 | larger than the entire pool |
| full-frame RLE (measured, zoom 0, all five types) | 11,712 – 13,928 | ~70% of the pool for one instance; two instances impossible |
| **pass-0 RLE only (measured, worst case over 5 types × 5 zooms)** | **3,886** | affordable |

Measured with the reference model against the shipped Q4.12 arithmetic, cap
48, colour indices as actually emitted. Per-type worst cases for the pass-0
cache: Mandelbrot 3,508 · Dendrite 3,096 · Rabbit 3,886 · Burning Ship 3,852 ·
Tricorn 3,574. Those figures assume three bytes per run; the shipped format
packs a run into **one word** (colour in bits 15..12, last column in 11..0),
so the same worst case is ~2,600 bytes against a 4,000-byte buffer.

### Why pass-0 was the right thing to cache

`fr_advance` already renders in three progressive passes: pass 0 does rows
0, 4, 8… painted as **4-row bands**, so the full canvas is covered at
quarter vertical resolution after only 25% of the work; pass 1 fills rows 2,
6, 10… as 2-row bands; pass 2 fills the rest. The renderer also already
coalesces each row into runs before drawing them.

So caching pass 0 as runs is nearly free in code — replaying it *is* the
emit loop the app already has — and it restores the whole picture, coarsely,
in one blit.

### What shipped

**Option A, the coarse RLE restore cache.** 4,000 bytes of package bss. On
`W_PAINT` with an unchanged view, `fr_redraw` replays the cached runs
immediately and sets the worker to resume refining rather than restarting.
`fr_kick` is the single invalidation point, because every view change
already funnelled through it. The buffer is capped and a row that does not
fit is simply not cached, so a pathological view degrades to a partial
replay rather than corrupting. Contract: SPEC.md §40.1.

*Effect:* a window move looks instantaneous; the expensive 75% of the work
is what gets redone, in the background.

### Scoped out, still wanted

**B. Implement the declared-but-unused `FT_SYM`.** The design pinned
symmetry flags — x-axis mirror for Mandelbrot and Tricorn, 180° rotational
for both Julias — and shipped without exploiting them. A free 2× on four of
the five types, halving every re-render. ~40 lines, package-only. `FT_SYM`
is already in the parameter table and SPEC.md §40 records that it is
declared, not exploited.

Verify with a byte-compare harness against the reference model before
trusting it: this is the one optimisation that can silently corrupt half a
frame. Note the axis moves off the canvas centre after a click-recentre and
can leave the canvas entirely, so the mirror has to be computed from
`fr_x0`/`fr_y0`/`fr_step`, not assumed.

**C. Non-destructive window moves, kernel-wide.** Blit the window rect to
its new position and repaint only the newly exposed area, instead of
erase-and-repaint. This is the architecturally correct fix and every
application benefits, not just this one. But it is a real `wm.inc` change
with occlusion bookkeeping (you may only blit what was actually visible),
and it helps *moves* only — un-obscuring still has to repaint, so Part 1's
cache is still wanted either way.

---

## Part 2 — a covered window could not draw at all (landed)

### What happened

Covering any part of a Bounce window stopped the ball. It looked frozen. The
same was true of Clock and of Fractal, whose percentage stuck and whose
canvas went blank.

### Why

`wm_obscured` answers a **boolean**: is any visible window later in
`wm_zord` overlapping our frame rect, drop shadow included. Every background
task used it as a veto — one covered pixel and the whole frame was skipped.

It had to. The `gfx_*` primitives draw in **absolute screen coordinates with
no clipping** beyond the screen edge. A background window that drew while
covered would paint straight over the window on top of it. Skipping was the
only safe answer available.

SPEC.md §14 made this explicit for Bounce: the frame was skipped *without
erasing or stepping*, so the square stayed put until the window could be
seen again. That was a deliberate consequence, not a bug — but it was the
wrong trade once a package could own a worker and a render could take two
minutes.

### Why this is only a background-task problem

The normal repaint path does not need clipping: `wm_paint_all` draws visible
windows **back to front**, so the painter's algorithm resolves overlap for
free. Clipping is needed only for *asynchronous, single-window* drawing.
That narrowed the change considerably, and **the repaint path is still
unclipped** — SPEC.md §11.3 rule 3.

### What shipped

A visible-region rect list, `wm_clip_set` / `wm_clip_clear`, built per lock
hold, 16 rects capped, overflow degrading to "skip the frame". The hook sits
at the **public entry** of each of the six primitives, above the `[bb_on]`
dispatch, so one implementation covers VRAM, back buffer, VGA and both mono
adapters. `gfx_unlock` clears the clip. Full contract: SPEC.md §11.3.

Three details worth keeping in mind, because none of them is obvious from
the plan as originally written:

- **`gfx_xor_rect` could not be clipped by intersection.** An outline is not
  the intersection of its bounding rect with anything — clipping it that way
  draws edges through the middle of the shape. It decomposes into four
  `gfx_xor_fill` strips first.
- **`font_char` and `icon_draw16` clip whole-cell, not per-pixel.** Neither
  can draw a partial shape, and both already skip a glyph that would cross a
  *screen* edge; a clip edge is the same decision one region in.
- **Which means fills and glyphs do not clip alike**, and anything that
  erases a rect and then draws text into it has to reconcile them or it goes
  *blank* rather than stale. A third slot, `wm_clip_test` (0x00C8), is what
  lets a caller ask the glyphs' question first: the Clock erases per cell
  behind it, the fractal's status strip gates the whole strip on it. This
  was not in the plan and is the one thing the plan would have got wrong;
  SPEC.md 11.3 calls it the granularity rule.

### Hazards, resolved

- **Region blow-up.** Naive repeated subtraction is O(4ⁿ). Resolved by the
  16-rect cap with documented degradation rather than a band merge; four
  Disk windows plus a full dock stays well inside it.
- **Cost per frame.** The region build *is* `wm_obscured`'s z-order walk plus
  a subtraction that does nothing when nothing overlaps, so there is no
  `wm_obscured` fast path and no `wm_zord` generation counter — a pre-test
  would only walk the list twice.
- **Back-buffer dirty rect.** Clipped drawing produces several sub-rects.
  `bb_dirty` only ever widens the bounding box and is reset in exactly two
  places, so N sub-rects accumulate into one box and one flush pushes it.
  Nothing needed changing; SPEC.md §32 records why.
- **Menu bar and dock.** Windows clamp to `y >= MBAR_H` except under
  `WF_FULL`, which owns the bar legitimately, and windows cover the dock by
  design (`wm_paint_all` paints the dock first, then windows). Neither needs
  subtracting.
- **Fullscreen windows.** `wm_clip_set` seeds from `wm_content`'s rule: under
  `WF_FULL` the frame *is* the content.
- **Kernel budget.** Estimated ~650 bytes of code and 128 bytes of `.bss`;
  actual cost of both parts together is **914 bytes** of `.text` (the
  binding guard is assertion 2, `KTEXT_SIZE + KFAR_SIZE > KERN_MAX`),
  taking `build/kernel.bin` from 40,825 to 41,739 against the 45,056 limit —
  3,317 bytes of slack left. The overrun over the estimate is the
  `gfx_xor_rect` decomposition, three API slots rather than two, the icon
  hook and the Clock's per-cell erase.

---

## How the two parts interact

They are complementary, not alternatives. Part 2 lets a partly covered
window keep drawing; it does **not** help the move case, because uncovering
still goes through `wm_paint_all`, which white-fills and calls `W_PAINT`.
Part 1's cache is what makes that cheap. Landing Part 2 alone would have
left the fractal restarting on every move; Part 1 alone would have left it
blank whenever a corner was covered.

## Reference

- The clip region: SPEC.md §11.3; `wm_clip_set` and friends in `kernel/wm.inc`
- The hook: the `GFXCLIP` macro and `gfx_clip_run` in `kernel/vga12.inc`;
  `wm_clip_test` for `font_char` (`kernel/font.inc`) and `ico_core`
  (`kernel/icons.inc`)
- Erase-before-paint: `wm_draw_win`'s `.content` label, and the back-to-front
  repaint in `wm_paint_all`
- The restore cache: SPEC.md §40.1; `fr_cache_row` / `fr_replay` /
  `fr_redraw` in `apps/fractal/fractal.asm`
- Package worker task contract: SPEC.md §20.6
- Background-task draw discipline: SPEC.md §7, §14

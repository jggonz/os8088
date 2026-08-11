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
| 3 | A window in the background is redrawn whole for a sliver | **part landed** — see Part 3 below |
| 1B | `FT_SYM` declared but not exploited | not started |
| 1C | Non-destructive window moves, kernel-wide | superseded by Part 3 |

---

## Part 1 — a repaint threw the picture away (landed)

### What happened

Moving the Fractal window blanked the image and re-rendered from row 0.
Under QEMU that is ~0.5 s of visible redraw; on a 4.77 MHz XT a full frame
is **~115 s**, and losing it is brutal. (It is a redraw, not a flicker —
PERFORMANCE.md §1 on why the distinction is worth keeping.)

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
same was true of the Timer and of Fractal, whose percentage stuck and whose
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
at the **public entry** of each of the six primitives, above the `[vid_mono]`
dispatch, so one implementation covers VRAM, VGA and both mono adapters. `gfx_unlock` clears the clip. Full contract: SPEC.md §11.3.

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
  lets a caller ask the glyphs' question first: the Timer erases per cell
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
- **Back-buffer dirty rect.** Clipped drawing produces several sub-rects, and
  `bb_dirty` only ever widened the bounding box, so N sub-rects accumulated
  into one box and one flush pushed it — nothing needed changing. Moot since
  SPEC.md §32's back buffer was removed; kept because it is the shape a
  future accumulating damage rect would take.
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
  hook and the Timer's per-cell erase.

---

## Part 3 — a background window is redrawn whole to put a sliver back

### Where this came from

Reported from the field as *"File Manager redraws continually when you take
actions on windows in front of it"*, and isolated by the reporter with the
comparison that settles it: two windows side by side, drag the right one left
over the left one. If the left one is a Note Pad it does not redraw; if it is
a Disk window it repaints in full.

### What has landed

| | what | SPEC | measured |
|---|---|---|---|
| a | a Disk-window menu command that changes nothing draws nothing | §22.13 | `Builtins > Timer` 1 → 0 `fm_repaint` |
| b | `fm_status_only` puts the grow box back | §22.13 | 28 px, wrong since §22.9 |
| c | the Disk window joins the raise cache | §22.14 | `SU_MISS` → `SU_HIT`, no `W_PAINT` |
| d | the grow box comes off a window that loses the front | §11.1.1 | was permanent once (c) stopped repainting |
| e | dragging across a window marks it on the rect the mover LEFT | §11.91.2 | 4 windows → 1 on 3 of 5 drags |
| f | the cache is taken wherever a window is drawn, not only at a raise | §11.96.4 | drags 2-5 `SU_HIT` instead of full repaints |
| g | minimizing banks instead of dropping | §11.96.5 | restore-from-dock is a blit |
| h | the vacated test is `old` minus the mover's new FRAME | §11.91.2 | see the table below |
| i | **`gfx_restore` can put back part of its buffer** | §5.8 | the primitive; skips 0 at rest |
| j | **the raise cache restores only what the pass painted** | §11.96.6 | 30.63 → 15.27 ms, 2.01x |
| k | **a bank is only worth what was on the glass** | §11.96.7 | a real bug: 3,876 stale px |
| l | **the edge merge is bounded by the same rect** | §11.96.8 | 18.59 → 8.09 ms, 2.30x |
| m | **a RAISE puts back only what was covered** | §11.96.10 | Paint 9,090 → 5,948 ms, 1.53x |

(i) and (j) are the primitive and its first consumer, below. **(k) was not on
anyone's list**: verifying (j) against `REDRAWFULL=1` turned up 7,907 differing
pixels that were **already there** — the pristine tree against its own
reference — and the cause was `wm_raise` banking the outgoing front window's
cache *after* `wm_dock_under` had drawn the incoming window over it. The cache
then held the covering window's picture, and it surfaced one operation later as
a ghost window inside another. A wrong cache is invisible until it is used,
which is why nothing near the ordering ever looked wrong; SPEC.md §11.96.7 has
it. **The moral for the next round of this work is that the 0-differing-pixels
standard is worth running even when you are confident**, and worth running
*before* you write anything — a pre-existing failure otherwise arrives looking
exactly like your own.

### The primitive — BUILT for `gfx_restore`, still owed for `gfx_blit4`

**A blit that can draw a sub-rect of its source.** `gfx_restore` and
`gfx_blit4` were both off §11.3's clipped list for the same stated reason —
*"a blit cannot take a sub-rect without advancing its source to match"* — and
that one sentence was the whole of what stood between here and two separate
wins:

- **the raise cache putting back only the part that was uncovered**, instead
  of the whole window (`gfx_restore`) — **landed, SPEC.md §5.8/§11.96.6**;
- **Paint repainting only the uncovered part of its canvas** (`gfx_blit4`) —
  **there is no primitive to build here at all.** See "Consumer 2" below.

**"Build it once and both follow" was wrong, and the reason decides where the
rest of the work is.** What makes a sub-rect impossible for a caller is not the
blit — it is **who owns the source layout**. `gfx_save`'s buffer is the kernel's
private business, plane-major at a stride the kernel computes, so a caller
cannot address into it and the kernel had to grow §5.8's three words.
`gfx_blit4`'s source is the caller's own pointer and its own `BP` stride, so a
sub-rect is already expressible by advancing the pointer and passing a smaller
`CX`/`DX` — and `pt_blit` has taken an arbitrary canvas rect all along, rounding
its left edge to an even pixel because two pixels share a byte. Being off
§11.3's clipped list means *the kernel* will not clip a blit; it never meant the
caller could not ask for one.

**And the arithmetic was never the hard part of either.** What cost this round
its time was (a) the register discipline at the call site — the arming call
takes a rect in `AX/BX/CX/DX` and `BX` is the window pointer `wm_draw_win` is
about to be handed, which assembles, boots, and hard-freezes the machine inside
a garbage `W_PAINT` — and (b) the one-shot's *disarm site*: it is cleared by
the restore bodies rather than by the caller, so a forgetful caller inherits
"whole", and in `sw_xfer` that clear has to be **gated on `[sw_dir]`** because
`.done` is the *save* path's exit too and `wm_su_edge` is two `gfx_save` calls
between the arm and the restore.

#### The buffer layout it has to walk

`gfx_save` writes **all plane-0 rows, then plane 1, 2, 3**; each row is
`(x2/8) - (x1/8) + 1` bytes, rows top to bottom, `x1` rounded down and `x2`
up. So for a saved rect `B` and a sub-rect `S` inside it:

```
full_row = (B.x2/8) - (B.x1/8) + 1        ; bytes per saved row
full_rows = B.y2 - B.y1 + 1
sub_row  = (S.x2/8) - (S.x1/8) + 1        ; bytes to move per row
start    = (S.y1 - B.y1) * full_row + (S.x1/8 - B.x1/8)
row_skip   = full_row - sub_row           ; step the BUFFER past each row
plane_skip = (full_rows - sub_rows) * full_row
```

Both skips are **0 today**, which is what makes this additive: the cursor's
own path (`vga_save_vram` / `vga_restore_vram` called directly from IRQ4)
is byte-identical with them zeroed.

#### The two insertion points

- **`sw_xfer`** (`kernel/softgfx.inc`), the 1bpp body, restore branch
  `.rst`/`.rrow`: `add si, [cs:sw_bskip]` after the `rep movsb`, and the
  plane skip after `pop di`. **`cs:` is mandatory** — DS is the buffer inside
  that loop, which is why the existing code already reads `[cs:vid_rowadd]`.
- **`vga_restore_vram`** (`kernel/vga12.inc`), `.row`: the same two adds. DS
  is the buffer there too.

`vga_rect_setup` recomputes the SCREEN geometry from whatever rect it is
handed, so the screen side needs nothing: pass the sub-rect and it lands in
the right place.

#### The trap that is already paid for once — byte columns

A sub-rect's left and right **byte columns** contain pixels outside the
sub-rect. Restoring them writes the cache's version of pixels that may have
changed. This is the same hazard §11.96.2 solved for the whole-window case,
and the same answer works: `wm_su_edge` reads those two columns off the
screen **as they are now** and patches the bits outside the rect into the
buffer before the restore. Reuse it; do not invent a masked write, for
§11.96.2's reason (it would slow the cursor's primitive for one caller).

### Then the two consumers

#### Consumer 1 — the raise cache restores only the uncovered part (LANDED)

The plan said to hand `wm_dmg_wins`'s uncovered rect — `(W ∩ old) \ new_frame`
— to `wm_su_try` as a one-shot. **What shipped is a rect that ACCUMULATES, and
the difference is not a refinement, it is the correctness argument.**

The plan's own next paragraph is why: `wm_draw_win` drawing a window whole is
what makes it safe to paint over the window above it, so restoring only a strip
needs the strip to cover everything that really did paint over this window. The
vacated rect is not that. **The chrome is the reason** — `wm_draw_win` writes
the outline, the drop shadow and the title bar *whether its cache hit or not*,
so a window below yours takes your pixels away every time it is drawn, wherever
its frame reaches you. So `wm_dmg_x1..y2` means *what this pass has painted*
from `.draw` onward: seeded with the damage rect (already covering the dither
and every touched drive zone), grown by each window's frame as it is drawn.
Marking is bottom-to-top and drawing is back-to-front, so one pass in that
order sees every contribution before it can matter.

This is the **safe intermediate** the plan named, and it is safe for a reason
worth stating: the marking is untouched, so every window that was marked is
still marked and still drawn — only how much of itself it puts back changed.
The bottom-most drawn window gains most, and **that is the majority case rather
than a corner**: after §11.91.2, three of five measured drags mark exactly one
window.

Two things a reader will want that the plan did not anticipate:

- **The dock strip is per WINDOW, not a term in the shared rect** — see Set 30.
  Folded in, it made every window's owed rect full width because the strip is.
- **An empty intersection is the best answer available**, not a failure: nothing
  this pass painted reached the content, so the content is already right and no
  pixel is written at all.

**A raise still restores the whole content**, and deliberately: `wm_raise` has
no damage rect, and what a raised window owes is the part that was *covered*,
which is the complement of §11.3's visible region rather than anything this
pass computed. That is a separate consumer and it is not built.

**It is built now — SPEC.md §11.96.10**, and it turned out to need no region
arithmetic at all: the *bounding box* of the covered part is the union of the
windows above us intersected with our content, which is one walk of `wm_zord`
(`wm_cov_rect`) rather than a complement of `wm_clip_tab`. It is asked before
`wm_lift` and spent into `wm_su_sub` immediately before `wm_draw_win`, because
everything in between — the bar, the dock, the outgoing window's chrome — can
itself reach `wm_draw_win` and would consume a one-shot armed any earlier. Both
consumers took it unchanged, which is what (j) and §11.90.2 were built for.

#### Consumer 2 — Paint, which cannot use the cache and should not

Paint holds a canvas, an undo image and a clipboard. A raise cache would be a
fourth copy of pixels it already has, and on the machine this is written for
that displaces things that matter more. §11.96.1's question 3 asked "is its
repaint expensive?" and answered "no, it is one `gfx_blit4`" — the better
question is **who already holds the pixels**:

> If only the kernel can hold them, that is `WF_SAVEU`. If the application
> already holds them, it does not need a cache — it needs to be told **which
> part to put back**.

Two answers to one question, picked by ownership. Paint is the second answer
and it is the most expensive repaint in the system.

**And §11.3's granularity rule does not bite here**, which is why this is not
the general "clip every `W_PAINT`" work: the rule is about a *fill and a
glyph* disagreeing, and 95% of Paint's repaint is a **blit**, which clips per
pixel like a fill. Its chrome — tool palette, colour strip, W/H boxes, Apply
— is fills and glyphs, but each element is small and self-contained, so it
takes `tm_row_draw`'s answer: redraw whole elements the region touches.

What is missing on the app side is an **API**: a package can ask
`OSAPI_WM_OBSCURED` (a boolean) and `wm_clip_test` (does a rect cross a clip
edge). Neither says *"here is the rect you owe"*.

#### What consumer 2 is actually blocked on, measured

**`wm_draw_win` white-fills the whole content before every `W_PAINT`** — Part
1's point 1, still true, no opt-out flag, and `apps/paint`'s own `pt_fsbed`
comment documents it. So a slot that tells Paint which rect it owes achieves
**nothing** until the kernel stops erasing the rest, and that is a change to the
kernel's erase contract rather than an optimisation: every application depends
on the fill.

**A BLANK CANVAS IS THE VERY CHEAP CASE, and the first version of this section
priced only that** — PERFORMANCE.md Set 32 is the correction. Measured on a
cycle-accurate 5150/CGA, Paint raised from behind a Disk window:

| | blank canvas | a textured 492×133 BMP |
|---|---|---|
| kernel white fill + palette + strip + divider | ~376 | ~376 |
| the canvas blit (one `gfx_blit4`) | ~211 | **~8,670** |
| **one Paint repaint** | ~602 ms | **~9,060 ms** |

**Nine seconds**, and the blit is linear in the *runs* it covers, not the pixels
(`pt_blit`'s own note: a `gfx_hline` per run at ~756 µs of arriving each). So the
canvas is not "a third of the problem" as this section first said — at 85 runs a
row it is 96% of it, and an ordinary drawing at ~30 runs a row lands near the
three seconds the field reports.

**That kills the ordering this section first proposed.** `WF_SAVEU` on Paint
*would* be enormously faster — a restore is a flat `rep movsb` at ~5.5 µs a byte
against the blit's ~244, so ~50 ms on 1bpp against 8,670 — but it is the wrong
lever anyway, on memory:

- Paint already holds canvas + undo + clipboard, **~127 KB** at stock size.
- A cache over its content is ~9 KB on 1bpp, **~36 KB on VGA**, ~150 KB for a
  window grown over most of a 640×480 screen.
- On a **256 KB machine** the heap is ~160 KB after the kernel, so the VGA figure
  cannot exist and the 1bpp one only just can. Purgeability (§50.6) makes that a
  refusal rather than damage — which is to say the feature simply is not there on
  the machines this project is calibrated against.

**Drawing less of the canvas beats caching it, and it composes.** A repaint that
owes 10% of the canvas costs ~10% of the blit — about 0.9 s against 8.7 — at **no
memory cost at all**, on every adapter and every machine size, and it stacks with
a cache wherever a cache fits. So the erase contract is not the expensive
prerequisite to be avoided; it is the thing worth doing first.

**Read "owes 10%" literally, because every later paraphrase of it here got it
backwards.** What a raised window owes is the part that was *covered*, so the
0.9 s case is a canvas 10% covered — 90% visible — and the "only 10% of the
canvas was visible" the sections below quote is the *opposite* case and worth
7.8 s. Measured, PERFORMANCE.md Set 39: 59.9% covered is 1.53x, and there is no
single number for it because there is no typical amount of covering.

Two smaller things stand unchanged, and one is now the *shape* of the answer
rather than an alternative to it:

1. **The white fill must become opt-out.** `pt_repaint`'s own comment is *"Every
   part of this draws its own background, so no white fill is needed first"* — so
   for Paint the kernel's fill is waste today, before any of the above.
2. **The chrome is small in AREA and dear in TIME** — that ~376 ms of palette,
   strip and divider is a narrow left-hand column, about **1 KB** of cache on
   1bpp. It is the part a cache holds cheaply, exactly where the canvas is the
   part it cannot.

#### The design that follows: registered cache regions, and exclusions

Which points at one mechanism rather than two competing ones. A window registers
**regions it hands to the cache** and **regions it keeps**; a repaint then
restores the cached regions that the damage touches, and calls `W_PAINT` with the
damage rects that fall in the kept ones. For Paint that is exactly the right
split — cache the chrome (~1 KB, kills ~376 ms), keep the canvas (no memory, and
the app blits only the rect it is handed) — and it makes the white fill the **last
resort** it should be rather than the default: a region that is neither cached
nor claimed by the app is the only thing that needs blanking, and blanking is
what a repaint should be seen to do least of.

Three things from consumer 1 carry straight into it, and one warning:

- `wm_dmg_x1..y2` from `.draw` onward already **is** the damage, accumulated
  (§11.96.6), and `wm_su_owed` already makes it a per-window answer.
- A per-window rect **list** is what `wm_clip_tab` already is (§11.3, 16 rects
  with a documented overflow degradation), so the shape exists.
- The one-shot discipline (§5.8) is what keeps it safe: armed immediately before
  the draw, disarmed by whatever consumes it.
- **The warning is §11.3's granularity rule.** Handing an app a damage rect and
  no clip leaves it free to draw outside it; handing it a clip as well brings back
  the fill-clips-per-pixel / glyph-clips-per-cell trap for every app that adopts
  it. Paint is the easy consumer because 96% of its repaint is a blit; the next
  one will not be.

**One thing to look at while in there**, found by the measurement and not chased:
that raise produced **two** `wm_grow_paint` calls for Paint's window (374 ms
then 211 ms), which reads as its `W_PAINT` running twice. `[pt_apend]`'s
deferred-resize path calls `OSAPI_WM_FRONT` from inside the paint, which would
do exactly that.

**Three things consumer 1 settled that a rect-passing slot can be written
against**, whenever the erase contract is faced:

1. **The rect exists and is already computed.** `wm_dmg_x1..y2` from `.draw`
   onward *is* "what this pass painted", accumulated (§11.96.6), and
   `wm_su_owed` already turns it into a per-window answer. A slot that hands a
   package `wm_su_owed`'s output intersected with its content rect is the whole
   API — no new bookkeeping, and it answers 0 rects for a window nothing
   touched, which is the case worth having.
2. **It must be a `W_PAINT`-time question, not a callable one.** A package that
   asks "what do I owe" outside its paint has no answer that stays true, and
   the one-shot discipline is what makes the kernel's own use safe: armed
   immediately before the draw, disarmed by the thing that consumes it. The
   natural shape is therefore a slot a paint proc may call *while it is being
   painted*, refusing otherwise — not a flag in the window record.
3. **A package must be able to decline.** `wm_su_srect`'s fallback is "restore
   the whole buffer, always correct and only dearer"; the app-side equivalent
   is that ignoring the answer and repainting whole stays correct, so the slot
   can ship before any app uses it and Paint can adopt it alone.

### Measured, so the next session does not have to re-derive it

A busy VGA desktop — a Disk window, Piano, Hello and Note Pad, all
overlapping — driven by reading the window table and z-order out of the guest
and computing what each drag genuinely uncovered:

| drag | windows redrawn (before → after (h)) | uncovered |
|---|---|---|
| −60,+20 | 4 → 3 | 1,220 px of 126,502 (1.0%) |
| +50,−15 | 4 → 4 | 6,050 px (4.8%) |
| −40,−25 | **4 → 1** | 0 px |
| +70,+30 | 4 → 4 | 11,190 px (8.8%) |
| −90,0 | **4 → 1** | 0 px |

So after (h), a window is redrawn only when something of it really was
uncovered — and when it is, **between 91% and 99% of what is drawn was not
uncovered.** That is the whole of what the sub-rect blit is for.

Raising a covered Disk window on a cycle-accurate 5150/CGA, by
PERFORMANCE.md Part 3.1's instrument: **644 ms without the raise cache, 215
ms with it.** The residual is chrome plus a whole-content restore; on CGA the
content is ~5.7KB of `rep movsb`, on VGA four planes of it. **Do not spend
the sub-rect work on the strength of the 644 → 215 figure** — that is the
cache's win and it is already banked. Measure the restore itself first; an
attempt via `flicker` could not attribute it (a raise also repaints the bar,
the dock and the outgoing title bar), and a breakpoint-and-step attempt was
too slow through the debug server. A breakpoint at `gfx_restore` and another
at `wm_grow_paint`, reading `cycles` at each, is the measurement that was
being set up when the session ended.

**That measurement was then taken, and PERFORMANCE.md Sets 30 and 31 are it**:
on a 318×136 Disk window the restore is **47.86 ms** — 29.64 of blit and 18.22
of `wm_su_edge`'s merge — against 48.84 ms for all the chrome around it. So the
instruction above was right to insist on it, and the answer justified both
halves of the work. On one drag that genuinely uncovers, priced end to end:

| | edge | blit | restore |
|---|---|---|---|
| before | 18.59 | 30.63 | 49.22 ms |
| §11.96.6 — the sub-rect blit | 18.59 | 15.27 | 33.86 ms |
| §11.96.8 — the bounded merge | **8.09** | 15.27 | **23.36 ms** |

**2.11x on the restore.** The edge half beat the row-count ratio (2.30x against
the 1.45x that 136 → 94 rows predicts) because bounding it also means **an edge
column the restore does not reach is not merged at all** — a sub-rect in the
middle of a window does no edge work whatever its height.

**What is left of the restore is the blit again**, and there is no obvious next
cut in it: `rep movsb` over exactly the bytes that changed, at a per-row overhead
the two renderers share with the cursor.

### How to verify any of it

`tools/subcheck.py` is that, as one command — `capture` a session, `capture` it
again against the other build, `diff`. `make REDRAWFULL=1` is the reference: it
puts the Disk window off the raise cache, `wm_dmg_stale` back on the whole
damage rect, `wm_su_bank` back to one-shot, `wm_hide` back to dropping,
`fm_docmd` back to a whole-window repaint, and §11.96.6's arming off. **0
differing pixels is the standard**, on VGA mode 12h, CGA and Hercules — and it
is met, over 11 steps covering three drags, two raises, a minimize, a restore
from the dock and a close.

**Two traps in the harness cost this round a run each, and both are silent.**
`os88mouse`'s `click` and `drag` send their button packets without proving them,
and the 1200-baud UART drops one clocked in while the previous is in flight — a
dropped *release* leaves the button down, so the next press is no edge at all
and **every step after it quietly does nothing** while each call still succeeds.
Measured: a session whose last seven steps left the window table completely
unchanged. `_edge` is the proving primitive; `subcheck`'s `pclick`/`pdrag` use
it. And **compare the window GEOMETRY before comparing pixels**: two runs that
put the windows in different places are not the same session, and the pixel diff
blames the kernel for it — thousands of differing pixels in a window-shaped
region, which is exactly what a real defect looks like.

Four more things cost a run each and are worth reading before the first one:

- **No animating window in the session.** A Timer's digits and a Fractal's
  render differ between two passes and read as a redraw defect. Use
  `Builtins > Disk`, which exercises the same paths and stands still.
- **Mask the mouse arrow and the menu-bar clock**, or a run a minute apart
  reports a defect at the clock cell. The bar is a row taller on some
  builds — mask `y < 18` rather than a tighter bound.
- **Rebuild the reference** after any change to a path the session touches.
  A stale reference reports the difference between yesterday's kernel and
  today's.
- **Read the window rects out of the guest** rather than eyeballing
  coordinates from a screenshot: `wm_wins` (stride `WIN_SIZE` = 26,
  `W_FLAGS`/`W_X`/`W_Y`/`W_W`/`W_H` at 0/2/4/6/8), `wm_zord` and `wm_zn`.
  Half the harness time in this round went on clicks that missed.

### Budget

The image rung has **167 bytes** of slack and the cold rung **86**
(`kern_big`, after (h)). The next `.text` byte anywhere costs a 512-byte
step, so the sub-rect work should expect to ask for one.

---

## How Parts 1 and 2 interact

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

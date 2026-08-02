# os8088 redraw plan

**Standing plan for two related defects in how os8088 puts pixels back on the
screen.** SPEC.md is the binding contract for what the kernel *is*; this
document is the plan for fixing what it currently does badly, why each fix is
shaped the way it is, and what was measured rather than guessed. Neither part
has landed. Read this before touching `wm.inc`, `vga12.inc` or a background
task's draw loop.

The two parts are independent and can land in either order. They are filed
together because they are the same underlying gap: **os8088 has no way to
reproduce a window's content except by asking the application to compute it
again, and no way to draw part of a window.**

| part | defect | blast radius |
|------|--------|--------------|
| 1 | A repaint destroys content; the app must recompute it from scratch | Fractal (severe), any future compute-heavy app |
| 2 | A window that is even 1px covered cannot draw at all | Bounce, Clock, Fractal — every background task |

---

## Part 1 — a repaint throws the picture away

### What happens

Move the Fractal window and the image blanks and re-renders from row 0. Under
QEMU that is ~0.5 s and reads as a flicker; on a 4.77 MHz XT a full frame is
~115 s, and losing it is brutal.

### Why — two facts, neither of them the app's choice

1. **The kernel erases before every paint.** `kernel/wm.inc:909` fills the
   whole content area with white and *then* calls `W_PAINT`. This is
   unconditional, for every window, and there is no "do not erase me" flag.
   By the time the app is asked to paint, its pixels are already gone.
2. **The app has nothing to restore from.** It keeps no frame buffer, so the
   only way to put pixels back is to compute them again.

What *is* preserved is the view state — type, centre, zoom, palette.
`fr_paint` calls `fr_kick`, which resets only `fr_pass` / `fr_row` /
`fr_prog`; `fr_setup` recomputes the step from the centre and zoom already
held. The user does not lose their place. They lose the *work*.

### Why there is no frame buffer — measured, not assumed

The canvas is 320×170. The package pool is **19,968 bytes total, shared by
every resident package**, and `fractal`'s image already takes 2,287 of it.

| storage strategy | bytes | verdict |
|---|---|---|
| raw, 4bpp packed | 27,200 | larger than the entire pool |
| full-frame RLE (measured, zoom 0, all five types) | 11,712 – 13,928 | ~70% of the pool for one instance; two instances impossible |
| **pass-0 RLE only (measured, worst case over 5 types × 5 zooms)** | **3,886** | affordable |

Measured with the reference model against the shipped Q4.12 arithmetic, cap
48, colour indices as actually emitted. Per-type worst cases for the pass-0
cache: Mandelbrot 3,508 · Dendrite 3,096 · Rabbit 3,886 · Burning Ship 3,852 ·
Tricorn 3,574.

### Why pass-0 is the right thing to cache

`fr_advance` (`apps/fractal/fractal.asm:718`) already renders in three
progressive passes: pass 0 does rows 0, 4, 8… painted as **4-row bands**, so
the full canvas is covered at quarter vertical resolution after only 25% of
the work; pass 1 fills rows 2, 6, 10… as 2-row bands; pass 2 fills the rest.
The renderer also already coalesces each row into runs before drawing them.

So caching pass 0 as runs is nearly free in code — replaying it *is* the emit
loop the app already has — and it restores the whole picture, coarsely, in one
blit.

### Options, cheapest first

**A. Coarse RLE restore cache (recommended).** ~4KB of package bss. On
`W_PAINT` with an unchanged view, replay the cached runs immediately, then set
`fr_pass = 1` so the worker resumes refining rather than restarting. Invalidate
the cache on any view change (type, palette, centre, zoom) — those already
funnel through `fr_kick`, which is the single invalidation point. Cap the
buffer and fall back to a full restart on overflow, so a pathological view
degrades to today's behaviour rather than corrupting.

*Effect:* a window move looks instantaneous; the expensive 75% of the work is
what gets redone, in the background, on rows the user is not staring at.

**B. Implement the declared-but-unused `FT_SYM`.** The design pinned symmetry
flags — x-axis mirror for Mandelbrot and Tricorn, 180° rotational for both
Julias — and shipped without exploiting them. A free 2× on four of the five
types, halving every re-render including the one after a move. ~40 lines,
package-only. `FT_SYM` is already in the parameter table, documented as
*declared, not yet exploited*.

Verify with a byte-compare harness against the reference model before trusting
it: this is the one optimisation that can silently corrupt half a frame.

**C. Non-destructive window moves, kernel-wide.** Blit the window rect to its
new position and repaint only the newly exposed area, instead of erase-and-
repaint. This is the architecturally correct fix and every application
benefits, not just this one. But it is a real `wm.inc` change with occlusion
bookkeeping (you may only blit what was actually visible), and it helps *moves*
only — un-obscuring still has to repaint, so Part 1's cache is still wanted.

### Acceptance criteria

- Drag the Fractal window mid-render: the coarse image is back within one
  frame of the drop, and the percentage resumes from where it was rather than 0%.
- Change fractal type: the cache is invalidated and the render restarts at 0%.
- Two instances open: both still fit the package pool alongside `mines` and
  `notepad` (check `ld_alloc` does not refuse the fourth package).
- `python3 tools/os88disk.py --verify build/apps.img` passes; the image size
  is reported and compared against the 19,968-byte pool.

---

## Part 2 — a covered window cannot draw at all

### What happens

Cover any part of a Bounce window and the ball stops. It looks frozen. The
same is true of Clock and of Fractal, whose percentage sticks and whose canvas
goes blank.

### Why

`wm_obscured` (`kernel/wm.inc:624`) answers a **boolean**: is any visible
window later in `wm_zord` overlapping our frame rect, drop shadow included.
Every background task uses it as a veto:

```
gfx_lock
  test word [bx+W_FLAGS], 2     ; still visible?
  jz .skip
  call wm_obscured
  jc .skip                      ; ← one covered pixel and the whole frame is skipped
  ... draw ...
gfx_unlock
```

It has to, today. The `gfx_*` primitives draw in **absolute screen
coordinates with no clipping** beyond the screen edge — there is no clip
region anywhere in the tree (`grep -rn clip kernel/*.inc` finds only screen-
edge clamps in `font.inc` and `icons.inc`). A background window that drew
while covered would paint straight over the window on top of it. Skipping is
the only safe answer available.

SPEC.md §14 makes this explicit for Bounce: the frame is skipped *without
erasing or stepping*, so the square stays put until the window can be seen
again. That is a deliberate consequence, not a bug — but it is the wrong
trade now that a package can own a worker and a render can take two minutes.

### Why this is only a background-task problem

The normal repaint path does not need clipping: `wm_paint_all` draws visible
windows **back to front**, so the painter's algorithm resolves overlap for
free. Clipping is needed only for *asynchronous, single-window* drawing —
which, before worker tasks, meant only Clock and Bounce.

This narrows the change considerably. **Do not clip the repaint path.**

### The design: a visible-region rect list, set per lock-hold

Replace the boolean veto with a region:

```
wm_clip_set    in BX = window ptr; caller HOLDS the gfx lock
               builds the window's visible region into the clip list
               out CF = 1 the window is entirely invisible (skip the frame)
                   CF = 0 clipping is armed

wm_clip_clear  disarm
```

**Building the region.** Start with the window's content rect. For every
visible window later in `wm_zord`, subtract its frame rect **including the 1px
drop shadow** — occupied extent is (x, y) to (x+w, y+h) inclusive, per
`wm_draw_win`'s shadow at (+1,+1) and `wm_obscured`'s own comment. Each
subtraction replaces a rect with up to four fragments (above, below, left,
right of the occluder).

**Storage.** `MAX_WIN` is 12, so at most 11 occluders. Cap the list at 16
rects, 8 bytes each — 128 bytes of `.bss`. On overflow, degrade to CF = 1
("skip this frame"), which is exactly today's behaviour and therefore cannot
regress anything.

**Validity.** The region is computed from `wm_zord` and the window rects,
both of which the UI task mutates only under the gfx lock. Within one lock
hold they cannot change, so a region built at `wm_clip_set` is valid until
`gfx_unlock`. This is the same argument that makes `wm_obscured` trustworthy
today, and it is the reason the region must be rebuilt on every lock-hold
rather than cached across one.

### Where the hook goes — the load-bearing detail

`gfx_pixel`, `gfx_hline`, `gfx_vline` and `gfx_frame` **all funnel into
`gfx_fill`** (verified: `vga12.inc:175`, `:195`, `:281`, `:374` — a pixel is a
1×1 rect, an hline a 1-row rect, a frame two hlines and two vlines). So the
whole rectangle vocabulary is one choke point. The full set is six:

| entry | note |
|---|---|
| `gfx_fill` | covers pixel, hline, vline, frame, fill |
| `gfx_fill_gray` | |
| `gfx_xor_rect` | |
| `gfx_xor_fill` | |
| `font_char` | covers `font_str` |
| `icon_draw16` | |

Every one of these begins with a `cmp byte [bb_on], 0` dispatch to its `bb_*`
twin. **Put the clip hook above that line**, at the public entry, and one
implementation covers the VRAM path, the back-buffer path, VGA and both mono
adapters — because on mono the software renderer *is* the direct path
(SPEC.md §39.5). This is the same reasoning that puts `bb_mono_chk` where it
is, and getting it wrong means a clip that works on VGA and silently does
nothing on Hercules.

The hook itself: when the clip is disarmed, fall through unchanged (zero cost
on the repaint path). When armed, iterate the clip list, intersect the
primitive's rect with each fragment, and re-enter the body per surviving
fragment.

### Rules that must be written into SPEC.md

1. **`gfx_unlock` clears the clip.** Making the clip state die with the lock
   hold is what stops a leaked region from silently truncating the next
   painter. Do not rely on callers to clear it.
2. **Transient overlays are never clipped.** The drag outline and the menu
   highlights call `vga_xor_rect_vram` / `vga_xor_fill_vram` direct,
   deliberately bypassing the back buffer (SPEC.md §32). They are drawn by the
   UI task in a different lock hold and must stay unclipped. Rule 1 makes this
   structural rather than a convention.
3. **Clipping is for background tasks only.** `wm_paint_all` is back-to-front
   and must stay unclipped.
4. **`wm_obscured` stays**, demoted to a fast-path hint: fully-visible is the
   common case and skips the region build entirely.

### What changes in the applications

- **Bounce** (`app_bounce_task`): replace the `wm_obscured` veto with
  `wm_clip_set`, and **always step**, even when fully covered. Stepping while
  invisible is safe because `app_bounce_paint` draws the ball from state, so
  the repaint on uncover shows it at its current position. SPEC.md §14's
  "skip without stepping" rule is retired and must be rewritten.
- **Clock** (`app_clock_task`): same substitution. Time-keeping already runs
  every iteration regardless.
- **Fractal**: `fr_emit_body`'s visibility re-check becomes a `wm_clip_set`,
  so a partly covered fractal keeps rendering the part you can see.

### Hazards to work through before implementing

- **Region blow-up.** Naive repeated subtraction is O(4ⁿ). Use a band-based
  merge or accept the 16-rect cap with the documented degradation. Measure
  the worst realistic case: 4 Disk windows plus a dock full of apps.
- **Cost per frame.** Bounce re-arms the region every 2 ticks, ×10 instances.
  Budget the region build; if it is too expensive, cache it against a
  `wm_zord` generation counter bumped by `wm_front` / `wm_show` / `wm_hide` /
  `wm_destroy` / the drag drop.
- **Back-buffer dirty rect.** Clipped drawing produces several sub-rects; each
  must dirty the buffer. Check `bb_*` accumulate correctly rather than
  assuming one rect per call.
- **Menu bar and dock.** Confirm a window can never overlap rows 0..19 or the
  dock strip; if it can, the region must subtract them too.
- **Fullscreen windows** (`WF_FULL`): no border, no title bar — check
  `wm_content` interaction.
- **Kernel budget.** Estimate ~650 bytes of code and 128 bytes of `.bss`.
  The **binding** guard is assertion 2 (`KTEXT_SIZE + KFAR_SIZE >
  APP_LOAD_OFF`), not assertion 1: the `.fartext` blob rides at the tail of
  the kernel image, so `build/kernel.bin` *is* image + far, and it must land
  below 45,056. At 40,825 bytes that leaves **4,231 bytes** of slack — the
  number to budget against. Assertion 1 (image + `.bss`) has 6,210 and is not
  the constraint. Moving the clip code to `.fartext` buys nothing here, since
  it counts against the same guard — and it is per-primitive hot code that
  belongs in `.text` regardless. Measure with the recipe in SPEC.md §15.1
  before starting, not after.

### Acceptance criteria

- Cover half a Bounce window: the ball keeps moving, is drawn only in the
  visible half, and never paints over the covering window. Cover it entirely:
  it keeps stepping and appears at the right place when uncovered.
- Same for Clock's digits and Fractal's canvas and percentage.
- Drag a window over a rendering fractal: no artefacts on the dragged window,
  no artefacts on its shadow.
- Verify on all three adapters — `make test`, `make test VIDEO=cga`, and
  `make xt-hercules` for the probe. A clip that works on VGA and no-ops on
  mono is the expected failure mode of getting the hook placement wrong.
- Verify with the back buffer both off and on (Control Panel → Display).

---

## How the two parts interact

They are complementary, not alternatives. Part 2 lets a partly covered window
keep drawing; it does **not** help the move case, because uncovering still goes
through `wm_paint_all`, which white-fills and calls `W_PAINT`. Part 1's cache
is what makes that cheap. Landing Part 2 alone would leave the fractal still
restarting on every move; landing Part 1 alone would leave it blank whenever a
corner is covered.

## Reference

- Erase-before-paint: `kernel/wm.inc:909`, and the back-to-front repaint at `:545`
- `wm_obscured`: `kernel/wm.inc:624`; z-order storage `wm_zord` / `wm_zn` at `:1026`
- Drop shadow geometry: `kernel/wm.inc:771` and `:778`
- Rect primitives funnelling into `gfx_fill`: `kernel/vga12.inc:175`, `:195`, `:281`, `:374`
- Back-buffer dispatch pattern: `kernel/vga12.inc:294` (`gfx_fill`), `:710` (`gfx_xor_rect`)
- Fractal passes: `apps/fractal/fractal.asm:718` (`fr_advance`), `:386` (`fr_worker`)
- The no-frame-buffer decision as shipped: `apps/fractal/fractal.asm:50`
- Package worker task contract: SPEC.md §20.6
- Background-task draw discipline: SPEC.md §7, §14

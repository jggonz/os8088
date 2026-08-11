# Handoff — the redraw round, and what is left of it

**Branch `claude/clipped-blit-redraw-ebm6eb`, cut from `elendilon`.** This is the
continuation of REDRAW-SPEC.md Part 3, which is still the standing plan — read
its Part 3 first, then this. SPEC.md is the binding contract for everything
below; PERFORMANCE.md Sets 30–34 are the measurements.

---

## What landed, with its number

| SPEC | what | measured |
|---|---|---|
| §5.8 | `gfx_restore` can put back **part** of its buffer — three skip words, 0 at rest | — |
| §11.96.6 | the raise cache restores only what the pass painted | blit 30.63 → 15.27 ms, **2.01x** |
| §11.96.8 | the edge merge bounded to the same rect | 18.59 → 8.09 ms, **2.30x** |
| §11.96.7 | **a bug**: a bank is only worth what was on the glass when it was taken | 7,907 stale px → 0 |
| §11.90.1 | `WF_OWNBG` — the white fill in front of `W_PAINT` becomes opt-out | white hole 2,617 ms → **none** |
| §11.90.2 | `OSAPI_WM_DAMAGE` — the app is told which rect it owes | canvas blit 8,670 → 6,759 ms, **1.28x** |
| §11.96.10 | **a RAISE puts back only what was covered** (was item A) | Paint raise 9,090 → 5,948 ms, **1.53x** |
| §11.97 | a window below draws no chrome where something above will cover it | drag flash 14,253 → 10,665 px, **1.34x** |

One restore is **49.22 → 23.36 ms (2.11x)**. Every step verified at **0 differing
pixels** on CGA, Hercules and VGA mode 12h.

**Three corrections to the record are part of the work**, because each would
otherwise mislead the next reader:

- **`gfx_blit4` needs no sub-rect primitive.** REDRAW-SPEC paired it with
  `gfx_restore` as "the same arithmetic twice"; what makes a sub-rect impossible
  for a caller is not the blit but **who owns the source layout**. `gfx_save`'s
  buffer is the kernel's private plane-major business; `gfx_blit4`'s source is the
  caller's own pointer and `BP` stride, and `pt_blit` has taken an arbitrary
  canvas rect all along.
- **A blank Paint canvas is unrepresentative by 41x** (211 ms against 8,670).
  Never price a blit on flat art.
- **Paint's associations are not a bug.** It declares no header block, but
  `assoc.inc` ships a static table (`BMP`/`GIF` → PAINT, `TXT` → NOTEPAD, `MOD` →
  TRACKER, `MD` → ARTFUL), so a document double-click reaches it.

---

## Open bugs

### 1. Call-to-front showed another window's content — FIXED, §11.96.9

Reported on **PCem / Hercules / 8088 4.77** with a screenshot: the APPS window
called to the front came back with **another window's content inside it and no
title bar over that content**.

**It was a regression from §11.96.6 in this same round.** `wm_su_bank` reads a
window's content off the **screen** after `wm_draw_win`, which was safe while
every draw was whole; a draw that restores only a strip leaves the rest of that
rect holding whatever is on top, and that got banked as the window's own content.
The missing title bar is the tell — a raise cache holds content and never chrome.

Fix: a partial draw does not re-bank, and keeping the existing cache is not a
compromise but the right answer, a partial draw having changed nothing.
`tools/callfront.py` is the gate; **0 differing pixels** over all 11 steps of the
reported session on Hercules against `REDRAWFULL=1`.

**Two things about it are worth more than the fix**, and both are in §11.96.9:
`subcheck`'s two windows cascade 16px apart, so its partial restores are very
nearly whole ones and its banks come out nearly clean — **it takes three windows
with partial overlaps** to leave a large untouched region, which is what the field
session had. And a wrong cache is **invisible until it is used**: §11.96.7 says so
in its own paragraph, and it caught the same author twice in one round.

### 2. The window BELOW a drag flashes its edges and shadow — FIXED, §11.97

Reported alongside bug 1: dragging Note Pad, the window underneath shows its
**frame and drop shadow** for a frame or two before being painted over.

`wm_chrome_clip` arms `wm_covered`'s region over the **frame** rect — where
`wm_clip_set` (§11.3) arms it over the *content* rect — across the outline and
the drop shadow, and disarms before the title bar (§11.97.1). Both are
`gfx_fill`, which clips per pixel, so nothing had to learn about regions. Measured
(PERFORMANCE.md Set 40) on a drag across another window, three runs of each
build: **transient pixels 14,253 → 10,665, 1.34x**, with the frame count and
the visible-redraw time unmoved — which is the honest shape of it. The same
frames still change the same pixels; what changed is how many are written and
then immediately overwritten.

Five things the doing of it settled, against what this section predicted:

- **The prediction that mattered was right.** The whole-element `wm_covered`
  test would not have helped; the region is what was wanted.
- **§11.91's marking is the correctness argument**, and it is the same one that
  made drawing over a window above safe, read the other way round: a window
  overlapping a marked window is marked too, transitively upwards, so anything
  above that overlaps you is drawn later in this same pass.
- **`wm_clip_set` is the wrong entry point** — it drops the raise cache, which is
  exactly what the window is about to restore from. Seed and occlude directly,
  as `wm_covered` does. And **spend the deferred cursor hide** (§7.1.4): an armed
  region stops the clipped primitives taking it themselves.
- **The standalone `wm_draw_title` hazard did not arise**, because the arm lives
  inside `wm_draw_win` and not in the title painter — but the title bar had to
  come OUT of the armed span anyway (§11.97.1). `font_char` clips per whole
  CELL, so a glyph the region cuts is a glyph that STRADDLES the cut and the
  visible rows go with the covered ones: `callfront` caught a window's caption
  losing its top five rows, 112 differing pixels on CGA. This section's claim
  that the granularity rule could not bite here was simply wrong.
- **The session has to be the right one, and it was not.** §11.91.2 marks the
  window underneath on the rect the mover VACATED, so a drag that does not
  leave ground inside it redraws **the mover alone** — and the flash then
  measured is a residue of the desktop dither, worth 891, 280 and 902 across
  three runs of ONE binary. The first before/after off that read as 3.97x and
  was noise. Arm `wm_draw_win` and count the calls before believing a flicker
  number.

**What is left is the CONTENT**, which is the larger half of the same flash and
a genuinely different piece of work: `wm_su_try` restores ONE rect (§5.8) where
the visible region is a list, so bounding it is one `gfx_restore` per fragment,
each paying §5.7's per-call floor and §11.96.2's edge merge. That is a trade to
measure, not a free win. The residual bbox also names one row at
`[vid_dock_y0]` — the dock strip — and the grow box is outside the armed span,
being drawn after `W_PAINT`.

### 3. Paint's `W_PAINT` runs twice per raise — IT DOES NOT, retired

Carried by four Sets and never traced. It is **one** `wm_draw_win`, **one**
`W_PAINT`, and no `wm_front` re-entry of any kind — on the raise and on the
drag-off alike. The two `wm_grow_paint` hits that read as two passes are two
different callers: `pt_draw_strip` ends in `pt_growbox` → `OSAPI_WM_GROW`
(§11.1 — the strip's white bed erases the box, so every path that repaints the
strip owes it), and `wm_draw_win`'s own `.growbox` runs after `W_PAINT`. The
~400 ms in front of the first is the palette, the colour strip and the divider,
which is exactly what Set 32's own table calls it.

**So there is no 402 ms of free money**, and the lesson is about the instrument:
`os88span.py`'s Paint scenarios arm three symbols and `wm_grow_paint` was the
last of them, so a second hit of it read as a second pass. **When a trace
implies a control-flow shape, arm the symbol that shape would have to go
through** — here `wm_draw_win` and `wm_front`, neither of which fires. Set 40
has the traces.

---

## Outstanding work, biggest first

### A. A raise should restore only what was covered — **DONE, SPEC.md §11.96.10**

`wm_cov_rect` walks the windows above us before `wm_lift`, intersects each one's
frame with our content and accumulates a bounding box; `wm_raise` spends it into
`wm_su_sub` immediately before `wm_draw_win`. Both consumers took it unchanged.
**No region arithmetic was needed** — the complement of `wm_clip_tab` was the
plan and the union's bounding box is the same answer for one walk.

Three things came out of it that the next reader wants:

- **The estimate was backwards and the code is not.** "Only 10% of the canvas
  visible" is 90% *covered*, and a raise owes the covered part — so that case is
  7.8 s, not 0.9. The win is proportional to how much was covered and there is no
  typical amount: 59.9% covered measures 1.53x, 95% covered measures 1.12x, both
  exactly their own geometry (Set 39).
- **`wm_damage` had to learn about `WF_FULL`.** §11.2's branch of `wm_draw_win`
  white-fills the whole frame with no `WF_OWNBG` opt-out, so a partial answer
  there blanks the rest. Latent until now, because `wm_dmg_wins` rarely marks a
  fullscreen window and `wm_front` can raise one.
- **`AL = 2` is the argument**, on `wm_raise`'s existing 0/1, so only `wm_front`
  can ask: `wm_show`'s window has no pixels on the glass to keep. That is the
  whole safety condition and it is a property of the *caller*, which is why it is
  an argument and not a test inside `wm_raise`.

**And it found a defect in Paint on its way through** — SPEC.md §42.9,
docs/PAINT-NOTES.md. A window that resizes itself inside its own `W_PAINT` was
laying out the rest of that paint at the size it used to be, so 41 columns of
Paint's colour strip showed the **desktop dither** through `WF_OWNBG`. It had
always repaired itself on the next whole repaint; this change stopped raises
being whole and it stopped repairing. Two things to carry forward: **a gate
failure whose bbox is nowhere near your change is evidence about the tree** (the
steps *before* the raise showed it in both builds at 0 differing pixels, which
is what proved it pre-dated the change), and **an optimisation that stops
something being redrawn inherits every defect that redraw was hiding** —
§48.9.1's rule, arriving from the other direction.

### B. `gfx_blit4` still pays a drawing call per RUN

The 8.7 s itself, and **the single largest drawing cost in the system**. It
removed the far call per run — which is what it was built for and it worked — and
still emits one `gfx_hline` per run, paying §5.7's ~756 µs per-call floor
thousands of times. Priced per byte against `gfx_restore`: **244 µs against 5.5**.

Fully scoped in **docs/PAINT-NOTES.md** with its three hazards (1bpp and VGA are
different problems; the banked layout wants `gfx_nextrow` inlined per row; it must
stay byte-identical) and a gate that already exists (`tools/ptcheck.py`). **This is
the best piece to hand to a separate session** — self-contained in the renderers,
needing none of the `wm.inc` context the rest of this round is about.

### C. Registered cache regions and exclusions

The design the reporter proposed, of which §11.90.2 is half: a window registers
**regions it hands to the cache** and **regions it keeps**; a repaint restores the
cached ones the damage touches and hands the app the damage rects falling in the
kept ones. For Paint that is *cache the chrome* (~1 KB, kills ~376 ms) and *keep
the canvas* (no memory). The cache-side registration is not built.

And the stated end state: **"blank it" becomes the last resort for every window**,
not the default — a region that is neither cached nor claimed is the only thing
needing blanking. §11.90.1 is the opt-out half; flipping the default needs every
app audited, since the fill is what most `W_PAINT`s draw into.

### D. §11.91's marking still keys on rects, not redrawn regions

The step REDRAW-SPEC Part 3 deferred. §11.96.6 accumulates a bounding box, so the
bottom-most drawn window gains most and the saving tapers above it. Keying the
marking on each window's *redrawn region* is a real change to the marking pass.

---

## Before you write any code

**Footprint spare is 2,560 bytes on `kern_big` — FIVE 512-byte steps — and
3,584 on `kern_small`, which is seven and owes a conversation** (the raise met
SPEC.md §41.11's removal on the integration branch; docs/KERNEL-MEMORY.md's
"Where it goes" states the choice). That
is `KERN_BUDGET`'s **seventeenth move**, asked for and granted for this work
(2KB on `kern_big` and 2KB on `kern_small`, docs/KERNEL-MEMORY.md's table row
17): §11.96.9's fix had spent the step the image rung had 15 bytes left of and
taken the spare to one step against a standard of four. **It moves both guards
because a redraw optimisation is worth most on the slowest machine** — the
machine that feels a 49 ms restore is the 4.77MHz one at the RAM floor — so
nothing in this round may be put behind `%ifndef KERN_SMALL`.

Item A was what the raise was granted for, and it spent **one of the two
steps — on `kern_small` only**. §11.96.10 cost `.text` +355 and `.cold` +22:
`kern_big` crossed no rung and stands at **2,560 spare (five steps), 147 bytes
left in the image rung** (was 502); `kern_small`'s image rung **CROSSED**, so its
spare went **3,584 → 3,072 (seven steps to six)**. That is the shape the move was
asked for and it is the asymmetry to expect from anything in this round — the
small kernel's rungs are closer together. Report both, and watch `kern_big`'s
image rung: the next `.text` byte past 147 buys a whole 512 there too.

**Re-bless after every change** — `python3 tools/kernsize.py --bless` and,
because the two builds have separate baselines, `python3 tools/kernsize.py
--build build/smallk --bless -DKERN_SMALL` after `make kernsplit`.
`docs/KERNEL-MEMORY.md` has the accounting rule: report both numbers and never
call a change that crossed no rung "free".

**Verify by pixel diff, on all three adapters, or not at all.** `make
REDRAWFULL=1` is the reference kernel for this round's paths. The standard is **0
differing pixels** on CGA, Hercules and VGA mode 12h. It is not optional here: a
0-pixel gate is the only thing that can tell "the picture is the same, only less
of it was drawn" from "the picture changed", and every single round of this work
turned up something the author did not expect.

**And run the gate BEFORE you write anything.** §11.96.7 was found because a
verification came back with 7,907 differing pixels that were *already there*, and
proving they were not mine cost a bisect and two full capture cycles. A
pre-existing failure arrives looking exactly like your own.

## The instruments

| tool | what |
|---|---|
| `tools/subcheck.py` | the §5.8/§11.96.6 gate: one scripted session through two kernels, frame by frame |
| `tools/ptcheck.py` | the §11.90.1/§11.90.2 gate: Paint, with a **textured** picture |
| `tools/sucheck.py` | the §11.96 raise-cache gate (was broken on HEAD; fixed) |
| `tools/os88span.py` | prices a span of kernel work in guest cycles between named symbols — Sets 30–34 |
| `tools/rawdiff.py` | `show` / `diff` / `zoom` over the captures: **where** and **what**, not just how many |
| `tools/callfront.py` | the §11.96.9 gate: three partially overlapping windows, capturing after every step |
| `tools/mkbmp.py` | a 16-colour BMP with a chosen run density, for the Paint gates |
| `tools/chromeflick.py` | the §11.97 gate: how much of a drag's repaint is written and then overwritten — **the one defect a 0-pixel diff can never see** |

**Four harness traps, all of which cost a run in this round:**

1. **A dropped mouse edge is silent and cumulative.** `os88mouse`'s `click`/`drag`
   do not prove their button packets, and the 1200-baud UART drops one sent while
   the previous is in flight. A dropped *release* leaves the button down, so every
   later step quietly does nothing while each call reports success — measured, a
   session whose last seven steps left the window table completely unchanged. Use
   `subcheck.pclick` / `pdrag`, which prove both edges.
2. **Compare window GEOMETRY before pixels.** Two runs that put the windows in
   different places are not the same session, and the pixel diff blames the kernel:
   thousands of differing pixels in a window-shaped region, which is what a real
   defect looks like. `subcheck.diff` refuses on a geometry mismatch first.
3. **Mask the mouse arrow.** Its *position* is derived and identical between runs;
   whether it is *drawn* is not, the cursor being erased under the gfx lock and
   restored at the unlock. The same build captured twice differed by 45
   arrow-shaped pixels.
4. **Use a textured picture for anything involving a blit.** Blank art is one run
   a row; it is 41x cheaper and it is uniform white, which is exactly the colour a
   missing fill would have left — the one picture that cannot tell a kept promise
   from a broken one.
5. **Do not edit the tree while a capture is running.** `os88sym` asserts its map
   against `build/kernel.bin` and refuses rather than answering with a plausible
   wrong address, so one edit made during a batch of captures fails every run
   after it — at the first symbol lookup, before a window is open, so there is
   nothing left to look at. Cost this round one full six-capture sweep.

**And the machines.** MartyPC's Hercules and CGA are the GLaBIOS configs
(`os8088_5150_herc_gla`, `os8088_5150_cga_gla`), because the IBM 5150 ROM is not
in the tree; VGA mode 12h is `os8088_xt_vga`. A field report from PCem is in the
right units and does not announce itself — docs/FIELD-MACHINES.md's rule is to ask
which machine a number came from.

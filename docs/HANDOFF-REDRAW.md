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

### 2. The window BELOW a drag flashes its edges and shadow

Reported alongside bug 1: dragging Note Pad, the window underneath shows its
**frame and drop shadow** for a frame or two before being painted over.

This one is understood without reproducing it, and it is `wm_draw_win`'s doing:
the draw pass goes **back to front**, and a marked window draws its **chrome
whole — outline, drop shadow and title bar — whether its cache hit or not**
(§11.96.6 says so explicitly, because that is why the damage rect has to
accumulate). So the lower window paints its frame across ground the mover is
about to cover, and the mover, being frontmost, is drawn last.

**It is probably more visible since §11.96.6**, not less: the content restore got
2x faster, so the chrome draw is now a larger share of the window in which the
lower window is wrong.

The fix is to stop drawing chrome that something above will cover — and **the
obvious shape of that is wrong, so read this before writing it.** "Ask
`wm_covered`'s question per chrome element" is what this section said first, and
a desk check says it would barely help: `wm_covered` answers *wholly* covered,
the outline is a ring spanning the whole window, and the pixels that flash are
by definition the ones the mover covers — so the element is nearly always
PARTLY covered, the whole-element test says "draw it", and the flash stays
exactly where it was.

**What is wanted is the region, not the boolean.** `wm_covered` already seeds an
arbitrary rect (`wm_clip_seed`) and subtracts the windows above it
(`wm_clip_occl`); arming that over the **frame** rect — where `wm_clip_set`
(§11.3) arms it over the *content* rect — makes the outline, the drop shadow and
the title bar clipped draws, so each puts down only the fragments nothing above
will cover. Two things make it sound: a pixel the region drops is a pixel some
visible window above owns, and that window is either redrawn whole later in this
pass (§11.91 marks transitively) or was never disturbed; and §11.3's granularity
trap does not bite for the same reason — a title glyph dropped at a cut is a
glyph inside somebody else's window.

Hazards, all of them things this round has already been caught by: `wm_draw_title`
is **also called standalone** from `wm_raise` (§11.96.7's ordering), where the
z-order may not yet be what the glass shows; `gfx_unlock` clears the clip, so the
arm is valid for exactly one lock hold; the drag outline is XOR and must stay
unclipped (§11.3 rule 2); and `WF_FULL` has no shadow. **Price it with
`os88marty.py flicker` first** (PERFORMANCE.md Part 3.1) — it counts exactly this
defect and the number goes in a Set — and note that the region arithmetic is the
same arithmetic item A needs, so **doing A first may make this a call-site
change rather than a mechanism**.

### 3. Paint's `W_PAINT` runs twice per raise

Flagged three times in Sets 32–34 and never chased. Two `wm_draw_win` passes for
Paint's window — a ~376 ms one that draws no canvas, then the real one. Almost
certainly **`[pt_apend]`'s deferred-resize path calling `OSAPI_WM_FRONT` from
inside `W_PAINT`**, which re-enters the raise. It doubles the cost of every
measurement in those Sets.

---

## Outstanding work, biggest first

### A. A raise should restore only what was covered

**The biggest number left.** Both new features answer "whole" on a raise, and
correctly: `wm_raise` arms no damage rect because there is none. But what a raised
window owes is the part that **was covered**, which is computable — the complement
of §11.3's visible region, taken **before `wm_lift`** while the z-order still
agrees with the glass. That is the case the reporter described as "only 10% of the
canvas visible", and it turns 8.7 s into ~0.9 s for Paint and shrinks every
cached restore as well.

Both consumers are already built and waiting for it: `wm_su_sub` (§11.96.6) and
`OSAPI_WM_DAMAGE` (§11.90.2) both take a rect and neither cares where it came
from. **This is a `wm_raise` change, not a new mechanism.**

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

Item A is what the raise was granted for; spend it there. **Re-bless after every
change** — `python3 tools/kernsize.py --bless` and, because the two builds have
separate baselines, `python3 tools/kernsize.py --build build/smallk --bless
-DKERN_SMALL` after `make kernsplit`. `docs/KERNEL-MEMORY.md` has the accounting
rule: report both numbers and never call a change that crossed no rung "free".

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

**And the machines.** MartyPC's Hercules and CGA are the GLaBIOS configs
(`os8088_5150_herc_gla`, `os8088_5150_cga_gla`), because the IBM 5150 ROM is not
in the tree; VGA mode 12h is `os8088_xt_vga`. A field report from PCem is in the
right units and does not announce itself — docs/FIELD-MACHINES.md's rule is to ask
which machine a number came from.

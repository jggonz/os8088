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
| §5.4.1 | a blit run goes straight into the framebuffer, 1bpp and VGA | canvas blit 5,526 → 2,431 ms (CGA), 4,226 → 2,163 (VGA) |
| §5.4.1.1 | …and then the runs go: the 1bpp pair decoder, no hybrid | 2,431 → **517 ms**, and CONSTANT in the content |
| §5.4.1.2 | four pairs are one byte: an aligned body per x PARITY | 517 → **259 ms** even, 508 → **299** odd |
| §11.96.11 | a window names a BAND; the kernel banks it, the app owes the rest | Paint raise 680.9 → **451.0 ms**, 1.51x |
| §11.96.11.1 | …and FOUR of them, held as a fragment list | Paint chrome 421.8 → **90.6 ms**, 4.7x |
| §11.91.3 | **item D measured and NOT built** — the transitive mark it targets does not arise | 0 of 3 windows spared |
| §43.9 | Solitaire: `WF_OWNBG`, and a full repaint stops filling every pile rect twice | move repaint 934.6 → **809.0 ms**, 13 fewer fills |
| §43.9 | …and `wm_band` **measured and declined** there — its whole-content cache already answers | raise 97.5 ms, no `W_PAINT` at all |

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
  Never price a blit on flat art — **which §5.4.1.1 retired**: a blit costs the
  same whatever is in it now, and flat art is the one case that got *slower*.
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
(PERFORMANCE.md Set 42) on a drag across another window, three runs of each
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

**What IS there, and is worth its own commit later, is that the grow box is
drawn TWICE per Paint repaint.** The two callers run back to back from the
user's point of view — `pt_draw_strip` ends in `pt_growbox` and then
`wm_draw_win`'s `.growbox` draws the same 13x13 box again a few hundred
milliseconds later — and `wm_grow_paint` is **14 drawing calls** (fill, frame,
frame, fill, frame), which §5.7 prices at ~756 µs of *arriving* each: about
**10.6 ms, paid twice**, plus PERFORMANCE.md Part 1's double draw on the glass.

Paint's call is not redundant in general and must not simply be deleted: the
strip's white bed erases the box, so every path that repaints the strip owes it
one, and the tool, colour, width and toggle clicks repaint the strip **without
a `W_PAINT` behind them**. What is redundant is the call made *inside* a
`W_PAINT`, where the kernel is about to draw the box anyway. The shape of the
fix is therefore a "we are inside our own paint proc" test in `pt_growbox`, or
`wm_draw_win` skipping `.growbox` for a window that has drawn its own — and the
first is much the smaller change. Deferred deliberately; it is ~21 ms of every
Paint repaint and none of the multi-second ones.

**So there is no 402 ms of free money**, and the lesson is about the instrument:
`os88span.py`'s Paint scenarios arm three symbols and `wm_grow_paint` was the
last of them, so a second hit of it read as a second pass. **When a trace
implies a control-flow shape, arm the symbol that shape would have to go
through** — here `wm_draw_win` and `wm_front`, neither of which fires. Set 42
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
  exactly their own geometry (Set 41).
- **`wm_damage` had to learn about `WF_FULL`.** §11.2's branch of `wm_draw_win`
  white-fills the whole frame with no `WF_OWNBG` opt-out, so a partial answer
  there blanks the rest. Latent until now, because `wm_dmg_wins` rarely marks a
  fullscreen window and `wm_front` can raise one.
- **`AL = 2` is the argument**, on `wm_raise`'s existing 0/1, so only `wm_front`
  can ask: `wm_show`'s window has no pixels on the glass to keep. That is the
  whole safety condition and it is a property of the *caller*, which is why it is
  an argument and not a test inside `wm_raise`.

**And it found a defect in Paint on its way through** — SPEC.md §42.10,
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

### B. `gfx_blit4` paid a drawing call per RUN — **DONE, SPEC.md §5.4.1**

The 8.7 s itself, and it was **the single largest drawing cost in the system**.
`sw_blit_span` writes a run into the 1bpp framebuffer itself, so §5.7's per-call
floor is paid once a CALL rather than once a run: the row base and the dither
phase are worked out once a row, §39.4's reduction is a table read, and what is
left is two masked bytes and a `rep stosb`. Measured (PERFORMANCE.md Set 43) on a
cycle-accurate 5150/CGA: the canvas blit **5,526 → 2,431 ms on 85-runs-a-row art
and 18,777 → 8,365 on 308**, the same 2.25x both times, because what goes is a
fixed cost per run.

Three things to carry forward:

- **2.25x and not 45x, and the gap is not a shortfall.** The 45x was
  `gfx_restore`'s **per-BYTE** cost against the blit's; this is a **per-RUN**
  change. It removes ~400 µs of arriving and leaves ~315 µs of per-run work —
  the scan's three 4-bit shifts, the `repe scasb` setup, the tail nibble decode
  and the span writer's forty instructions.
- **The rest needs a different routine, and a hybrid.** A byte-by-byte decoder
  that gathers 8 pixels' bits regardless of runs is ~44 clocks a pixel: another
  20x on `FINE.BMP`'s art and **fifteen times worse on a flat row**, where the
  run path draws 492 pixels in one span. So the end state is keyed on the row's
  run density, and this change is the half that is unconditionally right.
- **VGA is `vga_blit_span`**, the same routine with the byte pattern replaced by
  a port write: Enable Set/Reset armed once a CALL, the colour one `out` a run,
  and the same left-edge / `rep stosb` / right-edge shape. **1.95–2.01x**, a
  little behind 1bpp because an ISA `out` is not free.

**`PTROW=1` belongs in any future run of this gate**: `FINE.BMP`'s 1–2 pixel runs
put every run inside ONE framebuffer byte touching neither edge, which is the
narrowest case the masks have and one a picture barely reaches.

### B2. The byte decoder — **BUILT, SPEC.md §5.4.1.1**

`sw_blit_row` walks the destination byte by byte; a source byte is two pixels
and a 256-byte table turns it into two destination bits. **No hybrid** —
docs/LAST-DROP-PERF.md 3 is the costing, kept as the record of a deliberate
omission. Measured (PERFORMANCE.md Set 44):

| Paint's canvas | pre-§5.4.1 | span writer | **decoder** |
|---|---|---|---|
| flat, 1 run a row | — | 132.1 ms | **517.6 ms** |
| textured, 85 a row | 5,526.2 | 2,430.7 | **516.6** |
| fine, 308 a row | 18,777.3 | 8,364.9 | **517.6** |

**0.2% spread across a 3.6x range of run density** — a blit has one cost now.

**The lesson is a rule and it is §5.7's own: count BYTES, not clocks.** This
section predicted 4.71 µs a pixel from textbook instruction timings and it is
9.56 measured, because an 8088 floors at **4.34 clocks per instruction byte** —
the 8-bit bus starves the prefetch queue, so the SIZE of a tight loop is its
speed. Every clock-count estimate of a loop in this document should be read as
a lower bound.

**And VGA keeps the span writer** — four planes want the run's bits each, so
its decoder is four Map Mask passes, a different routine.

### B3. The aligned bodies — **BUILT, SPEC.md §5.4.1.2**

B2's own closing arithmetic. The per-pair count test looks for a boundary that
arrives every *fourth* pair, so once the first destination byte is stored and
the phase is fixed, four pairs are one whole byte and the accounting goes.
**259.1 ms at an even canvas x (1.99x), 299.0 at an odd one (1.70x)**, still
flat in content (0.4% across 85 → 308 runs a row). PERFORMANCE.md Set 45.

**Both phases had to be built, and finding that out is the transferable part.**
The even body is reachable only at an even destination x; Paint's template
lands its canvas on one; so every gate and every measurement in this round had
been pricing *one half of the routine*, with the other half one pixel of drag
away. `PTNUDGE` is now an argument to `tools/ptcheck.py` and
`tools/os88span.py` — an odd sideways drag before the cover/raise — and the
rule it encodes is that **a gate that never moves the thing it tests proves
the alignment it happened to start at**. Set 42's chrome-flash drag was the
same mistake in another place.

**What is left in this loop is small.** The even body is 37 instruction bytes
per eight pixels and about half of that is the four `lodsb`/`cs xlat` pairs,
which are execution-bound rather than fetch-bound (25 clocks for 4 bytes); the
whole loop is ≈153 clocks executing against ≈148 fetching, so it is balanced
and there is no third factor of two here. The next real lever on a blit is
**not** drawing it — §11.90.2's damage rect, already in.

### What else the blit is under

`gfx_blit4` has three consumers besides Paint's canvas, and one of them is more
latency-critical than Paint has ever been:

- **Solitaire's card BACKS — MEASURED, and the estimate above them was stale
  by an order of magnitude.** This entry used to read *"a card back was 278 ms
  on CGA and is 125 ms after §5.4.1, and would be ~13 ms with the decoder"*,
  priced from the run count. The decoder landed (§5.4.1.1/§5.4.1.2) and nobody
  came back to it. Measured (PERFORMANCE.md Set 49): a whole Solitaire repaint
  issues **22 `gfx_blit4` calls in 562.6 ms** and the dearest single one is
  **18.7 ms**, so the prediction was right and this line was two rounds behind
  it. **There is nothing left to win here** — a back is no longer priced by
  what is in it.
  What the same measurement DID find is §43.9: the repaint is ~91% per-call
  floor, and twelve of its fills were the same pixels twice.
- **ArtfulType's KEYSTROKE PATH — TABLED, and it is a SESSION OF ITS OWN.**
  Do not pick this up as part of the redraw round: the owner has taken it out
  and will drive it separately, because the reported symptom (twenty characters
  in twenty seconds) is an order of magnitude away from anything the blit
  arithmetic below predicts, and closing that gap is an investigation rather
  than an optimisation. What is known is recorded here and nothing more.
  `at_draw_line` composes a line, expands it to
  packed 4bpp and puts it on screen as **one `OSAPI_GFX_BLIT4`, per keystroke**,
  ungated by adapter. Text is the worst case for run coalescing — every glyph
  edge is a run — so this is structurally the biggest beneficiary in the system
  and the one where the saving is felt as *typing latency* rather than as a
  repaint. **It is NOT measured**: three attempts to time a keystroke through
  the debug server produced no `gfx_blit4` hit at all while typing plainly
  worked before the breakpoints were armed, which is a harness problem and not
  an app one — **and solving that harness problem is the first half of the
  tabled session**, because until a keystroke can be timed at all, nothing
  about it can be told from anything else. If a line is the ~1,200 runs a
  45-character line of 8x8 glyphs suggests, the three §5.4.1 steps have already
  taken it from seconds to tens of milliseconds and the report is stale; if it
  has not, the cost is somewhere else entirely and the blit was never it.
- **Tracker** mentions the idiom in its fullscreen text path but does not blit.

### C. Registered cache regions — **BUILT, SPEC.md §11.96.11 and §11.96.11.1**

The design the reporter proposed, of which §11.90.2 is half: a window registers
**regions it hands to the cache** and **regions it keeps**; a repaint restores the
cached ones the damage touches and hands the app the damage rects falling in the
kept ones. For Paint that is *cache the chrome* and *keep the canvas*.

**What the measurement said, and it decided the shape**: Paint's chrome is 604
drawing calls and 421.8 ms, and **451 of them — 75% — are the 44-pixel tool
palette**; the bottom strip is 131 calls and 99 ms. So the built half is a
**band on one edge**, which keeps both the cached part and the kept part
RECTANGLES and so needs no region arithmetic anywhere: `wm_band` (slot 0x03B8),
the cache banks the band, `wm_damage` hands the app the content minus it.
**Paint raise 680.9 → 451.0 ms, 1.51x**, and Paint needed no drawing change —
`pt_draw_pal` was already gated on the damage rect. PERFORMANCE.md Set 46.

The remaining 22% is §11.96.11.1: **four** bands rather than one, held as a
fragment list in the cache. `KERN_SMALL_BUDGET`'s twentieth move paid for it.
**Chrome 421.8 → 90.6 ms, 4.7x; the whole raise 680.9 → 350.8, 1.94x**
(PERFORMANCE.md Set 48), and the three bugs it sprang are all one mistake —
a routine that was right about "the buffer" is wrong about "a fragment of it".
Two of them showed only on Hercules, because the two adapters' sessions restore
a different set of fragments.

And the stated end state: **"blank it" becomes the last resort for every window**,
not the default — a region that is neither cached nor claimed is the only thing
needing blanking. §11.90.1 is the opt-out half; flipping the default needs every
app audited, since the fill is what most `W_PAINT`s draw into.

### D. §11.91's marking keys on rects — **MEASURED AND NOT BUILT, SPEC.md §11.91.3**

The step REDRAW-SPEC Part 3 deferred: key the marking on each window's
*redrawn region* instead of its rect. **It buys nothing here**, and the
measurement is the deliverable — PERFORMANCE.md Set 47.

It can only spare a window marked **transitively**, one that does not overlap
the damage rect but does overlap a marked window below. On a session built to
favour exactly that — three windows, Note Pad parked over a Disk window's
INTERIOR and dragged off — the damage rect is (127,60)–(427,199) and **all
three drawn windows overlap it: zero transitive marks.** A drag's damage rect
is the union of where the mover was and is, so every window it uncovered
overlaps it by construction; a two-window drag draws two, one of which is the
mover and can never be spared.

And the fix cannot be a bounding box — the frame outline's bbox IS the whole
window rect — so it needs a rect list with union and intersection, in the
routine where a wrong answer leaves STALE pixels rather than extra ones. The
other half, §11.96.6's taper, needs the same region and is worth 19% of ONE
restore, about 4 ms of a pass costing over 100.

**What would flip it**: small windows floating over large ones — a palette, a
tool window, an inspector. os8088 has none.

---

## Before you write any code

**Footprint spare is 2,560 bytes on `kern_big` — five steps, after the merge
that brought the network driver's own raise — and 512 on `kern_small`, which is
ONE.** `KERN_CODE_MAX` is the one to watch on the big kernel now: `.text` +
`.bss` is **64,965 of 65,536, 571 bytes left**, and that ceiling is 16-bit
offsets and cannot be raised. The next thing that does not fit there goes to
`.cold` (§2.6) or the boot overlay (§2.5), neither of which relieves the
footprint.
`KERN_SMALL_BUDGET`'s **twentieth move**, 96,256 → 97,280, was asked for and
granted and is **allocated to window redraw improvements**: §11.96.11 had left
the small build exactly on the old figure with 0 spare, and the items below are
what the step is for. It is the first move that figure has taken at the 1KB unit
its own rule sets, `kern_big` having moved by 2KB throughout. The image rungs
have 299 bytes (`kern_big`) and 294 (`kern_small`). The paragraphs below are the history of
how that was arrived at, and the figures in them are the ones each move
reported at the time.

The seventeenth move left 2,560 on `kern_big` and
3,584 on `kern_small`, which was seven steps and owed a conversation (the raise met
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
`kern_big` stands at **2,560 spare (five steps), 147 bytes
left in the image rung** (was 502); `kern_small`'s image rung **CROSSED**, so its
spare went **3,584 → 3,072 (seven steps to six)**. That is the shape the move was
asked for and it is the asymmetry to expect from anything in this round — the
small kernel's rungs are closer together. Report both, and watch `kern_big`'s
image rung: the next `.text` byte past 147 buys a whole 512 there too.

**§5.4.1.1 and §5.4.1.2 spent the rest.** The eighteenth move (+512,
`kern_big` only) was an ASK rather than a grant — the pair table landed exactly
on the guard — and the nineteenth (+2,048) was granted with it as one piece of
work, "blit4 rendering speed"; 148 of that is spent on the aligned bodies and
`kern_small` crossed another rung for them (1,536 → 1,024). The live figures
are at the top of this section.

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

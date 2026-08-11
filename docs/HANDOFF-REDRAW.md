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

### B. `gfx_blit4` paid a drawing call per RUN — **DONE, SPEC.md §5.4.1**

The 8.7 s itself, and it was **the single largest drawing cost in the system**.
`sw_blit_span` writes a run into the 1bpp framebuffer itself, so §5.7's per-call
floor is paid once a CALL rather than once a run: the row base and the dither
phase are worked out once a row, §39.4's reduction is a table read, and what is
left is two masked bytes and a `rep stosb`. Measured (PERFORMANCE.md Set 41) on a
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

### B2. The byte decoder — costed, not built

§5.4.1 removed the per-CALL floor from a blit and left a per-RUN cost. This is
the design that removes *that*, and it is written down with its arithmetic
because the number it was originally justified by — "45x" — is a **per-byte**
figure and this is not going to reach it.

**The measured constants it is designed against**, two-point fits from Set 41
(the same session at two run densities, so per-row and per-pixel terms cancel):

| | per run, before §5.4.1 | after | removed |
|---|---|---|---|
| 1bpp (CGA) | 828 µs | **371 µs** | 457 |
| VGA | 526 µs | **258 µs** | 268 |

**The design.** Stop emitting per run and walk the DESTINATION byte by byte. A
source byte is two pixels and maps, through §39.4's reduction, to a **two-bit**
destination pattern that depends only on the byte's value and one parity bit —
so a 256-byte table answers it outright and the inner loop is four table reads
and a store per destination byte:

```
    mov al, [es:si]     ; the source byte (2 px); ES is the source, so not lodsb
    inc si
    cs xlat             ; AL = its 2-bit pattern
    shl dl, 1
    shl dl, 1
    or  dl, al          ; ...four times, then `mov [di], dl`
```

Counted on an 8088 that is **~180 clocks per 8 pixels = 22.5 clocks a pixel =
4.71 µs** — and note that is an instruction count, not a measurement.

**The crossover is 1.84 runs a row, not one run per 85 pixels**, and the first
version of this section had it wrong in a way worth keeping: it compared the
decoder's per-PIXEL cost against the MARGINAL per-run cost, forgetting that the
run path also reads every source byte (`repe scasb`) whether it coalesces
anything or not. Measured on flat art — one run a row, `mkbmp ... flat` — the run
path costs **132.1 ms** for the same 110 rows, which fixes the missing term:

```
run path, µs a row = 830 + 371 x runs          (830 = the scan and the row setup)
decoder,   µs a row = 1,513                    (whatever is in the row)
```

That model reproduces all three measured points within 3% — flat 132.1 against
132.1, textured 2,352 against 2,430.7, fine 8,292 against 8,364.9 — so the
crossover is where `830 + 371R = 1,513`, i.e. **R = 1.84**. Anything that is not
a solid bar is above it.

| | now | with the decoder | |
|---|---|---|---|
| CGA, flat (1 run/row) | 132.1 ms | 166.5 | **1.26x WORSE** |
| CGA, 85 runs/row | 2,430.7 ms | 166.5 | **14.6x** |
| CGA, 308 runs/row | 8,364.9 ms | 166.5 | **50.2x** |
| VGA, four Map Mask passes | | ~4x the above | 2.9x / 9.6x |

**So the hybrid is not worth building**, which is the second correction. It buys
26% on a perfectly flat row and costs a switch plus keeping BOTH paths alive
forever. Decoder-only is the design: a blit becomes **constant time in its
content**, which is a better property for a UI than a fast case and a slow one.

**And dropping the hybrid is what makes it affordable.** The span writers
`sw_blit_span` (167 bytes), `vga_blit_span` (157) and `vga_sr_on` (12) are
**336 bytes** that the decoder replaces outright; the switch would be ~30 more.
What must stay either way is `gfx_blit_run` (34 bytes) and the run scan, because
they are the fallback for the three refused cases.

**What it costs.****What it costs.**

- **~200 bytes of `.text`** for the 1bpp decoder (the aligned inner loop, the
  leading and trailing partial bytes, and the odd-`x0` case) plus ~40 for the
  table build, **against 336 bytes of span writers it deletes**. ~250 more for
  VGA's four-pass twin, which deletes nothing extra.
- **512 bytes of table** for 1bpp (two parities x 256), 512–1,024 more for VGA.
- **And it has to live in `.text`, not on the heap**, which is the part that
  costs real budget. `xlat` is DS-relative and the loop already needs three
  segments at once — source (ES), framebuffer, table — so the table goes in CS
  and is read with `cs xlat`, leaving DS for the framebuffer. A purgeable heap
  claim (§50.6) would be free against the budget and cannot be addressed here.
- **So, decoder-only and 1bpp-only: +240 of code +512 of table −336 deleted =
  about +416 bytes, against 383 bytes of slack in `kern_big`'s image rung.**
  That is one 512-byte step, and painfully close to none at all — a hundred
  bytes of a tighter inner loop and it fits in what is already there. **With the
  hybrid it is unambiguously two steps** (nothing is deleted and the switch is
  added), and VGA's twin plus its own table is two or three more. The budget has
  **512 bytes spare on `kern_big`**, so 1bpp decoder-only is the only variant
  that does not certainly need a raise.
- **Verification is the existing gates and no new ones**: `ptcheck` on three
  adapters plus `PTROW=1`, which is exactly the shape §5.4.1 was proved with.
  The new failure modes are bit phase (`x0` mod 2 and mod 8), the dither parity
  the table bakes in, and the partial bytes at each end.

**The honest summary is that 1bpp decoder-only is worth it and VGA is
marginal.** 14.6x on the machine this project is calibrated against for one
budget step or none, at the price of 26% on a blank canvas; 2.9x on VGA for two
or three more steps, on an adapter the calibration machine does not have.

**And Paint is not the only consumer** — see "What else the blit is under",
below.

### What else the blit is under

`gfx_blit4` has three consumers besides Paint's canvas, and one of them is more
latency-critical than Paint has ever been:

- **Solitaire's card BACKS.** The source says what they cost — *"the back is a
  lattice, so `gfx_blit4` coalesces it into 634 `gfx_fill` runs on the 32x44
  metrics (336 on CGA's 28x28)"* — so priced with the measured constants a card
  back was **278 ms on CGA** and is **125 ms** after §5.4.1, and would be
  ~13 ms with the decoder. `sol_drawall` draws the whole tableau; `sol_cmd_deal`
  already has a comment about how dear one back is.
- **ArtfulType's KEYSTROKE PATH.** `at_draw_line` composes a line, expands it to
  packed 4bpp and puts it on screen as **one `OSAPI_GFX_BLIT4`, per keystroke**,
  ungated by adapter. Text is the worst case for run coalescing — every glyph
  edge is a run — so this is structurally the biggest beneficiary in the system
  and the one where the saving is felt as *typing latency* rather than as a
  repaint. **It is NOT measured**: three attempts to time a keystroke through
  the debug server produced no `gfx_blit4` hit at all while typing plainly
  worked before the breakpoints were armed, which is a harness problem and not
  an app one. **Take that measurement first** — if a line is the ~1,200 runs a
  45-character line of 8x8 glyphs suggests, it dwarfs everything in this
  document.
- **Tracker** mentions the idiom in its fullscreen text path but does not blit.

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

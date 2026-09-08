# What a vector game found on the other side of §53.7's fence


> **COMPLETED.** One item was built - `fsx_page` (SPEC.md §53.10), because
> `FSI_PAGES` had been published since SPEC.md §53.4 with no supported way to
> act on it and the mechanism differs in kind per adapter - and the other was
> **priced and deliberately left**, which is a decision rather than a debt.
>
> **The useful half of that second finding is that the CHEAP version is not
> worth building**: `gfx_line` has no batch form, and a loop over `gfx_line`
> collects the far-call cell (46.7 us) and *not* §5.6.8's 128.7 us arrival,
> because the rest of the arrival is `gfx_line`'s own prologue. Anyone
> proposing a batch line primitive should start from that number.
>
> The largest finding in the file - that three apps now carry their own
> Bresenham - is deliberately **not** proposed as a slot. Read §0 first: it
> carries the profile every argument here is against.

*A bare `§` in this file means SPEC.md.*

TANK ATTACK (§85) was built to be a **load**: a hundred-odd line segments a
frame, a game attached, on the machine this project is calibrated against.
§78's WIREFRAME already exercises the kernel's line primitive in a window at
twelve edges a frame. This exercised what is on the *other* side of §53.7 —
the machine, in a **foreign** video mode, where no kernel drawing slot is
legal and every pixel is the application's.

This document is what that found. One item has been built and is §53.10; the
rest are written down with their measurements so the next person argues with
numbers rather than with me.

## 0. The measurements everything below is against

Taken on **MartyPC**, cycle-accurate 4.77 MHz 8088, GLaBIOS, 360KB floppies —
the default instrument (docs/TESTING.md). Frame counts are read out of the
package's own `tk_frames` from outside the guest, so nothing is instrumented
into the frame.

| | frame rate | viewport |
|---|---:|---|
| 5150 + CGA, `FSXM_CGA320` | **6.06 fps** | 320×200×4 |
| 5150 + Hercules, `FSXM_HERC` | **4.32 fps** | 640×200 mono in 720×348 |
| XT + VGA, `FSXM_MODEX` | (this row once said MartyPC does not model the unchain - it does, on `os8088_xt_vga`, and CLEAR SKIES' Mode X frame is priced and gated there, SPEC.md 88.12 and `tests/skies.py`) | 320×240×256 |

**These are the rates it shipped at.** §85.3.4 to §85.3.6 and §85.6.6.3 are the
round that followed, measured exact and scene for scene rather than sampled:
the same Hercules frames run 5.2 → 9.0 fps on a twelve-piece scatter and
2.8 → 3.9 on a deliberate cluster, and eight fresh games average 9.6 fps
still and 7.8 turning where they averaged 5.4 and 4.5. §85.3.4 is also why
the sampled profile in §0.1 below should not be trusted for the walk's
share: it read nearly twice the exact figure.

**And the thing it is being compared against was measured too**, rather than
remembered. The reference is a capture of the 1983 MS-DOS port. Sampling its
play area at 30 Hz and counting the samples where more than eight pixels of an
80×50 reduction moved by more than a quarter of full scale — which separates a
new game frame from the capture's own compression noise, and the histogram is
cleanly bimodal, 321 samples of nothing against 41 of everything:

```
  second   0  1  2  3  4  5  6  7  8  9 10 11
  updates  1  0  4  5  4  1  8  6  6  6  6  6
```

**About 6 frames a second, peaking at 8.** So this port is at the original's
rate on the original's machine, and the rest of this document is about
headroom rather than about a gap.

### 0.0 And the frames do not flash, which is the point of the design

`tests/tank.py` samples the lit pixels out of VRAM **once per displayed
frame** — the instrument §78.5 had to build for the same question, because
`m.flicker()` wants a screen that settles and a game never does again.

**A FLASH IS A DIP, NOT A LOW FRAME, and getting that wrong cost a round
here.** The first version of this gate priced each frame against the run's
MEDIAN, and on CGA it read 99.9% one run and 55.6% the next — on a game that
had not changed. What the 55.6% run was seeing is a scene that legitimately
lost half its ink: an object left the view, the picture walked *down* and
stayed down. A frame taken apart on the glass is thin **between two full
ones**, so each frame is priced against its NEIGHBOURS, which is the only
comparison a moving picture supports.

Forty displayed frames, three runs, both 1bpp adapters:

| | thinnest against its neighbours | frames under 90% |
|---|---:|---:|
| CGA, 320×200, shadow + span blit | **100%** | **0 of 40** |
| Hercules, 640×200, shadow + span blit | **100%** | **0 of 40** |

That is what "no pixel is written twice and none is written that did not have
to be" looks like from outside: **not one displayed frame in forty, on either
adapter, is thinner than the frames either side of it.** §78.5's windowed
comparison is the measure of it — its `Whole figure` order has an emptiest
frame of **0%** ink and puts 56% of displayed frames under half full, and even
its best order dips to 44%.

### 0.1 Where a foreign-mode frame goes

400 CS:IP samples on the CGA machine, bucketed by symbol, after the pacing fix
in §2.1:

| | share |
|---|---:|
| `tk_line_cga` — the Bresenham walk itself | 20.8% |
| `tk_blit` — the dirty spans onto the glass | 13.8% |
| `tk_clearspans` — last frame's runs, erased | 6.2% |
| `tk_seg` + `tk_outcode` + `tk_lprep` — clip and set up | ~9% |
| `tk_project` + `tk_rotshape` + `tk_objcam` + `tk_emit` — the geometry | ~11% |
| `tk_glyph_*` + `tk_drawtab` + `tk_hseg` + `tk_ridge` — the panel | ~8% |

The shape of that is the finding: **the pixels are a fifth of it.** A frame
lights about 2,000 of them and costs about 790,000 cycles, which is 395 cycles
a pixel against the ~60 the walk actually spends — so the rest is *per
segment* and *per row* overhead, in a hundred-odd segments and two hundred
rows. That is the same conclusion §5.6.8 reached about the windowed side from
the other direction, and it is why the items below are about arrivals and
per-row work rather than about inner loops.

## 1. There is no kernel raster past `fsx_mode`, and every fsx app pays for it

§53.7 makes every drawing slot illegal after a mode set, for a reason that is
correct: they render *desktop* geometry into a *foreign* framebuffer. The
consequence is that an fsx app in a foreign mode writes its own rasteriser.

Three now have. Missile Command (§48) carries `mx_fill`, `mx_xor`, `mx_frame`,
`mx_text` and `mx_px`. TANK ATTACK carries `apps/tank/tkraster.inc`: a
Bresenham with three plots, a horizontal run with three, a glyph blitter with
three, a span clear and a span blit — **about 1,100 lines, and none of it new
work in kind.** The kernel has all of it, in another coordinate system.

**This is the largest finding and it is NOT proposed as a slot**, because the
obvious shape of the fix is wrong in the same way §53.6.1's XMS stash was
wrong. "Let the drawing slots take a surface" means every primitive in the
kernel grows a level of indirection that only an fsx app ever uses, on a
machine where `gfx_hline` already spends 3,235 cycles on six pixels
(docs/plans/completed/GFX-REWORK-PLAN.md). The two shapes that might survive that argument:

- **A published 1bpp BAND rasteriser.** `kernel/band.inc` already composes a
  title bar into a caller-supplied band with `band_hline_x` and
  `band_frame_x`, at 2,110 cycles against `gfx_hline`'s 4,112 (§5.9). A
  `band_line` and a `band_glyph` would let a foreign-mode app compose in the
  one layout that is adapter-independent and then spend one loop of its own
  turning the band into whatever the card wants. It would serve the windowed
  side too, which is what makes it worth the bytes.
- **Nothing at all**, and this section as the reason. An fsx app that sets a
  mode has taken the machine; owning the raster is what taking the machine
  means, and three copies of Bresenham is a smaller price than a kernel that
  carries a surface abstraction for three callers.

The measurement that would decide it is not taken. What is now known is the
size of the duplication (1,100 lines) and the cost profile above.

## 2. `FSI_PAGES` was published and un-actionable — BUILT (§53.10)

§53.4 has advertised a page count per mode since it was written. Nothing could
use it, because the mechanism differs in kind per adapter and none of it is
derivable from the info block: Mode X moves the CRTC start address in bytes, a
Hercules does not move it at all but flips bit 7 of 3B8h, and that bit does
nothing until bit 1 of 3BFh allows it — which `vid_setmode` deliberately
leaves clear (§39.6).

`fsx_page` (slot 0x04E8) writes the register **and waits for the retrace that
latches it**, so its contract is one sentence: when it returns, the page named
is the one being scanned and every other page is yours to draw on. **108 bytes
of `.text` and one 8-byte slot**; accrued image 153/512 → 261/512, `KERN_SIZE`
unmoved.

Both ways of getting it wrong were hit while building the consumer, and both
look like a card that has stopped working rather than like a bug: writing the
start address *before* the retrace rather than waiting for the one that
latches it, and enabling a Hercules page without its configuration bit.

### 2.1 Not a gfx finding, but the largest single win: pace the SIMULATION, not the frame

The first build paced one frame per system tick, which is what §53.5's
`FSXW_TICK` is for. On the target machine that **cost 18.5% of the frame**,
measured: a frame that overruns a tick by a fifth then rounds up to the next
whole one, so a 2.2-tick frame becomes a 3-tick frame.

The fix is not a finer clock. It is to notice that a game has two rates and
was conflating them: the **simulation** must advance at a fixed rate or the
game plays three times as fast on a 386 as on the XT it was built for, and the
**drawing** should go as fast as the machine can. So `tk_steps` reads
`[ticks]`, steps the world once per elapsed tick (capped at three, or a floppy
stall teleports a shell clean through a tank), and waits **only** when nothing
is owed. `FSXW_TICK` disappeared from the profile entirely.

Any fsx app with a simulation in it has this bug or has solved it privately.
It is worth a paragraph in §53.5 rather than a slot.

## 3. `gfx_line` has no batch form — proposed, not built

§5.6.8 built `gfx_lstepv` so that N resumable *walks* could be stepped in one
arrival, and priced the arrival it saves at **128.7 µs** on the 5150 itself.
There is no equivalent for **whole lines**: a caller with a figure hands over
one segment per far call.

WIREFRAME draws twenty-four of them a frame and pays about **3.1 ms** of pure
arrival, which is 5.6% of a tick. A denser figure pays proportionally more; at
the hundred segments a frame this game draws it would be **12.9 ms**, a
quarter of a tick, to arrive.

The shape is `gfx_lstepv`'s exactly — `ES:DI` = an array of `{x1,y1,x2,y2}`,
`CX` = how many, one pen for the batch.

**But the naive version does not collect the 128.7 µs, and that is the part
worth writing down.** §5.6.8's arrival is the far-call cell *plus a prologue*,
and `gfx_line`'s prologue is not small: `cur_unlazy`, eight pushes, and
`GFXDENTER`/`GFXDORG`'s display resolution (§39.14.2). A `gfx_lines` that
simply loops over `gfx_line` pays all of that per segment and collects only
the cell — **46.7 µs**, Part 9's measured figure. Twenty-four edges of
WIREFRAME is then 1.1 ms of a 55 ms frame, which is 2%, and not a reason to
spend kernel bytes.

Collecting the rest means **splitting `gfx_line` into a public wrapper and an
inner body**, with the unlazy, the pushes and the display entry hoisted into
the wrapper and into the batch. That is the change worth making and it is a
change to a load-bearing routine, so it wants its own round: the arithmetic
above is the case for it, `tests/wirefps.py` is the instrument (it already
runs WIREFRAME twice on one boot and reads the package's own frame rate, so
the A/B is a poke and a re-read), and §5.6.8's own two `gfxbench` rows are the
shape of the measurement.

**It is not built here** for two reasons and the first is the weaker one: this
game cannot use it, being in a foreign mode where `gfx_line` is illegal. The
second is that the version that would be cheap to add is the one that buys 2%,
and adding it would make the real change harder to argue for afterwards.

### 3.1 The one defect the design's own shape produced

Worth recording beside the findings, because it is what a per-row dirty span
costs and nothing else in this tree has paid it yet. A shallow walk steps x
**after** its final plot, so a line whose last pixel is the last in its byte
leaves the byte index one past the row — and the span end with it. The clear
then wipes the next row's first byte and the blit copies it to the device.

On CGA that lands on a row about to be redrawn and is invisible. On
**Hercules**, where a 640-pixel viewport sits in a 720-pixel row, it lands in
the five bytes of margin that **nothing ever erases** — because the erase *is*
the frame's own spans — and it collects. Reported from a playtest as stale ink
down the right-hand edge; found by counting lit bytes outside the viewport,
with the player DRIVING, which is the condition that produces the case at all.

The general form: **a span-based erase can only reach what a span names, so a
mark that is wrong by one is permanent rather than transient.** Anything that
adopts this pattern wants the same one-compare clamp and the same gate.

## 4. What turned out NOT to be missing

Worth recording, because each of these was expected to be a gap:

- **`OSAPI_FONT_GLYPHS` is exactly the right shape for a foreign mode.** It
  hands over the bitmap rather than drawing it, so a panel can be lettered in
  the kernel's own 8×8 face with no second typeface and no `int 10h` probe.
  Every backend here letters through it.
- **`OSAPI_KEY_DOWN` (§9.7) is what a driving game needs**, and there is
  nothing to add: `int 16h` for what was typed, the scancode map for what is
  held, and the bracket may call both.
- **`fsx_wait`'s `[ticks]` bound is load-bearing and correct.** `fsx_page`
  reuses it rather than writing a second retrace poll, and a dead status port
  therefore cannot hang a page flip.
- **The heap claim is the right home for a shadow buffer.** 16KB, freed with
  the instance, no teardown hook — and on CGA the shadow is not a workaround
  for a missing page but the *faster* path anyway, since RAM has no wait
  states.

## 4.1 The attract window, and what the resumable walk turned out to want — BUILT (§85.10)

The queued list from the playtests is done. Two of its items were the reason
this section existed at all, and both landed in the WINDOW rather than in the
bracket — which is the interesting part, because the window is the only half of
this game that draws with kernel slots.

- **The logo is one polyline, and that is what makes the walk usable.**
  `OSAPI_GFX_LINIT`/`LSTEP` (§5.6.7) had no consumer in this package until
  here. The obvious shape — a cursor that lights one letter's edge, stops, and
  moves to the next — needs a table of where each letter begins and a special
  case at every joint. So the wireframe font was built with **each letter's
  exit point as the next letter's entry point**: `TANK ATTACK` is a single
  55-point stroke, a cursor walks the whole logo without being told anything
  about letters, and a retraced stroke inside a letter (the T's stem) is
  invisible because it lays the same ink over the same pixels.
- **The erase is the thing `gfx_line` cannot do.** A bright section that
  starts and ends *inside* a segment is not a line, so §5.6.2's promise — the
  pixel set is a property of the endpoint pair — does not cover it, and
  drawing it with one whole-line call and erasing it with another leaves a
  dashed remnant every frame. Two cursors running the same recurrence in the
  same direction agree by construction. This is exactly the case §5.6.7's own
  text describes, met in the field for the first time.
- **A walk block is ABSOLUTE, and a window MOVES.** The one defect this cost:
  the window manager can carry a window's content to a new place as a blit,
  without a `W_PAINT`. Everything the attract window holds — both walk blocks
  and the score band's byte-aligned left edge — is in screen coordinates, so
  the next band roll scrolled a rect the band no longer occupied and smeared
  one cell of it sideways. The fix is four instructions comparing the live
  content origin against the stored one on every wake; the general lesson is
  that **any cached absolute coordinate needs that compare**, and a package
  which only ever recomputes them in its paint proc has a latent version of
  this bug.
- **`OSAPI_GFX_SCROLL` is the whole cost of a scrolling list** — one blit and
  one `font_run` for the row that arrived — and its refusal path is a
  correctness path, not a fallback nobody reaches: it fires whenever another
  window overlaps the band.
- **A hi-score save file** and **an initials prompt**, apps/cyclone's shape;
  and **objects now block shots** with a once-at-fire ray test (§85.6.2).

Nothing here asked for a new slot.

## 4.2 An OR-ing raster has no way to take ink away — and every fsx app will hit this

The second round's own finding, and the most general thing in this file.

Both 1bpp backends lay a pixel with `or [es:di], al`, which is right and
deliberate: the shadow was cleared to 0 by the span pass, so there is nothing
underneath to preserve, and where two beams cross they add — which is what a
vector display does. Mode X stores, because it cannot read cheaply.

Then GAME OVER needed a **halo**: a one-pixel margin cleared around every
letter stroke so the scene stops touching the lettering. On VGA that could
have been a darker ink. On Hercules **black is the absence of a bit, not a
colour**, so there was no way to ask for it at all — the raster could add ink
and nothing else.

The fix is small once seen: a second parameter on the plot macro, three more
instantiations of the line body (**718 bytes**), and an ink whose device value
is *every bit of one pixel* rather than a colour, so the walk builds the right
mask and the erase plot ANDs its complement. The drawing path pays nothing —
the halo is reached by swapping the `[tk_lineproc]` cell for one pass.

**The generalisable part:** any app past §53.7's fence that composes with OR
has silently decided it can never un-draw anything smaller than a span. That is
fine until the first time it wants an outline, a cursor, or a rubber band.
Decide it deliberately rather than discovering it.

And the cost is real — 279 ms a frame for four passes over forty segments on a
4.77 MHz 8088 — which is what turned up the *other* finding: the settled
game-over frame was being redrawn continuously although nothing on it could
change. Stopping that (SPEC.md §85.8.1) took the halo from a per-frame cost to
a per-keystroke one.

## 4.2.1 The KERNEL draws in a foreign mode too, and nobody had checked

The one finding here that is not the app's fault.

§53.7 says no kernel drawing slot is legal past `fsx_mode`, and every fsx app
obeys it. What nobody had asked is whether the **kernel** obeys it — and it did
not. §12.8's file-progress widget is stepped from inside the disk transfer
loop, and a package writing a file inside its bracket (which §53.7 permits: it
forbids drawing, not the file API) armed it. It then composed a menu bar at the
desktop's coordinates, in the desktop's colours, into the app's raster — and
nothing took it away, because the app's erase is its own dirty spans and it
never knew those bytes were dirty. Reported from a 5150 as a strip sitting on
top of a game's HUD for the rest of the session.

Two fixes, and the second is the one worth remembering:

1. `fpg_arm` gains a fourth refusal, `[fsx_cur] != 0xFF`. It belongs in the
   widget and not at the call sites, because the disk path has no business
   knowing which video mode is set.
2. **`fsx_mode` tears down anything already up**, before it touches a mode
   register — the widget may have been armed by a file read *earlier in the
   same gfx-lock hold*, the hold the bracket inherited from the callback that
   started it. And it calls the widget's own `fpg_finish` rather than zeroing
   its state, because bit 1 of `[fpg_on]` is a cursor debt: clearing the byte
   would leave `cur_level` negative and the arrow gone for the session.

**17 bytes**, and it is worth asking the same question of anything else the
kernel draws on its own initiative — the toast (§59.9), the blanker (§64) —
rather than waiting for each to be reported.

## 4.3 The player moved at the frame rate and the world at the tick rate

Not a gfx finding, but it came out of the same playtest and it is the one most
likely to be repeated. §2.1 of this file is the win: *pace the simulation, not
the frame*. It was applied to the world and **not to the input**, which went on
reading held keys and applying them once per `tk_input` call — once per frame.

So on a 386 the player turned at about 1.6× the enemy's rate and on the 4.77
MHz 8088 at about a third of it, which is exactly how it was reported: *"on a
386 I can usually make my turns and shots; on a 5150 I usually cannot."*

**If you split simulation rate from frame rate, the input is part of the
simulation.** Latch it in the frame and spend it in the step.

## 5. The one measurement worth taking next

Whether the shadow-plus-span-blit path beats direct VRAM drawing on a **real**
CGA by more than it costs. Here it is a necessity — mode 4 uses 16,000 of a
CGA's 16,384 bytes and there is no second page — but the profile says the blit
and the span clear together are **20%** of the frame, against a walk that is
21%. On a real 5150, CGA VRAM contention is the reason this trade might be
free or better; MartyPC models the CPU exactly and the card's contention
approximately, so this is a docs/FIELD-MACHINES.md question and not one this
container can answer.

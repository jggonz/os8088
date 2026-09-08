# Paint's stroke: what was wrong, what was fixed, what is open

A handoff. The subject is why a freehand stroke in `apps/paint` did not follow
the hand on a 4.77 MHz 8088, what each fix was worth, and the two things still
on the table. **SPEC.md §42.8.3 – §42.8.8.2 are the binding record**; this file
is the map across them, the refusals, and the instruments, so that a session
picking this up does not re-run the dead ends.

Everything here was measured on `os8088_5150_herc_gla` under MartyPC — a
cycle-accurate 4.77 MHz 8088 — with the 8 px nib unless said otherwise. The
instrument is deterministic: two runs of `chordcost.py` on one binary agreed to
a single cycle.

## 1. State

| branch | at | what |
|---|---|---|
| `elendilon-new-2` | `f4faa84` | the wobble fix, merged, gated, pushed |
| `paint-stroke-cost` | see below | nine commits on top, plus merges of `elendilon-new-2` and `damage-repair` |

`paint-stroke-cost` commits, oldest first:

```
5d34abb  a stroke ended at the last SAMPLE, not at the release   (42.8.4)
98e2938  bank a run of leading edges into one rect               (42.8.5)
4fb1227  block-granular undo: built, measured, REFUSED           (42.8.6)
67b193a  a diagonal step's two leading edges are one dab         (42.8.7)
e979cd7  the canvas catches up at the release                    (42.8.8)
1a41305  pay the bank down in the idle turns                     (42.8.8.1)
837610b  the undo save moves up a level, to once per CHORD       (42.8.8.2)
be928b0  a handoff, and what "756 us" actually is        (this document)
    +    a run of scan lines in one call                 (5.10, 42.8.9)
```

`make test-full` is **green — 25 passed, 2 skipped** (`ctoolchain` and
`ps2mouse`; the C compiler and QEMU are not installed in the container). The
`buildmatrix` row failed for one commit here and `damage-repair`'s merge fixed
it, section 5.1 below.

## 2. What a chord costs now

One 8-step chord, 8 px nib, bracketed exactly by `pt_seg` / `pt_seg.out`:

| | at §42.8.3 | now |
|---|---:|---:|
| straight down | 9.28 ms | **2.51 ms** |
| straight across | 4.16 ms | **2.85 ms** |
| **45°** | **36.16 ms** | **13.03 ms** |

and the release settle after a 25-sample diagonal stroke driven at the wire's
own 40 reports a second: **357.9 → 113.0 ms**, the deferred half **14.29 → 4.52
ms a sample**.

**The number that decides whether the ink keeps up** is 13.03 + 4.52 = 17.5 ms
of work against a 25 ms report interval. Before this work a 45° chord was 27 ms
against 25 and the machine was oversubscribed; it is not any more.

## 3. The five fixes, and what each was actually worth

**3.1 The wobble was not lag at all (§42.8.3).** `pt_seg` kept the Bresenham
denominator in **CX, which is also what `loop` counts down**, so the
denominator shrank every iteration and the minor axis stepped ever more often
toward the end of a chord. A chord of dx=1, dy=8 stepped x four times and
landed 3 px past its own target; the next sample pulled it back and it
overshot the other way. **Ink where the hand never went, not ink arriving
late.** Fixed by moving the denominator to BX: +2 bytes. This is the one that
mattered — everything after it is speed.

**3.2 A stroke ended at the last SAMPLE (§42.8.4).** Both tracking loops read
the pointer and the buttons from one `OSAPI_MOUSE` and left on the button
without spending the position that came back with it. `pt_rubber` was worse:
its loop opens with `pt_wait_tick`, so a rectangle ended up to a whole tick
short of the corner it was released on. Measured: ink stopped 46 px short at
640 px/s; now lands on the pointer.

**3.3 Banking a run of leading edges (§42.8.5).** A run of y-steps the
Bresenham does not interrupt is a stack of identical rows at one x, i.e. a
rectangle. Calls `ddy + ddx` → at most `2*ddx + 1`, gated on `ddx + 1 < ddy` so
it is **never worse**. Fan of twelve 90 px rays: **1,341 → 723 `pt_rect`
calls**. +348 bytes of package code.

**3.4 A diagonal step is one dab (§42.8.7).** At a diagonal step the two
leading edges are an L and the brush's own square contains it — the extra
pixels are already that colour, and `pt_rect` is priced by its fixed preamble
and by `gfx_fill`'s per-SCAN-LINE cost, not per pixel. 16 calls → 8, 36.16 →
29.03 ms. **This is the exact inverse of the trade `pt_seg` was built on and
both are right**: for an axis-aligned step the edge is one call and so is the
dab, so the pixels buy nothing; for a diagonal one the edges are two calls.
The rule is per STEP, not per brush.

**3.5 The canvas catches up at the release (§42.8.8, .1, .2).** Two thirds of a
diagonal chord was the app's own bookkeeping, so it moved out of the
button-down path: the stroke writes the SCREEN only and banks its POINTS; the
canvas and the undo image are replayed through the same walk afterwards.

**The ordering is the whole idea.** Undo needs the pixels that were there
before the ink and drawing destroys them, so "record points, save at the end"
is impossible if the canvas has been written all along. Deferring the canvas is
exactly what makes the deferral legal.

Then §42.8.8.1 pays the bank down in `pt_stroke`'s idle turns (which §42.8.1
was spinning away) and §42.8.8.2 hoists the undo marking to **once per chord**,
which is the amortisation that finally makes §42.8.6's blocks pay.

## 4. Refusals — do not rebuild these

**4.1 Block-granular undo, on the hot path (§42.8.6).** Built, worked exactly
as designed (32-byte blocks, 16 words copied where 112 were), and **lost**:
1.45x on a vertical chord, 11% worse across, 4.8% worse at 45°. The per-call
bookkeeping cost about what the shorter copy saved. That is PERFORMANCE.md Part
2's fetch floor — on an 8088 an added instruction is priced by its ENCODED
BYTES as much as its clocks, so a model counting clocks alone predicts a win
that does not arrive.

**4.2 The same blocks, moved to the flush (§42.8.8.1).** The argument was good
— the hot path no longer marks at all — and it still did not compound: 145.6 ms
of settle without, 141.0 with, **worse per banked sample** (6.72 against 5.82).
Moving *when* they ran did not change *what they were*: still one mask
computation and one block scan per `pt_rect` call.

**4.3 What did work is the third placement (§42.8.8.2)**: once per CHORD.
6.96 → 4.52 ms a banked sample. The lesson is that the amortisation base
matters more than the mechanism — primitive calls, then primitive calls again,
then chords.

## 5. Open

**5.1 The screen half — a sweep primitive. BUILT (SPEC.md §5.10, §42.8.9).**
`OSAPI_GFX_SPANS` fills a run of consecutive scan lines, one x interval each,
in one call; Paint derives the list from the chord's own Bresenham and emits
it once. **A 45° chord's screen half is 15.19 → 9.44 ms and the release settle
112.2 → 72.4.** The pixels are byte-identical — `tests/paintundo.py` returns
the hash it did before, on Hercules *and* VGA.

**Everything this section predicted about the shape was right and every number
in it was wrong**, in both directions, which is worth reading before trusting
the next estimate on this page:

- *"A span list is WORSE than what we do now"* — false, and the reasoning
  ("`gfx_fill` is priced per CALL") was the right rule applied to the wrong
  quantity. It is priced per call **plus per row**, and the dabs draw 64 rows
  where the spans draw 16. Measured: 16 spans are 26,207 cycles against the
  eight fills' 45,600.
- *"~400 cycles a row, ~4x"* — the row is ~450, but the first build reused
  `sw_rect_pl` and paid **1,806**, because that is a *rect's* body and a span
  is one row (SPEC.md §5.10.4). With its own row writer it lands at **1,638
  cycles a span**, and the chord at 1.61x rather than 4x.
- *"~250–400 bytes, 1bpp-gated with VGA falling back"* — it is **494 of
  `.text` and 12 of `.bss`**, and it is not gated: both renderers have a fast
  path, because on VGA a fill's fixed cost is *larger* and falling back to
  dabs would have made VGA the only adapter the feature skipped.
- *"a rasterization mismatch is live rather than theoretical"* — correct, and
  §42.8.9's answer is that the generator uses `pt_seg`'s **own** recurrence
  step for step. `paintundo`'s redo check was the gate, exactly as predicted,
  and it earned its keep on a different bug: `gfx_sp_next` left `ES` on the
  caller's span list, so the row writers drew the picture into Paint's own
  image (SPEC.md §5.10, §77.10's shape one layer down).

**What it cost.** `.text` +494, `.bss` +12, `KERN_SIZE` 118,784 → 119,296 —
one image rung, **512 bytes of every machine's RAM**, which the owner
pre-authorised. The shipped kernel clears guard 5 (`MIN_RAM_KB`) by 1,024
bytes. **The KNOB kernels do not** on this branch: `make BAND=1` and
`make DISKCNT=1` are 512 over, because `BAND=1` was sitting at **exactly** the
ceiling — 126,976 of 126,976, zero slack — so any byte added anywhere to the
shipped kernel breaks them.

That was worth understanding rather than working around, because the guard's
own comment in `kernel/kernel.asm` had already named this failure mode —
*"that is the guard steering the wrong thing"*, about `BOOTMARK=1` and
`BOOTHALT=20` forcing a 141-byte fix down to 38 — and `damage-repair` is the
branch that acted on it: `MIN_RAM_KB` is **196 KB for `kern_big`** and 128 for
`kern_small`, on the finding that *boot-time reach* and *resident footprint*
are two questions one constant was answering, with `KERN_BUDGET` then derived
from a `KERN_RESIDENT_KB` of 128. **It is merged here and `buildmatrix` is
green**; nothing in this feature was cut for it. `kernsize` now prints both
quantities, and the numbers this branch stands at are **`KERN_SIZE` 119,808 of
a 129,536 budget — 9,728 spare — and 70,144 before it could not boot on
196 KB.**

**5.1.1 The scratch round trip. DONE, and it was free.** `gfx_sp_next` now
answers in **registers** — `AH`/`AL` the masks, `BX` the span, `DI` the byte —
instead of writing four `vga_*` words for a row writer three instructions away
to read back. **1,638 → 1,393 cycles a span, and the kernel is 57 bytes
SMALLER** (SPEC.md §5.10.2). The estimate above said "worth perhaps 300 cycles
a span, and it costs `.text`"; it was 245, and it cost negative bytes, because
the registers also retire the interior's reload of the left byte — it steps
*back* over itself from the right edge instead (§5.7.1's shape). A version
fused per renderer would have carried two copies of the clip and the mask
lookups; this carries none.

**What is left in the screen half.** Of an armed chord's **8.61 ms**, 4.67 is
`gfx_spans` and 3.94 is `pt_spgen`. Inside `gfx_spans`: **4,957 cycles of
entry**, nearly all of it the cursor hide — once per lock hold, and not this
primitive's to avoid — then 1,086 a span. So the two remaining targets are
`pt_spgen`'s two passes, which are **package** bytes and cost the kernel
nothing, and the per-span 1,086, which is now mostly irreducible work: a list
read, four compares, two table reads, six shifts and the masked bytes
themselves.

**5.1.2 `gfx_spans` is gated only through Paint. DONE** — `tests/spantest.py`
and `tests/spantest/` now cover the rest of §5.10's contract (SPEC.md §5.10.5),
on both adapters, and were checked against two mutants. The paragraph below is
the record of what was missing.

~~ `tests/paintundo.py` on both
adapters is what proves the pixels, and it is a strong gate — the redo hash
compares the span-drawn screen against a canvas the untouched walk wrote — but
it only ever exercises the shapes Paint asks for. **Four behaviours in §5.10's
contract have no test**: the three refusals (an armed clip region, a second
display, `kern_small`), an empty row (`x1 > x2`), the vertical clip (a run
starting above the screen or running past the bottom), and a middle grey's row
alternation. A second caller — the game the primitive was generalised for —
would meet all of them. A `tests/spans/` package drawing one shape twice, once
through `OSAPI_GFX_SPANS` and once through a `GFX_FILL` a row, and diffing the
two regions, is the gate that closes it.

Note also that **`tests/paintwalk.py` does not reach the generator**: its chords
are dx=1, dy=8, which §42.8.5's gate collapses, so all eight are unarmed. The
arrival invariant is trivially true for `pt_spgen` (it assigns the target
outright), so this is a gap in coverage rather than in correctness — but a
`--diag` mode on that row would be worth having.~~

**5.2 The residual settle.** **72.4 ms** after a 25-sample *saturated*
diagonal — the metronome case, and 5.1 took it down with the screen half
exactly as predicted: nothing in §42.8.8 changed, but the screen finishing
sooner is more idle time, and §42.8.8.1 spends idle time paying the bank off,
so a banked sample costs 4.49 → **2.90 ms** at the release.

**5.3 ~~The undo save, once per ROW rather than once per chord.~~ REFUSED,
for two independent reasons, and the second one is that it cannot be built as
written.**

**It targets 5.3% of the settle.** The release flush, split by where the
cycles go (one chord's replay, 57,179 cycles = 12.0 ms on a 4.77 MHz 8088):

| | cycles | |
|---|---:|---:|
| `pt_rect` — writing the canvas | 33,781 | **59.1%** |
| `pt_ucopy` — the undo copy itself | 13,252 | **23.2%** |
| `pt_umark_chord` | 1,717 | 3.0% |
| `pt_umark_b` | 1,307 | 2.3% |
| `pt_seg` | 1,516 | 2.7% |

The *marking* is the 5.3% at the bottom. The copying above it is **already**
once per row — `pt_umask` is what stops a second chord re-saving a row it
shares — so what 5.3 proposed to amortise was the cheapest thing in the
flush.

**And the single pass it assumes no longer exists.** 5.3 was written before
§42.8.8.1, which pays the bank down **one chord at a time** in the idle turns
(`pt_idlepay` calls `pt_flush_n` with `CX = 1`). There is no moment at which
the whole stroke's geometry is in hand: by the release, most of it has already
been replayed and dropped. The premise was true of the design 5.3 was written
against and false of the one that shipped.

**5.3.1 What the same idea is worth applied to the 59%.** The intent behind
5.3 above — *amortise per row, not per chord* — is right; it was aimed at the wrong
term. `pt_rect` writes **eight canvas rows per dab** and a chord's six dabs are
48 row-writes for a union that is about fifteen rows, which is §42.8.9's
argument one image along: the screen half took the same medicine and went 2.05x.
Paint already generates that span list (`pt_spgen`) and already has it tested.

Costed at a chord's replay going from 33,781 cycles of `pt_rect` to roughly
15,000 — **12.0 ms → ~8 ms a chord** — for a second row-writer of ~100 lines
with `pt_rect`'s mask arithmetic per span rather than per rect (§5.10.4's
lesson: the per-row setup is most of it, so reusing `pt_rect` per span buys
almost nothing), **packed-only**, the planar canvas a VGA carries keeping the
walk.

**BUILT** (SPEC.md §42.8.9.2). `pt_cvspans` is that writer; `pt_sparm` arms it
on `[pt_defer]` clear, `[pt_noscr]` set, `[pt_uoff]` set and `[pt_planar]`
clear, and `[pt_spxo]` is the one word that makes `pt_spgen` emit canvas x
instead of screen x. `tests/paintundo.py` returns the same redo hash on both
adapters.

**What it actually did, and where the estimate was wrong.** Measured by
bracketing every `pt_flush_n` across a whole stroke rather than the one left at
the release — 6 px a report at 25 ms, an 8 px nib, 14.9 output rows a chord
against 7.2 `pt_rect` calls of eight rows each:

| a chord's replay | before | after |
|---|---:|---:|
| the canvas half | 70,575 cyc — 14.79 ms | **53,077 cyc — 11.12 ms** |
| the drawing | 46,099 (`pt_rect`) | **12,458** (`pt_cvspans`) |
| deriving the list | — | 16,544 (`pt_spgen`) |
| the whole stroke's canvas half | 295.7 ms | **233.5 ms** |

**3.7x on the drawing, 1.33x on the half.** The estimate said ~33% and the
answer is 25%, and the gap is one term: **`pt_spgen` costs 16,544 cycles**,
not the ~5,000 the costing assumed, and the replay pays a *second* one because
the live screen pass already spent the first. It is ~1,100 cycles an output
row, which is more than writing the row costs.

Two things worth writing down because they are not what the costing predicted:

- **Per row, `pt_cvspans` is SLOWER than `pt_rect`** — 890 cycles against 800.
  The masks are computed per span instead of hoisted per rect, and a span is
  wider than a dab. It wins purely on the count, 14.9 against 57.6, which is
  the only thing §42.8.9 ever claimed. Had the row counts been closer this
  would have been another Set 88.
- **The release flush is now usually EMPTY.** The instrument that produced
  5.3's table above measured "the last `pt_flush`" and now reads 719 cycles:
  §42.8.8.1's idle turns keep up, so there is nothing left owing at the
  release. That is the feature working, and it is also why the before/after
  here had to be re-measured with a bracket that covers the whole stroke.

**5.3.2 …and then `pt_spgen` itself.** Not another writer: 31% of the half,
paid on *both* halves, and its second pass recomputed
`clamp(r ± nib − wkmin, 0, ddy)` from scratch every row when both bounds
advance by exactly one row at a time. **BUILT** (SPEC.md §42.8.9.3): the two
window ends walk in `BX` and `SI`, one `add 4` and a ceiling each, and the
lower clamp becomes a hold count `pt_wkseq` returns beside the starting offset.

| | before | after |
|---|---:|---:|
| `pt_spgen`, a chord | 16,544 cyc | **11,827 cyc** |
| the canvas half, a chord | 11.12 ms | **10.14 ms** |
| the live 45° chord's walk | 3.99 ms | **2.99 ms** |
| straight down / across | 10,955 / 11,782 cyc | 10,955 / 11,781 |

The straight rows being unchanged **to the cycle** is the check, not a
footnote: they are unarmed, so `pt_spgen` never runs on them.

Taken with 5.3.1 the replay is **14.79 → 10.14 ms a chord, 1.46x**. What is
left undone deliberately is a middle loop with the hold, the ceiling and the
canvas clip all lifted out — ~219 cycles a row against ~445 — because the four
boundaries are four more derivations of the same geometry, which is §42.8.3's
warning, for ~3,000 cycles a chord a half.

Nothing on the canvas list below touches any of this.

**5.5 THE OWNER'S CANVAS-REDRAW LIST.** A different subject from this
document — none of it is about a stroke — but it is written down here so it is
not lost, and it will move to its own plan when the first of it is measured.
**Nothing below has been measured yet**, and the notes are a first read only:

1. **~~Store the canvas 1bpp when the picture is black and white~~ — VALID,
   MAYBE SOMEDAY, NOT TODAY.** On a new image, or a loaded one with no other
   colour in it, and on a mono adapter: the canvas is packed 4bpp or four
   planes (§42.13) and every repaint decodes, where at 1bpp on a 1bpp screen
   the decode disappears and a repaint is `gfx_blit1`/`gfx_blitp`-shaped. It
   would need a fact tracked on load and invalidated the first time a third
   colour is drawn.

   **The idea is sound and the NEED for it has mostly gone**, which is a
   different thing from the idea being wrong — the owner's reading, and it is
   four separate reasons rather than one:

   - **The first draw does not benefit.** Transposing to planar costs about
     what drawing it through the fast `gfx_blit4` costs, at least on VGA. The
     conversion is the price of the *second* repaint onward, not the first.
   - **There is not usually a second repaint.** Once Paint's mono save-under is
     working again, a covered window comes back from the cache and never
     redraws its canvas at all. That is the case this was for.
   - **An extended desktop cannot use planar anyway** — a straddling window is
     one of `gfx_blitp`'s refusals (§5.4.3, and §5.4.3.3 is the one refusal
     that turned out not to be about the machine), so there is no benefit
     there to win.
   - What is left is **the redraw on a maximize**, and **enough RAM to load the
     image but not enough to take the save-under**. Both are real, and neither
     is enough to upend the whole mono draw path today.

   Revisit it if the save-under stops covering the common case, or if a
   measurement makes the maximize redraw the thing that hurts.

   **REOPENED, on a reason this entry does not contain — see
   docs/plans/completed/PAINT-1BPP-PLAN.md.** All four bullets above are about SPEED, and the
   thing that reopened it is MEMORY: a 1bpp canvas is a quarter the claim, and
   the 128KB floor machine's 13.5 KB of free heap funds 61 rows of a 448-wide
   canvas at 4bpp against **245** at 1bpp. That is the difference between the
   letterbox SPEC.md §42.6.5 cuts and the full Hercules default.

   **The third bullet is also WRONG and is corrected there.** A straddling
   window is one of `gfx_blitp`'s refusals; it is *not* one of `gfx_blit1`'s —
   that routine detects the seam and falls to `.percol`, drawing per band
   column with each column resolving its own display (SPEC.md §39.14.6). So an
   extended desktop is the one place a 1bpp canvas has a fast path and a planar
   one can never have one. The other three stand, and the new plan's §5 agrees
   with this entry's conclusion about speed on this entry's own numbers: the
   maximize is a predicted 1.107 → ~0.66 s, and `pt_draw_pal`'s 0.251 s is
   untouched by any of it.
2. **~~Then store a grey as its actual dither~~ — the same answer**, since it
   follows from 1 and is probably part of it. §39.4 reduces a grey to a dither
   at draw time anyway, so storing it that way would make the canvas truthful
   rather than a colour the machine has no access to; but it buys nothing on
   its own, and 1 is what would carry it.
3. **~~Does the planar conversion fire on a mono → mono resize?~~ NO —
   MEASURED, and the real bug was next door.** `pt_topacked` and `pt_toplanar`
   run **zero** times across open → draw → drag → maximize → restore on a
   Hercules: `pt_geom` picks packed on a 1bpp adapter and nothing moves it.
   What a maximize *does* do is **grow the canvas** — 448×258 → 670×258 — which
   is a real `pt_resize`, and that is the second the eye sees before Paint's
   interface draws. Chasing it found SPEC.md §42.8.6.1 instead: `pt_ucopy` and
   `pt_uswap_row` walked all eight undo blocks of a row that had six, clipped
   each block's END at the stride and never its START, and copied **two 64 KB
   `rep movsw` per row** on the two that did not exist. **97 seconds**, and a
   band of whatever was past the undo image written across the bottom of the
   picture — in the canvas, so it survived a save. `tests/paintsize.py` is the
   gate.

   **WHERE THE MAXIMIZE ACTUALLY GOES** — measured on the current tree, a
   Hercules, 448×258 → 670×258 with one stroke on the canvas, every phase
   bracketed and nothing left unattributed:

   | | s | |
   |---|---:|---:|
   | `pt_free_undo` + `pt_free_clip` | 0.023 | |
   | `pt_layout` | 0.018 | |
   | **`pt_wipe`** | **0.148** | 13% |
   | **the copy-back** | **0.228** | 21% |
   | `pt_alloc_undo` + `pt_alloc_clip` | 0.027 | |
   | — `pt_resize` — | *0.446* | *40%* |
   | `pt_fsbed` | 0.041 | |
   | **`pt_draw_pal`** | **0.251** | **23%** |
   | `pt_szdraw` | 0.037 | |
   | **`pt_draw_strip`** | **0.132** | 12% |
   | **`pt_blit`** — the canvas | **0.200** | 18% |
   | **the whole maximize** | **1.107** | |

   **The resize is not the biggest half of it.** Redrawing Paint's own
   interface is 0.461 s — as much as `pt_resize` — and `pt_draw_pal` alone is
   the largest single item in the operation, larger than the wipe, the copy or
   the canvas blit. That matches what the owner watches: chrome, a pause,
   *then* the interface, *then* the picture.

   **Three redundancies are visible in that table without any new design:**

   - **The wipe and the copy-back overlap.** `pt_wipe` writes all 86,688 bytes
     of the new canvas and the copy-back immediately overwrites 57,792 of them —
     **67% of the wipe is thrown away**, about 0.099 s.
   - **The copy-back carries ground onto ground.** §42.18's table already knows
     which bands hold ink; with one stroke that is ~64 rows of 258, so about
     **75% of the copy** is moving `[pt_blankc]` on top of `[pt_blankc]`,
     about 0.17 s. Wiping with `[pt_blankc]` instead of `CWHITE` is what makes
     skipping it correct for any ground colour.
   - **The palette did not move.** The tool column is anchored LEFT and this
     maximize did not change the height at all (258 → 258), so `pt_draw_pal`
     redrew eight buttons and sixteen swatches that were already exactly right,
     for 0.251 s. Only the bed's new columns needed anything — which is #5 and
     #7's own argument applied to the interface rather than to the canvas.

   So roughly **0.27 s of `pt_resize`'s 0.446 is provably redundant**, and
   `pt_draw_pal`'s 0.251 is very nearly all of it. **This is a measurement of
   the tree as it stands and NOT a comparison against `main`'s tip** — it says
   where the time is now, not which commit put it there.

   **What is LEFT of #3 is a different question, and it is PUNTED.** With the
   97 seconds gone, the canvas resize a maximize performs still costs about
   **three quarters of a second more than it did at `main`'s tip** — the owner
   watches the chrome draw maximized, then a pause, then Paint's interface.
   That is a regression somewhere in the range between the two, and finding it
   is a bisect rather than a reading. **Take it either as the LAST item on this
   list or immediately before #7**, never in front of the cheap wins: it is
   open-ended, and #4 and #5 may make the resize's repaint small enough that
   the three quarters of a second stops mattering.

   One small residue, unrelated and not the 0.75 s: `pt_onresize` arms
   `[pt_wantpl]` unconditionally, and on a 1bpp-only machine the probe can
   never say yes, so the flag stays armed for ever and every `pt_blit` after
   the first resize pays a spare `OSAPI_GFX_BLITP` far call. Tens of
   microseconds, not a rung.
4. **~~An empty canvas is one `gfx_fill`~~ DONE (SPEC.md §42.15).**
5. **~~A drag-resize LARGER is two blank rects~~ DONE, in the same mechanism.**
   `[pt_iall]` plus an **inked bounds** rect: 1 means nothing is known and a
   repaint decodes the lot; 0 means every pixel outside the bounds is
   `[pt_blankc]`. `pt_blit` intersects its clipped rect with it and gets three
   behaviours out of one mechanism — an empty bounds is one fill (#4), a
   sub-rect fills the four bands around it and decodes the middle (#5), and the
   whole canvas is today's single decode.

   **#5 is NOT an OS ordering fix, checked**: `wm_rz_paint` calls
   `wm_draw_win` on a grow, and that hands the app a `W_PAINT` for its whole
   content with no damage rect and no narrowed clip — which is the minimum the
   window manager can safely do, because the app may re-lay out at the new size
   and Paint does. The knowledge that the old area is unchanged is the app's.
   **#6 is the same shape**: on a shrink the kernel repaints the union of old
   and new, and the surviving window's content is already-correct pixels, but
   the kernel cannot know that while the app is free to re-lay out. So #6 is
   not unlocked by this and would need a way for an app to say *my content did
   not change* — an API question, not an ordering one.
6. **~~A SHRINK needs no redraw at all~~ — DONE, both phases.** It was about
   kernel message ordering, as this entry guessed: `ui_grow` repaints the union
   of the rect the window had and the rect it has, so on a shrink the window
   was told *all of you* about pixels nothing had painted over. SPEC.md
   §11.90.3 arms `[wm_dmg_rzwin]` across that one repaint and `wm_damage`
   answers the shrunk window with an empty rect; what MOVED stays the
   application's, because only it knows its strip is anchored. Measured on a
   Hercules, 448×258 → 392×218: the canvas blit **0.639 s → skipped**, the
   whole `W_PAINT` **1.820 → 1.180 s**. `tests/paintanchor.py` is the row.
   What is left of the shrink is `pt_resize` at **1.094 s**, which is #3's
   punt and not this.

   **PHASE 2 IS ALSO DONE** (SPEC.md §42.17) and is a different question in the same
   code: *if a shrink would destroy artwork, shrink as far as it can instead of
   refusing that axis.* §42.9's refusal pinned the axis where it started, so
   one stroke 120 px from the edge kept the whole width. It is now a binary
   search over `pt_lose_w`/`pt_lose_h` — monotone predicates whose two ends are
   already known — which reuses both storage formats' scanners rather than
   writing two more. Measured: over ink reaching column 168 and row 128, a
   request of 60 gives the width back to **169** and a request of 40 the height
   to **129**. `tests/paintshrink.py` is the row, and it drives the SIZE BOXES
   rather than the grow box because the window's own minimum stops a drag long
   before the ink does.
7. **~~A table of empty rects on the canvas?~~ — BUILT** (SPEC.md §42.18), and
   it cleared the bar this entry set: the writers do maintain it, and what they
   pay is a compare and a store per BAND per axis, once per rect.

   **The measurement is what shaped it.** #5's single bounds is switched off by
   any one pixel, so a full repaint of a canvas with one stroke on it cost
   **0.880 s** where the same canvas blank cost **0.069** — 98% ground, paid
   for in full. It is now **0.181 s**, a 4.9x, and the blank case is unchanged.

   **A table of ROW BANDS, not of arbitrary rects**, 16 rows each with its own
   inclusive x range. Bands because a blit is priced per row and per pixel with
   a small fixed part, so cutting on the row axis costs one call per band and
   nothing else, where cutting on both would fragment the loop that does the
   work — and because the x range inside a band is what makes a DIAGONAL cheap,
   which one bounds rect can never be. Adjacent bands with the same answer
   merge before anything is drawn, vertically only: merging horizontally means
   decoding the union of two x ranges, and at 7.1 µs a pixel against a call's
   fixed part that is a loss past about fifty columns.

   Of the owner's four options this is the *cheap* one and the *colour* one
   together — a band carries a range and the ground is `[pt_blankc]` — and
   neither *regen* option was needed: a band that stops being inked stays
   marked, and the cost of that is bounded by the band, not by the canvas.
   +308 bytes.

   **A SUB-STEP IS OWED: the STRADDLE, and a test for it.** SPEC.md §39.11.0 is
   the finding — two cards of one kind is not a configuration any PC can have,
   so both halves of a two-display machine always differ and a window across the
   seam is always the restrictive case: `gfx_blitp` refuses it and `gfx_blit4`
   sends it to `.slow`, one `gfx_fill` a run. That makes the straddle where this
   table pays most, and it is unmeasured — two attempts here got the window
   across the seam and neither produced a canvas blit to time, which is itself
   worth understanding.

   It is unmeasured because **nothing tests multi-display at all**, which is why
   defects keep arriving there and being found by hand. The step is one row:
   extend the desktop, drag a window across the seam, force a repaint, compare
   both cards' framebuffers against the same view drawn on one — after which
   every later change is covered without anybody remembering to look.
8. **~~"Save Changes?" on close, if the canvas was touched~~ — BUILT**, and
   the owner's **three follow-ups on it are DONE**. It does use the standard
   buttons, and two of the three were the shared control's rather than Paint's
   — which is the point of a shared control, in both directions:

   - **the mouse-off un-fill** — the alert installed `W_ONMOUSEUP` and nothing
     between the press and the release, so a button held and dragged off stayed
     filled. §13.8.2 built `W_ONDRAG` for exactly this and the alert never asked
     for it (SPEC.md §75.3.0).
   - **the whole row refreshing on mousedown** — `os88ui_adn` called
     `os88ui_abtns` for a state change that can only touch two buttons.
     Measured on the old code: **three `os88ui_btn` calls for one press**, one
     now.
   - **GIF for a new canvas** (SPEC.md §42.16.1) — and `pt_load` now sets
     `[pt_sfmt]` from the magic it decoded, which is the half that makes the
     default safe: without it, opening `FOO.BMP` and closing it would have
     written `FOO.GIF`, because §42.16's Save coerces the name to the flag's
     extension.

   `tests/alertbtn.py` is the row, and it fails on the old code with the
   owner's own numbers. Otherwise as below.
   (SPEC.md §42.16), and it departed from this entry twice, both deliberately
   and both written up there: a **resize does not** set the flag (it changes
   the document, but then open/maximize/restore/close would ask about a blank
   picture nobody drew on), and a refused `os88ui_ask` leaves Paint **open**
   rather than saving, because a picture usually has no name to save under and
   writing `PICTURE.BMP` into the current folder unasked is the worse outcome.
   The rest of this entry is what shipped. **Every mechanism for it already
   existed**: `OSAPI_WM_ONCLOSE`
   (0x0468) is §75.1's close negotiator, `os88ui_ask` with `OS88UI_ASAVE` puts
   the question up, and §75.3's rules about an alert already being up are
   written. Paint registers no close proc at all today.

   **Copy `np_onclose`'s shape and NOT `np_dirty`.** Notepad decides by
   CHECKSUMMING the document, which is right for a few KB of text and wrong
   for 57–87 KB of canvas — the same reason a uniformity scan lost to a flag
   in #4. A flag set where `[pt_iall]` is set is the answer.

   It is **not** `[pt_iall]`, though, and the difference is the whole design:
   `[pt_iall]` is cleared by `pt_wipe` and re-established by `pt_resize`,
   because it is about what the canvas LOOKS like. Dirty is about the
   DOCUMENT — a resize changes it (the dimensions are saved), New and Open
   and Save clear it, and undoing back to the start does not (Notepad's
   checksum does notice that; a flag does not, and that is an accepted
   difference rather than a bug to design around). So: the same six routines,
   one more byte store each, plus `pt_resize`, and cleared at Save/New/Open.

   §75.1's fallback rule applies unchanged and is the part not to improvise:
   `os88ui_ask` refuses when an alert is already up and ALWAYS on `kern_small`,
   and the documented answer is to **save and close anyway** rather than to
   become unclosable.
   Treat with suspicion until measured: §42.8.9.1 in this same document is
   exactly this trade going the wrong way — a min and a max per row cost what
   the drawing cost, and the two cancelled to within 1%. If it can be made to
   work it will be at a COARSE granularity (a byte per block, marked by
   something already walking the blocks, like `pt_umark_b`) rather than a rect
   list maintained per operation.

**5.4 `PT_BNKMAX` is 64 samples.** Overflow spends exactly one sample and
compacts, which is the undeferred cost for that one report and degrades
gracefully. Flushing the whole bank there would be a ~900 ms hitch in the
MIDDLE of a stroke.

## 6. The instruments, and the traps they cost

**The hand must be paced on the GUEST clock.** `os88mouse`'s wall-clock path
costs ~0.51 guest seconds a report (§7.3.1), so a scripted stroke arrives at
about two reports a second against a hand's forty, every chord is a pixel or
two long, and **both builds draw the same smooth picture**. Pacing packets with
`m.advance(cycles=...)` — one per 25 ms of guest time, which is what 1200 baud
carries — reproduced §42.8.3 on the first chord. That is the instrument
SPEC.md §42.8.2 said did not exist.

**The release has to be guest-paced too.** `mo._edge(False)` costs ~0.5 guest
seconds during which `pt_stroke` keeps drawing, so a wall-clock release lets
the loop catch up completely and **no lag can ever be measured**.

**A window drag does NOT reliably repaint the canvas.** `tests/paintdraw.py`
uses one, but on a 1bpp adapter a short move can be served without repainting
the content at all, and `[pt_cx0]`/`[pt_cy0]` are then whatever the last
`pt_org` left — which reads as a canvas mismatch when it is a stale sampling
origin. It cost a session's worth of chasing before a screenshot showed the
picture was perfect. **Use `tests/paintundo.py`'s redo instead**: `pt_undo_swap`
repaints the touched rows OUT OF the canvas, so it is a real canvas oracle.

**Hash a rect INSIDE the canvas.** `cw x ch` from `[pt_cx0]` runs past the
window on a 720 px screen and takes in desktop and chrome — a "blank" canvas
held 2,754 black pixels.

Rows added: `tests/paintwalk.py` (the arrival invariant — nothing in `pt_seg`
assigns the destination, so the brush at the head of each chord must equal the
target of the one before it) and `tests/paintundo.py` (**nothing covered undo
at all** before this). Both are `soak`, both registered.

`paintwalk` began by counting `pt_bar_x`/`pt_bar_y` calls and **would have
failed §42.8.5 for being faster**; it also had to learn that §42.8.8 gives
`pt_segdo` a second consumer — the replay walks every chord again, and
`[pt_noscr]` is the discriminator.

Scratch instruments live in the session scratchpad, not the repo:
`chordcost.py` (per-chord cycles and `pt_rect` calls, `--noscr` for the canvas
half alone), `settle.py` (the release settle), `strokefan.py` (a twelve-ray fan
hashed on the screen and after a repaint — the pixel-identity gate for every
change here), `paintsweep.py`, `rectsplit.py`, `steptrace.py`.

## 7. "756 µs" — what it is and is not

Carried here because it was quoted throughout this work and it is stale in two
directions.

- **It is a FIELD measurement of the fixed part of a SMALL call** on a 4.77 MHz
  5150, and PERFORMANCE.md Part 1 says in terms: *do not quote "756 us" as the
  cost of a call, or as a floor a design has to beat.* Part 9 Set 89 puts it
  plainly — **an average of the family, not a floor.**
- **It has been taken apart and cut by about a fifth.** One `gfx_pixel` was
  **196 guest instructions across eleven routines** with no hot spot, a third
  of it push/pop pairs (29.7 clocks) and near call/rets (52.1). SPEC.md §5.7 is
  the seven rules that came out of it; under `-icount` the pixel path came down
  **19.6%** and every `gfx_*` row with it.
- **Part 2 has deliberately NOT been re-measured**, because an inferred figure
  must not quietly replace a field one. So 756 is the last *field* number and
  the improvement is recorded in a different unit.
- **It is path-specific twice over.** A **near** kernel entry measured **382
  µs** — "the routine without the far call and without `gfx_lock`". And **VGA
  is faster than mono**: `GFX_PIXEL` **505 µs** on VGA against ~756, because a
  planar write puts eight pixels across four planes into one store where the
  1bpp renderer does a read-modify-write for anything unaligned.
- **Current-build figures to use instead** (Set 89, MartyPC): `gfx_hline`
  **3,235 cycles for 6 pixels and 4,112 for 312** — the width barely matters;
  `gfx_frame` 16,402 for an 11×11 box; `gfx_fill` 3,106 → 41,323 over one
  window raise.

**This does not move any number in this document.** The ~3,500-cycle fixed part
used to size the sweep primitive in section 5.1 below was measured here on the current build (a 1-row
fill at 3,658 cycles against an 8-row at 4,716), not taken from Part 2 — so it
already carries §5.7's improvement. Paint's own path is a **far call on a 1bpp
adapter**, which is exactly the case the 756 figure describes, which is why the
two agree.

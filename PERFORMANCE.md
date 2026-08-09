# PERFORMANCE.md — the machine this is for, and why your emulator lies about it

**Read this before you change anything that draws, lays out, or loops.** It
is required reading alongside [SPEC.md](SPEC.md) (what the kernel *is*),
[CLAUDE.md](CLAUDE.md) (how to work on it) and
[docs/TESTING.md](docs/TESTING.md) (where a test can run at all). This one
is about the target machine, which is not the one you are looking at.

**A bare `§` here means SPEC.md**, as it does everywhere else in this repo.
This document's own divisions are called **Part 1**..**Part 9**, so the two
can never be confused.

os8088 targets an **IBM PC/XT: an Intel 8088 at 4.77 MHz**, 4,772,727 cycles
a second, an 8-bit bus, no cache, no multiplier worth the name, and a
framebuffer on the far side of ISA. Every agent that has worked on this
project has tested it on QEMU under a container that is roughly **three
orders of magnitude faster**. QEMU is not a slow-machine simulator; it runs
the guest at host speed and models no 8086 timing whatsoever.

That gap has cost this project defect after defect — a drawing primitive nine
times slower than the one it replaced while measuring identical (Part 3), a
window operation costing two whole-screen repaints per click (§26.2), a
keystroke that flashed its line blank on every press (§27.2), a benchmark
whose own counters lapped on the machine they were written for (Part 6,
rule 3). Not one of them was visible in a screendump. Every one was found
either on real hardware or by *counting* rather than looking.

Two sentences carry the whole document:

> **QEMU is exact about how much work the guest does, and useless about how
> long that work takes.** So count work, never time it — and for the three
> defects it cannot show at all (Parts 1 and 3), the judgement is made on
> hardware.

### ...and MartyPC changes the first sentence, but not the third

**`make marty` is now the first tool to reach for** when the thing under test
runs on an 8088 with a CGA or a Hercules (docs/TESTING.md carries the
ordering, docs/MARTYPC-DEBUG.md the recipe). It is **cycle-accurate**: Set 11
put it within 0–4% of the 5150 on 45 of 47 `gfxbench` rows, which is the
closest agreement any emulator has managed here, so on the CPU and on drawing
you may finally *time* work rather than only count it. The debugger attached
to it costs the guest no cycles at all, so measuring does not change the
measurement.

**It is cycle-accurate and it is NOT disk-accurate, and that distinction is
load-bearing enough to sit at the top of this document rather than in Set 11
where it was measured.** MartyPC models instruction timing, the prefetch queue
and bus contention; it models no platter, no seek and no interleave:

| | real 5150 | MartyPC |
|---|---|---|
| read 16 KB, cold motor | **8.07 s** | **0.27 s** — 30x fast |
| boot | **38,886 ms** | **2,306 ms** — 17x fast |

So **any figure with a disk in its path is wrong on MartyPC, by more than an
order of magnitude and in the flattering direction** — and that reaches a lot
that is not obviously about disks: a boot time, a package launch, a Tracker
module load, a `SYSTEM.CFG` write, the Control Panel closing. Part 9's disk
rows come off the 5150 and nowhere else.

**Nor will it catch a disk CORRECTNESS bug**, which is how the last two disk
optimisations came to measure *worse* than what they replaced. §18.91's `AL`
bug is the case: the BIOS moved nine sectors and answered `AL = 1`, the kernel
believed `AL`, and re-read the rest one sector at a time — 148 sectors in 34
`int 13h` calls for a 32-sector file on the 5150, while **the same binary on
the same image under QEMU moved 34 sectors in 6 calls**. Correct, fast, and
silent. It took the field machine plus §18.94's counters to see it, the boot
sector carried the identical bug for as long again, and no emulator here would
ever have shown either. An emulator's floppy controller returns what its
author believed the hardware returns.

---

## Part 1 — The vocabulary — say what you actually saw

The prose in this repo has historically called every visible drawing defect
"flicker". On a 4.77 MHz machine they are three different things with three
different causes and three different fixes, and calling them all flicker is
how one gets fixed and the other two get shipped. Use these names.

### Visible redraw — *not* a flicker

A window's whole content, or the whole screen, being painted again. On real
hardware **you watch it happen**: the fill sweeps, then the text lands, row
by row. It is not a flash you half-notice, it is a wait you sit through.
Heavy applications — Paint, the Task Manager, a Disk window full of files —
take **seconds** of it. A fractal frame is ~115 s and a full repaint used to
throw it away (REDRAW-SPEC.md).

If a change makes any operation call `wm_paint_all`, or makes a window
repaint its whole content where it used to repaint a band, that is this
defect. It will look instantaneous in a screendump and it is the single
most expensive mistake available in this codebase.

**It is measurable now** — Part 3.1. A Disk window's full repaint
(`fm_repaint`) is **11 frames, 183 ms** on CGA and 9 frames, 150 ms on VGA:
the number of displayed frames between the first change and the settled
state, times the frame period. That is not an estimate from a call count, it
is the count of frames a person would have watched go by.

### Double-draw flash — the pixel written twice

Anything that draws a region and then draws over it: the classic is the
**erase-and-letter pair** — `gfx_fill` the rect white, then `font_char` the
glyphs into it. Between the two the region is *blank*, and on an XT that gap
is tens of milliseconds, several display frames. The area is smaller than a
full redraw, so it reads as a flash rather than a wait — **but it is still
very plainly visible**, on every keystroke, on every update.

It is invisible in QEMU at any frame rate a screendump can sample, and **no
timing column reports it**, because the two methods take comparable *time* and
differ only in what is on screen during it. §6.1 is the fix (`font_run`, one
store per cell, old content straight to final); §27.2 and §11.94 are the two
consumers converted for the flash rather than for the 10.7%.

**There is a column for it now, and it is a count of pixels.** Part 3.1 is the
method; the short version is that a frame-by-frame capture of the *rendered*
framebuffer answers "which pixels ended up exactly as they started, but showed
something else in between", and those pixels are this defect by definition.
Measured: that same `fm_repaint` flashes **1,963 pixels for 10 frames — 166 ms**
on CGA. A Note Pad keystroke flashes **nothing at all in the text**, which is
§6.1 working, verified rather than asserted.

The same defect wearing other clothes: a background fill under an icon that
is then drawn; `wm_grow_paint` filling its 13×13 square before framing it,
which survived as a flashing corner after Note Pad's rows were fixed; a
pattern strip erased then re-lettered every scroll row (§45.11).

#### The standing rule: do not blank and then redraw

**Treat every blank-then-redraw as a defect until it has been shown to be the
cheaper of two evils, and assume it is visible on every adapter — VGA
included — on real hardware.** It is not a mono problem and it is not a
slow-machine problem; it is a *two-writes-to-one-pixel* problem, and the eye
samples between them at 50–70Hz whatever the card is.

What makes this rule necessary rather than obvious is that the defect **does
not appear in any timing column** — blanking first is usually the same amount
of work — and does not appear in a *screenshot* either, since a capture taken
after the operation shows the correct final pixels.

**It is not invisible to this harness, and do not write that it is.**
MartyPC measures it and Part 3.1 is the method: sample the rendered
framebuffer once per displayed frame and count the pixels whose before and
after agree while something else was on the glass between them. That count
*is* this defect. Reach for it before concluding anything about a flash, and
read the BBOX with the number. QEMU is the one that cannot show it, and QEMU
is the fallback for 286+/VGA and a short list besides.

Two sampling mistakes, because both look like an absence: **screendumps over
a socket are not frames** — a shot every ~50 ms straddles a 40 ms window and
reports a clean picture on both sides of it — and the instrument's floor is
**3 frames**, so an event of one or two needs the capture window slid across
it rather than aimed at it.

The one exception is written up under *What it does not cover*: a flash that
**spans a mode change** has no before/after pair for `transient` to compare,
and wants the lit-pixel count of the first frames in the new mode instead.

What is left for a person on a real machine is narrower than the above makes
it sound, but it is not nothing: whether the flash is *objectionable* at the
size and position it occupies; anything decided by **the machine's own ROM**
rather than by this kernel (§39.6's CGA mode set is the worked example — the
emulated ROM does not produce the flash the IBM one does); and the analogue
consequences a framebuffer has no field for, the beam-current swing an IBM
5151 answers with an audible pop among them.

What the fix looks like, in order of preference:

1. **One opaque write per cell** — `font_run` (§6.1) for text, an image blit
   for pictures. Old content straight to final; nothing in between exists.
   Padding a run with spaces out to the band *is* the erase.
2. **Draw only what changed.** A caption that changed does not owe the
   header, the rows and the buttons a repaint (§52.10.6); a scroll owes the
   rows and not the chrome (§22.11); a lamp owes a lamp (§56.12).
3. **Invert rather than repaint.** XOR is its own inverse, so moving a
   selection is two inversions and no glyph is re-lettered (§22.2, §27.8.2).

The exceptions worth knowing, both structural rather than lazy: a **frame
cannot be drawn opaquely**, so a control whose border changes from solid to
dithered has to have its rect cleared first (§52.10.6) — keep that rect as
small as the control; and **`wm_draw_win`'s content fill** is the one blank a
window is *entitled* to, because a window being shown had no pixels there at
all.

### Stall and input overrun

The machine stops answering. A held gfx lock across a long render freezes
the cursor (§20.6 rule 3 forbids it). A redraw slower than the key repeat
loses keystrokes to a full BIOS buffer. A pattern view that costs more than
the frame budget reads as a *hung* display rather than a slow one — which is
exactly why the tracker stops animating its grid on a tier-0 machine
(§45.9.1).

Whether a human can outpace the redraw is a property of the real machine's
speed against a real person's hands, and *that* cannot be measured here. What
can: **how long the machine is not answering**, which is the other half of it.
Part 3.1's capture gives the span of any operation in milliseconds of guest
time on a cycle-accurate 8088, so "this redraw takes 183 ms" is a number, and
comparing it against the ~9-tick (500 ms) typematic delay or a 55 ms tick is
arithmetic rather than a guess. The part still reserved for a person is
whether *their* hands outpace it.

---

## Part 2 — Calibration — estimate without a machine

**Everything in the first table was measured on an IBM 5150 — 8088 at 4.77
MHz, 640KB — with a Hercules card and again with a CGA, by `gfxbench` and
`sysbench` (Part 9).** Where the two adapters differ, both are given. Nothing
here is an estimate; the estimates are in the second table, and they are
labelled.

### The three numbers that price almost everything

| | Hercules | CGA |
|---|---|---|
| **Any `gfx_*` drawing call — the fixed part** | **756 us** | 756 us |
| **One 8×8 glyph cell** | **901 us** | 909 us |
| **One 78-cell row of text** | **71.4 ms** | 72.7 ms |

The first is the one to internalise, because it is the one nothing in this
project believed. `GFX_PIXEL` and `GFX_HLINE 8px` measured **765.64 and
764.82 us on Hercules and 765.70 and 764.80 on CGA** — two different
routines, two physically different cards whose framebuffers are 13% apart,
agreeing to one part in ten thousand. Almost the whole cost of a small
drawing call is fixed setup, about **3,600 CPU clocks**, and it is CPU-side:
the card barely shows through.

> **A redraw is priced by how many primitive calls it makes, not by how many
> pixels it covers.** Everything below is a consequence of that sentence.

**That floor has since been taken apart and cut by about a fifth, and this
table has NOT been re-measured** (rule 8: a figure carries its machine and
its build). Part 9 Set 3 is the teardown — one `gfx_pixel` is 196 guest
instructions across eleven routines, a third of them push/pops and near
call/rets — and the work it prompted is SPEC.md §5.7. Under `-icount` the
pixel path came down 19.6% and every `gfx_*` row with it. Until somebody
runs `gfxbench` on the 5150 again, **estimate with the 756 us above**: it is
the number that was measured, the improvement is measured in a different
unit, and an inferred figure must not quietly replace a field one.

### Measured — drawing

| quantity | Hercules | CGA |
|---|---|---|
| `gfx_fill`, per scan line | 177 us | 182 us |
| `gfx_fill`, per pixel | 0.28 us | 0.33 us |
| `gfx_hline`, per pixel past the fixed cost | 1.16 us | 1.20 us |
| `GFX_FILL 64x64` | 12.4 ms | 13.0 ms |
| `GFX_FILL 256x128` | 31.7 ms | 33.9 ms |
| `font_run`, per cell of a ten-cell run | 905 us | 918 us |
| `font_run` aligned vs. the skewed hand-written pair | 1.24× | 1.24× |
| `WM_TITLE` strip (§11.92) | 43.2 ms | 44.4 ms |
| A full text page (78 cells × 34 / 16 rows) | **2.50 s** | 1.24 s |
| One vertical retrace period | ~20 ms (50 Hz) | ~16.6 ms (60 Hz) |

A fill spends **91% of a 64-pixel-wide row on arriving**: 177 us of setup
against 18 us of pixels. The per-pixel half is already at the bus (0.28 us/px
is 2.2 us per framebuffer byte against the 3.26 a raw `rep stosb` costs), so
there is nothing in the inner loop and most of an order of magnitude in the
row setup.

### Measured — the machine

| quantity | value |
|---|---|
| CPU, from `MUL` and `DIV` independently | **4.64 and 4.68 MHz** (nominal 4.7727) |
| **8088 instruction floor** | **4.34 clocks per instruction BYTE** — see below |
| A segment override | +3.78 clocks (one extra instruction byte), *not* the book's 2 |
| API far-call cell (`OSAPI_*`) | 46.7 us |
| Near `call` + `ret` | 11.0–11.5 us |
| `OSAPI_TASK_YIELD` (a full switch) | 693 us |
| RAM `rep stosw` | 1.76 us/byte |
| RAM byte read-modify-write (a 5-instruction loop) | 15.3 us/byte = 72.8 clocks |
| **Framebuffer byte read-modify-write** | **16.7 us/byte = 79.6 clocks** (CGA 81.0) |
| Framebuffer vs. RAM, word write | 1.57× (CGA 1.78×) |
| Framebuffer vs. RAM, read-modify-write | 1.09× (CGA 1.11×) |
| An ISA status-port `in` | 8.7 us |
| The kernel's own tick + mouse + scheduler | **1–3%** of a busy CPU |
| Floppy throughput | **7,457 bytes/second** (was 2,100 before Set 17) |
| Floppy, per `int 13h` CALL | **~400 ms — about 2 of the 200 ms revolutions**, whether it moves 1 sector or 9. THIS is the unit to estimate a disk change in |
| Floppy, per 512-byte sector | **65 ms** inside a well-coalesced run (was 238 — see below) |
| Floppy, open and read a one-sector file | 810 ms |
| System tick | 18.2065 Hz; **65,536 PIT counts, measured exactly** |
| Serial mouse | 1200 baud |

**The framebuffer read-modify-write was quoted at "~30 cycles" here and in
§39.5 for years. It is 79.6, and only about 7 of those are the bus** — the
rest is five 8088 instructions. The figure was low, and it was attributed to
the wrong thing.

**The floppy rows above carried the SAME kind of error for longer, and it
was the bug rather than the measurement.** `238 ms per sector` and
`2,100 bytes/second` were measured honestly and then outlived their cause:
they are SPEC.md §18.91's `AL` bug, where the BIOS moved nine sectors,
answered `AL = 1`, and the kernel re-asked for the other eight one at a time
— a revolution each. Set 17 fixed it and the same 16 KB read went 8.29 s →
2.09 s, so **every estimate made from 238 ms is 3.9x too pessimistic**, and
every conclusion that "we must not spend sectors at boot" needs re-deriving
before it is quoted again. Set 18 then took the boot itself from 39.88 s to
**9.94 s** on the same machine.

**And the sector was always the wrong unit — the call is the right one.**
Set 14 timed a whole 9-sector track in ONE `int 13h` at **384 ms**, 1.92
revolutions, and Set 17's residual is read off the same figure: *"32 sectors
in ~4 calls of ~400 ms is 1.6 s, and we measure 2.09 — about two extra
calls' worth."* A call costs one to two revolutions almost regardless of how
much it moves, so **`int 13h` calls, not sectors, are what a disk change
should be costed in** — which is exactly why §18.94's counters report both
and why a mount is quoted as `12 sectors / 4 calls`. The per-sector row is
kept because it is the right unit inside one long coalesced run, and that is
the only place it means anything.

The Compaq Portable III (Set 18) is the second data point and it moves with
the drive rather than the CPU: 360 RPM, so a revolution is 166.7 ms and the
same read is **11,047 B/s / 46 ms a sector**.

#### THREE quantities, all different, and one number was doing all three

This is Set 11's lesson again — a walk step's arrival, a one-pixel block and
a marginal pixel turned out to be three figures where the tree had one — and
the disk had the same disease for longer. Estimate with the row that matches
the ACCESS SHAPE:

| shape | cost | how known |
|---|---|---|
| a sector **inside a coalesced run** | **65 ms** (5150), 46 ms (Compaq III) | measured, Sets 17/18 |
| an **`int 13h` call** in such a run | **~400 ms**, 1–2 revolutions, near enough whatever it moves | measured, Sets 14/17 |
| an **isolated single-sector access** — a boot sector at LBA 0, a lone directory sector | **~150–200 ms once the motor is up**, and most of a second if it is not | **MODELLED, NOT MEASURED** (docs/ASSOC-PLAN.md): ~100 ms average rotational latency + ~80 ms average seek across 40 tracks at a 6 ms step + ~15 ms settle |

The third row is the one that keeps getting confused with the first, and it
is the one that matters for anything re-reading track 0: **an isolated
first-sector read is not a 65 ms sector, because it is a seek**. A read of
LBA 0 from wherever the heads were is up to a full 39-track stroke, so the
~80 ms in that model is an *average* and the worst case is dearer.

**`238 ms` is none of the three.** It is the AL bug — a revolution burned per
sector because the kernel re-asked for eight of every nine — and it happened
to sit near the isolated-access figure, which is exactly what let it survive
as a plausible per-sector cost for so long. **Anything derived from it is
wrong, and derivations from it are annotated as pre-fix throughout this
tree rather than deleted**, so the reasoning that produced a decision is
still readable next to the number that has since moved.

### The 8088 instruction floor — what replaced "add 20–40%"

This document used to end Part 2 with "8086-nominal cycle counts under-report
an 8088 by 20–40%", from a plan document. The measured ratio runs from **1.01
to 4.34**, and reading it against instruction *bytes* explains all of it:

| instruction | bytes | measured clk | 8086 book | ratio |
|---|---|---|---|---|
| `nop` / `inc r16` / `xchg ax,r16` | 1 | **4.34** | 3 / 2 / 3 | 1.44 / 2.17 / 1.44 |
| `mov r16,r16` / `add` / `cmp` / `shl r16,1` | 2 | **8.69** | 2 / 3 / 3 / 2 | up to **4.34** |
| `jmp short`, taken | 2 | 18.19 | 15 | 1.21 |
| `mov ax,[disp16]` | 3 | 21.61 | 14 | 1.54 |
| `call near` + `ret` | 4 | 52.13 | 27 | 1.93 |
| `mul r16` (with its reload) | 5 | 132.53 | 129 | **1.02** |
| `div r16` (with its reloads) | 7 | 162.85 | 160 | **1.01** |

> **An 8088 costs `max(execution clocks, 4.34 × instruction bytes)`**, plus
> about 4 clocks per byte of memory operand, plus a queue refill after every
> taken branch.

It is a floor, not a tax. `MUL` and `DIV` measure at 1.01–1.02 because the
sequencer stays busy long enough to hide every fetch; a run of
register-to-register moves measures at 4.34× because nothing hides anything.
**A shorter encoding beats a cheaper instruction** — `xchg ax,bx` (one byte)
is twice as fast as `mov ax,bx` (two) although the book prices them at 3 and
2. So the question is never "what percentage do I add"; it is whether the
code is fetch-bound or execution-bound.

### Still written down rather than measured — treat as estimates

These are readings of the instruction stream that no field set has confirmed.
The floor above says most of them are **optimistic**, because a written-down
count is an 8086 count.

| quantity | value | source |
|---|---|---|
| `repe scasb` run scan | ~15 clocks/byte | `kernel/vga12.inc` |
| Naive per-pixel decode (shift by CL) | 75–90 clocks/pixel | ibid — the pre-coalescer `gfx_blit4`, Part 3 |
| Back-buffer flush vs. its RAM render | ~24× | §32 — **VGA only**, and the field machine has none |
| 4-plane flush vs. 1-plane (`bb_mono`) | ~3.7× | ibid |
| Note Pad layout walk iteration | ~500 8086 cycles | §27.4 |
| ArtfulType `at_getb` | ~32 clocks/character | `apps/artful/atdoc.inc` — 9 bytes of instruction, so ≥39 by the floor |

Multiply and say that you did. "~500 cycles per iteration × 404 iterations
≈ 42 ms" is a reading of the instruction stream presented honestly; quoting
404 as if it were a duration is not.

---

## Part 3 — What QEMU cannot show at all

Not "shows inaccurately" — **cannot show**.

**MartyPC shows the first two, and Part 3.1 is how.** That is most of why it
now comes first. A cycle-accurate 8088 spends a visible redraw's seconds, so
a repaint that should not be happening shows up in the cycle count rather than
only on somebody's desk — and because the emulated card rasterises real
frames, the *glass* can be sampled at the rate an eye samples it. The third is
half a person and half a number now; the fourth — a lost optimisation that
kept its shape — is unchanged, because it is a failure of the *question*, not
of the emulator.

1. **Visible redraw.** A full window repaint is microseconds under QEMU and
   seconds on the target. Nothing in a screendump, a timing column or a QMP
   script distinguishes a window that repaints from one that does not.
   **MartyPC prices it in frames** (Part 3.1).
2. **Double-draw flash.** Both methods take comparable time; they differ in
   what is on the glass during it, and no timing column has a place to put
   that. **MartyPC counts the pixels** (Part 3.1).
3. **Perceived latency and input overrun.** How long the machine fails to
   answer is measurable (Part 3.1's span). Whether a particular person's
   hands outpace it is not.

And one that is worse than invisible, because it looks like a *success*:

4. **A lost optimisation that kept its shape.** It has happened twice.
   §48.10 is the second: `mc_blob` replaced 39 one-row fills with 6 nested
   rects, which is 6.5× fewer *calls* and 4.7× more *scan lines*, because
   nested rects overlap — 37.1 ms against the 36.4 it replaced. It measured
   as a win in every counter anyone had, and only a field log from the target
   machine said otherwise. And `gfx_blit4`'s first version
   emitted one call per run exactly as designed, and decoded every pixel
   individually inside the scan instead of comparing byte pairs. **Under QEMU
   it measured as exactly as fast**, because QEMU does not model 8086 timing —
   so every screendump was right and every test passed. This is why the cycle
   counts in `kernel/vga12.inc` are *written down* rather than measured.

   The three figures this entry used to carry — "nine times slower on
   hardware", "a quarter of a second", "over two" — were all derived from
   those same written-down counts and none was ever measured. They are gone
   rather than corrected, because the field set prices the primitive a
   different way entirely and it should be the one quoted.

   What the field set adds, and what a caller of the fixed primitive needs:
   **`gfx_blit4` emits one `gfx_hline` per coalesced run, and a `gfx_hline`
   costs ~0.5 ms whatever its length.** Measured, 64×64 pixels either way:
   one run per row (64 calls) is **28 ms**; sixteen runs per row (1,024
   calls) is **561 ms**. Twenty times, for the same pixels. So the cost of a
   blit is `runs × 0.5 ms`, the pixel count barely enters it, and *how flat
   the picture is* is the whole performance story.

---

## Part 3.1 — Measuring flicker: one sample per displayed frame

This part of the document said for years that the double-draw flash could not
be observed anywhere but on somebody's desk. That was true of QEMU and it is
not true of MartyPC, and the reason is worth stating precisely rather than as
a capability list.

### Why frames are the unit

**A CRT shows whatever the raster read on its last pass.** So "what a person
saw" is not a continuous thing — it is a *sequence of completed frames*, and
anything that happens entirely between two of them was never on the glass at
all. MartyPC rasterises into a front/back pair, and `display_buf()` is the
front one: the last frame the card actually finished. Step the machine until
`frame_count()` increments, grab that buffer, repeat — and you have sampled
the screen exactly as often as an eye does, no oftener and no seldomer.

Measured frame periods, from the capture itself rather than from a datasheet:

| adapter | cycles/frame | period | rate |
|---|---|---|---|
| CGA 640x200 | **79,574** | **16.67 ms** | 60.0 Hz |
| VGA mode 12h | ~77,500 | **16.23 ms** | 61.6 Hz |

Both are on a 4.77 MHz 8088, so a frame is **about 79,000 cycles** and any
defect shorter than that is, by construction, invisible — to this instrument
and to the user alike. That is a feature and it is also the boundary: this
measures *visibility*, never *work*. Part 4's counters and `gfxbench` are what
price work, and a change can be right on one and wrong on the other.

### The metric

    transient(k) = |{ p : first[p] == last[p]  AND  frame_k[p] != first[p] }|

**A pixel whose value before the operation and after it are the same, but
which showed something else in between, was written twice for no reason and
the user could see it.** That is the double-draw flash stated as arithmetic,
and three properties make it the right definition:

- **It needs no notion of "background".** An erase-and-letter pair is caught
  because the cell goes to the fill colour and comes back to the same glyph —
  not because the fill colour is special.
- **It cannot fire on an honest change.** A caret moving, a digit
  incrementing, a window's contents genuinely changing: there
  `first[p] != last[p]`, so the pixel is excluded before anything is counted.
  Every pixel it counts is one the operation did not, in the end, need to
  touch.
- **It is conservative.** An operation that changes most of what it draws has
  most of its pixels excluded, so the number that comes back is a floor.

`changed` — the ordinary frame-to-frame delta — is the other half, and prices
the **visible redraw**: frames between the first change and the settled state,
times the frame period, is how long the user watched it happen.

### The protocol

```sh
python3 tools/os88marty.py 127.0.0.1:9001 flicker -n 90 --click
```

or, driving it properly, in three steps that are all load-bearing:

```python
m.run(); pointer.goto(x, y)      # 1. get into position WITH THE MACHINE RUNNING
m.pause(); m.mouse(0,0,l=True)   # 2. inject while PAUSED - the packet queues
r = m.flicker(frames=90)         # 3. capture; the action lands inside the window
```

1. **Position first, with the machine running.** A mouse packet is decoded by
   the guest's own ISR over several frames; moving during the capture measures
   the move, not the click.
2. **Inject while paused.** The packet or keystroke sits in the device queue
   and is delivered as the capture runs, so the action is *inside* the window
   rather than racing it.
3. **Check `settled`.** It reports whether the last three frames are
   identical. If they are not, `last` is not an end state, every `transient`
   above was measured against a moving target, and the answer is more frames —
   not a smaller number.

### The anchors

Measured on `os8088_5150_cga`, CGA, 640x200, one boot, reproducible to the
pixel across repeats and in both directions:

| operation | visible redraw | flash | worst frame | where |
|---|---|---|---|---|
| idle desktop | 0 frames | 0 frames | 0 px | — |
| pointer move, no click | 0 frames | 0 frames | **0 px** | — |
| Note Pad keystroke | 3 frames, 50 ms | 1 frame, 17 ms | 42 px | *the mouse pointer* |
| Disk window repaint (`fm_repaint`) | 11 frames, **183 ms** | 10 frames, **166 ms** | **1,963 px** | the content rect |
| ...the same on VGA mode 12h | 9 frames, 150 ms | 8 frames, 133 ms | 2,201 px | the content rect |

Three of those five rows are the validation, and they are worth reading as
such rather than as results:

- **Idle is zero.** The instrument does not manufacture a defect out of a
  quiet machine, a blinking caret or the menu-bar clock.
- **A pointer move is zero**, which is an independent confirmation of SPEC.md
  §7.1.2 from a different direction entirely. That change made a cursor move
  write every byte exactly once, and was verified by diffing framebuffers
  against a known-broken build; here a completely separate instrument, asking
  a different question, agrees that nothing on the glass is ever disturbed.
- **`fm_repaint` is large**, and it must be: §22.3 describes it as one big
  fill plus ~40 `font_str`s, which is the erase-and-letter pair at window
  scale. A metric that did not fire here would be measuring nothing.

And the negative control is the sharpest of them. **A Note Pad keystroke
flashes nothing in the text** — §6.1's `font_run` and §27.2's "the padding IS
the erase" working, measured rather than asserted. The 42 pixels it does
report are somewhere else entirely.

### The trap: a count without a location misattributes

Those 42 pixels were first read as "small, therefore the text still flashes a
little". They are the **mouse pointer**, blinking off for one frame while the
gfx lock is held (SPEC.md §7.1.4), and the way that was established is the
lesson: the count barely moves when the pointer moves — 40 against 42 — because
an arrow is the same size wherever it sits, and only the clipped edge differs.
Comparing counts said "not the cursor" and was wrong. The **bounding box**
settled it in one run:

| pointer parked at | worst | bbox |
|---|---|---|
| (600, 190) | 40 px | `[600, 190, 606, 199]` |
| (400, 150) | 42 px | `[400, 150, 406, 160]` |
| (180, 111) | 42 px | `[180, 111, 186, 121]` |

A 7x11 box at the pointer, three times. **Always ask where.** `flicker`
returns a bbox per frame for exactly this reason, and a number quoted without
one is a number that has not been attributed.

### What it does not cover

- **Hercules.** MartyPC's MDA does not rasterise Hercules graphics mode: the
  rendered framebuffer is **0 lit pixels of 252,000** and never changes, and
  `frame_count()` never advances, so a capture times out rather than
  answering. Nothing said so, because `shot`'s VRAM route reads *guest memory*
  and works perfectly there — which is why this went unnoticed until something
  asked the card instead of the kernel. Measure the flash on **CGA** and reason
  across: §39.5 is one parameterised 1bpp renderer for both adapters and the
  drawing code is identical, so what differs is the geometry (1.9x the pixels)
  and the frame rate, not whether a cell is blanked before it is lettered.
- **Anything shorter than a frame**, as above. Correct by construction and
  worth restating: a defect the raster never catches is a defect nobody saw.
- **Anything that spans a MODE CHANGE**, and this one is a property of the
  metric rather than of the emulator. `transient` counts pixels that *ended as
  they started* while showing something else in between — and across a mode
  set there is no such pixel, because the before picture is a text raster and
  the after picture is a bitmap. Nothing ends as it started, so the count is
  ~0 however bad the flash. The measurement that does work there is cruder and
  direct: step frames and count **lit pixels in the first few frames after the
  card reports the new mode**. A cleared framebuffer is ~0; the previous text
  page reinterpreted as 640x200 is not. SPEC.md §39.6 is the worked example,
  and its result is worth carrying: on `os8088_5150_cga` — **the real IBM 5150
  ROM**, which every `os8088_5150_*` machine runs — the first graphics frame is
  **1–2 lit pixels of 128,000 with the fix and without it**, and the text frame
  before it is 0–16. A null result, and the reason is the next bullet.
- **Powered-up video RAM.** A real card comes up with static in it; MartyPC
  comes up with zeros. So any defect whose *material* is uninitialised VRAM —
  SPEC.md §39.6's flash before the progress bar is the standing example — has
  nothing to work with here and cannot be reproduced however finely you
  sample. PCem models it as noise. This is a short list, but it is the one
  place where "the emulator cannot show it" is still literally true, so do not
  read the rest of this section as saying no such class exists.
- **Work.** A change can cut the flash and cost more cycles, or the reverse.
  Part 4 and Part 9 price work; this prices what the work looked like.
- **The disk.** Unchanged and absolute — an operation with a disk in it has
  the wrong *span* here by more than an order of magnitude, so `flicker`'s
  millisecond figures are only meaningful for operations that touch no floppy.
  The frame *counts* are still real; it is the wall time they represent that
  is wrong.

---

## Part 3.2 — Measuring smoothness: pacing is variance, not rate

Part 3.1 asks what a single operation looked like. This asks what a
**continuously animating** one looks like over time, and they are different
defects with different fixes. A scrolling pattern grid, a bouncing ball or a
game loop can be entirely free of double-draw flash and still look bad.

**What the eye objects to is not the cost of a frame, it is the variance
between them.** A step that lands 2 frames after the last one and then 7
frames after that reads as judder even when the average rate is exactly right
— and "exactly right" is not a figure of speech here, as the worked example
below shows. So the number that matters is the **spread** of the update
intervals, and the one that does not is the mean.

`pace` records, once per displayed frame, how many pixels differ from the
frame before. That series is everything: the gaps between non-zero entries are
the intervals, and their standard deviation is the judder. It keeps two frames
server-side rather than all of them — unlike `flicker`, which needs every
frame to compare against a settled end state — so it runs for thousands of
frames, which is what a pacing question needs.

```sh
python3 tools/os88marty.py 127.0.0.1:9001 pace -n 500
```

**Evenness = sd / mean** is the headline. 0.00 is a metronome; past ~0.5 the
irregularity is what you notice rather than the rate.

### The worked example: Tracker's fullscreen text mode

SPEC.md §45.13's `FSXM_TEXT80` screen, playing a MOD at BPM 125 / Speed 7 on
a 5150 with a Sound Blaster, 500 frames at 59.9 Hz. **This is a before-and-
after**: the numbers immediately below are the version this instrument was
first pointed at, and §45.15's smoothness work is measured against them at the
end of the section.

Before:

```
  updates      : 131 in 500 frames (26.2% of frames move)
  interval     : mean 3.79 fr (63.3 ms) -> 15.8 updates/s
  JITTER       : sd 2.41 fr (40.2 ms), min 1, max 7 fr (117 ms)
  evenness     : 0.63
  intervals    : 1fr x53  3fr x2  4fr x3  5fr x21  6fr x40  7fr x11
```

**The interval histogram is bimodal, and that is the whole diagnosis.** 53
intervals of one frame and 72 of five to seven, with almost nothing between.
A bimodal histogram means **two things are updating on two different rhythms**
and the summary line is their interleaving — a "mean of 3.79 frames"
describing nothing that is actually on the screen. Splitting by update size
separates them:

| | n | mean | sd | evenness | intervals |
|---|---|---|---|---|---|
| **big** (the grid) | 61 | 8.28 fr (**138.2 ms**) | 4.91 | 0.59 | 1fr x13, 6fr x12, 7fr x6, **13fr x20** |
| small (the counters) | 63 | 7.69 fr (128.4 ms) | 3.38 | 0.44 | 5fr x14, 6fr x11, 7fr x13, 8fr x12 |

Repeated on a from-scratch build the whole picture comes back: evenness 0.61,
big-update mean 8.23 fr (137.4 ms), sd 4.73, and the same `1fr` / `13fr`
clustering. The split is done by `report_pace` automatically, on a threshold
of a quarter of the largest update — there is nothing to tune.

And now the result that makes the measurement trustworthy: **a MOD row at
BPM 125 / Speed 7 lasts exactly 140 ms** — ticks-per-row 7 at a tick rate of
`BPM x 2 / 5` = 50 Hz. The measured mean interval of the big update is
**138.2 ms across two runs (137.4 and 138.2), an error of 1.3-1.9%.** The
instrument is measuring the music, from pixels, with no instrumentation in the
guest at all.

So **the rate is right and the delivery is not**: the correct number of
screen updates arrive at the wrong times, clustering at intervals of one
frame and thirteen instead of a steady eight. That is what "visibly not
smooth" is, and it is now a number that a change can be held against.

What this does *not* establish is the cause. A pair of big writes one frame
apart is the shape you would expect from §45.13's "the frame is a `rep movsw`"
followed by "the band is an attribute, relit after the blit" — two writes, one
row apart in time — but the measurement says nothing about which code did
what, and a hypothesis with an obvious mechanism is exactly the kind that gets
believed without checking. The way to settle it is a counter or a breakpoint
on the two write paths, not another reading of this table.

### The same screen after §45.15/§45.16's smoothness work

Identical protocol, identical module, identical machine — the interpolated
play model (§45.15.1/§45.15.2), the pattern-window scroll and the **measured
frame clock** (§45.16, which takes the text screen off the 18.2 Hz system tick
and onto the retrace) merged in:

| | before | after |
|---|---|---|
| updates in 500 frames | 125–131 | **226–229** |
| mean interval | 3.79–3.99 fr | 2.16–2.21 fr |
| **jitter (sd)** | 2.41–2.44 fr (**40.2–40.7 ms**) | 1.28–1.33 fr (**21.4–22.2 ms**) |
| evenness | 0.61–0.63 | 0.60 |
| interval histogram | `1fr x48  5fr x14  6fr x50  7fr x10` | `1fr x90  2fr x64  3fr x25  4fr x29  5fr x15  6fr x1  7fr x1` |
| big-update stalls | **`13fr x20`** — a third of them | *gone* |

**It is substantially smoother, and it lands where SPEC.md said it would.**
§45.16 predicts, from the field constants and without any of this, that the
retrace clock *"takes the jitter to ±18 ms"*. Measured here, from pixels, with
nothing in the guest: **sd 21.4–22.2 ms, down from 40.2–40.7.** Two independent
routes to the same number is the strongest evidence in this document that
either of them is right.

The histogram says the rest more clearly than any summary line. The absolute
timing scatter **halved**, 40.7 ms to 21.4 ms. The distribution stopped being bimodal — one decaying cluster where
there were two — which means the two rhythms were merged into one. The long
tail collapsed: 61 gaps of six or seven frames became **two**. And the
`13fr x20` cluster that dominated the big updates, twenty stalls of 217 ms
each, is gone entirely. Updates nearly doubled at about a fifth the size:
the work is spread rather than delivered in bursts.

**What has not changed is the evenness ratio, and that is the remaining
issue.** 0.60 against 0.61–0.63, because the mean and the sd both halved.
The raw gap sequence says what it is:

```
4 1 1 4 1 3 4 2 1 2 4 2 4 1 2 1 1 4 1 4 2 4 1 2 4 1 1 5 2 1 1 2 2 8 1 1 1 1 1 1 4 2 ...
```

**39% of the gaps are exactly one frame, in ~34 separate runs** — updates
still arrive in *bursts of consecutive frames* separated by quiet periods of
four to eight. The bursts are much shorter than the old stalls, which is why
it looks better, but the delivery is still clumped rather than even. And the
rate is 25.4 updates/s against a MOD tick rate of **50/s** at this tempo, so
it is not simply one update per tick either.

What that is *caused by* is not established here, and the mechanism is not
obvious enough to guess at safely — 50 Hz of ticks against a 59.9 Hz raster
cannot divide evenly, so some beat is inherent, but nothing in these numbers
separates an inherent beat from a burst the app could pace better.

### The mouse cursor contaminates it, and the contamination FLATTERS

**In any graphics-mode capture with the pointer on screen, some of what `pace`
reports is the arrow.** `gfx_lock` erases it and `gfx_unlock` puts it back
(SPEC.md §7.1.4), so a locked draw can produce a *second* changed-frame a frame
or two later, at the pointer and nowhere else. Measured on a windowed Tracker
with the pointer parked at (300,120): **15–18% of all update events had a bbox
lying entirely inside the arrow cell, and every one of them was exactly 44
pixels** — the 8x12 arrow's lit-pixel count.

The damage is not that it adds noise. It is that **it subdivides genuine
stalls**: a blink landing in the middle of an 18-frame gap reads as two shorter
gaps, so the histogram, the mean and the max-gap all come out better than the
truth. Three runs, same scene, with and without the arrow:

| | updates | mean interval | **max gap** |
|---|---|---|---|
| run 1 as measured | 83 | 4.82 fr | 20 fr (334 ms) |
| run 1 cursor excluded | 74 | 5.29 fr | **22 fr (367 ms)** |
| run 2 as measured | 98 | 3.95 fr | 17 fr (284 ms) |
| run 2 cursor excluded | 87 | 4.51 fr | **18 fr (300 ms)** |
| run 3 as measured | 82 | 4.70 fr | 22 fr (367 ms) |
| run 3 cursor excluded | 63 | 6.34 fr | **25 fr (417 ms)** |

Every column moves the same way: the count is inflated by 11–23%, the mean
interval is short, and the worst stall is understated by up to 50 ms. An
instrument whose error is in the flattering direction is the one to distrust
most — it is the same shape as the disk figure at the top of this document.

**Two workarounds, and they answer slightly different questions.**

```sh
os88marty.py <addr> pace -n 400 --no-cursor              # detected from the data
os88marty.py <addr> pace -n 400 --ignore 300,120,307,131 # exact, if you know it
```

`--no-cursor` finds the most frequent bbox no larger than 8x12 and drops the
update events that are entirely it — **no kernel offsets, survives a rebuild**,
and it says what it excluded. `--ignore` excludes a rect server-side, before
the pixels are counted at all, so it also removes the arrow's contribution from
frames where the app changed *too*. On the same scene they agree on what
matters — 61 updates and a 22-frame max gap either way, mean 6.32 against
6.35 frames — and differ only in the pixel-count columns, which pacing does
not use.

Reproduced on a from-scratch build against the Task Manager, where it is
starker still: **24 updates become 16** — a third of them were the arrow —
and the max gap goes 31 to 32 frames.

**Two different cursors, and `video` only knows about one of them.** Its
`cursor` field is the **CRTC text cursor**, which the card draws itself; in a
graphics mode it correctly answers `visible: false` *even while the mouse
arrow is plainly on screen*, because the arrow is pixels the KERNEL wrote and
the card has no idea it is a cursor. So `video.cursor` is the check for a
blinking hardware cursor in a text mode and is **not** a check for the mouse.
For the arrow, use `--no-cursor`, or nothing at all if the pointer is
somewhere you do not care about — it contaminates only where it sits.

**And a text mode has no drawn arrow at all**, which is why the before/after
above needed no correction: Tracker's `FSXM_TEXT80` fullscreen has the gfx
lock held for the whole bracket (so the kernel's arrow is off) and §45.13
hides the CRTC cursor with `int 10h`, and `video` confirms `visible: false`.
Check rather than assume — a text screen that *left* its hardware cursor on
would blink it at a fraction of the field rate, a periodic contaminant with no
relation to anything the guest is animating.
### …and what the evenness ratio was hiding: 28.8% of the machine in a poll

The question two sections up — *what causes the clumping* — got its answer
from a **different instrument**, and the reason it had to is worth the
paragraph: `pace` reads pixels, so it can say the scroll is uneven and can
never say why. The complaint that reopened it was audible rather than visible
— *"the music plays smoothly for the first 10–20 s of fullscreen, then has
dropouts"* — and the two turned out to be one defect seen from either side.

Everything below is the `FSXM_TEXT80` fullscreen, so the cursor correction the
section above establishes does not apply to any of it: the bracket holds the
gfx lock throughout (no kernel arrow) and §45.13 hides the CRTC cursor. The
before and the after were taken the same way, on the same scene, with the same
build of the tool.

**The instrument was a sampling profiler with no code in the guest**: ask
MartyPC for CS:IP a few thousand times and bin it by the nearest symbol out
of a NASM listing. Tracker on the §45.13 text screen, MartyPC's
cycle-accurate 5150 with a Sound Blaster, `BEVERLY.MOD` in XT mode:

| | windowed | fullscreen, retrace clock | fullscreen, `FSXW_FRAME` |
|---|---|---|---|
| `mp_mixch_xt` — the mixer | 47.2% | **35.0%** | **51.4%** |
| `fsx_insync` + `fsx_wait` — the poll | — | **28.8%** | *absent* |
| `task_yield` | — | — | 0.8% |
| bytes/second reaching the card | 5,533 | **4,808** | **5,520** |
| ring lead, halves of 2,048 | 7.0–8.0 | **1.0**–8.0 | **5.0**–8.0 |

`fsx_wait`'s retrace path is a **busy-wait**, and to a round-robin scheduler a
busy-wait is work: the drawing task took its half of the machine whether it
needed it or not, and the half it did not need was the mixer's. The mixer
needs ~55% of a 4.77MHz 8088 to hold 5,500 Hz; it was getting 35%. 4,808
against 5,533 is **13% of the audio never played** — the DSP pausing, which is
what `SBL_ST_UNDER` does by design rather than loop stale samples — and the
ring's eight halves are ~3 seconds of cushion, which is exactly why it sounds
perfect for the first half-minute and then does not. §53.5.1 is the fix: a
frame clock that waits on `[sch_subs]` and **yields**, so the frame time the
app does not spend goes to its worker instead of to a port.

**The scroll, measured the `pace` way, over the same change** — BIG-update
class, which is the pattern-grid blit:

| | retrace clock | `FSXW_FRAME` | + the `[tui_fpt]` fix |
|---|---|---|---|
| mean interval | 8.88 fr (148.2 ms) | 8.20 fr (136.9 ms) | 8.12 fr (135.4 ms) |
| **jitter (sd)** | **5.60 fr** | 1.57 fr | 1.57 fr |
| evenness | **0.63** — judder | **0.19** | **0.19** |
| intervals ≤ 6 fr or ≥ 10 fr | scattered, 1–13 fr | 22 of 110 | **5 of 148** |

The ideal is 8.39 CRT frames (140 ms at this tempo). After both changes
**96% of row intervals are 7, 8 or 9 CRT frames**, which is what a 54.6 Hz
display of a 7.14 Hz row stream quantizes to and is therefore the floor: a
row change can only be shown on a frame, so 128 ms and 146 ms alternating is
the best a 54.6 Hz clock can do, and nothing short of more frames improves
it.

The third column is a second, independent defect the first fix exposed
(§45.15.2): `[tui_fcnt]` is reset *by* the frame standing on the tick edge, so
it ends a tick holding the frames **inside** the tick — two, not three. The
sub-tick interpolation divided by two and then capped, so the position froze
for the last third of every tick and jumped half a tick. It measured as
`[tui_fpt]` = 2 on a machine measured at three frames a tick; that
disagreement *is* the bug, and it had been invisible while the frame count
was a wobbling 1.81.

**And the earlier open question is answered in passing**: the "39% of gaps are
exactly one frame, in bursts" reading was the *small*-update class — the VU
needles and the status line — not the scroll, which the BIG class isolates.
The scroll's own clumping was the starvation.

### The trap that cost two wrong answers: the guest's own counters

The music's tempo is the ground truth this measurement leans on, and reading
it off the screen is where it goes wrong. `Pos x 64 + Row` looks like a row
index and **is not one**: an order position does not always span 64 rows (a
pattern break ends one early) and an order list may revisit a pattern, so the
arithmetic silently under- or over-counts across a boundary. Measured that
way, three windows of the same playback gave **+2.4%, +10.4% and +28.1%**
against the 140 ms row — and the obvious reading, *the tempo is drifting*, is
wrong.

Measure **inside one pattern** instead — `Ptn` unchanged and `Row` advancing,
so no multiplication is involved — and take the **ratio of sums** rather than
the mean of per-window ratios, because a short window holds three to five rows
and `ms/row` quantises to `window/integer`. Done that way the same playback
reads **141.54 ms/row against 140.00, +1.1%**, with individual windows
138.5–145.9. The tempo was correct the whole time.

Both errors produced *plausible* numbers with no warning, which is the theme:
`+26%` and a quantised histogram of exactly three values both look like
findings.

### Using it

- **Pick the run length from the thing you are watching.** 500 frames is 8.3
  seconds; a defect with a period longer than the capture reads as a single
  interval or none at all. The desktop's menu-bar clock shows `HH:MM`, so it
  updates **once a minute** — 3,594 frames — and a 600-frame capture of an
  idle desktop correctly reports *nothing is animating* rather than a rate.
- **Read the histogram before the summary.** One mode means one rhythm and the
  mean is meaningful. Two modes mean the mean is an artefact.
- **`--min` raises the pixel threshold for "an update"**, which is the cheap
  way to ignore a caret or a counter and watch only the thing that moves.
- Everything Part 3.1 says about the **disk** applies unchanged: an animation
  that pauses for I/O has the wrong gap here by more than an order of
  magnitude.
- It needs a rasterising card — and **Hercules is one, which Part 3.1's note
  said it was not.** MartyPC's MDA does rasterise HGC graphics: 250-frame
  captures there run to completion and reproduce, and the rate they report
  agrees with the guest's own frame counter to 1% (below). What is *not* true
  on Hercules is that the rendered buffer is byte-exact — it disagrees with
  the VRAM route on **2.8% of pixels on a paused machine**, spread evenly
  across x mod 8 and x mod 9 and concentrated at horizontal edges, so it is a
  raster alignment rather than a decode. That costs `pace` nothing, because it
  compares rendered frames with rendered frames; it is why a **"0 differing
  pixels" check on Hercules must stay on the VRAM route** (`shot --kind herc`).

### Missile Command: what the exclusive bracket buys is evenness, not rate

SPEC.md §48.13's same-mode bracket (§53.7) takes the `gfx_lock`/`gfx_unlock`
pair out of every frame — Part 9 Set 4 priced that pair at **21.8% of a
77-second session** with no pixel of the game in it. What that is worth was
never measured on the axis the complaint was about. On MartyPC's Hercules at
50.9 Hz (19.66 ms/frame), a wave descending with no player input, mode
certified `M_PLAY` and the wave number unchanged across the capture, and both
arms entered at the same point in the wave:

| 250 frames | windowed | bracket (`F`) |
|---|---|---|
| paint-to-paint | 3.15 / 3.04 fr (62.0 / 59.7 ms) | 2.78 / 2.95 fr (54.7 / 58.1 ms) |
| **jitter (sd)** | 1.17 / 1.20 fr (22.9 / 23.6 ms) | **0.41 / 0.90 fr (8.1 / 17.7 ms)** |
| **evenness** | **0.37 / 0.40** | **0.15 / 0.30** |
| histogram | `2fr x12 3fr x59 5fr x3 6fr x1 8fr x3` | `2fr x19 3fr x69` |
| | `2fr x15 3fr x62 5fr x1 6fr x1 8fr x1 11fr x1` | `2fr x19 3fr x59 4fr x2 5fr x1 6fr x2 8fr x1` |

**The rate is identical and the delivery is not.** Both are one frame per PIT
tick; the bracket's best run is `2fr x19 3fr x69` **and nothing else**, which
is not merely even but *exactly* at the floor — a 54.925 ms tick on a 19.66 ms
display is 2.794 frames, so a metronome here **must** emit 2s and 3s in the
ratio 0.206 : 0.794, giving sd 0.405 and **evenness 0.145**. Measured: 0.148.
There is no smoothness left to win in that run. Windowed, the same 2/3 cadence
carries a tail of 5, 6, 8 and 11-frame gaps that is not there in the bracket.

Two things make the number trustworthy. **The game's own deadline counter is
the ground truth**: `[mc_due]` advances once per `mc_worker` iteration, and
sampled against MartyPC's cycle count it reads **18.19 and 18.37 fps** against
a PIT tick of 18.2065 — so windowed Missile Command keeps its deadline exactly,
and the jitter above is *delivery*, not dropped frames. (It reads 0 inside the
bracket, correctly: `mc_fsx_main` has a loop of its own and never touches it.)

**And the game itself, measured at the source, is a metronome — which `pace`
alone could not have told you.** A breakpoint on `mc_worker`'s
`call mc_render` stops the machine once per game frame, cycle-exact, with no
code in the guest; MartyPC pauses while it is stopped, so guest time does not
advance and nothing is perturbed. Windowed, a wave descending, no input:
**199 consecutive frames at 54.92 ms mean, sd 1.28 ms**, against a PIT tick of
54.925 — and diffing the framebuffer at each frame boundary, **0 of 199 frames
drew nothing**. Under sustained fire (fire injected every 10th frame from the
harness, so the load pattern is identical between arms) it is 18.21 fps with
sd 5.43–6.44 ms, and only **~5% of frames are off-tick at all**.

That is the instrument to reach for when `pace` reports a tail: it separates
*the game delivered late* from *the display sampled it at an awkward phase*,
and here it was the second. It also priced the one pacing change worth making —
`MC_LAGMAX` 4 → 0, SPEC.md §48.20: **sd 5.43/6.44 → 4.43/4.76 ms, worst short
frame 29 → 36 ms, worst long frame 98 → 81 ms, and 18.21 fps in all four
runs.** Chasing a missed deadline is what the judder *is* when motion is
per-frame, and stopping costs no frames.

**`pace`'s own interval statistic had to be adapted too, for a reason that
generalises.** Tracker's grid arrives as one blit on an otherwise-static
screen, so one changed frame is one update. Missile Command *paints* for
1.5–2.0 displayed frames per game frame — a 4.77MHz machine cannot fill a
615x171 content box inside 19.66 ms — so the card catches it mid-paint and one
game frame contributes several consecutive changed frames. Counting gaps
between changed frames then splits every frame in two and reports ~28
updates/s for an 18.2 fps game. **Grouping consecutive changed frames into one
paint** is the fix, and it is validated rather than assumed: it recovers
16.1–16.7 fps against the counter's 18.2. The rule of thumb is that `pace`'s
raw intervals mean what they say only while a paint fits inside one displayed
frame.

---

## Part 4 — What QEMU is exact about: work

The guest does the identical amount of work on both machines, and QEMU will
report it precisely. So when the question is "is this slow because it does
too much?", **instrument a counter and read it over QMP** — do not reach for
86Box, and do not guess.

**On MartyPC the counter is often unnecessary**: `step` returns real cycles,
so work and time are the same question there and `tools/os88marty.py` reads
any variable by name out of a `nasm -l` listing without one being added. The
counter idiom below is still the QEMU answer, still correct, and still the
only route when the thing you are counting is not on an 8088.

```nasm
; kernel/font.inc, in .text so the offset is fixed
dbg_cells:  dw 0
...
font_run_cell:
    inc word [cs:dbg_cells]
```

```sh
nasm ... -l /tmp/k.lst   &&  grep dbg_cells /tmp/k.lst     # -> 0x1E78
python3 tools/qmp.py build/qmp.sock 'xp /2xh 0x2478'       # KERNEL_SEG*16 + off
```

`h` is a word; HMP's `w` is four bytes. **Editing any include before the one
holding the counter moves the offset**, so re-derive it after every rebuild.

A **package** can write the same counter — `mov ax, KERNEL_SEG / mov es, ax /
inc word [es:0x1E7E]` — which is how a walk inside an app is counted without
knowing the segment its region was claimed at.

This is what settled the Note Pad question (§27.4). A user reported typing
getting slower as a note grew and inferred that more than one character was
being redrawn. The cell counter said **2 cells per keystroke** at every note
length and every window width — the drawing was already right — and a counter
in the layout walk said 404 iterations, growing linearly. The cost was in a
place no screenshot could show and no wall clock here could measure.

### Instructions, when you want a number rather than a count

Both benchmarks time against counter 0 of the 8253 read directly (a 55 ms
tick cannot resolve a 3 ms row). Under QEMU that counts *host* speed and is
worthless, so run them with `-icount` and the PIT counts guest
**instructions** instead — deterministic, ±1 count across runs, and the same
on any host:

```sh
make bench
make test TESTAPPS=build/bench.img QEMU="qemu-system-i386 -icount shift=3,sleep=off"
```

Reproducible and machine-independent, but **not time**. And it *understates*
the mono win, because what alignment removes is disproportionately memory
traffic. `build/bench360.img` on a real 4.77 MHz 8088 (or 86Box) is where the
PIT is a wall clock and the microsecond column means microseconds.

#### Turning an icount run into milliseconds

An icount PIT count is instructions, and one conversion factor makes it a
duration. It was derived by pinning `fontbench`'s Hercules row against the
10.09 ms that same row costs on hardware:

| | |
|---|---|
| one `-icount shift=3` PIT count | **0.359 ms** of real XT ≈ 105 guest instructions |
| implied 8088 clocks per instruction | **~16.4** |

**The field set cross-checks both and they hold.** 16.4 clocks for an average
instruction is what the instruction floor predicts once memory operands and
taken branches are added to `max(exec, 4.34 × bytes)` — a 2–3 byte
instruction is 8.7–13 clocks of fetch floor before either. And the figure
`§48.8` derived through this route, **~1.16 ms for one mono `gfx_fill` of a
~27px row** (3.11 PIT counts ≈ 337 instructions), lands inside the measured
model: 756 µs fixed + 177 µs for the row + 27 × 0.28 µs of pixels ≈ 0.94 ms,
and the measured `GFX_FILL 8x8` is 1.13 ms. So the conversion is sound within
about 25%, which is the right precision to quote it at.

Use it for a row a field set does not cover. Where Part 2 has the quantity
measured, prefer Part 2 — a derivation through two anchors cannot beat a
direct reading.

### The two harnesses that produce a document rather than a screen

`fontbench` and `typebench` each answer one question and fit on a screen.
`gfxbench` and `sysbench` answer forty each, so they page — and they **save
the whole report to a text file** on the current volume (`S`, or the Bench
menu). That file is the point: it is meant to be carried off the machine and
pasted into Part 9 below.

- **`gfxbench`** prices every `gfx_*` and `font_*` slot on whichever adapter
  it booted on, most of them at **two sizes** so the per-call term and the
  per-pixel term come apart, plus the raw RAM and framebuffer bandwidth
  underneath them. One package for Hercules AND CGA deliberately: both are
  the same 1bpp software renderer over four different numbers (§39.3), which
  it reads from `OSAPI_VIDEO` at run time, so the two columns are the same
  measurement rather than two sources that can drift. `GFXHERC.TXT` /
  `GFXCGA.TXT` / `GFXVGA.TXT`.
- **`sysbench`** prices the machine underneath: **8086-nominal clocks against
  a real 8088 per instruction class** (the number the last line of Part 2 has
  been quoting from memory), RAM bandwidth, the clock ladder, the API's
  far-call floor, what the kernel's own interrupts cost per second of
  ordinary work, and the floppy. `SYSBENCH.TXT`.

Both are timed the same way, and it is a deliberate departure from
`fontbench`: the `cli` window is **one iteration, not one row**, so the tick,
the mouse and any sound refill are serviced *between* iterations and land in
no measurement at all — where `fontbench`'s whole-row `cli` let one unlucky
row absorb another task's slice and move by more than the effect. Rows too
long for a 55 ms PIT wrap fall back to tick timing and are flagged `t`; a row
whose worst iteration came within a third of the wrap is flagged `!`.

Read the caution block at the top of either report before quoting anything
from a QEMU run. Two rows there are worse than noise on an emulator and say
so themselves: the retrace period (QEMU's status port toggles on every read,
so a poll always terminates) and the VRAM rows under a `HERCSEG=` kernel
(B0000 is unmapped, so they measure plain RAM and the bus ratio reads 100).

**Instructions are the better proxy, not framebuffer traffic**, and Part 9
settles it beyond argument. §6.1.1 originally predicted the opposite and was
corrected once by measurement: the XT came in at the *instruction* figure
(1.30×, independently 1.24× on the second harness), not the 3.6× traffic
figure. The field set then proved the general case — the same two primitives
measured **0.01% apart on a Hercules and a CGA whose framebuffers are 13%
apart at the bus**. Per-call and per-row setup dominate the byte-writes they
guard, on every adapter. Traffic remains the right *explanation* of where the
writes went; it is not the right predictor of time.

---

## Part 5 — The standing budget — what is already cheap, and must stay cheap

Nearly every expensive path in this system has already been made cheap once,
by somebody who measured it. **A change that reintroduces a full repaint is a
regression against a documented number, not a neutral refactor.** This is the
list to check yourself against.

| operation | was | is | contract |
|---|---|---|---|
| Show / raise a window | whole-screen repaint: dither, drive icons, dock, bar, every window's frame and `W_PAINT` | the bar, the dock, the outgoing title bar, this window | §11.90 |
| Raise an already-frontmost window | a screen | **no window at all** | §11.90 |
| Click a background window's title bar | two full screen redraws (raise + drag release) | two title bars | §11.90, `ui_drag` |
| Hide / destroy / drag a window | a screen | the damage rect, and only the windows in it | §11.91 |
| The desktop dither inside a damage rect | the WHOLE rect, and then every window overlapping it drawn on top — so every pixel under a window was dithered and then painted over. Measured on CGA: dragging a window flashes **4,819 px** worst, dragging it back **5,222** | `wm_dmg_gray` — the rect minus every visible window, through the region machinery `wm_clip_set` already had, so `gfx_fill_gray`'s one clipped call paints only what is genuinely uncovered: **609** and **1,413** | §11.91.1 |
| Double-click a title bar to zoom | `wm_paint_all` both ways: a screen's dither, every drive zone, both strips, every window's `W_PAINT`. Worst transient **23,842 px**, 445 ms of flashing | out: nothing is revealed, so `wm_draw_win` and the chrome — **1,540 px**. Back: the union through `wm_paint_dmg`, with the dither clipped — **1,161 px**, a 20x drop | §11.95.1, §11.91.1 |
| Retitle a window | full frame repaint | one `TITLE_H` strip | §11.92 |
| Mount / unmount a volume | `wm_paint_all` | the zone grid — measured **371 glyphs → 182** | §26.3 |
| Select a covered drive icon | **two** whole-screen repaints per click | one XOR strip, zero repaints; byte-identical output | §26.2 |
| Select a file row (Disk window) | ~130 glyphs + a dozen fills | two XOR bands; **zero** `font_char`, **zero** `gfx_fill` for most cases | §22.2 |
| Scroll a Disk window one row | `fm_repaint`: header, both buttons, every visible row, the whole scroll bar and the status line. Measured on CGA with `os88marty.py flicker` — **16 frames of visible redraw = 262 ms, 15 of them flashing = 246 ms, worst 2,772 transient pixels**, bounding box the whole window content | one `gfx_scroll`, the row it exposed, two XOR bands and the thumb: **5 frames = 83 ms, 2 flashing = 33 ms, worst 320 px** — and the bounding box is one row and the bar. Framebuffer **byte-identical** to the full repaint on CGA (both byte phases), Hercules and VGA mode 12h, 25 frames each | §22.11 |
| Scroll a Disk window that is already at an end stop | a full repaint to show the same pixels — **266 ms** | **nothing at all**, 0 frames | §22.11 |
| Type into the file dialog's name box | ~120 glyphs + a 298×151 fill | `font_char` **972 → 36**, scanlines **7,600 → 184** (8 chars) | §38.8 |
| Note Pad keystroke | full content fill + a glyph per character | **2 cells**; `font_char` **8,410 → 350**, scanlines **5,020 → 1,960** (20 keystrokes, 410-char note) | §27.2 |
| Note Pad layout per keystroke | 404 walk iterations at 200 chars, growing | 35, and flat | §27.4 |
| Note Pad caret keys | Up 1,608 iterations / Home 1,608 / Left 804 | 184 / 90 / 60 | §27.5 |
| Note Pad walk below the view | 6 walks, 10,079 iterations (72% off-screen) | 2 walks, 1,015 | §27.7.1 |
| Note Pad scroll | letter 19 rows | one blit + 4 rows | §27.7.2 |
| Note Pad insert at the front | 1,600 cells ≈ most of a second | a scroll, settled later | §27.3 |
| An opaque text run | 228–336 framebuffer accesses, alignment-dependent | flat **80**; 1.30× on hardware (`fontbench`), 1.24× on a second harness and a second adapter (Part 9), and no flash | §6.1, §6.1.1 |
| Task Manager row update | 20 glyphs to change 3, twice a second | the changed chunks only | §28.2 |
| Timer digits | eight erase-and-letter pairs — 8 `gfx_fill` + 8 `font_char`, each cell blank between its two calls, **twice a second for as long as the window is open**: 320 + 320 calls in 20 s, ~24.7 ms a redraw | one `font_run` over the cells that CHANGED, byte-aligned by `WF_SNAP`: **0 fills, 0 `font_char`**, 20 runs of 22 cells in the same 20 s, and no blank interval | §14.1, §6.1 |
| Menu bar redraw | every window operation | gated on `[menu_bdirty]` | §12.05 |
| Menu bar when it IS owed | one white fill across the whole bar, then the logo's 121 `gfx_pixel` calls, the app name and every cell title put back — measured on CGA with `os88marty.py flicker`: switching application is **9 frames flashing = 149 ms, worst 534 transient pixels**, bounding box the full width of the bar | three segments, each on its own trigger. The menus are composed into `menu_bcell` and emitted as the one `font_run` span that DIFFERS: **1 frame = 17 ms, worst 37 px — and those 37 are the mouse arrow**. The logo and the rule are drawn only when something painted over the bar | §12.9 |
| The menu-bar clock | a 206px erase and ~20 glyph cells re-lettered on **every window operation**, for a string that changes once a minute | a check word: nothing at all. When it does change, one opaque `font_run` right-aligned in its field by leading spaces — no fill, no blank interval | §12.1, §6.1 |
| The file-activity widget finishing | `menu_force` + `menu_draw_bar`: the whole bar white-filled and the logo, the name, every title **and the clock** put back, to give an 88px strip back | two thin fills of the rows that carry no text, and the text band rewritten by the run that was already going to be emitted | §12.8, §12.9 |
| Dock redraw | every window operation | per-tile keys: a focus change is 2 tiles, a quiet desktop is 0 | §30.1 |
| A dock tile whose MARK moved (every focus change) | erased to white and rebuilt — fill, frame, two-pass masked icon blit, marks — twice, to move one 1px rectangle. On CGA: **9 frames flashing = 149 ms, worst 534 px** | one `gfx_xor_rect` or one `gfx_xor_fill`; **the icon is not touched**. Both marks are their own inverse, so old XOR new names the difference | §30.3 |
| Damage that reaches the dock strip | `dock_force`: all 640px of rule and field restored and **every** tile rebuilt, for a window that closed in one corner | the damaged span only, and the keys of just the tiles it overlaps | §30.3.1 |
| Arkanoid pause / resume | the whole content — background, both rails, every brick, paddle, ball, capsules, shots, status strip | the banner's 9-row band: `gfx_fill` **89 → 2**, `font_char` **10 → 6** | §44.1 |
| Missile Command explosion (1bpp / 8088) | a full disc **every** frame for 27 frames plus 12 ring erases — ~750 fills a burst, 124 ms a frame in a busy wave | three drawn states, five-rect discs — 22 fills a burst, 7.9 ms | §48.8 |
| Missile Command terrain repair | `[mc_gdirty]`, one byte: the whole ground band, six cities and three bases — **143 ms**, five times in 86 frames | a damage **span**: 16.5 ms, byte-identical to a full repaint | §48.9 |
| Missile Command score strip | the whole strip blanked and re-lettered on every kill | three `font_run` fields, space-padded — no blank interval | §48.9, §6.1 |
| Missile Command missile trails | an app-side Bresenham emitting one `gfx_hline` per **row** — a whole-trail erase was 267 fills, ~310 ms, a five-tick stall | one `gfx_line`: 59 ms worst frame, and the busy frame whole went 190 ms → **43.5 ms** | §5.6, §48.8.3 |
| Missile Command fullscreen | the §11.2 fullscreen WINDOW — still in the z-order, still pre-empted, so **6.2 ms of `gfx_lock`+`wm_clip_set` and 5.7 ms of `gfx_unlock` on every frame**, 21.8% of a session | §53.7's same-mode bracket: `lok` and `unl` measured at **0**, and the double cursor goes with them | §48.13 |
| A dilated STEEP line | three Bresenham walks over the same pixels — 37.8 ms of a 73.5 ms Missile Command frame | one walk writing a three-bit mask: **1.91×**, measured in guest instructions by `gfxbench`'s transposed pair after `linetest`'s host-time 1.3–1.9× settled nothing | §5.6.6 |
| Missile Command crosshair | 8 `gfx_xor_fill` **every frame** whether the mouse moved or not — **8.6 ms of a 55 ms tick**, idle frames included | 0 unless it moved or something drew through it; 4 signed compares per primitive otherwise, and the screen is byte-identical | §48.11 |
| Missile Command burst life | grow, peak, **collapse**, gone — 39 fills a burst, and the collapse alone is 42% of it for one visible state | grow and hold, with the life cut 27→21 frames so Σr (all a burst's lethality) is preserved to 3.3%: 25 fills, **18.3 → 12.4 ms a frame** | §48.12 |
| Solitaire stock click | 635 wasted fill runs **every click** | 0 unless the picture changed | §43.7 |
| Solitaire column redraw | every card, backs included (634 runs each) | buried backs kept; a measured move skips 246 runs | §43.7 |
| Fractal repaint | re-render from row 0 (~115 s) | replay the pass-0 cache, resume refining | §40.1 |
| Tracker row scroll | 30+ strips erase-then-text | 2 `gfx_scroll` + 3 strips | §45.12 |
| Tracker on a tier-0 machine | a per-position repaint it does not have | one banded line | §45.9.1 |
| Tracker's XT fullscreen | the scrolling grid in pixels: **2,567 glyph cells/s**, ~2.6 s of drawing per second of music | an 80x25 text mode: **0** glyph cells, **0** `gfx_fill` — 1,121 `rep movsw` words a row change, ~4% of the machine, and the grid scrolls again | §45.13 |
| Tracker mixing at 11 kHz | ~7.9M cycles/s against a 4.77M budget | ~2.1M at 5,500 Hz, bounds check out of the inner loop | §45.9 |
| Tracker's readouts and grid | quoted the MIXER, which `trk_feed` keeps a ring ahead of the card — **2.2–3.0 s of music** at XT mode's 5,500 Hz. Measured: press Enter, screendump 0.15 s later, the grid is on **row 21** | position stamps — the row the card is *inside*. Same screendump reads **row 00**, and the cost is one word the worker's existing status poll already had | §45.15 |
| Tracker's text frame clock | one frame per tick: a 120 ms row drawn 110, 110, 110, **165** ms after the last, and on a module past 18.2 rows/s the display never shows some rows at all | `FSXW_FRAME` — the fsx sub-tick, 54.6 Hz, and it **yields** rather than polls, so the mixer keeps the machine (the retrace clock that came between cost 28.8% of the CPU and 13% of the audio) | §45.16, §53.5.1 |
| Tracker's text shadow rebuild | all 64 rows in one frame — 256 `mp_cell2txt` + 3,776 `lodsb`/`stosw` + a 9,676-byte blank ≈ **140–330 ms, once every ~9 s**, reported from the field as the screen stopping and then jumping | `TTX_SHCHUNK` = 4 rows a frame, cursor starting at the visible window and wrapping. **Confirmed on the 5150**: 51 s of bracket, frame spacings 432 × 1 tick / 247 × 2 / 2 × 3 and nothing else, with all five pattern boundaries indistinguishable from the baseline. (§45.13.4 took the shadow to 82 rows — 328 calls, 21 chunk frames instead of 16 — which lengthens the *rebuild*, not the frame the field run measured) | §45.13.2 |
| Paint brush stroke | width² per pixel of travel | the dab's leading edge, one `gfx_fill` per step | docs/PAINT-NOTES.md |
| Paint undo | whole canvas | row-granular and lazy | ibid |
| Menu save-under | 20KB claimed permanently, then 20KB per menu | sized from the rect actually dropped (~4KB VGA, ~1KB Hercules) | docs/KERNEL-MEMORY.md |
| Covered background window | skipped the frame entirely | draws its visible region | §11.3 |
| Copy a file | 5 volume switches per file | 2, one `dsk_read_chain` per chunk | §22.5 |
| FAT access across a copy | re-read on every switch | a window per volume: 45 mounts → 3 loads | §18.8.1 |
| The per-call floor itself (1bpp) | one `gfx_pixel` = **196** guest instructions of generic rect machinery | **158**; `GFX_FILL 8x8` −19.3%, `64x64` −14.5%, `GFX_BLIT4` −13.8%, output byte-identical on all three adapters | §5.7, Part 9 Set 3 |
| The pointer during a window refresh | `gfx_lock` erased the cursor for the **whole** hold, so a Task Manager refresh — twice a second, and a visible span on a 4.77 MHz machine — blinked the pointer off and on every time, most obviously with the mouse sitting still | the hide is **deferred** and `wm_clip_set` spends it only if the cursor is reachable: **30 of 31 refreshes kept it on screen**, 0 cell violations across menus, drags and a Control Panel | §7.1.4 |
| A press-and-hold on a file row | `fm_drag`'s `.wait` poll, unpaced: `gfx_unlock`/`task_yield`/`gfx_lock` as fast as the CPU allows, drawing **nothing**. **20,761 lock/unlock pairs a second** under `-icount`; on the field machine each costs ~1.9 ms, so it is the whole machine for as long as the button is down, and the pointer blinks continuously | one pair a tick, like the three sibling loops that already lingered: **21** | §7.1.3 |
| Moving the cursor | erase-then-draw, two walks, so every byte where the old and new cells overlap is written **twice** — and the value in between is the background, `ffff` on all twelve rows inside a window. ~6.5% of a 20 ms Hercules frame, on every mouse packet | each byte written **once**: pass 1 skips what pass 2 will write, pass 2 sources its background from the save buffer. No gate, no union walk, and the pair unmoved at 544 counts against 541 | §7.1.2, docs/FIELD-NOTES.md 6 |
| A renderer row step | `call gfx_nextrow`: a near call plus two CS-overridden memory reads, **three times per scan line** | three register instructions, parameters hoisted out of the loop | §39.3, §32 |
| `gfx_lock` + `gfx_unlock` — the pair every drawing burst pays | the mouse cursor over a 16x16 cell it never fills: a save, a white pass and a black pass, three walks over the same bytes, plus a restore — **17.82** guest-instruction counts a pair on Hercules, ~6.4 ms of the field machine, 11.6% of a 55 ms frame | the arrow's real 8x12 cell, no third byte, and on 1bpp **one** fused read-bank-paint-write pass: **5.41**, ~1.94 ms, 3.5% — **3.29x**, output byte-identical on all three adapters | §7.1, Part 9 Set 7 |

Two entries in that table are load-bearing beyond their own numbers.
**`OSAPI_WM_GROW` was called on every Note Pad keystroke** — free in the
emulator, a visible flashing corner at 33 ms a keystroke on hardware. And
**the covered-icon click cost two whole-screen repaints**, found by putting a
counter on `wm_paint_all` and watching a single click take it from 4 to 6.
Both are the same lesson: the emulator will not tell you.

---

## Part 6 — The rules that fall out

**1. Nothing repaints more of the screen than it changed.** This is the whole
architecture, not an optimisation: §11.3's clip region, §11.90/§11.91's
damage rects, §11.92's title strip, §27.2's row signatures, §28.2's chunks,
§38.8's bodies-and-wrappers, §43.7's per-pile redraw. If your change makes
something repaint a superset of what it altered, you have spent a second of
somebody's afternoon.

**2. Nothing writes a pixel twice.** The erase-and-letter pair is the
canonical violation and `font_run` (§6.1) is the answer — one decision per
cell, on both its paths, so it cannot even produce §11.3's granularity
failure. Where a run is not available, ask whether the erase is needed at all
(`sol_covers`, `np_clean`), and if it is, whether it can be the *inside* of
the changed branch rather than in front of it (§28.2 — "what is not redrawn
must not be blanked either").

**3. Size every range from the slowest machine it will ever run on.** A
constant sized while looking at QEMU encodes the wrong range, and the
failures are structural rather than proportional:

| sized against QEMU | what a real XT did |
|---|---|
| a 16-bit elapsed counter, one subtraction start-to-end | rows are 1.5M counts; it lapped silently into a small plausible number |
| `>= 32768 means the run overran` | most legitimate rows are 32768..65535; it discarded them |
| a ratio computed from `counts >> 4` | `>> 4` is still 90,000; it overflowed the word and printed 696 for 134 |
| a per-iteration fold, guarded only *near* the wrap | a 561 ms body reported 561 mod 54.92 = 12 ms, unflagged, and made a primitive doing 20× the work look 2.4× faster (Part 9) |
| `OSAPI_WM_GROW` on every keystroke | free in the emulator; a flashing 13×13 corner at 33 ms a keystroke |

A 32-bit accumulator folded per iteration costs a few instructions and cannot
lap; a 16-bit one sized "generously" against QEMU is wrong by 20×.

**4. Measure before redesigning.** The obvious hypothesis is wrong often
enough to matter — Note Pad's drawing was already correct when a user reported
it slow, and the fix that hypothesis would have produced was a fix to working
code. Put a counter in. It costs one rebuild.

**5. A counter is not a timer.** It says how many times something ran, not
what it cost. Multiply by Part 2 and say that you did.

**6. Keeping the shape of an optimisation is not keeping the optimisation.**
`gfx_blit4` is the standing example: one call per run, exactly as designed,
and 75–90 clocks a pixel inside it. Nine times slower, and QEMU said it was
identical. When you rewrite something whose *reason* is speed, verify the
reason survived, not the structure. (The fixed primitive's own cost model is
in Part 3 item 4: **runs × 0.5 ms**, pixels almost free.)

**7. Prefer a self-checking harness to a careful one.** Three of the four in
that table were caught by **one number on screen contradicting another**, not
by inspection — `typebench`'s CHAR row does 1.33× `fontbench`'s
PAIR work, so it cannot be the smaller number, and it was. Put redundant
quantities on the screen: a raw count *and* a derived time, two rows whose
relative sizes are known in advance, a ratio you can recompute by hand from
the columns beside it. A harness that reports one number per run is one you
have to trust.

**8. Treat every number as provisional, and cite where it came from.** A
benchmark figure without a date and a machine is worth very little. The
figures in §6.1.1 have been corrected by real hardware three times — twice
because a harness was wrong, and once because the *prediction* was (traffic,
not instructions).

**9. Refusal is a normal path.** Where the floor cannot deliver, say so at
call time and in prose — the `bb_avail` idiom, three layers deep: the probe
flag gates the setter *and* the caption *and* the click (§47). Do not ship
a feature that silently costs seconds on the target; ship one that greys
itself with the reason (§47 rule 3: grey a **fact**, never a guess).

**10. Degrade by tier, and know which tier you are on.** `OSAPI_CPU_INFO`
answers `CPU_8086` for the target machine, and two apps key real behaviour
off it: the tracker pre-arms XT mode and stops animating its pattern grid
(§45.9/§45.9.1), and Note Pad enables the visual break (§27.3). That is a
**fact the code can test**, unlike a guess about speed — and it is the
honest way to spend an optimisation that only the slow machine needs.
Everything else that is sized for the floor — Missile Command's fifteen
in-flight missiles, the tracker's 3-row VU bars — is simply sized for the
floor on every machine.

---

## Part 7 — Checking a change

In rough order of cost, and you do not always need all of it.

1. **Count the work.** Put a counter at the drawing primitive your change
   touches (`font_char`, `font_run_cell`, `gfx_fill`, `wm_paint_all`) and read
   it over QMP before and after. If the count went up, stop here.
   **`font_char` and `font_run_cell` are one number and must both be
   counted**, and on a mono adapter that is not a nicety: §6.1's fast path
   letters a cell without going near `font_char`, so a counter on
   `font_char` alone read **58 cells/s** for a Tracker pattern grid that was
   drawing **2,567** (§45.13.1). A plausible small number is the failure mode
   here, exactly as in rule 3 below — and for the same reason, **sample a
   16-bit counter often enough that it cannot lap between reads** (a second
   is ample; thirty is not) and accumulate the deltas mod 65,536.
2. **Look at it on a 1bpp adapter.** `make test VIDEO=cga` and
   `make test VIDEO=herc HERCSEG=0x7000` — the two adapters a 4.77 MHz machine
   actually has, where `[bb_on]` is permanently 1 and the software renderer
   *is* the direct path. A change that is free on VGA can be the whole cost on
   mono, and vice versa. docs/HERCULES-TESTING.md, because Hercules is not
   screendumpable and the failure is silent.
3. **Price it.** Multiply the counts by Part 2's calibration and write the
   milliseconds down in the commit message.
4. **Instruction-count it** if the change is inside a primitive:
   `-icount shift=3,sleep=off` and the benchmarks (Part 4). If the change is
   to a `gfx_*` or `font_*` slot, `gfxbench` already has a row for it and a
   before/after pair of its report is a diff.
5. **Run it on period hardware** if the change is about *time* rather than
   *work* — `make xt` (4.77 MHz 8088), `make 286`, `make 386sx`, `make 386`
   for the middle of the range. 86Box is not installed in the web container;
   those targets do not run there, and that is a real limit on what a web
   session can conclude.
6. **Watch it with your eyes** if the change is about flash or redraw. That
   judgement is made on hardware and by a person. Nothing above substitutes.

**Under QEMU, wall clock is still a lower bound worth having.** Paint's
measured figures under `make run-640` — a full-canvas flood fill in ~4 s, a
448×280 4bpp BMP open in ~8 s, a 448×280 GIF at ~125,000 dictionary walks —
are useful precisely because they are already slow *here*. A real 8 MHz
machine is several times slower and a 4.77 MHz 8088 slower again. If it is
seconds in the emulator, it is out of reach on the target: that is how JPEG
was ruled out (tens of seconds per 448×280 frame before the dither).

---

## Part 8 — Where the numbers live

| what | where |
|---|---|
| The testing matrix, and modelling the old machine from a fast one | [docs/TESTING.md](docs/TESTING.md) |
| The real machines, whose they are, and how to take a set on one | [docs/FIELD-MACHINES.md](docs/FIELD-MACHINES.md) |
| `font_run`, and the primitive priced four ways | SPEC.md §6.1 – §6.1.4 |
| The per-call floor, taken apart, and the seven rules holding it down | SPEC.md §5.7 |
| `gfx_blit4` / `gfx_scroll`, and the cycle counts written down | SPEC.md §5.4, §5.5; `kernel/vga12.inc` |
| The clip region, and the granularity rule | SPEC.md §11.3 |
| Show / hide / drag / retitle costs | SPEC.md §11.90 – §11.92 |
| `WF_SNAP`, and the keystroke priced | SPEC.md §11.94 |
| Note Pad's redraw optimisations, seven of them | SPEC.md §27.2 – §27.7.2 |
| The Task Manager's chunks | SPEC.md §28.2 |
| Double buffering, and the flush's 24× | SPEC.md §32 |
| The mono renderer's inner loop | SPEC.md §39.3, §39.5 |
| The fractal's restore cache | SPEC.md §40.1; [REDRAW-SPEC.md](REDRAW-SPEC.md) |
| Solitaire's incremental repaint | SPEC.md §43.7 |
| Arkanoid's pause band, and what an erase in the play area owes | SPEC.md §44.1 |
| The tracker on an 8088 | SPEC.md §45.9 – §45.12 |
| ArtfulType's performance contract | SPEC.md §46.1 |
| Greying a control honestly | SPEC.md §47 |
| Paint's design notes and what it cost | [docs/PAINT-NOTES.md](docs/PAINT-NOTES.md) |
| Per-device cycle budgets on the floor machine | [docs/SOUND-PLAN.md](docs/SOUND-PLAN.md) |
| Memory, and why there is no growth room | [docs/KERNEL-MEMORY.md](docs/KERNEL-MEMORY.md) |
| The benchmarks themselves | `tests/fontbench/`, `tests/typebench/`, `tests/gfxbench/`, `tests/sysbench/` (`make bench`) |
| The field measurements they produced | Part 9, below |

---

## Part 9 — The field reports

Part 2's calibration table has fifteen rows and **two of them were ever
measured on the target**; the rest are estimates from those two, or figures
carried over from a plan document. Part 9 is where that stops being true. It
holds the reports `gfxbench` and `sysbench` write, taken on real hardware,
verbatim enough that a later reader can tell a measurement from an inference.

### How to take a set

```sh
make bench                      # build/bench360.img is the 5.25" one
# write build/bench360.img to a floppy, and DO NOT write-protect it -
# the reports are saved back to that disk
# boot os8088-360.img, open Disk B, launch GFXBENCH.O88
#   R runs it (about ten seconds on a 4.77MHz machine)
#   S saves GFXHERC.TXT / GFXCGA.TXT / GFXVGA.TXT
# then SYSBENCH.O88, likewise, to SYSBENCH.TXT
```

A machine with two adapters gets two `gfxbench` sets. The probe picks one
(§39.1), so the other needs a kernel built with `VIDEO=` forcing it:

```sh
make BUILD=build/cga VIDEO=cga all      # ...into its OWN build directory,
                                        # or build/ ships a forced kernel
```

Do that in a separate `BUILD=` directory and nowhere else. A `VIDEO=`-forced
kernel that reaches `build/` is a machine that boots the wrong adapter for
everyone, and that has happened. Nothing under `build/` is committed, so it
cannot reach the repo — but it will sit on your images until a knob-free
`make` rebuilds them, and it is a release cut from one that does the damage.

### What the next set is being asked

Set 3 spent a model rather than a measurement in three places, and three rows
were added to the harnesses so the next field set settles them. Each has an
expected answer, which is the point — a row you cannot be surprised by is not
worth taking:

| row | what it settles | it should say |
|---|---|---|
| `sysbench: shl r16,cl (4)` and `(13)`, and the derived `shl clk/bit x100` | **the variable-shift model.** "8 clocks plus 4 per bit" is the 8086 book, and §5.7 traded two edge-mask shifts and `gfx_rowbase`'s `shl bx,13` for table lookups on the strength of it. One row can only report a total; the SLOPE needs two | the derived line near **400** (4.00 clocks a bit). Well under it and the tables bought less than claimed; well over and they bought more |
| `sysbench: mov al,[bx+disp16]` | **what a table lookup costs** — the other side of that trade, and the addressing mode all four kernel tables use (`gfx_inktab`, the two mask tables, `vid_banktab`). Nothing measured it before | ~17 clocks by the book, so **~74 us per 1,000**; it must come in well under `shl r16,cl (13)` or the trade is a wash |
| `sysbench: mov ax,i + mul [m]` | **the `mul` §5.7 did NOT remove** from `gfx_rowbase`, on the argument that the alternative is a per-row table `KERN_BUDGET` cannot fund. Only the register form was measured | close to `mul r16` plus an EA. If it is much worse, the table is worth costing again |
| `gfxbench: GFX_FILL 256x1`, and the derived `fill ns per row` | **the per-ROW term, cleanly.** Set 1 fitted `c + a*rows + b*px` to three sizes, got a NEGATIVE per-call term and over-predicted the 8x8 by 1.27x, and said so. 256x1 against 256x128 differs by 127 rows and by nothing else | `fill ns per row` near **177,000** on Hercules / 182,000 on CGA. Where it disagrees with the two-point fit, **this one is the measurement and that one is the model** |
| `gfxbench: FULLSCREEN in+out` | **the whole-screen repaint.** Part 1 calls it a "visible redraw", Part 5's entire budget table is organised around avoiding it, and no field set has ever put a number on it — because a package cannot reach one. `wm_fullscreen`'s exit is a `wm_paint_all`, and it is the ONE composition call legal from a window callback (below) | **seconds**, and it is method T for that reason. What is in it: the desktop dither, the drive zones, the dock, the menu bar and every visible window — one of which is this report, priced separately by `whole page of rows` |
| `gfxbench: GFX_FILL 64x64 clipped` | **what §11.3's clip region costs a covered background window.** `WM_CLIP_SET+CLEAR` was measured; drawing *under* one never was. It sits next to its own unclipped row, so the gap is the answer | a little over the unclipped row plus the `SET+CLEAR` cell. Much more and `gfx_clip_run`'s re-entry is dearer than the region arithmetic it saves |
| `gfxbench:` the whole **fullscreen block** | **whether a primitive costs what it costs wherever it is drawn.** Same code, same sandbox, different place on the glass, no chrome around it. The rows carry the same labels as their windowed twins so they diff by name | the primitives to be **boring** — landing on their twins. One that does not has found something position-dependent nobody believed was |
| `sysbench: boot ticks` / `boot ms` | **how long the machine takes to boot** (§15.4) — the one thing this project could never measure, because it is over before a package can run. On a floppy machine it is mostly the 125-sector kernel read, and Sets 17/18 took it from 39.88 s to **9.94 s** by fixing §18.91's `AL` bug in both transfer loops - so 238 ms a sector is the number this row was written against and NOT the one to expect now | a number at last. Resolution is one tick, 54.925 ms, which on a boot measured in seconds is quantisation rather than noise |
| `gfxbench: GFX_LSTEP x8` vs **`GFX_LSTEPV x8`** | **§5.6.8's batching, which was argued from §5.7's floor and never measured.** The two rows draw the identical eight pixels and differ only in arriving eight times or once | it already contradicted its own prediction: **118** in instructions, not the ~800 the floor implies, because `gfx_lstep` is not a rect primitive and its arrival is a far-call cell rather than `vga_rect_setup`. Expect higher than 118 on iron — far-call cells are 46.7 µs for ~7 instructions — but §5.6.8's own field figures imply **356**, and nothing reconciles that yet. **This is the row most likely to find something** |
| `gfxbench:` the four **`GFX_LINE`** rows | **§5.6.6's dilated-line optimisation, in microseconds.** The instruction answer is already in (below); this is the duration. The two geometries are the same line transposed, 128 pixels each, so the pair checks itself | the two **thin** rows to match; `line shal fat/thin` near **300** (three walks, the control); `line steep fat/thin` near **156**, which is the claim |
| `sysbench:` the **hard-disk block** | **§52's driver on real spinning MFM, which has never been measured** — and the first hard-disk twin of the floppy rows. Read-only by construction: it mounts, walks the FAT, reads one file and puts the volume back, because the disk it will run against is somebody's DOS 3.3 install (docs/FIELD-MACHINES.md) | anything at all. The floppy is **7,457 bytes/second** post-Set-17 (2,100 before it, and that older pair is quoted all over this tree's history - check which side of the `AL` fix a figure comes from before comparing anything to it); whatever `hdd bytes/sec` says is the first number on the other side of that. `HDD FILE_DFREE` is the one to watch — the 9-sector FAT window (§18.8) has to page across a 41-sector FAT, which is what §18.8.1 was written against |

None of them says anything on an emulator, and two say so loudly: under
`-icount` both shift rows measure identically and the derived per-bit line
reads **0**, which is correct and is the caution block in miniature.

**The two decomposed `lstep` rows are WRONG in the first field set that
carries them, and they are recoverable by hand.** `lstep arrival us x100` and
`lstep pixel us x100` were computed with a raw `sub`/`sbb`, which **underflows
whenever the vector row measures larger than the scalar one** — which is what
noise does the moment the two are close, and the whole point of the pair is
that they might be. What comes out is a nine-digit number (a sighting run
printed `514229986` and `385674937`), so it does not hide, but it is exactly
Part 6 rule 3's failure: arithmetic that looks like a measurement. Both
subtractions go through the floored `gb_sub` now, and an inverted pair reports
an arrival of **0** and gives the whole cost to the pixel, which is what "the
batching saved nothing measurable" honestly means.

Nothing is lost, because **both inputs are printed as their own rows in the
same report**. Take `R_A` = `GFX_LSTEP x8 (8 calls)` and `R_B` =
`GFX_LSTEPV x8 (1 call)`, both µs × 100 per iteration, and redo the two lines:

| | |
|---|---|
| arrival, µs × 100 | `(R_A − R_B) / 7` |
| pixel, µs × 100 | `(R_B − arrival) / 8` |

That is the same pair of equations the harness solves — `R_A = 100(8a + 8p)`,
`R_B = 100(a + 8p)` — so a set taken with the broken build is a complete set
with two rows to recompute, not a set to retake. If `R_B > R_A` the equations
have no positive solution and the answer is the floored one: arrival 0, pixel
`R_B / 8`.

**Reading the fullscreen pairs has one trap, and it is a VGA one.**
`[bb_mono]` (§32) is one-way, and `bb_mono_chk` is five instructions cheaper
once it has retired — so if anything drawn between the two passes used a
colour that is not 0 or 15, every fullscreen row comes in slightly under its
twin for a reason that has nothing to do with fullscreen. It shows as a flat
few instructions per drawing call rather than a proportional gap, and it is
visible in the QEMU sighting run: the VGA `GFX_PIXEL` pair read 408 against
389 while the **CGA pair read 456 against 456**. On the two 1bpp adapters
`bb_init` retires the flag at boot (§39.5), so the columns that matter for
the target machine are a clean A/B.

**And the drag is not there, which is an API fact rather than an omission.**
A benchmark runs inside a window callback, which holds the gfx lock, and
every call that moves or resizes a window forbids it — `OSAPI_WM_RESIZE` says
"Do NOT hold the gfx lock" in as many words, and `WM_SHOW`/`WM_HIDE`/
`WM_FRONT` take it themselves, so from a callback they are a deadlock rather
than a measurement. `wm_fullscreen` and `wm_title_set` are the two exceptions,
and `FULLSCREEN in+out` is what a drag's repaint looks like through them: the
frame changes size and position, and the screen is put back. Timing a real
`ui_drag` would mean a **worker task** doing the composition unlocked while
the UI task formats the results — possible under §20.6, and the reason it was
not done here is that a harness bug is worse than a missing row (rule 8).

### What to record with the numbers

**Every figure here is provisional and carries its machine** (Part 6 rule 8).
A report without the four lines below is worth very little — and *which*
machine, who holds it, and how a build gets onto its floppies are in
[docs/FIELD-MACHINES.md](docs/FIELD-MACHINES.md), because an agent is told
which account it is running as and forgets it, while a fork's name is in the
repo forever.

**A result is not a field result because a human handed it to you.** The 5150's
owner also tests routinely on **PCem**, which models period hardware at period
speed — so unlike a QEMU figure, a PCem one is in the right units and does not
announce itself. Do not assume a number came off the iron unless the run on it
was actually discussed; **ask**, and label the set with what it was:

| | |
|---|---|
| machine | make, CPU, clock, RAM |
| adapter | which card, and whether the kernel probed it or was forced |
| build | the commit the images were built from |
| date | when it was run |

### Set 1 — IBM 5150, 4.77 MHz 8088, 640KB, Hercules

| | |
|---|---|
| machine | IBM PC 5150, 8088 at 4.77 MHz, 640KB (256 on the board, 384 on an AST SixPakPlus), **one** 360KB floppy and a 20MB MFM hard disk, no sound card — [docs/FIELD-MACHINES.md](docs/FIELD-MACHINES.md) |
| adapter | Hercules (720x348), auto-detected. The machine also holds a CGA, permanently, on its own monitor |
| build | `62c4172` (`gfxbench`/`sysbench` in `tests/`) |
| reports | `GFXHERC.TXT`, `SYSBENCH.TXT` |

*(That machine line was recorded as "two floppies" when this set was taken and
is corrected here from the owner's own inventory: one Tandon TM100-2 and a
Seagate ST-225. Nothing in the set rests on it — the floppy rows are
per-sector on the drive that was actually used — but a provenance line that is
wrong is worth fixing where it is read rather than where it was written.)*

**Take the harness's own agreement first, because everything below rests on
it.** The two suites share no measurement code path beyond `benchlib`, and
their four common quantities land on top of each other:

| cross-check | gfxbench | sysbench | apart |
|---|---|---|---|
| RAM `rep stosw`, 2048 B | 34,313 counts | 34,317 | 0.012% |
| RAM read-modify-write | 149,090 | 149,084 | 0.004% |
| loop overhead, 400 iters | 29,701 | 29,699 | 0.007% |

And the timebase checks itself twice. `PIT counts per tick` measured **65,542
against the 65,536 the whole conversion assumes** — 0.009% — so method P and
method T really are the same unit. The CPU-speed estimate, derived
independently from the `MUL` row and the `DIV` row, came back **4.64 and 4.68
MHz** against a nominal 4.7727; the 2% shortfall is the book figure for those
two being a range (118–133 clocks for `MUL`), not a slow machine.

#### The 8088's real instruction cost is a straight line, and it is not a percentage

Part 2 has been ending on "8086-nominal cycle counts under-report an 8088 by
20–40%", from a plan document. That is not the shape of it:

| instruction | bytes | measured clk | 8086 book | ratio |
|---|---|---|---|---|
| `nop` | 1 | 4.34 | 3 | 1.44 |
| `inc r16` | 1 | 4.34 | 2 | 2.17 |
| `xchg ax,r16` | 1 | 4.34 | 3 | 1.44 |
| `mov r16,r16` | 2 | 8.69 | 2 | **4.34** |
| `add r16,r16` | 2 | 8.69 | 3 | 2.89 |
| `cmp r16,r16` | 2 | 8.69 | 3 | 2.89 |
| `shl r16,1` | 2 | 8.69 | 2 | **4.34** |
| `mov al,[si]` | 2 | 15.22 | 13 | 1.17 |
| `mov al,[es:si]` | 3 | 19.00 | 15 | 1.26 |
| `jmp short`, taken | 2 | 18.19 | 15 | 1.21 |
| `mov ax,[disp16]` | 3 | 21.61 | 14 | 1.54 |
| `mov [disp16],ax` | 3 | 24.08 | 15 | 1.60 |
| `push ax`+`pop ax` | 2 | 29.70 | 19 | 1.56 |
| `call near`+`ret` | 4 | 52.13 | 27 | 1.93 |
| `mov ax,i`+`mul r16` | 5 | 132.53 | 129 | **1.02** |
| `xor`+`mov ax,i`+`div r16` | 7 | 162.85 | 160 | **1.01** |

Read the first seven rows down the *bytes* column: **4.34 clocks per
instruction byte, identically, whatever the instruction does.** That is the
4-byte prefetch queue behind an 8-bit bus, and it is a floor, not a tax:

> **An 8088 costs `max(execution clocks, 4.34 x instruction bytes)`**, plus
> ~4 clocks per byte of memory operand, plus a queue refill (~4 clocks per
> byte of the next instructions) after every taken branch.

So the useful question is never "what percentage do I add" — it is whether the
code is execution-bound or fetch-bound. `MUL` and `DIV` measure at 1.01–1.02
because the sequencer is busy long enough to hide every fetch; a run of
register-to-register moves measures at 4.34x because nothing hides anything.
**Shorter encodings are faster than cheaper instructions.** A `shl ax,1` and a
`mov ax,bx` cost exactly the same 8.69 clocks despite the book pricing them at
2 apiece, and `xchg ax,bx` — one byte — beats both at 4.34.

#### The glyph cell: the Part 2 anchor is right

| | |
|---|---|
| `FONT_CHAR`, one cell | **901 us** |
| `FONT_RUN`, per cell of ten | 905 us |
| a whole 78x34 page, per cell | 915 us |

Three routes, three numbers within 1.6%. **Part 2's "~1 ms per 8x8 glyph
cell" is confirmed** — it is 0.90 ms on a Hercules 5150, and that is the
number to keep estimating with.

`font_run` against the hand-written erase-and-letter pair came out at
**1.24x** for the skewed case (`skewPAIR/RUN x100 = 124`). tests/fontbench,
written separately and measured on different hardware, says **1.30**
(SPEC.md §6.1.1). Two harnesses, two machines, 5% apart.

#### A mono fill is bound by its ROWS, not its pixels

This is the finding the two-size design existed to produce, and the third
size is what proved it.

```
GFX_FILL   8x8     (  8 rows,     64 px)   1,128 us
GFX_FILL  64x64    ( 64 rows,  4,096 px)  12,443 us
GFX_FILL 256x128   (128 rows, 32,768 px)  31,682 us

fit to the two large sizes:   177 us per ROW  +  0.28 us per pixel
```

177 us is **840 clocks of setup per scan line**. A 64-pixel-wide row spends
177 us arriving and 18 us drawing: **91% overhead**. The per-pixel half is at
or below what a raw store costs — 0.28 us/px works out to 2.2 us per
framebuffer byte against the 3.26 us/byte a raw `rep stosb` to B000 measures
— so there is nothing to win in the inner loop and most of an order of
magnitude to win in the row setup, on every fill in the system.

**Two caveats on those coefficients, because the model does not quite fit.**
Solving all three sizes for `c + a*rows + b*px` gives a NEGATIVE per-call
term, which means the 8x8 point does not lie on the plane the other two
define; the two-point fit above over-predicts the 8x8 by 1.27x. So treat 177
and 0.28 as a decomposition of the large sizes, not as a law — the *shape*
(row-dominated, inner loop already at the bus) is solid, the coefficients
have a quarter-stop of slack in them.

And the single-slope figure this set's report printed (`fill ns per pixel =
2806`) is wrong as a model: it came from the 8x8/64x64 pair, where the row
cost dominates and is charged to the pixels. The harness prints two slopes
now, so a cost that is not linear in pixels shows itself.

#### The framebuffer is barely slower than RAM, which nothing here assumed

| 2,048 bytes, identical loops | RAM | Hercules B000 | ratio |
|---|---|---|---|
| `rep stosw` | 3,595 us | 5,651 us | **1.57** |
| `rep stosb` | 4,918 us | 6,668 us | 1.36 |
| byte read-modify-write | 31,238 us | 34,169 us | **1.09** |

A Hercules card costs about **4.8 extra clocks per byte written** and almost
nothing on a read-modify-write, because on a 4.77 MHz 8088 the *instructions*
are the bottleneck and the card's wait states hide inside them. The
read-modify-write measures **79.6 clocks per byte** end to end — SPEC.md §39.5
quotes "~30 cycles" — but only ~7 of those 79.6 are the bus. **The figure was
low and it was attributed to the wrong thing:** the mono renderer's inner step
is expensive because it is five 8088 instructions, not because it touches
video memory.

#### What a screenful costs

| operation | measured |
|---|---|
| one `GFX_PIXEL` / `GFX_HLINE 8px` call | 765 us |
| `GFX_FILL 8x8` | 1.13 ms |
| `GFX_FILL 64x64` | 12.4 ms |
| `GFX_FILL 256x128` | 31.7 ms |
| `GFX_SCROLL 256x128` by 8 | 48.2 ms |
| `WM_TITLE` strip (§11.92) | **43 ms** |
| full 78x34 text page | **2.50 s** |
| whole-screen fill, extrapolated | ~0.76 s |
| one vertical retrace period | 18.7 ms (53.5 Hz) |

**2.5 seconds for a page of text** is Part 1's "visible redraw" with a number
on it at last, and it is why §11.90/§11.91's incremental repaints exist. A
title strip at 43 ms is a fifth of a floppy revolution — cheap, and worth the
17 rows §11.92 bought.

The API floor is small enough to ignore and worth knowing exactly:
`GET_TICKS` through the far-call table is **46.7 us**, a near `call`+`ret` is
11.5 us, `SET_COLOR` 48 us, `WM_GEOM` 79 us, `WM_CLIP_SET`+`CLEAR` 328 us,
an ISA status-port `in` 8.7 us.

#### The floppy is one sector per revolution

| | |
|---|---|
| 16 KB read, cold motor | 7.63 s |
| 16 KB read, warm | 7.80 s |
| a one-sector file, open and read | 796 ms |
| throughput | **2,100 bytes/second** |

32 sectors in 7.63 s is **238 ms per sector** — the figure this whole tree
then quoted for years, and the one Sets 16-18 turned into 65 by fixing the
`AL` bug this very row is the symptom of — and a 360KB floppy turns once
every 200 ms — so `dsk_xfer`'s one-`int 13h`-per-sector loop (§18.4.1) catches
**one sector per revolution and misses the other eight**. Warm is not faster
than cold, which confirms it: this is rotational latency, not motor spin-up
and not bandwidth.

That prices two things that were guesses. A 116KB Tracker module is **57
seconds** — which turned out to name a specific bug rather than a general
cost: `dskw_rdata`, the body behind `OSAPI_FILE_READ`, was issuing one int 13h
per sector while its own twin on the write side coalesced runs (SPEC.md
§18.4.2). Measured after: the same load is **295 sectors in 34 calls, against
244**. And a per-track batch — nine sectors per revolution instead of one
— is worth about **9x on every load in the system**, which is the largest
single number in this document.

> **That 9x is REFUTED, and this paragraph is left standing as the thing that
> was wrong** (Set 13, docs/FIELD-NOTES.md 7). A/B'd on this machine with the
> `FLOPPY1=1` knob, the per-track batch is **13% slower on the boot and 15%
> slower on a 16KB read**. It is a model — nine sectors a revolution instead
> of one — and the model is a statement about revolutions that the drive, the
> controller or the media does not honour. Do not cost anything against it.

**Both loops have since taken it** — `dsk_xfer` in SPEC.md §18.91 and the boot
sector in §18.93 — and the *call counts* held exactly as predicted, but the
*time* did not, which is the whole of Set 13. The detour is still worth
recording: the IBM ROM's diskette parameter table says the FDC may not pass
**sector 8** (§18.92), so until both loops installed a table of their own, a
9-sector track cost two commands rather than one. Simulated exactly on the
131-sector kernel, the boot read is **131 int 13h calls → 16 (8.2x)**, so
roughly 31 s → 4–5 s. Honouring the ROM's EOT instead gave 30 calls and
6–9 s, which is what an 8-sector ceiling on a 9-sector track is worth.

#### The kernel's own interrupts cost 1–3%

The same 800-iteration workload, timed with the `cli` window excluding every
interrupt and then again with all of them included. Two runs:

```
run 1   3,430,961 excluded   3,473,408 included   1.2%   (53 ticks)
run 2   3,430,971            3,538,944            3.1%   (54 ticks)
```

The excluded halves agree to 0.001%; the included halves differ by exactly one
tick, because method T quantises to 54.92 ms and this row is only ~1.9 s long.
**So the answer is 1–3% and the method resolves to ±1.9%** — quoting the 1.2%
alone, as the first draft of this section did, was reading a difference of two
numbers to a precision neither has. The tick, the mouse poll and the scheduler
are somewhere under a twentieth of a busy 8088 either way, and there is no
headroom problem.

`TASK_YIELD` — a full switch away and back — is **693 us**. `FILE_DFREE`,
which the SDK correctly says does no disk I/O, is **40 ms**, which is a lot of
FAT walking for a "free" call and is worth a look.

### Set 2 — the same 5150, driven as a CGA, plus a second `sysbench`

| | |
|---|---|
| machine | as Set 1 |
| adapter | **CGA (640x200)**, `VIDEO=cga` forced — the probe finds the Hercules first |
| build | `62c4172` (so it carries Set 1's two bad rows too) |
| reports | `GFXCGA.TXT`, a second `SYSBENCH.TXT` |

#### The harness repeats to 0.05% across two boots

`sysbench` measures nothing adapter-dependent, so running it on both boots is a
straight reproducibility test — twelve rows, two separate power-ups, a
different video card live:

```
loop overhead 29,699 / 29,695     mul       106,029 / 106,030
nop           27,807 / 27,822     div        78,171 /  78,153
mov r16,r16   55,672 / 55,676     RAM stosw  34,317 /  34,311
TASK_YIELD   248,030 / 248,033    FILE_DFREE 2,860,360 / 2,860,368
```

**Worst disagreement: 0.054%.** `PIT counts per tick` read 65,542 the first
time and **65,536 exactly** the second. Whatever else is wrong in these
reports, the measurement is not noisy.

#### The floor is in the CPU, not the framebuffer — and this is the proof

The four RAM rows match Set 1 to 0.015%, as they must. The framebuffer rows do
not, because they are the actual card:

| 2,048 bytes, identical loops | Hercules | CGA | |
|---|---|---|---|
| `rep stosw` | 5,651 us | 6,393 us | CGA **+13%** |
| `rep stosb` | 6,668 us | 7,418 us | +11% |
| word read | 12,777 us | 13,922 us | +9% |
| read-modify-write | 34,169 us | 34,774 us | +2% |
| **VRAM/RAM, word write** | **1.57x** | **1.78x** | |

So a CGA is measurably slower to write than a Hercules — the contention every
period programmer knows about, and it is 13%, not the order of magnitude
folklore suggests, because at 4.77 MHz the 8088 cannot go fast enough to
suffer much. Now put the primitives beside it:

| | Hercules | CGA | |
|---|---|---|---|
| `GFX_PIXEL` | 765.64 us | 765.70 us | **+0.008%** |
| `GFX_HLINE 8px` | 764.82 us | 764.80 us | **-0.003%** |
| `FONT_CHAR` one cell | 901.37 us | 908.56 us | +0.8% |
| `FONT_RUN` 10 aligned | 9,049 us | 9,175 us | +1.4% |
| `GFX_FILL 64x64` | 12,443 us | 12,961 us | +4.2% |

**Two physically different video cards, 13% apart at the bus, and the two
smallest primitives agree to one part in ten thousand.** That is as clean a
proof as this project will ever get that the ~756 us floor is CPU-side setup
and not framebuffer access — and it explains the gradient down the table:
the more of a call's time is actually spent writing pixels, the more the card
shows through (0.0% for a single pixel, 4.2% for a 4,096-pixel fill).

The fill decomposition agrees across the two adapters as well: **182 us per row
+ 0.33 us per pixel** on CGA against 177 + 0.28 on Hercules — the per-row
constant, which is pure setup, is 3% apart; the per-pixel term, which is the
bus, is 16% apart.

#### And the page repaint agrees across two screen heights

| | rows | measured | per row | per cell |
|---|---|---|---|---|
| Hercules | 34 + status | 2,499 ms | 71,403 us | 915 us |
| CGA | 16 + status | 1,236 ms | 72,696 us | 932 us |

Half the screen, half the time, **1.8% apart per row** — two independent
measurements of the same quantity on the same machine. A text page costs
roughly **72 ms per 78-cell row** on a 4.77 MHz 8088 whatever it is displayed
on.

#### Three rows of these sets are wrong

Recorded here rather than quietly re-run, because Part 6 rule 8 applies to the
apparatus too:

- **`RAM repe scasb` (25.77 us) is meaningless.** `repe` repeats *while
  equal*, so scanning for a byte that is never there stops at the first
  comparison. It should be `repne`. Fixed after this set; the row is junk in
  this one.
- **`one full-width row` (14.5 ms) is not a row of text.** It draws ten
  glyphs and 68 spaces, and a space cell is ~5x cheaper than a glyph on
  `font_run`'s fast path — 186 us/cell against the 915 us/cell a real page
  measures. That is exactly why the report prints `page predicted` beside
  `page measured`: they came out 0.49 s and 2.50 s, the check fired, and the
  fault was in the predictor. Fixed after this set; **the 2.50 s measurement
  is the good one**.
- **`one retrace period` is biased low by up to one frame in N.** The body
  waits for the retrace bit to fall and then rise, so it *leaves* the phase at
  a rising edge and every later iteration is a whole frame — but the first
  starts wherever the suite happened to be and can return almost at once. At
  N = 4 that is a quarter of the answer: the CGA read **80.6 Hz** where three
  of its four iterations were a clean **60.4 Hz**, and Hercules read 53.5 Hz
  against a card that runs at 50. Fixed after these sets with an untimed
  priming call and N = 12; **treat both retrace figures here as ~1 frame low.**
- **`GFX_BLIT4`'s striped row LAPPED THE COUNTER TEN TIMES**, on both adapters
  (Hercules 12.0 ms, CGA 13.2 ms reported; both ~10 laps short of ~20x their
  solid row). Everything that looked wrong about the blit was that. See below.

#### The blit anomaly was a lapped counter, and settling it produced the real finding

As published, the set said `GFX_BLIT4` was **2.36x slower with a solid source
than with 4-pixel runs** — 28.2 ms against 12.0 ms for the same 4,096 pixels.
That is backwards: a long run is the coalescer's best case, and the same
package measures 13-20x the *other* way on every adapter under QEMU.

Reading `gfx_blit4` cleared the primitive: the scanner is right, and
`gfx_blit_run` emits exactly one `gfx_hline` per coalesced run — so the solid
source makes 64 calls of 64 px and the striped one makes 1,024 calls of 4 px.
The striped source therefore does about **20x the work**, which is exactly what
the emulator says. The arithmetic then falls out:

```
reported striped         12.0 ms
+ 10 PIT laps (54.92 ms) 549.2 ms
= true                  561.2 ms   -> 19.87x the solid row
QEMU/Hercules work ratio             20.3x
```

**`bl_fold`'s modular subtraction is correct and its `!` guard is not enough.**
The guard flags an iteration *approaching* the wrap; an iteration that laps
reports its remainder, which is small, plausible, and unflagged. A 561 ms body
published itself as 12 ms.

The harness now brackets every method-P row with the tick counter and re-runs
it under method T when they contradict each other (flag `w`). One subtlety cost
a debugging round and is worth repeating: **ticks cannot measure a lapping row,
only detect one.** With IF = 0 the 8259 latches a single pending IRQ0 however
many the PIT raises, so a body that laps ten times still yields one tick — the
tick count under-reports by the lap factor. The usable test is `ticks >= N`,
which says *every* iteration crossed a tick boundary and contradicts any PIT
total claiming they were shorter than 54.92 ms. Verified by injecting a body
that laps deliberately: unflagged and 5x low before, flagged and correct after,
with no false positive anywhere else in the suite.

#### And so: a `gfx_hline` costs ~0.5 ms whatever its length

With the blit row corrected, the number that looked like a contradiction
becomes the corroboration:

| route to one `gfx_hline` | per call |
|---|---|
| solid blit, 64 calls in 28.2 ms | 441 us |
| striped blit, 1,024 calls in 561 ms | 548 us |
| `GFX_HLINE 8px` through the API | 765 us |

`GFX_PIXEL` measured 765.64 us and `GFX_HLINE 8px` 764.82 us — two different
routines agreeing to 0.1% — and fitting the two hline sizes gives **756 us
fixed + 1.16 us per pixel**. So the cost of a small drawing call is almost
entirely a fixed **~3,600 clocks**, three independent routes agree on it to
within the difference between a 4-, 8- and 64-pixel line, and the API far-call
cell is not it (`GET_TICKS` through the same table is 46.7 us).

**That is the largest single lever in the graphics system, and it is the same
lever the fill block found from the other side.** A fill costs ~177 us per
scan line with the pixels nearly free; an hline costs ~756 us with the pixels
nearly free. Both say the per-call and per-row setup in the mono renderer
dwarfs the drawing, and both say the inner loops are already at the bus. A
redraw is priced by **how many primitive calls it makes**, not by how many
pixels it covers — which is the opposite of the assumption every estimate in
Part 2 was built on.

### Set 3 — the floor taken apart, and a fifth of it removed

| | |
|---|---|
| machine | **not a machine** — QEMU with `-icount shift=3,sleep=off`, so the PIT counts guest INSTRUCTIONS (Part 4). Reproducible, machine-independent, **not time** |
| adapter | CGA 640x200 (`VIDEO=cga`) for the mono renderer; VGA 640x480 for the planar one; Hercules for the pixel check only |
| build | `dc92330` against the same tree plus the §5.7 changes |
| date | 2026-08-06 |

Set 1 and Set 2 said the floor was CPU-side setup but not **which** setup, so
the first thing done here was to count it rather than guess (rule 4). One
`gfx_pixel` on the 1bpp renderer is **196 guest instructions**, and the
static path agrees: they are spread over eleven routines with no hot spot
anywhere — the API far-call cell, `gfx_pixel`'s rect marshalling, §11.3's
clip test, `bb_mono_chk`, the `[bb_on]` dispatch, `vga_rect_setup`,
`gfx_rowbase`, `bb_dirty_rect`, `bb_ink`, `bb_plane_op`, `bb_col`. **About a
third of it was register discipline and call structure** — 13 push/pop pairs
at Part 2's measured 29.7 clocks and ~10 near call/rets at 52.1 — and none
of it was drawing. SPEC.md §5.7 lists the seven changes and why each is a
rule rather than a tidy-up.

The mono renderer, before and after, in PIT counts over N iterations:

| row | N | before | after | repeat | per call |
|---|---|---|---|---|---|
| `GFX_PIXEL` | 300 | 560 | 451 | 449 | **−19.6%** |
| `GFX_FILL 8x8` | 200 | 531 | 430 | 427 | −19.3% |
| `GFX_VLINE 8px` | 200 | 538 | 437 | 435 | −18.9% |
| `GFX_FRAME 64x64` | 24 | 573 | 468 | — | −18.3% |
| `GFX_HLINE 8px` | 200 | 375 | 314 | 310 | −16.8% |
| `GFX_FILL 64x64` | 24 | 688 | 588 | 587 | −14.6% |
| `GFX_BLIT4 4px runs` | 6 | 15,930 | 13,713 | 13,711 | −13.9% |
| `GFX_FILL_GRAY 64x64` | 24 | 680 | 586 | 585 | −13.9% |
| `GFX_BLIT4 solid` | 12 | 2,637 | 2,276 | 2,275 | −13.7% |
| `GFX_XOR_FILL 64x64` | 24 | 710 | 616 | 614 | −13.4% |
| `WM_TITLE strip` (§11.92) | 4 | 420 | 368 | 367 | −12.4% |
| `GFX_FILL 256x128` | 6 | 419 | 372 | — | −11.2% |

**The control rows are the point of the table, not an afterthought** (rule
7): `GFX_FILL_PAT 64x64` (806 → 802), `GFX_SCROLL 256x128` (1,351 → 1,354)
and `one full-width row` of text (2,220 → 2,219) are the three drawing paths
none of these changes touch, and all three sat still. A harness that had
moved them would have been measuring itself.

The same suite on **VGA**, where the planar VRAM bodies run and `bb_col`
never does, so only the shared coordinate core changed: `GFX_PIXEL` 424 →
404 (−4.7%), `GFX_FILL 8x8` 338 → 317 (−6.2%), `GFX_BLIT4 4px runs` 13,110
→ 12,192 (−7.0%), `GFX_FILL 64x64` and `GFX_SCROLL` unmoved. Nothing on
either adapter got worse.

**Two honest limits.** First, a repeat run of the whole suite on the
unchanged kernel puts the noise floor where you would expect: rows in the
hundreds of counts repeat to under 1% (`GFX_BLIT4 solid` to 0.04%), and rows
in single digits — `GET_TICKS`, `MOUSE`, `ISA status port in`, `GFX_HLINE
256px` — swing by more than the effect and **must not be quoted from this
set at all**. Second and more important, **icount counts instructions and
the question was clocks**. Three of the seven changes remove clocks without
removing instructions: two variable shifts (8 clocks plus 4 per bit), a
`shl bx,13` (60), and push/pop pairs (one instruction, 15 clocks each). A
hand model over the changed sequences, priced from Set 1's own measured
per-class table, puts the pixel path at about **−620 clocks of 3,600, −17%**
— which agrees with the −19.6% instruction figure to within the precision
either deserves. **What would settle it is `gfxbench` on the 5150 again**,
and until that happens Part 2's 756 us stands as the number to estimate
with.

Rendering was verified byte-for-byte rather than by eye, on all three
adapters and both renderers, over a fixture of desktop dither, window
frames, a file listing, an XOR selection band, a pull-down menu and its
save-under restore: **CGA pixel-identical** bar the menu-bar clock's last
glyph cell, **VGA pixel-identical** with the §32 back buffer both off and
on, and **Hercules** differing by 10 pixels of menu bar — against 17 between
two boots of the *same* kernel, so below that fixture's own reproducibility.

#### What is left, and what it would cost

Priced from the same teardown, for whoever comes next:

| still on the floor | worth | why it was not taken |
|---|---|---|
| `gfx_rowbase`'s `mul` by the stride | ~145 clocks, 4% | a per-row table is 2 bytes x `[vid_h]` — 960 on VGA, and `KERN_BUDGET` has 1,536 left |
| `bb_rect`'s eight push/pop pairs | ~240 clocks, 7% | it is `gfx_fill`'s "clobbers flags" contract, which every caller in the tree leans on |
| the API far-call cell | ~223 clocks, 6% | the package ABI (§20.1) |
| a dedicated 1-row body for `gfx_hline`/`gfx_pixel` | maybe 25% of what remains | a second implementation of the same pixels, ~100 bytes, and Part 3 item 4's exact failure mode |
| a one-entry memo on `gfx_rowbase` | ~4% of a text row (78 cells share a y) | it is a *loss* on the single-call case this section is about — the wrong trade for the headline number, the right one for text |

### Set 4 — MartyPC, and the first log that could be trusted against another

| | |
|---|---|
| machine | **MartyPC**, a cycle-accurate IBM 5150 emulator, 4.77 MHz 8088, Hercules |
| adapter | Hercules 720x348, content 628x247 (fullscreen window), CPU tier 0, coarse explosion on |
| build | `523cff1` plus the debug frame logger (never committed — `tools`-side script) |
| date | 2026-08-07 |
| run | 77 seconds of Missile Command, fullscreen, heavy play — 1,233 frames, **mean 16.0 fps against the 18 the tick allows** |

Sets 1–3 were taken by a harness that runs once and reports. This one is
different in kind: it is a **77-second log of a real application**, one row a
second, with the frame split into stages — and it is the first that carries
its own **calibration**, which is what makes it comparable to the next one.

Two earlier logs from a different machine could not be compared at all, and
nothing in either said so: between them `lok` went 5.24 → 6.09 ms and `unl`
4.79 → 5.81 ms, which is kernel code neither run touched. So a run now times
a fixed, known quantity of work at each end and prints it.

| CAL | cpu | call | rows |
|---|---|---|---|
| start | 48,227 | 61,303 | 175,991 |
| end | 48,223 | 51,461 | 164,983 |
| drift | **0.01%** | −16.1% | −6.3% |

**The CPU number agreeing to one part in ten thousand is the whole point**:
the machine did not move under the measurement, so every row between the two
is comparable to every other. And the two fill references disagreeing is not
noise — it is a measurement nobody set out to take. The `start` sample runs
inside `mc_render` with a **clip region armed** (§11.3) and the `end` one from
a menu callback with none, so the 16% is **what arming the clip costs a
`gfx_fill` call**. The per-scan-line figure, which the clip does not touch,
agrees to 1% across the pair.

| this machine | | Part 2's 5150 |
|---|---|---|
| per-call floor | **1,078 µs** unclipped, **1,284 µs** clipped | 756 µs |
| per scan line | **126 µs** (150.9 and 149.4 counts) | 177 µs |

The per-call figures are **not** `gfxbench`'s number: they are a fill as an
*application* issues one, so they carry the API far call, `mc_fillc`'s clamp
and its overlay hook. The per-scan-line figure is inside the kernel loop and
is comparable — and it is **lower** than Part 2's.

#### Where 77 seconds went

| stage | seconds | of wall |
|---|---|---|
| `lok` — gfx lock + `mc_track` + `wm_clip_set` | 9.68 | **12.6%** |
| `mov` — drawing this frame's trail segments | 9.07 | 11.8% |
| `wip` — erasing dead missiles' trails | 7.90 | 10.3% |
| `crs` — the crosshair overlay | 7.29 | 9.5% |
| `unl` — gfx unlock | 7.08 | **9.2%** |
| `exp` — the explosions | 5.85 | 7.6% |
| `upd` — all game logic | 5.25 | 6.8% |
| `rst` — terrain, status, banner | 3.84 | 5.0% |

**`lok` + `unl` is 21.8% of the session — the largest item in the game, and
neither draws a single pixel of it.** 6.2 ms and 5.7 ms are paid on *every*
frame, including ones whose content is unchanged; on a 55 ms tick that is a
fifth of the machine spent entering and leaving the drawing critical section.
It is also the one item here that is entirely kernel-side, so whatever is in
it is being paid by every application on every machine.

#### A line pixel costs more than a fill ROW

The sharpest number in the set, and it is a kernel one rather than a game one:

| | |
|---|---|
| dilated trail erases in the run | 82, of which **31 steep (38%)** |
| trail pixels walked | 15,807 |
| per erase | **96 ms** over 193 pixels — nearly two whole ticks for one dead missile |
| **per trail pixel** | **500 µs** |
| a `gfx_fill` scan line, same machine | **126 µs** |

A dilated line pixel costs **4× what a whole 64-pixel fill row costs**, and
even undilated it would be ~167 µs — still more than a fill row. §5.6.1 puts
the `gfx_line`/`gfx_fill` crossover at "~27px" and that was an estimate; the
measured ratio says the walk is far dearer per pixel than it assumed, and the
crossover wants re-deriving from these two numbers rather than from the old
one.

Two caveats, both worth carrying: 38% steep means §5.6.6's one-walk path
fires on a **minority** of these erases (a QEMU count said 53% — the real
machine's play says 38%), and the 500 µs does not fully decompose into three
Bresenham passes at the instruction floor, so something in `mc_wipe_trails`
beyond the walk is unaccounted for and has not been found yet.

### Set 5/6 — the trail work, after §48.14 and §48.15

| | |
|---|---|
| machine | a 5150-class emulator, Hercules, CPU tier 0, coarse explosion on |
| adapter | Hercules 720x348, content 628x247, **surface 5** — the §53.7 same-mode exclusive bracket |
| build | `444e87d` plus the debug frame logger (never committed) |
| date | 2026-08-07 |
| run | 71 seconds of Missile Command, heavy play — 62 usable rows |

**`lok` and `unl` are 0 on every row**, which is §48.13's bracket confirmed
in the field: entering and leaving the drawing critical section, which Set 4
priced at 21.8% of the machine, now costs nothing at all. `all` is 0 too — no
stray full repaints.

The run splits cleanly in two, and the split is the finding:

| stage | stuttering (13 s, 14.2 fps) | keeping up (49 s, 17.8 fps) |
|---|---|---|
| frame (`upd`+`ren`) | **46.24 ms** | 21.26 ms |
| `exp` explosions | 10.50 | 2.36 |
| `mov` trail draw | 10.20 | 6.52 |
| `drn` drain | 9.11 | 1.92 |
| `upd` update | 6.14 | 2.43 |
| `rst` terrain/status | 3.99 | 2.21 |
| `crs` crosshair | 3.60 | 3.26 |
| `wip` teardown | 1.03 | 0.67 |
| drained pixels a frame | 46.6 (cap 64) | 8.4 |

**A stuttering second's MEAN frame is under the tick.** 46.2 ms against
54.9 — it is the tail that crosses, and that is why this reads as a stutter
rather than the freezes Set 4 carried.

#### The pixel is honest; the arrival is not

| | |
|---|---|
| `drn`: 46.6 px a frame for 9.11 ms | **~160 µs a walked pixel** |
| `mov`: ~19 px a frame for 10.20 ms | **~570 µs a walked pixel** |

The same operation, three and a half times apart. `gfx_lstep_mono` and
`gfx_line_mono` are the *same loop*, so §48.14 did not make a pixel dearer —
it stopped drawing each one three times, and 160 µs is Set 4's 500 µs divided
by the three passes it removed, to within the noise. What separates the two
rows is **how many times the caller arrived**: `mov` was one far call per
live missile per frame (seven or eight of them, §5.7's ~756 µs floor each, to
move two or three pixels), while the drain already handed several trails over
per call.

That is §5.6.8 and §48.16: ten calls a frame become three. The remainder —
`exp` at 10.5 ms with 8.8 fills and 34.6 scan lines a frame — is at the
structural floor §48.12 left it at and is priced entirely by §5.7.

#### The calibration disagreed with itself, and the reason is known

`CAL start 48299 51523 140563` against `CAL end 49682 44255 135015`: cpu 2.9%
apart, **call 14%**, rows 3.9%. That is not the machine moving. `start` runs
with a clip region armed and `end` does not, and the gap is the clip's
per-call cost — the same 16% Set 4 saw. Use the **end** figures for a
per-call floor (44255/40 = 1,106 counts = **927 µs**) and treat the start ones
as the clipped price of the same call.
### Set 7 — the lock and unlock, which turned out to be the cursor

| | |
|---|---|
| machine | **not a machine** — QEMU with `-icount shift=3,sleep=off`, so the PIT counts guest INSTRUCTIONS (Part 4). Reproducible, machine-independent, **not time** |
| adapter | all three: Hercules 720x348 (`VIDEO=herc`), CGA 640x200 (`VIDEO=cga`), VGA 640x480 |
| build | `079d9a8` against the same tree plus the §7.1 changes |
| date | 2026-08-07 |

Set 4 said `lok` + `unl` was **21.8% of a 77-second session** with no pixel
of the game in it, and that it was the one item in that table entirely inside
the kernel — so every application on every machine was paying it. It did not
say what was in it, because a stage-level frame log cannot: `lok` there is
`gfx_lock` *plus* `mc_track` *plus* `wm_clip_set`.

**Set 5/6 above and this one answer different questions, and both answers
stand.** §48.13 took Missile Command *out* of the pair — a same-mode
exclusive bracket never unlocks, so `lok` and `unl` are 0 on every row of
that log, and for that one app on that one surface there is nothing left to
optimise. But a bracket is what a full-screen arcade game can do, not what
the Disk window, Note Pad, the Task Manager, Paint, the Control Panel or any
ordinary windowed app can: they take and drop the lock on every redraw, by
design, and Set 4's 21.8% is what that costs them. Set 5/6 is the escape
hatch measured; this is the toll booth made cheaper for everyone still
driving through it.

So the first thing done here was to give the pair a row of its own.
`gfxbench`'s `GFX_UNLOCK+LOCK pair` is that row, and it is measured
**backwards** — `OSAPI_GFX_LOCK` from a callback that already holds the lock
is a deadlock, but unlock-then-lock is the same two routines in the other
order, and `fm_drag` already uses that idiom inside a callback. The two
halves cannot be separated from a package because they must alternate.

**The mutex is not what it costs.** `gfx_lock_flag` is one byte and six
instructions; everything else in those two routines is the **mouse cursor** —
the lock erases it, the unlock saves what it will cover and draws it again.
SPEC.md §7.1 is what was done about it; the short version is that the arrow
is 8x12 inside 16x16 tables nobody had ever measured against, that its width
makes the cell's third framebuffer byte unreachable, and that on a 1bpp
adapter the save and the two draw passes are the same bytes in the same order
and compose into one.

| adapter | before | repeat | after | repeat | per pair, after | ratio |
|---|---|---|---|---|---|---|
| Hercules | 1,782 | — | **541** | 541 | 5.41 counts | **3.29x** |
| CGA | 1,798 | — | **547** | — | 5.47 | **3.29x** |
| VGA | 2,300 | 2,296 | **1,626** | 1,633 | 16.26 | **1.41x** |
| VGA, draw fused too | 2,300 | 2,296 | **1,495** | — | 14.95 | **1.54x** |

100 iterations a row. The two mono columns agreeing to 1.1% before and 1.1%
after is the harness checking itself: they are the same renderer over four
different numbers, so a gap between them would be a finding about `gfxbench`
rather than about the cursor. VGA gains least and should — its save is four
planes through Read Map Select and its draw is Set/Reset through the Bit
Mask, so there is no single byte to fuse and all it gets is the 8x12 cell and
the retired third byte.

Through Part 4's conversion (one icount count ≈ 0.359 ms of real XT), the
pair on the field machine goes from **~6.4 ms to ~1.94 ms**, and a 55 ms
frame that spent 11.6% of itself entering and leaving the critical section
now spends 3.5%.

**The output is byte-identical**, which for a save-under is the only thing
worth asserting: the framebuffer was captured with the cursor parked at five
places — over the Disk window's glyphs byte-aligned and at shift 3, against
the right screen edge where the second byte is clipped away, against the
bottom edge where the arrow is cut short, and on the bare desktop — on all
three adapters, before and after. **0 differing pixels** every time. The
same capture, taken again after moving the cursor away and back, is also 0,
which is the erase checking itself within one run.

#### The control rows, and the one that needed a second look

Every other row in the three reports tracks the **loop-overhead baseline**,
which is re-measured per run and is not perfectly stable: it moved 55 → 66
counts over `BL_BASE_N` = 400 on VGA between the two runs, and every method-P
row is reported net of `ovhx x N / 256`. So a row at N = 300 comes in about 8
counts lower for that reason alone and nothing else. Read the small-N rows
against that number before reading anything into them:

| VGA row | N | before | after | baseline explains | left over |
|---|---|---|---|---|---|
| `GFX_PIXEL` | 300 | 407 | 396 | 8.3 | −2.7 |
| `GFX_HLINE 8px` | 200 | 273 | 267 | 5.5 | −0.5 |
| `GFX_VLINE 8px` | 200 | 332 | 330 | 5.5 | +3.5 |
| `GFX_FILL 64x64` | 24 | 370 | 370 | 0.7 | +0.7 |
| `GFX_FILL 256x1` | 100 | 22,085 | 22,083 | 2.8 | +0.8 |

`GFX_FILL 8x8` looked like the exception on the first pass — 340 → 316, where
the baseline explains only 5.5 — and it is worth the paragraph because the
**wrong** conclusion was available and attractive: a 7% win on a primitive
this change never touched. Two things said not to take it. A row that reads
*below* `GFX_VLINE 8px` is not a small win but an impossible one, since a
vline is a single-byte rect and an 8x8 fill is the same rect eight pixels
wide. And `GFX_PIXEL` reaches `gfx_fill` through the identical path, so any
per-call saving there would have shown up in it, and did not (−2.7 against a
predicted −14 if `bb_mono_chk` had been the mechanism).

So both kernels were re-run. **They agree: 317 after, 317 before.** The 340
was a one-off excursion in a single baseline run and the change did nothing
to VGA's fill at all. `GFX_UNLOCK+LOCK pair` itself repeats to within 0.4% on
both kernels and both adapters, which is what makes it quotable.

**One row moving on its own, in the direction you were hoping for, is the
thing to re-run** — Part 6 rule 7, and the reason Set 3 carries a repeat
column.

#### How much is left: the pair is almost never needed

The same run instrumented the *other* question — not what the pair costs but
how often it is earned. `gfx_lock` snapshots the 8x12 cell after the erase
and `gfx_unlock` compares it before the redraw, so "did anything write there
during this hold" is answered by the pixels rather than by a hook, which
catches paths no hook could. Over one session (boot, two folders, a window
drag, two package loads, 26 keystrokes, idle): **5,020 pairs, 7 of them
(0.14%) with anything written into the cursor cell**, 40 with the mouse
moving, 0.7 drawing calls per hold. Note Pad's 26 keystrokes were 25 pairs,
none dirty; an idle desktop is 0.

The sharpest case is not a frame loop but a **press-and-hold**: `fm_drag`'s
wait loop is `gfx_unlock` / `task_yield` / `gfx_lock` per iteration and draws
nothing, so on the field machine holding the button over a file turns the
pair over about 500 times a second — which is a *blink*, not just a bill.
`ui_task` already says as much where it gates the clock's lock: "taking the
gfx lock blinks the cursor, and that blink IS the flicker the
seconds-in-menu-bar setting exists to remove."

SPEC.md §7.1.1 has the economics and the nine ways a lazy hide could miss —
**and why it was measured a second time and then dropped.** About 4,900 of
those 5,020 holds were `fm_drag`'s unpaced `.wait` loop spinning the lock
while a button was held, not application drawing. Pacing that loop (§7.1.3)
makes the identical session **120 holds**, of which 46 either drew into the
cursor cell or moved the mouse — so a lazy hide would now save ~74 x 1.94 ms
= **144 ms across several minutes**, for a check at every framebuffer-writing
path, a permanent-smear failure mode, and a new package ABI contract to cover
the one path that cannot be hooked. Re-measure before building an
optimisation whose justification predates its neighbours' fixes.

### Set 9 — MartyPC, and the first log with a working MAX line

| | |
|---|---|
| machine | **MartyPC**, cycle-accurate IBM 5150, 4.77 MHz 8088, Hercules |
| adapter | Hercules 720x348, content 628x247, surface 5 (§53.7 same-mode bracket) |
| build | `c39eb93` plus the debug frame logger (never committed) |
| date | 2026-08-07 |
| run | 60 usable seconds, mean **15.63 fps** |

**Sets 6-8 were PCem and this one is MartyPC**, which the reporter did not
know at the time and the **calibration caught anyway** — which is the whole
reason it is in the log (§docs/FIELD-MACHINES.md: *a number is not a field
number because a human handed it to you*):

| | cpu | per-call | per-scan-line |
|---|---|---|---|
| PCem, Sets 7-8 | 48,300-48,544 | 1,072 counts (899 µs) | 121 counts (102 µs) |
| MartyPC, Set 9 | 49,788 | **1,246 counts (1,044 µs)** | **149 counts (125 µs)** |
| the real 5150 (Part 2) | — | ~900 counts (756 µs) | ~211 counts (177 µs) |

The **CPU agrees to 0.7%** and the *drawing* does not: MartyPC prices a call
16% dearer and a scan line 21% dearer. That is the shape you would expect
from cycle-accurate bus modelling — framebuffer writes on an 8088 are
bus-bound and PCem is optimistic about them — and it means **no absolute
figure from Sets 6-8 may be compared with one from Set 9**, only ratios
within a set.

#### The MAX line: a per-second mean cannot describe a per-frame spike

One row a second gives the worst *single* frame's stage split in raw counts.
It is the only instrument in this investigation that answered the question
asked, and the previous three logs could not have:

| | |
|---|---|
| seconds whose worst frame exceeded one tick (54.9 ms) | **33 of 60** |
| of those, biggest stage `exp` | **20** |
| ...`rst` | 8 |
| ...`mov` / `all` / `upd` | 3 / 1 / 1 |
| worst three frames | 126.5, 111.3, 107.8 ms |
| `exp` in those three | **76.1, 65.8, 74.0 ms** |
| fills on a worst frame (mean) | **34.8**, against those seconds' mean of 19.1 |

So the stutter is a **spike, not a load** — a second whose stages average
60 ms containing one frame of 126 — and the two things that spike are a
salvo's explosions redrawing on the same frame and the status strip
re-lettering 29 cells to change one digit. §48.17 is both.

The instrument itself had to be fixed first, and the bug is worth recording:
the frame-start snapshot was taken in the worker's `.frame:` hook, and the
**catch-up path runs a frame without passing it** — the same catch-up the
reporter had noticed as trails "playing in fast motion" after a file save. So
across a second boundary the snapshot went stale under a reset, the delta came
out small and *negative*, and an unsigned comparison read −303 as four billion
and latched it for the whole second. The tell is `3599181` repeated across a
row, which is 2³² counts in milliseconds. It is banked at frame *end* now.

### Set 10 — the first valid before/after, and both fixes missed

| | |
|---|---|
| machine | MartyPC again — **per-call floor 1,247 counts against Set 9's 1,246, 0.15% apart** |
| build | `1156e3c` plus the logger |
| run | 71 seconds, mean 15.97 fps (Set 9: 15.63) |

This is the first pair in the whole investigation that may be compared
number for number, and it says §48.17's two changes did nothing at the frame
level:

| worst-frame stage | Set 9 median | Set 10 median | Set 9 p90 | Set 10 p90 |
|---|---|---|---|---|
| `exp` | 19.0 | 16.6 | 39.4 | 39.9 |
| `rst` | 7.1 | 9.6 | 28.4 | 27.7 |
| seconds with a frame over one tick | 55% | 55% | | |

Both hypotheses were wrong. The salvo was never synchronised — an ICBM is
intercepted as a blast radius *grows*, so a cluster's detonations already
spread over several frames. And `rst`'s 20-49 ms was not the status strip:
the inference rested on "only eleven fills in that frame, so it must be
text", and **the fill counter cannot see a glyph**, so that was never
evidence either way. The strip fix is kept because it is strictly less work
for byte-identical output; the ramp jitter is dropped because it costs
lethality and buys nothing (§48.10's rule).

#### What the same numbers say when the arithmetic is done properly

| | |
|---|---|
| a worst frame | **34.8 fills, 172.6 scan lines** |
| priced at this run's own calibration | 34.8×1,247 + 137.8×151 = **53.8 ms** |
| the median worst frame | **63.4 ms** |
| the ARRIVING alone | 34.8×1,096 = **36.4 ms — 57% of the frame** |

**~85% of a worst frame is `gfx_fill`.** One blob is 7-11 rects (simulated
against `mc_blob`'s own band logic), so `exp` at 40-76 ms is three to six
blobs and nothing else. §48.18 is the three changes that cut the count.

#### The negative result: a vector gfx_fill recovers ~4%

Costed before building, against this run's floor. A batch removes the API
cell, the `GFXCLIP` test, the `bb_mono_chk` call and the `bb_on` dispatch —
about **170 of ~4,381 clocks**. It cannot remove eight push/pop pairs,
`vga_rect_setup`'s twenty-odd memory accesses, `gfx_rowbase`, the dirty-rect
and mode round-trips, the plane loop or `bb_ink`, because those are per
*rect* and not per *call*.

That is the difference from §5.6.8, which did pay: a walk step's fixed cost
was block staging and `gfx_ink` — genuinely per call — while a fill's is
geometry. Shortening *that* is §5.7's problem: diffuse, no hot spot, already
worth 20% once, and wanting a dedicated pass with `tests/gfxbench` as the
gate rather than a new API slot.

### Set 11 — the 5150 again, and four more machines beside it

**Five machines, eight reports, and the provenance matters more than usual**
— three of them are real iron and two are emulators, and the two emulators
were run *to be a delta against the 5150*, not to price anything. Nothing
below treats a PCem or MartyPC figure as a measurement of os8088.

| set | machine | adapter | notes |
|---|---|---|---|
| **11a** | IBM 5150, 4.77 MHz 8088, 640KB | Hercules GB101 | the calibration machine (docs/FIELD-MACHINES.md) |
| **11b** | ...the same 5150 | IBM CGA, `VIDEO=cga` build | both cards are permanent; the build ignores the Hercules |
| **11c** | Toshiba T1100 Plus | CGA (LCD) | tier 0, and the instruction table says **16-bit bus at ~7.1 MHz** |
| **11d** | PCem, MartyPC | both adapters, both | claim a 4.77 MHz 8088; **delta only** |

Build: `16844dd` field disks. PCem's two columns are the same machine with the
video config changed and agree everywhere outside the video block, as expected.

**A fifth set was taken and is deliberately DISCARDED**, on the owner's
instruction and for a reason worth writing down rather than deleting: a
Packard Bell Victory 286 (16 MHz AMD, 4MB, onboard **Paradise PVGA1A**). It is
a VGA machine, its files were hand-renamed `GFXVGA.TXT`, and the reports
self-identify as `CGA 640x200 mono` — because the field disk is a `VIDEO=cga`
build, so what was measured is **a Paradise VGA driven through the CGA
framebuffer path on a 286**. That is a fourth thing, not a data point on any
of the three the project supports, and two of its derived rows are actively
misleading: `est CPU MHz x100` read **8866** and `shl clk/bit x100` read
**29**, both because they are computed against 8088 instruction timings that a
286 does not have. Keeping the numbers would cost a future reader more than it
gives them. The machine itself is in docs/FIELD-MACHINES.md; what it needs
before it is worth running again is a **VGA** field disk, which does not exist.

#### MartyPC is the real thing on the CPU and not on the disk

Against 11a, row for row, **MartyPC lands within 0–4% on 45 of 47 gfxbench
rows** — the closest agreement any emulator has managed here, and enough that
its "cycle accurate" claim survives contact with a 5150. PCem is uniformly
**10–20% fast**. Three rows are the exceptions and each says something:

| row | 5150 | PCem | MartyPC |
|---|---|---|---|
| `one retrace period` | **19,473 µs** (51.4 Hz, right for Hercules) | 9,533 | 9,501 |
| `VRAM write word` | 5,647 µs | 2,529 (**no VRAM cost at all**) | 6,082 |
| `read 16K, cold motor` | **8.07 s** | 1.10 s | **0.27 s** |
| `boot ms` | **38,886** | 5,108 | 2,306 |

So: **neither emulator models floppy rotational latency**, which is the one
thing the last two disk optimisations were aimed at, and PCem additionally
gives Hercules VRAM the speed of RAM. Use MartyPC for CPU and drawing work,
and neither for anything with a disk in it.

#### The two disk optimisations bought nothing on the iron

This is the finding of the set, and it is a **regression against a
prediction, not against a measurement**. Same machine, same test, same
media, kernel before and after both §18.4.2's run coalescing and §18.91's
per-track batching:

| | Set 1 (before) | Set 11a (after) |
|---|---|---|
| 16 KB read, cold motor | 7.63 s | **8.07 s** |
| 16 KB read, warm | 7.80 s | **8.18 s** |
| a one-sector file | 796 ms | 796 ms |
| throughput | 2,100 B/s | **2,001 B/s** |

Set 1 priced the per-track batch at "about **9x** on every load in the
system, which is the largest single number in this document". Measured: it
is **1.0x**, and if anything 6% the wrong way. The boot agrees — 138 kernel
sectors at the unbatched 238 ms is 32.8 s, and `boot ticks` says **708
(38.9 s)** against a predicted 4–5.

Two things rule out the obvious explanations. The file is **contiguous** —
every file on the field image is `runs=1`, so the coalescer hands `dsk_xfer`
one 32-sector run — and the T1100 Plus, a *different* real machine with a
different drive, reads at **2,161 B/s**, the same wall — and its maintenance
manual gives that drive as **300 RPM, 250 kbit/s, 100 ms average latency**,
the same revolution the 5150's 360KB Tandon turns at, which is exactly where
two different machines would land if a sector still costs one. The two emulators
disagree loudly and cannot arbitrate, because neither models the latency.
What is left is either that the multi-sector `int 13h` is not being issued on
that hardware, or that it is and the drive/media does not reward it; **the
`FLOPPY1=1` A/B disk is what separates those**, and that knob exists for
exactly this (SPEC.md §18.91).

A third real machine did read **4.5x faster**, and it is the discarded
Packard Bell 286 — a different CPU, a different controller and an unknown
drive/media pairing, so it says only that the wall is not universal.

#### What the set was asked, and what it answered

| question | answer |
|---|---|
| `shl clk/bit x100`, the variable-shift model | **400** — the 8086 book's 4.00 clocks a bit, exactly. §5.7's mask and bank tables are justified |
| `mov al,[bx+disp16]`, what a table lookup costs | **24.09 clocks** against `shl r16,cl (13)`'s **60.37**. The trade is 2.5x, not a wash |
| `mov ax,i + mul [m]` | **154.98** against `mul r16`'s 132.54 — the memory form costs 17%, so the `mul` left in `gfx_rowbase` is not the thing to remove |
| `GFX_FILL 256x1` and `fill ns per row` | **void — harness bug**, below |
| `FULLSCREEN in+out` | **6.17 s** on Hercules, **3.50 s** on CGA. Part 1's "visible redraw", priced |
| `GFX_FILL 64x64 clipped` | 8,750 µs against 8,221 unclipped: **+528 µs** to draw under an armed region, against a 328 µs `SET+CLEAR`. The region arithmetic is cheaper than re-entering the primitive |
| the whole **fullscreen block** | **boring, as hoped**: `GFX_PIXEL` 640.87 vs 641.32 (0.07%), `GFX_FILL 64x64` 8,221.20 vs 8,225.77 (0.06%), `FONT_CHAR` 890.02 vs 887.92 (0.24%), `FONT_RUN` 8,996 vs 9,008 (0.13%). A primitive costs what it costs wherever it is drawn |
| `boot ticks` / `boot ms` | **708 / 38,886** (Hercules), 700 / 38,447 (CGA) |
| the **hard disk** | it works, and it is **50,904 B/s** — **25x the floppy**. `HDD FILE_DFREE` 402 ms across a 41-sector FAT through a 9-sector window; mount and back 1.79 s |

#### §5.6.8 is settled, and the instruction count was right

`LSTEP8/LSTEPV8` reads **116** on the 5150 against **118** in guest
instructions under `-icount`. The decomposition is what makes it useful:

| | |
|---|---|
| a walk step's **arrival** | **128.7 µs** |
| a walk step's **pixel** | **655.0 µs** |

The pixel dominates the arrival **5:1**, so batching cannot be where the cost
is, and §5.6.8's field-inferred **356** is refuted — by the machine those
field figures came off. The open gap the row was written to close closes in
favour of the cheap measurement, which is the outcome that was least
expected and the reason the row exists.

**One caution on reading the second number, because three quantities are in
play and only two have names.** `gb_b_lstepv8` steps eight *separate blocks*
of one pixel each, so its per-pixel term is a whole one-pixel **block** —
staged in, `gfx_ls_box`, `gfx_ls_addr` through `gfx_rowbase`, the pixel,
staged back out. It is not the marginal pixel inside a multi-pixel step. The
tell is that **655.0 µs lands within 2% of this machine's own `GFX_PIXEL`
(640.87)**: a one-pixel walk costs what a pixel costs, because it is one.
Missile Command's drain, which steps tens of pixels on a single block,
measured the marginal pixel at **160-195 µs** (Sets 9-12). So:

| | |
|---|---|
| arrival — what a batch removes | **128.7 µs** |
| block setup + first pixel — what it does not | **655.0 µs** |
| each further pixel on the same block | **~175 µs** |

The row's conclusion survives that intact and is if anything sharpened: for a
caller stepping N blocks two or three pixels each, the batch saves (N−1) ×
128.7 µs — about 0.9 ms a frame at Missile Command's eight live missiles,
against the ~6 ms §48.16 inferred — while the **per-block setup**, ~3.8 ms a
frame, is the term that actually dominates and that neither candidate named.

`gfx_line`'s pair came out **better** than claimed: `line steep fat/thin` is
**135** against a predicted 156 (CGA 134), with the shallow control at
**309** (CGA 297) where three walks say ~300. The two *thin* rows were meant
to match and are **10% apart** — steep 21,184 µs, shallow 19,245 (CGA 21,403
/ 19,245) — so a y-major line costs a tenth more than the same line
transposed, which is `gfx_rowbase` per step against a pointer add.

#### The one row nobody can explain: `GFX_UNLOCK+LOCK`

| 5150 Herc | 5150 CGA | T1100 Plus | PCem | MartyPC |
|---|---|---|---|---|
| **2,241 µs** | **2,402 µs** | 119 µs | 223 µs | 246 µs |

As a fraction of that machine's own `GFX_PIXEL`, the 5150 is **3.49** and
every other machine is **0.16–0.38** — a 9x outlier on the one machine that
agrees with MartyPC everywhere else. It is also **the only row in either
harness that cannot be measured with interrupts off** (`gfx_lock` ends with
`sti` by contract), and the only variable work inside it is
`cur_lazyend` → `cur_move`, which runs when the mouse has moved since the
cursor was drawn. Not reproduced under QEMU: with the pointer in the middle
of the screen and again parked in a corner, the pair is **0.5 instructions an
iteration** either way — with the mouse *idle*, which is the whole question.

SPEC.md §7.1.4.1, found separately and after these runs, measured that
mechanism firing: under a flood of mouse packets, **279 `cur_move` calls in
972 unlocks**, about 29%. So a moving pointer plausibly explains the whole
gap, and the row would then be measuring unlock+lock **while the mouse
moves** — arguably the number that matters, since a Missile Command player
never stops moving it. Open until somebody says whether they had a hand on
the mouse. docs/FIELD-NOTES.md 8.

#### The harness bug: two fill rows measured a line-step for four commits

`GFX_FILL 256x128` and `GFX_FILL 256x1` are **void in every report in this
set**. `bl_body` is a module word and the fill block set it once and reused
it across three rectangle sizes — correct until the `GFX_LINE` and
`GFX_LSTEP` blocks were inserted between the 64x64 fill and the 256x128 one,
after which both rows timed `gb_b_lstepv8`. It could not be seen from the
report: a fill and a vector walk-step happen to cost about the same, so the
two rows agreed with each other to **0.4%** (6,410 and 6,432 counts against
the lstepv row's 6,406) and the two derived rows they feed printed a tidy
**0**.

`tools/benchlint.py` refuses it now — every `call bl_run` must set
`[bl_body]` since the previous one, so repeating a body is allowed and
*carrying* one is not, which is what an insertion cannot silently break. Run
by `make bench`. Four sites were carrying legitimately and now say so.

Everything else in the set stands: the bug is two raw rows and two derived
ones out of about sixty.

### Set 12 — the three trades, measured; and the one that was reverted

| | |
|---|---|
| machine | MartyPC, per-call floor **1,287 counts** against Set 10's 1,247 — 3.2% dearer, so these gains are if anything understated |
| build | `7f010ec` plus the logger |
| run | 54 usable seconds, mean 15.98 fps |

|  | Set 9 | Set 10 | **Set 12** |
|---|---|---|---|
| median worst frame | 57.9 | 63.0 | **52.4 ms** |
| worst frames over a tick | 54% | 54% | **49%** |
| `exp` median / p90 / max | 19.0 / 39.4 / 76.1 | 16.6 / 39.9 / 73.8 | **15.7 / 32.1 / 47.1** |
| `crs` median | 6.8 | 6.9 | **4.4** |
| fills on a worst frame (mean) | 20.3 | 22.4 | **17.2** |

**The median worst frame went under the tick for the first time in the
investigation** (52.4 against 54.9), and `exp`'s *maximum* — the thing that
actually reads as a stutter — fell 36%.

Attribution, and the reason the band change did not survive:

- **The crosshair is 6.9 → 4.4 ms**, exactly the two-of-four-calls the change
  predicted, and it is paid on every frame the mouse moves. Kept on looks as
  well as on cost.
- **Two drawn states** removes one blob draw in three and shortens a burst
  from 21 frames to 15, so fewer are live at once. Most of `exp`'s fall.
- **The coarser bands were reverted.** Two fills out of nine per blob, about
  **4.3 ms on a worst frame** — real, and roughly the margin that took the
  median under the tick — but the burst lost its round edge for it. The
  measured middle, if it is ever wanted: **R/6 + 1 is also 7 fills** at the
  only radius two states ever draw, with a 3px step instead of 4.

### Set 13 — the floppy A/B, and two open notes closed

Three runs on the IBM 5150 in one sitting, from the `c5f404d` field disks:
`herc.img` (the shipped transfer), `cga.img`, and **`flop1.img`** — the same
kernel with `FLOPPY1=1`, one sector per `int 13h`. The operator left the mouse
alone from before `R` on the first two and moved it continuously through the
third, which is what the new `-- the run --` block was built for and what
makes two of these rows interpretable at all.

**The calibration is the tightest this project has had**, which is what
licenses everything below: against Set 11 on the same machine, `GET_TICKS` and
`GFX_BLIT4 solid` are **identical to the count**, `GFX_FILL 64x64` is 0.2%
apart and `GFX_PIXEL` 2.9%. The two runs being compared for the floppy differ
by **0.13%** on `GFX_PIXEL` and 1.5% on `read 1 sector file`.

#### The per-track batching is SLOWER on the target machine

| | batched (`herc.img`) | one sector a call (`flop1.img`) |
|---|---|---|
| 16 KB read, cold motor | 8.90 s | **7.69 s** |
| 16 KB read, warm | 8.73 s | **7.58 s** |
| a one-sector file | 961 ms | 947 ms |
| throughput | 1,875 B/s | **2,161 B/s** |
| **`boot ticks`** | **715** (39.3 s) | **621** (34.1 s) |

**−13% on the boot and −15% on the file read**, measured two independent
ways in one session. Set 1 predicted the batch at **9x faster**; SPEC.md
§18.91/§18.93 and CLAUDE.md all still carry that number.

The A/B also settles what FIELD-NOTES 7 could not: the multi-sector command
**is** reaching the hardware. If it were being silently decomposed the two
columns would be identical, and they are 94 ticks apart on the boot alone.
Something about a multi-sector `int 13h` on that drive, that controller or
that media costs more than the revolutions it saves, and the emulators cannot
see it because neither models the revolutions in the first place (Set 11).

#### What the floppy SHOULD do, and what DOS gets on the same machine

The A/B says the batching is a loss. The next question is what the *floor*
is, and the arithmetic is not close:

| | bytes/second |
|---|---|
| raw MFM bit rate (250 kbit/s — the Tandon's "32 KB/s") | 31,250 |
| **a whole 9-sector track per revolution (1:1)** | **23,040** |
| ...with a 25 ms step every cylinder | 21,685 |
| **a 2:1 interleave — two revolutions a track** | **11,520** |
| a 3:1 interleave | 7,680 |
| **one sector per revolution** | **2,560** |
| | |
| **DOS 3.3 on this machine, this drive, this media** | **~12,700** |
| os8088, `FLOPPY1=1` | 2,161 |
| os8088, batched | 1,877 |

A 360KB disk turns at 300 RPM, so a revolution is 200 ms and a track holds
4,608 data bytes. Against that, **os8088 catches 0.84 sectors per revolution
unbatched and 0.73 batched** — it is not merely missing the next sector, it
is missing whole turns — while **DOS 3.3 copying 62 KB off the same disk in
about 5 seconds is catching five**, and that figure *includes* a per-file
round trip because of how `COPY` works, so DOS's raw read rate is higher
still.

**That rules out the media and the drive.** Whatever the physical interleave
is, DOS achieves 5 sectors a revolution on it and we achieve 0.84, so the
6x is ours. Set 13's finding that batching makes it *worse* is a second
symptom of the same thing rather than a separate puzzle: if a multi-sector
command were streaming at all, nine sectors in one call could not cost more
than nine calls.

`sysbench` grew a block for exactly this — **raw `int 13h`, called by the
benchmark with no kernel code in the way**, timing one sector, a whole track
in one call, and the same track one call at a time, plus the four bytes of
the diskette parameter table the BIOS is actually using. If `int 13h track,
1 call` comes back near one revolution the hardware streams perfectly and
the fault is entirely in `dsk_xfer`; if it comes back near nine, the BIOS or
the media does not stream and nothing above it can help. Under QEMU it reads
correctly and measures zero, which is the caution block working as intended.

The parameter table is worth a look on its own. QEMU/SeaBIOS reports **EOT
9** (so §18.92's patch is landing), head settle **15 ms**, and **motor start
8** — eighths of a second, so **one full second** before a transfer the BIOS
believes needs the motor started. DOS installs its own table with smaller
values. What that byte reads on the 5150, and whether the BIOS thinks the
motor has stopped between our calls, is now on the report.

#### `GFX_UNLOCK+LOCK` was never the mouse, and is no longer 9x

| | Set 11 (`16844dd`) | now, pointer untouched | now, pointer moved all run |
|---|---|---|---|
| `GFX_UNLOCK+LOCK pair` | **2,241 µs** | **290 µs** | **369 µs** |
| `pointer moved (samples)` | not recorded | **0 of 120** | **64 of 120** |
| `pointer x / y span` | not recorded | **0 / 0** | **706 / 332** |

So the mouse is worth **+27%**, not 9x — and with it demonstrably untouched
the row lands at 290 µs, in line with PCem's 223 and MartyPC's 246. Every
other row moved 0–4.4% between the two builds and `GET_TICKS` did not move at
all, so this is a change in the kernel and not in the machine or the
operator. Two commits between those builds touch that path — SPEC.md
§7.1.4.1 (`cur_lazyend` saving every register) and the COM1/COM2 probe
(§9.5) — and which of them did it is not established. The anomaly is closed;
if it returns, the pointer block now says whether a hand was on the mouse.

#### The per-row fill term, at last, and it lands on the model

The two rows void in Set 11 are repaired and the derived line is the one this
set was sent for:

| | measured | predicted |
|---|---|---|
| `fill ns per row`, Hercules | **176,850** | 177,000 — **0.08% out** |
| `fill ns per row`, CGA | **194,831** | 182,000 — 7% out |
| `fill ns/px 64-box`, Hercules / CGA | 526 / 589 | — |

`GFX_FILL 256x128` is 23,351 µs against `256x1`'s 888, so 127 rows cost
22,463 µs and the per-call floor is what is left. **Use 177,000 ns a scan
line on Hercules**; Part 2's older 177 µs figure was right and is now
measured against its own two-point fit rather than inferred from three sizes.

#### Two things to know before reading any of it

**The hard-disk block reported no volume**, on all three runs. That is
§51.3 working as written and not a fault: a freshly built image carries no
`SYSTEM.CFG`, so no driver is wanted, so the hard disk is never probed. Tick
**Drivers → Hard Disk** in the Control Panel and **close the panel** (§31.8 —
closing is what writes) before a set that wants those rows.

**The reports saved themselves**, which is why there are six files rather
than the three a forgotten `S` would have left.

### Set 14 — the BIOS underneath us, and the floppy question is answered

One `sysbench` run on the IBM 5150, from the `f8e40df` disks. The new block
calls `int 13h` **itself**, with no os8088 code in the path, and three rows
settle six weeks of argument:

| row | measured | revolutions | bytes/second |
|---|---|---|---|
| `int 13h 1 sector` | **199.1 ms** | **1.00** | 2,571 |
| `int 13h track, 1 call` (9 sectors) | **384.5 ms** | **1.92** | **11,985** |
| `int 13h track, 9 calls` | **2,004.8 ms** | **10.02** | 2,298 |
| | | | |
| os8088's own 16 KB read, warm | 8.57 s | 1.34 **per sector** | **1,912** |

A revolution is 200 ms, and every one of those numbers lands on a whole
number of them. Read in order:

- **One sector, repeated, costs exactly one revolution** — which is not a
  fault, it is the definition: the same sector comes round once a turn. It
  also confirms the instrument, because 1.00 is not a number you get by
  accident.
- **A whole track in ONE call costs 1.92 revolutions.** So the media is
  **2:1 interleaved** (4,608 bytes in two turns is 11,520 B/s and we measured
  11,985), and the drive, the controller and the BIOS **stream perfectly
  well** when asked for nine sectors at once.
- **The same nine sectors as nine calls costs 10.02 revolutions**, one per
  sector, because control returns to the caller and the next sector has gone
  past by the time the command is reissued.

**So the ceiling on this machine is 11,985 bytes/second and os8088 achieves
1,912 — a factor of 6.3, and every bit of it ours.** The drive is exonerated,
the controller is exonerated, the media's interleave is exonerated, and so is
the BIOS: `int 13h track, 1 call` *is* our batched read done right, and it is
six times faster than what `dsk_xfer` actually achieves.

The decisive comparison is the last two rows against each other. os8088 costs
**1.34 revolutions per sector** — worse than the 1.00 that nine separate BIOS
calls cost, and nowhere near the 0.21 that one batched call costs. Whatever
`dsk_xfer` is issuing, **it is not reaching the hardware as multi-sector
commands**; SPEC.md §18.91's batching is either not forming the runs it
believes it is, or something between it and the BIOS is decomposing them.
That is now a code question with a number attached, and the next step is to
count what `dsk_xfer` actually issues (the `DISKCNT` knob counts sectors and
would need to count *calls*).

It also explains Set 13 without needing a second mechanism: if the batched
path is issuing per-sector commands anyway, then `FLOPPY1=1` measuring 15%
*faster* is just the batched path's extra arithmetic with none of its
benefit.

#### The parameter table, and the ROM's own values

| | 5150 | QEMU/SeaBIOS |
|---|---|---|
| EOT (§18.92 patches this) | **9** | 9 |
| step rate / head unload | **00CF** | 00AF |
| head settle | **25 ms** | 15 ms |
| motor start | **8** — eighths of a second, so **one full second** | 8 |

The patch is landing. The other three are the IBM ROM's, which is what
§18.92 intends — but `head settle 25 ms` is paid per seek and DOS installs
15, and `motor start` is a **whole second** before any transfer the BIOS
believes needs the motor started. Neither can explain a 6.3x on a read that
never seeks, so they are not the bug; they are worth a look afterwards.

#### Two harness corrections this set forced

**`bios track 1 call B/s` and `bios track 9 calls B/s` read 4x low** in this
set — 2,996 and 574 against the true 11,985 and 2,298. `bl_last` is the total
count for the whole row and the derived rows divided it by *one iteration's*
bytes. Both are recomputable exactly from the `us/op` column, which is
per-iteration and correct: **4,608 / (us/op)**. Fixed.

**And the block hard-froze the machine once**, on the first run after a cold
boot, then ran normally after a reboot. docs/FIELD-NOTES.md 10: a package
cannot make an `int 13h` safe, because the BIOS runs its disk handler and its
IRQ6 nesting on whichever 256-byte task stack is current and the kernel's own
`dsk_xfer` holds `sch_lock` across every call so nothing switches underneath
one. It is kept because it answered the question and the answer was worth
6.3x; nothing shipped may copy it.

### Set 15 — 148 sectors to read 32, and the splitter was never the problem

One `sysbench` run on the 5150 from the `d5a1fc9` `dskdbg.img`, plus a DOS
cross-check on the same machine. The instrumented kernel (SPEC.md §18.94)
answers the question Set 14 left:

| one 16 KB `OSAPI_FILE_READ` | |
|---|---|
| sectors moved | **148** |
| int 13h calls | **34** |
| longest run | **9** |
| controller resets | **0** |
| sectors per call | **4.35** |

**The run splitter works.** 4.35 sectors a call with a longest run of 9 is
not one-per-call, nothing is being retried, and at 252 ms a call every one of
those transfers costs about what the raw `int 13h` rows in the same report
say the BIOS charges (199 ms for one sector, 398 ms for nine). §18.91 is
doing its job.

**We are moving 4.6x the data the file contains.** A 16 KB file is 32
sectors; the read issued 148. That single number is the whole gap:

| | |
|---|---|
| 32 sectors batched 9 to a call | ~4 calls, **1.59 s**, 10,291 B/s |
| measured | 34 calls, **8.57 s**, 1,912 B/s |
| | **5.4x** |

So the floppy problem was never the FDC, the interleave, the batching or the
BIOS — every one of those was measured and cleared — and it is not even the
*call* efficiency. **It is 116 sectors of traffic nobody asked for.**

#### The same binary, the same image, on QEMU

| | 5150 | QEMU |
|---|---|---|
| 16 KB read: sectors / calls | **148 / 34** | **34 / 6** |
| 1-sector read: sectors / calls | — | 3 / 3 |
| `disk_mount` calls in either | — | **0** |

QEMU is very nearly optimal: 32 data sectors plus 2 of directory. So the
extra 116 are **machine-dependent**, which is why no amount of reading the
source found them. The block now measures each operation on its own and
counts `disk_mount` calls inside it, so the next run says whether the
overhead is a remount, the directory walk, or something in the chain walker
that only fires on real geometry.

#### DOS confirms the ceiling, on one file this time

Set 13's DOS figure copied several small files, so it carried a directory
write, a FAT write and a fresh seek each time. Repeated with the single
170 KB contiguous file now on the field disks: **~15 seconds, about 2 of them
with the disk not spinning, and it stopped three times** (a 64 KB buffer,
copied out to the ST-225 between passes).

| | bytes/second |
|---|---|
| DOS, one 170 KB file, spinning time only | **~13,390** |
| `int 13h track, 1 call` from this report | **11,570** |
| 2:1 interleave, by arithmetic | 11,520 |
| os8088 | 1,912 |

Three independent numbers within 16% of each other, and the media is 2:1
interleaved. **11.5–13.4 KB/s is what this drive does**; that is the target.

#### The raw rows repeat, which is what makes them a measurement

Set 14 against Set 15, same machine, different boots:

| | Set 14 | Set 15 | revolutions |
|---|---|---|---|
| `int 13h 1 sector` | 199.106 ms | 199.106 ms | **1.00** |
| `int 13h track, 1 call` | 384.480 ms | 398.211 ms | **1.99** |
| `int 13h track, 9 calls` | 2,004.789 ms | 1,991.057 ms | **9.99** |

Tick quantisation is 54.9 ms and the rows land within one tick of each other
across boots. The two derived `bios ... B/s` rows now read correctly —
**11,570** and **2,314** — after Set 14's 4x correction.

The parameter table repeats exactly too: EOT **9**, step/head unload
**00CF**, head settle **25 ms**, motor start **8** (one second). Stable, the
ROM's, and not the bug.

### Set 16 — the BIOS moves nine sectors and says it moved one

The trace settled it in one run. `sysbench` on the 5150, one 16 KB
`OSAPI_FILE_READ`, every `int 13h` `dsk_xfer` issued, as `lba+run`:

```
    5  1      5  1    254  7    255  6    256  5    257  4    258  3
  259  2    260  1    261  9    262  8    263  7    264  6    265  5
  266  4    267  3    268  2    269  1    270  9    271  8    272  7
  273  6    274  5    275  4    276  3    277  2    278  1    279  7
  280  6    281  5    282  4    283  3    284  2    285  1
```

Two directory reads, then **the LBA advances by one while the run counts
down** — 7,6,5,4,3,2,1 then 9,8,…,1 then 9,8,…,1 then 7,…,1. Thirty-four
calls, a run sum of 148, and **thirty-three distinct LBAs**: nothing is read
twice and nothing is skipped. That shape has exactly one cause.

**`dsk_xfer` asked for nine sectors, the BIOS moved nine, and answered
`AL = 1`.** The code believed it (§18.91's short-count handling), advanced one
sector and re-asked for the other eight — then seven, then six. The data
stayed correct, because the sectors it re-read were sectors it had already
read correctly; `CF` stayed 0 so nothing retried; and the only symptom was
that **every sector cost its own revolution**.

That one fact retires the whole investigation:

| symptom | why |
|---|---|
| 148 sectors requested for a 32-sector file (Set 15) | 32 calls at descending run lengths sum to 148 |
| 1.34 revolutions a sector against the BIOS's 0.21 (Set 14) | 32 calls, each catching one sector |
| **batching measured 15% SLOWER than `FLOPPY1=1` (Set 13)** | **the same call count plus the run arithmetic, for nothing** |
| DOS 6x faster on the same drive and media (Set 13) | DOS trusts `CF` |
| QEMU perfect on the same binary and image (Set 15) | SeaBIOS returns the full count |

Set 13's inversion is the one worth dwelling on. "The batching is slower than
no batching" looked like a fact about floppy hardware and was in fact the
signature of batching that never happened — the run splitter did all its work
and then threw the result away, one sector at a time.

#### The fix is to read the contract

**`CF = 0` is the BIOS saying the whole request completed. `AL` is not.**
`dsk_xfer` now advances by `[dsk_run]`, which is what DOS does and what the
int 13h contract says. `make DISKAL=1` restores the old behaviour for an A/B
on iron.

The old reasoning was sound and is worth keeping in view: a BIOS *may*
terminate a multi-sector read early and report it, and advancing by the
request would then step past a hole and produce a file with a gap and a
shifted tail — intact first sector, so the load succeeds and the package dies
when its code is reached. That case has never been observed here, and
docs/FIELD-NOTES.md 5 already recorded that the short-count fix "changed
nothing" when it landed. It changed something now.

Because that risk is real if the reading is wrong, **`sysbench` verifies the
file it reads**. `BENCH.DAT` is filled with `(i >> 9) & 0xFF`, so every byte
of sector *n* is *n*: a skipped sector shows as a gap (sector *n* holding
*m > n*), a re-read as a repeat, and the row names the first sector that
disagrees and what it found there. A benchmark that got 6x faster and quietly
wrong is the worst available outcome, so the disk says.

#### What to expect

32 sectors in ~4 calls at ~400 ms each: **~1.6 s against 8.4 s**, so roughly
**5x on every load in the system** — the number Set 1 predicted for a
different reason and Set 13 measured as a loss. Under QEMU the change is a
no-op, which is the point: 34 sectors in 6 calls before and after, data check
OK.

### Set 17 — the fix measured: 4.1x on the floppy

One `sysbench` run on the 5150, `herc.img` from the `f9f226c` field build —
the ordinary kernel, not the instrumented one.

| | before (Set 15) | after |
|---|---|---|
| 16 KB read, cold motor | 8.29 s | **2.09 s** |
| 16 KB read, warm | 8.35 s | **2.20 s** |
| **throughput** | 1,912 B/s | **7,457 B/s** |
| | | **3.9x** |

Set 16's reading of the contract was right: `CF = 0` is the BIOS saying the
whole request completed, `AL` is not, and believing `AL` was costing a
revolution a sector. Everything else in the report is unchanged to within a
tick, so this is the transfer and nothing else — `read 1 sector file` is
810 ms both times, because a one-sector read never had a run to lose.

**It is not yet at the ceiling.** 7,457 against the BIOS's own 11,570 for a
whole track in one call is 1.55x still on the table, and the arithmetic says
where: 32 sectors in ~4 calls of ~400 ms is 1.6 s, and we measure 2.09 —
about two extra calls' worth. `dsk_read_chain`'s run coalescing tops out at
the track and the DMA page, and the file's first cluster is rarely
track-aligned, so a 32-sector read is 4 calls only when it starts on a track
boundary and 5 when it does not. That is the next thing to look at, and it is
worth about a second on every large load.

#### The check that licensed it did not run

`sb_verify` — the row that reads `BENCH.DAT` back and confirms every byte of
sector *n* is *n* — was written inside the `DISKCNT=1` instrument block. The
field run booted the **plain** kernel, so the block printed "this kernel
carries no disk instrument" and skipped, and the 4x was taken with the one
check that made it safe switched off. It is called from the floppy block now,
on every kernel, and reads `data check, 32 sectors  OK`.

**A guard that only runs on the build you are not shipping is not a guard.**
The same shape as PERFORMANCE.md Part 6's rule about harness bugs, and it
cost a round trip to a machine that is not in this room.

### Set 18 — the boot is 4x, and a second real machine

Two `sysbench` runs from the `b71f6ca` field build: the 5150 on `herc.img`,
and the **Compaq Portable III** — a new machine, and the first AT-class one
in the register whose numbers are worth keeping.

#### The boot sector's half of Set 16

| | before | after |
|---|---|---|
| **`boot ticks`** | **726** (39.88 s) | **181** (9.94 s) |
| 16 KB read | 8.29 s | 2.09 s |
| | | **4.0x on both** |

`read_run`'s `.done` believed `int 13h`'s `AL` exactly as `dsk_xfer` did, and
the boot is 140 sectors — at a revolution each that is **33 of the 40
seconds**. Set 17 fixed one loop and reported the other unchanged; this is the
other. **A cold boot of this OS on a 4.77 MHz 8088 is now ten seconds.**

And `data check, 32 sectors  OK` on **both** machines — the guard that
licenses trusting `CF` finally ran where it matters. Set 17 took its 4x with
that check switched off by a scoping mistake; it is unconditional now and it
passes on real hardware, on two different BIOSes and two different drives.

#### Compaq Portable III — 286, CGA, 1.2 MB drive

| | |
|---|---|
| `boot ticks` | **154** (8.46 s) |
| 16 KB read, warm | **1.48 s — 11,047 B/s** |
| 16 KB read, cold motor | 3.63 s |
| `read 1 sector file` | 673 ms |
| adapter | CGA 640x200 (the plasma panel) |

**11,047 B/s is 1.48x the 5150's 7,457**, and the mechanism is the drive
rather than the CPU: a 1.2 MB drive spins at **360 RPM**, so a revolution is
166.7 ms against the 5150's 200, and the 32-sector read costs 46.3 ms a
sector — **0.28 revolutions**. That is batching working properly, on media
this drive is only nominally compatible with.

The cold/warm gap is 2.4x here against the 5150's 1.05x, which is the AT
BIOS's media-type detection: a 360 KB disk in a 1.2 MB drive has to be
identified by trying data rates, and that cost is paid once.

Its mouse rows read `mouse found 0`, and that is **not** a fault: there was
no mouse plugged into it. The owner has one serial mouse and it was on the
5150. It looks like §9.5.2's cross-wired IRQ and is the same category of
misreading as taking `No volume at index 2` for a missing hard disk — ask
what was connected.

#### The disk error that would not reproduce

The previous build refused to boot this machine — `os8088: disk error`, the
boot sector's own message. `make BOOTDIAG=1` was built to name the int 13h
status in one boot instead of a three-disk bisect, and the answer was **no
error at all**: the same code on a freshly written disk booted and read
correctly, and the data check passed.

**No code change explains it**, and it is worth saying so rather than
claiming the fix. The boot sector's `.done` change alters *how many* calls
are issued, not whether they succeed, and on a BIOS that reports `AL`
honestly both readings advance correctly. What is left is the disk: **360 KB
media in a 1.2 MB drive is marginal by construction** — 48 tpi tracks under a
96 tpi head — and a rewrite is the ordinary fix. The knob stays, because the
next one may not be marginal media and one boot is cheaper than three.

### The directory sector was read twice (SPEC.md §18.4.3)

Set 18 left 1.55x on the table — 7,457 B/s against the BIOS's own 11,570 —
and §18.94's trace says where a third of it went:

```
    5  1      5  1    254  7    255  6 ...
```

**LBA 5 twice.** `dskw_find` walked the directory and recorded where the
entry was; `dskw_ent_load` then re-read the same sector to copy the 32 bytes
out. Seven call sites, all `call dskw_find` / `jc` / `call dskw_ent_load`.

A re-read of a sector that has just passed the head costs a **whole
revolution** — 199 ms — which is 9.5% of a 16 KB read and **a third of the
cost of opening a small file**, where the whole operation is three sectors.

`dskw_find` copies the entry while the buffer still holds it. Measured under
QEMU, which counts calls exactly even though it cannot time them:

| | before | after |
|---|---|---|
| 16 KB `FILE_READ` | 34 sectors / **6 calls** | 33 / **5** |
| one-sector `FILE_READ` | 3 / **3** | 2 / **2** |

Predicted on the 5150 at 199 ms a call: the 16 KB read **2.09 s → ~1.89 s**
(7,457 → ~8,670 B/s) and a small-file open **810 ms → ~540**, which is the
larger win in practice — every `SYSTEM.CFG` read, every package header check
and every double-click pays it.

What is left after this is structural rather than a bug: the first data run
is short whenever a file's first cluster is not track-aligned (5 sectors of a
9-sector track in the trace above), and a run coalesces only to the track and
the DMA page. Worth knowing before anyone goes looking for another factor of
two — there is not one there.

### Set 19 — the Compaq's disk, and why a BETTER interleave leaves MORE on the table

Two machines, same commit, same 360KB media, `DISKCNT=1` in both (SPEC.md
§18.94.1, so this is an ordinary field disk rather than a special one). The
operator's question was whether the Compaq Portable III's disk section
"paused a moment, and took longer overall than the 5150". **Half right, and
the half that is wrong is the interesting half.**

|                          | IBM 5150 | Compaq Portable III |
|---|---|---|
| drive / revolution       | 360K, 300 RPM, 200 ms | 1.2M, 360 RPM, **167 ms** |
| `int 13h` 1 sector       | 199.1 ms = **0.996 rev** | 164.8 ms = **0.989 rev** |
| `int 13h` track, 1 call  | 384.5 ms = 1.922 rev | 206.0 ms = **1.236 rev** |
| ...as bytes/second       | 11,984 | **22,368** |
| interleave that implies  | 2:1 | ~1:1 |
| 16K read, cold motor     | 1.867 s | **3.570 s** |
| 16K read, warm           | 2.032 s | **1.483 s** |
| 1-sector file open       | 0.604 s | **0.508 s** |
| `floppy bytes/sec`       | 8,062 | **11,047** |
| gap to its own ceiling   | 1.49x | **2.03x** |

**The pause is real and it is the cold-motor row**: 3.570 s against the
5150's 1.867 s, a single `N = 1` measurement that pays spin-up and the first
seek on a drive that was not turning. Everything *after* it is faster on the
Compaq — 16K warm 1.483 s against 2.032, a file open 0.508 against 0.604, and
11,047 bytes/second against 8,062. Summed, the whole disk section is **15.2 s
on the Compaq against 17.5 s on the 5150**, so "took longer overall" is the
impression rather than the measurement, and the cold-motor row is what
produced it.

**`int 13h 1 sector` = one revolution on both machines is the check that
licenses the whole table.** It comes out 0.996 rev at 300 RPM and 0.989 rev
at 360 RPM; read the Compaq against the 300 RPM scale instead and it becomes
0.82 of a revolution, which no drive can do. That is how the drive was
identified as a 360 RPM one from the report alone — and it is why
`sysbench`'s raw-int-13h header, which hard-coded "300 RPM = 200 ms a turn",
now prints both scales and says the 1-sector row is what tells you which.

**Now the finding.** The Compaq's BIOS streams a track at 22,368 B/s where
the 5150's manages 11,984 — 1.87x, because its media is close to 1:1
interleaved where the 5150's is 2:1. os8088 gets 11,047 against 8,062, only
1.37x. So **the faster disk is the one os8088 wastes more of**: 2.03x off its
ceiling against 1.49x.

The mechanism is per-call rotational latency, and both machines pay about the
same number of *milliseconds* for it. A 33-sector read is 5 `int 13h` calls
(1, 6, 9, 9, 8 — identical on both machines, as the trace confirms), and
against each machine's own streaming rate:

|                                | 5150 | Compaq |
|---|---|---|
| 33 sectors if streamed         | 7.04 rev | 4.53 rev |
| measured                       | 10.16 rev | 8.90 rev |
| excess, per call               | 0.62 rev = **124 ms** | 0.87 rev = **145 ms** |

124 ms and 145 ms — a little over half a revolution each, which is exactly
what "the sector went past while os8088 walked the FAT, so wait for it to
come round" costs. It is a **fixed cost in time**, so a drive that streams
1.87x faster loses 1.87x more of its ceiling to it. Nothing about the Compaq
is slow; its ceiling is simply further away.

**What this does and does not license.** It does *not* say there is another
factor of two available: Set 18's coda still stands, in that a run coalesces
only to the track and the DMA page and a file's first cluster is rarely
track-aligned. What it says is that the remaining gap is **per-call latency
rather than per-sector transfer**, and therefore that the lever is the call
*count* — 5 calls for a 32-sector file — and not anything inside a call. It
also warns against a tempting mistake: measuring this on the 5150 alone makes
the gap look like 1.49x and nearly closed, and the same code on a
better-interleaved disk is 2.03x. **A single calibration machine can flatter
a latency bug**, because how much a fixed delay costs depends on how fast the
thing you are delaying would otherwise have gone.

### Set 20 — Tracker's mixer was 45% of the machine, and it is BUS-bound

**How it was found, and the instrument is worth as much as the answer.**
MartyPC's `status` returns `flat_ip`, so sampling it from the host and
bucketing by the nearest preceding label out of the NASM listing **is a
profiler that costs the guest nothing** — no counters, no instrumentation, no
guest cycles, and a sample rate uncorrelated with anything the guest does
because it is driven from outside. `tools/`-side, ~150 samples a second, and
it needs the listing parsed properly: a label line in a NASM listing carries
no address (the address turns up on the next line that emits a byte), and
getting that wrong buckets everything into the nearest *data* label — the
first run of it put 41% of the guest in `mp_ntab`.

Windowed, `BEVERLY.MOD`, XT mode (5,500 Hz, 4 channels), on a cycle-accurate
4.77MHz 8088:

| | % of the whole machine |
|---|---|
| `mp_mixch_xt.addl` — the add-pass inner loop | **31.7%** |
| `mp_mixch_xt.stl` — the store-pass inner loop | **13.5%** |
| the rest of `mp_mixch_xt` (setup, run split, edges) | 6.3% |
| `mp_chupd` | 3.7% |
| everything outside the package (kernel, BIOS, idle) | 35% |

**45% of the machine is in two seven-instruction loops**, which agrees with
the independent measurement that the worker is `[trk_mixing]` for **65% of
wall time** and the windowed display gets **5.8 frames a second against 7.14
rows** (SPEC.md §45.16.5).

**Why it costs what it costs, and this is the general lesson.** The 8088 has
an **8-bit** bus: every byte fetched *or* accessed is 4 clocks, and they
serialise. So the cost of a loop is its BYTE TRAFFIC, instruction bytes and
data bytes together — not its instruction count and not its "complexity".

| | instr | data | |
|---|---|---|---|
| `mov al,[es:si]` | 3 | 1 | the sample; the `26` override is a byte |
| `xlat` | 1 | 1 | the volume table — already no multiply |
| `add [di],al` | 2 | 2 | accumulate: a read AND a write |
| `inc di` | 1 | 0 | |
| `add dx,bp` | 2 | 0 | the fractional accumulator |
| `adc si,[mp_cstepi]` | 4 | 2 | **a loop-INVARIANT memory read** |
| `loop` | 2 | 0 | |
| | **15** | **6** | = 21 bytes = **84 clocks** |

84 × 4 channels × 5,500 Hz = 1.85M clocks/s = **38.7%** of 4.77MHz, against
45.2% profiled with the setup and edges in it. The model is the machine.

**What is actually removable is 3.5 of those 21 bytes.** `adc si, imm16`
patched in place instead of a loop-invariant memory read is −2; unrolling 4x
amortises `loop` to −1.5. That is 84 → 70 clocks, **38.7% → 32.3%** — about
**6 points of the whole machine**, given back to everything, forever. That
was the PREDICTION; the next paragraph is what the machine actually did.

**Measured after: 45.2% → 29.6%, which is 16 points and not 6.** Same
machine, same module, same windowed XT mode, 60-second samples:

| | before | after |
|---|---|---|
| the add-pass inner loop | `.addl` **31.7%** | `.addq1` **20.2%** + `.addl` 0.5% |
| the store-pass inner loop | `.stl` **13.5%** | `.stq` **8.9%** + `.stl` ~0 |
| the rest of `mp_mixch_xt` | 6.3% | 4.7% + `mp_stepi_set` 0.9% |
| outside the package (kernel, BIOS, **idle**) | 42% | **49%** |

The last row is the one that says it is real: nothing else changed, so the
machine got its time back as idle.

**The byte model under-predicted by 10 points, and the reason is worth
keeping.** It priced only bus traffic, which is the right first-order model
on an 8-bit bus and is not the whole cost: `adc si, [disp16]` also pays an
effective-address calculation and the 8086's memory-operand penalty, and
`loop` is 17 clocks *taken* where the model charged it 2 bytes = 8. Both of
those are execution clocks the fetch model cannot see. **A byte-traffic model
is a floor, not an estimate** — when it says an optimisation is worth 6
points, the machine may hand back more, and it will not hand back less.

**Two bugs on the way, both of which measured as successes.** The first is
CLAUDE.md's own warning arriving in person: a stale `cmp byte [mp_first], 0 /
je .addl` left from the pre-unrolled structure sent the **add pass** — 70% of
the work — straight past the unrolled loop to the one-sample-at-a-time
remainder. It assembled, it ran, the output was correct, an amplitude-envelope
comparison against the old build matched on 90.2% of 876 windows, and the
build was 241 bytes bigger for nothing. **Only the profile could see it**, as
`.addl` 25.2% with `.addq1` at zero — the optimisation had kept its shape and
lost its substance. The second: `mp_stepi_set` as a table walk over
`mp_steppatch` profiled at **3.1% of the whole machine** on its own, because
it runs once per channel per chunk. Straight-line `mov [imm16], ax` — the
accumulator-direct form, 3 instruction bytes and a 2-byte write, and no
register but AX, so the three pushes go too — is 200 clocks against ~480, and
it profiles at 0.9%.

**Verified on the running binary rather than by reading it**, because the only
new failure mode an unrolled run split has is dropping or duplicating a
sample. Breakpoints at `mp_mixch_xt` and its `.fin`, walked in alternation:
**58 of 58 calls advanced DI by exactly `[mp_chunk]`**, 15 of them the store
pass and 43 the add pass. And the self-modifying half: **20 of 20 mixes had
all ten patched immediates equal to `[mp_cstepi]`**.

**And that is the honest ceiling: it does not fix windowed Beverly.** 5.8
frames a second becomes roughly 7, against 7.14 rows — better everywhere and
still not enough for a display that has to show every row. The only large
lever left is the SAMPLE RATE, because the work is `channels × rate` and
nothing else: 5,500 → 4,000 Hz is 27% fewer channel-samples. That is a trade
against audio quality, and it is a decision rather than an optimisation.

**That decision has been taken, and the answer is NO.** 4,000 Hz was built
onto the bench build's `K` key and listened to on the field owner's hardware:
*"4,000 Hz sounds terrible."* The option is **closed** — not deferred — so
nothing downstream should re-propose it, and the arithmetic above is kept
only to say what was on offer and what it would have cost. 5,500 Hz is XT
mode's rate and stays. The windowed display's answer is not a cheaper mixer
at all: it is **drawing less**, which is §45.16.6 — the readout is the
position alone, which the machine can always keep up with.

**Three things NOT worth doing, costed and rejected.** Moving the sample to
`DS` to drop the `26` override needs `es: xlat` instead, which is the same
byte back. Transposing the mix to per-sample-across-channels puts the
accumulator in a register and saves the read-modify-write — but four
channels' worth of position, step and table pointer cannot live in an 8086's
registers, so it buys one byte and spends four reloading state. And filling
the buffer with 0x80 to make every channel an add pass costs a whole extra
`rep stosb` pass to save the store pass's two bytes.

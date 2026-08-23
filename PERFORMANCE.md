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

**It was cycle-accurate and NOT disk-accurate, and that half has now been
fixed — read Set 35 before quoting the old ratio.** MartyPC models instruction
timing, the prefetch queue and bus contention; upstream it modelled **no
platter, no seek and no interleave at all**, which is where these came from:

| | real 5150 | MartyPC, upstream |
|---|---|---|
| read 16 KB, cold motor | **8.07 s** | **0.27 s** — 30x fast |
| boot | **38,886 ms** | **2,306 ms** — 17x fast |

`tools/martypc/patches/04-floppy-disk-timing.patch` gives the drive a platter,
a data rate, a seek and a configurable interleave (Set 35, docs/MARTYPC-DEBUG.md),
and Set 37 then found the one number that had been *assumed* — that the field
machine's 360KB media was 2:1, which it never was — and the doubled head
settle sitting behind it. On the IBM ROM the boot is **188 ticks against the
field's 205**, and `tests/sysbench`'s whole raw floppy block reproduces the
field machine's own report to the measurement quantum, seven of thirteen
rows exactly. The error went **4.4x fast → 1.17x slow → 0.92x**, and every change
of sign on that road was worth as much as the size. So the rule is now:

> **A disk TIMING figure from MartyPC is worth having, and is still checked on
> the 5150 before it lands here.** Part 9's disk rows come off the 5150.
>
> **A disk CORRECTNESS question moved half way**: what the *ROM* does is
> MartyPC's, because MartyPC runs the ROM — §18.91's `AL` bug reproduces
> there (Set 35). What the *chip and the drive* do is still the emulator
> author's belief, and still the 5150's question.

**The correctness half is the one that changed, and this document said the
opposite for two sets.** §18.91's `AL` bug is the case: the BIOS moved nine
sectors and answered `AL = 1`, the kernel believed `AL`, and re-read the rest
one sector at a time — 148 sectors in 34 `int 13h` calls for a 32-sector file
on the 5150, while **the same binary on the same image under QEMU moved 34
sectors in 6 calls**. Correct, fast, and silent. QEMU missed it because
SeaBIOS is a different BIOS, not because emulation cannot see it: `make
DISKAL=1` on `os8088_5150_herc` boots in **893 ticks against 188** and shows
**870 sectors in 183 reads against 183 in 24** — 4.75x the traffic, against
the field's measured 4.6x. What no emulator here invents is what a real 765
puts in ST1, or whether a real drive ever returns short.

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
| Floppy throughput, os8088's own `FILE_READ` | **21,307 bytes/second** warm (**Set 24**; Set 22 says 19,883 on the same machine, and it was 7,457 at Set 17 and 2,100 before it) |
| …cold motor | **12,969 B/s** (16 KB in 1.263 s — Set 24) |
| The BIOS's own best: a whole track in one `int 13h` | **11,570 B/s** (Set 24; 11,984 at Set 14) — **os8088 is now 1.84x THIS**, see below |
| Floppy, raw `int 13h`, ONE sector | **199 ms** — one 300 RPM revolution exactly (Set 22) |
| Floppy, raw `int 13h`, a 9-sector TRACK | **384 ms** — 1.92 revolutions (Sets 14/22). A call costs 1–2 revolutions near enough whatever it moves, so **calls are the unit to estimate a disk change in** |
| Floppy, per 512 bytes DELIVERED by `FILE_READ` | **24 ms** (Set 24) — and read the note below before using it |
| **Floppy WRITE throughput** | **7,020 bytes/second** — 32 KB created in 4.67 s (Set 24). **A write is 3.0x dearer than a read** and the tree carried one "floppy throughput" row for both until Set 24 split them; price a save with THIS |
| Floppy, per 512 bytes WRITTEN | **73 ms** (Set 24) |
| Floppy, the same bytes appended in 8 KB chunks | **2.36x** the one-call create (Set 24) — each chunk is a whole §18.4 commit |
| Floppy, open and read a one-sector file | 796–810 ms — **Set 17, and NOT re-measured since §18.95's cache landed.** Treat as an upper bound |
| System tick | 18.2065 Hz; **65,536 PIT counts, measured exactly** |
| Serial mouse | 1200 baud |

**The framebuffer read-modify-write was quoted at "~30 cycles" here and in
§39.5 for years. It is 79.6, and only about 7 of those are the bus** — the
rest is five 8088 instructions. The figure was low, and it was attributed to
the wrong thing.

**AND THE FLOPPY ROWS WENT STALE A SECOND TIME, IN THIS TABLE, UNDER THE
PARAGRAPH THAT WARNS ABOUT IT.** They read `7,457 B/s / 65 ms a sector` — Set
17's — for the whole of Sets 22 and 24, which took the same read to 19,883 and
then 21,307 B/s. Nothing was wrong with any measurement; the summary table
simply was not edited when Part 9 moved, and every consumer of it inherited a
figure **2.9x too pessimistic**. It reached docs/FIELD-MACHINES.md's machine
register, `sysbench`'s own report text, `trklog.inc`, and a whole plan document
(docs/NET-PLAN.md) that concluded a parallel cable would be *faster* than the
floppy partly on the strength of it.

**So every row in the table above now names the Set it came from**, and that
is the rule rather than a tidy-up: a figure with no provenance cannot be
checked for staleness by reading, only by re-deriving it, and nobody
re-derives a number that looks settled. **When Part 9 gains a set that moves a
row, the row moves in the same commit.** Part 6 rule 8 says every figure
carries its machine; this is the same rule pointed at time instead of hardware.

The two big consequences of the correction, because they invert conclusions
this document had drawn:

- **os8088 is no longer slower than the BIOS — it is 1.84x faster.** Set 17's
  standing residual, *"1.55x still on the table against the ROM's 11,570"*,
  is spent and reversed: a `dsk_xfer` run spans the track boundary with the
  multi-track bit set, so it gets more than one track's sectors per
  revolution where the ROM's single call stops at EOT, and §18.95's cache
  answers a repeat with no revolution at all. Any text still framing the
  floppy as *approaching* the BIOS figure is pre-Set-22.
- **The sector has stopped being a meaningful unit**, which is the older
  lesson taken one step further. §18.95's cache means some of a file's
  sectors are not read at all — Set 22 measured a 16 KB read as **18 sectors
  in 2 `int 13h`** where the file is 32 sectors — so "per sector" now
  measures a mixture of a transfer and a cache hit. The 24 ms row above is
  bytes DELIVERED, not sectors moved, and the honest units are the two raw
  `int 13h` rows and the throughput.

**The first time these rows went stale it was the bug rather than the
measurement.** `238 ms per sector` and
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
| 512 bytes **delivered by a warm `FILE_READ`** | **24 ms** (5150) | measured, **Set 24**. NOT a sector transferred — §18.95's cache means some are never read |
| a sector **inside a coalesced run**, pre-cache | 65 ms (5150), 46 ms (Compaq III) | measured, Sets 17/18 — **superseded by the row above for any estimate made today**, and kept because pre-Set-22 reasoning all over this tree is derived from it |
| an **`int 13h` call** in such a run | **199 ms** for one sector, **384 ms** for a 9-sector track — 1 to 1.92 revolutions, near enough whatever it moves | measured, Sets 14/22 |
| an **isolated single-sector access** — a boot sector at LBA 0, a lone directory sector | **~150–200 ms once the motor is up**, and most of a second if it is not | **MODELLED, NOT MEASURED** (docs/ASSOC-PLAN.md): ~100 ms average rotational latency + ~80 ms average seek across 40 tracks at a 6 ms step + ~15 ms settle |

**A fourth quantity has since split off the first**, which is why the table
grew a row rather than having one edited: §18.95's cache made *bytes
delivered* and *sectors transferred* different things, and the throughput row
in the summary table measures the first. A change that saves REVOLUTIONS is
costed with the `int 13h` rows; a change that saves BYTES is costed with the
24 ms row; and a change that saves cache MISSES is costed with neither until
somebody measures it.

The isolated-access row is the one that keeps getting confused with the
others, and it is the one that matters for anything re-reading track 0: **an
isolated first-sector read is not a streamed 24 ms, because it is a seek**. A
read of LBA 0 from wherever the heads were is up to a full 39-track stroke, so
the ~80 ms in that model is an *average* and the worst case is dearer.

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
| Scroll the Browser one line, deep in a page | the tier test read the old scroll POSITION rather than the delta, so past one windowful every scroll repainted the whole band, on every page, for the rest of the document. Measured on a cycle-accurate 5150/CGA in a 15-row band, one Down key: **15 `font_run`s and no `gfx_scroll` at all**, **19 frames of visible redraw = 317 ms** | one `gfx_scroll` and the row it exposed, at every depth: **1 `font_run` and 1 `gfx_scroll`**, **5 frames = 83 ms**. Framebuffer **0 differing pixels** against the band repaint on CGA (15 rows) and Hercules (27), 55 scroll steps each | §71.10 |
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
| Press an arrow on the Drivers page | §13.8.3's pressed look reached this page as `cp_drv_boxes` — **every** row's checkbox, on both edges of every press, and `os88ui_glyph` white-fills its box before it draws a pixel. Measured on a cycle-accurate 5150/CGA with `tests/drvscroll.py`: the press is **9 frames = 133.3 ms** of visible redraw with the flashing rect spanning the whole list, the release carries another **100 ms** in front of the scroll's own repaint, and a press on a **greyed** arrow spends **116.7 ms** twice to draw nothing at all | the one control the down state moved to or from: the press is **2 frames = 16.7 ms**, the flash is the mouse pointer's own cell and the rows are 0 differing pixels; a greyed arrow is **0 frames**; the release is the scroll's pane repaint alone, **367.1 → 267.1 ms**. A driver row's own press 133.3 → **33.6 ms**, one glyph instead of four | §31.1.2 |
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
| A renderer row step | `call gfx_nextrow`: a near call plus two CS-overridden memory reads, **three times per scan line** | three register instructions, parameters hoisted out of the loop | §39.3 |
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
| `sysbench`'s three CPU books, and the tier that picks one | Part 8.1, below |
| The benchmarks themselves | `tests/fontbench/`, `tests/typebench/`, `tests/gfxbench/`, `tests/sysbench/` (`make bench`) |
| The field measurements they produced | Part 9, below |

---

## Part 8.1 — `sysbench`'s CPU rows carry THREE books, and pick by tier

`tests/sysbench`'s CPU block measures each instruction and prints the
**book** figure beside it, so the ratio column says how far the machine is
from the manual. That book was Intel's **8086** table, unconditionally, on
every machine — which is right on the target and wrong on everything else,
and the Packard Bell Victory 286 (docs/FIELD-MACHINES.md) is where it showed:

```
est CPU MHz x100 MUL    8879        <- an 8879 on a 16MHz 286
est CPU MHz x100 DIV    9759
shl clk/bit x100 ~400     28
```

**None of those three is a measurement error.** `est CPU MHz` inverts
`MHz = 4.7727 x nominal / measured`, which is exact — with the *right*
nominal. A 286 does `mul r16` in 21 clocks and an 8086 in 125, so quoting the
8086's figure inflates the answer by exactly that ratio. Re-derived against
the 286 book, from the same report's own measured column:

| | measured x100 | 8086 book | est MHz | 286 book | est MHz |
|---|---|---|---|---|---|
| `mov ax,i + mul r16` | 693 | 12900 | **88.79** | 2300 | **15.83** |
| `xor+mov+div r16` | 782 | 16000 | **97.59** | 2600 | **15.86** |

Two independent rows landing on 15.83 and 15.86 for a machine the register
calls a **16 MHz** AMD 286 — agreeing with each other to 0.2% — is the
cross-check `sb_mhz` was written to provide, working for the first time on a
machine that is not an 8088.

**One row per instruction, three book columns, `sb_nomof` picks.** The
alternative is a table per CPU, which is three places that must agree about
what row 12 is. `sb_entof` owns the stride for the same reason: it was 8
bytes with four sites open-coding `shl bx, 3`, and an open-coded stride is a
silently wrong row rather than a build error.

**The ratio column reads differently on a fast machine and that is not a
defect.** It is measured-in-4.77MHz-units over book, so on a 16MHz machine
every execution-bound row lands near **477/1584 = 30**. The Packard Bell's
register rows duly cluster at 26–30 with the 286 book, where the 8086 book
scattered them from 4 to 29 — and the rows that do *not* join the cluster are
the interesting ones: its memory rows sit at 61–81, which is real bus cost
the 286's zero-wait-state book does not carry.

**`shl clk/bit` had to change currency, not just books.** It is a slope — the
two `shl r16,cl` rows nine bits apart, subtracted — and it was reported in
4.77MHz clock periods, so a 286's one-clock-per-bit shift measured **0.28 of
one**: true, and unreadable against any book. It is scaled by the MUL
estimate into the machine's own clocks now (`x100 * MHzx100 / 477`), which on
a 4.77MHz machine is a factor of 1 **by construction**, so tier 0's published
figure does not move. The Packard Bell reads **92** against the 286 book's
100. The book row beside it is derived from the same two table rows the
measurement subtracts — `(nom13 - nom4) / 9`, giving 400 / 100 / **0** — and
the 386's zero is the barrel shifter, which is the whole reason this row
stopped meaning anything past tier 0.

**What is validated and what is not.** The 286 column is confirmed against
field data, above. The 386 column is **book figures that have not yet met
hardware** — and its MUL and DIV are *early-out* on that part, data-dependent
in a way the 286's are not, so treat a 386 `est CPU MHz` as approximate until
a 386 report lands in Part 9. What is verified for it is the mechanism: a
tier-2 run prints `against the 80386 book` with the 386 column beside every
row and `shl clk/bit book` at 0.

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
| `sysbench:` the **hard-disk block** | **§52's driver on real spinning MFM, which has never been measured** — and the first hard-disk twin of the floppy rows. Read-only by construction: it mounts, walks the FAT, reads one file and puts the volume back, because the disk it will run against is somebody's DOS 3.3 install (docs/FIELD-MACHINES.md) | anything at all — **and it has since been measured: 74,553 B/s against the floppy's 21,307, 3.5x** (Set 24). The floppy figure moved twice while this row said 7,457: check which side of Set 17 (the `AL` fix) AND of Sets 22/24 (§18.95's cache) a figure comes from before comparing anything to it. `HDD FILE_DFREE` is the one to watch — the 9-sector FAT window (§18.8) has to page across a 41-sector FAT, which is what §18.8.1 was written against |

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

**Reading the fullscreen pairs had one trap, and §32's removal retired it.**
`[bb_mono]` was one-way and `bb_mono_chk` five instructions cheaper once it
had retired — so if anything drawn between the two passes used a colour that
is not 0 or 15, every fullscreen row came in slightly under its twin for a
reason that had nothing to do with fullscreen. Neither the flag nor the call
exists now; the paragraph is kept because the *shape* recurs, and because the
figures below were taken while it did. It shows as a flat
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
| adapter | **CGA (640x200)**, `VIDEO=cga` forced — this machine's SW1-5/6 say mono, so §39.1's `int 11h` rung boots it on the Hercules and the set needed a kernel told otherwise. Since §39.11 that forcing is unnecessary: the Display page switches the card at run time |
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
clip test, `bb_mono_chk`, the `[bb_on]` dispatch, `vga_rect_setup`,   <!-- pre-§32-removal -->
`gfx_rowbase`, `bb_dirty_rect`, `sw_ink`, `sw_plane_op`, `sw_col`. **About a
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

The same suite on **VGA**, where the planar VRAM bodies run and `sw_col`
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
glyph cell, **VGA pixel-identical**, and
**Hercules** differing by 10 pixels of menu bar — against 17 between
two boots of the *same* kernel, so below that fixture's own reproducibility.

#### What is left, and what it would cost

Priced from the same teardown, for whoever comes next:

| still on the floor | worth | why it was not taken |
|---|---|---|
| `gfx_rowbase`'s `mul` by the stride | ~145 clocks, 4% | a per-row table is 2 bytes x `[vid_h]` — 960 on VGA, and `KERN_BUDGET` has 1,536 left |
| `sw_rect`'s eight push/pop pairs | ~240 clocks, 7% | it is `gfx_fill`'s "clobbers flags" contract, which every caller in the tree leans on |
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
cell, the `GFXCLIP` test, the `bb_mono_chk` call and the `bb_on` dispatch —   <!-- pre-§32-removal -->
about **170 of ~4,381 clocks**. It cannot remove eight push/pop pairs,
`vga_rect_setup`'s twenty-odd memory accesses, `gfx_rowbase`, the dirty-rect
and mode round-trips, the plane loop or `sw_ink`, because those are per
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

> **The disk half of that heading expired at Set 37.** The `read 16K, cold
> motor` row below — 0.27 s against the 5150's 8.07 — is upstream MartyPC
> with no platter in it. Sets 35 and 37 gave it one; the same row now reads
> 1.15–1.65 s against the field's 1.59 (Set 38, and it is an N = 1 row). The
> CPU half stands unchanged.

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

  > **The interleave half of that is WRONG — see Set 37.** The media is 1:1
  > and the missing revolution is the IBM ROM's own head-settle delay loop,
  > 52.5 ms of `LOOP $` once per `int 13h`. The B/s agreement that seemed to
  > confirm 2:1 is bytes over the *whole call*, overhead included, so it
  > cannot tell the two apart. The rest of the bullet stands: nine sectors in
  > one command does stream.
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

> **Set 37 overturns the interleave line and keeps the target.** 11,570
> against 11,520 is bytes over the *whole* `int 13h` call, and a quarter of
> that call is the ROM's head-settle delay loop — so 1:1 media plus 52.5 ms
> of BIOS produces the identical figure. The media is 1:1. What the drive
> does is unchanged: 11.5–13.4 KB/s is still the target, because it is
> measured rather than derived.

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

> **Set 37 changes the CAUSE and not the finding.** Both machines' media is
> 1:1; what differs is the per-call BIOS overhead. Read the two track rows as
> 1.00 revolution of transfer plus the rest: the 5150 spends **0.99 rev** of
> overhead and latency, the Compaq **0.24**. That is what a 52.5 ms delay
> loop on a 4.77 MHz 8088 looks like against the same nominal 25 ms wait on a
> Compaq ROM and a 12 MHz 286 — big enough to overshoot sector 1 and cost a
> whole turn on one machine, small enough to fit the slack on the other. The
> 1.87x, the 1.37x and "the faster disk is the one os8088 wastes more of" are
> all measurements and all stand. The `interleave that implies` row does not.

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

### Set 21 — what the sound buffers actually need, sampled from outside the guest

MartyPC, `os8088_5150_sb` (cycle-accurate 4.77MHz 8088, DSP 2.01, CGA), with
the driver's own counters read over the debug connection — **no code in the
guest**, so the measurement costs the machine nothing. `lead = [sbl_total] -
[sbl_consumed]` is the ring lead in bytes; the driver's segment comes out of
`drv_tab` row 0 and its variable offsets out of a `nasm -l` of the driver.

Beverly Hills Cop, XT mode (5,500 Hz), ~1,500 samples over 40 s each:

| Tracker build | ring | top-up target | lead min | median | max | underruns |
|---|---|---|---|---|---|---|
| shipped, windowed | 16KB (8 halves) | 7 | **6.00** | 7.00 | 8.00 | 0 |
| shipped, fullscreen (§53) | 16KB | 7 | **6.00** | 7.00 | 8.00 | 0 |
| `TRK_RING` 8KB, `TRK_PREROLL` 3 | 8KB (4 halves) | 3 | **3.00** | 3.00 | 4.00 | 0 |

Three things follow.

**The steady state needs four halves, not eight.** The worker tops the ring
up to `TRK_RING - TRK_HALF` and the card takes one half per block IRQ, so the
lead simply oscillates about the target; the deepest drain below it in either
mode was **one half**. Fullscreen measuring identical to windowed is
§53.5.1's `FSXW_FRAME` fix holding — that table's `1.0 min` was the bug.

**What sizes the ring is `TRK_PREROLL`, not the lead.** Six halves are staged
before the open, and the open refuses a ring smaller than what is staged
(`cmp cx, ax` / `.e7`), so a straight `TRK_RING` 8192 does not play at all —
it reports *Sound open failed*. Six halves came from a field report of a
hitch ~744 ms into the first play after a load (the old pre-roll of two), so
the 16KB grant is **a field bug fix expressed as a buffer size**, not slack.
The 8KB row above only exists because the pre-roll was given back to get it.

**And the harness could not price the case the cushion is for.** MartyPC was
cycle-accurate and 30x fast on disk when this was written (Set 11), and the
transient a ring absorbs is mostly floppy: one ~12-sector mount at the 5150's
~65 ms/sector is **~780 ms ≈ 2.1 halves** at the XT rate. *Set 37 changed
that half of it* — the floppy turns now and a mount costs real guest time, so
this IS reachable on an IBM-ROM machine, and re-measuring it is open work. Against a 7-half target that is
comfortable and against a 3-half one it is not. So *do not* read the third
row as a licence to shrink the ring — that is a number this instrument is not
entitled to give, and the 5150 is where it would have to come from.

**ModPlug is the divergence SPEC.md §56.1 predicted.** Same 16KB ring, same
2,048-byte halves, and a pre-roll of **two** halves — exactly what Tracker
had before the field report. Unmeasured here, and worth a run on the iron
before anything is concluded about it.

#### Set 21.1 — XT mode confirmed, and where Tracker's grant actually fails

Set 21 asserted "XT mode" from the source. Read off the card instead
(`[sbl_tc]`, the DSP time constant, `TC = 256 - 1e6/rate`): **0x4B → 5,524
Hz** on every 5150 run above, windowed and fullscreen. XT mode is armed by
`OSAPI_CPU_INFO` returning tier 0, so it is a claim about the **CPU**, not
the RAM — every 8088 gets it, the 640KB field machine included.

**A 128KB machine cannot run Tracker at all.** Its region alone is **48KB**
(measured off the claim map, not the 16,480-byte image), against a 36.5KB
heap — the launch fails with *Out of memory* before a window exists. So
"XT mode is every 128KB machine" is true of the *rate* and empty of
Tracker: the smallest machine that runs it is 256KB.

The claim map on `os8088_5150_sb_256k` (164.5KB of heap), with a 75KB module
loaded, is where the interesting failure lives:

```
CLAIM   8.0 KB  the SB DMA buffer (§34.6.1)
CLAIM  75.0 KB  the module
free   11.5 KB  <-- everything the ring grant can come out of
CLAIM  48.0 KB  Tracker's region
CLAIM   6.0 KB  SOUND.DRV's image
```

**The module loads and then Play refuses.** `TRK_RING` is a flat 16,384, the
pool tiers to 8KB out of 11.5, `sbl_grant_alloc` answers err 7, and Tracker
says *Out of memory* over a module whose title is on screen. Reproduced
deliberately: a 2.6KB module plays (largest run 63.5KB), a 98KB one is
honestly refused at load (*Too big for free memory*, ceiling ~86.5KB), and
**67–86KB is the band that loads and cannot play**.

Two fixes, and they are not alternatives:

- **Reserve before sizing.** `trk_needk` is checked against
  `OSAPI_MEM_AVAIL` with nothing held back for the ring, so the load
  succeeds into memory the Play needs. Checking against
  `avail - TRK_RING - a pool floor` moves the refusal to the load, where it
  is honest and cheap. No trade, no field risk.
- **Tier the ring.** 16,384 → 8,192 → 4,096, with `TRK_PREROLL` scaled to
  `ring/half - 1`. This is the one that widens the range rather than just
  reporting it, and it costs the **pre-roll**: 6 halves is 2.23 s at the XT
  rate, 3 halves is 1.12 s, and the field hitch that raised it from 2 landed
  at **744 ms**. 1.12 s is 1.5x a figure known to be too short, which is a
  judgement the iron has to make, not this harness.

And the units are XT mode's own argument for the small ring: 8KB at 5,500 Hz
is **1.49 s**, exactly what 16KB buys at 11,000. A tier-0 machine's rate is
half, so its ring may be half for the same seconds of cushion.

### Set 22 — the disk round on the iron: 10x on a read, and the install measured

One `sysbench` run and one install on the 5150 (`herc.img`, Hercules,
`DISKCNT=1`), after §18.9.2/§18.9.3/§18.95/§18.95.4 and §18.4.3.

#### The read is 10x, and it has passed the BIOS

| 16 KB `FILE_READ` | Set 15 | Set 17 | **now** |
|---|---|---|---|
| cold motor | 8.29 s | 2.09 s | **1.21 s** |
| warm | 8.35 s | 2.20 s | **0.82 s** |
| throughput | 1,912 B/s | 7,457 B/s | **19,883 B/s** |

**2.7x on this round and 10.4x since Set 15** — and the number that matters
more than the ratio: **19,883 B/s against the BIOS's own best**, a whole track
in one `int 13h`, of **11,984**. os8088 now reads its own files 1.66x faster
than the fastest thing the ROM can do with this drive, because §18.95's cache
answers a repeat without a revolution. Set 17's "1.55x still on the table
against 11,570" is spent and then some.

The counter block says how: **one 16 KB read is 18 sectors in 2 `int 13h`**,
both runs of 9, no mounts, no resets — against Set 15's **148 sectors in 34
calls** for the same file. Fourteen of the file's 32 sectors were already in
the cache from the rows above it, which is the feature working rather than a
measurement artefact; the two 9-sector runs are what is left.

**The calibration is unchanged, which is what licenses the comparison.** Raw
`int 13h`: one sector **199 ms** (one 300 RPM revolution exactly), a whole
track in one call **384 ms** = 1.92 revolutions = 11,984 B/s, the same nine
sectors in nine calls **2.005 s**. Set 14 measured 384 ms and 11,985 B/s on
the same machine. The drive, the controller and the interleave have not moved.

**The boot has not moved either** — 180 ticks / 9.886 s against Set 18's 181 /
9.94 s — and should not have: the boot sector's read happens before the kernel
exists, so nothing in this round can reach it.

#### §18.95.2's cursor, priced on the machine it is for

`int 13h` **in a whole 8-entry directory walk: 0**. That is §18.95.2's entire
argument, measured on the iron rather than simulated: there are no revolutions
there for a cursor to save. What is left is CPU, and the field puts it at
**112.01 us per entry skipped** — against MartyPC's 60.11, so the emulator
under-states this by 1.9x and a decision taken on its figure alone would have
been taken on half the real cost. Even so: an 8-entry walk is 37.0 ms and a
perfect cursor 29.3, so the cursor is worth **7.8 ms**, and **55.6 ms** at 32
entries. The recommendation stands.

(The walk row carries `!` — one iteration close to the 55 ms wrap — so read
37 ms as approximate. The slope, taken from two short rows, is not affected.)

#### §18.95.4's capacity is LOWER on the iron: 8, not 12

The cliff is at 10 chunks on the 5150 and at 14 on MartyPC, so the widest
working set kept free is **8** where the emulator said 12. Same compiled
`DSK_RAH_RUNS` = 14, different volume: the field disk's root has 12 entries
against the bench floppy's 7, so resolving `BIGFILE.DAT` by name walks more
directory sectors and those take more slots. The row is documented as a lower
bound on the constant and it behaves like one — **and the gap between the two
machines is the walk's metadata, which is a property of the DISK and not of
the cache.**

#### The install: 46.2 s, and three quarters of it is the floppy

**IT WORKED.** The ST-225 was partitioned, formatted and written by os8088,
and the machine then **booted from it and ran**. That is the first end-to-end
hard-disk install on the hardware SPEC.md §52 was written for, and it is worth
recording separately from the timings: every figure below is measured on an
install whose output is a working system, not on one that merely ran to
completion. It also retires the §52.11.4/§52.11.5/§52.11.6 round — those three
fences were found on an emulator, and the machine agrees.

`INSTBNCH.TXT` off the machine, onto a pristine ST-225 partition:

| | fC | fS | dC | dS | tk |
|---|---:|---:|---:|---:|---:|
| `fmt` | 0 | 0 | 118 | 118 | 43 |
| `sys` | 82 | 715 | 209 | 818 | **768** |
| `app` | 5 | 33 | 6 | 6 | 30 |
| `TOT` | 87 | 748 | 333 | 942 | **841** |

841 ticks is **46.2 s**. Priced against this machine's own measured 384 ms for
a track in one call, `sys`'s **82 floppy calls are ~31.5 s of its 42.2** —
about three quarters — leaving ~10.7 s for 209 device commands, ~51 ms each,
which is what an ST-225 seek-and-write costs.

**The floppy side is already at the ceiling per call**: 715 sectors in 82
calls is **8.72 sectors a call**, within 3% of the nine a track holds. There
is nothing left in coalescing.

**What is left is the sector COUNT**: the files that arrived on C: are about
**290 sectors** of payload and the install read **715**. That is 2.5x, which is
Set 15's shape in a new place — and unlike Set 15 it is not a contract being
misread, because the calls are properly batched. It is the next thing to look
at.

**`fmt` writes one sector per command** — 118 for 118 — which is `hd_fmt`'s
own loop handing `hd_bios_xfer` a sector at a time. On this drive that is 43
ticks, **2.4 s**, so it is real and it is small; it would be the obvious win
on a slower drive and is not the one to spend effort on here.

**`app` did nothing**, and that is the harness rather than the OS: a field disk
carries the benchmarks in its root and has no `APPS` folder, which is what
§52.10.5 keys "this is the apps disk" on. The install is the system disk's
alone, and its 46.2 s is a floor, not the whole job.

#### The correction, and how it was caught

**This set claimed a 2.5x surplus in the install's reads and there is none.**
The error was not in the measurement but in the denominator: a payload guessed
from what lands on C: rather than read off the disk being copied. The lesson
is the one Part 4 keeps restating in other words — *a ratio is only as good as
the quantity underneath it*, and the quantity here was available from one
`os88disk` line that the write-up did not go and look at.

What caught it was §18.94.3's trace, on MartyPC, at no cost to anyone's
morning: the same install there gives **118 calls / 946 sectors** against the
field's **87 / 748** — the emulator copies a second floppy the field run had
no `APPS` folder for — and the distinct-sector count is what settles the
question. That division of labour is worth stating on its own: **counts and
traces on MartyPC, seconds on the 5150**, and a claim that needs both should
not be made from either alone.

### Set 23 — the interior row walk stops banking DI (SPEC.md §5.7.1)

`tests/gfxbench` on MartyPC, all three adapters, the same kernel before and
after the change and nothing else different. Reported by a **forum reader**
(`raphlinus`) reading `gfx_fill`'s `.irow` on GitHub — the loop banked DI
around a `rep stosb` that had already advanced it — which §5.7's own pass had
walked past three times.

| `GFX_*` row, counts | CGA | Hercules | VGA (12h) |
|---|---|---|---|
| `GFX_FILL 64x64` | **−3.26%** | **−4.02%** | **−10.75%** |
| `GFX_FILL 64x64 clipped` | −3.08% | −3.79% | −9.22% |
| `GFX_FILL 256x128` | −2.57% | −2.98% | −5.21% |
| `GFX_FILL_GRAY 64x64` | −3.27% | −4.02% | **−14.75%** |
| `GFX_FILL_PAT 64x64` | −1.76% | −2.24% | −9.04% |
| `GFX_XOR_FILL 64x64` | −3.95% | −4.17% | −5.73% |
| `GFX_FILL 8x8` | **+0.00%** | +0.00% | +0.00% |
| `GFX_FILL 256x1` (ONE row) | −0.09% | −0.78% | +0.00% |
| `fill ns per row` | 187,091 → **182,109** | — | — |

**The last two rows are the result, as much as the first one.** A one-row
fill does not move, because what this removes is per scan LINE and §5.7's
~756 µs floor is per CALL — so the lever is exactly where §11.90/§11.91 said
the money is not, and it is worth having only because a fill is usually tall.
VGA gains 2–4x what the mono adapters do: its interior `rep` covers four
planes at once through Set/Reset, so the loop is short and its overhead a
larger share, where `sw_plane_op` walks a plane with `stosw` and does more
work per turn.

**Two rows moved the wrong way and neither is a regression**, which is worth
recording because both look alarming and both are the instrument. `GFX_BLIT4
4px runs` (+0.99%) and `one full-width row` (+6.67%) are exact multiples of
65,536 and carry gfxbench's own `w` flag — *an iteration LAPPED the counter,
so that row is tick-timed and coarse* — so they moved by one tick, which is
their resolution. And `GFX_UNLOCK+LOCK pair` read −3.27% on CGA and −3.20% on
Hercules against untouched code: **the same kernel measured twice moves it
+3.31%**, because it is the one row whose span cannot be interrupt-free
(`gfx_lock` ends with `sti` by contract). Every other finely-measured row
repeats inside 0.5%, which is what makes the fill column above mean anything.

**Correctness is the byte-identity, not the picture.** A seven-step scripted
session — desktop, a menu HELD open over its save-under, the item picked, a
Disk window, an XOR selection band, that band moved, and a window dragged —
captured through `vram` on the two 1bpp cards (one value per pixel, banks
resolved) and `fbuf` on VGA: **21 of 21 steps, 0 differing pixels.** The
apparatus needed one correction first, and it is Part 4's rule again: a
capture taken mid-repaint reported 31 differing pixels on Hercules, and the
OLD kernel diffed against ITSELF reported the identical 31 in the identical
box. `settle` before each grab; the control is what said which of the two
kernels was lying, and the answer was neither.

**Two more apparatus traps, both found re-running this at the merge onto
`elendilon`, and both worth knowing before trusting any framebuffer diff
here.** `vram` hands back **one value per pixel** with the banks already
resolved — it is not packed — so a bbox computed as though it were reports
an x scaled by 8 and a y eight times too large, which put a menu-bar
difference in the middle of the desktop and sent the first reading of it
looking at the wrong subsystem entirely. And **the menu bar CLOCK is a
difference the kernel is right to make**: with no RTC the fallback starts at
`Jul 04 2026 00:00`, a VGA session runs about 50 s of guest time, and a run
that crosses the minute shows `00:01` against the other's `00:00` — 93
pixels in one 8x8 cell at (624,6), reproducing exactly because it is
systematic rather than random, which is precisely what a real defect looks
like. Crop the cell and read it before believing it.

Cost: `.text` +13 bytes (the per-row saving is 2–4 bytes, the once-per-call
setup 2–9), no rung crossed, `KERN_SIZE` unchanged.

### Set 24 — the WRITE priced on both drives, and the append penalty is real

`REPORTS9`: two `sysbench` runs on the 5150, one with `A:` current and one
with `C:` current, both booted from the **ST-225**. Same binary, same session,
so the two write columns differ only in the medium under them. Set 22's
`bl_usfield` wrap is gone from this build — the whole-microsecond field is
nine digits and the append row reads 11.04 s rather than 1.04.

**The boot is 59 ticks / 3,240 ms from the ST-225**, against 181 / 9,941 from
the floppy in the previous round: **3.07x**, and the first figure this project
has for what a hard disk is worth at boot on the target machine.

| | floppy `A:` | ST-225 `C:` |
|---|---|---|
| 16KB read, warm | 768,960 us — **21,307 B/s** | 219,703 us — **74,553 B/s** |
| 16KB read, cold motor | 1,263,292 us | 329,554 us |
| BIOS's own track, 1 call | 398,211 us — 11,570 B/s | (floppy row) |
| one cluster written | 1,263,292 us | 329,554 us |
| create | **32KB** in 4,668,686 us | **128KB** in 1,428,069 us |
| …as sectors / `int 13h` | 73 / 12 (6.08 a call) | 261 / 22 (11.86 a call) |
| replace | skipped: needs 2x free | 1,537,920 us, 265 / 25 |
| append, 8KB chunks | 11,040,070 us, 100 / 33 | 5,437,646 us, 366 / 126 |
| append ÷ create | **2.36x** | **3.81x** |

**A 16KB read is now 1.84x the BIOS's own one-call track read** (21,307 B/s
against 11,570), which is not a paradox and is worth saying plainly: a
`dsk_xfer` run spans the track boundary with the multi-track bit set, so it
gets more than one track's sectors per revolution, where the ROM's single
call stops at EOT. The figure that used to bound us is behind us.

**The append penalty is the whole finding.** The same bytes written as a run
of 8KB chunks instead of one call cost 2.36x on the floppy and 3.81x on the
hard disk, and the split says why: sectors go 73 → 100 (1.37x) and 261 → 366
(1.40x), while `int 13h` calls go 12 → 33 (**2.75x**) and 22 → 126 (**5.7x**).
The extra sectors are the FAT and the directory; the extra *calls* are that
each chunk is a whole §18.4 commit — allocate, flush the FAT, link, flush
again, write the directory sector — and a commit is four small, scattered
transfers that no coalescer can join to anything.

So the lever is **commit once per file, not once per chunk**: hold the FAT
dirty across the run and write the directory entry at the end, which is what
`dskw_write` already does for a whole-file write and what `dskw_append` cannot
do because each call is a complete operation by contract. Nothing here is
implemented; this set is the measurement that says what it would be worth —
**2.4x to 3.8x on every chunked write in the system**, which is the file
manager's copy (§22.5), the installer's big files (§52.10.11) and any package
saving something larger than its buffer.

**The ST-225's own numbers, for the record**: `FILE_DFREE` 315,823 us, a mount
and back 302,091 us, one cluster 329,554 us. And `COMMAND.COM` answers
`FERR_NOENT` — the DOS 3.3 install docs/FIELD-MACHINES.md used to warn about
is gone, overwritten by an os8088 install, so that row measures nothing now
and should be pointed at a file this OS puts there.
### Set 25 — the typeface priced: tallx against the IBM ROM face (SPEC.md §6.2)

`tests/gfxbench` on MartyPC, CGA, a **cycle-accurate 4.77MHz 8088 running the
real IBM 5150 BIOS** (`BIOS_IBM5150_27OCT82_1501476_U33.BIN`, md5
`f453eb2d…`, the ROM MartyPC knows as `ibm5150_82_v4`). Asked because tallx
looks sparser than the ROM face and a blank glyph row is the one thing
`font_char` skips whole — so the question was whether the baked typeface costs
the machine anything.

**Both faces were BAKED for the A/B.** Comparing tallx against a stock kernel
would compare two different binaries — the ROM probe is assembled in one and
not the other — so the ROM face was extracted from the U33 image at
`F000:FA6E` and built as a `.f8` too. The two kernels are **85,384 bytes each
and differ in 395 bytes, all of them inside the 760-byte glyph table**; the
guest's live `font_glyphs` confirms it (the ROM build reads **760/760
identical to the ROM**, the tallx build 365/760). Every non-text `gfxbench`
row agrees to ±10 counts in ~200,000 — 0.005%, which is the noise floor and
also the proof that nothing but the glyphs moved.

| `gfxbench` row | IBM ROM | tallx | delta |
|---|---|---|---|
| `FONT_CHAR one cell` (`'W'`) | 895.46 µs | 880.17 µs | **−1.71%** |
| `FONT_STR 10 aligned` | 8,403.00 µs | 8,297.39 µs | **−1.26%** |
| `PAIR 10 aligned` | 10,152.95 µs | 10,047.35 µs | −1.04% |
| **`FONT_RUN 10 aligned`** | 8,879.66 µs | 8,880.01 µs | **+0.00%** (+5 counts) |
| `PAIR 10 skewed 5` | 10,659.09 µs | 10,433.22 µs | −2.12% |
| `FONT_RUN 10 skewed` | 10,840.54 µs | 10,618.65 µs | −2.05% |
| `FONT_WIDTH 10` | 217.91 µs | 217.91 µs | +0.00% |
| `boot ticks` / `kernel span KB` | 60 / 94 | 60 / 94 | **identical** |

**tallx is the CHEAPER face, and the direction was the surprise** — it has
**32.0% blank rows against the ROM's 25.3%**. It is **rows and never
columns**: `or ah, ah / jz` is the renderer's only content-dependent branch,
and a row is a whole byte whatever is in it.

**`FONT_RUN` aligned at +0.00% is the result, as much as the first row.** The
fast path (SPEC.md §6.1, mono, `x & 7 == 0`) is branch-free — the cell owns
its framebuffer byte, so it *stores* every row whether the glyph set any bits
or not. A face's ink costs exactly nothing there, and that is the path every
hot incremental redraw in the tree was moved to (§12.9's menu bar, §22.11's
scroll, §27.2's Note Pad row, §11.94's `WF_SNAP` consumers). **Skewed, there
is no fast path** — `font_run` falls back to the pair — so it becomes
content-dependent again *and* picks up a second term: the low `x & 7` bits of
a row spill into a second byte, which is the one place a face's COLUMNS cost
anything and is what §6.1.4's blank column 7 is about. Predicted −0.62 spill
writes a cell at `x & 7 = 5`, measured as the skewed delta being twice the
aligned one.

**The model predicts the measurement from the glyphs alone**, which is what
makes `tools/os88fontcost.py` worth having: the bench string `'C-2 01 A0F'`
differs by 7 drawn rows, ×81.0 clocks (Part 9's five-instruction step) = 118.8
µs predicted against **105.61 µs measured**; `'W'` differs by one row, 17.0 µs
predicted against **15.29 µs**. Inverted, the measurement prices **one drawn
row at 15.09 µs** on this CGA — the same number from the other end, the gap
being that a *skipped* row still pays its own load, test and branch.

**And the honest figure for a session is HALF the bench's.** `'C-2 01 A0F'`
is uppercase-and-digit heavy, which is exactly where tallx wins: its capitals
are **6 rows against the ROM's 7**, and so are its digits. **Lowercase is a
wash** — 5.77 rows against 5.81 — because tallx spends on a 5-row x-height
what it saves on cap height. Weighted by os8088's own 29,082 characters of UI
text, which is mostly lowercase, the face is **4.557 rows/glyph against 4.815,
so −0.46% of an 840 µs cell**. Against a 2.50 s page of text that is 11 ms.

**Nothing else moved.** Identical `boot ticks`, identical `kernel span`,
identical `KERNEL.SYS` sector count — the glyph table is 760 bytes either way,
so a face costs the same disk, the same RAM and the same boot on any machine.

**What could NOT be measured, and it is worth knowing why.** An end-to-end UI
operation cannot see this at all: opening a Disk window (~40 `font_str` calls)
measured **18.29 s and 20.97 s on two trials of the SAME kernel**, a 15%
spread against a 0.5% effect — the floppy mount dominates it, and at the time
MartyPC was not disk-accurate either (Set 11; it is, since Set 37 — but a
mount dominating a 0.5% effect is a fact about the operation, not the
instrument, so the conclusion is unchanged). A per-primitive harness with the interrupts off
is the only instrument with the resolution, which is what `gfxbench` is for.

### Set 26 — `font_run`'s compose, and the blank-row skip that does not transfer (SPEC.md §6.1.5)

Asked directly, after Set 25: `font_char` skips a blank glyph row, so can
`font_run`?

**No, and the reason is what `font_run` IS.** It is *opaque* — the cell owns
its framebuffer byte, so a blank glyph row is still **painted**, with the
background. There is no write to skip. What could be skipped is the compose
arithmetic, guarded by `or al, al` + `jz`: four bytes on every row to save
eleven on the ~30% of rows that are blank, plus a taken branch's prefetch
flush, on the innermost loop of every string the slow machines draw.
Break-even at best.

**The compose itself was the thing to fix, and §6.1 had already noticed it
without the code taking it** — "which on mono reduces to the glyph or its
complement". `(glyph & ink) | (~glyph & background)` is
`(glyph & (ink ^ background)) ^ background`, both terms constant per plane; in
CL and CH a row becomes a load, an `and`, an `xor` and a store.

| `gfxbench` row, CGA | baseline | hoisted | delta |
|---|---|---|---|
| **`FONT_RUN` 10 aligned** | 8,879.59 µs | 7,898.60 µs | **−11.05%** |
| `FONT_CHAR` one cell | 895.44 | 895.27 | −0.02% |
| `FONT_STR` 10 aligned | 8,402.93 | 8,401.88 | −0.01% |
| `PAIR` 10 aligned | 10,152.88 | 10,151.83 | −0.01% |
| `FONT_RUN` 10 skewed 5 | 10,839.77 | 10,840.26 | +0.00% |
| `PAIR` 10 skewed 5 | 10,658.81 | 10,659.02 | +0.00% |
| `GFX_PIXEL` · `GFX_FILL 64x64` · `GFX_FILL 8x8` | — | — | +0.00% |

**The unmoved rows are the result as much as the first one.** Skewed is
+0.00% because an unaligned run never reaches `font_run_cell` at all (§6.1's
gate), and `FONT_CHAR`/`FONT_STR` are untouched code — so the change landed
exactly where it was aimed and nowhere else. Cost: `.text` **−12** bytes,
`.bss` **−2**, no rung crossed.

**Byte-for-byte on both mono adapters**, a scripted desktop → chip menu →
Disk-window session through each kernel: **Hercules 0 differing pixels of
250,560 on all three screens**, CGA 0 on two.

**The third CGA screen reported 31 differing pixels and is Set 23's trap, not
a defect.** The bbox was x 624..630, y 6..12 — *one 8x8 cell*, which is the
menu bar clock: with no RTC the fallback starts at `Jul 04 2026 00:00` and a
run crossing the minute reads `00:01`. Cropped and read as pixels rather than
believed, the baseline's last digit is `0` and the other's is `1`. Masking
that field, every screen is 0 of 128,000. **A systematic, reproducible
difference in one cell looks exactly like a real bug** — read it before
reporting it.

**What is left is bigger than what was taken, and it is NOT measured.**
`font_run_cell` runs the whole prologue **per cell** — two edge compares, the
clip test, the glyph address, `gfx_rowbase`, `font_ink` *twice* (each a
further near call) and the plane setup — and across a run those all produce
the same answers, because a run shares its y and both its colours and differs
only by one byte of DI per cell. Priced against this measurement: a cell of an
aligned run is ~888 µs of which the eight-row loop is ~270, so **~70% of
`FONT_RUN` is a prologue a run could pay once**. Near `call`+`ret` alone is
11.0–11.5 µs before any body (Part 9's table), so the ceiling is real — but so
is the restructuring, and this figure is inferred rather than measured.

### Set 27 — the run pays once: `FONT_RUN` is 1.86x (SPEC.md §6.1.6)

Set 26 ended by naming what it had not taken: `font_run_cell` ran the whole
prologue **per cell**, for values a RUN shares. This is that, measured.

A run is ONE y, ONE pair of colours and a column that advances by one byte a
cell — and the cell body was calling **`gfx_rowbase`**, whose 16-bit `MUL` an
8088 charges ~120 clocks for, and **`font_ink` twice**, per cell. Ten cells
paid ten row bases and twenty ink reductions to draw one word. All of it moved
up into `font_run_x`; `DI` then holds the run's first byte and the cell loop
does `inc di`.

**The plane loop went with it** — it walked `[vid_planes]`, 4 or 1, but the
path is gated on `[vid_mono]` three instructions above the cell loop, so the
4-plane walk could never execute. With one plane there is nothing to restart,
which retires `[font_rn_si]`, `[font_rn_di]`, `[font_rn_fs]` and
`[font_rn_bs]` as well.

| `gfxbench` row, CGA | original | §6.1.5 | §6.1.6 | vs original |
|---|---|---|---|---|
| **`FONT_RUN` 10 aligned** | 8,879.59 µs | 7,898.60 | **4,785.69** | **−46.10%, 1.86x** |
| `FONT_CHAR` one cell | 895.44 | 895.27 | 895.50 | +0.01% |
| `FONT_STR` 10 aligned | 8,402.93 | 8,401.88 | 8,402.93 | +0.00% |
| `PAIR` 10 aligned | 10,152.88 | 10,151.83 | 10,152.88 | +0.00% |
| `FONT_RUN` 10 skewed 5 | 10,839.77 | 10,840.26 | 10,840.47 | +0.01% |
| `PAIR` 10 skewed 5 | 10,658.81 | 10,659.02 | 10,659.02 | +0.00% |
| `GFX_PIXEL` · `GFX_FILL` 8x8 / 64x64 · `GFX_SCROLL` | — | — | — | ±0.01% |

Cost across both changes: `.text` 54,597 → **54,540 (−57)**, `.bss` 4,736 →
**4,730 (−6)**. No rung crossed. **Byte-for-byte identical to the ORIGINAL
kernel** — the pre-§6.1.5 one, not the intermediate — over a scripted
desktop → chip-menu → Disk-window session: CGA 0 differing pixels of 128,000
on all three screens, Hercules 0 of 250,560 on all three. (The Disk window's
31-pixel clock cell is Set 26's documented trap again.)

**One real hole was found and closed before it shipped, and it is the reason
to distrust a lockstep.** `DI` tracks x by `inc di`, and `(x+8)>>3 ==
(x>>3)+1` holds for every x **except across the 16-bit wrap** — so a run at a
NEGATIVE x (0xFFF0 = −16, which is byte-aligned and therefore takes the fast
path) skips two cells on the unsigned edge test, wraps to x = 0, and then
draws at `DI` = 0x2000 past where it belongs: a word rendered 8KB into the
framebuffer. The old code recomputed `DI` per cell and could not have it. The
guard is one compare per run — a run STARTING off screen goes per cell
(§6.1.2's path, which recomputes both) — and it costs 9.85 µs on a ten-cell
run, 0.2%. **It was found by arithmetic, not by a test**: every screen in the
pixel gate passed before and after, because nothing in this tree draws text at
a negative x today.

**What is left, and it is NOT measured.** The row loop is now ~70% of a cell,
and **44% of the row body is the banked-row advance** — `add di,
[cs:vid_rowadd]` + `test di, [cs:vid_wrapbit]` + `jz`, 12 of 27 bytes, paid 8
times per CELL. A run's cells are contiguous bytes *within* a row, so a
ROW-MAJOR walk would pay it 8 times per RUN instead, and would open `rep
stosb` for repeated glyphs — which §27.2's space-padding idiom makes common,
since there the padding IS the erase. It needs a per-run array of glyph
pointers (80 cells × 2 bytes of `.bss`) or 8x the glyph-address lookups, so it
is a real trade rather than a free one.

**docs/LAST-DROP.md carries the rejected one in full** — the patch, the
figures, the price and what would have to change for the answer to flip — so
the next session that has this idea can read it instead of building it.

### Set 28 — the run is drawn a ROW at a time: `FONT_RUN` is 2.84x (SPEC.md §6.1.7)

Set 27 named what it had left: the row body was 27 bytes of which **12 were the
banked-row advance**, paid eight times per CELL. But the cells of an aligned run
are consecutive bytes *within a row*, so that advance belongs to the row.

`font_run_x` is two passes now. Pass 1 walks the string once into
`font_rn_tab` — a glyph pointer per cell, stopping at the first cell past
`[vid_cwm8]`, so the drawn cells are a *prefix* and therefore contiguous. Pass
2 runs eight times, once per glyph row, each a `lodsw`/`stosb` walk of the whole
run with `gfx_nextrow` paid **once at the end of it**.

**The table is what frees `ES`**, which is the part that is not obvious: `ES`
holds the caller's *string* right up to the end of pass 1 (`font_run` is an X
stub — the string is in the package's segment), so a cell-major loop has to
reload it per cell to reach the framebuffer. Once the string has been read,
`ES:DI` is the framebuffer for the whole run and the masks sit in `DX` for all
eight passes.

| `gfxbench` row, CGA | original | §6.1.6 | §6.1.7 | vs §6.1.6 | vs original |
|---|---|---|---|---|---|
| **`FONT_RUN` 10 aligned** | 8,879.59 µs | 4,785.69 | **3,126.81** | **−34.66%** | **−64.79%, 2.84x** |
| `FONT_CHAR` one cell | 895.44 | 895.50 | 895.27 | −0.03% | −0.02% |
| `FONT_STR` 10 aligned | 8,402.93 | 8,402.93 | 8,402.93 | +0.00% | +0.00% |
| `PAIR` 10 aligned | 10,152.88 | 10,152.88 | 10,151.83 | −0.01% | −0.01% |
| `FONT_RUN` 10 skewed 5 | 10,839.77 | 10,840.47 | 10,843.40 | +0.03% | +0.03% |
| `GFX_PIXEL` · `GFX_FILL` · `GFX_SCROLL` | — | — | — | +0.00% | ±0.00% |

**Cost, and this one is NOT free.** `.text` **+137** and `.bss` **+176** against
the pre-§6.1.5 baseline — the 180-byte table plus its count — and **the image
rung CROSSED**: `KERN_SIZE` 95,744 → 96,256, spare 2,560 → **2,048, four
steps**, which is the standard this tree keeps. That was authorised in advance
for this path specifically. The three changes together are net **+137 `.text`**
for **2.84x**.

**Three bounds hold it up and each is a correctness argument.** A **clip
region** makes cells individually refusable and one rejected in the middle
breaks the run into pieces `stosb` cannot walk — so an armed region costs one
`wm_clip_test` over the whole run and a **cut** run goes per cell (§6.1.2's
path). **Table overflow** cannot happen while `FONT_RN_MAX` = 90 covers
`vid_w / 8`, and falls back anyway. And **`BP` carries the glyph row**, so
`font_run_x` saves it — §7.1.4.1 is what forgetting costs.

**Verified against the ORIGINAL kernel**, three screens on each mono adapter,
with a partly-occluded Task Manager behind the Disk window so the clip-region
fallback is exercised too: **0 differing pixels** on four of six screens, and
on the other two the difference is **8 pixels in one cell — and it is TRUE.**
The Disk window reads `Free 248K` where the old kernel read `Free 249K`,
because a kernel 512 bytes bigger takes one more 1 KB cluster: read out of the
two images' FATs independently, 249 free clusters against 248. Set 26's clock
cell was the same lesson and this is the second instance — **a systematic
difference in one glyph cell is what BOTH a real defect and a correctly
reported fact look like.** Read the cell.

**What is left.** The inner loop is 14 bytes — `lodsw`, `xchg`, `add bx,bp`,
`mov al,[ss:bx]`, `and`, `xor`, `stosb`, `loop`. Unrolling the eight row passes
would make the row a disp8 on the glyph fetch and retire `BP` and the `add`,
taking it to ~12; it costs ~240 bytes of `.text`, which is another rung, and at
that point the fetch is nearly all of it. A `rep stosb` for a run of identical
glyph rows (a span of spaces, which §27.2's padding makes common) is the other
candidate and needs a compare per cell to find the span.

### Set 29 — Set 28's two candidates, measured: one rejected, one kept

Set 28 named two and measured neither. Both were built against the same kernel
and run on the same machine — and **`gfxbench` had to grow two rows first**,
because the existing one could not have answered the second question:
`C-2 01 A0F` has no adjacent repeat, so a span optimisation measures as exactly
nothing on it, while the text this system actually draws is padded on purpose.
`FONT_RUN 20 text` and `FONT_RUN 20 padded` are the same LENGTH and differ only
in content.

**The baseline decomposes, which is what makes the rest readable**: 10 cells at
3,131.77 µs and 20 at 5,394.84 µs, so **~872 µs is fixed per run and ~226 µs is
per cell**. And `20 text` against `20 padded` is **0.02%** — content was free
before any of this.

| `gfxbench` row, CGA | baseline | A unrolled | B span |
|---|---|---|---|
| `FONT_RUN 10 aligned` | 3,131.77 µs | **−4.74%** | +1.44% |
| `FONT_RUN 20 text` | 5,394.84 | **−4.44%** | +0.84% |
| `FONT_RUN 20 padded` | 5,393.73 | −4.44% | **−31.33%** |
| `FONT_CHAR` · `FONT_STR` · `PAIR` · skewed · every `GFX_*` | — | ±0.01% | ±0.03% |

**A — unrolling the eight row passes: REJECTED.** The bar it was given was 5%
and it returns **4.74%**, for **+267 bytes of `.text`** (against the ~240
estimated) and a CROSSED rung — spare 2,048 → 1,536, three steps, under the
four this tree keeps. Two things make it a clearer no than the headline
suggests. The disp8 saves **one** byte a cell-row, not two: `mov al,[ss:bx+r]`
is 4 bytes where `add bx,bp` + `mov al,[ss:bx]` is 5. And **it gets worse as
runs get longer**, which is the direction real text goes — solving the two rows
gives a **fixed** saving of 57.5 µs (the row counter, once a run) and **9.1 µs
a cell**, so the percentage falls as the per-cell term dominates.

**B — the trailing span as one `rep stosb`: KEPT.** +138 bytes of `.text`, +4
of `.bss`, **no rung crossed**. It is a trade and both halves are real: −31.3%
on a padded run, **+1.4% on a run with no span**, which is the fixed cost of
deciding. Framebuffer **byte-identical** to the shipped kernel on both mono
adapters.

**The first version cost +3.06% on unpadded runs and the fix is the general
lesson**: it tested for a span *per row*, and a span is a property of the RUN —
so every unpadded run answered the same question eight times. Moving the branch
to `.rm` took it to +1.50%. A second refinement, gating the backward search on
the last two entries matching, took it only to +1.44% — **the diagnosis there
was wrong**: what remains is not the search but the ~45 µs of *deciding at all*,
and that is why the first fix was worth 1.5 points and the second 0.06.

**How much of a real session HAS a span, measured rather than argued.** Four
counters in a scratch kernel, two scripted sessions: **34%** of the cells
`font_run` drew were inside a span (42 of 124), and **28%** in the other (53 of
187); half the runs had one.

**The probe also produced a claim that was WRONG, and correcting it is worth
more than the original number.** In a 40-second session with the Task Manager
open, unoccluded and refreshing, `font_run` was called **four times** — from
which this entry first concluded that `FONT_RUN` is a burst cost that an idle
system barely pays. **That generalises from the single most change-gated
consumer in the tree.** §28 is the app that had `tm_rowok`, `tm_chunksum` and
the per-chunk cell walk built for it precisely so that a refresh which changes
nothing draws nothing; measuring text frequency with it is measuring how well
§28 was optimised, not how often os8088 draws text.

**The survey says the opposite.** Of fifteen shipped packages, **nine call
`OSAPI_FONT_STR` and never `FONT_RUN` at all** — Paint, ArtfulType, Solitaire,
Arkanoid, Fractal, Recorder, Piano, Minesweeper, Tamegram — so most of the
system's text does not reach this path yet, and the apps that *have* converted
are the ones that draw **long runs continuously while the user works**: Note
Pad's `np_rflush` letters a row space-padded to the whole band (§27.2),
Tracker's `tui_str` draws the FT2 screen, ModPlug composes four LCD lines an
18 Hz frame (§56.12).

So the right reading of Sets 25–28 is the reverse of the first one: **the 2.84x
lands on the paths that are least gated and longest-running**, and §6.1.8's
span is worth having for the same reason — a padded Note Pad row is exactly the
shape it collapses. The Task Manager is the one consumer that had already
solved the problem a different way.

### Set 30 — the raise cache's restore, and the sub-rect that halves it (SPEC.md §5.8/§11.96.6)

REDRAW-SPEC Part 3 left one instruction for whoever picked it up: *do not spend
the sub-rect work on the strength of the 644 → 215 ms figure — that is the
cache's win and it is already banked. Measure the restore itself first.* So this
set starts there, and the measurement is three exec breakpoints inside one
`wm_draw_win` on a cycle-accurate 5150/CGA, with `cycles` read at each.

A 318×136 Disk window content, raised with its cache live:

| span | cycles | ms |
|---|---|---|
| `wm_draw_win` → `wm_su_try` (all the chrome: frame, shadow, title bar) | 233,108 | 48.84 |
| `wm_su_try` → `gfx_restore` (`wm_su_ck` + `wm_su_edge`, the edge merge) | 86,962 | **18.22** |
| `gfx_restore` → `wm_grow_paint` (the blit) | 141,465 | **29.64** |

So the restore is **47.86 ms** against 48.84 for everything around it — the
blit is not a rounding error on a raise, and the **edge merge is 38% of it**,
which nothing had priced before.

**What the sub-rect is worth, on the case it is for.** Two Disk windows, the
front one dragged clear and then dragged *away* so it genuinely uncovers an L
(dragged the other way, §11.91.2 marks nothing at all and the whole question
does not arise). Same session, same machine, breakpoints on `gfx_restore` and
`gfx_sub_off`, which bracket the blit:

| build | rect restored | cycles | ms |
|---|---|---|---|
| whole content | (111,38,428,173) — 41 B/row × 136 | 146,212 | 30.63 |
| sub-rect | (216,80,428,173) — 27 B/row × 94 | 72,860 | **15.27** |

**2.01x**, against 2.20x predicted by the byte counts — the difference is the
per-row overhead the narrower band still pays 94 times.

**The dock strip cost 1.4x of that on its own, and finding out was the whole
lesson.** The first version folded the strip into the shared painted rect
whenever `dock_paint` had drawn, and the strip is *full width*, so every
window's owed rect became full width — including a window nowhere near the
dock. Measured: 21.28 ms rather than 15.27, a 41-byte row where 27 would do.
The marking pass one screen up had already got this right (`.mnodock` tests
each window's own `y+h`), and `wm_su_owed` follows it. **A term that is only
true of some windows does not belong in a rect they all share** — §26.3's
phantom drive-zone columns are the same mistake, and this is the second time
the answer has been "ask per window".

**What is NOT measured here, and should be next**: the 18.22 ms edge merge is
still done over the *whole* banked rect's rows even when the restore is a
94-row band, because `wm_su_edge` walks the buffer with one stride and bounding
it to the sub-rect's rows needs the two-level walk the plane skip implies. That
is the larger of the two remaining halves — bigger than what the blit has left
to give. **Set 31 is that, done.**

### Set 31 — the edge merge bounded to the rect being restored (SPEC.md §11.96.8)

Set 30 halved the blit and named what it had left alone: `wm_su_edge`, walking
the whole banked rect however small a strip the restore put back. Same machine
(cycle-accurate 5150/CGA), same session, same drag — the two Disk windows with
the front one dragged *away* so it genuinely uncovers — and the span is
`wm_su_try` → `gfx_restore`, which is `wm_su_ck` plus the merge:

| build | cycles | ms |
|---|---|---|
| the whole banked rect (136 rows, both columns) | 88,736 | 18.59 |
| bounded to the restored rect | 38,596 | **8.09** |

**2.30x**, and the blit beside it is unchanged at 15.27 ms, which is the check
that the two halves are independent. So one restore on this drag:

| | edge | blit | restore |
|---|---|---|---|
| before the round | 18.59 | 30.63 | 49.22 ms |
| after §11.96.6 | 18.59 | 15.27 | 33.86 ms |
| after §11.96.8 | **8.09** | 15.27 | **23.36 ms** |

**2.11x on the restore**, from two changes that are each provably the same
pixels.

**Two things in the 2.30x are not the row count**, and they are worth separating
because the second is free where the first is not. The rows walked drop 136 → 94
(a 1.45x), and the **left edge column is not merged at all**: this restore's
`x1` is 216, which is byte column 27 against the banked rect's 13, so there is
no overhang on that side to repair — those pixels are the window's own content,
which the pass did not paint, so the cache and the screen already agree. A
sub-rect in the middle of a window does *no* edge work whatever its height. That
is the half of the win the row bound alone would not have found, and it is why
the figure beats the 1.45x the geometry suggests.

**What is left of the restore is now the blit again**, 15.27 against 8.09 — and
there is no obvious next cut in it: it is `rep movsb` over exactly the bytes that
changed, at 27 bytes a row for 94 rows, and the per-row overhead is the row loop
the two renderers already share with the cursor.

### Set 32 — Paint's repaint with a PICTURE in it: blank was the very cheap case

Set 30's Paint figures were taken on a **blank** canvas, and that turns out to
be the wrong end of a 41x range. Same machine (cycle-accurate 5150/CGA), same
window, same cover-and-raise — the only difference is what is in the canvas. The
picture arrives by **double-clicking it**: `assoc.inc` ships `BMP` → PAINT, so
§54.5's launch-with-a-document path loads it with no file dialog to drive.

| canvas | runs/row | the canvas blit |
|---|---|---|
| blank (one run a row) | 1 | **211 ms** |
| a textured 492×133 16-colour BMP | 84.9 | **8,670 ms** |

**8.7 seconds**, and the model holds: 11,298 runs at ~0.77 ms each against
`pt_blit`'s own quoted ~0.5 ms per `gfx_hline`, the balance being the 4bpp source
scan. A picture with ~30 runs a row — an ordinary drawing rather than this
deliberately noisy one — lands near the three seconds the field reports.

**What this settles about `WF_SAVEU` for Paint, which is the opposite of the
guess on record.** "Paint is already drawing a blit, so a restore cannot be
faster" is false by two orders of magnitude, because the two are not the same
kind of operation: `gfx_blit4` costs **per RUN** (a `gfx_hline` per run, ~756 µs
of arriving each, §5.7), while `gfx_restore` is a flat `rep movsb` with no
per-run cost at all. Priced per byte off Set 30 and this set:

| | bytes moved | ms | per byte |
|---|---|---|---|
| `gfx_restore`, whole content (Set 30) | 5,576 | 30.63 | **5.5 µs** |
| `gfx_blit4`, textured canvas | ~35,600 | 8,670 | **244 µs** |

So a raise cache over Paint's content would be ~9 KB and ~50 ms on a 1bpp
adapter against 8,670 — **170x** — and ~36 KB and ~200 ms on VGA, 43x.

**The objection that survives is memory, and it is adapter-shaped.** `wm_su_kb`'s
`(bpr + 2) × rows × planes`: Paint's stock content on CGA/Hercules is ~9 KB, on
VGA ~36 KB, and a window grown to most of a 640×480 screen is ~150 KB. Paint
itself already holds canvas + undo + clipboard, ~127 KB at stock size — so on a
**256 KB machine** (heap ≈ 160 KB after the kernel) there is no room for the VGA
figure and only just room for the 1bpp one, and the claim being purgeable
(§50.6) means it is simply refused rather than damaging. The cache is therefore
a **640 KB-machine** optimisation on VGA and a marginal one on the machines this
project is calibrated against.

**Which is why drawing LESS of the canvas beats caching it, and composes with
everything.** The blit is linear in the runs it covers, so a repaint that owes
10% of the canvas costs ~10% of 8,670 ms — about **0.9 s** — at **no memory cost
at all**, on every adapter and every machine size. That is a bigger win than the
cache can give on a 256 KB machine and it stacks with the cache where the cache
fits. §11.90's unconditional white fill is what stands in front of it.

**And two smaller things this run recorded**: the repaint issues **two**
`wm_draw_win` passes for Paint's window (a 376 ms one that draws no canvas, then
the 8,670 ms one), which is the `[pt_apend]` deferred-resize path calling
`OSAPI_WM_FRONT` from inside `W_PAINT` and is worth its own look — **and that is
WRONG, see Set 40**: it is one pass, and the two hits were `pt_growbox` and
`wm_draw_win`'s own `.growbox`, the 376 ms in front of the first being the
palette, strip and divider this very table names; and the
~376 ms of palette, colour strip and divider is **small in area** — a narrow
left-hand column — so it is the part a cache would hold for about 1 KB.

### Set 33 — the white fill measured as a PICTURE, not as work (SPEC.md §11.90.1)

`wm_draw_win`'s fill is one `gfx_fill` — ~24 ms on a Disk-window-sized content —
so as *work* there is nothing here. As a *picture* it is PERFORMANCE.md Part 1's
double draw in its purest form, and the gap between its two layers is however
long `W_PAINT` takes. Sampled on a cycle-accurate 5150/CGA over a Paint raise
with Set 32's textured bitmap, an interior box of the canvas:

| | the sample box |
|---|---|
| the kernel fills (before) | **fully white from +350 ms to +2,967 ms — 2,617 ms** |
| `WF_OWNBG` (after) | **never blank**: peak whiteness 0.818, never uniform |

**The 2,617 ms is the BOX's figure and not the window's.** `pt_blit` works down
the canvas in bands, so a given row is white from the fill until the blit reaches
it: the box sampled here is in the upper middle and clears at 2.6 s, and the
bottom rows stay white for the whole 8,670 ms. Which is the shape of the defect —
the user watches their picture vanish and then wipe back down the screen.

**So the flag's value is not the 24 ms and quoting it that way would be
misleading.** It is that a repaint stops being visible as a blank-then-fill. The
same argument applies to every window whose `W_PAINT` is slow, and to none whose
`W_PAINT` is fast — which is why it is opt-in per window rather than a default
(§11.90.1).

Verified against a build that still gets the fill: **0 differing pixels** on CGA,
Hercules and VGA mode 12h across cover / raise / drag-across / re-raise
(`tools/ptcheck.py`). The gate uses a **textured** BMP deliberately: a blank
canvas is uniform white, which is precisely the colour the fill would have left,
so it is the one picture that cannot tell a kept promise from a broken one.

### Set 34 — Paint told which rect it owes (SPEC.md §11.90.2)

§11.90.1 stopped the kernel whitening Paint's content; this hands Paint
§11.96.6's accumulated damage so it blits only the part it owes. Measured on a
cycle-accurate 5150/CGA with Set 32's textured bitmap, the Disk window dragged
**off** Paint by (+70, +45) — a `wm_dmg_wins` pass, which is where a damage rect
exists at all:

| | Paint's `W_PAINT` | its canvas blit |
|---|---|---|
| the whole content | 9,024.5 ms | 8,669.8 ms |
| the damage rect | **7,114.1 ms** | **6,758.8 ms** |

**1.28x, and it is exactly proportional rather than surprising**: the damage
covered 73% of Paint's content width and the blit came in at 78% of the whole.
That is the feature working as designed — the blit is linear in the runs it
covers — and it is also the honest ceiling, because two things bound how small
the rect can get.

**A drag's damage is at least the MOVER's own rect.** `ui_drag` passes the union
of where the window was and where it is, so the saving is bounded by how much
smaller the moving window is than the one underneath: a Disk window 320 wide
dragged over a Paint 536 wide can only ever uncover so little of it, and 1.28x is
what that geometry allows. A small window dragged across a full-screen Paint
saves nearly everything; that is the same arithmetic, not a different feature.

**And a RAISE still repaints whole**, which is the bigger of the two gaps and the
obvious next step. `wm_raise` arms no damage rect — there is none to arm — so
`wm_damage` answers "whole", correctly. But what a raised window actually owes is
the part that **was covered**, which is computable: it is the complement of
§11.3's visible region taken *before* `wm_lift`. That is the case where "only 10%
of the canvas was visible" turns 8.7 s into ~0.9 s, and nothing in this set
reaches it. (**Set 35 built it and that last sentence is backwards** — 10%
visible is 90% covered and costs 7.8 s; the 0.9 s case is 10% *covered*.)

Verified with `tools/ptcheck.py` against a build that asks for nothing: **0
differing pixels** on CGA, Hercules and VGA mode 12h.

**A harness correction came out of it, and it is the second time this round that
the arrow has cost a run.** `subcheck`'s docstring argued the mouse arrow needed
no masking because both runs drive the pointer to the same derived coordinates.
True of its POSITION and silent about whether it is DRAWN — the cursor is erased
under the gfx lock and put back at the unlock (§7.1.4), so whether a capture
catches it depends on where `settle` lands relative to the last one. Measured:
**the same build captured twice differed by 45 arrow-shaped pixels** over a dock
tile. Both gates now blank a 16x16 box at the published cursor position. A gate
that fails at random is worse than no gate.

### Set 35 — MartyPC's floppy is given a platter

Not a field report: a change to the **instrument**, measured against the field
reports that already exist. `tools/martypc/patches/04-floppy-disk-timing.patch`
(`03-` when this was written; renumbered when elendilon's video-DIP patch landed),
docs/MARTYPC-DEBUG.md.

#### What was actually there

Upstream MartyPC models no floppy mechanism whatever, and the source says so
plainly once you look: `operation_read_data` calls the drive once and streams
the whole run to DMA as fast as the CPU turns; `command_seek_head` returns
`CommandComplete` in the same breath it is issued, so **a seek is free**;
`FloppyDriveMechanicalState`, with its `MotorSpinningUp` and `HeadSeeking`
arms, is an enum **nothing in the tree references**; and `media_geom`'s
sectors-per-track is hardcoded `0`, because until now no code wanted it. Set
11's 30x was not a calibration error. There was nothing to calibrate.

#### One mechanism, three rows

The patch models rotation (a head angle per drive), the MFM data rate (32 us a
byte at 250 kbit/s, so a 512-byte sector's data field is 16.384 ms), the
physical **interleave**, and a per-cylinder seek. Sets 14 and 22's three raw
`int 13h` rows then fall out of it together, which is the point — they were
never three facts:

| | field, IBM 5150 | model, 2:1 |
|---|---|---|
| one sector, re-read | 199.1 ms = **1.00 rev** | **199.106 ms = 1.00 rev** |
| a 9-sector track, one call | 384.5 ms = **1.92 rev** | **590.5 ms = 2.95 rev** |
| the same nine as nine calls | 2,004.8 ms = **10.02 rev** | **2,197.0 ms = 10.99 rev** |

**The middle row is a MISS and the right column is measured, not modelled.**
An earlier draft of this set put 1.86 rev there, which was this document's
author working the mechanism out on paper rather than reading it off the
machine — `sysbench` on MartyPC says 2.95. The single-sector row is exact to
four digits and the nine-call row is within 10%, so the per-sector arithmetic
is right; what the track row exposes is a **quantization boundary**. The model
finishes the ninth sector 28 ms before sector 1 comes round again, and its
turnaround — the real IBM ROM returning from `int 13h` and being re-entered,
which is cycle-accurate — does not fit in 28 ms, so every iteration waits a
further whole revolution. The 5150 catches that same sector. **So the media
model is sound and the miss is one of tens of milliseconds at a
revolution-sized cliff**, which is precisely the shape that makes the boot
17% slow rather than 3x, and precisely why a rotational model has to be
checked against a rotational measurement instead of against its own algebra.

**Also measured and NOT the cause**: the platter was being turned during a
transfer's byte-streaming phase as well as by the transfer's own per-sector
walk, which double-counts. Fixing it is right — the operation owns the angle —
and it is worth **2%**, not the miss above. It measured as nothing against a
7,980 ms boot transfer total and is recorded here so the next reader does not
re-derive it as the explanation.

**The interleave cannot come from the image and does not.** A raw sector image
records the LOGICAL order and says nothing about the platter, so 2:1 is a
statement in the machine config — the 5150's media is 2:1 (Set 14), the Compaq
Portable III's ~1:1 (Set 19), and that difference is exactly why the Compaq
streams a track in 1.24 revolutions where the 5150 needs 1.92.

> **The first sentence stands and the rest is wrong — Set 37.** Interleave is
> indeed a config statement and cannot be derived from an image. Neither
> machine's media is interleaved: both are 1:1, the config sets none, and what
> separates 1.24 revolutions from 1.92 is per-call BIOS overhead. Read this
> whole set with that substituted — including the `model, 2:1` column above,
> whose 2.95-revolution miss is the doubled turnaround and not a quantization
> cliff the model was entitled to.

#### Measured end to end

`boot ticks`, 360KB image, os8088's own counter:

| machine | media | before | after | field (Set 22) |
|---|---|---|---|---|
| `os8088_5150_cga_gla` (GLaBIOS) | 1:1 | 41 (2.25 s) | **130 (7.14 s)** | — |
| `os8088_5150_cga` (IBM ROM) | 2:1 | — | **210 (11.53 s)** | 180 (9.886 s) |
| `os8088_5150_herc` (IBM ROM) | 2:1 | — | **211 (11.59 s)** | **180 (9.886 s)** |

The last row is the like-for-like — the field's 180 is a Hercules boot off
`herc.img` — and **4.4x fast becomes 1.17x SLOW**. The sign matters as much as
the magnitude: the old error flattered every disk decision taken on it, and
this one costs a little instead.

**The residual is attributed rather than assumed.** Instrumenting the charge
puts the boot at **7.2 s of rotation and transfer over 161 sector waits against
0.62 s over 14 seeks** — so the seek model, the one part no row in this
document pins, is 8% of disk time and cannot be the 17%. The sector average is
44.9 ms against the 42.7 that this document's own 384 ms/9 implies, and that
5% is physical: a short run pays a full rotational latency for its first
sector and only a whole-track read amortises one. The rest is not disk.
One caveat on the comparison: the field boots a `make field` disk and this
boots the shipped one (167 sectors), so `tools/fieldsize.py`'s rung check is
what licenses putting the two side by side.

#### Two things it found

**GLaBIOS abandons a floppy operation after ~250 ms.** Measured three times
running, at 250.2 / 245.8 / 245.8 ms, after which it resets the controller and
the boot sector prints `os8088: disk error` — status **80**, read off the
screen with `make BOOTDIAG=1`, which is precisely the boot this knob exists
for. A 6-sector 2:1 run takes 305 ms, so under that BIOS it can never
complete. It is the BIOS and not the model, on three counts: the FDC presents
a correctly BUSY status register throughout, **seeks of 329 ms complete fine in
the same boot**, and **the IBM ROM boots the identical 2:1 configuration** —
which was a prediction when this was written and is now the 211 above. So 2:1
goes only on the `ibm5150_82_v4` machines, and the GLaBIOS twins keep 1:1.

> **The BIOS limit is real and the exception it forced is gone — Set 37.** No
> machine carries 2:1 any more, because the field machine's media never did,
> so no config has to distinguish the two ROMs and `os8088_5150_cga_gla`
> boots `combo.img` in 175 ticks. The ~250 ms abandon is still a fact about
> GLaBIOS and still the reason not to take a disk number off it.

**And a run must be paced per SECTOR, not charged as one lump.** The first
version delayed a whole multi-sector run in one silent block, which is not what
a drive does — a controller starts DRQing as the first sector arrives and
pauses only over the inter-sector gaps. Per-sector pacing reads out of the
trace as `82.8 ms, 44.4, 44.4, …`: the first sector's rotational latency, then
two slots of 2:1 gap apiece. It did not rescue GLaBIOS, whose limit is on the
whole operation rather than on silence, but it is the honest model and the
totals are identical by construction.

#### What it does not touch

The patch changes what a disk **costs** and never what it **says**. §18.91's
`AL` bug is a claim about what a real ROM returns; an emulator returns what its
author believed, and this one still does. Correctness questions — short reads,
`int 1Eh`'s EOT, BIOS interrupt stack depth — remain the 5150's, exactly as
before. Motor spin-up, the PCjr PIO paths and Format Track are unmodelled or
uncalibrated, and the seek figures are the BIOS's own SPECIFY request rather
than anything measured: the field's three rows all read one track and never
seek, so nothing in Part 9 pins them. **Hard disks are untouched.**

#### The `AL` bug reproduces here, and that was not expected

Set 35 shipped saying MartyPC "will still not catch a disk CORRECTNESS bug".
**That is wrong, and it is wrong in the useful direction.** With the IBM ROM in
`tools/martypc/roms/`, MartyPC does not *model* the BIOS — it **executes** it,
so a bug in IBM's `int 13h` is present by construction. Same image, same
machine (`os8088_5150_herc`), shipped kernel against `make DISKAL=1`:

| | shipped (trusts `CF`) | `DISKAL=1` (trusts `AL`) |
|---|---|---|
| int 13h-level reads | 23 | **177** |
| sectors moved | 177 | **846** — 4.8x |
| **longest run** | **9** | **9** |
| `boot_ticks` | 211 | **1152** |

`longest_run` = 9 in both is Set 16's finding restated by the emulator: the
kernel asks for nine sectors, is given nine, and asks again. The 4.8x traffic
is Set 15's 4.6x. **QEMU missed this because SeaBIOS is a different BIOS**, not
because emulation cannot see it — and that distinction was never drawn here,
which is why MartyPC inherited a blindness it does not have.

The counters are the CONTROLLER's, read over the debug socket
(`os88marty.py`'s `m.disk()`), so the guest needs no `DISKCNT=1` kernel and no
test package: **§18.94's block, on a shipped image, from outside**.

So the boundary is now between the ROM and the chip. **BIOS-level** behaviour —
what `int 13h` returns, `int 1Eh`'s EOT, the ROM's own arithmetic — is
reproduced because it is IBM's code. **Controller-level** behaviour — what a
real 765 puts in ST1 on a CRC error, whether a real drive ever returns short —
remains the emulator author's belief, and remains the 5150's question.

#### Set 35.1 — the seek and the spin-up get an instrument

The model's two untested numbers now have `sysbench` blocks
(docs/TESTING.md, `make field`). Run here on `cga.img` under the IBM ROM with
2:1 media, so this is **what the model predicts, not what the drive does** —
the 5150 is what turns it into a measurement.

**The seek rows come out as a staircase, which is the design and not an
accident.** A read ends at a fixed angular position, so the seek hides inside
the wait for sector 1 to come round and the rows can only step in whole
revolutions:

| row | us/op (a pair of reads) | revolutions per read |
|---|---:|---:|
| `seek 0 cyl (baseline)` | 398,211 | **1.00** |
| `seek 1 cyl, pair` | 398,211 | 1.00 |
| `seek 5 cyl, pair` | 398,211 | 1.00 |
| `seek 10 cyl, pair` | 398,211 | 1.00 |
| `seek 20 cyl, pair` | 810,154 | **2.03** |
| `seek 39 cyl, pair` | 1,194,634 | **2.99** |

The baseline is 1.00 revolution per read to three digits, which is what says
the instrument is sound — it is the same check `int 13h 1 sector` has always
been. **The break falls between 10 and 20 cylinders**, and that is the whole
datum: at the DPT's `00CF` (SRT = 12, so 8 ms a cylinder at 250 kbit/s) plus a
25 ms settle, a 20-cylinder seek is 185 ms against the ~184 ms of rotational
slack a 1-sector read leaves — so it steps up *just*, exactly where the model
says. **If the 5150's break falls lower, its drive steps slower than the BIOS
asked for; if there is no break at all, it steps faster than 5.1 ms and the
model is overcharging every seek in the system.**

**The spin-up rows read INVERTED here, and that is the correct answer for this
machine**: cold 164,777 us against warm 219,702, with `motor status 40:3F` =
`0000` so the drive really had stopped. MartyPC models no spin-up — the header
says a cold row not slower than the warm one means none is being paid — and
the two rows are one revolution apart in either direction because N = 1 and
each catches the platter wherever it was. On the 5150 the cold row should be
**about a second longer**, that being what the DPT asks for, and the size of
the gap over and above it is the platter itself.

**One thing to check on the iron while it is running.** MartyPC's ROM reports
`DPT motor start /8 s` = **4** — half a second — where Set 14 read **8** on the
field machine off the same 27 OCT 82 ROM. One of those two readings is wrong
and the report prints the byte, so the next field run settles it.

#### ...and the report is one round from its own ceiling

`SYSBENCH.TXT` is **337 rows of 380 and 14,806 arena bytes of 16,000** — 7%
spare. That ceiling has never been visible: `bl_full` says a report
*truncated* and nothing said how close one that did not had come, which is why
the ROW ceiling was raised three times after the fact and the arena's never
was. Both figures are printed in the trailer now.

**And it cannot simply be raised.** `bl_out` is `BL_ARENA`-sized too, so every
byte costs two, and sysbench's image plus bss is already within ~5KB of the
64KB **segment** a package gets (SPEC.md §20.1): +3,456 overflows it, and the
failure is a wall of `word data exceeds bounds` at unrelated lines rather than
anything naming the constant. The next round wants a row dropped or the suite
split, and now that decision can be taken from the report rather than from a
truncated one coming back off a machine nobody can re-run cheaply.

### Set 36 — the seek measured on the iron: the model's one guess was right

One `sysbench` run on the IBM 5150 off `combo.img` built `DISKCNT=1`, Hercules
primary. The build is identified rather than assumed — `kernel span KB 97` and
`kernel image KB 83` match that build's 97.0 / 83.4, `free on it, KB 45` is its
309-of-354 clusters, and the MM58167 answering at tier 2 with
`adapters avail 0006` is docs/FIELD-MACHINES.md's machine and no emulator.

#### The head step, which nothing had ever measured

Set 35's model took its step rate from what the BIOS asks for through SPECIFY
(`00CF` → SRT 12 → 8 ms a cylinder at 250 kbit/s) and its settle from the DPT,
both on trust, because every raw row in this document reads one track and
never moves the head. In revolutions per read, against the model:

| cylinders | **5150** | MartyPC |
|---:|---:|---:|
| 0 (baseline) | **1.000** | 1.000 |
| 1 | **1.000** | 1.000 |
| 5 | **1.034** | 1.000 |
| 10 | **1.000** | 1.000 |
| 20 | **2.000** | 2.034 |
| 39 | **2.138** | 3.000 |

**The break falls between 10 and 20 cylinders, which is exactly where the
model puts it** — 10 cylinders is 105 ms and fits inside the ~184 ms of
rotational slack a 1-sector read leaves; 20 is 185 ms and does not. Read as a
slope instead, 10 → 39 cylinders costs the 5150 **7.81 ms a cylinder** against
the model's 8.00. **The guess was right to 2%, and it is no longer a guess.**

#### ...and the same rows locate what is still wrong

> **Set 37 answers this whole subsection, and the answer is not the one it
> guesses at below.** The turnaround was not too long: 2:1 media the machine
> never had was making every read too long, and the drive was being charged a
> 25 ms head settle the BIOS already spends in software. With both fixed,
> every row of the ladder below is exact except the 39-cylinder one, which is
> one tick short.

MartyPC's 39-cylinder row is **3.000 revolutions against the field's 2.138**,
a slope of 13.7 ms a cylinder against 7.8 — and the model is *not* using 13.7,
it is using 8. What that gap measures is the same defect Set 35.1 found in the
track row: **MartyPC's per-call turnaround is too long, so a read that should
just catch sector 1 misses it and waits another whole revolution.** The field's
2.138 is not a whole number and MartyPC's 3.000 is, which is the tell. One
cause, two symptoms, and it is the residual behind the boot as well.

**Boot, on the identical disk**: 205 ticks on the 5150, **222 on MartyPC** —
**1.08x**, against the 1.17x Set 35 reported off a different pair of images.

#### The spin-up rows did not answer, and say so

`1 sector, motor COLD` is **164,777 us** and warm **219,702** — cold *faster*,
with `motor status 40:3F` reading `0000`. Both are exact tick multiples (3 and
4), so at N = 1 the ±1 revolution of rotational luck is larger than the effect.

But the row is not merely noisy, it is **impossible as stated**: a read that
completes in 165 ms is **under one revolution**, and no platter starts from
rest inside one. So either the drive was still turning, or no motor-start wait
is paid on this path — and `0040:003F` is the **BIOS's belief**, not the
hardware: the DOR at 0x3F2 is write-only, so a package cannot confirm it.
That is a limit of the instrument and is recorded rather than worked around.
Raising N does not fix it either: the wait for the motor to stop would land
inside the timed region.

#### Two smaller things this settles

**`DPT motor start /8 s` is 8** on the machine — one full second, as Set 14
read. MartyPC's ROM reports **4**. Same 27 OCT 82 dump, different answer, so
something between the ROM and `int 1Eh` differs in the emulator; it is worth
knowing and it is not what the boot gap is made of.

**One 16 KB `FILE_READ` is 27 sectors in 3 `int 13h`**, runs of 9 at
contiguous LBAs 594/603/612, **no mounts and no resets** — 5 of the file's 32
sectors served from §18.95's cache, and the repeat read identical. And the
report came back at **343 rows of 380 and 14,867 arena bytes of 16,000**, 7%
spare, which is Set 35's warning arriving on the machine it was about.

### Set 37 — the media was never 2:1, and the whole floppy block now matches

Set 36 left one thing wrong and named it: MartyPC's 39-cylinder seek row read
**3.000 revolutions against the field's 2.138**, and its track read 590 ms
against 384/398 — "one cause, two symptoms, and it is the residual behind the
boot as well." That diagnosis was right about there being one cause and wrong
about what it was. The cause is **this document**, and specifically Set 14's
conclusion that the calibration 5150's 360KB media is 2:1 interleaved.

It is not. It is 1:1, like every other 360KB floppy an IBM machine ever
formatted.

#### How a wrong inference got made, and what would have caught it

Set 14 measured a whole 9-sector track in one `int 13h` at **398,211 µs** and
read it as revolutions: a 1:1 track is one revolution (200 ms) and this was
two, therefore the platter costs two turns to give up nine sectors, therefore
2:1. The corroborating number was **11,570 bytes/second**, against 11,520 for
2:1 by arithmetic — a 0.4% match, and `tests/sysbench` prints that arithmetic
in its own header.

Both numbers are real. Both have a second explanation that fits them exactly,
and it is the right one: **1:1 media plus a quarter-revolution of BIOS overhead
per `int 13h` call**, which locks the measured total to the same two
revolutions and the same 11,520 B/s. The B/s figure cannot discriminate at all
— it is bytes divided by the *whole call*, overhead included — and the
revolution count only discriminates if you already know the overhead is small.

Nobody had measured the overhead. It is not small.

#### The overhead, measured in the ROM

Sampling the guest's `CS:IP` through the debug server across the disk phase of
a boot puts **8.8%** of it at `F000:EEBF`, which disassembles to

```
EEB8  mov cx, 0x226        ; 550
EEBB  or  ah, ah
EEBD  jz  eec5
EEBF  loop eebf            ; <- 8.8% of the whole disk phase
EEC1  dec ah
EEC3  jmp short eeb8
```

— IBM's millisecond delay, called with `AH` = the diskette parameter table's
head-settle byte. The ROM's own table (`F000:EFC7`,
`CF 02 25 02 08 2A FF 50 F6 19 04`) asks for **25 ms**, and the field machine's
`sysbench` reports the same 25. What it actually costs is another matter: 550
iterations of `LOOP $` on an 8088 is ~18 clocks apiece, so IBM's "1 ms" is
**2.1 ms** and the settle is **52.5 ms**. Tracing every FDC port access
confirms it end to end — the gap between the last SENSE INTERRUPT result byte
and the first READ DATA command byte is **52.48 ms with three MSR reads in
it**, so the guest is not waiting on the controller, it is counting.

That is a quarter of a revolution, once per `int 13h`, on both machines. It is
the missing turn.

#### What the correction is worth

`interleave` set to 1 on every machine, and the drive's own `FDC_HEAD_SETTLE_US`
taken to **zero** — the settle is the BIOS's and modelling it a second time in
the drive is the same wait counted twice, which at 39 cylinders costs a whole
extra revolution. Then `tests/sysbench` on `os8088_5150_herc`, against the
field machine's own report from the identical `combo.img`:

| row | 5150 | MartyPC | quanta |
|---|---:|---:|---:|
| `int 13h 1 sector` | 199,106 | **199,106** | **0** |
| `int 13h track, 1 call` | 398,211 | 384,480 | −1 |
| `int 13h track, 9 calls` | 1,991,057 | 2,004,789 | +1 |
| `seek 0 cyl (baseline)` | 398,211 | **398,211** | **0** |
| `seek 1 cyl, pair` | 398,211 | **398,211** | **0** |
| `seek 5 cyl, pair` | 411,943 | **411,943** | **0** |
| `seek 10 cyl, pair` | 398,211 | **398,211** | **0** |
| `seek 20 cyl, pair` | 796,423 | **796,423** | **0** |
| `seek 39 cyl, pair` | 851,349 | 796,423 | −4 |
| `read 16K, warm` | 1,208,366 | 1,153,440 | −4 |
| `read 16K, cold motor` | 1,592,846 | 1,647,772 | +4 |
| `WRITE, create` | 4,668,686 | **4,668,686** | **0** |
| `WRITE, append in chunks` | 11,259,773 | 11,040,070 | −16 |

A quantum is 13,731 µs — one tick over the row's four iterations, which is
`bl_run`'s resolution and not a property of either machine. **Seven of the
thirteen are exact**, including every short seek and the one-sector read, and
two more are one quantum out. The `track, 1 call` row is the sharpest case of
what a quantum means: 384,480 is not merely close to the field's 398,211, it
is *the number the field itself reported* for that row in Set 14 — the row sits
on a quantum boundary and flips between runs.

The 39-cylinder row is the one still worth a sentence: 4 quanta (one tick)
short, where before this change it was **26 quanta** (1,208,366 against
851,349). The residual is the step-rate slope, which Set 36 put at 7.81 ms a
cylinder against the model's 8.00 and which nothing here has re-measured.

Every CPU, memory, PIT and scheduler row in the same report matches to a count
or two, as they already did.

#### Boot, and what the honest sequence was

`boot ticks` on `combo.img`, `os8088_5150_herc`, against the field's 205:

| | ticks | vs field |
|---|---:|---:|
| Set 36's build (2:1, frozen platter) | 222 | 1.08x |
| the platter turning, still 2:1 | **260** | 1.27x |
| 1:1 and no doubled settle | **188** | **0.92x** |

The middle row is the one to keep. Set 36's 222 was a *frozen* platter — the
angle only advanced during a transfer, so every read found its sector exactly
where the last one left it and no turnaround ever cost a revolution. Making
the rotation unconditional is plainly correct and made the headline number
**worse**, which is what said the error was somewhere the frozen platter had
been flattering. Two more revolutions per track is not a number a plausible
fix moves by 10%; it is a whole missing turn, and that is what sent this at
the media.

#### The rule this is an instance of

**A ratio that matches an arithmetic prediction is not evidence unless
everything the ratio divides by has been measured.** 11,570 against 11,520 is
a 0.4% agreement and it identified the wrong mechanism, because the divisor
was the whole `int 13h` call and a quarter of that call is a BIOS delay loop
nobody had looked at. The instrument that settled it was not a better
benchmark — it was `CS:IP` sampling plus the ROM's own disassembly, which is
to say: go and find out what the machine is *doing*, not just how long it took.

#### Two things it retired

**The GLaBIOS exception.** Set 35 gave 2:1 media only to the IBM-ROM machines,
because GLaBIOS abandons a floppy operation after ~250 ms and a 2:1 track read
takes up to 400. At 1:1 no machine needs the exception and every config here
carries the same disks; `os8088_5150_cga_gla` boots `combo.img` in 175 ticks.

**"MartyPC's floppy is 1.17x slow."** It is 0.92x, and the sign changed twice
on the way — 4.4x fast before Set 35, 1.17x slow after it, 1.27x slow once the
platter really turned, and 0.92x now. `make marty` remains the place to *find*
a disk regression and the 5150 remains the place a number lands, but the gap
between them on this bench is now one measurement quantum on most rows.

### Set 38 — no machine here is "the calibration", and the drive proves it

Set 37's whole result was taken on `os8088_5150_herc`, which invites two wrong
readings: that Hercules is where the disk measures right, and that the other
seventeen machines are uncalibrated. Both were worth checking rather than
answering, because the drive being modelled is one **Tandon TM100-2** and its
behaviour cannot depend on which video card is in the slot next to it.

#### The controller's own traffic, all eighteen machines, one image

`combo.img` booted on every machine, with `m.disk()` read from outside the
guest afterwards — the FDC's own counters, so no guest code is involved and
the workload is identical by construction. Grouped by traffic:

| reads | sectors | run | seeks/cyl | seek ms | resets | machines |
|---:|---:|---:|---:|---:|---:|---|
| 24 | 186 | 9 | 29/54 | 432.0 | 3 | **`_cga`, `_herc`, `_both`, `_sbonly`, `_cga_720b`, `_cga_4fdd`** |
| 26 | 199 | 9 | 31/80 | 640.0 | 3 | `_sb`, `_sb_256k` (a sound driver loads — §51.3.1) |
| 31 | 195 | 9 | 36/80 | 640.0 | 3 | `_sb_128k` (128 KB: the OS does more disk work) |
| 26 | 201 | 9 | 26/241 | 1928.0 | 6 | `_cga_gla`, `_both_gla`, `_xt_hdd` |
| 25 | 195 | 9 | 25/241 | 1928.0 | 5 | `_herc_gla`, `_both_gla_mono`, `_xt_vga` |
| 26 | 201 | 9 | 21/131 | 1048.0 | 6 | `_cga_1fd` (one drive: no B: to probe) |
| 27/28 | 208/214 | 9 | 27–28/267 | 2136.0 | 5/6 | `_xt_vga_sb`, `_xt_hdd_sb` |

**Six machines are bit-identical** — the same reads, the same sectors, the
same longest run, the same seeks, the same cylinders crossed and the same
432.0 ms of seek — across CGA, Hercules, a two-card machine, a Sound Blaster
with no OPL, a 720 KB drive as B: and a four-drive machine. **The Tandon does
not change across 5150s**, and that is now a measurement rather than an
argument from the source.

What *does* move inside that group is `transfer_ms`: 4,111.0 on CGA, 4,243.0
on Hercules, 4,228.2 on the four-drive machine — a **3.2%** spread. That is
rotational latency, not the drive: the counts are identical, so the same
sectors were asked for in the same order, and what differed is how long the
guest took between calls. A Hercules boot draws a different splash.

Everything below the first group is the **BIOS or the OS**, never the drive.
The `_sb` rows are §51.3.1's boot probe finding an OPL and loading a driver;
`_sb_128k` is os8088 working harder in 128 KB; the GLaBIOS rows are a
different ROM entirely, and the 241 cylinders against 54 is the tell.

#### `tests/sysbench`'s raw block, five machines against the iron

Same `combo.img`, driven through the UI, `int 13h` called with no kernel code
in the way:

| row | 5150 | `_cga` | `_herc` | `_sb_256k` | `_cga_gla` | `_xt_hdd` |
|---|---:|---:|---:|---:|---:|---:|
| `int 13h 1 sector` | 199,106 | **199,106** | **199,106** | 205,971 | **199,106** | **199,106** |
| `track, 1 call` | 398,211 | **398,211** | 384,480 | 384,480 | 247,166 | 247,166 |
| `track, 9 calls` | 1,991,057 | **1,991,057** | 2,004,789 | 2,004,789 | 247,166 | 247,166 |
| `seek 0 cyl` | 398,211 | **398,211** | **398,211** | **398,211** | **398,211** | **398,211** |
| `seek 1 cyl` | 398,211 | **398,211** | **398,211** | **398,211** | **398,211** | **398,211** |
| `seek 5 cyl` | 411,943 | 398,211 | **411,943** | 398,211 | 398,211 | 398,211 |
| `seek 10 cyl` | 398,211 | **398,211** | **398,211** | **398,211** | **398,211** | **398,211** |
| `seek 20 cyl` | 796,423 | 810,154 | **796,423** | **796,423** | 810,154 | 810,154 |
| `seek 39 cyl` | 851,349 | 796,423 | 796,423 | 796,423 | 796,423 | 796,423 |
| `1 sector, motor COLD` | 164,777 | **164,777** | **164,777** | **164,777** | 219,703 | **164,777** |
| `1 sector, motor warm` | 219,703 | **219,703** | 164,777 | **219,703** | 164,777 | 164,777 |
| | **exact / within 1q** | **8 / 10** | 7 / 9 | 6 / 10 | 4 / 6 | 5 / 7 |

**Which rows land exactly SHUFFLES between the three IBM machines**, and that
is the finding. `_cga` nails both track rows and misses `seek 5`; `_herc`
nails `seek 5` and misses both track rows; `_sb_256k` nails `seek 20` and
`motor warm`. Nine or ten of eleven are inside one quantum on all three. So
**the residual is phase, not model** — a row sitting on a 13,731 µs boundary
falls whichever side the guest's turnaround happens to put it — and *no single
machine is the calibration*. The right unit is the **class**: an IBM-ROM 5150,
whichever card is in it.

`_sb_256k`'s `int 13h 1 sector` at 205,971 is the one row outside that: half a
quantum, and it is the only machine of the three with a **sound driver
loaded**, so its worker is taking turns the others' do not.

#### GLaBIOS is a different instrument, and by how much is now known

`track, 1 call` is **247,166 against the IBM ROM's 398,211 — 1.61x lighter**,
and `track, 9 calls` is *the same 247,166*, which looks impossible and is not.
On 1:1 media sector *n+1* follows sector *n* immediately, so nine separate
one-sector reads cost one revolution **if the BIOS can turn a call around
inside one sector time (22 ms)**. GLaBIOS can; the IBM ROM cannot, because its
head-settle loop alone is 52.5 ms (Set 37) — so it misses every time and pays
ten revolutions for the same nine sectors. One number, both rows, and it is
the same 52.5 ms that made the media look 2:1.

That has a consequence for two figures already in SPEC.md. §18.95.3's cache
table was measured on `os8088_5150_cga_gla` and §52.10's install counts on
`os8088_xt_hdd` — **both GLaBIOS**. Their *call counts* are unaffected, a call
being a call on any BIOS, and both sections say counts are the claim. A
*timing* taken on either would be 1.61x light, and neither section says that.
Both now do.

#### So: should the other machines be "calibrated"?

**No, and there is nothing to calibrate.** The drive is one model in
`marty_core` and the first table proves it produces identical traffic wherever
it is installed; there is no per-machine constant to tune and adding one would
be inventing a difference the hardware does not have. What was actually
missing was a **statement of which machines a disk number may come off**, and
that is now in docs/MARTYPC-DEBUG.md's machine table:

- **IBM-ROM 5150** (`_cga`, `_herc`, `_both`, `_sb`, `_sbonly`, `_sb_128k`,
  `_sb_256k`, `_cga_720b`, `_cga_4fdd`) — field-comparable, ±1 quantum.
- **GLaBIOS** (`_cga_gla`, `_herc_gla`, `_both_gla`, `_both_gla_mono`,
  `_cga_1fd`, `_xt_vga`, `_xt_vga_sb`, `_xt_hdd`, `_xt_hdd_sb`) — the drive is
  the same and the BIOS is not. Counts yes, seconds no.

The 5150 is still where a number LANDS. What changed is that the gap is one
measurement quantum on a machine of the right class, and 1.61x on a machine of
the wrong one — and the second of those was invisible until it was measured.

### Set 39 — the parallel cable measured, and 90% of it was arriving

`tests/lptlink` between the **5150** (Hercules GB101's LPT at **03BC**) and
the DOS machine (**DIO-500 at 0378**), a LapLink nibble cable between them.
Two runs, the master role swapped between them. **docs/NET-PLAN.md step 1.**

**Neither end is os8088**, which is the point: no kernel byte is involved, so
a result here is a fact about the cable and the protocol and cannot be
anything else.

#### The link, and what it does

16,128 bytes each way per run, **zero errors in all four transfers**, and the
scan found the two machines at different addresses without being told either.

| | first build | after the fix |
|---|---|---|
| 5150 **sending** | 1,536 B/s | **3,741 B/s** |
| 5150 **receiving** | 1,595 B/s | **3,538 B/s** |
| per nibble, sending | 325 us | **133.7 us** |

**2.44x**, and the asymmetry is the confirming detail rather than noise: which
direction is faster follows **the 5150's role, not who is master**. Swapping
the master swapped the labels and left the numbers where they were, and the
receiving direction reproduced to the *exact tick* in both runs — 83, and 83.
`lp_rnib` does about six instructions more than `lp_snib` (three `shr`s, the
mask, the `[lp_lastop]` store, the reversal branch); isolated that is ~12.7 us
at Part 2's instruction-byte floor, measured it is **7.7**, and the difference
is that the two ends run concurrently so some of the receiver's extra work
happens while the sender is already waiting.

#### Why the first build was 4.2x slower than its own model

The model said 77 us a nibble and the machine said 325. **The model was not
wrong about the wire — it was wrong about everything around it.** Counted
against this Part's own constants (4.34 clocks per instruction byte, 4.77 MHz)
the prediction is **360 us, within 11% of the measurement**:

| per nibble, on the 5150 | us |
|---|---|
| `lp_wfar`, four calls | **289** |
| …of which `ticks()`, four reads | **94** |
| `lp_rnib`'s own body and frame | 71 |
| **the wire** — two `in`s and three `out`s | **~30** |

**Nine tenths of it was arriving, not working**, and the single worst item was
reading the BIOS tick counter to build a deadline **that is almost never
reached**. That deadline is *correct* — it had just replaced a poll-count
timeout that broke between machines of different speeds — and what was wrong
was paying for it on the fast path.

This is §5.7's finding in a new place, and the third time this document has
recorded it: one `gfx_pixel` was 196 guest instructions of generic rect
machinery across eleven routines with no hot spot anywhere; a walk step's
arrival, a one-pixel block and a marginal pixel were three quantities the tree
had as one; and now a nibble's transport is 30 us of wire under 300 us of
call frames. **When a measurement comes in several times its model, count the
arriving before re-examining the work.**

Four changes, none touching the protocol: the deadline is **lazy** (`ticks` is
not read until a whole `LP_SPIN` of polls has gone unanswered, which on a
working link never happens), the wait is a **macro rather than a called proc**,
the poll body is 9 bytes rather than 16 (the level is a branch, not a compare
against a variable), and `DX` walks between the two ports with `inc`/`dec`
instead of being reloaded from memory.

#### What is left, and why it was not spent

Predicted after the fix was 93 us; measured is 134. The residual is the
layering *above* the nibble — `lp_sbyte`'s frame and the benchmark's own byte
loop, ~28 us a nibble by the same count — plus the far end's response latency.

**The remaining lever is the handshake itself**: two phases instead of four
halves both the waits and the `out`s per nibble, and NET-PLAN §1.1.1 records
that it is safe *only* with the turnaround guard that four phases made
unnecessary. That is worth perhaps another 1.5x and it is **not** being spent
here, because the transport's home is a driver, where the code above it is
real file I/O rather than a benchmark loop. Tune it there, against a file
operation, or the harness gets optimised instead of the feature.

#### For the record, against this machine's own disk

| | bytes/second |
|---|---|
| floppy `FILE_READ`, warm (Set 24) | 21,307 |
| **the cable** | **3,741** |
| ST-225 (Set 24) | 74,553 |

**The cable is 5.7x slower than the machine's own floppy** — and the case for
it was never the rate. A 360 KB image crosses it in **99 seconds**; the same
image reaches this machine any other way via the seven-step path in
docs/FIELD-MACHINES.md.

### Set 40 — the network drive works, and a third of it is turnaround

**docs/NET-PLAN.md step 2, on the iron**: the 5150 (GB101's LPT at **03BC**)
against the DOS machine (DIO-500 at **0378**), NET.DRV loaded and
`OS88NET.COM` serving a 720KB image. The Control Panel reads **Linked, 1440
sectors** — the image's exact size — a Disk window lists `APPS`, `MEDIA` and
`NETTEST.TXT`, and **double-clicking the text file launched Note Pad and
opened it**: the file association resolved, the package loaded and the
document arrived, all across the cable. **Everything above `dsk_xfer` worked
unchanged**, which was the whole bet of doing block mode first.

**The document open took about ten seconds**, and the first decomposition of
it published here was WRONG — it is corrected below, and the correction is
worth as much as the number.

**`NOTEPAD.O88` DID NOT CROSS THE CABLE.** It was charged to the wire at 32
sectors on the reasoning that the network volume's own `APPS` folder held a
copy, so §54.4.2's rung 3 would find it there. It never got that far:
`disk_mount` calls `asc_use` on every FULL mount, which reads that volume's
`ASSOC.DAT` and stamps `assoc_drv`/`assoc_clus` for **every program it
names** — so booting from the combo disk seeded Note Pad's hint to `A:\APPS`
before the cable was touched at all, and **rung 1 is that hint**, tried first.
The 15,904 bytes came off the floppy at 21,307 B/s, in about 0.75 s. The
machine's owner spotted it from the drive light; the code says the same.

So what actually crossed for that open is the **document** (809 bytes, 2
sectors) and `assoc_back`'s **quiet mount** of the network volume — §18.9's
BPB + FAT window + cwd, no listing and no harvest, so ~11 sectors. Call it 13:

| | sectors | cost |
|---|---:|---:|
| **data**, at Set 39's 3,741 B/s | ~13 | **~1.8 s** |
| **turnaround**, 2 x 54.9 ms per SECTOR | ~13 | **~1.4 s** |
| `NOTEPAD.O88` off the FLOPPY | 32 | ~0.75 s |
| the floppy's own mounts, and the volume switched twice | | the rest |

**The turnaround conclusion survives the correction and the size of it does
not.** `lp_turn` spends one whole system tick per direction reversal —
deliberately, and Set 39's own header says why: a spin count cannot be made
to hold between a 5150 and an unknown far end, and a reversal was expected to
be *"a handful per run"*. In block mode it is **two per sector**, because
`net_blk` sent a count of **1** every time even though the protocol carries a
count byte and `dsk_xfer` hands it an already-coalesced run. Batching is worth
**~1.4 s of this open**, not the 3.7 s first claimed — and much more on
anything that actually streams off the wire, which this did not.

That is still SPEC.md §18.91's floppy batching in a new place: **a cost model
built for streaming, met by a caller that does not stream.**

**And a package launched off the wire remains UNTESTED**, which is the other
thing the correction says. The association hint makes a program on the
network volume lose to the boot disk's copy of it — which is *right*, it is
5.7x faster — so opening a document can never be the test for it. Launching
`MINES.O88` from the network Disk window is, because that is `loader_run` on
a row of that window rather than `assoc_locate`.

**The measurement that would have settled it in one glance**: `OS88NET.COM`
prints a dot per sector served, and 34 dots against 13 is not a subtle
difference. Read the DOS side's screen, not the arithmetic.

#### The write path, checked from OUTSIDE os8088

A file copied onto the network volume and the image carried back to the host.
This is the check that cannot be made from inside: the writer and the reader
are the same FAT12 code, so **both halves agreeing on the same wrong thing**
is exactly the failure a self-consistent volume hides (docs/FIELD-NOTES.md 4
is what that costs).

| check | result |
|---|---|
| `os88disk.py --verify` | **OK** — FAT12, 713 clusters, 6 files, 24 in use |
| sectors changed against the image that was sent out | **4** |
| …which ones | FAT1, FAT2, the root directory sector, data cluster 25 |
| the volume walked from the host, no os8088 code involved | clean; every pre-existing file unmoved, same clusters |
| `BENCHSML.DAT` against the source it was copied from | **byte-identical**, md5 `ad4c06ff458c1c57df65d7fe63df735a` |

Four sectors and no others: both FAT copies updated **symmetrically**, one
directory entry, one data cluster, and nothing written anywhere else on a
713-cluster volume. The commit ORDER (§18.4 rule 1 — data, FAT, then the
directory entry) cannot be seen in a finished image, but its result can, and
the result is coherent.

**This was the pre-batching build.** The framing changed with Set 40's
batching (one command per run, a fixed 512-byte frame per sector whatever the
status), so the write path is verified for the protocol as it was and owes a
second run on the protocol as it is.

### Set 41 — a RAISE puts back only what was covered (SPEC.md §11.96.10)

Set 34's own last paragraph names this as the gap it could not reach: `wm_raise`
armed no rect, so both consumers — the raise cache's restore and §11.90.2's
`OSAPI_WM_DAMAGE` — answered *whole* however little of the window had been
covered. `wm_cov_rect` computes the covered box before `wm_lift`, and
`tools/os88span.py`'s `raise` and its new `paintraise` price the two ends of what
that is worth. Cycle-accurate 5150/CGA, `pttest.img`'s textured 492×133 BMP:

| span | before | after | |
|---|---|---|---|
| **a covered Disk window raised** (`raise`) | | | |
| `wm_su_try` → `gfx_restore` (the edge merge) | 18.57 | **9.46** | 1.96x |
| `gfx_restore` → `wm_grow_paint` (the blit) | 30.72 | **29.44** | 1.04x |
| the whole `wm_draw_win` | 98.17 ms | **87.77 ms** | **1.12x** |
| **Paint raised from under a Disk window** (`paintraise`) | | | |
| the canvas blit | 8,668.7 | **5,526.2** | 1.57x |
| the whole `wm_draw_win` | 9,090.1 ms | **5,947.8 ms** | **1.53x** |

**Both rows are their own geometry and neither is a surprise, which is what says
the feature does what it claims.** `sc_raise`'s two Disk windows cascade 16 px
apart, so the covering window takes **95% of the raised one's content width and
all of its rows**: a restore is `rep movsb` per row, 95% of the bytes is 95% of
the cost, and 30.72 → 29.44 is exactly that. The merge halves for §11.96.8's
reason rather than a proportional one — *an edge column the restore does not
reach is not merged at all*, and this sub-rect's left edge is inside the window,
so one column is merged instead of two.

`paintraise` is the case the work was granted for: a 536-wide canvas with a
320-wide Disk window over part of it. The covered box is **321 of 536 columns,
59.9%**, and the blit comes in at **63.7%** of whole, `pt_blit` being linear in
the runs it covers.

**And the estimate this was costed against was the wrong way round.** Set 34 and
REDRAW-SPEC Part 3 both quote *"only 10% of the canvas was visible turns 8.7 s
into ~0.9 s"* — but 10% visible is 90% **covered**, and what a raise owes is the
covered part, so that case is 7.8 s and the one worth 0.9 s is 10% covered. The
mechanism was right and the arithmetic in front of it was not. **The win is
proportional to how much was covered and there is no single number for it**: a
window with a corner under something gains nearly everything, a window almost
entirely buried gains almost nothing. Both rows above are quoted for that reason.

**One thing the run appeared to confirm, and Set 42 took back.** Both
`paintraise` traces show two `wm_grow_paint` hits for Paint's window — one at
+402 ms with no canvas in front of it, then the real one — which this Set read
as REDRAW-SPEC Part 3's open item, `[pt_apend]`'s deferred resize re-entering
the raise. **It is not**: armed properly, there is one `wm_draw_win`, one
`W_PAINT` and no `wm_front` at all, and the two hits are `pt_growbox` and
`wm_draw_win`'s own `.growbox`. The 402 ms is the palette, the strip and the
divider. Set 42 has the trace; this paragraph is left standing because a
retracted claim is worth more than a deleted one.

Verified at **0 differing pixels** on CGA, Hercules and VGA mode 12h against
`make REDRAWFULL=1`, over all three gates — `subcheck` (11 steps), `ptcheck`
(5, the two raises among them) and `callfront` (11).

**A harness rule the run cost itself once: do not edit the tree while a capture
is running.** `os88sym` asserts its map against `build/kernel.bin` and refuses
rather than answering with a plausible wrong address, so an edit made during a
sequence of captures fails every one after it — at the first symbol lookup, which
is before any window is opened, so the run leaves nothing to look at.

**And the gate found a defect in Paint that was nowhere near it** — SPEC.md
§42.10. `ptcheck` came back with 2,218 differing pixels on VGA (655 on Hercules)
in a region **outside** the covered box entirely: 41 columns of desktop dither
inside Paint's own content, the full height of the colour strip. `pt_wfix`
rewrites `W_W`/`W_H` from `pt_track`, which runs at the *top* of `W_PAINT` after
`pt_org` has already derived `[pt_contw]`, so a window that grew laid out the
rest of that paint at the width it used to be — and under `WF_OWNBG` nobody
fills the band. **It repaired itself on any later WHOLE repaint, which is why it
had never been seen; §11.96.10 stopped raises being whole and it stopped
repairing.** The steps before the raise show it in *both* builds at 0 differing
pixels, which is what proves it pre-dates the change. One `pt_org` after
`pt_wfix` is the fix, and all three adapters then read 0.

### Set 42 — the chrome flash under a drag, and a bug that was not there

Two of the three open items in docs/HANDOFF-REDRAW.md, on a cycle-accurate
5150/CGA. One was real and is fixed; one was a misreading of a trace and is
retired.

#### The flash — SPEC.md §11.97

Reported from the field: *dragging a window, the one underneath shows its frame
and drop shadow for a frame or two before being painted over.* Priced with Part
3.1's instrument (`tools/chromeflick.py`) over one drag of a Disk window across
another, the release captured at 50 frames of 16.4 ms, **three runs of each
build**:

| | before | after | |
|---|---|---|---|
| visible redraw | 19–21 frames, 317–344 ms | 20 frames, 327–332 ms | — |
| frames with transient pixels | 17–18 | 18–21 | — |
| **total transient pixels** | 14,509 / 14,614 / 13,637 | 10,953 / 10,353 / 10,688 | **1.34x** |
| worst transient frame | 2,205 / 2,339 / 2,072 | 1,612 / 1,601 / 1,625 | **1.37x** |

**Two statistics agreeing at ~1.35x, and neither of the other two columns
moving — which is the honest shape of it.** The same frames still change the
same pixels; what changed is how many are written and then immediately
overwritten. What is removed is the lower window's outline and drop shadow
inside the mover's new frame: 1px black lines on a grey dither, which is the
most visible thing a redraw can flash.

**Quote the SUM, not the worst frame, and read the tool's docstring before
either.** A once-per-frame sampler catches whichever intermediate states happen
to fall on a frame boundary, so one frame's peak is partly a coin toss — here
the two statistics happen to agree because the session is stable, and that is
worth checking rather than assuming.

**What is left (10,665 of it) is the content restore and the title bar**, and
both are different pieces of work. `wm_su_try` puts back ONE rect (§5.8) where
the visible region is a list, so bounding it means one `gfx_restore` per
fragment, each paying §5.7's per-call floor and §11.96.2's edge merge — a real
trade rather than a free win. The title bar is §11.97.1's per-cell problem.

**And the first version of this measurement was of the wrong operation**, which
is the finding worth more than the number. SPEC.md §11.91.2 marks the window
underneath on the rect the mover **vacated**, so a drag has to leave ground
inside that window *and* still cover part of it afterwards. Dragged the other
way, `wm_dmg_wins` redraws **the mover alone** — a trace with `wm_draw_win`
armed says so in one line — and the flash then measured is a residue of the
desktop dither, which is small and almost entirely sampling phase: the same
binary returned worst-frame 891, 280 and 902 in three consecutive runs, and the
first before/after pair off it read as a 3.97x win that did not exist. The
corrected session is stable to ±3% on the sum and ±1% on the worst frame.
**A flicker number is only as good as the assertion that the session performs
the operation** — arm the symbol and count the calls.

#### The bug that was not there — Paint's `W_PAINT` does NOT run twice

Sets 32 and 34 both recorded *"the repaint issues two `wm_draw_win` passes for
Paint's window, a ~376 ms one that draws no canvas and then the 8,670 ms one"*,
and attributed it to `[pt_apend]`'s deferred resize calling `OSAPI_WM_FRONT`
from inside `W_PAINT`. Traced with `wm_draw_win`, `wm_front`, `wm_raise`,
`wm_paint_all`, `wm_paint_dmg`, `wm_grow_paint` and `gfx_blit4` all armed, on
both the raise and the drag-off:

```
wm_raise      1 / 1              wm_paint_dmg   -    / -
wm_draw_win     75.58   1 / 1    wm_draw_win     18.55   12 / 1
wm_grow_paint  403.72   - / 1    wm_grow_paint  384.24    - / 1
gfx_blit4       17.92   - / -    gfx_blit4        5.17    - / -
wm_grow_paint 5527.13   1 / 1    wm_grow_paint 6758.76    1 / 1
                                 wm_draw_win      0.89   12 / 0
                                 wm_grow_paint  204.51    0 / 0
```

**One `wm_draw_win` for Paint, one `W_PAINT`, and no re-entry of any kind.** The
two `wm_grow_paint` hits inside it are two different callers: `pt_draw_strip`
ends in `pt_growbox` → `OSAPI_WM_GROW` (SPEC.md §11.1 — the strip's white bed
erases the box, so every path that repaints the strip owes it), and
`wm_draw_win`'s own `.growbox` runs after `W_PAINT`. The 403 ms in front of the
first is not a pass, it is the palette, the colour strip and the divider — which
is exactly what Set 32's own table calls it. **The prose turned two hits of one
symbol into two passes, and four Sets carried it.**

`os88span.py`'s `paint` and `paintraise` scenarios arm three symbols, which is
what let this stand: `wm_grow_paint` was the last mark in both, so a second hit
of it read as a second pass. **When a trace implies a control-flow shape, arm
the symbol that shape would go through** — here `wm_draw_win` and `wm_front`,
neither of which fires.

### Set 43 — a blit run goes straight into the framebuffer (SPEC.md §5.4.1)

docs/HANDOFF-REDRAW.md's item B, and the largest single drawing cost in the
system. `gfx_blit4` coalesces runs of equal pixels and emits one `gfx_hline`
per run — which is one `gfx_fill`, which is §5.7's per-call floor of clip test,
display hook, adapter dispatch, `vga_rect_setup` and row-base arithmetic before
a byte is written. On a 1bpp adapter `sw_blit_span` writes the run itself
instead: the row base and the dither phase are worked out once a **row**, the
§39.4 reduction is one table read, and what is left is two masked bytes and a
`rep stosb`.

Cycle-accurate 5150/CGA, Paint raised from under a Disk window (so §11.96.10's
damage rect, ~65% of the canvas width), `tools/os88span.py paintraise`:

| the canvas | runs/row | before | after | |
|---|---|---|---|---|
| `TEXTURE.BMP`, runs of 3–8 px | 84.9 | 5,526.2 ms | **2,430.7 ms** | **2.27x** |
| `FINE.BMP`, runs of 1–2 px | 308.0 | 18,777.3 ms | **8,364.9 ms** | **2.25x** |

**The two ratios agreeing is the result, not a coincidence**: what the change
removes is a fixed cost *per run*, so it scales with the run count and the art
does not matter. Per run: ~740 µs → ~326 µs on the textured canvas and ~702 →
~313 on the fine one, so about **400 µs of arriving** goes and about **315 µs of
per-run work** stays.

The whole Paint raise is 5,947.8 → **2,851.5 ms** on the textured canvas, and
against the figure this round started from — before §11.96.10 gave a raise a
damage rect at all — **9,090.1 → 2,851.5, 3.19x**.

**Flat art gets faster too, which the plan doubted.** docs/PAINT-NOTES.md's
sketch was a plane-parallel decoder that "would beat this on detailed pictures
and lose to it on flat ones"; this is not that. It keeps the run scan, so a
solid row is still ONE run — it just costs less. Measured on `mkbmp`'s `flat`
art (one run a row) with the same damage rect: **132.1 ms**, which with the
84.9-runs figure fits a two-term model of the whole path —

```
run path, µs a row = 830 + 371 x runs      (830 = the scan and the row setup)
```

— reproducing all three measured densities within 3% (flat 132.1 against 132.1,
textured 2,352 against 2,430.7, fine 8,292 against 8,364.9). **That 830 µs is
the term the "45x" estimate forgot**, and it is what decides whether the byte
decoder needs a hybrid; docs/HANDOFF-REDRAW.md item B2 has the working.

**Merging the two routines was worth 11%** — 2,698.1 → 2,430.7 ms — and that is
the 8088 lesson rather than a tidy-up. Written as a `gfx_blit_span` that worked
out the pattern plus an `sw_span` that wrote it, the pair spent **24 stack slots
and a near call/ret** on prologue alone: about 400 of the ~1,900 clocks a run
cost. On this machine the instruction COUNT is the price and there is no work in
a push.

**What is left, and what it would take.** ~315 µs a run is ~1,500 clocks: the
run scan's three 4-bit shifts (24 clocks each on an 8088), the `repe scasb`
setup, the tail nibble decode, and `sw_blit_span`'s own forty-odd instructions.
Removing it means not working per run at all — a decoder that walks the
destination BYTE by byte, gathering eight pixels' bits regardless of runs, at
roughly 44 clocks a pixel. On `FINE.BMP`'s damage rect that is ~394 ms against
8,365, another 20x; on a flat row it is 4.5 ms against 0.3, fifteen times WORSE.
So the end state is a hybrid keyed on the row's run density, and this change is
the half of it that is unconditionally right.

**And VGA followed**, `vga_blit_span`: the four planes take their data from
Set/Reset rather than from the CPU byte, so a run is one `out` for the colour
and then the same masked-byte shape the 1bpp writer has — which is why its
interior can be a `rep stosb` of a byte whose *value* is irrelevant, exactly as
`gfx_fill`'s own interior is. Enable Set/Reset is armed **once a call** and
cleared at the end, because it is the half that does not change between runs.

| the canvas, on `os8088_xt_vga` | runs/row | before | after | |
|---|---|---|---|---|
| `TEXTURE.BMP` | 84.9 | 4,226.2 ms | **2,163.5 ms** | **1.95x** |
| `FINE.BMP` | 308.0 | 14,401.6 ms | **7,151.1 ms** | **2.01x** |

Slightly under the 1bpp figure and for the obvious reason: a run costs two to
four `out`s here against none there, and an ISA `out` is not free.

Verified **0 differing pixels** on CGA, Hercules and VGA mode 12h with
`tools/ptcheck.py` (5 steps) and `tools/subcheck.py` (11), and separately with
`PTROW=1` on CGA and on VGA — `FINE.BMP`'s 1–2 pixel runs, where every run sits
inside ONE framebuffer byte and touches neither edge of it, which is the
narrowest case §5.4.1 has and the one a picture barely exercises.

**One 25-pixel `subcheck` difference is NOT a defect and is worth knowing
about**, because it will recur on every kernel-size change that crosses a
cluster: the drive-A Disk window shows `Free ...` for the SYSTEM disk, which
carries `KERNEL.SYS`, so this commit's 174 → 175 sectors is 139 → 140 clusters
used and one kilobyte less free. It appears in exactly the five steps where that
window's status line is on screen and in none of the others, which is what
identifies it; `make`'s own `N/354 clusters` line confirms it in one command.

### Set 44 — the blit stops working per run (SPEC.md §5.4.1.1)

docs/HANDOFF-REDRAW.md item B2, built without its hybrid. Same session, same
damage rect, cycle-accurate 5150/CGA, three run densities:

| Paint's canvas | runs/row | pre-§5.4.1 | span writer | **decoder** |
|---|---|---|---|---|
| `FLAT.BMP` | 1.0 | — | 132.1 ms | **517.6 ms** |
| `TEXTURE.BMP` | 84.9 | 5,526.2 ms | 2,430.7 ms | **516.6 ms** |
| `FINE.BMP` | 308.0 | 18,777.3 ms | 8,364.9 ms | **517.6 ms** |

**516.6 / 517.6 / 517.6 — a 0.2% spread across a 3.6x range of run density.**
That is the result: a blit now has ONE cost, and "never price a blit on flat
art" (Set 32) stops being a rule anyone has to remember. Against the span
writer it is **4.7x** on a picture and **16.2x** on detailed art; against the
run path this round started from, 10.7x and 36.3x. The price is the flat row:
132.1 → 517.6, **3.9x worse**, on the one operation nobody waits for.

**Three things this measurement corrects, and the first is a rule.**

**The estimate was 2.9x optimistic because it counted clocks and not BYTES.**
Item B2 predicted 4.71 µs a pixel from a textbook instruction timing; measured
it is **9.56 µs**. §5.7's own floor says why — **4.34 clocks per instruction
byte on an 8088** — and the loop is ~20 instruction bytes a pair, which is 10
a pixel, which is 43 clocks, which is 9.0 µs. The 8-bit bus starves the
prefetch queue, so on this machine **the size of a loop IS its speed** and a
clock-count estimate of a tight loop will always read low. *Count bytes.*

**So the crossover is ~10 runs a row, not 1.84** — still far below anything
with content in it (textured art is 55 a row over the damage rect, fine art
201), and still above flat.

**And the next step is visible in the same arithmetic**: the per-pair count
test is 8 of the 20 bytes. An unrolled four-pairs-a-byte loop, legal whenever
`x` is even, would be about twice as fast again — ~260 ms — at the price of a
second loop body for odd `x`.

**One bug, caught by the gate and worth naming**: the dither bit is
`parity XOR 1`, not `parity`. `sw_parity`'s `0xAA` has its bit SET at x = 0 of
an even row and bit 7 is the leftmost pixel. Inverted, it changes every
dithered pixel and nothing else — 20,532 differing pixels, which reads like a
far deeper defect than a single `xor al, 1`.

Verified **0 differing pixels** with `tools/ptcheck.py` on CGA, Hercules and
VGA mode 12h, and on CGA with `PTROW=1` (`FINE.BMP`'s 1–2 pixel runs). The
`subcheck` 25-pixel difference is Set 43's known one — `KERNEL.SYS` grew a
cluster and the drive-A window reports one kilobyte less free.

**Cost: `.text` +169, `.bss` +513, and it lands exactly ON `KERN_BUDGET`.**
`KERN_BUDGET`'s eighteenth move is one step to keep the tree buildable, and it
is an **ask** rather than a grant — the constant's own comment carries the two
ways to hand it back, both costed.

### Set 45 — the aligned bodies, and the parity nobody had looked at (SPEC.md §5.4.1.2)

Set 44's own closing arithmetic, built. The per-pair count test is 8 of the 19
instruction bytes a pair costs to fetch, and the boundary it looks for arrives
every *fourth* pair — so once the first destination byte is stored and the
phase is fixed, four pairs are one whole byte and the accounting is not needed
at all. Same Paint raise, same damage rect, cycle-accurate 5150/CGA:

| canvas x | phase | §5.4.1.1 | **§5.4.1.2** | |
|---|---|---|---|---|
| **74** | even, nothing pending | 516.6 ms | **259.1 ms** | 1.99x |
| **95** | odd, one bit pending | 508.3 ms | **299.0 ms** | 1.70x |

…and the "one cost" property survives: **259.1 ms at 85 runs a row against
260.2 at 308**, 0.4%. Against the run path this round started from, **21.3x**
on a picture and **72.4x** on detailed art. Predicted 2.05x from the byte
count and measured 1.99 — Set 44's *count bytes* rule holding on the first
estimate made under it.

**The finding is the second row, and it was invisible until it was looked
for.** The even body is reachable only at an even destination x, Paint's
template lands its canvas on one, and every gate and every measurement in this
whole round had therefore priced *one half of the routine* — with the other
half a **one-pixel drag away**. `PTNUDGE` (an odd sideways drag before the
cover/raise) is now an argument to both `tools/ptcheck.py` and
`tools/os88span.py`, and it is what showed the cliff: the same picture, the
same window, one pixel over, **508.3 ms against 259.1**. The odd body is 52
bytes per eight pixels against the even one's 37, the carry epilogue being the
whole difference, and it takes the spread down to 15%.

**The crossover moved with it, and docs/LAST-DROP.md 3 is re-costed**: the run
path is still `830 + 371 x runs` µs a row and the decoder is now 1,948, so the
hybrid's band is **~3 runs a row** rather than Set 44's ~10, and what it buys
on flat art is 1.96x rather than 3.9x. Same verdict, less of it.

**A harness rule falls out of it.** *A gate that never moves the thing it
tests proves the alignment it happened to start at.* This is the second time
this round — Set 42's chrome flash measured a drag that vacated nothing — and
both had the same shape: a scripted session that reads as thorough because it
has many steps, all of them at one phase of the thing under test.

**One trap in the layout, and NASM catches it**: `loop .pair` is a short jump
and cannot reach over 90 bytes of unrolled body, so the two bodies live past
the routine's `ret` and the three "no pairs left" exits trampoline through one
`.nomore`. The first attempt put them inline, which is `short jump is out of
range` and not a silent wrong answer — the one class of layout mistake in this
tree that fails loudly.

Verified **0 differing pixels** with `tools/ptcheck.py` at **both phases** on
CGA and Hercules (`PTNUDGE=21` against a reference build of the same commit
without the change), at the even phase on VGA mode 12h and on CGA with
`PTROW=1`, and with `tools/subcheck.py` on CGA — 11 steps, 0 pixels, so Set
42's known 25-pixel free-space difference is gone with the cluster count back
where it was.

**Cost: `.text` +148.** `kern_big` crosses no rung and is left with **63 bytes**
in its image rung against 211, so the next byte added to `.text` anywhere buys
a whole 512-byte step; `kern_small` crossed one (1,536 → 1,024 spare, two
steps, still inside its own budget). `KERN_BUDGET`'s nineteenth move (+2,048,
granted with the eighteenth as one piece of work) is what that slack is drawn
from.

### Set 46 — Paint's palette stops being drawn (SPEC.md §11.96.11)

docs/HANDOFF-REDRAW.md item C, in the half that fits. **Paint has no raise
cache at all and the reason is memory** — over its whole content that is ~9 KB
on 1bpp, ~36 KB on VGA and ~150 KB grown, on top of the ~127 KB it already
holds — while its repaint is the most expensive in the tree.

**The measurement that decided the design** breakpointed the primitives
between `wm_draw_win` and the canvas blit and bucketed each by the rect it was
handed, so no package symbols were needed. Cycle-accurate 5150/CGA, Paint
covered by a Disk window and raised:

| region | `gfx_fill` | `hline` | `vline` | `frame` | `font_char` | total |
|---|---:|---:|---:|---:|---:|---:|
| **tool palette** | 225 | 184 | 23 | 12 | 7 | **451** |
| bottom strip | 64 | 27 | 26 | 13 | 1 | 131 |
| everything else | 10 | 2 | 4 | 1 | 5 | 22 |

**604 drawing calls, 421.8 ms — and 75% of it is a 44-pixel column** of eight
wells, eight 16×16 icons and the size boxes, none of which has changed since
the window opened. That is what said a BAND is enough and a general kept region
is not worth its code (§11.96.11.1): the strip is the other 99 ms.

So a window names one band on one edge, the cache banks that alone, and
`wm_damage` hands the app the content minus it:

| | before | after | |
|---|---:|---:|---|
| chrome, up to the canvas blit | 421.8 ms | **191.9 ms** | 2.19x |
| the canvas blit | 259.1 ms | 259.1 ms | — (same damage rect) |
| **the whole Paint raise** | **680.9 ms** | **451.0 ms** | **1.51x** |

Paint's cache is ~1 KB on 1bpp and ~13 KB on VGA, and **Paint needed no drawing
change at all** — `pt_draw_pal` and `pt_fsbed`'s bed for it were already gated
on §11.90.2's damage rect. Its side of this is nine instructions: name the band,
and promise `WF_SAVEU` only if the band was taken.

**One bug, and the gate is what found it — which is the transferable part.**
Two routines destroy `wm_su_son` and its four rect words on the way through a
restore, and **both are right to**: `wm_su_srect` spends the one-shot per window
so a skipped window cannot pass its rect to the next, and `wm_su_edge` reuses
the four words as scratch *on the explicit reasoning that the one-shot has
already been spent*. Restoring only the flag left `wm_damage` intersecting the
app's half against the BAND's rect, which is empty — so Paint was told it owed
nothing and correctly drew nothing: **19,696 differing pixels, the canvas never
repainted.** A span measurement had already shown it as a *win* (the whole raise
"190.8 ms" with no `gfx_blit4` in it at all), which is exactly what a
performance number looks like when the work has gone missing rather than got
cheaper. **A timing that improves by more than the change can explain is a
correctness question, not a result.**

Verified **0 differing pixels** with `tools/ptcheck.py` on CGA, Hercules and
VGA mode 12h against `make REDRAWFULL=1` (which refuses the band, so Paint
declines the promise and the reference is exactly today's behaviour), and with
`tools/subcheck.py` on CGA — 6 pixels, one digit of drive A's free space, the
kernel having grown a cluster.

**Cost: `.text` +417, and it is the last of the small kernel's.** `kern_big`
crosses one image rung and stands at 1,024 spare (two steps); **`kern_small` is
at 0 spare, exactly on `KERN_BUDGET`** — it builds, and there is not one byte
left for the next feature. That is a decision to take with whoever asks for the
next one, not a build fix.

### Set 47 — item D measured, and not built (SPEC.md §11.91.3)

docs/HANDOFF-REDRAW.md item D: key §11.91's marking on each window's **redrawn
region** instead of its rect. **A negative result, and the measurement is the
deliverable** — it costs one guest run and it stops the next session spending a
budget step on it.

**It can only ever spare a window marked TRANSITIVELY**, one that does not
overlap the damage rect but does overlap a marked window below. So the question
is how often that arises, and the session was built to favour it: three windows
(Drive A, Drive B, Note Pad), Note Pad parked over the *interior* of a Disk
window and clear of its frame, then dragged off. Cycle-accurate 5150/CGA, the
damage rect read out of the guest at the `wm_dmg_wins` breakpoint:

```
damage         (127,60)-(427,199)
win 0  Disk    (110,20) 320x155   overlaps the damage
win 1  Disk    (126,20) 320x155   overlaps the damage
win 2  NotePad (127,60) 260x155   the mover - marked unconditionally
```

**Three windows drawn, zero transitive marks.** A two-window drag draws two, of
which one is again the mover. The reason is structural rather than lucky: a
drag's damage rect is the union of where the mover WAS and where it IS, so
every window it uncovered overlaps it *by construction*.

Two more things stand in the way even where the case does arise. `wm_draw_win`
writes the outline, the drop shadow and the title bar **whole** whatever the
cache did, so a transitively-marked window is genuinely damaged unless it sits
strictly inside the lower window's interior — and windows here cascade 16px and
overlap at their edges. And **the fix cannot be a bounding box**: the outline's
bbox IS the whole window rect, so no accumulation of rects comes out smaller
than today's. It needs a rect LIST with union and intersection, in the routine
where a wrong answer leaves STALE pixels rather than extra ones.

**The other half of item D is the same conclusion by another route.**
§11.96.6's accumulated bounding box means the second window drawn restores the
first window's whole rect as well as the damage — the taper its own text names.
Computed on the measured geometry: window 1 restores rows 38..173 where the
ideal is 60..173, **136 against 114 — 19% of one restore**, about 4 ms of a pass
costing over 100. Same region, same reason, same answer.

**What would flip it**: a UI with small windows floating over large ones — a
palette, a tool window, an inspector — where the transitive case is the common
one rather than the absent one. os8088 has none.

**Cost of finding out: 0 bytes and four guest runs**, against several hundred
bytes and the riskiest routine in the window manager. That ratio is the argument
for measuring the case before building the mechanism, and it is the third time
this round has paid off (§19.2.3.1 and §48.18.1 are the others in this file).

### Set 48 — the cache holds four fragments, and Paint's chrome is 4.7x (SPEC.md §11.96.11.1)

Set 46 gave a window ONE band on one edge and took Paint's tool column out of
its repaint. This gives it four, so the bottom strip goes too. Same Paint
raise, same cycle-accurate 5150/CGA:

| | before the round | one band (Set 46) | **four (Set 48)** |
|---|---:|---:|---:|
| chrome, up to the canvas blit | 421.8 ms | 191.9 ms | **90.6 ms** |
| the canvas blit | 259.1 ms | 259.1 ms | 259.1 ms |
| **the whole Paint raise** | **680.9 ms** | 451.0 ms | **350.8 ms** |

**4.7x on the chrome and 1.94x on the raise.** Paint's cache is ~3 KB on 1bpp
against ~9 KB for its whole content, which is why it could not have one at all
before Set 44. Its side of this is two `OSAPI_WM_BAND` calls: the tool column at
launch, and the strip from `pt_bands` on every paint, because the strip's height
is `content − canvas` and the size boxes move it without moving the window.

**Three bugs, and all three are the same mistake in three places**: *a routine
that was right about "the buffer" is wrong about "a fragment of it".*

- **`gfx_save` writes `span+1` bytes a row, not `span+3`.** The +3 in the old
  size arithmetic is the image *plus* §11.96.2's two edge-scratch columns.
  Charged per fragment it put every later fragment's offset past its own
  pixels — 2,943 differing pixels, read as a stripe down Paint's tool column.
- **The edge scratch has to be SHARED and past every fragment.** Derived inside
  `wm_su_edge` as "past this rect's image" it is the same address while there is
  one fragment and lands inside the next one's pixels when there are four.
- **`wm_su_edge` patches at the FRAGMENT's base, not `WSU_IMG`.** Two symptoms
  at once and they looked unrelated: the left band's overhang column came back
  stale — a one-pixel line down the *outside* of the window, 100 px — while the
  bottom strip was corrupted where the misdirected merge landed, 326 px.

**And the second and third only showed on Hercules.** CGA passed the first fix
clean and Hercules did not, on the same binary: the covered rect on the CGA
session reaches the left band and on the Hercules one does not, so the two
adapters restore a different set of fragments. *Three adapters is not
belt-and-braces here; it is the only way this class of bug appears at all.*

Verified **0 differing pixels** with `tools/ptcheck.py` on CGA, Hercules and VGA
mode 12h against `make REDRAWFULL=1`, and with `tools/subcheck.py` on CGA — 11
steps, 0 pixels, which is also the proof that a window naming NO band takes
exactly the path it always did (fragment 0 is then the whole content).

**Cost: `.text` +559 over Set 44.** Both kernels cross one image rung and stand
at **512 spare, one step** — `KERN_SMALL_BUDGET`'s twentieth move is what paid
for it, and it is spent.


### Set 49 — Solitaire fills nothing twice (SPEC.md §43.9)

The question asked was whether §11.96.11's `wm_band` is worth anything to
Solitaire. **It is not, and measuring that is what found what is.** Solitaire
has promised `WF_SAVEU` since it was written and its content banks in ~3.7 KB
on 1bpp, so the raise cache already answers every partial case — a raise from
under another window **97.5 ms**, a window dragged off it **78.7 ms**, an
un-minimize **172.1 ms**, and `W_PAINT` running in none of them. A band is for
a window whose whole-content cache is unaffordable; this one's is affordable
and installed, and Klondike has no static furniture at an edge to name.

`gfx_blit4` is what makes those three claims checkable rather than inferred:
card backs are the only thing in this window that blits (§43.3), so a capture
with none in it is a capture in which `sol_drawall` did not run.

**What is expensive is the full repaint — a New Game, a Restart, a launch,
every `wm_paint_all` — and two of its fills were the same pixels twice.**
Cycle-accurate 5150/CGA, 220x136 content, the *same deal in both builds*
(`[osapi_seed]` written from the harness, because `sol_newgame` takes its
entropy from `OSAPI_RAND` and nowhere else — two runs of one binary otherwise
deal differently and no pixel or call count is comparable):

| | before | after |
|---|---:|---:|
| New Game — the app drawing itself | 789 calls, **664.2 ms** | 777 calls, **562.6 ms** |
| a window MOVE — through `wm_draw_win` | 978 calls, **934.6 ms** | 965 calls, **809.0 ms** |

**Exactly 12 fills and exactly 13**, which is the shape that says the change did
what it claims and nothing else: twelve pile wipes on both, plus the kernel's
white fill on the one that goes through `wm_draw_win`.

- **§43.9.2, the twelve wipes.** `sol_drawall` fills the whole content with felt
  and then `sol_drawpile` fills *each pile's rect* with felt again, except where
  `sol_covers` already declined. Per call: the whole-content fill **26.45 ms**,
  seven tableau column wipes at **11.85–12.93** (a column's rect is the full
  content height), five waste/empty-foundation wipes at **3.93–4.06** —
  **102.9 ms of 681.3, 15.1%**, for pixels that were already that colour.
  Measured after: **101.6 ms**.
- **§43.9.1, `WF_OWNBG`.** Solitaire predates §11.90.1 and is its second
  consumer. One fill of **~25 ms**, and as a *picture* it is Part 1's double
  draw at maximum contrast — the felt is `CGREEN`, which §39.4 reduces to
  **black** on both mono adapters, so every kernel-driven repaint was the window
  going white, then black, then filling with cards over two-thirds of a second.

**The repaint is ~91% floor and the call count is the only lever**, which is why
both of these are worth their size: 816 calls x PERFORMANCE.md Part 2's ~756us
is 617 ms of the 681. Nothing inside those calls was touched. This is §48.18's
finding in another package.

Verified **0 differing pixels** on CGA, Hercules and VGA mode 12h — three
captures each (the deal, the same board after a MOVE, and a Restart in place)
against a build of `HEAD`'s `solitaire.asm`, by `tools/solcheck.py`. The move is
in the session because `WF_OWNBG`'s failure mode is a pixel the app does not
write showing *what was there before*, which is only another window's content
once something has moved.

**Cost: the kernel is untouched.** `solitair.o88` 5,799 -> 5,823 bytes, which is
heap while the game is open and nothing when it is closed.

### Set 50 — Solitaire's drop redraws the cards that MOVED (SPEC.md §43.10)

Set 49 priced Solitaire's FULL repaint and left §43.7's incremental path alone
on the strength of its own reasoning: a move touches two piles, so it costs two
`sol_drawpile` calls and never a whole content. That is true, and it is not the
same claim as *a drop is cheap*.

**Measured on a cycle-accurate 5150 with a Hercules card, one card dragged off
a six-card run onto another column** — the commonest move in Klondike, driven
through the UART with the drawing primitives breakpointed and the board poked
to a chosen position so the case measured is the case chosen:

| | before | after |
|---|---:|---:|
| card faces drawn | 11 | 3 |
| drawing calls | 481 | 210 |
| guest time | **332.5 ms** | **171.2 ms** |

**Eight of those eleven faces were pixel-identical to what was already on the
screen.** Cards leave and arrive at the *top* of a column, so the ones
underneath do not move — and `sol_drawpile` erased and redrew them anyway,
because §43.7's keep stopped at the face-down run. The fan was `(cfd 2, cfa 5,
cfu 14)` before the move and after it, so nothing below the change had moved by
so much as a pixel.

**The two ratios differ — 2.3x the calls, 1.94x the time — because the faces
that survive are the expensive ones**: 41 calls per face on average before, 66
after. A buried card is a sliver that skips its centre pip and its bottom edge
(§43.3), and slivers are exactly what a keep keeps; what is left to draw is the
full-height card at the bottom of the column and the one that just arrived.

**A re-fan is the case where there is nothing to win, and it is not rare.**
`sol_colfan` tightens a column that would run past the content bottom, so on
CGA's 136-row desktop a column growing from 7 cards to 10 takes the face-up
step from 12px to 10px and *every* card in it really does move. Measured, that
same drop is **309 calls before and 309 after** — the shadow keeps nothing and
is right not to. The win is on the adapters with the room for long columns,
which is Hercules and VGA, and on the field machine that is the primary card.

**What it cost: nothing the kernel can see.** `solitair.o88` 5,823 -> 5,889
bytes of image and 1,276 -> 1,451 of bss — 203 bytes of shadow against the 28
the sliver cache used — all of it inside the package's own region, which is a
heap claim (§20.1). No kernel file changed.

**And §11.96's raise cache was asked whether it covers any of this. It does
not, and it is not idle either** (§43.10.1). The question arrived as *"a drag
and drop did not seem to use this buffer — maybe this is a global issue?"*,
which is the same words §11.96.4 was written for, so it was re-asked with the
instrument rather than answered from the document. Two scenarios added to
`tools/os88span.py`, cycle-accurate 5150/Hercules:

| | | |
|---|---|---:|
| `solraise` | Solitaire covered, then called to the front | `wm_su_try` → `gfx_restore` **15.24 ms** |
| `sol` | a Disk window dragged **off** Solitaire | `wm_su_try` → `gfx_restore` **32.76 ms** |
| `dragoff` | ...and the same for two Disk windows, as a control | `wm_su_try` → `gfx_restore` **10.84 ms** |

**Zero `gfx_blit4` in any of them**, so `sol_drawall` ran in none — against
§43.9's 681 ms full repaint. The buffer works, for this package and generally.
What it cannot do is a card drop: §11.96.4's bank **drops the front window's
cache and takes everybody else's**, and the window a card is being dropped on
is by definition the front one. The two mechanisms compose — the cache for
what another window covered, the shadow for what the app moved itself — and
after this change they compose over an incremental drawing that is exact.

**One measurement here was wrong before it was right, and the reason is in
`os88span.py`'s own docstring.** A first pass with a hand-rolled probe reported
`wm_su_try` firing **zero** times on a Solitaire raise whose picture came back
perfect, which reads as "the cache is being bypassed" — the very claim under
test. It was the harness: the probe sent its press and release as back-to-back
raw packets, which the 1200-baud UART drops (`subcheck.pclick` exists for
exactly this), and armed the breakpoints before driving the input instead of
after. **Drive the input before arming and send only the last packet**, which
is what the scenarios above do and what that file has said since Set 30.

**Verified by comparison, not by argument.** `tools/solcheck.py` grew a PLAY
phase for this: fourteen real drags chosen from the guest's own board (so both
builds play the same game off the same seeded deal), including a six-card run,
a three-card run, waste plays, a foundation play that empties a column outright,
and Auto Finish — capturing after every one. The gate is that the incrementally
drawn content equals **the same position forced through a full `W_PAINT`**:
**0 differing pixels on Hercules and 0 on CGA**. That comparison needs no second
build, which is what makes it the strong one — a wrong keep is a card left
standing where a card no longer is, a real picture rather than a broken-looking
one, so nothing about the screen would say so.

**`make SOLNOKEEP=1` is the second build, and it found something that is not
this change.** It stubs `sol_keep` to 0, so every column is erased and redrawn
whole — strictly more drawing than either §43.7 or §43.10 — and its own content
then differs from *its own* full repaint by **12 pixels**: one column, one pixel
wide, twelve rows tall, at a tableau column that the move in question neither
redraws nor keeps. Two independent runs of it are byte-identical to each other
and both carry it, so it is deterministic rather than a timing or cursor
artifact; it appears at one identifiable step — a six-card drag whose outline
crosses four columns — and survives every later move, which is what a stray
pixel in a column nothing repaints does.

**It is unexplained and it is not safely a property of the knob**, because
`keep = 0` is a state the shipped build reaches too: on the first draw after
`sol_pinv`, and after any re-fan. What is established is the paragraph above —
the shipped build's own output is exact against a full repaint on both 1bpp
adapters — and not a clean cross-build diff, which this did not produce (15 of
19 captures identical, 4 differing by those same 12 pixels, with the reference
the build that disagrees with ground truth). Worth an investigation of its own;
the trace to start from is which drawing call touches that column during that
step, since neither pile being redrawn owns it.

Three things the harness had to be taught first, and each of them made a broken
build look like a passing one:

- **The listing has to be of the build that is there.** A package's bss names
  are `equ`s over `os88_image_end` with no map, so the offsets are walked out of
  the source and checked against an instruction in the listing — and the listing
  was being assembled without the knob's `-D`, so it described the other build.
  It was caught only because the two images happen to differ in size.
- **The crop has to be in guest coordinates.** `solcheck` cropped the card's
  rasterised frame, which on Hercules is 720x350 for a 720x348 screen at
  dx = -16 (`sucheck.fb` has said so for as long as it has existed). Both builds
  are offset the same way so a cross-build diff survived it; a comparison across
  a window MOVE does not.
- **Blanking the mouse cell is not enough when the two captures are different
  steps.** The pointer is somewhere else at each, so one image gets a black box
  where the other has cards — 321 pixels of difference, in BOTH builds, which is
  what said it was the harness. The arrow is parked clear of the window now and
  the blanking is belt and braces.

### Set 51 — a window MOVED replays its content (SPEC.md §11.96.12)

Set 50's question came back one level up: *"drag the whole Solitaire window by
its title and it redraws its entire screen card by card, instead of from its
last full window shadow buffer — I'm not sure if this is global or not."*

**It was global**, and `tools/winmove.py` was written to price it. Cycle-accurate
5150/Hercules, each window dragged by its own title bar:

| | drawing calls | guest time | the tell |
|---|---:|---:|---|
| a Disk window | 207 | **236.6 ms** | 71 `font_char` — the listing re-lettered |
| Solitaire | 1,016 | **914.7 ms** | 22 `gfx_blit4` — every card back again |

`wm_su_try` appeared in both traces and `gfx_restore` in neither: the dragged
window is FRONT, so §11.96.4 had dropped its cache, and `wm_su_ck` compares the
absolute content rect, which a move changes in all four numbers.

**The fix is a relabel, not a mechanism.** `wm_dc_take` banks through
`wm_su_take` at the old position and moves the claim's header by the drag's own
delta; `wm_su_ck` then agrees and the raise cache's own restore — fragments,
`wm_su_edge`, free — does the rest. The same drag through both builds
(`make DRAGCACHE=0` is the A/B), Solitaire, (231,20) → (167,20):

| | drawing calls | guest time | its cards |
|---|---:|---:|---|
| `DRAGCACHE=0` | 981 | **954.3 ms** | 22 `gfx_blit4` |
| with the cache | 210 | **462.4 ms** | **0** — one `gfx_restore` |

**4.7x the calls, 2.06x the time.** What remains is honest: the window
*underneath* is genuinely revealed and must be redrawn, which is most of the
462 ms.

**Cost: `.text` +174 bytes — and what that costs the MACHINE depends on the
base it lands on, which is worth stating because both numbers are real.** On
the branch it was written against it crossed no rung and the footprint moved
**+0**. Merged onto `elendilon`, where §2.6's cold-segment round had left the
image rung with under 200 bytes of slack, the same 174 bytes cross it:
`KERN_SIZE` 102,912 → **103,424**, `KERN_BUDGET` spare 2,048 → **1,536
(3 steps)**. That is
docs/KERNEL-MEMORY.md's accounting rule working as written — a change that
crosses no rung is not free, it has spent slack that belonged to the next
feature, and here the next feature was the merge itself.

The other half of that round is the good news: `.text+.bss` is **51,787 of
65,536** afterwards, so **13,749** are left against the ceiling that cannot be
raised — where this change's own branch had 228.

**Verified: 0 differing pixels of 250,560 on Hercules and of 128,000 on CGA**,
whole screen, against the reference build — and `gfx_restore` fires for
**Fractal**, which promises no `WF_SAVEU` and has a live worker, which is what
says every window is eligible rather than only the ones §11.96.1 marked.

Four things this cost, and every one of them made a working build look broken
or a broken one look fine:

- **The collector ate the first hit.** `burst` opened with `run`, and the guest
  is already stopped at the *first* symbol of the operation when it is entered —
  which is precisely the routine under test. `wm_dc_take` was executing all
  along and simply never appeared, which read as "the new code never runs".
- **The first honest trace then showed the bank succeeding and the restore
  refusing**, and the reason was real rather than a slip: the test drag moved
  the window's bottom *below the screen* (content `y2` 361 on a 348-row
  Hercules), which `wm_su_try` correctly refuses. `wm_dc_take` asks that
  question up front now, so the ~4 ms of claim and save is not spent on a
  window that will be drawn in full anyway.
- **782 differing pixels that were two different card games.** Solitaire seeds
  from `GET_TICKS`; the gate now pins `[osapi_seed]` and presses N, which
  `tools/solcheck.py` documents in capitals.
- **`os88sym` refused to run against the knob build at all** — correctly, since
  a map of a different kernel is a wrong answer rather than a missing one. It
  reads `$OS88_DEFINES` now, so every tool layered above it (`os88geom.word`,
  `sucheck.fb`, `Marty.sym`) can drive an A/B build without threading defines
  through each one. That trap is named in `os88span.py`'s docstring and had no
  remedy until now.

### Set 52 — VGA wants the byte boundary MORE than mono does (SPEC.md §11.94)

`WF_SNAP` was gated on `[vid_mono]` for most of its life, on the premise that
what snapping buys is `font_run`'s single-store cell path — which is genuinely
1bpp-only — and therefore that on VGA the flag is a no-op costing 8px drag
steps for nothing. **The premise measures false, and the refutation was already
printed in SPEC.md §11.94**: its `-icount` table said 2.1% on Hercules against
5.8% on VGA, sitting directly above the paragraph explaining that the flag does
nothing there. The number had been filed as a fact about `font_run` rather than
about *alignment*.

**Mode 12h is eight pixels to a byte** — four planes of one bit each — so an
unaligned 8x8 cell spills into a second framebuffer byte on VGA exactly as on a
Hercules, and `font_char`'s planar `.vram` path pays for the spill with a second
`GC8 Bit Mask` `out` and a second latched read-modify-write, once per row per
cell. Its `.no_second` early-out is taken only when `x & 7 == 0`.

Measured, `tests/typebench` (40 keystrokes, whole 40-cell line redrawn after
each — what `np_redraw` does to its dirty band), cycle-accurate 4.77MHz 8088;
Hercules on `os8088_5150_herc` with the period IBM ROM, VGA on `os8088_xt_vga`:

| adapter | CHAR aligned | CHAR skewed 5 | one `font_run` | alignment is worth |
|---|---:|---:|---:|---:|
| Hercules | 1424 ms | 1472 ms | 316 ms | **3.4%** |
| VGA | 1013 ms | 1108 ms | 996 ms | **9.4%** |

**The adapter the gate excluded gains 2.8x the one it was written for.** The
`font_run` column is the other half of the story and is why the premise was
believable: on Hercules the run is **4.5x** the CHAR pair (the single-store
path), and on VGA it is level with it (ratio 101) because a run there falls back
to `gfx_fill` + `font_str`. The fallback's *glyphs* are what come out aligned,
and that is where the 9.4% lives.

`tests/gfxbench` on `os8088_xt_vga` prices the primitive the same way:

| row | µs |
|---|---:|
| `PAIR 10 aligned` | 6984.93 |
| `PAIR 10 skewed 5` | 7855.51 |
| `FONT_RUN 10 aligned` | 7192.01 |
| `FONT_RUN 10 skewed` | 8051.55 |

**12.5%** and **12.0%** — and the report's own derived `skewPAIR/RUN x100` row
reads 109.

**The real consumer was paying it.** Note Pad on VGA opened with its content
origin at **x = 61**, which is skew 5 — the exact column typebench's skewed row
measures — read both out of the guest (`W_X` 60) and off the framebuffer. With
the gate removed it opens at 56. Timer lands at 328 and the Task Manager at 248,
both aligned, both verified by reading `wm_wins` rather than by looking.

Two things worth keeping:

- **Removing it is a no-op on mono by construction** — the deleted instructions
  are a `cmp`/`je` whose branch was never taken there — and that was checked
  rather than claimed: identical 7-step scripted sessions (boot, Disk window,
  folder, launch Note Pad, type, drag, type again) against a reference kernel
  with the gate restored are **0 differing framebuffer bytes** on Hercules and
  on CGA. The only bytes that ever differed were a **20-byte 6x6 box at
  x = w−16, y = 6..11** on both adapters — the menu bar clock's last glyph
  cell, `00:01` against `00:02`, which advances with wall time and landed on a
  different step on each adapter. A diff that lands in the same relative
  6x6 box on two different geometries is a clock, not a regression.
- **It made the kernel smaller**: `.text` −18, `.cold` −4, no rung crossed,
  footprint unchanged. Removing a gate removes code.

The cost is real and it is the one the old comment named: **8-pixel horizontal
drag steps on VGA**, on every window carrying the flag. Verified from `W_X` 55 —
a 3px and a 5px drag land back on 55, 9px steps to 63, 14px to 71, every landing
with an aligned content origin.

### Set 53 — alignment becomes the default, and a stamp that never worked

Set 52 removed `WF_SNAP`'s `[vid_mono]` gate. This inverts the flag: `WF_NOSNAP`
(SPEC.md §11.94.1), so **every window keeps its content origin on a multiple of
8 unless it opts out**, and `wm_create`'s `mov word [bx+W_FLAGS], 1` is the whole
mechanism — no template word, nothing to remember where a window is made, and a
reused record snapped again by construction.

**The published slot did not change contract**, which is what kept the cost at
zero: `OSAPI_WM_SNAP` still means "AL non-zero = keep me aligned", so every
historical `AL = 1` caller now asks for something it already has and no `.o88`
was invalidated. What moved is only what a window that never calls gets.

**Measured, all three adapters, cycle-accurate 8088.** Windows that had never
asked and are now aligned:

| window | content origin before | after |
|---|---:|---:|
| Disk window (VGA) | 111 (≡ 7) | 104 |
| Disk window (Hercules) | — | 104 |
| Control Panel (Hercules) | — | 160 |
| About box (Hercules) | — | 168 |
| Note Pad (VGA) | 61 (≡ 5) | 56 |

**The Disk window is the case that justifies the inversion, and the scroll path
is the thing to check.** §22.11's `fm_scrollpaint` rounds its blit span INWARD
because a row's icon starts at `fm_cx + 4`, and the strip it cannot move is
`(8 − ((k+4) mod 8)) mod 8` px for a content origin `≡ k (mod 8)` — so aligned,
`k` is always 0 and the strip is always 4px, where unaligned `k = 4` came up one
position in eight and cost nothing. §22.11.1's strip pass therefore now always
runs. Against that: the ~40 `font_str`s at `fm_cx + 24` all land aligned.
Verified — three arrow-clicks of scroll, then the same state forced through a
full repaint (drag out 40px and back, which a snapped window returns from
exactly): **0 differing framebuffer bytes on VGA and on CGA**, the only diff
being the menu bar clock's last glyph cell.

**And it found a Makefile bug that has been live for most of this tree's life.**
`$(VIDSTAMP)` exists so that changing a knob rebuilds the kernel — the trap
CLAUDE.md names for `VIDEO=`, where booting the previous adapter "reads exactly
like the probe being broken". Its `rm -f` list covered `kernel.bin`, and
`kernel.bin` **is not assembled from source**: `os88mod.py` splits it, `ctrl.drv`
and `format.drv` out of `kernel-full.bin` (SPEC.md §2.8), and `kernel-full.bin`
depends on the sources alone. A knob changes the command line and no source — so
deleting only `kernel.bin` re-ran the split on the PREVIOUS knob's
`kernel-full.bin`, and `make VIDEO=cga` after a plain build shipped a
`KERNEL.SYS` and two `.drv` files with no CGA in them. Silently.

Found by accident and measured both ways: an incremental plain rebuild after
`make SNAPAUDIT=1` produced an image **39,504 bytes** different from a clean one;
with `kernel-full.bin`, `ctrl.drv` and `format.drv` added to the stamp's list it
is **0**. Every VIDDEF knob was affected — `VIDEO=`, `RTC=`, `DISKCNT=`,
`REDRAWFULL=`, `DRAGCACHE=`, `NOSPLIT=`, `FONT=`, `RAMKB=`. **Any A/B in this
tree taken by flipping a knob without `make clean` in between is suspect**, and
that includes ones whose numbers are already in this file.

**`make SNAPAUDIT=1` is the new instrument** (SPEC.md §11.94.2): a histogram of
every glyph's `x & 7`, split four ways by `wm_pkgcall`'s stacked `snap_cur` so
what a window draws inside its own callback is counted apart from the chrome
around it, and filterable to ONE window from the host. It only means anything
now that alignment is the default — with the origin on a multiple of 8, a
glyph's screen `x & 7` IS its content-relative one, so a non-zero bucket is the
app's own offset. §11.94.3 is what it and the constants together found.
**It has an unexplained artifact**: a constant 4 glyphs in bucket 7 per window
per forced repaint, so counts under about ten say nothing.

### Set 54 — snapping in Y buys nothing, and the scroll recomputes what it could add

The Y question was left open by Set 53 and `docs/SNAP-PLAN.md` §6. It is
answered: **a `WF_SNAP` in Y would gain nothing on any adapter**, and the
investigation redirects to something that does.

**Why text has exactly zero y-dependence.** The vertical layout is SPEC.md
§39.3's banked framebuffer — VGA 1 bank (`bmask` 0, `rowadd` +80), Hercules 4
(`bmask` 3, `rowadd` +0x2000, `wrapfix` +0x805A), CGA 2 (`bmask` 1, `wrapfix`
+0xC050) — and `gfx_nextrow` pays one extra `add` only on the step that carries
out of the last bank. **A glyph is 8 rows and 8 is a whole multiple of both 2
and 4**, so among the 8 advances an 8x8 cell makes, the number that wrap is
exactly 8/banks wherever the cell starts: among any 8 consecutive rows, exactly
4 have `y & 1 == 1` and exactly 2 have `y & 3 == 3`. Not approximately — there
is nothing to recover. On VGA there are no banks at all. This is the opposite
of the x case, where §6.1.4's arithmetic showed an unaligned cell spans two
framebuffer bytes and must read-merge-write both; there is no vertical
equivalent, because a row is a row whatever y it is.

**Fills already solved the row-step problem, and note HOW.** `sw_plane_op`
holds `gfx_nextrow`'s two parameters in registers so a row step is three
register instructions (§5.7) — by holding the increment, not by constraining y.
Measured confirmation, and it is a pleasing one: **a breakpoint on
`gfx_nextrow` during a Hercules desktop paint never fires**, because every row
loop that matters has inlined it.

**What the investigation found instead.** `gfx_scroll`'s two backends compute a
full row address PER ROW for an address that advances by a constant —
`vgas_lincopy` two `mul word [vid_stride]`, `vgas_bankcopy` two `gfx_rowbase`
calls, each a `mul` plus a variable `shr` plus a bank-table lookup plus
call/ret. Measured, cycle-accurate 4.77MHz 8088, Hercules:

| | measured |
|---|---:|
| `gfx_rowbase`, one call | **11 instructions, 319 cycles = 66.84 µs** |
| `GFX_SCROLL 256x128` | **51,229 µs** = 400 µs/row for 32 bytes |
| `GFX_FILL 256x128`, same bytes, register row step | 24,338 µs = ~185 µs/row |
| `GFX_SCROLL 256x128` on VGA | 29,096 µs = 227 µs/row |

The `gfx_rowbase` figure is a breakpoint-and-step measurement, not a model: two
of them are **134 µs of the mono scroll's 400 µs per row, a third of it**, and
the fill doing comparable per-row work costs less than half as much per row.
Estimated **~1.45x on mono** (51,229 → ~35,000 µs) and ~1.3x on VGA if the
scroll adopts the fill's shape. Cost it against §48.18.1 before building — that
one recovered 4% and was dropped — but this is per-row rather than per-call, so
it scales with the band's height.

**And this is where a vertical quantum finally earns something — `nbanks`, not
8, on the scroll DELTA rather than a window origin.** The constant-delta trick
needs `rowbase(y+dy) − rowbase(y)` equal for every row. On VGA that is
unconditional (linear: `dy × stride` for any `dy`), so the VGA backend can drop
its per-row multiplies today with no precondition at all. On the banked
adapters it holds iff **`dy ≡ 0 (mod nbanks)`** — a multiple of 2 on CGA, 4 on
Hercules. The tree's deltas already qualify (Note Pad's 32px row step, the Disk
window's 16px `FM_ROW_H`); the one exception is Note Pad's find panel, whose
29/41px shift §27.10.2 chose deliberately. Even unaligned, the general path can
halve its arithmetic by walking the destination with the register step and
paying `gfx_rowbase` for the source alone.

**So the lever is the increment, not the origin** — which is the same shape as
§5.7's answer and as `sw_plane_op`'s: on this machine you win by not
recomputing, not by constraining where things sit. An 8px vertical drag quantum
would have been the visible price of the wrong answer, and a dearer one than
the horizontal quantum: a window steps more noticeably on a 348-row screen than
on a 720-column one.

### Set 55 — the drag cache's phase snap is now a no-op, and that is the win

§11.96.13 added `ui_drag_phase` because the drag cache can only replay pixels at
a `dx` whose low three bits are 0, and a hand drops a window on an arbitrary
pixel — so seven drags in eight refused and redrew in full. It bought that with
up to 7px of horizontal drop precision "paid by every window on every adapter".

**Set 53's inversion has since made it free, and the two pieces of work were
done independently.** Alignment is the default now, so `wm_snap_ax` puts `W_X`
on `≡ 7 (mod 8)` before `ui_drag_phase` runs and `ui_origx` is on it too: every
legal x a drag moves between is one phase, so `dx` is a multiple of 8 **by
construction**. Measured at the routine's own entry with a breakpoint, a
13-pixel drag of a Disk window:

| window | `W_X` | `ui_origx` | `dx` | `dx & 7` | phase snap |
|---|---:|---:|---:|---:|---|
| default (snapped) | 111 | 103 | +8 | **0** | **moves nothing** |
| `WF_NOSNAP` poked in | 124 | 111 | +13 | 5 | moves it 5px |

Identical on `os8088_5150_herc` and `os8088_xt_vga`. The counterfactual is the
half that makes it evidence rather than a coincidence: the flag was written into
the live window record from outside the guest, and the same drag then behaved
exactly as §11.96.13 described the whole tree behaving.

So the drag cache reaches its fast path **structurally** rather than by
correcting each drop, §11.96.13's 7px is a cost no window in the tree pays, and
the routine is now the fallback for the two cases `wm_snap_ax` declines — an
opted-out window, and one too wide for the snap to fit. **Keep it**: in both,
the phase really is a coin toss. And keep `ui_drag`'s ordering, because
`ui_drag_phase` above `wm_snap_ax` would bring the coin toss back.

**Two gates, and only one of them is an alignment question.** §11.96.13 is a
horizontal byte phase and generalized snapping subsumes it. §11.96.14 — a
window's vertical extent running off the screen — has no quantum in it at all:
there is no sub-row unit vertically (Set 54), so no snapping in x, y or
otherwise moves it, and a Solitaire whose 303 rows do not fit a 305-row band is
not made to fit by aligning anything. Clipping was the only answer. Conflating
the two is easy enough that both sections now say which they are at the top.

### Set 56 — the scroll stops recomputing its row address: 1.49x and 1.52x

Set 54 found `gfx_scroll` computing a full row address for both ends of every
row when the address advances by a constant, and priced the waste with a
breakpoint: `gfx_rowbase` is 11 instructions and **319 cycles = 66.84 µs** on a
Hercules, so two calls were 134 µs of a 400 µs row. Built (SPEC.md §5.5.1).

| `GFX_SCROLL 256x128` | before | after | |
|---|---:|---:|---:|
| Hercules | 51,229.07 µs | **34,471.99 µs** | **1.49x** |
| VGA | 29,095.95 µs | **19,194.37 µs** | **1.52x** |

400 → 269 µs a row on Hercules against the 134 µs the change removes, so the
saving landed where the measurement pointed. **VGA beat its own estimate** —
~1.3x predicted, 1.52x measured — because the two `mul`s were a bigger share of
a shorter per-row cost there, and the rewrite also took `cld` and two adds out
of the loop.

**`GFX_FILL 256x128` reports byte-identical counts in both runs** — 174,235 on
Hercules, 100,611 on VGA — which is the control that makes the comparison mean
anything: the fill was not touched, so two runs agreeing on it are two runs that
can be compared on the scroll.

**`make SCROLLROW=1` is the reference build, and it reproduces the previous
kernel's image byte for byte** (md5 47074a3c…, the same image shipped at
9abacfa) — which is how the A/B proves the reference arm really is the old code
rather than something that merely resembles it.

**A scroll is a COPY, so byte identity is the whole claim**: a wrong delta shows
the wrong pixels, not no pixels, and "it looks right" is exactly what a wrong
offset also looks like. Verified against `SCROLLROW=1` on identical scripted
sessions:

| adapter | what it drives | steps | result |
|---|---|---:|---|
| VGA | Note Pad caret scroll, both directions + page jumps | 6 | **0 differing bytes** |
| Hercules | the same | 6 | identical md5 at every step |
| CGA (2 banks, not 4) | Disk window row scroll + page jumps | 5 | identical md5 at every step |
| Hercules | Note Pad's find panel — `dy` 29 then 41, **UNALIGNED** | 4 | identical but for the clock's last glyph cell |

That last row is the one that matters most for the banked fast path: it is the
tree's only scroll whose `dy` is not a whole number of banks, so it proves the
dispatch still falls to the general form instead of taking a constant delta that
does not exist there.

**One bug, and it is SPEC.md §5.7.1's rule arriving from the other side.** The
first version stepped the destination with `add di, stride` and was wrong on
every adapter — **`rep movsb` has already advanced DI by the row's byte count**,
and the reference form never had to care because it recomputed DI from scratch
each row. 14,580 differing bytes on the very first VGA capture. The fix is free
on the linear path (fold `-nb` into the increment, which is computed once) and
one `sub di, [vgas_nb]` on the banked one, where `gfx_nextrow` is
column-preserving and so has to start from the column it is preserving. **The
A/B is what caught it**: the pixels were plausible, the window looked like a
window, and nothing errored.

**Cost: `.text` +116, no rung crossed — but the image rung is down to 48 bytes
of slack** (164 before). The next addition to `.text` anywhere buys a whole
sector, which is the guard working rather than this change being dear.

### Set 57 — the audit's "artifact" was a window CAPTION, and the tool was right

`make SNAPAUDIT=1` carried a documented unexplained artifact: every window's
callback reported a **constant 4 glyphs in bucket 7** whatever the app, which
made any count under about ten worthless and left Piano, Arkanoid and Missile
Command unresolved in `docs/SNAP-PLAN.md`.

**It was `wm_draw_title`.** A caption's pen is **centred in the title bar by the
kernel** and no application can influence it, and it was being attributed to
whichever `wm_pkgcall` bracket happened to be open. The constant 4 was the
**`'APPS'` caption of the Disk window sitting behind every app under test** —
four characters, identical in every run, which is exactly why it read as an
instrument fault rather than as data. `wm_draw_title` suspends the attribution
now, keying on who *chose* the pen rather than on who is on the stack.

Measured after: the audited window reports **nothing** of its own and
`snap_kchar` grows by exactly the two captions — 151 → 165, gaining `[3:10, 7:4]`
for `'ArtfulType'` and `'APPS'`.

**What made it findable was logging the CHARACTER**, and two ways of reading a
stack produced confident nonsense on the way there:

- The caller's `AX` is at `[ss:sp+2]`, not `[ss:sp]` — the latter is the saved
  `BX`, which is **constant inside `font_str_x`'s loop** and therefore looks
  exactly like a real answer. Every logged character came out `'R'`.
- A return address must be resolved in **`KERNEL_SEG` only**: `.cold` code
  cannot near-call `font_char`, it goes through a far `cw_` shim (SPEC.md §2.6),
  so resolving against `.cold` too picks a nearer symbol by accident — it named
  `dskw_rbody.fatname`, a *disk* routine, as the thing that drew a glyph.

Both are the same shape as this session's other traps: **a wrong reading that is
stable is indistinguishable from a right one**, and only a value that had to
vary — the characters spelling `APPS` and `ArtfulType` — could tell them apart.

**Three corrections to §11.94.3's list fall out**, and they are corrections in
the direction of less work rather than more:

- **Hello is not a defect at all.** `hl_line` centres every string —
  `(HL_CONT_W - width)/2 + content_left` — so it is §3's category 2. Its earlier
  "≡ 3 for 5 of its 35 glyphs" was its caption plus that centring.
- **ArtfulType's "≡ 3 (measured)" was its own caption**, ten centred glyphs the
  kernel drew. Its literal 14 stands; re-audit before moving anything.
- **Fractal is confirmed and is the worst in the tree**: 2,801 glyphs in one
  launch, **16% aligned, 2,323 of them at bucket 2** — the status row redrawn per
  progress tick, at the `FR_X_*` constants the static pass had already named.

**And a note for whoever finishes the list: a drag no longer forces a repaint.**
§11.96.12's cache replays the content instead of calling `W_PAINT`, so a dragged
window reports nothing — which is what the first version of this survey
measured. Reset with no filter and then LAUNCH the app.

**Cost: nothing.** All of it is inside `%ifdef SNAPAUDIT`, and a plain build is
**0 differing bytes** against the same commit before this work.

### Set 58 — the Disk window and Fractal go on the grid, and one of them was not an alignment problem

Emulator: MartyPC, cycle-accurate 4.77MHz 8088, `os8088_5150_cga` /
`os8088_5150_herc` / `os8088_xt_vga`. Two items off docs/SNAP-PLAN.md's list,
and the second is the interesting one.

**The Disk window (SPEC.md §22.11.1.1/§22.11.2, seven constants + two): 0 bytes.**
`.text`, `.bss`, `.cold`, `.lowbss`, `.ovl` all **+0**, no rung crossed,
`KERN_BUDGET` spare unchanged at 3,072 — every one of the nine is an
`add`/`sub reg, imm8` at both the old value and the new. What moved: the header
pen and the status line's 6 → 8 (with their truncation constants 14 → 16 and
12 → 14), the row icon 4 → 8, the row name 24 → 32, the name budget
`(cw-88)/8` → `(cw-96)/8`, `fm_scrollpaint`'s strip test 4 → 8, `FMI_CELL_W`
78 → 80 and the grid icon `fm_cellx + 31` → `+ 32`.

**Two things the numbers could not see, and one of them was in this tree's own
notes.** docs/SNAP-PLAN.md said an aligned content origin makes
`fm_scrollpaint`'s left strip *always* 4px wide, so §22.11.1's strip pass runs on
every scroll; that is backwards — `fm_bx1 = align_up(fm_cx)` and an aligned
`fm_cx` already is a multiple of 8, so the strip is **empty** and §11.94.1's
default had retired that pass already (it used to fire on three window positions
in eight, not one). What 4 → 8 actually buys is that `ico_pass` lands each
16-pixel icon row in a **three**-byte window at a shift of `x & 7` and at shift 0
the third byte is always zero and skipped — two latched read-modify-writes a row
instead of three, on both the mask and the data pass — plus the pass becomes
unreachable at *every* window position rather than only the aligned ones. In the
grid that argument is worth more: the columns started at 0, 78, 156 = **0, 6, 4
mod 8** and the icon's own centre was 31 = **7 mod 8**, the worst phase there is.

And moving the icon to +8 with the name still at +24 put the 16px icon cell's
right edge **exactly on the name's first letter** — `ARTFUL.O88`'s A against the
app diamond. Both numbers are multiples of 8, the kernel is 0 bytes bigger, and a
four-row scroll stayed byte-identical to a full repaint on all three adapters:
**nothing in the verification recipe could see it.** A 5x crop could. That is now
a rule in docs/SNAP-PLAN.md §5 — look at the pixels that moved, every item.

**Fractal (SPEC.md §40.2.1): 2,557 glyph cells → 565, 4.5x — and it was not an
alignment item.** Set 57 put it first of what was left (2,542 glyphs sampled,
12.5% aligned, 2,222 in bucket 2). The reason those glyphs existed is that
`fr_status_maybe` runs up to a hundred times a render and called the FULL strip
painter each time: a white fill plus five re-lettered fields, to move one digit.
Nothing but the percentage can change while a render runs — type, zoom and
palette all go through `fr_kick` — so a tick now draws ONE field as one opaque
`font_run` padded to `FR_PCT_CELLS`, and the five pens became multiples of 8
(`FR_X_NAME` 8, `FR_X_ZOOM` 128, `FR_X_ZNUM` 168, `FR_X_PCT` 200, `FR_X_PAL`
248).

Measured with `make SNAPAUDIT=1`, **one byte-identical kernel and two apps
images** so the package is the only variable, over one `View ▸ Redraw` render:

| | `font_char` calls | `font_run` calls | glyph **cells** | aligned |
|---|---|---|---|---|
| before | 2,542 | 3 | **2,557** | 12.5% |
| after | 50 | 103 | **565** | **100%** |

The cells column prices a `font_run` at its width (5) rather than at one call,
because the raw totals would flatter the new build by counting a five-cell run as
a single glyph. Off-grid glyphs 2,222 → **0** — not aligned, *gone*, which is the
better of the two outcomes. ~100 `gfx_fill`s a render go with them. Package
+79 bytes of heap; **0 bytes of kernel**.

**The two halves are one change.** `font_run`'s single-store cell path needs
`[vid_mono]` *and* `x & 7 == 0` (§6.1), so on the adapters this app is slowest on
the aligned pen is what makes the opaque run pay — and `snap_hrun` reported all
103 runs in bucket 0, which is what says the fast path was reached rather than
merely permitted.

**These are COUNTS, under `-icount`-equivalent conditions on a cycle-accurate
8088 — not microseconds on iron.** Part 2's ~1ms per glyph cell puts 2,557 cells
at ~2.6 s of drawing per render against ~0.57 s, and ~100 fills at ~756us each at
another ~76 ms; treat those as arithmetic from measured constants, not as field
figures. The 5150 is where they would land.

**Two harness traps, both of which produced a confident wrong answer:**

- **`m.advance()` runs a bounded amount of guest time and STOPS.** A poll loop
  that ends in one leaves the machine paused, and a paused guest processes no
  UART packets — so every later `os88mouse` call fails with "stuck at" its
  starting position, which reads exactly like a broken hit-test. One `m.run()`
  between the loop and the first click.
- **A stash-for-reference build leaves `build/` holding the REFERENCE.** Two
  scratch images copied after `git stash pop` but before the rebuild were the old
  package, and the run reported `image 3138` — the ref's size — while claiming to
  test the new one. Related and worse: `cp file backup` run twice, the second
  time after the file was already modified, captured the change the restore was
  meant to undo, which is how b3c5219 shipped half of the grid change
  undocumented. **Rebuild after the pop; never reuse a backup path.**

### Set 59 — Note Pad's panel goes on the bank quantum; Tracker is measured and left alone

Emulator: MartyPC, cycle-accurate 4.77MHz 8088, `os8088_5150_cga` /
`os8088_5150_herc` / `os8088_xt_vga`. Two more items off docs/SNAP-PLAN.md, and
the second is a **negative result** — the more useful of the two.

**Note Pad's find panel (SPEC.md §27.10.3): 0 bytes.** §27.10.2 makes opening or
closing it one `OSAPI_GFX_SCROLL` of the whole text band, and the delta that blit
is handed *is* the panel's height — so the height decides which of `gfx_scroll`'s
two paths runs. Set 56's constant-delta path is gated on
`dy & [vid_bmask] == 0`, and `bmask` is **3 on Hercules and 1 on the CGA**, so 29
and 41 missed it on both. `NP_FP_H` is 32 now, 44 with Replace.

SNAP-PLAN called this "one character" and it is not: `2*NP_FP_ROW +
2*NP_FP_PAD + 1` is odd for **every** value of either constant, so the correction
has to be explicit (`NP_FP_SLACK = (-NP_FP_RAW) & 3`). Verified 0 differing
pixels against a forced full repaint on CGA and Hercules.

**Tracker (SPEC.md §45.19): measured, and NOT changed.** §11.94.3 had it as the
worst offender in the tree and SNAP-PLAN ranked it near the top. `make
SNAPAUDIT=1`, one forced full repaint, histogram **filtered to Tracker's own
record**:

| adapter | glyph cells | aligned | off-grid buckets |
|---|---:|---|---|
| Hercules | 237 | 26.2% | 1:16, **5:159** |
| VGA 12h | 354 | 39.3% | 1:16, 4:40, **5:159** |

Four things the literal-pen sample could not see. Every value the playing view
updates goes through `tui_rdout`, which **already rounds its pen down to a byte
boundary when `[tui_mono]` is set** — so the frequently-drawn text is on
`font_run`'s single-store path whatever the caller's constant says.
`tui_top_cga`'s labels are already at 0/64/136/200/288. What is left is
`tui_draw_all` — **event-driven**, where this plan claimed the app "redraws
continuously while playing"; `tui_draw_dyn` draws only what changed against a
`tui_l*` shadow. And 159 of the off-grid cells are `tui_s_logo` at 149, which is
exactly `112 + (179-104)/2` — the centring of `'T R A C K E R'` in its
112..290 box, a protected kind.

Priced: alignment shaves a cell rather than removing it — Set 52's 3.4% on
Hercules, 9.4% on VGA — so all 175 off-grid Hercules cells are worth **~6 ms of a
~237 ms repaint, on an event**. Against Fractal (Set 58): 2,557 cells a hundred
times a render, where the fix *removed* 78% of the glyphs. **Two orders of
magnitude apart, and that is the whole decision.**

What the measurement does point at is costed in §45.19 and not taken:
`tui_rdout` deliberately keeps the erase-and-letter pair on colour, so VGA's
per-frame values are drawn unaligned on the adapter where alignment is worth
2.8x more — but fixing it is a refactor of that routine, not a constant.

**THE MEASUREMENT ITSELF WAS WRONG TWICE, both in the flattering direction.**
`os88snap.reset(m)` with no argument aims the filter at **every** window, so the
first census read 487 cells that included the About panel being used to force the
repaint and the Disk window behind it — an unknown share of a number reported as
Tracker's. And the caller log's first 24 glyphs spelled `'os8088 1.0'`, which is
not on the FT2 face at all: a **windowed** Tracker shows its splash card, and the
pens §11.94.3 lists are on the fullscreen face. Filtering to Tracker's own record
(`wm_wins - 0x600 + slot*28`) gave the numbers above.

**And the row-index trap cost four runs in one session**, which is enough to call
it a rule: a fixed number of `ArrowDown`s plus a fixed row opens a *different
package on every adapter*, because `[fm_fit]` is ~5 rows on a 200-line CGA and 8
on Hercules. It opened ArtfulType for Fractal, row 7 for piano, MODPLUG for Note
Pad and PAINT for Tracker — each time silently, each time looking like the app
under test was broken. Read `[fm_lscr]` and `[fm_fit]` out of the kernel and take
the row the wanted directory index is actually on.

**One more pointer fault, same family as Set 58's.** The find-panel check
reported 80 differing pixels and "MISMATCH": they were the mouse arrow in two
places — `mo.menu` releases it in the menu bar, the About close box is inside the
window — and the union of those two cells is exactly the bbox reported, which
reads precisely like a blit that moved a band of text. The reference build scored
83 in the *identical* bbox, and that is what proved it was the instrument.

### Set 60 — Tamegram's HUD goes on the grid; Paint was already there

Emulator: MartyPC, cycle-accurate 4.77MHz 8088, `os8088_5150_herc`. docs/SNAP-PLAN.md
listed these as "one constant table each". One of them was one constant table.
The other needed nothing at all.

**Tamegram (SPEC.md §49.5.1): 0% → 100% aligned, 0 bytes.** Its HUD columns were
4, 60, 108, 164, 116, 180 and 210 — **six of the seven at 4 mod 8**, the most
uniformly off-grid face in the tree and the cheapest to correct. They are named
constants now (`TG_HC_*`), with one `%error` that `OR`s all seven and checks a
single bit pattern, so a column added later cannot quietly miss the boundary
(proved to fire at 118).

Measured over **900 frames = 17.7 s of real play**, histogram filtered to
Tamegram's own window record:

| | glyph cells | aligned |
|---|---:|---|
| before | 109 | **0.0%** (all in bucket 4) |
| after | 109 | **100.0%** |

Same cell count, same span — only the phase moved, which is what an alignment
change should look like. **The worth is small and is stated as such**: ~6 cells a
second is 0.6% of the machine at Part 2's ~1ms a cell, and alignment saves 3.4%
of that (Set 52). Taken because it is free, because it puts the face on the same
8px margin as the rest of the tree, and because leaving the last
uniformly-fixable entry on the survey invites the next reader to re-derive the
census. §49.5 is why the time is not the reason: a PLAY frame draws **no text at
all** (four cells erased, four drawn), and the HUD is redrawn only when
`[tg_full]` is raised — measured at about once every eleven seconds at the
opening fall speed.

`LOCK` moved 210 → **208**, left by 2 rather than right by 6: right would have
ended row 1 at exactly `TG_HUD_W`, leaving the minimum-width case with no margin.
The widest row is 240 of 248 now, where it was 242. Verified by crop rather than
by count, because `tg_str` **drops** a string that would leave the content box
rather than clipping it — so a column moved too far right fails by a field being
*absent*, which a diff reports as "fewer pixels changed".

**Paint (SPEC.md §42.12): nothing to do.** Measured, a full repaint draws **62
glyph cells, 100% aligned**. `PT_PAL_X0` = 1 is not a text pen at all — it is the
tool palette's left column, and a palette is fills, frames and blitted icons, so
this plan's "drawn on every tool change" costs no glyph. The off-grid pens in the
source are real but fire only on paths a repaint does not take: `pt_draw_dims`'
`.plain` fallback (a window too short for the size boxes) and `pt_szdraw` (typing
into the canvas-size boxes).

**And that field's pen cannot be aligned — geometry, not oversight.** `PT_SEPX`
is 43, so the label and the framed field share 43 pixels; an 8px `W`/`H` label
plus `PT_SZ_BW` = 32 is 40 of them, fixing the label at 0..7 and the frame at
8..39, and the pen is 2px inside the frame by construction — 10, which is 2 mod
8. A multiple of 8 needs `PT_SZ_BX` ≡ 6, and the nearest value clearing an 8px
label is 14, whose frame ends at 45, past the rule. Moving the pen deeper inside
the frame costs the caret its column.

**One harness note.** `settle()` cannot be used past Tamegram's launch — it is a
falling-block game with a worker, so the screen never stops changing and settle
raises after its limit. `m.advance(frames=N)` is both the fix and the better
instrument: what matters is not one repaint but how much text the game draws per
second, and a fixed frame span answers exactly that. And `os88sym` refused a stale
map when `build/kernel.bin` had been rebuilt without the knob — the guard working
as designed, and the reason to `make SNAPAUDIT=1` immediately before an audit run.

### Set 61 — the survey's tail, walked in one batch: one change, six reasons not to

Emulator: MartyPC, cycle-accurate 4.77MHz 8088, `os8088_5150_herc`. The last
entries in docs/SNAP-PLAN.md §2 — the Task Manager, Recorder, Minesweeper, the two
HDD pages, Piano, Arkanoid, Missile Command. **One change came out of them**, and
the reasons the others did not are the useful part.

**Task Manager (SPEC.md §28.5): four pens, 0 bytes — and the plan said five.**
Three of the "five literal `+6` sites" are `add ax, 6` on **frame rects** — the CPU
graph's frame, the bar's, the RAM map's — and a rect has no glyph phase. The four
that are text pens are the memory page's XMS line and the heap page's
TOTAL/SPLIT/FRAG summaries, one `font_str` each from `tm_str`, each gated by
`tm_elchk`. They sat at 6 while `TM_PEN` is 8 and `TM_MPEN` is 16, giving this
window a third inset of its own; they are at `TM_PEN` now. `tm_rowfill` still
erases the band from `rowx+6`, and those two calls plus the `font_str` are the
only operations on it, so **what moves is two erased columns nobody letters**.

**Minesweeper: its off-grid text is CENTRING, and it is the app's densest.** The
number on every revealed cell is `add cx, 4` — an 8px glyph in a 16px cell, which
is `(16-8)/2` exactly. Aligning it would sit every number on the grid 4px
off-centre in its square. The mode text is `MN_BOARD_W/2`. A two-glyph mine
counter is all that is genuinely off-grid.

**Piano: largely not a constant.** `PN_MSG_X` is 112, already aligned. The key
letter is `key_x + 5`, and since §11.98.1 `key_x` comes from `pn_metrics` scaling
the keyboard into whatever content height the window has — so **no constant can
align it on any adapter**, and the +5 centres the glyph in a key whose width is
derived. One label at `cx, 2` on a full repaint is the whole of that row, which
means SNAP-PLAN's "Piano | 2" was stale the moment §11.98.1 landed.

**Arkanoid: four of six sites are `OSAPI_FONT_WIDTH`-centred**, a fifth is a
letter centred in a power-up body. The sixth is the score at `cx, 2`.

**Recorder** is two strings in `rc_draw_status`, whose only caller is
`rc_draw_all` — repaint-only. **The HDD pages** draw on open and on a click.
**Missile Command** computes every pen, and Set 46's `mc_srun` already reduced its
status strip to the span between the first and last differing cell.

**WHAT THE WHOLE SURVEY TURNED OUT TO BE**, across Sets 52–61. Three entries
mattered:

| item | what it really was | measured |
|---|---|---|
| Fractal (§40.2.1) | a **redraw defect wearing an alignment costume** | 2,557 cells → 565, **4.5x** |
| Disk window (§22.11.1.1/.2) | genuinely alignment — the ~40 rows that dominate `fm_repaint` | 0 bytes, 9 constants |
| Tamegram (§49.5.1) | genuinely alignment, uniformly — 6 of 7 pens at ≡ 4 | 0% → **100%** aligned |

Every other entry was one of four things, and **a literal-pen grep cannot tell any
of them from a defect**: *centring* (protected — Hello, Minesweeper's cell
numbers, Arkanoid's four, Tracker's logo, Tamegram's banner, Paint's About card),
*not a text pen at all* (three of the Task Manager's five; Paint's `PT_PAL_X0`),
*geometry derived at run time* (Piano's keys), or *a handful of cells on an event*
(Recorder, the HDD pages, Paint's size box, Tracker's whole face). That is 11 of
14 entries, and each one had to be censused to know which.

**The two rules the survey earned**, both of which cost a round before they were
written down. *Ask how often a pen is drawn before moving it* — Fractal's 78%
saving came from not drawing, not from aligning, and Tracker's 237 event-driven
cells looked identical to it in a grep. And *look at a zoomed crop*: a 0-byte,
byte-identical, fully-aligned change still made a window worse when the Disk
window's icon landed on its own label.

**One harness note.** The Task Manager cannot be driven with the pointer — it
refreshes twice a second and holds the gfx lock, so 1200-baud packets never
converge (measured: stuck 139px short, the same failure Fractal's worker
produces). Its pages cycle on a click in the content, so `[tm_view]` was written
from outside the guest instead, with the offset taken from nasm's own `[map]` via
`tests/dispapps.py` rather than counted by hand. That poke skips `tm_click`'s
erase fill, so the capture shows the outgoing page underneath — the summary lines
themselves are the evidence, and the two-pixel claim rests on `tm_rowfill`'s span
rather than on the picture.

### Set 62 — the first VGA field set, off 5150 #2

**Machine: `Elendilon/os8088`'s IBM PC 5150 #2** (docs/FIELD-MACHINES.md) —
stock 8088 at 4.77MHz, 640KB, **PVGA1A-JK VGA, 256KB**, mode 12h at 640x480x4.
Not the calibration machine and deliberately not period: its storage is a
Picomem, so **no disk row from it belongs in Part 2**. Drawing is another
matter — the CPU is a stock 8088 and the card is a real PVGA1A on a real ISA
bus, and nothing in the drawing path touches the Picomem. **These are the
first VGA numbers this project has ever had off real hardware**; every
calibrated figure in Part 2 came off Hercules and CGA.

Taken after the machine's colour fault was repaired (docs/FIELD-NOTES.md 24.1.3),
on a session with no corruption in it.

| row | 5150 #2, VGA | Part 2, mono (5150 #1) |
|---|---|---|
| `GFX_PIXEL` | **505.4 us** | ~756 us fixed per call |
| `GFX_FILL 8x8` | **593.5 us** | — |
| `GFX_FILL 64x64` | 3,922 us | — |
| `GFX_FILL 256x1` | 620.5 us | — |
| `FONT_CHAR one cell` | **620.4 us** | ~1 ms |
| `FONT_RUN 10 aligned` | 7,029 us | — |
| `FONT_RUN 10 skewed` | 7,882 us | — |
| `GFX_UNLOCK+LOCK pair` | 316.3 us | — |
| `WM_CLIP_TEST` | 71.5 us | — |
| boot | **155 ticks** (8.5 s) | 181 (Set 18) |

**The headline is that VGA is FASTER per call than the mono adapters**, which
is the opposite of the assumption a reader would carry in from Part 2's "756us
of fixed cost" and worth stating plainly. A planar write puts eight pixels
across four planes into one store through Set/Reset, where the 1bpp renderer
does a read-modify-write for anything unaligned; the per-call floor lands at
~505 us against mono's ~756, and a glyph cell at 620 us against ~1 ms.

**What this does NOT license.** Part 2's constants stay as they are: they are
the *calibration* machine's, on the *target* adapters, and SPEC.md's redraw
budgets are written against them. This set is a second column, not a
replacement — and the honest reading of it is that **the mono adapters are the
slow case and sizing against them is still right**.

**Alignment shows here too**: `FONT_RUN 10 aligned` 7,029 us against `skewed`
7,882 — **12.1%**, which is within a whisker of §11.94's `gfxbench`-under-icount
figure of 12.5% for the same pair. An independent confirmation on iron of a
number that had only ever been counted in instructions.

### Set 63 — a STRADDLED window, priced on iron, and what it costs

Same machine and session as Set 62 (5150 #2, VGA primary + Hercules extended
right), with the `gfxbench` window **across the seam** — `displays 2`,
`sandbox display 0`, **`sandbox straddles 1`**, which the report says itself.
The first measurement of SPEC.md §39.14's per-display split on real cards.

| row | VGA alone | straddled | x |
|---|---|---|---|
| `GFX_PIXEL` | 505.4 us | 930.1 us | **1.84** |
| `GFX_FILL 8x8` | 593.5 | 1,036.1 | 1.75 |
| `GFX_FILL 64x64` | 3,922.6 | 4,478.0 | 1.14 |
| `GFX_FILL 256x1` | 620.5 | 2,437.3 | **3.93** |
| `FONT_CHAR one cell` | 620.4 | 851.1 | 1.37 |
| `FONT_RUN 10 aligned` | 7,028.8 | 8,715.5 | 1.24 |
| `GFX_BLIT4 solid` | 39,498.8 | 164,777.2 | **4.17** |
| `GFX_UNLOCK+LOCK pair` | 316.3 | 322.8 | 1.02 |
| `WM_CLIP_SET+CLEAR` | 549.9 | 574.2 | 1.04 |

**The shape is the finding, not the ratios.** A straddling primitive is issued
twice — once per display, through `gfx_disp_run` — so a *small* operation pays
close to double (`GFX_PIXEL` 1.84x, `GFX_FILL 8x8` 1.75x) because it is nearly
all fixed cost, and a *large* one pays much less (`GFX_FILL 64x64` 1.14x)
because the per-scan-line work is split between the two halves rather than
duplicated. Anything that does not draw is untouched: the lock pair and
`wm_clip_set` are within 4%, which is measurement noise.

**The two outliers are the two that change SHAPE, and both are documented.**
`GFX_FILL 256x1` at 3.93x is a 256px-wide row against a 640px display: split
at the seam it becomes two fills of a few dozen pixels each, so it lands on the
per-call floor twice and loses the whole benefit of being one wide rect.
`GFX_BLIT4` at 4.17x is SPEC.md §39.14.7.1's gate doing exactly what it says —
a straddled blit gives up §5.4.1's fast path and every coalesced run becomes a
`gfx_fill`, which is the trade that section made deliberately after the
extended-desktop Paint regression.

**None of this is a number to design against**, and that is worth stating
plainly: a straddled window is a transient state a user creates by dragging,
not a configuration anything runs in. What the set is for is that the cost was
modelled in §39.14 and had never been weighed on iron; it is 1.1x–1.8x for
ordinary drawing and 4x for the two shapes that were already known to
degenerate.
### Set 64 — a composed band against the face it replaces (SPEC.md §5.4.2)

Emulator: QEMU under `-icount shift=3,sleep=off`, so the PIT counts guest
instructions; one count is **0.359 ms of real XT** (Part 2's conversion).
Harness: `tests/bandbench`, `make bench`, N=8 a row. **Every row below was taken
in one run on one adapter, including the `FONT_RUN` row it is compared with** —
the point of the harness, because the plan this gated was written against two
existing harnesses that disagreed by 23% about the same primitive.

The workload is **one line of text 624 pixels wide**, two ways: 78 cells of the
kernel's 8×8 face, and 104 proportional glyphs at a 6-px average advance — a
third more characters in the same width, which is most of what setting a
proportional line is for.

| ms on a 4.77 MHz 8088 | VGA 640×480 | CGA 640×200 | Herc 720×348 |
|---|---|---|---|
| `FONT_RUN` 78 cells, byte-aligned | 59.06 | **24.37** | **24.32** |
| `FONT_RUN` 78 cells at x+5 | 63.86 | 67.94 | 67.45 |
| compose 104 glyphs (plain loop) | 42.50 | 42.68 | 42.59 |
| `GFX_BLIT1` 624×8 | 2.87 | 2.83 | 2.83 |
| `GFX_BLIT1` 624×12 | 3.19 | 4.04 | 4.04 |
| band: plain compose + blit 624×12 | 45.73 | 46.63 | 46.63 |
| compose 104 glyphs (**pre-shifted**) | — | **21.18** | — |
| band: **pre-shifted** compose + blit | — | **25.31** | — |

**The emit is nearly free and the compose is the whole bill.** `GFX_BLIT1`'s two
heights give its line directly: 6.75 counts for 4 rows is **2.42 ms a row**, 78
bytes, **3.9 µs a byte** — which is `rep movsb` on an 8088 to the byte, and the
intercept is 1.1 counts ≈ 395 µs, under §5.7's 756 µs floor because the blit
skips almost everything a drawing call normally does. Composing is 91% of the
plain band and 84% of the pre-shifted one.

**So the pre-shifted glyph table is not an optimisation, it is the design.** A
95 × 8-phase × 8-row table of pre-shifted, pre-complemented AND masks is 12,160
bytes and takes the compose from **42.68 to 21.18 ms — 2.01×** — because the
inner loop stops being seven instructions with a `shr ax,cl` in it and becomes
three with the band's stride as an assemble-time displacement. The whole
operation then costs **25.31 ms against the aligned 8×8 row's 24.37 — 1.04×**,
for 104 proportional glyphs in a 12-row face instead of 78 fixed 8×8 cells; and
**2.68× less than the same line lettered at an unaligned x**, which is what a
proportional pen actually is.

**The two comparisons say different things and both are true.** Against the
honest status quo for text at an arbitrary pen — `font_run`'s fallback, which is
where every proportional glyph would land — the band is **2.7× faster**. Against
a byte-aligned 8×8 run, which §11.94 made the default for windows, it is **4%
dearer**. The plan this gated claimed "never slower" against a 71.4 ms bar; that
bar is the *unaligned* row, and stated against the aligned one the claim is
parity rather than a win.

**Cross-check against Part 2, and one disagreement worth writing down.** The
unaligned row is **865 µs a cell** on Hercules against Part 2's measured 905 —
4.4% apart, and the two agree. The aligned row is **312 µs a cell**, and here
`tests/fontbench`'s published 10-cell figures (§6.1.1) imply about 725. Most of
that gap is arithmetic rather than contradiction: a 10-cell run is mostly
`font_run`'s per-CALL setup — two `font_ink`s, a `gfx_rowbase` with a 16-bit
`mul` an 8088 charges ~120 clocks for — and fitting both harnesses to one line
puts that fixed cost near **4 ms**, five times §5.7's 756 µs floor, not at it.
That is a figure neither harness set out to measure and neither has pinned;
**§6.1.1's per-cell numbers should not be extrapolated to a long run until it
is.** Nothing in this set depends on it: every ratio quoted above is between
rows of the same run on the same machine.

**Correctness, and it is in the same harness on purpose.** The identity row
composes the same string at a fixed 8-px advance from the very ROM glyphs
`FONT_RUN` letters with, and blits it directly under a `FONT_RUN` of it. Read
back and compared pixel for pixel: **0 differing pixels of 624 × 8 on all three
adapters** (1,236 ink pixels on VGA, 2,602 on CGA, 1,034 on Hercules). That
covers the shift, the row walk and — on CGA and Hercules — the interleaved bank
wrap, which is the part no screenshot of a working desktop would have exercised.

### Set 65 — is there room for 11,000 Hz in XT mode? Windowed yes, fullscreen no

Set 20 ended on a question it could not answer. It took Tracker's XT mixer from
45.2% of a 4.77 MHz 8088 to 29.6%, said "the only large lever left is the SAMPLE
RATE, because the work is `channels × rate` and nothing else", and then spent
that lever *downwards* — 4,000 Hz, built onto the bench build's `K` key,
listened to on the field owner's hardware and **closed** ("4,000 Hz sounds
terrible"). This is the same lever pushed the other way: what did the 16 points
buy, and is it enough for **11,000 Hz** — double XT mode's rate, and the rate
the default mode already asks for.

Machine: `os8088_5150_sb_gla`, a cycle-accurate 4.77 MHz 8088 with a CGA and a
Sound Blaster (DSP 2.01). Module: `BEVERLY.MOD`, four channels, XT mode
**pre-armed** (`mp_xt` = 1, which is what a tier-0 machine boots into). Build:
`make trklog`, whose `K` key moves the XT rate without leaving XT mode
(docs/FIELD-NOTES.md 16). Instrument: `tools/os88rate.py`.

**THE HEADLINE NUMBER IS NOT THE PROFILE.** A profile says where the time went;
it does not say whether the music came out. So the measure is **bytes per second
reaching the card** — `d[trk_consumed]` over `d(guest cycles)` — because when
the ring cannot supply them SPEC.md §34.5's honest path takes bounded SILENCE
rather than looping stale samples, so that ratio *is* the fraction of the song
that was audible. The ring lead (`trk_total − trk_consumed`) is the same story
one step earlier.

| windowed | 5,500 Hz | 11,000 Hz | 11,000 Hz, 4.7 min |
|---|---|---|---|
| **audible** | 5,511 B/s = **100.2%** | 11,036 = **100.3%** | 11,019 = **100.2%** |
| ring lead, min / median | 14,336 / 16,384 | 6,144 / 14,336 | 2,048 / 14,336 |
| `mp_mixch_xt` + `mp_stepi_set` | 35.0% | **62.8%** | 61.1% |
| `[SOUND.DRV]` | 4.8% | 8.8% | 9.3% |
| kernel, BIOS, idle | 48.4% | 18.4% | 19.5% |

| fullscreen text (§45.13) | 5,500 Hz | 11,000 Hz | 11,000 Hz, `T` |
|---|---|---|---|
| **audible** | 5,500 B/s = **100.0%** | 9,166 = **83.3%** | 8,348 = **75.9%** |
| ring lead, min / median | 14,336 / 14,336 | 2,048 / 8,192 | 2,048 / 8,192 |
| `mp_mixch_xt` + `mp_stepi_set` | 34.4% | **52.7%** | 53.4% |
| `ttx_*` — the scrolling grid | 10.8% | 9.5% | 9.6% |
| kernel, BIOS, idle | 26.9% | 9.4% | 9.2% |

**Windowed, 11,000 Hz holds outright** — 100.2% of the music over 280 seconds of
guest time, which is most of the module rather than a sample of it. **On the
fullscreen text screen it does not: 17% of the song is never played**, and the
ring lead spends its time at one half instead of seven.

**The arithmetic between those two lines is the whole result, and it closes.**
The mixer *needs* about **62% of a 4.77 MHz 8088** to hold 11,000 Hz — windowed
it gets 62.8% and delivers 100.3%. The fullscreen text screen spends ~10 points
on the grid, so the mixer gets 52.7%, and 52.7 / 62 = **85% against 83.3%
measured**. The model is the machine, again.

**The surprise is which surface is the expensive one, and it is not the one this
tree's prose implies.** §45.13 exists because the graphics grid cost 2,567 glyph
cells a second and the text grid costs "~4% of the machine", so fullscreen text
reads as the *cheap* surface. It is the dear one — because §45.9.1 already stops
animating the **windowed** grid on a tier-0 machine while something is playing,
so windowed-while-playing draws almost nothing and fullscreen draws a scrolling
grid. Measured, that grid is **9.5–10.8%**, not 4%. At 5,500 Hz the difference
is invisible: both surfaces deliver 100% and the slack absorbs it. At 11,000 Hz
there is no slack, and the same 10 points are the entire margin.

**The frame clock is NOT the lever, and the control says so.** `T` puts the
frame clock back on the tick — 18.2 Hz instead of `FSXW_FRAME`'s 54.6 — and the
drawing cost does not move (9.6% against 9.5%), because §45.16.1 already made
the blit change-driven: it is priced per **ROW**, and rows do not slow down when
frames do. Three times fewer frames bought nothing, and delivery came out
*worse* rather than better (75.9%), which on one run each is inside what these
two conditions can be told apart by — the useful reading is that it did not
help, not that it hurt. Anything that recovers those 10 points has to stop the
grid scrolling, which is the one thing §45.13 was built to start.

**So the answer to "is there capacity" is yes, and the answer to "can it be
offered" is a decision rather than a measurement** — the shape Set 20's 4,000 Hz
question had, and it belongs where that one was settled: on hardware, by ear.
The three ways it could go, priced:

- **11 kHz windowed only**, refused on the text surface the §47 say-why-not way,
  the way `L` and `S` already are (§45.7). Costs nothing measured here and gives
  up nothing that works today.
- **11 kHz everywhere, and the fullscreen grid stops scrolling at that rate** —
  §45.9.1's argument applied to the text screen at double the rate. Buys the
  ~10 points; gives back exactly what §45.13 was built for.
- **11 kHz everywhere as it stands.** 17% of the music silently missing. Not an
  option: SPEC.md §45.8's degradations are all *honest* ones, and this is not.

**Two things this set does not prove.** Every number here is MartyPC's: the
calibration machine, 5150 #1, is period and has **no sound card at all**. A
field confirmation goes to **5150 #2** (docs/FIELD-MACHINES.md), whose Picomem
carries a Sound Blaster the rebuilt PMInit now reaches — and that machine is
worth having for this one, because what XT mode spends is CPU and its 8088 is a
stock 4.77 MHz part; only the storage, the DSP and its DMA are the card's. And
the audible question, whether 11 kHz is worth having, cannot be measured at all.
`BEVERLY.MOD` is one four-channel module, and the mixer's cost is
`channels × rate` with silent channels skipped, so a module that rests fewer
channels costs more than this one does.

**What says the instrument is not manufacturing the result**: the 5,500 Hz
windowed rows reproduce Set 20's post-fix figures on a different harness — the
two inner loops measure **29.5%** here against Set 20's **29.6%** — and both
5,500 Hz rows deliver 100.0–100.2%, which is the "nothing is wrong" reading a
starving row has to be told apart from.

**One trap, and it cost the first two runs.** A `.DRV` is a v4 package whose
image is a heap claim taken from the TOP down (SPEC.md §51), so its code sits in
HIGH memory — and a `base <= ip < base + 0x10000` package window swallows it and
buckets it into whatever the last label in the listing happens to be. This put
SOUND.DRV's block ISR into **`tlog_s_clkr`, a STRING**, at 4.8% and 8.8%; the
giveaway was that a string scaled with the sample rate. It is a bucket of its
own now, and the package window stops at the image size — which is at header
**+8**, not +4, where the link base lives and is always 0.

### Set 66 — the fullscreen ladder, and the harness was in the measurement

Set 65 said fullscreen 11 kHz delivered **83.3%** of the music and put ~10
points of that on the §45.13 text screen's drawing. Two things were wrong with
it and the second is the one worth keeping.

**Five points of that "drawing" was the LOG.** `ttx_status` calls `ttx_putn`,
and in the `TRKLOG` build it runs **every frame** — its own header says why, the
status line being live so a `[ttx_lmsg]` pointer compare cannot see digits
change inside one buffer — where the shipped build redraws it on a message
CHANGE. So the capacity run was pricing 59 characters 54.6 times a second that
`TRACKER.O88` never spends. `TTXQSTAT` takes that back, and `ttx_putn` leaves
the profile entirely.

| fullscreen text, 11,000 Hz | audible | mixer | grid |
|---|---|---|---|
| Set 65, as measured | 83.3% | 52.7% | `ttx_blit` 4.8% + `ttx_putn` 4.7% |
| `TTXQSTAT` — what the shipped build draws | **88.4%** | 50.2% | `ttx_blit` 5.2% |
| `TTXPAGE` — blit once a window, not once a row | **90.7%** | 51.5% | `ttx_blit` 1.9% |
| `TTXNODRAW` — no grid blit **at all** | **91.9%** | 52.7% | — |

**A page-flipping grid — the window jumping a page every `TTX_VIS` rows instead
of sliding a row, ~19× fewer blits — is worth 2.3 points of delivery, and
removing the blit outright is worth 3.5.** (The `TTXPAGE` row is a lower bound
on the real thing by 59 attribute writes a row, which it does not do.)

> **THE CONCLUSION THIS SET DREW FROM THAT WAS WRONG, AND SET 67 IS THE
> CORRECTION.** It read the `TTXNODRAW` row as the ceiling on *drawing* and
> concluded that no drawing change could make fullscreen 11 kHz hold.
> `TTXNODRAW` removes the grid **BLIT** and nothing else, and the blit is
> **under half** of what the text screen costs — the readouts, the VU needles,
> the shadow rebuild and `tui_sync` are the rest. The true ceiling is
> **99.8%**, so it holds. The rows below are all correct; the sentence that
> read them was not. **A knob named for what it removes is not evidence about
> the category it sits in.**

**What is actually in the way is the SCHEDULER, and it took bucketing the
kernel to see it.** "Outside the package" was 17.6% on a run that was starving,
which is already the tell: a machine that is out of cycles does not have 17%
spare. Resolved against the kernel's own symbols:

| `TTXNODRAW`, 11,000 Hz fullscreen | |
|---|---|
| `sch_switch.nowrap` | **5.2%** |
| `sch_switch.scan` | **2.2%** |
| `task_yield.save` | 1.2% |
| `osapi_table` | 1.3% |

**~8% of a 4.77 MHz 8088 inside the task switch**, because in an fsx bracket two
tasks are always runnable — the drawing side and §53.2's kept worker — and
`fsx_wait`'s `FSXW_FRAME` is a **spin on `task_yield`**, so every round trip is
two context switches. That is also why Set 65's `T` control did nothing: `T`
moves the app's frame WAIT between `FSXW_TICK` and `FSXW_FRAME`, and
`FSXW_TICK` is a `hlt` — which idles the CPU rather than handing it to the
worker, so it measured *worse*.

**Set 67 measured `FSXF_FASTTICK` and the switch count is NOT what it costs.**
Dropping the sub-tick is worth **+4.7 points** and the scheduler's own share
does not fall at all (11.4% → 12.0%). The hypothesis in this paragraph — that
three IRQ0s a tick mean three times the switch cost — is refuted by its own
A/B; what the sub-tick actually buys back is frames, and frames are what the
text screen's furniture is priced in.

**The general lesson is the one this tree keeps relearning.** The bench build is
not the shipped build, and the difference does not announce itself: it assembles,
it boots, it plays, and it charges the thing under test for the instrument's own
work. Set 65's `ttx_putn` rows were a *measurement of TRKLOG*. Every figure here
is `TTXQSTAT`, and the windowed numbers Set 65 shipped its verdict on are
unaffected — windowed, `ttx_status` had nothing to draw over and `ttx_putn` never
appeared in those profiles at all.

**The SHIPPED player was measured separately, and it had to be.** The rate pick
is behind an `%ifdef` — `trk_play` reads `tlog_xrate` under `TRKLOG` and
`trk_xhi` without it — so every figure above is evidence about the bench build
and none of it is evidence about `TRACKER.O88`. Driven through §45.9.3's own
`R` key instead of `K` (`os88rate.py --rkey`, which exists for this):
**11,019 B/s of 11,000 = 100.2%** over 234 s of guest time, windowed, mixer
57.9%, ring lead median 14,336 of 16,384. That is the number the feature ships
on. `tests/trkrate.py --shipped` is the same distinction as a gate.

**And two of the four knobs above are permanent.** `TTXQSTAT` stays because
without it no fullscreen capacity figure taken on a `TRKLOG` build means
anything; `TTXPAGE` and `TTXNODRAW` stay because the negative result is the
valuable part and the next author to propose a cheaper grid should be able to
re-run it in one command rather than rebuild the argument. All three are inside
`%ifdef` and `TRACKER.O88` is **byte-identical** with and without them.

### Set 67 — fullscreen CAN hold 11 kHz, and the grid blit was never the cost

Set 66 concluded that no change to the fullscreen text screen's drawing could
make 11 kHz hold. That was wrong, and the way it was wrong is worth more than
the number: its ceiling knob `TTXNODRAW` removes the grid **BLIT** and nothing
else, and it was read as though it removed *drawing*. The blit is **under half**
of what that screen costs.

All rows: `os8088_5150_sb_gla`, `BEVERLY.MOD`, XT mode, 11,000 Hz,
`TTXQSTAT`, ~170 s of guest time each. The last row is the shipped windowed
player at the same rate, for scale.

| fullscreen text | audible | mixer | text screen | scheduler |
|---|---|---|---|---|
| as shipped | 88.7% | 61.2% | 13.0% | 11.4% |
| `TTXPAGE` — page-flip the grid | 90.7% | — | — | — |
| `TTXNODRAW` — no grid blit | 91.9% | 52.7%\* | — | — |
| `TTXNOFAST` — no sub-tick | 93.4% | 65.0% | 11.5% | 12.0% |
| `TTXNOFAST TTXPAGE` | **96.8%** | 68.0% | 6.1% | 14.4% |
| `TTXNOFAST TTXNODRAW` | **96.8%** | 68.2% | 5.3% | 14.7% |
| **`TTXNOFAST TTXNOALL` — no text drawing at all** | **99.8%** | 68.4% | 0.2% | 19.9% |
| *(windowed, the shipped player)* | *99.8%* | *70.1%* | *0.2%* | *9.0%* |

\* Set 66's figure, taken before `TTXFSANY` existed — see the harness note.

**So it holds, and the last row says why it always could**: with nothing to
draw the bracket gives the mixer **68.4%**, within 1.7 points of what windowed
gives it. The bracket is not the problem.

**The grid blit is ~40% of the screen's cost and the furniture is the rest.**
`TTXNOFAST TTXPAGE` and `TTXNOFAST TTXNODRAW` land on the **same 96.8%** — once
the blit is gone, page-flipping has captured all of it — and the 3.2% still
missing is the readouts, the VU needles, the shadow rebuild and `tui_sync`,
which is the half no grid change touches. **A cheaper grid gets two thirds of
the way and cannot get further.**

**The sub-tick is worth +4.7 points and NOT for the reason Set 66 gave.** That
set put the bracket's cost on `FSXF_FASTTICK` tripling IRQ0 and therefore the
switch scan; the A/B refutes it — the scheduler's share does not fall when the
sub-tick goes (11.4% → 12.0%), it *rises* as the mixer gets more to do. What
the sub-tick actually costs is **frames**, and frames are the unit the
furniture is priced in. Note the scheduler at **19.9%** on the row that
delivers 99.8%: `fsx_wait`'s yield-spin is not a tax, it is idle time with a
name, and a run that reports 20% there while delivering everything asked for is
saying the machine had it to spare.

**Why windowed looked "cheaper" than a mode that owns the machine**, which is
the question that started this: it is not overhead, it is work. The bracket
*does* hand the app more — the kernel's own share falls from **10.8% to 3.1%**,
no gfx lock, no cursor, no `ui_task` — and §45.9.1 stopped animating the
**windowed** grid on a tier-0 machine while something is playing, so windowed
XT mode draws **0.2%** and fullscreen draws 13.0%. Fullscreen is dearer because
it is the mode that still shows you the music. **The 21.8% cursor-and-lock
figure that makes windowed sound expensive is Set 4's, and it is Missile
Command's** — an app that takes the lock every frame. Here `cur_shape_pass`
measures **0.6%**.

**What it would take, priced.** Not proposed, not built — the shipped feature
is windowed-only (SPEC.md §45.9.3) and this is what a later attempt would be
buying:

- **Drop `FSXF_FASTTICK` at the high rate** — +4.7. It costs the frame rate,
  54.6 → 18.2 Hz, which §45.16 measured the scroll's smoothness in.
- **Page-flip the grid** — +3.4 on top of that. §45.13.5's carry already moves
  a window rather than reformatting one, so this is a change to *when* it
  blits, and it costs the row-by-row scroll the text screen exists to give
  back.
- **The last 3.2% is the per-frame furniture** and wants §56.12's shape — an
  invalidation mask over the readouts and needles, which is a bigger change
  than either of the above and the only one that does not cost a visible
  feature.

Two of those three are giving back exactly what §45.13 was built to provide.
**That is the trade, and it is a decision rather than an optimisation.**

**A fourth, found while wiring the §45.9.3 gate: the listing pattern was
anchored on `<1>`, NASM's marker for an INCLUDED line** — so it saw every
`.inc` and none of `tracker.asm`, and no `trk_*` symbol had ever appeared in a
profile. That reads as "the app spends no time in its own main file" rather
than as a missing pattern. The first attempt at the fix was worse and is the
part worth keeping: a loose `\w+:` search anywhere in the line **harvests
English**, and it put `es` at 44.1% of the machine, `pass` at 8.7% and `DX` at
1.9% — words out of comments — while the profile still looked exactly like a
profile. Both patterns are anchored at the start of the source now and the
comment is cut off first. **A profile is a histogram of whatever you told it
the symbols were, and it never looks wrong.** Every figure in Sets 65–67
predates this and is unaffected: the mis-anchored pattern dropped
`tracker.asm`'s own labels into one fallback bucket, which measured 0.2–2.3%,
and every category quoted above comes from an `.inc` or the kernel.

**Three harness corrections, and every one of them changed a number.** The
11 kHz refusal §45.9.3 added made the fullscreen measurement **unreachable** —
`K` now also sets `trk_xhi`, so `F` was declined and a run labelled "fullscreen
text" was silently windowed, reporting **100.3%** on a screen it never reached.
`TTXFSANY` is the bench bypass, and the general rule is that **a product
refusal must not make the measurement it was decided from unrunnable.** The
`--defines` list was **prepending `TRKLOG` invisibly**, so asking for the
shipped player got a bench listing and `mp_xt` read as **254**; it is the full
list now, and `make trkrate` writes each disk's defines beside it so the tool
reads them rather than being told. And Set 66's own `ttx_status` correction is
the third — that one is in Set 66.

### Set 68 — RunCPM's row composer, priced before it was quoted (SPEC.md §74.2)

Emulator: QEMU under `-icount shift=3,sleep=off`, VGA; one count is **0.359 ms
of real XT** (Part 4). Harness: `tests/rcband` (`make rcbandbench`), N=8 a row,
one run — the `FONT_RUN` bar in the same run, per Set 64's rule. It exists
because §71.2's cost model priced `rc_band`'s compose at an unmeasured ~40 µs a
cell and every derived number rested on it (rule 4).

| ms on a 4.77 MHz 8088 (VGA) | counts/op | ms |
|---|---|---|
| `FONT_RUN` 79 cells, byte-aligned | 167 | **60.0** |
| `RC_BAND` 79 cells, **shipping** compose (drawing nothing) | 32.4 | 11.6 |
| `RC_BAND` 79 cells, the **first** loop (kept in the harness) | 67.4 | 24.2 |
| `GFX_BLIT1` 632×8, stride 80 | 8.1 | 2.9 |
| band 79 = compose + blit | 40.4 | **14.5** |
| `RC_BAND` 1 cell | 0.9 | 0.31 |
| band 1 cell = compose + blit | 2.9 | **1.03** |
| `FONT_RUN` 1 cell | 4.5 | 1.62 |
| band 11 cells = compose + blit | 7.75 | 2.78 |

**Read as lines: a band is 860 µs a call + 173 µs a cell** (the compose 145 of
it, the emit ~31 — Set 64's 395 + 31/cell for `blit1` alone holds); the first
loop was **306 µs a cell**, 7× the model's guess, and the rewrite (a constant
stride, the eight rows unrolled, the attribute bit rotated in a register —
Set 64's own lesson) halved it. **The band beats `font_run` at every width**:
1.03 ms against 1.62 for one cell on VGA, 14.5 against 60 for a row (against
Set 64's ~24 ms aligned row on CGA/Hercules the 79-cell band is 1.7× cheaper,
not 4×). **Two 1-cell bands (2.07 ms) beat one span from 8 cells up**, which
is where §71.2's same-row cursor split sits. The identity rows — the string
lettered, the same string composed and blitted under it, and a third with
every eighth cell reverse-video so the attribute byte's wrap is on the glass
— are the correctness check, by screendump.

### Set 69 — what a LINE pixel costs, decomposed (SPEC.md §5.6)

| | |
|---|---|
| machine | MartyPC, `os8088_5150_herc` and `os8088_5150_cga` — a cycle-accurate 4.77 MHz 8088 with the real IBM 5150 ROM |
| harness | `tools/os88linecost.py`, which is new and is **not** a package: the CPU is parked on a stub written into `gfx_pairtab0` (256 idle `.bss` bytes inside KERNEL_SEG) and `status()["cycles"]` is read either side, with IF = 0 |
| build | `elendilon` at the branch point, shipped kernel, no knobs |
| date | 2026-08-21 |

It exists because rule 4 was owed: §5.6.1 puts the `gfx_line`/`gfx_fill`
crossover at "~27px" from an instruction estimate, Set 4 measured a dilated
trail pixel at 500 µs and could not say what the 500 was made of, and Set 5/6
left "something in `mc_wipe_trails` beyond the walk is unaccounted for".

#### The line, as a straight line

Three lengths per geometry, fitted:

| | arrival | per pixel |
|---|---:|---:|
| steep (y-major) | 1,822 cyc = **382 µs** | 723.1 cyc = **151.5 µs** |
| shallow (x-major) | 1,905 cyc = **399 µs** | 657.1 cyc = **137.7 µs** |

The fit is exact to one cycle at the middle length, so this really is
`a + b·pixels` and not a curve. The 10% steep/shallow gap is Set 11's, still
there, still `gfx_rowbase` per step against a pointer add. **The arrival is
382 µs and not §5.7's ~756**: this calls the near kernel entry directly, so it
is the routine without the far call and without `gfx_lock`.

Cross-check against iron: Set 11's `GFX_LINE steep thin` on the field 5150 is
**21,184 µs** for the same 32×127 geometry and this reports **19,775** for it
without gfxbench's far call and wrapper — 7% apart, in the direction the
missing wrapper predicts.

**A dilated erase is 1.36× a thin draw when it is steep and 2.98× when it is
shallow** (128 px: 26,844 µs against 19,775; 53,767 against 18,023). That is
§5.6.6's one-walk path confirmed from outside: three passes would be three
times, and steep is not.

#### Where the 723 cycles go — measured, one piece at a time

Each piece run 500 times in a `loop`, net of the `loop` itself:

| piece | cyc | ins |
|---|---:|---:|
| the five per-pixel guard compares (§5.6.3's clip box + §5.6.6's `gfx_ln_wide`) | **170.0** | 10 |
| the `e2` block — `e2 = 2*err` and both Bresenham tests, through `.bss` | **298.0** | 17 |
| ...the same decision in registers | 116.0 | 9 |
| `call gfx_nextrow`, as the walk makes it | **159.0** | 6 |
| ...the same three instructions INLINE | 100.0 | 4 |

(The `e2` rows and the register row each carry one `push`/`pop` pair of
harness, ~30 cyc; the `gfx_nextrow` rows carry a `sub di,[vid_rowadd]` undo,
~25.)

So a steep line pixel is roughly **170 guard + 270 bookkeeping + 134 row step
+ 38 store + ~110 the rest**. Nothing is anomalous and nothing is missing:
Set 5/6's unaccounted remainder was the guard block and the call.

#### The finding, in one line

**40.8 instructions per pixel at 17.7 cycles each.** The cycles-per-
instruction is not the problem — a register-resident candidate measures 15.7
— and 17.7 is simply what a **direct memory operand** costs on an 8088:
`cmp si,[gfx_ln_cx1]` is 4 bytes and a word read, 15 clocks of execution, 4
more for the 8-bit bus, and 4 clocks a byte of prefetch that the queue cannot
hide across eight taken branches. Every one of `dx`, `dy`, `err`, `e2`, `sx`,
`x2`, `y2`, the four clip edges, the wide flag and the ink is a word in
`.bss`. Part 2's `max(clocks, 4.34 × bytes)` floor is the whole story and the
110-byte loop body is over it.

#### The candidate, measured on both 1bpp adapters

An octant-split walk — `d = 2dx − dy`, the major axis stepping every pixel so
it needs no test, `d` and `2dx` and `2dy` in registers, the clip spent before
the loop, the row step inline. `tools/os88linecost.py --model` proves it lays
the **identical** pixel set over 9,480 endpoint pairs, and the harness diffs
the framebuffer to prove it again on the glass.

| 128 px | Hercules | CGA |
|---|---:|---:|
| `gfx_line` | 94,383 cyc | 86,063 |
| candidate, portable row step | **19,415 (4.86×)** | **20,244 (4.69×)** |
| ...with the Hercules-only `js` wrap | 16,086 (5.87×) | *lays a different line* |
| ...with the framebuffer store removed | 11,223 | 12,894 |

Every "identical" row above is byte-for-byte identical framebuffer. The `js`
row is the reason the harness diffs at all: Hercules' `vid_wrapbit` is 0x8000
and IS the sign bit, CGA's is 0x4000, and the shortcut is silently wrong
there — 64 bytes different, no error, a plausible-looking line.

**147 cyc/px portable = 30.8 µs a pixel, against 151.5.** The store is
38 cyc/px of it (5% of the old loop, 26% of the new one).

#### What did NOT pay, and it is the interesting half

**Accumulating a framebuffer byte instead of one read-modify-write per pixel
is worth 10%, not the 8× the store count suggests** (112.1 cyc/px against
124.9 on a 127×32 line, both identical pixels). A shallow line only holds
eight pixels in one byte if it is shallow enough that `gfx_line` already sends
it to `gfx_hline` (§5.6.1); at 127×32 the row changes every fourth pixel and
the byte has to be spent then anyway. Rule 5 in miniature — the optimisation's
*shape* was right and its *reason* was not present.

`docs/LINE-PERF-PLAN.md` is what this would cost to build, and what has to
move with it.

### Set 70 — §5.6.4.1 built and measured, and one defect it found

| | |
|---|---|
| machine | MartyPC, `os8088_5150_herc` and `_cga`, real IBM 5150 ROM |
| harness | `tools/os88linecost.py gfx_line`, three lengths per geometry, fitted |
| build | the fast walk in the kernel, against Set 69's numbers for the same rows |
| date | 2026-08-21 |

| | Set 69 | now | |
|---|---:|---:|---|
| steep, per pixel | 723.1 cyc | **151.9** | **4.76×** |
| shallow, per pixel | 657.1 | **128.2** | **5.13×** |
| a 128 px steep line, whole | 94,382 | **22,845** | 4.13× |
| a 128 px shallow line, whole | 86,017 | **19,815** | 4.34× |
| a **dilated** shallow erase, 128 px | 256,617 | **58,011** | 4.42× |
| the arrival | 1,822 | **3,405** | 0.54× |

**The arrival nearly doubled and that is the trade.** The eligibility tests,
the interval and `gfx_ls_addr` are ~1,700 cycles where the general walk's
header was ~600, so the two cross at **2.8 pixels** — which is why `LF_MINMAJ`
refuses a line shorter than four and the change is a win at every length
rather than a win on average. A dilated **steep** erase is untouched: §5.6.6's
three-column walk is a different shape and the fast walk refuses it.

**The pixels are identical and that is checked twice**: `tests/linefast.py`
against the same kernel with the dispatch poked out, on both 1bpp adapters,
clipped and not; and `os88linecost.py candidate`, which still reports
`identical` against the independent stub Set 69 validated.

#### The defect the comparison found (SPEC.md §5.6.4.3)

The two walks disagreed on every line entering the screen **from the top**,
and the fast one was right. `gfx_line_mono` called `gfx_rowbase(y1)` with
`y1 = -5`; `gfx_rowbase` is banked, so that is bank 3 of a 16-bit product that
wrapped, and five `gfx_nextrow` steps from it land **90 bytes past row 0**
rather than at it — a line drawn **four rows low on Hercules**, silently, for
as long as the primitive has existed. The walk skipped the pixels above the
box correctly and addressed every pixel after them wrongly.

`gfx_lm_pre` steps the state to the box's top edge before any address is
computed; `gfx_ls_addr` uses `sar` so the left edge works the same way. 54
bytes, on both kernels, and it changes nothing for a line that starts on
screen — which is every line any shipped package draws today, which is why
nobody had seen it.

**Price: `.text` +686, one 512-byte image rung**, `KERN_BUDGET` spare 1,024 →
512. `kern_small` does not get it (§5.6.4.4) — it did not fit, and the guard
said so rather than anybody noticing.

### Set 71 — the frame rate of a program that only draws lines (SPEC.md §78)

| | |
|---|---|
| machine | MartyPC, `os8088_5150_herc` and `_cga`, real IBM 5150 ROM |
| harness | `tests/wirefps.py` — **the program reading its own answer** |
| build | shipped kernel and shipped `WIRE.O88`, one boot each |
| date | 2026-08-21 |

Sets 69 and 70 priced a line pixel. This is what that is worth to something
that draws lines for a living: `apps/wire` tumbles a wireframe cube with
nothing but `OSAPI_GFX_LINE` — twelve edges drawn and twelve erased a frame —
and keeps its own frame rate in `wr_fps`. The two runs are **the same boot and
the same binary**: three bytes of `gfx_line_raw` are poked to `stc`/`nop`/`nop`
and every line goes down §5.6.4's general walk instead, with nothing else
different, and then poked back.

| | general walk | fast walk | back |
|---|---:|---:|---:|
| Hercules 720×348 | 8.1 fps | **18.2** | 18.2 |
| CGA 640×200 | 11.4 | **18.2** | 17.1 |

**18.2 fps is the system tick.** On both adapters the fast walk stops being
what limits the frame rate, which is the only form of "fast enough" that means
anything here — and `View → Medium` is sized so it lands there rather than
past it.

**CGA's ratio is lower and that is the arrival, not the walk.** Its window is
clamped to a 200-row desktop, so the figure is smaller, so fewer of the
frame's cycles are pixels and more are the 24 per-call arrivals Set 70 priced
at ~713 µs each. The same change looks smaller the less drawing there is to do
— which is the honest shape of a per-pixel optimisation and worth having a
second adapter say out loud.

**The "back" column is the control.** It is the point of poking rather than
building twice: a figure that did not return would mean the two samples had
drifted apart for some other reason. 17.1 on CGA against 18.2 is one frame in
a six-second window, which is the tenth `wr_fps` is quantised to.

### Set 72 — the erase's SPREAD by angle, which is what a player sees

| | |
|---|---|
| machine | MartyPC, `os8088_5150_herc`, real IBM 5150 ROM |
| harness | `tools/os88linecost.py`'s rig, driven by hand; three configurations poked on one boot |
| geometry | **193 pixels dilated**, steep and shallow — Set 4's own measured whole-trail erase length |
| date | 2026-08-21 |

Reported from the field after Set 70 shipped: Missile Command's trails "speed
up and slow down". They do, and it is not the arrival — every `gfx_line` call
pays the same one. It is that §5.6.4.1 made the **shallow** dilated erase 4.6×
cheaper and left the **steep** one exactly where it was, because §5.6.6's wide
walk is the one shape the fast walk refuses.

| a 193 px dilated whole-trail erase | before Set 70 | after Set 70 | after §5.6.6.1 |
|---|---:|---:|---:|
| steep | 40,456 µs | 40,474 | **20,438** |
| shallow | 81,092 | 17,446 | 17,446 |
| **spread** | 40,636 | **23,028** | **2,992** |

Set 4 counted **38% of a real run's erases steep**, so after Set 70 a
whole-trail erase cost 40 ms or 17 ms depending on the angle, two frames in
five drawing the long straw. 23 ms is **42% of a 54.9 ms tick**, and a frame
budget that swings by nearly half a tick on a property of the geometry is
exactly what "speeds up and slows down" looks like.

**The absolute numbers all improved and the experience got worse**, which is
the finding worth keeping: 40/81 has a spread of 40 ms too, but both arms are
over the tick, so every frame stutters and none of them stands out. Set 70
took the common arm under the tick and left the other one over it. **Variance
against a smooth baseline is more visible than variance against a rough one**,
and a mean is not a frame rate.

Clipped against a single window rect — the way §48 actually draws — the same
three columns are 20,577 / 17,570 / 89,392, the last being what three passes
cost when the fast walk is refused and the reason `gfx_lf_wide3` asks rather
than assuming (§5.6.6.1): dropping the wide walk unconditionally would be
2.2× **worse** on exactly the lines it still serves.

### Set 73 — the flicker, as ink on the glass (SPEC.md §78.5)

| | |
|---|---|
| machine | MartyPC, `os8088_5150_herc`, real IBM 5150 ROM |
| harness | `tests/wireflick.py` — the object area sampled once per DISPLAYED frame, its ink counted |
| build | `apps/wire`, Medium, twelve edges |
| date | 2026-08-21 |

`m.flicker()` (Part 3.1) is the wrong instrument here and says so: it requires
the screen to **settle**, and the whole point of this window is that it never
does again. So the thing a person actually reacts to is measured instead —
**the figure going away** — by counting the object area's lit pixels frame by
frame.

| draw order | emptiest frame | mean ink | frames under half full | fps |
|---|---:|---:|---:|---:|
| Whole figure — erase all, then draw all | **0%** | 48% | **56%** | 17.1 |
| Edge at a time — erase old[i], draw new[i] | **72%** | 90% | **0%** | **17.1** |
| Edge, then repair — and a third pass | 53% | 92% | 0% | 12.1 |

**Erase-all-then-draw-all empties the window completely on more than half the
frames a viewer sees**, and the fix is free: the same twenty-four line calls
in a different order, plus twenty-two `SET_COLOR`s, about a millisecond. The
floor goes 0% → 72% and the blank frames go 56% → 0% for no frame rate at all.
It is the default now.

**The repair pass is dominated, and instructively.** It costs 5 fps to buy 2%
more ink — the nicks are that small — and its *emptiest* frame is **worse**,
53% against 72%, because a third walk makes the frame half again as long and a
sample is likelier to land mid-pair. **A change that improves the mean and
worsens the floor**: Set 72's lesson one round later, in a different
mechanism, and the reason both columns are reported.

#### The API this rules out

"Pair the erase with the draw in the kernel" is the obvious suggestion and it
does not work, for a reason worth recording. To leave a *shared* pixel alone
the kernel has to know it is shared, which means walking both lines in
**lockstep** — and two Bresenham states do not fit in eight registers, so they
go back to `.bss` at §5.6.4's 723 cycles a pixel. That is **4.6× what two
§5.6.4.1 walks cost** (Set 70): the flicker would be paid for by making every
line on the machine slower. A paired *call* is still worth Set 11's measured
**128.7 µs** of arrival an edge, but that is not a flicker saving — the erase
still precedes the draw inside it.

The ordering is the caller's, and one millisecond of `SET_COLOR` is what it
costs to get it right.

### Set 74 — can a wireframe be moved by writing DIFFERENCES? (SPEC.md §78.5)

| | |
|---|---|
| machine | MartyPC, `os8088_5150_herc_gla_144`, cycle-accurate 4.77 MHz 8088 |
| harness | `gfxbench`, two rows added for the candidates |
| date | 2026-08-22 |

§78.5 offers three draw orders and says none of them is free. This set asks
whether there is a fourth shape that is: **rasterise the figure into a private
1bpp mask, and commit the frame as the XOR of that mask against last frame's,
so the framebuffer is touched only where the two differ.** A pixel the two
frames share is then never written at all, which is exactly the fracture
§78.5 pays for.

**The premise it was proposed on is false, and Set 1 already said so.** The
suggestion is usually phrased as *buffer the pixels in RAM and batch the
write*, on the assumption that video memory is the expensive part. It is not:
Set 1 measured the framebuffer at **1.09× RAM** for a read-modify-write and
1.57× for `rep stosw`, because on a 4.77 MHz 8088 the instructions are the
bottleneck and the card's wait states hide inside them. Moving pixels to RAM
and back saves nothing. Any win has to come from **issuing fewer operations**,
not from where they land.

| row | N | µs/op | per pixel |
|---|---:|---:|---:|
| `GFX_LINE shallow thin` (127×32) | 24 | 4,753.23 | 37.1 µs |
| …the same, less Set 11's 714 µs arrival | | 4,039 | **31.6 µs** = 151 clocks |
| **`mask line 127x32`** — the candidate rasteriser | 24 | **3,147.90** | **24.6 µs** = 117 clocks |
| **`xordiff 128x128`** — the candidate commit | 24 | **27,690.71** | 27.0 µs a WORD |

**The rasteriser is the good news: 1.29× faster per pixel, and no arrival at
all.** What it drops is everything `gfx_line` does that a caller compositing
its own figure does not need — clipping, the ink, the dither table, the
per-row `gfx_rowbase`. Priced against the cube saver's frame, which is 24
whole lines of ~45 px:

| | |
|---|---|
| now — 24 × (714 µs arrival + 45 × 31.6 µs) | **51.3 ms** (measured whole: 56) |
| rasterise 12 edges into a mask, 540 px × 24.6 µs | **13.3 ms** |

**And the commit is the bad news, and it is the whole finding: 27.7 ms.** The
scan is priced by the box's AREA and not by its ink — 1,024 words at 27 µs
each — so on a 128×128 box it costs more than the rasteriser saves. The naive
loop is nine instructions a word and the 8088's 4.34 clocks per instruction
byte does the rest. A cube's ink is 540 pixels in 12,100; **99% of that scan
finds nothing and pays full price for it.**

So the fourth order works and the obvious implementation of it does not:
41 ms against 56, for two 2 KB buffers and a new kernel primitive — and the
next subsection is what to do instead.

**The next move looked like a hierarchy** — a 1-bit-per-word dirty summary, so
the scan is priced by ink instead of area. It was going to be measured next.
It should not be, and the reason is the row that was measured instead.

#### The diff was the wrong question: `gfx_blit1` already does the commit

§5.4.2's `OSAPI_GFX_BLIT1` puts a 1bpp band down in **one arrival**, byte-
aligned, **in final screen polarity**. For a figure on a plain ground that IS
the commit: the band *replaces* the box, so the old figure goes and the new one
arrives in the same pass, no pixel is ever read, and no pixel is ever written
twice. It was built for proportional text a package sets itself. It has been
shipped all along.

| row | N | µs/op |
|---|---:|---:|
| `xordiff 128x128` — read A, read B, write the difference | 24 | 27,698.78 |
| **`GFX_BLIT1 128x128`** — write the whole box, blind | 24 | **16,609.98** |
| `clear mask 2048` — `rep stosw`, the other half of the frame | 24 | **3,113.08** |

**Writing the whole box blind beats computing which parts of it changed, by
1.7×.** That is the shape of every read-modify-write on this machine: the diff
has to *read* both masks to find out it has nothing to do, and a read it acts
on is no cheaper than a write it did not need to think about. A hierarchy would
have been an elaborate way to make the losing option lose by less.

So the cube saver's frame, every term measured:

| | |
|---|---|
| clear the mask | 3.1 ms |
| rasterise 12 edges, 540 px × 24.6 µs | 13.3 ms |
| `gfx_blit1` the box | 16.6 ms (12.3 at the cube's real 110×110) |
| **total** | **~29 ms**, against **56** now |

...and it is **constant**, which the current frame is not: the cost is the box
and the ink, not the attitude. **No pixel is ever dark that should be lit**,
because the band carries both figures' pixels in one pass. What survives is a
horizontal shear while the band goes down — the figure a few rows behind
itself, never disconnected from itself.

**`gfx_blit1` itself had room in it, and less than the clocks said.** Its row
loop was `rep movsb`; an 8088 moves a byte in 17 clocks that way and 12.5 as
`rep movsw`, which predicted 1.36×.

| `GFX_BLIT1 128x128` | µs/op |
|---|---:|
| `rep movsb` | 16,609.98 |
| **`rep movsw`** | **15,124.87** |

**1.10×.** The prediction was high because the `rep` is not the whole row: the
loop around it — the count, the `push`/`pop di`, the band step, the row step,
the wrap test — is fixed, and at 16 bytes a row halving the iterations saves
72 clocks of about 619. It is a five-byte change that leaves `.cold` **one
byte** free, it is every `gfx_blit1` in the OS, and it was verified the only
way that means anything: Word rendering `WELCOME.DOC` produces a
**byte-identical framebuffer** before and after (sha1 `7c625283…`, 105,116 lit
pixels), which exercises the odd-bytes-per-row path a proportional glyph run
produces.

At 8.1 µs a byte before and 7.4 after, against Set 1's 2.76 for a framebuffer
`rep stosw`, most of what is left is that fixed per-row work and not the bus.

**One limit is structural.** The technique is priced by the bounding box, so it
suits a compact figure and not a sprawling one: §79.5's geometry mode has a
236×236 box, four times this row's area. That mode does not want it anyway —
it never erases and redraws, it accumulates.

#### A line drawn as byte-aligned RUNS — not for this, and possibly for a horizon

Microsoft Flight Simulator 1.0 on a 5150 manages about 5 fps at half screen,
and its lines look *stepped*: long straight runs with a visible jog between
them, where ours are even. Most of that is resolution — it is CGA 320×200 in
four colours against our 640×200 mono, so every pixel is twice as wide before
anything else is different.

But the technique the look suggests is real and we do not have it. **Draw a
shallow line as per-row horizontal RUNS and round the run boundaries to byte
edges**, and each run becomes a `stos` instead of eight read-modify-writes.
§5.6.1 already sends a sufficiently shallow line to `gfx_hline`; this is the
general case of that, and the error it introduces is where the y step *lands
along the run*, up to ±4 px — which on a near-horizontal line is a
perpendicular error of almost nothing, and on a steep one is ruinous.

**So it is the wrong answer for a wireframe** — a cube's edges would stop
meeting at its corners — and it is plausibly the right one for a **horizon, a
ground grid, or a road**, where the lines are long, nearly horizontal, and
nobody can see a step land four pixels early. Noted here rather than built:
the shape that wants it is a first-person 3D game, and there isn't one yet.

Whether Artwick's code did this is not something this note claims. The
screenshot shows a resolution and a shape; it does not show an algorithm.

### Set 75 — where a composited frame's time actually goes (SPEC.md §78.8.1)

Set 74 left `Composed` shipping as a menu entry that lost to `Edge at a time`
in a window and won on a full screen, with the difference recorded as
*unexplained* and a named guess: the size of the thing being swept. **The guess
was right and the reasoning under it was wrong**, which is this set's point.

Taken on MartyPC, `os8088_5150_cga_gla`, WIREFRAME in `Draw ▸ Composed`, with
an exec breakpoint on `wr_compose`, `wr_put` and `wr_keep`. **Every stop is
labelled by its `ip`** rather than assumed to alternate, and the three spans
sum to the frame — which is the check that the labelling is right, and the
reason the first version of this measurement is not in the table.

| span | band = 128 × 120 | band = the figure's box |
|---|---:|---:|
| `wr_compose` → `wr_put`, rasterise the mask | 17.0 ms | **15.9 ms** |
| `wr_put` → `wr_keep`, the blit | 14.3 ms | **5.3 ms** |
| idle, waiting for the tick | 23.6 ms | **33.7 ms** |
| **frame** | **54.9 ms** | **54.9 ms** |
| band, mean bytes | 1,920 | **426** |

**The entire win is the blit — 2.7x — and compose barely moves.** The band was
sized to the window and the figure to the geometry, and nothing connected the
two; sizing it to the union of this frame's bounding box and the one already on
the glass (the union, because the band erases by covering) is 4.5x fewer bytes
of glass swept. Compose does not care: `wr_mline` walks one pixel at a time
with a loop count of `max(dx, dy) + 1`, so the band never enters it, and the
1.1 ms it does drop is exactly the smaller mask clear (2,048 bytes to ~944).

**The idle column is the flicker budget.** It reads like room for a bigger
figure or a second one, and it is — but every byte added to the band moves from
the idle column into the blit column, which is the only span where the glass is
incoherent. The frame rate would not improve either: 18.2 Hz is the tick.

**Three lessons, and the third is the one that cost a wrong number.**

**Rule 4 again, sharper: a diff is not a timer.** The first instrument here
compared the glass against the mask that was supposed to be on it, and reported
**72% of samples torn**. It is a good instrument for the *mechanism* — it found
the 116 px of stale ink that is §78.8's "third shade", which no aggregate would
have shown — and a bad one for the *cost*, because during `wr_compose` the mask
is half-rebuilt while the glass holds the previous frame **complete**. That is
not a flicker and the diff counts it as one. The true figure is the blit alone:
26% of frames caught mid-sweep before, **10%** after.

**LABEL EVERY BREAKPOINT STOP, and never infer one from alternation.** The
first version of the `before` column read compose = 37.8 ms and idle = 2.8 ms,
and the conclusion drawn from it — *the app is 95% busy* — was wrong and
plausible, which is the dangerous combination. Two exec breakpoints were armed
and the stops assumed to alternate `A, B, A, B`; the run happened to begin on
`B`, so every "A → B" gap was really `B → A`, and 37.8 ms is `put → keep →
compose` (14.3 + 23.6) read as compose. The off-by-one was noticed and then
argued away: *if both pairs were offset the totals would not reconcile* — true,
and useless, because only ONE of the two pairs was offset. Both reconciled to
54.9 ms and both looked right. **The fix is one line** — read `ip` at each stop
and label it — and it is the only thing that distinguishes the two readings.
The corrected table above came from a re-run that does it; the number that
survived unchanged from the bad run is the blit, because that pair happened to
start on the right address.

**A fixed-size scratch buffer is a performance decision in disguise.** `WR_BW`
and `WR_BH` read as capacity constants — how big a mask can this package hold —
and were silently also the per-frame cost, because the whole buffer was cleared
and the whole band blitted whatever the figure needed. Nothing in the code said
so and nothing in §78.8 said so. They are now the mask's capacity and only
that; `wr_bw`/`wr_bh` carry the frame's. Worth checking wherever a package
composes into a buffer it declared once — the saver's cube (§79.5.6) has the
same shape, and there the figure genuinely does fill its band.

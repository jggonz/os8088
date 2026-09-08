# The Last Drop Of Performance


> **A REGISTER, NOT A PLAN WITH AN END.** Every row here is deferred
> indefinitely: something the machine COULD do, priced, with the reason it is
> not taken today. It lives under `docs/plans/` because a row is a proposal
> rather than a description of the tree — but it is *outstanding forever* by
> design, and it is finished when it is empty, which is not a state anyone is
> working towards. **Nothing here is a commitment and no row is owed.**
>
> Read it the way it is written: before optimising something, to see whether it has already been built,
> measured and shelved for not clearing the price of its own footprint —
> and, since §4, to see what was TAKEN, so a decision is re-made rather
> than rediscovered.

> Companion: **docs/plans/LAST-DROP-BYTES.md** is the same idea for BYTES — the menu of
> footprint the machine could still get back, and the register of what is refused
> and why. This file is CYCLES; that one is BYTES.

**Optimisations that were built, measured, and found CORRECT — and shelved
anyway, because they did not clear the price of their own footprint.**

**...and, since §4, the INVERSE**: a change that was **taken**, whose price is
recorded here so that it can be re-decided rather than rediscovered. The two
classes belong in one file because they are one question asked from opposite
sides — *is this trade worth it?* — and because the thing that rots is not the
verdict, it is the arithmetic behind it. An entry marked **TAKEN** is in the
shipped kernel today; every other entry is not in the tree at all.

This is not the negative-results file. A negative result is a thing that does
not work: SPEC.md §19.2.3.1's track alignment that was 3 calls *worse*,
§48.18.1's vector `gfx_fill` that recovers 4%. Those are written down so nobody
re-derives them. **This file is the other class** — changes that do exactly
what they claim, at a price that is currently too high. They are not wrong,
they are not finished, and without a record they get re-invented, re-built and
re-measured from scratch.

PERFORMANCE.md Set 29 is what this file exists to prevent a second time: two
candidates were *named* in Set 28 and had to be fully built to be judged. The
one that lost is below, in enough detail to re-apply in an afternoon.

**Every entry carries the same five things**: what it does, the patch, the
measurement, the price, and — the part that matters — **what would have to
change for the answer to flip**.

---

## 1. `font_run`: unrolling the eight row passes

**Status:** built, measured, byte-correct, **shelved.**
**Against:** SPEC.md §6.1.7's row-major pass.
**Verdict:** −4.74% for +267 bytes of `.text` and a crossed rung, against a
5% bar.

### What it does

§6.1.7's drawing pass runs eight times, once per glyph row, with the row
number in `BP`:

```
    mov al, [ss:bx]             ; 3 bytes - the glyph row byte
    add bx, bp                  ; 2 bytes - ...+ the row this pass is on
```

Unroll the eight passes and the row becomes an **assembly-time constant**, so
it rides in the fetch's `disp8` and the `add` disappears with it — and so does
`BP`, and the row counter, and the loop that carried them:

```
    mov al, [ss:bx + %1]        ; 4 bytes, %1 = 0..7
```

NASM folds row 0's `+0` back to the no-displacement form, so that one pass is
a byte shorter again.

### The patch

A macro invoked eight times — not eight hand-written copies, which is how two
copies of a five-instruction body stop agreeing:

```nasm
%macro FONT_RN_ROW 1
    mov si, font_rn_tab
    mov cx, [font_rn_n]
    push di                     ; this row's first byte
%%px:
    lodsw                       ; AX = this cell's glyph base (DS:SI)
    xchg ax, bx                 ; BX = it, in one byte
    mov al, [ss:bx + %1]        ; the glyph row - %1 is the row, at last
    and al, dl
    xor al, dh
    stosb
    loop %%px
    pop di
    add di, [cs:vid_rowadd]     ; gfx_nextrow, once a row
    test di, [cs:vid_wrapbit]
    jz %%nw
    add di, [cs:vid_wrapfix]
%%nw:
%endmacro
```

and at `.rm`, in place of the `xor bp, bp` / `.rmrow` / `inc bp` / `cmp bp, 8`
loop:

```nasm
%assign FRR 0
%rep 8
    FONT_RN_ROW FRR
%assign FRR FRR+1
%endrep
    jmp .out
```

`font_run_x` keeps its `push bp` — pass 1 still counts cells in it.

### Measured

`tests/gfxbench` on MartyPC, CGA, a cycle-accurate 4.77 MHz 8088. Same kernel
either side, nothing else different.

| row | baseline | unrolled | delta |
|---|---|---|---|
| `FONT_RUN 10 aligned` | 3,131.77 µs | 2,983.21 | **−4.74%** |
| `FONT_RUN 20 text` | 5,394.84 | 5,155.22 | **−4.44%** |
| `FONT_RUN 20 padded` | 5,393.73 | 5,154.38 | −4.44% |
| `FONT_CHAR` · `FONT_STR` · `PAIR` · skewed · every `GFX_*` | — | — | ±0.01% |

### The price

`.text` **+267 bytes** — against the ~240 estimated — and it **crosses an
image rung**: `KERN_SIZE` 96,256 → 96,768, spare 2,048 → **1,536**, three
steps, under the four this tree keeps.

### Why it is shelved, beyond the bar

Two things make it a weaker candidate than −4.74% suggests, and both are
properties of the change rather than of the day it was measured.

**The disp8 saves ONE byte a cell-row, not two.** `mov al,[ss:bx+r]` is 4
bytes where `add bx,bp` + `mov al,[ss:bx]` is 5. The rest of the win is the
row counter, which is paid once a run.

**It decays on exactly the runs that matter.** Solving the 10- and 20-cell
rows separates the saving into a **fixed 57.5 µs** (the counter) and **9.1 µs
a cell** (the `add`). So the percentage falls as runs lengthen — 4.74% at ten
cells, 4.44% at twenty — and the consumers this path exists for draw long
runs: Note Pad's `np_rflush` letters a row padded to the whole band (§27.2),
Tracker's `tui_str` draws the FT2 screen, ModPlug composes four LCD lines.

**And it is partly antagonistic with §6.1.8.** The unroll's per-cell saving
applies to cells drawn *singly*; the trailing span moves cells out of that
loop and into a `rep stosb`. On a padded run — the case §6.1.8 exists for —
there are fewer cells left for the unroll to help. The two do not add.

### What would flip the answer

- **267 bytes of `.text` coming free inside the current image rung.** It has
  116 left, so ~151 bytes would have to be found elsewhere first; then the
  unroll costs no rung and the question is only whether 4.74% is worth 267
  bytes.
- **A workload dominated by span-less runs** — which is the opposite of what
  §6.1.8 is tuned for, so the two questions should be re-asked together, not
  separately.
- **A cheaper unroll.** Eight copies of the *whole* row block is what costs
  267 bytes; the inner loop is only 12–13 of them. Unrolling the cell loop by
  two inside a single row pass, or finding an addressing form that reaches the
  glyph row without either `BP` or a `disp8`, would buy most of the win for a
  fraction of the image.

---

## 2. Interior spans, and the full run-length encode

**Status:** costed on paper, **never built.**

SPEC.md §6.1.8 collapses the **trailing** span only. The obvious generalisation
is to run-length encode the whole run in pass 1 — `(glyph, count)` pairs — and
let every repeated span become a `rep stosb`, which would also catch the gaps
*between* columns rather than only the padding after them.

It was dropped before it was built, for a reason worth keeping:

**`rep stosb` needs the count in `CX`, and `CX` is the entry counter.** The
register file is full in that loop — `AX` scratch, `BX` glyph, `CX` count,
`DX` masks, `SI` table, `DI` framebuffer, `BP` row, `ES` framebuffer segment —
so the outer loop would have to terminate on `cmp si, [end]` instead of
`loop`, which is 6 bytes an entry against 2. Add the count fetch and the
per-entry cost goes from 14 bytes to about 20: **a ~43% loss on text with no
repeats at all**, to win on text that has them. The trailing-span form avoids
this entirely by being *one* span, outside the loop, with its count loaded
once a row.

A form that keeps `loop` and still reaches interior spans would be worth
building. Nobody has found one.

---

## 3. The blit decoder's HYBRID — a run path kept for flat art

**Status:** costed against measured constants, **never built.**
**Against:** SPEC.md §5.4.1's decoder (docs/plans/completed/HANDOFF-REDRAW.md item B2).
**Verdict:** buys 1.96x on a row with **one run in it** and nothing at all on
any other row, for ~366 bytes and a permanent second code path — and §5.4.1.2
has since moved the crossover from ~10 runs a row to **~3**, so the band it
wins in is narrower than when it was costed.

### What it does

The byte decoder walks the destination byte by byte and costs the same
whatever is in the row. The run path costs `830 + 371 x runs` µs a row
(PERFORMANCE.md Set 43, fitted to three measured densities within 3%), so it
is *cheaper* on rows with almost nothing in them. The hybrid keeps both and
picks per row — from the PREVIOUS row's run count, pictures being locally
coherent, first row on the run path, a wrong guess costing one row. Per row
and never mid-row, so the two paths have no seam to disagree at.

### Measured

Both halves are built and measured now (PERFORMANCE.md Sets 41, 42 and 43), so
these are figures rather than estimates. The run path costs `830 + 371 x runs`
µs a row and the decoder 1,948 (259.1 ms over 133 rows), so the crossover is
**~3 runs a row** — it was ~10 before §5.4.1.2's aligned bodies:

| CGA, Paint's canvas | run path | decoder |
|---|---|---|
| flat, 1 run a row | **132.1 ms** | 259.1 |
| textured, 85 runs a row | 2,430.7 ms | **259.1** |
| fine, 308 runs a row | 8,364.9 ms | **260.2** |

### The price

- **~336 bytes that the decoder would otherwise delete** — `sw_blit_span`
  (167), `vga_blit_span` (157), `vga_sr_on` (12) — because the hybrid needs
  them alive, plus ~30 for the switch itself.
- On `kern_big` that is the difference between fitting in the image rung's
  existing slack and **crossing a 512-byte rung**, at a moment when the
  footprint has 512 bytes spare.
- And a permanent second path through the hottest drawing routine in the
  system, which every future change to either half has to keep byte-identical
  with the other.

### Why it is shelved

**Nothing anyone waits on draws flat art through `gfx_blit4`** — and the
decoder shipped without it (SPEC.md §5.4.1.1), so this entry is now the record
of a deliberate omission rather than of an unbuilt option. Its consumers
are Paint's canvas, Solitaire's card backs (a lattice, 336 runs on CGA by the
source's own count) and **ArtfulType's keystroke path**, which puts a whole
line of TEXT on screen as one blit — and text is the least flat thing there
is. The one case the hybrid protects is a blank Paint canvas, which is 132 ms
against 259: an operation nobody is sitting through, made 127 ms slower —
half what it was when this was written, because §5.4.1.2 took the decoder's
side of the comparison down and left the run path exactly where it was.

There is also a property worth having on the other side of the ledger:
decoder-only makes a blit **constant time in its content**. A UI with one
predictable cost is better than one with a fast case and a slow one, and it
is what stops "never price a blit on flat art" (docs/plans/completed/HANDOFF-REDRAW.md) being
a rule anybody has to remember.

### What would flip the answer

- **A consumer that blits large flat areas often.** A picture viewer with
  letterboxing, a window background drawn as a blit, a game with flat sprites.
  None exists today; the moment one does, this is 1.96x on it.
- **A cheaper switch than "the previous row".** If the run count for a row
  were already known — say the source carried it — the switch would be a
  compare and the 30 bytes would be most of the cost. That is a change to the
  blit's ABI, not to this routine.
- **A budget with room in it.** The verdict above is 336 bytes against a rung;
  on a kernel with four steps spare rather than one, the same measurement
  reads very differently.

---

## 4. `gfx_xor_strips`: one outline decomposition instead of four — **TAKEN**

**Status:** built, measured, **SHIPPED** (`d0edd73`); **re-decided at the
post-squash review and KEPT** (PERFORMANCE.md Set 115, below).
**Verdict:** **−352 bytes** for **+37% on a VGA `gfx_xor_rect`**, which is
**+2.22 ms of a 55 ms drag tick** and could not be seen by hand. Taken — and
written down here because the cost is real, is confined to one primitive, and is
the entry to re-open the day something XORs rectangles faster than a drag does.

### What it does

The same four-strip decomposition of a rectangle outline was written out **four
times**: `gfx_xor_rect_clip` did it through the public `gfx_xor_fill`;
`gfx_xor_rect_raw` did it on VGA through hand-rolled `vga_xor_hline` and
`vga_vline_core`; `softgfx.inc`'s `sw_xor_rect` did it through `sw_xor_fill`; and
`gfx_frame` drew the same rectangle as two hlines and two vlines with overlapping
corners — which in replace mode is the identical pixels, the strips' top and bottom
being full width.

`gfx_xor_strips` is that decomposition **once**, with the fill routine to draw a
strip with carried in `BP`. Three heads: `gfx_xor_rect_d` passes
`gfx_xor_fill_raw` (already inside the display hook and past the clip),
`gfx_xor_rect`'s clipped arm passes the public `gfx_xor_fill` so each strip meets
`GFXCLIP` itself — an outline is not the intersection of its bounding rect with
anything — and `gfx_frame` passes `gfx_fill`. `vga_xor_hline`, `vga_vline_core` and
`sw_xor_rect` then have no callers at all and are deleted.

**−291 `.text` in `vga12.inc` and −61 in `softgfx.inc`.** It also retires the last
uses of `SCREEN_W`/`SCREEN_H`/`ROW_BYTES` as clip BOUNDS in that file, which were
safe only because a VGA display really is 640×480.

### Measured

`tests/gfxbench` on MartyPC, cycle-accurate 4.77 MHz 8088, before and after the
same merge:

| row | adapter | before | after | delta |
|---|---|---:|---:|---|
| `GFX_XOR_RECT 64x64` | **VGA** | 2,997.29 µs | 4,109.27 | **+1,111.98, +37.10%** |
| `GFX_XOR_RECT 256x128` | **VGA** | 5,469.71 | 6,584.53 | **+1,114.82, +20.38%** |
| `GFX_XOR_RECT 256x1` | **VGA** | 833.21 | 1,073.99 | +240.78, +28.90% |
| `GFX_XOR_RECT 64x64` | HERC | 5,021.09 | 4,945.20 | −75.89, −1.51% |
| `GFX_XOR_RECT 64x64` | CGA | 5,142.58 | 5,201.80 | +59.22, +1.15% |

**The shape of the cost is the finding, not the percentage.** The absolute delta is
**the same ~1,112 µs at 64×64 and at 256×128** — sixteen times the area, the same
bill. It is **fixed per call**: three extra rect arrivals per outline, each paying
its own `vga_rect_setup`, its own arming and its own `vga_gc_reset`, where the old
VGA arm armed once and ran four hand-rolled edges. Nothing about it is per-pixel,
which is why the percentage falls as the rect grows and why the degenerate 256×1 —
which collapses to fewer strips — pays a fraction of it.

**Both 1bpp adapters are unchanged**, and that is structural rather than lucky:
that path already decomposed into four `sw_xor_fill` strips and always has. The
whole cost is VGA's.

### The price, in the thing that actually draws XOR rectangles

`ui_drag`'s `.track` loop is paced to one tick by its `.linger`, so a held drag is
**exactly two XOR rects a tick** — erase and redraw — inside a **held `gfx_lock`**.
At +1,112 µs each that is **+2.22 ms of every 55 ms tick, about 4%**, and the
window in which the outline is DARK gets longer at both ends. That second half is a
double-draw flash, VGA only, and **invisible in every emulator in this tree**.

Two things weigh against it, and both are on the record:

* **The owner drove old against new by hand on a held window drag and could not
  tell the difference.** That is the strongest evidence there is for a defect whose
  whole class is "does not show in an emulator".
* **`gfx_frame` got FASTER on all three adapters** in the same merge — same four
  calls, one wrapper frame shallower: HERC 6,782.60 → 6,514.44 (**−3.95%**), CGA
  6,944.18 → 6,752.92 (**−2.75%**), VGA 3,917.00 → 3,895.17 (−0.56%). `gfx_frame` is
  drawn far more often than a drag outline is.

**The prediction was 60% low.** The commit costed it at "+0.7 ms per XOR rect …
+1.4 ms of a 55 ms tick" and the measurement is +1.11 and +2.22. The *shape* —
fixed per call, VGA only, 1bpp free — was right; the magnitude was not, which is
the ordinary reason PERFORMANCE.md rule 4 says to measure.

### What would flip the answer

* **A consumer that XORs rectangles faster than a drag does.** A game's sprite
  erase, a rubber-band selection updated more than once a tick, marching ants, any
  outline drawn in a loop. Today the consumers are `ui_drag` (two a tick), the
  dock's focus rect (one per focus change) and, through API slot 0x0050
  (`OSAPI_GFX_XOR_RECT`), four packages: Paint's rubber band and marquee
  (`pt_rb_xor`, `pt_marq_xor`, paced by `pt_wait_tick`), Solitaire's card-drag
  outline (`sol_dxor`) and Cyclone's aiming box (`cy_cur_xor`) - each one or
  two rects an event, so the accounting below holds for the whole set. At ten rects a tick this entry is
  11 ms of a 55 ms tick and the answer is different.
* **A drag that stops being tick-paced.** The 4% is 4% *because* `.linger` caps the
  loop at one pass a tick. Anything that makes the drag smoother makes this dearer
  in exact proportion.
* **The flash being SEEN.** `gfxbench` answers the microsecond question and cannot
  answer this one. The commit named `deskbench` on VGA and `os88marty.py flicker`
  over one held drag as the measurement; `dockmark` is the control and not the
  measurement, since one rect per focus change says nothing about 36.4 a second.

  **`deskbench` HAS NOW BEEN TAKEN** — the table is in PERFORMANCE.md, *What a
  BUSY DESKTOP costs*. It does not settle this, because there is no before: it
  is the first reading, so it is the baseline the next change is compared
  against and nothing more. What it *does* show is the asymmetry this entry
  predicts, in the row that exercises the path. Moving the bottom window of a
  four-window scene — which repaints the three above it — reads a
  transient÷changed ratio of **0.96 on CGA, 0.97 on Hercules and 3.15 on VGA**,
  and 3.15 means the outline is written and unwritten three times for every
  pixel the move actually moved. That is the only row in the eleven where the
  adapters differ in kind rather than in size, and VGA-only is exactly the
  shape of the merge: three extra `gfx_xor_rect` arrivals per strip, 1bpp free.
  It is **not** proof, since the same asymmetry would follow from VGA simply
  having more to repaint, and the honest test remains the one this entry
  already names — a flicker run over a held drag, built both ways.
### Re-decided at the review, with the body in hand

The code review that followed the size passes flagged this row from the diff
without having read this entry, and PERFORMANCE.md Set 115 re-measured it
against the last squash: `GFX_XOR_RECT 64x64` **2,997 → 4,083 µs (+36%)**,
256x128 5,470 → 6,552, 256x1 833 → 1,068 — the same fixed ~1.1 ms per call the
table above found. The reviewer's proposed fix was then BUILT, not argued: the
pre-pass body — `vga_set_xor` once, `vga_xor_hline` twice, `vga_vline_core`
twice, `vga_gc_reset` once — restored under `GFX_VGA` with its clipping on
`[vid_cw]`/`[vid_ch]` so the EGA row shares it. It measures **3,087 µs** (the
3% over the pre-pass figure is the X cell's, Set 115), 256x128 5,558, 256x1
881, for **304 bytes of `kern_big` `.text` and one image rung**, and it is
pixel-exact on the glass: one outline changes exactly the perimeter's on-screen
pixels in five shapes (a 64x64, a two-row, a one-column, one hanging off the
top-left, one off the bottom-right) and a second restores the frame.

**And it was taken out again**, because the accounting above still holds: the
outline's only consumers are `ui_drag` — two a tick, inside a tick-paced loop —
and the dock's focus rect, and neither pays a frame for the difference. 304
bytes against 2.2 ms of a drag tick that the owner could not see by hand is the
trade this entry took the first time. What the review adds is the body's own
measured price, so the day the list under *What would flip the answer* gains an
entry, the answer is a `git show` of the review's commit and 304 bytes — not a
rebuild from this description. The clipped head and `kern_small` were never in
the question: the first sends each strip through `GFXCLIP` on purpose, the
second has no VGA.

* **A cheaper merge — the version to build if any of the above happens.** The
  1,112 µs is three extra *arrivals*, not three extra decompositions.
  `gfx_xor_strips` calls a whole rect fill per strip through `BP`, and each VGA
  arrival re-runs setup, arming and `gc_reset`. A strip walker that armed the GC
  **once** and drove four rect setups inside one bracket would keep all 352 bytes
  and most of the 1,112 µs. Nobody has costed one. It is not free — the clipped arm
  deliberately sends each strip through `GFXCLIP` separately, so the one-bracket
  form can only serve the unclipped head — but the unclipped head is `ui_drag`'s,
  which is the only one that pays.

---

## The apparatus, so it is not rebuilt

Everything Set 29 needed already exists in the tree:

- **`tests/gfxbench` carries `FONT_RUN 20 text` and `FONT_RUN 20 padded`** —
  the same length, differing only in content, because the original
  `C-2 01 A0F` has no adjacent repeat and a span optimisation measures as
  *exactly nothing* on it.
- **The A/B recipe** is a bootable 360KB disk per kernel:
  `make BUILD=build/<tag> build/<tag>/boot360.bin`, then `os88disk.py` with
  `--boot`/`--kernel` and `build/gfxbench.o88` in the root. Both disks must
  come out the same cluster count, or the Disk window's `Free NNNK` differs
  and the pixel gate reports it as a defect (it is not — PERFORMANCE.md Set 28).
- **The pixel gate** is `desktop → chip menu → Disk window` captured through
  `vram` on both mono adapters and diffed byte for byte. Mask the menu bar
  clock: with no RTC it starts at `Jul 04 2026 00:00` and a run crossing the
  minute reads `00:01`.
- **Span frequency** is four counters in `.text` (`inc word [frdbg_runs]` and
  friends) read back with `os88marty.sym`. Scratch instrumentation, never
  committed.

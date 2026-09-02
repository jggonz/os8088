# The Last Drop Of Performance

**Optimisations that were built, measured, and found CORRECT — and shelved
anyway, because they did not clear the price of their own footprint.**

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
**Against:** SPEC.md §5.4.1's decoder (docs/HANDOFF-REDRAW.md item B2).
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
is what stops "never price a blit on flat art" (docs/HANDOFF-REDRAW.md) being
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

# Byte-boundary text — the windows that do not align their own

**Status: the KERNEL half is BUILT and measured, and SNAPPING IN Y IS ANSWERED
- NO, see §6: it would gain nothing on any adapter, and the vertical quantum
that does pay is the BANK COUNT applied to a scroll DELTA rather than 8 applied
to a window origin. §6.3 is the real opportunity that investigation found -
`gfx_scroll` recomputing a row address per row, a third of its cost on mono.

**The x half is done:** `WF_SNAP`'s `[vid_mono]` gate is gone (SPEC.md §11.94,
PERFORMANCE.md Set 52) and the flag is INVERTED, so every window's content
origin is on a multiple of 8 unless it opts out (SPEC.md §11.94.1, Set 53).
§§1–5 below are the remaining work: the ~14 windows whose own text does not
land on that boundary, and what each one costs to fix. Nothing there is
started.**

The one-line summary: **the kernel now guarantees the window's origin is
byte-aligned, and roughly half the tree's windows then put their text at an
odd offset from it and give the win straight back.**

---

## 1. What an aligned pen is worth

Measured, not modelled — a cycle-accurate 4.77MHz 8088, `tests/typebench`'s 40
keystrokes with the whole 40-cell line redrawn after each (what `np_redraw`
does to its dirty band):

| adapter | CHAR aligned | CHAR skewed 5 | alignment is worth |
|---|---:|---:|---:|
| Hercules | 1424 ms | 1472 ms | **3.4%** |
| VGA | 1013 ms | 1108 ms | **9.4%** |

`tests/gfxbench` on VGA prices the primitive higher: `PAIR 10 aligned`
6984.93 µs against `PAIR 10 skewed 5` 7855.51 µs, **12.5%**.

**Why VGA gains most, which is the thing this whole line of work turned on:**
mode 12h is eight pixels to a byte, four planes of one bit, so an unaligned
8x8 cell spills into a second framebuffer byte there exactly as on a Hercules —
and `font_char`'s planar `.vram` path pays for the spill with a second `GC8 Bit
Mask` `out` **and** a second latched read-modify-write, once per row per cell.
Its `.no_second` early-out is taken only when `x & 7 == 0`.

So the fixes below are worth **3–12% of the drawing** in the window they touch,
which on a 4.77MHz machine is the difference between a repaint you watch happen
and one you do not. They are also, without exception, **one constant each**.

---

## 2. The list

Each row is one offset. The pen is content-relative, and the content origin is
now guaranteed `≡ 0 (mod 8)`, so `offset mod 8` is the whole question.

### 2.1 The Disk window — the biggest single win left

| what | pen | fix | risk |
|---|---|---|---|
| header (`Drive B:  7 files`) | `fm_cx + 6` | 6 → 8 | 2px shift of one string |
| icon-grid cells | `fm_cx + 78 × col` | `FMI_CELL_W` 78 → 80 | grid columns move; check the cell still holds a 16px icon + label at the narrowest content width |
| row icon inset | `fm_cx + 4` | 4 → 8 | see below — this one is not about text |

Its **file rows are already aligned** (`fm_cx + 24`), which is why the
inversion was worth taking for this window at all: those ~40 `font_str`s
dominate `fm_repaint`. The header is one string per repaint and worth little on
its own; the **icon grid is worth the most in this file**, because every label
in the grid is off-grid *and by a different amount per column* (78, 156, 234 …
≡ 6, 4, 2 …), so no single window position can help any of them.

**The icon inset is a separate, larger prize.** `fm_scrollpaint` rounds its
blit span INWARD because a row's icon starts 4px in, and with an aligned origin
that strip is now *always* 4px wide — so SPEC.md §22.11.1's strip pass runs on
every scroll, where an unaligned `k = 4` used to skip it one window position in
eight. Moving the inset 4 → 8 would make the row band byte-aligned at both
ends and **delete the strip pass from the common case entirely**. It costs 4px
of row indent everywhere and wants `fm_hit` checked alongside it (SPEC.md §22's
one-place-for-geometry rule).

### 2.2 The apps

| app | off-grid pen(s) | note |
|---|---|---|
| **Tracker** | 6, 38, 108, 111, 116, 150, 205, 210, 258, 290 | the **largest** item here. Its pattern grid was already moved onto 8px boundaries to earn §6.1 (SPEC.md §45.9); the rest of the FT2 face never was — 17% of sampled literal pens aligned. It also redraws continuously while playing, so this is the app where the percentage is spent most often |
| **Tamegram** | 4, 60, 116, 164, 210 | HUD; 7 of 8 sampled pens ≡ 4, so a single `+4` on the base would align nearly all of it |
| **Fractal** | `FR_X_ZOOM` 130, `FR_X_ZNUM` 170, `FR_X_PAL` 250 | all ≡ 2; `FR_X_PCT` 200 is already 0. **Confirmed by measurement, and it is the worst in the tree: 2,801 glyphs in one launch, 16% aligned, 2,323 of them at bucket 2** — the status row redrawn per progress tick. Four constants in one table |
| **Paint** | `PT_PAL_X0` 1, plus pens at ≡ 1 and ≡ 2 | 2 of 5 sampled pens aligned. The palette is drawn on every tool change |
| **ArtfulType** | a literal 14 | **the "≡ 3 (measured)" here was its own CAPTION** — 10 centred glyphs the kernel drew, now excluded (§11.94.2). Its 8 and 16 are already right. Re-audit before moving anything; §46 is a *writer*, so its per-keystroke line draw is the typebench case |
| **HDD Control Panel page** | `HDP_LX` 2 | `HDP_F1X-10` 56 and `HDP_F2X-10` 104 are already aligned |
| **HDD tool window** | `HTW_LX` 4 | the installer's own `HIW_LX` is already 8 |
| **Recorder** | 4 | |
| **Minesweeper** | 4 | also draws at 8 |
| **Piano** | 2 | one sampled pen — low confidence, needs the runtime audit |
| **Arkanoid** | 2 | one sampled pen — same |
| **Task Manager** | five literal `+6` sites | despite `TM_PEN` being 8 and `TM_MPEN` 16. SPEC.md §11.94 records moving `TM_PEN` 6 → 8; these five were missed |
| ~~**Hello**~~ | **not a defect** | `hl_line` CENTRES every string — `(HL_CONT_W - width)/2 + content_left` — so it is §3's category 2 and must not be "fixed". Its earlier "≡ 3 for 5 glyphs" was its own caption plus that centring |
| **Missile Command** | unresolved | every pen is computed at run time. Needs `make SNAPAUDIT=1`, not a grep |
| `DEBUG.DRV` | `DBGP_LX` 1 | unshipped (SPEC.md §62.9.4); fix if it is ever restored |

**Already aligned, and most of them because somebody moved two pixels to get
there:** Note Pad (`NP_MARGIN` 6 → 8), the Task Manager (`TM_PEN` 6 → 8), the
Timer (`APP_TMR_DX` 47 → 48), Solitaire, the HDD installer (`HIW_LX` 8), the
Disk window's file rows (`+24`) — and **Frotz**, which rounds its own text
origin up to a multiple of 8 in `zw_bounds` and so never needed the flag at
all. Frotz is the model: an app that does its own rounding is correct on every
adapter and under any window policy.

---

## 3. Three kinds of off-grid pen are CORRECT

Do not "fix" these, and do not let a future audit report them as defects:

1. **A right-aligned numeric column** — the Disk window's size column ends at
   `rgt - 21`, the Task Manager's `CPU`/`MEM`. The right edge is the
   constraint; the left edge is wherever the number's width puts it.
2. **A centred string** — `ui_note`'s message, Missile's `DEFEND YOUR CITIES`
   banner, the Timer's digits (which is why `APP_TMR_DX` cost one pixel of
   asymmetry to align).
3. **A label tied to a control** whose own geometry is not on a multiple of 8 —
   moving the text without the control is worse than leaving both.

The rule that separates them from the list in §2: **an item belongs in §2 only
if the pen is a constant offset from the content origin and moving it moves
nothing else.**

---

## 4. The instrument

`make SNAPAUDIT=1` (SPEC.md §11.94.2, `kernel/font.inc`) histograms the `x & 7`
of every glyph the machine draws; `tools/os88snap.py` resets, filters and reads
it. Four tables, split by `wm_pkgcall`'s stacked `snap_cur`, so what a window
draws inside its own callback is counted apart from the chrome around it, and
the host can aim the filter at ONE window.

It only means anything now that alignment is the default: with the content
origin on a multiple of 8, a glyph's **screen** `x & 7` IS its
content-relative one, so a non-zero bucket is the app's own offset and nothing
else. Before the inversion the same histogram measured where the user had
dragged the window.

A plain build emits not one byte of it — macro and counters live inside the
same `%ifdef`, and it sits at the top of `font_char`, the innermost drawing
call in the system.

**The artifact is FIXED and the instrument now names the string** (SPEC.md
§11.94.2, PERFORMANCE.md Set 57). The "constant 4 glyphs in bucket 7" was
`wm_draw_title`: a caption is **centred in the title bar by the kernel**, no app
can influence it, and it was being attributed to whichever callback bracket
happened to be open — the constant 4 being the **`'APPS'` caption of the Disk
window sitting behind every app under test**. `wm_draw_title` suspends the
attribution now. The log also records the pen y and the **character**, so it
prints the off-grid text rather than a bucket count.

**Two things about driving it:**

- **A drag no longer forces a repaint.** §11.96.12's drag cache replays the
  content instead of calling `W_PAINT`, so a dragged window reports nothing —
  which is what the first version of this survey did. Reset with **no filter**
  and then launch the app: its first paint runs inside its own `wm_pkgcall`, so
  it lands in `snap_h*`. Two windows' callbacks then share that table, which the
  pen y separates.
- **Several apps draw no text until interacted with** — an empty Note Pad draws
  no glyphs at all, which an early attempt measured as "aligned".

The `§2` figures marked "sampled literal pens" come from neither: they are the
literal `mov cx, N` values within a few lines of a text call, so they say *this
app has off-grid pens* and **not** *this fraction of its glyphs are off-grid*.
An app whose pens are all computed shows as nothing at all.

---

## 5. Ordering

By what the percentage is spent on rather than by size of diff:

1. **Tracker** — redraws while playing, and has the most off-grid pens.
2. **Disk window icon grid** (`FMI_CELL_W` 78 → 80) — every label off-grid by
   a different amount per column.
3. **Disk window icon inset** (4 → 8) — not text at all, but it deletes
   §22.11.1's strip pass from every scroll.
4. **ArtfulType** — a writer, so it is the typebench case exactly.
5. **Fractal, Paint, Tamegram** — one constant table each.
6. **Task Manager's five `+6`s, Recorder, Minesweeper, HDD's two pages,
   Hello** — small, mechanical, and Hello matters because it is the template a
   new package starts from.
7. **Piano, Arkanoid, Missile** — audit first; do not move a constant on one
   sampled pen.

Each item is its own commit, because each is a **visible layout shift** and
wants looking at on a 1bpp adapter (SPEC.md §47's rule about greying applies to
the same class of change: a mono adapter differs from VGA in kind, not just in
depth). Verify the way SPEC.md §11.94.1 was verified — an identical scripted
session against a reference build, framebuffers diffed — and expect the diff to
be non-zero here, because the pixels really do move. What must not change is
the *content*: same strings, same widths, nothing clipped at the right edge
that was not clipped before.

---

## 6. Snapping to Y — INVESTIGATED, and the answer is no

**Asked and answered: a `WF_SNAP` in Y would gain nothing, on any adapter.**
The investigation is kept because the reasoning redirects the effort somewhere
that pays, and because "we never checked Y" is otherwise a question that comes
back every time somebody reads §11.94.

### 6.1 Text: provably zero, not merely small

The vertical layout is the banked framebuffer (SPEC.md §39.3), and the
parameters are:

| adapter | banks (`y &` mask) | `rowadd` | `wrapfix` |
|---|---|---|---|
| VGA | 1 (mask 0) | +80 | — |
| Hercules | 4 (mask 3) | +0x2000 | +0x805A |
| CGA | 2 (mask 1) | +0x2000 | +0xC050 |

`gfx_nextrow` is `add di, rowadd` plus a test, and it pays one extra `add`
only on the step that carries out of the last bank. **A glyph is 8 rows, and 8
is a whole multiple of both 2 and 4** — so among the 8 row-advances an 8x8 cell
makes, the number that wrap is exactly 8/banks (4 on CGA, 2 on Hercules)
**wherever the cell starts**. Not approximately: among any 8 consecutive rows
the count with `y & 1 == 1` is exactly 4 and with `y & 3 == 3` is exactly 2.

On VGA there are no banks at all — `bmask` and `wrapbit` are 0, so
`gfx_nextrow` is `add di, 80` with the test always falling through.

So text has **no** y-dependence to recover. This is the opposite of the x case,
where §6.1.4's arithmetic showed an unaligned cell spans two framebuffer bytes
and must read-merge-write both: there is no vertical equivalent, because a row
is a row whatever y it is.

### 6.2 Fills already solved it, in registers, with no alignment at all

`sw_plane_op` banks `gfx_nextrow`'s two parameters in registers, so a row step
is three register instructions rather than a near call plus two CS-overridden
memory reads — done for SPEC.md §5.7's reasons, and note *how* it was done: by
holding the increment, not by constraining y. **Measured confirmation:** a
breakpoint on `gfx_nextrow` during a Hercules desktop paint **never fires**,
because every row loop that matters has inlined it.

### 6.3 What the investigation DID find: `gfx_scroll` recomputes what it could add

`gfx_scroll`'s two backends walk their rows the other way round, and both
compute a full row address **per row** for an address that advances by a
constant:

- `vgas_lincopy` (VGA, and the buffer planes): two `mul word [vid_stride]`
  per row.
- `vgas_bankcopy` (Hercules and CGA): two `gfx_rowbase` calls per row, and
  `gfx_rowbase` is a `mul`, a variable `shr`, a bank-table lookup and a
  call/ret.

**Measured on a cycle-accurate 4.77MHz 8088 with a Hercules card:**

| | measured |
|---|---:|
| `gfx_rowbase`, one call | **11 instructions, 319 cycles = 66.84 µs** |
| `GFX_SCROLL 256x128` (gfxbench) | **51,229 µs** = 400 µs per row for 32 bytes |
| `GFX_FILL 256x128` (same bytes, register row step) | 24,338 µs = ~185 µs per row |
| `GFX_SCROLL 256x128` on VGA | 29,096 µs = 227 µs per row |

So **two `gfx_rowbase` calls are 134 µs of the mono scroll's 400 µs per row —
a third of it** — and the fill, doing comparable per-row work with a register
row step, costs less than half as much per row.

The fix is the fill's: hoist one row address out of the loop, step it with the
register form, and derive the other end from a constant delta. **BUILT and
measured** (SPEC.md §5.5.1, PERFORMANCE.md Set 56): `GFX_SCROLL 256x128` goes
51,229 → **34,472 µs on Hercules (1.49x)** and 29,096 → **19,194 µs on VGA
(1.52x)**, against an estimate of ~1.45x and ~1.3x. The estimate was low on VGA
because the two `mul`s were a larger share of a shorter per-row cost there.

### 6.4 …and where a vertical quantum earns its keep — `nbanks`, not 8

The constant-delta trick above needs `rowbase(y+dy) − rowbase(y)` to be the
same for every row, and that is where alignment finally appears:

- **On VGA it is free and unconditional.** Linear addressing means the delta is
  `dy × stride` for any `dy`, so the VGA backend can drop its per-row
  multiplies today with no precondition whatsoever.
- **On the banked adapters it holds iff `dy ≡ 0 (mod nbanks)`** — `dy` a
  multiple of 2 on CGA and of 4 on Hercules — because only then do source and
  destination sit in the same bank and differ by `(dy / nbanks) × stride`.

**So the quantum that matters vertically is the BANK COUNT, and it applies to
the scroll DELTA rather than to the window's origin.** That is why snapping a
window in Y is the wrong lever: it constrains the wrong number.

The tree's real deltas mostly already qualify:

| caller | `dy` | CGA (2) | Herc (4) |
|---|---:|---|---|
| Note Pad row scroll (`NP_SB_STEP` 4 rows) | 32 px | ✅ | ✅ |
| Disk window `fm_scrollpaint` (`FM_ROW_H`) | 16 px | ✅ | ✅ |
| Note Pad find panel (§27.10.2) | 29 / 41 px | ❌ | ❌ |

So the only caller that would have to move is the find panel, and the change is
one character: its height is `NP_FP_ROW*2 + NP_FP_PAD*2 + 1` = **29** (and
`+ NP_FP_ROW` = **41** with the replace row), so **the `+ 1` border line is the
whole of what makes it odd**. 32 and 44 would qualify on both mono adapters.
§27.10.2 notes the shift is deliberately "not a multiple of 8", but that was
about the *blit working at all* at a non-8 shift rather than about its cost, so
nothing there argues for keeping it odd. Even
unaligned, the general path can still halve its arithmetic: walk the
destination with the register step and pay `gfx_rowbase` for the source alone.

**None of this needs `WF_SNAP` in Y, and none of it needs an 8-pixel vertical
drag quantum** — which would have been the visible price, and a dearer one than
the horizontal quantum, since a window steps more noticeably vertically on a
348-row screen than horizontally on a 720-column one.

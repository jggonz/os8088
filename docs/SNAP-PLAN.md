# Byte-boundary text — the windows that do not align their own

**Status: the KERNEL half is BUILT and measured. `WF_SNAP`'s `[vid_mono]` gate
is gone (SPEC.md §11.94, PERFORMANCE.md Set 52) and the flag is INVERTED, so
every window's content origin is on a multiple of 8 unless it opts out
(SPEC.md §11.94.1, Set 53). This document is the remaining half: the ~14
windows whose own text does not land on that boundary, and what each one costs
to fix. Nothing here is started.**

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
| **Fractal** | `FR_X_ZOOM` 130, `FR_X_ZNUM` 170, `FR_X_PAL` 250 | all ≡ 2; `FR_X_PCT` 200 is already 0. Four constants in one table, and the status row is redrawn per progress tick |
| **Paint** | `PT_PAL_X0` 1, plus pens at ≡ 1 and ≡ 2 | 2 of 5 sampled pens aligned. The palette is drawn on every tool change |
| **ArtfulType** | ≡ 3 (measured), and a literal 14 | its own menu bar and status line; the 8 and 16 elsewhere are already right. §46 is a *writer*, so its per-keystroke line draw is exactly the typebench case |
| **HDD Control Panel page** | `HDP_LX` 2 | `HDP_F1X-10` 56 and `HDP_F2X-10` 104 are already aligned |
| **HDD tool window** | `HTW_LX` 4 | the installer's own `HIW_LX` is already 8 |
| **Recorder** | 4 | |
| **Minesweeper** | 4 | also draws at 8 |
| **Piano** | 2 | one sampled pen — low confidence, needs the runtime audit |
| **Arkanoid** | 2 | one sampled pen — same |
| **Task Manager** | five literal `+6` sites | despite `TM_PEN` being 8 and `TM_MPEN` 16. SPEC.md §11.94 records moving `TM_PEN` 6 → 8; these five were missed |
| **Hello** | ≡ 3 for 5 of its 35 glyphs | deliberately the minimal package; fix it anyway, because it is what a new package is copied from |
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

**Two things to fix in the instrument before leaning on it:**

- **A known artifact.** Every window's callback reports a constant **4 glyphs
  in bucket 7** per forced repaint, whatever the app, and it has not been
  chased down. A count under about ten therefore says nothing. This is why
  Piano, Arkanoid and Missile are still marked low-confidence or unresolved.
- **Driving it is the expensive part.** An app has to be made to *draw text*,
  and several draw none until interacted with — an empty Note Pad draws no
  glyphs at all, which is what a first attempt at this survey measured. A
  repaint is forced by dragging the window a whole number of 8px steps and
  back, which a snapped window returns from exactly.

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

## 6. Open question — should the snap apply to Y as well?

`wm_snap` is x-only. The content origin's y is `W_Y + TITLE_H + 1` and nothing
keeps it on a multiple of 8.

**It is not the same trade, and the reason is worth stating before anyone
builds it.** A glyph is 8 rows and `font_char` walks them one at a time through
`gfx_nextrow` whatever y is, so y-alignment buys **nothing per glyph** — there
is no second-byte spill in the vertical direction. Where it could pay is
elsewhere:

- `gfx_scroll` and `gfx_blit4`, whose row arithmetic is per-row anyway but
  whose *bank* arithmetic is not (SPEC.md §39.3's banked layout: a bank holds
  whole rows and a row's base is `rowbase(y)`).
- Note Pad's §27.7.2 blit, which already rounds its y span to whole rows by
  hand — the one place in the tree that has needed it.

So the honest form of the question is not "should windows snap in y" but
**"does any primitive here cost more at an odd y, and if so which"** — a
`gfxbench` row, not a kernel change. Measure before building: an 8px vertical
drag quantum is a much more visible cost than the horizontal one, because
`ui_drag` drags an outline and the eye tracks vertical steps on a 348-row
screen more readily than horizontal ones on a 720-column one.

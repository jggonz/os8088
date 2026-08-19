# Byte-boundary text — the windows that do not align their own

**Status: the KERNEL half is BUILT and measured, and SNAPPING IN Y IS ANSWERED
- NO, see §6: it would gain nothing on any adapter, and the vertical quantum
that does pay is the BANK COUNT applied to a scroll DELTA rather than 8 applied
to a window origin. §6.3 is the real opportunity that investigation found -
`gfx_scroll` recomputing a row address per row, a third of its cost on mono.

**The x half is done:** `WF_SNAP`'s `[vid_mono]` gate is gone (SPEC.md §11.94,
PERFORMANCE.md Set 52) and the flag is INVERTED, so every window's content
origin is on a multiple of 8 unless it opts out (SPEC.md §11.94.1, Set 53).

**AND SO IS THE APPLICATION HALF: every entry in §2 is closed.** Three were
worth doing and the rest were not — §7 is the summary, and it is the part to read
first, because the reason most of them were not is more useful than the three
that were. Sets 52–60.**

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

### 2.1 The Disk window — ✅ DONE (the list view), and the grid is next

| what | was | is | note |
|---|---|---|---|
| header (`Drive B:  7 files`) | `fm_cx + 6` | `+ 8` | truncation `sub ax, 14` → `16` moved with it |
| status line (`Size … Free …`) | `fm_cx + 6` | `+ 8` | the second string drawn from `fm_hdrbuf`; `sub ax, 12` → `14` |
| row icon inset | `fm_cx + 4` | `+ 8` | not a text pen — see below |
| row name | `fm_cx + 24` | `+ 32` | forced by the icon, not by alignment — see below |
| name budget | `(cw-88)/8` | `(cw-96)/8` | keeps the column's right edge at `cw-64` |
| `fm_scrollpaint`'s strip test | `cx + 4` | `+ 8` | reads the inset it is about |
| `FMI_CELL_W` (grid) | 78 | 80 | every `fm_cellx` on a byte; one `equ`, so `fm_layout`/`fm_hit`/the selection rect follow |
| grid icon | `fm_cellx + 31` | `+ 32` | `(80-16)/2` — aligned **and** exactly centred |

**Cost: 0 bytes in every section** — `.text`, `.bss`, `.cold`, `.lowbss`,
`.ovl` all +0, no rung crossed, `KERN_BUDGET` spare unchanged at 3,072. Every
one of those seven constants is an `add`/`sub reg, imm8` at both the old value
and the new.

Its **file rows were already aligned** (`fm_cx + 24`, now 32), which is why the
inversion was worth taking for this window at all: those ~40 `font_str`s
dominate `fm_repaint`. The header and the status line are one string each per
repaint and worth little on their own.

**The icon inset was the interesting one, and this file had its reason wrong.**
The claim here used to be that with an aligned origin the strip is *always* 4px
wide, so §22.11.1's strip pass runs on every scroll. That is backwards:
`fm_bx1 = align_up(fm_cx)` and an aligned `fm_cx` is already a multiple of 8,
so `fm_bx1 == fm_cx` and the strip is **empty** — §11.94.1's default had
already retired the pass, which used to fire on three window positions in eight
(`cx & 7 ∈ {1,2,3}`), not one. What 4 → 8 actually buys is two other things:
`ico_pass` lands each 16-pixel row in a three-byte window at a shift of
`x & 7`, and at shift 0 the third byte is always zero and skipped — two latched
read-modify-writes a row instead of three, on both passes — and it makes the
pass unreachable at *every* window position, `wm_snap_ax`'s one refusal
included. `fm_hit` needed nothing: it divides y by the row height and x by
`FMI_CELL_W`, and references neither the icon inset nor the name pen.

**The name pen is the finding worth carrying to the other items.** Moving the
icon to +8 with the name at +24 puts the 16px icon cell's right edge exactly on
the name's first letter — `ARTFUL.O88`'s A touching the app diamond. Both
numbers are multiples of 8, both are "aligned", the kernel is 0 bytes bigger
and the scroll is byte-identical to a full repaint: **nothing in the
verification recipe of §5 can see it.** Only the picture can. So: for each
remaining item, look at a zoomed crop of what moved and not only at the diff
count.

### 2.2 The apps

| app | off-grid pen(s) | note |
|---|---|---|
| ~~**Tracker**~~ | ~~6, 38, 108, 111, 116, 150, 205, 210, 258, 290~~ | **MEASURED AND CLOSED WITHOUT CHANGING IT** — SPEC.md §45.19. It was ranked the largest item here on this sample and the sample was misleading four ways: its per-frame values go through `tui_rdout`, which **already rounds its pen down to a byte boundary on mono**; `tui_top_cga`'s labels are already at 0/64/136/200/288; what is left is `tui_draw_all`, which is event-driven and not the "redraws continuously while playing" this row claimed (`tui_draw_dyn` draws only what changed); and 159 of the off-grid cells are `tui_s_logo` at 149, which is `112 + (179-104)/2` — the centring of `'T R A C K E R'`, §3's protected kind. Measured: 237 cells 26.2% aligned on Hercules, 354 / 39.3% on VGA, all of it on one repaint |
| ~~**Tamegram**~~ | ~~4, 60, 116, 164, 210~~ | ✅ **DONE** — SPEC.md §49.5.1. The sample was right for once: six of the seven pens were ≡ 4 and `+4` fixed them, 0 bytes, 0% → 100% aligned measured over 17.7 s of play. `LOCK` went 210 → 208 (left by 2, not right by 6, which would have ended row 1 on `TG_HUD_W` exactly). Its flash banner and About panel are centred and stay off-grid |
| ~~**Fractal**~~ | ~~`FR_X_ZOOM` 130, `FR_X_ZNUM` 170, `FR_X_PAL` 250~~ | ✅ **DONE, and it was not an alignment item** — SPEC.md §40.2.1. The pens moved, but what paid was that `fr_status_maybe` called the FULL strip painter ~100 times a render: **2,557 glyph cells → 565, 4.5x**, off-grid 2,222 → 0, ~100 fills gone. The two halves are one change, `font_run`'s single-store path needing the aligned pen |
| ~~**Paint**~~ | ~~`PT_PAL_X0` 1, plus pens at ≡ 1 and ≡ 2~~ | ✅ **NOTHING TO DO** — SPEC.md §42.12. Measured: a repaint is **62 cells, 100% aligned**. `PT_PAL_X0` is not a text pen (the palette is fills and blitted icons, so "drawn on every tool change" costs no glyph), and the size-box field's pen is **geometrically forced**: 8px label + 32px framed field = 40 of the 43px `PT_SEPX` strip |
| **ArtfulType** | a literal 14 | **the "≡ 3 (measured)" here was its own CAPTION** — 10 centred glyphs the kernel drew, now excluded (§11.94.2). Its 8 and 16 are already right. Re-audit before moving anything; §46 is a *writer*, so its per-keystroke line draw is the typebench case |
| **HDD Control Panel page** | `HDP_LX` 2 | **left** — a page draws on open and on a click. `HDP_F1X-10` 56 and `HDP_F2X-10` 104 are already aligned |
| **HDD tool window** | `HTW_LX` 4 | **left**, same reason; the installer's own `HIW_LX` is already 8 |
| **Recorder** | 4 | **left** — two strings in `rc_draw_status`, whose one caller is `rc_draw_all`: repaint-only |
| ~~**Minesweeper**~~ | ~~4~~ | **PROTECTED, not a defect** — `add cx, 4` is an 8px glyph centred in a 16px cell, `(16-8)/2`, and it is the app's densest text: aligning it puts every number on the grid 4px off-centre. Mode text is `MN_BOARD_W/2`. A 2-glyph counter is all that is genuinely off-grid |
| ~~**Piano**~~ | ~~2~~ | **largely not a constant** — `PN_MSG_X` is already 112, and the key letter is `key_x + 5` where `key_x` comes from §11.98.1's run-time scaled keyboard, so no constant can align it. One label at `cx, 2` on a repaint is the row |
| ~~**Arkanoid**~~ | ~~2~~ | **four of its six text sites are `OSAPI_FONT_WIDTH`-centred**, a fifth is a letter centred in a power-up body; the sixth is the score at `cx, 2` |
| ~~**Task Manager**~~ | ~~five literal `+6` sites~~ | ✅ **DONE, and it was FOUR** — SPEC.md §28.5. Three of the five are `add ax, 6` on FRAME RECTS (CPU graph, bar, RAM map) and a rect has no glyph phase. The four real pens — the memory page's XMS line and the heap page's TOTAL/SPLIT/FRAG summaries — are at `TM_PEN` now, 0 bytes; `tm_rowfill` still erases from +6, so only 2px of white margin moves |
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

1. ~~**Disk window list view**~~ — ✅ done, §2.1: header, status line, icon
   inset, name pen, and the strip test that reads the inset. 0 bytes.
2. ~~**Fractal**~~ — ✅ done, SPEC.md §40.2.1, and it turned out **not to be an
   alignment item at all**. The audit put it first of what was left (2,542
   glyphs sampled, 12.5% aligned, 2,222 in bucket 2), and the reason was that
   `fr_status_maybe` ran ~100 times a render and called the FULL strip painter
   each time — a white fill plus five re-lettered fields to move one digit. The
   five pens are multiples of 8 now (8, 128, 168, 200, 248) *and* a progress
   tick draws one opaque padded `font_run`: **2,557 glyph cells → 565, 4.5x**,
   off-grid 2,222 → **0**, ~100 fills a render gone. The two halves are one
   change, because `font_run`'s single-store path needs the aligned pen.
   **The lesson for the rest of this list: ask how often a pen is drawn before
   moving it.** An off-grid glyph that should not be drawn at all is not an
   alignment defect, and aligning it would have banked a fifth of the win.
3. ~~**Tracker**~~ — ✅ **measured and CLOSED without changing it**, SPEC.md
   §45.19. One forced full repaint, histogram filtered to its own record:
   Hercules **237 cells, 26.2% aligned**; VGA **354, 39.3%**. Four things this
   plan's literal-pen sample could not see. Its per-frame values all go through
   `tui_rdout`, which **already rounds its pen down to a byte boundary on mono**
   — so the frequently-drawn text is on `font_run`'s fast path whatever the
   caller's constant says. `tui_top_cga`'s labels are already at 0/64/136/200/288.
   What is left is `tui_draw_all` — event-driven, not per frame — and alignment
   shaves ~3.4% (Herc) / 9.4% (VGA) off a cell rather than removing it, so all
   175 off-grid Hercules cells are worth **~6 ms of a ~237 ms repaint, on an
   event**. And 159 of them are `tui_s_logo` at 149, which is exactly
   `112 + (179-104)/2` — the centring of `'T R A C K E R'` in its box, §4's
   second protected kind. The one thing the measurement does point at —
   `tui_rdout` keeping the erase-and-letter pair on VGA — was costed in §45.19
   as a refactor rather than a constant, and has since been **taken** (SPEC.md
   §45.20): the widening is unconditional and the emit is one path through a
   second entry point, `tui_runo`, so on colour `font_run`'s own fallback does
   the fill the routine used to write by hand. VGA **354 cells / 39.3% aligned
   → 340 / 48.5%**, bucket 4's 40 cells — the status line, and the only
   unaligned windowed readout there — emptied into bucket 0. The package is 54
   bytes *smaller* and no kernel byte moved. Mono is byte-identical by
   construction and was checked that way (0 differing pixels on Hercules and
   CGA); on VGA the status line's pen moves from content x = 4 to x = 0, which
   is where the two mono adapters have always drawn it.
4. ~~**Disk window icon grid**~~ — ✅ done, SPEC.md §22.11.1.2: `FMI_CELL_W`
   78 → 80 and the icon's `fm_cellx + 31` → `+ 32`, which is exactly the centre
   of an 80-wide cell, so it is aligned and centred at once. 0 bytes. The
   **label** is centred and was left alone (§4's second kind): a 9-character
   name is 72px in an 80px cell, so any rounding puts it flush against a side.
5. **ArtfulType** — a writer, so it is the typebench case exactly.
6. ~~**Paint, Tamegram**~~ — ✅ done. **Tamegram** was the cheapest entry in the
   whole survey and is now seven named columns, all multiples of 8, `%error`-
   checked (SPEC.md §49.5.1): six pens were ≡ 4, so `+4` on six numbers, 0 bytes,
   **0% → 100% aligned** measured over 17.7 s of real play. Its worth is small and
   stated as such — ~6 cells/s, so 0.6% of the machine and alignment saves 3.4% of
   that; it is taken because it is free and because leaving the last uniformly-
   fixable entry invites a re-derivation. **Paint needed nothing**: measured, a
   repaint is **62 cells, 100% aligned** (SPEC.md §42.12). `PT_PAL_X0` is not a
   text pen — the palette is fills and blitted icons — and the size-box field's
   pen is *geometrically forced* off-grid: an 8px label plus a 32px framed field
   is 40 of the 43px `PT_SEPX` strip, so the pen sits 2px inside a frame at 8.
7. ~~**Task Manager's five `+6`s, Recorder, Minesweeper, HDD's two pages**~~ and
   ~~**Piano, Arkanoid, Missile**~~ — ✅ walked in one batch, **one change taken**,
   SPEC.md §11.94.4. What the batch found:
   - **Three of the Task Manager's "five `+6`" are `add ax, 6` on FRAME RECTS**
     (CPU graph, bar, RAM map) and a rect has no glyph phase. The four real pens
     — the memory page's XMS line and the heap page's TOTAL/SPLIT/FRAG summaries
     — moved to `TM_PEN`, 0 bytes (§28.5). Nothing but 2px of white margin moves:
     `tm_rowfill` still erases from +6.
   - **Minesweeper's off-grid text is CENTRING and it is the app's densest text**
     — `add cx, 4` is an 8px glyph in a 16px cell, i.e. `(16-8)/2`; aligning it
     puts every number on the grid 4px off-centre. Its mode text is
     `MN_BOARD_W/2`. A 2-glyph counter is all that is genuinely off-grid.
   - **Piano's is largely not a constant.** `PN_MSG_X` is already 112, and the
     key letter is `key_x + 5` where `key_x` comes from §11.98.1's run-time
     scaled keyboard — no constant can align it. One label at `cx, 2` remains.
   - **Four of Arkanoid's six text sites are `OSAPI_FONT_WIDTH`-centred**, a
     fifth is a letter centred in a power-up body; the sixth is the score.
   - **Recorder** is repaint-only (`rc_draw_status` has one caller). **HDD's
     pages** draw on open and on a click. **Missile** computes every pen and its
     strip is already §48.17's differing-span emitter.
8. ~~**Hello**~~ — already struck above: `hl_line` centres every string.

Each item is its own commit, because each is a **visible layout shift** and
wants looking at on a 1bpp adapter (SPEC.md §47's rule about greying applies to
the same class of change: a mono adapter differs from VGA in kind, not just in
depth). Verify the way SPEC.md §11.94.1 was verified — an identical scripted
session against a reference build, framebuffers diffed — and expect the diff to
be non-zero here, because the pixels really do move. What must not change is
the *content*: same strings, same widths, nothing clipped at the right edge
that was not clipped before.

**And LOOK at a zoomed crop, every time.** §2.1's icon shifted onto its own
label — a defect that is 0 bytes, byte-identical under the scroll A/B, and
plainly wrong in a 5x crop. The diff count and the size report can both be
perfect while the window is worse.

---

## 6. Snapping to Y — INVESTIGATED, and the answer is no

**Asked and answered: a `WF_SNAP` in Y would gain nothing, on any adapter.**
The investigation is kept because the reasoning redirects the effort somewhere
that pays, and because "we never checked Y" is otherwise a question that comes
back every time somebody reads §11.94.

**...and it came back once, from a direction this section did not cover, and
the answer held.** SPEC.md §11.96.13.1 found a reason to quantise a *drop's*
`dy` that has nothing to do with the renderer: the drag cache replays a
window's banked pixels at the new position, and a screen-phased dither inside
that window comes back inverted when the delta is odd. Everything below stays
true — there is still no y-alignment win in this renderer, and the vertical
quantum that pays is still `nbanks` applied to a scroll delta — and the drop
snap was shipped and then withdrawn on its own terms: 7px of vertical drop
precision, and a sub-8px nudge that moved nothing, bought a 50% checkerboard in
the other phase inside a scroll-bar track. **`W_X` can afford its 7px because
§11.94 has already put the window on the phase, so `dx` is a multiple of 8 by
construction and the snap moves nothing; there is no `wm_snap_ay` to do that for
`y`, and this section is why there is not.**

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
| Note Pad find panel (§27.10.2) | **32 / 44 px** | ✅ | ✅ |

**✅ DONE — SPEC.md §27.10.3.** The find panel was the only caller that had to
move, and this file said the change was "one character": it is not, and the
reason is worth keeping. `NP_FP_ROW*2 + NP_FP_PAD*2 + 1` is odd for **every**
value of those two constants — `2*anything` is even and the separating rule adds
one — so no edit to either could have fixed it and the correction has to be
explicit: `NP_FP_SLACK = (-NP_FP_RAW) & 3`, rounding UP because down would take
a pixel off something using it. `NP_FP_ROW` must itself be a multiple of 4 or
the Replace panel's `+NP_FP_ROW` undoes it, which is an `%error` now.
The three rows land below the buttons (`np_pbtny` is bottom-anchored), so nothing
is squeezed; 0 bytes; 0 differing pixels against a forced full repaint on CGA and
Hercules. §27.10.2's "not a multiple of 8" note was about the *blit working at
all* at a non-8 shift rather than about its cost, so nothing there argued for
keeping it odd.

**None of this needs `WF_SNAP` in Y, and none of it needs an 8-pixel vertical
drag quantum** — which would have been the visible price, and a dearer one than
the horizontal quantum, since a window steps more noticeably vertically on a
348-row screen than horizontally on a 720-column one.


---

## 7. What the survey turned out to be

Every entry is closed. Three mattered:

| item | what it really was | measured |
|---|---|---|
| **Fractal** (§40.2.1) | a **redraw defect wearing an alignment costume** — the full strip painter called ~100 times a render | 2,557 glyph cells → 565, **4.5x** |
| **Disk window** (§22.11.1.1/.2) | genuinely alignment: the ~40 row strings that dominate `fm_repaint` | 0 bytes, 9 constants |
| **Tamegram** (§49.5.1) | genuinely alignment, and uniformly so — 6 of 7 pens at ≡ 4 | 0% → **100%** aligned |

Everything else was one of four things, and **a literal-pen grep cannot tell any
of them from a defect**:

1. **Centring** — protected (§4). Hello, Minesweeper's cell numbers, Arkanoid's
   four centred sites, Tracker's logo, Paint's About card, Tamegram's banner.
2. **Not a text pen** — three of the Task Manager's "five", which are frame rects.
3. **Derived at run time** — Piano's key letters, after §11.98.1.
4. **A handful of cells on an event** — Recorder, the HDD pages, Piano's label,
   Arkanoid's score, Paint's size box, and Tracker's whole face.

**The two rules the survey earned.** *Ask how often a pen is drawn before moving
it* — Fractal's 78% saving came from not drawing, not from aligning. And *look at
a zoomed crop*, because a 0-byte, byte-identical, fully-aligned change can still
make a window worse (§22.11.1.1's icon landing on its own label).

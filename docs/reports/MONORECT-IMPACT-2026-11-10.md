# KHP_MONORECT: what it would affect, and by how much (2026-11-10)

A MEASUREMENT of one tree, for one decision. The owner asked, of
`KHP_MONORECT` (the 1bpp rect walked a bank at a time, plus mono-5's XOR
column): *"This seems very big in bytes (+344 kern_big, +16 for mono-5). Get
me a full report on everything it would affect, and by how much. Package
users too."*

**The tree is `2cf57256`** (branch `speed-and-fix`, = `elendilon` after kernel
size pass 19's flag day). Every byte figure below was assembled from that
tree on 2026-11-10 with the knob off and on. Every TIME figure is from
`docs/plans/khp1/mono.md` (MartyPC, emulated 4.77 MHz 5150/XT, guest
cycles, `-n 3` medians), taken on `kernel-hotpaths` in November 2026 and
**not re-taken on this tree**. `softgfx.inc`, which holds every line of the
knob, is byte-for-byte unchanged since then, so the routine's own cycle counts
still apply. The desktop scenarios around it have changed (size pass 19
rewrote a third of `wm.inc`), so an operation-level figure is the one
MEASURED THEN. Figures are marked:

* **M**: measured on MartyPC, quoted from mono.md / LOG.md;
* **D**: derived from those measurements by the per-row model in section 2.3,
  which reproduces the measured points to within 1%;
* **P**: predicted from the code's instruction counts and not measured
  anywhere.

## 0. The answer in one screen

* **Bytes: +360 resident on kern_big** (.text +354, .bss +6), measured.
  mono-1 is 338 + 6 = **344**; mono-5 is **16**. The same +360 lands on
  **kern_emu** and on **kern_mega's conventional memory**, because the knob
  rides `KERN_BIG`, which both of those also define. **kern_small: 0**. Its
  kernel assembles byte-identical with the knob on, because `SW_BANKWALK` is
  `KERN_BIG`-only.
* **Who gains: only a 1bpp desktop.** That means CGA and Hercules (the
  owner's 5150 #1 boots on its Hercules GB101) and any card the kernel drives
  at 1bpp (`[vid_mono]`). On VGA/EGA planar, VESA, kern_small and FSX
  foreign-mode games, nothing changes.
* **What it buys (M, mono-1 alone on its own base):** a window raise
  **-8.7 ms CGA / -15.1 ms Hercules**, a menu open **-5.3 / -4.8 ms**, a
  Disk-window PgDn **-8.5 / -4.1 ms**, and a drag **-6.0 / -21.3 ms per
  outline pass**. mono-5 adds **-8.8 ms** of a CGA drag. A large fill halves:
  `GFX_FILL 64x64` **-51%**, about **-3.2 ms a call**.
* **What it costs in time:** short rects get slower. `GFX_FILL 8x8` is
  **+2.4%** (+15 µs) and `GFX_VLINE 8px` is **+2.1%** (M). By the model, the
  same ~+10-15 µs applies to every 2-8-row rect and every 9-24-row one-byte
  column (P). Nothing a user can see moves by that.
* **Pixels: none change** (section 4). The ORDER rows land in within one call
  does change.
* **It is splittable.** 213 + 6 bytes are shared machinery. The rest is six
  row bodies of 10-37 bytes each. **Wide solid/grey fills only is +224**, and
  **wide fills plus XOR fills is +270** (both assembled here, section 1.3).
* **Recommendation (section 5): take the whole knob on kern_big ALONE**, and
  gate it out of kern_emu and kern_mega, where it buys nothing their machines
  use. That is +360 on the one kernel the owner's 1bpp machines run, and 0 on
  the other three. The subsets are a fallback if the bytes have to come down.
  Each subset gives up the drag and outline gains, which are the Hercules
  machine's largest win.

## 1. What it replaces and adds

### 1.1 The change

`kernel/kernel.asm`'s KHP block defines `KHP_MONORECT` for no kernel. With
it on, kern_big (and anything defining `KERN_BIG`) gets `SW_BANKWALK`, and
`kernel/softgfx.inc` replaces the 1bpp rect's three column-major passes with
a row-major walk. The three old passes are `sw_col` on the left edge, `sw_col`
on the right edge, then the interior rows, each pass stepping rows with
`gfx_nextrow`'s add/test/wrap and toggling the dither on every row.

On a banked framebuffer (B = 2 on CGA, 4 on Hercules), the new code walks a
rect as B *phases*: every row of bank j, top to bottom. Inside a phase the row
step is one `add di, si`, the dither pattern is constant, and each row is
written in ONE pass: masked left byte, whole bytes by `rep stosw`, masked right
byte. A whole-byte edge folds into the `rep stosw`. A one-row rect (every
hline, every pixel) jumps straight into its row body. A short rect keeps the
old body. The entry tests are:

| rect | path with the knob |
|---|---|
| one row (any width) | the row body directly, no phases |
| 2-8 rows | the OLD column-major body (`sw_plane_cm`), after a 5-instruction question |
| 9+ rows, at least two bytes wide | the bank walk |
| one byte column, 9-24 rows | the OLD body, after a longer question |
| one byte column, 25+ rows | the bank walk |

`sw_col` (90 bytes, with its whole-column store `.frow`) and the old body
(120 bytes, now labelled `sw_plane_cm`) both STAY. The knob adds the new path
beside them and takes nothing out.

### 1.2 Bytes, per routine and per kernel (measured at `2cf57256`)

Each kernel was assembled with the Makefile's own nasm line, knob off and on,
and read with `tools/kernsize.py --json` and the map.

| kernel | .text | .bss | **resident delta** | footprint (`KERN_SIZE`) off -> on |
|---|---:|---:|---:|---|
| kern_big | +354 | +6 | **+360** | 95,232 -> 95,232 (no rung crossed; per CLAUDE.md the rung is not the price) |
| kern_emu | +354 | +6 | **+360** | 95,232 -> 95,744 |
| kern_mega | +354 | +6 | **+360 conventional** (all `.text`, none `.mega`) | resident after the switch 61,440 -> 61,952 |
| kern_small | 0 | 0 | **0**: `kernel-full.bin` is byte-identical with the knob on | 57,856 -> 57,856 |

kern_emu and kern_mega were assembled against `build/`'s generated includes
rather than `build/emuk/` and `build/megak/`. The deltas are the knob's own
lines, which do not depend on those includes.

The +354 bytes of `.text`, by routine (kern_big map, knob on):

| routine | bytes | what it is | part |
|---|---:|---|---|
| `sw_plane_op` (new entry) | 15 | the rows question | shared |
| `sw_po_big` | 17 | 9+ rows: wide or a column of 25+? | shared |
| `sw_po_sel` | 121 | picks the row body, sets up registers and phase geometry (1 byte is the `t_hotpar` parity pad) | shared |
| `sw_walk` | 60 | runs the body once per bank phase | shared |
| `sw_k_i` | 18 | row body: whole bytes only (aligned fills) | wide fills |
| `sw_k_lir` | 37 | row body: masked left, whole bytes, masked right | wide fills |
| `sw_k_col` | 17 | row body: one masked byte column (vlines, shadows) | columns |
| `sw_k_colf` | 10 | row body: one whole byte column | columns |
| `sw_k_xrow` | 33 | row body: XOR row, interior complemented a word at a time | XOR fills |
| `sw_k_xcol` | 26 | row body: XOR column, two rows a `loop` (10 without mono-5) | XOR columns |
| **total** | **354** | | |
| `.bss`: `sw_n0`, `sw_rp`, `sw_kern` | 6 | the phase geometry and the body pointer | shared |

So the knob is **219 bytes of machinery and 141 of row bodies**: wide fills
55, columns 27, XOR fills 33, XOR columns 26.

### 1.3 Subsets, assembled (kern_big, `2cf57256`)

These variants were spliced into scratch copies of `kernel/` and assembled.
**They were not proved pixel-equal and not measured**: they are prices.

| variant | resident | keeps | gives up (falls back to the old body) |
|---|---:|---|---|
| whole knob | **+360** | everything | - |
| without mono-5 | +344 | everything, XOR column one row a loop | ~-8.8 ms of a CGA drag (M) |
| **B: wide fills + XOR fills** | **+270** | solid/grey rects at least two bytes wide (9+ rows, or one row), XOR fills likewise | every one-byte column (vlines, window shadows, frame sides), every XOR rect's sides (drag and grow outlines, Paint's marquee, Solitaire's hand) |
| **A: wide solid/grey fills only** | **+224** | solid/grey rects at least two bytes wide | B's losses, plus every XOR fill (menu highlights, selection bands) |

Could a smaller subset buy most of it? **For raise and menu open, probably
yes (P)**: their gains are the title bar, the menu panel, the content ground
and scroll-bar troughs, which are all wide fills. **For drag, no (P)**: a drag
pass is the XOR outline's two tall one-byte sides plus two one-row XOR rows.
Subset A keeps the rows' direct path but loses the sides. The drag is where
Hercules gained most: 94.3 -> 73.0 ms a pass, against CGA's 75.0 -> 69.0
(M), because its four phases share one setup that the old body paid a row at
a time. How the measured scenario figures split between the parts was not
measured. Before any subset is taken it must be re-proved
(`docs/plans/khp1/patches/tools/equivalence/mono_uni.py`) and re-measured.

## 2. Every kernel path that reaches it, and what each gains

### 2.1 The entry points

`sw_rect` (via `sw_fill`, `sw_fill_gray`, `sw_xor_fill`) is the 1bpp body
behind these kernel primitives, all in `kernel/vga12.inc`:

| primitive | reaches `sw_rect` as | shape it hands down |
|---|---|---|
| `gfx_fill` / `OSAPI_GFX_FILL` | `gfx_fill_raw` -> `VXMONO sw_fill` | the rect, once per clip rect |
| `gfx_fill_gray` / `OSAPI_GFX_FILL_GRAY` | `VIDEXTM VX_GRAY, sw_fill_gray` | the rect (50% dither) |
| `gfx_xor_fill` / `OSAPI_GFX_XOR_FILL`, `vga_xor_fill_vram` | `sw_xor_fill` | the rect |
| `gfx_hline` / `OSAPI_GFX_HLINE` | a one-row `gfx_fill` | one row: the direct path |
| `gfx_vline` / `OSAPI_GFX_VLINE` | a one-column `gfx_fill` | a one-byte column |
| `gfx_pixel` | a 1x1 `gfx_fill` | one row, one byte |
| `gfx_frame` / `OSAPI_GFX_FRAME` | four strips through `gfx_fill_raw` | two one-row strips, two one-byte columns |
| `gfx_xor_rect` / `OSAPI_GFX_XOR_RECT`, `vga_xor_rect_vram` | four strips through `gfx_xor_fill_raw` | the same, XOR |

Not reached: `gfx_fill_pat` (`sw_fill_pat`, the `KHP_MONOPAT` knob), save and
restore (`sw_xfer`, mono-2, already shipped), `gfx_scroll` (`KHP_MONOSCROLL`),
`gfx_spans`, `gfx_points`, the blits (`sw_blit_row`), and text (glyphs are
drawn by `font.inc` directly; only `font_run_x`'s gap fill and
`font_run_scell`'s cell erase are `gfx_fill`s).

### 2.2 The kernel's callers

The direct call sites at `2cf57256`, by routine. A `ct_cw_*` or `dkk_*`
thunk is a module or `.cold` caller, which is the same primitive.

| area | routines (calls) | shapes, and the knob's effect on them |
|---|---|---|
| windows (`wm.inc`) | `wm_draw_win` (fill x2, frame, hline x2, vline), `wm_draw_title` (fill x2, hline x3), `wm_title_band` (fill x2), `wm_tbox` (fill, frame), `wm_draw_shadow` (hline, vline), `wm_grow_paint`/`_erase` (fill, frame x2), `wm_tsend` (hline), `wm_chrome_relit_x` (xor fill), `wm_dmg_gray` (`gfx_fill_gray` per damage band, via `thm_desk`) | content ground and title band (wide, 9+ rows: walk); title stripes (one row: direct); shadow and frame sides (one-byte columns: walk from 25 rows); the desktop dither in damage rects (wide grey: walk) |
| drag and grow (`ui.inc`, `wm.inc`, `files.inc`) | `ui_drag_xor`, the grow outline and the file-drag outline, all `vga_xor_rect_vram` | XOR outline: two one-row XOR rows (direct `sw_k_xrow`) and two tall XOR columns (`sw_k_xcol`, mono-5), redrawn on every pointer step |
| menus (`menu.inc`) | `menu_drop` (fill, frame), `menu_draw_bar` (fill x2, hline x4), `menu_bemit` (fill), `menu_item_xor` (`vga_xor_fill_vram`), `menu_title_xor` (xor fill) | the panel ground (wide: walk); the item highlight, `MENU_ITEM_H` = 16 rows of XOR (walk); the bar's rules (one row) |
| Disk windows (`files.inc`) | `fm_sel_bar` (xor fill x2), `fm_wfill`, `fm_more_mark` (fill), `fm_draw_core` (frame x2) | selection band, `FM_ROW_H` = 16 rows of XOR (walk), twice a selection change |
| dock (`dock.inc`, `dockmod.inc`) | `db_paint` (fill, hline, xor fill, xor rect), `db_draw_tile` (frame x2, xor fill), `dkx_xor_ring` | the 24-row strip (`DOCK_H`), tile frames, focus marks by XOR |
| desk layer (`desk.inc`) | `desk_draw_zone` (fill), `desk_zone_hilite`, `desk_col` (xor fill) | zone grounds and highlights |
| progress widget (`fprog.inc`) | `fpg_paint` (fill, frame), `fpg_ring` (fill x3), `fpg_clear`, `fpg_lit` (fill), `fpg_hatch` (`gfx_fill_gray`) | small bars and rings, mostly under 9 rows: old body, +~15 µs each (P) |
| text (`font.inc`) | `font_run_x`'s gap fill, `font_run_scell`'s cell erase | glyph-height fills (8 rows on every adapter): old body, the +2% case |
| others | `ui_lit_go` (xor fill), `sd_paint` (stack panel), plus the controls in CTRL.DRV (`ctrl.inc`) and EXTD.DRV (`extmod.inc`) | as their shapes |

### 2.3 The cost model, and the operations

From mono.md's `GFX_FILL` 8x8 / 64x64 / 256x128 / 256x1 points, CGA, the old
body costs about **2,800 + R x (326 + 12.9 W)** cycles for an R-row,
W-byte-wide fill, and the walk saves about **240 cycles a row** (D). The
model gives 30,260 and 97,400 cycles for the two big fills against the
measured 30,250 and 97,270. The saving per row is roughly constant because
the knob takes out per-ROW overhead (three passes and their row steps
become one), not per-byte work: `rep stosw` costs the same either way. The
walk then pays about 250 cycles a PHASE: ~500 on CGA, ~1,000 on Hercules.

| shape (one call) | CGA, before -> after | Hercules | source |
|---|---|---|---|
| `GFX_FILL 64x64` (aligned) | 6.34 -> 3.12 ms (-51.0%) | -47.7% | M |
| `GFX_FILL_GRAY 64x64` | -51.0% | -47.6% | M |
| `GFX_FILL 256x128` | 20.4 -> ~13.8 ms (-32.2%) | -30.5% | M (%), D (ms) |
| `GFX_XOR_FILL 64x64` | 9.0 -> ~6.0 ms (-33.6%) | -31.5% | M (%), D (ms) |
| `GFX_XOR_RECT 256x128` (an outline) | 8.0 -> ~5.6 ms (-30.8%); mono-5's share 6.12 -> 5.58 | -26.4% | M |
| `GFX_VLINE 128px` | 4.47 -> ~3.44 ms (-23.1%) | -18.8% | M |
| `GFX_FILL 256x1` / `GFX_HLINE 256px` | -18.8% / -18.2%, about -140 µs | | M |
| `GFX_FRAME 64x64` | -17.7%, about -1.1 ms | -12.7% | M |
| `WM_TITLE strip` | -13.0% | -10.4% | M |
| `GFX_HLINE 8px` | -1.7% | | M |
| `GFX_PIXEL` | 0.0% | | M |
| **`GFX_VLINE 8px`** | **+2.1%** (+16 µs) | | M |
| **`GFX_FILL 8x8`** | **+2.4%** (+15 µs) | | M |
| any 2-8-row rect; a one-byte column of 9-24 rows | +~10-15 µs a call | same | P |
| a 16-row XOR band (menu item, Disk-window row) | about -0.6 to -0.8 ms a band, so -1.2 to -1.6 ms a highlight move (off and on) | similar | D |
| a 16-pixel-wide scroll-bar trough, 100-150 rows (`gfx_fill_gray`, two whole bytes, no interior) | about -2.5 ms (CGA, 100 rows) to -3.9 ms (Hercules, 150 rows) | | P: about 130 cycles a row, less the phases |
| a full-width content clear, R rows | about -0.05 ms a row: -7.5 ms at 150 rows, about 18% of that fill | similar | D |

The operations, mono-1 alone, M (`os88hot ab`, mono.md section 3):

| operation | CGA | Hercules |
|---|---|---|
| raise a Disk window | 179.1 -> 170.4 ms (**-8.7**) | 229.3 -> 214.2 (**-15.1**) |
| Disk window PgDn | 131.2 -> 122.7 (**-8.5**) | 131.8 -> 127.7 (**-4.1**) |
| menu open (File) | 46.2 -> 40.9 (**-5.3**) | 47.6 -> 42.8 (**-4.8**) |
| drag, busy per outline pass | 75.0 -> 69.0 (**-6.0**) | 94.3 -> 73.0 (**-21.3**) |
| drag, mono-5 alone (`sw_k_xcol` busy a drag) | 49.0 -> 40.2 (**-8.8**) | not separated |
| 86Box AT/286 raise (whole mono branch, relative only) | busy 23 -> 20 ms | |

A drag at 7 passes is therefore about **-42 ms CGA (-6 a pass) and up to about
-150 ms Hercules (-21 a pass)** of CPU across the gesture (D). A drag waits on
the serial mouse, so busy time is the measure there, not wall time.

**What is slower:** every 2-8-row rect and every 9-24-row one-byte column pays
the question. The kernel's frequent ones are glyph-cell erases
(`font_run_scell`, 8 rows), the progress widget's small parts and short frame
sides. +15 µs is about 0.2% of a 7 ms text-row redraw (P); no desktop scenario
in mono.md moved the wrong way.

## 3. Packages

### 3.1 What reaches it

A package reaches the knob through the slots `OSAPI_GFX_FILL`, `_FILL_GRAY`,
`_XOR_FILL`, `_XOR_RECT`, `_FRAME`, `_HLINE` and `_VLINE`, and in C through
`os88_gfx_fill`, `_fill_gray`, `_xor_fill`, `_xor_rect`, `_frame`, `_hline`
and `_vline` (`apps/cc/os88thunk.asm`). The shared `apps/os88ui.inc` controls
use them too: buttons, check boxes, the scroll bar's trough (`FILL_GRAY`) and
selection bands (`XOR_FILL`). **73 of the 76 package directories under
`apps/` reach it**: all but `imgtest`, `m6502` and `rlapply`. Every package
also gains indirectly, because the kernel draws its frame, title bar, content
ground, menus, dock and drag outline (section 2).

Not affected:

* **FSX foreign-mode games** draw their own VRAM inside an `OSAPI_FSX_RUN`
  bracket: WireBall, WireFleet, PIXELSTEIN's fullscreen bracket, isoTron's
  fullscreen, Tracker's text-mode fullscreen. A same-mode bracket (Missile
  Command's) still draws through the kernel and IS affected.
* Pictures drawn with `OSAPI_GFX_BLIT1/BLIT4/BLITP`, `GFX_POINTS`,
  `GFX_FILL_PAT` or `GFX_SCROLL`: Paint's canvas, Pixel, Arkanoid's field, card
  faces in Solitaire. (Gorillas draws its buildings with blits but its empty
  sky with `GFX_FILL`, so the sky fills gain.)
* Anything on VGA/EGA/VESA, and anything on kern_small.

### 3.2 Ranked by what they would gain (1bpp, kern_big)

Per-package figures are D/P from section 2.3's per-shape costs and the rect
sizes in each package's source. No package was timed.

| rank | package(s) | what they draw through it | estimate |
|---|---|---|---|
| 1 | **Scribe, Word, Sheet, ArtfulType, TexPad, Note Pad, CWORD** (93, 70, 59, 33, 25 and 18 call sites; CWORD in C) | content clears and repaint bands (wide, often 9+ rows), selection highlights as XOR bands repainted while the mouse drags a selection, scroll-bar troughs, ruler and frame lines | the largest steady benefit: about 0.05 ms a row of every wide fill and about 0.7 ms a 16-row XOR band (D). A full-width clear of a 150-row content area is about -7.5 ms; dragging a selection over text lines is about -1 ms a line crossed (P) |
| 2 | **Task Manager** | graph columns coalesced into `gfx_fill` (narrow, up to 40 rows), memory bars by `FILL_GRAY`, hover rows by XOR (`TM_ROW_H` 11 rows) | graph repaint: a few ms (P); hover move about -1 ms (P) |
| 3 | **every package with an `os88ui` scroll bar or list** (Disk windows, the file dialog, Control Panel pages, Browser, Wire Chat, MIDIRack and the rest) | the scroll bar's grey trough (two whole bytes wide, tall: walks), list selection bands | about -2.5 to -4 ms a whole-trough repaint (P), and less for a thumb move, which repaints part of the trough (the Disk window's PgDn measured -8.5 ms CGA, M) |
| 4 | **Paint** | rubber band and marquee as XOR rects on every pointer step, filled-rect tool, tool palette | marquee: -15 to -30% per outline step at sizes over 25 rows, if the column and XOR parts are taken (M at 256x128: -2.5 ms an outline); the canvas itself is BLITP and unaffected |
| 5 | **Solitaire** | a dragged hand as `XOR_RECT` plus one-row XOR rules on every pointer step; table felt fills | about -0.5 to -1.5 ms per drag step depending on hand height (P, from the 256x128 outline); faces are BLIT4 and unaffected |
| 6 | **C ports' screens: C64, Apple II, RunCPM, Lemmings, PACCMAN, Weave/Loom** | border and status fills, one content-box clear on a repaint (Lemmings), PACCMAN's maze-area clears | ms per repaint where the fill is wide and tall (P); per-character erases are glyph-height, the old body |
| 7 | **window faces drawn once** (Calc, MIDIRack, Audio, Syscheck, Tracker's GUI, Recorder, CDRack and the other face packages) | button and panel grounds | a few ms per full face paint (P); per-key flashes are small rects |
| 8 | **small-sprite windowed games**: Bounce (8x8 square), Missile Command (discs of five small rects, crosshair XOR fills), Mines (cells), Fractal (1-4-row bands per run), VBattle, Cyclone | mostly rects under 9 rows | **neutral to slightly slower**: +~15 µs per 2-8-row fill (P), around +2% of each such fill; a one-row wide run gains about 140 µs |
| - | FSX games, blit-drawn pictures | | unaffected |

## 4. Behaviour and pixels: no change

The knob changes no pixel and no result. The evidence, all from the pass
that built it (mono.md section 3, `docs/plans/khp1/patches/tools/README.md`):

* **`mono_uni.py`** (`docs/plans/khp1/patches/tools/equivalence/`) loads
  both kernels under unicorn with CGA and Hercules geometry, starts both
  from the same random framebuffer, and calls `sw_fill`, `sw_fill_gray`,
  `sw_xor_fill` (and the other mono items' routines) with random clipped,
  one-row, one-column, single-pixel, aligned and tall rects. It compares the
  framebuffer, the caller's buffer and every register each contract keeps.
  **11,996 cases, 0 differ.** Its negative control works: dropping the
  per-phase dither toggle fails 1,257 of 4,000 cases.
* **`swcolsame`** (soak row) A/Bs the bank walk against `NOCOLFAST=1`'s
  column-major body on MartyPC, CGA and Hercules: **0 differing pixels**.
* Each masked byte is still one store, `dest ^= (pat ^ dest) & mask`
  (SPEC.md 39.25.1), so an interrupt cannot rewrite bits outside the mask.

The one difference is **the order rows are written within one call**: on
CGA a rect lands even rows first, then odd rows; on Hercules it lands in four
interleaves. No pixel is written twice, so `m.flicker`'s transient count
cannot rise. `deskflash` is the row that watches this. A slow rect seen
mid-draw looks interlaced rather than top-down. At 3-15 ms a rect that is
under one frame at 60 Hz.

None of this was re-run on `2cf57256`. `mono_uni.py` has not been run since
the pass ("expect to adapt one before trusting it"). `khpknobs`
(full tier) assembles the arm on every run, so it has not rotted. Taking
the knob means re-running these rows: `swcolsame`, `deskflash`,
`win1bpp`, `wdmenusu`, `paintsu`, `blitpair`, `khpknobs` and `hotpar`, plus
`deskbench` on CGA and Hercules as the measurement, and `mono_uni.py` once.

## 5. Recommendation

| choice | bytes (kern_big) | buys (1bpp only) |
|---|---:|---|
| leave it off | 0 | nothing; the shipped column-major body stays |
| A: wide solid/grey fills | +224 | most of the raise, menu-open and scroll-bar gains (P); none of the drag |
| B: A plus XOR fills | +270 | adds menu and selection highlights, about 1.2-1.6 ms a highlight move (D) |
| **the whole knob** | **+360** | raise -9 / -15 ms, menu open -5 ms, PgDn -8.5 / -4 ms, drag -6 / -21 ms a pass, plus mono-5's -8.8 ms a CGA drag (M; CGA / Hercules) |

**Take the whole knob, on kern_big only.** The reasons:

* **The owner's own machine is a 1bpp machine.** 5150 #1 boots on its
  Hercules (docs/FIELD-MACHINES.md), and Hercules shows the largest single
  gain (-21 ms a drag pass).
* **The subsets save 90-136 bytes but give up exactly the gestures where
  Hercules gains most** (outlines), and each needs its own equivalence proof
  and measurement. The whole knob has both already.
* **The 360 bytes buy nothing on kern_emu** (browser and hypervisor
  machines, VGA) **or kern_mega** (286+ VGA-class machines). Gate them out
  with `%ifndef KERN_EMU` / `%ifndef KERN_MEGA` around the define; at the
  moment, defining the knob for kern_big would carry it into both.

If 360 bytes is too many for kern_big's remaining spare, **B at +270** is the
fallback, with a measured A/B first. Leaving the knob off costs nothing on any
machine but a 1bpp one.

## 6. How these figures were taken

```sh
make                                     # tree 2cf57256, into this worktree's build/
nasm ... -DKERN_BIG [-DKHP_MONORECT] --before "[map all k.map]" kernel/kernel.asm
python3 tools/kernsize.py --json -DKERN_BIG [-DKHP_MONORECT]     # and -DKERN_EMU, -DKERN_MEGA
python3 tools/kernsize.py --json -DKERN_SMALL [-DKHP_MONORECT] --build build/smallk
```

The routine spans are the distance between adjacent labels in the kern_big
map. The subsets were edited copies of `kernel/softgfx.inc` assembled the same
way. Package call sites were counted with `grep` over `apps/` (asm, inc and C,
host tests excluded).

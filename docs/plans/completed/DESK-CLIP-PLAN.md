# DESK-CLIP - THE DESKTOP'S CELLS DRAWN ONLY WHERE THEY SHOW

**STATUS: BUILT - SPEC.md §11.3.5 and §11.91.6 are the contract and this file
is the design record behind them.** The owner chose section 5.1's answer for
both kernels: option A + C2 on `kern_big`, option B + C2's promotion
one-shot on `kern_small`. Everything from section 1 on is the COSTING as it
was written, against throwaway prototypes in private trees on
`claude/jolly-mayer-sxly49` at `db44c20`, 2026-10-01; section 0 is what the
production build measured. The instrument is `tools/deskclip.py`, and
`tests/deskclip.py` (`deskclip`, `deskclipsmall`) is the gate.

## 0. What was built, measured

**Bytes, resident, `tools/kernsize.py --json` against the tree before it**
(no rung crossed on either kernel):

| build | built | the plan said |
|---|---|---|
| `kern_big` (A + C2) | **+322**: `.text` +233, `.bss` +4, `.cold` +85 | 484 prototype, ~400 ESTIMATED |
| `kern_small` (B + one-shot) | **+58**: `.text` +36, `.cold` +22 | 92 prototype |

By symbol span: `ico_clip` 198 (prototype 268), `wm_zone_r` 24 (44),
`desk_zone_clip` 72 (121). Where the bytes went: `wm_zone_r` clobbers rather
than preserves and leaves through `wm_clip_clear`, which writes no flag;
`desk_zone_clip` copies each fragment over slot 0 instead of swapping it
there and back (slot 0 is drawn first, so nothing is lost); the walk is
handed its draw routine in DX instead of testing a flag; the overflow path
is the only one that grows `[wm_dmg_zb]`, so `desk_dmg_zones` lost its call
on `kern_big`; `ico_clip` clobbers what `ico_core` resets anyway and folds
the "anything cut" test into an AND of the masks; and the promotion one-shot
is one `shr` and a `jc`. `kern_small` asks `wm_zone_r` as a QUESTION at
`desk_dmg_zones`, so it needs none of the fragment machinery, and takes only
the one-shot of C2: without the clip, its windows over a partly visible cell
are marked by the zones' box whatever the vacated rect says, so the empty
rect would change nothing there.

**Gestures, one reveal pass, guest ms on a 4.77 MHz 8088** (section 2.2's
layouts; the Task Manager rows carry its worker's +-20 ms):

| gesture | Hercules before -> after | VGA before -> after |
|---|---|---|
| drag over the cell column, a window parked over a cell | 233.3 -> **151.1** | 195.1 -> **139.2** |
| ...the parked window the Task Manager | 779.7 / 761.3 -> **444.5** | 630.7 / 633.7 -> **332.2** |
| close a window whose frame reaches a cell another covers | 156.9 -> **115.6** | 120.5 -> **86.8** |
| a cell repainted in place under a window | 131.0 -> **72.7** | 238.3 -> **59.3** |
| plain desktop, no cell (the control) | 202.1 -> 202.2 | 221.0 -> 221.0 |

`kern_small`, Hercules: drag **214.9 -> 118.6**, in-place repaint **118.3 ->
83.2**, close 146.8 -> 147.2. Every row read 0 differing pixels against a
whole repaint except `drag` on `kern_big`, whose 32 are the reference's own
(section 6.2).

**The gate goes red for each part broken**, measured on the production
build: `wm_zone_r` without its frame subtraction - 495 pixels (`cell`, both
adapters) and 313 (`close`, Hercules); `ico_clip`'s column masks forced to
0FFFFh - 32 pixels (`cell`, both) and 27 (`close`, Hercules); C2's stores
taken out - the window over the cell redrawn on both, and on Hercules a
title promoted; `kern_small`'s skip taken out - both covered cells drawn
and the parked window redrawn. `drag` is green under the first two breaks
on both adapters, and correctly: the part of the parked window the cell
would wrongly cover inside the damage is under the mover, which is drawn
after it - so `cell` is the row's sharp edge and `drag` is its coverage.

**The owner's ask:** *"the cells are not clipping to only visible areas, thus
damaging windows they do not need to. That can and will cause repaint
cascades, just like when that bug existed on the original desktop. Lets cost
fixing that, we should have draw calls that know which parts need drawn and
which do not (thus avoiding damaging windows that don't need damaged)."*

**The answer, in one table, as COSTED** (section 5 has the derivation). Option A+C2 is a
prototype measured at **+484 resident bytes on `kern_big`** (no rung crossed;
per CLAUDE.md's banner that is not the point). `kern_small` gets a different
answer, because its cells are a different shape (section 4.1): option B plus
C2's promotion fix, **+92 bytes measured**, buys its drag row -94 ms and its
in-place cell repaint -35 ms, and whether the floor machine spends 92 bytes
at all is the owner's call (section 5.1).

`kern_big`:

| gesture, one reveal pass | Hercules today | Hercules A+C2 | VGA today | VGA A+C2 |
|---|---|---|---|---|
| drag a window over the cell column, a window parked over a cell | 233.3 ms | **152.2** | 195.1 | **140.3** |
| ...the parked window being the Task Manager | 779.7 | **446.1** | 630.7 | **333.0** |
| close a window whose frame reaches a cell another window sits over | 156.9 | **117.1** | 120.5 | **85.9** |
| a cell repainted in place (mount, medium change) under a window | 131.0 | **73.5** | 238.3 | **60.0** |
| the dither-only control, no cell touched | 202.1 | 202.2 | 221.0 | 221.0 |

---

## 1. What a cell does today, and which half of the claim is true

The brief made five claims about `wm_paint_dmg` (kernel/wm.inc, SPEC.md
§11.90/§11.91). Four are right. **The first is wrong, and the correction is
what scopes this plan.**

| claim | verdict |
|---|---|
| (1) the desktop dither is clipped to the damage rect but NOT to windows | **FALSE.** `wm_dmg_gray` seeds the region with each display's band of the damage and `wm_dmg_occl` subtracts every visible window's FRAME (and an open hidden dock, SPEC.md §30.6.1) before one clipped `gfx_fill_gray` - SPEC.md §11.91.1, since long before this branch. The `plain` control in section 2 shows it: no window is repainted that the move did not uncover |
| (2) every cell the rect touches is drawn WHOLE and unclipped | **TRUE.** `desk_paint_mask_x` calls `desk_draw_zone` with no region armed - `wm_dmg_gray` ends in `wm_clip_clear` - so the cell's ground, picture, caption box, caption text and selection XOR are written over its whole rect, inside the damage or not, under a window or not |
| (3) the dock and the bar | true, and untouched here |
| (4) the windows, back to front, transitively marked | true |
| fca7231: a drawn cell marks only windows over ITS box (`[wm_dmg_zb]`) | true; `wm_dmg_wins` asks `[wm_dmg_zb]` per window at `.mnodmg` and `wm_su_owed` widens that window's owed rect by it |

So the cascade the owner remembers from the "original desktop" is already
fixed on the DITHER side, and the cells are the last instance of it: a cell
is drawn over every window lying on it, so every such window is owed a
repaint, so every window above THAT one is marked transitively (SPEC.md
§11.91.3's rule), which is the VGA `cell` row's 164.5 ms W_PAINT below.

What the clip machinery offers today (SPEC.md §11.3): `wm_clip_rect` builds a
bare rect's visible region with every window's OCCUPIED box (shadow included)
as an occluder, 16 rects, CF = 1 on overflow and ZF = 1 when wholly covered;
the fills clip per pixel; `font_char` clips whole ROWS (§11.3.2) and on
`kern_big` whole COLUMNS too (§11.3.4), against the ONE winning fragment;
`gfx_blit1` on `kern_big` walks every fragment (§5.4.2.7); **`icon_draw16`
and `ico_core` clip whole-icon only** - drawn iff the icon lies wholly inside
one fragment, otherwise skipped - and `gfx_blit4`/`gfx_scroll` are unhooked.
Rule 3 of §11.3 says the repaint path stays unclipped and relies on the
painter's algorithm; the desktop is the one layer that algorithm cannot
excuse, because nothing is drawn after a window that would put the window
back except the window itself.

## 2. Measured baseline

### 2.1 The instrument

`tools/deskclip.py <scenario> [--machine M] [--parked tm] [--detail]`. It
sets the desktop up with `os88ui` verbs and nothing armed, then sends ONLY
the packet that starts the repaint (the release; a `[desk_cdirty]` post for
`cell`) with `bp_trace` armed on the phase boundaries of `wm_paint_dmg`, on
every primitive's PUBLIC entry and on `wm_draw_win` (whose BX names the
window and whose `[wm_su_son]`/`[wm_su_sx1..sy2]` say what it is owed). Every
number is guest cycles on a cycle-accurate 4.77 MHz 8088, divided by
4,772,727 - so it is the 5150's milliseconds, not the host's. A "call" is an
entry to `gfx_fill`, `gfx_fill_gray`, `gfx_fill_pat`, `gfx_xor_fill`,
`font_run_x`, `font_char`, `ico_core`, `gfx_restore`, `gfx_blit1` or
`gfx_blit4` - so a `gfx_hline` or `gfx_frame` counts as the fills it
funnels into. A window is "cache" when it was put back from its raise cache
(SPEC.md §11.96) and "W_PAINT" when its package redrew it.

Then it **verifies**: the screen the pass left against a forced whole
repaint (`[cp_dirty]` -> `wm_paint_all`, tests/zonedmg.py's reference), the
Task Manager and the menu bar's clock masked out because both change by
themselves. `os8088_5150_herc_gla` is read out of the 1bpp framebuffer;
`os8088_xt_vga` out of the rendered card. Repeat runs of one scenario agree
to within 0.1 ms - except where the Task Manager is open, whose worker runs
inside the span while the UI task holds the lock (its ground phase read 55.5
and 74.9 ms on two identical runs); those rows are quoted from one run with
the spread said.

### 2.2 The scenarios

| name | layout | what makes it the case |
|---|---|---|
| `drag` | B:'s Disk window parked with its right edge across B:'s cell; A:'s window, shrunk to 194x100, dragged from the left edge to the right-hand column, over A:'s cell, the top of B:'s and the parked window's corner | the parked window does not overlap the mover's VACATED rect, so SPEC.md §11.91.2 has nothing to mark it for - the cells are the only reason it repaints |
| `drag --parked tm` | the same with the Task Manager parked over the cell, a window with no raise-cache restore, and the folder window it was opened from moved clear of it so it is not marked transitively | a needless mark paying a W_PAINT instead of a cache blit |
| `close` | tests/zonedmg.py's layout: W shrunk, its corner over B:'s cell; V below it, its top edge across the cell; V is closed | V's frame reaches the cell and not W |
| `cell` | B:'s window parked over B:'s cell; the cell posted dirty the way `desk_cmark` posts a mount | no window moved at all |
| `plain` | two shrunk Disk windows top-left, a Calculator moved across plain desktop beneath both | the control: no cell, only the dither |

### 2.3 Per gesture, today

Hercules (720x348, 1bpp), `os8088_5150_herc_gla`:

| gesture | pass | calls | ground | cells | windows phase | promote | windows repainted, and how much |
|---|---|---|---|---|---|---|---|
| drag | **233.28 ms** | 68 | 36.75 | **70.20** (2 cells) | 121.56 | 0.26 | mover 68.03 ms (cache); **parked 51.12 ms (cache, its 322x22 strip under the cells' box) - owed only because the cells were drawn over it** |
| drag, Task Manager parked | **779.69** (761.26 a second run) | 268 | 74.85 | 70.85 | 629.21 | 0.27 | mover 108.86 (cache); SYSTEM 221.53 (W_PAINT, legitimately - the mover's old rect uncovered it); **Task Manager 296.43 ms, W_PAINT, 152 calls - owed only because of the cells** |
| close | **156.90** | 51 | 44.59 | 34.89 (1 cell) | 60.82 | 0.27 | **W 58.35 ms (cache, owed 383..717 x 92..318) - owed only because the cell was drawn over its corner** |
| cell | **130.96** | 57 | 9.15 | 34.89 (1 cell) | 44.87 | **37.63** | **parked 43.13 (cache)**; and `.promote` redraws the FRONT window's title and grow box, 38 calls, because it was not redrawn in this pass - although no window changed z-order (section 6.3) |
| plain | **202.14** | 67 | 32.76 | 0.81 | 163.98 | 0.27 | Disk 39.23, APPS 48.11, Calculator 74.30 (all cache, all legitimately uncovered) |

VGA (640x480, mode 12h), `os8088_xt_vga`:

| gesture | pass | calls | ground | cells | windows phase | promote | windows repainted, and how much |
|---|---|---|---|---|---|---|---|
| drag | **195.10 ms** | 68 | 23.85 | **50.72** | 115.76 | 0.26 | mover 75.23; **parked 38.13 (cache) - the cells' doing** |
| drag, Task Manager parked | **630.71** (633.68) | 220 | 29.45 | 49.52 | 545.72 | 0.27 | mover 171.42; SYSTEM 77.77 (legit); **Task Manager 293.54, W_PAINT, 149 calls - the cells' doing** |
| close | **120.45** | 51 | 29.66 | 26.39 | 49.21 | 0.26 | **W 47.95 (cache) - the cell's doing** |
| cell | **238.25** | 108 | 7.80 | 25.17 | **200.59** | 0.26 | **parked 33.62 (cache) - and window 0, which sits ABOVE the parked one and overlaps it, 164.52 ms of W_PAINT, 89 calls: the transitive mark (§11.91.3). This is the cascade, measured** |
| plain | **220.96** | 67 | 20.55 | 0.81 | 195.00 | 0.26 | Disk 36.06, APPS 58.58, Calculator 98.03 (legit) |

`kern_small` on the same Hercules (`OS88_BUILD=<tree>/smallk
OS88_DEFINES=KERN_SMALL --image ../small360.img`; it does not drive a VGA,
SPEC.md §39.27). Its cell is 32 wide on a 44 pitch and sits 56 pixels in
from the band's right edge (`DESK_CW`/`DESK_PX`/`DESK_ZXOFF`), so the same
layouts put the cells at x 662..697:

| gesture | pass | calls | cells | windows phase | promote | windows repainted |
|---|---|---|---|---|---|---|
| drag | **214.93 ms** | 68 | 60.54 (2 cells) | 114.77 | 0.26 | mover 65.10; **parked 47.28 - the cells' doing** |
| close | **146.78** | 51 | 29.99 | 56.62 | 0.26 | **W 55.50 - the cell's doing** |
| cell | **118.26** | 57 | 29.99 | 43.52 | **36.35** | **parked 41.80**, and the spurious promotion |

What one whole cell costs, stop by stop (`--detail`), Hercules / VGA: ground
8.1 / 5.4 ms, picture (with `desk_caprect`'s caption measure) 21.4 / 16.6,
caption box 1.7 / 0.9, caption text 2.6-3.3 / 3.0 - **~35 / ~26 ms a cell**,
of which the 32x32 picture is 60%. In the `drag` rows nearly all of those
pixels are then painted over by the mover or the parked window, which are
on top of them; in `kern_small`'s, all of them are (section 4.1).

**Verify read 0 differing pixels on every row** (the menu bar's clock aside).

## 3. Where the waste is, before any option

Two quantities, and the options attack them separately:

- **The cells' own pixels**: a cell drawn whole where it is covered or
  outside the damage. 70.2 of the Hercules `drag` row's 233.3 ms, nearly all
  of it under the mover.
- **The windows the cells mark**: the parked window (51.1 / 38.1 ms), the
  Task Manager (296.4 / 293.5), W (58.4 / 48.0), and in `cell` the
  transitive W_PAINT (164.5 on VGA). This is the owner's half, and on every
  row except `drag` it is the larger one.

## 4. Options

### 4.1 Option B - whole-or-nothing per cell (PROTOTYPED, MEASURED: NOTHING on `kern_big`, the answer on `kern_small`)

Per touched zone ask the region: overflow or partly covered -> today's path
(drawn whole, its box marks the windows over it); wholly visible -> drawn
unclipped and marks nothing; wholly covered (by FRAMES, inside the damage,
so a shadow corner is never mistaken for cover) -> not drawn at all.

**+73 resident bytes on both kernels** (`.text` +61, `.cold` +12), measured
with `tools/kernsize.py --json` against the base. Measured, today -> B:

| gesture | `kern_big` Hercules | `kern_big` VGA | `kern_small` Hercules |
|---|---|---|---|
| drag | 233.28 -> 236.64 | 195.10 -> 198.46 | 214.93 -> **121.05** |
| close | 156.90 -> 158.23 | 120.45 -> 123.00 | 146.78 -> 147.06 |
| cell | 130.96 -> 132.40 | 238.25 -> 239.75 | 118.26 -> 119.47 |

**On `kern_big` it buys nothing and costs the region builds** (~1.3-1.7 ms a
touched zone), and the reason is geometric rather than a tuning matter: **a
window cannot cover a `kern_big` cell in the right-hand column.** A cell's
rect carries its caption's 2-pixel overhang to x 717 (Hercules) / 637 (VGA),
while §11.94 puts a frame's right edge on a multiple of 8 and the drag keeps
it on the screen, so the furthest a window reaches is 712 + its shadow at
713 / 632 + 633. A drag aimed 64 pixels past the edge landed in the same
place. That column is therefore always PARTIAL under a window, never
COVERED, and the partial case is today's path. B could fire there only for
an inner column (a shortcut, SPEC.md §26.8) and for "clear air", where no
window is over the cell and there was nothing to mark anyway.

**On `kern_small` it is the whole of the drag row, -93.9 ms (44%).** The
cell there is 32 wide and 56 pixels in from the band's edge, so the mover
over the top of B:'s cell and the parked window over its bottom cover it
between them - FRAMES only, verified at 0 differing pixels - and both cells
are skipped (cells phase 60.54 -> 0.17 ms) and the parked window is not
marked (47.28 ms -> not drawn). Where a cell is partly visible, which is
`close` and `cell`, it is today's path plus ~1.1 ms of region build.

### 4.2 Option A - clip the desktop's own drawing (PROTOTYPED, MEASURED)

**What changes.** A touched cell is drawn into the region **zone AND damage,
minus every visible window's FRAME** - the dither's own region (§11.91.1),
intersected with the cell. Three facts make that the right region and not
`wm_clip_rect`'s:

1. **AND damage.** Outside the damage the cell is already correct on the
   glass; today it is erased and redrawn anyway, which flashes its picture's
   black pixels white for the length of the mask pass.
2. **Frames, not occupied boxes.** `wm_clip_rect` subtracts the occupied box,
   whose corners `(x+w, y)` and `(x, y+h)` no window draws. The dither does
   draw them, so a cell clipped by boxes would leave a dithered pixel where
   its picture or caption should be. Subtracting frames agrees with the
   dither on every pixel.
3. **...so the cell draws over shadow L lines, and that costs nothing new.**
   The region lies inside the damage, and §11.91.4's `wm_dmg_shadowed`
   already owes the L of every window whose shadow the damage reaches. The
   prototype first carried a separate "zones' shadow box" for this; breaking
   it on purpose changed **0 pixels**, it was redundant, and it came out
   (-68 bytes).

**Each fragment is drawn alone.** The fills clip per pixel against any
number of fragments, but `font_char` and an icon can only be exact against
ONE (§11.3.2's under-draw, §11.3.4's winning fragment). So
`desk_zone_clip` swaps fragment *k* into slot 0 of `wm_clip_tab`, sets
`[wm_clip_n]` to 1, calls `desk_draw_zone` unchanged, and swaps back - no
copy of the list, no new `.bss`, and no change to `desk_draw_zone` at all.
Overflow (more than 16 fragments) is today's path: drawn whole, the zone's
box grown into `[wm_dmg_zb]`, so fca7231's per-window marking becomes the
fallback rather than the rule.

**The icon gains ROWS and COLUMNS (`ico_clip`).** Today an icon under a
region is whole or nothing, so a cell cut by a window edge would lose its
picture and keep its ground - the granularity rule of §11.3 broken in the
desktop. `ico_clip` replaces `ico_core`'s `wm_clip_test`: it asks
`wm_clip_rows` (§11.3.2) for the row range and the winning fragment, works
out one 16-bit column mask per icon word from that fragment's x1/x2, and if
anything is cut **restages the body into `ico_ibuf`** - only the drawable
rows of the mask table, then the same rows of the data table, every word
ANDed with its mask. The passes write only SET bits, so clearing a column
from both tables clips it exactly, and the unclipped path pays nothing: the
branch is above it (`CLIPQ` / `je .noclip`). Under the cull (§11.3.3) it
answers whole, as today. The stage is in place for an indexed or staged
record (the destination never passes the source) and fits `ICO_IBUF_SZ` for
any icon up to 32x32; a wider record keeps `wm_clip_test`.

The two halves of the icon cost separately and are needed separately:

| | what it buys | bytes |
|---|---|---|
| rows only (§11.3.2's shape) | a HORIZONTAL cut - `close`, where the damage's top edge crosses the picture | ~35, ESTIMATED: bias `[ico_maskp]`/`[ico_datap]` by `r0 * rb` and take `rn` rows, the arithmetic ico_core's top-of-screen clip already does |
| columns (§11.3.4's shape) | a VERTICAL cut - a window's side edge across the picture, which is `drag` and `cell` | the rest of the measured 268-byte `ico_clip` span |

Without columns, a vertically cut cell has to take the fallback, and that
is both rows that save the most. So columns are not optional.

**Bytes, measured** (`tools/kernsize.py --json`, prototype against base):
`kern_big` **`.text` +324, `.bss` +5, `.cold` +155 = +484 resident**,
`KERN_SIZE` +0. `kern_small` +0 (the prototype is `%ifdef KERN_BIG`
throughout). By symbol span: `ico_clip` 268, `wm_zone_r` 44, `desk_zone_clip`
with its swap 121; the rest is the dispatch flag, the thunk and C2 below.
The prototype was written for a measurement and not for size; the mask
builder clamps both distances by compare-and-branch where §11.3.4's
two-shift idiom would serve, and the restage could share the indexed
decoder's store loop. A production version is **~400 bytes, ESTIMATED**
against that 268 + 121.

### 4.3 Option C2 - a cell repainted in place uncovered nothing

`desk_zones_paint_x` repaints a cell with the cell's rect as the damage, so
every window over the cell OVERLAPS THE DAMAGE and is marked by the damage
test itself - option A alone does not reach it. Measured, A without C2:
`cell` **145.30 ms** on Hercules and **253.06** on VGA, both WORSE than
today, because the cell got dearer (section 4.4) and every window still
repainted.

But nothing was uncovered: no window moved. That is exactly what §11.91.2's
vacated rect expresses, so C2 arms it EMPTY - `[wm_dmg_stwin]` a value no
window has, the vacated rect's x2 = -32768, which no rect meets - and every
window over the cell passes `wm_dmg_stale` as "nothing of it was uncovered".
The dither's own eaten L lines are still owed through `wm_dmg_shadowed`
(measured: the `cell` row's windows phase is 9.3 ms, two fills). **17 bytes
of `.cold`**, three stores, consumed by `wm_dmg_wins` like every one-shot
there.

**...and the same pass must not PROMOTE.** `.promote` redraws the front
window's title and grow box whenever that window was not redrawn in this
pass - 37.6 ms on Hercules in today's `cell` row, a pre-existing waste
(section 6.3). With C2 a cell repaint redraws nobody, so it would pay that on
every mount. A one-shot `[wm_dmg_npro]` set beside the stwin store skips it:
**11 bytes of `.text`** (`xor`/`xchg`/`or`/`jnz` and the byte) and the
5-byte store, by count, inside the +484; **19 measured** where it was
assembled on its own beside B (section 5.1).

C2 is only correct WITH A: without clipping, the cell is drawn over the
window and the window has to be marked.

### 4.4 What A+C2 measures

Hercules:

| gesture | today | A+C2 | cells phase | windows repainted |
|---|---|---|---|---|
| drag | 233.28 | **152.16** (-81.1) | 70.20 -> 30.20 | mover only (68.05); **the parked window not touched** |
| drag, Task Manager | 779.69 / 761.26 | **446.10** / 427.12 / 442.49 | 70.85 -> 26.93 | mover, SYSTEM; **the Task Manager not touched** (-296 ms of W_PAINT) |
| close | 156.90 | **117.13** (-39.8) | 34.89 -> 19.30 | **none** - W is not redrawn; `.promote` then gives it the front window's pinstripes, 36.66 ms, which today it got inside its full repaint |
| cell | 130.96 | **73.45** (-57.5) | 34.89 -> **50.62** | none; the parked window's L, 2 fills, 9.28 ms; no promotion |
| plain | 202.14 | 202.15 | - | unchanged |

VGA:

| gesture | today | A+C2 | cells phase | windows repainted |
|---|---|---|---|---|
| drag | 195.10 | **140.27** (-54.8) | 50.72 -> 28.05 | mover only (75.23) |
| drag, Task Manager | 630.71 / 633.68 | **333.00** / 317.21 / 335.25 | 49.52 -> 22.93 | mover, SYSTEM; **not the Task Manager** |
| close | 120.45 | **85.90** (-34.6) | 26.39 -> 15.77 | none; promotion 25.27 |
| cell | 238.25 | **60.03** (-178.2) | 25.17 -> **39.31** | none - **the 164.5 ms transitive W_PAINT is gone with the mark that started it**; the L, 8.48 ms |
| plain | 220.96 | 220.99 | - | unchanged |

Primitive calls per pass, today -> A+C2: Hercules drag 68 -> 64, Task
Manager 268 -> 122, close 51 -> 44, cell 57 -> 15; VGA 68 -> 64, 220 -> 77,
51 -> 44, 108 -> 15.

**Where A costs instead of saving: a cell cut into fragments, with no window
spared.** `cell`'s cell is two fragments (the band above the parked window
and the strip right of it) and its cells phase went **34.9 -> 50.6 ms**
(+15.7 Hercules, +14.1 VGA): each fragment pays `desk_draw_zone`'s fixed
calls again, the region build is ~1.8 ms a zone, and a caption the fragment
cuts goes through `font_run`'s per-cell path. A gesture where every window
over a cell is marked by the damage anyway and the cell is fragmented is
the one that gets dearer, by about that much. None of the five measured is
one.

**Verify: 0 differing pixels on every row** except `drag`, where it reads
**32 on both adapters, and they are the REFERENCE's** (section 6.2): the
parked window's title glyphs left of the mover's bottom corner, which
`wm_paint_all` under-draws and A no longer redraws.

**Visual risk, looked at on a 1bpp adapter.** No pixel outside the damage
is written (today the whole cell is). Inside a fragment the order is
today's - ground, picture, caption - so the double-draw is today's on fewer
pixels; nothing is drawn twice ACROSS fragments, they are disjoint. The
dither's phase is absolute (§11.91.1), so ground drawn in fragments is the
ground drawn whole; Hercules verified at 0 pixels. An icon is never left
half-drawn - the hole the whole-icon rule would have left is exactly what
`ico_clip` exists to prevent, and breaking its column mask is 32 pixels
(section 5.2). Two things were NOT looked at: the extended desktop
(`ico_clip` translates by `[vid_ox]` under `gfx_dnest`, untested on
`os8088_5150_both_gla`), and `m.flicker`, which would price the shorter
erase-redraw window in transient pixels rather than assert it.

**What it retires.** `[wm_dmg_zb]`'s per-window marking and `wm_su_owed`'s
zone widening stay, as the overflow fallback, and stop being reached by any
cell the region can describe. `tests/zonedmg.py`'s assertion inverts: the
window over the cell must NOT be redrawn, and its pixels must still be
right. ico_clip changes what a BACKGROUND painter's partly covered icon does
- the winning fragment's part is drawn where nothing was - which is §11.3.2's
own direction (fewer pixels than visible, never one outside).

### 4.5 Option C - compose the cell into a band and blit it (NOT BUILT, REFUSED ON ITS ESTIMATE)

Compose ground, picture and caption into a 1bpp band in RAM and put it down
with one `gfx_blit1`, which on `kern_big` already walks every fragment
(§5.4.2.7). Every pixel would be written once, ending the ground-then-picture
double draw too. Three things price it out:

- **The blit under-draws at a fragment's left edge**: up to seven columns,
  and systematically SIX right of a snapped window's shadow (§5.4.2.7's own
  measurement). On the desktop that is stale picture beside a window's right
  edge - exactly `drag`'s and `cell`'s geometry - so it needs a head mask
  before it can be used here.
- **A composer**: dither, icon passes and glyphs into RAM. The nearest thing
  in the tree is `kernel/band.inc` (`BAND=1`, the title-bar composer),
  **1,634 bytes** and not shipped. ESTIMATED >= 1,200 bytes resident plus a
  598-byte band (`DESK_CW` + overhang, 13 bytes x 46 rows) of `.bss`.
- **Colour.** `gfx_blit1` carries two colours (§5.4.2.2); a cell is ground,
  white and black, which is two only while the theme's desktop is the
  black-on-white dither. A coloured `OS88_THEME` ground is three.

### 4.6 The window side: "revealed area only"

With A+C2 the window side already IS revealed-area-only for every case
measured: a window is marked by the damage and the vacated rect (§11.91.2),
by the dock strip, or transitively - and the cells no longer mark at all
short of overflow. The remaining over-mark is §11.91.3's transitive rule,
whose measured negative result stands; what A does is remove the FIRST mark
that set the VGA `cell` cascade going, which is worth more than any
narrowing of the rule would be.

## 5. Recommendation

### 5.1 The trade, per build

**`kern_big`: take A + C2.** Spend **~400 resident bytes** (ESTIMATED for a
production version; **484 measured** for the prototype) for, on a 4.77 MHz
8088:

| gesture | Hercules | VGA |
|---|---|---|
| a window dragged over the cell column with a window parked over a cell | **-81 ms** of 233 | **-55** of 195 |
| ...the parked window a package with no cache (the Task Manager) | **~-330** of ~770 | **~-300** of ~632 |
| a window closed whose frame reaches a cell another window covers | **-40** of 157 | **-35** of 120 |
| a cell repainted in place under a window (every mount, medium change, item move) | **-57** of 131 | **-178** of 238 |
| no cell touched | 0 | 0 |

and **+14 to +16 ms** on a cell cut into two fragments where no window
would have been spared.

C2's promotion one-shot (**19 bytes**, measured on `kern_small` as B plus
the one-shot against B alone) is separable and worth having **even without A**:
it is the 37.6 ms of today's Hercules `cell` row spent redrawing a front
window that did not change. Without A the rest of C2 is useless (the cell is
drawn over the windows, so they must be marked) but harmless.

**`kern_small`: B plus C2's promotion one-shot, or nothing - the owner's
call.** Measured as one prototype, **+92 resident bytes** (`.text` +75,
`.cold` +17; B's 73 and the one-shot's 19), on Hercules:

| gesture | today | B + promotion one-shot |
|---|---|---|
| drag over the cells, the mover and a parked window covering them between them | 214.93 ms | **121.05** (-93.9) |
| a cell repainted in place under a window | 118.26 | **83.21** (-35.1: the promotion) |
| close (the cell partly visible) | 146.78 | 147.05 (+0.3) |
| any pass touching a partly visible cell | | +~1.1 ms a cell, the region build |

Against it stands the floor machine's standing rule - SPEC.md §39.27.4,
nothing is spent from the rungs `kern_small` gave back - which is why this
is a question rather than a recommendation. A on `kern_small` is NOT the
alternative: it needs `font_char`'s column cut, which §11.3.4.1 refused on
this build (93 bytes, plus §11.3.4.2's 25 for `font_run_cell`), on top of
`ico_clip` and the fragment loop - **~600 bytes ESTIMATED** to spare the
`close` row's W (55.5 ms, less the ~36 ms promotion it would then be owed,
as on `kern_big`) and the `cell` row's parked window (41.8).

### 5.2 What gates it, and that each goes red

Each of these was broken on purpose against the prototype (`cell`
scenario, `verify`) and read:

| break | Hercules | VGA |
|---|---|---|
| the region keeps the damage but subtracts no frames (cell drawn over the parked window, which is not marked) | **495 pixels** in 618..664 x 116..137 | - |
| `ico_clip`'s column masks forced to 0FFFFh (rows clipped, columns not) | **32 pixels** in 654..664 x 116..123 | **32** in 574..584 x 116..123 |
| C2 taken out | windows phase 44.79 ms, promote 36.41 | windows phase 201.32 ms (the transitive W_PAINT back) |
| the zones' shadow box taken out | **0** - redundant, removed (section 4.2) | **0** |

The rows to build:

1. **`tests/deskclip.py`, a soak row** over `tools/deskclip.py`'s `cell`,
   `close` and `drag` on `os8088_5150_herc_gla` and `os8088_xt_vga` (and, if
   `kern_small` takes B, its `drag` on Hercules: with B taken out - which is
   today's kernel - the parked window repaints, 47.28 ms). Assert
   (a) no `wm_draw_win` for the parked window / W, (b) the pixels of
   *window AND cell* against a whole repaint - NOT the whole screen, for
   section 6.2's reason - and (c) a `cell` pass with no `.promote` calls. The
   three breaks above are its section-1 proof (docs/WRITING-TESTS.md §1).
2. **`tests/zonedmg.py`**, inverted: W must not be redrawn, and W over the
   cell must still match. Today's break (`clc` before `.mnodmg`'s `jc .mset`)
   stops meaning anything; its new one is the first row of the table.
3. **Unchanged rows to run**: `tmgraph` (the BAR leg is fca7231's), `dmgcull`
   (`ico_clip`'s cull arm draws whole), `icoclip` (the `.bss` span a draw
   moves: the restage writes `ico_ibuf` only), `runclip`/`runclipcga`,
   `deskitem`, `desksc`, `deskfdd`, `wirezone` (the service item's picture is
   the DRIVER's, drawn per fragment), `dispcheck`, and `dispseam` for the
   extended desktop this was not run on.

## 6. Findings on the way

### 6.1 `kern_big`'s edge column cannot be covered, and `kern_small`'s can

Section 4.1: on `kern_big`, x 714..717 (Hercules) and 634..637 (VGA) are
reached by no window that stays on its display, so "is this cell under a
window" is never YES for the column the drives live in. `kern_small`'s
narrower cell sits 56 pixels in from the edge and is covered routinely. The
same option is therefore worthless on one kernel and the whole answer on the
other, which no amount of reasoning about either would have said - it took
running both.

### 6.2 `wm_paint_all` is not a perfect reference

In `drag`, the parked window's title text "Disk" sits just left of the
mover's bottom-left corner. Its glyph cells are visible across TWO fragments
- the strip beside the mover and the region below it - and §11.3.2's
one-fragment rule draws only one fragment's rows, so **the whole repaint
leaves rows 121-123 of four letters blank**, and today's damage pass leaves
the same, because it redraws the window the same way - so on today's kernel
this is on the glass after any repaint of a window whose title a corner
crosses like this. A+C2 does not redraw
the window and keeps the letters, so the full-screen compare reads 32 pixels
"wrong" that are the reference's. It is pre-existing, visible on the glass
today wherever a window's corner sits across another's title, and is
§11.3.4.2's accepted under-draw seen from the title bar. Out of scope here;
a gate must compare the rects it is about.

### 6.3 `.promote` fires whenever the front window was not redrawn

`wm_paint_dmg`'s promotion asks only whether `wm_top` was redrawn in this
pass, not whether the front window CHANGED. Every pass that leaves the front
window alone therefore redraws its title bar and grow box - 38 calls, 37.6 ms
on Hercules (25.3 on VGA, measured in `close`, where the same draw is done
for a reason) - for pixels that were already right. Today it
mostly hides, because the front window is usually marked by something; A
makes it visible on every pass that marks nobody. C2's one-shot fixes the
in-place cell repaint; a general fix (remember the front window before the
pass and promote only when it differs) is a few bytes more and covers the
`close` row's legitimate case unchanged.

### 6.4 The dither-only cascade is gone already

The `plain` control repaints three windows on both adapters, and all three
were uncovered by the move: §11.91.1 and §11.91.2 already make the dither
exact. The owner's "original desktop" cascade survives only through the
cells, which is why this plan is about them and nothing else.

## 7. Open questions

1. **Does a production `ico_clip` reach ~170 bytes?** The estimate in section
   4.2 rests on the prototype's span and on §11.3.4's mask idiom; it is not a
   measurement.
2. **The extended desktop.** The cells live on the primary, the region is
   virtual, `ico_clip` translates under `gfx_dnest`. Untested.
3. **`m.flicker`.** A+C2 writes fewer pixels and erases nothing outside the
   damage; the transient-pixel count would say how much of that a person
   sees. Not taken.
4. **A general promotion test** (section 6.3) as its own change, both
   kernels.

## Appendix A. The prototype, as measured

Kept here because the private tree it was built in does not outlive the
session, and because section 4.2's byte estimate is argued against it. It is
`%ifdef KERN_BIG` throughout, written for a measurement and not for size, and
it is the +484 of section 4.2 - not a review-ready change.

`kernel/wm.inc`, beside `wm_rgrow`, with a `cw_wm_zone_r` thunk in
`kernel/kernel.asm`:

```nasm
; in: AX..DX = the zone's rect. out: CF = 1 overflowed (nothing armed);
;     CF = 0 ZF = 1 nothing to draw; CF = 0 ZF = 0 armed. Regs preserved.
wm_zone_r:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, wm_dmg_x1
    call gfx_rect_isect         ; zone AND damage...
    mov si, dx
    mov dx, bx
    call wm_clip_seed
    call wm_dmg_occl            ; ...minus every FRAME: the dither's region
    jc .over
    cmp word [wm_clip_n], 0
    jmp kret_si
.over:
    call wm_clip_clear
    stc
    jmp kret_si
```

`wm_paint_dmg`'s `.promote`, first four lines, with `wm_dmg_npro db 0`
beside `wm_dmg_fs`:

```nasm
    xor al, al                  ; a pass that uncovered NOTHING promoted
    xchg al, [wm_dmg_npro]      ; nobody (one-shot, desk_zones_paint)
    or al, al
    jnz .out
```

`kernel/desk.inc`: `desk_paint_x` sets `[desk_pmc]` to 0 and
`desk_paint_mask_x` to 1, `desk_pm_body` calls `desk_zone_clip` instead of
`desk_draw_zone` when it is 1, `desk_dmg_zones_x` stops growing
`[wm_dmg_zb]` (the fallback below does it), and `desk_zones_paint_x` arms
C2 in front of its `cw_wm_paint_dmg`:

```nasm
    mov byte [wm_dmg_npro], 1   ; nothing is promoted...
    mov word [wm_dmg_stwin], 1  ; ...and nothing was uncovered: no window is
    mov word [wm_dmg_st1+4], 8000h ; at 1, and x2 = -32768 meets no rect
```

```nasm
desk_zone_clip:                 ; AL = zone; preserves all
    push ax
    push bx
    push cx
    push dx
    push si
    push ax
    call desk_zs
    pop ax
    jc .out                     ; not shown
    push ax
    call desk_zone_rect
    call KERNEL_SEG:cw_wm_zone_r
    pop ax
    jc .whole
    jz .out                     ; nothing of it revealed
    mov si, [wm_clip_n]
.f:
    dec si                      ; fragment SI, ALONE: swapped into slot 0
    mov bx, si
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, wm_clip_tab
    call desk_fswap
    push word [wm_clip_n]
    mov word [wm_clip_n], 1
    call desk_draw_zone         ; unchanged
    pop word [wm_clip_n]
    call desk_fswap
    or si, si
    jnz .f
    call KERNEL_SEG:cw_wm_clip_clear
    jmp short .out
.whole:                         ; overflow: today's path, and its marking
    push ax
    call desk_zone_rect
    call KERNEL_SEG:cw_wm_dmg_zadd
    pop ax
    call desk_draw_zone
.out:
    jmp kretc_si

desk_fswap:                     ; swap the rect at BX with wm_clip_tab[0]
    push ax
    push cx
    push si
    mov si, wm_clip_tab
    mov cx, 4
.s:
    mov ax, [si]
    xchg ax, [bx]
    mov [si], ax
    inc si
    inc si
    inc bx
    inc bx
    loop .s
    sub bx, 8
    pop si
    pop cx
    pop ax
    ret
```

`kernel/icons.inc`: `ico_core` calls `ico_clip` where it called
`wm_clip_test`, and `ico_cmw resw ICO_IBUF_WW` joins the `ico_*` cells.

```nasm
; in: AX..DX = the icon's rect; SI -> body. out: CF = 1 draw nothing;
;     CF = 0 draw from SI (ico_ibuf when restaged; ico_h/ico_y moved)
ico_clip:
    cmp word [ico_ww], ICO_IBUF_WW
    ja .whole                   ; too wide to stage: whole-or-nothing
    call wm_clip_rows
    jc .ret
    cmp byte [wm_dmg_cull], 0
    jne .okc                    ; the cull draws a shape whole (11.3.3)
    push si
    push di
    push bp
    mov bx, [wm_clip_wf]        ; the winning fragment
    mov ax, [ico_x]
    cmp byte [gfx_dnest], 0
    je .v
    add ax, [vid_ox]            ; the fragment is VIRTUAL (39.14.4)
.v:
    mov di, ico_cmw
    mov bp, ICO_IBUF_WW
    xor si, si                  ; SI = "a column is cut"
.m:                             ; one 16-bit mask a word of the row
    mov dx, [bx+WCR_X1]
    sub dx, ax                  ; d0, clamped 0..16
    jns .m0
    xor dx, dx
.m0:
    mov cx, ax
    add cx, 15
    sub cx, [bx+WCR_X2]         ; d1, clamped 0..16
    jns .m1
    xor cx, cx
.m1:
    cmp dx, 16
    jbe .m2
    mov dx, 16
.m2:
    cmp cx, 16
    jbe .m3
    mov cx, 16
.m3:
    push ax
    push bx
    xchg cx, dx
    mov ax, 0FFFFh
    shr ax, cl                  ; (0FFFFh >> d0) & (0FFFFh << d1)
    mov bx, 0FFFFh
    mov cl, dl
    shl bx, cl
    and ax, bx
    mov [di], ax
    not ax
    or si, ax
    pop bx
    pop ax
    inc di
    inc di
    add ax, 16
    dec bp
    jnz .m
    cmp word [ico_ww], 1
    jne .w2
    mov ax, [ico_cmw]           ; one word a row: word 1's mask is not ours
    not ax
    mov si, ax
.w2:
    mov al, [wm_clip_r0]        ; the whole icon, uncut? draw it as it is
    or al, al
    jnz .stage
    mov al, [wm_clip_rn]
    xor ah, ah
    cmp ax, [ico_h]
    jne .stage
    or si, si
    jz .nost
.stage:                         ; rows r0..r0+rn of BOTH tables -> ico_ibuf,
    pop bp                      ; masked; dest never passes source
    pop di
    pop si
    push di
    push bp
    push es
    push ds
    pop es
    mov bx, [ico_ww]
    mov ax, bx
    shl ax, 1
    push ax
    mul word [ico_h]
    mov bp, ax                  ; BP = mask table -> data table
    pop ax
    mov dl, [wm_clip_r0]
    xor dh, dh
    mul dx
    add si, ax
    mov di, ico_ibuf
    mov cl, [wm_clip_rn]
    xor ch, ch
    push cx
    push si
    call .tab
    pop si
    add si, bp
    pop cx
    push cx
    call .tab
    pop cx
    mov [ico_h], cx
    mov al, [wm_clip_r0]
    xor ah, ah
    add [ico_y], ax
    mov si, ico_ibuf
    pop es
    pop bp
    pop di
    clc
    ret
.nost:
    pop bp
    pop di
    pop si
.okc:
    clc
.ret:
    ret
.whole:
    jmp wm_clip_test
.tab:                           ; CX rows of BX words, DS:SI -> ES:DI
    mov dx, bx
    push bx
    mov bx, ico_cmw
.tw:
    lodsw
    and ax, [bx]
    stosw
    inc bx
    inc bx
    dec dx
    jnz .tw
    pop bx
    loop .tab
    ret
```

Option B's prototype is `wm_zone_q` - `wm_clip_rect`, then `wm_clip_test`
for clear air, then `wm_dmg_occl` over zone AND damage for "covered by
frames" - called from `desk_dmg_zones_x` in place of the unconditional
`cw_wm_dmg_zadd`: a covered zone shifts in a 0, a clear one a 1 with no box,
a partial or overflowed one a 1 and the box. 73 bytes.

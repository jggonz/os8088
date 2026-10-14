# ISOTRON-PLAN: isoTron, a light-wall duel in a raised 3D arena

Status: DESIGN RECORD, written before any code. Branch `game/isotron`,
worktree `/Users/jggonz/Repos/os8088-isotron`. Author: the architect pass of
the native-game-port workflow.

Authority, when two documents disagree: SPEC.md §108 (once written) > this
plan > the five scout reports (`scout-rules`, `scout-gfxwin`, `scout-gfxfs`,
`scout-timing`, `scout-integ`) in the reports folder outside the repo. The
scouts are inputs, not contracts. §2 says where they disagreed and which one
won.

## 0. Provenance policy (binding, user decision)

1. isoTron is a remake from **observed rules**. There is no source, no
   disassembly, and nothing is transcribed. Every surface is drawn by code:
   a slab, a dither, a wall column. No reference pixel is committed, traced
   or read by `make` or any test.
2. The name is **isoTron** on every screen, in the window title and in every
   file. The package is `apps/isotron/` and the file is `ISOTRON.O88`. The
   original's title, its publisher's name, address and copyright line, and
   the original's joke labels never appear. There is no copyright line
   anywhere. The title lettering is our own: extruded block letters drawn by
   the game's own wall renderer (§9.1), not the film's logo style.
3. The reference frames, the original executable and the scouts' working
   files live outside the repo. `tests/unit/t_isotron_clean.py` is a
   fast-tier gate that fails `make` if the game's sources, the isoTron tools
   and tests, or the Makefile block name them (§12.1).
4. The constants below (grid, projection, wall height, pace, tower rule) are
   **facts measured from the footage**, pinned as the remake's own constants.
   Rules are not expression. The AI, the difficulty tiers beyond the first,
   the sounds, the 16-colour palette, the 1bpp patterns, the screens and all
   wording are our own invention, and SPEC.md §108 says so where it pins them.

## 1. Decisions at a glance

| subject | decision | why (section) |
|---|---|---|
| SPEC section | **§108** (§107 is taken on `feature/dia`; §104 is reserved by the MIDIRack worktrees) | §2 |
| grid | **one cell = one CGA pixel**: u = 1..215 by d = 1..99 inside a ring of outline cells (217 x 101 grid) | §3.1 |
| projection | oblique (cabinet) 45 degrees, not isometric: `x = u + d`, `y = 174 - d - 24(h-1)` in a 320x200 logical picture | §3.1 |
| wall | a cell is one screen column: 1 white top pixel and 22 face rows | §3.1 |
| occlusion | **one column span per step, at most 22 grid probes, zero cells redrawn**, proved in §5 | §5 |
| picture | one adapter-independent **320x200 canvas of 4-bit SURFACE IDS** (32,000 bytes). Colour and pattern are applied only when the canvas is presented | §6 |
| windowed renderer | the canvas presented at 2x horizontally with a row map, through `OSAPI_GFX_BLIT1` (CGA, Hercules, mono fallback) or `OSAPI_GFX_BLITP` (VGA, EGA) from small strips, one lock hold per worker wake | §7 |
| full screen | CGA **320x200x4, palette 1 low intensity** (the original's own look, 1:1); VGA **mode 0Dh 320x200x16** (1:1, direct planar spans); EGA **same-mode 640x350x16** bracket through the kernel's `gfx_blitp` (2x by 1.75); Hercules **720x348 1bpp** (2x by 1.5, centred) | §8 |
| toggle | **F / f** and **Alt+Enter** both ways, plus a Game menu item. Esc ends the game first, then leaves full screen (§11.2.1). The game state and the canvas are untouched by a toggle | §8.6 |
| pace | the original's **iteration** model: every live computer head moves 1 cell, the player moves p/6 cells, and each iteration is charged its fitted 5150 cost against a real clock | §4 |
| difficulty | Rookie / Veteran / Lunatic: the same model with the cost scaled x1, x3/4, x1/2, plus a longer AI look-ahead | §4.3, §10 |
| tower | **a player win that follows a player win raises the tower; past 4 it resets to 1**. This fits all 22 rounds of footage, and is the brief's anomaly resolved | §3.4 |
| computer | **h heads at tower height h**, from one cell | §3.3 |
| AI | seeded 16-bit LCG, exactly one byte per head-move; look-ahead, blocked turn, intercept, wander and U-turn | §10 |
| input | keystrokes into a 3-deep turn FIFO, popped once per player move; a typematic filter on turns only | §11 |
| audio | `OSAPI_SND_TONE` only: a soft tick every 4th player cell, a falling crash sweep, round-start beeps, a tower chime and a game-over phrase | §12 |
| memory | **one 59,037-byte claim** (canvas, grid and strip), image of 16 KB or less, bss of 8 KB or less; kern_small refuses in words | §13 |
| disk class | **ships**: in `APPS_GAMES` (apps, apps720, apps120 and games360), on the live media, filtered off `apps360.img` with a dated §24.6.1 note; plus an on-demand `make isotrondisk` in four geometries | §15 |
| reference model | `tools/isotronsim.py`: rules, AI, pace, canvas and every presenter. The guest equals it step for step and byte for byte | §14 |

## 2. Where the scouts disagreed, and who won

1. **The grid pitch.** The timing scout and both graphics scouts assumed
   blocks several pixels wide (for example "8 px by 2 rows", "24 columns",
   "6.07 steps/s"). The rules scout re-rendered two complete DOSBox rounds
   from a one-pixel grid with **0 differing pixels outside the HUD text**.
   **The rules scout wins.** Every rate, box and table that the other scouts
   priced on bigger cells is re-derived here. Their mechanisms survive:
   gfxwin's lock protocol and strip blits, gfxfs's id canvas and per-adapter
   presenters, timing's clock, FIFO and harness.
2. **The occlusion neighbourhood.** gfxwin and gfxfs both proved a constant
   table of occluders to redraw (9 to 36 pieces). On the measured grid every
   cell owns one screen column, so the neighbourhood collapses to the sight
   diagonal and redraws vanish (§5). gfxwin's general proof is kept as the
   reason a bigger-cell variant would still be bounded, and nothing more.
3. **What the windowed renderer keeps.** gfxwin wanted a shadow in each
   display's own bit format. gfxfs wanted one id canvas for every surface.
   **The id canvas wins.** It is 32 KB against gfxwin's 53 KB on VGA, and
   it makes a two-display format switch and a window/full-screen toggle a
   *present*, never a recompose. The cost is a strip conversion per box,
   priced in §7.4.
4. **EGA full screen.** gfxfs proposed direct planar register writes inside
   a same-mode bracket and cited the Video Player. The Video Player's direct
   planar writes are in Mode X, a **foreign** mode. SPEC.md §53.7 grants the
   card's registers to the app *inside a foreign mode* and grants the drawing
   slots to a bracket that never sets one. Nothing in the tree writes the
   desktop's own EGA planes behind the kernel. **So EGA full screen uses the
   kernel's `gfx_blitp` in a same-mode bracket** (Paint and Tracker are the
   same-mode precedents), which is legal by §53.7's letter. That is the
   windowed colour presenter at a 640x350 geometry, without the windowed lock
   tax. Direct set/reset spans would be about 20 times cheaper per step, and
   are recorded as an owner decision (§18, item 3), not taken.
5. **The pace.** The timing scout proposed fixed step periods of 9/6/3
   sub-ticks on a coarse grid. On the one-pixel grid, the rules scout's
   fitted iteration model reproduces six measured rates within 5%. It also
   reproduces the original's couplings: more heads slow everyone, a boost
   slows the computer, a dying head speeds the game. **The fitted model wins
   (§4)**, clocked by the timing scout's sub-tick clock.
6. **Section number.** The integration scout found §107 taken on
   `feature/dia`, and that `grep -nE "^## [0-9]+\."` misses `## 103 REDLINE`.
   **§108.** Whichever of the two branches lands second renumbers, and
   `checkdocs` refuses the duplicate.
7. **Sound per step.** The timing scout's tick on every step, at the real
   rate of about 80 to 230 head-steps a second, is a buzz that costs up to 9%
   of the machine. **One soft tick every 4th player cell, at most one per
   frame** (§12).

## 3. The rules and constants (all CONFIRMED or OBSERVED unless marked)

### 3.1 Coordinates, projection, wall

| constant | value | status |
|---|---|---|
| playfield | u = 1..215 (left to right), d = 1..99 (front to back): 21,285 cells | CONFIRMED |
| outline ring | u = 0 or 216, d = 0 or 100: grid cells marked occupied (value 3) | CONFIRMED |
| grid layout | `idx = d*217 + u`, one byte a cell, 217 x 101 = **21,917 bytes**. E/W are +/-1, N/S are +/-217, and the front-diagonal probe step is **-216** | design |
| headings | 0 = N (+d), 1 = E (+u), 2 = S (-d), 3 = W (-u); left = (h+3)&3, right = (h+1)&3 | design |
| screen position of cell (u,d), tower height h | top pixel at `x = u + d`, `y = 174 - d - 24(h-1)` in a 320x200 logical picture | CONFIRMED |
| one step | E: (+1, 0) on screen; N: (+1, -1) | CONFIRMED |
| wall column | 1 top pixel (white) plus 22 face rows; 23 rows in all | CONFIRMED |
| face pattern | one id per (owner, axis of the move that entered the cell); its colour depends only on the logical x parity (§6.2) | CONFIRMED |
| collision | moving into a grid cell that is non-zero (a trail or the outline) is a crash. The rims are paint only, and a head may run over them | CONFIRMED |
| player start | (108, 27) heading N; the start cell is occupied at round start | CONFIRMED |
| computer start | (108, 90), on the back rim; the cell is occupied at round start | CONFIRMED |

### 3.2 The slab (logical picture, level j at offset o = 24(j-1))

All rows below are `y - o`.

| part | rows | columns | surface id |
|---|---|---|---|
| back outline | 74 | 100..316 | EDGE |
| front outline | 174 | 0..216 | EDGE |
| left outline | 74..174 | x = 174 - y | EDGE |
| right outline | 74..174 | x = 390 - y | EDGE |
| back rim | 75..96 | 175-y .. 389-y | RIM |
| left rim | 97..173 | 175-y .. 196-y | RIM |
| floor | 97..173 | 197-y .. 389-y | FLOOR, or FLOORDOT where (x+y) mod 4 = 0 |
| front face | 175..196 | 0..215 (x = 216 stays VOID) | SLABF |
| right face | 75..196 | max(217, 391-y) .. min(316, 412-y) | SLABR |

**A tower of height h** is drawn by `canvas_rebuild(h)`:

1. Clear the canvas to VOID.
2. For each level j = 1..h-1, draw its **lower body** only: the front outline
   row, the right outline's rows, the front face and the right face.
3. Draw level h in full.

The one-row gap between levels stays VOID. Every wall pixel lies inside its
level's top polygon (rows 75..173), as §5.3 shows. So the same call that
draws a new arena also erases the old walls, whether the height is the same
(after a computer win), one higher (a raise), or back to 1 (a reset). At
h = 4 the back outline is at y = 2, and the HUD corners never meet the
picture (§9.3).

### 3.3 A round

1. `canvas_rebuild(h)` is presented with the sweep (§9.4).
2. Place the player and **h computer heads**. At h = 1 there is one head,
   heading E or W by the first RNG byte of the round (bit 0: 0 = E, 1 = W).
   At h = 2..4 the heads are W, E, S, N, taking the first h. They are moved
   in that index order.
3. The round runs in **iterations** (§4.1). Each iteration:
   - **Player first.** Apply F10/F9 to the speed level. Then `acc += p`, and
     while `acc >= 6`: `acc -= 6`, pop at most one turn from the FIFO,
     apply it, and move one cell. If that move crashes, the player is dead
     and **the computer wins now**: the computer heads do not move in this
     iteration.
   - **Then each live computer head, in index order.** Draw one RNG byte,
     run the AI (§10), and move one cell. A crash kills that head; it stops
     and its wall stays.
   - If every computer head is dead, **the player wins**.
   - A contested cell goes to whoever moved into it first in this order.
4. Cells are marked in the grid and drawn into the canvas as they are
   entered (§5).

### 3.4 Score, losses in a row, and the tower

- The winner of a round scores 1. Scores are cumulative for the game, and
  there is no target score (OBSERVED: 10-9).
- **Three losses in a row end the game** (OBSERVED, twice). A win resets the
  count.
- **The tower rule** (OBSERVED, fits all 22 rounds):
  - a player win raises the tower **only if the previous round was also a
    player win**;
  - h + 1 = 5 resets it to 1;
  - a loss never lowers it;
  - a new game starts at h = 1 with "previous round" set to none.

  After a reset, "previous round was a win" stays true, as the rule says.
  The footage cannot show whether that is so, because its next round was a
  loss. SPEC.md §108 records the brief's simpler rule ("every win raises")
  and the six rounds of footage that refute it. This is the anomaly the
  brief asked us to resolve.

## 4. Pace: the iteration model, the clock and the governor

### 4.1 Virtual time

An iteration is charged its fitted 5150 cost, in units of
**u = 1/768 of a BIOS tick (71.52 us)**:

```
cost(iteration) = A + B * (live heads at the iteration's start)
                    + C * (player cells moved in it)
```

| tier | A | B | C | pace relative to the 5150 footage |
|---|---|---|---|---|
| 1 / A **Rookie** | 36 | 178 | 62 | x1 (2.57, 12.73, 4.43 ms: the fitted figures) |
| 2 / B **Veteran** | 27 | 134 | 47 | x4/3 (our invention) |
| 3 / C **Lunatic** | 18 | 89 | 31 | x2 (our invention) |

Player speed `p` is in sixths of a cell per iteration:

- normal is 2;
- each F10 (or Up) keystroke adds 1, **including typematic repeats**, so a
  held key ramps it as in the footage, up to 9;
- F9 (or Down) returns it to 2.

The resulting rates, at **Rookie** (computer cells/s per head, player
cells/s):

| h | p = 2 (normal) | p = 9 (full boost) | head-steps/s |
|---|---|---|---|
| 1 | 59.6 / 19.9 | 45.5 / 68.3 | 79 / 114 |
| 2 | 33.9 / 11.3 | 28.8 / 43.2 | 79 / 101 |
| 3 | 23.7 / 7.9 | 21.1 / 31.6 | 79 / 95 |
| 4 | 18.2 / 6.1 | 16.6 / 24.9 | 79 / 91 |

The footage measured 60/20 at h1 and 34/12 at h2, which this matches. At
Lunatic every rate doubles: the worst case is **228 head-steps/s** (h1, full
boost). Veteran's worst is 151 (h1, full boost). `isotronsim.py` prints the
whole 3 x 4 x 2 table. Every budget in §16 is sized from that.

### 4.2 The clock

`now`, in u:

- **Windowed** (worker): `now = GET_TICKS * 768`, read at each wake. The
  worker sleeps one tick (`OSAPI_TASK_SLEEP` AX = 1), on Dot Delirium's
  `dd_worker` shape.
- **Full screen** (`FSXF_FASTTICK`): `now = ticks * 768 + phase * 256`.
  `phase` counts `FSXW_FRAME` returns since `[ticks]` last moved, capped at 2,
  and resets to 0 when the tick moves. This is the timing scout's clock.
  `apps/os88pit.inc` is **not** used inside the bracket (timing scout §3).
- The counters are 32-bit and compared by subtraction, so they are
  wrap-safe.

### 4.3 The governor

```
while (V <= now) and not round_over:  iterate(); V += cost
if now - V > 768:  V = now - 768      ; a stall owes at most one tick
```

- A stall drops the time it owes and never teleports a head. Every move
  still tests one cell.
- `V = now` at round start, at resume from pause, and on every
  windowed/full-screen toggle, so no burst follows any of them.
- Presentation is per head per wake (windowed) or per step (full screen),
  never per screen (§7, §8). **An overloaded machine therefore loses frame
  rate and keeps the game's pace.** This is the speed argument: the logic
  is a few thousand cycles a step, and the game holds the pace if the CPU
  can run the logic.
- The **windowed presentation governor**: if a wake's lock hold exceeds
  45 ms, present only every second wake. Steps owed in the skipped wake
  merge into the next union box. After 32 wakes under 30 ms, return to
  every wake. The logic still runs at every wake.
- `it_tflags` bit 0 (FAST) is the harness override: run iterations without
  the clock, for the logic rows (§14).

### 4.4 Between rounds (on `GET_TICKS`, the same in both modes)

| phase | length | notes |
|---|---|---|
| CRASH | 0.8 s | crash sweep (§12). The score overlay changes 0.25 s after the crash (OBSERVED) |
| WAIT | until Space or Enter | OBSERVED: the original waited for the space bar |
| SWEEP | target 1.5 s, ceiling **2.2 s** (the original's measured time) | §9.4 |
| READY | 0.5 s | three beeps (silent options: a silent 0.5 s) |
| GAMEOVER | 3.5 s, or Space | the message overlay (§9.3), then the title |

## 5. Occlusion: the bound, and its proof (goes to SPEC.md §108 verbatim)

**Lemma 1 (one cell, one column).** Cell (u,d) is drawn only in screen column
`x = u + d`. Two cells share a column exactly when they lie on one sight
diagonal (u+k, d-k). The one with the larger k is k rows lower and nearer
the viewer.

**Lemma 2 (the visible span).** Given the occupied set, including the
outline ring, cell c's visible pixels are column x(c), rows
`[y_c, y_c + min(k*, 23))`. Here k* is the least k in 1..22 for which
(u+k, d-k) is occupied, and 23 when no such k exists. The front and right
edge clips are this same rule: the outline cell met on the diagonal is the
front edge (k = d) or the right edge (k = 216 - u), and its top row is
exactly the first clipped row. **No separate clip arithmetic exists.**

**Theorem (one span per step).** Entering cell c and writing its span
(top-pixel id at y_c, face id below, `min(k*, 23)` rows in all) produces
exactly the canvas a full painter's repaint of the new occupied set
produces. Cases:

- **Cells in front of c, and every other column.** Their visible spans do
  not depend on c, and nothing of theirs is written.
- **Cells behind c, at (u-j, d+j).** Each one's old visible span ended at
  `y_c - j + k_old`. Every occupied cell between it and c is in front of it,
  so `k_old = j + k*` whenever no cell between them was occupied. Its span
  therefore ended exactly at `y_c + k*`, which is where c's span ends. Rows
  `y_c .. y_c + k* - 1` were its pixels or the floor's, and they are now
  c's. A cell with an occupied cell between itself and c had no pixel in
  that range.
- **The floor and rims beneath c** are simply overwritten.

**The bound.** A step makes at most **22 grid probes** and writes **one
column of at most 23 rows**. It **redraws no previously drawn cell**, so
drawing order does not matter. The brief asked us to "redraw the occupied
cells in front that overlap". On this grid that set is always empty,
because clipping the new span is the equivalent and cheaper operation.

**What is counted (CLAUDE.md rule 4):**

- `it_nprobe`: probes made in the last step, never more than 22;
  `it_maxprobe` is its maximum over the round;
- `it_span`: rows written in the last step, never more than 23; `it_maxspan`
  is its maximum;
- `it_ncol`: canvas columns touched in the last step, always 1;
- `it_cwrite`: canvas bytes changed by the step. These must lie inside the
  one column, which `isotronsim.py` checks against a full repaint **after
  every step**.

The negative controls: a probe limit of 21, and a span of 23 rows
regardless of k*. Both must break the canvas equality on a fixture that has
a cell exactly 22 or 1 diagonals in front.

### 5.3 Why every wall pixel lies inside its level's top polygon

The top pixel of (u,d) is at x = u+d, y = 174-d (offset 0). Its distance
from the left edge 175-y is u-1, which is at least 0. The right edge is the
probe's clip (Lemma 2). Face rows run down to at most 173, the front clip.
The top row of a wall is 174-99 = 75 or lower, so no wall pixel is above the
back outline.

## 6. The canvas (adapter-independent; the picture lives here once)

### 6.1 Format

- 320 x 200 pixels at 4 bits a pixel, **high nibble = even x**: 160 bytes a
  row, **32,000 bytes**, at offset 0 of the instance claim (§13).
- **A nibble is a surface id, not a colour.** The presenter's look-up table
  turns an id into colour or pattern, given the logical x parity and, on
  1bpp, the device row parity.
- The canvas holds **only the picture: no text.** Text is an overlay that
  each presenter draws itself (§9.3). The canvas comparison in the tests is
  therefore exact, and the windowed text is the desktop's crisp `font_run`.

| id | surface | id | surface |
|---|---|---|---|
| 0 | VOID | 8 | PSIDE (player, N/S move) |
| 1 | FLOOR | 9 | PTOP (player top pixel) |
| 2 | FLOORDOT | 10 | CFRONT (computer, E/W move) |
| 3 | RIM | 11 | CSIDE (computer, N/S move) |
| 4 | EDGE (outline) | 12 | CTOP (computer top pixel) |
| 5 | SLABF (front face) | 13 | LOGO face (title letters) |
| 6 | SLABR (right face) | 14 | LOGO top |
| 7 | PFRONT (player, E/W move) | 15 | FLASH (crash highlight, presenter-only) |

### 6.2 Looks (each a data table; the 1bpp and 16-colour ones must be LOOKED at, §17)

Entries are written ink for even x / ink for odd x.

| id | CGA 320 pal 1 low (the original's) | 16-colour "isoTron" (VGA 0Dh, EGA, windowed colour) | 1bpp pair per logical px (even dev row / odd dev row), by x parity |
|---|---|---|---|
| VOID, FLOOR | 0 / 0 | 0 / 0 | 00 / 00 |
| FLOORDOT | 1 (cyan) | 1 blue | 10 / 00 |
| RIM | 2 / 2 (magenta) | 5 / 5 magenta | 11 / 00 (horizontal stripes) |
| EDGE | 3 / 3 (grey) | 15 / 15 white | 11 / 11 |
| SLABF | 0 / 3 (stripes) | 8 / 7 grey stripes | 10 / 01 (checker) |
| SLABR | 0 / 3 | 0 / 8 dark stripes | 10 / 00 |
| PFRONT | 1 / 3 (cyan and white: the footage's periwinkle, INFERRED) | 11 / 11 light cyan | 11 / 11 |
| PSIDE | 1 / 1 (solid cyan, CONFIRMED) | 3 / 3 cyan | 11 / 10 |
| PTOP | 3 | 15 | 00 (a black notch over the lit face) |
| CFRONT | 0 / 2 (stripes, CONFIRMED) | 5 / 13 magenta stripes | 00 / 11 by x parity (vertical stripes, 4 dev px period) |
| CSIDE | 2 / 0 (CONFIRMED) | 13 / 5 | 11 / 00 by x parity |
| CTOP | 3 | 15 | 11 |
| LOGO face / top | 1 / 3 | 14 yellow / 15 | 11 / 11 |

- The **C** key cycles the looks on colour surfaces. On CGA it toggles low
  and high intensity (`int 10h AH=0Bh`, never port 3D9h; Skies'
  `cs_cga_pal` is the precedent). On VGA and EGA, full screen and windowed,
  it switches between the 16-colour **isoTron** look and **Classic**, the
  CGA table drawn in 0/3/5/7.
- A look change is a LUT rebuild and a full present, never a redraw of the
  game.
- `tools/isotronsim.py` carries the same tables, and the tests compare the
  glass against them.

### 6.3 The step's canvas write

For each head that enters cell c:

1. Mark the grid.
2. Probe the diagonal: `cmp byte [es:bx - 216*k], 0` with an unrolled
   displacement, at most 22 of them.
3. Write `min(k*, 23)` nibbles down column x at a 160-byte stride, the top
   id first.

The nibble mask and the two ids are fixed for the whole step, so the loop
is about 50 cycles a row. Then it calls the live surface's **span hook**
(§7.3, §8.2).

## 7. The windowed renderer

### 7.1 Geometry

- **Horizontal scale 2, on every adapter.** All are 640 or 720 wide.
  Logical columns 0..316 (the right face ends at 316) become **634 device
  pixels**. On a 640 desktop (638 content) the picture sits at content
  offset 0. On Hercules (718) it sits at offset 40. §11.94 snaps the content
  origin to a multiple of 8, so device byte = logical x / 4, and the
  pattern phase is absolute.
- **Vertical: a row map.** `Hc = min(content_h, 400)`, and device row r
  shows logical row `s(r) = (r * 200) / Hc` (point sampling; `s` is
  non-decreasing). The table is at most 400 words, rebuilt on
  `OSAPI_WM_ONRESIZE`. A logical span `[a, b)` is the contiguous device run
  `{r : a <= s(r) < b}`, so the span hook stays one run.

  | adapter | content box (Dot Delirium's measured maxima) | Hc | scale | wall face |
  |---|---|---|---|---|
  | VGA | 638 x ~416 | 400 | 2.0 | 44 rows |
  | EGA | 638 x ~286 | 286 | 1.43 | 31 |
  | Hercules | 718 x ~284 | 284 | 1.42 | 31 |
  | CGA 640x200 | 638 x ~136 | 136 | 0.68 | 15 (rows dropped, §17) |

- `OS88_PREFER` asks VGA 638x420, Hercules 718x300 and CGA 638x200. The
  kernel clamps, and the box that comes back is what the map is cut from.
  The window is not resizable (Dot Delirium §93.4.1), but `ONRESIZE` stays
  hooked.
- Below **Hc = 120**, or a content width under 634, the game refuses in
  words: "Window too small."

### 7.2 One wake (the worker) - Dot Delirium's `dd_render` lock protocol

```
TASK_SLEEP 1
OSAPI_GFX_LOCK; ES = KERNEL_SEG
  run the governor (§4.3): iterations -> grid, canvas, and per head a DIRTY BOX
  (the union of its spans this wake, in logical px; heads whose boxes touch merge)
  if the window is visible: CLIP_SET (CF = nothing shows);
     colour only: OBSERVED? -> CLIP_CLEAR when nothing is over the window
     present each dirty box (7.4)
  sound requests (§12), after the measured work
OSAPI_GFX_UNLOCK
```

- **Every canvas and grid write happens inside a lock hold.** A `W_PAINT`
  on the UI task therefore never sees a half-step, and `fsx_run`, which is
  entered from a UI callback holding the lock, never interrupts one.
- A hidden window still runs the logic. It also pauses on focus loss (§11).
- **`W_PAINT`** sets `OSAPI_WM_OWNBG`, presents the damaged rect from the
  canvas (rounded out to device bytes) and redraws the overlay text in it.
- **During a SWEEP** a paint presents only the bands already revealed
  (`it_swept`). The rest is owed to the sweep, as Dot Delirium learned
  (§93.5.17).

### 7.3 Surface choice, every wake

- `OSAPI_WM_DISPLAY` (DH = bpp), then a **`gfx_blitp` probe** (DI bit 15) on
  the content rect. That probe is the only correct "wholly on a colour
  display, no straddle?" question (§5.4.3.2).
- Colour if the probe says yes; otherwise **mono**. A colour window whose
  claim, kernel (`NOPLANE`) or a `gfx_blitp` refusal says no also falls back
  to mono, with `OSAPI_GFX_BLIT1_PEN` (ink `CLCYAN`, ground `CBLACK`).
- A format change is a **full present** by the worker, never by a paint
  (PIXELSTEIN's `px_back_ck` rule, §97.14). The canvas does not change.

### 7.4 Presenting a box

**Mono (CGA, Hercules, fallback).**

1. Round the box out to whole device bytes (4 logical px).
2. For each device row r: read canvas row s(r), and turn each canvas byte
   (2 logical px) into a device nibble through a 2 x 256 LUT indexed by
   device row parity.
3. Write the nibbles into a strip.
4. One `OSAPI_GFX_BLIT1` per box (at most 255 rows a call), **at an x ON
   the byte grid**. The content box is not on it (x = 1 on CGA and VGA, 3 on
   Hercules), and SPEC.md 5.4.2.8 reads an x off the grid as a head mask -
   the strip's first byte lands on the byte column holding x and its first
   x & 7 pixels are not drawn; `kern_small` refuses such an x outright. Wave
   1's debug view shipped exactly that and lost the arena's u = 0 wall on
   every adapter, so the picture's origin is the content's x rounded UP to a
   multiple of 8 (wave 1's `it_ox`), with the 0..7 columns left of it filled
   once at a paint. `OSAPI_GFX_BLITP` takes the same origin.

**Colour (VGA, EGA).**

1. The same walk, but each canvas byte goes through a 256-entry x 4-plane
   LUT, phase-paired, into a 4-plane strip.
2. The plane step is under 16 KB, so DI bit 14 can walk an armed clip.
3. One `OSAPI_GFX_BLITP` per box.
4. Keep the region and set DI bit 14 when anything covers the window. Use
   `CLIP_CLEAR` and the plane-major fast path (§5.4.3.5) when nothing does
   **and** the box does not reach device x 634 or beyond. Content ends at
   637, so the last byte (632..639) must be walked by the clip.

**The strip** is the claim's tail (§13): 5,120 bytes, so a full present runs
in bands of 16 device rows (80 bytes x 4 planes x 16).

### 7.5 What a wake costs on a 4.77 MHz XT (paper; wave 2 replaces it with MartyPC numbers)

Measured units come from the gfxwin scout:

| unit | cost |
|---|---|
| `gfx_blit1` | 9,000 cycles + 100 a row + 21 a byte |
| `gfx_blitp` fast path | about 5,000 cycles + 400 a row |
| windowed lock tax | 8.6 ms (Dot Delirium, XT-VGA) |
| logic and canvas | 2,100 cycles a head-step typical (§16) |

| case (per wake, 18.2 wakes/s) | heads | head-steps | 1bpp (CGA / Hercules) | VGA / EGA colour |
|---|---|---|---|---|
| Rookie h1, normal | 2 | 4.4 | 8.6 + 2 x 3.1 + 1.9 = **16.7 ms (30%)** | 8.6 + 2 x 6.5 + 1.9 = **23.5 ms (43%)** |
| Lunatic h1, full boost | 2 | 12.5 | 8.6 + 2 x 3.4 + 5.5 = **20.9 ms (38%)** | 8.6 + 2 x 8.0 + 5.5 = **30.1 ms (55%)** |
| Lunatic h4, full boost | 5 | 10.0 | 8.6 + 5 x 3.1 + 4.4 = **28.5 ms (52%)** | 8.6 + 5 x 6.3 + 4.4 = **44.5 ms (81%)** |

- The last colour row is why §4.3's presentation governor exists. It also
  stops a real 8-bit VGA, which is slower than MartyPC's (gfxwin §2), from
  costing the game its pace.
- Box sizes:
  - **1bpp:** a box is 1 to 3 device bytes x 23 to 35 rows. The strip
    conversion is about 50 cycles a byte, so about 3k cycles.
  - **Colour:** 23 to 35 logical rows become 46 to 70 rows at 2x, x 4
    planes. The conversion is about 6k to 9k cycles.

## 8. The full-screen renderers (the FSX bracket, SPEC.md §53)

### 8.1 Mode by the window's display (`OSAPI_FSX_CAPS` DL, §53.7.1; never `OSAPI_VIDEO`)

| display kind | mode | colours | geometry | step present |
|---|---|---|---|---|
| CGA | `FSXM_CGA320` | 4: palette 1, **low intensity** (the 1982 look); C toggles high | 1:1 | direct span: banked RMW, about 1,200 cycles |
| VGA | `FSXM_VGA0D` (320x200x16 planar) | 16: the isoTron look; C gives Classic. BIOS default palette, **no DAC writes** | 1:1 | direct span: set/reset colour, a 1-bit bit mask, a latch RMW a row at stride 40, about 1,000 cycles |
| EGA | **same-mode bracket**, no `fsx_mode` (§2.4) | 16: the same tables | 2x by 1.75 rows = 634 x 350 at `OSAPI_FSX_SURF`'s origin | per tick, through the windowed colour presenter (`gfx_blitp` from strips), without the lock tax |
| Hercules | `FSXM_HERC` (720x348, 4 banks) | 1bpp, the §6.2 pair table by card row parity | 2x by 1.5 rows = 634 x 300, centred at card (40, 24) | direct span: masked RMW with a pattern a card row, about 1,800 cycles |
| anything else | - | - | - | the Full Screen item greys with its reason (§47) |

- **The row maps:** EGA `m(y) = (y*7)/4`. Hercules: logical row y owns 2
  card rows if y is even and 1 if odd. A row table of 200 entries holds
  (card offset, count).
- **Rejected:**
  - VGA 13h, which has twice 0Dh's bytes and shares nothing with EGA;
  - Mode X and 12h, which buy nothing (no scrolling, no flipping);
  - CGA 160x100 retimed text, where a cell would be half a column;
  - CGA 640x200 composite artifact colour, which the brief says not to
    match;
  - EGA through `FSXM_CGA320`. It is the documented fallback when
    `OSAPI_FSX_SURF` does not answer a 640x350 rect, and it shows 4
    colours.

### 8.2 One bracket frame (`FSXF_FASTTICK`, no worker, §93's shape: "two loops driving one screen is two writers")

```
drain int 16h -> FIFO / commands (§11); os88alt_edge
run the governor -> each step: grid, canvas, SPAN HOOK (direct VRAM; EGA: extend the dirty box)
EGA only, once a tick: present the dirty boxes through gfx_blitp
sound (§12)
FSXW_FRAME
```

### 8.3 Entry, exit and full presents

**Entry:**

1. Wait for the activating key's release (Excitebike `xb_fullscreen
   .release`), because the BIOS mode set masks the break.
2. Run `OS88_ALTENTER_SEED`.
3. `fsx_mode` (not on EGA).
4. Set the palette (CGA through `int 10h AH=0Bh`).
5. Build the LUTs and row table.
6. Make a **full present** of the canvas, plus the overlay text.
7. `V = now`.

**Exit:** return from the proc. On every adapter but EGA, §53.6's mode set
restores the card. On EGA nothing was programmed. The window's `W_PAINT`
then presents the canvas windowed.

**Full presents** happen on entry, on a look change, and in the sweep:

| surface | method | estimate |
|---|---|---|
| CGA | a 2 x 256 LUT, about 70 cycles an output byte | 16,000 bytes, about **0.23 s** |
| VGA 0Dh | chunky-to-planar through a 4 x 256 LUT, 4 map-mask passes | 32,000 plane bytes, about **0.57 s** |
| Hercules | the pair LUT x 1.5 rows | about **0.3 s** |
| EGA | strips through `gfx_blitp`, 350 rows x 80 bytes x 4 planes at about 6.3 us a plane-byte | about **1.0 to 1.3 s on an XT** (modelled: MartyPC has no EGA) |

All are under the 2.2 s ceiling. An EGA is usually in an AT, about 4 times
faster.

### 8.4 Text in full screen

Text comes from `OSAPI_FONT_GLYPHS` (legal in a bracket, §53.7): 8x8 cells,
opaque, **each device byte written once**.

- **CGA:** 2 bytes a glyph row, ink and ground through the LUT.
- **0Dh:** per plane, `(glyph AND inkbit) OR (NOT glyph AND groundbit)`,
  written once with that plane's map mask.
- **Hercules and EGA:** scaled with their row maps (EGA through a 1bpp strip
  and `gfx_blit1` with a pen).

### 8.5 Register hygiene

- On 0Dh, every span routine leaves the GC registers in write mode 0 with
  set/reset enabled. The bracket's exit mode set restores everything.
- No palette, AC or DAC register is touched on EGA.
- No port 3D9h write anywhere: an EGA or VGA running mode 4 has none.

### 8.6 The toggle (both directions, any time, mid-round included)

- **F / f, Alt+Enter, and Game > Full Screen** (§11.2.1, binding).
- **Esc:**

  | where | effect |
  |---|---|
  | in a round, full screen | ends the game; the title stays full screen |
  | at the title, full screen | leaves full screen |
  | windowed, in a round | ends the game |
  | windowed, at the title | does nothing |

  This is §11.2.1's rule that Esc "keeps meaning cancel first".
- A toggle changes no game state:
  - the iteration, RNG, checksum, grid and canvas are bit-identical across
    it, and the `isotrontoggle` row asserts that;
  - `V = now` on both sides;
  - the FIFO and the key-seen tracking are reset (§11).
- **Windowed to full screen:** `W_ONKEY` (the UI task, lock held) calls
  `OSAPI_FSX_RUN` with `FSXF_FASTTICK`. The worker cannot be mid-step,
  because it mutates state only under the lock.
- **Full screen to windowed:** the bracket proc returns, `wm_paint_all`
  repaints, and the worker resumes on its next wake with `V = now`.

## 9. Screens, HUD and the sweep (our own wording)

### 9.1 The title (canvas plus overlay text)

- The word **isoTron** in extruded block letters, drawn by the wall
  renderer itself:
  - each letter is a few straight strokes on a small virtual grid, from a
    stroke table in `itcanvas.inc` that we typed;
  - each stroke is a run of wall columns with the LOGO ids, so the title is
    3D in exactly the game's projection;
  - lower-case "iso", capital "T", lower-case "ron".
- Overlay text, ours:

```
        A light-wall duel in three dimensions.

  Turn left ...... Ins  Left  Z      Faster .... F10  Up
  Turn right ..... Del  Right X      Normal .... F9   Down
  Pause .......... P                 Sound ..... M
  Colours ........ C                 Full screen F
  End game ....... Esc               Continue .. Space

          1  Rookie     2  Veteran     3  Lunatic      with sound
          A  Rookie     B  Veteran     C  Lunatic      silent
```

There is no copyright line, no publisher name, and no year.

### 9.2 Menus (windowed)

- The app-name cell carries **About isoTron** through `OSAPI_ABOUT_SET`
  (`%define OS88UI_ABOUT`, `os88ui.inc`'s standard card). This is required
  (CLAUDE.md, §12.2), and there is no Help menu.
- **Game** menu:

  | item | key |
  |---|---|
  | Rookie | 1 |
  | Veteran | 2 |
  | Lunatic | 3 |
  | - | |
  | Sound (check mark) | M |
  | Colours | C (greyed on a mono surface, with its reason) |
  | Full Screen | F (greyed when `FSX_CAPS` offers nothing, with its reason) |
  | Pause | P |
  | - | |
  | End Game | Esc |

- The menu numbers start a game **with sound on** (the letters' "silent" is
  the Sound check, unticked).

### 9.3 The HUD and messages (overlay text, never in the canvas)

| element | full screen (logical, 8x8 cells) | windowed (device, `font_run`) |
|---|---|---|
| `PLAYER n` | row 0, col 2 | top-left of the content, one line |
| `COMPUTER n` | row 24, right-aligned to col 38 | bottom-right, one line |
| game over: **`THREE LOSSES IN A ROW. GAME OVER.`** | row 23, col 0, across the front face | the same place through the row map |
| pause: **`PAUSED - PRESS P`** | centred over the void above the slab | the same |

- The corners never meet the picture at any height. At h = 4 the left edge
  at rows 0..15 is at x >= 87, and the HUD ends at x = 80. At rows 184..199
  the right face reaches x <= 228, and `COMPUTER n` starts at x = 232.
- A full windowed present is the canvas **minus** the overlay rects (at
  most 2 boxes a rect), and then `font_run`. Each pixel is written once.

### 9.4 The sweep (the original's draw order, kept as the visual signature)

1. Rebuild the canvas **without** the outline, and present the rim, the
   right face and the lower tower bands at once.
2. Present the front face left to right in 8-logical-px vertical bands,
   about 0.4 s.
3. Present the floor in 8-px bands, about 0.9 s.
4. Write the outline into the canvas and present its four edges last (the
   diagonals as 8x8 boxes).
5. Pace each stage on `GET_TICKS` so the total is about **1.5 s** where the
   surface is fast. A slow surface (EGA on an XT, windowed VGA) runs flat
   out, and is still under the 2.2 s ceiling.

Until a band is presented, the old picture shows there: the new slab is
"drawn over the old picture", as in the footage. The pace is measured on
MartyPC (wave 4).

## 10. The computer (seeded, deterministic, difficulty-scaled)

**RNG.**

```
s = s * 25173 + 13849 (mod 65536)
r = s >> 8
```

That is one `mul` on the 8088. It is seeded per game from `GET_TICKS`, or
from the harness word `it_tseed`. **Every computer head-move draws exactly
one r** before it decides, so the stream depends only on the move count.
The h = 1 heading draw at round start (§3.3) is the only other draw.
`OSAPI_RAND` is not used: its seed is a kernel global any package can stir.

**`run(dir)`** is the number of free cells from the head in dir, capped
at 255.

**Per head, in order; the first rule that turns ends the decision:**

1. **Opening.** The *opening head* is the head at h = 1, and the E head at
   h >= 2. If it has no turn yet, heads E or W, has `leg == 32`, and
   `run(S) > LOOK`: turn S.
2. **Blocked.** If `run(ahead) <= LOOK`: turn to the side with the larger
   run. On a tie, `r & 1` decides (0 = left). If both runs are 0, go
   straight and crash.
3. **U-turn pending.** If a wander armed one, decrement it. At 0, turn to
   `uside` if `run(uside) > 16`.
4. If `leg < 8`, go straight.
5. **Intercept**, if `r < ICPT` and the player is alive:
   - for side in (left, right), skipping any side parallel to the player's
     heading, find the crossing point M of the head's new ray and the
     player's forward ray;
   - `t` = head cells to M and `k` = player cells to M, both > 0;
   - player iterations `pi = (k*6 + p - 1) / p`;
   - turn if `t + 2 < pi <= 120` and `run(side) >= t`.
6. **Wander.** If `(r XOR 0A5h) < WANDER`: side = left if `r & 1`, else
   right. If `run(side) > 16`, turn and arm a U-turn:
   `upend = 8 + (r & 7)`, `uside` = the same side.
7. Otherwise go straight. A turn sets `leg = 0`, and each move increments
   it.

**Difficulty parameters** (Rookie's are the footage's reading; the rest are
ours):

| tier | LOOK | ICPT (/256) | WANDER (/256) | pace (§4.1) |
|---|---|---|---|---|
| Rookie | 1 | 64 | 6 | x1 |
| Veteran | 3 | 96 | 4 | x4/3 |
| Lunatic | 6 | 128 | 2 | x2 |

`isotronsim.py`'s `ai_move` is **normative**. SPEC.md §108 transcribes it,
and the guest equals it (§14.2).

**The test bot** (harness only, `it_tflags` bit 1) plays the player:

- one F10 at iteration 0;
- when `run(ahead) <= 3`, push a turn toward the larger side (left on a
  tie) into the FIFO;
- its own LCG, seeded `it_tseed XOR 5A5Ah`, unused by the rule above and
  reserved.

The sim carries the same bot. It is what lets a MartyPC row play whole
seeded games.

## 11. Input

- **Windowed:** keystrokes arrive in `W_ONKEY` (UI task). Turns go into a
  **3-entry single-producer, single-consumer byte ring**: the producer
  writes the slot and then the head, and the consumer reads the slot and
  then advances the tail. There is no lock, because the worker may not take
  one. Commands are flags the worker reads.
- **Full screen:** the frame drains `int 16h` completely each frame, as Dot
  Delirium does and §67.11.2's lesson demands.
- **Key map.** Extended keys are tested by **AH**; never case-fold before
  testing AL = 0 (§67.11.1). Letters are matched by ASCII. Keypad Ins with
  NumLock on is AL = '0', AH = 52h.

  | action | keys |
  |---|---|
  | turn left | Ins (52h), Left (4Bh), Z |
  | turn right | Del (53h), Right (4Dh), X |
  | boost +1 | F10 (44h), Up (48h); repeats count |
  | boost off | F9 (43h), Down (50h) |
  | pause | P |
  | sound | M |
  | look | C |
  | toggle | F / f, Alt+Enter |
  | end game / leave full screen | Esc (§8.6) |
  | continue | Space, Enter |
  | at the title | 1 2 3 (sound), A B C (silent) |

- **The typematic filter, turns only** (the timing scout's rule). A turn
  keystroke counts if:
  1. `OSAPI_KEY_DOWN` saw its key **up** at any sample since its last
     keystroke (sampled each frame and in `W_ONKEY`); or
  2. the last one was accepted less than 6 ticks ago. That window is under
     any PC's 500 ms typematic delay, so this is a real fast re-press.

  Otherwise it is a repeat, and it is dropped. The tracking resets at round
  start and at every toggle. `OS88_ALTENTER_ARM` in the entry proc arms
  `KEY_DOWN`.
- **Focus.** Windowed play auto-pauses when the window loses the front
  (`dd_focus_ck`: `OSAPI_WM_TOP` plus `OSAPI_MENU_OWNER`). It resumes on P
  or Space. In the bracket, we are the machine.

## 12. Audio (`OSAPI_SND_TONE` only; worker-safe, §34.3)

| event | sound | calls | priority |
|---|---|---|---|
| wall tick (sound on) | 110 Hz (165 Hz while boosted), CX = 1 lease. Every 4th player cell, **at most one per frame**. Full screen: explicit off on the next frame. Windowed: the lease expires at the tick boundary | at most 2 per 4 player cells | 40h |
| crash | falling sweep, 1,400 Hz x15/16 a step to about 110 Hz, 15 entries at one entry per tick (0.8 s), then off | 16 | 60h |
| round start | 440, 440, 880 Hz, 2 ticks on and 1 off | 6 | 50h |
| tower rises | 523, 659, 784 Hz up, during the sweep | 4 | 50h |
| game over | 523, 392, 330, 262 Hz, 4 ticks each, under the message | 5 | 50h |

- **Every off goes out at 60h**: an off obeys the priority compare (§34.3).
- Silence on: pause, focus loss, Esc, every toggle, close. Teardown also
  catches it.
- **M** toggles sound in game, as 1942, Excitebike and Cyclone do.
- Sound requests run after the measured work and before the wait
  (Excitebike §102.5), behind an `it_tonecall equ $` mark and an `it_ntone`
  counter.
- **Cost:** 1,997 cycles a call (Set 146). The worst case is 2 calls x 34
  ticks a second, about 136k cycles, which is **2.9%** of the XT.

## 13. Memory

**The instance claim** is one `OSAPI_MEM_CLAIM`, pinned (§66 default), with
ES = its segment for the step:

| offset | bytes | what |
|---|---|---|
| 0 | 32,000 | canvas (§6) |
| 32,000 | 21,917 | grid (§3.1) |
| 53,917 | 5,120 | strip (§7.4), 16-byte aligned |
| **total** | **59,037** (3,690 paragraphs) | `%error` if the layout passes 65,535 |

**Package image and bss** (pinned in SPEC after wave 2):

| item | budget |
|---|---|
| image (code and tables) | 16 KB or less, about 10 KB after lz4 |
| bss: LUTs (CGA 512 B, 1bpp pairs 1 KB, planar 4 KB, row maps 400 + 200 + 200 words) | 8 KB or less |
| bss: instance state, the FIFO, harness bytes | included above |
| stack | the class covering the worker's deepest chain (`t_stkclass` reads the include tree) |

- About 83 KB a running instance. A second instance claims its own, or
  refuses in words.
- **kern_small** has a 52.5 KB arena, so the claim refuses, in words:
  `isoTron needs 58 KB of free memory; this machine has N KB.` That is
  §24.5's "a package that merely wants heap can refuse itself". No bracket
  has been entered, so it is never a black screen. If `t_smallreq` demands
  otherwise, wave 6 adds the `SMALLOMIT_GAMES` line with a `FORBIDDEN` row
  (the pxstein model).

## 14. Testing

### 14.1 The host reference model `tools/isotronsim.py` (standard library only)

**Modules:**

- `Rng`, `Grid`, `Round`, `Game`: the rules, tower and score, from §3;
- `ai_move`: §10, normative;
- `Pace`: the virtual cost and governor of §4, in iterations;
- `Canvas`: `rebuild(h)`, `step_span`, `full_repaint(grid)`, the title
  logo;
- the presenters:

  | presenter | output |
  |---|---|
  | `cga320` | 16,000 VRAM bytes, banked |
  | `vga0d` | 4 planes, plus RGB through the default palette |
  | `herc` | 32 KB of 4 banks |
  | `ega` | 640x350 RGB |
  | `win1(Hc)`, `win4(Hc)` | the windowed device picture |

  Each supports a box or the whole picture.

**CLI:**

```
--selfcheck
--game SEED --tier T [--bot] [--inputs FILE] --json
--canvas ... --out F
--present SURF ... --out F
--png F   (zlib + struct)
```

**`--selfcheck`** (deterministic):

1. Stored hashes for six seeded bot games: Rookie, Veteran and Lunatic,
   each covering a tower 1-2-3-4-1 and a three-loss game over.
2. **Incremental equals full repaint after every step**, for 30 rounds.
3. The §5 fixtures, with k* = 1, 22 and 23.
4. The presenters' LUTs against a per-pixel reference rendering of each
   look.
5. **Negative controls that must FAIL:**
   - the probe limit at 21;
   - an unclipped span;
   - the tower rule "every win raises";
   - the RNG constant 25173 changed to 25175;
   - the player moving after the computer.

### 14.2 Guest harness (bss bytes, always assembled; Excitebike's shape)

**Inputs:**

| byte | meaning |
|---|---|
| `it_tseed` | seed |
| `it_tdiff` | tier |
| `it_tflags` | bit 0 FAST (no clock), bit 1 BOT, bit 2 NOFLOW (straight into a game at `it_tdiff`, with no waits and no sweep pacing), bit 3 HOLD (set `it_tpause = 1` and stop at round end) |
| `it_tinput` | a short (iteration, key) table |

**Outputs:**

| group | fields |
|---|---|
| game state | `it_iter`, `it_round`, `it_score`, `it_h`, `it_lossrow`, `it_prev`, `it_winner`, `it_rng`, `it_ck` |
| bound counters (§5) | `it_nprobe`, `it_maxprobe`, `it_span`, `it_maxspan`, `it_ncol` |
| sound and step count | `it_ntone`, `it_nsteps` |
| windowed geometry | `it_hc` |

`it_ck` is defined as `ck = rol(ck, 1) XOR idx` for every cell placed, in
placement order.

**Marks** (`equ $`): `it_step0` / `it_step1` (one head-step), `it_wake0` /
`it_wake1`, `it_frame_end`, `it_sweep0` / `it_sweep1`, `it_tonecall`.

**Symbols:** bss offsets come by assembling with
`dw label - os88_image_end` appended (the `n1942.py` recipe). The base is
the unpacked image size at +8 of `build/isotron.bin`.

### 14.3 Rows (`tests/suite.py`; budgets MEASURED by the wave that adds the row)

| row | tier | what it asserts | machine |
|---|---|---|---|
| `isotronclean` | **fast** (~0.2 s) | the provenance gate (this plan's section 0, item 3); a `--selftest` injects a token into a temp copy and must fail | host |
| `isotronsim` | soak | `tools/isotronsim.py --selfcheck`, including its negative controls | host |
| `isotronrules` | soak | FAST + BOT seeded games at all 3 tiers. After every round, guest = sim on (winner, `it_iter`, scores, h, `it_lossrow`, `it_rng`, `it_ck`). Asserts a tower 1-2-3-4-1 and a three-loss game over. Negative control: the sim with "every win raises" must disagree | MartyPC `os8088_5150_cga_gla` |
| `isotronbound` | soak | `it_maxprobe` and `it_maxspan` equal the sim's, and are at most 22 and 23; `it_ncol == 1` every step; the canvas (32,000 bytes) equals the sim's after each round | MartyPC |
| `isotronwin` | soak | windowed glass equals sim `win1`/`win4` under the content rect, outside the overlay rects and with the pointer parked off the window, on CGA, Hercules and XT-VGA | MartyPC (3 machines) |
| `isotronfs` | soak | full-screen VRAM or fbuf equals sim `cga320`/`herc`/`vga0d` after a seeded round | MartyPC (3 machines) |
| `isotronega` | soak | windowed and full-screen glass equal sim `win4`/`ega` | QEMU `VIDEO=ega`, a private tree through `tools/os88build.py` |
| `isotrontoggle` | soak | F, Alt+Enter and the menu toggle mid-round, both ways: state bit-identical, glass right on both sides, Esc semantics (§8.6) | MartyPC CGA and VGA |
| `isotrontwodisp` | soak | on Hercules the window is mono; F gives the `FSXM_HERC` bracket; the VGA is untouched; a drag across switches format with one full present | MartyPC `os8088_xt_vga_herc` |
| `isotronperf` | soak, serial | the cycles of a head-step per surface, a wake per windowed surface, and the sweep per surface, minimum of 25. Each within +25% of §16's line **and** under its ceiling | MartyPC (3 machines) |
| `isotronpace` | soak | iterations per guest second within 2% of §4.1 (Rookie h1 and h2, Lunatic h1, both modes); the stall-drop rule | MartyPC |
| `isotroninput` | soak | real keys: double tap = U-turn; a held turn repeats nothing; a held F10 ramps; NumLock Ins; Up and Down; Esc semantics | MartyPC |
| `isotronsound` | soak | `MARTYPC_WAV`: crash sweep falling (Goertzel); silent tiers give silence; M; `inb(61h) & 3 == 0` on pause, Esc and toggle | MartyPC |
| `isotronflow` | soak | the UI driven: launch, press 1, BOT plays, Space continues, the game-over overlay, back to the title; About opens | MartyPC |

- `tests/isotronlib.py` (the guest reader and Game class) is a library and
  goes in `t_registry`'s `UNREGISTERED` with a reason, as pxslib did.
- Every test derives `ROOT` from `__file__`. No absolute paths
  (`t_registry`).

### 14.4 PR pictures

Taken with `tools/shot.py` (QEMU) and MartyPC captures:

- windowed VGA;
- windowed CGA 1bpp;
- windowed Hercules;
- full-screen CGA 320 palette 1;
- full-screen VGA 0Dh;
- full-screen Hercules;
- EGA (QEMU);
- the tower at h = 4.

The brief requires at least one 1bpp shot and one VGA shot.

## 15. Integration (the integration scout's checklist, decided)

**SPEC.md:** `## 108. isoTron — a light-wall duel in a raised 3D arena
(`apps/isotron/isotron.asm`)`. The heading contains "isoTron" as a word, so
`os88index` finds it. It is written before the code and updated each wave.

**Makefile block:** headed `# isoTron (SPEC.md 108)`, with no reference
path in it.

- `$(BUILD)/isotron.bin:` names **every `%include`** (`t_pkgdeps`).
- `.o88` through `$(OS88PKG)`.
- `isotrondisk` builds four geometries (`isotron.img`, `isotron720`,
  `isotron120`, `isotron360`), each with `os88disk.py --verify`. It carries
  `ISOTRON.O88` and `README.TXT` (from `apps/isotron/README.md`).
- **Ship:**
  - `$(BUILD)/isotron.o88` in `APPS_GAMES`, which reaches apps, apps720,
    apps120, games360, allapps and the live media (`t_livefull` PART A);
  - filter it out of `APPS_GAMES_360` with a dated §24.6.1 comment
    (apps360 has 29 KB free; games360 has 98 KB);
  - check apps720 (70 KB free) and `make combo` (`COMBO_DROP` with a reason
    if it does not fit).
- 86Box machines, for LOOKING only:

  | machine | copied from | changes |
  |---|---|---|
  | `vm/xt-isotron` | `xt-excitebike` (VGA XT) | uuid and `fdd_02_fn` only |
  | `vm/xt-isotron-cga` | `xt-pixelstein` (`ibmxt86`, 640 KB) | the same |
  | `vm/xt-isotron-herc` | `xt-pixelstein-herc` | the same |
  | `vm/xt-isotron-ega` | `xt-ega` | the same |

  Targets `xt-isotron*`, all in `.PHONY`.

**Docs and repo files:**

- **README.md:** make lines and the Games paragraph.
- **PERFORMANCE.md:** Sets from **155**. Part 5 gets a row: "isoTron step:
  one column, at most 22 probes, 0 redraws".
- **docs/INDEX.md:** regenerated with `tools/os88index.py` whenever the
  `.bin` rule, the SPEC heading or a newly tracked doc (this plan) changes.
- **CLAUDE.md:** the VM list sentence (optional, as PIXELSTEIN did).
- **Never** edit `readme.txt` (the system disk's README.TXT).

## 16. The per-step cycle budget on a 4.77 MHz XT (the gate the waves are measured against)

**One head-step** (cycles; the first two lines are MEASURED on MartyPC's
5150 after wave 1's review fixes - the paper figures were 150 and 600 /
9,000, under the 8088's fetch floor - and wave 2 and wave 3 replace the rest
with a MartyPC minimum-of-25):

| component | typical | worst | note |
|---|---|---|---|
| move, collision, grid mark, `it_ck` | 470 | 470 | measured: 927 median with wave 1's debug bit, which is 460 of it |
| AI decision (computer head) | 870 / 1,180 | ~7,600 | measured medians, Rookie / Lunatic. `it_run` is 250 + 54 a cell N/S and 228 + 15.6 a cell E/W (`repe scasb`); worst = blocked heading N/S with both E/W sides 215 free (a N/S pair shares one column, at most 98 cells between them) |
| occlusion probes | 200 | 700 | at most 22, unrolled |
| canvas span | 1,100 | 1,300 | at most 23 nibbles, read-modify-write |
| counters and harness | 60 | 60 | |
| **logic + canvas** | **~2,800** | **~10,100** | Lunatic's typical; the 3,000 ceiling below has ~200 to spare |

**Full-screen span hook**, added to each head-step:

| surface | cycles |
|---|---|
| CGA 320 | 1,200 |
| VGA 0Dh | 1,000 |
| Hercules | 1,800 |
| EGA | 0 per step (per-tick boxes, §8.2) |

**Machine load at the worst rate** (228 head-steps/s, Lunatic h1 full
boost), full screen: 228 x (2,800 + 1,800) + 54.6 frames x 1,500 =
**1,131k cycles/s = 24%** of the XT on the slowest surface (Hercules). It is
about 21% on CGA and VGA. (It read 971k / 20% on the paper logic figures.)

**Ceilings the perf row enforces** (each line also at most +25% of its
measured wave value once pinned):

| measurement | ceiling |
|---|---|
| head-step, logic + canvas, typical | 3,000 cycles |
| head-step + full-screen span, CGA / VGA 0Dh / Hercules | 4,500 / 4,000 / 5,000 cycles |
| windowed wake, Rookie h1, 1bpp / VGA | 22 / 30 ms |
| windowed wake, Lunatic h4 full boost, 1bpp / VGA | 35 / 50 ms; above 45 ms the presentation governor must engage |
| sweep, every surface | 2.2 s; target 1.5 s where the surface allows |
| full present on toggle | 1.5 s (the EGA model is exempt: no MartyPC EGA; QEMU checks pictures only) |

## 17. Risks and look questions (look on the glass before calling it done)

1. **The 1bpp patterns** (§6.2) on CGA 640x200 and Hercules, windowed and
   full screen. The two walls, the rims and the slab must be told apart by
   pattern alone (§39.4, §47). They are data, so a fix costs no code.
2. **CGA windowed at 0.68 vertical.** Point sampling drops one row in
   three, so a depth wall's white top edge can skip a pixel. The full-screen
   CGA path is the 1:1 original. If the look fails, the alternative is a
   "top-preserving" sample (a dropped row's EDGE/TOP id wins), which is a
   LUT-side change. Wave 2 decides it by looking.
3. **Windowed VGA at Lunatic h4.** This is 81% on paper. The presentation
   governor holds the pace, and the frame rate drops. Measured in wave 2,
   and re-priced in wave 6 if the governor engages at Rookie.
4. **The EGA full-screen cost on an XT** is modelled only, because there is
   no MartyPC EGA. QEMU checks the pixels; 86Box `xt-isotron-ega` is for
   looking.
5. **§108 numbering** collides if `feature/dia` takes 108 too. `checkdocs`
   catches it.
6. **Fast-tier budget.** The fast tier is at 28.9 s of 30 on clean main. Only
   `isotronclean` (0.2 s) goes in fast; everything else is soak.

## 18. Decisions the owner may revisit (each is one table or one string)

1. Tier names (Rookie / Veteran / Lunatic) and the HUD labels (PLAYER /
   COMPUTER).
2. The 16-colour isoTron look, and whether Classic should be the default
   on VGA and EGA.
3. EGA full screen through direct set/reset spans in a same-mode bracket:
   about 20x cheaper a step, but it reads §53.7's register grant beyond a
   foreign mode. Not taken.
4. "Previous round was a win" surviving a tower reset (§3.4).
5. Typematic filtering on turns (the original almost certainly spun on a
   held key).

## 19. Waves

**Every wave ends with the SHIP step:**

- commit the named paths (never `git add -A`, `-u` or `.`);
- push `game/isotron`;
- **wave 1 creates the PR** (draft) against `main`. Each later wave
  **updates the PR body** with that wave's acceptance results, and adds its
  screenshots when it has them.

**Each wave's acceptance:**

- `make` exits 0, and its output has no `Error` line;
- the fast tier is all green (an overrun under host load is the known
  pre-existing case: check `uptime` and the row results);
- the listed commands pass.

### Wave 1 - skeleton, integration, the rules engine, the reference model

**Scope:**

- `apps/isotron/`:
  - `isotron.asm`: the header `isoTron`, the icon, `OS88_REGION_MOVABLE`,
    `OS88_ALTENTER_ARM`, `OSAPI_ABOUT_SET`, the Game menu (Full Screen
    greyed "Not yet" for now), the claim of §13 with a refusal in words,
    and the worker with its governor;
  - `itrules.inc`: grid, iteration, collision, rounds, scores, losses in a
    row, the tower rule, the RNG and the clock;
  - `itai.inc`: §10 and the test bot;
  - `itinput.inc`: the FIFO and key map. The typematic filter may land in
    wave 5;
  - `ittest.inc`: the harness bytes, counters and marks;
  - `README.md`.
- A **flat top-down debug view**: a 1bpp image of the grid, 1 px a cell,
  presented per wake through `gfx_blit1`. It is deleted in wave 2.
- `tools/isotronsim.py`: rules, AI, pace, `--selfcheck` with the rules
  negative controls. Canvas and presenters come in wave 2.
- Integration:
  - SPEC.md §108: rules, constants, the tower rule with the anomaly, pace
    and AI;
  - the Makefile block, `isotrondisk` in four geometries, `APPS_GAMES` plus
    the apps360 filter;
  - `t_isotron_clean.py` and its fast row;
  - `tests/isotronlib.py` (UNREGISTERED) and `tests/isotron.py` with the
    `isotronrules` row;
  - `isotronsim` in soak;
  - INDEX regenerated, including this plan.

**Acceptance:**

```
make                                              # exit 0, no 'Error'
python3 tests/unit/t_isotron_clean.py             # ok
python3 tests/unit/t_isotron_clean.py --selftest  # the injected token fails
python3 tools/isotronsim.py --selfcheck           # ok, every negative control refused
python3 tools/os88test.py soak -k 'isotronrules'  # guest = sim on every round of 3 tiers
python3 tools/os88index.py --check                # current
make isotrondisk                                  # 4 images, each --verify clean
```

- The PR exists.

### Wave 2 - the canvas, the slab and tower, walls with the occlusion span, every windowed renderer

**Scope:**

- `itcanvas.inc`:
  - `canvas_rebuild(h)`, the step span of §6.3, the title logo strokes;
  - a debug full repaint, used only by tests.
- `itwin.inc`:
  - the row map;
  - mono and colour LUTs, the strips, `BLIT1` and `BLITP`;
  - dirty boxes per head, merged;
  - the surface choice per wake, with the probe;
  - `W_PAINT`, `ONRESIZE`;
  - the HUD overlay through `font_run`;
  - the presentation governor.
- The debug view goes.
- The sim gains `Canvas` and the `win1`/`win4` presenters, the incremental
  equals full repaint selfcheck, the §5 fixtures and their negative
  controls.
- Rows: `isotronbound`, `isotronwin`, `isotronega` (windowed half), and the
  windowed part of `isotronperf`.
- SPEC §108: the canvas, ids and looks, the §5 proof verbatim, and the
  windowed renderer.
- PERFORMANCE Set 155: windowed wake and head-step prices on three
  machines.

**Acceptance:**

```
python3 tools/isotronsim.py --selfcheck           # includes incr==full and the k*=1/22/23 fixtures
python3 tools/os88test.py soak -k 'isotronbound'  # it_maxprobe<=22, it_maxspan<=23, it_ncol==1, canvas==sim
python3 tools/os88test.py soak -k 'isotronwin'    # glass==sim on CGA, Hercules, XT-VGA
python3 tools/os88test.py soak -k 'isotronega'    # QEMU VIDEO=ega windowed glass==sim
python3 tools/os88test.py soak -k 'isotronperf'   # windowed lines under the section 16 ceilings
```

- A human-checked picture of each windowed adapter goes in the reports
  folder and the PR body, judged against this plan's section 17, items 1 and 2.

### Wave 3 - the full-screen colour renderers and the toggle

**Scope:**

- `itfs.inc`:
  - the mode choice by `FSX_CAPS` DL;
  - CGA 320 palette 1 low (and high on C);
  - VGA 0Dh direct planar spans and the chunky-to-planar full present;
  - Hercules pair spans at 2x by 1.5;
  - EGA same-mode through the colour presenter at 640x350, with the
    `FSXM_CGA320` fallback;
  - glyph writers for each surface;
  - the bracket loop with `FSXF_FASTTICK`, the key drain and
    `os88alt_edge`;
  - entry key-release wait and register hygiene.
- The toggle (F/f, Alt+Enter, the menu item ungreyed) and the Esc
  semantics of §8.6.
- The sim gains the `cga320`, `vga0d`, `herc` and `ega` presenters.
- Rows: `isotronfs`, `isotrontoggle`, `isotrontwodisp`, the full-screen
  half of `isotronega`, and the full-screen part of `isotronperf`.
- SPEC §108: full screen.
- PERFORMANCE Set 156.

**Acceptance:**

```
python3 tools/os88test.py soak -k 'isotronfs'        # VRAM==sim on CGA (mode 4, pal 1 low), Herc, VGA 0Dh
python3 tools/os88test.py soak -k 'isotrontoggle'    # state bit-identical across 2 toggles each way, glass right
python3 tools/os88test.py soak -k 'isotrontwodisp'   # Herc bracket leaves the VGA's mode and bytes alone
python3 tools/os88test.py soak -k 'isotronega'       # full-screen EGA glass==sim (QEMU)
python3 tools/os88test.py soak -k 'isotronperf'      # head-step+span under 4,500/4,000/5,000 cycles
```

### Wave 4 - the game's flow: title, menus, the sweep, the tower sequence, game over, pause

**Scope:**

- The title screen (the logo and the §9.1 overlay text), the 1-3 and A-C
  choices, and the Game menu complete.
- The between-round flow of §4.4: crash, the score update at 0.25 s,
  waiting for Space, the paced sweep of §9.4, READY.
- The game-over overlay, then the title.
- Pause (P), and the focus auto-pause.
- The look cycle (C).
- The tower raise and reset drawn through the sweep, in both modes.
- Rows: `isotronflow`, and the sweep part of `isotronperf`.
- A picture series of the tower 1, 2, 3, 4, 1 against the sim's `--png`.
- SPEC §108: the screens and the flow.

**Acceptance:**

```
python3 tools/os88test.py soak -k 'isotronflow'   # launch -> '1' -> BOT game -> game over overlay -> title
python3 tools/os88test.py soak -k 'isotronperf'   # sweep <= 2.2 s on every MartyPC surface, ~1.5 s where fast
```

- The tower sequence pictures equal the sim's canvas for h = 1, 2, 3, 4, 1.
- The reports folder holds the screenshots.

### Wave 5 - audio, input polish and the pace

**Scope:**

- `itsnd.inc`: the §12 table, the priorities, offs at 60h, M, and silence
  on every exit path.
- The typematic filter, double tap, NumLock Ins, and the Up/Down twins.
- The windowed and full-screen clocks verified, the stall-drop rule, and
  `V = now` at each resume.
- Rows: `isotronsound`, `isotroninput`, `isotronpace`.
- SPEC §108: audio, input and pace.
- PERFORMANCE Set 157 (pace and sound cost).

**Acceptance:**

```
python3 tools/os88test.py soak -k 'isotronsound'   # sweep falls, silent tiers silent, gate off on pause/Esc/toggle
python3 tools/os88test.py soak -k 'isotroninput'   # double tap = U-turn; held turn = one turn; F10 ramps
python3 tools/os88test.py soak -k 'isotronpace'    # it/s within 2% of section 4.1 in both modes
```

### Wave 6 - performance audit, every adapter and geometry, docs, the ship

**Scope:**

- Re-measure every §16 line, and tune whatever is over +25%. EGA's
  modelled line is stated as modelled.
- Check the presentation governor is idle at Rookie.
- The 86Box `vm/xt-isotron{,-cga,-herc,-ega}` machines and their targets.
- `make isotrondisk` in all four geometries.
- games360, apps720 and combo fit (or `COMBO_DROP`); `t_livefull` part B
  on a built live image.
- The kern_small refusal (and `t_smallreq` if it demands a row).
- README make lines and Games paragraph, PERFORMANCE final Set (158) and
  the Part 5 row, final SPEC §108, INDEX.
- The PR body with all pictures (§14.4) and the measured table.
- `make test-full` alone on a quiet host.

**Acceptance:**

```
make && make isotrondisk                          # exit 0, images --verify clean
python3 tools/os88test.py soak -k 'isotron*' --marty-jobs 1   # every isoTron row green
make test-full                                    # green (known pre-existing main failures excepted, named)
python3 tools/os88index.py --check                # current
```

- The PR body lists the measured cycles per surface against §16, and
  carries one 1bpp and one VGA screenshot at least.

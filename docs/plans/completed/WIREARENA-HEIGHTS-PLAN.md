# WireArena with heights: stairs, platforms and lifts

**STATUS: BUILT** (reconciled 2026-11-15). Waves 0 to 3 as section 9 has
them (wave 3 with the owner's revised arena, ramps and the one lift, 7.4
and 9.7); waves 4 and 5 landed item by item in the plans after this one,
and 9.12 says where. Open of this plan: the host reference's raised lift in
`tools/wacheck.py`, and the ramp walks of 9.7.3 as suite rows - both carried,
with 9.3's owner items, in [MULTIPLAYER-FPS-PLAN](../MULTIPLAYER-FPS-PLAN.md)
17.3 when this plan was filed under `completed/` (2026-11-15). Since that
day there is no Game menu (the match setup chooses the arena) and the keys
F, G, T and L are gone: the grid and the treads are the detail levels'
(F1-F5), and the field of view is 75 degrees (WIREARENA-NET-PLAN 9.9).
Written first for discussion with the owner, it answers the
owner's note of 2026-11-13:

> *"Lets start on stairs, platforms, and moving platforms; with a more
> 'complete' arena as the demo in the end. The arena can have an open area,
> similar to the one now. In the center, a platform with stairs up to it on
> two sides (we'll eventually put pickups and weapons around). On one side of
> the arena, a looping hallway, two smaller entrances to it, with a smaller
> room in the back; a single platform of a height that can be jumped up onto.
> On the other side of the arena, two platforms that when stepped on raise up
> to a center platform, the highest view in the arena (needs to be able to
> look 'down' far enough to still shoot, and not be so high someone on it
> can't be shot). If any of that causes large performance concerns, discuss
> and plan it out with me before building it."*

**It does raise large performance concerns.** This plan is the discussion
the owner asked for.

WireArena is `apps/wirearena/`. Its history is
[MULTIPLAYER-FPS-PLAN](../MULTIPLAYER-FPS-PLAN.md) §16. Its two segments are
described in [WIREARENA-PARTS-PLAN](WIREARENA-PARTS-PLAN.md) §7–8.

This plan was written on branch `heights-plan`, cut from `multiplayer-fps`
at `a4edcb7f`. At that commit the P bench runs at **18.9 fps at 40 columns**
and **15.7 at 80** (PARTS-PLAN §8.2).

Claims are marked as in the parent plans:

- **Measured**: a number was taken, and the method is given.
- **Derived**: arithmetic from measured unit costs, or a host model
  calibrated against a measurement.
- **Forecast**: a cost for code nobody has written yet, set against shipped
  code of the same kind.

---

## The verdict, short

**1. Heights cost more than columns.** Today's renderer spends a frame on
FIXED costs, about 9,000 clk for each face it projects and spans. Columns are
cheap by comparison (Measured, section 2.1).

A platform lower than the eye is a box whose top outline is visible, so it
draws **two to three more edges** than a wall does. It also leaves its
columns OPEN, so the walls behind it are drawn as well. The requested arena
puts up to **15 boxes** in one view; today's busiest has about 5.

**2. The simplest renderer is too slow.** It would reuse today's span for
every edge. In the busy views the host model gives it **7–9 fps** (Derived,
section 2.4). That is not playable.

**3. The recommended renderer** keeps today's spans for TALL walls. It draws
every box LOWER than the arena's tallest as an OUTLINE: its corners projected
once, each edge a cheap clipped line. Each flight of stairs is ONE object.

Forecast at 40 columns, against today's 18.9 on the P bench:

| where | fps |
|---|---|
| on the floor, averaged over the open area | **~19** |
| the busiest floor view (platform, stairs, tower and building in view) | **~15** |
| on the central platform | **~18** |
| on the tower | **~12** |
| in the hallway | **~25** |

At 80 columns, take about 20% off those figures.

**4. The tower is the costly view.** It looks down on the whole arena, so it
sees the most box tops.

The levers, each costed in section 2.5:

- fewer low boxes, for example no pillars;
- flights as single objects rather than step boxes;
- building walls taller than any eye, so the hallway is not seen from above.

None of them brings the tower back to today's rate. **That is the owner's
call**, question 1 in section 7.

**5. Gameplay limits.** The pitch range has to grow from ±25° to about ±33°.
From the tower that leaves ~5 m of dead ground round its base. The FOV-60
jump bug (eye × kyq overflowing 16 bits) is hit by EVERY raised eye, so its
fix is wave 1's first job.

---

## 1. The renderer

### 1.1 Why today's binary SHUT is enough today, and stops being enough

Today every wall is 3 m tall and stands on the floor. Every eye is below
3 m: standing 1.6 m, at the top of a jump 2.6 m.

So a nearer wall hides everything behind it in its columns, all the way up.
Something behind it would need its top above the line from the eye over the
wall's top, and nothing is taller. That is why `wa_cov` is one tag a column
(MULTIPLAYER-FPS-PLAN §16.1). It is also why the walk stops when no column
is open.

**Heights break this in two ways:**

- **A box lower than the arena's tallest** leaves its columns open ABOVE its
  top edge. A wall behind a 1.25 m platform still shows its top edge above
  the platform.
- **An eye above a box's top** sees the top: its FAR edges, which no face of
  today's renderer draws. Stairs are several such boxes. The tower and the
  raised lifts put the eye at 4.1 m, above every 3 m wall.

### 1.2 What replaces the shut tag: a clip row and a depth stack per column

This is §4.3's original proposal, cut down to what boxes on one floor need.

**Nothing in the arena hangs.** Every box stands on the floor, so a nearer
box only ever covers a column from some row DOWN. The Doom-style top clip
array is not needed. Each column keeps:

- **`wa_clip[c]`**: the topmost half-row covered so far. It starts at 48,
  nothing covered. Drawing goes front to back, so this only rises.
- **The SHUT tag, `wa_cov`, as today.** A column is shut when nothing behind
  can show any more:
  - a TALL face covered it. That is a face at the arena's greatest height,
    seen from an eye below that height, or the room;
  - or `wa_clip[c]` has reached row 0.

  The walk still ends when no column is open, and `wa_left` still counts the
  open ones.
- **A depth stack, `wa_cst[c]`**: up to 4 entries of (span tag, row), one
  pushed each time a face raises the clip. These are the bands of the column,
  front to back. Figures, the floor grid and the pistol's ray test against it
  (section 1.5).

  The host model's deepest stack is 5 entries (the busiest floor view, flights
  counted as one). If a column overflows, its two deepest entries merge into
  one at the NEARER of their depths. That is conservative: a figure in the
  merged band can only be hidden one frame too readily.

**Edges against the clip.** A face draws in an open column as it does today,
except that each cell is tested:

- the top edge if its row is above `wa_clip[c]`;
- the bottom edge likewise;
- a corner post (`│`) from the top down to the clip.

Then a TALL face shuts the column. A LOW face sets `wa_clip[c]` to
min(clip, its top row) and pushes a stack entry.

**What a column then costs** (Forecast, against the Measured 662 clk an
opened column costs today, section 2.1):

- **A visible column of a tall face:** ~+120 clk in all, for the clip load,
  a compare in each of the flat span's two passes (`FLATPASS`) and the
  store.
- **A column visited but wholly hidden** (a wall's edges both under a nearer
  platform): ~90 clk. Today a shut column costs a step.
- **An arena with no low box is FLAT, and keeps today's code path.** The two
  current arenas are flat. They stay byte-identical, and the P and B benches
  do not move.

### 1.3 Low boxes: the OUTLINE path

**Measured: a face plus its span costs ~8,800 clk before its first column**
(3,752 for the face, 5,041 for the span). A box lower than the eye needs:

- its 1–2 front faces;
- 2–3 far top edges;
- all of them clipped.

Drawn as today's spans, that is 4–5 span setups per box, ~40,000 clk:
~8 ms a box. A flight of four steps would be 32 ms.

**The outline path** draws a box lower than the arena's tallest like this:

1. **Its corners, once:**
   - camera x and z, and the column, from today's vertex cache (shared with
     its neighbours);
   - then the top row at the box's own height, and the floor row, by one
     multiply each.
2. **Its visible outline edges:**
   - the top and bottom edges of its front faces;
   - when the eye is above it, the top edges of its back faces, which are
     the far edges of its top;
   - the corner posts at its silhouette.

   Each edge is a DDA in (column, half-row) space: one divide to set it up,
   then a column at a time. Each cell is tested against `wa_clip`, plotted
   with the glyphs the spans use, and the clip is updated.
3. **The clip, column by column**, rises to the box's silhouette. That is the
   far top edge when the eye is above the box, the near one when below.

**What a low box costs** (Forecast):

| piece | cost |
|---|---|
| its corners: transforms, columns, top and floor rows | ~5,200 clk |
| an edge's setup | ~700 clk |
| a column of an edge | ~180 clk |
| a hidden column of an edge | ~60 clk |

The edge path is NOT `wa_span`'s exact 16.16 interpolation from a clipped
end. That is the precision the far-wall bug of MULTIPLAYER-FPS-PLAN §16.5
needed: a wall clipped ~150 columns off the screen. A low box is small and
rarely clipped. When a corner IS behind the near plane (standing on the
platform and looking down), the edge is clipped at the near plane before
its DDA starts, which is MULTIPLAYER-FPS-PLAN 16.5's fix in the form that applies here.

**`tools/wacheck.py` is extended to heights in wave 2.** That is how the
cheaper interpolation's error is held to a number.

**Flights of stairs as ONE object.** A flight is a footprint, a direction, N
treads and a rise. Its corners are N + 1 pairs on two lines, and its
outline is:

- **N tread lines**: each tread's front edge, which is the riser's top;
- **two straight stringers** up its sides;
- **the bottom edge.**

That is one object's fixed cost and N + 3 edges, against N boxes each with
4–5 edges.

**This is a look the owner should see first** (question 3). The stringer is
a straight line, so each side does not show the saw-tooth of each step.

### 1.4 Order inside the walk

The box tree orders boxes front to back. A low box's top lies inside its own
footprint, so the BSP order holds per column for its top edges too. Each box
is drawn whole before the next: front faces, then the far top edges.

A box the player stands ON is the nearest. Its four top edges surround the
eye, so all are "back" edges, clipped at the near plane where they pass
behind it.

### 1.5 What tests against the depth stack

- **Figures (`wa_femit`).** Today a figure cell is hidden when its column's
  wall is nearer: one inverse depth, `wa_colz` / `wa_wallz`. With heights,
  the cell at row r is hidden when the stack has an entry NEARER than the
  figure whose row is ≤ r.

  For figures on the floor this is exact. A nearer face's floor contact
  always projects below any farther figure's feet when the eye is above the
  floor. A figure standing ON a platform is not hidden by the platform's own
  top: the far top edge's entry has the far edge's depth, which is behind
  the figure.

  Cost: ~120 clk a figure cell (Forecast). There are ~15 figure cells a frame.

  **A figure behind a low platform shows above it.** That is the owner's
  example, and the reason for the stack.
- **The pistol's ray (`WRV_TRACE`).** It already tests the screen: the
  crosshair cell, against the figures' projected boxes and the wall in that
  column. It becomes the stack walk at the crosshair column and row.
  - **A shot passes over a low platform** when the crosshair row is above
    that platform's band.
  - **It is stopped by a platform's lip** when the lip covers the crosshair
    cell nearer than the figure.

  What you see at the crosshair is what you hit, as now.
- **The floor grid (G).** A dot is hidden when an entry nearer than it covers
  its row. Today it is tested against `wa_colz`.

### 1.6 Projection, the eye, and the FOV-60 jump bug

The bug is MULTIPLAYER-FPS-PLAN §16.6's: the screen squashes at the top of a
jump at 60°. The renderer makes `numt = (3 m − eye) × kyq` and
`numb = eye × kyq` as 16-bit products.

At 60° `kyq` is 230, so a jump's eye, 166 × 230 = 38,180, already overflows.
**With heights every raised eye does**:

- the tower's eye, 262, is 45,326 at 75°;
- the building's 6 m walls give |h − eye| up to 282, 64,860 at 60°.

**The fix:**

- Keep `num = (h − eye) × kyq / 4` per distinct height, made once a frame.
  The arena has ≤ 8 heights: ~200 clk each.
- `wa_ptbi` takes the row as `hq − (num × iz) >> 17`.

Two bits of precision go, out of a quarter half-row: ~1/16 of a row
(Derived), well under the half-row an edge is drawn at. The same
scale-by-4 goes into `wa_dots`' floor product, `wa_marker`'s head and feet,
and the figures' `eye` terms.

**Wave 1 lands this first.** It fixes today's bug on today's arena, and
every later wave stands on it.

**Pitch.** Today the clamp is ±80 quarter half-rows: ±20 half-rows of shear.
The aim is through the screen's centre. At 75° that is a depression of
atan(20 / 43.5) = **24.7°**; 19° at 60° and 31° at 90°.

| the shot | angle needed (Derived) |
|---|---|
| from the tower (eye 4.1 m) to a torso (~0.9 m) 4 m out | 38.8° |
| the same, 6 m out | 28.2° |
| the same, 8 m out | 21.9° |
| from the floor up to a torso on the tower, 3 m out | 30.7° |
| the same, 4 m out | 24.0° |

**The proposal is ±112** (±28 half-rows): **32.8° at 75°**, 26° at 60° and
40° at 90°. From the tower that leaves dead ground of ~5 m round its base
(206 / tan 32.8° = 319 units).

That is more than the tower's own lip hides. Standing 1.5 m back from the
lip hides only ~1.5 m beyond it, and standing at the lip hides nothing.

A y-shear reads as looking up and down to about ±25–30° before verticals
fail to converge (MULTIPLAYER-FPS-PLAN §4.2). 33° is at the edge of that.
**The owner should look at it** (question 4).

All the arithmetic stays in 16 bits: `hq` runs −16 … 208 quarter half-rows.

**The near plane** stays at 16 units (0.25 m). Standing at the tower's lip
and looking down, its own edges pass under the eye and are clipped there.
Their rows are hundreds of half-rows off the screen, which the scaled
product keeps in 16 bits.

### 1.7 The box tree and `tools/arenagen.py`

**The BSP is unchanged.** Boxes are still 2D footprints split by axis lines,
and heights do not enter the order. The demo layout in section 4 is chosen
so that no four boxes make a pinwheel. `box_tree` builds its tree today,
37 nodes. A pinwheel still refuses at build time, as now.

**Changes to arenagen:**

- **A box gains a height.** It also gains a kind (solid, flight or lift). A
  flight has its direction and tread count; a lift has its index into the
  FRAME block's lift heights.
- **Boxes may touch the room.** Today `arena_tables` refuses a box at a room
  wall (`ROOM[0] < a < c < ROOM[2]`). The tower, its lifts, the building and
  the jump platform stand against walls. Making that `<=` costs nothing:
  WA_ZMAX is still the room's diagonal.
- **Face masks.** A face wholly covered by a neighbour at least as tall, or
  by the room wall, is not emitted. Examples are the tower's and lifts'
  faces against the east wall, and the building blocks' faces against each
  other. That is a 4-bit mask a box, read by `wa_box`.
- **Heights.** The arena's distinct heights go into a table, the per-frame
  `num` products of 1.6.
- **Movement checks.**
  - Every walkable rise is ≤ the step-up (section 3.1).
  - The jump platform is above the step-up and below the jump's reach.
  - The patrols climb only walkable rises.
- **The visible budget (section 4.2)**, checked by sampling poses. The host
  model of section 2 becomes that check: `tools/wamodel.py` in wave 0.
- **The limits that stay:**
  - 31 boxes and 127 vertices: byte indices into `wa_boxes` and `wa_vtx`;
  - 63 x and 63 y values;
  - **8 distinct coordinate steps** (`WA_MAXKS`, `wa_products`).

  The demo uses 19 boxes, 70 vertices, 16 x and 18 y values and **7
  steps**. A ninth step costs one more product pair a frame, two `imul`s:
  ~300 clk (Derived).

---

## 2. Performance

### 2.1 Today's frame, measured by parts

`trace.py` is in this plan's scratch kit (section 8). It sets breakpoints at
the entries of `wa_render`, `wa_leaf`, `wa_face`, `wa_span`, `wa_figures`,
`wa_cross` and `wa_present` in the renderer part. It takes exact guest
cycles between them over 30 frames of the P bench, at 40 columns, on MartyPC
`os8088_5150_cga` at `a4edcb7f`.

| piece | per frame | each | ms a frame |
|---|---|---|---|
| frame setup (products, clears, order) | 1 | 13,538 clk | 2.84 |
| a box the cone drops | 3.43 | 705 | 0.51 |
| a box the cone keeps, its faces chosen | 5.57 | 1,546 | 1.80 |
| a box face that spans | 7.47 | 3,752 | 5.87 |
| a box face that ends (off-screen, edge-on, hidden) | 3.67 | 2,229 | 1.71 |
| a room face that spans (clipped) | 3.00 | 4,745 | 2.98 |
| a room face that ends | 1.00 | 2,681 | 0.56 |
| **a span: 5,041 + 662 × columns opened** (least squares over 314 spans; 3.8 columns a span) | 10.47 | 7,570 | 16.60 |
| figures, 2.7 in view | 1 | 42,669 | 8.94 |
| crosshair | 1 | 1,693 | 0.35 |
| present, sim, input and HUD (`wa_present` to the next frame) | 1 | 33,071 | 6.93 |
| the parts glue | 1 | 1,429 | 0.30 |
| **total** | | **236,194** | **49.5** |

Columns opened a span, by count of spans:

| columns | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9+ |
|---|---|---|---|---|---|---|---|---|---|---|
| spans | 12 | 61 | 85 | 54 | 30 | 16 | 4 | 17 | 5 | 30 |

Box spans open 17.3 columns a frame and room spans 22.7.

**The lesson for heights:** columns are 26% of span time. Setups,
geometry and figures are the rest.

### 2.2 The host model, and its calibration

`hmodel.py` (section 8) renders a pose on the host, as both renderers would.
The arena's boxes are walked in `arenagen`'s tree order. Each box takes:

- the cone test (`wa_leaf`'s, at its 1 − u bounds);
- its faces (`wa_box`'s choice);
- the exact pre-check range;
- a span per face over its open columns.

It prices each step at section 2.1's measured costs. The heights renderer
of section 1 adds per-column clips, the depth stack and the outline path,
priced at the Forecast costs given there.

**Calibration** (Derived): today's arena over 32 poses of the P bench's
circle. The model gives:

| | model | measured (2.1) |
|---|---|---|
| boxes kept | 5.2 | 5.6 |
| box faces spanned | 7.6 | 7.5 |
| box faces ended | 2.8 | 3.7 |
| spans | 10.2 | 10.5 |
| columns | 40.0 | 40.0 |
| render | 41.6 ms | 42.3 ms |
| frame | 48.6 ms | 49.5 ms (the trace window) |

The P bench's own average is 52.7 ms. So model frames are scaled by
52.7 / 48.6 = **1.084** into fps (×1.147 at 80 columns). That scaling folds
in what the model leaves out: the HUD, the meter and figure-count drift.

### 2.3 The poses

The demo arena is the layout of section 4, at FOV 75 with 2.7 figures in
view as on P. The model's poses:

- **Floor:** 8 places in the open area × 16 headings.
- **Floor, P-like:** (800, 400), looking north-east across the stairs and
  the platform at the tower and the building's corner. This is the busiest
  floor view found.
- **On the platform:** eye 2.85 m, 16 headings. Also at the top of a jump
  there (3.85 m).
- **On the tower:** eye 4.1 m, looking west ±45°, level and pitched down 60
  quarter half-rows. Also on a raised lift.
- **The hallway:** 4 places × 16 headings.
- **The back room:** 16 headings.

### 2.4 What the renderer would do, and what it costs

**Per frame at 40 columns** (Derived from the model). The variants are:

- **A**, the simplest: today's span for every edge, far top edges included,
  4 steps a flight.
- **G**, the recommended: section 1.3's outline path, flights as objects,
  building walls 6 m.

| | today's P | A, busiest floor | G, busiest floor | A, tower | G, tower |
|---|---|---|---|---|---|
| boxes the cone keeps | 5.2 | 14.9 | 8.9 | 19.3 | 14.8 |
| tall faces (spans) | 10.4 | 29.8 | 7.2 | 32.0 | 16.9 |
| far top edges | 0 | 18.0 | 6.0 (outline) | 24.4 | 15.3 (outline) |
| span columns visited / wholly hidden | 40 / 0 | 83 / 19 | 38 / 2 | 59 / 14 | 40 / 0 |
| top-edge span columns | 0 | 49 | — | 73 | — |
| outline edges / their columns | — | — | 33 / 145 | — | 24 / 135 |
| cells written (present: 138 clk each, in both lists) | 134 | 210 | 199 | 227 | 222 |
| deepest stack (A: the poses' mean of their deepest; G: the deepest) | — | ~7 | 5 | ~3 | 3 |

**Forecast fps at 40 columns** (model × 1.084; the worst pose of each set
in brackets):

| where | A: today's spans for everything | D: outline, 4 step boxes a flight | E: outline, 2 step boxes | **G: outline, flights as objects** |
|---|---|---|---|---|
| floor, open area | 13.8 (6.6) | 17.6 (10.0) | 18.8 (11.2) | **19.1 (11.4)** |
| floor, busiest | 7.4 | 12.4 | 14.1 | **14.7 (13.6)** |
| on the platform | 12.7 | 17.3 | 18.4 | **18.4 (17.1)** |
| at a jump's top, on the platform | 12.8 | 17.5 | 18.7 | **18.7** |
| on the tower | 7.2 | 11.1 | 12.1 | **12.4 (11.2)** |
| on a raised lift | 7.7 | 11.4 | 12.4 | **12.7** |
| hallway | 21.6 (8.7) | 24.2 | 24.7 | **24.9 (15.0)** |
| back room | 22.8 | 24.1 | 24.4 | **24.6** |
| *today's arena: P bench / floor average* | *19.0 / 24.6* | | | |

**At 80 columns** (×1.147): G gives 15.0 on the floor, 11.2 at the busiest,
14.4 on the platform, 9.8 on the tower and 20.0 in the hallway. Today's P is
15.8.

**Sensitivity to the Forecast unit costs.** Scaling every heights-only cost
of section 1 by 0.67 / 1.0 / 1.5 moves G as follows:

| where | ×0.67 | ×1.0 | ×1.5 |
|---|---|---|---|
| floor | 20.1 | 19.1 | 17.6 |
| busiest | 16.2 | 14.7 | 12.9 |
| platform | 19.7 | 18.4 | 16.8 |
| tower | 13.3 | 12.4 | 11.2 |
| hallway | 25.7 | 24.9 | 23.7 |

**Where G's tower frame goes** (74 model ms):

| part | ms |
|---|---|
| spans of tall faces | 17 |
| present, sim and loop | 12 |
| tall faces' geometry | 11 |
| figures | 10 |
| outline edges | 7 |
| low boxes' corners | 6 |
| leaf tests | 4 |
| setup | 3 |
| the room | 3 |

So the tower is slow because it sees MOST OF THE ARENA: 15 boxes. Their tops
are not the main cost.

**And on today's arena**, flat, on the flat path: **nothing moves.** The P
and B benches stay where they are (section 1.2).

### 2.5 If it is too slow: the levers, costed

Each is G at 40 columns (Derived), as floor / busiest / tower fps.

| lever | floor / busiest / tower | what it costs the game |
|---|---|---|
| G as recommended | 19.1 / 14.7 / 12.4 | — |
| no pillars in the open area (17 boxes) | 20.0 / 16.5 / 13.4 | cover in the open area |
| flights of 2 treads (rise 27 units, 0.42 m: over the 24-unit step-up, so 3 treads is the least that walks) | 19.2 / 15.0 / 12.5 | almost nothing gained: the treads are cheap edges |
| a lower tower (2 m, not 2.5) | 19.0 / 14.6 / 12.4 | nothing gained: it sees the same boxes |
| building walls 3 m, not 6 (F against D) | about −0.3, and −1 fps at the platform's jump top | the hallway seen INTO from the tower and from a jump on the platform |
| per-step boxes rather than flight objects (D) | −1.5 / −2.3 / −1.3 | the true saw-tooth stair sides |
| today's spans for everything (A) | −5 / −7 / −5 | — not viable |

The levers the owner proposed:

- **"Fewer, larger steps":** it helps only while steps are BOXES (E against
  D: +1 to +2 fps). Once a flight is one object it buys nothing (G).
- **"Closing columns by a platform's solid band":** that IS the clip row of
  section 1.2. A low platform closes its columns below its top. What it
  cannot do is close them above, because what is behind shows there.
  Treating low boxes as tall would hide the arena behind every step: wrong
  on the glass.
- **"Culling the hallway when outside it":** with 6 m building walls this is
  already nearly free. From outside, its interior boxes end at the cone or
  the occlusion pre-check (2,229 clk a face, 705 a box). The hallway sets are
  the fastest in the table. A zone flag would save ~1 ms in views through an
  entrance (Derived), not worth it yet.

Other levers, for later rounds, ranked:

1. **Per-span glue, ~11% of today's frame** (MULTIPLAYER-FPS-PLAN §16.8's
   next item). Every tall span in the tower view pays it.
2. **A visible-box budget enforced by `arenagen`**, section 4.2. A design
   rule, at no runtime cost.
3. **Figures, 10 ms of the tower frame at 2.7 figures.** The level of detail
   for small figures that pass 4 measured and the owner left open.
4. **A height-band level of detail.** Low boxes beyond ~12 m drawn as their
   top outline only, without corner posts or the front bottom edge.
   ~1–2 ms in the busy views (Derived from the edge counts). A picture change.

---

## 3. Physics and the sim

### 3.1 Floors, steps and jumping

- **`wa_floor`**: the floor under a footprint is the tallest box its square
  overlaps, or 0. It reads the same box table as the collision, plus heights
  and the lifts' current heights. It takes the radius square `wa_collide`
  already widens. Standing on a lift's edge rides the lift.
- **Step-up: 24 units (0.375 m).** A move whose new floor is at most 24
  above the feet climbs: the feet snap up, no jump. Higher, and it collides,
  exactly as a wall does now, unless the feet are already above that floor
  (in the air). That is the whole stair rule.
  - The flights' treads rise 16 (4 treads to 1.25 m). They walk.
  - **The jump platform, 48 (0.75 m), does not walk.** The jump reaches
    it: `WA_JUMPV` 2432 and `WA_GRAV` 179 (8.8 a tick) give an apex of
    v² / 2g = 64.5 units, ~1.0 m, at 13.6 ticks.
  - **The central platform, 80 (1.25 m), cannot be jumped onto.** Its
    stairs are needed. If the owner wants it jumpable, it must be under
    ~60 (question 5).
- **Gravity and landing.** `wa_jz` becomes height above THE FLOOR UNDER YOU,
  and today's integration is unchanged.
  - Walking off an edge starts a fall from that height. The eye is
    `floor + 102 + jz`, the same as now.
  - **Landing:** when `floor + jz` would go below the floor, it sets the feet
    there.
  - **Stepping down:** a drop of up to 24 snaps down without a fall, so
    descending stairs does not bob.
  - No fall damage yet.

### 3.2 The lifts

- **A lift is a box whose height the game moves.** Its height is a FRAME
  variable (`wafb.inc`, PARTS-PLAN §7.3), copied to the renderer every frame
  like the eye. The renderer reads it as the box's height, and the tree does
  not change.
  - At rest it is 16 (0.25 m): a step onto it.
  - Raised it is 160, the tower's height, so it is flush with the tower.
- **Trigger:**
  - Something standing on it (`wa_floor` names the lift) for 0.5 s (15
    ticks) starts it up.
  - It rises at 4.3 units a tick, ~2 m/s: 144 units in ~1.1 s.
  - It holds at the top while occupied. Once empty for 2 s (60 ticks), it
    goes back down.
- **Riders:** whoever stands on it has floor = lift height, so the feet rise
  with it. A rider at the edge is carried by the radius square.
- **Nothing can be under a lift.** It is solid from the floor up.
- **Someone beside it:**
  - when it rises, nothing happens to them: they are outside its footprint;
  - when it comes DOWN beside someone, nothing happens either;
  - walking into its side while it is above the step-up collides, as a wall.
- **Dummies** trigger lifts only if their patrol crosses one (question 7).

### 3.3 Figures on stairs and platforms

- **A figure record gains its floor height, `F_Z`** (F_SIZE 40 → 42). It is
  a FIGS field the renderer copies with the head bytes.
  - The renderer adds it to every joint's height (`wa_fjl`'s height terms)
    and to the marker's feet.
  - The sim sets it from `wa_floor` with the same step-up.
- **The dummies' patrols** are re-laid for the new arena. One crosses the
  platform by its two flights. `arenagen` checks every leg's rises, as it
  checks today that no leg runs into a wall.

### 3.4 The pistol through heights

It is section 1.5's screen-space test: the crosshair cell, the figures'
projected boxes, and the column's depth stack.

- **Over a low platform:** the shot passes when the crosshair row is above
  the platform's band in that column.
- **A platform's lip:** it stops the shot when the lip covers the crosshair
  cell and is nearer than the figure.
- **On the tower:** a figure standing back from the lip is shot through its
  visible part, its torso and head above the lip.

The tower's height is set by section 1.6's pitch numbers: shots at ±33°
reach from ~5 m out (down) and ~3 m out (up).

---

## 4. The arena

### 4.1 The layout, in arenagen units (64 a metre; heights above the floor)

*Arena 2 as wave 2 built it. Wave 3 revised it from the owner's sketch:
7.4.1 is the layout now; the heights here stand.*

The room is unchanged: 0..2048 square, walls 3 m. Nothing stands behind a
room wall, so the room shuts its columns whatever the eye.

**West: the building.** Walls 384 units (6 m): taller than any eye, so it is
never seen into from above. It is a loop corridor 2 m wide round a core,
with two 2 m entrances and a back room.

| box | x | y | h |
|---|---|---|---|
| front wall, south piece | 640..704 | 512..640 | 384 |
| front wall, middle | 640..704 | 768..1280 | 384 |
| front wall, north piece (entrances at y 640..768 and 1280..1408) | 640..704 | 1408..1536 | 384 |
| south wall | 256..640 | 512..576 | 384 |
| north wall | 256..640 | 1472..1536 | 384 |
| the core (the loop runs round it) | 384..512 | 704..1344 | 384 |
| south-west block | 0..256 | 512..704 | 384 |
| north-west block | 0..256 | 1344..1536 | 384 |
| back wall, south (the back room's door at y 960..1088) | 192..256 | 704..960 | 384 |
| back wall, north | 192..256 | 1088..1344 | 384 |
| **the jump platform**, in the back room (0..192 × 704..1344) | 0..128 | 960..1088 | **48** (0.75 m) |

**Centre: the platform and its stairs.** The platform is 5 m × 4 m with
flights on its south and north sides. Each flight is 2 m wide and 2 m long:
4 treads, each rising 16 and 0.5 m deep.

| box | x | y | h |
|---|---|---|---|
| the platform | 1088..1408 | 896..1152 | **80** (1.25 m) |
| south flight (rises northward) | 1184..1312 | 768..896 | 16 → 64, then the platform |
| north flight (rises southward) | 1184..1312 | 1152..1280 | 16 → 64 |

**East: the tower and its lifts**, against the east wall.

| box | x | y | h |
|---|---|---|---|
| the tower | 1856..2048 | 896..1152 | **160** (2.5 m) |
| south lift | 1856..2048 | 640..896 | 16 → 160 |
| north lift | 1856..2048 | 1152..1408 | 16 → 160 |

**The open area:** two pillars, 384 high, as cover. They are the first
lever of section 2.5.

| box | x | y |
|---|---|---|
| pillar | 1472..1600 | 448..576 |
| pillar | 1472..1600 | 1472..1600 |

**Totals: 19 boxes, a 37-node tree, 70 vertices, 16 x and 18 y values, 7
distinct coordinate steps.**

- The start (1024, 480) is clear.
- **The patrols are re-laid in wave 5.**
- **Weapons and pickups** go round the platform, as the owner said, in a
  later plan.

### 4.2 The visible budget

MULTIPLAYER-FPS-PLAN §4.1's rule, "~25 segments visible from anywhere", was
written for walls. It is restated in the renderer's own units, and
`arenagen` refuses an arena over it, sampled over its free floor and every
platform top:

**From any pose, at most ~15 boxes kept by the cone and ~35 outline edges.**

That is where G's model gives the tower ~12 fps. This arena sits at the
limit **from the tower only**, and its floor views keep ~9 boxes.

### 4.3 What grows in the code's limits

**`WA_MBOX`, `WA_MVTX` and `WA_MNODE` are generated maxima**, so they follow
the arena with no other change. The renderer's BSS grows by about 2 × the
added boxes, 6 × the added vertices and 12 × the added nodes (Derived).

**The byte-index limits are not reached.** 31 boxes and 127 vertices are
the hard ceilings, against 19 and 70 here.

---

## 5. Memory

| where | what | bytes (Forecast) |
|---|---|---|
| **renderer part, BSS** (16.7 KB free today, PARTS-PLAN §8.1) | `wa_clip` (a byte a column) | 80 |
| | the depth stack (4 × tag and row × 80 columns) | 640 |
| | the heights' `num` products, ≤ 8 | 16 |
| | box heights and face masks, 31 boxes | ~100 |
| | the bigger tree, vertex cache and box order of this arena | ~550 |
| **renderer part, image** | the clip in the span passes and corners | 300–600 |
| | the outline path: corners, the edge DDA, the silhouette | 1,500–2,500 |
| | flights | 400–800 |
| | the stack in figures, trace and dots | 200–400 |
| | per-height projection and the overflow fix | 100–200 |
| | **the renderer, in all** | **~3.0–5.2 KB of 16.7** |
| **game segment** (55.8 KB free) | `wa_floor`, step-up, landing | 400–700 |
| | lifts (state machine, trigger, FRAME copy) | 300–500 |
| | figures' floor heights and patrols | 200–400 |
| | pitch range, FRAME block (+4 bytes lifts, +2 a figure) | ~50 |
| | **the game, in all** | **~1–1.7 KB** |
| **arena part** (lazy, one per arena) | heights, kinds, masks, flights, lift indices (~4 bytes a box) | ~+100 |
| | the demo arena whole (today's full arena, 9 boxes, is 488 bytes) | ~800–1,000 |
| **host** | `arenagen` heights, masks, flights, lifts, checks; `tools/wamodel.py` | — |

**Nothing here threatens a segment.** The renderer part keeps ≥ 11 KB free
after it.

---

## 6. Waves

Each wave ends with its user-visible outcome on the glass and its rows
named. Per CLAUDE.md, the soak union and `test-full` run once, after the
last wave.

**Wave 0. The host tools.**
- `tools/wamodel.py` (this plan's model, its unit costs in one table, and
  the pose sets).
- `arenagen`'s box schema: height, kind, face mask, touching the room.
- The demo arena drafted as arena 3, a lazy part like the two today, not yet
  loadable.
- The visible-budget check, wired into `arenagen`.

*Outcome:* the model's per-pose report, which the owner reads before any
renderer work.

*Verified by:*
- the model reproducing section 2.2's calibration;
- `arenagen` refusing a pinwheel, an over-budget arena and an unwalkable
  patrol.

**Wave 1. Projection, the eye, the pitch, and the clip arrays on the flat
path.**
- The `num / 4` products: the FOV-60 jump bug fixed.
- Pitch to ±112.
- `wa_clip` and the depth stack, used by tall faces, figures, the trace and
  the dots.
- A flat arena keeps today's code.

*Outcome:* the jump at 60° no longer squashes the screen, and the pitch
reaches 33°.

*Verified by:*
- posed text VRAM of both current arenas byte-identical to `a4edcb7f` at
  75 and 90, apart from the deliberate pitch range;
- at 60 the jump's frames, compared against the host reference;
- the P and B benches unmoved (within 0.5%).

**Wave 2. Low boxes.**
- The outline path and its far top edges.
- Flights as objects.
- The demo arena loadable, static (no lifts yet, heights fixed).
- Teleport poses (`wacheck`'s method) on the platform and the tower.

*Outcome:* the arena on the glass, at every pose of section 2.3, with
figures showing above low platforms.

*Verified by:*
- `tools/wacheck.py` extended with heights: the host reference ray-casts
  boxes with tops, and the edge-error count is held to today's ~6 in 30
  poses;
- screenshots at the section 2.3 poses;
- **the forecast checked**: a heights bench pose set measured against the
  model, so the model is re-priced before wave 3.

**Wave 3. Walking on heights.**
- `wa_floor`, step-up, landing, stepping down.
- Jumping onto the back room's platform.
- `F_Z` and the figures drawn at their floor.
- The pistol over and against lips.

*Outcome:* walk up a flight, jump onto the platform, shoot a dummy over a
low platform and miss one behind a lip.

*Verified by:* scripted MartyPC runs through `tools/os88ui.py`, reading
`wa_px`, `wa_py`, `wa_jz` and the floor as the guest has them, and the
hits.

*As built (9.7):* with ramps for flights, the revised arena, the asymmetric
pitch, the arena's own patrols and the one lift (wave 4's ride, landed
here: 7.4.4). The scripted runs are scratch scripts, not suite rows yet.

**Wave 4. The lifts.** *(Its ride landed in wave 3 for the one lift, 7.4.4:
the lift's height goes G -> R as one ARENA word rather than in FRAME. Left
for wave 4: dummies on lifts, more than one lift, and the host reference's
raised lift.)*
- Lift heights in the FRAME block.
- The trigger, the rise, the hold and the return.
- Riding a lift.

*Outcome:* step on a lift, ride it to the tower, step across.

*Verified by:* a scripted ride reading the lift's height and the eye each
tick, and the renderer's picture of a raised lift against the host
reference.

**Wave 5. The demo arena complete.**
- Patrols across the platform. *(Landed in wave 3: 7.4.5.)*
- The start.
- A heights play bench: an `H` bench with a fixed busy pose and the tower,
  like P (MULTIPLAYER-FPS-PLAN §16.7).
- Measured against section 2.4's forecast.
- The levers of section 2.5 the owner chose.

*Outcome:* the demo, with its measured rate on the HUD.

*Verified by:* the H and P benches, the posed-VRAM sets, `wacheck`, and
both 1bpp adapters looked at (SPEC.md §39.4, the rule CLAUDE.md repeats).

---

## 7. Questions for the owner

1. **The frame rate with heights.** At 40 columns, forecast:

   | where | fps |
   |---|---|
   | open floor | ~19 (today's P bench is 18.9) |
   | busiest floor view | ~15 |
   | on the platform | ~18 |
   | on the tower | ~12 |
   | in the hallway | ~25 |

   80 columns is about 20% lower. **Is ~12 on the tower acceptable** for
   the arena's best view, or should the layout shrink what the tower sees?
   For example: no pillars (+1), a smaller centre, or a tower in a corner
   looking along a wall.
2. **The building's walls at 6 m, taller than any eye.** The hallway is then
   never seen into from above. That is cheaper, and the hallway works as a
   building with no roof. Or 3 m walls, the same as the room: seen into from
   the tower and from a jump on the platform, at ~1 fps more cost there. A
   true roof is room-over-room, which this renderer does not do (§4.1).
3. **Stairs as flight objects or per-step boxes.**
   - Flight objects draw each tread's line and two straight side stringers,
     ~+1.5 to +2.5 fps.
   - Per-step boxes draw the true stepped sides.

   This is a screenshot decision, offered in wave 2.
4. **Pitch to ±33° at 75°** (26° at 60°, 40° at 90°). That leaves ~5 m of
   dead ground round the tower's base from its top. Is the shear acceptable
   at its ends?
5. **The central platform at 1.25 m,** stairs only (a jump reaches ~1 m). Or
   under ~0.95 m, jumpable too.
6. **The tower at 2.5 m.** From it, a torso 6 m away is 28° down. From the
   floor, a figure at its lip is in view from anywhere. One standing 1.5 m
   back shows its head and torso from ~4 m out (Derived).
7. **Lifts.**
   - Do the dummies (later, other players and bots) trigger them, or only
     the player?
   - Is a 0.5 s start, a ~1.1 s ride and a 2 s return delay right?
8. **80 columns:** at ~11 fps in the busy views, is 80 a mode for faster
   machines (V20 and up) once heights land?

---

### 7.1 The owner's answers (2026-11-13)

These bind the waves. Where one changes a section above, it overrides it.

- **Hiding the hallway from outside: yes, and it is a direction.** Larger
  multi-area arenas or "levels" will come, and a view must not reach all the
  way across them. Keep the building walls taller than any eye, so they
  hide what is behind them. Leave room for an area-to-area visibility cut
  later; do not build it now.
- **Q1, frame rate:** lower fps on the tower is acceptable. Fewer pillars in
  the open area is an available lever. Another optimisation round will come
  once the remaining features are in (pickups, the AI), which will cost
  frames of their own.
- **Q2, building walls:** the higher walls (6 m).
- **Q3, stairs:** a flight as one object with straight sides, as planned.
  Screenshots in wave 2 decide whether it is good enough.
- **Q4, pitch ±33°:** acceptable, and so is ground at the tower's base that
  cannot be hit from the top. **But it must be SYMMETRIC.** If someone on the
  tower cannot hit someone below, the one below must not be able to hit them
  either. The design checks this both ways. Body heights against eye heights
  make the two angles differ, and so does the tower's lip, which hides a
  player standing back from the edge.
- **Q5, central platform:** stairs only, at 1.25 m. It is a test of the
  mechanics. The jump test is the platform in the small back room off the
  hallways.
- **Q6, tower:** 2.5 m.
- **Q7, lifts:** bots may trigger them if it is cheap. The rudimentary AI
  will need something like it later, at a cost of its own. The timings are
  the owner's to judge by riding. As numbers they look a little long, so
  start shorter, and make them constants that are easy to change.
- **Q8, 80 columns:** already a mode for faster machines. Hercules has no
  40-column choice, so it pays the 80-column price.

### 7.1.1 The dead ground: the owner's decision, and the limits it derives

The owner, on 9.3's open question:

> *"I don't like A because then you can 'see' but not 'hit'. We might still
> take it if nothing else is performant, but lets consider a 'you can look
> down further than up' view angle, and if it doesn't hurt performance the
> layout alternative."*

The order of preference, then:

1. **An asymmetric pitch**: a down limit deeper than the up limit, chosen so
   the reach is the same from both ends.
2. **The lip-and-depth layout** (9.3's (b)), if it costs no frame time.
3. **The eye-to-eye rule** (9.3's (a)) only as a fallback. Say so before
   building it.

**The arithmetic** (Derived, host). The pitch is a SHEAR, so a limit in
quarter half-rows is a limit on the tangent: `kyq` = 173 at 75° makes
112 quarter half-rows tan 0.644, 32.8°.

- **From the floor** (eye 102) the lowest part of a body at the tower's lip
  is the lip, 58 above the eye. At an up limit U it is in reach from
  58 / tan U out.
- **From the tower** (eye 262) the highest part of a body on the floor is
  its head, 112, 150 below the eye. At a down limit D it is in reach from
  150 / tan D out.
- **Equal reach** is tan D = (150 / 58) tan U, so **D = 2.586 U** in
  quarter half-rows.

| up limit U | down limit D | reach both ways |
|---|---|---|
| 112 (32.8°, today's) | **290 (59°)** | **1.4 m** |
| 96 (29°) | 248 (55°) | 1.7 m |
| 80 (25°) | 207 (50°) | 2.0 m |
| 64 (20°) | 166 (44°) | 2.5 m |

The first row keeps today's up limit and is the recommendation. A smaller
U gives a smaller D and a wider dead ground round the tower, for no gain in
cost: the cost below is the same for every row.

**Does it still read at −290?** Screenshots from the tower at −290 and at
−112, and from the floor at +112 looking at the lip, at both widths (9.4.6).
A shear keeps verticals vertical and figures upright. At −290 the floor
grid is steep and the horizon is far above the screen: the platform is at
the top edge and the figures near the tower's base fill the middle. The
owner judges whether that reads.

**The 16-bit products at −290 with the tower's eye** (Derived):

- the horizon is at −194 quarter half-rows (−48 half-rows), which every
  product and the clip arrays carry signed;
- `wa_numb` and the per-height pairs of 7.3.1 already choose their shift
  per frame (7.2.3);
- **the figures' narrow sum** is bounded by `wa_fvn`. Looking DOWN it
  grows, so it stays narrow. Looking UP from the tower (horizon half-row
  52) it drops to 73, and figures whose sum passes that take the 32-bit
  wide path. That costs only in those frames: the tower looking up.

**The cost** (Derived): none per frame beyond the clamp. A deeper pitch
puts more floor and less wall in view, and floor is cheaper than wall in
this renderer. The one added cost is the figures' wide path when looking
up from the tower. Nothing was measured, because the clamp is not changed
in wave 2.

**What is visible but cannot be hit.** The crosshair is the screen's
middle, so at a pitch limit the half-screen beyond it still shows bodies
the crosshair cannot reach (Derived; the screen's edge is 96 quarter
half-rows past the middle):

- **from the tower at D = 290**, a floor body 1.06–1.4 m from the base
  (standing at the lip) shows below the crosshair and cannot be hit;
- **from the floor at U = 112**, a body standing 0.75–1.4 m back on the
  tower shows above the crosshair and cannot be hit.

The two bands are the same reach from both ends. Each is the rest of the
screen past a limit, the band every FPS with a pitch clamp has. Inside
1.06 m (from the tower) the lip begins to hide the body.

**The layout alternative costs frame time.** A lip hides a body below only
within 0.47 × the setback (150 / 102 − 1, from the tower's eye over its
lip). Hiding the whole 1.4 m band from a player standing at the lip
therefore needs a parapet about 1.25 m tall round the tower's top. That is
a box, and every box in view costs frame time (9.4.2). It is not taken.

**For wave 3:**

- `WA_PITCH` becomes TWO limits: up 112, down 290.
- The pistol's test stays the screen's: a hit is what the crosshair covers.
- Figures cannot stand on the tower until wave 3's `F_Z`, so the from-the-floor
  half was checked with a figure at the lip's height on the host only.
- **It is not changed in wave 2.**

### 7.1.2 The owner after wave 2 (2026-11-13)

- **A speed pass on the heights renderer comes before wave 3.** It is split
  between agents: the outline path's per-column work, the low boxes'
  per-box setup, and the pass setup with the flat arenas' overhead.
- **Pitch at 33 degrees up and 59 degrees down:** the owner will judge it by
  playing on the platforms. Wave 3 builds the asymmetric limits (7.1.1) as
  easy-to-change constants.
- **Flights drawn as straight-sided ramps are fine as a style** (Q3 settled).
- **Not yet answered:** whether the kernel's divide-fault panel (9.3 item 5)
  should be on in the shipped kernels.

### 7.2 The contracts for waves 0 and 1 (pinned before the code)

Waves 0 and 1 change three interfaces. Each is pinned here first: the
arena part, the box schema the host tools share, and the renderer's
column state.

**7.2.1 The arena part grows four words** (extending
[WIREARENA-PARTS-PLAN](WIREARENA-PARTS-PLAN.md) 7.5). The descriptor's 15
words keep their offsets, and four more follow:

| offset | word | |
|---|---|---|
| 30 | `boxh` | offset of the BOX RECORDS: per box, in `wa_boxes`' order, 4 bytes |
| 32 | room height | units above the floor (today 192) |
| 34 | `hmax` | the arena's TALLEST height, the room's included: a face at it, seen from below it, shuts its columns |
| 36 | `lifts` | offset of the LIFT TABLE: a count (word), then per lift its box index (byte), 0, its rest height and its raised height (words) |

The tables follow at 38. Each box record holds:

- its **height** (a word): for a lift, its rest height;
- its **kind** (a byte):
  - bits 0–1: 0 solid, 1 flight, 2 lift;
  - for a flight: bits 2–3 the face it rises TOWARD (0 S, 1 E, 2 N, 3 W),
    and bits 4–7 its tread count;
  - for a lift: bits 4–7 its index in the lift table;
- its **faces** (a byte):
  - bits 0–3: S, E, N and W drawn. A face wholly against the room wall,
    or wholly covered by neighbours at least as tall, is not;
  - bits 4–7: those faces' TOP EDGES drawn. One is dropped only where
    neighbours of exactly the same height cover it, so the top runs on
    unbroken.

The two arenas of today are written with every box 192, solid, all faces
and tops drawn, room 192, `hmax` 192 and no lifts. A renderer that reads
none of it is unaffected. Wave 1 reads `hmax` and the box heights, only to know the
arena is FLAT (no box below `hmax`).

**7.2.2 The box schema** (host, `tools/arenagen.py`). An arena's boxes are
4-tuples, a 3 m solid box as today, or `Box(x0, y0, x1, y1, h, kind=...)`.
The same records feed:

- the arena parts;
- the visible-budget check;
- `tools/wamodel.py`, which reads arenas from `arenagen` and keeps no
  layout of its own.

Arena 2 is the demo of section 4.1, built as a third lazy part. It is not
on the Game menu until wave 2 draws heights: today's renderer would draw
every box 3 m tall. Its patrols wait for wave 5, so the patrol check skips
it until then.

**7.2.3 The eye arithmetic** (section 1.6, as built). `numt` and `numb`
each become a (product, shift) pair, made once a frame:

- **when the 16-bit product fits**, it is today's, with today's shift: the
  same bits;
- **when it does not**, it is the 32-bit product / 4 with a shift two
  smaller.

So every frame that did not overflow is byte-identical, and one that did is
right where it was wrong. The floor grid's product takes the same pair.

This replaces section 1.6's "num / 4 always", which would have moved a
quarter of a quarter half-row in rare frames. The FIGURES' narrow path
(`wa_fjl`) sums `eye × fv8 + 256 hh` in 16 bits. It is taken only when that
fits, against a bound made once a frame, so a raised eye or a high horizon
takes the 32-bit wide path instead.

**7.2.4 Pitch** is clamped at ±112 quarter half-rows (±80 before),
section 1.6.

**7.2.5 The column state** (section 1.2):

- **`wa_cclip`**: a word a column (`wa_clip` is the near-plane clipper's
  name already).
- **`wa_cst`**: per column a count and 4 entries of (span tag, row).
- **Cleared each frame** on an arena that is not flat.
- **Queried by one routine, `wa_occl`**: column, row, inverse depth → hidden
  or not. The figures, the pistol's ray and the floor grid ask it.
  - On a flat arena a column has no entries, so `wa_occl` is today's wall
    test.
  - A column's tall face is its `wa_cov` tag, as today.

The WRITERS of entries are the low boxes' outline path and the
clip-tested passes of tall faces. Nothing before wave 2 can make a low
entry, so they land with wave 2, which exercises them.

**7.2.6 `tools/wamodel.py`** prints the per-pose forecast of section 2 for
an arena:

    python3 tools/wamodel.py --arena 2 [--width 40|80] [--variant G]

It also checks an arena against the visible budget of section 4.2:
`arenagen --arena N` refuses one over it.

### 7.3 The contracts for wave 2 (pinned before the code)

Wave 2 draws heights. A flat arena keeps every path of wave 1: its posed
frames are byte-identical (9.4.1).

**7.3.1 Tall and low.** `wa_loadarena` copies the box records of 7.2.1 into
`wa_bxh` (4 bytes a box) and the room's height into `wa_roomh`. On an arena
that is not flat the leaves are walked by `wa_leafh`, a second copy of
`wa_leaf`, which sorts each box:

- **TALL**: its height is the arena's `hmax` and the eye is below it. It is
  today's box (`wa_boxh`) with its own height's top. `wa_hnt` makes that
  height's (product, shift) pair, `wa_numt` and `wa_nsht`, by 7.2.3's rule.
  It keeps the pair for the next box of the same height (`wa_ntht`), so the
  room and the building pay for it once a frame.
- **LOW**: any other box. It is drawn in OUTLINE (7.3.3).

The room's faces take the same pair at `wa_roomh`. On a flat arena the pair
is fixed (shift 3, height 192) and `wa_boxh` is never reached.

**7.3.2 The clip-tested passes.** On an arena that is not flat, every tall
face's edges are tested against the column's clip before they are drawn.
`wa_cclip` is the topmost half-row a nearer low box already covers.

- **The spans** (`SPANLOOP`'s fourth argument): four more instances,
  `wa_lc00`..`wa_lc11`, selected by `wa_loopsc`. The top edge is drawn only
  below the clip, by `WPLOT` (the first plot in a cell wins). The bottom
  edge is tested the same way.
- **The flat passes** (`FLATPASS`'s third argument): `wa_foc` and `wa_fpc`.
- **The corner posts** are clamped to the clip (`wa_cclp`).
- **The steep fills** (`wa_slope`) are clamped to the clip as well.

A tall face shuts its column (`wa_cov`), as today. Nothing behind it is
drawn, so it needs no stack entry.

**7.3.3 The outline path** (`apps/wirearena/wrlow.inc`, `wa_lowbox`). A low
box is drawn in phases:

1. **Corners.** Its four corners are transformed through the shared vertex
   cache (`wa_vert`). Each keeps its own height (`wl_ch`). A corner in the
   view is projected once a box: column, inverse depth, top row and floor
   row (`wl_kc`..`wl_kf`). The floor's pair is made once a frame (`wl_ffr`)
   and the box's once a box.
2. **Posts** at the visible corners, against the clip as it stood.
3. **The faces the player is outside of**, a pass each (`wl_draw`):
   - the floor edge, against the clip as it stood;
   - then the top edge, which RAISES the clip and pushes a stack entry (its
     span tag and the half-row it covers from).

   An edge with a corner out of the view is clipped (`wa_clip`) and its ends
   projected on the spot (`wl_xedge`).
4. **With the eye above the box**, its other faces' top edges, the far side
   of its top, raising the clip again.

A pass's columns are four loops (`WLLOOP`: one edge or two, shallow or
steep), plotted cell by cell against the clip. A top edge at or above the
screen's top shuts its column, holding its tag in `wa_cov` as a wall's
does. The stack keeps `WA_CSTK` = 4 entries a column; a fifth is merged
into the deepest.

**7.3.4 Flights** (kind 1) are ONE object:

- **The low end** is a face as tall as the first tread. Its two corners
  take that height.
- **The sides** (the stringers) are straight, from the low end's corners
  to the top's.
- **The inner treads** are lines across the flight at their heights. Their
  ends are lerped in camera space along each side by the flight's going
  (`wl_going`), and drawn nearest first: from the low end when the flight
  is seen from below it, else after the top.

**7.3.5 The game.**

- **`wa_feet`** is the floor under the player, zero until wave 3, and
  `eye = jz + WA_EYE + feet`.
- **The Game menu** gains "Heights arena, 40 columns" and "Heights arena,
  80 columns". *(Since NET-PLAN 9.4: the setup's Arena list.)*
- **The play bench** run in the heights arena cycles five poses, A to E,
  marked on the HUD (`wa_pbposes`: position, heading, base pitch, feet).
  Its tower and platform poses set `wa_feet`, so the raised eye is the
  game's own: no poke.

### 7.4 The contracts for wave 3 (pinned with the code)

Wave 3 also takes the owner's batch of 2026-11-13: one lift, not full
width; the maze made back into two hallways with no outside corner
sections; RAMPS instead of stairs, the stepped look kept only as a graphics
option that costs nothing when off; and any layout or speed suggestions.
The owner's sketch is "an idea, NOT to scale".

**7.4.1 The revised arena 2** (`tools/arenagen.py`'s `HEIGHTS_BOXES`; 4.1
stands for the room and the heights, not for the layout).

| box | x | y | h |
|---|---|---|---|
| annex fill, south / north (the room's corners west of the open area) | 0..640 | 0..384 / 1664..2048 | 384 |
| annex front (one face to the open area) | 576..640 | 576..1472 | 384 |
| annex block, south / north | 128..576 | 576..832 / 1216..1472 | 384 |
| **jump platform**, in the back room | 384..576 | 960..1088 | **48** |
| platform | 1088..1408 | 896..1152 | 80 |
| south ramp (rises N) / north ramp (rises S) | 1184..1312 | 704..896 / 1152..1344 | 0 → 80 |
| tower | 1856..2048 | 896..1152 | 160 |
| **the lift**, north of the tower, half its width | 1920..2048 | 1152..1280 | 16 → 160 |
| pillar, south / north | 1472..1600 | 384..512 / 1536..1664 | 384 |

The annex is one rectangle (x 0..640, y 384..1664) inside the room:

- a hallway runs west along each side of it (y 384..576 and 1472..1664);
- the corridor along the room's west wall (x 0..128) joins them;
- the back room (x 128..576, y 832..1216) opens off that corridor, between
  the blocks, and its east side is the annex front: one face to the open
  area, two corners, where the old front had three faces and six.

Totals:

| | wave 2 | wave 3 |
|---|---|---|
| boxes | 19 | **13** |
| tree nodes | 37 | 25 |
| vertices | 70 | 51 |
| x / y values | 16 / 18 | 14 / 17 |

The start (1024, 480) is clear. The picture is
`tools/arenagen.py`'s layout plotted from above (9.7.2).

**7.4.2 Ramps.** A box of kind 1 (`FLIGHT`) is a RAMP, kept in the same
record (7.2.1):

- its top runs from 0 at its low face to its height at the face it rises
  TOWARD, linearly;
- its tread count stays in the kind byte, for the stepped look.

The floor on it is `h × num / len` (the sim's `wa_hat`, arenagen's
`height_at`):

- `len` is its extent along the slope;
- `num` is how far the square under the walker reaches along it from the
  low face.

**Drawn** (`wl_flight0`):

- its low face's two corners stand on the floor (the floor's pair);
- its sides run straight from there to its top;
- the low face is one edge, its top being its floor edge (`wl_lom`);
- no treads.

**The T key** sets `wa_treads`, a FRAME byte. The renderer then draws the
ramp STEPPED, as wave 2 drew a flight: the low face a tread tall, and the
treads as lines across. Its cost when off is **one byte test per ramp a
frame**, in `wl_flight0`. Nothing per column or per pass reads it. The walk
is the ramp either way.

**A ramp's top edge is always drawn, its high end's included.** arenagen
drops a top edge only where a neighbour of the same height covers it. A
ramp's high end meets the platform at the platform's height, but it is the
only edge that closes the slope's columns. Dropped, the platform's face
behind it showed through the slope (found by `wacheck`, 9.7.4).

arenagen refuses:

- a ramp whose high end meets nothing within a step;
- a tread count outside 1..15;
- a tread rise over a step (`STEPUP`: the slope's limit);
- more than one lift.

**7.4.3 The floor, the step, the jump** (the sim, `wirearena.asm`). On a
heights arena (`wa_aflat` 0) the player's tick works on three quantities:

- **the floor** under the square (x ± `WA_RAD`, y ± `WA_RAD`): the tallest
  height of any box it overlaps, a ramp's at the square's highest point on
  it. `wa_floor` walks a table of the boxes WIDENED by the radius, built
  once an arena is loaded (`wa_lists`):
  - every box, for collision;
  - the low ones, for the player's floor;
  - the low ones widened by `WA_FRAD`, for a dummy's feet.

  Overlap is then a point inside, four compares against registers.
- **the feet**: `wa_feet` on the ground; in the air, `wa_feet` is a base and
  `wa_jz` (8.8) the height above it. `wa_jz` is rebased into `wa_feet`
  whenever it would go below 0, so a fall from any height stays in range.
- **the eye**: feet + `WA_EYE` + `wa_jz`, as before.

The rules:

- **A move collides** where the floor is more than `WA_STEPUP` (24) above
  the feet on the ground, or above the feet at all in the air. So a jump
  (apex 69) reaches the back room's 48 and not the platform's 80.
- **On the ground**, the feet follow the floor up (by at most a step,
  which the collision guarantees) and down a step. Past a step down, they
  fall.
- **In the air**, gravity as before. The feet land on the floor they come
  down onto (descending, at or below it).
- **The floor is read again** only when the player has moved or stands on
  the lift.

A flat arena keeps today's collision, jump and landing, unchanged (one byte
test a call).

**7.4.4 The lift** (3.2, one per arena). The ARENA section carries:

- its record's offset in `wa_bxh`;
- its rest and raised heights.

The sim moves its height in `wa_bxh`, which `wa_floor` reads, and copies
that one word into the renderer's `wa_bxh` every frame (`wa_rframe`).

- **Trigger:** standing on it, on the ground, for `WA_LWAIT` = 10 ticks
  (1/3 s).
- **Rise and return:** `WA_LSPD` = 5 a tick (1 s for 144).
- **Hold:** it comes back down after `WA_LHOLD` = 45 ticks (1.5 s) empty.
- **Riders** follow it by the step rule, since 5 is under a step.

The owner asked for start values shorter than 3.2's; they are constants.
The renderer draws it as the low box it is at its current height.

**7.4.5 Patrols per arena.** An arena part may carry its own patrols
(descriptor words 38 and 40: the block's offset and bytes, 0 = the
primary's `PATHS`). `wa_loadarena` makes the block's offsets into
addresses (`wa_aptab`, `wa_pptr`).

- **Arena 2's three patrols:**
  - up the south ramp, across the platform and down the north one;
  - round the annex's hallways and corridor;
  - round the tower.
- **Leg check:** arenagen samples every tick of every leg. No rise or drop
  may exceed a step, and no leg may cross a wall or the lift.
- **Climbing legs:** a leg that leaves the ground anywhere carries bit 15
  in its heading word. `wa_figz` reads the floor only on such a leg. Any
  other leg is the ground, unread.

**7.4.6 Figures at their floor** (3.3).

- **The record:**
  - `F_Z` is at 18, among the bytes copied G → R every frame
    (`WA_FIGHEAD` 20);
  - the rest moves up two, so `F_SIZE` is 42;
  - `WR_ABI` is 3.
- **Drawing:** the renderer draws a figure with its own eye, `wa_feye`
  = `wa_eye` − `F_Z`. Every joint and the far marker read it.
- **The narrow joint path** is taken only for `wa_feye` ≥ 0. Its bound,
  the camera eye's, covers any smaller eye.

**7.4.7 The pitch**: `WA_PITCHU` 112 up, `WA_PITCHD` 290 down
(`waconst.inc`), on every arena (7.1.1, 7.1.2).

**7.4.8 The frame block's ARENA section** grows (R writes, R → G at
`wa_loadarena`):

- `wa_aflat`;
- `wa_ahmax`;
- `wa_bxh`, moved out of the renderer's BSS;
- the lift's three words;
- the patrol block (`wa_apn`, `wa_apath`, `WA_APATHSZ`).

FRAME's spare byte became `wa_treads`.

## 8. The instruments behind the numbers

Wave 0 made them tools. The working scripts first kept in `tools/wamodel/`
are gone, their substance carried over.

- **`tools/watrace.py`**: the breakpoint trace of section 2.1, and with
  `--analyse` its per-piece table and span regression. Run from a worktree
  root, on the split build.
- **`tools/wamodel.py`**: the renderer model (7.2.6).
  - It reads the arenas from `tools/arenagen.py` and keeps no layout of its
    own.
  - It prices today's walk, the heights renderer and the outline path at the
    unit costs Measured (`C`) and Forecast (`CF`).
  - It scales into fps against the P bench measured on the same tree.
  - `--arena N [--width 40|80] [--variant G|A]` prints the pose sets of
    section 2.3 for the demo, or the P bench and a floor sample for a flat
    arena.
  - `--budget N` prints the arena's most boxes and outline edges in view,
    the check `arenagen --arena N` makes (4.2).
  - Its variant A prices today's span for every edge with each flight as
    one box. Section 2.4's A had four step boxes a flight, so A here is
    kinder than the table there.

---

## 9. As built: waves 0 to 3 (2026-11-13, branches `heights` from `20d0cf89` and `wave3`)

### 9.1 Wave 0: the host tools and the demo arena

- **`tools/arenagen.py`** takes the box schema of 7.2.2.
  - Boxes may stand against the room wall.
  - The arena parts carry the box records and the lift table of 7.2.1.
  - New refusals:
    - a flight whose tread or top rises more than the step-up (24 units);
    - a lift resting above a step, or rising to no neighbour's top;
    - a platform above the jump that no flight or lift reaches;
    - an arena over the visible budget.
  - The jump's apex comes from `waconst.inc` by the game's own
    integration: **69 units**, not section 3.1's 64.5 from v²/2g.
- **Arena 2** is section 4.1's layout:
  - 19 boxes, a 37-node tree, 70 vertices, 16 x and 18 y values and 7
    coordinate steps;
  - its part `waa2.bin` is 1,032 bytes;
  - it is a fourth parts row, and not on the Game menu (7.2.2).

  Its face masks come out as section 1.7 expects:
  - the tower's and the lifts' faces against the east wall are dropped;
  - so are the lifts' faces against the tower and the flights' top ends
    against the platform;
  - the building's shared faces and the tops between its equal blocks are
    dropped;
  - the platform's faces behind the flights stay, because they show above
    the top tread.
- **`tools/wamodel.py`** reproduces section 2.4's G forecast exactly: the
  same model, now on `arenagen`'s own arena. **`tools/watrace.py`** is the
  trace.
- **The working tables** follow the largest arena (`WA_MBOX` 19, `WA_MVTX`
  70, `WA_MNODE` 37): +1,018 bytes of the renderer part's BSS. Posed text
  VRAM of both current arenas was unchanged (c40 at 60°, 100 poses; the
  open arena at 80 columns, 60 poses).

**A correction to section 4.2.** The visible budget of ~15 boxes is broken
by this arena's own room corners. From (2016, 96), looking across, all 19
boxes are kept. The forecast there is ~11 fps at 40 columns, the same as the
tower's (Forecast: `wamodel` over the budget's poses, worst 11.1 fps).

So the arena's worst views are not only the tower's. The check is set at
20 boxes and 40 outline edges, which this arena meets (19 and 36). It is
still a check, and the owner's 7.1 accepts the tower's rate. **The room
corners should be named to the owner beside it.**

### 9.2 Wave 1: the eye, the pitch, the column state

- **The eye.** `wa_numb`, the bottom's eye × kyq, is a (product, shift)
  pair (7.2.3).
  - `wa_numt` needs none: |3 m − eye| × kyq is at most ~21,000 for any eye
    today.
  - Per height in wave 2 it will need one. That is where a general routine
    belongs, so none was kept.
  - The floor grid takes the pair.
  - The figures' narrow sum is bounded by `wa_fvn`: 128, the same every
    frame of today's arenas, with a divide only when the eye is above 128
    (or the horizon below half-row 61, which ±112 never reaches).
- **The pitch** is clamped at ±112 (`WA_PITCH`, `waconst.inc`).
- **The column state**, per 7.2.5:
  - `wa_cclip` (renamed: `wa_clip` is the near-plane clipper);
  - `wa_cstn` and `wa_cst`, cleared each frame on an arena that is not
    flat;
  - `wa_hflat` and `wa_hmax`, read from the arena part;
  - `wa_occl` and `wa_occlow`, asked by the pistol's ray, the floor grid
    and the far figures' bar;
  - the near figures through `wa_fthrs` and `wa_frowv`. A figure's sweep
    (`FEMIT`) is assembled twice, flat and heights, so a flat arena pays one
    compare a figure.

**Correctness** (MartyPC `os8088_5150_cga`, H off,
`pdump_noh.py`-style posed sets against `20d0cf89`):

- **The posed sets:** 720 poses over both arenas, both widths and all three
  fields of view. **708 are identical.**
- **The 12 that differ** are exactly the poses whose eye × kyq passes 32,767:
  eye 146–150 at 60°. Of the 720 poses, 12 have that, and all 12 changed.
  That is the overflow the fix is for: their floor lines wrapped above the
  walls' tops.
- **The heights paths:** with `wa_hflat` forced to 0 every frame, three
  sets (300 poses) are identical to the flat run. That forced run takes
  `FEMIT 1`, `wa_fthrs`, `wa_frowv` and `wa_occlow`, confirmed by
  breakpoints.
- **The jump at 60°:** the before and after screenshots are in the
  session's scratch directory (`shots/jump60-before-after.png`).

**Speed.** P and B are the benches; render cycles are per posed frame:

| | 20d0cf89 | wave 1 |
|---|---|---|
| P bench, 40 columns | 18.9 fps (MS 52.7, R 45.3) | 18.9 fps (MS 52.9, R 45.5) |
| P bench, 80 columns | 15.7 (MS 63.3, R 51.8) | 15.7 (MS 63.5, R 52.0) |
| B bench, 40 columns, full / sparse | 33.1 / 36.3 | 33.0 / 36.2 |
| render cycles, 60 posed frames (the same poses, interrupts left out) | — | **+640 clk a frame (+0.4%)** |

That +640 is the cost of:

- the bottom's variable shift, ~10 clk a vertex;
- the narrow bound;
- the flat tests: one a frame, two a figure;
- the far figures' query as a call.

**Bytes.** The renderer part grows 46,214 → 48,903:

| | image | BSS |
|---|---|---|
| wave 0 | — | +1,018 |
| wave 1 | +624 | +1,047 |

Of wave 1's BSS, the column state is 960 bytes and the figures'
thresholds 160. The game grows 10 bytes of image (a parts row) and 80 of
BSS (`wa_boxes` at 19 boxes). The arena parts are 534, 261 and 1,032
bytes.

### 9.4 Wave 2: heights drawn (static)

Built to 7.3. Arena 2 is on the Game menu at both widths *(the setup's
Arena list since NET-PLAN 9.4, and its default)*. Its heights are
fixed: the lifts rest at 16 and the player's eye is the floor's, except at
the bench's poses (7.3.5).

**9.4.1 The flat arenas** (MartyPC `os8088_5150_cga`, H off):

- **Correctness.** The posed sets against wave 1 (`b9037950`): 720 poses
  over both arenas, both widths and all three fields of view. **All 720 are
  identical.**
- **Speed.** It moved by 0.1 fps.

| | wave 1 | wave 2 |
|---|---|---|
| P bench, 40 columns | 18.9 fps (MS 52.9) | 18.8 (MS 53.1) |
| P bench, 80 columns | 15.7 (MS 63.5) | 15.6 (MS 63.7) |
| B bench, 40 columns, full / sparse | 33.0 / 36.2 | 32.8 / 36.1 |
| render cycles, the same 60 posed frames | — | **+420 clk a frame (+0.25%)** |

The +420 is:

- one test a span (`wa_gen`);
- the flat test at the general path and at the leaves;
- the room's flat test;
- the per-frame setup of the pair.

The faces, boxes and leaves were split into flat and heights copies
(`FACEBODY`, `BOXBODY`, `LEAFBODY`) to keep it this small. **It is 0.5–0.6%
on the benches, against wave 1's own 0.5% bar.**

**9.4.2 The heights bench** (Measured, MartyPC `os8088_5150_cga`; the
play bench's circle at each pose of 7.3.5), against `tools/wamodel.py
--arena 2 --bench`:

| pose | 40 columns, measured | 40, model | 80 columns, measured | 80, model |
|---|---|---|---|---|
| A, the busiest floor (800, 400) | **8.7** | 15.2 | **7.1** | 11.7 |
| B, the open floor, across the platform (1600, 1024) | **7.7** | 11.5 | **6.0** | 8.8 |
| C, on the platform (eye 182) | **18.6** | 19.3 | **15.2** | 16.0 |
| D, on the tower (eye 262, pitch −40) | **7.8** | 12.0 | **6.2** | 9.4 |
| E, in the hallway | **24.4** | 23.0 | **19.6** | 19.0 |

The raised eyes are the bench's own: its C and D poses set `wa_feet`.

**Where the model is right and where it is wrong.** It is right where few
low boxes are in view (C, E). It is 35–43% optimistic where many are (A, B,
D).

A breakpoint count at pose A (`lcount`) prices the outline path against
the model's units:

| | measured | model's forecast |
|---|---|---|
| a pass's setup | ~3,100 clk | 700 |
| a column of a pass | ~880 clk | 180 |
| a low box's prelude (corners, pairs, seen faces) | ~11,000 clk | 5,200 |

The column is the largest of them. Each cell is:

- a clip test;
- a `WPLOT`;
- for a top edge, a clip write and a stack push;

and the two edges of a face are walked together. The model priced it as
today's span column.

**Already taken against it:**

- the four `WLLOOP` loops (one edge or two, shallow or steep);
- the floor pair once a frame;
- each box's pair once a box.

Together they took pose A from 7.6 to 8.7.

**9.4.3 `tools/wacheck.py` with heights** (`--arena 2`). The host reference
casts each column as a ray through the boxes at their heights, flights as
their true steps. Its poses cover the free floor, the platform's top, the
tower's and the room's corners.

| | poses | columns off | not at a flight's side |
|---|---|---|---|
| 40 columns | 60 | 46 of 2,338 | 9 |
| 80 columns | 40 | 89 of 3,110 | 7 |
| today's arena (unchanged) | 30 | 6 poses with an edge two rows off, as before | — |

- **At a flight's side** the renderer draws the straight stringer of 7.1's
  Q3 and the reference the true steps. That accounts for most of the
  columns off.
- **The rest** are single columns at a near corner, the kind today's arena
  has.
- **A stringer whose low end is clipped by the near plane** is projected
  from the clipped point at the flight's low height. It runs straight to
  the top, a slightly different line from the unclipped one.

**9.4.4 Bytes.**

| | image | BSS | part |
|---|---|---|---|
| renderer part, wave 1 | 17,657 | 31,246 | 48,903 |
| renderer part, wave 2 | **22,115** | **31,716** | **53,831** |
| of which `wrlow.inc` | 2,364 | 140 | |
| of which the clip-tested and heights copies in `wrender.inc` | 2,094 | 82 | |
| of which the span table, 96 → 127 spans a frame | — | 248 | |

The span table grows because every outline pass registers a span tag (its
stack entries name it). 127 is the most a tag byte (x 2) indexes. The game
grows 7,125 → 7,319 bytes (+194: the menu items, `wa_feet`, the bench's
pose table). The arena parts are unchanged.

**9.4.5 The figures, the floor grid and the pistol** ask `wa_occl`, as wave
1 wired them. Wave 2 is the first to WRITE low entries, so the figures
show above a low platform and stop at its top. The screenshot is in 9.4.6.

**9.4.6 Screenshots** (MartyPC `os8088_5150_cga`, posed by `shotpose.py`
in the session's scratch directory). Each is in `shots/` at both widths
(`-40`, `-80`):

| view | file |
|---|---|
| the platform with its two flights, from the floor | `w2-platform`, `w2-platform-side` |
| the hallway's entrance, and inside the hallway | `w2-hallway-entrances`, `w2-hallway-inside` |
| the back room's jump platform | `w2-backroom-jump` |
| the lifts and the tower, from the floor | `w2-lifts-tower` |
| the view from the tower, looking down | `w2-from-tower` |
| a figure behind the platform, showing above it | `w2-figure-behind-platform` |
| all of these, stacked | `w2-sheet-40`, `w2-sheet-80` |
| the dead ground of 7.1.1: the tower at −290 and −112, the floor at +112 | `w3-tower-down-290`, `w3-tower-down-112`, `w3-floor-up-112`, `w3-deadground-sheet` |

**The flights as one object** are the owner's to judge from
`w2-platform`. Each tread is one line and the sides are straight.

**9.4.7 What the plan got wrong.**

1. **The outline path's unit costs are 3–4× the forecast** (9.4.2). Section
   2.4's G forecast is too optimistic wherever many low boxes are in view.
   The tower is not the worst view; the open floor looking across the
   platform is as slow (B, D).
   The levers for wave 5, before section 2.5's:
   - a column loop that tests the clip once a run rather than once a cell;
   - top and floor edges walked as one span pair when both are unclipped;
   - the prelude's corner projections shared with the tall path's vertex
     cache.
2. **7.2.3 said `wa_numt` needs no pair.** It does, per height, as wave 1
   expected. It is `wa_hnt`, cached by height.
3. **Not the plan's error, but worth keeping:** a view of the hallway's
   entrances from straight in front shows nothing. The building's 6 m wall
   fills the view from 4.6 m out, its top is off the screen, and only its
   foot shows. The entrances read when seen obliquely
   (`w2-hallway-entrances`).
4. **9.1's room-corner caveat stands.** The corners keep every box in view.
5. **The flat arenas pay 0.5% for the heights arena's presence.** Wave 1
   paid 0.4%, so it is about 0.9% in all. The flat body is otherwise today's.
6. **7.1's Q4 assumed ±112 could be made symmetric.** It cannot be with one
   limit, because body and eye heights differ. 7.1.1 is the derivation:
   **the down limit should be 290, the up limit stays 112**, from wave 3.

### 9.5 The open-area hang: a divide fault in the outline path (fixed)

**The report** (the owner, a 5150 on 86Box, CGA, "Heights arena, 40
columns"):

- any view into the open area, from the start turned slightly right
  onward, ran for about half a second;
- then one frame showed every few seconds, and keys, the mouse and the
  clock stopped;
- the HUD read `FPS 000.0 MS000.0 R040.7 P006.0`;
- MartyPC never showed it.

**The cause.** `wa_lowbox` tested "is this a flight" as `kind & 3`
non-zero. A LIFT (kind 2) passed that test, so `wl_flight0` divided the
lift's height by the bits a flight keeps its tread count in. For a lift,
those bits hold its index in the lift table, and the south lift's index is
0. The result was **`div cx` with CX = 0 (`wl_flight0`), every frame the
south lift's box was walked**. The leaf walk visits it from most of the
arena, and from the start once turned about 20° right of north.

(Measured, MartyPC: an exec breakpoint on the int 0 vector, `div0.py` in
the session's scratch `hang2/`. The first hit was pose (1120, 480), heading
200, at `wl_flight0+38`, with AX = 16, the lift's height, and CX = 0.)

**Why only on the 5150.** On a shipped kernel, int 0 goes to the BIOS's
catch-all handler.

- **On MartyPC's GLaBIOS** the fault is silent. The 8088 returns past the
  `div`, the rise is garbage, and a 16-unit lift hides it.
- **On the IBM ROM** (`ibm5150_5700051`, vm/pc5150) the handler reads the
  PIC's in-service register and MASKS whatever level it finds there.

Measured on 86Box (vm/pc5150 with CGA, 256 KB, the ST-11M):

- after a few turns to the right, the BIOS tick at 0040:006C stood still
  across a second of emulated time;
- INTR_FLAG read 10h;
- the HUD read `FPS 000.0 MS001.0 R002.9`.

`pit_now` then wraps every 54.9 ms. R and P still read under it, the
whole frame reads as about 0, the sim almost never accrues a tick, and the
keys, polled in the tick, freeze.

**The fix.** `wa_lowbox` keeps the kind only for a flight (kind & 3 = 1)
and zeroes it otherwise. A lift draws as the solid low box it rests as
until wave 4 raises it. Every flight path keys off that one byte, so the
whole class (a lift or a solid box reaching flight code) is closed at
once.

`tools/arenagen.py` now refuses a flight with fewer than 1 or more than 15
treads: the renderer divides by the count, and it is a nibble.

**The instrument that keeps it closed.** `tools/wacheck.py --arena 2`
clears INTR_FLAG before every pose and fails a pose that leaves it set: a
divide fault, or any other unexpected interrupt.

- Before the fix: 12 of 20 poses faulted.
- After the fix: none. That holds for 60 poses at 40 columns and 40 at 80.

**The class, after the fix** (MartyPC, `div0.py`): an exec breakpoint on
the int 0 vector across the whole arena found **no hit**:

- at 40 columns, a 256-unit grid, 8 positions × 8, at 8 headings
  (512 poses);
- at 80 columns, a 384-unit grid at 8 headings (200 poses).

Unchanged by the fix:

- the flat arenas' 720 posed frames, identical to wave 1;
- the P, B and heights benches, as in 9.4;
- the renderer part, 12 bytes larger (53,843).

**Evidence** (the session's scratch directory, `hang2/`):

| | file |
|---|---|
| 86Box before the fix: the tick stops and the HUD goes to zeros | `b4.png` |
| 86Box after the fix: the same turns, through the lifts | `f4.png` |

**What the other suspects came to:**

- The task-0 stack goes 48 bytes below the render's SP at open-area poses,
  with 382 bytes left above `STK0_BOT`.
- Past the part's image and BSS, in 275 open-area poses, the only bytes
  that changed were the game's own frame timers. The game writes them
  itself, between the render and the present; a write watch named the
  writer.
- The game's region starts right after the part's, so an out-of-bounds
  write would have shown.

### 9.6 The heights speed pass (2026-11-13)

Three agents worked from `5f8c7514`: `hs-cols` on the outline path's
per-column loops, `hs-box` on low-box and flight setup, and `hs-pass` on
pass setup, the tall faces' clip passes, the figure queries and the flat
arenas' overhead. `hs-pass` then integrated the three as `hs-merge`.

**What landed:**

- **The pass cull (`hs-pass`).** At pose A, 13 of 27 outline passes drew
  nothing and cost ~3,100 clk each. `wl_draw` now scans the highest half-row
  either edge reaches against each column's clip before any divide, and
  drops the pass. This is exact: the interpolation stays between the ends.
- **Column loops (`hs-cols`).** One clip test per edge. The colour and the
  column countdown are kept in registers. The steep fill is tested on whole
  half-rows. A pass wholly below the screen skips its setup. Loop work
  fell 39%.
- **Setup (`hs-box`).** Corners are 16-byte records, and only the corners in
  use are projected. Treads step by addition, and a tread inside the view
  skips `wa_clip`.
- **The flat arenas' overhead is about halved.** The eye's products are
  remade only when the eye or kyq changes. `.edge` is faster. The figure
  queries skip rows above the lowest partial threshold.
- **A drawing bug fixed (found twice, independently).** `wl_treads` lost the
  tread number across `wl_lowseen`. So a flight seen from its low end (the
  usual view from the floor) drew every inner tread as one phantom line in
  the wrong place. `wacheck --arena 2` went from 46 to 32 columns off at 40
  columns, and from 92 to 70 at 80.
- **Dropped at the merge:** `hs-box`'s whole-box cull (`wl_hidden`) and its
  per-face test. With the pass cull underneath them, they cost more than
  they saved: mean render 247,808 clk with them, 244,641 without, over 260
  posed frames.

**Measured on MartyPC `os8088_5150_cga`:**

| | 5f8c7514 | merged |
|---|---|---|
| heights bench, 40 columns, A/B/C/D/E | 8.7/7.7/18.6/7.8/24.4 | **10.4/9.1/20.4/9.0/24.5** |
| heights bench, 80 columns | 7.1/6.0/15.2/6.2/19.6 | **8.3/7.0/16.9/7.2/19.6** |
| P bench, 40/80 | 18.8/15.6 | 18.9/15.7 |
| B bench, full/sparse | 32.8/36.1 | 33.0/36.2 |

The forecast (2.4) was 15.2/11.5/19.3/12.0/23.0 at 40 columns. The busy
views (A, B, D) are still ~30% short of it.

**Correctness:**

- The flat posed sets (720 frames) are byte-identical to `5f8c7514`.
- The heights posed set (260 frames) is identical to the tread fix's
  picture at every step of the merge.
- `wacheck --arena 2` has 0 divide faults.

**Bytes:** the renderer part is 54,242 (+399 on `5f8c7514`).

**What is left, by the agents' measurements:**

- Flight treads cost ~2.4k clk each in `wa_clip` and `wl_proj` before
  `wl_draw` can cull them.
- Posts are ~14k clk at A.
- A culled pass still costs ~700 clk.
- Runs of shut columns are stepped one at a time in `FLATPASS`.
- A cheaper whole-box cull would pay if it ran only for boxes spanning many
  columns.

### 9.7 Wave 3: walking on heights, ramps, the revised arena (2026-11-13, branch `wave3` from `a36d061f`)

Built to 7.4. Every measurement is MartyPC `os8088_5150_cga`; the scripts
are in the session's scratch directory, `w3/`.

**9.7.1 What landed.**

- **The arena:** the revised arena 2 (13 boxes, 7.4.1), with its own
  patrols (7.4.5).
- **Ramps** for flights, drawn and walked (7.4.2):
  - the stepped look is the T option, one byte test per ramp a frame when
    off *(since 2026-11-15 no key: Very High's, 9.11)*;
  - a ramp's high-end top edge is always drawn.
- **The sim** (7.4.3):
  - the floor;
  - the step up and down;
  - the jump that reaches the back room's 48 and not the platform's 80;
  - the fall, the landing, the eye following the floor.
- **The one lift** (7.4.4), ridden: wave 4's ride.
- **Figures at their floor**: `F_Z`, `wa_feye` (7.4.6).
- **The pitch** up 112, down 290 (7.4.7).
- **Tools:**
  - `wacheck --arena 2` casts ramps as slopes and pitches across the whole
    clamp; `--treads 1` checks the stepped look for faults;
  - `wamodel` prices ramps (no treads) and the new poses;
  - arenagen checks every patrol tick for steps.

**9.7.2 The layout** is plotted from arenagen's own records, with the
patrols, the start and the bench poses: `w3/shots/layout.png`.

**9.7.3 Walking, jumping, the lift, the pistol** (scripted held keys on the
game's own sim, `walk.py`, sampled every 0.25 s or every tick; `shoot.py`
for the pistol):

| run | what the guest's state did |
|---|---|
| W from (1248, 560) north | feet 0 → 15 → 48 → 80 up the south ramp, across the platform, down the north ramp to 0; eye = feet + 102 throughout |
| W into the jump platform | stopped at x 360 (its face less the radius) |
| W + Space at it | rose (jz 13,265 → 17,685), landed on it: feet 48, eye 150 |
| W + Space at the platform | could not get on (apex 69 < 80); the in-air collision has no step allowance |
| W off the platform's east edge | fell 80 → 0 in ~0.5 s, the feet rebased each tick |
| onto the lift from the north, standing | after 1/3 s it rose 16 → 160 in ~1 s with the feet on it; walked south onto the tower at 160; the lift went back to 16 after 1.5 s empty |
| W off the tower | fell 160 → 0 in ~0.75 s |
| shot over the platform at a dummy beyond it (pitch 0) | **hit** |
| shot into the platform's lip at the same dummy (pitch −13) | **miss**; the same pitch with no lip between: **hit** |

**The see-but-not-hit check of 7.1.1, from both ends:**

| from | dummy | result |
|---|---|---|
| the tower's lip, pitch −290 (eye 262) | on the floor, 1.25 m from the player | shows at the bottom of the screen, **miss** |
| the tower's lip, pitch −290 | on the floor, 1.9 m | **hit** |
| the floor 1.2 m from the tower, pitch +112 | 0.94 m back on the tower (`F_Z` 160) | shows above the crosshair, **miss** |
| the floor 1.9 m out, pitch +112 | 0.5 m back on the tower | **hit** |

Screenshots: `w3/shots/dg-sheet-40.png`, `dg-sheet-80.png`.

**9.7.4 Correctness.**

- **The flat arenas:** the posed sets (720 frames, both arenas, both
  widths, three fields of view) are byte-identical to `a36d061f`.
- **`wacheck --arena 2`**, poses across the whole pitch clamp, 0 divide
  faults everywhere:

  | run | columns off | rate |
  |---|---|---|
  | 40 columns, 60 poses | 28 of 2,340 | 1.2% |
  | 80 columns, 40 poses | 13 of 3,060 | 0.4% |
  | stepped (T), 20 poses | 30 of 767 | (expected: the reference is the ramp) |
  | today's arena | 6 poses in 30, as before | — |

  - The columns off are single columns at a near corner, as on today's
    arena.
  - Ramps are exact, so wave 2's flight-side exemption counts nothing now.
  - `wacheck` found one real error on the way: the platform's face showing
    through a ramp from above, fixed by drawing the ramp's high-end top
    edge (7.4.2).
- **Every divide the wave adds is proved:**
  - `wa_hat`: the quotient is at most the ramp's height, and the divisor is
    its extent, never 0;
  - stepped `wl_flight0`: tread count 1..15, which arenagen enforces;
  - `wl_going`: guarded by at least 2 treads.

**9.7.5 Speed.**

| | a36d061f | wave 3 |
|---|---|---|
| P bench, 40 / 80 columns | 18.9 / 15.7 (MS 52.9 / 63.5) | 18.8 / 15.6 (MS 53.1 / 63.7) |
| B bench, full / sparse | 33.0 / 36.2 | 33.0 / 36.2 |

The flat cost is ~0.2 ms a frame:

- the dummies' F_Z through the renderer: ~200 clk a frame, from the
  figure-pose deltas without a timer tick in them;
- the sim's flat tests: +180 clk a tick (4,472 against 4,291).

The heights bench, measured against `wamodel --arena 2 --bench`:

| pose | 40, wave 2 arena (9.6) | **40, wave 3** | 40, model | 80, wave 2 arena | **80, wave 3** | 80, model |
|---|---|---|---|---|---|---|
| A, the busiest floor | 10.4 | **12.8** | 17.0 | 8.3 | **10.3** | 12.9 |
| B, the open floor across the platform | 9.1 | **13.7** | 16.2 | 7.0 | **10.0** | 11.9 |
| C, on the platform | 20.4 | **25.9** | 23.6 | 16.9 | **20.5** | 18.9 |
| D, on the tower | 9.0 | **11.8** | 15.1 | 7.2 | **9.2** | 11.5 |
| E, in a hallway | 24.5 | **25.0** | 25.1 | 19.6 | **20.5** | 20.1 |

E moved: the old hallway is gone, and E is now in the annex's north
hallway looking west.

- **The model** is right on C and E and 15–25% optimistic on A, B and D, as
  in 9.6.
- **The revised arena** is worth 23–51% on the busy views.
- **The sim nearly undid it.** A naive floor walk over every box cost
  **14,267 clk a tick** at pose A, against 5,008 on a flat arena. Three
  changes brought it to **5,572**:
  - the widened tables;
  - the floor read only for low boxes;
  - a dummy's floor read only on a leg that leaves the ground, and the
    player's only when they moved.

  At that cost the sim is ~1.6 ms of a frame at 12 fps (the HUD's MS less
  R and P).

**9.7.6 Bytes.**

| | a36d061f | wave 3 |
|---|---|---|
| renderer part | 54,242 (image 22,500, BSS 31,742) | **53,863** (image 22,755, BSS 31,108) |
| game | image 7,319, BSS 603 | **image 8,278, BSS 1,241** |
| arena parts 0 / 1 / 2 | 534 / 261 / 1,032 | 538 / 265 / **848** |
| WARENA.O88 on disk | 20,982 | 21,695 |

- **The renderer's BSS shrinks** because its tables follow the largest
  arena, now 13 boxes, not 19.
- **The game's image** carries the sim's floor, collision, jump, lift and
  patrol code, +959.
- **The game's BSS** carries the three floor tables (480 bytes) and
  ARENA's additions.

**9.7.7 Screenshots** (the session's scratch, `w3/shots/`, each at `-40`
and `-80`; `w3-sheet-40.png` and `w3-sheet-80.png` stack them):

| view | file |
|---|---|
| the platform and its ramps, from the floor | `w3-platform-ramps` |
| the same from the side | `w3-platform-side` |
| the same with the stepped option (T) | `w3-platform-treads` |
| the annex's south hallway, looking in | `w3-hallway-south` |
| the annex from the open area's north-east | `w3-annex-front` |
| the corridor along the west wall | `w3-corridor` |
| the back room's jump platform, from the corridor | `w3-backroom-jump` |
| the tower and the lift | `w3-tower-lift` |
| from the tower, looking down | `w3-from-tower` |
| walking up the south ramp, four frames of the walk | `ramp-sheet.png`, `ramp80-sheet.png` |
| the see-but-not-hit check | `dg-sheet-40.png`, `dg-sheet-80.png` |
| the layout from above | `layout.png` |

**9.7.8 Suggestions for the owner** (Forecast, `wamodel`, bench poses at 40
columns; the model is 15–25% optimistic on the busy poses, so read the
differences, not the numbers):

| layout | A | B | C | D | E |
|---|---|---|---|---|---|
| as built | 17.0 | 16.2 | 23.6 | 15.1 | 25.1 |
| no pillars | **18.7** | 16.2 | 23.8 | 15.2 | 25.3 |
| ramps as wide as the platform (sharing its corners) | 16.5 | 16.1 | 22.7 | 14.7 | 25.1 |
| a tall pillar between the platform and the tower | 15.7 | 16.1 | 23.5 | 15.4 | 25.0 |
| the pillars moved to block the long diagonals | 15.7 | 16.2 | 23.6 | 13.2 | 25.1 |

- **In this renderer a box costs more than the view it hides,** unless it
  closes the WHOLE view. A partly hidden box is still set up and walked.
  So the cheap layout is the one with fewer boxes in the cone: **taking
  the two pillars out** is the one change that pays (+10% at A).
- **Wider ramps** cost more columns than their shared corners save.
- **Code levers still open** (9.6's): posts (a ramp's low corners are
  zero-length posts still set up), a culled pass's ~700 clk, and runs of
  shut columns in `FLATPASS`.
- **The back room** might want a door rather than an open alcove for feel
  (7.4.1 dropped the door's two jamb boxes as "corners that buy nothing").
  Seen only from the corridor, they would cost nothing in the open area.

**9.7.9 What the plan got wrong.**

1. **Section 3 did not price the sim.** A floor read over every box made a
   heights tick 2.9 times a flat one. It needed the tables, the low-box
   list, the leg flags and the cached player floor (9.7.5).
2. **3.1's step-up applied in the air** would let a jump (69) reach the
   platform (80): 69 + 24 > 80. In the air a move needs the feet above the
   floor at all.
3. **7.2.1's top-edge rule** (dropped where a same-height neighbour covers
   it) is wrong for a ramp's high end (7.4.2).
4. **The patrols could not wait for wave 5:** dummies standing at their
   floor need patrols that walk the heights.
5. **3.2's lift heights in FRAME:** one ARENA word, copied each frame,
   sufficed.
6. **The forecast** stays optimistic on the busy views (9.7.5). C and E
   are right, now on the new arena too.

### 9.8 Speed round 5, architecture: the frame accounted, and a PVS (2026-11-13, branch `r5-arch` from `b507c171`)

The architecture agent of round 5 looked at the whole pipeline for work
that could be REMOVED rather than shaved. Three other agents tuned the
outline path, the tall walls and the figures, present and sim in place.

**9.8.1 The frame, accounted by measurement.** A whole-frame single-step
trace (`framet.py` in the session's scratch `r5/arch/`) runs the game's own
bench under MartyPC `os8088_5150_cga`. It steps from the main loop's head
through four whole iterations: sim, input, the frame-block copy, render,
present, HUD and the kernel's interrupts. `areas.py` and `split.py` give
the exclusive cycles of every routine. `leafwin.py` splits `wa_render` by
leaf (each box in walk order), the room, figures and setup. `incl.py` gives
inclusive costs by call window. The figures are guest cycles a frame at
`b507c171`, 40 columns:

| | P | A | B | C | D | E |
|---|---|---|---|---|---|---|
| **frame (clk)** | 253,746 | 371,170 | 330,814 | 186,406 | 380,469 | 181,713 |
| low boxes: per box (prelude, posts, flight, dispatch) | 0 | 51,035 | 36,903 | 19,412 | 49,210 | 0 |
| low boxes: per pass setup (`wl_draw`) | 0 | 25,212 | 22,345 | 3,834 | 24,197 | 0 |
| low boxes: per column (`wl_l*`, fills, shuts) | 0 | 40,289 | 61,492 | 0 | 46,614 | 20,657\* |
| tall: per box and face (leaf, box, face, vertex, clip, projection) | 65,418 | 72,243 | 62,472 | 51,821 | 82,592 | 39,594 |
| tall: per span setup (`wa_span`, corners) | 37,069 | 34,962 | 31,902 | 10,250 | 38,611 | 19,888 |
| tall: per column (flat passes, span loops) | 39,442 | 31,202 | 33,131 | 31,113 | 37,318 | 38,487 |
| frame setup (`wa_products`, clears, order) | 15,153 | 19,496 | 19,111 | 19,381 | 19,702 | 19,631 |
| figures, floor grid, crosshair | 56,764 | 40,603 | 4,572 | 12,827 | 17,324 | 10,911 |
| present | 22,981 | 29,468 | 34,620 | 20,100 | 38,597 | 15,702 |
| game: the sim (ticks, dummies, lift, floor) | 6,085 | 13,266 | 12,150 | 7,700 | 13,338 | 7,663 |
| game: input, HUD, loop, timers, glue, kernel | 10,723 | 13,395 | 12,115 | 9,969 | 12,967 | 9,179 |

\* E's is `wa_slope`, the tall walls' steep fills, which the low path shares.

At 80 columns, A is 455,551 clk and P 298,848. The columns' share grows
(low columns 71,252 and tall 52,938 at A) and so does present (55,823 at A,
42,841 at P).

**Fixed against per column.** At 40 columns the per-column loops are 15–30%
of a frame. Present, which is per listed cell, is another 8–11%. **Everything
else, 60–75%, is a fixed cost per object:**

- a low box pays ~10k clk of prelude, posts and dispatch, ~1.2k for its leaf
  and ~3.6k for its corners' transforms, before its first pass;
- a pass pays ~2.4k clk of setup before its first column, and ~770 clk a
  column (least squares, 9.8.6); a pass culled before its setup still costs
  ~630;
- a tall face pays ~4k clk (its vertices, face test, clip and projection),
  and its span ~3.5k more before the first column;
- a figure near enough for limbs pays ~20k clk.

Per leaf at pose A, the platform and its two ramps cost 107k clk (29% of the
frame): 50.6k for the near south ramp, 38.8k for the platform and 18.7k for
the north ramp. The tower is 31k, the lift 23k, a tall pillar 24k, the room
50.6k and two figures 45k. At B the platform alone is 77k, mostly columns.

**What the accounting rules out:**

- **Temporal coherence.** Every play frame moves the camera: the P bench's
  circle turns it every frame, as the mouse does in play. Every vertex's
  column and every edge's rows change. A frame-to-frame cache of
  projections or spans would pay its own checks and hit nothing.
- **Coarser columns at distance, or splitting the frame across two.** Columns
  are at most a third of a frame. Halving the far ones buys a few percent and
  costs the look.
- **Hierarchical cone tests.** A box the cone drops costs ~450 clk. Pre-filtering
  them by angle (per cell) saves under 2k clk a frame.
- **Lazy products.** Measured slower in 16.2, and `wa_products` is ~260 clk a
  coordinate.

**What does remove work: not visiting what cannot be seen.** At B, the jump
platform behind the annex's front costs 15.3k clk and the annex blocks
6.3k each. All of that work draws nothing. A low box has no test that
rejects it before its ~10k prelude, and a tall one pays its vertices
before its face test.

**9.8.2 The PVS.** Each cell of a 128-unit grid (2 m, 16 × 16 over the room)
carries the boxes that can draw anything from SOMEWHERE in the cell.
`wa_order`, which already caches the front-to-back walk per cell of the
tree's split lines, leaves the rest off the list. Its cell is now the grid
cell as well. A box off the list costs nothing: no cone test, no transform,
no prelude. On a frame that does not cross a cell the cost is **zero**. A
crossing pays one walk of the tree, a cell lookup and a bit test per leaf.

`tools/arenagen.py` (`pvs_masks`) builds it at make time, in about 1.3 s for
arena 2:

- **Only TALL boxes occlude.** A low box shows what stands behind it above
  its top. A tall box is at the arena's tallest, and arenagen refuses the
  PVS unless every eye the game allows is below that (`pvs_eyemax`: the
  highest floor, the eye, the jump). So occlusion is a 2D question.
- **A box draws only in columns whose centre ray meets it.** A column a
  nearer tall face covers is shut. So a box that no ray from the cell
  reaches, before a tall box in front of it, draws nothing.
- **Per point, an exact test.** For two disjoint axis-aligned boxes, the one
  on the point's side of a separating line is met first on any ray that meets
  both. So an occluder can hide a box only if the point is on the occluder's
  side of every such line. Then the box is hidden exactly when the union of
  those occluders' angular intervals covers its own.
- **Conservative against the renderer's rounding.** A box's interval is
  widened, and each occluder's narrowed, by 0.003 rad plus 5 / distance. A
  column's projection truncates by under 1/16 of a column, 0.0025 rad at the
  widest (90 degrees, 40 columns). A camera-space coordinate truncates by
  under a unit, which is under 2 / z on the screen. And a tested point is
  within 2 units of every point on the outline.
- **The points tested.** A ray from inside the cell, run backwards, leaves
  it or meets an occluder's face. So the points tested are the cell's outline
  and the tall boxes' outlines inside it (a unit out) every 4 units, plus a
  16-unit grid inside.
- **Which arenas carry one.** An arena carries a PVS only when it culls at
  least 10% of its box-cells. Arena 2's culls 31.4%; the flat arenas' 2.3%
  and 0%, so they carry none and do not change.

The part's descriptor gains a word at 42 (WIREARENA-PARTS-PLAN 7.5).
`WRV_ARENA` copies the masks into `wa_pvs`, 512 bytes of BSS
(`WA_PVSN` × `WA_PVSB`).

*Since (MULTIPLAYER-FPS-PLAN 16.10, 2026-11-14): the PVS has a second, LOW
pass - what the boxes below the tallest hide, at the highest eye a cell can
have - which drops each ramp from the cells where it lies behind the
platform.*

**9.8.3 Measured** (MartyPC `os8088_5150_cga`, the games' own benches):

Both columns were taken on the same harness and system disk; "before" is
`b507c171`. fps (MS):

| | before | with the PVS |
|---|---|---|
| P bench, 40 / 80 columns | 18.8 (53.1) / 15.6 (63.7) | 18.8 (53.1) / 15.6 (63.7) |
| B bench, 40 columns, full / sparse | 33.0 (30.3) / 36.2 (27.6) | 33.0 (30.3) / 36.2 (27.6) |
| heights A, 40 / 80 | 12.8 (77.6) / 10.3 (97.0) | 12.9 (77.1) / 10.3 (96.7) |
| heights B, 40 / 80 | 13.7 (72.9) / 10.0 (99.5) | **15.0 (66.4) / 10.7 (93.1)** |
| heights C, 40 / 80 | 25.9 (38.5) / 20.5 (48.7) | **31.3 (31.9) / 23.9 (41.7)** |
| heights D, 40 / 80 | 11.8 (84.4) / 9.2 (108.2) | **12.3 (81.0) / 9.5 (105.0)** |
| heights E, 40 / 80 | 25.0 (39.9) / 20.5 (48.7) | 25.1 (39.8) / 20.5 (48.6) |

The whole-frame traces agree: B goes 330,814 → 298,350 clk a frame
(−9.8%), C 186,406 → 149,878 (−19.6%) and D 380,469 → 362,106 (−4.8%).
`wamodel --arena 2 --bench`, which now walks with the PVS too, gives
17.1/17.4/26.2/15.4/25.1 at 40 columns.

What it culls at the bench poses: A the annex's north block and the jump
platform (both already outside the cone there); B both annex blocks and the
jump platform; C the same three; D the jump platform; E nothing.

**9.8.4 Correctness.** Text VRAM against `b507c171`, byte for byte:

- the heights posed set, 260 frames (the round's fixed `hpose.py`: `F_SIZE`
  42, the figures' `F_Z` poked, wave 3's pose E, no lift poses);
- a large random set, 510 poses at 40 columns and 410 at 80;
- a targeted set: for every (cell, box) the PVS culls, a pose in that cell
  (at its corners and edges, a player's radius in) turned toward the box, at
  a random eye and pitch. That is 550 poses at 40 columns and 550 at 80.

All were identical.

Two more checks:

- **The flat posed sets**, 720 frames, are identical as well. Arenas 0 and 1
  carry no PVS. Their leaves pass with one byte compare when the order is
  rebuilt, and on no other frame.
- **`wacheck`**: `--arena 2` 60 poses at 40 columns and 40 at 80, and the
  flat arena's 30. Each reports the same poses and the same columns as
  `b507c171`'s build, with 0 divide faults.

**The test goes red when the PVS is wrong.** I built it with a margin of
−0.01 rad, which wrongly culls slivers. Its own targeted set, 600 poses a
width, showed 1 pose wrong at 40 columns and 2 at 80: a post column missing,
seen past a pillar's edge.

**The margin has room.** A PVS with no margin at all (35.4% culled) passed
its own targeted set, 684 poses a width, with no frame differing.

**9.8.5 Bytes.**

| | `b507c171` | with the PVS |
|---|---|---|
| renderer part | 53,863 (image 22,755) | 54,530 (image 22,906): +667, of which `wa_pvs` 512 and its three cells 4 |
| arena 2's part | 848 | 1,366: the masks 512, their header 4, the descriptor's word 2 |
| arenas 0 and 1 | | +2 each (the descriptor's word) |
| `WARENA.O88` (the same packer flags) | 21,695 | 21,960 |

**9.8.6 What the frame says to do next** (each Measured above, priced
against the frame):

1. **The low box's passes: ~2,400 clk of setup and ~770 a column** (least
   squares over the 122 drawn passes of the A, B and D traces; the 90 culled
   ones cost ~630 each). That is more than twice a tall flat span's
   column (~320, 16.8). Merging a
   box's adjacent edges into one pass would not save the per-edge part: each
   edge still needs its own exact start, its own step and its own tag (the
   inverse depth is linear along one edge only). It would save the glue, the
   cull scan and the dispatch, ~1k clk a merge, so ~1–3% (Forecast). That
   is not worth the risk now.
2. **The low box's prelude, ~10k clk a box** (`wa_lowbox` alone is 6–7k):
   larger than any of its passes.
3. **Figures, ~20k clk each up close** — 22% of P's frame.
4. **The room, 45–50k clk at P and A:** 4 clipped faces. The two that draw
   little still cost 5–8k between them.
5. **A PVS grid of 64 units** culls 36.0% of arena 2's box-cells against
   31.4%. At the bench poses it adds only the annex's south block at D,
   ~6k clk (1.7%), for 1.5 KB more BSS and part. It is one constant,
   `PVS_SHIFT`, if a future arena wants it.
6. **For multi-area levels** (the owner's wish not to see across the whole
   map), the PVS is the mechanism, and its runtime cost does not grow with
   the map. A wall that fills a doorway, or a tall partition, culls whole
   rooms. Each box costs a bit per cell. At 31 boxes, `WA_PVSB` is 4 bytes
   and the table 1 KB.

### 9.9 Speed round 5, merged (2026-11-13)

Four branches from `b507c171`, merged without conflict:

- `r5-arch`: the frame accounted, and the per-cell PVS (9.8).
- `r5-low`: wrlow.inc.
  - Floor and top edges walked in separate loops.
  - Corners transformed and projected in one unrolled pass, and a box's
    height pair reused for the next box of that height.
  - Zero-length posts skipped, and the walks over faces unrolled.
- `r5-walls`: wrender.inc's walls.
  - A face with both ends in view draws straight from the vertex cache.
  - Heights' tall spans test the clip only where a low box raised one.
  - Steep spans skip steps that cannot draw.
- `r5-figs`: figures, present and the sim.
  - Byte-wide joint blends, and the figure header taken from tables.
  - The floor read skipped on ground legs, and one `pit_now` fewer a frame.
  - Present's redrawn cells fall through.
  - The joints reordered ends-first in arenagen.

**Measured on MartyPC `os8088_5150_cga`, the merged build, no look option on:**

| | b507c171 | round 5 |
|---|---|---|
| P bench, 40/80 | 18.8/15.6 | **19.3/16.1** |
| B bench, full/sparse | 33.0/36.2 | **33.4/36.6** |
| heights, 40, A/B/C/D/E | 12.8/13.7/25.9/11.8/25.0 | **13.8/15.6/34.1/12.9/27.0** |
| heights, 80, A/B/C/D/E | 10.3/10.0/20.5/9.2/20.5 | **10.9/11.1/26.6/9.9/22.0** |

**Correctness:**

- The posed sets are identical to `b507c171`: 980 frames, VRAM and the drawn
  figures' trigger fields. That is 720 flat frames over both arenas, both
  widths and all three fields of view, plus 260 heights frames.
- Mean render cycles per pose: −1.3 to −1.9% flat, −6.3% (40) and −6.8% (80)
  on heights.
- `wacheck --arena 2` (30 poses): 5 of 1,183 columns off, all single
  columns at a near corner, and 0 divide faults.
- The fast tier passes.

**Bytes:** the renderer part is 55,629 (+1,766 on `b507c171`), of which the
PVS is 667 and arena 2's part +518.

**The look options:** each comes with side-by-side videos, and they wait for
the owner's choice. All are off.

- **J (a key, `r5-figs`):** small figures under fv8 16 are drawn as 5
  straight limbs, with no elbows or knees. P is +0.5 fps.
- **Build knobs in wrlow.inc (`r5-low`):**
  - `WL_NOPOSTS`: no vertical corner posts on low boxes.
  - `WL_FARTOP=64`: a low box's far top edge is drawn only when the eye is
    64 or more above it. This gives heights B +2.3 fps, but things behind
    the top can show through.
  - `WL_RAMP2`: a ramp is drawn as its two sides only.
  - All three together: heights 40 gains about A +1.7, B +3.4, D +0.6.
- **Build knobs in wrender.inc (`r5-walls`):**
  - `WA_LOOK_ROUND`: rounded product high words.
  - `WA_LOOK_COARSE`: a shallow edge is drawn level.
  - `WA_LOOK_NOPOST`: no posts on walls under 8 half-rows tall.
  - `WA_LOOK_ROOMF`: the room's walls are drawn as floor lines only.
  - All four together: P 19.0 → 21.3 at 40 columns, 15.8 → 18.1 at 80, and
    heights +0.1 to 0.8.
  - Making a knob a key needs a frame-block flags byte and a handler.
- **Rejected by measurement:**
  - K: no blend for small figures.
  - Nearest post only.
  - Whole-box culls, twice more.
  - Jumping shut-column runs in the flat passes.
  - Temporal reuse: the camera turns almost every frame.
  - Coarser distant columns.
  - A frame cap.

### 9.10 The lift that jittered (fixed, 2026-11-13, branch `liftfix` from `e9ca8cff`)

**The report** (the owner, 86Box pc5150, CGA, 40 columns, the Heights
arena): "The ramps work well … The lift just 'jitters' me in place when I
stand on it, never lifting me up to the tower."

**Reproduced on MartyPC** `os8088_5150_cga` on the first try: walk onto the
lift from the north and let go of W. Read every tick off the game's own
variables:

- the feet went 16, 0, 16, 0, every other tick;
- `wa_fpx`, the cached floor's x, read **40** on the zero ticks. That is
  the lift's record offset (box 10 × 4), not an x.

**The cause** was in `wa_hjump`'s cached floor read (7.4.3). The floor is
read again only if the player moved, or if the cached floor's box is the
lift. The lift test loaded `wa_flrb` into AX and jumped to `.read` when it
matched. But `.read` takes AX = x and BX = y, so it stored 40 as `wa_fpx`
and read the floor at x = 40: the ground against the west wall.

- The feet stepped down to 0, and `wa_flrb` became the ground.
- `wa_ltick` saw nobody on the lift and reset its count.
- On the next tick x ≠ 40, so the floor was read correctly: the lift, 16
  (or 21 once it had started), and the feet stepped back up.
- The count never reached `WA_LWAIT`.

It worked only when the rider's x or y changed every tick. A moving rider
reads the floor through the other branch, where AX is right. Every still
case failed:

- keys up;
- turning with the mouse;
- W held into the tower's face (the collision puts y back);
- landing from a jump;
- standing on either edge.

The 40-column frame rate and the edges had nothing to do with it. Wave 3's
scripted ride (9.7.3) must have kept the player moving.

**The fix**: the lift test uses CX, not AX (+1 byte of the game's image).
CX is free there, and `.have` reloads it.

**Checked**: `tests/walift.py`, a soak row (`walift`, 14 s). It runs nine
scripted rides on the Heights arena. Each ends with the player STILL on the
lift, and each is checked tick by tick:

| ride | e9ca8cff | liftfix |
|---|---|---|
| backed on, every key up | feet 16/0, lift stays at 16 | up in 32 ticks (1.07 s), feet 160, no drop; off onto the tower; the lift home |
| walked on, the mouse turning the view | jitter, stays at 16 | up in 33 ticks |
| W held into the tower's face | stuck at y 1180, feet 31 | carried up, walks onto the tower by itself in 46 ticks |
| jumped on | jitter | up in 32 ticks |
| on its west edge (the square half on the ground) | jitter | up in 31 ticks |
| on its north edge | jitter | up in 39 ticks |
| walked off west mid-rise (lift at 80) | never rose | fell to 0, and the lift went home |
| back onto the raised lift from the tower | 41 of 90 still ticks off it | held up, feet 160, for 3 s |
| dropped onto it from the tower as it came down | landed at 25, never rose again | stopped under the rider at 30, back up in 33 ticks |

The row is red on `e9ca8cff` with 14 failures.

Riding it DOWN is not a ride, by 3.2's design: the lift holds while
occupied and returns only when empty. A rider who drops onto it as it comes
down stops it, and is carried back up a third of a second later.

**Nothing can be under it:**

- A player whose square overlaps it stands on it. Its height is their
  floor.
- Its side collides like a wall while it is above a step.
- No dummy reaches its column. arenagen keeps every patrol tick 12 units
  from a lift, and a dummy's floor square is `WA_FRAD` 8.

**The feet trail the lift by one tick**, by 7.4.4's step rule: `wa_ltick`
moves it at the end of the tick, and the floor is read at the next one. So
the eye is up to 5 units below the lift's top for one tick. This was left
alone.

**The flat arenas are byte-identical**:

- the posed sets, 980 poses (flat 720, heights 260), match `e9ca8cff`
  exactly, with render cycles within ±0.23%;
- the P bench is 19.3 at 40 columns, as before.

**The video** is in the session's scratch, `lift/wirearena-lift.mp4`:
before and after, one frame a sim tick, from MartyPC.

### 9.11 Detail levels (2026-11-13, branch `detail` from `e9ca8cff`)

Round 5's look options are now five **detail levels**, chosen at run time
by keys **1 to 5** *(F1 to F5 since WIREARENA-WEAPONS-PLAN 2.1; 1 to 6 are
the weapons)*. The HUD shows the level at column 37, where H's mark
was. They follow the owner's verdicts on the round-5 videos.

**The levels** (`wa_levtab` in `wirearena.asm`: a row per level, the
options as bits, so moving an option between levels is one line):

| level | key | options | grid (G) | treads (T) | floor lines | a small figure from |
|---|---|---|---|---|---|---|
| Very Low | 1 | Low + ROOMF + FARTOP | off | off | dashed, steep ones too | 464 units |
| Low | 2 | Medium + NOPOST | off | off | dashed | 464 |
| **Medium** (the default on an 8088) | 3 | J + ROUND + COARSE | off | off | whole | 464 |
| **High** (the default on a 286 or better) | 4 | Medium | **on** | off | whole | 696 |
| Very High | 5 | none: exact products and edges, full figures | on | **on** | whole | never |

The floor lines' column is `wa_dash`, a byte of its own beside the grid's
and the treads' (2026-11-15, [WIREARENA-ARENAS-PLAN](WIREARENA-ARENAS-PLAN.md)
4.5 and 5): a shallow line drawn in every other column at Low, and a steep
one in every other row too at Very Low. The owner, on a trial build: *"Dashes:
actually look surprisingly fine."*

The options, a bit each in `wa_look` (LK_ in `waconst.inc`):

- **J** (LK_FEW, `r5-figs`): a small figure (fv8 under 16) as its seven
  joints and five straight limbs. The owner: it "can become the default".
  - How small is the level's now (`wa_flodv`, CONFIG's, `wa_levtab`'s last
    column; 2026-11-15, ARENAS-PLAN 5): under 24 at Medium and below, 16
    at High. At the default 75 degrees that is from 464 units ahead and
    from 696 (depth, not range; a wider view brings both nearer). The
    owner, on trial builds at 24 and 32: *"figures24 at medium and below
    works; you can hardly tell at that distance simply because there are
    too few characters to display more data until they are right next to
    you."* Row `wafiglod` (soak) holds each level's line.
- **ROUND** (`r5-walls`): the products' rounded high words, a sum each for a
  vertex. "Can hardly see much difference."
- **COARSE**: an edge moving less than a half-row along a whole wall is
  drawn level, at its middle. "Can hardly notice."
- **NOPOST**: no corner posts on walls under 8 half-rows tall. "Not great
  but works better than always-nopost."
- **ROOMF**: the room's walls as floor lines alone. "Looks like a boundary
  line instead of a wall, but for an arena meant to look like that it
  works."
- **FARTOP** (`r5-low`): a low box's far top edge is drawn only with the eye
  64 or more above it. The owner was unsure, so it is at Very Low alone for
  now.
- **RAMP2** (`r5-low`): a ramp drawn as its two sides. "Likely ok": it has
  its **own key, R**, is in no level, and is off by default. *Since
  (WIREARENA-WEAPONS-PLAN 6.5, 6.7): it drops the ramp's foot line alone,
  and it is on at Very Low and Low, with no key of its own.*

**Dropped:**

- `WL_NOPOSTS` ("it deletes what defines the block");
- `WL_NEARPOST` and K (rejected by measurement in round 5);
- **H**, the held edges (no visible difference, and not repeatable): the
  hold code, its four loops and its 320 bytes of BSS are gone.

**Kept as keys:** *(until 2026-11-15, when G, T and L went - the grid
and the treads are set by the level alone: WIREARENA-NET-PLAN 9.9, and the
walls' tops' branch left the renderer, 9.10)*

- **G** and **T** are manual overrides. Choosing a level sets both; either
  key then flips its own until the next level is chosen.
- **L** (no wall tops) is a debug key in no level. At Very Low it buys
  little: P is 22.6/19.5 with it, against 22.1/18.7 without. And it reads
  poorly where ROOMF already leaves the room as lines.

**How they cost nothing when off.** The game's level sets `wa_look`, a
FRAME byte where `wa_hyst` was, and the grid and treads bytes. The renderer
reads them once per object or once per frame, never once per column:

| option | where it is tested | off costs |
|---|---|---|
| ROUND | `wa_products` (a frame); `wa_vert` or `wa_vertr` through `wa_vertp`, set once a frame by `wa_lookset`; the cone test's near margin, `wa_lnear` | ~8 clk a vertex, ~10 a box |
| COARSE | `wa_fedge`, only for an edge moving under a half-row | a byte test such an edge |
| NOPOST | the span's tail | a byte test a span |
| ROOMF | `wa_roomf`, set for the room's faces | a byte test a span |
| FARTOP | `wl_fartop`, a word `wa_lookset` makes 0 or 64; the box's two eye tests subtract it | the subtracts' memory reads |
| RAMP2 | before a flight's corners | a byte test a flight |
| J | the figure's head | the byte test it had (`wa_flod` before) |

The frame block keeps its layout: `wa_look` takes `wa_hyst`'s byte, and
CONFIG's `wa_flod` is a pad again, J having moved into `wa_look`. **WR_ABI
is 4** anyway, because the byte's meaning changed.

**Correctness** (MartyPC `os8088_5150_cga`; r5-figs' posed sets, 720 flat
and 260 heights frames, the level poked into the frame block):

- **No options, no grid, no treads** (Very High's options all off): VRAM
  and the figures' trigger fields **identical to `e9ca8cff` in all 980
  frames.**
- **Each level against the build-time knob build of its options at
  `e9ca8cff`, the same pokes, all identical over 980 frames:**
  - Very Low: `WA_LOOK_ROUND WA_LOOK_COARSE WA_LOOK_NOPOST WA_LOOK_ROOMF
    WL_FARTOP=64` + J;
  - Low: ROUND, COARSE, NOPOST + J;
  - Medium, and High with the grid: ROUND, COARSE + J;
  - Very High: the plain build with the grid and treads.
  - RAMP2 is identical to `WL_RAMP2` (at Medium).
- **`wacheck --level N`** (a new option: the level's key after the menu),
  at every level: **0 divide faults.**

  | level | today's arena (30 poses) | heights 40 (40) | heights 80 (30) |
  |---|---|---|---|
  | 1 | 28 off (ROOMF's missing tops, by design) | 429 of 1,560 columns | 654 of 2,336 |
  | 2 | 3 | 5 of 1,560 | 14 of 2,336 |
  | 3 | 3 | 5 of 1,561 | 15 of 2,339 |
  | 4 | 3 | 5 of 1,562 | 14 of 2,334 |
  | 5 | 6 | 7 of 1,563 (treads: expected) | 19 of 2,340 |

- The fast tier passes.

**Speed** (MartyPC `os8088_5150_cga`; fps; heights A–E):

| | P 40 | P 80 | B full / sparse | heights 40 | heights 80 |
|---|---|---|---|---|---|
| `e9ca8cff` | 19.3 | 16.1 | 33.4 / 36.6 | 13.8 / 15.6 / 34.1 / 12.9 / 27.0 | 10.9 / 11.1 / 26.6 / 9.9 / 22.0 |
| 1 Very Low | **22.1** | **18.7** | 36.6 / 38.0 | 15.7 / 18.3 / 34.6 / 13.1 / 27.7 | 12.7 / 13.2 / 26.8 / 10.0 / 22.8 |
| 2 Low | 21.3 | 17.6 | 34.4 / 37.7 | 14.0 / 15.8 / 34.6 / 13.1 / 27.1 | 11.0 / 11.2 / 26.8 / 10.0 / 22.1 |
| **3 Medium** | **20.5** | **17.0** | 34.3 / 37.7 | 14.0 / 15.8 / 34.6 / 13.1 / 27.1 | 11.0 / 11.2 / 26.7 / 10.0 / 22.1 |
| 4 High (grid) | 14.4 | 12.5 | 23.6 / 25.1 | 11.4 / 12.8 / 22.7 / 11.4 / 19.8 | 9.3 / 9.6 / 18.9 / 9.0 / 16.9 |
| 5 Very High | 13.7 | 11.9 | 23.0 / 24.5 | 10.1 / 11.1 / 22.4 / 10.0 / 19.6 | 8.3 / 8.4 / 18.9 / 8.1 / 16.8 |

- **Medium**, the default on the target, is P 20.5/17.0, against
  `e9ca8cff`'s 19.3/16.1.
- **High and Very High** carry the floor grid, which costs a 4.77 MHz 8088
  about a third of its rate. They are the 286's levels.
- **Very High's cost against High** is the exact products, exact edges
  and full figures: P 40 −0.7 fps. On heights the treads come on too:
  A −1.3.

**Bytes:**

- the renderer part: 55,629 → **54,781** (−848: H's loops and BSS out, the
  level code in);
- the game: 8,322 → **8,390** (+68: the table and the keys);
- `WARENA.O88`: 22,840 → 22,888.

**Videos** (side by side, labelled in the frame; the P bench and heights
view A; the session's scratch, `r5/walls/lv/vids/`):

- `lev1-P`, `lev1-A`, `lev2-*`, `lev4-*`, `lev5-*`: Medium on the left, the
  level on the right;
- `lev3-*`: Very High on the left, Medium on the right;
- `ramp2-A`: Medium, and Medium with R.

### 9.3 Open, for the owner and the next waves

*What of this is still open is carried in MULTIPLAYER-FPS-PLAN 17.3
(2026-11-15).*

1. **The symmetric dead ground** is decided (7.1.1) and built in wave 3:
   up 112, down 290 (7.4.7), the bands as derived (9.7.3). The owner
   judges the limits by play; they are two constants.

   The question as it stood after wave 1, for the record:
   - at ±112 the screen's reach was not symmetric: 3.6 m from the tower,
     1.4 m from the floor;
   - two rules were offered: (a) the eye-to-eye angle, and (b) the lip and
     depth laid out to hide the gap.
2. **The heights arena's rate:** 11.8–25.9 fps at 40 columns on the
   revised arena (9.7.5). The busy views are still under the forecast.
   9.7.8 has the owner's options; the pillars are the cheap one.
   *Since:* performance pass 6 (MULTIPLAYER-FPS-PLAN 16.9, 2026-11-14) has
   the five views at 15.5 / 17.0 / 43.2 / 15.0 / 35.4 fps at 40 columns.
   The owner ruled there that FARTOP stays at Very Low alone.
3. **The room corners' views** (9.1), beside the tower's.
4. **Flights as one object:** settled as ramps (7.1.2, 7.4.2). The stepped
   look is the T option *(Very High's since the T key went, 2026-11-15)*.
5. **Any package's divide fault, on an IBM ROM, kills the machine's IRQs**
   (9.5). The shipped kernels leave int 0 to the BIOS. CLAUDE.md's rule
   keeps the divide-fault panel behind a knob "until a field report says it
   is being hit", and 9.5 is the first such report. It came off 86Box's
   pc5150, not iron.

   What the kernel does about it is the owner's call; it is not this
   plan's.
6. **The detail levels** (9.11): the owner re-reviews the labelled videos;
   moving an option between levels is a line of `wa_levtab`. FARTOP sits
   at Very Low for now. RAMP2 is on at Very Low and Low
   (WIREARENA-WEAPONS-PLAN 6.7).
7. **Wave 4's remainder** (6): dummies on lifts, more than one lift, the
   host reference's raised lift. The walking runs of 9.7.3 are scratch
   scripts; as suite rows they belong in `soak`
   (docs/WRITING-TESTS.md).

### 9.12 Waves 4 and 5: where each item landed (reconciled 2026-11-15)

| item (section 6) | where it landed |
|---|---|
| lift heights in the frame block; the trigger, rise, hold, return; riding | wave 3 (7.4.4) for the one lift; every lift's, a table a lift (WIREARENA-ARENAS-PLAN 4.1) |
| dummies on lifts | the bots ride them (WIREARENA-AI-PLAN 8.1); a lift leg names its lift (ARENAS-PLAN 4.1) |
| more than one lift | up to four (ARENAS-PLAN 4.1) |
| the host reference's raised lift | **open**: `tools/wacheck.py` still leaves lifts out of its reference |
| patrols across the platform | wave 3 (7.4.5), then the route graph (AI-PLAN 3) |
| the start | each arena's own (`arenagen`'s `*_START`) |
| a heights play bench | not an `H` key: the P bench takes each arena's own views, the heights arena's A to E (ARENAS-PLAN 4.2) |
| measured against 2.4's forecast | 9.7.5, and performance pass 6 (MULTIPLAYER-FPS-PLAN 16.9) |
| the levers of 2.5 the owner chose | flights as ramps (7.4.2); the detail levels (9.11); the owner's rulings in 16.10 |
| the walking runs as suite rows | partly: lifts and tops are `walift` and `waarenas`; the ramp walks of 9.7.3 are still scratch scripts |

# WireArena: the Maze and the Twin, and what the engine stopped assuming

**STATUS: BUILT** (2026-11-14), on branch `multiplayer-fps`. Both arenas play
on every adapter the game runs on. Section 5 gives the measured frame times,
section 6 the parts of the brief that changed on the way, and section 7 the
checklist. The Twin was rebuilt the same day from the owner's drawing
(section 3): two squares and a path drawn as FLOOR LINES, with no room walls
(4.5, 4.6). The floor lines' drawer was rebuilt on 2026-11-15 (4.5; section
5, *The second drawer*).

**The Maze is RETIRED** (2026-11-15, the owner: *"the new one is better"*).
Arena 3 is now the HEDGE, the owner's redrawing of it -
[WIREARENA-HEDGES-PLAN](WIREARENA-HEDGES-PLAN.md). What section 1 and the
measurements below say of the Maze is the record of how it was built; its
data is gone from `tools/arenagen.py`.

The owner's brief, 2026-11-14:

> *"Lets do a couple more arenas to see how well the system holds up before
> we do ctf mode."*

and, while the arenas were being built:

> *"Engine modifications are in scope if needed, if we can do so with
> minimal performance loss. I find we set a lot of limits based on the first
> thing we build, rather than what can be generically supported."*

So this plan has two halves. Sections 1 to 3 are the two arenas, which are
DATA (`tools/arenagen.py`, `MAZE_*` and `TWIN_*`). Section 4 lists the limits
the first three arenas had set and that these two ran into, and how each was
made general. Section 5 gives the measurements and section 6 the deviations.

WireArena is `apps/wirearena/`. The parent plan is
[MULTIPLAYER-FPS-PLAN](MULTIPLAYER-FPS-PLAN.md). The arena format is
[WIREARENA-HEIGHTS-PLAN](WIREARENA-HEIGHTS-PLAN.md) section 7, and the parts
layout is [WIREARENA-PARTS-PLAN](WIREARENA-PARTS-PLAN.md).

## 1. The Maze (arena 3)

Green hedges are in rings round an open centre. Every hedge is 384 units
(6 m), taller than any eye in the arena, so a hedge always shuts the view.
Coordinates below are offsets from the room's middle, (1024, 1024).

- **The centre.** It is a 512-unit court. In it stands the red STACK, three
  platforms on one another, the largest at the bottom: 384 square at 48
  units, 256 square at 104, and 128 square at 160.
  - Each step is 48 to 56 units, inside the jump's 61 of reach.
  - The top carries the ROCKET LAUNCHER or the GRENADE LAUNCHER, chosen at
    each respawn.
- **Four exits from the centre.** They open into the square inner ring. The
  centre's hedge has them on its east and west sides, by the corners.
- **Four entrances to the outer ring.** The middle hedge has them on its
  north and south sides, by the corners. That is the brief's offset: no
  entrance can see along the ring to another entrance.
- **The four rooms**, cut into the outer ring's wall at the cardinals:
  - EAST: a platform at 48 with the SHOTGUN or CHAINGUN;
  - WEST: a platform at 48 with the RAILGUN or SHOTGUN;
  - SOUTH: a platform at 48 with HEALTH or SHIELD;
  - NORTH: the smallest room, an indent holding the LIFT. It rises from 16
    to 128, onto the outer ring wall's top, the PLATEAU. The plateau is walked
    from the lift round to the east and west rooms.
- **Colours.** Hedges are attribute `A_HEDGE` (green, 02h on colour cards,
  07h on mono). The stack and the room platforms are `A_PLAT` (red, 04h and
  07h). The room's own walls are the hedge colour too (`ARENA_ROOMCOL`).

The graph has 27 route nodes:
- the court loop;
- the inner ring;
- the outer ring;
- three room spurs.

There are 7 respawn points.

## 2. LINE boxes (the first twin's walk)

A LINE is a kind of box: the boundary of a walk. The first twin was built
from twelve of them. The rebuilt twin (section 3) uses FLOOR LINES instead
(4.5), which draw a diagonal and cost a fraction of the frame. The LINE kind
stays in the engine for an arena that wants an axis-aligned box the feet stop
at and nothing else sees.

- **What it does.**
  - It stops a player's feet like a wall.
  - Shots, sight and the renderer treat it as nothing: it is drawn as a line
    on the floor and seen across.
  - A rocket flies over it.
- **How it is made.**
  - `arenagen` gives a LINE kind byte bit 7.
  - `wa_hat` answers a line's height as `WA_LINEW` (4096) for feet. While
    `wa_hproj` is set, around the rocket tick, it answers the line's own
    height (8).
  - The first twin cut the void beside its walk into the FEWEST LINE boxes a
    GUILLOTINE partition gives. The box tree needs straight separating cuts,
    which a free-form tiling need not have.
- **How it is drawn.**
  - A LINE's face bits are its top edges only, with no face. The outline path
    (`wrlow.inc` passes 2 and 3) then draws one rectangle on the floor, with
    no posts and no floor edge under each top edge.
  - Edges between two LINE boxes are already hidden by HEIGHTS-PLAN 7.2.1's
    same-height rule.
  - So exactly the walk's outline is drawn.
  - This took the first twin's worst view from 56 outline edges to 37.

## 3. The Twin (arena 4)

The owner, on the first build:

> *"Twin should be the one of the lighter arenas in the game by design,
> basically two boxes and ground lines only. ... All blue is single line
> height - no walls around the edges, nothing above ground level except the
> two towers and the lifts behind them."*

with a drawing: two squares joined by a path that dips to the middle and
back. The arena is that drawing, made even and mirrored about the middle.

- **The floor.** Two 640-unit squares, (64, 704)-(704, 1344) and
  (1344, 704)-(1984, 1344). The path leaves each from the middle of its
  inner side, 128 wide, and runs down to a short flat bottom at the middle.
  - The whole edge is ONE closed line of 16 FLOOR LINES (4.5), in the room's
    blue.
  - There are no room walls (4.6).
- **The towers.** One in each square, 128 by 256 and 240 tall, its long side
  toward the path.
  - Each top carries a RAILGUN.
  - Behind each tower, on the side away from the path, is a LIFT: 128 by
    128, rising from 16 to 240, flush with the top, hidden from the path.
- **A CHAINGUN** stands on the ground beside each tower: south of the west
  one, north of the east one, as drawn.
- **HEALTH or SHIELD** sits at the path's bottom.
- **The flags of a CTF to come** stand behind each lift, at (192, 1024) and
  (1856, 1024): `TWIN_FLAGS`, recorded and not yet in the game.
- **Bots.** They walk round each tower and along the path, two ways, one
  lane each. They ride a lift up, take the railgun, and drop off the tower's
  front.

The graph has 22 route nodes, including the two lift legs and two drops.
There are 6 respawn points. Nothing above the floor but four boxes: the
towers and the lifts.

## 4. The engine: limits the first arenas set

### 4.1 Several lifts

The sim moved exactly ONE lift. `arenagen` refused a second lift, so the twin
was first drawn with stairs, against the brief. Now up to `WA_MLIFT` (4) lifts
each move independently.

- **Tables.** The frame block (`wafb.inc`) carries `wa_nlift` and tables a
  word per lift: `wa_lift` (its record's offset), `wa_lrest` and `wa_ltop`.
  `WR_ABI` is now 8. The renderer part fills the tables from the arena's
  lift list at ARENA.
- **The sim.** `wa_ltick` loops over the lifts, calling `wa_lift1` with
  SI = index x 2. The per-lift state is also tables: `wa_lon`, `wa_loff`,
  the bots' rider counts `wa_lbn` and `wa_lbot` (a byte each, a word apart),
  and the guest's `wa_lklift`.
  - The player's lift is whichever one's box is the floor (`wa_flrb`).
  - `wa_hjump`'s still-rider check now tests the floor box's KIND, so it
    works for any lift (HEIGHTS-PLAN 9.10's fix, kept).
  - The renderer's copy (`wa_rframe`) copies every lift's height.
- **Bots.** A lift leg's kind byte names its lift in bits 4-5. `arenagen`
  finds the lift under the leg's rider node. `wa_bwait` reads the bits.
- **The link.** SNAP byte 8 carried the host's one lift's height / 2. It now
  carries ONE LIFT IN TURN: `index x 64 + height / 4`, `wa_lkli` rotating.
  - The guest fences the index against `wa_nlift`, and follows each lift at
    twice the sim's speed, as before.
  - The cost is a constraint: a lift's heights must be multiples of 4, up to
    252. `arenagen` refuses anything else.
- **Bytes.** The game grew 202 bytes for this whole round (4.1 to 4.3).

### 4.2 The play bench's views are the arena's

The P bench (MULTIPLAYER-FPS-PLAN 16.7) had five views for the heights arena
and one for the first two, all in a table in the game. In any other arena it
fell back to the heights arena's views, which stand inside the maze's hedges
and the twin's lines.

- The views are now ARENA DATA. `arenagen.ARENA_BENCH` holds them per arena,
  at the end of the route-graph block: a count, then x, y, angle, base
  pitch, floor and HUD mark, 12 bytes each. `wa_pbstart` takes the next view
  each press.
  - `arenagen` refuses a view that stands in a wall or a line.
  - The heights views A to E and the first two arenas' P view are exactly
    what they were.
- **Views per arena:**
  - the maze: the court (its worst outline-edge count), the east room, the
    inner ring's corner, and the plateau;
  - the twin: at the path looking east, on the west tower, the path's bottom,
    and behind the west lift (the flag's place).
- **The tools.** `tools/wabench.py` and `tools/waprof.py` take `A<n>-<width>`
  for any arena. `tests/walift.py`'s `start_keys` counts the arena drop-down
  off `arenagen.arenas()`. With five arenas, *"Right wraps to 0"* was wrong.

### 4.3 A tall box can be a floor

A box as tall as the arena's tallest was always a WALL: no floor table held
it. The twin's towers are the tallest boxes, and they must be stood on.

- `arenagen` sets kind bit 6 on a tall SOLID that a flight or a lift beside
  it climbs onto (`climbed`, within a step of its height).
  - `wa_lists` puts such a box in the player's and the figures' low tables.
  - Sight and the renderer still treat it as tall.
- On the host side:
  - `pvs_eyemax` counts its top;
  - `height_checks` allows pickups on it;
  - route legs walk over it, and the step rule keeps them off it from the
    ground.

### 4.4 Visible-budget ceilings per arena

`wamodel`'s visible budget (HEIGHTS-PLAN 4.2) is 20 boxes and 40 outline
edges in any view.
- The twin is inside it: 4 boxes and 15 edges at worst, its floor lines
  counted an edge each.
- The maze's court reaches 42 edges at (1248, 1248), its worst view.
  `OVER_BUDGET` lets one arena past with its own ceiling, (20, 44) for the
  maze. Its cost is measured, in section 5, where it is worst.

`wamodel` also now counts a LINE's top edge as one edge, not a face's two.

### 4.5 Floor lines

Every line the engine drew was a box's edge, and every box is axis-aligned,
so a diagonal could only be a staircase of boxes. A FLOOR LINE is a straight
line on the floor between two of the arena's vertices, at any angle.

- **Data.** `arenagen.ARENA_FLINES` gives an arena closed lines as point
  lists. At most `WA_MFL` (24) segments.
  - Their ends join the arena's vertices, so the frame's vertex cache makes
    them.
  - The arena part carries a table at header +48: a count, then each line's
    x0, y0, x1, y1 and its two vertices.
  - The renderer part copies the ends into the frame block (`wa_nfl`,
    `wa_fl`, for the game) and the vertex pairs into its own `wa_fqv`.
    `WR_ABI` is 9.
- **Drawing** (`wa_flines`, after every box and before the floor's dots, in
  the room's colour). The drawer was rebuilt on 2026-11-15: the first one
  cost more than the arena's boxes, figures and pickups together (section 5,
  *The second drawer*). The picture is the same to a rounding.
  - **Ends once a view, cells every frame.** A line's two ends ON THE SCREEN
    are made when the view, the pitch or the eye has moved (`wa_flmake`) and
    kept in its record. Every frame draws from the records (`wa_fldraw`). So
    a still view pays for its lines' cells and nothing else.
  - **The vertices.** The floor lines have their own vertex records, one a
    distinct point (`wa_flinit`, at GEOM). When the view moves, each goes
    into camera space off the frame's products - two adds - and is tested
    against the four planes the screen's floor lies between:
    - its BOTTOM and its TOP. The floor's row is a function of depth alone,
      so each screen edge is a depth, `wa_fqzn` and `wa_fqzf`, a divide a
      frame. There is a top only when the horizon is above the screen,
      looking down;
    - its two SIDES. A vertex inside the 90 degrees every view lies within
      is tested by its COLUMN - the projection's own divide, which a line
      drawn to it needs anyway. One outside them needs no divide.
    A vertex nearer than the screen's bottom is asked nothing more.
  - **The lines.** Both ends outside one plane: nothing, for two byte loads
    and a test. Both inside: the ends are the vertices' own column and row,
    the row made once a vertex. Any other is CUT in camera space at the
    planes it crosses, a divide a cut, to a quarter of a unit - a line by
    the player's feet is cut a few units from the eye, where a whole unit is
    half a column. A cut end lies on the screen's edge: no guard band, and
    nothing off the screen to step over.
  - **The walk is an error term** (Bresenham's), set up without a divide and
    exact from the ends' sixteenths of a column and quarters of a half-row.
    The first drawer stepped an 8.8 fraction, a divide a line.
    - A SHALLOW line, under a half-row a column, is walked by COLUMN in
      dashes. A half-row on is the other dash, and every second one the next
      row: no table, no row kept.
    - A STEEP one is walked by CELL ROW. It draws a post's `│` where its
      column holds, and `\` or `/` where it steps.
    - The first version filled each column's step with slashes, and a column
      of slashes read as two parallel lines: the owner's *"weirdly
      doubled"*, 2026-11-14.
    - The steep threshold was two half-rows a column. Past one, a line of
      dashes read as steps: the owner's *"horizontally stacked"*. It is one
      now.
- **What hides it.** A floor point's depth is a function of its row alone,
  so it is behind a face exactly when it lies above the face's foot.
  - The frame's spans give the HULL of the columns anything was drawn in,
    once a frame. A line whose columns lie outside it is walked with no test
    at all.
  - Inside it, a column with a tall face, or a cell at or under a low box's
    top (`wa_cclip`), is asked about.
  - A span's foot lies between its two ends' feet (`wa_flfeet`, two
    multiplies once a frame a span). Those two settle nearly every ask with
    two compares, in the walk's own loop. A point between them takes the
    foot in its column, a divide (`wa_flfoot`).
  - Behind a tall face, the face's other columns are stepped over AT ONCE -
    a multiply and a divide - while the line stays above the face's whole
    foot. A tower before the eye hides a dozen columns of every line behind
    it.
- **Tried and not kept: ends snapped to cells.** The figures' limbs are
  drawn between whole sub-pixels (`wa_flgo`), with a setup of some thirty
  instructions and no multiply. Floor lines drawn that way, between their
  ends' cells, were 6,000 cycles a frame cheaper and visibly wrong: a far
  line is two or three half-rows tall on the screen, and a half-row's error
  at each end split one far edge into two rows of dashes. Over the 40
  seeded poses its picture differed from the first drawer's in 321 line
  cells, where the exact walk differs in 45, and an exact referee sided
  with the first drawer in 275 of them.
- **Considered and not built: drawing floor lines inside the box walk.**
  - The idea: each line cut where the box tree's split lines and each box's
    faces cross it, every piece drawn at its place in the front-to-back
    walk. The column clip alone would then hide it, as it hides a wall.
  - The owner asked for it on 2026-11-14. Hiding is now about 5,000 to
    9,000 cycles a frame; the twin's 16 lines would become about 50 pieces,
    each with a setup of its own of about 1,100 cycles.
- **The feet.** `wa_collide` asks `wa_flcross` first. A step that crosses a
  floor line is refused, as a wall's is, and the player slides along it.
  - A line is near only when the new point is within 16 units of its box.
  - Then two 32-bit cross products ask whether the step's ends lie either
    side of the line, and two more whether the line's ends lie either side of
    the step (or on it). That last test lets a step pass the open end of a
    line, through the path's gap.
- **Bots and arenagen.** `arenagen` refuses:
  - a route leg that leaves the floor lines;
  - a respawn, a pickup, a bench view or the start outside them (point in
    polygon, with a margin).

  `wamodel` counts a floor line in view as one edge.

Row `waflines` (soak) holds the drawer's picture to an exact referee: the
game's integer camera space, then the line's image in exact rationals.

### 4.6 An arena with no room walls

`ARENA_ROOMH` sets an arena's room height, and 0 means NO ROOM WALLS. The
renderer skips the room's four faces when the height is 0. The game still
keeps the player inside the room (an invisible bound the floor lines sit well
within), and `wamodel` skips the room as the renderer does. The twin uses it:
its floor lines are its edge.

### 4.7 Limits still standing (not met by these arenas)

| limit | where | why it stands |
|---|---|---|
| 63 boxes (31 until [HEDGES-PLAN](WIREARENA-HEDGES-PLAN.md) 2) | a tree leaf's box x 4 is a byte | the maze uses 29, hedge B 40 |
| boxes separable by axis lines | `box_tree` | guillotine partitions meet it (section 2) |
| 32 route nodes, 4 ways out each | `WA_MGN`, the node record | the maze uses 27 |
| 8 respawns, 8 pickups | their tables | 7 and 5 used |
| 3 home loops | one per figure, `WA_NFIG` | |
| 4 lifts, heights multiples of 4 to 252 | `WA_MLIFT`, SNAP byte 8 (4.1) | |
| 24 floor lines | `WA_MFL`, the frame block (4.5) | the twin uses 16 |
| 8 distinct steps between coordinate values | `MAXKS`, `wa_products` | the twin's coordinates sit on a 64 grid: 5 steps |

## 5. Measured

All on MartyPC's 5150 with CGA, 40 columns.

**`tools/waposes.py`**, 40 seeded poses per arena, the renderer's exact
cycles a pose:

| arena | ms a pose | worst view (wamodel) |
|---|---|---|
| heights | 34.0 | |
| maze | 36.5 | 15 boxes, 42 edges |
| first twin, LINE boxes as low boxes (two edges each) | 69.8 | 16 boxes, 56 edges |
| first twin, LINE boxes as one edge (section 2) | 64.0 | 16 boxes, 37 edges |
| twin as drawn: floor lines, no room walls | 35.6 | 4 boxes, 15 edges |
| **twin as drawn, the second floor-line drawer** | **28.6** | |

**`tools/wabench.py`**, the P bench: every pickup present, three dummies
walking, 8 s per view, the HUD's averages. Frames a second, and the render
time in ms:

| arena | A | B | C | D | E |
|---|---|---|---|---|---|
| heights | 16.2 (52.6) | 16.9 (48.7) | 43.1 (17.9) | 14.9 (56.3) | 35.2 (23.2) |
| maze | 10.5 (83.5) | 26.5 (29.8) | 15.8 (53.8) | 45.8 (18.5) | |
| first twin | 9.3 (91.7) | 8.5 (100.7) | 9.8 (88.1) | 9.2 (96.0) | |
| twin as drawn, first floor-line drawer | 17.2 (50.6) | 14.5 (61.4) | 18.2 (47.1) | 13.8 (63.9) | |
| twin as drawn, the first drawer tuned | 18.5 (46.7) | 17.0 (51.7) | 19.4 (43.7) | 13.3 (66.3) | |
| **twin as drawn, the second drawer** | **22.4 (37.9)** | **20.9 (41.5)** | **24.0 (34.6)** | **17.2 (50.3)** | |

The twin's views differ between the two builds (4.2). The first twin's were
before the south tower, on it, the walks' bar and behind it. The rebuilt
twin's are at the path, on the west tower, the path's bottom and behind the
west lift.

The Very Low detail level (F1) moved the first twin by under 1 fps, the
maze's court to 11.8 and the heights views by up to 3.

**What the first twin's cost was.** `tools/waprof.py --what A4-40 --live 3`:
- the outline path's top-edge walk `wl_rtop` was 18.5% of a frame;
- `wa_lowbox` 10.6%;
- `wl_draw` 6.6%.

Its 37 edges each crossed most of the screen, about 100 cycles a column.

**What the rebuilt twin's cost was, with the first floor-line drawer.**
`tools/waprof.py --what A4-40 --pose 1`, the tower-top view (B), was 326,000
cycles a frame and came to 277,000 (58 ms):

| part | cycles a frame | share |
|---|---|---|
| floor lines (16; ends, setup, walk, hiding) | 122,898 | 44.9% |
| boxes (the towers and lifts, the one stood on as an outline) | 35,451 | 13.0% |
| figures (the bench's three dummies) | 34,548 | 12.6% |
| pickups | 16,381 | 6.0% |
| the present | 15,286 | 5.6% |
| the rest (the view, products, the tick) | 49,157 | 17.8% |

- A floor line costs about 7,700 cycles in that view.
  - About half of it is fixed: its two ends' transform and projection
    (divides), the clip where an end is behind the eye, and the walk's set
    up. The 8088 spends 15 to 25 cycles on an instruction that touches
    memory, so a hundred-odd instructions of setup are 2,000 cycles.
  - The rest is its cells on the screen: about 100 to 150 cycles each in a
    clear column, more in a column a box stands in.
  - So what costs is a line's LENGTH ON THE SCREEN plus a fixed toll per
    line. Its length in the arena matters only through how many cells it
    covers.
- Why the open arena is faster with more lines: its lines are wall faces.
  - A face is one span over its columns with its top and bottom stepped in
    the most-tuned loop in the renderer.
  - A wall shuts its columns, so nothing behind it is drawn at all.
  - Its arena is FLAT (no box below the tallest), so no depth stacks and no
    outlines.
  - The twin has none of that: nothing occludes, and every line is drawn
    end to end.
- **What the heights machinery costs** (its lifts make the twin a heights
  arena). `tools/waprof.py --what A4-40 --pose 0` measured the bench's view
  A at 261,286 cycles a frame. With `--poke R:wa_hflat=1`, the renderer told
  the arena is flat, it is 242,041: 7%.
  - A first experiment read 25 fps against 70. That was wrong: the game's
    frame counter laps every 127 frames, and a second that spanned a lap
    counted it as frames.

**The second drawer** (2026-11-15, section 4.5). The owner: *"These lines
are costing drastically more than walls."* They were: in the tower-top view
the 16 lines were 112,000 cycles of a 276,000-cycle frame (123,000 in the
table above, which gives them all of the shared vertex cache's work),
against 35,000 for the arena's boxes. Only about 27,000 of that drew cells
- some 70 of them. The rest was each line's and each vertex's arithmetic and the
per-column hiding records. A line is short on a 40-column screen, about
eight cells, so what a line costs before its first cell is what the lines
cost.

Measured against the tree before it, the same day, both builds on the same
poses (`tools/waposes.py`, `tools/wabench.py`, `tools/waprof.py`):

| | first drawer | second | |
|---|---|---|---|
| 40 seeded poses, a moved view, 40 columns | 34.3 ms | 28.6 ms | -16.6% |
| ...80 columns | 42.5 ms | 33.9 ms | -20.1% |
| ...a still view, 40 columns | 32.3 ms | 22.4 ms | -30.6% |
| 150 stress poses (tower tops, by a line, steep pitch), 40 columns | 28.9 ms | 22.7 ms | -21.7% |
| ...80 columns, 60 degrees | 33.4 ms | 24.7 ms | -26.2% |
| P bench A to D, 40 columns, fps | 19.1, 17.3, 20.3, 13.9 | 22.4, 20.9, 24.0, 17.2 | +17% to +24% |
| P bench A to D, 80 columns, fps | 14.3, 13.3, 14.7, 10.4 | 17.5, 16.2, 18.2, 13.8 | +22% to +33% |
| the floor lines alone, view A, cycles a frame | 107,800 | 68,300 | -37% |
| ...view B, the tower top | 112,500 | 66,000 | -41% |
| ...view D, behind the lift | 135,000 | 75,000 | -44% |

(The first drawer's share counts its 16 vertices' part of the shared vertex
cache and its clip. The poses' first-drawer figure is 34.3 here and 35.6 in
the table above: the first drawer's last tuning came between.)

- The heights arena, the maze and the first arena draw the same picture in
  the same cycles, to 10 in 160,000.
- The picture of the twin differs from the first drawer's in 45 line cells
  over the 40 poses, each by one cell: the first drawer's 8.8 step against
  an exact one, and a cut at the guard band against a cut at the screen's
  edge. An exact referee sides with each about as often.
- Where the second drawer's cycles go, view B (66,000):

| part | cycles a frame |
|---|---|
| the vertices: camera space, planes, columns (16) | 9,500 |
| their rows (10) | 3,700 |
| the lines' pass, and two cuts | 9,000 |
| the walks' setups (9 lines) | 10,500 |
| the walks (about 66 cells) | 24,500 |
| hiding | 7,700 |
| the hull, the planes | 1,100 |

- **What is left is the picture's own.** A cell is about 180 cycles plain
  and 300 where it is tested: the stamp, the glyph and the list are three
  stores at 30 cycles each on an 8088, as a wall's cell is. A line in view
  is about 1,100 cycles of setup and its two ends about 1,300 each. A drawn
  line is now about 7,000 cycles, a wall's face in the open arena about
  6,000.
- **Not done, and what each would buy.**
  - The walk's state kept across still frames, not only its ends: about
    10,000 cycles of a still frame.
  - A pitch-only move keeping the vertices' camera space and columns: about
    15,000 cycles of such a frame.
  - Fewer cells: a far or a low-detail line drawn dashed. The owner's call -
    it changes the look.

**Size.** The renderer part is 61,403 of its 65,024 bytes. It was 59,041
after the two arenas: 2,182 more for the first round, then 2,505 for floor
lines (code, the per-column and per-vertex caches) and no room. The second
drawer is 1,101 bytes more: its code is about twice the first's, its tables
smaller.
- Almost all of it is tables sized to the LARGEST arena: the maze's 92
  vertices against the heights arena's 51 (82 bytes per vertex table), its
  PVS at four mask bytes a cell (+512), and its box tree (+384).
- The arena parts themselves are 5,112 bytes (maze) and 2,705 (twin), loaded
  only when chosen.

## 6. Where it differs from the brief

- **Maze, the plateau's south half.** The plateau runs from the north lift
  round to the east and west rooms. Past those rooms, its south half is cut
  off from any walk, so those pieces are plain hedges.
- **Maze, which sides.** The brief fixed only the offset between exits and
  entrances. The centre's exits are on its east and west sides, the middle
  ring's entrances on its north and south.
- **Twin, the first build.** The first build read the brief's walk as a path
  that turns right and back, built from LINE boxes inside room walls. The
  owner's drawing replaced it (section 3).
- **Twin, lifts not stairs.** The very first build had stairs behind the
  towers, because of the one-lift limit. Section 4.1 removed the limit.
- **Twin, symmetry.** The drawing is mirrored left to right in its path, and
  its chainguns are turned about the middle (south of the west tower, north
  of the east). The arena keeps both, as drawn.
- **Twin, bots.** Bots ride up and drop off the front; they do not patrol a
  tower's top. The sight grid counts a tall box's cells as solid, so a bot
  standing on a tower would neither see nor be seen by the grid. It fetches
  the railgun and comes down.
- **Twin, falling.** As the brief allowed, nobody falls off the walk: the
  floor line stops them.

## 7. Checklist

- [x] The maze: hedges, the stack, exits and entrances, four rooms, the
  north lift onto the plateau; green and red on colour cards.
- [x] The twin as drawn: two squares and a dipping path as floor lines, no
  room walls, towers with railguns, a hidden lift behind each, chainguns
  beside, health or shield at the path's bottom, the CTF flags' places
  recorded.
- [x] Floor lines: data, drawing with occlusion, the feet, `arenagen`'s
  checks (4.5); no room walls (4.6).
- [x] Several lifts: sim, renderer, link, bots, `arenagen` (4.1).
- [x] The bench's views per arena; the tools take any arena (4.2).
- [x] Tall boxes as floors (4.3); LINE boxes (2).
- [x] Measured: `waposes` and the P bench on every arena (5).
- [x] Row `waarenas` (soak): both lifts, the towers, the floor lines (a
  straight edge and the path's sloping one), the bots' rides, the maze's
  stack and lift, each arena's bench view.
  - It was red with bit 6 dropped, one lift ticked and the views' pointer a
    word off: 5 failures (the first twin).
  - It was red with `wa_flcross` answering "clear": both floor-line checks.
- [x] The second floor-line drawer (4.5): ends once a view, an exact walk
  with no divide, hiding off the spans' feet; measured (5).
- [x] Row `waflines` (soak): nine poses of the twin against an exact
  referee - no cell off its lines, none missing in an open view, none
  through the tower, 85 in 100 its own cell and dash, no divide fault.
  - It was red with the shallow walk's error a modulus out, a side cut left
    where it was, a steep line that never steps, and a tall face's columns
    not asked about.

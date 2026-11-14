# WireArena: the Maze and the Twin, and what the engine stopped assuming

**STATUS: BUILT** (2026-11-14), on branch `multiplayer-fps`. Both arenas play
on every adapter the game runs on. Section 5 gives the measured frame times,
section 6 the parts of the brief that changed on the way, and section 7 the
checklist.

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

## 2. LINES (the twin's walk)

A LINE is a new kind of box: the boundary of the walk.

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
  - The void beside the walk is cut into the FEWEST LINE boxes a GUILLOTINE
    partition gives (`_lines`). The box tree needs straight separating cuts,
    which a free-form tiling need not have.
- **How it is drawn.**
  - A LINE's face bits are its top edges only, with no face. The outline path
    (`wrlow.inc` passes 2 and 3) then draws one rectangle on the floor, with
    no posts and no floor edge under each top edge.
  - Edges between two LINE boxes are already hidden by HEIGHTS-PLAN 7.2.1's
    same-height rule.
  - So exactly the walk's outline is drawn: the brief's *"one line"*.
  - This took the twin's worst view from 56 outline edges to 37.

## 3. The Twin (arena 4)

Two towers face each other across the room. They are mirrored through its
middle.

- **The towers.**
  - Each is 256 square and 240 tall, the arena's tallest box, so from the
    ground it hides what stands behind it.
  - Each top carries a RAILGUN.
  - Behind each tower, on the side away from the middle, is a LIFT. It rises
    from 16 to 240, flush with the top, and is hidden entirely from the
    ground in front.
- **Beside each tower** is a CHAINGUN on the ground.
- **The walk.** From each tower's base a walk goes out toward the middle,
  turns to its RIGHT, comes back along a bar the two walks share, and joins
  the other. Where they join, in the middle, is HEALTH or SHIELD.
- **The lines.** Everything that is not walk is LINE (section 2): 12 boxes.
- **Bots.**
  - Bots ride the lifts: a lift leg up, then a drop off the tower's front.
  - They take the railgun on the way when they FETCH it.

The graph has 23 route nodes, including the two lift legs and two drops.
There are 6 respawn points.

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
  - the twin: before the south tower, on top of it, the walks' bar, and
    behind it (its most boxes in view).
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
- The twin is inside it: 16 boxes and 37 edges at worst.
- The maze's court reaches 42 edges at (1248, 1248), its worst view.
  `OVER_BUDGET` lets one arena past with its own ceiling, (20, 44) for the
  maze. Its cost is measured, in section 5, where it is worst.

`wamodel` also now counts a LINE's top edge as one edge, not a face's two.

### 4.5 Limits still standing (not met by these arenas)

| limit | where | why it stands |
|---|---|---|
| 31 boxes | the box index x 8 is a byte | the maze uses 29 |
| boxes separable by axis lines | `box_tree` | guillotine partitions meet it (section 2) |
| 32 route nodes, 4 ways out each | `WA_MGN`, the node record | the maze uses 27 |
| 8 respawns, 8 pickups | their tables | 7 and 5 used |
| 3 home loops | one per figure, `WA_NFIG` | |
| 4 lifts, heights multiples of 4 to 252 | `WA_MLIFT`, SNAP byte 8 (4.1) | |

## 5. Measured

All on MartyPC's 5150 with CGA, 40 columns.

**`tools/waposes.py`**, 40 seeded poses per arena, the renderer's exact
cycles a pose:

| arena | ms a pose | worst view (wamodel) |
|---|---|---|
| heights | 34.0 | |
| maze | 36.5 | 15 boxes, 42 edges |
| twin, lines as low boxes (two edges each) | 69.8 | 16 boxes, 56 edges |
| twin, lines as one edge (section 2) | 64.0 | 16 boxes, 37 edges |

**`tools/wabench.py`**, the P bench: every pickup present, three dummies
walking, 8 s per view, the HUD's averages. Frames a second, and the render
time in ms:

| arena | A | B | C | D | E |
|---|---|---|---|---|---|
| heights | 16.2 (52.6) | 16.9 (48.7) | 43.1 (17.9) | 14.9 (56.3) | 35.2 (23.2) |
| maze | 10.5 (83.5) | 26.5 (29.8) | 15.8 (53.8) | 45.8 (18.5) | |
| twin | 9.3 (91.7) | 8.5 (100.7) | 9.8 (88.1) | 9.2 (96.0) | |

The Very Low detail level (F1) moves the twin by under 1 fps (8.8 to 10.2),
the maze's court to 11.8 and the heights views by up to 3.

**What the twin's cost is.** `tools/waprof.py --what A4-40 --live 3`:
- the outline path's top-edge walk `wl_rtop` is 18.5% of a frame;
- `wa_lowbox` is 10.6%;
- `wl_draw` is 6.6%.

Each of the 37 edges crosses most of the screen in an open room, at about
100 cycles a column with its plot, clip test and depth-stack entry. So the
twin is the cost of drawing a long boundary in a room nothing occludes. No
detail level removes that. A narrower view, or fewer turns in the walk,
would.

**Size.** The renderer part is 56,536 of its 65,024 bytes, 2,182 more than
before this round.
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
- **Twin, lifts not stairs.** The first build had stairs behind the towers,
  because of the one-lift limit. Section 4.1 removed the limit, and the twin
  now has the lifts the brief asked for.
- **Twin, bots.** Bots ride up and drop off the front; they do not patrol a
  tower's top. The sight grid counts a tall box's cells as solid, so a bot
  standing on a tower would neither see nor be seen by the grid. It fetches
  the railgun and comes down.
- **Twin, falling.** As the brief allowed, nobody falls off the walk: the
  line stops them.

## 7. Checklist

- [x] The maze: hedges, the stack, exits and entrances, four rooms, the
  north lift onto the plateau; green and red on colour cards.
- [x] The twin: towers with railguns, a hidden lift behind each, the walk as
  one line, chainguns beside, health or shield in the middle.
- [x] Several lifts: sim, renderer, link, bots, `arenagen` (4.1).
- [x] The bench's views per arena; the tools take any arena (4.2).
- [x] Tall boxes as floors (4.3); LINE boxes (2).
- [x] Measured: `waposes` and the P bench on every arena (5).
- [x] Row `waarenas` (soak): both lifts, the towers, the line, the bots'
  rides, the maze's stack and lift, each arena's bench view. It was red with
  bit 6 dropped, one lift ticked and the views' pointer a word off: 5
  failures.

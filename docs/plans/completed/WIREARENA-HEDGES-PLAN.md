# WireArena: the Hedges, the maze redrawn for speed

**STATUS: BUILT; THE HEDGE IS ARENA 3** (2026-11-15), on branch
`multiplayer-fps`. The owner's two redrawings of the Maze were built as
arenas 5 and 6 beside it. The owner chose A, `hedge`, for its PLAY - *"the
offsets hint where you are"* - and B went (section 6); then the Maze itself
went, and the hedge took its place as arena 3 (section 9). Section 3 is
what the drawings measured; section 4 what would make them faster, of which
the owner asked for 4.1 (section 7) and 4.2 (section 8). **"Arenas 5 and 6"
below are the trial's numbers: arena 5 is Crossing now**
([WIREARENA-THREE-ARENAS-PLAN](WIREARENA-THREE-ARENAS-PLAN.md)), and the
hedge is arena 3. Filed under `completed/` (2026-11-15); 4.1's faces, which
section 7 did not build, are carried as a renderer lever in
[MULTIPLAYER-FPS-PLAN](../MULTIPLAYER-FPS-PLAN.md) 17.3.

The owner's brief, 2026-11-14, with two drawings (not to scale):

> *"A new arena in Wire Arena, hedge, has come in. It works decently well, so
> we may keep it as is; but I'd like to try a few other options to make it
> generally faster. This may require an engine change; I think the total
> object count/arena size might have been a blocker."*
>
> *"The entries here are moved around, and one option has less entries. The
> goal is to obscure any view from 'going too far', thus allowing maximized
> generation time culling, and maximized runtime culling while still
> providing an interesting playfield. The size of the center room is larger,
> and the edge rooms as well. The hallways are a bit longer to prevent angle
> views reaching as far as they do today. The entries into the rooms is more
> narrow, allowing the gap to be jumped and the player to go around the
> entire map on the outer wall. The outer wall does not need its current
> additional height, unless that is very cheap to keep."*

The Maze is [WIREARENA-ARENAS-PLAN](WIREARENA-ARENAS-PLAN.md) section 1, and
the arena format [WIREARENA-HEIGHTS-PLAN](WIREARENA-HEIGHTS-PLAN.md) section 7.
Both arenas are DATA, `tools/arenagen.py`'s `_hedge()`.

## 1. The layout

Absolute coordinates. The main square is x 256..1792, y 256..2048; the
court's middle is (1024, 1152).

- **The court**, x 704..1344, y 704..1600: 640 x 896 against the Maze's 512
  square. The red stack is the Maze's, three tiers at 48, 104 and 160 with
  the rocket or grenade launcher on top.
- **Two rings** of 6 m hedges round it, every corridor 2 m (128) wide, every
  gap 2 m. No gap faces the next one out.
  - Hedge A, the fewer entries: the court's hedge open N and S at the
    middle; the middle hedge open W by its north end and E by its south end.
  - Hedge B: the court's hedge open at the middle of every side; the middle
    hedge a pinwheel, open by one end of every side.
- **The outer wall**, 1 m thick and 2 m (128) high, walked all the way
  round. Its gaps are 2 m: a running jump clears one (below).
- **Three rooms OUTSIDE the wall**, W, E (4 x 8 m) and S (8 x 4 m), each
  off a gap in the wall, a platform at 48 in the middle with the Maze's
  pickups: E shotgun or chaingun, W railgun or shotgun, S health or shield.
- **The lift** fills the north gap and rises from 16 to the wall's top.
- **The edge**: tall fills round the rooms, the hedges' green. They are
  tall because a tall box is the CHEAP one (section 3).
- The drawings' marks this reading took as eraser leftovers: in the first,
  a tick on the court hedge's east side and one on the middle hedge's south
  side. Neither is a gap here.

The graph walks the court, both rings and the three rooms (31 nodes in B, 25
in A); the bots do not use the wall's top. Seven respawns, the four pickups,
the start at the south room's gap facing north *(as drawn; since
2026-11-15 arenagen's `facing()` turns a spawn that faces a wall, and the
hedge's start and its four axis respawns were turned - NET-PLAN 9.8)*. The P bench's views: the
court beside the stack, into the east room, the inner ring's SW corner, on
the wall's top.

## 2. What the engine stopped assuming

**More than 31 boxes.** Hedge A is 37 boxes and B 40: the stack's 9, the
rings', the wall's 8 and the lift, 6 fills and 3 platforms. A tree leaf
held its box x 8 in a byte, which stops at 31. It holds box x 4 now, so an
arena may have **63**:

- `arenagen` writes the leaf's byte as box x 4 and checks boxes x 4 <= 255;
- `wa_order` reads it: its PVS byte is the byte >> 5, its bit (byte >> 2)
  & 7, and the order's record (box x 4) is the byte as it stands - one
  shift fewer than before on each path;
- the PVS was already a bit a box, sized by the largest arena
  (`pvs_bytes`): 5 bytes a cell now;
- the other per-box tables are sized by `WA_MBOX` already.

The Maze renders exactly as before: 174,297 clk a pose before, 174,284
after (`waposes`, 40 poses, 40 columns).

The byte limits still standing: 127 vertices (B has 124), 63 distinct x and
63 y values, 8 distinct coordinate steps.

**A box tree cannot split a pinwheel.** No axis line separates four boxes
each past a corner of the next, so `box_tree` refused B's middle hedge as
drawn. Its NE corner is a box of its own (`middle hedge NE`): 5 boxes, not 4.

**A gap a jump clears.** `height_checks` refused the wall's south half: a
128-high box no flight or lift reaches. It now also reaches a box across a
gap from a reached box of the same height, the gap at most `jump_gap()`:
half the ground a running jump covers, off `WA_JUMPV`, `WA_GRAV` and
`WA_MSPD` (27 ticks at 10 units: 270, so 135 allowed; the gaps are 128).
Computed, not yet tried in the game. *(Tried since and confirmed by the
owner: section 5.)*

**Size.** The renderer part grew 1,655 bytes (61,957 of its 65,024): the
tables sized to the largest arena - B's 124 vertices against the Maze's
92, its 79-node tree, its 40 boxes. The arena parts are 5,492 (A) and 6,263
(B) bytes, loaded only when chosen.

## 3. Measured

All on MartyPC's 5150 with CGA, `tools/waposes.py`, exact render cycles a
pose. The seeded pose set puts three figures near the view and a quarter of
the poses on a raised box, and the figures dominate single frames; so the
comparison below is the same 200 poses per arena (seed 1) with the figures
moved behind the camera: geometry alone. A pose is binned by where it
stands.

**As drawn**, ms a pose (40 columns) and kclk by region:

| arena | all | court | inner ring | outer ring | rooms and the wall's top |
|---|---|---|---|---|---|
| Maze | 28.0 | 189 | 124 | 123 | 121 |
| Hedge A | **27.9** | **141** | **101** | 137 | 148 |
| Hedge B | 30.7 | 186 | 120 | 128 | 139 |

With the figures in (the seeded set, 200 poses): Maze 37.8, A 38.0, B 40.7.

So neither is faster as a whole. A's centre is 20-25% cheaper - the larger
court keeps the eye farther from the stack, and two gaps a ring let less in
- and its outer ring and rooms give it back. B's extra gaps cost it the
centre's saving.

**What the outer ring costs.** The walk-round wall is LOWER than the
hedges, so the renderer draws it as an OUTLINE (HEIGHTS-PLAN 1) and it hides
nothing: the fills and room walls behind it are drawn as well. A tall box is
one span a face on the most-tuned path, and shuts its columns. Hedge A with
the wall made tall (not walkable; no lift), same poses:

| region | as drawn | wall tall | |
|---|---|---|---|
| outer ring | 137 | 80 | -42% |
| rooms | 140 | 111 | -21% |
| inner ring | 101 | 93 | -7% |
| court | 139 | 139 | 0 |
| **all** | **27.9 ms** | **22.8 ms** | -18% |

`tools/waprof.py`, the bench's view B (the outer ring, into the east room):
219,815 clk a frame with the low wall, 178,986 with the tall one. Of the
41,000 between them about 31,000 is the outline path itself - `wa_lowbox`
+9,600, `wl_rtop` +7,300, `wl_post` +6,500, `wl_f1` +4,400, `wl_draw`
+3,400 - and the rest the scenery behind.

**The other knobs**, Hedge A, all 200 poses:

| variant | ms | where it moved |
|---|---|---|
| as drawn | 27.9 | |
| the room's walls at the wall's height (128) | 27.9 | nowhere |
| no platforms in the rooms | 27.0 | rooms -20% |
| the stack ONE tier (one box, 384 square at 48) | 24.2 | court -27 to -34%, inner -10% |
| the wall tall and no platforms | 22.1 | |

The fills lowered to the room's walls (192) were refused by `height_checks`
(unreachable low boxes), and would draw as outlines anyway: tall is their
cheap height.

So, against the brief's *"unless that is very cheap to keep"*: the extra
height is not a cost but a saving. **What costs is the walk-round wall
being low, and the stack's nine boxes.**

## 4. What would make it faster

In order of what it buys:

1. **Draw a low box whose top is above the eye as FACES** (an engine
   change). From below its top a low box looks exactly like a tall face of
   its height, with the scene visible above it: the outline path is the
   wrong tool there. Through the face path with a column clip at its top
   row, about 31,000 of view B's 41,000 come back; the outer ring would be
   near the tall wall's 80 kclk while the wall stays walkable. It helps
   every arena with a low box seen from beside it (the Maze's plateau, the
   heights arena's platforms). It is a change to the heights renderer's
   column clip (`wrlow.inc`), and is not started. *(Section 7 answered
   the owner's 4.1 with the shut test instead; the faces are still not
   built.)*
2. **The stack.** Nine boxes is what three tiers cost when no box may
   stand on another. A tier standing on a tier would be 3 boxes; it touches
   collision, the depth stack, the PVS's low pass and the bots. A design
   answer is fewer tiers: one tier is -13% on the whole arena.
3. **Hedge A over B.** Fewer gaps, less seen: B costs 10% more.
4. **No room platforms**: -3%, the rooms' views -20%.

## 5. Checklist (the trial)

- [x] Hedge A and Hedge B as arenas 5 and 6, playable, on the setup's
  arena list (`hedge A`, `hedge B`)
- [x] Up to 63 boxes an arena: leaf byte box x 4, `wa_order`
- [x] `height_checks`: a gap a running jump clears (`jump_gap`)
- [x] Measured: as drawn, and five variants of A (section 3)
- [x] The jump across a wall gap tried in the game - and the stack's
  tiers: the owner, 2026-11-15, *"The tier jumps work fine, and the gaps
  as well."*
- [x] The owner's choice: A, for its play (section 6); section 4's 4.1 and
  4.2 asked for and built (sections 7, 8)
- [x] Suite rows: the fourteen WireArena soak rows (section 8)

## 6. The owner's rulings, 2026-11-15

> *"The longer hallways serve their purpose well, and basically make A/B
> close to the same - you can't see from one entrance into and through a
> second entrance like you could in the smaller old hedge. So I will pick A
> - but the reason is gameplay rather than performance, in this case."*

- **A stays as arena 5, `hedge`; B is removed.** Its pinwheel note
  (section 2) stands as a fact about the box tree.
- **A's outer ring was not slower than B's.** The question was why fewer
  gaps should cost more. They do not: each arena's seeded poses are its
  own, about 50 in the outer ring, and one frame there runs 40,000 to
  330,000 clk. On A's own 200 poses B measures 136.6 kclk to A's 138.7 in
  the outer ring, and more everywhere else (court +8 to +22%, inner +14%,
  all +7.1%).
- **What the tall-wall test was.** It raised the walkway ITSELF to the
  hedges' height - no wall added. Its saving is mostly not what the tall
  wall hid: about three quarters is the outline path a low box takes,
  which section 4.1 attacks.
- **The edge is blue.** The fills are boxes of their own, so they take the
  room's blue (`COL_ROOM`), and the room's own walls (the north edge) go
  back to the room's colour from the hedges' green, as drawn.
- **Section 4.1 (low boxes as faces) and 4.2 (a box standing on a box)
  are asked for**, 4.1 with *"push a bit on it ... a lot of setup that can
  be optimized, reduced or even removed"*.

## 7. Faster low boxes (4.1, the owner's item 2)

**Instrument.** `tools/waposes.py record --profile p.json [--detail
PREFIX]`: each pose rendered once more single-stepped, the renderer's
cycles charged by routine over the whole set - a profile of 200 poses
rather than of four bench views. A change that must not change the picture
is checked by `waposes compare` on eight sets (the hedge with and without
figures at 40 and 80 columns, the Maze, heights at 40 and 80, the Twin, the
flat arena): every pose's text screen byte for byte, and its cycles.

**Where a low box goes** (the hedge, 200 poses, figures out: 133 kclk a
pose). The outline path is about a quarter of the frame, and its larger
half is SETUP, not drawing: `wa_lowbox` 4,600 clk a box before any edge
(the four corners' projections, two multiplies each, ~2,100; the faces,
heights and corners chosen, ~1,600; the passes' dispatch ~700),
`wl_draw` ~1,000 a pass (its cull's scan, its tag) and `wl_run` ~700 an
edge. The per-column loops are the rest. Every arena has the same shape.

**7.1 The shut test - kept.** A column is a direction, the same at every
height, so every cell of a low box lies between its silhouette's end
columns, which the walk has already made. When every column there is SHUT
by nearer tall faces it can draw nothing, and `wa_render` skips it with one
`repne scasw` before any corner is projected. The picture is identical in
all eight sets; Maze -2.0%, hedge -1.4%, Twin -1.2%, heights +0.2% (the
test's own cost, where nothing tall hides a low box).

**7.2 A clip test - tried and taken out.** Seen from below its top, a low
box's highest row is its top at its nearest silhouette vertex: one
multiply, then a scan for a column whose clip lies under that row. It made
every set slower than 7.1 alone (hedge -1.1% against -1.4%, heights +0.5%):
a low box in view wholly behind nearer LOW ones is rare, so the multiply
and the scan were paid for nothing. The setup ate the gain.

**7.3 The floor's pair once a frame.** `wa_render` makes the outline
path's floor pair where it clears the column state, and `wa_lowbox` no
longer tests the frame for it.

## 8. A box standing on a box (4.2, the owner's item 3)

*"Stackable boxes will help in future arenas too, so lets try it."*

**The model.** `Box(..., on=lower)`: a box STANDS on the box just before
it in the arena's list - both low solids, it strictly inside that one's
footprint and taller - and its faces rise from that box's top. A stack is
at most `WA_MSTK` (3) boxes. The ground is the tallest box over a point, so
everything that reads heights (`wa_floor`, `wa_hat`, the low tables, the
routes, the pickups' z) takes a stack as it is, and the game is unchanged.
The kind byte's bits 2 (it carries the next box) and 3 (it stands on the
one before) mark it: free on a solid, and every reader masks them off. The
hedge's stack is three boxes where it was nine: 31 boxes in all, 104
vertices.

**Drawing it.** The box tree holds a stack's bottom alone; `wa_lowbox`
draws the box standing on it between its own NEAR faces and the FAR side
of its top (`wl_child`), recursively, with its floor - its posts and floor
edges - at the lower box's top. Every ray from the eye crosses the lower
box's near faces first and its far edge last, so that order is right from
any eye, the player standing on a tier included. With the eye above the
lower box its far side is drawn after, on its corners, so those and its
faces' bits are saved round the call (74 bytes a level, `rep movsw`).

What `wl_child` tests before drawing, each MEASURED:
- **`wa_left`**: a lower box whose near faces shut every column ends the
  stack, as the walk's own stop would have.
- **The walk's hint corners** (a box wholly past the right edge's line or
  behind, or wholly left): the stack's upper boxes were drawn with no view
  test at first, so a player on the bottom tier set up the tiers behind
  them. Pose 125, standing on the bottom tier: 106 -> 155 kclk, fixed.
- **A tier's own PVS bit - tried and taken out.** It bought nothing
  (+0.07%). Its first version seemed to save 5%: a slip skipped tiers
  that should have been drawn, and the picture comparison caught it.

**The PVS.** A box that can be seen keeps every box it stands on (its bits
ORed down the stack): a tall tier over a hedge can show where its base
cannot. And the low pass (`pvs_lowseen`) let a nearer box hide a farther
one by its whole span along a ray, which for boxes that never overlap is
all in front; a stack's bottom runs on PAST the tier on it, and its far
edge was counted as hiding that tier. Only the part of a box in front of
another can hide it now. Every other arena's part is byte-identical.

**Measured** (the hedge's 200 poses, figures out, against the nine-box
stack; every other arena's pictures identical and its cost within 0.01%):

| region | nine boxes | stacked | |
|---|---|---|---|
| court | 138.5 | 117.1 | -15.4% |
| court, on a tier (48, 104, 160) | 152, 155, 125 | 132, 131, 110 | -12 to -15% |
| inner ring | 94.1 | 89.5 | -4.8% |
| outer ring, rooms | | | +0.1% |
| **all** | **131.3** | **122.7** | **-6.5%** |

22 of the 200 pictures differ, in 246 cells: the nine boxes' seams - a
doubled post where ring pieces met, a broken crease - and an edge that
steps a column differently now that it is one edge, not a piece's. A few
frames are dearer (+15 to +18 kclk, looking steeply down at the bottom
tier): there a ring piece's far edge shut every column before the others
came up, while a tier standing on a box is drawn before that far edge,
which it may hide.

Tried in the game by the owner (2026-11-15): the tiers' jumps and the
wall's gaps both work.

**What was left alone in the outline path**, and why. Its per-column loops
test a column both against its clip and for being SHUT; were a shut
column's clip always 0, the clip alone would answer both. But the tall
path, which closes nearly every column of a frame, would have to store
that 0 in five places - about 40 stores a frame to save about 70 compares:
some 0.5%, in the renderer's most-tuned loops. Not taken.

**The final pass, 2026-11-15**: `make` (the gates and the fast tier), and
the fourteen WireArena soak rows - `walift waarenas wasetup waroutes wabots
wascore wabotarms walink walinknet walinkudp walinkip walink4 walink4ip
waweapons` - all pass. `walink4` (four machines on UDP, wall-clock
deadlines) failed once with the other rows' emulators sharing the host
(142 s) and passed alone (47 s).

## 9. The owner's rulings, the second round (2026-11-15)

> *"Lets make all of the outer outer walls (the ones now blue) the full
> height, if that doesn't cost performance, so we don't have some higher
> and some lower. Original maze: Lets delete it, the new one is better."*

- **The edge is all of one height.** The fills round the rooms were the
  hedges' 384 already; the room's own walls (the north edge, behind the
  wall's north side) were 192, and are 384 now (`ARENA_ROOMH`). It does
  not cost - it SAVES: the hedge's 200 poses 122,740 -> 122,210 clk (-0.4%)
  at 40 columns, 60 seeded poses 202,840 -> 201,271 (-0.8%) at 80. The
  room is now as tall as the tallest box: its faces take the tall path,
  and from the wall's top the eye no longer sees over them.
- **The Maze is deleted; the hedge is arena 3** in its place, `hedge` on
  the setup's list, so the Twin stays arena 4 and nothing else renumbers.
  Its data, its bench views, its over-budget allowance (the hedge is
  inside the visible budget) and its arena part are gone. `waarenas` now
  checks the hedge where it checked the Maze: the stack's top a floor at
  160, its middle tier - a box standing on a box - one at 104, its lift
  onto the wall, and its first bench view.
- **On "a second outer wall".** The owner's drawing has none: the walkway
  is the outer wall, and beyond it only the edge. This arena keeps the
  tall fills as that edge round the rooms, which the owner likes the look
  of; the tall-wall test of section 3 raised the WALKWAY itself, not a
  second wall.

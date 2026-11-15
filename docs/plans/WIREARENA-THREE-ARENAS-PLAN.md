# WireArena: Crossing, the Pit and the Ziggurat

**STATUS: BUILT** (2026-11-15): A1 to A3; the Pit's cost is the owner's to weigh (section 8, A3). Written to the owner's three drawings below,
before any code; section 7 is the waves and section 8 what was built.

The owner, 2026-11-15, with three MS Paint drawings (*"not perfectly to
scale ... take them as general layout"*):

> *"The first is crossing, intended to be performant. If its not going to
> be let me know. No height or elevation changes on this one. Black are
> walls or pillars. Orange KoTH flag. Red team flags/team spawn sides.
> Yellow pickup locations. For this one, grenade/rocket inside the
> crossing, chaingun/shotgun/railgun in each corner. Health or shield in
> the center. Placed after the Pillars map (Heights remains the default
> selection, and next)."*
>
> *"Then The Pit. Very ambitious. Will need very high outer walls, or no
> outer walls at all. The top ring is the highest elevation. The black
> walls in the middle should prevent them from crossing the pit without
> going down into it. The second layer can get to the top with a hop. The
> pit is quite deep, and you ride the lifts on either side out. Yellow
> spawns between all weapons at all locations; there are no health packs,
> you are not meant to survive. Bots may need to learn how to jump; we
> could add a ramp, but this is already going to be iffy on performance."*
>
> *"Finally, Ziggurat. Unsquare very high boundary walls, 4 pillars, a very
> tall center tower, 4 ramps to the top of it. No CTF. Health, shield or
> railgun at the top. Chaingun, rocket launcher at the four corner spawns
> hidden behind the pillars out of view from the zigg."*

The arena format is [WIREARENA-HEIGHTS-PLAN](completed/WIREARENA-HEIGHTS-PLAN.md)
section 7 and `tools/arenagen.py`; the modes each arena plays come from its
data ([WIREARENA-MODES-PLAN](completed/WIREARENA-MODES-PLAN.md) section 6). All three
are DATA in `tools/arenagen.py`, plus the engine work section 5 names.

Coordinates are world units (64 a metre), the room 2048 square, y north.

## 1. Crossing (arena 5)

Flat: every box the same height (3 m, `WALL_H`), so the arena takes the
renderer's and the collision's FLAT paths (`wa_aflat`), the cheapest there
are - the Pillars' class of arena.

- **The crossing**: a plus-shaped corridor 2 m (128) wide through the
  middle, each arm reaching 512 from the centre (x and y 512..1536), its
  sides 1 m walls (a tall box keeps to the sight grid's 64), open at the
  four arm ends: four L-shaped corners of two boxes each.
- **Four pillars**, 2 m square, one in each quarter: x 320..448 and
  1600..1728, y 384..512 and 1536..1664.
- **The room's walls** as every walled arena's.
- **Pickups**: a rocket or grenade launcher in the north and south arms;
  health or shield at the centre; a chaingun, shotgun or railgun in each
  corner of the room.
- **Modes**: all four - the flags at the middle of the west wall (red) and
  the east (blue), the sides west and east, the hill at the centre.
- **The routes**: a ring round the room's edge, the four arms to the
  centre; two of the bots' home loops through the crossing.

## 2. The Pit (arena 7)

Three levels: the **pit** floor (0), the **second layer** round it (192, 3
m: deeper than an eye, so from the pit its sides are walls) and the **top
ring** round that (240: a hop above the second layer - 0.75 m, which the
jump's 1.08 m clears). No room walls: the arena stands in the dark, its
edge the room's (the collision's room test keeps the feet in).

- **The pit**: x 384..1664, y 512..1536, 20 x 16 m.
- **The second layer**: from the pit's edge to 2 m from the room's - bands
  4 m wide west and east, 6 m north and south.
- **The top ring**: the outer 2 m all round.
- **The dividers**, north and south: a tall block across the top ring (x
  576..1472) and a tall block across the second layer from it to the pit's
  edge (x 896..1152), so the west half reaches the east only through the
  pit. Tall: 7 m, over any eye on the ring.
- **Two lifts**, against the pit's west and east sides, from the pit floor
  to the second layer.
- **Pickups**: four, on the second layer beside the dividers, each spawning
  as any weapon (shotgun, chaingun, grenade, rocket, railgun). No health,
  no shield.
- **Modes**: all four - the flags on the top ring at the middle of the west
  and east (red, blue), the sides west and east, the hill at the pit's
  centre.
- **Spawns** on the second layer and the top ring (section 5.1).
- **The bots**: down into the pit by a DROP, out by a lift, onto the top
  ring by a HOP (section 5.2).

## 3. The Ziggurat (arena 6)

- **The walls**: the room's, very high (8 m), the four corners cut by tall
  boxes stepped three to a corner (section 8, A2) - the octagon of the drawing, in the
  boxes the arena format has (no diagonal walls).
- **The tower**: 4 m square (x and y 896..1152) and 4.5 m tall (288) - the
  tallest a flight reaches (15 treads of a step's 24 at most).
- **Four ramps** up its four sides, 2 m wide, 6 m long, 12 treads each.
- **Four pillars**, 2 m square, between the tower and each corner.
- **Pickups**: health, shield or a railgun on the tower's top; a chaingun
  or rocket launcher at each corner, behind its pillar from the tower.
- **Spawns**: the four corners, behind the pillars - west red, east blue.
- **Modes**: free-for-all, team deathmatch, king of the hill (the hill on
  the tower's top). No CTF: no flags.

## 4. The setup's list

The Arena list's order: open, pillars, **crossing**, heights, hedge, twin,
**the pit**, **ziggurat**. The heights stays the default. The list's order
becomes a table (item to arena and back) where `wa_sarmap` swapped two.

## 5. The engine

1. **A spawn on a platform**: a respawn point may stand on a box's top (the
   Pit's), not only on the floor. arenagen checks it stands on the top of
   whatever is under it; the game's spawn already reads the floor afresh
   and steps the feet onto it.
2. **The bots' HOP**: a route leg kind (arenagen's) rising more than a step
   and within the jump (apex less 8): the bot's height follows the floor
   onto the higher box. On the guest it is a walk; the way back is a drop.
3. **A flag base on a platform**: a base's height rides in its record (the
   byte after the node, halved), and a flag at home stands there.
4. **Eight arenas**: the part rows, the Makefile's list, the setup's tables
   and the rotation's window (its rows nearer, so eight fit a CGA screen).

## 6. Performance

Measured with `tools/waposes.py` (random poses, exact render cycles) on the
5150 with CGA against the existing arenas. Crossing is meant to be as cheap
as the pillars; the Pit is the owner's *"iffy"* - if its frames run far
above the hedge's, that is said with the numbers and the plan offers the
cut (a lower ring, fewer boxes) before going further.

## 7. Waves

- [x] **A1 Crossing**, the setup's eight-arena table, the rotation window
  fitting eight. Checked by `waarenas` (its rows' new checks) and
  `wamodes`' lists; measured.
- [x] **A2 The Ziggurat**. Checked by `waarenas`; measured.
- [x] **A3 The Pit** with sections 5.1 to 5.3. Checked by `waarenas` (a
  lift out of the pit, a hop to the ring, the bots' ride and hop) and
  `wactf` on its flags; measured.

## 8. As built

**A1** (2026-11-15): Crossing is arena 5, `_crossing()` in
`tools/arenagen.py`: 12 boxes, all 3 m (the flat paths), 15 route nodes,
six respawns (two a side, two neutral), seven pickups, all four modes. The
walls are 1 m, not the drawing's thin lines: a tall box keeps to the sight
grid's 64. The setup's order is arenagen's `ARENA_ORDER`, emitted as
`WA_ORD<item>` and `WA_ITM<arena>`; `wa_sarmap` and `wa_sitem` read the two
tables, and the Arena list's names are built from them. The Rotation window
opens at y 22 so eight rows fit a CGA.

**Measured** (`tools/waposes.py record --width 40 --n 40`, the 5150 with
CGA): Crossing **135,257** clk a frame on average (max 246,450), Pillars
**123,749** (max 201,005) - 9% dearer than the cheapest walled arena, as
the owner intended.

**A2** (2026-11-15): the Ziggurat is arena 6, `_ziggurat()`: 25 boxes - the
corners cut by three tall steps of 2 m each, the tower 4 m square and 288
high, four 12-tread flights, four pillars - 14 route nodes, four corner
spawns (west red, east blue), six pickups. Modes: free-for-all, team
deathmatch, king of the hill (the hill on the top). **The top's pickups are
two, not one**: the owner's *"health, shield or railgun"* mixes a weapon
with health, which a single spawn's choices may not (arenagen's rule, the
owner's: health and shield apart from weapons) - so health or shield at the
top's south edge and a railgun at its north.

**A bug the ramps found**: a flight's tread count is the high nibble of its
kind byte, and the game's `wa_hat` read bit 7 as a LINE (a wall to feet)
before asking the kind - so a flight of 8 treads or more was a wall. The
heights' flights have 4. `wa_hat` asks the kind first now; `waarenas`'s
climb of the south ramp was red before (the feet stopped at its foot).

**Measured** (120 poses, 40 columns, the 5150 with CGA): the Ziggurat
**226,405** clk a frame; the hedge **167,164**; with two corner steps
instead of three, **207,356**. No routine dominates the profile - it is the
box and vertex count, the ramps' outlines and the tower over the eye. Two
steps would save 8% at the octagon's look; three were kept, the owner's to
call.

**A3** (2026-11-15): the Pit is arena 7, `_pit()`: 18 boxes - the ring at
240 in six pieces, the layer at 192 in six, four dividers at 448, two lifts
- 27 route nodes, six spawns (three a side, on the layer and the ring), four
pickups each any weapon, all four modes (the flags on the ring, the hill at
the pit's centre). No room walls (`ARENA_ROOMH` 0); the collision's room
test keeps the feet in.

- **5.1, spawns on a platform**: arenagen's `on_top` - the square over one
  height alone, no taller box over it and no lower one under part of it -
  takes the respawns, the start and the flag bases. The game needed
  nothing: a spawn's feet start at 0 and the first tick's floor read steps
  them onto the box (`wa_hjump`'s `.onto`).
- **5.2, HOP**: arenagen's leg kind 4, one way, rising at most the jump's
  apex less 8 (61); emitted as a WALK, so on the guest the bot's height
  follows the floor onto the ring (`wa_figz`); the way back a DROP.
- **5.3, a base's height**: the byte after the base's node is its height
  halved; `wc_home` stands the flag there.
- **Checked by** `waarenas`: the start on the layer, CTF's flags at the
  ring's 240 (red with the height dropped), a lift out of the pit, a hop
  onto the ring, and the bots riding a lift and hopping onto the ring
  within two minutes; `wamodes` and `wamodeslink` take the new rotation
  (after the Twin, king of the hill goes to the pit, then the ziggurat).

**Measured** (120 poses, 40 columns, the 5150 with CGA): the Pit **242,017**
clk a frame (51 ms) against the hedge's **167,164** and the Ziggurat's
226,405 - the dearest arena by 7%. The profile names the cost: the
low-box outline path (`wa_lowbox`, `wl_rtop`, `wl_rfloor`, `wl_draw`,
`wl_run`, `wa_occlow`) is about 75,000 clk a frame, 31% - from the ring and
the layer nearly everything is below the eye, so it is drawn as outlines.
The cuts, the owner's to choose: the ring the layer's height (flags and
spawns on the layer, no hop) takes six boxes out of every outline pass; a
single-piece layer band each side (the dividers standing on it) takes four
more.


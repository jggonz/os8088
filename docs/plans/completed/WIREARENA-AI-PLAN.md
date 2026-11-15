# WireArena: bots that shoot back, and the match around them

**STATUS: BUILT** (reconciled 2026-11-15): waves B1 to B4 and two rounds
of play notes (section 8). Section 1 records the owner's rulings on what
was left of WireArena (2026-11-14), and 1.1 where each one stands now;
sections 2–6 are the bots and the match flow, priced; section 7 the
questions and the owner's answers; section 8 what was built and measured.
Filed under `completed/` on 2026-11-15: what 1.1 left open, after the
owner's rulings of that day, is carried in
[MULTIPLAYER-FPS-PLAN](../MULTIPLAYER-FPS-PLAN.md) 17.

WireArena is `apps/wirearena/`. Its history is
[MULTIPLAYER-FPS-PLAN](../MULTIPLAYER-FPS-PLAN.md) §16,
[WIREARENA-HEIGHTS-PLAN](WIREARENA-HEIGHTS-PLAN.md) §9 and
[WIREARENA-WEAPONS-PLAN](WIREARENA-WEAPONS-PLAN.md). The computer opponent
was proposed in MULTIPLAYER-FPS-PLAN §9; this plan replaces that section's
design with the owner's brief: **fast more than good**.

Claims are marked as in the parent plans: **Measured** (a number taken, the
method given), **Derived** (arithmetic from measured unit costs) and
**Forecast** (a cost for code nobody has written yet).

## 1. What is left, as the owner ruled (2026-11-14)

| item | ruling |
|---|---|
| **Bots / shooting back** | **Next.** Fast more than good: routes like today's patrols, with branches a bot picks between; a time to shoot and an accuracy. This plan |
| **Player death, respawn timer, respawn points** | Yes - and a **scoreboard on a held Tab**; 40/80 columns move to **- and +** (Clear Skies' view-size keys). Section 5 |
| **Dummies on lifts** | Yes. Section 3.4 |
| **The serial link, then Ethernet and the lobby** | After the bots (MULTIPLAYER-FPS-PLAN waves 4–5) |
| **kern_mega** | Before shipping: tested, with soak rows of its own |
| **Keyboard-only aiming** | Adjust it (turn acceleration) - or the owner tests serial play with the PicoMEM v2 that is in the 5150 for Video Battle |
| **Real-CGA snow** | **Accept it; the user picks.** 80 columns is not performant on a 5150 today, and the 5150 is the only machine with a snowing card (MULTIPLAYER-FPS-PLAN §14 question 1, answered) |
| **Game modes** | Yes to all: duel, free-for-all with bots, last stand, and **capture the flag** especially |
| **Jump pads, teleporters** | Liked: they connect points of a map |
| **Replay, kill-cam** | A good feature |
| **More arenas** | **Last**, after every change that affects performance |
| **Footsteps, more sounds** | Maybe |
| **Radar** | Maybe: a prototype first. Maps are small and will stay small |
| **Two players on one 5150** | Too slow on a 5150; a bonus after everything else |

### 1.1 Where each stands (2026-11-15)

- **Built**: bots that shoot back (this plan, 8); death, respawn and Tab's
  board (8.3); dummies on lifts (8.1); the serial link, then Ethernet and
  the lobby ([WIREARENA-LINK-PLAN](WIREARENA-LINK-PLAN.md),
  [WIREARENA-NET-PLAN](WIREARENA-NET-PLAN.md)); real-CGA snow - the player
  picks 40 or 80 columns in the setup.
- **Game modes** (the owner, 2026-11-15): duel and free-for-all are what
  the game plays already - a duel is one other bot, free-for-all the main
  mode. **Team deathmatch** joins the list, and it and capture the flag
  spawn a player on their own SIDE only. Capture the flag is next:
  [WIREARENA-CTF-PLAN](WIREARENA-CTF-PLAN.md). Last stand is still to
  come. *(Since: capture the flag, team deathmatch and king of the hill are
  built - CTF-PLAN, [WIREARENA-MODES-PLAN](WIREARENA-MODES-PLAN.md); last
  stand is an idea for after 1.0, MULTIPLAYER-FPS-PLAN 17.4.)*
- **Open, not started**: kern_mega's linked match and its soak rows
  (LINK-PLAN L3.3); keyboard turn acceleration; jump pads and teleporters;
  replay and kill-cam; a radar prototype; footsteps and more sounds; two
  players on one 5150. More arenas come last, after every change that
  affects performance.
  *Since the owner's rulings (2026-11-15, MULTIPLAYER-FPS-PLAN 17.1):
  kern_mega is another session's; jump pads and teleporters are not in this
  release; footsteps were tried and removed; two players on one system is
  dropped. The rest is MULTIPLAYER-FPS-PLAN 17.3 and 17.4.*

## 2. The shape: a bot is a figure that walks a ROUTE and rolls dice

Today's dummies already cost what a bot's body costs: a figure record, a
patrol walked a leg at a time with the leg's step worked out once
(`wa_figtick`, ~780 clk a figure a tick, Measured: the whole-frame traces
of WEAPONS-PLAN 3.3). A bot adds a little to that, and nothing a frame:

- **where to go**: at the end of each leg, a branch picked (section 3);
- **what it sees**: the player, every fourth tick, along a grid (section
  4.1);
- **when it shoots, and whether it hits**: a reaction delay, a gap between
  shots, an accuracy roll (section 4.3);
- **which way it faces**: turned toward its target by the sign of a cross
  product, 48 a tick, as figures turn today (section 4.2).

No bot ever traces a ray through the renderer, plans a path at run time or
takes a square root. Everything that is search is done on the host by
`tools/arenagen.py`, which already builds and checks the patrols, and
everything at run time is a table lookup, a compare or a dice roll.

**The renderer does not change** for a bot: a bot is drawn as a dummy is.
The one exception is the firing cue (4.3), a figure's attribute.

**For the network later**: a bot's state is a figure record (where it is,
which way it faces, its animation) plus events (it fired, it hit, it died).
That is exactly what a remote player's packets will carry under the owner-
authority model (MULTIPLAYER-FPS-PLAN §8.1). So the bots exercise the same
records and events the serial and Ethernet waves will fill, and a match
against bots is a match against "players" whose input happens to be local.

## 3. Routes

### 3.1 The route graph (arenagen)

An arena's patrols become a GRAPH: **nodes** (points on the floor, a
platform or a lift) and **legs** between them, each walkable both ways.
arenagen checks every leg as it checks a patrol today: every tick's rise
within a step, no wall or lift crossed, and bit 15 where the leg leaves the
ground (`wa_figz` reads the floor only there).

| table | what | bytes (Forecast, 24 nodes and 40 legs) |
|---|---|---|
| nodes | x, y; its legs' indices (up to 4) | ~200 |
| legs | its two nodes; its heading and step count each way | ~320 |
| next hop | for each node and each GOAL node, the leg to take first (a breadth-first search on the host) | 24 x 24 = 576 |
| nearest node | for each 128-unit PVS cell, the nearest node reachable from it | 256 |
| tall grid | a bit a 64-unit cell, set where a tall box stands (section 4.1) | 128 |

About 1.5 KB more in the arena's part, copied into the game's half at load
(the game's segment has ~44 KB free). **arenagen refuses** a graph with an
unreachable node, a leg no walker can walk, or a tall box off the grid.

### 3.2 Picking a branch

At the end of a leg, a bot picks its next leg by its STATE (4.4):

| state | picks |
|---|---|
| roam | a random leg, not the one it came by (unless it is a dead end) |
| hunt | the next hop toward the node nearest where it last saw the player (the nearest-node table, then the next-hop table: two lookups) |
| engage | it keeps walking its leg - a moving target is harder to hit - and turns to face the player |
| fetch | the next hop toward a pickup's node (health when low, a weapon spawn when it has only the pistol) |

**Cost: two table reads a leg** (Forecast: under 200 clk, a few times a
second).

### 3.3 Weapons for bots

A bot walking over a pickup's node takes it as the player does
(`wa_pktake`'s checks, against the bot's own health, shield and arms). A bot
fires its best weapon for the range: the shotgun close, a projectile far.
Projectiles fly through the existing code; they need an OWNER, so a rocket
does not hurt the bot that fired it. Wave 4.

### 3.4 Bots on lifts

`wa_ltick` counts a rider who is the player. It will count any figure
standing on the lift (four compares a figure a tick). A leg that ends on a
lift is a WAIT: the bot stands until the lift is up, then walks off on the
leg that leaves it. The bot's floor is read on that node (bit 15), so it
rides as the player does. This is HEIGHTS-PLAN wave 4's remainder.

## 4. Seeing, turning, shooting

### 4.1 Line of sight: a grid walk, every fourth tick

*Since (MULTIPLAYER-FPS-PLAN 16.10, 2026-11-14): a ROAMING bot looks every
other turn of its own, 16 ticks apart; an engaged, hunting or fetching one as
below.*

The tall boxes are the only occluders, and they all stand on a 64-unit grid
(arena 2's 7 tall boxes do; arenagen will refuse one that does not). So a
bot sees the player when a walk from it to the player, a 64-unit step at a
time, enters no tall cell.

**Measured on the host** (`arena 2`, 20,000 random pairs of free floor
points, against exact segment-against-box tests):

| step | wrong | of which "sees through" | steps: mean / 95% / max |
|---|---|---|---|
| **64 (a cell)** | **1.81%** | all of them | **11.3** / 23 / 31 |
| 32 | 0.92% | all of them | 21.8 / 46 / 62 |
| 16 | 0.45% | all of them | 42.9 / 91 / 124 |

It is never wrong the other way: a clear line is never called blocked. The
errors are lines that graze a wall's corner within a few units. Sees
through a corner's tip, a bot can shoot the player round it by a hair.
**Recommended: 64.**

**Cost** (Derived): a step is a fixed-point add on each axis and a bit
test, ~70 clk on the 8088: ~800 clk a check on average. Staggered, each
bot checks every fourth tick: **~200 clk a bot a tick**.

Low boxes (platforms, ramps) do not block a bot's sight. Heights matter
only through the tall boxes, as in the renderer's PVS (HEIGHTS-PLAN 9.8.2).
A bot also notices the player only within 90 degrees of its facing, unless
it is already hunting or engaging (one dot product's sign).

### 4.2 Turning to face: a cross product's sign

An engaging bot turns toward the player by up to 48 a tick (`wa_figtick`'s
turn rate). The direction is the sign of `dx sin - dy cos` at its facing,
two multiplies. Its facing is where it shoots: no arctangent anywhere. A
bot turns while walking its leg (its body faces the player, its legs carry
it on), which the animation already allows.

**Cost**: ~300 clk a tick per engaging bot (Derived).

### 4.3 Shooting: a reaction, a gap, a roll

| | easy | normal | hard |
|---|---|---|---|
| reaction (from first sight to the first shot) | 24 ticks (0.8 s) | 15 (0.5 s) | 9 (0.3 s) |
| accuracy at 4 m, standing target | 45% | 60% | 75% |
| ...falls by | 5% a further 4 m, and a quarter more if the player is moving | | |

- A shot fires once the bot is facing the player within a few degrees,
  past its reaction and its weapon's gap.
- **A hit is a dice roll** against that accuracy (the weapon spawns' LCG).
  The weapon's damage goes to the player through `wa_phurt`, which already
  takes the shield first.
- No ray, no TRACE: line of sight said the player is visible.
- A projectile is launched along the bot's facing, and hits or misses by
  flying.

**The player must SEE it coming:**

- The firing bot's figure draws yellow for two ticks: `wa_figure` already
  picks an attribute by `F_FLASH`; a flag picks `A_CROSS`'s yellow. That
  is one byte test a figure.
- Each weapon's tone, as the player's.
- A hit on the player flashes the HUD's heart and plays a low tone.

**Cost**: a few compares a tick, and a roll a shot (Forecast: under 100
clk).

### 4.4 States

| state | enters when | leaves when |
|---|---|---|
| roam | it spawns; it lost the player 5 s ago | it sees the player (hunt or engage) |
| engage | it sees the player | it loses sight (hunt) |
| hunt | it lost sight | it sees the player (engage); 5 s pass (roam) |
| fetch | its health is under 40, or it has only the pistol and a spawn is near | it has the pickup, or it sees the player |

A byte a bot, and one switch at the leg's end or on a sight change.

### 4.5 What it costs, in all

| | a bot a tick | 3 bots, a frame at ~15 fps (2 ticks) |
|---|---|---|
| walking its route (today's patrol, Measured) | ~780 clk | (already paid today) |
| line of sight, every fourth tick | ~200 | ~1.2k |
| facing an engaged target | ~300 (only while engaging) | up to ~1.8k |
| states, branches, shots, rolls | ~100 | ~0.6k |
| **added** | | **~2–4k clk: 0.4–0.8 ms, 1–2% of a frame** (Forecast) |

The renderer pays nothing new but the firing cue's byte test. A bench row
will measure it on the P bench with the bots running, against the dummies.

## 5. Death, respawn, the score

- **The player dies at 0.** The view tips to the floor: the eye drops
  over half a second, nothing is drawn differently. Then a **respawn timer**
  (3 s, its count on the HUD) and a **respawn point**.
- **Respawn points**: a few per arena, from arenagen. The game takes the one
  farthest (by the sum of the axis distances) from the nearest live bot:
  four compares a point, at a respawn.
- **Bots respawn the same way**, at a respawn point's nearest node, no
  longer at their patrol's start.
- **The score**: kills and deaths per player and per bot. A kill is
  credited to whoever fired the shot or the projectile (projectiles carry
  an owner, 3.3).
- **The scoreboard, while Tab is held**: an overlay box of names, kills and
  deaths, built once when Tab goes down. Two ways to draw it (question 2):
  - over the live view: up to ~100 overlay cells, ~2 ms a frame while held
    (Derived from WEAPONS-PLAN 3.3's overlay cost);
  - in place of the view: free while held, and a full redraw when it is
    let go.
- **40/80 columns move to - and +**, as Clear Skies sizes its view, and Tab
  is the scoreboard's.
- **A match's end** (a kill limit, or a time) is the game modes' (after the
  network waves), not this plan's.

## 6. Waves

Each ends with its outcome on the glass and its rows named; the soak union
and `test-full` run once after the last (CLAUDE.md).

| wave | builds | outcome | checked by |
|---|---|---|---|
| **B1: routes** | the route graph, next hop, nearest node (arenagen and its checks); bots walking it with ROAM's random branches; bots on the lift | the dummies wander the whole arena, ride the lift to the tower | arenagen's refusals; a soak row: every leg walked by some bot within N minutes of sim, the lift ridden, no figure ever inside a wall |
| **B2: seeing and shooting** | the tall grid and the sight walk; facing; ENGAGE and HUNT; reaction, gap, accuracy; the firing cue; hits on the player | the bots turn, shoot back, and hit at the difficulty's rate | a soak row: a staged duel per difficulty, hits counted against the expected rate within a bound; sight against the host's exact test on sampled poses |
| **B3: death and the score** | the player's death and respawn timer, respawn points, bots' respawns, kills and deaths, Tab's scoreboard, - and + | a match that goes on: die, wait, respawn, the score on Tab | a soak row: deaths, respawn points chosen away from bots, the score's counts |
| **B4: bots' arms** | FETCH; bots taking pickups; bots firing every weapon by range; projectiles' owners | bots that pick up the rocket launcher and use it | the row's duel with each weapon |

Measured at the end of each wave, as the weapons were: the P bench with bots
against dummies, and the heights poses.

## 7. Questions for the owner

1. **Line of sight's step**: 64 (1.8% corner grazes seen through, ~800 clk a
   check) or 32 (0.9%, twice the cost)? Recommended: 64.
2. **The scoreboard**: over the live view (~2 ms a frame while Tab is held)
   or in place of the view (free while held)? Recommended: over the view -
   in a fight, holding Tab should not blind you.
3. **Difficulty**: chosen how? Recommended: three items on the Game menu
   (Easy, Normal, Hard), remembered for the session.
4. **How many bots**: today's three figures (the PARTS-PLAN ruling: at most
   three). Keep three for bots-only play, and later share them between bots
   and remote players?
5. **The bots' names** on the scoreboard: fixed (Bot 1-3), or a short list
   of names?

### 7.1 The owner's answers (2026-11-14)

1. **64** - "slightly seeing around corners is not a big issue".
2. **Over the view** - try it, and see whether it is both readable and
   performant.
3. **The Game menu**, yes - though the menu "will need a lot of work in
   general before we go live (match setup)".
4. **Three, shared** with remote players. More might be a 286+ thing later,
   if it is easy.
5. **A short list of robotic names.**

## 8. As built

### 8.1 B1: routes, and bots on the lift (2026-11-14)

**The graph** (`tools/arenagen.py`: `graph_of`, `graph_edges`,
`graph_block`). The Heights arena's is laid by hand (`HEIGHTS_GRAPH`):

- 18 nodes, 27 legs, 51 one-way edges;
- its three old patrol loops, unchanged, as the bots' HOME loops: over the
  ramps (split at the platform's middle, the shotgun spawn), the annex's
  hallways and corridor (through the health pack), and round it all;
- seven legs between the loops;
- the lift: a leg onto it at rest (`LIFT_ON`), one off it raised onto the
  tower (`LIFT_UP`, through the rocket spawn), and a DROP off the tower's
  west face.

The flat arenas' graphs are PATHS' three loops joined by four legs
(`PATH_LINKS`): 14 nodes, 36 edges.

arenagen refuses a leg that runs into a wall or climbs more than a step (a
drop may fall any height), a node with more than four ways out, a node that
cannot reach every other, and a home loop that is not walkable. The checks
replace the patrols' own.

**The bytes**: the graph rides in the arena's part (descriptor words 38 and
40, where its patrols were): 762 bytes for arena 2, 550 for the flat ones.
The game copies it straight out of the part at load, into `wa_gph`
(`WA_GPHSZ`). The renderer never sees it, so the patrols' block is gone from
ARENA in both halves.

**The walk** (`wirearena.asm`):

- a figure's `F_PATH` names the edges and `F_LEG` its edge, x 12;
- an edge's first 8 bytes are a patrol leg's row, so `wa_figtick` walks an
  edge as it walked a leg;
- at an edge's end, `wa_bnext` takes the node's next edge: at random, any
  but the way back (the way back from a dead end);
- in a bench (`wa_bdet`) it takes the node's FIRST edge, its home loop's
  next, so the benches' dummies walk their old patrols exactly. The graph
  puts each loop's own leg first.

**The lift**: an edge onto it waits at its start (`F_BWAIT` 1) for the lift
at rest. One off it waits ON it (`F_BWAIT` 2), and a waiting bot counts as
a rider (`wa_lbn`, read by `wa_ltick` the next tick). So the lift rises
under the bot, and the bot walks off at the top. While waiting, its floor
is read every tick, so it rides up with the lift.

**`F_SIZE` is 56** (the bots' bytes, 44–55, the game's alone; `WR_ABI` 6).

**Checked**: `tests/waroutes.py`, a soak row (~45 s).

- **Two minutes of roaming**: every bot left its home loop, 50 of 51 edges
  were walked, no figure stood in a tall box, and on every climbing edge
  every figure stood on its floor.
- **Bot 0 at the lift's foot**: it waited, rode to 160, walked onto the
  tower, dropped off, and the lift went home.
- **Red** with the branch never taken (4 checks) or the bot not counted as
  a rider (2).
- `waweapons` now stands the bots on their home loops: it is about the
  weapons.

**Speed**: the benches as before within 0.3 ms (P 20.1 / 16.7; heights 40
13.2 / 14.9 / 33.0 / 12.5 / 26.0). The benches walk the home loops, so
their scenes are the same; the cost is a call at each edge's end and a byte
test a figure a tick.

### 8.2 B2: seeing, facing, shooting back (2026-11-14)

**The tables** (arenagen, after the graph's edges in its block):

- **the sight grid**: the tall boxes, a bit a 64-unit cell, 128 bytes.
  arenagen refuses a tall box off the grid. The game spreads it to a byte a
  cell at load (`wa_sgrow`, 1 KB), so the walk tests a byte, with no shift
  or mask;
- **the nearest node to each PVS cell's middle**, among the nodes it sees
  (256 bytes);
- **the next hop**: for each node and goal, the edge to take first, by
  shortest steps (a lift's legs dearer) - 18 x 18 bytes for arena 2.

**A bot's thinking** (`wa_bthink`, a call a bot a tick, after its walk):

- **Its look**, every eighth tick on its own turn (the three bots'
  staggered):
  - the SIGHT WALK (`wa_sight`): quarter-unit fixed point, so a cell's
    column and row are whole high bytes; a column table; about ten
    instructions a 64-unit step;
  - the BEARING (`wa_bear`): an octant's ratio by one divide, through a
    65-byte arctangent table;
  - a roaming bot notices only within 90 degrees of its facing.
- **Its states**: ROAM; ENGAGE on sight (its reaction starts, and the node
  nearest the player is noted); HUNT when lost - the next hop toward that
  node at each node (`wa_bnext`) - and ROAM again there, or after 5 s.
- **Its turn**: an engaged bot turns toward the bearing of its last look,
  48 a tick, onto it when within that; `wa_figtick` leaves its facing
  alone. Already on it, the tick costs one compare.
- **Its shot** (`wa_bshoot`), on the bearing, past its reaction and gap,
  seeing the player:
  - a dice roll against its accuracy: 5 points less each further 4 m (the
    axes' larger distance and half the smaller), a quarter less while the
    player moves;
  - a hit is 20 to the player (`wa_phurt`: the shield first), a low tone,
    and the HUD's heart white on red for 6 ticks;
  - its CUE: `F_FLASH` 0x80 + 6, which the renderer draws in `A_FIRE` -
    yellow; on Hercules and MDA normal intensity against the body's bright.
    Five ticks, so a 10 fps frame at 80 columns cannot miss it (three were
    missed on Hercules, looked at).

| | easy | normal (the default) | hard |
|---|---|---|---|
| reaction, ticks | 24 | 15 | 9 |
| between shots | 30 | 20 | 14 |
| accuracy at 4 m | 45% | 60% | 75% |

The Game menu has three items, **Bots: easy, normal, hard**, each with a
toast. *(Since 2026-11-15 there is no Game menu: the match setup's Skill
drop-down is the one place - WIREARENA-NET-PLAN 9.9.)* A bench (and `walift`, `waweapons`) runs with `wa_bdet` set: the
bots walk their home loops and do not think.

**What it costs, measured** (MartyPC `os8088_5150_cga`, a breakpoint trace
of `wa_bthink` in play, Heights 40, the player standing at the start, the
bots roaming and mostly engaged; 149 frames):

| build | a call | a frame | of the frame |
|---|---|---|---|
| first (facing by sin, cos and four 32-bit products every tick; a look every 4th) | 3,131 clk | 22,691 | **5.9%** |
| the bearing by table, the turn by compare | 1,693 | 11,728 | 3.2% |
| the byte grid, quarter units; non-engaged bots look every 8th | 1,302 | 9,465 | 2.5% |
| **every look every 8th; on the bearing, one compare** | **845** | **5,889** | **1.6%** |

A look is now ~2,160 clk for the sight walk and ~720 for the bearing. The
benches are as before within 0.4 ms (P 20.0 / 16.6; heights 40 13.1 /
14.8 / 32.7 / 12.5 / 25.9).

**Checked**: `tests/wabots.py`, a soak row (~32 s).

- **The menu**: 'Bots: hard' sets the difficulty.
- **Sight**: 40 of 40 random placements agree with arenagen's
  `sight_clear`, the same walk on the host. On the host that walk is wrong
  in 1.73% of random lines against exact geometry, never calling a clear
  line blocked.
- **The duels**, at each difficulty, a bot 400 units off, 600 ticks: it
  turns onto the player (0 degrees off once on it); 20 / 29 / 40 shots
  against 19 / 29 / 40 expected; 45% / 72% / 75% hits against 45 / 60 / 75;
  its cue and the heart's flash seen.
- **The hunt**: lost, it went to the node nearest the last sighting and
  roamed again.
- **Red** with the turn's sign reversed: it never comes on target, so it
  never fires.

The routes', weapons' and lift's rows still pass. `walift` now holds the
bots on their home loops: a bot's shot killed the rider.

### 8.3 B3: death, respawn, the score, Tab's board (2026-11-14)

- **The player's death**:
  - `wa_phurt` at 0 sets a 3 s timer (`wa_pdead`). While it runs, the keys
    and the trigger do nothing, pickups are not taken, the chaingun stops,
    and the bots stop thinking;
  - the eye sinks 6 a tick to 20 over the floor (`wa_ptick`);
  - "RESPAWN n" counts in the view's middle, rebuilt a second at a time;
  - then `wa_prespawn`: the player is back at the respawn point farthest
    (the sum of the axis distances) from the nearest live bot, whole, with
    the pistol alone.
- **Respawn points** (arenagen, after the next hops): 5 in the Heights
  arena, 4 in the flat ones, each on free floor, with the route node it
  sees nearest. A bot whose time dead is up comes back at the point
  farthest from the player, walking on from that node (`wa_brespawn`; a
  bench, at its home loop's start).
- **The score**: kills and deaths for YOU and the three bots (bytes).
  - The player's weapons credit the player's kills.
  - A bot's shot that kills the player credits that bot (`wa_hurtby`).
  - The player's own rocket is a death and a kill less (not below 0).
- **The names**: three of eight robotic names, all different, picked at
  random at the match's start: COG, RIVET, SERVO, SPROCKET, DIODE,
  PISTON, BOLT, GEAR.
- **The board, while Tab is held**, over the live view, as the owner asked:
  - a 20-cell box over the middle: a header, then YOU and the robots by
    kills, most first *(since 2026-11-15 on rows 14 to 18, under the
    arena's minimap on rows 1 to 13 - WIREARENA-NET-PLAN 9.7)*;
  - white on blue (inverse on mono), the header yellow, YOU bright green;
  - the overlay's (`WA_MOV` 160), rebuilt as it comes and goes.
- **The width moves to - and +** (and the keypad's), Clear Skies' keys. The
  help says so *(the help is now the About card, the setup's Keys)*.

**Readable**: on CGA at 40 columns, yes (looked at). **What it costs**: the
P bench with Tab held all through it is **53.3 ms a frame against 49.9 at
40 columns (+3.4 ms, 18.7 fps against 20.0), and 63.5 against 60.0 at 80**.
That is about 100 opaque cells re-stamped every frame (WEAPONS-PLAN 3.3's
overlay cost). With Tab up it costs nothing.

If that is too dear, a cheaper board drops its background: only the
letters and numbers, ~45 cells, the view showing between them (about
half the cost, and harder to read).

**Checked**: `tests/wascore.py`, a soak row (~11 s), covering everything
above. It is red with the chooser taking the first point, and with a bot's
kill not credited.

**Speed**: the benches as before within 0.4 ms (P 20.0 / 16.6; heights 40
13.1 / 14.8 / 32.7 / 12.5 / 25.9).

The renderer part is now 61,780 bytes (the bigger overlay), with 3,244
left of 65,024.

### 8.4 B4: the bots' arms (2026-11-14)

- **Taking** (`wa_btake`): at each node a bot reaches, a present pickup
  whose node it is, if it helps: health below 100, the shield below 100, a
  weapon not its own or low. It takes the weapon and a pickup's rounds
  (`F_BWPN`, `F_BAMMO`). arenagen's table gives each pickup the route node
  within 40 of it: health 7, the ramp platform's spawn 1, the tower's 16;
  the shield none (it needs a jump).
- **FETCH** (`wa_bfetch`): a roaming bot below 40 health, with the health
  pack there, or with the pistol alone and a weapon spawn there, makes the
  pickup's node its goal and walks the next hops to it (HUNT's walk).
- **Firing by weapon** (`wa_bshoot`):

| | pistol | shotgun | chaingun | grenades | rockets | railgun |
|---|---|---|---|---|---|---|
| hit | 20 | 60 within 4 m, 35 within 8, 15 past | 8 | a projectile | a projectile | 100 |
| gap | the difficulty's | 30 | 4 | 30 | 36 | 45 |
| accuracy | the difficulty's | +15 | -10 | (it flies) | (it flies) | the difficulty's |

  A weapon out of rounds falls back to the pistol.
- **A bot's projectile** (`wa_blaunch`):
  - launched from its chest along its facing;
  - its climb set to reach the player's chest after the ticks the distance
    takes (bounded, so the divide cannot fault); a grenade's against
    gravity too;
  - its OWNER in the projectile's record (`RG_OWN`; `WA_RKGSZ` 16). It
    meets the PLAYER (`wa_rkhitp`: player-fired ones never did), spares its
    owner in a figure hit and in its splash, and its kills are its owner's
    (`wa_killer`, which `wa_kill` credits).
  - A bot's splash hurts the player whole; the player's own, half.

**A bug found on the way**: the respawn points' and the pickups' nodes'
pointers were rounded to a word on the ABSOLUTE address. But the copy of
the block (`wa_gph`) can sit at an odd one, and B4's new variables put it
there. Both pointers read a byte early. A bot took a phantom "pickup" past
the table's end (kind 0: a shotgun) - found by `wabotarms`. The offset is
rounded within the block now. In B3's build the block was even, which is
why `wascore`'s respawns were right there.

**Checked**: `tests/wabotarms.py`, a soak row (~22 s).

- A bot reaching the platform's spawn takes the grenade launcher written
  there (8 rounds), and the spawn is empty.
- A bot at 30 health FETCHES node 7 and takes the pack.
- A bot's rocket is owned by it and hurts the player. The bot's own splash
  spares it (the player 80 units off). The kill is the bot's.
- The chaingun fired 44 rounds in 150 ticks. A railgun hit is 100, a close
  shotgun hit 60.
- **Red** with nothing taken at nodes (the spawn and the fetch), and with a
  bot's rocket passing through the player.

All six WireArena rows pass. **In play**, by the breakpoint trace,
`wa_bthink` is now 698 clk a call, 1.3% of the frame. The benches are as
before (P 20.0 / 16.6; heights 40 13.1 / 14.8 / 32.7 / 12.5 / 25.9).

**Bytes**: the game 14,096 (+2,705 over the four waves: routes, sight,
states, the score and board, the arms), the renderer part 61,780,
`WARENA.O88` 34,114.

### 8.5 Play notes: a longer respawn, any seen target (2026-11-14)

The owner's first play of B4: *"Bots turn to me, fire, and generally follow
me."* Two changes asked for:

**A dead bot is gone 8 seconds, not 3.** Three let a bot back before the
player could cross the arena after the kill. `F_DEAD` now starts at
`WA_BDEADT` (240 ticks). The body lies `WA_BBODYT` (90 ticks, 3 s) and is
then taken off (`F_NFR` = 0, nothing drawn) for the rest of the wait. The
shotgun's red collapse still lasts as long as the body lies: its `F_FLASH`
stays 90, so it ends with the body.

**A bot picks whoever it sees: the player or another bot.** `F_BTGT` (byte
55, the record's last spare) names the target: 0 the player, 1..3 a bot.
It costs no extra sight walk. A bot still walks one line of sight a look
(every 8th tick); the look's TARGET alternates. An engaged bot keeps its
target. Any other bot looks at the player one look and at the next live
bot the next (`wa_bnextt`). `wa_btpos` puts the target's place in
`wa_tx`/`wa_ty` and its chest's height in `wa_tz`, or says there is none:
the player dead, the bot dead, or itself. `wa_sight`, `wa_bear`,
`wa_bshoot` and `wa_blaunch` read those instead of the player's place.

- A target that dies: the engaged bot roams at its next look. The player
  dead is now only that case, so the old early exit is gone. Bots keep
  fighting each other while the player waits to respawn.
- `wa_btpos` runs again just before a shot. The target may have died since
  the look, and `wa_tx` is shared, holding whichever bot looked last.
- A hitscan hit on a bot is `wa_hurt` with the weapon's damage (the
  shotgun a flat 35, its kill the red collapse) and the shooter as
  `wa_killer`. The score's kill and death are the bots'. A bot target
  always counts as moving: a quarter off the roll.
- **Shot, it shoots back**: a bot hit by another bot and not already
  fighting turns on the shooter (ENGAGE, its target the shooter, a
  reaction first). It fires once its next look sees them. The player's
  hits are as before.
- Projectiles needed nothing: `wa_rkhit` and `wa_boom` already hit any
  figure but the owner. A bot's rocket now aims at the target's chest.

**Checked**: the six WireArena rows pass. Their changes:

- `wabots` gains BOT AGAINST BOT: the player in the back tunnel, two bots
  face to face. They pick each other (engaged, targets 2 and 1). One kills
  the other, and the kill and the death are both bots'. The player is
  unhurt. **Red** with the bot looks fixed on the player: neither picks
  the other and nobody dies.
- `wascore`'s bot respawn now measures the times: the body lies 87 ticks
  and the bot is back after 236. (The kill lands a few ticks before the
  clock starts.)
- Rows that stage a bot engaged on the player write `F_BTGT` = 0.
- `wabots`' SIGHT keeps the other two bots dead. A HUNT-ing bot's second
  look may be at one of them, and that look is not the check's.
- `waroutes`' lift ride parks bots 1 and 2. Otherwise bot 0 stops on the
  lift to finish a fight from the ROAM run.
- `waweapons`' shotgun check reads the red as the body's 90 ticks, not
  `F_DEAD`'s.

**In play**, by the same breakpoint trace, `wa_bthink` is 648 clk a call,
1.23% of the frame (B4: 698, 1.3%). The renderer is untouched.

**Bytes**: the game 14,352 (+256), `WARENA.O88` 34,306 (+192).

### 8.6 Play notes: shields by chance, switching, keeping on the player (2026-11-14)

The owner's second play: the bots targeted each other but did not seem to
kill each other. They did. Measured in two-minute matches, bot-on-bot kills
were 3 with the player in view and 15 with the player hidden. But nearly
nine hits in ten went to the player: a bot engaged on the player stayed on
them. Bot 0 also always lived with a full shield, a weapons-round fixture,
so it took twice the killing. The owner's rulings and what was built:

**Shields by chance.** "On medium/hard, randomize spawning with a shield
for each bot at a 20% chance." `wa_spawne` rolls `wa_rand8` < `WA_BSHP`
(51 of 256) on normal and hard, for any bot; easy never. A bench
(`wa_bdet` set) keeps the fixture: dummy 0 shielded, so the bench scenes
and `waweapons` are unchanged.

**A shot bot turns on the shooter** ("seems fine"). `wa_hurt` now calls
`wa_bstruck` for every damage path: hitscan, rocket, grenade, the player's
too. A bot not fighting turns on whoever shot it. A bot fighting someone
else turns only when the shooter is nearer than its own target.

**A nearer enemy** ("also seems fine"). Every fourth look, an engaged bot
first checks for a live enemy within three quarters of its target's
distance (`wa_bclose`: four `wa_btpos` and a sum of differences each, no
sight walk). If there is one, it walks one sight line to it: seen, it is
the target; not, the look goes on to its own target as before.

**So that the bots still follow the player** (the owner's worry):

- **A grudge**: a bot the player hits holds to the player for
  `WA_GRUDGE` (90 ticks, 3 s). No nearer enemy and no bot's shot moves it.
  It counts down 8 a look, so its cost is a look's, not a tick's.
- **The player's hits** turn a bot on them, the same as a bot's do. Before,
  a bot the player shot did nothing.
- **Hearing**: every shot the player fires (`wa_bhear`, from `wa_fire`)
  sends each live ROAMING bot within `WA_HEAR` (1024 units, |dx|+|dy|)
  hunting the player's node. Engaged and hunting bots are left alone. A bot
  that wanders off still comes back when the player starts shooting.
- The three-quarters margin itself: a bot does not drop the player for an
  enemy one step nearer.

**Checked**: the six WireArena rows pass. `wabots` gains four checks:

- SHIELDS: 16 of 80 lives on normal, 0 of 20 on easy.
- SHOOTS BACK: the player shoots bot 0 from behind; it turns on them
  (engaged, target 0) with a grudge of 86 ticks.
- THE GRUDGE: bot 1 comes up 150 off. Bot 0 stays on the player while the
  grudge runs, then takes bot 1.
- HEARING: a shot at nothing; bot 0, 500 off and facing away, comes
  hunting; bot 1 in the back tunnel, out of earshot, roams on.

**Red** with the shield roll never true, the grudge never held and the
hearing call removed: SHIELDS, THE GRUDGE and HEARING all fail. `wascore`'s
pistol kill now clears bot 1's shield (it may have spawned with one). Each
of `wabots`' duels pins `wa_seed`. With the clock's seed, 20 rolls landed
outside the 15-point band about once in 200 runs, and it did once here.

**In play**, by the breakpoint trace: the first build was 2.44% of the
frame. The grudge countdown ran a divide (`wa_bidx`) on every bot every
tick, and the nearer-enemy walk came every other look. With the countdown
on the looks and the walk every fourth look, it is 1,287 -> 876 clk a
call, 5,415 clk a frame, 1.66% (8.5: 1.23%). The rest is real work: more
engaged and hunting bots in the same 149 frames, and their looks are sight
walks.

**Bytes**: the game 14,700 (+348), `WARENA.O88` 34,562 (+256).

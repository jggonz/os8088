# WireArena: weapons and pickups

**STATUS: BUILT** (2026-11-14), round two too (section 6: six weapons, the
weapon spawns, R). Section 2 is round one's contract as built; section 3
records what was measured, what it cost and what was done about it; section 4
what is open.

WireArena is `apps/wirearena/`. Its history is
[MULTIPLAYER-FPS-PLAN](MULTIPLAYER-FPS-PLAN.md) §16 and
[WIREARENA-HEIGHTS-PLAN](WIREARENA-HEIGHTS-PLAN.md) §9. Its two segments are
described in [WIREARENA-PARTS-PLAN](WIREARENA-PARTS-PLAN.md) §7–8.

This work was done on branch `multiplayer-fps`, from `04ae112`.

## 1. The owner's request (2026-11-13)

> *"Next up for this session is pickups and weapons.*
>
> *Move the detail levels that just went in to F1-F5, 1-5 will become weapon
> selection as is standard in a FPS.*
>
> *Weapons for the first round:*
> - *Pistol (default, infinite ammo, reloads after 6 shots, quick reload, 3-4
>   shots to kill)*
> - *Shotgun (3 shots to reload, 6 ammo per pickup, medium reload, damage based
>   on range, cheaply, unless a cone check is also cheap. 1 direct close hit to
>   kill)*
> - *Rocket launcher (3 shots then reload, long reload, projectile is not
>   instant, 1 shot to kill if direct hit)*
>
> *If its cheap in performance, the weapon can be displayed in front of the
> player, so its easy to see which one is selected.*
>
> *The ammo pickups are the weapons themselves, so we minimize things we have
> to render. Each weapon's kill should have a different death animation
> (pistol the current fall over, shotgun turn red and collapse differently,
> rocket launcher fly to bits then disappear).*
>
> *Additional pickups: Health pack (restores health); Shield pack (gives a
> second health pool, turns the player's chest blocks yellow).*
>
> *The pickups need to be distinguishable between each other, but as cheap as
> possible to draw. A line for the weapons, a larger block for the packs,
> would be fine.*
>
> *For placement, the health can go in the back tunnel - not visible or
> needing to be rendered from the front of the arena. The shield pack on top
> of the platform in the small back room. The shotgun on the ramp platform,
> and the rocket launcher on the lift platform.*
>
> *Keeping performance is the primary concern here."*

## 2. The contract

### 2.1 Keys

| key | does |
|---|---|
| **1** | the pistol |
| **2** | the shotgun, once picked up and while it has shells |
| **3** | the rocket launcher, once picked up and while it has rockets |
| 4, 5 | nothing yet (the next weapons) |
| **F1–F5** | the detail level, Very Low to Very High (was 1–5; HEIGHTS-PLAN 9.11) |

Every other key is as it was. R stays RAMP2's, so a reload is automatic: a
weapon reloads when its magazine empties, and on being chosen with an empty
magazine.

*Superseded by round two (section 6): keys 1–6 are six weapons, R reloads
at any time, and RAMP2 moved to Y.*

### 2.2 The weapons

Times are 30 Hz sim ticks. Damage is against 100 health.

| | pistol | shotgun | rocket launcher |
|---|---|---|---|
| magazine | 6 | 3 | 3 |
| reserve | infinite | +6 a pickup, at most 24 | +6 a pickup, at most 24 |
| reload | 15 ticks (0.5 s) | 36 (1.2 s) | 75 (2.5 s) |
| between shots | 4 ticks | 15 | 18 |
| hit | hitscan through the crosshair | hitscan, a CONE | a projectile, 32 units a tick (15 m/s) |
| damage | **34**: 3 shots kill | direct: inverse depth / 32 (128 at 2 m, 64 at 4 m); in the cone only: half | direct: a kill; splash: 128 less the distance, within 2 m |
| a kill | the fall over (today's DEATH) | red, and a COLLAPSE forward | the figure flies to BITS, then is gone |

A trigger is still one shot a press. The first pickup of a weapon gives it
loaded (3 in the magazine, 3 in reserve) and switches to it. A weapon with
nothing left switches back to the pistol.

**The shotgun's cone is the pistol's ray widened.** TRACE (the renderer's
verb) tests each figure's projected extent against the crosshair; the cone
widens that test by `W/8` sub-columns either side (about ±5 degrees at any
width and field of view) and 3 half-rows up and down. A figure is in the cone
when its extent overlaps the widened crosshair; DIRECT when it covers the
crosshair itself. The occlusion test is asked at the point of the figure
nearest the crosshair. The cost is a few compares a figure, once a shot.

**The damage by range is the inverse depth the renderer already keeps**
(`F_PIZ`, 0x7FFF0 / z): a shift, no divide, no distance. Every figure in the
cone is hit.

**The rocket** is the game's (G's) to fly: position 16.8 in x, y and z, a
velocity from the view's yaw and pitch (the pitch is a screen shear, so the
vertical speed is `pitch × 2^19 / kyh`, 8.8 a tick: the crosshair's own ray).
Each tick it moves, then:

- outside the room it is gone (over the walls), or explodes (into them);
- at or under the floor there (`wa_floor` over every box, widened by the
  player's radius: a wall stops it 20 units out) it explodes;
- inside a live figure's square (±24) between its feet and 120 above, it
  explodes and the figure is killed outright (a direct hit);
- after 120 ticks it is gone.

The explosion's splash reaches figures and the player within 128 units
(the largest of the three axis distances). The player takes half. At most 3
rockets fly at once.

### 2.3 Health, the shield, death

- The player has 100 health and a shield of 0–100. Damage takes the shield
  first. Only the player's own rockets damage the player, until there are
  other players. At 0 the player starts again at the arena's start with the
  pistol alone.
- A figure has 100 health (`F_HP`, now byte 42, the game's alone) and a
  shield (`F_SH`, byte 10, where `F_HP` was, in the bytes copied to the
  renderer every frame). **A figure with a shield is drawn with a YELLOW
  chest**: after the figure's one sweep, the renderer walks down its
  neck-to-pelvis limb a cell row at a time and gives each cell the figure
  drew this frame (its stamp says so) the attribute `A_SHLD` (yellow; on
  Hercules and MDA, normal intensity against the body's bright). No second
  sweep: a few compares a row. Dummy 0 starts every life with a full shield, so the chest and
  the second pool have something to show until there are other players.
- `F_SIZE` grows from 42 to 44. **WR_ABI is 5.**

**The deaths**, a keyframe animation each, made by `tools/arenagen.py`:

| weapon | animation | colour | after it |
|---|---|---|---|
| pistol | DEATH (8 keyframes, today's) | as hit | lies there 3 s |
| shotgun | COLLAPSE (6): the knees go, the body folds forward onto them | red the whole time: `F_FLASH` is set to `F_DEAD`, which the renderer already draws in `A_FLASH` | lies there 3 s |
| rocket | GIB (6): each joint flies apart and falls | as hit | **gone**: the sim sets `F_NFR` 0 when the animation ends, and the renderer skips a figure with no keyframes (one compare a figure a frame) |

A GIB figure is drawn as its joints alone, each a 2×2 sub-pixel block when
near (the head's), with no limbs: the bits. It costs less than a body.

### 2.4 Pickups

**Drawn as the cheapest thing that reads**, through the figures' own
sub-pixel layer (`wa_plotf`), so a figure in front of one draws over it:

| pickup | shape (units: wide, from–to above its floor) | colour | Hercules/MDA |
|---|---|---|---|
| shotgun | a LINE, 48 wide, 4–10 up | brown | bright, thin |
| rocket launcher | a LINE, 64 wide, 4–20 up | light green | bright, longer and thicker |
| health pack | a BLOCK, 32 wide, 0–28 up | light red | bright, square |
| shield pack | a BLOCK, 24 wide, 0–44 up | yellow | bright, tall |

Every shape faces the eye (a billboard) and keeps at least one sub-pixel.

**The cost.** A pickup costs nothing unless it is present and its cell can see
it. Then one transform, a cone test, two divides (the scales at its depth),
and per column it covers one occlusion query (`wa_occl`, which is what the
figures and the far marker ask) and a `wa_plotf` a cell. Forecast: ~2k clk
for one at range, rising with the cells it covers when near.

**The PVS culls them.** Each arena that carries a PVS (HEIGHTS-PLAN 9.8)
carries a byte a cell with a bit a pickup: whether any point of the cell
sees any part of the pickup's square (±16 units) past the tall boxes, by
the same test and margins as the boxes' masks (`pvs_seen`). `wa_order`, which
already finds the cell, keeps a pointer to its byte. An arena with no PVS
tests every pickup it has. So from the open area the back tunnel's health
pack costs one AND a frame.

**Placement** (arena 2, the Heights arena; `tools/arenagen.py`):

| pickup | at | on | respawns |
|---|---|---|---|
| health pack | (64, 1024) | the back tunnel: the corridor along the west wall, behind the annex, opposite the back room's mouth | 20 s |
| shield pack | (480, 1024) | the jump platform in the back room, 48 up | 30 s |
| shotgun | (1248, 1024) | the ramp platform's middle, 80 up | 10 s |
| rocket launcher | (1952, 1024) | the tower's top, 160 up: the platform the lift goes to | 10 s |

The flat arenas (0 and 1) carry no pickups, so their frames are unchanged:
the pistol alone there.

**Taken** when the player's square is within 40 of it (the player's radius
and the pickup's) and the feet within 8
below to 48 above its floor, and only when it helps: a weapon below its cap,
health below 100, the shield below 100. Health restores to 100; the shield
fills to 100. Not during a bench.

### 2.5 What the player sees

**The OVERLAY**: a list of cells the game builds when something it shows
changes (a weapon, a shot, a reload's start and end, a pickup, a hit, the
width), copied into the renderer only then, and drawn every frame after the
figures and before the crosshair, through the stamp and list like the
crosshair's cell (so `wa_present` writes none of them unless it changed).
At most 32 cells.

- **The weapon in front of the player**, right of centre at the bottom of the
  view: the pistol 7 cells, the shotgun 12, the rocket launcher 16, each in
  its pickup's colour. While it reloads it sits a row lower.
- **Row 23, left**: `♥` and the health; `♦` and the shield when there is one
  (yellow).
- **Row 23, right**: the magazine, and the reserve or `∞`.

Row 24 stays the meter, untouched: it is the owner's instrument.

**Rockets and explosions** are drawn after the figures, through the pickups'
routine: a rocket a 2×2 sub-pixel bright dot, an explosion a block that grows
over 7 ticks (half width 12 + 6 a tick), yellow and red by turns.

### 2.6 The frame block (`wafb.inc`)

| section | adds | copied |
|---|---|---|
| FRAME | `wa_pkon` (a bit a pickup present), a pad | G → R every frame: one word more |
| ARENA | `wa_npk`, `wa_pks` (8 pickups: x, y, z, kind, pad) | R → G at load |
| OVERLAY (new) | `wa_ovn`, `wa_ov` (32 × cell offset, glyph and attribute) | G → R when the game rebuilt it |
| ROCKETS (new) | `wa_rks` (3 rockets: x, y, z, state) | G → R while any flies or explodes, and once after |

The arena part's descriptor gains two words: 44 its pickups (0: none), 46
their PVS bytes (0: none).

### 2.7 How it is checked

- The P bench (arena 0) and the heights poses A–E, before and after, on
  MartyPC `os8088_5150_cga`. The flat arenas draw no pickups, so P pays the
  overlay, the rocket loop's test, a word more of FRAME and the shielded
  dummy's chest alone.
- A scripted run on MartyPC: each pickup taken and respawned; each weapon's
  ammo, reload and switch; a kill by each weapon with its animation; a
  rocket's flight and splash; the shield's yellow chest; screenshots.
- The fast tier, as every `make`.

## 3. As built, and measured (2026-11-14)

### 3.1 Checked

**`tests/waweapons.py`**, a soak row (`waweapons`, ~13 s), on MartyPC
`os8088_5150_cga`: one boot into the Heights arena, every item of 2.1–2.4
asserted on the game's own variables (section 2.7's list). It is red with
the pickup reach broken (10 checks fail) and with the shotgun's death mapped
to the rocket's (1 fails). `walift` still passes.

Looked at, on CGA at 40 columns and Hercules at 80: each pickup in its
colour and shape (on Hercules: bright, told apart by proportion), the
shielded dummy's chest (yellow; normal intensity against bright on
Hercules), the red collapse, the gib's bits under the explosion, the weapon
in front, and the HUD's `♥` / `♦` / ammunition.

### 3.2 Bytes

| | `04ae112` | now |
|---|---|---|
| the renderer part (`wrpart.bin`, image and BSS) | 54,781 | 60,972 (+6,191: the COLLAPSE and GIB keyframes 4,224, the pickups' PVS 256, the code and its BSS); 4,052 left of the part's 65,024 |
| the game (`wirearena.bin`) | 8,391 | 10,426 (+2,035: weapons, rockets, pickups, overlay) |
| arena 2's part | 1,366 | 1,660 (+294: four pickups and their PVS bytes) |
| `WARENA.O88` (packed) | 22,888 | 29,424 |

### 3.3 Speed (MartyPC `os8088_5150_cga`, the games' own benches, Medium)

fps (ms a frame):

| | `04ae112` | first build | now |
|---|---|---|---|
| P bench, 40 | 20.5 (48.6) | 19.5 (51.2) | **20.1 (49.6)** |
| P bench, 80 | 17.0 (58.5) | 16.3 (61.3) | **16.8 (59.5)** |
| heights 40, A | 14.0 (71.0) | 12.9 (77.5) | **13.2 (75.6)** |
| heights 40, B | 15.8 (62.9) | 14.5 (68.7) | **14.9 (66.9)** |
| heights 40, C | 34.6 (28.9) | 31.5 (31.7) | **33.1 (30.2)** |
| heights 40, D | 13.1 (76.1) | 12.2 (81.9) | **12.6 (79.1)** |
| heights 40, E | 27.2 (36.7) | 24.6 (40.6) | **26.1 (38.3)** |
| heights 80, A–E | 11.0 / 11.2 / 26.8 / 10.0 / 22.1 | 10.0 / 10.2 / 24.8 / 9.3 / 20.4 | **10.3 / 10.5 / 25.8 / 9.6 / 21.4** |

**Where the cost is**, by breakpoint trace (exact guest cycles a frame, the
same bench frames, `04ae112` against now):

| | P bench, 40 | heights E | heights A |
|---|---|---|---|
| the overlay (14 cells with the pistol) | ~1.7k | ~1.7k | ~1.7k |
| pickups: the PVS passes them, the view cone rejects them (~1.3k each) | 0 (none in arena 0) | ~2.7k (two behind the eye) | |
| pickups drawn (~3.1k each, then ~1.8k a column for the occlusion query and ~0.8k a cell) | 0 | 0 | ~13k (the shotgun and the launcher) |
| the tick's new work (weapon, rockets, pickups) | ~0.7k | ~0.7k | ~1.6k |
| the rest (the rocket test, FRAME's word, the chest, the frame glue) | ~1k | ~1k | ~1k |
| **the frame, in all** | **+4.3k (+1.9%)** | **+6.1k (+3.8%)** | |

**What was done about the first build's cost:**

- **The overlay reaches the glass once.** At first each cell was stamped,
  listed and compared by present every frame: 1.7 ms of the P bench.
  Now a fresh overlay lists its cells once, and puts back the old cells it
  no longer covers. On any other frame a cell is only stamped, so present's
  old list never blanks it. It is given its glyph again only where the view
  drew under it, and then present finds the glass already right. Present
  is back to `04ae112`'s exactly; what is left is the stamp loop.
- **The shield's chest is a recolour, not a second sweep** of the figure's
  box: a few compares a cell row down the neck-pelvis limb.
- **Rockets cost nothing when there are none.** A live count skips the
  tick's loop, and the ROCKETS section is copied only while one lives.
- **The pickups' transform takes z first.** A pickup behind the eye costs
  two multiplies, not four. The scales at its depth are multiplies by the
  inverse depth the renderer already tabulates, not divides. The
  occlusion query takes the figures' bound first.
- **What is left is the occlusion query**, at ~1.8k a column. The divide
  was already avoided. The time is the depth-stack walk at columns where a
  platform's lip is in front, which is what hides a pickup on a platform
  correctly from below. It is kept.

### 3.4 Interpretations, for the owner to correct

- **"The lift platform"** was read as the tower's top, the platform the
  lift goes up to, by analogy with "the ramp platform" (the platform the
  ramps go up to). It is one line of `HEIGHTS_PICKUPS` to put it on the lift
  itself, but a pickup there rides the lift, and that costs code in both
  halves.
- **"The back tunnel"** was read as the corridor along the west wall,
  behind the annex: the pickup sits opposite the back room's mouth.
- **"Turns the player's chest blocks yellow"**: there is no other player
  yet, so dummy 0 lives with a shield to show it. The renderer draws any
  figure with a shield this way; a networked player's will be one.
- **The flat arenas carry no pickups**: the owner's placement is the Heights
  arena's. Weapons other than the pistol are there only.
- **A reload is automatic**: R is RAMP2's (HEIGHTS-PLAN 9.11).

### 3.5 Two red lines into the sky (fixed, 2026-11-14)

**The report** (the owner): "The shotgun kill sometimes has a bug, at a
medium distance its kill animation will draw two red lines up into the sky
briefly."

**The cause.** Beyond ~1.6 m a figure's joints take the renderer's NARROW
path (`WA_FJN`, MULTIPLAYER-FPS-PLAN 16), whose height multiply is
`mul ch`: the keyframe's height is an UNSIGNED byte. That path's contract is
joint heights 0..111 units (`wa_fvn`'s bound), but nothing checked it.
COLLAPSE's crouch put two joints 4 units under the floor. Read as 252, they
projected far above the screen, and their limbs drew up to them, red, for
the frames they were under it. Up close the 16-bit path multiplies signed,
so it never showed there. GIB broke the bound at the other end (116 up).

**The fix, in the data, at no run-time cost** (`tools/arenagen.py`):

- COLLAPSE is lifted onto the floor (`on_floor`), as a fall always was;
- GIB's bits fly no higher than the bound;
- arenagen REFUSES any keyframe with a joint outside 0..111 (`JMAXY`).

Every other animation's keyframes are byte-identical.

**Checked**: `waweapons` gains a medium-range shotgun kill (at 260 units)
and counts red cells in the view's top third over the whole collapse:
**128 on `88910746`, 0 now**.

## 4. Open

1. **Pickups visible from a platform-heavy view cost ~1.5 ms each** (A and
   B, ~1 fps). Cheaper options, each a look change: one occlusion verdict a
   pickup (the far marker's), which lets a pickup half behind a wall's edge
   draw over the edge; or, at the lower detail levels, no pickups beyond a
   distance.
2. **The overlay's ~1.7k a frame** is the stamp loop over its cells, and
   most of that is the weapon in front (the pistol's 7 cells; the shotgun's
   12 and the launcher's 16 cost more). If that is too dear, a smaller
   sprite or none, at a level, are one table each.
3. **The PVS's margin keeps the back tunnel's health pack in the cells
   against the annex's front** (x 640–768): there it is transformed and
   occluded, not culled.
4. **Dummies do not shoot back**, so only the player's own rockets hurt the
   player: health and the shield pack matter once there are opponents.

## 5. Proposed: the next weapons (for the owner, 2026-11-14)

*The owner took all three; section 6 is what was built. The table below is
the proposal as it was put.*

Each is priced against what the first round measured (3.3).

| weapon | what it is | what it reuses | its cost a frame |
|---|---|---|---|
| **Railgun** | one shot then a long reload; hitscan that PIERCES every figure on the crosshair's ray; a beam drawn for ~3 frames from the weapon to the far wall | TRACE's mask already lists every figure hit (cone 0, 0); the beam is a screen-space line of cells, through the overlay's stamp-and-list | **0** when not firing; ~20 cells for 3 frames after a shot |
| **Grenade launcher** | a lob that ARCS (gravity on the rocket's climb), bounces off floors and walls, and goes off after a fuse or on a figure | the rocket's flight, collision, splash, billboard and live count; a bounce is the climb negated and halved, a wall's an axis reflected (the player's two-axis collide) | as a rocket: ~1.5k a visible grenade, nothing with none |
| **Chaingun** | held trigger, ~8 shots a second, low damage (~12), a slight alternating spread; spins down | TRACE with a small cone, alternated; the trigger's held state | **none for drawing**; one TRACE a shot (a few hundred clk) while held |

How each fits the levels:

- **The railgun** wants long sightlines: the tower, the open area, a long
  hall. It is the theme's own: a line of light across a wireframe.
- **The grenade launcher** wants heights and corners: lobbing onto the
  platform, over a lip, round the annex's hallways.
- **The chaingun** wants close quarters: corridors, small rooms.

A fourth, if a level wants a trap: **proximity mines**. A mine is dropped,
drawn as a pickup's billboard (a small block), and goes off when a figure
comes within reach. It costs what a pickup costs while in view.

## 6. Round two: six weapons, weapon spawns, R (2026-11-14)

The owner: *"All three sound good. Lets put the chaingun on 3, grenade
launcher on 4, rockets on 5, and railgun on 6. Can we select a random weapon
to spawn on the two weapon spawn locations? The ramp platform could spawn
shotgun, chaingun or grenade launcher, and the tower one rocket launcher or
railgun. Also lets have 'R' reload at any point, giving the player the
ability to reload during downtimes instead of being forced to empty the
clip."* And on RAMP2: *"Not drawing the bottom line of the ramp worked fine,
but having the ramp become see-though and show the bottom of the platform
did not."*

### 6.1 Keys

| key | does |
|---|---|
| **1–6** | pistol, shotgun, chaingun, grenade launcher, rocket launcher, railgun (each once picked up, while it has anything to fire) |
| **R** | reload now: a magazine not full tops up from the reserve (the pistol's is endless); nothing while one is under way |
| ~~Y~~ | RAMP2 (was R): a ramp without its foot's line - see 6.5; since 6.7 on at Low and Very Low, and no key |
| **F1–F5** | the detail level, as before |

### 6.2 The weapons

| | pistol | shotgun | chaingun | grenade launcher | rocket launcher | railgun |
|---|---|---|---|---|---|---|
| key | 1 | 2 | 3 | 4 | 5 | 6 |
| magazine | 6 | 3 | 30 | 4 | 3 | 1 |
| a pickup gives / reserve cap | - | 6 / 24 | 60 / 120 | 8 / 24 | 6 / 24 | 5 / 15 |
| reload (ticks) | 15 | 36 | 40 | 60 | 75 | 60 |
| between shots | 4 | 15 | 4 (held: ~7.5 a second) | 15 | 18 | 24 |
| hit | the ray | a cone | the ray, a little wider (W/32, a half-row) | a lob: 16 a tick, +6 up, gravity, bounces | 32 a tick, straight | the ray, through EVERY figure on it |
| damage | 34 | inverse depth / 32 (/ 64 the cone alone) | 12 | splash; a figure met: a kill | splash; a direct hit: a kill | 150 (through 50 of a shield) |
| its kill | the fall | the red collapse | the fall | the bits | the bits | the fall |

The first pickup of a weapon gives what a pickup gives, the magazine first
(the shotgun's 3 and 3 as before; the chaingun's 30 and 30; the grenade
launcher's 4 and 4; the railgun's 1 and 4).

**The chaingun** fires while the trigger is held: the tick fires a round
whenever its gap has passed (`wa_wtick`), each a TRACE as the pistol's. A
press still fires at once.

**The grenade** is the rocket's projectile (`wa_rks`, its type byte 1) with:

- gravity on its climb, the player's (`WA_GRAV`);
- each horizontal axis moved alone; a wall there sends it back at half its
  speed. A wall is the room's edge, or a floor (`wa_floor` over every box)
  more than `WA_GSTEP` (8) over it, so it rolls up a ramp and bounces off a
  platform's side;
- the floor stopping it, and bouncing it at half its fall, under a unit a
  tick at rest; each bounce rubs a quarter off its roll;
- a 2 s fuse (60 ticks), or a live figure met: a kill outright (the
  rocket's direct hit), then the splash.

It costs a rocket's drawing, and up to three `wa_floor` reads a tick while
it lives. Four projectiles may be live at once (was 3).

**The railgun's beam** is six overlay cells (`\`, bright cyan) from the
barrel to the crosshair, the ray seen from the gun, for 6 ticks (0.2 s):
the overlay is rebuilt when it starts and when it ends. Nothing is drawn
when the railgun is not firing.

**The new sprites**: the chaingun 13 cells, grey; the grenade launcher 9,
magenta; the railgun 12, cyan. The magazine and reserve take 2 and 3 digits
(the chaingun carries 120).

**The new pickups' lines** (2.4's rule: lines for weapons; on mono the
proportions tell them apart):

| | half width | from–to | colour |
|---|---|---|---|
| chaingun | 28 | 4–14 | grey |
| grenade launcher | 20 | 4–16 | magenta |
| railgun | 40 | 4–8 | cyan |

### 6.3 Weapon spawns

A pickup's record carries a CHOICES byte (it was a pad): a bit a kind it
may spawn as, 0 for a fixed pickup. arenagen writes the Heights arena's:

- the ramp platform: the shotgun, the chaingun or the grenade launcher;
- the tower: the rocket launcher or the railgun.

Each time a spawn spawns - the match's start, and every respawn - the game
picks one at random (`wa_pkroll`: an LCG seeded from the clock at the
match's start) and writes it into its kind byte, and into the renderer's
copy. That is the one ARENA byte G writes (wafb.inc says so). The benches
take each spawn's first choice, so a run draws the same scene every time.
arenagen refuses a spawn that chooses among anything but weapons.

### 6.4 R: reload at any time

A reload now TOPS UP: at its end the magazine takes what it has room for,
out of the reserve, where before it was empty and took all. R starts one
whenever the magazine is not full and there is a reserve (the pistol always
has one). An empty magazine still reloads by itself.

### 6.5 RAMP2, as the owner judged it

RAMP2 dropped a ramp's low-end floor line AND its high end's top. The high
end's top is the edge that closes the slope's columns (HEIGHTS-PLAN 7.4.2).
Without it, the platform's face behind showed through the ramp. RAMP2 now
drops the foot's line alone (`wl_lo`'s bit, `wrlow.inc`), which is the part
the owner liked. Its key is Y; it is still in no detail level.

**What it buys** (MartyPC `os8088_5150_cga`, Medium, the heights poses, the
same build `cb0b62b8` with Y off and on; fps, ms a frame):

| pose | 40 off | 40 on | 80 off | 80 on |
|---|---|---|---|---|
| A, the busiest floor view | 13.2 (75.5) | **13.5 (73.8)** | 10.3 (96.9) | **10.5 (95.1)** |
| B, the open floor | 14.9 (66.9) | 15.0 (66.4) | 10.5 (95.0) | 10.6 (94.3) |
| C, on the platform | 33.1 (30.2) | 33.1 (30.2) | 25.8 (38.7) | 25.8 (38.7) |
| D, on the tower | 12.6 (79.1) | **12.9 (77.3)** | 9.6 (103.6) | **9.8 (101.9)** |
| E, the annex hallway | 26.1 (38.3) | 26.1 (38.3) | 21.4 (46.7) | 21.4 (46.7) |

About 1.7 ms (~2.3%) where both ramps' feet are in view (A, and D looking
down on them), 0.5 ms at B, and nothing where no foot is drawn: C stands
on the platform the ramps lead to, and E cannot see them. A foot is one
outline pass of a low box: its setup and its columns (HEIGHTS-PLAN 9.8.6's
~2.4k clk and ~770 a column).

### 6.6 Checked

`waweapons` grew to cover the round (~23 s). It is green twice running.

- **The dice**: 30 forced respawns of each spawn. The platform gave all
  of {shotgun, chaingun, grenade launcher} and nothing else, the tower both
  of {rockets, railgun}, and the renderer's kind always matched the game's.
- **R**: the pistol at 3 back to 6; the shotgun at 1 and 3 to 3 and 1.
- **The chaingun**: held, 10 rounds, a kill by the fall.
- **A grenade** at the tower's face: back off the wall, up off the floor,
  off on its fuse after 58 ticks. One into a dummy: a kill, the bits.
- **The railgun**: two dummies in line, one shot, both dead; its beam 6
  cells for its ticks, then gone.
- **Y** toggles RAMP2.

It is red with the dice fixed to the first choice (the dice check) and with
R doing nothing (both R checks).

The row's shots now stand each target dummy still, at a fixed open spot.
Shooting "ahead of a walking dummy" missed whenever it turned a corner.

**Bytes**: the renderer part 61,142 (+170 on round one: the shapes, the
grenade's, the overlay's room; 3,882 left of 65,024); the game 11,321
(+895); `WARENA.O88` 29,954.

**Speed**: the benches as 3.3's "now", within 0.1 fps everywhere (P 20.1 /
16.8; heights 40 13.2 / 14.9 / 33.1 / 12.6 / 26.1). Nothing new is drawn
unless it is in hand or in flight, and the benches' spawns are 3.3's.

**RAMP2 on the glass** (CGA, 40 columns): from in front of the south ramp
and from its side, Y off against Y on. The pictures differ only in the
foot's cells; the platform's face stays hidden behind the slope.

### 6.7 RAMP2 joins the detail levels (2026-11-14)

The owner: *"It can go on Low and Very Low, then. And we can take out its
own custom key after that."*

- `wa_levtab` carries `LK_RAMP2` on levels 1 and 2. A level now sets
  `wa_look` whole (`wa_setlevel` no longer keeps a key's bit).
- The Y key is gone. RAMP2 has no key of its own.
- At Low and Very Low the heights views gain what 6.5 measured: ~1.7 ms at
  A and D, 0.5 at B. Medium and above draw as before.
- `waweapons` checks F1–F5 set RAMP2 on Very Low and Low alone, and that Y
  does nothing.

### 6.8 The owner's balance, the railgun's kill, the held weapon (2026-11-14)

The owner: *"the pistol is one of the most powerful weapons because of its
low reload time and rate of fire. Its a better chain gun than the chain
gun."* And a death of the railgun's own - *"struck by lightning then
dissolve from head to toe quickly"*, in the shot's blue - a pistol that
"bucks up" on a shot, cheaply, and a reload pose easier to see.

**Balance** (`wa_wmag` and its neighbours): the pistol's gap 4 -> 15 ticks
(half a second, `WA_PCOOL`), its reload 15 -> 30; the chaingun's magazine
30 -> 50, a pickup's 60 -> 100, its reserve's cap 120 -> 200. The bots'
own gaps (`wa_bgap`, `wa_bwgap`) were already slower and are unchanged.

**The railgun's kill is kill kind 3** (`wa_wdie`, which nothing read until
now - the bots' hitscan kills take it from there too, so a bot's railgun
zaps): two animations either side of GIB in `tools/arenagen.py`'s ANIMS,
because the renderer draws every animation from GIB on as its joints
alone.

- **ZAP** (before GIB: limbs): stiff, arched, the arms flung up, every
  joint shaking - two keyframes looping for `WA_ZAPT` (15 ticks, half a
  second), drawn in the beam's bright cyan flickering to white frame by
  frame: the bolt.
- **DISSOLVE** (after GIB: joints alone): three keyframes, each a cut
  lower - every joint above it gone into the foot below it - then
  nothing (0.4 s). `wa_zapgo` switches one to the other, on our figures
  and on a remote one (`wa_lkfig`).
- **Its colour** is a ninth attribute, `A_RAIL` (0x0B, bright on mono),
  chosen by the figure's animation in `wa_figures` - a few compares a
  figure a frame.
- **Over the link** the kind rides the two bits it always had (SNAP's
  "how the sender died", HSNAP's bots): a remote player killed by our
  railgun dissolves on our screen, and so do the host's bots.

**The pistol's kick**: for the first 5 ticks of its gap (`WA_BUCKT`, a
third of it - three ticks could fall between two frames at 4.77 MHz) the
held pistol is drawn a row up. The overlay is cached, so the cost is the
rebuild at the shot (which happened anyway) and one more when the kick
ends. **The reload pose**: two rows down and two columns right, tipped
away, where one row down read as nothing.

**Bytes**: `wirearena.bin` +132. **The renderer part +2,192**, the five
keyframes and their tables - and the part has a hard cap, 65,024 bytes
with its bss (`wrpart.asm`'s guard): it stands at **63,909, 1,115 under**.
ZAP and DISSOLVE were cut from 3 + 5 keyframes to 2 + 3 for that; the
first build left 35 bytes.

**Checked**: `waweapons` grew the railgun's kill (kill 3, ZAP, its cyan on
the screen, DISSOLVE once the bolt is over, gone after it), the kick (a
row up after the shot, back by the next) and the reload pose (two down,
two right, back after) - red with the kick taken out and with the
railgun's kill the fall. Its pistol and chaingun checks follow the new
numbers; `wascore`'s pistol waits its gap, and `walink`'s shots wait on
the shooter's own gap (`wa_cool`) rather than the host's clock. All
thirteen WireArena rows pass. `walinkudp` failed once in a three-lane run
before its match began - the harness could not get a key into the guest
- and passed alone.

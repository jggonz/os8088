# A multiplayer first-person shooter for the 5150, in text mode: investigation

**STATUS (2026-11-15): A GAME, ON ITS OWN DISK; RELEASE WORK IN PROGRESS.**
WireArena (`apps/wirearena/`, `WARENA.O88`, `make wirearenadisk`) is a
text-mode arena shooter for kern_big and kern_mega (kern_small refuses it):
mouse look, six weapons and pickups, bots that route, shoot and play for the
flag, free-for-all, team deathmatch, capture the flag and king of the hill
with a score to win and a rotation, six arenas (open, pillars, crossing,
heights, hedge, twin), and up to four participants over a serial cable,
raw Ethernet or UDP, all chosen in the match setup window. It is still on an
on-demand disk of its own and listed as an `instrument` in
`apps/RETIRED.txt`; section 17.2's release work puts it on shipped disks.

This document began as the investigation (sections 1-15, proposals as
written on 2026-11-12) and is now WireArena's umbrella: section 16 is the
record of Wave 0 and the renderer passes, and **section 17 holds the owner's
rulings, what is in progress, what is open and the ideas for after 1.0**.
The plans that built each part are in `docs/plans/completed/` (17.5 lists
them); WireArena has no SPEC.md section yet, so those plans and this one are
its contract.

The owner's answers to section 14 (2026-11-12): prototype to decide 40 vs 80
columns; spend the relative-mouse bytes (done, SPEC.md 9.14 - 38 resident
bytes on kern_big and kern_mega, refused on kern_small for its budget); raw
Ethernet pausing TCP is acceptable; kern_small decided once the size is
known (refused: PARTS-PLAN 6).

The owner asked for this on 2026-11-12, on branch `multiplayer-fps` (cut from
`elendilon` at `9f5c3c9c`):

> *Investigate a 3D, ~30FPS, multiplayer (ethernet, serial if its plausible)
> FPS for a 5150 ... block and half block characters, making effective use of
> black/negative space ... arenas; multiple layers and jumping if it is
> viable ... modern mouse aiming ... stick figure like ... a computer opponent
> ... a single pistol to start with, but investigate arcing weapons or
> projectile types that are not instant ... fluid animations ... Make other
> suggestions, and look into what performance room we have.*

This document is the answer. It separates three kinds of claim:

- **Measured** means a number was taken. Each one cites its source.
- **Predicted** means arithmetic from PERFORMANCE.md's unit costs. Nothing
  has run it.
- **Proposed** means a design choice. None of it is built. *(As written,
  2026-11-12. Since then most of it is built - section 17.5 lists the
  plans that built it.)*

The working title used below is **WireArena**. It follows the house naming
for link games (WireBall §109, WireFleet §114) and is the owner's to change.

---

## 1. The verdict, short

| ask | verdict |
|---|---|
| ~30 fps on a 5150 | **Plausible in text mode, and only there.** A whole 80x25 frame costs 45% of a 30 fps budget to present (measured). Presenting only the changed cells of a mostly black frame costs 13-26% (measured). The rest of the frame is predicted to fit, at about 30 fps for a 1v1 duel and 22-26 fps in a busy four-figure scene (section 4.7). No graphics mode can do it: the fastest 3D frame this tree has measured on a 5150 is 10.8 fps (Clear Skies' C160 tower, SPEC.md 88). |
| block / half-block characters, black negative space | **Yes, and the black is what makes the frame rate possible.** Present cost is per changed cell, so walls drawn as thin edges on black are cheap and filled walls are not. |
| line glyphs for wall edges, solid blocks for the opponent | **Yes, and recommended as the design's central rule** (section 4.4). It also carries over to the mono Hercules, where colour cannot do that job. |
| multiple layers and jumping | **Jumping: yes.** **Layers: "2.5D" yes** — raised platforms, tiers round a pit, steps, jump pads. **No true room-over-room**: a bridge you can walk under is out. |
| modern mouse aiming | **Yes, with one OS gap to close.** Yaw comes from mouse X. Pitch is a y-shear of the horizon, the Heretic/Duke trick, which costs nothing per pixel. **The OS has no relative mouse** (section 7). It needs about 10 resident bytes plus an API cell, or the package can borrow the UART the way WireBall does. |
| stick figures, walls never hiding or blurring them | **Yes.** Figures are drawn last, in solid bright blocks, depth-tested per column. Walls are thin and dim, with black between. |
| computer opponent | **Yes, cheaply** (section 9). It can reuse Pixelstein's line-of-sight walk and Tank's aim, and runs as a "virtual remote player". |
| pistol, plus arcing and slow projectiles | **Yes.** Projectiles are cheap to draw (one glyph each) and almost free to network (a spawn event; the flight is deterministic). Section 6. |
| fluid movement, hit and death animations | **Yes, within a cell grid.** Keyframes are pre-projected on the host and interpolated on the guest at about 4k clk per visible figure (predicted). The motion is as fluid as an 80x25 grid can show; section 5 is honest about that limit. |
| Ethernet | **Plausible, with a caveat.** No package can reach UDP *(since 2026-11-14 one can: `NETV_UDP`, SPEC.md 72.24)*. The route is `NETV_RAW` broadcast frames (section 8.2). **MartyPC has no NIC**, so development runs on QEMU and 86Box. On the owner's own hardware, 5150 #2 (the PicoMEM's NE2000) is the only XT that can play. |
| serial | **Plausible for two players at 2400 baud** (the practical XT ceiling, SPEC.md 114.3), using owner-authority state sync. **But 5150 #1's one COM port is the mouse's**, so a serial match there is keyboard-aimed. |

---

## 2. The machines it is for

From docs/FIELD-MACHINES.md:

- **5150 #1, the owner's:**
  - 8088 at 4.77 MHz, 640 KB.
  - **Hercules GB101 (it boots on this) and IBM CGA**, each on its own monitor.
  - **One serial port, COM1, with the mouse on it.**
  - An LPT port at 3BC.
  - **No sound card. No NIC.**
- **5150 #2:**
  - Stock 8088, VGA (PVGA1A).
  - A **PicoMEM 2.x: NE2000**, Sound Blaster and AdLib.
- **Victory 286:** PicoMEM 1.x Ethernet.
  - This is the natural partner for 5150 #2 in a real Ethernet match.
- **Compaq Portable III:** two serial ports, a CGA-class plasma panel, and a
  modem on COM1.

So the **primary adapter is the Hercules in text mode**, which has no snow
and no colour. The **IBM CGA is the hard case**: it has snow and the worst
VRAM contention. Both have to be first-class.

---

## 3. Performance room — what a frame can afford

**One frame at 30 fps is 159,091 clk** (4,772,727 Hz ÷ 30). At 25 fps it is
190,909 clk; at 20 fps, 238,636. Unit costs come from PERFORMANCE.md Part 2:

| | |
|---|---|
| `mul r16` | 132.5 clk |
| `div r16` | 162.9 clk |
| near `call`+`ret` | 52.1 clk |
| `mov ax,[disp16]` | 21.6 clk |
| the fetch floor | 4.34 clk a byte |

### 3.1 Presenting a frame — measured

[docs/reports/TEXTMODE-WRITES-MARTYPC-2026-11-12.md](../reports/TEXTMODE-WRITES-MARTYPC-2026-11-12.md)
has the full table:

| present | clk | share of a 30 fps frame |
|---|---|---|
| whole 80x25, char+attr, `rep movsw` (CGA / Hercules) | 72,092 / 74,641 | 45% / 47% |
| whole attribute plane, i.e. "80x50 half-block" | 96,076 | 60% |
| whole 40x25 | 36,080 | 23% |
| **600 changed cells, sparse** (67.9 clk a cell CGA, 71.3 Hercules) | ~40,700 | **26%** |
| 300 changed cells | ~20,400 | 13% |
| whole C160 (160x100x16) | ~384,000 | 241%: **not a 30 fps mode** (Pixelstein measured 6.9-7.2 fps there, SPEC.md 97.15) |
| a CGA 320x200 graphics frame: 16 KB at Set 148's ~86 clk a word in mode 4 | ~690,000 | 430% |

**This is why the answer is text mode.** Only text has a whole-frame present
that fits a 30 fps budget. Only a mostly black text frame leaves room for a
3D renderer beside it.

Caveat: MartyPC prices CGA 80-column text the same as 40-column (report,
*What this cannot say* item 1). A real IBM CGA is worse in 80-column, so
budget up to ~1.3x the CGA rows until a field run confirms them.

### 3.2 What the existing 3D games say

These are the measured rates from SPEC.md:

- **Pixelstein 3D (§97):** a DDA raycaster.
  - Default 8.5-8.8 fps, peak 14.6 fps.
  - **A column costs ~1,100-1,300 clk to set up and hit.**
  - The raycaster alone (cast 166k clk) is over a whole 30 fps frame.
- **TANK (§85):** a wireframe renderer.
  - 6-14 fps.
  - Its time goes on graphics-mode line drawing and span blits, not on
    geometry.
- **Clear Skies (§88):** filled polygons, 5.7-10.8 fps.

**Lesson:** a raycaster is the wrong tool here, because it spends per COLUMN
whatever the scene holds. A **segment renderer** spends per visible WALL, and
a small arena has few walls.

---

## 4. Rendering — "edges on black", 2.5D

### 4.1 The world (proposed)

- **An arena is a set of 2D sectors with floor and ceiling heights**, the
  Doom model.
- It is **built on the host into a BSP** by a new `tools/` converter, and
  shipped as a PART (§20.12).
- **Wall directions are restricted to axis-aligned and 45°.** That has three
  payoffs:
  - BSP split tests become compares, not multiplies.
  - Collision is cheap.
  - The edge glyphs (section 4.4) have fewer slopes to approximate.
- **"Layers"** are sectors at different floor heights:
  - a raised ring round a pit;
  - stepped tiers;
  - pillars and platforms;
  - jump pads.
- **Not possible in this model:** room-over-room, i.e. bridges you walk under.
- **Arena budget:** about 40-60 wall segments, with at most ~25 visible from
  anywhere. That figure is a design rule for the arena author, and the host
  converter can CHECK it by sampling viewpoints.

### 4.2 The camera (proposed)

- **Yaw:** 1,024 steps a turn, from mouse X.
- **Pitch:** a y-SHEAR of the horizon, from mouse Y.
  - The horizon row is offset; it is not a true rotation.
  - It costs one add per projected y.
  - It reads as "looking up and down" across about ±25-30° before verticals
    visibly fail to converge, which an 80x50 grid hides well.
  - Hitscan and projectile launch use the same shear, so the angle you aim at
    is the angle that fires.
- **Jumping:** gives the eye a z. Gravity and landing are fixed-point
  physics at a fixed step.
- **Projection:** one reciprocal per projected vertex, one `div`, then `mul`s.

### 4.3 Visibility (proposed)

The BSP is walked front to back, Doom-style, with **per-column clip
arrays**: 80 top bounds and 80 bottom bounds.

- A solid wall closes its columns.
- A step's riser narrows them.
- Sectors are walked until every column is closed or the BSP runs out.

Interiors are NOT filled. Only edges are plotted, but they are clipped by the
same arrays, so a far edge never shows through a near wall. Hidden-line
removal comes from the clip arrays, with no z-buffer.

The per-column depth of the nearest wall is kept, 80 words. Figures and
projectiles are depth-tested against it.

### 4.4 Composing the cell — walls thin, figures solid (proposed)

The frame is built in a RAM shadow of 80x25 char+attr cells, 4,000 bytes, in
two layers:

1. **Wall layer — thin line glyphs, dim.**
   - Each plotted edge chooses a glyph by its sub-cell position and slope:
     `─` and `_` for near-horizontal, `/` and `\` for diagonals, `│` for
     verticals, and corner glyphs where edges meet.
   - Distant floor-grid dots `·` are optional. They are a strong motion cue
     at a few dozen cells a frame.
   - Colour (CGA, VGA, EGA): dark grey, blue or cyan, dimmer with distance.
   - Mono (Hercules, MDA): normal intensity. The MDA font's line glyphs join
     across the 9th column, which suits this.
2. **Figure layer — solid blocks, bright.**
   - A figure is drawn at **2x2 sub-cell resolution**, so 160x50 in effect.
   - Each cell's 4-bit occupancy mask maps through a 16-entry table to the
     nearest CP437 glyph: ` ▀ ▄ ▌ ▐ █`, with the two diagonal masks
     approximated.
   - So a vertical limb can sit on a HALF column (`▌`/`▐`) and a horizontal
     one on a half row (`▀`/`▄`).
   - Colour: bright white, yellow or the player's colour, on black.
   - Mono: high intensity.
   - **A figure cell overrides a wall cell** when the figure is nearer than
     that column's wall. That is how walls never hide or blur a figure in
     front of them.

**The contrast rule** is: walls thin and dim, figures solid and bright,
projectiles a single bright glyph (`•` `*` `o`), and black everywhere else.
It holds in colour and in mono. Pick a glyph palette that leaves the
figure's glyphs (`▀▄▌▐█`) to figures alone, so the two layers cannot be
confused even on the 5151.

**The glyph table is per FONT.** The CGA 8x8 ROM font and the MDA/Hercules
9x14 one place `_`, `─` and `/` differently. Wave 0 decides the palette by
looking at both, on the glass.

**A VGA/EGA bonus.** §53.7 lets a bracket program the sequencer and graphics
controller directly, so a text-mode FONT can be loaded with custom glyphs:

- 2x4 "sextant" blocks;
- line pieces at 8 slopes;
- a crosshair.

That roughly doubles effective resolution on 5150 #2's VGA at no cost per
frame. It goes behind a `[vid_planes]` = 4 door. CGA and Hercules have ROM
fonts and keep the CP437 palette.

### 4.5 Presenting it (proposed)

- **Two shadows**, previous and current, 8 KB in all.
- Each row records the min/max column touched in EITHER frame.
- Only that interval is compared, with `repe cmpsw` in RAM, and changed runs
  are copied with `rep movsw`.
  - Wall edges and limbs run horizontally, so runs are common. A run beats
    the 67.9 clk a cell sparse figure.
- **Standing still costs ~0 writes.** That is the cost of edges on black,
  and it is why the frame rate holds.
- **Turning is the worst case:** every edge moves. That case sets the
  budget, in section 4.7.

Per adapter:

| adapter | mode | presentation |
|---|---|---|
| Hercules | FSX `TEXT80`, 1 page | delta writes. No snow. Tearing is possible but short, so start the writes after `FSXW_VSYNC` |
| VGA / EGA | `TEXT80` | page-flip between two text pages through `OSAPI_FSX_PAGE`, with **damage history per page** (CLAUDE.md's rule, from WireBall); custom font as above |
| CGA, clone or compatible | `TEXT80`, 4 pages | page flip as on VGA |
| IBM CGA, 1981 card | **snows in 80 columns** | three options, below. A menu item picks; the default is the owner's call |

The three options on an IBM CGA are:

- **(a) Accept snow.** Pixelstein's C160 does. The specks scale with changed
  cells, so a calm view is clean.
- **(b) 40x25 mode.** It has no snow, half the present cost, and half the
  geometry columns, at half the horizontal resolution.
- **(c) Write only in the non-display span.** CGA-SNOW-PLAN puts vertical
  blank at ~1.2 ms. A 262-line field with 200 displayed lines has ~3.9 ms
  with display-enable low. That would be ~85-275 sparse cells a frame, which
  is enough for a calm view and not for a fast turn. It needs a measurement
  before anyone relies on it.

### 4.6 Fluid motion inside a cell grid — the honest limit

At 80x25 a figure crossing the screen moves in half-column steps (4 px).
Vertical motion moves in half-row steps (4 px on CGA, 7 on Hercules).

- **Smoothness comes from rate, not resolution.** 30 updates a second of
  4-pixel steps reads as motion. 8 updates of 1-pixel steps reads as
  stutter.
- **The sim runs at a fixed step** (proposed: 30 Hz) and **keeps fractional
  positions**. It converts to cells only when drawing (WireBall's rule), so
  motion never aliases to the frame rate.

### 4.7 The predicted frame

This is arithmetic and not a measurement. Wave 0 measures it.

| stage | 1v1 duel, walking | worst: fast turn, 4 figures, 6 projectiles |
|---|---|---|
| BSP walk and cull (axis-aligned splits) | 1.5k | 2k |
| rotate ~24 endpoints (4 `mul` each) | 15k | 18k |
| reciprocal plus screen x and up to 4 heights per endpoint | 20k | 24k |
| near-plane clipping | 2k | 4k |
| column clip walk (~200 column-steps) | 12k | 15k |
| edge plotting into the shadow (~400 cells) | 18k | 22k |
| figures (about 4k each, section 5) | 8k | 16k |
| projectiles, HUD | 3k | 6k |
| diff and present (section 4.5) | 45k | 70k |
| sim, collision, input, net, AI | 15k | 25k |
| interrupts and kernel (tick, mouse at 40 Hz) | 5k | 5k |
| **total** | **~145k: ~33 fps** | **~207k: ~23 fps** |

**What this adds up to:**

- **~30 fps for a duel looks attainable** on Hercules and VGA.
- On a real IBM CGA in 80 columns it is marginal.
- A busy scene drops into the low 20s. The fixed-step sim keeps that
  smooth, and the arena budget in section 4.1 is the lever that holds the
  rate.

---

## 5. Characters and animation (proposed)

- **Skeleton.** A stick figure has ~11 joints: head, neck, pelvis, two each
  of elbow, hand, knee and foot.
- **Animations:**

| animation | keyframes |
|---|---|
| idle | 2 |
| run | 6 |
| strafe | 4 |
| jump and land | 3 |
| shoot | 2 |
| flinch, front | 2 |
| flinch, back | 2 |
| die, falling backward | 6 |
| die, crumpling | 6 |
| a body on the floor | 1 |

- **Built on the host into 2D tables.** Joints are pre-projected for **16
  view facings** at unit scale: about 40 frames × 16 facings × 11 joints × 2
  bytes, roughly 14 KB, which is a PART.
- **On the guest, per figure:**
  - one reciprocal for its distance;
  - an 8-bit scale of 22 values (`mul r8`, ~70 clk);
  - a lerp between two keyframes for in-between frames (shifts and adds);
  - ~10 limb lines plotted into the figure layer.
  - That totals **about 4k clk a figure** (predicted).
- **Facing.** The figure's facing relative to the viewer picks the table.
  Weak perspective (one scale for the whole body) is invisible at this size.
- **Hits.**
  - The figure flashes for 2 frames: bright red on CGA, reverse video on
    mono.
  - A flinch keyframe is chosen by the side the hit came from.
  - The crosshair shows a hit marker.
- **Death.**
  - The fall direction follows the killing shot.
  - The body stays on the floor until respawn, which helps the read of
    "where the fight was".
- **Muzzle flash.** One bright cell at the hand for 1 frame. It is also a
  cue to where an off-centre shooter is.
- **Size range.** At 80x50 half-rows, a standing figure is ~40 half-rows tall
  at 2 m and ~5 at 16 m. Beyond ~20 m it is drawn as a 2-cell "marker" glyph
  pair, so a distant opponent is never sub-pixel.

---

## 6. Weapons and projectiles (proposed)

*The first round is built: the pistol, the shotgun and the rocket launcher,
with pickups - [WIREARENA-WEAPONS-PLAN](completed/WIREARENA-WEAPONS-PLAN.md).*

| weapon | kind | what makes it interesting here |
|---|---|---|
| **Pistol** | hitscan | the baseline. Its ray uses the same BSP as rendering |
| **Grenade launcher** | **arcing**: ballistic, bounces, fuse | it makes jumping and the pitch shear matter. Splash damage. It also needs a trail of `·` dots for 3 frames |
| **Plasma** | slow straight bolt, dodgeable | you see it coming at 30 fps, which a hitscan duel never gives |
| **Rocket** | fast bolt, splash | **rocket-jumping** follows for free from the jump physics |
| **Rail** | hitscan with a trail drawn for 3 frames | wireframe makes a line through space look superb |

**Cost:**

- A projectile is one transform, one projection and one glyph: ~600-800 clk.
- On the network a projectile is **a spawn event**: origin, velocity and
  tick. Flight is deterministic integer physics, so both machines fly it
  identically.

Pickups (health, armour, ammunition) are a cheap addition, and give the
computer opponent goals other than "chase".

---

## 7. Input — the one OS gap

Facts (SPEC.md 9, 53.1, 96.10.2, 97.11):

- In a bracket a package reads `OSAPI_MOUSE`. It gets **absolute
  coordinates, clamped to the desktop**.
- `mou_apply` keeps **no accumulator**, so motion against an edge is lost.
- There is **no relative-mouse slot**. `OSAPI_MOUSE_REL` is named as "not
  taken" in §97.11. *(Since built: SPEC.md 9.14, way A below.)*
- There is no warp call. Pixelstein steers by "offset from centre", which is
  not modern aiming.
- The serial mouse reports ~40 times a second (1200 baud, 3-byte packets),
  which suits 30 fps.

The two ways to fix it:

- **A. Recommended — a kernel mickey accumulator plus a cell.**
  - `mou_apply` adds raw dx/dy into two words before clamping.
  - A new API cell (`OSAPI_MOUSE_REL`) reads and clears them.
  - DOS-EXEC-PLAN prices it at **~10 resident bytes** plus the cell, on
    every kernel.
  - **INT 33h's mickey counters (§96.10.2) want the same accumulator.**
    Spending these bytes buys two features, not one.
  - Adding a cell at the end is the non-bump form of §20.2.0.1's lock update.
  - SPEC.md goes first.
- **B. No kernel bytes — the package takes the UART.**
  - Mask IRQ4.
  - Poll COM1 in an `FSXF_RATE` hook (582 Hz against a byte every 8.3 ms at
    1200 baud).
  - Decode the Microsoft protocol itself, as WireBall's `wblink.inc` does
    for its link.
  - It works, but:
    - it dies on kern_small (no rate hook);
    - it ignores a PS/2 mouse;
    - it duplicates the kernel's decoder.

**Keys:** `OSAPI_KEY_DOWN` (a 128-bit held map, SPEC.md 9.7) serves WASD,
jump and crouch with any number held. That is Pixelstein's and WireBall's
pattern. Drain int 16h for Esc and F-keys.

**Keyboard-only aiming** must exist anyway: 5150 #1 cannot have a mouse and
a serial link at once. Arrows turn and PgUp/PgDn pitch, with acceleration so
turning is fast and fine aiming still works.

---

## 8. Multiplayer

### 8.1 The model (proposed): owner authority, not lockstep

Each machine simulates **its own player locally with zero input latency**.
It sends snapshots at 10-15 Hz:

- position (6 bytes)
- yaw and pitch (2)
- animation state and frame (1)
- health and armour (1)
- sequence (1)
- events (0 or more)

That is ~12-16 bytes before framing. The remote player is interpolated
between snapshots.

**Hits are judged by the SHOOTER.** It sends `HIT(victim, damage, dir)`, so
the target you saw is the target you hit. This is WireBall's
ownership-handoff spirit carried into an FPS. Projectiles are spawn events,
and splash damage is judged by the victim's machine.

**Why not lockstep:** lockstep is Doom's model. At 2400 baud it adds 2-3
ticks of input delay to your own movement, which is exactly what mouse aim
cannot tolerate.

### 8.2 Ethernet (proposed: raw broadcast frames)

- **No package can reach UDP.** The package verbs are TCP plus
  `NETV_RAW`/`RAWTX`/`RAWRX` (SPEC.md 72.22). *(Since 2026-11-14 they
  include UDP, SPEC.md 72.24, and a match on a network with an address
  uses it; raw frames are the fallback - WIREARENA-NET-PLAN 1.)*
- **TCP is wrong for a shooter.** A lost segment stalls every later snapshot
  behind it.
- **The route is a RAW claim with a private EtherType:**
  - broadcast `HELLO` frames for discovery and lobby;
  - unicast snapshots to each peer's MAC;
  - 2-4 players.
- The receive filter is own-MAC plus broadcast (§72.8), which is exactly
  this.
- **Costs and constraints** (§72.2, 72.18, 72.22):
  - A raw claim is **exclusive and stops `eth_pump`**. DHCP and TCP stop
    while a match runs, which is acceptable during a match.
  - The NIC is **polled, not interrupt-driven**: about a 10-frame ring,
    drained only inside verbs.
  - A verb costs ~1-1.6 ms on a 5150, so 2-3 verbs a frame is ~10-15% of a
    30 fps frame. Polling every frame keeps the ring from overflowing with 3
    peers at 15 Hz.
  - Calls must come from the foreground, never from a rate hook.
- **A gap to settle in SPEC first:** §53.7's list of legal calls does not
  name `OSAPI_DRV_CALL`. `FSXL_DRV` and DOS's packet driver imply it is
  legal, and the game should not rest on an implication. *(Settled:
  §53.7 names it now - WIREARENA-LINK-PLAN 12.2.)*
- **Cards:** NE1000 (8-bit ISA, so it fits an XT) and NE2000, including the
  PicoMEM's.
- **Where it can be tested:**
  - QEMU (closed-list entry 5);
  - 86Box (`vm/pc5150` has an NE1000, `xt-wire`);
  - **not MartyPC**, which has no NIC.
  - On real hardware: 5150 #2 against the Victory 286.

### 8.3 Serial (plausible, two players)

- **Link:** the §115 desktop lease, opened before the bracket. Read and Write
  are legal inside the bracket, and the kernel's ISR feeds 255-byte rings.
- **2400 baud.** WireFleet measured ~9% byte loss at 9600 between two XTs
  (§114.3). That gives 240 bytes/s each way: ~15-byte snapshots at 10-12 Hz
  plus events, so it fits.
- Framing and checksum: WireBall's 7Eh/7Dh format (§109.2) or WireFleet's
  CRC-16 frame, reused as SOURCE.
- **Port conflict:** the lease refuses the mouse's port. **5150 #1 has one
  port**, so a serial match there is keyboard-aimed. The Compaq has two.
- **Testing:** two QEMUs on a socket chardev (`tests/wireball.py` is the
  pattern), or two 86Box XTs on a FIFO (`tools/wireball-pair.py`). **MartyPC
  cannot link two instances.**

### 8.4 Not viable as it stands

- **NET.DRV's LapLink cable (§62)** is os8088-master to a DOS slave only,
  CPU-polled, at 3.7 KB/s.
- An os8088-to-os8088 parallel link would be NEW work. **It is worth naming
  anyway: it is the one cable 5150 #1 can use without giving up its mouse**
  (its LPT is on the Hercules card). That makes it a candidate for a later
  wave if the owner wants iron-against-iron on #1.

---

## 9. The computer opponent (proposed)

*Replaced by [WIREARENA-AI-PLAN](completed/WIREARENA-AI-PLAN.md), to the owner's brief
of 2026-11-14: fast more than good - routes with branches, a time to shoot
and an accuracy. Its section 1 records the owner's rulings on everything
left.*

- **It is a "virtual remote player".** It produces the same input the net
  code consumes, so bot matches exercise the multiplayer path, and a 1v1
  against a bot needs no second machine.
- **Navigation** uses a waypoint graph built on the host per arena, the same
  converter as the BSP. It covers jump links and jump-pad links.
- **States:**

| state | behaviour |
|---|---|
| roam | go to a pickup |
| hunt | head to the last known position |
| engage | strafe, keep distance, pick a weapon by range |
| retreat | low health |

- **Line of sight** uses Pixelstein's `CheckLine` walk (§97), or a ray
  through the BSP.
- **Aim** uses Tank's cross and dot steering (`tk_tankai`), plus a **reaction
  delay and an aim error per difficulty**. Grenades lead by solving a fixed
  table of arcs, not by trigonometry at run time.
- **Cost:** ~2-5k clk a bot a tick (predicted). Up to 3 bots fit beside the
  renderer, with the frame rate falling as the section 4.7 table shows.

---

## 10. Sound

5150 #1 has **no sound card**. PC-speaker PCM (`OSAPI_FSX_SPK`) costs ~48%
of the machine at 5.5 kHz (§34.11.4), so it is out.

Use **`OSAPI_SND_TONE` effects**, as Pixelstein does: shot, hit, death, jump,
pickup. An opponent's footsteps are clicks whose rate follows their speed,
which is a positional cue a speaker can give. *(Footsteps were tried and
removed: 17.1.)*

On machines with a Sound Blaster or AdLib, SOUND.DRV's FM can play richer
effects, behind the usual refusal.

---

## 11. Kernels and adapters

| | |
|---|---|
| **kern_big** | the primary target. Every feature is designed here |
| **kern_mega** | **a shipped kernel, so the design includes it** (CLAUDE.md). In a bracket a claim is a selector: VRAM and the PARTS come through `OSAPI_SEG`, or the whole bracket goes `FSXF_REAL` (`apps/os88real.inc`). Network and serial behave the same. Its rows sit beside kern_big's |
| **kern_small** | **no `FSXF_RATE`, no NIC.** A reduced build is possible — bots or serial, keyboard aim, with a fixed step taken off the tick at 18.2 Hz (WireBall's text-court fallback is the precedent). Otherwise it refuses cleanly. The owner decides which |
| adapters | `TEXT80` on Hercules, CGA, EGA and VGA through one renderer and **per-font glyph tables**. `TEXT40` on CGA is the snow-free option |

Memory: code ~20-30 KB, the animation tables ~14 KB, arenas 4-8 KB each, and
shadows 8 KB. The code fits one segment, and the arenas and animations are
PARTS. The region must follow §66.6.1.1, `OS88_REGION_MOVABLE`.

**Superseded by [WIREARENA-PARTS-PLAN](completed/WIREARENA-PARTS-PLAN.md)** (2026-11-13):
it measured the segment, forecast +35 to +80 KB still to come, and split the
game into a renderer part, the game in the primary, keyframes as an asset
and arenas fetched per match. Its section 6 records the owner's answers:
kern_big and kern_mega only (kern_small refuses), at most three figures, and
640 KB as the design machine.

---

## 12. How it would be verified

- **Renderer and frame rate:** MartyPC on `os8088_5150_cga_gla`,
  `_herc_gla` and `os8088_xt_vga`.
  - Guest cycles per frame.
  - **`m.flicker()` and `m.pace()`**, so smoothness is MEASURED in frames,
    not judged by eye (PERFORMANCE.md Parts 3.1-3.2).
- **Real-CGA text contention and snow:** one field run on 5150 #1's CGA. The
  report's caveat 1 is the question to ask.
- **Ethernet:** two QEMUs on a socket netdev, then 86Box `pc5150`, then
  5150 #2 against the Victory 286.
- **Serial:** two QEMUs on a socket chardev, and 86Box XT pairs.
- **The AI:** a bot-against-bot soak row with seeded RNG. Deterministic, so
  a replay is bit-exact.

---

## 13. Other suggestions

1. **Two players on ONE 5150 #1, one on each monitor.**
   - The owner's machine has a Hercules and a CGA on separate screens. Render
     P1's view to B000 and P2's to B800, at half the rate each (~15 fps).
   - Controls: the mouse plus WASD for one player, the keypad for the other.
   - No cable, and unique to that machine.
   - **Open question:** whether an FSX bracket can own BOTH cards. §53 is
     written for one display, and §39.11's extended desktop is where two-card
     work already lives.
   - *Dropped by the owner (2026-11-15, 17.1): it would need a 286 or
     better, which is unlikely to carry a Hercules card, and the game plays
     best on CGA anyway.*
2. **Deterministic replay and kill-cam.** Fixed-step integer sim plus
   recorded inputs replays exactly. A 3-second "kill-cam" from the killer's
   view is cheap, and so are demo files.
3. **A distance cue from the edge colour.** Near edges are bright cyan, far
   ones dark blue. On mono, near is bright and far is normal. Depth reads
   instantly in a wireframe.
4. **A floor dot-grid** under arenas with open floors. It gives strong
   parallax for a few dozen cells.
5. **Modes:**
   - duel;
   - free-for-all with bots;
   - "last stand" (waves of bots, solo);
   - a one-flag capture.
6. **Arena features that exercise the engine's strengths:**
   - jump pads (arcs);
   - teleporters (a cut, which costs nothing to render);
   - a central pit with tiers ("layers").
7. **A radar strip** in the HUD row: opponents as single glyphs by bearing.
   It is cheap, and it compensates for the narrow field of view a text grid
   forces.
8. **A host-side reference renderer** (Python) that runs the arena file and
   produces the expected cell grid for a camera. Rows then compare the
   guest's shadow against it byte for byte, which is `weavesim.py`'s role
   for WEAVE.

---

## 14. Risks and open questions for the owner

1. **Real IBM CGA in 80 columns.** Contention may be ~1.3x MartyPC's, and
   there is snow. Snow, 40-column mode or vertical-blank writes: which is the
   default?
   *The owner (2026-11-14): accept the snow; the user picks the width. 80
   columns is not performant on a 5150 today, and the 5150 is the only
   machine with a snowing card.*
2. **The relative-mouse cell.** It costs ~10 resident bytes plus a cell on
   every kernel, and INT 33h wants it too. Approve, or should the package
   borrow the UART?
   *Approved and built (SPEC.md 9.14).*
3. **Ethernet by raw frames.** A match stops TCP and DHCP while it runs. Is
   that acceptable? And §53.7 should name `OSAPI_DRV_CALL` explicitly.
   *Acceptable; §53.7 names it (WIREARENA-LINK-PLAN 12.2). UDP came later
   and raw frames are now the fallback (WIREARENA-NET-PLAN 1).*
4. **kern_small:** a reduced build, or a clean refusal?
   *Refusal (WIREARENA-PARTS-PLAN 6).*
5. **The glyph palette.** It is decided by looking at the CGA 8x8 and MDA
   9x14 fonts on the glass in Wave 0. The table in section 4.4 is a starting
   point.
6. **The two-monitor local mode** needs a ruling on whether a bracket may
   own both cards. *Dropped by the owner (2026-11-15, 17.1).*

## 15. A first cut of the waves (draft — to be rewritten as stories when approved)

- **Wave 0 — the renderer spike. This decides everything else.**
  - The host BSP converter and one arena.
  - The edge-on-black renderer plus the presenter in a bare `TEXT80` bracket.
  - The camera driven from keys.
  - Measure cycles a frame on CGA, Hercules and VGA in MartyPC, plus
    `flicker` and `pace`.
  - Exit criterion: ≥30 fps walking and ≥20 fps turning on Hercules, or a
    revised plan.
- **Wave 1:** the relative-mouse cell (SPEC first), yaw and pitch shear,
  jumping, and collision with steps and tiers.
- **Wave 2:** figures and animation tables, the pistol, hits and deaths, the
  HUD.
- **Wave 3:** the computer opponent, navigation and waypoints, and a bot
  soak row.
- **Wave 4:** the serial link, with the owner-authority model.
- **Wave 5:** the Ethernet raw link, the lobby, and 2-4 players.
- **Wave 6:** arcing and slow weapons, pickups, sound, more arenas, the CGA
  snow option and kern_mega rows.
- **Wave 7:** the field run on 5150 #1, the CGA questions, and the
  PERFORMANCE.md set.

---

## 16. Wave 0 — what was built and what it measured

> **Read this section as a log** (since 2026-11-15). What it calls "the
> full arena" is **pillars** on the setup's list (arena 0); the keys F (field
> of view, now fixed at 75 degrees), G (floor grid), H (held edges), L
> (sparse tops), T (treads) and J are gone - the grid and treads are the
> detail levels' (F1-F5), H and the sparse level were removed - and B and P
> exist only in a `make WABENCH=1` build (WIREARENA-NET-PLAN 9.9, 9.10).
> There is no Game menu: the match setup window chooses everything (NET-PLAN
> 9.4, 9.9).

**Built:** `apps/wirearena/` (`WARENA.O88`, `make wirearenadisk`). It is an
on-demand `instrument` in `apps/RETIRED.txt` until it is a game. *(Since
2026-11-15 it is one, still on its own disk and still listed there until
the release work of 17.2 ships it.)* The package
has these parts:

- `wirearena.asm`: the shell, the loop, input and the 30 Hz sim.
- `wrender.inc`: the renderer.
- `tools/arenagen.py`: builds the sine table, the arena, the patrols, and
  the stick-figure keyframes pre-projected for 16 facings.

What the prototype contains:

- A 32 m room with nine blocks.
- Three dummies running patrols. Each has run, flinch and death animations,
  blended in quarter steps.
- The pistol: hitscan through the crosshair, three hits to drop a dummy, a
  hit flash, and respawn after three seconds.
- Mouse look through `OSAPI_MOUSE_REL`, WASD, jumping, and pitch by y-shear.
- **Tab** swaps between 80 and 40 columns, live. **V** presents after
  vertical retrace. **B** runs the BENCH. *(Since: `-` and `+` set the
  width, Tab held is the board and minimap, B is a test build's.)*
- A HUD meter showing fps, frame ms, render ms, present ms and video writes
  a frame.

**The bench** (B) is 64 frames from (1024, 700), turning 1/64 of a circle
each frame, with the dummies frozen at their path starts. It is the same work
on every build, in either width. These figures come from MartyPC on
`os8088_5150_cga`, `_herc` and `os8088_xt_vga`. The three agree to the
millisecond, because MartyPC charges their text memory alike (see the report's
caveat about a real IBM CGA):

| | render | present | frame | fps | video writes a frame |
|---|---|---|---|---|---|
| 80 columns | 69.4 ms | 13.4 ms | 76.7 ms | **13.0** | 193 |
| 40 columns | 52.0 ms | 7.8 ms | 55.3 ms | **18.0** | 113 |

These are against section 4.7's prediction of ~145k clk (~30 ms) for a
duel. **The first renderer costs ~2.5x the prediction at 80 columns.**

The sampling profile of a busy view (`flat_ip` bucketed by listing labels)
shows where the time goes:

| part | share |
|---|---|
| wall edges | ~24% |
| per-column depth raster | ~16% |
| figures (limb plotting, keyframe blends) | ~19% |
| geometry (transform, clip, project) | ~22% |
| present | ~10% |

The first version was at 7.4 fps before two rounds of tightening. Most of
that loss was a call chain four levels deep per plotted cell.

**What it looks like** is on the glass in screenshots taken during the work:

- **Walls** read as a dashed wireframe. A shallow slope becomes a staircase
  of `─` and `_` at half-row steps; box corners are `│`. They dim with
  distance: bright cyan, then cyan, then blue on colour; normal intensity on
  mono, where the MDA font's line glyphs join into solid lines.
- **The figures**, close up, read unmistakably as running stick men with the
  pistol arm out. At mid range they are a small cluster of blocks. 80 columns
  keeps limb detail further out than 40 does.
- **Snow** on a real IBM CGA at 80 columns is not measured. MartyPC does not
  show it. 86Box reportedly does (the owner); the owner's own CGA is the real
  test.

**The design levers for 30 fps**, in order of expected gain:

1. **Front-to-back "solid segment" occlusion instead of the per-column
   depth buffer.** The arena's walls are full-height, so a column closed once
   is closed. That removes the raster's per-column compares and stores. It
   also lets the edge pass step each top and bottom line incrementally across
   its visible span, rather than reloading and rounding per column.
   Front-to-back order comes from a host-built 2D BSP (section 4.1).
2. **Shared products for axis-aligned boxes.** A box's corners share x or y,
   so 8 multiplies replace 16. A reciprocal table can stand in for the
   per-vertex divides.
3. **Figures:** clip and depth-test per LIMB, not per sub-pixel, and plot
   inline.
4. **Present:** walk the two lists in one merged pass.

The decision the owner asked to make — **40 or 80** — now has both on one
disk, with the meter, switchable live.

### 16.1 Optimization round 1 — measured

The bench is unchanged: 64 frames, a fixed spin, MartyPC `os8088_5150_cga`.

| | render | present | fps | before |
|---|---|---|---|---|
| 80 columns | 64.0 ms | 13.4 ms | **13.7** | 13.0 |
| 40 columns | 50.1 ms | 7.6 ms | **18.4** | 18.0 |

**What was done**, each checked on the glass:

- **The renderer is now front to back over a box-level BSP.** Boxes are
  convex, so the tree orders only the nine boxes: 17 nodes, every split a
  clean axis-aligned line, refused at build time otherwise. The room comes
  last.
  - A box's faces are chosen by four compares.
  - A column a nearer wall drew is closed, and the walk stops once all are
    closed.
  - A wall whose reachable columns are all closed is dropped BEFORE any clip
    or divide. The reachable range is found from camera space, and an open
    column is found with one `repne scasw`.
  - Transforms are two 32-bit adds per vertex, off per-coordinate products
    made once a frame.
  - Projections are cached per vertex.
  - The left/right frustum clip is gone. Spans clamp to the screen; a guard
    band of |x| <= 8z keeps the column divide in range.
  - The per-column depth buffer and the separate edge pass are gone: a
    span draws its open columns as it steps.
  - Slope fills are tested only on spans steep enough to need them.
  - Corners are drawn after the loop. A box corner is drawn once: a face's
    start, and its end only on a silhouette.
- **The host check of the BSP order** compares the first front-facing wall
  per column against a ray-cast, over 24,000 columns from 300 viewpoints.
  It found 0 errors, and 1,112 with the walk reversed.
- **Cells are indexed by OFFSET** throughout (stamp, mask, lists, row
  tables), which removes a shift at every plot and in both present loops.
- **Figures:** sub-pixels plotted inline in the line loop. Past ~15 m a
  figure is a one-sub-column marker instead of nine limbs.

**Why it moved so little.** Two instruments explain it.

- **Counters** read off the guest: per frame 35 walls walked, 19 facing, 7
  spans set up, 79 columns opened, 27 vertices, 14 clips (before the guard
  band), and one figure of ~20 sub-pixels.
- **The listing:** an opened column ran ~260 bytes of code, and the 8088
  fetches at ~4.34 clk a byte (PERFORMANCE.md Part 2) — ~1,100 clk before
  any data moves, ~1,800 measured.

The plan's per-frame estimate (section 4.7) priced instructions at a few
clocks each; the floor is ~20. After this round the profile is FLAT: span
~29%, projection ~11%, vertices ~8%, present ~8%, figures ~9%, nothing else
over 5%. No single lever is left.

**The honest reading:**

- 30 fps at 80 columns is out of reach for this renderer on a stock 4.77 MHz
  8088.
- 40 columns is ~19 fps on the bench's worst case, a continuous fast spin
  across the busiest view. The HUD in the look-around runs read 12–16 fps
  at 40 columns, including the 30 Hz sim's ~2 ms a tick. (Wave 0's 20–24
  readings were taken while a cap bug ran the sim at a third of its rate.)
- What would still move it:
  1. A per-column path in the ~100-byte class. That means a hand-scheduled
     register loop, the accumulators out of memory, and tops and bottoms as
     8.8 values when on screen.
  2. A reciprocal table instead of the per-vertex divides.
  3. Fewer cells: for example, top edges only, or no slope fills, as a
     player option.
  4. Present merged into the span pass.

  Each is a few percent to perhaps 15%. Together they might bring 40 columns
  to the mid-20s.

### 16.2 Optimization round 2, and a sparse detail level — measured

Same bench. F is the full detail level, S the sparse one.

| | F render | F fps | S render | S fps | round 1 |
|---|---|---|---|---|---|
| 80 columns | 55.5 ms | **15.5** | 49.5 ms | **18.5** | 13.7 |
| 40 columns | 46.8 ms | **19.5** | 43.7 ms | **21.7** | 18.4 |

**The column loop is hand-tuned.** It is one NASM macro (`SPANLOOP`)
assembled four ways — top edge or not, steep-step fills or not — so no
column tests a mode. Inside it:

- The loop ends on a compare, not a counter.
- An edge's glyph comes from a half-row table.
- The distance shade is no longer chosen per column. A span is cut into at
  most three column ranges where its inverse depth crosses a threshold
  (`.bands`).
- Depth is neither stepped nor stored. A span registers its projected ends in
  a table (`wa_cov` holds the span's tag), and `wa_wallz` interpolates the
  inverse depth for the columns a figure or the trigger actually asks about
  (`wa_figcols`, `wa_colsz`).
- An opened column at full detail on a flat span is now ~120 bytes of code,
  against ~260.

After this round the loop itself is ~11% of a 40-column frame. The span
SETUP is ~18%: the occlusion scans and two divides a span.

**Tried and backed out.** Computing the per-coordinate products lazily, once
per coordinate per frame, measured SLOWER: 19.5 -> 19.0 fps. Nearly every
coordinate is used, so the stamp tests were pure cost. The note is at
`wa_products`.

**The sparse detail level (L)** *(removed 2026-11-15: WIREARENA-NET-PLAN
9.9, 9.10)* draws floor lines and corner posts — no top
edges and no top slope fills. It is about 40% fewer video writes (80
columns: 149 against 240 a frame), and the posts still carry the walls'
height. The HUD shows F or S.

**Where 40 columns stands:** ~20-22 fps on the worst-case spin. The rest is
mostly fixed per frame and does not shrink with the width:

| part | share |
|---|---|
| vertex transforms, projections, products | ~22% |
| span setup | ~18% |
| figures | ~10% |
| present | ~7% |

### 16.3 The owner's first field run, and what it found

The owner ran WARENA on 5150 #1's CGA (2026-11-12):

- **40 columns:** "looks fine, as the default for CGA". It is now first in
  the Game menu. *(Since: no Game menu; 40 columns is the default on an
  8088, in the setup's Columns drop-down.)*
- **The rate in play** — moving the camera, three dummies, shooting — was
  **10.4–11.7 fps**: "still too low to be playable".
- **The sparse level:** makes the room "a lot harder to read" and "doesn't
  seem to have a major impact on FPS". Its measured gain was 2 fps at 40
  columns.
- **"WASD does not seem to do anything"**, while Esc and L worked.

**WASD was not the keyboard.**

- The start position (1024, 256) was INSIDE the south stub (960..1088 x
  192..320), so every step collided and was undone. Turning and the trigger
  worked because they do not move the player.
- Wave 0's scripted walk never caught it: no shot was compared with the one
  before it.
- `tools/arenagen.py` now REFUSES a start inside a wall (and a patrol that
  runs into one). The start is (1024, 480).
- Making the start per-arena then exposed a second bug: `wa_reset` stored
  the start ANGLE into the pitch, the jump height and velocity, and the hit
  counts, which until then had relied on AX being 0. Fixed.

**The field rate was taken from inside the stub, which is no measurement of
play.** It should be re-taken.

**A second, sparser ARENA.** The menu now offers "Open arena": the same
room, only the four pillars, beside the full one. *(Since: both are on the
setup's Arena list, as "open" and "pillars".)* The question it answers is
how much of a frame is the scene's own complexity. Arenas are host-built
(`arenas()`), copied into working tables at launch (`wa_loadarena`); each
tree is walked where it was assembled.

A play scenario on MartyPC `os8088_5150_cga`: about 8 guest seconds of mouse
turning with the dummies running and the trigger pulled every second, read
off the HUD at the end:

| | fps in play |
|---|---|
| full arena, 40 columns | **20.1** |
| open arena, 40 columns | **24.8** |
| full arena, 80 columns | 14.5 |

These are emulator figures. The report's caveat about a real IBM CGA's
text-mode contention applies, and the field rate is the one that decides.

### 16.4 The owner on 86Box, and what followed

The owner ran 86Box (2026-11-12), and the timing there is coarser than
MartyPC's:

- **The rate:** the full arena at 40 columns ran 12–14 fps with three
  figures, the camera moving, running and shooting. The open arena ran ~2
  fps more.
- **The target:** the FULL level of detail, which will hold rooms and stairs
  on screen. The open arena is a style a level may use.
- **The look:** walls read well up close. Far away their distance "keeps
  jumping as the perspective changes", closer at the side of the view and
  pushed back at its centre.

**That has three causes.**

1. **Planar projection.** A wall's height goes with its depth ALONG the
   view, so at the edge of a 90-degree view it is 1.41x its height at the
   centre.
2. **The distance shades used that same depth**, so a wall turning to the
   centre could drop a colour band.
3. **The half-row quantum** is 15–25% of a far wall's height.

**The owner's answers, and what was built.** Each is measured on the bench
at 40 columns.

| change | how | cost |
|---|---|---|
| **Colour says what a wall IS, not how far.** Two shades never read as depth, and Hercules has none: boundary walls dark blue, items (pillars, blocks, later stairs) cyan, floor dots dark grey, figures white; mono keeps every wall normal intensity and the figures bright | a per-box colour index in each arena's data (`wa_<tag>_bcol`), set per box in the walk | the per-span band split is gone (cause 2 with it), so it costs nothing |
| **F: the field of view**, 90 -> 75 -> 60 degrees. A zoom, the vertical scales following, so proportions hold. The edge stretch is 1.41x / 1.30x / 1.15x | `wa_setfov` | none: it is a constant |
| **H: held far edges.** An edge keeps last frame's half-row until the true position is a quarter half-row past the boundary, per column. Against cause 3's flicker on slow turns | `EDGE`; `SPANLOOP` is assembled eight ways now | 20.1 -> 19.3 fps |
| **G: a floor grid**, a dark dot every 2 m within ~6 m, behind walls and under figures | one divide and three multiplies a dot | 20.1 -> 15.9 fps |

*(Since 2026-11-15 the F, H and G keys are gone: the view is 75 degrees,
H was dropped (WIREARENA-HEIGHTS-PLAN 9.11) and the grid is the High and
Very High levels'.)* Each toggle shows at the HUD's tail: view (9/7/6), H, F/S, G. Rejected: a
true-distance (cylindrical) projection — it bends straight walls and is
slower.

### 16.5 Far walls drawn in front of nearer ones — a precision bug, found and fixed

**The owner's feedback** (86Box, with screenshots):

- Colour per item: keep.
- 75 degrees: "probably best", but no change to the distance issue.
- Held edges: no visible difference.
- Floor grid: "looks amazing" but costs too much on 4.77 MHz. Keep it as an
  option, on by default on a 286 or better — done, through `OSAPI_CPU_INFO`.

**The shots showed what 16.4's three causes could not explain.** A
dark-blue ROOM wall drew TALLER than a cyan pillar standing in front of it.
Every wall here is 3 m on one floor, so no projection makes a farther wall
taller. It was a bug.

**The instrument: `tools/wacheck.py`.** It boots WARENA in MartyPC and
teleports the player to random poses by writing the BSS. It reads text VRAM
directly, because `screen()` drops `─` and `│`. Then it compares each
column's edge rows with a host reference: the nearest front-facing wall a
ray through the column's centre meets, projected with the guest's own
integer scales.

**Two of its own mistakes cost time, and are fixed in it:**

- reading the screen through `screen()`, which saw half the edges;
- BSS addresses from a stale map, which wrote every pose into the wrong
  bytes once the image grew. The tool now assembles a map of the exact build
  it boots, and refuses a mismatch.

**What was wrong:**

- A wall clipped at the guard band — a room wall running off the side of the
  view or behind the camera — starts ~150 columns off the screen.
- The edges' value at the first VISIBLE column was STEPPED from there, with a
  per-column step truncated to a whole 13.3 unit. Up to 0.99 of a unit was
  lost per column: 2+ half-rows by the screen.
- Bottoms erred less than tops because their change across the wall is
  smaller, so mostly the top edge rose — the wall "jumped forward". Turning
  moved the clip point and the error: "jump back". Walking closer shrank the
  off-screen run: "recedes to its actual distance".

**The fix** (`wa_span`):

- The first open column's top and bottom are interpolated EXACTLY: one
  multiply and divide each (`.vat`).
- The per-column steps carry a 16-bit fraction (`.step32`, an `adc` in the
  loop).
- The far corner is exact too.
- Steps are also computed whenever the segment spans a column. They had
  been skipped when the TRIMMED span was one column, giving the wall's end
  height there.

**The result** (90 degrees, seed 1, 30 poses):

| build | poses with an edge two rows or more off |
|---|---|
| without the fix | 15 of 30 |
| with the fix | 5 of 30 |

The same poses, so the tool shows both. A wider run gave 6 of 60. What
remains is a single column where a NEAR box's corner lands one column early
or late; rounding the projection's divide did not change it, and it is not
yet explained.

**Cost:** the bench went from 20.1 to 19.1 fps at 40 columns.

**The checker also caught a fresh bug in 16.4's FOV toggle.** The column is
`centre + x * scale / z`. At 90 degrees the centre (8W) and the scale (8Wf)
are one number, and `wa_k16` stood in for both. At 75 and 60 degrees every
wall was drawn ~6 columns right of its place — figures were placed right,
and the floor dots had the same fault. The fix is a separate `wa_c16`. **The
owner's judgment of 75 degrees was made on that build** and wants a second
look. With the fix, at 75 degrees, 6 of 30 poses have one such column.

### 16.6 Performance pass 3 — four agents, three areas, measured together

**The owner's verdicts:**

- The walls look right even far away: 16.5 fixed it.
- 75 degrees "still feels about right"; 90 gives the most awareness and 60
  is narrow.
- 60 has a jump bug: the screen squashes at the top of a jump. This is very
  likely `eye x kyq` passing 16 bits at 60 degrees with the eye raised.
  Deferred until stairs and platforms make jumps matter. *(Fixed by
  WIREARENA-HEIGHTS-PLAN 1.6; and since 2026-11-15 the view is fixed at 75
  degrees.)*

**How the pass was run.** One read-only reviewer was asked whether there is
a better core technique. Three implementers worked in isolated worktrees —
wall geometry; columns and present; figures and the sim — each measured on
the same bench and held to `tools/wacheck.py`. Their branches merged with no
textual conflict.

**The reviewer's key finding came from a host simulation.** Over 2,000 free
poses in the full arena, only 1.8 of the 9 boxes are visible, 6.4 lie wholly
outside a 90-degree cone, and 0.4 are in view but hidden. The coverage
machinery served the rare case; frustum work was spent on boxes no ray could
reach. Its other proposals:

| proposal | outcome |
|---|---|
| figure limbs into a mask, then one plot per cell | done |
| per-column wall records | weighed by the columns agent: ~1% for real overlay risk, not done |
| Doom-style wall setup without clipping | deferred to stairs |
| self-modifying loops | low, and kern_mega faults on writes through CS |

It advised against the top-from-bottom height ratio (it breaks with stairs),
reduced-rate or interlaced walls, and page flipping for speed.

**Geometry (`perf-geometry`):**

- A 1/z reciprocal table (5.8 KB of BSS, the same bits as the divide).
- Columns computed with the transform; tops, bottoms and 1/z only after a
  face passes the occlusion pre-check.
- Products from per-axis steps: ~12 multiplies a frame instead of 52.
- An unrolled clip, and faces from a table.
- **BSP nodes culled against a conservative 90-degree cone before any vertex
  work** (arenagen emits node bounds).
- Faces wholly off one side dropped.
- Geometry went from ~16.8 to ~8.9 ms.
- Text VRAM was byte-identical to the base over 130 poses, at both widths and
  at 60 and 90 degrees.

**Columns and present (`perf-cells`):**

- The top edge lives in registers.
- One divide per flat edge's step, and the start value as step x offset.
- The far corner comes from the accumulators.
- Corners skip stamp tests where nothing can collide.
- List appends use `stosw`, and present takes two cells a turn.
- A run of shut columns is jumped by a multiply (`wa_gap`).
- An opened column is ~105 bytes of code, down from ~142. The precision is
  unchanged: error sums against the reference were 382 before, 376 after.

**Figures and sim (`perf-figures`):**

- Limbs are walked with the midpoint rule into a cell-mask scratch. The
  line's state is its place in the code: ~70 clk a sub-pixel against ~700.
  One depth test and plot follows per cell.
- 8-bit joint multiplies beyond ~1.6 m.
- Keys are polled once a frame, not once a tick.
- Standing still skips the walk and the collisions.
- The figure pass is 37–47% cheaper with three figures in view, and the sim
  60% cheaper.
- Hits, flinch and death were checked. Posed VRAM is byte-identical to the
  old renderer with only the new line rule.

**Merged, measured on MartyPC `os8088_5150_cga`:**

| | before (e02c073a) | after |
|---|---|---|
| bench, 40 columns, full | 18.8 fps | **28.1 fps** (R 31.4, P 7.2) |
| bench, 40 columns, sparse | 21.1 | **31.4** |
| bench, 80 columns, full | 14.8 | **21.5** |
| play, 40 columns (HUD) | ~19 | **27.2** |
| play, open arena, 40 columns | ~25 | **36.1** |
| play, 80 columns | ~14.5 | **24.5** |

`wacheck`: the same 6 single-column poses. The package is 21.8 KB of image
and 36.8 KB of BSS, 58.6 KB of the 63.5 KB a package may have.

**Left, each under ~2%:**

- inline `wa_plotf` in the figures' emit;
- a FOV-aware cone;
- `wa_face` handing values over in registers;
- a run-based column loop;
- analytic clipped limb walks;
- the bottom edge's step in registers, which needs DX freed from the stamp
  scheme.

The renderer is near the 8088's fetch floor for this design. A further step
would need a change of design, not tuning.

### 16.7 A play bench — the `P` key

> **Since 2026-11-15 a TEST build's** (WIREARENA-NET-PLAN 9.9): `B` and `P`
> are assembled only with `make WABENCH=1`; the shipped game has neither.
> The rows and tools that press them build a private tree with it
> (`tests/walift.py`'s `bench_build`).

Bench `B` (16.1) flies a fixed path through the arena and is good for
comparing renderer changes. The owner asked for a second bench that looks like
PLAY. Its numbers should be judged against a frame rate felt at the controls.
The owner's estimate from play on 86Box was 16–19 fps.

**What it does.** `P` starts it from the Game screen, in either width. For
240 sim ticks (8 s):

- **A fixed pose.** The player stands at (224, 800), looking about 11 degrees
  north of east. That is part of the room, not all of it, and not a corner.
- **Running dummies.** The three dummies are spawned and advanced 160, 64 and
  80 ticks along their patrols before the run starts. They then run and animate
  through the 8 s.
- **A circle of the aim.** The view moves in a circle, not a turn: yaw swings
  ±6 degrees and pitch ±2 rows, a quarter-period apart, once every ~1.5 s
  (23 sixty-fourths of a turn of phase a tick). The mouse is read and thrown
  away, and WASD is ignored.
- **The HUD.** The numbers clear when it starts. At the end they show the
  run's averages: `FPS`, `MS` a frame, `R` (render ms), `P` (present ms) and
  the text cells written per frame. A `P` at column 32 marks a play-bench
  result.

**How the pose was chosen.** A host search (`arenagen`'s geometry, the 75-degree
view less 6.5 degrees of margin, 2.5–25 m) found no pose that kept two dummies
in view for a full 10 s under stricter limits. At 8 s with these limits, two or
more are in view at every one of 24 samples, and three at 5 of them.

**Measured on MartyPC `os8088_5150_cga`, full detail, FOV 75:**

| | FPS | ms a frame | R | P | cells a frame |
|---|---|---|---|---|---|
| play bench, 40 columns | **15.9** | 62.8 | 52.6 | 7.0 | 115 |
| play bench, 80 columns | **12.9** | 77.1 | 61.0 | 12.7 | 246 |
| bench `B`, 40 columns (16.6) | 28.1 | — | 31.4 | 7.2 | — |

The 40-column figure sits at the bottom of the owner's 16–19. Present costs
about what it did on bench `B`. The difference is render, at 52.6 ms against
31.4. This view keeps much of the full arena's walls in front of it, and two
or three animated figures in every frame. So this is the bench
to quote for play, and render in a full room is where the next round should
look.

### 16.8 Performance pass 4 — five agents on the play bench, measured merged

Five agents worked from `d30f8d30`, one on the whole frame and one each on
geometry, spans, figures and present. Each owned an area of `wrender.inc` or
`wirearena.asm` and had to prove its picture: text VRAM byte-identical to the
base over hundreds of posed frames, with figures posed in every state.

**What the whole-frame count found (`perf-overall`).** On the P bench at 40
columns the renderer does almost no wasted drawing: nothing is overdrawn and
the coverage map shuts 0.07 faces a frame. The frame goes on FIXED costs per
face, span and figure. About 6 ms went on faces that drew nothing (off-screen
or edge-on), and ~1 ms on cone tests at split nodes that never dropped
anything.

**What landed:**

- **Geometry (`perf-geom`).**
  - The front-to-back box order is cached per cell of the split lines
    (`wa_order`, `wa_lord`), so a frame walks no inner node while the player
    stays in that cell. That covers turning and moving inside it, so it is
    not a "nothing changed" test.
  - Only the boxes take the cone test (`wa_leaf`). `perf-overall` found the
    same.
  - `wa_vert` leaves column sentinels, so `wa_face`'s pre-check is two loads.
  - A narrower-view bound at 75 and 60 degrees.
- **The exact pre-check (`perf-overall`).** It uses the same column range as
  `wa_span`, `(c + 7) >> 4 .. ((c + 7) >> 4) - 1`, so a face seen edge-on ends
  before its tops and bottoms are made. At the merge it was re-expressed on
  `perf-geom`'s x16 sentinels, with the +7 taken from the remainder's carry,
  because ±32767 would overflow an add.
- **Spans (`perf-spans`).** A span opens only ~4 columns, so per-span setup
  (~4,700 clk) outweighed the column loop.
  - Flat spans are drawn in two passes by runs, one edge a pass: `FLATPASS`,
    `wa_fedge`, `wa_fcell`.
  - A shut column costs one step, not a `wa_gap` call.
  - An opened column costs ~170 + 150 clk, against ~780.
  - The sparse level takes the same path. `wa_l000`, `wa_l100` and
    `wa_eatt` are gone.
- **Figures (`perf-figs`).**
  - The joints' bounding box is found first. On-screen figures skip the
    limbs' clip tests, and a figure wholly behind a wall skips limbs and
    emit.
  - `wa_femit` plots inline.
  - The draw order is an insertion sort.
  - Wall depth in a column is taken from the span's end depths whenever
    those bound it.
  - Keys are asked of the kernel only when they could have changed: once
    every 4 frames, or when the BIOS buffer saw them.
  - The sim ticks are cheaper.
  - **A bug fix that changes the picture:** a far figure drawn as a marker
    took the previous near figure's colour. Now it takes its own, so a hit
    far dummy flashes. Only marker attribute bytes differ.
- **Present (`perf-present`).** It already walked lists, not the screen, so
  its cost was ~138 clk a listed cell, near the bus floor.
  - Four cells a turn, with the usual case falling through.
  - Compared against text VRAM, not a RAM copy of it. `wa_vis` is gone,
    −3,840 bytes.
  - Indexed off DI.
  - `wa_fmask` is the odd byte of `wa_stamp`'s word, another −3,840 bytes.

**Merged, measured on MartyPC `os8088_5150_cga`, full detail, FOV 75:**

| | d30f8d30 | pass 4 |
|---|---|---|
| **P bench, 40 columns** | 15.9 fps (MS 62.8, R 52.6, P 7.0) | **19.1 fps** (MS 52.2, R 44.8, P 5.3) |
| **P bench, 80 columns** | 12.9 (MS 77.1, R 61.0, P 12.7) | **15.8** (MS 63.0, R 51.5, P 9.4) |
| B bench, 40 columns, full | 28.1 | **33.7** (R 26.7, P 5.6) |
| B bench, 40 columns, sparse | 31.4 | **37.0** |

Each agent's own gain on P at 40 columns was 16.3 (overall), 16.9 (geom),
16.3 (spans), 16.8 (figs) and 16.3 (present). The merge adds them up.

**Correctness of the merge:**

- The figures agent's six sets (720 posed frames, including two
  marker-heavy far sets) are identical to its post-fix references.
- The spans agent's five sets (560 frames, mixing sparse, hysteresis and
  floor grid at all three FOVs) are identical to `d30f8d30`.
- The open arena (60 frames) is identical.
- Hercules at 80 columns (120 frames) differs from `d30f8d30` in 2 attribute
  bytes only: the marker fix, `0x0F` → `0x70`, the mono flash.
- `wacheck --poses 30` gives 6, the same as the base.
- The fast tier passes.

**Size:** image 22,201 → 23,226 bytes, BSS 36,803 → 29,383. The package is
52,609 of the 65,024 bytes a segment may hold. Present's 7,680 bytes paid
for everything else, with ~6.6 KB to spare.

**Owner's calls this pass raised:**

1. **Present reads text VRAM at 80 columns on CGA.** A real IBM CGA snows on
   a CPU read in 80-column text as on a write, so this adds ~206 reads to
   ~242 writes a frame there. 40 columns (the CGA default), Hercules, EGA and
   VGA do not snow. It is one commit (`8faddff8`) and can be dropped alone.
   **The owner kept it (2026-11-13):** snow is the 5150's CGA alone, a 5150
   with CGA will mostly play at 40 columns, and the later cards that solved
   snow tend to sit beside a faster CPU (a V20 and up) that 80 columns
   wants anyway.
2. **Figure detail by distance.** Measured on `perf-overall` and not
   committed: markers below `fv8` 16 (~11 m) give P 40 = 17.8 fps on that
   branch (from its 16.3); below 24 (~7 m), 18.2. `perf-figs` built
   five straight limbs below `fv8` 20 and rejected it: mid-range figures lose
   the legs and arm that make them read as men.
3. **A kernel slot returning the key-state map** would save ~0.4 ms a frame.
   It is an SDK/ABI change. *(Since built: `OSAPI_KEY_MAP`, which the game
   calls once a frame.)*

**Next, by the agents' estimates:**

- Per-span glue is now the largest single cost, ~11% of a frame. `wa_span`
  could read the vertex cache directly, ~1.3% (both `perf-geom` and
  `perf-spans` name it).
- Rounded 16-bit product high words, ~1.4%. Not byte-identical.
- The room's clipped walls, ~3 ms.
- The joints' quarter-step blend for small figures, ~0.4 ms.

### 16.9 Performance pass 6 — the engine looked at whole, then five areas (2026-11-14)

The owner: *"Do a performance pass on wire arena. Look into the overall
engine: what could be done differently and come out cheaper? Then study each
individual component, and optimize it. The goal is to generally look and
have the same featureset, not to be identical. Bring any major look or
feature changes to me. Small optimizations can add up."*

**What was new:** the picture no longer had to be byte-identical, which the
five rounds before were held to. It still had to look the same.

**How it was run.** A coordinator studied the frame whole, built three
instruments (below, now in `tools/`), and took the floor grid and present.
Five agents each took one area in a worktree of its own, from `f0707c6c`:
wall geometry, spans and columns, figures and things, the low boxes
(`wrlow.inc`), and the game (`wirearena.asm`, `walink.inc`). Their branches
were merged one at a time, each measured on the merged tree. Another
session was pushing four-player and UDP link work to the branch throughout;
it was merged in seven times.

**The instruments** (each a development instrument, not a suite row):

- **`tools/waposes.py`: posed frames.** A fixed, seeded set of poses. The
  camera and the three figures are written into the game's BSS at the
  renderer's door, with the world held as a bench's (every pickup present,
  the bots dummies). Per pose it records the text screen, the guest cycles
  of the render, and a divide fault if one happened. A render a timer tick
  landed in is run again, so a set's mean repeats to about 0.002%.
  `sets` records the eight reference sets; `cmpsets` compares two
  recordings.
- **`tools/wabench.py`:** the game's own benches, read off the HUD.
- **`tools/waprof.py`:** exclusive guest cycles per routine over whole
  frames of a bench or of live play, by single-stepping.

**Measured, merged** (MartyPC `os8088_5150_cga`, Medium, FOV 75; "before"
is `f0707c6c`):

| | before | after |
|---|---|---|
| P bench, 40 columns | 20.0 fps (MS 50.0, R 42.3, P 5.2) | **26.3** (MS 38.0, R 31.7, P 4.2) |
| P bench, 80 columns | 16.6 | **22.3** |
| B bench, 40 columns | 33.7 | **40.9** |
| heights 40, A / B / C / D / E | 13.1 / 14.7 / 32.7 / 12.4 / 25.8 | **15.5 / 17.0 / 43.2 / 15.0 / 35.4** |
| heights 80, A / B / C / D / E | 10.1 / 10.4 / 25.6 / 9.5 / 21.2 | **12.2 / 12.3 / 32.1 / 11.5 / 29.4** |
| P bench at High (the grid), 40 columns | 14.4 | 16.7 with the grid's rewrite alone; not re-taken merged |

By the profiler, a P-bench frame went from 231,288 to 169,124 clk (−26.9%)
and heights view A from 357,156 to 304,478 (−14.8%).

Posed sets, render clk a pose, a MOVED view (the renderer now skips work
while the view stands; a still view gains about ten points more):

| set | before | after | |
|---|---|---|---|
| full arena, 40 columns | 154,340 | 123,671 | −19.9% |
| full arena, 80 columns | 183,945 | 144,808 | −21.3% |
| open arena, 40 columns | 132,036 | 104,683 | −20.7% |
| heights, 40 columns | 192,866 | 156,696 | −18.8% |
| heights, 80 columns | 246,477 | 197,238 | −20.0% |
| full arena, Very High | 206,302 | 151,560 | −26.5% |
| heights, Very High | 265,838 | 198,391 | −25.4% |
| heights, Very Low | 190,079 | 152,836 | −19.6% |

**Live play** is where the game's own work shows; the benches hide it,
because their bots are dummies. On one seeded heights match (the game
agent's measurement, its branch alone): the sim's tick 16,414 → 11,397 clk
walking and 13,256 → 9,415 with nobody at the keys; an overlay rebuild
18,691 → 5,257 clk with the pistol; the frame 398,979 → 369,653 clk
(11.96 → 12.91 fps), the game's state identical tick for tick.

**What the engine study found.** After five rounds no stage dominated, and
the frame was instruction count rather than arithmetic: about 12,500
instructions and about 100 multiplies and divides. The changes that
removed work rather than shaved it:

- **A clipped face needs no clip** (geometry). Every wall is axis-aligned,
  so along a face the inverse depth is linear in the screen column and
  depends only on the face's plane. A face's end outside the view is made
  AT THE SCREEN'S EDGE with two multiplies, where `wa_clip` and `wa_proj`
  took up to three interpolations and two projections. The room's walls,
  which are nearly always clipped, then take the cheap in-view path. The
  old clip is the fallback when the plane is inside the near plane (5 of 40
  against-the-wall poses).
- **A box's silhouette instead of a cone test** (geometry). The order list
  names each box's three silhouette vertices; a box is kept or dropped by
  those vertices' columns, exactly, at any field of view, and a dropped box
  leaves a hint that proves it still out next frame with one corner's test.
  `wa_leaf`, `wa_box` and the cone are gone.
- **A view that stands is not transformed again** (geometry). Products,
  kept boxes' vertices and the room's corners stand while the position and
  angle do: 15–20% of the P bench's frames.
- **A span whose edges are both level is one loop** (columns): no divide, no
  fraction, no second pass — 6 of 10 spans a frame at a COARSE level. A
  steep span drops an edge that has left the screen for good; a pass ends
  when its edge leaves the screen.
- **A figure's box before its joints** (figures), from two host tables, so a
  figure off the screen or behind walls in every column makes no joint (17%
  of the figures in the cone); occlusion verdicts go by runs of columns
  under one span; the emit sweep asks nothing about depth.
- **Low boxes' shallow edges without a divide, walked in runs** (low); the
  per-box prelude cut; an edge under the eye and under the screen leaves
  before its clip.
- **The floor under a square by a 16 × 16 grid of cells**, not a walk over
  every box (game): `wa_floor` 1,419 → 508 clk a call, and it was 30% of a
  tick. The overlay is built straight into the renderer part.
- **The floor grid as running sums** (coordinator): no divide and one
  multiply a dot where there were a divide and three; the lines behind the
  player are not walked. What the grid adds to a Medium frame fell from
  about 51,000 to about 24,000 clk on the posed sets.
- **Present writes a listed cell without reading the glass first**
  (coordinator): two cells in three have changed in play, so the compare
  cost more than it saved, and the card sees fewer bus cycles.

**The picture.** Over the eight reference sets (350 poses):

- **Figures, pickups and the overlay are identical** in every pose.
- **Wall edges** differ in 121 cells of 60 poses on the full arena at 40
  columns (66 of 60 on heights): a wall running off the screen takes its
  half-row steps a column earlier or later, because its end is now exact at
  the screen's edge, and a low box's shallow edge can land a half-row
  differently. No wall, post or box is missing.
- **`tools/wacheck.py`** (edges against a host ray cast) says the edges are
  as right as before or a little more: 5 of 30 poses with an edge two rows
  off on the full arena (5 before); 16 of 1,460 columns off on heights at 40
  columns (20 of 1,458 before); 19 of 2,253 at 80 (26 of 2,293).
- **The grid's dots moved, and that is a fix.** The old grid kept x/z ×
  65,536 in 16 signed bits, so a dot more than half way from the centre to
  the edge of a 90-degree view wrapped to the other side of the screen: 61
  of 180 dots in the posed set were in the wrong cell. All 162 are now where
  the projection puts them.
- **The HUD's `N` counts the writes made** (every listed cell and blank),
  not the cells that changed: about 148 where it read 95 on the P bench.
- 0 divide faults in every set, the against-the-wall sets (530 poses) and
  the agents' larger private runs.

**Checked.** The twelve WireArena suite rows (`walift`, `waroutes`,
`wabots`, `wascore`, `wabotarms`, `waweapons`, `walink`, `walinknet`,
`walinkudp`, `walinkip`, `walink4`, `walink4ip`) pass on the merged tree.
`waweapons` caught one bug during the pass, in the game agent's own branch
(a pickup's early return that took "every pickup present" for "no timer
running"), fixed there before the merge.

**Three things the merge had to settle between areas:**

- The geometry pass makes the level's choices only when the level changes;
  the columns' `wa_collook` hangs off that, and reads the sparse key too, so
  the sparse key became a second change key (`wa_kspa`). *(Both gone since
  2026-11-15: WIREARENA-NET-PLAN 9.10.)*
- The columns' steep way reads a span's ends back off the face's vertices
  (`wa_fv`); a face with an end at the screen's edge keeps it in a scratch
  vertex, so `wa_face` now leaves the ends the span HAS in `wa_fv`.
- The figures pass folded the depth queries into one (`wa_occl`), which the
  grid now calls; its own helper is gone.

**Bytes.** The renderer part 61,780 → 62,071 of 65,024 (+291, after 320
bytes of BSS nothing read and an uncalled routine came out). The game's
image 20,532 → 21,258 and its BSS +6,472 (the floor grids) on the game
agent's branch, in a segment with room (34,614 of 65,024 there; the
four-player link merged since has grown the image to 25,975).

**Tried and rejected, by measurement** (the agents' reports have each
number): whole-box culls for low boxes, a fourth time, in four forms;
run-form flat passes; `wa_fedge`'s start with fewer bits (the staircase
would shimmer); 16-bit product chains; lazy products; emitting figure cells
as the limbs are walked; a cross-multiplied depth compare; the player's
step cached by angle.

**The owner's rulings and what is still the owner's:**

- **FARTOP stays at Very Low alone** (the owner, 2026-11-14): it removes
  perspective, notably half of a ramp seen from the side — "most speed,
  least recognizable geometry". Not to be proposed for Medium again.
- The rest of the list, and what the owner made of each, is 16.10.

**Where a frame goes now** (P bench, 40 columns, 169,124 clk): the level
spans' loop 16%, `wa_face` 12%, present 11%, the frame's fixed setup 7.5%,
figures about 18%, vertices 6%, products 3.6%, the game about 6%. On
heights view A (304,478 clk) the low boxes are still about a third.

### 16.10 The owner's rulings on pass 6's open list, and what was built (2026-11-14)

| 16.9 raised | the owner | so |
|---|---|---|
| far figures as markers sooner; five-limb figures at closer range | "We tried a variant of these earlier and they didn't work very well" | **declined**; not to be raised again |
| a wider COARSE (edges moving under 1.5 or 2 half-rows drawn level) | "Lets put it on low and very low" | **built**: `LK_COARSE2` |
| roaming bots look every 16th tick, not every 8th | "This seems good" | **built** |
| the link's cable read skipped on the frame after an empty one | "I'd need to test this, keep it on the list, but not yet" | **open**, not built |
| the meter's R and P sampled one frame in four | "Lets put the entire fps/render line on a key, F10; nothing shown or sampled by default" | **built**: the meter is off until asked for |
| a floor-eye PVS in which low boxes occlude | "data in the map that tells us ahead of time what you can't see? That is a great optimization" | **built**: the PVS's low pass |
| FARTOP at Medium | no: "most speed, least recognizable geometry" | stays at **Very Low alone** |

**The meter on F10.** Row 24 shows nothing as shipped *(since 2026-11-15
it shows the weapons and the last kill while the meter is off, and every
number replaces them: WIREARENA-NET-PLAN 9.6)*. A frame sums nothing
and reads the clock once, for the sim. F10 cycles: the frame rate alone
(`FPS nnn.n`), every number (the row as it was: fps, frame, render and
present ms, video writes, and the view, level, sparse and grid marks; three
clock reads a frame), off. The B and P benches turn every number on
themselves, because their result is that row. *(Since 2026-11-15 the
sparse mark is gone and the benches are a `WABENCH=1` build's.)* `make wirearenadisk
WAMETER=1|2` builds a game that starts with it on (a stamped knob, for a
benchmark build). `tests/waweapons.py` checks the cycle from off.

**`LK_COARSE2`**, `wa_look`'s bit 7, on Low and Very Low: with COARSE, an
edge that moves under TWO half-rows along a wall (seven quarter half-rows
or fewer) is drawn level at its middle, where COARSE alone takes under one.
Measured on the posed sets, render clk a pose: Low, the full arena −2.0%
and heights −0.6%; Very Low −0.3% and −0.2% (its room has no tops
already). Medium and up are identical. A long shallow wall line loses its
single step: 383 cells differ in 22 of 60 Low poses on the full arena.
`WR_ABI` is 7. Very Low's `wa_look` is now 0xFF, so the level's change key
is a word with the sparse key, and "none" is 0xFF in that half. *(The
sparse key is gone since `WR_ABI` 13: WIREARENA-NET-PLAN 9.10.)*

**Roaming bots** look every other turn, 16 ticks apart; engaged, hunting
and fetching bots look every eighth tick as before. A sight walk and a
bearing fewer for each roaming bot: up to about 350 clk a tick by the game
agent's figures (not re-measured).

**What it costs in notice, corrected.** 16.9 said "up to 0.27 s later".
That is the AVERAGE. A roaming bot's looks take turns between the player
and the next live bot, so it looked at the player every 16 ticks and now
does every 32: 0.27 s later on average, 0.53 s at worst (1.07 s between
looks at the player, from 0.53). `tests/wabots.py` held a roaming bot to
"engaged within 12 ticks" and failed 4 runs in 11 until its wait was 40;
without the change it passed 6 of 6.

**The PVS's low pass** (`tools/arenagen.py`, `pvs_low`). The PVS of
HEIGHTS-PLAN 9.8 lets only TALL boxes hide anything, because an eye can see
over a low one. How much it sees over depends on the eye's height, and that
is bounded per cell:

- The boxes are a heightfield, and over a heightfield a higher eye sees
  everything a lower one does. So one test per cell, at the HIGHEST eye a
  player can have there, covers every eye.
- That eye (`pvs_eyes`) is the eye over the highest floor in reach of the
  cell, a jump more, or what is left of a jump off a higher floor some
  cells away by the time its fall has covered the cells between, by the
  game's own gravity, jump and speed out of `waconst.inc`.
- From each of the cell's test points, rays are cast across the box's whole
  angle. Along a ray the box's top is hidden when a nearer box's top stands
  at least as steep from the eye.
- Only boxes below the tallest are looked for; a tall box stays the flat
  test's, margins and all, and a tall box in the way hides with those same
  margins. The low test itself has NO margin: it is the geometry's answer,
  so that a ramp whose top edge runs along the platform's (equal, in exact
  arithmetic) is dropped.

On the heights arena it drops each ramp from the 38 cells where it lies
behind the platform: 2.3% of box-cells more, 33.7% in all. It costs 1.7 s
of build time, nothing at run time and no byte (the table's size is
unchanged). `WA_PVSLOW=0` in the environment builds without it, for the A/B.

Measured (MartyPC `os8088_5150_cga`, Medium):

| | before | with the low pass |
|---|---|---|
| heights view A, 40 / 80 columns | 15.5 / 12.2 fps | **16.2 / 12.7** |
| the other four views | | unchanged |
| 900 poses in the culled cells, turned toward the culled ramp, the eye standing, jumping and at the cell's highest; render clk a pose | 232,602 | 218,084 (−6.2%) |
| the same, 400 poses at 80 columns | 282,031 | 266,862 (−5.4%) |

**Its picture:** of those 900 poses 873 are identical with and without it;
27 differ by one or two cells (29 in all; 11 of 400 at 80 columns, 16
cells). Each is a `_` the hidden ramp's top edge used to leave on the
platform's far edge line, where rounding had let one cell of it through;
the line is now the platform's own. 0 divide faults.

**Checked:** the twelve WireArena suite rows on the final build (below);
`waweapons` goes red with the level table's bit or F10's cycle changed.

**Still open** (carried to 17.3)**:** the link's cable read (above); the hints of 16.9's agents
that were interface changes between areas and not taken (a byte-wide
`wa_fbuf`, `wa_face` handing its columns to the span code in registers, a
"clip raised" flag for the tall spans on heights).

## 17. Rulings, work in progress, and what is open (2026-11-15)

The plans that built WireArena were swept on 2026-11-15 and filed under
`docs/plans/completed/` (17.5). Everything they still had open is here, with
a pointer to where it came from, after the owner's rulings below.

### 17.1 The owner's rulings (2026-11-15)

- **Settings surviving a restart: not wanted.** A player should not reopen
  into the last match's settings, and saving costs time. The setup's
  settings, the nick and the typed address are in-session only
  (WIREARENA-NET-PLAN 2 and N1, annotated there).
- **Joining a match in progress** (NET-PLAN N6) **and evicting a bot** for a
  joiner: deferred until a hosted server browser exists.
- **Two players on one system: dropped** from every plan. It would need a
  286 or better, which is unlikely to carry a Hercules card, and the game
  plays best on CGA anyway.
- **Jump pads and teleporters: not in this release**, since no arena is
  built around them. Possibly the next release, or a single-player campaign
  reusing the engine, where teleporting between areas makes levels longer
  with no work on the old areas.
- **A double or quad damage power-up:** maybe the next round (untuned).
- **Closed, not parked:**
  - **footsteps**: tried and removed. They do not work on a PC speaker,
    there is no directional sound, and too much noise is just noise;
  - **the other players' held weapons drawn on their figures**: tried in an
    early prototype and thrown away - seen only up close, and it costs
    performance (WIREARENA-LINK-PLAN 6's "Its weapon is not drawn" stays
    so);
  - **a hit marker**: not needed - the struck figure already flashes red.
- **kern_mega** is another session's now: its linked match, SERIAL.DRV's
  verbs run there and its soak rows are that session's (17.3 lists them
  for the hand-over).
- **The field run on the 5150** is the owner's, at home, after the arena
  work (17.3).

### 17.2 The release round (2026-11-15)

Built, on the owner's list (`apps/wirearena/wirearena.asm` unless named):

- [x] **Spawn protection** (`WA_PROTT`, 60 ticks): for 2 s after a spawn
  or a respawn - or to its first shot - nothing hurts the figure. The
  player's (`wa_pprot`) is judged in `wa_phurt`, which a linked player's
  damage reaches on its own machine, so it holds linked too; its heart is
  cyan while it lasts. Each bot's (`wa_bprot`) in `wa_hurt`. The link
  rows clear it on a player they set up to be shot.
- [x] **The score against the goal** on the HUD row, five cells left of
  the weapon digits: `12/20`, or the score alone with no Win at;
  free-for-all our kills, white; a team mode our team's in its colour
  (`wa_hudsc`, written when it changed).
- [x] **Pause**: P, offline only - the main loop owes no ticks, the mouse
  and the trigger are drained, PAUSED over the view. (In a `WABENCH=1`
  build P is the play bench.)
- [x] **Mouse sensitivity**: `[` and `]`, nine levels about the factors the
  game shipped with (yaw 96 a count, pitch 1), the session's (persistence
  declined, 17.1); "Mouse sensitivity N" on the HUD's message.
- [x] **Q and E**: the weapon before and after, of those had and loaded
  (`wa_wcycle`, through `wa_wsel`'s own test).
- [x] **Esc in a linked match asks**: an outlined box, its frame white and
  the question red - "Leave the match? Y/N" - Y leaves, N or Esc stays;
  nothing else typed is taken while it is up. Offline Esc leaves at once.
  `walink` and `walink4` press Y after their Esc.
- [x] **The hit's direction, a PROTOTYPE for the owner to try**: hurt by a
  figure that is OFF the screen (outside the view's 37.5 degrees either
  side), a red arrow left or right of the crosshair, or under it from
  behind, for 20 ticks (`wa_hitdir`, off `wa_bear`). In view: nothing.
- The About card gains "Q/E: weapon. [/]: mouse. P: pause." (13 lines).

Checked on a MartyPC 5150 CGA: the protection's countdown and heart,
`0/20` on row 24, E E E Q Q stepping 1 2 3 2 1, `]` and `[`, P stopping
the ticks (0 a second, 31 again after), the box drawn and N and Y, and all
three arrows - the player turned to keep the nearest bot behind: 52 hits,
each cue seen.

- [x] **WireArena on the shipped disks** (the Makefile's `WA_SHIP`):
  - at 360KB, `games2-360.img`, the second games disk (SPEC.md 24.6), the
    package at its root, 71 of 354 clusters. It would have fitted
    `games360.img` with one cluster spare, and a game disk at its last
    cluster cannot save a high score;
  - `GAMES/` on the 1.44MB and 1.2MB apps disks (147KB and 152KB free
    before it) and on `more720.img` (126KB; SPEC.md 24.7). `apps720.img`,
    with 25KB free, leaves it off;
  - the live media through `$(ALLAPPSARGS)`;
  - off `apps/RETIRED.txt`'s instruments;
  - the gates that walk the shipped images (`t_image`, `t_diskverify`,
    `t_catdisk`, `t_retired`, `t_noovl`, `t_stik`, `abicompat`) and the
    release zip (`360K/GAMES2.IMG`) know the new disk;
  - kern_mega's apps disks are the kern_mega session's to place. Another session builds
the Ziggurat and the Pit
([WIREARENA-THREE-ARENAS-PLAN](WIREARENA-THREE-ARENAS-PLAN.md)).

### 17.3 Open, after the rulings

**The game and its contract**

- **WireArena has no SPEC.md section.** Its contract is these plans,
  most now in `completed/`; a section of its own (the frame block, the
  arena part, the link's frames, the setup) is owed before or at the
  release. (CLAUDE.md, *Building an app or game in waves*, step 3.)
- **Keyboard turn acceleration**: the arrows turn at one fixed rate. The
  owner asked for it (AI-PLAN 1, "Adjust it"); mouse sensitivity (17.2) is
  the other half. ([AI-PLAN](completed/WIREARENA-AI-PLAN.md) 1, 1.1)
- **The heights plan's interpretations** of the owner's placements (the
  lift platform as the tower's top, the back tunnel) were never confirmed.
  ([WEAPONS-PLAN](completed/WIREARENA-WEAPONS-PLAN.md) 3.4)
- **The detail levels**, for the owner to re-review from the videos;
  moving an option is one line of `wa_levtab`.
  ([HEIGHTS-PLAN](completed/WIREARENA-HEIGHTS-PLAN.md) 9.3 item 6)
- **The room corners' views** were to be named to the owner beside the
  tower's; probably moot since performance pass 6, to close or drop.
  ([HEIGHTS-PLAN](completed/WIREARENA-HEIGHTS-PLAN.md) 9.1, 9.3 item 3)
- **The divide-fault panel** in the shipped kernels: the owner's call, and
  the kernel's rather than WireArena's.
  ([HEIGHTS-PLAN](completed/WIREARENA-HEIGHTS-PLAN.md) 9.3 item 5, 7.1.2)

**The link**

- **The cable read skipped on the frame after an empty one**: "keep it on
  the list, but not yet". (16.10)
- **WireFleet onto `apps/os88cable.inc`**, so the copy does not stay two.
  ([LINK-PLAN](completed/WIREARENA-LINK-PLAN.md) L3.1)
- **The 86Box XT pair** at 2400 baud, for the byte loss QEMU cannot show:
  `tools/wireball-pair.py --game wirearena` exists, no result is recorded.
  ([LINK-PLAN](completed/WIREARENA-LINK-PLAN.md) L3.2)
- **What the card costs on an 8088** (raw and UDP), unmeasured: MartyPC has
  no NIC, so `vm/xt-wire` or the iron.
  ([LINK-PLAN](completed/WIREARENA-LINK-PLAN.md) 12.5,
  [NET-PLAN](completed/WIREARENA-NET-PLAN.md) 8)

**Rows not yet written**

- capture the flag with four players on UDP
  ([CTF-PLAN](completed/WIREARENA-CTF-PLAN.md) 11.3);
- free-for-all's and team deathmatch's linked wins
  ([MODES-PLAN](completed/WIREARENA-MODES-PLAN.md) 9, M4);
- the setup's buttons in `tests/btnall.py`
  ([NET-PLAN](completed/WIREARENA-NET-PLAN.md) 9.1);
- the ramp walks of HEIGHTS-PLAN 9.7.3, still scratch scripts
  ([HEIGHTS-PLAN](completed/WIREARENA-HEIGHTS-PLAN.md) 9.12);
- a launch row, a refusal row (too little memory) and a move row
  (`tests/pkgmove.py`'s pattern) for the parts
  ([PARTS-PLAN](completed/WIREARENA-PARTS-PLAN.md) 4.2);
- `tools/wacheck.py`'s host reference still leaves lifts out
  ([HEIGHTS-PLAN](completed/WIREARENA-HEIGHTS-PLAN.md) 9.12).

**For the kern_mega session** (17.1): a linked match on kern_mega and
SERIAL.DRV's verbs run there ([LINK-PLAN](completed/WIREARENA-LINK-PLAN.md)
L3.3, 11.2); kern_mega soak rows of WireArena's own
([AI-PLAN](completed/WIREARENA-AI-PLAN.md) 1, 1.1;
[PARTS-PLAN](completed/WIREARENA-PARTS-PLAN.md) 4.2's kern_mega row); the
umbrella's Wave 6 "kern_mega rows" (15).

**The owner's, after the arena work** (17.1): the field run on 5150 #1, the
CGA questions and the PERFORMANCE.md set that follows from it (15, Wave 7).

**Renderer levers, measured or estimated and not taken** (internal; each
under a few percent):

- 16.9's interface hints between areas: a byte-wide `wa_fbuf`, `wa_face`
  handing its columns over in registers, a "clip raised" flag (16.10);
- the frame block's version 2, ~0.25%; the hot keyframes into the pose
  part for 4 KB at ~1% of render; a smaller `wa_rcp`
  ([PARTS-PLAN](completed/WIREARENA-PARTS-PLAN.md) 8.2, 8.4, 8.6);
- a low box whose top is above the eye drawn as FACES - asked for, and 7
  built the shut test instead ([HEDGES-PLAN](completed/WIREARENA-HEDGES-PLAN.md)
  4.1, 7);
- a pickup's single occlusion verdict, the overlay's cost, the PVS margin
  by the annex ([WEAPONS-PLAN](completed/WIREARENA-WEAPONS-PLAN.md) 4);
- posts, a culled pass, shut runs in `FLATPASS`; merged edges; a 64-unit
  PVS ([HEIGHTS-PLAN](completed/WIREARENA-HEIGHTS-PLAN.md) 9.7.8, 9.8).

### 17.4 After 1.0: ideas

None is promised; each is where it was proposed.

- **Joining a match in progress** and **evicting a bot** for a joiner, once
  a hosted browser exists (17.1; [NET-PLAN](completed/WIREARENA-NET-PLAN.md)
  9.4 has the estimate, ~1 KB).
- **A central server browser** on os8088.com (NET-PLAN 1).
- **Host migration**: today the host leaving ends the match (NET-PLAN 5).
- **More than four participants**, "a 286+ thing later" (AI-PLAN 7.1).
- **Jump pads and teleporters**, the next release or a single-player
  campaign (17.1; AI-PLAN 1, 13.6).
- **A double or quad damage power-up** (17.1).
- **Last stand**: waves of bots, solo - the owner's "yes to all" modes
  (AI-PLAN 1, 1.1; CTF-PLAN 1; 13.5). **One-flag capture** (13.5).
- **A match time limit** beside Win at's score (AI-PLAN 5).
- **Replay, kill-cam and demo files** off the fixed-step sim (AI-PLAN 1;
  13.2).
- **A radar strip** in the HUD row, a prototype first; the Tab minimap is a
  plan of the arena, not a radar (AI-PLAN 1; 13.7).
- **More sounds** (a jump, a pickup, a death; footsteps are closed, 17.1),
  and FM effects on a Sound Blaster or AdLib (AI-PLAN 1.1; 10).
- **More animations**: strafe, jump and land, shoot, a flinch from behind,
  a fall that follows the shot (5).
- **More weapons**: plasma, a slow dodgeable bolt (6); proximity mines
  (WEAPONS-PLAN 5); armour (6).
- **An os8088-to-os8088 parallel cable**, the one link 5150 #1 can use
  without giving up its mouse (8.4).
- **Larger, multi-area levels** with an area-to-area visibility cut
  (HEIGHTS-PLAN 7.1, 9.8 item 6), and a door on the heights arena's back
  room for feel (HEIGHTS-PLAN 9.7.8).
- **Page flipping on a 286**, only if it makes the game faster
  (PARTS-PLAN 6, Q4).

### 17.5 Where the plans went

| plan | what it built | now |
|---|---|---|
| [WIREARENA-PARTS-PLAN](completed/WIREARENA-PARTS-PLAN.md) | the renderer part, the pose part, an arena a lazy part | completed |
| [WIREARENA-HEIGHTS-PLAN](completed/WIREARENA-HEIGHTS-PLAN.md) | stairs, platforms, lifts, the heights arena, the PVS, the detail levels | completed |
| [WIREARENA-WEAPONS-PLAN](completed/WIREARENA-WEAPONS-PLAN.md) | six weapons, pickups, weapon spawns, the deaths | completed |
| [WIREARENA-AI-PLAN](completed/WIREARENA-AI-PLAN.md) | bots: routes, sight, shooting, death and respawn, Tab's board | completed |
| [WIREARENA-LINK-PLAN](completed/WIREARENA-LINK-PLAN.md) | two machines over a cable (protocol v1), then raw Ethernet | completed |
| [WIREARENA-NET-PLAN](completed/WIREARENA-NET-PLAN.md) | UDP, up to four, the match setup window, the nick, the minimap | completed |
| [WIREARENA-ARENAS-PLAN](completed/WIREARENA-ARENAS-PLAN.md) | the Twin (the Maze, since retired), floor lines, several lifts | completed |
| [WIREARENA-HEDGES-PLAN](completed/WIREARENA-HEDGES-PLAN.md) | the hedge, 63 boxes, a box standing on a box | completed |
| [WIREARENA-CTF-PLAN](completed/WIREARENA-CTF-PLAN.md) | teams, side spawns, capture the flag | completed |
| [WIREARENA-MODES-PLAN](completed/WIREARENA-MODES-PLAN.md) | Win at, team deathmatch, king of the hill, the rotation | completed |
| [WIREARENA-THREE-ARENAS-PLAN](WIREARENA-THREE-ARENAS-PLAN.md) | Crossing built; the Ziggurat and the Pit | **in progress** |

# Stickio

An original black and white platform adventure for os8088. Thirty courses in
six worlds introduce brick steps, pits, question blocks, one-way ledges,
spring pads, spikes, and walking, hopping and flying enemies. Every course has
a midway checkpoint and an exit flag. Courses grow from 1,024 to 2,560 pixels.

The [Super Mario Bros inspired feature gap analysis and development plan](../../docs/plans/STICKIO-SMB-PLAN.md)
records the implemented P0 foundations, P1 camera, speed-sensitive jumps and braking/skid,
and remaining gameplay and course improvements.

Build and launch:

```
make stickiodisk
make run RUNAPPS=build/stickio360.img
```

Open drive B: and double-click **STICKIO.O88**. Select a starting course with
left/right, then press Enter, F or Alt+Enter. Stickio is also included in the
standard games disk and larger application disks.

| Key | Action |
|---|---|
| Left/right or A/D | Move; reverse to brake and skid on the ground |
| Z | Jump; movement speed adds height, early release shortens it |
| Shift or X | Run |
| P | Pause/resume |
| M | Toggle sound |
| R | Retry at the checkpoint, spending one life |
| Esc or Alt+Enter | Return to the desktop |
| Enter after a clear | Next course |
| Enter after game over/ending | Start a new adventure |

Jump on enemies to defeat them; contact from the side costs a life. Question
blocks give a coin when struck from below. Coins are worth ten points, enemies
25, course clears 100. Every fifty coins earn another life, up to nine. Five
lives start a session. The flag halfway through a course becomes the respawn
point. Progress lasts for the current fullscreen session; starting from the
launcher starts a fresh session at the selected course.

Full jumps rise about 53 pixels from rest, 57 at walking speed, and 62 at running
speed. Build speed before pressing Z; holding the run key at rest still gives
the standing jump. Releasing Z early keeps the jump short. Steering or reversing
in the air retains the launch profile. Jump buffering and coyote time still last
five simulation steps, and springs and stomp bounces keep their original strength.

Reversing on the ground brakes twice as quickly as air steering, with a braced
skid pose while momentum carries you the old way. A full-speed run stops in
eight simulation steps; a walk stops in five. Keep holding the new direction
to accelerate back the other way after stopping. Releasing movement uses the
same passive friction as before.

Retries keep score and coins, restore the checkpoint's explicit safe x/y, reset
enemies, and give ninety simulation steps of protection. Collected coins and
question-block rewards stay consumed; each enemy's score pays once per course,
even when the enemy returns on retry. A new course or fullscreen session resets
the reward ledger. Walkers follow terrain and fall from edges; hoppers turn at
unsupported edges and jump from the height they stand on. Flyers patrol relative
to their authored height.

All art, level layouts and music are original. The figure has eight running
poses with articulated elbows and knees, a weight-bearing idle, ascent, descent
and bent-knee landing recoil and skid poses. Left-facing poses are compiled mirrors. A support foot moves backward through stance while the opposite knee swings
forward; the head dips on recoil and rises during passing. Gait
phase follows distance traveled; runtime code only composites 16x24 bitmaps.
`make stickio-art` produces a pose sheet, animated gait preview and WAV auditions
of the six world themes in `build/stickio-art/`. Pillow is needed only for previews.
The normal build uses Python's standard library and NASM.

P0 authoring keeps the thirty-course campaign and adds a version-one JSON fixture
at `levels/p0-first-room.json`. It demonstrates a raised walker, lower hopper,
supported arrivals and named sections. `tools/stickio_courses.py` checks tile
materials, RLE coverage, stable object/reward IDs, supported protected arrivals,
and the twenty-record/six-nearby-actor budget. Unsupported room links and later
mechanics are rejected. The compiler writes `manifest.json`, validated fixture
data and a collision-layout SVG into `build/stickio-art/`. `const.inc` names the
player physics in 8.8 units and simulation steps. This fixture is a
foundation example; the first-world course redesign remains later work.

The XT path uses CGA 320x200 with only black and white. VGA uses the same CGA
mode. Hercules doubles both axes to a 640x256 viewport within 720x348. A tile
column cache retains identical ground and sky while scrolling. Terrain and
sprites are composed in RAM; saved sprite footprints restore the terrain in
reverse order, including overlaps. Changed row spans are transferred at vertical
retrace, so the display never sees a separate sprite erase pass. There is no heap
claim for graphics, full-frame copy, floating point or runtime pixel rasterizer. The
54.62 Hz simulation clock is separate from rendering, with at most four owed
steps per frame. The small kernel uses three simulation steps per ordinary tick.
See PERFORMANCE.md Set 156 for measured scrolling rates on the 4.77 MHz emulator.
Rendering quantizes horizontal positions to four pixels; collisions retain
fractional movement and the camera can scroll both directions.
The camera stays still while the player origin is 104–136 pixels from the left
edge, then follows the crossed edge in four-pixel increments. This leaves more
room to see the course ahead and allows short reversals without scrolling.
Course entry and checkpoint retries reset the view around the safe arrival;
the viewport always stops at the course boundaries.

Sound follows the OS sound route. The PC speaker plays the lead melody and
priority effects. AdLib and Sound Blaster play a three-voice OPL2 arrangement
plus an independent effect voice. Sound Blaster adds precomputed 8 kHz digital
effects through a nonblocking DMA stream. Music has six 32-note themes; jump,
coin, hurt, stomp and clear/checkpoint effects each have tonal and PCM versions.
Failed sound claims fall back gracefully. No blocking speaker PCM, guest mixer,
private sound IRQ or direct sound-port writes are used.

Validation:

```
make stickiocheck
python3 tests/stickio.py --adapter cga
python3 tests/stickio.py --adapter herc
python3 tests/stickio.py --adapter vga
python3 tests/stickio_display.py
python3 tests/stickio.py --sound speaker
python3 tests/stickio.py --sound adlib
python3 tests/stickio.py --sound sb
```

The guest checks unpack all 30 courses, compare terrain pixels and incremental
scrolling with a separate renderer, exercise real physics/state transitions and
keyboard delivery (arrows never jump; held and tapped Z do), and record 4.77 MHz
frame work costs. They also check CGA/VGA's rendered framebuffer and verify that
video memory retains the old frame until RAM composition is complete. Captures
and measured results go into `build/stickio-proof/`. Sound checks exercise all six themes and
five effects on the actual emulated devices and check mute/pause/release. These
are emulator results; real XT wait states, monitor behavior and listening on real
sound cards still need a hardware run. The levels use authored obstacle motifs;
a full human playthrough of all thirty courses remains useful for difficulty tuning.

P0 checks also compare 365 steps against an independent integer player model,
including reversal, tap/held jumps, buffering/coyote departure, walls, ceilings,
one-way ledges and springs. The actor/retry matrix checks different terrain
heights, wall/edge policies, one-way passage, pit retirement, invisible boundary
contacts, runtime overflow and one-time payouts. Compiler checks include invalid
content and a deterministic authored-fixture assembly. Size, live frame periods,
simulation-time ratio and catch-up loss are recorded in `build/stickio-proof/`;
these traces do not establish whole-course reachability or human playability.
P1 camera checks sweep every legal room width on the host, compare threshold
rounding and 97 running/reversal steps in the guest, and verify camera-driven
scrolling pixels on all three adapters, including room ends and load/retry resets.
P1 jump checks compare 837 steps per adapter, covering both signs at the speed thresholds, full/tap envelopes,
profile retention during reversal, buffered landings and coyote launches,
gravity phases and fall cap, ceilings, walls, one-way support, springs, stomps
and retry resets. Host envelopes are saved in `build/stickio-proof/p1-jump-envelopes.json`.
P1 braking adds 392 signed stop/clamp and ground/air transition steps, including
jump launches, landings, edge departure, walls and neutral input. Skid checks
exercise both mirrored poses, priority over landing recoil, cancellation after
stopping or jumping, and 28 incremental framebuffer checks on every adapter.

# Stickio

An original black and white platform adventure for os8088. Thirty courses in
six worlds introduce brick steps, pits, question blocks, one-way ledges,
spring pads, spikes, and walking, hopping and flying enemies. Every course has
a midway checkpoint and an exit flag. Courses grow from 1,024 to 2,560 pixels.

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
| Left/right or A/D | Move, with acceleration and friction |
| Z | Jump; release early for a shorter jump |
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

All art, level layouts and music are original. The figure has eight running
poses with articulated elbows and knees, a weight-bearing idle, ascent, descent
and a bent-knee landing recoil. Left-facing poses are compiled mirrors. A support foot moves backward through stance while the opposite knee swings
forward; the head dips on recoil and rises during passing. Gait
phase follows distance traveled; runtime code only composites 16x24 bitmaps.
`make stickio-art` produces a pose sheet, animated gait preview and WAV auditions
of the six world themes in `build/stickio-art/`. Pillow is needed only for previews.
The normal build uses Python's standard library and NASM.

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

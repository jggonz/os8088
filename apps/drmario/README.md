# DrMarco for os8088

A native 8086 single-player adaptation of Nintendo's Dr. Mario (1990), using
the supplied NES disassembly as the gameplay reference and its capsule/virus
tiles, with original generated DrMarco screen artwork. No NES CPU or PPU is
emulated. The feature roadmap is [DRMARCO_PLAN.MD](../../DRMARCO_PLAN.MD).

Build in this worktree:

```
make drmarco
make drmarcodisk
```

The standalone disks are 360 KB, 720 KB, 1.2 MB and 1.44 MB. This application
is registered as `local`: standard os8088 builds and release images do not
require or redistribute the user-supplied NES assets.
The previous `drmario` and `drmariodisk` commands remain aliases. Source folders,
`DRMARIO_SOURCE`, and `build/drmario*.img` disk paths remain stable; the package
is now `build/drmarco.o88` and appears on disk as `DRMARCO.O88`.

The default reference directory is `../NES-Games-Disassembly/Dr. Mario`.
Override it with `make drmarco DRMARIO_SOURCE='/path/to/Dr. Mario'`.
The supplied reference is revision `df2c8e5`; the importer records SHA-256
hashes of both inputs in `build/drmario-art/dm-source.txt`.
NES graphics are imported into `build/drmario-art/`, never committed.
`make drmario-assets` refreshes the import. No external files are needed at
runtime: open `DRMARCO.O88` from the generated application disk.

The original doctor and laboratory surround are committed in
[`art/drmarco-screen.png`](art/drmarco-screen.png), with the built-in imagegen
prompt in [`art/PROMPT.md`](art/PROMPT.md). The compiler uses Pillow to resize
and map this artwork to native palettes, then emits compact run-length streams
for each VGA plane and CGA bank. These are drawn once on fullscreen entry or
reentry; ordinary game frames do not decode the background. Native art previews
are written to `build/drmario-art/drmarco-{vga,cga}-art.png`.

The desktop launcher selects level 0–20 with Left/Right and LOW/MED/HI speed
with S. Enter, a click, F or Alt+Enter enters fullscreen. Leaving fullscreen
preserves the board; Enter resumes it. Changing setup discards the old board.

- Left/Right: move; Down: faster fall.
- Z/X: turn counterclockwise/clockwise; Up also turns clockwise.
- P: pause; N: restart at the current level.
- Enter after a clear: next level; Enter after game over: retry.
- Escape or Alt+Enter: restore the desktop.

Match at least four cells of one color horizontally or vertically. Viruses
stay fixed; capsule halves remain connected until one is cleared. Loose pieces
fall and can start cascades. Score counts cleared viruses with a multiplier
for multiple viruses and for speed. The next capsule is always visible.

VGA uses 320×240 Mode X with blue/red/yellow cells. CGA uses fullscreen
320×200, black background and the bright green/red/yellow hardware palette;
green represents the NES blue pieces. The three colors are distinct on RGB
CGA; no composite monitor or programmable DAC is assumed. Other adapters
can display the launcher but cannot play.

Tiles are converted to native VGA planes and packed CGA bytes at build time.
The font is cached from the OS at launch. A 128-byte shadow detects changed
bottle cells, including the active capsule. Movement checks only its two old and two new
cells; locks and cascades compare the whole bottle. VGA batches writes by plane, and
CGA alternates banks while copying complete packed rows. Text compares cached
characters. Idle frames write no video memory. No full framebuffer, heap,
background worker or per-pixel game-loop drawing is needed.

The reference capsule generator, color tables and speed curve are retained;
timing rounds to os8088's 54.6 Hz fullscreen clock. Virus placement applies
the source's level height and distance-two color exclusions, with a native
random retry scheme. This is an adaptation, not a cycle-exact NES port:
competitive multiplayer, NES music, attract scenes and endings are absent.
PC speaker cues use the OS sound service. See SPEC.md §99.


Verification:

```
make drmarcodisk build/os8088-360.img
python3 tests/drmario.py
python3 tests/drmario.py --qemu-display
```

The main gate runs actual 8088 code in MartyPC on VGA and CGA, checks every
starting level, matches, cascades, connected gravity, rotation wall kicks,
game over, level progression, held keys, short taps, pause, and mode restoration.
It compares video memory with the source CHR pixels, verifies the embedded
background decoder against the uncompressed art, checks that the portrait,
capsule preview and footer survive HUD updates, and requires incremental
paints to equal full repaints. A corrupted pixel must fail the oracle.
Use `--source /path/to/Dr. Mario` with a nondefault asset directory.

Screenshots and timings are in `build/drmario-proof/`. MartyPC's display
capture crops Mode X after the BIOS mode transition; the `*-native.png`
images decode all video planes, and `vga-qemu.png` verifies the complete
320×240 display using a second emulator. QEMU supplies no speed measurements.

Measured in MartyPC at 4,772,727 Hz (2026-09-28):

| Operation | VGA | CGA |
|---|---:|---:|
| Horizontal capsule move: renderer | 3.41 ms | 1.37 ms |
| Idle renderer, minimum of eight samples | 0.022 ms | 0.022 ms |
| Slowest setup across levels 0–20 | 39.12 ms | 39.13 ms |
| Background decode, fullscreen entry only | 331.96 ms | 97.33 ms |

Movement timings include changed-cell detection and video writes, and any
interrupts during that call; keyboard delivery and the frame wait are excluded.
The earlier whole-bottle comparison took 6.23 ms VGA and 4.19 ms CGA for the
same move. These are emulator cycle measurements, not physical XT measurements.
The instance uses 36,189 image bytes plus 10,194 BSS bytes (46,383 total);
the compressed package is 14,312 bytes. No kernel allocation or framebuffer
is added. Background decoding runs only when entering/reentering fullscreen.

# Dr. Mario for os8088

A native 8086 single-player adaptation of Nintendo's Dr. Mario (1990), using
the supplied NES disassembly as the gameplay reference and its original tiles.
No NES CPU or PPU is emulated.

Build in this worktree:

```
make drmario
make drmariodisk
```

The standalone disks are 360 KB, 720 KB, 1.2 MB and 1.44 MB. This application
is registered as `local`: standard os8088 builds and release images do not
require or redistribute the user-supplied NES assets.

The default reference directory is `../NES-Games-Disassembly/Dr. Mario`.
Override it with `make drmario DRMARIO_SOURCE='/path/to/Dr. Mario'`.
The supplied reference is revision `df2c8e5`; the importer records SHA-256
hashes of both inputs in `build/drmario-art/dm-source.txt`.
Original graphics are imported into `build/drmario-art/`, never committed.
`make drmario-assets` refreshes the import. No external files are needed at
runtime: open `DRMARIO.O88` from the generated application disk.

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
make drmariodisk build/os8088-360.img
python3 tests/drmario.py
python3 tests/drmario.py --qemu-display
```

The main gate runs actual 8088 code in MartyPC on VGA and CGA, checks every
starting level, matches, cascades, connected gravity, rotation wall kicks,
game over, level progression, held keys, short taps, pause, and mode restoration.
It compares video memory with the source CHR pixels and requires incremental
paints to equal full repaints. A corrupted pixel must fail the oracle.
Use `--source /path/to/Dr. Mario` with a nondefault asset directory.

Screenshots and timings are in `build/drmario-proof/`. MartyPC's display
capture crops Mode X after the BIOS mode transition; the `*-native.png`
images decode all video planes, and `vga-qemu.png` verifies the complete
320×240 display using a second emulator. QEMU supplies no speed measurements.

Measured in MartyPC at 4,772,727 Hz (2026-09-28):

| Operation | VGA | CGA |
|---|---:|---:|
| Horizontal capsule move: renderer | 4.05 ms | 1.37 ms |
| Idle renderer, minimum of eight samples | 0.022 ms | 0.022 ms |
| Slowest setup across levels 0–20 | 38.66 ms | 39.12 ms |

Movement timings include changed-cell detection and video writes, and any
interrupts during that call; keyboard delivery and the frame wait are excluded.
The earlier whole-bottle comparison took 6.23 ms VGA and 4.19 ms CGA for the
same move. These are emulator cycle measurements, not physical XT measurements.
The instance uses 9,999 image bytes plus 10,194 BSS bytes (20,193 total);
the compressed package is 5,662 bytes. No kernel memory allocation is added.

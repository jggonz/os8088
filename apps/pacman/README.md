# Pac-Man for os8088

A native 8086 adaptation of Roklan's Atari computer Pac-Man disk version,
revision 3.0, dated 10/03/82 in `PACMAN.ASM`. Open `GAMES/PACMAN.O88` from
the software disk, or install Pac-Man from The Wire.

- Arrow keys or WASD steer; turns are buffered until a legal junction.
- P or Space pauses. N starts a new game.
- F enters or leaves full screen; Esc leaves it.
- The system About menu shows the credits. Dismiss with a click or key,
  then press P to resume. Switching to another window suspends play.

Eat all 256 dots and four power pellets to advance the maze. Dots score 10,
pellets 50; frightened ghosts score 200, 400, 800 and 1600. Fruit appears at
80 and 160 dots eaten and awards 100 through 5000 depending on the level.
You start with three lives and earn one extra at 10,000 points.

The source's maze, wall graphics, player and ghost silhouettes, junction
masks, home corners, release-delay ladder, frightened-time ladder and fruit
score ladder are carried into the port. The 18.2 Hz worker replaces the
Atari video interrupts, speaker tones replace POKEY sound, and ghosts use
alternating corner/chase phases with distance-based fleeing. The original
scripted opening and patrol routes, attract screens, two-player swapping,
difficulty selector and intermission cartoons are not implemented.

VGA and Hercules display a 320 by 176 board. CGA uses a 320 by 88 board,
sampling alternate source rows. All layouts retain the same maze and game
coordinates. Dirty tile bands are composed in RAM and blitted once, so
moving sprites do not erase directly on the display. Monochrome displays
use packed 1-bit bands; color displays use packed 4bpp bands. Each instance owns its
state and one 256-byte-stack worker; no kernel changes or extra heap claims
are required. The packed canvas accounts for 28,160 bytes of the BSS.

Build with `make build/pacman.o88`, or `make` for every software floppy.
The port's contract is [SPEC.md §89](../../SPEC.md#89-pac-man-appspacmanpacmanasm).

## Source and license

Imported from the sibling `atari-pacman` checkout, upstream
[DillonDepeel/Pacman-Source-Code](https://github.com/DillonDepeel/Pacman-Source-Code),
commit `0596d9ac32c223361e5f50664f7e7884b1d2a7fd`.
The upstream MIT notice is preserved in [LICENSE](LICENSE) and embedded
in the standalone package. The original
assembly credits Roklan Corp. and Atari Inc.; Pac-Man originated at Namco.

`assets.inc` is committed so a normal build needs no sibling repository,
network access, ROM file or 6502 assembler. Reproduce or verify it with:

```sh
python3 tools/pacman_assets.py ../atari-pacman
python3 tools/pacman_assets.py ../atari-pacman --check
```

`PACDAT1.ASM` supplies DATMAZ and PACCHR; `PACDAT2.ASM` supplies HTAB01..10,
VTABLE/HTABLE, sprite bytes, HOMEHV, STARTV and BLUTIM. The extractor expands
the maze-used characters' four 2-bit Atari pixels into four bytes of doubled
packed 4bpp pixels, with
wall and sprite colors chosen to remain visible on monochrome adapters.

Run `python3 tests/unit/t_pacman.py` for exhaustive maze reachability, and
`python3 tests/pacman.py --machine os8088_xt_vga` for the guest gameplay
gate. The same gate runs on `os8088_5150_cga_gla` and
`os8088_5150_herc_gla`; `--capture PATH.png` saves an emulator screenshot.

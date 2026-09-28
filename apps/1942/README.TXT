1942 - a native remake for os8088

Open 1942.O88, press Enter for fullscreen, then Enter to take off.
Arrows: move   Space/Z: fire   X: invulnerable roll (3 per stage)
P: pause      M: sound        C: cycle CGA palette
Escape/F: desktop            N: new game
Enter in the launcher resumes your game.

VGA: 320x240 Mode X, a 256x240 image with black side borders.
CGA: 320x200, four colors, 5:4 horizontal and 5:6 vertical scaling.
CGA uses the card's real background/group/intensity controls; it cannot
program arbitrary RGB colors. Three profiles: Ocean, Jungle and Night.

Shoot orange aircraft to release weapon pickups. Collect them for two,
then three guns. Each stage sends 24 aircraft. Every fourth stage ends
with a large aircraft. Clear 32 stages to complete the mission.
Three lives. Respawning grants temporary invulnerability. Sound effects
use the OS speaker service. No NES soundtrack is emulated.

This is a native remake, not an emulator or an exact NES conversion.
Aircraft shapes, formations and gameplay are new. Font and background/
explosion textures are extracted from your own 1942 cartridge at build
time. The original 1942 game and cartridge graphics belong to Capcom.
No ROM is included in the source repository.

Build from the repository root:
  make 1942disk NESROM=/path/to/1942.nes
Customize RGB colors and CGA maps in apps/1942/palette.json, then rebuild.
The build produces build/1942.img and build/1942-360.img.
Requires NASM and Python 3; no C compiler or network download.

A 64KB canvas is claimed per instance. An 8086 instruction set is used;
game speed depends on the machine and slows when rendering takes longer
than the nominal frame interval. VGA is expected to be the faster path.

Automated gate (requires the repository's MartyPC installation):
  make 1942test NESROM=/path/to/1942.nes
It checks VGA/CGA bytes against an independent scaler, compares incremental
and full redraws, and exercises controls, collision, grace, pickups, bosses,
victory, palette cycling and desktop restoration. The host-only asset
validation checks need no ROM:
  python3 tests/n1942.py --assets-only

Measured on MartyPC's 4.77 MHz 8088: a firing-heavy 90-frame run was about
3.5 frames/s on VGA and 3.9 frames/s on CGA. Quiet scenes run faster. This
is functional on an XT, but an AT-class or faster machine is recommended
for playing. These are emulated timings, not physical-hardware results.

1942 - a standalone native shooter for os8088

Open 1942.O88, press Enter for fullscreen, then 1/2 selects players and Enter takes off.
Arrows: move   Space/Z: fire   X: invulnerable roll (3 per stage)
P: pause      M: sound        C: cycle CGA palette
Escape/F: desktop            N: new game
Enter in the launcher resumes your game and route position.
Enter also advances the mission results screen.

VGA: 320x240 Mode X, centered 256x240 playfield.
CGA: 320x200 color, 5:4 horizontal and 5:6 vertical scaling.
C cycles three hardware palette profiles: Ocean, Jungle and Night.

The cartridge build imports all 32 encounter schedules, formation paths and
small-plane entry positions. Aircraft dive, reverse, sweep and cross the
screen; bombers follow waypoint routes and fire aimed shots. The giant
background bomber appears on played stages 7, 15, 23 and 31.

Destroy a complete orange formation to earn a POW: stronger guns, a screen
clear, two armed wingmen, an extra roll or an extra life. Wingmen can be lost
to enemy fire or contact. A secret bonus plane appears after enough kills.
Takeoff and landing bracket each mission. Results award shooting-percentage
and unused-roll bonuses. Extra lives arrive at 20,000, 80,000 and every further
80,000 points. Clear 32 stages to complete the mission.

Two players alternate after a death, retaining separate scores, lives,
weapons and stages; a returning player restarts their current stage.
High scores last until the application closes. Music and effects use native
speaker note streams, with effects taking priority over music.

BUILD (NASM and Python 3; no emulator or C compiler):
  make 1942disk
If 1942.nes is present in the repository root, its aircraft and complete
landscape route are imported automatically. Or specify your local file:
  make 1942disk N1942_ROM=/path/to/1942.nes
The importer checks the supported cartridge payload before decoding it.
No cartridge or extracted graphics are downloaded or committed.
  make 1942disk N1942_ROM=
This selects the committed original art, with cyclic scrolling scenery.

The resulting build/1942-360.img and build/1942.img are game disks, not
boot disks. Boot os8088 in A: and mount the game disk in B:. Keep 1942.O88
and its .GFX, .V42 and .C42 companions together. The cartridge is only a
build input; the XT runs the compiled native package and graphics files.

GRAPHICS
A loading message appears before graphics I/O. Terrain uses a circular
cache: only incoming rows are decoded, with no full-buffer memory shift.
VGA scrolls and flips two hidden pages with hardware start addresses and
latch copies. CGA scrolls its video banks in hardware, merging old/new actor
rectangles before copying packed pixels. Neither shifts the full terrain
buffer every frame. Aircraft use cached/compiled sprite drawing. The HUD
stays fixed. Pause freezes the route, and resume retains its position.
Three 64KB claims hold graphics/canvas storage; the XT target is 640KB RAM.

ARTWORK
Without a cartridge, apps/1942/art/ supplies the original project artwork.
With one, tools/1942nes.py decodes NES aircraft assemblies, tile flips,
palettes, loop/directional animations, wingmen, POWs, projectiles, the title
and font, all 23 landscape pages and the 32-stage route. VGA colors are an
NES palette approximation. This remains a native remake with its own combat
and timing, rather than an emulator or cycle-faithful NES conversion.

TEST
  make 1942test
Checks VGA/CGA pixels, loading, scrolling, aircraft, formation rewards, POWs,
results, two-player turns, combat, pause/resume, sound controls, file validation and desktop restoration on 4.77MHz MartyPC. Guest-cycle
performance includes rendering and presentation; disk loading is separate.

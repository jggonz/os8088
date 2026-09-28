1942 - a standalone native shooter for os8088

Open 1942.O88, press Enter for fullscreen, then Enter to take off.
Arrows: move   Space/Z: fire   X: invulnerable roll (3 per stage)
P: pause      M: sound        C: cycle CGA palette
Escape/F: desktop            N: new game
Enter in the launcher resumes your game.

VGA: 320x240 Mode X, a 256x240 image with black side borders.
CGA: 320x200, four colors, 5:4 horizontal and 5:6 vertical scaling.
CGA uses real background/group/intensity controls: arbitrary RGB colors
are available on VGA only. Three CGA profiles: Ocean, Jungle and Night.

Original aircraft bank as you move; explosions have four animation frames.
Ocean, reef and harbor scenery banks alternate every four stages. Shoot
orange aircraft for weapon pickups. Collect them for two, then three guns.
Each stage sends 24 aircraft. Every fourth stage ends with a bomber.
Clear 32 stages to complete the mission. Three lives; temporary protection
on respawn. Sound effects use the OS speaker service.

BUILD (NASM and Python 3; no ROM, downloads or C compiler):
  make 1942disk
The resulting build/1942-360.img and build/1942.img are game disks, not
boot disks. Boot os8088 in drive A: and mount the game disk in B:.
Keep 1942.O88 and all .GFX, .V42 and .C42 files together in one directory.
The same files can be copied to a hard disk or the live volume's 1942 folder.

GRAPHICS
The package contains its own 8086 graphics engine. VGA draws to a hidden
page, copies cached sprite pixels with hardware latches, and flips pages at
vertical retrace. CGA uses compiled 8086 sprite stores in RAM and copies
only changed rectangles into the video banks. Neither adapter converts a
full chunky canvas in the frame loop. A cached backdrop keeps detailed
terrain from becoming a per-frame CPU expense. Wakes move over the scenery.
Two 64KB memory claims hold the sprite bank and background/canvas storage;
640KB RAM is the XT target. Stage loading briefly pauses play for disk I/O.

ARTWORK
All game art is committed under apps/1942/art/. The PNG masters were
created for this project with imagegen; the indexed production artwork and
an original stencil font are compiled by tools/1942assets.py. No graphics
are extracted from an NES cartridge. This is an independent native remake,
not an emulator or a faithful conversion of Capcom's original game.

Customize VGA colors, the CGA ink map and profiles in palette.json, then
rebuild. Editing the PNG masters is optional: tools/1942art.py imports them
with Pillow, writes the indexed source and resets the palette. Pillow and
image generation services are not needed for normal builds.

TEST
  make 1942test
Exercises VGA/CGA output against an independent compositor, combat controls,
collisions, roll/grace, pickups, bosses, stage loads, victory and desktop
restoration on MartyPC. Performance is measured in guest cycles on the
pinned 4.77MHz XT models, including presentation and frame pacing.

XT PERFORMANCE (4.77MHz MartyPC, not a physical-hardware guarantee)
Crowded combat: VGA 7.44fps average / 6.63fps slowest frame; CGA 5.53fps /
5.14fps slowest. The fixture holds 12 aircraft, 16 shots, 16 enemy bullets
and four explosions on screen. Ordinary firing runs measured about
17-21fps. The accepted XT floor is 5fps; the regression gate enforces it.

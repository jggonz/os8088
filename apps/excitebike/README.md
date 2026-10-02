# 8BitBike for os8088

A native 8086 motocross racer being rebuilt to match the 1984 cartridge's
graphics, courses, rules and sound. Cartridge data is imported once into
committed, reviewable text sources; ordinary builds use only those sources
and never need the reference checkout. Nintendo's marks are replaced with
generated 8BitBike art, and no copyright line is displayed.

The cartridge-faithful rebuild is tracked in `excitebike_plan.md` and
SPEC.md §102. Wave 0 renames the package and sets up the import pipeline;
the current engine still uses the previous original art, tracks and sound
until their imported replacements land. `docs/plans/EXCITEBIKE-PLAN.md` records
that engine's design; its original-art policy is superseded.

**Current engine:** This build is the whole game around the rider: a title,
a mode menu (A: alone against the clock, B: with CPU riders, three on the VGA and
two on the CGA and the Hercules), five courses of two laps each (a sixth if you put a designed
`EXBTRACK.DAT` beside the package), READY 3, 2, 1 and GO!, a lap flash, the
finish and a results screen that RANKS the time against the course's par - first,
second, third or not qualified - and a campaign: a qualified ride goes on to the
next course, the fifth begins a second pass with the harder obstacles, the last
course repeats with a shrinking qualify window, and a ride that does not qualify
is game over. Best times are kept while the window is open. Leave the title alone
for ten seconds and the attract demo drives a race by itself. Underneath: the
scroll engine on VGA 0Dh (a frame every 18 ms on a 4.77 MHz XT, three opponents
included), CGA (retrace-paced) and the Hercules (the same engine, two card pixels to a
game pixel: a 360-pixel-wide picture at the monitor's own 50 Hz), and the rider: throttle and turbo, four lanes,
ramps, hills and kickers, hurdles, mud and rough ground, an engine that heats and
can stall, wheelies that can flip, landings that can crash. And sound, all of it original: five songs (title, select, results, the
finish fanfare, game over) and nine effects, an engine whose pitch follows the
rider's speed, on an AdLib or Sound Blaster when the machine has one and on the PC
speaker (an octave up for the engine, the lead voice for the tunes) when it has
not. See `docs/plans/EXCITEBIKE-PLAN.md` and SPEC.md 102.

Keys: the arrows steer (Up/Down the lane, Left lifts the nose, Right drops it),
Z the throttle, X the turbo (it heats the engine: watch the gauge, the arrows on
the track cool it), Enter pauses, C changes the palette, Esc leaves the race (and
the title), M turns the sound off and on. In the menus: Up/Down and Enter, A/B for the mode, B on the title for
the best times.

Build:

```
make 8bitbike          # build/8bitbike.o88
make 8bitbikedisk      # four standalone floppies: 1.44MB, 720KB, 1.2MB, 360KB
make excitebiketest    # the front-end gate (needs MartyPC, see docs/MARTYPC-DEBUG.md)
make excitebikeaudio   # the sound: score, engine, effects, FM claims, the speaker's own capture
make excitebikevideo   # the scroll engine, pixel for pixel against tools/exbsim.py
make excitebikeperf    # frame-rate and governor gates
make excitebikeref     # the rider simulation against tools/exbsim.py, step for step
make excitebikelap     # a whole course at turbo on VGA and CGA (`tests/excitebike_perf.py --herc --lap` for the Hercules)
make excitebikeflow    # the menus, the rank, the campaign, EXBTRACK.DAT, the opponents' invariants
make excitebikeselfb   # Selection B's frame rate
make excitebikegeom    # the four floppy geometries, booted (360KB, 720KB, 1.44MB) or walked (1.2MB), and the low-memory refusal
make xt-excitebike     # 86Box: a 4.77 MHz VGA XT with the 360KB game disk in B: (a human looks; it asserts nothing)
```

The old `excitebike` and `excitebikedisk` target names remain aliases.

Offline refreshes, outside the normal build:

```
EXCITEBIKE_REF=/path/to/Excitebike make excitebike-import
EXCITEBIKE_REF=/path/to/Excitebike make excitebike-fixtures  # needs host cc
make excitebikeimport  # compares imports; SKIPs without the reference
```

`cart/PROVENANCE.md` records the pins and imported sources; `cart/ADAPTATIONS.md`
records remaining differences. Committed NES traces are read by
`python3 tests/excitebike_oracle.py` without the reference or a C compiler.

A plain `make 8bitbikedisk` needs only NASM and Python 3 (standard library):
there is no source directory to point at and no optional import.

Open `8BITBIKE.O88` from the disk (drive B:) and keep `8BITBIKE.VGA`, `8BITBIKE.CGA`,
`8BITBIKE.HRC`, `8BBV.GFX`, `8BBC.GFX` and `8BBH.GFX` beside it. Enter or Alt+Enter goes
full screen (VGA, CGA or Hercules; an EGA desktop shows the splash and says so), `C`
changes the palette, Enter pauses a race and Esc goes back a screen (and, from the
title, to the desktop).

`EXBTRACK.DAT` (optional, at most 224 bytes) is a designed course: `EXBT`, a word par
in hundredths, a word stream length, the stream as (column id, run) pairs ending 0, 0,
a word trigger count and (column, script) pairs - SPEC.md 102.4 has the layout.

Machines: a VGA or CGA or Hercules XT with 640KB is the target. The lane dividers are white on the
VGA and black on the CGA and the Hercules, whose ground would otherwise swallow a white dash. The
lap flash strobes the banner plates on the VGA (a palette rewrite; the other two have no palette to
rewrite). On an EGA desktop the splash and help open and START says `VGA, CGA OR HERC ONLY`: an EGA
has no CGA colour-select port and nothing here can host one (SPEC.md 102.8.5). On a machine with too
little memory left for the game's own claims Enter is refused with `NOT ENOUGH MEMORY` and the window
stays as it was; the 256KB floor machine holds one 8BitBike.

The floppy is the same eight files in each geometry: `8BITBIKE.O88`, `README.MD`, `8BBV.GFX`,
`8BBC.GFX`, `8BBH.GFX` and the three `8BITBIKE.*`. The 1.2MB one needs a 5.25 inch HD drive.

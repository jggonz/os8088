# Excitebike for os8088

A native 8086 motocross racer in the spirit of the classic side-scrolling
dirt-bike game: lanes, ramps, hurdles, mud, a temperature bar, two laps against
the clock. It is **not** an emulator and it does **not** contain, read or
derive anything from Nintendo's game: every tile, sprite, banner, letter,
splash and sound is original work drawn and composed for this project, kept as
plain-text sources under `art/`, `tracks/` and `audio/` and compiled by
`tools/excitebike_assets.py`. `art/README.md` records how each asset class was
made.

**Status: complete (wave 7 of 7).** This build is the whole game around the rider: a title,
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
make excitebike        # build/excbike.o88
make excitebikedisk    # four standalone floppies: 1.44MB, 720KB, 1.2MB, 360KB
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

A plain `make excitebikedisk` needs only NASM and Python 3 (standard library):
there is no source directory to point at and no optional import.

Open `EXCBIKE.O88` from the disk (drive B:) and keep `EXBSPL.VGA`, `EXBSPL.CGA`,
`EXBSPL.HRC`, `EXBV.GFX`, `EXBC.GFX` and `EXBH.GFX` beside it. Enter or Alt+Enter goes
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
stays as it was; the 256KB floor machine holds one Excitebike.

The floppy is the same eight files in each geometry: `EXCBIKE.O88`, `README.MD`, `EXBV.GFX`,
`EXBC.GFX`, `EXBH.GFX` and the three `EXBSPL.*`. The 1.2MB one needs a 5.25 inch HD drive.

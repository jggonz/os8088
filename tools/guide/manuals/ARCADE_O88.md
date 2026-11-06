# ARCADE

ARCADE plays seven arcade games of 1975 to 1982 on their boards' own
programs: Space Invaders, Space Invaders Part II, Lunar Rescue, Gun Fight,
Sea Wolf, Q*bert and Pac-Man. Its window is a cabinet with the list of
games, a screen showing each game, its card with the controls, and buttons
to play, to add games and to change their settings.

No game comes with os8088. You add the ROM sets you own, as MAME names
them, and ARCADE checks each one and keeps it on a disk of your choosing.

## Starting ARCADE

Double-click `ARCADE.O88`. It is on its own disk, and in the `ARCADE`
folder of the everything disks. On kern_mega, os8088's kernel for a 286 or
later, use the ARCADE disk made for kern_mega: the same program and games,
keeping its memory above the first megabyte where the machine has it.
Opening it reads no game: it looks for each game's file and marks it in the
list:

| Mark | Meaning |
|---|---|
| READY | its file was found: Enter plays it |
| ROM NEEDED | add its ROM set first |
| NEEDS VGA | Pac-Man and Q*bert need a VGA display |

The first time you leave a game, ARCADE keeps a picture of its screen, and
the cabinet's screen shows it from then on.

## Adding your ROM sets

Choose **Add ROM sets...** on the **Game** menu, the **ADD ROMS** button,
or press Insert, and choose the set's `.ZIP` in the file chooser:
`invaders.zip`, `invadpt2.zip`, `lrescue.zip`, `gunfight.zip`,
`seawolf.zip`, `qbert.zip` or `pacman.zip`. ARCADE checks every chip
against the one it was made for, and writes one file, such as `QBERT.ROM`,
in the zip's own folder. The menu bar's file bar shows it working. The zip
is not needed again.

It also takes a merged set, whose clones' chips sit in folders inside the
zip; a split Pac-Man, if MAME's `puckman.zip` is in the same folder as
`pacman.zip`; and a folder of loose chip files, by choosing any one of
them. A zip whose chips are not right writes nothing and says so.

ARCADE remembers every folder it wrote a file in, so a disk of ROMs works in
any drive. A game whose file cannot be found says **not found** when you
play it, or **disk in B:** when it was made on a floppy that is not in.

## Playing

Choose a game with Up and Down, Home and End, a click, or its first letter,
and press Enter or Space, click **PLAY**, or click the chosen game again.
The game fills the screen:

| Key | What it does |
|---|---|
| 5 or C | insert a coin |
| 1, 2 | start a one- or two-player game |
| Arrows | move (Pac-Man: all four; Q*bert: see below) |
| Space | fire (Lunar Rescue: thrust) |
| P | pause; any key plays on |
| F1 | the game's card over its picture; any key plays on |
| Esc | back to the cabinet; the game waits where it was |

Q*bert's joystick is turned on the cabinet, so the keys are the diagonals:
the keypad's 9, 3, 1 and 7 as they look on the screen, or the arrows, Up
being up-right. Gun Fight is for two: the left gunman walks with W A S D,
aims with R and F and fires with G; the right one uses the arrows, Page Up,
Page Down and Space. Sea Wolf aims with Left and Right.

## Settings and scores

**SETTINGS** (or F2) shows the chosen game's switches in words: Space
Invaders' bases and bonus, and Pac-Man's lives, bonus, difficulty and
ghost names. Up and Down choose one, Left and Right change it, and Esc or F2
goes back. They are kept for next time. Space Invaders' and Pac-Man's best
scores are kept too, and come back when the game is next switched on.

## The Game menu

| Item | What it does |
|---|---|
| Play | plays the chosen game |
| Reset | switches the game's board off and on again |
| Add ROM sets... | adds a ROM set |
| a game's name | chooses that game and plays it |

## Sound

With a Sound Blaster every sound plays mixed. Q*bert's sound board runs
beside the game on a 386 or faster; on a slower machine Q*bert is silent.
With an AdLib alone the voices play on its FM chip, and otherwise the PC
speaker plays one sound at a time.

## Files

| File | What |
|---|---|
| `<SET>.ROM` | a game's chips, written by Add ROM sets... beside its zip |
| `<SET>.PIC` | the picture of a game, beside its `.ROM` |
| `ARCADE.DAT` | where your ROMs are, settings and best scores, in `SYSTEM\APPDATA` on ARCADE's disk |

## Good to know

- The five 8080 games run at full speed on an IBM PC/XT on any display.
  Q*bert keeps its own pace there too, drawing fewer pictures a second.
  Pac-Man plays a frame or two a second on an XT, and most of its speed on
  a 16 MHz 286.
- A 360KB disk holds ARCADE and several games' ROMs.
- [[ARCADE.TXT]] is the same notes as a text file.

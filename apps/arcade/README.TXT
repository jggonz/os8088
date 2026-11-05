ARCADE for os8088
=================

Seven arcade games, played on the boards' own programs: five 8080
boards translated to 8086 code, Pac-Man's Z80 board the same way, and
Q*bert, whose CPU is an 8088 already, so its program runs as it is.

    Space Invaders          Midway's 8080 board (Taito, 1978).
    Space Invaders Part II  Taito's colour board (1979).
    Lunar Rescue            Taito's colour board (1979): land on the
                            moon, carry the men up to the mother ship.
    Gun Fight               Midway (1975): a duel, two players at one
                            keyboard.
    Sea Wolf                Midway (1976): sink the ships through the
                            periscope.
    Q*bert                  Gottlieb (1982), its own 8088 program. VGA
                            only.
    Pac-Man                 Namco's Z80 board (1980). VGA only, and a
                            286 or faster for the board's full speed.

YOUR ROMS
---------
ARCADE carries no game. Its code has the boards' ROM bytes left out, and
each game needs its set's ROMs, from a board you own or a licensed
collection, as MAME names them:

    invaders.zip  invadpt2.zip  lrescue.zip  gunfight.zip
    seawolf.zip   qbert.zip     pacman.zip

Add them with Game > Add ROM sets... (or ADD ROMS, or Insert), and choose
the set's .ZIP in the file chooser. ARCADE checks every chip it needs
against the one it was made for, and writes one file, <SET>.ROM (for
instance QBERT.ROM), in the zip's own folder. The .zip is not needed
again. It also takes:

  - a MERGED set, whose clones' chips sit in folders inside the zip;
  - a SPLIT Pac-Man: pacman.zip holds only the chips that differ, so put
    MAME's puckman.zip in the same folder and choose pacman.zip;
  - LOOSE chips: a folder of the set's chips as files - choose any one.

A zip whose chips are not the set's writes nothing and says so. The menu
bar's file bar shows the import going. It is done once: on an 8088 it
takes some seconds a set.

A .ROM is the chips alone: Space Invaders' is 8KB, Q*bert's 68KB (with
its sound board's), Pac-Man's 25KB. ARCADE remembers every folder it
wrote one in, so a disk of ROMs works in any drive. If a game's file is
not found when you play it, ARCADE says so - "QBERT.ROM: not found", or
"QBERT.ROM: disk in B:" when it was made on a floppy that is not in.

THE CABINET
-----------
Open ARCADE.O88: a cabinet with every game in a list, each marked READY,
ROM NEEDED or NEEDS VGA. Up and Down (or a click, or a game's first
letter) choose one; its card says how it is played. The first time you
leave a game, ARCADE keeps a picture of its screen, and shows it there
from then on (as <SET>.PIC beside the .ROM).

Enter, Space, PLAY or a click on the chosen game plays it full screen:

    5 or C        insert a coin
    1, 2          one or two players
    Arrows        move (Left, Right; Pac-Man: all four)
    Space         fire (Lunar Rescue: thrust)
    P             pause; any key plays on
    F1            the game's card over its picture
    Esc           back to the cabinet; the game waits where it was

SETTINGS (or F2) shows the chosen game's switches in words - Space
Invaders' bases and bonus, Pac-Man's lives, bonus, difficulty and the
ghosts' names - kept for next time. A game's best score is kept too
(Space Invaders' and Pac-Man's), and comes back when it is next switched
on. Game > Reset powers the board off and on again.

Q*BERT. The joystick is turned on the cabinet, so the keys are the four
diagonals: the keypad's corners as they look on the screen - 9 up and
right, 3 down and right, 1 down and left, 7 up and left - or the arrows
as MAME has them: Up is up-right, Right down-right, Down down-left, Left
up-left. On a 286 every frame is drawn. On an 8088 the game keeps its
full speed and the picture moves in steps: it is drawn only as often as
the machine can manage.

GUN FIGHT. The left gunman: W A S D to walk, R and F to raise and lower
his gun, G to fire. The right gunman: the arrows, Page Up and Page Down,
Space.

SEA WOLF. Left and Right turn the periscope, Space fires. Under the
picture (VGA and Hercules) are the cabinet's lamps: the four torpedoes,
then READY, then RELOAD.

SOUND
-----
With a Sound Blaster, every sound plays at once, mixed: Space Invaders'
sounds are synthesised for this disk (no recordings), Pac-Man's three
voices are its own sound chip, played sample by sample, and Q*bert's are
its own sound board - its 6502 and its program, run as the game plays
(a 386 or faster: on an 8088 or a 286 Q*bert is silent; its speech is
not spoken). With an AdLib alone, the voices play on its FM chip. Lunar
Rescue's end-of-level tunes are not played: the board makes them by
toggling a single bit in time, which this does not follow. Otherwise the
PC speaker plays one sound at a time. If the Control Panel sends tones to
the speaker, so does ARCADE.

VGA shows the cabinet's colour overlay (red at the top, green at the
bottom); Hercules and CGA show it black and white, as the board drew it.
Pac-Man's tall screen stands upright in the middle of the monitor, twice
as wide and one and a half times as tall as the board's. Q*bert's fills
the monitor's height: twice as wide, 15/8 as tall.

THE GAMES' OWNERS
-----------------
Space Invaders, Space Invaders Part II and Lunar Rescue are copyright
Taito Corporation; Gun Fight and Sea Wolf, Midway; Q*bert, Gottlieb (now
Sony Pictures Entertainment); Pac-Man, Bandai Namco. No ROM of theirs is
on this disk or in the os8088 source tree.

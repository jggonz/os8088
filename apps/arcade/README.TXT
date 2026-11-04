ARCADE for os8088
=================

Arcade boards whose ROMs were translated to 8086 code before this disk
was made - and one whose CPU is an 8088 already, so its program runs
as it is. Choose the game from the Game menu:

    Space Invaders          Midway's 8080 board (Taito, 1978).
    Space Invaders Part II  Taito's colour board (1979).
    Lunar Rescue            Taito's colour board (1979): land on the
                            moon, carry the men up to the mother ship.
    Gun Fight               Midway (1975): a duel, two players at one
                            keyboard.
    Sea Wolf                Midway (1976): sink the ships through the
                            periscope.
    Q*bert                  Gottlieb (1982), its own 8088 program. VGA
                            only, and silent for now.
    Pac-Man                 Namco's Z80 board (1980). VGA only, and a
                            286 or faster for the board's full speed.

The five 8080 games run at full speed on an IBM PC/XT on any adapter;
on VGA the two Taito games are in colour. A game is read from this disk
when you pick it, and the one before is put away.

Open ARCADE.O88. Enter, Space or a click plays full screen:

    5 or C        insert a coin
    1, 2          one or two players
    Arrows        move (Left, Right; Pac-Man: all four)
    Space         fire (Lunar Rescue: thrust)
    Esc           back to the window; the game waits where it was

Q*BERT. The joystick is turned on the cabinet, so the keys are the
four diagonals: the keypad's corners as they look on the screen - 9 up
and right, 3 down and right, 1 down and left, 7 up and left - or the
arrows as MAME has them: Up is up-right, Right down-right, Down
down-left, Left up-left. On a 286 every frame is drawn. On an 8088 the
game keeps its full speed and the picture moves in steps: it is drawn
only as often as the machine can manage.

GUN FIGHT. The left gunman: W A S D to walk, R and F to raise and lower
his gun, G to fire. The right gunman: the arrows, Page Up and Page Down,
Space.

SEA WOLF. Left and Right turn the periscope, Space fires. Under the
picture (VGA and Hercules) are the cabinet's lamps: the four torpedoes,
then READY, then RELOAD.

Game > Reset powers the board off and on again.

SOUND. With a Sound Blaster, every sound plays at once, mixed: Space
Invaders' sounds are synthesised for this disk (no recordings), and
Pac-Man's three voices are its own sound chip, played sample by sample.
With an AdLib alone, the voices play on its FM chip. Lunar Rescue's
end-of-level tunes are not played: the board makes them by toggling a
single bit in time, which this does not follow. Otherwise the PC
speaker plays one sound at a time. If the Control Panel sends tones to
the speaker, so does ARCADE.

VGA shows the cabinet's colour overlay (red at the top, green at the
bottom); Hercules and CGA show it black and white, as the board drew it.

Pac-Man's tall screen stands upright in the middle of the monitor,
twice as wide and one and a half times as tall as the board's.
Q*bert's fills the monitor's height: twice as wide, 15/8 as tall.

THE ROMS. Space Invaders, Space Invaders Part II and Lunar Rescue are
copyright Taito Corporation; Gun Fight and Sea Wolf, Midway; Q*bert,
Gottlieb (now Sony Pictures Entertainment). Their ROMs were fetched from the Internet
Archive's arcade collection when this disk was built and checked against
MAME's hashes. Pac-Man is copyright Bandai
Namco; its ROM comes only from the builder's own copy, checked the same
way, and the Pac-Man entry is absent when the disk was built without
it. Neither ROM is part of the os8088 source tree.

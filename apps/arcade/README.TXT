ARCADE for os8088
=================

Arcade boards whose ROMs were translated to 8086 code before this disk
was made. Choose the game from the Game menu:

    Space Invaders   Midway's 8080 board (Taito, 1978). Any adapter.
    Pac-Man          Namco's Z80 board (1980). VGA only: needs a 286
                     or faster for the board's full speed.

Open ARCADE.O88. Enter, Space or a click plays full screen:

    5 or C        insert a coin
    1, 2          one or two players
    Arrows        move (Invaders: Left, Right; Pac-Man: all four)
    Space         fire (Invaders)
    Esc           back to the window; the game waits where it was

Game > Reset powers the board off and on again.

SOUND. With a Sound Blaster, every sound plays at once, mixed: Space
Invaders' sounds are synthesised for this disk (no recordings), and
Pac-Man's three voices are its own sound chip, played sample by sample.
With an AdLib alone, the voices play on its FM chip. Otherwise the PC
speaker plays one sound at a time. If the Control Panel sends tones to
the speaker, so does ARCADE.

VGA shows the cabinet's colour overlay (red at the top, green at the
bottom); Hercules and CGA show it black and white, as the board drew it.

Pac-Man's tall screen stands upright in the middle of the monitor,
twice as wide and one and a half times as tall as the board's.

THE ROMS. Space Invaders is copyright Taito Corporation. Its ROM was
fetched from the Internet Archive's arcade collection when this disk was
built and checked against MAME's hashes. Pac-Man is copyright Bandai
Namco; its ROM comes only from the builder's own copy, checked the same
way, and the Pac-Man entry is absent when the disk was built without
it. Neither ROM is part of the os8088 source tree.

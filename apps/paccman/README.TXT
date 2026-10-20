PACCMAN - the arcade Pac-Man for os8088
=======================================

PACCMAN.O88 is the arcade Pac-Man: the tall maze, the four ghosts with their
own personalities, the power pills, the bonus fruit, and the arcade's own
pictures and tunes. It is a port of Andre Weissflog's pacman.c, a faithful
re-creation of the arcade game written in C.

STARTING IT
-----------
Double-click PACCMAN.O88. The window opens on the attract screen, which
introduces the ghosts one by one. Press any key to start a game, except F,
which switches full screen.

PaccMan wants a fast machine. On a 386 it plays at full arcade speed. On an
original 4.77 MHz PC or XT it runs at about a quarter of arcade speed, two or
three pictures a second, so the ghosts and Pac-Man jump along. The game is
still playable there, slowly.

KEYS
----
  Arrows, or W A S D   steer Pac-Man
  P                    pause, and press again to carry on
  Space                carry on after a pause
  N                    start a new game
  F                    full screen, and back to the window
  Esc                  leave full screen

Hold a direction key until Pac-Man has turned. A quick tap counts only for
one picture, so if Pac-Man is not at a corner by then the turn is forgotten.

P, N and Space work only during a game. On the attract screen they start a
game like any other key.

THE MENUS
---------
  PaccMan > About PaccMan    who made it, and the keys. This pauses a game
  PaccMan > Close            closes PaccMan
  Game > New Game            starts a new game
  Game > Pause / Resume      the label says what it will do
  Game > Sound Off / On      the label says what it will do
  Game > Full Screen         full screen, like F

SCORING
-------
  a dot                      10
  a power pill               50
  blue ghosts after a pill   200, 400, 800, then 1600
  the bonus fruit            100 on the first level, rising to 5000

You start with three lives. The high score is kept while the window is open.

SOUND
-----
The opening tune, the siren, the eating sounds and the death tune play on
the PC speaker. The arcade has three voices and the speaker has one, so you
hear the most important sound of the moment.

WHERE IT COMES FROM
-------------------
The program follows Andre Weissflog's pacman.c
(https://github.com/floooh/pacman.c), MIT licence, (c) 2020 Andre Weissflog.
The maze, tile, sprite and colour tables are the Pac-Man arcade ROM data
(Namco), carried the same way pacman.c carries them. The game rules follow
Jamey Pittman's Pac-Man Dossier.

The licence of pacman.c follows, as it asks to be included:

MIT License

Copyright (c) 2020 Andre Weissflog

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

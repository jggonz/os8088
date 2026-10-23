# PaccMan

PaccMan is the arcade Pac-Man: the original tall maze, the four ghosts with
their own personalities, the power pills, the bonus fruit and the arcade's
own pictures and tunes. Steer Pac-Man round the maze and eat every dot
without being caught. Eat a power pill and, for a short time, the ghosts
turn blue and you can eat them instead.

It is a port of Andre Weissflog's pacman.c, a faithful re-creation of the
arcade game written in C.

![A game in progress. Blinky has left the ghost house; the other three are still waiting.](shot:paccman-play){full}

## Starting PaccMan

![The attract screen: Shadow, Speedy, Bashful and Pokey, better known as Blinky, Pinky, Inky and Clyde.](shot:paccman-attract){right}

PaccMan has a disk of its own. Open that disk and double-click
**PACCMAN.O88**. Beside it is [[README.TXT]], the keys and where the game came from. The
window opens on the attract screen, which introduces the ghosts one by one
and the points for a dot and a power pill.

Press any key to start a game, except `F`, which switches full screen.

**PaccMan wants a fast machine.** On a 386 it plays at full arcade speed.
On an original 4.77 MHz PC or XT it runs at about a quarter of arcade
speed, two or three pictures a second, so the ghosts and Pac-Man jump
along. The game is still playable there, slowly.

## Playing

| Key | What it does |
|---|---|
| Arrow keys, or `W` `A` `S` `D` | Steer Pac-Man |
| `P` | Pause, and press again to carry on |
| `Space` | Carry on after a pause |
| `N` | Start a new game |
| `F` | Full screen, and back to the window |
| `Esc` | Leave full screen |

**Hold a direction key until Pac-Man has turned.** PaccMan reads which keys
are held down. A quick tap counts only for one picture, so if Pac-Man is
not at a corner by then the turn is forgotten. Keep the key down and he
turns at the next opening.

`P`, `N` and `Space` work only during a game. On the attract screen they
start a game like any other key.

You start with three lives; the spare ones are shown at the bottom left. A
ghost that catches you costs a life, and the game ends when the last one is
gone. Clear every dot and the maze starts again with the next level. The
bonus fruit for each level is shown at the bottom right.

## Scoring

| You eat | Points |
|---|---|
| A dot | 10 |
| A power pill | 50 |
| Blue ghosts after one pill | 200, 400, 800, then 1600 |
| The bonus fruit | 100 on the first level, rising to 5000 |

The fruit appears twice a level below the ghost house, for a short time. The
high score is kept while the window is open and forgotten when you close
it. There are no extra lives.

## Full screen

Press `F`, or choose **Game > Full Screen**, to fill the screen. The maze
stays the same size in the middle of a black screen. Press `F` or `Esc` to
go back to the window. If another program is already full screen, PaccMan
says **Another window is full screen.**

![PaccMan full screen on a VGA display.](shot:paccman-full){full}

## The menus

| Menu | Item | What it does |
|---|---|---|
| PaccMan | About PaccMan | Shows who made it and the keys. This pauses a game |
| PaccMan | Close | Closes PaccMan |
| Game | New Game | Starts a new game |
| Game | Pause / Resume | Stops the game or starts it again. The label says what it will do |
| Game | Sound Off / Sound On | Turns the sound off or on. The label says what it will do |
| Game | Full Screen | Full screen, like `F` |

The About card goes away with any key, a click or a menu command, and the
game stays paused until you press `P` or `Space` or choose **Resume**.

## Sound

PaccMan plays the arcade's opening tune, the siren, the eating sounds and
the death tune on the PC speaker. The arcade has three voices and the
speaker has one, so you hear the most important sound of the moment.
**Game > Sound Off** silences it.

## Good to know

- PaccMan works on VGA, EGA, CGA and Hercules displays. On CGA the maze is
  drawn at half height to fit the screen. On CGA and Hercules the coloured
  names on the attract screen are white.
- Pinky shows white rather than pink, and Clyde is the same yellow as
  Pac-Man.
- **1UP**, **2UP** and **CREDIT 0** are part of the arcade screen. There is
  no two-player game, and no coins are needed.
- Like the C version it comes from, PaccMan has no intermissions between
  levels.
- A paused window ignores the game keys, so a key you press while paused
  does not move Pac-Man afterwards.

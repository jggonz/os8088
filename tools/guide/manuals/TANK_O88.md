# Tank Attack

Tank Attack is a first-person tank battle drawn in glowing wireframe lines,
in the style of the 1980 arcade game. You drive across a flat plain dotted
with cubes, pyramids and slabs, with mountains on the horizon. Enemy tanks
come at you one at a time. Turn to face them, line up the sight and shoot
before they shoot you.

![Playing: an enemy tank ahead, and ENEMY IN RANGE lit beside the radar.](shot:tank-range){full}

The game takes over the whole screen while you play. Between games it sits
in a small window on the desktop that shows the title, the high scores and
the keys.

## Starting Tank Attack

Open the software disk, then the `GAMES` folder, and double-click
**TANK.O88**. On the Games disk it sits at the top level instead.

![The Tank Attack window, with the high scores and the keys.](shot:tank-window){right}

The window shows the title, the six best scores and a short list of keys.
**PRESS F TO PLAY** blinks under the keys. To start, do any of these:

- press `F` or `Enter`,
- click anywhere in the window,
- choose **Game > Play**.

Press `N` in the window, or choose **Game > New Game**, to throw away a game
you left part way through.

## Driving and shooting

| Key | What it does |
|---|---|
| `Up` or `W` | Drive forward |
| `Down` or `S` | Reverse |
| `Left` or `A` | Turn left |
| `Right` or `D` | Turn right |
| `Space` | Fire a shell |
| `P` | Pause, and press again to carry on |
| `N` | Start a new game |
| `F`, `Esc` or `Alt+Enter` | Leave the game and go back to the desktop |

Hold the driving keys down; you can drive and turn at the same time. A slow
PC draws fewer pictures a second, but the tanks and shells move just as fast
as on a fast one.

Shells stop when they hit a cube, a pyramid or a slab, so the scenery is
cover for you and for the enemy alike.

## Reading the screen

![A shell on its way to the enemy tank.](shot:tank-fire){full}

- **The radar** is the circle at the top. A line sweeps round it, and each
  enemy tank shows as a blip. The top of the circle is straight ahead.
- **ENEMY IN RANGE** lights up when a tank is in front of you and close
  enough for a shell to reach it.
- **The sight** is the pair of brackets in the middle of the view. When a
  shot fired now would hit, the brackets close in round the target and a
  short beep sounds. Fire then.
- **HIGH** is the best score in the table, and **PL 1** is your score.
- **The tank symbols** under the scores are the tanks you have left.

You start with four tanks. Each enemy tank you destroy is worth 1,000
points, and a new one soon comes looking for you.

## The enemy

An enemy tank turns towards you, closes in and fires once it has had time to
take aim. It turns more slowly than you do, so driving round its side is the
way to beat it. When a tank first appears it waits a little before its
first shot.

The first tanks of a game are easy: they wait longer before shooting and
miss more often. Over the first twenty tanks they shoot sooner and aim
better, and then they stay that hard. Losing a tank does not make the
game easier again.

![Hit: the glass cracks.](shot:tank-hit){full}

When an enemy shell hits you, the glass cracks and the world stands still
for a moment. Then you carry on with one tank fewer, and the enemy has to
take aim again, so you have a moment to get away.

## Game over and high scores

When your last tank is hit, the game is over. The words **GAME OVER** fly in
from all sides and settle in the middle of the screen, with your score
under them.

If your score is one of the six best, the game asks for your **NAME**. Type
up to three letters, use `Backspace` to correct them, and press `Enter`.
While you are typing, every key goes into the name, so `Esc` and `F` do not
leave the game.

![The end of a game: press N for a new game or F to go back.](shot:tank-over){full}

![The window after the game, with the new score in the table.](shot:tank-scores){left}

Then press `N` to play again or `F` to go back to the desktop.

The high scores are saved on the disk Tank Attack was started from, in the
file `TANK.HS` in the `SYSTEM\APPDATA` folder, so they are there the next
time you play.

## The Game menu

| Item | Key | What it does |
|---|---|---|
| Play | `F` or `Enter` | Starts the game, or carries on with the one you left |
| New Game | `N` | Throws away the game in progress and starts a new one |

**Tank Attack > About Tank Attack** shows who made it. **Tank Attack >
Close** closes the game.

If the display cannot show the game, the first item reads **Play (no
mode)** and does nothing.

## Display and memory

The game looks the same on every display, with these differences:

- **VGA**: the whole screen, 320 by 240 dots in colour. Scenery is cyan, the
  panel magenta, the sight white and the enemy tanks orange.
- **CGA**: the whole screen, 320 by 200 dots in cyan, magenta and white, the
  colours of the 1983 PC version.
- **Hercules**: a wide 640 by 200 box in the middle of the screen, in black
  and white.

On CGA and Hercules the game needs about 32 KB of free memory for its
picture. If there is not enough, the game does not start and the window
stays on the desktop. Close another program and try again. The copy on the
disks for the smallest 128 KB machine is built to need less.

## Good to know

- Leaving the game with `F` or `Esc` keeps it as it was, and **Play**
  carries on from there. A finished game always starts a new one.
- Each disk keeps its own high-score table.
- The sounds come from the PC speaker.

# Missile Command

Missile Command is a version of Atari's 1980 arcade game. Enemy missiles
rain down on six cities. You defend them from three bases on the ground by
firing counter-missiles that burst in the sky and destroy anything caught
in the blast. The waves, the scoring and the explosions follow the original
arcade game.

![Wave 2 in a window. Four incoming missiles are on their way down to the cities.](shot:missile-window){full}

## Starting Missile Command

Open the disk that holds it, then the `GAMES` folder, and double-click
**MISSILE.O88**. It opens in a large window. You can play there, or go to
full screen (see below). The Mode X screen needs a VGA card; everything
else runs on any display.

## Playing

Each wave opens with **DEFEND YOUR CITIES**, and the missiles start falling a
moment later. A crosshair follows the mouse. Click to fire: a counter-missile
leaves the nearest base that still has missiles and bursts where you clicked.
Any enemy missile that flies into the burst is destroyed.

You can also choose the base yourself. `1`, `2` and `3` fire from the left,
centre and right base at the crosshair, and `Space` fires from the nearest
one, like a click. A base that is empty answers with a short low beep.

The ground runs along the bottom: a base, three cities, a base, three cities
and a base. Each base holds ten counter-missiles. All three bases are rebuilt
and refilled at the start of every wave, even ones that were hit. Cities do
not come back, except through a bonus city.

Aim a little ahead of a missile, because your counter-missile takes time to
get there. A burst close to the ground does no damage and scores nothing.

## What comes down

- **Missiles** fall from the top of the screen. Some split into several partway
  down.
- **A satellite or a bomber** crosses the sky from wave 2 and drops more
  missiles.
- **Smart bombs** arrive from wave 6. They are slower, and they swerve away
  from a burst they see coming.

Every wave is faster than the last, and the colours change every two waves.

## Scoring

The strip across the top shows your score on the left, your best score so
far in yellow in the middle, and the wave and the score multiplier on the
right, such as `W3 x2`.

| You destroy | Points |
|---|---|
| A missile | 25 |
| A satellite or a bomber | 100 |
| A smart bomb | 125 |

All of these are multiplied by the multiplier, which starts at 1 and rises
every two waves, up to 6.

![The bonus count at the end of a wave.](shot:missile-bonus){right}

At the end of a wave the game counts **BONUS POINTS** with a beep for each
item: 5 for every counter-missile you did not use and 100 for every city
still standing, both times the multiplier. Every 10,000 points earns a bonus
city, which appears at the start of the next wave.

The game ends at the end of a wave when no cities are left. It shows
**THE END - N FOR A NEW GAME**. The middle of the strip then shows your best
score.

## Full screen

Press `F` or `Alt+Enter`, or choose **Game > Full Screen**, to play on the
whole screen. The menu bar and the arrow pointer go away and only the
crosshair is left. Press `Esc` or `F` to go back to the window.

![The game in full screen, just after the last city fell.](shot:missile-full){full}

On a VGA card, **Game > Mode X** (or `M`) gives the game a low-resolution
320 by 240 screen of its own. `Esc` or `F` brings you back. On any other
display the item reads **Mode X (Vga)** and does nothing.

## Menus and keys

| Menu item | Key | What it does |
|---|---|---|
| Game > New Game | `N` | Starts again from wave 1 |
| Game > Pause | `P` | Stops the game, or carries on |
| Game > Full Screen | `F` | Plays on the whole screen, or goes back to the window |
| Game > Mode X | `M` | VGA only: the game on its own 320 by 240 screen |
| Missile > About Missile | | Shows who made the game. Click or press a key to put it away |
| Missile > Close | | Closes the game |

| Key | What it does |
|---|---|
| `1`, `2`, `3` | Fire from the left, centre or right base |
| `Space` | Fire from the nearest base |
| `Esc` | Leave full screen or Mode X |

## Good to know

- The game keeps running when you click into another window. Press `P`
  first if you want to stop.
- The game stands still while the About card is showing.
- The best score lasts until you close the game. Nothing is saved to disk.
- The game makes its sounds on the PC speaker.

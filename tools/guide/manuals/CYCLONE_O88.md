# Cyclone 88

Cyclone 88 is a tube shooter after the arcade game Tempest 2000. You are the
claw on the rim of a web that stretches away into the distance. Enemies climb
up the lanes of the web towards you, and you move round the rim and fire
down the lanes to stop them. Clear a wave and you fly down the tube to the
next web.

![A game in progress: the claw on the rim at the left, two shots on their way down the web, and enemies climbing.](shot:cyclone-play){full}

## Starting Cyclone 88

Cyclone 88 is in the `GAMES` folder of the software disk, and at the top
level of the games disk. Double-click **CYCLONE.O88**. It runs in a window
and needs nothing special. It plays on VGA, Hercules and CGA alike.

![The title screen, with the high scores scrolling.](shot:cyclone-title){left}

The window opens on the title screen. A web warps in and out, and the high
scores and a list of the enemies and powerups scroll past underneath.

**Press `Enter` to start**, as the screen says, or click in the window. The
space bar does not start a game.

## Playing

| Key | What it does |
|---|---|
| `Left` / `Right` | Move round the rim, one lane at a time |
| `Space` | Fire down your lane. Hold it to keep firing |
| `Z` | Fire the superzapper |
| `J` | Jump, if you have a jump |
| `P` | Pause, and press again to carry on |
| `N` | Start a new game |
| `F` or `Alt+Enter` | Play full screen, and back |
| `Esc` | Leave full screen |

![How To Play.](shot:cyclone-help){right}

You can aim with the mouse too. Click in the window and the claw moves to the
lane in the direction of the pointer, seen from the middle of the web, and
fires. Hold the button down to keep firing.

The strip at the top shows your score, the level (`LV`), your lives (`x3`),
one `Z` for each superzapper charge, and a letter for each powerup you hold:
`L` for the particle laser, `D` for the AI droid and `J` for a jump.

An enemy that reaches the rim walks along it towards you and fires down at
you. Shoot it first, or jump clear. Losing a life does not restart the wave.
You start with three lives, and can hold up to nine.

## The webs and the enemies

There are eight webs: a circle, a square, a plus, a triangle, a star, a V, a
flat one and a U. Each level takes the next one, and after the eighth they
start again.

New enemies arrive one kind at a time:

| Enemy | From level | Points |
|---|---|---|
| Flipper, climbs the web | 1 | 150 |
| Tanker, splits when shot | 2 | 100 |
| Spiker, builds spikes up its lane | 3 | 50 |
| Fuseball, walks the rim | 5 | 250 |
| Pulsar, electrifies a lane | 7 | 200 |

From level 3 on, enemies are worth more: the points above are doubled on
levels 3 and 4, tripled on 5 and 6, and so on, up to six times as much from
level 11. Each wave you clear adds 100 points for each level number. Every
20,000 points gives you a spare life.

Each wave has more enemies, arriving faster, and they climb faster too.

## Powerups and the superzapper

About one kill in eight leaves a powerup: a hollow box, the only hollow
thing on the web. It drifts up its lane to the rim. Be on that lane when it
arrives, or move onto it just after, to collect it. Each one is also worth
500 points, and its name appears on the screen.

| Powerup | What it does | From level |
|---|---|---|
| ZAPPER CHARGE | One more superzapper charge | 1 |
| JUMP | Press `J` to hop over what is on your lane | 1 |
| PARTICLE LASER | Your shots go through enemies, for a while | 4 |
| AI DROID | A second claw that hunts and shoots on its own, for a while | 6 |
| SPARE LIFE | One more life | 6 |

The superzapper destroys every enemy on the web at once. Every level starts
with one free charge (the screen says **SUPERZAPPER RECHARGE**), and you can
hold up to three.

## Full screen

![Full screen: the same game across the whole display.](shot:cyclone-fullscreen){full}

Press `F` or choose **Game > Full Screen** and the game fills the whole
screen at the same size and colours as the desktop. Press `F` or `Esc` to go
back to the window. In full screen the game draws its own small pointer for
mouse aiming.

## The menus

| Menu | Item | What it does |
|---|---|---|
| Game | New Game | Start a new game |
| Game | Pause | Pause or carry on, like `P` |
| Game | Full Screen | Play full screen, like `F` |
| Help | How To Play | A panel with the keys and the rules |
| Cyclone 88 | About Cyclone 88 | Who made it |
| Cyclone 88 | Close | Close the game |

The How To Play and About panels pause a game in progress. Any key or click
puts them away.

## High scores

The table keeps the five best scores. When you beat one, the game asks for
your **NAME**: type up to three letters or digits, use `Backspace` to
correct them, and press `Enter`. The table is saved as `CYCLONE.HS` in the
`SYSTEM\APPDATA` folder of the disk Cyclone 88 was started from, so the
disk must not be write-protected.

## Good to know

- `Esc` in a window does nothing, so a stray key cannot end your game. Close
  the window to quit.
- Cyclone 88 is for one player.
- The sound effects are simple tones and need no sound card.

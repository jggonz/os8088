# Arkanoid

Arkanoid is a brick-breaking game. You move a paddle along the bottom of the
window and bounce a ball up into a wall of coloured bricks. Break every brick
to clear the wall, and keep the ball from getting past your paddle.

![A rally under way. Some bricks are gone and an L capsule is falling.](shot:arkanoid-window){full}

## Starting Arkanoid

Open the disk that holds it, then the `GAMES` folder, and double-click
**ARKANOID.O88**. The game plays in its own window. It does not use full
screen, and it needs nothing special.

## Playing

A new game starts with the ball resting on the paddle and **SPACE TO SERVE**
on the screen. Press `Space` to send the ball up.

![The start of a game, waiting for the serve.](shot:arkanoid-ready){left}

Move the paddle with the `Left` and `Right` arrow keys. A tap nudges it a
short way and then it stops. Hold the key and it speeds up after a moment.
The mouse does not steer.

Where the ball lands on the paddle sets the angle it leaves at, and so does
the way the paddle is moving when it hits. Hold an arrow key as you serve to
throw the ball harder that way.

The top two rows of bricks have a small white notch. They take two hits:
the first chips the brick and the second breaks it.

The strip across the top shows your score on the left, one small green bar
for each ball you have left in the middle, and the wall number (`LV`) on the
right.

## Capsules

One broken brick in eight drops a capsule, and up to three can fall at once.
Catch one with the paddle to use it. The mark on the capsule says what it
does:

| Capsule | What it does |
|---|---|
| E | Expand: the paddle grows wider, up to a limit |
| C | Catch: the ball sticks to the paddle. Press `Space` to send it off again |
| L | Laser: the paddle grows two red guns. `Space` fires a pair of bolts, and each bolt takes one hit off the brick it meets |
| S | Slow: the ball slows down a notch, down to a lower limit |
| Heart | An extra ball |

Losing a ball takes away every capsule you have caught and puts the ball
back to its normal speed.

## Walls and scoring

| Event | Points |
|---|---|
| A hit that chips a two-hit brick | 5 |
| A brick broken | 10 |
| A capsule caught | 50 |
| A wall cleared | 100 |

![The end of a game.](shot:arkanoid-over){right}

When the last brick goes the game says **WALL CLEARED** and builds the next
wall. Each new wall starts with the ball on the paddle, and the ball gets a
little faster with every wall.

You start with three balls. When the last one is lost the game shows
**GAME OVER**. Press `N` to start again.

## Pausing

![The game paused.](shot:arkanoid-pause){left}

Press `P`, or choose **Game > Pause**, to stop the game. Press `P` or `Space`
to carry on.

The game also pauses on its own when you click another window or the
desktop, so you never lose a ball you were not watching. Coming back does not
restart it: press `Space` when you are ready.

## Menus and keys

| Menu item | Key | What it does |
|---|---|---|
| Game > New Game | `N` | Starts a new game at wall 1 with a score of 0 |
| Game > Pause | `P` | Pauses the game, or carries on |
| Arkanoid > About Arkanoid | | Shows who made the game. Press a key or click to put it away |
| Arkanoid > Close | | Closes the game |

| Key | What it does |
|---|---|
| `Left`, `Right` | Move the paddle |
| `Space` | Serve, release a caught ball, fire the laser, or carry on after a pause |

## Good to know

- `Space` never pauses the game. Only `P`, the menu or clicking away does.
- There is no high score table, and nothing is saved.
- The game makes its sounds on the PC speaker.

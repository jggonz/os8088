# Minesweeper

Minesweeper is the classic mine-finding puzzle. Ten mines are hidden under a
board of 9 by 9 covered squares. Uncover every square that does not hold a
mine and you win. Uncover a mine and the game is over.

![A game in progress: some squares opened, two mines flagged.](shot:mines-window){full}

## Starting Minesweeper

Open the software disk, then the `GAMES` folder, and double-click
**MINES.O88**. On the Games disk it sits at the top level instead. You can
open more than one Minesweeper at a time; each plays its own game.

## Playing

Click a covered square to uncover it. The first square you uncover in a game
is always safe: the mines are only laid after that first click, and never
under it.

An uncovered square shows a number when mines touch it. The number says how
many of its eight neighbours hold a mine. A square with no mines around it
shows nothing, and all its neighbours open by themselves, so one click can
open a whole empty region.

![Flag mode on: the strip says F=FLAG.](shot:mines-flagmode){right}

Use the numbers to work out where the mines must be. When you are sure a
square holds a mine, mark it with a flag:

- **Right-click** a covered square to put a flag on it. Right-click it again
  to take the flag off.
- A flagged square cannot be uncovered by accident. Take the flag off first.
- An uncovered square cannot take a flag.

The number at the left of the strip above the board counts down as you place
flags: it shows ten less the flags on the board. It never goes below 0.

## Flag mode

If your mouse has one button, or you prefer to flag with the left button,
press `F` or choose **Game > Flag Mode**. The strip shows **F=FLAG**, and
every left click now flags or unflags a square instead of uncovering it.
Press `F` again, or pick the item again, to go back to uncovering.

## Winning

![A won game.](shot:mines-won){left}

You win when all 71 safe squares are open. The strip says **YOU WIN!**. The
mines you did not flag stay covered.

After a win the board takes no more clicks. Press `N` or choose
**Game > New Game** to play again.

## Losing

![A lost game: the mine that went off is on red.](shot:mines-lost){right}

If you uncover a mine, the strip says **BOOM! N=NEW** and every mine is
shown. The one you clicked sits on a red square. A flag you placed on a
square with no mine under it shows a mine with a red cross through it, so
you can see which guess was wrong.

The board then takes no more clicks until you press `N` or choose
**Game > New Game**.

## The Game menu

| Item | Key | What it does |
|---|---|---|
| New Game | `N` | Clears the board and starts a fresh game |
| Flag Mode | `F` | Left clicks flag squares instead of uncovering them, until you pick it again |

**Mines > About Mines** shows who made it. **Mines > Close** closes this
game.

## Good to know

- The board is always 9 by 9 with ten mines. There are no other sizes.
- Minesweeper has no clock and keeps no scores, and it saves nothing to
  disk. Closing the window ends the game.
- Any click or key takes the About card down again.

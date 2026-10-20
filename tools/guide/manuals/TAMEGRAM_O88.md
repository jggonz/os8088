# TameGram

TameGram is a falling-block puzzle with a twist: the blocks do not always
fall down. Each new unit appears in the middle of a 32 by 32 square, the
Containment Matrix, and falls toward one of its four edges. Line up eight
squares of one colour and they are purged. TameGram was designed and written
by Jason Page.

![A game in progress: units piled against the edges, the status at the top.](shot:tamegram-window){full}

## Starting TameGram

Open the software disk, then the `GAMES` folder, and double-click
**TAMEGRAM.O88**. On the Games disk it sits at the top level instead. The
game starts as soon as the window opens. You can open more than one TameGram
at a time.

## How the game works

![How to Play.](shot:tamegram-howto){right}

Every unit is four squares in one of seven shapes, and belongs to one of two
**factions**: AMBER (yellow) or GREEN. The **VECTOR** says which way the
current unit falls: NORTH, EAST, SOUTH or WEST. It changes at random with
every new unit, unless you lock it.

A unit falls one step at a time until it lands against the edge of the
matrix or against squares already there. Then the next unit appears in the
middle.

When eight or more squares of the same faction stand in an unbroken line,
across or down, they flash and are **purged**. What is left then slides
along the current vector to fill the gaps, and if that makes new lines of
eight, those go too. One unit can set off a whole chain.

The game ends when a new unit has no room in the middle. The matrix shows
**OVERFLOW - N**: press `N` to start again.

## The status lines

| Shows | What it means |
|---|---|
| THREAT | Your score: 1 for every unit that lands, 10 for every eight squares purged |
| PURGES | How many groups of eight squares you have purged |
| VECTOR | The way the current unit is falling |
| FACTION | The faction of the current unit |
| LOCK | Shown when the vector is locked |

Every fourth purge makes the units fall faster.

## Keys

The arrow keys work by screen direction. An arrow across the way the unit
falls moves it one square that way. An arrow in the direction it falls
pushes it one square further (a soft drop). An arrow against the fall does
nothing.

| Key | What it does |
|---|---|
| Arrow keys | Move sideways, or drop one square faster |
| `A` / `D` | Move one square sideways, whichever way the unit falls |
| `Space`, `Z` or `R` | Rotate the unit |
| `Enter` | Drop the unit all the way at once |
| `P` | Halt the game, and carry on |
| `N` | Start a new game |
| `C` | Colour-blind colours on and off |
| `F` | Lock the vector, and unlock it |

The mouse is only for the menus and for closing the help and About cards.

## Halting, colours and the vector

![Halted: the game stops and says HALT.](shot:tamegram-halt){right}

**Halt** stops the game. The matrix shows **// HALT** and nothing moves
until you halt again. A game that has ended cannot be halted.

**Colour-Blind** swaps the factions' colours for BLUE and ORANGE, and the
status line names them.

**Lock Vector** keeps the current direction for every unit that follows,
until you unlock it. The status shows **LOCK** while it is on.

![Colour-blind colours, with the vector locked.](shot:tamegram-colourblind){left}

## The Protocol menu

| Item | Key | What it does |
|---|---|---|
| New Protocol | `N` | Clears the matrix and starts a new game |
| Halt | `P` | Stops the game; pick it again to carry on |
| Colour-Blind | `C` | Switches between the two sets of faction colours |
| Lock Vector | `F` | Keeps the falling direction fixed, or lets it change again |

## The Help menu

**Help > How to Play** puts the rules and the list of keys over the matrix.
The game waits while the card is up. Any key or click closes it.

**TameGram > About TameGram** shows the credits on the same kind of card.
**TameGram > Close** closes the game.

## Good to know

- TameGram keeps no high scores and saves nothing to disk.
- On a CGA screen the matrix is drawn smaller so the whole board fits in the
  window.

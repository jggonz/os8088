# DrMarco

DrMarco is a capsule-dropping puzzle game based on Nintendo's Dr. Mario for
the NES. The bottle is full of red, yellow and blue viruses. Two-colour
capsules fall from the top, and you steer and turn them so that four or more
cells of one colour line up. A line of four clears, and any virus in it goes
with it. Clear every virus to finish the level.

![A game in progress on VGA: the bottle in the middle, the score panel on the left and the next capsule above DrMarco.](shot:drmarco-play){full}

## Starting DrMarco

DrMarco is on the games disk, at the top level, and in the `GAMES` folder of
the 720KB, 1.2MB and 1.44MB software disks. Double-click **DRMARCO.O88**.

It opens a window with the title screen and its music. The game itself takes
over the whole screen, and that needs a **VGA** or a **CGA** display. On
Hercules you can see the title and help pages and hear the music, and the
title page says **VGA/CGA TO PLAY**.

## The title screen

![The title screen, with the settings row under the menu choices.](shot:drmarco-splash){full}

Before you start, choose how the game will go. The row under the menu
choices shows the current settings.

| Key | What it does |
|---|---|
| `Left` / `Right` | Choose the starting level, 0 to 20 |
| `S` | Choose the speed: LOW, MED or HI |
| `M` | Choose the music: FEVER, CHILL or OFF |
| `H` | Open the help page. `H` or `Esc` comes back |
| `Enter`, `F` or `Alt+Enter` | Start the game |

![The help page.](shot:drmarco-help){right}

You can also click **ENTER START GAME** or **H HOW TO PLAY**. The help page
lists the keys. Click it, or press `H` or `Esc`, to go back.

Level 0 has 4 viruses in the bottle, and each level adds 4 more, up to 84 on
level 20. Higher levels also stack the viruses higher up the bottle.

## Playing

| Key | What it does |
|---|---|
| `Left` / `Right` | Move the capsule. Hold the key to keep moving |
| `Down` | Make the capsule fall faster |
| `Z` | Turn the capsule anticlockwise |
| `X` or `Up` | Turn the capsule clockwise |
| `P` | Pause, and press again to carry on |
| `N` | Start the current level again, with the score back at zero |
| `M` | Change the music |
| `Esc` or `Alt+Enter` | Go back to the desktop |

The **NEXT** box shows the capsule that comes after this one. Viruses never
move. The two halves of a capsule stay joined until one half is cleared. A
half left hanging falls, and pieces that fall can line up and clear again.
Every ten capsules the fall speeds up a little.

When the last virus goes, the panel says **CLEAR!**. Press `Enter` for the
next level. When a new capsule has no room at the top of the bottle, the game
is over; press `Enter` to try the level again as a new game, with the score
back at zero. `P` does nothing on either screen.

The three germs under DrMarco dance while their colour is still in the
bottle. When you clear one of their colour they look dizzy, and when the
last one of that colour goes they disappear.

## Scoring

Each virus you clear is worth 100 points at LOW speed, 200 at MED and 300 at
HI. When one clear takes out several viruses, each one after the first is
worth twice the one before, up to 32 times the starting value. The score
carries on from level to level. There is no high-score table.

## Leaving and coming back

`Esc` takes you back to the desktop with the game paused. In the window,
press `Enter` to carry on where you left off. Changing the level or the
speed on the title screen throws the paused game away, and `Enter` then
starts a fresh one. Changing the music does not.

## Sound

With the sound driver loaded and an AdLib or Sound Blaster card chosen in the
Control Panel, the music plays on the card in three voices with sound effects
beside it. Without a card, the PC speaker plays the tune, and effects take
over the speaker for a moment. Music OFF keeps the effects.

## Good to know

- **DrMarco > About DrMarco** shows the credits. A key or a click puts the
  card away. **DrMarco > Close** closes the game.
- DrMarco is for one player. There is no two-player game.
- DrMarco saves nothing to disk.
- It needs a computer with enough free memory. It does not run on the
  smallest 128KB system disks, which do not carry it.

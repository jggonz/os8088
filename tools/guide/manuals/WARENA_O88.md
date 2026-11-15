# WireArena

WireArena is a first-person arena shooter drawn in text. Thin wall edges
stand on black, the fighters are solid figures, and the whole screen is
characters, which is what lets it run on an IBM PC with a CGA card. Play
alone against up to three bots, or with up to four players on other
machines over a serial cable or a network.

There are eight arenas, six weapons, health and shield pickups, and four
ways to play: free-for-all, team deathmatch, capture the flag and king of
the hill.

![A match on the Heights, with a bot in front of you. Your health is at the bottom left, the weapons and the score at the bottom right.](shot:warena-match){full}

## Starting WireArena

WireArena is in the `GAMES` folder of the 1.44 MB and 1.2 MB software disks
and of the second 720 KB software disk, and at the top of the second 360 KB
games disk. Double-click `WARENA.O88`. The match setup window opens on the
desktop.

It needs the standard system disk. The small system disk for 128 KB
machines refuses it with *Needs kern_big or mega*. A mouse is the best way to
aim, and the arrow keys turn as well. Every display works: CGA, EGA, VGA,
Hercules and MDA.

## Setting up a match

![The match setup window.](shot:warena-setup){right}

The setup window holds every choice. Click a box to drop its list, or use
the keys: Up and Down move between the settings, and Left and Right change
the one marked with `>`.

| Setting | What it chooses |
|---|---|
| Arena | where you play, out of those the mode can use |
| Columns | 40 or 80 characters across |
| Bots | none to three computer players |
| Skill | how well the bots aim and react: easy, normal or hard |
| Play | offline, serial cable, local network or internet |
| Mode | free-for-all, team deathmatch, capture flag or KingOfTheHill |
| Win at | the score that ends the match, or no limit |
| Nick | your name, up to eight characters, for other players to see |

**Play** (or `Enter`) starts the match. **Keys** (or `K`) shows the list of
keys. Columns starts at 40 on an IBM PC or XT and at 80 on a 286 or newer.
A Hercules card always plays at 80.

![The Arena list, dropped.](shot:warena-arenas){left}

The last item of the Arena list, **rotation...** (or `R`), opens the
Rotation window. It lists the arenas the chosen mode can use, each with a
check box. When a match is won, the next one starts in the next checked
arena. At least one arena stays checked (*One arena stays in*).

![The Rotation window for team deathmatch.](shot:warena-rotation){right}

## Playing

Move the mouse to look around and click to fire. `W`, `A`, `S` and `D`
walk and step sideways, `Space` jumps, and the arrow keys turn and look up
and down. `Ctrl` also fires.

Walk over a pickup to take it. A weapon spawn hands out a weapon and its
rounds, health heals you, and a shield takes damage for you. For two
seconds after you appear, or until you fire, nothing can hurt you; the heart
in the corner is cyan while that lasts.

When you die the screen counts down to your respawn. You come back at the
respawn point farthest from the nearest enemy.

Hold `Tab` to see the scoreboard and a map of the arena, with you as a red
dot. `P` pauses a match you are playing alone. `Esc` leaves the match; in a
match with other players it asks first: *Leave the match? Y/N*.

![Tab held: the map of the arena and the scoreboard.](shot:warena-scores){full}

## Weapons

| Key | Weapon |
|---|---|
| `1` | pistol: it never runs out |
| `2` | shotgun |
| `3` | chaingun |
| `4` | grenades: a lob that bounces and goes off on a fuse |
| `5` | rockets |
| `6` | railgun |

`Q` and `E` step to the previous and next weapon you have. `R` reloads.
The numbers at the bottom right show the weapons you carry, the one in your
hand highlighted, and the rounds it has left.

## Ways to play

- **Free-for-all**: every player and bot for themselves. Each kill scores.
- **Team deathmatch**: red against blue. Your team's kills score, and your
  shots and blasts do not hurt a teammate.
- **Capture flag**: take the other team's flag from its base and carry it
  to your own base while your flag is at home. A dropped flag goes home by
  itself after a while, or at once when one of its team touches it.
- **KingOfTheHill**: red against blue for the hill. A team alone on it
  takes it, and the team holding it scores a point every few seconds.

The score against the goal shows at the bottom right, for example `0/20`.
When someone reaches it the winner's name shows for ten seconds, and play
goes on in the next arena of the rotation.

![Capture the flag on the Twin: the blue team at its base, the team scores across the top.](shot:warena-ctf){full}

## The arenas

| Arena | What it is like |
|---|---|
| open | one room with nothing in the way |
| pillars | a room of pillars to hide behind |
| crossing | two corridors that cross, weapons in each corner |
| heights | a tower, a lift up to it and a ramp; the default |
| hedge | low hedges and platforms between two team sides |
| twin | two matching halves, a base on each side |
| the pit | a deep pit with a ring above it and a lift out on each side |
| ziggurat | a tall stepped tower with four ramps to its top |

Some arenas cannot play every mode: capture flag needs bases, and team
deathmatch needs a side for each team. The Arena list offers only the
arenas the chosen mode can use.

## Settings while you play

These keys change how the game looks and feels during a match. Each one
says what it changed at the bottom of the screen for a few seconds, for
example *Detail High*.

![F4 pressed: Detail High at the bottom left.](shot:warena-detail){full}

| Key | What it changes |
|---|---|
| `F1` to `F5` | the detail: Very Low, Low, Medium, High, Very High |
| `-` and `+` | 40 or 80 columns |
| `V` | waits for the screen's refresh before drawing (Vsync on or off) |
| `[` and `]` | how far the view turns for a move of the mouse |
| `F10` | a frame rate counter, then every figure, then off |

Lower detail draws fewer lines and runs faster. The game picks Medium on an
IBM PC or XT and High on a 286 or newer. 40 columns runs faster than 80.

![The same match at 80 columns.](shot:warena-wide){full}

## Playing with others

Up to four people can play one match, each on their own machine. Bots fill
the places the players leave, if the host asks for them.

**Over a serial cable**: connect two machines with a null modem cable on
COM1 or COM2 and set **Play** to *serial cable* on both. Each machine finds
the other by itself, on whichever port the mouse does not use. The **Link**
menu's **Automatic**, **Connect COM1**, **Connect COM2** and **Disconnect**
choose the port by hand. A serial cable links two machines.

**Over a network**: each machine needs a network card. Set **Play** to
*local network* to find the other players on the same network, or to
*internet* and type the host's address in the box beside it.

The players' list shows who is there. Click **Ready** when you are set. The
host is chosen by the game, and its arena, bots, skill and mode are the
match's (*The host picks the arena*). The match starts when every player is
ready. During it, *LINK LOST* means nothing has arrived from another player
for a while, and *OPPONENT LEFT* means they went back to their setup
window.

## Keys

| Key | What it does |
|---|---|
| Mouse | look; click fires |
| `W` `A` `S` `D` | walk forward, left, back, right |
| Arrows | turn, and look up and down |
| `Space` | jump |
| `Ctrl` | fire |
| `1` to `6` | choose a weapon |
| `Q` / `E` | previous / next weapon |
| `R` | reload |
| `Tab` (held) | scoreboard and map |
| `P` | pause, alone only |
| `Esc` | leave the match |
| `F1` to `F5`, `-`/`+`, `V`, `[`/`]`, `F10` | settings, above |

## Good to know

- Nothing is saved between sessions. The setup starts fresh each time you
  open WireArena.
- The serial cable runs at 2400 baud. Keep the mouse on the other port.
- Two players cannot share one machine.
- A match runs full screen. The desktop comes back when you leave it.

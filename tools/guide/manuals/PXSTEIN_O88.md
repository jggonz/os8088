# Pixelstein 3D

Pixelstein 3D is a first-person maze shooter in the style of the 1992
classic. You are locked in a castle of grey and blue stone, wood, brick and
iron, eight floors high. Find your way through each floor, past helmeted
guards and their dogs, open doors with the right keys, and throw the
elevator switch to climb to the next floor. Escape from the top floor to
win.

![Full screen: a corridor on the first floor, with the status bar below.](shot:pxstein-full){full}

You can play in a window on the desktop or full screen. Full screen gives a
bigger picture, in colour where the display has it, and plays faster.

## Starting Pixelstein 3D

Open the Games disk and double-click **PXSTEIN.O88** at its top level. On the
larger software disks it is in the `GAMES` folder.

![The title card in the window, with the best scores.](shot:pxstein-title){full}

The window opens on the title card, with the best scores and these keys:

- `Space` starts a new game on the first floor.
- `F` goes full screen.
- `T` runs the timedemo (see Good to know).
- `C` lets you type a floor's code to start on that floor.

## Moving and fighting

| Key | What it does |
|---|---|
| `Up` or `W` | Walk forward |
| `Down` or `S` | Walk back |
| `Left`, `Right` | Turn |
| `A`, `D` | Step sideways (strafe) |
| `Shift` | Hold to run |
| `Ctrl` | Fire |
| `Space` | Open or close the door ahead, or throw a switch |
| `1`, `2`, `3` | Choose the knife, the pistol or the machine gun |
| `Tab` | Show the map; `Tab` again to play |
| `P` | Pause, and press again to carry on |
| `V` | Change the size of the view |
| `F` | Full screen, or back to the window |
| `Esc` | Leave full screen |

The same keys work in the window and full screen. You slide along walls
rather than stopping dead.

**Weapons.** The pistol fires one shot each time you press `Ctrl`. The
machine gun keeps firing while you hold `Ctrl`. Both use ammunition; with
none left you fight with the knife, which only reaches a guard right in
front of you. You start each floor with eight rounds and can carry 99.

![A guard steps out in the first hall.](shot:pxstein-fight){full}

**Guards** stand watch or patrol. Once one sees you it gives chase, opens
doors to follow you, and shoots. A guard you catch before it has seen you
takes double damage. **Dogs** are faster than you can walk and bite instead
of shooting. A guard is worth 100 points and a dog 200.

**Doors** slide open with `Space` and close again by themselves after a
while. Gold and silver doors stay shut until you have picked up the key of
the same colour. Some walls are **secret doors**: press `Space` against a
suspicious wall to find them.

## Things to pick up

Walk over an item to take it.

| Item | What it gives |
|---|---|
| Ammunition | 8 rounds |
| First-aid kit | 25 health |
| Food | 10 health |
| Gold key, silver key | Opens the doors of that colour on this floor |
| Treasure | 100 points |
| Chalice | 500 points |
| Extra life | One more life, full health and 25 rounds |

Health items stay where they are while your health is full.

## The status bar

Under the view the bar shows the **FLOOR**, your **SCORE**, your **LIVES**,
your face, your **HEALTH** and **AMMO**, the keys you hold and the weapon in
your hand. The face looks worse as your health falls and grins for a moment
when you pick up treasure.

## Floors, lives and the end

![The card at the end of a floor, with the code for the next one.](shot:pxstein-done){full}

Each floor ends at the **elevator switch**. Face it and press `Space`: that
is worth 1,000 points. A card then shows how many of the floor's guards you
killed, secrets you found and treasure you took, your time, and the
**CODE** of the next floor. Write the code down: with `C` on the title card
you can start a new game on that floor. Press `Space` or `Enter` to go on.

You start with three lives and 100 health. When your health runs out the
screen washes red and the floor starts again with full health, eight
rounds, the pistol, no keys and one life fewer. When the last life is gone
the game is over. Throw the switch on the eighth floor and the card says
**YOU ESCAPED**.

If your score is good enough for the table, the game asks for your **NAME**:
type three letters and press `Enter` to save it. The scores are kept on the
disk the game was started from, in the file `PXSTEIN.HS` in the
`SYSTEM\APPDATA` folder.

## The map

Press `Tab` during play to see a map of the floor seen from above, showing
every place you have already seen and you as a marker. The game stands
still while the map is up. Press `Tab` again to carry on.

## Playing in a window

![Playing in the window on a VGA desktop.](shot:pxstein-window){full}

In the window the picture is in black and white on most desktops, with a
line of text under the status bar that says how the picture is drawn. On a
VGA or EGA desktop with a 286 or faster PC, **Detail > Colour: On** gives
the window sixteen colours. On a slower PC that item is greyed and says why.

## The menus

**Game**

| Item | What it does |
|---|---|
| Full Screen | Plays full screen, like `F` |
| Pause | Pauses the game, like `P` |
| Sound: On / Off | Turns the sound effects on or off |
| Mouse: On / Off | With the mouse on, moving the pointer left or right of the middle of the view turns you |

**Mode** lists the full-screen modes this display offers; pick the one to
use. See Display modes below.

**Detail**

| Item | What it does |
|---|---|
| Auto | Chooses the detail for you, and steps down when the game slows |
| Wire | Draws only the edges of the walls: the fastest |
| Flat | Walls in solid colours |
| Textured | Walls with their stone, wood and brick patterns, and proper pictures for guards and items |
| Full res | Draws the full number of columns |
| Low res | Draws half as many columns, each twice as wide: faster |
| Colour | The window's sixteen colours, where the PC can manage them |

**View**

| Item | What it does |
|---|---|
| Size 48, Size 56, Size 64 | How wide the view is. A narrower view draws faster |
| Size 72, Size 80 | Wider views, for full screen only. Use `V` there |
| Rows 80 | The height of the view |

Your choices are saved in the file `PXSTEIN.CFG` in the `SYSTEM\APPDATA`
folder and come back the next time.

**Pixelstein 3D > About Pixelstein 3D** shows the credits. **Pixelstein 3D >
Close** closes the game.

## Display modes

The game picks the best mode for the display, and the **Mode** menu offers a
second one where there is a choice:

- **VGA**: **Mode X**, 320 by 240 in 256 colours, or **CGA 320x200**.
- **CGA**: **CGA 320x200** in four colours, or **CGA 160x100x16**, a coarser
  picture in sixteen colours, on a real CGA card only.
- **EGA**: CGA 320x200.
- **Hercules**: full screen in black and white.

## Speed and memory

![The Detail menu.](shot:pxstein-menu-detail){right}

On a 4.77 MHz PC, Auto starts full screen at Textured with Low res, and the
window at Flat. On a 286 or faster it starts at the best detail.

The textured walls need about 117 KB of free memory. With less, the game
plays Flat and the item reads **Textured (needs 117 KB)**, greyed. If there
is room for the textured walls but not for the pictures of guards and
items, the item reads **Textured (sprites 76 KB)** and they are drawn as
plain boxes. On a machine with 256 KB of memory the game plays Flat, with
boxes for guards and items and no weapon in the view. It does not run on
the smallest 128 KB machine.

## Good to know

- **The timedemo**: press `T` on the title card. The game goes full screen
  and walks a set route along the first corridor by itself, with the guards
  standing still. Then the title card shows how many pictures it drew, how
  long that took and the pictures per second, a fair way to compare one PC
  with another. Press any key to stop it early.
- `F` and `Esc` take you out of full screen without losing your place.
- While the game asks for your name, every key goes into the name.

# Lemmings

Lemmings is the 1991 puzzle game: a trap door drops lemmings into a level
and they walk, fall and drown without a thought until you give one of them a
skill. Make a digger, a builder, a blocker or a bomber at the right moment
and the rest follow the way you made, to the exit. Each level asks you to
save a share of its lemmings against a clock. There are 120 levels in four
ratings, Fun, Tricky, Taxing and Mayhem, and this is the original game
built from its own data files, with the original's screens, pictures and
words.

![The level list: a rating, its levels, and what the chosen level asks for.](shot:lemmings-launcher){full}

## Starting Lemmings

Lemmings has a disk of its own. Open that disk, open the **LEMMINGS**
folder and double-click **LEMMINGS.O88**. The window that opens is the
level list: the four ratings across the top, the levels of the chosen
rating below, and the chosen level's facts on the right. A level in grey is
one this size of disk does not carry; click it and it says which file is
missing and what that file would cost. Beside the program is
[[README.TXT]], the keys and where the game came from, and [[LEVELS.TXT]],
which says the same thing for the whole disk at once.

Choose a level and press `Enter` or click **Play**. The screen becomes the
game's: the level's preview with its picture, then, on a click or `Enter`,
the level itself. When a level ends, the results screen says how many you
saved against how many you needed. Click the left button, or press `Enter`,
for the next level, or to try again if you fell short; click the right
button, or press `Esc`, for the level list.

![A level in play: the terrain above, the skill panel and the map below.](shot:lemmings-play){full}

## Playing

Click a skill button on the panel, then click a lemming to give it that
skill. Hold the right button while you click to pick a lemming that is not
already working. Push the pointer against the left or right edge of the
level to scroll, and click or drag in the map at the bottom right to jump
there. The status line above the panel says what is under the pointer, how
many lemmings are out, what share are in, and the time left.

| Key | What it does |
|---|---|
| `F1` / `F2` | Release rate down / up, while held |
| `F3` to `F10` | The eight skills, in the panel's order |
| `F11` | Pause, and again to carry on |
| `F12`, twice quickly | Nuke: every lemming explodes |
| `Left` / `Right` | Scroll the view |
| `Ctrl`+`Left` / `Right` | Scroll one pixel at a time |
| `Ctrl`+`F1` / `F2` | Release rate to its minimum / maximum, while paused |
| `F` | Leave the full screen for the window, and back again |
| `Esc` | Finish the level now |

The pause and nuke buttons on the panel do the same as `F11` and `F12`, for
a keyboard without them.

## The menu

| Menu | Item | What it does |
|---|---|---|
| Lemmings | About Lemmings | Who made it, and where the full credits are |
| Lemmings | Close | Closes Lemmings |

## Files

Lemmings keeps the level you have reached in **SYSTEM/APPDATA/LEMMINGS.SAV**
on its own disk, and the level list opens on it next time. The **Save
Progress** row says which level that is; on a disk that cannot be written it
is grey and says so.

## Good to know

- The 1.44MB and 1.2MB disks carry all 120 levels. The 720KB disk carries
  116, leaving off the four levels drawn on a special picture. The 360KB
  disk carries two of the five graphic sets and the 53 levels that use
  them; the others are listed in grey.
- On a 4.77 MHz PC or XT the game runs at about four frames a second on a
  quiet level and under one on a crowded one, so a level takes longer than
  it would in 1991 but is no harder: the release rate and the clock slow
  down with it. Loading a level takes about ten seconds there.
- On VGA the level is drawn in its own sixteen colours. On CGA and
  Hercules it is drawn in two colours: one for the terrain and one for the
  lemmings, the objects and the panel. The preview and results screens are
  shorter on CGA and EGA, and the **Mode** row says so.
- The sounds are short tones on the PC speaker, one for each of the
  original's events. There is no music: the original's music is a sound
  driver of its own, not in the game's data, and the **Music** row says so.
- The original's level codes are not here, because the code algorithm lives
  in the original program rather than its data. Progress is kept in a file
  instead; the **Level Code** row says so.

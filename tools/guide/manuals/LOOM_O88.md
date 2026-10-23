# Loom

Loom is where Weave apps are made. A Weave app starts as a few plain text
files: one describes what is in the app's window, one says what happens
when you use it, and, if the app needs them, one holds a spreadsheet's
starting cells and one draws the pictures for a game. Loom edits those
files, checks them, and packs them into the single `.WAB` file that [[WEAVE.O88]]
runs, all on the machine itself.

The quickest way to learn it is to open one of the three demo apps, change
something small, pack it and run it.

![Loom with the Greeter's layout open, and a new line typed under the title.](shot:loom-window){full}

## Starting Loom

Loom is on the Weave disk, in the `LOOM` folder, together with the source
files of the three demo apps. Double-click a `.WML` file, such as
[[FORM.WML]], and Loom opens with that app's files ready to edit.
Double-clicking a `.WJS` file opens its app in the same way. You can also
double-click `LOOM.O88` and use **File > Open Project...**.

Loom wants room to work: about 110KB while you edit, and about 110KB more
for the few seconds a pack takes. A 640KB machine has plenty. On a 256KB machine
Weave can run apps, but Loom will usually not have room to pack them, and
says so: **Pack needs 112KB; the largest free run is 60KB.**

## What an app is made of

An app's files sit together in one folder and share one name: the Greeter
is `FORM.WML` with [[FORM.WJS]] beside it. Only the `.WML` file is required;
it is the one you open.

| File | What it is for |
|---|---|
| `.WML` | The layout: every label, button, field and other part on the app's window, in order |
| `.WJS` | The script: what happens when a button is pressed, a box ticked, a key pressed |
| `.WFX` | The starting cells of a spreadsheet grid |
| `.WSP` | The pictures (sprites) moved around a game area |

If a file named after the `.WML` is missing, Loom also looks for
`MAIN.WJS`, `SHEET.WFX` and `SPRITES.WSP` in the same folder.

### The layout (.WML)

The layout is written as tags, the way a web page is. Here is a whole app
that tosses a coin, in a file called `COIN.WML`:

| Line | What it means |
|---|---|
| `<app name="Coin">` | Starts the app and gives it a name |
| `<card id="main">` | Starts the app's window contents. Every card needs an `id` |
| `<label style="bold">Toss a coin</label>` | A line of bold text |
| `<label id="face" w="10" br="1">Heads</label>` | A label the script can change, 10 characters wide, on a new row |
| `<button id="toss" onclick="flip" br="1">Toss</button>` | A button; pressing it runs the script's `flip` |
| `</card>` | Ends the card |
| `<script src="COIN.WJS"/>` | Names the script file |
| `</app>` | Ends the app |

Parts are laid out left to right, and `br="1"` starts a new row. An `id`
gives a part a name the script can use.

The parts you can put on a card:

| Tag | What it is |
|---|---|
| `label` | One line of text |
| `text` | A paragraph that wraps to fit |
| `rule` | A horizontal line |
| `box` and `spacer` | An empty frame, and empty space |
| `meter` | A bar that fills up, with `value` and `max` |
| `button` | A button, with `onclick` |
| `check` | A tick box, with `checked` and `onchange` |
| `radio` | One of a group of choices, with `group` |
| `input` | A field to type in, with `cols` for its width; `onchange` runs on `Enter` |
| `list` | A list of `item`s to choose from, with `onselect` |
| `grid` | A spreadsheet, with `cols` and `rows` |
| `canvas` | A game area, `w` by `h` pixels, holding `sprite`s |

Any part can also take `w` and `h` (its size in characters), `style="bold"`
or `style="invert"`, `align="center"` or `align="right"`, and `hidden` or
`disabled`. An app can have up to eight cards, and up to four menus made
with `<menu title="Party">` and `<item oncommand="doReset">Reset the
tally</item>`. That is the whole list: there are no fonts, colours or other
styles, except that a game area and its sprites can take colours with
`paper`, `ink` and `color`.

### The script (.WJS)

The script is a list of functions, written in a small version of
JavaScript. Each function runs when the layout says so. This is
`COIN.WJS`:

| Line | What it means |
|---|---|
| `function flip() {` | The function the Toss button names |
| `if (rand(2) == 0) {` | Pick 0 or 1 at random |
| `face.text = "Heads";` | Change the label called `face` |
| `} else {` | Otherwise... |
| `face.text = "Tails";` | ...show Tails |
| `}` and `}` | The end of the `if`, and of the function |

A script can keep values in `var`s, use `if`, `while` and `for`, join text
with `+`, and read and change the parts of the layout: `who.text`,
`count.value`, `loud.checked`, `ball.x`. Numbers are whole numbers from
-32768 to 32767. Some ready-made functions help: `alert("message")` opens a
message box, `timer(ticks, fn)` runs a function a little later (18 ticks to
a second), `tone(freq, ticks)` plays a note on the speaker, `str(n)` turns a
number into text, and `len`, `substr`, `find` and `rand` work on text and
numbers. `saveState()` and `loadState()` keep the script's values on disk
between runs. Lines starting with `//` are notes for yourself.

### The starting cells (.WFX)

A grid's starting cells go one to a line: a cell name, `=`, and then a
number, some text in quotes, or a formula starting with `=`. For a grid
`<grid id="g" cols="2" rows="4" h="8"/>` in `BILLS.WML`, this `BILLS.WFX`
fills it in:

| Line | What it means |
|---|---|
| `# Rent and power, and their total` | A note; Loom ignores lines starting with `#`, and blank lines |
| `A1 = "Rent"` | Text in cell A1 |
| `B1 = 450` | A number in cell B1 |
| `B2 = 62.50` | Numbers can have up to four decimal places |
| `B4 = =SUM(B1:B2)` | A formula: B4 shows the sum of B1 and B2 |

The functions are `SUM`, `MIN`, `MAX`, `AVG` and `COUNT` over a range,
`IF(test, yes, no)`, `ABS` and `ROUND`.

### The sprites (.WSP)

A sprite is drawn with characters: `#` is a dot of colour and `.` lets the
background show through. Each sprite starts with a line giving its name,
width and height; the width must be a multiple of 8. This is a flying
saucer 16 dots wide and 6 high:

| Line |
|---|
| `sprite UFO 16 6` |
| `......####......` |
| `....########....` |
| `################` |
| `.##.##.##.##.##.` |
| `################` |
| `...##......##...` |

A layout then puts it in a game area with `<sprite id="ufo" img="UFO"
x="8" y="20"/>`. A sprite can have up to eight frames, separated by a line
holding only `-`. A `.WSP` file has no notes: a line starting with `#` is
part of a picture.

## Editing

The window has the project's files down the left, the file you are
editing on the right, and a status line along the bottom.

The top line of the list is the project's name. Below it are its four
files: a `-` means the project has no file of that kind, and a `*` means the
file has changes you have not saved. Click a file to edit it, or press
`Ctrl-N` to go to the next one.

![The Greeter's script, one Ctrl-N along.](shot:loom-script){full}

Click in the text to put the caret there, and type. Long lines are not
wrapped; the view slides sideways as the caret moves along them.

| Key | What it does |
|---|---|
| Arrow keys | Move the caret |
| `Home` / `End` | Go to the start or end of the line |
| `Page Up` / `Page Down` | Move a screenful up or down |
| `Backspace` / `Delete` | Delete the character before or after the caret |
| `Enter` | Start a new line |
| `Tab` | Type a tab |
| `Ctrl-S` | Save this file |
| `Ctrl-P` | Pack the app |
| `Ctrl-N` | Go to the project's next file |

The scroll bar on the right moves through a long file. Loom has no
copy and paste and no undo.

## Packing and running

**File > Pack Bundle** (`Ctrl-P`) turns the project into a `.WAB` file. It
saves every changed file first, then checks and packs the lot, and writes
the `.WAB` into the same folder as the sources, with the same name:
`FORM.WML` packs to [[FORM.WAB]]. The status line says **Packing...** while it
works and then, for example, **Packed FORM.WAB, 800 bytes. Reload it in
Weave with ^R.** The list on the left says **Packed** and the size.

![The File menu, about to pack.](shot:loom-menu){right}

To run it, double-click the new `.WAB` in the Disk window, which opens it
in Weave. After that, the loop is quick: change a file in Loom, press
`Ctrl-P`, click the Weave window and press `Ctrl-R`, and Weave runs the new
version.

The demo apps that Weave's own folder holds are left alone. Packing
`FORM.WML` in the `LOOM` folder writes a new `LOOM/FORM.WAB`, and
`WEAVE/FORM.WAB` stays as it was.

![The packed Greeter running in Weave, with its new line under the title.](shot:loom-run){full}

## When a pack fails

Loom checks everything before it writes a bundle: every tag and setting,
every function the layout names, every part the script names. When
something is wrong, nothing is written. The list on the left says **Pack
failed** with the file and line, Loom jumps to that line, and the status
line says what is wrong.

![A tag Weave does not have: Loom stops at line 9 and says so.](shot:loom-error){full}

Fix the line and press `Ctrl-P` again. If you have moved away, click the
file name or the line number under **Pack failed** to jump back to the
mistake.

## Preview

**View > Preview** shows the app's card as Weave will lay it out, without
running anything: buttons do not press and fields do not take typing. A
grid or a game area appears as an empty frame of the right size. The status
line says **Preview: layout and controls only - Weave runs the app**. Choose **View > Edit** to
go back to the text. Preview packs the project to draw it, so a mistake
shows up here just as it would in a pack.

![The Greeter in Preview.](shot:loom-preview){full}

## The File menu

| Item | Key | What it does |
|---|---|---|
| New Project... | | Start a new app: name it, and Loom writes a starter `.WML` and opens it |
| Open Project... | | Open an app by choosing its `.WML` file |
| Save | `Ctrl-S` | Save the file you are editing |
| Save All | | Save every changed file in the project |
| Pack Bundle | `Ctrl-P` | Save, check and pack the app into a `.WAB` |

**New Project...** suggests the name `MAIN.WML`; type another name to use
that instead. If the file is already there, Loom opens it rather than
writing over it. The starter app has one card with one label in it, and it
packs as it stands, so `Ctrl-P` straight away gives you a `.WAB` to open in
Weave.

Save, Save All and Pack Bundle are grey until a project is open.

## The View menu

| Item | What it does |
|---|---|
| Preview | Show the app's card as Weave will draw it. While it is showing, the item reads **Edit** and takes you back |
| Sidebar | Hide or show the list of files on the left |

The **Loom** menu holds **About Loom** and **Close**. If any file has
unsaved changes when you close Loom, it asks whether to save them first,
with **Save**, **Discard** and **Cancel**.

## Files

Loom opens and saves `.WML`, `.WJS`, `.WFX` and `.WSP` files, and writes
`.WAB` files. They are all plain text except the `.WAB`, so any text editor
can read them too. Loom remembers the last project you had open in
`SYSTEM/APPDATA/LOOM.CFG` on the disk it was started from.

## Good to know

- A source file can hold up to 6,144 characters and 400 lines. Loom will not
  open a longer file, and stops typing at the limit with a message.
- In the `LOOM` folder all three demos share one folder, so Loom shows
  `SHEET.WFX` in the list for the Greeter and for Pong as well. They do not
  use it, and it does no harm. A new app of your own is tidiest in a folder
  of its own.
- Keep `WEAVE.O88` and `LOOM.O88` on the disk you work on: a `.WAB`
  opens in Weave, and a `.WML` opens in Loom, from any folder on it.
- Loom draws each key as it is typed. On a slow machine, very fast typing
  can get ahead of it.

# Apple II Plus

The Apple II Plus program is a 48K Apple II Plus in a window, with Applesoft
BASIC and the Autostart Monitor in its ROM. It starts the way the real
machine did: the **APPLE ][** banner and the `]` prompt, and Applesoft answers
what you type.

It shows all three of the Apple's screens: 40 by 24 text, lo-res blocks and
hi-res graphics. You can type BASIC programs, load and save them as `.BAS`
files, paste listings in from other programs, and hear the Apple's speaker.

![The welcome program's first screen.](shot:apple2-window){full}

## Starting the Apple II Plus

The program comes on a disk of its own, in a folder called `APPLE2`. Open the
folder and double-click **APPLE2.O88**. The window's title is **Apple II Plus
Emulator**, and its menu bar is headed **Apple II+**.

The folder also holds [[WELCOME.BAS]], a short demonstration written for
os8088. Double-click it and the Apple starts with the program already
loaded; type `RUN` at the `]` prompt to run it, or `LIST` to read it.
[[README.TXT]] has notes about the program and [[COPYING]] is its licence.

The Apple needs a 49KB block of free memory for its own memory. Its ROMs are
inside APPLE2.O88 itself, so the program still starts wherever you copy it.

## How fast it is

On an IBM PC or XT the Apple runs very slowly. A 4.77 MHz 8088 runs it at
about **half of one per cent** of a real Apple II's speed. It reaches the `]`
prompt in well under a minute and answers each key you type, but something a
real Apple does in a second takes minutes. Running WELCOME.BAS on an XT, the
text screen is up two or three seconds after `RUN`, and the lo-res and hi-res
screens take about a minute and a quarter each. They draw a line at a time,
so you can watch them arrive.

The program is usable on a 386 and comfortable on a 486. The right end of the
status row shows the speed as a percentage of a real Apple, measured every
second; on an XT it reads 0% or 1%.

## Using Applesoft BASIC

Type a line and press `Enter` (the Apple's RETURN). A line with a number in
front is stored as part of the program; a line without one runs at once. Try
`PRINT 2+2`, or:

1. Type `10 PRINT "HELLO"` and press `Enter`.
2. Type `20 GOTO 10` and press `Enter`.
3. Type `RUN` and press `Enter`. Press `Ctrl-C` to stop it.

`LIST` shows the program, `NEW` clears it, `HOME` clears the screen. `GR` and
`HGR` switch to the graphics screens and `TEXT` back.

![Typing at the prompt: a sum, and part of a listing.](shot:apple2-prompt){full}

The Apple II Plus has no lower case, so every letter you type arrives as a
capital.

## The keyboard

| Apple key or action | PC key |
|---|---|
| RETURN | `Enter` |
| Left arrow | `Left arrow` or `Backspace` |
| Right arrow | `Right arrow` |
| ESC | `Esc` |
| CTRL | `Ctrl` |
| RESET (CTRL-RESET) | `Ctrl-F2`, or **Machine > Control-Reset** |
| Open-Apple-CTRL-RESET | `Ctrl-F3`, or the Machine menu item |
| Game buttons 0 and 1 | `F1` and `F2` |

The Apple II Plus has no up or down arrow and no DELETE key, so `Up arrow`,
`Down arrow` and `Delete` do nothing, and neither do the characters it does
not have: the backquote, `{`, `}`, `|` and `~`. To move the cursor around the
screen, use the Apple's own editing keys: `Esc` and then `I`, `J`, `K` or `M`.

`Ctrl-F` and `Alt-Enter` belong to the window (see Full screen and colour), so
the Apple cannot be sent CTRL-F. `Esc` always goes to the Apple.

On a machine with no mouse, os8088 uses the arrow keys and `Space` for its
pointer. Press `Scroll Lock` to give them to the Apple; the status row reminds
you with **ScrollLock: arrows, Space.**

## The screens

The window shows whichever screen the Apple has switched on, and the left end
of the status row names it: **TEXT**, **LORES** or **HIRES**, with **MIXED**
when four lines of text sit under a graphics picture and **PG2** when the
second page is showing.

![The lo-res screen of the welcome program, with four rows of text under it.](shot:apple2-lores){full}

The window is black and white. A lo-res colour is shown as black or white by
how bright it is, and hi-res is shown without its colours. For colour, use
**Machine > Color NTSC** on a VGA screen.

## Loading and saving programs

**File > Load Program...** opens a file dialog. Pick a `.BAS` file and click
**Open**; the program goes into the Apple's memory and you type `RUN`. The file
must be a tokenised Applesoft program, which is what Save Program writes. Any
other file is refused with **Not an Applesoft program.**, and nothing in the
Apple changes.

**File > Save Program...** writes the program in memory to a file, suggesting
the name `PROGRAM.BAS`. A `.BAS` file can be double-clicked to start the
Apple with it loaded.

## Copy and paste

**Edit > Copy** puts the text screen on the clipboard, one line per row, so you
can paste it into [[NOTEPAD.O88]] or any other program.

**Edit > Paste** types the clipboard into the Apple, as fast as the Apple reads
it. Lower case becomes capitals. So a BASIC listing written in Note Pad and
copied can be pasted in and run.

## Full screen and colour

**Machine > Toggle Fullscreen** makes the window fill the screen, still black
and white. On a 286 or faster the picture is magnified: twice the size on
VGA, twice the width on CGA and Hercules. On an 8088 it stays at its own size.

**Machine > Color NTSC** shows the Apple in colour, using the whole screen in
a VGA colour mode: the sixteen lo-res colours, and hi-res colours the way an
Apple on a colour television showed them. It needs a VGA and is greyed on CGA
and Hercules.

In both, press `Ctrl-F` or `Alt-Enter` to go back to the window. There is no
menu bar while they are on, and `Esc` goes to the Apple.

## The File menu

| Item | Key | What it does |
|---|---|---|
| Load Program... | | Load a `.BAS` program file |
| Save Program... | | Save the program in memory as a `.BAS` file |
| Quit | | Close the window |

## The Edit menu

| Item | Key | What it does |
|---|---|---|
| Copy | | Copy the text screen to the clipboard |
| Paste | | Type the clipboard into the Apple |

## The Machine menu

![The Machine menu.](shot:apple2-machine){full}

| Item | Key | What it does |
|---|---|---|
| Open-Apple-Control-Reset | `Ctrl-F3` | Restart the Apple as if it had just been switched on, without clearing its memory. Game button 0 is held down during the restart |
| Control-Reset | `Ctrl-F2` | Press RESET: stop the running program and return to the `]` prompt. The program stays in memory |
| Power On | | Switch the Apple off and on, clearing its memory. It asks first |
| Toggle Fullscreen | `Ctrl-F`, `Alt-Enter` | Fill the screen in black and white, or go back |
| Color NTSC | | Show the Apple in colour on a VGA screen |
| Flashing text | | Show FLASH text flashing. A `*` means it is on |
| Mute | | Silence the speaker, or turn it back on |

![Power On asks before it clears the Apple's memory.](shot:apple2-power){right}

**Power On** asks **Are you sure you want to reboot? (All data will be
lost!)**. Click **Yes** or press `Enter` or `Y` to go ahead; any other key, or
a click anywhere else, leaves the Apple as it was.

Configure Slots... and Joystick... are greyed: there is no Disk II drive in
this version, and the paddles always read the middle of their range.
Louder is greyed because the PC speaker has no volume control. Flashing text
is greyed on an 8088, where redrawing the flashing text would take time away
from the Apple; FLASH text is then shown without flashing.

## The CPU menu

| Item | What it does |
|---|---|
| Normal: 1MHz | Turn Warp off. A `*` appears when the Apple is running at about its real speed |
| Warp | Let the Apple run in longer stretches, for more speed on a 286 or faster. On an 8088 it changes nothing and says so. The speaker is silent while it is on |
| Stop | Stop the Apple's processor. The item then reads **Stopped** |
| Running | Shows a `*` while the processor runs. When it is stopped this item reads **Continue**; pick it to start again |

Fast: 3.5MHz, Step and Next are greyed: there is no 3.5MHz mode (Warp is the
speed control) and no debugger.

## The Apple II+ menu

The menu beside the apple has **About Apple II+**, which shows the program's
name and version, where its parts come from and their licences, and **Close**, which closes the window.

## Good to know

- This is an Apple II Plus, not a IIe: 48K of memory, Applesoft in ROM, no
  80-column card and no double hi-res.
- There is no disk drive yet. Programs come in through Load Program and Edit >
  Paste.
- The speaker plays tones. Beeps and most music work. Sounds that depend on
  the exact waveform, such as sampled speech, do not; the status row says
  **Square-wave tones only.** the first time this matters.
- The paddles always read the middle of their range. The game buttons are `F1`
  and `F2`.
- WELCOME.BAS shows each screen and waits for a key between them; it ends with
  a beep from the speaker.

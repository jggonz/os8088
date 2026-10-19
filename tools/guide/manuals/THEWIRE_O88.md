# The Wire

The Wire is os8088's online software library. It fetches the list of every
program the project publishes from os8088.com and shows each one with its
icon, a picture, a short description and the kind of machine it suits. Pick
one and either run it straight away from memory, without it ever touching a
disk, or add it to a floppy to keep. A machine with a network card and one
360KB drive can reach the whole collection this way.

The Wire needs a network card. This picture shows it on a machine without
one, where it says so and tells you what to do.

![The Wire on a machine with no network card.](shot:thewire-nocard){full}

## Starting The Wire

The Wire is on the system disk, in the `SYSTEM` folder. There are three ways
to open it:

- When the network driver is running, a **Wire** icon appears on the desktop.
  Double-click it.
- Choose **More Sticky Apps...** from the desktop's **Sticky Apps** menu.
- Open the `SYSTEM` folder on the system disk and double-click
  **THEWIRE.O88**.

## What it needs

![The File menu on a machine with no network card.](shot:thewire-file-menu){right}

The Wire needs an NE1000 or NE2000 network card, the [[ETHER.DRV]] driver and
a working connection to the internet. Turn the driver on in **Control Panel >
Drivers**.

Without them, the window opens anyway. The list is empty, both buttons are
greyed, the bottom line reads **Wire connection unavailable**, and the right
side says what to do:

> The Wire needs ETHER.DRV and a network card. Control Panel > Drivers turns
> it on. Or browse the library at os8088.com/wire

After turning the driver on, choose **File > Refresh** to try again. You can
also look through the same library in a web browser at os8088.com/wire.

The Wire does not run on the small 128KB system, which has no network
support.

## Browsing the library

When The Wire opens it connects (**Connecting to Wire...**), fetches the list
(**Loading from the Wire...**), and then the bottom line says how many
programs there are, such as **Available on the Wire: 31 programs**.

The left side is the list: each program's icon and name. The newest ones are
marked **NEW**. Click a name to select it, or use the keyboard:

| Key | What it does |
|---|---|
| Up, Down | The program above or below |
| PgUp, PgDn | A page up or down |
| Home, End | The first or last program |
| `Enter` | Load Program |

The right side shows the selected program in full: a picture (fetched when
you select it, or **No picture**), its name, the machine it is recommended
for and its size, and a few lines about what it does. If your machine is
slower than the one recommended, a line such as **This machine: 8088/8086**
says so. That is advice only; you can still try it.

On a CGA screen there is no room for the picture, so the description is shown
without it.

## Choosing what to show

The **Show:** row along the top narrows the list to programs that suit a
machine:

| Choice | Shows |
|---|---|
| All | Every program |
| 8088/8086 | Programs that suit an original PC or XT |
| 286 | Those, and programs that want a 286 |
| 386 | Everything up to a 386 |
| 486+ | Everything up to a 486 or faster |

## Running a program: Load Program

**Load Program** fetches the program into memory and starts it, exactly as if
you had double-clicked it on a disk. Nothing is written to any disk, so it is
gone when you close it. The bottom line says, for example, **Loaded Browser
from the Wire**.

Some programs cannot be run this way:

- A program that needs extra files of its own beside it says **Needs its
  files on a disk - use Add to Disk**.
- A program that comes as a bundle of several files needs a RAM disk. Load
  Program then sets one up with **RAMDISK.DRV**, unpacks the bundle into it
  and starts the program from there. Without the driver it says **Needs
  RAMDISK.DRV - use Add to Disk**. When memory or the RAM disk is too small it
  says how much more is needed.

## Keeping a program: Add to Disk...

**Add to Disk...** opens the usual Save dialog with the program's file name
filled in. Choose the disk and folder and click Save. The Wire writes the
program, then any extra files it needs, one after another; the bottom line
counts them (**Adding Paint... 1 of 2 files**), and when it is done a message
says the program was **added to your disk**. For a bundle, it makes the
bundle's own folder where you chose and unpacks everything into it.

Before it writes anything, The Wire checks there is room. If there is not, it
says **Not enough room on that disk** and writes nothing. If something goes
wrong part way through, the files already written stay on the disk and the
bottom line names the one that failed.

## When a button is greyed

**Load Program** and **Add to Disk...** grey themselves when they would not
work, and clicking one anyway shows the reason on the bottom line:

| Message | Means |
|---|---|
| Select a program first | Nothing is selected |
| The catalog is not loaded | The list could not be fetched |
| A transfer is already running | Wait for the current fetch to finish |
| Available as a floppy from os8088.com | This one comes only as a whole disk image from the website |
| Needs a newer os8088 - update the system disk | The program is for a newer os8088 than yours |
| Made for an older os8088 | The program is for an older os8088 |
| Needs a newer Wire - update the system disk | This program needs a newer version of The Wire |

## The File menu

| Item | What it does |
|---|---|
| Refresh | Fetches the list again from os8088.com |
| Load Program | Runs the selected program from memory |
| Add to Disk... | Saves the selected program to a disk |
| Close | Closes The Wire |

The **The Wire** menu at the left of the bar has **About The Wire** and
**Close**.

The About card also shows the date of the list it fetched, or question marks
when there is none yet.

![The About card, before any list has been fetched.](shot:thewire-about){full}

## When something goes wrong

| Message | Means |
|---|---|
| The Wire stopped answering | The connection broke during a fetch |
| The Wire refused the connection | The server would not let The Wire connect |
| The Wire did not answer (404) | The server answered with an error; the number is its code |
| Catalog not understood | The list arrived but could not be read |
| Not enough memory for that program | Close something and try again |
| That program would not start (N) | The program arrived but would not start |
| Made for another os8088 version | The program does not fit this version of os8088 |

## Files

The Wire writes nothing unless you use **Add to Disk...**. It reads one
optional file, `SYSTEM\APPDATA\WIRE.CFG` on the disk it started from: a single
line naming a different server, such as `myserver` or
`myserver:8080/wire/` (the port and the folder are optional). Without the
file it uses os8088.com.

## Good to know

- A program run with Load Program starts in the folder The Wire is looking
  at, and is not saved anywhere. Use Add to Disk... for anything you want to
  keep.
- Pictures are fetched one at a time as you select programs. Moving on before
  one arrives simply drops it.
- The list and the programs come over a plain web connection to os8088.com.

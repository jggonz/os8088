# RunCPM

RunCPM is a CP/M 2.2 computer in a window. It emulates a Z80 processor with
64KB of memory, runs Digital Research's own command processor, and shows an
80 by 25 character terminal. CP/M was the standard operating system of the
small business computers that came before the IBM PC, and thousands of
programs were written for it.

Its disk comes with the CP/M master disk (the standard commands, Microsoft
BASIC, an assembler, a text editor and more). The larger disks add arcade
games, dungeon crawlers, WordStar and Turbo Pascal.

![RunCPM after starting, with a listing of the programs on drive A.](shot:runcpm-window){full}

## Starting RunCPM

RunCPM comes on a disk of its own, in four sizes. **RUNCPM.O88** is at the
top of the disk, beside a folder called `A` that holds CP/M's drive A.
Double-click **RUNCPM.O88**.

RunCPM needs 64KB of free memory for the Z80, and a little more for the
files it has open. If there is not enough, it does not start, and a message
on the menu bar says how much it asked for and how much the largest free
block is.

The window prints RunCPM's banner, then the command processor's prompt,
`A>`. The banner includes a line such as **Estimated Z80 clock speed: 0.19
MHz**: RunCPM measures how fast your machine runs it, each time it starts.

## How fast it is

Be ready for it to be slow on an IBM PC or XT. On a 4.77 MHz 8088 the Z80
runs at about 0.19 MHz, against the 2 to 4 MHz of a real CP/M machine. That
is roughly a tenth to a twentieth of the speed. Commands such as `DIR`,
`TYPE` and BASIC work, at a pace you can watch. Programs that compute a lot,
such as the assembler, take minutes.

On a 286 or 386 RunCPM is fast and comfortable to use. Nothing slows it down
to a real machine's speed, so on a fast machine an arcade game can run too
fast to play. Most of those games have a speed setting of their own.

## Using CP/M

Type a command at the `A>` prompt and press `Enter`. Some commands to start
with:

| Type | What it does |
|---|---|
| `DIR` | List the files in the current drive and user area |
| `DIR *.COM` | List only the programs |
| `TYPE NAME.TXT` | Show a text file on the screen |
| `ERA NAME.TXT` | Delete a file |
| `REN NEW.TXT=OLD.TXT` | Rename a file |
| `USER 5` | Change to user area 5 |
| `MBASIC` | Start Microsoft BASIC-85 (`SYSTEM` goes back to CP/M) |
| `TE NAME.TXT` | Edit a text file in the full-screen text editor |
| `EXIT` | Leave RunCPM and close its window |

A program is any file ending in `.COM`. Type its name without the `.COM` to
run it. `Ctrl-C` at the prompt restarts the command processor, and many programs
stop when you press it.

![Microsoft BASIC-85 running a short program.](shot:runcpm-mbasic){full}

The disk carries [[1STREAD.ME]], RunCPM's own notes on what is on the master
disk. Type `TYPE 1STREAD.ME` to read it. A file called `LEFT-OFF.TXT` names
the master disk's files that did not fit on this size of disk.

## Drives and user areas

CP/M drives are folders on the floppy. Drive A is the folder `A`, and inside
it each user area is a folder of its own: user area 0 is `A\0`, user area 5 is
`A\5`, and so on. The master disk is in user area 0.

`USER n` (0 to 15) changes the user area. The prompt stays `A>`, so use `DIR`
to see where you are. A new user area's folder is made the first time you
change to it. Only drive A exists at first; `B:` answers **Bdos Err on B:
Select**. The `FORMAT` program makes a new drive folder.

Because the drives are ordinary folders, you can copy a CP/M program onto the
floppy from os8088's Disk window or from a PC, into `A\0`, and run it in
RunCPM.

## The games and applications

The larger disks carry software from the RunCPM software collection, each in
a user area of drive A. Type `USER` and the number, then `DIR`.

| Type | What is there | On which disks |
|---|---|---|
| `USER 5` | Yahoo Software's arcade games: LADDER, CATCHUM and PM | 720KB, 1.2MB, 1.44MB |
| `USER 6` | Dungeon crawlers: NEMESIS, DM (Dungeon Master) and CASTLE | 1.2MB, 1.44MB |
| `USER 7` | GAINA, a video game | 1.2MB, 1.44MB |
| `USER 8` | Borland's Turbo Pascal 3.01A (type `TURBO`) | 1.44MB |
| `USER 9` | MicroPro WordStar 3.30 (type `WS`) | 1.2MB, 1.44MB |

The 360KB disk carries the master disk's programs and no games. Each disk has
a file called `GAMES.TXT` in user area 0 that lists what is on it; type
`TYPE GAMES.TXT`.

![The arcade games in user area 5 of the 1.44MB disk.](shot:runcpm-user){full}

**Tell a game what terminal it is on before you play it.** Run its setup
program once (`LADCONF`, `CATCONF`, `TINST` for Turbo Pascal, `WINSTALL` for
WordStar) and choose **10) Heathkit/Zenith H19 (ANSI)**. Do not choose the
DEC VT-100 entry: LADDER and CATCHUM send their screen positions wrongly with
it. Turbo Pascal needs no setup and starts with `Terminal: ANSI`.

![Turbo Pascal starting in user area 8.](shot:runcpm-turbo){full}

## The terminal

The terminal understands the cursor movement and screen clearing of a VT100,
which is what most CP/M programs expect. It shows reverse video. It does not
show bold, underline or colour.

In a window on a 640-pixel-wide screen, RunCPM shows 79 of the 80 columns,
and on a CGA screen 17 of the 25 rows. Press `Alt-F` or `Alt-Enter` to make
the window fill the screen; then all 80 columns and 25 rows show on every
display. Press the same key again to go back. While it fills the screen,
messages that would go to the menu bar are printed on the terminal instead.

## Keys

Keys with a character send it to CP/M as they are: `Ctrl` with a letter sends
the control code, `Enter` is a carriage return, `Esc` is escape. The other
keys send what a VT100 terminal sends for them:

| Key | What CP/M receives |
|---|---|
| Arrow keys | The VT100 cursor keys |
| `Home`, `End`, `Page Up`, `Page Down`, `Insert`, `Delete` | The VT100 codes for those keys |
| `Ctrl-Backspace` | DEL (character 127) |
| `Ctrl-2` | NUL (character 0) |
| `Alt-F` or `Alt-Enter` | Fill the screen, or go back to a window (RunCPM itself) |
| `Alt-L` | Ask for the name of a `.COM` program and run it straight away (RunCPM itself) |
| `Alt-C` | Show a count of screen updates on the menu bar, a tool for developers (RunCPM itself) |

When you type a command line, CP/M's line editor understands these keys:

| Key | What it does |
|---|---|
| `Ctrl-A` / `Ctrl-F` | Move left / right |
| `Ctrl-B` | Go to the start or the end of the line |
| `Backspace` (or `Ctrl-H`) | Delete the character before the cursor |
| `Ctrl-G` | Delete the character at the cursor |
| `Ctrl-K` | Delete to the end of the line |
| `Ctrl-X` | Delete to the start of the line |
| `Ctrl-U` | Delete the whole line |
| `Ctrl-R` | Type the line out again |
| `Ctrl-W` | Bring back the last line you typed |
| `Ctrl-E` | Start a new screen line without ending the command |
| `Ctrl-C` | Abandon the line |
| `Ctrl--` (Ctrl and minus) | Show this list on the screen |

## The RunCPM menu

RunCPM has no menus of its own, only the one with its name.

| Item | What it does |
|---|---|
| About RunCPM | Shows the program's name, version and licence. The Z80 pauses while it is up; press `Enter`, `Esc` or `Space`, or click **OK** |
| Close | Closes the window |

## Files

A file you save in CP/M is saved on the floppy, in the folder of its drive
and user area. Text that a program sends to the printer goes into the file
`LST.TXT` in `A\0`, and punch output into `PUN.TXT`.

If a file named `AUTOEXEC.TXT` is beside RUNCPM.O88, the command in it is
typed for you every time CP/M starts.

## Good to know

- A file is written back to the floppy when the program closes it, or when
  the program ends. If you close the window while a program still has a
  changed file open, that change is lost. Leave the program first, or type
  `EXIT`.
- Files larger than 65,535 bytes cannot be opened, and a program can have at
  most eight files open at once. This is why Zork, The Hitchhiker's Guide to
  the Galaxy and Colossal Cave Adventure are not on the disks.
- A folder on the floppy cannot grow, so each user area was made with room
  for new files. A full disk refuses to save, and RunCPM says so on the menu
  bar.
- RunCPM has no serial port, so XMODEM cannot send or receive.
- RunCPM is CP/M 2.2. Programs that need CP/M 3 do not run.

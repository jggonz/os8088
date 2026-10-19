# DOS

DOS runs MS-DOS programs, the `.COM` and `.EXE` files written for the IBM PC.
It does not imitate a processor: your machine is already the kind of computer
those programs were written for, so DOS gives each program the memory, files
and services it expects from MS-DOS and lets it run at full speed.

The DOS window is also a command prompt. You can list folders, copy and
rename files, change drives and start programs by typing, the way you would
at an MS-DOS prompt. When a program ends, what it left on its screen stays in
the window, followed by a line that says how it ended.

![The DOS window after running OS88NET.COM with the argument /?. The program's help text stayed in the console when it ended.](shot:dos-window){full}

## Starting DOS

DOS is **DOS.O88**, in the `APPS` folder of the system disk. There are three
ways to use it:

- **Double-click a `.COM` or `.EXE` file** in any Disk window. DOS opens and
  runs that program at once. This works for a program on any disk, because
  the system knows that DOS is the program for those files.
- **Double-click a `.LNK` shortcut** that DOS saved. It runs the program with
  the arguments and settings stored in the shortcut.
- **Double-click DOS.O88** itself. The window opens at the prompt, and you
  type the name of a program or a command.

DOS needs a machine with the full system (it is not on the disks for the
smallest, 128KB machines). A Sound Blaster, a hard disk and a network card
are all optional; a program that wants one can use it when it is there.

## The window

Across the top is the **command box**. It holds the full path of the program
the window runs, such as `B:\SYSTEM\DOS\OS88NET.COM`. Beside it are two
buttons:

- **Setup** opens the settings for the next run: arguments, environment and
  memory (see below).
- **Run** runs the program the command box names.

You can type in the command box yourself. Type a path, a space and the
program's arguments, such as `B:\GAMES\FOO.COM /M`, and press `Enter` or
click **Run**. DOS moves the arguments into the Arguments field on the Setup
page and runs the program.

Under the bar is the **console**, an 80-column text screen with a prompt in
it. On VGA and Hercules it shows 25 lines; on CGA it shows 17. Click in the
console to type at the prompt. A fresh window starts with the caret in the
command box, so click the console first.

## Running a program

At the prompt, type the program's name and press `Enter`. You do not need to
type `.COM` or `.EXE`: a name on its own is looked for as `.COM` first, then
`.EXE`, in the current folder. Anything after the name is passed to the
program as its arguments, so `os88net /?` runs OS88NET.COM with `/?`.

A running program has the whole machine. The screen switches to whatever
mode the program uses, the desktop is put away, and every key goes to the
program, `Esc` included. When the program ends, the desktop comes back and
the program's last text screen is copied into the console. The last line
says how it went, for example:

`B:\SYSTEM\DOS\os88net.COM ended, exit code 000 (Arena: 459KB)`

The exit code is the number the program handed back when it finished, and
the arena is the memory the program was given.

![OS88NET.COM run with no arguments. With no parallel port to use, it says so and ends.](shot:dos-ended){full}

To run the same program again, press `Enter` with nothing typed, or click
**Run**.

## Commands at the prompt

Type `help` for a list. These are the commands the prompt understands:

| Command | What it does |
|---|---|
| `CD [path]` | Changes folder. `CD ..` goes up one level, and a path such as `CD \SYSTEM\DOS` goes down as many as it names. A folder that is not there answers `Path not found`, and you stay where you were |
| `COPY a b` | Copies a file. Wildcards such as `*.*` are allowed |
| `DEL name` | Deletes a file. `ERASE` is the same |
| `DIR [/P] [/W]` | Lists this folder. `/P` stops after each screenful, `/W` shows names only, in columns |
| `ECHO text` | Prints the text |
| `MD name`, `RD name` | Makes or removes a folder. `MKDIR` and `RMDIR` are the same |
| `MOVE a b` | Moves or renames a file |
| `OPEN file` | Opens a document with the program that claims it, as a double-click would |
| `REM text` | A note. Does nothing |
| `REN old new` | Renames a file. `RENAME` is the same |
| `TYPE file` | Prints a text file |
| `VER` | Says what this is: `os8088 DOS Version 3.31` |
| `XCOPY a b` | Copies a folder and everything in it |
| `A:` | A drive letter on its own changes drive |
| `NAME [arg]` | Runs a program, or opens a document with it |

![HELP lists every command, one line each.](shot:dos-help){full}

`DIR` lists names, sizes and folders, with the number of files at the end.
It shows no dates, and it lists files in the order the disk holds them.

Typing the name of an os8088 program in the current folder, such as
`CALC.O88` or just `CALC`, opens that program in its own window on the desktop, the same as
double-clicking it. Typing a program name and then a document, such as
`NOTEPAD README.TXT`, opens the document with that program.

Each drive remembers its own current folder. After `B:`, `CD APPS`, `A:` and
`B:` again, you are back in `B:\APPS`. A drive that is not there answers
`Invalid drive specification`, and you stay where you were.

![VER, and a redirection to a file, which the prompt refuses.](shot:dos-ver){full}

You can add `> NUL` to the end of a command to hide what it prints. Sending
the output to a file, such as `DIR > LIST.TXT`, is refused with `Cannot
redirect to that file`, and the command does not run.

A name the prompt does not know, and that is not a program in the current
folder, answers `Bad command or file name`, with or without anything typed
after it.

## Full screen

**Program > Full Screen**, or `Alt+Enter`, puts the console on the whole
text screen, all 80 columns and 25 lines. This is useful on CGA, where the
window shows only 17 lines. Press `Esc` or `Alt+Enter` to come back to the
window. The prompt works the same in both places.

![The console on the full text screen. The corner says how to leave.](shot:dos-fullscreen){full}

## Setup

**Setup**, on the bar or in the **Program** menu, opens the settings that
apply to the next run. The command box sits at the top of the page, so you
can see and change what will run. **Return** goes back to the console.

![The Setup page, with the memory choices on the right.](shot:dos-setup){full}

**Arguments** is the text the program receives after its name, up to 127
characters. Pressing `Enter` here runs the program again with the new
arguments, which is handy when a program tells you to try a different
option.

**Environment** has four rows, each one `NAME=VALUE`, for programs that read
settings from the environment, such as `SOUND=C:\SB` or `MTCPCFG=C:\MTCP.CFG`.
A row that is empty, or has no `=`, is left out. On a machine with a Sound
Blaster, DOS adds a `BLASTER=` line by itself, describing the card.

The right half is about memory. **Est DOS Ram (Current Machine)** is how much
memory the program will get with the settings as they stand. It changes as
you change them.

**Disk cache** decides how much of that memory DOS keeps for speeding up
disk reads: **Auto**, **32K**, **18K**, **9K** or **Off (SLOW!)**. A program
that reads the disk a lot runs faster with a cache. A program that needs
every kilobyte can turn it down.

The two choices below it say where the program runs:

- **Inside the OS** is the normal choice. os8088 stays loaded, and when the
  program ends you are back at the desktop exactly as you left it. Under it,
  **Hard drives** and **Network** keep those drivers loaded for the program.
  Clear one to unload that driver while the program runs and give its
  memory to the program; it comes back afterwards. The figure beside each is
  the most memory that kind of driver can take. **Limit** caps the memory the program is
  given, in kilobytes. Leave it empty to give it all there is.
- **Shut down the OS** unloads os8088 itself and gives the program nearly
  the whole machine: on a 640KB PC, roughly 150KB more than Inside the OS.
  On a machine with a hard disk, os8088 saves your session to the disk first
  and puts it back, windows and all, when the program ends. On a machine
  without one there is nothing to come back to, so DOS asks first: **Open
  windows are lost. Proceed?** Choose **Proceed** only after saving your
  work; the machine restarts when the program ends. **Disable the mouse**
  turns the mouse off for the program, which saves a little time on a slow
  machine when the program does not use it.

The options under the choice that is not picked are greyed. Clicking one of
them picks its choice.

**Save Shortcut** writes these settings, the program, its arguments and its
environment into a `.LNK` file, using the standard Save dialog. Double-click
the shortcut later to run the program the same way. A plain double-click on
the program itself always uses the default settings.

## The Program menu

| Item | Key | What it does |
|---|---|---|
| Run | `Enter` in the command box | Runs the program the command box names |
| Setup | | Opens the Setup page |
| Full Screen | `Alt+Enter` | Puts the console on the whole text screen |

![The Program menu.](shot:dos-menu){right}

The **DOS** menu has **About DOS** and **Close**, which closes this window.

## What works, and what does not

DOS runs ordinary real-mode programs: text programs, utilities, and games
that draw their own screens, play their own sound on a Sound Blaster or the
PC speaker, and read the mouse. It tells programs it is DOS version 3.31.
It also offers extended memory to programs that look for an XMS driver, and,
on a machine with a network card, a packet driver for programs such as mTCP
that bring their own networking.

Some things are not there:

- There is no batch file support, so `.BAT` files do not run.
- There is no `COMMAND.COM` file. A program that starts a command shell to
  copy or delete files gets the prompt's own commands instead, which is
  enough for many installers.
- A program cannot stay resident after it exits. When it ends, it is gone.
- When a program asks for something DOS does not provide, the console says
  which request it was, for example `It asked for INT 21h AH=57h.`
- A program needs a processor that runs it. On an 8088, a program written
  for a 286 or later will not work.

If a program does not fit, the console says `Not enough RAM - try Setup,
"Shut down the OS".` Close other windows, lower the disk cache, or choose
**Shut down the OS**.

## Files and drives

Drive letters are the desktop's own: `A:` and `B:` are the floppy drives,
`C:` is the hard disk when there is one, and `D:` is next. A machine with no
hard disk has `A:`, `B:` and then a gap. A program starts in the folder it
was launched from, and can reach every drive.

## Good to know

- Keep a copy of anything a program will change. A DOS program has the whole
  machine while it runs and can do anything the hardware allows.
- `Esc` belongs to the running program. To leave a program, use its own way
  out.
- [[OS88NET.COM]], in `SYSTEM\DOS` on the apps disk, is a DOS program for
  another PC: run on an MS-DOS machine joined to this one by a parallel
  cable, it shares that machine's files as a drive here.

C64 - a Commodore 64 for os8088
===============================

C64.O88 is a windowed Commodore 64: a 6510 in a 64KB heap claim, a VIC-II and
two CIAs, and the 320x200 screen composed into 1bpp bands. It is a
reimplementation of VICE 3.10's `x64` machine for os8088.

TWO FILES, ONE FOLDER
---------------------
C64.O88   the package - and everything it needs is INSIDE it: the KERNAL,
          BASIC and character ROMs (20,480 bytes) and its second segment
          (the menu commands, the loader, the About panel), both appended
          past the program and read at launch before anything else runs
COPYING   the GNU General Public License, version 2 - the licence this
          package is under. It is not read by the program; it is here
          because the licence has to travel with the binary.

C64.ROM and C64.OVL used to be files of their own beside the package, and a
copy that took the program and left either behind was a machine that could
not start or a program whose every menu command refused. Neither can be left
behind now.

LICENCE
-------
VICE is Copyright (C) 1996-2025 the VICE team and is distributed under the GNU
General Public License, version 2 or later. This package is derived from VICE
and is therefore GPL-2-or-later. The full licence text is COPYING, on this
disk beside the package, and apps/c64/COPYING in the os8088 source tree.

THE ROMS ARE NOT GPL AND ARE NOT OURS
-------------------------------------
The KERNAL, BASIC and character generator images inside C64.O88 are
Copyright (C) Commodore Business Machines. They are included exactly as VICE
distributes them.

WHAT THIS MACHINE IS NOT
------------------------
There is no 1541, no datasette and no cartridge port. File > Attach disk
image... puts a .D64 on drive 8, and the KERNAL's LOAD is served from it:
LOAD"$",8 lists it, LOAD"NAME",8 and LOAD"*",8,1 load from it. That is not a
real drive - a program with its own fast loader, or one that talks to the
drive directly, finds nothing there. With no disk, drive 8 is the folder
the C64 stands in: SAVE"NAME",8 writes NAME.PRG there, LOAD"NAME",8 reads
it back and LOAD"$",8 lists the folder's .PRG files. SAVE goes to the
folder even with a disk attached, and LOAD looks there for a name the disk
has not got. Any other device answers as nothing on the serial port does:

    LOAD"*",9

    SEARCHING FOR *
    ?DEVICE NOT PRESENT  ERROR
    READY.

Programs are loaded with File > Smart attach... from a .PRG file. On a VGA
display, Preferences > Fullscreen (Alt+D) is the C64 in colour, 320x200 in
VGA mode 13h; Alt+D again comes back. The WINDOW is monochrome on every
adapter, VGA included, resolved from the C64's 16 colours by luminance: text crisply, bitmap and multicolour pictures as a
dither, sprites lit or dark against the paper. A split screen is drawn split
by character row. Sprites collide (Preferences > Sprite collisions) and can
be behind the picture.

COPY AND PASTE
--------------
Edit > Copy puts the 40x25 screen on the system clipboard, so it can be
pasted into Note Pad or anywhere else. Edit > Paste types the clipboard into
the machine through the KERNAL's own keyboard buffer - ten characters at a
time, and only while the machine has read the last ten, which is the pace a
real one types at. A long paste therefore takes as long as the machine takes
to read it, and a program that stops reading stops the paste.

A paste is limited to the first 2048 characters - about fifty lines of a
BASIC listing. A longer clipboard is pasted as far as it fits and the status
row says so.

Both items are greyed when there is no machine to use them on: with no
C64.ROM on the disk, and - for Paste - after a JAM or while the machine is
PAUSED, because in none of those does anything type what it queued. Copy
stays available on a jammed or paused machine: the picture on the screen is
real.

Three things about it are the emulator's answer and not a defect. Copied text
comes back in LOWER case, because PETSCII 65..90 is the lower case whichever
character set the screen is showing. Pasted text should be in lower case for
the same reason: type `print 2+2` into Note Pad, not `PRINT 2+2`, or the
machine receives the shifted letters, which are graphics characters. And a
graphics character on the screen copies as a full stop, which is the
substitution the original emulator makes for anything it cannot spell in
plain ASCII.

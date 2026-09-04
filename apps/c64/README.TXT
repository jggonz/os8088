C64 - a Commodore 64 for os8088
===============================

C64.O88 is a windowed Commodore 64: a 6510 in a 64KB heap claim, a VIC-II and
two CIAs, and the 320x200 screen composed into 1bpp bands. It is a
reimplementation of VICE 3.10's `x64` machine for os8088.

THREE FILES, ONE FOLDER
-----------------------
C64.O88   the package - and the KERNAL, BASIC and character ROMs are INSIDE
          it, 20,480 bytes appended past the program and claimed and read at
          launch before anything else runs
C64.OVL   its second segment - the menu commands, the loader, the About panel
COPYING   the GNU General Public License, version 2 - the licence this
          package is under. It is not read by the program; it is here
          because the licence has to travel with the binary.

The first two must be in the SAME folder: the .OVL is resolved in the
launching instance's current directory. A disk with no C64.OVL is a program
whose every menu command refuses, politely.

C64.ROM used to be a fourth file, read at launch from beside the package - and
a copy that took the program and left it behind was a machine that could not
start. It cannot be left behind now.

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
There is no 1541, no datasette and no cartridge port, so a LOAD from drive 8
answers the way a C64 with nothing plugged into the serial port answers -
which is the honest machine, not a defect. Typed at this machine, verbatim:

    LOAD"*",8

    SEARCHING FOR *
    ?DEVICE NOT PRESENT  ERROR
    READY.

Programs are loaded with File > Smart attach... from a .PRG file. The screen
is monochrome on every adapter, VGA included, resolved from the C64's 16
colours by luminance. Every menu item that cannot work in this build is
present, greyed, and names the fact that greys it.

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

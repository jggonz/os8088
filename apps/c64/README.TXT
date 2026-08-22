C64 - a Commodore 64 for os8088
===============================

C64.O88 is a windowed Commodore 64: a 6510 in a 64KB heap claim, a VIC-II and
two CIAs, and the 320x200 screen composed into 1bpp bands. It is a
reimplementation of VICE 3.10's `x64` machine for os8088.

FOUR FILES, ONE FOLDER
----------------------
C64.O88   the package
C64.OVL   its second segment - the menu commands, the loader, the About panel
C64.ROM   the KERNAL, BASIC and character ROMs, read at launch
COPYING   the GNU General Public License, version 2 - the licence this
          package is under. It is not read by the program; it is here
          because the licence has to travel with the binary.

The first three must be in the SAME folder: the .OVL is resolved in the launching
instance's current directory, and C64.ROM is read from there too. A disk with
the package and no C64.ROM is a program that refuses at launch and says so; a
disk with no C64.OVL is a program whose every menu command refuses, politely.

LICENCE
-------
VICE is Copyright (C) 1996-2025 the VICE team and is distributed under the GNU
General Public License, version 2 or later. This package is derived from VICE
and is therefore GPL-2-or-later. The full licence text is COPYING, on this
disk beside the package, and apps/c64/COPYING in the os8088 source tree.

THE ROMS ARE NOT GPL AND ARE NOT OURS
-------------------------------------
The KERNAL, BASIC and character generator images inside C64.ROM are
Copyright (C) Commodore Business Machines. They are included exactly as VICE
distributes them.

WHAT THIS MACHINE IS NOT
------------------------
There is no 1541, no datasette and no cartridge port, so LOAD"*",8 answers
with the ?DEVICE NOT PRESENT error - which is the honest machine, not a
defect. (What the screen prints is the KERNAL's own wording, and it is not
quoted here until wave 2 runs the ROM and it can be transcribed rather than
remembered.) Programs are
loaded with File > Smart attach... from a .PRG file. The screen is monochrome
on every adapter, VGA included, resolved from the C64's 16 colours by
luminance. Every menu item that cannot work in this build is present, greyed,
and names the fact that greys it.

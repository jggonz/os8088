APPLE2 - an Apple II Plus for os8088
===================================

APPLE2.O88 is a windowed 48K Apple II Plus: a 6502 in a 64KB heap claim,
Applesoft BASIC and the Autostart Monitor in ROM, and the machine's 280x192
screen composed into 1bpp bands and blitted into a window.

TWO FILES AND A LICENCE, IN ONE FOLDER
--------------------------------------
APPLE2.O88  the package - and the Apple II+ ROMs are INSIDE it, 14,848 bytes
            appended past the program and claimed and read at launch before
            anything else runs: Applesoft and the Autostart Monitor, the
            character generator, and the Disk II boot ROM
APPLE2.OVL  its second segment - the menu commands, Load and Save Program,
            the disk dialog and the About panel
COPYING     the GNU General Public License, version 2, plus the MIT notices
            of the two MIT-licensed emulators this port is derived from. It
            is not read by the program; it is here because the licence has to
            travel with the binary.

The first two must be in the SAME folder: the .OVL is resolved in the
launching instance's current directory. A disk with no APPLE2.OVL is a
program whose menu commands refuse, politely - the machine itself still runs
and still draws, because the character-generator decode is in the package and
not in the overlay.

LICENCE
-------
This package's 6502 core is derived from the C64 package in the os8088 tree,
which is derived from VICE - Copyright (C) 1996-2025 the VICE team, GNU
General Public License version 2 or later. apps/apple2/ is therefore
GPL-2-or-later; the rest of os8088 is not. The full licence text is COPYING,
on this disk beside the package, and apps/apple2/COPYING in the os8088 source
tree, and it accompanies every release.

The behaviour of the machine is reimplemented from four emulators, none of
whose source is included here:

    VICE       GPL-2-or-later   the 6502 core, by way of the C64 package
    AppleWin   GPL-2-or-later   everything specific to the II Plus
    MII        MIT              the menus, the About panel, the video model
    apple2emu  MIT              the screen address maps and the Edit menu

THE ROMS ARE NOT GPL AND ARE NOT OURS
-------------------------------------
The Applesoft, Monitor, character-generator and Disk II images inside
APPLE2.O88 are Copyright (C) Apple Computer, Inc. They are not in the os8088
source tree at all: they are fetched at a pinned commit and checked by
SHA-256 when the package is built.

WHAT THIS MACHINE IS
--------------------
An Apple II PLUS, not a //e: 48K, Applesoft in ROM, no Language Card, no
80-column card, no auxiliary memory and no double hi-res. Those are not
missing features of this build - they are cards a II Plus does not have, and
a menu that greyed them would be handing you a //e's checklist.

Every menu item that cannot work in this build is present, greyed, and the
fact that greys it is written beside it in the source.

WHAT THIS BUILD DOES, AND WHAT IT DOES NOT DO YET
------------------------------------------------
The machine runs. Double-click APPLE2.O88 and the Apple II Plus banner and
the `]` prompt come up out of the machine's own ROM, with the cursor
flashing; Applesoft answers, the keyboard is the II Plus's own byte map, and
Machine > Control-Reset and Machine > Open-Apple-Control-Reset are live.

The screen is the TEXT page: the window, the character generator read out of
the ROM, the seven-pixel cell, inverse and flashing text, and a damage model
that draws only what changed.

Still to come, and docs/APPLE2-PORT-PLAN.md in the source tree says which
wave has which:

    the lo-res and hi-res composers, and the MIXED split and PAGE2 with them
    fullscreen MAGNIFICATION - Machine > Toggle Fullscreen works today, at
        1:1, and Ctrl+F and Alt+Enter are its chords
    colour, which is the foreign video mode and needs the fullscreen above
    File > Load Program... and Save Program..., and Edit > Copy and Paste
    Machine > Power On, which is greyed until it has its confirmation
    the game buttons and the paddles
    the speaker
    the Disk II

A menu item that cannot work in this build is present and GREYED, with the
fact that greys it beside it in the source. Nothing here is silently
missing, and nothing is live that can only answer "not yet".

There is no speed control: the 6502 runs the whole of every slice the OS
gives it, and the percentage at the right-hand end of the status row is what
that came to, measured against a 1.02 MHz Apple.

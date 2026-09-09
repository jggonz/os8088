APPLE2 - an Apple II Plus for os8088
===================================

APPLE2.O88 is a windowed 48K Apple II Plus: a 6502 in a 64KB heap claim,
Applesoft BASIC and the Autostart Monitor in ROM, and the machine's 280x192
screen composed into 1bpp bands and blitted into a window.

THREE FILES AND A LICENCE, IN ONE FOLDER
----------------------------------------
APPLE2.O88  the package - and the Apple II+ ROMs are INSIDE it, 14,848 bytes
            appended past the program and claimed and read at launch before
            anything else runs: Applesoft and the Autostart Monitor, the
            character generator, and the Disk II boot ROM
APPLE2.OVL  its second segment - the menu commands, Load and Save Program,
            the disk dialog and the About panel
WELCOME.BAS a short Applesoft program written for this port. Double-click it
            and the machine starts with it already loaded: type RUN, or LIST
            to read it. It shows the text page, INVERSE text, lo-res with
            MIXED's four text rows, hi-res, and the speaker - and nothing
            this build does not draw.
COPYING     the GNU General Public License, version 2, plus the MIT notices
            of the two MIT-licensed emulators this port is derived from. It
            is not read by the program; it is here because the licence has to
            travel with the binary. (Installed from The Wire, the flat
            catalog gives it and this file the names COPYING.A2 and
            APPLE2.TXT - the same two documents under names that cannot
            collide with another program's.)

APPLE2.O88 AND APPLE2.OVL must be in the SAME folder, and WELCOME.BAS must be
there too if you want to open it by double-click: the .OVL is resolved in the
launching instance's current directory, and a double-click on a document
leaves that directory on the DOCUMENT's. A disk with no APPLE2.OVL is a
program whose menu commands refuse, politely - the machine itself still runs
and still draws, because the character-generator decode is in the package and
not in the overlay. The licence is a document and is read by nobody but you,
so it may sit wherever it lands.

HOW LONG WELCOME.BAS TAKES, ON THE SLOWEST MACHINE THIS RUNS ON. On a
4.77MHz XT the text screen is up two or three seconds after RUN, and the
lo-res screen and the hi-res fan take about a minute and a quarter each.
Both draw progressively - a band or a line at a time - so you can watch them
arrive. It is the emulated Apple that is slow and not the window; the same
program is a blink on a 486. The numbers were measured on the machine, not
estimated.

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

A PROGRAM GETS IN AND OUT TWO WAYS. Edit > Paste types a listing from the
system clipboard into the keyboard, at whatever rate the machine drinks it -
lower case folds to upper, because a II Plus keyboard has none - so a BASIC
listing written in Note Pad and copied runs here. Edit > Copy puts the text
page back on the clipboard, one line a row with the trailing spaces off.
File > Save Program... writes a tokenised Applesoft program to a `.BAS` file
and File > Load Program... reads one back; a `.BAS` beside APPLE2.O88 is
double-clickable on its own, and a program saved from a machine whose program
area was somewhere else is repaired on the way in, as Applesoft's own
FIX.LINKS does. A file that is not a tokenised Applesoft program is refused by
name, and nothing is written into the machine until it has been checked.

Machine > Power On cold-starts the machine and asks first. CPU > Stop and
CPU > Continue stop and start the 6502; CPU > Warp lifts the ceiling on how
much of a slice it may run, which does nothing on a 4.77 MHz 8088 and says
so.

ALL THREE SCREENS ARE HERE: the 40x24 TEXT page with the character generator
read out of the ROM, the seven-pixel cell, inverse and flashing text; 40x48
LO-RES blocks; and 280x192 HI-RES - with MIXED (a graphics picture over four
text rows) and PAGE2 on both of them, and a damage model that draws only what
changed. GR, HGR and the soft switches all do what a II Plus does.

The window is MONOCHROME and says so: a lo-res colour is black or white by
its luminance, and hi-res drops the half-dot shift, which is what MII and
apple2emu do in mono. AppleWin instead shifts the picture half a dot, which a
280-pixel monochrome band cannot express.

Machine > Color NTSC IS THE COLOUR, and it takes the whole machine rather
than the whole window: 320x200x256 with the Apple's 280x192 centred in it, one
byte a pixel, lo-res's sixteen colours and hi-res's artifact colours - the
half-dot shift included - straight out of MII's own palette. It needs a VGA
and greys on CGA and on Hercules with the number that cut the monochrome
writers.

CTRL+F AND ALT+ENTER LEAVE IT, and they are the only way out. There is no
menu bar in there to pick anything from, and Esc is NOT a way out - Esc is a
key on an Apple II Plus keyboard and goes to the machine, as every letter
does, which is why the chords have a Ctrl and an Alt in them.

Machine > Toggle Fullscreen is the other one and is a WINDOW the size of the
screen, in monochrome, with the desktop still under it. It MAGNIFIES: 2x on
both axes on a VGA, 2x horizontally on CGA and Hercules - where 384 doubled
scan lines do not fit - and 1:1 on an 8088, where the doubling costs more than
it is worth. Ctrl+F and Alt+Enter are its chords too.

THE SPEAKER SOUNDS. $C030 is timed rather than sampled: the interval between
toggles becomes a frequency and the OS's square voice plays it while the
interval holds steady, which is what an Applesoft beep, a BEEP-style POKE
loop and most of the era's music routines are. What it cannot reproduce is
anything whose sound IS the waveform - click tracks, sampled speech, a
Mockingboard - and the status row says `Square-wave tones only.` once rather
than leaving the reader to wonder. Machine > Mute switches it off; CPU > Warp
silences it, because a note timed off an emulated clock running as fast as it
can is not the note the program asked for.

F1 and F2 are the game buttons PB0 and PB1. On a machine with NO MOUSE the
system takes the arrow keys and the space bar for its pointer, and the status
row says `ScrollLock: arrows, Space.` once - ScrollLock hands them back.

Still to come, and docs/APPLE2-PORT-PLAN.md in the source tree says which
wave has which:

    the paddles, which answer centre
    the Disk II

A menu item that cannot work in this build is present and GREYED, with the
fact that greys it beside it in the source. Nothing here is silently
missing, and nothing is live that can only answer "not yet".

There is no 3.5MHz mode and no throttle: the 6502 runs the whole of every
slice the OS gives it, and the percentage at the right-hand end of the status
row is what that came to, measured against a 1.02 MHz Apple. CPU > Warp is
this port's speed control - it lifts the ceiling the slice may grow to.

HOW FAST IT ACTUALLY IS, MEASURED AND NOT ESTIMATED. On an IBM PC/XT - an
8088 at 4.77 MHz - the machine runs the Apple at 0.54% of 1.02 MHz at the
prompt and 0.64% inside a BASIC loop, so the status row reads 0% and stays
there: the `]` prompt answers, typing arrives, and a listing that would take
a real Apple a second takes minutes. That is a machine to LOOK at rather
than to use. It is usable on a 386 and comfortable on a 486; the percentage
on the row is the honest number for whatever you are running it on, and it
is measured every second rather than assumed.

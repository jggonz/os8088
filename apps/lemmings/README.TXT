LEMMINGS - Lemmings for os8088
==============================

LEMMINGS.O88 is a native reimplementation of Lemmings (DMA Design /
Psygnosis, 1991, the DOS release) for the Intel 8086, built from the
original's own data files. Nothing of the original program is in it: what
was carried across is the level data, the graphics, the on-screen wording
and the behaviour those describe.

WHAT IS ON THIS DISK
--------------------
LEMMINGS.O88   the package. Its second segment - the About card, the preview
               and postview text, the greying facts, the level loader and
               composer, the panel's live drawing and the bracket's input -
               rides INSIDE it as a part, so there is no separate file to
               lose
README.TXT     this file
LEVELS.TXT     what THIS disk's geometry carries, level by level, which
               levels it does not and why, and what each graphic set and
               special picture costs here in bytes and clusters
LEMMAN.LEM     the converted data's manifest and its cost table
LEMSTR.LEM     every sentence the program letters
LEMR0-3.LEM    one file per rating - Fun, Tricky, Taxing, Mayhem - thirty
               level records each. This is what the CHOOSER reads: a name, a
               style, the preview numbers and whether this disk carries the
               level's pictures
LEMGRn_p.LEM   a graphic set, in numbered parts - no band file exceeds 64,512
               bytes, so a bank that would is written as LEMGR0_0.LEM,
               LEMGR0_1.LEM and so on and read back in sequence. The
               chooser and LEVELS.TXT name such a bank LEMGR0_*.LEM
LEMSPn_p.LEM   a special picture, in numbered parts, the same way
LEMLVn.LEM     the raw level records, eight to a file - what a LEVEL is built
               from once one is chosen
LEMMAIN.LEM    the shared bank out of MAIN.DAT

All of them must be in the SAME FOLDER: every band file is resolved in the
launching instance's own directory, so a copy that takes the package and
leaves a band behind is a program that comes up and says so.

A smaller disk carries less. The 720KB disk has everything but the four
special pictures; the 360KB disk has two graphic sets and only the levels
that use them. A level whose data is not on the disk is still in the list,
greyed, and clicking it says which file is missing and what it would have
cost in clusters of this disk's own total. LEVELS.TXT says the same thing
for the whole disk at once.

ORIGINAL CREDITS
----------------
As the original itself gives them (transcribed from Lemmix
src/Base.Strings.pas SCredits, the "Original credits..." block):

    Lemmings By DMA Design
    Programming By Russell Kay
    Animation By Gary Timmons
    Graphics By Scott Johnston
    Music By Brian Johnston & Tim Wright
    PC Music By Tony Williams
    Copyright 1991 Psygnosis Ltd.

The About card names the first two, the copyright and Lemmix's author, and
points here: the whole block does not fit a dialog on a 640x200 screen.

THIS PORT, AND WHOSE WORK IT CARRIES
------------------------------------
The port is os8088's own work. Where it needed to know what the original
does, it read three published reimplementations and the format documents,
and every table, string and offset taken from one of them names its source
in the header of the file that carries it:

    Lemmings (1991) and its data - DMA Design / Psygnosis Ltd.
    Lemmix - Eric Langedijk, under Lemmix's own licence (its Licence.txt:
        zlib's text with a clause of its own added, "use it for free, use
        it for fun"). The level record's bit unpacking, the screens' wording
        and layout, the rating names, the level order, the pointer's
        bitmaps, and the GAME - its ORIG mechanics, bugs included
    lemmings_3ds - Matthias (esoteric-programmer), public domain but for
        what its LICENCE.txt lists as third-party: among it the doc/
        folder, format documents it collected elsewhere and whose licence
        it calls unknown - credited below to their authors, and nothing
        of them copied. The graphic-set decoding, and a second reading of the
        mechanics: the build replays three test levels through this port
        and through a program written from lemmings_3ds, and stops if one
        lemming on one frame disagrees
    Lemmings.ts - Thomas Zeugner, MIT. The animation list, the mask rule,
        and the independent reader the build checks the converter against
    The format documents - ccexplore, Mindless, rt, Simon and Volker Oth

None of those programs is copied into this one, and nothing under a GPL or
LGPL licence is in it. The original's own on-screen strings ARE carried
verbatim - the level names, the preview wording, the result texts -
because a paraphrase of them would be a knock-off rather than a port.

PLAYING
-------
Choose a rating and a level and press Enter or click Play. The screen
becomes the original's: the level's PREVIEW - its number and name, its
lemmings, what must be saved, the release rate, the time, the rating and the
graphic set, over a picture of the level - while the level is built behind
it. The picture is the level's terrain only: the original also draws its
entrance, exit and traps there, and this one does not (SPEC.md 116.4.3.1
says what drawing them would cost). Click (or press Enter) and the level
comes up: the terrain above, the skill panel below, the status line across
the top of the panel. When it ends, the original's RESULTS screen says how
many you saved against how many you needed, in the original's words: click
the left button (or press Enter) for the next level - or to try this one
again, if you fell short - and the right button, or Esc, for the level list,
which after a success is already on the next level, as in the original
(after Mayhem 30, on Fun 1). Esc on the preview goes back to the list too.

On a CGA or an EGA those two screens are 640x200 rather than the original's
640x350: every line keeps its width and its height and the runs of blank
lines between them are shortened, and the preview's picture is 64 rows
rather than 80. The level list's greyed Mode row says so.

    the mouse      the pointer. Click a skill button, then click a lemming
                   to give it that skill. Hold the RIGHT button while you
                   click to pick a lemming that is NOT already working -
                   and, as in the 1991 game, holding it over nothing but
                   working lemmings gives the skill to the last non-working
                   lemming the game saw under the pointer at the end of a
                   frame (or while paused), wherever it is now.
                   Push the pointer against the left or right edge of the
                   level picture (not the panel) to scroll; click or drag
                   in the map at the bottom right to jump there
    F1 / F2        release rate down / up, while held
    F3 - F10       the eight skills, in the panel's order
    F11            pause (the panel's paws button on every machine)
    F12            nuke - press it twice, quickly (or double-click the
                   mushroom button). An XT keyboard has no F11 or F12; the
                   two panel buttons are the original's own way to both
    f or F         leave the full screen for the window - from the
                   level, its preview or its results; a level waits exactly
                   where it was. f, Enter or Space in the window goes back
                   in, and Esc there lets the level go. Results shown in
                   the window leave off the original's "Press right mouse
                   button for menu": a right click in an os8088 window
                   belongs to the desktop, and Esc is what leaves them
    Esc            finish the level now

Those are the 1991 game's keys, F1 to F12 and Esc, plus the full-screen key
this desktop gives every program. Three more come from Lemmix, the Windows
reimplementation this port's screens are taken from, and each displaces
nothing of the original's:

    Left / Right   scroll the view, eight pixels a step for as long as the
                   key is held
    Ctrl+Left /    scroll it one pixel a step
      Ctrl+Right
    Ctrl+F1 / F2   release rate to its minimum / maximum, while paused

The status line says what is under the pointer and how many, how many
lemmings are OUT, what share of the level's lemmings are IN, and the time
left. Every one of those is the original's own wording and position.

The sounds are short tones on the PC speaker, one for each of the original's
events and for no others - let's go, the trap door opening, a skill chosen
and given, oh no, an explosion, a splat, a lemming home, a drowning, the
fire, a tool striking steel, the builder's last three bricks and a trap.
The original is silent while a lemming digs, bashes or mines, opens its
umbrella or is nuked, and so is this. The tones are this port's own: the
original's AdLib music and effects are its own driver's code (ADLIB.DAT),
which is not in the data this port fetches.

The nuke behaves as the 1991 game's does: after it, the per cent rescued is
counted against the lemmings already released, not the level's total. So does
an early pause: pausing before frame 34 (the trap door) stops the clock, once
play resumes, for as many frames as were left until 34 - the 1991 game's own
behaviour, carried as the nuke glitch is.

THE DATA FILES ARE NOT DISTRIBUTED WITH THE SOURCE
--------------------------------------------------
The 26 DOS data files this port converts are fetched at build time by
tools/getlemmings.py, pinned by SHA-256, and are never committed to the
os8088 source tree. tools/os88lem.py turns them into the band files listed
above, so that the 8086 does no bit-stream decoding at any point. A 27th
file rides with them: the explosion's spray of eighty particles is a table
(8,160 bytes) that was extracted from the game's executable, not from its
data files, and Lemmix carries that extract as Particles.dat. It is fetched
the same way and lands in LEMMAIN.LEM, drawn for the 52 frames after a
bomber goes off (its last row twice, as Lemmix draws it); as in Lemmix, a
level does not end until the last spray has.

HOW IT LOOKS ON EACH CARD, AND HOW FAST
---------------------------------------
On a VGA the level is drawn in mode 0Dh - 320x200 in the level's own sixteen
colours - with the skill panel held still below a CRTC line compare while the
terrain scrolls past it by hardware pel panning. On a CGA and an EGA it is
mode 4, four colours; on a Hercules it is a 640x200 box in the middle of the
720x348 screen. Neither of those two cards has the level's palette, so
everything on them is drawn by COLOUR CLASS - the level's colours 1 to 7 are
the lemmings', the objects' and the panel's, 8 to 15 are the terrain's, and 0
is black - which is what keeps a lemming visible on top of terrain when there
is no colour to tell them apart. The explosion's spray is the one exception:
its particles are single pixels, so on these two cards all of them are drawn
in the solid class, where a terrain-class pixel would be lost in the
terrain's colour or dither.

What each card makes of those two classes is different, because the two cards
are different. A CGA in mode 4 has two bits a pixel and three colours, so the
terrain is drawn in one solid colour and the lemmings, objects and panel in
another: nothing is dithered there. A Hercules has one bit a pixel and no
colour at all, so the lemmings, objects and panel are solid and the terrain is
a 50% dither.

The minimap at the bottom right is the one place a colour is chosen rather
than carried. On a VGA it is the original's own build-brick colour, with the
white view rectangle over it. Those two are the same class on the other two
cards - the rectangle would disappear into the map - so there the map is drawn
in the terrain class and the rectangle in the other one, which keeps it
readable.

Neither of those two cards can scroll, either, so the picture is composed in
memory and blitted. The game scrolls 8 pixels a step - Lemmix's own step -
about 146 pixels a second here (Lemmix ~138), on every card, so the scroll
SPEED is the same on all three: 8 is two bytes on a CGA, one on a Hercules,
and a VGA pans in hardware.
What differs is the precise one-pixel scroll (Ctrl+Left / Right): a VGA
honours every pixel, a CGA
moves on every fourth and a Hercules on every eighth. A pixel-exact scroll on
those two would cost about 40% of every frame.

MEASURED on a 4.77 MHz 8088 (an IBM 5160, under a cycle-accurate emulator):
one terrain probe is 178 microseconds on either card. On the VGA a scrolled
frame is 755 microseconds, because the card does the scrolling; on the CGA it
is 178 milliseconds, because the CPU does.

Loading a level takes about 9 seconds on the VGA for Fun 1 - 191 terrain
pieces at 22 milliseconds each, a third of a second to copy the finished
terrain to the card, and about 4 seconds to sample the map at the bottom
right - and up to about 13 seconds for the largest level in the set. The CGA
and Hercules are a few seconds quicker, because they draw no colour. The
skill panel and the word LOADING go down before that starts, and on a VGA a
bar creeps across the map window for the whole wait. The terrain is built
in memory and appears all at once.

A frame is 17 a second in the original. MEASURED on the same emulated 8088
playing this disk's levels: about 4 a second on a quiet level (3.9 on a VGA,
4.6 on a CGA) and under one on a crowded one (0.6 and 0.9 with sixty out),
where drawing the lemmings is most of the frame; a spray in flight, drawn
since those figures were taken, costs more. Everything in the game -
the release rate, the time limit - slows with it, so no level becomes harder
because the machine is slow. The pointer is the original's own, in its
three colours on a VGA - the blue centre and the green and white arms - and
in one solid colour on the other two cards, which have no third. It moves
each time the game updates the screen - several times a second on a quiet
level, about once a second on a crowded one on the XT - and stays lit while
the game draws around it. A click is caught even in the middle of the
longest redraw, on every card.

This is a 1991 game on a 1981 processor and it plays at what that costs; the
design record (SPEC.md 116.7.4) has the whole table.

WHAT IS NOT HERE
----------------
Music. ADLIB.DAT is a compiled x86 sound driver, not a score this OS's FM
voices could play, and it is not in the 26-file data set at all. The Music
row on the launcher is greyed and says so.

The original's access codes. That algorithm lives in the game's executable
and in none of its data files. Progress is kept in
SYSTEM/APPDATA/LEMMINGS.SAV instead, on a disk that can be written: the
Save Progress row names the level reached ("Fun Level 2"), and the list
opens on it. Every disk keeps two clusters free for the file - a save
writes the new copy before it lets the old one go. A save the disk refuses
is said at once, and the row greys.

Oh No! More Lemmings. This port is the original 120 levels.

Anything else the build cannot do is present in the program and greyed with
the fact that greys it, never silently missing.

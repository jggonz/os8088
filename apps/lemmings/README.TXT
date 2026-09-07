LEMMINGS - Lemmings for os8088
==============================

LEMMINGS.O88 is a native reimplementation of Lemmings (DMA Design /
Psygnosis, 1991, the DOS release) for the Intel 8086, built from the
original's own data files. Nothing of the original program is in it: what
was carried across is the level data, the graphics, the on-screen wording
and the behaviour those describe.

WHAT IS ON THIS DISK
--------------------
LEMMINGS.O88   the package
LEMMINGS.OVL   its second segment - the About card, the preview text, the
               greying facts, the launcher's chrome and the progress file
README.TXT     this file
LEVELS.TXT     what THIS disk's geometry carries, level by level, with the
               cluster arithmetic that decided it
LEMMAN.LEM     the converted data's manifest and its cost table
LEMSTR.LEM     every sentence the program letters
LEMR0-3.LEM    one file per rating - Fun, Tricky, Taxing, Mayhem - thirty
               level records each. This is what the CHOOSER reads: a name, a
               style, the preview numbers and whether this disk carries the
               level's pictures
LEMGRn_p.LEM   a graphic set, in numbered parts - no band file exceeds 64,512
               bytes, so a bank that would is written as LEMGR0_0.LEM,
               LEMGR0_1.LEM and so on and read back in sequence
LEMSPn_p.LEM   a special picture, in numbered parts, the same way
LEMLVn.LEM     the raw level records, eight to a file - what a LEVEL is built
               from once one is chosen
LEMMAIN.LEM    the shared bank out of MAIN.DAT

All of them must be in the SAME FOLDER: LEMMINGS.OVL and every band file are
resolved in the launching instance's own directory, so a copy that takes the
package and leaves a band behind is a program that comes up and says so.

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

The About card names the first two of those and points at this file, because
the whole block plus a product line and a port line is past the twelve-row
ceiling a dialog has on a 640x200 screen.

THIS PORT
---------
The port is os8088's own work. Where it needed to know what the original
does, it read three published reimplementations and one format document, and
every table, string and offset taken from one of them names its source at the
line that carries it, in the source tree and in this port's design record:

    Lemmix (Eric Langedijk) - the level record's bit unpacking, the preview
        and postview screens' wording and layout, the rating names, the
        level order and its odd-table overrides
    Lemmings.ts - a second reading of the same records
    lemmings_3ds - a third, and the graphic-set decoding
    lemmings_lvl_file_format.txt - the LVL record, field by field

None of those is copied into this program and nothing under a GPL or LGPL
licence is in it. The original's own on-screen strings ARE carried verbatim -
the level names, the preview wording, the result texts - because a paraphrase
of them would be a knock-off rather than a port.

THE DATA FILES ARE NOT DISTRIBUTED WITH THE SOURCE
--------------------------------------------------
The 26 DOS data files this port converts are fetched at build time by
tools/getlemmings.py, pinned by SHA-256, and are never committed to the
os8088 source tree. tools/os88lem.py turns them into the band files listed
above, so that the 8086 does no bit-stream decoding at any point.

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
is no colour to tell them apart.

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
readable. It is the only thing telling you where you are in a level 1,584
pixels wide.

Neither of those two cards can scroll, either, so the picture is composed in
memory and blitted. That makes the scroll step 4 pixels on a CGA and 8 on a
Hercules, against 1 on a VGA. It is a real difference and it is deliberate: a
pixel-exact scroll on those cards would cost about 40% of every frame to make
a scroll that is already smooth at 17 frames a second smoother.

MEASURED on a 4.77 MHz 8088 (an IBM 5160, under a cycle-accurate emulator):
one terrain probe is 192 microseconds on either card. On the VGA a scrolled
frame is 755 microseconds, because the card does the scrolling; on the CGA it
is 178 milliseconds, because the CPU does.

Building a level's terrain out of its 400 pieces takes about 25 seconds on the
VGA and 5 on the CGA. The skill panel and the word LOADING go down before that
starts, and a bar creeps across the map window while it runs, because once the
game has the machine the system cannot put a message on the screen for it. On
a VGA the level is composed straight into the framebuffer the card is
displaying, so you watch it build; on a CGA and a Hercules it is composed in
memory and the screen stays black until it is done.

This is a 1991 game on a 1981 processor and it plays at what that costs; the
design record has the whole table.

WHAT IS NOT HERE
----------------
Music. ADLIB.DAT is a compiled x86 sound driver plus its data rather than a
score, and it is not in the 26-file data set at all; there is no FM score
path in this OS. The Music row on the launcher is greyed and says so.

The original's access codes. That algorithm lives in the game's executable
and in none of its data files. Progress is kept in
SYSTEM/APPDATA/LEMMINGS.SAV instead, on a disk that can be written.

Oh No! More Lemmings. This port is the original 120 levels.

Anything else the build cannot do is present in the program and greyed with
the fact that greys it, never silently missing.

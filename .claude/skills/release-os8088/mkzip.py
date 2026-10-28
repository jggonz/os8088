#!/usr/bin/env python3
"""Pack a release: one zip of disks, and beside it the zips that are not disks.

    os8088-<version>.zip         every floppy, in a folder per disk size
    os8088-<version>-live.zip    the live USB image and CD, and the Mac burner
    os8088-<version>-videos.zip  three 86Box machines with the video demos
    os8088-<version>-tools.zip   the firmware and the host-side utilities

THE MAIN ZIP IS LAID OUT BY DISK SIZE (approved for v1.0.20261026):

    1440K/            OS88-144 APPS-144 VMSYS144 (emu.img) ALLn-144
    1200K/            OS88-120 APPS-120 ALLn-120
    720K/             OS88-720 APPS-720 MORE-720
    720K/128K-RAM/    OS88-720 (small720.img)
    360K/             OS88-360 APPS-360 MEDIA360 OFFIC360 NETWK360 GAMES360
                      EMU360 UTIL360
    360K/128K-RAM/    OS88-360 (small360.img)
    PROGRAMS/<APP>/   one larger program per folder, the size in the name
                      (WORD-144.IMG, SCRIB120.IMG, CPM-360.IMG ...), and its
                      licence beside it when it is not the project's own;
                      PIXEL/ holds the 1.44MB disk alone, for its gallery

EVERY IMAGE AND FOLDER NAME IS 8.3, so the unpacked tree can be copied onto
an os8088 volume and every disk in it mounted there. DEST and PROGRAMS below
are where each image goes, and the script refuses a name that is not 8.3
rather than shipping one the machine cannot hold.

THE README IS GROUPED BY THE READER'S QUESTION, NOT BY THE BUILD. ITEMS are
the things a reader chooses between -- "the word processor", "the 1.44MB
pair" -- each naming the images it comes as, and GROUPS are the situations a
reader is in: booting it for the first time, a 360KB XT, an emulator, no
floppy drive at all, one more program. Each group carries the instructions for
using its own files. Only what was packed is described: a group with nothing
in the zip is left out whole, because a README that explains a file the reader
does not have reads as a packing mistake. tools/os88guide.py reads GROUPS and
ITEMS too, so the project guide lists exactly the disks a release ships; the
build names in ITEMS are what it and the website's tools match on.

TWO THINGS THIS DELIBERATELY DOES NOT DO.

It does not glob `build/*.img`. ITEMS is an ALLOWLIST, and it is one because
`build/` also holds disks that must never leave this machine -- `zork*.img`
carries Infocom story files that `tools/getstories.py` fetched and nobody here
has the right to redistribute, and every `tests/` gate writes its scratch
image into the same directory. A glob ships those the first time somebody runs
an unrelated target before cutting a release, and the mistake is invisible in
the zip listing until it is public.

It does not build anything. Run `make` and `make emu` (and any on-demand
target you want released) first; a missing REQUIRED image stops the script,
and a missing optional one is reported and skipped. The images `make`
produces are the release (four geometries of each pair since SPEC.md 19's
1.2MB disk, the 360KB-only media disk, the five 360KB-only category disks of
SPEC.md 24.6, the 720KB geometry's second apps disk, more720.img, SPEC.md
24.7, and the 128KB machine's small360.img and small720.img, SPEC.md 24.5.7),
and so is the emulator system disk (`make emu`, SPEC.md 9.11.7). The
one-program disks, the everything sets, the live media (`make live`), the
video machines (`make viddemozip`) and the tools (`make toolszip`) are
on-demand targets; their zips are written when they were built and named as
skipped when not.

A COMPANION BUILT FROM AN OLDER TREE IS REFUSED, not skipped. The live media
and the video machines carry a kernel, and the tools zip carries ROMs cut
against one kernel that refuse every other (SPEC.md 2.10), so a companion
older than build/kernel.sys describes a build this release is not. Rebuild it.
And a companion zip of THIS version that this run did not write - an earlier
run's, whose source has since gone - is deleted, and said, because the
release attaches `os8088-<version>-*.zip` by glob.

A RELEASE STAMPED WITH ANOTHER DATE IS REFUSED. Line 1 of
apps/install/OS88VER.TXT is the release INSTALL is assembled with and the
published tree announces (SPEC.md 117.10.1), and it must be the YYYYMMDD in
--version - and so must build/nettree/*/SYSTEM/OS88VER.TXT where a tree is
built, so a tree cut before a re-stamp is caught too. An installed machine
decides "newer" by comparing that line, so a stale one is an upgrade nobody
is ever offered.

The zips are byte-for-byte reproducible: entries are written in a fixed
order, every timestamp is the date in the version string, and permissions are
fixed. The images are already deterministic (tools/os88disk.py pins the volume
serial and every FAT timestamp), so the same version built from the same
commit produces the same zips, and a reader can check that.
"""
import argparse
import hashlib
import os
import re
import sys
import zipfile

# --- THE GROUPS -------------------------------------------------------------
# (key, heading, how to use the files in it). ORDER IS THE README'S ORDER, and
# it runs from what everybody needs to what somebody might want. The text is
# Markdown that also reads as plain text: a reader who opens README.md in
# Notepad gets the same document as one who opens it on GitHub.
#
# The emulator group's QEMU line HAS NO SERIAL MOUSE AND THAT IS LOAD-BEARING:
# QEMU's `pc` machine answers the VMware backdoor by default, and the msmouse
# beside it would be a second pointing device -- positions down one, buttons
# split across both, which reads as a drag that never ends (the Makefile's
# VMPORT note). tests/vmmouse.py boots the same way, with `-serial none`.
GROUPS = [
    ("start", "Start here: a system disk and a software disk", """\
os8088 boots from a system disk in drive A and runs its programs from a
software disk in drive B. You need one pair, in the disk size your machine or
emulator uses, and each size has a folder of its own. If you are not sure,
use the 1.44MB pair.

{table}

To run the 1.44MB pair in QEMU, from the folder you unpacked this into:

    qemu-system-i386 \\
      -drive file=1440K/OS88-144.IMG,format=raw,if=floppy -boot a \\
      -drive file=1440K/APPS-144.IMG,format=raw,if=floppy,index=1 \\
      -chardev msmouse,id=m0 -serial chardev:m0

The last line is the mouse. os8088 drives a serial mouse, so the emulator has
to put one on a serial port, and that is QEMU's way of doing it. For another
pair, change the two file names. Other emulators have their own way of
attaching a mouse; the project's site has the current recipe and the 86Box
machine files. In an emulator, the emulator disk below is usually the better
choice for drive A."""),

    ("xt", "More software disks for a 360KB machine", """\
A 360KB floppy holds a quarter of what a 1.44MB one does, so at that size the
programs do not all fit on the software disk. These disks, in the 360K
folder, carry the rest. Boot 360K/OS88-360.IMG in drive A as usual, then put
one of these in drive B in place of APPS-360.IMG. There are no 1.44MB, 1.2MB
or 720KB versions: at those sizes everything on them is already on the
system disk or a software disk."""),

    ("x720", "A second software disk for a 720KB machine", """\
A 720KB software disk cannot hold every program either, so a second one
carries what APPS-720.IMG leaves off. Boot 720K/OS88-720.IMG in drive A as
usual, and put either software disk in drive B. There are no versions in the
other sizes: at 1.44MB and 1.2MB everything on it is already on the software
disk, and at 360KB the disks above carry it."""),

    ("small", "A machine with as little as 128KB of memory", """\
The 128K-RAM folders inside 360K and 720K hold a system disk with a smaller
kernel and every program that runs in 128KB, all on the one disk, so a
machine with one drive needs nothing else. Boot one in drive A in place of
the system disk for your size. The 720KB disk is formatted with larger
clusters than a standard one, because the small kernel reads only a
two-sector FAT; for the same reason a machine running it reads standard 360KB
disks but no other 720KB disk."""),

    ("emu", "In an emulator or virtual machine: the pointer without a grab", """\
1440K/VMSYS144.IMG is a system disk for running os8088 in an emulator or a
virtual machine. It is OS88-144.IMG with one addition: a driver for the
absolute pointer VMware defined, which QEMU, VMware, VirtualBox and the v86
browser emulator all provide. With it the os8088 pointer sits wherever your
own mouse is. There is no clicking into the window to capture the mouse and
no key combination to let it go.

Use it in place of OS88-144.IMG, with the same APPS-144.IMG in drive B:

    qemu-system-i386 \\
      -drive file=1440K/VMSYS144.IMG,format=raw,if=floppy -boot a \\
      -drive file=1440K/APPS-144.IMG,format=raw,if=floppy,index=1

There is no mouse line this time. QEMU provides the absolute pointer by
itself, and adding the serial mouse from the command above would give the
system two mice that disagree about where the buttons are.

The driver uses instructions an 8086 does not have, so it only loads on a 386
or later. That is every emulator and virtual machine in practice. On an older
machine VMSYS144.IMG runs like OS88-144.IMG and uses the serial mouse, and a
real PC/XT cannot read a 1.44MB disk anyway, so for real hardware use the
pairs above."""),

    # THE LIVE GROUP IS THE LIVE ZIP'S, not this one's: its files go to
    # os8088-<version>-live.zip and its text heads that zip's README. It stays
    # in GROUPS because tools/os88guide.py lists the live media from here.
    ("live", "No floppy drive: the live USB image and the live CD", """\
These carry the whole system and every program, with no floppies at all.
Booted this way the system runs from a hard-disk partition that appears as
drive C, with every program on it.

To write the USB image to a stick -- everything already on the stick is
erased -- and boot a PC from it in legacy BIOS mode, on macOS or Linux, with
the stick at /dev/sdX and unmounted:

    dd if=LIVEUSB.IMG of=/dev/sdX bs=1M

On a Mac, burn-usb-macos.py does this with an interactive guide instead: it
lists the attached USB flash drives, has you confirm the one to erase by
typing its name, and verifies the write. It burns the CD too, when a burner
is attached. It needs Python 3 and nothing else; from the folder you
unpacked this into:

    python3 burn-usb-macos.py --image LIVEUSB.IMG --iso OS8088.ISO

In QEMU, one or the other:

    qemu-system-i386 -drive file=LIVEUSB.IMG,format=raw -boot c \\
      -chardev msmouse,id=m0 -serial chardev:m0

    qemu-system-i386 -cdrom OS8088.ISO -boot d \\
      -chardev msmouse,id=m0 -serial chardev:m0"""),

    ("all", "Every program, on a set of disks", """\
The software disk carries a selection. These sets carry every program,
including the ones that otherwise have a disk of their own below, spread over
as many disks as they take: ALLn-144.IMG in the 1440K folder and ALLn-120.IMG
in the 1200K folder. Boot a system disk in drive A and put any disk of the
set in drive B; CONTENTS.TXT on each disk says which programs are on it.
There is no 720KB or 360KB set."""),

    ("progs", "One program per disk", """\
The PROGRAMS folder has a folder for each of these larger programs, holding
one disk per size it comes in, with the size in the file name: 144 for
1.44MB, 120 for 1.2MB, 720 and 360. Boot the system disk for your size in
drive A, and put one of these in drive B in place of the software disk. None
of them is bootable on its own."""),

    ("about", "About this zip", """\
Every disk image and folder here has a name of eight characters or fewer, so
the whole tree can be copied onto an os8088 disk and its images used there.

The build is deterministic, so anyone who builds this version from source gets
these same bytes. To check every file against SHA256SUMS on macOS or Linux,
from the folder you unpacked this into:

    shasum -a 256 -c SHA256SUMS

{licence}"""),
]

# --- THE ITEMS --------------------------------------------------------------
# (group, title, required, description, files). A file is (name, size, note).
# ORDER IS THE ZIP'S ORDER and the README's. Required = built by `make` or
# `make emu`; optional = its own on-demand target.
#
# THE EVERYTHING SETS (SPEC.md 19.10) are as many disks as the payload needs,
# so they are named by the LIST FILE the build writes, not here. An "@" file
# expands to the images that list names; each must match the set's own
# pattern or the zip refuses, so the allowlist stays an allowlist.
#
# In the "start" group the two files of an item are the system disk and the
# software disk of one size, and the description is what that size is for --
# that group is a table rather than a list.
ITEMS = [
    ("start", "1.44MB", True,
     "Most emulators, and a PC with a 3.5-inch high-density drive.",
     [("os8088.img", "1.44MB", ""), ("apps.img", "1.44MB", "")]),
    ("start", "1.2MB", True,
     "An AT-class PC whose only drive is a 5.25-inch high-density one, which "
     "reads neither 3.5-inch disk. Carries everything the 1.44MB pair does.",
     [("os8088-120.img", "1.2MB", ""), ("apps120.img", "1.2MB", "")]),
    ("start", "720KB", True,
     "A 3.5-inch double-density drive.",
     [("os8088-720.img", "720KB", ""), ("apps720.img", "720KB", "")]),
    ("start", "360KB", True,
     "A real IBM PC/XT of the period, which reads 360KB disks and nothing "
     "larger. Some programs do not fit at this size; the next section has "
     "the disks that carry them.",
     [("os8088-360.img", "360KB", ""), ("apps360.img", "360KB", "")]),

    ("xt", "Music disk", True,
     "The music programs and what they play: MIDIRack with its ten songs, "
     "XTMP3 with the MP3 an XT is meant for, and the music module the "
     "tracker opens. They do not fit beside the other programs at this size, "
     "so they ride a disk of their own.",
     [("media360.img", "360KB", "")]),
    ("xt", "Office disk", True,
     "The spreadsheet and the chart viewer -- which at this size are on this "
     "disk and on no other -- beside Word, Paint, ArtfulType, the calculator "
     "and the font viewer.",
     [("office360.img", "360KB", "")]),
    ("xt", "Network disk", True,
     "The web browser, the FTP server, Telnet and The Wire, for an XT with an "
     "Ethernet card.",
     [("network360.img", "360KB", "")]),
    ("xt", "Games disk", True,
     "Every game on one floppy.",
     [("games360.img", "360KB", "")]),
    ("xt", "Emulators disk", True,
     "RealDMG, the Game Boy emulator, with the project's own demo cartridge "
     "in MEDIA. The other emulators -- the Commodore 64, the Apple II Plus "
     "and CP/M -- each have a disk of their own below.",
     [("emu360.img", "360KB", "")]),
    ("xt", "Utilities disk", True,
     "System Check, which checks a system disk against this release, the "
     "REDLINE benchmark and the Task Manager, with the DOS ends of the "
     "cable link and the split-file tool, OS88NET.COM and OS88CZ.COM, in "
     "SYSTEM\\DOS.",
     [("util360.img", "360KB", "")]),

    ("small", "128KB system disk", True,
     "The small kernel with every program that fits a 128KB machine.",
     [("small360.img", "360KB", ""), ("small720.img", "720KB", "")]),

    ("x720", "Second software disk, 720KB", True,
     "PiXEL with five of its sample pictures in PICTURES, MIDIRack with "
     "the eight songs the first disk has no room for, RealDMG with its demo "
     "cartridge, DrMarco and the demo video. MIDIRack is on both software "
     "disks, so its songs open from either.",
     [("more720.img", "720KB", "")]),

    ("emu", "Emulator system disk", True,
     "The system disk with the absolute-pointer driver, switched on. Goes in "
     "drive A with APPS-144.IMG in drive B; there is no separate software disk "
     "for it.",
     [("emu.img", "1.44MB", "")]),

    ("live", "Live USB image", False,
     "The whole system and every program on one bootable hard-disk image. "
     "Settings and saved files are kept on the stick.",
     [("os8088-usb.img", "", "")]),
    ("live", "Live CD", False,
     "The same system as a bootable CD image, for burning to a disc or "
     "booting in an emulator. A CD is read-only, so settings and saved files "
     "last until power-off; the USB image keeps them.",
     [("os8088.iso", "", "")]),

    ("all", "Everything set, 1.44MB", False,
     "Both word processors, the story reader, the CP/M emulator, the "
     "Commodore 64, the Apple II Plus, the RealDMG Game Boy, the Weave "
     "programs and their editor, "
     "the 1942 air-combat game, the REDLINE benchmark, five of PiXEL's "
     "sample pictures, and everything on the software disk.",
     [("@apps-all.list", "1.44MB", "")]),
    ("all", "Everything set, 1.2MB", False,
     "The same set on 1.2MB disks.",
     [("@apps-all-120.list", "1.2MB", "")]),

    ("progs", "Word processor", False,
     "A word processor modelled on Microsoft Word for Windows 1.1a -- its "
     "menus, ribbon, ruler and keys -- written in assembly.",
     [("word.img", "1.44MB", ""), ("word120.img", "1.2MB", ""),
      ("word720.img", "720KB", "")]),
    ("progs", "Scribe", False,
     "The word processor with a second set of choices -- the same document "
     "format, its own program -- and a welcome document to open.",
     [("scribe.img", "1.44MB", ""), ("scribe120.img", "1.2MB", ""),
      ("scribe720.img", "720KB", ""), ("scribe360.img", "360KB", "")]),
    ("progs", "Word processor, C edition", False,
     "The same word processor written again in C and built by the C "
     "compiler, with a welcome document to open.",
     [("cword.img", "1.44MB", ""), ("cword120.img", "1.2MB", ""),
      ("cword720.img", "720KB", ""), ("cword360.img", "360KB", "")]),
    ("progs", "CP/M emulator", False,
     "Runs CP/M 2.2 programs in a window. Each size carries the emulator and "
     "as many CP/M programs as fit.",
     [("runcpm.img", "1.44MB", "the CP/M programs that run under it"),
      ("runcpm120.img", "1.2MB", "four of the five program areas the 1.44MB "
       "disk has -- the word processor comes off, being the largest -- and "
       "more of CP/M's own disk than the 1.44MB one holds"),
      ("runcpm720.img", "720KB", "the arcade games only"),
      ("runcpm360.img", "360KB", "no programs -- there is no room for them "
       "at this size")]),
    ("progs", "Commodore 64", False,
     "A Commodore 64 emulator, ported from VICE, with the three Commodore ROM "
     "images inside it and its licence, COPYING, on the disk.",
     [("c64.img", "1.44MB", ""), ("c64120.img", "1.2MB", ""),
      ("c64720.img", "720KB", ""), ("c64360.img", "360KB", "")]),
    ("progs", "Apple II Plus", False,
     "An Apple II Plus emulator, with Applesoft BASIC and the character "
     "generator inside it, a BASIC program to load, a README and its "
     "licence, COPYING.",
     [("apple2.img", "1.44MB", ""), ("apple2120.img", "1.2MB", ""),
      ("apple2720.img", "720KB", ""), ("apple2360.img", "360KB", "")]),
    ("progs", "PaccMan", False,
     "The arcade-accurate Pac-Man, built by the C compiler. It wants a 386 "
     "to play at full speed.",
     [("paccman.img", "1.44MB", ""), ("paccman120.img", "1.2MB", ""),
      ("paccman720.img", "720KB", ""), ("paccman360.img", "360KB", "")]),
    ("progs", "1942", False,
     "An air-combat game after the 1942 arcade game, written in assembly "
     "with original artwork: 32 stages, one or two players, and music on an "
     "AdLib or Sound Blaster, with sampled gunfire and explosions on a "
     "Sound Blaster. It needs a VGA or CGA card. Both disks carry the same "
     "game.",
     [("1942.img", "1.44MB", ""), ("1942-360.img", "360KB", "")]),
    ("progs", "MIDIRack", False,
     "A MIDI music player, with ten songs to play: four written for it and "
     "six public-domain classics. It sounds "
     "through an AdLib or Sound Blaster FM chip, a synthesizer or a "
     "wavetable on a Sound Blaster, an MPU-401 MIDI port, or the PC "
     "speaker. The wavetable's instruments are made from S. Christian "
     "Collins's GeneralUser GS sound bank, and only this disk, the "
     "everything set and the live media carry them.",
     [("midirack.img", "1.44MB", ""), ("midirack120.img", "1.2MB", ""),
      ("midirack720.img", "720KB", ""), ("midirack360.img", "360KB", "")]),
    ("progs", "PiXEL", False,
     "An image viewer and editor. It opens JPEG, PNG, GIF, BMP, PCX, TIFF "
     "and six more formats, shows them dithered to the desktop's colours "
     "or full screen in the best mode the card has, and can crop, rotate, "
     "adjust and save them. PiXEL itself is on the 1.44MB and 1.2MB "
     "software disks, the 360KB office disk and the second 720KB disk; "
     "this disk is it with the whole gallery of ten sample pictures in "
     "PICTURES, which the software disks have no room for. The second "
     "720KB disk and the everything sets carry five of them.",
     [("pixel.img", "1.44MB", "")]),
    ("progs", "XTMP3", False,
     "An MP3 player that plays every song at the best quality the computer "
     "and its sound card can keep up with, and says which quality that is. "
     "A slow PC decodes the song to a WAV file on its hard disk first, then "
     "plays that. With three demo songs in MEDIA. It is on the 1.44MB "
     "software disk and the 360KB music disk; at 1.2MB and 720KB it is "
     "here.",
     [("xtmp3120.img", "1.2MB", ""), ("xtmp3720.img", "720KB", "")]),
    ("progs", "Weave", False,
     "Web-style programs -- markup, script and formulas -- compiled into one "
     "bundle file and run natively. Carries the runtime, three demo "
     "programs, the editor that builds them and the sources they were built "
     "from.",
     [("weave.img", "1.44MB", ""), ("weave120.img", "1.2MB", ""),
      ("weave720.img", "720KB", ""), ("weave360.img", "360KB", "")]),
]

# --- WHERE EACH IMAGE GOES ---------------------------------------------------
# A build name -> its path in the zip. The main disks are named here one by
# one, because each name is a choice somebody approved; the one-program disks
# and the everything sets follow rules, below. An image ITEMS packs and none of
# these place stops the script.
DEST = {
    "os8088.img": "1440K/OS88-144.IMG",
    "apps.img": "1440K/APPS-144.IMG",
    "emu.img": "1440K/VMSYS144.IMG",
    "os8088-120.img": "1200K/OS88-120.IMG",
    "apps120.img": "1200K/APPS-120.IMG",
    "os8088-720.img": "720K/OS88-720.IMG",
    "apps720.img": "720K/APPS-720.IMG",
    "more720.img": "720K/MORE-720.IMG",
    "small720.img": "720K/128K-RAM/OS88-720.IMG",
    "os8088-360.img": "360K/OS88-360.IMG",
    "apps360.img": "360K/APPS-360.IMG",
    "media360.img": "360K/MEDIA360.IMG",
    "office360.img": "360K/OFFIC360.IMG",
    "network360.img": "360K/NETWK360.IMG",
    "games360.img": "360K/GAMES360.IMG",
    "emu360.img": "360K/EMU360.IMG",
    "util360.img": "360K/UTIL360.IMG",
    "small360.img": "360K/128K-RAM/OS88-360.IMG",
    # the live zip's, which has no folders
    "os8088-usb.img": "LIVEUSB.IMG",
    "os8088.iso": "OS8088.ISO",
}

# The everything sets: disk N of a list goes in the folder of its size.
SETS = {"apps-all.list": "1440K/ALL%d-144.IMG",
        "apps-all-120.list": "1200K/ALL%d-120.IMG"}

# An ITEMS title in the "progs" group -> (folder under PROGRAMS, short name).
# A disk is <short>-<size>.IMG when the short name has room for the dash and
# <short><size>.IMG when it does not: WORD-144.IMG, SCRIB144.IMG.
PROGRAMS = {
    "Word processor": ("WORD", "WORD"),
    "Scribe": ("SCRIBE", "SCRIB"),
    "Word processor, C edition": ("CWORD", "CWORD"),
    "CP/M emulator": ("RUNCPM", "CPM"),
    "Commodore 64": ("C64", "C64"),
    "Apple II Plus": ("APPLE2", "APPL2"),
    "PaccMan": ("PACCMAN", "PACCM"),
    "1942": ("1942", "1942"),
    "MIDIRack": ("MIDIRACK", "MIDI"),
    "PiXEL": ("PIXEL", "PIXEL"),
    "XTMP3": ("XTMP3", "XTMP3"),
    "Weave": ("WEAVE", "WEAVE"),
}
SIZE_TAG = {"1.44MB": "144", "1.2MB": "120", "720KB": "720", "360KB": "360"}

ZIPGROUP = {"live": "live"}     # every other group goes in the main zip

# Files packed beside the images, when the image they belong to is here, in
# the folder of the program they cover.
# THE C64 IS GPL-2-OR-LATER because VICE is, and C64-SPEC 1.2 says in
# as many words that "the release zip carries COPYING". Its own floppies carry
# it too -- this is the second copy, for a reader who unpacks the zip and never
# mounts a disk. (trigger image, source path, path in the zip, description)
EXTRAS = [
    ("c64.img", "apps/c64/COPYING", "PROGRAMS/C64/COPYING",
     "The GNU General Public License version 2, which is the licence the "
     "Commodore 64 emulator is under, because it is a port of VICE, which "
     "is. It applies to that one program and to nothing else here."),
    ("apple2.img", "apps/apple2/COPYING", "PROGRAMS/APPLE2/COPYING",
     "The GNU General Public License version 2, which is the licence the "
     "Apple II Plus emulator is under -- its display code came from the "
     "same project the Commodore 64 emulator's did. It applies to that one "
     "program and to nothing else here."),
    ("paccman.img", "apps/paccman/LICENSE", "PROGRAMS/PACCMAN/LICENSE",
     "The MIT licence on Andre Weissflog's pacman.c, which PaccMan was "
     "ported from. It names him as its copyright holder; the rest of this "
     "release is under the project's own MIT licence."),
]

# The Mac burner rides in the live zip under a name that says what it is for.
LIVE_TOOL = ("tools/os88burn.py", "burn-usb-macos.py")

# The companions `make` builds as zips of their own: (key, file in build/, its
# top folder, the target that makes it). Each is repacked under
# os8088-<version>-<key>/ with this release's timestamps.
COMPANIONS = [
    ("videos", "os8088-videos.zip", "os8088-videos", "make viddemo viddemozip"),
    ("tools", "os8088-tools.zip", "os8088-tools", "make toolszip"),
]

# The freshness reference: every companion that carries a kernel, or ROMs cut
# against one, must be at least this new.
KERNFILE = "kernel.sys"

# The release stamp's source, and the published trees `make nettree` cuts
# (SPEC.md 117.10.1, 117.10.2) - each must say this version's date.
VERFILE = "apps/install/OS88VER.TXT"
NETTREES = ("big", "mega", "emu")

README = """\
# os8088 {version}

os8088 is a graphical operating system for the Intel 8086, written in assembly
and booted from a floppy disk. It has overlapping windows, a menu bar, a mouse
and programs you can run several copies of at once.

This zip holds {count} files. The disks are in a folder for each disk size --
1440K, 1200K, 720K and 360K -- and PROGRAMS holds the larger programs that
come on disks of their own. They are described below by what you are trying
to do, and each section says how to use its files. Most people need only the
first section.

{toc}

{body}
{beside}
Built from commit {commit}.
"""

LIVE_README = """\
# os8088 {version}: live media

{body}

## About this zip

- `README.md` -- this file.
- `SHA256SUMS` -- a SHA-256 checksum for every other file here. To check
  them on macOS or Linux, from the folder you unpacked this into:
  `shasum -a 256 -c SHA256SUMS`
- `burn-usb-macos.py` -- the Mac guide described above.

The floppy disks are in os8088-{version}.zip.

Built from commit {commit}.
"""

BESIDE = {
    "live": "the live USB image and live CD, for a PC with no floppy drive",
    "videos": "three ready-to-run 86Box machines with the video demos on "
              "their hard disks",
    "tools": "the firmware and the utilities: the PicoMEM firmware, the "
             "kernel ROMs for an IBM PC, the video encoder, the Game Boy "
             "precompiler, the file splitter, the Browser's proxy and the DOS "
             "tools",
}

DOS83 = re.compile(r"[A-Z0-9_\-]{1,8}(\.[A-Z0-9_\-]{1,3})?")


def version_date(version):
    """A fixed timestamp, so the same version packs to the same bytes.

    The zip format cannot store a date before 1980, so a version string that
    carries no date falls back to the epoch the format itself starts at.
    """
    m = re.search(r"(\d{4})(\d{2})(\d{2})", version)
    if m:
        y, mo, d = (int(x) for x in m.groups())
        if 1980 <= y <= 2107 and 1 <= mo <= 12 and 1 <= d <= 31:
            return (y, mo, d, 12, 0, 0)
    return (1980, 1, 1, 0, 0, 0)


def release_of(path):
    """Line 1 of an OS88VER.TXT, read as the Makefile's INREL reads it."""
    with open(path, "rb") as f:
        first = re.split(rb"[\r\n]", f.read(), maxsplit=1)[0]
    return first.replace(b" ", b"").decode("ascii", "replace")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def info(name, when):
    """One entry, with everything the OS would otherwise vary pinned flat."""
    zi = zipfile.ZipInfo(name, date_time=when)
    zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = 0o644 << 16
    zi.create_system = 3            # unix, whatever host packed it
    return zi


def add(zf, name, data, when):
    zf.writestr(info(name, when), data)


def add_file(zf, name, path, when):
    """A file streamed in: a video machine's disk is half a gigabyte."""
    with open(path, "rb") as f, zf.open(info(name, when), "w", force_zip64=True) as w:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            w.write(chunk)


def para(s, w=78, lead="", cont=""):
    """Wrap `s` at `w` columns, the first line after `lead` and the rest after
    `cont` -- each line measured with its OWN prefix, so a long lead does not
    narrow every line after it."""
    out, line, pre = [], "", lead
    for word in s.split():
        if line and len(pre) + len(line) + 1 + len(word) > w:
            out.append(pre + line)
            line, pre = word, cont
        else:
            line = (line + " " + word).strip()
    if line:
        out.append(pre + line)
    return "\n".join(out)


def anchor(heading):
    """GitHub's heading anchor: lower case, punctuation gone, spaces to -."""
    return re.sub(r"[^a-z0-9 -]", "", heading.lower()).replace(" ", "-")


def dest_of(item, name, size):
    """Where a packed image goes, or None when nothing places it."""
    if name in DEST:
        return DEST[name]
    if item[0] == "progs" and item[1] in PROGRAMS and size in SIZE_TAG:
        folder, short = PROGRAMS[item[1]]
        sep = "-" if len(short) <= 4 else ""
        return "PROGRAMS/%s/%s%s%s.IMG" % (folder, short, sep, SIZE_TAG[size])
    return None


def is83(path):
    return all(DOS83.fullmatch(p) for p in path.split("/"))


def render(packed, extras, keys):
    """The README body: one section per group in `keys` with a file packed.

    `packed` is [(item, [(path, size, note), ...])] with only the files that
    were found, @-lists already expanded; `extras` is [(path, desc)].
    """
    sections, toc = [], []
    for key, heading, intro in GROUPS:
        if key not in keys:
            continue
        items = [(it, fs) for it, fs in packed if it[0] == key]
        if key == "about":
            items = [None]           # always present: SHA256SUMS is
        if not items:
            continue
        toc.append("- [%s](#%s)" % (heading, anchor(heading)))
        out = ["## " + heading, ""]
        if key == "start":
            rows = ["| Disk size | System disk (drive A) | Software disk "
                    "(drive B) | Use it on |", "|---|---|---|---|"]
            for (_, title, _, desc, _), fs in items:
                rows.append("| %s | `%s` | `%s` | %s |"
                            % (title, fs[0][0], fs[1][0], desc))
            out.append(intro.format(table="\n".join(rows)))
        elif key == "about":
            lic = ("os8088 is under the MIT licence; the LICENSE file in "
                   "the source repository has the terms.")
            if extras:
                lic += (" A few programs here carry a licence of their own, "
                        "in the program's folder beside its disks. Each says "
                        "which program it covers, and nothing else in os8088 "
                        "is affected by it.")
            out.append(intro.format(licence=para(lic)))
            out.append("")
            out.append(para("This file. It describes every other file in "
                            "the zip.", lead="- `README.md` -- ",
                            cont="  "))
            out.append(para("A SHA-256 checksum for every disk image and "
                            "licence file here.",
                            lead="- `SHA256SUMS` -- ", cont="  "))
            for name, desc in extras:
                out.append(para(desc, lead="- `%s` -- " % name, cont="  "))
        else:
            out.append(intro)
            for (_, title, _, desc, _), fs in items:
                out += ["", "### " + title, "", para(desc), ""]
                for name, size, note in fs:
                    tail = ", ".join(x for x in (size, note) if x)
                    out.append(para(tail, lead="- `%s`%s" % (name,
                                    " -- " if tail else ""), cont="  ")
                               if tail else "- `%s`" % name)
        sections.append("\n".join(out))
    return "\n".join(toc), "\n\n".join(sections)


def write_zip(out, root, entries, when):
    """entries: [(path under root, bytes or a file path)], already in order.
    Read back rather than trusting the write: a truncated member is a
    perfectly valid-looking zip until somebody unpacks it."""
    tmp = out + ".part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9,
                         allowZip64=True) as zf:
        for name, data in entries:
            if isinstance(data, bytes):
                add(zf, "%s/%s" % (root, name), data, when)
            else:
                add_file(zf, "%s/%s" % (root, name), data, when)
    with zipfile.ZipFile(tmp) as zf:
        bad = zf.testzip()
        if bad:
            raise SystemExit("mkzip: %s is corrupt in %s" % (bad, tmp))
        n = len(zf.namelist())
    os.replace(tmp, out)
    print("mkzip: %s" % out)
    print("  %d entries, %.1fMB, sha256 %s"
          % (n, os.path.getsize(out) / 1e6, sha256(out)))


def repack(src, old_top, out, root, when):
    """A companion zip `make` built, under this release's folder and dates."""
    with zipfile.ZipFile(src) as zin:
        members = [zi for zi in zin.infolist() if not zi.is_dir()]
        for zi in members:
            head, _, rest = zi.filename.partition("/")
            if head != old_top or not rest:
                raise SystemExit("mkzip: %s holds %s, outside its %s/ folder"
                                 % (src, zi.filename, old_top))
        tmp = out + ".part"
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9,
                             allowZip64=True) as zout:
            for zi in sorted(members, key=lambda z: z.filename):
                rest = zi.filename.partition("/")[2]
                ni = info("%s/%s" % (root, rest), when)
                ni.external_attr = zi.external_attr   # the .py stay executable
                with zin.open(zi) as r, zout.open(ni, "w", force_zip64=True) as w:
                    for chunk in iter(lambda: r.read(1 << 20), b""):
                        w.write(chunk)
    with zipfile.ZipFile(tmp) as zf:
        bad = zf.testzip()
        if bad:
            raise SystemExit("mkzip: %s is corrupt in %s" % (bad, tmp))
        n = len(zf.namelist())
    os.replace(tmp, out)
    print("mkzip: %s" % out)
    print("  %d entries, %.1fMB, sha256 %s"
          % (n, os.path.getsize(out) / 1e6, sha256(out)))


def main():
    ap = argparse.ArgumentParser(description="Pack the release zips.")
    ap.add_argument("--version", required=True, help="e.g. v1.0.20260819")
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--out-dir", help="default: the build directory")
    ap.add_argument("--commit", default="", help="the OS commit it was built from")
    args = ap.parse_args()

    bd = args.build_dir
    od = args.out_dir or bd
    v = args.version
    when = version_date(v)

    # The release stamp (SPEC.md 117.10.1) is this version's date, in the
    # source INSTALL is assembled from and in every published tree built.
    m = re.search(r"\d{8}", v)
    if not m:
        print("mkzip: %s carries no YYYYMMDD, so there is no release to check "
              "OS88VER.TXT against -- refusing." % v, file=sys.stderr)
        return 1
    stamps = [VERFILE] + [os.path.join(bd, "nettree", k, "SYSTEM", "OS88VER.TXT")
                          for k in NETTREES]
    for i, p in enumerate(stamps):
        if not os.path.isfile(p):
            if i == 0:
                print("mkzip: %s is missing -- refusing." % p, file=sys.stderr)
                return 1
            continue
        rel = release_of(p)
        if rel != m.group(0):
            print("mkzip: %s says release %r and this is %s -- stamp it (step "
                  "1a), rebuild, and try again." % (p, rel, v), file=sys.stderr)
            return 1

    commit = args.commit
    if not commit:
        import subprocess
        try:
            commit = subprocess.check_output(
                ["git", "rev-parse", "HEAD"], text=True).strip()
        except Exception:
            commit = "unknown"

    kern = os.path.join(bd, KERNFILE)
    if not os.path.isfile(kern):
        print("mkzip: %s is not built -- run `make` first." % kern, file=sys.stderr)
        return 1
    kern_t = os.path.getmtime(kern)

    def stale(path, target):
        if os.path.getmtime(path) < kern_t:
            print("mkzip: %s is older than %s, so it was built from another "
                  "tree -- run `%s` again." % (path, kern, target), file=sys.stderr)
            return True
        return False

    packed, missing, skipped = [], [], []
    for item in ITEMS:
        group, title, required, desc, files = item
        found = []
        for name, size, note in files:
            if name.startswith("@"):
                lst = os.path.join(bd, name[1:])
                if not os.path.isfile(lst):
                    skipped.append(name[1:])
                    continue
                stem = re.escape(name[1:-len(".list")])
                with open(lst) as f:
                    imgs = [os.path.basename(l.strip()) for l in f if l.strip()]
                for i, img in enumerate(imgs):
                    if not re.fullmatch(stem + r"-\d+\.img", img) or \
                            not os.path.isfile(os.path.join(bd, img)):
                        print("mkzip: %s names %r, which is not a disk of that "
                              "set in %s -- refusing." % (lst, img, bd),
                              file=sys.stderr)
                        return 1
                    found.append((SETS[name[1:]] % (i + 1), img, size,
                                  "disk %d of %d" % (i + 1, len(imgs))))
                continue
            if os.path.isfile(os.path.join(bd, name)):
                d = dest_of(item, name, size)
                if d is None:
                    print("mkzip: nothing says where %s goes in the zip -- add "
                          "it to DEST or PROGRAMS." % name, file=sys.stderr)
                    return 1
                found.append((d, name, size, note))
            elif required:
                missing.append(name)
            else:
                skipped.append(name)
        if found:
            packed.append((item, found))

    if missing:
        print("mkzip: these are built by `make` and `make emu` and are not in "
              "%s:" % bd, file=sys.stderr)
        for m in missing:
            print("  %s" % m, file=sys.stderr)
        print("mkzip: run `make && make emu` and try again -- refusing to pack "
              "a partial release.", file=sys.stderr)
        return 1

    # An extra rides only when the image it belongs to did. A licence for a
    # program that is not in the zip is noise; a program in the zip without
    # its licence is the thing this list exists to stop.
    have = {name for _, fs in packed for _, name, _, _ in fs}
    extras = []
    for trigger, src, as_name, desc in EXTRAS:
        if trigger in have:
            if not os.path.isfile(src):
                print("mkzip: %s is in the zip and %s is missing -- refusing to "
                      "ship it without its licence." % (trigger, src), file=sys.stderr)
                return 1
            extras.append((as_name, src, desc))

    # Every path in the disk zips is 8.3 -- the point of the layout -- and no
    # two land on one name.
    paths = [d for _, fs in packed for d, _, _, _ in fs] + [n for n, _, _ in extras]
    bad = [p for p in paths if not is83(p)]
    dup = sorted({p for p in paths if paths.count(p) > 1})
    if bad or dup:
        print("mkzip: not an 8.3 name: %s; on two files: %s -- refusing."
              % (", ".join(bad) or "none", ", ".join(dup) or "none"), file=sys.stderr)
        return 1

    def split(zipkey):
        return [(it, [(d, sz, note) for d, _, sz, note in fs])
                for it, fs in packed if ZIPGROUP.get(it[0], "main") == zipkey]

    def files_of(zipkey):
        return [(d, os.path.join(bd, n)) for it, fs in packed
                if ZIPGROUP.get(it[0], "main") == zipkey for d, n, _, _ in fs]

    # --- the companions: which exist, and whether they are this tree's ------
    live = files_of("live")
    for _, p in live:
        if stale(p, "make live"):
            return 1
    comps = []
    for key, fname, top, target in COMPANIONS:
        src = os.path.join(bd, fname)
        if not os.path.isfile(src):
            skipped.append(fname)
            continue
        if stale(src, target):
            return 1
        comps.append((key, src, top))
    written = (["live"] if live else []) + [k for k, _, _ in comps]

    # A companion zip of this version that this run will not write is an
    # earlier run's, and the release attaches os8088-<version>-*.zip by glob.
    for k in ["live"] + [c[0] for c in COMPANIONS]:
        old = os.path.join(od, "os8088-%s-%s.zip" % (v, k))
        if k not in written and os.path.isfile(old):
            os.remove(old)
            print("mkzip: removed %s -- an earlier run wrote it and this one "
                  "did not." % old)

    # --- the main zip ----------------------------------------------------------
    os.makedirs(od, exist_ok=True)
    main_files = sorted(files_of("main") + [(n, s) for n, s, _ in extras])
    sums = ["%s  %s" % (sha256(p), name) for name, p in main_files]
    toc, body = render(split("main"), [(n, d) for n, _, d in extras],
                       [g for g, _, _ in GROUPS if ZIPGROUP.get(g, "main") == "main"])
    beside = ""
    if written:
        beside = "\nBeside this zip, from the same build:\n\n" + "\n".join(
            para(BESIDE[k], lead="- `os8088-%s-%s.zip` -- " % (v, k), cont="  ")
            for k in written) + "\n"
    readme = README.format(version=v, commit=commit, toc=toc, body=body,
                           beside=beside, count=len(main_files) + 2)
    root = "os8088-%s" % v
    write_zip(os.path.join(od, root + ".zip"), root,
              [("README.md", readme.encode()),
               ("SHA256SUMS", ("\n".join(sums) + "\n").encode())] + main_files,
              when)
    raw = sum(os.path.getsize(p) for _, p in main_files)
    print("  %d images and licences, %.1fMB unpacked" % (len(main_files), raw / 1e6))

    # --- the live zip -----------------------------------------------------------
    if live:
        lfiles = sorted(live) + [(LIVE_TOOL[1], LIVE_TOOL[0])]
        sums = ["%s  %s" % (sha256(p), name) for name, p in lfiles]
        _, lbody = render(split("live"), [], ["live"])
        lreadme = LIVE_README.format(version=v, commit=commit,
                                     body=lbody.split("\n", 2)[2])
        lroot = "os8088-%s-live" % v
        write_zip(os.path.join(od, lroot + ".zip"), lroot,
                  [("README.md", lreadme.encode()),
                   ("SHA256SUMS", ("\n".join(sums) + "\n").encode())] + lfiles,
                  when)

    # --- the videos and the tools ----------------------------------------------
    for key, src, top in comps:
        r = "os8088-%s-%s" % (v, key)
        repack(src, top, os.path.join(od, r + ".zip"), r, when)

    if skipped:
        print("mkzip: on-demand files NOT built, so not released: %s"
              % ", ".join(skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())

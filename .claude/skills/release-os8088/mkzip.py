#!/usr/bin/env python3
"""Pack a release into ONE zip.

A release used to attach eleven loose `.img` files to the GitHub release, and
a reader had to know which two of them they wanted before they could download
anything. This builds a single `os8088-<version>.zip` instead: every image the
build produced, a README.md that explains every file in it, grouped by what
the reader is trying to do, and a SHA256SUMS file covering all of them.

THE README IS GROUPED BY THE READER'S QUESTION, NOT BY THE BUILD. Sixty-odd
images in manifest order is a list nobody can choose from, so ITEMS below are
the things a reader chooses between -- "the word processor", "the 1.44MB
pair" -- each naming the images it comes as, and GROUPS are the situations a
reader is in: booting it for the first time, a 360KB XT, an emulator, no
floppy drive at all, one more program. Each group carries the instructions for
using its own files, so the paragraph about writing a USB stick sits beside
the USB image and not 40 lines away from it. Only what was packed is
described: a group with nothing in this zip is left out whole, because a
README that explains a file the reader does not have reads as a packing
mistake.

TWO THINGS THIS DELIBERATELY DOES NOT DO.

It does not glob `build/*.img`. ITEMS is an ALLOWLIST, and it is one because
`build/` also holds disks that must never leave this machine -- `zork*.img`
carries Infocom story files that `tools/getstories.py` fetched and nobody here
has the right to redistribute, and every `tests/` gate writes its scratch
image into the same directory. A glob ships those the first time somebody runs
an unrelated target before cutting a release, and the mistake is invisible in
the zip listing until it is public.

It does not build anything. Run `make` and `make emu` (and any on-demand
target you want in the zip) first; a missing REQUIRED image stops the script,
and a missing optional one is reported and skipped. That split is the point --
the images `make` produces are the release (four geometries of each pair since
SPEC.md 19's 1.2MB disk, plus the 360KB-only media disk and the three
360KB-only category disks of SPEC.md 24.6 -- office, network and games, which
at that geometry are the ONLY published home of the spreadsheet and the chart
viewer), and so is the emulator system disk (`make emu`, SPEC.md 9.11.7),
which needs nothing a bare `make` does not and so is required too: it is the
kern_emu kernel with VMMOUSE.DRV and a SYSTEM.CFG that turns it on, and it
pairs with the SHIPPED apps.img, so it adds one image and no software disk.
The `apps-all` sets/`word`/`cword`/`scribe`/`runcpm`/`c64`/`apple2`/`paccman`/
`weave`/`loom` and the live media (`make live`, SPEC.md 80) are on-demand
targets that a tree without the C toolchain cannot build at all.

The zip is byte-for-byte reproducible: entries are written in ITEMS order,
every timestamp is the date in the version string, and permissions are fixed.
The images are already deterministic (tools/os88disk.py pins the volume serial
and every FAT timestamp), so the same version built from the same commit
produces the same zip, and a reader can check that.
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
emulator uses. If you are not sure, use the 1.44MB pair.

{table}

To run the 1.44MB pair in QEMU, from the folder you unpacked this into:

    qemu-system-i386 \\
      -drive file=os8088.img,format=raw,if=floppy -boot a \\
      -drive file=apps.img,format=raw,if=floppy,index=1 \\
      -chardev msmouse,id=m0 -serial chardev:m0

The last line is the mouse. os8088 drives a serial mouse, so the emulator has
to put one on a serial port, and that is QEMU's way of doing it. For another
pair, change the two file names. Other emulators have their own way of
attaching a mouse; the project's site has the current recipe and the 86Box
machine files. In an emulator, the emulator disk below is usually the better
choice for drive A."""),

    ("xt", "Extra disks for a 360KB machine", """\
A 360KB floppy holds a quarter of what a 1.44MB one does, so at that size the
programs do not all fit on the software disk. These disks carry the rest. Boot
os8088-360.img in drive A as usual, then put one of these in drive B in place
of apps360.img. There are no 1.44MB, 1.2MB or 720KB versions: at those sizes
everything on them is already on the software disk."""),

    ("emu", "In an emulator or virtual machine: the pointer without a grab", """\
emu.img is a system disk for running os8088 in an emulator or a virtual
machine. It is os8088.img with one addition: a driver for the absolute pointer
VMware defined, which QEMU, VMware, VirtualBox and the v86 browser emulator
all provide. With it the os8088 pointer sits wherever your own mouse is. There
is no clicking into the window to capture the mouse and no key combination to
let it go.

Use it in place of os8088.img, with the same apps.img in drive B:

    qemu-system-i386 \\
      -drive file=emu.img,format=raw,if=floppy -boot a \\
      -drive file=apps.img,format=raw,if=floppy,index=1

There is no mouse line this time. QEMU provides the absolute pointer by
itself, and adding the serial mouse from the command above would give the
system two mice that disagree about where the buttons are.

The driver uses instructions an 8086 does not have, so it only loads on a 386
or later. That is every emulator and virtual machine in practice. On an older
machine emu.img runs like os8088.img and uses the serial mouse, and a real
PC/XT cannot read a 1.44MB disk anyway, so for real hardware use the pairs
above."""),

    ("live", "No floppy drive: the live USB image and the live CD", """\
These carry the whole system and every program, with no floppies at all.
Booted this way the system runs from a hard-disk partition that appears as
drive C, with every program on it.

To write the USB image to a stick -- everything already on the stick is
erased -- and boot a PC from it in legacy BIOS mode, on macOS or Linux, with
the stick at /dev/sdX and unmounted:

    dd if=os8088-usb.img of=/dev/sdX bs=1M

On a Mac with the os8088 source checkout, `make burn` does this with an
interactive guide instead: it lists the attached USB flash drives, has you
confirm the one to erase by typing its name, and verifies the write. It burns
the CD too, when a burner is attached.

In QEMU, one or the other:

    qemu-system-i386 -drive file=os8088-usb.img,format=raw -boot c \\
      -chardev msmouse,id=m0 -serial chardev:m0

    qemu-system-i386 -cdrom os8088.iso -boot d \\
      -chardev msmouse,id=m0 -serial chardev:m0"""),

    ("all", "Every program, on a set of disks", """\
The software disk carries a selection. These sets carry every program,
including the ones that otherwise have a disk of their own below, spread over
as many disks as they take. Boot a system disk in drive A and put any disk of
the set in drive B; CONTENTS.TXT on each disk says which programs are on it.
There is no 720KB or 360KB set."""),

    ("progs", "One program per disk", """\
Each of these is one larger program with what it needs beside it. Boot the
system disk for your size in drive A, and put one of these in drive B in place
of the software disk. None of them is bootable on its own."""),

    ("about", "About this zip", """\
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
     "The music module the music players open. It does not fit beside the "
     "programs at this size, so it rides a disk of its own.",
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

    ("emu", "Emulator system disk", True,
     "The system disk with the absolute-pointer driver, switched on. Goes in "
     "drive A with apps.img in drive B; there is no separate software disk "
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
     "Commodore 64, the Apple II Plus, the Weave programs and their editor, "
     "the 1942 air-combat game, and everything on the software disk.",
     [("@apps-all.list", "1.44MB", "")]),
    ("all", "Everything set, 1.2MB", False,
     "The same set on 1.2MB disks.",
     [("@apps-all-120.list", "1.2MB", "")]),

    ("progs", "Word processor", False,
     "A word processor modelled on Microsoft Word for Windows 1.1a -- its "
     "menus, ribbon, ruler and keys -- written in assembly.",
     [("word.img", "1.44MB", ""), ("word120.img", "1.2MB", ""),
      ("word720.img", "720KB", ""), ("word360.img", "360KB", "")]),
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
     "AdLib or Sound Blaster. It needs a VGA or CGA card. The "
     "1.44MB disk also carries sampled gunfire and explosions for a Sound "
     "Blaster; the 360KB disk leaves them out for room.",
     [("1942.img", "1.44MB", ""), ("1942-360.img", "360KB", "")]),
    ("progs", "Weave", False,
     "Web-style programs -- markup, script and formulas -- compiled into one "
     "bundle file and run natively. Carries the runtime, three demo "
     "programs, the editor that builds them and the sources they were built "
     "from. Use this one to run a program.",
     [("weave.img", "1.44MB", ""), ("weave120.img", "1.2MB", ""),
      ("weave720.img", "720KB", ""), ("weave360.img", "360KB", "")]),
    ("progs", "Weave editor", False,
     "The same editor and runtime as the Weave disk, with the demo programs' "
     "SOURCES to open rather than the finished bundles. Use this one to "
     "change a program.",
     [("loom.img", "1.44MB", ""), ("loom120.img", "1.2MB", ""),
      ("loom720.img", "720KB", ""), ("loom360.img", "360KB", "")]),
]

# Files packed beside the images, when the image they belong to is here.
# THE C64 IS GPL-2-OR-LATER because VICE is, and C64-SPEC 1.2 says in
# as many words that "the release zip carries COPYING". Its own floppies carry
# it too -- this is the second copy, for a reader who unpacks the zip and never
# mounts a disk. (trigger image, source path, name in the zip, description)
EXTRAS = [
    ("c64.img", "apps/c64/COPYING", "COPYING.C64",
     "The GNU General Public License version 2, which is the licence the "
     "Commodore 64 emulator is under, because it is a port of VICE, which "
     "is. It applies to that one program and to nothing else here."),
    ("apple2.img", "apps/apple2/COPYING", "COPYING.APPLE2",
     "The GNU General Public License version 2, which is the licence the "
     "Apple II Plus emulator is under -- its display code came from the "
     "same project the Commodore 64 emulator's did. It applies to that one "
     "program and to nothing else here."),
    ("paccman.img", "apps/paccman/LICENSE", "LICENSE.PACCMAN",
     "The MIT licence on Andre Weissflog's pacman.c, which PaccMan was "
     "ported from. It names him as its copyright holder; the rest of this "
     "release is under the project's own MIT licence."),
]

README = """\
# os8088 {version}

os8088 is a graphical operating system for the Intel 8086, written in assembly
and booted from a floppy disk. It has overlapping windows, a menu bar, a mouse
and programs you can run several copies of at once.

This zip holds {count} files. They are grouped below by what you are trying to
do, and each group says how to use its files. Most people need only the first
group.

{toc}

{body}

Built from commit {commit}.
"""


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


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def add(zf, name, data, when):
    """One entry, with everything the OS would otherwise vary pinned flat."""
    info = zipfile.ZipInfo(name, date_time=when)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    info.create_system = 3          # unix, whatever host packed it
    zf.writestr(info, data)


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


def render(packed, extras):
    """The README body: one section per group that has a file in this zip.

    `packed` is [(item, [(name, size, note), ...])] with only the files that
    were found, @-lists already expanded; `extras` is [(name, desc)].
    """
    sections, toc = [], []
    for key, heading, intro in GROUPS:
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
                        "as files beside the images. Each says which program "
                        "it covers, and nothing else in os8088 is affected "
                        "by it.")
            out.append(intro.format(licence=para(lic)))
            out.append("")
            out.append(para("This file. It describes every other file in "
                            "the zip.", lead="- `README.md` -- ",
                            cont="  "))
            out.append(para("A SHA-256 checksum for every disk image and licence file "
                            "here.",
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


def main():
    ap = argparse.ArgumentParser(description="Pack one release zip.")
    ap.add_argument("--version", required=True, help="e.g. v1.0.20260819")
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--out", help="default: <build-dir>/os8088-<version>.zip")
    ap.add_argument("--commit", default="", help="the OS commit it was built from")
    args = ap.parse_args()

    bd = args.build_dir
    out = args.out or os.path.join(bd, "os8088-%s.zip" % args.version)
    root = "os8088-%s" % args.version
    when = version_date(args.version)

    commit = args.commit
    if not commit:
        import subprocess
        try:
            commit = subprocess.check_output(
                ["git", "rev-parse", "HEAD"], text=True).strip()
        except Exception:
            commit = "unknown"

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
                    found.append((img, size, "disk %d of %d" % (i + 1, len(imgs))))
                continue
            if os.path.isfile(os.path.join(bd, name)):
                found.append((name, size, note))
            elif required:
                missing.append(name)
            else:
                skipped.append(name)
        if found:
            packed.append((item, found))

    # An extra rides only when the image it belongs to did. A licence for a
    # program that is not in the zip is noise; a program in the zip without
    # its licence is the thing this list exists to stop.
    have = {name for _, fs in packed for name, _, _ in fs}
    extras = []
    for trigger, src, as_name, desc in EXTRAS:
        if trigger in have:
            if not os.path.isfile(src):
                print("mkzip: %s is in the zip and %s is missing -- refusing to "
                      "ship it without its licence." % (trigger, src), file=sys.stderr)
                return 1
            extras.append((as_name, src, desc))

    if missing:
        print("mkzip: these are built by `make` and `make emu` and are not in "
              "%s:" % bd, file=sys.stderr)
        for m in missing:
            print("  %s" % m, file=sys.stderr)
        print("mkzip: run `make && make emu` and try again -- refusing to pack "
              "a partial release.", file=sys.stderr)
        return 1

    images = [(name, os.path.join(bd, name)) for _, fs in packed
              for name, _, _ in fs]
    sums = ["%s  %s" % (sha256(p), name)
            for name, p in images + [(n, s) for n, s, _ in extras]]
    toc, body = render(packed, [(n, d) for n, _, d in extras])
    readme = README.format(version=args.version, commit=commit, toc=toc,
                           body=body, count=len(images) + len(extras) + 2)

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        add(zf, "%s/README.md" % root, readme.encode(), when)
        add(zf, "%s/SHA256SUMS" % root, ("\n".join(sums) + "\n").encode(), when)
        for name, p in images + [(n, s) for n, s, _ in extras]:
            with open(p, "rb") as f:
                add(zf, "%s/%s" % (root, name), f.read(), when)

    # Read it back rather than trusting the write: a truncated member is a
    # perfectly valid-looking zip until somebody unpacks it.
    with zipfile.ZipFile(out) as zf:
        bad = zf.testzip()
        if bad:
            print("mkzip: %s is corrupt in %s" % (bad, out), file=sys.stderr)
            return 1
        n = len(zf.namelist())

    raw = sum(os.path.getsize(p) for _, p in images)
    print("mkzip: %s" % out)
    print("  %d entries, %d images, %.1fMB packed from %.1fMB"
          % (n, len(images), os.path.getsize(out) / 1e6, raw / 1e6))
    print("  sha256 %s" % sha256(out))
    if skipped:
        print("  on-demand disks NOT built, so not in the zip: %s"
              % ", ".join(skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())

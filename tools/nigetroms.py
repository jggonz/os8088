#!/usr/bin/env python3
"""nigetroms: fetch the NES ROMs the INFONES disks ship, and the harness's
test fixtures (SPEC.md 91.14.2).

    python3 tools/nigetroms.py -o build/nesroms            # fetch + verify
    python3 tools/nigetroms.py -o build/nesroms --list     # print the manifest
    python3 tools/nigetroms.py -o build/nesroms --check    # verify, never fetch
    python3 tools/nigetroms.py --catalog <file> NAME NAME  # a disk's CATALOG.TXT
    python3 tools/nigetroms.py --readme  <file> NAME NAME  # ...and its README.TXT
    python3 tools/nigetroms.py --fixtures build/nesroms    # the SYNTHETIC
                                                           # refusal fixtures

tools/getstories.py is the shape and its rules are this file's rules.

**NOTHING THIS SCRIPT DOWNLOADS IS COMMITTED.** Every ROM below is someone
else's work under someone else's copyright and a git repository is not a
distribution channel for it, so the bytes land in build/, which is ignored
outright, and the disk images are built from there.

TWO QUESTIONS, TWO ANSWERS (nes-roms.md 0). "May we FETCH it at build time"
and "may we REDISTRIBUTE it on a floppy and through The Wire" are different
questions, and the second needs a GRANT. Every entry below carries one and the
`ships` field says which disks it goes on:

    A   every geometry, 360KB included
    B   720KB, 1.2MB and 1.44MB - it does not fit the 360KB disk
    L   The Wire's library only, because of a non-commercial or share-alike
        condition this project's floppies stay clear of (decision 2)

**THE TEST ROMS ARE NOT HERE AND CANNOT BE.** `christopherpow/nes-test-roms`
has no LICENSE and no permission statement in any readme - the nesdev
consensus is an inference about intent, not a grant - so nestest and blargg's
singles are fetched by the HARNESS that needs them
(apps/infones/hosttest/nicputest.sh), into build/nesroms/tests/, and ship
nowhere: not on a floppy, not through The Wire, not in this repository.

Adding your own: NESROMS='path/to/GAME.NES' in the Makefile puts it on the
disk beside these. It must already be a valid 8.3 name - os88disk.py has no
long-name handling and fails hard rather than truncating.
"""
import argparse
import hashlib
import os
import sys
import urllib.error
import urllib.request

TIMEOUT = 120


class Rom:
    """One shippable ROM.

    name     the 8.3 name it takes on the disk
    mapper   what the iNES header must say (checked after the fetch)
    prg      16KB PRG banks the header must claim
    chr      8KB CHR banks; 0 means CHR-RAM
    ships    'A', 'B' or 'L' - see the module docstring
    licence  the short name that goes in CATALOG.TXT
    offer    a GPL source offer, or None. The GPL's section 6 wants the
             corresponding source offered with the binary, so a title that
             carries one names its repository and its TAG here and README.TXT
             prints both (SPEC.md 91.14.2)
    """

    def __init__(self, name, title, author, url, sha, size, mapper, prg, chr_,
                 ships, licence, note, offer=None):
        self.name = name
        self.title = title
        self.author = author
        self.url = url
        self.sha = sha
        self.size = size
        self.mapper = mapper
        self.prg = prg
        self.chr = chr_
        self.ships = ships
        self.licence = licence
        self.note = note
        self.offer = offer


# Every SHA-256 below was computed from the fetched file, not copied from a
# page (nes-roms.md 2.2).
ROMS = [
    Rom("CROOM.NES", "Concentration Room", "Damian Yerrick",
        "https://github.com/pinobatch/croom-nes/releases/download/v0.02a/croom.nes",
        "2ce17df1ad66a8a0533c0a8739f5b5ebe275c264924bbe350c42c5ac0394f20e",
        24592, 0, 1, 1, "A", "GPL-3.0",
        "A memory game. Two players or one.",
        offer=("https://github.com/pinobatch/croom-nes", "v0.02a")),
    Rom("THWAITE.NES", "Thwaite", "Damian Yerrick",
        "https://github.com/pinobatch/thwaite-nes/releases/download/v0.04/thwaite.nes",
        "a2df24d9c9f72e56c2fdc4c703becc47a5700ad0158da8208247635ebeb3779c",
        24592, 0, 1, 1, "A", "GPL-3.0",
        "Defend six towns from missiles.",
        offer=("https://github.com/pinobatch/thwaite-nes", "v0.04")),
    Rom("RFK.NES", "robotfindskitten", "Damian Yerrick",
        "https://github.com/pinobatch/rfk-nes/releases/download/v0.10/robotfindskitten.nes",
        "13abbea91f553780c88c2a85a40b7e86fd5916026c01bfc4f88a8b9b9a9abfe1",
        32784, 0, 2, 0, "A", "Zlib",
        "Find the kitten. CHR-RAM: it writes its own tiles."),
    Rom("RHDE.NES", "RHDE: Furniture Fight", "Damian Yerrick",
        "https://github.com/pinobatch/rhde-nes/releases/download/v0.07/rhde.nes",
        "b2c4748a5b3651e393572046daf126213653b58b55472cdfdf4b863834dd0241",
        32784, 0, 2, 0, "A", "All-Permissive",
        "Furnish a house, then fight over it. CHR-RAM."),
    Rom("MEGAMTN.NES", "Mega Mountain", "AdrianMakesGames and Damian Yerrick",
        "https://raw.githubusercontent.com/pinobatch/MegaMountainNES/master/mega_mountain.nes",
        "b3a29bc7d6c272ec9af42a21eea44872f17feadcb866a5098ba28a7fbbc78d2d",
        40976, 0, 2, 1, "B", "GPL-3.0 + MIT",
        "A platformer. Engine by Doug Fraker (MIT).",
        offer=("https://github.com/pinobatch/MegaMountainNES", "master")),
]

BY_NAME = dict((r.name, r) for r in ROMS)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(65536), b""):
            h.update(blk)
    return h.hexdigest()


def ines_check(rom, path):
    """The header must say what the manifest says it says.

    This is not belt and braces: a ROM whose mapper changed between releases
    would be fetched, hashed, shipped and then REFUSED on the machine with
    `Mapper #%d is unsupported.`, which reads as a defect in the emulator.
    Checking it here names the file instead."""
    with open(path, "rb") as f:
        h = f.read(16)
    if len(h) != 16 or h[0:4] != b"NES\x1a":
        return "not an iNES file"
    mapper = (h[6] >> 4) | (h[7] & 0xF0 if h[12:16] == b"\0\0\0\0" else 0)
    if mapper != rom.mapper:
        return "mapper %d, manifest says %d" % (mapper, rom.mapper)
    if h[4] != rom.prg:
        return "%d PRG banks, manifest says %d" % (h[4], rom.prg)
    if h[5] != rom.chr:
        return "%d CHR banks, manifest says %d" % (h[5], rom.chr)
    return None


def fetch(rom, out, check_only):
    path = os.path.join(out, rom.name)
    if os.path.exists(path):
        got = sha256(path)
        if got == rom.sha:
            bad = ines_check(rom, path)
            if bad:
                print("nigetroms: %s: %s" % (rom.name, bad))
                return False
            return True
        print("nigetroms: %s has the wrong hash, re-fetching" % rom.name)
        os.remove(path)
    if check_only:
        print("nigetroms: %s is missing" % rom.name)
        return False
    print("nigetroms: fetching %-12s %s" % (rom.name, rom.title))
    try:
        with urllib.request.urlopen(rom.url, timeout=TIMEOUT) as r:
            data = r.read()
    except (urllib.error.URLError, OSError) as e:
        print("nigetroms: cannot fetch %s: %s" % (rom.url, e))
        return False
    if len(data) != rom.size:
        print("nigetroms: %s is %d bytes, manifest says %d"
              % (rom.name, len(data), rom.size))
        return False
    got = hashlib.sha256(data).hexdigest()
    if got != rom.sha:
        print("nigetroms: %s is not the pinned file" % rom.name)
        print("           want %s" % rom.sha)
        print("           got  %s" % got)
        return False
    with open(path + ".part", "wb") as f:
        f.write(data)
    os.replace(path + ".part", path)
    bad = ines_check(rom, path)
    if bad:
        print("nigetroms: %s: %s" % (rom.name, bad))
        return False
    return True


# ---------------------------------------------------------------------------
# THE SYNTHETIC FIXTURES
#
# Two REFUSALS have to be photographed on the glass and neither may be a real
# ROM: an unsupported mapper and a header that claims more than the file holds
# (SPEC.md 91.10). Both are iNES headers over zero bytes, which needs no
# licence from anybody and cannot be mistaken for software.
# ---------------------------------------------------------------------------
def fixtures(out):
    os.makedirs(out, exist_ok=True)

    # BADMAP.NES - mapper 66, which is a real mapper this port does not
    # implement, so the refusal is InfoNES's own sentence with 66 in it.
    hdr = bytearray(16)
    hdr[0:4] = b"NES\x1a"
    hdr[4] = 1                      # 1 x 16KB PRG
    hdr[5] = 1                      # 1 x 8KB CHR
    hdr[6] = 0x21                   # mapper low nibble 2, vertical mirroring
    hdr[7] = 0x40                   # ...and high nibble 4: mapper 66
    body = bytes(16384 + 8192)
    with open(os.path.join(out, "BADMAP.NES"), "wb") as f:
        f.write(bytes(hdr) + body)

    # SHORT.NES - a header claiming 256KB of PRG in a 24KB file. NONE of the
    # three reference emulators checks this and all three would read off the
    # end of their buffer; here it is refused with both numbers (SPEC.md 91,
    # nirom.c's length check).
    hdr = bytearray(16)
    hdr[0:4] = b"NES\x1a"
    hdr[4] = 16                     # 16 x 16KB = 256KB claimed...
    hdr[5] = 1
    hdr[6] = 0x01
    with open(os.path.join(out, "SHORT.NES"), "wb") as f:
        f.write(bytes(hdr) + bytes(24576))      # ...in 24KB of file
    print("nigetroms: wrote BADMAP.NES (mapper 66) and SHORT.NES "
          "(a lying header) - SYNTHETIC, and neither ships")


# ---------------------------------------------------------------------------
# THE PER-GEOMETRY TEXT FILES (the `make cpmsw` / GAMES.TXT precedent)
# ---------------------------------------------------------------------------
def catalog(path, names):
    have = [BY_NAME[n] for n in names]
    missing = [r for r in ROMS if r.name not in names]
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", newline="\r\n") as f:
        f.write("INFONES - the games on THIS disk\r\n")
        f.write("=" * 40 + "\r\n\r\n")
        for r in have:
            f.write("%-12s %s\r\n" % (r.name, r.title))
            f.write("             %s\r\n" % r.author)
            f.write("             %s\r\n" % r.licence)
            f.write("             %s\r\n\r\n" % r.note)
        if missing:
            f.write("Not on this disk:\r\n\r\n")
            for r in missing:
                why = ("it does not fit this geometry"
                       if r.ships == "B" else
                       "its licence carries a non-commercial or share-alike\r\n"
                       "             condition, so it is offered through The "
                       "Wire only")
                f.write("%-12s %s - %s\r\n" % (r.name, r.title, why))
            f.write("\r\n")
        f.write("A NOTE ON SPEED, and it is the honest one: on an IBM PC/XT\r\n")
        f.write("this program SHOWS a NES emulator running on an 8088. It is\r\n")
        f.write("not a machine to play on. The panel prints both rates - how\r\n")
        f.write("fast the game advances, and how often a picture is drawn -\r\n")
        f.write("and on a 4.77 MHz machine the second is about one picture\r\n")
        f.write("every two seconds. A 386 is playable for a slow game and a\r\n")
        f.write("486DX2/66 is near full speed.\r\n")


def readme(path, names):
    have = [BY_NAME[n] for n in names]
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", newline="\r\n") as f:
        f.write("INFONES - licences and source offers\r\n")
        f.write("=" * 40 + "\r\n\r\n")
        f.write("INFONES.O88 and INFONES.OVL are derived from InfoNES\r\n")
        f.write("(https://github.com/jay-kumogata/InfoNES) at commit\r\n")
        f.write("fe3295c0, Copyright (c) Jay Kumogata / Jay's Factory,\r\n")
        f.write("under the Apache License 2.0. The licence text is\r\n")
        f.write("LICENSE.TXT on this disk. Modification notice: derived\r\n")
        f.write("from InfoNES fe3295c0, restructured for 8086 real mode.\r\n\r\n")
        # THE ABOUT PANEL CREDITS agnes AND MIT ASKS FOR THE NOTICE. The row
        # `Scroll model from agnes (MIT)` is accurate about the code -
        # nippu.c's loopy v/t pair, its $2007 buffer and its palette mirror
        # follow agnes's expressions rather than nofrendo's, and for the
        # licence reason (SPEC.md 91.1) - so agnes is a SHIPPED-CODE reference
        # and not only the harness's oracle, and MIT wants its notice "in all
        # copies or substantial portions". A pre-wave-1 resolution (R9) put
        # that notice in the harness files alone, on the assumption that agnes
        # would stay an oracle; wave 1 took the model into shipped code, so
        # the notice travels with it. This is the one place a disk can carry
        # it: the About panel names agnes and cannot hold a licence.
        f.write("The scroll model in this port follows agnes\r\n")
        f.write("(https://github.com/kgabis/agnes), and its notice is\r\n")
        f.write("reproduced as that licence requires:\r\n\r\n")
        f.write("  MIT License\r\n\r\n")
        f.write("  Copyright (c) 2019 Krzysztof Gabis\r\n\r\n")
        f.write("  Permission is hereby granted, free of charge, to any\r\n")
        f.write("  person obtaining a copy of this software and associated\r\n")
        f.write("  documentation files (the \"Software\"), to deal in the\r\n")
        f.write("  Software without restriction, including without\r\n")
        f.write("  limitation the rights to use, copy, modify, merge,\r\n")
        f.write("  publish, distribute, sublicense, and/or sell copies of\r\n")
        f.write("  the Software, and to permit persons to whom the Software\r\n")
        f.write("  is furnished to do so, subject to the following\r\n")
        f.write("  conditions:\r\n\r\n")
        f.write("  The above copyright notice and this permission notice\r\n")
        f.write("  shall be included in all copies or substantial portions\r\n")
        f.write("  of the Software.\r\n\r\n")
        f.write("  THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF\r\n")
        f.write("  ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED\r\n")
        f.write("  TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A\r\n")
        f.write("  PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT\r\n")
        f.write("  SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY\r\n")
        f.write("  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION\r\n")
        f.write("  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR\r\n")
        f.write("  IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER\r\n")
        f.write("  DEALINGS IN THE SOFTWARE.\r\n\r\n")
        f.write("Nintendo Entertainment System and NES are trademarks of\r\n")
        f.write("Nintendo. This program is not affiliated with or endorsed\r\n")
        f.write("by Nintendo, and no Nintendo software is on this disk.\r\n\r\n")
        f.write("THE GAMES\r\n\r\n")
        for r in have:
            f.write("%-12s %s\r\n" % (r.name, r.title))
            f.write("             Copyright %s\r\n" % r.author)
            f.write("             %s\r\n" % r.licence)
            if r.offer:
                url, tag = r.offer
                f.write("             The corresponding source for this\r\n")
                f.write("             binary is %s\r\n" % url)
                f.write("             at %s, and is obtainable from that\r\n" % tag)
                f.write("             URL and from os8088.com beside this\r\n")
                f.write("             download.\r\n")
            f.write("\r\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--out", default="build/nesroms")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--fixtures", metavar="DIR")
    ap.add_argument("--catalog", metavar="FILE")
    ap.add_argument("--readme", metavar="FILE")
    ap.add_argument("names", nargs="*")
    a = ap.parse_args()

    if a.list:
        for r in ROMS:
            print("%-12s %-24s %-16s ships %s  %s"
                  % (r.name, r.title, r.licence, r.ships, r.url))
        return 0
    if a.fixtures:
        fixtures(a.fixtures)
        return 0
    if a.catalog:
        catalog(a.catalog, a.names)
        return 0
    if a.readme:
        readme(a.readme, a.names)
        return 0

    os.makedirs(a.out, exist_ok=True)
    ok = True
    for r in ROMS:
        if not fetch(r, a.out, a.check):
            ok = False
    if ok:
        print("nigetroms: %d ROM(s) in %s, every hash and every iNES header "
              "checked" % (len(ROMS), a.out))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

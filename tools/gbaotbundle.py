#!/usr/bin/env python3
"""THE GAME BOY PRECOMPILER, PACKED FOR PEOPLE WITH NO os8088 TREE
(docs/REALDMG-SPEC.md 20.12), in tools/os88vbundle.py's shape.

    make gbaotbundle          # -> build/os8088-gbaot.zip
    python3 tools/gbaotbundle.py OUT.zip --build build

One folder, `os8088-gbaot/`: the window (gbaotgui.py), everything it
imports, the binaries gbaot.py would otherwise assemble - RealDMG's
precompiled core, its templates, the host driver and the RDG_AOT package
image, in gbaot-bin/ - and a README.TXT. Unpacked anywhere it needs Python
and `pip install unicorn`, and no assembler: gbaot.py finds gbaot-bin/
beside itself when there is no apps/realdmg/ above it.

THE FILE LIST IS COMPUTED, NOT WRITTEN: the roots and every tools/ module
they import (os88vbundle.closure). A third-party import that is not
THIRD_PARTY stops the build. The zip is DETERMINISTIC: sorted entries, every
timestamp 1980-01-01.
"""
import argparse
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import os88vbundle  # noqa: E402

ROOTS = ("gbaotgui.py", "gbaot.py", "os88disk.py", "os88pkg.py")
THIRD_PARTY = {"unicorn"}
BINS = ("aothost.bin", "rdgcore.bin", "rdgtmpl.bin", "rdgcore.inc", "realdmgaot.bin")
FOLDER = "os8088-gbaot"

README = """os8088 GAME BOY PRECOMPILER
===========================

Turns a Game Boy cartridge into an os8088 package that runs it with the
whole cartridge translated ahead of time: RealDMG, with nothing to
translate while the game plays and no profile to play for first.

    python3 gbaotgui.py

or, without a window:

    python3 gbaot.py make GAME.GB -o GAME.O88 [--replay GAME.RP1,GAME.SS1]

NEEDS
  Python 3.8 or newer with Tk (python.org's installer has it; on Linux the
  python3-tk package), and one Python package:
      python3 -m pip install unicorn
  Nothing else: the parts that would be assembled are in gbaot-bin/.

USING IT
  1. Choose the cartridge. The package is written beside it, GAME.O88.
  2. Make the package (a minute or two; a 1 MB cartridge longer).
  3. With "Put the ROM inside the package" ticked - the default - GAME.O88
     is the whole game in one file: copy it to an os8088 disk - or tick a
     floppy and it makes one - and double-click it. Unticked, put GAME.O88
     and the cartridge, under the name the window shows, in one folder of
     an os8088 disk and double-click the package: it opens its own ROM, and
     refuses any other.

  WITH THE BOX TICKED THE PACKAGE CONTAINS THE CARTRIDGE ROM. It is the
  cartridge itself, and it must not be shared or posted. To give someone
  only the precompiled code, untick the box: that package carries no ROM and
  opens the one beside it, which they bring themselves. A package is one
  cartridge's either way.

  COUNTERS: the window runs the translated game for a while with a scripted
  player and puts the code that ran most in the first of the package's
  images - on a machine whose memory holds only some of them, the rest of
  the code is interpreted, so the hottest code is what should be kept. A
  replay you recorded in RealDMG (Save State > Record Replay: NAME.RPn and
  the NAME.SSn it starts from) does the same from your own play.

  CHECK runs that many frames of the package's code beside the interpreter
  and compares every one - minutes, and the proof that it is the same
  machine.

This folder is built from the os8088 tree by `make gbaotbundle`;
`python3 gbaot.py --help` has every command.
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--build", default=os.path.join(os.path.dirname(HERE), "build"))
    a = ap.parse_args()
    import gbaot
    gbaot.aot_image(a.build)               # (every binary, current)
    mods, other = os88vbundle.closure(ROOTS)
    stray = other - THIRD_PARTY
    if stray:
        sys.exit("gbaotbundle: the precompiler imports %s, which is neither in tools/ nor "
                 "expected (THIRD_PARTY)" % ", ".join(sorted(stray)))
    files = [(m, open(os.path.join(HERE, m), "rb").read(), 0o755) for m in mods]
    d = gbaot.aotdir(a.build)
    for b in BINS:
        files.append(("gbaot-bin/" + b, open(os.path.join(d, b), "rb").read(), 0o644))
    files.append(("gbaot-bin/rdgdefs.inc",
                  open(os.path.join(gbaot.INC, "rdgdefs.inc"), "rb").read(), 0o644))
    files.append(("README.TXT", README.replace("\n", "\r\n").encode(), 0o644))
    tmp = a.out + ".part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data, mode in sorted(files):
            zi = zipfile.ZipInfo("%s/%s" % (FOLDER, name), os88vbundle.STAMP)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = (0o100000 | mode) << 16
            z.writestr(zi, data)
    os.replace(tmp, a.out)
    print("gbaotbundle: %s - %d files: %s" % (a.out, len(files),
                                              " ".join(n for n, _, _ in sorted(files))))
    return 0


if __name__ == "__main__":
    sys.exit(main())

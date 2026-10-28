#!/usr/bin/env python3
"""THE FILE SPLITTER, PACKED FOR PEOPLE WITH NO os8088 TREE (SPEC.md 20.14,
20.17), in tools/os88vbundle.py's shape.

    make czbundle          # -> build/os8088-cz.zip
    python3 tools/os88czbundle.py OUT.zip --dos build/os88cz.com

One folder, `os8088-cz/`: the window (os88czgui.py), the command line
(os88cz.py), everything they import, OS88CZ.COM - the same tool for a PC
running DOS - and a README.TXT. Unpacked anywhere it needs Python with Tk
and nothing else.

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

ROOTS = ("os88czgui.py", "os88cz.py")
THIRD_PARTY = {"tkinterdnd2"}           # optional: os88drop.py's file drop
FOLDER = "os8088-cz"

README = """os8088 FILE SPLITTER AND COMPRESSOR
===================================

Gets a file onto an os8088 machine that is bigger than a floppy, or makes a
file smaller on the disk without the machine noticing.

    python3 os88czgui.py

or, without a window:

    python3 os88cz.py split  VIDEO.V88 --size 720k     -> VIDEO.001, .002 ...
    python3 os88cz.py join   VIDEO.001                 -> VIDEO.V88
    python3 os88cz.py pack   README.TXT -o README.CZ   -> one packed file
    python3 os88cz.py unpack README.CZ  -o README.TXT
    python3 os88cz.py info   VIDEO.003

NEEDS
  Python 3.8 or newer with Tk (python.org's installer has it; on Linux the
  python3-tk package). Nothing else. Optional: `python3 -m pip install
  tkinterdnd2` lets you drop a file on the window on any system (on
  Windows a drop works without it).

SPLIT AND JOIN
  Split cuts a file into parts that each fit a floppy of the size you
  choose - 360 KB, 720 KB, 1.2 MB or 1.44 MB, each less 2 KB so the part
  still fits once an OS has put its own files on the disk - or any size
  you type (bytes, 500k, 1.1m), compressing what gets smaller. Ticked, the
  window also writes a floppy image per part, on the smallest disk that
  holds it. On the os8088 machine, copy the parts into one folder and
  choose Uncompress on any one of them in its disk window: the original
  appears beside them. Or choose
  Uncompress To... on a part still on its floppy, which asks where the
  result goes and then for each disk in turn.

PACK AND UNPACK
  Pack makes one compressed file that os8088 expands as it reads it, so
  every program sees the original. It must fit in the machine's memory
  whole; for anything bigger, split it instead.

ON A PC RUNNING DOS
  OS88CZ.COM is the same tool for the machine at the other end of a disk
  walk:
    OS88CZ J PART [DEST]           join a set, asking for each disk
    OS88CZ S FILE [SIZE] [DEST]    split (SIZE 360, 720, 1200, 1440
                                   or bytes)
    OS88CZ U FILE OUT              expand a packed file

This folder is built from the os8088 tree by `make czbundle`;
`python3 os88cz.py --help` has every command.
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--dos", required=True, help="OS88CZ.COM to ship")
    a = ap.parse_args()
    mods, other = os88vbundle.closure(ROOTS)
    stray = other - THIRD_PARTY
    if stray:
        sys.exit("czbundle: the splitter imports %s, which is neither in tools/ nor "
                 "expected (THIRD_PARTY)" % ", ".join(sorted(stray)))
    files = [(m, open(os.path.join(HERE, m), "rb").read(), 0o755) for m in mods]
    files.append(("OS88CZ.COM", open(a.dos, "rb").read(), 0o644))
    files.append(("README.TXT", README.replace("\n", "\r\n").encode(), 0o644))
    tmp = a.out + ".part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data, mode in sorted(files):
            zi = zipfile.ZipInfo("%s/%s" % (FOLDER, name), os88vbundle.STAMP)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = (0o100000 | mode) << 16
            z.writestr(zi, data)
    os.replace(tmp, a.out)
    print("czbundle: %s - %d files: %s" % (a.out, len(files),
                                           " ".join(n for n, _, _ in sorted(files))))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""THE VIDEO BATTLE BUILDER, PACKED FOR PEOPLE WITH NO os8088 TREE
(docs/plans/VBATTLE-BUILDER-PLAN.md), the encoder bundle's shape (SPEC.md
98.2.13, tools/os88vbundle.py).

    make vbtbundle          # -> build/os8088-battle-builder.zip
    python3 tools/os88vbtbundle.py OUT.zip --engine build/vbattle.o88

One folder, `os8088-battle-builder/`: the builder's window, everything it
imports or runs as a process (the analysis, the bundle builder, and the
video encoder the analysis runs on footage), VBATTLE.O88 to play what it
builds, and a README.TXT. The file list is COMPUTED by os88vbundle's own
closure over every `import` from the ROOTS, so a module the builder starts
to need is in the next zip with nothing to remember; a third-party import
that is not expected stops the build. The zip is deterministic: sorted
entries, every timestamp 1980-01-01.
"""
import argparse
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import os88vbundle as VB    # noqa: E402

ROOTS = ("os88vbtgui.py", "vbtana.py", "os88vbt.py", "vbtproj.py",
         "os88venc.py")
THIRD_PARTY = {"numpy", "PIL", "tkinterdnd2"}
DATA = VB.DATA
FOLDER = "os8088-battle-builder"

README = """os8088 VIDEO BATTLE BUILDER
===========================

Builds a Video Battle (BATTLE.VBT) out of footage: an idle loop, the
actions' clips joined to it exactly, the music and its loop, the menus,
the characters and the enemy. VBATTLE.O88, in this folder, plays it on
os8088: put the two side by side on a disk.

    python3 os88vbtgui.py [PROJECT.vbp]

The window: the Battle, Idle and Clips tabs (the Menus and Build tabs
come in later rounds - see docs/plans/VBATTLE-BUILDER-PLAN.md in the
os8088 tree). Everything it does is also a command:

    python3 os88vbt.py demo-project demo.vbp     the demo battle, as a
                                                 project (no footage needed)
    python3 os88vbt.py build demo.vbp            -> BATTLE.VBT beside it
    python3 os88vbt.py recommend PROJECT.vbp     the loop's and every clip's
                                                 points, with their scores
    python3 vbtproj.py PROJECT.vbp               what is wrong with it, and
                                                 what the engine does not
                                                 play yet
    python3 vbtana.py --help                     the analysis, one command
                                                 each

A project keeps its encoded frames in PROJECT.cache/ beside it: the first
look at a video encodes it (a minute or two), every one after reads it back.

NEEDS
  Python 3.11 or newer with Tk (python.org's installer has it; on Linux the
  python3-tk package), and two Python packages:
      python3 -m pip install numpy pillow
  ffmpeg and ffprobe on the PATH (https://ffmpeg.org): they read your
  footage and sounds.
  To LISTEN in the window: afplay (macOS), winsound (Windows, built in),
  or paplay, aplay or ffplay (Linux).
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--engine", required=True, help="VBATTLE.O88 to ship")
    a = ap.parse_args()
    mods, other = VB.closure(ROOTS)
    stray = other - THIRD_PARTY
    if stray:
        sys.exit("os88vbtbundle: the builder imports %s, which is neither in "
                 "tools/ nor expected (THIRD_PARTY) - add it there and to "
                 "the README, or drop the import" % ", ".join(sorted(stray)))
    files = [(m, open(os.path.join(HERE, m), "rb").read(), 0o755)
             for m in mods]
    for m in mods:
        for name, src in DATA.get(m, ()):
            files.append((name, open(os.path.join(HERE, src), "rb").read(),
                          0o644))
    files.append(("VBATTLE.O88", open(a.engine, "rb").read(), 0o644))
    files.append(("README.TXT", README.replace("\n", "\r\n").encode(),
                  0o644))
    tmp = a.out + ".part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data, mode in sorted(files):
            zi = zipfile.ZipInfo("%s/%s" % (FOLDER, name), VB.STAMP)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = (0o100000 | mode) << 16
            z.writestr(zi, data)
    os.replace(tmp, a.out)
    print("os88vbtbundle: %s - %d files: %s" % (
        a.out, len(files), " ".join(n for n, _, _ in sorted(files))))
    return 0


if __name__ == "__main__":
    sys.exit(main())

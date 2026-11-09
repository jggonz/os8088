#!/usr/bin/env python3
"""THE VIDEO BATTLE BUILDER, PACKED FOR PEOPLE WITH NO os8088 TREE
(docs/plans/completed/VBATTLE-BUILDER-PLAN.md), the encoder bundle's shape (SPEC.md
98.2.13, tools/os88vbundle.py).

    make vbtbundle          # -> build/os8088-battle-builder.zip
    python3 tools/os88vbtbundle.py OUT.zip --engine build/vbattle.o88

One folder, `os8088-battle-builder/`: the builder's window, everything it
imports or runs as a process (the analysis, the bundle builder, the
video encoder the analysis runs on footage, and the two disk makers),
VBATTLE.O88 to play what it builds, with `--boot` what a hard disk BOOTS
from in boot/ (the encoder bundle's own list, SPEC.md 98.2.13), a
README.TXT, and GUIDE.HTML - the user's guide, docs/battle-guide/, with
every guide/ picture the page names (os88vbundle.guide_files) and only
those. The file list is COMPUTED by os88vbundle's own
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
         "vbtrules.py", "vbtband.py", "os88venc.py",
         "os88disk.py", "os88hdd.py",       # (the disks, run as processes)
         "vbtprof.py")                      # (VBATTLED.O88's profiles, read)
THIRD_PARTY = {"numpy", "PIL", "tkinterdnd2"}
DATA = VB.DATA
GUIDE = os.path.join(HERE, "..", "docs", "battle-guide")
FOLDER = "os8088-battle-builder"

README = """os8088 VIDEO BATTLE BUILDER
===========================

Builds a Video Battle (BATTLE.VBT) out of footage: an idle loop, the
actions' clips joined to it exactly, the music and its loop, the menus,
the characters and the enemy. VBATTLE.O88, in this folder, plays it on
os8088: put the two side by side on a disk.

    python3 os88vbtgui.py [PROJECT.vbp]

GUIDE.HTML, in this folder, is the full guide: open it in a web browser.
It walks through making a battle, tab by tab, and says when and why to
reach for each setting.

The window: the Battle, Idle, Clips, Openings, Title & endings, Actions,
Characters & menus, Enemy and Build tabs. Title & endings is a TITLE loop
before the openings (left on Enter by a fade, or by a clip) and how victory
and defeat end: held, faded or joined to an END loop that waits for Enter,
or faded into playing again. Build writes BATTLE.VBT beside the project, checks
every join, shows each clip against the disk it is made for and what the
engine will claim of a 640 KB machine, and makes a disk of it: a floppy, or
a hard disk image for 86Box or a real disk (it BOOTS os8088: what it boots
from is in boot/). Everything it does is also a command:

    python3 os88vbt.py demo-project demo.vbp     the demo battle, as a
                                                 project (no footage needed)
    python3 os88vbt.py build demo.vbp            -> BATTLE.VBT beside it
    python3 os88vbt.py disk demo.vbp --disk 3    ...and a 1.44 MB floppy
                                                 of it (0-3 the floppies,
                                                 4-6 the hard disks)
    python3 os88vbt.py recommend PROJECT.vbp     the loop's and every clip's
                                                 points, with their scores
    python3 vbtproj.py PROJECT.vbp               what is wrong with it, and
                                                 what the engine does not
                                                 play yet
    python3 vbtana.py --help                     the analysis, one command
                                                 each
    python3 vbtprof.py VBPROF1.DAT               what a play with
                                                 VBATTLED.O88 recorded:
                                                 where a machine stuttered

It builds BATTLE.VBT for CGA, and BATTLEV.VBT (VGA, 4, 8 or 16 colours)
and BATTLEH.VBT (Hercules) when the Battle tab asks: put the one for your
adapter beside VBATTLE.O88, or all three. On a machine with XMS (a 286 and
up) or an EMS board (an 8088 too), the engine holds the battle in that
memory while the title waits, and plays from it.

A project keeps its encoded frames in PROJECT.cache/ beside it: the first
look at a video encodes it (a minute or two), every one after reads it back.

NEEDS
  Python 3.11 or newer with Tk (python.org's installer has it; on Linux the
  python3-tk package), and two Python packages:
      python3 -m pip install numpy pillow
  ffmpeg and ffprobe on the PATH (https://ffmpeg.org): they read your
  footage and sounds.
  To DROP files on the window (a project, videos, sounds, pictures):
  built in on Windows; on macOS and Linux
      python3 -m pip install tkinterdnd2
  (without it, every drop has a button that does the same).
  To LISTEN in the window: afplay (macOS), winsound (Windows, built in),
  or paplay, aplay or ffplay (Linux).
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--engine", required=True, help="VBATTLE.O88 to ship")
    ap.add_argument("--diag", help="VBATTLED.O88 to ship beside it (the "
                    "engine that writes a play's profile, SPEC.md 126.8)")
    ap.add_argument("--boot", metavar="BUILD",
                    help="a build folder to take a hard disk's BOOT files "
                    "from into boot/ (os88mkdisk's HD_BOOT and HD_DRV), so "
                    "the builder's hard disks boot")
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
    if a.diag:
        files.append(("VBATTLED.O88", open(a.diag, "rb").read(), 0o644))
    if a.boot:
        import os88mkdisk as M
        for _, f in M.HD_BOOT + M.HD_DRV:
            files.append(("boot/" + f, open(os.path.join(a.boot, f),
                                            "rb").read(), 0o644))
    files.append(("README.TXT", README.replace("\n", "\r\n").encode(),
                  0o644))
    for name, path in VB.guide_files(GUIDE):    # the guide and its pictures
        files.append((name, open(path, "rb").read(), 0o644))
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

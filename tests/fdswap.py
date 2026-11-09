#!/usr/bin/env python3
"""fdswap - a floppy swapped under an open Disk window lists the NEW disk.

    python3 tests/fdswap.py [--machine os8088_5150_cga_gla]

WHAT IT GUARDS. A 5150's drives have no change line, so every navigation
(a full mount) re-reads the boot sector to notice a swap (SPEC.md 18.9).
Since kernel hot-paths pass 1 a LISTING mount empties the read-ahead cache
first and reads LBA 0 THROUGH it as one fill of eight sectors - the boot
sector, the FATs and the root's head in one int 13h - and re-tags every
record with the new disk's signature (SPEC.md 18.95.11). That is safe only
because the flush comes first: the re-tag would otherwise hand the OLD
disk's chunks the new disk's key, and the listing would be the old disk.

THE SCRIPT. Boot with apps360 in B:, open B: (its root must list as mtools
reads it), enter APPS, swap B: for games360 under the open window (MartyPC
`mount`; three guest seconds for the motor to stop, the only thing an XT
can notice by), go up with `..` - a full mount - and compare the listing
with games360's root as mtools reads it. Then enter games360's MEDIA, swap
back, go up, and compare with apps360's root. Every comparison is the
window's own listing cache (os88ui.listing) against mdir.

RED, TRIED: with the mount's `call dsk_rah_flush` replaced by three nops,
the listing after the swap is apps360's old root (APPS, GAMES) where
games360's should be, and this fails naming the missing and extra names.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88ui                                                   # noqa: E402

FAIL = []


def check(ok, what):
    print(("  ok    " if ok else "  FAIL  ") + what)
    if not ok:
        FAIL.append(what)


def mdir_root(img):
    """The root's names as mtools reads them: 8.3, upper case, folders too."""
    out = subprocess.run(["mdir", "-b", "-i", img, "::/"],
                         capture_output=True, text=True, check=True).stdout
    names = set()
    for line in out.splitlines():
        n = line.strip().rstrip("/").split("/")[-1]
        if n and not n.startswith("."):
            names.add(n.upper())
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    os.chdir(ROOT)
    with tempfile.TemporaryDirectory() as td:
        apps = os.path.join(td, "apps.img")
        games = os.path.join(td, "games.img")
        shutil.copyfile("build/apps360.img", apps)  # mount uses files in place
        shutil.copyfile("build/games360.img", games)
        motor = int(4.772727e6 * 3)                  # > the 2.03 s motor-off
        with os88ui.boot("build/os8088-360.img", apps=apps,
                         machine=a.machine) as ui:
            m = ui.m

            def shown():
                return {n.upper() for n, t in ui.listing() if t != 3}

            def same(want, what):
                got = shown()
                check(got == want, "%s (missing %s, extra %s)"
                      % (what, sorted(want - got)[:6], sorted(got - want)[:6]))

            ui.open_drive("B")
            same(mdir_root(apps), "B: lists apps360's root as mtools does")
            ui.open("APPS")
            m.mount(1, games)
            m.advance(cycles=motor)
            m.run()
            ui.open("..")
            ui.settle()
            same(mdir_root(games), "after a swap, `..` lists games360's root")
            ui.open("MEDIA")
            m.mount(1, apps)
            m.advance(cycles=motor)
            m.run()
            ui.open("..")
            ui.settle()
            same(mdir_root(apps), "swapped back, `..` lists apps360's root")
    print("fdswap: %s" % ("FAIL (%d)" % len(FAIL) if FAIL else "ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())

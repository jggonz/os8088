#!/usr/bin/env python3
"""navcalls - how many int 13h calls a Disk-window navigation costs.

    python3 tests/navcalls.py [--geom 360|720]

On a 5150 one int 13h call is ~400 ms almost regardless of its length
(PERFORMANCE.md Part 2), so a navigation is priced in CALLS. Kernel
hot-paths pass 1 took them down (disk-3, disk-5; SPEC.md 18.95.11): a
listing mount reads the boot sector, the FATs and the root's head as ONE
8-sector fill, and the FAT window's fill serves the root.

This counts the FDC's reads on MartyPC (Marty.disk(), which counts calls
exactly; no time is asserted) for: open B: (its root), enter APPS, `..` to
the root, enter APPS again, `..` again - and asserts the counts the pass
measured:

               open root   enter APPS   Up
    360KB      2           2            1      (os8088_5150_cga_gla)
    720KB      2           2            1      (os8088_5150_herc_sb_720_gla,
                                                the one 80-cylinder profile)

A navigation that costs MORE calls is a regression worth ~0.4 s each on the
target. One that costs fewer is a change somebody should explain: the row
says so rather than passing silently.

RED, TRIED: on the pass's base (a7ee7fab, before disk-3/disk-5) the same
script reads 4 / 3 / 3 on both geometries.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88ui                                                   # noqa: E402

GEOM = {
    "360": ("build/os8088-360.img", "build/apps360.img", "os8088_5150_cga_gla"),
    "720": ("build/os8088-720.img", "build/apps720.img",
            "os8088_5150_herc_sb_720_gla"),
}
WANT = [("open B: (root)", 2), ("enter APPS", 2), ("Up (..) to root", 1),
        ("enter APPS again", 2), ("Up (..) again", 1)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--geom", choices=sorted(GEOM), action="append")
    a = ap.parse_args()
    os.chdir(ROOT)
    fail = []
    for g in a.geom or sorted(GEOM):
        sysimg, apps, machine = GEOM[g]
        with os88ui.boot(sysimg, apps=apps, machine=machine) as ui:
            m = ui.m
            steps = [lambda: ui.open_drive("B"), lambda: ui.open("APPS"),
                     lambda: ui.open(".."), lambda: ui.open("APPS"),
                     lambda: ui.open("..")]
            for (name, want), fn in zip(WANT, steps):
                c0 = m.disk().get("reads", 0)
                fn()
                ui.settle()
                got = m.disk().get("reads", 0) - c0
                ok = got == want
                print("  %s  %sKB  %-18s %d call(s), want %d"
                      % ("ok  " if ok else "FAIL", g, name, got, want))
                if not ok:
                    fail.append("%sKB %s: %d, want %d" % (g, name, got, want))
    print("navcalls: %s" % ("FAIL (%d)" % len(fail) if fail else "ok"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Every package on kern_small's SYSTEM disk OPENS on the 128KB machine.

    python3 tests/smalllaunch.py [machine]

SPEC.md 24.5.6 puts the whole apps payload on the small system disk, so a
128KB machine with one drive has every program on the floppy it booted from -
and nothing launched them all. The Calculator had not opened on kern_small at
all: its entry carried wm_create's CF through the calls after it on the
understanding that every one preserves FLAGS, and on kern_small three of them
do not - OSAPI_WM_ONDRAG, _ONTIMER and _TIMER answer CF = 1 by design (SPEC.md
13.8.2, 13.9: "a package tests CF and does without"). So the refusal of a
feature the Calculator does without came back from its entry as LD_EABORT,
"Load failed", every time. Every row that drives the Calculator boots kern_big,
and the 128KB rows open Note Pad.

The list is READ OFF THE BUILT DISK, not written here, so a package that joins
the small disk is covered the day it does - which is the gap this row exists
for. Each is opened by path, its window confirmed by os88ui, and closed again
before the next, on ONE boot: kern_small has six window slots (SPEC.md
11.102), so what is left standing has to be bounded, and a package that
cannot be closed is a finding too.

**How to make it go red** (WRITING-TESTS 1): take the `clc` back out of the
end of apps/calc/calc.asm's cal_entry - CALC.O88 fails with LD_EABORT.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, os.path.join(HERE, "unit"))
# kern_small, and set BEFORE the imports: os88sym checks the map against the
# binary the moment it is asked (tests/regrowshed.py's note).
os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")
import os88build                                            # noqa: E402
os88build.use_build("build/smallk")
import os88sym                                              # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_128k"

# A PRIVATE TREE (WRITING-TESTS 5.2), regrowshed's shape: `make small` writes
# build/*.drv beside build/smallk/, so out of tree is what lets this run beside
# anything else - and both readers are pointed at it, the kernel's at smallk/.
_T = os88build.tree(targets=("small",))
_T.apply()
os.environ["OS88_BUILD"] = os.path.join(_T.dir, "smallk")
os.environ["OS88_DEFINES"] = "KERN_SMALL"
os88sym.default_defines("KERN_SMALL")

import os88ui                                               # noqa: E402
from t_image import Vol                                     # noqa: E402

DISK = _T.img("small360.img")


def packages():
    """Every *.O88 on the disk, as the path os88ui.path takes."""
    v = Vol(open(DISK, "rb").read(), DISK)
    out = []
    for folder, name, attr, clus, length in v.walk():
        base, ext = name[:8].decode().rstrip(), name[8:].decode().rstrip()
        if ext == "O88":
            out.append("A:/%s%s.O88" % (folder, base))
    return sorted(out)


def main():
    pkgs = packages()
    if len(pkgs) < 10:
        sys.exit("smalllaunch: only %d packages on %s - the walk has lost "
                 "the disk, not the other way round" % (len(pkgs), DISK))
    bad = []
    with os88ui.boot(DISK, machine=MACHINE, limit=180) as ui:
        for p in pkgs:
            try:
                w = ui.path(p)
            except os88ui.UIError as e:
                print("FAIL %-24s %s" % (p, str(e).splitlines()[-1]))
                bad.append(p)
            else:
                print(" ok  %-24s -> %r" % (p, w.title))
                ui.close(w)
            # ...and the Disk windows the path came through: six slots. The
            # list is RE-READ per close, because a snapshot names records
            # that a close (or a failed launch's own teardown) has freed.
            for _ in range(6):
                left = [d for d in ui.windows()
                        if d.title.upper() in ("APPS", "GAMES", "SYSTEM")]
                if not left:
                    break
                ui.close(left[0])
    if bad:
        sys.exit("smalllaunch: %d of %d packages did not open on kern_small: "
                 "%s" % (len(bad), len(pkgs), ", ".join(bad)))
    print("smalllaunch: all %d packages on the small system disk open on %s"
          % (len(pkgs), MACHINE))


if __name__ == "__main__":
    main()

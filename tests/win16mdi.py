#!/usr/bin/env python3
"""win16mdi - windows inside a window: the Multiple Document Interface
(SPEC.md 124.27, docs/plans/WIN16-ANY-PLAN.md S20b).

    python3 tests/win16mdi.py            (row win16mdi)
    python3 tests/win16mdi.py --keep     leave the QEMU for a look

*As a user, File Manager 3.0 opens its drive window, and Program Manager
its groups.*

MDI.EXE (tests/win16/mdi.c, `make win16fixtures`) makes a frame with a
Window menu, an MDICLIENT and three children by WM_MDICREATE, and checks
them as File Manager and Program Manager use them: the last made active and
all three listed in the Window menu; WM_MDITILE side by side, apart,
covering the client and each told its size; WM_MDICASCADE staggered, one
size, the active one lowest; SC_MINIMIZE an icon at the client's bottom, a
tile then above it, WM_MDIICONARRANGE and WM_MDIRESTORE; WM_MDIMAXIMIZE
filling the client, merging the frame's bar and answered by
WM_MDIGETACTIVE, then WM_MDIRESTORE putting both back; WM_MDINEXT; and
WM_MDIDESTROY one at a time until none is active. Its MessageBox, read off
the program's stack, is "MDI OK" or names each check that failed.
"""
import argparse
import os
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
from win16mines import QDrive, b                              # noqa: E402
import win16fpu as F                                          # noqa: E402

NAME = "MDI.EXE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16mdi: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
        return 77
    import os88ui
    import xmcheck
    bad = []
    work = tempfile.mkdtemp(prefix="w16i-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, [NAME])
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        F.run_prog(d, ui, xmcheck.sym, S, dw, NAME, bad, "QEMU",
                   want={NAME: "MDI OK"})
        if not a.keep:
            d.p.close()
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16mdi: MDI's checks passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

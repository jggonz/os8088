#!/usr/bin/env python3
"""win16ctlproc - WIN16's controls have a window procedure a program can call
(SPEC.md 124.25.18, docs/plans/WIN16-NEXT-PLAN.md; Visual Basic 3.0's runtime
superclasses Edit, Button and the rest).

    python3 tests/win16ctlproc.py            (row win16ctlproc)
    python3 tests/win16ctlproc.py --keep     leave the QEMU for a look

*As a Visual Basic program's user, the program starts instead of ending with
"Unexpected error; quitting".*

CTLPROC.EXE (tests/win16/ctlproc.c, `make win16fixtures`) asks GetClassInfo for
Edit and Button, registers a class of its own over each copied procedure,
creates them and uses them (WM_SETTEXT, EM_REPLACESEL, WM_GETTEXT,
BM_SETCHECK, BM_GETCHECK), and subclasses a plain Edit with
SetWindowLong(GWL_WNDPROC), eating one key. Its MessageBox, read off the
program's stack, is "CTLPROC OK" or names each check that failed.
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

NAME = "CTLPROC.EXE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16ctlproc: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
        return 77
    import os88ui
    import xmcheck
    bad = []
    work = tempfile.mkdtemp(prefix="w16m-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, [NAME])
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        F.run_prog(d, ui, xmcheck.sym, S, dw, NAME, bad, "QEMU",
                   want={NAME: "CTLPROC OK"})
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
        print("win16ctlproc: CTLPROC's checks passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

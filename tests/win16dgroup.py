#!/usr/bin/env python3
"""win16dgroup - the program's own DGROUP answers the Global calls
(SPEC.md 124.9.2, docs/plans/WIN16-NEXT-PLAN.md).

    python3 tests/win16dgroup.py            (row win16dgroup)
    python3 tests/win16dgroup.py --keep     leave the QEMU for a look

*As a user, a Visual Basic 3.0 program starts instead of ending silently.*

DGROUP.EXE (tests/win16/dgroup.c, `make win16fixtures`) asks GlobalHandle(SS),
GlobalSize, GlobalLock, GlobalUnlock, grows its DGROUP with GlobalReAlloc as
VBRUN300 does, writes past the old stack and reads it back, and is refused a
size past 64 KB. Its MessageBox, read off the program's stack, is "DGROUP OK"
or names each check that failed.
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

NAME = "DGROUP.EXE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16dgroup: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
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
                   want={NAME: "DGROUP OK"})
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
        print("win16dgroup: DGROUP's checks passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

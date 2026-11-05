#!/usr/bin/env python3
"""win16lheap - a local heap grows when it is full (SPEC.md 124.25.19).

    python3 tests/win16lheap.py             (row win16lheap)
    python3 tests/win16lheap.py --keep      leave the QEMU for a look

*As a user, a Visual Basic 3.0 program that allocates past its heap goes on
instead of ending "Unexpected error; quitting".*

LHEAP.EXE (tests/win16/lheap.c, `make win16fixtures`) makes a 1 KB local heap
in its DGROUP and allocates 100 blocks of 100 bytes in it, checks every
answer distinct and every block's data, LocalReAllocs a moveable block
larger and calls LocalCompact; then it LocalInit's a heap of 496 bytes in a
locked GlobalAlloc block of 2 KB and allocates 60 blocks there, so the heap
passes the block's own end and the block must grow; then the same in a block
whose neighbour is in use (kern_mega moves it, real mode stops short with the
data intact), and registers 70 window classes, past the class block's first
64 records (SPEC.md 124.25.18.1). Its MessageBox, read off
the program's stack, is "LHEAP OK" or names each check that failed.
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

NAME = "LHEAP.EXE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16lheap: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
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
                   want={NAME: "LHEAP OK"})
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
        print("win16lheap: LHEAP's checks passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

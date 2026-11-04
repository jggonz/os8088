#!/usr/bin/env python3
"""win16bigbmp - a GDI bitmap past 64,000 bytes (SPEC.md 124.25.16,
docs/plans/WIN16-NEXT-PLAN.md).

    python3 tests/win16bigbmp.py            (row win16bigbmp)
    python3 tests/win16bigbmp.py --keep     leave the QEMU for a look

*As a user, Paintbrush 3.0 keeps a 640 x 480 picture whole instead of
saying "Insufficient memory for this operation".*

BIGBMP.EXE (tests/win16/bigbmp.c, `make win16fixtures`) makes a 640 x 480
bitmap - 153,600 bytes in WIN16's 4 bits a pixel, three tiles - selects it
into a memory DC, draws in each tile and across the seam between the
first two, reads every pixel back, BitBlts a block of the last tile onto
the window and a source that crosses the seam, reads a scan through
GetDIBits, writes one through SetDIBits, StretchBlts the seam and deletes
it. Its MessageBox, read off the program's stack, is "BIGBMP OK" or names
each check that failed. This row is kern_big's (QEMU, real mode); the
kern_mega arm of the same fixture is looked at by hand (KERN-MEGA-PLAN).
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

NAME = "BIGBMP.EXE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16bigbmp: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
        return 77
    import os88ui
    import xmcheck
    bad = []
    work = tempfile.mkdtemp(prefix="w16bb-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, [NAME])
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        F.run_prog(d, ui, xmcheck.sym, S, dw, NAME, bad, "QEMU",
                   want={NAME: "BIGBMP OK"})
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
        print("win16bigbmp: BIGBMP's checks passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

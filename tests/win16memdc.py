#!/usr/bin/env python3
"""win16memdc - drawing into a memory DC, and ExtFloodFill on the window's
(SPEC.md 124.25.3-124.25.6, docs/plans/WIN16-ANY-PLAN.md S19b).

    python3 tests/win16memdc.py            (row win16memdc)
    python3 tests/win16memdc.py --keep     leave the QEMU for a look

*As a user, a Windows 3.x paint program keeps what I draw in its picture.*

MEMDC.EXE (tests/win16/memdc.c, `make win16fixtures`) draws into a 64 x 48
bitmap held by a memory DC with every call Paintbrush draws with - PatBlt,
FillRect, Rectangle, MoveTo/LineTo, a filled Polygon, ExtFloodFill, TextOut,
SetPixel - and reads each back through GetDIBits; sends the bits out and
back by GetBitmapBits/SetBitmapBits and GetDIBits/SetDIBits; cuts a fill by
a clip region and ExcludeClipRect; copies the screen into a bitmap and a
bitmap onto the screen through a moved window origin; and fills on the
window's DC, inside a frame and across the whole client; and calls what VBRUN300
imports from GDI (SPEC.md 124.25.13: OffsetWindowOrg, SetRectRgn,
CreatePenIndirect, CreateBrushIndirect, Arc, Pie, the metafile calls). Its MessageBox,
read off the program's stack, is "MEMDC OK" or names each check that failed.
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

NAME = "MEMDC.EXE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16memdc: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
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
                   want={NAME: "MEMDC OK"})
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
        print("win16memdc: MEMDC's checks passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

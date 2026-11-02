#!/usr/bin/env python3
"""win16dll - a program's own libraries (SPEC.md 124.23,
docs/plans/WIN16-ANY-PLAN.md S18).

    python3 tests/win16dll.py            (row win16dll)
    python3 tests/win16dll.py --keep     leave the QEMU for a look

*As a user, a Windows program that comes with libraries of its own runs.*

DLLUSE.EXE (tests/win16/dlluse.c, `make win16fixtures`) imports W16DLL.DLL
by name and by ordinal; W16DLL's LibMain must have run before the program's
first instruction; GetModuleHandle, GetProcAddress (by name and ordinal),
GetModuleFileName and GetModuleUsage answer for it; LoadLibrary("W16DLL2.DLL")
loads a second library that imports W16DLL too, and its function raises
W16DLL's counter - one DGROUP for both users; FreeLibrary runs W16DLL2's WEP;
LoadLibrary of a file that is not there answers 2. The program's MessageBox,
read off its stack, is "DLLUSE OK" or names each check that failed. The
three files sit at B:'s root, the program's folder, which is where WIN16
looks for a library (124.23.1).
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

FILES = ("DLLUSE.EXE", "W16DLL.DLL", "W16DLL2.DLL")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    missing = [f for f in FILES if not os.path.exists(b(os.path.join("win16", f)))]
    if missing:
        print("win16dll: SKIP - no build/win16/%s (`make win16fixtures`)" % missing[0])
        return 77
    import os88ui
    import xmcheck
    bad = []
    work = tempfile.mkdtemp(prefix="w16d-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, FILES)
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        F.run_prog(d, ui, xmcheck.sym, S, dw, "DLLUSE.EXE", bad, "QEMU",
                   want={"DLLUSE.EXE": "DLLUSE OK"})
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
        print("win16dll: DLLUSE and its two libraries - every check passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

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
looks for a library first (124.23.1).

Every leg also puts W16DLL3.DLL in LIBS/ below the program's folder, where
no plain name is looked for: LoadLibrary("W16DLL3.DLL") must answer 2 and
LoadLibrary of its full path (lower case, with a '/') load it (124.25.22);
and USER's SetWindowsHookEx, UnhookWindowsHookEx and CallNextHookEx, bound
by name through GetProcAddress as CTL3D.DLL binds them, must answer a set,
call-next and unhook round trip.

The second and third legs (124.23.1.1) put the two libraries in B:'s
SYSTEM/APPDATA/WIN16 - the Windows directory - and in SYSTEM/APPDATA/WIN16/SYSTEM
- the system directory, where a setup installs a shared library - with only
DLLUSE.EXE at the root: every check must still pass, `filepath` among them:
the program opens the path GetModuleFileName answers for W16DLL, which is the
library's own folder and not the program's.
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
LIBS = ("W16DLL3.DLL",)         # in LIBS/ below the program, by full path only


def run_leg(a, bad, sysdir):
    """sysdir: None, or the folder the libraries are in instead of the root"""
    import os88ui
    import xmcheck
    work = tempfile.mkdtemp(prefix="w16d-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        libs = ["LIBS:" + b(os.path.join("win16", f)) for f in LIBS]
        if sysdir:
            F.bdisk(bimg, 1440, ("DLLUSE.EXE",), extra=[
                sysdir + ":" + b(os.path.join("win16", f))
                for f in FILES[1:]] + libs)
        else:
            F.bdisk(bimg, 1440, FILES, extra=libs)
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    missing = [f for f in FILES + LIBS if not os.path.exists(b(os.path.join("win16", f)))]
    if missing:
        print("win16dll: SKIP - no build/win16/%s (`make win16fixtures`)" % missing[0])
        return 77
    bad = []
    for leg, sysdir in (("beside", None),
                        ("Windows directory", "SYSTEM/APPDATA/WIN16"),
                        ("system directory", "SYSTEM/APPDATA/WIN16/SYSTEM")):
        before = len(bad)
        run_leg(a, bad, sysdir)
        print("win16dll: %s leg %s" % (leg, "FAILED" if len(bad) > before
                                       else "ok"))
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16dll: DLLUSE and its two libraries - every check passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

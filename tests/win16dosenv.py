#!/usr/bin/env python3
"""win16dosenv - GetDOSEnvironment answers SELECTOR:0 (SPEC.md 124.25.21).

    python3 tests/win16dosenv.py             (row win16dosenv)
    python3 tests/win16dosenv.py --keep      leave the QEMU for a look

*As a user, Microsoft Mozart (Visual Basic 3.0) loads SPUSH.VBX instead of
being closed with a protection fault in the library's C start-up.*

DOSENV.EXE (tests/win16/dosenv.c, `make win16fixtures`) asks for the
environment and checks that the answer's offset is 0 and its selector is
readable (the start-up's own `or ax,ax / jz / mov dx,ax / mov es,dx` loads
one that exists), that the list ends at a double NUL followed by the word 1,
and that a second call answers the same block. Its MessageBox, read off the
program's stack, is "DOSENV OK" or names each check that failed.
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

NAME = "DOSENV.EXE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16dosenv: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
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
                   want={NAME: "DOSENV OK"})
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
        print("win16dosenv: DOSENV's checks passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

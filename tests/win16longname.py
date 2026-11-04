#!/usr/bin/env python3
"""win16longname - resources asked for by names past 15 characters
(SPEC.md 124.10).

    python3 tests/win16longname.py            (row win16longname)
    python3 tests/win16longname.py --keep     leave the QEMU for a look

*As a user, Microsoft Arcade's Asteroids starts a game on F2 instead of
reporting "Error code = -1".*

LONGNAME.EXE (tests/win16/longname.c, `make win16fixtures`) opens a dialog
named in 21 characters (Asteroids' own NUMBEROFPLAYERSDIALOG, which a 16-byte
copy of the name cut to 15 and so never found), loads a menu named in 40, a
bitmap named in 255 and one by "#300", a resource of a 28-character TYPE and
accelerators named in 26, finds the dialog in lower case, and must NOT find
it cut to 15 or with a character added. Its MessageBox, read off the
program's stack, is "LONGNAME OK" or names each check that failed. A dialog
that was never made is a row that reads "dlg21".
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

NAME = "LONGNAME.EXE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16longname: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
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
                   want={NAME: "LONGNAME OK"})
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
        print("win16longname: LONGNAME's checks passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

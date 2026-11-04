#!/usr/bin/env python3
"""win16vbuser - the KERNEL and USER functions Visual Basic 3.0's runtime
imports (SPEC.md 124.25.15, docs/plans/WIN16-NEXT-PLAN.md L1).

    python3 tests/win16vbuser.py            (row win16vbuser)
    python3 tests/win16vbuser.py --keep     leave the QEMU for a look

*As a user, a Visual Basic 3.0 program no longer ends the first time it
calls one of thirteen functions WIN16 lacked.*

VBUSER.EXE (tests/win16/vbuser.c, `make win16fixtures`) calls GetCurrentPDB,
SetHandleCount, GetCodeHandle, FreeModule (on W16DLL.DLL, loaded twice),
GetClassWord and GetClassLong, GetUpdateRgn, BeginDeferWindowPos,
DeferWindowPos and EndDeferWindowPos, CreateIcon, EndMenu and
TrackPopupMenu, and checks each answer against a value it computes. The two
TrackPopupMenu calls (SPEC.md 124.25.16) wait for the host: this row sends Down
and Enter to the first popup (the window must get WM_COMMAND 1) and Esc to the
second (no command), each time the menu loop shows `w16_mtrk` set with a popup
open. Its MessageBox, read
off the program's stack, is "VBUSER OK" or names each check that failed.
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

FILES = ("VBUSER.EXE", "W16DLL.DLL")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    missing = [f for f in FILES if not os.path.exists(b(os.path.join("win16", f)))]
    if missing:
        print("win16vbuser: SKIP - no build/win16/%s (`make win16fixtures`)" % missing[0])
        return 77
    import os88ui
    import xmcheck
    bad = []
    work = tempfile.mkdtemp(prefix="w16v-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, FILES)
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        st = {"last": None, "n": 0}

        def pump(p):
            # the host half of TrackPopupMenu: answer each popup once it is
            # up. A popup is NEW by the x it was placed at (w16_mnx): the gap
            # between two menus is too short for a poll to see
            if p.pb("w16_mtrk") == 1 and p.pb("w16_mkbd") == 2 \
                    and p.pb("w16_mlev") >= 1 and p.pw("w16_mnx") != st["last"]:
                st["last"] = p.pw("w16_mnx")
                for k in (("down", "ret"), ("esc",))[min(st["n"], 1)]:
                    d.hmp("sendkey " + k)
                st["n"] += 1

        F.run_prog(d, ui, xmcheck.sym, S, dw, "VBUSER.EXE", bad, "QEMU",
                   want={"VBUSER.EXE": "VBUSER OK"}, pump=pump)
        if st["n"] != 2:
            bad.append("TrackPopupMenu was answered %d times, not 2" % st["n"])
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
        print("win16vbuser: every check passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

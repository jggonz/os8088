#!/usr/bin/env python3
"""win16nowin - a Windows program with no WIN16 to run it (SPEC.md 54.4.2.4,
124.18.1, docs/plans/WIN16-ANY-PLAN.md S24).

    python3 tests/win16nowin.py            (row win16nowin)
    python3 tests/win16nowin.py --keep     leave the QEMU for a look

*As a user, a Windows program on a disk with no WIN16 anywhere tells me what
it needs.*

B: holds TINY16.EXE and nothing else; the shipped system disk in A: has
APPS\\DOS.O88 and no WIN16.O88. A double-click on B:\\TINY16.EXE opens DOS
(`.EXE` is its extension), which hands the NE to WIN16.O88. It is nowhere,
so the toast must say `Needs WIN16.O88`.

Before 54.4.2.4 the miss re-ran the sweep by extension, which answered DOS
again, which handed the program on again inside the call, each round one
more DOS on the UI task's stack: the machine stopped on STACK OVERFLOW TASK
00 and no toast came.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
from win16mines import QDrive, b                              # noqa: E402
import win16fpu as F                                          # noqa: E402

NAME = "TINY16.EXE"
WANT = "WIN16.O88"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    exe = b(os.path.join("win16", NAME))
    if not os.path.exists(exe):
        print("win16nowin: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
        return 77
    import os88ui
    import xmcheck
    sym = xmcheck.sym
    bad = []
    work = tempfile.mkdtemp(prefix="w16n-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", bimg,
                        "--size", "1440", exe], check=True, capture_output=True)
        d = QDrive(work, bimg, a.keep)
        ui = os88ui.UI(d, mouse=object(), sym=sym, verbose=False)
        dw = F.open_b(d, ui, sym)
        i, _ = ui.entry(NAME, dw)
        t0 = F.ticks(d)
        d.dblclick(*ui.row_xy(dw, i - ui.scroll(dw)))
        seen = {}

        def toasted():
            txt, on = ui.toast()
            if on and "Needs" in txt:
                seen["t"] = txt
                return True
            return False
        try:
            d.wait(toasted, 120, "a toast saying what TINY16.EXE needs")
        except RuntimeError as e:
            txt, on = ui.toast()
            bad.append("%s - the toast %s %r" % (e, "shows" if on else
                                                  "last staged", txt))
        if "t" in seen:
            print("1. the toast: %r" % seen["t"])
            if WANT not in seen["t"]:
                bad.append("the toast says %r, not that it needs %s"
                           % (seen["t"], WANT))
        d.idle(2.0)
        t1 = F.ticks(d)
        print("2. the BIOS tick went %d -> %d" % (t0, t1))
        if t1 <= t0:
            bad.append("the machine stopped: the BIOS tick did not move")
        if a.keep:
            print("kept: %s (QMP %s)" % (work, d.sock))
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16nowin: a Windows program with no WIN16 says it needs one")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

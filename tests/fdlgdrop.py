#!/usr/bin/env python3
"""SPEC.md 38.0.1: FDLG.DRV's claim comes back on EVERY way a dialog ends.

    python3 tests/fdlgdrop.py

kern_small ships the Standard File dialog as an on-demand module (SPEC.md
38.0), and SPEC.md 2.8.3 makes the FEATURE decide when to give the image
back.  Four routes end a dialog and only ONE of them used to:

    close / minimize box   fdlg_reap_x -> fdlg_gate     dropped
    Open / Save button     fdlg_commit -> fdlg_close    LEAKED
    Cancel button          .b_cancel   -> fdlg_close    LEAKED
    Escape                 .close      -> fdlg_close    LEAKED

The three that leaked clear `[fdlg_win]` from INSIDE the image's own
W_ONCLICK, and `mod_drop` lived behind a guard on that same word - in
`ui.inc`'s ladder, in `fdlg_reap`'s thunk and once more at the top of
`fdlg_reap_x`.  So the pass that should have noticed was turned away by the
very store it was meant to notice, and `mod_tab[MOD_FDLG].seg` stayed
non-zero for the rest of the session: a 16KB claim held for a dialog nobody
could see, on the machine with 128KB in it.

THE ASSERTION IS THE TABLE WORD AND NOT A PICTURE, because the leak is
invisible: the dialog really is gone, the window really is destroyed, and
the next dialog reuses the image it never gave back.  Only the heap knows.

Each route is checked as a TRANSITION - loaded while the dialog is up, zero
after - so a build that had simply stopped loading the module would fail the
first half rather than pass the second.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
# kern_small ONLY - the module does not exist on kern_big, where fdlg.inc is
# resident and there is nothing to give back. Set before the imports, because
# os88sym checks the map against the binary the moment it is asked.
os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")
os.environ.setdefault("OS88_BUILD", "build/smallk")
import os88marty as M                                      # noqa: E402
from os88mouse import Mouse                                # noqa: E402
from os88fixture import need                               # noqa: E402
from os88geom import (WIN_SIZE, MAX_WIN, FD_BX1, FD_BX2,   # noqa: E402
                      FD_BY0, FD_BY1, FD_BY2, FD_BH,
                      FD_LX1, FD_LX2, FD_ROW0, FD_ROWH)

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_128k"
ARM = ("KERN_SMALL",)
def equ(path, name):                # tests/diskclone.py's helper. The row
    src = open(path).read()         # index has moved once already (HIBER.DRV
    m = re.search(r"^%s\s+equ\s+(\d+)" % name, src, re.M)   # took slot 3),
    if not m:                       # and a stale one reads ANOTHER module's
        sys.exit("%s: no `%s equ`" % (path, name))          # MODR_SEG word
    return int(m.group(1))


MOD_FDLG = equ("kernel/mod.inc", "MOD_FDLG")
MODR_SIZE = equ("kernel/mod.inc", "MODR_SIZE")
W_FLAGS, W_X, W_Y, W_W, W_H, W_TITLE = 0, 2, 4, 6, 8, 10
# fdlg.inc's chrome comes out of os88geom, which reads the kernel: this file
# clicks a button column and a listing row, and a local copy of either is a
# row that clicks empty background and reports the FEATURE broken.

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


need("build/muptest.img")           # `all` builds nothing under tests/
need("build/small360.img")           # ...and `all` does not build kern_small

with M.launch("build/small360.img", apps="build/muptest.img",
              machine=MACHINE) as m:
    S = lambda n: m.sym(n, ARM)                            # noqa: E731
    M.settle(m, limit=180)
    M.no_saver(m)
    mo = Mouse(marty=m)
    tab = S("mod_tab")

    def held():
        """mod_tab[MOD_FDLG].MODR_SEG - 0 = the image is not in RAM."""
        return int.from_bytes(m.read(tab + MOD_FDLG * MODR_SIZE, 2), "little")

    def wins():
        b = m.read(S("wm_wins"), WIN_SIZE * MAX_WIN)
        return [tuple(_u16(b, i * WIN_SIZE + o)
                      for o in (W_X, W_Y, W_W, W_H, W_TITLE))
                for i in range(MAX_WIN) if _u16(b, i * WIN_SIZE + W_FLAGS) & 2]

    def titled(sym):
        want = S(sym) - (M.KERNEL_SEG << 4)      # W_TITLE is a NEAR offset
        return next((w for w in wins() if w[4] == want), None)

    def dlg():
        return titled("fdlg_s_topen") or titled("fdlg_s_tsave")

    def btn(d, which):
        cx, cy = d[0] + 1, d[1] + 18
        top = (FD_BY0, FD_BY1, FD_BY2)[which - 1]
        return cx + (FD_BX1 + FD_BX2) // 2, cy + top + FD_BH // 2

    def row(d, n):
        """The MIDDLE of listing row n. The whole row is the hit target, so
        aiming at the frame's centre needs nothing about where the text pen
        or the icon column falls."""
        cx, cy = d[0] + 1, d[1] + 18
        return (cx + (FD_LX1 + FD_LX2) // 2,
                cy + FD_ROW0 + n * FD_ROWH + FD_ROWH // 2)

    print(f"== {MACHINE} : FDLG.DRV is given back on every route "
          f"(SPEC.md 38.0.1) ==")
    check("nothing held at the desktop", held() == 0, f"(seg={held():04X})")

    # --- put muptest up; its second window is what opens a dialog --------
    vw = int.from_bytes(m.read(S("vid_w"), 2), "little")
    step = int.from_bytes(m.read(S("desk_zstep"), 2), "little")
    h1 = int.from_bytes(m.read(S("desk_zh1"), 2), "little")
    mo.dblclick(vw - 40, 32 + step + h1 // 2)
    M.settle(m)
    d = wins()[-1]
    mo.click(d[0] + d[2] // 2, d[1] + 9)        # raise the Disk window
    M.settle(m)
    mo.dblclick(d[0] + 40, d[1] + 18 + 30)      # launch MUPTEST
    M.settle(m)
    w = wins()[-1]
    OPEN = (w[0] + 1 + 100 + 31, w[1] + 18 + 40 + 9)

    def open_dialog():
        """muptest's own control, so the package window stays put between
        routes - re-walking the desktop would raise a different window and
        move every coordinate under us."""
        mo.menu(OPEN[0], OPEN[1], OPEN[0] + 2, OPEN[1])
        M.settle(m)
        return dlg()

    def dismiss(label, act):
        d = open_dialog()
        if d is None:
            check(f"{label}: a dialog opened", False)
            return
        check(f"{label}: the image is held while the dialog is up",
              held() != 0, f"(seg={held():04X})")
        act(d)
        M.settle(m)
        time.sleep(1.5)                 # a UI pass or two past the dismissal
        check(f"{label}: ...and given back when it ends",
              held() == 0, f"(seg={held():04X})")
        check(f"{label}: the dialog really did close", dlg() is None)

    def press(pt):
        mo.to(*pt)
        mo._edge(True)
        time.sleep(0.4)
        mo._edge(False)                 # SPEC.md 13.8.3: buttons fire on the
                                        # release, so a plain click is wrong

    dismiss("Cancel button", lambda d: press(btn(d, 2)))
    dismiss("Escape", lambda d: m.key("Escape"))
    dismiss("close box", lambda d: mo.click(d[0] + 8, d[1] + 9))
    # ...and Open LAST, because it is the only route that needs a selection:
    # SPEC.md 38 greys the default button until one exists (SPEC.md 47 - a
    # fact, not a guess), so pressing it with nothing picked is a no-op and
    # would read exactly like a leak.
    def commit(d):
        mo.click(*row(d, 0))
        time.sleep(0.6)
        press(btn(d, 1))

    dismiss("Open button", commit)

print()
if fails:
    sys.exit(f"fdlgdrop: FAILED: {', '.join(fails)}")
print("fdlgdrop: all routes give the image back")

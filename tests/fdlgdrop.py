#!/usr/bin/env python3
"""SPEC.md 38.0.1: FDLG.DRV's claim comes back on EVERY way a chooser ends.

    python3 tests/fdlgdrop.py [machine]

kern_small ships the Standard File chooser's glue as an on-demand module
(SPEC.md 38.0): `fdlg_open` loads it, and `fdlg_reap` drops it once
`[fdlg_win]` is 0. SPEC.md 2.8.3 makes the FEATURE decide when the image goes
back, and four routes end a chooser:

    Save button   (commit)   FDH_BTN  -> fdlg_post  -> fdlg_reap
    Cancel button            FDH_BTN  -> fdlg_post  -> fdlg_reap
    Escape                   FDH_KEY  -> fdlg_post  -> fdlg_reap
    close box                fdlg_gate (the window went away under it)

The first three POST an answer from inside the chooser's own callbacks and the
fourth is found by the gate, so they reach the drop by different roads - and
the old dialog leaked the image on exactly three of them, because `mod_drop`
sat behind a compare of the very word those three had just cleared. A 16KB
claim held for the rest of the session on the machine with 128KB in it.

THE ASSERTION IS THE TABLE WORD AND NOT A PICTURE, because the leak is
invisible: the chooser really is gone, the window really is destroyed, and the
next chooser reuses the image it never gave back. Only `mod_r_fdlg` (mod_tab's
FDLG row, MODR_SEG) knows.

Each route is checked as a TRANSITION - loaded while the chooser is up, zero
after - so a build that had simply stopped LOADING the module fails the first
half rather than passing the second.

The chooser is muptest's: its button Two opens a SAVE chooser (the form that
does not grey its default button, so the commit needs only a name - a single
click on a file puts one in the box, SPEC.md 38.4).
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
# kern_small ONLY - the module does not exist on kern_big, where fdlg.inc is
# resident and there is nothing to give back. Set before the imports, because
# os88sym checks the map against the binary the moment it is asked.
os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")
import os88build                                           # noqa: E402
os88build.use_build("build/smallk")
import os88geom as geom                                    # noqa: E402
import os88ui                                              # noqa: E402
from os88fixture import need                               # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_128k"


def equ(path, name):                # tests/diskclone.py's helper
    src = open(path).read()
    mm = re.search(r"^%s\s+equ\s+(\d+)" % name, src, re.M)
    if not mm:
        sys.exit("%s: no `%s equ`" % (path, name))
    return int(mm.group(1))


MOD_FDLG = equ("kernel/mod.inc", "MOD_FDLG")
MODR_SIZE = equ("kernel/mod.inc", "MODR_SIZE")
fails = []


def check(name, cond, note=""):
    print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, note))
    if not cond:
        fails.append(name)


need("build/muptest.img")           # `all` builds nothing under tests/
need("build/small360.img")          # ...and `all` does not build kern_small

with os88ui.boot("build/small360.img", apps="build/muptest.img",
                 machine=MACHINE) as ui:
    m = ui.m
    row = ui._S("mod_r_fdlg")
    print("== %s : FDLG.DRV is given back on every route (SPEC.md 38.0.1) =="
          % MACHINE)
    check("mod_r_fdlg is mod_tab's FDLG row",
          row == ui._S("mod_tab") + MOD_FDLG * MODR_SIZE)

    def held():
        """MODR_SEG of the FDLG row - 0 = the image is not in RAM."""
        return int.from_bytes(m.read(row, 2), "little")

    check("nothing held at the desktop", held() == 0, "(seg=%04X)" % held())

    # --- put muptest up; its button Two is what opens a chooser -------------
    ui.path("B:/MUPTEST.O88")
    ui.settle()
    mup = ui.window("MupTest")
    cx, cy = mup.content[:2]
    TWO = (cx + 100 + 31, cy + 40 + 9)     # muptest's mu_layout: Two is
                                           # content (100,40) 63x19

    def dismiss(label, act):
        ui.raise_window(ui.window("MupTest"))
        ui.mo.click(*TWO, settle=0)        # fires on muptest's RELEASE
        w = ui.chooser()
        check("%s: the chooser is the Save form" % label,
              w.title == "Save As", "(%r)" % w.title)
        check("%s: the image is held while the chooser is up" % label,
              held() != 0, "(seg=%04X)" % held())
        act(w)
        ui.chooser_gone()
        try:                            # the drop is the reap's, one pass on
            ui._wait(lambda: held() == 0, "the image given back",
                     os88ui.T_NAV)
        except os88ui.UIError:
            pass                        # ...the check below says so
        check("%s: ...and given back when it ends" % label,
              held() == 0, "(seg=%04X)" % held())

    def commit(w):
        # a single click on a FILE fills the Save box (SPEC.md 38.4), and the
        # box must hold a name or Save only beeps - which would read as a leak
        name, _ = ui.listing(w)[0]
        ui.mo.click(*ui.row_xy(w, 0), settle=0)
        ui._wait(lambda: bytes(m.read(ui._S("fm_ebuf"), 13))
                 .split(b"\0")[0].decode("ascii", "replace") == name,
                 "%s in the Save box" % name, os88ui.T_NAV)
        ui.settle()                     # the box's line is drawn
        ui.chooser_button(ui.CH_OPEN, w)

    dismiss("Cancel button",
            lambda w: ui.chooser_button(ui.CH_CANCEL, w))
    dismiss("Escape", lambda w: m.key("Escape"))
    dismiss("close box", lambda w: ui.close(w))
    dismiss("Save button (commit)", commit)

print()
if fails:
    sys.exit("fdlgdrop: FAILED: %s" % ", ".join(fails))
print("fdlgdrop: all routes give the image back")

#!/usr/bin/env python3
"""SPEC.md 13.8.3 / 38.3: the chooser's column buttons fire on the RELEASE.

    python3 tests/fdlgup.py [machine]

The Standard File chooser is a Disk window in a chooser role (SPEC.md 38.1),
and on kern_big its button column - Open|Save, Cancel, Drive - is ids 3 to 5
of the Disk window's OWN button set: `fm_brect` takes their rects from
`fdlg_brect`, `fm_bhit` walks five ids for the chooser, and `fm_onup_x` hands
a fired id >= 3 to the chooser (SPEC.md 38.3). So they press, track, cancel on
slide-off and fire on release (SPEC.md 13.7) exactly like Refresh - and that
is what this row holds them to, because a press that fired would leave no way
to think better of a mis-aimed press, which is the whole of what SPEC.md 13.6
says a control with no safe prefix action wants the release for.

SIX CASES. Every verdict is guest STATE where there is one - [fdlg_win] for
"did it close", the chooser block's FS_DRV for "did Drive fire", [fm_dbtn] for
"which button is DRAWN pressed" - and pixels only for what is a picture:

  E  a press draws Cancel DOWN            -> [fm_dbtn] = Cancel, and its
                                             rect changed under the press
  B  ...and back UP when it slides off    -> [fm_dbtn] = 0, the rect upright
  A  the slide-off release fires NOTHING  -> the chooser is still up
  D  press Cancel, release on DRIVE       -> still up AND FS_DRV unmoved -
                                             neither the pressed button nor
                                             the one under the release fired
  F  press Drive and HOLD                 -> FS_DRV unmoved while held; the
                                             release moves it (SPEC.md 38.11)
  C  press and release ON Cancel          -> the chooser closes

C and F are the ones that say the feature did not eat the feature it
decorates: a build that had simply stopped dispatching column ids would pass
A, B and D. F is also the direct positive form of the claim - the same button
NOT firing on its press and firing on its release.

The chooser is muptest's: its button Two opens a Save chooser (any chooser
would do; this is the fixture the row has always used).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88geom as geom                                     # noqa: E402
import os88marty as M                                       # noqa: E402
import os88ui                                               # noqa: E402
from os88fixture import need                                # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
FM_BCANCEL, FM_BDRIVE = 4, 5        # files.inc's ids: the column is 3..5
fails = []


def check(name, cond, note=""):
    print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, note))
    if not cond:
        fails.append(name)


need("build/muptest.img")          # `all` builds nothing under tests/

with os88ui.boot("build/os8088-360.img", apps="build/muptest.img",
                 machine=MACHINE) as ui:
    m, mo = ui.m, ui.mo
    mono = m.video()["type"] in ("cga", "mda", "herc")
    print("== %s : the chooser fires on the release (SPEC.md 13.8.3) =="
          % MACHINE)

    def lit(rect):
        if mono:
            _, _, rows = m.vram()
            return sum(rows[y][x] for y in range(rect[1], rect[3] + 1)
                       for x in range(rect[0], rect[2] + 1))
        W, _, px = m.fbuf()
        return sum(1 for y in range(rect[1], rect[3] + 1)
                   for x in range(rect[0], rect[2] + 1)
                   if any(px[(y * W + x) * 3:(y * W + x) * 3 + 3]))

    def dbtn():
        return ui._byte("fm_dbtn")

    def drv():
        return m.read((geom.KERNEL_SEG << 4) + ui._word("fdlg_blk")
                      + geom.FS_DRV, 1)[0]

    def up():
        return ui._word("fdlg_win") not in (0, 0xFFFF)

    def wait(cond, what):
        ui._wait(cond, what, os88ui.T_NAV)

    ui.path("B:/MUPTEST.O88")
    ui.settle()
    cx, cy = ui.window("MupTest").content[:2]
    ui.raise_window(ui.window("MupTest"))
    mo.click(cx + 100 + 31, cy + 40 + 9, settle=0)   # muptest's Two
    w = ui.chooser()

    cancel = ui.chooser_button_xy(ui.CH_CANCEL, w)
    drive = ui.chooser_button_xy(ui.CH_DRIVE, w)
    rect = (cancel[0] - geom.FM_BTN_W // 2, cancel[1] - geom.FM_BTN_H // 2,
            cancel[0] + geom.FM_BTN_W // 2, cancel[1] + geom.FM_BTN_H // 2)
    d0 = drv()

    # --- E: the press DRAWS it down ----------------------------------------
    mo.to(cancel[0], cancel[1] - 40)        # park off the column first
    ui.settle()                             # ...the next reads are PIXELS
    upright = lit(rect)
    mo.to(*cancel)
    mo._edge(True)
    wait(lambda: dbtn() == FM_BCANCEL, "Cancel to be drawn pressed")
    ui.settle()
    down = lit(rect)
    check("a press draws Cancel DOWN", down != upright,
          "([fm_dbtn]=%d; %d lit held, %d upright)" % (dbtn(), down, upright))

    # --- B: it comes back UP while still held, off the button --------------
    mo.to(cancel[0] - 90, cancel[1], l=True)        # l=True: STILL HELD
    wait(lambda: dbtn() == 0, "Cancel to be drawn upright again")
    ui.settle()
    off = lit(rect)
    check("...and back UP when the pointer slides off it", off == upright,
          "(%d lit, upright is %d)" % (off, upright))

    # --- A: the release lands off it: NOTHING fires ------------------------
    mo._edge(False)
    M.pace(m, 1.0)          # a fired Cancel posts and the reap closes it a
                            # pass later: give it that pass, then ask
    check("a slide-off release does NOT close the chooser", up())

    # --- D: released on a DIFFERENT button: nothing fires ------------------
    mo.to(*cancel)
    mo._edge(True)
    wait(lambda: dbtn() == FM_BCANCEL, "Cancel to be drawn pressed")
    mo.to(*drive, l=True)                   # slide onto Drive, still held
    wait(lambda: dbtn() == 0, "Cancel to let go")
    check("...and sliding onto Drive does not press Drive", dbtn() == 0,
          "([fm_dbtn]=%d)" % dbtn())
    mo._edge(False)
    M.pace(m, 1.0)
    check("released on ANOTHER button: the chooser is still up", up())
    check("...and Drive did not fire either", drv() == d0,
          "(FS_DRV %d -> %d)" % (d0, drv()))

    # --- F: Drive does NOT fire on its press, and DOES on its release ------
    mo.to(*drive)
    mo._edge(True)
    wait(lambda: dbtn() == FM_BDRIVE, "Drive to be drawn pressed")
    M.pace(m, 1.0)
    check("Drive held: nothing has fired yet", drv() == d0,
          "(FS_DRV %d -> %d)" % (d0, drv()))
    mo._edge(False)
    try:
        wait(lambda: drv() != d0, "Drive to move the chooser")
    except os88ui.UIError:
        pass                                # ...said below
    check("Drive released: it fires (SPEC.md 38.11)", drv() != d0,
          "(FS_DRV %d -> %d)" % (d0, drv()))
    check("...and the chooser is still up", up())

    # --- C: press AND release on Cancel: it closes -------------------------
    # The one that says the feature did not eat the feature it decorates.
    mo.to(*cancel)
    mo._edge(True)
    wait(lambda: dbtn() == FM_BCANCEL, "Cancel to be drawn pressed")
    mo._edge(False)
    try:
        ui.chooser_gone()
    except os88ui.UIError:
        pass
    check("press-and-release ON Cancel closes the chooser",
          ui._word("fdlg_win") == 0)

print()
if fails:
    print("%d FAILED: %s" % (len(fails), ", ".join(fails)))
    sys.exit(1)
print("all pass")

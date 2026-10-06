#!/usr/bin/env python3
"""The chooser's default button: REDRAWN IN PLACE must equal FRESHLY PAINTED.

    python3 tests/fdlggrey.py [machine]

The Standard File chooser is a Disk window in a chooser role (SPEC.md 38.1),
and in its OPEN form the column's first button - Open, the default - is
GREYED while nothing is selected (SPEC.md 38.3, 47). A selection change does
not repaint the window: it XORs two bands and redraws that one button in place
(SPEC.md 38.8, `FDH_SEL` -> `fdlg_drawbtn`). That partial redraw is the thing
under test, because a button redrawn without its ground ors the new caption
onto the old one - and a greyed caption then comes out identical to a live one
while the frame dithers correctly, which reads as SPEC.md 47 rule 1 broken
when the mechanism is fine.

Each state is reached by a PARTIAL redraw and then compared against the same
state reached by a FULL repaint (`V` twice: the Disk window's view toggle,
which keeps the selection and goes through fm_repaint - a window MOVE would
not do, SPEC.md 11.96.12 replays a moved window's pixels instead of drawing
them):

  greyed -> live      a click on a row          (fm_onclick's FDH_SEL)
  live   -> live      Down, on kern_big         (22.26's FDH_SEL: no flip,
                                                 so NO redraw, SPEC.md 38.8)
  live   -> greyed    a click on empty list     (fm_onclick's .clear)

The negative control is free: the live and greyed pictures must DIFFER, so a
button region that read nothing (a wrong rect, a button drawn elsewhere)
cannot pass the equalities.

The chooser is Note Pad's File > Open. Point it at kern_small, where the glue
is the on-demand FDLG.DRV (SPEC.md 38.0), with os88sym's own knobs:

    OS88_DEFINES=KERN_SMALL OS88_BUILD=build/smallk \\
    OS88_SYSIMG=build/small360.img python3 tests/fdlggrey.py

The small system disk carries the apps (SPEC.md 24.5.6), so Note Pad is
launched off A: there and off B: on the big build.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88geom as geom                                     # noqa: E402
import os88ui                                               # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
SYS = os.environ.get("OS88_SYSIMG", "build/os8088-360.img")
SMALL = "smallk" in os.environ.get("OS88_BUILD", "")
NP = os.environ.get("OS88_NP", ("A:" if SMALL else "B:")
                    + "/APPS/NOTEPAD.O88")
APPS = "build/apps360.img"
fails = []


def check(name, cond, note=""):
    print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, note))
    if not cond:
        fails.append(name)


def blk(ui):
    return (geom.KERNEL_SEG << 4) + ui._word("fdlg_blk")


def fs(ui, off, n=2):
    return int.from_bytes(ui.m.read(blk(ui) + off, n), "little")


def btn(ui, w):
    """The default button's pixels - its 63x14 rect and a 2px margin for the
    default ring - as a tuple, so two states compare exactly."""
    xc, yc = ui.chooser_button_xy(ui.CH_OPEN, w)
    x1, x2 = xc - geom.FM_BTN_W // 2 - 2, xc + geom.FM_BTN_W // 2 + 2
    y1, y2 = yc - geom.FM_BTN_H // 2 - 2, yc + geom.FM_BTN_H // 2 + 2
    W, H, px = ui.m.fbuf()
    return tuple(px[(y * W + x) * 3] < 0x40
                 for y in range(y1, y2 + 1) for x in range(x1, x2 + 1))


def until(ui, cond, what):
    ui._wait(cond, what, os88ui.T_NAV)


def repaint(ui):
    """A FULL repaint of the chooser, selection kept: V twice (SPEC.md 22)."""
    v0 = fs(ui, geom.FS_VIEW, 1)
    ui.m.key("KeyV")
    until(ui, lambda: fs(ui, geom.FS_VIEW, 1) != v0, "the view to toggle")
    ui.settle()
    ui.m.key("KeyV")
    until(ui, lambda: fs(ui, geom.FS_VIEW, 1) == v0, "the view to come back")
    ui.settle()


def same(name, a, b):
    d = sum(x != y for x, y in zip(a, b))
    check(name, d == 0, "(%d px differ)" % d)


with os88ui.boot(SYS, apps=APPS, machine=MACHINE) as ui:
    m = ui.m
    print("== %s : the chooser's default button, redraw vs repaint (%s) =="
          % (MACHINE, SYS))
    ui.path(NP)
    ui.settle()                 # a menu picked while the app is still
                                # coming up is lost (os88ui's known race)
    ui.menu_pick("File", "Open")
    w = ui.chooser()
    ui.settle()
    rows = ui.listing(w)
    check("the Open form is up", w.title == "Open", "(%r)" % w.title)
    check("nothing is selected", fs(ui, geom.FS_SEL) == 0xFFFF,
          "(FS_SEL %04X)" % fs(ui, geom.FS_SEL))
    grey0 = btn(ui, w)

    # --- greyed -> live, by a click on a row ---------------------------------
    ui.mo.click(*ui.row_xy(w, 0), settle=0)
    until(ui, lambda: fs(ui, geom.FS_SEL) == 0, "row 0 to be selected")
    ui.settle()
    live_redrawn = btn(ui, w)
    repaint(ui)
    live_fresh = btn(ui, w)
    check("the selection survived the repaint", fs(ui, geom.FS_SEL) == 0)
    same("LIVE (click): redrawn == freshly painted", live_redrawn, live_fresh)
    check("LIVE is not the same picture as GREYED", live_fresh != grey0,
          "(ink %d live vs %d greyed)" % (sum(live_fresh), sum(grey0)))
    check("GREYED carries less ink than LIVE (a dithered caption)",
          sum(grey0) < sum(live_fresh),
          "(ink %d vs %d)" % (sum(grey0), sum(live_fresh)))

    # --- live -> live, by Down (kern_big: SPEC.md 22.26 moves a selection) ---
    # FDH_SEL again, and this time the greying does not flip, so 38.8 draws
    # nothing: the button must still be the freshly painted live one
    if not SMALL:
        m.key("ArrowDown")
        until(ui, lambda: fs(ui, geom.FS_SEL) == 1, "Down to select row 1")
        ui.settle()
        down_redrawn = btn(ui, w)
        repaint(ui)
        down_fresh = btn(ui, w)
        same("LIVE (Down): redrawn == freshly painted", down_redrawn,
             down_fresh)
        same("...and is the live picture the click left", down_fresh,
             live_fresh)

    # --- live -> greyed, by a click on empty list ----------------------------
    # The row BELOW the last entry, inside the list (fm_listb is the layout
    # cache, and the chooser is the window just laid out). It must not be
    # within the double-click window of the row click above - it is not a row,
    # so fm_onclick's .clear takes it either way.
    blank = ui.row_xy(w, len(rows))
    listb = w.y + geom.TITLE_H + ui._word("fm_listb")
    check("the folder leaves blank list to click", blank[1] + 6 < listb,
          "(%d rows, blank row at y=%d, list ends y=%d)"
          % (len(rows), blank[1], listb))
    ui.mo.click(*blank, settle=0)
    until(ui, lambda: fs(ui, geom.FS_SEL) == 0xFFFF,
          "the selection to clear")
    ui.settle()
    grey_redrawn = btn(ui, w)
    repaint(ui)
    grey_fresh = btn(ui, w)
    same("GREYED (click): redrawn == freshly painted", grey_redrawn,
         grey_fresh)
    same("GREYED is the picture the chooser OPENED with", grey_fresh, grey0)

    ui.chooser_cancel("escape")

print()
if fails:
    print("FAILURES:")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("all pass")

#!/usr/bin/env python3
"""SPEC.md 22.26: in a Disk window the arrows move a SELECTION (kern_big).

Driven in B:\\APPS, which holds more packages than the window shows rows, so
the view has to FOLLOW:

  1. with nothing selected Down SCROLLS, as it always did;
  2. a click selects row 0, and Down x N walks the selection off the bottom
     of the view - FS_SEL is the row, and FS_SCRL has moved just far enough
     that it is the last visible one;
  3. PgUp brings it back up a page, Up to the top;
  4. after every leg exactly ONE row band is inverted on the glass, and it
     is the selected row's - which is what catches a band left behind,
     because the move takes the old band off itself, parks [fm_lsel] across
     fm_scroll_by and puts the new one on afterwards. (A comparison against
     a full repaint cannot be had here: the view toggle that forces one also
     scrolls back to the top.)

  5. a move SHUTS the double-click window: click row 1, Down, click row 2,
     all inside FM_DBLCLK's 9 ticks, and nothing opens. FS_CLKT was the
     click on row 1, and fm_onclick reads "same index as FS_SEL, within 9
     ticks" as a double-click, so without .selmove's re-stamp the second
     click alone opened row 2. The gesture is STEPPED with the guest paused
     (the shape of os88mouse's dblclick), so its span is the guest's and not
     the host's, and the span is checked rather than assumed.

VERIFIED TO FAIL: taking `call fm_sel_bar ; the old band off` out of
.selmove leaves the old band on screen and reds the picture checks; taking
.selmove's FS_CLKT store out opens row 2 on leg 5's one click.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88geom as geom                                     # noqa: E402
import os88ui                                               # noqa: E402
from os88mouse import DBL_TICKS                             # noqa: E402

fails = []


def check(name, cond, note=""):
    print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, note))
    if not cond:
        fails.append(name)


def word(ui, blk, off):
    return int.from_bytes(ui.m.read(blk + off, 2), "little")


def step_until(ui, cond, what, steps=240):
    """Advance the PAUSED guest ~4 ms at a time until `cond()` holds."""
    for _ in range(steps):
        ui.m.advance(cycles=ui.mo.DBL_STEP)
        if cond():
            return
    raise RuntimeError("never saw %s across %d guest cycles"
                       % (what, steps * ui.mo.DBL_STEP))


def step_to(ui, x, y):
    """os88mouse's `to`, with the guest PAUSED: one packet, then cycles until
    the published pointer moves, until it is exactly at (x, y)."""
    for _ in range(8):
        cx, cy, _b = ui.mo.where()
        if (cx, cy) == (x, y):
            return
        ui.m.mouse(max(-100, min(100, x - cx)), max(-100, min(100, y - cy)))
        step_until(ui, lambda: ui.mo.where()[:2] != (cx, cy),
                   "the pointer move from (%d,%d)" % (cx, cy))
    raise RuntimeError("the pointer never reached (%d,%d)" % (x, y))


def bands(ui, w, fit):
    """Which visible rows are drawn INVERTED: a row whose band is mostly
    dark is a selection band (SPEC.md 22.2's XOR), text alone never is."""
    x0 = w.x + 1 + 2
    x1 = w.x + w.w - 2 - 18                 # clear of the scroll bar
    y0 = w.y + geom.TITLE_H + 1 + geom.FM_ROW_Y0
    W, H, px = ui.m.fbuf()
    out = []
    for r in range(fit):
        dark = tot = 0
        for y in range(y0 + r * geom.FM_ROW_H + 2,
                       y0 + (r + 1) * geom.FM_ROW_H - 2):
            for x in range(x0, x1, 2):
                dark += px[(y * W + x) * 3] < 0x40
                tot += 1
        if dark * 2 > tot:
            out.append(r)
    return out


with os88ui.boot("build/os8088-360.img", apps="build/apps360.img") as ui:
    m = ui.m
    w = ui.path("B:/APPS")
    ui.settle()
    blk = ui._fsblk(w)
    n = word(ui, blk, geom.FS_N)
    print("== SPEC.md 22.26: the arrows move a selection (B:\\APPS, %d "
          "entries) ==" % n)

    # --- 1: nothing selected - Down scrolls ----------------------------------
    m.key("ArrowDown")
    ui.settle()
    check("with nothing selected Down scrolls",
          word(ui, blk, geom.FS_SEL) == 0xFFFF
          and word(ui, blk, geom.FS_SCRL) == 1,
          "(sel %04X, scrl %d)" % (word(ui, blk, geom.FS_SEL),
                                   word(ui, blk, geom.FS_SCRL)))
    m.key("ArrowUp")
    ui.settle()

    # --- 2: a click selects; Down walks it off the bottom, the view follows --
    x, y = ui.row_xy(w, 0)
    ui.mo.click(x, y, settle=0)
    ui.settle()
    check("a click selects row 0", word(ui, blk, geom.FS_SEL) == 0)
    fit = ui._word("fm_fit")
    steps = min(n - 1, fit + 2)
    for _ in range(steps):
        m.key("ArrowDown")
    ui.settle()
    sel = word(ui, blk, geom.FS_SEL)
    scrl = word(ui, blk, geom.FS_SCRL)
    check("Down x %d moves the selection to row %d" % (steps, steps),
          sel == steps, "(%d)" % sel)
    check("...and the view follows it to the last visible row",
          scrl == steps - fit + 1, "(scrl %d, fit %d)" % (scrl, fit))
    b = bands(ui, w, fit)
    check("ONE band on the glass, on the selected row", b == [sel - scrl],
          "(inverted rows %r, want [%d])" % (b, sel - scrl))

    # --- 3: PgUp a page, Up to the top ---------------------------------------
    m.key("PageUp")
    ui.settle()
    check("PgUp moves it a page", word(ui, blk, geom.FS_SEL) == steps - fit,
          "(%d)" % word(ui, blk, geom.FS_SEL))
    for _ in range(steps):
        m.key("ArrowUp")
    ui.settle()
    check("Up stops at the first row", word(ui, blk, geom.FS_SEL) == 0
          and word(ui, blk, geom.FS_SCRL) == 0,
          "(sel %d, scrl %d)" % (word(ui, blk, geom.FS_SEL),
                                 word(ui, blk, geom.FS_SCRL)))
    b = bands(ui, w, fit)
    check("...and ONE band, on row 0", b == [0], "(inverted rows %r)" % b)

    # --- 5: a move shuts the double-click window -----------------------------
    mo = ui.mo
    x1, y1 = ui.row_xy(w, 1)
    x2, y2 = ui.row_xy(w, 2)
    mo.to(x1, y1)
    mo._sep()                           # the first press is a FIRST click
    before = {v.i for v in ui.windows()}
    was = ui.listing(w)
    m.pause()
    try:
        mo._gedge(True)
        t1 = mo.ticks()
        mo._gedge(False)
        step_until(ui, lambda: word(ui, blk, geom.FS_SEL) == 1,
                   "the click select row 1")
        m.key("ArrowDown")
        step_until(ui, lambda: word(ui, blk, geom.FS_SEL) == 2,
                   "Down move the selection to row 2")
        step_to(ui, x2, y2)
        mo._gedge(True)
        t2 = mo.ticks()
        mo._gedge(False)
    finally:
        m.go()
    span = (t2 - t1) & 0xFFFFFFFF
    check("click, Down, click lands inside the double-click window",
          span < DBL_TICKS, "(%d ticks apart, window %d)" % (span, DBL_TICKS))

    def opened():
        return (any(v.i not in before and v.visible for v in ui.windows())
                or ui.listing(w) != was or ui._byte("ld_pending") != 0)
    try:
        ui._wait(opened, "an open", 10.0)
        quiet = False
    except os88ui.UIError:
        quiet = True
    check("...and the second click only SELECTS: nothing opened", quiet,
          "(sel %d, titles %r)" % (word(ui, blk, geom.FS_SEL), ui.titles()))

print()
if fails:
    print("FAILURES:")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("all pass")

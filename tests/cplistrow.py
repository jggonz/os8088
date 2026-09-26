#!/usr/bin/env python3
"""DOES A CONTROL PANEL SELECTION REDRAW WHAT CHANGED, OR BLANK A PANE?
   (SPEC.md 31.1.4, 31.5.3)

    make && python3 tests/cplistrow.py

`cp_list` used to be the redraw path for a selection, and it FILLS THE WHOLE
LEFT PANE WHITE before re-lettering every row - its own comment named that as
the feature. So clicking a category blanked every name on the pane and drew
them all again to move one highlight: SPEC.md 13.14.6 rule 1, in the one
window whose entire job is being clicked.

NEITHER HALF OF THAT SHOWS IN A SCREENSHOT. The final frame is identical
whichever way the pane was drawn, which is PERFORMANCE.md Part 1's "invisible
in an emulator" exactly - a visible redraw and a double-draw flash, two of the
three defects this project keeps paying for. So this reads the CALLS.

EXACTLY TWO ROWS ARE LETTERED. font_run_x is the only way a list name reaches
the screen, and a selection changes two rows - the one the bar left and the one
it arrived at. More than two means cp_list ran, and cp_list's FIRST act is the
pane erase, so the count is the blank: they are the same routine and there is
no way to have one without the other.

  ...and font_run_x ALONE, deliberately. gfx_fill would say it more directly
  and cannot be traced here: every drawing call in the OS goes through it, a
  selection click repaints the whole right-hand page as well, and each stop is
  a round trip - the first version of this row armed it and the machine never
  finished the repaint inside any budget. A breakpoint's cost is the guest's,
  and a hot symbol is not free to watch.

...and it asserts the PICTURE too, because a redraw that draws nothing is also
"two rows and no wide fill": the new row must end up with a black bar under
white text and the old row must not.

THE DATE/TIME PAGE IS THE SAME QUESTION ONE PAGE ALONG (SPEC.md 31.5.3).
Clicking a different field called cp_time_rows(1), whose first act is to fill
the whole date-and-time band white - so moving the caret blanked both rows and
re-lettered all six fields to take one black box off one of them. The count
here is the same tell: six names means the band went, two means it did not.

WHAT IT WOULD CATCH, verified red by reverting the source: the list pane's
erase and its six names, a cp_listrow that skips the bar when a row is NOT
selected (which leaves the old bar standing for ever), and the time page's
band erase and its six fields.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp                    # noqa: E402
from cycweb import shot                                        # noqa: E402

CP_DIVX, CP_IBX1, CP_IBX2 = 88, 2, 85
CP_I0Y, CP_IROWH, CP_IBH, CP_IX = 6, 14, 12, 6
CP_RX, CPT_FX, CPT_DY, CPT_TY = 96, 8, 26, 48       # the Date/Time page
CP_ITIME = 1                                        # cp_items' record 1
TITLE_H = dispcp.TITLE_H
FAIL = []


def check(ok, what):
    print("   %-56s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def barrow(px, W, wx, wy, row, mono):
    """How many pixels of row `row`'s bar band are INK."""
    x0 = wx + 1 + CP_IBX1
    y0 = wy + TITLE_H + 1 + CP_I0Y + row * CP_IROWH
    n = 0
    for y in range(y0, y0 + CP_IBH):
        for x in range(x0, x0 + (CP_IBX2 - CP_IBX1 + 1)):
            lit = px[y * W + x]
            if (not lit) if mono else lit:
                n += 1
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    a = ap.parse_args()
    S = os88sym.linear
    mono = "herc" in a.machine or "cga" in a.machine

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)       # SPEC.md 79 DRAWS, so nothing armed below
                                    # could ever settle - and every bp_trace
                                    # would be watching the saver's fish
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle, page=None)
        os88marty.settle(m)
        wx, wy = dispcp._cp_win(m, S)
        hide = m.read(S("cp_hide"), 1)[0]
        shown = [r for r in range(8) if not (hide & (1 << r))]
        print("   panel at (%d,%d), %d rows shown, [cp_sel] = %d"
              % (wx, wy, len(shown), m.read(S("cp_sel"), 1)[0]))
        check(len(shown) >= 2, "the machine shows at least two categories")
        if len(shown) < 2:
            sys.exit("cplistrow: nothing to move a selection between")

        sel = m.read(S("cp_sel"), 1)[0]
        selrow = shown.index(sel) if sel in shown else 0
        target = 0 if selrow != 0 else 1        # a DIFFERENT row
        mo.to(4, 4)
        os88marty.settle(m)

        # --- the click, with every drawing call recorded ---------------------
        pane = wx + 1 + CP_DIVX
        top = wy + TITLE_H

        def left(tr):
            return sorted({h["regs"]["dx"] for h in tr.hits if h.get("regs")
                           and h["regs"]["cx"] < pane
                           and top <= h["regs"]["dx"] <= top + 1 + CP_I0Y
                           + len(shown) * CP_IROWH})

        with os88marty.bp_trace(m, "font_run_x", regs=True) as tr:
            mo.click(wx + 1 + CP_IX + 30,
                     wy + TITLE_H + 1 + CP_I0Y + target * CP_IROWH
                     + CP_IROWH // 2, settle=0)
            # The click's own proof is not its repaint: stay in the block
            # until the selection has moved and the lettering has stopped,
            # both counted in guest time.
            tr.until(lambda: m.read(S("cp_sel"), 1)[0] == shown[target],
                     "the selection to move", 30, required=False)
            os88marty.quiesce(m, lambda: left(tr), guest=1.0,
                              what="the left pane's lettering to stop")
        mo.to(4, 4)

        # TWO IDENTICAL CAPTURES, NOT settle(). A settle straight out of a
        # bp_trace block does not return here - the trace pumps the guest
        # through its own daemon and the screen-change gate never reports
        # quiet, so the row died in the harness rather than at an assertion.
        # What settle is FOR is a screen that has stopped moving, and two
        # identical reads say that directly.
        W = H = None
        last = None
        for _ in range(8):
            os88marty.pace(m, 0.5)
            W, H, cur = shot(m)
            if cur == last:
                break
            last = cur
        px = last

        # THE LEFT PANE ONLY: cp_page redraws the whole right-hand pane on a
        # selection and is SUPPOSED to - the page really did all change.
        runs = left(tr)
        print("   left-pane names lettered: %d at y %s" % (len(runs), runs))
        check(len(runs) == 2,
              "EXACTLY TWO rows were lettered - the pane was not redrawn, "
              "and so was not erased (%d)" % len(runs))

        # --- and the picture ------------------------------------------------
        newsel = m.read(S("cp_sel"), 1)[0]
        check(newsel == shown[target], "the selection moved (%d -> %d)"
              % (sel, newsel))
        ink_new = barrow(px, W, wx, wy, target, mono)
        ink_old = barrow(px, W, wx, wy, selrow, mono)
        band = (CP_IBX2 - CP_IBX1 + 1) * CP_IBH
        check(ink_new > band * 0.6,
              "the row clicked has the BAR (%d of %d px ink)" % (ink_new, band))
        check(ink_old < band * 0.4,
              "...and the row it left does not (%d of %d px ink)"
              % (ink_old, band))

        # --- 2. THE DATE/TIME PAGE, same question (SPEC.md 31.5.3) -----------
        # A field's caret is a black box round it, and moving it used to erase
        # the whole date-and-time band. Two fields change, so two runs.
        dispcp.open_panel(m, mo, S, os88marty.settle, page=CP_ITIME)
        mo.to(4, 4)
        os88marty.pace(m, 1.0)
        wx, wy = dispcp._cp_win(m, S)
        fld = m.read(S("cp_tsel"), 1)[0]
        tgt = 3 if fld != 3 else 0          # a field on the OTHER row
        fy = (CPT_DY if tgt < 3 else CPT_TY)
        fx = CPT_FX + (0, 32, 56, 0, 24, 48, 72)[tgt]
        # THE TICK RE-LETTERS ALL SEVEN FIELDS EVERY SECOND and is supposed
        # to (SPEC.md 31.5.1), so counting RUNS here counts the clock: the
        # first version of this assertion did, saw six, and was reading the
        # tick rather than the click.
        #
        # THE FILL IS THE DISCRIMINATOR, and 31.5.1 is why: the tick "erases
        # nothing" - it makes no gfx_fill at all. So every fill in the window
        # belongs to the click, a band erase is ~140px wide and a field's box
        # is at most CPT_YRW + 4. gfx_fill is cheap to trace HERE, where
        # cp_page is not called and the panel is otherwise still.
        with os88marty.bp_trace(m, "gfx_fill", regs=True) as tr:
            mo.click(wx + 1 + CP_RX + fx + 4,
                     wy + TITLE_H + 1 + fy + 4, settle=0)
            tr.until(lambda: m.read(S("cp_tsel"), 1)[0] == tgt,
                     "the caret to move", 30, required=False)
            os88marty.quiesce(m, lambda: tr.n, guest=1.0,
                              what="the field boxes' fills to stop")
        mo.to(4, 4)
        newfld = m.read(S("cp_tsel"), 1)[0]
        pane = wx + 1 + CP_RX
        fills = sorted({h["regs"]["cx"] - h["regs"]["ax"] + 1 for h in tr.hits
                        if h.get("regs") and h["regs"]["ax"] >= pane})
        print("   time page: [cp_tsel] %d -> %d, fill widths %s"
              % (fld, newfld, fills))
        check(newfld == tgt, "the caret moved to the field clicked")
        check(len(fills) <= 2 and (not fills or max(fills) <= 40),
              "only the two FIELD BOXES were filled - the band was not erased "
              "(%s)" % (fills if fills else "no fill at all"))

    print("cplistrow: %s" % ("FAILED - " + "; ".join(FAIL) if FAIL else "ok"))
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()

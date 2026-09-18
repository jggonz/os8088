#!/usr/bin/env python3
"""EVERY ENVIRONMENT ROW IS ON THE SETUP PAGE, AND REACHABLE (SPEC.md 96.32.2.1).

    python3 tests/dosenvfold.py [--machine os8088_5150_cga_gla]

The four `NAME=VALUE` rows had a page of their own while the window was 288 px
wide.  At 80 columns they fit under the first one in Setup's left column, so the
second page went - and with it the `<` and `>` that cycled to it.  What can go
wrong with that is entirely GEOMETRY, and it goes wrong QUIETLY: a row placed
past the bottom of the content box is simply not drawn, and a row the hit test
does not reach is a box the user types into and nothing reads.

So the row asserts the four things a fold can break, on the TIGHTEST adapter:

  1  all `DOS_ENVN` rows are PLACED - a non-empty rect each, which is what
     `dos_place` owes before any hit test;
  2  they are in ORDER and do not overlap, at `DOS_EROWH` pitch;
  3  the last one's bottom clears `dos_paint_furn`'s button row.  That is the
     assertion the fold is really about: the measurement said CGA has 96 px
     free below the first field and three more rows need 48, and this is what
     says so on a machine rather than in a comment;
  4  ...and a CLICK on the last row puts the caret in it and a keystroke lands
     in ITS buffer.  Placement without a hit test is the silent half.

CGA because it is the smallest content box of the three (638x197 against
718x257 and 638x257), so a row that fits here fits everywhere.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88geom as G                                           # noqa: E402
import os88marty as M                                          # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402



def fail(msg):
    print("dosenvfold: FAIL: %s" % msg)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()

    with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                     machine=a.machine) as ui:
        m = ui.m
        if not ui.path("A:/APPS/DOS.O88"):
            fail("double-clicking DOS.O88 opened no window")
        # **SETTLE BEFORE READING A RECT** (docs/WRITING-TESTS.md 60): `path`
        # confirms on the window RECORD, and the bar's buttons are placed by
        # the paint that follows it - so the click below would go to a rect of
        # zeros on a loaded box.
        M.settle(m)
        dm = dosmap.package()
        ps = dosmap.instance(m)
        mo = os88mouse.Mouse(marty=m)

        mo.click(*dosmap.centre(m, ps, dm, "dos_erect"))
        M.settle(m)
        page = m.read((ps << 4) + dm["dos_page"], 1)[0]
        if page != dm["DOS_PAGE_SET"]:
            fail("the bar's button left [dos_page] = %d and Setup is %d - "
                 "there is one setup page now and that button opens it"
                 % (page, dm["DOS_PAGE_SET"]))

        w = [x for x in G.windows(m) if x.title.startswith("DOS")]
        if not w:
            fail("no DOS window in the table")
        # **READ THE BUTTON ROW, DO NOT DERIVE IT.** `dos_furn_rects` takes
        # the content height from OSAPI_WM_GEOM, which on a CLAMPED window is
        # not the height the window table's content field reports: on CGA the
        # slot answers 160 where the table says 197, so `ch - 18` computes
        # 179 for a row that really starts at 142 and the assertion below is
        # 37 pixels looser than it reads. `dos_trect` is where the buttons
        # actually are.
        furn = dosmap.rect(m, ps, dm, "dos_trect")[1]

        def rect(n):
            b = dm["dos_eln"] + n * dm["DOS_LNSZ"]
            return tuple(int.from_bytes(
                m.read((ps << 4) + b + dm["LN_X1"] + 2 * i, 2), "little")
                for i in range(4))

        rs = [rect(n) for n in range(dm["DOS_ENVN"])]
        for n, r in enumerate(rs):
            print("dosenvfold: row %d  x %d..%d  y %d..%d" % (n, r[0], r[2],
                                                              r[1], r[3]))
        # --- 1. every row placed --------------------------------------------
        for n, r in enumerate(rs):
            if r[2] <= r[0] or r[3] <= r[1]:
                fail("row %d has an empty rect %r - dos_place owes every row a "
                     "rect before any hit test (SPEC.md 96.32.2.1)" % (n, r))
        # --- 2. in order, at the declared pitch, not overlapping -------------
        for n in range(1, len(rs)):
            if rs[n][1] <= rs[n - 1][3]:
                fail("row %d starts at y=%d and row %d ends at y=%d - the rows "
                     "overlap" % (n, rs[n][1], n - 1, rs[n - 1][3]))
            step = rs[n][1] - rs[n - 1][1]
            if step != dm["DOS_EROWH"]:
                fail("row %d is %d px below row %d and DOS_EROWH is %d"
                     % (n, step, n - 1, dm["DOS_EROWH"]))
        # --- 3. ...and the last one clears the button row --------------------
        # THE WHOLE POINT OF THE FOLD, asserted in SCREEN coordinates against
        # the window's own content box: a row past this is a row the user
        # cannot see and nothing reports.
        if rs[-1][3] >= furn:
            fail("row %d's last line is at y=%d and the button row starts at "
                 "%d - the last environment row is under Save Shortcut "
                 "(SPEC.md 96.32.2.1)" % (len(rs) - 1, rs[-1][3], furn))
        print("dosenvfold: %d rows, the last ends %d px above the buttons"
              % (len(rs), furn - rs[-1][3]))

        # --- 4. and the hit test reaches the LAST one ------------------------
        last = dm["dos_eln"] + (dm["DOS_ENVN"] - 1) * dm["DOS_LNSZ"]
        mo.click(rs[-1][0] + 20, rs[-1][1] + 6)
        if not m.read((ps << 4) + last + dm["LN_FOCUS"], 1)[0]:
            fail("a click inside row %d's own rect did not give it the caret - "
                 "dos_fld_hit walks every row now that dos_click_env is gone"
                 % (dm["DOS_ENVN"] - 1))
        m.type_text("Z=9")
        M.settle(m)
        buf = int.from_bytes(m.read((ps << 4) + last + dm["LN_BUF"], 2),
                             "little")
        got = bytes(m.read((ps << 4) + buf, 16)).split(b"\0")[0]
        if got != b"Z=9":
            fail("typing into row %d put %r in its buffer - the click focused "
                 "one row and the keystroke went to another"
                 % (dm["DOS_ENVN"] - 1, got))
        print("dosenvfold: row %d took the caret and %r"
              % (dm["DOS_ENVN"] - 1, got))

    print("dosenvfold: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

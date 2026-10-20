#!/usr/bin/env python3
"""A LINE OF TEXT A WINDOW EDGE CUTS IS NOT DRAWN OVER THAT WINDOW (SPEC.md
11.3.4.2).

    make && python3 tests/runclip.py [--machine os8088_5150_cga_gla]

The Task Manager re-letters its rows once a second, and a Disk window is
dragged over it so its LEFT EDGE crosses those rows. `font_run` on a 1bpp
adapter draws an aligned run the region cuts cell by cell through
`font_run_cell`, and the cell under the covering window's edge is the one
11.3.4 changed the meaning of: `wm_clip_rows` answers it (the rows are
visible) and hands its COLUMNS back as a mask. A cell that stores its whole
byte draws the columns the Disk window covers - its left border and up to six
pixels of its content - once a second, for ever.

So the assertion is about the COVERING window: its left border, beside the
Task Manager's rows, is solid ink after three seconds of the Task Manager's
repaints. (Not "unchanged": the damage is re-done every second, so a
snapshot after the move already carries it.) And the negative control is that `font_run_cell`
was actually handed the cell the edge cuts, counted at a breakpoint on its
entry - a layout that sends the cut cell anywhere else (a run whose cells all
sit on one side, a Task Manager that stopped repainting) passes the first leg
on any kernel, and did, the first time this row was written.

Broken on purpose - font_run_cell's `.merge` taken out, the store left whole -
the Disk window's border and edge are drawn on and it FAILS; on the kernel
before 11.3.4.2 it FAILS the same way.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88ui                                               # noqa: E402
import os88marty                                            # noqa: E402

TITLE_H = 18


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--kernel", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args()
    bad = []
    with os88ui.boot(a.kernel, apps=a.apps,
                     machine=os88marty.machine(a.machine)) as ui:
        m = ui.m
        dk = ui.open_drive("B")         # first: on CGA the Task Manager is
        tm = ui.path("A:/SYSTEM/TASKMGR.O88")   # two columns wide and would
        w = ui._as_win(tm)              # cover B's desktop zone
        # The Disk window's left edge through the CPU column - the run each
        # process row re-letters once a second, '  81%' at 129 px into the
        # frame, five cells - with its top below the Task Manager's title so
        # the edge crosses every process row. (Measured: the other columns are
        # lettered once, and a row that cuts one of them is never repainted.)
        # A frame lands on x & 7 = 7 (SPEC.md 11.94), so the cut cell is the
        # one whose last column is the Disk window's BORDER - solid ink from
        # top to bottom, and therefore the assertion.
        ui.move_window(dk, w.x + 136, w.y + TITLE_H + 24)
        dk = ui._as_win(dk.i)
        w = ui._as_win(w.i)
        y1, y2 = dk.y, min(dk.y + dk.h, w.y + w.h) - 1
        cut = dk.x & ~7                 # the cell the edge runs through

        # NOT a comparison over time: the Task Manager re-letters the cut cell
        # every second, so a snapshot taken after the move already carries
        # its damage and "unchanged" passes on the broken kernel - which the
        # first version of this row did. The border's own value is the truth.
        changed = 0
        for _ in range(3):
            os88marty.guest_sleep(m, 1.0)
            _, _, rows = m.vram()
            changed = max(changed, sum(1 for y in range(y1, y2 + 1)
                                       if rows[y][dk.x]))
        # THE NEGATIVE CONTROL: font_run_cell was handed the cut cell - on
        # this adapter, at this edge - in that time, so the leg above was
        # asked of the path it is about. Counted at the routine's ENTRY, which
        # both the kernel before 11.3.4.2 and after it have.
        m.bp_exec("font_run_cell")
        seen, stops = 0, 0
        while stops < 600 and seen < 4:
            m.run()
            if not m.wait_stop(3.0):
                break
            stops += 1
            r = m.regs()
            if r["cx"] == cut and y1 <= r["dx"] + 7 and r["dx"] <= y2:
                seen += 1
        m.bp_exec()
        m.run()
        print("   Disk window at x=%d (x & 7 = %d) over the Task Manager: %d "
              "of the %d border pixels beside its rows are not ink; "
              "font_run_cell handed the cut cell at x=%d %d time(s) in %d "
              "entries" % (dk.x, dk.x & 7, changed, y2 - y1 + 1, cut, seen,
                           stops))
        if changed:
            bad.append("the Task Manager drew over %d pixels of the Disk "
                       "window's border (SPEC.md 11.3.4.2)" % changed)
        if not seen:
            bad.append("font_run_cell was never handed the cell the edge "
                       "cuts, so the row proved nothing")
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

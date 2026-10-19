#!/usr/bin/env python3
"""THE TASK MANAGER'S SECOND COLUMN CARRIES ROWS (SPEC.md 28.1.1, 28.1.2).

    make && python3 tests/tmcol2.py

CGA is the only shipped adapter that takes SPEC.md 28.1.1's two-column path -
155 usable pixels do not hold a useful list in one column, so the window
doubles in width and the process list wraps into a second column beside the
first. That column shipped EMPTY, from the day the mode landed, and this is
the row that would have said so.

**Why nothing caught it.** Every visible piece of the second column was
correct: the frame was twice as wide, the column-1 header was on the glass at
`TM_C2_HDR_Y`, and column 0 drew its rows. The header loop counts `[tm_cols]`
and never asks `tm_row_place` anything, so a screenshot showed a properly
composed two-column window with one of the columns blank - which reads as a
drawing fault in the rows rather than as a layout fault in the depth, and
reads like nothing at all in a diff.

The defect was one word (SPEC.md 28.1.2): the process list took column 0's
depth from `[tm_colrows]`, which is cut from the MEMORY view's `TMM_ROW_Y`.
The process list starts 30px lower, at `TM_ROW_Y`, so that depth named ~2.7
rows the frame has no height for. `tm_row_place` placed them in column 0 and
then refused them on `[tm_ylim]` - and that refusal is the one the
column-major order promises is MONOTONE, so `tm_rows` stopped there. It
stopped INSIDE column 0, and everything after it, the whole of column 1
included, was never reached.

So the assertion is about ROWS ON THE GLASS and deliberately not about the
window's shape:

  COL0   column 0 carries a row of text in every band `[tm_pcolrows]` deep.
  COL1   ...and column 1 in every band `[tm_col2rows]` deep - the leg that
         read [0, 0, 0, 0, 0, 0, 0, 0, 0, 0].
  TAIL   ...and the band after the last one is BLANK, which is the negative
         control: a test that only counts ink upwards passes just as well on
         a routine that has stopped clipping at all, and that routine letters
         the list over the dock strip once a second (SPEC.md 28.1.1).
  LIST   the two columns together show the WHOLE list, TM_ROWS = 13 rows.
  LAUNCH the first row of column 1 is a real text row and not a free slot -
         which is the user-visible bug as it was reported: open a package
         once column 0 is full and it does not appear anywhere.

LAUNCH is the leg that needs the desktop set up, and the set-up IS the
reported repro: a Disk window, a package, a second Disk window, then the Task
Manager. That fills column 0 (IVT+Pad, System and the folder windows, SPEC.md
28.4.7) and pushes MINES and TaskMgr into column 1, so column 1's first band
is a live row rather than one of the `-` free slots that follow it.

**A free slot is not nothing**, and that is why LAUNCH measures ink rather
than presence: a `-` row is one scanline of about 36 pixels where a real row
is seven scanlines of about 300. Counting "is there ink" would pass on a
column of nothing but free slots, which is what the fix's first cut produced.

THE GEOMETRY IS READ OUT OF THE SOURCE, never restated here. Nine constants
decide where a row lands - seven of `apps/taskmgr/taskmgr.asm`'s and two of
the SDK's - and a test that hard-codes them is a test that goes quietly wrong
the day one of them moves.

`vram` and not `fbuf`: this is a CGA, so the framebuffer is flat in guest
memory and the 1bpp read is exact and cheap. And `guest_sleep` and not
`settle`, for tmground.py's reason - the performance view animates, so this
window never stills.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tests", "unit"))
import os88ui                                               # noqa: E402
import os88marty                                            # noqa: E402
from harness import check, done                             # noqa: E402

KERNEL = "build/os8088-360.img"
APPS = "build/apps360.img"
# The ONLY adapter that runs two columns. Resolved through os88marty.machine
# so it lands on the GLaBIOS twin: the period IBM ROM is not in this tree, and
# a row whose machine depends on which files happen to be lying about is a row
# whose result cannot be compared with anybody else's (tools/os88marty.py).
MACHINE = os88marty.machine("os8088_5150_cga")

# A real text row against a `-` free slot: measured 36 subpixels for a free
# slot and 300+ for a row of text, so anything between them separates the two.
REAL_INK = 100


def consts():
    """TM_* geometry, read out of the package's own source (see the header)."""
    # The package's own file first, the SDK behind it: TM_ROWS is written in
    # terms of INST_MAX and the row geometry in terms of TITLE_H, and both of
    # those are `apps/os88api.inc`'s.
    src = "\n".join(open(os.path.join(ROOT, *p)).read() for p in
                    (("apps", "taskmgr", "taskmgr.asm"),
                     ("apps", "os88api.inc")))
    want = ("TM_ROW_Y", "TM_ROW_H", "TM_RW", "TM_COLW", "TM_C2_ROW_Y",
            "TM_C2_HDR_Y", "TM_ROWS", "TM_PROWS", "INST_MAX", "TITLE_H")
    out = {}
    for name in want:
        m = re.search(r"^%s\s+equ\s+([^;\n]+)" % name, src, re.M)
        if not m:
            raise SystemExit("tmcol2: %s is no longer an `equ` this row can "
                             "read - see the header on why it is read rather "
                             "than restated" % name)
        out[name] = m.group(1).strip()
    ev = {}
    for _ in range(len(want)):               # resolve the few that reference
        for k, v in out.items():             # each other, in any order
            try:
                ev[k] = int(eval(v, {}, dict(ev)))
            except Exception:
                pass
    missing = [k for k in want if k not in ev]
    if missing:
        raise SystemExit("tmcol2: could not evaluate %s" % ", ".join(missing))
    return ev


def ink(rows, x1, x2, y1, y2):
    """Black subpixels in a band. `vram` rows are 1 = lit = WHITE."""
    return sum(1 for y in range(y1, y2 + 1) for x in range(x1, x2 + 1)
               if not rows[y][x])


def main():
    C = consts()
    pitch, rw, colw = C["TM_ROW_H"], C["TM_RW"], C["TM_COLW"]

    with os88ui.boot(KERNEL, apps=APPS, machine=MACHINE) as ui:
        m = ui.m
        # THE REPORTED REPRO, in the order the harness can drive it: a Disk
        # window, a package, a second Disk window, then the Task Manager.
        ui.open_drive("B")
        ui.open("GAMES")
        ui.open("MINES.O88")
        tm = ui.path("A:/SYSTEM/TASKMGR.O88")
        ui.raise_window(tm)
        os88marty.guest_sleep(m, 3.0)

        w = ui._as_win(tm)
        _, _, rows = m.vram()

    # The content origin, and the two columns' bands within it.
    cx, cy = w.x + 1, w.y + C["TITLE_H"]
    right = min(cx + colw + rw, len(rows[0])) - 1

    # IS THERE A SECOND COLUMN AT ALL? Asked of the column-1 HEADER and not of
    # the frame's width: `wm_snap_w` rounds a published frame so its CONTENT is
    # a whole number of framebuffer bytes (SPEC.md 11.94.5), so the 464 this
    # window asks for arrives as 458 and a width test reads NO on a window that
    # plainly has two. The header is drawn once per `[tm_cols]`, which is the
    # fact this row needs to be true before any leg below means anything.
    two = ink(rows, cx + colw, right,
              cy + C["TM_C2_HDR_Y"], cy + C["TM_C2_HDR_Y"] + 7) > 0
    check(two, "the window is in TWO-COLUMN mode",
          "every leg below is vacuous on a one-column window, and a "
          "layout change that quietly dropped the second column would "
          "otherwise pass this row green (SPEC.md 28.1.1)",
          got="column-1 header ink at x=%d" % (cx + colw), want="> 0")
    if two:
        # How deep is each column? Column 0 is the process list's own
        # (TM_ROW_Y), every later one is top-anchored at TM_C2_ROW_Y, and both
        # are cut from the FRAME - which is what SPEC.md 28.1.2 is about.
        pcolrows = (w.h - (C["TITLE_H"] + 1 + C["TM_ROW_Y"])) // pitch
        col2rows = (w.h - (C["TITLE_H"] + 1 + C["TM_C2_ROW_Y"])) // pitch
        # A screen too short for every row folds IVT+Pad into System
        # (SPEC.md 28.4.7), so the list it draws is one row shorter.
        whole = C["TM_PROWS"]
        if pcolrows + col2rows < whole:
            whole -= 1
        col2rows = min(col2rows, whole - pcolrows)

        def bands(col, first, n):
            """Ink per row band of a column, `n` bands from its first row."""
            x1 = cx + col * colw
            return [ink(rows, x1, min(x1 + rw - 1, right),
                        cy + first + k * pitch, cy + first + k * pitch + 7)
                    for k in range(n)]

        c0 = bands(0, C["TM_ROW_Y"], pcolrows)
        c1 = bands(1, C["TM_C2_ROW_Y"], col2rows + 1)   # +1 = the TAIL band

        check(all(v > 0 for v in c0), "COL0: every row band carries text",
              "column 0 is the half that always worked; if it is empty the "
              "reading below is about a window that did not paint at all",
              got=c0, want="all > 0")

        check(all(v > 0 for v in c1[:col2rows]),
              "COL1: every row band of the SECOND column carries text",
              "THE LEG THAT WAS ZERO. The process list shared the memory "
              "view's column-0 depth, so tm_rows hit tm_ylim inside column 0 "
              "and stopped - column 1 kept its header and got no rows at all "
              "(SPEC.md 28.1.2)",
              got=c1[:col2rows], want="all > 0")

        check(c1[col2rows] == 0, "TAIL: the band after the last row is BLANK",
              "the negative control: unbounded, the tail of the list is "
              "lettered over the dock strip once a second (SPEC.md 28.1.1), "
              "and counting ink upwards alone cannot tell that from a fix",
              got=c1[col2rows], want=0)

        # COUNTED OFF THE GLASS, not off the geometry. The first cut of this
        # leg summed the two DEPTHS - which are what the layout intends, so it
        # passed on the broken build with the second column visibly empty.
        drawn = sum(1 for v in c0 + c1[:col2rows] if v > 0)
        check(drawn == whole,
              "LIST: both columns together show the WHOLE process list",
              "TM_PROWS is IVT+Pad, System and one row per instance slot, "
              "less IVT+Pad where the screen is short (SPEC.md 28.4.7), and a short "
              "screen is exactly where the list has to wrap rather than be "
              "truncated - it drew 3 of 13 before SPEC.md 28.1.2",
              got=drawn, want=whole)

        check(c1[0] >= REAL_INK,
              "LAUNCH: column 1's first row is a package, not a free slot",
              "the bug as it was reported - open an app once column 0 is "
              "full and it appears nowhere. A column of `-` free slots would "
              "pass COL1 and fail here",
              got=c1[0], want=">= %d" % REAL_INK)

    return done("tmcol2")


if __name__ == "__main__":
    sys.exit(main())

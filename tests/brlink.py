#!/usr/bin/env python3
"""Two links with a space between them are TWO underlines (BROWSER-PLAN 2.6).

    make browsertest && python3 tests/brlink.py [--adapter cga|herc]

**The bug this exists for renders perfectly and reads wrong.** Whitespace in
the parser collapses to a PENDING space spent at the next ink character, and
for `</a> <a>` that character arrives *after* `D_LNK1` - so the space lands
inside the second anchor's span, `br_lmark` says 1 for that cell, and
`br_underline` (which rules one run of marked cells in one fill) draws
straight through the gap. Two links then read as one with an underscore
between them, which is exactly how the field reported it on the proxy's
`231 comments  r/vintagecomputing` line.

Nothing about the TEXT is wrong, so a text comparison - which is what
tests/brtest.py does against tools/htmsim.py - passes on the broken build.
This asserts the PIXELS under it:

  1. the two-link line has TWO underline runs with unlit pixels between them;
  2. the one-link control line has ONE, so a change that simply stopped
     underlining would fail here rather than pass;
  3. both runs are as wide as their words, so an underline that shrank to fit
     the fix would fail too.
"""
import argparse
import sys

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
from brtest import MACHINE, frame                      # noqa: E402

S = os88sym.linear
CELL = 8                       # the ROM font's cell, and the underline's unit


def runs_on(fb, stride, y, x0, x1):
    """The dark runs on one framebuffer row, as (start, length)."""
    out, run = [], 0
    for x in range(x0, x1):
        if not fb[y * stride + x]:
            run += 1
        elif run:
            out.append((x - run, run))
            run = 0
    if run:
        out.append((x1 - run, run))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    ap.add_argument("--shot", default=None)
    args = ap.parse_args()
    fails = []
    with os88marty.launch("build/os8088-360.img", apps="build/brtest360.img",
                          machine=MACHINE[args.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "LINKS.HTM")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("brlink: LINKS.HTM did not open a browser window")
        bx, by, bw, bh = dispcp.win_rect(m, S, wins2[-1])
        w, h, fb = frame(m, args.adapter)
        stride = len(fb) // h
        if args.shot:
            sw, sh, srows = (m.vram("herc") if args.adapter == "herc"
                             else m.vram("cga"))
            os88marty.write_png(args.shot, sw, sh, srows)

        x0, x1 = bx + 1, min(bx + bw - 1, stride)
        y0, y1 = by + dispcp.TITLE_H + 1, min(by + bh - 1, h)
        # An UNDERLINE is a run of dark pixels at least a word long on a row
        # whose neighbours above are mostly light: glyph rows are broken up by
        # the gaps inside letters, so a 24-pixel unbroken run is the rule and
        # not the text. Collect every row that has one.
        # ...and a run WIDER than the text band is the window's own chrome -
        # a rule, a border, the status line - not an underline. Without this
        # bound the control check below was satisfied by a 488-pixel line the
        # browser draws across the whole content, which is a check that cannot
        # fail for the reason it claims.
        wide = (x1 - x0) - 4 * CELL
        rows = []
        for y in range(y0, y1):
            rr = [r for r in runs_on(fb, stride, y, x0, x1)
                  if 3 * CELL <= r[1] <= wide]
            if rr:
                rows.append((y, rr))
        print("underline rows: %s"
              % [(y, [r[1] for r in rr]) for y, rr in rows])
        two = [rr for _, rr in rows if len(rr) == 2]
        one = [rr for _, rr in rows if len(rr) == 1]
        if not two:
            fails.append("no row carries TWO underline runs: the space between "
                         "the links is inside one of them, so `AAAA BBBB` is "
                         "underlined as a single link")
        else:
            a, b = two[0]
            gap = b[0] - (a[0] + a[1])
            print("two-link row: runs %s and %s, %d unlit pixels between"
                  % (a, b, gap))
            if gap < 1:
                fails.append("the two underlines touch")
            for r in (a, b):
                if not (3 * CELL <= r[1] <= 5 * CELL):
                    fails.append("an underline is %dpx for a 4-character word"
                                 % r[1])
        if not one:
            fails.append("the control line has no single-run underline - the "
                         "test cannot tell a fix from a browser that stopped "
                         "underlining")
        else:
            ctl = max(r[1] for rr in one for r in rr)
            print("control row: widest run %dpx (CCCCCCCC is 8 cells)" % ctl)
            if not (7 * CELL <= ctl <= 9 * CELL):
                fails.append("the control underline is %dpx, not ~64" % ctl)
    for f in fails:
        print("FAIL: %s" % f)
    print("brlink: %s" % ("all good" if not fails else "%d FAILURE(S)"
                          % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

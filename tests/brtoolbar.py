#!/usr/bin/env python3
"""The toolbar's buttons keep their right edge (BROWSER-PLAN 14.4).

    make && make browsertest && python3 tests/brtoolbar.py [--adapter cga|herc]

Reported as *"the right side of the Reload button does not seem to fully
draw"*, and it is an OVER-DRAW rather than a missing draw: `br_toolbar` frames
all three buttons correctly and then `br_status` paints the state field over
the last one's right stroke.

`br_srect` right-aligns that field and FLOORS its pen to a multiple of 8 for
SPEC.md 6.1's single-store path. A floor moves the pen LEFT - by up to 7px,
which is further than `BR_BTNG`'s 3px gap is wide - so the pen landed on
Reload's own right-edge column, and the field is one OPAQUE `font_run` padded
in FRONT with spaces (the text is right-aligned inside it too), so those
spaces drew white ground over the frame.

**It is not adapter-specific**, though it was reported on Hercules: the
window's `br_cx`/`br_cw` are the same on both 1bpp adapters and there is no
adapter term in the arithmetic at all - Reload's right edge is x=184 and the
floored pen came back 184 on each.

Three assertions, and the second and third are what stop a fix that cheats:

1. THE PEN NEVER STARTS BEFORE THE BOUND IT WAS GIVEN. The invariant
   `br_spen >= br_r3.x2 + BR_BTNG + 1`, read from the app's own geometry.
2. ALL THREE BUTTONS HAVE AN UNBROKEN RIGHT COLUMN, measured on the glass
   after the state has been drawn. Back and Fwd are the control: a change that
   stopped framing buttons would pass assertion 1 and fail here.
3. THE STATE IS STILL DRAWN. `br_swid` cells, with ink in them - so setting
   the field's width to zero fails too.
"""
import argparse
import sys

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
from brclick import browser_syms, MACHINE              # noqa: E402

S = os88sym.linear
u16 = lambda b: b[0] | (b[1] << 8)                     # noqa: E731
BR_BTNG = 3                                            # apps/browser/browser.asm


def say(*a):
    print(*a)
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="herc", choices=sorted(MACHINE))
    ap.add_argument("--shot", default=None)
    a = ap.parse_args()

    sy, fails = browser_syms(), []
    with os88marty.launch("build/os8088-360.img", apps="build/brtest360.img",
                          machine=MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        before = dispcp.win_list(m, S)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "DEMO.HTM")
        wins = dispcp.win_list(m, S)
        if len(wins) <= len(before):
            sys.exit("brtoolbar: DEMO.HTM opened no window")
        rec = m.read(S("wm_wins") + wins[-1] * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        rw = lambda n: u16(m.readseg(pseg, sy[n], 2))   # noqa: E731

        def rect(n):
            return [u16(m.readseg(pseg, sy[n] + i * 2, 2)) for i in range(4)]

        r1, r2, r3 = rect("br_r1"), rect("br_r2"), rect("br_r3")
        spen, swid = rw("br_spen"), rw("br_swid")
        say("Back   x %d..%d  y %d..%d" % (r1[0], r1[2], r1[1], r1[3]))
        say("Fwd    x %d..%d" % (r2[0], r2[2]))
        say("Reload x %d..%d" % (r3[0], r3[2]))
        say("state  pen %d, %d cells (first x it may use: %d)"
            % (spen, swid, r3[2] + BR_BTNG + 1))

        # --- 1: the pen is inside the field it was given -----------------
        floor_x = r3[2] + BR_BTNG + 1
        if spen < floor_x:
            fails.append("br_spen is %d and the first x the state may use is "
                         "%d - the field starts %d px INSIDE the Reload "
                         "button and its opaque ground erases the frame"
                         % (spen, floor_x, floor_x - spen))
        if spen & 7 != rw("br_cx") & 7:
            fails.append("br_spen %d is not 8-aligned relative to br_cx %d - "
                         "SPEC.md 6.1's single-store path is lost and the "
                         "field blanks on every state change"
                         % (spen, rw("br_cx")))

        w, h, rows = (m.vram("herc") if a.adapter == "herc"
                      else m.vram("cga"))
        fb = [bytes(r) for r in rows]
        if a.shot:
            os88marty.write_png(a.shot, w, h, rows)

        # --- 2: every button's right column is unbroken ------------------
        for name, r in (("Back", r1), ("Fwd", r2), ("Reload", r3)):
            x = r[2]
            lit = [y for y in range(r[1], r[3] + 1) if fb[y][x]]
            if lit:
                fails.append("%s's right edge (x=%d) has %d unlit row(s) of "
                             "%d - rows %r are background where the frame "
                             "should be"
                             % (name, x, len(lit), r[3] - r[1] + 1, lit))
            else:
                say("%-6s right edge x=%d: all %d rows drawn"
                    % (name, x, r[3] - r[1] + 1))

        # --- 3: ...and the state is still there --------------------------
        if swid == 0:
            fails.append("br_swid is 0 - the state field was removed rather "
                         "than moved, so nothing says whether a fetch is "
                         "running")
        else:
            ink = sum(1 for y in range(r3[1], r3[3] + 1)
                      for x in range(spen, min(spen + swid * 8, w))
                      if not fb[y][x])
            say("state field has %d ink pixels" % ink)
            if ink < 20:
                fails.append("the state field has %d ink pixels - it is not "
                             "being drawn at all" % ink)

    for f in fails:
        print("FAIL:", f)
    print("brtoolbar: %d assertion(s) failed" % len(fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()

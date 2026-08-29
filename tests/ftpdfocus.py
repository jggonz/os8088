#!/usr/bin/env python3
"""Does FTPD's Setup page keep a caret it cannot type into? (SPEC.md 77.45.4)

    make && python3 tests/ftpdfocus.py [--machine os8088_5150_herc_gla]

`fd_setup_toggle` cleared `[fd_pfoc]` on the way out of the page and left
`LN_FOCUS` set on the field that had it. `fd_line_init` does not clear it
either - `os88line_set` resets the length, the caret and the view and says
nothing about focus - so the NEXT visit drew a caret in a field
`fd_setup_key` would not type into, because that routine reads `[fd_pfoc]`.

The assertion is a pixel one and it is exact: the page on a SECOND visit,
with nothing clicked, must be the page on the first visit with nothing
clicked. A stale caret is 8 black pixels, and the first check measures that
same 8 on the way in so the number is the page's and not this file's.

It also types with nothing focused and requires that to change nothing, which
is the other half of the same defect and the half a screenshot cannot show.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402
import os88geom                                         # noqa: E402
import dispcp                                           # noqa: E402

S = os88sym.linear
MACHINE = "os8088_5150_cga_gla"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

FD_PAD, FD_BTNH, FD_BTNW = 4, 14, 44
FD_SETY, FD_SROW, FD_FLDX = 22, 16, 112

fails = []


def check(name, cond, note=""):
    print("  %-4s %s%s" % ("ok" if cond else "FAIL", name,
                           "   " + note if note else ""))
    if not cond:
        fails.append(name)


# THE WHOLE WINDOW and not a band around one field: MartyPC's framebuffer is
# the CARD's, and on Hercules that is 720x350 against a 720x348 screen - so a
# band computed from guest coordinates lands beside the caret there, where a
# crop of the window contains it on every adapter. The routine is
# tools/os88marty.py's, shared with tests/ftpdflick.py and tests/gfxlk.py.
crop = os88marty.crop_rgb


def ndiff(a, b):
    return sum(1 for i in range(0, min(len(a), len(b)), 3)
               if a[i:i + 3] != b[i:i + 3])


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    wx, wy, ww, wh = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    wx, wy, ww, wh = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "FTPD.O88")
    time.sleep(2)
    os88marty.settle(m)
    wx, wy, ww, wh = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
    ox, oy, cw = wx + 1, wy + os88geom.TITLE_H, ww - 2
    print("FTPD at (%d,%d) %dx%d on %s\n" % (wx, wy, ww, wh, MACHINE))

    setb = (ox + cw - FD_PAD - 1 - FD_BTNW // 2, oy + FD_PAD + FD_BTNH // 2)
    done = (ox + FD_PAD + FD_BTNW // 2, oy + FD_PAD + FD_BTNH // 2)
    park = (wx + ww + 20, wy + 4)

    def enter():
        mo.click(*setb)
        time.sleep(1.5)
        mo.to(*park)
        os88marty.settle(m)

    def leave():
        mo.click(*done)                 # Done is what COMMITS (SPEC.md 77.17)
        time.sleep(1.5)
        mo.to(*park)
        os88marty.settle(m)

    # FIELD 0 takes the caret and NOTHING IS TYPED into it, so the page on a
    # second visit is the page on the first unless a caret was left behind.
    enter()
    first = crop(m, wx, wy, ww, wh)

    mo.click(ox + FD_FLDX + 40, oy + FD_SETY + 7)        # into PASV addr
    time.sleep(1.0)
    mo.to(*park)
    os88marty.settle(m)
    n = ndiff(first, crop(m, wx, wy, ww, wh))
    check("the click puts a caret in field 0, and it is 8 px",
          n == 8, "(%d px)" % n)

    leave()
    enter()

    n = ndiff(first, crop(m, wx, wy, ww, wh))
    check("no caret is left on the second visit",
          n == 0, "(%d px differ - a stale caret is 8)" % n)

    before = crop(m, wx, wy, ww, wh)
    m.type_text("ZZZ")                  # nothing is focused, so this must
    time.sleep(1.5)                     # reach nothing at all
    os88marty.settle(m)
    n = ndiff(before, crop(m, wx, wy, ww, wh))
    check("typing with nothing focused changes nothing", n == 0, "(%d px)" % n)

    fw, fh, data = m.fbuf()
    os88marty.write_png_rgb("build/ftpdfocus.png", fw, fh, data)

print("\nftpdfocus: %s"
      % ("PASS" if not fails else "FAILED: " + ", ".join(fails)))
sys.exit(1 if fails else 0)

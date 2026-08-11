#!/usr/bin/env python3
"""How many rows of the SECOND monitor can a straddling window use?
(SPEC.md 39.19.3)

    make && python3 tests/dispvy.py

A window whose origin is on the primary is floored at MBAR_H by ui_ylow,
because the primary's menu bar is there. The second card has no menu bar - so
with display 1 placed at (primary width, 0) those MBAR_H rows of it were spent
on a band no straddling window could enter: 20 of a CGA's 200, 10% of the
monitor, on the machine this project is calibrated against.

Placing display 1 at (primary width, MBAR_H) maps virtual MBAR_H to its local
row 0 and gives them back. This measures exactly that, in the only terms that
matter: drag a window as high as the clamp allows and ask which of the second
card's rows it reaches.

  before   topmost row reached = MBAR_H
  after    topmost row reached = 0

...and it checks the SECOND card's pixels rather than the window record,
because a record saying y = MBAR_H with the display's origin moved underneath
it is the same number meaning two different things.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

MBAR_H, TITLE_H = 20, 18
VID_CTX_SZ, VID_CTX_VX, VID_CTX_VY = 42, 36, 38
S = os88sym.linear


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)

    fail = []
    say = lambda s: print("  " + s)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        if len(m.cards()) != 2:
            sys.exit("dispvy: %s is not a two-card machine" % a.machine)
        cards = m.cards()
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_primary(m, mo, S, os88marty.settle, 0)     # HERCULES, as
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")  # the field is
        dispcp.close_panel(m, mo, S, os88marty.settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispvy: the Control Panel did not turn Extend on")

        kind = m.read(S("vid_kind"), 1)[0]
        pri = [c for c in cards if c["type"] == ("mda" if kind == 1
                                                 else "cga")][0]
        sec = [c for c in cards if c is not pri][0]
        skind = "herc" if sec["type"] == "mda" else "cga"
        ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
        seam = u16(ctx, VID_CTX_SZ + VID_CTX_VX)
        vy = u16(ctx, VID_CTX_SZ + VID_CTX_VY)
        say("display 1 (%s) at (%d,%d)" % (sec["type"], seam, vy))
        if vy != MBAR_H:
            fail.append("display 1's VY is %d, not MBAR_H (%d) - Right is "
                        "still aligning it with the primary's row 0 "
                        "(SPEC.md 39.19.3)" % (vy, MBAR_H))

        # a Disk window, dragged to straddle and then driven up as hard as the
        # clamp allows
        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri["idx"])
        w = dispcp.win_list(m, S)
        if not w:
            sys.exit("dispvy: no Disk window")
        slot = w[-1]
        wx, wy, ww, wh = dispcp.win_rect(m, S, slot)
        mo.drag(wx + ww // 2, wy + TITLE_H // 2, seam - ww // 3 + ww // 2,
                wy + TITLE_H // 2)
        os88marty.settle(m, card=sec["idx"])
        wx, wy, ww, wh = dispcp.win_rect(m, S, slot)
        if not wx < seam < wx + ww - 1:
            sys.exit("dispvy: the window does not straddle the seam")
        # ...to MBAR_H and not to 0: past the seam, virtual row 0 is now the
        # DEAD ZONE, and mou_clamp correctly refuses to put the pointer there
        # (it raises rather than drifting, which is os88mouse doing its job).
        mo.drag(wx + ww // 2, wy + TITLE_H // 2, wx + ww // 2, MBAR_H)
        os88marty.settle(m, card=sec["idx"])
        wx, wy, ww, wh = dispcp.win_rect(m, S, slot)
        say("straddling at (%d,%d) %dx%d - the clamp stopped it at y=%d"
            % (wx, wy, ww, wh, wy))
        if wy != MBAR_H:
            fail.append("the drag floor is %d, not MBAR_H - a straddling "
                        "window's origin is on the primary and ui_ylow should "
                        "give it the chrome's floor" % wy)

        # ...and now the only question: which of the SECOND card's rows does
        # it actually cover? Its own columns, so the desktop cannot answer.
        # ANY LIT PIXEL IS NOT THE TEST, and the first version of this made
        # that mistake: the desktop is a 50% dither, so "is anything lit here"
        # answers yes on bare desktop and reported row 0 for both builds - a
        # gate that would have passed the change it exists to check. What
        # separates window from desktop is UNIFORMITY: a window's content is
        # solid white and its frame a solid black rule, and the dither is
        # neither.
        sw, sh, rows = m.vram(skind)
        x0, x1 = 2, min(wx + ww - 1, seam + sw - 1) - seam - 2
        span = max(1, x1 - x0 + 1)
        top = None
        for y in range(0, min(sh, 80)):
            r = rows[y]
            f = sum(1 for x in range(max(0, x0), x1 + 1) if r[x]) / float(span)
            if abs(f - 0.5) > 0.3:
                top = y
                break
        say("the topmost row of the second card it reaches: %s" % top)
        if top is None:
            fail.append("the window covers none of the second card at all")
        elif top > 2:
            fail.append("it reaches only row %d of the second monitor - %d "
                        "rows of it are unreachable by a straddling window "
                        "(SPEC.md 39.19.3)" % (top, top))

    print()
    for f in fail:
        print("dispvy: FAIL: %s" % f)
    if fail:
        return 1
    print("dispvy: a straddling window reaches the second monitor's first "
          "row - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

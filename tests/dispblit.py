#!/usr/bin/env python3
"""Does a BLIT reach the second display? (SPEC.md 39.14.7)

    make && python3 tests/dispblit.py

`gfx_blit4` used to take `GFXDENTER` - one display, the one holding its
top-left corner, cut at that display's edge - on the argument that a blit
cannot take a sub-rect without advancing its source to match. That argument is
about the DESTINATION RECT and it is false of a RUN: `gfx_blit4` emits one
solid `gfx_hline` per coalesced run, and a run of one colour refers to no
source at all. So the runs split themselves through `gfx_fill`'s `GFXDISP`,
and the hook is gone.

What it cost while it was there, reported from a Hercules+CGA 5150: *Paint's
canvas does not draw on the second screen when straddled.* Every stroke landed
on both cards (the pencil is `gfx_line` per mouse SAMPLE, so each little
segment resolves its own display) and every REPAINT took the picture back off
the second one, because the whole canvas arrives as one `OSAPI_GFX_BLIT4`.

So the test is that shape exactly, and it has to be, because the two halves
disagree only across a repaint:

  1. straddle Paint and draw over the seam - both cards get ink,
  2. force a full repaint by dragging the window away and back to the SAME
     place, so the region measured is the same rectangle both times,
  3. the second display's half of the canvas still holds the drawing.

Step 3 is the assertion; steps 1 and 2 are what make it mean anything. A run
of this against a kernel with the hook back in must FAIL, or the test does not
contain the case (SPEC.md 39.14.6's lesson, learned twice on Note Pad).

It builds its own one-file B: disk rather than navigating `apps360.img`, so
"row 0" is PAINT.O88 and stays PAINT.O88 whatever else gets added to APPS.
"""
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

MBAR_H, TITLE_H = 20, 18
# vid_ctx: an 18-word run with vid_cw/vid_ch inside it, then VX/VY/KIND.
VID_CTX_CW, VID_CTX_CH, VID_CTX_SZ = 14, 16, 42
VID_CTX_VX, VID_CTX_VY = 36, 38
PT_CV_X, PT_CHROME_W, PT_CHROME_H = 44, 46, 42
S = os88sym.linear


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def win_by(m, slot):
    return dispcp.win_rect(m, S, slot)          # WIN_SIZE lives in dispcp


def wins(m):
    return dispcp.win_list(m, S)                # ...and so does the check


def region(rows, x0, x1, y0, y1):
    out = bytearray()
    for y in range(y0, y1 + 1):
        r = rows[y]
        out += bytes(r[x0:x1 + 1])
    return bytes(out)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/blitdisk.img")
    a = ap.parse_args(argv)

    if not os.path.exists(a.apps):
        subprocess.check_call([sys.executable, "tools/os88disk.py",
                               "-o", a.apps, "--size", "360",
                               "build/paint.o88"])

    fail = []
    say = lambda s: print("  " + s)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        cards = m.cards()
        if len(cards) != 2:
            sys.exit("dispblit: %s has %d video card(s), not 2"
                     % (a.machine, len(cards)))
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("dispblit: the Control Panel did not turn Extend on")

        kind = m.read(S("vid_kind"), 1)[0]
        pri = [c for c in cards if c["type"] == ("mda" if kind == 1
                                                 else "cga")][0]
        sec = [c for c in cards if c is not pri][0]
        skind = "herc" if sec["type"] == "mda" else "cga"
        ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
        seam = u16(ctx, VID_CTX_SZ + VID_CTX_VX)
        secw = u16(ctx, VID_CTX_SZ + VID_CTX_CW)
        sech = u16(ctx, VID_CTX_SZ + VID_CTX_CH)
        say("primary %s, secondary %s at x=%d (%dx%d content)"
            % (pri["type"], sec["type"], seam, secw, sech))

        # --- Paint, off a disk whose only file is Paint ----------------------
        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri["idx"])
        w = wins(m)
        if not w:
            sys.exit("dispblit: no Disk window after double-clicking B:")
        dslot = w[-1]
        dx, dy, dw, dh = win_by(m, dslot)
        dispcp.open_row(m, mo, S, os88marty.settle, dx, dy,
                        card=pri["idx"])
        w = wins(m)
        if dslot == w[-1]:
            sys.exit("dispblit: PAINT.O88 did not launch from row 0")
        pslot = w[-1]
        px, py, pw, ph = win_by(m, pslot)
        cw = pw - 2 - PT_CHROME_W                 # the canvas, from the frame
        ch = ph - TITLE_H - 1 - PT_CHROME_H
        say("Paint at (%d,%d) %dx%d -> a %dx%d canvas" % (px, py, pw, ph,
                                                          cw, ch))
        if cw < 120 or ch < 24:
            sys.exit("dispblit: that canvas is too small to straddle anything")

        # --- straddle it: the canvas's MIDDLE on the seam --------------------
        want = seam - PT_CV_X - 1 - cw // 2
        mo.drag(px + pw // 2, py + TITLE_H // 2, want + pw // 2,
                py + TITLE_H // 2)
        os88marty.settle(m, card=sec["idx"])
        px, py, pw, ph = win_by(m, pslot)
        cx0 = px + 1 + PT_CV_X                    # canvas, in virtual coords
        cy0 = py + TITLE_H + 1
        say("...dragged to (%d,%d): canvas x %d..%d, y %d..%d"
            % (px, py, cx0, cx0 + cw - 1, cy0, cy0 + ch - 1))
        if not cx0 < seam < cx0 + cw - 1:
            sys.exit("dispblit: the canvas does not cross the seam at %d - "
                     "there is no case in this test" % seam)

        # the part of the canvas that is on the SECOND card, in ITS coordinates
        rx1 = min(cx0 + cw - 1, seam + secw - 1) - seam
        ry0, ry1 = cy0, min(cy0 + ch - 1, sech - 1)
        say("the second card's share of it: local x 0..%d, y %d..%d"
            % (rx1, ry0, ry1))

        # --- 1: draw over the seam -------------------------------------------
        for i in range(4):
            y = cy0 + 8 + i * max(4, (ch - 16) // 4)
            if y > ry1:
                break
            mo.drag(seam - 90, y, seam + 90, y)
        os88marty.settle(m, card=sec["idx"])
        sw, sh, rows = m.vram(skind)
        before = region(rows, 0, rx1, ry0, ry1)
        ink_before = sum(1 for b in before if not b)
        say("after drawing: %d unlit (inked) px of %d on the second card"
            % (ink_before, len(before)))
        if ink_before < 40:
            fail.append("nothing was drawn onto the second card at all - the "
                        "strokes never got there, so the repaint below cannot "
                        "be about a blit")

        # --- 2: force a full repaint, in the SAME place ----------------------
        mo.drag(px + pw // 2, py + TITLE_H // 2, px + pw // 2 - 96,
                py + TITLE_H // 2)
        os88marty.settle(m, card=sec["idx"])
        mo.drag(px + pw // 2 - 96, py + TITLE_H // 2, px + pw // 2,
                py + TITLE_H // 2)
        os88marty.settle(m, card=sec["idx"])
        nx, ny, nw, nh = win_by(m, pslot)
        say("...dragged away and back: (%d,%d), was (%d,%d)"
            % (nx, ny, px, py))
        if (nx, ny) != (px, py):
            fail.append("the window came back to (%d,%d) and not (%d,%d) - "
                        "the two captures are not the same rectangle"
                        % (nx, ny, px, py))

        # --- 3: the picture is still on the second card ----------------------
        sw, sh, rows = m.vram(skind)
        after = region(rows, 0, rx1, ry0, ry1)
        ink_after = sum(1 for b in after if not b)
        diff = sum(1 for x, y in zip(before, after) if x != y)
        say("after the repaint: %d inked px (was %d), %d px differ"
            % (ink_after, ink_before, diff))
        if ink_after < ink_before // 2:
            fail.append("the repaint took %d of %d inked pixels off the "
                        "second card - gfx_blit4 is cut at the display edge "
                        "(SPEC.md 39.14.7)" % (ink_before - ink_after,
                                               ink_before))

    print()
    for f in fail:
        print("dispblit: FAIL: %s" % f)
    if fail:
        return 1
    print("dispblit: a straddled canvas survives its own repaint on both "
          "cards - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

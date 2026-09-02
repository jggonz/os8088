#!/usr/bin/env python3
"""DOES DRAWING ON THE PLANAR CANVAS TOUCH ONLY WHAT IT DREW? (SPEC.md 42.13)

    make && python3 tests/paintdraw.py

tests/paintplan.py opens a picture and compares it against the file, so it
covers every routine that WRITES a whole row - the GIF decoder, pt_wipe,
pt_line_put. It covers none of the ones that write PART of one, and those are
the tools: pt_rect is the pencil's dab and the eraser's and the rectangle's,
and on four planes it builds a left mask, a right mask and a byte count that
the packed path gets from a single shift.

That is worth its own row because the failure is not subtle and it is not
visible either. `mov ah, 0xFF` writes the high half of the register the left
byte column is sitting in, so the span came out 61,696 instead of 0 and one
pencil dab was a `rep stosb` of nearly the whole segment, four times a row -
seconds of it on the target machine, and a black smear over everything the
stroke had passed. On screen it looked RIGHT the whole time, because the
screen half of pt_rect is one gfx_fill of the rect that was asked for; the
canvas is what was wrong, and it only showed on the next full repaint.

So the oracle here is not "the picture came back": it is **a stroke may only
change pixels inside its own bounding box**. Everything outside it must still
be the file, to the pixel. A span that runs away fails that by tens of
thousands of pixels whatever route it takes through memory, and a stroke that
is drawn correctly passes it without this test having to know Bresenham.

The screen is read TWICE: once with the picture as the stroke left it, and
once after the window has been dragged, which repaints the canvas from
memory. The first says the tools drew what they were asked for; the second
says the canvas AGREES - which is the half a `gfx_fill` to the screen hides.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp                 # noqa: E402
from blitpair import gif_pixels                              # noqa: E402

S = os88sym.linear
PAD = 6                     # the brush is 1px, but the bbox is checked with
                            # room for the widest nib the strip can select


def split(fb, fw, ox, oy, iw, ih, px, box):
    """(differing pixels inside the stroke's box, differing pixels outside)."""
    x0, y0, x1, y1 = box
    inside, out = [], []
    for rw in range(ih):
        for c in range(iw):
            if (fb[((oy + rw) * fw + ox + c) * 3] < 128) == (px[rw * iw + c] == 1):
                continue
            (inside if x0 <= c <= x1 and y0 <= rw <= y1 else out).append((c, rw))
    return inside, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/paintdraw.img")
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--gif", default="build/OS8088.GIF")
    a = ap.parse_args()

    if a.apps == "/tmp/paintdraw.img":
        os88marty.scratch_disk(a.apps, "APPS:build/paint.o88",
                               "MEDIA:" + a.gif)

    iw, ih, px = gif_pixels(a.gif)
    print("   %s: %dx%d" % (a.gif, iw, ih))

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, bx, by, "MEDIA")
        os88marty.settle(m)
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, os88marty.settle,
                                                bx, by,
                                                dispcp.row_of(m, S,
                                                              "OS8088.GIF")))
        mo.to(rx, ry)
        os88marty.settle(m)

        # The canvas origin is ASKED for, off the blit that draws it, for
        # tests/paintplan.py's reason: where Paint puts its picture is not
        # this test's opinion to hold.
        m.bp_exec("gfx_blitp")
        mo.dblclick(rx, ry)
        geom = None
        for _ in range(40):
            if not m.wait_stop(limit=300.0):
                break
            r = m.regs()
            if r["cx"] >= iw:
                geom = (r["ax"], r["bx"])
                break
            m.bp_exec("gfx_blitp")
            m.run()
        if geom is None:
            sys.exit("paintdraw: no gfx_blitp as wide as the picture - the "
                     "canvas is not planar, or it fell back to nibbles")
        ox, oy = geom
        m.bp_exec()
        m.run()
        time.sleep(6)
        pw = [w for w in dispcp.win_list(m, S) if w != disk][-1]
        wx, wy, ww, wh = dispcp.win_rect(m, S, pw)

        # --- the stroke, LOW IN THE PICTURE and on purpose. A pt_rect that
        # runs away writes forward from the row it was given, and the canvas
        # is stored bottom row first, so the smear covers every row ABOVE the
        # one being drawn. Near the top of OS8088.GIF that is the logo's black
        # ground, and black ink over black ground is a defect nothing can see;
        # from the bottom it crosses the white lettering and cannot hide.
        #
        # Deliberately NOT byte-aligned at either end either: the left and
        # right masks are what pt_rect builds and a run that starts and stops
        # mid-byte exercises both.
        #
        # And WALKED rather than dragged in one hop: pt_stroke polls the
        # mouse, so a press and a single jump to the far end is a stroke the
        # guest sees two positions of. Each waypoint is its own report.
        y0 = ih - 12
        pts = [(21, y0), (27, y0 - 3), (33, y0 - 5), (39, y0 - 7),
               (45, y0 - 9), (51, y0 - 6), (55, y0 - 4), (61, y0 - 2)]
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        box = (min(xs) - PAD, min(ys) - PAD, max(xs) + PAD, max(ys) + PAD)
        t0 = time.time()
        c0 = m.status()["cycles"]
        mo.to(ox + pts[0][0], oy + pts[0][1])
        if mo.where()[2] & 1:
            mo._edge(False)
        mo._edge(True)
        for sx, sy in pts[1:]:
            mo.to(ox + sx, oy + sy, l=True)
        mo._edge(False)
        os88marty.settle(m)
        cyc = m.status()["cycles"] - c0
        print("   stroke %r: %.1fs wall, %d guest cycles" %
              (pts, time.time() - t0, cyc))
        mo.to(4, 4)                 # the arrow is drawn INTO the framebuffer
        os88marty.settle(m)
        fw, fh, fb = m.fbuf(card=0)
        din, dout = split(fb, fw, ox, oy, iw, ih, px, box)

        # --- and again from the canvas: a drag repaints it out of memory
        newy = wy + 40
        mo.drag(wx + ww // 2, wy + 9, wx + ww // 2, newy)
        os88marty.settle(m)
        time.sleep(6)
        mo.to(4, 4)
        os88marty.settle(m)
        wx2, wy2 = dispcp.win_rect(m, S, pw)[:2]
        lw, lh, lfb = m.fbuf(card=0)
        nox, noy = ox + (wx2 - wx), oy + (wy2 - wy)
        print("   window %d,%d -> %d,%d; canvas %d,%d -> %d,%d"
              % (wx, wy, wx2, wy2, ox, oy, nox, noy))
        ain, aout = split(lfb, lw, nox, noy, iw, ih, px, box)

    ok = True
    for name, good, bad in (("on screen", din, dout),
                            ("after a repaint", ain, aout)):
        print("   %-16s %d pixels changed inside the stroke's box, %d outside"
              % (name, len(good), len(bad)))
        # A STROKE THAT DREW NOTHING PASSES EVERY BOUNDS CHECK THERE IS, so
        # the count inside the box is asserted first: a drag the guest did not
        # see reads exactly like a tool that stayed inside its rect.
        if len(good) < 20:
            print("      ...the stroke did not draw: the drag never reached "
                  "the canvas, so this run proves nothing")
            ok = False
        if bad:
            xs = [b[0] for b in bad]
            ys = [b[1] for b in bad]
            print("      box x %d..%d y %d..%d, first 8: %r"
                  % (min(xs), max(xs), min(ys), max(ys), bad[:8]))
            ok = False
    if not ok:
        sys.exit("paintdraw: FAILED")
    print("paintdraw: the pencil touched only what it drew")
    return 0


if __name__ == "__main__":
    sys.exit(main())

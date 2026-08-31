#!/usr/bin/env python3
"""IS PAINT'S PLANAR CANVAS THE PICTURE? (SPEC.md 42.13)

    make && python3 tests/paintplan.py

On a colour adapter Paint stores its canvas as FOUR PLANES - the layout the
card wants - so a repaint is a copy rather than a transpose (SPEC.md 5.4.3).
That changes every routine that reads or writes a pixel, and the way to know
they all agree is to make the picture come back out: open OS8088.GIF, which
is two colours, and compare the screen against THE FILE.

It is an end-to-end test on purpose. Between the file and the screen sit the
GIF decoder, pt_line_put packing colour indices into four planes, pt_wipe,
pt_adopt's resize, and gfx_blitp - and a defect in any of them shows here as
pixels that are not the logo. Nothing is mocked and there is no golden image
to regenerate.

IT RUNS TWICE, and the second pass drags the window BELOW [vid_rowmax] -
gfx_rowbase answers those rows with a `mul` that writes DX and a shift that
writes CL, and SPEC.md 5.4.3.1 is what a row loop holding either of them did
there. Every window this harness opens otherwise sits in the top half of the
screen.

TWO THINGS IT ALSO PROVES, both by construction. That the canvas went planar
at all: the geometry comes off a breakpoint on gfx_blitp, and on a build that
stayed packed that breakpoint never fires. And that the canvas ORIGIN is on
the byte grid, because gfx_blitp refuses anything else and Paint would have
fallen back to nibbles (pt_topacked) before the first frame.
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
KBASE = os88sym.KERNEL_SEG << 4


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/paintplan.img")
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--gif", default="build/OS8088.GIF")
    a = ap.parse_args()

    if a.apps == "/tmp/paintplan.img":
        os88marty.scratch_disk(a.apps, "APPS:build/paint.o88",
                               "MEDIA:" + a.gif)

    iw, ih, px = gif_pixels(a.gif)
    black = sum(1 for v in px if v == 1)
    print("   %s: %dx%d, %d black of %d" % (a.gif, iw, ih, black, iw * ih))

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

        # The CANVAS blit, not a palette swatch: the first gfx_blitp as wide
        # as the picture. Anything narrower is Paint's own furniture.
        m.bp_exec("gfx_blitp")
        mo.dblclick(rx, ry)
        geom = None
        for _ in range(40):
            if not m.wait_stop(limit=300.0):
                break
            r = m.regs()
            if r["cx"] >= iw:
                geom = (r["ax"], r["bx"], r["cx"], r["dx"])
                break
            m.bp_exec("gfx_blitp")
            m.run()
        if geom is None:
            sys.exit("paintplan: no gfx_blitp as wide as the picture - the "
                     "canvas is not planar, or it fell back to nibbles")
        x, y, w, h = geom
        print("   canvas blit x=%d y=%d w=%d h=%d%s"
              % (x, y, w, h, "" if x % 8 == 0 else "  ...NOT byte-aligned"))
        m.bp_exec()
        m.run()
        time.sleep(6)
        mo.to(4, 4)                 # the arrow is drawn INTO the framebuffer
        os88marty.settle(m)
        fw, fh, fb = m.fbuf(card=0)

        # --- AND AGAIN WITH THE CANVAS BELOW gfx_rowbase's TABLE
        # (SPEC.md 5.4.3.1, and the reason this pass exists at all).
        # [vid_rowmax] rows are answered from vid_rowtab and everything under
        # that from gfx_rowbase_calc, which ends in a `mul` and so writes DX -
        # and takes its shift count in CL. A row loop holding a counter or a
        # byte column in either of those is correct for the top 348 rows of a
        # 480-row screen and runs away below them. That is not a case a test
        # finds by accident: every window this harness opens sits in the top
        # half. The window is moved by hand for the same reason the odd-x
        # case is - a drag cannot reliably put it there.
        low = None
        rowmax = u16(m.read(S("vid_rowmax"), 2))
        vh = u16(m.read(S("vid_ch"), 2))
        pw = [w for w in dispcp.win_list(m, S) if w != disk][-1]
        wx, wy, ww, wh = dispcp.win_rect(m, S, pw)
        newy = min(rowmax - 20, vh - wh - 1)
        print("   window (%d,%d,%d,%d) -> y=%d, rowmax=%d vh=%d"
              % (wx, wy, ww, wh, newy, rowmax, vh))
        if newy > wy:
            # DRAGGED, not poked: a window that merely had its W_Y rewritten
            # is put back by the raise cache without a W_PAINT, so nothing
            # blits and the pass proves nothing. A drag is a real move and
            # ends in a real repaint - which is also exactly what the field
            # did to find this.
            m.bp_exec("gfx_blitp")
            mo.drag(wx + ww // 2, wy + 9, wx + ww // 2, newy + 9)
            for _ in range(40):
                if not m.wait_stop(limit=300.0):
                    print("   ...no gfx_blitp fired after the raise: either it "
                          "REFUSED and Paint fell back to nibbles, or the "
                          "machine is not coming back")
                    break
                r = m.regs()
                if r["cx"] >= iw:
                    low = (r["ax"], r["bx"], r["cx"], r["dx"])
                    break
                m.bp_exec("gfx_blitp")
                m.run()
            m.bp_exec()
            m.run()
            time.sleep(6)
            mo.to(4, 4)
            os88marty.settle(m)
            lw, lh, lfb = m.fbuf(card=0)

        # --- AND THE FOUR COLUMNS TO THE LEFT OF IT (SPEC.md 42.13.2).
        # Paint's window is WF_OWNBG, so the kernel fills none of its content
        # and a column no part of Paint draws shows the DESKTOP. Moving the
        # canvas to a multiple of 8 left exactly four of those, and they read
        # as a black stripe down the left of every picture. The divider is
        # black, its bed is white, and neither is the 50% dither underneath.
        edge = []
        for row in range(y, y + h):
            if fb[(row * fw + x - 1) * 3] >= 128:
                edge.append(("divider not black", x - 1, row))
            for c in range(x - 5, x - 1):
                if fb[(row * fw + c) * 3] < 128:
                    edge.append(("bed not white", c, row))
        print("   left of the canvas: divider at x=%d, bed x=%d..%d, "
              "%d wrong pixels" % (x - 1, x - 5, x - 2, len(edge)))
        if edge:
            print("      first 6: %r" % (edge[:6],))

        # THE REFUSAL PATH (SPEC.md 42.13.1) IS NOT TESTED HERE and cannot
        # be from a shipped kernel: gfx_blitp says no to a 1bpp adapter, an x
        # off the byte grid and a straddled seam, and none of the three is
        # reachable by dragging - the window manager clamps a window to the
        # desktop, so the canvas never hangs off an edge, and this harness has
        # no machine with a VGA and a second card in it. tests/paintpack.py is
        # that row: it builds a NOPLANE kernel, where every gfx_blitp refuses
        # in six bytes, so pt_topacked runs on the first blit.

    if edge:
        sys.exit("paintplan: FAILED - %d pixels between the tool column and "
                 "the canvas belong to nobody" % len(edge))
    if (w, h) != (iw, ih):
        sys.exit("paintplan: the canvas is %dx%d and the picture %dx%d - "
                 "Paint cropped it, so this is comparing two things"
                 % (w, h, iw, ih))
    bad = [(c, rw)
           for rw in range(h) for c in range(w)
           if (fb[((y + rw) * fw + x + c) * 3] < 128) != (px[rw * iw + c] == 1)]
    print("   canvas %dx%d on %dx%d: %d pixels differ from the file"
          % (w, h, fw, fh, len(bad)))
    if bad:
        xs = [b[0] for b in bad]
        ys = [b[1] for b in bad]
        print("   box x %d..%d  y %d..%d, first 12: %r"
              % (min(xs), max(xs), min(ys), max(ys), bad[:12]))
        sys.exit("paintplan: FAILED")
    if low is None:
        print("   (no second pass: the window would not fit below row %d)"
              % rowmax)
    else:
        lx, ly, lwid, lhgt = low
        print("   canvas below the row table: y=%d..%d, table ends at %d"
              % (ly, ly + lhgt - 1, rowmax))
        bad = [(c, rw)
               for rw in range(lhgt) for c in range(lwid)
               if (lfb[((ly + rw) * lw + lx + c) * 3] < 128)
               != (px[rw * iw + c] == 1)]
        print("   canvas %dx%d: %d pixels differ from the file"
              % (lwid, lhgt, len(bad)))
        if bad:
            sys.exit("paintplan: FAILED below the row table")
    print("paintplan: the planar canvas IS the picture, %d pixels" % (iw * ih))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""HOW LONG DOES PAINT TAKE TO OPEN OS8088.GIF? - in GUEST CYCLES

    make && python3 tests/paintgif.py
    ...and against another kernel:
    python3 tests/paintgif.py --image /tmp/ref-360.img

Reported from the field: the same GIF that renders in about a second on
build 674 takes more than ten on 704, with Paint's own binary BYTE-IDENTICAL
between them (`md5sum build/paint.o88`) - so whatever it is, it is kernel
side.

Cycles rather than wall time, because MartyPC runs the guest at whatever
multiple of real time the host manages and a stopwatch here measures the
host. Guest cycles on a 4.77MHz 8088 are seconds/4,772,727 and are exactly
reproducible. `m.disk()` is read across the same span, because "10 seconds"
on a machine with a floppy in it is the shape of a disk regression as much as
a drawing one - and OS8088.GIF is 2,138 bytes, five sectors, so it should not
be either.

The apps disk is built for this (`os88disk.py -o /tmp/paintbench.img --size
360 APPS:build/paint.o88 MEDIA:build/OS8088.GIF`) so that Paint's Open dialog
comes up on B:\\MEDIA with the GIF the only thing in it - SPEC.md 38.10's
default, and it removes every navigation click from the measurement.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")

from os88geom import (VID_CTX_SZ, VID_CTX_VX,          # noqa: E402
                      VID_CTX_VY, VID_CTX_KIND, VID_CTX_CH)
# SPEC.md 39.14's per-display record: DERIVED from VID_CTX_W and never
# written down here. This file spelled it `42 + 36`, which is the
# VID_CTX_W = 18 layout - two bytes early, and what sits there is
# display 1's vid_chm8, so the seam read 192 instead of 720.
import os88marty, os88mouse, os88sym, dispcp                # noqa: E402

S = os88sym.linear
TITLE_H = 18


def u16(b, i=0): return b[i] | (b[i + 1] << 8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/paintbench.img")
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--far", action="store_true",
                    help="...and then drag Paint wholly onto the second one")
    ap.add_argument("--extend", action="store_true",
                    help="extend the desktop onto the second display first - "
                         "the field says the slowdown needs this")
    a = ap.parse_args()

    # The docstring's recipe, RUN rather than transcribed: a registered row
    # that only passes after a human builds its disk by hand is a row that
    # fails for a reason the summary line cannot name.
    if a.apps == "/tmp/paintbench.img":
        os88marty.scratch_disk(a.apps, "APPS:build/paint.o88",
                               "MEDIA:build/OS8088.GIF")

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run(); os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        if a.extend:
            dispcp.open_panel(m, mo, S, os88marty.settle)
            dispcp.set_mode(m, mo, S, os88marty.settle, "right")
            dispcp.close_panel(m, mo, S, os88marty.settle)
            print("extended: vid_ndisp=%d vid_w=%d"
                  % (m.read(S("vid_ndisp"), 1)[0], u16(m.read(S("vid_w"), 2))))

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, bx, by, "MEDIA")
        os88marty.settle(m)

        # OS8088.GIF, double-clicked, launches Paint THROUGH THE ASSOCIATION
        # (SPEC.md 54) with the picture already decoded and drawn, which is
        # exactly the operation the field timed and is one click rather than a
        # menu walk. The click is hand-built rather than open_named's because
        # the whole measurement is the cycles either side of it - but WHICH
        # ROW is still asked rather than written down (it used to be a literal
        # 1, on the reasoning that row 0 is the synthesized `..`).
        bx, by, bw, bh = dispcp.win_rect(m, S, disk)
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, os88marty.settle,
                                                bx, by,
                                                dispcp.row_of(m, S,
                                                              "OS8088.GIF")))

        mo.to(rx, ry)
        os88marty.settle(m)
        c0 = m.status()["cycles"]
        m.disk(reset=True)
        mo.dblclick(rx, ry)
        # WAIT FOR IT RATHER THAN SLEEPING A GUESS - the whole question is
        # how long this takes, so a fixed sleep either truncates the slow
        # case or pads the fast one.
        t0 = time.time()
        while time.time() - t0 < 90.0:
            if [w for w in dispcp.win_list(m, S) if w != disk]:
                break
            time.sleep(0.2)
        os88marty.settle(m)
        c1 = m.status()["cycles"]
        d = m.disk()

        pw = [w for w in dispcp.win_list(m, S) if w != disk]
        if not pw:
            sys.exit("paint did not open on the association")
        print("paint window %r" % (dispcp.win_rect(m, S, pw[-1]),))
        w, h, fb = m.fbuf(card=0)
        os88marty.write_png_rgb("/tmp/paintgif.png", w, h, fb)

        if a.far:
            # THE NEW PATH: a blit wholly on the SECOND display, which is the
            # case the gate translates. Straddling is tests/dispblit.py's and
            # one display is every machine that was already correct.
            seam = u16(m.read(S("vid_ctx"), 2 * VID_CTX_SZ),
                       VID_CTX_SZ + VID_CTX_VX)
            px, py, pw2, ph = dispcp.win_rect(m, S, pw[-1])
            mo.drag(px + pw2 // 2, py + TITLE_H // 2,
                    seam + 340, py + TITLE_H // 2)
            os88marty.settle(m, card=1)
            px, py, pw2, ph = dispcp.win_rect(m, S, pw[-1])
            print("paint dragged to x %d..%d (seam %d)"
                  % (px, px + pw2 - 1, seam))
            for c, nm in ((0, "vga"), (1, "herc")):
                w2, h2, fb2 = m.fbuf(card=c)
                os88marty.write_png_rgb("/tmp/paintgif-far-%s.png" % nm,
                                        w2, h2, fb2)
                print("   %s: %d lit" % (nm, sum(
                    1 for i in range(0, len(fb2), 3)
                    if fb2[i] or fb2[i + 1] or fb2[i + 2])))

        cyc = c1 - c0
        print()
        print("LAUNCH + DECODE + DRAW: %d cycles = %.2f s at 4.77MHz"
              % (cyc, cyc / 4772727.0))
        print("disk over the same span: %r" % (d,))
    return 0


if __name__ == "__main__":
    sys.exit(main())

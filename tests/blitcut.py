#!/usr/bin/env python3
"""DOES SPEC.md 39.14.7.2's CUT draw the same pixels - and how much faster?

    make && python3 tests/blitcut.py

blitplane.py's shape one seam along, and for the same reason: a straddling
`gfx_blit4` used to give up SPEC.md 5.4.1's fast path ENTIRELY, so every
coalesced run of the whole block became a `gfx_fill` at its own ~600us
arrival - including every run nowhere near the seam. 39.14.7.2 cuts the block
at the seam instead and runs the routine once per half, each on the fast path
it would have had if the window had never been dragged.

TWO RUNS OF ONE SCRIPT, ONE KERNEL EACH: the shipped one, and `NOBLITCUT=1`,
which puts the whole-virtual path of 39.14.7 back. Both must rasterise
IDENTICALLY on BOTH CARDS, and the shipped one must be several times quicker.

WHY OS8088.GIF AND NOT A BLANK CANVAS - blitplane.py's reason exactly. The
cut duplicates the per-ROW setup and splits the per-PIXEL work, so a picture
that coalesces well is the case it helps LEAST: a blank canvas is one run a
row and is nearly all setup. The logo's ground is SPEC.md 63's 50% dither,
which is the case the field reported and the case the decoders exist for.

WHY THE WINDOW IS DRAGGED RATHER THAN PLACED. It has to straddle a REAL seam
with a real dead zone above the second monitor's first row (39.19.3), and the
drag is also what makes `gfx_blitp` refuse and Paint convert its canvas to
nibbles (42.13.1) - which is what puts the block through `gfx_blit4` at all.
Placing W_X by hand would skip the refusal and measure a canvas that is still
four planes.

THE BRACKET IS ENTRY TO `.pops`, WITH SP BACK WHERE IT STARTED, and the SP
test is not belt-and-braces: with the cut in, one straddling blit enters
`gfx_blit4` THREE times - the outer call and its two halves - and all three
leave by the same `ret`. SP is what tells them apart, the halves' frames
sitting one return address deeper than the outer's.
"""
import argparse
import os
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
from os88geom import (VID_CTX_SZ, VID_CTX_VX, VID_CTX_VY,   # noqa: E402
                      VID_CTX_CW, VID_CTX_CH)

TITLE_H = 18
MBAR_H = int(os88sym.equates(()).get("MBAR_H", 20))
MIN_GAIN = 5.0          # measured 44.6x on VGA+MDA; the floor is a long way
                        # below it on purpose - what would break this test is
                        # the cut not firing, and that is a factor, not a few
                        # per cent


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def diff_box(mine, theirs, skip=None):
    """-> (count, (x0, y0, x1, y1)) over two same-sized bit images.

    The BOX is what makes a failure readable. A defect in the cut is
    structural - a column, a row, a block one nibble to the left - and lands
    as a box the shape of the thing that went wrong; anything that ticks over
    guest time lands as a box the shape of ITSELF, somewhere else entirely.

    `skip` is an (x0, y0, x1, y1) the compare ignores, and there is exactly
    one thing that needs it: THE CLOCK (SPEC.md 12.1). The two legs of this
    test run for wildly different amounts of GUEST time - one of them is 44x
    slower at the very thing under test, which is the point - so the menu
    bar's clock cell reads a different time in each and no amount of settling
    can make it agree. Measured before it was excluded: 12 differing pixels,
    ALL of them inside that cell and none outside it, the box (624,6,629,9)
    being one digit's strokes; and the count moved 25 -> 12 between two runs,
    which is itself the signature of a clock rather than of a defect.

    It is the CELL and not the menu bar, and not the whole strip: a real
    defect anywhere else in the chrome still fails this test.
    """
    w, h, a = mine
    b = theirs[2]
    n = 0
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    for j, (p, q) in enumerate(zip(a, b)):
        if p == q:
            continue
        x, y = j % w, j // w
        if skip and skip[0] <= x <= skip[2] and skip[1] <= y <= skip[3]:
            continue
        n += 1
        x0, y0 = min(x0, x), min(y0, y)
        x1, y1 = max(x1, x), max(y1, y)
    return n, (None if n == 0 else (x0, y0, x1, y1))


def run(image, apps, machine, defines):
    """-> (cycles, geometry, [card framebuffers as bit strings])"""
    def S(n):
        return os88sym.linear(n, defines)

    settle = os88marty.settle
    with os88marty.launch(image, apps=apps, machine=machine, boot=False) as m:
        if len(m.cards()) != 2:
            sys.exit("blitcut: %s is not a two-card machine" % machine)
        m.run()
        settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, settle)
        dispcp.set_primary(m, mo, S, settle, 0)
        dispcp.set_mode(m, mo, S, settle, "right")
        dispcp.close_panel(m, mo, S, settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("blitcut: the Control Panel did not turn Extend on")

        def ctx(i):
            return m.read(S("vid_ctx") + i * VID_CTX_SZ, VID_CTX_SZ)
        D = [(u16(ctx(i), VID_CTX_VX), u16(ctx(i), VID_CTX_VY),
              u16(ctx(i), VID_CTX_CW), u16(ctx(i), VID_CTX_CH))
             for i in (0, 1)]
        seam = max(D[0][0], D[1][0])

        dispcp.open_drive(m, mo, S, settle, "B", card=0)
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, settle, bx, by, "MEDIA", card=0)
        settle(m, card=0)
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, settle, bx, by,
                                                dispcp.row_of(m, S,
                                                              "OS8088.GIF"),
                                                card=0))
        mo.dblclick(rx, ry)
        settle(m, card=0, limit=300.0)
        time.sleep(4)
        pw = [w for w in dispcp.win_list(m, S) if w != disk][-1]
        wx, wy, ww, wh = dispcp.win_rect(m, S, pw)

        # STRADDLE: two thirds over the far card, so the seam is well inside
        # the canvas and the title bar stays on the near one.
        tgt = seam - ww // 3
        mo.drag(wx + ww // 2, wy + TITLE_H // 2,
                tgt + ww // 2, wy + TITLE_H // 2)
        settle(m, card=0, limit=300.0)
        time.sleep(4)
        wx2, wy2 = dispcp.win_rect(m, S, pw)[:2]
        if not (wx2 < seam < wx2 + ww):
            sys.exit("blitcut: the window ended at x=%d, which does not "
                     "straddle the seam at %d" % (wx2, seam))
        print("   window (%d,%d) %dx%d, seam x=%d" % (wx2, wy2, ww, wh, seam))
        clk = u16(m.read(S("vid_clk_hx"), 2))    # SPEC.md 12.1's cell, read
                                                 # from the guest: it is
                                                 # derived from the PRIMARY's
                                                 # width and is not a constant

        # --- MEASURE one canvas repaint, entry to return -------------------
        entry, ret = S("gfx_blit4"), S("gfx_blit4.pops") + 7
        m.bp_exec(entry, ret)
        m.run()
        pend, got, geom = None, [], None
        t0 = time.time()

        def driver():                       # a 16px nudge is a whole repaint
            mo.drag(wx2 + ww // 2, wy2 + TITLE_H // 2,
                    wx2 + ww // 2 + 16, wy2 + TITLE_H // 2)
        threading.Thread(target=driver, daemon=True).start()
        while time.time() - t0 < 240 and not got:
            if m.status().get("state") == "running":
                time.sleep(0.01)
                continue
            r = m.regs()
            ip = (r["cs"] << 4) + r["ip"]
            if ip == entry:
                if pend is None and r["cx"] > 200:      # the CANVAS, not a
                    pend = (r["sp"], m.status()["cycles"],      # dock tile
                            r["ax"], r["bx"], r["cx"], r["dx"])
            elif ip == ret and pend is not None and r["sp"] == pend[0]:
                got.append(m.status()["cycles"] - pend[1])
                geom = pend[2:]
            m.run()
        m.bp_exec()
        m.run()
        if not got:
            sys.exit("blitcut: no straddling canvas blit arrived in 240s")

        # ...and back where it started, so the two kernels are compared over
        # the same rectangle, with the pointer parked off it.
        mo.drag(wx2 + ww // 2 + 16, wy2 + TITLE_H // 2,
                wx2 + ww // 2, wy2 + TITLE_H // 2)
        settle(m, card=0, limit=300.0)
        mo.to(4, 4)
        settle(m, card=0, limit=200.0)
        time.sleep(6)
        fbs = []
        for c in m.cards():
            fw, fh, fb = m.fbuf(card=c["idx"])   # what the card RASTERISED:
            fbs.append((fw, fh,                  # mode 12h is four planes and
                        bytes(1 if fb[j] >= 128 else 0      # is not readable
                              for j in range(0, fw * fh * 3, 3))))
        if dispcp.win_rect(m, S, pw)[0] != wx2:
            sys.exit("blitcut: the window did not come back to x=%d" % wx2)
    return got[0], geom, fbs, clk


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/blitcut.img")
    ap.add_argument("--machine", default="os8088_xt_vga_mda")
    a = ap.parse_args()

    if a.apps == "/tmp/blitcut.img":
        os88marty.scratch_disk(a.apps, "APPS:build/paint.o88",
                               "MEDIA:build/OS8088.GIF")

    print("   shipped kernel:")
    cyc, geom, fbs, clk = run(a.image, a.apps, a.machine, ())

    # THIS ROW REBUILDS THE TREE (blitplane.py says why, and the `finally` is
    # not decoration): `make NOBLITCUT=1` writes build/kernel.bin, so a run
    # that dies in the middle leaves every emulator row after it driving a
    # kernel nobody asked for.
    ref = "/tmp/blitcut-noblitcut.img"
    print("   NOBLITCUT=1 kernel: building")
    try:
        subprocess.check_call(["make", "NOBLITCUT=1", a.image],
                              stdout=subprocess.DEVNULL)
        subprocess.check_call(["cp", a.image, ref])
        print("   NOBLITCUT=1 kernel:")
        rcyc, rgeom, rfbs, _ = run(ref, a.apps, a.machine, ("NOBLITCUT",))
    finally:
        subprocess.check_call(["make"], stdout=subprocess.DEVNULL)

    bad = 0
    if geom != rgeom:
        print("   the two kernels blitted DIFFERENT rects, %r against %r"
              % (geom, rgeom))
        bad += 1
    for i, (mine, theirs) in enumerate(zip(fbs, rfbs)):
        if mine[:2] != theirs[:2]:
            print("   card %d: %dx%d against %dx%d" % (i, mine[0], mine[1],
                                                       theirs[0], theirs[1]))
            bad += 1
            continue
        # ...and the clock is the PRIMARY's (SPEC.md 39.16.2), so display 1
        # is compared entire - which it passed before this existed.
        skip = (clk, 0, mine[0] - 1, MBAR_H - 1) if i == 0 else None
        d, box = diff_box(mine, theirs, skip)
        print("   card %d: %d differing pixels of %d%s"
              % (i, d, mine[0] * mine[1], "" if not d else " - box %r" % (box,)))
        if d:                       # ...and the two images, so a failure can
            for tag, im in (("cut", mine), ("nocut", theirs)):   # be looked at
                open("/tmp/blitcut-%s-%d.pgm" % (tag, i), "wb").write(
                    b"P5\n%d %d\n1\n" % (im[0], im[1]) + im[2])
            print("      -> /tmp/blitcut-{cut,nocut}-%d.pgm" % i)
        bad += d
    gain = rcyc / float(cyc)
    print("   the straddled canvas blit: %d cycles against %d - %.2fx"
          % (cyc, rcyc, gain))
    if gain < MIN_GAIN:
        print("blitcut: only %.2fx (floor %.1f) - the cut is not firing"
              % (gain, MIN_GAIN))
        bad += 1
    if bad:
        sys.exit("blitcut: FAILED")
    print("blitcut: identical pixels on both cards, and %.2fx" % gain)
    return 0


if __name__ == "__main__":
    sys.exit(main())

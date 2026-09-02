#!/usr/bin/env python3
"""DOES A 1bpp PAINT COME BACK FROM THE CACHE INSTEAD OF REDRAWING?
(SPEC.md 11.96.11, 42.13)

    make && python3 tests/paintsu.py

Paint's canvas is the most expensive repaint in the tree, and on a 1bpp
adapter it is expensive for a reason no format change can remove: the canvas
is 4bpp because it IS the file (SPEC.md 42), the screen holds a quarter of
that, and SPEC.md 39.4 maps all sixteen colours onto three classes - so the
reduction is not a re-ordering the way SPEC.md 42.13's four planes are on a
VGA. Measured on a cycle-accurate 5150/CGA, a 466x110 canvas is 399 ms, of
which 31.2 cycles a pixel are sw_blit_row's and 1.4 are gfx_blit4's, and
SPEC.md 5.4.1.2 already took the 2x that was available in the loop.

So Paint does not decode it faster - it arranges not to decode it at all. The
raise cache (SPEC.md 11.96) banks a covered window's pixels and puts them back
when it is revealed, and SPEC.md 11.96.11's own costing is that Paint's whole
content is ~9 KB on 1bpp against ~36 on VGA and ~150 grown large. On a colour
adapter Paint therefore banks the tool column as a BAND and redraws the
canvas; on a 1bpp one it banks the lot.

THREE ASSERTIONS NOW, and the third is what stops the second lying. A
restore is only answerable for what CHANGED across it, so the picture is
compared against the file BEFORE the cover as well as after: pixels that were
already wrong are reported as the draw's, pixels the cover changed are the
cache's. Without that split this row spent its life saying "the cache put back
the wrong pixels" about a cache that was restoring them faithfully - measured,
486 differing before the Control Panel opened and 455 after, the same rows
both times.

TWO ASSERTIONS, and the second is the one the measurement does not make.
That the canvas is not redrawn - no gfx_blit4 as wide as the picture crosses
the uncover, where the band build issues one 376 pixels wide. And that what
came back is RIGHT: a cache that restores the wrong pixels saves all the time
in the world. OS8088.GIF is two colours, so SPEC.md 39.4 sends every pixel of
it to a solid class and the framebuffer can be compared against the FILE.

The cover is the Control Panel, opened and closed. It is not a drag: a 640x200
screen has nowhere to put a 320x155 window clear of a 516x152 one, a click on
a background window's title does not raise it from here, and a drag leaves the
pointer clamped so every click after it lands somewhere else.
"""
import argparse
import os
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp                 # noqa: E402
from blitpair import gif_pixels                              # noqa: E402

S = os88sym.linear
BAND_KB = 8                 # the band cache measures 3 KB, the whole content 9


def serialise(m):
    """One socket, two threads.

    The debug link is a request/response pipe, and the thread that closes the
    panel and the loop that polls for a breakpoint interleave on it - which
    arrives as a JSON decode error rather than as anything about the guest.
    """
    lock = threading.Lock()
    inner = m.cmd

    def cmd(**kw):
        with lock:
            return inner(**kw)
    m.cmd = cmd


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/paintsu.img")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--gif", default="build/OS8088.GIF")
    a = ap.parse_args()

    if a.apps == "/tmp/paintsu.img":
        os88marty.scratch_disk(a.apps, "APPS:build/paint.o88",
                               "MEDIA:" + a.gif)

    iw, ih, px = gif_pixels(a.gif)
    kbase = os88sym.KERNEL_SEG << 4
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
        m.bp_exec("gfx_blit4")
        mo.dblclick(rx, ry)
        geom = None
        for _ in range(60):
            if not m.wait_stop(limit=300.0):
                break
            r = m.regs()
            if r["cx"] >= iw:
                geom = (r["ax"], r["bx"])
                break
            m.bp_exec("gfx_blit4")
            m.run()
        if geom is None:
            sys.exit("paintsu: the canvas never blitted through gfx_blit4 - "
                     "is this a 1bpp machine?")
        ox, oy = geom
        m.bp_exec()
        m.run()
        time.sleep(6)

        # --- WHAT IS ON THE GLASS BEFORE ANYTHING IS COVERED ---------------
        # WITHOUT THIS THE ROW BLAMES THE CACHE FOR WHAT THE DRAW GOT WRONG,
        # and it did: the picture came back from the cache pixel for pixel and
        # the row still said "the cache put back the wrong pixels", because
        # the two rows it compared were already wrong before the Control Panel
        # ever opened. A restore is only answerable for what CHANGED across
        # it, so both sides are measured and the two failures are named apart.
        mo.to(4, 4)                     # ...THE POINTER OFF THE CANVAS FIRST,
        os88marty.settle(m)             # which the uncover capture below has
        fw0, fh0, fb0 = m.fbuf(card=0)  # always done: it is parked over the
        before = _differs(fb0, fw0, ox, oy, iw, ih, px)   # file's icon at this
                                        # point, and an arrow drawn on the
                                        # canvas is 31 differing pixels that
                                        # belong to nothing

        # --- WHAT THE CACHE IS ASKED FOR, off wm_su_kb's own answer
        serialise(m)
        m.bp_exec("wm_su_kb")
        opened = []
        threading.Thread(
            target=lambda: (time.sleep(1.0),
                            opened.append(_open(m, mo))), daemon=True).start()
        want = None
        if m.wait_stop(limit=120.0):
            r = m.regs()
            ret = int.from_bytes(m.read((r["ss"] << 4) + r["sp"], 2), "little")
            m.bp_exec(kbase + ret)
            m.run()
            if m.wait_stop(limit=120.0):
                want = m.regs()["ax"]
        m.bp_exec()
        m.run()
        for _ in range(60):
            if opened:
                break
            time.sleep(1.0)
        print("   the raise cache asks for %s KB"
              % ("?" if want is None else want))
        time.sleep(6)

        # --- UNCOVER: is the canvas redrawn?
        m.bp_exec("gfx_blit4")
        done = []
        threading.Thread(
            target=lambda: (time.sleep(1.0),
                            done.append(_close(m, mo))), daemon=True).start()
        wide = None
        for _ in range(160):
            if not m.wait_stop(limit=45.0):
                break
            r = m.regs()
            if r["cx"] >= iw // 2:
                wide = (r["ax"], r["bx"], r["cx"], r["dx"])
                break
            m.bp_exec("gfx_blit4")
            m.run()
        m.bp_exec()
        m.run()
        for _ in range(60):
            if done:
                break
            time.sleep(1.0)
        time.sleep(8)
        mo.to(4, 4)
        os88marty.settle(m)
        fw, fh, fb = m.fbuf(card=0)

    print("   uncover: a canvas-sized gfx_blit4 %s"
          % ("never ran - the cache put it back" if wide is None
             else "RAN, x=%d y=%d w=%d h=%d" % wide))
    bad = _differs(fb, fw, ox, oy, iw, ih, px)
    print("   against the file: %d of %d pixels differed BEFORE the cover, "
          "%d after" % (len(before), iw * ih, len(bad)))
    for tag, w in (("before", before), ("after", bad)):
        if w:
            xs, ys = [b[0] for b in w], [b[1] for b in w]
            print("      %-6s box x %d..%d y %d..%d, first 8: %r"
                  % (tag, min(xs), max(xs), min(ys), max(ys), w[:8]))
    kept = set(before) & set(bad)
    lost = [b for b in bad if b not in set(before)]
    if want is None or want < BAND_KB:
        sys.exit("paintsu: the cache is %s KB - that is a BAND, so Paint did "
                 "not take the whole content on a 1bpp adapter" % want)
    if wide:
        sys.exit("paintsu: the canvas was redrawn, so the cache did not cover "
                 "it")
    if lost:
        sys.exit("paintsu: FAILED - the cache changed %d pixel(s) that were "
                 "right before the cover" % len(lost))
    if before:
        sys.exit("paintsu: FAILED - the canvas was ALREADY wrong in %d "
                 "pixel(s) before anything covered it, and the cache put "
                 "those same pixels back faithfully. This is not the raise "
                 "cache: look at what DREW them" % len(before))
    print("paintsu: the canvas came back from the cache, %d pixels, undrawn"
          % (iw * ih))
    return 0


def _differs(fb, fw, ox, oy, iw, ih, px):
    """Every canvas pixel that is not the file's, as (x, y)."""
    return [(c, rw) for rw in range(ih) for c in range(iw)
            if (fb[((oy + rw) * fw + ox + c) * 3] < 128)
            != (px[rw * iw + c] == 1)]


def _open(m, mo):
    dispcp.open_panel(m, mo, S, os88marty.settle, page=None)
    return True


def _close(m, mo):
    dispcp.close_panel(m, mo, S, os88marty.settle)
    return True


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""DOES THE FLOOD FILL FIND THE SAME EDGES THE PICTURE HAS? (SPEC.md 42.13.2)

    make && python3 tests/paintfill.py

The fill is the one tool that READS the canvas to decide what to draw, and on
four planes `pt_fpix` gathers one bit from each - so a plane addressed wrongly
does not corrupt anything, it makes the fill see a picture that is not there.
It shipped keeping the plane stride in CX and the plane counter in CH, which
is CX's own high half: `mov ch, 4` turned a stride of 60 into 1084 and three
planes of four came out of addresses the canvas does not own. What the user
saw was a fill stopping at boundaries nothing had drawn.

THE ORACLE IS A FLOOD FILL ON THE HOST, over the same file, from the same
seed. OS8088.GIF is two colours, so its pixels map one-to-one onto what the
screen shows, and filling WHITE with the default black ink needs no colour
click at all: every white pixel connected to the seed must come back black and
every other pixel must be untouched. That is an exact answer, not a
tolerance - a fill that leaks fails it, a fill that stops early fails it, and
a fill that gets the picture right for the wrong reason cannot.
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp                 # noqa: E402
from blitpair import gif_pixels                              # noqa: E402

S = os88sym.linear
PT_CV_X = 48                # SPEC.md 42.13: the canvas's inset in the content
PT_BW = 20                  # the palette's button side and pitch
PT_PAL_DY = 21
PT_T_FILL = 6


def tool_xy(ox, oy, i):
    """The centre of tool button i, in screen coordinates."""
    x = 1 if not (i & 1) else 22
    y = 1 + PT_PAL_DY * (i >> 1)
    return ox - PT_CV_X + x + PT_BW // 2, oy + y + PT_BW // 2


def host_fill(iw, ih, px, sx, sy):
    """The file's white region containing (sx,sy), four-connected.

    An explicit stack, not recursion: the logo's ground is one region of tens
    of thousands of pixels and CPython's frame limit is well under that.
    """
    white = bytearray(1 if v != 1 else 0 for v in px)
    if not white[sy * iw + sx]:
        sys.exit("paintfill: the seed is not a white pixel")
    seen = bytearray(iw * ih)
    stack = [(sx, sy)]
    seen[sy * iw + sx] = 1
    n = 0
    while stack:
        x, y = stack.pop()
        n += 1
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < iw and 0 <= ny < ih:
                i = ny * iw + nx
                if white[i] and not seen[i]:
                    seen[i] = 1
                    stack.append((nx, ny))
    return seen, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/paintfill.img")
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--gif", default="build/OS8088.GIF")
    a = ap.parse_args()

    if a.apps == "/tmp/paintfill.img" and not os.path.exists(a.apps):
        subprocess.check_call(
            [sys.executable, "tools/os88disk.py", "-o", a.apps, "--size",
             "360", "APPS:build/paint.o88", "MEDIA:" + a.gif])

    iw, ih, px = gif_pixels(a.gif)

    # The seed: a white pixel with white all around it, taken from the file
    # rather than guessed, so this does not depend on where the logo's
    # lettering happens to sit.
    seed = None
    for y in range(2, ih - 2):
        for x in range(2, iw - 2):
            if all(px[(y + dy) * iw + x + dx] != 1
                   for dy in (-2, -1, 0, 1, 2) for dx in (-2, -1, 0, 1, 2)):
                seed = (x, y)
                break
        if seed:
            break
    if seed is None:
        sys.exit("paintfill: no white pixel in %s to seed from" % a.gif)
    want, n = host_fill(iw, ih, px, *seed)
    print("   %s: %dx%d, seed %r, %d pixels in its white region"
          % (a.gif, iw, ih, seed, n))

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
            sys.exit("paintfill: no gfx_blitp as wide as the picture - the "
                     "canvas is not planar, or it fell back to nibbles")
        ox, oy = geom
        m.bp_exec()
        m.run()
        time.sleep(6)

        tx, ty = tool_xy(ox, oy, PT_T_FILL)
        mo.click(tx, ty)
        os88marty.settle(m)
        time.sleep(2)
        mo.click(ox + seed[0], oy + seed[1])
        os88marty.settle(m)
        time.sleep(10)
        mo.to(4, 4)                 # the arrow is drawn INTO the framebuffer
        os88marty.settle(m)
        fw, fh, fb = m.fbuf(card=0)

    leaked = []                     # filled where the file's region is not
    missed = []                     # still white inside it
    for rw in range(ih):
        for c in range(iw):
            black = fb[((oy + rw) * fw + ox + c) * 3] < 128
            inside = want[rw * iw + c]
            if inside and not black:
                missed.append((c, rw))
            elif not inside and black != (px[rw * iw + c] == 1):
                leaked.append((c, rw))
    print("   filled: %d pixels of the region left unfilled, %d pixels "
          "changed outside it" % (len(missed), len(leaked)))
    for name, bad in (("unfilled", missed), ("outside", leaked)):
        if bad:
            xs = [b[0] for b in bad]
            ys = [b[1] for b in bad]
            print("      %s: box x %d..%d y %d..%d, first 8: %r"
                  % (name, min(xs), max(xs), min(ys), max(ys), bad[:8]))
    if missed or leaked:
        sys.exit("paintfill: FAILED - the fill and the picture disagree "
                 "about where the edges are")
    print("paintfill: the fill found exactly the picture's edges, %d pixels"
          % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())

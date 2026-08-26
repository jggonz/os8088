#!/usr/bin/env python3
"""IS THE 1bpp CANVAS THE PICTURE? (SPEC.md 5.4.1.1, 39.4)

    make && python3 tests/blitpair.py [--machine os8088_5150_herc_gla]

`sw_blit_row` decodes a packed 4bpp row straight into the 1bpp framebuffer
through a pair of 256-byte tables, and it reads them with a SEGMENT OVERRIDE
- `ss xlat` since SPEC.md 5.4.1.3 moved them to `.lowbss`, `cs xlat` while
they were in `.bss`. A table read through the wrong segment is 256 bytes of
whatever else is there: the loop still runs, the blit still returns, and the
canvas is noise. Nothing in the suite could see that, which is why this row
exists.

WHY OS8088.GIF MAKES IT EXACT. The logo is TWO COLOURS, so 39.4 sends every
pixel of it to a solid class - black or white, no dither - and the canvas on
a 1bpp screen is then the GIF's own bitmap, pixel for pixel. So this compares
the framebuffer against the file rather than against another capture: no
golden image to regenerate, and no second build to run.

THE POINTER IS PARKED FIRST and that is not tidiness. The mouse arrow is
drawn INTO the framebuffer (SPEC.md 7.1), so a pointer resting on the canvas
is 31 differing pixels in an 8x12 box - which reads exactly like a decoder
bug and is a diagonal line if you look at where they are.

CGA AND NOT HERCULES, and the reason is the harness rather than the kernel.
`fbuf` comes back at (0, 0) on CGA and at **dx = -16, dy = +2** on Hercules
(docs/MARTYPC-DEBUG.md, and tests/dispband.py's `band` is where that bit
before), so a comparison in guest coordinates is sampling the wrong pixels
there - 15,054 of 51,260 differ, which looks precisely like a reduction bug
and is not one. `sw_blit_row` and its tables are the SAME code on both 1bpp
adapters, so CGA answers the question this row asks; a Hercules arm wants
the VRAM route rather than the rendered one, and would be answering a
different question (39.3's banking) at the same time.
"""
import argparse
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp                 # noqa: E402

if os.environ.get("NOPLANE"):
    os88sym.default_defines("NOPLANE")      # the knob kernel is a DIFFERENT
                                            # kernel and every address here
                                            # has to be asked for with it
S = os88sym.linear


def gif_pixels(path):
    """(w, h, one palette index per pixel) for a GIF87a with a global table.

    A reader rather than a dependency: tools/os88logo.py WRITES this file and
    the point of the row is to check the guest against it independently.
    """
    d = open(path, "rb").read()
    if d[:6] not in (b"GIF87a", b"GIF89a"):
        sys.exit("blitpair: %s is not a GIF" % path)
    flags = d[10]
    p = 13 + 3 * (2 << (flags & 7))
    while d[p] != 0x2C:                     # skip extension blocks
        p += 2
        while d[p]:
            p += d[p] + 1
        p += 1
    iw, ih = struct.unpack("<HH", d[p + 5:p + 9])
    p += 10
    mcs = d[p]
    p += 1
    data = bytearray()
    while d[p]:
        n = d[p]
        data += d[p + 1:p + 1 + n]
        p += 1 + n

    clear, end = 1 << mcs, (1 << mcs) + 1
    dic = {i: bytes([i]) for i in range(clear)}
    nxt, size = end + 1, mcs + 1
    out, prev, bit, total = bytearray(), None, 0, len(data) * 8
    while bit + size <= total:
        by, off = bit >> 3, bit & 7
        v = (data[by]
             | (data[by + 1] << 8 if by + 1 < len(data) else 0)
             | (data[by + 2] << 16 if by + 2 < len(data) else 0))
        code = (v >> off) & ((1 << size) - 1)
        bit += size
        if code == clear:
            dic = {i: bytes([i]) for i in range(clear)}
            nxt, size, prev = end + 1, mcs + 1, None
            continue
        if code == end:
            break
        ent = dic[code] if code in dic else prev + prev[:1]
        out += ent
        if prev is not None:
            dic[nxt] = prev + ent[:1]
            nxt += 1
            if nxt == (1 << size) and size < 12:
                size += 1
        prev = ent
    return iw, ih, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/blitpair.img")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--gif", default="build/OS8088.GIF")
    a = ap.parse_args()

    if a.apps == "/tmp/blitpair.img" and not os.path.exists(a.apps):
        subprocess.check_call(
            [sys.executable, "tools/os88disk.py", "-o", a.apps, "--size",
             "360", "APPS:build/paint.o88", "MEDIA:" + a.gif])

    iw, ih, px = gif_pixels(a.gif)
    black = sum(1 for v in px if v == 1)
    print("   %s: %dx%d, %d black of %d" % (a.gif, iw, ih, black, iw * ih))
    # THE SUBJECT HAS TO HAVE INK IN IT, and in both colours. Every check
    # below is "the canvas equals the file", which a BLANK canvas satisfies
    # perfectly against a blank file - so the one thing this comparison cannot
    # tell you on its own is that anything was drawn at all. A fifth of the
    # picture is the floor rather than a measurement: OS8088.GIF's ground is
    # SPEC.md 63's 50% dither, so it is near half, and a --gif that fell below
    # a fifth either way would be too flat to prove a decoder either.
    floor = iw * ih // 5
    if black < floor or (iw * ih - black) < floor:
        sys.exit("blitpair: %s is %d black of %d - too flat to compare. An "
                 "all-one-colour picture is matched by an all-one-colour "
                 "canvas, including one nothing ever drew"
                 % (a.gif, black, iw * ih))

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

        # The geometry is ASKED rather than computed: the canvas x depends on
        # the window template and the adapter, and a rect this test guessed
        # would be a second opinion about where Paint put its picture.
        m.bp_exec("gfx_blit4")
        mo.dblclick(rx, ry)
        # The CANVAS, not a palette swatch or an icon: the first blit as wide
        # as the picture. Which call that is depends on the adapter and on
        # whether Paint is holding four planes, so it is asked rather than
        # assumed.
        r = None
        for _ in range(200):
            if not m.wait_stop(limit=300.0):
                break
            r = m.regs()
            if r["cx"] >= iw:
                break
            m.bp_exec("gfx_blit4")
            m.run()
            r = None
        if r is None:
            sys.exit("blitpair: Paint never blitted a canvas through gfx_blit4")
        x, y, bw, bh = r["ax"], r["bx"], r["cx"], r["dx"]
        print("   blit x=%d y=%d w=%d h=%d" % (x, y, bw, bh))
        m.bp_exec()
        m.run()
        time.sleep(6)
        mo.to(4, 4)
        os88marty.settle(m)
        w, h, fb = m.fbuf(card=0)

    if (bw, bh) != (iw, ih):
        sys.exit("blitpair: the canvas is %dx%d and the picture %dx%d - "
                 "Paint cropped it, so this row is comparing two things"
                 % (bw, bh, iw, ih))
    bad = [(c, rw)
           for rw in range(bh) for c in range(bw)
           if (fb[((y + rw) * w + x + c) * 3] < 128) != (px[rw * iw + c] == 1)]
    print("   canvas %dx%d on %dx%d: %d pixels differ from the file"
          % (bw, bh, w, h, len(bad)))
    if bad:
        print("   row 0 want %s" % "".join("1" if px[c] == 1 else "0"
                                           for c in range(32)))
        print("   row 0 got  %s" % "".join(
            "1" if fb[(y * w + x + c) * 3] < 128 else "0" for c in range(32)))
        xs = [b[0] for b in bad]
        ys = [b[1] for b in bad]
        print("   box x %d..%d  y %d..%d, first 12: %r"
              % (min(xs), max(xs), min(ys), max(ys), bad[:12]))
        sys.exit("blitpair: FAILED")
    print("blitpair: the canvas IS the picture, %d pixels" % (iw * ih))
    return 0


if __name__ == "__main__":
    sys.exit(main())

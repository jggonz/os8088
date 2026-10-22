#!/usr/bin/env python3
"""Gorillas' field on the packed desktop: a blue sky and solid buildings
(SPEC.md 120.5, the package rule for DH).
    make vesatest && python3 tests/vesagor.py [--png out.png]

Boots build/vesafsx.img - tests/vesafsx.py's disk, whose \\AUTOSTRT link
opens Gorillas on the 1024x768x16 desktop - and plays the setup through:
seven Enters keep every question's default and an eighth takes "P = Play
game", which draws the skyline. Then it reads the field off a screendump.

WHAT IT ASSERTS: the window's blue (CBLUE) region is the size of a field
and not of a three-line HUD; most of its rows are sky, edge to edge; and
in its lower third a building colour runs solid for dozens of pixels.

THE RED IT WAS WRITTEN AGAINST: OSAPI_WM_DISPLAY answers DH = 16 on this
display, and Gorillas' tables are keyed 1 / 2 / 4 - with no "4" to match,
its band composer packed the scene as 2-bit CGA rows and handed them to
OSAPI_GFX_BLIT4 as 4-bit ones: a white sky under the HUD and every building
a comb of one-pixel columns (10 sky rows, no run past 14 px, where the VGA
desktop draws 97 and 68). Gorillas folds kind 4 to VGA at depth 4 now.
QEMU by name, vesa's reason.
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88qemu                                             # noqa: E402
import shot                                                 # noqa: E402
import vesa                                                 # noqa: E402
import vesafsx                                              # noqa: E402
from os88geom import VID_VESA                               # noqa: E402

BLUE = (0, 0, 170)                  # CBLUE as QEMU lands the 565 word
WHITE = (255, 255, 255)


def q(c):
    """A QEMU 565 -> 888 channel, back to the EGA's four levels."""
    return min((0, 85, 170, 255), key=lambda v: abs(v - c))


def ega(pix, w, x, y):
    return tuple(q(c) for c in vesa.px(pix, w, x, y))


def field(pix, sw, sh):
    """(box w, box h, sky rows, longest solid building run) of the window's
    blue region below the menu bar - the HUD band and, when it is drawn, the
    sky above the skyline."""
    xs, ys = [], []
    for y in range(24, sh, 2):
        for x in range(0, sw, 2):
            if ega(pix, sw, x, y) == BLUE:
                xs.append(x)
                ys.append(y)
    if not xs:
        return 0, 0, 0, 0
    x1, x2, y1, y2 = min(xs), max(xs), min(ys), max(ys)
    bw, bh = x2 - x1 + 1, y2 - y1 + 1
    sky = 0
    for y in range(y1, y2 + 1):
        n = sum(1 for x in range(x1, x2 + 1) if ega(pix, sw, x, y) == BLUE)
        if n >= 0.9 * bw:
            sky += 1
    best = 0
    for y in range(y1 + 2 * bh // 3, y2 + 1):
        run, last = 0, None
        for x in range(x1, x2 + 1):
            c = ega(pix, sw, x, y)
            if c == last and c not in (BLUE, WHITE):
                run += 1
                best = max(best, run)
            else:
                run, last = 1, c
    return bw, bh, sky, best


def main():
    out_png = None
    img = "build/vesafsx.img"
    args = sys.argv[1:]
    while args:
        a = args.pop(0)
        if a == "--png":
            out_png = args.pop(0)
        elif a == "--img":
            img = args.pop(0)
    vesa.kill_stale()
    vesafsx.launch(img)
    fails = []
    try:
        q_ = heapmap.Qmp(vesa.SOCK)
        t0 = time.time()
        while vesa.byte(q_, "vid_kind") != VID_VESA:
            if time.time() - t0 > 60:
                fails.append("the desktop never reached VID_VESA")
                break
            time.sleep(0.5)
        os88qemu.pace(q_, 12.0)             # the autostart's load and first paint
        for _ in range(7):                  # the seven questions, at their defaults
            q_.hmp("sendkey ret")
            os88qemu.pace(q_, 1.5)
        q_.hmp("sendkey ret")               # ...and Play, which draws the skyline
        os88qemu.pace(q_, 4.0)
        sw, sh, pix = vesa.screendump(q_, os.path.join(ROOT, "build", "vesa-shot"))
        if out_png:
            shot.png(out_png, sw, sh, pix)
        if (sw, sh) != (vesa.WANT_W, vesa.WANT_H):
            fails.append("screen is %dx%d, want %dx%d" % (sw, sh, vesa.WANT_W, vesa.WANT_H))
        bw, bh, sky, run = field(pix, sw, sh)
        print("vesagor: blue region %dx%d, %d sky rows, longest solid building run %d px"
              % (bw, bh, sky, run))
        if bw < 400 or bh < 150:
            fails.append("the window's blue region is %dx%d: a HUD band and no sky "
                         "(the field wants hundreds of rows of CBLUE)" % (bw, bh))
        if sky < 40:
            fails.append("%d rows of sky edge to edge, want 40 or more: the sky is "
                         "not CBLUE" % sky)
        if run < 32:
            fails.append("the longest solid run of a building colour in the lower "
                         "third is %d px, want 32 or more: the buildings are a comb "
                         "(2-bit rows blitted as 4-bit)" % run)
    finally:
        vesa.kill_stale()
    if fails:
        print("vesagor: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("vesagor: ok - Gorillas' sky and skyline on the 1024x768x16 desktop")
    return 0


if __name__ == "__main__":
    sys.exit(main())

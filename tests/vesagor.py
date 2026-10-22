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
import os88sym                                              # noqa: E402
from pxsyms import instance                                 # noqa: E402
import subprocess                                           # noqa: E402
import tempfile                                             # noqa: E402

BLUE = (0, 0, 170)                  # CBLUE as QEMU lands the 565 word


def gor_syms():
    """{name: offset} and the image bytes of apps/gorillas/gorillas.asm as it
    is (tests/pxsyms.py's shape, for this package)."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "g.asm"), os.path.join(d, "g.map")
        open(cp, "w").write(open(os.path.join(ROOT, "apps/gorillas/gorillas.asm")).read()
                            + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/", "-I", "apps/gorillas/",
                        "-o", os.path.join(d, "g.bin"), cp], cwd=ROOT, check=True)
        out = {}
        for L in open(mp):
            f = L.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[1], 16)
        return out, open(os.path.join(d, "g.bin"), "rb").read()
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
        # --- Gorillas' instance, and its setup read off its own state ------
        # (not off the glass: the caret blinks, so a panel is never still,
        # and the splash and the first question are both a blue page - the
        # fixed-pace first cut typed an Enter into the splash and the eighth
        # landed on the choice page)
        syms, image = gor_syms()
        os88qemu.acted(q_, lambda: instance(q_, os88sym.linear, image), 120.0,
                       "Gorillas' instance")
        seg = instance(q_, os88sym.linear, image)
        if not seg:
            fails.append("no Gorillas instance: the autostart link did not open it")
            raise SystemExit(0)
        B = lambda n: q_.read(seg * 16 + syms[n], 1)[0]            # noqa: E731
        os88qemu.acted(q_, lambda: B("gr_state") == 5, 60.0, "the setup page")
        for i in range(7):                  # the seven questions, at their defaults
            q_.hmp("sendkey ret")
            os88qemu.acted(q_, lambda: B("gr_setupfield") > i or B("gr_state") == 6,
                           15.0, "question %d answered" % (i + 1))
        if B("gr_state") != 6:
            fails.append("after seven Enters the state is %d, not the choice page (6)"
                         % B("gr_state"))
        q_.hmp("sendkey ret")               # ...and Play, which draws the skyline
        os88qemu.acted(q_, lambda: B("gr_state") < 4, 15.0, "the game")
        # ...band by band, under a loaded host for longer than one pace: read
        # the field until the buildings are there, or 30 s have gone, and
        # judge what is on the glass then (the comb never satisfies this)
        t0 = time.time()
        while True:
            os88qemu.pace(q_, 3.0)
            sw, sh, pix = vesa.screendump(q_, os.path.join(ROOT, "build", "vesa-shot"))
            bw, bh, sky, run = field(pix, sw, sh)
            if run >= 32 or time.time() - t0 > 30:
                break
        if out_png:
            shot.png(out_png, sw, sh, pix)
        if (sw, sh) != (vesa.WANT_W, vesa.WANT_H):
            fails.append("screen is %dx%d, want %dx%d" % (sw, sh, vesa.WANT_W, vesa.WANT_H))
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
    except SystemExit:
        pass
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

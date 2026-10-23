#!/usr/bin/env python3
"""A package's 4-bit blit on the packed display, pixel for pixel (SPEC.md 120.3).

    make vesatest && python3 tests/vesablit4.py [--png out.png] [--img disk]

Boots build/vesablit4.img - tests/vesa.py's disk plus \\AUTOSTRT\\WBALL.LNK, a
link to WIREBALL.O88 on the apps disk - so the machine boots straight into
WireBall's front page on the 1024x768x16 desktop. Its title art is one
OSAPI_GFX_BLIT4 of a 416x104 block (apps/wireball/art/wbfront.inc, generated
and checked in), the largest 4-bit blit a shipped package draws, and it goes
through the driver's VX_BLIT4 body with the kernel's gfx_blit_* block, the
caller's ES:SI and its BP stride.

WHAT IT ASSERTS: the art is on the glass EXACTLY - every one of its 43,264
pixels the EGA colour its nibble names, as the card renders 565 - wherever
the window put it (the block is found by its own rows, not by a coordinate).
The driver as first shipped loaded the stride into AX on top of the block's
x, so the row began 96 pixels left, the right edge clipped it to 320, and
the source phase read 16 KB past the block: the art drew as a window of
noise in palette colours, which the owner saw on the 86Box 486 before any
row did. QEMU by name, vesa's reason.
"""
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88fixture                                          # noqa: E402
import os88build as _B                                      # noqa: E402
import os88qemu                                             # noqa: E402
import shot                                                 # noqa: E402
import vesa                                                 # noqa: E402
from os88geom import VID_VESA                               # noqa: E402

ART = os.path.join(ROOT, "apps", "wireball", "art", "wbfront.inc")
EGA16 = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
         (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
         (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
         (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]


def q565(c):
    """An EGA entry as the 565 word's channels, back at 8 bits the way a
    565 -> 888 expansion lands them: the high bits replicated."""
    r, g, b = c[0] >> 3, c[1] >> 2, c[2] >> 3
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def art():
    """The checked-in art: (w, h, rows of colour indexes)."""
    w = h = None
    rows = []
    data = []
    take = False
    for line in open(ART):
        m = re.match(r"^WB_ART_([WH])\s+equ\s+(\d+)", line)
        if m:
            if m.group(1) == "W":
                w = int(m.group(2))
            else:
                h = int(m.group(2))
            continue
        if line.startswith("wb_artcolor:"):
            take = True
            continue
        if take and re.match(r"^\w+:", line):
            break
        if take:
            m = re.match(r"^\s*db\s+(.*)", line)
            if m:
                data += [int(t, 0) for t in m.group(1).split(",")]
    stride = w // 2
    for y in range(h):
        row = []
        for x in range(w):
            b = data[y * stride + x // 2]
            row.append((b >> 4) if x % 2 == 0 else (b & 15))
        rows.append(row)
    return w, h, rows


def launch(img):
    os88fixture.need(img, "build/apps.img")
    em = "qemu" + "-system-i386"
    subprocess.run(
        em + " -machine pc,vmport=off -vga std"
        " -drive file=%s,format=raw,if=floppy -boot a"
        " -drive file=%s,format=raw,if=floppy,index=1"
        " -serial msmouse"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (_B.at(img), _B.at("build/apps.img"), vesa.SOCK, vesa.PIDFILE),
        cwd=ROOT, shell=True, check=True)
    os88qemu.own(vesa.PIDFILE, vesa.SOCK)


def main():
    out_png = None
    img = "build/vesablit4.img"
    args = sys.argv[1:]
    while args:
        a = args.pop(0)
        if a == "--png":
            out_png = args.pop(0)
        elif a == "--img":
            img = args.pop(0)
    w, h, rows = art()
    want = [[q565(EGA16[i]) for i in row] for row in rows]
    vesa.kill_stale()
    launch(img)
    fails = []
    try:
        q = heapmap.Qmp(vesa.SOCK)
        t0 = time.time()
        while vesa.byte(q, "vid_kind") != VID_VESA:
            if time.time() - t0 > 60:
                fails.append("the desktop never reached VID_VESA")
                break
            time.sleep(0.5)
        os88qemu.pace(q, 12.0)              # the autostart's load and first paint
        sw, sh, pix = vesa.screendump(q, os.path.join(ROOT, "build", "vesa-shot"))
        if out_png:
            shot.png(out_png, sw, sh, pix)
        if (sw, sh) != (vesa.WANT_W, vesa.WANT_H):
            fails.append("screen is %dx%d, want %dx%d" % (sw, sh, vesa.WANT_W, vesa.WANT_H))
        else:
            # find the block by a distinctive row of it: the first row with
            # colours past black and white, matched whole
            key = next(y for y in range(h)
                       if any(i not in (0, 15) for i in rows[y]))
            keyrow = want[key]
            found = None
            for y in range(0, sh - h + 1):
                for x in range(0, sw - w + 1):
                    if vesa.px(pix, sw, x, y) != keyrow[0]:
                        continue
                    if all(vesa.px(pix, sw, x + i, y) == keyrow[i]
                           for i in range(0, w, 7)):
                        found = (x, y - key)
                        break
                if found:
                    break
            if not found:
                fails.append("WireBall's title art is nowhere on the glass "
                             "whole: its row %d matches no screen row" % key)
            else:
                x0, y0 = found
                bad = 0
                first = None
                for y in range(h):
                    for x in range(w):
                        if vesa.px(pix, sw, x0 + x, y0 + y) != want[y][x]:
                            bad += 1
                            if first is None:
                                first = (x, y)
                if bad:
                    fails.append("the art at (%d,%d): %d of %d pixels differ "
                                 "from the block's nibbles (first at %r)"
                                 % (x0, y0, bad, w * h, first))
                else:
                    print("vesablit4: the art at (%d,%d), %dx%d exact" % (x0, y0, w, h))
    finally:
        vesa.kill_stale()
    if fails:
        print("vesablit4: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("vesablit4: ok - a 416x104 OSAPI_GFX_BLIT4 pixel for pixel on VID_VESA")
    return 0


if __name__ == "__main__":
    sys.exit(main())

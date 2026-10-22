#!/usr/bin/env python3
"""A package's preferred window size on the packed display (SPEC.md 120.5, 11.100.1).

    make vesatest && python3 tests/vesapref.py [--png out.png] [--img disk]

Boots build/vesapref.img - tests/vesa.py's disk plus \\AUTOSTRT\\SOLIT.LNK, a
link to SOLITAIR.O88 on the apps disk - so the machine boots into Solitaire
on the 1024x768x16 desktop. Solitaire declares OS88_PREFER's three-entry
table, and the kernel takes VGA's row for every kind past CGA (EGA, VESA).

WHAT IT ASSERTS: the green table on the glass is as wide as the package's
own SOL_PW_B says, read out of apps/solitaire/solitaire.asm. The kernel as
first shipped folded only EGA, so kind 4 read sixteen bytes past the table
and Solitaire opened 110 pixels wide (nine packages, each at whatever its
image held there). QEMU by name, vesa's reason.
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

SRC = os.path.join(ROOT, "apps", "solitaire", "solitaire.asm")
GREEN = (0, 170, 0)                 # CGREEN as QEMU lands the 565 word
CBLUE = (0, 0, 170)                 # ...and CBLUE, the card back's


def ega(c):
    """A QEMU 565 -> 888 channel, back to the EGA's four levels."""
    return min((0, 85, 170, 255), key=lambda v: abs(v - c))


def equ(name, src):
    """A SOL_* constant, its equ chain resolved."""
    consts = {}
    for line in open(src):
        m = re.match(r"^(SOL_\w+)\s+equ\s+([^;]+)", line)
        if m:
            consts[m.group(1)] = m.group(2).strip()

    def val(n):
        e = consts[n]
        for k in sorted(consts, key=len, reverse=True):
            if k in e:
                e = e.replace(k, "(%d)" % val(k))
        return int(eval(e))
    return val(name)


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
    img = "build/vesapref.img"
    args = sys.argv[1:]
    while args:
        a = args.pop(0)
        if a == "--png":
            out_png = args.pop(0)
        elif a == "--img":
            img = args.pop(0)
    want_w = equ("SOL_PW_B", SRC)
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
        os88qemu.pace(q, 12.0)
        sw, sh, pix = vesa.screendump(q, os.path.join(ROOT, "build", "vesa-shot"))
        if out_png:
            shot.png(out_png, sw, sh, pix)
        # the widest run of table green on any row: the content width
        best = 0
        for y in range(24, sh - 40):
            run = 0
            for x in range(sw):
                if vesa.px(pix, sw, x, y) == GREEN:
                    run += 1
                    best = max(best, run)
                else:
                    run = 0
        # the frame width SOL_PW_B holds the content plus its 1px frame
        # either side; the table's green may stop a pixel or two short of
        # the content edge on either side
        if not (want_w - 6 <= best <= want_w):
            fails.append("Solitaire's table is %d pixels wide; SOL_PW_B says the "
                         "window is %d (the VGA row of its preference table)"
                         % (best, want_w))
        else:
            print("vesapref: Solitaire's table %d wide, the window's preferred %d"
                  % (best, want_w))
        # ...and the stock's card back is CBLUE: sol_backbuf is filled with
        # CBLUE where [sol_bpp] >= 4 and the dither-class CLBLUE on a 1bpp
        # card. Solitaire read DH = 16 as "not 4" and drew the 1bpp back
        # (SPEC.md 120.5, the package rule for DH) - and its red pips black
        # (quantised: the 565 word's blue lands as 173, its green as 170)
        blue = sum(1 for y in range(24, sh - 40) for x in range(sw)
                   if tuple(ega(c) for c in vesa.px(pix, sw, x, y)) == CBLUE)
        if blue < 200:
            fails.append("%d CBLUE pixels on the desktop, want a card back's "
                         "worth (hundreds): Solitaire took its 1bpp arm on a "
                         "depth of 16" % blue)
        else:
            print("vesapref: %d CBLUE pixels - the card back drawn on the colour arm"
                  % blue)
    finally:
        vesa.kill_stale()
    if fails:
        print("vesapref: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("vesapref: ok - OS88_PREFER's VGA row taken on VID_VESA")
    return 0


if __name__ == "__main__":
    sys.exit(main())

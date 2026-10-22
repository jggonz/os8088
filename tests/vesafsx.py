#!/usr/bin/env python3
"""A fullscreen bracket from the packed desktop, and back (SPEC.md 120.5, 53).

    make vesatest && python3 tests/vesafsx.py [--png out.png] [--img disk]

Boots build/vesafsx.img - tests/vesa.py's disk plus \\AUTOSTRT\\GORIL.LNK, a
link to GORILLAS.O88 on the apps disk - so the machine boots into Gorillas'
setup window on the 1024x768x16 desktop. Alt+Enter takes Gorillas into its
bracket from any state; it asks OSAPI_FSX_CAPS and reads DL - VID_VGA takes
mode 12h through OSAPI_FSX_MODE, VID_CGA 320x200, and anything else the
NATIVE surface, drawn planar. Escape leaves the bracket, and fsx_leave puts
the VESA mode back through vid_setmode's arm.

WHAT IT ASSERTS: the desktop is 1024x768 with Gorillas up; after Alt+Enter
the screen is 640x480 (mode 12h: the VGA arm was taken, so DL said VID_VGA);
after Escape the screen is 1024x768 again and the desktop's left margin is
the checkerboard (the VESA mode came back and the desktop was repainted).
The kernel as first shipped answered kind 4, Gorillas took the native
surface and drew planar onto the packed desktop - the owner's report, and
this row's red: the screen stays 1024x768. QEMU by name, vesa's reason.
"""
import os
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


def margin_checker(pix, sw, sh):
    bad = 0
    for y in range(24, sh - 40, 2):
        if not vesa.checker_at(pix, sw, 2, y):
            bad += 1
    return bad


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
            shot.png(out_png.replace(".png", "-desk.png"), sw, sh, pix)
        if (sw, sh) != (vesa.WANT_W, vesa.WANT_H):
            fails.append("before: screen is %dx%d, want %dx%d" % (sw, sh, vesa.WANT_W, vesa.WANT_H))
        q.hmp("sendkey alt-ret")            # Gorillas: the bracket, any state
        os88qemu.pace(q, 4.0)
        sw2, sh2, pix2 = vesa.screendump(q, os.path.join(ROOT, "build", "vesa-shot"))
        if out_png:
            shot.png(out_png.replace(".png", "-full.png"), sw2, sh2, pix2)
        if (sw2, sh2) != (640, 480):
            fails.append("after Alt+Enter: the screen is %dx%d, want 640x480 - "
                         "Gorillas did not take the VGA arm (OSAPI_FSX_CAPS's "
                         "DL was not VID_VGA, so it drew onto the packed desktop)"
                         % (sw2, sh2))
        q.hmp("sendkey esc")
        os88qemu.pace(q, 5.0)
        sw3, sh3, pix3 = vesa.screendump(q, os.path.join(ROOT, "build", "vesa-shot"))
        if out_png:
            shot.png(out_png.replace(".png", "-back.png"), sw3, sh3, pix3)
        if (sw3, sh3) != (vesa.WANT_W, vesa.WANT_H):
            fails.append("after Escape: screen is %dx%d, want the VESA mode back"
                         % (sw3, sh3))
        else:
            bad = margin_checker(pix3, sw3, sh3)
            if bad:
                fails.append("after Escape: the desktop's left margin is not the "
                             "checkerboard on %d row pairs (the desktop did not "
                             "come back whole)" % bad)
            if vesa.byte(q, "vid_kind") != VID_VESA:
                fails.append("after Escape: vid_kind is not VID_VESA")
    finally:
        vesa.kill_stale()
    if fails:
        print("vesafsx: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("vesafsx: ok - a BIOS-mode bracket from the 1024x768x16 desktop (%dx%d) and back"
          % (sw2, sh2))
    return 0


if __name__ == "__main__":
    sys.exit(main())

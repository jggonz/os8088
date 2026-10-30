#!/usr/bin/env python3
"""Shut Down on a VESA desktop draws its "turn off" screen (SPEC.md 120.4, 12.3.2).

    make vesatest && python3 tests/vesashut.py [--png out.png]

CTRL.DRV's Shut Down detaches every driver (drv_shutdown_x) before it draws
"You can now turn off your computer." - and on a VESA desktop that took the
renderer away with [vid_kind] still VID_VESA, so vid_setmode and the text
reached the null entry (SPEC.md 120.3.2) and the card stayed on the frozen
desktop in its VBE mode with nothing drawn. Fixed at the owner's word in
kernel size pass 14: the shutdown puts the kind back to VID_VGA and applies
its geometry before the mode set, so the screen is the card's own mode 12h
and the kernel draws on it. Every other kind takes the arm's not-taken jump.

WHAT IT ASSERTS, after System > Shut Down... and Enter on build/vesa.img:

  the screen      640 x 480 (mode 12h), not the desktop's 1024 x 768
  vid_kind        0 (VID_VGA), vid_planes 4
  the sentence    white pixels in the middle band, the rest of it black

Broken on purpose (the arm taken out of kernel/shutdown.inc): the screen
stays 1024 x 768 and the row fails on the first line.

QEMU by name on CLAUDE.md's closed list: no other emulator here has a VBE.
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
import os88sym                                              # noqa: E402
import os88qemu                                             # noqa: E402
from os88geom import VID_VESA                               # noqa: E402
import vesa                                                 # noqa: E402

# THIS PROCESS'S pair (os88qemu.paths), never a fixed file under build/
SOCK, PIDFILE = os88qemu.paths("vesashut")
MENU_LOGO = (8, 5)              # the System menu's cell on the bar
MENU_SHUT = (50, 124)           # its last item, "Shut Down..." (1024x768)


def launch():
    if os88qemu.MEGA:       # kern_mega on Bochs's VBE: tests/vesa.py's launch
        return vesa.launch(sock=SOCK, pidfile=PIDFILE)
    os88fixture.need("build/vesa.img", "build/apps.img")
    for f in (SOCK, PIDFILE):
        if os.path.exists(f):
            os.unlink(f)
    em = "qemu" + "-system-i386"     # never whole on a command line
    os88qemu.start(
        em + " -machine pc,vmport=off -vga std"
        # SNAPSHOT: the SHARED images, never read back here. Booted
        # WRITABLE they locked every other row out of them - QEMU refuses
        # even a snapshot=on open of a file another QEMU writes - and the
        # Shut Down this row presses WRITES to them (docs/plans/
        # SOAK-PARALLEL.md 17.10)
        " -drive file=%s,format=raw,if=floppy,snapshot=on -boot a"
        " -drive file=%s,format=raw,if=floppy,index=1,snapshot=on"
        " -serial msmouse"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (_B.at("build/vesa.img"), _B.at("build/apps.img"), SOCK, PIDFILE),
        cwd=ROOT, shell=True, check=True)
    os88qemu.own(PIDFILE, SOCK)


def mouse(*args):
    subprocess.run(["python3", "tools/mouse.py", "--screen",
                    "%dx%d" % (vesa.WANT_W, vesa.WANT_H), SOCK] + [str(a) for a in args], cwd=ROOT, check=True)


def main():
    out_png = None
    if "--png" in sys.argv:
        out_png = sys.argv[sys.argv.index("--png") + 1]
    fails = []
    launch()
    q = heapmap.Qmp(SOCK)
    try:
        os88qemu.acted(q, lambda: vesa.byte(q, "vid_kind") == VID_VESA, 120.0,
                       "vid_kind = VID_VESA")
        os88qemu.pace(q, 8.0)
        mouse("to", *MENU_LOGO)
        mouse("down")
        os88qemu.pace(q, 1.0)
        mouse("to", *MENU_SHUT)
        os88qemu.pace(q, 0.5)
        mouse("up")
        os88qemu.pace(q, 2.0)
        subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey ret"],
                       cwd=ROOT, check=True)
        os88qemu.acted(q, lambda: vesa.byte(q, "vid_kind") != VID_VESA, 30.0,
                       "the shutdown leaving VID_VESA")
        os88qemu.pace(q, 3.0)
        sw, sh, pix = vesa.screendump(q, os.path.join(ROOT, "build",
                                                      "vesashut-shot"))
        if out_png:
            import shot
            shot.png(out_png, sw, sh, pix)
        if (sw, sh) != (640, 480):
            fails.append("the turn-off screen is %d x %d, want 640 x 480 - "
                         "the card stayed in its VBE mode" % (sw, sh))
        kind, planes = vesa.byte(q, "vid_kind"), vesa.byte(q, "vid_planes")
        if (kind, planes) != (0, 4):
            fails.append("vid_kind %d vid_planes %d, want 0 and 4" % (kind, planes))
        if (sw, sh) == (640, 480):
            band = [vesa.px(pix, sw, x, y) for y in range(200, 300)
                    for x in range(0, sw)]
            white = sum(1 for p in band if p == (255, 255, 255))
            other = sum(1 for p in band if p not in ((255, 255, 255), (0, 0, 0)))
            if white < 150:
                fails.append("%d white pixels in the middle band, want the "
                             "sentence (150+)" % white)
            if other > len(band) // 4:
                fails.append("%d of %d middle-band pixels are neither black nor "
                             "white - the desktop is still there" % (other, len(band)))
            print("vesashut: %dx%d, %d white pixels in the sentence's band"
                  % (sw, sh, white))
    finally:
        try:
            q.hmp("quit")
        except Exception:
            pass
    if fails:
        print("vesashut: FAIL")
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print("vesashut: ok - Shut Down on a VESA desktop draws its screen in mode 12h")


if __name__ == "__main__":
    main()

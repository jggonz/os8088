#!/usr/bin/env python3
"""The packed-pixel display comes up and draws (SPEC.md 119).

    make vesatest && python3 tests/vesa.py [--png out.png]

QEMU's standard VGA carries a VBE 2.0+ BIOS, so VESA.DRV's probe finds
1024x768 at 16bpp (mode 0117h) exactly as it would on a real card, and the
desktop switches onto it before the first paint. That makes this the one CI
gate the feature gets: MartyPC and the 86Box XTs have no VBE at all, so the
same disk on them is the negative control (the row refuses, the desktop stays
on the VGA) - which tests/vesanone.py will assert when it exists.

QEMU by name on CLAUDE.md's closed list. Nothing here is a time: the machine
under it is not a 4.77 MHz 8088, and the pixels are what is asserted.

WHAT IT ASSERTS, after [vid_kind] settles on VID_VESA (4):

  vid_kind    4            vx_boot_x attached the image and bvs_switch took it
  vid_ext     1            every planar body far-calls the driver (119.3)
  vid_cw/ch   1024 x 768   the mode Auto chose on this card (119.2.3)
  vid_planes  16           the DEPTH, which is what the save-under sizers
                           multiply by (119.4)
  vesa_row    DRVR_SEG != 0, DRVR_ERR == 0

then a 1024 x 768 screendump: the menu bar's band at the top is not black
(the chrome was painted through the driver's fills and runs), the desktop
below it carries the 50% dither (gfx_fill_gray's body: a 2x2 block holding
both black and white), and the dock's band at the bottom is not black.
"""
import os
import signal
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
import shot                                                 # noqa: E402

SOCK = os.path.join(ROOT, "build", "vesa.sock")
PIDFILE = os.path.join(ROOT, "build", "vesa.pid")

VID_VESA = 4
WANT_W, WANT_H, WANT_BPP = 1024, 768, 16


def kill_stale():
    if os.path.exists(PIDFILE):
        try:
            os.kill(int(open(PIDFILE).read().strip()), signal.SIGTERM)
            time.sleep(1)
        except Exception:
            pass
    for f in (SOCK, PIDFILE):
        if os.path.exists(f):
            os.unlink(f)


def launch():
    """The standard VGA (QEMU's default, spelled out) and the gate disk."""
    os88fixture.need("build/vesa.img", "build/apps.img")
    em = "qemu" + "-system-i386"     # never whole on a command line: kill_stale
    subprocess.run(
        em + " -machine pc,vmport=off -vga std"
        " -drive file=%s,format=raw,if=floppy -boot a"
        " -drive file=%s,format=raw,if=floppy,index=1"
        " -serial msmouse"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (_B.at("build/vesa.img"), _B.at("build/apps.img"), SOCK, PIDFILE),
        cwd=ROOT, shell=True, check=True)
    os88qemu.own(PIDFILE, SOCK)


def byte(q, sym, off=0):
    return q.read(os88sym.linear(sym) + off, 1)[0]


def word(q, sym, off=0):
    b = q.read(os88sym.linear(sym) + off, 2)
    return b[0] | (b[1] << 8)


def screendump(q, path):
    ppm = os.path.abspath(path + ".ppm")
    q.hmp('screendump "%s"' % ppm)
    w, h, pix = shot.read_ppm(ppm)
    os.unlink(ppm)
    return w, h, pix


def px(pix, w, x, y):
    i = (y * w + x) * 3
    return pix[i], pix[i + 1], pix[i + 2]


def nonblack_share(pix, w, x0, y0, x1, y1):
    n = tot = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            tot += 1
            if px(pix, w, x, y) != (0, 0, 0):
                n += 1
    return n / max(1, tot)


def main():
    out_png = None
    if "--png" in sys.argv:
        out_png = sys.argv[sys.argv.index("--png") + 1]
    kill_stale()
    fails = []
    try:
        launch()
        q = heapmap.Qmp(SOCK)
        for _ in range(200):
            try:
                q.hmp("info status")
                break
            except OSError:
                time.sleep(0.1)

        # vx_boot_x runs in kmain after kmain_o returns; [vid_kind] moving to
        # VID_VESA is bvs_switch's store and the signal the whole chain ran
        os88qemu.acted(q, lambda: byte(q, "vid_kind") == VID_VESA, 120.0,
                       "vid_kind = VID_VESA")
        kind = byte(q, "vid_kind")
        if kind != VID_VESA:
            seg = word(q, "vesa_row", 2)
            err = byte(q, "vesa_row", 14)
            fails.append("vid_kind %d, want %d: the desktop did not switch "
                         "onto the VBE (vesa_row DRVR_SEG %04X, DRVR_ERR %d)"
                         % (kind, VID_VESA, seg, err))
            raise SystemExit("\n".join(fails))

        ext = byte(q, "vid_ext")
        if ext != 1:
            fails.append("vid_ext %d, want 1" % ext)
        w, h = word(q, "vid_cw"), word(q, "vid_ch")
        if (w, h) != (WANT_W, WANT_H):
            fails.append("vid_cw x vid_ch %d x %d, want %d x %d (Auto on QEMU's "
                         "standard VGA, SPEC.md 119.2.3)" % (w, h, WANT_W, WANT_H))
        bpp = byte(q, "vid_planes")
        if bpp != WANT_BPP:
            fails.append("vid_planes %d, want %d: the depth is the plane count "
                         "on this kind (SPEC.md 119.4)" % (bpp, WANT_BPP))
        if word(q, "vesa_row", 2) == 0:
            fails.append("vesa_row DRVR_SEG is 0: the image is not loaded")
        err = byte(q, "vesa_row", 14)
        if err:
            fails.append("vesa_row DRVR_ERR %d, want 0" % err)

        # --- the desktop's first paint, on the glass -------------------------
        os88qemu.pace(q, 3.0)       # let wm_paint_all and the dock finish
        sw, sh, pix = screendump(q, os.path.join(ROOT, "build", "vesa-shot"))
        if out_png:
            shot.png(out_png, sw, sh, pix)
            print("wrote", out_png)
        if (sw, sh) != (WANT_W, WANT_H):
            fails.append("screendump is %d x %d, want %d x %d" % (sw, sh,
                                                                   WANT_W, WANT_H))
        else:
            bar = nonblack_share(pix, sw, 0, 0, sw, 20)
            if bar < 0.5:
                fails.append("the menu bar's band is %.0f%% non-black; the "
                             "chrome was not painted" % (100 * bar))
            dock = nonblack_share(pix, sw, 0, sh - 24, sw, sh)
            if dock < 0.3:
                fails.append("the dock's band is %.0f%% non-black" % (100 * dock))
            # the desktop dither: a 2x2 block in the open desktop has both
            # black and white (gfx_fill_gray through the driver)
            x0, y0 = sw // 2, sh // 2
            blk = {px(pix, sw, x0 + dx, y0 + dy) for dx in (0, 1) for dy in (0, 1)}
            if not ((0, 0, 0) in blk and (255, 255, 255) in blk):
                fails.append("the desktop at (%d, %d) is %r, want the 50%% "
                             "dither of black and white" % (x0, y0, sorted(blk)))
    finally:
        pass
    if fails:
        print("vesa: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("vesa: ok - %dx%d at %dbpp on VID_VESA, chrome and dither drawn"
          % (WANT_W, WANT_H, WANT_BPP))
    return 0


if __name__ == "__main__":
    sys.exit(main())

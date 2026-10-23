#!/usr/bin/env python3
"""The packed-pixel display comes up and draws (SPEC.md 120).

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
  vid_ext     1            every planar body far-calls the driver (120.3)
  vid_cw/ch   1024 x 768   the mode Auto chose on this card (120.2.3)
  vid_planes  16           the DEPTH, which is what the save-under sizers
                           multiply by (120.4)
  vesa_row    DRVR_SEG != 0, DRVR_ERR == 0

then a 1024 x 768 screendump: the menu bar's band at the top is not black
(the chrome was painted through the driver's fills and runs), the desktop
below it carries the 50% dither (gfx_fill_gray's body: a 2x2 block holding
both black and white), and the dock's band at the bottom is not black.

Then the DRIVING half (S2): the File menu dropped - a white box under the
bar where the dither was (the menu's save-under went through the driver's
save, its items through the one-pass run) - and closed again with every
pixel of the dither back (the restore); then the A: drive icon (a blit1 band,
at the desktop's right edge) double-clicked, and a Disk window up - a white
title bar where the dither was.

Then B: double-clicked, and its APPS folder: twenty files, more than a Disk
window shows, so the window draws its row of CHEVRONS under the list - one
gfx_fill_pat, the only pattern fill on an idle desktop - and the desktop's
left margin, every 2x2 block of it between the bar and the dock, must still
be the dither afterwards. The driver's pattern body spent CX on the tile
copy before it cut the width from it, and the three chevron rows went
65,433 pixels each across the desktop (found on the 86Box 486 by eye, then
here).
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
from os88geom import VID_VESA                               # noqa: E402

SOCK = os.path.join(ROOT, "build", "vesa.sock")
PIDFILE = os.path.join(ROOT, "build", "vesa.pid")

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


class Mouse:
    """tools/mouse.py, one process per action, on the 1024 x 768 screen."""

    def run(self, *args):
        subprocess.run(["python3", "tools/mouse.py", "--screen",
                        "%dx%d" % (WANT_W, WANT_H), SOCK] + list(args),
                       cwd=ROOT, check=True, capture_output=True)

    def click(self, x, y):
        self.run("click", str(x), str(y))

    def press(self, x, y):                  # menus: press, drag, release
        self.run("to", str(x), str(y))
        self.run("down")

    def release_at(self, x, y):
        self.run("to", str(x), str(y))
        self.run("up")

    def dblclick(self, x, y):
        self.run("to", str(x), str(y))
        subprocess.run(["python3", "tools/qmp.py", SOCK,
                        "mouse_button 1", "sleep 0.08", "mouse_button 0",
                        "sleep 0.12",
                        "mouse_button 1", "sleep 0.08", "mouse_button 0"],
                       cwd=ROOT, check=True, capture_output=True)


def dither_at(pix, w, x0, y0):
    """Is the 2x2 block at (x0, y0) the desktop's 50% dither?"""
    blk = {px(pix, w, x0 + dx, y0 + dy) for dx in (0, 1) for dy in (0, 1)}
    return (0, 0, 0) in blk and (255, 255, 255) in blk


def checker_at(pix, w, x0, y0):
    """...and is it the CHECKERBOARD: neighbours differ along x and along y,
    the diagonal agrees. dither_at is satisfied by a pattern's vertical
    stripes too (a tile row holds black and white), which is how the gate
    missed the runaway the first time."""
    a, b = px(pix, w, x0, y0), px(pix, w, x0 + 1, y0)
    c, d = px(pix, w, x0, y0 + 1), px(pix, w, x0 + 1, y0 + 1)
    return a != b and a != c and a == d and {a, b} == {(0, 0, 0), (255, 255, 255)}


def white_share(pix, w, x0, y0, x1, y1):
    n = tot = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            tot += 1
            if px(pix, w, x, y) == (255, 255, 255):
                n += 1
    return n / max(1, tot)


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
                         "standard VGA, SPEC.md 120.2.3)" % (w, h, WANT_W, WANT_H))
        bpp = byte(q, "vid_planes")
        if bpp != WANT_BPP:
            fails.append("vid_planes %d, want %d: the depth is the plane count "
                         "on this kind (SPEC.md 120.4)" % (bpp, WANT_BPP))
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

        # --- the driving half: a menu down and up, a window opened ---------
        if not fails:
            m = Mouse()
            m.press(135, 9)                     # File, on the menu bar: a
            os88qemu.pace(q, 1.5)               # menu is press, drag, release
            sw, sh, pix = screendump(q, os.path.join(ROOT, "build", "vesa-shot"))
            if out_png:
                shot.png(out_png.replace(".png", "-menu.png"), sw, sh, pix)
            # the drop's box sits under the title: a white run where the
            # dither was (its save-under was taken through the driver)
            share = white_share(pix, sw, 125, 24, 185, 40)
            if share < 0.7:
                fails.append("File menu: %.0f%% white under the title, want a "
                             "dropped menu's box" % (100 * share))
            m.release_at(600, 400)              # ...released away: it closes
            os88qemu.pace(q, 1.5)
            sw, sh, pix = screendump(q, os.path.join(ROOT, "build", "vesa-shot"))
            # ...checked INSIDE the box's rect, at both ends of a row the
            # drop covered: a restore put back at the wrong x leaves the
            # right half a ghost (S2 found exactly that)
            if not (dither_at(pix, sw, 130, 30) and dither_at(pix, sw, 200, 30)
                    and dither_at(pix, sw, 240, 44)):
                fails.append("File menu closed: the dither under it did not "
                             "come back whole (the restore)")
            # the A: icon at the desktop's right edge (desk zone 0): a Disk
            # window opens on a double-click
            m.dblclick(975, 45)
            os88qemu.pace(q, 4.0)
            sw, sh, pix = screendump(q, os.path.join(ROOT, "build", "vesa-shot"))
            if out_png:
                shot.png(out_png.replace(".png", "-disk.png"), sw, sh, pix)
            # a window somewhere on the desktop: a white title band at least
            # 200 px wide on some row between the bar and the dock
            found = False
            for y in range(24, sh - 40, 4):
                run = best = 0
                for x in range(0, sw):
                    if px(pix, sw, x, y) == (255, 255, 255):
                        run += 1
                        best = max(best, run)
                    else:
                        run = 0
                if best >= 200:
                    found = True
                    break
            if not found:
                fails.append("no Disk window after a double-click on A: - no "
                             "white run of 200 px on any row")
            # the B: icon under it: the apps disk's root overflows a Disk
            # window, so this one draws the chevron band (a pattern fill);
            # the desktop's left margin must be the dither still, every
            # block of it - the runaway covered 64 whole rows
            m.dblclick(975, 100)
            os88qemu.pace(q, 5.0)
            sw, sh, pix = screendump(q, os.path.join(ROOT, "build", "vesa-shot"))
            if out_png:
                shot.png(out_png.replace(".png", "-disk2.png"), sw, sh, pix)
            # B:'s root is four folders; APPS (the first row of the second
            # window, which cascades to a fixed place) holds twenty files,
            # more than the list shows
            m.dblclick(165, 143)
            os88qemu.pace(q, 5.0)
            sw, sh, pix = screendump(q, os.path.join(ROOT, "build", "vesa-shot"))
            if out_png:
                shot.png(out_png.replace(".png", "-apps.png"), sw, sh, pix)
            bad = [y for y in range(24, sh - 40, 2)
                   if not checker_at(pix, sw, 2, y)]
            # ...and the menu bar's disk widget is DOWN: the bar between the
            # menus and the clock, every row of it, is white again after
            # the transfers that opened two windows (SPEC.md 12.8, 120.5:
            # on a 1024-wide bar the widget sits past the composer's cells
            # and fpg_finish has to erase its band rows itself)
            left = [(x, y) for y in range(1, 18) for x in range(600, 860, 2)
                    if px(pix, sw, x, y) != (255, 255, 255)]
            if left:
                fails.append("the disk widget's chrome is still on the bar after "
                             "the transfers: %d non-white pixels between x=600 "
                             "and 860, first at %r" % (len(left), left[0]))
            if bad:
                fails.append("B:\\APPS open: the desktop's left margin is not "
                             "the dither on %d row pairs (first at y=%d) - a "
                             "fill ran past its rect" % (len(bad), bad[0]))
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

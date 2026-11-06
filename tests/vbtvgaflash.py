#!/usr/bin/env python3
"""VGA4's FLASHES, and the write bands that cut them (SPEC.md 126.3.2).

    make vbattle && python3 tests/vbtvgaflash.py [--samples N]

On VGA a pixel is four bits in four PLANES, and a VGA4 record writes each
group of planes that wants one value under its own Map Mask - so between a
pixel's first store and its last the screen holds a colour that is neither
the old frame's nor the new one's. Written mask by mask over the WHOLE
frame (os88vid.vga4_subs' order), that state lasts the frame's whole
decode, and the beam shows it: the owner's "VGA tears constantly ...
things shimmer and flicker where they change". The Video Player's Ikaruga
barely flashes because it is PAGE FLIPPED (its title says so; flip, two
pages): each frame drawn on the hidden page. The battle's builder now
writes the partial masks in BANDS of target.vga_bands rows (16 by
default; mask 0Fh, one store a byte, stays whole), so the wrong colour
lasts a band's decode - the engine is unchanged.

86Box's VGA XT (kern_big; MartyPC's VGA showed no flashes on the same
build, so it is not this row's instrument), the demo battle with a VGA4
title of Conway's Life (ffmpeg's lavfi, white on dark blue: every frame
turns cells between colours that differ in several planes):

  0. on the host, the banded bundle's title decodes to the same frames as
     the whole-frame one's - the picture is unchanged;
  1. whole-frame writes: the glass, sampled N times through the title loop,
     shows pixels in a colour their pixel never takes in any of the loop's
     frames - over 100 a sample on average (the effect, so 2 proves
     something);
  2. bands of 16 rows: at most a third as many.

Measured when written: 639 -> 88 a sample (bands of 16), and on the owner's
own VGA footage (debug only) 308 -> 30 for 11.6% more record bytes.
BROKEN ON PURPOSE (vga_banded answering ops unbanded) 2 goes red.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
os.environ["OS88_EMU"] = "86box"
import os88build    # noqa: E402
import os88geom as geom    # noqa: E402
import os88marty    # noqa: E402
import os88ui       # noqa: E402
import os88vbt as B         # noqa: E402
import os88vid as V         # noqa: E402
import vbtproj as P         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

MACHINE = "os8088_xt_vga_hdd_sb"
TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
LIFE = ("life=s=320x200:r=12:ratio=0.3:life_color=#ffffff:"
        "death_color=#202060:mold=0")
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def bundle(tmp, bands, mp4):
    vbp = os.path.join(tmp, "f%d.vbp" % bands)
    B.demo_project(vbp, bars=2)
    p = P.Project.load(vbp)
    p.set(("target", "vga_bands"), bands)
    p.add_source("life", path=mp4)
    c = p.d["clips"]["title"]
    c["source"], c["in"], c["out"] = "life", 0, 47
    p.d["title"]["loop"] = [0, 47]
    p.save()
    return B.build_project(p, kind="vga4")


def title_frames(data):
    bd = B.Bundle(data)
    rows = B.REND.rows
    surf = B.REND.blank()
    out = []
    for r in bd.stream(bd.clips[bd.title])[:-1]:
        B.decode(surf, r)
        pl = [np.unpackbits(np.frombuffer(
            bytes(surf[q * V.PLANE:q * V.PLANE + 40 * rows]), np.uint8))
            .reshape(rows, 320) for q in range(4)]
        out.append(pl[0] | (pl[1] << 1) | (pl[2] << 2) | (pl[3] << 3))
    return out, rows


def glass_flashes(tmp, data, syms, n):
    """The mean flash pixels a sample over N samples of the title loop"""
    frames, rows = title_frames(data)
    pal = [tuple(data[352 + 3 * i:355 + 3 * i]) for i in range(16)]
    rgb = np.array([[(c << 2) | (c >> 4) for c in v] for v in pal], np.int16)
    allowed = np.zeros((rows, 320, 16), bool)
    yy, xx = np.arange(rows)[:, None], np.arange(320)[None, :]
    for f in frames:
        allowed[yy, xx, f] = True
    vbt = os.path.join(tmp, "BATTLEV.VBT")
    open(vbt, "wb").write(data)
    vhd = os.path.join(tmp, "c.vhd")
    subprocess.run(
        [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
         "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
         "--vbr", os88build.at("build/boothd.bin"),
         "--mbr", os88build.at("build/mbr.bin"),
         "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
         "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
         "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
         "--file", "VBATTLE.O88=" + os88build.at("build/vbattle.o88"),
         "--file", "BATTLEV.VBT=" + vbt], check=True, capture_output=True)
    m = os88marty.launch(None, machine=MACHINE,
                         extra=["--mount", "hd:0:" + vhd], boot=False)
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=300)
        os88marty.no_saver(m)
        os88marty.ui_done(m)
        w = ui.path("C:/VBATTLE.O88")
        base = struct.unpack_from("<H", m.read(
            geom.winptr(m, w.i, ui._S) + geom.W_SEG, 2))[0] << 4

        def rb(nm):
            return m.read(base + syms[nm], 1)[0]
        m.key("Enter")
        os88marty.until(m, lambda mm: rb("vb_tplay") == 1, "the title",
                        poll=0.1, limit=600, guest=120)
        os88marty.until(m, lambda mm: True, "a settle", poll=0.1, limit=60,
                        guest=3)
        bad = []
        for _ in range(n):
            fw, fh, buf = m.fbuf()
            a = np.frombuffer(buf, np.uint8).reshape(fh, fw, 3)
            a = a[::fh // 200, ::fw // 320][:rows, :320].astype(np.int16)
            d = np.abs(a[:, :, None, :] - rgb[None, None, :, :]).sum(3)
            good = allowed[yy, xx, d.argmin(2)] & (d.min(2) <= 12)
            bad.append(int((~good).sum()))
            os88marty.until(m, lambda mm: True, "a gap", poll=0.0,
                            limit=30, guest=0.137)
    finally:
        m.quit()
    return sum(bad) / float(n), max(bad)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", type=int, default=60)
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    try:
        mp4 = os.path.join(tmp, "life.mp4")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-f", "lavfi", "-i",
                        LIFE, "-t", "4", "-pix_fmt", "yuv420p", mp4],
                       check=True)
        whole, banded = bundle(tmp, 0, mp4), bundle(tmp, 16, mp4)
        fw, _ = title_frames(whole)
        fb, _ = title_frames(banded)
        ok(len(fw) == len(fb) and all((x == y).all() for x, y in zip(fw, fb)),
           "0. the banded title decodes to the whole-frame one's %d frames"
           % len(fw))
        res = {}
        for name, data in (("whole", whole), ("bands", banded)):
            d = os.path.join(tmp, name)
            os.makedirs(d)
            res[name] = glass_flashes(d, data, syms, a.samples)
            print("     %s: %d bytes, flash pixels a sample %.0f (worst %d)"
                  % (name, len(data), res[name][0], res[name][1]),
                  flush=True)
        ok(res["whole"][0] > 100, "1. whole-frame writes FLASH on 86Box's "
           "VGA: %.0f pixels a sample in a colour of no frame"
           % res["whole"][0])
        ok(res["bands"][0] * 3 <= res["whole"][0],
           "2. bands of 16 rows: %.0f a sample, %.1fx fewer"
           % (res["bands"][0], res["whole"][0] / max(res["bands"][0], 0.5)))
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print("vbtvgaflash: %d FAILED" % len(fails))
        return 1
    print("vbtvgaflash: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

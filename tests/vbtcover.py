#!/usr/bin/env python3
"""THE TITLE PICTURE in the battle's window (SPEC.md 125.2), on MartyPC.

    make vbattle && python3 tests/vbtcover.py

The owner: "An option to give a 'title picture' that will be shown on the
desktop, when the battle is loaded (or embedded). Still need one line of
instructions on click/enter to begin, below the title." The demo battle
with a picture (an ellipse under a white bar) and its line, built and
played:

  cga:  on the XT's CGA desktop (640 x 200) the window is the picture and a
        line, titled with the battle's whole 31-letter name: the picture on
        the glass IS the bundle's tall variant (320 x 100) bit for bit, and
        the line under it is lit; Enter plays it and Esc comes back - the
        picture already read back as the play hands the screen over (no
        flash of the text window), and bit for bit again;
  herc: on the 5150's Hercules desktop (720 x 348), its own variant (360
        wide), bit for bit.

Broken on purpose (vb_cvpaint's blit taken out) both arms red.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build, os88geom as geom, os88marty, os88ui   # noqa: E402
import os88vbt as B         # noqa: E402
import vbtproj as P         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
ARMS = {"cga": ("os8088_xt_hdd_sb", "cga4", "BATTLE.VBT", B.CV_TALL),
        "herc": ("os8088_5150_herc_hdd_sb_gla", "herc", "BATTLEH.VBT",
                 B.CV_HERC)}
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def pixel_reader(m, arm):
    """(x, y) -> 1 lit, off the desktop's own framebuffer"""
    if arm == "herc":
        v = m.read(0xB0000, 32768)
        return lambda x, y: (v[0x2000 * (y & 3) + 90 * (y >> 2) + (x >> 3)]
                             >> (7 - (x & 7))) & 1
    v = m.read(0xB8000, 16384)
    return lambda x, y: (v[0x2000 * (y & 1) + 80 * (y >> 1) + (x >> 3)]
                         >> (7 - (x & 7))) & 1


def variant(cover, kind):
    for i in range(cover[2]):
        k, _, w, h, st, off = struct.unpack_from("<BBHHHI", cover, 100 + 12 * i)
        if k == kind:
            return w, h, st, cover[off:off + st * h]
    raise SystemExit("no variant %d" % kind)


def run(arm, tmp, syms):
    machine, kind, name, cv = ARMS[arm]
    from PIL import Image, ImageDraw
    vbp = os.path.join(tmp, "c.vbp")
    B.demo_project(vbp, bars=2)
    im = Image.new("RGB", (640, 480), (0, 0, 0))
    dr = ImageDraw.Draw(im)
    dr.ellipse((100, 80, 540, 440), fill=(220, 160, 40))
    dr.rectangle((0, 0, 639, 40), fill=(255, 255, 255))
    im.save(os.path.join(tmp, "cover.png"))
    p = P.Project.load(vbp)
    p.set(("name",), "The Demo Battle of the Beetle 31"[:B.TITLE_MAX])
    p.set(("cover",), {"image": "cover.png", "line": "Click or press Enter "
                       "to begin"})
    p.save()
    data = B.build_project(p, kind=kind)
    w, h, st, bm = variant(B.Bundle(data).cover, cv)
    f = os.path.join(tmp, name)
    open(f, "wb").write(data)
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
         "--file", "%s=%s" % (name, f)], check=True, capture_output=True)
    m = os88marty.launch(None, machine=machine,
                         extra=["--mount", "hd:0:" + vhd], boot=False)
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=300)
        os88marty.no_saver(m)
        os88marty.ui_done(m)
        wn = ui.path("C:/VBATTLE.O88")
        rec = m.read(ui._S("wm_wins") + wn.i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4

        def rb(nm):
            return m.read(base + syms[nm], 1)[0]

        def rw(nm):
            return struct.unpack_from("<H", m.read(base + syms[nm], 2))[0]

        def until(cond, what, guest=120.0):
            os88marty.until(m, lambda mm: cond(), what, poll=0.05,
                            limit=900.0, guest=guest)

        def key(k):
            m.key(k, up=False)
            os88marty.until(m, lambda mm: True, "k", poll=0.0, limit=10.0,
                            guest=0.06)
            m.key(k, down=False)

        def look(tag):
            os88marty.ui_done(m)
            m.pause()
            px = pixel_reader(m, arm)
            x0, y0 = rw("vb_cvx"), rw("vb_py")
            bad, where = 0, []
            mx, my = struct.unpack("<HH", m.read(ui._S("mouse_x"), 4))
            for y in range(h):
                for x in range(w):
                    if mx <= x0 + x < mx + 16 and my <= y0 + y < my + 16:
                        continue        # (the pointer, over it)
                    want = (bm[y * st + (x >> 3)] >> (7 - (x & 7))) & 1
                    if px(x0 + x, y0 + y) != want:
                        bad += 1
                        where.append((x0 + x, y0 + y))
            if where and os.environ.get("VBTC_DBG"):
                xs, ys = [q[0] for q in where], [q[1] for q in where]
                print("   differ in x %d..%d y %d..%d; mouse %d,%d; at %d,%d"
                      % (min(xs), max(xs), min(ys), max(ys), mx, my, x0, y0))
            lit = sum(px(x0 + x, y0 + h + 2 + y) for y in range(8)
                      for x in range(w))
            if os.environ.get("VBTC_SHOT"):     # (a look, for a person)
                from PIL import Image
                W, H = (720, 348) if arm == "herc" else (640, 200)
                im = Image.new("1", (W, H))
                im.putdata([px(x, y) * 255 for y in range(H) for x in range(W)])
                im.save("%s-%s-%s.png" % (os.environ["VBTC_SHOT"], arm,
                                          tag.replace(" ", "_")))
            m.run()
            ok(rb("vb_cvon") == 1 and (rw("vb_cvw"), rw("vb_cvh")) == (w, h)
               and bad == 0 and lit > 100,
               "%s, %s: the picture on the glass is the bundle's %d x %d, bit "
               "for bit (%d differ), the line under it lit (%d px)"
               % (arm, tag, w, h, bad, lit))
        tp = struct.unpack_from("<H", rec, geom.W_TITLE)[0]  # (os88geom's
        title = m.read(base + tp, 48).split(b"\0")[0].decode()  # reads 24)
        ok(title == p.d["name"] and len(title) == B.TITLE_MAX,
           "%s: the window titled with the battle's whole name, %d letters "
           "(%r)" % (arm, len(title), title))
        look("opened")
        if arm == "cga":
            key("Enter")
            until(lambda: rb("vb_ready") == 1, "the play")
            until(lambda: rb("vb_tplay") == 1, "the title")
            key("Escape")
            until(lambda: rb("vb_ready") == 0, "back on the desktop",
                  guest=30.0)
            os88marty.ui_done(m)
            # NO FLASH: the picture read back while the screen was still the
            # play's ([vb_cvback], its state as FSX_RUN returned) - read
            # after, the window's first paint was its text
            ok(rb("vb_cvback") == 1, "cga: the picture back in memory as the "
               "play hands the screen back (no text-window flash)")
            look("after a play")
    finally:
        m.quit()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("cga", "herc", "both"), default="both")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    for arm in (("cga", "herc") if a.arm == "both" else (a.arm,)):
        with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) \
                as tmp:
            run(arm, tmp, syms)
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

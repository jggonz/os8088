#!/usr/bin/env python3
"""A rendition's PICTURE SET DOWN (SPEC.md 125.3.2): Hercules' 640 x 348 at
half size, centred in black, on MartyPC's 5150 with a Hercules.

    make vbattle && python3 tests/vbtscale.py

The owner: "herc's 720*348 is very heavy, so we need to be able to set
resolutions down." The demo battle, no title, an ffmpeg test pattern as a
FULL-SCREEN opening (so the encoder's own frames are made, not a scene's),
built for Hercules at 100% and at 50%:

  host: at 50% every frame is the picture 320 x 174 centred in the 640 x
        348 screen and black round it - byte-aligned, so no record touches
        the black - and the bundle is smaller than at 100%; the same holds
        for CGA4 and VGA4 at 75% (their canvas frames); a picture size the
        engine has no use for is refused by the check;
  glass: the 50% Hercules bundle plays: while the opening runs, its
        picture is lit inside the box and NOTHING outside it, and the
        battle goes on to its menu. The engine is unchanged - a smaller
        picture is only fewer bytes a frame.

Broken on purpose (pad_into putting the picture at the top left) the
glass's box check fails.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import numpy as np          # noqa: E402
import os88build, os88geom as geom, os88marty, os88ui   # noqa: E402
import os88vbt as B         # noqa: E402
import vbtana as A          # noqa: E402
import vbtproj as P         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_5150_herc_hdd_sb_gla"
KEYS = {"return": "Enter", "esc": "Escape"}
GS_MENU = 1
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def herc_rows(mem, rows=348):
    """Hercules memory as rows of the battle's 80 bytes (from byte 5)"""
    return [mem[(y & 3) * 8192 + (y >> 2) * 90 + 5:
                (y & 3) * 8192 + (y >> 2) * 90 + 85] for y in range(rows)]


def project(tmp, scale):
    vid = os.path.join(tmp, "intro.mp4")
    if not os.path.exists(vid):
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                        "-i", "testsrc2=size=320x240:rate=15", "-t", "3",
                        vid], check=True)
    vbp = os.path.join(tmp, "s%d.vbp" % sum(scale.values()))
    B.demo_project(vbp, bars=2)
    p = P.Project.load(vbp)
    p.set(("enemy", "hp"), 1)
    p.set(("title",), None)
    p.remove_clip("title")
    p.add_source("intro", path=vid)
    if scale:
        p.set(("target", "scale"), dict(scale))
    p.save()
    n = len(A.screens(p, "intro", full=True))
    p.add_clip("intro", "intro", (0, n - 1), anchors=(), exit=0)
    p.set(("openings",), [{"clip": "intro", "sound": None, "full": True},
                          {"clip": "reveal", "sound": "riser"}])
    p.set(("target", "renditions"), ["cga4", "herc"])
    p.save()
    return p


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        p100 = project(tmp, {})
        p50 = project(tmp, {"herc": 50, "cga4": 75, "vga4": 75})
        fr = A.rend_screens(p50, "intro", "herc", full=True)
        f = np.asarray(fr[len(fr) // 2])
        pw, ph, x0, y0 = A.boxed(640, 348, 50)
        inside = f[y0:y0 + ph, x0:x0 + pw]
        outside = f.copy()
        outside[y0:y0 + ph, x0:x0 + pw] = 0
        ok(f.shape == (348, 640) and (pw, ph, x0, y0) == (320, 174, 160, 86)
           and inside.any() and not outside.any(),
           "host: Hercules at 50%%: the picture %d x %d at %d,%d, black "
           "round it" % (pw, ph, x0, y0))
        for kind in ("cga4", "vga4"):
            g = np.asarray(A.rend_screens(p50, "intro", kind)[5])
            qw, qh, qx, qy = A.boxed(320, 142, 75)
            o = g.copy()
            o[qy:qy + qh, qx:qx + qw] = 0
            ok(g.shape == (142, 320) and g[qy:qy + qh, qx:qx + qw].any()
               and not o.any(), "host: %s at 75%%: %d x %d at %d,%d"
               % (kind, qw, qh, qx, qy))
        h100 = B.build_project(p100, kind="herc")
        h50 = B.build_project(p50, kind="herc")
        ok(len(h50) < len(h100), "host: the Hercules bundle at 50%% is %d KB "
           "against %d KB at 100%%" % (len(h50) // 1024, len(h100) // 1024))
        q = P.Project(p50.d)
        q.set(("target", "scale"), {"herc": 60})
        ok([x for x in q.check() if x.where == "target.scale"],
           "the check refuses a picture size of 60%")
        files = []
        for k in ("cga4", "herc"):
            data = B.build_project(p50, kind=k)
            fn = os.path.join(tmp, B.BUNDLE_NAMES[k])
            open(fn, "wb").write(data)
            files.append((B.BUNDLE_NAMES[k], fn))
        b = B.Bundle(h50)
        vhd = os.path.join(tmp, "c.vhd")
        cmd = [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
               "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
               "--vbr", os88build.at("build/boothd.bin"),
               "--mbr", os88build.at("build/mbr.bin"),
               "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
               "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
               "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
               "--file", "VBATTLE.O88=" + os88build.at("build/vbattle.o88")]
        for nm, fn in files:
            cmd += ["--file", "%s=%s" % (nm, fn)]
        subprocess.run(cmd, check=True, capture_output=True)
        m = os88marty.launch(None, machine=MACHINE,
                             extra=["--mount", "hd:0:" + vhd], boot=False)
        try:
            m.run()
            ui = os88ui.UI(m)
            ui.up(limit=300)
            os88marty.no_saver(m)
            os88marty.ui_done(m)
            w = ui.path("C:/VBATTLE.O88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4

            def rb(nm, off=0):
                return m.read(base + syms[nm] + off, 1)[0]

            def rw(nm, off=0):
                return struct.unpack_from("<H", m.read(base + syms[nm] + off,
                                                       2))[0]

            def until(cond, what, guest=120.0, poll=0.05):
                os88marty.until(m, lambda mm: cond(), what, poll=poll,
                                limit=900.0, guest=guest)
            m.key(KEYS["return"], up=False)
            until(lambda: True, "a key", guest=0.06, poll=0.0)
            m.key(KEYS["return"], down=False)
            until(lambda: rb("vb_ready") == 1, "the play to start")
            ok(rb("vb_kind") == 2, "the engine took BATTLEH.VBT")
            c0 = b.openings[0]
            until(lambda: rb("vb_cc") == c0 and rw("vb_cfr") >= 5,
                  "the opening, five frames in")
            bx0, bx1 = x0 // 8, (x0 + pw) // 8
            looks, lit_in, lit_out = 0, 0, 0
            while rb("vb_cc") == c0 and looks < 15:
                m.pause()
                rows = herc_rows(m.read(0xB0000, 32768))
                m.run()
                looks += 1
                for y, r in enumerate(rows):
                    if y0 <= y < y0 + ph:
                        lit_in += sum(1 for x in r[bx0:bx1] if x)
                        lit_out += sum(1 for x in r[:bx0] + r[bx1:] if x)
                    else:
                        lit_out += sum(1 for x in r if x)
                until(lambda: True, "a moment", guest=0.1, poll=0.0)
            ok(looks >= 5 and lit_in > 1000 * looks and lit_out == 0,
               "glass: the opening at half size - lit inside its %d x %d box "
               "(%d bytes over %d looks), nothing outside it (%d)"
               % (pw, ph, lit_in, looks, lit_out))
            until(lambda: rb("vb_state") == GS_MENU, "the menu", guest=180.0)
            ok(True, "the battle goes on to its menu")
            m.key(KEYS["esc"], up=False)
            until(lambda: True, "a key", guest=0.06, poll=0.0)
            m.key(KEYS["esc"], down=False)
        finally:
            m.quit()
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""A FULL-SCREEN opening, then one WITH THE MENU (SPEC.md 125.3): the owner's
"Opening0: Fullscreen; Opening1: Menu appears, video is in its upper
position", on MartyPC's XT with a CGA.

    make vbattle && python3 tests/vbtopenfull.py

The demo battle with no title, its openings an ffmpeg test pattern marked
FULL and the demo's REVEAL with the menu:

  host: the full opening is encoded over the WHOLE screen - its frames 200
        rows, the band's rows carrying picture - and carries CF_FULL; the
        menu one does not, and its frames are the canvas's with the band's
        rows black under them; the check refuses a full opening last or
        after a menu one;
  glass: while the full opening plays the band's rows show ITS picture and
        the band is not let in ([vb_bandok] 0); at the menu opening's first
        frame it is ([vb_bandok] 1), and the band comes up under it - the
        engine's band buffer on the glass - before the idle loop is reached.

Broken on purpose (the engine bringing the band in at any opening) the
band's rows are the band's while the full one plays.
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
MACHINE = "os8088_xt_hdd_sb"
KEYS = {"return": "Enter", "esc": "Escape"}
GS_MENU = 1
ROWS = 142                  # the demo's canvas
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def band_bytes(vram):
    """CGA's rows ROWS..199 out of the 16 KB screen"""
    return b"".join(vram[(y & 1) * 8192 + (y >> 1) * 80:
                         (y & 1) * 8192 + (y >> 1) * 80 + 80]
                    for y in range(ROWS, 200))


def project(tmp):
    vid = os.path.join(tmp, "intro.mp4")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                    "testsrc2=size=320x240:rate=15", "-t", "3", vid],
                   check=True)
    vbp = os.path.join(tmp, "full.vbp")
    B.demo_project(vbp, bars=2)
    p = P.Project.load(vbp)
    p.set(("enemy", "hp"), 1)
    p.set(("title",), None)
    p.remove_clip("title")          # (the demo's title, unused here)
    p.add_source("intro", path=vid)
    p.save()
    n = len(A.screens(p, "intro", full=True))
    p.add_clip("intro", "intro", (0, n - 1), anchors=(), exit=0)
    p.set(("openings",), [{"clip": "intro", "sound": None, "full": True},
                          {"clip": "reveal", "sound": "riser"}])
    # the check: a full one LAST, or after a menu one, is refused
    q = P.Project(p.d)
    q.set(("openings",), [{"clip": "reveal", "sound": None},
                          {"clip": "intro", "sound": None, "full": True}])
    errs = [x for x in q.check() if x.level == "ERROR"
            and x.where.startswith("openings")]
    ok(len(errs) == 2, "the check refuses a full opening last and after a "
       "menu one (%s)" % [x.text[:40] for x in errs])
    ok(not [x for x in p.check() if x.level == "ERROR"],
       "the full-then-menu order passes the check")
    p.save()
    return p, B.build_project(p)


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        p, data = project(tmp)
        b = B.Bundle(data)
        o0, o1 = b.clips[b.openings[0]], b.clips[b.openings[1]]
        ok(o0["flags"] & P_CF_FULL and not o1["flags"] & P_CF_FULL,
           "the full opening carries CF_FULL, the menu one not (%02X %02X)"
           % (o0["flags"], o1["flags"]))
        scr = bytearray(65536)
        lit0 = 0
        for r in b.stream(o0):
            B.decode(scr, r)
            lit0 = max(lit0, sum(1 for x in band_bytes(scr) if x))
        rest1 = 0
        for r in b.stream(o1)[:-1]:     # (its last record is the seam)
            B.decode(scr, r)
            rest1 = max(rest1, sum(1 for x in band_bytes(scr) if x))
        ok(lit0 > 1000 and rest1 == 0,
           "host: the full opening's picture reaches the band's rows (%d "
           "bytes lit), the menu one's leaves them black (%d)"
           % (lit0, rest1))
        vbt = os.path.join(tmp, "BATTLE.VBT")
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
             "--file", "BATTLE.VBT=" + vbt], check=True, capture_output=True)
        m = os88marty.launch(None, machine=MACHINE,
                             extra=["--mount", "hd:0:" + vhd], boot=False)
        try:
            m.run()
            ui = os88ui.UI(m)
            ui.up(limit=240)
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
            c0 = b.openings[0]
            until(lambda: rb("vb_cc") == c0 and rw("vb_cfr") >= 5,
                  "the full opening, five frames in")
            looks, lit, ok_ = 0, 0, 0
            while rb("vb_cc") == c0 and looks < 20:
                m.pause()
                v = m.read(0xB8000, 16384)
                band_ok = rb("vb_bandok")
                m.run()
                looks += 1
                lit = max(lit, sum(1 for x in band_bytes(v) if x))
                ok_ += band_ok
                until(lambda: True, "a moment", guest=0.1, poll=0.0)
            ok(looks >= 5 and lit > 1000 and ok_ == 0,
               "glass: while the FULL opening plays its picture is in the "
               "band's rows (%d bytes lit), the band not let in (%d looks)"
               % (lit, looks))
            c1 = b.openings[1]
            until(lambda: rb("vb_cc") == c1, "the menu opening")
            until(lambda: rb("vb_bandok") == 1, "the band let in", guest=5.0)
            ok(rb("vb_cc") == c1 and rb("vb_mode") != 2,
               "at the MENU opening's first frame the band is let in "
               "(clip %d, before the idle loop)" % rb("vb_cc"))
            # the band is composed off the screen and then presented (~1-2
            # guest s on the XT under the hook): looked for until it is up
            seen = {"n": 0, "cc": None}

            def band_up():
                seen["n"] = sum(1 for x in band_bytes(m.read(0xB8000, 16384))
                                if x)
                seen["cc"] = rb("vb_cc"), rb("vb_mode")
                return seen["n"] > 200
            os88marty.until(m, lambda mm: band_up(), "the band up",
                            poll=0.1, limit=900.0, guest=10.0)
            ok(True, "...and the band comes up under it (%d bytes in its "
               "rows; clip %d, mode %d)" % ((seen["n"],) + seen["cc"]))
            until(lambda: rb("vb_state") == GS_MENU, "the menu", guest=180.0)
            ok(True, "the battle reaches its menu")
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


P_CF_FULL = B.CF_FULL

if __name__ == "__main__":
    sys.exit(main())

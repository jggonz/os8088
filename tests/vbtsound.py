#!/usr/bin/env python3
"""THE MUSIC UNDER THE BAND (SPEC.md 125.5), on MartyPC's 5150 with a
Hercules and a Sound Blaster - the owner: "when the menus are drawing the
sound shuts off ... sometimes half a second to more than a second later".

    make vbattle && python3 tests/vbtsound.py [--turns N]

A band rebuilt keeps the reader away for its whole length - on Hercules'
XT the composing and the two-by-two present are ~2 s of the foreground's
time under the hook - and the music's ring (3 s) was refilled only once
under half. So a rebuild that began with the ring near half ran it dry,
under the band or just after. The engine now tops the ring up first
(vb_mtopup) and keeps the lowest level it stood at as a rebuild ended
([vb_mlow]).

The demo battle on BATTLEH.VBT, its music long enough to STREAM, the enemy
too strong to fall: turn after turn of menus and enemy moves, then

  - the music never ran dry ([vb_munder] 0);
  - at the end of every band rebuild the ring still held over a second of
    music ([vb_mlow]).

Broken on purpose (vb_mtopup returning at once) the lowest falls under a
second.
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
MACHINE = "os8088_5150_herc_hdd_sb_gla"
GS_MENU = 1
RATE = 11025
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--turns", type=int, default=6)
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        vbp = os.path.join(tmp, "s.vbp")
        B.demo_project(vbp, bars=48)
        p = P.Project.load(vbp)
        p.set(("enemy", "hp"), 9000)
        p.set(("target", "renditions"), ["cga4", "herc"])
        p.save()
        files = []
        for k in ("cga4", "herc"):
            f = os.path.join(tmp, B.BUNDLE_NAMES[k])
            open(f, "wb").write(B.build_project(p, kind=k))
            files.append((B.BUNDLE_NAMES[k], f))
        vhd = os.path.join(tmp, "c.vhd")
        cmd = [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
               "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
               "--vbr", os88build.at("build/boothd.bin"),
               "--mbr", os88build.at("build/mbr.bin"),
               "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
               "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
               "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
               "--file", "VBATTLE.O88=" + os88build.at("build/vbattle.o88")]
        for nm, f in files:
            cmd += ["--file", "%s=%s" % (nm, f)]
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

            def key(k):
                m.key(k, up=False)
                until(lambda: True, "a key", guest=0.06, poll=0.0)
                m.key(k, down=False)
                until(lambda: True, "a key", guest=0.25, poll=0.0)
            key("Enter")
            until(lambda: rb("vb_ready") == 1, "the play to start")
            ok(rb("vb_kind") == 2 and rb("vb_snd") and not rb("vb_mres"),
               "Hercules, a card, and the music STREAMING")
            until(lambda: rb("vb_tplay") == 1, "the title")
            key("Enter")
            for t in range(a.turns):
                until(lambda: rb("vb_state") == GS_MENU and
                      rb("vb_dirty") == 0, "a menu", guest=240.0)
                key("Enter")            # Attack; the enemy's turn after
                until(lambda: rb("vb_state") != GS_MENU, "the turn",
                      guest=30.0)
            until(lambda: rb("vb_state") == GS_MENU and rb("vb_dirty") == 0,
                  "the last menu", guest=240.0)
            low, under = rw("vb_mlow"), rw("vb_munder")
            ok(under == 0, "the music never ran dry over %d turns ([vb_munder]"
               " %d)" % (a.turns, under))
            ok(low != 0xFFFF and low > RATE,
               "as every band rebuild ended the ring still held %.2f s of "
               "music at its lowest (%d bytes)" % (low / float(RATE), low))
            key("Escape")
        finally:
            m.quit()
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

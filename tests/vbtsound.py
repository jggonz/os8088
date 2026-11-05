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

--heavy: the idle loop replaced by one changing all over (55 KB/s on
Hercules, the owner's) and the cursor moved quickly down and back at every
menu - the card never halted ([vb_nunder]), the music never dry. A
MEASUREMENT more than a gate: MartyPC's XT-IDE did not halt the card here
before the every-pass check either (the owner's ST-225 did).
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
    ap.add_argument("--heavy", action="store_true",
                    help="an idle loop that changes all over (Hercules "
                    "~50 KB/s), and the cursor moved quickly at each menu")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        vbp = os.path.join(tmp, "s.vbp")
        B.demo_project(vbp, bars=48)
        p = P.Project.load(vbp)
        p.set(("enemy", "hp"), 9000)
        p.set(("target", "renditions"), ["cga4", "herc"])
        if a.heavy:
            # THE OWNER'S IDLE LOOP, ~55 KB/s on Hercules: a picture moving
            # all over, every frame
            iv = os.path.join(tmp, "idle.mp4")
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f",
                            "lavfi", "-i", "testsrc2=size=320x240:rate=15,"
                            "noise=alls=18:allf=t", "-t", "3", "-r", "15", iv],
                           check=True)
            p.add_source("busy", path=iv)
            p.set_idle("busy", (0, 12))     # (~40 KB: what the machine
            for cid, c in list(p.d["clips"].items()):   # holds beside the
                if c.get("anchors"):                    # rings; the clips
                    p.set_anchors(cid, [0, 6])          # anchored inside it)
                    p.set(("clips", cid, "exit"), 0)
        p.save()
        files = []
        idle_kbs = 0.0
        for k in ("cga4", "herc"):
            f = os.path.join(tmp, B.BUNDLE_NAMES[k])
            data = B.build_project(p, kind=k)
            open(f, "wb").write(data)
            files.append((B.BUNDLE_NAMES[k], f))
            if k == "herc":
                bb = B.Bundle(data)
                ilen = struct.unpack_from("<I", data, 342)[0]   # (VH_IDLE)
                idle_kbs = ilen / 1024.0 / (bb.nidle / float(bb.fps))
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
            try:
                until(lambda: rb("vb_ready") == 1, "the play to start")
            except os88marty.MartyError:
                e = rw("vb_err")
                print("ENGINE SAYS: %r" % (m.read(base + e, 80).split(b"\0")[0]
                                           if e else None), flush=True)
                if os.environ.get("VBTS_SHOT"):
                    w_, h_, d_ = m.fbuf(False)
                    os88marty.write_png_rgb(os.environ["VBTS_SHOT"], w_, h_, d_)
                raise
            ok(rb("vb_kind") == 2 and rb("vb_snd") and not rb("vb_mres"),
               "Hercules, a card, and the music STREAMING")
            until(lambda: rb("vb_tplay") == 1, "the title")
            key("Enter")
            for t in range(a.turns):
                until(lambda: rb("vb_state") == GS_MENU and
                      rb("vb_dirty") == 0, "a menu", guest=240.0)
                if a.heavy:             # QUICKLY down and back (the
                    for k in ["ArrowDown"] * 3 + ["ArrowUp"] * 3:   # owner's
                        m.key(k, up=False)                      # "three down
                        until(lambda: True, "k", guest=0.04,    # arrow
                              poll=0.0)                         # presses -
                        m.key(k, down=False)                    # quickly")
                        until(lambda: True, "k", guest=0.1, poll=0.0)
                    until(lambda: True, "settle", guest=3.0, poll=0.0)
                key("Enter")            # Attack; the enemy's turn after
                until(lambda: rb("vb_state") != GS_MENU, "the turn",
                      guest=30.0)
            until(lambda: rb("vb_state") == GS_MENU and rb("vb_dirty") == 0,
                  "the last menu", guest=240.0)
            low, under = rw("vb_mlow"), rw("vb_munder")
            print("     the card halted %d times; the picture waited %d; "
                  "the idle loop %.1f KB/s" % (
                      rw("vb_nunder"), rw("vb_stall"), idle_kbs), flush=True)
            if a.heavy:
                ok(rw("vb_nunder") == 0, "the card never halted under the "
                   "busy idle loop (%d)" % rw("vb_nunder"))
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

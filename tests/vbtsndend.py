#!/usr/bin/env python3
"""THE CARD'S STREAM ENDED UNDER THE BATTLE (SPEC.md 125.3): the owner's
"no sound causes a permanent freeze", on MartyPC's XT with a CGA and an SB.

    make vbattle && python3 tests/vbtsndend.py

SOUND.DRV's watchdog ENDS a stream whose block interrupt is ~165 ms late
(SPEC.md 34.5). The engine took that as the play gone silent for good -
and a clip's sound cursor then never moved, the ring's room was counted
from it, and the next action's picture froze once it had played what its
ring held: the owner's "the music cut out ... the next action clip's video
played, up to the point that it was supposed to play a sound - and then
the video also froze. Permanently. ESC exited to the desktop just fine".

The demo battle to its first menu; then the driver's own watchdog is made
to fire (its countdown, [sbl_wd], poked to 1 - the next tick ends the
stream, the path the owner's machine took), and:

  1. the engine OPENS THE STREAM AGAIN ([vb_nended] 1, [vb_snd] 1) and the
     music plays on (its ring taken from, the card's count moving);
  2. the next action (Attack) plays TO ITS END and the idle loop is back.

Broken on purpose (vb_snd_keep's ENDED going silent, as before) 1 fails;
the ring's room counted from a sound cursor whatever the card, and a
stream left ended, 2 fails.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build, os88geom as geom, os88marty, os88sym, os88ui   # noqa: E402
import os88vbt as B         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_xt_hdd_sb"
GS_MENU = 1
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def sound_syms(tmp):
    """SOUND.DRV's symbols, by assembling it as the Makefile does - and
    held to build/sound.bin byte for byte, or they describe another driver"""
    src = os.path.join(tmp, "s.asm")
    mp = os.path.join(tmp, "s.map")
    open(src, "w").write(open("drivers/sound/sound.asm").read() +
                         "\n[map symbols %s]\n" % mp)
    out = os.path.join(tmp, "s.bin")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-DPICOMEM",
                    "-I", "drivers/sound/", "-I", "drivers/", "-I", "apps/",
                    "-o", out, src], check=True)
    if open(out, "rb").read() != open(os88build.at("build/sound.bin"),
                                      "rb").read():
        raise SystemExit("vbtsndend: build/sound.bin is not this tree's "
                         "driver - run make")
    return {m.group(2): int(m.group(1), 16) for m in re.finditer(
        r"^\s*([0-9A-F]+)\s+[0-9A-F]+\s+(\S+)$", open(mp).read(), re.M)}


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    EQ = os88sym.equates(())
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        ss = sound_syms(tmp)
        vbp = os.path.join(tmp, "s.vbp")
        B.demo_project(vbp, bars=48)    # (music long enough to STREAM)
        import vbtproj as P
        p = P.Project.load(vbp)
        p.set(("enemy", "hp"), 9000)
        # ARI's ATTACK STREAMED: eight seconds of a picture moving all over,
        # far more than its ring holds - a stuck sound cursor costs a clip
        # that fits its ring nothing, and the owner's Bolt did not
        v = os.path.join(tmp, "big.mp4")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                        "-i", "testsrc2=size=320x240:rate=15,noise=alls=12:"
                        "allf=t", "-t", "8", v], check=True)
        p.add_source("big", path=v)
        import vbtana as A
        n = len(A.screens(p, "big"))
        c = p.d["clips"]["ari_attack"]
        c.update(source="big", **{"in": 0, "out": n - 1})
        p.save()
        vbt = os.path.join(tmp, "BATTLE.VBT")
        open(vbt, "wb").write(B.build_project(p))
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
            drow = m.read(ui._S("drv_tab"), EQ["DRVR_SIZE"])     # SOUND: 0
            dseg = struct.unpack_from("<H", drow, EQ["DRVR_SEG"])[0]

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
            until(lambda: rb("vb_tplay") == 1, "the title")
            key("Enter")
            until(lambda: rb("vb_state") == GS_MENU and rb("vb_dirty") == 0,
                  "the menu", guest=240.0)
            ok(rb("vb_snd") == 1, "the card plays (the demo's music)")
            # THE WATCHDOG: its countdown to 1, the next tick ends it
            m.pause()
            m.write(dseg * 16 + ss["sbl_wd"], b"\x01\x00")      # (and its
            m.write(dseg * 16 + ss["sbl_wd_rst"], b"\x01\x00")  # reload: a
            # block IRQ reloads it every 46 ms, a tick spends it every 55 -
            # the reopen's verb 0 sets both afresh)
            m.run()
            until(lambda: rw("vb_nended") >= 1, "the stream ended and seen",
                  guest=10.0)
            for _ in range(8):
                m.pause()
                print("     after the reopen: card %d ended %d under %d "
                      "cons %d wpos %d rbase %d mtake %d mund %d" % (
                          rb("vb_snd"), rw("vb_nended"), rw("vb_nunder"),
                          rw("vb_cons32"), rw("vb_wpos"), rw("vb_rbase"),
                          rw("vb_mtake"), rw("vb_munder")), flush=True)
                m.run()
                until(lambda: True, "a moment", guest=0.25, poll=0.0)
            t0 = rw("vb_mtake")
            cons0 = rw("vb_cons32")
            until(lambda: True, "a moment", guest=1.0, poll=0.0)
            ok(rw("vb_nended") == 1 and rb("vb_snd") == 1 and
               rw("vb_mtake") != t0 and rw("vb_cons32") != cons0,
               "1. the stream ENDED (the driver's watchdog) and opened again: "
               "the music plays on (ended %d, card %d)" % (
                   rw("vb_nended"), rb("vb_snd")))
            key("Enter")            # Attack
            until(lambda: rb("vb_mode") == 1, "an action's clip", guest=30.0)
            big = max(c_["len"] for c_ in B.Bundle(open(vbt, "rb").read())
                      .clips)
            cc = rb("vb_cc")
            try:
                until(lambda: rb("vb_mode") == 2, "the idle loop again",
                      guest=60.0)
                back = True
            except os88marty.MartyError:
                back = False
            ok(back, "2. the next action (clip %d, %d KB - more than a ring) "
               "played TO ITS END and the idle loop is back (stalled %d "
               "periods)" % (cc, big // 1024, rw("vb_stall")))
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

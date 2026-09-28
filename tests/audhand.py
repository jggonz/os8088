#!/usr/bin/env python3
"""THE AUDIO PLAYER'S HANDOFF FINDS ITS QUEUE (SPEC.md 86.11.1).

    make && python3 tests/audhand.py

`-DAP_HANDOFF` (off by default) makes a second double-clicked .WAV go to the
Audio Player already running, through APQUEUE.DAT on the system volume's
root, instead of opening a second window. The running player polls for it
every AP_QUE_TICKS.

The poll stood at the root with OSAPI_FILE_GOTO_Q, which moves the machine
and NOT the instance - and every file call first puts the machine back in the
instance's own folder (SPEC.md 74.1). So it read APQUEUE.DAT from wherever
the player was standing and never saw a handoff, unless that was the system
root. The layout here makes that impossible: C: is the system volume (a
hard disk, with SOUND.DRV - the poll runs from the wakes a PLAYING stream
makes, so it needs a card), the player is B:\\APPS\\AUDIO.O88 and its
tracks are in B:\\MEDIA.

  1. B:\\MEDIA\\ONE.WAV opens one Audio Player, one track in its list.
  2. B:\\MEDIA\\TWO.WAV is handed off: the second instance says so and opens
     no window.
  3. Within two polls the first player has BOTH tracks, and there is still
     one Audio Player window.

Broken on purpose - the poll's ap_que_root back to GOTO_Q - 3 FAILS with one
track.
"""
import os
import struct
import subprocess
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M, os88ui, os88build, os88geom as geom   # noqa: E402
from cycweb import pkg_syms                                    # noqa: E402

MACHINE = "os8088_5150_herc_hdd_sb_gla"
TEMPLATE = os.path.join(ROOT, "build/martypc/run/media/hdds/default_xtide.vhd")
SECS = 25
DEFS = ("AP_HANDOFF",)


def wav(path, hz, secs):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(11025)
        w.writeframes(bytes(128 + (40 if (i * hz * 2 // 11025) & 1 else -40)
                            for i in range(11025 * secs)))


def main():
    os.chdir(ROOT)
    bad = []
    tmp = os.path.join(ROOT, "build", "audhand-%d" % os.getpid())
    os.makedirs(tmp)
    try:
        binf, pkg = os.path.join(tmp, "a.bin"), os.path.join(tmp, "AUDIO.O88")
        subprocess.run(["nasm", "-f", "bin", "-w+error"] +
                       ["-D" + d for d in DEFS] +
                       ["-I", "apps/", "-I", "apps/audio/", "-o", binf,
                        "apps/audio/audio.asm"], check=True)
        subprocess.run([sys.executable, "tools/os88pkg.py", binf, "-o", pkg],
                       check=True, capture_output=True)
        one, two = os.path.join(tmp, "ONE.WAV"), os.path.join(tmp, "TWO.WAV")
        wav(one, 440, SECS)     # long enough to still be PLAYING
        wav(two, 660, 1)
        disk = os.path.join(tmp, "b.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", "APPS:" + pkg, "MEDIA:" + one,
                        "MEDIA:" + two], check=True, capture_output=True)
        syms, _ = pkg_syms("apps/audio/audio.asm",
                           ("apps/", "apps/audio/"), defines=DEFS)
        vhd = os.path.join(tmp, "c.vhd")
        subprocess.run(
            [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
             "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
             "--vbr", os88build.at("build/boothd.bin"),
             "--mbr", os88build.at("build/mbr.bin"),
             "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
             "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
             "--file", "SOUND.DRV=" + os88build.at("build/sound.drv")],
            check=True, capture_output=True)
        m = M.launch(None, apps=disk, machine=MACHINE,
                     extra=["--mount", "hd:0:" + vhd])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=240)

            def players():
                return [w for w in ui.windows() if "Audio" in w.title]

            def ntracks(w):
                rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                             geom.WIN_SIZE)
                base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4
                return m.read(base + syms["apl_n"], 1)[0]
            media = ui.path("B:/MEDIA")
            ui.open("ONE.WAV", expect=None)
            M.until(m, lambda mm: len(players()) == 1, "the first player",
                    poll=0.3, limit=300.0, guest=120.0)
            p = players()[0]
            M.until(m, lambda mm: ntracks(p) >= 1, "ONE.WAV in its list",
                    poll=0.3, limit=120.0, guest=30.0)
            print("   1: one player, %d track(s)" % ntracks(p))
            mw = ui._as_win(media.i)        # the player covers most of
            ui.mo.click(mw.x + 12, mw.y + 4)  # MEDIA's title: raise it by
            M.until(m, lambda mm: (ui.front() or p).i == mw.i,  # the corner
                    "MEDIA to the front", poll=0.3, limit=120.0, guest=20.0)
            ui.open("TWO.WAV", expect=None)
            try:
                txt = ui.wait_toast(says="Sent to Audio Player")
            except Exception as e:
                txt = "none (%s)" % str(e).split(".")[0]
            print("   2: the second double-click: toast %r, %d player "
                  "window(s)" % (txt, len(players())))
            if "Sent" not in txt:
                bad.append("2: TWO.WAV was not handed off: %s" % txt)
            try:
                M.until(m, lambda mm: ntracks(p) >= 2, "TWO.WAV to arrive",
                        poll=0.5, limit=300.0, guest=20.0)
            except M.MartyError:
                pass
            n, np_ = ntracks(p), len(players())
            print("   3: after the polls: %d track(s), %d player window(s)"
                  % (n, np_))
            if n != 2 or np_ != 1:
                bad.append("3: the running player has %d track(s) and there "
                           "are %d player windows - the handoff was not "
                           "picked up" % (n, np_))
        finally:
            m.close()
    finally:
        for f in os.listdir(tmp):
            os.remove(os.path.join(tmp, f))
        os.rmdir(tmp)
    for x in bad:
        print("   FAIL: %s" % x)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

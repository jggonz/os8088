#!/usr/bin/env python3
"""AN ENDING'S CLIP: full screen, played once, and on to the title (SPEC.md
125.3.1) - the owner's "like openings, can the clips joined to have an
option to use their own music and be fullscreen? Can we also join victory
to a clip that doesn't loop - and then that clip (via a fade, or a join) to
title?", on MartyPC's XT with a CGA.

    make vbattle && python3 tests/vbtending.py [--arm join|fade]

The demo battle, its enemy at 1 HP, an ffmpeg test pattern for its end
clip (a video, so that full screen is the encoder's whole 200 rows):

  join: victory JOINS a FULL-SCREEN clip with its OWN sound that PLAYS
        ONCE and then JOINS the title. The bundle says so (CF_ENDS and
        CF_FULL, no CF_LOOP; header 346 = EH_AGAINJ); on the glass the band
        goes down and the clip's picture fills its rows; when the clip is
        over the play starts again AT THE TITLE with NO FADE anywhere.
  fade: victory FADES (the canvas only) to a once-clip in the canvas, which
        then FADES (the whole screen) to the title.
  fullloop: victory FADES to a FULL-SCREEN end clip that LOOPS: the fade
        over the whole screen, the band down while it goes round.

Broken on purpose (vb_eloop_go not taking the once-clip's "then") the
battle holds at the clip's end and never reaches the title.
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
import vbtana as A          # noqa: E402
import vbtproj as P         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_xt_hdd_sb"
GS_MENU = 1
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def band_lit(vram, by=142):
    return sum(1 for y in range(by, 200) for x in vram[
        (y & 1) * 8192 + (y >> 1) * 80:(y & 1) * 8192 + (y >> 1) * 80 + 80]
        if x)


def project(tmp, arm):
    vid = os.path.join(tmp, "outro.mp4")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                    "testsrc2=size=320x240:rate=15", "-f", "lavfi", "-i",
                    "sine=f=330", "-t", "3", "-shortest", vid], check=True)
    vbp = os.path.join(tmp, "e.vbp")
    B.demo_project(vbp, bars=2)
    p = P.Project.load(vbp)
    p.set(("enemy", "hp"), 1)
    # the victory clip as a DROPPED video's is made: joining the idle loop,
    # not ending - the owner's "ending misses its loop's frame 0" (an
    # ending's clip is built as one, whatever it says)
    p.set(("clips", "victory", "joins"), True)
    p.set(("clips", "victory", "ends"), False)
    p.add_source("outro", path=vid)
    p.save()
    full = arm in ("join", "fullloop")
    n = len(A.screens(p, "outro", full=full))
    p.add_clip("outro", "outro", (0, n - 1), anchors=())
    snd = p.loop_sound("outro", (0, n)) if arm == "join" else None
    if arm == "fullloop":
        p.set_ending("victory", "fade", "outro", (0, n), None, full=True,
                     plays="loop")
    else:
        p.set_ending("victory", arm, "outro", (0, n), snd, full=full,
                     plays="once", then=arm)
    p.save()
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("join", "fade", "fullloop"),
                    default="join")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        p = project(tmp, a.arm)
        errs = [x for x in p.check() if x.level == "ERROR"]
        ok(not errs, "the project checks (%s)" % errs)
        data = B.build_project(p)
        b = B.Bundle(data)
        how, li = b.endings["victory"]
        fl = b.clips[li]["flags"]
        if a.arm == "fullloop":
            want, wthen = B.CF_LOOP | B.CF_FULL, 0
        else:
            want = (B.CF_ENDS | (B.CF_FULL | B.CF_OWNSND if a.arm == "join"
                                 else 0))
            wthen = 4 if a.arm == "join" else 3
        ok(fl & ~B.CF_JOINS == want and b.ends_then["victory"] == wthen,
           "%s: the bundle - the end clip's flags %02X (want %02X), then "
           "%d" % (a.arm, fl, want, b.ends_then["victory"]))
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

            def key(k):
                m.key(k, up=False)
                until(lambda: True, "a key", guest=0.06, poll=0.0)
                m.key(k, down=False)
                until(lambda: True, "a key", guest=0.25, poll=0.0)
            key("Enter")
            until(lambda: rb("vb_ready") == 1, "the play to start")
            until(lambda: rb("vb_tplay") == 1, "the title")
            key("Enter")
            until(lambda: rb("vb_state") == GS_MENU, "the menu", guest=240.0)
            key("Enter")                # Attack, at the 1-HP enemy
            until(lambda: rb("vb_over") == 1, "victory", guest=120.0)
            # from here to the title again, every look: whether a fade ran,
            # over how many rows, and what the band's rows show
            fades, lit, bandon = [], [], []
            seen_clip = False
            for _ in range(2000):
                m.pause()
                if rb("vb_fon"):
                    rows = rw("vb_frows")
                    if not fades or fades[-1] != rows:
                        fades.append(rows)
                if rb("vb_eplay"):
                    seen_clip = True
                    lit.append(band_lit(m.read(0xB8000, 16384)))
                    bandon.append(rb("vb_banddone"))
                back = rb("vb_tplay") == 1 and rb("vb_over") == 0
                m.run()
                if back or (a.arm == "fullloop" and len(lit) > 40):
                    break
                until(lambda: True, "a moment", guest=0.1, poll=0.0)
            if a.arm == "fullloop":
                ok(seen_clip and fades == [200] and lit and
                   max(lit[5:] or [0]) > 1500 and not any(bandon[2:]),
                   "fullloop: the WHOLE SCREEN faded (%s rows) to a "
                   "full-screen end loop going round, the band down (up to "
                   "%d bytes lit in its rows)" % (fades, max(lit or [0])))
            else:
                ok(back, "%s: after the clip, the play again AT THE TITLE"
                   % a.arm)
            if a.arm == "join":
                ok(seen_clip and not fades,
                   "join: victory JOINED the clip and the clip JOINED the "
                   "title - no fade anywhere (%s)" % fades)
                ok(lit and max(lit) > 1500 and not any(bandon[2:]),
                   "join: the clip FULL SCREEN - the band down, its rows "
                   "the picture's (up to %d bytes lit)" % max(lit or [0]))
            elif a.arm == "fade":
                ok(seen_clip and fades[:1] == [142] and 200 in fades,
                   "fade: the canvas faded to the clip, then the whole "
                   "screen to the title (fades over %s rows)" % fades)
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

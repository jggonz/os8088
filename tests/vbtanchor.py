#!/usr/bin/env python3
"""AN ACTION'S SOUND WITH ITS PICTURE, from an anchor NEARER THAN THE LEAD
(SPEC.md 126.3), on MartyPC's XT with a CGA and an SB.

    make vbattle && python3 tests/vbtanchor.py

The owner, on 86Box's pc5150: "the audio for a clip is often delayed by
~2s ... about 50% for bolt, and 25% for the others", every one of those a
clip with ONE anchor. An action's sound must start ahead of the card's
write position, so its anchor is the nearest at least [vb_dmin] frames on
(the lead's and two) - and when the clip's only anchor is nearer, the one
a LAP on: d = (k - now) mod N + N. The sound was booked for that; the
picture matched the anchor's FRAME and went in on the first pass, a lap
(24 frames, 1.6 s) ahead of its sound.

The demo battle, both characters on Attack alone, its clip given ONE
anchor, the enemy too strong to fall; Attack taken turn after turn at
idle frames spread round the loop. At each:

  - when the anchor is chosen, its distance in frames ([vb_bestd]): at
    least one turn must have taken the lap (d >= N), or this tested
    nothing;
  - when the picture enters the clip, the card's clock against the frame
    its sound was booked for ([vb_due] - [vb_apat]): within two frames,
    every time.

Broken on purpose (the anchor's countdown taken out of .anchored) the lap
turns' pictures go in ~24 frames ahead of their sound and this goes red.
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
import os88build, os88geom as geom, os88marty, os88ui   # noqa: E402
import os88vbt as B         # noqa: E402
import vbtproj as P         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_xt_hdd_sb"
GS_MENU = 1
TURNS = 14
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def s32(v):
    return v - (1 << 32) if v & (1 << 31) else v


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        vbp = os.path.join(tmp, "a.vbp")
        B.demo_project(vbp)
        p = P.Project.load(vbp)
        d = p.d
        d["seed"] = 7
        d["characters"][1]["menu"] = {"top": [{"action": "attack"}],
                                      "side": []}
        d["characters"][0]["menu"] = {"top": [{"action": "attack"}],
                                      "side": []}
        for c in d["characters"]:
            c["hp"] = 999
        d["enemy"]["hp"] = 9999
        p.set_anchors("ari_attack", [12])
        p.save()
        data = B.build_project(p)
        bd = B.Bundle(data)
        ci = [c["name"] for c in bd.clips].index("ARI_ATTA")
        nidle, spf = bd.nidle, bd.spf
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

            def rb(nm):
                return m.read(base + syms[nm], 1)[0]

            def rw(nm):
                return struct.unpack_from("<H", m.read(base + syms[nm], 2))[0]

            def rd(nm):
                return struct.unpack_from("<I", m.read(base + syms[nm], 4))[0]

            def until(cond, what, guest=120.0, poll=0.05):
                os88marty.until(m, lambda mm: cond(), what, poll=poll,
                                limit=900.0, guest=guest)

            def gnow():
                return int(m.status().get("cycles", 0)) / os88marty.GUEST_HZ

            def rest(secs):             # (the GUEST's seconds: an until on
                t0 = gnow()             # a condition already true returns
                until(lambda: gnow() - t0 >= secs, "a rest",  # at once)
                      guest=secs + 5.0, poll=0.0)

            def key(k):
                m.key(k, up=False)
                rest(0.06)
                m.key(k, down=False)
                rest(0.1)
            key("Enter")
            until(lambda: rb("vb_ready") == 1, "the play")
            rest(1.0)
            if rb("vb_tplay"):          # (a title: left on a key)
                key("Enter")
            seen = []           # (d, frames the picture led its sound by)
            for i in range(TURNS):
                until(lambda: rb("vb_state") == GS_MENU and
                      rb("vb_dirty") == 0, "a menu", guest=240.0)
                until(lambda: rw("vb_aring") == 0 and rw("vb_apring") == 0,
                      "the last clip's sound over", guest=30.0)
                # (spread round the loop: a rest a little longer each turn)
                rest(0.2 + (0.37 * i) % 1.6)
                key("Enter")
                until(lambda: rw("vb_pk") != 0xFFFF, "the anchor",
                      guest=30.0, poll=0.0)
                dd = rw("vb_bestd")
                until(lambda: rw("vb_pk") == 0xFFFF,   # (the entry drawn:
                      "the clip on the glass", guest=30.0, poll=0.0)  # its
                # frame 0 on the glass, the clip's sound due with it
                lead = s32((rd("vb_apat") - rd("vb_due")) & 0xFFFFFFFF)
                seen.append((dd, lead / float(spf)))
                print("     turn %2d: anchor %2d frames on, the picture "
                      "%+.1f frames ahead of its sound" % (
                          i, dd, lead / float(spf)), flush=True)
                until(lambda: rb("vb_state") != GS_MENU, "the turn",
                      guest=60.0)
            key("Escape")
        finally:
            m.quit()
    laps = [x for x in seen if x[0] >= nidle]
    ok(laps, "a turn took the anchor a LAP on (%d of %d, d >= %d)"
       % (len(laps), len(seen), nidle))
    worst = max(abs(x[1]) for x in seen) if seen else 99
    ok(seen and worst <= 2.0,
       "every turn's picture entered within two frames of its sound "
       "(worst %.1f; the lap turns %s)" % (
           worst, ", ".join("%+.1f" % x[1] for x in laps)))
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

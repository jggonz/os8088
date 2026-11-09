#!/usr/bin/env python3
"""A CLIP WITH ITS OWN SOUND, its sound WITH its picture (SPEC.md 126.3),
on MartyPC's XT with a CGA and an SB.

    make vbattle && python3 tests/vbtownsnd.py

The owner: "The sound in the opening, and ending (post-victory) video is
~0.3-0.5s late. It plays fine, its just playing late ... the sound mixed
into clips is now right ... So the only clips late are ones set to 'their
own sound'." Such a clip's sound was booked at the frame's own time, which
the sound ring - written the card's lead (0.37 s) ahead - had already
passed: it started at once and was heard a lead after its picture.

tests/vbtending.py's battle (--opening fade): the title faded into a first
opening with its own sound, and victory joining an end clip with its own
sound. While each plays, the record on the glass (its records less
[vb_cleft]) against the record HEARD (those its sound has fed, less what the
card has queued and not played - [vb_wpos] - [vb_cons32]): within two frames (a whole
frame on the glass against a sound mid-frame) - the opening at its start (its intro is too big for its ring and
noisy, and on the XT its picture stalls further on), the end clip every
look.

Broken on purpose (vb_abook booking at [vb_due] as before) the picture is
~5 frames ahead of its sound and this goes red.
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
import vbtending as E       # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_xt_hdd_sb"
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        p = E.project(tmp, "joinfade", opening="fade")
        data = B.build_project(p)
        b = B.Bundle(data)
        names = [c["name"] for c in b.clips]
        intro = names.index("INTRO")
        outro = [i for i, n in enumerate(names) if n.endswith("OUTRO")][0]
        spf = b.spf
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

            def until(cond, what, guest=120.0, poll=0.02):
                os88marty.until(m, lambda mm: cond(), what, poll=poll,
                                limit=900.0, guest=guest)

            def gnow():
                return int(m.status().get("cycles", 0)) / os88marty.GUEST_HZ

            def rest(secs):
                t0 = gnow()
                until(lambda: gnow() - t0 >= secs, "a rest",
                      guest=secs + 5.0, poll=0.0)

            def key(k):
                m.key(k, up=False)
                rest(0.06)
                m.key(k, down=False)

            def looks(clip, tag, n=8):
                """The picture's frame against the sound's, N looks"""
                until(lambda: rb("vb_cc") == clip and rw("vb_cfr") >= 3 and
                      rw("vb_aring") != 0, tag + " under way", guest=120.0)
                out = []
                for _ in range(n):
                    m.pause()
                    if rb("vb_cc") != clip or rw("vb_aring") == 0:
                        m.run()
                        break
                    # THE RECORD ON THE GLASS against THE RECORD HEARD: the
                    # sound's written place (records fed, and into the one
                    # it is in) less what the card has not played yet
                    nrec = b.clips[clip]["nrec"]
                    pic = nrec - rw("vb_cleft") - 1
                    wrote = nrec - rw("vb_aleft") + rw("vb_afpos") / float(spf)
                    q = (rd("vb_wpos") - rd("vb_cons32")) & 0xFFFFFFFF
                    if q & 0x80000000:      # (signed: a card a moment past
                        q -= 1 << 32        # what was written - an underrun)
                    queued = q / float(spf)
                    out.append(pic - (wrote - queued))
                    m.run()
                    rest(0.25)
                return out
            key("Enter")
            until(lambda: rb("vb_ready") == 1, "the play")
            until(lambda: rb("vb_tplay") == 1, "the title")
            rest(1.0)
            key("Enter")                    # the title FADES to the opening
            d1 = looks(intro, "the first opening")
            ok(d1 and abs(sorted(d1[:3])[1]) <= 2.0,   # (its START, the
                                    # median of three: the intro is streamed
                                    # and noisy, an underrun there now and
                                    # then a single look off, and on an
                                    # XT its picture stalls further on)
               "the first opening (its own sound, after the title's fade): "
               "the picture against its sound %s frames (the stream reopened "
               "%d times)" % (["%+.1f" % x for x in d1], rw("vb_nended")))
            until(lambda: rb("vb_state") == 1 and rb("vb_dirty") == 0,
                  "the menu", guest=240.0)
            key("Enter")                    # Attack, at the 1-HP enemy
            d2 = looks(outro, "the end clip")
            ok(d2 and max(abs(x) for x in d2) <= 2.0,
               "the end clip (its own sound, victory joined to it): the "
               "picture against its sound %s frames"
               % ["%+.1f" % x for x in d2])
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

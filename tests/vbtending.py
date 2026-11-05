#!/usr/bin/env python3
"""AN ENDING'S CLIP: full screen, played once, and on to the title (SPEC.md
125.3.1) - the owner's "like openings, can the clips joined to have an
option to use their own music and be fullscreen? Can we also join victory
to a clip that doesn't loop - and then that clip (via a fade, or a join) to
title?", on MartyPC's XT with a CGA.

    make vbattle && python3 tests/vbtending.py [--arm join|fade|fullloop|
        joinfade] [--herc] [--noisy] [--secs N]

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

  joinfade: victory JOINS a once-clip with its OWN sound, which then FADES
        (the whole screen) to the title - the owner's, on Hercules with
        --herc, and with --noisy a picture that changes all over every
        frame, so that the clip is bigger than its ring and is STREAMED.

  --opening fade|leave: a FIRST OPENING of its own, full screen, its own
        sound, streamed - after the title's fade, or the title's way out
        (entered at its frames) - played to its end WITH its sound, and the
        play on to the menu. --delay places Enter in the title.

Every fade is held under 4 guest s (16 frames: ~1 s, paced by the hook's
clock).

Broken on purpose (vb_eloop_go not taking the once-clip's "then") the
battle holds at the clip's end and never reaches the title; the joined
sound skipping [vb_lbase] (a loop's) rather than the record the picture
skipped, joinfade --herc --noisy plays static and freezes at frame 54 of
135; a fade's step a pass of the loop, the fades take 8 s (XT) and 23 s
(Hercules); the next opening started at once whatever its ring held,
--opening leave --delay 1.2 freezes about one run in two (the second
opening at frame 17 of 19, its ring still the title's: VBTE_TRACE=1 prints
both rings as it goes).
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
HMACHINE = "os8088_5150_herc_hdd_sb_gla"
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


def project(tmp, arm, secs=3, noisy=False, opening=False):
    vid = os.path.join(tmp, "outro.mp4")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                    "testsrc2=size=320x240:rate=15" + (",noise=alls=100:allf=t"
                                                      if noisy else ""),
                    "-f", "lavfi", "-i",
                    "sine=f=330", "-t", str(secs), "-shortest", vid], check=True)
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
    if opening:
        # A FIRST OPENING of its own, full screen, its OWN sound, too big for
        # its ring (the owner's on Hercules: "sometimes the first opening
        # clip is freezing ... it would start with no sound")
        iv = os.path.join(tmp, "intro.mp4")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                        "-i", "testsrc2=size=320x240:rate=15,"
                        "noise=alls=40:allf=t", "-f", "lavfi", "-i",
                        "sine=f=220", "-t", "8", "-shortest", iv],
                       check=True)
        p.add_source("intro", path=iv)
        k = len(A.screens(p, "intro", full=True))
        p.add_clip("intro", "intro", (0, k - 1), anchors=())
        isnd = p.loop_sound("intro", (0, k))
        if opening == "leave":      # THE TITLE'S WAY OUT: entered at its
            p.set(("clips", "intro", "cues"),    # frames, no fade
                  [{"sound": isnd, "frame": 0}])
            t = p.d["title"]
            p.set_title(t["clip"], t["loop"], t["sound"], leave="intro",
                        anchors=range(0, t["loop"][1] - t["loop"][0], 3))
        else:
            p.add_opening("intro", isnd)
            p.move_opening(len(p.d["openings"]) - 1, -99)
            p.set_opening(0, full=True)
        p.save()
    full = arm in ("join", "fullloop")
    n = len(A.screens(p, "outro", full=full))
    p.add_clip("outro", "outro", (0, n - 1), anchors=())
    snd = p.loop_sound("outro", (0, n)) if arm in ("join", "joinfade") \
        else None
    if arm == "joinfade":
        p.set_ending("victory", "join", "outro", (0, n), snd, full=False,
                     plays="once", then="fade")
    elif arm == "fullloop":
        p.set_ending("victory", "fade", "outro", (0, n), None, full=True,
                     plays="loop")
    else:
        p.set_ending("victory", arm, "outro", (0, n), snd, full=full,
                     plays="once", then=arm)
    p.save()
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("join", "fade", "fullloop",
                                       "joinfade"),
                    default="join")
    ap.add_argument("--secs", type=int, default=3)
    ap.add_argument("--delay", type=float, default=0.0,
                    help="guest seconds of the title before Enter")
    ap.add_argument("--noisy", action="store_true",
                    help="a picture that changes all over every frame: a "
                    "clip bigger than its ring")
    ap.add_argument("--opening", choices=("fade", "leave"),
                    help="a first opening of its own: full screen, its own "
                    "sound, streamed - after the title's fade, or the "
                    "title's way out (entered at its frames)")
    ap.add_argument("--herc", action="store_true",
                    help="on MartyPC's Hercules 5150, its own rendition")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        p = project(tmp, a.arm, a.secs, a.noisy, a.opening)
        errs = [x for x in p.check() if x.level == "ERROR"]
        ok(not errs, "the project checks (%s)" % errs)
        if a.herc:
            p.set(("target", "renditions"), ["cga4", "herc"])
            p.save()
        data = B.build_project(p)
        hdata = B.build_project(p, kind="herc") if a.herc else None
        b = B.Bundle(hdata or data)
        how, li = b.endings["victory"]
        fl = b.clips[li]["flags"]
        if a.arm == "fullloop":
            want, wthen = B.CF_LOOP | B.CF_FULL, 0
        else:
            want = (B.CF_ENDS | (B.CF_FULL | B.CF_OWNSND if a.arm == "join"
                                 else B.CF_OWNSND if a.arm == "joinfade"
                                 else 0))
            wthen = 4 if a.arm == "join" else 3
        ok(fl & ~B.CF_JOINS == want and b.ends_then["victory"] == wthen,
           "%s: the bundle - the end clip's flags %02X (want %02X), then "
           "%d" % (a.arm, fl, want, b.ends_then["victory"]))
        vbt = os.path.join(tmp, "BATTLE.VBT")
        open(vbt, "wb").write(data)
        hvbt = os.path.join(tmp, "H.VBT")
        if hdata:
            open(hvbt, "wb").write(hdata)
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
             "--file", "BATTLE.VBT=" + vbt] + (
                 ["--file", "%s=%s" % (B.BUNDLE_NAMES["herc"], hvbt)]
                 if a.herc else []), check=True, capture_output=True)
        m = os88marty.launch(None, machine=HMACHINE if a.herc else MACHINE,
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
            if a.delay:                 # (Enter at a chosen moment of it)
                until(lambda: True, "a moment", guest=a.delay, poll=0.0)
            if a.opening:
                oi = b.openings[0]
                onrec = b.clips[oi]["nrec"]
                ofr, osnd = 0, False
                key("Enter")
                last = None
                for _ in range(3000):
                    if rb("vb_state") == GS_MENU:
                        break
                    m.pause()
                    if os.environ.get("VBTE_TRACE"):
                        st = [rb("vb_cc"), rb("vb_mode"), rw("vb_cring"),
                              rw("vb_aring"), rb("vb_apclip"),
                              rb("vb_onext")]
                        for rg in ("vb_rg0", "vb_rg1"):
                            r = m.read(base + syms[rg], 28)
                            st += [r[2], r[3], r[26], r[27]]
                        if st != last:
                            print("T %.2f cc mode cring aring apclip onext | "
                                  "rg0 clip play vd ad | rg1 ... %s" % (
                                      int(m.status().get("cycles", 0)) /
                                      os88marty.GUEST_HZ, st), flush=True)
                            last = st
                    if rb("vb_cc") == oi and rb("vb_mode") == 1:
                        ofr = max(ofr, rw("vb_cfr"))
                        osnd = osnd or rb("vb_apclip") == oi and \
                            rw("vb_aring") != 0
                    m.run()
                    until(lambda: True, "a moment", guest=0.05, poll=0.0)
                ok(ofr >= onrec - 2 and osnd,
                   "the FIRST OPENING, full screen with its own sound and "
                   "streamed, played to its end (frame %d of %d records), "
                   "its sound heard (%s)" % (ofr, onrec, osnd))
            else:
                key("Enter")
            try:
                until(lambda: rb("vb_state") == GS_MENU, "the menu",
                      guest=60.0 if a.opening else 240.0)
            except os88marty.MartyError:
                m.pause()
                print("STALL cc=%d mode=%d cfr=%d cleft=%d aring=%04X "
                      "apring=%04X apclip=%d mon=%d bandok=%d state=%d "
                      "onext=%d" % (rb("vb_cc"), rb("vb_mode"), rw("vb_cfr"),
                                    rw("vb_cleft"), rw("vb_aring"),
                                    rw("vb_apring"), rb("vb_apclip"),
                                    rb("vb_mon"), rb("vb_bandok"),
                                    rb("vb_state"), rb("vb_onext")))
                for rg in ("vb_rg0", "vb_rg1"):
                    r = m.read(base + syms[rg], 90)
                    u = lambda o, n=2: int.from_bytes(r[o:o + n], "little")
                    print("%s @%04X clip=%d play=%d fill=%d voff=%d aoff=%d "
                          "vdone=%d adone=%d poff=%d bytes=%d foff=%d" % (
                              rg, syms[rg], r[2], r[3], u(4, 4), u(10, 4),
                              u(18, 4), r[26], r[27], u(44, 4), u(56, 4),
                              u(60, 4)))
                print("lens", [b.clips[o].get("len") for o in b.openings])
                print("openings", b.openings, [b.clips[o]["nrec"]
                                                for o in b.openings])
                raise
            key("Enter")                # Attack, at the 1-HP enemy
            until(lambda: rb("vb_over") == 1, "victory", guest=120.0)
            # from here to the title again, every look: whether a fade ran,
            # over how many rows, and what the band's rows show
            fades, lit, bandon = [], [], []
            seen_clip = False
            cfrs = []
            def cyc():
                return int(m.status().get("cycles", 0))
            fon, fspans = False, []
            for _ in range(2000):
                m.pause()
                if rb("vb_fon") != fon:
                    fon = not fon
                    fspans.append(cyc())
                if rb("vb_fon"):
                    rows = rw("vb_frows")
                    if not fades or fades[-1] != rows:
                        fades.append(rows)
                if rb("vb_eplay"):
                    seen_clip = True
                    cfrs.append(rw("vb_cfr"))
                    lit.append(band_lit(m.read(0xB8000, 16384)))
                    bandon.append(rb("vb_banddone"))
                back = rb("vb_tplay") == 1 and rb("vb_over") == 0
                m.run()
                if back or (a.arm == "fullloop" and len(lit) > 40):
                    break
                until(lambda: True, "a moment", guest=0.1, poll=0.0)
            secs = [(fspans[i + 1] - fspans[i]) / os88marty.GUEST_HZ
                    for i in range(0, len(fspans) - 1, 2)]
            if secs:
                # A FADE OF 16 FRAMES (~1 s) is paced by the hook's clock:
                # a step a pass of the loop - the reader's, the band's and
                # a frame's wait - made it 8 s on the XT, 23 on Hercules
                ok(max(secs) < 4.0, "the fades took %s guest s (under 4)"
                   % ["%.2f" % x for x in secs])
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
            elif a.arm == "joinfade":
                nrec = b.clips[li]["nrec"]
                ok(seen_clip and fades == [348 if a.herc else 200]
                   and max(cfrs or [0]) >= nrec - 2,
                   "joinfade: victory JOINED the once-clip, which played to "
                   "its end (frame %d of %d records) and then the whole "
                   "screen FADED to the title (fades over %s rows)"
                   % (max(cfrs or [0]), nrec, fades))
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

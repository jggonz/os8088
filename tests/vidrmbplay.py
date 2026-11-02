#!/usr/bin/env python3
"""vidrmbplay - the Video Player CHOOSES its bracket (SPEC.md 98.3.24,
docs/plans/MEGA-VIDEO-REALMODE-PLAN.md W5).

    make mega && python3 tests/vidrmbplay.py [--keep] [--pkg F.o88 --map F.map]

kern_mega's arm asks OSAPI_FSX_RUN for FSXF_REAL whenever real mode serves
the play as well as protected mode does, and keeps the protected-mode
bracket for the plays the owner ruled stay there (5.3). The rows that play
through it (megavidring, megavidbig, the megaport vid* rows) say a play
WORKED; this one says WHICH bracket it worked in, two ways that do not lean
on each other: the kernel's own [fsx_rmode], polled while the play runs
(1 only inside a real-mode bracket), and the player's [vp_rwhy] after it -
the reason the info card's ninth line names (0 = real mode). Four machines
at once, each a Bochs of its own:

  spk   1MB, tests/vidplay.py's clip with PCM8 on the SPEAKER, in the window.
        REAL: [fsx_rmode] seen 1, [vp_rwhy] 0, every frame drawn - and the
        speaker library's chain is KERNEL_SEG:fsx_rtick, which only
        OSAPI_FSX_SPK's real-mode door (AL = 2 inside a real bracket,
        os88spk_go) ever writes there
  held  16MB, the big clip HELD in XMS and read in place under the speaker:
        PROTECTED (ruling 1, the speaker's clock kept fast) - [fsx_rmode]
        never 1, [vp_rwhy] VPR_SPK (12h), every frame drawn
  card  16MB with a Sound Blaster 16, the same held clip through the card:
        REAL, the hold read in place by a 386's loaded base with the yield
        sites live - [fsx_rmode] seen 1, [vp_rwhy] 0, every frame drawn
  live  1MB, a RESIDENT Live clip played on the desktop (no bracket at
        all) and then taken to the full screen with F: that bracket is
        PROTECTED (Live is the desktop's) - [fsx_rmode] never 1, [vp_rwhy]
        VPR_LIVE (11h)

and no kern_mega FAULT or CLOSED on any of them.

THE FAILING CASES (docs/WRITING-TESTS.md 1): --pkg with the W5 player of
02a5f114 (before the speaker's rebind) hangs spk and held in the bracket -
the shaper part far-called through a protected-mode CODE alias as a real
segment; and a player whose vp_rdoor never sets FSXF_REAL leaves spk and
card in protected mode, which the poll of [fsx_rmode] reads as 0.
Both verified when the row was written (the W5 entry).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]

import os88bochs  # noqa: E402
import vidplay  # noqa: E402
import megavidring  # noqa: E402

BUILD = os.path.join(ROOT, "build")
VMAP = os.path.join(BUILD, "megak", "apps", "video.map")
KSEG = 0x0060
VPR_LIVE, VPR_SPK = 0x11, 0x12
HOLD = 110                      # seconds for 1.26MB to fill the hold off B:
# leg: (MB, clip, sound card, wait for the hold, the bracket, vp_rwhy)
LEGS = {
    "spk": (1, "small", False, False, True, 0),
    "held": (16, "big", False, True, False, VPR_SPK),
    "card": (16, "big", True, True, True, 0),
    "live": (1, "live", False, False, False, VPR_LIVE),
}


def run(leg, disk, out, display):
    megs, _, sb, held, real, why = LEGS[leg]
    d = os.path.join(out, leg)
    os.makedirs(d)
    box = os88bochs.Box(d, [], bimg=disk, btype="1_44", megs=megs,
                        display=display, sb=sb, cfg=1 if sb else None)

    def bad():
        return box.faults()             # (MEGA_E9: off the kernel's state)

    def vb(label, n=1):
        return box.pkg_read("Video", label, n, VMAP)

    def word(label):
        b = vb(label, 2)
        return b[0] | b[1] << 8

    try:
        if not box.wait_pm(300):
            return "%s: kern_mega never reached protected mode" % leg
        rmode = box.sym("fsx_rmode")
        rtick = box.sym("fsx_rtick") - KSEG * 16
        box.up()
        box.capture()
        box.dbl(591, 105, "B:'s Disk window")
        box.dbl(170, 123, "the clip's open")
        box.until(lambda: bad() or any(i[1].startswith("Video")
                                       for i in box.instances()),
                  "the player", 60, poll=1.0)
        if held:
            box.until(lambda: bad() or vb("vp_xfull")[0],
                      "the hold to fill off B:", HOLD + 60, poll=1.0)
        elif not bad():
            box.ui_done("the poster", secs=120, lock=False)
        box.shot(os.path.join(d, "open.png"))
        if bad():
            return "%s: %s at the open" % (leg, bad()[0])
        if vb("vp_rwhy")[0] != 0xFF:
            return "%s: [vp_rwhy] %02Xh before any play - the row would " \
                   "read a stale answer" % (leg, vb("vp_rwhy")[0])
        box.xd("key", "space")
        if leg == "live":               # Live on the desktop, then F
            box.guest_sleep(2)
            box.xd("key", "f")
        seen = [False]

        def over():
            if box.read(rmode, 1)[0] == 1:
                seen[0] = True
            return bad() or vb("vp_played")[0]
        box.until(over, "the play to end", 240, poll=0.25)
        box.shot(os.path.join(d, "played.png"))
        if bad():
            return "%s: %s" % (leg, bad()[0])
        base = word("vp_base")
        drew, frames = word("vp_done") - base, word("vp_frames") - base
        got = vb("vp_rwhy")[0]
        msg = "%s (%dMB): drew %d of %d, %s bracket, [vp_rwhy] %02Xh" % (
            leg, megs, drew, frames, "a REAL" if seen[0] else "no real", got)
        if drew != frames or frames == 0:
            return msg + " - not every frame"
        if seen[0] != real:
            return msg + " - wanted %s" % ("real mode" if real
                                           else "protected mode")
        if got != why:
            return msg + " - wanted %02Xh" % why
        if leg == "spk":
            ch = vb("os88spk_chain", 4)
            off, seg = ch[0] | ch[1] << 8, ch[2] | ch[3] << 8
            msg += ", speaker chain %04X:%04X" % (seg, off)
            if (seg, off) != (KSEG, rtick):
                return msg + " - not the real-mode door's %04X:%04X" % (
                    KSEG, rtick)
        print("vidrmbplay: " + msg)
        return None
    finally:
        box.stop()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--pkg", help="the VIDEO.O88 to play (default: the "
                    "kern_mega arm's)")
    ap.add_argument("--map", help="--pkg's own video.map (its labels: "
                    "default the tree's)")
    ap.add_argument("--leg", action="append", choices=list(LEGS))
    a = ap.parse_args()
    global VMAP
    VMAP = a.map or VMAP
    legs = a.leg or list(LEGS)
    out = os.path.join(BUILD, "bochs-vidrmbplay")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    tmp = tempfile.mkdtemp(prefix="vidrmbplay-")
    t0 = time.time()
    res = {}
    try:
        clips = {}
        disks = {}
        for leg in legs:
            kind = LEGS[leg][1]
            if kind not in clips:
                sub = os.path.join(tmp, "c-" + kind)
                os.makedirs(sub)
                clips[kind] = (vidplay.clip(sub, "lin80", big=True)
                               if kind == "big" else
                               megavidring.clip(sub, "lin80"
                                                if kind == "small" else kind))
            stage = os.path.join(tmp, leg)
            os.makedirs(stage)
            shutil.copy(a.pkg or os.path.join(BUILD, "megak", "apps",
                                              "video.o88"),
                        os.path.join(stage, "VIDEO.O88"))
            disks[leg] = os.path.join(out, leg + ".img")
            subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                         "os88disk.py"),
                            "-o", disks[leg], "--size", "1440",
                            os.path.join(stage, "VIDEO.O88"), clips[kind]],
                           check=True, stdout=subprocess.DEVNULL)

        def go(leg, disp):
            try:
                res[leg] = run(leg, disks[leg], out, disp)
            except BaseException as e:      # (reported, never swallowed)
                res[leg] = "%s: %r" % (leg, e)
        ths = [threading.Thread(target=go, args=(leg, os88bochs.take_display()
                                                 if i else None))
               for i, leg in enumerate(legs)]
        for th in ths:
            th.start()
        for th in ths:
            th.join()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    fails = [res[leg] for leg in legs if res.get(leg)]
    missing = [leg for leg in legs if leg not in res]
    print("vidrmbplay: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    for leg in missing:
        print("FAIL: %s: no verdict" % leg)
    if not fails and not missing and not a.keep:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails or missing else 0


if __name__ == "__main__":
    sys.exit(main())

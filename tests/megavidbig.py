#!/usr/bin/env python3
"""A BIGSP video (SPEC.md 98.1.4.1) played from XMS on kern_mega without a
fault (docs/plans/KERN-MEGA-PLAN.md 14.58).

    python3 tests/megavidbig.py              the row
    python3 tests/megavidbig.py --keep       ...and leave the E9 logs and shots
    python3 tests/megavidbig.py --pkg F.o88  play another VIDEO.O88 (the one
                                             before a change)

Under protected mode the Video Player reads a held file, or its XMS bank, IN
PLACE through OSAPI_XMEM_RUN's windows: one every 32 KB, each 64 KB long
(SPEC.md 98.3.18.16). A record is whole in its window only when it starts no
further into its 32 KB than 64 KB less its length - true of every record of
an ordinary file (32 KB at most) and false of most of a BIGSP file's, whose
records run to 63.5 KB. The decoder then ran off the window's end: SI wrapped
to the window's start and the frame was drawn out of the wrong bytes until a
store ran past the screen or a word read sat at FFFFh - a #GP in the player
inside its fullscreen bracket, the screen trashed and the machine stopped.
That is the owner's Mechwarrior 2 (apps/video/demo/vga/04-MECH2.V88) on the
Victory 286, freezing at its first records past 32 KB, frame 170. The player
now plays such a file by copying (`vp_pmbigq`), as the ring of one already
did (vp_rhiq).

The clip is tests/vidplay.py's --big one (lin80, 640 x 480, whole-screen
noise records of 38,400 bytes, super-packets to 94 sectors) on a 1.44MB B:
beside VIDEO.O88. Two boots of build/mega.img under Bochs, each playing it
once with Space, fullscreen:

  bank   2MB: too little XMS to hold the file, so it is BANKED - and the bank
         reads chunks during the play, so Play is pressed at once;
  held   16MB: the whole file HELD, after the hold has had time to fill off
         the floppy (HOLD seconds) - a Play before that copies anyway.

Each must end with no kern_mega FAULT (a FAULT inside the bracket leaves the
machine stopped on a trashed screen; --keep keeps the shots that show it).

THE FAILING CASE (docs/WRITING-TESTS.md 1): VIDEO.O88 from before 14.58
(`--pkg`, or the two `call vp_pmbigq` lines taken out): both legs FAULT
within seconds of Play - `#GP` at vd_native's `rep movsw`, DS an LDT run
selector, SI FFFFh or DI past the screen. Verified both ways when the row was
written.

It reads build/ (wants=) and writes only under build/bochs-megavidbig/.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88bochs  # noqa: E402
import os88vid as vid  # noqa: E402
import vidplay  # noqa: E402

BUILD = os.path.join(ROOT, "build")
HOLD = 110                      # seconds for 1.26MB to fill the hold off B:
PLAY = 30                       # 150 frames at 30 fps, and its settle


def leg(name, megs, disk, out, wait):
    os88bochs.MEGS = megs
    d = os.path.join(out, name)
    os.makedirs(d)
    box = os88bochs.Box(d, [], bimg=disk, btype="1_44")
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            return "%s: kern_mega never reached protected mode" % name
        time.sleep(25)
        box.capture()
        os88bochs.dbl(591, 105)              # B:
        time.sleep(10)
        os88bochs.dbl(170, 123)              # CLIP.V88, the first row
        time.sleep(wait)
        os88bochs.shot(os.path.join(d, "open.png"))
        os88bochs.xd("key", "space")
        time.sleep(PLAY)
        os88bochs.shot(os.path.join(d, "play.png"))
        os88bochs.xd("key", "Escape")
        time.sleep(5)
        os88bochs.shot(os.path.join(d, "after.png"))
        fault = [l for l in box.lines() if l.startswith("kern_mega FAULT")]
        if fault:
            return "%s (%dMB): %s" % (name, megs, fault[0])
        print("megavidbig: %-4s %2dMB played, no fault" % (name, megs))
        return None
    finally:
        box.stop()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--pkg", help="the VIDEO.O88 to play (default: the tree's)")
    a = ap.parse_args()
    out = os.path.join(BUILD, "bochs-megavidbig")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    tmp = tempfile.mkdtemp(prefix="megavidbig-")
    t0 = time.time()
    try:
        v88 = vidplay.clip(tmp, "lin80", big=True)
        r = vid.Reader(v88)
        if not r.rmax > 32768:
            print("FAIL: the clip has no record past 32 KB (%d)" % r.rmax)
            return 1
        stage = os.path.join(tmp, "VIDEO.O88")
        shutil.copy(a.pkg or os.path.join(BUILD, "megak", "apps",  # the ARM
                                          "video.o88"), stage)      # (14.66)
        disk = os.path.join(out, "big.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "1440", stage, v88],
                       check=True, stdout=subprocess.DEVNULL)
        fails = [f for f in (leg("bank", 2, disk, out, 3),
                             leg("held", 16, disk, out, HOLD)) if f]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("megavidbig: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if not fails and not a.keep:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

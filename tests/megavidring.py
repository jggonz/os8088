#!/usr/bin/env python3
"""The Video Player's kern_mega arm on a 1MB machine - every claim
CONVENTIONAL and every one an LDT selector (KERN-MEGA-MEMORY-PLAN MC7).

    python3 tests/megavidring.py             the row: four legs at once
    python3 tests/megavidring.py --keep      ...and leave the shots
    python3 tests/megavidring.py --pkg F.o88 play another VIDEO.O88

Since MC7's step 6 a claim + N paragraphs names some other descriptor, and
the identity map that made the arm's 8-paragraph grid work stops at the
kernel's own segments. The arm names a claim's parts through OSAPI_SEG now:
the ring's slots one SEG_SUB each (vp_rsmk), a planar image's planes
(vp_plmk), the layer's slots (vp_lymk), a walk's record through a SEG_AT
window (vp_at), and in the hook a resident play's records through bound
windows SEG_AIM re-aims (vp_rres). megavidbig and megavidv2 play at 2MB and
16MB, where the ring is above 1MB or the file held in place; a 1MB machine
has no pool, so this is the CONVENTIONAL ring and nothing else. Four clips,
each a Bochs machine of its own at 1MB, each opened from B: (the poster:
the key read and decoded through vp_kpic) and played to its end:

  lin80   tests/vidplay.py's clip, PCM8 on the speaker, played IN THE
          WINDOW with Space: the desktop's frame buffer is a SEG_WIN window
  vga4    a 16-colour clip (four bit-planes, vp_plset) played FULL SCREEN
          (F, then Space)
  layer   a vga clip with a LAYER (98.1.9), the disk test's words zeroed and
          key 0's CPU verdict poked in as tests/vidlyplay.py does, so the
          layer is read out of the ring's top - its slots, mirror and
          bounce vp_lymk's
  live    a RESIDENT Live clip (98.1.7, 98.3.10): its block read whole, its
          positions paragraph offsets the hook aims a window at

Each must reach [vp_played] with every frame drawn ([vp_done] =
[vp_frames] - [vp_base], the card's `Drew N/N`) and no kern_mega CLOSED or
FAULT; the layer's must have drawn layer records and missed none.

THE FAILING CASE (docs/WRITING-TESTS.md 1): --pkg with VIDEO.O88 from
before the conversion (41475cad): every leg is CLOSED at its open - #GP at
vp_kpic's or vp_thpic's record segment, the grid's sum. Verified when the
row was written.

It needs ffmpeg (the clips are encoded here), reads build/ and writes only
under build/bochs-megavidring/.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88bochs  # noqa: E402
import os88vid as vid  # noqa: E402
import vidplay  # noqa: E402

BUILD = os.path.join(ROOT, "build")
VMAP = os.path.join(BUILD, "megak", "apps", "video.map")
LEGS = ("lin80", "vga4", "layer", "live")


def syms():
    out = {}
    for ln in open(VMAP):
        p = ln.split()
        if len(p) == 3:
            try:
                out[p[2]] = int(p[1], 16)
            except ValueError:
                pass
    return out


def life(tmp, name, args):
    src = os.path.join(tmp, name + ".mkv")
    subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                    "life=s=400x200:mold=10:r=15:ratio=0.4:seed=5088:"
                    "death_color=#000000:life_color=#ffffff,"
                    "trim=duration=6", "-c:v", "ffv1", src], check=True)
    out = os.path.join(tmp, name.upper() + ".V88")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                 "os88venc.py"),
                    src, out, "--quiet", "--fps", "15"] + args, check=True)
    return out


def clip(tmp, leg):
    if leg == "lin80":
        return vidplay.clip(tmp, "lin80")
    if leg == "vga4":
        return life(tmp, leg, ["--preset", "vga4", "--audio", "none"])
    if leg == "live":
        return life(tmp, leg, ["--preset", "live-vga", "--live", "vga",
                               "--audio", "none"])
    v = life(tmp, leg, ["--preset", "vga", "--audio", "none", "--disk",
                        "12000", "--reserve", "32", "--layer-disk", "40000",
                        "--layer-memory", "128"])
    d = bytearray(open(v, "rb").read())   # (the decode, not the disk test:
    struct.pack_into("<HH", d, vid.H_LYBKS, 0, 0)    # tests/vidlyplay.py)
    open(v, "wb").write(d)
    return v


def run(leg, disk, out, display, sy):
    d = os.path.join(out, leg)
    os.makedirs(d)
    box = os88bochs.Box(d, [], bimg=disk, btype="1_44", megs=1,
                        display=display)

    def bad():
        return box.faults()

    def word(label):
        b = box.pkg_read("Video", label, 2, VMAP)
        return b[0] | b[1] << 8

    try:
        if not box.wait_pm(300):
            return "%s: kern_mega never reached protected mode" % leg
        box.up()
        box.capture()
        box.dbl(591, 105, "B:'s Disk window")
        box.dbl(170, 123, "the clip's open")
        box.until(lambda: bad() or any(i[1].startswith("Video")
                                       for i in box.instances()),
                  "the player", 60, poll=1.0)
        if not bad():
            box.ui_done("the poster", secs=120, lock=False)
        box.shot(os.path.join(d, "open.png"))
        if bad():
            return "%s: %s at the open" % (leg, bad()[0])
        if leg == "layer":                  # key 0's CPU verdict, kept and
            base = box.pkg_base("Video")    # passed (tests/vidlyplay.py)
            box.dbg(["setpmem 0x%x 1 1" % (base + sy["vp_lycv"])])
        if leg == "vga4":
            box.xd("key", "f")              # full screen, paused...
            box.until(lambda: bad() or box.in_fsx(), "the full screen", 60,
                      poll=1.0)
            box.guest_sleep(3)          # GUEST seconds (Box.guest_sleep)
        box.xd("key", "space")              # ...and played (in the window
        box.until(lambda: bad() or box.pkg_read(        # where it can be)
            "Video", "vp_played", 1, VMAP)[0], "the play to end", 240,
            poll=1.0)
        box.shot(os.path.join(d, "played.png"))
        if bad():
            return "%s: %s" % (leg, bad()[0])
        base = word("vp_base")              # (the card's "Drew N/M": a
        drew = word("vp_done") - base       # play from a key counts from it)
        frames = word("vp_frames") - base
        msg = "%s: drew %d of %d" % (leg, drew, frames)
        if leg == "layer":
            got, mis = word("vp_lygot"), word("vp_lymis")
            msg += ", layer drew %d, missed %d" % (got, mis)
            if got == 0 or mis:
                return msg + " - the layer was not played"
        if drew != frames or frames == 0:
            return msg + " - not every frame"
        print("megavidring: " + msg)
        return None
    finally:
        box.stop()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--pkg", help="the VIDEO.O88 to play (default: the "
                    "kern_mega arm's)")
    ap.add_argument("--leg", action="append", choices=LEGS)
    a = ap.parse_args()
    legs = a.leg or LEGS
    out = os.path.join(BUILD, "bochs-megavidring")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    tmp = tempfile.mkdtemp(prefix="megavidring-")
    sy = syms()
    t0 = time.time()
    res = {}
    try:
        disks = {}
        for leg in legs:
            v = clip(tmp, leg)
            stage = os.path.join(tmp, leg)
            os.makedirs(stage)
            shutil.copy(a.pkg or os.path.join(BUILD, "megak", "apps",
                                              "video.o88"),
                        os.path.join(stage, "VIDEO.O88"))
            disks[leg] = os.path.join(out, leg + ".img")
            subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                         "os88disk.py"),
                            "-o", disks[leg], "--size", "1440",
                            os.path.join(stage, "VIDEO.O88"), v],
                           check=True, stdout=subprocess.DEVNULL)

        def go(leg, disp):
            try:
                res[leg] = run(leg, disks[leg], out, disp, sy)
            except Exception as e:      # (reported, never swallowed)
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
    print("megavidring: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if not fails and not a.keep:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

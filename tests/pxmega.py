#!/usr/bin/env python3
"""PiXEL on kern_mega with NO #GP fixup: every sample picture opened, an
animated GIF played and the editor's Rotate both ways, under MEGATRAP=1's
census (docs/plans/KERN-MEGA-PLAN.md 14.40).

    python3 tests/pxmega.py            the row
    python3 tests/pxmega.py --keep     ...and leave the E9 log and the shots

kern_mega COMPLETES a store through a code selector and a far transfer
through a data segment instead of faulting (mega_gpw, mega_gpfix), and a
segment off its 8-paragraph grid is a FAULT. PiXEL had all three: 55,481
fixups opening BALLOONS.PNG, a FAULT on an animated GIF's frame, on a
progressive JPEG, on Rotate and on Blur. tests/megatrap0.py starts packages
and counts what their launch costs; a decoder's cost is in USING it, so this
row opens pictures. It boots `make megatrap`'s kernel (a private tree) under
Bochs - the one emulator here that enforces a 286's segment rules
(docs/BOCHS.md) - with B: holding PIXEL.O88 and apps/pixel/samples, double-
clicks BALLOONS.PNG, walks the folder with Space (BOUNCE.GIF, CAT.GIF,
CITY.PCX, FLOWER.JPG, HOUSE.PNG, LAKE.JPG, MOUNTAIN.BMP, ROOM.JPG: every
decoder part and the plan search on a 4bpp display), then picks the Rotate
tool and clicks the canvas with each button. Every step must add NO trap
line and no kern_mega FAULT, and leave a canvas that is a picture (more
than its ground) and not the one before it.

THE FAILING CASE (docs/WRITING-TESTS.md 1): put pxgif.asm's pa_comp back to
`and di, 15` (one paragraph) and BOUNCE.GIF's first frame is a FAULT; put
any `mov [es:...]` of pxplan.inc's pl_plan1 back to `[cs:...]` and
BALLOONS.PNG takes thousands of traps. Both were measured red when this was
written.
"""
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
import os88build  # noqa: E402

SAMPLES = ("BALLOONS.PNG", "BOUNCE.GIF", "CAT.GIF", "CITY.PCX", "FLOWER.JPG",
           "HOUSE.PNG", "LAKE.JPG", "MOUNTAIN.BMP", "ROOM.JPG")
CANVAS = (40, 75, 440, 430)             # the canvas, in a shot's pixels
QUIET = 12                              # seconds of no E9 line: settled
LIMIT = 300


def canvas(path):
    """The canvas's pixels, and the share of them that is NOT its ground
    (PXP_BG's dark grey, the commonest colour of a canvas with a picture
    narrower than it): a picture's, or nothing"""
    from PIL import Image
    im = Image.open(path).convert("RGB").crop(CANVAS)
    cols = sorted(im.getcolors(1 << 16) or [(0, None)], reverse=True)
    return im.tobytes(), 1.0 - cols[0][0] / float(im.size[0] * im.size[1])


def main():
    keep = "--keep" in sys.argv
    t = os88build.tree("MEGATRAP=1", targets=(
        "mega.img", "megarun.o88", "mbr.bin", "megak/apps/pixel.o88"))
    out = os.path.join(t.dir, "pxmega")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    disk = os.path.join(out, "pics.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "1440", t.img("megak/apps/pixel.o88")] +
                   [os.path.join(ROOT, "apps", "pixel", "samples", s)
                    for s in SAMPLES], check=True, stdout=subprocess.DEVNULL)
    t0 = time.time()
    box = os88bochs.Box(out, [], bimg=disk, btype="1_44", build=t.dir)
    fails = []
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            print("FAIL: kern_mega never reached protected mode")
            return 1
        time.sleep(25)
        box.capture()
        os88bochs.dbl(591, 105)          # B:
        time.sleep(10)
        prev = [None]

        def step(name, act, picture=True):
            a = os.path.getsize(box.e9)
            act()
            end, last, lt = time.time() + LIMIT, a, time.time()
            while time.time() < end:
                time.sleep(1)
                n = os.path.getsize(box.e9)
                if n != last:
                    last, lt = n, time.time()
                elif time.time() - lt >= QUIET:
                    break
            with open(box.e9, "rb") as f:
                f.seek(a)
                new = f.read().decode("latin-1").splitlines()
            traps = [l for l in new if l.startswith("T ")]
            fault = [l for l in new if l.startswith("kern_mega FAULT")]
            shot = os.path.join(out, name + ".png")
            os88bochs.shot(shot)
            px, pic = canvas(shot)
            print("pxmega: %-24s %5d traps%s, picture %2.0f%% of the canvas"
                  % (name, len(traps), " FAULT" if fault else "", 100 * pic))
            if fault:
                fails.append("%s: %s" % (name, fault[0]))
            if traps:
                fails.append("%s took %d #GP fixups, the first `%s` "
                             "(tools/megatraps.py %s)"
                             % (name, len(traps), traps[0], box.e9))
            if picture and (pic < 0.03 or px == prev[0]):
                fails.append("%s: the canvas is not a new picture (%.0f%% "
                             "of it not ground%s)" % (name, 100 * pic,
                             ", the same as the step before"
                             if px == prev[0] else ""))
            prev[0] = px

        step(SAMPLES[0], lambda: os88bochs.dbl(165, 123))
        for s in SAMPLES[1:]:
            step(s, lambda: os88bochs.xd("key", "space"))
        step("Rotate (clockwise)", lambda: (os88bochs.xd("key", "r"),
                                            os88bochs.goto(240, 250),
                                            os88bochs.xd("click", 1)))
        step("Rotate (anticlockwise)", lambda: os88bochs.xd("click", 3))
    finally:
        box.stop()
    print("pxmega: %d steps, %.0fs under Bochs" % (len(SAMPLES) + 2,
                                                  time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if not fails and not keep:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

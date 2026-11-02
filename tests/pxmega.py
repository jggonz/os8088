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
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
import os88build  # noqa: E402
import os88sym  # noqa: E402

PXR = dict(MW=32, MH=34, HAVE=43)       # px_cur's fields (tests/megapixel.py)


class Pixel(object):
    """PiXEL's own state, read through its descriptor by the package's map
    (tests/megapixel.py's Pixel, cut to what a step waits on: that module
    sets OS88_BUILD at import, so it is not imported here)."""

    def __init__(self, box, build):
        self.box = box
        self.map = os88sym._parse_map(os.path.join(build, "pixel.map"))[0]

    def read(self, label, n):
        return self.box.pkg_read("PiXEL", self.map[label], n)

    def byte(self, label):
        return self.read(label, 1)[0]

    def name(self):
        return self.read("px_cur", 13).split(b"\0")[0].decode("latin-1")

    def rec(self):
        r = self.read("px_cur", 44)
        return dict(MW=struct.unpack_from("<H", r, PXR["MW"])[0],
                    MH=struct.unpack_from("<H", r, PXR["MH"])[0],
                    HAVE=r[PXR["HAVE"]])

    def idle(self):
        return (self.byte("px_busy") == 0 and self.byte("px_job") == 0
                and self.byte("px_pcon") == 0)

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
    px = Pixel(box, t.dir)
    fails = []
    try:
        if not box.wait_pm(300):
            print("FAIL: kern_mega never reached protected mode")
            return 1
        # the desktop, B:'s window and every step below end on the GUEST's
        # state - each step used to wait out QUIET s of no E9 line, 12 s a
        # step on a clean run. The trap windows still tile the whole run end
        # to end, and the last one has a tail.
        #
        # A STEP ENDS ON PIXEL'S OWN WORD, then on the UI's. ui_done alone
        # is not "the picture is decoded": PiXEL's worker decodes and its UI
        # task paints between chunks (px_progpaint), so the UI task is idle
        # between two of them and ui_done returned with FLOWER.JPG's first
        # strip on the glass ("Opening 0%", 1% of the canvas). And a Space
        # whose open has not run yet leaves the PREVIOUS picture idle. So a
        # picture step waits for [px_cur]'s name to be the file and HAVE set
        # with [px_busy] clear - px_open clears HAVE before it names the
        # file and px_finish sets it, so the three together are only true of
        # THIS decode, finished - and a Rotate for the master's dimensions
        # turned and PiXEL idle (megapixel.py's settle, the same bytes).
        box.up()
        box.capture()
        box.dbl(591, 105, "B:'s Disk window")
        prev = [None]
        pos = [os.path.getsize(box.e9)]

        def step(name, act, ready, picture=True, tail=0.0):
            act()
            try:
                box.until(lambda: box.pkg_base("PiXEL") is not None and
                          ready(), "PiXEL to finish " + name, LIMIT,
                          poll=0.5)
                box.ui_done("PiXEL to paint " + name, secs=LIMIT)
            except RuntimeError as e:   # a FAULT: reported below, from E9
                print("pxmega: %s" % e)
            box.guest_sleep(tail)       # GUEST seconds (Box.guest_sleep)
            with open(box.e9, "rb") as f:
                f.seek(pos[0])
                t = f.read()
            t = t[:t.rfind(b"\n") + 1]     # (whole lines; the windows TILE)
            pos[0] += len(t)
            new = t.decode("latin-1").splitlines()
            traps = [l for l in new if l.startswith("T ")]
            fault = [l for l in new if l.startswith("kern_mega FAULT")]
            shot = os.path.join(out, name + ".png")
            box.shot(shot)
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

        def opened(s):
            return lambda: (px.rec()["HAVE"] == 1 and px.name() == s
                            and px.byte("px_busy") == 0)

        def turned():
            r = px.rec()
            want = (r["MH"], r["MW"])
            return lambda: (px.idle() and
                            (lambda q: (q["MW"], q["MH"]) == want)(px.rec()))

        step(SAMPLES[0], lambda: box.dbl(165, 123), opened(SAMPLES[0]))
        for s in SAMPLES[1:]:
            step(s, lambda: box.xd("key", "space"), opened(s))
        step("Rotate (clockwise)", lambda: (box.xd("key", "r"),
                                            box.goto(240, 250),
                                            box.xd("click", 1)), turned())
        step("Rotate (anticlockwise)", lambda: box.xd("click", 3), turned(),
             tail=QUIET / 4)
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

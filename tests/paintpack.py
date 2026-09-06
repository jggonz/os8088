#!/usr/bin/env python3
"""DOES THE REFUSAL PATH WORK? (SPEC.md 42.13.1)

    make && python3 tests/paintpack.py

Paint holds its canvas as four planes on a colour adapter and hands it to
`gfx_blitp`, which refuses a 1bpp adapter, an x off the byte grid, a straddled
seam and a kernel built without the planes. On `CF` Paint runs `pt_topacked`,
turns the picture back into nibbles and draws it through `gfx_blit4`.

**None of the four refusals is reachable by driving the shipped kernel.** The
window manager clamps a window to the desktop so the canvas never hangs off an
edge, the origin is snapped to the byte grid by construction, and this harness
has no machine with two cards in it. The fourth one is: `NOPLANE=1` takes the
decoder out and `gfx_blitp` becomes `stc` / `ret`, so the FIRST canvas blit
refuses and the fallback runs for real - which is the only way any of this is
exercised without a second monitor.

That matters more than the shape of the code suggests. A recovery path with no
test is not "probably fine": this one shipped with SEVEN pushes and SIX pops,
so `ret` took the saved AX - the canvas x - as its return address and the
machine executed the desktop as instructions. It was found by hand and it will
not be found by hand twice.

The comparison is tests/blitpair.py's, run against a VGA machine: OS8088.GIF
is two colours, so every pixel on the screen belongs to a solid class and the
canvas can be compared against THE FILE. This row only supplies the kernel.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88marty                                             # noqa: E402
import dispapps                                              # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--machine", default="os8088_xt_vga")
    a = ap.parse_args()

    # A COLOUR PICTURE, and it has to be said now: SPEC.md 42.23.6 opens a GIF
    # whose colour table has two entries ONE BIT DEEP on any adapter, and
    # build/OS8088.GIF has exactly two - so the fixture every picture row here
    # uses stopped being able to give this one a four-plane canvas.
    # dispapps.colour_gif appends two unused entries and changes not one
    # pixel, so every oracle below is the one it always was.
    gif = dispapps.colour_gif()

    # The tree is REBUILT, the way tests/blitplane.py rebuilds it, and the
    # `finally` is not decoration: `make NOPLANE=1` writes build/kernel.bin,
    # and a run that dies in the middle would leave every emulator row after
    # it driving a kernel nobody asked for - described perfectly by its own
    # symbol map, so nothing would complain.
    apps = "/tmp/paintpack.img"
    os88marty.scratch_disk(apps, "APPS:build/paint.o88",
                           "MEDIA:" + gif)

    print("   NOPLANE=1 kernel: building")
    try:
        subprocess.check_call(["make", "NOPLANE=1", a.image],
                              stdout=subprocess.DEVNULL)
        env = dict(os.environ, NOPLANE="1")
        rc = subprocess.call(
            [sys.executable, os.path.join(HERE, "blitpair.py"),
             "--image", a.image, "--machine", a.machine,
             "--apps", apps], env=env)
        # ...and once Paint is holding nibbles, the PACKED clipboard: a
        # planar canvas is what tests/paintbig.py normally drives, and this
        # is the only kernel on which the other half of pt_copy and pt_paste
        # runs at all without a second monitor (SPEC.md 42.13.3).
        if not rc:
            print("   ...and the packed clipboard, through gfx_blit4:")
            rc = subprocess.call(
                [sys.executable, os.path.join(HERE, "paintbig.py"),
                 "--image", a.image, "--machine", a.machine,
                 "--apps", apps, "--blit", "gfx_blit4"], env=env)
    finally:
        subprocess.check_call(["make"], stdout=subprocess.DEVNULL)
    if rc:
        sys.exit("paintpack: the canvas pt_topacked produced is NOT the "
                 "picture (or it never got there)")
    print("paintpack: gfx_blitp refused, pt_topacked converted, and the "
          "packed canvas draws, resizes and copies")
    return 0


if __name__ == "__main__":
    sys.exit(main())

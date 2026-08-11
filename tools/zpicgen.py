#!/usr/bin/env python3
"""Build the picture fixture's art: three flat blocks, and what to expect.

    python3 tools/zpicgen.py -o build/zpic          # `make zpic` runs this

WHY FLAT BLOCKS. tests/frotz/zpictest.inf draws these and then waits, and
tools/zharness.py photographs the screen and reads the pixels the interpreter
said it blitted into. A screendump can be held to a flat block exactly - every
pixel of the rectangle is one known colour, or the picture is wrong - and it
cannot be held to a photograph without the host learning to dither the same way
tools/os88pix.py does, which would be the code under test checking itself.

Each picture is therefore ONE palette colour, and three different ones: a
picture drawn at another's origin, or two drawn on top of each other, is then a
visibly different colour rather than a plausible one.

The sizes are all NON-SQUARE and all different. @picture_data answers height
first and width second (Standard 15), which is easy to get the wrong way round
and stays plausible when you do; nothing here is symmetrical enough to let that
through. The numbers are 1, 2 and 5 for the reason zpictest.inf gives.

The expectations go in a file beside the archive rather than being hard-coded
in the gate, because the archive and the gate would then be two statements of
the same fact, free to drift.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88pix                                  # noqa: E402  (for the palette)
import shot                                     # noqa: E402  (for the writer)

RELEASE = 7

# (picture number, palette index, width, height). The indices are CBLUE, CRED
# and CGREEN - three of the eight the 1bpp adapters do NOT have, which is fine:
# the fixture is a VGA check and says so, because on Hercules or CGA every one
# of them rounds to a dither and "solidly this colour" stops being a question
# with an answer (SPEC.md 39.4).
PICTURES = [
    (1, 1, 64, 32),
    (2, 4, 32, 16),
    (5, 2, 48, 24),
]


def flat_png(path, index, w, h):
    r, g, b = os88pix.PALETTE[index]
    shot.png(path, w, h, bytes([r, g, b]) * (w * h))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "build", "zpic"))
    ap.add_argument("--name", default="zpictest",
                    help="the story's base name; the archive is <name>.PIX, "
                         "which is what zp_mkname looks for beside it")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    spec = []
    for num, index, w, h in PICTURES:
        png = os.path.join(args.out, f"p{num}.png")
        flat_png(png, index, w, h)
        spec.append(f"{num}={png}")

    archive = os.path.join(args.out, args.name + ".PIX")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88pix.py"),
                    "-o", archive, "--release", str(RELEASE), *spec],
                   check=True)

    expect = os.path.join(args.out, args.name + ".expect")
    with open(expect, "w") as f:
        f.write("# picture, palette index, r, g, b, width, height.\n")
        f.write("# Written by tools/zpicgen.py; read by tools/zharness.py.\n")
        for num, index, w, h in PICTURES:
            r, g, b = os88pix.PALETTE[index]
            f.write(f"{num} {index} {r} {g} {b} {w} {h}\n")
    print(f"zpicgen: {len(PICTURES)} pictures -> {archive}, {expect}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

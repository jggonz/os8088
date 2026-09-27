#!/usr/bin/env python3
"""The .V88 file and its host tools - SPEC.md 98, VIDEO-PLAN wave 1.

    python3 tests/vidfmt.py [--samples DIR] [--target cga|herc|lin80]

Host-side, no emulator. Two halves:

1. `os88vid --selfcheck`: generated frames through `encode` on all three
   layouts (SPEC.md 98.1.2) - a black start, a white fill (RUNL), noise
   (SLICEL), isolated bytes (absolute P1..P6), runs and slices of every
   short length, a moving box, PCM8 audio on two of the four - and a
   synthetic XDC stream through `import --target` cga, herc and lin80.
   Every frame is decoded back through the keyframe at or before it and
   compared with its source, and four corruptions must each be refused FOR
   THEIR OWN REASON (a record past its super-packet, a truncated file, two
   keyframes swapped, a write outside the canvas).
2. With the owner's XDC streams (--samples DIR or $OS88_XDC_SAMPLES; they are
   not in the tree): every one imported, and `verify --against` holds the
   .V88 to XDC's screen AND audio after every frame, plus every writer's
   rule of SPEC.md 98.1.3 and every field a player reads (98.1.6).

Broken on purpose - spans merged across bytes outside the canvas, or every
keyframe stamped one frame early - the self-check FAILS naming the layout and
the frame; the record, truncation and keyframe corruptions are its own
negative controls.
"""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "tools", "os88vid.py")
STREAMS = ("BBBB_BW", "BBBBCOMP", "TRONDISC", "THUNDERC", "BADAPPLE")


def run(*args):
    r = subprocess.run([sys.executable, TOOL] + list(args),
                       capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    print("   " + out.replace("\n", "\n   "))
    return r.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", default=os.environ.get("OS88_XDC_SAMPLES"))
    ap.add_argument("--target", default="cga",
                    choices=("cga", "herc", "lin80"))
    a = ap.parse_args()
    bad = run("--selfcheck")
    if not a.samples:
        print("   (no --samples / $OS88_XDC_SAMPLES: the owner's streams are "
              "not in the tree, so only the self-check ran)")
        return bad
    with tempfile.TemporaryDirectory() as tmp:
        for n in STREAMS:
            src = os.path.join(a.samples, n + ".XDV")
            if not os.path.exists(src):
                print("   %s: not in %s, skipped" % (n, a.samples))
                continue
            out = os.path.join(tmp, n + ".V88")
            bad |= run("import", src, out, "--target", a.target)
            if not bad:
                bad |= run("verify", out, "--against", src)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

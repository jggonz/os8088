#!/usr/bin/env python3
"""What a Game Boy picture is made of, and what a lossy renderer loses.

    python3 tools/gblossy.py GAME.GB [--frames N] [--shots PREFIX F1 F2 ...]

The instrument behind docs/reports/REALDMG-LOSSY-2026-10-22.md. It runs the
game on tools/gbprof.py's pinned, patched Peanut-GB under gbprof's
platformer input script (or, --flap, the bare harness's Kirby script) and, at every line, composes the line again with
tools/gbprof/gblossy.c's own renderer - faithfully, checked against
Peanut-GB's pixels, and once per lossy variant (no background, no window,
no sprites, sprites opaque, the priority bit ignored, two shades) - and
prints which layer the pixels came from and how many each variant gets
wrong. --shots writes a PPM per named frame, the variants side by side:
faithful, opaque sprites, two shades, no background (window kept), and
sprites alone. NO ROM IS EVER COMMITTED.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gbprof  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("rom")
    ap.add_argument("--frames", type=int, default=3000)
    ap.add_argument("--flap", action="store_true",
                    help="the bare harness's Kirby input (A 6 frames of 12, Up and Right "
                         "held), as tests/rdgcore/rdgbench.py times it")
    ap.add_argument("--shots", nargs="+", metavar="ARG",
                    help="PREFIX then the frame numbers to write")
    a = ap.parse_args()
    gbprof.patch(gbprof.fetch())
    exe = os.path.join(gbprof.WORK, "gblossy")
    subprocess.check_call([os.environ.get("CC", "cc"), "-O2", "-w", "-I", gbprof.WORK,
                           "-o", exe, os.path.join(ROOT, "tools", "gbprof", "gblossy.c")])
    if a.flap:
        os.environ["GBLOSSY_FLAP"] = "1"
    sys.exit(subprocess.call([exe, a.rom, str(a.frames)] + (a.shots or [])))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""REALDMG.O88's speaker, asked for a pitch no speaker can play, on a 286.

    python3 tests/gbhifreq.py [--vm 286]

docs/REALDMG-SPEC.md section 17. GBTONE.GB (tools/gbprof/gbtone.asm, ours)
ends by triggering square channel 1 at x = 7FFh - 131,072 Hz, silent - and
then counts its frames at C000h. rw_sound works a channel's pitch out as
131,072 / (2048 - x), which divides by 1 there; the quotient does not fit in
16 bits and the CPU raises a divide error. The compatibility sweep's
Alleyway sets x = 7FEh when it pauses, and REALDMG.O88 hung.

It has to be a 286: an 8088's divide error returns AFTER the div, so the
package carries on with a wrong pitch and gbsound (MartyPC) passes either
way; a 286's returns TO it, the BIOS's handler is an IRET, and the machine
divides for ever. The row boots vm/<VM> through tests/rdgcore/rdgwin.py and
asserts the game passed the note (its count is above zero) and that the
core's frames kept climbing across five emulated seconds after it. Without rw_sound's
range check before the div it stops dead.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(ROOT)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import rdgwin  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--vm", default="286")
    a = ap.parse_args()
    rom = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "gbours.py")],
                                  text=True).strip().splitlines()[-1]
    o = argparse.Namespace(rom=rom, vm=a.vm, keep="", mem=None, big=False, rdpin="")
    b = rdgwin.boot(o)
    try:
        seg, view, core = rdgwin.find(b)
        b.cmd("step 6000")                      # past frame 150, the note
        c = b.memr(view * 16 + 0xC000, 1)[0]
        f0 = rdgwin.frames(b, view)
        b.cmd("step 5000")
        f1 = rdgwin.frames(b, view)
    finally:
        b.cmd("quit")
    run = (f1 - f0) & 0xFFFF
    print("gbhifreq: the ROM's count since the note at x = 7FFh %d; the core's frames %d -> %d "
          "(%d in 5 s)" % (c, f0, f1, run))
    if not c or run < 100:
        print("FAIL gbhifreq: the game stopped once channel 1 went to x = 7FFh")
        return 1
    print("gbhifreq: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

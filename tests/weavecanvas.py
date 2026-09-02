#!/usr/bin/env python3
"""The CANVAS core, diffed against tools/weavesim.py in raw QEMU (12.1.3).

    python3 tests/weavecanvas.py
    python3 tests/weavecanvas.py --timeout 120

A thin driver over apps/weave/hosttest/weavecv.sh, which is where the real
work is: generate the corpus from the model, assemble
apps/weave/hosttest/weavecv.asm around the SHIPPING apps/weave/wspr.inc and
apps/weave/wwork.inc, boot it from a boot sector with SS != DS and read the
verdict off COM1. The .asm's header says what each case checks.

WHY IT EXISTS BESIDE weavevm, AND IT IS NOT THE SAME REASON. weavevm diffs
two interpreters that both already had end states to compare. WEAVE-SPEC
6.10.2's composition had NO oracle at all: the model deliberately does not
draw pixels, and the canvas's buffer is not on any card - so a sprite composed
one byte to the left, or a dirty band run one band too short, is invisible in
every screenshot this family takes and arrives at a person as "the game
flickers". The model therefore grew a composer written from 6.10.2, and this
is the machine's half of it (12.1.3).

It is WAVE 5's FIRST gate, for wave 3's and wave 4's reason: a canvas wired to
an unverified composer reports its defects as component defects.

WHY QEMU AND NOT MartyPC: neither, really - there is no OS here. The guest is
a boot sector, the subject is three %included files, and what QEMU supplies is
an 8086, a serial port and `isa-debug-exit`. Nothing about the machine under
it is being asserted, which is also why this asserts CORRECTNESS and never a
time.
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeout", type=int, default=120,
                    help="seconds to wait for the guest's summary line")
    a = ap.parse_args()
    r = subprocess.run([os.path.join(ROOT, "apps/weave/hosttest/weavecv.sh"),
                        str(a.timeout)], cwd=ROOT)
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())

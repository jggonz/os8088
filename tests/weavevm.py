#!/usr/bin/env python3
"""The WJS VM, diffed against tools/weavesim.py in raw QEMU (WEAVE-SPEC 12.3).

    python3 tests/weavevm.py
    python3 tests/weavevm.py --timeout 120

A thin driver over apps/weave/hosttest/weavevm.sh, which is where the real
work is: generate the corpus from the model, assemble
apps/weave/hosttest/weavevm.asm around the SHIPPING apps/weave/wvm.inc, boot
it from a boot sector with SS != DS and read the verdict off COM1. The .asm's
header says what each row checks.

WHY IT IS A ROW AND THE OTHER THREE BOOT-SECTOR GATES ARE NOT. `rcz80test`,
`rcmemtest`, `c64memtest` and `c64cputest` are `make` targets only, because
`tests/unit/t_registry.py` polices `tests/*.py` and a shell driver under
apps/*/hosttest/ is invisible to it. WEAVE-SPEC 12.3 names `weavevm` as a soak
ROW, so it needs a file here - and there is a second reason to want one: this
is the gate the rest of wave 3 is built on, and `os88test.py soak -k 'weave*'`
is the family's one command (12.3). `make weavevm` runs the same thing.

WHY QEMU AND NOT MartyPC (docs/TESTING.md asks the question of every row):
neither, really. There is no OS here. The guest is a boot sector, the subject
is one %included file, and what QEMU supplies is an 8086, a serial port and
`isa-debug-exit`. Nothing about the machine under it is being asserted - which
is also why the row asserts CORRECTNESS and never a time.
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
    r = subprocess.run([os.path.join(ROOT, "apps/weave/hosttest/weavevm.sh"),
                        str(a.timeout)], cwd=ROOT)
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())

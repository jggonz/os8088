#!/usr/bin/env python3
"""win16big - a program over 64 KB that Solitaire does not cover (SPEC.md
124.20, docs/plans/WIN16-ANY-PLAN.md S15).

    python3 tests/win16big.py              the QEMU leg (row win16big)
    python3 tests/win16big.py --keep       leave QEMU and its disks for a look

BIG.EXE (tests/win16/big.c, `make win16fixtures`) is medium model: sixteen
functions of straight-line arithmetic, each in a code segment of its own, are
17 code segments and about 155 KB of code. It calls each far, forward and
back, writes a 128 KB GlobalAlloc block through a __huge pointer and reads it
back 64 KB at a time by adding __AHINCR (KERNEL.114, which the loader binds
to its value, 1000h) to the segment. Its MessageBox carries the result, read
off the program's stack as win16fpu reads FPU.EXE's, and must equal the same
sums computed here. A loader that placed a segment wrongly, a far call that
landed in the wrong one, a block that was not 128 KB, or an __AHINCR that was
not 1000h each changes a number. The fixture not built is a SKIP.
"""
import argparse
import os
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
from win16mines import QDrive, b                              # noqa: E402
import win16fpu as F                                          # noqa: E402

NAME = "BIG.EXE"
HUGE_N = 131072


def want():
    """big.c's arithmetic, in 16 bits: f0..f15 then f15..f0, each fk the
    steps k*500 .. k*500+499 of h = (h * 31 + k) ^ (h >> 3)"""
    def f(n, h):
        for k in range(n * 500, n * 500 + 500):
            h = ((h * 31 + k) & 0xFFFF) ^ (h >> 3)
        return h
    h = 1
    for n in list(range(16)) + list(range(15, -1, -1)):
        h = f(n, h)
    s = 0                               # the steps' own mix, over the bytes
    for i in range(HUGE_N):             #   (a plain sum of them is 0000)
        s = ((s * 31 + ((i * 7 + (i >> 9)) & 0xFF)) & 0xFFFF) ^ (s >> 3)
    return ("big: code %04X\nhuge %d bytes, sum %04X, 0 wrong\n__AHINCR 1000"
            % (h, HUGE_N, s))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16big: SKIP - BIG.EXE is not built (make win16fixtures)")
        return 77
    import os88ui
    import xmcheck
    bad = []
    work = tempfile.mkdtemp(prefix="w16b-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, [NAME])
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        F.run_prog(d, ui, xmcheck.sym, S, dw, NAME, bad, "QEMU",
                   want={NAME: want()})
        if not a.keep:
            d.p.close()
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16big: 17 code segments and a 128 KB huge block, as computed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

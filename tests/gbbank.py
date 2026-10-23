#!/usr/bin/env python3
"""RealDMG's bank cache, on 86Box's 286 with 4MB: a 1MB cartridge kept above
1MB, its banks fetched as the game switches.

    python3 tests/gbbank.py [--vm 286] [--secs 20]

docs/REALDMG-SPEC.md section 10.3. GBBANK.GB (tools/gbprof/gbbank.asm,
ours, built by tools/gbours.py) is an MBC5 cartridge of 64 banks that
switches among all of them in an LFSR's order and checks every switch two
ways: bank n's own bytes at 4000h, and a routine at 4010h that answers n -
code run from 4000-7FFF, where the core's DS and the recompiler's bank keys
meet the cache. It counts switches checked at C000h and wrong ones at C002h.

The row boots vm/<VM> with 4MB through tests/rdgcore/rdgwin.py's boot, opens
the cartridge, and asserts that it went to the bank cache with fewer slots
than banks, that banks were fetched, that switches kept being checked over
--secs of emulated time, and that NOT ONE read the wrong bank. A machine
with no extended memory cannot have the cache at all, so this is 86Box's
(MartyPC is an 8088). Taking the fetch's copy out - a slot keeps the bank it
had - is 1,000s of wrong switches.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import rdgwin  # noqa: E402


def wram(b, view):
    m = b.memr(view * 16 + 0xC000, 16)
    return m[0] | m[1] << 8, m[2], m[3], m[15]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--vm", default="286")
    ap.add_argument("--secs", type=float, default=20)
    a = ap.parse_args()
    rom = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "gbours.py"),
                                   "gbbank"], text=True).strip().splitlines()[-1]
    o = argparse.Namespace(rom=rom, vm=a.vm, keep="", mem=4096, press="", fs=False)
    b = rdgwin.boot(o)
    fails = []
    try:
        b.cmd("step 60000")                     # the 1MB load, off a floppy
        seg, view, core = rdgwin.find(b)
        n0, bad0, first0, ok0 = wram(b, view)
        b.cmd("step %d" % int(a.secs * 1000))
        n1, bad1, first1, ok1 = wram(b, view)
        bk = rdgwin.bankstat(b, seg)
    finally:
        b.cmd("quit")
    done = (n1 - n0) & 0xFFFF
    print("gbbank: %s; %d switches checked in %.0f emulated s, %d wrong%s"
          % ("cache %d slots, %d banks fetched" % bk if bk else "ONE CLAIM",
             done, a.secs, bad1, " (the first in bank %d)" % first1 if bad1 else ""))
    if bk is None:
        fails.append("the cartridge did not go to the bank cache")
    else:
        if bk[0] >= 63:
            fails.append("%d slots hold every bank: nothing to fetch" % bk[0])
        if bk[1] < 63:
            fails.append("only %d banks fetched" % bk[1])
    if ok1 != 0xA5:
        fails.append("the cartridge never checked a switch")
    if done < 200:
        fails.append("%d switches in %.0f s: the cache stalls the game" % (done, a.secs))
    if bad1:
        fails.append("%d switches read the wrong bank" % bad1)
    for f in fails:
        print("FAIL gbbank: " + f)
    print("gbbank: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

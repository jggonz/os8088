#!/usr/bin/env python3
"""kern_mega: a package's OWN region moved by the pool's compaction while
that package is the caller (docs/plans/KERN-MEGA-PLAN.md 14.15's open item,
14.46).

    python3 tests/megaown.py            the row
    python3 tests/megaown.py --keep     ...and leave the boot's directory

14.15 compacts the extended pool inside the claim that found no run, and
argues - without a run - that the CALLER's own region may be one of the
blocks that move: its selectors never change, only their descriptors' bases,
and the cell's `retf` and `pop ds` reload CS and DS. MEGACP (tests/megacp)
could not show it, its region lying below everything it claimed. This boots
kern_mega under Bochs at 2MB, so the pool is small enough to fill, with
tests/megaown's two builds on C: in this order: MEGAHOLE (OS88_HIGH |
OS88_HIGHMEM) holds three 60KB pool claims and closes its own window ten
seconds later, which leaves a HOLE under MEGAOWN's region; MEGAOWN then
fills the pool, frees every other block and claims 64KB from its own wake
handler, walking the pool's region blocks before and after.

Its region must be at a LOWER offset after the claim than before it; the
claim and every survivor's stamps must read back (`MEGAOWN: ok`, which also
checks a word of MEGAOWN's own bss and of its own code, read through DS and
CS after the call that moved them); a second callback two seconds later must
say the same (`MEGAOWN: after ok`); and no kern_mega FAULT or CLOSED.

THE FAILING CASE (docs/WRITING-TESTS.md 1): make mx_compactf pin a package
region (kernel/mega.inc, `cmp al, 0x80` -> `cmp al, 0xA0` in its owner
test) and the region stays where it was: red on "did not move".
"""
import os
import re
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
from os88fixture import need  # noqa: E402

PKGS = ["build/megahole.o88", "build/megaown.o88"]


def main():
    keep = "--keep" in sys.argv
    for p in PKGS + ["build/megarun.o88", "build/mega.img",
                     "build/mbr.bin"]:
        need(p)
    out = tempfile.mkdtemp(prefix="megaown-")
    t0 = time.time()
    os88bochs.MEGS = 2              # a pool MEGAOWN can fill
    box = os88bochs.Box(out, [os.path.join(ROOT, p) for p in PKGS])
    fails = []
    lines = []
    try:
        if box.wait_for("kern_mega:", 240):
            time.sleep(15)              # the desktop's first frame
            box.capture()
            os88bochs.dbl(591, 160)     # C:
            time.sleep(10)
            os88bochs.dbl(150, 123)     # RUN
            time.sleep(8)
            os88bochs.dbl(160, 139)     # 0RUN.O88
            end = time.time() + 200
            while time.time() < end:
                lines = box.lines()
                if any(l.startswith("kern_mega FAULT") or
                       l.startswith("MEGAOWN: after") or
                       l.startswith("MEGAOWN: no") for l in lines):
                    break
                time.sleep(1)
            time.sleep(2)
            os88bochs.shot(os.path.join(out, "after.png"))
            lines = box.lines()
        else:
            fails.append("no kern_mega line in 240s")
    finally:
        box.stop()
    for l in lines:
        if l.startswith("kern_mega FAULT") or l.startswith(
                "kern_mega CLOSED"):
            fails.append(l)
    for l in lines:
        if l.startswith("MEGAHOLE") or l.startswith("MEGAOWN"):
            print("megaown: " + l)
    if "MEGAHOLE: closing" not in lines:
        fails.append("MEGAHOLE never gave its claims back")
    regs = [int(m.group(1), 16) for m in
            (re.match(r"MEGAOWN: region (\w+) (\w+)", l) for l in lines) if m]
    if len(regs) != 2:
        fails.append("MEGAOWN walked %d region blocks where it should have "
                     "seen exactly one, twice (is MEGAOWN above 1MB?)"
                     % len(regs))
    elif regs[1] >= regs[0]:
        fails.append("MEGAOWN's region did not move: KB %d before the claim, "
                     "%d after - the hole under it was never compacted "
                     "across" % tuple(regs))
    if "MEGAOWN: ok" not in lines:
        fails.append("MEGAOWN's steps or self checks failed: %s" % " | ".join(
            l for l in lines if l.startswith("MEGAOWN: no")))
    if "MEGAOWN: after ok" not in lines:
        fails.append("MEGAOWN's second callback did not read itself back")
    print("megaown: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if keep or fails:
        print("megaown: the boot is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

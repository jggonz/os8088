#!/usr/bin/env python3
"""kern_mega: OSAPI_XMEM_CAPS ANSWERS WHAT A CLAIM IS THEN GIVEN
(docs/plans/KERN-MEGA-PLAN.md 14.122, SPEC.md 41.8).

    python3 tests/megacaps.py            the row
    python3 tests/megacaps.py --keep     ...and leave the boot's directory

This boots kern_mega under Bochs at 4MB off A: with C: beside it - so the
boot's 32KB read-ahead store AND C:'s FAT window are kernel CACHES in the
pool above 1MB (14.121) - and asserts both are standing before MEGARUN
starts tests/megacaps's MEGACAPS, which asks OSAPI_XMEM_CAPS, claims exactly
its KB through OSAPI_XMEM_ALLOC, frees it, and claims one KB more. Asserts:

  1. `MEGACAPS: caps N` - a pool to plan from;
  2. `MEGACAPS: got` - the claim of exactly N was given (mx_alloc sheds the
     caches on the way), so the answer is not LONG;
  3. `MEGACAPS: over refused` - N + 1KB was refused, so it is not SHORT:
     every cache the claim could shed was counted as room;
  4. no kern_mega FAULT or CLOSED.

THE FAILING CASE (docs/WRITING-TESTS.md 1): the tree before 14.122 has
`mx_caps` subtract the caches, so N is ~37KB short and the claim of N + 1KB
is GIVEN - red on (3), `MEGACAPS: over GOT`.
"""
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
from os88fixture import need  # noqa: E402

PKGS = ["build/megacaps.o88"]


def cache_state(box):
    """The read-ahead store and C:'s FAT window (volume 2): selectors above
    1MB, each a kernel cache in the pool."""
    r, w = box.reads([("dsk_rah_seg", 2), ("dsk_fatwc", 16)])
    out = {}
    for k, v in (("dirw", int.from_bytes(r, "little")),
                 ("fatw", int.from_bytes(w[4:6], "little"))):
        if v & 4 and box.sel_base(v) >= 0x100000:
            out[k] = "%04X@%X" % (v, box.sel_base(v))
    print("megacaps: caches standing in the pool: %s" % out)
    return out


def main():
    keep = "--keep" in sys.argv
    for p in PKGS + ["build/megarun.o88", "build/mega.img",
                     "build/mbr.bin"]:
        need(p)
    out = tempfile.mkdtemp(prefix="megacaps-")
    t0 = time.time()
    os88bochs.MEGS = 4
    box = os88bochs.Box(out, [os.path.join(ROOT, p) for p in PKGS])
    fails, lines, caches, xkb = [], [], {}, None
    try:
        if box.wait_pm(240):
            box.up()
            xkb = int.from_bytes(box.read("xm_kb", 2), "little")
            caches = cache_state(box)
            box.megarun()
            end = time.time() + 180
            while time.time() < end:
                lines = box.said()
                if any(l.startswith("kern_mega FAULT") or
                       l.startswith(("MEGACAPS: done", "MEGACAPS: no"))
                       for l in lines):
                    break
                time.sleep(0.2)
            box.guest_sleep(1)          # the lines after the verdict
            lines = box.said()
        else:
            fails.append("kern_mega never switched to protected mode in 240s")
    finally:
        box.stop()
    for l in lines:
        if l.startswith("kern_mega FAULT") or l.startswith(
                "kern_mega CLOSED"):
            fails.append(l)
        if l.startswith("MEGACAPS"):
            print("megacaps: " + l)
    if not caches.get("dirw") or not caches.get("fatw"):
        fails.append("the read-ahead store and C:'s FAT window were not "
                     "both standing in the pool: %s" % caches)
    caps = [l for l in lines if l.startswith("MEGACAPS: caps ")]
    if not caps:
        fails.append("the cell never answered: %s" % [
            l for l in lines if l.startswith("MEGACAPS")])
    else:
        print("megacaps: the pool is %sK, CAPS %dK" % (
            xkb, int(caps[0].split()[2], 16)))
        if "MEGACAPS: got" not in lines:
            fails.append("a claim of exactly CAPS was not given: %s - the "
                         "answer read LONG" % [
                             l for l in lines if "short" in l])
        if "MEGACAPS: over refused" not in lines:
            fails.append("a claim of CAPS + 1KB was given: %s - the answer "
                         "read SHORT (a cache counted as used)" % [
                             l for l in lines if "over" in l])
    print("megacaps: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if keep or fails:
        print("megacaps: the boot is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

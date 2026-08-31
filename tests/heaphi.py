#!/usr/bin/env python3
"""A driver's second image goes at the TOP of the heap (SPEC.md 50.3.2.1).

    make && python3 tests/heaphi.py

WHAT IT DRIVES is a user's sequence and not a synthetic one, because the
defect it guards is a sequence: an XT with 640KB, an already-formatted hard
disk and a `SYSTEM.CFG` that asks for nothing, then open the Control Panel,
tick Hard Drive (which is the mount, SPEC.md 52.6.1), tick Ram Disk, and
select the Ram Disk page - which is what reads `RAMPAGE.DRV` in.

WHAT IT ASSERTS is the number `mem_claim` actually answers a claim from, and
not the one `mem_avail` prints. A refusal is the LAST thing that routine does:
it sheds the caches and compacts first (SPEC.md 50.6.2, 66.9), so what a
claimant can have is the arena with the purgeable blocks dissolved and the
movable ones packed down through the room they leave. `heapmap.Map.compacted`
is that walk, modelled on the host.

The defect this exists for moved that number by 64.5KB while moving the LIVE
largest run by nothing at all, which is why a test written against `runs()`
would have watched it happen:

    + Ram Disk page selected      413.0K live   413.0K compacted   <- before
                                  413.0K live   477.5K compacted   <- after

`dsk_rah_want`'s 63KB block is `MEM_P_DIRW`, claimed at the BOOT MOUNT before
any driver exists, and `mem_claim_dma`'s 64KB page rule lands it at 0x20000
with the arena's floor beneath it. Dissolve it and there is a ~64.5KB hole at
the bottom for the movable claims above to pack into. `RAMPAGE.DRV` claimed
low first-fits ABOVE those movables and is pinned - so the fill point stops
there, the hole stays a separate run, and the arena is two pieces for as long
as the page is loaded.

**A PINNED CLAIM'S COST IS DECIDED BY WHAT IS ABOVE IT**, which is why the
one assertion here is a NUMBER and the structural sentence is derived from it
on failure. "Nothing pinned sits low in the arena" would be the obvious test
and it is false: the hard disk's 6KB listing claim (SPEC.md 22.6/66.5.10.1) is
pinned for ever and sits directly UNDER the read-ahead, and it is free - it is
below every movable claim, so the compactor packs them past it. Sent high it
measured 9KB WORSE. That case is in SPEC.md 50.3.2.1, and this test has to
keep passing with that claim exactly where it is.

IT IS MARTY'S, and not on CLAUDE.md's QEMU list: an 8088 with a hard disk
behind an option ROM is exactly what `os8088_xt_hdd` is, and nothing here is a
time.
"""
import argparse
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))   # AFTER tests/, so that
import heapmap                                    # noqa: E402  tools/heapmap
import os88marty as M                             # noqa: E402  wins over the
import os88sym                                    # noqa: E402  test of the
from os88mouse import Mouse                       # noqa: E402  same name
from os88geom import WIN_SIZE, MAX_WIN            # noqa: E402

MACHINE = "os8088_xt_hdd"
CP_I0Y, CP_IROWH, CP_RX = 6, 14, 96     # ctrl.inc: the item list, the pane
CP_DBY1, CP_DROWH = 20, 26              # ...the Drivers page's hit bands
CP_IDRV = 2                             # SPEC.md 31.3
DRVR_SZ, DRVR_SEG = 16, 2               # driver.inc's row: 0 = not loaded
HDD_ROW, RD_ROW = 1, 3                  # drv_tab rows (SPEC.md 31.1's order)
MEM_P_DIRW = 0xFE02                     # memory.inc: the read-ahead's tag


class Q(object):
    """heapmap.Map wants .read(linear, n); Marty already has exactly that."""

    def __init__(self, m):
        self.m = m

    def read(self, linear, n):
        return self.m.read(linear, n)


def equ(path, name):
    """One `NAME equ <n>` out of a source file, rather than a copy here - the
    read-ahead's size has moved once already and a stale copy would assert
    the old one."""
    for line in open(os.path.join(ROOT, path)):
        f = line.split()
        if len(f) >= 3 and f[0] == name and f[1] == "equ":
            return int(f[2], 0)
    raise RuntimeError("no %s in %s" % (name, path))


def rah_kb():
    return (equ("kernel/disk.inc", "DSK_RAH_SECS") * 512
            * equ("kernel/disk.inc", "DSK_RAH_RUNS")) // 1024


def wins(m):
    b = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    return [(i,) + tuple(struct.unpack_from("<H", b[i * WIN_SIZE:], o)[0]
                         for o in (2, 4, 6, 8))
            for i in range(MAX_WIN)
            if struct.unpack_from("<H", b[i * WIN_SIZE:], 0)[0] & 2]


def quiet(m, s=15.0):
    try:
        M.settle(m, limit=s)
    except M.MartyError:
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args()

    want = rah_kb()
    sym = {n: os88sym.linear(n)
           for n in ("mem_base", "mem_top", "spl_live", "mem_tab")}
    bad = []

    with M.launch(a.image, apps=a.apps, machine=a.machine) as m:
        M.settle(m)
        q, mo = Q(m), Mouse(marty=m)

        mo.menu(8, 8, 8, 40)                    # chip menu -> Control Panel
        quiet(m)
        cp = [w for w in wins(m) if w[3] >= 280 and w[4] >= 100]
        if not cp:
            sys.exit("no Control Panel - nothing below this could mean anything")
        cp = cp[-1]
        x0, y0 = cp[1] + 1, cp[2] + 18

        def item(n):
            mo.click(x0 + 40, y0 + CP_I0Y + n * CP_IROWH + 7)
            quiet(m)

        def drvrow(r):
            mo.click(x0 + CP_RX + 40,
                     y0 + CP_DBY1 + r * CP_DROWH + CP_DROWH // 2)
            time.sleep(8)                       # the load is a floppy read
            quiet(m)

        def seg(r):
            return int.from_bytes(
                m.read(m.sym("drv_tab") + r * DRVR_SZ + DRVR_SEG, 2), "little")

        item(CP_IDRV)
        drvrow(HDD_ROW)
        if not seg(HDD_ROW):
            sys.exit("the hard-disk driver did not load")
        drvrow(RD_ROW)
        rdseg = seg(RD_ROW)
        if not rdseg:
            sys.exit("the RAM disk driver did not load")

        nst = m.read(m.sym("cp_nst"), 1)[0]     # the driver pages follow the
        item(nst + 1)                           # static rows: HDD then RD
        time.sleep(10)                          # the first paint READS the
        quiet(m)                                # page image off the floppy
        mp = heapmap.Map(q, sym)

    print("\n  hard disk + RAM disk + the Ram Disk page")
    for c in mp.claims:
        print("    %s" % repr(c))
    live = max((p for _, p in mp.runs()), default=0) / 64.0
    comp = max((p for _, p in mp.compacted()), default=0) / 64.0
    print("    largest %.1fK live, %.1fK after a modelled compaction" % (live, comp))

    cache = [c for c in mp.claims if c.own == MEM_P_DIRW]
    if not cache:
        sys.exit("no %04X claim - the read-ahead never came up, so neither "
                 "assertion below is about anything" % MEM_P_DIRW)
    cache = cache[0]
    if cache.kb < want:
        sys.exit("the read-ahead is %.1fK, not the %dK disk.inc sizes it at"
                 % (cache.kb, want))

    # THE ASSERTION IS THE NUMBER, and the structural statement is derived
    # from it rather than asserted beside it. There is no predicate over the
    # map that separates a barrier which costs from one which does not - the
    # hard disk's listing claim is pinned, low, and directly under the cache,
    # and it is FREE (SPEC.md 50.3.2.1) - so anything simpler than the
    # compaction walk either misses this defect or fails on that claim.
    #
    # `comp - live` is the cache plus whatever gap sits under it: 63K + 1.5K
    # here. Requiring the cache's own size alone leaves that gap as margin and
    # stays correct if it ever closes.
    if comp - live < want:
        at, cap = mp.base, None
        for c in mp.claims:                 # mem_cp_plan's walk: where does
            if c.purgeable:                 # the first run END, and on what?
                continue
            if c.pinned:
                if c.seg > at:
                    cap = c
                    break
                at = c.end
            else:
                at += c.para
        bad.append(
            "a compaction buys %.1fK and the read-ahead alone is %dK. "
            "mem_claim sheds and compacts before it refuses (SPEC.md 50.6.2), "
            "so this is the number a claim is actually answered from, and it "
            "has stopped being worth the dearest cache in the system."
            % (comp - live, want))
        if cap is not None:
            bad.append(
                "the fill point packs up to %05X and stops at %s. A pinned "
                "claim ABOVE the movable ones cuts the arena in two for as "
                "long as it is held; below them it costs nothing, which is "
                "the whole of SPEC.md 50.3.2.1. If this one is a driver's "
                "second image its base is its CS and it wants "
                "OSAPI_MEM_CLAIM_HI." % (at << 4, repr(cap).strip()))

    # ...and one sanity check, so a run where the page never loaded cannot
    # pass by having nothing to be wrong about.
    if not [c for c in mp.claims if c.own == rdseg and c.pinned]:
        bad.append("no pinned claim owned by the RAM disk driver at %04X - "
                   "RAMPAGE.DRV was never read in, so this run proves nothing"
                   % rdseg)

    print()
    for b in bad:
        print("  !! %s" % b)
    print("heaphi: %s" % ("FAIL" if bad else
                          "ok - a compaction buys %.1fK, the read-ahead is %dK"
                          % (comp - live, want)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""A DRIVER IMAGE moves, and the driver keeps working (SPEC.md 66.6.3).

    make && make build/regmove360.img && python3 tests/drvmove.py

IT DRIVES THE SCENARIO THE WHOLE DOCUMENT EXISTS FOR
(docs/plans/HEAP-UNPIN-PLAN.md 2.0), which is a user's sequence and not a
synthetic one: mount one driver, mount a second above nothing, unmount the
FIRST, and the second is left standing over a hole that never closes. Before
this, a driver image was pinned by `mem_can_move` outright - its base is a CS -
so the hole stayed for the rest of the session and every later mount added
another.

    ceiling [ HDD 8K ][ RAMDISK 9K ] ......... free      mount, mount
    ceiling [  hole  ][ RAMDISK 9K ] ......... free      unmount the hard disk

WHAT MAKES IT SAFE IS THREE FACTS AND NO DECLARATION (66.6.3), and the one
worth naming here is `mem_ivt_names`: the kernel scans all 256 vectors for the
image's segment rather than asking the driver, because SOUND.DRV hooks FIVE
(its IRQ, plus four candidates it probes with) and a driver-side declaration
would have to be right about all of them for ever. So this row asserts the
RAM disk's image moves AND that the machine still has a working driver behind
it - `drv_tab`'s row, the published class fast path and the volume all agree.

FOUR ASSERTIONS:

  1. the image claim is declared movable at all - `MC_RLOC` out of `mem_tab`,
     because `drv_load` declares it and a silent refusal there would leave
     everything below vacuous;
  2. IT MOVED - `drv_tab`'s DRVR_SEG for the RAM disk row, before and after;
  3. NOTHING STILL HOLDS THE OLD SEGMENT - every `drv_fseg*`, `drv_blkseg`,
     every `drv_tab` row and every claim owner, by name. A stale one does not
     fault: it far-calls a dispatcher in freed memory on the next volume
     access, which is the failure this row exists to make loud;
  4. and the machine still draws afterwards, because on an 8086 a wild far
     call executes rather than trapping.
"""
import argparse
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                    # noqa: E402  tools/heapmap
import os88marty as M                             # noqa: E402
import os88sym                                    # noqa: E402
from os88mouse import Mouse                       # noqa: E402
import heaphi                                     # noqa: E402
import dispcp                                     # noqa: E402
import os88geom                                   # noqa: E402
import sheetmove                                  # noqa: E402

u16, claims, uncovered = sheetmove.u16, sheetmove.claims, sheetmove.uncovered
CP_I0Y, CP_IROWH, CP_RX = heaphi.CP_I0Y, heaphi.CP_IROWH, heaphi.CP_RX
CP_DBY1, CP_DROWH, CP_IDRV = heaphi.CP_DBY1, heaphi.CP_DROWH, heaphi.CP_IDRV
DRVR_SZ, DRVR_SEG = heaphi.DRVR_SZ, heaphi.DRVR_SEG
HDD_ROW, RD_ROW = heaphi.HDD_ROW, heaphi.RD_ROW


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=heaphi.MACHINE)
    ap.add_argument("--apps", default="build/regmove360.img")
    a = ap.parse_args()
    S = os88sym.linear

    with M.launch("build/os8088-360.img", apps=a.apps,
                  machine=a.machine) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)

        mo.menu(8, 8, 8, 40)                    # chip menu -> Control Panel
        heaphi.quiet(m)
        cp = [w for w in heaphi.wins(m) if w[3] >= 280 and w[4] >= 100]
        if not cp:
            sys.exit("no Control Panel - nothing below could mean anything")
        cp = cp[-1]
        x0, y0 = cp[1] + 1, cp[2] + 18

        def item(n):
            mo.click(x0 + 40, y0 + CP_I0Y + n * CP_IROWH + 7)
            heaphi.quiet(m)

        def drvrow(r):
            mo.click(x0 + CP_RX + 40,
                     y0 + CP_DBY1 + r * CP_DROWH + CP_DROWH // 2)
            time.sleep(8)                       # a load is a floppy read
            heaphi.quiet(m)

        def seg(r):
            return u16(m.read(S("drv_tab") + r * DRVR_SZ + DRVR_SEG, 2))

        item(CP_IDRV)
        drvrow(HDD_ROW)
        if not seg(HDD_ROW):
            sys.exit("the hard-disk driver did not load")
        drvrow(RD_ROW)
        rd0 = seg(RD_ROW)
        if not rd0:
            sys.exit("the RAM disk driver did not load")
        print("hdd %04x   ramdisk %04x" % (seg(HDD_ROW), rd0))

        bad = 0
        reg = [c for c in claims(m, S) if c[0] == rd0]
        rloc = reg[0][3] if reg else 0
        print("  1 image declared movable   %s"
              % ("MC_RLOC=%04x, %dKB" % (rloc, reg[0][1] // 64) if rloc else
                 "NO  <-- drv_load's declaration did not take, so every "
                 "assertion below is vacuous"))
        bad += not rloc

        drvrow(HDD_ROW)                         # ...and UNMOUNT it: the hole
        if seg(HDD_ROW):
            sys.exit("the hard-disk driver did not unmount, so no hole opened")
        print("      hard disk unmounted - the hole is above the RAM disk now")

        # --- force ------------------------------------------------------------
        mo.click(cp[1] + 8, cp[2] + 9)          # close the panel
        heaphi.quiet(m)
        dispcp.open_drive(m, mo, S, M.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)
        M.settle(m)
        disk = dispcp.win_rect(m, S, dslot)[:2]
        before = set(w.i for w in os88geom.windows(m, S) if w.visible)
        dispcp.open_named(m, mo, S, M.settle, *disk, name="FILLER.O88")
        time.sleep(8)
        M.settle(m)
        new = [w for w in os88geom.windows(m, S)
               if w.visible and w.i not in before]
        if not new:
            sys.exit("the filler never opened a window")
        fl = new[0]
        pt = uncovered(m, S, fl, prefer_title=True)
        if pt is None:
            sys.exit("the filler's title bar is wholly covered")
        mo.click(*pt)
        M.settle(m)
        for _ in range(6):
            m.key("KeyA")
            time.sleep(6)
            M.settle(m)
            if seg(RD_ROW) not in (rd0, 0):
                break

        rd1 = seg(RD_ROW)
        moved = rd1 and rd1 != rd0
        print("  2 the image MOVED          %s"
              % ("%04x -> %04x" % (rd0, rd1) if moved else
                 "NO (%04x) <-- the run proves nothing" % rd1))
        bad += not moved

        # NOT "drv_fseg2 == rd1": the DISK class has ONE published pair and the
        # hard disk's detach cleared it, so 0 there is the machine's own state
        # and asserting the RAM disk into it would be asserting a bug. What a
        # move can actually get wrong is a word left holding the OLD segment,
        # so that is what this reads - every one of them, by name.
        words = {"drv_fseg": 0, "drv_fseg2": 0, "drv_fseg3": 0,
                 "drv_fseg4": 0, "drv_fseg5": 0, "drv_blkseg": 0}
        for n in words:
            words[n] = u16(m.read(S(n), 2))
        rows = {"drv_tab[%d]" % r: seg(r) for r in range(6)}
        owners = {"claim owner %04x" % o: o
                  for o in set(c[2] for c in claims(m, S))}
        stale = sorted(k for k, v in
                       list(words.items()) + list(rows.items())
                       + list(owners.items()) if v == rd0)
        print("  3 nothing still holds %04x  %s"
              % (rd0, "OK (%d kernel words checked)"
                 % (len(words) + len(rows) + len(owners)) if not stale else
                 "STALE: %s - a far call through one of these lands in freed "
                 "memory on the next volume access" % ", ".join(stale)))
        bad += bool(stale)

        # ...and the machine is still RUNNING after it. A wild far call through
        # a word the move missed does not fault on an 8086, it executes - so
        # "the desktop still repaints" is a real assertion here and not a
        # formality.
        alive = True
        try:
            M.settle(m)
        except M.MartyError:
            alive = False
        print("  4 the machine still draws   %s"
              % ("OK" if alive else "NO - the screen never settled after the "
                 "move, which is what a far call into freed memory looks like"))
        bad += not alive

        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

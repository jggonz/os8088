#!/usr/bin/env python3
"""A LAUNCH READS THE POSTER'S OWN CACHE, and the cost is the assertion.

docs/plans/LISTING-HOME-PLAN.md wave 1.  `loader_run_x` is handed a directory
INDEX plus `[ld_pwin]`, the state block of the Disk window that posted it, and
SPEC.md 22.1 is explicit that this "may not be the one currently mounted".  It
used to make the GLOBAL snapshot be that folder with `fmv_sync_x` - a loud
mount, so the directory scan, the sort and the icon harvest - purely to
resolve an index against a listing the poster already holds a byte-for-byte
copy of.

**NOTHING INSIDE THE GUEST CAN SEE THIS.**  Both spellings open the same
package into the same window with the same title, and a screenshot of either
is the same picture; the only difference is how many times the machine went
to the disk.  So the row counts at the CONTROLLER with `os88marty.disk()`,
which `pathcost` and `dosmedia` do for the same reason.

TWO ARMS, and the row is worthless with either one alone:

  * **A, the same window** - act in the window you last moved, which is the
    common case.  `fmv_sync_x`'s free path is two compares and a `ret` there,
    so the old code already did NO I/O and the bar is PARITY.  This arm
    exists because the first build of the wave failed it: a quiet chdir is
    not free in a free path's place, `dsk_here_ok` asking whether the media
    CANNOT have changed and a floppy's always can, so it mounted.  Measured
    3 reads / 531 ms against the loud sync's 2 / 306 - a change that removes
    a mount and adds a bigger one, invisible from inside.
  * **B, another window** - the case the wave is FOR.  A second Disk window
    on the other drive makes the standing folder not the poster's, and the
    launch is posted from the first.  Base 10 reads / 58 sectors / 7 seeks;
    with the cache read, 4 / 13 / 2.  Six `int 13h` at ~400 ms apiece on a
    4.77 MHz XT (PERFORMANCE.md Part 2) is ~2.4 guest seconds.

VERIFIED TO FAIL both ways: putting `fmv_sync_x` back on both arms takes B to
10 reads, and taking the "already standing there" test out takes A to 3.

The budgets are the measured figures with headroom, not the figures - a
geometry or a folder with different contents moves them - and arm B is
asserted against arm A rather than against a constant, because what the wave
removes is the DIFFERENCE between the two.
"""
import sys, os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "tools"))
import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402

IMG = "build/os8088-360.img"
APPS = "build/apps360.img"
PKG = "CALC.O88"

# Arm A is parity with a listing-free launch, so it is bounded near the
# measurement: the package itself is 12 sectors in 2 reads and a third read
# is a mount that should not be there.
A_READS = 2
# Arm B pays a quiet mount of the poster's volume on top of arm A. The bar is
# the LOUD one it replaces, which was 10 reads / 58 sectors.
B_READS = 6
B_SECTORS = 24


def fail(msg):
    print("ldcost: FAIL: " + msg)
    sys.exit(1)


def shot(m, ui, label):
    os88marty.settle(m)
    d = m.disk()
    print("ldcost: %-18s reads=%2d sectors=%2d seeks=%d transfer=%.0f ms"
          % (label, d["reads"], d["read_sectors"], d["seeks"],
             d["transfer_ms"]))
    return d


def main():
    with os88ui.boot(IMG, apps=APPS) as ui:
        m = ui.m

        wb = ui.open_drive("B")
        ui.path("APPS")
        os88marty.settle(m)
        n = len(ui.listing(win=wb))
        if n < 4:
            fail("B:/APPS lists %d entries - the arms below are about what a "
                 "FOLDER costs to re-list, so a folder with nothing in it "
                 "cannot tell the two spellings apart" % n)

        # --- A: the poster IS the standing window ---------------------------
        m.disk(reset=True)
        w = ui.open(PKG, win=wb)
        a = shot(m, ui, "A same-window")
        ui.close(w)

        if a["reads"] > A_READS:
            fail("the same-window launch took %d reads against %d. That arm "
                 "is PARITY with the loud sync, whose free path is two "
                 "compares - an extra read here is a mount put where there "
                 "was none, which is what the first build of this wave did"
                 % (a["reads"], A_READS))

        # --- B: the poster is NOT the standing window -----------------------
        ui.open_drive("A")              # the machine now stands on A:'s root
        os88marty.settle(m)
        ui.raise_window(wb)             # ...and the poster is B:/APPS again
        os88marty.settle(m)
        m.disk(reset=True)
        ui.open(PKG, win=wb)
        b = shot(m, ui, "B other-window")

        if b["reads"] > B_READS or b["read_sectors"] > B_SECTORS:
            fail("the cross-window launch took %d reads / %d sectors against "
                 "%d / %d. The loud sync it replaces was 10 / 58, so this is "
                 "the wave's own regression: the entry is being resolved "
                 "against the GLOBAL snapshot again rather than out of the "
                 "poster's cache (SPEC.md 22.1)"
                 % (b["reads"], b["read_sectors"], B_READS, B_SECTORS))

        extra = b["reads"] - a["reads"]
        print("ldcost: the cross-window launch costs %d read(s) more than the "
              "same-window one" % extra)
        if extra > 3:
            fail("%d reads more than arm A. What separates the two arms is "
                 "ONE quiet mount of the poster's volume; anything beyond it "
                 "is a listing being rebuilt" % extra)

    print("ldcost: ok")


if __name__ == "__main__":
    main()

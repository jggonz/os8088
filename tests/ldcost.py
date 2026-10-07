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

**ARM A'S BAR IS COMPUTED, NOT REMEMBERED.** It was `A_READS = 2`, the count
measured when it was written, and a kernel size pass then made it 3 with the
loader untouched: the read-ahead cache takes no 64KB page head (SPEC.md
18.95.7), so where its slots sit is wherever the heap puts it, and
`dsk_runcap` cuts a fill at the page boundary (SPEC.md 18.91). A slot at
0x1FC00 split CALC.O88's first fill two sectors in. That is a correct
transfer and not a mount, and a constant cannot tell the two apart. So the
bar is what the package's OWN fills cost, read off the guest after the launch
- the chain from `[ld_clus]` walked on the host image, and the slots out of
SPEC.md 18.95's read-ahead cache, through which every BIOS read passes as a
FILL of the chunk that missed: its table (`dsk_rah_tab`, over the claim at
`[dsk_rah_seg]`) says which slots hold the package's sectors and where they
sit. One call a fill, two when the slot straddles a page. Anything above that
is a read the launch did not need, which is exactly what the arm asks - and
the sectors are asserted too, because a mount reads the DIRECTORY and those
are not in the package's fills.
"""
import sys, os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "tools"))
import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402
import os88sym                                              # noqa: E402
from os88fat import Fat12                                   # noqa: E402

IMG = "build/os8088-360.img"
APPS = "build/apps360.img"
PKG = "CALC.O88"

# Arm A is parity with a listing-free launch: its bar is `plain_reads` below,
# the package's own transfer and nothing else. Arm B pays a quiet mount of the
# poster's volume on top of that - measured at 2 reads - so its bar is the
# package's transfer plus MOUNT_READS. The LOUD sync it replaces was 10 reads
# / 58 sectors.
MOUNT_READS = 4
B_SECTORS = 24


def plain_reads(m, img):
    """(int 13h calls, sectors) the package's OWN transfer cost, off the guest.

    A launch does not read into the region directly: every BIOS read passes
    through SPEC.md 18.95's read-ahead cache, which FILLS a chunk from the LBA
    that missed to the end of its track and copies out of it. So the package's
    own disk cost is exactly the fills that hold its sectors, and the cache's
    table says which those are - read here, after the launch, rather than
    re-derived.

    A fill is ONE call unless its slot straddles a 64KB physical page, and
    since SPEC.md 18.95.7 the claim takes no page head, so whether one does is
    wherever the heap put the cache. `dsk_runcap` then cuts the run at the
    boundary and the fill is TWO calls. The first build of this row counted
    that as a mount the day a kernel size pass moved the heap.
    """
    e = os88sym.equates()
    rd = lambda name, n: bytes(m.read(os88sym.linear(name), n))
    w = lambda name: int.from_bytes(rd(name, 2), "little")
    clus = w("ld_clus")
    fsz = int.from_bytes(rd("ld_fsz", 4), "little")
    v = Fat12(img)
    lbas = [v.cluster_lba(c) + k for c in v.chain(clus) for k in range(v.spc)]
    mine = set(lbas[:(fsz + 511) // 512])
    seg, runs = w("dsk_rah_seg"), w("dsk_rah_runs")
    if not seg:
        raise SystemExit("ldcost: FAIL: no read-ahead claim after the launch "
                         "(SPEC.md 18.95) - this row's arithmetic assumes one")
    rec, chunk = e["RAH_REC"], e["DSK_RAH_SECS"] * 512
    tab = rd("dsk_rah_tab", runs * rec)
    calls = sectors = 0
    for i in range(runs):
        r = tab[i * rec:(i + 1) * rec]
        lba = int.from_bytes(r[e["RAH_LBA"]:e["RAH_LBA"] + 2], "little")
        n = int.from_bytes(r[e["RAH_N"]:e["RAH_N"] + 2], "little")
        if not n or not mine & set(range(lba, lba + n)):
            continue
        lo = (seg << 4) + i * chunk
        calls += 1 + ((lo >> 16) != ((lo + n * 512 - 1) >> 16))
        sectors += n
    return calls, sectors


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
        a_reads, a_secs = plain_reads(m, APPS)
        print("ldcost: %s's own fills are %d call(s), %d sector(s)"
              % (PKG, a_reads, a_secs))
        ui.close(w)

        if a["reads"] > a_reads or a["read_sectors"] > a_secs:
            fail("the same-window launch took %d reads / %d sectors against "
                 "the %d / %d the package's own fills cost. That arm is "
                 "PARITY with the loud sync, whose free path is two compares "
                 "- an extra read here is a mount put where there was none, "
                 "which is what the first build of this wave did"
                 % (a["reads"], a["read_sectors"], a_reads, a_secs))

        # --- B: the poster is NOT the standing window -----------------------
        ui.open_drive("A")              # the machine now stands on A:'s root
        os88marty.settle(m)
        ui.raise_window(wb)             # ...and the poster is B:/APPS again
        os88marty.settle(m)
        m.disk(reset=True)
        ui.open(PKG, win=wb)
        b = shot(m, ui, "B other-window")
        b_pkg, _ = plain_reads(m, APPS)
        B_READS = b_pkg + MOUNT_READS

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

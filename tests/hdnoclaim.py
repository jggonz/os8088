#!/usr/bin/env python3
"""A mounted hard-disk partition costs NO HEAP (SPEC.md 22.6).

    make && python3 tests/hdnoclaim.py

**THIS ROW REPLACES `hdmove`, and the thing it replaces is the reason it
exists.** `HDD.DRV` used to claim `HDD_LISTKB` = 6KB per mounted partition and
hand it to `osapi_vol_add`, so that a hard disk listed `DSK_VENT` = 64 entries
where a floppy got 32. It was the only claim in the tree with three holders,
two of them the kernel's, and `hdmove` was 394 lines proving it could move
under a compaction.

SPEC.md 22.6 retired the donation instead, and the argument is worth keeping
where the gate is: SPEC.md 25.9 took the icon bodies out of a listing (four
fifths of the claim), and SPEC.md 22.6.2 then raised `DSK_NENT` to 64, after
which the claim funded a listing the `.lowbss` floor already gave for free -
24KB held on a four-partition machine to deliver nothing at all.

**A RETIREMENT NEEDS A GATE MORE THAN A MECHANISM DOES.** Nothing in the
kernel can fail to assemble if a per-volume claim comes back: `osapi_vol_add`
ignores DX, so a driver that claims and donates would simply LEAK 6KB per
mount, silently, on the one machine class nobody tests on a floppy. So the
four checks are the inverse of the ones `hdmove` made:

  1. the hard disk mounts at least one volume - without which 2-4 pass by
     checking nothing, which is docs/WRITING-TESTS.md 1's whole point;
  2. the driver owns EXACTLY ONE heap record, its own image (`MEM_K_DRV`),
     and no second claim of any size;
  3. a hard disk's listing is in THE WINDOW'S OWN CLAIM and `[dsk_dseg]` is
     0 - the same place a floppy's goes, and the same word at rest;
  4. and the listing WORKS: the volume's root lists entries, so 2 and 3 are
     not green because the mount quietly failed.

**BROKEN ON PURPOSE, and check 2 is the one that earns the row.** Claim 6KB in
`hd_mount_one` and simply hold it - which is what a reintroduced per-volume
claim looks like - and the machine behaves perfectly: the partition mounts,
C: opens, the root lists its six DOS files, and every other check here is
green. Check 2 alone goes red, naming the record: `2360 6KB owner 9bc0`.
Measured, with the break in.

**Check 3 cannot be broken from the driver at all, and that is the point of
having it.** `osapi_vol_add` ignores DX now, so no driver can aim the listing
at a claim however hard it tries; check 3 is the guard on the KERNEL half -
`dsk_list_pick`, `DV_SEG` and the `mem_rr_tab` rows coming back - which is a
change nothing on the driver side can cause and nothing else here would see.

**AND CHECK 3 WAS RE-CUT WHEN THE MECHANISM MOVED UNDER IT.** It read
`[dsk_dseg] == LOW_SEG`: the listing lived in the `.lowbss` floor, for a
driver-backed volume exactly as for a floppy. SPEC.md 22.6.3 deleted that
floor - a mount writes into the store its CALLER supplied - so the old form
was checking for a buffer that no longer exists, which is a red that says
nothing about the thing the row is for. The subject and the danger are
unchanged; what moved is where "the listing is not the driver's" is written
down. It is now the destination word being 0 AT REST, plus the window's own
FS_VSEG claim being a record the driver does not own.

IT IS MARTY'S: an 8088 with a hard disk behind an option ROM is what
`os8088_xt_hdd` is, and nothing here is a time.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88ui                                               # noqa: E402
import dispcp                                               # noqa: E402
import os88geom                                             # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402

MACHINE = "os8088_xt_hdd"
MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
DRVR_SZ, DRVR_SEG = 16, 2
HDD_ROW = 1                             # drv_tab row 1 is the hard disk
CP_I0Y, CP_IROWH, CP_RX = 6, 14, 96
CP_DBY1, CP_DROWH, CP_IDRV = 20, 26, 2

_fail = []


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def check(ok, what, why, got=None, want=None):
    print("%-4s %s" % ("ok" if ok else "FAIL", what))
    if not ok:
        if got is not None:
            print("       got:  %s" % (got,))
        if want is not None:
            print("       want: %s" % (want,))
        print("       why:  %s" % why)
        _fail.append(what)


def fs_vseg(m, S):
    """The ACTING Disk window's own listing store (SPEC.md 22.6.3).

    Off [fm_vp], which is the block every painter and hit-tester reads
    through - so this is the same word the rows on the glass came out of.
    """
    vp = u16(m.read(S("fm_vp"), 2))
    if not vp:
        return 0
    return u16(m.read((os88geom.KERNEL_SEG << 4) + vp + os88geom.FS_VSEG, 2))


def claims(m, S):
    """(base, paragraphs, owner, reloc) for every live mem_tab record."""
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    return [tuple(u16(raw, i * MC_SIZE + k) for k in (0, 2, 4, 8))
            for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    a = ap.parse_args()

    def S(name):
        return os88sym.linear(name)

    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        ui = os88ui.UI(m, mouse=mo, verbose=False)

        # --- tick the hard disk in, which is also what mounts it -----------
        # A driver row is NOT WANTED by default (SPEC.md 51.3), so the tick is
        # both the request and the mount (SPEC.md 52.6.1).
        mo.menu(8, 8, 8, 40)                    # chip menu -> Control Panel
        os88marty.settle(m)
        cp = [w for w in os88geom.windows(m, S)
              if w.visible and w.w >= 280 and w.h >= 100]
        if not cp:
            print("FAIL: no Control Panel - nothing could be ticked")
            return 1
        cp = cp[-1]
        x0, y0 = cp.x + 1, cp.y + 18
        mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)
        os88marty.settle(m)
        mo.click(x0 + CP_RX + 40,
                 y0 + CP_DBY1 + HDD_ROW * CP_DROWH + CP_DROWH // 2)
        # UNTIL, NOT SETTLE. drv_load reads the image off the system disk
        # holding the gfx lock, so the screen is MORE still while it works
        # than when it is done - which is the case os88marty.settle is
        # documented to get silently wrong. The condition is the fact itself:
        # drv_tab's row carries the loaded image's segment.
        def loaded(mm):
            return u16(mm.read(S("drv_tab") + HDD_ROW * DRVR_SZ + DRVR_SEG, 2))
        try:
            os88marty.until(m, loaded, "the hard-disk driver to load",
                            guest=90.0)
        except Exception as e:
            print("FAIL: the hard-disk driver did not load: %s"
                  % str(e).split("\n")[0][:160])
            return 1
        os88marty.settle(m)
        seg = loaded(m)
        print("hard disk driver at %04x" % seg)
        mo.click(cp.x + 8, cp.y + 9)            # close the panel
        os88marty.settle(m)

        # --- 1: it mounted something, or the rest checks nothing -----------
        letter, why = None, []
        for cand in "CDEF":
            try:
                ui.uncover(*os88geom.drive_pt(m, cand, S))
            except Exception:
                pass                            # no zone for that letter
        for cand in "CDEF":
            try:
                dispcp.open_drive(m, mo, S, os88marty.settle, cand)
                letter = cand
                break
            except Exception as e:
                why.append("%s: %s" % (cand, str(e).split("\n")[0][:120]))
        check(letter is not None, "the hard disk mounted a browsable volume",
              "checks 2-4 are about a MOUNTED partition; with none of them up "
              "they would go green having measured nothing at all. " +
              "  ".join(why),
              got="no zone opened", want="one of C: D: E: F:")
        if letter is None:
            return 1
        os88marty.settle(m)

        # --- 2: the driver owns its IMAGE and nothing else ------------------
        mine = [c for c in claims(m, S) if c[2] == seg or c[0] == seg]
        image = [c for c in claims(m, S) if c[0] == seg]
        extra = [c for c in mine if c[0] != seg]
        print("driver-owned records:")
        for bs, pa, ow, rl in sorted(mine):
            print("   %04x %5d KB owner %04x%s%s"
                  % (bs, pa // 64, ow, "  MOVABLE" if rl else "",
                     "  <- the image" if bs == seg else ""))
        check(len(image) == 1, "the driver's image is one heap record",
              "the image IS a claim (drv_load rounds the file up to KB and "
              "claims it), so finding none means this row is reading the "
              "wrong segment and check 2 below is vacuous",
              got=len(image), want=1)
        check(not extra,
              "a mounted partition costs the driver NO second claim",
              "SPEC.md 22.6 retired the donated listing claim. A per-volume "
              "claim that comes back is not refused by anything - "
              "osapi_vol_add ignores DX now - so it would simply hold 6KB a "
              "partition for the life of the mount, and nothing but this "
              "would say so",
              got=["%04x %dKB" % (c[0], c[1] // 64) for c in extra],
              want="no record but the image")

        # --- 3: ...and the listing is THIS WINDOW'S, with that volume open --
        # **THIS CHECK WAS `[dsk_dseg] == LOW_SEG` AND THAT IS NOW FALSE**
        # (docs/plans/LISTING-HOME-PLAN.md 13, SPEC.md 22.6.3). There is no
        # floor listing at all: a mount writes into the store its CALLER
        # supplied, so a Disk window on a hard disk lists into its own
        # FS_VSEG claim exactly as one on a floppy does, and the destination
        # word is 0 at rest because it is an ARGUMENT rather than a home.
        #
        # The row's subject is unchanged and so is its danger - a per-volume
        # donation coming back silently - but the shape of "the listing is
        # not the driver's" moved with the mechanism. Checking `LOW_SEG` now
        # would be checking for a buffer that does not exist, which is a red
        # that says nothing; checking 0 plus the window's own claim is the
        # same statement against the kernel we have.
        dseg = u16(m.read(S("dsk_dseg"), 2))
        nmax = u16(m.read(S("dsk_nmax"), 2))
        doff = u16(m.read(S("dsk_doff"), 2))
        print("opened %s:  [dsk_dseg] = %04x  [dsk_doff] = %04x  "
              "[dsk_nmax] = %d" % (letter, dseg, doff, nmax))
        check(dseg == 0,
              "[dsk_dseg] is 0 at rest, so no listing has a standing home",
              "the destination is an argument set for one mount and cleared "
              "after it (SPEC.md 22.6.3). A non-zero value here is a `.bss` "
              "word left naming a block across arbitrary time - and this one "
              "is MOVABLE on kern_big and PURGEABLE on kern_small",
              got="%04x" % dseg, want="0000")

        vseg = fs_vseg(m, S)
        print("%s: the window's own listing store is %04x" % (letter, vseg))
        check(vseg != 0 and all(vseg != c[0] for c in mine),
              "the hard disk's listing is in THE WINDOW'S claim, not a "
              "donated one",
              "this is the kernel's half of the same fact: a driver-backed "
              "volume lists where a floppy lists, into the Disk window's own "
              "FS_VSEG. If that store is one of the DRIVER's records the "
              "donation is back under a new name",
              got="%04x" % vseg, want="a claim the driver does not own")
        check(nmax > 0, "[dsk_nmax] is a real cap",
              "a zero cap lists nothing, which would make check 4 below read "
              "an empty window as a mount failure",
              got=nmax, want="> 0")

        # --- 4: and the volume actually LISTS --------------------------------
        rows = ui.listing()                 # [(name, type)], SPEC.md 19.1
        print("%s: lists %d entries: %s"
              % (letter, len(rows), [r[0] for r in rows][:6]))
        check(len(rows) > 0, "the hard disk's root lists entries",
              "checks 2 and 3 are both satisfied by a volume that failed to "
              "mount, which is the shape of false green this row is most "
              "exposed to",
              got=len(rows), want="> 0")
        check(len(rows) <= nmax,
              "...and no more than [dsk_nmax] of them",
              "the listing is bounded by the floor's cap now, for every "
              "volume on the machine",
              got=len(rows), want="<= %d" % nmax)

    print()
    if _fail:
        print("hdnoclaim: %d FAILED: %s" % (len(_fail), ", ".join(_fail)))
        return 1
    print("hdnoclaim: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

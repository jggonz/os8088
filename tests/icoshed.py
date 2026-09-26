#!/usr/bin/env python3
"""The icon store is shed, and the window it was drawing for gets it BACK
(SPEC.md 25.9.5).

A DOS box claims the whole arena (SPEC.md 96.35), which is a full compaction,
which correctly drops the machine-wide icon store - it is `MEM_PG_TRIV`
precisely so that this is the cheap thing to give up. What was missing is the
other half of cheap: getting it back. A repaint is not a mount, so every Disk
window on screen went on drawing SPEC.md 25's generic icon until the user
navigated somewhere else.

THIS ROW IS THE ROUND TRIP AND IT ASSERTS THE MIDDLE OF IT, because the two
ends look identical on a kernel where the notice is never raised: the store is
lazily re-claimed by the first lookup either way, so `ico_seg` is non-zero
again at the end whether or not anything repaired the REFERENCES. What tells
the two apart is the state WHILE the program is running - `[ico_n]` cleared
and the window owing `FSD_ICONS` - and the reference byte afterwards.

Four checks:

  1. the store holds rows before the launch, and the window has a cache
  2. INSIDE the DOS program: the store is gone, `[ico_n]` is 0 (which is
     `ico_demote`'s half - SPEC.md 54.7.4's guard in `asc_use` is a compare on
     that byte and was inert while the shed left it set), and the window owes
     FSD_ICONS
  3. after the box closes: the debt is spent and the store holds rows again
  4. ...and the window's own reference byte for the .COM RESOLVES - it names
     a row the store actually holds, which is the thing the user sees: a page
     icon rather than the generic diamond. It is checked against the live row
     count and not against the sentinel, because a repair that never runs
     leaves the byte exactly as it was and what moved under it is the store

VERIFIED RED both ways. With `ico_demote`'s call to `fmv_icostale` taken out,
check 2's debt is 0 and check 4 reads 0xFF. With only the `[ico_n]` store
removed, check 2 reads the stale count and the repair then declines to
re-absorb, which check 3 sees as an empty store.

MartyPC: the box is a real DOS program taking the real arena, and no other
emulator here runs one.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/dossnd360.img"
MACHINE = "os8088_xt_hdd_sb"
NAME = "DOSSND.COM"

# kernel/files.inc - the state block, and the debt this row is about
FS_SIZE, FS_N, FS_VSEG, FS_IOFH, FS_DIRTY = 57, 6, 16, 15, 19
FSD_ICONS = 3
FV_ICOIX = 0                    # the reference index's own base in a slot
ICO_R_NONE = 0xFF

bad = []


def fail(msg):
    print("icoshed: FAIL: %s" % msg)
    sys.exit(1)


def store(m):
    return (struct.unpack("<H", m.read(m.sym("ico_seg"), 2))[0],
            m.read(m.sym("ico_n"), 1)[0])


def slot0(m):
    b = bytes(m.read(m.sym("fm_pool"), FS_SIZE))
    return (struct.unpack_from("<H", b, FS_N)[0],
            struct.unpack_from("<H", b, FS_VSEG)[0],
            b[FS_IOFH], b[FS_DIRTY])


def refbyte(m, row=0):
    """The acting window's reference byte for entry `row` (SPEC.md 25.9)."""
    n, vseg, iofh, _ = slot0(m)
    if not vseg or not iofh:
        return None
    off = (iofh << 8) + FV_ICOIX + row
    return m.readseg(vseg, off, 1)[0]


def quiet(m):
    """The store, the window's state block and the disk all holding still -
    the aftermath of a mount, an exit or a close, read in GUEST time rather
    than slept through."""
    os88marty.quiesce(m, lambda: (store(m), slot0(m), refbyte(m),
                                  m.disk().get("reads")),
                      guest=1.0, what="the store and the window's state")


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=APPS, machine=MACHINE) as ui:
        m = ui.m
        ui.open_drive("B")
        # the mount's icon rows and the window's cache: what is read next
        quiet(m)

        seg0, n0 = store(m)
        fsn, vseg, iofh, dirty = slot0(m)
        print("icoshed: before  ico_seg=%04X ico_n=%d   window N=%d VSEG=%04X "
              "IOFH=%02X DIRTY=%d" % (seg0, n0, fsn, vseg, iofh, dirty))
        if not n0:
            fail("the store holds NO rows before the launch, so shedding it "
                 "cannot lose anything and this row asserts nothing. %s "
                 "should have a composed document body (SPEC.md 54.3)" % NAME)
        if not vseg:
            fail("the Disk window has no listing cache of its own, so it "
                 "holds no reference byte and there is nothing to dangle")
        ref0 = refbyte(m)
        print("icoshed: ...and its reference byte for entry 0 is %02X" % ref0)

        # --- the shed: a DOS program takes the whole arena -----------------
        w = ui.path("B:/" + NAME)
        if not w:
            fail("double-clicking %s opened no window" % NAME)
        # The shed and the notice it raises, in the GUEST time an idle box's
        # five seconds bought; what is there at the end is what is checked.
        try:
            os88marty.until(m, lambda _m: store(m)[0] == 0
                            and slot0(m)[3] == FSD_ICONS,
                            "the store shed", poll=0.3,
                            limit=5 * os88marty.GUEST_PACE
                            / os88marty.GUEST_BUDGET_RATIO)
        except os88marty.MartyError:
            pass

        seg1, n1 = store(m)
        _, _, _, dirty1 = slot0(m)
        print("icoshed: in DOS  ico_seg=%04X ico_n=%d   window DIRTY=%d"
              % (seg1, n1, dirty1))
        if seg1:
            fail("the store was NOT shed (ico_seg=%04X): the DOS box did not "
                 "take the whole arena, so there is nothing to repair and "
                 "every check below would pass for the wrong reason" % seg1)
        if n1:
            bad.append("[ico_n] is %d with the store GONE - ico_demote did "
                       "not clear it (SPEC.md 25.9.5). SPEC.md 54.7.4's "
                       "guard in asc_use is a compare on this byte, so a "
                       "stale count makes the next mount skip the ASSOC.DAT "
                       "re-read and pay a sector per package instead" % n1)
        if dirty1 != FSD_ICONS:
            bad.append("the window owes %d and not FSD_ICONS (%d): "
                       "fmv_icostale did not raise the notice, so nothing "
                       "will repair the references" % (dirty1, FSD_ICONS))

        # --- and back --------------------------------------------------------
        m.key("Escape")                 # the program's key: it exits, and
        quiet(m)                        # the box puts the drivers back
        ui.close(w)
        quiet(m)                        # ...and the repair's ASSOC.DAT read

        seg2, n2 = store(m)
        _, _, _, dirty2 = slot0(m)
        ref2 = refbyte(m)
        print("icoshed: after   ico_seg=%04X ico_n=%d   window DIRTY=%d "
              "ref=%02X" % (seg2, n2, dirty2, ref2))
        if dirty2:
            bad.append("the window still owes %d after the box closed: the "
                       "debt was never spent, so the repair did not run"
                       % dirty2)
        if not n2:
            bad.append("the store is still EMPTY: fmv_icorefs either refused "
                       "or its asc_use re-read did not happen")
        # **THE REFERENCE HAS TO RESOLVE, not merely not be the sentinel.**
        # A repair that never runs leaves the byte exactly as it was - row 0,
        # a perfectly ordinary value - and what changed under it is that row 0
        # no longer exists. So the check is against the live row count, which
        # is the condition dsk_ico_stage actually applies; testing the byte
        # alone passes on the broken kernel.
        if ref2 == ICO_R_NONE or ref2 >= n2:
            bad.append("entry 0's reference is %02X against %d live row(s), "
                       "so it resolves to nothing and %s draws SPEC.md 25's "
                       "generic icon - which is the defect exactly as the "
                       "field reported it" % (ref2, n2, NAME))

    if bad:
        print("icoshed: FAIL")
        for b in bad:
            print("  - %s" % b)
        return 1
    print("icoshed: the store was shed, the notice was raised and the "
          "window's references came back for one ASSOC.DAT read")
    return 0


if __name__ == "__main__":
    sys.exit(main())

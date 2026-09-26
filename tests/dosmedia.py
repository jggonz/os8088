#!/usr/bin/env python3
"""A FLOPPY SWAPPED UNDER A RUNNING PACKAGE (SPEC.md 18.9.1.1).

Reported from the field: the DOS box standing on B:, `DIR` correct, a
DIFFERENT 720KB disk inserted, `DIR` again - and the listing was the old
disk's, permanently, until the program was closed and reopened.

**THE HARNESS CANNOT SWAP A FLOPPY UNDER A RUNNING GUEST** - MartyPC's debug
server has `disks` and `flush` and no `insert` - so this row cannot stage the
report directly. What it CAN assert is the thing the report is a symptom of,
and it is a sharper assertion than the swap would be: **does a quiet re-stand
on a floppy re-read LBA 0?**

That one sector is the whole mechanism. SPEC.md 18.95's read-ahead is keyed on
(volume, [dsk_sigcur]), and [dsk_sigcur] is the position-sensitive sum of the
boot sector THIS MOUNT READ. If nothing re-reads it the key cannot change, so
the cache serves the old disk for ever - which is exactly what the field saw,
and it measured as `reads=0` on every DIR.

So the row is a two-sided budget on `m.disk()`, counted at the CONTROLLER
rather than in the guest:

  1  after the motor has stopped, a DIR costs AT LEAST ONE read. At zero the
     medium is never checked and a swap is invisible. This is the defect.
  2  ...and NOT MANY. The point of 18.9.1 is that the identity check is one
     sector and the directory still comes out of RAM, so a DIR that re-reads
     the whole directory is 18.95 undone - a different regression, the same
     row, the other direction.
  3  ...and the byte the predicate RESTS ON is really maintained by this
     BIOS. 18.9.1 skips the mount while a motor is spinning, read out of
     0040:003F, and a BIOS that never set that bitmap would make the skip
     dead code - correct, because the failure is a wasted mount, and silently
     costing every re-stand a revolution. Measured here: the bit for B: is
     seen set in 65 of 825 samples across one DIR, so it is live.

     **IT IS NOT "TWO DIRs BACK TO BACK", which is what this asserted first
     and which passed while measuring nothing.** Typing a command goes
     through `settle`, which is seconds of host time, so the motor has
     always stopped by the second one and both cost the same single read -
     an assertion of `burst <= n` that is 1 <= 1 for ever. A green row that
     tests nothing is worse than no row (docs/WRITING-TESTS.md 1).

Break it on purpose: take `FCPX dsk_media_ok` out of `fcp_goto` and assertion
1 reads 0.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import doscon                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/doscom360.img"
BOX = "A:/APPS/DOS.O88"

# The motor-off countdown is `dsk_dpt` byte 2 = 0x25 = 37 ticks = 2.03s
# (SPEC.md 18.9.1). Four GUEST seconds is comfortably past it and still cheap.
SPUNDOWN = 4.0
# One sector is the boot sector. A root directory is seven of them on a 720KB
# disk and the FAT is three, so anything at or above this is the cache gone.
TOOMANY = 6


def fail(msg):
    print("dosmedia: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - a plain `make` builds it" % p)

    with os88ui.boot(SYS, apps=APPS) as ui:
        m = ui.m
        w = ui.path(BOX)
        bx = doscon.Box(m)

        bx.type("B:\n")                      # ...a FLOPPY, which is the whole
        if bx.b("dos_vol") != 1:             # subject: a driver volume is not
            fail("the box did not move to B:, it is on volume %d"
                 % bx.b("dos_vol"))

        # --- 1 and 2: a DIR with the motor stopped ------------------------
        os88marty.guest_sleep(m, SPUNDOWN)
        m.disk(reset=True)
        bx.type("DIR\n")
        d = m.disk()
        n = d["reads"]
        rows = bx.live()
        if not any("File(s)" in r for r in rows[-3:]):
            fail("DIR printed no listing at all: %r" % rows[-4:])
        if n == 0:
            fail("a DIR after %.0fs idle cost ZERO controller reads, so "
                 "nothing re-read LBA 0 and [dsk_sigcur] cannot change - "
                 "SPEC.md 18.95's cache is keyed on it, so a floppy swapped "
                 "under this box is invisible for ever (SPEC.md 18.9.1.1)"
                 % SPUNDOWN)
        if n >= TOOMANY:
            fail("a DIR cost %d controller reads, and the identity check is "
                 "ONE sector - the directory is supposed to still come out of "
                 "SPEC.md 18.95's cache. %d reads is that cache gone" % (n, n))
        print("dosmedia: a DIR with the motor stopped costs %d read(s) - the "
              "medium is checked and the directory is not re-read" % n)

        # --- 3: ...and the predicate's own input is alive on this machine --
        # 18.9.1 skips the mount while a motor is SPINNING, which it reads
        # out of 0040:003F. A BIOS that never sets that bitmap makes the skip
        # dead code: still correct - the failure is a wasted mount - but
        # every re-stand then costs a revolution and nothing says so. So the
        # byte is sampled while a DIR is actually running.
        seen = {}
        m.type_text("DIR\n")
        # sampled over GUEST time - what 6s gave on an idle box - and ended
        # early by the answer, which is all the assertion asks for
        c0, span = m.status()["cycles"], 6.0 * (os88marty.GUEST_PACE or 4.5)
        while (m.status()["cycles"] - c0) / os88marty.GUEST_HZ < span:
            seen[m.read(0x400 + 0x3F, 1)[0]] = 1
            if 2 in seen:
                break
        os88marty.settle(m)
        if 2 not in seen:                    # bit 1 = drive 1 = B:
            fail("0040:003F never showed B:'s motor bit while a DIR ran - it "
                 "held %r. SPEC.md 18.9.1's skip reads that bitmap, so a BIOS "
                 "that does not maintain it makes the predicate dead code and "
                 "every quiet re-stand costs a revolution"
                 % sorted(seen))
        print("dosmedia: ...and 0040:003F really carries B:'s motor bit, so "
              "18.9.1's skip is live here and not dead code")

    print("dosmedia: ok")


if __name__ == "__main__":
    main()

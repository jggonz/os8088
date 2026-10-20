#!/usr/bin/env python3
"""The Memory page's estimate is what the machine would really hand over.

    python3 tests/dosest.py [--machine NAME]

SPEC.md 96.36.3.1 and 51.12.2 - TWO defects the field found in one sitting,
on an IBM 5150 with a Sound Blaster. The page read `433K`, then `467K` on
going back into it with nothing changed, and the program got `447`.

**Both halves need a SOUND CARD to be visible at all**, which is why this is
its own row rather than two assertions inside tests/dosram.py: the sound term
is the only one added to the estimate unconditionally, so on a machine with
no card the arithmetic under test never runs.

  1. THE ORDER. `dos_mck_place` reads the three class words the arena is a
     sum of and DRAWS NOTHING, so it sat below the arena block in
     dos_paint_mem. The FIRST paint of a fresh window therefore computed the
     figure from the bss zeros the loader left - 433 - and every recompute
     after it had the real terms - 467. A control that is a term of a figure
     has to be read before the figure.

     It is invisible to a probe that looks afterwards: `[dos_msnk]` reads
     correctly by the time the paint RETURNS, because the routine that fills
     it runs later in that same paint. Only the drawn row remembers. So this
     compares the row as first painted against the row after a recompute that
     changes nothing, and they must agree.

  2. THE FIGURE. `OSAPI_DRV_CLASSK`'s plain form quoted `drv_memk`, which is
     SPEC.md 51.2.4's "while it is doing its primary job, at the TOP RUNG of
     any claim it sizes to the machine" - 6 image + 8 DMA + 20 SBL_POOLKB for
     the Sound Blaster. The pool is claimed ON THE FIRST GRANT, so a mounted
     silent driver holds 14 and the estimate was 20 high for ever. The plain
     form weighs the heap now, and this row re-derives the same sum from
     `OSAPI_CLAIM_SNAPSHOT`'s own data - the image record (owned by
     MEM_K_DRV, based at DRVR_SEG) plus everything owned BY that segment -
     so the assertion is against the machine and not against a constant.

WHAT IT WOULD CATCH: the read hoisted back below the arena (1); either form
of OSAPI_DRV_CLASSK fed to the other's caller (2); and a driver growing a
claim that the sum does not see, which would show up here as the page and the
heap disagreeing rather than as a number nobody can check.

It runs on MartyPC and must: no other emulator here models a Sound Blaster.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty as M                                      # noqa: E402
import os88mouse                                           # noqa: E402
import os88ui                                              # noqa: E402
import os88geom                                            # noqa: E402
import dosmap                                              # noqa: E402

SYS = os.path.join(ROOT, "build", "os8088-360.img")
APPS = os.path.join(ROOT, "build", "apps360.img")
MACHINE = "os8088_5150_herc_sb_gla"


def fail(msg):
    print("dosest: FAIL: %s" % msg)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    a = ap.parse_args()
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - run `make` first" % p)

    with os88ui.boot(SYS, apps=APPS, machine=a.machine) as ui:
        m = ui.m
        mo = os88mouse.Mouse(marty=m)
        snd = struct.unpack("<H", m.read(m.sym("drv_tab")
                                         + os88geom.DRVR_SEG, 2))[0]
        if not snd:
            fail("SOUND.DRV is not mounted on %s. The sound term is the only "
                 "one this page adds unconditionally, so with no card there "
                 "is no arithmetic here to test (SPEC.md 51.3.1)" % a.machine)

        if not ui.path("A:/APPS/DOS.O88"):
            fail("double-clicking A:/APPS/DOS.O88 opened no window")
        M.settle(m)
        dm = dosmap.package()
        ps = dosmap.instance(m)

        def pb():
            return dosmap.instance(m) << 4

        def word(n):
            return int.from_bytes(m.read(pb() + dm[n], 2), "little")

        def arena():
            raw = m.read(pb() + dm["dos_marn"], 8).split(b"\0")[0]
            try:
                return int(raw.decode("latin-1").rstrip("KB").strip())
            except ValueError:
                fail("the arena row reads %r and should be digits and a KB - "
                     "the page has never been painted (SPEC.md 96.36.3)" % raw)

        # --- 1: the FIRST paint against a recompute that changes nothing -----
        mo.click(*dosmap.centre(m, ps, dm, "dos_erect"))
        M.settle(m)
        if m.read(pb() + dm["dos_page"], 1)[0] != dm["DOS_PAGE_SET"]:
            fail("the bar's button did not reach the Setup page")
        first = arena()
        print("dosest: the first Setup paint reads %d K, [dos_msnk]=%d"
              % (first, word("dos_msnk")))

        # Away to the other arm and straight back: it repaints the block twice
        # and recomputes the figure both times, and changes NOTHING about the
        # machine - so the two readings are the same question asked twice.
        rr = [int.from_bytes(m.read(pb() + dm["dos_mrad"] + 2 * i, 2), "little")
              for i in range(4)]
        pitch = int.from_bytes(m.read(pb() + dm["dos_mrad"] + 14, 2), "little")
        mo.click((rr[0] + rr[2]) // 2, rr[1] + pitch + pitch // 2)
        M.settle(m)
        mo.click((rr[0] + rr[2]) // 2, rr[1] + pitch // 2)
        M.settle(m)
        if m.read(pb() + dm["dos_keepc"], 1)[0] != 0:
            fail("could not get back to `Inside the OS`; the pick is %d"
                 % m.read(pb() + dm["dos_keepc"], 1)[0])
        again = arena()
        if first != again:
            fail("the first Setup paint said %d K and a recompute that "
                 "changed NOTHING says %d. dos_mck_place fills the three "
                 "class words the arena is a sum of and it draws nothing, so "
                 "it must run BEFORE the arena block in dos_paint_mem - below "
                 "it, the first paint of a fresh window adds the bss zeros "
                 "the loader left (SPEC.md 96.36.3.1)" % (first, again))
        print("dosest: ...and %d K again after a recompute - the terms are "
              "read before the figure" % again)

        # --- 2: the sound term is what the class HOLDS, off the heap ---------
        # Re-derived from the claim table rather than from any constant: the
        # image is owned by MEM_K_DRV and based at the driver's segment, and
        # everything the driver claimed for itself carries that segment as its
        # owner word (SPEC.md 50.2's `mov bx, es`).
        raw = bytes(m.read(m.sym("mem_tab"), 32 * os88geom.MC_SIZE))
        para = 0
        for i in range(32):
            r = raw[i * os88geom.MC_SIZE:(i + 1) * os88geom.MC_SIZE]
            base, sz, own = struct.unpack_from("<HHH", r, 0)
            if not base:
                continue
            if own == snd or (own == os88geom.MEM_K_DRV and base == snd):
                para += sz
        held = para >> 6
        msnk = word("dos_msnk")
        if not held:
            fail("SOUND.DRV is at %04X and the claim table has nothing owned "
                 "by it or based there - either the driver is gone or this "
                 "row is reading mem_tab wrongly" % snd)
        if msnk != held:
            fail("the page's sound term is %d K and the class is HOLDING %d. "
                 "OSAPI_DRV_CLASSK's plain form must weigh the heap, not "
                 "drv_memk - DRVM_SND is 7 image + 16 SBL_PLAYKB, what it holds "
                 "PLAYING, and the pool is claimed on the FIRST GRANT, so a "
                 "mounted silent driver holds its 7 and the constant is 16 "
                 "high for ever (SPEC.md 51.12.2, 34.5.2)"
                 % (msnk, held))
        print("dosest: the sound term is %d K and the class holds %d K - the "
              "same number, off the same heap" % (msnk, held))
        print("dosest: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

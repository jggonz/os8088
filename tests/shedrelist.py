#!/usr/bin/env python3
"""A Disk window whose listing cache was SHED re-lists on its next focus
(kern_small, SPEC.md 50.6).

    make small smallapps && python3 tests/shedrelist.py

On the 128KB floor machine Paint's launch is the heap pressure that EVICTS
the GAMES window's MEM_P_VIEW claim (SPEC.md 50.6.13): fmv_demote zeroes FS_VSEG and owes an
FSD_CACHE re-list, which fm_focus pays with fmv_hload when the window is
raised. The re-list used to name its destination from FS_VSEG BEFORE
fmv_store's fmv_fit re-claimed the store, so the mount went quiet into a 0
store and the window read "Drive B: 0 files" until Refresh - reported from
the field. fmv_load now re-claims first.

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1), measured: take the `call
fmv_fit` out of fmv_load and the raise leaves 0 entries of 10
(measured 2026-10-14, with Missile as the pressure).

THE PRESSURE IS PAINT, opened from A: (SPEC.md 50.6.13). It was Gorillas
alone until the 2026-10 size passes, then Missile and Gorillas - two REGIONS,
which come down from the top - and since a claim that is not a cache evicts
only the caches it LANDS on, a region no longer reaches a store at the
floor: Gorillas' 46KB evicted the read-ahead and the icon store above GAMES'
listing and left the listing alone (dirty FSD_ICONS, the store intact). A
DATA claim lands at the floor, so Paint's canvas evicts the GAMES store
(measured: 0E80, FSD_CACHE, then 13 of 13 entries after the raise). The
shed check below stays, so a machine that stops reaching the case still says
so rather than passing.

Every reading is guest state - the window's own FS_DIRTY/FS_VSEG and the
listing os88ui reads off its cache - and the pauses are GUEST seconds.
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88build as _B                                   # noqa: E402
_B.use_build("build/smallk")
os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")

import os88marty                                         # noqa: E402
import os88sym                                           # noqa: E402
import os88ui                                            # noqa: E402

MACHINE = "os8088_5150_gla_128k"


def main():
    os.chdir(ROOT)
    eq = os88sym.equates(("KERN_SMALL",))
    with os88ui.boot("build/small360.img", apps="build/smallapps360.img",
                     machine=MACHINE,
                     quick=True) as ui:
        ui.still(0)     # booted on STATE; the first paint done (SOAK-SPEED 4)
        m = ui.m
        ui.open_drive("B")
        ui.open("GAMES")

        def games():
            return [w for w in ui.windows() if w.title.startswith("GAMES")][0]

        def state():
            w = games()
            b = ui._fsblk(w)
            return (m.read(b + eq["FS_DIRTY"], 1)[0],
                    int.from_bytes(m.read(b + eq["FS_VSEG"], 2), "little"),
                    len(ui.listing(w)))
        before = state()[2]
        ui.path("A:/APPS/PAINT.O88")    # the pressure: its canvas lands at
        os88marty.ui_done(m)            # the floor, on GAMES' store (doc)
        shed = state()
        print("  before %d entries; after the launch dirty %d, store %04x, "
              "%d entries" % ((before,) + shed))
        if shed[0] == 0 or shed[1] != 0:
            print("FAIL: the launch did not shed the window's store - this "
                  "machine no longer reaches the case, so the row proves "
                  "nothing")
            return 1
        ui.raise_window(games())
        os88marty.pace(m, 3)
        after = state()
        print("  after the raise: dirty %d, store %04x, %d entries"
              % after)
        if after[0] != 0 or after[2] != before:
            print("FAIL: the shed window re-listed %d of %d entries"
                  % (after[2], before))
            return 1
    print("shedrelist: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

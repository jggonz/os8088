#!/usr/bin/env python3
"""A Disk window whose listing cache was SHED re-lists on its next focus
(kern_small, SPEC.md 50.6).

    make small smallapps && python3 tests/shedrelist.py

On the 128KB floor machine Gorillas' launch is the heap pressure that sheds
the GAMES window's MEM_P_VIEW claim: fmv_demote zeroes FS_VSEG and owes an
FSD_CACHE re-list, which fm_focus pays with fmv_hload when the window is
raised. The re-list used to name its destination from FS_VSEG BEFORE
fmv_store's fmv_fit re-claimed the store, so the mount went quiet into a 0
store and the window read "Drive B: 0 files" until Refresh - reported from
the field. fmv_load now re-claims first.

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1), measured: take the `call
fmv_fit` out of fmv_load and the raise leaves 0 entries of 14.

Every reading is guest state - the window's own FS_DIRTY/FS_VSEG and the
listing os88ui reads off its cache - and the pauses are GUEST seconds:
Gorillas' setup screen blinks a cursor, so a screen settle never ends.
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
                     machine=MACHINE) as ui:
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
        ui.open("GORILLAS.O88")
        os88marty.pace(m, 3)
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

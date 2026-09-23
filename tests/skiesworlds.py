#!/usr/bin/env python3
"""Does EVERY Clear Skies location load its world and fly? (SPEC.md 88.10.5)

    make && python3 tests/skiesworlds.py

**IT EXISTS BECAUSE NOTHING FLEW SAN FRANCISCO.** `skieswater` visits LBG, LCY
and JFK, `skiesgeom` both Paris runways, and every other skies row takes the
default - so the one location whose world is the LAST stream in the package
file was never picked by anything, and shipped unflyable. The field found it:
*"I'm unable to fly in san fran - clicking the fly button does nothing (no
error, but also, no flying)."*

`cs_wldget` asked `OSAPI_FILE_READ_AT` for a capacity rounded UP to whole
clusters - which §20.14.3 requires - and then checked the DELIVERED count
against that same rounded capacity. Every stream but the last has more file
behind it and filled it by accident; the last one ends at EOF and never can, so
`cs_wldpick` returned CF=1 and `cs_cmd_fly`'s `jc .out` made Fly a no-op
(SPEC.md 88.10.5.4.1).

**THE ASSERTION IS THE WORLD THAT ARRIVED, not that the screen changed.** A
silent load failure leaves the launcher on screen and takes no mode, so a
pixel test would be asking a question with no answer; `[cs_wldnow]` is one byte
and says exactly which world is in the overlay. It is checked against
`cs_apwld`'s own row - read out of the guest rather than hard-coded here, so
adding a location or re-sorting the list cannot make this row quietly wrong.

**EVERY ROW IS INDEPENDENT.** `[cs_wldnow]` is forced to 0FFh before each
attempt, which is the first-pick path (vocabulary AND world, SPEC.md 88.10.5) -
so a location cannot pass on the world its predecessor happened to leave in the
overlay, and `cs_wldpick`'s idempotent arm is not what is being tested. Nine
full loads is the point of the row rather than a cost to trim.

Measured, before the fix and after: eight locations flew either way and
**SFO flew only after** - `cs_wldnow` FF, nothing loaded, on every geometry.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)

import os88marty                                            # noqa: E402
import dispapps                                             # noqa: E402
from skies import open_game, Bss                            # noqa: E402

MACHINE = "os8088_5150_herc_gla"
NONE = 0xFF                             # [cs_wldnow]: no world in the overlay


def apwld(m, seg, mp, n):
    """cs_apwld, out of the GUEST - one byte a location, the world it stands
    in. Read rather than restated, so a new location cannot make this row
    assert the old list."""
    return list(m.readseg(seg, mp["cs_apwld"], n))


def names(m, seg, mp, n):
    """cs_apnames' strings, for a message that names the place."""
    out = []
    for i in range(n):
        p = int.from_bytes(m.readseg(seg, mp["cs_apnames"] + 2 * i, 2), "little")
        s = bytearray()
        while True:
            c = m.readseg(seg, p + len(s), 1)[0]
            if not c or len(s) > 30:
                break
            s.append(c)
        out.append(s.decode("latin-1"))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args()

    bad = []
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        slot, seg, base = open_game(m)
        r = Bss(m, seg, base)
        mp = dispapps._map("skies")
        # HOW MANY, off the guest's own layout: cs_apnames is one WORD per
        # location and cs_apwld follows it, so the gap is the count. CS_NPORTS
        # is an `equ` and so is not in the map at all, and a literal 9 here is
        # a number that goes stale the day a tenth location lands.
        n = (mp["cs_apwld"] - mp["cs_apnames"]) // 2
        if not 1 <= n <= 32:
            sys.exit("skiesworlds: cs_apnames..cs_apwld is %d bytes, which is "
                     "not a location list - the map has moved under this row"
                     % (mp["cs_apwld"] - mp["cs_apnames"]))
        wld = apwld(m, seg, mp, n)
        loc = names(m, seg, mp, n)
        print("  %d locations, worlds %s" % (n, wld))

        if r.byte("cs_want") == 0:      # CSB_NONE - the Fly button is greyed
            sys.exit("skiesworlds: this display offers no mode to fly in, so "
                     "no location can be tested")

        for i in range(n):
            m.pause()
            r.poke("cs_apnow", bytes([i]))
            r.poke("cs_wldnow", bytes([NONE]))   # the FULL load, every time
            r.poke("cs_inited", b"\x00")
            r.poke("cs_back", b"\x00")
            r.poke("cs_quit", b"\x00")
            m.run()
            m.type_text("f")
            flew = False
            for _ in range(25):
                m.advance(frames=20)
                m.run()
                if r.byte("cs_back"):
                    flew = True
                    break
            got = r.byte("cs_wldnow")
            if not flew:
                bad.append(loc[i])
                print("  FAIL %-12s row %d: Fly did NOTHING - [cs_wldnow] is "
                      "%s, so cs_wldpick refused and cs_cmd_fly's `jc .out` "
                      "took it (SPEC.md 88.10.5.4.1)"
                      % (loc[i], i,
                         "FF, nothing loaded" if got == NONE else "%d" % got))
            elif got != wld[i]:
                bad.append(loc[i])
                print("  FAIL %-12s row %d: flew with world %d in the overlay "
                      "where cs_apwld says %d - the record cs_airport points "
                      "at belongs to a different country"
                      % (loc[i], i, got, wld[i]))
            else:
                print("  ok   %-12s row %d -> world %d" % (loc[i], i, got))

            if flew:                                    # back to the launcher
                m.type_text("f")
                for _ in range(40):
                    m.advance(frames=20)
                    m.run()
                    if r.byte("cs_quit"):
                        break
                m.advance(frames=60)
                m.run()

    if bad:
        print("skiesworlds: FAILED - %s" % ", ".join(bad))
        return 1
    print("skiesworlds: ok - all %d locations fly" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())

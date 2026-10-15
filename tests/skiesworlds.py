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

**AND THE BYTES, since the streams are ONE part (SPEC.md 88.10.7).** A world
is found by its BYTE offset inside csw.z now, so an offset table one stream
out reads a neighbour's bytes - which may well expand (it is a valid stream),
leave `[cs_wldnow]` right and draw another country. So every flight also holds
the overlay, vocabulary and world, to what `tools/csworlds.py` assembles for
that world; and after the last flight's way out, the UNION to the title bands
the program read back into it - which since SPEC.md 88.10.7.1 is the KEPT
packed copy in `cs_artz` exact, then the title and the plane in use expanded and
NOTHING ELSE, then each of the other bands as a repaint draws it.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)

import os88marty                                            # noqa: E402
import dispapps                                             # noqa: E402
import csworlds                                             # noqa: E402
from skies import open_game, Bss                            # noqa: E402

MACHINE = "os8088_5150_herc_gla"
NONE = 0xFF                             # [cs_wldnow]: no world in the overlay
# a plane record's band: skies.asm's own equ, read rather than restated - an
# equ is not in nasm's map
CSP_ART = int(re.search(r"^CSP_ART\s+equ\s+(\d+)", open(os.path.join(
    ROOT, "apps", "skies", "skies.asm")).read(), re.M).group(1))
PLANES = ("cs_p_c172", "cs_p_pitts", "cs_p_fouga", "cs_p_bijave", "cs_p_a5")


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
            # THE BYTES ARE READ AT THE PICK, before a frame runs: an object
            # record carries state the renderer writes (a LOD's hysteresis
            # bit, 88.5.4), so the overlay a moment into a flight is not the
            # world as it arrived and a compare there fails on content
            m.bp_exec(seg * 16 + mp["cs_cmd_fly.picked"])
            m.run()
            m.type_text("f")
            have = None
            if m.wait_stop(60):
                want = csworlds.overlay(csworlds.WORLDS[wld[i]])
                lo = csworlds.CS_VOCAB_AT
                hi = csworlds.CS_WLD_AT + len(csworlds.assemble(
                    csworlds.WORLDS[wld[i]], os.path.join(ROOT, "build"))[0])
                have = m.readseg(seg, lo, hi - lo)
            m.bp_exec()
            m.run()
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
                # THE BYTES, as they arrived: the overlay holds the vocabulary
                # and this world exactly as csworlds.py assembles them at this
                # tree's address
                if have is None or have != want[lo:hi]:
                    bad.append(loc[i])
                    print("  FAIL %-12s row %d: world %d is in [cs_wldnow] but "
                          "the overlay's bytes are not that world's - %d of %d "
                          "differ (SPEC.md 88.10.7: a stream offset out by one)"
                          % (loc[i], i, got,
                             sum(x != y for x, y in zip(have or b"",
                                                        want[lo:hi])),
                             hi - lo))
                else:
                    print("  ok   %-12s row %d -> world %d, %d bytes exact"
                          % (loc[i], i, got, hi - lo))

            if flew:                                    # back to the launcher
                m.type_text("f")
                for _ in range(40):
                    m.advance(frames=20)
                    m.run()
                    if r.byte("cs_quit"):
                        break
                m.advance(frames=60)
                m.run()

        # ...AND THE TITLE BANDS (SPEC.md 88.10.7, 88.10.7.1). The art stream
        # is KEPT packed in cs_artz from the launch on, and a band is expanded
        # into the union only when it is first DRAWN after a flight - so the
        # way out leaves exactly the title and the aeroplane in use, and each
        # of the other four arrives when a repaint asks for it. cs_kx is the
        # union's base: an equ, in the map. The band table is read out of the
        # GUEST (cs_artroff), so a re-cut csart.inc cannot make this row
        # assert the old one.
        m.pause()
        art = open(os.path.join(ROOT, "build", "csart.bin"), "rb").read()
        csz = open(os.path.join(ROOT, "build", "csw.z"), "rb").read()
        wof = [int.from_bytes(m.readseg(seg, mp["cs_wstroff"] + 2 * i, 2),
                              "little") for i in range(11)]
        kept = csz[wof[9]:wof[10]]
        nb = (mp["cs_artzoff"] - mp["cs_artroff"]) // 2 - 1
        roff = [int.from_bytes(m.readseg(seg, mp["cs_artroff"] + 2 * i, 2),
                               "little") for i in range(nb + 1)]
        plane0 = int.from_bytes(m.readseg(seg, base + dispapps.bss_off(
            "skies", "cs_plane"), 2), "little")

        def bandof(rec):
            o = int.from_bytes(m.readseg(seg, rec + CSP_ART, 2), "little")
            return roff.index(o) if o in roff[:-1] else -1

        def held():
            """[cs_artok], and the bands it claims whose bytes are wrong."""
            ok = r.byte("cs_artok")
            have = m.readseg(seg, mp["cs_kx"], len(art))
            wrong = [i for i in range(nb) if ok >> i & 1
                     and have[roff[i]:roff[i + 1]] != art[roff[i]:roff[i + 1]]]
            return ok, wrong

        zok = r.byte("cs_artzok")
        zhave = m.readseg(seg, mp["cs_artz"], len(kept))
        if not zok or zhave != kept:
            bad.append("the kept bands")
            print("  FAIL kept bands: [cs_artzok] %d, %d of %d bytes of cs_artz "
                  "differ from csw.z's art stream (SPEC.md 88.10.7.1)"
                  % (zok, sum(x != y for x, y in zip(zhave, kept)), len(kept)))
        else:
            print("  ok   bands kept packed in cs_artz, %d bytes exact"
                  % len(kept))
        ok, wrong = held()
        want = 1 | 1 << bandof(plane0)
        if ok != want or wrong:
            bad.append("the title bands")
            print("  FAIL after the way out: [cs_artok] %02X where the title "
                  "and the plane in use are %02X, bands %s wrong"
                  % (ok, want, wrong))
        else:
            print("  ok   after the way out: the title and band %d, exact, and "
                  "nothing else expanded" % bandof(plane0))
        # ...and every other band ON DEMAND: a key on the instructions page
        # goes back to the title page through cs_repaint, which draws whatever
        # [cs_plane] names
        for rec in PLANES:
            m.pause()
            r.poke("cs_plane", mp[rec].to_bytes(2, "little"))
            r.poke("cs_page", b"\x01")
            m.run()
            m.type_text("x")
            for _ in range(20):
                m.advance(frames=10)
                m.run()
                if r.byte("cs_page") == 0 and \
                        r.byte("cs_artok") >> bandof(mp[rec]) & 1:
                    break
        m.pause()
        ok, wrong = held()
        r.poke("cs_plane", plane0.to_bytes(2, "little"))
        m.run()
        if ok != (1 << nb) - 1 or wrong:
            bad.append("the bands on demand")
            print("  FAIL on demand: [cs_artok] %02X after every plane was "
                  "drawn, bands %s wrong" % (ok, wrong))
        else:
            print("  ok   on demand: all %d bands expanded as they were drawn, "
                  "%d bytes exact" % (nb, len(art)))

    if bad:
        print("skiesworlds: FAILED - %s" % ", ".join(bad))
        return 1
    print("skiesworlds: ok - all %d locations fly" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""THE SECOND AEROPLANE FLIES BY ITS OWN MODEL (SPEC.md 88.7.2) and wears its
own panel (88.9.3), on MartyPC.

    python3 tests/skiespitts.py [--machine os8088_5150_herc_gla]

A plane record names a near proc in CSP_ATT, and that proc owns the two
attitude axes and the stall - which is the whole of what makes one aeroplane
feel unlike another. The Cessna's (`cs_att_trim`) clamps both axes and
returns them toward level; the Pitts Special's (`cs_att_free`) does neither.
So the row flies BOTH from the same pinned state and holds them apart:

  1. the Plane list carries the two this row flies, the trainer at row 0 and
     the Pitts at row 1 - it does NOT assert how many there are, which is
     what broke it when the fleet went from two to five (88.7.5);
  2. held hard over, the Pitts rolls PAST its record's MAXROLL and keeps
     going - right round through inverted, which the 16-bit angle does for
     nothing - while the Cessna stops at 60 degrees;
  3. released, the Pitts STAYS where the stick left it and the Cessna comes
     back toward level. That is the auto-level, and it is the difference a
     pilot notices first;
  4. held nose-up, the Pitts goes over the top: pitch passes 90 degrees,
     which the trainer's clamp forbids;
  5. its panel is its own arrangement. The LAYOUT ROWS are shared on purpose
     since 88.9.8 - a cockpit is a grid, and five aeroplanes wandering off
     it read as five accidents - so what this asserts is what actually
     differs: the attitude indicator sits off the centreline and at its own
     size, at least one readout is on the other side of the panel, and the
     two decoration sets are different lengths and different instruments.

The attitudes are pinned by poke rather than flown up to, for the reason
tests/skiesperf.py pins its scenes: two aeroplanes have to be asked the same
question from the same place, and a take-off roll is not the same place
twice.

--clobber-att is the red run (docs/WRITING-TESTS.md 1): it points the Pitts'
CSP_ATT at the trainer's model - the aeroplane without its flight model - and
check 4 must then fail, the nose stopping dead at 90 degrees where the free
model takes it over the top.

CHECKS 2 AND 3 SURVIVE THAT, and the reason is worth writing down. The
Pitts' record carries MAXROLL 32,767 for a reader's sake, and under the
CLAMPING model `cs_axis` compares a signed word against it - so the sum
overflows to negative before the compare can bite, and the roll wraps
anyway, by accident. A 180-degree limit is not a limit. `cs_att_free` never
reads either limit, so nothing shipped depends on that; the row asserts the
one behaviour the proc alone decides.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSP_MAXROLL, CSP_ATT = 28, 34
DEG = 65536.0 / 360.0
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-att", action="store_true",
                    help="give the Pitts the trainer's model: the row must go red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")

    def off(n):
        return dispapps.bss_off("skies", n)
    sg = lambda v: v - 0x10000 if v >= 0x8000 else v        # noqa: E731

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def w(name):
            return int.from_bytes(m.readseg(seg, base + off(name), 2), "little")

        def sw(name):
            return sg(w(name))

        def byte(name):
            return m.readseg(seg, base + off(name), 1)[0]

        def poke(name, data):
            m.write(lin + base + off(name), data)

        def rec(at, o):
            return int.from_bytes(m.readseg(seg, at + o, 2), "little")

        m.advance(frames=30)
        m.run()
        if a.clobber_att:
            m.pause()
            m.write(lin + mp["cs_p_pitts"] + CSP_ATT,
                    mp["cs_att_trim"].to_bytes(2, "little"))
            m.run()
            print("  (the Pitts given the trainer's CSP_ATT: this run must fail)")

        # --- 1. the list offers two, and the second is the Pitts -------------
        n = (mp["cs_plnames"] - mp["cs_planes"]) // 2    # the table's own length
        check(n >= 2, "the Plane list has the two this row flies (%d rows)" % n)
        second = rec(mp["cs_planes"], 2)
        check(second == mp["cs_p_pitts"],
              "...and row 1 is the Pitts (%04x)" % second)

        # --- 5. the panels are laid out differently --------------------------
        ck = {name: rec(rec(at, 32), 0) for name, at
              in (("c172", mp["cs_p_c172"]), ("pitts", mp["cs_p_pitts"]))}
        wins = {}
        for name, at in (("c172", mp["cs_p_c172"]), ("pitts", mp["cs_p_pitts"])):
            k = rec(at, 32)
            wp, nw = rec(k, 0), rec(k, 2)
            wins[name] = [tuple(sg(rec(wp, 8 * i + 2 * j)) for j in range(4))
                          for i in range(nw)]
            wins[name + "_deco"] = rec(k, 16)         # CSK_NDECO
        # ...they share the layout's ROWS by design (88.9.10), so the claim
        # is that at least one readout is on the other side of the panel -
        # a shared grid is not a shared panel
        moved = [(x, y) for x, y in zip(wins["c172"], wins["pitts"]) if x != y]
        check(moved and all(x[1] == y[1] for x, y in moved),
              "a readout is on the other side and the rows still line up "
              "(%d moved: %s)" % (len(moved), moved))
        for name, at in (("c172", mp["cs_p_c172"]), ("pitts", mp["cs_p_pitts"])):
            k = rec(at, 32)
            wins[name + "_adi"] = (sg(rec(k, 6)), sg(rec(k, 8)), sg(rec(k, 10)))
        check(wins["c172_adi"] != wins["pitts_adi"],
              "the attitude indicator is the Pitts' own: %s against the "
              "trainer's %s" % (wins["pitts_adi"], wins["c172_adi"]))
        check(wins["pitts_deco"] > 0 and wins["c172_deco"] > 0
              and wins["pitts_deco"] != wins["c172_deco"],
              "both wear decorations and they are not the same set "
              "(Pitts %d, trainer %d)"
              % (wins["pitts_deco"], wins["c172_deco"]))

        # --- fly each of them from the same pinned attitude ------------------
        def pick(row):
            po = [rec(mp["cs_drplane"], 2 * i) for i in range(4)]
            ui.mo.click((po[0] + po[2]) // 2, (po[1] + po[3]) // 2)
            m.advance(frames=20)
            m.run()
            # THE OPEN LIST'S FIRST ROW IS OS88UI_DR_TOP (SPEC.md 13.14.2)
            # and no longer the row under the box: a list that would not fit
            # below its control slides UP into the window. The Plane list is
            # short enough that the two agree today, and the arithmetic that
            # assumed it would drift silently the moment a sixth aeroplane is
            # written - it would click a row and pick another.
            top = rec(mp["cs_drplane"], 22)
            ui.mo.click(po[0] + 20, top + 1 + 12 * row + 6)
            m.advance(frames=20)
            m.run()

        def airborne():
            """Level at 600 m and 70 m/s, engine in, the world paused."""
            m.pause()
            for nm, v in (("cs_px", -2400), ("cs_py", 600), ("cs_pz", -2000)):
                poke(nm, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", (7282).to_bytes(2, "little"))
            poke("cs_pitch", b"\x00\x00")
            poke("cs_roll", b"\x00\x00")
            poke("cs_spd", (70 * 128).to_bytes(2, "little"))
            poke("cs_thr", (100).to_bytes(2, "little"))
            poke("cs_state", b"\x01")
            m.run()

        def hold(key, frames, step=15):
            """Hold a key and sample the two angles as it runs.

            THE PRESS IS CONFIRMED AND NOT WAITED FOR (docs/WRITING-TESTS.md
            7.1). A key is delivered to the emulator's queue and reaches the
            guest some ticks later; under load this row read a trainer that
            had never rolled at all - peak 0 of a 10,923 limit - which passes
            the limit check trivially and then fails the return-to-level one,
            pointing at a flight model that was never asked to do anything.
            [cs_kroll] and [cs_kpitch] are the guest's own answer."""
            out = []
            m.key(key, down=True, up=False)
            nm = "cs_kroll" if key in ("ArrowLeft", "ArrowRight") else "cs_kpitch"
            for _ in range(30):
                m.advance(frames=2)
                m.run()
                if byte(nm) != 0:
                    break
            else:
                sys.exit("skiespitts: the guest never saw %s ([%s] stayed 0)"
                         % (key, nm))
            for _ in range(frames // step):
                m.advance(frames=step)
                m.run()
                out.append((sw("cs_roll"), sw("cs_pitch")))
            m.key(key, down=False, up=True)
            return out

        result = {}
        for row, name in ((0, "c172"), (1, "pitts")):
            if byte("cs_back") != 0:            # out to the launcher, where the
                m.type_text("f")                # list is - the first time round
                m.advance(frames=60)            # we are already there
                m.run()
            pick(row)
            plane = w("cs_plane")
            check(plane == rec(mp["cs_planes"], 2 * row),
                  "%s: the list's row %d is in hand (%04x)" % (name, row, plane))
            m.type_text("f")
            m.advance(frames=80)
            m.run()
            check(byte("cs_back") != 0, "%s: the bracket took a mode" % name)
            maxroll = rec(plane, CSP_MAXROLL)
            airborne()
            rolls = hold("ArrowRight", 300)
            peak = max(abs(r) for r, _ in rolls)
            held = sw("cs_roll")
            m.advance(frames=90)                # ...and let go for five seconds
            m.run()
            after = sw("cs_roll")
            airborne()
            pitches = hold("ArrowDown", 240)
            top = max(p for _, p in pitches)
            result[name] = dict(plane=plane, maxroll=maxroll, peak=peak,
                                held=held, after=after, top=top,
                                rolls=[r for r, _ in rolls])
            print("      %s: MAXROLL %d, roll reached %d, released %d -> %d, "
                  "pitch topped %d (%d deg)"
                  % (name, maxroll, peak, held, after, top, round(top / DEG)))

        c, p = result["c172"], result["pitts"]
        # --- 2. the Pitts rolls past the limit and right round ---------------
        check(c["peak"] <= c["maxroll"] + 2,
              "the trainer stops at its roll limit (%d of %d)" % (c["peak"], c["maxroll"]))
        wrapped = any(x > 20000 for x in p["rolls"]) and any(x < -20000 for x in p["rolls"])
        check(wrapped, "the Pitts rolls RIGHT ROUND - past 110 degrees both ways "
                       "as the angle wraps (%d..%d)" % (min(p["rolls"]), max(p["rolls"])))
        # --- 3. no auto-level ------------------------------------------------
        check(abs(c["after"]) < abs(c["held"]),
              "the trainer returns toward level when the stick is centred "
              "(%d -> %d)" % (c["held"], c["after"]))
        check(p["after"] == p["held"],
              "the Pitts stays exactly where the stick left it (%d -> %d)"
              % (p["held"], p["after"]))
        # --- 4. over the top -------------------------------------------------
        check(c["top"] <= 8000,
              "the trainer's nose stops at its pitch limit (%d)" % c["top"])
        check(p["top"] > 16384,
              "the Pitts goes over the top - past 90 degrees of pitch (%d, %d deg)"
              % (p["top"], round(p["top"] / DEG)))
        m.type_text("f")
        m.advance(frames=40)
        m.run()

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""THE PANEL'S TWO HALVES (SPEC.md 88.9.4): sampled on the gate, painted per
PAGE - on MartyPC, on Mode X, which is the only backend with two pages.

    python3 tests/skiespanel.py [--machine os8088_xt_vga]

The gate is in TICKS and the page is the frame's PARITY, so a gate frame
lands on whichever page happens to be current. When the same read served
both, the other page kept whatever it last had - and the altimeter read
1,683 feet one frame and 1,666 the next, for ever, which is what the owner
saw. The reading is latched into one shared set now and each page's own
cache decides whether it still needs painting.

  1. in a steady climb the DISPLAYED sequence - the page each frame drew,
     read after it - never goes backwards, and it does not stand still
     either;
  2. the labels are drawn with the cockpit and not with the readings, so a
     panel that has never been repainted still says SPD/ALT/HDG/THR;
  3. and the STATE box says which state (88.9.4.1). cs_k_state packs the
     state, the stall and cs_onwater into one word so the box repaints when
     a hull leaves the water; cs_d_state tested the whole of the high byte
     for the stall, and an amphibian airborne OFF the water - which is how
     every A5 flight starts - read STALLED for the whole flight.

THE TWO PAGES' CACHES ARE NOT THE CHECK, and were tried as one first: in
both arms they sit about one gate apart, because the gate lands on
alternating pages either way. What differs is whether the page about to be
SHOWN carries the latest reading.

--clobber-share is the red run (docs/WRITING-TESTS.md 1): it sends the
between-gates path back to `.same`, which is the code exactly as it was, and
check 1 must go red. --clobber-state puts cs_d_state's stall test back to the
whole composite byte - `or ah, ah` for `test ah, 1` - and check 3 must go red
on the two rows where the hull is off the water.
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
CS_PI_ALT = 1
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-state", action="store_true",
                    help="cs_d_state tests the WHOLE composite byte for the"
                         " stall again: check 3 goes red")
    ap.add_argument("--clobber-share", action="store_true",
                    help="one read per page again: the row must go red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")

    def off(n):
        return dispapps.bss_off("skies", n)

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def byte(n):
            return m.readseg(seg, base + off(n), 1)[0]

        def poke(n, d):
            m.write(lin + base + off(n), d)

        def key(page, item):
            return int.from_bytes(m.read(lin + base + off("cs_pkeys")
                                         + page * 32 + item * 2, 2), "little")

        m.advance(frames=30)
        m.run()
        if a.clobber_state:
            # `test ah, 1` is F6 C4 01; `or ah, ah` is 08 E4 and a NOP, which
            # sets ZF off the WHOLE byte exactly as the defect did
            lo, hi = mp["cs_d_state"], mp["cs_d_bar"]
            code = m.read(lin + lo, hi - lo)
            i = code.find(b"\xF6\xC4\x01")
            if i < 0:
                sys.exit("skiespanel: cs_d_state does not hold 88.9.4.1's "
                         "stall test where this patch expects it")
            m.pause()
            m.write(lin + lo + i, b"\x08\xE4\x90")
            m.run()
            print("  (cs_d_state tests the whole composite byte again: this "
                  "run must fail)")
        if a.clobber_share:
            # `cmp byte [cs_pgate], 0` is 80 3E lo hi 00, and the `je` after
            # it is the one that now goes to .paint. .same is cs_pitem's last
            # byte, which is the byte before cs_pkey.
            lo, hi = mp["cs_pitem"], mp["cs_pkey"]
            code = m.read(lin + lo, hi - lo)
            gate = (base + off("cs_pgate")).to_bytes(2, "little")
            pat = b"\x80\x3E" + gate + b"\x00\x74"
            i = code.find(pat)
            if i < 0:
                sys.exit("skiespanel: cs_pitem does not test the gate the way "
                         "this patch expects - re-read it before trusting the "
                         "red run")
            site = lo + i + 5                   # the `74 dd`
            dest = hi - 1                       # .same, the closing ret
            d = dest - (site + 2)
            if not 0 <= d <= 127:
                sys.exit("skiespanel: .same is %d bytes away, out of a short "
                         "jump" % d)
            m.pause()
            m.write(lin + site + 1, bytes([d]))
            m.run()
            print("  (the between-gates path sent back to .same: must fail)")

        m.type_text("f")
        m.advance(frames=150)
        m.run()
        back = byte("cs_back")
        check(back == 1, "the bracket took MODE X, which is the two-page "
                         "backend (cs_back %d)" % back)

        # --- a steady climb ---------------------------------------------------
        m.pause()
        for nm, v in (("cs_px", -2400), ("cs_py", 500), ("cs_pz", -2000)):
            poke(nm, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
        poke("cs_pitch", (int(15 * 65536 / 360)).to_bytes(2, "little"))
        poke("cs_roll", b"\x00\x00")
        poke("cs_spd", (40 * 128).to_bytes(2, "little"))
        poke("cs_thr", (100).to_bytes(2, "little"))
        poke("cs_thrust", (16).to_bytes(2, "little"))
        poke("cs_thracc", (16 * 256).to_bytes(2, "little"))
        poke("cs_state", b"\x01")
        m.run()

        m.bp_exec(lin + mp["cs_render"])
        rows = []
        for _ in range(22):
            m.run()
            if m.wait_stop(30) is None:
                sys.exit("skiespanel: cs_render never ran")
            rows.append((byte("cs_ppage"), key(0, CS_PI_ALT), key(1, CS_PI_ALT)))
        m.bp_exec()
        m.run()
        rows = rows[8:]                         # ...past the first paint of
                                                # each page, which is 0
        # WHAT THE GLASS SHOWED, which is the only thing that settles this.
        # The two pages' caches are NOT the check and were tried as one: in
        # both arms they sit one gate apart, because the gate lands on
        # alternating pages either way. What differs is whether the page
        # about to be SHOWN carries the latest reading, and that is exactly
        # the sequence below.
        shown = [rows[i + 1][1 + rows[i][0]] for i in range(len(rows) - 1)]
        print("      displayed: %s" % shown)
        backs = [i for i in range(len(shown) - 1) if shown[i + 1] < shown[i]]
        check(not backs,
              "the altimeter never goes BACKWARDS in a climb (%d of %d frames "
              "did)" % (len(backs), len(shown) - 1))
        check(len(set(shown)) >= 3,
              "...and it is not simply frozen (%d distinct readings in %d "
              "frames)" % (len(set(shown)), len(shown)))

        # --- 3. the labels are the cockpit's ----------------------------------
        m.pause()
        _, _, fb = m.fbuf(0)
        m.run()
        fw = 640
        lit = sum(1 for y in range(300, 330) for x in range(28, 76)
                  if fb[(y * fw + x) * 3:(y * fw + x) * 3 + 3] != b"\0\0\0")
        print("      the SPD label's cells: %d lit pixels" % lit)
        check(lit > 40, "the labels are on the panel (%d lit)" % lit)

        # --- 3. THE STATE BOX SAYS WHICH STATE (SPEC.md 88.9.4.1) -----------
        #
        # cs_k_state packs THREE things into one word - the state in AL, and
        # the stall and cs_onwater into AH - so that the box repaints when a
        # hull leaves the water as well as when a stall starts. cs_d_state
        # then tested the WHOLE of AH for the stall, and cs_onwater sets bit
        # 1: an amphibian airborne off the water, which is how every A5
        # flight starts, read STALLED for the whole flight and nothing ever
        # cleared it. Every combination, driven by the three source bytes so
        # the key really changes and the painter really runs.
        #
        # SINCE 88.7.10.2 THE SAME BIT HAS AN AIR ARM. The airbrake is the
        # one state a pilot cannot see out of the window, and the strip said
        # FLYING with it out; a STALL still outranks it, because the brake is
        # a thing the pilot chose and the stall is one that happened.
        WANT = ((1, 0, 0, 0, "FLYING"), (1, 1, 0, 0, "STALL"),
                (1, 0, 1, 0, "FLYING"), (1, 1, 1, 0, "STALL"),
                (1, 0, 0, 1, "AIRBRAKE OUT"),   # the air arm (88.7.10.2)
                (1, 0, 1, 1, "AIRBRAKE OUT"),   # ...off the water too
                (1, 1, 0, 1, "STALL"),          # ...and a STALL outranks it
                (2, 0, 0, 1, "CRASHED"),        # ...as a crash outranks both
                (0, 0, 0, 0, "ON THE GROUND"), (0, 0, 1, 0, "ON THE WATER"),
                (0, 0, 0, 1, "BRAKES ON"),      # the latch (88.7.10.1), bit 2
                (0, 0, 1, 1, "BRAKES ON"))      # ...and it beats the water
        wrong, seen = [], 0
        for st, sl, wt, br, want in WANT:
            # A DIFFERENT STATE FIRST, so the key really changes: the panel
            # repaints an item only when its key moves (88.9.4), so a row
            # that happens to be what the aeroplane is already doing would
            # read as "the painter never ran" and it would be RIGHT to.
            m.pause()
            m.write(lin + base + off("cs_pause"), b"\x01")
            m.write(lin + base + off("cs_state"), b"\x02")     # CRASHED
            m.write(lin + base + off("cs_kbrake"), b"\x00")
            m.run()
            # CONFIRMED AND NOT COUNTED: the panel repaints an item at a GATE
            # (every CS_PRATE ticks), and eight card frames is a fifth of a
            # tick on Mode X - so under load the crash was never painted, and
            # the row below then reported "the key did not change", which was
            # perfectly true and told nobody anything at all
            m.bp_exec(lin + mp["cs_d_state"])
            m.run()
            if m.wait_stop(30) is None:
                sys.exit("skiespanel: cs_d_state never ran for the crash")
            m.bp_exec()
            m.run()
            m.pause()
            for nm, v in (("cs_state", st), ("cs_stall", sl),
                          ("cs_onwater", wt), ("cs_kbrake", br),
                          ("cs_pause", 1)):
                m.write(lin + base + off(nm), bytes([v]))
            m.run()
            m.bp_exec(lin + mp["cs_d_state"])
            m.run()
            if m.wait_stop(30) is None:
                wrong.append("state %d stall %d water %d brake %d: the "
                             "painter never ran - the key did not change"
                             % (st, sl, wt, br))
                m.bp_exec()
                m.run()
                continue
            m.bp_exec(lin + mp["cs_pcell"])       # SI is the string it chose
            m.run()
            m.wait_stop(30)
            si = m.regs()["si"] & 0xFFFF
            got = m.readseg(seg, si, 20).split(b"\0")[0].decode(
                "ascii", "replace").strip()
            m.bp_exec()
            m.run()
            seen += 1
            if got != want:
                wrong.append("state %d stall %d water %d brake %d: %r, "
                             "wanted %r" % (st, sl, wt, br, got, want))
        check(seen == len(WANT) and not wrong,
              "the state box says which state, over %d combinations%s"
              % (seen, "" if not wrong else " - " + "; ".join(wrong[:3])))
        m.pause()
        m.write(lin + base + off("cs_onwater"), b"\x00")
        m.write(lin + base + off("cs_pause"), b"\x00")
        m.run()
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

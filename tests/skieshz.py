#!/usr/bin/env python3
"""THE HORIZON REACHES THE GLASS (SPEC.md 88.3.3.1), on MartyPC.

    python3 tests/skieshz.py [--machine os8088_5150_herc_gla]

The field reported a horizon that would not turn - *"it just stays stable;
if I change my bank angle it will draw only at SOME of the bank angles"* -
and wedges of ground that came and went with the bank.

cs_skyground was right the whole time: pinned at 45 degrees its cs_xl reads
a correct diagonal and at 90 a vertical line where the arithmetic puts it.
The band's rows were drawn into the SHADOW and never carried to the glass,
because the band loop wrote each split row's span and never widened the
span set's ROW RANGE - and 88.3.3's range is the only thing cs_blit walks.

So the row checks the GLASS, not the arithmetic, against the guest's own
[cs_nx]/[cs_ny]/[cs_nz]: sky is where the ray r = (X/sclx, -Y/scly, 1) has
r.n > 0 (88.4.1), and everything else is the fill being wrong.

Two things make it a real test rather than a plausible one. It PINS the
attitude, which is what makes the failure reachable at all - a pinned world
is one where no row changes kind and no object marks anything, so nothing
else widens the range. And it compares over GROUPS OF FOUR ROWS, because
the Hercules ground is 0x88 00 22 00 and two of its four phases are blank:
a per-row test calls half the ground sky and passes on a broken build.

AND THE SAME THING HAPPENED AGAIN ONE MODE ALONG (SPEC.md 88.13.3.1), so
there is a second check. With the ground fill OFF the two sides are both
black on a 1bpp adapter and what tells them apart is ONE SEGMENT - and the
field reported it *"disappearing at some angles, some of the time"*.
`cs_skyground` runs BEFORE `cs_scene`, so all three of the words `cs_seg`
reads about its caller are the PREVIOUS frame's last object's: `cs_pinview`
skips the clip, `cs_pwhole` skips the marking, and `cs_markacc` accumulates
into an object box `cs_drawobj` resets a moment later. The segment was drawn
into the shadow and never carried, exactly as the band's rows were.

Check 2 is the GLASS again, and it is an A/B on the segment's own draw: the
`je` that gates it is turned into a `jmp`, and the difference between the
two views is precisely the horizon's blitted pixels. What it is held to is
the host's own Liang-Barsky clip of the same segment against the same view,
so a pose where the horizon genuinely does not cross the view expects
nothing and a pose where it does expects a length.

--clobber-range is the red run for check 1 (docs/WRITING-TESTS.md 1): it
NOPs the two stores that widen the range and nothing else, which is the code
as it was. --clobber-hzmark is check 2's, and puts back all four stores'
worth of leftover.
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
DEG = 65536.0 / 360.0
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def sg(v):
    return v - 0x10000 if v >= 0x8000 else v


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--step", type=int, default=30)
    ap.add_argument("--clobber-range", action="store_true",
                    help="do not widen the range over the band: rows go red")
    ap.add_argument("--clobber-hzmark", action="store_true",
                    help="the horizon's segment back on the leftovers: check 2 goes red")
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

        def sw(n):
            return sg(int.from_bytes(m.readseg(seg, base + off(n), 2), "little"))

        def poke(n, d):
            m.write(lin + base + off(n), d)

        def rec(at, o):
            return int.from_bytes(m.readseg(seg, at + o, 2), "little")

        m.advance(frames=30)
        m.run()
        if a.clobber_range:
            # the two stores that widen it, and only those - the same
            # encodings appear in cs_hzrows, which is past cs_hzkeep
            lo, hi = mp["cs_skyground"], mp["cs_hzkeep"]
            code = m.read(lin + lo, hi - lo)
            n = 0
            for pat in (b"\x89\x5d\xfc", b"\x89\x45\xfe"):
                i = code.find(pat)
                if i < 0:
                    sys.exit("skieshz: cs_skyground does not widen the range "
                             "the way this patch expects")
                m.pause()
                m.write(lin + lo + i, b"\x90\x90\x90")
                m.run()
                n += 1
            print("  (the band no longer widens the row range: must fail)")

        po = [rec(mp["cs_drplane"], 2 * i) for i in range(4)]
        ui.mo.click((po[0] + po[2]) // 2, (po[1] + po[3]) // 2)
        m.advance(frames=20)
        m.run()
        top = rec(mp["cs_drplane"], 22)
        ui.mo.click(po[0] + 20, top + 1 + 12 * 2 + 6)       # the Magister
        m.advance(frames=20)
        m.run()
        m.type_text("f")
        m.advance(frames=80)
        m.run()
        poke("cs_setbld", b"\x00")      # nothing built: the view is the fill
        m.advance(frames=20)
        m.run()
        vx, vy = sw("cs_vx"), sw("cs_vy")
        wx0, ww, wh = sw("cs_wx0"), sw("cs_ww"), sw("cs_wh")
        vcx, vcy = sw("cs_vcx"), sw("cs_vcy")
        sclx, scly = sw("cs_sclx"), sw("cs_scly")
        check(wh > 0 and ww > 0 and sclx > 0 and scly > 0,
              "the view is up: %dx%d at %d,%d, scale %d/%d"
              % (ww, wh, vx + wx0, vy, sclx, scly))

        def pic():
            fb = m.read(0xB0000, 4 * 0x2000)
            rows = []
            for y in range(vy, vy + wh):
                o = (y & 3) * 0x2000 + (y >> 2) * 90
                rows.append([(fb[o + (x >> 3)] >> (7 - (x & 7))) & 1
                             for x in range(720)])
            return rows

        def pinp(roll, pitch):
            """Check 2's OWN pose, and not check 1's.

            The attitude is what decides the horizon's geometry, but the
            POSITION decides which objects are in the scene - and it is the
            previous frame's last object whose cs_pinview/cs_pwhole the
            segment inherits (88.13.3.1). Over check 1's corner of the world
            the defect does not reproduce at any bank angle; over the city it
            does, at 8 of 180 poses. A red run that stays green is a check
            that tests nothing (docs/WRITING-TESTS.md 1)."""
            pin(roll)
            for nm, v in (("cs_px", 0), ("cs_py", 400), ("cs_pz", -1200)):
                poke(nm, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", b"\x00\x00")
            poke("cs_pitch", (int(pitch * DEG) & 0xFFFF).to_bytes(2, "little"))

        def pin(roll):
            for nm, v in (("cs_px", -22000), ("cs_py", 538), ("cs_pz", -22000)):
                poke(nm, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", (7282).to_bytes(2, "little"))
            poke("cs_pitch", (int(-3 * DEG) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_roll", (int(roll * DEG) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_rrate", b"\x00\x00")
            poke("cs_prate", b"\x00\x00")
            poke("cs_spd", (120 * 128).to_bytes(2, "little"))
            poke("cs_state", b"\x01")

        m.bp_exec(lin + mp["cs_render"])
        m.run()
        if m.wait_stop(30) is None:
            sys.exit("skieshz: cs_render never ran")
        worst, wrongs = (0, None), []
        for deg in range(0, 360, a.step):
            for _ in range(4):
                pin(deg)
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skieshz: cs_render never ran")
            nx, ny, nz = sw("cs_nx"), sw("cs_ny"), sw("cs_nz")
            rows = pic()
            wrong, sample = 0, None
            for g in range(0, wh - 3, 4):
                for x in range(wx0, wx0 + ww, 8):
                    vs = [(x + 3 - vcx) * nx / sclx - (r - vcy) * ny / scly + nz
                          for r in range(g, g + 4)]
                    if min(vs) < 120 and max(vs) > -120:
                        continue                # the group straddles the line
                    sky = vs[0] > 0
                    inked = any(rows[r][vx + x + k] for r in range(g, g + 4)
                                for k in range(8))
                    if sky != (not inked):
                        wrong += 1
                        if sample is None:
                            sample = (g, x, "SKY but inked" if sky
                                      else "GROUND but blank")
            if wrong > worst[0]:
                worst = (wrong, deg)
            if wrong > 4:
                wrongs.append((deg, wrong, sample))
        m.bp_exec()
        m.run()
        for deg, n, s in wrongs:
            print("      roll %3d: %d byte-groups wrong, first %s" % (deg, n, s))
        check(not wrongs,
              "the fill matches the guest's own normal at every bank angle "
              "(worst %d byte-groups at roll %s)" % worst)

        # === 2: the WIRE horizon's one segment reaches the glass (88.13.3.1)
        poke("cs_setfill", b"\x00")     # ground and buildings as outlines: on
        poke("cs_setbld", b"\x04")      # 1bpp both sides are black and this
        poke("cs_setlod", b"\x03")      # segment is the only thing between.
        poke("cs_pause", b"\x01")       # HIGH/ULTRA on purpose: the words the
        m.advance(frames=20)            # segment inherits are an OBJECT's.
        m.run()                         # AND THE WORLD STOPS, because check 2
                                        # is a difference between two captures
                                        # and an aeroplane at 120 knots moves
                                        # the whole scene between them - which
                                        # reads as the horizon having drawn
                                        # hundreds of pixels, whatever it did
        lo = mp["cs_skyground"]
        # 0x800 AND NOT 0x400: cs_skyground grew past a kilobyte when the band
        # got a span pass and a fused row (88.3.1.1, 88.3.1.3), and a window
        # that ends before the gate reads as "the code moved" rather than as
        # the window being short. `cmp byte [cs_hzhave], 0` occurs once in the
        # package, so a generous window cannot match the wrong site.
        code = m.read(lin + lo, 0x800)
        gate = (b"\x80\x3E" + (base + off("cs_hzhave")).to_bytes(2, "little")
                + b"\x00\x74")
        g = code.find(gate)
        if g < 0:
            sys.exit("skieshz: cs_skyground does not gate its segment where "
                     "this expects - re-read it before trusting check 2")
        jeat = lin + lo + g + len(gate) - 1

        if a.clobber_hzmark:
            # the three stores before the call and the one after: 88.13.3.1's
            # whole change, back to the leftovers it was written for
            n = 0
            for nm, val in (("cs_pinview", 0), ("cs_pwhole", 0),
                            ("cs_ownmk", 0xFF), ("cs_ownmk", 0)):
                pat = (b"\xC6\x06" + (base + off(nm)).to_bytes(2, "little")
                       + bytes([val]))
                k = code.find(pat, g)
                if k < 0:
                    continue
                m.write(lin + lo + k, b"\x90" * 5)
                n += 1
            if n != 4:
                sys.exit("skieshz: found %d of 88.13.3.1's 4 stores - re-read "
                         "cs_skyground before trusting the red run" % n)
            print("  (the horizon back on the previous object's words: must fail)")

        def clip(x1, y1, x2, y2, xa, ya, xb, yb):
            """Liang-Barsky, on the host: what of the segment is in the view."""
            dx, dy = x2 - x1, y2 - y1
            t0, t1 = 0.0, 1.0
            for pp, qq in ((-dx, x1 - xa), (dx, xb - x1),
                           (-dy, y1 - ya), (dy, yb - y1)):
                if pp == 0:
                    if qq < 0:
                        return None
                    continue
                r = qq / pp
                if pp < 0:
                    if r > t1:
                        return None
                    t0 = max(t0, r)
                else:
                    if r < t0:
                        return None
                    t1 = min(t1, r)
            return (x1 + t0 * dx, y1 + t0 * dy, x1 + t1 * dx, y1 + t1 * dy)

        wb0, wbn, wx1 = sw("cs_wb0"), sw("cs_wbn"), sw("cs_wx1")
        box0 = vx // 8

        def viewpx():
            fb = m.read(0xB0000, 4 * 0x2000)
            out = bytearray()
            for y in range(vy, vy + wh):
                o = (y & 3) * 0x2000 + (y >> 2) * 90 + box0
                out += fb[o + wb0:o + wb0 + wbn]
            return bytes(out)

        m.bp_exec(lin + mp["cs_render"])
        m.run()
        if m.wait_stop(30) is None:
            sys.exit("skieshz: cs_render never ran")

        def frames(n=3):
            for _ in range(n):
                m.run()
                if m.wait_stop(60) is None:
                    sys.exit("skieshz: cs_render never ran")

        short = []
        for pitch in (-12, 0, 12):
            for deg in range(0, 360, a.step):
                pinp(deg, pitch)
                frames(4)
                m.write(jeat, b"\x74")          # as it ships
                frames(3)
                on = viewpx()
                hz = [sg(int.from_bytes(
                    m.read(lin + base + off("cs_hzsa") + 2 * k, 2), "little"))
                    for k in range(4)]
                have = m.readseg(seg, base + off("cs_hzhave"), 1)[0]
                m.write(jeat, b"\xEB")          # ...and never drawn
                frames(3)
                dark = viewpx()
                m.write(jeat, b"\x74")
                drew = sum(bin(p ^ q).count("1") for p, q in zip(on, dark))
                c = clip(hz[0], hz[1], hz[2], hz[3],
                         wx0, 0, wx1, wh - 1) if have else None
                want = 0 if c is None else int(max(abs(c[2] - c[0]),
                                                   abs(c[3] - c[1]))) + 1
                if want >= 8 and drew < want // 2:
                    short.append((pitch, deg, drew, want))
        m.bp_exec()
        m.run()
        for pitch, deg, drew, want in short:
            print("      pitch %4d roll %3d: %d pixels of about %d"
                  % (pitch, deg, drew, want))
        check(not short,
              "the wire horizon reaches the glass at every attitude that "
              "crosses the view (%d of %d poses short)"
              % (len(short), 3 * (360 // a.step)))

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

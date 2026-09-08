#!/usr/bin/env python3
"""THE BOX LOD'S GATE, PAST SIX KILOMETRES (SPEC.md 88.5.4.2): a solid too
small to tell apart is one filled rectangle, at EVERY range and not only the
near half of the world.

    python3 tests/skieslod.py [--machine os8088_5150_herc_gla]

cs_drawobj decides between cs_boxlod and the whole model by comparing the
model's projected size against 11 cz, and it built that product in CX - a
WORD, which 11 cz stops fitting at 5,958 m. Past there it wrapped, the
comparison fell the wrong way and every solid in the band drew all of its
vertices and all of its faces to cover four pixels. Nothing shipped stood in
the band, so nothing was slow and nothing looked wrong; what it cost was the
next building anybody added, at exactly the distance a JFK take-off starts
from (6,845 m to the Empire State).

Four of NYC-JFK's anonymous towers are moved nowhere - the CAMERA is put
7 km back down the runway heading and their ranges raised so the cull keeps
them - and then, over one frame:

  1. every one of them takes cs_boxlod and none takes cs_stackverts, which
     is the gate falling the right way;
  2b. ...and at DRAW DISTANCE = ULTRA the same four at the same place take
     the POLYGONS instead - cs_boxlod not entered at all and nothing
     reaching cs_rect (88.13.2.1), which is that rung's whole feature;
  2. the box reaches the glass - cs_rect once per tower - so a gate that
     merely CALLED cs_boxlod and had it refuse would not pass this;
  3. and the frame is shorter for it: the marginal cost of the four is
     under 7 ms a tower, against the ~11.9 the full path costs them there;
  4. and THE IMPOSTOR IS THE SIZE OF THE MODEL IT STANDS IN FOR. cs_boxlod
     works in whole metres and says so in cs_pshr, and it can REFUSE - and
     the caller then takes the full path, where cs_nearat, cs_stackverts and
     cs_flatverts all read the object's scale out of that byte. A refusal
     that left 0 behind drew the model at a fraction of its size, over
     exactly the part of the approach where the rectangle crosses CS_LODPX:
     a building flickering between two sizes as the aeroplane taxied, which
     is how it was reported off a 10 MHz 8086;
  5. and NOTHING ON THE SKYLINE GOES AWAY AND COMES BACK down the take-off
     run - asked of the GUEST and not of the pixels: cs_boxlod and
     cs_stackverts are the two paths a solid can take, so a frame is a map
     of object -> box, polygons or absent, and a taxi straight at the city
     is monotone. A state that returns after another is the defect exactly.

Check 3 is deliberately a BOUND and not a ratchet: it has to separate two
regimes (4.2 ms a tower boxed, 11.9 unboxed on a 4.77 MHz 8088) and not
pin whatever this month's rectangle costs.

--clobber-lod is the red run (docs/WRITING-TESTS.md 1): it NOPs the two
instructions that refuse the multiply, which restores the wrapping product
exactly, and the first three checks must go red. --clobber-ultra NOPs the
`je` that 88.13.2.1 hangs on, so Ultra boxes what Far boxes, and check 2b
must go red.

TWO OF THE RED RUNS HERE NO LONGER GO RED, and neither is a check that
stopped working - both are checks whose defect stopped existing:

  --clobber-shr NOPs the pairs inside cs_boxlod that give cs_pshr back, and
  check 4 gets its model arm by putting `stc / ret` ON cs_boxlod - a routine
  replaced by two bytes never reaches the instructions the clobber patched.
  The check itself is sound and caught the defect when it was live; the
  clobber and the check do not meet. Asking the BYTE instead needs a range
  where cs_boxlod is CALLED and then refuses, and that window is a few
  hundred metres wide, different per model and not reachable at all for
  these four at this eye height - it was tried and withdrawn rather than
  left in place reading nothing.

  --clobber-tall puts the impostor's height bound back to CS_LODPX, which
  returns a two-pixel-wide tower to the polygon path - where its winding
  USED to be decided by rounding. SPEC.md 88.5.10 fixed that: the winding is
  the whole polygon's area now, so the same tower on the polygon path is
  stable and check 5 has nothing to see. The bound is about COST now and no
  longer about flicker.
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPS = 4772727.0
CSO_X, CSO_RANGE, CSO_SKIP, CSO_SIZE = 4, 10, 18, 20
CSA_OBJS, CSA_NOBJ = 18, 20
CSL_MOD, CSL_ULTRA = 1, 3       # the Draw Distance rungs this row names
DIST = 8000                     # ...down the runway heading, in the band
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def main(argv):
    import math
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-lod", action="store_true",
                    help="the wrapping multiply back: the row must go red")
    ap.add_argument("--clobber-shr", action="store_true",
                    help="cs_boxlod keeps the scale it clobbered: check 4 red")
    ap.add_argument("--clobber-ultra", action="store_true",
                    help="Ultra stops turning the impostor off: check 2b red")
    ap.add_argument("--clobber-tall", action="store_true",
                    help="the tall bound back to CS_LODPX: check 5 goes red")
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

        def word(n):
            return int.from_bytes(m.read(lin + base + off(n), 2), "little")

        def byte(n):
            return m.readseg(seg, base + off(n), 1)[0]

        def poke(n, d):
            m.write(lin + base + off(n), d)

        m.advance(frames=40)
        m.run()

        # --- NYC-JFK, by name out of the guest's own table -----------------
        nport = int.from_bytes(m.readseg(seg, mp["cs_drport"] + 10, 2), "little")
        port = None
        for i in range(nport):
            p = int.from_bytes(m.readseg(seg, mp["cs_ports"] + 2 * i, 2), "little")
            q = int.from_bytes(m.readseg(seg, p, 2), "little")
            nm = ""
            while True:
                c = m.readseg(seg, q + len(nm), 1)[0]
                if not c:
                    break
                nm += chr(c)
            if "JFK" in nm:
                port = p
        if port is None:
            sys.exit("skieslod: no JFK in cs_ports")
        m.pause()
        poke("cs_airport", port.to_bytes(2, "little"))
        poke("cs_inited", b"\x00")
        m.run()
        m.type_text("f")
        m.advance(frames=140)
        m.run()

        if a.clobber_lod:
            # `cmp cx, 5958` is 81 F9 46 17 and the `jae` after it 73 dd:
            # six bytes of NOP puts the wrapping multiply back exactly.
            lo, hi = mp["cs_drawobj"], mp["cs_sizepx"]
            code = m.read(lin + lo, hi - lo)
            pat = b"\x81\xF9" + (5958).to_bytes(2, "little") + b"\x73"
            i = code.find(pat)
            if i < 0:
                sys.exit("skieslod: cs_drawobj does not refuse the multiply "
                         "the way this patch expects - re-read it before "
                         "trusting the red run")
            m.pause()
            m.write(lin + lo + i, b"\x90" * 6)
            m.run()
            print("  (the wrapping 11 cz put back: must fail)")

        if a.clobber_shr:
            # The `mov al,[cs_bshr] / mov [cs_pshr],al` pairs that give the
            # object's transform scale back. Without them a REFUSED impostor
            # leaves the full path running in whole metres.
            lo, hi = mp["cs_boxlod"], mp["cs_stackverts"]
            code = m.read(lin + lo, hi - lo)
            pat = (b"\xA0" + (base + off("cs_bshr")).to_bytes(2, "little")
                   + b"\xA2" + (base + off("cs_pshr")).to_bytes(2, "little"))
            n, i = 0, code.find(pat)
            while i >= 0:
                m.pause()
                m.write(lin + lo + i, b"\x90" * 6)
                m.run()
                n += 1
                i = code.find(pat, i + 6)
            if not n:
                sys.exit("skieslod: cs_boxlod does not restore cs_pshr the "
                         "way this patch expects - re-read it before "
                         "trusting the red run")
            print("  (cs_boxlod keeps the scale it clobbered, %d site(s): "
                  "must fail)" % n)

        if a.clobber_ultra:
            # the `je .stack` that 88.13.2.1 hangs on, NOPped: Ultra then
            # behaves exactly like Far and boxes the same solids
            lo, hi = mp["cs_drawobj"], mp["cs_boxlod"]
            code = m.read(lin + lo, hi - lo)
            i = code.find(b"\x80\x3E"
                          + (base + off("cs_setlod")).to_bytes(2, "little")
                          + b"\x03\x74")
            if i < 0:
                sys.exit("skieslod: cs_drawobj does not hold 88.13.2.1's "
                         "Ultra test where this patch expects it")
            m.pause()
            m.write(lin + lo + i + 5, b"\x90\x90")
            m.run()
            print("  (Ultra no longer turns the impostor off: this run must "
                  "fail)")
        if a.clobber_tall:
            # `cmp si, CS_LODTALL` is 83 FE 1A: put the bound back to
            # CS_LODPX and a two-pixel-wide tower returns to the polygon
            # path, where its winding is decided by rounding.
            lo, hi = mp["cs_boxlod"], mp["cs_stackverts"]
            code = m.read(lin + lo, hi - lo)
            i = code.find(b"\x83\xFE\x1A")
            if i < 0:
                sys.exit("skieslod: the CS_LODTALL compare is not where this "
                         "patch expects - re-read it before trusting the red "
                         "run")
            m.pause()
            m.write(lin + lo + i + 2, bytes([8]))
            m.run()
            print("  (the tall bound put back to CS_LODPX: must fail)")

        objs = int.from_bytes(m.read(lin + port + CSA_OBJS, 2), "little")
        nobj = int.from_bytes(m.read(lin + port + CSA_NOBJ, 2), "little")
        # Every anonymous BOX in the table, whatever rung it belongs to -
        # most of them are CSO_DENSE since 88.13.1.2 and the count is not a
        # constant this row should carry.
        #
        # FOUR OF THEM AND NO MORE, and that cap is load-bearing. place()
        # lays them in a row 200 m apart across the sight line, so the number
        # found decides how wide that row is: when 88.13.1.3 took Manhattan's
        # anonymous boxes from ten to sixteen the row became 3 km wide, the
        # outer towers went off-axis, and BOTH of this file's own red runs
        # went green - --clobber-tall's dip landing at 2.6% against a 3%
        # bound. The checks below are tuned to four towers on the sight line
        # and the world may grow as many more as it likes.
        want = tuple(mp[n] for n in ("cs_m_jfk_mid", "cs_m_jfk_dtn",
                                     "cs_m_jfk_hi", "cs_m_jfk_lo"))
        rng0 = [int.from_bytes(m.read(lin + objs + i * CSO_SIZE + CSO_RANGE, 2),
                               "little") for i in range(nobj)]
        towers = [i for i in range(nobj)
                  if int.from_bytes(m.read(lin + objs + i * CSO_SIZE, 2),
                                    "little") in want][:4]
        check(len(towers) == 4, "NYC-JFK's anonymous towers found in its "
                                "object table (%d)" % len(towers))

        h = math.radians(310)                   # the runway's own heading

        def place(on):
            """The camera DIST back down the heading, the towers ON it.

            Where they stand in the shipped world is not the question here -
            two of the four are off to the side from this camera and the
            frustum drops them, which would make the counts below a fact
            about JFK's layout rather than about the gate. They are put in a
            row across the sight line instead, all at DIST.
            """
            m.pause()
            for k, i in enumerate(towers):
                lat = (k - (len(towers) - 1) / 2.0) * 200.0
                x = int(round(lat * math.cos(h)))
                z = int(round(-lat * math.sin(h)))
                m.write(lin + objs + i * CSO_SIZE + CSO_X,
                        ((x & 0xFFFF).to_bytes(2, "little")
                         + (z & 0xFFFF).to_bytes(2, "little")))
            for nm, v in (("cs_px", int(round(-DIST * math.sin(h)))),
                          ("cs_py", 200),
                          ("cs_pz", int(round(-DIST * math.cos(h))))):
                poke(nm, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", (int(310 * 65536 / 360) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_pitch", b"\x00\x00")
            poke("cs_roll", b"\x00\x00")
            poke("cs_state", b"\x01")
            poke("cs_pause", b"\x01")
            poke("cs_setbld", b"\x04")     # HIGH: six of these are CSO_DENSE
            for i in range(nobj):
                r = 15900 if (i in towers and on) else 0
                m.write(lin + objs + i * CSO_SIZE + CSO_RANGE,
                        r.to_bytes(2, "little"))
                m.write(lin + objs + i * CSO_SIZE + CSO_SKIP, b"\x00\x00")
            m.run()

        stages = ("cs_render", "cs_boxlod", "cs_stackverts", "cs_rect")
        byoff = {mp[s]: s for s in stages}

        def one_frame():
            """Every stage entered between one cs_render and the next."""
            m.bp_exec(*[lin + mp[s] for s in stages])
            m.run()
            if m.wait_stop(60) is None:
                sys.exit("skieslod: nothing stopped")
            while byoff.get(m.regs()["ip"]) != "cs_render":
                m.run()
                if m.wait_stop(60) is None:
                    sys.exit("skieslod: cs_render never ran")
            hits = {}
            for _ in range(400):
                m.run()
                if m.wait_stop(60) is None:
                    break
                nm = byoff.get(m.regs()["ip"])
                if nm == "cs_render":
                    break
                hits[nm] = hits.get(nm, 0) + 1
            m.bp_exec()
            m.run()
            return hits

        def frame_ms(n=9):
            m.bp_exec(lin + mp["cs_render"])
            m.run()
            m.wait_stop(60)
            c0 = m.status()["cycles"]
            out = []
            for _ in range(n):
                m.run()
                if m.wait_stop(60) is None:
                    sys.exit("skieslod: no frame")
                c1 = m.status()["cycles"]
                out.append((c1 - c0) / CPS * 1000.0)
                c0 = c1
            m.bp_exec()
            m.run()
            return sorted(out)[len(out) // 2]

        # --- 1 and 2: which path the four take, over one frame -------------
        place(True)
        m.advance(frames=6)
        m.run()
        hits = one_frame()
        nbox = hits.get("cs_boxlod", 0)
        nstk = hits.get("cs_stackverts", 0)
        nrect = hits.get("cs_rect", 0)
        check(nbox == len(towers) and nstk == 0,
              "at %d m every one of the %d takes the box: cs_boxlod %d, "
              "cs_stackverts %d (want %d and 0)"
              % (DIST, len(towers), nbox, nstk, len(towers)))
        check(nrect >= len(towers),
              "and the box REACHES THE GLASS: cs_rect %d (want %d or more, "
              "so a cs_boxlod that refused would not pass)"
              % (nrect, len(towers)))

        # --- 2b: ...and DRAW DISTANCE = ULTRA turns the whole thing off ----
        #
        # 88.13.2.1: the impostor is the one rung on that ladder that is a
        # SUBSTITUTION rather than a distance, so the rung above Far takes
        # the substitution away and every solid draws its polygons at every
        # size. The SAME towers at the SAME place, which is what makes this
        # about the rung and not about a layout: cs_boxlod is not entered at
        # all, cs_stackverts is entered once per tower, and nothing reaches
        # cs_rect - that routine having exactly one caller.
        m.pause()
        poke("cs_setlod", bytes([CSL_ULTRA]))
        for i in range(nobj):
            m.write(lin + objs + i * CSO_SIZE + CSO_SKIP, b"\x00\x00")
        m.run()
        m.advance(frames=6)
        m.run()
        uh = one_frame()
        check(uh.get("cs_boxlod", 0) == 0 and uh.get("cs_rect", 0) == 0
              and uh.get("cs_stackverts", 0) >= len(towers),
              "at Ultra the same %d take the POLYGONS: cs_boxlod %d, cs_rect "
              "%d, cs_stackverts %d (want 0, 0 and %d or more)"
              % (len(towers), uh.get("cs_boxlod", 0), uh.get("cs_rect", 0),
                 uh.get("cs_stackverts", 0), len(towers)))
        m.pause()
        poke("cs_setlod", bytes([CSL_MOD]))
        for i in range(nobj):
            m.write(lin + objs + i * CSO_SIZE + CSO_SKIP, b"\x00\x00")
        m.run()
        m.advance(frames=6)
        m.run()

        # --- 3: and the frame is shorter for it ----------------------------
        on = frame_ms()
        place(False)
        m.advance(frames=6)
        m.run()
        m.bp_exec()
        m.run()
        gone = frame_ms()
        per = (on - gone) / len(towers)
        check(per < 7.0,
              "the %d cost %.2f ms a tower (%.1f ms against %.1f), under "
              "the 7 ms that separates the box from the full path"
              % (len(towers), per, on, gone))

        # --- 4: THE IMPOSTOR IS THE SIZE OF THE MODEL IT STANDS IN FOR ---
        VW8 = word("cs_wbn")

        def viewpx():
            m.pause()
            fb = m.read(0xB0000, 0x8000)
            m.run()
            vy, wh = word("cs_vy"), word("cs_wh")
            wb0, wbn = word("cs_wb0"), word("cs_wbn")
            b0 = word("cs_vx") // 8
            out = bytearray()
            for y in range(vy, vy + wh):
                o = (y & 3) * 0x2000 + (y >> 2) * 90 + b0
                out += fb[o + wb0:o + wb0 + wbn]
            return bytes(out)

        keep = m.read(lin + mp["cs_boxlod"], 2)

        def stand(on, ox, oz, dist, box):
            m.pause()
            for i in range(nobj):
                o = objs + i * CSO_SIZE
                m.write(lin + o + CSO_RANGE,
                        (15900 if i == on else 0).to_bytes(2, "little"))
                m.write(lin + o + CSO_SKIP, b"\x00\x00")
            for n, v in (("cs_px", ox), ("cs_py", 60), ("cs_pz", oz - dist)):
                poke(n, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            for n in ("cs_hdg", "cs_pitch", "cs_roll"):
                poke(n, b"\x00\x00")
            poke("cs_state", b"\x01")
            poke("cs_pause", b"\x01")
            poke("cs_setbld", b"\x04")
            poke("cs_setlod", b"\x01")
            poke("cs_setfill", b"\x03")
            # `stc / ret` makes every impostor refuse: the whole model
            m.write(lin + mp["cs_boxlod"], keep if box else b"\xF9\xC3")
            m.run()
            m.advance(frames=14)
            return viewpx()

        def extent(p_, q_):
            xs, ys = [], []
            for j in range(len(p_)):
                d = p_[j] ^ q_[j]
                if d:
                    for k in range(8):
                        if d >> k & 1:
                            xs.append((j % VW8) * 8 + k)
                            ys.append(j // VW8)
            if not xs:
                return None
            return (max(xs) - min(xs) + 1, max(ys) - min(ys) + 1)

        wrong = []
        for i in towers[:4]:
            ox = int.from_bytes(m.read(lin + objs + i * CSO_SIZE + 4, 2),
                                "little")
            oz = int.from_bytes(m.read(lin + objs + i * CSO_SIZE + 6, 2),
                                "little")
            ox = ox - 65536 if ox > 32767 else ox
            oz = oz - 65536 if oz > 32767 else oz
            for dist in (3000, 4500, 7000):
                empty = stand(-1, ox, oz, dist, 1)
                b = extent(stand(i, ox, oz, dist, 1), empty)
                w_ = extent(stand(i, ox, oz, dist, 0), empty)
                if b != w_:
                    wrong.append("obj %d at %d m: impostor %s, model %s"
                                 % (i, dist, b, w_))
        m.pause()
        m.write(lin + mp["cs_boxlod"], keep)
        m.run()
        check(not wrong,
              "the impostor is the size of the model it stands in for, over "
              "four towers at three ranges%s"
              % ("" if not wrong else " - " + "; ".join(wrong[:3])))

        # --- 5: AND THE SKYLINE DOES NOT FLICKER (88.5.4.4) ---------------
        #
        # THE WHOLE WORLD BACK FIRST. Check 4 stands one object at a time and
        # puts every other range to 0; without this the sweep below reads a
        # sky with one building in it and passes flat.
        m.pause()
        for i in range(nobj):
            m.write(lin + objs + i * CSO_SIZE + CSO_RANGE,
                    rng0[i].to_bytes(2, "little"))
        m.run()
        #
        # The mass of lit pixels above the horizon, stepping the eye four
        # metres at a time down the take-off run. A building that vanishes
        # for a few metres and comes back takes a bite out of it and puts it
        # straight back, which is what the field saw: with the tall bound at
        # CS_LODPX the run reads 181, 141, 187 and the largest single step is
        # a quarter of the whole skyline.
        rax = int.from_bytes(m.read(lin + port + 2, 2), "little")
        raz = int.from_bytes(m.read(lin + port + 4, 2), "little")
        rax = rax - 65536 if rax > 32767 else rax
        raz = raz - 65536 if raz > 32767 else raz
        rh = int.from_bytes(m.read(lin + port + 8, 2), "little")
        hh = rh * 2 * math.pi / 65536.0
        # WHAT EACH OBJECT WAS DRAWN AS, and not how many pixels were lit.
        #
        # This was a LIT-PIXEL sweep - the mass above the horizon, and a
        # value below both its neighbours meant something had gone away for
        # four metres and come back. That reads a dithered picture, and when
        # 88.13.1.3 put twelve more towers on this skyline the ordinary
        # phase shift of a 50% dither behind a moving eye became 2.7% of it:
        # the clean run and --clobber-tall both landed under a 3% bound and
        # BOTH OF THIS FILE'S RED RUNS WENT GREEN. A tolerance that a denser
        # world can walk through is not a bound.
        #
        # So it asks the guest instead. cs_boxlod and cs_stackverts are the
        # two paths a solid can take and cs_obj says which object is on one,
        # so a frame is a MAP of object -> box, polygons or absent; a taxi
        # straight at the city is monotone, so a state that goes away and
        # COMES BACK is the defect exactly, whatever the dither did.
        seen_at = []
        for k in range(14):
            d = -840 + k * 4
            m.pause()
            for n, v in (("cs_px", int(rax + d * math.sin(hh))), ("cs_py", 20),
                         ("cs_pz", int(raz + d * math.cos(hh)))):
                poke(n, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", (rh & 0xFFFF).to_bytes(2, "little"))
            poke("cs_pitch", b"\x00\x00")
            poke("cs_roll", b"\x00\x00")
            poke("cs_state", b"\x01")
            poke("cs_pause", b"\x01")
            poke("cs_setbld", b"\x04")
            poke("cs_setlod", b"\x02")
            poke("cs_setfill", b"\x03")
            for i in range(nobj):
                m.write(lin + objs + i * CSO_SIZE + CSO_SKIP, b"\x00\x00")
            m.run()
            m.advance(frames=10)
            m.run()
            state = {}
            m.bp_exec(lin + mp["cs_render"])
            m.run()
            if m.wait_stop(60) is None:
                sys.exit("skieslod: cs_render never ran")
            m.bp_exec(lin + mp["cs_render"], lin + mp["cs_boxlod"],
                      lin + mp["cs_stackverts"])
            for _ in range(300):
                m.run()
                if m.wait_stop(60) is None:
                    break
                ip = m.regs()["ip"]
                if ip == mp["cs_render"]:
                    break
                ob = int.from_bytes(m.read(lin + base + off("cs_obj"), 2),
                                    "little")
                state[ob] = "b" if ip == mp["cs_boxlod"] else "p"
            m.bp_exec()
            m.run()
            seen_at.append(state)
        seen_at = seen_at[2:]           # the first two are the scene settling
        flick = []
        for ob in sorted(set().union(*[set(s) for s in seen_at])):
            seq = [s.get(ob, ".") for s in seen_at]
            for i in range(len(seq)):   # a state that RETURNS after another
                if seq[i] in seq[i + 1:]:
                    j = seq.index(seq[i], i + 1)
                    if any(c != seq[i] for c in seq[i:j]):
                        nm = int.from_bytes(m.read(lin + ob + 14, 2), "little")
                        flick.append("%s %s" % (
                            m.readseg(seg, nm, 24).split(b"\0")[0]
                            .decode("ascii", "replace"), "".join(seq)))
                        break
        check(len(seen_at[0]) >= 4 and not flick,
              "nothing on the skyline goes away and comes back down the "
              "take-off run: %d objects drawn, %d flickered%s"
              % (len(seen_at[0]), len(flick),
                 "" if not flick else " - " + "; ".join(flick[:3])))

    print("skieslod: %s" % ("FAIL - " + "; ".join(bad) if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

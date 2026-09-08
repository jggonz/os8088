#!/usr/bin/env python3
"""THE OCCLUSION PASS (SPEC.md 88.13.7), on MartyPC, on Hercules.

    python3 tests/skiesocc.py [--machine os8088_5150_herc_gla]

cs_occlude marks the visible objects that stand wholly behind another one and
cs_drawpass skips them. The width rule it uses is exact for an object dead
ahead and errs either way for one off to the side (88.13.7.1) - a rule that
is conservative for every bearing was measured first and stops finding
Nepal's spurs 400 m off the threshold - so what keeps it honest is not the
arithmetic but this row.

  1. NEPAL asks for the pass (CSA_OCC) and finds objects to hide down the
     first two kilometres of the take-off run;
  2. EVERY VERDICT IS A NO-OP ON THE GLASS. For each object the pass says is
     hidden, the scene is captured, that object's range is set to 0, and the
     scene is captured again: the two must be pixel-identical over the view.
     If the rule ever hides something a player can see, the frames differ and
     this row goes red - which is the whole reason it exists;
  3. and the frame is SHORTER for it, by more than the pass costs;
  4. a location that did not ask gets no verdicts at all.

The captures SETTLE on the guest's own picture rather than advancing a fixed
number of frames: a paused Clear Skies is not a still picture - the water
moves - and a fixed advance catches the scene mid-repaint, which reads as a
difference that is not there.

--clobber-occ makes cs_occpair answer YES without testing anything, which is
the pass hiding whatever happens to be behind an occluder, and check 2 must
go red.
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
CSO_RANGE, CSO_SKIP, CSO_SIZE = 10, 18, 20
CSA_X, CSA_Z, CSA_HDG, CSA_OBJS, CSA_NOBJ, CSA_FLAGS = 2, 4, 8, 18, 20, 34
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
    ap.add_argument("--clobber-occ", action="store_true",
                    help="cs_occpair says yes without testing: must go red")
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

        def put(n, d):
            m.write(lin + base + off(n), d)

        def word(n):
            return int.from_bytes(m.read(lin + base + off(n), 2), "little")

        m.advance(frames=40)
        m.run()
        ports = {}
        for i in range(int.from_bytes(m.readseg(seg, mp["cs_drport"] + 10, 2),
                                      "little")):
            p = int.from_bytes(m.readseg(seg, mp["cs_ports"] + 2 * i, 2),
                               "little")
            q = int.from_bytes(m.readseg(seg, p, 2), "little")
            nm = ""
            while True:
                c = m.readseg(seg, q + len(nm), 1)[0]
                if not c:
                    break
                nm += chr(c)
            ports[nm] = p

        if a.clobber_occ:
            # cs_occpair's first instruction is `mov ax, [cs_ob_y0]`; an
            # `stc` and a `ret` in its place is the pass hiding everything
            # that has an occluder in front of it, tested or not.
            m.pause()
            m.write(lin + mp["cs_occpair"], b"\xF9\xC3")
            m.run()
            print("  (cs_occpair answers yes without testing: must fail)")

        def enter(port):
            m.pause()
            put("cs_airport", port.to_bytes(2, "little"))
            put("cs_inited", b"\x00")
            m.run()
            m.type_text("f")
            m.advance(frames=130)
            m.run()

        def viewpx():
            m.pause()
            fb = m.read(0xB0000, 0x8000)
            m.run()
            vy, wh = word("cs_vy"), word("cs_wh")
            wb0, wbn = word("cs_wb0"), word("cs_wbn")
            box0 = word("cs_vx") // 8
            out = bytearray()
            for y in range(vy, vy + wh):
                o = (y & 3) * 0x2000 + (y >> 2) * 90 + box0
                out += fb[o + wb0:o + wb0 + wbn]
            return bytes(out)

        def settled():
            prev = None
            for _ in range(12):
                m.advance(frames=6)
                cur = viewpx()
                if cur == prev:
                    return cur
                prev = cur
            return prev

        def pin(port, d, y, side=0, turn=0, drop=()):
            ax = int.from_bytes(m.read(lin + port + CSA_X, 2), "little")
            az = int.from_bytes(m.read(lin + port + CSA_Z, 2), "little")
            ax = ax - 65536 if ax > 32767 else ax
            az = az - 65536 if az > 32767 else az
            hdg = int.from_bytes(m.read(lin + port + CSA_HDG, 2), "little")
            h = hdg * 2 * math.pi / 65536.0
            objs = int.from_bytes(m.read(lin + port + CSA_OBJS, 2), "little")
            nobj = int.from_bytes(m.read(lin + port + CSA_NOBJ, 2), "little")
            m.pause()
            for n, v in (("cs_px", int(ax + d * math.sin(h)
                                       + side * math.cos(h))),
                         ("cs_py", y),
                         ("cs_pz", int(az + d * math.cos(h)
                                       - side * math.sin(h)))):
                put(n, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            put("cs_hdg", ((hdg + turn) & 0xFFFF).to_bytes(2, "little"))
            put("cs_pitch", b"\x00\x00")
            put("cs_roll", b"\x00\x00")
            put("cs_state", b"\x01")
            put("cs_pause", b"\x01")
            put("cs_setfill", b"\x03")
            for i in range(nobj):
                o = objs + i * CSO_SIZE
                if i in drop:
                    m.write(lin + o + CSO_RANGE, b"\x00\x00")
                m.write(lin + o + CSO_SKIP, b"\x00\x00")
            m.run()
            return objs, nobj

        def verdicts():
            """The pass's OWN marks, read inside a frame."""
            m.bp_exec(lin + mp["cs_drawpass"])
            m.run()
            if m.wait_stop(60) is None:
                sys.exit("skiesocc: cs_drawpass never ran")
            nv = word("cs_nvisn")
            occ = m.read(lin + base + off("cs_occ"), max(nv, 1))
            keys = [int.from_bytes(m.read(lin + base + off("cs_vkey")
                                          + 4 * i + 2, 2), "little")
                    for i in range(nv)]
            recs = [int.from_bytes(m.read(lin + k, 2), "little") for k in keys]
            m.bp_exec()
            m.run()
            return [recs[i] for i in range(nv) if occ[i]]

        def frame_ms(n=9):
            m.bp_exec(lin + mp["cs_render"])
            m.run()
            m.wait_stop(60)
            c0 = m.status()["cycles"]
            out = []
            for _ in range(n):
                m.run()
                if m.wait_stop(60) is None:
                    sys.exit("skiesocc: no frame")
                c1 = m.status()["cycles"]
                out.append((c1 - c0) / CPS * 1000.0)
                c0 = c1
            m.bp_exec()
            m.run()
            return sorted(out)[len(out) // 2]

        # --- 1 and 2: Nepal's verdicts, and each one against the glass -----
        nep = [k for k in ports if "NEPAL" in k][0]
        port = ports[nep]
        flags = int.from_bytes(m.read(lin + port + CSA_FLAGS, 2), "little")
        check(flags & 1, "%s asks for the pass (CSA_FLAGS %d)" % (nep, flags))
        enter(port)
        # EACH VERDICT IS CHECKED WITH THE PASS OFF, which is the whole
        # point: with it ON the object is already being skipped, so removing
        # it changes nothing and the check passes whatever the pass believes.
        # The first version did exactly that and its --clobber-occ run - a
        # cs_occpair that says yes without testing - came back GREEN with
        # twenty-two verdicts "confirmed invisible".
        # EIGHT viewpoints, and not all of them on the centreline: the rule
        # is exact for an object dead ahead and errs for one off to the side,
        # so a row that only ever looks straight down the runway is tuning
        # against the one case that cannot go wrong. The last three are
        # offset across the strip and turned.
        VIEWS = ((0, 30, 0, 0), (400, 140, 0, 0), (800, 250, 0, 0),
                 (1200, 360, 0, 0), (1600, 470, 0, 0), (2000, 590, 0, 0),
                 (600, 200, 700, 0), (1400, 400, -900, 0),
                 (1000, 300, 0, 4000))
        said = {}
        for v in VIEWS:
            objs, nobj = pin(port, *v)
            m.advance(frames=8)
            m.run()
            said[v] = [(rec - objs) // CSO_SIZE for rec in verdicts()]
        m.pause()
        m.write(lin + port + CSA_FLAGS, b"\x00\x00")   # the pass OFF
        m.run()
        total, blind = 0, 0
        for v in VIEWS:
            d = v[0]
            objs, nobj = pin(port, *v)
            m.advance(frames=8)
            m.run()
            was = settled()
            total += len(said[v])
            for i in said[v]:
                keep = int.from_bytes(m.read(lin + objs + i * CSO_SIZE
                                             + CSO_RANGE, 2), "little")
                pin(port, *v, drop=(i,))
                now = settled()
                if now == was:
                    blind += 1
                else:
                    n = sum(bin(was[j] ^ now[j]).count("1")
                            for j in range(len(was)))
                    check(False, "%d m out: the pass hid object %d, and with "
                                 "the pass OFF taking it away changes %d "
                                 "pixels - it was VISIBLE" % (d, i, n))
                m.pause()
                m.write(lin + objs + i * CSO_SIZE + CSO_RANGE,
                        keep.to_bytes(2, "little"))
                m.run()
            print("      %5d m out%s: %d hidden, %d confirmed invisible"
                  % (d, " (off the line)" if v[2] or v[3] else "",
                     len(said[v]), blind))
        m.pause()
        m.write(lin + port + CSA_FLAGS, flags.to_bytes(2, "little"))
        m.run()
        check(total >= 4, "the pass finds objects to hide down the take-off "
                          "run (%d verdicts over four viewpoints)" % total)
        check(blind == total,
              "and EVERY verdict is a no-op on the glass (%d of %d)"
              % (blind, total))

        # --- 3: and the frame is shorter for it ----------------------------
        objs, nobj = pin(port, 800, 250)
        frame_ms(4)
        on = frame_ms()
        m.pause()                               # the pass off: the flag alone
        m.write(lin + port + CSA_FLAGS, b"\x00\x00")
        m.run()
        pin(port, 800, 250)
        frame_ms(4)
        offms = frame_ms()
        m.pause()
        m.write(lin + port + CSA_FLAGS, flags.to_bytes(2, "little"))
        m.run()
        check(on < offms * 0.92,
              "the frame is shorter with the pass on (%.1f ms against %.1f)"
              % (on, offms))

        # --- 4: a location that did not ask gets nothing -------------------
        #
        # Switched by POKING cs_airport, not by leaving the bracket: F
        # toggles, and one that has not landed leaves this reading a world
        # that is not being drawn (SPEC.md 88.13.1.1).
        other = [k for k in ports if "NEPAL" not in k][0]
        m.pause()
        put("cs_airport", ports[other].to_bytes(2, "little"))
        m.run()
        pin(ports[other], 0, 60)
        m.advance(frames=8)
        m.run()
        check(not verdicts(),
              "%s did not ask, so the pass hides nothing there" % other)

    print("skiesocc: %s" % ("FAIL - " + "; ".join(bad) if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

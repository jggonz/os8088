#!/usr/bin/env python3
"""THE FAR MODEL NEVER STANDS IN WHILE THE EYE IS INSIDE THE NEAR ONE
(SPEC.md 88.6.1.1).

    python3 tests/skieswater.py [--machine os8088_5150_herc_gla] [--clobber-guard]

Reported off the machine as "the Icon A5, when taking off from Rio, has
nothing in view. On mono, I can't even see the water - it just looks like
ground? On VGA I can see the water." It was not the adapter: the two
machines were at different Draw Distances. cs_drawobj takes an object's FAR
model past CSO_LOD scaled by the rung, and at Near the 0.6 puts Rio's bay -
CSM_RAD 1,926 against a threshold of 1,564 - on its centreline while the A5
spawns 1,685 m from its origin, INSIDE it. The water is then a line at the
horizon and what is under the aeroplane is the horizon band's own ground:
12.5% dither on Hercules, green on Mode X. Four of the nine water strips did
it; Rio's has since been re-laid down the bay at Sugarloaf (SPEC.md 88.7.7.5)
and spawns 702 m from the bay's origin, inside every rung's threshold, so
this row flies the three that still sit past it - Le Bourget, London City
and JFK.

For each of them, at Draw Distance = Near:

  1. the spawn really is past the scaled threshold of a piece it is inside,
     so the guard is what decides the picture - a world edit that moved the
     strip would make this row a test of nothing, and it says so instead;
  2. every water piece whose origin is within its own radius of the eye
     enters cs_flatverts as its NEAR model and its far model does not - asked
     of the GUEST at the breakpoint, one frame, and not of the pixels;
  3. and on Hercules the rows under the horizon are the river's stripes
     (SPEC.md 88.4.4: FF 00 FF 00) rather than the ground's 12.5% - the
     reading the field made, done by the machine.

--clobber-guard NOPs the compare and the load that clamp the threshold to
CSM_RAD, which is the switch exactly as it shipped, and every location goes
red on 2. Check 3 goes red with it at two of the three: at JFK the Hudson
is a second piece under the eye, 1,089 m down the nose against a threshold
of 1,804, so its stripes are under the horizon in both arms and only the
East River's centreline says which arm this is.
"""
import argparse
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

PORTS = ["SPX", "LCY", "MIA", "VNLK", "JFK", "ISSY", "LBG", "SDU", "SFO"]
LOCS = ["LBG", "LCY", "JFK"]            # the three of 88.6.1.1's four whose
                                        # spawn is still past the scaled
                                        # threshold: Rio's strip moved
                                        # (88.7.7.5) and is inside it now
NEAR_SCALE = 154                        # cs_lodscl[CSL_NEAR], 8.8
FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def equates():
    E = {}
    for line in open(os.path.join(ROOT, "apps", "skies", "skies.asm")):
        m = re.match(r"^(CS[A-Z]*_[A-Z0-9_]+)\s+equ\s+"
                     r"(-?(?:0[xX][0-9A-Fa-f]+|\d+))\s*(?:;|$)", line)
        if m:
            E[m.group(1)] = int(m.group(2), 0)
    need = ["CSA_OBJS", "CSA_NOBJ", "CSO_MODEL", "CSO_FAR", "CSO_X", "CSO_Z",
            "CSO_LOD", "CSO_SIZE", "CSO_NAME", "CSM_RAD", "CSM_TYPE",
            "CSM_FLAT", "CSM_NF", "CSM_FACES", "CSI_RIVER", "CSL_NEAR"]
    miss = [n for n in need if n not in E]
    if miss:
        sys.exit("skieswater: skies.asm no longer defines %s" % ", ".join(miss))
    return E


def sg(v):
    return v - 65536 if v >= 32768 else v


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-guard", action="store_true",
                    help="the far-model switch as it shipped: must go red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    E = equates()
    mp = dispapps._map("skies")

    def off(n):
        return dispapps.bss_off("skies", n)

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def W(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def Bb(n):
            return m.readseg(seg, base + off(n), 1)[0]

        def poke(n, d):
            m.write(lin + base + off(n), d)

        def rec(at, o):
            return int.from_bytes(m.readseg(seg, at + o, 2), "little")

        def name(at):
            return m.readseg(seg, at, 24).split(b"\0")[0].decode("latin-1")

        if a.clobber_guard:
            # `cmp bx, [si + CSM_RAD]` / `jge` / `mov bx, [si + CSM_RAD]`, the
            # eight bytes 88.6.1.1 added at the switch, NOPped - which is the
            # switch as it shipped. Bounded to cs_drawobj so a second copy of
            # the pattern elsewhere cannot be the one patched.
            lo, hi = mp["cs_drawobj"], mp["cs_stackverts"]
            code = m.read(lin + lo, hi - lo)
            pat = bytes([0x3B, 0x5C, E["CSM_RAD"], 0x7D, 0x03,
                         0x8B, 0x5C, E["CSM_RAD"]])
            hits = [i for i in range(len(code)) if code[i:i + 8] == pat]
            if len(hits) != 1:
                sys.exit("skieswater: the radius clamp is not where this "
                         "patch expects (%d match(es) in cs_drawobj) - re-read "
                         "cs3d.inc before trusting the red run" % len(hits))
            m.pause()
            m.write(lin + lo + hits[0], b"\x90" * 8)
            m.run()
            print("  (the far-model switch as it shipped: this must fail)")

        RENDER, FLAT = lin + mp["cs_render"], lin + mp["cs_flatverts"]

        def enter(loc):
            """Leave the flight if in one, pick `loc` and the A5 at Draw
            Distance = Near, and enter the bracket."""
            # IN THE BRACKET is cs_back set AND cs_quit clear: cs_back is the
            # backend and stays set after a flight, cs_quit is what the second
            # f sets on the way out (csgame.inc's .quit)
            if Bb("cs_back") and not Bb("cs_quit"):
                m.type_text("f")
                for _ in range(40):
                    m.advance(frames=20)
                    m.run()
                    if Bb("cs_quit"):
                        break
                if not Bb("cs_quit"):
                    sys.exit("skieswater: the flight never gave the launcher back")
                m.advance(frames=60)            # the launcher repainted
                m.run()
            m.pause()
            # THE ROW AND NOT THE RECORD (SPEC.md 88.10.5): cs_cmd_fly turns
            # cs_apnow into a world on its way into the bracket
            poke("cs_apnow", bytes([PORTS.index(loc)]))
            poke("cs_plane", mp["cs_p_a5"].to_bytes(2, "little"))
            poke("cs_setlod", bytes([E["CSL_NEAR"]]))
            poke("cs_inited", b"\x00")
            m.run()
            m.type_text("f")
            # ...and cs_inited too: cs_fsx_main sets cs_back before cs_reset
            # has put the aeroplane anywhere, and a read in that window is an
            # A5 at (0, 0) heading north
            for _ in range(40):
                m.advance(frames=20)
                m.run()
                if Bb("cs_back") and not Bb("cs_quit") and Bb("cs_inited"):
                    break
            if not Bb("cs_back") or Bb("cs_quit") or not Bb("cs_inited"):
                sys.exit("skieswater: the fsx bracket never took a mode at %s" % loc)
            one_frame()                         # the first frame drawn, so
                                                # every word below is settled

        def one_frame():
            """The models cs_flatverts was entered with between one cs_render
            and the next: {model address: times}."""
            m.pause()
            m.bp_exec(RENDER, FLAT)
            m.run()
            for _ in range(200):
                if m.wait_stop(180) is None:
                    sys.exit("skieswater: cs_render never ran")
                if m.regs()["ip"] == mp["cs_render"]:
                    break
                m.run()
            drawn = {}
            for _ in range(400):
                m.run()
                if m.wait_stop(180) is None:
                    sys.exit("skieswater: the frame never ended")
                if m.regs()["ip"] == mp["cs_render"]:
                    break
                md = W("cs_mdl")
                drawn[md] = drawn.get(md, 0) + 1
            m.bp_exec()
            m.run()
            return drawn

        def water_stripes():
            """Hercules only: of the view rows under the horizon, how many are
            lit edge to edge and how many are dark - the river's FF 00 FF 00
            against the ground's two pixels in sixteen."""
            v = m.video()
            if v["type"] not in ("mda", "hercules", "herc"):
                return None
            w, h, rows = m.vram(None)
            x0 = W("cs_vx") + W("cs_wx0")
            ww, vy = W("cs_ww"), W("cs_vy")
            y0 = vy + sg(W("cs_hzy1")) + 2          # clear of the horizon band
            y1 = vy + W("cs_wh") - 1
            lit = [sum(rows[y][x0:x0 + ww]) for y in range(y0, y1)]
            full = sum(1 for n in lit if n >= ww - ww // 20)
            dark = sum(1 for n in lit if n <= ww // 5)
            return full, dark, len(lit)

        for loc in LOCS:
            enter(loc)
            port = W("cs_airport")
            ex, ez = sg(W("cs_ex")), sg(W("cs_ez"))
            th = W("cs_hdg") * 2 * math.pi / 65536.0
            print("--- %s: the A5 at (%d, %d) heading %03.0f, Draw Distance = Near"
                  % (loc, ex, ez, W("cs_hdg") * 360.0 / 65536))
            check(Bb("cs_onwater") == 1, "%s: the A5 starts ON THE WATER" % loc)
            # every water piece with a far model whose origin is within its
            # own radius of the eye - the objects the guard is about
            objs, nobj = rec(port, E["CSA_OBJS"]), rec(port, E["CSA_NOBJ"])
            inside = []
            for o in range(objs, objs + nobj * E["CSO_SIZE"], E["CSO_SIZE"]):
                far = rec(o, E["CSO_FAR"])
                md = rec(o, E["CSO_MODEL"])
                if not far or m.readseg(seg, md + E["CSM_TYPE"], 1)[0] != E["CSM_FLAT"]:
                    continue
                if not m.readseg(seg, md + E["CSM_NF"], 1)[0]:
                    continue
                if m.readseg(seg, rec(md, E["CSM_FACES"]) + 1, 1)[0] != E["CSI_RIVER"]:
                    continue
                dx, dz = sg(rec(o, E["CSO_X"])) - ex, sg(rec(o, E["CSO_Z"])) - ez
                rad = sg(rec(md, E["CSM_RAD"]))
                if math.hypot(dx, dz) >= rad:
                    continue
                along = dx * math.sin(th) + dz * math.cos(th)
                thr = (sg(rec(o, E["CSO_LOD"])) * NEAR_SCALE) >> 8
                inside.append((o, md, far, rad, along, thr))
            check(len(inside) > 0,
                  "%s: the eye is inside at least one water piece that has a "
                  "far model (%d)" % (loc, len(inside)))
            # 1. the spawn is past the SCALED threshold of one of them, so the
            #    guard is what decides - otherwise this row guards nothing
            bites = [t for t in inside if t[4] > t[5]]
            check(len(bites) > 0,
                  "%s: ...and past the scaled CSO_LOD of one of them, so the "
                  "clamp is what decides (%s)"
                  % (loc, ", ".join("%s %.0f m down the nose against %d"
                                    % (name(rec(t[0], E["CSO_NAME"])), t[4], t[5])
                                    for t in inside)))
            # 2. one frame: what cs_flatverts was entered with
            drawn = one_frame()
            for o, md, far, rad, along, thr in inside:
                nm = name(rec(o, E["CSO_NAME"]))
                check(md in drawn and far not in drawn,
                      "%s: %s (radius %d, origin %.0f m down the nose) is drawn "
                      "as its NEAR model and not its centreline (near x%d, far x%d)"
                      % (loc, nm, rad, along, drawn.get(md, 0), drawn.get(far, 0)))
            # 3. Hercules: the rows under the horizon are stripes
            st = water_stripes()
            if st is not None:
                full, dark, n = st
                check(full >= n // 3 and dark >= n // 3,
                      "%s: under the horizon %d of %d view rows are lit edge to "
                      "edge and %d dark - the river's stripes, not the ground's "
                      "dither" % (loc, full, n, dark))

    if FAILS:
        print("skieswater: %d check(s) failed:" % len(FAILS))
        for f in FAILS:
            print("  - " + f)
        sys.exit(1)
    print("skieswater: ok")


if __name__ == "__main__":
    main(sys.argv[1:])

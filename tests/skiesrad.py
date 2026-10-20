#!/usr/bin/env python3
"""cs_pwhole NEVER LIES: no vertex is behind when the object says it is whole
(SPEC.md 88.5.11), on MartyPC.

    python3 tests/skiesrad.py [--clobber-rad]

Reported off the machine: *"in building/ground wire mode, with otherwise max
settings, sometimes lines will draw across the cockpit."*

`cs_projall` predicts, from `CSM_RAD`, that an object is wholly in front of
the near plane, and two fast paths are built on that prediction being true:
`cs_edge1` opens `cmp byte [cs_pwhole],0 / jne .both` and reads
`cs_sxv`/`cs_syv` WITHOUT testing `cs_fv`, and `.conv` sets `cs_pinview` off
a box taken from the projected vertices, which turns `cs_seg`'s clip off
entirely. A vertex that took `.behind` has no `cs_sxv` this frame - the slot
still holds whatever the last object to use that index left in it - so the
edge is drawn from a real vertex to a stale one with no clip, and it goes
wherever that stale coordinate is.

**THE INVARIANT IS THE ASSERTION AND THE PIXELS ARE NOT**, deliberately.
Whether a line actually lands on the cockpit depends on what the previous
object happened to leave in the slot, so the symptom is intermittent by
construction - it showed on 1 of 80 poses swept around the Shard. A row that
asserted the pixels would go green on a broken build four times in five.
What is exact, and true at the moment the frame is drawn, is this: if
`cs_pwhole` is set then every `cs_fv` is 1.

The Shard is the model to fly at - 306 m, a `CS_PYR`, the tallest thing in
any world - and the pose is a dive past it, which is what puts its apex
behind the near plane while its base is comfortably in front.

--clobber-rad is the red run (docs/WRITING-TESTS.md 1) and restores BOTH
halves of the defect, because either alone is enough to hide it: the Shard's
CSM_RAD goes back to the 209 that `wx + wz + h / 2` gave it, and 88.5.11.1's
five-byte `mov byte [cs_pwhole],0` in `.behind` is NOPped out.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402
import csworlds                                             # noqa: E402

PORTS = ["SPX", "LCY", "MIA", "VNLK", "JFK", "ISSY", "LBG", "SDU", "SFO"]
SHARD = (2700, 480)                     # where csw_lcy.inc stands it
POSES = [                               # (distance short of it, altitude, pitch)
    (160, 220, -60), (200, 240, -60), (240, 260, -60),
    (160, 260, -45), (200, 200, -75), (120, 180, -60),
]
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
    ap.add_argument("--clobber-rad", action="store_true",
                    help="the Shard's old radius AND no .behind guard: red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")

    def off(n):
        return dispapps.bss_off("skies", n)

    E = {}
    for line in open(os.path.join(ROOT, "apps", "skies", "skies.asm")):
        m = re.match(r"^(CSM_[A-Z0-9_]+)\s+equ\s+(\d+)\s*(?:;|$)", line)
        if m:
            E[m.group(1)] = int(m.group(2))
    if "CSM_RAD" not in E or "CSM_NV" not in E:
        sys.exit("skiesrad: skies.asm no longer defines CSM_RAD/CSM_NV")

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def W(n):
            return int.from_bytes(m.read(lin + base + off(n), 2), "little")

        def Bb(n):
            return m.readseg(seg, base + off(n), 1)[0]

        def poke(n, d):
            m.write(lin + base + off(n), d)

        m.pause()
        # THE ROW AND NOT THE RECORD (SPEC.md 88.10.5): a location's record
        # lives in the world overlay, so only the loaded world has one - and
        # cs_cmd_fly turns the row into a world on its way into the bracket.
        poke("cs_apnow", bytes([PORTS.index("LCY")]))
        poke("cs_inited", b"\x00")
        m.run()
        m.type_text("f")                        # ONE f enters the flight; a
        for _ in range(30):                     # second one LEAVES it
            m.advance(frames=20)
            m.run()
            if Bb("cs_back") and not Bb("cs_quit"):
                break
        if not Bb("cs_back") or Bb("cs_quit"):
            sys.exit("skiesrad: the fsx bracket never took a mode")

        RENDER = lin + mp["cs_render"]

        def gframes(n):
            """n COMPLETE guest frames. advance(frames=) is EMULATOR frames
            and a High/Ultra frame is ~195 ms of guest time, so forty of
            those do not cover one of these - which is how an earlier version
            of this compared two pictures neither of which had been drawn."""
            m.pause()
            m.bp_exec(RENDER)
            m.run()
            for _ in range(n + 1):
                if m.wait_stop(180) is None:
                    sys.exit("skiesrad: a guest frame never came")
                m.run()
            m.pause()
            m.bp_exec()
            m.run()

        gframes(2)                              # the flight fully up FIRST:
        m.pause()                               # cs_inited=0 re-initialises
        poke("cs_setbld", b"\x04")              # the world and puts the
        poke("cs_setlod", b"\x03")              # aeroplane back on the runway
        poke("cs_setfill", b"\x00")             # over any pose
        poke("cs_pause", b"\x01")
        m.run()
        gframes(2)

        # THE WORLD'S MAP AND NOT THE PROGRAM'S (SPEC.md 88.10.5). The Shard
        # is a mesh in csw_lcy.inc, which is packed into a part and read into
        # the overlay at run time - so `cs_m_lcy_shd` is not a symbol of the
        # package at all any more. csworlds.world_map lays the world out at
        # the address cs_wldget puts it, so what comes back is the address
        # the guest has.
        shard = csworlds.world_map(csworlds.WORLD_OF["lcy"])["cs_m_lcy_shd"]
        rad = int.from_bytes(m.readseg(seg, shard + E["CSM_RAD"], 2), "little")
        nvx = m.readseg(seg, shard + E["CSM_NV"], 1)[0]
        print("    cs_m_lcy_shd: CSM_RAD %d over %d levels" % (rad, nvx))

        if a.clobber_rad:
            m.pause()
            m.write(lin + shard + E["CSM_RAD"], (209).to_bytes(2, "little"))
            # ...and 88.5.11.1's guard. cs_projall writes cs_pwhole twice -
            # 0 at the top and 0 in .behind - so the SECOND in address order
            # is the guard, and a count that is not two means the routine has
            # been rewritten and this patch is guessing.
            lo, hi = mp["cs_projall"], mp["cs_faces"]
            code = m.read(lin + lo, hi - lo)
            pat = b"\xC6\x06" + (base + off("cs_pwhole")).to_bytes(2, "little") + b"\x00"
            at, hits = -1, []
            while True:
                at = code.find(pat, at + 1)
                if at < 0:
                    break
                hits.append(at)
            if len(hits) != 2:
                sys.exit("skiesrad: cs_projall clears cs_pwhole %d times, not "
                         "2 - re-read it before trusting the red run"
                         % len(hits))
            m.write(lin + lo + hits[1], b"\x90" * 5)
            m.run()
            print("  (the Shard back to 209 and no .behind guard: must fail)")

        def pin(d, alt, pitch):
            m.pause()
            for nm, v in (("cs_px", SHARD[0]), ("cs_py", alt),
                          ("cs_pz", SHARD[1] - d)):
                m.write(lin + base + off(nm),
                        ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            for nm, v in (("cs_hdg", 0), ("cs_pitch", pitch), ("cs_roll", 0)):
                m.write(lin + base + off(nm),
                        ((v * 65536 // 360) & 0xFFFF).to_bytes(2, "little"))
            m.run()
            gframes(1)
            m.pause()                           # A PIN NOTHING READS BACK IS
            got = [int.from_bytes(m.read(lin + base + off(n), 4), "little",
                                  signed=True) // 256
                   for n in ("cs_px", "cs_py", "cs_pz")]
            m.run()
            want = [SHARD[0], alt, SHARD[1] - d]
            if got != want:
                sys.exit("skiesrad: the pose did not take - %s against %s. The "
                         "world was still stepping when it was written "
                         "(docs/WRITING-TESTS.md 13 row 35)" % (got, want))

        names = {v: k for k, v in mp.items() if k.startswith("cs_m_")}
        viol, objs = {}, 0
        for d, alt, pitch in POSES:
            pin(d, alt, pitch)
            m.pause()
            m.bp_exec(lin + mp["cs_edges"])
            m.run()
            for _ in range(80):
                if m.wait_stop(180) is None:
                    break
                objs += 1
                if Bb("cs_pwhole"):
                    nv = max(0, min(W("cs_nv"), 24))
                    fv = [int.from_bytes(
                        m.read(lin + base + off("cs_fv") + 2 * k, 2), "little")
                        for k in range(nv)]
                    if any(f != 1 for f in fv):
                        nm = names.get(W("cs_mdl"), hex(W("cs_mdl")))
                        viol.setdefault(nm, ((d, alt, pitch), nv, fv,
                                             Bb("cs_pinview")))
                m.run()
            m.bp_exec()
            m.run()

        for nm, (pose, nv, fv, piv) in viol.items():
            print("    %-20s at d=%d alt=%d pitch=%d: nv=%d pinview=%d fv=%s"
                  % (nm, pose[0], pose[1], pose[2], nv, piv,
                     "".join(str(f) for f in fv)))
        check(objs > 40, "the dive draws objects to look at (%d over %d poses)"
              % (objs, len(POSES)))
        check(not viol,
              "no object says cs_pwhole with a vertex behind the near plane "
              "(%d model(s) did)" % len(viol))

    print("  %s" % ("ok" if not bad else "FAILED: " + "; ".join(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

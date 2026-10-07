#!/usr/bin/env python3
"""CLEAR SKIES' faces and edges, COUNTED - and each term priced by ADDING it.

**COUNT WITH --fly, PRICE WITHOUT IT.** The two halves of this file want
opposite things from the machine. A POPULATION is what the program actually
draws, so it has to be flying - pinned, `slightbank` at 12 degrees puts 2.2
segments a frame into a line body where flying puts 15. A PRICE is a
whole-frame difference between two arms, so both arms have to see the same
scene - flying, this file's own resolution goes to 26-43 ms on a 217 ms
frame and nothing resolves at all. Pinned, the same terms come back at +/-16
to +/-111 cycles. So --fly skips the pricing half and says so.

    python3 tests/skiescount.py [--scene city] [--machine os8088_5150_herc_gla]
                                [--roll 12]

AN INSTRUMENT, NOT A GATE - registered as such in tests/unit/t_registry.py,
and it asserts nothing. `tests/skiesperf.py` prices a STAGE by patching its
call out; that answers "what does this stage cost" and cannot answer "how much
of it is redundant", because a NOPed call takes its consequences with it - NOP
`cs_edge` and the polygon's rows are never filled either, so the number is the
tracing PLUS the fill it removed.

**So every A/B here prices a term by ADDING it, never by removing it, and every
arm draws the identical picture.** It needs `make skiesprobe`, which is
`apps/skies` built with -DCSPROBE - counters and four runtime arms behind
`%ifdef`, so the SHIPPED package is byte-identical (`make` then `md5sum
build/skies.bin`).

    cs_dbl      trace every edge TWICE. cs_edge is idempotent - a chain takes
                the same value and `.both` takes min/max - so the frame's
                difference over the trace count is ONE trace, exactly
    cs_nomark   skip the index lookup and cs_edgemark, so the difference is
                what a runtime DEDUP TEST costs. It is paid on every edge and
                pays back on the duplicates alone, which is the whole of why
                SPEC.md 88.4.2.1 refuses it
    cs_cpy      a duplicate ALSO pays the copy that would stand in for the
                trace it skips, done INTO SCRATCH in front of the real trace
    cs_dupgath  ...the gather alone, and cs_duparea the winding cross alone
    cs_dblplot  a 1bpp line's PLOT, done twice. `or` is idempotent too, so
                this prices one `or [es:di], al` exactly (SPEC.md 88.4.3.1)
    cs_axoff    cs_axcull (88.5.12) computes and does NOT act, so the winding
                decides every face - the A/B for what the cull is worth, and
                with the counters beside it the AUDIT for whether the two
                verdicts ever differ. **cs_axmask must be poked to 0xFF**:
                package bss is zeroed, so it defaults to 0 and the cull is
                then inert in BOTH arms (docs/plans/SKIES-FRAME-PLAN.md 0.2)
    cs_dupface  every face repeats its PREAMBLE - the gather of its projected
                vertices by index and the quad's diagonal cross - into
                scratch. 29-40% of walked faces are then thrown away by the
                winding, so this prices what an earlier cull could reach
                (docs/plans/SKIES-FRAME-PLAN.md 3)

THE SCENE DECIDES THE ANSWER AND THE PINNED THREE ARE THE WRONG ONES for a
question about buildings: `runway`, `city` and `tower` are SPEC.md 88.12's,
where the skyline is far enough to be LOD-boxed (88.5.4) and the big polygons
are one-face FLAT models. The three `df*` scenes stand among La Defense's six
110 m towers instead, at the same ~950 m, and the discriminator is the PITCH:
with the eye above the roofs a box shows two walls AND a roof - three faces
meeting at three shared edges of twelve traced - and with it below, two faces
sharing one of eight.
"""
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88build
import os88parts
import os88pkg                                              # noqa: E402
import skies as skiestest                                   # noqa: E402


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# WHERE `cswidx.inc` IS. Clear Skies' resident world index is GENERATED
# (SPEC.md 88.10.5.3), so it is not in apps/skies/ and nasm reaches it only
# through the build tree - which the Makefile passes as `-I $(BUILD)/` and
# every script that re-assembles for a LISTING has to pass too, or the tree
# "does not assemble" and the message points at the package. os88build.at
# honours $OS88_BUILD, so a frozen soak tree resolves to its own copy.
CSWIDX = os.path.join(ROOT, os88build.at("build")) + os.sep

CPS = 4772727                           # the 4.77 MHz clock: cycles a second
PROBE = "build/skiesprobe"

SCENES = {                              # x, y, z (metres), heading, pitch[, roll]
    # --- SPEC.md 88.12's three, so a reading here sits beside skiesperf's ---
    "runway": None,                     # wherever cs_reset put it
    "tower": (-640, 150, -640, 45, 0),
    "city": (150, 300, -900, 30, -5),
    # --- LA DEFENSE: six cs_m_tower2, 60 m square and 110 m tall, around
    #     (-3940, +3580), at ~950 m so all three share a projected size and
    #     an LOD rung. These are the BUILDING scenes (see the note above) ---
    "dfsquare": (-3940, 300, 2600, 0, -12),     # nose on, eye above the roofs
    "dfangled": (-4600, 300, 2900, 45, -12),    # 45 deg off, eye above
    "dflevel": (-4600, 60, 2900, 45, 0),        # 45 deg off, eye BELOW them
    # --- THE BANK'S OWN SCENE, tests/skiesprof.py's `slightbank` profile to
    #     the metre, so a population counted here sits beside a stage timed
    #     there. Its subject is the LINE WALK and not the faces: a bank takes
    #     every ground-hugging segment off the run slice (SPEC.md 88.4.3, six
    #     pixels a row) and onto the per-pixel arm, and `--roll` is what walks
    #     that. 0 is the control and is where the slice still has them all ---
    "slightbank": (150, 33, -2000, 30, 0, 12),
}


def probemap():
    """The -DCSPROBE build's map, and a check that build/skiesprobe holds it.

    NOT dispapps._map: that one compares a `defines` build against
    build/smallapp/, which is the APP_SMALL arm and not this one. The
    comparison itself is kept, because it is the failure that costs a whole
    reading - build/ behind the tree returns offsets for a layout the guest
    has not got, and what comes back is plausible rubbish rather than an
    error.
    """
    src = os.path.join(ROOT, "apps/skies/skies.asm")
    fd, tmp = tempfile.mkstemp(suffix=".asm")
    os.close(fd)
    fd, mp = tempfile.mkstemp(suffix=".map")
    os.close(fd)
    fd, bn = tempfile.mkstemp(suffix=".bin")
    os.close(fd)
    open(tmp, "w").write(open(src).read() + "\n[map all %s]\n" % mp)
    r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-DCSPROBE",
                        "-I", os.path.join(ROOT, "apps") + os.sep,
                        "-I", os.path.join(ROOT, "apps", "skies") + os.sep,
                        # ...and the PROBE TREE'S OWN cswidx.inc, not build/'s:
                        # `make skiesprobe` recurses with BUILD=build/skiesprobe
                        # and generates one there, so reaching for build/'s
                        # would assemble this against the SHIPPED tree's
                        # addresses - which is the stale-tree failure the
                        # comparison below exists to catch, arriving through
                        # the include path instead (skiesdiag's note one file
                        # along, for the same reason)
                        "-I", os.path.join(ROOT, PROBE) + os.sep,
                        "-o", bn, tmp], capture_output=True, text=True)
    if r.returncode:
        sys.exit("skiescount: the -DCSPROBE build does not assemble:\n"
                 + r.stderr[:400])
    out = {}
    for line in open(mp):
        p = line.split()                # "<vaddr> <raddr> <name>", HEX
        if len(p) == 3:
            try:
                out[p[2]] = int(p[0], 16)
            except ValueError:
                pass
    fresh = open(bn, "rb").read()
    for f in (tmp, mp, bn):
        try:
            os.unlink(f)
        except OSError:
            pass
    o88 = os.path.join(ROOT, PROBE, "skies.o88")
    try:
        raw = open(o88, "rb").read()
        built = os88pkg.image_unwrap(raw)
        # A PART IS NOT THE IMAGE (SPEC.md 88.10.4): the image is a LOADER now
        # and `skies.asm` assembles to part 0, so an image that declares parts
        # is compared against the PART - dispapps._map's rule, which this
        # routine deliberately does not go through
        if os88parts.table_at(built) is not None:
            built = os88parts.part_bytes(raw, 0)
    except OSError as e:
        sys.exit("skiescount: %s (%s) - run `make skiesprobe`" % (o88, e))
    if built != fresh:
        sys.exit("skiescount: %s holds a %d-byte image and the tree assembles "
                 "to %d, so every offset below describes a layout the guest "
                 "has not got. Run `make skiesprobe`." % (o88, len(built),
                                                          len(fresh)))
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--scene", default="city", choices=sorted(SCENES))
    ap.add_argument("--frames", type=int, default=12)
    ap.add_argument("--count-frames", type=int, default=8)
    ap.add_argument("--fill", default="all", choices=("all", "wire", "terrain",
                                                      "bldg"),
                    help="[cs_setfill]: `wire` clears both bits, so every face "
                         "is its OUTLINE (SPEC.md 88.13.3) and the frame is "
                         "segments rather than polygons")
    ap.add_argument("--fly", action="store_true",
                    help="do NOT pause the world after the teleport: fly it, "
                         "with the bank HELD, the way tests/skiesprof.py's "
                         "profiles do. **A PAUSED SCENE IS NOT THE FLYING "
                         "ONE and the counts do not transfer** - `slightbank` "
                         "paused at 12 degrees puts 2.2 segments a frame into "
                         "a line body and flying puts 15 into cs_seg, so a "
                         "population counted with the world stopped prices a "
                         "renderer nobody runs. Pinning is right for an A/B "
                         "where both arms must draw the IDENTICAL picture; it "
                         "is wrong for a census")
    ap.add_argument("--price", action="store_true",
                    help="with --fly: take the per-term A/Bs anyway. They "
                         "will not resolve - see the note on --fly - and this "
                         "is here so that claim can be re-checked rather than "
                         "believed")
    ap.add_argument("--spd", type=int, default=60,
                    help="with --fly: metres a second (slightbank's 60)")
    ap.add_argument("--thr", type=int, default=100,
                    help="with --fly: the throttle (slightbank's 100)")
    ap.add_argument("--roll", type=float, default=None,
                    help="override the scene's bank, in degrees. The whole "
                         "point of the `slightbank` scene: the shallow arm's "
                         "population is a function of it and of nothing else "
                         "in the scene")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    MP = probemap()
    render = MP["cs_render"]

    def off(n):
        return MP[n] - MP["os88_image_end"]

    with os88marty.launch(a.image, apps=os.path.join(PROBE, "apps360.img"),
                          machine=a.machine) as m:
        slot, seg, base = skiestest.open_game(m)
        lin = seg << 4

        def w(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def poke(n, d):
            m.write(lin + base + off(n), d)

        m.type_text("f")
        m.advance(frames=30)
        m.run()
        print("  backend %d, view %dx%d, scene %s, fill %s"
              % (w("cs_back") & 0xFF, w("cs_ww"), w("cs_wh"), a.scene, a.fill))

        # --- pin the scene: skiesperf.py's poke, and its skip-table clear ---
        sc = SCENES[a.scene]
        m.pause()
        if sc:
            x, y, z, hdg, pitch = sc[:5]
            roll = sc[5] if len(sc) > 5 else 0
            if a.roll is not None:
                roll = a.roll
            for n, v in (("cs_px", x), ("cs_py", y), ("cs_pz", z)):
                poke(n, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", ((hdg * 65536 // 360) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_pitch", ((pitch * 65536 // 360) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_roll",
                 (int(roll * 65536 / 360) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_state", b"\x01")
            if a.fly:
                poke("cs_spd", (a.spd * 128).to_bytes(2, "little"))
                poke("cs_thr", a.thr.to_bytes(2, "little"))
        # cs_axmask IS ZERO IN BSS and `and dl, [cs_axmask]` is what decides
        # a face, so a -DCSPROBE build has cs_axcull INERT until this is
        # poked - every face passes, every vertex reads as wanted, and a
        # census of what the cull reaches comes back all zeros. This file's
        # own docstring has said so since the day it was written and this
        # is the line that acts on it.
        poke("cs_axmask", b"\xFF")
        poke("cs_setfill", bytes([{"all": 3, "terrain": 1, "bldg": 2,
                                   "wire": 0}[a.fill]]))
        if not a.fly:
            poke("cs_pause", b"\x01")
        ap_ = w("cs_airport")           # a poke is a teleport: SPEC.md 88.5.2's
        objs = int.from_bytes(m.read(lin + ap_ + 18, 2), "little")
        nobj = int.from_bytes(m.read(lin + ap_ + 20, 2), "little")
        for o in range(objs, objs + nobj * 20, 20):
            m.write(lin + o + 18, b"\x00\x00")
        m.run()

        # --- the counts, over N frames with the counters zeroed between ----
        m.bp_exec(lin + render)
        m.run()
        if m.wait_stop(20) is None:
            sys.exit("skiescount: cs_render never ran")
        m.run()
        m.wait_stop(20)                 # a warm-up frame, discarded
        for n in ("cs_dbg_etr", "cs_dbg_edup", "cs_dbg_ecut", "cs_dbg_erow",
                  "cs_dbg_edrow", "cs_dbg_fwalk", "cs_dbg_fcull",
                  "cs_dbg_fpoly", "cs_dbg_fout", "cs_dbg_fbox", "cs_dbg_ftr",
                  "cs_dbg_wsh", "cs_dbg_wsl", "cs_dbg_wst", "cs_dbg_wvt",
                  "cs_dbg_wshpx", "cs_dbg_wshby", "cs_dbg_wstpx",
                  "cs_dbg_wvtpx", "cs_dbg_wslrow", "cs_dbg_wslpx",
                  "cs_dbg_prow", "cs_dbg_ppx", "cs_dbg_pby",
                  "cs_dbg_pby2", "cs_dbg_pby4", "cs_dbg_pby8",
                  "cs_dbg_mfr", "cs_dbg_mstab",
                  "cs_dbg_fvn", "cs_dbg_fvx", "cs_dbg_fvz",
                  "cs_dbg_pvobj", "cs_dbg_pvobje", "cs_dbg_pvedge",
                  "cs_dbg_pvfree", "cs_dbg_vcand", "cs_dbg_vunused"):
            m.write(lin + base + off(n), b"\x00\x00")
        N = a.count_frames
        # --- WITH --fly THE BANK IS RE-PINNED EVERY FRAME ------------------
        # 88.7.5's easing rolls the bank out over the ticks, so a census that
        # teleported to 12 degrees and then flew twelve frames would be
        # counting a scene that was already back near level by the end. The
        # breakpoint is cs_render's own entry, so this pins the roll the
        # frame's cs_matrix is about to read - skiesprof.py pins at the call
        # to cs_matrix itself, one step further in, which differs only by the
        # easing inside a single frame and cannot move a population.
        rollv = None
        if a.fly and sc:
            rollv = (int((a.roll if a.roll is not None
                          else (sc[5] if len(sc) > 5 else 0)) * 65536 / 360)
                     & 0xFFFF).to_bytes(2, "little")
            poke("cs_roll", rollv)
        m.run()
        for _ in range(N):
            if m.wait_stop(20) is None:
                sys.exit("skiescount: the frame never came")
            if rollv is not None:
                poke("cs_roll", rollv)
            m.run()
        m.wait_stop(20)
        m.bp_exec()
        tr, dup, cut = w("cs_dbg_etr"), w("cs_dbg_edup"), w("cs_dbg_ecut")
        row, drow = w("cs_dbg_erow"), w("cs_dbg_edrow")
        fw, fc, fp = w("cs_dbg_fwalk"), w("cs_dbg_fcull"), w("cs_dbg_fpoly")
        fo, fb, fto = w("cs_dbg_fout"), w("cs_dbg_fbox"), w("cs_dbg_ftr")
        drawn = max(fp - fo - fb, 1)
        print("  objects in the frame: %d" % w("cs_nvisn"))
        print("  faces/frame: %.1f walked, %.1f back-culled (%.0f%%), %.1f to "
              "cs_poly, of which %.1f off-view and %.1f boxed (1-2 rows)"
              % (fw / N, fc / N, 100.0 * fc / max(fw, 1), fp / N, fo / N, fb / N))
        print("  %.1f objects drew faces, %.1f faces traced -> %.2f faces an "
              "object" % (fto / N, drawn / N, drawn / max(fto, 1)))
        print("  edges/frame: %.1f traces, %.1f duplicate (%.1f%%), %.1f on cut "
              "faces (%.1f%%)" % (tr / N, dup / N, 100.0 * dup / max(tr, 1),
                                  cut / N, 100.0 * cut / max(tr, 1)))
        print("  rows/frame:  %.1f traced, %.1f duplicate (%.1f%%), %.1f rows a "
              "trace" % (row / N, drow / N, 100.0 * drow / max(row, 1),
                         row / max(tr, 1)))

        wsh, wsl = w("cs_dbg_wsh"), w("cs_dbg_wsl")
        wst, wvt = w("cs_dbg_wst"), w("cs_dbg_wvt")
        shpx, shby = w("cs_dbg_wshpx"), w("cs_dbg_wshby")
        stpx, vtpx = w("cs_dbg_wstpx"), w("cs_dbg_wvtpx")
        px = shpx + stpx + vtpx
        print("  1bpp line walk/frame: %.1f shallow per-pixel, %.1f shallow "
              "SLICED, %.1f steep, %.1f vertical"
              % (wsh / N, wsl / N, wst / N, wvt / N))
        print("    pixels plotted %.1f (shallow %.1f, steep %.1f, vertical "
              "%.1f)" % (px / N, shpx / N, stpx / N, vtpx / N))
        print("    the shallow ones land in %.1f DISTINCT BYTES, so %.1f plots "
              "(%.1f%% of all) could merge into one write"
              % (shby / N, (shpx - shby) / N,
                 100.0 * (shpx - shby) / max(px, 1)))
        slrow, slpx = w("cs_dbg_wslrow"), w("cs_dbg_wslpx")
        print("    the SLICED ones lay %.1f pixels in %.1f RUNS (%.1f px a run)"
              " <== a run is ~130 bytes whatever it lays"
              % (slpx / N, slrow / N, slpx / max(slrow, 1)))
        prow, ppx = w("cs_dbg_prow"), w("cs_dbg_pby")
        pby = w("cs_dbg_pby")
        px2 = w("cs_dbg_ppx")
        b2, b4, b8 = w("cs_dbg_pby2"), w("cs_dbg_pby4"), w("cs_dbg_pby8")
        print("  POLYGON FILL/frame: %.1f rows, %.1f pixels, %.1f BYTES "
              "(%.1f px a row, %.2f bytes a row)"
              % (prow / N, px2 / N, pby / N, px2 / max(prow, 1),
                 pby / max(prow, 1)))
        print("    rows spanning <=2 bytes %.1f (%.0f%%), <=4 %.1f (%.0f%%), "
              "<=8 %.1f (%.0f%%)  <== the SHAPE, which a mean hides"
              % (b2 / N, 100.0 * b2 / max(prow, 1), b4 / N,
                 100.0 * b4 / max(prow, 1), b8 / N, 100.0 * b8 / max(prow, 1)))
        mfr, mst = w("cs_dbg_mfr"), w("cs_dbg_mstab")
        print("  MATRIX unchanged in %d of %d frames (%.0f%%) <== a per-frame "
              "table pays from the SECOND stable frame (88.5.13.2)"
              % (mst, mfr, 100.0 * mst / max(mfr, 1)))
        pvo, pvoe = w("cs_dbg_pvobj"), w("cs_dbg_pvobje")
        pve, pvf = w("cs_dbg_pvedge"), w("cs_dbg_pvfree")
        print("  PROJECTED vertices %.1f a frame over %.1f objects; %.1f of "
              "them (%.0f%%) are in a model that DRAWS EDGES and so cannot be "
              "skipped whatever cs_axcull culls (%.1f of %.1f objects)"
              % ((pve + pvf) / N, pvo / N, pve / N,
                 100.0 * pve / max(pve + pvf, 1), pvoe / N, pvo / N))
        vc, vu = w("cs_dbg_vcand"), w("cs_dbg_vunused")
        print("    of the %.1f in an edge-free model, %.1f (%.0f%%) are wanted "
              "by NO face that survives cs_axcull <== the only ones a "
              "cull-before-project could skip (%.0f%% of ALL projected)"
              % (vc / N, vu / N, 100.0 * vu / max(vc, 1),
                 100.0 * vu / max(pve + pvf, 1)))
        fvn, fvx, fvz = w("cs_dbg_fvn"), w("cs_dbg_fvx"), w("cs_dbg_fvz")
        print("  FLAT vertices %.1f a frame; x matches the previous vertex's "
              "%.1f (%.0f%%), z %.1f (%.0f%%) <== three imuls each, already "
              "to hand" % (fvn / N, fvx / N, 100.0 * fvx / max(fvn, 1),
                           fvz / N, 100.0 * fvz / max(fvn, 1)))

        # --- the tick wait out, for the whole run (skiesperf.py's rule: a
        #     frame faster than a tick reads 55 ms and every arm reads it) ---
        lst = tempfile.mkstemp(prefix="skiescount_", suffix=".lst")
        os.close(lst[0])
        # ...and the PROBE TREE'S OWN cswidx.inc, exactly as probemap() above
        # says. This site read `build/`'s - the SHIPPED overlay address - and
        # got away with it until the probe build outgrew the gap under it and
        # `times` went negative. One of the two nasm calls in this file had
        # learned the lesson and the other had not.
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-DCSPROBE",
                        "-I", "apps/", "-I", "apps/skies/",
                        "-I", os.path.join(ROOT, PROBE) + os.sep,
                        "-o", lst[1] + ".bin",
                        "-l", lst[1], "apps/skies/skies.asm"], check=True)
        import re
        rx = re.compile(r"\s*\d+\s+([0-9A-F]{8})\s+([0-9A-F\[\]]+)\s+"
                        r"(?:<\d+>\s*)?(.*)$")
        lab = re.compile(r"\s*\d+\s+(?:[0-9A-F]{8}\s+(?:[0-9A-F()\-\[\]]+\s+)?)?"
                         r"(?:<\d+>\s*)?([A-Za-z_][A-Za-z0-9_]*):")
        scope, wait = False, None
        for L in open(lst[1]):
            mm = lab.match(L)
            if mm:
                scope = (mm.group(1) == "cs_steps")
            if not scope:
                continue
            mm = rx.match(L)
            if mm and mm.group(3).split(";")[0].rstrip().endswith(
                    "call OSAPI_FSX_WAIT"):
                wait = (int(mm.group(1), 16),
                        bytes.fromhex(mm.group(2).replace("[", "").replace("]", "")))
                break
        os.unlink(lst[1])
        os.unlink(lst[1] + ".bin")
        if wait is None:
            sys.exit("skiescount: no OSAPI_FSX_WAIT site in cs_steps")
        m.pause()
        m.write(lin + wait[0], b"\x90" * len(wait[1]))
        m.run()

        def frames(n):
            m.bp_exec(lin + render)
            m.run()
            m.wait_stop(20)
            c0 = m.status()["cycles"]
            out = []
            for _ in range(n):
                m.run()
                if m.wait_stop(20) is None:
                    sys.exit("skiescount: the frame never came")
                c1 = m.status()["cycles"]
                out.append((c1 - c0) / CPS * 1000.0)
                c0 = c1
            m.bp_exec()
            return sum(out) / len(out)

        spreads = []                    # every "nothing changed" difference

        def ab(name):
            """The arm off and on, twice each and INTERLEAVED, so drift
            cannot land on one of them - and the WITHIN-ARM spread of each
            pair is kept, because two readings of the SAME arm differing is
            the instrument telling you what it cannot resolve."""
            r = {}
            for arm in (0, 1, 0, 1):
                m.pause()
                m.write(lin + base + off(name), bytes([arm]))
                m.run()
                r.setdefault(arm, []).append(frames(a.frames))
            m.pause()
            m.write(lin + base + off(name), b"\x00")
            m.run()
            spreads.append(abs(r[0][0] - r[0][1]))
            spreads.append(abs(r[1][0] - r[1][1]))
            return sum(r[0]) / 2.0, sum(r[1]) / 2.0

        def nullab():
            """THE INSTRUMENT'S OWN RESOLUTION: four groups of frames with
            NOTHING poked between them, split as if they were two arms. What
            comes back is what this machine, this scene and this frame count
            report as a difference when there is no difference."""
            r = {0: [], 1: []}
            for arm in (0, 1, 0, 1):
                m.pause()
                m.run()
                r[arm].append(frames(a.frames))
            spreads.append(abs(r[0][0] - r[0][1]))
            spreads.append(abs(r[1][0] - r[1][1]))
            return abs(sum(r[1]) / 2.0 - sum(r[0]) / 2.0)

        def res():
            """The largest difference seen where there was none to see."""
            return max([RES0] + spreads)

        def price(dlt, cnt, added=True):
            """A PER-UNIT price, or a sentence saying why there is not one.

            **An A/B is a WHOLE-FRAME difference divided by a PER-FRAME
            COUNT**, so its per-unit error is the frame resolution over that
            count - and with a handful of anything a frame, that error is
            bigger than the answer. This file used to divide anyway and print
            the result with no warning at all: on `slightbank` at 12 degrees
            it read a dedup TEST at **-283 cycles an edge**, a winding cross
            at **-1031**, and ONE `or [es:di], al` at **2819.3** against a
            true cost near 13. Every one of those is impossible, and every one
            looked like a measurement. The counts are in the hundreds on the
            scenes this tool was written for (`city`, the `df*` three) and in
            single figures on the ones `--fly` and `--roll` opened up.

            A NEGATIVE delta for an ADDED term is the loudest case and is
            reported as such: the term cannot have made the machine faster.
            """
            R = res()
            if cnt < 0.5:
                return "no count to divide by"
            err = R / cnt / 1000.0 * CPS
            if added and dlt < 0:
                return ("NOT RESOLVED (%.2f ms FASTER with the term added, "
                        "which it cannot be; %.1f a frame over a %.2f ms "
                        "resolution is +/-%.0f cycles)" % (-dlt, cnt, R, err))
            if abs(dlt) < 2 * R:
                return ("NOT RESOLVED (%.2f ms against a %.2f ms resolution; "
                        "%.1f a frame makes that +/-%.0f cycles)"
                        % (dlt, R, cnt, err))
            return ("%.0f +/- %.0f cycles"
                    % (dlt / cnt / 1000.0 * CPS, err))

        def solid(dlt, cnt, added=True):
            return not price(dlt, cnt, added).startswith(("NOT RESOLVED", "no "))

        # --- AND --fly CANNOT BE PRICED, only counted -----------------------
        # Every A/B below is a WHOLE-FRAME difference between two arms, so it
        # needs both arms to see the same scene - and --fly is the world
        # moving between them. Measured on `slightbank` at 12 degrees flying:
        # the resolution is **26 to 43 ms on a 217 ms frame**, so nothing
        # resolves and the section costs a quarter of an hour to say so.
        # Pinned, the same scene's terms come back at +/-16 to +/-111 cycles.
        # THE TWO MODES ANSWER DIFFERENT QUESTIONS: --fly is for a POPULATION
        # (what does the machine actually draw), pinned is for a PRICE (what
        # does one of them cost), and 7.1.15.1 is the same lesson from the
        # other side - a paused census is not the flying one.
        if a.fly and not a.price:
            print("  --- the per-term A/Bs are SKIPPED under --fly ---")
            print("  An A/B needs both arms to see the SAME SCENE and --fly is "
                  "the world moving between them: measured, the resolution "
                  "goes to 26-43 ms on a 217 ms frame, so no term resolves "
                  "and finding that out costs ~15 minutes. Run WITHOUT --fly "
                  "for a price (pinned, the same terms read +/-16 to +/-111 "
                  "cycles) and WITH it for a population. `--price` takes them "
                  "anyway.")
            return

        RES0 = nullab()
        base_ms, twice = ab("cs_dbl")
        per = (twice - base_ms) / max(tr / N, 1)
        print("  frame: %.2f ms (%.2f fps), mean of %d exact frames, the tick "
              "wait patched out" % (base_ms, 1000 / base_ms, a.frames))
        print("  RESOLUTION: %.2f ms - the largest difference this scene "
              "reports where NOTHING changed (null A/B %.2f, worst within-arm "
              "spread %.2f). A term smaller than twice it has no per-unit "
              "price here, however confidently one could be divided out"
              % (res(), RES0, max(spreads) if spreads else 0.0))
        print("  ONE cs_edge trace = %s; ALL %d of them = %.2f ms (%.1f%%)"
              % (price(twice - base_ms, tr / N), round(tr / N), per * tr / N,
                 100 * per * (tr / N) / base_ms))
        print("    the %.1f DUPLICATES = %.2f ms (%.2f%%) <== the whole prize"
              % (dup / N, per * dup / N, 100 * per * (dup / N) / base_ms))
        t0, t1 = ab("cs_nomark")         # arm 1 SKIPS the test, so t1 < t0
        tst = (t0 - t1) / max(tr / N, 1)
        print("    the dedup TEST = %s an edge, %.2f ms a frame (paid on every "
              "edge)" % (price(t0 - t1, tr / N), tst * tr / N))
        c0_, c1_ = ab("cs_cpy")
        cpe = (c1_ - c0_) / max(dup / N, 1)
        print("    the COPY that replaces a skipped trace = %s a duplicate, "
              "%.2f ms a frame" % (price(c1_ - c0_, dup / N), cpe * dup / N))
        save = per * dup / N
        net = save - tst * tr / N - cpe * dup / N
        stat = 60.0 / CPS * 1000        # a precomputed flag: a byte read + a branch
        netp = save - stat * tr / N - cpe * dup / N
        # THE NET IS ONLY AS GOOD AS ITS WORST TERM, and it is three A/Bs
        # deep - so it is not printed at all when one of them did not resolve.
        # A net assembled from unresolved parts is the most confident-looking
        # number in the file and the least true.
        parts = ((twice - base_ms, tr / N, "the trace"),
                 (t0 - t1, tr / N, "the dedup test"),
                 (c1_ - c0_, dup / N, "the copy"))
        weak = [w for d, c, w in parts if not solid(d, c)]
        if weak:
            print("    ==> NET: NOT REPORTED - %s did not resolve, and a net "
                  "of three A/Bs is only as good as its worst term"
                  % " and ".join(weak))
        else:
            print("    ==> NET at runtime: save %.2f, test %.2f, copy %.2f = "
                  "%+.2f ms (%+.2f%%)"
                  % (save, tst * tr / N, cpe * dup / N, net,
                     100 * net / base_ms))
            print("    ==> NET with the topology PRECOMPUTED per model (~60 "
                  "cycles a flag): %+.2f ms (%+.2f%%)"
                  % (netp, 100 * netp / base_ms))
        g0, g1 = ab("cs_dupgath")
        a0_, a1_ = ab("cs_duparea")
        f0, f1 = ab("cs_dupface")
        fpre = (f1 - f0) / max(fw / N, 1)
        print("  a face's PREAMBLE (its gather by index + the winding cross) = "
              "%s, %.2f ms a frame over %.1f walked"
              % (price(f1 - f0, fw / N), fpre * fw / N, fw / N))
        gth = (g1 - g0) / max(fw / N, 1)
        are = (a1_ - a0_) / max(fw / N, 1)
        print("      of which the GATHER %s and the CROSS %s"
              % (price(g1 - g0, fw / N), price(a1_ - a0_, fw / N)))
        if solid(f1 - f0, fw / N):
            print("    the %.1f BACK-CULLED faces = %.2f ms (%.2f%%) <== what "
                  "an earlier cull could reach"
                  % (fc / N, fpre * fc / N, 100 * fpre * (fc / N) / base_ms))
        else:
            print("    the %.1f BACK-CULLED faces: NOT REPORTED - the preamble "
                  "it is priced from did not resolve" % (fc / N))
        if solid(g1 - g0, fw / N):
            print("      ...of which reordering alone - the CROSS off the "
                  "indices BEFORE the gather - reaches %.2f ms (%.2f%%)"
                  % (gth * fc / N, 100 * gth * (fc / N) / base_ms))
        if px:
            p0, p1 = ab("cs_dblplot")
            ppl = (p1 - p0) / max(px / N, 1)
            print("  --- THE 1bpp PLOT ---")
            print("  ONE `or [es:di],al` plot = %s; ALL %.0f of them = %.2f ms "
                  "(%.1f%%)"
                  % (price(p1 - p0, px / N), px / N, ppl * px / N,
                     100 * ppl * (px / N) / base_ms))
            if solid(p1 - p0, px / N):
                print("    the %.1f MERGEABLE plots = %.2f ms (%.2f%%) <== the "
                      "ceiling of a byte accumulator"
                      % ((shpx - shby) / N, ppl * (shpx - shby) / N,
                         100 * ppl * ((shpx - shby) / N) / base_ms))
            else:
                print("    the %.1f MERGEABLE plots: NOT REPORTED - the plot "
                      "it is priced from did not resolve"
                      % ((shpx - shby) / N))


if __name__ == "__main__":
    main(sys.argv[1:])

#!/usr/bin/env python3
"""A LINE OF THE PREVIOUS HORIZON MUST NOT SURVIVE (SPEC.md 88.3.1.1.3).

    python3 tests/skiesstale.py [--machine os8088_5150_herc_gla] [--clobber]

docs/FIELD-NOTES.md 40, reported from play: banking one way left *"a blank
line in the ground"*, banking the other *"a filled line in the sky"*, both
carried from where the horizon was on the previous frame, and both sticking
around until something drew over them.

**THE ASSERTION NEEDS NO MODEL OF THE BLIT.** After `cs_blit` RETURNS the card
must equal the shadow over the whole view, byte for byte - the blit's one job
is to make that true. Anything else is wrong on the glass by definition, so
this row cannot be fooled by a host-side reconstruction of the union rule
disagreeing with the real one at the edges, which is what happened to the
first instrument (§88.3.2.2) and briefly read as a confirmation of this bug.

**WHY IT ROLLS THROUGH ZERO.** A band row's span is the crossing's byte and
one either side, on the argument that the rest of the row is what it was.
When the roll changes SIGN the two sides exchange - the fill lays the whole
row mirrored about a crossing that has barely moved - and that argument fails
over the row's whole width. Every other row is repaired by `cs_hzrows`' kind
arm as the band sweeps past it. The CENTRE row is the one the band never
leaves, which is why the field saw exactly one line, and why this row drives
the bank from +16 degrees to -48 rather than holding one.

`--clobber` keeps `cs_hzsides` equal to `cs_hzl` at the compare, which is the
old behaviour exactly, and it must go RED.
"""
import argparse
import importlib.util
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import dispapps                                             # noqa: E402
import skies as skiestest                                   # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WH = 112                                # the Hercules view (cs_vptab)
START, STEP, FRAMES = 16.0, -4.0, 17    # +16 -> -48, through zero at frame 4


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--nostep", action="store_true",
                    help="poke cs_mknostep: a thin diagonal's mark goes back "
                         "on its BOX (SPEC.md 88.3.2.2's A/B)")
    ap.add_argument("--clobber", action="store_true",
                    help="hold cs_hzsides at cs_hzl, which is the behaviour "
                         "before 88.3.1.1.3 - this row must then FAIL")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    spec = importlib.util.spec_from_file_location("sp", "tests/skiesprof.py")
    sp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sp)
    MP = dispapps._map("skies")
    P = sp.PROFILES["slightbank"]
    find = sp.sites()
    mtx = find("cs_render", r"call cs_matrix$")[0][0]
    bltr = find("cs_r_end", r"call cs_blit$")[0][1]
    side = find("cs_skyground", r"mov dx, \[cs_hzsides\]")
    side = side or find("cs_skyground", r"cmp dx, \[cs_hzsides\]")
    if a.clobber and not side:
        sys.exit("skiesstale: no cs_hzsides compare to clobber - the fix is "
                 "not in this build")
    print("  bank %+.0f -> %+.0f degrees, %d frames, %s"
          % (START, START + STEP * FRAMES, FRAMES,
             "CLOBBERED (must fail)" if a.clobber else "as shipped"))
    bad, seen = [], [0]
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        slot, seg, base = skiestest.open_game(m)
        lin = seg << 4
        off = lambda n: MP[n] - MP["os88_image_end"]           # noqa: E731
        goff = lambda x: base + x - MP["os88_image_end"]       # noqa: E731
        rw = lambda n: int.from_bytes(                         # noqa: E731
            m.readseg(seg, base + off(n), 2), "little")
        poke = lambda n, d: m.write(lin + base + off(n), d)    # noqa: E731
        m.type_text("f")
        m.advance(frames=30)
        m.run()
        m.pause()
        frames = [0]

        def on_hit(mm, rec):
            addr = rec["addr"] - lin
            if side and addr == side[0][0]:
                # the A/B: last frame's pair, made equal to this frame's
                mm.write(lin + base + off("cs_hzsides"),
                         mm.readseg(seg, base + off("cs_hzl"), 2))
                return True
            if addr == mtx:
                if frames[0] == 0:
                    for n, v in zip(("cs_px", "cs_py", "cs_pz"), P["pos"]):
                        poke(n, ((int(v * 256)) & 0xFFFFFFFF)
                             .to_bytes(4, "little"))
                    poke("cs_hdg", ((P["hdg"] * 65536 // 360) & 0xFFFF)
                         .to_bytes(2, "little"))
                    poke("cs_spd", (P.get("spd", 40) * 128)
                         .to_bytes(2, "little"))
                    poke("cs_state", b"\x01")
                    poke("cs_thr", (100).to_bytes(2, "little"))
                if a.nostep:
                    poke("cs_mknostep", b"\x01")
                r = START + STEP * frames[0]
                poke("cs_roll", (int(r * 65536 / 360) & 0xFFFF)
                     .to_bytes(2, "little"))
                frames[0] += 1
                return True
            if addr != bltr or frames[0] < 2:
                return True
            seen[0] += 1
            wb0 = mm.readseg(seg, base + off("cs_wb0"), 1)[0]
            wbn = mm.readseg(seg, base + off("cs_wbn"), 1)[0]
            dev = mm.readseg(seg, base + off("cs_devoff"), WH * 2)
            kind = mm.readseg(seg, base + off("cs_rowkind"), WH)
            cur = mm.readseg(seg, goff(rw("cs_spcur")), WH * 2)
            sh = mm.readseg(rw("cs_shseg"), rw("cs_tbase"), 80 * WH)
            vr = mm.readseg(rw("cs_fsi"), 0, 0x8000)
            for y in range(WH):
                d0 = int.from_bytes(dev[y * 2:y * 2 + 2], "little")
                cols = [wb0 + b for b in range(wbn)
                        if sh[80 * y + wb0 + b] != vr[d0 + wb0 + b]]
                if cols:
                    bad.append((round(START + STEP * (frames[0] - 1), 1), y,
                                kind[y], (cur[y * 2], cur[y * 2 + 1]), cols))
            return True

        m.run()
        targets = [lin + mtx, lin + bltr] + ([lin + side[0][0]]
                                             if (a.clobber and side) else [])
        with os88marty.bp_trace(m, *targets, poll=0.0008, cap=40000,
                                on_hit=on_hit):
            os88marty.until(m, lambda _: seen[0] >= FRAMES - 2,
                            "%d blitted frames" % (FRAMES - 2),
                            poll=0.4, limit=900.0, guest=1800.0)
        m.pause()
    for roll, y, k, span, cols in bad[:8]:
        print("    roll %+6.1f row %3d kind %d span %s  STALE bytes %s"
              % (roll, y, k, span, cols[:10]))
    if len(bad) > 8:
        print("    ...%d more rows" % (len(bad) - 8))
    print("  %d frames checked, %d row(s) where the card != the shadow"
          % (seen[0], len(bad)))
    if a.clobber:
        ok = bool(bad)
        print("  %s: the clobbered build %s"
              % ("ok" if ok else "FAIL",
                 "leaves stale rows, as it must" if ok
                 else "left NONE - this row proves nothing"))
    else:
        ok = not bad
        print("  %s: %s" % ("ok" if ok else "FAIL",
                            "the blit left the card equal to the shadow on "
                            "every frame" if ok
                            else "a previous horizon survived on the glass "
                                 "(docs/FIELD-NOTES.md 40)"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

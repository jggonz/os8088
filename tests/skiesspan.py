#!/usr/bin/env python3
"""A STORED SPAN MUST NAME A BYTE OF THE VIEW.

`cs_hzrows`'s erase arm and `cs_blit` both take the span at its word: the
first refills `[lo, hi]` as ABSOLUTE bytes of the row, the second copies the
same range to the card.  Neither clamps.  So a pair naming a byte outside
`[cs_wb0, cs_wb0 + cs_wbn - 1]` is a write into the box border - or, when
the low byte borrows past zero, an unsigned 252 that lands a quarter of the
way into a row three lines down.

The check is one line and needs no picture: read `cs_spcur` after the frame
is drawn and before the blit, and compare every non-empty pair against the
view's own byte range.

**THE SIZE IS THE POINT.**  `cs_wx0` is `((vw - ww) / 2) & 0xF0`, so at
`CSZ_FULL` the view is the whole box on all four backends and `cs_wb0` is
ZERO - there is no margin for `CS_MKD_SLOP` to widen into.  At MODERATE
there are 11 to 15 bytes of it.  So a run at one size says nothing about
the other, and `SIZE=full` is the arm that matters.

    SIZE=full PROF=rollsweep python3 tests/skiesspan.py 12 2 40
    SIZE=mod  PROF=bank      python3 tests/skiesspan.py 12 2 40
"""
import os, sys, importlib.util
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # this tree, wherever it is checked out
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)
import os88marty, dispapps, skies as skiestest
spec = importlib.util.spec_from_file_location("sp", "tests/skiesprof.py")
sp = importlib.util.module_from_spec(spec); spec.loader.exec_module(sp)

MP = dispapps._map("skies")
ROLL0 = float(sys.argv[1]) if len(sys.argv) > 1 else 6.0
STEP = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
NPT = int(sys.argv[3]) if len(sys.argv) > 3 else 40
PROFS = os.environ.get("PROF", "slightbank,rollsweep").split(",")
SIZE = os.environ.get("SIZE", "full")
NOSTEP = int(os.environ.get("NOSTEP", "0"))
WARM = 4
WHMAX = 152
find = sp.sites()
mtx = find("cs_render", r"call cs_matrix$")[0][0]
blt = find("cs_r_end", r"call cs_blit$")[0][0]      # BEFORE the blit


def run():
    """Every profile in ONE guest: the invariant is single-frame, so nothing
    a previous profile left behind can flatter or fail the next."""
    out = []
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine="os8088_5150_herc_gla") as m:
        slot, seg, base = skiestest.open_game(m); lin = seg << 4
        off = lambda n: MP[n] - MP["os88_image_end"]
        poke = lambda n, d: m.write(lin + base + off(n), d)
        rw = lambda n: int.from_bytes(m.readseg(seg, base + off(n), 2), "little")
        # THE SIZE IS SET BEFORE THE BRACKET OPENS, not with the `+` key
        # in flight: `+` runs cs_r_setup from a key handler and a read after
        # it comes back INCONSISTENT - wx0 from one geometry and wb0 from the
        # other - which is the shape of a half-applied change and reads
        # exactly like this test's own defect. Poked here, `f` enters the
        # bracket once and cs_r_setup computes the whole geometry from it.
        poke("cs_setsize", bytes([{"small": 0, "mod": 1, "full": 2}[SIZE]]))
        m.type_text("f"); m.advance(frames=30)
        m.run(); m.pause()
        wb0, wbn = rw("cs_wb0"), rw("cs_wbn")   # noqa: E126
        wh = rw("cs_wh")
        wx0, ww = rw("cs_wx0"), rw("cs_ww")
        # ...and the geometry must agree with ITSELF before anything is read
        # off it: wbn bytes hold ww pixels, so wb0 of those bytes hold wx0.
        # A half-applied size change satisfies neither and would be reported
        # as this test's own defect.
        if not (0 < wbn <= 80 and 0 < wh <= 200 and ww == 0 + wbn * (ww // wbn)
                and wx0 == wb0 * (ww // wbn) and wb0 + wbn <= 80):
            sys.exit("skiesspan: the view geometry does not agree with "
                     "itself - wx0 %d ww %d wh %d wb0 %d wbn %d. Nothing "
                     "below can be trusted" % (wx0, ww, wh, wb0, wbn))
        print("     geometry: SIZE=%s  wx0 %d ww %d wh %d -> view bytes "
              "[%d,%d] of the row's 80"
              % (SIZE, wx0, ww, wh, wb0, wb0 + wbn - 1))
        ap_ = rw("cs_airport")
        objs = int.from_bytes(m.read(lin + ap_ + 18, 2), "little")
        nobj = int.from_bytes(m.read(lin + ap_ + 20, 2), "little")
        for o in range(objs, objs + nobj * 20, 20):
            m.write(lin + o + 18, b"\x00\x00")
        for prof in PROFS:
            P = dict(sp.PROFILES[prof])
            for nm, v in zip(("cs_px", "cs_py", "cs_pz"), P["pos"]):
                poke(nm, ((int(v * 256)) & 0xFFFFFFFF).to_bytes(4, "little"))
            poke("cs_hdg", ((P["hdg"] * 65536 // 360) & 0xFFFF)
                 .to_bytes(2, "little"))
            poke("cs_spd", (P.get("spd", 40) * 128).to_bytes(2, "little"))
            poke("cs_state", b"\x01")
            poke("cs_pitch", ((int(P["pitch"] * 65536 / 360)) & 0xFFFF)
                 .to_bytes(2, "little"))
            poke("cs_thr", P["thr"].to_bytes(2, "little"))
            poke("cs_mknostep", bytes([NOSTEP]))
            n = [0]; shots = []

            def on_hit(mm, rec, n=n, shots=shots):
                a = rec["addr"] - lin
                if a == mtx:
                    i = n[0] - WARM
                    r = ROLL0 + STEP * (i if i > 0 else 0)
                    poke("cs_roll", ((int(r * 65536 / 360)) & 0xFFFF)
                         .to_bytes(2, "little"))
                    n[0] += 1
                    return True
                if a == blt and n[0] > WARM and len(shots) < NPT:
                    shots.append((mm.readseg(seg, base + rw("cs_spcur")
                                             - MP["os88_image_end"], wh * 2),
                                  mm.readseg(rw("cs_shseg"), rw("cs_tbase"),
                                             80 * wh)))
                return True
            with os88marty.bp_trace(m, lin + mtx, lin + blt, poll=0.0008,
                                    cap=60000, on_hit=on_hit):
                os88marty.until(m, lambda _: len(shots) >= NPT,
                                "%s, %d frames" % (prof, WARM + NPT), poll=0.4,
                                limit=900.0, guest=3000.0)
            m.pause()
            out.append((prof, shots))
    return wb0, wbn, wh, out


wb0, wbn, wh, runs = run()
hi_lim = wb0 + wbn - 1
FAIL = 0
for PROF, s in runs:
    bad = tot = marg = 0
    worst = 0
    rows = set(); margrows = set()
    for f, (span, sh) in enumerate(s):
        for y in range(wh):                     # ...AND THE HARM ITSELF: the
            for c in list(range(0, wb0)) + list(range(hi_lim + 1, 80)):
                if sh[y * 80 + c]:              # box border beside the view,
                    marg += 1                   # which nothing in the frame
                    margrows.add(y)             # may write and cs_blit copies
        hits = []                               # to the card like any other
        for y in range(wh):
            lo, hi = span[y * 2], span[y * 2 + 1]
            if lo == 0xFF and hi == 0x00:       # the EMPTY sentinel
                continue
            if lo < wb0:
                hits.append((y, lo, hi, "lo", wb0 - lo))
            if hi > hi_lim:
                hits.append((y, lo, hi, "hi", hi - hi_lim))
        if not hits:
            continue
        bad += 1
        tot += len(hits)
        for y, lo, hi, side, by in hits:
            rows.add(y)
            worst = max(worst, by)
        if bad <= 2:
            print("     %s roll %5.2f: %d pair(s) OUTSIDE the view [%d,%d]"
                  % (PROF, ROLL0 + STEP * f, len(hits), wb0, hi_lim))
            for y, lo, hi, side, by in hits[:6]:
                print("        row %3d span [%3d,%3d]  %s by %d"
                      % (y, lo, hi, side, by))
    if bad or marg:
        FAIL += 1
    print("  %-11s [nostep=%d]: %s  %d of %d frames, %d pairs in %d rows, "
          "worst %d byte(s); BORDER ink %d bytes in %d rows"
          % (PROF, NOSTEP, "FAIL" if (bad or marg) else "clean", bad, len(s),
             tot, len(rows), worst, marg, len(margrows)))
sys.exit(1 if FAIL else 0)

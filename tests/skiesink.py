#!/usr/bin/env python3
"""INK OUTSIDE ITS ROW'S SPAN IS A STALE PIXEL WAITING TO HAPPEN.

The field reports ink from a road or a river surviving a frame it should
have been erased in - after banking, on the right of the view, until the
line crosses it again.

**The erase IS the span.** `cs_hzrows`'s `.r` arm, for a row whose KIND is
unchanged from last frame, refills exactly the bytes LAST frame's span
covers - *"what last frame drew on the row IS the erase"* (SPEC.md 88.3.1).
So ink laid outside its row's recorded span is never erased, and it sits
there until something marks that byte again. That is the report, exactly.

So this is not an A/B and needs no history: on any WHOLE-GROUND row
(`cs_rowkind` = 1), every byte that differs from that row's ground pattern
must lie inside `cs_spcur`'s pair for the row. A byte that does not is the
defect, found in the frame that CREATES it rather than the frame that shows
it.

Two earlier detectors were confounded and are worth not rebuilding: the
CARD-against-SHADOW one (tests/skiesstale.py) cannot see this at all,
because the shadow itself keeps the ink and the card faithfully matches it;
and an incremental-against-full-repaint one measures `cs_rowkind`'s own
refill rule rather than the marking, so it reports the FULL arm having MORE
ink - the opposite sign to the bug.

    FLY=1 PROF=bank python3 tests/skiesink.py 12 0 60
    FLY=1 PROF=rollsweep python3 tests/skiesink.py 12 0 60

PROF picks a tests/skiesprof.py profile, FLY=1 lets the world run (the
invariant is single-frame, so it can), NOSTEP=1 and NOSHORT=1 turn off
SPEC.md 88.3.2.3's stepped mark and 88.4.6.2's short-run body.

**IT FAILS TODAY** and that is what it is for: docs/FIELD-NOTES.md 41.
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
STEP = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
NPT = int(sys.argv[3]) if len(sys.argv) > 3 else 40
NOSTEP = int(os.environ.get("NOSTEP", "0"))
NOSHORT = int(os.environ.get("NOSHORT", "0"))
PROF = os.environ.get("PROF", "slightbank")
P = dict(sp.PROFILES[PROF])
FLY = int(os.environ.get("FLY", "0"))
WARM = 4
WB0, WBN, WH = 15, 50, 112
find = sp.sites()
mtx = find("cs_render", r"call cs_matrix$")[0][0]
blt = find("cs_r_end", r"call cs_blit$")[0][0]      # BEFORE the blit


def run():
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine="os8088_5150_herc_gla") as m:
        slot, seg, base = skiestest.open_game(m); lin = seg << 4
        off = lambda n: MP[n] - MP["os88_image_end"]
        poke = lambda n, d: m.write(lin + base + off(n), d)
        rw = lambda n: int.from_bytes(m.readseg(seg, base + off(n), 2), "little")
        m.type_text("f"); m.advance(frames=30); m.run(); m.pause()
        for n, v in zip(("cs_px", "cs_py", "cs_pz"), P["pos"]):
            poke(n, ((int(v * 256)) & 0xFFFFFFFF).to_bytes(4, "little"))
        poke("cs_hdg", ((P["hdg"] * 65536 // 360) & 0xFFFF).to_bytes(2, "little"))
        poke("cs_spd", (P.get("spd", 40) * 128).to_bytes(2, "little"))
        poke("cs_state", b"\x01")
        poke("cs_pitch", ((int(P["pitch"] * 65536 / 360)) & 0xFFFF)
             .to_bytes(2, "little"))
        poke("cs_thr", P["thr"].to_bytes(2, "little"))
        # THE INVARIANT IS SINGLE-FRAME, so it can be checked in FLIGHT -
        # which is where the field sees this and where a paused scene may
        # simply not contain the trigger. Nothing here is an A/B, so the
        # world moving costs the check nothing.
        if not FLY:
            poke("cs_pause", b"\x01")
        poke("cs_mknostep", bytes([NOSTEP]))
        poke("cs_slnoshort", bytes([NOSHORT]))
        ap_ = rw("cs_airport")
        objs = int.from_bytes(m.read(lin + ap_ + 18, 2), "little")
        nobj = int.from_bytes(m.read(lin + ap_ + 20, 2), "little")
        for o in range(objs, objs + nobj * 20, 20):
            m.write(lin + o + 18, b"\x00\x00")
        n = [0]; shots = []

        def on_hit(mm, rec):
            a = rec["addr"] - lin
            if a == mtx:
                i = n[0] - WARM
                r = ROLL0 + STEP * (i if i > 0 else 0)
                poke("cs_roll", ((int(r * 65536 / 360)) & 0xFFFF)
                     .to_bytes(2, "little"))
                n[0] += 1
                return True
            if a == blt and n[0] > WARM and len(shots) < NPT:
                shots.append((
                    mm.readseg(rw("cs_shseg"), rw("cs_tbase"), 80 * WH),
                    mm.readseg(seg, base + off("cs_rowkind"), WH),
                    mm.readseg(seg, base + off("cs_pat"), 4),
                    mm.readseg(seg, base + rw("cs_spcur")
                               - MP["os88_image_end"], WH * 2)))
            return True
        with os88marty.bp_trace(m, lin + mtx, lin + blt, poll=0.0008, cap=60000,
                                on_hit=on_hit):
            os88marty.until(m, lambda _: len(shots) >= NPT,
                            "%d frames" % (WARM + NPT), poll=0.4,
                            limit=900.0, guest=3000.0)
        m.pause()
    return shots


s = run()
bad = 0; tot = 0; sides = {"left": 0, "right": 0}; rowset = set()
mid = WB0 + WBN // 2
for f, (sh, kind, pat, span) in enumerate(s):
    roll = ROLL0 + STEP * f
    hits = []
    for y in range(WH):
        if kind[y] != 1:                        # whole-GROUND rows only
            continue
        lo, hi = span[y * 2], span[y * 2 + 1]
        # THE GROUND BYTE IS THE ROW'S OWN MODE, not a table. cs_pat is the
        # POLYGON's fill pattern (CS_POLYROW indexes it by row & 3), not the
        # ground's - reading it as the ground's flagged 36,365 bytes of
        # perfectly ordinary dither. The ground repeats with period 4 in y
        # and is uniform across x, so the commonest byte in the row IS it,
        # and ink is a minority by construction.
        # THE GROUND BYTE COMES FROM OUTSIDE THE SPAN, not from the whole
        # row. The row's own mode is the ground only while ink is a MINORITY,
        # and a solid polygon covering more than half a row makes the mode the
        # INK - after which every ground byte reads as a leak. Row 110 of
        # `rollsweep` did exactly that: mode FF, 54 dither bytes flagged.
        # Bytes outside the span were not written this frame by construction,
        # so they ARE the ground; a row with too few of them cannot be judged.
        out = [sh[y * 80 + c] for c in range(WB0, WB0 + WBN)
               if not (lo <= c <= hi)]
        if len(out) < 8:
            continue
        p = max(set(out), key=out.count)
        for c in range(WB0, WB0 + WBN):
            if sh[y * 80 + c] == p:
                continue
            if lo <= c <= hi:                   # inside the span: erased
                continue
            hits.append((y, c, sh[y * 80 + c], lo, hi))
    if not hits:
        continue
    bad += 1; tot += len(hits)
    for y, c, b, lo, hi in hits:
        rowset.add(y)
        sides["right" if c >= mid else "left"] += 1
    if bad <= 3:
        print("     roll %5.2f: %d bytes of INK OUTSIDE THEIR ROW'S SPAN"
              % (roll, len(hits)))
        for y, c, b, lo, hi in hits[:6]:
            print("        row %3d byte %2d = %02X, span [%d,%d]  (%s of it)"
                  % (y, c, b, lo, hi, "right" if c > hi else "left"))
            l0 = max(WB0, min(c, hi) - 4); l1 = min(WB0 + WBN - 1, c + 4)
            for yy in (y - 1, y, y + 1):
                if not (0 <= yy < WH):
                    continue
                row = sh[yy * 80 + WB0:yy * 80 + WB0 + WBN]
                gp = max(set(row), key=row.count)
                sl, sh_ = span[yy * 2], span[yy * 2 + 1]
                px = "".join("".join("#" if sh[yy * 80 + cc] & (0x80 >> i)
                                     else "." for i in range(8))
                             for cc in range(l0, l1 + 1))
                bar = "".join(("|" if (sl <= cc <= sh_) else " ") * 8
                              for cc in range(l0, l1 + 1))
                print("          r%-3d k%d gnd %02X span[%3d,%3d] %s"
                      % (yy, kind[yy], gp, sl, sh_, px))
                print("               %s              %s"
                      % (" " * 14, bar))
print("  %s%s [nostep=%d noshort=%d]: %s  %d of %d frames, %d bytes "
      "in %d rows (%d LEFT of span, %d RIGHT of span)"
      % (PROF, " FLYING" if FLY else " pinned", NOSTEP, NOSHORT,
         "FAIL" if bad else "clean", bad, len(s), tot, len(rowset),
         sides["left"], sides["right"]))
sys.exit(1 if bad else 0)

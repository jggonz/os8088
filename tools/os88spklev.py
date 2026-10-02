#!/usr/bin/env python3
"""Replay a speaker capture through the model, and compare levellers.

    python3 tools/os88spklev.py CAP.pkl [CAP.pkl ...] [--fixed 3,5,7]
    python3 tools/os88spklev.py --carrier CAP.pkl     (the whine, by level)

The other half of docs/plans/SPEAKER-LEVELLER-NEXT.md section 3's
instrument. A capture is tools/os88spkcap.py's pickle: every span Tracker's
shaper was handed on the machine, and what it wrote.

First it checks the MODEL still is the machine: tools/os88spkfx.py's Shaper,
seeded from the state the machine had after the capture's first span (the
capture joins the song mid-way, so that one span's state is unknowable), is
run over every later span and must write the same counts the machine did. A
capture from a levelled play (rat 0) settles after the hold's few spans, and
those are not counted. If this line says anything but EXACT, the table below
it is about a model and not about the machine.

Then the table: each variant run over the captured inputs, with the metrics
the leveller rounds were judged on -

  wander   the level's spread within each second, in dB, averaged (how much
           the parts that should not change, change)
  mean     the mean gain, in dB (loudness)
  clip     the share of samples at the soft clip's end (what the hits cost)
  travel   the level's movement, dB a second
  V-dips   spans 4 dB or more below the level on both sides (a "hitch")

Silent spans (a peak under PGATE) are left out of wander and mean. A variant
is a subclass of Shaper run the same way - add one to VARIANTS - so a
candidate is judged here, on the owner's modules, before any assembly.
"""
import argparse
import os
import pickle
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import os88spkfx as F                                # noqa: E402


def tsp_const(name):
    """a Tracker constant out of apps/tracker/trkspk.inc, not restated"""
    for ln in open(os.path.join(ROOT, "apps/tracker/trkspk.inc")):
        f = ln.split()
        if len(f) >= 3 and f[0] == name and f[1] == "equ":
            return int(f[2], 0)
    raise SystemExit("%s not found in apps/tracker/trkspk.inc" % name)


def exact(c):
    """(spans compared, spans that differ) - the model against the machine"""
    recs, rate = c["recs"], c["rate"]
    sh = F.Shaper(rate, pre=F.PRE_NONE, idle=True)
    tot0, src0, out0, lev, d, h = recs[0]
    sh.lev, sh.d, sh.h, sh.rat = lev, d, h, c.get("rat", 0)
    sh.prev, sh.gate = src0[-1], False
    skip = 0 if sh.rat else F.HOLD + 1
    n = bad = 0
    for k, (tot, src, out, lev, d, h) in enumerate(recs[1:]):
        xs = list(src)
        sh.level(xs)
        got = sh.emit(xs)
        if k < skip:
            continue
        n += 1
        bad += got != out
    return n, bad


def metrics(c, sh):
    """run one variant over a capture; its row of the table"""
    rate, span = c["rate"], c["span"]
    gf = F.gains()
    levs, loud = [], []
    clip = tot = 0
    for rec in c["recs"]:
        xs = list(rec[1])
        p = sh.level(xs)
        sh.emit(xs)
        g = gf[sh.lev]
        for x in xs:
            tot += 1
            if (abs(x - 128) * g) >> 8 >= 128:
                clip += 1
        levs.append(sh.lev)
        loud.append(p >= F.PGATE)
    secs = len(levs) * span / float(rate)
    per = max(1, int(rate / span))                  # spans a second
    db = [v * F.LEV_DB for v in levs]
    ws = []
    for i in range(0, len(db) - per + 1, per):
        w = [db[j] for j in range(i, i + per) if loud[j]]
        if len(w) > 1:
            m = sum(w) / len(w)
            ws.append((sum((v - m) ** 2 for v in w) / len(w)) ** 0.5)
    ld = [v for v, l_ in zip(db, loud) if l_]
    travel = sum(abs(db[i] - db[i - 1]) for i in range(1, len(db))) / secs
    vd = sum(1 for i in range(1, len(levs) - 3)
             if levs[i] <= levs[i - 1] - 2 and
             levs[i] <= max(levs[i + 1:i + 4]) - 2)
    return (sum(ws) / len(ws) if ws else 0.0,
            sum(ld) / len(ld) if ld else 0.0,
            100.0 * clip / max(1, tot), travel, vd)


def carrier(c, sh, win_ms=20.0):
    """--carrier: what the pulse train puts at the PULSE RATE, the whine.
    A pulse high for a fraction D of its period puts (1 - e^-i2piD) / i2pi
    at the rate's own frequency: loudest at D = 1/2, nothing at either end.
    Averaged over a window that coherent part is the steady TONE; the rest
    moves with the music and is not heard as a tone. MUSIC is D's spread in
    the same windows, the sound itself. dB against a steady 50% pulse (tone,
    carrier) and a full-scale sine (music)"""
    import cmath
    import math
    n = F.spk_n(c["rate"])
    per = max(8, int(c["rate"] * win_ms / 1000.0))
    ref = (1.0 / math.pi) ** 2
    zs, ds = [], []
    for rec in c["recs"]:
        xs = list(rec[1])
        sh.level(xs)
        for cnt in sh.emit(xs):
            d = cnt / float(n)
            ds.append(d)
            zs.append((1 - cmath.exp(-2j * math.pi * d)) / (2j * math.pi))
    tone = tot = mus = 0.0
    nw = 0
    for i in range(0, len(zs) - per + 1, per):
        w, dw = zs[i:i + per], ds[i:i + per]
        m = sum(w) / per
        tone += abs(m) ** 2
        tot += sum(abs(z) ** 2 for z in w) / per
        md = sum(dw) / per
        mus += sum((v - md) ** 2 for v in dw) / per
        nw += 1
    db = lambda v, r: 10.0 * math.log10(max(v / nw, 1e-12) / r)
    return (db(tone, ref), db(tot, ref), db(mus, 0.125),
            100.0 * sum(ds) / len(ds))


def shipped(rate):
    """Tracker today: one level a song, the ratchet from TSP_LSTART"""
    sh = F.Shaper(rate, pre=F.PRE_NONE, idle=True)
    sh.ratchet(tsp_const("TSP_LSTART"))
    return sh


def leveller(rate):
    """the per-span leveller the ratchet replaced (TSP_RATCHET=0)"""
    return F.Shaper(rate, pre=F.PRE_NONE, idle=True)


def fixed(n):
    def make(rate):
        sh = F.Shaper(rate, pre=F.PRE_NONE, idle=True)
        sh.freeze(n)
        return sh
    return make


VARIANTS = [("shipped: ratchet from %d" % tsp_const("TSP_LSTART"), shipped),
            ("per-span leveller", leveller)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("caps", nargs="+")
    ap.add_argument("--carrier", action="store_true",
                    help="the whine instead of the leveller table: the tone "
                         "at the pulse rate, against the music, for the "
                         "ratchet and every fixed level")
    ap.add_argument("--fixed", default="",
                    help="comma-separated levels to add as frozen variants "
                         "(what the volume bar sets, SPEC.md 45.25.3)")
    a = ap.parse_args()
    variants = list(VARIANTS)
    for v in filter(None, a.fixed.split(",")):
        variants.append(("fixed level %d" % int(v), fixed(int(v))))
    for path in a.caps:
        with open(path, "rb") as f:
            c = pickle.load(f)
        secs = len(c["recs"]) * c["span"] / float(c["rate"])
        print("%s: %s, %.1f s at %d Hz, %d dry grants, captured rat %d"
              % (path, c.get("mod", "?"), secs, c["rate"], c.get("dry", 0),
                 c.get("rat", 0)))
        n, bad = exact(c)
        print("  the model against the machine: %s (%d spans)"
              % ("EXACT" if not bad else "%d DIFFER" % bad, n))
        if a.carrier:
            print("  %-26s %8s %8s %8s %9s %7s"
                  % ("variant", "tone", "carrier", "music", "music-tone",
                     "duty"))
            for name, make in [variants[0]] + [
                    ("fixed level %d" % v, fixed(v)) for v in range(F.NLEV)]:
                t_, cr, mu, du = carrier(c, make(c["rate"]))
                print("  %-26s %6.1fdB %6.1fdB %6.1fdB %7.1fdB %6.1f%%"
                      % (name, t_, cr, mu, mu - t_, du))
            continue
        print("  %-26s %8s %8s %7s %9s %7s"
              % ("variant", "wander", "mean", "clip", "travel", "V-dips"))
        for name, make in variants:
            w, m, cl, tr, vd = metrics(c, make(c["rate"]))
            print("  %-26s %6.2fdB %6.1fdB %6.2f%% %6.1fdB/s %7d"
                  % (name, w, m, cl, tr, vd))
    return 0


if __name__ == "__main__":
    sys.exit(main())

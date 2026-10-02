#!/usr/bin/env python3
"""Capture what Tracker's speaker shaper was handed and what it wrote.

    python3 tools/os88spkcap.py MODULE.MOD [--secs 40] [--out cap.pkl]
                                [--machine NAME] [--no-turbo]
                                [--pkg TRACKER.O88 --define NAME[=V] ...]

The instrument docs/plans/SPEAKER-LEVELLER-NEXT.md section 3 describes, kept
runnable. Boots MartyPC (by default the 7.16 MHz VGA XT with a fixed disk,
--turbo, which takes the 8,000 Hz rung), puts TRACKER.O88 and the module on
the disk, opens the module and lets it play on the speaker. A breakpoint
just after tsp_fill's last os88spkfx_emit (tsp_fill.one + 3, the
`add [es:TSP_RL], bx`) stops the machine once a span, and each stop records:

    (total, src, out, lev, d, h)

total   the ring's TOTAL before this span is counted in (so `out` is the
        span at TOTAL mod TSP_RL)
src     mp_outbuf - the mixed span, the shaper's input (already filtered at
        load, so the shaper runs PRE_NONE)
out     the span just written into the ring - the shaper's output, counts
lev,d,h os88spkfx_lev / _d / _h after the span: the level in force, the
        carrier's offset and its target

The pickle is a dict: rate, span, rat (os88spkfx_rat at the first stop: 0
levelled, 1 the ratchet, 2 frozen), recs, dry (the ring's dry grants over
the capture - a capture that starved measured the starvation) and mod.

tools/os88spklev.py replays it through tools/os88spkfx.py's Shaper, which
reproduces the machine exactly, so a leveller variant is a subclass run over
the captured inputs and only the winner is written in assembly.

BEVERLY.MOD is in the tree (apps/tracker/beverly.mod); ELYSIUM.MOD, the other
module every round was judged on, is the owner's and is not.
"""
import argparse
import os
import pickle
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88build, os88marty                          # noqa: E402
import trkspk                                        # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("module")
    ap.add_argument("--secs", type=float, default=40.0)
    ap.add_argument("--out", default=None,
                    help="the pickle (default: the module's name + .pkl, "
                         "beside it in build/)")
    ap.add_argument("--machine", default=trkspk.TURBO)
    ap.add_argument("--pkg", default=None,
                    help="a Tracker package other than build/tracker.o88 - a "
                         "listening build - with --define for each define "
                         "it was assembled with, so its symbols are read")
    ap.add_argument("--define", action="append", default=[])
    ap.add_argument("--no-turbo", action="store_true",
                    help="run the machine at its own clock (the rung it "
                         "picks is then that machine's)")
    a = ap.parse_args()
    os.chdir(ROOT)
    mod = os.path.basename(a.module).upper()
    out = a.out or os.path.join("build", os.path.splitext(mod)[0].lower()
                                + ".pkl")
    t = trkspk.Trk(a.machine,
                   [("TRACKER.O88", a.pkg or os88build.at("build/tracker.o88")),
                    (mod, os.path.abspath(a.module))],
                   mod, defines=tuple(a.define),
                   extra=[] if a.no_turbo else ["--turbo"])
    try:
        t.until(lambda: t.rb("tsp_open") == 1 or t.rb("tsp_force") == 1,
                "the play")
        if t.rb("tsp_open") != 1:
            t.m.type_text(" ")
            t.until(lambda: t.rb("tsp_open") == 1, "the play, again")
        rate, span = t.rw("tsp_rate"), t.rw("tsp_span")
        print("%s: %d Hz, %d a span, predicted %d%%"
              % (mod, rate, span, t.rw("tsp_pct")))
        at = t.a("tsp_fill.one") + 3
        rseg = t.rw("tsp_rseg") << 4
        rl = trkspk_rl()
        a_src, a_lev = t.a("mp_outbuf"), t.a("os88spkfx_lev")
        a_d, a_h = t.a("os88spkfx_d"), t.a("os88spkfx_h")
        a_rat = t.a("os88spkfx_rat")
        k_dry = t.a("os88spk_grant.dry")
        recs, dry, rat = [], [0], []

        def hit(mm, rec):
            if rec.get("addr") == k_dry:
                dry[0] += 1
                return
            if not rat:
                rat.append(mm.read(a_rat, 1)[0])
            tot = int.from_bytes(mm.read(rseg + rl, 2), "little")
            p = tot % rl
            if p + span <= rl:
                o = mm.read(rseg + p, span)
            else:
                o = mm.read(rseg + p, rl - p) + mm.read(rseg, span - (rl - p))
            recs.append((tot, bytes(mm.read(a_src, span)), bytes(o),
                         mm.read(a_lev, 1)[0], mm.read(a_d, 1)[0],
                         mm.read(a_h, 1)[0]))
        need = int(a.secs * rate / span)
        with os88marty.bp_trace(t.m, at, k_dry, on_hit=hit):
            t0 = time.time()
            while len(recs) < need and time.time() - t0 < 900:
                time.sleep(0.5)
        print("%d spans (%.1f s), %d dry grants, rat %d"
              % (len(recs), len(recs) * span / float(rate), dry[0],
                 rat[0] if rat else -1))
        with open(out, "wb") as f:
            pickle.dump(dict(rate=rate, span=span, rat=rat[0] if rat else 0,
                             recs=recs, dry=dry[0], mod=mod), f)
        print("-> " + out)
    finally:
        t.close()
    return 0


def trkspk_rl():
    """TSP_RL, the ring's length, out of the source rather than restated"""
    for ln in open(os.path.join(ROOT, "apps/tracker/trkspk.inc")):
        f = ln.split()
        if len(f) >= 3 and f[0] == "TSP_RL" and f[1] == "equ":
            return int(f[2], 0)
    raise SystemExit("TSP_RL not found in apps/tracker/trkspk.inc")


if __name__ == "__main__":
    sys.exit(main())

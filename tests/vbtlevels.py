#!/usr/bin/env python3
"""LEVELS: every sound as loud as the music (SPEC.md 125.3) - the owner's
"I'm having a hard time matching the music volume and the clip volumes".

    python3 tests/vbtlevels.py

On the demo battle (host only - the engine is unchanged, the bytes are the
question):

  - vbtana.levels measures every sound in use AS HEARD - the music and an
    effect at half scale, an opening's own sound at full - and suggests each
    a level that matches the music, never past its peak;
  - applied, every sound measures at the music's loudness or at its peak's
    limit, within half a dB;
  - and IN THE BUILT BUNDLE'S OWN BYTES the opening's sound (stored full
    scale) is now as loud as the music region (stored half scale): before,
    the riser played 5 dB over the music.

Broken on purpose (PLAY_SCALE's own at 0.5, as if an opening's sound were
stored like the music) the bundle's two loudnesses part again.
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import numpy as np          # noqa: E402
import os88vbt as B         # noqa: E402
import vbtana as A          # noqa: E402
import vbtproj as P         # noqa: E402

fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def bundle_loud(data, rate):
    """(the music region's loudness, opening 0's own sound's) in the
    bundle's bytes, each as the card plays it (full scale = 128)"""
    b = B.Bundle(data)
    mus = (np.frombuffer(b.mus, np.uint8).astype(float) - 128) / 128
    op = b.clips[b.openings[0]]
    snd = b"".join(r[len(r) - B.struct.unpack_from("<H", r, 2)[0]:]
                   for r in b.stream(op))
    own = (np.frombuffer(snd, np.uint8).astype(float) - 128) / 128
    return A.loudness(mus, rate)[0], A.loudness(own, rate)[0]


def main():
    with tempfile.TemporaryDirectory() as tmp:
        vbp = os.path.join(tmp, "l.vbp")
        B.demo_project(vbp, bars=2)
        p = P.Project.load(vbp)
        rate = p.d["target"]["rate"]
        r = A.levels(p)
        rows = {x["sound"]: x for x in r["sounds"]}
        ok(r["target"] is not None and rows["music"]["plays"] == "music"
           and rows["riser"]["plays"] == "own"
           and rows["tick"]["plays"] == "effect",
           "measured: %d sounds, the music at %.1f dB; the riser plays as "
           "an opening's own, the tick as an effect"
           % (len(rows), r["target"]))
        m0, o0 = bundle_loud(B.build_project(p), rate)
        for sid, x in rows.items():
            p.d["sounds"][sid]["level"] = x["suggest"]
        p.save()
        r2 = A.levels(p)
        bad = []
        for x in r2["sounds"]:
            if x["loud"] is None or x["plays"] == "music":
                continue
            at_target = abs(x["loud"] - r["target"]) < 0.5
            at_peak = abs(x["headroom"]) < 0.5
            if not (at_target or at_peak):
                bad.append("%s %.1f" % (x["sound"], x["loud"]))
        ok(not bad, "applied: every sound at the music's %.1f dB or at its "
           "peak's limit (%s)" % (r["target"], bad or "all"))
        m1, o1 = bundle_loud(B.build_project(p), rate)
        ok(abs(o0 - m0) > 3 and abs(o1 - m1) < 1.0,
           "the BUNDLE: the opening's sound against the music %.1f dB "
           "before, %.1f dB after" % (o0 - m0, o1 - m1))
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

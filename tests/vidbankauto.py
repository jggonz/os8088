#!/usr/bin/env python3
"""--bank auto, --layer-bank auto and --layer-passes - SPEC.md 98.2.1.3.3,
on the host.

    python3 tests/vidbankauto.py

A 28-second Life clip (vidlyorder's) for a 20 KB/s disk on a 286, with a
layer for 60 KB/s:

1. --bank auto --pool 832, prefilled whole: the file ASKS for what the
   encode drew, in whole 32 KB slots, under the pool it was encoded against
   (this clip draws 640 of 832) - and the options record holds the KB, not
   the word, with the pool beside it.
2. --layer-bank auto --layer-pool 832, two passes: the header's layer bank
   is the layer's pool less H_LYNEED, the bank the layer's machine keeps
   for the base - every KB of the pool given to one or the other - and the
   second pass made the layer KNOWING that need: on the layer's machine the
   base's bank never ran dry under it (short 0): the second pass holds the
   layer's decode to what leaves the base's bucket at or above the least
   the base's whole future needs, which the first pass recorded.
3. The same with --layer-passes one: the same need learned as it goes, so
   the layer was made on disk the bank turned out to need (short > 0 on
   this clip) - which is what two passes are for. If one pass ever came
   out clean on it, 2 would not be testing anything.
4. A fixed --bank and --layer-bank are untouched: asked as given.

Broken on purpose: `layer_first_pass` returning None makes 2's short what
3's is; `bspan` left at its start makes 1 ask for the whole pool.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88vid as vid                                       # noqa: E402
import os88venc as venc                                     # noqa: E402

BASE = ["--preset", "cga", "--profile", "286-slow", "--audio", "none",
        "--fps", "15", "--disk", "20000", "--reserve", "32",
        "--prefill", "all", "--layer-disk", "60000",
        "--layer-memory", "64"]


def run(src, out, *extra):
    a = venc.parser().parse_args([src, out, "--quiet"] + BASE + list(extra))
    return venc.encode(a)


def main():
    if not shutil.which("ffmpeg"):
        print("vidbankauto: SKIP - no ffmpeg")
        return 0
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "life.mkv")
        subprocess.run(
            ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
             "life=s=640x200:mold=10:r=15:ratio=0.4:seed=5088:death_color=#000000:"
             "life_color=#ffffff,trim=duration=28", "-c:v", "ffv1", src],
            check=True)
        # --- 1
        p = os.path.join(tmp, "A.V88")
        run(src, p, "--bank", "auto", "--pool", "832")
        r = vid.Reader(p)
        o = r.options()["o"]
        print("   1. bank auto, pool 832: asks %d KB (prefill %s); the record "
              "%r, pool %r" % (r.xbank, "all" if r.xpre == vid.XPRE_ALL
                               else r.xpre, o.get("bank"), o.get("pool")))
        if not 0 < r.xbank < 832 or r.xbank % 32:
            bad.append("1: --bank auto asked %d KB of an 832 KB pool"
                       % r.xbank)
        if o.get("bank") != float(r.xbank) or o.get("pool") != 832.0:
            bad.append("1: the record holds bank %r, pool %r"
                       % (o.get("bank"), o.get("pool")))
        # --- 2, 3: a lossy base (a small bank), so the layer has work
        res = {}
        for passes in ("two", "one"):
            p = os.path.join(tmp, "L%s.V88" % passes)
            res[passes] = run(src, p, "--bank", "256", "--layer-bank",
                              "auto", "--layer-pool", "832",
                              "--layer-passes", passes)
            r = vid.Reader(p)
            ly = res[passes]["layer"]
            print("   %s pass%s: need %d KB, layer bank %d KB, layer %.1f "
                  "KB/s, short %d" % (passes, "es" if passes == "two" else "",
                                      r.lyneed, r.lbank, ly["kbs"],
                                      ly["short"]))
            if r.lyneed + r.lbank != 832 or r.lbank % 32:
                bad.append("%s: the layer bank %d and the need %d are not "
                           "the 832 KB pool" % (passes, r.lbank, r.lyneed))
            if not ly["kbs"]:
                bad.append("%s: no layer was made - nothing tested" % passes)
        if res["two"]["layer"]["short"]:
            bad.append("2: two passes, and the layer was still made on disk "
                       "the base's bank needed in %d frames"
                       % res["two"]["layer"]["short"])
        if not res["one"]["layer"]["short"]:
            bad.append("3: one pass came out clean on this clip - 2 tests "
                       "nothing")
        # --- 4
        p = os.path.join(tmp, "F.V88")
        run(src, p, "--bank", "256", "--layer-bank", "512",
            "--layer-passes", "one")
        r = vid.Reader(p)
        print("   4. fixed: asks %d KB, the layer's %d" % (r.xbank, r.lbank))
        if (r.xbank, r.lbank) != (256, 512):
            bad.append("4: a fixed bank and layer bank came out %d and %d"
                       % (r.xbank, r.lbank))
    for b in bad:
        print("   FAIL: %s" % b)
    print("vidbankauto: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""A STREAMED CLIP HELD TO THE DISK (SPEC.md 125.4.3): the builder's
reader's model, host side.

    python3 tests/vbtcap.py

The owner's Hercules battle cut its clips' own sound where a clip ran
faster than the disk for a second or two (Bolt's first 1.5 s at 129 KB/s
against a 74 KB/s average). The demo battle, its ARI attack given two
seconds of a picture changing all over (~140 KB/s at 100% Hercules):

  - capped (the default): the build says it held that clip, and the
    reader's model run over the BUILT records - the head in at the start,
    the profile's disk at three quarters less the music each frame, no
    more than CAP_HOLD ahead - never overdraws by more than half a frame's
    income; every path still verifies (each seam lands on the loop);
  - uncapped (target.burst_cap false): the same clip overdraws by tens of
    KB - which is the cut the owner heard.

Broken on purpose (CAP_HOLD set to 1 MB, so the model never holds a frame
back) the capped build overdraws like the uncapped one and this goes red.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88vbt as B         # noqa: E402
import vbtana as A          # noqa: E402
import vbtproj as P         # noqa: E402

fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def worst(data, name, cap):
    """The deepest overdraw of the reader's model over clip NAME's records"""
    b = B.Bundle(data)
    c = [x for x in b.clips if x["name"].strip().lower() == name[:8]][0]
    o, sizes = c["off"], []
    for _ in range(c["nrec"]):
        n = struct.unpack_from("<H", data, o)[0]
        sizes.append(n)
        o += n
    per = cap["rate"] / float(cap["fps"])
    avail, low = cap["head"] - sizes[0], 0.0
    for n in sizes[1:-1]:           # (the last is the seam onto the loop)
        avail = min(cap["hold"], avail + per) - n
        low = min(low, avail)
    return low, per, max(sizes) / 1024.0


def main():
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as t:
        v = os.path.join(t, "burst.mp4")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                        "-i", "testsrc2=size=320x240:rate=15,noise=alls=45:"
                        "allf=t:enable='between(t,1,3)'", "-t", "4", v],
                       check=True)
        B.demo_project(os.path.join(t, "c.vbp"))
        p = P.Project.load(os.path.join(t, "c.vbp"))
        p.add_source("burst", path=v)
        n = len(A.screens(p, "burst"))
        c = p.d["clips"]["ari_attack"]
        c.update(source="burst", **{"in": 0, "out": n - 1})
        c["cues"] = [q for q in c.get("cues", []) if q["frame"] < n]
        p.save()
        # the ENGINE's figures, not the builder's constants: what a ring
        # holds ahead of the sound (88 KB less the lead's ~27 KB, rounded
        # down) and the 5150-st225 disk at three quarters less the music
        cap = dict(rate=int(119000 * 0.75) - p.d["target"]["rate"],
                   hold=57344, head=40960, fps=p.d["target"]["fps"])
        on = B.build_project(p, kind="herc")      # (build_project verifies)
        held = dict(B.LAST_CAPPED)
        low_on, per, big_on = worst(on, "ari_attack", cap)
        ok(held.get("ari_attack", 0) > 5 and low_on >= -per / 2,
           "capped: ARI's attack held to the disk (%s), the reader's model "
           "never overdrawn past half a frame's income (deepest %.1f KB, "
           "income %.1f KB a frame), every path verified" % (
               held, low_on / 1024, per / 1024))
        p.d["target"]["burst_cap"] = False
        off = B.build_project(p, kind="herc")
        low_off, _, big_off = worst(off, "ari_attack", cap)
        ok(not B.LAST_CAPPED and low_off < -16384,
           "uncapped: the same clip overdraws the model by %.0f KB (largest "
           "record %.1f KB, capped %.1f)" % (-low_off / 1024, big_off,
                                             big_on))
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

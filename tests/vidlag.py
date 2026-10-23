#!/usr/bin/env python3
"""The lag, searched - SPEC.md 98.2.1.1.3.

    python3 tests/vidlag.py

Host-side. Three questions:

1. IS A FRAME'S CEILING THE MOST IT MAY TAKE? Under the lag schedule a
   frame may run until the NEXT one would start `--lag` or more behind the
   sound, and the next starts at a call, half a period apart. So the
   ceiling is safe (spent whole, the next frame starts short of the
   allowance) and TIGHT (a hundredth of a period more and it would not),
   for every allowance and every lag the frame starts at - an allowance
   between two calls included, which is where it was not: the ceiling was
   `allowance - a call`, so the frame stopped short of a call it would have
   started the next one at anyway, and 2.25 frames cut 89 of Mech2's
   frames where 2.0 cut 25. Broken on purpose (that formula back), 17 of
   the 31 cases here are not tight.
2. DOES THE RANGE SEARCH EARN ITS KEEP? A 6 s clip on Mech2's own shape
   (mode 13h, the 286-picomem1, a card's PCM8, a 63.5 KB record, an XMS
   bank prefilled): one noisy frame alone, then a train of two-frame noise
   pulses - bursts of different weight, where fixed allowances cut 5, 5, 3,
   3, 3 frames at 67 to 200 ms. `--lag 50-200` must cut no more frames than
   `--lag 200` (the search has the top as one of its choices), must choose
   more than one allowance and one below the top (a burst drawn whole at
   every allowance keeps the least lag), and replayed as the player
   schedules it - each frame at the first call after it is due and after
   the frame before it ends - its picture must fall a whole frame behind
   somewhere (or this tests nothing) and never as far as 200 ms. Broken on
   purpose (the search picks its WORST candidate), it cuts more than the
   top alone.
3. DO THE RECORD AND THE OPTION SAY THE SAME? `--lag` parses a number as
   the fixed allowance and LO-HI as a range, refuses HI below LO, and an
   options record made before ranges (version 35) reads its number as the
   fixed allowance it was.

Needs ffmpeg and numpy for question 2, and SKIPS without them: the box
declining to answer, not a pass.
"""
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))

import os88vid as vid                                       # noqa: E402

try:
    import numpy                                            # noqa: F401
    import os88venc as venc                                 # noqa: E402
except ImportError:
    print("SKIP: numpy is not installed")
    sys.exit(0)

FPS = 15.0
PROFILE = "286-picomem1"


def ceiling(bad):
    """Question 1, on an Encoder the encode would make for Mech2"""
    g = vid.Geom(vid.LAY_LIN320, 320, 200)
    e = venc.Encoder(g, dict(venc.PROFILES[PROFILE]), FPS,
                     venc.CYC_AUDIO * 1470, 22050)
    e.owe, e.fgrid = None, e.q / 2      # (the unflipped card's schedule)
    loose = n = 0
    for L in (1.0, 1.25, 1.5, 1.6, 2.0, 2.25, 2.5, 3.1):
        for F in (0.0, 0.5, 1.0, 1.5, 2.0):
            if F >= L:
                continue
            e.flag_lag, e.flag = L, F * e.q
            e.ceil = e.peak = math.inf
            e.begin()
            c = e.ceil
            e.end(c)
            safe = e.flag < L * e.q
            e.flag = F * e.q
            e.end(c + 0.01 * e.q)
            tight = e.flag >= L * e.q
            n += 1
            if not safe:
                bad.append("the ceiling at a lag of %g, %g frames behind: "
                           "spent whole, the next frame starts %.2f behind"
                           % (L, F, e.flag / e.q))
            loose += not tight
    print("   1: %d of %d ceilings the most a frame may take" % (n - loose, n))
    if loose:
        bad.append("%d of %d ceilings leave room a frame could have taken "
                   "and still kept the next short of the allowance"
                   % (loose, n))


def search(tmp, bad):
    """Question 2"""
    src = os.path.join(tmp, "pulses.mkv")
    subprocess.run(
        ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
         "testsrc2=size=320x200:rate=15:duration=6", "-f", "lavfi", "-i",
         "sine=frequency=440:sample_rate=22050:duration=6", "-vf",
         "noise=alls=40:allf=t:enable='between(n,15,15)+"
         "lt(mod(n,6),2)*between(n,38,70)'",
         "-c:v", "ffv1", "-c:a", "pcm_s16le", src], check=True)

    def enc(lag, name):
        out = os.path.join(tmp, name + ".V88")
        os.environ["OS88_VENC_LAGDUMP"] = os.path.join(tmp, name + ".json")
        try:
            res = venc.encode(venc.parser().parse_args(
                [src, out, "--quiet", "--preset", "vga8", "--profile",
                 PROFILE, "--fps", "%g" % FPS, "--fit", "fill",
                 "--frame-cap", "63.5", "--bank", "2944", "--prefill", "all",
                 "--lag", lag]))
        finally:
            del os.environ["OS88_VENC_LAGDUMP"]
        log = None
        if os.path.exists(os.path.join(tmp, name + ".json")):
            with open(os.path.join(tmp, name + ".json")) as fh:
                log = json.load(fh)
        return res, out, log
    top, _, _ = enc("200", "top")
    rng, out, log = enc("50-200", "range")
    real = venc.lag_best
    venc.lag_best = lambda runs: max(runs, key=lambda r: (r["cut"], -r["L"]))
    try:                                # (broken on purpose: the worst)
        worst, _, _ = enc("50-200", "worst")
    finally:
        venc.lag_best = real
    chose = sorted(set(L for _, L, _, _ in (log or {}).get("log", ())))
    hi = log["cands"][-1] if log else None
    print("   2: --lag 200 cut %d frames, --lag 50-200 %d (chose %s of %s "
          "frames), its WORST pick %d"
          % (top["cuts"], rng["cuts"], chose,
             log and log["cands"], worst["cuts"]))
    if rng["cuts"] > top["cuts"]:
        bad.append("the range cut %d frames, the top of it alone %d"
                   % (rng["cuts"], top["cuts"]))
    if worst["cuts"] <= top["cuts"]:
        bad.append("the search's worst pick cut %d, no more than the top "
                   "alone's %d - the clip cannot tell a search from none"
                   % (worst["cuts"], top["cuts"]))
    if len(chose) < 2 or not chose or chose[0] >= hi:
        bad.append("the search chose %s of %s: not one allowance below the "
                   "top, or only one" % (chose, log and log["cands"]))
    # THE PLAYER'S SCHEDULE, from the file: a call every half period off
    # the card, each frame drawn at the first call after it is due and
    # after the one before it is drawn
    r = vid.Reader(out)
    P = venc.PROFILES[PROFILE]
    t, _ = venc.profile_table(P, vid.LAY_LIN320)
    q = vid.HZ / r.fps * P["speed"]
    aud = venc.CYC_AUDIO * r.abytes + venc.HOOK_CYC
    # (the file plays out of XMS in place on this profile, 98.3.18.13: a
    # record's borrows on top of its decode, at the profile's own prices)
    us = vid.HZ / 1e6 * P["speed"]
    xfix = P["xbor_us"] * P.get("xbor_n", 3) * us
    xbyte = P["xbor_us"] / float(P.get("xyield", 2048)) * us
    end, most = 0.0, 0.0
    for i, (rec, _, _) in enumerate(r.records()):
        start = math.ceil(max(i * q, end) / (q / 2) - 1e-9) * (q / 2)
        most = max(most, (start - i * q) / q)
        end = start + vid.cycles_of(rec, table=t) + aud + xfix + \
            xbyte * len(rec)
    allow = 0.200 * r.fps
    print("   2: replayed, the picture %.2f frames behind at most (200 ms is "
          "%.1f)" % (most, allow))
    if most >= allow:
        bad.append("the range's picture fell %.2f frames behind, as far as "
                   "--lag 200's %.1f" % (most, allow))
    if most < 1:
        bad.append("the range's picture was never a frame behind - the leg "
                   "tests nothing")


def option(bad):
    """Question 3"""
    for v, want in (("100", 100.0), ("50-200", "50-200"), (" 50 - 150 ",
                                                           "50-150"),
                    ("120-120", 120.0), (100.0, 100.0)):
        got = venc.lag_arg(v)
        if got != want:
            bad.append("--lag %r read as %r, not %r" % (v, got, want))
    for v in ("200-50", "-5", "fast"):
        try:
            venc.lag_arg(v)
            bad.append("--lag %r was taken" % v)
        except Exception:
            pass
    if venc.lag_bounds("50-200") != (50.0, 200.0) or \
            venc.lag_bounds(100.0) != (100.0, 100.0):
        bad.append("lag_bounds: %r, %r" % (venc.lag_bounds("50-200"),
                                           venc.lag_bounds(100.0)))
    o, _ = venc.opts_migrate(dict(v=35, o=dict(lag=100.0)))
    if o.get("lag") != 100.0:
        bad.append("a version-35 record's --lag 100 read as %r"
                   % o.get("lag"))
    print("   3: --lag's spellings and a version-35 record read as made")


def main():
    bad = []
    ceiling(bad)
    option(bad)
    if not shutil.which("ffmpeg"):
        print("SKIP: question 2 needs ffmpeg")
    else:
        tmp = tempfile.mkdtemp()
        try:
            search(tmp, bad)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    for b in bad:
        print("FAIL: " + b)
    if bad:
        return 1
    print("vidlag: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

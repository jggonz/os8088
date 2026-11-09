#!/usr/bin/env python3
"""A CAPTURE read by its cadence (SPEC.md 126.2.5): the frames its footage
had, not every fourth captured one by time.

    python3 tests/vbtcadence.py

The owner's footage is captured at 60 fps from 15 fps originals, and the
capture drifts - runs of four captured frames to an original, now and then
three and a stray part-drawn one, or five. Read by time (ffmpeg's fps
filter) that takes one original twice and steps over the next: "it stutters
exactly once, always the exact same spot". Here a synthetic original, 48
frames of a bar moving across, each frame its own, with ONE genuine hold
(frames 30 and 31 the same picture), is captured that way into a lossless
60 fps file, and:

  1. read by time it shows a captured frame that is NO original - the
     stray, part-drawn one: most of it the frame before, so a near repeat,
     and the frame after a jump of two (the bug, seen);
  2. vbtana.vid_path reads it by its cadence: 48 frames, each its original
     exactly - the hold kept as two;
  3. a source at its own rate (30 fps of distinct frames) is not a
     capture: read by time, the file unchanged;
  4. `cadence: false` reads the capture by time.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import vbtana as A          # noqa: E402

W, H = 96, 64
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def original(n=48):
    fr = []
    for k in range(n):
        f = np.zeros((H, W, 3), np.uint8)
        x = (k if k < 31 else k - 1) * 2      # (31 holds 30's picture)
        f[10:54, x % (W - 8):x % (W - 8) + 8] = (200, 180, 40)
        f[2:6, :] = (k * 5 % 255 if k != 31 else 30 * 5, 60, 90)
        fr.append(f)
    return fr


def capture(orig, runs):
    """ORIG at 60 fps: original K for RUNS[K] captured frames; a run given
    as (3, 1) is three captures and one part-drawn (half the next)"""
    out = []
    for k, r in enumerate(runs):
        if isinstance(r, tuple):
            out += [orig[k]] * r[0]
            nxt = orig[min(k + 1, len(orig) - 1)]
            torn = orig[k].copy()
            torn[H // 2:] = nxt[H // 2:]
            out.append(torn)
        else:
            out += [orig[k]] * r
    return out


def write(path, frames, fps):
    p = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo",
                          "-pix_fmt", "rgb24", "-s", "%dx%d" % (W, H), "-r",
                          str(fps), "-i", "-", "-c:v", "ffv1", path],
                         stdin=subprocess.PIPE)
    for f in frames:
        p.stdin.write(f.tobytes())
    p.stdin.close()
    assert p.wait() == 0


def read(path, by_time=None):
    vf = ["-vf", "fps=%d" % by_time] if by_time else []
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path] + vf +
                         ["-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                         capture_output=True).stdout
    return list(np.frombuffer(raw, np.uint8).reshape(-1, H, W, 3))


class Proj(object):
    def __init__(self, tmp, path, **t):
        self.path = os.path.join(tmp, "p.vbp")
        self.d = {"sources": {"s": {"kind": "video", "path": path}},
                  "target": dict(fps=15, **t)}

    def here(self, p):
        return p


def main():
    orig = original()
    runs = []                   # the drift: 3 + a stray, a 5, now and then
    for k in range(len(orig)):
        runs.append((3, 1) if k % 7 == 3 else 5 if k % 11 == 6 else
                    3 if k % 11 == 7 else 4)
    with tempfile.TemporaryDirectory() as tmp:
        cap = os.path.join(tmp, "cap.mkv")
        write(cap, capture(orig, runs), 60)
        timed = read(cap, by_time=15)
        stray = [i for i, f in enumerate(timed)
                 if not any(np.array_equal(f, o) for o in orig)]
        ok(stray, "1. read by time, %d of its %d frames are no original - "
           "the capture's stray part-drawn frames (at %s)"
           % (len(stray), len(timed), stray))
        A.CADENCED.clear()
        p = A.vid_path(Proj(tmp, cap), "s")
        got = read(p) if p != cap else []
        same = [np.array_equal(g, o) for g, o in zip(got, orig)]
        ok(p != cap and len(got) == len(orig) and all(same) and
           A.CADENCED.get(cap) == (60, 15),
           "2. by its cadence: %d frames of %d, %d exactly the original's "
           "(the hold kept as two), recorded as 60 fps of 15" % (
               len(got), len(orig), sum(same)))
        own = os.path.join(tmp, "own.mkv")
        write(own, original(), 30)
        ok(A.vid_path(Proj(tmp, own), "s") == own,
           "3. a source at its own rate is no capture: read by time")
        ok(A.vid_path(Proj(tmp, cap, cadence=False), "s") == cap,
           "4. cadence false: the capture read by time")
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

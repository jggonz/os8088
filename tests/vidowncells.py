#!/usr/bin/env python3
"""A SCENE ON A FLIPPED FILE - SPEC.md 98.2.5.2's cells and 98.2.5.2.1's
scene BUILT AHEAD, on 16 colours flipped against its own pages.

    python3 tests/vidowncells.py [--leg cells|build]
                                 [--broken|--broken2|--broken3|--broken4]

Host-side: ffmpeg makes the clip, tools/os88venc.py encodes it and
tools/os88vid.py decodes what each frame SHOWS, pages and all. The clip
is a test card and then a hard cut to a held, grained picture - every
pixel changing at once, as in the owner's Miku Miku 0:41.8 - each its own
sixteen (--scene-palettes), 640 wide on 16 colours' own 640 x 350 screen,
--flip --flip-own, on the 286 whose decode cannot draw the new scene
whole. The ladder is coarsescene:frame,cut:100: its cut rung takes the
first prefix wherever it can draw, so the row asks what the two defects
decide and not what the view as seen prefers.

Two defects kept the rung from ever drawing a cell here, and each row of
the check is one of them:

1. THE LADDER WAS SKIPPED AT THE SCENE: a flipped file holds no frame
   its palette changes at, and the guard that kept the hold out kept the
   whole ladder out - the scene's first frame was the one frame the scene
   rung is for. Its cells must be drawn: the report counts them, and the
   ladder decides rung 1 at least once.
2. ONE PAGE OF TWO: against its own pages, the frame after the scene's
   first is drawn on the page that still holds the scene before, so
   cells on one page and a cut on the other alternated on the glass. The
   frame after the scene's first must show as little of the scene before
   as the scene's first does - cells on both.

--broken encodes with the rung left out (--ladder cut:100, which only
cuts) and must FAIL; --broken2 keeps the rung and draws the cells on the
scene's first page alone (own_cells 1), and must FAIL row 2.

THE BUILD LEG (--flip-build 8, no ladder): the same clip, the scene's
first frame built on the page the glass is not showing by frames HELD
before it. Each row of it:

3. BUILT: the report names the scene, after one held frame or more; each
   of those records is HELD and the glass shows the frame before them
   throughout - the last scene stays up, nothing torn.
4. WHOLE ON ITS FLIP: the scene's first frame shows its target exactly.
5. THE PAGE LEFT BEHIND: the record after the flip carries REC_UP, the
   glass's copy onto that page through the latches (98.1.3.10), and the
   frame it shows is the new scene and not the old.

--broken3 builds the scene and leaves the copy out (no own_next after
the flip) and must FAIL row 5; --broken4 never builds (every trial
reported cut) and must FAIL rows 3 and 4."""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88venc as venc    # noqa: E402
import os88vid as vid      # noqa: E402

SKIP = 77


def shown_rgb(np, r, f, cv=None):
    """Frame f as the glass shows it (or `cv`, a canvas, in f's colours)"""
    if cv is None:
        cv = vid.decode_at(r, f)
    pal = np.frombuffer(vid.palette_at(r, f) or vid.STD16, np.uint8)
    return pal.reshape(-1, 3)[:16].astype(np.int64)[
        (np.frombuffer(cv, np.uint8) if isinstance(cv, bytes)
         else np.asarray(cv, np.uint8)).reshape(-1)]


def scene_k(np, keep):
    """The scene change: the TARGET most unlike the one before it"""
    return max(range(1, len(keep)),
               key=lambda f: np.count_nonzero(keep[f] != keep[f - 1]))


def nearer_old(np, r, keep, k, f):
    """The share of frame f's pixels nearer frame k - 1's picture than f's
    own target"""
    old = shown_rgb(np, r, k - 1)
    s_ = shown_rgb(np, r, f)
    t_ = shown_rgb(np, r, f, keep[f])
    return float(np.mean(np.abs(s_ - old).sum(1) < np.abs(s_ - t_).sum(1)))


def cells_leg(np, src, tmp, rungless, bad):
    out = os.path.join(tmp, "own.V88")
    args = venc.parser().parse_args(
        [src, out, "--quiet", "--preset", "vga4", "--box", "640x350",
         "--screen", "640x350", "--palette", "colour16",
         "--scene-palettes", "--flip", "--flip-own",
         "--profile", "286-picomem1",
         "--ladder", "cut:100" if rungless
         else "coarsescene:frame,cut:100"])
    keep = []
    res = venc.encode(args, keep)
    vid.verify_v88(out)
    r = vid.Reader(out)
    cells = sum(res.get("cells", {}).values())
    own = 2                         # (cells on each page: own_cells)
    lad = res.get("ladder") or {}
    print("   cells drawn %d (%s), ladder %s"
          % (cells, res.get("cells"), lad))
    if cells < own or not any(li for li in lad):
        bad.append("the scene rung drew cells on %d of %d pages (cells "
                   "%r, ladder %r)" % (cells, own, res.get("cells"), lad))
    # the scene change, and what each frame around it SHOWS, pages and all
    k = scene_k(np, keep)
    share = {f: nearer_old(np, r, keep, k, f) for f in (k, k + 1)}
    print("   scene change at frame %d: nearer the scene before than "
          "the new one on %.0f%% of frame %d and %.0f%% of frame %d"
          % (k, 100 * share[k], k, 100 * share[k + 1], k + 1))
    if share[k + 1] > max(0.10, 2 * share[k]):
        bad.append("frame %d, the other page, is nearer the scene "
                   "before on %.0f%% of it (frame %d %.0f%%): cells on "
                   "one page and a cut on the other"
                   % (k + 1, 100 * share[k + 1], k, 100 * share[k]))
    if share[k] > 0.10:
        bad.append("frame %d, the scene's first, is nearer the scene "
                   "before on %.0f%% of it: no cells there"
                   % (k, 100 * share[k]))


def build_leg(np, src, tmp, bad):
    out = os.path.join(tmp, "build.V88")
    args = venc.parser().parse_args(
        [src, out, "--quiet", "--preset", "vga4", "--box", "640x350",
         "--screen", "640x350", "--palette", "colour16",
         "--scene-palettes", "--flip", "--flip-own",
         "--profile", "286-picomem1", "--flip-build", "8"])
    keep = []
    res = venc.encode(args, keep)
    vid.verify_v88(out)
    r = vid.Reader(out)
    k = scene_k(np, keep)
    m = (res.get("built") or {}).get(k, 0)
    recs = [rec for rec, _, _ in r.records()]
    hdr = [struct.unpack_from("<HH", rec, 2) for rec in recs]
    held = [f for f in range(len(hdr)) if hdr[f][0] & vid.REC_HELD]
    print("   build: scene at frame %d, built %r; held %s; REC_UP on %s"
          % (k, res.get("built"), held,
             [f for f in range(len(hdr)) if hdr[f][1] & vid.REC_UP]))
    if not m or held != list(range(k - m, k)):
        bad.append("the scene at frame %d was not built ahead: built %r, "
                   "held %s" % (k, res.get("built"), held))
    else:
        still = [f for f in range(k - m, k)
                 if vid.decode_at(r, f) != vid.decode_at(r, k - m - 1)]
        if still:
            bad.append("frames %s, held, do not show frame %d's picture"
                       % (still, k - m - 1))
    wrong = int(np.count_nonzero(
        np.frombuffer(vid.decode_at(r, k), np.uint8) !=
        np.asarray(keep[k], np.uint8).reshape(-1)))
    up = k + 1 < len(hdr) and bool(hdr[k + 1][1] & vid.REC_UP)
    after = nearer_old(np, r, keep, k, k + 1)
    print("   build: frame %d %d pixels off its target; frame %d REC_UP %s, "
          "nearer the scene before on %.0f%%" % (k, wrong, k + 1, up,
                                                 100 * after))
    if wrong:
        bad.append("frame %d, the scene built ahead, is %d pixels off its "
                   "target" % (k, wrong))
    if not up or after > 0.10:
        bad.append("frame %d, the page the flip left: REC_UP %s, nearer the "
                   "scene before on %.0f%%" % (k + 1, up, 100 * after))


def _no_up(turn):
    def f(self):
        if getattr(self, "vpbuild", None) and \
                self.stats["frames"] - 1 in self.vpbuild:
            self.own_next = False
        return turn(self)
    return f


def main():
    if venc.np is None or not shutil.which("ffmpeg"):
        print("   SKIP: needs ffmpeg and numpy")
        return SKIP
    np = venc.np
    argv = sys.argv[1:]
    broken2 = "--broken2" in argv
    broken3 = "--broken3" in argv
    broken4 = "--broken4" in argv
    legs = [argv[argv.index("--leg") + 1]] if "--leg" in argv else \
        ["build"] if broken3 or broken4 else \
        ["cells"] if "--broken" in argv or broken2 else ["cells", "build"]
    if broken2:                         # (cells on the scene's first page
        venc.own_cells = lambda e: 1    # alone: defect 2 put back)
    if broken3:                         # (no copy after the flip)
        venc.Flipped.own_turn = _no_up(venc.Flipped.own_turn)
    if broken4:                         # (no hold ever builds it)
        venc.build_try = lambda *x, **kw: 1
    broken = "--broken" in argv or broken2 or broken3 or broken4
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        # two scenes, each moving: a scrolling test card, then a zooming
        # Mandelbrot in other colours - every pixel changes at the cut
        src = os.path.join(tmp, "src.mkv")
        subprocess.run(
            ["ffmpeg", "-v", "error",
             "-f", "lavfi", "-i", "testsrc2=size=640x400:rate=24:duration=1",
             "-f", "lavfi", "-i", "mandelbrot=size=640x400:rate=24",
             "-f", "lavfi", "-i",
             "sine=frequency=440:sample_rate=22050:duration=2",
             "-filter_complex", "[0:v]setsar=1[a];[1:v]trim=end_frame=1,"
             "noise=alls=50,loop=loop=23:size=1,setpts=N/24/TB,setsar=1[b];"
             "[a][b]concat=n=2:v=1:a=0,format=yuv420p[v]",
             "-map", "[v]", "-map", "2:a", "-c:v", "ffv1",
             "-c:a", "pcm_s16le", src], check=True)
        if "cells" in legs:
            cells_leg(np, src, tmp, broken and not (broken2 or broken3 or
                                                    broken4), bad)
        if "build" in legs:
            build_leg(np, src, tmp, bad)
    for b in bad:
        print("   FAIL: " + b)
    if broken:
        print("   (--broken: %s)" % ("FAILED, as it must" if bad
                                     else "PASSED - the check sees nothing"))
        return 0 if bad else 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

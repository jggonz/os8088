#!/usr/bin/env python3
"""PIXELSTEIN 3D's frame, staged, on MartyPC (SPEC.md 97.10) - AN INSTRUMENT.

    python3 tests/pxsperf.py [--machine os8088_5150_cga_gla] [--frames 8]
                             [--windowed] [--c160] [--probe]

tests/skiesperf.py's shape: it asserts nothing but that every stage
produced a number. In the bracket (or --windowed), at Textured Low res 64 x
80 (the default), Textured Full 64 x 80, Textured Low res 48 x 80 (the
fallback) and Flat Low res, on both pinned scenes and on both frames (a
FULL REPAINT and a TURN), five exec breakpoints split the frame into cast /
gather / compose / present / loop (pxslib.stage_times) and the draw queue's length
is read off px_qp - the walls the driver drew that frame. `--probe` builds
the package with -DPXPROBE into a scratch 360 KB disk and reads its two
counters beside the stages (ladder entries, columns skipped); the shipped
build carries none of that and tests/unit/t_pxsscale.py holds the plain
assembly to build/pxstein.o88.
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins (pxslib)
import os88marty                                                # noqa: E402
import pxslib                                                    # noqa: E402

RUNGS = (("tex", True, 64, "Textured Low res 64x80"),
         ("tex", False, 64, "Textured Full 64x80"),
         ("tex", True, 48, "Textured Low res 48x80"),
         ("flat", True, 64, "Flat Low res 64x80"))
FAIL = []


def check(ok, what):
    print("   %-66s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def probe_disk(tmp):
    """The PXPROBE package on a 360 KB disk of its own."""
    b = os.path.join(tmp, "pxgame.bin")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-DPXPROBE", "-I", "apps/",
                    "-I", "apps/pixelstein/", "-o", b, "apps/pixelstein/pxgame.asm"],
                   check=True, cwd=ROOT)
    o88 = os.path.join(tmp, "pxstein.o88")
    subprocess.run([sys.executable, "tools/os88pkg.py", "build/pxstein.bin", "-o", o88,
                    "--part", b, "--part", "build/pxslev.bin", "--part", "build/pxsart.bin"],
                   check=True, cwd=ROOT, capture_output=True)
    img = os.path.join(tmp, "probe360.img")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", img, "--size", "360", o88],
                   check=True, cwd=ROOT, capture_output=True)
    return o88, img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    ap.add_argument("--frames", type=int, default=8)
    ap.add_argument("--windowed", action="store_true")
    ap.add_argument("--c160", action="store_true")
    ap.add_argument("--probe", action="store_true", help="the -DPXPROBE build, counters beside the stages")
    a = ap.parse_args()
    os.chdir(ROOT)
    tmp = tempfile.mkdtemp(prefix="pxsperf_")
    apps = a.apps
    if a.probe:
        pxslib.DEFINES = ("PXPROBE",)
        pxslib.PKG, apps = probe_disk(tmp)
        print("   probe build: %s on %s" % (pxslib.PKG, apps))
    rows = []
    with os88marty.launch(a.image, apps=apps, machine=a.machine) as m:
        g = pxslib.open_game(m)
        st = g.state()
        print("   PXSTEIN.O88: window %d, part 0 at %04x, tier %d, texok %d"
              % (g.win, g.seg, st["tier"], st["texok"]))
        if not a.windowed:
            if a.c160:
                m.pause()
                g.poke_byte("px_mode", 2)
                g.poke_byte("px_fsxm", 0)
                m.run()
            g.enter_fsx()
        world = "windowed" if a.windowed else g.state()["back"]
        L = pxslib.layout()
        for rung, lowres, size, label in RUNGS:
            if rung == "tex" and not st["texok"]:
                continue
            g.pin(rung=rung, lowres=lowres, size=size)
            for scene in ("a", "b"):
                for mode in ("full", "turn"):
                    m.advance(frames=30)
                    m.run()
                    g.scene(scene)
                    g.wait_frames(1)
                    if a.windowed:
                        times = g.frame_times(a.frames, mode=mode)
                        med = dict(frame=pxslib.median([t[0] for t in times]))
                    else:
                        stg = g.stage_times(a.frames, mode=mode)
                        med = {k: pxslib.median([x[k] for x in stg]) for k in stg[0]}
                        # THE MINIMUM BESIDE THE MEDIAN: the guest is cycle-
                        # exact and a stage's only noise is IRQ0 - a present
                        # copies the same 5,120 bytes every frame, so a
                        # median that caught the tick reads ~6,000 clk over
                        # one that did not (the first take of this wave
                        # called that band "the CGA's wait states"; review,
                        # wave 2). On a FULL REPAINT the min is the stage's
                        # work with no tick in it and the median is what a
                        # player gets; on a turn row the min is the frame
                        # with the fewest dirty rows (60 of 80 reads 74,573)
                        # and says nothing about the tick
                        med["present_min"] = min(x["present"] for x in stg)
                    m.pause()
                    q = (g.word("px_qp") - L["Q"]) // pxslib.PXG_QSZ
                    pr = None
                    if a.probe:
                        pr = (g.word("px_pr_lad"), g.word("px_pr_skip"))
                    cols = g.state()["cols"]
                    m.run()
                    rows.append((label, scene, mode, cols, med, q, pr))
                    line = "   %-24s %s %-5s %2d cols: frame %6.1f ms" % (
                        label, scene.upper(), mode, cols, pxslib.ms(med["frame"]))
                    if "cast" in med:
                        line += "  cast %5.1f  gather %4.1f  compose %5.1f  present %5.1f  loop %4.1f" % (
                            pxslib.ms(med["cast"]), pxslib.ms(med["gather"]),
                            pxslib.ms(med["compose"]), pxslib.ms(med["present"]),
                            pxslib.ms(med["loop"]))
                    line += "  queue %2d" % q
                    if pr:
                        line += "  ladder %3d  skipped %2d" % pr
                    print(line)
                    check(med["frame"] > 0, "%s %s %s produced a frame" % (label, scene, mode))
        if not a.windowed:
            g.leave_fsx()
    print()
    print("   %s %s: the staged frame (median of %d; ms at 4.772727 MHz; per column where "
          "it applies)" % (a.machine, world, a.frames))
    for label, scene, mode, cols, med, q, pr in rows:
        if "cast" in med:
            print("     %-24s %s %-5s %2d cols  cast %7d (%5.0f/ray)  gather %6d  compose %7d "
                  "(%5.0f/col)  present %7d (min %7d; of it waited %6d)  loop %5d  = %6.1f ms  queue %d%s"
                  % (label, scene.upper(), mode, cols, med["cast"], med["cast"] / cols,
                     med["gather"], med["compose"], med["compose"] / cols, med["present"],
                     med["present_min"], med["wait"], med["loop"], pxslib.ms(med["frame"]), q,
                     "  ladder %d skipped %d" % pr if pr else ""))
        else:
            print("     %-24s %s %-5s %2d cols  = %6.1f ms  queue %d" % (
                label, scene.upper(), mode, cols, pxslib.ms(med["frame"]), q))
    if FAIL:
        print("pxsperf: FAIL (%d)" % len(FAIL))
        return 1
    print("pxsperf: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

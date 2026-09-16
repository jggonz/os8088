#!/usr/bin/env python3
"""The package against the reference renderer, column by column and byte by
byte (SPEC.md 96.5, 96.10).

    python3 tests/pxssim.py [--machine os8088_5150_cga_gla] [--windowed-only]

tools/pxssim.py is PIXELSTEIN 3D's frame on the host - the same tables, the
same walker, the same hit, the same shadow bytes - and this row holds the
guest to it on the two pinned scenes, at both rungs and both resolutions,
windowed (the WIN1 band is the Hercules byte set) and in the bracket (the
adapter's own backend). What is compared: the column arrays px_cast wrote
(top, bot, wallh, mat, side, u) and the WHOLE 80 x 80 shadow the compose
wrote, after a forced full frame - AND AGAIN AFTER --turns (3) TURN FRAMES
composed incrementally against that one, nothing forced, so the shadow is
what the skip, the two-ends arm and px_wrun's union path LEFT (SPEC.md
96.5) and must still be the host's whole picture of the last pose. A
forced frame never takes those paths; the first cut of this row compared
forced frames only and would have passed a delta-fill that wrote the wrong
rows. Zero differing columns and zero differing bytes, or the row names
the first column and the first row that disagree - which is the cheapest
diagnosis of the cast there is, and why the reference renderer exists.

MartyPC, because the arrays are read out of the package's bss and the shadow
out of its claim; the machine's speed does not enter into it.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins: THIS
import os88marty                                                # noqa: E402
import pxslib                                                    # noqa: E402
import pxssim                                                    # noqa: E402
import pxslevel                                                  # noqa: E402
# ...file shadows tools/pxssim.py by name, and `pxssim` above must be the
# reference renderer and not this row (pxslib asserts the same)
assert hasattr(pxssim, "render"), "tests/pxssim.py: `pxssim` resolved to this file"

FAIL = []
BACKEND = {"win1": "herc", "cga4": "cga4", "herc": "herc", "modex": "modex",
           "cga16": "cga16"}


def check(ok, what):
    print("   %-66s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def compare(g, lv, label, px, py, head, n, rung, lowres):
    """The guest's arrays and shadow against the host's cast and render of
    (px, py, head): the checks, with the guest paused only to read."""
    g.m.pause()
    st = g.state()
    cols = g.columns()
    sh = g.shadow()
    g.m.run()
    backend = BACKEND[st["back"]]
    check(cols["cols"] == n and st["rung"] == pxslib.PXR[rung] and
          bool(st["lowres"]) == lowres,
          "%s: the guest is at %d columns, rung %d, lowres %d (want %d/%d/%d)"
          % (label, cols["cols"], st["rung"], st["lowres"], n, pxslib.PXR[rung],
             lowres))
    view = pxssim.cast_view(lv.cells, px, py, head, n)
    want = dict(top=[c["top"] for c in view], bot=[c["bot"] for c in view],
                wallh=[c["wallh"] for c in view], mat=[c["mat"] for c in view],
                side=[c["side"] for c in view], u=[c["u"] for c in view])
    for k in ("top", "bot", "wallh", "mat", "side", "u"):
        got = cols[k][:n]
        bad = [i for i in range(n) if got[i] != want[k][i]]
        check(not bad, "%s: %s agrees in every column (%s)"
              % (label, k, "all %d" % n if not bad else
                 "column %d is %d, host %d; %d differ" % (bad[0], got[bad[0]],
                                                            want[k][bad[0]], len(bad))))
    host = pxssim.render(view, backend, n, rung)
    diff = [i for i in range(len(host)) if host[i] != sh[i]]
    check(not diff, "%s: the shadow (%s) is the host's, all %d bytes (%s)"
          % (label, backend, len(host), "0 differ" if not diff else
             "row %d byte %d is %02X, host %02X; %d differ"
             % (diff[0] // 80, diff[0] % 80, sh[diff[0]], host[diff[0]], len(diff))))


def one(g, lv, scene, rung, lowres, turns):
    g.pin(rung=rung, lowres=lowres)
    px, py, head = g.scene(scene)
    g.wait_frames(1)
    g.force()                       # a second whole frame (a full repaint,
    g.wait_frames(1)                # px_force_all's memory), so the settings
                                    # were in force for all of this one
    n = 32 if lowres else 64
    label = "%s %s %s" % (scene.upper(), rung, "low" if lowres else "full")
    compare(g, lv, label, px, py, head, n, rung, lowres)
    # ...and then INCREMENTALLY: `turns` turn frames, each composed against
    # the memory of the one before with nothing forced, and the shadow must
    # still be the host's whole picture of the last pose. On Mode X the
    # memory is per page, so the frame compared was composed against the
    # one two back - the same rule, read off the page the flip showed
    for k in range(1, turns + 1):
        head = (head + pxslib.PX_TURN) & 0xFFF
        g.turn(px, py, head)
        g.wait_frames(1)
    if turns:
        compare(g, lv, label + " +%d turns" % turns, px, py, head, n, rung, lowres)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    ap.add_argument("--windowed-only", action="store_true")
    ap.add_argument("--turns", type=int, default=3,
                    help="turn frames composed incrementally after the forced one (0: none)")
    a = ap.parse_args()
    os.chdir(ROOT)
    lv = pxslevel.parse(os.path.join(pxslevel.DEFAULT_DIR, "e1m1.txt"))
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        g = pxslib.open_game(m)
        print("   PXSTEIN.O88: window %d, part 0 at %04x, handoff %s"
              % (g.win, g.seg, g.handoff()))
        for world in ("window", "bracket"):
            if world == "bracket":
                if a.windowed_only:
                    break
                g.enter_fsx()
            print("   -- %s: backend %s" % (world, g.state()["back"]))
            for scene in ("a", "b"):
                for rung in ("flat", "wire"):
                    for lowres in (True, False):
                        one(g, lv, scene, rung, lowres, a.turns)
        if not a.windowed_only:
            g.leave_fsx()
    if FAIL:
        print("pxssim: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxssim: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

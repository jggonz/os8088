#!/usr/bin/env python3
"""The package against the reference renderer, column by column and byte by
byte (SPEC.md 97.5, 97.10).

    python3 tests/pxssim.py [--machine os8088_5150_cga_gla] [--windowed-only]
                            [--turns 3] [--steps 3]

tools/pxssim.py is PIXELSTEIN 3D's frame on the host - the same tables, the
same walker, the same hit, the same shadow bytes - and this row holds the
guest to it on the two pinned scenes, at every rung and both resolutions,
windowed (the WIN1 band is the Hercules byte set) and in the bracket (the
adapter's own backend). What is compared: the column arrays px_cast wrote
(top, bot, wallh, mat, side, u) and the WHOLE 80 x 80 shadow the compose
wrote, after a forced full frame - AND AGAIN AFTER --turns (3) TURN FRAMES
composed incrementally against that one, nothing forced, so the shadow is
what the skip, the two-ends arm and px_wrun's union path LEFT (SPEC.md
97.5) and must still be the host's whole picture of the last pose - AND
AGAIN AFTER --steps (3) FORWARD STEPS, the eye walked along its heading
with nothing forced: the one motion that holds a wall's u still while its
height grows, which the Textured skip must see through the quantised
height in px_lh (97.3) - the first cut's five-byte skip froze the centre
columns of a wall walked at, and three turns never showed it. A forced
frame never takes those paths; the first cut of this row compared forced
frames only and would have passed a delta-fill that wrote the wrong rows.
Zero differing columns and zero differing bytes, or the row names the
first column and the first row that disagree - which is the cheapest
diagnosis of the cast there is, and why the reference renderer exists.

THE SIZE ROW IS SWEPT TOO: every Size (48..80) at both resolutions in the
bracket, and the window's three, each a forced frame and one turn - the
resolution passed to the renderer, never inferred from the count. The
first cut ran Size 64 alone, and a compose that drew every other Size
eight bytes from where the present read it passed 401 checks (review).

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
           "cga16": "cga16", "win4": "modex"}   # (WIN4: Mode X's bytes, 97.14)


def check(ok, what):
    print("   %-66s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def compare(g, lv, label, px, py, head, n, rung, lowres, size):
    """The guest's arrays and shadow against the host's cast and render of
    (px, py, head): the checks, with the guest paused only to read."""
    g.m.pause()
    st = g.state()
    cols = g.columns()
    sh = g.shadow()
    g.m.run()
    backend = BACKEND[st["back"]]
    st["sprok"] = g.byte("px_sprok")
    check(cols["cols"] == n and st["rung"] == pxslib.PXR[rung] and
          bool(st["lowres"]) == lowres and st["size"] == size,
          "%s: the guest is at %d columns, rung %d, lowres %d, Size %d (want %d/%d/%d/%d)"
          % (label, cols["cols"], st["rung"], st["lowres"], st["size"], n,
             pxslib.PXR[rung], lowres, size))
    seen = set()
    doors = {d["cell"]: d["pos"] for d in g.doors() if d["pos"]}    # the slid ones
    view = pxssim.cast_view(lv.cells, px, py, head, n, rung, seen, doors)
    lv.eye = (px, py, head)
    want = dict(top=[c["top"] for c in view], bot=[c["bot"] for c in view],
                wallh=[c["wallh"] for c in view], mat=[c["mat"] for c in view],
                side=[c["side"] for c in view], u=[c["u"] for c in view])
    keys = ["top", "bot", "wallh", "mat", "side", "u"]
    if rung == "tex":
        want["hq"] = [c["hq"] for c in view]        # the quantised height
        cols["hq"] = list(g.bytes_("px_h", n))      # (97.3), px_h
        keys.append("hq")
    for k in keys:
        got = cols[k][:n]
        bad = [i for i in range(n) if got[i] != want[k][i]]
        check(not bad, "%s: %s agrees in every column (%s)"
              % (label, k, "all %d" % n if not bad else
                 "column %d is %d, host %d; %d differ" % (bad[0], got[bad[0]],
                                                            want[k][bad[0]], len(bad))))
    # ...and the sprites and the weapon over it (wave 3, 97.6): the world at
    # its spawn - the sim is frozen for the whole row - and the pistol at
    # rest; a refused sprite set (px_sprok 0) draws neither, and the host
    # follows the guest's word for it
    host = pxssim.render(view, backend, n, rung, lowres, lv, seen,
                         (1, 0) if st["sprok"] else None)
    diff = [i for i in range(len(host)) if host[i] != sh[i]]
    check(not diff, "%s: the shadow (%s) is the host's, all %d bytes (%s)"
          % (label, backend, len(host), "0 differ" if not diff else
             "row %d byte %d is %02X, host %02X; %d differ"
             % (diff[0] // 80, diff[0] % 80, sh[diff[0]], host[diff[0]], len(diff))))


def step_to(lv, px, py, head, k):
    """The eye k steps along its heading from (px, py) at PX_SPEED a step,
    the package's own arithmetic (px_step: fwd * (cos, sin) in Q8.8), on
    the host - and the cell it lands in must be open, or the scene walks
    into a wall the package's collision would have refused."""
    dx = pxssim.mul14(pxslib.PX_SPEED, pxssim.cos_q14(head)) * k
    dy = pxssim.mul14(pxslib.PX_SPEED, pxssim.sin_q14(head)) * k
    nx, ny = px + dx, py + dy
    cell = lv.cells[(ny >> 8) * pxslevel.MAP_W + (nx >> 8)]
    assert not cell & (pxslevel.SOLID | pxslevel.DOOR), \
        "pxssim: %d steps from (%d,%d) heading %d land in a wall" % (k, px, py, head)
    return nx, ny


def one(g, lv, scene, rung, lowres, turns, size=64, steps=0):
    g.pin(rung=rung, lowres=lowres, size=size)
    px, py, head = g.scene(scene)
    g.wait_frames(1)
    g.force()                       # a second whole frame (a full repaint,
    g.wait_frames(1)                # px_force_all's memory), so the settings
                                    # were in force for all of this one
    n = size // 2 if lowres else size
    label = "%s %s %s %d" % (scene.upper(), rung, "low" if lowres else "full", size)
    compare(g, lv, label, px, py, head, n, rung, lowres, size)
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
        compare(g, lv, label + " +%d turns" % turns, px, py, head, n, rung, lowres, size)
    # ...and then FORWARD: `steps` steps along the heading the turns left,
    # nothing forced - u stands on the wall ahead while h grows, the case
    # the Textured skip's sixth byte (px_lh) exists for
    for k in range(1, steps + 1):
        sx, sy = step_to(lv, px, py, head, k)
        g.turn(sx, sy, head)
        g.wait_frames(1)
    if steps:
        sx, sy = step_to(lv, px, py, head, steps)
        compare(g, lv, label + " +%d steps" % steps, sx, sy, head, n, rung, lowres, size)


def door_sweep(g, lv, rung):
    d0 = g.door(0)
    cell = d0["cell"]
    ex, ey = (cell & 63) * 256 + 128 - 256, (cell >> 6) * 256 + 128
    for pos in (64, 128, 192):
        for lowres in (True, False):
            size = 48
            n = size // 2 if lowres else size
            g.pin(rung=rung, lowres=lowres, size=size)
            g.m.pause()
            g._mark()
            g.eye_poke(ex, ey, 0)
            g.pcell_poke(ex, ey)
            g.door_poke(0, pos=pos, state=2, timer=0)
            g.force_all_poke()
            g.m.run()
            g.wait_frames(1)
            compare(g, lv, "door 0 at %d %s %s %d" % (pos, rung, "low" if lowres else "full", size),
                    ex, ey, 0, n, rung, lowres, size)
    g.m.pause()
    g.door_poke(0, pos=0, state=0, timer=0)
    g.m.run()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    ap.add_argument("--windowed-only", action="store_true")
    ap.add_argument("--turns", type=int, default=3,
                    help="turn frames composed incrementally after the forced one (0: none)")
    ap.add_argument("--steps", type=int, default=3,
                    help="forward steps composed incrementally after the turns (0: none)")
    a = ap.parse_args()
    os.chdir(ROOT)
    lv = pxslevel.parse(os.path.join(pxslevel.DEFAULT_DIR, "e1m1.txt"))
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        g = pxslib.open_game(m)
        print("   PXSTEIN.O88: window %d, part 0 at %04x, handoff %s"
              % (g.win, g.seg, g.handoff()))
        g.sim(False)                # nothing moves: the host renders the
                                    # world at its spawn (97.6, 97.10)
        for world in ("window", "bracket"):
            if world == "bracket":
                if a.windowed_only:
                    break
                g.enter_fsx()
            print("   -- %s: backend %s" % (world, g.state()["back"]))
            texok = g.state()["texok"]
            rungs = ("tex", "flat", "wire") if texok else ("flat", "wire")
            for scene in ("a", "b", "c"):
                for rung in rungs:
                    for lowres in (True, False):
                        one(g, lv, scene, rung, lowres, a.turns, 48,
                            a.steps if rung != "wire" else 0)
            # THE SIZE ROW (97.3): every other Size this world offers, the
            # Textured rung (the arm whose column base the review found at a
            # constant), both resolutions, scene B, one turn - the geometry
            # is the same for every rung once px_cbias is right
            sizes = pxslib.SIZES if world == "bracket" else pxslib.WIN_SIZES
            for size in sizes:
                if size == 48:
                    continue
                for lowres in (True, False):
                    one(g, lv, "b", "tex" if texok else "flat", lowres, 1, size)
            # THE DOOR SWEEP (97.2.4; review, wave 3): the hall's door a
            # tile ahead of the eye, poked a quarter, half and three
            # quarters open - the gap through it and the slab's own texel
            # from its edge, the hit point STAYING ON THE SLAB (the first
            # cut moved it along the slab by the position, and the pinned
            # scenes hold every door shut, so 1,021 checks never saw it)
            door_sweep(g, lv, "tex" if texok else "flat")
            g.pin(rung="flat", lowres=True, size=64)      # the default Size back
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

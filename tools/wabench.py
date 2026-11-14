#!/usr/bin/env python3
"""wabench - WireArena's own benches on MartyPC, read off its HUD row
(docs/plans/MULTIPLAYER-FPS-PLAN.md 16.1 the B bench, 16.7 the P bench;
WIREARENA-HEIGHTS-PLAN.md 9 the heights views A to E).

A DEVELOPMENT INSTRUMENT, not a suite row: the numbers the owner quotes
(FPS, ms a frame, R render, P present, N video writes a frame), one line a
result, each bench in an emulator of its own, four at a time.

    python3 tools/wabench.py [--tree DIR] [--level 1..5] WHAT...

WHAT: P40 P80 (the play bench), B40 B80 (the spin), O40 (the open arena's
play bench), H40 H80 (the heights arena's five views). --tree is the checkout
whose build/warena360.img is measured (default: this one); the system disk
and MartyPC are this checkout's.
"""
import sys, os, argparse, concurrent.futures as cf, subprocess, json
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser()
ap.add_argument("--tree", default=ROOT)
ap.add_argument("--level", type=int, default=0)
ap.add_argument("--one", default=None)
ap.add_argument("what", nargs="*")
a = ap.parse_args()
F = 4772727
ITEM = {"P40": (0, 40), "P80": (0, 80), "B40": (0, 40), "B80": (0, 80),
        "O40": (1, 40), "H40": (2, 40), "H80": (2, 80)}  # (arena, width)
def one(w):
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88ui
    W = 40 if w.endswith("40") else 80
    out = []
    with os88ui.boot(os.path.join(ROOT, "build/os8088-360.img"),
                     apps=os.path.join(a.tree, "build/warena360.img"), verbose=False) as ui:
        m = ui.m
        ui.path("B:/WARENA.O88"); m.advance(cycles=F // 2); m.go()
        sys.path.insert(0, os.path.join(ROOT, "tests"))
        import walift             # (the setup by its keys: the Game
        walift.start_keys(m, *ITEM[w])  # menu's Play items went)
        m.advance(cycles=F)
        if a.level:
            m.key("F%d" % a.level); m.advance(cycles=F // 4)
        def hud():
            b = m.read(0xB8000 + 24 * W * 2, W * 2)
            return bytes(b[0::2]).decode("cp437")
        if w[0] == "B":
            m.key("KeyB"); m.advance(cycles=F * 6)
            out.append((w, hud()))
        else:
            n = 5 if w[0] == "H" else 1
            for i in range(n):
                m.key("KeyP"); m.advance(cycles=int(F * 9.2))
                out.append((w, hud()))
    return out
if a.one:
    for w, h in one(a.one):
        print("%s | %s" % (w, h), flush=True)
    sys.exit(0)
me = os.path.abspath(__file__)
def run(w):
    return subprocess.run([sys.executable, me, "--tree", a.tree, "--level", str(a.level), "--one", w],
                          capture_output=True, text=True)
with cf.ThreadPoolExecutor(4) as ex:
    for r in ex.map(run, a.what):
        sys.stdout.write(r.stdout)
        if r.returncode:
            sys.stdout.write("ERR " + r.stderr[-800:] + "\n")

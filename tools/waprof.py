#!/usr/bin/env python3
"""waprof - WireArena's frame, PROFILED: exclusive guest cycles per routine
over whole frames of one of its benches (or of live play), by single-stepping
MartyPC - every instruction's cycles go to the label it sits under, in the
game (G:name), the renderer part (name) or neither (kernel/other: the timer
tick, the kernel's calls). About two host seconds a frame.

A DEVELOPMENT INSTRUMENT, not a suite row (docs/plans/MULTIPLAYER-FPS-PLAN.md
16.9: what a frame of the P bench is made of).

    python3 tools/waprof.py [--tree DIR] [--what P40|P80|O40|H40|H80]
            [--pose 0..4] [--frames N] [--level 1..5] [--detail PREFIX]
            [--live SECONDS] [--out f.json]

--pose picks the heights arena's view (0 = A .. 4 = E). --detail PREFIX adds
a second table by EVERY label (local and macro-local ones too: `..@NN.name`
is a label inside a macro) under routines whose name starts with PREFIX
(`wa_` the renderer's, `G:` the game's). --live N profiles live play N guest
seconds in, no bench: the bots think and shoot, so it is not repeatable to
the cycle. The maps are assembled here and compared byte for byte with the
build, so a stale build is refused.
"""
import sys, os, re, struct, subprocess, tempfile, json, argparse, collections, bisect, time
ap = argparse.ArgumentParser()
ap.add_argument("--tree", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ap.add_argument("--what", default="P40")
ap.add_argument("--pose", type=int, default=0)   # heights: 0=A..4=E
ap.add_argument("--frames", type=int, default=3)
ap.add_argument("--level", type=int, default=0)
ap.add_argument("--warm", type=float, default=2.0)
ap.add_argument("--detail", default="")
ap.add_argument("--out", default="")
ap.add_argument("--poke", action="append", default=[],
                help="R:SYM=BYTE (the renderer's) or G:SYM=BYTE (the game's), "
                     "written before the profile - an A/B on a flag")
ap.add_argument("--live", type=float, default=0.0, help="no bench: profile live play after this many guest seconds (bots think and shoot)")
a = ap.parse_args()
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88ui
from os88pkg import PKG_FMT
F = 4772727
ITEM = {"P40": (0, 40), "P80": (0, 80), "O40": (1, 40), "H40": (2, 40),
        "H80": (2, 80)}  # (arena, width) - or A<n>-<width>, any arena
import re as _re
_g = _re.match(r"A(\d+)-(\d+)$", a.what)
if _g:
    ITEM[a.what] = (int(_g.group(1)), int(_g.group(2)))
T = os.path.abspath(a.tree); B = os.path.join(T, "build")
if T == ROOT:                       # (P, the play bench: a test build's since
    sys.path.insert(0, os.path.join(ROOT, "tests"))  # NET-PLAN 9.9 - this
    import walift                   # checkout's WABENCH=1 tree; another
    B = walift.bench_build()        # checkout's build/ made with it)
def amap(path, binp):
    d = tempfile.mkdtemp(); mp, bn = os.path.join(d, "m"), os.path.join(d, "b")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-DWA_BENCH", "-I", "apps/", "-I", "apps/wirearena/",
                           "-I", "drivers/net/", "-I", B + "/", "--before", "[map all %s]" % mp, "-o", bn, path], cwd=T)
    if open(bn, "rb").read() != open(binp, "rb").read():
        raise SystemExit("waprof: %s is not its source - make wirearenadisk first" % binp)
    o = {}
    for line in open(mp):
        f = line.split()
        if len(f) == 3 and re.match(r'^[0-9A-F]+$', f[0]) and re.match(r'^[0-9A-F]+$', f[1]):
            o[f[2]] = int(f[0], 16)
    return o
g = amap("apps/wirearena/wirearena.asm", B + "/wirearena.bin")
r = amap("apps/wirearena/wrpart.asm", B + "/wrpart.bin")
def table(mp, glob):
    it = sorted((v, k) for k, v in mp.items() if (("." not in k and not k.startswith("..@")) if glob else True))
    return [x[0] for x in it], [x[1] for x in it]
gA, gN = table(g, True); rA, rN = table(r, True)
gA2, gN2 = table(g, False); rA2, rN2 = table(r, False)
gRev = {v: k for v, k in zip(gA, gN)}; rRev = {v: k for v, k in zip(rA, rN)}
gSet = set(gA); rSet = set(rA)
W = 40 if a.what.endswith("40") else 80
with os88ui.boot(os.path.join(ROOT, "build/os8088-360.img"), apps=B + "/wirerena360.img", verbose=False) as ui:
    m = ui.m
    ui.path("B:/WIRERENA.O88"); m.advance(cycles=F // 2); m.go()
    sys.path.insert(0, os.path.join(ROOT, "tests"))
    import walift             # (the setup by its keys: the Game
    walift.start_keys(m, *ITEM[a.what])  # menu's Play items went)
    m.advance(cycles=F)
    if a.level:
        m.key("F%d" % a.level); m.advance(cycles=F // 4)
    if a.level == 5:          # (Very High turns vsync on: off again,
        m.key("KeyV"); m.advance(cycles=F // 4)   # so its numbers compare)
    buf = m.read(0x10000, 0x90000)
    b = [0x10000 + o for o in range(0, len(buf) - 32, 16)
         if buf[o:o + 2] == b'O8' and buf[o + 2] == PKG_FMT and buf[o + 16:o + 26] == b'Wire Arena'][-1]
    if a.live:
        m.advance(cycles=int(F * a.live))
    else:
        for i in range(a.pose + 1):
            m.key("KeyP"); m.advance(cycles=int(F * (9.2 if i < a.pose else a.warm)))
    rs = struct.unpack('<H', m.read(b + g["wa_rds"], 2))[0] * 16
    for pk in a.poke:
        where, rest = pk.split(":", 1)
        sname, val = rest.split("=")
        m.write((rs + r[sname]) if where == "R" else (b + g[sname]),
                bytes([int(val, 0)]))
        m.advance(cycles=F // 4)
    gimg = os.path.getsize(B + "/wirearena.bin"); rimg = os.path.getsize(B + "/wrpart.bin")
    # run to the game's wa_render entry
    ent = b + g["wa_render"]
    m.bp_exec(ent); m.go(); assert m.wait_stop(30) is not None; m.bp_exec()
    st = m.status()
    prev = (int(st["cs"]) << 4) + int(st["ip"]); pc = int(st["cycles"])
    excl = collections.Counter(); det = collections.Counter(); cnt = collections.Counter()
    frames = 0; t0 = time.time(); n = 0; c0 = pc
    def name(lin, d=False):
        if b <= lin < b + gimg:
            A, N = (gA2, gN2) if d else (gA, gN); o = lin - b
            i = bisect.bisect_right(A, o) - 1
            return "G:" + N[i]
        if rs <= lin < rs + rimg:
            A, N = (rA2, rN2) if d else (rA, rN); o = lin - rs
            i = bisect.bisect_right(A, o) - 1
            return N[i]
        return "kernel/other"
    while True:
        st = m.step(1)
        lin = (int(st["cs"]) << 4) + int(st["ip"]); c = int(st["cycles"])
        nm = name(prev)
        excl[nm] += c - pc
        if a.detail and nm.startswith(a.detail):
            det[name(prev, True)] += c - pc
        prev, pc = lin, c; n += 1
        if lin == ent:
            frames += 1
            if frames >= a.frames: break
        # entries count
        if b <= lin < b + gimg:
            if (lin - b) in gSet: cnt["G:" + gRev[lin - b]] += 1
        elif rs <= lin < rs + rimg:
            if (lin - rs) in rSet: cnt[rRev[lin - rs]] += 1
    tot = pc - c0
print("frames %d instr %d cycles/frame %.0f (%.2f ms)  host %.0fs" % (frames, n, tot / frames, tot / frames / 4772.727, time.time() - t0))
for k, v in excl.most_common(60):
    print("  %-28s %8.0f clk/frame %5.1f%%  calls/frame %.2f" % (k, v / frames, 100.0 * v / tot, cnt[k] / frames))
if a.detail:
    print("--- detail", a.detail)
    for k, v in det.most_common(60):
        print("  %-40s %8.0f clk/frame" % (k, v / frames))
if a.out:
    json.dump({"excl": excl, "cnt": cnt, "frames": frames, "tot": tot}, open(a.out, "w"))

#!/usr/bin/env python3
"""watrace - exact guest cycles of WireArena's renderer pieces on MartyPC
(docs/plans/completed/WIREARENA-HEIGHTS-PLAN.md 2.1: the measured unit costs that
tools/wamodel.py prices a frame with).

SUPERSEDED by tools/waprof.py (every routine, exclusive cycles) and
tools/waposes.py (exact cycles a posed frame): performance pass 6
(docs/plans/MULTIPLAYER-FPS-PLAN.md 16.9) removed wa_leaf and made
wa_face's way into the span code indirect, so the breakpoints named below
are no longer all there. Kept for the record of how HEIGHTS-PLAN 2.1's unit
costs were taken; it refuses to run on a build without its labels.

Breakpoints at the entries of the game's wa_render and the renderer part's
wa_render, its .walked and .roomless, wa_leaf, wa_face, wa_span, wa_figures,
wa_cross and wa_present; the P bench runs, and the cycles between stops are
summed per piece. A span's cost is fitted as a + b x the columns it opened.
Run from the worktree root, on the split build:

    python3 tools/watrace.py ARENA,WIDTH 30 out.json [keys...]   (e.g. 0,40)
    python3 tools/watrace.py --analyse out.json
"""
import sys
if len(sys.argv) == 3 and sys.argv[1] == "--analyse":
    import json, collections
    ev = json.load(open(sys.argv[2]))
    # drop up to first G.render
    i0 = next(i for i, e in enumerate(ev) if e["k"] == "G.render")
    ev = ev[i0:]
    frames = sum(1 for e in ev if e["k"] == "G.render") - 1
    seg = collections.defaultdict(lambda: [0, 0])
    spans = []   # (cycles, cols opened, range cols, room?)
    faces = collections.defaultdict(lambda: [0, 0])
    room = False
    for a, b in zip(ev, ev[1:]):
        d = b["c"] - a["c"]
        k = a["k"]
        if k == "wa_render.walked": room = True
        if k == "G.render": room = False
        if k == "wa_span":
            opened = a["left"] - b["left"]
            c1 = (a["c1"] + 7) >> 4; c2 = ((a["c2"] + 7) >> 4) - 1
            spans.append((d, opened, room))
            seg["span"][0] += 1; seg["span"][1] += d
        elif k == "wa_face":
            outc = "drew->span" if b["k"] == "wa_span" else "ended"
            faces[(room, outc)][0] += 1; faces[(room, outc)][1] += d
            seg["face " + ("room " if room else "box ") + outc][0] += 1
            seg["face " + ("room " if room else "box ") + outc][1] += d
        elif k == "wa_leaf":
            outc = "->face" if b["k"] == "wa_face" else "culled"
            seg["leaf " + outc][0] += 1; seg["leaf " + outc][1] += d
        else:
            seg[k + " -> " + b["k"]][0] += 1; seg[k + " -> " + b["k"]][1] += d
    tot = sum(v[1] for v in seg.values())
    print("frames %d, cycles/frame %.0f (%.1f ms)" % (frames, tot / frames, tot / frames / 4772.727))
    for k, (n, c) in sorted(seg.items(), key=lambda x: -x[1][1]):
        print("  %-40s n/frame %5.2f  avg %6.0f clk  per frame %7.0f clk %5.2f ms" % (k, n / frames, c / n, c / frames, c / frames / 4772.727))
    # span cost vs columns: least squares
    import statistics
    xs = [s[1] for s in spans]; ys = [s[0] for s in spans]
    mx, my = statistics.mean(xs), statistics.mean(ys)
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
    a = my - b * mx
    print("span cost = %.0f + %.0f x columns opened (n=%d, mean cols %.1f)" % (a, b, len(spans), mx))
    h = collections.Counter(min(s[1], 9) for s in spans)
    print("cols opened histogram", sorted(h.items()))
    for rm in (False, True):
        ss = [s for s in spans if s[2] == rm]
        if ss:
            print(" room" if rm else " box", "spans/frame %.2f cols/frame %.2f clk/frame %.0f" % (len(ss) / frames, sum(s[1] for s in ss) / frames, sum(s[0] for s in ss) / frames))
    sys.exit(0)

import sys, os, re, struct, subprocess, tempfile, json
sys.path.insert(0, 'tools')
sys.path.insert(0, 'tests')
import os88ui
import walift                                   # (its start(): the setup)
from os88pkg import PKG_FMT
F = 4772727
item, nfr, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
pre = sys.argv[4:]
B = walift.bench_build()                        # (P: a test build's, NET-PLAN 9.9)
def amap(path, binp):
    d = tempfile.mkdtemp(); mp, bn = os.path.join(d, "m"), os.path.join(d, "b")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-DWA_BENCH", "-I", "apps/", "-I", "apps/wirearena/",
                           "-I", "drivers/net/", "-I", B + "/", "--before", "[map all %s]" % mp, "-o", bn, path])
    assert open(bn, "rb").read() == open(binp, "rb").read(), "stale " + binp
    o = {}
    for line in open(mp):
        f = line.split()
        if len(f) == 3 and re.match(r'^[0-9A-F]+$', f[0]) and re.match(r'^[0-9A-F]+$', f[1]):
            o[f[2]] = int(f[0], 16)
    return o
g = amap("apps/wirearena/wirearena.asm", B + "/wirearena.bin")
r = amap("apps/wirearena/wrpart.asm", B + "/wrpart.bin")
NAMES = ["G.render", "wa_render", "wa_render.walked", "wa_render.roomless", "wa_leaf", "wa_face",
         "wa_span", "wa_figures", "wa_cross", "wa_present"]
with os88ui.boot("build/os8088-360.img", apps="build/warena360.img") as ui:
    m = ui.m
    w = ui.path("B:/WARENA.O88"); m.advance(cycles=F // 2); m.go()
    ar, wd = (int(x) for x in item.split(","))
    walift.start(ui, w, walift.amap(bench=True), arena=ar, w40=(wd == 40))
    m.advance(cycles=F)
    for k in pre:
        m.key(k); m.advance(cycles=F // 4)
    buf = m.read(0x10000, 0x90000)
    b = [0x10000 + o for o in range(0, len(buf) - 32, 16)
         if buf[o:o + 2] == b'O8' and buf[o + 2] == PKG_FMT and buf[o + 16:o + 22] == b'WARENA'][-1]
    m.key("KeyP"); m.advance(cycles=F // 2)
    m.pause()
    rs = struct.unpack('<H', m.read(b + g["wa_rds"], 2))[0] * 16
    gone = [n for n in NAMES[1:] if n not in r]
    if gone:
        raise SystemExit("watrace: the renderer has no %s any more (pass 6) - use tools/waprof.py" % ", ".join(gone))
    pts = [b + g["wa_render"]] + [rs + r[n] for n in NAMES[1:]]
    m.bp_exec(*pts)
    rd = lambda n: struct.unpack('<h', m.read(rs + r[n], 2))[0]
    ev = []; frames = 0
    while frames <= nfr:
        m.go()
        if m.wait_stop(30) is None:
            raise SystemExit("no stop")
        st = m.status()
        lin = (int(st["cs"]) << 4) + int(st["ip"])
        k = pts.index(lin)
        e = {"k": NAMES[k], "c": int(st["cycles"]), "left": rd("wa_left")}
        if NAMES[k] == "wa_span":
            e["c1"] = rd("wa_c1"); e["c2"] = rd("wa_c2")
        if NAMES[k] == "wa_face":
            e["ax"] = int(m.regs()["ax"])
        if NAMES[k] == "G.render":
            frames += 1
        ev.append(e)
    m.bp_exec()
json.dump(ev, open(out, "w"))
print("events", len(ev), "frames", frames)

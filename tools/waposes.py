#!/usr/bin/env python3
"""waposes - WireArena POSED FRAMES on MartyPC: the exact text screen and the
exact render cycles of a fixed, seeded set of poses - the instrument of
performance pass 6 (docs/plans/MULTIPLAYER-FPS-PLAN.md 16.9).

A DEVELOPMENT INSTRUMENT, not a suite row. It boots the system disk and
WARENA, opens an arena, starts the play bench (which makes the world a
bench's: every pickup present, the bots dummies that neither think nor
shoot) and holds the bench's clock at 0 so it never ends. (The HUD's health can read 80 or
100 from one run to the next - a bot's shot before the bench starts - which
is two cells of row 23 and not the renderer.) Then, per pose, it
stops at the game's wa_render, writes the camera AND the three figures
(place, facing, animation, phase, flash, dead) straight into the game's BSS -
at the renderer's door, so no tick can move them - and records

  - text VRAM (rows 0..23) after the present: the PICTURE, byte for byte;
  - the guest cycles from the game's wa_render to its wa_present: the frame
    block's copy, the far call and the whole renderer - of a MOVED view (the
    renderer skips work while the view stands, and a pose is rendered more
    than once here: its view key is forgotten before each; --still keeps it). A render a timer tick
    landed in is run again (the BIOS's tick count says so), and the frame
    the stamps lap on is skipped, so a pose repeats to ~40 clk and a set's
    mean to ~0.002% - a 0.05% change is visible;
  - a DIVIDE FAULT, off the BIOS's INTR_FLAG (0040:006B), which int 0's
    catch-all writes: on an IBM ROM a fault masks whatever IRQ is in service.

The pose list is a function of (arena, n, seed) alone, never of the build, so
two builds' recordings compare pose for pose:

  python3 tools/waposes.py record --arena 0|1|2 --width 40|80 [--level 1..5]
          [--n 60] [--seed 1] [--keys KeyG,KeyT] [--still | --nod] [--poses LIST.json]
          [--tree DIR] [--profile p.json [--detail PREFIX]] --out f.json
  python3 tools/waposes.py compare base.json new.json [--list] [--show N]
  python3 tools/waposes.py show f.json N
  python3 tools/waposes.py sets OUTDIR [--tree DIR]     the eight reference sets
  python3 tools/waposes.py cmpsets BASEDIR NEWDIR

--tree is the checkout whose apps/wirearena sources and build/warena360.img
are measured (default: this one) - a worktree needs only `make
wirearenadisk`; MartyPC and the system disk are this checkout's.
"""
import sys, os, re, json, struct, random, argparse, subprocess, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SETS = [("a0w40", 0, 40, 60, 0), ("a0w80", 0, 80, 40, 0), ("a2w40", 2, 40, 60, 0), ("a2w80", 2, 80, 40, 0),
        ("a0w40L5", 0, 40, 40, 5), ("a2w40L5", 2, 40, 40, 5), ("a2w40L1", 2, 40, 40, 1), ("a1w40", 1, 40, 30, 0)]

def cp(b):
    return bytes([b]).decode("cp437") if 32 <= b < 255 and b != 127 else "?"

def grid(rec, W):
    v = bytes.fromhex(rec["vram"])
    return ["".join(cp(v[(r * W + c) * 2]) for c in range(W)) for r in range(24)]

def compare(a):
    A_, B_ = json.load(open(a.base)), json.load(open(a.new))
    W = A_["width"]
    assert A_["poses"] == B_["poses"], "different pose lists"
    ident = 0; cells = 0; chars = 0; worst = []; ca = cb = 0
    per = []
    for i, (x, y) in enumerate(zip(A_["frames"], B_["frames"])):
        va, vb = bytes.fromhex(x["vram"]), bytes.fromhex(y["vram"])
        d = sum(1 for k in range(0, len(va), 2) if va[k:k + 2] != vb[k:k + 2])
        dc = sum(1 for k in range(0, len(va), 2) if va[k] != vb[k])
        cells += d; chars += dc
        if d == 0: ident += 1
        else: worst.append((d, i))
        ca += x["cyc"]; cb += y["cyc"]
        per.append((y["cyc"] - x["cyc"], i))
    n = len(A_["frames"])
    print("%d poses: %d identical, %d differ; %d cells differ in all (%d by glyph, the rest attribute only)"
          % (n, ident, n - ident, cells, chars))
    print("render cycles: base %.0f  new %.0f  mean per pose %+.2f%%  (%.0f -> %.0f clk/pose)"
          % (ca, cb, 100.0 * (cb - ca) / ca, ca / n, cb / n))
    worst.sort(reverse=True)
    if worst:
        print("most-different poses (cells, pose#):", worst[:12])
    if a.list:
        for d, i in sorted(per):
            print("  pose %3d  %+7d clk  %s" % (i, d, A_["poses"][i]))
    if a.show is not None:
        i = a.show
        ga, gb = grid(A_["frames"][i], W), grid(B_["frames"][i], W)
        print("pose", i, A_["poses"][i])
        for ra, rb in zip(ga, gb):
            mark = "".join("^" if p != q else " " for p, q in zip(ra, rb))
            print(ra + " | " + rb + (" <" if ra != rb else ""))
    return 0

def show(a):
    A_ = json.load(open(a.file))
    print(A_["poses"][a.n])
    for r in grid(A_["frames"][a.n], A_["width"]):
        print(r)

def poses(arena, n, seed):
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import arenagen as A
    rnd = random.Random(seed * 1000 + arena)
    boxes = [A.bx(b) for b in A.arenas()[arena][2]]   # (any arena: a flat
    hm = A.hmax(boxes)                  # one's boxes are all at the tallest)
    nonflat = any(b.h < hm for b in boxes)
    lines = [tuple(b) for b in boxes if b.kind == A.LINE]
    out = []
    plats = [b for b in boxes if b.h < hm and b.kind not in (A.LIFT, A.LINE)
             and b.h > 40] if nonflat else []
    for k in range(n):
        feet = 0
        kind = k % 4 if nonflat else 0
        if kind == 2 and arena != 2:    # (the heights arena's corners)
            kind = 0
        if kind == 1 and plats:
            b = rnd.choice(plats)
            px = rnd.randint(b[0] + 16, b[2] - 16); py = rnd.randint(b[1] + 16, b[3] - 16)
            feet = A.floor_at(px, py, boxes, 20)
        elif kind == 2:
            px, py = rnd.choice([(700, 80), (1980, 80), (700, 1980), (1980, 1980), (1980, 480),
                                 (1980, 1560), (60, 420), (60, 1630)])
        else:
            while True:
                px, py = rnd.randint(60, 1988), rnd.randint(60, 1988)
                if not A.inside(px, py, [tuple(b) for b in boxes], 40):
                    break
        ang = rnd.randint(0, 1023)
        pitch = rnd.choice([0, 0, 0, rnd.randint(-60, 40), rnd.randint(-290, 112)])
        jump = rnd.choice([0, 0, 0, 0, rnd.randint(1, 60)]) if not nonflat else 0
        figs = []
        import math
        for f in range(3):
            # two in three: somewhere ahead in the view; else anywhere free
            for _ in range(200):
                if rnd.random() < 0.67:
                    d = rnd.choice([rnd.randint(60, 300), rnd.randint(100, 700), rnd.randint(300, 1500)])
                    th = 2 * math.pi * ang / 1024 + rnd.uniform(-0.6, 0.6)
                    fx, fy = int(px + d * math.cos(th)), int(py + d * math.sin(th))
                else:
                    fx, fy = rnd.randint(40, 2008), rnd.randint(40, 2008)
                if not (30 < fx < 2018 and 30 < fy < 2018):
                    continue
                if lines and A.inside(fx, fy, lines, 12):
                    continue
                if nonflat:
                    fz = A.floor_at(fx, fy, boxes, 8)
                    if fz >= hm or any(A.bx(b).kind == A.LIFT and b[0] - 12 < fx < b[2] + 12 and b[1] - 12 < fy < b[3] + 12 for b in boxes):
                        continue
                    if A.floor_at(fx, fy, boxes, 14) != fz and fz == 0:
                        continue
                else:
                    fz = 0
                    if A.inside(fx, fy, [tuple(b) for b in boxes], 12):
                        continue
                break
            st = rnd.choice(["run"] * 6 + ["flinch", "death", "dead", "flash"])
            anim, nfr = (4, 8)
            flash = dead = 0
            if st == "flinch": anim, nfr = 12, 3
            if st in ("death", "dead"): anim, nfr = 15, 8; dead = 40
            ph = rnd.randint(0, nfr * 4 - 1)
            if st == "dead": ph = nfr * 4 - 1
            if st == "flash": flash = 3
            figs.append(dict(x=fx, y=fy, z=fz, ang=rnd.randint(0, 1023), anim=anim, nfr=nfr, ph=ph,
                             flash=flash, dead=dead))
        out.append(dict(px=px, py=py, ang=ang, pitch=pitch, feet=feet, jump=jump, figs=figs))
    return out

def record(a):
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88ui
    from os88pkg import PKG_FMT
    F = 4772727; T = os.path.abspath(a.tree); W = a.width
    B = os.path.abspath(a.build) if a.build else os.path.join(T, "build")
    d = tempfile.mkdtemp(); mp, bn = os.path.join(d, "m"), os.path.join(d, "b")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-I", "apps/", "-I", "apps/wirearena/", "-I", "drivers/net/",
                           "-I", B + "/", "--before", "[map all %s]" % mp, "-o", bn, "apps/wirearena/wirearena.asm"], cwd=T)
    if open(bn, "rb").read() != open(B + "/wirearena.bin", "rb").read():
        raise SystemExit("waposes: %s/wirearena.bin is not its source - make wirearenadisk first" % B)
    g = {}
    for line in open(mp):
        f = line.split()
        if len(f) == 3 and re.match(r'^[0-9A-F]+$', f[0]) and re.match(r'^[0-9A-F]+$', f[1]):
            g[f[2]] = int(f[0], 16)
    src = open(os.path.join(T, "apps/wirearena/waconst.inc")).read()
    FO = {k: int(v) for k, v in re.findall(r"^(F_[A-Z]+)\s+equ\s+(\d+)", src, re.M)}
    ps = json.load(open(a.poses)) if a.poses else poses(a.arena, a.n, a.seed)
    frames = []; faults = 0
    with os88ui.boot(os.path.join(ROOT, "build/os8088-360.img"), apps=B + "/warena360.img", verbose=False) as ui:
        m = ui.m
        w = ui.path("B:/WARENA.O88"); m.advance(cycles=F // 2); m.go()
        import os88marty                # the setup's arena and width, then
        p = os88marty.Pkg(m, w)         # Enter: its Play button (the Game
        p.write(g["wa_lkarena"], bytes([a.arena]))  # menu's Play items went,
        p.write(g["wa_want"], bytes([1 if W == 40 else 0]))  # NET-PLAN 9.4)
        m.key("Enter"); m.advance(cycles=F)
        if a.level:
            m.key("F%d" % a.level); m.advance(cycles=F // 4)
        for k in [x for x in a.keys.split(",") if x]:
            m.key(k); m.advance(cycles=F // 4)
        buf = m.read(0x10000, 0x90000)
        b = [0x10000 + o for o in range(0, len(buf) - 32, 16)
             if buf[o:o + 2] == b'O8' and buf[o + 2] == PKG_FMT and buf[o + 16:o + 22] == b'WARENA'][-1]
        # the PLAY bench's start makes the world a bench's (every pickup there at
        # its first choice and none taken, the bots dummies that neither think
        # nor shoot, no keys); its clock is poked back to 0 every frame, so it
        # never ends and the world stays so - the view it sets is overwritten
        # at the renderer's door like everything else
        m.key("KeyP"); m.advance(cycles=F // 3)
        pkon = m.read(b + g["wa_pkon"], 1)
        # A MOVED VIEW: the renderer keeps the view its products were made for
        # (wa_pang) and skips work while the view stands. A pose is rendered more
        # than once here, so the measured render would be a STILL view's - which
        # play seldom is. Forgetting the key before every render makes each a
        # moved view's (--still keeps it: what a standing player's frame costs).
        pang = None
        rmap = {}
        if a.nod: a.still = True
        if not a.still or a.profile:
            mp2, bn2 = os.path.join(d, "m2"), os.path.join(d, "b2")
            subprocess.check_call(["nasm", "-f", "bin", "-w+error"] + ["-D" + x for x in a.define]
                                  + ["-I", "apps/", "-I", "apps/wirearena/",
                                     "-I", B + "/", "--before", "[map all %s]" % mp2, "-o", bn2,
                                     "apps/wirearena/wrpart.asm"], cwd=T)
            if open(bn2, "rb").read() != open(B + "/wrpart.bin", "rb").read():
                raise SystemExit("waposes: %s/wrpart.bin is not its source - make wirearenadisk first" % B)
            for line in open(mp2):
                f = line.split()
                if len(f) == 3 and re.match(r'^[0-9A-F]+$', f[0]) and re.match(r'^[0-9A-F]+$', f[1]):
                    rmap[f[2]] = int(f[0], 16)
            if not a.still:
                pang = struct.unpack('<H', m.read(b + g["wa_rds"], 2))[0] * 16 + rmap["wa_pang"]
        e_r, e_p, e_m = b + g["wa_render"], b + g["wa_present"], b + g["wa_meter"]
        prof = None
        if a.profile:                   # (--profile: the routines' cycles,
            import bisect, collections  # single-stepped, waprof's way)
            rs = struct.unpack('<H', m.read(b + g["wa_rds"], 2))[0] * 16
            rimg = os.path.getsize(B + "/wrpart.bin")
            glob = lambda k: "." not in k and not k.startswith("..@")
            rt = sorted((v, k) for k, v in rmap.items() if glob(k))
            rtd = sorted((v, k) for k, v in rmap.items())
            rA, rN = [x[0] for x in rt], [x[1] for x in rt]
            rAd, rNd = [x[0] for x in rtd], [x[1] for x in rtd]
            rEnt = {v: k for v, k in rt}
            prof = dict(excl=collections.Counter(), cnt=collections.Counter(),
                        det=collections.Counter(), n=0)
            def rname(lin, d):
                if rs <= lin < rs + rimg:
                    A, N = (rAd, rNd) if d else (rA, rN)
                    return N[bisect.bisect_right(A, lin - rs) - 1]
                return "G:/kernel"
        def run_to(addr):
            m.bp_exec(addr); m.go()
            if m.wait_stop(60) is None: raise SystemExit("no stop")
            st = m.status()
            assert (int(st["cs"]) << 4) + int(st["ip"]) == addr
            return int(st["cycles"])
        def w16(name, v): m.write(b + g[name], struct.pack('<H', v & 0xFFFF))
        for p in ps:
            last = None; dirty = []
            for rep in range(9):        # until a render (not the pose's first: it re-orders the boxes) no
                                        # timer tick landed in: those repeat to ~40 clk
                if a.nod:               # (--nod: a render a half-row of pitch away first, so the
                    run_to(e_r)         # measured one is a PITCH-ONLY move's - the view standing)
                    w16("wa_px", p["px"]); w16("wa_py", p["py"]); w16("wa_ang", p["ang"] << 6)
                    w16("wa_pitch", p["pitch"] + 4); w16("wa_feet", p["feet"]); w16("wa_jz", 0); w16("wa_jv", 0)
                    w16("wa_eye", 102 + p["feet"] + p["jump"]); w16("wa_pbtk", 0)
                    run_to(e_p); run_to(e_m)
                c0 = run_to(e_r)
                w16("wa_px", p["px"]); w16("wa_py", p["py"]); w16("wa_ang", p["ang"] << 6)
                w16("wa_pitch", p["pitch"]); w16("wa_feet", p["feet"]); w16("wa_jz", 0); w16("wa_jv", 0)
                w16("wa_eye", 102 + p["feet"] + p["jump"])
                w16("wa_pbtk", 0)
                lap = m.read(b + g["wa_frame"], 1)[0] >= 126   # the stamps lap next frame: 28k clk of clears
                m.write(b + g["wa_pkon"], pkon); m.write(b + g["wa_pdead"], bytes(1))
                w16("wa_hp", 100)
                for fi, f in enumerate(p["figs"]):
                    fb = b + g["wa_figs"] + fi * FO["F_SIZE"]
                    m.write(fb + FO["F_X"], struct.pack('<HHH', f["x"], f["y"], f["ang"]))
                    m.write(fb + FO["F_ANIM"], bytes([f["anim"], f["nfr"], 1]) + struct.pack('<H', f["ph"])
                            + bytes([f["flash"], f["dead"]]) + struct.pack('<H', f["z"]))
                if pang is not None: m.write(pang, b"\xff\xff")
                m.write(0x46B, bytes(1))      # the BIOS's INTR_FLAG: int 0's catch-all writes it
                t0 = m.read(0x46C, 2)
                c1 = run_to(e_p)
                clean = m.read(0x46C, 2) == t0
                c2 = run_to(e_m)
                if rep >= (2 if a.still else 1) and not lap:   # (a still view's: past its first still frame,
                                                               # which sets up what the later ones keep)
                    if clean:
                        last = c1 - c0
                        break
                    dirty.append(c1 - c0)
            if prof is not None:        # once more, stepped (its own render:
                run_to(e_r)             # the timed one stays exact)
                w16("wa_px", p["px"]); w16("wa_py", p["py"]); w16("wa_ang", p["ang"] << 6)
                w16("wa_pitch", p["pitch"]); w16("wa_feet", p["feet"]); w16("wa_jz", 0); w16("wa_jv", 0)
                w16("wa_eye", 102 + p["feet"] + p["jump"]); w16("wa_pbtk", 0)
                for fi, f in enumerate(p["figs"]):
                    fb = b + g["wa_figs"] + fi * FO["F_SIZE"]
                    m.write(fb + FO["F_X"], struct.pack('<HHH', f["x"], f["y"], f["ang"]))
                    m.write(fb + FO["F_ANIM"], bytes([f["anim"], f["nfr"], 1]) + struct.pack('<H', f["ph"])
                            + bytes([f["flash"], f["dead"]]) + struct.pack('<H', f["z"]))
                if pang is not None: m.write(pang, b"\xff\xff")
                st = m.status(); prev = (int(st["cs"]) << 4) + int(st["ip"]); pc = int(st["cycles"])
                while True:
                    st = m.step(1)
                    lin = (int(st["cs"]) << 4) + int(st["ip"]); c = int(st["cycles"])
                    nm = rname(prev, False)
                    prof["excl"][nm] += c - pc
                    if a.detail and nm.startswith(a.detail):
                        prof["det"][rname(prev, True)] += c - pc
                    if rs <= lin < rs + rimg and (lin - rs) in rEnt:
                        prof["cnt"][rEnt[lin - rs]] += 1
                    prev, pc = lin, c
                    if lin == e_p:
                        break
                prof["n"] += 1
                run_to(e_m)
            flt = m.read(0x46B, 1)[0]
            if flt:
                faults += 1
                print("DIVIDE FAULT (INTR_FLAG %02X) at pose %d: %r" % (flt, len(frames), p), flush=True)
            vr = m.read(0xB8000, W * 24 * 2)
            frames.append(dict(vram=bytes(vr).hex(), cyc=last if last is not None else min(dirty) - 3130,   # (a long render never misses the tick: less its ~3,130)
                                pres=c2 - c1, fault=flt))
        m.bp_exec()
    json.dump(dict(arena=a.arena, width=W, level=a.level, seed=a.seed, poses=ps, frames=frames), open(a.out, "w"))
    if prof is not None:
        json.dump(prof, open(a.profile, "w"))
        tot = sum(prof["excl"].values())
        print("PROFILE over %d poses, %.0f clk a pose stepped (the game's own part of the frame "
              "block's copy is G:/kernel):" % (prof["n"], tot / prof["n"]))
        for k, v in prof["excl"].most_common(40):
            print("  %-22s %8.0f clk/pose %5.1f%%  calls/pose %6.2f" % (k, v / prof["n"], 100.0 * v / tot,
                                                                     prof["cnt"][k] / prof["n"]))
        if a.detail:
            print("--- detail", a.detail)
            for k, v in prof["det"].most_common(50):
                print("  %-40s %8.0f clk/pose" % (k, v / prof["n"]))
    n = len(frames)
    print("%s: %d poses, %d DIVIDE FAULTS, render %.0f clk/pose (%.2f ms), still-frame present %.0f" % (
        a.out, n, faults, sum(f["cyc"] for f in frames) / n, sum(f["cyc"] for f in frames) / n / 4772.727,
        sum(f["pres"] for f in frames) / n))

ap = argparse.ArgumentParser()
sub = ap.add_subparsers(dest="cmd")
r = sub.add_parser("record")
r.add_argument("--arena", type=int, default=0); r.add_argument("--width", type=int, default=40)
r.add_argument("--level", type=int, default=0); r.add_argument("--n", type=int, default=60)
r.add_argument("--seed", type=int, default=1)
r.add_argument("--keys", default=""); r.add_argument("--tree", default=ROOT)
r.add_argument("--build", default="", help="the build directory measured (default: TREE/build) - a knob build's "
               "private one, `make BUILD=dir wirearenadisk WADASH=1`")
r.add_argument("--define", action="append", default=[], help="a define the renderer part of --build was "
               "assembled with (WA_DASH=1), for its map")
r.add_argument("--profile", default="", help="also each pose rendered once more SINGLE-STEPPED: the "
               "renderer's cycles by routine over the set, into this JSON (and printed) - ~2 host s a pose")
r.add_argument("--detail", default="", help="with --profile: by every label under routines named so")
r.add_argument("--out", required=True)
r.add_argument("--still", action="store_true", help="a standing view: the renderer's still-view work kept")
r.add_argument("--nod", action="store_true", help="a standing view whose PITCH alone moved since the frame before")
r.add_argument("--poses", default="", help="a JSON list of poses to use instead of the seeded set (poses()'s own shape)")
c = sub.add_parser("compare")
c.add_argument("base"); c.add_argument("new"); c.add_argument("--list", action="store_true")
c.add_argument("--show", type=int, default=None)
s = sub.add_parser("show"); s.add_argument("file"); s.add_argument("n", type=int)
t = sub.add_parser("sets"); t.add_argument("outdir"); t.add_argument("--tree", default=ROOT)
u = sub.add_parser("cmpsets"); u.add_argument("basedir"); u.add_argument("newdir")
a = ap.parse_args()

def sets(a):
    """the eight reference sets, four emulators at a time"""
    os.makedirs(a.outdir, exist_ok=True)
    for i in range(0, len(SETS), 4):
        ps = [subprocess.Popen([sys.executable, os.path.abspath(__file__), "record", "--arena", str(ar),
                                "--width", str(w), "--n", str(n), "--level", str(lv), "--tree", a.tree,
                                "--out", os.path.join(a.outdir, name + ".json")])
              for name, ar, w, n, lv in SETS[i:i + 4]]
        if any(p.wait() for p in ps):
            return 1

def cmpsets(a):
    for name, *_ in SETS:
        print("==", name)
        a.base, a.new = os.path.join(a.basedir, name + ".json"), os.path.join(a.newdir, name + ".json")
        a.list, a.show = False, None
        compare(a)

if not a.cmd:
    ap.print_help(); sys.exit(2)
sys.exit({"record": record, "compare": compare, "show": show, "sets": sets, "cmpsets": cmpsets}[a.cmd](a) or 0)

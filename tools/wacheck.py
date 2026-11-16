#!/usr/bin/env python3
"""wacheck - WireArena's wall edges against a host reference, pose by pose.

A DEVELOPMENT INSTRUMENT, not a suite row (docs/plans/MULTIPLAYER-FPS-PLAN.md
16.5). It boots the system disk and WIRERENA under MartyPC, opens 40 columns,
then for each random pose (clear of every box) writes the player's position
and angle straight into the package's BSS, lets half a guest second draw, and
reads text VRAM at B800 - `screen()` drops the box-drawing glyphs, so it would
miss every `─` edge. Per column it compares the topmost and bottommost edge
glyph (`─` 0xC4, `_` 0x5F) with a reference: the nearest front-facing wall a
ray through the column's centre meets, projected with the guest's own scales
(75 degrees - the default - eye 1.6 m, pitch 0). A column with a figure in it is skipped. A
pose is reported when an edge is two rows or more off.

The BSS addresses come from a map of the build it boots, assembled here and
compared byte for byte with build/wirearena.bin - an address from an older
map writes the pose into the wrong bytes, and that once made a fix look like
a regression for an hour.

    make wirearenadisk && python3 tools/wacheck.py [--seed N] [--poses N]

It found the far-wall precision bug: 15 poses in 30 with an edge two rows or
more off before the fix, 5 in 30 after (single columns at a near corner).

    python3 tools/wacheck.py --arena 2 [--poses N] [--width 40|80]

HEIGHTS (docs/plans/completed/WIREARENA-HEIGHTS-PLAN.md wave 2): the heights arena, its
boxes at their heights, the reference cast per column as a ray through the
boxes' footprints, nearest first - a box the eye is below the top of hides
its column above as far as its top and no further; one the eye is above
shows its top's far edge too; a wall at the arena's tallest (and the room)
hides the rest. A RAMP (kind FLIGHT, HEIGHTS-PLAN 7.4.2) is cast as the
slope it is - its surface a plane, so a column's rows over it run between
where the ray enters it and where it leaves - which is what the renderer
draws by default (its stepped look is a graphics option, T, not checked
here). Poses: the free floor, the platform's top, the tower's, the room's
corners (where every box is in view), each a random heading and a pitch
anywhere in the clamp (WA_PITCHU up, WA_PITCHD down). Every expected edge (the topmost and bottommost of a front face,
the far top) must have a drawn edge (`─` `_` `/` and backslash) within a row, and
every drawn edge an expected one: a column fails either way.

Each heights pose also fails on a DIVIDE FAULT: the BIOS's catch-all handler,
which int 0 reaches on a shipped kernel, writes INTR_FLAG (0040:006B), and
this clears it before the pose and reads it after. The fault is silent on
MartyPC's GLaBIOS; the IBM ROM's handler MASKS whatever IRQ is in service at
the time, so on a 5150 the timer, the keyboard and the mouse die one by one
(HEIGHTS-PLAN 9.5: lifts taken for flights, a zero tread count).
"""
import argparse, math, os, random, struct, subprocess, sys, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import arenagen as A                      # noqa: E402
import os88ui                             # noqa: E402
sys.path.insert(0, os.path.join(ROOT, "tests"))
import walift                             # noqa: E402  (its start())
from os88pkg import PKG_FMT              # noqa: E402

W = 40
FOVF = 334                  # 75 degrees, the default (wa_fovf): the guest's
K16 = (8 * W * FOVF) >> 8   # own integer arithmetic, from wa_setfov
KYQ = (133 * FOVF) >> 8
HQ = 96
NUMT = (192 - 102) * KYQ
NUMB = 102 * KYQ
SIN = [int(round(16384 * math.sin(2 * math.pi * j / 1024))) for j in range(1024)]
VARS = dict(px="wa_px", py="wa_py", ang="wa_ang", pitch="wa_pitch",
            jz="wa_jz", jv="wa_jv", eye="wa_eye", feet="wa_feet",
            treads="wa_treads")
GLYPH = {0xC4: '-', 0x5F: '_', 0xB3: '|', 0x2F: '/', 0x5C: '\\'}
FIGURE = (0xDB, 0xDF, 0xDC, 0xDD, 0xDE)


def offsets():
    d = tempfile.mkdtemp(prefix="wacheck")
    mp, bn = os.path.join(d, "wa.map"), os.path.join(d, "wa.bin")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                           "-I", "apps/wirearena/", "-I", "drivers/net/",
                           "-I", "build/", "--before",
                           "[map all %s]" % mp, "-o", bn,
                           "apps/wirearena/wirearena.asm"], cwd=ROOT)
    if open(bn, "rb").read() != open(os.path.join(ROOT, "build/wirearena.bin"), "rb").read():
        raise SystemExit("wacheck: build/wirearena.bin is not this source - "
                         "make wirearenadisk first")
    out = {}
    for line in open(mp):
        f = line.split()
        if len(f) == 3 and f[2] in VARS.values():
            out[[k for k, v in VARS.items() if v == f[2]][0]] = int(f[0], 16)
    if len(out) != len(VARS):
        raise SystemExit("wacheck: the map lacks %r" % (set(VARS) - set(out)))
    return out


def walls():
    x0, y0, x1, y1 = A.ROOM
    out = [((x0, y0), (x0, y1)), ((x0, y1), (x1, y1)),
           ((x1, y1), (x1, y0)), ((x1, y0), (x0, y0))]
    for (a, b, c, d) in A.BOXES:
        out += [((a, b), (c, b)), ((c, b), (c, d)), ((c, d), (a, d)),
                ((a, d), (a, b))]
    return out


def expect(px, py, a, ws):
    s, c = SIN[a & 1023], SIN[(a + 256) & 1023]

    def cam(p):
        dx, dy = p[0] - px, p[1] - py
        return (dx * s - dy * c) / 16384.0, (dx * c + dy * s) / 16384.0
    res = []
    for col in range(W):
        r = (16 * col + 8 - 8 * W) / K16
        best = None
        for (p, q) in ws:
            if (px - p[0]) * (q[1] - p[1]) - (py - p[1]) * (q[0] - p[0]) <= 0:
                continue                      # seen from behind
            (x1, z1), (x2, z2) = cam(p), cam(q)
            den = (x2 - x1) - r * (z2 - z1)
            if abs(den) < 1e-9:
                continue
            t = (r * z1 - x1) / den
            if not 0 <= t <= 1:
                continue
            z = z1 + t * (z2 - z1)
            if z > 0.5 and (best is None or z < best):
                best = z
        if best is None:
            res.append(None)
            continue
        t, b = HQ - NUMT / best, HQ + NUMB / best
        res.append((math.floor((t * 8 + 16) / 32), math.floor((b * 8 + 16) / 32), best))
    return res


def hboxes():
    """The heights arena's boxes for the cast: (x0, y0, x1, y1, h, height
    at a point). A RAMP (a FLIGHT, drawn by default as straight sloped
    sides, HEIGHTS-PLAN 7.4.2) is cast as the slope it is: linear from 0 at
    its low face to h at the face it rises toward."""
    out = []
    for b in A.HEIGHTS_BOXES:
        b = A.bx(b)
        x0, y0, x1, y1 = b
        if b.kind != A.FLIGHT:
            out.append((x0, y0, x1, y1, b.h, lambda x, y, h=b.h: h))
            continue
        f = {"N": lambda x, y, b=b: b.h * (y - b[1]) / (b[3] - b[1]),
             "S": lambda x, y, b=b: b.h * (b[3] - y) / (b[3] - b[1]),
             "E": lambda x, y, b=b: b.h * (x - b[0]) / (b[2] - b[0]),
             "W": lambda x, y, b=b: b.h * (b[2] - x) / (b[2] - b[0])}[b.toward]
        out.append((x0, y0, x1, y1, b.h, lambda x, y, f=f: min(b.h, max(0, f(x, y)))))
    return out


def expect_h(px, py, a, eye, pitch, boxes, w, hmax, kyq, k16):
    """-> per column (expected edge half-rows, a flight met first?)"""
    s, c = SIN[a & 1023] / 16384.0, SIN[(a + 256) & 1023] / 16384.0
    hq = 96 + pitch

    def hr(h, z):
        return math.floor((hq - (h - eye) * kyq / z + 2) / 4)
    res = []
    for col in range(w):
        r = (16 * col + 8 - 8 * w) / k16
        # the ray: camera (r z, z); in world, from (px, py) along d
        dx, dy = r * s + c, -r * c + s       # (camera x = dx s - dy c ...)
        hits = []
        for (x0, y0, x1, y1, h, hf) in boxes:
            t0, t1 = -1e18, 1e18
            for o, d, lo, hi in ((px, dx, x0, x1), (py, dy, y0, y1)):
                if abs(d) < 1e-12:
                    if not lo <= o <= hi:
                        t0 = 1e18
                    continue
                ta, tb = (lo - o) / d, (hi - o) / d
                t0, t1 = max(t0, min(ta, tb)), min(t1, max(ta, tb))
            if t0 < t1 and t1 > 0:
                ti = max(t0, 0.25)
                hits.append((t0, t1, h, hf(px + dx * ti, py + dy * ti),
                             hf(px + dx * t1, py + dy * t1)))
        hits.sort()
        exp, clip, flight = set(), 48, False
        for t0, t1, h, h0, h1 in hits:
            zi, zo = max(t0, 0.25), t1           # (z along the ray: |d| is 1
            if t0 > 0.25:                        # in camera z, by construction)
                for e in (hr(0, zi), hr(h0, zi)):
                    if 0 <= e < 48 and e < clip:
                        exp.add(e)
            tall = h >= hmax and eye < h
            if tall:
                clip = -1
                break
            top = hr(h0, zi) if t0 > 0.25 else 48
            if eye > h1:                         # (a ramp's surface is a plane:
                e = hr(h1, zo)                   # its rows run between its ends)
                if 0 <= e < 48 and e < min(clip, top):
                    exp.add(e)
                top = min(top, e)
            clip = min(clip, top)
            if clip <= 0:
                break
        if clip > 0:                             # the room
            x0, y0, x1, y1 = A.ROOM
            ts = []
            for o, d, lo, hi in ((px, dx, x0, x1), (py, dy, y0, y1)):
                if abs(d) > 1e-12:
                    ts.append(max((lo - o) / d, (hi - o) / d))
            z = min(ts)
            for e in (hr(0, z), hr(A.WALL_H, z)):
                if 0 <= e < 48 and e < clip:
                    exp.add(e)
        res.append((exp, flight))
    return res


def main_heights(a, off):
    W = a.width
    fovf = 334
    k16 = (8 * W * fovf) >> 8
    kyq = (133 * fovf) >> 8
    boxes = hboxes()
    hmax = A.hmax(A.HEIGHTS_BOXES)
    random.seed(a.seed)
    F = 4772727
    plats = [A.bx(b) for b in A.HEIGHTS_BOXES       # (and the ramps)
             if A.bx(b).h < hmax and A.bx(b).kind != A.LIFT and A.bx(b).h > 40]
    stats = dict(poses=0, bad=0, badnf=0, cols=0, off=0, offnf=0, faults=0)
    with os88ui.boot(os.path.join(ROOT, "build/os8088-360.img"),
                     apps=os.path.join(ROOT, "build/wirerena360.img"),
                     machine=a.machine) as ui:
        m = ui.m
        w = ui.path("B:/WIRERENA.O88")
        m.advance(cycles=F // 2)
        m.go()
        walift.start(ui, w, walift.amap(), w40=(W == 40))
        m.advance(cycles=F * 2)
        if a.level:
            m.key("Digit%d" % a.level)
            m.advance(cycles=F // 2)
        buf = m.read(0x10000, 0x90000)
        base = None
        for o in range(0, len(buf) - 32, 16):
            if buf[o:o + 2] == b'O8' and buf[o + 2] == PKG_FMT and \
                    buf[o + 16:o + 26] == b'Wire Arena':
                base = 0x10000 + o
        for n in range(a.poses):
            kind = n % 4                 # floor, a platform's top, a corner,
            feet = 0                     # the floor again
            if kind == 1:
                b = random.choice(plats)
                px = random.randint(b[0] + 16, b[2] - 16)
                py = random.randint(b[1] + 16, b[3] - 16)
                feet = A.floor_at(px, py, A.HEIGHTS_BOXES, 20)  # (wa_hjump's)
            elif kind == 2:
                px, py = random.choice([(700, 80), (1980, 80), (700, 1980), (1980, 1980),
                                        (1980, 480), (1980, 1560), (60, 420),
                                        (60, 1630)])
            else:
                while True:
                    px, py = random.randint(60, 1988), random.randint(60, 1988)
                    if not A.inside(px, py, A.HEIGHTS_BOXES, 40):
                        break
            ang = random.randint(0, 1023)
            pitch = random.choice([0, 0, random.randint(-60, 40), random.randint(-290, 112)])
            eye = 102 + feet
            m.pause()
            for k, v in (('px', px), ('py', py), ('ang', (ang << 6) & 0xFFFF),
                         ('pitch', pitch & 0xFFFF), ('jz', 0), ('jv', 0),
                         ('feet', feet), ('eye', eye)):
                m.write(base + off[k], struct.pack('<H', v))
            m.write(base + off['treads'], bytes([a.treads]))
            m.write(0x46B, b'\0')       # the BIOS's INTR_FLAG: its catch-all
            m.advance(cycles=F // 2)     # handler, int 0's, writes it
            vr = m.read(0xB8000, W * 25 * 2)
            if m.read(0x46B, 1)[0]:
                stats["faults"] += 1
                print("pose %d: px=%d py=%d a=%d pitch=%d eye=%d: an UNEXPECTED "
                      "INTERRUPT (a divide fault) - INTR_FLAG %02X"
                      % (n, px, py, ang, pitch, eye, m.read(0x46B, 1)[0]))
            exp = expect_h(px, py, ang, eye, pitch, boxes, W, hmax, kyq, k16)
            badc, badnf = [], []
            for c in range(W):
                want, fl = exp[c]
                cells = [vr[(r * W + c) * 2] for r in range(24)]
                if any(ch in FIGURE for ch in cells):
                    continue
                got = {r for r in range(24) if cells[r] in (0xC4, 0x5F, 0x2F, 0x5C)}
                rows = {e >> 1 for e in want}
                if c == W // 2:                  # (the crosshair's cell)
                    rows.discard(12)
                miss = [r for r in rows if not any(abs(r - g) <= 1 for g in got)]
                extra = [g for g in got if cells[g] in (0xC4, 0x5F)
                         and not any(abs(r - g) <= 1 for r in rows)]
                stats["cols"] += 1
                if miss or extra:
                    stats["off"] += 1
                    badc.append((c, sorted(rows), sorted(got)))
                    if not fl:
                        stats["offnf"] += 1
                        badnf.append(c)
            stats["poses"] += 1
            if badc:
                stats["bad"] += 1
            if badnf:
                stats["badnf"] += 1
                print("pose %d: px=%d py=%d a=%d pitch=%d eye=%d: %d column(s) off "
                      "(%d at a flight's side), e.g. (col, want rows, got) %s"
                      % (n, px, py, ang, pitch, eye, len(badc), len(badc) - len(badnf),
                         [x for x in badc if x[0] in badnf][:3]))
    print("wacheck heights: %d poses, %d with a column off (%d not at a flight's "
          "side); %d of %d columns off (%d not at a flight's side); %d with a "
          "divide fault"
          % (stats["poses"], stats["bad"], stats["badnf"], stats["off"], stats["cols"],
             stats["offnf"], stats["faults"]))
    return 1 if stats["badnf"] or stats["faults"] else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--arena", type=int, default=0)
    ap.add_argument("--width", type=int, default=40)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--poses", type=int, default=30)
    ap.add_argument("--machine", default="os8088_5150_cga")
    ap.add_argument("--treads", type=int, default=0,
                    help="1: ramps drawn as stepped treads (the T option) - "
                         "for its divide faults: the reference is the ramp, so "
                         "its columns off are expected")
    ap.add_argument("--level", type=int, default=0,
                    help="1..5: the detail level, Very Low to Very High (its "
                         "key, after the menu: HEIGHTS-PLAN 9.11); 0 the "
                         "default. A level's look changes (ROOMF's missing "
                         "room tops, say) count as columns off")
    a = ap.parse_args()
    off = offsets()
    if a.arena == 2:
        return main_heights(a, off)
    ws = walls()
    random.seed(a.seed)
    F = 4772727
    bad = 0
    with os88ui.boot(os.path.join(ROOT, "build/os8088-360.img"),
                     apps=os.path.join(ROOT, "build/wirerena360.img"),
                     machine=a.machine) as ui:
        m = ui.m
        w = ui.path("B:/WIRERENA.O88")
        m.advance(cycles=F // 2)
        m.go()
        walift.start(ui, w, walift.amap(), arena=0)
        m.advance(cycles=F * 2)
        if a.level:
            m.key("Digit%d" % a.level)
            m.advance(cycles=F // 2)
        buf = m.read(0x10000, 0x90000)
        base = None
        for o in range(0, len(buf) - 32, 16):
            if buf[o:o + 2] == b'O8' and buf[o + 2] == PKG_FMT and \
                    buf[o + 16:o + 26] == b'Wire Arena':
                base = 0x10000 + o
        if base is None:
            raise SystemExit("wacheck: WIRERENA is not in memory")
        for n in range(a.poses):
            while True:
                px, py = random.randint(60, 1988), random.randint(60, 1988)
                if not A.inside(px, py, A.BOXES, 40):
                    break
            ang = random.randint(0, 1023)
            m.pause()
            for k, v in (('px', px), ('py', py), ('ang', (ang << 6) & 0xFFFF),
                         ('pitch', 0), ('jz', 0), ('jv', 0), ('eye', 102)):
                m.write(base + off[k], struct.pack('<H', v))
            m.advance(cycles=F // 2)
            vr = m.read(0xB8000, W * 25 * 2)
            exp = expect(px, py, ang, ws)
            worst = []
            for c in range(W):
                if exp[c] is None:
                    continue
                cells = [vr[(r * W + c) * 2] for r in range(24)]
                if any(ch in FIGURE for ch in cells):
                    continue
                edge = [r for r in range(24) if cells[r] in (0xC4, 0x5F)]
                if not edge:
                    continue
                ht, hb, z = exp[c]
                d = max(abs(edge[0] - (ht >> 1)) if 0 <= ht < 48 else 0,
                        abs(edge[-1] - (hb >> 1)) if 0 <= hb < 48 else 0)
                if d >= 2:
                    worst.append((c, ht >> 1, edge[0], hb >> 1, edge[-1], round(z)))
            if worst:
                bad += 1
                print("pose %d: px=%d py=%d a=%d  %d column(s) off, e.g. "
                      "(col, want top, got, want bottom, got, depth) %s"
                      % (n, px, py, ang, len(worst), worst[:4]))
    print("wacheck: %d poses, %d with an edge two rows or more off"
          % (a.poses, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

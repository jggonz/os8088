#!/usr/bin/env python3
"""waflines - WireArena's FLOOR LINES against an exact referee
(docs/plans/WIREARENA-ARENAS-PLAN.md 4.5).

The floor-line drawer (apps/wirearena/wrender.inc, wa_flines) cuts each line
at the screen's edges in camera space and walks it by an error term. This
row holds its PICTURE to a referee that shares none of its arithmetic: the
game's own integer camera space, then the line's image in exact rationals -
which cells an exact drawer plots. Nine fixed poses of the twin, rendered
through tools/waposes.py (the game's wa_render at a poked camera, text VRAM
read back):

  stray     every floor-line cell on the screen lies within a cell of the
            referee's line (every pose) - a wrong end, a wrong step, a line
            drawn through a tower
  cover     where no box is in the view, every cell of the referee's line
            has a floor-line cell within a cell of it - a line dropped, cut
            short, or stopped at a row
  hidden    behind the west lift, the lines beyond the tower are NOT drawn
            inside the tower's face (wa_flhid, and the run it skips)
  exact     of all the floor-line cells drawn, 85 in 100 are the referee's
            own cell and dash - a walk that starts a fraction out lies within
            a cell of the line everywhere, and beside it
  still     the same poses as a STILL view (drawn from the walk the first
            still frame kept) and as a NOD (the pitch alone moved: the rows'
            pass, the vertices' camera space kept) draw the moved view's
            floor-line cells, every one
  dashed    at Very Low (F1: wa_dash 2) a shallow line's dashes stand in the
            even columns alone and a steep line's cells in the even rows,
            still on the referee's lines, about half of Medium's cells
  faults    no divide fault in any render (waposes reads the BIOS's flag)

A floor-line cell is one of the five line glyphs in the room's colour; the
towers are another colour, so the two never mix.

    python3 tests/waflines.py [--keep FILE]
"""
import sys, os, json, math, subprocess, tempfile
from fractions import Fraction as F
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import arenagen as A

W, F256 = 40, 334               # 40 columns, the default 75 degrees
ARENA, TAG = 4, "a4"
GLYPHS = {0xC4: "-", 0x5F: "_", 0xB3: "S", 0x5C: "S", 0x2F: "S"}
A_ROOM = 0x01                   # the room's colour on a colour card (wrtab.inc)
EXACT = 85                      # cells in 100 that must be the referee's own
                                # (measured 90; a walk a modulus out reads 67)
AWAY = dict(x=1024, y=80, z=0, ang=0, anim=4, nfr=8, ph=0, flash=0, dead=0)

# name, x, y, angle (1024ths), pitch, feet, what is asked beyond `stray`
POSES = [
    ("the path, east along it",  640, 1024,   0,    0,   0, ""),
    ("the path's bottom",       1024,  832,   0,    0,   0, ""),
    ("behind the west lift",     192, 1024,   0,    0,   0, "hidden"),
    ("the west square, west",    300,  800, 512,    0,   0, "cover"),
    ("the east square, east",   1500, 1260,   0,    0,   0, "cover"),
    ("along the west edge",       84, 1024, 384, -150,   0, "cover"),
    ("down the path's slope",    850,  900, 900,  -60,   0, ""),
    ("on the west tower",        512, 1024,   0,  -40, 240, ""),
    ("the path, looking down",  1100,  830, 256, -290,   0, "cover"),
]


def rnd(a, b):
    return (2 * a + b) // (2 * b)


class View:
    def __init__(self, px, py, ang, pitch, feet):
        self.px, self.py = px, py
        self.s = round(16384 * math.sin(2 * math.pi * ang / 1024))
        self.c = round(16384 * math.cos(2 * math.pi * ang / 1024))
        self.eye = 102 + feet
        self.k16 = (8 * W * F256) >> 8
        self.c16 = 8 * W
        self.kyq = (133 * F256) >> 8
        self.hq = pitch + 96
        self.np = F(self.eye * self.kyq * 0x7FFF0, 1 << 19)
        self.t = F(self.c16, self.k16)

    def cam(self, X, Y):
        """the game's camera space at the default level: each coordinate's
        product rounded, then summed (wa_products' ROUND, wa_vtr)"""
        dx, dy = X - self.px, Y - self.py
        return (rnd(dx * self.s, 16384) - rnd(dy * self.c, 16384),
                rnd(dx * self.c, 16384) + rnd(dy * self.s, 16384))

    def image(self, x, z):
        return self.c16 + self.k16 * x / z, self.hq + self.np / z

    def vertex(self, x, z):
        """a vertex in the view as the game projects it: the column's
        divide rounds toward zero, the row is the floor's (product, shift)
        pair off the reciprocal table and rounds up (wa_hnum, wa_flrow)"""
        c = x * self.k16
        c = (abs(c) // z) * (1 if c >= 0 else -1) + self.c16
        fn, fs = -self.eye * self.kyq, 3
        if not -32768 <= fn <= 32767:
            fn, fs = -((-fn) >> 2), 1
        return F(c), F(self.hq - (((fn * (0x7FFF0 // z)) >> 16) >> fs))


def referee(v):
    """the cells an exact drawer plots for the arena's floor lines: (row,
    column) -> '-' or '_' (a shallow line's dash) or 'S' (a steep one's)"""
    cells = {}
    for (x0, y0, x1, y1) in A.flines(TAG):
        P = [tuple(map(F, v.cam(x0, y0))), tuple(map(F, v.cam(x1, y1)))]
        for d in (lambda p: p[1] - 16, lambda p: p[0] + v.t * p[1],
                  lambda p: v.t * p[1] - p[0]):
            a, b = d(P[0]), d(P[1])
            if a < 0 and b < 0:
                P = None
                break
            if a < 0:
                u = a / (a - b)
                P = [tuple(p + u * (q - p) for p, q in zip(P[0], P[1])), P[1]]
            elif b < 0:
                u = b / (b - a)
                P = [P[0], tuple(p + u * (q - p) for p, q in zip(P[1], P[0]))]
        if P is None:
            continue
        E = []
        for p, o in zip(P, (v.cam(x0, y0), v.cam(x1, y1))):
            E.append(v.vertex(*o) if p == tuple(map(F, o)) else v.image(*p))
        (c1, q1), (c2, q2) = E
        dc, dq = c2 - c1, q2 - q1
        if 4 * abs(dq) >= abs(dc):
            if dq == 0:
                continue
            if dq < 0:
                (c1, q1), (c2, q2), dc, dq = (c2, q2), (c1, q1), -dc, -dq
            y = max(0, math.ceil((q1 - 4) / 8))
            while 8 * y + 4 <= q2 and y < 24:
                col = math.floor((c1 + dc * (8 * y + 4 - q1) / dq) / 16)
                if 0 <= col < W:
                    cells[(y, col)] = "S"
                y += 1
        else:
            if dc < 0:
                (c1, q1), (c2, q2), dc, dq = (c2, q2), (c1, q1), -dc, -dq
            k = max(0, math.ceil((c1 - 8) / 16))
            while 16 * k + 8 < c2 and k < W:
                h = math.floor((q1 + dq * (16 * k + 8 - c1) / dc + 2) / 4)
                if 0 <= h < 48:
                    cells[(h // 2, k)] = "-" if h % 2 == 0 else "_"
                k += 1
    return cells


def tower_face(v, box):
    """the screen cells a tall box hides every farther floor point in: its
    columns, a cell in from each side, and the rows above its highest foot,
    a row clear of it - or None when a corner of it is behind the eye"""
    x0, y0, x1, y1 = box
    cols, feet = [], []
    for X, Y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        x, z = v.cam(X, Y)
        if z < 32:
            return None
        c, q = v.image(F(x), F(z))
        cols.append(c / 16)
        feet.append((q + 2) / 4)
    return (math.ceil(min(cols)) + 1, math.floor(max(cols)) - 1,
            math.floor(min(feet) / 2) - 1)


def near(cell, cells):
    r, c = cell
    return any((r + i, c + j) in cells for i in (-1, 0, 1) for j in (-1, 0, 1))


def main():
    keep = sys.argv[sys.argv.index("--keep") + 1] if "--keep" in sys.argv else None
    poses = [dict(px=x, py=y, ang=a, pitch=p, feet=f, jump=0, figs=[AWAY] * 3)
             for (_, x, y, a, p, f, _) in POSES]
    d = tempfile.mkdtemp()
    pin, pout = os.path.join(d, "poses.json"), keep or os.path.join(d, "out.json")
    json.dump(poses, open(pin, "w"))
    # three recordings of the same poses, an emulator each, side by side: a
    # MOVED view (every render makes the lines' ends anew), a STILL one (the
    # third render on: drawn from the walk the first still frame kept) and a
    # NOD (the pitch alone moved since the render before: the rows' pass)
    outs = [pout, os.path.join(d, "still.json"), os.path.join(d, "nod.json"),
            os.path.join(d, "verylow.json")]
    runs = [subprocess.Popen([sys.executable, os.path.join(ROOT, "tools/waposes.py"), "record",
                              "--arena", str(ARENA), "--width", str(W), "--poses", pin,
                              "--out", o] + flag, cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True)
            for o, flag in zip(outs, ([], ["--still"], ["--nod"], ["--level", "1"]))]
    logs = [q.communicate()[0] for q in runs]
    for q, o, log in zip(runs, outs, logs):
        if q.returncode or not os.path.exists(o):
            print(log[-2000:])
            print("waflines: FAIL - waposes could not record the poses")
            return 1
    r = runs[0]
    r.stdout = logs[0]
    rec = json.load(open(pout))
    fails = 0
    for log in logs:
        if " 0 DIVIDE FAULTS" not in log:
            print("  FAIL  a divide fault in a render: %s" % log.strip().splitlines()[-1])
            fails += 1
    # THE STILL VIEW AND THE NOD draw the moved view's picture: the same
    # camera, so the same floor-line cells - whichever way their ends and
    # their walks were come by
    for what, o in (("still view (the kept walk)", outs[1]), ("nod (the rows alone)", outs[2])):
        other = json.load(open(o))
        diff = n = 0
        for (name, *_), fa, fb in zip(POSES, rec["frames"], other["frames"]):
            va, vb = bytes.fromhex(fa["vram"]), bytes.fromhex(fb["vram"])
            for k in range(0, 23 * W * 2, 2):
                la = va[k + 1] == A_ROOM and va[k] in GLYPHS
                lb = vb[k + 1] == A_ROOM and vb[k] in GLYPHS
                n += la
                if (la or lb) and va[k:k + 2] != vb[k:k + 2]:
                    if not diff:
                        print("  FAIL  the %s draws another picture: %s, row %d column %d"
                              % (what, name, k // 2 // W, k // 2 % W))
                    diff += 1
        if diff:
            print("          %d floor-line cell(s) differ from the moved view's" % diff)
            fails += 1
        else:
            print("  ok    the %s: the moved view's %d cells" % (what, n))
    # VERY LOW (F1) draws the lines DASHED (wa_dash 2): a shallow line's
    # dashes in the even columns alone, a steep line's cells in the even
    # rows - and still on the referee's lines, and in the open views every
    # cell of them that the dashes keep
    low = json.load(open(outs[3]))
    nd = odd = off = gap = 0
    for (name, x, y, a, p, f, ask), fr in zip(POSES, low["frames"]):
        vram = bytes.fromhex(fr["vram"])
        drawn, other = {}, set()
        for row in range(23):
            for col in range(W):
                ch, at = vram[(row * W + col) * 2], vram[(row * W + col) * 2 + 1]
                if at == A_ROOM and ch in GLYPHS:
                    drawn[(row, col)] = GLYPHS[ch]
                elif ch != 0x20:
                    other.add((row, col))
        R = referee(View(x, y, a, p, f))
        nd += len(drawn)
        odd += sum(1 for (r, c), g in drawn.items() if (c if g != "S" else r) & 1)
        off += sum(1 for c in drawn if not near(c, R))
        if ask == "cover":
            gap += sum(1 for c, g in R.items() if c[0] < 22 and c not in other
                       and not ((c[1] if g != "S" else c[0]) & 1) and not near(c, drawn))
    bad = []
    if odd:
        bad.append("%d cell(s) in an odd column (a dash) or an odd row (a steep line)" % odd)
    if off:
        bad.append("%d cell(s) off every line" % off)
    if gap:
        bad.append("%d cell(s) the dashes keep, not drawn" % gap)
    print("  %-4s  Very Low, the lines dashed: %d cells%s"
          % ("FAIL" if bad else "ok", nd, "".join("\n          " + b for b in bad)))
    fails += bool(bad)
    boxes = [A.bx(b) for b in A.arenas()[ARENA][2]]
    total = exact = 0
    for (name, x, y, a, p, f, ask), fr in zip(POSES, rec["frames"]):
        vram = bytes.fromhex(fr["vram"])
        drawn, other = {}, set()
        for row in range(23):               # (row 23 carries the HUD's text)
            for col in range(W):
                ch, at = vram[(row * W + col) * 2], vram[(row * W + col) * 2 + 1]
                if at == A_ROOM and ch in GLYPHS:
                    drawn[(row, col)] = GLYPHS[ch]
                elif ch != 0x20:
                    other.add((row, col))
        v = View(x, y, a, p, f)
        R = referee(v)
        total += len(drawn)
        exact += sum(1 for c, g in drawn.items() if R.get(c) == g)
        stray = sorted(c for c in drawn if not near(c, R))
        bad = []
        if stray:
            bad.append("%d cell(s) off every line, the first at row %d column %d"
                       % (len(stray), stray[0][0], stray[0][1]))
        if not drawn:
            bad.append("no floor line on the screen at all")
        if ask == "cover":
            miss = sorted(c for c in R if c[0] < 23 and c not in other
                          and not near(c, drawn))
            if miss:
                bad.append("%d cell(s) of a line not drawn, the first at row %d column %d"
                           % (len(miss), miss[0][0], miss[0][1]))
        if ask == "hidden":
            face = tower_face(v, boxes[0])
            inside = [c for c in R if face and face[0] <= c[1] <= face[1] and c[0] <= face[2]]
            if len(inside) < 8:
                bad.append("the referee has %d cell(s) behind the tower - the pose asks "
                           "nothing" % len(inside))
            seen = sorted(c for c in drawn if face and face[0] <= c[1] <= face[1]
                          and c[0] <= face[2])
            if seen:
                bad.append("%d cell(s) drawn THROUGH the tower, the first at row %d column %d"
                           % (len(seen), seen[0][0], seen[0][1]))
        print("  %-4s  %-26s %3d cells, the referee's %3d%s"
              % ("FAIL" if bad else "ok", name, len(drawn), len(R),
                 "".join("\n          " + b for b in bad)))
        fails += bool(bad)
    if not total * 3 <= nd * 10 <= total * 7:
        print("  FAIL  Very Low draws %d cells where Medium draws %d: not about half" % (nd, total))
        fails += 1
    if total < 200:
        print("  FAIL  %d floor-line cells over the nine poses - under 200: the "
              "colour or the glyphs this row reads are not the drawer's" % total)
        fails += 1
    print("  %d of %d floor-line cells are the referee's own cell and dash" % (exact, total))
    if exact * 100 < total * EXACT:
        print("  FAIL  under %d in 100: the lines lie beside the referee's, not on them"
              % EXACT)
        fails += 1
    if fails:
        print("waflines: FAIL - %d check(s)" % fails)
        return 1
    print("waflines: ok - nine poses of the twin, %d floor-line cells: %d in 100 the "
          "referee's own, none off its lines, none missing where nothing stands, none "
          "through the tower; a still view and a nod draw the same; Very Low's are "
          "dashed; no divide fault"
          % (total, exact * 100 // total))
    return 0


if __name__ == "__main__":
    sys.exit(main())

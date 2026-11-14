#!/usr/bin/env python3
"""wamodel - a host model of WireArena's wall renderer, priced in guest cycles
(docs/plans/WIREARENA-HEIGHTS-PLAN.md sections 2 and 7.2.6).

It renders a pose on the host as the renderer would - the arena's boxes in
tools/arenagen.py's tree order, the view cone, each box's faces, the exact
pre-check, a span a face over its open columns - and counts what that costs:
boxes kept, faces, spans, columns, outline edges, cells. Today's renderer is
priced at unit costs MEASURED on MartyPC (a breakpoint trace of the P bench,
HEIGHTS-PLAN 2.1); the heights renderer of HEIGHTS-PLAN section 1 adds
per-column clips, the depth stack and the OUTLINE path for boxes lower than
the arena's tallest, priced at FORECAST costs (`CF`), until wave 2 measures
them. A frame is scaled into fps against the P bench measured on the same
tree (`MEASURED`), which folds in what the model leaves out (the HUD, the
meter, figure-count drift).

    python3 tools/wamodel.py --arena 2 [--width 40|80] [--variant G|A]
    python3 tools/wamodel.py --budget N     the visible budget of arena N

Variants: G is the recommended renderer (outline path for low boxes, a
flight as one object); A today's span for every edge, far tops included.
Arenas 0 and 1 are flat: there both variants are today's renderer.

The visible budget (HEIGHTS-PLAN 4.2) is checked by `arenagen --arena N`
through budget(): from any sampled pose - the free floor on a grid, and
standing on every box a walker can reach - at most BUDGET_BOXES boxes kept by
the cone and BUDGET_EDGES outline edges.
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arenagen as A                      # noqa: E402

NEAR = 16
FOVF = {90: 256, 75: 334, 60: 443}
KYQ = 133
EYE = 102

# --- unit costs, guest clk: MEASURED, P bench 40 columns, a4edcb7f
# (HEIGHTS-PLAN 2.1: exact cycles between breakpoints at the renderer's
# entries over 30 frames; a span is 5,041 + 662 a column by least squares)
C = dict(setup=13538, leaf_cull=705, leaf_in=1546, face_box_drew=3752,
         face_box_end=2229, face_room_drew=4745, face_room_end=2681,
         span=5041, col=662, fig=15800, cross=1693, glue=1429,
         present_loop=33071, cell_present=138)
# --- FORECAST costs of the heights renderer (HEIGHTS-PLAN 1.2, 1.3)
CF = dict(col_clip=120,       # a visited column of a tall face: the clip
          col_hidden=90,      # a column visited, nothing in it visible
          top_span=3800,      # (variant A) a far top edge's span setup...
          top_col=420,        # ...and a column of it
          face_h=250,         # a face's own top height (a multiply a vertex)
          stack=40,           # a clip change: the depth stack's push
          fig_cell=120,       # a figure cell: the stack walk
          height_setup=200,   # a distinct height a frame (num, shift)
          lowbox=5200,        # OUTLINE: a low box's corners
          edge=700,           # an outline edge's setup (one divide)
          ecol=180,           # ...a column of it, clip-tested and plotted
          ehid=60)            # ...a column of it, hidden or shut
# --- the P bench measured at 20d0cf89 (MS, a frame), to scale model frames
MEASURED = {40: 52.7, 80: 63.3}
TREADS = False                # a ramp drawn stepped (the T option, HEIGHTS-
                              # PLAN 7.4.2): off, the default
BUDGET_BOXES = 20             # HEIGHTS-PLAN 4.2, as built (7.3): the demo
BUDGET_EDGES = 40             # arena keeps 19 and 36 at its worst poses


class Arena:
    def __init__(self, n, lifts_raised=True):
        tag, name, boxes, start = A.arenas()[n]
        self.tag, self.name, self.start = tag, name, start
        self.boxes = [A.bx(b) for b in boxes]
        self.hts = [(b.top if (b.kind == A.LIFT and lifts_raised) else b.h)
                    for b in self.boxes]
        self.room_h = A.ARENA_ROOMH.get(tag, A.WALL_H)   # (0: no walls)
        self.flines = A.flines(tag)
        self.hmax = A.hmax(self.boxes)
        self.nodes = []
        A.box_tree(list(range(len(self.boxes))), self.nodes, self.boxes)
        self.flat = all(h >= self.hmax for h in self.hts)
        self.pvs = A.pvs_masks(tag, boxes)    # (HEIGHTS-PLAN 9.8: wa_order's)

    def order(self, px, py):
        out = []
        g = 1 << A.PVS_SHIFT
        m = self.pvs[min(max(int(px), 0) // g, len(self.pvs) - 1)][
            min(max(int(py), 0) // g, len(self.pvs[0]) - 1)] if self.pvs else -1

        def n(i):
            k, box, v, lo, hi, ids = self.nodes[i]
            if k == 2:
                if m >> box & 1:
                    out.append(box)
                return
            p = px if k == 0 else py
            a, b = (lo, hi) if p < v else (hi, lo)
            n(a)
            n(b)
        n(0)
        return out


class View:
    def __init__(self, px, py, ang, eye=EYE, pitch=0, fov=75, W=40):
        self.px, self.py, self.eye, self.W = px, py, eye, W
        a = ang * 2 * math.pi / 1024
        self.s, self.c = math.sin(a), math.cos(a)
        f = FOVF[fov]
        self.k16 = (8 * W * f) >> 8
        self.c16 = 8 * W
        self.kyq = (KYQ * f) >> 8
        self.hq = pitch + 96
        self.tc = {90: 1.0, 75: 25 / 32, 60: 19 / 32}[fov]   # wa_leaf's bound

    def cam(self, p):
        dx, dy = p[0] - self.px, p[1] - self.py
        return dx * self.s - dy * self.c, dx * self.c + dy * self.s

    def row(self, h, z):
        return int(math.floor((self.hq - (h - self.eye) * self.kyq / z + 2) / 4))


def faces(b):
    x0, y0, x1, y1 = b
    c = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    return {"S": (c[0], c[1]), "E": (c[1], c[2]), "N": (c[2], c[3]), "W": (c[3], c[0])}


def outside(b, px, py):
    fr = []
    if py < b[1]:
        fr.append("S")
    elif py > b[3]:
        fr.append("N")
    if px > b[2]:
        fr.append("E")
    elif px < b[0]:
        fr.append("W")
    return fr


def cone_out(b, v):
    pts = [v.cam(p) for p in ((b[0], b[1]), (b[2], b[1]), (b[2], b[3]), (b[0], b[3]))]
    if all(z < NEAR for x, z in pts):
        return True
    t = v.tc
    return all(x > t * z + 4 for x, z in pts) or all(x < -t * z - 4 for x, z in pts)


def render(ar, v, heights=True, outline=True):
    """One frame of arena ar from view v -> counts and clk. heights=False:
    today's renderer, every face shutting its columns."""
    W = v.W
    shut = [False] * W
    clip = [48] * W
    depth = [0] * W
    st = dict(leaf_cull=0, leaf_in=0, face_drew=0, face_end=0, rface_drew=0,
              rface_end=0, top_faces=0, spans=0, top_spans=0, cols=0,
              cols_hidden=0, top_cols=0, cells=0, stack=0, stackmax=0,
              lowboxes=0, edges=0, ecols=0, clk=0)
    clk = C["setup"] + C["glue"]
    if heights and not ar.flat:
        clk += CF["height_setup"] * len(set(ar.hts))

    def span(p1, p2, h, kind, tall, cheap=False):
        nonlocal clk
        (x1, z1), (x2, z2) = v.cam(p1), v.cam(p2)
        if z1 < NEAR and z2 < NEAR:
            return False
        if z1 < NEAR or z2 < NEAR:
            t = (NEAR - z1) / (z2 - z1)
            xn = x1 + t * (x2 - x1)
            if z1 < NEAR:
                x1, z1 = xn, NEAR
            else:
                x2, z2 = xn, NEAR

        def col16(x, z):
            x = max(-8 * z, min(8 * z, x))
            return v.c16 + x * v.k16 / z
        c1, c2 = col16(x1, z1), col16(x2, z2)
        if c1 > c2:
            if kind == "front":
                return False
            c1, c2, x1, z1, x2, z2 = c2, c1, x2, z2, x1, z1
        a = max((int(c1) + 7) >> 4, 0)
        b = min(((int(c2) + 7) >> 4) - 1, W - 1)
        if a > b or all(shut[c] for c in range(a, b + 1)):
            return False
        i1, i2 = 1 / z1, 1 / z2
        nvis = nhid = ncells = 0
        for c in range(a, b + 1):
            if shut[c]:
                continue
            u = (16 * c + 8 - c1) / max(c2 - c1, 1e-9)
            z = 1 / (i1 + (i2 - i1) * u)
            t = v.row(h, z)
            bt = v.row(0, z)
            vis = 0
            if t < clip[c] and 0 <= t < 48:
                vis += 1
            if kind == "front" and bt < clip[c] and 0 <= bt < 48 and bt != t:
                vis += 1
            if kind == "front" and (c == a or c == b):
                lo, hi = max(t + 1, 0), min(bt, clip[c], 48)
                vis += max(0, hi - lo) // 2
            if vis == 0 and t >= clip[c]:
                nhid += 1
            else:
                nvis += 1
            ncells += vis
            if tall:
                shut[c] = True
            else:
                if t < clip[c]:
                    clip[c] = t
                    depth[c] += 1
                    st["stack"] += 1
                    st["stackmax"] = max(st["stackmax"], depth[c])
                hz = ar.hmax - v.eye
                lim = v.row(ar.hmax, z) if hz > 0 else v.row(v.eye, 1e9)
                if clip[c] <= max(lim, 0):
                    shut[c] = True
        st["cells"] += ncells
        n2 = 1 + (kind == "front")
        if cheap:
            st["edges"] += n2
            st["ecols"] += (nvis + nhid) * n2
            clk += CF["edge"] * n2 + (CF["ecol"] * nvis + CF["ehid"] * nhid) * n2
        elif kind == "front":
            st["spans"] += 1
            st["cols"] += nvis
            st["cols_hidden"] += nhid
            clk += C["span"] + C["col"] * nvis
            if heights and not ar.flat:
                clk += CF["col_clip"] * nvis + CF["col_hidden"] * nhid
        else:
            st["top_spans"] += 1
            st["top_cols"] += nvis + nhid
            clk += CF["top_span"] + CF["top_col"] * nvis + CF["col_hidden"] * nhid
        return True

    for bi in ar.order(v.px, v.py):
        if all(shut):
            break
        b, h = ar.boxes[bi], ar.hts[bi]
        if cone_out(b, v):
            st["leaf_cull"] += 1
            clk += C["leaf_cull"]
            continue
        st["leaf_in"] += 1
        clk += C["leaf_in"]
        fr = outside(b, v.px, v.py)
        fs = faces(b)
        tall = (not heights) or (h >= ar.hmax and v.eye < h)
        cheap = heights and outline and not tall
        if cheap:
            st["lowboxes"] += 1
            clk += CF["lowbox"] - C["leaf_in"] + 600
            if b.kind == A.FLIGHT and TREADS:   # stepped: treads as extra edges
                (x1, z1), (x2, z2) = v.cam((b[0], b[1])), v.cam((b[2], b[1]))
                if z1 > NEAR and z2 > NEAR:
                    ca = v.c16 + x1 * v.k16 / z1
                    cb = v.c16 + x2 * v.k16 / z2
                    ncol = max(0, min(W - 1, int(max(ca, cb)) >> 4)
                               - max(0, int(min(ca, cb)) >> 4) + 1)
                    st["edges"] += b.treads
                    st["ecols"] += b.treads * ncol
                    clk += b.treads * (CF["edge"] + CF["ecol"] * ncol)
        hh = h if b.kind != A.FLIGHT else h // 2   # (a ramp's mean)
        line = getattr(b, "kind", None) == A.LINE   # (a LINE: its top's
        for k in fr[:2]:                            # edge alone, no face)
            ok = span(*fs[k], hh, "top" if line else "front", tall, cheap)
            if cheap:
                continue
            if ok:
                st["face_drew"] += 1
                clk += C["face_box_drew"]
            else:
                st["face_end"] += 1
                clk += C["face_box_end"]
            if heights and not ar.flat:
                clk += CF["face_h"]
        if heights and v.eye > h:       # the far top edges
            for k in "SENW":
                if k in fr:
                    continue
                p1, p2 = fs[k]
                st["top_faces"] += 1
                ok = span(p2, p1, hh, "top", False, cheap)
                if not cheap:
                    clk += C["face_box_drew"] if ok else C["face_box_end"]
    for (x0, y0, x1, y1) in ar.flines:      # (a floor line in the view: an
        (xa, za), (xb, zb) = v.cam((x0, y0)), v.cam((x1, y1))   # edge)
        if (za >= NEAR or zb >= NEAR) and not (xa > za and xb > zb) and \
                not (xa < -za and xb < -zb):
            st["edges"] += 1
    if not all(shut) and ar.room_h:
        r = A.ROOM
        rc = [(r[0], r[1]), (r[2], r[1]), (r[2], r[3]), (r[0], r[3])]
        for p1, p2 in ((rc[0], rc[3]), (rc[3], rc[2]), (rc[2], rc[1]), (rc[1], rc[0])):
            if span(p1, p2, ar.room_h, "front", True):
                st["rface_drew"] += 1
                clk += C["face_room_drew"]
            else:
                st["rface_end"] += 1
                clk += C["face_room_end"]
    st["clk"] = clk
    return st


def frame_ms(st, base_cells, nfig=2.7, figcells=15, heights=True):
    clk = st["clk"] + C["fig"] * nfig + C["cross"] + C["present_loop"]
    if heights:
        clk += CF["fig_cell"] * figcells + CF["stack"] * st["stack"]
        clk += C["cell_present"] * 2 * max(0, st["cells"] - base_cells)
    return clk / 4772.727


# --- poses ------------------------------------------------------------------
def yaws(px, py, eye, n=16, pitch=0, W=40, fov=75, a0=0, span=1024):
    return [View(px, py, a0 + span * i / n, eye, pitch, fov, W) for i in range(n)]


def pbench(W=40):
    """The P bench's circle (MULTIPLAYER-FPS-PLAN 16.7), in arena 0"""
    return [View(224, 800, 32 + 17 * math.sin(2 * math.pi * i / 32), EYE,
                 int(16 * math.cos(2 * math.pi * i / 32)), 75, W) for i in range(32)]


def pose_sets(ar, W=40, fov=75):
    S = {}
    if ar.tag == "a2":
        S["floor, 8 places x 16 headings"] = sum(
            [yaws(x, y, EYE, W=W, fov=fov) for (x, y) in
             [(800, 400), (800, 1650), (1000, 1024), (1600, 600), (1600, 1450),
              (1500, 1024), (1250, 700), (1250, 1350)]], [])
        S["floor, busiest: (800, 400) looking NE"] = yaws(800, 400, EYE, 9, 0, W, fov, 64, 192)
        S["on the platform"] = yaws(1248, 1024, 80 + EYE, W=W, fov=fov)
        S["on the platform, a jump's top"] = yaws(1248, 1024, 80 + EYE + A.jump_apex(), W=W, fov=fov)
        S["on the tower, west +-45 deg"] = yaws(1952, 1024, 160 + EYE, 9, 0, W, fov, 384, 256)
        S["on the tower, looking down"] = yaws(1952, 1024, 160 + EYE, 9, -60, W, fov, 384, 256)
        S["on the raised lift, west"] = yaws(1984, 1216, 160 + EYE, 9, -30, W, fov, 384, 256)
        S["hallways and corridor, 4 places x 16 headings"] = sum(
            [yaws(x, y, EYE, W=W, fov=fov) for (x, y) in
             [(600, 480), (300, 480), (64, 1000), (600, 1568)]], [])
        S["back room"] = yaws(250, 1024, EYE, W=W, fov=fov)
    else:
        if ar.tag == "a0":
            S["P bench"] = pbench(W)
        S["floor, 8 places x 16 headings"] = sum(
            [yaws(x, y, EYE, W=W, fov=fov) for (x, y) in
             [(800, 400), (800, 1650), (1000, 700), (1600, 600), (1600, 1450),
              (1300, 1024), (1250, 700), (700, 1350)]
             if not A.inside(x, y, ar.boxes, 24)], [])
    return S


# the play bench's poses in the heights arena (apps/wirearena/wirearena.asm,
# arenagen.ARENA_BENCH): x, y, angle (1024ths), base pitch, the floor under the eye
BENCH = [("A, busiest floor", 800, 400, 160, 0, 0), ("B, open floor", 1600, 1024, 512, 0, 0),
         ("C, on the platform", 1248, 1024, 512, 0, 80), ("D, on the tower", 1952, 1024, 512, -40, 160),
         ("E, hallway", 600, 1568, 512, 0, 0)]


def bench_sets(W=40):
    """The bench's circle (yaw +-17/1024, pitch +-16, MULTIPLAYER-FPS-PLAN
    16.7) at each pose, as the guest's bench runs it"""
    out = {}
    for name, x, y, a, p, f in BENCH:
        out[name] = [View(x, y, a + 17 * math.sin(2 * math.pi * i / 32), EYE + f,
                          p + int(16 * math.cos(2 * math.pi * i / 32)), 75, W) for i in range(32)]
    return out


def budget_poses(ar, W=40):
    """Every free floor point on a 192-unit grid, and the middle of every box
    a walker can stand on (below the arena's tallest), 8 headings each"""
    out = []
    for x in range(96, 2048, 192):
        for y in range(96, 2048, 192):
            if not A.inside(x, y, ar.boxes, 24) and A.in_walk(ar.tag, x, y):
                out += yaws(x, y, EYE, 8, W=W)
    for b, h in zip(ar.boxes, ar.hts):
        if h < ar.hmax:
            out += yaws((b[0] + b[2]) / 2, (b[1] + b[3]) / 2, h + EYE, 8, W=W)
    return out


def budget(n):
    """-> (most boxes kept, most outline edges, the pose of each) over the
    sampled poses of arena n"""
    ar = Arena(n)
    mb = me = (0, None)
    for v in budget_poses(ar):
        st = render(ar, v)
        if st["leaf_in"] > mb[0]:
            mb = (st["leaf_in"], (round(v.px), round(v.py), v.eye))
        if st["edges"] > me[0]:
            me = (st["edges"], (round(v.px), round(v.py), v.eye))
    return mb, me


def report(n, W, variant, bench=False):
    ar = Arena(n)
    base = render(Arena(0), pbench(W)[0], False)
    base_cells = sum(render(Arena(0), v, False)["cells"] for v in pbench(W)) / 32
    pm = sum(frame_ms(render(Arena(0), v, False), base_cells, heights=False)
             for v in pbench(W)) / 32
    cal = MEASURED[W] / pm
    print("arena %d (%s), %d columns, variant %s; model frames x %.3f (the P bench: "
          "model %.1f ms, measured %.1f)" % (n, ar.name, W, variant, cal, pm, MEASURED[W]))
    keys = ["leaf_in", "face_drew", "face_end", "top_faces", "spans", "cols",
            "cols_hidden", "lowboxes", "edges", "ecols", "cells", "stackmax"]
    print("%-40s %6s %6s " % ("pose set", "fps", "worst") +
          " ".join("%8s" % k[:8] for k in keys))
    for name, ps in (bench_sets(W) if bench else pose_sets(ar, W)).items():
        tot = {}
        ms = []
        for v in ps:
            st = render(ar, v, heights=True, outline=(variant == "G"))
            for k, x in st.items():
                tot[k] = max(tot.get(k, 0), x) if k == "stackmax" else tot.get(k, 0) + x
            ms.append(frame_ms(st, base_cells, heights=not ar.flat) * cal)
        m = {k: (x if k == "stackmax" else x / len(ps)) for k, x in tot.items()}
        print("%-40s %6.1f %6.1f " % (name, 1000 * len(ms) / sum(ms), 1000 / max(ms)) +
              " ".join("%8.1f" % m[k] for k in keys))
    del base


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--arena", type=int, default=2)
    ap.add_argument("--width", type=int, choices=(40, 80), default=40)
    ap.add_argument("--variant", choices=("G", "A"), default="G")
    ap.add_argument("--budget", type=int, metavar="N")
    ap.add_argument("--bench", action="store_true",
                    help="the heights arena's play-bench poses A-E")
    a = ap.parse_args()
    if a.budget is not None:
        (b, bp), (e, ep) = budget(a.budget)
        print("arena %d: at most %d boxes kept (at %r), %d outline edges (at %r); "
              "the budget is %d and %d" % (a.budget, b, bp, e, ep, BUDGET_BOXES, BUDGET_EDGES))
        return 0 if b <= BUDGET_BOXES and e <= BUDGET_EDGES else 1
    report(a.arena, a.width, a.variant, a.bench)
    return 0


if __name__ == "__main__":
    sys.exit(main())

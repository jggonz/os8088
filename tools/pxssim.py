#!/usr/bin/env python3
"""PIXELSTEIN 3D's reference renderer (SPEC.md 97.12): the frame, on the host.

    python3 tools/pxssim.py [--level apps/pixelstein/levels/e1m1.txt]
                            [--scene a|b | --at X Y HEADING]
                            [--size 64] [--res low|full]
                            [--backend cga4|herc|modex|cga16]
                            [--png OUT.png] [--dump OUT.json] [--zoom 2]

tools/htmsim.py's and tools/weavesim.py's shape: the same tables
(tools/pxstab.py, imported - not copied), the same fixed point, the same
walker, the same hit arithmetic and the same shadow bytes the package
produces, in Python, so that a guest column array or a guest shadow can be
diffed against a number that was arrived at by a second, independent route.
tests/pxssim.py (wave 1) reads the package's arrays out of MartyPC and holds
them to this; tools/pxslevel.py's DDA sweep runs the walker here.

THE WALKER IS THE GENERATED BODY'S, INSTRUCTION FOR INSTRUCTION (SPEC.md
97.2). Two walkers, one per grid-line family, each a POINTER into a map
laid out along its own stepping axis plus a 16-bit fraction:

  V  crosses vertical lines (x = integer), walks the TRANSPOSED map
     mapT[x*64 + y] with SI = the cell pointer and DX = y's fraction, so
     `add dx, frac / adc si, int + 64*dxs` steps both x (a whole row of the
     transposed map, 64 bytes) and y (the tangent's integer part plus the
     fraction's carry, ONE byte) in two instructions;
  H  crosses horizontal lines (y = integer), walks the plain map
     map[y*64 + x] with DI and BX, the cotangent, likewise.

Which crossing comes first is a POINTER COMPARE against a key that names the
corner cell (xt, yt) in each layout - CX = mapT + xt*64 + yt, BP = map +
yt*64 + xt - because a pointer and its key share their major part, so the
compare is exactly the minor coordinate's: V is first when its y has not yet
reached yt (dy > 0: SI < CX), H when its x has not reached xt (DI < BP), and
a tie (the same cell) goes to the other walker, which is what makes the cell
a hit is tested by the walker that entered it. A walker whose step is clamped
(tan > 127.996: it crosses no line of its family inside a 64-tile map) or
whose first intercept lands off the map is PARKED - its pointer set where the
compare can never choose it (97.2.3).

A closed door is a slab across the cell's middle (97.2.4): the walker that
entered the door cell half-steps its intercept, and if the ray is still in
the cell there it hits the slab with u = that fraction; else it left the cell
before the slab (into the jamb, which the other walker will find) and the walk
resumes. A sliding door (wave 3) passes doorpos - how far the slab has slid,
0..255 - and the walker subtracts it from the fraction BEFORE the side's
mirror, so a ray striking the visible part of a half-open door reads the
texture from its leading edge; nothing passes it yet, and the default None
is a shut door.

The hit (97.2.5): nx = the perpendicular distance, dx*cos + dy*sin through
MUL14 (apps/tank/tk3d.inc's shift-left-two of the 32-bit product), clamped
at PX_MINDIST; h = PX_HEIGHTK / nx rows, one div a column; u = the hit
fraction, mirrored per side so a texture reads left to right from wherever it
is seen; side = V lit, H dark.

WHAT THIS RENDERS is a rung: FLAT - flat lit and dark faces, the ceiling and
the floor, a door slab in its own ink; WIRE - the edges; and TEXTURED (wave
2, `--rung tex`) - the column's height quantised to the scaler set's, texel
rows as tools/pxsgen.py gives them, bytes off tools/pxsart.py's byte-texture
set with the odd-row phase - into the 80-byte-stride, byte-a-column shadow
every backend shares (97.3): Size bytes of every row, centred - at the
shipped rung, Size 64 x Resolution Low res, 32 rays two bytes wide (97.1;
--size 64 --res low is the default for that reason) - expanded to device
pixels per backend only for the PNG. THE RESOLUTION IS AN ARGUMENT, never
inferred from the column count: Size 48 at Full and a 96-byte band at Low
res would both be 48 columns, and the first cut's `bpc = 2 if cols == 32`
could model exactly one Size, which is how a compose that placed every other
Size 8 bytes from where the present read it went unseen (review, wave 2).
Sprites arrive with wave 3; the bytes of a full frame are what a forced full
redraw writes, and that is what the gate diffs.
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import pxstab                                               # noqa: E402
import pxslevel                                             # noqa: E402
import pxsgen                                               # noqa: E402
import shot                                                 # noqa: E402

ANG = pxstab.ANG
SIN = pxstab.sin_table()
TAN = pxstab.tan_table()
CLAMP = pxstab.TAN_CLAMP
MAP_W = pxslevel.MAP_W
VIEW_H = 80                     # rows of the view (97.1)
STRIDE = 80                     # bytes a shadow row (97.3)
HEIGHTK = 51200                 # h = HEIGHTK / nx: a wall fills 80 rows at 2.5
                                # tiles, which is square on CGA 320x200 (97.1)
MINDIST = 23                    # 0.09 tiles in Q8.8: the near clamp
MAXH = 120                      # 1.5 x the view: the tallest scaler (97.3)
SOLID, DOOR, DOOR_EW = pxslevel.SOLID, pxslevel.DOOR, pxslevel.DOOR_EW

# the backends with a BYTE shadow (97.3): one byte a column a row, 80 bytes
# a row, the picture centred in the row - Size x 4 device pixels on CGA320
# and Mode X, Size x 8 in the 640-px Hercules box, Size x 2 in C160's 160.
# Mode X's 160 and 320 rays (rungs 4 and 5) have no byte shadow at all and
# are not rendered here; geometry() refuses them with 97.3's sentence.
BACKENDS = ("cga4", "herc", "modex", "cga16")
ART_OF = {"cga16": "c160"}      # tools/pxsart.py's name for the retime's set
SIZES = (48, 56, 64, 72, 80)    # the Size row (97.3)
# every column count the two rows can make: Size at Full, Size / 2 at Low res
SHADOW_COLS = tuple(sorted(set(SIZES) | set(s // 2 for s in SIZES)))
FANS = {c: pxstab.fan_table(c, k) for c, k in pxstab.FANS}


def sin_q14(a):
    a &= ANG - 1
    q, i = a >> 10, a & 1023
    v = SIN[i] if q in (0, 2) else SIN[1024 - i]
    return v if q < 2 else -v


def cos_q14(a):
    return sin_q14(a + 1024)


def mul14(a, b):
    """apps/tank/tk3d.inc's MUL14: (a * b) >> 14, signed, floor."""
    return (a * b) >> 14


def cast_ray(cells, px, py, a, doorpos=None, seen=None):
    """One ray from (px, py) in Q8.8 at 12-bit angle a. Returns the hit.

    The dict carries what the package's column arrays carry plus what the
    tests want to see: crossings (the walker passes, the hit's included),
    which walker hit, the hit point in Q8.8, the cell and its byte. `seen`,
    a set, collects the cells the walkers PASSED - what the package marks
    with the frame's spotvis generation (97.1: the pass tail's store, never
    the hit cell) - for the sprite candidates of 97.6.
    """
    a &= ANG - 1
    q, i = a >> 10, a & 1023
    if q & 1 == 0:
        tan_v, tan_h = TAN[i], TAN[1024 - i]
    else:
        tan_v, tan_h = TAN[1024 - i], TAN[i]
    dxs = 1 if q in (0, 3) else -1
    dys = 1 if q in (0, 1) else -1
    xtile0, ytile0 = px >> 8, py >> 8
    xf, yf = px & 255, py & 255
    xt, yt = xtile0 + dxs, ytile0 + dys          # the corner cell (the key)

    # --- V: its first intercept, or parked ------------------------------
    v_parked = tan_v == CLAMP
    if not v_parked:
        xpartial = (256 - xf) if dxs > 0 else xf
        yoff = (tan_v * xpartial) >> 8            # Q8.8, the mul's middle bytes
        yint = py + dys * yoff
        vrow = yint >> 8                          # floor
        vfrac = (yint & 255) << 8                 # DX: Q0.16 of the tile
        if vrow < 0 or vrow >= MAP_W:
            v_parked = True
    vx = xt
    # --- H: likewise ------------------------------------------------------
    h_parked = tan_h == CLAMP
    if not h_parked:
        ypartial = (256 - yf) if dys > 0 else yf
        xoff = (tan_h * ypartial) >> 8
        xint = px + dxs * xoff
        hcol = xint >> 8
        hfrac = (xint & 255) << 8
        if hcol < 0 or hcol >= MAP_W:
            h_parked = True
    hy = yt
    assert not (v_parked and h_parked)

    crossings = 0
    v_int, v_frc = tan_v >> 8, (tan_v & 255) << 8
    h_int, h_frc = tan_h >> 8, (tan_h & 255) << 8

    # THE CONTROL FLOW IS THE GENERATED BODY'S (97.2.2), and it has no
    # "neither" state:
    #
    #         jmp vcheck
    # ventry: <the V pass>
    # vcheck: cmp si, cx / jb ventry     ; V first? (dy < 0: ja)
    # hentry: <the H pass>               ; ...else H, unconditionally
    # hcheck: cmp di, bp / jb hentry     ; H first? (dx < 0: ja)
    #         jmp ventry                 ; ...else V, unconditionally
    #
    # so a TIE - the ray through a cell's corner, which a spawn at a tile's
    # centre makes of every 45-degree ray - is tested by both walkers, and
    # the same cell twice is what the machine does. A parked walker is never
    # entered because the border is solid: the other walker hits it first.
    def v_first():
        if v_parked:
            return False
        if h_parked:
            return True
        # SI < CX  <=>  vrow < yt (dy > 0); SI > CX <=> vrow > yt (dy < 0)
        return vrow < hy if dys > 0 else vrow > hy

    def h_first():
        if h_parked:
            return False
        if v_parked:
            return True
        return hcol < vx if dxs > 0 else hcol > vx

    state = "vcheck"
    while True:
        if state == "vcheck":
            state = "ventry" if v_first() else "hentry"
            continue
        if state == "hcheck":
            state = "hentry" if h_first() else "ventry"
            continue
        if state == "ventry":
            if v_parked:
                raise RuntimeError("the parked V walker was entered at (%d,%d) "
                                   "heading %d: is the border solid?" % (px, py, a))
            state = "vcheck"
            # --- the V pass: cell (vx, vrow) -----------------------------
            crossings += 1
            cell = cells[vrow * MAP_W + vx]
            if cell & (SOLID | DOOR):
                if cell & DOOR and not cell & DOOR_EW:
                    # the slab at x = vx + 0.5: where is y there?
                    y88 = vrow * 256 + (vfrac >> 8)
                    slab = y88 + dys * (tan_v >> 1)
                    if slab >> 8 == vrow:
                        u = slab & 255
                        dp = doorpos.get(vrow * MAP_W + vx) if isinstance(doorpos, dict) \
                            else doorpos          # (a dict is per door, wave 3)
                        if dp is not None and u < dp:
                            pass                  # the door has slid past: through
                        else:
                            if dp is not None:
                                u -= dp           # the slid slab's own texel,
                            return dict(side=0, cell=(vx, vrow), byte=cell,
                                        hx=vx * 256 + 128, hy=slab,
                                        u=u if dxs > 0 else 255 - u,
                                        crossings=crossings, door=True)
                    # else it left the cell before the slab: walk on
                elif cell & SOLID:
                    hx = vx * 256 if dxs > 0 else (vx + 1) * 256
                    hy_ = vrow * 256 + (vfrac >> 8)
                    u = hy_ & 255
                    # THE JAMB IS DECIDED HERE (97.2.5): the face the ray
                    # came through a DOOR cell to reach takes material 15.
                    # The cell across the line is the un-stepped walker's,
                    # (vx - dxs, vrow), which the body still has in SI.
                    jamb = cells[vrow * MAP_W + vx - dxs] & DOOR
                    return dict(side=0, cell=(vx, vrow),
                                byte=(pxslevel.JAMB << 4) | (cell & 15) if jamb else cell,
                                hx=hx, hy=hy_, u=u if dxs > 0 else 255 - u,
                                crossings=crossings, door=False)
            # pass: spotvisT[vx*64 + vrow] = gen; step
            if seen is not None:
                seen.add(vrow * MAP_W + vx)
            if dys > 0:
                s = vfrac + v_frc
                vfrac = s & 0xFFFF
                vrow += v_int + (s >> 16)
            else:
                s = vfrac - v_frc
                vfrac = s & 0xFFFF
                vrow -= v_int + (1 if s < 0 else 0)
            vx += dxs
            # CX += 64*dxs (with vx), BP += dxs (xt)
            xt += dxs
            if vrow < 0 or vrow >= MAP_W:
                v_parked = True
        elif state == "hentry":
            if h_parked:
                raise RuntimeError("the parked H walker was entered at (%d,%d) "
                                   "heading %d: is the border solid?" % (px, py, a))
            state = "hcheck"
            crossings += 1
            cell = cells[hy * MAP_W + hcol]
            if cell & (SOLID | DOOR):
                if cell & DOOR and cell & DOOR_EW:
                    x88 = hcol * 256 + (hfrac >> 8)
                    slab = x88 + dxs * (tan_h >> 1)
                    if slab >> 8 == hcol:
                        u = slab & 255
                        dp = doorpos.get(hy * MAP_W + hcol) if isinstance(doorpos, dict) \
                            else doorpos
                        if dp is not None and u < dp:
                            pass
                        else:
                            if dp is not None:
                                u -= dp           # ...before the mirror, both sides
                            return dict(side=1, cell=(hcol, hy), byte=cell,
                                        hx=slab, hy=hy * 256 + 128,
                                        u=255 - u if dys > 0 else u,
                                        crossings=crossings, door=True)
                elif cell & SOLID:
                    hyy = hy * 256 if dys > 0 else (hy + 1) * 256
                    hx_ = hcol * 256 + (hfrac >> 8)
                    u = hx_ & 255
                    jamb = cells[(hy - dys) * MAP_W + hcol] & DOOR
                    return dict(side=1, cell=(hcol, hy),
                                byte=(pxslevel.JAMB << 4) | (cell & 15) if jamb else cell,
                                hx=hx_, hy=hyy, u=255 - u if dys > 0 else u,
                                crossings=crossings, door=False)
            if seen is not None:
                seen.add(hy * MAP_W + hcol)
            if dxs > 0:
                s = hfrac + h_frc
                hfrac = s & 0xFFFF
                hcol += h_int + (s >> 16)
            else:
                s = hfrac - h_frc
                hfrac = s & 0xFFFF
                hcol -= h_int + (1 if s < 0 else 0)
            hy += dys
            yt += dys
            if hcol < 0 or hcol >= MAP_W:
                h_parked = True
        if crossings > 512:
            raise RuntimeError("the walk did not end from (%d,%d) heading %d: "
                               "is the border solid?" % (px, py, a))


def project(px, py, heading, hit, rung="flat"):
    """nx, h, top, bot, hq from a hit, the package's way (97.2.5). Under the
    TEXTURED rung h is QUANTISED to the scaler set's heights (97.3: over 120
    clamps, every height aliases down through px_hq) before top and bot, so
    the rows the scaler stores are the rows the column claims; wallh keeps
    the true h for the sprite z-test."""
    dx = hit["hx"] - px
    dy = hit["hy"] - py
    nx = mul14(dx, cos_q14(heading)) + mul14(dy, sin_q14(heading))
    if nx < MINDIST:
        nx = MINDIST
    h = HEIGHTK // nx
    wallh = h
    hq = pxsgen.quantise(h) if rung == "tex" else h
    if hq >= VIEW_H:
        top, bot = 0, VIEW_H - 1
    else:
        top = (VIEW_H - hq) >> 1
        bot = top + hq - 1
    return nx, wallh, top, bot, hq


def cast_view(cells, px, py, heading, cols=64, rung="flat", seen=None, doors=None):
    """...`doors`: {cell index: position 0..256} of the doors that have slid
    (wave 3; tests/pxssim.py reads the guest's door table into it), so a
    half-open door's gap and its slab's texel are the package's."""
    fan = FANS[cols]
    out = []
    for c in range(cols):
        a = (heading + fan[c]) & (ANG - 1)
        hit = cast_ray(cells, px, py, a, doorpos=doors, seen=seen)
        nx, wallh, top, bot, hq = project(px, py, heading, hit, rung)
        out.append(dict(c=c, angle=a, nx=nx, wallh=wallh, top=top, bot=bot, hq=hq,
                        side=hit["side"], u=hit["u"], mat=hit["byte"] >> 4,
                        door=hit["door"], crossings=hit["crossings"],
                        cell=hit["cell"], hx=hit["hx"], hy=hit["hy"]))
    return out


# --- the Flat rung's inks, per backend (placeholders until wave 2's tables) ----

def ink(backend, what, mat=0, side=0, row=0):
    par = row & 1
    if backend == "cga4":                       # palette 0: black, green, red, brown
        if what == "ceil":
            return 0x00
        if what == "floor":
            return 0x55
        if mat == pxslevel.DOOR_MAT or what == "door":
            return 0xDD if side == 0 else 0x88
        return 0xFF if side == 0 else 0xAA
    if backend == "herc":
        if what == "ceil":
            return 0x00
        if what == "floor":
            return 0x22 if par else 0x88
        if mat == pxslevel.DOOR_MAT or what == "door":
            return (0xEE if par else 0xBB) if side == 0 else (0x44 if par else 0x11)
        return 0xFF if side == 0 else (0xAA if par else 0x55)
    if backend == "modex":
        if what == "ceil":
            return 0
        if what == "floor":
            return 8
        return mat if side == 0 else 16 + mat
    if backend == "cga16":
        if what == "ceil":
            return 0x00
        if what == "floor":
            return 0x22                         # green: 8 is the dark faces'
        m = {1: 7, 2: 7, 3: 9, 4: 6, 5: 6, 6: 4, 12: 3, 14: 14, 13: 6}.get(mat, 7)
        d = {7: 8, 9: 1, 6: 8, 4: 8, 3: 3, 14: 6}.get(m, 8)
        v = m if side == 0 else d
        return (v << 4) | v
    raise ValueError(backend)


def geometry(backend, cols, lowres=False):
    """(bytes a column, the first byte of the picture) for a column count
    AT A RESOLUTION.

    Resolution: Low res (docs/plans/PIXELSTEIN-PLAN.md 16) is Size / 2 rays
    over the same view, so a column is TWO shadow bytes; Full is one - and
    which it is comes from the caller (the package's px_lowres), because a
    count alone does not say (48 columns is Size 48 Full or a 96-byte band
    at Low res). The picture is centred in the 80-byte row on EVERY byte
    backend (97.3) - the Hercules band is the box's 80 bytes, not 64, so
    Size 72 and 80 fit there as they do on CGA. A count the row cannot
    hold, and Mode X's 160/320 rays, which have no byte shadow, are REFUSED
    rather than wrapped: the first cut of this centred a negative x0 and
    wrote into the end of the bytearray."""
    if backend not in BACKENDS:
        raise ValueError("%s: not a byte-shadow backend (97.3)" % backend)
    if cols not in SHADOW_COLS:
        raise ValueError("%d columns: Mode X's 160/320 rays write the planes "
                         "directly and have no byte shadow (97.3); the "
                         "reference renderer models them in the wave that "
                         "builds rungs 4 and 5" % cols)
    bpc = 2 if lowres else 1
    width = cols * bpc
    if width not in SIZES:
        raise ValueError("%d columns x %d bytes is a %d-byte band, not a Size "
                         "(%s)" % (cols, bpc, width, SIZES))
    x0 = (STRIDE - width) // 2
    if x0 < 0 or x0 + width > STRIDE:
        raise ValueError("%d columns x %d bytes do not fit an %d-byte row"
                         % (cols, bpc, STRIDE))
    return bpc, x0


_BT = {}


def bt_set(backend):
    """The byte-texture set of tools/pxsart.py, once per backend (the
    retime is `cga16` here and `c160` there: ONE alias, at this boundary)."""
    art = ART_OF.get(backend, backend)
    if art not in _BT:
        import pxsart
        _BT[art] = pxsart.bt_set(pxsart.masters(), art)
    return _BT[art]


def render(view, backend, cols=64, rung="flat", lowres=False, world=None,
           seen=None, weapon=(1, 0)):
    """...and, since wave 3, the sprites of `world` (a Level: its statics and
    actors at their spawn cells, the sim frozen) whose cells `seen` holds,
    then the weapon (weapon, frame) - or no weapon at all when `weapon` is
    None (the rungs before the sprite set: a refused part 4)."""
    sh = bytearray(render_walls(view, backend, cols, rung, lowres))
    if world is not None and seen is not None:
        recs = candidates(world, seen, world.eye[0], world.eye[1], world.eye[2],
                          cols, lowres)
        draw_sprites(sh, view, recs, backend, cols, lowres, rung, weapon is None)
    if weapon is not None:
        draw_weapon(sh, backend, cols, lowres, rung, weapon[0], weapon[1])
    return bytes(sh)


def render_walls(view, backend, cols=64, rung="flat", lowres=False):
    """A full frame of the Flat rung, of the Wire rung (PIXELSTEIN-PLAN 14's
    rung 0) or of the TEXTURED rung (97.3: the scaler of the column's
    quantised height, texel v over the rows pxsgen.texel_rows gives it,
    the byte off the byte-texture set with the odd-row phase - tools/
    pxsart.py's texel(), the same rule the transpose and the generator
    follow): VIEW_H rows of STRIDE bytes, the band Size = cols x (2 at Low
    res, 1 at Full) bytes wide and centred.

    Wire: the ceiling and floor tones as a flat ground with the horizon at
    the middle row, then per column the wall's top and bottom edge pixel, and
    a vertical run from the neighbour's edge to this one's where the height
    changes - a corner, a door slab's end, a material change. Nothing else is
    written, which is what makes its present cheap: rows that hold no edge
    are not dirty."""
    sh = bytearray(STRIDE * VIEW_H)
    bpc, x0 = geometry(backend, cols, lowres)
    if rung == "wire":
        for r in range(VIEW_H):
            v = ink(backend, "ceil", row=r) if r < VIEW_H // 2 \
                else ink(backend, "floor", row=r)
            for b in range(cols * bpc):
                sh[r * STRIDE + x0 + b] = v
        edge = ink(backend, "wall", 1, 0, 0)          # the lit tone: solid
        prev = None
        for col in view:
            c, top, bot = col["c"], col["top"], col["bot"]
            rows = {top, bot}
            if prev is not None:
                pt, pb, pk = prev
                if pt != top or pb != bot or pk != (col["mat"], col["side"]):
                    rows |= set(range(min(pt, top), max(pt, top) + 1))
                    rows |= set(range(min(pb, bot), max(pb, bot) + 1))
            for r in rows:
                for b in range(bpc):
                    sh[r * STRIDE + x0 + c * bpc + b] = edge
            prev = (top, bot, (col["mat"], col["side"]))
        return bytes(sh)
    if rung == "tex":
        import pxsart
        bt = bt_set(backend)
        art = ART_OF.get(backend, backend)      # texel() takes u >> 4 on c160
        rowmap = {}
        for col in view:
            c, top, bot, hq = col["c"], col["top"], col["bot"], col["hq"]
            if hq not in rowmap:
                m = {}
                for tv, rows in enumerate(pxsgen.texel_rows(hq)):
                    for r in rows:
                        m[r] = tv
                rowmap[hq] = m
            tex = rowmap[hq]
            for r in range(VIEW_H):
                if r < top:
                    v = ink(backend, "ceil", row=r)
                elif r > bot:
                    v = ink(backend, "floor", row=r)
                else:
                    v = pxsart.texel(bt, art, col["mat"], col["side"], col["u"],
                                     tex[r], r)
                for b in range(bpc):
                    sh[r * STRIDE + x0 + c * bpc + b] = v
        return bytes(sh)
    for col in view:
        c, top, bot = col["c"], col["top"], col["bot"]
        for r in range(VIEW_H):
            if r < top:
                v = ink(backend, "ceil", row=r)
            elif r > bot:
                v = ink(backend, "floor", row=r)
            else:
                v = ink(backend, "door" if col["door"] else "wall", col["mat"],
                        col["side"], r)
            for b in range(bpc):
                sh[r * STRIDE + x0 + c * bpc + b] = v
    return bytes(sh)


# --- the sprites and the weapon (wave 3, 97.6) --------------------------------------

MAXSPR, SPRCAP = 8, 8000
FOCAL = int(round(pxstab.FOCAL))    # PX_FOCAL as pxtab.inc carries it, 222
WPNCOLS, WPNH, WPNROW0 = 16, 24, 56
SPR_INK = {"cga4": (0x88, 0x88), "herc": (0x88, 0x22), "modex": (0x18, 0x18),
           "cga16": (0x88, 0x88)}   # a silhouette's (even, odd) tone (pxrast.inc)
G_WALK0, G_SHOOT, G_PAIN, G_DIE, G_DEAD = 0, 10, 12, 13, 16
DECO0, PICK0 = 17, 23


def _pxsart():
    import pxsart
    return pxsart


def cdiv(a, b):
    """idiv: the quotient truncated toward zero."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def transform(px, py, heading, sx, sy, cols, lowres):
    """px_spr_xform: (h, column) of the point (sx, sy) seen from the eye, or
    None when it is too near, or wider of the axis than 45 degrees."""
    dx, dy = sx - px, sy - py
    c, s_ = cos_q14(heading), sin_q14(heading)
    nx = mul14(dx, c) + mul14(dy, s_)
    if nx < MINDIST:
        return None
    ny = mul14(dy, c) - mul14(dx, s_)
    if abs(ny) >= nx:
        return None
    h = HEIGHTK // nx
    devpx = cdiv(ny * FOCAL, nx)
    col = cols // 2 + (devpx >> (3 if lowres else 2))
    return h, col


def actor_frame(a, col, heading, cols):
    """px_act_frame for a STANDING actor (the sim frozen): the walk frame
    of the facing the viewer sees, and whether it is mirrored."""
    fan = FANS[cols]
    c = min(max(col, 0), cols - 1)
    rel = ((fan[c] + heading + ANG // 2) - a[3] + ANG // 16) & (ANG - 1)
    f8 = rel >> 9
    if (a[2] & 0x7F) == 1:          # THE DOG (wave 6): four masters make
        pxsart = _pxsart()          # its eight (pxact.inc's px_dogfac)
        master, mirror = pxsart.D_FACING[f8]
        return pxsart.D_WALK0 + master * 2, bool(mirror)
    if f8 <= 4:
        return G_WALK0 + f8 * 2, False
    return G_WALK0 + (8 - f8) * 2, True


def candidates(world, seen, px, py, heading, cols, lowres):
    """The sprite list of 97.6 - statics whose cell was passed, actors whose
    cell or an open neighbour was - transformed, sorted far to near (a tie
    keeps the insertion order: statics first, then actors, as the package
    walks its tables), at most MAXSPR with the farthest dropped."""
    import bisect
    cells = world.cells
    out = []                        # (h, order, rec)
    order = 0
    hq_of = pxsgen.quantise

    def add(h, col, frame, mirror):
        nonlocal order
        hq = hq_of(min(h, MAXH))
        w = pxsgen.width(hq, lowres)
        c0 = col - w // 2
        vis = min(c0 + w, cols) - max(c0, 0)
        if vis <= 0:
            return
        cost = vis * min(hq, VIEW_H)
        rec = dict(h=h, hq=hq, col=col, w=w, c0=c0, cost=cost, frame=frame,
                   mirror=mirror, half=False)
        keys = [o[0] for o in out]
        if len(out) >= MAXSPR:
            if h <= out[0][0]:
                return
            out.pop(0)
            keys = keys[1:]
        i = bisect.bisect_right(keys, h)
        out.insert(i, (h, order, rec))
        order += 1

    for x, y, kind, blocking in world.statics:
        cell = y * MAP_W + x
        if cell not in seen:
            continue
        t = transform(px, py, heading, x * 256 + 128, y * 256 + 128, cols, lowres)
        if t is None:
            continue
        frame = PICK0 + kind if kind < 8 else DECO0 + kind - 8
        add(t[0], t[1], frame, False)
    for a in world.actors:
        cell = a[1] * MAP_W + a[0]
        ok = False
        for d in (0, -1, 1, -64, 64, -65, -63, 63, 65):
            if cells[cell + d] & (SOLID | DOOR):
                continue
            if cell + d in seen:
                ok = True
                break
        if not ok:
            continue
        t = transform(px, py, heading, a[0] * 256 + 128, a[1] * 256 + 128, cols, lowres)
        if t is None:
            continue
        frame, mirror = actor_frame(a, t[1], heading, cols)
        add(t[0], t[1], frame, mirror)
    recs = [o[2] for o in out]
    total = sum(r["cost"] for r in recs)
    for r in recs:                  # the cap: the farthest halve first
        if total <= SPRCAP:
            break
        r["half"] = True
        total -= r["cost"] >> 1
    return recs


def draw_sprites(sh, view, recs, backend, cols, lowres, rung, boxes=False):
    """The posts (Textured) or the silhouettes (Flat, Wire) of the sorted
    candidates into the shadow, far to near, z-tested per column against
    wallh - or, with `boxes` (no sprite set: part 4 refused), the frame's
    whole extent in the silhouette tone."""
    pxsart = _pxsart()
    art = ART_OF.get(backend, backend)
    bpc, x0 = geometry(backend, cols, lowres)
    frames = pxsart.sprites()
    ink = SPR_INK[backend]
    for r in recs:
        hq = r["hq"]
        idx, alpha = frames[r["frame"]]
        fr = pxsart.spr_frame(idx, alpha, art)
        runs = pxsart.frame_runs(alpha, pxsart.SPR, art == "c160")
        c2t = pxsgen.col2tex(hq, lowres)
        trows = pxsgen.texel_rows(hq)
        top = (VIEW_H - hq) >> 1
        for c in range(max(r["c0"], 0), min(r["c0"] + r["w"], cols)):
            if r["half"] and ((c - r["c0"]) & 1):
                continue
            if view[c]["wallh"] >= r["h"]:
                continue
            if boxes:
                for row in range(max(top, 0), min(top + hq, VIEW_H)):
                    for k in range(bpc):
                        sh[row * STRIDE + x0 + c * bpc + k] = ink[row & 1]
                continue
            src = c2t[1 + c - r["c0"]]
            if r["mirror"]:
                src = 31 - src
            if art == "c160":
                src >>= 1
            for v0, v1 in runs[src]:
                for v in range(v0, v1):
                    for row in trows[v]:
                        if rung == "tex":
                            b = pxsart.phase(art, fr[src * 32 + v], row)
                        else:
                            b = ink[row & 1]
                        for k in range(bpc):
                            sh[row * STRIDE + x0 + c * bpc + k] = b
    return sh


def draw_weapon(sh, backend, cols, lowres, rung, weapon=1, wframe=0):
    """The weapon's frame: sixteen byte columns at the band's middle, rows
    56..79 through the 24-row scaler's texel rows (Full's byte store,
    whatever the resolution)."""
    pxsart = _pxsart()
    art = ART_OF.get(backend, backend)
    bpc, x0 = geometry(backend, cols, lowres)
    size = cols * bpc
    idx, alpha = pxsart.weapons()[weapon * 3 + wframe]
    fr = pxsart.spr_frame(idx, alpha, art, pxsart.WPN_W)
    runs = pxsart.frame_runs(alpha, pxsart.WPN_W, art == "c160")
    trows = pxsgen.texel_rows(WPNH)
    ink = SPR_INK[backend]
    for k in range(WPNCOLS):
        src = k >> 1 if art == "c160" else k
        for v0, v1 in runs[src]:
            for v in range(v0, v1):
                for row in trows[v]:
                    row += WPNROW0 - (VIEW_H - WPNH) // 2
                    if rung == "tex":
                        b = pxsart.phase(art, fr[src * 32 + v], row)
                    else:
                        b = ink[row & 1]
                    sh[row * STRIDE + x0 + size // 2 - WPNCOLS // 2 + k] = b
    return sh


# --- the PNG: shadow bytes to device pixels ------------------------------------

CGA_PAL0 = [(0, 0, 0), (0, 0xAA, 0), (0xAA, 0, 0), (0xAA, 0x55, 0)]
PALETTE = pxstab.__dict__.get("PALETTE") or [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00), (0x00, 0xAA, 0xAA),
    (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA), (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA),
    (0x55, 0x55, 0x55), (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55), (0xFF, 0xFF, 0xFF)]


def expand(sh, backend):
    """(w, h, rgb rows) of the whole shadow at device pixels."""
    rows = []
    for r in range(VIEW_H):
        line = []
        for b in sh[r * STRIDE:(r + 1) * STRIDE]:
            if backend == "cga4":
                for k in (6, 4, 2, 0):
                    line.append(CGA_PAL0[(b >> k) & 3])
            elif backend == "herc":
                for k in range(7, -1, -1):
                    line.append((0xFF, 0xB0, 0x00) if (b >> k) & 1 else (0, 0, 0))
            elif backend == "modex":
                if b < 16:
                    c = PALETTE[b]
                else:
                    c = tuple(v * 6 // 10 for v in PALETTE[b & 15])
                line += [c] * 4
            else:                                   # cga16: two nibbles
                line.append(PALETTE[b >> 4])
                line.append(PALETTE[b & 15])
        rows.append(line)
    return len(rows[0]), VIEW_H, rows


def write_png(path, sh, backend, zoom=2):
    w, h, rows = expand(sh, backend)
    zx, zy = zoom, zoom
    if backend == "herc":
        zx, zy = zoom, zoom * 2           # a Hercules pixel is ~1.55:1
    pix = bytearray()
    for row in rows:
        line = bytearray()
        for c in row:
            line += bytes(c) * zx
        pix += line * zy
    shot.png(path, w * zx, h * zy, bytes(pix))
    return w * zx, h * zy


# --- the pinned scenes (97.10) --------------------------------------------------

def scenes(lv):
    sx, sy, sa = lv.spawn
    return {
        # A: the corridor - the spawn, looking down the entry corridor
        "a": (sx * 256 + 128, sy * 256 + 128, sa),
        # B: a 90-degree turn at a doorway - standing one tile SOUTH-EAST
        # of the hall's door at (13,3), half way through a turn from the
        # corridor's axis to south, the jamb at the view's left edge and
        # the far wall foreshortening across it. THE DELTA-FILL-HEAVY
        # FRAME (97.10): a PX_TURN turn here rewrites all 32 columns for
        # 1,706 Flat stores against scene A's 1,082 - the first B, at
        # (14,3), read 1,486 and was cheaper than A on every frame
        # measured; no scene in E1M1 is dearer than A on a FULL repaint,
        # where the cast dominates, so A binds the promise and B the turn
        "b": (14 * 256 + 128, 4 * 256 + 128, 512),
        # C: THE SPRITE SCENE (wave 3, 97.6, 97.10): the brick room's north-
        # west corner at (28,8) looking south-east - the gold key at
        # (30,10), the barrel at (33,12) and the guard at (31,13) in view,
        # three sprites over textured walls, the plan's scene-A sprite
        # count on a pose the level actually holds
        "c": (28 * 256 + 128, 8 * 256 + 128, 512),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--level", default=os.path.join(pxslevel.DEFAULT_DIR, "e1m1.txt"))
    ap.add_argument("--scene", default="a")
    ap.add_argument("--at", nargs=3, type=int, metavar=("X", "Y", "HEADING"),
                    help="Q8.8 x, y and a 12-bit heading instead of a scene")
    ap.add_argument("--size", type=int, default=64, choices=SIZES,
                    help="the Size row's band, bytes (97.3); 64 is the default")
    ap.add_argument("--res", default="low", choices=("low", "full"),
                    help="Low res = Size / 2 rays two bytes wide - the shipped "
                         "XT rung (97.1) and so the default; Full = Size rays "
                         "(Mode X's 160/320 rays have no byte shadow and are a "
                         "later wave's)")
    ap.add_argument("--backend", default="cga4", choices=BACKENDS)
    ap.add_argument("--rung", default="flat", choices=("flat", "wire", "tex"),
                    help="the Detail rung the PNG shows")
    ap.add_argument("--png")
    ap.add_argument("--dump")
    ap.add_argument("--zoom", type=int, default=2)
    a = ap.parse_args()
    lv = pxslevel.parse(a.level)
    if a.at:
        px, py, heading = a.at
    else:
        px, py, heading = scenes(lv)[a.scene]
    lowres = a.res == "low"
    cols = a.size // 2 if lowres else a.size
    seen = set()
    view = cast_view(lv.cells, px, py, heading, cols, a.rung, seen)
    lv.eye = (px, py, heading)
    cr = [c["crossings"] for c in view]
    hs = [c["wallh"] for c in view]
    print("pxssim: %s at (%d.%02d, %d.%02d) heading %d, Size %d %s (%d columns): "
          "crossings mean %.1f max %d, wall rows %d..%d (mean %.1f)"
          % (lv.name, px >> 8, (px & 255) * 100 // 256, py >> 8,
             (py & 255) * 100 // 256, heading, a.size, a.res, cols,
             sum(cr) / float(len(cr)), max(cr), min(hs), max(hs),
             sum(min(h, VIEW_H) for h in hs) / float(len(hs))))
    if a.dump:
        json.dump(dict(level=lv.name, px=px, py=py, heading=heading, cols=cols,
                       size=a.size, lowres=lowres, backend=a.backend, columns=view),
                  open(a.dump, "w"), indent=1)
        print("pxssim: wrote %s" % a.dump)
    if a.png:
        sh = render(view, a.backend, cols, a.rung, lowres, lv, seen)
        w, h = write_png(a.png, sh, a.backend, a.zoom)
        print("pxssim: wrote %s (%dx%d, %s, %s)" % (a.png, w, h, a.backend, a.rung))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""EXCITEBIKE original-art AUTHORING tool (SPEC.md 102.2, docs/plans/EXCITEBIKE-PLAN.md 12.2).

    python3 tools/excitebike_art.py --write     # (re)write apps/excitebike/art/*.txt|json
    python3 tools/excitebike_art.py --check     # the committed files equal what --write would write

This is NOT a build dependency.  `make` never runs it: the text files it
writes are the SOURCE OF RECORD (edit them by hand afterwards; they, not this
script, are authoritative) and `tools/excitebike_assets.py` compiles them.
What this script is for is the record of HOW the first set was made: every
tile, ramp profile, bike pose, banner and letter below is drawn by a
procedure written for this project (wheel circles, frame segments, a rider
blob and helmet, wedge profiles, crowd dots and a hand-written 5x7 alphabet)
and NOTHING here reads a ROM, a CHR file, a screenshot or anyone else's
graphics (the art policy, plan section 0).  Python standard library only,
deterministic (a private LCG, never `random`), so `--check` can hold the
committed text to the procedure.

Pixel conventions of the files it writes:
  tiles      8x8 grids, `.` = slot 0, `0-9a-f` = a palette slot
  poses      `.` transparent, `0` outline (opaque black), `1` rider colour,
             `2` bike body, `3` highlight / skin
  glyphs     `.` off, `#` on
"""
import argparse
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "apps", "excitebike", "art")


# ---------------------------------------------------------------------------
# deterministic noise (a private LCG: stable across Python versions)
# ---------------------------------------------------------------------------
class Rng:
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def next(self, n):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.s >> 8) % n


# ---------------------------------------------------------------------------
# palette (original colour choices) - slot numbers used everywhere below
# ---------------------------------------------------------------------------
K, GD, G, GL, DD, D, DL, SKY, CD, W, R, Y, O, B, SK, GR = range(16)

PALETTE = {
    "slots": [
        ["black", [0, 0, 0]],
        ["grass-dark", [24, 96, 40]],
        ["grass", [48, 152, 56]],
        ["grass-light", [120, 200, 80]],
        ["dirt-dark", [104, 60, 32]],
        ["dirt", [176, 112, 56]],
        ["dirt-light", [224, 168, 96]],
        ["sky", [88, 168, 248]],
        ["shade", [56, 56, 104]],
        ["white", [248, 248, 248]],
        ["red", [216, 32, 40]],
        ["yellow", [248, 216, 32]],
        ["orange", [248, 128, 24]],
        ["blue", [40, 88, 216]],
        ["skin", [240, 176, 136]],
        ["grey", [168, 168, 176]],
    ],
    # five course themes: a theme is a set of slot overrides, so the same art
    # serves every track (plan 4.4)
    "themes": [
        {"name": "meadow", "slots": {}},
        {"name": "dusk", "slots": {"7": [248, 144, 96], "1": [40, 80, 56],
                                   "2": [64, 128, 72], "3": [120, 168, 96]}},
        {"name": "desert", "slots": {"7": [136, 200, 248], "1": [176, 136, 72],
                                     "2": [216, 176, 96], "3": [240, 216, 136],
                                     "4": [120, 56, 32], "5": [200, 96, 56]}},
        {"name": "frost", "slots": {"7": [176, 208, 248], "1": [96, 144, 152],
                                    "2": [152, 200, 208], "3": [224, 240, 248],
                                    "5": [152, 112, 88], "6": [200, 168, 144]}},
        {"name": "night", "slots": {"7": [16, 24, 72], "1": [16, 56, 40],
                                    "2": [24, 96, 48], "3": [56, 136, 72],
                                    "6": [176, 128, 88], "8": [24, 24, 56]}},
    ],
    # CGA 320x200x4 inks (palette 0: 1 green, 2 red, 3 yellow-or-brown) per
    # slot, two profiles the C key cycles (plan 4.5).  Hand-authored per SLOT:
    # a nearest-colour map loses the ramps and is forbidden.
    "cga": {
        "regs": {"p0_high": 16, "p0_low": 0},
        "p0_high": [0, 0, 1, 1, 0, 3, 3, 0, 0, 3, 2, 3, 2, 1, 3, 3],
        "p0_low":  [0, 0, 1, 1, 0, 3, 3, 0, 0, 3, 2, 3, 2, 1, 3, 3],
    },
    # Hercules: 0 black, 1 half (row-alternating pattern), 2 white
    "herc": [0, 0, 1, 2, 0, 1, 2, 0, 0, 2, 2, 0, 1, 1, 2, 1],
}


# ---------------------------------------------------------------------------
# small drawing kit
# ---------------------------------------------------------------------------
class Grid:
    def __init__(self, w=8, h=8, fill=0):
        self.w, self.h = w, h
        self.p = [[fill] * w for _ in range(h)]

    def set(self, x, y, v):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = v

    def rect(self, x0, y0, x1, y1, v):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, v)

    def specks(self, rng, n, v, y0=0, y1=7):
        for _ in range(n):
            self.set(rng.next(self.w), y0 + rng.next(y1 - y0 + 1), v)

    def text(self, chars="0123456789abcdef"):
        return "\n".join("".join("." if v == 0 else chars[v] for v in row)
                         for row in self.p)


def solid(v):
    return Grid(fill=v)


def slope_tile(y_left, y_right, body, line, hi, bg, dither=None, base_rng=None):
    """A wedge profile tile.  y_left / y_right are the surface heights in
    pixels above the tile's bottom edge at its left and right edge (0..8).
    Below the surface: `body`; the surface pixel: `line`; a highlight pixel
    under the line; above: `bg` (grass, with a few dark specks)."""
    g = Grid(fill=bg)
    if base_rng is not None:
        g.specks(base_rng, 5, GD, 0, 7)
    for x in range(8):
        h = y_left + (y_right - y_left) * (x + 0.5) / 8.0
        top = 8 - int(round(h))          # row index of the surface pixel
        for y in range(8):
            if y > top:
                g.set(x, y, body)
            elif y == top and h >= 0.5:
                g.set(x, y, line)
        if h >= 1.5 and top + 1 < 8:
            g.set(x, top + 1, hi)
    return g


# ---------------------------------------------------------------------------
# tiles
# ---------------------------------------------------------------------------
def make_tiles():
    T = {}          # name -> (Grid, class)

    def add(name, grid, cls="none"):
        assert name not in T, name
        T[name] = (grid, cls)

    rng = Rng(0xE8B1)

    # --- grass: the far verge and the lawn.  SPEED IS A DRAWING RULE HERE (SPEC.md
    # 102.1): the scroll engine skips an entering line that already holds the
    # right pixels, and a line equals the NEXT one only when the art has vertical
    # runs - so the lawn is VERTICAL BLADES (a fixed colour down each pixel column
    # of the tile, every row identical) and the same tile all the way down, and
    # the far verge likewise.  grass_b is the same picture under its own name.
    lawn = [G, GD, G, GL, G, G, GD, G]
    for n in ("grass_a", "grass_b"):
        g = solid(G)
        for x, v in enumerate(lawn):
            g.rect(x, 0, x, 7, v)
        add(n, g)
    g = solid(GD)
    for x in (1, 4, 6):
        g.rect(x, 0, x, 7, G)
    add("grass_far", g)
    for n, off in (("tuft_a", 0), ("tuft_b", 3)):
        g = solid(G)
        for x in (1 + off % 3, 4 + off % 2, 6):
            g.set(x, 5, GL)
            g.set(x, 6, GL)
            g.set(x - 1 if x > 0 else x, 7, GD)
            g.set(x, 7, GD)
        add(n, g)

    # --- the track surface: banks, dirt, the dashed lane dividers
    g = solid(D)
    g.rect(0, 0, 7, 1, G)
    g.rect(0, 2, 7, 2, GD)
    for x in (1, 5):                # speckle as short vertical streaks (runs, see above)
        g.rect(x, 3, x, 7, DD)
    g.rect(3, 3, 3, 7, DL)
    add("bank_top", g)
    g = solid(D)
    g.rect(0, 5, 7, 7, DD)
    g.rect(0, 4, 7, 4, DL)
    for x in (2, 6):
        g.rect(x, 0, x, 3, DL)
    add("bank_bot", g)
    # dirt: flat.  The track surface is the one region every plain column shares, so
    # any pixel texture in it costs the scroll engine two changing lines a tile row
    # (SPEC.md 102.1); the texture the eye needs to tell surfaces apart is in the
    # ROUGH and MUD pieces and the lane dashes, which are where the rules change.
    add("dirt_a", solid(D))
    add("dirt_b", solid(D))
    for n, dash in (("dirt_line_a", 0), ("dirt_line_b", 4)):
        g = solid(D)
        for x in range(8):
            if (x + dash) % 8 < 4:
                g.set(x, 7, W)
            else:
                g.set(x, 7, DL)
        add(n, g)

    # --- hedge along the bottom of the picture: vertical runs again (speed is a
    # drawing rule: SPEC.md 102.1), a fixed colour down each pixel column.  The
    # "ab" pair is the plain columns' and the "cd" pair the ramps', which differ
    # from it by a phase so a long ramp does not stripe against the plain hedge.
    for pair, ph in (("ab", 0), ("cd", 3)):
        top = solid(GD)
        bot = solid(GD)
        for x in range(8):
            if (x + ph) % 3 == 0:
                top.rect(x, 0, x, 7, G)
            if (x + ph) % 4 == 1:
                bot.rect(x, 0, x, 7, K)
            elif (x + ph) % 4 == 3:
                bot.rect(x, 0, x, 7, G)
        add("hedge_" + pair[0], top)
        add("hedge_" + pair[1], bot)

    # --- rough ground and mud (slow the rider)
    for n in ("rough_a", "rough_b"):
        g = solid(D)
        ph = 0 if n.endswith("a") else 4
        for bx in (1 + ph % 3, 5 - ph % 3):
            g.rect(bx, 3, bx + 1, 4, DD)
            g.set(bx, 2, DL)
            g.set(bx + 1, 2, DD)
            g.rect(bx - 1, 5, bx + 2, 5, DD)
        g.specks(rng, 4, DL)
        add(n, g, "rough")
    for n in ("mud_a", "mud_b"):
        g = solid(D)
        g.rect(0, 1, 7, 6, DD)
        g.rect(1, 0, 6, 0, DD)
        g.rect(1, 7, 6, 7, DD)
        g.specks(rng, 5, K, 1, 6)
        g.specks(rng, 2, SKY, 2, 5)
        add(n, g, "mud")

    # --- hurdles: a low log-and-post barrier and a tall gate
    def hurdle_low(flip):
        g = solid(D)
        g.specks(rng, 4, DD)
        g.rect(0, 3, 7, 5, W)
        for x in range(0, 8, 2):
            g.rect(x + flip, 3, x + flip, 5, R)
        g.rect(0, 2, 7, 2, K)
        g.rect(0, 6, 7, 6, K)
        g.rect(1, 6, 1, 7, K)
        g.rect(6, 6, 6, 7, K)
        return g
    add("hurdle_lo_a", hurdle_low(0), "hurdle_lo")
    add("hurdle_lo_b", hurdle_low(1), "hurdle_lo")

    def hurdle_high(top):
        g = solid(D)
        g.specks(rng, 3, DD)
        g.rect(1, 0, 2, 7, K)
        g.rect(5, 0, 6, 7, K)
        g.rect(2, 0, 2, 7, GR)
        g.rect(5, 0, 5, 7, GR)
        band = (1, 2) if top else (4, 5)
        g.rect(0, band[0], 7, band[1], Y)
        g.rect(0, band[0] - 1, 7, band[0] - 1, K)
        g.rect(0, band[1] + 1, 7, band[1] + 1, K)
        g.rect(3, band[0], 4, band[1], K)
        return g
    add("hurdle_hi_a", hurdle_high(True), "hurdle_hi")
    add("hurdle_hi_b", hurdle_high(False), "hurdle_hi")

    # --- arrow panels (cool the engine)
    for n, sh in (("arrow_a", 0), ("arrow_b", 2)):
        g = solid(D)
        g.specks(rng, 3, DD)
        # a chevron pointing right, three pixels thick
        for i in range(4):
            x = 1 + i + sh // 2
            g.rect(x, 1 + i, x + 1, 1 + i, B)
            g.rect(x, 6 - i, x + 1, 6 - i, B)
        g.rect(0, 0, 7, 0, DD)
        g.rect(0, 7, 7, 7, DD)
        add(n, g, "arrow")

    # --- the striped kicker (launch ramp, three tiles)
    def kicker(part):
        g = solid(D)
        y0, y1 = ((0, 3), (3, 6), (6, 8))[part]
        for x in range(8):
            h = y0 + (y1 - y0) * (x + .5) / 8
            top = 8 - int(round(h + 1))
            for y in range(8):
                if y > top:
                    g.set(x, y, Y if (x + y + part * 2) % 4 < 2 else K)
                elif y == top:
                    g.set(x, y, K)
        return g
    for part, n in enumerate(("kicker_a", "kicker_b", "kicker_c")):
        add(n, kicker(part), "kicker")

    # --- ramps: body tiles in the lane rows, profile tiles above them
    def ramp_body(alt):
        g = solid(O)
        for y in range(8):
            for x in range(8):
                if (x + y + alt * 3) % 8 == 0:
                    g.set(x, y, DD)
        g.rect(0, 0, 7, 0, Y)
        g.rect(0, 7, 7, 7, DD)
        return g
    add("rbody_a", ramp_body(0), "ramp")
    add("rbody_b", ramp_body(1), "ramp")
    rr = Rng(77)
    prof = (
        ("rp_up_s", 0, 8), ("rp_up_ga", 0, 4), ("rp_up_gb", 4, 8),
        ("rp_dn_s", 8, 0), ("rp_dn_ga", 8, 4), ("rp_dn_gb", 4, 0),
        ("rp_top", 8, 8), ("rp_fill", None, None))
    for n, a, b in prof:
        if a is None:
            g = solid(O)
            for y in range(8):
                for x in range(8):
                    if (x + y * 2) % 7 == 0:
                        g.set(x, y, DD)
            add(n, g, "ramp")
        else:
            add(n, slope_tile(a, b, O, K, Y, G, base_rng=Rng(rr.next(999) + 1)),
                "ramp")

    # --- hills: the same profile set in earth colours
    add("hbody_a", _hill_body(0), "hill")
    add("hbody_b", _hill_body(1), "hill")
    for n, a, b in prof:
        hn = "h" + n[1:]
        if a is None:
            g = solid(DD)
            for y in range(8):
                for x in range(8):
                    if (x * 3 + y) % 9 == 0:
                        g.set(x, y, D)
            add(hn, g, "hill")
        else:
            add(hn, slope_tile(a, b, D, K, DL, G, base_rng=Rng(rr.next(999) + 1)),
                "hill")

    # --- finish line and start gate
    for n, ph in (("chequer_a", 0), ("chequer_b", 1)):
        g = Grid()
        for y in range(8):
            for x in range(8):
                g.set(x, y, W if ((x // 2) + (y // 2) + ph) % 2 == 0 else K)
        add(n, g, "finish")
    g = solid(D)
    g.rect(3, 0, 4, 7, GR)
    g.rect(2, 0, 2, 7, K)
    g.rect(5, 0, 5, 7, K)
    add("finish_post", g, "finish")
    g = solid(D)
    g.rect(0, 0, 7, 2, R)
    g.rect(0, 3, 7, 3, K)
    g.rect(0, 4, 7, 7, D)
    g.rect(0, 5, 7, 5, W)
    add("gate_bar", g, "gate")
    g = solid(D)
    g.rect(2, 0, 5, 7, W)
    g.rect(1, 0, 1, 7, K)
    g.rect(6, 0, 6, 7, K)
    g.rect(3, 0, 4, 7, R)
    add("gate_post", g, "gate")
    g = solid(D)
    g.rect(0, 0, 7, 0, K)
    g.rect(0, 1, 7, 7, DD)
    g.specks(rng, 4, D)
    add("gate_shadow", g, "gate")

    # --- crowd (five rows, four heads-and-shoulders variants) and the banner
    heads = (R, Y, B, SK)
    for i in range(4):
        g = solid(CD)
        r3 = Rng(100 + i)
        for hx in (1, 5):
            hy = 1 + r3.next(3)
            hc = heads[(i + hx) % 4]
            g.rect(hx, hy, hx + 1, hy + 1, SK)
            g.rect(hx - 1 if hx > 0 else 0, hy + 2, hx + 2, min(hy + 4, 7), hc)
            g.set(hx, hy - 1 if hy > 0 else 0, K)
        g.specks(r3, 4, K, 4, 7)
        add("crowd_" + "abcd"[i], g)
    g = solid(SKY)
    add("sky_a", g)
    g = solid(SKY)
    g.rect(1, 2, 4, 3, W)
    g.rect(2, 1, 5, 1, W)
    g.rect(5, 2, 6, 3, W)
    add("sky_cloud", g)
    for n, c1, c2 in (("pennant_a", R, Y), ("pennant_b", B, W)):
        g = solid(SKY)
        g.rect(0, 0, 0, 7, K)
        for y in range(1, 6):
            g.rect(1, y, 7 - abs(y - 3) * 2, y, c1 if y % 2 else c2)
        add(n, g)
    for n, base in (("plate_a", R), ("plate_b", B)):
        g = solid(base)
        g.rect(0, 0, 7, 0, W)
        g.rect(0, 7, 7, 7, K)
        if n == "plate_a":
            g.rect(0, 0, 0, 7, W)
        for x in range(1, 7, 2):
            g.rect(x, 2, x, 5, W)
        add(n, g)
    g = solid(GR)
    g.rect(0, 0, 7, 1, W)
    g.rect(0, 5, 7, 5, K)
    g.rect(0, 2, 7, 4, GR)
    g.rect(0, 6, 7, 7, CD)
    add("fence_a", g)
    g = solid(GR)
    g.rect(0, 0, 7, 1, W)
    g.rect(0, 5, 7, 5, K)
    g.rect(3, 2, 3, 4, K)
    g.rect(0, 6, 7, 7, CD)
    add("fence_b", g)

    # --- HUD pieces (the temperature gauge)
    for n, v in (("gauge_empty", CD), ("gauge_cool", B), ("gauge_hot", R)):
        g = solid(K)
        g.rect(1, 2, 6, 5, v)
        g.rect(1, 2, 6, 2, W if v != CD else GR)
        add(n, g)
    g = solid(K)
    g.rect(0, 1, 0, 6, GR)
    g.rect(1, 1, 7, 1, GR)
    g.rect(1, 6, 7, 6, GR)
    add("gauge_cap", g)
    return T


def _hill_body(alt):
    g = solid(D)
    for y in range(8):
        for x in range(8):
            if (x * 2 + y + alt * 5) % 9 == 0:
                g.set(x, y, DD)
            elif (x + y * 3 + alt) % 11 == 0:
                g.set(x, y, DL)
    g.rect(0, 7, 7, 7, DD)
    return g


# ---------------------------------------------------------------------------
# columns, pieces, scripts
# ---------------------------------------------------------------------------
GRASS = ["grass_far", "grass_a", "grass_b", "grass_a", "grass_b", "grass_a"]   # rows 8..13
LANE_ROWS = 6      # tile rows 16..21 are the six collision rows


def plain_col(kind):
    # The three plain columns differ ONLY in the tuft row (and so in 8 pixel lines): a
    # line of one equals the next line of another everywhere else, which is what
    # lets the scroll engine skip most of an entering plain column (SPEC.md 102.1).
    g = list(GRASS)
    tuft = {"a": "tuft_a", "b": "tuft_b", "c": "tuft_a"}[kind]
    return g + [tuft, "bank_top", "dirt_a", "dirt_line_a", "dirt_b",
                "dirt_line_b", "dirt_a", "bank_bot"]


def lanes(cols, rows_map, base):
    """Replace lane-row tiles (rows 16..21 = indices 8..13 of the 14) in
    `base`.  rows_map: {row: tilename} with row 16..21."""
    out = list(base)
    for r, t in rows_map.items():
        out[r - 8] = t
    return out


def make_columns():
    C = {}      # name -> (14 tiles, hedge)

    def col(name, tiles, hedge="ab"):
        assert len(tiles) == 14, (name, len(tiles))
        assert name not in C
        C[name] = (tiles, hedge)

    col("plain_a", plain_col("a"), "ab")
    col("plain_b", plain_col("b"), "ab")
    col("plain_c", plain_col("c"), "ab")
    pa = plain_col("a")
    pb = plain_col("b")
    # rough: whole-width, and a two-lane patch
    col("rough_1", lanes(0, {r: "rough_a" for r in range(16, 22)}, pa))
    col("rough_2", lanes(0, {r: "rough_b" for r in range(16, 22)}, pb))
    col("rough_3", lanes(0, {16: "rough_a", 17: "rough_b", 18: "rough_a"}, pb))
    col("rough_4", lanes(0, {19: "rough_b", 20: "rough_a", 21: "rough_b"}, pa))
    # mud
    col("mud_1", lanes(0, {r: "mud_a" for r in range(16, 22)}, pa))
    col("mud_2", lanes(0, {r: "mud_b" for r in range(16, 22)}, pb))
    col("mud_3", lanes(0, {17: "mud_a", 18: "mud_b", 19: "mud_a"}, pa))
    col("mud_4", lanes(0, {19: "mud_b", 20: "mud_a"}, pb))
    # hurdles
    col("hlo_1", lanes(0, {16: "hurdle_lo_a", 17: "hurdle_lo_b"}, pa))
    col("hlo_2", lanes(0, {20: "hurdle_lo_a", 21: "hurdle_lo_b"}, pb))
    col("hlo_3", lanes(0, {18: "hurdle_lo_b", 19: "hurdle_lo_a"}, pa))
    col("hhi_1", lanes(0, {r: "hurdle_hi_a" for r in (16, 17, 18)}, pa))
    col("hhi_2", lanes(0, {r: "hurdle_hi_b" for r in (19, 20, 21)}, pb))
    col("hhi_3", lanes(0, {r: ("hurdle_hi_a" if r % 2 else "hurdle_hi_b")
                          for r in range(16, 22)}, pa))
    # arrows
    col("arr_1", lanes(0, {r: "arrow_a" for r in (17, 18, 19, 20)}, pa))
    col("arr_2", lanes(0, {r: "arrow_b" for r in (17, 18, 19, 20)}, pb))
    col("arr_3", lanes(0, {r: "arrow_a" for r in (16, 17, 20, 21)}, pb))
    # kicker
    col("kick_1", lanes(0, {r: "kicker_a" for r in range(16, 22)}, pa))
    col("kick_2", lanes(0, {r: "kicker_b" for r in range(16, 22)}, pb))
    col("kick_3", lanes(0, {r: "kicker_c" for r in range(16, 22)}, pa))

    def profile_col(name, prefix, height, top_tile, alt):
        """A wedge column: lane rows are `<p>body`, rows 15 upward carry the
        profile.  height h = number of profile rows above the lane band."""
        body = "rbody_" if prefix == "r" else "hbody_"
        fill = prefix + "p_fill"
        tiles = list(pa if alt == 0 else pb)
        for r in range(16, 22):
            tiles[r - 8] = body + "ab"[(r + alt) % 2]
        # the row directly above the lanes is row 15 (index 7): height 1
        # puts the profile tile there; height h stacks fill up to row 15
        for k in range(height):
            row = 15 - k
            tiles[row - 8] = top_tile if k == height - 1 else fill
        col(name, tiles, "ab" if alt == 0 else "cd")
    for prefix, heights in (("r", (1, 2)), ("h", (2, 3, 4))):
        for h in heights:
            for nm, tt in (("us", prefix + "p_up_s"), ("ga", prefix + "p_up_ga"),
                           ("gb", prefix + "p_up_gb"), ("tp", prefix + "p_top"),
                           ("ds", prefix + "p_dn_s"), ("da", prefix + "p_dn_ga"),
                           ("db", prefix + "p_dn_gb")):
                profile_col("%s%d_%s" % (prefix, h, nm), prefix, h, tt, h % 2)
    # finish and gate
    col("fin_1", lanes(0, {r: ("chequer_a" if r % 2 else "chequer_b")
                          for r in range(16, 22)}, pa))
    col("fin_2", lanes(0, {r: "finish_post" for r in (16, 21)}, pb))
    col("gate_1", lanes(0, {16: "gate_post", 21: "gate_post"}, pa))
    col("gate_2", lanes(0, {16: "gate_bar", 17: "gate_shadow"}, pb))
    return C


# a piece: (name, class, script, [columns])
def make_pieces():
    P = []

    def piece(name, cls, script, cols):
        P.append((name, cls, script, cols))
    piece("plain_a", "plain", "-", ["plain_a"])
    piece("plain_b", "plain", "-", ["plain_b"])
    piece("plain_c", "plain", "-", ["plain_c"])
    piece("rough_a", "rough", "rough", ["rough_1", "rough_2"])
    piece("rough_b", "rough", "rough", ["rough_1", "rough_2", "rough_1"])
    piece("rough_c", "rough", "rough", ["rough_3", "rough_4"])
    piece("mud_a", "mud", "mud", ["mud_1", "mud_2"])
    piece("mud_b", "mud", "mud", ["mud_1", "mud_2", "mud_1"])
    piece("mud_c", "mud", "mud", ["mud_3", "mud_4"])
    piece("hurdle_lo_a", "hurdle_lo", "hurdle_lo", ["hlo_1"])
    piece("hurdle_lo_b", "hurdle_lo", "hurdle_lo", ["hlo_2"])
    piece("hurdle_lo_c", "hurdle_lo", "hurdle_lo", ["hlo_3"])
    piece("hurdle_hi_a", "hurdle_hi", "hurdle_hi", ["hhi_1"])
    piece("hurdle_hi_b", "hurdle_hi", "hurdle_hi", ["hhi_2"])
    piece("hurdle_hi_c", "hurdle_hi", "hurdle_hi", ["hhi_3"])
    piece("arrow_a", "arrow", "arrow", ["arr_1", "arr_2"])
    piece("arrow_b", "arrow", "arrow", ["arr_1", "arr_2", "arr_1"])
    piece("arrow_c", "arrow", "arrow", ["arr_3", "arr_2"])
    piece("kicker_a", "kicker", "kicker", ["kick_1", "kick_2", "kick_3"])
    piece("kicker_b", "kicker", "kicker", ["kick_1", "kick_2", "kick_2", "kick_3"])
    # ramps: a..h differ in length and height
    piece("ramp_a", "ramp", "ramp_a", ["r1_us", "r1_tp", "r1_ds"])
    piece("ramp_b", "ramp", "ramp_b", ["r1_ga", "r1_gb", "r1_tp", "r1_da", "r1_db"])
    piece("ramp_c", "ramp", "ramp_c", ["r1_us", "r1_tp", "r1_tp", "r1_ds"])
    piece("ramp_d", "ramp", "ramp_d", ["r2_us", "r2_tp", "r2_ds"])
    piece("ramp_e", "ramp", "ramp_e", ["r2_ga", "r2_gb", "r2_tp", "r2_da", "r2_db"])
    piece("ramp_f", "ramp", "ramp_f", ["r1_ga", "r1_gb", "r1_tp", "r1_ds"])
    piece("ramp_g", "ramp", "ramp_g", ["r2_us", "r2_tp", "r2_tp", "r2_da", "r2_db"])
    piece("ramp_h", "ramp", "ramp_h", ["r1_us", "r1_tp", "r1_da", "r1_db"])
    # hills: long rises with a down-slope
    piece("hill_a", "hill", "hill_a", ["h2_ga", "h2_gb", "h2_tp", "h2_tp", "h2_da", "h2_db"])
    piece("hill_b", "hill", "hill_b", ["h3_ga", "h3_gb", "h3_tp", "h3_tp", "h3_ds"])
    piece("hill_c", "hill", "hill_c", ["h2_us", "h2_tp", "h2_tp", "h2_tp", "h2_da", "h2_db"])
    piece("hill_d", "hill", "hill_d", ["h3_us", "h3_tp", "h3_tp", "h3_da", "h3_db"])
    piece("hill_e", "hill", "hill_e", ["h4_ga", "h4_gb", "h4_tp", "h4_tp", "h4_tp", "h4_ds"])
    piece("hill_f", "hill", "hill_f", ["h4_us", "h4_tp", "h4_da", "h4_db"])
    piece("finish", "finish", "finish", ["fin_1", "fin_2"])
    piece("start_gate", "gate", "-", ["gate_1", "gate_2"])
    return P


# element scripts: (x, kind, n)  x = column offset within the piece.
#   ANG n   set the pitch target
#   DEFER n wait n columns before the next keyframe fires
#   H n     set the ground height (in 2-px steps)
SCRIPTS = {
    "rough": [(0, "ANG", 0), (0, "H", 0)],
    "mud": [(0, "ANG", 0), (0, "H", 0)],
    "hurdle_lo": [(0, "H", 1)],
    "hurdle_hi": [(0, "H", 3)],
    "arrow": [(0, "H", 0)],
    "kicker": [(0, "ANG", 1), (0, "H", 1), (1, "H", 2), (2, "ANG", 2), (2, "H", 3)],
    "ramp_a": [(0, "ANG", 2), (0, "H", 4), (1, "H", 4), (2, "ANG", 5), (2, "H", 0)],
    "ramp_b": [(0, "ANG", 1), (0, "H", 2), (1, "H", 4), (2, "ANG", 0), (2, "H", 4),
               (3, "ANG", 4), (3, "H", 2), (4, "H", 0)],
    "ramp_c": [(0, "ANG", 2), (0, "H", 4), (1, "DEFER", 1), (3, "ANG", 5), (3, "H", 0)],
    "ramp_d": [(0, "ANG", 3), (0, "H", 8), (1, "H", 8), (2, "ANG", 6), (2, "H", 0)],
    "ramp_e": [(0, "ANG", 2), (0, "H", 4), (1, "H", 8), (2, "ANG", 0), (2, "H", 8),
               (3, "ANG", 5), (3, "H", 4), (4, "H", 0)],
    "ramp_f": [(0, "ANG", 1), (0, "H", 2), (1, "H", 4), (2, "ANG", 0), (3, "ANG", 5),
               (3, "H", 0)],
    "ramp_g": [(0, "ANG", 3), (0, "H", 8), (1, "DEFER", 1), (3, "ANG", 4), (3, "H", 4),
               (4, "H", 0)],
    "ramp_h": [(0, "ANG", 2), (0, "H", 4), (1, "H", 4), (2, "ANG", 4), (2, "H", 2),
               (3, "H", 0)],
    "hill_a": [(0, "ANG", 1), (0, "H", 4), (1, "H", 8), (2, "ANG", 0), (2, "H", 8),
               (4, "ANG", 4), (4, "H", 4), (5, "H", 0)],
    "hill_b": [(0, "ANG", 1), (0, "H", 6), (1, "H", 12), (2, "ANG", 0), (2, "H", 12),
               (4, "ANG", 6), (4, "H", 0)],
    "hill_c": [(0, "ANG", 3), (0, "H", 8), (1, "ANG", 0), (1, "H", 8), (1, "DEFER", 2),
               (4, "ANG", 4), (4, "H", 4), (5, "H", 0)],
    "hill_d": [(0, "ANG", 3), (0, "H", 12), (1, "ANG", 0), (1, "DEFER", 1), (3, "ANG", 4),
               (3, "H", 6), (4, "H", 0)],
    "hill_e": [(0, "ANG", 1), (0, "H", 8), (1, "H", 16), (2, "ANG", 0), (2, "H", 16),
               (3, "DEFER", 1), (5, "ANG", 6), (5, "H", 0)],
    "hill_f": [(0, "ANG", 3), (0, "H", 16), (1, "ANG", 0), (2, "ANG", 4), (2, "H", 8),
               (3, "H", 0)],
    "finish": [(0, "H", 0)],
}


def write_tiles(T):
    out = ["# EXCITEBIKE tile set: 8x8, `.` = palette slot 0, 0-9a-f = a slot.",
           "# Original artwork, drawn by tools/excitebike_art.py (procedural); after",
           "# generation THIS FILE is the source of record.  `class` feeds the",
           "# 256-byte element class table (SPEC.md 102.2).", ""]
    for name, (g, cls) in T.items():
        out.append("tile %s class=%s" % (name, cls))
        out.append(g.text())
        out.append("")
    return "\n".join(out)


def write_pieces(C, P):
    out = ["# EXCITEBIKE pieces: dictionary COLUMNS (14 tile names for tile rows",
           "# 8..21, plus a hedge pair for rows 22-23), the PIECES built from",
           "# columns, and the element SCRIPTS (x = column offset in the piece;",
           "# ANGn pitch target, DEFERn wait n columns, Hn ground height).", ""]
    for name, (tiles, hedge) in C.items():
        out.append("col %s hedge=%s  %s" % (name, hedge, " ".join(tiles)))
    out.append("")
    for name, cls, script, cols in P:
        out.append("piece %s class=%s script=%s cols=%s" % (name, cls, script, ",".join(cols)))
    out.append("")
    for name, keys in SCRIPTS.items():
        out.append("script %s: %s" % (name, "  ".join("%d %s%d" % (x, k, n) for x, k, n in keys)))
    out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# the banner / crowd picture (64 tile columns x 8 tile rows)
# ---------------------------------------------------------------------------
def make_top():
    rows = []
    rng = Rng(5150)
    for r in range(5):
        row = []
        for x in range(64):
            row.append("crowd_" + "abcd"[(x * 3 + r * 5 + rng.next(2)) % 4])
        rows.append(row)
    # banner: row 5 sky with pennants and clouds, row 6 plates, row 7 fence
    r5, r6, r7 = [], [], []
    for x in range(64):
        k = x % 16
        r5.append("pennant_a" if k in (2, 3) else "pennant_b" if k in (10, 11)
                  else "sky_cloud" if k in (6, 14) else "sky_a")
        r6.append("plate_a" if 4 <= k < 8 else "plate_b" if 12 <= k < 16 else "sky_a")
        r7.append("fence_a" if x % 2 == 0 else "fence_b")
    rows += [r5, r6, r7]
    return rows


def write_top(rows):
    out = ["# EXCITEBIKE top picture: 64 columns x 8 tile rows = 512 x 64 px",
           "# (rows 0-4 crowd, 5-7 banner).  One row per line, tile names.", ""]
    for i, r in enumerate(rows):
        out.append("row %d: %s" % (i, " ".join(r)))
    out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# 5x7 alphabet, hand written for this project
# ---------------------------------------------------------------------------
GLYPHS5 = {
    " ": ".....|.....|.....|.....|.....|.....|.....",
    "!": "..#..|..#..|..#..|..#..|..#..|.....|..#..",
    '"': ".#.#.|.#.#.|.....|.....|.....|.....|.....",
    "#": ".#.#.|#####|.#.#.|.#.#.|#####|.#.#.|.....",
    "$": "..#..|.####|#.#..|.###.|..#.#|####.|..#..",
    "&": ".##..|#..#.|.##..|.#.#.|#..##|#..#.|.##.#",
    "%": "##..#|##.#.|...#.|..#..|.#...|.#.##|#..##",
    "'": "..#..|..#..|.....|.....|.....|.....|.....",
    "(": "...#.|..#..|.#...|.#...|.#...|..#..|...#.",
    ")": ".#...|..#..|...#.|...#.|...#.|..#..|.#...",
    "*": ".....|#.#.#|.###.|#####|.###.|#.#.#|.....",
    "+": ".....|..#..|..#..|#####|..#..|..#..|.....",
    ",": ".....|.....|.....|.....|..#..|..#..|.#...",
    "-": ".....|.....|.....|#####|.....|.....|.....",
    ".": ".....|.....|.....|.....|.....|.##..|.##..",
    "/": "....#|...#.|...#.|..#..|.#...|.#...|#....",
    "0": ".###.|#...#|#..##|#.#.#|##..#|#...#|.###.",
    "1": "..#..|.##..|..#..|..#..|..#..|..#..|.###.",
    "2": ".###.|#...#|....#|...#.|..#..|.#...|#####",
    "3": "####.|....#|....#|.###.|....#|....#|####.",
    "4": "...#.|..##.|.#.#.|#..#.|#####|...#.|...#.",
    "5": "#####|#....|####.|....#|....#|#...#|.###.",
    "6": ".###.|#....|#....|####.|#...#|#...#|.###.",
    "7": "#####|....#|...#.|..#..|.#...|.#...|.#...",
    "8": ".###.|#...#|#...#|.###.|#...#|#...#|.###.",
    "9": ".###.|#...#|#...#|.####|....#|....#|.###.",
    ":": ".....|.##..|.##..|.....|.##..|.##..|.....",
    ";": ".....|.##..|.##..|.....|.##..|..#..|.#...",
    "<": "...#.|..#..|.#...|#....|.#...|..#..|...#.",
    "=": ".....|.....|#####|.....|#####|.....|.....",
    ">": ".#...|..#..|...#.|....#|...#.|..#..|.#...",
    "?": ".###.|#...#|....#|...#.|..#..|.....|..#..",
    "@": ".###.|#...#|#.###|#.#.#|#.###|#....|.###.",
    "A": ".###.|#...#|#...#|#####|#...#|#...#|#...#",
    "B": "####.|#...#|#...#|####.|#...#|#...#|####.",
    "C": ".###.|#...#|#....|#....|#....|#...#|.###.",
    "D": "####.|#...#|#...#|#...#|#...#|#...#|####.",
    "E": "#####|#....|#....|####.|#....|#....|#####",
    "F": "#####|#....|#....|####.|#....|#....|#....",
    "G": ".###.|#...#|#....|#.###|#...#|#...#|.####",
    "H": "#...#|#...#|#...#|#####|#...#|#...#|#...#",
    "I": ".###.|..#..|..#..|..#..|..#..|..#..|.###.",
    "J": "..###|...#.|...#.|...#.|...#.|#..#.|.##..",
    "K": "#...#|#..#.|#.#..|##...|#.#..|#..#.|#...#",
    "L": "#....|#....|#....|#....|#....|#....|#####",
    "M": "#...#|##.##|#.#.#|#.#.#|#...#|#...#|#...#",
    "N": "#...#|##..#|#.#.#|#..##|#...#|#...#|#...#",
    "O": ".###.|#...#|#...#|#...#|#...#|#...#|.###.",
    "P": "####.|#...#|#...#|####.|#....|#....|#....",
    "Q": ".###.|#...#|#...#|#...#|#.#.#|#..#.|.##.#",
    "R": "####.|#...#|#...#|####.|#.#..|#..#.|#...#",
    "S": ".####|#....|#....|.###.|....#|....#|####.",
    "T": "#####|..#..|..#..|..#..|..#..|..#..|..#..",
    "U": "#...#|#...#|#...#|#...#|#...#|#...#|.###.",
    "V": "#...#|#...#|#...#|#...#|#...#|.#.#.|..#..",
    "W": "#...#|#...#|#...#|#.#.#|#.#.#|##.##|#...#",
    "X": "#...#|#...#|.#.#.|..#..|.#.#.|#...#|#...#",
    "Y": "#...#|#...#|.#.#.|..#..|..#..|..#..|..#..",
    "Z": "#####|....#|...#.|..#..|.#...|#....|#####",
    "[": ".###.|.#...|.#...|.#...|.#...|.#...|.###.",
    "\\": ".....|#####|#####|#####|#####|#####|.....",     # HUD gauge: a full cell (SPEC.md 102.3)
    "]": ".....|#####|#...#|#...#|#...#|#####|.....",     # HUD gauge: an empty cell
    "^": "..#..|.#.#.|#...#|.....|.....|.....|.....",
    "_": ".....|.....|.....|.....|.....|.....|#####",
}


def make_font():
    """64 glyphs = ASCII 32..95 (upper case; lower case folds on print)."""
    out = []
    for code in range(32, 96):
        ch = chr(code)
        pat = GLYPHS5[ch].split("|")
        assert len(pat) == 7 and all(len(p) == 5 for p in pat), ch
        rows = ["." + p + ".." for p in pat] + ["........"]
        out.append((code, rows))
    return out


def write_font(font):
    out = ["# EXCITEBIKE font: 64 glyphs = ASCII 32..95, 8x8, `#` on.  A hand-written",
           "# 5x7 alphabet drawn for this project (one blank row and column of",
           "# leading).  Lower case folds to upper on print.", ""]
    for code, rows in font:
        out.append("glyph %d" % code)
        out.extend(r.replace(".", ".") for r in
                   ("".join("#" if c == "#" else "." for c in row) for row in rows))
        out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# bike and rider poses (24x24, drawn by shape then rasterised)
# ---------------------------------------------------------------------------
def rot(p, a, pivot):
    c, s = math.cos(a), math.sin(a)
    x, y = p[0] - pivot[0], p[1] - pivot[1]
    return (pivot[0] + x * c - y * s, pivot[1] + x * s + y * c)


def dist_seg(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def in_poly(px, py, pts):
    inside = False
    n = len(pts)
    j = n - 1
    for i in range(n):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > py) != (yj > py) and px < (xj - xi) * (py - yi) / (yj - yi + 1e-12) + xi:
            inside = not inside
        j = i
    return inside


class Shapes:
    """An ordered shape list; later shapes paint over earlier ones."""

    def __init__(self):
        self.s = []

    def circle(self, c, r, ink):
        self.s.append(("c", c, r, ink))

    def ring(self, c, ro, ri, ink):
        self.s.append(("r", c, ro, ri, ink))

    def seg(self, a, b, t, ink):
        self.s.append(("s", a, b, t, ink))

    def poly(self, pts, ink):
        self.s.append(("p", pts, ink))

    def transform(self, a, pivot, shift=(0, 0)):
        out = Shapes()
        for sh in self.s:
            k = sh[0]
            f = lambda p: tuple(v + d for v, d in zip(rot(p, a, pivot), shift))
            if k == "c":
                out.s.append(("c", f(sh[1]), sh[2], sh[3]))
            elif k == "r":
                out.s.append(("r", f(sh[1]), sh[2], sh[3], sh[4]))
            elif k == "s":
                out.s.append(("s", f(sh[1]), f(sh[2]), sh[3], sh[4]))
            else:
                out.s.append(("p", [f(p) for p in sh[1]], sh[2]))
        return out

    def raster(self, w=24, h=24):
        g = [["."] * w for _ in range(h)]
        for y in range(h):
            for x in range(w):
                px, py = x + 0.5, y + 0.5
                v = None
                for sh in self.s:
                    k = sh[0]
                    if k == "c":
                        if math.hypot(px - sh[1][0], py - sh[1][1]) <= sh[2]:
                            v = sh[3]
                    elif k == "r":
                        d = math.hypot(px - sh[1][0], py - sh[1][1])
                        if sh[3] <= d <= sh[2]:
                            v = sh[4]
                    elif k == "s":
                        if dist_seg(px, py, sh[1][0], sh[1][1], sh[2][0], sh[2][1]) <= sh[3] / 2:
                            v = sh[4]
                    else:
                        if in_poly(px, py, sh[1]):
                            v = sh[2]
                if v is not None:
                    g[y][x] = v
        return g


def outline(g):
    """Add an opaque-black outline (ink 0) around every drawn pixel that
    borders transparency, 4-neighbour, never overwriting an ink."""
    h, w = len(g), len(g[0])
    out = [row[:] for row in g]
    for y in range(h):
        for x in range(w):
            if g[y][x] != ".":
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h and g[ny][nx] not in (".", "0"):
                    out[y][x] = "0"
                    break
    return out


# bike frame (facing right) in a 24-unit local space.  rear axle (5.5,17.5),
# front axle (18.5,17.5); ink 1 rider colour, 2 bike body, 3 skin/highlight,
# 0 tyre/outline.
def bike_shapes(spoke=0.0, lean=0.0, crouch=0.0, wheel_dy=0.0):
    S = Shapes()
    rear = (5.5, 17.5 + wheel_dy)
    front = (18.5, 17.5 + wheel_dy)
    for c in (rear, front):
        S.ring(c, 4.5, 3.1, "0")                       # tyre: the inside stays open
        for k in range(2):
            a = spoke + k * math.pi / 2
            d = (math.cos(a) * 3.0, math.sin(a) * 3.0)
            S.seg((c[0] - d[0], c[1] - d[1]), (c[0] + d[0], c[1] + d[1]), 0.9, "3")
        S.circle(c, 1.1, "2")                           # hub
    # swing arm, rear fender, frame, tank, engine, fork, bars
    S.seg(rear, (11.5, 15.0 + crouch), 1.6, "2")
    S.poly([(3.0, 12.8), (9.0, 11.8 + crouch), (9.5, 13.2 + crouch), (3.6, 14.2)], "2")     # seat/fender
    S.poly([(9.0, 11.0 + crouch), (15.0, 10.8 + crouch), (15.6, 13.0 + crouch),
            (9.6, 13.6 + crouch)], "2")                                                      # tank
    S.circle((11.2, 15.4 + crouch), 2.0, "0")                                                # engine
    S.seg((15.0, 12.4 + crouch), front, 1.6, "2")                                            # fork
    S.seg((15.0, 12.4 + crouch), (14.4, 10.0 + crouch), 1.3, "2")                            # bar post
    S.seg((13.2, 9.8 + crouch), (16.4, 10.2 + crouch), 1.2, "0")                             # bars
    return S


def rider_shapes(lean=0.0, crouch=0.0, arm=0.0, knee=0.0):
    """Rider over the seat.  lean > 0 leans forward (right)."""
    S = Shapes()
    hip = (8.6, 11.2 + crouch)
    sh = (9.6 + lean * 4.6, 5.4 + crouch + abs(lean) * 1.2)
    head = (sh[0] + 2.0 + lean, sh[1] - 2.2)
    grip = (14.4 + arm, 10.4 + crouch)
    footpeg = (11.0 + knee * 0.6, 15.4 + crouch)
    S.seg(hip, (12.6 + knee, 13.2 + crouch), 2.2, "1")           # thigh
    S.seg((12.6 + knee, 13.2 + crouch), footpeg, 1.8, "1")       # shin
    S.seg(hip, sh, 3.0, "1")                                       # torso
    S.seg(sh, ((sh[0] + grip[0]) / 2 + 0.6, (sh[1] + grip[1]) / 2 + 1.0), 2.0, "1")  # upper arm
    S.seg(((sh[0] + grip[0]) / 2 + 0.6, (sh[1] + grip[1]) / 2 + 1.0), grip, 1.8, "3")  # forearm/glove
    S.circle(head, 2.4, "1")                                       # helmet
    S.poly([(head[0] + 0.4, head[1] - 0.4), (head[0] + 3.0, head[1] + 0.2),
            (head[0] + 2.8, head[1] + 1.8), (head[0] + 0.4, head[1] + 1.4)], "3")   # visor / face
    return S


def pose(angle=0.0, spoke=0.0, lean=0.0, crouch=0.0, wheel_dy=0.0, arm=0.0,
         knee=0.0, rider=True, shift=(0.0, 0.0), riderdy=0.0):
    B = bike_shapes(spoke, lean, crouch, wheel_dy)
    R = rider_shapes(lean, crouch + riderdy, arm, knee) if rider else Shapes()
    S = Shapes()
    S.s = B.s + R.s
    pivot = (5.5, 17.5)
    # rotate about the rear axle (a pitch-up lifts the nose: negative angle)
    T = S.transform(math.radians(angle), pivot, shift)
    return outline(T.raster())


def lying_pose(angle, shift=(0, 0), rider_off=(8.0, 0.0), tumble=0.0):
    """A bike on its side (crash slide) or tumbling, with the rider apart."""
    B = bike_shapes(0.3)
    T = B.transform(math.radians(angle), (12.0, 12.0), shift)
    S = Shapes()
    S.s = T.s
    rc = (12.0 + rider_off[0], 12.0 + rider_off[1])
    R = Shapes()
    R.seg((rc[0] - 3.0, rc[1]), (rc[0] + 2.0, rc[1] - 0.6), 3.4, "1")
    R.seg((rc[0] - 3.0, rc[1]), (rc[0] - 6.0, rc[1] + 1.6 + tumble), 2.0, "1")
    R.seg((rc[0] + 2.0, rc[1] - 0.6), (rc[0] + 5.0, rc[1] + 1.0 - tumble), 1.8, "1")
    R.circle((rc[0] + 3.6, rc[1] - 1.8), 2.6, "1")
    R.circle((rc[0] + 4.4, rc[1] - 1.4), 1.2, "3")
    S.s += R.s
    return outline(S.raster())


def walking_pose(step):
    """The rider on foot pushing the bike (recovery): upright figure."""
    S = Shapes()
    S.seg((11.0, 10.5), (11.0, 5.5), 3.6, "1")                 # torso
    S.circle((11.4, 3.0), 2.7, "1")
    S.poly([(11.8, 2.6), (14.2, 3.0), (14.0, 4.4), (11.8, 4.2)], "3")
    a = 2.6 if step == 0 else -2.6
    S.seg((11.0, 10.5), (11.0 - a, 16.0), 2.4, "1")            # legs
    S.seg((11.0, 10.5), (11.0 + a, 16.0), 2.4, "1")
    S.seg((11.0 - a, 16.0), (11.0 - a + 1.5, 17.6), 2.0, "0")
    S.seg((11.0 + a, 16.0), (11.0 + a + 1.5, 17.6), 2.0, "0")
    S.seg((11.0, 6.5), (16.0, 9.5), 2.0, "3")                  # arm to the bars
    # the bike, small, rolling ahead of the rider
    for cx in (15.0, 22.0):
        S.ring((cx, 19.5), 2.6, 1.3, "0")
    S.seg((15.0, 19.5), (18.5, 15.5), 1.6, "2")
    S.seg((18.5, 15.5), (22.0, 19.5), 1.6, "2")
    S.seg((16.0, 12.5), (20.6, 12.5), 1.4, "0")
    S.seg((18.5, 15.5), (17.0, 12.6), 1.4, "2")
    return outline(S.raster())


POSES = [
    ("level_a", pose(spoke=0.0)),
    ("level_b", pose(spoke=0.78)),
    ("pitch_up_1", pose(angle=-9, lean=-0.35, shift=(0, -1.0))),
    ("pitch_up_2", pose(angle=-19, lean=-0.55, shift=(0.4, -1.2), spoke=0.3)),
    ("pitch_up_3", pose(angle=-30, lean=-0.75, shift=(0.8, -0.6), spoke=0.5)),
    ("pitch_up_4", pose(angle=-42, lean=-0.9, shift=(1.6, 0.2), spoke=0.9)),
    ("pitch_dn_1", pose(angle=7, lean=0.55, crouch=0.6, shift=(0, -0.4))),
    ("pitch_dn_2", pose(angle=15, lean=0.8, crouch=0.8, shift=(-0.4, -1.2), spoke=0.3)),
    ("pitch_dn_3", pose(angle=25, lean=1.0, crouch=1.0, shift=(-1.2, -2.2), spoke=0.6)),
    ("climb_1", pose(angle=-5, lean=0.5, crouch=0.4, shift=(0, -0.6))),
    ("climb_2", pose(angle=-11, lean=0.6, crouch=0.5, shift=(0.2, -1.0), spoke=0.4)),
    ("climb_3", pose(angle=-17, lean=0.7, crouch=0.6, shift=(0.5, -1.0), spoke=0.8)),
    ("air_level", pose(angle=-2, lean=0.3, crouch=-0.6, wheel_dy=-0.6, shift=(0, -1.6), spoke=1.2)),
    ("air_nose_up", pose(angle=-14, lean=-0.1, crouch=-0.5, wheel_dy=-0.6, shift=(0.4, -1.6), spoke=0.4)),
    ("air_nose_dn", pose(angle=13, lean=0.7, crouch=-0.2, wheel_dy=-0.4, shift=(-0.8, -2.4), spoke=0.9)),
    ("brace", pose(angle=5, lean=0.65, crouch=1.0, wheel_dy=-0.2, shift=(0, -0.6), knee=0.6)),
    ("land_squash", pose(crouch=1.6, wheel_dy=0.4, lean=0.5, riderdy=0.6, shift=(0, 0.4), knee=1.0)),
    ("slide_1", lying_pose(78, shift=(0.0, 3.2), rider_off=(2.0, 3.0))),
    ("slide_2", lying_pose(84, shift=(-0.5, 3.6), rider_off=(1.0, 3.6), tumble=0.6)),
    ("tumble_1", lying_pose(-40, shift=(-1.5, -0.5), rider_off=(5.5, 1.5), tumble=1.0)),
    ("tumble_2", lying_pose(-130, shift=(-1.0, 0.0), rider_off=(4.5, -4.0), tumble=-1.0)),
    ("tumble_3", lying_pose(-215, shift=(-1.0, 1.0), rider_off=(-1.5, 3.5), tumble=0.5)),
    ("walk_1", walking_pose(0)),
    ("walk_2", walking_pose(1)),
]


def small_sprites():
    # shadow 16x4: opaque ink 0 (drawn dithered by the back ends)
    sh = ["..000000000000..",
          ".00000000000000.",
          ".00000000000000.",
          "..000000000000.."]
    d1 = ["........", "........", "...33...", "..3003..",
          "..3003..", "...33...", "........", "........"]
    d2 = ["........", ".3....3.", "..3333..", ".30..03.",
          ".30..03.", "..3333..", ".3....3.", "........"]
    d3 = ["3......3", "........", "..3..3..", "........",
          "........", "..3..3..", "........", "3......3"]
    return [("shadow", 16, 4, sh), ("dust_1", 8, 8, d1), ("dust_2", 8, 8, d2),
            ("dust_3", 8, 8, d3)]


def write_poses(poses, smalls):
    out = ["# EXCITEBIKE poses: 24x24, `.` transparent, `0` opaque outline, `1` rider",
           "# colour (a per-rider variable), `2` bike body, `3` highlight / skin.",
           "# Drawn by shape (wheel rings, frame segments, rider blob and helmet)",
           "# by tools/excitebike_art.py; this file is the source of record.",
           "# Every pose: side-on, facing right, at most 260 opaque pixels.", ""]
    for name, grid in poses:
        out.append("pose %s 24x24" % name)
        out.extend("".join(row) for row in grid)
        out.append("")
    out.append("# small sprites (same inks)")
    out.append("")
    for name, w, h, rows in smalls:
        out.append("sprite %s %dx%d" % (name, w, h))
        out.extend(rows)
        out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# splash (layer description; rasterised by the compiler at three sizes)
# ---------------------------------------------------------------------------
# Colours here are the DESKTOP's 16 colours (the OS palette, EGA order), not the
# game's DAC slots: the splash paints into the desktop.  Coordinates are in
# VGA-native pixels of a 432x264 canvas; the compiler re-renders the same
# layers at 432x132 for the compact CGA desktop (vector layers stay crisp).
SPLASH = {
    "size": [432, 264],
    "reserve": [104, 166, 328, 261],
    "layers": [
        {"t": "rect", "r": [0, 0, 432, 264], "c": 1},
        {"t": "rect", "r": [0, 66, 432, 88], "c": 11},
        {"t": "rect", "r": [0, 88, 432, 110], "c": 14},
        {"t": "rect", "r": [0, 110, 432, 132], "c": 12},
        {"t": "rect", "r": [0, 132, 432, 154], "c": 4},
        {"t": "circle", "c0": [72, 150], "r": 58, "c": 14},
        {"t": "stripes", "r": [14, 118, 132, 150], "c": 12, "step": 6, "h": 3},
        {"t": "rect", "r": [0, 150, 432, 264], "c": 6},
        {"t": "rect", "r": [0, 150, 432, 153], "c": 0},
        {"t": "rect", "r": [0, 226, 432, 264], "c": 8},
        {"t": "poly", "pts": [[104, 166], [244, 166], [244, 118]], "c": 14},
        {"t": "line", "p0": [124, 166], "p1": [124, 152], "w": 6, "c": 4},
        {"t": "line", "p0": [150, 166], "p1": [150, 143], "w": 6, "c": 4},
        {"t": "line", "p0": [176, 166], "p1": [176, 134], "w": 6, "c": 4},
        {"t": "line", "p0": [202, 166], "p1": [202, 125], "w": 6, "c": 4},
        {"t": "line", "p0": [228, 166], "p1": [228, 121], "w": 6, "c": 4},
        {"t": "line", "p0": [104, 166], "p1": [244, 118], "w": 3, "c": 0},
        {"t": "line", "p0": [244, 118], "p1": [244, 166], "w": 3, "c": 0},
        {"t": "ellipse", "c0": [346, 160], "rx": 46, "ry": 5, "c": 0},
        {"t": "pose", "name": "air_nose_up", "x": 300, "y": 56, "s": 4,
         "inks": [0, 12, 14, 15]},
        {"t": "text", "s": "EXCITEBIKE", "x": 96, "y": 12, "sc": 3, "c": 15, "o": 0,
         "shadow": 4},
        {"t": "text", "s": "MOTOCROSS", "x": 144, "y": 42, "sc": 2, "c": 14, "o": 0},
        {"t": "border", "w": 8, "c": [15, 0]},
    ],
    "help": [
        {"t": "rect", "r": [8, 8, 315, 259], "c": 0},
        {"t": "frame", "r": [8, 8, 315, 259], "c": 9},
        {"t": "rect", "r": [316, 8, 424, 259], "c": 0},
        {"t": "pose", "name": "level_a", "x": 328, "y": 20, "s": 4,
         "inks": [0, 12, 14, 15]},
        {"t": "pose", "name": "air_level", "x": 328, "y": 132, "s": 4,
         "inks": [0, 12, 14, 15]},
    ],
}


def write_splash():
    return json.dumps(SPLASH, indent=1) + "\n"


def write_palette():
    return json.dumps(PALETTE, indent=1) + "\n"


# ---------------------------------------------------------------------------
def build():
    T = make_tiles()
    C = make_columns()
    P = make_pieces()
    used = {c for _, _, _, cols in P for c in cols}
    missing = used - set(C)
    assert not missing, missing
    C = {n: v for n, v in C.items() if n in used}     # no dictionary column nobody uses
    files = {
        "palette.json": write_palette(),
        "tiles.txt": write_tiles(T),
        "pieces.txt": write_pieces(C, P),
        "top.txt": write_top(make_top()),
        "font.txt": write_font(make_font()),
        "poses.txt": write_poses(POSES, small_sprites()),
        "splash.json": write_splash(),
    }
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    files = build()
    if a.write:
        os.makedirs(ART, exist_ok=True)
        for name, text in files.items():
            with open(os.path.join(ART, name), "w", newline="\n") as f:
                f.write(text)
            print("wrote", os.path.join("apps/excitebike/art", name), len(text), "bytes")
        return 0
    if a.check:
        bad = 0
        for name, text in files.items():
            p = os.path.join(ART, name)
            cur = open(p).read() if os.path.exists(p) else None
            if cur != text:
                print("DIFFERS from the procedure:", name, "(hand edits are allowed;"
                      " this only says the file is no longer the generator's output)")
                bad += 1
        return 1 if bad else 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())

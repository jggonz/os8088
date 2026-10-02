#!/usr/bin/env python3
"""8BitBike asset compiler (SPEC.md 102.2, docs/plans/EXCITEBIKE-PLAN.md 12).

    python3 tools/excitebike_assets.py -o build/excitebike-art     # what `make` runs
    python3 tools/excitebike_assets.py --selfcheck                 # budgets + determinism

Compiles the COMMITTED art, tracks and sound sources under
apps/excitebike/ into the files the package and its disk carry.  Python
standard library only (no Pillow - a plain `make` must not need it) and
deterministic: the same inputs give byte-identical outputs, no timestamps, no
host font, sorted iteration.

It reads NOTHING outside apps/excitebike/: no ROM, no CHR file, no
disassembly, no environment variable (SPEC.md 102.2;
tests/unit/t_excitebike_clean.py holds the build path to it).

Outputs, in the -o directory:
    8BBV.GFX  8BBC.GFX          the adapter art (VGA 0Dh planar / CGA 320x200x4)
    8BITBIKE.VGA .CGA .HRC        the desktop splash + help art, EXF1 rows
    exbtables.inc               NASM: sizes, checksums, record ids, load-screen glyphs
    exbtracks.inc               NASM: the track streams (in the package image)
    exbscripts.inc              NASM: element scripts (Wave 3 includes them)
    EXB.SND  exbsnd.inc         from tools/excitebike_audio.py (Wave 5 incbin's it)
    exb-art.json                counts, budgets, pose boxes (the tests' sidecar)
    tiles.png poses.png pieces.png top.png scene-*.png splash-*.png   contact sheets
    .exb-art                    stamp: sha256 of every input (make's rebuild key)

EXF1 (the desktop front-end file; the row grammar is the one DrMarco's DMF1
uses, restated here because importing tools/drmario_assets.py would make
Pillow a dependency of `make`):
    +0 'EXF1'  +4 word width (432)  +6 word height  +8 word depth (4 planes or 1)
    +10 directory: 2 x height words, absolute offsets of each row (splash rows,
        then help rows); identical rows share one packet string.
    row packets: 1..127 = repeat the next byte n times; 128..255 = copy the next
        n-127 literal bytes; 0 ends the row.  A 4-plane row is 216 bytes,
        plane-major (54 per plane); a 1-bit row is 54 bytes.

8BBV.GFX / 8BBC.GFX:
    +0 magic 'EXBV'|'EXBC'  +4 word total length  +6 word checksum
    +8 byte format 1, byte adapter, word record count R
    +12 R x (word offset, word length)
    the checksum is the 16-bit sum of every byte of the file with the checksum
    word taken as zero.  Record ids are the EXBR_* equates in exbtables.inc.
"""
import argparse
import hashlib
import json
import math
import os
import re
import struct
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import excitebike_audio as AUDIO  # noqa: E402

SRC = os.path.join(ROOT, "apps", "excitebike")

# --- the hard budgets of plan 12.5 -------------------------------------------------
B_TILES = 80
B_GFX_V = 20480
B_GFX_C = 16384
B_SPLASH = 24000
B_SND = 3072
B_IMAGE = 61440
B_DISK360 = 130 * 1024
B_SPRITE_VGA = 44 * 1024
B_SPRITE_CGA = 17 * 1024
B_SPRITE_CGA_CC = 28 * 1024          # ...with the compiled hot poses after the blobs (sprite.inc XS_CLAIM_CC)
B_DICT = 72
LAP_MAX = 797
RUNWAY = 43
POSE_W = POSE_H = 24
POSE_OPAQUE_MAX = 260
NPOSES = 24               # authored poses; four EFFECT poses follow them (see EFFECT_POSES)
POSE_SHADOW = 24          # pose ids of the effects: the ground shadow, then three dust puffs
POSE_DUST = 25
NPOSES_ALL = NPOSES + 4
NFONT = 64
CLASSES = ["none", "plain", "rough", "mud", "hurdle_lo", "hurdle_hi", "arrow",
           "kicker", "ramp", "hill", "finish", "gate"]

EGA_RGB = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0),
           (170, 0, 170), (170, 85, 0), (170, 170, 170), (85, 85, 85), (85, 85, 255),
           (85, 255, 85), (85, 255, 255), (255, 85, 85), (255, 85, 255),
           (255, 255, 85), (255, 255, 255)]
CGA_RGB_HIGH = [(0, 0, 0), (85, 255, 85), (255, 85, 85), (255, 255, 85)]
CGA_RGB_LOW = [(0, 0, 0), (0, 170, 0), (170, 0, 0), (170, 85, 0)]
# splash colours (desktop EGA order) -> 1-bit level: 0 black, 1 checker, 2 white
MONO_LEVEL = [0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2]

# the poses' ink -> palette slot (ink 0 is the outline: black); ink 1 is the
# rider colour and is a per-rider variable, this is its default (red)
INK_SLOT = [0, 10, 11, 14]


class ArtError(Exception):
    pass


def fail(msg):
    raise ArtError(msg)


# =============================================================================
# reading the sources
# =============================================================================
def read(rel):
    with open(os.path.join(SRC, rel), encoding="utf-8") as f:
        return f.read()


def blocks(text):
    """Yield (header words, [body lines]) for blank-line separated blocks,
    skipping `#` comments."""
    cur = None
    for raw in text.splitlines():
        line = raw.rstrip()
        if line.startswith("#"):
            continue
        if not line.strip():
            if cur:
                yield cur
                cur = None
            continue
        if cur is None:
            cur = (line.split(), [])
        else:
            cur[1].append(line)
    if cur:
        yield cur


def load_palette():
    p = json.loads(read("art/palette.json"))
    if len(p["slots"]) != 16:
        fail("palette.json: need 16 slots")
    return p


MONO_SUB = {}       # tile name -> {slot: slot}, from tiles.txt's `mono=` attribute (see Art.rows)


def load_tiles():
    tiles = []          # (name, rows, class)
    names = set()
    for head, body in blocks(read("art/tiles.txt")):
        if head[0] != "tile":
            fail("tiles.txt: unexpected %r" % head)
        name = head[1]
        attrs = dict(a.split("=", 1) for a in head[2:])
        cls = attrs.get("class", "none")
        if "mono" in attrs:                 # `mono=9>8`: on the CGA and the Hercules this tile's slot 9 is drawn as slot 8
            MONO_SUB[name] = {int(a): int(b) for a, b in
                              (p.split(">") for p in attrs["mono"].split(","))}
        if cls not in CLASSES:
            fail("tile %s: unknown class %s" % (name, cls))
        if name in names:
            fail("tile %s defined twice" % name)
        if len(body) != 8 or any(len(r) != 8 for r in body):
            fail("tile %s is not 8x8" % name)
        rows = []
        for r in body:
            row = []
            for ch in r:
                if ch == ".":
                    row.append(0)
                elif ch in "0123456789abcdef":
                    row.append(int(ch, 16))
                else:
                    fail("tile %s: bad pixel %r" % (name, ch))
            rows.append(row)
        names.add(name)
        tiles.append((name, rows, cls))
    return tiles


def load_pieces(tile_index):
    cols = {}           # name -> (tile ids x14, hedge ids x2)
    col_order = []
    pieces = []         # dicts
    scripts = {}
    hedges = {"ab": ("hedge_a", "hedge_b"), "cd": ("hedge_c", "hedge_d")}
    for raw in read("art/pieces.txt").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        w = line.split()
        if w[0] == "col":
            name = w[1]
            hedge = "ab"
            rest = w[2:]
            if rest and rest[0].startswith("hedge="):
                hedge = rest[0][6:]
                rest = rest[1:]
            if len(rest) != 14:
                fail("col %s: %d tiles, want 14" % (name, len(rest)))
            for t in rest + list(hedges[hedge]):
                if t not in tile_index:
                    fail("col %s: unknown tile %s" % (name, t))
            if name in cols:
                fail("col %s defined twice" % name)
            cols[name] = ([tile_index[t] for t in rest],
                          [tile_index[t] for t in hedges[hedge]])
            col_order.append(name)
        elif w[0] == "piece":
            attrs = dict(a.split("=", 1) for a in w[2:])
            pieces.append({"name": w[1], "class": attrs["class"],
                           "script": attrs["script"],
                           "cols": attrs["cols"].split(",")})
        elif w[0] == "script":
            name = w[1].rstrip(":")
            toks = line.split(":", 1)[1].split()
            if len(toks) % 2:
                fail("script %s: odd token count" % name)
            keys = []
            for i in range(0, len(toks), 2):
                m = re.fullmatch(r"(ANG|DEFER|H)(\d+)", toks[i + 1])
                if not m:
                    fail("script %s: bad keyframe %r" % (name, toks[i + 1]))
                keys.append((int(toks[i]), m.group(1), int(m.group(2))))
            scripts[name] = keys
        else:
            fail("pieces.txt: unexpected %r" % w[0])
    return cols, col_order, pieces, scripts


def load_top(tile_index):
    rows = {}
    for raw in read("art/top.txt").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        m = re.fullmatch(r"row (\d+): (.*)", line)
        if not m:
            fail("top.txt: bad line %r" % line)
        toks = m.group(2).split()
        if len(toks) != 64:
            fail("top.txt row %s: %d tiles, want 64" % (m.group(1), len(toks)))
        for t in toks:
            if t not in tile_index:
                fail("top.txt: unknown tile %s" % t)
        rows[int(m.group(1))] = [tile_index[t] for t in toks]
    if sorted(rows) != list(range(8)):
        fail("top.txt: need rows 0..7")
    return [rows[i] for i in range(8)]


def load_poses():
    poses, sprites = [], []
    for head, body in blocks(read("art/poses.txt")):
        if head[0] == "pose":
            w, h = (int(v) for v in head[2].split("x"))
            if (w, h) != (POSE_W, POSE_H) or len(body) != h or any(len(r) != w for r in body):
                fail("pose %s is not 24x24" % head[1])
            poses.append((head[1], body))
        elif head[0] == "sprite":
            w, h = (int(v) for v in head[2].split("x"))
            if len(body) != h or any(len(r) != w for r in body):
                fail("sprite %s is not %s" % (head[1], head[2]))
            sprites.append((head[1], w, h, body))
        else:
            fail("poses.txt: unexpected %r" % head)
    for name, body in poses:
        for r in body:
            for ch in r:
                if ch not in ".0123":
                    fail("pose %s: bad ink %r" % (name, ch))
    return poses, sprites


def effect_poses(sprites):
    """The small sprites as full 24 x 24 poses, so the shadow and the dust are drawn
    by the machinery that draws a bike (SPEC.md 102.3): the shadow ellipse sits in
    rows 20-23 of its box, centred; a dust puff sits in the box's lower RIGHT corner
    (the record is placed a whole box to the left of the rider, so the puff trails
    the rear wheel and the two boxes never overlap: the CGA back end composes each
    box from the world alone).  Ids POSE_SHADOW, POSE_DUST .. POSE_DUST + 2."""
    by = {n: (w, h, b) for n, w, h, b in sprites}
    out = []
    for name, x0, y0 in (("shadow", 4, 20), ("dust_1", 16, 16), ("dust_2", 16, 16),
                         ("dust_3", 16, 16)):
        w, h, body = by[name]
        grid = [["."] * POSE_W for _ in range(POSE_H)]
        for r in range(h):
            for c in range(w):
                grid[y0 + r][x0 + c] = body[r][c]
        out.append(("fx_" + name, ["".join(g) for g in grid]))
    return out


def load_font():
    glyphs = {}
    for head, body in blocks(read("art/font.txt")):
        if head[0] != "glyph" or len(body) != 8 or any(len(r) != 8 for r in body):
            fail("font.txt: bad glyph block %r" % head)
        glyphs[int(head[1])] = [sum((1 << (7 - x)) for x, c in enumerate(r) if c == "#")
                                for r in body]
    if sorted(glyphs) != list(range(32, 96)):
        fail("font.txt: need exactly ASCII 32..95")
    return [glyphs[c] for c in range(32, 96)]


# =============================================================================
# tracks
# =============================================================================
def parse_track(text, name, pieces_by_name):
    t = {"file": name, "title": name, "theme": 0, "laps": 2, "par": None, "par2": None,
         "pass1": [], "swaps": []}
    mode = "pass1"
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        w = line.split()
        if w[0] == "track":
            t["title"] = line[5:].strip()
        elif w[0] == "theme":
            t["theme"] = int(w[1])
        elif w[0] == "laps":
            t["laps"] = int(w[1])
        elif w[0] in ("par", "par2"):
            m = re.fullmatch(r"(\d+):(\d\d)\.(\d\d)", w[1])
            if not m:
                fail("%s:%d: %s wants m:ss.cc" % (name, n, w[0]))
            t[w[0]] = (int(m.group(1)) * 60 + int(m.group(2))) * 100 + int(m.group(3))
            t[w[0] + "_note"] = " ".join(w[2:])
        elif w[0] == "pass2":
            mode = "pass2"
        elif mode == "pass2" and w[0] == "swap":
            t["swaps"].append((w[1], w[2]))
        elif mode == "pass1":
            if w[0] not in pieces_by_name:
                fail("%s:%d: unknown piece %s" % (name, n, w[0]))
            cnt = 1
            if len(w) > 1:
                m = re.fullmatch(r"x(\d+)", w[1])
                if not m:
                    fail("%s:%d: expected xN" % (name, n))
                cnt = int(m.group(1))
            t["pass1"] += [w[0]] * cnt
        else:
            fail("%s:%d: cannot parse %r" % (name, n, line))
    if t["laps"] != 2:
        fail("%s: this game runs two laps" % name)
    if not 0 <= t["theme"] < 5:
        fail("%s: theme out of range" % name)
    if t["par"] is None:
        fail("%s: no par time" % name)
    if t.get("par2") is None:
        fail("%s: no par2 time (the second pass round the courses has its own)" % name)
    swap = dict(t["swaps"])
    for a, b in t["swaps"]:
        if a not in pieces_by_name or b not in pieces_by_name:
            fail("%s: swap of unknown piece" % name)
    t["pass2"] = [swap.get(p, p) for p in t["pass1"]]
    return t


def lap_columns(plist, pieces_by_name):
    """-> (column names, [(x, piece) for every piece start])"""
    cols, starts = [], []
    for p in plist:
        starts.append((len(cols), p))
        cols += pieces_by_name[p]["cols"]
    return cols, starts


def rle(ids):
    out = []
    i = 0
    while i < len(ids):
        j = i
        while j < len(ids) and ids[j] == ids[i] and j - i < 255:
            j += 1
        out.append((ids[i], j - i))
        i = j
    return out


def check_track(t, pieces_by_name):
    """The authoring rules of plan 5, asserted so a hand edit cannot break the
    budget.  Returns per-lap statistics."""
    stats = []
    for lap, plist in enumerate((t["pass1"], t["pass2"]), 1):
        cols, starts = lap_columns(plist, pieces_by_name)
        n = len(cols)
        if n > LAP_MAX:
            fail("%s lap %d: %d columns, over the %d budget" % (t["file"], lap, n, LAP_MAX))
        plain = sum(len(pieces_by_name[p]["cols"]) for p in plist
                    if pieces_by_name[p]["class"] == "plain")
        if plain * 100 < 70 * n:
            fail("%s lap %d: only %d%% plain columns (>= 70%% needed, the skip-list"
                 " model of plan 4.3 depends on it)" % (t["file"], lap, plain * 100 // n))
        last_end = 0          # a lap opens with 8 plain columns to land in
        obst = 0
        for x, p in starts:
            pc = pieces_by_name[p]
            if pc["class"] in ("plain",):
                continue
            if pc["class"] != "finish":
                obst += 1
            if x - last_end < 8:
                fail("%s lap %d: %s at column %d is %d columns after the previous"
                     " obstacle (8 needed: a rider must be able to land)"
                     % (t["file"], lap, p, x, x - last_end))
            last_end = x + len(pc["cols"])
        if plist[-1] != "finish":
            fail("%s lap %d: a lap ends with the finish piece" % (t["file"], lap))
        stats.append({"columns": n, "plain_pct": plain * 100 // n, "obstacles": obst})
    return stats


# =============================================================================
# native tile / pose / font encodings
# =============================================================================
def vga_tile(rows):
    out = bytearray()
    for plane in range(4):
        for y in range(8):
            v = 0
            for x in range(8):
                v |= ((rows[y][x] >> plane) & 1) << (7 - x)
            out.append(v)
    return bytes(out)


def cga_tile(rows, ink):
    out = bytearray()
    for y in range(8):
        for half in (0, 4):
            v = 0
            for k in range(4):
                v |= ink[rows[y][half + k]] << (6 - 2 * k)
            out.append(v)
    return bytes(out)


# Hercules (SPEC.md 102.7): one NES pixel is TWO Hercules pixels, so a tile row is 16 pixels = 2
# bytes - exactly the CGA tile's shape, with a 2-bit PAIR where the CGA has a 2-bit ink.  The
# pair is (left, right): 0 = 00 black, 3 = 11 white, and the mid level is ONE pixel lit -
# 2 = 10 or 1 = 01.  A tile's mid level alternates 10 / 01 with (row + column) - a checkerboard
# of pairs, the row-alternating pattern of the plan - and a pose's or a glyph's is always 10:
# a pose is placed on any row, so a pattern that depends on the row would need a variant of
# every pose for each parity.
HERC_PAIR = (0, 2, 3)


def herc_tile(rows, level):
    out = bytearray()
    for y in range(8):
        for half in (0, 4):
            v = 0
            for k in range(4):
                lv = level[rows[y][half + k]]
                if lv == 1:
                    pair = 2 if (y + half + k) % 2 == 0 else 1
                else:
                    pair = HERC_PAIR[lv]
                v |= pair << (6 - 2 * k)
            out.append(v)
    return bytes(out)


def pose_masks(body, w=POSE_W, h=POSE_H):
    """-> 4 masks (opaque, ink1, ink2, ink3), each h rows of ceil(w/8) bytes."""
    bw = (w + 7) // 8
    layers = [bytearray(bw * h) for _ in range(4)]
    for y, row in enumerate(body):
        for x, ch in enumerate(row):
            if ch == ".":
                continue
            bit = 0x80 >> (x % 8)
            layers[0][y * bw + x // 8] |= bit
            if ch in "123":
                layers[int(ch)][y * bw + x // 8] |= bit
    return [bytes(l) for l in layers]


def cga_font_glyph(g):
    out = bytearray()
    for row in g:
        for half in (4, 0):
            v = 0
            for k in range(4):
                if row & (1 << (3 - k + half)):
                    v |= 3 << (6 - 2 * k)
            out.append(v)
    return bytes(out)


def dac6(rgb):
    return bytes(min(63, (c + 2) * 63 // 255) for c in rgb)


# =============================================================================
# GFX containers
# =============================================================================
R_TILES, R_BAND, R_COLL, R_CLASS, R_TOP, R_POSES, R_SPRITES, R_FONT, R_PAL, R_INFO = range(10)
RECORD_NAMES = ["TILES", "BAND", "COLL", "CLASS", "TOP", "POSES", "SPRITES", "FONT",
                "PAL", "INFO"]


def pack_gfx(magic, adapter, records):
    n = len(records)
    head = 12 + 4 * n
    if head + sum(len(r) for r in records) > 0xFFFF:
        fail("%s is %d bytes: over one segment" % (magic, head + sum(len(r) for r in records)))
    off = head
    directory = b""
    body = b""
    for r in records:
        directory += struct.pack("<HH", off + len(body), len(r))
        body += r
    total = head + len(body)
    if total > 0xFFFF:
        fail("%s is %d bytes: over one segment" % (magic, total))
    blob = bytearray(magic.encode() + struct.pack("<HH", total, 0) +
                     bytes([1, adapter]) + struct.pack("<H", n) + directory + body)
    s = sum(blob) & 0xFFFF
    struct.pack_into("<H", blob, 6, s)
    return bytes(blob), s


class Art:
    """Everything parsed and validated, ready to emit."""

    def __init__(self):
        self.palette = load_palette()
        self.tiles = load_tiles()
        self.tile_index = {t[0]: i for i, t in enumerate(self.tiles)}
        self.mono = {self.tile_index[n]: m for n, m in MONO_SUB.items()}
        for need in ("hedge_a", "hedge_b", "hedge_c", "hedge_d"):
            if need not in self.tile_index:
                fail("tiles.txt: missing %s" % need)
        (self.cols, self.col_order, self.pieces, self.scripts) = load_pieces(self.tile_index)
        self.pieces_by_name = {p["name"]: p for p in self.pieces}
        self.top = load_top(self.tile_index)
        self.poses, self.sprites = load_poses()
        self.font = load_font()
        self.splash = json.loads(read("art/splash.json"))
        self.tracks = []
        tdir = os.path.join(SRC, "tracks")
        for fn in sorted(os.listdir(tdir)):
            if fn.endswith(".trk"):
                self.tracks.append(parse_track(read("tracks/" + fn), fn, self.pieces_by_name))
        self.validate()

    # ---- validation (the grammar + the budgets that are about the source) ----
    def validate(self):
        if len(self.tiles) > B_TILES:
            fail("tiles.txt has %d tiles, over the %d budget" % (len(self.tiles), B_TILES))
        if len(self.cols) > B_DICT:
            fail("band dictionary has %d columns, over the %d budget" % (len(self.cols), B_DICT))
        if len(self.poses) != NPOSES:
            fail("poses.txt has %d poses, the plan stores %d" % (len(self.poses), NPOSES))
        self.poses = list(self.poses) + effect_poses(self.sprites)
        self.pose_stats = []
        for name, body in self.poses:
            opaque = sum(1 for r in body for ch in r if ch != ".")
            inks = {ch for r in body for ch in r} - {"."}
            if opaque > POSE_OPAQUE_MAX:
                fail("pose %s has %d opaque pixels, over %d" % (name, opaque, POSE_OPAQUE_MAX))
            if not inks <= set("0123"):
                fail("pose %s: more than three ink layers" % name)
            ys = [y for y, r in enumerate(body) if any(ch != "." for ch in r)]
            xs = [x for r in body for x, ch in enumerate(r) if ch != "."]
            if not ys:
                fail("pose %s is empty" % name)
            # the design rule of plan 4.1: every pose lives within its box, and
            # the outline row 23 / column 23 are not touched (room for a shift)
            self.pose_stats.append({"name": name, "opaque": opaque,
                                    "box": [min(xs), min(ys), max(xs), max(ys)]})
        for s in self.pieces:
            for c in s["cols"]:
                if c not in self.cols:
                    fail("piece %s: unknown column %s" % (s["name"], c))
            if s["script"] != "-" and s["script"] not in self.scripts:
                fail("piece %s: unknown script %s" % (s["name"], s["script"]))
            if s["class"] not in CLASSES:
                fail("piece %s: unknown class %s" % (s["name"], s["class"]))
        used = {c for s in self.pieces for c in s["cols"]}
        for c in self.col_order:
            if c not in used:
                fail("column %s is defined but no piece uses it" % c)
        for name, keys in self.scripts.items():
            width = None
            for p in self.pieces:
                if p["script"] == name:
                    width = len(p["cols"]) if width is None else min(width, len(p["cols"]))
            if width is None:
                fail("script %s belongs to no piece" % name)
            x = -1
            for kx, kind, n in keys:
                if kx < x:
                    fail("script %s does not terminate in order at x=%d" % (name, kx))
                x = kx
                if kx >= width:
                    fail("script %s keyframe x=%d is past its %d-column piece" % (name, kx, width))
                if kind == "ANG" and not 0 <= n <= 7:
                    fail("script %s: ANG%d out of 0..7" % (name, n))
                if kind == "DEFER" and not 1 <= n <= 31:
                    fail("script %s: DEFER%d out of 1..31" % (name, n))
                if kind == "H" and not 0 <= n <= 63:
                    fail("script %s: H%d out of 0..63" % (name, n))
        self.track_stats = [check_track(t, self.pieces_by_name) for t in self.tracks]
        if not self.tracks:
            fail("no tracks")
        for t in self.tracks:
            if self.top is None:
                fail("no top")
        # the class table: a tile belongs to one class
        self.class_of_tile = [CLASSES.index(t[2]) for t in self.tiles]
        # the columns' collision rows: tile rows 21..16 bottom-up
        self.col_ids = {n: i for i, n in enumerate(self.col_order)}

    # ---- emitted records ------------------------------------------------------
    def rec_band(self):
        out = bytearray()
        for n in self.col_order:
            t14, hedge = self.cols[n]
            out += bytes(t14) + bytes(hedge)
        return bytes(out)

    def rec_coll(self):
        out = bytearray()
        for n in self.col_order:
            t14, _ = self.cols[n]
            out += bytes(t14[13 - k] for k in range(6))     # rows 21..16
        return bytes(out)

    def ccls_bytes(self):
        """The class of the tile under each collision row of every dictionary
        column: 6 bytes a column, row 0 = the lane nearest the camera.  This is
        what the simulation reads (exb_ccls); rec_coll holds the tile ids."""
        out = bytearray()
        for n in self.col_order:
            t14, _ = self.cols[n]
            out += bytes(self.class_of_tile[t14[13 - k]] for k in range(6))
        return bytes(out)

    def rec_class(self):
        b = bytearray(256)
        for i, c in enumerate(self.class_of_tile):
            b[i] = c
        return bytes(b)

    def rec_top(self):
        return bytes(v for row in self.top for v in row)

    def rec_poses(self):
        out = bytearray([len(self.poses)])
        for name, body in self.poses:
            for m in pose_masks(body):
                out += m
        return bytes(out)

    def rec_sprites(self):
        out = bytearray([len(self.sprites)])
        for name, w, h, body in self.sprites:
            out += bytes([w, h])
            for m in pose_masks(body, w, h):
                out += m
        return bytes(out)

    def rows(self, i, kind="vga"):
        """Tile i's 8 rows of palette slots AS THE ADAPTER DRAWS THEM.  The VGA draws the source
        rows; the CGA ('cga') and the Hercules ('herc') draw a tile with a `mono=` attribute with
        those slots substituted - the lane dash is WHITE on the VGA and black on the two adapters
        whose ground is a flat colour that white maps to as well (SPEC.md 102.8.1).  Every consumer
        of a tile's pixels for those adapters (the compiler, the reference renderer, the tests)
        asks here, so there is one truth."""
        rows = self.tiles[i][1]
        sub = self.mono.get(i)
        if kind == "vga" or not sub:
            return rows
        return [[sub.get(v, v) for v in r] for r in rows]

    def theme_dac(self, theme):
        slots = [tuple(s[1]) for s in self.palette["slots"]]
        for k, v in theme["slots"].items():
            slots[int(k)] = tuple(v)
        return b"".join(dac6(c) for c in slots)

    def rec_pal_vga(self):
        out = bytearray()
        for th in self.palette["themes"]:
            out += self.theme_dac(th)
        out += bytes(INK_SLOT)
        return bytes(out)

    def rec_pal_cga(self):
        c = self.palette["cga"]
        return (bytes(c["p0_high"]) + bytes(c["p0_low"]) +
                bytes([c["regs"]["p0_high"], c["regs"]["p0_low"]]) + bytes(INK_SLOT))

    def rec_pal_herc(self):
        """The same record as the CGA's, so the sprite loader is shared: the 'ink' of a slot is
        its 2-bit PAIR (HERC_PAIR of the slot's level), both profiles alike, and no 3D9h value
        (the card has none; the back end never writes it)."""
        m = bytes(HERC_PAIR[self.palette["herc"][s]] for s in range(16))
        return m + m + bytes([0, 0]) + bytes(INK_SLOT)

    def rec_info(self, adapter):
        return struct.pack("<8H", len(self.tiles), len(self.col_order), len(self.poses),
                           len(self.sprites), len(self.tracks), NFONT, len(CLASSES),
                           adapter)

    def gfx_vga(self):
        recs = [b"".join(vga_tile(t[1]) for t in self.tiles), self.rec_band(),
                self.rec_coll(), self.rec_class(), self.rec_top(), self.rec_poses(),
                self.rec_sprites(),
                b"".join(bytes(g) for g in self.font), self.rec_pal_vga(),
                self.rec_info(0)]
        return pack_gfx("EXBV", 0, recs)

    def gfx_cga(self):
        ink = self.palette["cga"]["p0_high"]
        recs = [b"".join(cga_tile(self.rows(i, "cga"), ink) for i in range(len(self.tiles))), self.rec_band(),
                self.rec_coll(), self.rec_class(), self.rec_top(), self.rec_poses(),
                self.rec_sprites(),
                b"".join(cga_font_glyph(g) for g in self.font), self.rec_pal_cga(),
                self.rec_info(1)]
        return pack_gfx("EXBC", 1, recs)

    def gfx_herc(self):
        lv = self.palette["herc"]
        recs = [b"".join(herc_tile(self.rows(i, "herc"), lv) for i in range(len(self.tiles))), self.rec_band(),
                self.rec_coll(), self.rec_class(), self.rec_top(), self.rec_poses(),
                self.rec_sprites(),
                b"".join(cga_font_glyph(g) for g in self.font), self.rec_pal_herc(),
                self.rec_info(2)]
        return pack_gfx("EXBH", 2, recs)


# =============================================================================
# the desktop splash
# =============================================================================
def rle_row(data):
    out = bytearray()
    pos = 0
    n = len(data)
    while pos < n:
        end = pos + 1
        while end < n and end - pos < 127 and data[end] == data[pos]:
            end += 1
        if end - pos >= 3:
            out += bytes((end - pos, data[pos]))
        else:
            end = pos + 1
            while end < n and end - pos < 128:
                if data[end:end + 3] == data[end:end + 1] * 3:
                    break
                end += 1
            out.append(127 + end - pos)
            out += data[pos:end]
        pos = end
    out.append(0)
    return bytes(out)


class Splash:
    W, H = 432, 264

    def __init__(self, art):
        self.art = art
        self.font = art.font
        self.poses = dict(art.poses)

    # -- glyph sampling
    def glyph_on(self, ch, gx, gy):
        code = ord(ch.upper())
        if not 32 <= code < 96 or not (0 <= gx < 8 and 0 <= gy < 8):
            return False
        return bool(self.font[code - 32][gy] & (0x80 >> gx))

    def text_on(self, layer, px, py):
        sc, x0, y0 = layer["sc"], layer["x"], layer["y"]
        s = layer["s"]
        gx = math.floor((px - x0) / sc)
        gy = math.floor((py - y0) / sc)
        if gy < 0 or gy >= 8 or gx < 0 or gx >= 8 * len(s):
            return False
        return self.glyph_on(s[gx // 8], gx % 8, gy)

    def render(self, w, h, layers, base=None):
        """-> list of h rows of w EGA colour indices, sampled from the 432x264
        canvas at (x+.5, (y+.5)*264/h): vector layers re-render crisply at the
        compact height."""
        k = self.H / float(h)
        grid = base if base is not None else [[0] * w for _ in range(h)]

        def rows_for(y0, y1):
            a = max(0, int(math.floor(y0 / k)))
            b = min(h, int(math.ceil(y1 / k)))
            return range(a, b)
        for L in layers:
            t = L["t"]
            if t == "rect":
                x0, y0, x1, y1 = L["r"]
                xa, xb = max(0, x0), min(w, x1)
                for y in rows_for(y0, y1):
                    fy = (y + .5) * k
                    if y0 <= fy < y1:
                        grid[y][xa:xb] = [L["c"]] * (xb - xa)
            elif t in ("circle", "ellipse"):
                cx, cy = L["c0"]
                rx = L["r"] if t == "circle" else L["rx"]
                ry = L["r"] if t == "circle" else L["ry"]
                for y in rows_for(cy - ry, cy + ry + 1):
                    fy = (y + .5) * k
                    for x in range(max(0, int(cx - rx)), min(w, int(cx + rx) + 1)):
                        if ((x + .5 - cx) / rx) ** 2 + ((fy - cy) / ry) ** 2 <= 1.0:
                            grid[y][x] = L["c"]
            elif t == "poly":
                pts = [tuple(p) for p in L["pts"]]
                ys = [p[1] for p in pts]
                xs = [p[0] for p in pts]
                for y in rows_for(min(ys), max(ys) + 1):
                    fy = (y + .5) * k
                    for x in range(max(0, min(xs)), min(w, max(xs) + 1)):
                        if _in_poly(x + .5, fy, pts):
                            grid[y][x] = L["c"]
            elif t == "line":
                (ax, ay), (bx, by) = L["p0"], L["p1"]
                hw = L["w"] / 2.0
                for y in rows_for(min(ay, by) - hw, max(ay, by) + hw + 1):
                    fy = (y + .5) * k
                    for x in range(max(0, int(min(ax, bx) - hw)), min(w, int(max(ax, bx) + hw) + 1)):
                        if _dist_seg(x + .5, fy, ax, ay, bx, by) <= hw:
                            grid[y][x] = L["c"]
            elif t == "stripes":
                x0, y0, x1, y1 = L["r"]
                for y in rows_for(y0, y1):
                    fy = (y + .5) * k
                    if y0 <= fy < y1 and int((fy - y0) // L["h"]) % 2 == 0 \
                            and int(fy - y0) % L["step"] < L["h"]:
                        grid[y][max(0, x0):min(w, x1)] = [L["c"]] * (min(w, x1) - max(0, x0))
            elif t == "pose":
                body = self.poses[L["name"]]
                s = L["s"]
                inks = L["inks"]
                for y in rows_for(L["y"], L["y"] + 24 * s):
                    fy = (y + .5) * k
                    gy = int((fy - L["y"]) // s)
                    if not 0 <= gy < 24:
                        continue
                    for x in range(max(0, L["x"]), min(w, L["x"] + 24 * s)):
                        ch = body[gy][int((x + .5 - L["x"]) // s)]
                        if ch != ".":
                            grid[y][x] = inks[int(ch)]
            elif t == "text":
                sc = L["sc"]
                r = max(1.0, sc * 0.75)
                ln = len(L["s"]) * 8 * sc
                for y in rows_for(L["y"] - r - 1, L["y"] + 8 * sc + r + sc + 1):
                    fy = (y + .5) * k
                    for x in range(max(0, int(L["x"] - r - 1)), min(w, int(L["x"] + ln + r + sc + 2))):
                        px = x + .5
                        if self.text_on(L, px, fy):
                            grid[y][x] = L["c"]
                        elif L.get("o") is not None and any(
                                self.text_on(L, px + dx * r, fy + dy * r)
                                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1),
                                               (1, 1), (-1, -1), (1, -1), (-1, 1))):
                            grid[y][x] = L["o"]
                        elif L.get("shadow") is not None and self.text_on(L, px - sc, fy - sc):
                            grid[y][x] = L["shadow"]
            elif t == "border":
                bw = L["w"]
                c0, c1 = L["c"]
                for y in range(h):
                    for x in range(w):
                        fy = (y + .5) * k
                        if x < bw or x >= w - bw or fy < bw or fy >= self.H - bw:
                            grid[y][x] = c0 if ((x // bw) + int(fy // bw)) % 2 == 0 else c1
            elif t == "frame":
                x0, y0, x1, y1 = L["r"]
                for y in range(h):
                    fy = (y + .5) * k
                    if not y0 <= fy < y1:
                        continue
                    for x in range(x0, min(w, x1)):
                        if x in (x0, x1 - 1) or fy < y0 + k or fy >= y1 - k:
                            grid[y][x] = L["c"]
            else:
                fail("splash: unknown layer %s" % t)
        return grid

    def compose(self, h, help_page):
        w = self.W
        layers = list(self.art.splash["layers"])
        g = self.render(w, h, layers)
        # the runtime menu area stays black (font_run draws its text there)
        rx0, ry0, rx1, ry1 = self.art.splash["reserve"]
        k = self.H / float(h)
        for y in range(h):
            fy = (y + .5) * k
            if ry0 <= fy < ry1:
                g[y][rx0:rx1] = [0] * (rx1 - rx0)
        if help_page:
            g = self.render(w, h, self.art.splash["help"], g)
        return g

    def native_rows(self, g, mode):
        """mode 'vga': 216-byte plane-major rows; 'mono': 54-byte rows."""
        rows = []
        for y, row in enumerate(g):
            if mode == "vga":
                r = bytearray()
                for plane in range(4):
                    for x in range(0, self.W, 8):
                        v = 0
                        for j in range(8):
                            v |= ((row[x + j] >> plane) & 1) << (7 - j)
                        r.append(v)
                rows.append(bytes(r))
            else:
                r = bytearray()
                for x in range(0, self.W, 8):
                    v = 0
                    for j in range(8):
                        lvl = MONO_LEVEL[row[x + j]]
                        on = lvl == 2 or (lvl == 1 and (x + j + y) & 1 == 0)
                        v |= (1 if on else 0) << (7 - j)
                    r.append(v)
                rows.append(bytes(r))
        return rows

    def file(self, tag):
        h, mode, depth = {"VGA": (264, "vga", 4), "HRC": (264, "mono", 1),
                          "CGA": (132, "mono", 1)}[tag]
        pages = []
        rows = []
        for help_page in (False, True):
            g = self.compose(h, help_page)
            pages.append(g)
            rows += self.native_rows(g, mode)
        head = b"EXF1" + struct.pack("<HHH", self.W, h, depth)
        offset = len(head) + 2 * len(rows)
        directory = bytearray()
        payload = bytearray()
        shared = {}
        for r in rows:
            if r not in shared:
                shared[r] = offset + len(payload)
                payload += rle_row(r)
            directory += struct.pack("<H", shared[r])
        blob = head + bytes(directory) + bytes(payload)
        if len(blob) > 65535:
            fail("8BITBIKE.%s exceeds one segment" % tag)
        return blob, pages


def _dist_seg(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def _in_poly(px, py, pts):
    inside = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > py) != (yj > py) and px < (xj - xi) * (py - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


# =============================================================================
# PNG contact sheets (stdlib only)
# =============================================================================
def write_png(path, w, h, rgb_rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rgb_rows)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


class Img:
    def __init__(self, w, h, bg=(32, 32, 40)):
        self.w, self.h = w, h
        self.px = [[bg] * w for _ in range(h)]

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def block(self, x, y, s, c):
        for j in range(s):
            for i in range(s):
                self.put(x + i, y + j, c)

    def text(self, x, y, s, font, c=(255, 255, 255)):
        for i, ch in enumerate(s.upper()):
            code = ord(ch)
            if not 32 <= code < 96:
                continue
            g = font[code - 32]
            for gy in range(8):
                for gx in range(8):
                    if g[gy] & (0x80 >> gx):
                        self.put(x + i * 8 + gx, y + gy, c)

    def save(self, path):
        write_png(path, self.w, self.h, [[v for c in row for v in c] for row in self.px])


def slot_rgb(art, theme=0):
    slots = [tuple(s[1]) for s in art.palette["slots"]]
    for k, v in art.palette["themes"][theme]["slots"].items():
        slots[int(k)] = tuple(v)
    return slots


def draw_tile(img, x, y, s, rows, colour):
    for j in range(8):
        for i in range(8):
            img.block(x + i * s, y + j * s, s, colour(rows[j][i]))


def sheet_tiles(art, out):
    rgb = slot_rgb(art)
    S = 4
    cols = 12
    n = len(art.tiles)
    cw, ch = 8 * S + 8, 8 * S + 18
    img = Img(cols * cw + 8, ((n + cols - 1) // cols) * ch + 8)
    for i, (name, rows, cls) in enumerate(art.tiles):
        x = 8 + (i % cols) * cw - 4
        y = 8 + (i // cols) * ch - 4
        draw_tile(img, x, y, S, rows, lambda v: rgb[v])
        img.text(x, y + 8 * S + 1, "%d" % i, art.font, (255, 255, 255))
    img.save(os.path.join(out, "tiles.png"))
    # the same set through the 4-colour and 2-colour adapters
    ink = art.palette["cga"]["p0_high"]
    img2 = Img(cols * cw + 8, ((n + cols - 1) // cols) * ch + 8)
    herc = art.palette["herc"]
    for i, (name, rows, cls) in enumerate(art.tiles):
        x = 8 + (i % cols) * cw - 4
        y = 8 + (i // cols) * ch - 4
        draw_tile(img2, x, y, S, art.rows(i, "cga"), lambda v: CGA_RGB_HIGH[ink[v]])
        img2.text(x, y + 8 * S + 1, "%d" % i, art.font)
    img2.save(os.path.join(out, "tiles-cga.png"))
    img3 = Img(cols * cw + 8, ((n + cols - 1) // cols) * ch + 8)
    for i, (name, rows, cls) in enumerate(art.tiles):
        rows = art.rows(i, "herc")
        x = 8 + (i % cols) * cw - 4
        y = 8 + (i // cols) * ch - 4
        for j in range(8):
            for k in range(8):
                lv = herc[rows[j][k]]
                on = lv == 2 or (lv == 1 and (j + k) % 2 == 0)
                img3.block(x + k * S, y + j * S, S, (255, 255, 255) if on else (0, 0, 0))
        img3.text(x, y + 8 * S + 1, "%d" % i, art.font)
    img3.save(os.path.join(out, "tiles-herc.png"))


def pose_colour(ch, rgb, rider):
    return {"0": rgb[0], "1": rider, "2": rgb[INK_SLOT[2]], "3": rgb[INK_SLOT[3]]}[ch]


def sheet_poses(art, out):
    rgb = slot_rgb(art)
    S = 4
    per = 8
    cw, chh = 24 * S + 8, 24 * S + 18
    rows_n = (len(art.poses) + per - 1) // per
    variants = [("VGA RED", rgb[10]), ("VIOLET", (150, 60, 200)), ("TEAL", (30, 170, 170))]
    img = Img(per * cw + 8, rows_n * chh * 1 + 8 + 2 * (24 * 2 + 20) + 60)
    sky = rgb[7]
    for i, (name, body) in enumerate(art.poses):
        x = 8 + (i % per) * cw
        y = 8 + (i // per) * chh
        for j in range(24 * S):
            for k in range(24 * S):
                img.put(x + k, y + j, sky if ((k // 8) + (j // 8)) % 2 else
                        tuple(int(v * .85) for v in sky))
        for j, row in enumerate(body):
            for k, ch in enumerate(row):
                if ch != ".":
                    img.block(x + k * S, y + j * S, S, pose_colour(ch, rgb, rgb[10]))
        img.text(x, y + 24 * S + 2, name, art.font)
    # colour variants, 2 colours and 4 colours (rider colour is a variable)
    y0 = 8 + rows_n * chh + 8
    x = 8
    S2 = 2
    for v, (label, col) in enumerate(variants):
        for i in (0, 2, 12, 17):
            body = art.poses[i][1]
            for j, row in enumerate(body):
                for k, ch in enumerate(row):
                    if ch != ".":
                        img.block(x + k * S2, y0 + j * S2, S2, pose_colour(ch, rgb, col))
            x += 24 * S2 + 6
        img.text(x - 4 * (24 * S2 + 6), y0 + 24 * S2 + 2, label, art.font)
    ink = art.palette["cga"]["p0_high"]
    y1 = y0 + 24 * S2 + 16
    x = 8
    for i in (0, 2, 12, 17, 22, 5, 15, 19):
        body = art.poses[i][1]
        for j, row in enumerate(body):
            for k, ch in enumerate(row):
                if ch != ".":
                    slot = INK_SLOT[int(ch)]
                    img.block(x + k * S2, y1 + j * S2, S2, CGA_RGB_HIGH[ink[slot]])
        x += 24 * S2 + 6
    img.text(8, y1 + 24 * S2 + 2, "CGA 4 COLOUR", art.font)
    y2 = y1 + 24 * S2 + 16
    x = 8
    herc = art.palette["herc"]
    for i in (0, 2, 12, 17, 22, 5, 15, 19):
        body = art.poses[i][1]
        for j, row in enumerate(body):
            for k, ch in enumerate(row):
                if ch != ".":
                    lv = herc[INK_SLOT[int(ch)]] if ch != "0" else 0
                    on = lv == 2 or (lv == 1 and (j + k) % 2 == 0)
                    img.block(x + k * S2, y2 + j * S2, S2,
                              (255, 255, 255) if on else (0, 0, 0))
                    if ch == "0":
                        img.block(x + k * S2, y2 + j * S2, S2, (0, 0, 0))
        x += 24 * S2 + 6
    img.text(8, y2 + 24 * S2 + 2, "HERC 2 COLOUR", art.font)
    img.save(os.path.join(out, "poses.png"))


def col_rows(art, name):
    t14, hedge = art.cols[name]
    return t14 + hedge


def sheet_pieces(art, out):
    rgb = slot_rgb(art)
    S = 2
    pieces = art.pieces
    W = 1400
    x, y, rowh = 8, 8, 0
    placed = []
    for p in pieces:
        w = len(p["cols"]) * 8 * S
        if x + w + 8 > W:
            x, y = 8, y + rowh + 22
            rowh = 0
        placed.append((p, x, y))
        x += max(w, 70) + 12
        rowh = max(rowh, 16 * 8 * S)
    img = Img(W, y + rowh + 30)
    for p, px, py in placed:
        for ci, cn in enumerate(p["cols"]):
            for r, t in enumerate(col_rows(art, cn)):
                _tile_scaled(img, px + ci * 8 * S, py + r * 8 * S, S, art.tiles[t][1], rgb)
        img.text(px, py + 16 * 8 * S + 4, p["name"], art.font)
    img.save(os.path.join(out, "pieces.png"))
    # and through the 4-colour adapter, for the ramps-by-shape review
    ink = art.palette["cga"]["p0_high"]
    img2 = Img(W, img.h)
    for p, px, py in placed:
        for ci, cn in enumerate(p["cols"]):
            for r, t in enumerate(col_rows(art, cn)):
                _tile_scaled(img2, px + ci * 8 * S, py + r * 8 * S, S, art.rows(t, "cga"),
                             [CGA_RGB_HIGH[ink[v]] for v in range(16)])
        img2.text(px, py + 16 * 8 * S + 4, p["name"], art.font)
    img2.save(os.path.join(out, "pieces-cga.png"))


def _tile_scaled(img, x, y, s, rows, rgb):
    for j in range(8):
        for i in range(8):
            img.block(x + i * s, y + j * s, s, rgb[rows[j][i]])


def sheet_scene(art, out):
    rgb = slot_rgb(art)
    t = art.tracks[0]
    lap, _ = lap_columns(t["pass1"], art.pieces_by_name)
    ids = ["plain_a", "plain_b"] * (RUNWAY // 2) + ["plain_a"] + lap

    def scene(x_off, cmap, fmt, kind="vga"):
        img = Img(320, 200, (0, 0, 0))
        for r in range(8):
            for c in range(40):
                _tile_scaled(img, c * 8, r * 8, 1, art.rows(art.top[r][(c + x_off) % 64], kind), cmap)
        for c in range(40):
            rows = col_rows(art, ids[c + x_off])
            for r in range(16):
                _tile_scaled(img, c * 8, 64 + r * 8, 1, art.rows(rows[r], kind), cmap)
        # a rider on the first lane, a hud line
        body = dict(art.poses)["level_a"]
        px, py = 88, 64 + 8 * 9
        for j, row in enumerate(body):
            for k, ch in enumerate(row):
                if ch != ".":
                    img.put(px + k, py + j, fmt(ch))
        for c in range(40):
            img.block(c * 8, 192, 8, (0, 0, 0))
        img.text(8, 192, "TIME 00:00.0  TEMP", art.font)
        return img
    # the flat run, and a scroll position on the first ramp/obstacle
    first = 0
    for i, cn in enumerate(ids):
        if cn.startswith(("r1_", "r2_", "h2_", "h3_", "h4_")):
            first = i
            break
    for tag, x_off in (("a", 30), ("b", max(0, first - 20))):
        img = scene(x_off, rgb, lambda ch: rgb[INK_SLOT[int(ch)]] if ch != "0" else rgb[0])
        # upscale x2 for viewing
        big = Img(640, 400, (0, 0, 0))
        for y in range(200):
            for x in range(320):
                c = img.px[y][x]
                big.block(x * 2, y * 2, 2, c)
        big.save(os.path.join(out, "scene-vga-%s.png" % tag))
        ink = art.palette["cga"]["p0_high"]
        cmap = [CGA_RGB_HIGH[ink[v]] for v in range(16)]
        img = scene(x_off, cmap, lambda ch: CGA_RGB_HIGH[ink[INK_SLOT[int(ch)]]] if ch != "0" else cmap[0], "cga")
        big = Img(640, 400, (0, 0, 0))
        for y in range(200):
            for x in range(320):
                big.block(x * 2, y * 2, 2, img.px[y][x])
        big.save(os.path.join(out, "scene-cga-%s.png" % tag))
        # Hercules (SPEC.md 102.7): 45 columns, two card pixels a game pixel, a pair per pixel
        lv = art.palette["herc"]
        himg = Img(720, 200, (0, 0, 0))

        def hput(px, line, pair):
            for k in range(2):
                if pair & (2 >> k):
                    himg.put(px * 2 + k, line, (255, 255, 255))

        ids45 = ids + [ids[-1]] * 50
        for c in range(45):
            for r in range(24):
                if r < 8:
                    t = art.top[r][(c + x_off) % 64]
                else:
                    t = col_rows(art, ids45[c + x_off])[r - 8]
                for j in range(8):
                    for i in range(8):
                        l = lv[art.rows(t, "herc")[j][i]]
                        line, px = r * 8 + j, c * 8 + i
                        hput(px, line, (2 if (line + px) % 2 == 0 else 1) if l == 1 else HERC_PAIR[l])
        body = dict(art.poses)["level_a"]
        for j, row in enumerate(body):
            for k, ch in enumerate(row):
                if ch != ".":
                    l = lv[INK_SLOT[int(ch)]] if ch != "0" else 0
                    px, py = 88 + k, 64 + 8 * 9 + j
                    for q in range(2):
                        himg.put(px * 2 + q, py, (0, 0, 0))
                    hput(px, py, HERC_PAIR[l])
        big = Img(720, 400, (0, 0, 0))
        for y in range(200):
            for x in range(720):
                big.block(x, y * 2, 1, himg.px[y][x])
                big.block(x, y * 2 + 1, 1, himg.px[y][x])
        big.save(os.path.join(out, "scene-herc-%s.png" % tag))


def sheet_top(art, out):
    rgb = slot_rgb(art)
    img = Img(512, 64, (0, 0, 0))
    for r in range(8):
        for c in range(64):
            _tile_scaled(img, c * 8, r * 8, 1, art.tiles[art.top[r][c]][1], rgb)
    big = Img(1024, 128, (0, 0, 0))
    for y in range(64):
        for x in range(512):
            big.block(x * 2, y * 2, 2, img.px[y][x])
    big.save(os.path.join(out, "top.png"))


def sheet_splash(sp, out):
    for tag in ("VGA", "HRC", "CGA"):
        blob, pages = sp.file(tag)
        for pi, g in enumerate(pages):
            h = len(g)
            img = Img(432, h, (0, 0, 0))
            for y in range(h):
                for x in range(432):
                    v = g[y][x]
                    if tag == "VGA":
                        img.put(x, y, EGA_RGB[v])
                    else:
                        lvl = MONO_LEVEL[v]
                        on = lvl == 2 or (lvl == 1 and (x + y) & 1 == 0)
                        img.put(x, y, (255, 255, 255) if on else (0, 0, 0))
            img.save(os.path.join(out, "splash-%s%s.png" % (tag.lower(), "-help" if pi else "")))


# =============================================================================
# the generated NASM
# =============================================================================
def db_lines(label, data, per=16):
    out = [label + ":"]
    for i in range(0, len(data), per):
        out.append("    db " + ",".join(str(b) for b in data[i:i + per]))
    return out


def emit_tracks(art):
    L = ["; generated by tools/excitebike_assets.py - do not edit",
         "; track streams: (column id, run length) pairs ending in a 0 run; then",
         "; element triggers: word x, byte script id, ending in x = 0xFFFF",
         "EXB_NTRACKS equ %d" % len(art.tracks),
         "EXB_RUNWAY equ %d" % RUNWAY,
         "EXB_LAP_MAX equ %d" % LAP_MAX,
         "exb_tracks: dw " + ",".join("exb_t%d" % (i + 1) for i in range(len(art.tracks)))]
    script_ids = {n: i + 1 for i, n in enumerate(art.scripts)}
    for i, t in enumerate(art.tracks):
        tag = "exb_t%d" % (i + 1)
        laps = []
        for plist in (t["pass1"], t["pass2"]):
            cols, starts = lap_columns(plist, art.pieces_by_name)
            ids = [art.col_ids[c] for c in cols]
            trig = []
            for x, p in starts:
                sname = art.pieces_by_name[p]["script"]
                if sname != "-":
                    trig.append((x, script_ids[sname]))
            laps.append((cols, ids, trig))
        L.append("%s: db %d,%d" % (tag, t["theme"], t["laps"]))
        L.append("    dw %d,%d,%d" % (t["par"], len(laps[0][1]), len(laps[1][1])))
        L.append("    dw %s_s1,%s_s2,%s_g1,%s_g2,%s_name,%d" % ((tag,) * 5 + (t["par2"],)))
        L.append("%s_name: db '%s',0" % (tag, t["title"].upper()))
        for n, (cols, ids, trig) in enumerate(laps, 1):
            runs = rle(ids)
            L.append("%s_s%d:" % (tag, n))
            L.append("    db " + ",".join("%d,%d" % r for r in runs) + ",0,0")
            L.append("%s_g%d:" % (tag, n))
            L.append("    dw " + ",".join("%d,%d" % (x, s) for x, s in trig) + (",0FFFFh" if trig else "0FFFFh"))
    return "\n".join(L) + "\n"


def emit_scripts(art):
    L = ["; generated by tools/excitebike_assets.py - do not edit (Wave 3 includes this)",
         "; keyframe = byte x, byte code (bits 7-6 kind: 0 ANG, 1 DEFER, 2 H; bits 5-0 n);",
         "; each script ends with 0FFh.  Script ids are 1-based in this order."]
    names = list(art.scripts)
    L.append("EXB_NSCRIPTS equ %d" % len(names))
    L.append("exb_scripts: dw " + ",".join("exb_script_" + n for n in names))
    kinds = {"ANG": 0, "DEFER": 1, "H": 2}
    for n in names:
        data = []
        for x, k, v in art.scripts[n]:
            data += [x, (kinds[k] << 6) | v]
        data.append(0xFF)
        L.append("exb_script_%s: db %s" % (n, ",".join(str(b) for b in data)))
    return "\n".join(L) + "\n"


# =============================================================================
# the scroll engine's skip lists (SPEC.md 102.1)
# =============================================================================
# The world is stored sheared: the entering column x, pixel line l, lands in the
# cell that held line l + 1 of column x - 40.  If W(x, l) already equals that
# cell's content the write can be skipped, so for every (entering, leaving)
# pair the compiler lists the lines that DIFFER and the guest writes those
# runs only.  The top picture repeats every 64 columns, so its lists are per
# x mod 64 and legal for ANY column; the band's are per pair of the three plain
# columns and legal only for that pair.  Line 63 (top) and line 127 (band) are
# always listed: their neighbour line lies in the other strip / off the page.
RUN_SETUP = 155          # clocks to start a run (xv_runs)
LINE_CLK = 37            # clocks a line (movsb + add di,39), measured on MartyPC
PLAIN_IDS = 3            # plain_a, plain_b, plain_c: column ids 0..2


def col_lines(art, cid):
    t14, hedge = art.cols[art.col_order[cid]]
    lines = []
    for t in list(t14) + list(hedge):
        lines += [tuple(r) for r in art.tiles[t][1]]
    return lines


def top_lines(art, x):
    lines = []
    for r in range(8):
        lines += [tuple(row) for row in art.tiles[art.top[r][x % 64]][1]]
    return lines


def diff_lines(a, b, n):
    """Lines l of an n-line strip where a[l] != b[l + 1]; the last line always."""
    return [l for l in range(n - 1) if a[l] != b[l + 1]] + [n - 1]


def make_runs(lines):
    """Sorted line numbers -> (first, count) runs, merging a gap whenever the
    lines it would rewrite cost less than starting another run."""
    runs = []
    for l in lines:
        if runs and (l - (runs[-1][0] + runs[-1][1])) * LINE_CLK <= RUN_SETUP:
            runs[-1][1] = l - runs[-1][0] + 1
        else:
            runs.append([l, 1])
    return [tuple(r) for r in runs]


def skip_runs(art):
    top = [make_runs(diff_lines(top_lines(art, x), top_lines(art, x - 40), 64))
           for x in range(64)]
    band = []
    for e in range(PLAIN_IDS):
        for f in range(PLAIN_IDS):
            band.append(make_runs(diff_lines(col_lines(art, e), col_lines(art, f), 128)))
    return top, band


def runs_cover(runs, lines):
    got = {l for a, n in runs for l in range(a, a + n)}
    return set(lines) <= got


def emit_skip(art):
    top, band = skip_runs(art)
    labels = {}
    body = []

    def lab(runs):
        key = tuple(runs)
        if key not in labels:
            labels[key] = "exb_sk%d" % len(labels)
            body.append(labels[key] + ":")
            for a, n in runs:
                body.append("    dw %d" % (40 * a))
                body.append("    db %d,%d" % (a, n))
            body.append("    dw 0FFFFh")
        return labels[key]
    L = ["; the skip lists: (word 40 * first line, byte first line, byte count) runs",
         "; ending in 0FFFFh, for the plain-pair band columns and the 64 top columns",
         "exb_full_top: dw 0",
         "    db 0,64",
         "    dw 0FFFFh",
         "exb_full_band: dw 0",
         "    db 0,128",
         "    dw 0FFFFh"]
    tl = [lab(r) for r in top]
    bl = [lab(r) for r in band]
    L.append("exb_skip_top:")
    for i in range(0, 64, 8):
        L.append("    dw " + ",".join(tl[i:i + 8]))
    L.append("exb_skip_band: dw " + ",".join(bl))
    L += body
    return "\n".join(L) + "\n"


def skip_stats(art):
    top, band = skip_runs(art)
    def lines(rs):
        return sum(n for a, n in rs)
    tl = [lines(r) for r in top]
    bl = [lines(r) for r in band]
    return {"top_lines_avg": sum(tl) / 64.0, "top_runs_avg": sum(len(r) for r in top) / 64.0,
            "band_lines_avg": sum(bl) / 9.0, "band_runs_avg": sum(len(r) for r in band) / 9.0}


LOAD_LINES = ["8BITBIKE", "LOADING GRAPHICS"]


def emit_tables(art, gv, gc, gh, spl, snd_size):
    """exbtables.inc: everything the package image needs to know about the files."""
    chars = sorted({c for line in LOAD_LINES for c in line})
    L = ["; generated by tools/excitebike_assets.py - do not edit",
         "EXB_GFX_VGA_SIZE equ %d" % len(gv[0]), "EXB_GFX_VGA_SUM equ %d" % gv[1],
         "EXB_GFX_CGA_SIZE equ %d" % len(gc[0]), "EXB_GFX_CGA_SUM equ %d" % gc[1],
         "EXB_GFX_HRC_SIZE equ %d" % len(gh[0]), "EXB_GFX_HRC_SUM equ %d" % gh[1]]
    for tag in ("VGA", "CGA", "HRC"):
        L.append("EXB_FRONT_%s_SIZE equ %d" % (tag, len(spl[tag])))
    L += ["EXB_NTILES equ %d" % len(art.tiles), "EXB_NCOLS equ %d" % len(art.col_order),
          "EXB_NPOSES equ %d" % len(art.poses), "EXB_NSPRITES equ %d" % len(art.sprites),
          "EXB_NFONT equ %d" % NFONT, "EXB_NCLASSES equ %d" % len(CLASSES)]
    for i, n in enumerate(RECORD_NAMES):
        L.append("EXBR_%s equ %d" % (n, i))
    for i, c in enumerate(CLASSES):
        L.append("EXBC_%s equ %d" % (c.upper(), i))
    for i, c in enumerate(art.col_order):
        L.append("EXBCOL_%s equ %d" % (c.upper(), i))
    # the loading screen is resident: drawn before the first disk read, from
    # glyph rows compiled into the package (VGA: 1bpp rows; CGA: 2bpp words)
    L.append("EXB_LOAD_NCHARS equ %d" % len(chars))
    L.append("exb_ld_v:")
    L.append("    db " + ",".join(str(b) for c in chars
                                   for b in art.font[ord(c) - 32]))
    L.append("exb_ld_c:")
    L.append("    db " + ",".join(str(b) for c in chars
                                   for b in cga_font_glyph(art.font[ord(c) - 32])))
    for i, line in enumerate(LOAD_LINES):
        L.append("exb_ld_%d: db %s,255" % (i, ",".join(str(chars.index(c)) if c != " " else "254"
                                                       for c in line)))
    L.append("EXB_LOAD_LEN0 equ %d" % len(LOAD_LINES[0]))
    L.append("EXB_POSE_SHADOW equ %d" % POSE_SHADOW)
    L.append("EXB_POSE_DUST equ %d" % POSE_DUST)
    L += db_lines("exb_ccls", art.ccls_bytes(), 6)
    L.append(emit_skip(art))
    return "\n".join(L) + "\n"


# =============================================================================
# the compile
# =============================================================================
INPUT_DIRS = ("art", "tracks", "audio")


def input_files():
    out = []
    for d in INPUT_DIRS:
        base = os.path.join(SRC, d)
        for fn in sorted(os.listdir(base)):
            p = os.path.join(base, fn)
            if os.path.isfile(p):
                out.append(p)
    for t in ("excitebike_assets.py", "excitebike_audio.py"):
        out.append(os.path.join(HERE, t))
    return out


def stamp():
    h = hashlib.sha256()
    for p in input_files():
        h.update(os.path.relpath(p, ROOT).encode() + b"\0")
        h.update(open(p, "rb").read())
    return h.hexdigest()


def compile_all(out, sheets=True):
    os.makedirs(out, exist_ok=True)
    art = Art()
    gv = art.gfx_vga()
    gc = art.gfx_cga()
    gh = art.gfx_herc()
    sp = Splash(art)
    spl = {}
    for tag in ("VGA", "CGA", "HRC"):
        spl[tag] = sp.file(tag)[0]
    snd, sndinc, sndrep = AUDIO.compile_audio()
    files = {"8BBV.GFX": gv[0], "8BBC.GFX": gc[0], "8BBH.GFX": gh[0],
             "8BITBIKE.VGA": spl["VGA"], "8BITBIKE.CGA": spl["CGA"], "8BITBIKE.HRC": spl["HRC"],
             "EXB.SND": snd}
    texts = {"exbtables.inc": emit_tables(art, gv, gc, gh, spl, len(snd)),
             "exbtracks.inc": emit_tracks(art), "exbscripts.inc": emit_scripts(art),
             "exbsnd.inc": sndinc}
    report = {
        "tiles": len(art.tiles), "columns": len(art.col_order), "pieces": len(art.pieces),
        "scripts": len(art.scripts), "poses": art.pose_stats,
        "gfx_vga": len(gv[0]), "gfx_cga": len(gc[0]), "gfx_herc": len(gh[0]),
        "splash": {k: len(v) for k, v in spl.items()}, "snd": len(snd),
        "tracks": [{"file": t["file"], "title": t["title"], "theme": t["theme"],
                    "par_cs": t["par"], "laps": s} for t, s in zip(art.tracks, art.track_stats)],
        "records_vga": [RECORD_NAMES[i] for i in range(len(RECORD_NAMES))],
    }
    # the stamp goes FIRST: make's outputs depend on it, so a compile that dies
    # part-way leaves an up-to-date stamp and missing outputs, which make's
    # fallback recipe turns back into a re-run that shows the error
    with open(os.path.join(out, ".exb-art"), "w") as f:
        f.write(stamp() + "\n")
    for name, data in files.items():
        with open(os.path.join(out, name), "wb") as f:
            f.write(data)
    for name, text in texts.items():
        with open(os.path.join(out, name), "w", newline="\n") as f:
            f.write(text)
    with open(os.path.join(out, "exb-art.json"), "w", newline="\n") as f:
        json.dump(report, f, indent=1, sort_keys=True)
        f.write("\n")
    if sheets:
        sheet_tiles(art, out)
        sheet_poses(art, out)
        sheet_pieces(art, out)
        sheet_top(art, out)
        sheet_scene(art, out)
        sheet_splash(sp, out)
    return art, files, report


# =============================================================================
# --selfcheck: every budget, printed, and determinism
# =============================================================================
def dir_bytes(d):
    out = {}
    for fn in sorted(os.listdir(d)):
        p = os.path.join(d, fn)
        if os.path.isfile(p):
            out[fn] = open(p, "rb").read()
    return out


def package_sizes(artdir):
    """(image, bss, packed) of the package assembled from THESE sources against
    the art just compiled into `artdir` - the header's own numbers, the ones
    the loader checks.  None if nasm is not there (the Makefile needs it, so
    that is a broken machine, not a normal one)."""
    import shutil
    import subprocess
    nasm = shutil.which("nasm")
    if not nasm:
        return None
    with tempfile.TemporaryDirectory() as td:
        binp = os.path.join(td, "x.bin")
        r = subprocess.run([nasm, "-f", "bin", "-w+error", "-I", os.path.join(ROOT, "apps") + "/",
                            "-I", SRC + "/", "-I", artdir + "/", "-o", binp,
                            os.path.join(SRC, "excitebike.asm")],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=ROOT)
        if r.returncode:
            fail("the package does not assemble:\n" + r.stdout.decode(errors="replace"))
        image, bss = struct.unpack_from("<HH", open(binp, "rb").read(16), 8)
        o = os.path.join(td, "x.o88")
        r = subprocess.run([sys.executable, os.path.join(HERE, "os88pkg.py"),
                            "--compress-if=lz4", binp, "-o", o],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=ROOT)
        packed = os.path.getsize(o) if r.returncode == 0 else None
        return image, bss, packed


def clusters_kb(sizes):
    """KB a set of files occupies on the 360KB disk: 1KB clusters."""
    return sum((n + 1023) // 1024 for n in sizes)


def selfcheck(keep=None):
    ok = True
    lines = []

    def budget(name, actual, limit, note=""):
        nonlocal ok
        good = actual <= limit
        ok = ok and good
        lines.append("  %-34s %7d / %-7d %s %s" % (name, actual, limit,
                                                   "ok" if good else "OVER", note))
    with tempfile.TemporaryDirectory() as t1, tempfile.TemporaryDirectory() as t2:
        art, files, rep = compile_all(t1, sheets=False)
        compile_all(t2, sheets=False)
        a, b = dir_bytes(t1), dir_bytes(t2)
        if a != b:
            print("FAIL: two compiles differ:", [k for k in a if a[k] != b.get(k)])
            return 1
        lines.append("determinism: two compiles byte-identical (%d files, %d bytes)"
                     % (len(a), sum(len(v) for v in a.values())))
        lines.append("budgets (plan 12.5):")
        budget("tiles.txt tiles", len(art.tiles), B_TILES)
        budget("8BBV.GFX bytes", len(files["8BBV.GFX"]), B_GFX_V)
        budget("8BBC.GFX bytes", len(files["8BBC.GFX"]), B_GFX_C)
        for tag in ("VGA", "CGA", "HRC"):
            budget("8BITBIKE.%s bytes" % tag, len(files["8BITBIKE." + tag]), B_SPLASH)
        budget("EXB.SND bytes", len(files["EXB.SND"]), B_SND)
        budget("band dictionary columns", len(art.col_order), B_DICT)
        # sprite claims, plan 6: the pose layers expanded into x phases at load time,
        # computed here by the reference model (tools/exbsim.py) - the bytes the
        # guest's loader must produce, which tests/excitebike_video.py compares
        sys.path.insert(0, HERE)
        import exbsim
        budget("sprite claim VGA", len(exbsim.vga_blob_bytes(art)), B_SPRITE_VGA)
        budget("sprite claim CGA", len(exbsim.cga_blob_bytes(art)), B_SPRITE_CGA)
        # wave 6 (SPEC.md 102.7.4): the hot poses as code, after the blobs in the same claim
        budget("sprite claim VGA + compiled poses",
               len(exbsim.vga_blob_bytes(art)) + len(exbsim.vga_compiled(art)[1]), B_SPRITE_VGA)
        budget("sprite claim CGA/Herc + compiled poses",
               len(exbsim.cga_blob_bytes(art)) + len(exbsim.compiled_poses(art)[1]), B_SPRITE_CGA_CC)
        # the scroll engine's skip lists (SPEC.md 102.1): each must cover every
        # line that differs, and here is what they buy
        top_runs, band_runs = skip_runs(art)
        for x, runs in enumerate(top_runs):
            if not runs_cover(runs, diff_lines(top_lines(art, x), top_lines(art, x - 40), 64)):
                lines.append("  skip list for top column %d misses a differing line" % x)
                ok = False
        for i, runs in enumerate(band_runs):
            e, f = divmod(i, PLAIN_IDS)
            if not runs_cover(runs, diff_lines(col_lines(art, e), col_lines(art, f), 128)):
                lines.append("  skip list for band pair %d,%d misses a differing line" % (e, f))
                ok = False
        st = skip_stats(art)
        lines.append("skip lists: top columns write %.1f of 64 lines in %.1f runs, plain band pairs "
                     "%.1f of 128 in %.1f runs (a full column is 192 lines in 2 runs)"
                     % (st["top_lines_avg"], st["top_runs_avg"], st["band_lines_avg"],
                        st["band_runs_avg"]))
        budget("8BBH.GFX bytes", len(files["8BBH.GFX"]), B_GFX_C)
        ps = package_sizes(t1)
        if ps is not None:
            image, bss, packed = ps
            budget("package image + bss", image + bss, B_IMAGE,
                   "(image %d + bss %d, assembled from these sources)" % (image, bss))
            names = [packed if packed is not None else image,
                     os.path.getsize(os.path.join(SRC, "README.md"))]
            names += [len(files[n]) for n in ("8BBV.GFX", "8BBC.GFX", "8BBH.GFX", "8BITBIKE.VGA",
                                              "8BITBIKE.CGA", "8BITBIKE.HRC")]
            budget("whole game on 360KB (1KB clusters)", clusters_kb(names) * 1024, B_DISK360,
                   "= %d KB of 354" % clusters_kb(names))
        else:
            lines.append("  %-34s %7s / %-7d %s" % ("package image + bss", "-", B_IMAGE,
                                                    "(nasm not found)"))
        lines.append("sources:")
        lines.append("  %d tiles, %d dictionary columns, %d pieces, %d element scripts, "
                     "%d poses (+%d small sprites), %d glyphs"
                     % (len(art.tiles), len(art.col_order), len(art.pieces), len(art.scripts),
                        len(art.poses), len(art.sprites), NFONT))
        worst = max(art.pose_stats, key=lambda s: s["opaque"])
        lines.append("  largest pose: %s, %d opaque pixels of %d"
                     % (worst["name"], worst["opaque"], POSE_OPAQUE_MAX))
        for t, s in zip(art.tracks, art.track_stats):
            lines.append("  track %-14s theme %d, par %d:%02d.%02d %s: lap 1 %d cols, lap 2 %d cols,"
                         " %d%%/%d%% plain, %d/%d obstacles"
                         % (t["title"], t["theme"], t["par"] // 6000, t["par"] // 100 % 60,
                            t["par"] % 100, "(" + t["par_note"] + ")" if t.get("par_note") else "",
                            s[0]["columns"], s[1]["columns"],
                            s[0]["plain_pct"], s[1]["plain_pct"],
                            s[0]["obstacles"], s[1]["obstacles"]))
        # negative controls: the compiler must REFUSE what the budgets forbid
        lines.append("negative controls:")
        refused = _negative_controls(art)
        for what, r in refused:
            lines.append("  %-44s %s" % (what, "refused" if r else "NOT REFUSED"))
            ok = ok and r
        # the checksum the guest verifies
        for tag, name in (("EXBV", "8BBV.GFX"), ("EXBC", "8BBC.GFX")):
            blob = bytearray(files[name])
            want = struct.unpack_from("<H", blob, 6)[0]
            struct.pack_into("<H", blob, 6, 0)
            got = sum(blob) & 0xFFFF
            if got != want or struct.unpack_from("<H", blob, 4)[0] != len(blob):
                lines.append("  %s checksum/length WRONG" % name)
                ok = False
        lines.append("  GFX magic, length and 16-bit checksum verified for both files")
        # the audio compiler's own gate
        for l in AUDIO.compile_audio()[2]:
            lines.append(l)
        if keep:
            compile_all(keep)                   # with the contact sheets
    print("excitebike_assets selfcheck")
    print("\n".join(lines))
    print("excitebike_assets selfcheck: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


def _negative_controls(art):
    """Each input mutation below must be rejected by the checks that guard the
    budgets; a control that is accepted means the guard is dead."""
    results = []

    def rejected(fn):
        try:
            fn()
        except (ArtError, SystemExit):
            return True
        return False

    # a skip list that drops a differing line would leave a stale pixel line behind
    def lossy_list():
        runs = skip_runs(art)[1][0]
        if runs_cover(runs[1:], diff_lines(col_lines(art, 0), col_lines(art, 0), 128)):
            return
        raise ArtError("skip list misses a line")
    results.append(("skip list missing a differing line", rejected(lossy_list)))

    # too many dictionary columns
    def many_cols():
        a = Art.__new__(Art)
        a.__dict__.update(art.__dict__)
        a.cols = dict(art.cols)
        a.col_order = list(art.col_order)
        for i in range(B_DICT):
            a.cols["x%d" % i] = art.cols[art.col_order[0]]
            a.col_order.append("x%d" % i)
        a.validate()
    results.append(("dictionary over %d columns" % B_DICT, rejected(many_cols)))

    def fat_pose():
        a = Art.__new__(Art)
        a.__dict__.update(art.__dict__)
        a.poses = [("fat", ["1" * 24] * 24)] + art.poses[1:]
        a.validate()
    results.append(("pose over %d opaque pixels" % POSE_OPAQUE_MAX, rejected(fat_pose)))

    def long_lap():
        t = dict(art.tracks[0])
        t["pass1"] = t["pass1"] + ["plain_a"] * 900
        t["pass2"] = t["pass1"]
        check_track(t, art.pieces_by_name)
    results.append(("lap over %d columns" % LAP_MAX, rejected(long_lap)))

    def crowded():
        t = dict(art.tracks[0])
        t["pass1"] = ["plain_a"] * 30 + ["hurdle_lo_a", "hurdle_lo_a"] + ["plain_a"] * 30 + ["finish"]
        t["pass2"] = t["pass1"]
        check_track(t, art.pieces_by_name)
    results.append(("two obstacles closer than 8 columns", rejected(crowded)))

    def sparse():
        t = dict(art.tracks[0])
        seq = ["plain_a"] * 8 + ["hill_a"] * 20
        t["pass1"] = seq + ["plain_a"] * 8 + ["finish"]
        t["pass2"] = t["pass1"]
        check_track(t, art.pieces_by_name)
    results.append(("fewer than 70% plain columns", rejected(sparse)))

    def big_gfx():
        pack_gfx("EXBV", 0, [b"\0" * 70000])
    results.append(("GFX larger than one segment", rejected(big_gfx)))

    def bad_sfx():
        AUDIO.parse_sfx("boom 3 (0 100)\n")
    results.append(("sound effect with 0 frames", rejected(bad_sfx)))
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--out", help="output directory")
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--no-sheets", action="store_true")
    a = ap.parse_args()
    try:
        if a.selfcheck:
            return selfcheck(a.out)
        if not a.out:
            ap.error("need -o OUT or --selfcheck")
        art, files, rep = compile_all(a.out, sheets=not a.no_sheets)
    except ArtError as e:
        print("excitebike_assets: %s" % e, file=sys.stderr)
        return 1
    print("excitebike_assets: %d tiles, %d columns, %d pieces, 8BBV.GFX %d, 8BBC.GFX %d, "
          "splash %s, EXB.SND %d" % (len(art.tiles), len(art.col_order), len(art.pieces),
                                       len(files["8BBV.GFX"]), len(files["8BBC.GFX"]),
                                       "/".join(str(len(files["8BITBIKE." + t])) for t in ("VGA", "CGA", "HRC")),
                                       len(files["EXB.SND"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""PIXELSTEIN 3D's art pipeline (SPEC.md 97.4): the wall masters, the five ink
tables, the lazy art part and the byte-texture set the package transposes.

    python3 tools/pxsart.py [-o apps/pixelstein/pxart.inc] [--stream build/pxsart.bin]
                            [--placeholder [--force]] [--check] [--preview DIR]

THE MASTERS are 32 x 32 PNGs in MATERIAL INDICES - the sixteen colours of
apps/os88api.inc, one index a pixel, committed under apps/pixelstein/art/
(docs/plans/PIXELSTEIN-PLAN.md 12.1: the C64-ROM class of decision), one file
a material, `w01_grey_stone.png` .. `w15_jamb.png`, named by tools/pxslevel.py's
MAT_NAMES so that a level, a master and a menu row are one list. Nothing here
draws pixels the package reads: the masters go to the machine as an LZ4
STREAM (the lazy art part, build/pxsart.bin, apps/skies/csart.py's shape -
the loader expands it through OSAPI_DECOMP into a claim the program keeps),
each master 32 rows of 16 bytes, TWO TEXELS A BYTE with the even column in
the high nibble; and `px_bt_build` (pxgen.inc) transposes them at launch into
ONE resident byte-texture set for the backend in force - column-major, 32
texels a column, lit then dark - through the INK TABLE of that backend, which
is the one thing this file emits into the include beside the counts:

  CGA 320x200x4   a 2x2 dither of palette 0 (black, green, red, brown): each
                  of the sixteen colours matched, in linear light at gamma
                  1.5, against the 21 four-pixel patterns WolfensteinCGA's
                  cgaify.cpp carries (read for technique, PIXELSTEIN-PLAN
                  13's sixth graft); the byte is the EVEN row's four pixels
                  and the odd row is the scaler's `ror al, 1` x 2 (97.3)
  Hercules, WIN1  a density ladder - seeded from the nine 8-bit levels of
                  the same source (ff 7f 77 57 55 15 11 01 00, 8..0 dots of
                  eight) and CULLED by --preview: the three dense levels
                  striped under the odd-row phase and their dot-complements
                  took their places (HERC_LEVELS) - picked by Rec.601
                  luminance; the odd row is `ror al, cl`, CL = 3
  C160            the attribute nibble: the colour itself, and a darker
                  twin for the dark face - the byte holds texels u and u+1
                  of ONE ROW (97.4), so the set is built from column pairs
  Mode X          the DAC entry: the index lit, the index + 16 dark

A DARK FACE IS THE SAME COLOUR AT 55% and matched again, never "one pattern
down" - a pattern ladder has no order that survives four inks.

THE RECIPE for a generated master (process-7 of the plan's reviews): generate
flat, posterised, low-frequency at 256 or 512 square; downsample by an INTEGER
factor with a MODE filter (a box filter returns mush); snap to the sixteen
RGBs; and the sprite key comes from ALPHA, never from near-magenta - index 5
is the key, an image model puts near-magenta inside a sprite, and `--check`
refuses a master with index 5 in it. Walls are opaque; a master with an alpha
channel is refused until wave 3's sprites give alpha a meaning.

`--placeholder` writes a procedural set of fifteen (deterministic, so the
include and the stream rebuild byte for byte) for a tree the image model has
not reached yet; `--force` overwrites masters that exist. `--preview DIR`
renders every master through every ink table at each adapter's pixel aspect
(VGA 1.00, Hercules 1.55, CGA 2.40) and PRINTS THE LOSABLE CRITERION - brick
against grey stone, distinguishable at 64 columns on the CGA4 and Hercules
sets - which `--check` (and tests/unit/t_pxsart.py) asserts. stdlib only:
`make` runs this to pack the stream.

THE SPRITES (wave 3, SPEC.md 97.4, 97.6) ride the same stream after the
walls, and THE KEY IS ALPHA: a sprite master is an RGBA PNG whose alpha is
0 (transparent) or 255 (opaque) at every pixel and nothing between; an
opaque pixel is one of the sixteen colours, and index 5 (magenta) is refused
inside one exactly as it is inside a wall - the key never comes from a
colour. Three sizes, one contract each (the names are SPRITES', PICKUPS'
and WEAPONS' below, under apps/pixelstein/art/):

  the guard and the decorations   32 x 32     g_f<0..4>_w<0|1>.png (five
                                              facings x two walk phases),
                                              g_shoot0/1, g_pain, g_die0..2,
                                              g_dead; d_<name>.png x 6
  the pickups                     16 x 16     p_<name>.png x 8, PLACED by
                                              the tool in the lower middle
                                              of a 32 x 32 frame (rows
                                              16..31, columns 8..23) so a
                                              pickup sits on the floor at
                                              half a wall's height
  the weapon                      16 x 24     wp_<knife|pistol|mgun>_<0..2>
                                              .png: 16 columns, 24 rows -
                                              the tool pads each frame to
                                              32 texel rows so the Full-res
                                              24-row scaler reproduces the
                                              24 authored rows exactly

The five guard facings are what the eight the engine draws are made from
(97.6): f0 faces the viewer, f1 is turned so its RIGHT side comes toward the
viewer (a three-quarter view), f2 shows its right side, f3 its right side
from behind, f4 its back; the engine mirrors f3, f2 and f1 for the left
side. Every frame goes to the machine as 512 packed texel nibbles (row
major, the even column in the high nibble, a transparent texel 0) and 128
alpha bytes (row major, bit 7 the leftmost column), and px_spr_build
(pxgen.inc) transposes it into part 4's column-major texels and a RUN TABLE
per column - the opaque spans, at most PXS_MAXRUNS = 4 a column, which
`--check` refuses past (a column with five spans is a sprite the post walk
cannot draw in four patched calls; simplify the art). The losable criterion
for the guard: the front (f0) and the right side (f2) must be
distinguishable at TWELVE columns - the width a guard has at two tiles -
on the CGA4 and Hercules sets, which `--preview` writes side by side
(spr-<backend>-front-side.png) and `--check` asserts.
"""
import argparse
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88lz                                               # noqa: E402
import pxslevel                                             # noqa: E402

ART_DIR = os.path.join(ROOT, "apps", "pixelstein", "art")
DEFAULT_OUT = os.path.join(ROOT, "apps", "pixelstein", "pxart.inc")
DEFAULT_STREAM = os.path.join(ROOT, "build", "pxsart.bin")
TEX = 32                        # texels a side
NWALL = 15                      # materials 1..15 (0 is open; 97.1)
WALLSZ = TEX * TEX // 2         # 512: 32 rows of 16 bytes, two texels a byte
KEY = 5                         # magenta: the sprite key, never inside art
SPR = 32                        # a sprite frame's texels a side (the guard, the
                                # decorations, and the frame a pickup sits in)
PICK = 16                       # a pickup master's side, placed at the frame's
                                # lower middle: rows PICK..31, columns 8..23
WPN_W, WPN_H = 16, 24           # a weapon master: 16 columns x 24 rows, padded
                                # to 32 texel rows for the 24-row scaler
SPRMSZ = SPR * SPR // 2 + SPR * SPR // 8      # 640: a frame in the stream
WPNMSZ = WPN_W * SPR // 2 + WPN_W * SPR // 8  # 320: a weapon frame in it
MAXRUNS = 4                     # opaque spans a column, at most (97.6)
RUNSZ = 1 + 2 * MAXRUNS         # a column's run table in part 4: count, pairs
FRSZ = SPR * SPR + SPR * RUNSZ  # 1,312: a transposed frame in part 4
WFRSZ = WPN_W * SPR + WPN_W * RUNSZ           # 656: a transposed weapon frame
# the guard's frames, in part 4's order (97.6): facing f (0..4) x walk phase
# (0, 1) at f * 2 + phase, then the two shoot frames, the pain frame, three
# die frames and the corpse
G_WALK0, G_SHOOT, G_PAIN, G_DIE, G_DEAD, NGUARD = 0, 10, 12, 13, 16, 17
DECO_NAMES = ("pillar", "barrel", "table", "bones", "puddle", "plant")  # kinds 8..13
PICKUP_NAMES = ("ammo", "medkit", "food", "goldkey", "silverkey", "treasure",
                "chalice", "life")                                       # kinds 0..7
WEAPON_NAMES = ("knife", "pistol", "mgun")
DECO0 = NGUARD                  # frame index of decoration kind 8
PICK0 = DECO0 + len(DECO_NAMES)  # ...and of pickup kind 0
# THE DOG'S frames (wave 6, 97.4): FOUR facings - 0 its front, 1 the
# three-quarter (its right side toward the viewer), 2 its right side, 3 its
# back - x walk phase at D_WALK0 + f * 2 + phase, then the bite, the fall
# and the corpse. After the pickups, so every index before them stands
D_WALK0 = PICK0 + len(PICKUP_NAMES)
D_NFACE = 4
D_BITE = D_WALK0 + 2 * D_NFACE
D_DIE = D_BITE + 1
D_DEAD = D_DIE + 1
NSPR = D_DEAD + 1               # 42 frames
# the eight facings the engine draws, from the dog's four: (master, mirrored)
# by the facing index of 97.6 (0 its front, 2 its right side, 4 its back, 6
# its left); the two quarters BEHIND take the side, which is what a dog seen
# from three-quarters behind mostly is - px_act_frame's px_dogfac table
D_FACING = ((0, 0), (1, 0), (2, 0), (2, 0), (3, 0), (2, 1), (2, 1), (1, 1))
DOG_TOP = 16                    # a dog is LOW (97.4): no opaque texel above
                                # this row on any of its frames - --check
NWPN = len(WEAPON_NAMES) * 3    # nine weapon frames
BACKENDS = ("cga4", "herc", "c160", "modex")     # WIN1 takes herc's
DARK = 0.55                     # a dark face's brightness
GAMMA = 1.5                     # cgaify's match gamma

# the sixteen colours of apps/os88api.inc (tools/pxssim.py's PALETTE)
PALETTE = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00), (0x00, 0xAA, 0xAA),
    (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA), (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA),
    (0x55, 0x55, 0x55), (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55), (0xFF, 0xFF, 0xFF)]
CGA_PAL0 = [(0, 0, 0), (0, 0xAA, 0), (0xAA, 0, 0), (0xAA, 0x55, 0)]

# cgaify.cpp's four-pixel patterns (read for technique): each is the four
# 2-bit pixels of one CGA byte, left to right. THE SOURCE LISTS 21 AND THIS
# IS 20: its USE_ALL_DITHERS table carries (0,0,0,0) twice, so twenty distinct
# patterns is the whole of the seed (the plan's graft 13.6 counts the list's
# rows, not its members; review, wave 2)
CGA_PATTERNS = [
    (0, 0, 0, 0), (0, 1, 0, 0), (0, 1, 0, 1), (1, 1, 0, 1), (1, 1, 1, 1),
    (1, 1, 3, 1), (3, 1, 3, 1), (3, 1, 3, 3), (0, 2, 0, 0), (0, 2, 0, 2),
    (2, 2, 0, 2), (2, 2, 2, 2), (2, 2, 3, 2), (3, 2, 3, 2), (3, 2, 3, 3),
    (3, 3, 3, 3), (0, 3, 0, 0), (0, 3, 0, 3), (3, 3, 0, 3), (1, 2, 1, 2)]
# ...and its nine 1bpp levels, densest first - AS --preview CULLED THEM: the
# source's 0x7F, 0x77 and 0x57 each leave a column lit on BOTH rows under the
# `ror al, cl` (3) odd-row phase (0x57 = 01010111, ror 3 = 11101010: bits 6
# and 1 stand in both), which reads as vertical stripes; the complements of
# the sparse levels (0xFE, 0xEE, 0xEA) put a hole where 0x01, 0x11, 0x15 put
# a dot, and alternate as those do
HERC_LEVELS = (0xFF, 0xFE, 0xEE, 0xEA, 0x55, 0x15, 0x11, 0x01, 0x00)


def slug(name):
    return "".join(ch if ch.isalnum() else "_" for ch in name.lower())


def master_path(mat):
    return os.path.join(ART_DIR, "w%02d_%s.png" % (mat, slug(pxslevel.MAT_NAMES[mat])))


# --- PNG, stdlib ------------------------------------------------------------------

def _chunks(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    i = 8
    while i < len(data):
        n, = struct.unpack(">I", data[i:i + 4])
        tag = data[i + 4:i + 8]
        yield tag, data[i + 8:i + 8 + n]
        i += 12 + n


def _unfilter(raw, w, h, bpp, stride):
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for _ in range(h):
        f = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
            elif f != 0:
                raise ValueError("PNG filter %d" % f)
        out += line
        prev = line
    return out


def read_png(path):
    """(w, h, pixels) - pixels a list of rows of (r, g, b, a)."""
    data = open(path, "rb").read()
    w = h = depth = ctype = None
    plte, trns, idat = None, None, b""
    for tag, body in _chunks(data):
        if tag == b"IHDR":
            w, h, depth, ctype, _, _, il = struct.unpack(">IIBBBBB", body)
            if il:
                raise ValueError("%s: interlaced PNGs are not read here" % path)
        elif tag == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif tag == b"tRNS":
            trns = body
        elif tag == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    chan = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bits = chan * depth
    stride = (w * bits + 7) // 8
    bpp = max(1, bits // 8)
    px = _unfilter(raw, w, h, bpp, stride)
    rows = []
    for y in range(h):
        line = px[y * stride:(y + 1) * stride]
        row = []
        for x in range(w):
            if ctype == 3:
                if depth == 8:
                    i = line[x]
                else:
                    per = 8 // depth
                    i = (line[x // per] >> (8 - depth * (x % per + 1))) & ((1 << depth) - 1)
                r, g, b = plte[i]
                a = trns[i] if trns is not None and i < len(trns) else 255
            elif ctype == 2:
                if depth != 8:
                    raise ValueError("%s: 16-bit RGB is not read here" % path)
                r, g, b = line[x * 3:x * 3 + 3]
                # tRNS on an RGB image names ONE colour as transparent (three
                # 16-bit samples): the alpha the opacity guard reads - an
                # image model's transparent background came through here as
                # opaque wall art before (review, wave 2)
                a = 255
                if trns is not None and len(trns) >= 6:
                    if (r, g, b) == tuple(v & 255 for v in struct.unpack(">HHH", trns[:6])):
                        a = 0
            elif ctype == 6:
                if depth != 8:
                    raise ValueError("%s: 16-bit RGBA is not read here" % path)
                r, g, b, a = line[x * 4:x * 4 + 4]
            elif ctype == 0:
                if depth != 8:
                    raise ValueError("%s: %d-bit grey is not read here" % (path, depth))
                r = g = b = line[x]
                a = 255
                if trns is not None and len(trns) >= 2:
                    if line[x] == (struct.unpack(">H", trns[:2])[0] & 255):
                        a = 0
            else:
                raise ValueError("%s: colour type %d is not read here" % (path, ctype))
            row.append((r, g, b, a))
        rows.append(row)
    return w, h, rows


def _chunk(tag, body):
    c = tag + body
    return struct.pack(">I", len(body)) + c + struct.pack(">I", zlib.crc32(c))


def write_png_indexed(path, w, h, idx):
    """An 8-bit palette PNG of the sixteen colours: idx is h rows of w indices."""
    raw = b"".join(b"\x00" + bytes(row) for row in idx)
    plte = b"".join(bytes(c) for c in PALETTE)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)))
        f.write(_chunk(b"PLTE", plte))
        f.write(_chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(_chunk(b"IEND", b""))


def write_png_rgb(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(v for px in row for v in px) for row in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(_chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(_chunk(b"IEND", b""))


# --- the masters ----------------------------------------------------------------

def nearest_index(rgb, tol=None):
    best, bd = 0, None
    for i, c in enumerate(PALETTE):
        d = sum((a - b) ** 2 for a, b in zip(rgb, c))
        if bd is None or d < bd:
            best, bd = i, d
    if tol is not None and bd > tol * tol * 3:
        return None
    return best


def load_master(mat, strict=True):
    """One master as 32 rows of 32 indices, or raises with the reason."""
    path = master_path(mat)
    w, h, rows = read_png(path)
    if (w, h) != (TEX, TEX):
        raise ValueError("%s: %dx%d, a wall master is %dx%d" % (path, w, h, TEX, TEX))
    out = []
    for y, row in enumerate(rows):
        line = []
        for x, (r, g, b, a) in enumerate(row):
            if a != 255:
                raise ValueError("%s: pixel (%d,%d) has alpha %d - a wall is opaque"
                                 % (path, x, y, a))
            i = nearest_index((r, g, b), tol=8 if strict else None)
            if i is None:
                raise ValueError("%s: pixel (%d,%d) is (%d,%d,%d), not one of the "
                                 "sixteen colours - snap it first" % (path, x, y, r, g, b))
            if i == KEY:
                raise ValueError("%s: pixel (%d,%d) is index %d, THE KEY - an image "
                                 "model puts near-magenta inside a master, and the key "
                                 "comes from alpha, never from a colour" % (path, x, y, KEY))
            line.append(i)
        out.append(line)
    return out


def masters(strict=True):
    """All fifteen, material 1 first."""
    return [load_master(m, strict) for m in range(1, NWALL + 1)]


def pack_master(idx):
    """32 rows x 16 bytes: texel (2p) in the high nibble, (2p + 1) low."""
    out = bytearray()
    for row in idx:
        for p in range(TEX // 2):
            out.append((row[2 * p] << 4) | row[2 * p + 1])
    return bytes(out)


def art_blob(ms):
    return b"".join(pack_master(m) for m in ms)


# --- the ink tables --------------------------------------------------------------

def lin(v):
    return (v / 255.0) ** GAMMA


def luma(rgb):
    r, g, b = rgb
    return 0.299 * r + 0.587 * g + 0.114 * b


def _cga_pattern(rgb, k):
    """The nearest of the 21 patterns in linear light - and NEVER the all-
    black one for a colour that is not black: palette 0 has no blue, so
    blue stone matched (0,0,0,0) and a whole wall of it vanished into the
    ceiling on the first CGA screendump. A dark colour becomes a sparse
    pattern, which is what a dark wall looks like."""
    want = [lin(v) * k for v in rgb]
    best, bd = None, None
    for pat in CGA_PATTERNS:
        if pat == (0, 0, 0, 0) and any(rgb):
            continue
        mean = [sum(lin(CGA_PAL0[p][ch]) for p in pat) / 4.0 for ch in range(3)]
        d = sum((a - b) ** 2 for a, b in zip(mean, want))
        if bd is None or d < bd:
            best, bd = pat, d
    return best


def cga4_byte(pat):
    return (pat[0] << 6) | (pat[1] << 4) | (pat[2] << 2) | pat[3]


def _herc_level(rgb, k):
    y = luma(rgb) / 255.0 * k
    best, bd = 0, None
    for lv in HERC_LEVELS:
        if lv == 0 and any(rgb):
            continue                    # the same rule: not black unless black
        d = abs(bin(lv).count("1") / 8.0 - y)
        if bd is None or d < bd:
            best, bd = lv, d
    return best


def _darker_index(i):
    """The nearest DIFFERENT colour of the sixteen to this one at 55%, darker
    than it and NEVER BLACK: C160's dark twin. Blue (1) has no darker colour
    but black and dark grey (8) only blue, so a colour with nothing darker
    takes the nearest non-black colour there is - blue's shadow is dark
    grey, dark grey's is blue (which is what the Flat rung's own C160 tone
    table already does for the steel and the cell door, pxrast.inc)."""
    if i == 0:
        return 0                    # black is black in shadow too
    rgb = PALETTE[i]
    want = tuple(v * DARK for v in rgb)
    best, bd = None, None
    for darker in (True, False):
        for j, c in enumerate(PALETTE):
            if j == i or j == KEY or j == 0 or (darker and luma(c) >= luma(rgb)):
                continue
            d = sum((a - b) ** 2 for a, b in zip(want, c))
            if bd is None or d < bd:
                best, bd = j, d
        if best is not None:
            return best
    return i


def _cga_luma(pat):
    return sum(luma(CGA_PAL0[p]) for p in pat)


def _cga_darker(pat):
    """The pattern one step DOWN in luminance from `pat`, never the all-black
    one: what a dark shade steps to when its match is the lit one. None when
    `pat` is already the sparsest lit pattern (the lit then steps UP)."""
    y = _cga_luma(pat)
    below = [p for p in CGA_PATTERNS if 0 < _cga_luma(p) < y]
    return max(below, key=_cga_luma) if below else None


def _cga_lighter(pat):
    y = _cga_luma(pat)
    above = [p for p in CGA_PATTERNS if _cga_luma(p) > y]
    return min(above, key=_cga_luma)


def _herc_darker(lv):
    """The next sparser level of the ladder (densest first), never 0x00;
    None at the sparsest lit level (the lit then steps UP)."""
    i = HERC_LEVELS.index(lv)
    nxt = HERC_LEVELS[i + 1] if i + 1 < len(HERC_LEVELS) else 0
    return nxt or None


def _herc_lighter(lv):
    i = HERC_LEVELS.index(lv)
    return HERC_LEVELS[max(i - 1, 0)]


def ink_tables():
    """{backend: (lit[16], dark[16])} - the texel byte (or nibble) of each of
    the sixteen indices, even-row phase.

    THREE RULES the dither and colour backends' tables obey (97.4): a colour
    that is not black never LIGHTS as all-black (blue stone vanished into
    the ceiling on the first CGA screendump); a colour's DARK shade is never
    its lit one - the 55% match steps one pattern or level DOWN when it
    lands on the same entry, so a corner between two faces of it carries a
    shading cue (blue read 0x10 / 0x10 on CGA4 and 0x01 / 0x01 on Hercules
    without it); and THE DARK SHADE IS NEVER ALL-BLACK EITHER - the first
    cut of the second rule stepped blue down to 0x00 on Hercules and blue,
    green, red and dark grey to black on C160, and w03_blue_stone is 79%
    index 1 over black mortar, so its dark face was a 97% black silhouette
    on two backends of five (review, wave 2). Where the lit match is already
    the sparsest lit pattern or level, the LIT steps up one instead and the
    dark keeps the floor: blue lights 0x11 and darkens to 0x01 on Hercules.
    On C160 the dark twin is the nearest darker non-black colour, or the
    nearest non-black colour when there is none darker (_darker_index)."""
    cga_lit = [_cga_pattern(c, 1.0) for c in PALETTE]
    cga_dark = [_cga_pattern(c, DARK) for c in PALETTE]
    herc_lit = [_herc_level(c, 1.0) for c in PALETTE]
    herc_dark = [_herc_level(c, DARK) for c in PALETTE]
    for i in range(1, 16):
        if cga_dark[i] == cga_lit[i]:
            down = _cga_darker(cga_lit[i])
            if down is None:
                cga_lit[i] = _cga_lighter(cga_lit[i])
            else:
                cga_dark[i] = down
        if herc_dark[i] == herc_lit[i]:
            down = _herc_darker(herc_lit[i])
            if down is None:
                herc_lit[i] = _herc_lighter(herc_lit[i])
            else:
                herc_dark[i] = down
    cga = ([cga4_byte(p) for p in cga_lit], [cga4_byte(p) for p in cga_dark])
    herc = (herc_lit, herc_dark)
    c160 = (list(range(16)), [_darker_index(i) for i in range(16)])
    modex = (list(range(16)), [16 + i for i in range(16)])
    return {"cga4": cga, "herc": herc, "c160": c160, "modex": modex}


def win4_tables():
    """WIN4's two 32 -> 16 tables (97.14), (even rows, odd rows): Mode X's
    byte i to the packed-4bpp byte of its colour in both nibbles - the lit
    sixteen themselves on every row, the dark face (16 + i) its LIT colour
    on the even rows and BLACK on the odd. A line dither, not a checker:
    the planar present makes each plane byte whole from ONE bit of the
    entry (`cbw`, 97.14), so a pattern inside a byte would cost the inner
    loop a mask a byte; a pattern ACROSS rows costs a table swap a row"""
    even = [i * 0x11 for i in range(16)] * 2
    odd = [i * 0x11 for i in range(16)] + [0] * 16
    return even, odd


RULED = ("cga4", "herc", "c160")    # the backends the three rules bind (Mode X
                                    # is a DAC: 16 + i is dark by construction)


def ink_rules():
    """(ok, lines): the three rules above, checked on the tables as built -
    what --check and tests/unit/t_pxsart.py assert."""
    ok, lines = True, []
    for be in RULED:
        lit, dark = ink_tables()[be]
        black = [i for i in range(1, 16) if lit[i] == 0]
        dblack = [i for i in range(1, 16) if dark[i] == 0]
        same = [i for i in range(1, 16) if dark[i] == lit[i]]
        good = not black and not dblack and not same
        ok = ok and good
        lines.append("%s: no non-black colour lights all-black (%s), no dark shade is "
                     "all-black (%s), no dark shade is its lit one (%s): %s"
                     % (be, "none" if not black else "indices %s" % black,
                        "none" if not dblack else "indices %s" % dblack,
                        "none" if not same else "indices %s" % same,
                        "ok" if good else "BROKEN"))
    return ok, lines


# --- the byte-texture set (px_bt_build's layout, 97.4) ----------------------------

SHADES = 2
MATSZ = SHADES * TEX * TEX      # 2,048 bytes a material
BT_SIZE = NWALL * MATSZ         # 30,720


def bt_set(ms, backend):
    """The 30,720-byte set: [mat-1][shade][column][v]. On C160 a column is a
    PAIR of texels (u >> 4, sixteen columns used of thirty-two)."""
    it = ink_tables()[backend if backend != "win1" else "herc"]
    out = bytearray(BT_SIZE)
    for mi, m in enumerate(ms):
        for s in range(SHADES):
            tab = it[s]
            base = mi * MATSZ + s * TEX * TEX
            for v in range(TEX):
                row = m[v]
                if backend == "c160":
                    for p in range(TEX // 2):
                        out[base + p * TEX + v] = (tab[row[2 * p]] << 4) | tab[row[2 * p + 1]]
                else:
                    for u in range(TEX):
                        out[base + u * TEX + v] = tab[row[u]]
    return bytes(out)


def phase(backend, byte, row):
    """The texel byte as the scaler stores it on `row` (97.3): even rows the
    byte itself, odd rows rotated as the pixel format wants."""
    if row & 1 == 0:
        return byte
    if backend == "cga4":
        return ((byte >> 2) | (byte << 6)) & 255            # ror al, 1 x 2
    if backend in ("herc", "win1"):
        return ((byte >> 3) | (byte << 5)) & 255            # ror al, cl (3)
    return byte


def texel(bt, backend, mat, side, u, v, row):
    """What the wall byte at texture fraction u (0..255), texel row v, on view
    row `row` is, off a bt_set."""
    col = (u >> 4) if backend == "c160" else (u >> 3)
    return phase(backend, bt[(mat - 1) * MATSZ + side * TEX * TEX + col * TEX + v], row)


# --- the losable criterion (97.4) ---------------------------------------------------

def distinct(ms, backend, a=6, b=1):
    """Brick (6) against grey stone (1) on this backend's lit set: the share
    of the 1,024 texel bytes that differ and the mean-density gap. (The
    'distinguishable at 64 columns at 1:1' of the plan: one byte a column,
    every texel, the render the machine makes.)"""
    bt = bt_set(ms, backend)
    sa = bt[(a - 1) * MATSZ:(a - 1) * MATSZ + TEX * TEX]
    sb = bt[(b - 1) * MATSZ:(b - 1) * MATSZ + TEX * TEX]
    diff = sum(1 for x, y in zip(sa, sb) if x != y) / float(TEX * TEX)
    if backend in ("cga4",):
        dens = lambda s: sum(bin(x).count("1") for x in s) / float(len(s) * 8)   # noqa: E731
    elif backend in ("herc", "win1"):
        dens = lambda s: sum(bin(x).count("1") for x in s) / float(len(s) * 8)   # noqa: E731
    else:
        dens = lambda s: sum(x & 15 for x in s) / float(len(s) * 15)             # noqa: E731
    return diff, abs(dens(sa) - dens(sb))


DIFF_MIN, DENS_MIN = 0.20, 0.10


def criterion(ms):
    """(ok, lines): brick vs grey stone on CGA4 and Hercules."""
    ok, lines = True, []
    for be in ("cga4", "herc"):
        diff, dens = distinct(ms, be)
        good = diff >= DIFF_MIN or dens >= DENS_MIN
        ok = ok and good
        lines.append("%s: brick vs grey stone - %.0f%% of texel bytes differ, mean "
                     "density gap %.2f (want >= %.0f%% or >= %.2f): %s"
                     % (be, diff * 100, dens, DIFF_MIN * 100, DENS_MIN,
                        "distinguishable" if good else "NOT DISTINGUISHABLE"))
    return ok, lines


# --- the placeholders: a procedural castle, deterministic --------------------------

class _Rng:
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def next(self, n):
        self.s = (self.s * 1103515245 + 12345) & 0x7FFFFFFF
        return (self.s >> 8) % n


def _blank(v):
    return [[v] * TEX for _ in range(TEX)]


def _rect(im, x0, y0, x1, y1, v):
    for y in range(max(0, y0), min(TEX, y1)):
        for x in range(max(0, x0), min(TEX, x1)):
            im[y][x] = v


def _speckle(im, rng, v, n, only=None):
    for _ in range(n):
        x, y = rng.next(TEX), rng.next(TEX)
        if only is None or im[y][x] == only:
            im[y][x] = v


def _blocks(im, rng, face, mortar, bh, bw, shift, speck=None, speckn=40):
    _rect(im, 0, 0, TEX, TEX, face)
    y = 0
    course = 0
    while y < TEX:
        _rect(im, 0, y, TEX, y + 1, mortar)
        x = (course * shift) % bw
        x -= bw
        while x < TEX:
            _rect(im, x, y, x + 1, y + bh, mortar)
            x += bw
        y += bh
        course += 1
    if speck is not None:
        _speckle(im, rng, speck, speckn, only=face)


def placeholder(mat):
    rng = _Rng(1000 + mat * 7919)
    im = _blank(7)
    if mat == 1:                                  # grey stone
        _blocks(im, rng, 7, 8, 8, 16, 8, speck=8, speckn=36)
    elif mat == 2:                                # grey stone, the second cut
        _blocks(im, rng, 7, 8, 6, 11, 5, speck=15, speckn=24)
    elif mat == 3:                                # blue stone
        _blocks(im, rng, 1, 0, 8, 16, 8, speck=9, speckn=40)
    elif mat == 4:                                # wood: vertical planks
        _rect(im, 0, 0, TEX, TEX, 6)
        for x in range(0, TEX, 8):
            _rect(im, x, 0, x + 1, TEX, 0)
            _rect(im, x + 4, 0, x + 5, TEX, 8)
        for y in (5, 13, 21, 29):
            for x in range(0, TEX, 8):
                im[y][x + 2] = 8
    elif mat == 5:                                # wood 2: horizontal planks
        _rect(im, 0, 0, TEX, TEX, 6)
        for y in range(0, TEX, 8):
            _rect(im, 0, y, TEX, y + 1, 0)
            _rect(im, 0, y + 4, TEX, y + 5, 8)
        _speckle(im, rng, 8, 30, only=6)
    elif mat == 6:                                # brick
        _blocks(im, rng, 4, 7, 4, 8, 4, speck=12, speckn=40)
    elif mat == 7:                                # banner over stone
        _blocks(im, rng, 7, 8, 8, 16, 8)
        _rect(im, 8, 2, 24, 30, 1)
        _rect(im, 8, 2, 24, 3, 14)
        _rect(im, 12, 10, 20, 22, 14)
        _rect(im, 14, 12, 18, 20, 1)
        _rect(im, 8, 28, 24, 30, 14)
    elif mat == 8:                                # portrait over wood
        _rect(im, 0, 0, TEX, TEX, 6)
        for x in range(0, TEX, 8):
            _rect(im, x, 0, x + 1, TEX, 0)
        _rect(im, 6, 4, 26, 28, 14)
        _rect(im, 8, 6, 24, 26, 8)
        _rect(im, 12, 9, 20, 19, 12)
        _rect(im, 14, 12, 15, 14, 0)
        _rect(im, 17, 12, 18, 14, 0)
        _rect(im, 14, 16, 18, 17, 0)
        _rect(im, 10, 19, 22, 26, 4)
    elif mat == 9:                                # emblem over stone
        _blocks(im, rng, 7, 8, 8, 16, 8)
        for y in range(6, 28):
            hw = 9 if y < 18 else 9 - (y - 18)
            _rect(im, 16 - hw, y, 16 + hw, y + 1, 4)
        _rect(im, 15, 8, 17, 24, 15)
        _rect(im, 9, 12, 23, 14, 15)
    elif mat == 10:                               # cell door: wood with bars
        _rect(im, 0, 0, TEX, TEX, 6)
        _rect(im, 4, 0, 28, TEX, 0)
        for x in (7, 12, 17, 22):
            _rect(im, x, 0, x + 2, TEX, 7)
        _rect(im, 4, 14, 28, 16, 8)
    elif mat == 11:                               # cell bars
        _rect(im, 0, 0, TEX, TEX, 0)
        for x in range(2, TEX, 6):
            _rect(im, x, 0, x + 2, TEX, 7)
            _rect(im, x + 1, 0, x + 2, TEX, 8)
        _rect(im, 0, 3, TEX, 5, 8)
        _rect(im, 0, 27, TEX, 29, 8)
    elif mat == 12:                               # steel door
        _rect(im, 0, 0, TEX, TEX, 8)
        _rect(im, 2, 2, 30, 30, 7)
        _rect(im, 15, 2, 17, 30, 8)
        for y in (4, 12, 20, 27):
            for x in (4, 11, 20, 27):
                im[y][x] = 15
        _rect(im, 19, 15, 23, 17, 0)
    elif mat in (13, 14):                         # the elevator switch, used / live
        _rect(im, 0, 0, TEX, TEX, 8)
        _rect(im, 2, 2, 30, 30, 7)
        _rect(im, 10, 6, 22, 26, 0)
        lever = 2 if mat == 13 else 4
        _rect(im, 14, 8 if mat == 13 else 16, 18, 24 if mat == 13 else 24, lever)
        _rect(im, 12, 22, 20, 24, 15)
        for y in (4, 27):
            for x in (4, 27):
                im[y][x] = 15
    else:                                         # 15: the jamb
        _rect(im, 0, 0, TEX, TEX, 8)
        _rect(im, 12, 0, 20, TEX, 7)
        _rect(im, 14, 0, 18, TEX, 15)
        for y in range(2, TEX, 6):
            im[y][15] = 8
            im[y][16] = 8
    return im


def write_placeholders(force=False):
    os.makedirs(ART_DIR, exist_ok=True)
    n = 0
    for mat in range(1, NWALL + 1):
        p = master_path(mat)
        if os.path.exists(p) and not force:
            continue
        write_png_indexed(p, TEX, TEX, placeholder(mat))
        n += 1
    return n


# --- the preview ---------------------------------------------------------------------

def _rgb_of(backend, byte, row):
    """One texel byte's device pixels as RGB triples."""
    b = phase(backend, byte, row)
    if backend == "cga4":
        return [CGA_PAL0[(b >> k) & 3] for k in (6, 4, 2, 0)]
    if backend in ("herc", "win1"):
        return [(0xFF, 0xB0, 0x00) if (b >> k) & 1 else (0, 0, 0) for k in range(7, -1, -1)]
    if backend == "modex":
        c = PALETTE[b & 15] if b < 16 else tuple(v * 6 // 10 for v in PALETTE[b & 15])
        return [c] * 4
    return [PALETTE[b >> 4], PALETTE[b & 15]]


ASPECT = {"cga4": (1, 1), "herc": (1, 2), "modex": (1, 1), "c160": (2, 1)}


def preview(ms, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    written = []
    for be in BACKENDS:
        bt = bt_set(ms, be)
        zx, zy = ASPECT[be]
        for mi in range(NWALL):
            rows = []
            for side in range(SHADES):
                for v in range(TEX):
                    line = []
                    ncol = TEX // 2 if be == "c160" else TEX
                    for u in range(ncol):
                        line += _rgb_of(be, bt[mi * MATSZ + side * TEX * TEX + u * TEX + v], v)
                    rows.append(line)
            w = len(rows[0])
            big = []
            for r in rows:
                line = []
                for px in r:
                    line += [px] * zx
                for _ in range(zy):
                    big.append(line)
            p = os.path.join(out_dir, "%s-w%02d.png" % (be, mi + 1))
            write_png_rgb(p, w * zx, len(big), big)
            written.append(p)
    return written


# --- the sprites (wave 3, 97.4, 97.6) -----------------------------------------------

def sprite_names():
    """The 42 sprite frames' file stems, in part 4's frame order."""
    out = []
    for f in range(5):
        for ph in range(2):
            out.append("g_f%d_w%d" % (f, ph))
    out += ["g_shoot0", "g_shoot1", "g_pain", "g_die0", "g_die1", "g_die2", "g_dead"]
    out += ["d_" + n for n in DECO_NAMES]
    out += ["p_" + n for n in PICKUP_NAMES]
    for f in range(D_NFACE):
        for ph in range(2):
            out.append("dog_f%d_w%d" % (f, ph))
    out += ["dog_bite", "dog_die", "dog_dead"]
    assert len(out) == NSPR
    return out


def weapon_names():
    return ["wp_%s_%d" % (w, k) for w in WEAPON_NAMES for k in range(3)]


def sprite_path(stem):
    return os.path.join(ART_DIR, stem + ".png")


def load_sprite(stem, w, h):
    """One RGBA master as (rows of indices, rows of alpha 0/1), w x h, or
    raises with the reason: the size, an alpha that is not 0 or 255, a
    colour off the sixteen, the key inside an opaque pixel."""
    path = sprite_path(stem)
    pw, ph, rows = read_png(path)
    if (pw, ph) != (w, h):
        raise ValueError("%s: %dx%d, this master is %dx%d" % (path, pw, ph, w, h))
    idx, alpha = [], []
    for y, row in enumerate(rows):
        li, la = [], []
        for x, (r, g, b, a) in enumerate(row):
            if a not in (0, 255):
                raise ValueError("%s: pixel (%d,%d) has alpha %d - a sprite's alpha is 0 "
                                 "or 255, THE KEY, and nothing between" % (path, x, y, a))
            if a == 0:
                li.append(0)
                la.append(0)
                continue
            i = nearest_index((r, g, b), tol=8)
            if i is None:
                raise ValueError("%s: pixel (%d,%d) is (%d,%d,%d), not one of the sixteen "
                                 "colours - snap it first" % (path, x, y, r, g, b))
            if i == KEY:
                raise ValueError("%s: pixel (%d,%d) is index %d, THE KEY, inside an opaque "
                                 "pixel - the key is alpha, never a colour" % (path, x, y, KEY))
            li.append(i)
            la.append(1)
        idx.append(li)
        alpha.append(la)
    return idx, alpha


def place_pickup(idx, alpha):
    """A 16 x 16 pickup into the lower middle of a 32 x 32 frame."""
    fi = [[0] * SPR for _ in range(SPR)]
    fa = [[0] * SPR for _ in range(SPR)]
    x0, y0 = (SPR - PICK) // 2, SPR - PICK
    for y in range(PICK):
        for x in range(PICK):
            fi[y0 + y][x0 + x] = idx[y][x]
            fa[y0 + y][x0 + x] = alpha[y][x]
    return fi, fa


def pad_weapon(idx, alpha):
    """A 16 x 24 weapon frame into 16 x 32 texel rows so that the 24-row
    scaler - texel v covers rows [(v*24)>>5, ((v+1)*24)>>5) - draws the 24
    authored rows exactly: texel 4k covers NO row and texels 4k+1, 4k+2,
    4k+3 land on rows 3k, 3k+1, 3k+2, so texels 4k and 4k+1 both carry
    row 3k (the first cut's docstring had the two the other way round;
    the code was right - review, wave 3)."""
    fi, fa = [], []
    for k in range(8):
        r0, r1, r2 = idx[3 * k], idx[3 * k + 1], idx[3 * k + 2]
        a0, a1, a2 = alpha[3 * k], alpha[3 * k + 1], alpha[3 * k + 2]
        fi += [r0, r0, r1, r2]
        fa += [a0, a0, a1, a2]
    assert all(((v * WPN_H) >> 5) == r for v, r in
               ((4 * k + j, 3 * k + (0, 0, 1, 2)[j]) for k in range(8) for j in range(4)))
    return fi, fa


def pack_sprite(idx, alpha, w=SPR):
    """w x 32 texels: the packed nibbles (row major, even column high), then
    the alpha bits (row major, bit 7 = column 0)."""
    out = bytearray()
    for row in idx:
        for p in range(w // 2):
            out.append((row[2 * p] << 4) | row[2 * p + 1])
    for row in alpha:
        for p in range(w // 8):
            b = 0
            for k in range(8):
                b = (b << 1) | (1 if row[8 * p + k] else 0)
            out.append(b)
    return bytes(out)


def column_runs(col):
    """The opaque spans of one column of alpha bits: [(v0, v1)] with v1
    exclusive."""
    runs, v = [], 0
    n = len(col)
    while v < n:
        if not col[v]:
            v += 1
            continue
        v0 = v
        while v < n and col[v]:
            v += 1
        runs.append((v0, v))
    return runs


def frame_runs(alpha, w=SPR, pairs=False):
    """Per column (per column PAIR on C160) the run list, checked against
    MAXRUNS; raises naming the column."""
    out = []
    ncol = w // 2 if pairs else w
    for c in range(ncol):
        if pairs:
            col = [alpha[v][2 * c] | alpha[v][2 * c + 1] for v in range(SPR)]
        else:
            col = [alpha[v][c] for v in range(SPR)]
        r = column_runs(col)
        if len(r) > MAXRUNS:
            raise ValueError("column %d has %d opaque spans; the post walk draws %d at most "
                             "(97.6) - simplify the art" % (c, len(r), MAXRUNS))
        out.append(r)
    return out


def sprites(strict=True):
    """All 42 frames as (idx, alpha) 32 x 32, in part 4's order."""
    out = []
    for stem in sprite_names():
        if stem.startswith("p_"):
            idx, alpha = load_sprite(stem, PICK, PICK)
            out.append(place_pickup(idx, alpha))
        else:
            out.append(load_sprite(stem, SPR, SPR))
    return out


def weapons():
    """The nine weapon frames as (idx, alpha) 16 x 32."""
    return [pad_weapon(*load_sprite(stem, WPN_W, WPN_H)) for stem in weapon_names()]


def sprite_blob(sp, wp):
    return b"".join(pack_sprite(i, a) for i, a in sp) + \
        b"".join(pack_sprite(i, a, WPN_W) for i, a in wp)


def spr_frame(idx, alpha, backend, w=SPR):
    """One frame as px_spr_build lays it in part 4: w columns of 32 texel
    bytes through the LIT ink table (C160: column pairs, both nibbles), then
    w run tables of RUNSZ bytes (count, then (v0, v1) pairs, v1 exclusive;
    C160's are the pairs')."""
    it = ink_tables()[backend if backend != "win1" else "herc"][0]
    pairs = backend == "c160"
    out = bytearray(w * SPR)
    if pairs:
        for c in range(w // 2):
            for v in range(SPR):
                out[c * SPR + v] = (it[idx[v][2 * c]] << 4) | it[idx[v][2 * c + 1]]
    else:
        for c in range(w):
            for v in range(SPR):
                out[c * SPR + v] = it[idx[v][c]]
    runs = frame_runs(alpha, w, pairs)
    tab = bytearray(w * RUNSZ)
    for c, r in enumerate(runs):
        tab[c * RUNSZ] = len(r)
        for k, (v0, v1) in enumerate(r):
            tab[c * RUNSZ + 1 + 2 * k] = v0
            tab[c * RUNSZ + 2 + 2 * k] = v1
    return bytes(out + tab)


def spr_set(sp, wp, backend):
    """The whole of part 4 for a backend: NSPR frames then NWPN weapon frames."""
    out = b"".join(spr_frame(i, a, backend) for i, a in sp)
    out += b"".join(spr_frame(i, a, backend, WPN_W) for i, a in wp)
    assert len(out) == NSPR * FRSZ + NWPN * WFRSZ
    return out


def spr_at12(frame, backend, w=12):
    """A frame drawn twelve columns wide the way the post walk draws it:
    column j takes source column (j * 32) // w (tools/pxsgen.py's col2tex),
    every texel row - the bytes a guard two tiles off puts on the glass."""
    fr = spr_frame(frame[0], frame[1], backend)
    cols = []
    for j in range(w):
        src = (j * SPR) // w
        if backend == "c160":
            src >>= 1
        cols.append(fr[src * SPR:(src + 1) * SPR])
    return cols


# THE FACING PAIRS THE CRITERION HOLDS APART (97.6): the front against the
# side is what a four-facing set has too; the odd facings f1 (the
# three-quarter view) and f3 (the side from behind) are what an EIGHT-facing
# set earns, so each is held against both of its neighbours - the pair that
# fails names the master to redraw. Every pair at twelve columns, the width
# a guard has at two tiles (review, wave 3: the first cut compared f0 and
# f2 alone, and "8 vs 4 facings" was decided by nothing)
SPR_PAIRS = ((0, 2, "front vs its side"), (1, 0, "three-quarter vs its front"),
             (1, 2, "three-quarter vs its side"), (3, 2, "side-from-behind vs its side"),
             (3, 4, "side-from-behind vs its back"))


# ...and THE DOG's, on its four (wave 6): the front against its side and
# the three-quarter against both - its back is the side's quarters' stand-in
# (D_FACING), so it has only to differ from the side
DOG_PAIRS = ((0, 2, "front vs its side"), (1, 0, "three-quarter vs its front"),
             (1, 2, "three-quarter vs its side"), (3, 2, "back vs its side"))

OUTLINE_MAX = 0.25              # a sprite's outline, lit bits over its bytes


def spr_outline(idx, alpha, w=SPR):
    """(x, y) of every OPAQUE texel with a transparent 4-neighbour inside
    the frame - the figure's edge against the key. The frame's own border
    is not an edge (a weapon's grip runs off the view's bottom, a body's
    feet stand on the floor)."""
    h = len(idx)
    out = []
    for y in range(h):
        for x in range(w):
            if not alpha[y][x]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h and not alpha[ny][nx]:
                    out.append((x, y))
                    break
    return out


def outline_density(idx, alpha, backend, w=SPR):
    """The share of lit bits over the outline's texel bytes through this
    backend's lit ink table: 0 is a black line round the figure."""
    it = ink_tables()[backend][0]
    pts = spr_outline(idx, alpha, w)
    if not pts:
        return 0.0
    return sum(bin(it[idx[y][x]]).count("1") for x, y in pts) / float(len(pts) * 8)


def opaque_density(idx, alpha, backend, w=SPR):
    it = ink_tables()[backend][0]
    op = [it[idx[y][x]] for y in range(len(idx)) for x in range(w) if alpha[y][x]]
    if not op:
        return 0.0
    return sum(bin(b).count("1") for b in op) / float(len(op) * 8)


def spr_criterion(sp, wp=None, ms=None):
    """(ok, lines): the guard's facings (w0) against each other at twelve
    columns on CGA4 and Hercules, every pair of SPR_PAIRS - distinguishable
    when >= 20% of the texel bytes differ, the opaque ones counted against
    each other and a transparent texel against an opaque one counting as a
    difference. And FIGURE AGAINST GROUND on the two 1bpp sets (review r1:
    the placeholder guard's body dithered to the brick's own density on
    Hercules and only its head read, and the pistol not at all): every
    sprite and weapon frame's OUTLINE - its opaque texels bordering the key
    - renders at most OUTLINE_MAX lit through the backend's ink table, a
    dark line round the figure whatever wall is behind it; the body's
    density against the walls' mean is reported beside it."""
    ok, lines = True, []
    pairs = [(G_WALK0, fa_, fb_, "guard", what) for fa_, fb_, what in SPR_PAIRS]
    pairs += [(D_WALK0, fa_, fb_, "dog", what) for fa_, fb_, what in DOG_PAIRS]
    for w0, fa_, fb_, who, what in pairs:
        front, side = sp[w0 + 2 * fa_], sp[w0 + 2 * fb_]
        for be in ("cga4", "herc"):
            a, b = spr_at12(front, be), spr_at12(side, be)
            diff = tot = 0
            for j in range(12):
                srcf = (j * SPR) // 12
                for v in range(SPR):
                    oa = front[1][v][srcf]
                    ob = side[1][v][srcf]
                    if not oa and not ob:
                        continue
                    tot += 1
                    if oa != ob or a[j][v] != b[j][v]:
                        diff += 1
            share = diff / float(tot) if tot else 0.0
            good = share >= DIFF_MIN
            ok = ok and good
            lines.append("%s: the %s's %s at 12 columns - %.0f%% of the "
                         "texels differ (want >= %.0f%%): %s"
                         % (be, who, what, share * 100, DIFF_MIN * 100,
                            "distinguishable" if good else "NOT DISTINGUISHABLE"))
    names, wnames = sprite_names(), weapon_names()
    high = [names[i] for i in range(D_WALK0, D_DEAD + 1)
            if any(sp[i][1][y][x] for y in range(DOG_TOP) for x in range(SPR))]
    ok = ok and not high
    lines.append("the dog is LOW - every frame in rows %d..31 (97.4): %s"
                 % (DOG_TOP, "yes" if not high else "NO - " + ", ".join(high)))
    for be in ("cga4", "herc"):
        bad = []
        for i, (idx, alpha) in enumerate(sp):
            d = outline_density(idx, alpha, be)
            if d > OUTLINE_MAX:
                bad.append("%s %.2f" % (names[i], d))
        for i, (idx, alpha) in enumerate(wp or ()):
            d = outline_density(idx, alpha, be, WPN_W)
            if d > OUTLINE_MAX:
                bad.append("%s %.2f" % (wnames[i], d))
        good = not bad
        ok = ok and good
        lines.append("%s: every sprite's OUTLINE renders dark (<= %.2f lit): %s"
                     % (be, OUTLINE_MAX, "yes" if good else "NO - " + ", ".join(bad)))
        if ms is not None:
            bt = bt_set(ms, be)
            walls = [sum(bin(x).count("1") for x in bt[k * MATSZ:k * MATSZ + TEX * TEX])
                     / float(TEX * TEX * 8) for k in range(NWALL)]
            lines.append("%s: figure against ground - the guard's front %.2f lit, the "
                         "pistol %.2f, the walls' mean %.2f (min %.2f, max %.2f); the "
                         "outline is what holds them apart"
                         % (be, opaque_density(sp[G_WALK0][0], sp[G_WALK0][1], be),
                            opaque_density(wp[3][0], wp[3][1], be, WPN_W) if wp else 0,
                            sum(walls) / len(walls), min(walls), max(walls)))
    return ok, lines


def _outline_dark(fr):
    """The placeholder's outline to black (index 0), so it holds 97.4's
    figure/ground rule on the 1bpp sets."""
    idx, alpha = fr
    for x, y in spr_outline(idx, alpha, len(idx[0])):
        idx[y][x] = 0
    return fr


# --- sprite placeholders: a helmeted guard, six decorations, eight pickups, the
#     weapon - procedural and deterministic, to be replaced by the image
#     model's on the same contract -----------------------------------------------------

def _sblank(w=SPR, h=SPR):
    return [[0] * w for _ in range(h)], [[0] * w for _ in range(h)]


def _sfill(fr, x0, y0, x1, y1, v):
    idx, alpha = fr
    for y in range(max(0, y0), min(len(idx), y1)):
        for x in range(max(0, x0), min(len(idx[0]), x1)):
            idx[y][x] = v
            alpha[y][x] = 1


def _sdisc(fr, cx, cy, r, v):
    idx, alpha = fr
    for y in range(len(idx)):
        for x in range(len(idx[0])):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                idx[y][x] = v
                alpha[y][x] = 1


def _guard(facing, phase, pose):
    """A helmeted guard in a blue coat, 32 x 32, facing 0..4 (front to
    back), walk phase 0/1, pose 'walk' / 'shoot0' / 'shoot1' / 'pain' /
    'die0..2' / 'dead'. Blocky on purpose: it is what the losable criterion
    is measured on until the real art lands, and every feature is a solid
    rectangle a 2x2 dither can still show."""
    fr = _sblank()
    coat, helm, face, boot, gun = 9, 8, 14, 0, 7
    if pose == "dead":
        _sfill(fr, 3, 24, 29, 31, coat)
        _sfill(fr, 3, 22, 9, 26, helm)
        _sfill(fr, 26, 25, 29, 29, boot)
        return fr
    if pose.startswith("die"):
        k = int(pose[3])
        top = 6 + 6 * k
        _sfill(fr, 10 + k, top, 22 - k, 28, coat)
        _sfill(fr, 11 + k, top - 4, 21 - k, top + 1, helm)
        _sfill(fr, 10, 28, 22, 31, boot)
        return fr
    # the body: head, coat, legs; the facing moves the face and the arms
    _sfill(fr, 11, 2, 21, 7, helm)                     # the helmet
    if facing == 0:
        _sfill(fr, 12, 7, 20, 11, face)
    elif facing == 1:
        _sfill(fr, 14, 7, 21, 11, face)
        _sfill(fr, 11, 7, 14, 11, helm)
    elif facing == 2:
        _sfill(fr, 17, 7, 21, 11, face)
        _sfill(fr, 11, 7, 17, 11, helm)
    elif facing == 3:
        _sfill(fr, 19, 7, 21, 11, face)
        _sfill(fr, 11, 7, 19, 11, helm)
    else:
        _sfill(fr, 11, 7, 21, 11, helm)
    _sfill(fr, 10, 11, 22, 24, coat)                   # the coat
    if facing == 2:                                    # the profile: narrow
        for y in range(11, 24):
            for x in (10, 11, 20, 21):
                fr[0][y][x] = 0
                fr[1][y][x] = 0
    elif facing in (1, 3):                             # the three-quarters:
        for y in range(11, 24):                        # between the two
            for x in ((10,) if facing == 1 else (10, 21)):
                fr[0][y][x] = 0
                fr[1][y][x] = 0
    if facing == 1:                                    # ...the near arm out,
        _sfill(fr, 22, 12, 27, 23, coat)               # the gun hand below it
        _sfill(fr, 24, 22, 29, 25, gun)
    if facing == 3:                                    # ...and from behind,
        _sfill(fr, 5, 12, 11, 22, 8)                   # the pack on its far side
    belt = 4
    _sfill(fr, 11, 17, 21, 19, belt)
    if pose == "pain":
        _sfill(fr, 12, 7, 20, 11, 12)                  # the face reddens
    if pose in ("shoot0", "shoot1"):
        _sfill(fr, 20, 12, 28, 15, gun)                # the pistol out
        if pose == "shoot1":
            _sfill(fr, 27, 10, 31, 17, 14)             # the flash
    elif facing == 0 or facing == 4:
        _sfill(fr, 7, 12, 10, 21, coat)                # arms
        _sfill(fr, 22, 12, 25, 21, coat)
        if facing == 0:
            _sfill(fr, 20, 15, 27, 18, gun)            # the gun in hand
    else:
        _sfill(fr, 20, 12, 26, 15, gun)
    # legs, the walk phase swapping which is forward
    if phase == 0:
        _sfill(fr, 11, 24, 15, 31, boot)
        _sfill(fr, 17, 24, 21, 30, boot)
    else:
        _sfill(fr, 11, 24, 15, 30, boot)
        _sfill(fr, 17, 24, 21, 31, boot)
    _sfill(fr, 11, 24, 21, 26, coat)
    return fr


def _dog(facing, phase, pose):
    """A brown dog, 32 x 32 (wave 6): facing 0..3 (its front, the
    three-quarter with its right side toward the viewer, its right side,
    its back), walk phase 0/1, pose 'walk' / 'bite' / 'die' / 'dead'. LOW -
    every frame in rows DOG_TOP..31, the frame's lower half, standing on the
    frame's floor (97.4; the first placeholder reached row 3, a guard-height
    dog taller than the door, and the frame costs were measured on it -
    review, wave 6 r2) - and blocky, as the guard is, until the image
    model's lands on the same contract."""
    fr = _sblank()
    coat, snout, nose, tooth, tongue = 6, 7, 0, 15, 12
    if pose == "dead":
        _sfill(fr, 4, 26, 26, 31, coat)
        _sfill(fr, 24, 25, 30, 30, coat)
        _sfill(fr, 8, 30, 20, 32, 4)                   # the pool
        return fr
    if pose == "die":
        _sfill(fr, 5, 21, 25, 27, coat)                # keeling over
        _sfill(fr, 22, 18, 29, 24, coat)
        _sfill(fr, 7, 27, 10, 31, coat)
        _sfill(fr, 19, 27, 22, 31, coat)
        return fr
    if pose == "bite":                                 # the jaws, open, at the
        _sfill(fr, 9, 16, 12, 18, coat)                # viewer: the ears
        _sfill(fr, 20, 16, 23, 18, coat)
        _sfill(fr, 9, 17, 23, 21, coat)                # the head
        _sfill(fr, 10, 21, 22, 22, snout)              # the upper jaw
        _sfill(fr, 11, 22, 21, 23, tooth)
        _sfill(fr, 11, 23, 21, 24, tongue)
        _sfill(fr, 11, 24, 21, 25, tooth)
        _sfill(fr, 10, 25, 22, 26, snout)              # the lower jaw
        _sfill(fr, 10, 26, 22, 29, coat)               # the chest
        _sfill(fr, 11, 29, 14, 32, coat)
        _sfill(fr, 18, 29, 21, 32, coat)
        return fr
    front, back = (32, 31) if phase == 0 else (31, 32)
    if facing == 0:                                    # its front
        _sfill(fr, 10, 16, 13, 19, coat)               # the ears
        _sfill(fr, 19, 16, 22, 19, coat)
        _sfill(fr, 11, 17, 21, 23, coat)               # the head
        _sfill(fr, 13, 21, 19, 24, snout)              # the muzzle
        _sfill(fr, 15, 21, 17, 22, nose)
        _sfill(fr, 10, 23, 22, 27, coat)               # the chest
        _sfill(fr, 11, 27, 14, front, coat)            # the forelegs
        _sfill(fr, 18, 27, 21, back, coat)
    elif facing == 1:                                  # three-quarter
        _sfill(fr, 6, 22, 21, 28, coat)                # the body, going away
        _sfill(fr, 18, 16, 21, 18, coat)               # the ear
        _sfill(fr, 17, 17, 26, 24, coat)               # the head, near
        _sfill(fr, 23, 20, 29, 24, snout)              # the muzzle
        _sfill(fr, 27, 20, 29, 21, nose)
        _sfill(fr, 3, 20, 7, 23, coat)                 # the tail
        _sfill(fr, 8, 28, 11, back, coat)
        _sfill(fr, 17, 28, 20, front, coat)
    elif facing == 2:                                  # its right side
        _sfill(fr, 6, 21, 24, 27, coat)                # the body, long
        _sfill(fr, 22, 16, 25, 18, coat)               # the ear
        _sfill(fr, 21, 17, 28, 24, coat)               # the head
        _sfill(fr, 26, 20, 31, 23, snout)              # the muzzle
        _sfill(fr, 29, 20, 31, 21, nose)
        _sfill(fr, 2, 19, 7, 22, coat)                 # the tail
        _sfill(fr, 7, 27, 10, front, coat)             # the legs, the walk
        _sfill(fr, 11, 27, 14, back, coat)             # swapping which pair
        _sfill(fr, 17, 27, 20, back, coat)             # reaches
        _sfill(fr, 21, 27, 24, front, coat)
    else:                                              # its back
        _sfill(fr, 15, 16, 17, 20, snout)              # the tail, up
        _sfill(fr, 11, 17, 14, 20, coat)               # the ears over it
        _sfill(fr, 18, 17, 21, 20, coat)
        _sfill(fr, 10, 19, 22, 28, coat)               # the rump
        _sfill(fr, 11, 28, 14, front, coat)
        _sfill(fr, 18, 28, 21, back, coat)
    return fr


def _deco(name):
    fr = _sblank()
    if name == "pillar":
        _sfill(fr, 11, 0, 21, 32, 7)
        _sfill(fr, 9, 0, 23, 3, 8)
        _sfill(fr, 9, 29, 23, 32, 8)
        for y in range(3, 29, 4):
            _sfill(fr, 13, y, 14, y + 2, 15)
    elif name == "barrel":
        _sfill(fr, 9, 12, 23, 32, 6)
        _sfill(fr, 9, 14, 23, 16, 8)
        _sfill(fr, 9, 22, 23, 24, 8)
        _sfill(fr, 9, 29, 23, 31, 8)
    elif name == "table":
        _sfill(fr, 3, 18, 29, 21, 6)
        _sfill(fr, 4, 21, 7, 32, 4)
        _sfill(fr, 25, 21, 28, 32, 4)
        _sfill(fr, 12, 14, 20, 18, 7)
    elif name == "bones":
        _sfill(fr, 4, 27, 12, 30, 15)
        _sfill(fr, 16, 26, 28, 28, 15)
        _sdisc(fr, 22, 23, 3, 15)
        _sfill(fr, 20, 22, 22, 24, 0)
    elif name == "puddle":
        _sfill(fr, 6, 28, 26, 31, 4)
        _sfill(fr, 9, 27, 22, 28, 12)
    else:                                               # plant
        _sfill(fr, 12, 24, 20, 32, 6)
        _sfill(fr, 8, 12, 24, 24, 2)
        _sfill(fr, 13, 6, 19, 12, 10)
        _sfill(fr, 6, 16, 9, 19, 10)
        _sfill(fr, 23, 15, 26, 18, 10)
    return fr


def _pickup(name):
    fr = _sblank(PICK, PICK)
    if name == "ammo":
        _sfill(fr, 4, 8, 12, 15, 8)
        _sfill(fr, 5, 5, 11, 8, 14)
    elif name == "medkit":
        _sfill(fr, 2, 6, 14, 15, 15)
        _sfill(fr, 7, 7, 9, 14, 4)
        _sfill(fr, 4, 9, 12, 12, 4)
    elif name == "food":
        _sfill(fr, 3, 10, 13, 15, 7)
        _sfill(fr, 5, 6, 11, 10, 6)
    elif name == "goldkey":
        _sfill(fr, 3, 7, 13, 9, 14)
        _sdisc(fr, 4, 8, 3, 14)
        _sfill(fr, 11, 9, 13, 12, 14)
    elif name == "silverkey":
        _sfill(fr, 3, 7, 13, 9, 7)
        _sdisc(fr, 4, 8, 3, 7)
        _sfill(fr, 11, 9, 13, 12, 7)
    elif name == "treasure":
        _sfill(fr, 4, 9, 12, 15, 6)
        _sfill(fr, 4, 6, 12, 9, 14)
        _sfill(fr, 7, 4, 9, 6, 14)
    elif name == "chalice":
        _sfill(fr, 5, 3, 11, 8, 14)
        _sfill(fr, 7, 8, 9, 13, 14)
        _sfill(fr, 4, 13, 12, 15, 14)
    else:                                               # life
        _sdisc(fr, 8, 8, 6, 12)
        _sfill(fr, 7, 4, 9, 12, 15)
        _sfill(fr, 4, 7, 12, 9, 15)
    return fr


def _weapon(name, k):
    fr = _sblank(WPN_W, WPN_H)
    if name == "knife":
        _sfill(fr, 6 + k, 4 - k, 9 + k, 16 - k, 7)
        _sfill(fr, 5 + k, 15 - k, 11 + k, 24, 6)
    elif name == "pistol":
        _sfill(fr, 5, 10 + k, 10, 24, 6)        # brown: index 8 IS Mode X's
                                                # floor grey, and the grip
                                                # vanished into it (review,
                                                # wave 3)
        _sfill(fr, 6, 4 + k, 9, 10 + k, 7)
        if k == 1:
            _sfill(fr, 4, 0, 11, 5, 14)
    else:
        _sfill(fr, 4, 8 + k, 12, 24, 6)         # (brown, as the pistol's)
        _sfill(fr, 6, 2 + k, 10, 8 + k, 7)
        _sfill(fr, 3, 14, 5, 22, 8)
        if k == 1:
            _sfill(fr, 3, 0, 13, 4, 14)
    return fr


def write_sprite_placeholders(force=False):
    n = 0
    stems = sprite_names()
    for i, stem in enumerate(stems):
        p = sprite_path(stem)
        if os.path.exists(p) and not force:
            continue
        if stem.startswith("g_"):
            if i < G_SHOOT:
                fr = _guard(i // 2, i & 1, "walk")
            elif i < G_PAIN:
                fr = _guard(0, 0, "shoot%d" % (i - G_SHOOT))
            elif i < G_DIE:
                fr = _guard(0, 0, "pain")
            elif i < G_DEAD:
                fr = _guard(0, 0, "die%d" % (i - G_DIE))
            else:
                fr = _guard(0, 0, "dead")
            _outline_dark(fr)
            write_png_rgba(p, SPR, SPR, fr[0], fr[1])
        elif stem.startswith("dog_"):
            if i < D_BITE:
                fr = _dog((i - D_WALK0) // 2, (i - D_WALK0) & 1, "walk")
            else:
                fr = _dog(0, 0, stem[4:])
            _outline_dark(fr)
            write_png_rgba(p, SPR, SPR, fr[0], fr[1])
        elif stem.startswith("d_"):
            fr = _outline_dark(_deco(stem[2:]))
            write_png_rgba(p, SPR, SPR, fr[0], fr[1])
        else:
            fr = _outline_dark(_pickup(stem[2:]))
            write_png_rgba(p, PICK, PICK, fr[0], fr[1])
        n += 1
    for stem in weapon_names():
        p = sprite_path(stem)
        if os.path.exists(p) and not force:
            continue
        _, name, k = stem.split("_")
        fr = _outline_dark(_weapon(name, int(k)))
        write_png_rgba(p, WPN_W, WPN_H, fr[0], fr[1])
        n += 1
    return n


def write_png_rgba(path, w, h, idx, alpha):
    """An RGBA PNG of the sixteen colours: alpha 255 where `alpha` is set."""
    raw = b"".join(b"\x00" + b"".join(bytes(PALETTE[idx[y][x]]) + (b"\xff" if alpha[y][x] else b"\x00")
                                        for x in range(w)) for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(_chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(_chunk(b"IEND", b""))


def spr_preview(sp, wp, out_dir):
    """Every frame through every backend's lit table at the adapter's aspect
    (a transparent texel drawn dark blue), and the twelve-column front/side
    pair the criterion is read from."""
    written = []
    for be in BACKENDS:
        zx, zy = ASPECT[be]
        for i, (idx, alpha) in enumerate(sp + wp):
            fr = spr_frame(idx, alpha, be, SPR if i < NSPR else WPN_W)
            w = SPR if i < NSPR else WPN_W
            ncol = w // 2 if be == "c160" else w
            rows = []
            for v in range(SPR):
                line = []
                for c in range(ncol):
                    if be == "c160":
                        op = alpha[v][2 * c] or alpha[v][2 * c + 1]
                    else:
                        op = alpha[v][c]
                    if op:
                        line += _rgb_of(be, fr[c * SPR + v], v)
                    else:
                        line += [(0, 0, 0x40)] * len(_rgb_of(be, 0, v))
                rows.append(line)
            big = []
            for r in rows:
                line = []
                for px in r:
                    line += [px] * zx
                for _ in range(zy):
                    big.append(line)
            stem = (sprite_names() + weapon_names())[i]
            p = os.path.join(out_dir, "spr-%s-%s.png" % (be, stem))
            write_png_rgb(p, len(big[0]), len(big), big)
            written.append(p)
        if be in ("cga4", "herc"):
            rows = []
            pair = [spr_at12(sp[G_WALK0], be), spr_at12(sp[G_WALK0 + 4], be)]
            al = [sp[G_WALK0][1], sp[G_WALK0 + 4][1]]
            for v in range(SPR):
                line = []
                for k in range(2):
                    for j in range(12):
                        src = (j * SPR) // 12
                        if al[k][v][src]:
                            line += _rgb_of(be, pair[k][j][v], v)
                        else:
                            line += [(0, 0, 0x40)] * len(_rgb_of(be, 0, v))
                    line += [(0x40, 0x40, 0x40)] * 4
                rows.append(line)
            big = []
            for r in rows:
                line = []
                for px in r:
                    line += [px] * zx
                for _ in range(zy):
                    big.append(line)
            p = os.path.join(out_dir, "spr-%s-front-side.png" % be)
            write_png_rgb(p, len(big[0]), len(big), big)
            written.append(p)
    return written


# --- the include and the stream ------------------------------------------------------

def generate(ms, sp=None, wp=None):
    it = ink_tables()
    blob = art_blob(ms)
    if sp is None:
        sp, wp = sprites(), weapons()
    sblob = sprite_blob(sp, wp)
    L = []
    w = L.append
    w("; apps/pixelstein/pxart.inc - GENERATED by tools/pxsart.py, DO NOT EDIT")
    w("; (SPEC.md 97.4). The art's numbers and the four ink tables (WIN1's band")
    w("; is the Hercules set); never pixels. The masters go to the machine as")
    w("; the lazy art part's LZ4 stream (build/pxsart.bin, part 4 of 97.9),")
    w("; which the loader expands into PXA_SIZE bytes, and px_bt_build")
    w("; transposes them through these tables into part 2 (pxgen.inc).")
    w("")
    w("PXA_NWALL   equ %d             ; wall masters, material 1 first" % NWALL)
    w("PXA_TEX     equ %d             ; texels a side" % TEX)
    w("PXA_WALLSZ  equ %d            ; 32 rows x 16 bytes: two texels a byte, the" % WALLSZ)
    w("                                ; even column in the high nibble")
    w("PXA_WALL0   equ 0               ; ...from the head of the claim")
    total = len(blob) + len(sblob)
    w("PXA_SIZE    equ %d          ; the expanded claim, bytes: the walls, then" % total)
    w("                                ; the sprite masters (97.4, wave 3)")
    w("PXA_KB      equ %d             ; ...as OSAPI_MEM_CLAIM wants it" % ((total + 1023) // 1024))
    w("PXA_SHADES  equ %d              ; lit, dark" % SHADES)
    w("PXA_MATSZ   equ %d           ; a material's bytes in the byte-texture set" % MATSZ)
    w("PXA_BTSIZE  equ %d          ; ...and the whole set (part 2, 97.9)" % BT_SIZE)
    w("PXA_BTKB    equ %d" % ((BT_SIZE + 1023) // 1024))
    w("")
    w("; THE SPRITE MASTERS in the claim (97.4, 97.6): %d frames of 32 x 32 after" % NSPR)
    w("; the walls - 512 packed nibbles (row major, even column high, a")
    w("; transparent texel 0) then 128 alpha bytes (row major, bit 7 the left")
    w("; column) - and %d weapon frames of 16 x 32 (256 + 64) after them" % NWPN)
    w("PXA_SPR0    equ %d           ; the first sprite frame's offset" % len(blob))
    w("PXA_NSPR    equ %d" % NSPR)
    w("PXA_SPRMSZ  equ %d            ; a frame in the claim" % SPRMSZ)
    w("PXA_SPRALPHA equ %d           ; ...its alpha bytes begin here" % (SPR * SPR // 2))
    w("PXA_WPN0    equ %d          ; the first weapon frame's offset" % (len(blob) + NSPR * SPRMSZ))
    w("PXA_NWPN    equ %d" % NWPN)
    w("PXA_WPNMSZ  equ %d            ; a weapon frame in the claim" % WPNMSZ)
    w("PXA_WPNALPHA equ %d           ; ...its alpha bytes begin here" % (WPN_W * SPR // 2))
    w("PXA_WPNW    equ %d             ; a weapon frame's columns" % WPN_W)
    w("")
    w("; THE SPRITE SET, a claim of its own (97.6, 97.9; PXS_*): what px_spr_build transposes the")
    w("; masters into - a frame is 32 columns of 32 texel bytes through the lit")
    w("; ink table (C160: 16 column pairs, both nibbles), then 32 run tables of")
    w("; PXS_RUNSZ bytes (count, then up to PXS_MAXRUNS (v0, v1) pairs, v1")
    w("; exclusive); a weapon frame the same at 16 columns. The frame order:")
    w("; the guard's walk (facing * 2 + phase), shoot, pain, die, dead, six")
    w("; decorations (static kind 8 first), eight pickups (kind 0 first), the")
    w("; dog's walk (four facings * 2 + phase), bite, die, dead (wave 6)")
    w("PXS_MAXRUNS equ %d" % MAXRUNS)
    w("PXS_RUNSZ   equ %d" % RUNSZ)
    w("PXS_RUNOFS  equ %d           ; a frame's run tables begin here" % (SPR * SPR))
    w("PXS_FRSZ    equ %d           ; a frame in the set" % FRSZ)
    w("PXS_WFRSZ   equ %d            ; a weapon frame in the set" % WFRSZ)
    w("PXS_WRUNOFS equ %d            ; ...its run tables" % (WPN_W * SPR))
    w("PXS_WPN0    equ %d          ; the first weapon frame" % (NSPR * FRSZ))
    w("PXS_SIZE    equ %d          ; the whole set" % (NSPR * FRSZ + NWPN * WFRSZ))
    w("PXS_KB      equ %d" % ((NSPR * FRSZ + NWPN * WFRSZ + 1023) // 1024))
    w("PXS_G_WALK0 equ %d              ; the guard's frames: facing * 2 + walk phase" % G_WALK0)
    w("PXS_G_SHOOT equ %d" % G_SHOOT)
    w("PXS_G_PAIN  equ %d" % G_PAIN)
    w("PXS_G_DIE   equ %d" % G_DIE)
    w("PXS_G_DEAD  equ %d" % G_DEAD)
    w("PXS_DECO0   equ %d             ; decoration kind 8's frame" % DECO0)
    w("PXS_PICK0   equ %d             ; pickup kind 0's frame" % PICK0)
    w("PXS_D_WALK0 equ %d             ; the dog's frames: facing * 2 + walk phase" % D_WALK0)
    w("PXS_D_BITE  equ %d" % D_BITE)
    w("PXS_D_DIE   equ %d" % D_DIE)
    w("PXS_D_DEAD  equ %d" % D_DEAD)
    w("")
    w("; the ink tables: the texel byte (C160: the attribute nibble, both nibbles")
    w("; built by the transpose) of each of the sixteen colour indices, sixteen")
    w("; lit then sixteen dark, even-row phase (the odd row is the scaler's")
    w("; rotate, 97.3). WIN1's band is the Hercules set")
    for be in BACKENDS:
        lit, dark = it[be]
        w("px_it_%s:" % be)
        w("    db " + ", ".join("0x%02X" % v for v in lit) + "    ; lit")
        w("    db " + ", ".join("0x%02X" % v for v in dark) + "    ; dark")
    w("")
    w("; WIN4's 32 -> 16 tables (97.14, wave 6): Mode X's byte, 0..15 lit and")
    w("; 16..31 dark, to its colour in BOTH nibbles, for the EVEN rows and then")
    w("; the ODD ones - a dark face is its LIT colour on the even rows and black")
    w("; on the odd, a line dither that keeps the material's hue (the first")
    w("; cut's C160 twin turned brown to red and red to blue)")
    w("px_it_win4:")
    for par, tab in enumerate(win4_tables()):
        w("    db " + ", ".join("0x%02X" % v for v in tab[:16]) + "    ; %s, lit" % ("even", "odd")[par])
        w("    db " + ", ".join("0x%02X" % v for v in tab[16:]) + "    ; %s, dark" % ("even", "odd")[par])
    w("")
    return "\n".join(L) + "\n"


def stream(ms, sp=None, wp=None):
    if sp is None:
        sp, wp = sprites(), weapons()
    blob = art_blob(ms) + sprite_blob(sp, wp)
    z = os88lz.compress(blob, os88lz.LZ4)
    assert os88lz.decompress(z, os88lz.LZ4, len(blob)) == blob, "the LZ4 round trip"
    return z


# --- the HUD masters (wave 4, SPEC.md 97.4, 97.13) -----------------------------
#
# ONE BIT DEEP: a pixel is INK when its alpha is >= 128 and its Rec.601
# luminance >= 64, ground otherwise - the machine draws ink in the bar's own
# colour for the backend in force, so the file's colours are a mask and
# nothing more. They go to the machine as pxhuda.inc (--hud), IN PART 0's
# IMAGE and not the lazy stream: the bar is drawn on every rung, and a Flat
# launch never fetches the stream. A row is w / 8 bytes, bit 7 leftmost.

HUD_INK = 64                        # ink: alpha >= 128 and luminance >= 64/255
                                    # (luma() is ALREADY 0..255 - review r2
                                    # found `luma * 255`, which made any
                                    # pixel not pure black ink)
HUD_DIGIT = (8, 16)
HUD_FACE = (16, 24)
HUD_KEY = (8, 8)
HUD_WPN = (24, 16)
HUD_NFACE = 6
HUD_SPECS = ([("h_digit%d" % d, HUD_DIGIT) for d in range(10)]
             + [("h_face%d" % f, HUD_FACE) for f in range(HUD_NFACE)]
             + [("h_goldkey", HUD_KEY), ("h_silverkey", HUD_KEY)]
             + [("h_%s" % n, HUD_WPN) for n in WEAPON_NAMES])
DEFAULT_HUD = os.path.join(ROOT, "apps", "pixelstein", "pxhuda.inc")


def hud_path(stem):
    return os.path.join(ART_DIR, stem + ".png")


def load_hud(stem, size):
    """A HUD master as h rows of w bits (1 ink, 0 ground), or raises."""
    path = hud_path(stem)
    w, h, rows = read_png(path)
    if (w, h) != size:
        raise ValueError("%s: %dx%d, a %s master is %dx%d (SPEC.md 97.4)"
                         % (path, w, h, stem.rstrip("0123456789"), size[0], size[1]))
    bits = [[1 if (a >= 128 and luma((r, g, b)) >= HUD_INK) else 0
             for (r, g, b, a) in row] for row in rows]
    if stem.startswith("h_digit"):
        # THE GAP IS THE MASTER'S (97.4): digits sit on adjacent cells, so
        # the rightmost column and the bottom row are GROUND - a digit that
        # fills its cell fuses with the next ("100" read as two blobs)
        if any(row[size[0] - 1] for row in bits):
            raise ValueError("%s: ink in the rightmost column - a digit's column %d is "
                             "ground, the gap to the next digit (SPEC.md 97.4)"
                             % (path, size[0] - 1))
        if any(bits[size[1] - 1]):
            raise ValueError("%s: ink in the bottom row - a digit's row %d is ground "
                             "(SPEC.md 97.4)" % (path, size[1] - 1))
    return bits


def huds():
    return [(stem, size, load_hud(stem, size)) for stem, size in HUD_SPECS]


def hud_bytes(bits):
    out = bytearray()
    for row in bits:
        for x in range(0, len(row), 8):
            v = 0
            for k in range(8):
                v = (v << 1) | row[x + k]
            out.append(v)
    return bytes(out)


# the placeholder digits: a bold seven-segment face in 7 x 15 of the 8 x 16
# cell (column 7 and row 15 are the gap, 97.4) - segments a (top) b (upper
# right) c (lower right) d (bottom) e (lower left) f (upper left) g (middle)
_SEG = {0: "abcdef", 1: "bc", 2: "abged", 3: "abgcd", 4: "fgbc", 5: "afgcd",
        6: "afgedc", 7: "abc", 8: "abcdefg", 9: "abcfgd"}


def _hud_digit(d):
    im = [[0] * 8 for _ in range(16)]
    segs = _SEG[d]

    def box(x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                im[y][x] = 1
    if "a" in segs:
        box(1, 1, 5, 2)
    if "g" in segs:
        box(1, 7, 5, 8)
    if "d" in segs:
        box(1, 13, 5, 14)
    if "f" in segs:
        box(0, 1, 1, 8)
    if "b" in segs:
        box(5, 1, 6, 8)
    if "e" in segs:
        box(0, 7, 1, 14)
    if "c" in segs:
        box(5, 7, 6, 14)
    return im


def _hud_face(f):
    """An oval head under a helmet's brim: eyes and mouth follow the frame."""
    im = [[0] * 16 for _ in range(24)]
    for y in range(24):
        for x in range(16):
            dx, dy = (x - 7.5) / 7.0, (y - 12.5) / 11.0
            if dx * dx + dy * dy <= 1.0:
                im[y][x] = 1
    for y in range(2, 6):                   # the helmet: a solid cap, its
        for x in range(1, 15):              # brim a hole's width off the brow
            if (x - 7.5) ** 2 / 49.0 + (y - 12.5) ** 2 / 121.0 <= 1.0:
                im[y][x] = 1
    for x in range(1, 15):
        im[6][x] = 0
    # eyes (ground holes in the ink)
    ey = 9 + (1 if f == 3 else 0)
    if f == 5:                              # dead: crosses
        for (x0, y0) in ((3, 8), (9, 8)):
            for k in range(3):
                im[y0 + k][x0 + k] = 0
                im[y0 + 2 - k][x0 + k] = 0
    else:
        for x0 in (4, 10):
            im[ey][x0] = im[ey][x0 + 1] = 0
            if f != 2:
                im[ey + 1][x0] = im[ey + 1][x0 + 1] = 0
    # mouth
    my = 17
    if f == 4:                              # the grin
        for x in range(4, 12):
            im[my][x] = 0
        for x in range(5, 11):
            im[my + 1][x] = 0
    elif f == 5:
        for x in range(5, 11):
            im[my + 1][x] = 0
    else:
        w = (4, 3, 3, 2)[f]
        for x in range(8 - w, 8 + w):
            im[my + (1 if f >= 2 else 0)][x] = 0
    if f in (2, 3):                         # a bruise, a cut
        im[13][3] = im[14][3] = im[13][12] = 0
    return im


def _hud_key(silver):
    im = [[0] * 8 for _ in range(8)]
    for y in range(1, 4):
        for x in range(0, 3):
            im[y][x] = 1
    im[2][1] = 0                            # the bow's hole
    for x in range(3, 8):
        im[2][x] = 1                        # the shank
    im[3][6] = im[4][6] = im[3][4] = 1      # the bit
    if silver:
        im[4][4] = 1
    return im


def _hud_wpn(name):
    im = [[0] * 24 for _ in range(16)]

    def box(x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                im[y][x] = 1
    if name == "knife":
        box(2, 7, 7, 9)                     # the grip
        box(8, 6, 8, 10)                    # the guard
        for x in range(9, 22):              # the blade, tapering
            box(x, 7, x, 9 if x < 18 else 8)
    elif name == "pistol":
        box(4, 4, 19, 7)                    # the slide
        box(4, 8, 8, 13)                    # the grip
        box(10, 8, 12, 9)                   # the trigger guard
    else:
        box(1, 5, 22, 8)                    # the receiver and barrel
        box(3, 9, 6, 13)                    # the grip
        box(12, 9, 14, 14)                  # the magazine
        box(0, 4, 3, 9)                     # the stock
    return im


def hud_placeholder(stem):
    if stem.startswith("h_digit"):
        return _hud_digit(int(stem[7:]))
    if stem.startswith("h_face"):
        return _hud_face(int(stem[6:]))
    if stem == "h_goldkey":
        return _hud_key(False)
    if stem == "h_silverkey":
        return _hud_key(True)
    return _hud_wpn(stem[2:])


def write_hud_placeholders(force=False):
    n = 0
    os.makedirs(ART_DIR, exist_ok=True)
    for stem, (w, h) in HUD_SPECS:
        p = hud_path(stem)
        if os.path.exists(p) and not force:
            continue
        bits = hud_placeholder(stem)
        assert len(bits) == h and all(len(r) == w for r in bits), stem
        write_png_indexed(p, w, h, [[15 if b else 0 for b in row] for row in bits])
        n += 1
    return n


# the HUD at each backend's aspect (--preview): a master's bit is ONE pixel
# of the destination on CGA4, Mode X, WIN1 and C160 (px_x2 / px_x4 expand a
# bit to the pixel's bits) and TWO dots on Hercules (doubled, pxhud.inc) -
# drawn here in 640 x 400 units, so the image model sees a digit as it ships
HUD_ASPECT = {"cga4": (2, 2), "herc": (2, 1), "c160": (4, 4), "modex": (2, 2), "win1": (1, 1)}
HUD_INKRGB = {"cga4": (0xAA, 0x55, 0x00), "herc": (0xFF, 0xFF, 0xFF),
              "c160": (0xFF, 0xFF, 0x55), "modex": (0xFF, 0xFF, 0x55), "win1": (0xFF, 0xFF, 0xFF)}


def hud_preview(hs, out_dir):
    """One PNG a backend: the ten digits side by side as the bar lays them
    (adjacent cells), then the faces, the keys and the weapons."""
    os.makedirs(out_dir, exist_ok=True)
    by = {stem: bits for stem, _z, bits in hs}
    rows_of = [["h_digit%d" % d for d in range(10)],
               ["h_face%d" % f for f in range(HUD_NFACE)] + ["h_goldkey", "h_silverkey"],
               ["h_%s" % n for n in WEAPON_NAMES]]
    written = []
    for be, (zx, zy) in HUD_ASPECT.items():
        ink = HUD_INKRGB[be]
        band = []
        for names in rows_of:
            h = max(len(by[n]) for n in names)
            for y in range(h + 2):
                line = []
                for n in names:
                    b = by[n]
                    for x in range(len(b[0])):
                        v = b[y][x] if y < len(b) else 0
                        line += [ink if v else (0, 0, 0)] * zx
                    if not n.startswith("h_digit"):
                        line += [(0, 0, 0x55)] * (4 * zx)   # a spacer, not a gap
                band.append(line)
        w = max(len(r) for r in band)
        big = []
        for r in band:
            r = r + [(0, 0, 0)] * (w - len(r))
            for _ in range(zy):
                big.append(r)
        p = os.path.join(out_dir, "hud-%s.png" % be)
        write_png_rgb(p, w, len(big), big)
        written.append(p)
    return written


def hud_generate(hs):
    """pxhuda.inc: the HUD masters' bits, in part 0's image (97.13)."""
    L = []
    w = L.append
    w("; apps/pixelstein/pxhuda.inc - GENERATED by tools/pxsart.py --hud, DO NOT")
    w("; EDIT (SPEC.md 97.4, 97.13). THE STATUS BAR'S MASTERS, ONE BIT DEEP: the")
    w("; one art the package carries in part 0's image rather than the lazy")
    w("; stream, because the bar is drawn on every rung and a launch on Flat never")
    w("; fetches the stream. A row is width / 8 bytes, bit 7 the leftmost pixel;")
    w("; a set bit is INK, drawn in the bar's own colour for the backend (pxhud.inc).")
    w("")
    w("PXU_DIGW    equ %d               ; a digit: cells (8 px) wide..." % (HUD_DIGIT[0] // 8))
    w("PXU_DIGH    equ %d              ; ...rows tall" % HUD_DIGIT[1])
    w("PXU_DIGSZ   equ %d              ; ...bytes" % (HUD_DIGIT[0] * HUD_DIGIT[1] // 8))
    w("PXU_FACEW   equ %d" % (HUD_FACE[0] // 8))
    w("PXU_FACEH   equ %d" % HUD_FACE[1])
    w("PXU_FACESZ  equ %d" % (HUD_FACE[0] * HUD_FACE[1] // 8))
    w("PXU_NFACE   equ %d" % HUD_NFACE)
    w("PXU_KEYH    equ %d" % HUD_KEY[1])
    w("PXU_KEYSZ   equ %d" % (HUD_KEY[0] * HUD_KEY[1] // 8))
    w("PXU_WPNW    equ %d" % (HUD_WPN[0] // 8))
    w("PXU_WPNH    equ %d" % HUD_WPN[1])
    w("PXU_WPNSZ   equ %d" % (HUD_WPN[0] * HUD_WPN[1] // 8))
    w("")
    groups = (("px_hud_dig", "h_digit"), ("px_hud_face", "h_face"),
              ("px_hud_key", "h_goldkey h_silverkey"), ("px_hud_wpn", "h_knife h_pistol h_mgun"))
    by = {stem: bits for stem, _size, bits in hs}
    for label, stems in groups:
        names = stems.split() if " " in stems else [s for s, _z in HUD_SPECS if s.startswith(stems)]
        w("%s:" % label)
        for stem in names:
            b = hud_bytes(by[stem])
            w("    ; %s" % stem)
            for i in range(0, len(b), 16):
                w("    db " + ", ".join("0x%02X" % v for v in b[i:i + 16]))
    w("")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", help="write the include here (default: nothing)")
    ap.add_argument("--stream", help="write the LZ4 art stream here")
    ap.add_argument("--placeholder", action="store_true",
                    help="write the procedural masters that are missing")
    ap.add_argument("--force", action="store_true", help="...overwriting the SPRITE placeholders "
                    "that exist (the wall masters only with --walls: they are the image model's)")
    ap.add_argument("--walls", action="store_true",
                    help="with --force: overwrite the WALL masters too - which are the image "
                         "model's since wave 2, so this is never what a sprite rewrite wants "
                         "(a review-round --force wrote fifteen placeholders over them)")
    ap.add_argument("--check", action="store_true",
                    help="refuse a bad master in words, and assert the losable criterion")
    ap.add_argument("--preview", metavar="DIR",
                    help="render every master through every ink table into DIR")
    ap.add_argument("--inks", action="store_true", help="print the ink tables")
    ap.add_argument("--hud", metavar="INC",
                    help="write the HUD masters' include (pxhuda.inc, 97.13) here")
    a = ap.parse_args()
    if a.placeholder:
        n = write_placeholders(a.force and a.walls)
        n += write_sprite_placeholders(a.force)
        n += write_hud_placeholders(a.force)
        print("pxsart: wrote %d placeholder master(s) under %s" % (n, ART_DIR))
    if a.hud and not (a.out or a.stream or a.check or a.preview or a.inks):
        # THE HUD ALONE, and nothing else loaded: the pxs-gen fast row runs
        # this inside every `make`, and the walls' and sprites' criteria are
        # ~0.6 s the include does not depend on
        try:
            hs = huds()
        except (ValueError, OSError) as e:
            sys.exit("pxsart: %s" % e)
        with open(a.hud, "w") as f:
            f.write(hud_generate(hs))
        return
    try:
        ms = masters()
        sp, wp = sprites(), weapons()
        for i, (idx, alpha) in enumerate(sp):
            try:
                frame_runs(alpha)
                frame_runs(alpha, SPR, True)
            except ValueError as e:
                raise ValueError("%s: %s" % (sprite_path(sprite_names()[i]), e))
        for i, (idx, alpha) in enumerate(wp):
            try:
                frame_runs(alpha, WPN_W)
                frame_runs(alpha, WPN_W, True)
            except ValueError as e:
                raise ValueError("%s: %s" % (sprite_path(weapon_names()[i]), e))
        hs = huds()
    except (ValueError, OSError) as e:
        sys.exit("pxsart: %s" % e)
    if a.hud:
        text = hud_generate(hs)
        with open(a.hud, "w") as f:
            f.write(text)
        print("pxsart: wrote %s (%d HUD masters)" % (a.hud, len(hs)))
    if a.inks:
        for be, (lit, dark) in ink_tables().items():
            print("%-6s lit  %s" % (be, " ".join("%02X" % v for v in lit)))
            print("%-6s dark %s" % ("", " ".join("%02X" % v for v in dark)))
    ok, lines = criterion(ms)
    rok, rlines = ink_rules()
    sok, slines = spr_criterion(sp, wp, ms)
    if a.preview:
        files = preview(ms, a.preview)
        files += spr_preview(sp, wp, a.preview)
        files += hud_preview(hs, a.preview)
        print("pxsart: wrote %d previews under %s" % (len(files), a.preview))
    if a.check or a.preview:
        for ln in lines + rlines + slines:
            print("pxsart: " + ln)
        if a.check and not ok:
            sys.exit("pxsart: the losable criterion FAILED - re-choose the materials "
                     "(SPEC.md 97.4)")
        if a.check and not rok:
            sys.exit("pxsart: an ink table breaks 97.4's rules (above)")
        if a.check and not sok:
            sys.exit("pxsart: the sprite criterion FAILED (above) - a facing pair "
                     "indistinguishable at 12 columns, or an outline that is not dark "
                     "on a 1bpp set - re-draw the master (SPEC.md 97.4)")
    if a.out:
        text = generate(ms, sp, wp)
        with open(a.out, "w") as f:
            f.write(text)
        print("pxsart: wrote %s (%d lines)" % (a.out, text.count("\n")))
    if a.stream:
        z = stream(ms, sp, wp)
        os.makedirs(os.path.dirname(os.path.abspath(a.stream)), exist_ok=True)
        with open(a.stream, "wb") as f:
            f.write(z)
        print("pxsart: wrote %s (%d bytes packed of %d: %d of walls, %d of sprites)"
              % (a.stream, len(z), NWALL * WALLSZ + NSPR * SPRMSZ + NWPN * WPNMSZ,
                 NWALL * WALLSZ, NSPR * SPRMSZ + NWPN * WPNMSZ))


if __name__ == "__main__":
    main()

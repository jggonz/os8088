#!/usr/bin/env python3
"""PIXELSTEIN 3D's art pipeline (SPEC.md 96.4): the wall masters, the five ink
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
                  and the odd row is the scaler's `ror al, 1` x 2 (96.3)
  Hercules, WIN1  a density ladder - seeded from the nine 8-bit levels of
                  the same source (ff 7f 77 57 55 15 11 01 00, 8..0 dots of
                  eight) and CULLED by --preview: the three dense levels
                  striped under the odd-row phase and their dot-complements
                  took their places (HERC_LEVELS) - picked by Rec.601
                  luminance; the odd row is `ror al, cl`, CL = 3
  C160            the attribute nibble: the colour itself, and a darker
                  twin for the dark face - the byte holds texels u and u+1
                  of ONE ROW (96.4), so the set is built from column pairs
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
NWALL = 15                      # materials 1..15 (0 is open; 96.1)
WALLSZ = TEX * TEX // 2         # 512: 32 rows of 16 bytes, two texels a byte
KEY = 5                         # magenta: the sprite key, never inside art
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

# cgaify.cpp's 21 four-pixel patterns (read for technique): each is the four
# 2-bit pixels of one CGA byte, left to right
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

    THREE RULES the dither and colour backends' tables obey (96.4): a colour
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


# --- the byte-texture set (px_bt_build's layout, 96.4) ----------------------------

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
    """The texel byte as the scaler stores it on `row` (96.3): even rows the
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


# --- the losable criterion (96.4) ---------------------------------------------------

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


# --- the include and the stream ------------------------------------------------------

def generate(ms):
    it = ink_tables()
    blob = art_blob(ms)
    L = []
    w = L.append
    w("; apps/pixelstein/pxart.inc - GENERATED by tools/pxsart.py, DO NOT EDIT")
    w("; (SPEC.md 96.4). The art's numbers and the four ink tables (WIN1's band")
    w("; is the Hercules set); never pixels. The masters go to the machine as")
    w("; the lazy art part's LZ4 stream (build/pxsart.bin, part 4 of 96.9),")
    w("; which the loader expands into PXA_SIZE bytes, and px_bt_build")
    w("; transposes them through these tables into part 3 (pxgen.inc).")
    w("")
    w("PXA_NWALL   equ %d             ; wall masters, material 1 first" % NWALL)
    w("PXA_TEX     equ %d             ; texels a side" % TEX)
    w("PXA_WALLSZ  equ %d            ; 32 rows x 16 bytes: two texels a byte, the" % WALLSZ)
    w("                                ; even column in the high nibble")
    w("PXA_WALL0   equ 0               ; ...from the head of the claim")
    w("PXA_SIZE    equ %d           ; the expanded claim, bytes" % len(blob))
    w("PXA_KB      equ %d              ; ...as OSAPI_MEM_CLAIM wants it" % ((len(blob) + 1023) // 1024))
    w("PXA_SHADES  equ %d              ; lit, dark" % SHADES)
    w("PXA_MATSZ   equ %d           ; a material's bytes in the byte-texture set" % MATSZ)
    w("PXA_BTSIZE  equ %d          ; ...and the whole set (part 3, 96.9)" % BT_SIZE)
    w("PXA_BTKB    equ %d" % ((BT_SIZE + 1023) // 1024))
    w("")
    w("; the ink tables: the texel byte (C160: the attribute nibble, both nibbles")
    w("; built by the transpose) of each of the sixteen colour indices, sixteen")
    w("; lit then sixteen dark, even-row phase (the odd row is the scaler's")
    w("; rotate, 96.3). WIN1's band is the Hercules set")
    for be in BACKENDS:
        lit, dark = it[be]
        w("px_it_%s:" % be)
        w("    db " + ", ".join("0x%02X" % v for v in lit) + "    ; lit")
        w("    db " + ", ".join("0x%02X" % v for v in dark) + "    ; dark")
    w("")
    return "\n".join(L) + "\n"


def stream(ms):
    blob = art_blob(ms)
    z = os88lz.compress(blob, os88lz.LZ4)
    assert os88lz.decompress(z, os88lz.LZ4, len(blob)) == blob, "the LZ4 round trip"
    return z


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", help="write the include here (default: nothing)")
    ap.add_argument("--stream", help="write the LZ4 art stream here")
    ap.add_argument("--placeholder", action="store_true",
                    help="write the procedural masters that are missing")
    ap.add_argument("--force", action="store_true", help="...overwriting those that exist")
    ap.add_argument("--check", action="store_true",
                    help="refuse a bad master in words, and assert the losable criterion")
    ap.add_argument("--preview", metavar="DIR",
                    help="render every master through every ink table into DIR")
    ap.add_argument("--inks", action="store_true", help="print the ink tables")
    a = ap.parse_args()
    if a.placeholder:
        n = write_placeholders(a.force)
        print("pxsart: wrote %d placeholder master(s) under %s" % (n, ART_DIR))
    try:
        ms = masters()
    except (ValueError, OSError) as e:
        sys.exit("pxsart: %s" % e)
    if a.inks:
        for be, (lit, dark) in ink_tables().items():
            print("%-6s lit  %s" % (be, " ".join("%02X" % v for v in lit)))
            print("%-6s dark %s" % ("", " ".join("%02X" % v for v in dark)))
    ok, lines = criterion(ms)
    rok, rlines = ink_rules()
    if a.preview:
        files = preview(ms, a.preview)
        print("pxsart: wrote %d previews under %s" % (len(files), a.preview))
    if a.check or a.preview:
        for ln in lines + rlines:
            print("pxsart: " + ln)
        if a.check and not ok:
            sys.exit("pxsart: the losable criterion FAILED - re-choose the materials "
                     "(SPEC.md 96.4)")
        if a.check and not rok:
            sys.exit("pxsart: an ink table breaks 96.4's rules (above)")
    if a.out:
        text = generate(ms)
        with open(a.out, "w") as f:
            f.write(text)
        print("pxsart: wrote %s (%d lines)" % (a.out, text.count("\n")))
    if a.stream:
        z = stream(ms)
        os.makedirs(os.path.dirname(os.path.abspath(a.stream)), exist_ok=True)
        with open(a.stream, "wb") as f:
            f.write(z)
        print("pxsart: wrote %s (%d bytes packed of %d)" % (a.stream, len(z), NWALL * WALLSZ))


if __name__ == "__main__":
    main()

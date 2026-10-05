#!/usr/bin/env python3
"""pixelsim: PiXEL's image core, in Python (SPEC.md 106.8 - 106.13).

    python3 tools/pixelsim.py --selfcheck          # the build's fast row
    python3 tools/pixelsim.py --gen                # rewrite apps/pixel/pxplans.inc
    python3 tools/pixelsim.py --decode FILE        # what PiXEL makes of a file
    python3 tools/pixelsim.py --render FILE OUT.PNG [--depth 4|1] [--aspect vga|ega|herc|cga]
    python3 tools/pixelsim.py --fsrender FILE OUT.PNG [--fsmode 0..7] [--ordered]

THIS IS THE AUTHORITY THE GUEST IS COMPARED WITH, BYTE FOR BYTE. Every
routine here is the integer arithmetic SPEC.md 106 states, written so that
apps/pixel/*.inc can be read against it line for line: the five simple
formats' header parsers and row decoders (pxsimple.inc), the one row emitter
with its box filter and its Floyd-Steinberg cube quantiser (pxmaster.inc),
the mixing-plan search and the Bayer tables (pxview.inc), the composer under
any zoom, pan and pixel aspect, and the histogram's statistics. Nothing here
is "close enough": tests/pxdecode.py and tests/pxopen.py fail on one byte.

NO THIRD-PARTY LIBRARY. A host without Pillow runs every row; --render writes
its PNG with zlib, which is stdlib.
"""
import argparse
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
PLANS_INC = os.path.join(HERE, "..", "apps", "pixel", "pxplans.inc")
QTAB_INC = os.path.join(HERE, "..", "apps", "pixel", "pxqtab.inc")
GAM_INC = os.path.join(HERE, "..", "apps", "pixel", "pxgam.inc")

# --- the answers (apps/pixel/pxsimple.inc's PXD_*) ---------------------------
PXD_OK, PXD_HEAD, PXD_DIMS, PXD_DEPTH, PXD_PACK, PXD_TRUNC, PXD_DATA, \
    PXD_MEM, PXD_READ, PXD_ORIENT, PXD_NOTYET, PXD_BIG, PXD_NOFILE, \
    PXD_NOTPIC = range(14)
PXD_WORDS = ["", "bad header", "size not valid", "depth not read",
             "packing not read", "cut short", "damaged", "not enough memory",
             "disk read failed", "right-to-left", "not read yet",
             "too big to unpack", "not a file here", "not a picture"]
DIM_MAX = 8192
HEAD_MAX = 2048                 # the bytes a header may be parsed from

# --- the row formats a decoder hands the emitter ------------------------------
RF_IDX, RF_GREY, RF_RGB = 0, 1, 2
# --- palette modes ------------------------------------------------------------
PM_PAL, PM_CUBE, PM_GREY = 0, 1, 2
# --- packings, for the Info panel ---------------------------------------------
PK_NONE, PK_RLE, PK_RLE8, PK_RLE4, PK_BITF, PK_LZW, PK_LZWI, PK_DEFL, \
    PK_DEFLI = range(9)

# os8088's sixteen (apps/os88api.inc, apps/os88img.inc's img_pal8088)
EGA16 = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00), (0x00, 0xAA, 0xAA),
    (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA), (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA),
    (0x55, 0x55, 0x55), (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55), (0xFF, 0xFF, 0xFF),
]

BAYER8 = [
    [0, 32, 8, 40, 2, 34, 10, 42],
    [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44, 4, 36, 14, 46, 6, 38],
    [60, 28, 52, 20, 62, 30, 54, 22],
    [3, 35, 11, 43, 1, 33, 9, 41],
    [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37],
    [63, 31, 55, 23, 61, 29, 53, 21],
]

# --- the cube (SPEC.md 106.8) -------------------------------------------------
R6 = [51 * k for k in range(6)]
R7 = [0, 43, 85, 128, 170, 213, 255]
GNEUT = [0, 1, 2, 4, 5, 6]      # the g level nearest 51k
Q6 = [(5 * v + 127) // 255 for v in range(256)]
Q7 = [(6 * v + 127) // 255 for v in range(256)]


# the four NEUTRAL codes, (k, GNEUT[k], k) for k = 1..4: their entries are the
# exact greys 51k, not (51k, R7[GNEUT[k]], 51k) - so a quantiser that lands on
# one has made a grey with no test of its own (SPEC.md 106.8)
NEUTRAL = [(k * 7 + GNEUT[k]) * 6 + k for k in range(1, 5)]     # 49, 98, 153, 202


def cube_palette():
    pal = []
    for r in range(6):
        for g in range(7):
            for b in range(6):
                pal.append((R6[r], R7[g], R6[b]))
    for k in range(1, 5):
        pal[NEUTRAL[k - 1]] = (51 * k, 51 * k, 51 * k)
    pal += [(51 * k, 51 * k, 51 * k) for k in range(1, 5)]
    assert len(pal) == 256
    return pal


def grey_palette():
    return [(i, i, i) for i in range(256)]


CUBE = cube_palette()
GREY = grey_palette()

# --- aspect (SPEC.md 106.11): a = an / ad, pixel width over height ------------
ASPECT = {"vga": (1, 1), "ega": (35, 48), "herc": (29, 45), "cga": (5, 12)}
ZSTEPS = [8192, 10923, 16384, 21845, 32768, 43691, 65536,
          131072, 196608, 262144, 393216, 524288]


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def u32(b, o):
    return u16(b, o) | (u16(b, o + 2) << 16)


def s32(b, o):
    v = u32(b, o)
    return v - (1 << 32) if v & 0x80000000 else v


class Refused(Exception):
    def __init__(self, code):
        Exception.__init__(self, PXD_WORDS[code])
        self.code = code


class Reader:
    """The ring, as the decoder sees it: bytes in order from offset 0."""

    def __init__(self, data):
        self.d = data
        self.p = 0

    def skip_to(self, off):
        if off > len(self.d):
            raise Refused(PXD_TRUNC)
        if off > self.p:
            self.p = off

    def byte(self):
        if self.p >= len(self.d):
            raise Refused(PXD_TRUNC)
        v = self.d[self.p]
        self.p += 1
        return v

    def byte_or_end(self):
        if self.p >= len(self.d):
            return -1
        v = self.d[self.p]
        self.p += 1
        return v

    def take(self, n):
        if self.p + n > len(self.d):
            raise Refused(PXD_TRUNC)
        v = self.d[self.p:self.p + n]
        self.p += n
        return v


class Pic:
    """What a header parse says, and the rows a decode emits."""

    def __init__(self):
        self.fmt = ""
        self.w = self.h = 0
        self.rf = RF_IDX
        self.pal = []           # the SOURCE palette (RF_IDX), up to 256
        self.pack = PK_NONE
        self.bits = 0
        self.rows = []          # (y, bytes) in emission order
        self.npal = 0
        self.par = None         # an INTERLACED picture: below 1/1 only the
                                # rows of this parity are summed (106.18)


# =============================================================================
# THE SNIFF (SPEC.md 106.6): the content names the format
# =============================================================================
SNIFF_EXT = {"JPG": "JPEG", "JPE": "JPEG", "PNG": "PNG", "GIF": "GIF",
             "BMP": "BMP", "PCX": "PCX", "TIF": "TIFF", "TGA": "TGA",
             "PIX": "PIX", "PBM": "PNM", "PGM": "PNM", "PPM": "PNM",
             "PNM": "PNM", "ICO": "ICO", "CUR": "ICO", "LBM": "LBM",
             "IFF": "LBM", "MAC": "MAC"}


def sniff(data, ext):
    """The EXTENSION first, then - on a file of 32 bytes or more - the
    CONTENT, which wins, in the order SPEC.md 106.25 says it (the guest's
    px_extfmt and px_sniffbuf)."""
    fmt = SNIFF_EXT.get(ext, "")
    if len(data) < 32:
        return fmt
    h = data[:69]
    if h[0] == 0xFF and h[1] == 0xD8 and h[2] == 0xFF:
        return "JPEG"
    if h[:4] == b"\x89PNG":
        return "PNG"
    if h[:4] == b"GIF8":
        return "GIF"
    if h[:2] == b"BM":
        return "BMP"
    if h[0] == 0x0A and h[2] == 1 and fmt == "PCX":
        return "PCX"
    if h[:4] in (b"II*\x00", b"MM\x00*"):
        return "TIFF"
    if h[:4] == b"O8PI":
        return "PIX"
    if h[0:1] == b"P" and 0x31 <= h[1] <= 0x36:
        return "PNM"
    if h[:4] == b"FORM" and h[8:12] in (b"ILBM", b"PBM "):
        return "LBM"
    if h[0] == 0 and h[1] == 0 and u16(h, 2) in (1, 2) and \
            1 <= u16(h, 4) <= 255 and h[9] == 0:
        return "ICO"
    if len(h) >= 69 and h[0] == 0 and h[65:69] == b"PNTG":
        return "MAC"
    if h[:2] == b"CZ":
        return "CZ"
    return fmt


# =============================================================================
# BMP
# =============================================================================
def _maskinfo(m):
    if m == 0:
        raise Refused(PXD_DEPTH)
    sh = 0
    while not (m >> sh) & 1:
        sh += 1
    wid = 0
    while (m >> (sh + wid)) & 1:
        wid += 1
    if (m >> (sh + wid)) != 0 or wid > 8:
        raise Refused(PXD_DEPTH)
    mx = (1 << wid) - 1
    lut = [(v * 255 + (mx >> 1)) // mx for v in range(mx + 1)]
    return sh, mx, lut


def bmp_header(head, fsz):
    p = Pic()
    p.fmt = "BMP"
    if len(head) < 26:
        raise Refused(PXD_HEAD)
    off = u32(head, 10)
    hsz = u32(head, 14)
    if hsz == 12:
        w, h = u16(head, 18), u16(head, 20)
        planes, bpp = u16(head, 22), u16(head, 24)
        comp, used, esz, palofs = 0, 0, 3, 26
    elif hsz in (40, 52, 56, 108, 124):
        if len(head) < 54:
            raise Refused(PXD_HEAD)
        w, h = s32(head, 18), s32(head, 22)
        planes, bpp = u16(head, 26), u16(head, 28)
        comp, used, esz = u32(head, 30), u32(head, 46), 4
        palofs = 14 + hsz
    else:
        raise Refused(PXD_HEAD)
    topdown = h < 0
    if h < 0:
        h = -h
    if w <= 0 or h <= 0 or w > DIM_MAX or h > DIM_MAX:
        raise Refused(PXD_DIMS)
    if planes != 1:
        raise Refused(PXD_HEAD)
    if bpp not in (1, 4, 8, 16, 24, 32):
        raise Refused(PXD_DEPTH)
    masks = None
    if comp == 0:
        pass
    elif comp == 1:
        if bpp != 8 or topdown:
            raise Refused(PXD_PACK)
    elif comp == 2:
        if bpp != 4 or topdown:
            raise Refused(PXD_PACK)
    elif comp in (3, 6):
        if bpp not in (16, 32):
            raise Refused(PXD_PACK)
        if hsz == 40:
            if len(head) < 66:
                raise Refused(PXD_HEAD)
            palofs += 12 if comp == 3 else 16
        masks = (u32(head, 54), u32(head, 58), u32(head, 62))
    else:
        raise Refused(PXD_PACK)
    p.w, p.h, p.bits = w, h, bpp
    p.pack = {0: PK_NONE, 1: PK_RLE8, 2: PK_RLE4}.get(comp, PK_BITF)
    if bpp <= 8:
        n = used if used else (1 << bpp)
        if n > 256:
            raise Refused(PXD_HEAD)
        if palofs + n * esz > len(head):
            raise Refused(PXD_HEAD)
        p.pal = [(head[palofs + i * esz + 2], head[palofs + i * esz + 1],
                  head[palofs + i * esz]) for i in range(n)]
        p.rf = RF_IDX
    else:
        p.rf = RF_RGB
        if masks is None:
            masks = (0x7C00, 0x03E0, 0x001F) if bpp == 16 else \
                (0xFF0000, 0x00FF00, 0x0000FF)
        p.masks = [_maskinfo(m) for m in masks]
    p.off = off
    p.topdown = topdown
    p.comp = comp
    return p


def bmp_rows(p, rd):
    rd.skip_to(p.off)
    w, h = p.w, p.h
    if p.comp in (1, 2):
        return bmp_rle(p, rd)
    stride = ((w * p.bits + 31) // 32) * 4
    for i in range(h):
        raw = rd.take(stride)
        y = i if p.topdown else h - 1 - i
        if p.bits <= 8:
            row = unpack_bits(raw, w, p.bits)
        elif p.bits == 24:
            row = bytearray(3 * w)
            for x in range(w):
                row[3 * x] = raw[3 * x + 2]
                row[3 * x + 1] = raw[3 * x + 1]
                row[3 * x + 2] = raw[3 * x]
        else:
            nb = p.bits // 8
            row = bytearray(3 * w)
            for x in range(w):
                v = int.from_bytes(raw[nb * x:nb * x + nb], "little")
                for c in range(3):
                    sh, mx, lut = p.masks[c]
                    row[3 * x + c] = lut[(v >> sh) & mx]
        p.rows.append((y, bytes(row)))


def unpack_bits(raw, w, bpp):
    row = bytearray(w)
    if bpp == 8:
        row[:] = raw[:w]
    elif bpp == 4:
        for x in range(w):
            b = raw[x >> 1]
            row[x] = (b >> 4) if not x & 1 else (b & 15)
    else:
        for x in range(w):
            row[x] = (raw[x >> 3] >> (7 - (x & 7))) & 1
    return row


def bmp_rle(p, rd):
    w, h = p.w, p.h
    rle4 = p.comp == 2
    i = 0
    x = 0
    row = bytearray(w)

    def put(v):
        nonlocal x
        if x < w:
            row[x] = v
        x += 1

    def flush():
        nonlocal i, x, row
        p.rows.append((h - 1 - i, bytes(row)))
        i += 1
        x = 0
        row = bytearray(w)

    while i < h:
        a = rd.byte()
        b = rd.byte()
        if a:
            for k in range(a):
                put(((b >> 4) if not k & 1 else (b & 15)) if rle4 else b)
        elif b == 0:
            flush()
        elif b == 1:
            while i < h:
                flush()
        elif b == 2:
            dx = rd.byte()
            dy = rd.byte()
            x += dx
            for _ in range(dy):
                if i >= h:
                    break
                keep = x
                flush()
                x = keep
        else:
            if rle4:
                nby = (b + 1) >> 1
                data = rd.take(nby)
                for k in range(b):
                    v = data[k >> 1]
                    put((v >> 4) if not k & 1 else (v & 15))
                if nby & 1:
                    rd.byte()
            else:
                data = rd.take(b)
                for v in data:
                    put(v)
                if b & 1:
                    rd.byte()


# =============================================================================
# PCX
# =============================================================================
def pcx_header(head, fsz, tail):
    p = Pic()
    p.fmt = "PCX"
    if len(head) < 128:
        raise Refused(PXD_HEAD)
    if head[2] != 1:
        raise Refused(PXD_PACK)
    ver, bpp = head[1], head[3]
    xmin, ymin, xmax, ymax = (u16(head, 4), u16(head, 6), u16(head, 8),
                              u16(head, 10))
    npl, bpl = head[65], u16(head, 66)
    w, h = xmax - xmin + 1, ymax - ymin + 1
    if w <= 0 or h <= 0 or w > DIM_MAX or h > DIM_MAX:
        raise Refused(PXD_DIMS)
    if (bpp, npl) not in ((1, 1), (1, 4), (8, 1), (8, 3)):
        raise Refused(PXD_DEPTH)
    if bpl < (w * bpp + 7) // 8 or bpl > 16384:
        raise Refused(PXD_HEAD)
    p.w, p.h, p.bits, p.pack = w, h, bpp * npl, PK_RLE
    p.bpp, p.npl, p.bpl = bpp, npl, bpl
    if (bpp, npl) == (1, 1):
        p.rf, p.pal = RF_IDX, [(0, 0, 0), (255, 255, 255)]
    elif (bpp, npl) == (1, 4):
        p.rf = RF_IDX
        if ver == 3:
            p.pal = list(EGA16)
        else:
            p.pal = [(head[16 + 3 * i], head[17 + 3 * i], head[18 + 3 * i])
                     for i in range(16)]
    elif npl == 1:
        if tail is not None and len(tail) == 769 and tail[0] == 0x0C \
                and fsz >= 128 + 769:
            p.rf = RF_IDX
            p.pal = [(tail[1 + 3 * i], tail[2 + 3 * i], tail[3 + 3 * i])
                     for i in range(256)]
        else:
            p.rf = RF_GREY
    else:
        p.rf = RF_RGB
    p.off = 128
    return p


def pcx_rows(p, rd):
    rd.skip_to(p.off)
    w, h, bpl, npl = p.w, p.h, p.bpl, p.npl
    n = bpl * npl
    line = bytearray(n)
    cnt, val = 0, 0
    for y in range(h):
        k = 0
        while k < n:
            if cnt == 0:
                c = rd.byte()
                if (c & 0xC0) == 0xC0:
                    cnt = c & 0x3F
                    val = rd.byte()
                    continue
                cnt, val = 1, c
            line[k] = val
            k += 1
            cnt -= 1
        if (p.bpp, npl) == (1, 1):
            row = unpack_bits(line, w, 1)
        elif (p.bpp, npl) == (1, 4):
            row = bytearray(w)
            for x in range(w):
                v = 0
                for pl in range(4):
                    v |= ((line[pl * bpl + (x >> 3)] >> (7 - (x & 7))) & 1) << pl
                row[x] = v
        elif npl == 1:
            row = bytes(line[:w])
        else:
            row = bytearray(3 * w)
            for x in range(w):
                row[3 * x] = line[x]
                row[3 * x + 1] = line[bpl + x]
                row[3 * x + 2] = line[2 * bpl + x]
        p.rows.append((y, bytes(row)))


# =============================================================================
# TGA
# =============================================================================
LUT5 = [(v * 255 + 15) // 31 for v in range(32)]


def tga_header(head, fsz):
    p = Pic()
    p.fmt = "TGA"
    if len(head) < 18:
        raise Refused(PXD_HEAD)
    idl, cmt, it = head[0], head[1], head[2]
    cmf, cml, cmb = u16(head, 3), u16(head, 5), head[7]
    w, h, pd, desc = u16(head, 12), u16(head, 14), head[16], head[17]
    if it not in (1, 2, 3, 9, 10, 11) or cmt > 1:
        raise Refused(PXD_HEAD)
    if w == 0 or h == 0 or w > DIM_MAX or h > DIM_MAX:
        raise Refused(PXD_DIMS)
    if desc & 0x10:
        raise Refused(PXD_ORIENT)
    cmbytes = 0
    if cmt:
        if cmb not in (15, 16, 24, 32):
            raise Refused(PXD_HEAD)
        cmbytes = (cmb + 7) // 8
    base = it & 7
    if base == 1:
        if not cmt:
            raise Refused(PXD_HEAD)
        if pd != 8:
            raise Refused(PXD_DEPTH)
        if cmf + cml > 256:
            raise Refused(PXD_HEAD)
        po = 18 + idl
        if po + cml * cmbytes > len(head):
            raise Refused(PXD_HEAD)
        pal = [(0, 0, 0)] * 256
        for j in range(cml):
            e = head[po + j * cmbytes:po + (j + 1) * cmbytes]
            if cmbytes == 2:
                v = e[0] | (e[1] << 8)
                pal[cmf + j] = (LUT5[(v >> 10) & 31], LUT5[(v >> 5) & 31],
                                LUT5[v & 31])
            else:
                pal[cmf + j] = (e[2], e[1], e[0])
        p.pal = pal[:cmf + cml] if cmf + cml else [(0, 0, 0)]
        p.rf = RF_IDX
    elif base == 2:
        if pd not in (15, 16, 24, 32):
            raise Refused(PXD_DEPTH)
        p.rf = RF_RGB
    else:
        if pd != 8:
            raise Refused(PXD_DEPTH)
        p.rf = RF_GREY
    p.w, p.h, p.bits = w, h, pd
    p.pack = PK_RLE if it & 8 else PK_NONE
    p.off = 18 + idl + cml * cmbytes
    p.topdown = bool(desc & 0x20)
    p.psz = (pd + 7) // 8
    p.rle = bool(it & 8)
    return p


def tga_rows(p, rd):
    rd.skip_to(p.off)
    w, h, psz = p.w, p.h, p.psz
    cnt, raw, val = 0, False, b""
    for i in range(h):
        y = i if p.topdown else h - 1 - i
        out = bytearray(3 * w if p.rf == RF_RGB else w)
        for x in range(w):
            if p.rle:
                if cnt == 0:
                    c = rd.byte()
                    cnt = (c & 0x7F) + 1
                    raw = not c & 0x80
                    if not raw:
                        val = rd.take(psz)
                pix = rd.take(psz) if raw else val
                cnt -= 1
            else:
                pix = rd.take(psz)
            if p.rf == RF_RGB:
                if psz == 2:
                    v = pix[0] | (pix[1] << 8)
                    out[3 * x:3 * x + 3] = bytes((LUT5[(v >> 10) & 31],
                                                  LUT5[(v >> 5) & 31],
                                                  LUT5[v & 31]))
                else:
                    out[3 * x:3 * x + 3] = bytes((pix[2], pix[1], pix[0]))
            else:
                out[x] = pix[0]
        p.rows.append((y, bytes(out)))


# =============================================================================
# PNM
# =============================================================================
WS = b" \t\n\r\x0b\x0c"


class PnmTok:
    """Tokens: whitespace and #-comments between, digits within. The getter
    answers -1 at the end; running out between tokens is the caller's
    `eof` refusal, running out inside a number ends the number."""

    def __init__(self, get, eof):
        self.get = get
        self.eof = eof

    def skipws(self):
        while True:
            c = self.get()
            if c < 0:
                raise Refused(self.eof)
            if c == 0x23:       # '#': to the end of the line
                while c not in (0x0A, 0x0D):
                    c = self.get()
                    if c < 0:
                        raise Refused(self.eof)
                continue
            if c in WS:
                continue
            return c

    def number(self, err):
        c = self.skipws()
        if not 0x30 <= c <= 0x39:
            raise Refused(err)
        v = 0
        while 0x30 <= c <= 0x39:
            v = v * 10 + c - 0x30
            if v > 65535:
                raise Refused(err)
            c = self.get()
        return v, c             # c: the byte that ended it, -1 at the end


def pnm_header(head, fsz):
    p = Pic()
    p.fmt = "PNM"
    pos = [2]

    def get():
        if pos[0] >= min(len(head), HEAD_MAX):
            return -1
        c = head[pos[0]]
        pos[0] += 1
        return c

    kind = head[1] - 0x30
    t = PnmTok(get, PXD_HEAD)
    w, _ = t.number(PXD_DIMS)
    h, end = t.number(PXD_DIMS)
    if w == 0 or h == 0 or w > DIM_MAX or h > DIM_MAX:
        raise Refused(PXD_DIMS)
    mx = 1
    if kind not in (1, 4):
        mx, end = t.number(PXD_HEAD)
        if mx == 0:
            raise Refused(PXD_HEAD)
    if end not in WS:
        raise Refused(PXD_HEAD)
    p.w, p.h, p.kind, p.maxv = w, h, kind, mx
    p.off = pos[0]
    if kind in (1, 4):
        p.rf, p.pal, p.bits = RF_IDX, [(255, 255, 255), (0, 0, 0)], 1
    elif kind in (2, 5):
        p.rf, p.bits = RF_GREY, 8 if mx < 256 else 16
    else:
        p.rf, p.bits = RF_RGB, 24 if mx < 256 else 48
    return p


def pnm_scale(v, mx):
    if v > mx:
        v = mx
    return (v * 255 + (mx >> 1)) // mx


def pnm_rows(p, rd):
    rd.skip_to(p.off)
    w, h, k, mx = p.w, p.h, p.kind, p.maxv
    nch = 3 if k in (3, 6) else 1
    t = PnmTok(rd.byte_or_end, PXD_TRUNC)
    for y in range(h):
        out = bytearray(w * nch)
        if k == 1:
            for x in range(w):
                c = t.skipws()
                if c not in (0x30, 0x31):
                    raise Refused(PXD_DATA)
                out[x] = c - 0x30
        elif k == 4:
            out = unpack_bits(rd.take((w + 7) // 8), w, 1)
        elif k in (2, 3):
            for j in range(w * nch):
                v, _ = t.number(PXD_DATA)
                out[j] = pnm_scale(v, mx)
        else:
            if mx < 256:
                raw = rd.take(w * nch)
                for j in range(w * nch):
                    out[j] = pnm_scale(raw[j], mx)
            else:
                raw = rd.take(2 * w * nch)
                for j in range(w * nch):
                    out[j] = pnm_scale((raw[2 * j] << 8) | raw[2 * j + 1], mx)
        p.rows.append((y, bytes(out)))


# =============================================================================
# PIX (SPEC.md 61.7)
# =============================================================================
def pix_header(head, fsz):
    p = Pic()
    p.fmt = "PIX"
    if len(head) < 32 or head[5] != 1 or u16(head, 6) == 0 \
            or u16(head, 10) != 16:
        raise Refused(PXD_HEAD)
    w, h, stride, off = u16(head, 18), u16(head, 20), u16(head, 22), \
        u32(head, 24)
    if w == 0 or h == 0 or w > DIM_MAX or h > DIM_MAX:
        raise Refused(PXD_DIMS)
    if stride < (w + 1) // 2:
        raise Refused(PXD_HEAD)
    p.w, p.h, p.bits, p.rf, p.pal = w, h, 4, RF_IDX, list(EGA16)
    p.off, p.stride = off, stride
    return p


def pix_rows(p, rd):
    rd.skip_to(p.off)
    for y in range(p.h):
        p.rows.append((y, bytes(unpack_bits(rd.take(p.stride), p.w, 4))))


# =============================================================================
# GIF (SPEC.md 106.18): the logical screen is the picture, the FIRST image is
# placed in it, and the rest of the screen is the background index
# =============================================================================
BG = 85                         # the view background a transparent pixel
                                # shows: the canvas's own dark grey


def blend(a, c):
    """c over the view background at alpha a (0..255), SPEC.md 106.18."""
    t = a * c + (255 - a) * BG + 128
    return (t + (t >> 8)) >> 8


def gif_header(head, fsz):
    if len(head) < 13:
        raise Refused(PXD_HEAD)
    w, h = u16(head, 6), u16(head, 8)
    if not (1 <= w <= DIM_MAX and 1 <= h <= DIM_MAX):
        raise Refused(PXD_DIMS)
    p = Pic()
    p.fmt, p.w, p.h, p.rf, p.pack = "GIF", w, h, RF_IDX, PK_LZW
    pk = head[10]
    p.bits = (pk & 7) + 1 if pk & 0x80 else 8
    p.npal = (2 << (pk & 7)) if pk & 0x80 else 0
    return p


GIF_START = (0, 4, 2, 1)
GIF_STEP = (8, 8, 4, 2)


def gif_frame_rows(fh, ilace):
    """The frame's rows in the order the stream carries them."""
    if not ilace:
        return list(range(fh))
    out = []
    for k in range(4):
        out += list(range(GIF_START[k], fh, GIF_STEP[k]))
    return out


def lzw_decode(flat, mincode, run):
    """os88lzw.inc in Python: GIF's LZW over the flat code bytes. run(bytes)
    answers True to stop. 'EOI', 'END' (the input ran out) or 'STOP'; a code
    the table never built is PXD_DATA."""
    if not 2 <= mincode <= 8:
        raise Refused(PXD_DATA)
    nbits = 8 * len(flat)
    pos = 0
    clr = 1 << mincode
    eoi = clr + 1
    prefix = [0] * 4096
    suffix = [0] * 4096
    free = cs = 0
    old = None
    first = 0

    def reset():
        return clr + 2, mincode + 1, None
    free, cs, old = reset()
    while True:
        if pos + cs > nbits:
            return "END"
        o = pos >> 3
        v = int.from_bytes(bytes(flat[o:o + 3]) + b"\0\0\0", "little")
        c = (v >> (pos & 7)) & ((1 << cs) - 1)
        pos += cs
        if c == clr:
            free, cs, old = reset()
            continue
        if c == eoi:
            return "EOI"
        if old is None:
            if c >= clr:
                raise Refused(PXD_DATA)
            old = first = c
            if run(bytes((c,))):
                return "STOP"
            continue
        if c > free:
            raise Refused(PXD_DATA)
        out = []
        k = c
        if c == free:
            out.append(first)
            k = old
        while k >= clr:
            out.append(suffix[k])
            k = prefix[k]
        out.append(k)
        first = k
        out.reverse()
        if run(bytes(out)):
            return "STOP"
        if free < 4096 and old < free:
            prefix[free] = old
            suffix[free] = first
            free += 1
            if cs < 12 and free >= (1 << cs):
                cs += 1
        old = c


def gif_rows(p, rd):
    hdr = rd.take(13)
    pk = hdr[10]
    gpal = rd.take(3 * (2 << (pk & 7))) if pk & 0x80 else None
    trans = None
    while True:                         # the blocks before the first image
        b = rd.byte()
        if b == 0x2C:
            break
        if b != 0x21:
            raise Refused(PXD_DATA)
        label = rd.byte()
        while True:
            n = rd.byte()
            if n == 0:
                break
            blk = rd.take(n)
            if label == 0xF9 and n >= 4:
                trans = blk[3] if blk[0] & 1 else None
    d = rd.take(9)
    left, top, fw, fh, fpk = u16(d, 0), u16(d, 2), u16(d, 4), u16(d, 6), d[8]
    if fw == 0 or fh == 0:
        raise Refused(PXD_DATA)
    if fpk & 0x80:
        n = 2 << (fpk & 7)
        raw = rd.take(3 * n)
        p.bits = (fpk & 7) + 1
    elif gpal is not None:
        raw = gpal
        n = len(raw) // 3
    else:
        raise Refused(PXD_DATA)
    p.npal = n
    pal = [tuple(raw[3 * i:3 * i + 3]) for i in range(n)]
    pal += [(0, 0, 0)] * (256 - n)
    if trans is not None:
        pal[trans] = (BG, BG, BG)
        p.npal = max(n, trans + 1)      # a key past the table is still a
    p.pal = pal                         # colour the plans must cover
    bgi = trans if trans is not None else hdr[11]
    ilace = bool(fpk & 0x40)
    if ilace:
        p.pack = PK_LZWI
        p.par = (top + 1) & 1
    mincode = rd.byte()
    flat = bytearray()                  # the sub-blocks, joined: the code
    while True:                         # stream ends at the terminator or the
        n = rd.byte_or_end()            # file's end, and is never an error
        if n <= 0:
            break
        got = rd.d[rd.p:rd.p + n]
        rd.p += len(got)
        flat += got
        if len(got) < n:
            break
    w, h = p.w, p.h
    order = gif_frame_rows(fh, ilace)
    rows = {}
    st = {"i": 0, "col": 0, "row": bytearray([bgi]) * w}

    def place(s):
        k = 0
        while k < len(s):
            take = min(fw - st["col"], len(s) - k)
            x = left + st["col"]
            if x < w:
                keep = min(take, w - x)
                st["row"][x:x + keep] = s[k:k + keep]
            st["col"] += take
            k += take
            if st["col"] == fw:
                y = top + order[st["i"]]
                if y < h:
                    rows[y] = st["row"]
                st["i"] += 1
                st["col"] = 0
                st["row"] = bytearray([bgi]) * w
                if st["i"] == fh:
                    return True
        return False
    end = lzw_decode(flat, mincode, place)
    if end != "STOP":
        raise Refused(PXD_TRUNC)
    out = []
    for y in range(min(top, h)):
        out.append((y, bytes([bgi]) * w))
    for fy in order:
        y = top + fy
        if y < h:
            out.append((y, bytes(rows[y])))
    for y in range(top + fh, h):
        out.append((y, bytes([bgi]) * w))
    p.rows = out


# =============================================================================
# PNG (SPEC.md 106.18): chunks, zlib, inflate, the five filters, Adam7
# =============================================================================
PNG_DEPTHS = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16),
              6: (8, 16)}
PNG_CHAN = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
PNG_ROWMAX = 32760              # two rows and their guards in one segment
ADAM7 = ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
         (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2))      # x0, y0, dx, dy


def be32(b, o):
    return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]


def png_header(head, fsz):
    if len(head) < 33 or head[8:16] != b"\0\0\0\x0dIHDR":
        raise Refused(PXD_HEAD)
    w, h = be32(head, 16), be32(head, 20)
    if not (1 <= w <= DIM_MAX and 1 <= h <= DIM_MAX):
        raise Refused(PXD_DIMS)
    depth, ct, comp, filt, il = head[24:29]
    if ct not in PNG_DEPTHS or depth not in PNG_DEPTHS[ct]:
        raise Refused(PXD_DEPTH)
    if comp or filt or il > 1:
        raise Refused(PXD_PACK)
    rowb = (w * depth * PNG_CHAN[ct] + 7) // 8
    if rowb > PNG_ROWMAX:
        raise Refused(PXD_BIG)
    p = Pic()
    p.fmt, p.w, p.h = "PNG", w, h
    p.rf = RF_IDX if ct == 3 else (RF_GREY if ct in (0, 4) else RF_RGB)
    p.bits = depth * PNG_CHAN[ct]
    p.pack = PK_DEFLI if il else PK_DEFL
    p.ct, p.depth, p.il = ct, depth, il
    return p


class IdatBits:
    """The IDAT stream, LSB first. A read past its end is PXD_TRUNC: the
    guest reads zeros there, and says 'cut short' at the first use of one."""

    def __init__(self, rd):
        self.rd = rd
        self.left = 0
        self.end = False
        self.bb = 0
        self.bn = 0

    def _byte(self):
        rd = self.rd
        while self.left == 0:
            if self.end:
                return -1
            if self.started:            # the CRC, then the next chunk
                if rd.p + 4 > len(rd.d):
                    self.end = True
                    return -1
                rd.p += 4
                if rd.p + 8 > len(rd.d):
                    self.end = True
                    return -1
                ln = be32(rd.d, rd.p)
                if rd.d[rd.p + 4:rd.p + 8] != b"IDAT" or ln >= 0x80000000:
                    self.end = True
                    return -1
                rd.p += 8
                self.left = ln
            self.started = True
        if rd.p >= len(rd.d):
            self.end = True
            self.left = 0
            return -1
        self.left -= 1
        v = rd.d[rd.p]
        rd.p += 1
        return v

    def bits(self, n):
        while self.bn < n:
            b = self._byte()
            if b < 0:
                raise Refused(PXD_TRUNC)
            self.bb |= b << self.bn
            self.bn += 8
        v = self.bb & ((1 << n) - 1)
        self.bb >>= n
        self.bn -= n
        return v

    def align(self):
        k = self.bn & 7
        self.bb >>= k
        self.bn -= k


LBASE = (3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51,
         59, 67, 83, 99, 115, 131, 163, 195, 227, 258)
LEXT = (0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
        5, 5, 5, 5, 0)
DBASE = (1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
         513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385,
         24577)
DEXT = (0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10,
        10, 11, 11, 12, 12, 13, 13)
CLORD = (16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15)


def huff(lens, complete):
    """Canonical code from lengths: (counts, symbols), or PXD_DATA when the
    set is over-subscribed, or incomplete where that is not allowed - an
    incomplete set is allowed only empty or as one code of length 1, and
    never for the code-length code (zlib's inftrees rule)."""
    cnt = [0] * 16
    for v in lens:
        cnt[v] += 1
    cnt[0] = 0
    left = 1
    for n in range(1, 16):
        left = 2 * left - cnt[n]
        if left < 0:
            raise Refused(PXD_DATA)
    used = sum(cnt)
    if left > 0 and (complete or not (used == 0 or (used == 1 and cnt[1] == 1))):
        raise Refused(PXD_DATA)
    syms = [s for n in range(1, 16) for s in range(len(lens)) if lens[s] == n]
    return cnt, syms


def hdecode(br, h):
    cnt, syms = h
    code = first = index = 0
    for n in range(1, 16):
        code |= br.bits(1)
        c = cnt[n]
        if code - first < c:
            return syms[index + code - first]
        index += c
        first = (first + c) << 1
        code <<= 1
    raise Refused(PXD_DATA)


FIXED_L = None


def inflate(br, need):
    """Up to `need` bytes of the stream: decoding stops after the symbol
    that reaches it, exactly as the guest's extraction does."""
    global FIXED_L
    cmf, flg = br.bits(8), br.bits(8)
    if (cmf & 15) != 8 or (cmf >> 4) > 7 or ((cmf << 8) | flg) % 31 or flg & 32:
        raise Refused(PXD_DATA)
    out = bytearray()
    while True:
        final = br.bits(1)
        bt = br.bits(2)
        if bt == 0:
            br.align()
            ln = br.bits(16)
            nl = br.bits(16)
            if ln != nl ^ 0xFFFF:
                raise Refused(PXD_DATA)
            while ln:
                out.append(br.bits(8))
                ln -= 1
                if len(out) >= need:
                    return out
        elif bt == 3:
            raise Refused(PXD_DATA)
        else:
            if bt == 1:
                if FIXED_L is None:
                    FIXED_L = (huff([8] * 144 + [9] * 112 + [7] * 24 + [8] * 8,
                                    False), huff([5] * 32, False))
                lh, dh = FIXED_L
            else:
                nlen = br.bits(5) + 257
                ndist = br.bits(5) + 1
                ncode = br.bits(4) + 4
                if nlen > 286 or ndist > 30:
                    raise Refused(PXD_DATA)
                cl = [0] * 19
                for i in range(ncode):
                    cl[CLORD[i]] = br.bits(3)
                ch = huff(cl, True)
                lens = []
                while len(lens) < nlen + ndist:
                    s = hdecode(br, ch)
                    if s < 16:
                        lens.append(s)
                        continue
                    if s == 16:
                        if not lens:
                            raise Refused(PXD_DATA)
                        v, r = lens[-1], 3 + br.bits(2)
                    elif s == 17:
                        v, r = 0, 3 + br.bits(3)
                    else:
                        v, r = 0, 11 + br.bits(7)
                    if len(lens) + r > nlen + ndist:
                        raise Refused(PXD_DATA)
                    lens += [v] * r
                if lens[256] == 0:
                    raise Refused(PXD_DATA)
                lh = huff(lens[:nlen], False)
                dh = huff(lens[nlen:], False)
            while True:
                s = hdecode(br, lh)
                if s < 256:
                    out.append(s)
                    if len(out) >= need:
                        return out
                    continue
                if s == 256:
                    break
                s -= 257
                if s >= 29:
                    raise Refused(PXD_DATA)
                ln = LBASE[s] + br.bits(LEXT[s])
                d = hdecode(br, dh)
                if d >= 30:
                    raise Refused(PXD_DATA)
                dist = DBASE[d] + br.bits(DEXT[d])
                if dist > len(out):
                    raise Refused(PXD_DATA)
                for _ in range(ln):
                    out.append(out[-dist])
                if len(out) >= need:
                    return out
        if final:
            raise Refused(PXD_TRUNC)    # the stream ended before the rows


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def unfilter(ft, cur, prev, bpp):
    n = len(cur)
    if ft == 0:
        return
    for i in range(n):
        a = cur[i - bpp] if i >= bpp else 0
        b = prev[i]
        c = prev[i - bpp] if i >= bpp else 0
        if ft == 1:
            v = a
        elif ft == 2:
            v = b
        elif ft == 3:
            v = (a + b) >> 1
        elif ft == 4:
            v = paeth(a, b, c)
        else:
            raise Refused(PXD_DATA)
        cur[i] = (cur[i] + v) & 255


def png_pixels(p, raw, n):
    """n pixels of an unfiltered scanline, in the emitter's row format."""
    ct, d = p.ct, p.depth
    if ct == 3 or (ct == 0 and d < 8):
        vals = unpack_bits(raw, n, d) if d < 8 else list(raw[:n])
        if ct == 3:
            return bytes(vals)
        scale = 255 // ((1 << d) - 1)
        return bytes(BG if v == p.tgrey else v * scale for v in vals)
    out = bytearray()
    if ct == 0:
        for i in range(n):
            if d == 16:
                v = (raw[2 * i] << 8) | raw[2 * i + 1]
                out.append(BG if v == p.tgrey else raw[2 * i])
            else:
                out.append(BG if raw[i] == p.tgrey else raw[i])
    elif ct == 4:
        st = 2 if d == 8 else 4
        for i in range(n):
            g, a = raw[st * i], raw[st * i + st // 2]
            out.append(blend(a, g))
    elif ct == 2:
        st = 3 if d == 8 else 6
        for i in range(n):
            px = raw[st * i:st * i + st]
            if d == 16:
                key = ((px[0] << 8) | px[1], (px[2] << 8) | px[3],
                       (px[4] << 8) | px[5])
                rgb = (px[0], px[2], px[4])
            else:
                key = rgb = (px[0], px[1], px[2])
            out += bytes((BG, BG, BG)) if key == p.trgb else bytes(rgb)
    else:
        st = 4 if d == 8 else 8
        h = st // 4
        for i in range(n):
            px = raw[st * i:st * i + st]
            a = px[3 * h]
            out += bytes((blend(a, px[0]), blend(a, px[h]), blend(a, px[2 * h])))
    return bytes(out)


def unpack_bits(raw, n, d):
    out = []
    for i in range(n):
        bit = i * d
        out.append((raw[bit >> 3] >> (8 - d - (bit & 7))) & ((1 << d) - 1))
    return out


def png_rows(p, rd):
    rd.skip_to(8)
    pal = None
    trns = None
    while True:                         # the chunks before the first IDAT
        hd = rd.take(8)
        ln, typ = be32(hd, 0), bytes(hd[4:8])
        if ln >= 0x80000000:
            raise Refused(PXD_DATA)
        if typ == b"IDAT":
            break
        if typ == b"IEND":
            raise Refused(PXD_DATA)
        if typ == b"PLTE" and p.ct == 3:
            if ln == 0 or ln % 3 or ln > 768:
                raise Refused(PXD_DATA)
            pal = rd.take(ln)
            rd.take(4)
        elif typ == b"tRNS" and ln <= 256:
            trns = rd.take(ln)
            rd.take(4)
        else:
            rd.take(ln)
            rd.take(4)
    p.tgrey = -1
    p.trgb = None
    if p.ct == 3:
        if pal is None:
            raise Refused(PXD_DATA)
        n = len(pal) // 3
        p.npal = n
        pl = [tuple(pal[3 * i:3 * i + 3]) for i in range(n)]
        if trns is not None:
            for i in range(min(n, len(trns))):
                pl[i] = tuple(blend(trns[i], c) for c in pl[i])
        p.pal = pl + [(0, 0, 0)] * (256 - n)
    elif trns is not None and p.ct == 0 and len(trns) == 2:
        p.tgrey = (trns[0] << 8) | trns[1]
    elif trns is not None and p.ct == 2 and len(trns) == 6:
        p.trgb = tuple((trns[2 * k] << 8) | trns[2 * k + 1] for k in range(3))
    br = IdatBits(rd)
    br.left = ln
    br.started = True
    w, h, ch = p.w, p.h, PNG_CHAN[p.ct]
    bpp = max(1, p.depth * ch // 8)
    passes = ADAM7 if p.il else ((0, 0, 1, 1),)
    plan = []
    total = 0
    for x0, y0, dx, dy in passes:
        pw = (w - x0 + dx - 1) // dx if w > x0 else 0
        ph = (h - y0 + dy - 1) // dy if h > y0 else 0
        if pw and ph:
            rb = (pw * p.depth * ch + 7) // 8
            plan.append((x0, y0, dx, dy, pw, ph, rb))
            total += ph * (rb + 1)
    data = inflate(br, total)
    nch = 3 if p.rf == RF_RGB else 1
    img = [bytearray(w * nch) for _ in range(h)]
    o = 0
    for x0, y0, dx, dy, pw, ph, rb in plan:
        prev = bytearray(rb)
        for j in range(ph):
            ft = data[o]
            cur = bytearray(data[o + 1:o + 1 + rb])
            o += rb + 1
            unfilter(ft, cur, prev, bpp)
            px = png_pixels(p, cur, pw)
            y = y0 + j * dy
            for k in range(pw):
                x = x0 + k * dx
                img[y][x * nch:(x + 1) * nch] = px[k * nch:(k + 1) * nch]
            prev = cur
    if p.il:
        p.par = 1
    p.rows = [(y, bytes(img[y])) for y in range(h)]


# =============================================================================
# JPEG (SPEC.md 106.19): baseline and progressive Huffman, 8-bit, one or
# three components, decoded STRAIGHT TO A SCALE - the IDCT itself is reduced
# (8x8 at 1/1, 4x4 at 1/2, 2x2 at 1/4, the DC alone at 1/8) - and turned
# upright by its EXIF orientation. Every operation is the guest's 16-bit
# two's-complement arithmetic (s16), so a hostile stream's wraparound is the
# guest's wraparound too. apps/pixel/pxjpeg.asm is this section in assembly,
# check for check and in the same order.
# =============================================================================
JZZ = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33,
       40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50,
       43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46,
       53, 60, 61, 54, 47, 55, 62, 63]     # zigzag k -> natural 8 v + u

# THE PRESCALE a coefficient is dequantised with, natural order: the
# multiplier is M = min((q P + 2048) >> 12, 32767), i.e. M = 32 q p(u) p(v)
# with every scale's normalisation folded in, and a pixel is the
# butterflies' sum >> 5.
#   1/1  jidctfst.c's aanscales (AAN's own prescale)
#   1/2  the 8-point basis averaged over PAIRS of pixels, coefficients u < 4:
#        p(u) = C(u)/2 cos(u pi/16) x (1, cos pi/8, 1/sqrt 2, cos pi/8)
#   1/4  ...over FOURS, u < 2: p(u) = C(u)/2 x (1, 0.640729)
#   1/8  the DC alone: p = 1 / (2 sqrt 2)
JP_P0 = [16384, 22725, 21407, 19266, 16384, 12873, 8867, 4520,
         22725, 31521, 29692, 26722, 22725, 17855, 12299, 6270,
         21407, 29692, 27969, 25172, 21407, 16819, 11585, 5906,
         19266, 26722, 25172, 22654, 19266, 15137, 10426, 5315,
         16384, 22725, 21407, 19266, 16384, 12873, 8867, 4520,
         12873, 17855, 16819, 15137, 12873, 10114, 6967, 3552,
         8867, 12299, 11585, 10426, 8867, 6967, 4799, 2446,
         4520, 6270, 5906, 5315, 4520, 3552, 2446, 1247]
JP_P1 = [16384, 20995, 15137, 17799, 20995, 26905, 19397, 22809,
         15137, 19397, 13985, 16444, 17799, 22809, 16444, 19336]   # 4 x 4
JP_P2 = [16384, 14846, 14846, 13452]                               # 2 x 2
JP_K1414, JP_K1847, JP_K1082, JP_K2613, JP_KT8 = 362, 473, 277, 669, 106
JP_BIAS = (128 << 5) + 16       # the level shift and the rounding, on the DC
JP_HEADS = 16                   # heads a header walk may read (SPEC 106.19)
# the sampling a three-component frame may have: Y's (H, V), Cb and Cr 1 x 1
JP_SAMP = ((1, 1), (1, 2), (2, 1), (2, 2), (4, 1))
# the coefficients a scale keeps: a natural index is kept when idx & mask is 0
JP_KEEP = (0x00, 0x24, 0x36, 0x3F)
JP_ZKEPT = (0, 1, 2, 4)         # 1/4's four, in zigzag order (the store's)
PK_JPEG, PK_JPEGP = 9, 10       # the Info panel's Packing


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def jmul(x, k):
    """(x k) >> 8, floor, its low sixteen bits: `imul`, then DL:AH."""
    return s16((x * k) >> 8)


def jp_mults(q, s):
    """The multipliers for quant table q (natural order) at scale s."""
    P = (JP_P0, JP_P1, JP_P2, [16384])[s]
    n = 8 >> s
    out = [0] * 64
    for v in range(n):
        for u in range(n):
            out[v * 8 + u] = min((q[v * 8 + u] * P[v * n + u] + 2048) >> 12,
                                 32767)
    return out


def aan8(d):
    """jidctfst.c's 1-D pass in 16 bits: d[0..7] by frequency -> 8 values."""
    t0, t1, t2, t3 = d[0], d[2], d[4], d[6]
    t10 = s16(t0 + t2)
    t11 = s16(t0 - t2)
    t13 = s16(t1 + t3)
    t12 = s16(jmul(s16(t1 - t3), JP_K1414) - t13)
    t0 = s16(t10 + t13)
    t3 = s16(t10 - t13)
    t1 = s16(t11 + t12)
    t2 = s16(t11 - t12)
    t4, t5, t6, t7 = d[1], d[3], d[5], d[7]
    z13 = s16(t6 + t5)
    z10 = s16(t6 - t5)
    z11 = s16(t4 + t7)
    z12 = s16(t4 - t7)
    t7 = s16(z11 + z13)
    t11 = jmul(s16(z11 - z13), JP_K1414)
    z5 = jmul(s16(z10 + z12), JP_K1847)
    t10 = s16(jmul(z12, JP_K1082) - z5)
    t12 = s16(jmul(z10, -JP_K2613) + z5)
    t6 = s16(t12 - t7)
    t5 = s16(t11 - t6)
    t4 = s16(t10 + t5)
    return [s16(t0 + t7), s16(t1 + t6), s16(t2 + t5), s16(t3 - t4),
            s16(t3 + t4), s16(t2 - t5), s16(t1 - t6), s16(t0 - t7)]


def red4(d):
    """The 4-point pass (1/2): two multiplies by tan(pi/8)."""
    ea, eb = s16(d[0] + d[2]), s16(d[0] - d[2])
    oa = s16(d[1] + jmul(d[3], JP_KT8))
    ob = s16(jmul(d[1], JP_KT8) - d[3])
    return [s16(ea + oa), s16(eb + ob), s16(eb - ob), s16(ea - oa)]


def red2(d):
    return [s16(d[0] + d[1]), s16(d[0] - d[1])]


def jclamp(v):
    v >>= 5
    return 0 if v < 0 else 255 if v > 255 else v


def jp_idct(blk, s):
    """blk: 64 dequantised values, natural order, JP_BIAS already on the
    DC. The block's (8 >> s)^2 pixels, row by row. Columns first."""
    if s == 3:
        return [jclamp(blk[0])]
    n = 8 >> s
    f = (aan8, red4, red2)[s]
    ws = [0] * (n * n)
    for u in range(n):
        col = f([blk[v * 8 + u] for v in range(n)])
        for y in range(n):
            ws[y * n + u] = col[y]
    out = []
    for y in range(n):
        out += [jclamp(v) for v in f(ws[y * n:y * n + n])]
    return out


# the colour tables: jdcolor.c's ycc_rgb_convert, exactly (SCALEBITS 16)
JC_RCR = [(91881 * (i - 128) + 32768) >> 16 for i in range(256)]
JC_BCB = [(116130 * (i - 128) + 32768) >> 16 for i in range(256)]
JC_GCB = [-22554 * (i - 128) for i in range(256)]
JC_GCR = [-46802 * (i - 128) + 32768 for i in range(256)]


def jp_c8(v):
    return 0 if v < 0 else 255 if v > 255 else v


class JBits:
    """THE ENTROPY-CODED SEGMENT, a bit at a time (SPEC.md 106.19). FF 00 is
    a data FF and FF FF is fill; FF and anything else is a MARKER, which
    ends the segment, and so does the file's end - past the end the bits
    are zeros, and a bit TAKEN from them makes the decode `cut short` at its
    next check (each MCU's end, an interval's, and before any refusal)."""

    def __init__(self, rd):
        self.rd = rd
        self.reset()

    def reset(self):
        self.acc = self.n = 0
        self.curfill = False
        self.ended = False
        self.marker = None          # what ended it; None is the file's end
        self.over = False

    def _load(self):
        if self.ended:
            return 0, True
        b = self.rd.byte_or_end()
        if b < 0:
            self.ended = True
            return 0, True
        if b != 0xFF:
            return b, False
        while True:
            c = self.rd.byte_or_end()
            if c < 0:
                self.ended = True
                return 0, True
            if c == 0:
                return 0xFF, False
            if c != 0xFF:
                self.ended = True
                self.marker = c
                return 0, True

    def bit(self):
        if self.n == 0:
            self.acc, self.curfill = self._load()
            self.n = 8
        self.n -= 1
        if self.curfill:
            self.over = True
        return (self.acc >> self.n) & 1

    def bits(self, k):
        v = 0
        for _ in range(k):
            v = (v << 1) | self.bit()
        return v

    def check(self):
        if self.over:
            raise Refused(PXD_TRUNC)

    def bad(self):
        """A refusal inside the data: `cut short` if the zeros were used."""
        self.check()
        raise Refused(PXD_DATA)

    def to_marker(self, skip_rst=False):
        """Discard to the marker that ends the segment (with skip_rst, the
        first that is not RSTn): its code, or None at the file's end."""
        while True:
            while not self.ended:
                self._load()
            m = self.marker
            if skip_rst and m is not None and 0xD0 <= m <= 0xD7:
                self.reset()
                continue
            return m


def jp_extend(v, s):
    return v - (1 << s) + 1 if s and v < (1 << (s - 1)) else v


class JHuff:
    """A Huffman table (JPEG Annex C), checked as jdhuff.c checks it."""

    def __init__(self, counts, vals, dc):
        if sum(counts) > 256:
            raise Refused(PXD_DATA)
        code = 0
        self.maxc = [-1] * 17
        self.off = [0] * 17
        p = 0
        for l in range(1, 17):
            self.off[l] = p - code          # vals[off + code] for code at l
            code += counts[l - 1]
            p += counts[l - 1]
            if code >= (1 << l):
                raise Refused(PXD_DATA)
            self.maxc[l] = code - 1 if counts[l - 1] else -1
            code <<= 1
        if dc and any(v > 15 for v in vals):
            raise Refused(PXD_DATA)
        self.vals = list(vals)

    def decode(self, br):
        code = 0
        for l in range(1, 17):
            code = (code << 1) | br.bit()
            if code <= self.maxc[l]:
                return self.vals[self.off[l] + code]
        br.bad()


class JFrame:
    pass


def jp_sof(seg, prog, code):
    """The frame header's fields (seg: the segment after its length). A
    refusal is `code` where SPEC.md 106.19 says `bad header`."""
    if len(seg) < 6:
        raise Refused(code)
    if seg[0] != 8:
        raise Refused(PXD_DEPTH)
    f = JFrame()
    f.h, f.w, f.nf = (seg[1] << 8) | seg[2], (seg[3] << 8) | seg[4], seg[5]
    if not (1 <= f.w <= DIM_MAX and 1 <= f.h <= DIM_MAX):
        raise Refused(PXD_DIMS)
    if f.nf not in (1, 3):
        raise Refused(PXD_DEPTH)
    if len(seg) != 6 + 3 * f.nf:
        raise Refused(code)
    f.comps = []
    for i in range(f.nf):
        cid, hv, tq = seg[6 + 3 * i], seg[7 + 3 * i], seg[8 + 3 * i]
        h, v = hv >> 4, hv & 15
        if not (1 <= h <= 4 and 1 <= v <= 4) or tq > 3 or \
                any(c[0] == cid for c in f.comps):
            raise Refused(code)
        f.comps.append([cid, h, v, tq])
    if f.nf == 1:
        f.comps[0][1] = f.comps[0][2] = 1   # one component: an MCU is a block
    elif (f.comps[0][1], f.comps[0][2]) not in JP_SAMP or \
            any((c[1], c[2]) != (1, 1) for c in f.comps[1:]):
        raise Refused(PXD_PACK)
    f.hmax, f.vmax = f.comps[0][1], f.comps[0][2]
    f.mcux = (f.w + 8 * f.hmax - 1) // (8 * f.hmax)
    f.mcuy = (f.h + 8 * f.vmax - 1) // (8 * f.vmax)
    f.prog = prog
    return f


def jp_exif(seg):
    """An APP1's bytes (those the head holds): its orientation, or 0."""
    if bytes(seg[:6]) != b"Exif\0\0" or len(seg) < 14:
        return 0
    t = seg[6:]
    if bytes(t[:4]) == b"II*\0":
        g16 = lambda o: t[o] | (t[o + 1] << 8)
        g32 = lambda o: g16(o) | (g16(o + 2) << 16)
    elif bytes(t[:4]) == b"MM\0*":
        g16 = lambda o: (t[o] << 8) | t[o + 1]
        g32 = lambda o: (g16(o) << 16) | g16(o + 2)
    else:
        return 0
    ifd = g32(4)
    if ifd + 2 > len(t):
        return 0
    for i in range(g16(ifd)):
        e = ifd + 2 + 12 * i
        if e + 12 > len(t):
            return 0
        if g16(e) == 0x0112:
            v = g16(e + 8)
            if g16(e + 2) == 3 and g32(e + 4) == 1 and 1 <= v <= 8:
                return v
            return 0
    return 0


def jpeg_header(data, fsz):
    """THE HEADER, as the guest's HEAD walks it: the first 2,048 bytes, and
    when a marker is not whole in them, the 2,048 at that marker - at most
    JP_HEADS heads - to the first frame header. A marker is read as four
    bytes (FF, its code, a length); the file ending inside them is `cut
    short`."""
    base, pos, heads, orient, exif = 0, 2, 1, 0, False
    jfif, adobe = False, None
    head = data[:HEAD_MAX]
    if len(head) < 4 or head[0] != 0xFF or head[1] != 0xD8:
        raise Refused(PXD_HEAD)
    while True:
        need = 4
        if pos + 4 <= len(head) and head[pos] == 0xFF and \
                head[pos + 1] in (0xC0, 0xC1, 0xC2):
            ln = (head[pos + 2] << 8) | head[pos + 3]
            if ln > HEAD_MAX - 2:
                raise Refused(PXD_HEAD)
            need = max(4, 2 + ln)           # a frame header, whole
        if pos + need > len(head):
            if base + pos + need > fsz:
                raise Refused(PXD_TRUNC)
            heads += 1
            if heads > JP_HEADS:
                raise Refused(PXD_HEAD)
            base += pos
            pos = 0
            head = data[base:base + HEAD_MAX]
            continue
        if head[pos] != 0xFF:
            raise Refused(PXD_HEAD)
        m = head[pos + 1]
        if m == 0xFF:
            pos += 1
            continue
        if m == 0x01 or 0xD0 <= m <= 0xD7:
            pos += 2
            continue
        if m in (0xD8, 0xD9):
            raise Refused(PXD_HEAD)
        ln = (head[pos + 2] << 8) | head[pos + 3]
        if ln < 2:
            raise Refused(PXD_HEAD)
        if m in (0xC0, 0xC1, 0xC2):
            f = jp_sof(head[pos + 4:pos + 2 + ln], m == 0xC2, PXD_HEAD)
            break
        if 0xC3 <= m <= 0xCF and m not in (0xC4, 0xC8, 0xCC):
            raise Refused(PXD_PACK)         # lossless, hierarchical, arithmetic
        seg = head[pos + 4:min(pos + 2 + ln, len(head))]
        if m == 0xE0 and ln - 2 >= 14 and bytes(seg[:5]) == b"JFIF\0":
            jfif = True
        if m == 0xEE and ln - 2 >= 12 and len(seg) >= 12 and \
                bytes(seg[:5]) == b"Adobe":
            adobe = seg[11]
        if m == 0xE1 and not exif:
            seg = head[pos + 4:min(pos + 2 + ln, len(head))]
            if bytes(seg[:6]) == b"Exif\0\0":
                exif = True
                orient = jp_exif(seg)
        pos += 2 + ln
    p = Pic()
    p.fmt = "JPEG"
    p.frame = f
    p.orient = orient or 1
    p.w, p.h = (f.h, f.w) if p.orient >= 5 else (f.w, f.h)
    p.rf = RF_GREY if f.nf == 1 else RF_RGB
    p.bits = 8 * f.nf
    p.pack = PK_JPEGP if f.prog else PK_JPEG
    p.prog = f.prog
    p.smin = 2 if f.prog else 0
    # three components are YCbCr - unless, as libjpeg reads them, there is no
    # JFIF marker and an Adobe one says transform 0, or neither and the
    # components are named R, G, B: then they are R, G, B as they stand
    p.jrgb = f.nf == 3 and not jfif and (
        adobe == 0 if adobe is not None else
        [c[0] for c in f.comps] == [82, 71, 66])
    return p


class JDec:
    """The decode's state: tables, the frame, the store (SPEC.md 106.19)."""

    def __init__(self, p, rd, s):
        self.p, self.rd, self.s = p, rd, s
        self.qt = [None] * 4
        self.dc = [None] * 4
        self.ac = [None] * 4
        self.ri = 0
        self.f = None
        self.keep = JP_KEEP[s]
        self.mult = None            # per component, latched at its first scan
        self.img = []               # the scaled rows, top-down, unoriented


def jp_seg(rd):
    """A marker segment's bytes after its length (the file ending is `cut
    short`; a length below 2 is `damaged`)."""
    ln = (rd.byte() << 8) | rd.byte()
    if ln < 2:
        raise Refused(PXD_DATA)
    return rd.take(ln - 2)


def jp_dqt(d, seg):
    o = 0
    while o < len(seg):
        pq, tq = seg[o] >> 4, seg[o] & 15
        o += 1
        if pq > 1 or tq > 3:
            raise Refused(PXD_DATA)
        n = 128 if pq else 64
        if o + n > len(seg):
            raise Refused(PXD_DATA)
        q = [0] * 64
        for k in range(64):
            v = (seg[o + 2 * k] << 8) | seg[o + 2 * k + 1] if pq else seg[o + k]
            if v == 0:
                raise Refused(PXD_DATA)
            q[JZZ[k]] = v
        d.qt[tq] = q
        o += n


def jp_dht(d, seg):
    o = 0
    while o < len(seg):
        tc, th = seg[o] >> 4, seg[o] & 15
        o += 1
        if tc > 1 or th > 3 or o + 16 > len(seg):
            raise Refused(PXD_DATA)
        counts = list(seg[o:o + 16])
        o += 16
        n = sum(counts)
        if n > 256 or o + n > len(seg):
            raise Refused(PXD_DATA)
        h = JHuff(counts, seg[o:o + n], tc == 0)
        o += n
        (d.ac if tc else d.dc)[th] = h


def jpeg_rows(p, rd, s):
    """THE DECODE: the stream from its first byte, at scale s - the rows of
    the picture at 1/2^s, upright, into p.rows as MASTER rows (p.presc)."""
    d = JDec(p, rd, s)
    if p.prog and s < 2:
        raise Refused(PXD_BIG)      # (the resident never asks: SPEC 106.19)
    rd.skip_to(2)
    m = None
    while True:
        if m is None:
            if rd.byte() != 0xFF:
                raise Refused(PXD_DATA)
            m = rd.byte()
            while m == 0xFF:
                m = rd.byte()
        if m == 0xD9:                       # EOI
            if d.f is None or not d.f.prog or not getattr(d, "scans", 0):
                raise Refused(PXD_DATA)
            jp_output(d)
            break
        if m in (0xD8,) or (m == 0xDA and d.f is None):
            raise Refused(PXD_DATA)
        if m == 0x01 or 0xD0 <= m <= 0xD7:
            m = None
            continue
        seg = jp_seg(rd)
        if m == 0xDB:
            jp_dqt(d, seg)
        elif m == 0xC4:
            jp_dht(d, seg)
        elif m == 0xDD:
            if len(seg) != 2:
                raise Refused(PXD_DATA)
            d.ri = (seg[0] << 8) | seg[1]
        elif 0xC0 <= m <= 0xCF and m not in (0xC4, 0xC8, 0xCC):
            if d.f is not None or m not in (0xC0, 0xC1, 0xC2):
                raise Refused(PXD_DATA)
            d.f = jp_sof(seg, m == 0xC2, PXD_DATA)
        elif m == 0xDA:
            nm = jp_scan(d, seg)
            if not d.f.prog:
                break
            m = nm
            continue
        m = None
    jp_orient(p, d)


def jp_scan(d, seg):
    """One scan: SOS's checks, then its data. Answers the marker that ended
    it (a progressive scan's), None for a baseline one."""
    f = d.f
    if len(seg) < 1:
        raise Refused(PXD_DATA)
    ns = seg[0]
    if not (1 <= ns <= f.nf) or len(seg) != 4 + 2 * ns:
        raise Refused(PXD_DATA)
    sc = []
    for i in range(ns):
        cid, t = seg[1 + 2 * i], seg[2 + 2 * i]
        ci = [k for k in range(f.nf) if f.comps[k][0] == cid]
        if not ci or ci[0] in [c[0] for c in sc] or t >> 4 > 3 or t & 15 > 3:
            raise Refused(PXD_DATA)
        sc.append((ci[0], t >> 4, t & 15))
    ss, se = seg[1 + 2 * ns], seg[2 + 2 * ns]
    ah, al = seg[3 + 2 * ns] >> 4, seg[3 + 2 * ns] & 15
    if f.prog:
        if ss == 0:
            if se != 0:
                raise Refused(PXD_DATA)
        elif ss > se or se > 63 or ns != 1:
            raise Refused(PXD_DATA)
        if ah and al != ah - 1:
            raise Refused(PXD_DATA)
        if al > 13:
            raise Refused(PXD_DATA)
    elif ns != f.nf:
        raise Refused(PXD_PACK)             # a baseline picture in more scans
    # the tables it needs: present now; the quant tables latched at a
    # component's first scan
    if d.mult is None:
        d.mult = [None] * f.nf
        d.store = None
    for ci, td, ta in sc:
        if d.mult[ci] is None:
            q = d.qt[f.comps[ci][3]]
            if q is None:
                raise Refused(PXD_DATA)
            d.mult[ci] = jp_mults(q, d.s)
        if (not f.prog or ss == 0 and ah == 0) and d.dc[td] is None:
            raise Refused(PXD_DATA)
        if (not f.prog or ss) and d.ac[ta] is None:
            raise Refused(PXD_DATA)
    d.scans = getattr(d, "scans", 0) + 1
    br = JBits(d.rd)
    if not f.prog:
        jp_baseline(d, br, sc)
        return None
    if d.store is None:
        d.store = []
        for c in f.comps:
            bpl, rows = f.mcux * c[1], f.mcuy * c[2]
            d.store.append([[0, 0, 0, 0, 0] for _ in range(bpl * rows)])
    if ss and d.s == 3:                     # 1/8 needs no AC: the scan is
        return br.to_marker(True)           # skipped to its next marker
    jp_progscan(d, br, sc, ss, se, ah, al)
    m = br.to_marker(True)
    if m is None:
        raise Refused(PXD_TRUNC)
    return m


def jp_restart(d, br, n):
    """The end of a restart interval: the RST marker that must follow."""
    br.check()
    m = br.to_marker()
    if m is None:
        raise Refused(PXD_TRUNC)
    if m != 0xD0 + (n & 7):
        raise Refused(PXD_DATA)
    br.reset()


def jp_baseline(d, br, sc):
    f, s = d.f, d.s
    n8 = 8 >> s
    pred = [0] * f.nf
    W_s, H_s = f.w >> s, f.h >> s
    total, done = f.mcux * f.mcuy, 0
    rst = 0
    for my in range(f.mcuy):
        planes = []
        for c in f.comps:
            planes.append([bytearray(f.mcux * c[1] * n8) for _ in range(c[2] * n8)])
        for mx in range(f.mcux):
            for ci, td, ta in sc:
                c = f.comps[ci]
                for v in range(c[2]):
                    for h in range(c[1]):
                        coef = [0] * 64
                        t = d.dc[td].decode(br)
                        pred[ci] = s16(pred[ci] + jp_extend(br.bits(t), t))
                        coef[0] = pred[ci]
                        k = 1
                        while k < 64:
                            rs = d.ac[ta].decode(br)
                            r, z = rs >> 4, rs & 15
                            if z:
                                k += r
                                if k > 63:
                                    br.bad()
                                val = jp_extend(br.bits(z), z)
                                if not JZZ[k] & d.keep:
                                    coef[JZZ[k]] = val
                                k += 1
                            elif r == 15:
                                k += 16
                            else:
                                break
                        jp_put(d, planes[ci], coef, d.mult[ci],
                               (mx * c[1] + h) * n8, v * n8)
            br.check()
            done += 1
            if d.ri and done % d.ri == 0 and done < total:
                jp_restart(d, br, rst)
                rst += 1
                pred = [0] * f.nf
        jp_band(d, planes, my)


def jp_put(d, plane, coef, mult, x0, y0):
    """Dequantise the kept coefficients, the bias on the DC, the IDCT at
    the scale, into the plane at (x0, y0)."""
    s = d.s
    n8 = 8 >> s
    blk = [0] * 64
    for z in range(64):
        if coef[z] and not z & d.keep:
            blk[z] = s16(coef[z] * mult[z])
    blk[0] = s16(blk[0] + JP_BIAS)
    px = jp_idct(blk, s)
    for y in range(n8):
        plane[y0 + y][x0:x0 + n8] = bytes(px[y * n8:(y + 1) * n8])


def jp_band(d, planes, my):
    """MCU row my's pixels at the scale into d.img: Y and, for colour, the
    chroma by REPLICATION - each chroma pixel serves hmax x vmax."""
    f, s = d.f, d.s
    n8 = 8 >> s
    W_s, H_s = f.w >> s, f.h >> s
    for yy in range(f.vmax * n8):
        y = my * f.vmax * n8 + yy
        if y >= H_s:
            break
        yp = planes[0][yy]
        if f.nf == 1:
            d.img.append(bytes(yp[:W_s]))
            continue
        cb = planes[1][yy // f.vmax]
        cr = planes[2][yy // f.vmax]
        row = bytearray(3 * W_s)
        if d.p.jrgb:
            for x in range(W_s):
                row[3 * x] = yp[x]
                row[3 * x + 1] = cb[x // f.hmax]
                row[3 * x + 2] = cr[x // f.hmax]
            d.img.append(bytes(row))
            continue
        for x in range(W_s):
            Y, b, r = yp[x], cb[x // f.hmax], cr[x // f.hmax]
            g = (JC_GCB[b] + JC_GCR[r]) >> 16
            row[3 * x] = jp_c8(Y + JC_RCR[r])
            row[3 * x + 1] = jp_c8(Y + g)
            row[3 * x + 2] = jp_c8(Y + JC_BCB[b])
        d.img.append(bytes(row))


def jp_progscan(d, br, sc, ss, se, ah, al):
    """A progressive scan into the store: DC first or refine (interleaved or
    not), AC first or refine (one component). The store keeps, a block,
    the four coefficients 1/4 needs (zigzag 0, 1, 2, 4) and which of the
    63 have been nonzero - which a refinement scan must know to be read."""
    f = d.f
    p1, m1 = 1 << al, s16(-1 << al)
    eobrun = 0
    pred = [0] * f.nf
    if len(sc) > 1:
        units = []
        for my in range(f.mcuy):
            for mx in range(f.mcux):
                u = []
                for ci, td, ta in sc:
                    c = f.comps[ci]
                    for v in range(c[2]):
                        for h in range(c[1]):
                            u.append((ci, td, ta, (my * c[2] + v) * f.mcux * c[1]
                                      + mx * c[1] + h))
                units.append(u)
    else:
        ci, td, ta = sc[0]
        c = f.comps[ci]
        cw = (f.w * c[1] + f.hmax - 1) // f.hmax
        ch = (f.h * c[2] + f.vmax - 1) // f.vmax
        bw, bh = (cw + 7) // 8, (ch + 7) // 8
        units = [[(ci, td, ta, by * f.mcux * c[1] + bx)]
                 for by in range(bh) for bx in range(bw)]
    rst = 0
    for i, u in enumerate(units):
        for ci, td, ta, bi in u:
            b = d.store[ci][bi]
            if ss == 0:
                if ah == 0:
                    t = d.dc[td].decode(br)
                    pred[ci] = s16(pred[ci] + jp_extend(br.bits(t), t))
                    b[0] = s16(pred[ci] << al)
                elif br.bit():
                    b[0] = s16(b[0] | p1)
                continue
            k = ss
            if ah == 0:                     # AC first
                if eobrun:
                    eobrun -= 1
                    continue
                while k <= se:
                    rs = d.ac[ta].decode(br)
                    r, z = rs >> 4, rs & 15
                    if z:
                        k += r
                        if k > se:
                            br.bad()
                        val = s16(jp_extend(br.bits(z), z) << al)
                        jp_sput(d, b, k, val)
                        b[4] |= 1 << k
                        k += 1
                    elif r == 15:
                        k += 16
                    else:
                        eobrun = (1 << r) + (br.bits(r) if r else 0) - 1
                        break
                continue
            # AC refine (jdphuff.c's decode_mcu_AC_refine, on the history)
            if eobrun == 0:
                while k <= se:
                    rs = d.ac[ta].decode(br)
                    r, z = rs >> 4, rs & 15
                    nv = 0
                    if z:
                        if z != 1:
                            br.bad()
                        nv = p1 if br.bit() else m1
                    elif r != 15:
                        eobrun = (1 << r) + (br.bits(r) if r else 0)
                        break
                    while k <= se:
                        if b[4] >> k & 1:
                            if br.bit():
                                jp_refine(d, b, k, p1, m1)
                        else:
                            if r == 0:
                                break
                            r -= 1
                        k += 1
                    if nv:
                        if k > se:
                            br.bad()
                        jp_sput(d, b, k, nv)
                        b[4] |= 1 << k
                    k += 1
            if eobrun > 0:
                while k <= se:
                    if b[4] >> k & 1 and br.bit():
                        jp_refine(d, b, k, p1, m1)
                    k += 1
                eobrun -= 1
        br.check()
        if d.ri and (i + 1) % d.ri == 0 and i + 1 < len(units):
            jp_restart(d, br, rst)
            rst += 1
            pred = [0] * f.nf
            eobrun = 0


def jp_sput(d, b, k, val):
    if k in JP_ZKEPT[1:]:
        b[JP_ZKEPT.index(k)] = val


def jp_refine(d, b, k, p1, m1):
    if k in JP_ZKEPT[1:]:
        i = JP_ZKEPT.index(k)
        if not b[i] & p1:
            b[i] = s16(b[i] + (p1 if b[i] >= 0 else m1))


def jp_output(d):
    """After a progressive picture's last scan: the store through the
    reduced IDCT, MCU row by MCU row, into d.img."""
    f, s = d.f, d.s
    n8 = 8 >> s
    for my in range(f.mcuy):
        planes = []
        for ci, c in enumerate(f.comps):
            bpl = f.mcux * c[1]
            pl = [bytearray(bpl * n8) for _ in range(c[2] * n8)]
            for v in range(c[2]):
                for bx in range(bpl):
                    b = d.store[ci][(my * c[2] + v) * bpl + bx]
                    coef = [0] * 64
                    for i, k in enumerate(JP_ZKEPT):
                        coef[JZZ[k]] = b[i]
                    jp_put(d, pl, coef, d.mult[ci], bx * n8, v * n8)
            planes.append(pl)
        jp_band(d, planes, my)


def jp_orient(p, d):
    """THE ORIENTATION (EXIF tag 274) applied to the master: d.img is the
    picture as stored, W_s x H_s; p.rows the master's rows, upright."""
    f, s = d.f, d.s
    W_s, H_s = f.w >> s, f.h >> s
    nch = 1 if f.nf == 1 else 3
    o = p.orient
    img = d.img
    if o == 1:
        rows = img
    else:
        mw, mh = (H_s, W_s) if o >= 5 else (W_s, H_s)
        rows = []
        for r in range(mh):
            row = bytearray(mw * nch)
            for c in range(mw):
                x, y = {2: (W_s - 1 - c, r), 3: (W_s - 1 - c, H_s - 1 - r),
                        4: (c, H_s - 1 - r), 5: (r, c), 6: (r, H_s - 1 - c),
                        7: (W_s - 1 - r, H_s - 1 - c), 8: (W_s - 1 - r, c)}[o]
                row[c * nch:(c + 1) * nch] = img[y][x * nch:(x + 1) * nch]
            rows.append(bytes(row))
    p.rows = list(enumerate(rows))
    p.presc = s


# =============================================================================
# THE EXTRAS PART (SPEC.md 106.25): TIFF, ICO and CUR, IFF ILBM and PBM,
# MacPaint - apps/pixel/pxextra.asm is this section in assembly, check for
# check and in the same order
# =============================================================================
EX_HEADS = 16                   # heads a header walk may read (the first too)
EX_TABLE = 8192                 # the part's TABLE: strips, or ICO's AND mask
EX_STRIPS = EX_TABLE // 8       # a dword offset and a dword count each
EX_ROWMAX = 32768


class Heads:
    """HEAD's view of the file: ONE head of up to HEAD_MAX bytes at a time,
    the first at offset 0 and each next one (PXD_MORE) the HEAD_MAX bytes at
    the offset asked for, clipped to the file. At most EX_HEADS of them, the
    first counted; one more is `bad header`. Two ways to read:

    need(off, n) - a STRUCTURE (n <= HEAD_MAX) from one head: the current
        head when it holds [off, off+n) whole; else, when the file holds it,
        a new head AT off; else `cut short` (checked before the head is
        counted, so a file that ends first is `cut short`, never `bad
        header`).
    run(off, n) - a BYTE RUN of any length, streamed: `cut short` at once
        when the file does not hold [off, off+n); then the bytes the current
        head holds from off, a new head at the FIRST BYTE IT DOES NOT HOLD,
        and so on - so a run starting outside the current head starts a head
        at off."""

    def __init__(self, data):
        self.d = data
        self.fsz = len(data)
        self.base = 0
        self.hl = min(HEAD_MAX, self.fsz)
        self.n = 1

    def _more(self, off):
        self.n += 1
        if self.n > EX_HEADS:
            raise Refused(PXD_HEAD)
        self.base = off
        self.hl = min(HEAD_MAX, self.fsz - off)

    def holds(self, off, n):
        return self.base <= off and off + n <= self.base + self.hl

    def need(self, off, n):
        if not self.holds(off, n):
            if off + n > self.fsz:
                raise Refused(PXD_TRUNC)
            self._more(off)
        return self.d[off:off + n]

    def run(self, off, n):
        if off + n > self.fsz:
            raise Refused(PXD_TRUNC)
        out = bytearray()
        a, end = off, off + n
        while a < end:
            if not self.base <= a < self.base + self.hl:
                self._more(a)
            b = min(end, self.base + self.hl)
            out += self.d[a:b]
            a = b
        return bytes(out)


class Bytes:
    """A bounded byte source: a TIFF strip's bytes, an IFF BODY's, the rest
    of a MacPaint file - reading past them is `cut short`."""

    def __init__(self, src):
        self.s = src
        self.p = 0

    def byte(self):
        if self.p >= len(self.s):
            raise Refused(PXD_TRUNC)
        v = self.s[self.p]
        self.p += 1
        return v

    def take(self, n):
        if self.p + n > len(self.s):
            raise Refused(PXD_TRUNC)
        v = self.s[self.p:self.p + n]
        self.p += n
        return v


BYTE1 = [bytes((v,)) for v in range(256)]


def unpackbits(src, total):
    """PackBits / ByteRun1 as ONE continuous stream (a TIFF strip's, IFF's
    BODY, MacPaint's):
    `total` bytes out; a run may cross a row; the stream stops the moment
    the last byte is made - the rest of a run dropped, nothing more read (a
    literal's unneeded bytes are not read either). 128 is nothing."""
    s, p, end = src.s, src.p, len(src.s)
    out = bytearray()
    while len(out) < total:
        if p >= end:
            raise Refused(PXD_TRUNC)
        n = s[p]
        p += 1
        if n < 128:
            k = min(n + 1, total - len(out))
            if p + k > end:
                raise Refused(PXD_TRUNC)
            out += s[p:p + k]
            p += k
        elif n > 128:
            if p >= end:
                raise Refused(PXD_TRUNC)
            out += BYTE1[s[p]] * min(257 - n, total - len(out))
            p += 1
    src.p = p
    return out


def lzw_tiff(src, run):
    """TIFF's LZW (os88lzw.inc's MSB-first reader with LZW_EARLY): codes MSB
    first from 9 bits to 12, Clear 256, End 257; the code size grows when the
    next free code PLUS ONE reaches a power of two (libtiff's early change);
    a full table (4096) adds nothing until a Clear. run(bytes) answers True
    to stop. 'EOI' (the End code), 'END' (a code would need bits past the
    input) or 'STOP'; a code above the next free code, or a first code after
    a Clear (or at the start) that is not a root, is PXD_DATA."""
    nbits = 8 * len(src)
    pos = 0
    prefix = [0] * 4096
    suffix = [0] * 4096
    free, cs, old, first = 258, 9, None, 0
    while True:
        if pos + cs > nbits:
            return "END"
        o = pos >> 3
        seg = bytes(src[o:o + 3])
        v = int.from_bytes(seg + b"\0" * (3 - len(seg)), "big")
        c = (v >> (24 - (pos & 7) - cs)) & ((1 << cs) - 1)
        pos += cs
        if c == 256:
            free, cs, old = 258, 9, None
            continue
        if c == 257:
            return "EOI"
        if old is None:
            if c > 255:
                raise Refused(PXD_DATA)
            old = first = c
            if run(bytes((c,))):
                return "STOP"
            continue
        if c > free:
            raise Refused(PXD_DATA)
        out = []
        k = c
        if c == free:
            out.append(first)
            k = old
        while k > 255:
            out.append(suffix[k])
            k = prefix[k]
        out.append(k)
        first = k
        out.reverse()
        if run(bytes(out)):
            return "STOP"
        if free < 4096 and old < free:
            prefix[free] = old
            suffix[free] = first
            free += 1
            if cs < 12 and free + 1 >= (1 << cs):
                cs += 1
        old = c


# --- TIFF ---------------------------------------------------------------------
TIF_READ = (256, 257, 258, 259, 262, 266, 273, 277, 278, 279, 284, 317, 320,
            338)                # the tags read, in the order values are read
TIF_ARRAYS = (273, 279, 320)    # read as arrays, not as a first value
TIF_TILES = (322, 323, 324)     # present: a tiled TIFF
TIF_SIZE = {3: 2, 4: 4}
TIF_GREY = {1: 255, 2: 85, 4: 17, 8: 1}


def tiff_header(data, fsz):
    """THE HEADER (SPEC.md 106.25), as the part's HEAD walks it:
    1. the 8-byte header in the first head; IFD0's offset 8 or more;
    2. the IFD's count word (need(ifd, 2)), 1..170, then the whole IFD
       (need(ifd, 2 + 12 n)) - ONE head holds 170 entries;
    3. THE SCAN, in entry order, nothing read through a head: a tag of
       TIF_READ whose type is not SHORT or LONG, or whose count is 0, is
       `bad header`; a later entry of the same tag replaces an earlier;
       a tile tag (322-324) is noted, its type unread; any other tag is
       skipped;
    4. THE VALUES, in TIF_READ's order (ascending tag, whatever the IFD's),
       every tag present but the three arrays: its FIRST value, from the
       entry's own 4 bytes when count x size <= 4, else need(offset, size);
    5. THE CHECKS, in this order: width or length absent `bad header`; either
       outside 1..8192 `size not valid`; a tile tag `packing not read`;
       compression not 1/5/32773 `packing not read`; photometric absent
       `bad header`, not 0..3 `depth not read`; FillOrder not 1 `packing not
       read`; StripOffsets or StripByteCounts absent `bad header`;
       RowsPerStrip 0 `bad header` (above the height: the height);
       PlanarConfiguration not 1 (2 with one sample is 1) `packing not
       read`; Predictor not 1, or 2 on samples not of 8 bits, `packing not
       read`; the kind `depth not read`; a row over 32,768 bytes `too big to
       unpack`; for Palette: ColorMap absent, not SHORT, or its count not
       3 x 2^bits `bad header`, then need(offset, 6 x 2^bits) (one head);
       the strips: more than 1,024 `too big to unpack`, either array's count
       below the strips `bad header`, then StripOffsets' first n values and
       StripByteCounts' (each from the entry when count x size <= 4, else
       run(offset, n x size)), then each offset at or past the end of the
       strip before it (unbounded arithmetic) or `packing not read`."""
    hd = Heads(data)
    head = data[:HEAD_MAX]
    if len(head) < 8:
        raise Refused(PXD_HEAD)
    if head[:2] == b"II":
        be = False
    elif head[:2] == b"MM":
        be = True
    else:
        raise Refused(PXD_HEAD)

    def w16(b, o):
        return (b[o] << 8) | b[o + 1] if be else b[o] | (b[o + 1] << 8)

    def w32(b, o):
        return (w16(b, o) << 16) | w16(b, o + 2) if be else \
            w16(b, o) | (w16(b, o + 2) << 16)
    if w16(head, 2) != 42:
        raise Refused(PXD_HEAD)
    ifd = w32(head, 4)
    if ifd < 8:
        raise Refused(PXD_HEAD)
    n = w16(hd.need(ifd, 2), 0)
    if not 1 <= n <= 170:
        raise Refused(PXD_HEAD)
    ifdb = bytes(hd.need(ifd, 2 + 12 * n))
    ent = {}
    tiles = False
    for i in range(n):
        e = 2 + 12 * i
        tag, typ, cnt = w16(ifdb, e), w16(ifdb, e + 2), w32(ifdb, e + 4)
        if tag in TIF_TILES:
            tiles = True
        elif tag in TIF_READ:
            if typ not in TIF_SIZE or cnt == 0:
                raise Refused(PXD_HEAD)
            ent[tag] = (typ, cnt, ifdb[e + 8:e + 12])

    def val(b, typ):
        return w16(b, 0) if typ == 3 else w32(b, 0)
    v = {}
    for tag in TIF_READ:
        if tag in ent and tag not in TIF_ARRAYS:
            typ, cnt, f = ent[tag]
            sz = TIF_SIZE[typ]
            b = f if cnt * sz <= 4 else hd.need(w32(f, 0), sz)
            v[tag] = val(b, typ)
    if 256 not in v or 257 not in v:
        raise Refused(PXD_HEAD)
    w, h = v[256], v[257]
    if not (1 <= w <= DIM_MAX and 1 <= h <= DIM_MAX):
        raise Refused(PXD_DIMS)
    if tiles:
        raise Refused(PXD_PACK)
    comp = v.get(259, 1)
    if comp not in (1, 5, 32773):
        raise Refused(PXD_PACK)
    if 262 not in v:
        raise Refused(PXD_HEAD)
    photo = v[262]
    if photo > 3:
        raise Refused(PXD_DEPTH)
    if v.get(266, 1) != 1:
        raise Refused(PXD_PACK)
    if 273 not in ent or 279 not in ent:
        raise Refused(PXD_HEAD)
    rps = v.get(278, h)
    if rps == 0:
        raise Refused(PXD_HEAD)
    if rps > h:
        rps = h
    bits, spp = v.get(258, 1), v.get(277, 1)
    planar = v.get(284, 1)
    if planar != 1 and not (planar == 2 and spp == 1):
        raise Refused(PXD_PACK)
    pred = v.get(317, 1)
    if pred != 1 and not (pred == 2 and bits == 8):
        raise Refused(PXD_PACK)
    if photo == 2:
        ok = bits == 8 and spp in (3, 4)
    else:
        ok = spp == 1 and bits in (1, 2, 4, 8)
    if not ok:
        raise Refused(PXD_DEPTH)
    rb = (w * bits * spp + 7) // 8
    if rb > EX_ROWMAX:
        raise Refused(PXD_BIG)
    p = Pic()
    p.fmt, p.w, p.h, p.bits = "TIFF", w, h, bits * spp
    p.pack = {1: PK_NONE, 5: PK_LZW, 32773: PK_RLE}[comp]
    if photo == 3:
        if 320 not in ent:
            raise Refused(PXD_HEAD)
        typ, cnt, f = ent[320]
        ne = 1 << bits
        if typ != 3 or cnt != 3 * ne:
            raise Refused(PXD_HEAD)
        m = hd.need(w32(f, 0), 6 * ne)
        p.pal = [(w16(m, 2 * i) >> 8, w16(m, 2 * (ne + i)) >> 8,
                  w16(m, 2 * (2 * ne + i)) >> 8) for i in range(ne)]
        p.rf, p.npal = RF_IDX, ne
    elif photo == 2:
        p.rf = RF_RGB
    else:
        p.rf = RF_GREY
    ns = (h + rps - 1) // rps
    if ns > EX_STRIPS:
        raise Refused(PXD_BIG)
    if ent[273][1] < ns or ent[279][1] < ns:
        raise Refused(PXD_HEAD)

    def array(tag):
        typ, cnt, f = ent[tag]
        sz = TIF_SIZE[typ]
        b = f if cnt * sz <= 4 else hd.run(w32(f, 0), ns * sz)
        return [val(b[sz * i:sz * i + sz], typ) for i in range(ns)]
    offs = array(273)
    cnts = array(279)
    for i in range(1, ns):
        if offs[i] < offs[i - 1] + cnts[i - 1]:
            raise Refused(PXD_PACK)
    p.strips = list(zip(offs, cnts))
    p.rps, p.rb, p.comp, p.pred, p.spp, p.photo = rps, rb, comp, pred, spp, photo
    p.tbits = bits
    p.alpha = photo == 2 and spp == 4 and v.get(338) in (1, 2)
    p.heads = hd.n
    return p


def tiff_pixels(p, row):
    w, b = p.w, p.tbits
    if p.photo == 2:
        if p.spp == 3:
            return bytes(row[:3 * w])
        out = bytearray(3 * w)
        for x in range(w):
            px = row[4 * x:4 * x + 4]
            if p.alpha:
                a = px[3]
                out[3 * x:3 * x + 3] = bytes((blend(a, px[0]), blend(a, px[1]),
                                              blend(a, px[2])))
            else:
                out[3 * x:3 * x + 3] = px[:3]
        return bytes(out)
    vals = unpack_bits(row, w, b) if b < 8 else row[:w]
    if p.photo == 3:
        return bytes(vals)
    k = TIF_GREY[b]
    if p.photo == 0:
        return bytes(255 - x * k for x in vals)
    return bytes(x * k for x in vals)


def tiff_rows(p, rd):
    w, h, rb, rps, spp = p.w, p.h, p.rb, p.rps, p.spp
    y = 0
    for off, cnt in p.strips:
        nr = min(rps, h - y)
        rd.skip_to(off)
        src = Bytes(rd.d[off:off + cnt])
        if p.comp == 1:
            rows = [src.take(rb) for _ in range(nr)]
        elif p.comp == 32773:
            body = unpackbits(src, nr * rb)
            rows = [body[i * rb:(i + 1) * rb] for i in range(nr)]
        else:
            need = nr * rb
            buf = bytearray()

            def run(s):
                buf.extend(s)
                return len(buf) >= need
            if lzw_tiff(src.s, run) != "STOP":
                raise Refused(PXD_TRUNC)
            rows = [buf[i * rb:(i + 1) * rb] for i in range(nr)]
        for r in rows:
            r = bytearray(r)
            if p.pred == 2:
                for i in range(spp, rb):
                    r[i] = (r[i] + r[i - spp]) & 255
            p.rows.append((y, tiff_pixels(p, r)))
            y += 1


# --- ICO and CUR ----------------------------------------------------------------
def ico_header(data, fsz):
    """THE HEADER (SPEC.md 106.25): the directory from the first head; the
    image (largest w x h, a byte of 0 being 256; then the larger bit count,
    a CUR's counting 0; then the first); need(off, 4) for its first bytes:
    89 'PNG' is PNG-in-ICO - png_header on the head AT off - else a DIB:
    need(off, hsz) for its header, the checks, need(off, hsz + 4 n) for the
    palette; then, but for 32 bits, the AND mask's size (8,192 or `too big
    to unpack`) and run(mask, size) into the TABLE."""
    hd = Heads(data)
    head = data[:HEAD_MAX]
    if len(head) < 6:
        raise Refused(PXD_HEAD)
    typ, n = u16(head, 2), u16(head, 4)
    if u16(head, 0) != 0 or typ not in (1, 2) or not 1 <= n <= 127:
        raise Refused(PXD_HEAD)
    dsz = 6 + 16 * n
    if fsz < dsz:
        raise Refused(PXD_TRUNC)
    best, key = 0, None
    for i in range(n):
        e = 6 + 16 * i
        k = ((head[e] or 256) * (head[e + 1] or 256),
             u16(head, e + 6) if typ == 1 else 0)
        if key is None or k > key:
            best, key = i, k
    off = u32(head, 6 + 16 * best + 12)
    if off < dsz:
        raise Refused(PXD_HEAD)
    if off >= fsz:
        raise Refused(PXD_TRUNC)
    sig = hd.need(off, 4)
    if bytes(sig) == b"\x89PNG":
        p = png_header(data[off:off + HEAD_MAX], fsz - off)
        p.fmt = "ICO"
        p.sbase = off
        p.heads = hd.n
        return p
    hsz = u32(sig, 0)
    if hsz not in (40, 108, 124):
        raise Refused(PXD_HEAD)
    b = hd.need(off, hsz)
    w, h2 = s32(b, 4), s32(b, 8)
    planes, bits, comp, used = u16(b, 12), u16(b, 14), u32(b, 16), u32(b, 32)
    if not 1 <= w <= DIM_MAX:
        raise Refused(PXD_DIMS)
    if h2 < 0 or h2 & 1:
        raise Refused(PXD_HEAD)
    hh = h2 >> 1
    if not 1 <= hh <= DIM_MAX:
        raise Refused(PXD_DIMS)
    if planes > 1:
        raise Refused(PXD_HEAD)
    if bits not in (1, 4, 8, 24, 32):
        raise Refused(PXD_DEPTH)
    if comp != 0:
        raise Refused(PXD_PACK)
    ne = 0
    if bits <= 8:
        ne = used or (1 << bits)
        if ne > (1 << bits):
            raise Refused(PXD_HEAD)
    b = hd.need(off, hsz + 4 * ne)
    pal = [(b[hsz + 4 * i + 2], b[hsz + 4 * i + 1], b[hsz + 4 * i])
           for i in range(ne)]
    p = Pic()
    p.fmt, p.w, p.h, p.bits, p.pack = "ICO", w, hh, bits, PK_NONE
    p.xo = off + hsz + 4 * ne
    p.stride = ((w * bits + 31) // 32) * 4
    p.ms = ((w + 31) // 32) * 4
    p.mask = None
    if bits != 32:
        msz = hh * p.ms
        if msz > EX_TABLE:
            raise Refused(PXD_BIG)
        p.mask = hd.run(p.xo + hh * p.stride, msz)
    p.ne = ne
    if bits <= 8 and ne < 256:
        p.rf, p.pal, p.npal = RF_IDX, pal + [(BG, BG, BG)], ne + 1
    else:
        p.rf, p.pal = RF_RGB, pal
    p.heads = hd.n
    return p


def ico_rows(p, rd):
    if getattr(p, "sbase", None) is not None:
        return png_rows(p, Reader(rd.d[p.sbase:]))
    rd.skip_to(p.xo)
    w, hh, bits = p.w, p.h, p.bits
    for r in range(hh):
        raw = rd.take(p.stride)
        y = hh - 1 - r
        m = p.mask[r * p.ms:(r + 1) * p.ms] if p.mask is not None else None

        def masked(x):
            return (m[x >> 3] >> (7 - (x & 7))) & 1
        if bits <= 8:
            vals = unpack_bits(raw, w, bits) if bits < 8 else raw[:w]
            if p.rf == RF_IDX:
                out = bytes(p.ne if masked(x) else vals[x] for x in range(w))
            else:
                out = bytearray(3 * w)
                for x in range(w):
                    out[3 * x:3 * x + 3] = bytes((BG, BG, BG)) if masked(x) \
                        else bytes(p.pal[vals[x]])
        elif bits == 24:
            out = bytearray(3 * w)
            for x in range(w):
                out[3 * x:3 * x + 3] = bytes((BG, BG, BG)) if masked(x) else \
                    bytes((raw[3 * x + 2], raw[3 * x + 1], raw[3 * x]))
        else:
            out = bytearray(3 * w)
            for x in range(w):
                a = raw[4 * x + 3]
                out[3 * x:3 * x + 3] = bytes((blend(a, raw[4 * x + 2]),
                                              blend(a, raw[4 * x + 1]),
                                              blend(a, raw[4 * x])))
        p.rows.append((y, bytes(out)))


# --- IFF: ILBM and PBM ----------------------------------------------------------
def be16(b, o):
    return (b[o] << 8) | b[o + 1]


def lbm_header(data, fsz):
    """THE HEADER (SPEC.md 106.25): 'FORM', a size, 'ILBM' or 'PBM '; the
    chunks from 12, each header need(pos, 8), to BODY; BMHD's 20 bytes
    need(data, 20) (its size below 20 `bad header`; width and height checked
    there and then), CMAP's first 3 x min(size / 3, 256) need(data, ...) (a
    CMAP of under 3 bytes is a CMAP of no entries: every one black), CAMG's
    4 when its size is 4 or more; the next chunk at data + size + (size &
    1), unbounded (past the file is `cut short` at the next header). A later
    BMHD, CMAP or CAMG replaces an earlier. After the walk: compression,
    then planes (PBM: 8), then HAM."""
    hd = Heads(data)
    head = data[:HEAD_MAX]
    if len(head) < 12:
        raise Refused(PXD_HEAD)
    if head[:4] != b"FORM" or head[8:12] not in (b"ILBM", b"PBM "):
        raise Refused(PXD_HEAD)
    pbm = head[8:12] == b"PBM "
    pos = 12
    bm = cmap = None
    camg = 0
    while True:
        ch = hd.need(pos, 8)
        cid, sz = bytes(ch[:4]), be32(ch, 4)
        d = pos + 8
        if cid == b"BMHD":
            if sz < 20:
                raise Refused(PXD_HEAD)
            b = hd.need(d, 20)
            w, h = be16(b, 0), be16(b, 2)
            if not (1 <= w <= DIM_MAX and 1 <= h <= DIM_MAX):
                raise Refused(PXD_DIMS)
            bm = (w, h, b[8], b[9], b[10])
        elif cid == b"CMAP":
            k = min(sz // 3, 256)
            cmap = bytes(hd.need(d, 3 * k)) if k else b""
        elif cid == b"CAMG" and sz >= 4:
            camg = be32(hd.need(d, 4), 0)
        elif cid == b"BODY":
            if bm is None:
                raise Refused(PXD_HEAD)
            break
        pos = d + sz + (sz & 1)
    w, h, planes, masking, comp = bm
    if comp not in (0, 1):
        raise Refused(PXD_PACK)
    if pbm:
        if planes != 8:
            raise Refused(PXD_DEPTH)
    else:
        if not 1 <= planes <= 8:
            raise Refused(PXD_DEPTH)
        if camg & 0x800:
            raise Refused(PXD_DEPTH)
    ne = 256 if pbm else 1 << planes
    if cmap is None:
        pal = [(g, g, g) for g in (i * 255 // (ne - 1) for i in range(ne))]
    else:
        k = len(cmap) // 3
        pal = [tuple(cmap[3 * i:3 * i + 3]) if i < k else (0, 0, 0)
               for i in range(ne)]
    if not pbm and planes == 6 and camg & 0x80:
        for i in range(32):
            pal[32 + i] = tuple(c >> 1 for c in pal[i])
    p = Pic()
    p.fmt, p.w, p.h, p.bits, p.rf = "LBM", w, h, planes, RF_IDX
    p.pal, p.npal = pal, ne
    p.pack = PK_RLE if comp else PK_NONE
    p.pbm, p.planes, p.masking, p.comp = pbm, planes, masking, comp
    p.body, p.bsize = d, sz
    p.heads = hd.n
    return p


def lbm_rows(p, rd):
    rd.skip_to(p.body)
    w, h = p.w, p.h
    if p.pbm:
        rl = (w + 1) & ~1
    else:
        pb = ((w + 15) >> 4) * 2
        rl = pb * (p.planes + (1 if p.masking == 1 else 0))
    src = Bytes(rd.d[p.body:p.body + p.bsize])
    if p.comp:
        body = unpackbits(src, rl * h)
    else:
        body = src.take(rl * h)
    for y in range(h):
        r = body[y * rl:(y + 1) * rl]
        if p.pbm:
            p.rows.append((y, bytes(r[:w])))
            continue
        out = bytearray(w)
        for pl in range(p.planes):
            pr = r[pl * pb:(pl + 1) * pb]
            for x in range(w):
                out[x] |= ((pr[x >> 3] >> (7 - (x & 7))) & 1) << pl
        p.rows.append((y, bytes(out)))


# --- MacPaint -------------------------------------------------------------------
MAC_W, MAC_H, MAC_RB = 576, 720, 72
BITS8 = [bytes((b >> (7 - i)) & 1 for i in range(8)) for b in range(256)]


def mac_header(data, fsz):
    start = 128 if fsz >= 69 and data[0] == 0 and data[65:69] == b"PNTG" \
        else 0
    if fsz < start + 512:
        raise Refused(PXD_TRUNC)
    p = Pic()
    p.fmt, p.w, p.h, p.bits, p.rf = "MAC", MAC_W, MAC_H, 1, RF_IDX
    p.pal, p.npal, p.pack = [(255, 255, 255), (0, 0, 0)], 2, PK_RLE
    p.off = start + 512
    p.heads = 1
    return p


def mac_rows(p, rd):
    rd.skip_to(p.off)
    body = unpackbits(Bytes(rd.d[p.off:]), MAC_RB * MAC_H)
    for y in range(MAC_H):
        p.rows.append((y, b"".join(map(BITS8.__getitem__,
                                       body[y * MAC_RB:(y + 1) * MAC_RB]))))


# =============================================================================
# GIF ANIMATION (SPEC.md 106.25): the images after the first, played on the
# 1/1 master. gif_anim() is the first pass, frame by frame; the guest is held
# to it a frame at a time
# =============================================================================
def gif_delay(d):
    """A GCE's delay in hundredths -> ticks of 18.2 a second."""
    if d < 2:
        d = 10
    return max(1, (d * 182 + 500) // 1000)


def _gif_exts(d, pos, st):
    """The blocks from pos to the next image descriptor: its offset (at the
    2Ch), or None - the trailer, the file's end, or any other byte, which end
    the pass. A GCE's sub-block of four bytes or more sets st's disposal,
    delay and transparent index (gif_rows' rule); a NETSCAPE2.0 extension's
    second sub-block, 3 bytes or more and starting 01, sets st['loop']."""
    while True:
        if pos >= len(d) or d[pos] != 0x21:
            return pos if pos < len(d) and d[pos] == 0x2C else None
        if pos + 1 >= len(d):
            return None
        label = d[pos + 1]
        pos += 2
        k = 0
        ns = False
        while True:
            if pos >= len(d):
                return None
            n = d[pos]
            pos += 1
            if n == 0:
                break
            if pos + n > len(d):
                return None
            blk = d[pos:pos + n]
            pos += n
            if label == 0xF9 and n >= 4:
                st["disp"] = (blk[0] >> 2) & 7
                st["delay"] = blk[1] | (blk[2] << 8)
                st["trans"] = blk[3] if blk[0] & 1 else None
            elif label == 0xFF:
                if k == 0:
                    ns = n == 11 and bytes(blk) == b"NETSCAPE2.0"
                elif k == 1 and ns and n >= 3 and blk[0] == 1:
                    st["loop"] = blk[1] | (blk[2] << 8)
            k += 1


def _gif_image(d, at):
    """The image whose descriptor is at `at`: (left, top, w, h, packed,
    table or None, offset after the table) - or None when the descriptor
    or its local table is past the file."""
    if at + 10 > len(d):
        return None
    left, top, fw, fh, fpk = u16(d, at + 1), u16(d, at + 3), u16(d, at + 5), \
        u16(d, at + 7), d[at + 9]
    pos = at + 10
    tab = None
    if fpk & 0x80:
        n = 2 << (fpk & 7)
        if pos + 3 * n > len(d):
            return None
        tab = [tuple(d[pos + 3 * i:pos + 3 * i + 3]) for i in range(n)]
        pos += 3 * n
    return left, top, fw, fh, fpk, tab, pos


def _gif_data(d, pos):
    """The minimum code size byte and the sub-blocks after it: (mincode,
    the code bytes, the offset after the terminator - None when the file
    ended first), or None when there is no mincode byte."""
    if pos >= len(d):
        return None
    mc = d[pos]
    pos += 1
    flat = bytearray()
    while True:
        if pos >= len(d):
            return mc, flat, None
        n = d[pos]
        pos += 1
        if n == 0:
            return mc, flat, pos
        got = d[pos:pos + n]
        pos += len(got)
        flat += got
        if len(got) < n:
            return mc, flat, None


def _gif_rect(left, top, fw, fh, sw, sh):
    """A frame's rect clipped to the screen, inclusive, or None."""
    if fw == 0 or fh == 0 or left >= sw or top >= sh:
        return None
    return left, top, min(left + fw, sw) - 1, min(top + fh, sh) - 1


def _rect_union(a, b):
    if a is None:
        return b
    if b is None:
        return a
    return (min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]))


def _gif_draw(master, sw, sh, img, mp, trans, mc, flat):
    """Frame pixels onto the master: each WHOLE row at its final y, clipped,
    a pixel equal to `trans` left as it is, every other one mp[index].
    True when every row was made; False when the stream ended or was
    damaged first (the rows already written stay)."""
    left, top, fw, fh, fpk = img[:5]
    order = gif_frame_rows(fh, bool(fpk & 0x40))
    st = {"i": 0, "row": bytearray()}

    def place(s):
        k = 0
        while k < len(s):
            take = min(fw - len(st["row"]), len(s) - k)
            st["row"] += s[k:k + take]
            k += take
            if len(st["row"]) == fw:
                y = top + order[st["i"]]
                if y < sh:
                    row = st["row"]
                    base = y * sw
                    for j in range(min(fw, sw - left) if left < sw else 0):
                        v = row[j]
                        if v != trans:
                            master[base + left + j] = mp[v]
                st["i"] += 1
                st["row"] = bytearray()
                if st["i"] == fh:
                    return True
        return False
    try:
        return lzw_decode(flat, mc, place) == "STOP"
    except Refused:
        return False


def _gif_nearest(c, pal, npal):
    best, bi = None, 0
    for i in range(npal):
        q = pal[i]
        e = 3 * (c[0] - q[0]) ** 2 + 6 * (c[1] - q[1]) ** 2 + (c[2] - q[2]) ** 2
        if best is None or e < best:
            best, bi = e, i
    return bi


def gif_scan(data):
    """The walk the first decode makes, and on: the global table, frame 0's
    image and GCE state, the loop count, and where frame 0's sub-blocks end
    (None when the file ended inside them)."""
    pk = data[10]
    gn = (2 << (pk & 7)) if pk & 0x80 else 0
    gpal = [tuple(data[13 + 3 * i:16 + 3 * i]) for i in range(gn)] or None
    st = {"disp": 0, "delay": 0, "trans": None, "loop": None}
    at = _gif_exts(data, 13 + 3 * gn, st)
    img = _gif_image(data, at)
    dat = _gif_data(data, img[6])
    return gpal, img, st, dat[2]


def gif_animated(data):
    """THE FLAG the first decode sets after its last row: a second image
    descriptor after the first image's sub-blocks, with only extensions
    between (walked by their lengths). The trailer, the file's end or any
    other byte: not animated. Never a refusal; the picture is unchanged."""
    _, _, _, pos = gif_scan(data)
    if pos is None:
        return False
    return _gif_exts(data, pos, {}) is not None


def gif_anim(data):
    """(loop, frames): `loop` the NETSCAPE2.0 count before the first image
    (0 forever, n passes after the first) or None (plays once); `frames` the
    FIRST pass, frame 0 first, each a dict:
      master    bytes, sw x sh: the 1/1 master after this frame is drawn
                (frame 0's is emit(decode(data), 0)'s)
      rect      the dirty rect (x1, y1, x2, y2) inclusive, or None: the
                previous frame's disposal rect when its disposal is 2 or 3,
                union this frame's rect, both clipped; frame 0's is the
                whole screen
      delay     ticks (gif_delay of its GCE's delay)
      disposal  its GCE's disposal, 0..7 (0 without a GCE)
      frame     (left, top, w, h) as its descriptor says
      whole     False for the frame that ENDED THE PASS part-drawn (its
                whole rows written; always the last in the list)
    A GIF that is not animated answers frame 0 alone. Raises Refused when
    the first decode refuses."""
    p0 = decode(data, "GIF")
    if p0.fmt != "GIF":
        raise Refused(PXD_HEAD)
    m0 = bytes(emit(p0, 0)[0])
    sw, sh = p0.w, p0.h
    pal, npal = p0.pal, p0.npal
    gpal, img0, st0, pos = gif_scan(data)
    B = st0["trans"] if st0["trans"] is not None else data[11]
    g0 = not img0[4] & 0x80              # frame 0 drew with the global table
    frames = [dict(master=m0, rect=(0, 0, sw - 1, sh - 1),
                   delay=gif_delay(st0["delay"]), disposal=st0["disp"],
                   frame=img0[:4], whole=True)]
    master = bytearray(m0)
    prev_rect = _gif_rect(img0[0], img0[1], img0[2], img0[3], sw, sh)
    prev_disp = st0["disp"]
    prev_save = None                     # frame 0's "before": all B
    ident = list(range(256))
    while pos is not None:
        st = {"disp": 0, "delay": 0, "trans": None}
        at = _gif_exts(data, pos, st)
        if at is None:
            break
        img = _gif_image(data, at)
        if img is None:
            break
        left, top, fw, fh, fpk, tab, pos = img
        if fw == 0 or fh == 0:           # skipped: its table, its data
            dat = _gif_data(data, pos)
            if dat is None:
                break
            pos = dat[2]
            continue
        if tab is None and gpal is None:
            break
        dat = _gif_data(data, pos)
        if dat is None or not 2 <= dat[0] <= 8:
            break
        mc, flat, pos = dat
        # the frame begins: the previous one's disposal, then what this
        # one's rect holds (for its own disposal 3), then its pixels
        dirty = None
        if prev_disp in (2, 3) and prev_rect is not None:
            x1, y1, x2, y2 = prev_rect
            for y in range(y1, y2 + 1):
                for x in range(x1, x2 + 1):
                    master[y * sw + x] = B if prev_save is None or \
                        prev_disp == 2 else prev_save[(y - y1) * (x2 - x1 + 1)
                                                      + x - x1]
            dirty = prev_rect
        rect = _gif_rect(left, top, fw, fh, sw, sh)
        save = None
        if rect is not None:
            x1, y1, x2, y2 = rect
            save = bytes(master[y * sw + x] for y in range(y1, y2 + 1)
                         for x in range(x1, x2 + 1))
        if tab is None and g0:
            mp = ident
        else:
            t = (tab if tab is not None else gpal)
            t = t + [(0, 0, 0)] * (256 - len(t))
            cache = {}
            mp = []
            for c in t:
                if c not in cache:
                    cache[c] = _gif_nearest(c, pal, npal)
                mp.append(cache[c])
        whole = _gif_draw(master, sw, sh, img, mp, st["trans"], mc, flat)
        frames.append(dict(master=bytes(master), rect=_rect_union(dirty, rect),
                           delay=gif_delay(st["delay"]), disposal=st["disp"],
                           frame=(left, top, fw, fh), whole=whole))
        if not whole:
            break
        prev_rect, prev_disp, prev_save = rect, st["disp"], save
    return (st0["loop"], frames)


def gif_restart(data):
    """The master when a pass begins again: the WHOLE screen disposed to the
    background index B, then frame 0 drawn by the frame rules (its
    transparent pixels leave B). It must be frame 0's master."""
    p0 = decode(data, "GIF")
    sw, sh = p0.w, p0.h
    gpal, img0, st0, _ = gif_scan(data)
    B = st0["trans"] if st0["trans"] is not None else data[11]
    master = bytearray([B]) * (sw * sh)
    mc, flat, _ = _gif_data(data, img0[6])
    _gif_draw(master, sw, sh, img0, list(range(256)), st0["trans"], mc, flat)
    return bytes(master)


# =============================================================================
# decode: a file's bytes -> a Pic with its rows, or Refused
# =============================================================================
def decode(data, ext="", scale=None):
    """`scale` is a JPEG's: it is decoded straight to it (SPEC.md 106.19),
    and None is the finest it may be shown at (1/4 for a progressive one);
    every other format's rows are the source's, and emit() scales them."""
    ext = ext.upper()
    fmt = sniff(data, ext)
    head = data[:HEAD_MAX]
    fsz = len(data)
    if fmt == "BMP":
        p = bmp_header(head, fsz)
        rows = bmp_rows
    elif fmt == "PCX":
        tail = data[-769:] if fsz >= 769 else None
        p = pcx_header(head, fsz, tail)
        rows = pcx_rows
    elif fmt == "TGA":
        p = tga_header(head, fsz)
        rows = tga_rows
    elif fmt == "PNM":
        p = pnm_header(head, fsz)
        rows = pnm_rows
    elif fmt == "PIX":
        p = pix_header(head, fsz)
        rows = pix_rows
    elif fmt == "GIF":
        p = gif_header(head, fsz)
        rows = gif_rows
    elif fmt == "PNG":
        p = png_header(head, fsz)
        rows = png_rows
    elif fmt == "TIFF":
        p = tiff_header(data, fsz)
        rows = tiff_rows
    elif fmt == "ICO":
        p = ico_header(data, fsz)
        rows = ico_rows
    elif fmt == "LBM":
        p = lbm_header(data, fsz)
        rows = lbm_rows
    elif fmt == "MAC":
        p = mac_header(data, fsz)
        rows = mac_rows
    elif fmt == "JPEG":
        p = jpeg_header(data, fsz)
        jpeg_rows(p, Reader(data), p.smin if scale is None else scale)
        return p
    elif fmt == "":
        raise Refused(PXD_NOTPIC)
    else:
        raise Refused(PXD_NOTYET)
    rows(p, Reader(data))
    return p


# =============================================================================
# THE EMITTER (SPEC.md 106.8): rows -> the master, at a scale
# =============================================================================
def mode_for(p, s):
    if p.rf == RF_RGB or (p.rf == RF_IDX and s):
        return PM_CUBE
    return PM_GREY if p.rf == RF_GREY else PM_PAL


# THE CUBE BY ORDERED DITHER (SPEC.md 106.8). A 16x16 BLUE-NOISE threshold
# matrix - void-and-cluster ranks 0..255 (Ulichney's construction, a Gaussian
# of sigma 1.5 on the torus, seeded by a fixed LCG; a constant here because
# the guest ships it) - cut to SIXTEEN classes, rank >> 4, with ONE threshold
# a pixel shared by its three channels. A level is (n v + T16[k]) div 255,
# n = 5 (six levels) or 6 (seven).
#
# WHY NOT THE WINDOW'S OWN 8x8 BAYER, OR A SMALL MATRIX: the master is
# dithered once and then shown through a second dither, the window's Bayer
# (106.11), which at 100% is phase-locked to it. The same Bayer in the master
# renders a tone between two cube levels as max(t0, 64f) rather than t0 +
# f (t1 - t0) - flat, then steep, which on a 1bpp display blows out a sky;
# and a 3x3 matrix beats against the period 8 in diagonal hatching. Blue
# noise has period 16, a multiple of 8, so nothing beats, and its classes are
# near-independent of Bayer's, so the second dither sees the right tone: on a
# grey ramp through a 1bpp display it errs by 1.9 per cent of white against
# Floyd-Steinberg's 1.2, and on VGA the two are hard to tell apart. It costs
# an 8088 a lookup a channel, where Floyd-Steinberg cost 1,361 cycles a pixel
# - which is why the guest no longer has one.
BLUE16 = [
    [ 19,  90, 218,  32, 119, 250,  57, 213, 116, 247,  28, 128, 167, 251, 139,  71],
    [149,  45, 195, 165,  70, 202,  87, 171,   1,  69, 184,  48,  80, 192,  38, 222],
    [103, 245, 123,   4, 106, 146,  23, 237, 101, 141, 231, 113, 215,   6, 121, 176],
    [ 13, 186,  67, 212, 234,  42, 188,  52, 154, 205,  18,  63, 160,  95, 233,  59],
    [134,  86, 153,  27, 172,  94, 129, 223,  75,  35, 178, 131, 244,  43, 145, 199],
    [254,  41, 221, 120,  54, 248,  17, 162, 115, 252,  92, 201,  15, 173,  83,  29],
    [159, 108, 191,  74, 144, 200,  84, 214,   7, 137,  49,  72, 111, 225, 127, 211],
    [ 60,  22, 232,   5, 180,  31, 110,  64, 189, 169, 235, 147, 183,  55,   3,  96],
    [240, 136, 163, 100, 126, 217, 152, 242,  39, 102,  21, 208,  34, 249, 166, 194],
    [ 44,  85, 207,  40, 246,  56,  14, 132,  81, 224, 124,  66,  99, 140,  79, 122],
    [179, 227,  10,  73, 170,  91, 182, 204, 161,   2, 193, 155, 228,  12, 203,  25],
    [156, 105, 143, 196, 114, 236,  30,  68, 109, 255,  88,  47, 177, 112, 239,  62],
    [219,  33, 253,  53,  20, 151, 125, 216,  51, 138,  26, 209,  78,  36, 148,  93],
    [198,  77, 130, 168, 210,  82, 190,   9, 174, 226, 164, 118, 243, 133, 175,   0],
    [117, 181,  11,  97, 230,  46, 104, 238,  76,  98,  58,   8, 187,  65, 229,  50],
    [158, 241,  61, 142, 185,  16, 157, 135,  37, 197, 150, 220,  89,  24, 107, 206],
]
T16 = [(255 * (2 * k + 1)) // 32 for k in range(16)]


def qclass(x, y):
    return BLUE16[y & 15][x & 15] >> 4


class Ordered:
    """The cube by ordered dither: a pixel is three lookups and two adds.
    `y` is the MASTER row, so a bottom-up file dithers as a top-down one."""

    def __init__(self, mw):
        pass

    def row(self, rgb, mw, y):
        out = bytearray(mw)
        brow = BLUE16[y & 15]
        for x in range(mw):
            t = T16[brow[x & 15] >> 4]
            qr = (5 * rgb[3 * x] + t) // 255
            qg = (6 * rgb[3 * x + 1] + t) // 255
            qb = (5 * rgb[3 * x + 2] + t) // 255
            out[x] = (qr * 7 + qg) * 6 + qb
        return out


def emit(p, s, pal_out=None):
    """The master (bytes), its size and mode, and its palette."""
    mw, mh = p.w >> s, p.h >> s
    mode = mode_for(p, s)
    master = bytearray(mw * mh)
    pal = p.pal + [(0, 0, 0)] * (256 - len(p.pal))
    q = Ordered(mw) if mode == PM_CUBE else None
    n = 1 << s
    nb, sh = n, 2 * s               # rows a block sums, and the shift
    if s and p.par is not None:     # an interlaced picture below 1/1: the
        nb, sh = n >> 1, 2 * s - 1  # rows of one parity (SPEC.md 106.18)
    acc, cnt = None, 0
    if getattr(p, "presc", None) is not None:
        # a JPEG's rows are the MASTER's already, at its scale and upright
        assert p.presc == s, "a JPEG decoded at 1/%d emitted at 1/%d" % (
            1 << p.presc, 1 << s)
        for y, row in p.rows:
            out = q.row(row, mw, y) if mode == PM_CUBE else row[:mw]
            master[y * mw:(y + 1) * mw] = out
        return master, mw, mh, mode, (CUBE if mode == PM_CUBE else GREY)
    for y, row in p.rows:
        if y >= mh * n:
            continue
        if s and p.par is not None and (y & 1) != p.par:
            continue
        if p.rf == RF_IDX and s:
            rgb = bytearray(3 * p.w)
            for x in range(p.w):
                rgb[3 * x:3 * x + 3] = bytes(pal[row[x]])
            row, rf = rgb, RF_RGB
        else:
            rf = p.rf
        nch = 3 if rf == RF_RGB else 1
        if s == 0:
            out = q.row(row, mw, y) if mode == PM_CUBE else row[:mw]
            master[y * mw:(y + 1) * mw] = out
            continue
        if cnt == 0:
            acc = [0] * (mw * nch)
        for X in range(mw):
            for c in range(nch):
                t = 0
                for k in range(n):
                    t += row[(X * n + k) * nch + c]
                acc[X * nch + c] += t
        cnt += 1
        if cnt == nb:
            avg = bytes(a >> sh for a in acc)
            r = y >> s
            out = q.row(avg, mw, r) if mode == PM_CUBE else avg
            master[r * mw:(r + 1) * mw] = out
            cnt = 0
    if mode == PM_CUBE:
        opal = CUBE
    elif mode == PM_GREY:
        opal = GREY
    else:
        opal = pal
    return master, mw, mh, mode, opal


# =============================================================================
# THE MIXING PLANS (SPEC.md 106.11)
# =============================================================================
WGT = (3, 6, 1)
PEN_SH = 5


def plan_for(T):
    """(c1, c2, t) for one target colour over the EGA sixteen."""
    best, c1 = None, 0
    for c in range(16):
        d = sum(WGT[k] * (T[k] - EGA16[c][k]) ** 2 for k in range(3))
        if best is None or d < best:
            best, c1 = d, c
    C1 = EGA16[c1]
    dd = [T[k] - C1[k] for k in range(3)]
    e0 = sum(WGT[k] * ((64 * dd[k]) >> 2) ** 2 for k in range(3))
    bestE, bc2, bt = e0, c1, 0
    for c2 in range(16):
        if c2 == c1:
            continue
        kk = [(EGA16[c2][k] - C1[k]) // 85 for k in range(3)]
        K = sum(WGT[k] * kk[k] * kk[k] for k in range(3))
        S = sum(WGT[k] * dd[k] * kk[k] for k in range(3))
        if S <= 0:
            continue
        num, den = 64 * S, 85 * K
        t = (num + den // 2) // den
        if t > 64:
            t = 64
        if t == 0:
            continue
        E = sum(WGT[k] * ((64 * dd[k] - 85 * kk[k] * t) >> 2) ** 2
                for k in range(3))
        E += (1849600 * K) >> PEN_SH
        if E < bestE:
            bestE, bc2, bt = E, c2, t
    return c1, bc2, bt


# THE GUEST'S SEARCH ORDER (pxview.inc's px_plan1): the same answer as
# plan_for, reached with less work, and `--selfcheck` holds the two equal
# over a grid of colours. Every c2 is tried in order of its K - the pair's
# contrast, whose penalty 57,800 K alone is a floor under its error - so the
# first K whose penalty exceeds the best error found ends the search; a
# pair's error is summed G, R, B and dropped once it exceeds the best; and
# a tie goes to the LOWER c2, which is plan_for's strict < in index order.
def _lev(c, k):
    return EGA16[c][k] // 85


PORD = []                       # per c1: the other fifteen, by (K, c2)
for _c1 in range(16):
    _o = []
    for _c2 in range(16):
        if _c2 != _c1:
            _kk = [_lev(_c2, k) - _lev(_c1, k) for k in range(3)]
            _o.append((sum(WGT[k] * _kk[k] ** 2 for k in range(3)), _c2))
    PORD.append(sorted(_o))
KMAX = 3 * 9 + 6 * 9 + 9
PENK = [(1849600 * K) >> PEN_SH for K in range(KMAX + 1)]


def plan_fast(T):
    best, c1 = None, 0
    for c in range(16):
        d = sum(WGT[k] * (T[k] - EGA16[c][k]) ** 2 for k in range(3))
        if best is None or d < best:
            best, c1 = d, c
    dd = [T[k] - EGA16[c1][k] for k in range(3)]
    bestE, bc2, bt, cand = 256 * best, c1, 0, False
    for K, c2 in PORD[c1]:
        if PENK[K] > bestE:
            break
        kk = [_lev(c2, k) - _lev(c1, k) for k in range(3)]
        S = sum(WGT[k] * dd[k] * kk[k] for k in range(3))
        if S <= 0:
            continue
        den = 85 * K
        t = min((64 * S + den // 2) // den, 64)
        if t == 0:
            continue
        E = PENK[K]
        for k in (1, 0, 2):
            E += WGT[k] * ((64 * dd[k] - 85 * kk[k] * t) >> 2) ** 2
            if E > bestE:
                break
        else:
            if E < bestE or (E == bestE and cand and c2 < bc2):
                bestE, bc2, bt, cand = E, c2, t, True
    return c1, bc2, bt


def plans(pal):
    return [plan_for(pal[i]) if i < len(pal) else (0, 0, 0)
            for i in range(256)]


def luma(rgb):
    return (77 * rgb[0] + 150 * rgb[1] + 29 * rgb[2]) >> 8


def gam1(y):
    """1bpp's gamma: halfway between linear and square, so a lit fraction
    reads as the grey it stands for on a CRT without crushing shadows."""
    return (y + (y * y + 127) // 255) >> 1


def thresh1(pal):
    return [(64 * gam1(luma(pal[i])) + 127) // 255 if i < len(pal) else 0
            for i in range(256)]


# =============================================================================
# THE VIEW (SPEC.md 106.11): zoom, aspect, steps, size, pan
# =============================================================================
def div_floor(a, b):
    return a // b


def fit_z(mw, mh, s, cw, ch, aspect):
    """Fit (SPEC.md 106.11): the largest z up to 1 that shows the whole
    picture, IN CLOSED FORM from the steps the view takes (106.23): hs =
    2^32 div (z << s) >= ceil(mw 2^16 / cw) keeps the width inside, vs = hs
    ad div an >= ceil(mh 2^16 / ch) the height. It was min(cw 2^16 / sw, ch
    ad 2^16 / (sh an)), whose view could floor its steps into a picture a
    row longer than the canvas (one case in seven, the CGA the worst), so
    the master's last row never showed at Fit."""
    an, ad = aspect
    hx = -(-(mw << 16) // cw)
    vy = -(-(mh << 16) // ch)
    hy = -(-(vy * an) // ad)
    z = min(65536, ((1 << 32) // max(hx, hy, 1)) >> s)
    return max(z, 1)


class View:
    def __init__(self, mw, mh, s, z, aspect, canvas, ox=None, oy=None):
        an, ad = aspect
        self.mw, self.mh, self.s, self.z = mw, mh, s, z
        self.hstep = (1 << 32) // (z << s)
        self.vstep = (self.hstep * ad) // an
        self.dw = -(-(mw << 16) // self.hstep)
        self.dh = -(-(mh << 16) // self.vstep)
        cx1, cy1, cx2, cy2 = canvas
        cw, ch = cx2 - cx1 + 1, cy2 - cy1 + 1
        self.canvas = canvas
        self.ox, self.oy = self.clamp(cw, ch, ox or 0, oy or 0)
        self.ix = cx1 + self.ox
        self.iy = cy1 + self.oy

    def clamp(self, cw, ch, ox, oy):
        """A side smaller than the canvas is centred (across, on the byte
        grid); a larger one is clamped so the canvas stays covered, and its
        offset across is a multiple of 8 (floored)."""
        if self.dw <= cw:
            ox = ((cw - self.dw) >> 1) & ~7
        else:
            lo = -(((self.dw - cw) >> 3) << 3)
            ox = max(lo, min(0, ox & ~7))
        if self.dh <= ch:
            oy = (ch - self.dh) >> 1
        else:
            oy = max(ch - self.dh, min(0, oy))
        return ox, oy

    def master_xy(self, x, y):
        return ((x - self.ix) * self.hstep) >> 16, ((y - self.iy) * self.vstep) >> 16


class GuestView:
    """A view given by its numbers, as a guest's VW record holds them - so a
    test renders exactly what the guest's composer was told to."""

    def __init__(self, ix, iy, hstep, vstep, dw, dh):
        self.ix, self.iy, self.hstep, self.vstep = ix, iy, hstep, vstep
        self.dw, self.dh = dw, dh

    def master_xy(self, x, y):
        return ((x - self.ix) * self.hstep) >> 16, \
            ((y - self.iy) * self.vstep) >> 16


def render(master, mw, mh, pal, depth, view, rect, ground):
    """{(x, y): colour} for a screen rect: a 4bpp index, or 0/1 (lit)."""
    if depth == 4:
        pl = CUBE_PLANS if pal is CUBE else GREY_PLANS if pal is GREY \
            else plans(pal)
    else:
        th = thresh1(pal)
    out = {}
    x1, y1, x2, y2 = rect
    for y in range(y1, y2 + 1):
        for x in range(x1, x2 + 1):
            if not (view.ix <= x < view.ix + view.dw and
                    view.iy <= y < view.iy + view.dh):
                out[(x, y)] = ground
                continue
            mx, my = view.master_xy(x, y)
            idx = master[my * mw + mx]
            b = BAYER8[(y - view.iy) & 7][x & 7]
            if depth == 4:
                c1, c2, t = pl[idx]
                out[(x, y)] = c2 if b < t else c1
            else:
                out[(x, y)] = 1 if b < th[idx] else 0
    return out


# =============================================================================
# THE HISTOGRAM (SPEC.md 106.12)
# =============================================================================
def isqrt(n):
    if n < 2:
        return n
    x = n
    y = (x + 1) // 2
    while y < x:
        x = y
        y = (x + n // x) // 2
    return x


def hist_counts(master):
    h = [0] * 256
    for v in master:
        h[v] += 1
    return h


def hist_stats(counts, pal, chan):
    """chan 0 luma, 1 red, 2 green, 3 blue -> (bins, mean, sd, min, max)."""
    bins = [0] * 256
    for i in range(256):
        if counts[i]:
            c = pal[i] if i < len(pal) else (0, 0, 0)
            v = luma(c) if chan == 0 else c[chan - 1]
            bins[v] += counts[i]
    n = sum(bins)
    if not n:
        return bins, 0, 0, 0, 0
    s1 = sum(v * bins[v] for v in range(256))
    mean = s1 // n
    s2 = sum(bins[v] * (v - mean) ** 2 for v in range(256))
    sd = isqrt(s2 // n)
    lo = min(v for v in range(256) if bins[v])
    hi = max(v for v in range(256) if bins[v])
    return bins, mean, sd, lo, hi


# =============================================================================
# --gen: the shipped plans (apps/pixel/pxplans.inc)
# =============================================================================
CUBE_PLANS = plans(CUBE)
GREY_PLANS = plans(GREY)


def plans_inc():
    out = ["; =============================================================================",
           "; os8088 - apps/pixel/pxplans.inc - GENERATED by tools/pixelsim.py --gen",
           ";",
           "; The mixing plans (SPEC.md 106.11) of the two palettes that never change:",
           "; the CUBE's and GREY's. Per index three bytes - c1, c2, t - in index order.",
           "; Then the search's own tables. A PAL palette's plans are that search's,",
           "; run by the decoder PART of the picture's kind (apps/pixel/pxplan.inc,",
           "; SPEC.md 106.20), which every decoder part includes - this file with it;",
           "; `pixelsim --selfcheck` (a fast row) fails the build if this file is not",
           "; what that search answers. Do not edit it by hand.",
           "; ============================================================================="]
    for name, pl in (("px_plcube", CUBE_PLANS), ("px_plgrey", GREY_PLANS)):
        out.append("%s:" % name)
        for i in range(0, 256, 8):
            out.append("    db " + ", ".join("%d,%d,%d" % pl[j]
                                              for j in range(i, i + 8)))
    out.append("; the plan search's ORDER (SPEC.md 106.11): per c1, the other fifteen")
    out.append("; by their K, and the K's; then 85 K and the penalty 57,800 K a K")
    out.append("px_pord:")
    for c1 in range(16):
        out.append("    db " + ", ".join("%d" % c2 for K, c2 in PORD[c1]))
    out.append("px_pordk:")
    for c1 in range(16):
        out.append("    db " + ", ".join("%d" % K for K, c2 in PORD[c1]))
    out.append("; ...each pair's k a channel, as (k + 3) x 2 (a word index)")
    out.append("px_pki:")
    for c1 in range(16):
        out.append("    db " + ", ".join(
            ",".join("%d" % ((_lev(c2, k) - _lev(c1, k) + 3) * 2) for k in range(3))
            for K, c2 in PORD[c1]))
    out.append("px_pk85:")
    for i in range(0, KMAX + 1, 13):
        out.append("    dw " + ", ".join("%d" % (85 * K) for K in range(i, min(i + 13, KMAX + 1))))
    out.append("px_pk578:")
    for i in range(0, KMAX + 1, 7):
        out.append("    dd " + ", ".join("%d" % PENK[K] for K in range(i, min(i + 7, KMAX + 1))))
    return "\n".join(out) + "\n"


def qtab_inc():
    """apps/pixel/pxqtab.inc: the ordered quantiser's tables, which stay in
    the resident package while the plans went to the decoder parts
    (SPEC.md 106.20)."""
    out = ["; =============================================================================",
           "; os8088 - apps/pixel/pxqtab.inc - GENERATED by tools/pixelsim.py --gen",
           ";",
           "; The ordered quantiser's blue-noise classes and thresholds (SPEC.md",
           "; 106.8), resident: the emitter's. `pixelsim --selfcheck` (a fast row)",
           "; fails the build if this file is not what pixelsim's BLUE16 and T16",
           "; say. Do not edit it by hand.",
           "; =============================================================================",
           "; per master row & 15 and column & 15, the first of its class's three",
           "; table pages (class x 3)..."]
    out.append("px_qpage:")
    for y in range(16):
        out.append("    db " + ", ".join("%d" % (3 * qclass(x, y))
                                         for x in range(16)))
    out.append("; ...and each class's threshold, T16")
    out.append("px_qt16:")
    out.append("    db " + ", ".join("%d" % t for t in T16))
    return "\n".join(out) + "\n"


# =============================================================================
# FULL SCREEN (SPEC.md 106.23): every mode PiXEL draws when the screen is its
# own, as the FULL-SCREEN part (apps/pixel/pxfull.asm) draws it - the view,
# the median cut, the plan search over any colours, the CGA palette's choice,
# the diffuser and the ordered dither - to the colour code of every pixel.
# tests/pxfsx.py reads the machine's video memory and DAC back against this.
# =============================================================================
# apps/pixel/pxfs.inc's PXM_*: the order a display's default is taken in
FSM_MODEX, FSM_VGA13, FSM_VGA12, FSM_DESK, FSM_C160, FSM_CGA320, \
    FSM_CGA640, FSM_HERC = range(8)
FSM_N = 8
FSM_NAMES = ["320x240, 256", "320x200, 256", "640x480, 16", "Desktop, 16",
             "160x100, 16", "320x200, 4", "640x200, 2", "720x348, 2"]
# the screen and its pixel's aspect a = an / ad, width over height (106.11's
# meaning): 13h, C160 and the CGA's 320 are 5/6, the CGA's 640 is 5/12, the
# EGA's desktop 35/48
FS_GEOM = [(320, 240, 1, 1), (320, 200, 5, 6), (640, 480, 1, 1),
           (640, 350, 35, 48), (160, 100, 5, 6), (320, 200, 5, 6),
           (640, 200, 5, 12), (720, 348, 29, 45)]
FS_256 = (FSM_MODEX, FSM_VGA13)
FS_MONO = (FSM_CGA640, FSM_HERC)
FS_BAND = 10                    # a caption band's rows (the part's PF_BAND)
# the CGA's RGBI sixteen are os8088's own, brown included; in the DAC's six
# bits a level is 0, 21, 42 or 63
CGA6 = [tuple(v >> 2 for v in c) for c in EGA16]
# the three sets of 320x200's foreground colours, in the order the chooser
# tries them: palette 1, palette 0, and mode 5's cyan, red and white
CGA_SETS = ((1, (3, 5, 7)), (0, (2, 4, 6)), (5, (3, 4, 7)))


def lum6(c):
    return 77 * c[0] + 150 * c[1] + 29 * c[2]


def fs_fit(mw, mh, W, H, an, ad):
    """Fit: the LARGEST zoom (16.16, screen pixels a master pixel across) at
    which the whole master shows. In closed form from the steps the view
    takes - hs >= ceil(mw 2^16 / W) keeps dw <= W, and vs = hs ad // an >=
    ceil(mh 2^16 / H) keeps dh <= H - so no row hangs past the screen."""
    hx = -(-(mw << 16) // W)
    vy = -(-(mh << 16) // H)
    hy = -(-(vy * an) // ad)
    return max(1, (1 << 32) // max(hx, hy, 1))


def fs_place(d, S, o):
    """A side's offset: centred when it fits (o None or not), else o clamped
    so the screen stays covered."""
    if d <= S:
        return (S - d) >> 1
    return max(S - d, min(0, o if o is not None else (S - d) >> 1))


class FsView:
    def __init__(self, mw, mh, mode, z, ox=None, oy=None, geom=None):
        W, H, an, ad = geom or FS_GEOM[mode]
        self.W, self.H = W, H
        self.z = z
        self.hs = (1 << 32) // z
        self.vs = (self.hs * ad) // an
        self.dw = -(-(mw << 16) // self.hs)
        self.dh = -(-(mh << 16) // self.vs)
        self.ox = fs_place(self.dw, W, ox)
        self.oy = fs_place(self.dh, H, oy)


def fs_zstep(z, up, mw, mh, mode):
    """The next of ZSTEPS above (up) or below z - z itself when there is
    none, or when that step would make a side of 32,768 or more."""
    for s in (ZSTEPS if up else ZSTEPS[::-1]):
        if s > z if up else s < z:
            v = FsView(mw, mh, mode, s)
            return s if v.dw < 32768 and v.dh < 32768 else z
    return z


def fs_weights(counts, n):
    """The histogram as weights under 2^16 in all: each count shifted right
    until the picture's pixels would be under 32,768, at least 1 where any."""
    sh = 0
    while (n >> sh) > 32767:
        sh += 1
    return [0 if c == 0 else max(1, c >> sh) for c in counts]


def fs_mediancut(pal, w, nmax=16):
    """THE ADAPTIVE SIXTEEN (SPEC.md 106.23): the palette's used entries as
    points weighted by their pixels; the box with the largest weight x
    weighted range (WGT, the plans' weights) is cut on that channel at its
    weighted MEAN - entries at or below it first, in palette order - until
    there are sixteen or nothing can be cut. Each box is its weighted mean,
    rounded. out (colours, their weights)."""
    boxes = [[i for i in range(256) if w[i]]]
    while len(boxes) < nmax:
        best = None
        for bi, b in enumerate(boxes):
            if len(b) < 2:
                continue
            rk = None
            for k in range(3):
                lo = min(pal[i][k] for i in b)
                hi = max(pal[i][k] for i in b)
                r = WGT[k] * (hi - lo)
                if rk is None or r > rk[0]:
                    rk = (r, k)
            if rk[0] == 0:
                continue
            pr = sum(w[i] for i in b) * rk[0]
            if best is None or pr > best[0]:
                best = (pr, bi, rk[1])
        if best is None:
            break
        _, bi, k = best
        b = boxes[bi]
        m = sum(w[i] * pal[i][k] for i in b) // sum(w[i] for i in b)
        boxes[bi:bi + 1] = [[i for i in b if pal[i][k] <= m],
                            [i for i in b if pal[i][k] > m]]
    cols, wts = [], []
    for b in boxes:
        W = sum(w[i] for i in b)
        cols.append(tuple((sum(w[i] * pal[i][k] for i in b) + W // 2) // W
                          for k in range(3)))
        wts.append(W)
    return cols, wts


def plan6(T, cols):
    """106.11's plan over ANY colours, in the DAC's six bits: (c1, c2, t, E).
    c1 the nearest (WGT, ties to the lower); each other c2 by the projection
    of T - c1 onto c2 - c1, t = round(64 S / K) clipped to 64, its error
    sum WGT (64 d - D t)^2 plus the contrast penalty 128 K - 106.11's own
    weights and penalty, scaled to six bits; a strictly lower error wins."""
    best, c1 = None, 0
    for i, C in enumerate(cols):
        d = sum(WGT[k] * (T[k] - C[k]) ** 2 for k in range(3))
        if best is None or d < best:
            best, c1 = d, i
    C1 = cols[c1]
    dd = [T[k] - C1[k] for k in range(3)]
    bestE = sum(WGT[k] * (64 * dd[k]) ** 2 for k in range(3))
    bc2, bt = c1, 0
    for c2, C2 in enumerate(cols):
        if c2 == c1:
            continue
        D = [C2[k] - C1[k] for k in range(3)]
        K = sum(WGT[k] * D[k] * D[k] for k in range(3))
        S = sum(WGT[k] * dd[k] * D[k] for k in range(3))
        if K == 0 or S <= 0:
            continue
        t = min(64, (64 * S + K // 2) // K)
        if t == 0:
            continue
        E = sum(WGT[k] * (64 * dd[k] - D[k] * t) ** 2 for k in range(3))
        E += 128 * K
        if E < bestE:
            bestE, bc2, bt = E, c2, t
    return c1, bc2, bt, bestE


def fs_cgapick(cols, wts):
    """320x200's four (SPEC.md 106.23): every background x {palette 1,
    palette 0, mode 5} x {high, low} scored against the adaptive colours,
    each its weight x (its best plan's error >> 8) over the four; the lowest
    score, the first in that order on a tie. out (set, high, bg, the four)."""
    T = [tuple(v >> 2 for v in c) for c in cols]
    best = None
    for st, fg in CGA_SETS:
        for hi in (1, 0):
            four = [None] + [c + 8 * hi for c in fg]
            for bg in range(16):
                four[0] = bg
                cc = [CGA6[c] for c in four]
                s = sum(w * (plan6(t, cc)[3] >> 8) for t, w in zip(T, wts))
                if best is None or s < best[0]:
                    best = (s, st, hi, bg, list(four))
    return best[1:]


class FsPic:
    """What the part makes of the shown picture for one mode: the colours
    the mode shows (6-bit), and per palette entry a plan or a level."""

    def __init__(self, master, mw, mh, pal, mode, ordered=False):
        self.master, self.mw, self.mh, self.mode = master, mw, mh, mode
        self.ordered = ordered
        pal = list(pal) + [(0, 0, 0)] * (256 - len(pal))
        cnt = hist_counts(master)
        self.cols6 = None
        self.cga = None
        if mode in FS_256:
            self.cols6 = [tuple(v >> 2 for v in c) for c in pal]
            self.kind = "256"
        elif mode in FS_MONO:
            self.kind = "mono"
            self.c1, self.c2 = [0] * 256, [1] * 256
            self.T = [gam1(luma(pal[i])) if cnt[i] else 0 for i in range(256)]
            self.t = [(64 * self.T[i] + 127) // 255 for i in range(256)]
            self.ground, self.light = 0, 1
            return
        else:
            self.kind = "plan"
            if mode == FSM_VGA12:
                cols, _ = fs_mediancut(pal, fs_weights(cnt, mw * mh))
                self.cols6 = [tuple(v >> 2 for v in c) for c in cols]
            elif mode == FSM_CGA320:
                cols, wts = fs_mediancut(pal, fs_weights(cnt, mw * mh))
                self.cga = fs_cgapick(cols, wts)
                self.cols6 = [CGA6[c] for c in self.cga[3]]
            else:                           # C160, the EGA's desktop
                self.cols6 = list(CGA6)
            self.c1, self.c2, self.t, self.T = [0] * 256, [0] * 256, \
                [0] * 256, [0] * 256
            for i in range(256):
                if cnt[i]:
                    c1, c2, t, _ = plan6(tuple(v >> 2 for v in pal[i]),
                                         self.cols6)
                    self.c1[i], self.c2[i], self.t[i] = c1, c2, t
                    self.T[i] = (t * 255 + 32) >> 6
        lum = [lum6(c) for c in self.cols6]
        self.ground = lum.index(min(lum))
        self.light = lum.index(max(lum))

    def frame(self, v, y0=0, y1=None, err=None):
        """Rows y0..y1-1 of the screen as colour codes (an index into
        cols6, or 0/1 lit on a mono mode). A full frame starts its diffusion
        at zero; a band starts from `err`, the state the full frame had
        there (the part keeps it for the caption band). Returns (rows, err)
        - err the state before row y1."""
        W, H = v.W, v.H
        if y1 is None:
            y1 = H
        err = list(err) if err is not None else [0] * W
        xa, xb = max(0, v.ox), min(W, v.ox + v.dw)
        rows = []
        for y in range(y0, y1):
            row = [self.ground] * W
            ky = y - v.oy
            if 0 <= ky < v.dh and xa < xb:
                my = (ky * v.vs) >> 16
                base = my * self.mw
                acc = (xa - v.ox) * v.hs
                idx = []
                for x in range(xa, xb):
                    idx.append(self.master[base + (acc >> 16)])
                    acc += v.hs
                if self.kind == "256":
                    row[xa:xb] = idx
                elif self.ordered:
                    brow = BAYER8[y & 7]
                    for k, i in enumerate(idx):
                        x = xa + k
                        row[x] = self.c2[i] if brow[x & 7] < self.t[i] \
                            else self.c1[i]
                else:
                    nxt = [0] * W
                    carry = 0
                    for k, i in enumerate(idx):
                        x = xa + k
                        val = self.T[i] + err[x] + carry
                        if val >= 128:
                            row[x], e = self.c2[i], val - 255
                        else:
                            row[x], e = self.c1[i], val
                        q = e >> 2
                        if x > xa:
                            nxt[x - 1] += q
                        nxt[x] += q
                        carry = e - 2 * q
                    err = nxt
            rows.append(row)
        return rows, err

    def rgb(self, code):
        """A colour code as 8-bit RGB, the DAC's six bits widened."""
        if self.kind == "mono":
            return (255, 255, 255) if code else (0, 0, 0)
        return tuple((v << 2) | (v >> 4) for v in self.cols6[code])


def fs_frame(master, mw, mh, pal, mode, ordered=False, z=None, ox=None,
             oy=None):
    """The whole screen of a mode at Fit (or zoom z): (FsPic, FsView, rows)."""
    W, H, an, ad = FS_GEOM[mode]
    fp = FsPic(master, mw, mh, pal, mode, ordered)
    v = FsView(mw, mh, mode, z or fs_fit(mw, mh, W, H, an, ad), ox, oy)
    rows, _ = fp.frame(v)
    return fp, v, rows


def cmd_fsrender(path, out, mode, scale, ordered):
    data = open(path, "rb").read()
    p = decode(data, os.path.splitext(path)[1][1:], scale)
    master, mw, mh, _, pal = emit(p, scale)
    fp, v, rows = fs_frame(master, mw, mh, pal, mode, ordered)
    W, H, an, ad = FS_GEOM[mode]
    # widened to the screen's own 4:3, so a look sees the shape the glass has
    sy = max(1, round(W * 3 / 4 / H))
    out_rows = []
    for r in rows:
        line = b"".join(bytes(fp.rgb(c)) for c in r)
        out_rows += [line] * sy
    write_png(out, W, H * sy, out_rows)
    extra = ""
    if fp.cga:
        extra = " set %d %s bg %d" % (fp.cga[0], "high" if fp.cga[1] else "low",
                                      fp.cga[2])
    print("%s: %s master %dx%d -> %s, z %d, %dx%d at (%d, %d)%s" % (
        path, FSM_NAMES[mode], mw, mh, out, v.z, v.dw, v.dh, v.ox, v.oy,
        extra))


# =============================================================================
# --render: a PNG of what PiXEL shows (for looking, on the host)
# =============================================================================
def write_png(path, w, h, rgbrows):
    raw = b"".join(b"\x00" + bytes(r) for r in rgbrows)

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def cmd_render(path, out, depth, aspect, cw, ch, scale):
    data = open(path, "rb").read()
    p = decode(data, os.path.splitext(path)[1][1:])
    master, mw, mh, mode, pal = emit(p, scale)
    asp = ASPECT[aspect]
    z = fit_z(mw, mh, scale, cw, ch, asp)
    v = View(mw, mh, scale, z, asp, (0, 0, cw - 1, ch - 1))
    px = render(master, mw, mh, pal, depth, v, (0, 0, cw - 1, ch - 1),
                8 if depth == 4 else 1)
    rows = []
    for y in range(ch):
        r = bytearray()
        for x in range(cw):
            c = px[(x, y)]
            r += bytes(EGA16[c] if depth == 4 else ((255, 255, 255) if c else (0, 0, 0)))
        rows.append(r)
    write_png(out, cw, ch, rows)
    print("%s: %s %dx%d -> master %dx%d mode %d, view %dx%d" %
          (path, p.fmt, p.w, p.h, mw, mh, mode, v.dw, v.dh))


# =============================================================================
# --selfcheck
# =============================================================================
# =============================================================================
# EDITING (SPEC.md 106.24): the palette operations, the pixel operations and
# the five writers - each the integer arithmetic apps/pixel/pxedit.asm and
# apps/pixel/pxwrite.asm do, so tests/pxedit.py and tests/pxsave.py hold the
# guest to these byte for byte
# =============================================================================

def pal_full(pal):
    return list(pal) + [(0, 0, 0)] * (256 - len(pal))


def clamp8(v):
    return 0 if v < 0 else 255 if v > 255 else v


# --- the palette operations: 256 entries in, 256 out -------------------------
def pal_invert(pal):
    return [(255 - r, 255 - g, 255 - b) for r, g, b in pal_full(pal)]


def pal_grey(pal):
    return [(luma(c),) * 3 for c in pal_full(pal)]


SEPIA = ((50, 98, 24), (45, 88, 21), (35, 68, 17))     # the matrix, x 128


def pal_sepia(pal):
    out = []
    for r, g, b in pal_full(pal):
        out.append(tuple(min(255, (k[0] * r + k[1] * g + k[2] * b) >> 7)
                         for k in SEPIA))
    return out


def bc_factor(c):
    """Contrast c (-90..90, a step of 10) as an 8.8 factor."""
    return 25600 // (100 - c) if c >= 0 else (256 * (100 + c)) // 100


def bc_offset(b):
    """Brightness b (-100..100) as an offset, rounded toward zero."""
    o = abs(b) * 51 // 20
    return o if b >= 0 else -o


def pal_bricon(pal, b, c):
    f, o = bc_factor(c), bc_offset(b)
    return [tuple(clamp8((((v - 128) * f) >> 8) + 128 + o) for v in e)
            for e in pal_full(pal)]


GAMMAS = [30, 40, 50, 60, 70, 80, 90, 100, 120, 140, 160, 180, 200, 250, 300]


def gamma_curve(g):
    """33 words, v = 8i, of 255 (v / 255) ^ (100 / g) - the last at v = 256,
    one past the range, so the top segment interpolates like the rest: what
    the part interpolates between (apps/pixel/pxgam.inc, made by --gen)."""
    return [int(round(255.0 * (8.0 * i / 255.0) ** (100.0 / g)))
            for i in range(33)]


def gamma_lut(g):
    c = gamma_curve(g)
    return [min(255, c[v >> 3] + (((c[(v >> 3) + 1] - c[v >> 3]) * (v & 7)
                                   + 4) >> 3)) for v in range(256)]


def pal_gamma(pal, g):
    t = gamma_lut(g)
    return [(t[r], t[gg], t[b]) for r, gg, b in pal_full(pal)]


def pal_poster(pal, n):
    def q(v):
        k = (v * (n - 1) + 127) // 255
        return (k * 255 + (n - 1) // 2) // (n - 1)
    return [(q(r), q(g), q(b)) for r, g, b in pal_full(pal)]


def pal_thresh(pal, t):
    return [((255,) * 3 if luma(c) >= t else (0,) * 3) for c in pal_full(pal)]


def levels_points(counts, pal, ch):
    """A channel's 1% and 99% points over the picture's pixels."""
    bins = [0] * 256
    for i in range(256):
        bins[pal[i][ch]] += counts[i]
    n = sum(bins)
    lo_n = n // 100
    hi_n = n - n // 100
    acc, lo, hi = 0, 0, 255
    for v in range(256):
        acc += bins[v]
        if acc > lo_n:
            lo = v
            break
    acc = 0
    for v in range(256):
        acc += bins[v]
        if acc >= hi_n:
            hi = v
            break
    return lo, hi


def pal_levels(pal, counts):
    pal = pal_full(pal)
    pts = [levels_points(counts, pal, ch) for ch in range(3)]
    out = []
    for e in pal:
        c = []
        for ch in range(3):
            lo, hi = pts[ch]
            v = e[ch]
            if hi <= lo:
                c.append(v)
            elif v <= lo:
                c.append(0)
            elif v >= hi:
                c.append(255)
            else:
                c.append((v - lo) * 255 // (hi - lo))
        out.append(tuple(c))
    return out


# --- the pixel operations: (master, w, h) in, (master, w, h, mode) out -------
def grey_pal(pal):
    return all(r == g == b for r, g, b in pal_full(pal))


def requant(rgbrows, w, h, pal):
    """RGB rows into a master: GREY when the palette is all greys (the
    operations keep a grey a grey), else the cube by the ordered dither at
    the DESTINATION's own place (SPEC.md 106.8)."""
    if grey_pal(pal):
        return bytes(b for row in rgbrows for b in row[0::3]), PM_GREY
    q = Ordered(w)
    return b"".join(bytes(q.row(row, w, y)) for y, row in enumerate(rgbrows)), \
        PM_CUBE


def op_rotate(m, w, h, k):
    """k = 1 a quarter clockwise, 3 anticlockwise, 2 a half turn."""
    if k == 2:
        return bytes(reversed(m)), w, h
    out = bytearray(w * h)
    for y in range(w):              # the destination is h wide, w tall
        for x in range(h):
            if k == 1:
                out[y * h + x] = m[(h - 1 - x) * w + y]
            else:
                out[y * h + x] = m[x * w + (w - 1 - y)]
    return bytes(out), h, w


def op_flip(m, w, h, across):
    rows = [m[y * w:(y + 1) * w] for y in range(h)]
    if across:
        rows = [bytes(reversed(r)) for r in rows]
    else:
        rows.reverse()
    return b"".join(rows), w, h


def op_crop(m, w, h, x1, y1, x2, y2):
    return b"".join(m[y * w + x1:y * w + x2 + 1] for y in range(y1, y2 + 1)), \
        x2 - x1 + 1, y2 - y1 + 1


RESIZE_STEPS = [(1, 4), (1, 3), (1, 2), (2, 3), (3, 4), (3, 2), (2, 1),
                (3, 1), (4, 1)]


def resize_dims(w, h, num, den):
    return max(1, w * num // den), max(1, h * num // den)


def op_resize(m, w, h, pal, num, den):
    pal = pal_full(pal)
    nw, nh = resize_dims(w, h, num, den)
    rows = []
    if num < den:                   # down: a box of the source a pixel
        for y in range(nh):
            y0 = y * h // nh
            y1 = max(y0 + 1, (y + 1) * h // nh)
            row = bytearray()
            for x in range(nw):
                x0 = x * w // nw
                x1 = max(x0 + 1, (x + 1) * w // nw)
                n = (x1 - x0) * (y1 - y0)
                s = [0, 0, 0]
                for yy in range(y0, y1):
                    for xx in range(x0, x1):
                        c = pal[m[yy * w + xx]]
                        s[0] += c[0]; s[1] += c[1]; s[2] += c[2]
                row += bytes((v + n // 2) // n for v in s)
            rows.append(row)
    else:                           # up: bilinear, in 8.8
        sx = (w << 8) // nw
        sy = (h << 8) // nh

        def place(i, step, lim):
            f = ((step * (2 * i + 1)) >> 1) - 128
            if f < 0:
                f = 0
            i0 = f >> 8
            if i0 >= lim - 1:
                return lim - 1, lim - 1, 0
            return i0, i0 + 1, f & 255
        for y in range(nh):
            ya, yb, wy = place(y, sy, h)
            row = bytearray()
            for x in range(nw):
                xa, xb, wx = place(x, sx, w)
                for ch in range(3):
                    a = pal[m[ya * w + xa]][ch]
                    b = pal[m[ya * w + xb]][ch]
                    c = pal[m[yb * w + xa]][ch]
                    d = pal[m[yb * w + xb]][ch]
                    top = a * (256 - wx) + b * wx
                    bot = c * (256 - wx) + d * wx
                    row.append((top * (256 - wy) + bot * wy + 32768) >> 16)
            rows.append(row)
    out, mode = requant(rows, nw, nh, pal)
    return out, nw, nh, mode


KERNELS = {                         # (taps row-major, shift, bias)
    "blur": ((1, 2, 1, 2, 4, 2, 1, 2, 1), 4, 8),
    "sharpen": ((0, -1, 0, -1, 5, -1, 0, -1, 0), 0, 0),
    "edge": ((-1, -1, -1, -1, 8, -1, -1, -1, -1), 0, 0),
    "emboss": ((-2, -1, 0, -1, 1, 1, 0, 1, 2), 0, 0),
}


def op_conv(m, w, h, pal, kind):
    pal = pal_full(pal)
    k, sh, bias = KERNELS[kind]
    rows = []
    for y in range(h):
        row = bytearray()
        ys = [min(max(y + d, 0), h - 1) for d in (-1, 0, 1)]
        for x in range(w):
            xs = [min(max(x + d, 0), w - 1) for d in (-1, 0, 1)]
            for ch in range(3):
                s = 0
                for j in range(3):
                    for i in range(3):
                        s += k[3 * j + i] * pal[m[ys[j] * w + xs[i]]][ch]
                row.append(clamp8((s + bias) >> sh))
        rows.append(row)
    out, mode = requant(rows, w, h, pal)
    return out, w, h, mode


PIXELATE = 8


def op_pixelate(m, w, h, pal, n=PIXELATE):
    pal = pal_full(pal)
    rows = [bytearray(3 * w) for _ in range(h)]
    for by in range(0, h, n):
        for bx in range(0, w, n):
            ye, xe = min(by + n, h), min(bx + n, w)
            cnt = (ye - by) * (xe - bx)
            s = [0, 0, 0]
            for y in range(by, ye):
                for x in range(bx, xe):
                    c = pal[m[y * w + x]]
                    s[0] += c[0]; s[1] += c[1]; s[2] += c[2]
            avg = bytes((v + cnt // 2) // cnt for v in s)
            for y in range(by, ye):
                for x in range(bx, xe):
                    rows[y][3 * x:3 * x + 3] = avg
    out, mode = requant(rows, w, h, pal)
    return out, w, h, mode


# --- the writers (apps/pixel/pxwrite.asm) -------------------------------------
def write_bmp8(m, w, h, pal):
    pal = pal_full(pal)
    stride = (w + 3) & ~3
    data = bytearray()
    for y in range(h - 1, -1, -1):
        data += m[y * w:(y + 1) * w] + b"\0" * (stride - w)
    off = 14 + 40 + 1024
    hdr = b"BM" + struct.pack("<IHHI", off + len(data), 0, 0, off)
    hdr += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 8, 0, len(data),
                       2835, 2835, 256, 0)
    hdr += b"".join(bytes((b, g, r, 0)) for r, g, b in pal)
    return hdr + bytes(data)


def write_bmp24(m, w, h, pal):
    pal = pal_full(pal)
    stride = (3 * w + 3) & ~3
    data = bytearray()
    for y in range(h - 1, -1, -1):
        line = bytearray()
        for x in range(w):
            r, g, b = pal[m[y * w + x]]
            line += bytes((b, g, r))
        data += line + b"\0" * (stride - len(line))
    hdr = b"BM" + struct.pack("<IHHI", 54 + len(data), 0, 0, 54)
    hdr += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(data),
                       2835, 2835, 0, 0)
    return hdr + bytes(data)


def write_pcx8(m, w, h, pal):
    pal = pal_full(pal)
    bpl = (w + 1) & ~1
    hdr = bytearray(128)
    hdr[0:4] = bytes((10, 5, 1, 8))
    struct.pack_into("<HHHHHH", hdr, 4, 0, 0, w - 1, h - 1, 72, 72)
    hdr[65] = 1
    struct.pack_into("<HH", hdr, 66, bpl, 1)
    out = bytearray(hdr)
    for y in range(h):
        line = m[y * w:(y + 1) * w] + b"\0" * (bpl - w)
        i = 0
        while i < bpl:
            v = line[i]
            n = 1
            while i + n < bpl and n < 63 and line[i + n] == v:
                n += 1
            if n > 1 or v >= 0xC0:
                out += bytes((0xC0 | n, v))
            else:
                out.append(v)
            i += n
    out.append(12)
    out += b"".join(bytes(c) for c in pal)
    return bytes(out)


def lzw_encode(data, mincode=8):
    """GIF's LZW, giflib's encoder rule for rule: a Clear first, the width
    raised once a code has been written at it while the next free code has
    reached its top, and a Clear when the next free code reaches 4095. Greedy
    LZW's output is the table policy's alone, so the guest's hash table and
    this dict write the same bytes."""
    clr = 1 << mincode
    eoi = clr + 1
    out = bytearray()
    acc = [0, 0]                        # bits, count
    st = {"run": eoi + 1, "bits": mincode + 1}

    def put(code):
        acc[0] |= code << acc[1]
        acc[1] += st["bits"]
        while acc[1] >= 8:
            out.append(acc[0] & 255)
            acc[0] >>= 8
            acc[1] -= 8
        if st["run"] >= (1 << st["bits"]) and code <= 4095 and st["bits"] < 12:
            st["bits"] += 1
    table = {}
    put(clr)
    cur = data[0]
    for c in data[1:]:
        k = (cur << 8) | c
        if k in table:
            cur = table[k]
            continue
        put(cur)
        cur = c
        if st["run"] >= 4095:
            put(clr)
            st["run"], st["bits"] = eoi + 1, mincode + 1
            table = {}
        else:
            table[k] = st["run"]
            st["run"] += 1
    put(cur)
    put(eoi)
    if acc[1]:
        out.append(acc[0] & 255)
    return bytes(out)


def write_gif(m, w, h, pal):
    pal = pal_full(pal)
    out = bytearray(b"GIF87a")
    out += struct.pack("<HHBBB", w, h, 0xF7, 0, 0)
    out += b"".join(bytes(c) for c in pal)
    out += b"\x2C" + struct.pack("<HHHHB", 0, 0, w, h, 0) + b"\x08"
    z = lzw_encode(m)
    for i in range(0, len(z), 255):
        blk = z[i:i + 255]
        out.append(len(blk))
        out += blk
    out += b"\x00\x3B"
    return bytes(out)


# deflate: fixed Huffman (RFC 1951 3.2.6) over a hash-chain LZ77 - window
# 4,095 bytes, 4,096 hash heads, a chain of at most DF_CHAIN, a match of
# 3..258 bytes, the longest kept (the nearest on a tie), and every position
# hashed after it is searched
DF_DIST = 4095
DF_CHAIN = 8
DF_GOOD = 32
LBASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43,
         51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258]
LEXT = [0] * 8 + [1] * 4 + [2] * 4 + [3] * 4 + [4] * 4 + [5] * 4 + [0]
DBASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257,
         385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289,
         16385, 24577]
DEXT = [0, 0, 0, 0] + [k for k in range(1, 14) for _ in (0, 1)]


def df_hash(d, i):
    return ((d[i] << 8) ^ (d[i + 1] << 4) ^ d[i + 2]) & 4095


def deflate_fixed(d):
    out = bytearray()
    acc = [0, 0]

    def bits(v, n):                     # LSB first
        acc[0] |= v << acc[1]
        acc[1] += n
        while acc[1] >= 8:
            out.append(acc[0] & 255)
            acc[0] >>= 8
            acc[1] -= 8

    def huff(code, n):                  # a Huffman code goes MSB first
        r = 0
        for _ in range(n):
            r = (r << 1) | (code & 1)
            code >>= 1
        bits(r, n)

    def lit(v):
        if v < 144:
            huff(0x30 + v, 8)
        elif v < 256:
            huff(0x190 + v - 144, 9)
        elif v < 280:
            huff(v - 256, 7)
        else:
            huff(0xC0 + v - 280, 8)
    bits(1, 1)                          # BFINAL
    bits(1, 2)                          # BTYPE 01, fixed
    n = len(d)
    head = [-1] * 4096
    prev = [-1] * 4096
    i = 0

    def insert(p):
        if p + 2 < n:
            hh = df_hash(d, p)
            prev[p & 4095] = head[hh]
            head[hh] = p
    while i < n:
        best, bdist = 0, 0
        if i + 2 < n:
            c = head[df_hash(d, i)]
            chain = DF_CHAIN
            lim = min(258, n - i)
            while c >= 0 and i - c <= DF_DIST and chain:
                ln = 0
                while ln < lim and d[c + ln] == d[i + ln]:
                    ln += 1
                if ln > best:
                    best, bdist = ln, i - c
                    if ln >= DF_GOOD or ln == lim:
                        break
                c = prev[c & 4095]
                chain -= 1
        if best >= 3:
            k = 0
            while LBASE[k + 1] <= best if k + 1 < len(LBASE) else False:
                k += 1
            lit(257 + k)
            if LEXT[k]:
                bits(best - LBASE[k], LEXT[k])
            j = 0
            while j + 1 < len(DBASE) and DBASE[j + 1] <= bdist:
                j += 1
            huff(j, 5)
            if DEXT[j]:
                bits(bdist - DBASE[j], DEXT[j])
            for p in range(i, i + best):
                insert(p)
            i += best
        else:
            lit(d[i])
            insert(i)
            i += 1
    lit(256)
    if acc[1]:
        out.append(acc[0] & 255)
    return bytes(out)


def adler32(d):
    a, b = 1, 0
    for v in d:
        a = (a + v) % 65521
        b = (b + a) % 65521
    return (b << 16) | a


PNG_IDAT = 8192                         # the IDAT chunks' data, at most


def write_png8(m, w, h, pal, mode):
    """Colour type 3 with a 256-entry PLTE, or type 0 on a GREY master;
    filter None on every row; one zlib stream of one fixed-Huffman block,
    cut into IDAT chunks of PNG_IDAT bytes."""
    raw = b"".join(b"\0" + m[y * w:(y + 1) * w] for y in range(h))
    z = b"\x78\x01" + deflate_fixed(raw) + struct.pack(">I", adler32(raw))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    grey = mode == PM_GREY
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(
        ">IIBBBBB", w, h, 8, 0 if grey else 3, 0, 0, 0))
    if not grey:
        out += chunk(b"PLTE", b"".join(bytes(c) for c in pal_full(pal)))
    for i in range(0, len(z), PNG_IDAT):
        out += chunk(b"IDAT", z[i:i + PNG_IDAT])
    return out + chunk(b"IEND", b"")


def pix_nearest(pal):
    """Each entry's nearest of os8088's sixteen, the squared distance
    unweighted and the lower index on a tie (tools/os88pix.py's rule)."""
    out = []
    for c in pal_full(pal):
        best, bd = 0, None
        for i, e in enumerate(EGA16):
            d = sum((c[k] - e[k]) ** 2 for k in range(3))
            if bd is None or d < bd:
                best, bd = i, d
        out.append(best)
    return out


PIX_MAX = 0xFFFF


def write_pix(m, w, h, pal):
    """os8088's own archive (SPEC.md 61.7), one picture, number 1: packed
    4bpp, the high nibble leftmost, in the fixed sixteen. None if a block
    of it would not fit one segment."""
    stride = (w + 1) // 2
    if stride * h > PIX_MAX:
        return None
    nm = pix_nearest(pal)
    buf = bytearray(32 + stride * h)
    buf[0:5] = b"O8PIX"
    buf[5] = 1
    struct.pack_into("<HHH", buf, 6, 1, 0, 16)
    struct.pack_into("<HHHHI", buf, 16, 1, w, h, stride, 32)
    for y in range(h):
        for x in range(0, w, 2):
            hi = nm[m[y * w + x]]
            lo = nm[m[y * w + x + 1]] if x + 1 < w else 0
            buf[32 + y * stride + (x >> 1)] = (hi << 4) | lo
    return bytes(buf)


WRITERS = {"BMP": write_bmp8, "BMP24": write_bmp24, "PCX": write_pcx8,
           "GIF": write_gif, "PIX": write_pix}


def write_as(fmt, m, w, h, pal, mode):
    if fmt == "PNG":
        return write_png8(m, w, h, pal, mode)
    return WRITERS[fmt](m, w, h, pal)


def gam_inc():
    out = ["; " + "=" * 77,
           "; os8088 - apps/pixel/pxgam.inc - GENERATED by tools/pixelsim.py --gen",
           ";",
           "; Effects > Gamma's curves (SPEC.md 106.24): for each of pixelsim's",
           "; GAMMAS, 33 words of 255 (8i / 255) ^ (100 / g), i = 0..32, which the",
           "; EDIT part interpolates between (gamma_lut). Do not edit it by hand.",
           "; " + "=" * 77,
           "pe_gam:"]
    for g in GAMMAS:
        c = gamma_curve(g)
        out.append("    dw " + ", ".join(str(v) for v in c[:17]) + "    ; %d.%02d" % (g // 100, g % 100))
        out.append("    dw " + ", ".join(str(v) for v in c[17:]))
    return "\n".join(out) + "\n"


def editcheck():
    """The wave-7 references against themselves and the readers: each
    operation's invariants, and every writer's file read back by pixelsim's
    own decoder (and Pillow's, when it is here) as the master it was made
    from. Too slow for the fast tier; tests/pxedit.py and pxsave.py run it."""
    bad = []

    def ok(c, what):
        if not c:
            bad.append(what)
    pal = [((i * 37) & 255, (i * 91) & 255, (i * 13) & 255) for i in range(256)]
    ok(pal_invert(pal_invert(pal)) == pal, "invert twice")
    ok(all(a == b == c for a, b, c in pal_grey(pal)), "greyscale is grey")
    ok(pal_bricon(pal, 0, 0) == pal, "brightness/contrast 0, 0 is nothing")
    ok(pal_gamma(GREY, 100) == GREY, "gamma 1.0 is nothing")
    ok(all(len(set(c)) == 1 and c[0] in (0, 255) for c in pal_thresh(pal, 128)),
       "threshold is black and white")
    ok(len(set(pal_poster(GREY, 4))) == 4, "posterize 4 is four levels")
    w, h = 13, 7
    m = bytes(((x * 7 + y * 31) ^ (x * y)) & 255 for y in range(h) for x in range(w))
    for k, back in ((1, 3), (3, 1), (2, 2)):
        r, rw, rh = op_rotate(m, w, h, k)
        ok(op_rotate(r, rw, rh, back)[0] == m, "rotate %d undone" % k)
    for a in (0, 1):
        f = op_flip(m, w, h, a)
        ok(op_flip(f[0], w, h, a)[0] == m, "flip %d twice" % a)
    ok(op_crop(m, w, h, 0, 0, w - 1, h - 1)[0] == m, "crop to all")
    g, _, _, mode = op_conv(m, w, h, GREY, "blur")
    ok(mode == PM_GREY and len(g) == w * h, "a grey blur is grey")
    flat = bytes([9] * (w * h))
    ok(op_conv(flat, w, h, GREY, "blur")[0] == flat, "a flat blur")
    for num, den in RESIZE_STEPS:
        out, nw, nh, _ = op_resize(m, w, h, GREY, num, den)
        ok((nw, nh) == resize_dims(w, h, num, den) and len(out) == nw * nh,
           "resize %d/%d" % (num, den))
    # writers: read back by the decoders, as the master they were made from
    try:
        from PIL import Image
        import io
    except ImportError:
        Image = None
    for mw, mh in ((13, 7), (64, 33), (1, 1), (300, 2)):
        m = bytes((((x * 7 + y * 31) ^ (x * y)) * 5) & 255
                  for y in range(mh) for x in range(mw))
        noise = bytes(((i * 2654435761) >> 13) & 255 for i in range(mw * mh))
        for mm in (m, noise):
            for fmt in ("BMP", "BMP24", "PCX", "GIF", "PNG", "PIX"):
                for p, mode in ((pal, PM_PAL), (CUBE, PM_CUBE), (GREY, PM_GREY)):
                    if fmt == "BMP24" and mode != PM_CUBE:
                        continue
                    f = write_as(fmt, mm, mw, mh, p, mode)
                    if f is None:
                        continue
                    ext = "BMP" if fmt == "BMP24" else fmt
                    q = decode(f, ext)
                    rm, rw, rh, rmode, rpal = emit(q, 0)
                    if fmt == "PIX":
                        nm = pix_nearest(p)
                        want = bytes(nm[v] for v in mm)
                        ok(rm == want, "PIX %dx%d read back" % (mw, mh))
                        continue
                    if fmt == "BMP24":
                        rgb = [b for v in mm for b in p[v]]
                        rows = [bytes(rgb[3 * mw * y:3 * mw * (y + 1)])
                                for y in range(mh)]
                        want, _ = requant(rows, mw, mh, p)
                        ok(rm == want, "BMP24 %dx%d read back" % (mw, mh))
                        continue
                    ok(rm == mm, "%s %dx%d mode %d: the master read back"
                       % (fmt, mw, mh, mode))
                    rp = pal_full(rpal)
                    ok(all(rp[v] == pal_full(p)[v] for v in set(mm)),
                       "%s %dx%d mode %d: the palette read back"
                       % (fmt, mw, mh, mode))
                    if Image is not None:
                        im = Image.open(io.BytesIO(f)).convert("RGB")
                        px = im.load()
                        good = all(px[x, y] == pal_full(p)[mm[y * mw + x]]
                                   for y in range(mh) for x in range(mw))
                        ok(good, "Pillow reads %s %dx%d mode %d"
                           % (fmt, mw, mh, mode))
    # a table that fills (GIF's Clear at 4,095) and a window that slides
    # (deflate's 4,095 bytes): a noisy 200 x 150 picture with repeats in it
    mw, mh = 200, 150
    big = bytes((((i * 2654435761) >> 13) & 255) if (i // 997) & 1
                else (i % 50) for i in range(mw * mh))
    for fmt in ("GIF", "PNG"):
        f = write_as(fmt, big, mw, mh, pal, PM_PAL)
        rm = emit(decode(f, fmt), 0)[0]
        ok(rm == big, "%s 200x150: a full table / a sliding window" % fmt)
        if Image is not None:
            im = Image.open(io.BytesIO(f))
            ok(im.mode == "P" and bytes(im.getdata()) == big,
               "Pillow reads %s 200x150" % fmt)
    if bad:
        for b in bad:
            print("pixelsim: FAIL " + b)
        return 1
    print("pixelsim: editcheck ok (palette and pixel operations; BMP, BMP24, "
          "PCX, GIF, PNG and PIX read back%s)"
          % ("" if Image is None else ", and by Pillow"))
    return 0


def selfcheck():
    bad = []

    def ok(cond, what):
        if not cond:
            bad.append(what)
    # the cube's arithmetic
    ok(len(set(CUBE[:252])) == 252, "cube entries distinct")
    ok(all(CUBE[NEUTRAL[k - 1]] == (51 * k,) * 3 for k in range(1, 5)),
       "the neutral codes are greys")
    # the ordered quantiser: the matrix is a permutation of the ranks, and a
    # flat field's sixteen classes average back to it within half a level
    ok(sorted(v for r in BLUE16 for v in r) == list(range(256)),
       "BLUE16 is a permutation of 0..255")
    for v in (0, 30, 100, 128, 200, 255):
        tot = sum(R6[(5 * v + T16[k]) // 255] for k in range(16))
        ok(abs(tot / 16.0 - v) <= 26, "ordered mean at %d" % v)
    ok(all(Q6[R6[k]] == k for k in range(6)), "Q6 inverts R6")
    ok(all(Q7[R7[k]] == k for k in range(7)), "Q7 inverts R7")
    ok(all(abs(R7[GNEUT[k]] - 51 * k) <= 26 for k in range(6)), "GNEUT")
    # plans: a desktop colour is its own plan; every t in range
    for c in range(16):
        ok(plan_for(EGA16[c]) == (c, c, 0), "EGA %d is its own plan" % c)
    ok(all(0 <= t <= 64 for _, _, t in CUBE_PLANS + GREY_PLANS), "t range")
    # the guest's pruned search answers plan_for's, over a grid and the two
    # shipped palettes (an 8-level grid with its top edge; 150,000 random
    # colours agreed too when it was written, which is too slow for a row)
    grid = [(r, g, b) for r in range(0, 256, 36) for g in range(0, 256, 36)
            for b in range(0, 256, 36)] + [(255, g, b) for g in range(0, 256, 15) for b in (0, 85, 170, 255)]
    bad_fast = [T for T in grid + CUBE + GREY if plan_fast(T) != plan_for(T)]
    ok(not bad_fast, "plan_fast differs from plan_for at %r" % bad_fast[:3])
    # the shipped plans are this search's
    try:
        ok(open(PLANS_INC).read() == plans_inc(),
           "apps/pixel/pxplans.inc is stale: run tools/pixelsim.py --gen")
        ok(open(QTAB_INC).read() == qtab_inc(),
           "apps/pixel/pxqtab.inc is stale: run tools/pixelsim.py --gen")
        ok(open(GAM_INC).read() == gam_inc(),
           "apps/pixel/pxgam.inc is stale: run tools/pixelsim.py --gen")
    except OSError:
        bad.append("apps/pixel/pxplans.inc missing: run tools/pixelsim.py --gen")
    # a small truecolour BMP round trip through the emitter at both scales
    w, h = 9, 5
    rows = []
    for y in range(h):
        r = bytearray()
        for x in range(w):
            r += bytes((x * 28, y * 60, (x * y * 7) & 255))
        rows.append(r)
    bmp = make_bmp24(w, h, rows)
    p = decode(bmp, "BMP")
    m, mw, mh, mode, pal = emit(p, 0)
    ok((mw, mh, mode) == (9, 5, PM_CUBE), "bmp24 1/1 shape")
    p = decode(bmp, "BMP")
    m2, mw2, mh2, _, _ = emit(p, 1)
    ok((mw2, mh2) == (4, 2), "bmp24 1/2 shape")
    # refusals
    for blob, code in ((bmp[:20], PXD_HEAD), (bmp[:60], PXD_TRUNC)):
        try:
            decode(blob, "BMP")
            bad.append("refusal %d not raised" % code)
        except Refused as e:
            ok(e.code == code, "refusal %d, got %d" % (code, e.code))
    # a view keeps the DDA inside the master
    for asp in ASPECT.values():
        for z in ZSTEPS[:8]:
            v = View(37, 23, 1, z, asp, (32, 40, 431, 339))
            mx, my = v.master_xy(v.ix + v.dw - 1, v.iy + v.dh - 1)
            ok(mx < 37 and my < 23, "DDA inside at z %d" % z)
    # statistics
    c = [0] * 256
    c[0], c[255] = 2, 2
    _, mean, sd, lo, hi = hist_stats(c, GREY, 0)
    ok((mean, sd, lo, hi) == (127, 127, 0, 255), "stats %r" % ((mean, sd, lo, hi),))
    # wave 3 (SPEC.md 106.18): the blend is exact rounding, with the two
    # ends the guest takes without a multiply; this inflate is zlib's on
    # streams of every level; this LZW reads a stream the encoder of the
    # textbook shape writes
    ok(all(blend(255, v) == v and blend(0, v) == BG for v in range(256)),
       "blend's ends")
    ok(all(blend(a, v) == (a * v + (255 - a) * BG + 127) // 255
           for a in range(0, 256, 17) for v in range(0, 256, 5)), "blend rounds")
    src = bytes(((i * 7919) >> 3) & 255 if i % 97 > 40 else i & 3
                for i in range(6000))
    for lvl in (0, 1, 6, 9):
        z = zlib.compress(src, lvl)

        rd = Reader(z)
        br = IdatBits(rd)
        br.left, br.started = len(z), True
        try:
            got = bytes(inflate(br, len(src)))
        except Refused as e:
            got = b"refused %d" % e.code
        ok(got == src, "inflate at zlib level %d" % lvl)
    # full screen (SPEC.md 106.23): Fit keeps the picture inside every
    # mode's screen; a colour of the set is its own plan; the cut of two
    # colours is the two; and a frame's errors stay in a byte's reach
    for mode in range(FSM_N):
        for mw, mh in ((320, 240), (72, 54), (640, 480), (1, 1), (8191, 3)):
            W, H, an, ad = FS_GEOM[mode]
            v = FsView(mw, mh, mode, fs_fit(mw, mh, W, H, an, ad))
            ok(v.dw <= W and v.dh <= H, "fs_fit inside, mode %d %dx%d"
               % (mode, mw, mh))
    ok(all(plan6(CGA6[c], CGA6)[:3] == (c, c, 0) for c in range(16)),
       "a CGA colour is its own plan6")
    two = [(10, 20, 30), (200, 100, 50)] + [(0, 0, 0)] * 254
    ok(fs_mediancut(two, [5, 3] + [0] * 254)[0] == two[:2], "the cut of two")
    m4 = bytes((x * 37 + y * 11) & 255 for y in range(8) for x in range(12))
    fp = FsPic(m4, 12, 8, GREY, FSM_HERC)
    rows, err = fp.frame(FsView(12, 8, FSM_HERC, 65536 * 4))
    ok(max(abs(e) for e in err) < 256, "the diffuser's errors stay bounded")
    # wave 8 (SPEC.md 106.25): one head holds the largest IFD and directory
    # the extras part reads, and the TABLE its strips; TIFF's LZW reads
    # KwKwK; a run-length stream stops at its last byte, a literal's
    # unneeded bytes unread
    ok(2 + 12 * 170 <= HEAD_MAX < 2 + 12 * 171 and 6 + 16 * 127 <= HEAD_MAX,
       "an IFD of 170 and a directory of 127 are one head each")
    ok(EX_STRIPS * 8 == EX_TABLE, "1,024 strips fill the TABLE")
    acc = 0
    for c in (256, 65, 66, 258, 260, 257):
        acc = (acc << 9) | c
    st = (acc << 2).to_bytes(7, "big")
    got = bytearray()
    ok(lzw_tiff(st, lambda s: got.extend(s)) == "EOI" and got == b"ABABABA",
       "TIFF LZW reads ABABABA")
    src = Bytes(b"\x02\x01\x02\x03\xFE\x09\x80")
    ok(unpackbits(src, 6) == b"\1\2\3\x09\x09\x09" and src.p == 6,
       "a run-length stream stops at its last byte")
    src = Bytes(b"\x04\1\2\3")
    ok(unpackbits(src, 3) == b"\1\2\3" and src.p == 4,
       "a literal's unneeded bytes are not read")
    # GIF animation (SPEC.md 106.25): two frames, the first transparent and
    # disposed to the background; a pass that begins again draws frame 0's
    # master exactly
    g = write_gif(bytes((x + y) % 3 for y in range(3) for x in range(4)), 4, 3,
                  EGA16)
    sub = lzw_encode(bytes((5, 2, 7, 7)))
    g = g[:781] + b"\x21\xF9\x04\x09\x0A\x00\x02\x00" + g[781:-1] + \
        b"\x21\xF9\x04\x01\x64\x00\x07\x00\x2C\x01\x00\x01\x00\x02\x00" \
        b"\x02\x00\x00\x08" + bytes((len(sub),)) + sub + b"\x00\x3B"
    loop, fr = gif_anim(g)
    ok(gif_animated(g) and loop is None and len(fr) == 2 and
       fr[1]["rect"] == (0, 0, 3, 2) and fr[1]["delay"] == 18 and
       fr[1]["master"] == bytes((2, 2, 2, 2, 2, 5, 2, 2, 2, 2, 2, 2)) and
       gif_restart(g) == fr[0]["master"], "GIF animation: two frames")
    if bad:
        for b in bad:
            print("pixelsim: FAIL " + b)
        return 1
    print("pixelsim: selfcheck ok (cube, %d+%d plans, emitter, refusals, "
          "views, statistics, blend, inflate, full screen, extras, animation)"
          % (len(CUBE_PLANS), len(GREY_PLANS)))
    return 0


def make_bmp24(w, h, rows):
    """A plain bottom-up 24-bit BMP of `rows` (R,G,B triples), top row first."""
    stride = (w * 3 + 3) & ~3
    data = bytearray()
    for r in reversed(rows):
        line = bytearray()
        for x in range(w):
            line += bytes((r[3 * x + 2], r[3 * x + 1], r[3 * x]))
        data += line + b"\x00" * (stride - len(line))
    hdr = b"BM" + struct.pack("<IHHI", 54 + len(data), 0, 0, 54)
    hdr += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(data),
                       2835, 2835, 0, 0)
    return hdr + bytes(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--gen", action="store_true")
    ap.add_argument("--decode")
    ap.add_argument("--render", nargs=2, metavar=("FILE", "OUT"))
    ap.add_argument("--depth", type=int, default=4)
    ap.add_argument("--aspect", default="vga")
    ap.add_argument("--size", default="408x300")
    ap.add_argument("--scale", type=int, default=0)
    ap.add_argument("--fsrender", nargs=2, metavar=("FILE", "OUT"))
    ap.add_argument("--fsmode", type=int, default=FSM_MODEX)
    ap.add_argument("--ordered", action="store_true")
    ap.add_argument("--editcheck", action="store_true")
    a = ap.parse_args()
    if a.editcheck:
        return editcheck()
    if a.fsrender:
        cmd_fsrender(a.fsrender[0], a.fsrender[1], a.fsmode, a.scale,
                     a.ordered)
        return 0
    if a.gen:
        open(PLANS_INC, "w").write(plans_inc())
        open(QTAB_INC, "w").write(qtab_inc())
        open(GAM_INC, "w").write(gam_inc())
        print("pixelsim: wrote apps/pixel/pxplans.inc")
        return 0
    if a.decode:
        data = open(a.decode, "rb").read()
        try:
            p = decode(data, os.path.splitext(a.decode)[1][1:])
        except Refused as e:
            print("%s: refused %d (%s)" % (a.decode, e.code, PXD_WORDS[e.code]))
            return 0
        m, mw, mh, mode, pal = emit(p, a.scale)
        print("%s: %s %dx%d bits %d rf %d -> %dx%d mode %d" %
              (a.decode, p.fmt, p.w, p.h, p.bits, p.rf, mw, mh, mode))
        return 0
    if a.render:
        cw, ch = (int(v) for v in a.size.split("x"))
        cmd_render(a.render[0], a.render[1], a.depth, a.aspect, cw, ch, a.scale)
        return 0
    return selfcheck()


if __name__ == "__main__":
    sys.exit(main())

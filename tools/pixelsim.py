#!/usr/bin/env python3
"""pixelsim: PiXEL's image core, in Python (SPEC.md 106.8 - 106.13).

    python3 tools/pixelsim.py --selfcheck          # the build's fast row
    python3 tools/pixelsim.py --gen                # rewrite apps/pixel/pxplans.inc
    python3 tools/pixelsim.py --decode FILE        # what PiXEL makes of a file
    python3 tools/pixelsim.py --render FILE OUT.PNG [--depth 4|1] [--aspect vga|ega|herc|cga]

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
PK_NONE, PK_RLE, PK_RLE8, PK_RLE4, PK_BITF = range(5)

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


def cube_palette():
    pal = []
    for r in range(6):
        for g in range(7):
            for b in range(6):
                pal.append((R6[r], R7[g], R6[b]))
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


# =============================================================================
# THE SNIFF (SPEC.md 106.6): the content names the format
# =============================================================================
def sniff(data, ext):
    h = data[:32]
    if len(h) >= 3 and h[0] == 0xFF and h[1] == 0xD8 and h[2] == 0xFF:
        return "JPEG"
    if h[:4] == b"\x89PNG":
        return "PNG"
    if h[:4] == b"GIF8":
        return "GIF"
    if h[:2] == b"BM":
        return "BMP"
    if len(h) >= 3 and h[0] == 0x0A and h[2] == 1 and ext == "PCX":
        return "PCX"
    if h[:4] in (b"II*\x00", b"MM\x00*"):
        return "TIFF"
    if h[:4] == b"O8PI":
        return "PIX"
    if len(h) >= 2 and h[0:1] == b"P" and 0x31 <= h[1] <= 0x36:
        return "PNM"
    if h[:2] == b"CZ":
        return "CZ"
    return {"PCX": "PCX", "TGA": "TGA"}.get(ext, "")


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
# decode: a file's bytes -> a Pic with its rows, or Refused
# =============================================================================
def decode(data, ext=""):
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


class Quant:
    """Floyd-Steinberg into the cube, ONE row of error (sixteenths)."""

    def __init__(self, mw):
        self.err = [[0, 0, 0] for _ in range(mw + 1)]   # +1: x-1 at x = 0

    def row(self, rgb, mw):
        out = bytearray(mw)
        err = self.err
        carry = [0, 0, 0]
        p0 = [0, 0, 0]
        p1 = [0, 0, 0]
        for x in range(mw):
            v = [0, 0, 0]
            for c in range(3):
                t = rgb[3 * x + c] + ((err[x + 1][c] + carry[c]) >> 4)
                v[c] = 0 if t < 0 else 255 if t > 255 else t
            qr, qg, qb = Q6[v[0]], Q7[v[1]], Q6[v[2]]
            if qr == qb and 1 <= qr <= 4 and qg == GNEUT[qr]:
                idx = 251 + qr
                rec = (51 * qr, 51 * qr, 51 * qr)
            else:
                idx = (qr * 7 + qg) * 6 + qb
                rec = (R6[qr], R7[qg], R6[qb])
            out[x] = idx
            for c in range(3):
                e = v[c] - rec[c]
                carry[c] = 7 * e
                err[x][c] = p1[c] + 3 * e       # x-1, final (slot x is x-1)
                p1[c] = p0[c] + 5 * e
                p0[c] = e
        for c in range(3):
            err[mw][c] = p1[c]
        return out


def emit(p, s, pal_out=None):
    """The master (bytes), its size and mode, and its palette."""
    mw, mh = p.w >> s, p.h >> s
    mode = mode_for(p, s)
    master = bytearray(mw * mh)
    pal = p.pal + [(0, 0, 0)] * (256 - len(p.pal))
    q = Quant(mw) if mode == PM_CUBE else None
    n = 1 << s
    acc, cnt = None, 0
    for y, row in p.rows:
        if y >= mh * n:
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
            out = q.row(row, mw) if mode == PM_CUBE else row[:mw]
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
        if cnt == n:
            avg = bytes(a >> (2 * s) for a in acc)
            out = q.row(avg, mw) if mode == PM_CUBE else avg
            r = y >> s
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
    an, ad = aspect
    sw, sh = mw << s, mh << s
    zx = (cw << 16) // sw
    zy = ((ch * ad) << 16) // (sh * an)
    z = min(zx, zy, 65536)
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
           "; A PAL palette's are built on the worker by px_plans, the same search;",
           "; `pixelsim --selfcheck` (a fast row) fails the build if this file is not",
           "; what that search answers. Do not edit it by hand.",
           "; ============================================================================="]
    for name, pl in (("px_plcube", CUBE_PLANS), ("px_plgrey", GREY_PLANS)):
        out.append("%s:" % name)
        for i in range(0, 256, 8):
            out.append("    db " + ", ".join("%d,%d,%d" % pl[j]
                                              for j in range(i, i + 8)))
    return "\n".join(out) + "\n"


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
def selfcheck():
    bad = []

    def ok(cond, what):
        if not cond:
            bad.append(what)
    # the cube's arithmetic
    ok(len(set(CUBE[:252])) == 252, "cube entries distinct")
    ok(all(Q6[R6[k]] == k for k in range(6)), "Q6 inverts R6")
    ok(all(Q7[R7[k]] == k for k in range(7)), "Q7 inverts R7")
    ok(all(abs(R7[GNEUT[k]] - 51 * k) <= 26 for k in range(6)), "GNEUT")
    # plans: a desktop colour is its own plan; every t in range
    for c in range(16):
        ok(plan_for(EGA16[c]) == (c, c, 0), "EGA %d is its own plan" % c)
    ok(all(0 <= t <= 64 for _, _, t in CUBE_PLANS + GREY_PLANS), "t range")
    # the shipped plans are this search's
    try:
        ok(open(PLANS_INC).read() == plans_inc(),
           "apps/pixel/pxplans.inc is stale: run tools/pixelsim.py --gen")
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
    if bad:
        for b in bad:
            print("pixelsim: FAIL " + b)
        return 1
    print("pixelsim: selfcheck ok (cube, %d+%d plans, emitter, refusals, "
          "views, statistics)" % (len(CUBE_PLANS), len(GREY_PLANS)))
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
    a = ap.parse_args()
    if a.gen:
        open(PLANS_INC, "w").write(plans_inc())
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

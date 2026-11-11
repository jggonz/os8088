#!/usr/bin/env python3
"""The Browser's ready copy of a GIF, as SPEC.md 71.26 says it is - built
from tools/pixelsim.py's decode and plans - and the check of the guest's
copies against it (tests/ethernet.py --gif-check).

The copy covers the whole box, COLS x 8 by ROWS x 8: white, the border black,
the picture at (border, border) scaled to the nearest pixel, each pixel c2
when BAYER8[y & 7][x & 7] < t else c1 (lit when < t1 in 1bpp), x and y from
the box's corner. Colour is four planes, row-interleaved; 1bpp one.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import pixelsim  # noqa: E402

TWMAX, THMAX = 96 * 8, 120 * 8
RMAX = 65520
DIMMAX, ROWMAX = 4096, 2048


def muldiv(a, b, c):
    if (a * b) >> 16 >= c:
        return 0xFFFF
    return max(1, a * b // c)


def size(W, H, dw, dh, b):
    """bri_size: the picture's size in its box, and the box."""
    if dw and dh:
        tw, th = dw, dh
    elif dw:
        tw, th = dw, muldiv(dw, H, W)
    elif dh:
        tw, th = muldiv(dh, W, H), dh
    else:
        tw, th = W, H
    maxw = TWMAX - 2 * b
    if tw > maxw:
        th, tw = muldiv(th, maxw, tw), maxw
    maxh = THMAX - 2 * b
    if th > maxh:
        tw, th = muldiv(tw, maxh, th), maxh
    return tw, th, (tw + 2 * b + 7) // 8, (th + 2 * b + 7) // 8


def frame_width(data):
    """The first frame's width (the Browser refuses one past 2048)."""
    i = 13
    if data[10] & 0x80:
        i += 3 * (2 << (data[10] & 7))
    while i < len(data):
        if data[i] == 0x2C:
            return data[i + 5] | data[i + 6] << 8
        i += 2
        while i < len(data) and data[i]:
            i += data[i] + 1
        i += 1
    return 0


def expected(data, dw, dh, b, mono):
    """(cols, rows, bytes) of the ready copy, or None: the image is none."""
    if data is None:
        return None
    old = pixelsim.BG
    pixelsim.BG = 255                   # the page's ground, not PiXEL's grey
    try:
        p = pixelsim.decode(data, "GIF")
    except pixelsim.Refused:
        return None
    finally:
        pixelsim.BG = old
    if p.fmt != "GIF" or not (1 <= p.w <= DIMMAX and 1 <= p.h <= DIMMAX):
        return None
    if frame_width(data) > ROWMAX:
        return None
    W, H = p.w, p.h
    tw, th, cols, rows = size(W, H, dw, dh, b)
    stride = cols if mono else 4 * cols
    if stride * rows * 8 > RMAX:
        return None
    master = dict(p.rows)
    pal = list(p.pal) + [(0, 0, 0)] * (256 - len(p.pal))
    pl = pixelsim.plans(pal)
    t1 = pixelsim.thresh1(pal)
    buf = bytearray([0xFF]) * (stride * rows * 8)

    def put(x, y, c):
        o = y * stride + (x >> 3)
        bit = 0x80 >> (x & 7)
        for p_ in range(1 if mono else 4):
            if c >> p_ & 1:
                buf[o + p_ * cols] |= bit
            else:
                buf[o + p_ * cols] &= ~bit & 0xFF
    for y in range(th + 2 * b):
        for x in range(tw + 2 * b):
            if x < b or y < b or x >= b + tw or y >= b + th:
                put(x, y, 0)
    for ty in range(th):
        src = master[ty * H // th]
        brow = pixelsim.BAYER8[(b + ty) & 7]
        for tx in range(tw):
            i = src[tx * W // tw]
            x = b + tx
            if mono:
                c = 1 if brow[x & 7] < t1[i] else 0
            else:
                c1, c2, t = pl[i]
                c = c2 if brow[x & 7] < t else c1
            put(x, b + ty, c)
    return cols, rows, bytes(buf)


IFMAX = 60000                       # BR_IFMAX: a file staged whole, at most
LV6 = (0, 51, 102, 153, 204, 255)


def px_scale(p, tw, th):
    """SPEC.md 71.30: a PNG's 1/1; a JPEG's the coarsest offered whose master
    is the box's picture each way, else the finest offered."""
    if p.fmt != "JPEG":
        return 0
    offered = list(range(p.smin, 4))
    for s in (3, 2, 1, 0):
        if s in offered and -(-p.w >> s) >= tw and -(-p.h >> s) >= th:
            return s
    return offered[0]


PXFMTS = ("PNG", "JPEG", "BMP", "PCX", "TGA", "PNM", "PIX", "TIFF", "ICO",
          "LBM", "MAC", "XBM")


def expected_px(data, dw, dh, b, mono, ext=""):
    """(cols, rows, bytes) of a decoder part's ready copy (SPEC.md 71.30,
    71.31), or None: the image keeps its alt box. Built from pixelsim's rows -
    the whole picture, upright, at the scale - with the Browser's ground (255)
    for a transparent pixel. `ext` is the URL's extension: a TGA, a PCX and a
    plain MacPaint have no signature."""
    if data is None or len(data) > IFMAX:
        return None
    fmt = pixelsim.sniff(data, ext.upper())
    if fmt not in PXFMTS:
        return None
    old = pixelsim.BG
    pixelsim.BG = 255
    try:
        if fmt == "JPEG":
            p0 = pixelsim.jpeg_header(data, len(data))
            p0.fmt = "JPEG"
            tw, th, cols, rows = size(p0.w, p0.h, dw, dh, b)
            s = px_scale(p0, tw, th)
            p = pixelsim.decode(data, "JPG", s)
        else:
            p = pixelsim.decode(data, ext)
            if fmt == "ICO" and p.fmt == "PNG":
                return None         # a PNG inside an ICO: refused (71.31)
            tw, th, cols, rows = size(p.w, p.h, dw, dh, b)
    except pixelsim.Refused:
        return None
    finally:
        pixelsim.BG = old
    stride = cols if mono else 4 * cols
    if stride * rows * 8 > RMAX:
        return None
    master = dict(p.rows)
    nch = 3 if p.rf == pixelsim.RF_RGB else 1
    MH = len(master)
    MW = len(master[0]) // nch
    if p.rf == pixelsim.RF_IDX:
        n = p.npal or len(p.pal)        # (pixelsim keeps a simple format's count in pal)
        pal = list(p.pal[:n]) + [(0, 0, 0)] * (256 - n)
    elif p.rf == pixelsim.RF_GREY:
        pal = [(v, v, v) for v in range(256)]
    else:
        pal = [(LV6[i // 36], LV6[i // 6 % 6], LV6[i % 6])
               for i in range(216)] + [(0, 0, 0)] * 40
    pl = pixelsim.plans(pal)
    t1 = pixelsim.thresh1(pal)
    buf = bytearray([0xFF]) * (stride * rows * 8)

    def put(x, y, c):
        o = y * stride + (x >> 3)
        bit = 0x80 >> (x & 7)
        for p_ in range(1 if mono else 4):
            if c >> p_ & 1:
                buf[o + p_ * cols] |= bit
            else:
                buf[o + p_ * cols] &= ~bit & 0xFF
    for y in range(th + 2 * b):
        for x in range(tw + 2 * b):
            if x < b or y < b or x >= b + tw or y >= b + th:
                put(x, y, 0)
    for ty in range(th):
        src = master[ty * MH // th]
        y = b + ty
        brow = pixelsim.BAYER8[y & 7]
        for tx in range(tw):
            sx = tx * MW // tw
            x = b + tx
            thr = brow[x & 7]
            if nch == 3:
                i = 0
                for v in src[3 * sx:3 * sx + 3]:
                    q = v * 5 // 255
                    if (v * 5) % 255 * 64 // 255 > thr:
                        q += 1
                    i = 6 * i + q
            else:
                i = src[sx]
            if mono:
                c = 1 if thr < t1[i] else 0
            else:
                c1, c2, t = pl[i]
                c = c2 if thr < t else c1
            put(x, y, c)
    return cols, rows, bytes(buf)


def parse_attrs(attrs):
    out = {}
    for kv in attrs.split():
        k, _, v = kv.partition("=")
        out[k] = int(v)
    return out


def check(m, pseg, offset_of, mono, gifs, say):
    """The guest's table and ready copies against `gifs`: a list of (name,
    data, attrs, shows) in INDEX order. offset_of(name) is a primary bss
    symbol's offset. Answers the list of failures."""
    fails = []
    itab = offset_of("br_itab")
    store = int.from_bytes(m.readseg(pseg, offset_of("br_istseg"), 2),
                           "little")
    for k, (name, data, attrs, shows) in enumerate(gifs):
        e = m.readseg(pseg, itab + 16 * k, 16)
        st, cols, rows = e[0], e[14], e[15]
        para = int.from_bytes(e[12:14], "little")
        a = parse_attrs(attrs)
        want = expected(data, a.get("width", 0), a.get("height", 0),
                        a.get("border", 0), mono)
        if (want is not None) != shows:
            fails.append("%s: the reference %s it but the fixture says %s"
                         % (name, "shows" if want else "refuses",
                            "shows" if shows else "refuses"))
            continue
        if want is None:
            if st != 2:
                fails.append("%s: state %d, the alt box (2) expected"
                             % (name, st))
            else:
                say("%-8s none, as it should be" % name)
            continue
        wc, wr, wb = want
        if st != 1:
            fails.append("%s: state %d, ready (1) expected" % (name, st))
            continue
        if (cols, rows) != (wc, wr):
            fails.append("%s: box %dx%d, %dx%d expected"
                         % (name, cols, rows, wc, wr))
            continue
        got = m.readseg(store + para, 0, len(wb))
        if got != wb:
            diff = next(i for i in range(len(wb)) if got[i] != wb[i])
            fails.append("%s: the ready copy differs at byte %d of %d "
                         "(%02X, %02X expected)"
                         % (name, diff, len(wb), got[diff], wb[diff]))
        else:
            say("%-8s %2dx%-3d %5d bytes, identical" % (name, cols, rows,
                                                         len(wb)))
    return fails

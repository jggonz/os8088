#!/usr/bin/env python3
"""pixcorpus: PiXEL's simple-format fixtures, made from nothing (SPEC.md 106.13).

    python3 tools/pixcorpus.py --out DIR      # write every fixture into DIR
    python3 tools/pixcorpus.py --check        # every fixture's verdict (fast row)

DETERMINISTIC AND NEVER COMMITTED. Every file is built here from a formula,
so a fixture is never a binary in the tree and two runs write the same bytes.
The corpus covers each depth, type, orientation and packing SPEC.md 106.10
reads - and a HOSTILE half: truncated files, lying lengths, 65,535 x 65,535
and zero dimensions, palettes past the head, RLE runs past the row and past
the picture, offsets past the end, a mask with a hole, a version nobody
wrote. Each fixture carries the verdict SPEC.md 106.10 promises for it, and
--check holds tools/pixelsim.py to those verdicts: the reference and the
corpus are two readings of the same section, and this is where they meet.
tests/pxdecode.py then holds the GUEST to pixelsim.

Names are 8.3 and upper case because they go onto a FAT12 floppy as they are.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pixelsim as P                                    # noqa: E402


# --- a picture to encode: deterministic, every channel moving ----------------
def rgb_at(x, y, w, h):
    return ((x * 255) // max(1, w - 1),
            (y * 255) // max(1, h - 1),
            ((x * 37 + y * 91) * 3) & 255)


def rgb_rows(w, h):
    return [[rgb_at(x, y, w, h) for x in range(w)] for y in range(h)]


def idx_rows(w, h, n):
    return [[(x * 3 + y * 5 + (x * y) // 3) % n for x in range(w)]
            for y in range(h)]


def pal_n(n):
    return [((i * 67) & 255, (i * 131 + 40) & 255, (255 - i * 29) & 255)
            for i in range(n)]


# =============================================================================
# BMP
# =============================================================================
def bmp(w, h, bpp, rows=None, pal=None, topdown=False, hsz=40, comp=0,
        masks=None, used=0, raw=None, off=None, planes=1):
    """A BMP. `rows` are indices (bpp <= 8) or (r, g, b) triples."""
    stride = ((w * bpp + 31) // 32) * 4
    if raw is None:
        data = bytearray()
        order = rows if topdown else list(reversed(rows))
        for r in order:
            line = bytearray()
            if bpp <= 8:
                acc, nb = 0, 0
                for v in r:
                    acc = (acc << bpp) | v
                    nb += bpp
                    if nb == 8:
                        line.append(acc)
                        acc, nb = 0, 0
                if nb:
                    line.append(acc << (8 - nb))
            elif bpp == 16:
                for (R, G, B) in r:
                    if masks == "565":
                        v = ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)
                    else:
                        v = ((R >> 3) << 10) | ((G >> 3) << 5) | (B >> 3)
                    line += struct.pack("<H", v)
            elif bpp == 24:
                for (R, G, B) in r:
                    line += bytes((B, G, R))
            else:
                for (R, G, B) in r:
                    line += bytes((B, G, R, 0x5A))
            data += line + b"\x00" * (stride - len(line))
    else:
        data = bytearray(raw)
    pal_b = bytearray()
    for (R, G, B) in (pal or []):
        pal_b += bytes((B, G, R)) if hsz == 12 else bytes((B, G, R, 0))
    extra = b""
    if comp == 3 and hsz == 40:
        m = (0xF800, 0x07E0, 0x001F) if masks == "565" else \
            masks if isinstance(masks, tuple) else (0xFF0000, 0xFF00, 0xFF)
        extra = struct.pack("<III", *m)
    if hsz == 12:
        ih = struct.pack("<IHHHH", 12, w, h, planes, bpp)
    else:
        ih = struct.pack("<IiiHHIIiiII", hsz, w, -h if topdown else h,
                         planes, bpp, comp, len(data), 2835, 2835, used, 0)
        if hsz > 40:
            m = masks if isinstance(masks, tuple) else (0xFF0000, 0xFF00, 0xFF)
            tailb = struct.pack("<IIII", m[0], m[1], m[2], 0)
            ih += (tailb + b"\x00" * hsz)[:hsz - 40]
    hdr_len = 14 + len(ih) + len(extra) + len(pal_b)
    if off is None:
        off = hdr_len
    fh = b"BM" + struct.pack("<IHHI", hdr_len + len(data), 0, 0, off)
    return bytes(fh + ih + extra + pal_b + data)


def rle8(w, h):
    """Bottom-up RLE8: runs, absolute runs (odd and even), a delta, an end
    of line and an early end of bitmap - and one run past the row's end."""
    s = bytearray()
    s += bytes((4, 7, 3, 9))                    # row h-1: 4x7, 3x9
    s += bytes((0, 3, 1, 2, 3, 0))              # absolute 3 (padded)
    s += bytes((w + 5, 11))                     # a run PAST the row's end
    s += bytes((0, 0))                          # end of line
    s += bytes((0, 4, 5, 6, 7, 8))              # absolute 4
    s += bytes((0, 2, 3, 1))                    # delta: right 3, down 1
    s += bytes((2, 200))
    s += bytes((0, 0))
    s += bytes((w, 33, 0, 0))                   # a full row
    s += bytes((0, 1))                          # end of bitmap, rows left
    return bytes(s)


def rle4(w, h):
    s = bytearray()
    s += bytes((5, 0x3A))                       # 3 A 3 A 3
    s += bytes((0, 5, 0x12, 0x34, 0x50, 0))     # absolute 5 nibbles (3 bytes+pad)
    s += bytes((0, 0))
    s += bytes((0, 4, 0xFE, 0xDC))              # absolute 4 nibbles
    s += bytes((w + 3, 0x77))                   # past the row's end
    s += bytes((0, 0))
    for _ in range(h - 2):
        s += bytes((w, 0x9C, 0, 0))
    return bytes(s)


def bmp_rle(w, h, comp, stream, n):
    pal = pal_n(n)
    pal_b = b"".join(bytes((B, G, R, 0)) for (R, G, B) in pal)
    ih = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 8 if comp == 1 else 4, comp,
                     len(stream), 2835, 2835, n, 0)
    off = 14 + 40 + len(pal_b)
    return b"BM" + struct.pack("<IHHI", off + len(stream), 0, 0, off) + ih \
        + pal_b + stream


# =============================================================================
# PCX
# =============================================================================
def pcx_rle(lines, cross=False):
    """ZSoft RLE; `cross` lets a run carry over from one line to the next."""
    out = bytearray()
    data = b"".join(lines) if cross else None
    seqs = [data] if cross else lines
    for seq in seqs:
        i = 0
        while i < len(seq):
            v = seq[i]
            n = 1
            while i + n < len(seq) and seq[i + n] == v and n < 63:
                n += 1
            if n > 1 or (v & 0xC0) == 0xC0:
                out += bytes((0xC0 | n, v))
            else:
                out.append(v)
            i += n
    return bytes(out)


def pcx(w, h, bpp, npl, lines, ver=5, hpal=None, tailpal=None, bpl=None,
        enc=1, cross=False, xmin=0, ymin=0):
    if bpl is None:
        bpl = ((w * bpp + 7) // 8 + 1) & ~1
    hdr = bytearray(128)
    hdr[0], hdr[1], hdr[2], hdr[3] = 0x0A, ver, enc, bpp
    struct.pack_into("<HHHHHH", hdr, 4, xmin, ymin, xmin + w - 1, ymin + h - 1,
                     72, 72)
    for i, c in enumerate(hpal or []):
        hdr[16 + 3 * i:19 + 3 * i] = bytes(c)
    hdr[65] = npl
    struct.pack_into("<HH", hdr, 66, bpl, 1)
    body = pcx_rle(lines, cross)
    tail = b""
    if tailpal is not None:
        tail = b"\x0C" + b"".join(bytes(c) for c in tailpal)
    return bytes(hdr) + body + tail


def pcx_lines_idx(rows, w, bpp, npl, bpl):
    out = []
    for r in rows:
        line = bytearray()
        for p in range(npl):
            pb = bytearray(bpl)
            for x, v in enumerate(r):
                if bpp == 8:
                    pb[x] = v
                else:
                    if (v >> p) & 1:
                        pb[x >> 3] |= 0x80 >> (x & 7)
            line += pb
        out.append(bytes(line))
    return out


def pcx_lines_rgb(rows, w, bpl):
    out = []
    for r in rows:
        line = bytearray()
        for c in range(3):
            pb = bytearray(bpl)
            for x, px in enumerate(r):
                pb[x] = px[c]
            line += pb
        out.append(bytes(line))
    return out


# =============================================================================
# TGA
# =============================================================================
def tga(w, h, itype, pd, pixels, cmap=None, cmbits=24, cmfirst=0, desc=0,
        idtext=b"", rle_cross=True):
    """`pixels`: a flat list of pixel byte strings, file order."""
    cm = b""
    cmt = 0
    cml = 0
    if cmap is not None:
        cmt, cml = 1, len(cmap)
        for (R, G, B) in cmap:
            if cmbits in (15, 16):
                cm += struct.pack("<H", ((R >> 3) << 10) | ((G >> 3) << 5) | (B >> 3))
            elif cmbits == 24:
                cm += bytes((B, G, R))
            else:
                cm += bytes((B, G, R, 255))
    hdr = struct.pack("<BBBHHBHHHHBB", len(idtext), cmt, itype, cmfirst, cml,
                      cmbits if cmap is not None else 0, 0, 0, w, h, pd, desc)
    if itype & 8:
        body = bytearray()
        i = 0
        n = len(pixels)
        while i < n:
            j = i
            while j + 1 < n and pixels[j + 1] == pixels[i] and j - i < 127:
                j += 1
            if j > i:
                body.append(0x80 | (j - i))
                body += pixels[i]
                i = j + 1
            else:
                k = i
                while k + 1 < n and pixels[k + 1] != pixels[k] and k - i < 127:
                    k += 1
                body.append(k - i)
                for q in range(i, k + 1):
                    body += pixels[q]
                i = k + 1
    else:
        body = b"".join(pixels)
    return hdr + idtext + cm + bytes(body)


def tga_px_rgb(rows, pd, bottom=True):
    out = []
    for r in (reversed(rows) if bottom else rows):
        for (R, G, B) in r:
            if pd in (15, 16):
                out.append(struct.pack("<H", ((R >> 3) << 10) | ((G >> 3) << 5) | (B >> 3)))
            elif pd == 24:
                out.append(bytes((B, G, R)))
            else:
                out.append(bytes((B, G, R, 0x80)))
    return out


def tga_px_byte(rows, bottom=True):
    return [bytes((v,)) for r in (reversed(rows) if bottom else rows) for v in r]


# =============================================================================
# PNM, PIX
# =============================================================================
def pnm_ascii(kind, w, h, mx, samples, comment=True):
    hdr = "P%d\n" % kind
    if comment:
        hdr += "# PiXEL corpus\n"
    hdr += "%d %d\n" % (w, h)
    if kind != 1:
        hdr += "%d\n" % mx
    body = []
    per = 1 if kind != 3 else 3
    for y in range(h):
        line = samples[y * w * per:(y + 1) * w * per]
        body.append(("" if kind == 1 else " ").join(str(v) for v in line))
    return (hdr + "\n".join(body) + "\n").encode()


def pnm_raw(kind, w, h, mx, samples):
    hdr = "P%d\n%d %d\n" % (kind, w, h)
    if kind != 4:
        hdr += "%d\n" % mx
    if kind == 4:
        body = bytearray()
        for y in range(h):
            row = samples[y * w:(y + 1) * w]
            for x0 in range(0, w, 8):
                b = 0
                for k in range(8):
                    if x0 + k < w and row[x0 + k]:
                        b |= 0x80 >> k
                body.append(b)
    elif mx < 256:
        body = bytes(samples)
    else:
        body = b"".join(struct.pack(">H", v) for v in samples)
    return hdr.encode() + bytes(body)


def pix(w, h, rows, ver=1):
    stride = (w + 1) // 2
    blk = bytearray()
    for r in rows:
        line = bytearray(stride)
        for x, v in enumerate(r):
            line[x >> 1] |= (v << 4) if not x & 1 else v
        blk += line
    off = 32
    hdr = bytearray(b"O8PIX") + bytes((ver,)) + struct.pack("<HHH", 1, 1, 16)
    hdr += b"\x00" * (16 - len(hdr))
    ent = struct.pack("<HHHHI", 1, w, h, stride, off) + b"\x00" * 4
    return bytes(hdr) + ent + bytes(blk)


# =============================================================================
# THE CORPUS: (name, bytes, verdict) - a verdict is 0 or a PXD_*
# =============================================================================
def corpus():
    out = []

    def add(name, data, verdict=0):
        out.append((name, data, verdict))

    W, H = 13, 7
    rgb = rgb_rows(W, H)
    # --- BMP ------------------------------------------------------------------
    add("B1.BMP", bmp(W, H, 1, idx_rows(W, H, 2), pal=[(10, 20, 30), (250, 240, 230)]))
    add("B1TD.BMP", bmp(W, H, 1, idx_rows(W, H, 2), pal=pal_n(2), topdown=True))
    add("B4.BMP", bmp(W, H, 4, idx_rows(W, H, 5), pal=pal_n(5), used=5))
    add("B8.BMP", bmp(W, H, 8, idx_rows(W, H, 200), pal=pal_n(200), used=200))
    add("B8OS2.BMP", bmp(W, H, 8, idx_rows(W, H, 256), pal=pal_n(256), hsz=12))
    add("B16.BMP", bmp(W, H, 16, rgb))
    add("B16BF.BMP", bmp(W, H, 16, rgb, comp=3, masks="565"))
    add("B24.BMP", bmp(W, H, 24, rgb))
    add("B24TD.BMP", bmp(W, H, 24, rgb, topdown=True))
    add("B32.BMP", bmp(W, H, 32, rgb))
    add("B32V4.BMP", bmp(W, H, 32, rgb, hsz=108, comp=3,
                         masks=(0xFF0000, 0xFF00, 0xFF)))
    add("BR8.BMP", bmp_rle(W, H, 1, rle8(W, H), 64))
    add("BR4.BMP", bmp_rle(W, H, 2, rle4(W, H), 16))
    greys = [[(v, v, v) if (x + y) % 3 else (v, min(255, v + 9), v)
              for x in range(40) for v in [(x * 6 + y * 11) & 255]]
             for y in range(6)]
    add("BGREY.BMP", bmp(40, 6, 24, greys))     # neutrals: SPEC.md 106.8
    add("BIG24.BMP", bmp(64, 48, 24, rgb_rows(64, 48)))
    add("BIG8.BMP", bmp(64, 48, 8, idx_rows(64, 48, 256), pal=pal_n(256)))
    # --- PCX ------------------------------------------------------------------
    i2 = idx_rows(W, H, 2)
    add("C1.PCX", pcx(W, H, 1, 1, pcx_lines_idx(i2, W, 1, 1, 2)))
    i16 = idx_rows(W, H, 16)
    add("C4.PCX", pcx(W, H, 1, 4, pcx_lines_idx(i16, W, 1, 4, 2),
                      hpal=pal_n(16)))
    add("C4V3.PCX", pcx(W, H, 1, 4, pcx_lines_idx(i16, W, 1, 4, 2), ver=3))
    i256 = idx_rows(W, H, 256)
    add("C8.PCX", pcx(W, H, 8, 1, pcx_lines_idx(i256, W, 8, 1, 14),
                      tailpal=pal_n(256), cross=True))
    add("C8G.PCX", pcx(W, H, 8, 1, pcx_lines_idx(i256, W, 8, 1, 14)))
    add("C24.PCX", pcx(W, H, 8, 3, pcx_lines_rgb(rgb, W, 14), cross=True))
    # --- TGA ------------------------------------------------------------------
    pal20 = pal_n(20)
    i20 = idx_rows(W, H, 20)
    add("T1.TGA", tga(W, H, 1, 8, tga_px_byte(i20), cmap=pal20))
    add("T9.TGA", tga(W, H, 9, 8, tga_px_byte([[v % 12 for v in r] for r in i20],
                                             bottom=False),
                      cmap=pal_n(12), cmbits=16, cmfirst=0, desc=0x20,
                      idtext=b"PiXEL"))
    add("T2_16.TGA", tga(W, H, 2, 16, tga_px_rgb(rgb, 16)))
    add("T2_24.TGA", tga(W, H, 2, 24, tga_px_rgb(rgb, 24, bottom=False), desc=0x20))
    add("T2_32.TGA", tga(W, H, 2, 32, tga_px_rgb(rgb, 32)))
    flat = [[(200, 100, 50) if x < 6 else rgb[y][x] for x in range(W)]
            for y in range(H)]
    add("T10.TGA", tga(W, H, 10, 24, tga_px_rgb(flat, 24)))
    grey = [[(x * 19 + y * 7) & 255 for x in range(W)] for y in range(H)]
    add("T3.TGA", tga(W, H, 3, 8, tga_px_byte(grey)))
    add("T11.TGA", tga(W, H, 11, 8, tga_px_byte([[v & 0xF0 for v in r]
                                                 for r in grey])))
    # --- PNM ------------------------------------------------------------------
    bits = [v for r in i2 for v in r]
    g15 = [(v >> 4) for r in grey for v in r]
    g255 = [v for r in grey for v in r]
    c255 = [c for r in rgb for px in r for c in px]
    add("N1.PBM", pnm_ascii(1, W, H, 1, bits))
    add("N2.PGM", pnm_ascii(2, W, H, 15, g15))
    add("N3.PPM", pnm_ascii(3, W, H, 255, c255))
    add("N4.PBM", pnm_raw(4, W, H, 1, bits))
    add("N5.PGM", pnm_raw(5, W, H, 255, g255))
    add("N5W.PGM", pnm_raw(5, W, H, 1000, [v * 3 for v in g255]))
    add("N6.PPM", pnm_raw(6, W, H, 255, c255))
    add("N6W.PPM", pnm_raw(6, W, H, 65535, [v * 257 for v in c255]))
    # --- PIX ------------------------------------------------------------------
    add("X.PIX", pix(W, H, i16))

    # --- the HOSTILE half ---------------------------------------------------
    good = bmp(W, H, 24, rgb)
    add("H01.BMP", good[:20], P.PXD_HEAD)                   # header cut short
    add("H02.BMP", good[:-30], P.PXD_TRUNC)                 # pixels cut short
    add("H03.BMP", bmp(70000, 2, 24, raw=b"\x00" * 64), P.PXD_DIMS)
    add("H04.BMP", bmp(0, 5, 24, raw=b""), P.PXD_DIMS)
    add("H05.BMP", bmp(W, H, 24, raw=b"\x00" * 300, off=0x7FFFFFF0),
        P.PXD_TRUNC)                                       # offset past the end
    add("H06.BMP", bmp(W, H, 2, raw=b"\x00" * 64), P.PXD_DEPTH)
    add("H07.BMP", bmp(W, H, 24, raw=b"\x00" * 64, comp=4), P.PXD_PACK)
    add("H08.BMP", bmp(W, H, 16, raw=b"\x00" * 300, comp=3,
                       masks=(0xF00F, 0x0F0, 0x00F)), P.PXD_DEPTH)  # a hole
    lie = bytearray(bmp(W, H, 8, idx_rows(W, H, 4), pal=pal_n(4), used=4))
    struct.pack_into("<I", lie, 46, 250)                    # palette PAST the head
    add("H09.BMP", bytes(lie[:60]), P.PXD_HEAD)
    rle_cut = bmp_rle(W, H, 1, bytes((3, 1, 0, 0, 3, 1)), 4)
    add("H10.BMP", rle_cut, P.PXD_TRUNC)                    # no end, no rows
    add("H11.BMP", bmp(65535, 65535, 1, raw=b"\x00" * 64), P.PXD_DIMS)
    add("H12.BMP", bmp(W, H, 8, raw=b"\x00" * 64, comp=1, topdown=True),
        P.PXD_PACK)                                         # top-down RLE
    hd = bytearray(pcx(W, H, 1, 1, pcx_lines_idx(i2, W, 1, 1, 2)))
    struct.pack_into("<H", hd, 8, 0)                        # xmax < xmin
    struct.pack_into("<H", hd, 4, 9)
    add("H13.PCX", bytes(hd), P.PXD_DIMS)
    hb = bytearray(pcx(W, H, 8, 1, pcx_lines_idx(i256, W, 8, 1, 14)))
    struct.pack_into("<H", hb, 66, 4)                       # bpl too small
    add("H14.PCX", bytes(hb), P.PXD_HEAD)
    add("H15.PCX", pcx(W, H, 8, 3, pcx_lines_rgb(rgb, W, 14))[:200],
        P.PXD_TRUNC)
    add("H16.PCX", pcx(W, H, 2, 1, []), P.PXD_DEPTH)
    add("H17.TGA", tga(W, H, 1, 8, tga_px_byte(i20), cmap=pal20, cmfirst=250),
        P.PXD_HEAD)                                         # map past 256
    add("H18.TGA", tga(W, H, 2, 24, tga_px_rgb(rgb, 24), desc=0x10),
        P.PXD_ORIENT)
    add("H19.TGA", tga(W, H, 10, 24, tga_px_rgb(rgb, 24))[:60], P.PXD_TRUNC)
    add("H20.TGA", tga(W, H, 1, 16, [], cmap=pal20), P.PXD_DEPTH)
    add("H21.PGM", b"P5\n99999 3\n255\n" + b"\x00" * 50, P.PXD_DIMS)
    add("H22.PGM", b"P2\n3 2\n255\n1 2 x 4 5 6\n", P.PXD_DATA)
    add("H23.PPM", pnm_raw(6, W, H, 255, c255)[:100], P.PXD_TRUNC)
    add("H24.PGM", b"P5\n3 2\n0\n" + b"\x00" * 6, P.PXD_HEAD)
    add("H25.PIX", pix(W, H, i16, ver=2), P.PXD_HEAD)
    add("H26.JPG", b"\xFF\xD8\xFF\xE0" + b"\x00" * 60, P.PXD_NOTYET)
    add("H27.PGM", b"P5\n" + b"#" * 3000 + b"\n3 2\n255\n" + b"\x00" * 6,
        P.PXD_HEAD)                                         # header past the head
    add("H28.BMP", bmp(W, H, 8, idx_rows(W, H, 4), pal=pal_n(4), used=4,
                       planes=3), P.PXD_HEAD)
    # a JPEG whose APP1 length walks the marker scan to the edge of 64K: the
    # sniff must stop, not wrap (wave-1 review MAJ-1). Named, not decoded
    add("H29.JPG", b"\xFF\xD8\xFF\xE1\xFF\xF2" + b"\xFF" * 2042,
        P.PXD_NOTYET)
    add("H30.DAT", b"Not a picture at all, just words." * 9, P.PXD_NOTPIC)
    # ...and a picture under a name PiXEL does not list: its bytes win
    add("B24X.DAT", bmp(W, H, 24, rgb))
    return [c for c in out if c is not None]


# fixtures of one content. The cube's ordered dither (SPEC.md 106.8) is a
# function of a pixel and its MASTER row, not of the order rows arrive in, so
# a bottom-up file and a top-down one agree too - eleven readers of one
# truecolour picture, one master. (B16BF.BMP is 5:6:5, a different picture
# at eight bits a channel: Floyd-Steinberg used to round it onto 5:5:5's
# master by luck, and an ordered dither is not obliged to.)
AGREE = [
    ("B24.BMP", "B32.BMP", "B32V4.BMP", "T2_32.TGA", "B24TD.BMP", "C24.PCX",
     "T2_24.TGA", "N3.PPM", "N6.PPM", "N6W.PPM"),
    ("B16.BMP", "T2_16.TGA"),
    ("N5.PGM", "T3.TGA"),
    ("N1.PBM", "N4.PBM"),
    ("C4.PCX", "X.PIX"),
]


def verdict(name, data):
    try:
        p = P.decode(data, name.rsplit(".", 1)[1])
    except P.Refused as e:
        return e.code, None
    return 0, p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    cs = corpus()
    names = [c[0] for c in cs]
    if len(set(names)) != len(names):
        sys.exit("pixcorpus: two fixtures share a name")
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        for name, data, _ in cs:
            open(os.path.join(a.out, name), "wb").write(data)
        print("pixcorpus: %d fixtures in %s" % (len(cs), a.out))
    if a.check or not a.out:
        bad = 0
        for name, data, want in cs:
            got, p = verdict(name, data)
            if got != want:
                print("pixcorpus: FAIL %s: pixelsim says %d (%s), the corpus %d (%s)"
                      % (name, got, P.PXD_WORDS[got], want, P.PXD_WORDS[want]))
                bad += 1
            elif p is not None:
                P.emit(p, 0)
        # two readings of one picture are one master: every truecolour
        # fixture of the same content, through five different decoders and
        # both emission orders, must quantise to the same bytes
        got = {}
        for name, data, want in cs:
            if not want:
                p = P.decode(data, name.rsplit(".", 1)[1])
                got[name] = bytes(P.emit(p, 0)[0])
        for grp in AGREE:
            if len(set(got[n] for n in grp)) != 1:
                print("pixcorpus: FAIL these should be one master: %s"
                      % " ".join(grp))
                bad += 1
        if bad:
            return 1
        print("pixcorpus: %d fixtures, %d good and %d hostile, every verdict "
              "pixelsim's" % (len(cs), sum(1 for c in cs if not c[2]),
                              sum(1 for c in cs if c[2])))
    return 0


if __name__ == "__main__":
    sys.exit(main())

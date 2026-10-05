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
import itertools
import os
import struct
import sys
import zlib

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
# GIF (SPEC.md 106.18): an LZW encoder of the standard shape - a code size
# that grows after the code that fills a power of two, and a Clear when the
# table is full or (deferred=True) no Clear at all
# =============================================================================
def lzw_encode(pixels, mincode, deferred=False, clear_every=0):
    clr = 1 << mincode
    eoi = clr + 1
    out = []                        # (code, size)
    cs = mincode + 1
    table = {}
    nxt = clr + 2
    out.append((clr, cs))
    w = None
    since = 0
    for k in pixels:
        if w is None:
            w = (k,)
            continue
        wk = w + (k,)
        if wk in table or (len(wk) == 1):
            w = wk
            continue
        code = table[w] if len(w) > 1 else w[0]
        out.append((code, cs))
        since += 1
        if nxt >= (1 << cs) and cs < 12:
            cs += 1
        if nxt < 4096:
            table[wk] = nxt
            nxt += 1
        elif not deferred:
            out.append((clr, cs))
            table, nxt, cs = {}, clr + 2, mincode + 1
        if clear_every and since >= clear_every and nxt < 4096:
            out.append((clr, cs))
            table, nxt, cs, since = {}, clr + 2, mincode + 1, 0
        w = (k,)
    if w is not None:
        out.append((table[w] if len(w) > 1 else w[0], cs))
        if nxt >= (1 << cs) and cs < 12:
            cs += 1
    out.append((eoi, cs))
    return pack_lsb(out)


def pack_lsb(codes):
    acc = nb = 0
    b = bytearray()
    for c, n in codes:
        acc |= c << nb
        nb += n
        while nb >= 8:
            b.append(acc & 255)
            acc >>= 8
            nb -= 8
    if nb:
        b.append(acc & 255)
    return bytes(b)


def subblocks(data, size=255):
    b = bytearray()
    for i in range(0, len(data), size):
        part = data[i:i + size]
        b.append(len(part))
        b += part
    b.append(0)
    return bytes(b)


def gif(sw, sh, pixels, gpal=None, lpal=None, fw=None, fh=None, left=0,
        top=0, ilace=False, trans=None, bgi=0, ver=b"89a", exts=(),
        mincode=None, deferred=False, clear_every=0, codes=None,
        more_frames=False):
    """A GIF: the screen sw x sh, ONE frame of `pixels` (rows, in picture
    order) fw x fh at (left, top). `codes` replaces the LZW stream."""
    fw = sw if fw is None else fw
    fh = sh if fh is None else fh
    b = bytearray(b"GIF" + ver + struct.pack("<HH", sw, sh))

    def tbits(p):
        n = 1
        while (2 << (n - 1)) < len(p):
            n += 1
        return n
    if gpal:
        gb = tbits(gpal)
        b += bytes((0x80 | ((gb - 1) << 4) | (gb - 1), bgi, 0))
        b += b"".join(bytes(c) for c in gpal)
        b += b"\0\0\0" * ((2 << (gb - 1)) - len(gpal))
    else:
        b += bytes((0, bgi, 0))
    for e in exts:
        b += e
    if trans is not None:
        b += b"\x21\xF9\x04" + bytes((1, 0, 0, trans)) + b"\0"
    pk = 0x40 if ilace else 0
    lb = 0
    if lpal:
        lb = tbits(lpal)
        pk |= 0x80 | (lb - 1)
    b += b"\x2C" + struct.pack("<HHHH", left, top, fw, fh) + bytes((pk,))
    if lpal:
        b += b"".join(bytes(c) for c in lpal)
        b += b"\0\0\0" * ((2 << (lb - 1)) - len(lpal))
    if mincode is None:
        mincode = max(2, lb or (gb if gpal else 2))
    order = []
    if ilace:
        for k in range(4):
            order += list(range((0, 4, 2, 1)[k], fh, (8, 8, 4, 2)[k]))
    else:
        order = list(range(fh))
    if codes is not None:
        data = codes
    else:
        flat = [v for y in order for v in pixels[y]]
        data = lzw_encode(flat, mincode, deferred, clear_every)
    b += bytes((mincode,)) + subblocks(data)
    if more_frames:
        b += b"\x21\xF9\x04\x00\x05\x00\x00\x00"
        b += b"\x2C" + struct.pack("<HHHH", 0, 0, 2, 2) + b"\0\x02"
        b += subblocks(lzw_encode([1, 1, 1, 1], 2))
    b += b"\x3B"
    return bytes(b)


# =============================================================================
# PNG (SPEC.md 106.18): chunks, the five forward filters, and a deflate
# writer of our own - stored, fixed and dynamic blocks, with code lengths
# chosen or computed - because the trees a hostile stream needs are not ones
# zlib will write
# =============================================================================
def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + \
        struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)


def png_raw(ct, depth, rows):
    """`rows` of samples (ints) -> packed scanline bytes, no filter byte."""
    out = []
    for r in rows:
        if depth < 8:
            acc = nb = 0
            line = bytearray()
            for v in r:
                acc = (acc << depth) | v
                nb += depth
                if nb == 8:
                    line.append(acc)
                    acc = nb = 0
            if nb:
                line.append(acc << (8 - nb))
            out.append(bytes(line))
        elif depth == 8:
            out.append(bytes(r))
        else:
            out.append(b"".join(struct.pack(">H", v) for v in r))
    return out


def filt(ft, cur, prev, bpp):
    out = bytearray(len(cur))
    for i in range(len(cur)):
        a = cur[i - bpp] if i >= bpp else 0
        b = prev[i]
        c = prev[i - bpp] if i >= bpp else 0
        if ft == 0:
            p = 0
        elif ft == 1:
            p = a
        elif ft == 2:
            p = b
        elif ft == 3:
            p = (a + b) >> 1
        else:
            p = P.paeth(a, b, c)
        out[i] = (cur[i] - p) & 255
    return bytes(out)


def png_stream(w, h, ct, depth, samples, ilace=False, fseq=(0, 1, 2, 3, 4)):
    """The filtered byte stream: `samples` is h rows of w pixels, each pixel
    a tuple of channel ints (or an int for one channel)."""
    ch = P.PNG_CHAN[ct]
    bpp = max(1, depth * ch // 8)
    passes = P.ADAM7 if ilace else ((0, 0, 1, 1),)
    out = bytearray()
    k = 0
    for x0, y0, dx, dy in passes:
        if w <= x0 or h <= y0:
            continue
        rows = []
        for y in range(y0, h, dy):
            r = []
            for x in range(x0, w, dx):
                v = samples[y][x]
                r += list(v) if isinstance(v, tuple) else [v]
            rows.append(r)
        raw = png_raw(ct, depth, rows)
        prev = bytes(len(raw[0]))
        for line in raw:
            ft = fseq[k % len(fseq)]
            k += 1
            out.append(ft)
            out += filt(ft, line, prev, bpp)
            prev = line
    return bytes(out)


def png(w, h, ct, depth, samples=None, ilace=False, plte=None, trns=None,
        z=None, stream=None, idat_split=None, extra=(), ihdr=None, iend=True,
        fseq=(0, 1, 2, 3, 4), crc_bad=False, zero_idat=False):
    b = bytearray(b"\x89PNG\r\n\x1a\n")
    b += chunk(b"IHDR", ihdr if ihdr is not None else
               struct.pack(">IIBBBBB", w, h, depth, ct, 0, 0, 1 if ilace else 0))
    for e in extra:
        b += e
    if plte is not None:
        b += chunk(b"PLTE", b"".join(bytes(c) for c in plte))
    if trns is not None:
        b += chunk(b"tRNS", trns)
    if z is None:
        if stream is None:
            stream = png_stream(w, h, ct, depth, samples, ilace, fseq)
        z = zlib.compress(stream, 9)
    parts = [z]
    if idat_split:
        parts = [z[i:i + idat_split] for i in range(0, len(z), idat_split)]
    if zero_idat:
        parts = [b""] + parts[:1] + [b""] + parts[1:]
    for k, part in enumerate(parts):
        c = chunk(b"IDAT", part)
        if crc_bad and k == 0:
            c = c[:-1] + bytes(((c[-1] ^ 0x55),))
        b += c
    if iend:
        b += chunk(b"IEND", b"")
    return bytes(b)


class BitW:
    def __init__(self):
        self.b = bytearray()
        self.acc = self.n = 0

    def put(self, v, n):
        self.acc |= v << self.n
        self.n += n
        while self.n >= 8:
            self.b.append(self.acc & 255)
            self.acc >>= 8
            self.n -= 8

    def code(self, c, n):           # a Huffman code goes MSB first
        r = 0
        for i in range(n):
            r = (r << 1) | ((c >> i) & 1)
        self.put(r, n)

    def align(self):
        if self.n:
            self.put(0, 8 - self.n)

    def bytes(self):
        out = bytearray(self.b)
        if self.n:
            out.append(self.acc & 255)
        return bytes(out)


def canon(lens):
    cnt = [0] * 16
    for v in lens:
        if v:
            cnt[v] += 1
    nxt = [0] * 16
    c = 0
    for n in range(1, 16):
        c = (c + cnt[n - 1]) << 1 if n > 1 else 0
        nxt[n] = c
    codes = [0] * len(lens)
    for s, v in enumerate(lens):
        if v:
            codes[s] = nxt[v]
            nxt[v] += 1
    return codes


def limited(freqs, limit):
    """Code lengths of at most `limit` bits (package-merge)."""
    items = sorted((f, s) for s, f in enumerate(freqs) if f)
    lens = [0] * len(freqs)
    if not items:
        return lens
    if len(items) == 1:
        lens[items[0][1]] = 1
        return lens
    leaves = [(f, [s]) for f, s in items]
    cur = list(leaves)
    for _ in range(limit - 1):
        pk = [(cur[i][0] + cur[i + 1][0], cur[i][1] + cur[i + 1][1])
              for i in range(0, len(cur) - 1, 2)]
        cur = sorted(leaves + pk, key=lambda t: t[0])
    for f, ss in cur[:2 * len(items) - 2]:
        for s in ss:
            lens[s] += 1
    return lens


def lz77(data, maxdist=32768):
    """Greedy matches of 3..258, for streams with both trees in use."""
    out, i, last = [], 0, {}
    while i < len(data):
        best = (0, 0)
        key = bytes(data[i:i + 3])
        j = last.get(key)
        if j is not None and i - j <= maxdist and len(key) == 3:
            n = 0
            while n < 258 and i + n < len(data) and data[j + n] == data[i + n]:
                n += 1
            if n >= 3:
                best = (n, i - j)
        if len(key) == 3:
            last[key] = i
        if best[0]:
            for k in range(1, best[0]):
                if i + k + 3 <= len(data):
                    last[bytes(data[i + k:i + k + 3])] = i + k
            out.append(best)
            i += best[0]
        else:
            out.append(data[i])
            i += 1
    return out


def lsym(n):
    for s in range(29):
        if s == 28 or P.LBASE[s + 1] > n:
            return s
    return 28


def dsym(d):
    for s in range(30):
        if s == 29 or P.DBASE[s + 1] > d:
            return s
    return 29


def fixed_lens():
    return [8] * 144 + [9] * 112 + [7] * 24 + [8] * 8, [5] * 32


def put_syms(bw, syms, ll, dl, eob=True):
    lc, dc = canon(ll), canon(dl)
    for t in syms:
        if isinstance(t, int):
            bw.code(lc[t], ll[t])
            continue
        n, d = t
        s = lsym(n)
        bw.code(lc[257 + s], ll[257 + s])
        bw.put(n - P.LBASE[s], P.LEXT[s])
        e = dsym(d)
        bw.code(dc[e], dl[e])
        bw.put(d - P.DBASE[e], P.DEXT[e])
    if eob:
        bw.code(lc[256], ll[256])


def put_dyn_header(bw, ll, dl, cl_lens=None, rle=True, hlit=None, raw=None):
    """HLIT, HDIST, HCLEN, the code-length code, the lengths."""
    nl = hlit if hlit is not None else max(257, max(i for i, v in enumerate(ll) if v) + 1)
    nd = max(1, max([i for i, v in enumerate(dl) if v] + [0]) + 1)
    allv = list(ll[:nl]) + list(dl[:nd])
    if raw is None:
        raw = []
        i = 0
        while i < len(allv):
            v = allv[i]
            r = 1
            while i + r < len(allv) and allv[i + r] == v:
                r += 1
            if rle and v == 0 and r >= 11:
                k = min(r, 138)
                raw.append((18, k - 11))
                i += k
            elif rle and v == 0 and r >= 3:
                k = min(r, 10)
                raw.append((17, k - 3))
                i += k
            elif rle and r >= 4:
                raw.append((v, None))
                k = min(r - 1, 6)
                raw.append((16, k - 3))
                i += 1 + k
            else:
                raw.append((v, None))
                i += 1
    if cl_lens is None:
        f = [0] * 19
        for s, _ in raw:
            f[s] += 1
        cl_lens = limited(f, 7)
    order = P.CLORD
    nc = 19
    while nc > 4 and cl_lens[order[nc - 1]] == 0:
        nc -= 1
    bw.put(nl - 257, 5)
    bw.put(nd - 1, 5)
    bw.put(nc - 4, 4)
    for i in range(nc):
        bw.put(cl_lens[order[i]], 3)
    cc = canon(cl_lens)
    for s, ex in raw:
        bw.code(cc[s], cl_lens[s])
        if s == 16:
            bw.put(ex, 2)
        elif s == 17:
            bw.put(ex, 3)
        elif s == 18:
            bw.put(ex, 7)


def subtab_png(nprefix, w, h):
    """A grey PNG whose lit/len code has `nprefix` nine-bit prefixes each
    holding two ten-bit codes, short codes completing the tree, and every
    symbol of the data a LONG one (the wave-3 review's subtab.py)."""
    ll = [0] * 286
    left = 512 - nprefix
    shorts = []
    for L in range(1, 10):
        if left >= (512 >> L):
            shorts.append(L)
            left -= 512 >> L
    longs = list(range(2 * nprefix))
    for c in longs:
        ll[c] = 10
    rest = [c for c in range(256) if c not in longs][:len(shorts) - 1] + [256]
    for c, L in zip(rest, shorts):
        ll[c] = L
    raw = bytearray()
    for y in range(h):
        raw.append(y % 5)                       # the filter bytes are long too
        raw += bytes(longs[5 + (x * 7 + y * 3) % (len(longs) - 5)]
                     for x in range(w))
    bw = BitW()
    bw.put(1, 1)
    bw.put(2, 2)
    put_dyn_header(bw, ll, [0] * 30)
    put_syms(bw, list(raw), ll, [0] * 30)
    z = b"\x78\x01" + bw.bytes() + struct.pack(">I", zlib.adler32(bytes(raw)))
    return png(w, h, 0, 8, z=z)


def runs1(data):
    """Literals and distance-1 matches only: a stream with ONE distance code."""
    out, i = [], 0
    while i < len(data):
        n = 1
        while i + n < len(data) and data[i + n] == data[i] and n < 259:
            n += 1
        out.append(data[i])
        if n - 1 >= 3:
            out.append((n - 1, 1))
        else:
            out += [data[i]] * (n - 1)
        i += n
    return out


def deflate(data, kind="dyn", ll=None, dl=None, matches=True, stored_max=65535,
            blocks=1):
    """A whole zlib stream of `data`: kind stored / fixed / dyn, the last
    with lengths computed (or given) - and `blocks` blocks of it."""
    bw = BitW()
    parts = [data[i * len(data) // blocks:(i + 1) * len(data) // blocks]
             for i in range(blocks)]
    for k, part in enumerate(parts):
        final = 1 if k == blocks - 1 else 0
        if kind == "stored":
            for j in range(0, max(1, len(part)), stored_max):
                piece = part[j:j + stored_max]
                last = final and j + stored_max >= len(part)
                bw.put(1 if last else 0, 1)
                bw.put(0, 2)
                bw.align()
                bw.put(len(piece), 16)
                bw.put(len(piece) ^ 0xFFFF, 16)
                for c in piece:
                    bw.put(c, 8)
            continue
        syms = (runs1(part) if matches == "runs" else
                lz77(part) if matches else list(part))
        if kind == "fixed":
            fl, fd = fixed_lens()
            bw.put(final, 1)
            bw.put(1, 2)
            put_syms(bw, syms, fl, fd)
            continue
        fl = [0] * 286
        fd = [0] * 30
        fl[256] = 1
        for t in syms:
            if isinstance(t, int):
                fl[t] += 1
            else:
                fl[257 + lsym(t[0])] += 1
                fd[dsym(t[1])] += 1
        L = ll if ll is not None else limited(fl, 15)
        D = dl if dl is not None else limited(fd, 15)
        bw.put(final, 1)
        bw.put(2, 2)
        put_dyn_header(bw, L, D)
        put_syms(bw, syms, L, D)
    adler = zlib.adler32(data) & 0xFFFFFFFF
    return b"\x78\xDA" + bw.bytes() + struct.pack(">I", adler)


def unary_lens(syms_by_freq, n=286):
    """A complete code where the k-th symbol gets k + 1 bits and the last two
    share the deepest length: long codes on purpose (codes past 9 bits are
    the guest's slow path, SPEC.md 106.18)."""
    L = [0] * n
    k = len(syms_by_freq)
    for i, s in enumerate(syms_by_freq):
        L[s] = min(i + 1, k - 1)
    return L


# =============================================================================
# JPEG (SPEC.md 106.19): the committed good fixtures (tools/pixjpeg.py, made
# once by Pillow and cjpeg and pinned), and a HOSTILE half derived from them
# here in pure Python - so no hostile file is committed, and every mutation
# says what it breaks
# =============================================================================
def jsegs(d):
    """[(offset, marker, length)] of a JPEG's segments up to its first SOS."""
    out, p = [], 2
    while p + 4 <= len(d):
        m = d[p + 1]
        ln = (d[p + 2] << 8) | d[p + 3]
        out.append((p, m, ln))
        if m == 0xDA:
            break
        p += 2 + ln
    return out


def jfind(d, m, n=0):
    return [s for s in jsegs(d) if s[1] == m][n]


def jpatch(d, off, *vals):
    d = bytearray(d)
    for i, v in enumerate(vals):
        d[off + i] = v
    return bytes(d)


def jsos_all(d):
    """Every SOS's offset (progressive files have many)."""
    out, p = [], 0
    while True:
        p = d.find(b"\xFF\xDA", p + 1)
        if p < 0:
            return out
        out.append(p)


def jcorpus(add):
    import pixjpeg as J
    for name, data, verdict in J.fixtures():
        add(name, data, verdict)
    g = J.fixtures()
    base = [d for n, d, v in g if n == "J420.JPG"][0]
    prog = [d for n, d, v in g if n == "JPROG.JPG"][0]
    rst = [d for n, d, v in g if n == "JRST.JPG"][0]
    o1 = [d for n, d, v in g if n == "JO1.JPG"][0]
    sof = jfind(base, 0xC0)[0]
    dht = jfind(base, 0xC4)[0]
    dqt = jfind(base, 0xDB)[0]
    sos = jfind(base, 0xDA)[0]
    app0 = jfind(base, 0xE0)[0]
    T, D, P_, Hd, Dm, Dp = (P.PXD_TRUNC, P.PXD_DATA, P.PXD_PACK, P.PXD_HEAD,
                            P.PXD_DIMS, P.PXD_DEPTH)
    # --- cut short ------------------------------------------------------------
    add("HJ01.JPG", base[:sof + 19], T)              # ends after the frame
    add("HJ02.JPG", base[:sos + 14 + 200], T)        # ends inside the data
    add("HJ03.JPG", base[:4], T)                     # inside the first marker
    add("HJ04.JPG", base[:sos + 14 + 200] + b"\xFF\xD9", T)  # EOI inside it
    # --- Huffman tables ------------------------------------------------------
    add("HJ05.JPG", jpatch(base, dht + 5, 3), D)     # three codes of length 1
    add("HJ06.JPG", jpatch(base, dht + 5 + 15, 255), D)  # 256+ symbols
    seg = base[dht:dht + 2 + ((base[dht + 2] << 8) | base[dht + 3])]
    short = seg[:2] + bytes(((len(seg) - 7) >> 8, (len(seg) - 7) & 255)) + \
        seg[4:-5]                                    # its symbols cut short
    add("HJ07.JPG", base[:dht] + short + base[dht + len(seg):], D)
    add("HJ08.JPG", jpatch(base, dht + 21, 16), D)   # a DC symbol of 16
    add("HJ09.JPG", jpatch(base, dht + 4, 0x20), D)  # table class 2
    add("HJ10.JPG", base[:dht] + base[dht + 2 + ((base[dht + 2] << 8) |
                                                 base[dht + 3]):], D)  # no DHT 0
    # --- quantisation --------------------------------------------------------
    add("HJ11.JPG", jpatch(base, dqt + 5 + 10, 0), D)  # a quantiser of 0
    add("HJ12.JPG", jpatch(base, dqt + 4, 0x20), D)  # precision 2
    add("HJ13.JPG", jpatch(base, sof + 12, 2), D)    # Y's table: never defined
    # --- the frame -----------------------------------------------------------
    add("HJ14.JPG", jpatch(base, sof + 11, 0x31), P_)  # Y 3 x 1
    add("HJ15.JPG", jpatch(base, sof + 14, 0x21), P_)  # Cb 2 x 1
    add("HJ16.JPG", jpatch(base, sof + 11, 0x02), Hd)  # H = 0
    add("HJ17.JPG", jpatch(base, sof + 7, 0, 0), Dm)   # width 0
    add("HJ18.JPG", jpatch(base, sof + 5, 0xFF, 0xFF, 0xFF, 0xFF), Dm)
    add("HJ19.JPG", jpatch(base, sof + 9, 2), Dp)      # two components
    add("HJ20.JPG", jpatch(base, sof + 4, 12), Dp)     # 12-bit samples
    add("HJ21.JPG", jpatch(base, sof + 1, 0xC3), P_)   # lossless
    add("HJ22.JPG", jpatch(base, sof + 1, 0xC9), P_)   # arithmetic
    add("HJ23.JPG", jpatch(base, sof + 13, 1), Hd)     # a repeated id
    # --- lengths that lie, bytes that are not markers ------------------------
    add("HJ24.JPG", jpatch(base, app0 + 2, 0xFF, 0xF0), T)  # past the file
    add("HJ25.JPG", jpatch(base, app0 + 2, 0, 1), Hd)  # a length of 1
    add("HJ26.JPG", jpatch(base, dqt, 0x00), Hd)       # not a marker
    # a frame header length past the 2,048 any head holds
    add("HJ27.JPG", jpatch(base, sof + 2, 0x08, 0x00), Hd)
    # seventeen heads' worth of segments before the frame header (106.19)
    pad = b"".join(b"\xFF\xE2\x08\x04" + bytes(2050) for _ in range(17))
    add("HJ28.JPG", base[:2] + pad + base[2:], Hd)
    # ...and three, which a header walk follows (good)
    pad3 = b"".join(b"\xFF\xE2\x08\x04" + bytes(2050) for _ in range(3))
    add("JHEADS3.JPG", base[:2] + pad3 + base[2:])
    # --- the scan -----------------------------------------------------------
    add("HJ29.JPG", jpatch(base, sos + 5, 9), D)       # a component not framed
    add("HJ30.JPG", jpatch(base, sos + 4, 2), D)       # Ns 2, length for 3
    add("HJ31.JPG", base[:sof] + base[sos:], Hd)       # a scan before a frame
    add("HJ32.JPG", base[:sos] + b"\xFF\xD9", D)       # EOI, no scan
    add("HJ33.JPG", base[:sos] + base[sof:sof + 19] + base[sos:], D)  # 2 frames
    add("HJ34.JPG", jpatch(base, sos + 6, 0x44), D)    # tables 4
    dri = jfind(rst, 0xDD)[0]
    add("HJ35.JPG", jpatch(rst, dri + 3, 5), D)        # DRI's length 5
    r0 = rst.find(b"\xFF\xD0")
    add("HJ36.JPG", jpatch(rst, r0 + 1, 0xD3), D)      # RST3 where RST0 is due
    add("HJ37.JPG", rst[:r0] + rst[r0 + 2:], D)        # RST0 missing
    # --- progressive -------------------------------------------------------
    ps = jsos_all(prog)
    ac = ps[1]                                         # Y 1-5, Al 2
    n = prog[ac + 4]
    k = ac + 5 + 2 * n
    add("HJ38.JPG", jpatch(prog, k, 6, 5), D)          # Ss 6 > Se 5
    add("HJ39.JPG", jpatch(prog, k + 1, 64), D)        # Se 64
    add("HJ40.JPG", jpatch(prog, k + 2, 0x20), D)      # Ah 2, Al 0
    add("HJ41.JPG", jpatch(prog, k + 2, 0x0E), D)      # Al 14
    dc = ps[0]
    n0 = prog[dc + 4]
    add("HJ42.JPG", jpatch(prog, dc + 5 + 2 * n0 + 1, 5), D)  # DC Se 5
    add("HJ43.JPG", prog[:ps[3]], T)                   # ends between scans
    add("HJ44.JPG", prog[:ps[4] + 60], T)              # ...and inside one
    # a DC first scan at Al 0 that a refinement then refines anyway: odd,
    # legal, and read (good)
    add("JPAL0.JPG", jpatch(prog, ps[0] + 5 + 2 * n0 + 1, 0, 0x00))
    # --- EXIF that lies is ignored, not refused; big-endian is read --------
    a1 = jfind(o1, 0xE1)[0]
    tif = a1 + 10
    add("JEXLIE.JPG", jpatch(o1, tif + 4, 0x40, 0x7F, 0, 0))  # IFD0 offset away
    mm = bytearray(o1)
    ln = (o1[a1 + 2] << 8) | o1[a1 + 3]
    exif = (b"Exif\0\0MM\0*\0\0\0\x08\0\x01\x01\x12\0\x03\0\0\0\x01"
            b"\0\x06\0\0\0\0\0\0")
    add("JEXMM.JPG", bytes(o1[:a1]) + b"\xFF\xE1" +
        bytes(((len(exif) + 2) >> 8, (len(exif) + 2) & 255)) + exif +
        bytes(o1[a1 + 2 + ln:]))
    # fill bytes before a marker, and a standalone RST outside a scan (good)
    add("JFILL.JPG", base[:dqt] + b"\xFF\xFF\xFF" + base[dqt:])


def wcorpus(add, W, H):
    """The GIF and PNG fixtures, good and hostile."""
    rgb = rgb_rows(W, H)
    i200 = idx_rows(W, H, 200)
    p200 = pal_n(200)
    # --- GIF ------------------------------------------------------------------
    i2 = idx_rows(W, H, 2)
    add("G1.GIF", gif(W, H, i2, gpal=[(10, 20, 30), (250, 240, 230)], ver=b"87a"))
    i16 = idx_rows(W, H, 16)
    add("G4.GIF", gif(W, H, i16, gpal=pal_n(16)))
    add("G8.GIF", gif(W, H, i200, gpal=p200 + [(0, 0, 0)] * 56))
    add("GI.GIF", gif(W, H, i16, gpal=pal_n(16), ilace=True))
    add("GL.GIF", gif(W, H, i16, lpal=pal_n(16)))
    add("GLG.GIF", gif(W, H, i16, gpal=pal_n(4), lpal=pal_n(16)[::-1]))
    add("GT.GIF", gif(W, H, i16, gpal=pal_n(16), trans=5))
    sub = [r[:7] for r in idx_rows(9, 4, 16)]
    add("GF.GIF", gif(W, H, sub, gpal=pal_n(16), fw=7, fh=4, left=3, top=2,
                      bgi=9))
    sub2 = [r[:6] for r in idx_rows(8, 5, 16)]
    add("GFI.GIF", gif(W, H, sub2, gpal=pal_n(16), fw=6, fh=5, left=9, top=3,
                       ilace=True, trans=2))           # clipped, odd top
    com = b"\x21\xFE" + subblocks(b"made by pixcorpus " * 20)
    app = b"\x21\xFF" + subblocks(b"NETSCAPE2.0") [:-1] + b"\x03\x01\x00\x00\x00"
    add("GX.GIF", gif(W, H, i16, gpal=pal_n(16), exts=(com, app),
                      more_frames=True))
    big = idx_rows(64, 48, 256)
    add("GBIG.GIF", gif(64, 48, big, gpal=pal_n(256)))
    add("GBIGI.GIF", gif(64, 48, big, gpal=pal_n(256), ilace=True))
    noise = [[(x * 7919 + y * 104729 + (x * y * 31)) % 251 for x in range(96)]
             for y in range(64)]
    add("GFULL.GIF", gif(96, 64, noise, gpal=pal_n(256)))   # Clear at 4096
    add("GDEF.GIF", gif(96, 64, noise, gpal=pal_n(256), deferred=True))
    add("GCLR.GIF", gif(W, H, i16, gpal=pal_n(16), clear_every=9))
    add("GM2.GIF", gif(W, H, i2, gpal=[(0, 0, 0), (255, 255, 255)], mincode=2))
    # --- PNG ------------------------------------------------------------------
    def grid(f):
        return [[f(x, y) for x in range(W)] for y in range(H)]
    for d in (1, 2, 4, 8, 16):
        mx = (1 << d) - 1
        mul = 997 if d == 16 else 1
        add("P0_%d.PNG" % d, png(W, H, 0, d, grid(
            lambda x, y: ((x * 3 + y * 5) * mul) % (mx + 1))))
    for d in (8, 16):
        k = 257 if d == 16 else 1
        add("P2_%d.PNG" % d, png(W, H, 2, d, [[tuple(c * k for c in px) for px in r]
                                              for r in rgb]))
        add("P4_%d.PNG" % d, png(W, H, 4, d, grid(lambda x, y: (
            ((x * 19 + y * 7) & 255) * k, ((x * 40 + y * 90) & 255) * k))))
        add("P6_%d.PNG" % d, png(W, H, 6, d, grid(lambda x, y: tuple(
            v * k for v in (rgb[y][x] + (((x * 51 + y * 13) & 255) if (x + y) % 3
                                         else (0 if x & 1 else 255),))))))
    for d in (1, 2, 4, 8):
        n = min(200, 1 << d)
        add("P3_%d.PNG" % d, png(W, H, 3, d, idx_rows(W, H, n),
                                 plte=pal_n(n)))
    # interlaced: every pass, on one of each kind
    add("P0_4I.PNG", png(W, H, 0, 4, grid(lambda x, y: (x + 2 * y) & 15), ilace=True))
    add("P2_8I.PNG", png(W, H, 2, 8, rgb, ilace=True))
    add("P3_2I.PNG", png(W, H, 3, 2, idx_rows(W, H, 4), plte=pal_n(4), ilace=True))
    add("P4_16I.PNG", png(W, H, 4, 16, grid(lambda x, y: (x * 4000, y * 9000)),
                          ilace=True))
    add("P6_8I.PNG", png(W, H, 6, 8, grid(lambda x, y: rgb[y][x] + ((x * 37) & 255,)),
                         ilace=True))
    add("P1PX.PNG", png(1, 1, 2, 8, [[(200, 100, 50)]], ilace=True))
    add("P3X2.PNG", png(3, 2, 0, 8, [[10, 20, 30], [40, 50, 60]], ilace=True))
    # transparency
    add("P3T.PNG", png(W, H, 3, 8, i200, plte=p200,
                       trns=bytes((i * 37) & 255 for i in range(150))))
    add("P0T.PNG", png(W, H, 0, 8, grid(lambda x, y: (x * 20) & 255),
                       trns=struct.pack(">H", 40)))
    add("P0T4.PNG", png(W, H, 0, 4, grid(lambda x, y: (x + y) & 15),
                        trns=struct.pack(">H", 3)))
    add("P2T.PNG", png(W, H, 2, 8, rgb, trns=struct.pack(">HHH", *rgb[2][3])))
    add("P2T16.PNG", png(W, H, 2, 16, [[tuple(c * 257 for c in px) for px in r]
                                       for r in rgb],
                         trns=struct.pack(">HHH", *(c * 257 for c in rgb[1][1]))))
    # the stream's shapes
    s8 = png_stream(W, H, 2, 8, rgb)
    add("PMI.PNG", png(W, H, 2, 8, z=zlib.compress(s8), idat_split=7,
                       zero_idat=True))
    add("PST.PNG", png(W, H, 2, 8, z=deflate(s8, "stored", stored_max=50)))
    add("PFX.PNG", png(W, H, 2, 8, z=deflate(s8, "fixed")))
    add("PDY.PNG", png(W, H, 2, 8, z=deflate(s8, "dyn", blocks=3)))
    grey = png_stream(W, H, 0, 8, grid(lambda x, y: ((x * y) % 13) * 9),
                      fseq=(0,))
    freq = {}
    for c in grey:
        freq[c] = freq.get(c, 0) + 1
    order = sorted(freq, key=lambda c: (-freq[c], c)) + [256]
    add("PLONG.PNG", png(W, H, 0, 8, z=deflate(grey, "dyn", matches=False,
                                                ll=unary_lens(order),
                                                dl=[0] * 30),
                         stream=grey))                # 15-bit codes, no dists
    runs = png_stream(W, H, 0, 8, grid(lambda x, y: (y * 31) & 255), fseq=(0,))
    add("PD1.PNG", png(W, H, 0, 8, z=deflate(runs, "dyn", matches="runs",
                                              dl=[1] + [0] * 29),
                       stream=runs))                  # one distance code
    add("PTXT.PNG", png(W, H, 0, 8, grid(lambda x, y: (x * 11) & 255),
                        extra=(chunk(b"tEXt", b"Comment\0" + b"x" * 3000),
                               chunk(b"gAMA", struct.pack(">I", 45455)),
                               chunk(b"zzZz", b"\1\2\3"))))
    add("PCRC.PNG", png(W, H, 0, 8, grid(lambda x, y: (x * 9) & 255),
                        crc_bad=True))        # a CRC is not read (106.18)
    add("PBIG.PNG", png(64, 48, 2, 8, rgb_rows(64, 48)))
    add("PBIGI.PNG", png(64, 48, 2, 8, rgb_rows(64, 48), ilace=True))
    add("PBIG3I.PNG", png(64, 48, 3, 8, idx_rows(64, 48, 256), plte=pal_n(256),
                          ilace=True))
    # inflate's slow paths, which no fixture reached until the wave-3 review
    # measured it (F9): a lit/len tree whose long codes fill MORE than the 64
    # sub-tables, so the canonical walk decodes real symbols; a distance code
    # past nine bits (the distance sub-table); a 16-bit grey's tRNS key,
    # plain and Adam7
    add("PSUB65.PNG", subtab_png(65, 60, 9))
    add("PDLONG.PNG", png(W, H, 0, 8, z=deflate(
        runs, "dyn", matches="runs", dl=[10, 10] + list(range(1, 10)) +
        [0] * 19), stream=runs))
    k16 = [[(0x1234, 0x12FF, 0x3412)[(x + y) % 3] for x in range(9)]
           for y in range(5)]
    add("PG16K.PNG", png(9, 5, 0, 16, k16, trns=struct.pack(">H", 0x1234)))
    add("PG16KI.PNG", png(9, 5, 0, 16, k16, ilace=True,
                          trns=struct.pack(">H", 0x1234)))
    # --- the HOSTILE half -----------------------------------------------------
    g = gif(W, H, i16, gpal=pal_n(16))
    add("HG01.GIF", g[:-12], P.PXD_TRUNC)                   # data cut short
    add("HG02.GIF", gif(W, H, i16, gpal=pal_n(16), codes=pack_lsb(
        [(16, 5), (3, 5), (30, 5), (17, 5)])), P.PXD_DATA)  # past the table
    add("HG03.GIF", gif(W, H, i16, gpal=pal_n(16), codes=pack_lsb(
        [(16, 5), (18, 5), (17, 5)])), P.PXD_DATA)          # not a root
    bad = bytearray(g)
    bad[13 + 48 + 10] = 9                                   # min code size 9
    add("HG04.GIF", bytes(bad), P.PXD_DATA)
    add("HG05.GIF", gif(W, H, i16), P.PXD_DATA)             # no table at all
    add("HG06.GIF", g[:13 + 48] + b"\x99" + g[13 + 48:], P.PXD_DATA)
    add("HG07.GIF", gif(0, H, [], gpal=pal_n(16), codes=b""), P.PXD_DIMS)
    add("HG08.GIF", gif(65535, 2, [], gpal=pal_n(16), codes=b""), P.PXD_DIMS)
    add("HG09.GIF", g[:11], P.PXD_HEAD)
    add("HG10.GIF", gif(W, H, i16, gpal=pal_n(16), codes=pack_lsb(
        [(16, 5), (3, 5), (17, 5)])), P.PXD_TRUNC)          # EOI too soon
    add("HG11.GIF", gif(W, H, [[]], gpal=pal_n(16), fw=0, fh=3, codes=b""),
        P.PXD_DATA)                                         # a frame of 0
    add("HG12.GIF", g[:13 + 48] + b"\x21\xFE\x40abc", P.PXD_TRUNC)
    add("HG13.GIF", g[:13 + 48] + b"\x3B", P.PXD_DATA)      # trailer first
    # PNG
    gp = png(W, H, 0, 8, grid(lambda x, y: x * 7 + y))
    add("HP01.PNG", gp[:30], P.PXD_HEAD)
    add("HP02.PNG", png(W, H, 0, 8, ihdr=struct.pack(">IIBBBBBB", W, H, 8, 0, 0,
                                                    0, 0, 0), z=b""), P.PXD_HEAD)
    add("HP03.PNG", png(0, H, 0, 8, z=b""), P.PXD_DIMS)
    add("HP04.PNG", png(70000, H, 0, 8, z=b""), P.PXD_DIMS)
    add("HP05.PNG", png(W, H, 3, 16, z=b""), P.PXD_DEPTH)
    add("HP06.PNG", png(W, H, 5, 8, z=b""), P.PXD_DEPTH)
    add("HP07.PNG", png(W, H, 0, 8, ihdr=struct.pack(">IIBBBBB", W, H, 8, 0, 0,
                                                    0, 2), z=b""), P.PXD_PACK)
    add("HP08.PNG", png(W, H, 0, 8, ihdr=struct.pack(">IIBBBBB", W, H, 8, 0, 1,
                                                    0, 0), z=b""), P.PXD_PACK)
    add("HP09.PNG", png(8192, 2, 6, 8, z=b""), P.PXD_BIG)
    add("HP10.PNG", png(W, H, 3, 8, idx_rows(W, H, 4)), P.PXD_DATA)  # no PLTE
    add("HP11.PNG", png(W, H, 3, 8, idx_rows(W, H, 4),
                        extra=(chunk(b"PLTE", b"\1\2\3\4\5\6\7"),)), P.PXD_DATA)
    add("HP12.PNG", gp[:33] + chunk(b"IEND", b"") + gp[33:], P.PXD_DATA)
    add("HP13.PNG", gp[:33] + b"\x80\0\0\0tEXt" + gp[33:], P.PXD_DATA)
    add("HP14.PNG", gp[:-30], P.PXD_TRUNC)
    s0 = png_stream(W, H, 0, 8, grid(lambda x, y: x * 7 + y))
    zz = deflate(s0, "fixed")
    add("HP15.PNG", png(W, H, 0, 8, z=b"\x77" + zz[1:]), P.PXD_DATA)  # CM 7
    add("HP16.PNG", png(W, H, 0, 8, z=b"\x78\x20" + zz[2:]), P.PXD_DATA)  # FDICT
    bw = BitW()
    bw.put(1, 1)
    bw.put(3, 2)
    add("HP17.PNG", png(W, H, 0, 8, z=b"\x78\x01" + bw.bytes() + b"\0" * 8),
        P.PXD_DATA)                                         # BTYPE 3
    bw = BitW()
    bw.put(1, 1)
    bw.put(0, 2)
    bw.align()
    bw.put(10, 16)
    bw.put(10, 16)
    add("HP18.PNG", png(W, H, 0, 8, z=b"\x78\x01" + bw.bytes() + b"\0" * 20),
        P.PXD_DATA)                                         # LEN != ~NLEN

    def dyn(ll, dl, **kw):
        bw = BitW()
        bw.put(1, 1)
        bw.put(2, 2)
        put_dyn_header(bw, ll, dl, **kw)
        return b"\x78\x01" + bw.bytes() + b"\0" * 64
    full = [8] * 256 + [8] + [0] * 29
    over = [7] * 256 + [7] + [0] * 29                       # 257 codes of 7
    add("HP19.PNG", png(W, H, 0, 8, z=dyn(over, [1, 1])), P.PXD_DATA)
    under = [9] * 256 + [9] + [0] * 29                      # half the space
    add("HP20.PNG", png(W, H, 0, 8, z=dyn(under, [1, 1])), P.PXD_DATA)
    clo = [0] * 19
    for s in (0, 8, 1, 2):
        clo[s] = 1                                          # four 1-bit codes
    add("HP21.PNG", png(W, H, 0, 8, z=dyn(full, [1, 1], cl_lens=clo)),
        P.PXD_DATA)
    clu = [0] * 19
    clu[8] = 2                                              # incomplete CL code
    clu[0] = 2
    clu[1] = 2
    add("HP22.PNG", png(W, H, 0, 8, z=dyn(full, [1, 1], cl_lens=clu,
                                           raw=[(8, None)] * 4)), P.PXD_DATA)
    add("HP23.PNG", png(W, H, 0, 8, z=dyn(full, [1, 1],
                                           raw=[(16, 0)] + [(8, None)] * 300)),
        P.PXD_DATA)                                         # repeat with none
    add("HP24.PNG", png(W, H, 0, 8, z=dyn(full, [1, 1],
                                           raw=[(8, None)] * 250 + [(18, 127)])),
        P.PXD_DATA)                                         # past the end
    add("HP25.PNG", png(W, H, 0, 8, z=dyn(full + [8, 8], [1, 1], hlit=288)),
        P.PXD_DATA)                                         # HLIT 288
    noeob = [8] * 256 + [0] + [8] * 2 + [0] * 27
    add("HP26.PNG", png(W, H, 0, 8, z=dyn(noeob, [1, 1])), P.PXD_DATA)
    bw = BitW()                                             # distance too far
    bw.put(1, 1)
    bw.put(1, 2)
    fl, fd = fixed_lens()
    put_syms(bw, [0, 5, (4, 3)], fl, fd)
    add("HP27.PNG", png(W, H, 0, 8, z=b"\x78\x01" + bw.bytes() + b"\0" * 64),
        P.PXD_DATA)
    bw = BitW()                                             # length symbol 286
    bw.put(1, 1)
    bw.put(1, 2)
    lc = canon(fl)
    bw.code(lc[0], fl[0])
    bw.code(lc[286], fl[286])
    add("HP28.PNG", png(W, H, 0, 8, z=b"\x78\x01" + bw.bytes() + b"\0" * 64),
        P.PXD_DATA)
    bw = BitW()                                             # distance 30
    bw.put(1, 1)
    bw.put(1, 2)
    dc = canon(fd)
    bw.code(lc[0], fl[0])
    bw.code(lc[0], fl[0])
    bw.code(lc[257], fl[257])
    bw.code(dc[30], 5)
    add("HP29.PNG", png(W, H, 0, 8, z=b"\x78\x01" + bw.bytes() + b"\0" * 64),
        P.PXD_DATA)
    bf = bytearray(s0)
    bf[(W + 1) * 3] = 5                                     # filter type 5
    add("HP30.PNG", png(W, H, 0, 8, z=zlib.compress(bytes(bf)), stream=bytes(bf)),
        P.PXD_DATA)
    add("HP31.PNG", png(W, H, 0, 8, z=deflate(s0[:40], "fixed"),
                        stream=s0[:40]), P.PXD_TRUNC)       # ends too soon
    add("HP32.PNG", gp[:33] + b"\x00\x10\x00\x00tEXt" + b"x" * 64, P.PXD_TRUNC)
    add("HP33.PNG", png(W, H, 0, 8, z=deflate(s0, "fixed")[:20], iend=False),
        P.PXD_TRUNC)                                        # the file ends
    # a STORED block whose length runs past the image data: the copy reaches
    # the zeros past the end - far past them (HP34), and by only three bytes,
    # the rows complete inside the four the guest reads as zero (HP35) - the
    # speed pass's de-chunked buffer ran past its guard on the first (SPEC.md
    # 106.22)
    stl = len(s0)
    add("HP34.PNG", png(W, H, 0, 8, z=b"\x78\x01\x01" + struct.pack(
        "<HH", 1000, 1000 ^ 0xFFFF) + s0[:10]), P.PXD_TRUNC)
    add("HP35.PNG", png(W, H, 0, 8, z=b"\x78\x01\x01" + struct.pack(
        "<HH", stl, stl ^ 0xFFFF) + s0[:stl - 3]), P.PXD_TRUNC)


# =============================================================================
# THE EXTRAS (SPEC.md 106.25): TIFF, ICO and CUR, IFF ILBM and PBM, MacPaint -
# writers of our own, so a hostile file is a parameter rather than a patch
# =============================================================================
def packbits(data):
    """PackBits / ByteRun1: a repeat for two or more of a byte (128 at most),
    literals of up to 128 bytes."""
    out = bytearray()
    lit = bytearray()

    def flush():
        for i in range(0, len(lit), 128):
            part = lit[i:i + 128]
            out.append(len(part) - 1)
            out.extend(part)
        del lit[:]
    for v, g in itertools.groupby(data):
        n = len(list(g))
        while n >= 2:
            k = min(n, 128)
            flush()
            out.extend((257 - k, v))
            n -= k
        if n:
            lit.append(v)
    flush()
    return bytes(out)


def tlzw(data):
    """TIFF LZW as libtiff writes it: Clear first, codes MSB first from 9
    bits, the size growing one code EARLY, a Clear when the next free code
    would be 4094, the End code last (at the width the reader then has)."""
    codes = []
    nb, free, tab = 9, 258, {}

    def put(c):
        codes.append((c, nb))
    put(256)
    if data:
        w = data[0]
        for c in data[1:]:
            k = (w << 8) | c
            t = tab.get(k)
            if t is not None:
                w = t
                continue
            put(w)
            tab[k] = free
            free += 1
            if free == 4094:
                put(256)
                nb, free, tab = 9, 258, {}
            elif free > (1 << nb) - 1:
                nb += 1
            w = c
        put(w)
        free += 1
        if free > (1 << nb) - 1 and nb < 12:
            nb += 1
    put(257)
    acc = n = 0
    out = bytearray()
    for c, k in codes:
        acc = (acc << k) | c
        n += k
        while n >= 8:
            n -= 8
            out.append((acc >> n) & 255)
    if n:
        out.append((acc << (8 - n)) & 255)
    return bytes(out)


def msb_codes(codes):
    """(code, width) pairs packed MSB first: a hand-made LZW stream."""
    acc = n = 0
    out = bytearray()
    for c, k in codes:
        acc = (acc << k) | c
        n += k
        while n >= 8:
            n -= 8
            out.append((acc >> n) & 255)
    if n:
        out.append((acc << (8 - n)) & 255)
    return bytes(out)


def pack_msb(vals, d):
    """One row of d-bit values, MSB first, padded to a byte."""
    acc = nb = 0
    line = bytearray()
    for v in vals:
        acc = (acc << d) | v
        nb += d
        if nb == 8:
            line.append(acc)
            acc = nb = 0
    if nb:
        line.append(acc << (8 - nb))
    return bytes(line)


TSZ = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8}


def tiff(w, h, photo, bits, rows, spp=1, comp=1, rps=None, pred=1, be=False,
         first=False, cmap=None, extra=None, short=False, tags=None, drop=(),
         strips=None, soffs=None, ifd_at=None, stride=None, cut=None):
    """A TIFF of `rows` (each row's sample bytes, packed). `first` puts the
    IFD before the data (Pillow's layout), else after it (libtiff's); `ifd_at`
    and `stride` place the IFD and each out-of-line value at offsets of their
    own (a head each); `tags` adds or replaces entries - tag: (type, values)
    or (type, count, raw 4 bytes) - and `drop` removes them."""
    o = ">" if be else "<"
    rows = [bytearray(r) for r in rows or []]
    if pred == 2:
        for r in rows:
            for i in range(len(r) - 1, spp - 1, -1):
                r[i] = (r[i] - r[i - spp]) & 255
    rp = h if rps is None or rps > h or rps == 0 else rps
    if strips is None:
        strips = []
        for s in range(0, h, rp):
            blk = rows[s:s + rp]
            if comp == 5:
                strips.append(tlzw(b"".join(blk)))
            elif comp == 32773:
                strips.append(b"".join(packbits(bytes(r)) for r in blk))
            else:
                strips.append(b"".join(bytes(r) for r in blk))
    ns = len(strips)
    at = 4 if not short else 3
    e = {256: (at, [w]), 257: (at, [h]), 258: (3, [bits] * spp),
         259: (3, [comp]), 262: (3, [photo]), 273: (at, [0] * ns),
         277: (3, [spp]), 279: (at, [len(s) for s in strips]), 284: (3, [1])}
    if rps is not None:
        e[278] = (4, [rps])
    if pred != 1:
        e[317] = (3, [pred])
    if cmap is not None:
        e[320] = (3, [c[0] * 257 for c in cmap] + [c[1] * 257 for c in cmap] +
                  [c[2] * 257 for c in cmap])
    if extra is not None:
        e[338] = (3, [extra])
    e.update(tags or {})
    for t in drop:
        e.pop(t, None)
    tl = sorted(e)

    def blob(t):
        ent = e[t]
        if len(ent) == 3:
            return None
        typ, vals = ent
        sz = TSZ.get(typ, 1)
        if len(vals) * sz <= 4:
            return None
        fmt = {1: "B", 2: "B", 3: "H", 4: "I"}.get(typ, "Q")
        b = b"".join(struct.pack(o + fmt, v) for v in vals)
        return b + b"\0" * (len(b) & 1)
    isz = 2 + 12 * len(tl) + 4
    data = b"".join(strips)
    pos = {}
    if first:
        ifd = ifd_at if ifd_at is not None else 8
        p = ifd + isz
        for t in tl:
            b = blob(t)
            if b is not None:
                if stride:
                    p = (p + stride - 1) // stride * stride
                pos[t] = p
                p += len(b)
        dat = p + (p & 1)
    else:
        dat = 8
        p = dat + len(data)
        p += p & 1
        for t in tl:
            b = blob(t)
            if b is not None:
                pos[t] = p
                p += len(b)
        ifd = p
    so = []
    q = dat
    for s in strips:
        so.append(q)
        q += len(s)
    if 273 in e and len(e[273]) == 2 and e[273][1] == [0] * ns:
        e[273] = (e[273][0], soffs if soffs is not None else so)
    end = max([ifd + isz, dat + len(data)] + [pos[t] + len(blob(t)) for t in pos])
    out = bytearray(end)
    out[0:8] = (b"MM" if be else b"II") + struct.pack(o + "HI", 42, ifd)
    out[dat:dat + len(data)] = data
    ib = bytearray(struct.pack(o + "H", len(tl)))
    for t in tl:
        ent = e[t]
        if len(ent) == 3:
            ib += struct.pack(o + "HHI", t, ent[0], ent[1]) + ent[2]
            continue
        typ, vals = ent
        b = blob(t)
        if b is None:
            fmt = {1: "B", 2: "B", 3: "H", 4: "I"}.get(typ, "B")
            raw = b"".join(struct.pack(o + fmt, v) for v in vals)
            ib += struct.pack(o + "HHI", t, typ, len(vals)) + (raw + b"\0" * 4)[:4]
        else:
            out[pos[t]:pos[t] + len(b)] = b
            ib += struct.pack(o + "HHII", t, typ, len(vals), pos[t])
    ib += b"\0\0\0\0"
    out[ifd:ifd + len(ib)] = ib
    return bytes(out[:cut] if cut is not None else out)


def t_grey(grid, d, wiz=False):
    mx = (1 << d) - 1
    return [pack_msb([mx - v if wiz else v for v in r], d) for r in grid]


def t_rgb(rows, alpha=None):
    out = []
    for y, r in enumerate(rows):
        line = bytearray()
        for x, px in enumerate(r):
            line += bytes(px)
            if alpha is not None:
                line.append(alpha(x, y))
        out.append(bytes(line))
    return out


def dib(w, h, bits, px, pal=None, mask=None, hsz=40, used=0, planes=1,
        comp=0, hfield=None, nomask=False):
    """An ICO's DIB: `px` top-down rows of indices (bits <= 8), (r, g, b)
    (24) or (r, g, b, a) (32); `mask` top-down rows of 0/1 (1 transparent)."""
    ih = struct.pack("<IiiHHIIiiII", hsz, w, 2 * h if hfield is None else hfield,
                     planes, bits, comp, 0, 0, 0, used, 0)
    ih += b"\0" * (hsz - 40)
    pb = b"".join(bytes((B, G, R, 0)) for (R, G, B) in (pal or []))
    stride = ((w * bits + 31) // 32) * 4
    xor = bytearray()
    for r in reversed(px):
        if bits <= 8:
            line = pack_msb(r, bits)
        elif bits == 24:
            line = b"".join(bytes((B, G, R)) for (R, G, B) in r)
        else:
            line = b"".join(bytes((B, G, R, A)) for (R, G, B, A) in r)
        xor += line + b"\0" * (stride - len(line))
    ms = ((w + 31) // 32) * 4
    am = bytearray()
    if not nomask:
        for r in reversed(mask or [[0] * w for _ in range(h)]):
            line = pack_msb(r, 1)
            am += line + b"\0" * (ms - len(line))
    return ih + pb + bytes(xor) + bytes(am)


def ico(imgs, typ=1, count=None, offs=None, reserved=0):
    """imgs: (dir w, dir h, bit count, blob) each; a CUR's bit count word is
    its hotspot's y."""
    n = len(imgs)
    out = bytearray(struct.pack("<HHH", reserved, typ,
                                n if count is None else count))
    p = 6 + 16 * n
    blobs = b""
    for i, (dw, dh, bc, b) in enumerate(imgs):
        off = p + len(blobs) if offs is None or offs[i] is None else offs[i]
        out += struct.pack("<BBBBHHII", dw & 255, dh & 255, 0, 0,
                           1 if typ == 1 else 3, bc, len(b), off)
        blobs += b
    return bytes(out) + blobs


def iff_chunk(cid, d):
    return cid + struct.pack(">I", len(d)) + d + (b"\0" if len(d) & 1 else b"")


def ilbm_raw(w, h, planes, idx, masking=0, pbm=False):
    out = bytearray()
    for y in range(h):
        if pbm:
            out += bytes(idx[y]) + (b"\0" if w & 1 else b"")
            continue
        pb = ((w + 15) >> 4) * 2
        for pl in range(planes):
            line = pack_msb([(v >> pl) & 1 for v in idx[y]], 1)
            out += line + b"\0" * (pb - len(line))
        if masking == 1:
            line = pack_msb([(x + y) & 1 for x in range(w)], 1)
            out += line + b"\0" * (pb - len(line))
    return bytes(out)


def iff(w, h, planes, idx, kind=b"ILBM", cmap=None, comp=0, masking=0,
        camg=None, pre=(), body=None, bmhd=None, cross=True, rowlen=None,
        tail=b""):
    """An IFF: BMHD, CMAP, CAMG, `pre` chunks, BODY. ByteRun1 runs cross
    plane rows and rows when `cross` (one stream), else each row on its own."""
    pbm = kind == b"PBM "
    if body is None:
        raw = ilbm_raw(w, h, planes, idx, masking, pbm)
        if comp == 1:
            if cross:
                body = packbits(raw)
            else:
                rl = rowlen or len(raw) // h
                body = b"".join(packbits(raw[i:i + rl])
                                for i in range(0, len(raw), rl))
        else:
            body = raw
    bm = bmhd if bmhd is not None else struct.pack(
        ">HHhhBBBBHBBhh", w, h, 0, 0, planes, masking, comp, 0, 0, 10, 11,
        w, h)
    ch = iff_chunk(b"BMHD", bm)
    if cmap is not None:
        ch += iff_chunk(b"CMAP", b"".join(bytes(c) for c in cmap))
    if camg is not None:
        ch += iff_chunk(b"CAMG", struct.pack(">I", camg))
    for c in pre:
        ch += c
    ch += iff_chunk(b"BODY", body)
    ch = kind + ch + tail
    return b"FORM" + struct.pack(">I", len(ch)) + ch


def mac_row(y):
    """MacPaint's picture: blocks of 24 bytes, a band of 50% grey, a
    diagonal, and two bytes at each end that differ row to row - so a
    stream that runs across rows carries both a repeat and a literal over
    a row's end."""
    a, b = (0xFF, 0x00) if (y >> 5) & 1 else (0x00, 0xFF)
    if 200 <= y < 260:
        a = b = 0xAA if y & 1 else 0x55
    r = bytearray(bytes((a,)) * 24 + bytes((b,)) * 24 + bytes((a,)) * 24)
    r[(y // 10) % 72] ^= 0x3C
    if y % 3:
        r[0:2] = bytes(((y * 13) & 255, (y * 17) & 255))
        r[70:72] = bytes(((y * 7) & 255, (y * 11) & 255))
    return bytes(r)


MAC_BODY = {}


def macpaint(macbin=False, cross=False):
    if cross not in MAC_BODY:
        rows = [mac_row(y) for y in range(720)]
        MAC_BODY[cross] = packbits(b"".join(rows)) if cross else \
            b"".join(packbits(r) for r in rows)
    hdr = struct.pack(">I", 2) + bytes((x * 13) & 255 for x in range(304))
    hdr += b"\0" * (512 - len(hdr))
    f = hdr + MAC_BODY[cross]
    if macbin:
        mb = bytearray(128)
        mb[1] = 7
        mb[2:9] = b"PICTURE"
        mb[65:69] = b"PNTG"
        mb[69:73] = b"MPNT"
        struct.pack_into(">I", mb, 83, len(f))
        f = bytes(mb) + f
    return f


def xcorpus(add, W, H):
    """TIFF, ICO and CUR, IFF and MacPaint (SPEC.md 106.25), good and
    hostile."""
    rgb = rgb_rows(W, H)
    grey = [[(x * 19 + y * 7) & 255 for x in range(W)] for y in range(H)]
    i2, i4, i16 = idx_rows(W, H, 2), idx_rows(W, H, 4), idx_rows(W, H, 16)
    i200 = idx_rows(W, H, 200)
    p200 = pal_n(200)
    p256 = p200 + [(0, 0, 0)] * 56

    def pgrid(d):
        mx = (1 << d) - 1
        return [[(x * 3 + y * 5) % (mx + 1) for x in range(W)]
                for y in range(H)]
    # --- TIFF: every kind, both byte orders, every packing --------------------
    add("FRGB.TIF", tiff(W, H, 2, 8, t_rgb(rgb), spp=3))
    add("FRGBM.TIF", tiff(W, H, 2, 8, t_rgb(rgb), spp=3, be=True, first=True))
    add("FRGBP.TIF", tiff(W, H, 2, 8, t_rgb(rgb), spp=3, comp=32773, rps=3))
    add("FRGBL.TIF", tiff(W, H, 2, 8, t_rgb(rgb), spp=3, comp=5, rps=2,
                          be=True))
    add("FRGBLP.TIF", tiff(W, H, 2, 8, t_rgb(rgb), spp=3, comp=5, pred=2,
                           rps=1, first=True))
    add("FRGBA0.TIF", tiff(W, H, 2, 8, t_rgb(rgb, lambda x, y: (x * 77) & 255),
                           spp=4, extra=0))             # ExtraSamples 0: ignored
    add("FRGBAN.TIF", tiff(W, H, 2, 8, t_rgb(rgb, lambda x, y: x * 9),
                           spp=4))                      # no ExtraSamples: ignored
    add("FRGBA1.TIF", tiff(W, H, 2, 8, t_rgb(rgb, lambda x, y: 255),
                           spp=4, extra=1, be=True))     # opaque: the picture
    add("FRGBA.TIF", tiff(W, H, 2, 8, t_rgb(rgb, lambda x, y: (x * 23 + y * 61)
                                            & 255 if (x + y) % 4 else
                                            (0 if x & 1 else 255)),
                          spp=4, extra=2, comp=5, pred=2, rps=3))
    add("FRGBS.TIF", tiff(W, H, 2, 8, t_rgb(rgb), spp=3, rps=0xFFFFFFFF,
                          short=True, first=True))       # SHORT, rps past h
    for d in (1, 2, 4, 8):
        g = pgrid(d) if d < 8 else grey
        add("FG%d.TIF" % d, tiff(W, H, 1, d, t_grey(g, d), rps=3))
        add("FW%d.TIF" % d, tiff(W, H, 0, d, t_grey(g, d, wiz=True), rps=2,
                                 be=True, first=True, comp=32773))
    add("FG8L.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), comp=5, rps=1))
    add("FG8LP.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), comp=5, pred=2,
                          be=True))
    add("FG8PL2.TIF", tiff(W, H, 1, 8, t_grey(grey, 8),
                           tags={284: (3, [2])}))        # planar 2, one sample
    for d, idx, n in ((1, i2, 2), (2, i4, 4), (4, i16, 16), (8, i200, 256)):
        pal = (p256 if d == 8 else pal_n(n))
        add("FP%d.TIF" % d, tiff(W, H, 3, d, [pack_msb(r, d) for r in idx],
                                 cmap=pal, rps=3))
        add("FP%dM.TIF" % d, tiff(W, H, 3, d, [pack_msb(r, d) for r in idx],
                                  cmap=pal, be=True, first=True, comp=5))
    add("FP8P.TIF", tiff(W, H, 3, 8, [bytes(r) for r in i200], cmap=p256,
                         comp=32773, rps=1, short=True))
    big = rgb_rows(64, 48)
    add("FBIG.TIF", tiff(64, 48, 2, 8, t_rgb(big), spp=3, rps=5))  # IFD a head on
    noise = [[(x * 7919 + y * 104729 + (x * y * 31)) % 251 for x in range(96)]
             for y in range(64)]
    add("FFULL.TIF", tiff(96, 64, 3, 8, [bytes(r) for r in noise],
                          cmap=pal_n(256), comp=5))       # a Clear at 4094
    col = [bytes((y & 255,)) for y in range(1024)]
    add("F1024.TIF", tiff(1, 1024, 1, 8, col, rps=1, first=True))
    add("FTPNG.TIF", png(W, H, 2, 8, rgb))               # the bytes win
    add("FCNTBIG.TIF", tiff(W, H, 1, 8, t_grey(grey, 8),
                            tags={279: (4, [0x7FFFFFFF])}))  # a count past
                                                    # the file: rows first
    # 16 heads exactly: the IFD past the first head and ten values, two
    # 2,400-byte arrays, each of its own (the rule SPEC.md 106.25 states)
    sp = {t: (4, [v, 0]) for t, v in ((256, 1), (257, 600))}
    sp.update({t: (3, [v, v, v]) for t, v in ((258, 8), (259, 1), (262, 1),
                                               (266, 1), (277, 1), (284, 1))})
    sp[278] = (4, [1, 1])
    sp[317] = (3, [1, 1, 1])
    col600 = [bytes(((y * 7) & 255,)) for y in range(600)]
    add("FHEAD16.TIF", tiff(1, 600, 1, 8, col600, rps=1, first=True,
                            ifd_at=2048, stride=2048, tags=sp))
    # --- TIFF: the HOSTILE half ---------------------------------------------
    good = tiff(W, H, 1, 8, t_grey(grey, 8), rps=3, first=True)
    add("HF01.TIF", good[:7], P.PXD_HEAD)                     # under 8 bytes
    add("HF02.TIF", b"IM" + good[2:], P.PXD_HEAD)             # no byte order
    add("HF03.TIF", good[:2] + b"\x2B\x00" + good[4:], P.PXD_HEAD)   # 43
    add("HF04.TIF", good[:4] + b"\x04\0\0\0" + good[8:], P.PXD_HEAD)
    add("HF05.TIF", good[:4] + b"\x00\x00\x01\x00" + good[8:], P.PXD_TRUNC)
    add("HF06.TIF", good[:8] + b"\0\0" + good[10:], P.PXD_HEAD)      # 0 tags
    add("HF07.TIF", good[:8] + b"\xAB\0" + good[10:], P.PXD_HEAD)    # 171
    add("HF08.TIF", good[:8] + b"\x09\0" + good[10:60], P.PXD_TRUNC)  # IFD cut
    add("HF09.TIF", tiff(W, H, 1, 8, t_grey(grey, 8),
                         tags={256: (1, [W])}), P.PXD_HEAD)   # BYTE width
    add("HF10.TIF", tiff(W, H, 1, 8, t_grey(grey, 8),
                         tags={259: (5, [1])}), P.PXD_HEAD)   # RATIONAL
    add("HF11.TIF", tiff(W, H, 1, 8, t_grey(grey, 8),
                         tags={277: (3, 0, b"\1\0\0\0")}), P.PXD_HEAD)  # count 0
    add("HF12.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), drop=(256,)), P.PXD_HEAD)
    add("HF13.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), drop=(262,)), P.PXD_HEAD)
    add("HF14.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), drop=(273,)), P.PXD_HEAD)
    add("HF15.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), drop=(279,)), P.PXD_HEAD)
    add("HF16.TIF", tiff(W, H, 1, 8, t_grey(grey, 8),
                         tags={256: (4, [0])}), P.PXD_DIMS)
    add("HF17.TIF", tiff(W, H, 1, 8, t_grey(grey, 8),
                         tags={257: (4, [8193])}), P.PXD_DIMS)
    add("HF18.TIF", tiff(W, H, 1, 8, t_grey(grey, 8),
                         tags={256: (4, [70000, 1])}), P.PXD_DIMS)  # out of line
    add("HF19.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), comp=7),
        P.PXD_PACK)                                       # JPEG-in-TIFF
    add("HF20.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), comp=8), P.PXD_PACK)
    add("HF21.TIF", tiff(W, H, 0, 1, t_grey(i2, 1), comp=4), P.PXD_PACK)
    add("HF22.TIF", tiff(W, H, 5, 8, t_rgb([[px + (9,) for px in r]
                                            for r in rgb]), spp=4),
        P.PXD_DEPTH)                                      # CMYK
    add("HF23.TIF", tiff(W, H, 6, 8, t_rgb(rgb), spp=3), P.PXD_DEPTH)
    add("HF24.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), tags={266: (3, [2])}),
        P.PXD_PACK)                                       # FillOrder 2
    add("HF25.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), rps=0), P.PXD_HEAD)
    add("HF26.TIF", tiff(W, H, 2, 8, t_rgb(rgb), spp=3,
                         tags={284: (3, [2])}), P.PXD_PACK)    # planar RGB
    add("HF27.TIF", tiff(W, H, 1, 4, t_grey(pgrid(4), 4), pred=2), P.PXD_PACK)
    add("HF28.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), pred=3), P.PXD_PACK)
    add("HF29.TIF", tiff(W, H, 1, 8, t_grey(grey, 8),
                         tags={322: (3, [16]), 323: (3, [16]),
                               324: (4, [8])}, drop=(273, 279)), P.PXD_PACK)
    add("HF30.TIF", tiff(W, H, 1, 16, [b"\0" * 2 * W] * H), P.PXD_DEPTH)
    add("HF31.TIF", tiff(W, H, 1, 3, [b"\0" * 5] * H), P.PXD_DEPTH)
    add("HF32.TIF", tiff(W, H, 2, 16, [b"\0" * 6 * W] * H, spp=3), P.PXD_DEPTH)
    add("HF33.TIF", tiff(W, H, 1, 8, [b"\0" * 2 * W] * H, spp=2), P.PXD_DEPTH)
    add("HF34.TIF", tiff(W, H, 3, 8, [bytes(r) for r in i200]), P.PXD_HEAD)
    add("HF35.TIF", tiff(W, H, 3, 4, [pack_msb(r, 4) for r in i16],
                         cmap=pal_n(15)), P.PXD_HEAD)     # 45 of 48
    add("HF36.TIF", tiff(W, H, 3, 4, [pack_msb(r, 4) for r in i16],
                         tags={320: (4, [0] * 48)}), P.PXD_HEAD)  # LONG map
    add("HF37.TIF", tiff(W, H, 3, 16, [b"\0" * 2 * W] * H), P.PXD_DEPTH)
    add("HF38.TIF", tiff(1, 1025, 1, 8, [b"\0"] * 1025, rps=1, tags={
        273: (4, 1025, b"\x08\0\0\0"), 279: (4, 1025, b"\x08\0\0\0")}),
        P.PXD_BIG)                                        # 1,025 strips
    add("HF39.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), rps=3,
                         tags={279: (4, [21, 21])}), P.PXD_HEAD)  # 2 of 3
    st = [bytes(r) for r in grey]
    add("HF40.TIF", tiff(W, H, 1, 8, st, rps=1, first=True,
                         soffs=[400 + 13 * (H - 1 - i) for i in range(H)]),
        P.PXD_PACK)                                       # strips backwards
    add("HF41.TIF", tiff(W, H, 1, 8, st, rps=1, first=True,
                         soffs=[300 + 12 * i for i in range(H)]),
        P.PXD_PACK)                                       # overlapping by one
    add("HF42.TIF", tiff(1, 600, 1, 8, col600, rps=1, first=True,
                         ifd_at=2048, stride=2048,
                         tags=dict(list(sp.items()) + [(338, (3, [0, 0, 0]))])),
        P.PXD_HEAD)                                       # 17 heads
    add("HF43.TIF", tiff(W, H, 3, 8, [bytes(r) for r in i200], cmap=p256,
                         first=True)[:300], P.PXD_TRUNC)  # ColorMap cut
    add("HF44.TIF", tiff(W, H, 1, 8, st, rps=1, first=True)[:150],
        P.PXD_TRUNC)                                      # array cut
    add("HF45.TIF", tiff(W, H, 1, 8, t_grey(grey, 8),
                         tags={257: (4, 2, struct.pack("<I", 100000))}),
        P.PXD_TRUNC)                                      # a value past the end
    add("HF46.TIF", good[:-5],
        P.PXD_TRUNC)                                      # data cut
    add("HF47.TIF", tiff(W, H, 1, 8, t_grey(grey, 8),
                         tags={279: (4, [W * H - 1])}), P.PXD_TRUNC)  # count lies
    add("HF48.TIF", tiff(W, H, 1, 8, t_grey(grey, 8), rps=3,
                         soffs=[8, 8 + 39, 0x7FFFFFF0]), P.PXD_TRUNC)  # past end
    pbcut = bytes((W - 1,)) + bytes(grey[0][:W - 1])
    add("HF49.TIF", tiff(W, H, 1, 8, None, comp=32773, rps=H,
                         strips=[pbcut]), P.PXD_TRUNC)    # PackBits cut
    lit = bytes((20,)) + bytes(range(21))                 # 21 for a row of 13
    add("HF50.TIF", tiff(W, 2, 1, 8, None, comp=32773,
                         strips=[lit]), P.PXD_TRUNC)      # 21 of 26 bytes
    add("FPBCROSS.TIF", tiff(W, 2, 1, 8, None, comp=32773,
                             strips=[lit + bytes((256 - 30, 77))]))  # a
                    # literal and a repeat across the row's end, the repeat
                    # stopping at the strip's last byte
    add("FPBTAIL.TIF", tiff(W, 1, 1, 8, None, comp=32773,
                            strips=[lit[:16]]))           # a literal of 21
                    # whose 13 needed bytes are in the strip: the rest unread
    lz = tlzw(b"".join(bytes(r) for r in grey))
    add("HF51.TIF", tiff(W, H, 1, 8, None, comp=5, strips=[lz[:40]]),
        P.PXD_TRUNC)                                      # LZW cut
    add("HF52.TIF", tiff(W, H, 1, 8, None, comp=5,
                         strips=[msb_codes([(256, 9), (65, 9), (66, 9),
                                            (257, 9)])]), P.PXD_TRUNC)  # End
    add("HF53.TIF", tiff(W, H, 1, 8, None, comp=5,
                         strips=[msb_codes([(256, 9), (65, 9), (300, 9),
                                            (257, 9)])]), P.PXD_DATA)  # > free
    add("HF54.TIF", tiff(W, H, 1, 8, None, comp=5,
                         strips=[msb_codes([(256, 9), (65, 9), (256, 9),
                                            (258, 9)])]), P.PXD_DATA)  # not a root
    add("HF55.TIF", tiff(W, H, 1, 8, None, comp=5,
                         strips=[msb_codes([(300, 9)] + [(0, 9)] * 9)]),
        P.PXD_DATA)                                       # no Clear, no root
    add("HF56.TIF", b"Not a TIFF, though it is named one." * 2, P.PXD_HEAD)
    # --- ICO and CUR -----------------------------------------------------------
    def mgrid(f):
        return [[1 if f(x, y) else 0 for x in range(W)] for y in range(H)]
    hole = mgrid(lambda x, y: (x * 3 + y * 5) % 7 == 0)
    rgba = [[px + (255,) for px in r] for r in rgb]
    add("I24.ICO", ico([(W, H, 24, dib(W, H, 24, rgb))]))
    add("I32.ICO", ico([(W, H, 32, dib(W, H, 32, rgba))]))
    add("I8.ICO", ico([(W, H, 8, dib(W, H, 8, i200, p200, used=200))]))
    add("I4.ICO", ico([(W, H, 4, dib(W, H, 4, i16, pal_n(16)))]))
    add("I1.ICO", ico([(W, H, 1, dib(W, H, 1, i2, [(0, 0, 0), (255, 255, 255)],
                                     hsz=108))]))
    add("I24M.ICO", ico([(W, H, 24, dib(W, H, 24, rgb, mask=hole))]))
    add("I8M.ICO", ico([(W, H, 8, dib(W, H, 8, i200, p200, used=200,
                                      mask=hole))]))
    add("I8F.ICO", ico([(W, H, 8, dib(W, H, 8, idx_rows(W, H, 256), pal_n(256),
                                      mask=hole))]))       # 256: the RGB path
    add("I4M.ICO", ico([(W, H, 4, dib(W, H, 4, i16, pal_n(9), used=9,
                                      mask=hole, hsz=124))]))  # 9..15 past it
    add("I1M.ICO", ico([(W, H, 1, dib(W, H, 1, i2, pal_n(2), mask=hole))]))
    a32 = [[px + (((x * 23 + y * 61) & 255) if (x + y) % 4 else
                  (0 if x & 1 else 255),) for x, px in enumerate(r)]
           for y, r in enumerate(rgb)]
    add("I32A.ICO", ico([(W, H, 32, dib(W, H, 32, a32, mask=hole))]))
    small = dib(4, 3, 4, idx_rows(4, 3, 16), pal_n(16))
    add("IPICK.ICO", ico([(4, 3, 4, small), (W, H, 24, dib(W, H, 24, rgb)),
                          (16, 3, 8, small), (W, H, 8, dib(W, H, 8, i200, p200,
                                                           used=200))]))
    add("ITIE.ICO", ico([(W, H, 4, dib(W, H, 4, i16, pal_n(16))),
                         (W, H, 24, dib(W, H, 24, rgb)),
                         (W, H, 24, dib(W, H, 8, i200, p200, used=200))]))
    add("I256.ICO", ico([(255, 255, 32, dib(W, H, 32, rgba)),
                         (0, 0, 4, dib(W, H, 4, i16, pal_n(16)))]))  # 0 is 256
    add("ICUR.CUR", ico([(W, H, 9, dib(W, H, 4, i16, pal_n(16), mask=hole)),
                         (W, H, 2, dib(W, H, 24, rgb))], typ=2))   # hotspots
    add("IPNG.ICO", ico([(4, 3, 4, small), (0, 0, 32, png(W, H, 2, 8, rgb))]))
    add("IPNGT.ICO", ico([(W, H, 32, png(W, H, 6, 8, [[px + (((x * 51 + y * 13)
                                                               & 255),)
                                                         for x, px in enumerate(r)]
                                                        for y, r in enumerate(rgb)]))]))
    add("IPNGX.DAT", ico([(W, H, 8, png(W, H, 3, 8, i200, plte=p200))]))
    add("I4X.DAT", ico([(W, H, 4, dib(W, H, 4, i16, pal_n(16)))]))
    # --- ICO: the HOSTILE half ------------------------------------------------
    g4 = ico([(W, H, 4, dib(W, H, 4, i16, pal_n(16)))])
    add("HI01.ICO", g4[:5], P.PXD_HEAD)
    add("HI02.ICO", b"\0\1" + g4[2:], P.PXD_HEAD)            # reserved 256
    add("HI03.ICO", g4[:2] + b"\3\0" + g4[4:], P.PXD_HEAD)   # type 3
    add("HI04.ICO", g4[:4] + b"\0\0" + g4[6:], P.PXD_HEAD)   # no images
    add("HI05.ICO", g4[:4] + b"\x80\0" + g4[6:], P.PXD_HEAD)  # 128
    add("HI06.ICO", g4[:4] + b"\x05\0" + g4[6:60], P.PXD_TRUNC)  # dir cut
    add("HI07.ICO", ico([(W, H, 4, b"")], offs=[10]) + g4[22:], P.PXD_HEAD)
    add("HI08.ICO", ico([(W, H, 4, b"")], offs=[5000]) + g4[22:], P.PXD_TRUNC)
    add("HI09.ICO", ico([(W, H, 4, dib(W, H, 4, i16, pal_n(16), hsz=40)[:4]
                          .replace(b"\x28", b"\x0C") + dib(W, H, 4, i16,
                                                           pal_n(16))[4:])]),
        P.PXD_HEAD)                                       # a 12-byte header
    add("HI10.ICO", ico([(W, H, 4, dib(0, H, 4, [[]] * H, pal_n(16)))]),
        P.PXD_DIMS)
    add("HI11.ICO", ico([(W, H, 4, dib(W, H, 4, i16, pal_n(16),
                                       hfield=2 * H + 1))]), P.PXD_HEAD)  # odd
    add("HI12.ICO", ico([(W, H, 4, dib(W, H, 4, i16, pal_n(16),
                                       hfield=-2 * H))]), P.PXD_HEAD)
    add("HI13.ICO", ico([(W, H, 4, dib(W, H, 4, i16, pal_n(16),
                                       hfield=0))]), P.PXD_DIMS)
    add("HI14.ICO", ico([(W, H, 4, dib(W, H, 4, i16, pal_n(16),
                                       planes=2))]), P.PXD_HEAD)
    add("HI15.ICO", ico([(W, H, 16, struct.pack("<IiiHHIIiiII", 40, W, 2 * H,
                                                1, 16, 0, 0, 0, 0, 0, 0) +
                          b"\0" * 400)]), P.PXD_DEPTH)
    add("HI16.ICO", ico([(W, H, 8, dib(W, H, 8, i200, p200, used=200,
                                       comp=1))]), P.PXD_PACK)
    add("HI17.ICO", ico([(W, H, 4, dib(W, H, 4, i16, pal_n(16), used=17))]),
        P.PXD_HEAD)
    add("HI18.ICO", ico([(W, H, 4, dib(W, H, 4, i16, pal_n(16), hsz=124))])
        [:22 + 100], P.PXD_TRUNC)                         # header cut
    add("HI19.ICO", g4[:22 + 40 + 30], P.PXD_TRUNC)       # palette cut
    add("HI20.ICO", g4[:-3], P.PXD_TRUNC)                 # mask cut
    add("HI21.ICO", ico([(W, H, 32, dib(W, H, 32, rgba, nomask=True))])[:-9],
        P.PXD_TRUNC)                                      # pixels cut
    add("HI22.ICO", ico([(0, 0, 1, dib(1024, 65, 1, [[]] * 65, pal_n(2))[:48])]),
        P.PXD_BIG)                                        # mask 8,320 bytes
    add("HI23.ICO", ico([(W, H, 32, png(W, H, 2, 8, rgb)[:40])]),
        P.PXD_TRUNC)                                      # PNG cut
    add("HI24.ICO", ico([(W, H, 32, png(W, H, 2, 8, rgb,
                                        ihdr=struct.pack(">IIBBBBB", W, H, 8, 2,
                                                         1, 0, 0)))]),
        P.PXD_PACK)                                       # PNG's own word
    add("HI25.ICO", ico([(W, H, 32, png(W, H, 2, 3, stream=b"\0" * 9,
                                        ihdr=struct.pack(">IIBBBBB", W, H, 3, 2,
                                                         0, 0, 0)))]),
        P.PXD_DEPTH)
    add("HI26.ICO", ico([(W, H, 32, png(W, H, 2, 8, rgb)[:12] + b"JHDR" +
                          png(W, H, 2, 8, rgb)[16:])]),
        P.PXD_HEAD)                                       # IHDR cut in a PNG
    add("HI27.ICO", ico([(W, H, 4, dib(W, H, 4, i16, pal_n(16)))])[:6 + 16 + 3],
        P.PXD_TRUNC)                                      # 3 bytes of the DIB
    # --- IFF: ILBM and PBM -----------------------------------------------------
    add("L8.LBM", iff(W, H, 8, i200, cmap=p200, comp=1))
    add("L8R.LBM", iff(W, H, 8, i200, cmap=p256))
    add("L4.LBM", iff(W, H, 4, i16, cmap=pal_n(16), comp=1, cross=False))
    add("L4M.IFF", iff(W, H, 4, i16, cmap=pal_n(16), comp=1, masking=1))
    add("L1.LBM", iff(W, H, 1, i2, cmap=[(250, 240, 230), (10, 20, 30)]))
    add("L1G.LBM", iff(W, H, 1, i2))                     # no CMAP: greys
    add("L5G.LBM", iff(W, H, 5, idx_rows(W, H, 32), comp=1))
    i64 = idx_rows(W, H, 64)
    add("L6EHB.LBM", iff(W, H, 6, i64, cmap=pal_n(32), camg=0x80, comp=1,
                         pre=(iff_chunk(b"DPPS", b"\1\2\3"),)))  # odd chunk
    add("L6.LBM", iff(W, H, 6, i64, cmap=pal_n(64), camg=0x4))
    add("L3S.LBM", iff(W, H, 3, idx_rows(W, H, 8), cmap=pal_n(5)))  # short CMAP
    add("LPBM.LBM", iff(W, H, 8, i200, kind=b"PBM ", cmap=p200, comp=1))
    add("LPBMR.LBM", iff(W, H, 8, i200, kind=b"PBM ", cmap=p256))
    add("LPBM8.IFF", iff(8, 4, 8, idx_rows(8, 4, 256), kind=b"PBM ",
                         cmap=pal_n(256)))                  # an even width
    add("L8X.DAT", iff(W, H, 8, i200, cmap=p200, comp=1))
    # 16 heads exactly: fifteen chunks of 2,040 bytes before BODY, whose
    # headers are a head each but the first's
    fill = tuple(iff_chunk(b"ANNO", bytes((k,)) * 2040) for k in range(15))
    add("LHEAD16.LBM", iff(W, H, 4, i16, cmap=pal_n(16), pre=fill))
    # --- IFF: the HOSTILE half ------------------------------------------------
    gl = iff(W, H, 4, i16, cmap=pal_n(16), comp=1)
    add("HL01.LBM", gl[:11], P.PXD_HEAD)
    add("HL02.LBM", gl[:8] + b"ACBM" + gl[12:], P.PXD_HEAD)
    add("HL03.LBM", gl[:12 + 8 + 20 + 4], P.PXD_TRUNC)    # a chunk header cut
    add("HL04.LBM", iff(W, H, 4, i16, bmhd=b"\0" * 18), P.PXD_HEAD)
    add("HL05.LBM", gl[:12 + 8 + 10], P.PXD_TRUNC)        # BMHD cut
    bodyfirst = b"FORM\0\0\0\x40ILBM" + iff_chunk(b"BODY", b"\0" * 8) + \
        gl[12:]
    add("HL06.LBM", bodyfirst, P.PXD_HEAD)
    add("HL07.LBM", iff(0, H, 4, [[]] * H), P.PXD_DIMS)
    add("HL08.LBM", iff(W, 9000, 1, [[0] * W], body=b""), P.PXD_DIMS)
    add("HL09.LBM", iff(W, H, 4, i16, comp=2, body=b"\0" * 8), P.PXD_PACK)
    add("HL10.LBM", iff(W, H, 0, i16, body=b"\0" * 8), P.PXD_DEPTH)
    add("HL11.LBM", iff(W, H, 24, [[0] * W] * H, body=b"\0" * 8),
        P.PXD_DEPTH)                                      # deep ILBM
    add("HL12.LBM", iff(W, H, 6, i64, cmap=pal_n(16), camg=0x800),
        P.PXD_DEPTH)                                      # HAM
    add("HL13.LBM", iff(W, H, 4, i16, kind=b"PBM ", body=b"\0" * 8),
        P.PXD_DEPTH)                                      # PBM of 4 planes
    add("HL14.LBM", iff(W, H, 4, i16, cmap=pal_n(16))[:-9], P.PXD_TRUNC)
    add("HL15.LBM", gl[:-5], P.PXD_TRUNC)                 # ByteRun1 cut
    lie = bytearray(iff(W, H, 4, i16, cmap=pal_n(16)))
    bo = lie.find(b"BODY")
    struct.pack_into(">I", lie, bo + 4, 20)               # BODY's size lies
    add("HL16.LBM", bytes(lie), P.PXD_TRUNC)
    add("HL17.LBM", iff(W, H, 4, i16, cmap=pal_n(16))[:12 + 28 + 8 + 20],
        P.PXD_TRUNC)                                      # CMAP cut
    add("HL18.LBM", iff(W, H, 4, i16, cmap=pal_n(16),
                        pre=fill + (iff_chunk(b"ANNO", b"\0" * 2040),)),
        P.PXD_HEAD)                                       # 17 heads
    add("HL19.LBM", iff(W, H, 4, i16, cmap=pal_n(16),
                        pre=(b"ANNO\xFF\xFF\xFF\xF0",)), P.PXD_TRUNC)  # a leap
    # --- MacPaint ---------------------------------------------------------------
    add("MPAINT.MAC", macpaint())
    add("MBINX.DAT", macpaint(macbin=True, cross=True))    # by its bytes
    mp = macpaint()
    add("HM01.MAC", mp[:511], P.PXD_TRUNC)                # no header
    add("HM02.MAC", macpaint(macbin=True)[:128 + 511], P.PXD_TRUNC)
    add("HM03.MAC", mp[:3000], P.PXD_TRUNC)               # the body cut
    add("HM04.MAC", mp[:512], P.PXD_TRUNC)                # no body at all


# =============================================================================
# ANIMATED GIF (SPEC.md 106.25): frames after the first, every disposal,
# tables mapped and not, interlace, clipping, the loop, and what ends a pass
# =============================================================================
def gframe(pix=None, w=None, h=None, left=0, top=0, lpal=None, ilace=False,
           trans=None, disp=0, delay=0, gce=True, mincode=None, codes=None,
           cut=None):
    """One frame: its GCE (when `gce`), descriptor, local table and data.
    `pix` rows in picture order; `cut` keeps that many bytes of the frame."""
    h = len(pix) if h is None else h
    w = len(pix[0]) if w is None else w
    b = bytearray()
    if gce:
        b += b"\x21\xF9\x04" + bytes(((disp << 2) | (trans is not None),)) + \
            struct.pack("<H", delay) + bytes((trans or 0,)) + b"\0"
    pk = 0x40 if ilace else 0
    if lpal:
        lb = 1
        while (2 << (lb - 1)) < len(lpal):
            lb += 1
        pk |= 0x80 | (lb - 1)
    b += b"\x2C" + struct.pack("<HHHH", left, top, w, h) + bytes((pk,))
    if lpal:
        b += b"".join(bytes(c) for c in lpal) + \
            b"\0\0\0" * ((2 << (lb - 1)) - len(lpal))
    mc = mincode if mincode is not None else 4
    if codes is None:
        codes = lzw_encode([v for y in gif_frame_order(h, ilace)
                            for v in pix[y]], mc) if w and h else b""
    b += bytes((mc,)) + subblocks(codes)
    return bytes(b[:cut] if cut is not None else b)


def gif_frame_order(h, ilace):
    if not ilace:
        return list(range(h))
    out = []
    for k in range(4):
        out += list(range((0, 4, 2, 1)[k], h, (8, 8, 4, 2)[k]))
    return out


def agif(sw, sh, frames, gpal=None, bgi=0, loop=None, pre=(), tail=b"\x3B"):
    b = bytearray(b"GIF89a" + struct.pack("<HH", sw, sh))
    if gpal:
        gb = 1
        while (2 << (gb - 1)) < len(gpal):
            gb += 1
        b += bytes((0x80 | ((gb - 1) << 4) | (gb - 1), bgi, 0))
        b += b"".join(bytes(c) for c in gpal) + \
            b"\0\0\0" * ((2 << (gb - 1)) - len(gpal))
    else:
        b += bytes((0, bgi, 0))
    if loop is not None:
        b += b"\x21\xFF\x0BNETSCAPE2.0\x03\x01" + struct.pack("<H", loop) + \
            b"\0"
    for e in pre:
        b += e
    for f in frames:
        b += f
    return bytes(b + tail)


def ablock(w, h, k):
    """A frame's pixels: sixteen indices, moving with k."""
    return [[(x + 2 * y + 3 * k) % 16 for x in range(w)] for y in range(h)]


# what --anim expects of each: (frames in the first pass, loop, the last
# frame whole) - gif_anim's own invariants are checked for every one
ANIM = {"GA1.GIF": (4, None, True), "GA2.GIF": (4, None, True),
        "GA3.GIF": (4, None, True), "GA4.GIF": (3, None, True),
        "GA5.GIF": (3, None, True), "GA6.GIF": (2, None, True),
        "GA7.GIF": (4, None, True), "GA8.GIF": (4, 3, True),
        "GA9.GIF": (3, 0, False), "GA10.GIF": (2, None, True),
        "GA11.GIF": (3, None, False), "GA12.GIF": (2, None, True),
        "GA13.GIF": (2, None, True), "GAJUNK.GIF": (1, None, True),
        "GX.GIF": (2, 0, True)}


def acorpus(add):
    """The animated GIFs: every one OPENS (106.18) as its first image."""
    p16 = pal_n(16)
    sw, sh = 24, 16
    bg = ablock(sw, sh, 0)
    f0 = gframe(bg, delay=10)
    add("GA1.GIF", agif(sw, sh, [f0] + [
        gframe(ablock(6, 5, k), left=2 + 6 * k, top=2 + 3 * k, disp=1,
               delay=20) for k in range(3)], gpal=p16, bgi=4))
    hole = [[3 if (x + y) % 3 == 0 else (x * y) % 16 for x in range(7)]
            for y in range(6)]
    add("GA2.GIF", agif(sw, sh, [gframe(bg, trans=5, disp=2)] + [
        gframe(hole, left=3 * k, top=2 * k + 1, trans=3, disp=2)
        for k in range(1, 4)], gpal=p16, bgi=9))         # B = 5, frame 0's key
    add("GA3.GIF", agif(sw, sh, [gframe(bg, disp=3)] + [
        gframe(ablock(8, 6, k), left=4 * k, top=k + 2, disp=3 if k != 2 else 1,
               trans=7 if k == 3 else None) for k in range(1, 4)],
        gpal=p16, bgi=2))                                 # 0's disposal 3: B
    near = [(c[0] ^ 3, c[1], min(255, c[2] + 9)) for c in p16[::-1]]
    g4 = p16[:12] + [(200, 10, 10), (200, 10, 10), (10, 20, 30), (10, 20, 32)]
    tie = [(10, 20, 31), (200, 10, 10), (201, 10, 10), (255, 255, 255),
           (0, 0, 0)]                     # 14 and 15 tie: 14; 12 and 13: 12
    add("GA4.GIF", agif(sw, sh, [f0, gframe(ablock(10, 6, 1), left=1, top=1,
                                            lpal=near, disp=1),
                                 gframe([[(x + 2 * y) % 8 for x in range(5)]
                                         for y in range(4)], left=9, top=8,
                                        lpal=tie)],
                        gpal=g4))       # local tables: mapped; indices 5..7
                                        # past the second's table are black
    lp0 = pal_n(13)
    add("GA5.GIF", agif(sw, sh, [gframe([[v % 13 for v in r] for r in bg],
                                        lpal=lp0, trans=12),
                                 gframe(ablock(9, 7, 1), left=6, top=3),
                                 gframe(ablock(4, 4, 2), left=0, top=0,
                                        lpal=p16[:8], disp=2)],
                        gpal=p16))                        # 0 local, then global
    add("GA6.GIF", agif(sw, sh, [f0, gframe(ablock(11, 13, 1), left=5, top=2,
                                            ilace=True, trans=0)],
                        gpal=p16))                        # interlaced later
    add("GA7.GIF", agif(sw, sh, [f0, gframe(ablock(10, 9, 1), left=18, top=11,
                                            disp=2),
                                 gframe(ablock(5, 5, 2), left=30, top=3,
                                        disp=2),         # wholly off: no rect
                                 gframe(ablock(6, 20, 3), left=0, top=4)],
                        gpal=p16))                        # partly off-screen
    add("GA8.GIF", agif(16, 12, [gframe(ablock(16, 12, 0), delay=0)] + [
        gframe(ablock(5, 5, k), left=k * 3, top=k * 2, delay=d)
        for k, d in ((1, 1), (2, 5), (3, 100))], gpal=p16, loop=3))
    good = gframe(ablock(12, 10, 2), left=4, top=3)
    add("GA9.GIF", agif(sw, sh, [f0, gframe(ablock(6, 5, 1), left=1, top=1),
                                 good[:len(good) - 14]], gpal=p16, loop=0,
                        tail=b""))                        # cut: the pass ends
    add("GA10.GIF", agif(sw, sh, [f0, gframe(None, w=0, h=5, left=3, top=3,
                                             disp=2, codes=b"\1\2\3"),
                                  gframe(ablock(6, 6, 1), left=8, top=4)],
                         gpal=p16))                      # a 0-wide frame skipped
    bad = lzw_encode([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12], 4)
    add("GA11.GIF", agif(sw, sh, [f0, gframe(ablock(4, 3, 1), left=2, top=2),
                                  gframe(ablock(4, 3, 2), left=9, top=9,
                                         codes=bad[:4] + b"\xFF\xFF\xFF")],
                         gpal=p16))                       # a code past the table
    add("GA12.GIF", agif(sw, sh, [gframe(bg, lpal=p16),
                                  gframe(ablock(4, 4, 1), left=2, top=2,
                                         lpal=p16[:4]),
                                  gframe(ablock(4, 4, 2), left=9, top=5)]))
    # ...no global table: frame 2 has no table and ENDS the pass unread
    add("GA13.GIF", agif(sw, sh, [f0, gframe(ablock(5, 5, 1), left=1, top=1),
                                  gframe(ablock(5, 5, 2), mincode=9,
                                         codes=b"\0")], gpal=p16))
    # ...a minimum code size of 9 ends it too
    add("GAJUNK.GIF", agif(sw, sh, [f0], gpal=p16,
                           tail=b"\x21\xF9\x04\0\0\0\0\0\x99junk after it"))


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
    add("H26.JPG", b"\xFF\xD8\xFF\xE0" + b"\x00" * 60, P.PXD_HEAD)
    add("H27.PGM", b"P5\n" + b"#" * 3000 + b"\n3 2\n255\n" + b"\x00" * 6,
        P.PXD_HEAD)                                         # header past the head
    add("H28.BMP", bmp(W, H, 8, idx_rows(W, H, 4), pal=pal_n(4), used=4,
                       planes=3), P.PXD_HEAD)
    # a JPEG whose APP1 length walks the marker scan to the edge of 64K: the
    # sniff must stop, not wrap (wave-1 review MAJ-1); since wave 4 the
    # header walk follows it past the file's end, which is `cut short`
    add("H29.JPG", b"\xFF\xD8\xFF\xE1\xFF\xF2" + b"\xFF" * 2042,
        P.PXD_TRUNC)
    add("H30.DAT", b"Not a picture at all, just words." * 9, P.PXD_NOTPIC)
    # ...and a picture under a name PiXEL does not list: its bytes win
    add("B24X.DAT", bmp(W, H, 24, rgb))
    wcorpus(add, W, H)               # GIF and PNG (SPEC.md 106.18)
    jcorpus(add)                     # JPEG (SPEC.md 106.19)
    xcorpus(add, W, H)               # TIFF, ICO, IFF, MacPaint (SPEC.md 106.25)
    acorpus(add)                     # animated GIFs (SPEC.md 106.25)
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
    # wave 3: one truecolour picture through every shape of PNG stream, and
    # one paletted picture through BMP, GIF and PNG
    ("B24.BMP", "P2_8.PNG", "P2_16.PNG", "P2_8I.PNG", "PMI.PNG", "PST.PNG",
     "PFX.PNG", "PDY.PNG"),
    ("B8.BMP", "G8.GIF", "P3_8.PNG"),
    ("G4.GIF", "GI.GIF", "GL.GIF", "GCLR.GIF", "P3_4.PNG"),
    ("GFULL.GIF", "GDEF.GIF"),
    # wave 8 (SPEC.md 106.25): the same pictures through TIFF, ICO, IFF -
    # every byte order, packing, predictor and layout; an opaque alpha, an
    # ExtraSamples of 0 and none; the ICO directory's choice; PNG-in-ICO
    ("B24.BMP", "FRGB.TIF", "FRGBM.TIF", "FRGBP.TIF", "FRGBL.TIF",
     "FRGBLP.TIF", "FRGBA0.TIF", "FRGBAN.TIF", "FRGBA1.TIF", "FRGBS.TIF",
     "FTPNG.TIF", "I24.ICO", "I32.ICO", "IPICK.ICO", "ITIE.ICO", "IPNG.ICO"),
    ("B8.BMP", "FP8.TIF", "FP8M.TIF", "FP8P.TIF", "I8.ICO", "IPNGX.DAT",
     "L8.LBM", "L8R.LBM", "LPBM.LBM", "LPBMR.LBM", "L8X.DAT"),
    ("N5.PGM", "FG8.TIF", "FW8.TIF", "FG8L.TIF", "FG8LP.TIF", "FG8PL2.TIF",
     "FCNTBIG.TIF"),
    ("C4.PCX", "FP4.TIF", "FP4M.TIF", "I4.ICO", "I256.ICO", "I4X.DAT",
     "L4.LBM", "L4M.IFF", "LHEAD16.LBM"),
    ("N1.PBM", "FP1.TIF", "FP1M.TIF", "I1.ICO", "L1.LBM", "L1G.LBM"),
    ("P3_2.PNG", "FP2.TIF", "FP2M.TIF"),
    ("P0_1.PNG", "FG1.TIF", "FW1.TIF"),
    ("P0_2.PNG", "FG2.TIF", "FW2.TIF"),
    ("P0_4.PNG", "FG4.TIF", "FW4.TIF"),
    ("FBIG.TIF", "PBIG.PNG"),
    ("MPAINT.MAC", "MBINX.DAT"),
]


def verdict(name, data):
    try:
        p = P.decode(data, name.rsplit(".", 1)[1])
    except P.Refused as e:
        return e.code, None
    return 0, p


def anim_check(cs):
    """SPEC.md 106.25: each animated GIF's first pass - its frame count, loop
    count and how it ends; frame 0 is the first decode's master and a pass
    that begins again draws it exactly; every dirty rect is on the screen."""
    bad = 0
    data = dict((n, d) for n, d, _ in cs)
    for name, (nf, loop, whole) in sorted(ANIM.items()):
        d = data[name]
        p = P.decode(d, "GIF")
        lp, fr = P.gif_anim(d)
        why = []
        if (len(fr), lp, fr[-1]["whole"]) != (nf, loop, whole):
            why.append("frames/loop/whole %r" % ((len(fr), lp, fr[-1]["whole"]),))
        if fr[0]["master"] != bytes(P.emit(p, 0)[0]):
            why.append("frame 0 is not the first decode's master")
        if P.gif_restart(d) != fr[0]["master"]:
            why.append("a restarted pass is not frame 0")
        if P.gif_animated(d) != (name != "GAJUNK.GIF"):
            why.append("animated flag")
        for f in fr:
            r = f["rect"]
            if len(f["master"]) != p.w * p.h or r is not None and not (
                    0 <= r[0] <= r[2] < p.w and 0 <= r[1] <= r[3] < p.h):
                why.append("a frame's master or rect")
        if name == "GA8.GIF" and [f["delay"] for f in fr] != [2, 2, 1, 18]:
            why.append("delays %r" % [f["delay"] for f in fr])
        if why:
            print("pixcorpus: FAIL %s: %s" % (name, "; ".join(why)))
            bad += 1
    if bad:
        return 1
    print("pixcorpus: %d animated GIFs, every first pass as expected"
          % len(ANIM))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--jpeg", action="store_true",
                    help="the JPEG fixtures' verdicts too (a pure-Python "
                    "JPEG decode is soak's, `pixjpegref`)")
    ap.add_argument("--anim", action="store_true",
                    help="play every animated GIF's first pass through "
                    "pixelsim.gif_anim and check what ANIM expects")
    a = ap.parse_args()
    cs = corpus()
    if a.anim:
        return anim_check(cs)
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
        agree = set(n for g in AGREE for n in g)
        got = {}
        for name, data, want in cs:
            if name.endswith(".JPG") and not a.jpeg:
                continue            # SPEC.md 106.19: soak's `pixjpegref`
            v, p = verdict(name, data)
            if v != want:
                print("pixcorpus: FAIL %s: pixelsim says %d (%s), the corpus %d (%s)"
                      % (name, v, P.PXD_WORDS[v], want, P.PXD_WORDS[want]))
                bad += 1
            elif p is not None:
                m = P.emit(p, getattr(p, "presc", None) or 0)[0]
                if name in agree:
                    got[name] = bytes(m)
        # two readings of one picture are one master: every truecolour
        # fixture of the same content, through five different decoders and
        # both emission orders, must quantise to the same bytes
        for grp in AGREE:
            if len(set(got.get(n) for n in grp)) != 1:
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

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
    wcorpus(add, W, H)               # GIF and PNG (SPEC.md 106.18)
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

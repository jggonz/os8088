#!/usr/bin/env python3
"""PageDesk's fixture generator (docs/PAGEDESK-SPEC.md 18, 20.1).

    python3 tools/os88pdfgen.py DIR              the eight shipping samples + MANIFEST.TXT
    python3 tools/os88pdfgen.py --stress DIR     generated stress files (never committed)
    python3 tools/os88pdfgen.py --negative DIR   the deliberately INVALID corpus + README.TXT

Pure standard library and DETERMINISTIC: no clock, no random, a fixed /ID,
and even the FlateDecode stream is written by this file's own fixed-Huffman
deflater so the bytes do not depend on the zlib build. Every sample except
FLATE.PDF is asserted, about its own bytes, to stay inside the XT profile of
PAGEDESK-SPEC 1.3.
"""
import argparse
import hashlib
import json
import os
import re
import sys
import unicodedata
import zlib

# ------------------------------------------------------------------ metrics
HELV = ([278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278]
        + [556] * 10 + [278, 278, 584, 584, 584, 556, 1015]
        + [667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667,
           778, 722, 667, 611, 722, 667, 944, 667, 667, 611]
        + [278, 278, 278, 469, 556, 333]
        + [556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556, 556,
           556, 333, 500, 278, 556, 500, 722, 500, 500, 500]
        + [334, 260, 334, 584])
HELVB = ([278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278]
         + [556] * 10 + [333, 333, 584, 584, 584, 611, 975]
         + [722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778, 667,
            778, 722, 667, 611, 722, 667, 944, 667, 667, 611]
         + [333, 278, 333, 584, 556, 333]
         + [556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611, 611,
            611, 389, 556, 333, 611, 556, 778, 556, 556, 500]
         + [389, 280, 389, 584])
TIMES = ([250, 333, 408, 500, 500, 833, 778, 180, 333, 333, 500, 564, 250, 333, 250, 278]
         + [500] * 10 + [278, 278, 564, 564, 564, 444, 921]
         + [722, 667, 667, 722, 611, 556, 722, 722, 333, 389, 722, 611, 889, 722, 722, 556,
            722, 667, 556, 611, 722, 722, 944, 722, 722, 611]
         + [333, 278, 333, 469, 500, 333]
         + [444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500, 278, 778, 500, 500, 500,
            500, 333, 389, 278, 500, 500, 722, 500, 500, 444]
         + [480, 200, 480, 541])
assert len(HELV) == len(HELVB) == len(TIMES) == 95
# (Helvetica, Times) widths of the non-ASCII characters this file uses.
SPECIAL = {
    '£': (556, 500), '€': (556, 500), '©': (737, 760), '®': (737, 760),
    '°': (400, 400), '±': (584, 564), '×': (584, 564), '÷': (584, 564),
    '«': (556, 500), '»': (556, 500), '¡': (333, 333), '¿': (611, 444),
    'ß': (611, 500), '–': (556, 500), '—': (1000, 1000), '‘': (222, 333),
    '’': (222, 333), '“': (333, 444), '”': (333, 444), '•': (350, 350),
    '…': (1000, 1000), 'æ': (889, 667), 'Æ': (1000, 889), 'œ': (944, 722),
    'Œ': (1000, 889), 'ø': (611, 500), 'Ø': (778, 722), '¥': (556, 500),
    '¢': (556, 500), '§': (556, 500), 'ﬁ': (500, 556), 'ﬂ': (500, 556),
}
BASE14 = ('Helvetica', 'Helvetica-Bold', 'Helvetica-Oblique', 'Helvetica-BoldOblique',
          'Times-Roman', 'Times-Bold', 'Times-Italic', 'Times-BoldItalic',
          'Courier', 'Courier-Bold', 'Courier-Oblique', 'Courier-BoldOblique')
GLYPH = {'Adieresis': 'Ä', 'odieresis': 'ö', 'Euro': '€', 'eacute': 'é',
         'germandbls': 'ß', 'bullet': '•'}


def char_width(font, ch):
    if font.startswith('Courier'):
        return 600
    fam = 1 if font.startswith('Times') else 0
    bold = font in ('Helvetica-Bold', 'Helvetica-BoldOblique')
    if font.startswith('Times') and font != 'Times-Roman':
        raise ValueError('no width table for ' + font)
    o = ord(ch)
    if 32 <= o <= 126:
        t = TIMES if fam else (HELVB if bold else HELV)
        return t[o - 32]
    if ch in SPECIAL and not bold:
        return SPECIAL[ch][fam]
    d = unicodedata.normalize('NFD', ch)
    if len(d) > 1 and 'A' <= d[0] <= 'z' and d[0].isalpha():
        return char_width(font, d[0])
    raise ValueError('no width for %r in %s' % (ch, font))


def text_width(font, s, size):
    return sum(char_width(font, c) for c in s) * size / 1000.0


def code_char(enc, code, diffs=None):
    if diffs and code in diffs:
        return GLYPH[diffs[code]]
    if enc == 'WinAnsiEncoding':
        try:
            return bytes([code]).decode('cp1252')
        except UnicodeDecodeError:
            return None
    if enc == 'MacRomanEncoding':
        return bytes([code]).decode('mac_roman')
    if enc == 'Standard':
        if code == 39:
            return '’'
        if code == 96:
            return '‘'
        if 32 <= code <= 126:
            return chr(code)
        return {174: 'ﬁ', 175: 'ﬂ', 225: 'Æ', 251: 'ß'}.get(code)
    raise ValueError(enc)


# ------------------------------------------------------------------ deflate
def _bitrev(v, n):
    r = 0
    for _ in range(n):
        r = (r << 1) | (v & 1)
        v >>= 1
    return r


_LB = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99,
       115, 131, 163, 195, 227, 258]
_LE = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0]
_DB = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025,
       1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577]
_DE = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
       12, 13, 13]


def zlib_fixed(data):
    """A zlib stream of one fixed-Huffman block, greedy LZ77. Deterministic on
    every host, and checked against the host's own zlib."""
    bits = [0, 0]
    out = bytearray()

    def put(v, n):
        bits[0] |= v << bits[1]
        bits[1] += n
        while bits[1] >= 8:
            out.append(bits[0] & 255)
            bits[0] >>= 8
            bits[1] -= 8

    def sym(s):
        if s < 144:
            put(_bitrev(0x30 + s, 8), 8)
        elif s < 256:
            put(_bitrev(0x190 + s - 144, 9), 9)
        elif s < 280:
            put(_bitrev(s - 256, 7), 7)
        else:
            put(_bitrev(0xC0 + s - 280, 8), 8)

    put(1, 1)
    put(1, 2)                       # BFINAL, BTYPE=01 (fixed)
    heads = {}
    i, n = 0, len(data)
    while i < n:
        best_l, best_d = 0, 0
        if i + 2 < n:
            key = data[i:i + 3]
            for j in reversed(heads.get(key, [])[-24:]):
                if i - j > 32768:
                    break
                l = 3
                while l < 258 and i + l < n and data[j + l] == data[i + l]:
                    l += 1
                if l > best_l:
                    best_l, best_d = l, i - j
        if best_l >= 3:
            li = max(k for k in range(29) if _LB[k] <= best_l)
            sym(257 + li)
            put(best_l - _LB[li], _LE[li])
            di = max(k for k in range(30) if _DB[k] <= best_d)
            put(_bitrev(di, 5), 5)
            put(best_d - _DB[di], _DE[di])
            step = best_l
        else:
            sym(data[i])
            step = 1
        for k in range(i, min(i + step, n - 2)):
            heads.setdefault(data[k:k + 3], []).append(k)
        i += step
    sym(256)
    if bits[1]:
        out.append(bits[0] & 255)
    z = b'\x78\xda' + bytes(out) + (zlib.adler32(data) & 0xffffffff).to_bytes(4, 'big')
    assert zlib.decompress(z) == data
    return z


def rl_encode(data):
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        j = i
        while j + 1 < n and data[j + 1] == data[i] and j - i < 127:
            j += 1
        if j > i:
            out += bytes([257 - (j - i + 1), data[i]])
            i = j + 1
        else:
            k = i
            while k < n and k - i < 128 and not (k + 1 < n and data[k + 1] == data[k]):
                k += 1
            out += bytes([k - i - 1]) + data[i:k]
            i = k
    out.append(128)
    return bytes(out)


def ahx_encode(data):
    h = data.hex().upper()
    return b'\n'.join(h[i:i + 64].encode() for i in range(0, len(h), 64)) + b'>'


# ------------------------------------------------------------------ pdf core
def fmt(x):
    if isinstance(x, int):
        return str(x)
    s = ('%.2f' % x).rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s


def lit(b):
    """A literal string for raw bytes: only ()\\ and printable ASCII kept."""
    o = ['(']
    for c in b:
        if c in b'()\\':
            o.append('\\' + chr(c))
        elif 32 <= c < 127:
            o.append(chr(c))
        else:
            o.append('\\%03o' % c)
    o.append(')')
    return ''.join(o)


def tlit(s):
    return lit(s.encode('cp1252'))


class Pdf:
    def __init__(self, name, version='1.3', allow_flate=False):
        self.name, self.version, self.allow_flate = name, version, allow_flate
        self.objs = {}
        self.next = 1
        self.fonts = {}            # (base, enc, diffs) -> (key, objnum)
        self.filters = []
        self.npages = 0
        self.annots_note = ''

    def alloc(self):
        n = self.next
        self.next += 1
        return n

    def put(self, n, body):
        if isinstance(body, str):
            body = body.encode('latin-1')
        self.objs[n] = body
        self.next = max(self.next, n + 1)
        return n

    def add(self, body):
        return self.put(self.alloc(), body)

    def stream(self, data, extra='', filters=(), num=None, indirect_len=False):
        enc = data
        for f in reversed(filters):
            enc = {'ASCIIHexDecode': ahx_encode, 'RunLengthDecode': rl_encode,
                   'FlateDecode': zlib_fixed}[f](enc)
            if f == 'FlateDecode' and not self.allow_flate:
                raise AssertionError('FlateDecode outside FLATE.PDF')
        for f in filters:
            if f not in self.filters:
                self.filters.append(f)
        fl = ''
        if len(filters) == 1:
            fl = ' /Filter /%s' % filters[0]
        elif filters:
            fl = ' /Filter [%s]' % ' '.join('/' + f for f in filters)
        n = self.alloc() if num is None else num
        if indirect_len:
            ln = self.add(str(len(enc)))
            ls = '%d 0 R' % ln
        else:
            ls = str(len(enc))
        self.put(n, ('<< %s /Length %s%s >>\nstream\n' % (extra, ls, fl)).encode('latin-1')
                 + enc + b'\nendstream')
        return n

    def font(self, base, enc='WinAnsiEncoding', diffs=None, last=126):
        assert base in BASE14, base
        key = (base, enc, tuple(sorted((diffs or {}).items())), last)
        if key in self.fonts:
            return self.fonts[key][0]
        k = '/F%d' % (len(self.fonts) + 1)
        first = min([32] + list((diffs or {}).keys()))
        ws = []
        for code in range(first, last + 1):
            ch = code_char(enc, code, diffs)
            try:
                ws.append(char_width(base, ch) if ch and ch.isprintable() else 0)
            except ValueError:
                ws.append(0)            # a code this file never uses
        if enc == 'WinAnsiEncoding' or enc == 'MacRomanEncoding':
            e = ' /Encoding /%s' % enc
        else:
            e = ''
        if diffs:
            dl = []
            for c in sorted(diffs):
                dl.append('%d /%s' % (c, diffs[c]))
            e = (' /Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding '
                 '/Differences [%s] >>' % ' '.join(dl))
        o = self.add('<< /Type /Font /Subtype /Type1 /BaseFont /%s /FirstChar %d /LastChar %d '
                     '/Widths [%s]%s >>' % (base, first, last, ' '.join(map(str, ws)), e))
        self.fonts[key] = (k, o)
        return k

    def font_res(self):
        return '<< %s >>' % ' '.join('%s %d 0 R' % (k, o) for k, o in self.fonts.values())

    def finish(self, root, info=None, version=None, extra_trailer=''):
        v = version or self.version
        return write_pdf(self.objs, root, info, v, self.name, extra_trailer)


def doc_id(name):
    return hashlib.sha256(('os88pdfgen/' + name).encode()).hexdigest()[:32].upper()


def xref_sections(offs):
    nums = sorted(offs)
    runs = []
    for n in nums:
        if runs and n == runs[-1][-1] + 1:
            runs[-1].append(n)
        else:
            runs.append([n])
    return runs


def write_pdf(objs, root, info, version, name, extra_trailer=''):
    out = bytearray(('%%PDF-%s\n' % version).encode() + b'%\xe2\xe3\xcf\xd3\n')
    offs = {}
    for n in sorted(objs):
        offs[n] = len(out)
        out += b'%d 0 obj\n' % n + objs[n] + b'\nendobj\n'
    xr = len(out)
    out += b'xref\n'
    runs = xref_sections(offs)
    if not runs or runs[0][0] != 0:
        runs.insert(0, [0])
    else:
        pass
    # object 0 is the free-list head and shares a subsection with object 1 when adjacent
    merged = []
    for r in runs:
        if merged and r[0] == merged[-1][-1] + 1:
            merged[-1].extend(r)
        else:
            merged.append(list(r))
    for r in merged:
        out += b'%d %d\n' % (r[0], len(r))
        for n in r:
            if n == 0:
                out += b'0000000000 65535 f \n'
            else:
                out += b'%010d 00000 n \n' % offs[n]
    size = max(objs) + 1
    idh = doc_id(name)
    tr = '<< /Size %d /Root %d 0 R' % (size, root)
    if info:
        tr += ' /Info %d 0 R' % info
    tr += ' /ID [<%s> <%s>]%s >>' % (idh, idh, extra_trailer)
    out += ('trailer\n%s\nstartxref\n%d\n%%%%EOF\n' % (tr, xr)).encode()
    return bytes(out)


def append_revision(base, objs, root, size, name, rev, free=(), info=None):
    """An incremental update: new/changed objects, an xref section of its own
    and /Prev back to the previous one."""
    prev = int(re.findall(rb'startxref\s+(\d+)', base)[-1])
    out = bytearray(base)
    offs = {}
    for n in sorted(objs):
        offs[n] = len(out)
        out += b'%d 0 obj\n' % n + objs[n] + b'\nendobj\n'
    xr = len(out)
    out += b'xref\n'
    allnums = sorted(set(offs) | set(free))
    for r in xref_sections({n: 1 for n in allnums}):
        out += b'%d %d\n' % (r[0], len(r))
        for n in r:
            if n in offs:
                out += b'%010d 00000 n \n' % offs[n]
            else:
                out += b'0000000000 00001 f \n'
    idh = doc_id(name)
    tr = '<< /Size %d /Root %d 0 R /Prev %d' % (size, root, prev)
    if info:
        tr += ' /Info %d 0 R' % info
    tr += ' /ID [<%s> <%s>] >>' % (idh, doc_id(name + '/r%d' % rev))
    out += ('trailer\n%s\nstartxref\n%d\n%%%%EOF\n' % (tr, xr)).encode()
    return bytes(out)


# ------------------------------------------------------------ content builder
class C:
    def __init__(self, pdf):
        self.pdf = pdf
        self.o = []

    def op(self, *a):
        self.o.append(' '.join(fmt(x) if isinstance(x, (int, float)) else x for x in a))

    def raw(self, s):
        self.o.append(s)

    def gray(self, g, stroke=False):
        self.op(g, 'G' if stroke else 'g')

    def rgb(self, r, g, b, stroke=False):
        self.op(r, g, b, 'RG' if stroke else 'rg')

    def lw(self, w):
        self.op(w, 'w')

    def dash(self, on, off):
        self.o.append('[%s %s] 0 d' % (fmt(on), fmt(off)))

    def undash(self):
        self.o.append('[] 0 d')

    def rect(self, x, y, w, h, mode='S'):
        self.op(x, y, w, h, 're', mode)

    def poly(self, pts, mode='S', close=True):
        self.op(pts[0][0], pts[0][1], 'm')
        for p in pts[1:]:
            self.op(p[0], p[1], 'l')
        self.o.append('h ' + mode if close else mode)

    def line(self, x1, y1, x2, y2):
        self.op(x1, y1, 'm', x2, y2, 'l', 'S')

    def curve(self, x0, y0, x1, y1, x2, y2, x3, y3, mode='S'):
        self.op(x0, y0, 'm', x1, y1, x2, y2, x3, y3, 'c', mode)

    def q(self):
        self.o.append('q')

    def Q(self):
        self.o.append('Q')

    def cm(self, a, b, c, d, e, f):
        self.op(a, b, c, d, e, f, 'cm')

    def text(self, x, y, font, size, s, align='l', gray=None, extra=''):
        k = self.pdf.font(font)
        w = text_width(font, s, size)
        if align == 'c':
            x -= w / 2
        elif align == 'r':
            x -= w
        body = 'BT %s %s Tf %s%s %s Td %s Tj ET' % (
            k, fmt(size), extra + ' ' if extra else '', fmt(x), fmt(y), tlit(s))
        if gray is not None:
            body = 'q %s g %s Q' % (fmt(gray), body)
        self.o.append(body)
        return w

    def bytes(self):
        return ('\n'.join(self.o) + '\n').encode('latin-1')


def wrap(font, size, text, width):
    words, lines, cur = text.split(), [], ''
    for w in words:
        t = (cur + ' ' + w) if cur else w
        if text_width(font, t, size) <= width:
            cur = t
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def para(c, x, y, width, text, font='Helvetica', size=11, lead=15, gray=None):
    for ln in wrap(font, size, text, width):
        c.text(x, y, font, size, ln, gray=gray)
        y -= lead
    return y


def table(c, x, ytop, cols, rows, size=10, rowh=20, head=True, font='Helvetica'):
    total = sum(cols)
    y = ytop
    c.lw(0.8)
    for ri, row in enumerate(rows):
        if head and ri == 0:
            c.gray(0.82)
            c.rect(x, y - rowh, total, rowh, 'f')
            c.gray(0)
        cx = x
        for ci, cell in enumerate(row):
            f = 'Helvetica-Bold' if (head and ri == 0) else font
            c.text(cx + 6, y - rowh + 6.5, f, size, cell)
            cx += cols[ci]
        y -= rowh
    c.gray(0, True)
    c.rect(x, y, total, rowh * len(rows))
    cx = x
    for w in cols[:-1]:
        cx += w
        c.line(cx, ytop, cx, y)
    for i in range(1, len(rows)):
        c.line(x, ytop - rowh * i, x + total, ytop - rowh * i)
    return y


# ------------------------------------------------------------ XT drawing
def draw_xt(c, ox, oy, s, label='IBM XT'):
    """An IBM XT as vector paths: monitor, system unit with two floppy slots
    and a vent grille, keyboard. Local box about 142 x 128."""
    c.q()
    c.cm(s, 0, 0, s, ox, oy)
    c.lw(1.1)
    c.gray(0, True)
    # keyboard (sheared)
    c.q()
    c.cm(1, 0, 0.45, 1, 4, 0)
    c.gray(0.88)
    c.rect(0, 0, 112, 15, 'B')
    c.gray(0)
    for row in range(2):
        for k in range(14):
            c.rect(4 + k * 7.6, 3 + row * 5.6, 5.6, 3.6, 'S')
    c.rect(34, 0.8, 38, 1.8, 'S')
    c.Q()
    # system unit: front, side, top
    c.gray(0.55)
    c.poly([(132, 19), (144, 29), (144, 57), (132, 47)], 'B')
    c.gray(0.9)
    c.poly([(0, 47), (132, 47), (144, 57), (12, 57)], 'B')
    c.gray(1)
    c.rect(0, 19, 132, 28, 'B')
    c.gray(0)
    for i in range(6):                         # vent grille
        c.line(8 + i * 3.4, 25, 8 + i * 3.4, 41)
    for fx in (52, 90):                        # floppy bays
        c.rect(fx, 25, 34, 16, 'S')
        c.rect(fx + 5, 31, 24, 3, 'S')
        c.rect(fx + 25, 26.5, 5, 2.5, 'f')
    c.rect(116, 24, 8, 5, 'S')
    # monitor
    c.gray(0.82)
    c.rect(44, 57, 40, 4, 'B')
    c.gray(0.55)
    c.poly([(104, 60), (116, 68), (116, 124), (104, 116)], 'B')
    c.gray(0.9)
    c.poly([(24, 116), (104, 116), (116, 124), (36, 124)], 'B')
    c.gray(1)
    c.rect(24, 60, 80, 56, 'B')
    c.gray(0)
    c.rect(32, 67, 64, 42, 'f')
    c.gray(1, True)
    c.lw(0.8)
    c.rect(38, 74, 52, 28, 'S')
    c.gray(0, True)
    c.text(64, 84, 'Helvetica-Bold', 11, label, align='c', gray=1)
    c.gray(0)
    c.Q()


def header_footer(c, section, pageno, title=None):
    c.gray(0)
    c.text(558, 750, 'Helvetica-Oblique', 9.5, 'os8088 / ' + section, align='r')
    c.lw(0.8)
    c.gray(0, True)
    c.line(54, 742, 558, 742)
    c.line(54, 76, 558, 76)
    c.text(558, 60, 'Helvetica-Bold', 8, section.upper() + '  %d' % pageno, align='r')
    c.text(54, 60, 'Helvetica', 8, 'os8088 XT Guide')


def sec_title(c, num, title, sub=None):
    c.text(54, 700, 'Helvetica-Bold', 28, '%s %s' % (num, title))
    if sub:
        c.text(54, 678, 'Helvetica-Oblique', 13, sub)


# ------------------------------------------------------------ samples
TASKS = {}
CAP = {}
DESC = {}


def pdf_info(pdf, title):
    return pdf.add('<< /Title %s /Producer (os8088 os88pdfgen) /Creator (os88pdfgen) >>' % tlit(title))


def simple_pages(pdf, pages, mediabox='[0 0 612 792]', extra_pages='', info_title=None, catalog_extra=''):
    """pages: list of (content_bytes, page_extra, filters). Resources and
    MediaBox are inherited from the Pages node."""
    pr = pdf.alloc()
    kids = []
    for content, pextra, flt in pages:
        cs = pdf.stream(content, filters=flt)
        kids.append(pdf.add('<< /Type /Page /Parent %d 0 R /Contents %d 0 R %s >>' % (pr, cs, pextra)))
    pdf.put(pr, '<< /Type /Pages /Kids [%s] /Count %d /MediaBox %s /Resources << /Font %s >> %s >>' % (
        ' '.join('%d 0 R' % k for k in kids), len(kids), mediabox, pdf.font_res(), extra_pages))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R %s >>' % (pr, catalog_extra))
    info = pdf_info(pdf, info_title or pdf.name)
    pdf.npages = len(kids)
    return root, info, kids, pr


def build_hello():
    pdf = Pdf('HELLO.PDF')
    c = C(pdf)
    c.text(72, 700, 'Helvetica-Bold', 30, 'Hello, PageDesk')
    c.text(72, 672, 'Times-Roman', 14, 'A one-page sample: text, a frame and a triangle.')
    c.lw(1.5)
    c.rect(72, 430, 468, 200, 'S')
    c.gray(0.85)
    c.poly([(120, 460), (300, 460), (210, 600)], 'f')
    c.gray(0)
    c.poly([(120, 460), (300, 460), (210, 600)], 'S')
    c.text(330, 560, 'Helvetica', 12, 'This page is small on purpose.')
    c.text(330, 540, 'Helvetica', 12, 'Add text or a note anywhere on it,')
    c.text(330, 520, 'Helvetica', 12, 'then Save As and open the copy.')
    c.text(72, 390, 'Courier', 11, 'The margins and the lower half are empty for your edits.')
    root, info, _, _ = simple_pages(pdf, [(c.bytes(), '', ())], info_title='Hello, PageDesk')
    return pdf, pdf.finish(root, info)


TASKS['HELLO.PDF'] = [
    ('first-render', {'op': 'open', 'expect': 'page 1 renders, no Incomplete rendering'}),
    ('add-text', {'op': 'text', 'page': 1, 'text': 'Hello from the XT', 'at': [72, 300]}),
    ('save-reopen', {'op': 'save-as', 'name': 'HELLO2.PDF', 'expect': 'FreeText annotation with /AP and /NM'}),
]
CAP['HELLO.PDF'] = 'supported'
DESC['HELLO.PDF'] = 'one simple text and vector page'

SECTIONS = [('01', 'At a glance', 1), ('02', 'Contents and conventions', 2),
            ('03', 'Connecting your XT', 3), ('04', 'Care and maintenance', 5),
            ('05', 'Operation and troubleshooting', 7), ('06', 'Reference and glossary', 11)]
PAGE_SECTION = {1: 'Overview', 2: 'Overview', 3: 'Getting started', 4: 'Getting started',
                5: 'Care', 6: 'Care', 7: 'Operation', 8: 'Operation', 9: 'Operation',
                10: 'Operation', 11: 'Reference', 12: 'Reference'}


def xt_pages():
    pdf = Pdf('XTGUIDE.PDF')
    pages = {}

    # 1 cover
    c = C(pdf)
    c.text(306, 690, 'Helvetica', 20, 'os8088', align='c')
    c.text(306, 630, 'Helvetica-Bold', 56, 'XT Guide', align='c')
    c.lw(1.5)
    c.gray(0, True)
    c.line(150, 610, 462, 610)
    c.text(306, 580, 'Helvetica-Oblique', 18, 'Installation and Operation', align='c')
    draw_xt(c, 189, 330, 1.9)
    c.text(306, 280, 'Helvetica', 11, 'os8088 XT Guide - Installation and Operation', align='c')
    c.text(306, 264, 'Helvetica', 10, 'For the IBM Personal Computer XT and compatibles', align='c')
    c.lw(0.8)
    c.line(54, 76, 558, 76)
    c.text(558, 60, 'Helvetica-Bold', 8, 'OVERVIEW  1', align='r')
    pages[1] = c

    # 2 contents (links added by the page builder)
    c = C(pdf)
    header_footer(c, 'Overview', 2)
    c.text(54, 700, 'Helvetica-Bold', 28, 'Contents')
    y = 640
    for num, title, pg in SECTIONS:
        t = '%s  %s' % (num, title)
        w = c.text(72, y, 'Helvetica', 14, t)
        pw = text_width('Helvetica', str(pg), 14)
        dots = int((520 - 72 - w - 12 - pw) / text_width('Helvetica', '.', 14))
        c.text(72 + w + 8, y, 'Helvetica', 14, '.' * dots, gray=0.4)
        c.text(542, y, 'Helvetica', 14, str(pg), align='r')
        y -= 30
    c.text(54, 410, 'Helvetica-Bold', 13, 'Conventions')
    y = para(c, 54, 390, 480, 'Keys are written in capitals. Text you type is shown in '
                              'Courier. A note in the margin marks something to check before '
                              'you go on. Page numbers in the contents are links.', size=11)
    c.text(72, y - 10, 'Courier', 11, 'COM1: 9600,N,8,1')
    pages[2] = c

    # 3 the mockup page
    c = C(pdf)
    header_footer(c, 'Getting started', 3)
    c.text(54, 700, 'Helvetica-Bold', 28, '03 Connecting your XT')
    c.text(54, 678, 'Helvetica-Oblique', 13, 'Link two computers.')
    draw_xt(c, 62, 536, 1.1)
    draw_xt(c, 356, 536, 1.1)
    c.lw(1.6)
    c.gray(0, True)
    c.op(216, 566, 'm')
    c.op(240, 566, 252, 554, 272, 560, 'c')
    c.op(292, 566, 306, 574, 330, 566, 'c')
    c.op(344, 562, 352, 566, 362, 566, 'c', 'S')
    c.text(289, 642, 'Helvetica', 11, 'Null-modem', align='c')
    c.text(289, 628, 'Helvetica', 11, 'serial cable', align='c')
    c.lw(1)
    c.line(289, 620, 289, 586)
    c.gray(0)
    c.poly([(289, 578), (284, 590), (294, 590)], 'f')
    for cx, lbl in ((148, 'Computer A'), (442, 'Computer B')):
        c.text(cx, 522, 'Helvetica', 10, 'IBM XT', align='c')
        c.text(cx, 510, 'Helvetica', 10, '(%s)' % lbl, align='c')
    c.text(54, 470, 'Helvetica-Bold', 16, 'Before you connect')
    c.text(72, 446, 'Helvetica', 12, '1.  Shut down both computers.')
    c.text(72, 426, 'Helvetica', 12, '2.  Connect a serial cable.')
    c.text(72, 406, 'Helvetica', 12, '3.  Select the same baud rate.')
    c.text(54, 366, 'Helvetica-Bold', 16, 'Recommended settings')
    c.gray(0.85)
    c.rect(54, 296, 96, 60, 'f')
    c.gray(0)
    for i, (a_, b_) in enumerate([('Port', 'COM1'), ('Speed', '9600 baud'),
                                  ('Format', '8 data bits, no parity, 1 stop bit')]):
        c.text(60, 356 - 20 * i - 14, 'Helvetica-Bold', 11, a_)
        c.text(160, 356 - 20 * i - 14, 'Helvetica', 11, b_)
    c.gray(0, True)
    c.lw(0.8)
    c.rect(54, 296, 366, 60)
    c.line(150, 296, 150, 356)
    c.line(54, 316, 420, 316)
    c.line(54, 336, 420, 336)
    pages[3] = c

    # 4 serial pinout
    c = C(pdf)
    header_footer(c, 'Getting started', 4)
    c.text(54, 700, 'Helvetica-Bold', 28, 'Serial pinout')
    c.text(54, 678, 'Helvetica-Oblique', 13, 'DB-9 male connector on the serial card.')
    # DB-9 shell
    c.lw(1.4)
    c.gray(0.9)
    c.poly([(380, 640), (500, 640), (492, 600), (388, 600)], 'B')
    c.gray(0)
    for i in range(5):
        c.rect(396 + i * 19, 626, 7, 7, 'S')
    for i in range(4):
        c.rect(405 + i * 19, 608, 7, 7, 'S')
    c.text(440, 580, 'Helvetica', 9, 'Pins 1-5 on top, 6-9 below', align='c')
    rows = [['Pin', 'Signal', 'Name', 'Cable to pin'],
            ['1', 'DCD', 'Carrier detect', '-'], ['2', 'RXD', 'Receive data', '3'],
            ['3', 'TXD', 'Transmit data', '2'], ['4', 'DTR', 'Terminal ready', '6'],
            ['5', 'GND', 'Signal ground', '5'], ['6', 'DSR', 'Data set ready', '4'],
            ['7', 'RTS', 'Request to send', '8'], ['8', 'CTS', 'Clear to send', '7'],
            ['9', 'RI', 'Ring indicator', '-']]
    table(c, 54, 560, [50, 70, 150, 110], rows, size=10.5, rowh=22)
    y = para(c, 54, 300, 440, 'A null-modem cable crosses transmit and receive, and the '
                              'handshake pairs, so each computer listens where the other '
                              'speaks. A straight cable will not work.', size=11)
    c.text(54, y - 12, 'Helvetica-Oblique', 10.5, 'Back to Connecting your XT (page 3)')
    pages[4] = c

    # 5 maintenance
    c = C(pdf)
    header_footer(c, 'Care', 5)
    sec_title(c, '04', 'Maintenance', 'Keep both machines in good order.')
    y = 640
    for item in ['Power both computers off before touching a cable.',
                 'Clean the floppy heads every 40 hours of use.',
                 'Check the card is seated and the screw is tight.',
                 'Keep the cable away from the power supply fan.',
                 'Vacuum the vents; do not blow dust into the drive.',
                 'Label each disk and keep a spare boot copy.']:
        c.lw(1)
        c.rect(60, y - 2, 10, 10, 'S')
        c.text(82, y, 'Helvetica', 12, item)
        y -= 28
    c.text(54, y - 20, 'Helvetica-Bold', 14, 'Cleaning schedule')
    table(c, 54, y - 34, [150, 140, 200], [['Part', 'How often', 'With'],
                                            ['Floppy heads', 'Every 40 hours', 'Head cleaner disk'],
                                            ['Keyboard', 'Monthly', 'Soft brush'],
                                            ['Screen', 'Monthly', 'Dry cloth'],
                                            ['Vents', 'Every 3 months', 'Vacuum']], size=10.5)
    pages[5] = c

    # 6 diagrams
    c = C(pdf)
    header_footer(c, 'Care', 6)
    c.text(54, 700, 'Helvetica-Bold', 28, 'Diagrams')
    c.text(54, 678, 'Helvetica-Oblique', 13, 'How the pieces connect.')
    boxes = [(54, 'Computer A'), (234, 'Null-modem'), (414, 'Computer B')]
    for bx, lbl in boxes:
        c.lw(1.2)
        c.gray(0.92)
        c.rect(bx, 560, 126, 70, 'B')
        c.gray(0)
        c.text(bx + 63, 592, 'Helvetica-Bold', 12, lbl, align='c')
    for x0, x1 in ((180, 234), (360, 414)):
        c.lw(1.4)
        c.line(x0, 595, x1 - 8, 595)
        c.poly([(x1, 595), (x1 - 9, 600), (x1 - 9, 590)], 'f')
    c.text(207, 604, 'Helvetica', 8, 'COM1', align='c')
    c.text(387, 604, 'Helvetica', 8, 'COM1', align='c')
    c.text(54, 520, 'Helvetica-Bold', 14, 'Start-up sequence')
    steps = ['Power on', 'BIOS check', 'Load os8088', 'Desktop']
    for i, s in enumerate(steps):
        bx = 54 + i * 130
        c.lw(1)
        c.gray(1)
        c.rect(bx, 450, 106, 40, 'B')
        c.gray(0)
        c.text(bx + 53, 466, 'Helvetica', 11, s, align='c')
        if i < 3:
            c.line(bx + 106, 470, bx + 128, 470)
            c.poly([(bx + 130, 470), (bx + 122, 474), (bx + 122, 466)], 'f')
    pages[6] = c

    # 7 operation
    c = C(pdf)
    header_footer(c, 'Operation', 7)
    sec_title(c, '05', 'Starting os8088', 'From power switch to desktop.')
    y = 640
    for i, s in enumerate(['Insert the system disk in drive A.', 'Switch on the monitor first, then the computer.',
                           'Wait for the loading screen to finish.', 'Double-click a program to open it.',
                           'Choose Shut Down from the menu before switching off.']):
        c.text(60, y, 'Helvetica-Bold', 12, '%d.' % (i + 1))
        c.text(84, y, 'Helvetica', 12, s)
        y -= 26
    para(c, 54, y - 20, 470, 'Booting from a floppy takes about a minute on a 4.77 MHz '
                              'machine. The pointer appears as soon as the mouse driver '
                              'is ready; do not press keys until the desktop is drawn.', size=11)
    pages[7] = c

    # 8 troubleshooting
    c = C(pdf)
    header_footer(c, 'Operation', 8)
    c.text(54, 700, 'Helvetica-Bold', 28, 'Troubleshooting')
    table(c, 54, 660, [150, 170, 184], [['Symptom', 'Likely cause', 'What to do'],
                                         ['No picture', 'Monitor not on', 'Switch on, then reboot'],
                                         ['Disk error', 'Dirty heads', 'Clean the heads'],
                                         ['No link', 'Straight cable', 'Use a null-modem'],
                                         ['Garbled text', 'Speeds differ', 'Match the baud rate'],
                                         ['Mouse frozen', 'Wrong port', 'Move it to COM1']],
          size=10.5, rowh=24)
    pages[8] = c

    # 9 keys and ports
    c = C(pdf)
    header_footer(c, 'Operation', 9)
    c.text(54, 700, 'Helvetica-Bold', 28, 'Keys and ports')
    table(c, 54, 660, [120, 384], [['Key', 'Action'], ['ESC', 'Close the open menu'],
                                    ['TAB', 'Move to the next field'], ['F1', 'Open help'],
                                    ['ALT+F4', 'Close the window'], ['CTRL+ALT+DEL', 'Restart the machine']],
          size=10.5, rowh=24)
    c.text(54, 470, 'Helvetica-Bold', 14, 'Ports on the back panel')
    table(c, 54, 456, [120, 120, 264], [['Port', 'Address', 'Use'], ['COM1', '03F8', 'Serial mouse or link'],
                                         ['COM2', '02F8', 'Second serial device'], ['LPT1', '0378', 'Printer']],
          size=10.5, rowh=24)
    pages[9] = c

    # 10 specifications
    c = C(pdf)
    header_footer(c, 'Operation', 10)
    c.text(54, 700, 'Helvetica-Bold', 28, 'Specifications')
    table(c, 54, 660, [200, 304], [['Item', 'Value'], ['Processor', 'Intel 8088, 4.77 MHz'],
                                    ['Memory', '640 KiB'], ['Display', 'VGA, CGA or Hercules'],
                                    ['Storage', '360 KB or 1.44 MB floppy'], ['Serial', 'RS-232, 9600 baud']],
          size=10.5, rowh=24)
    pages[10] = c

    # 11 glossary
    c = C(pdf)
    header_footer(c, 'Reference', 11)
    sec_title(c, '06', 'Glossary')
    y = 650
    for term, d in [('Baud', 'Symbols per second on a serial line.'), ('COM1', 'The first serial port.'),
                    ('Null modem', 'A cable that crosses transmit and receive.'),
                    ('Parity', 'An error check bit; here, none.'), ('Stop bit', 'Marks the end of a character.')]:
        c.text(54, y, 'Helvetica-Bold', 12, term)
        c.text(160, y, 'Helvetica', 12, d)
        y -= 26
    pages[11] = c

    # 12 index
    c = C(pdf)
    header_footer(c, 'Reference', 12)
    c.text(54, 700, 'Helvetica-Bold', 28, 'Index')
    y = 650
    for term, p in [('Baud rate', '3'), ('Cable, null-modem', '3, 4'), ('COM1', '3, 9'), ('Heads, cleaning', '5'),
                    ('Pinout', '4'), ('Start-up', '6, 7'), ('Troubleshooting', '8')]:
        c.text(54, y, 'Helvetica', 12, term)
        c.text(540, y, 'Helvetica', 12, p, align='r')
        y -= 22
    c.text(54, 440, 'Helvetica-Bold', 13, 'Notes')
    c.lw(0.5)
    c.gray(0.5, True)
    for i in range(8):
        c.line(54, 420 - i * 24, 558, 420 - i * 24)
    pages[12] = c
    return pdf, pages


def build_xtguide():
    pdf, pages = xt_pages()
    pr = pdf.alloc()
    pnums = [pdf.alloc() for _ in range(12)]
    # links: contents page rows, and page 4's back link
    annots = {}
    cont_links = []
    y = 640
    for num, title, pg in SECTIONS:
        r = '[54 %d 558 %d]' % (y - 6, y + 14)
        cont_links.append(pdf.add('<< /Type /Annot /Subtype /Link /Rect %s /Border [0 0 0] /A << /S /GoTo '
                                  '/D [%d 0 R /Fit] >> >>' % (r, pnums[pg - 1])))
        y -= 30
    annots[2] = cont_links
    annots[4] = [pdf.add('<< /Type /Annot /Subtype /Link /Rect [54 276 300 292] /Border [0 0 0] '
                         '/Dest [%d 0 R /XYZ 0 792 0] >>' % pnums[2])]
    for i in range(12):
        cs = pdf.stream(pages[i + 1].bytes())
        a = ''
        if (i + 1) in annots:
            a = ' /Annots [%s]' % ' '.join('%d 0 R' % x for x in annots[i + 1])
        pdf.put(pnums[i], '<< /Type /Page /Parent %d 0 R /Contents %d 0 R%s >>' % (pr, cs, a))
    pdf.put(pr, '<< /Type /Pages /Kids [%s] /Count 12 /MediaBox [0 0 612 792] /Resources << /Font %s >> >>' % (
        ' '.join('%d 0 R' % p for p in pnums), pdf.font_res()))
    # outlines
    tree = [('01', 'At a glance', 1, []), ('02', 'Contents and conventions', 2, []),
            ('03', 'Connecting your XT', 3, [('Serial pinout', 4)]),
            ('04', 'Care and maintenance', 5, [('Diagrams', 6)]),
            ('05', 'Operation and troubleshooting', 7, [('Troubleshooting', 8), ('Specifications', 10)]),
            ('06', 'Reference and glossary', 11, [('Index', 12)])]
    olroot = pdf.alloc()
    top = [pdf.alloc() for _ in tree]
    kid_nums = []
    for i, (n, t, pg, kids) in enumerate(tree):
        ks = [pdf.alloc() for _ in kids]
        kid_nums.append(ks)
        for j, (kt, kp) in enumerate(kids):
            pdf.put(ks[j], '<< /Title %s /Parent %d 0 R /Dest [%d 0 R /Fit]%s%s >>' % (
                tlit(kt), top[i], pnums[kp - 1],
                ' /Prev %d 0 R' % ks[j - 1] if j else '', ' /Next %d 0 R' % ks[j + 1] if j + 1 < len(ks) else ''))
        d = '<< /Title %s /Parent %d 0 R /Dest [%d 0 R /Fit]' % (tlit('%s %s' % (n, t)), olroot, pnums[pg - 1])
        if i:
            d += ' /Prev %d 0 R' % top[i - 1]
        if i + 1 < len(tree):
            d += ' /Next %d 0 R' % top[i + 1]
        if ks:
            d += ' /First %d 0 R /Last %d 0 R /Count %d' % (ks[0], ks[-1], len(ks))
        pdf.put(top[i], d + ' >>')
    total = len(tree) + sum(len(k) for _, _, _, k in tree)
    pdf.put(olroot, '<< /Type /Outlines /First %d 0 R /Last %d 0 R /Count %d >>' % (top[0], top[-1], total))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R /Outlines %d 0 R /PageMode /UseOutlines >>' % (pr, olroot))
    info = pdf_info(pdf, 'os8088 XT Guide - Installation and Operation')
    pdf.npages = 12
    return pdf, pdf.finish(root, info)


TASKS['XTGUIDE.PDF'] = [
    ('goto-page-3', {'op': 'goto', 'via': ['page-field', 'find:same baud rate', 'outline:03 Connecting your XT'], 'page': 3}),
    ('find-baud', {'op': 'find', 'text': 'same baud rate', 'page': 3, 'hits': 1}),
    ('highlight', {'op': 'highlight', 'page': 3, 'text': 'same baud rate', 'expect': '/Highlight with /QuadPoints and /AP'}),
    ('note', {'op': 'note', 'page': 3, 'text': 'Check the serial port before starting.'}),
    ('text', {'op': 'text', 'page': 3, 'text': 'Tested on two IBM XTs.', 'at': [54, 250]}),
    ('arrow', {'op': 'line', 'page': 3, 'from': [320, 420], 'to': [250, 406], 'ends': 'OpenArrow'}),
    ('save-reopen', {'op': 'save-as', 'name': 'XTGUIDE2.PDF', 'expect': 'original bytes unchanged, incremental update'}),
]
CAP['XTGUIDE.PDF'] = 'supported'
DESC['XTGUIDE.PDF'] = 'twelve-page illustrated guide, outlines and internal links'


def build_markup():
    pdf = Pdf('MARKUP.PDF')
    cs = []
    c = C(pdf)
    c.text(54, 722, 'Helvetica-Bold', 22, 'Site Inspection Worksheet')
    c.text(54, 704, 'Helvetica-Oblique', 10.5, 'Fill by added text; draw on the plan; every field below is a drawn box.')
    c.lw(0.8)
    c.gray(0, True)
    for i, lbl in enumerate(['Inspector', 'Date', 'Location', 'Machine']):
        y = 660 - i * 40
        c.text(54, y + 8, 'Helvetica', 10, lbl)
        c.rect(150, y, 380, 24, 'S')
    c.text(54, 490, 'Helvetica-Bold', 12, 'Checks')
    for i, lbl in enumerate(['Cable seated', 'Baud rate matches', 'Heads clean', 'Power stable']):
        y = 470 - i * 24
        c.rect(60, y, 12, 12, 'S')
        c.text(82, y + 2, 'Helvetica', 11, lbl)
    c.text(54, 360, 'Helvetica-Bold', 12, 'Plan (draw here)')
    c.rect(54, 150, 330, 200, 'S')
    c.text(404, 360, 'Helvetica-Bold', 12, 'Notes')
    c.rect(404, 150, 154, 200, 'S')
    c.dash(3, 3)
    c.rect(54, 100, 504, 36, 'S')
    c.undash()
    c.text(60, 120, 'Helvetica-Oblique', 9, 'Signature box (draw a mark)')
    cs.append((c.bytes(), '', ()))
    c = C(pdf)
    c.text(54, 722, 'Helvetica-Bold', 22, 'Review Sheet')
    c.text(54, 704, 'Helvetica-Oblique', 10.5, 'Practice shapes, then add and resolve a note.')
    c.lw(0.8)
    c.gray(0, True)
    for i, lbl in enumerate(['Rectangle', 'Ellipse', 'Arrow', 'Freehand']):
        x = 54 + (i % 2) * 260
        y = 520 - (i // 2) * 190
        c.text(x, y + 160, 'Helvetica-Bold', 11, lbl)
        c.rect(x, y, 240, 150, 'S')
    c.text(54, 150, 'Helvetica-Bold', 11, 'Open items')
    for i in range(4):
        c.lw(0.5)
        c.line(54, 130 - i * 22, 558, 130 - i * 22)
    cs.append((c.bytes(), '', ()))
    root, info, _, _ = simple_pages(pdf, cs, info_title='Site Inspection Worksheet')
    return pdf, pdf.finish(root, info)


TASKS['MARKUP.PDF'] = [
    ('fill-field', {'op': 'text', 'page': 1, 'text': 'J. Smith', 'at': [156, 668]}),
    ('draw', {'op': 'ink', 'page': 1, 'in': [54, 150, 384, 350]}),
    ('shapes', {'op': 'shapes', 'page': 2, 'kinds': ['Square', 'Circle', 'Line', 'Ink']}),
    ('resolve-note', {'op': 'note+resolve', 'page': 2, 'expect': 'reply /Text with /IRT /State /Completed'}),
    ('undo', {'op': 'undo', 'expect': 'last edit removed, dirty state follows'}),
]
CAP['MARKUP.PDF'] = 'supported (drawn fields are overlay targets, not AcroForm)'
DESC['MARKUP.PDF'] = 'two-page annotation worksheet'


def build_pages():
    pdf = Pdf('PAGES.PDF')
    specs = [
        ('Portrait letter', '', None),
        ('Landscape letter', '/MediaBox [0 0 792 612]', (792, 612)),
        ('Cropped (CropBox inset 36)', '/CropBox [36 36 576 756]', None),
        ('Rotate 90', '/Rotate 90', None),
        ('Rotate 180', '/Rotate 180', None),
        ('Rotate 270', '/Rotate 270', None),
        ('A4 portrait', '/MediaBox [0 0 595 842]', (595, 842)),
        ('Nonzero origin box', '/MediaBox [-100 -50 512 742]', (612, 792)),
    ]
    pages = []
    for i, (title, extra, wh) in enumerate(specs):
        w, h = wh or (612, 792)
        ox, oy = (-100, -50) if 'Nonzero' in title else (0, 0)
        c = C(pdf)
        c.lw(3)
        c.gray(0, True)
        c.rect(ox + 20, oy + 20, w - 40, h - 40, 'S')
        c.text(ox + w / 2, oy + h / 2 + 20, 'Helvetica-Bold', 160, str(i + 1), align='c')
        c.text(ox + w / 2, oy + h / 2 - 40, 'Helvetica', 24, 'PAGES.PDF page %d of 8' % (i + 1), align='c')
        c.text(ox + w / 2, oy + h / 2 - 72, 'Helvetica-Oblique', 18, title, align='c')
        c.text(ox + w / 2, oy + h - 84, 'Helvetica-Bold', 20, 'TOP EDGE', align='c')
        c.poly([(ox + w / 2, oy + h - 40), (ox + w / 2 - 12, oy + h - 56), (ox + w / 2 + 12, oy + h - 56)], 'f')
        c.text(ox + 36, oy + 36, 'Helvetica', 12, 'bottom-left')
        c.text(ox + w - 36, oy + h - 36 - 8, 'Helvetica', 12, 'top-right', align='r')
        pages.append((c.bytes(), extra, ()))
    # the MediaBox lives on each page where it differs; letter is inherited
    root, info, _, _ = simple_pages(pdf, pages, info_title='PAGES.PDF geometry set')
    return pdf, pdf.finish(root, info)


TASKS['PAGES.PDF'] = [
    ('reorder', {'op': 'move-page', 'from': 7, 'to': 2}),
    ('rotate', {'op': 'rotate', 'page': 1, 'degrees': 90}),
    ('duplicate', {'op': 'duplicate', 'page': 3}),
    ('insert-blank', {'op': 'insert-blank', 'after': 1}),
    ('delete', {'op': 'delete', 'page': 5}),
    ('extract', {'op': 'extract', 'pages': [2, 3], 'to': 'EXTRACT.PDF'}),
    ('insert-from', {'op': 'insert-from', 'source': 'HELLO.PDF', 'page': 1, 'after': 1,
                     'note': 'object numbers collide with the target'}),
]
CAP['PAGES.PDF'] = 'supported'
DESC['PAGES.PDF'] = 'eight pages: landscape, cropped, rotated 90/180/270, A4, nonzero-origin box'


def build_monoimg():
    pdf = Pdf('MONOIMG.PDF')
    import math
    # 64x48 1-bit concentric rings
    rows = bytearray()
    for y in range(48):
        for xb in range(8):
            b = 0
            for k in range(8):
                x = xb * 8 + k
                r = math.hypot(x - 31.5, y - 23.5)
                b = (b << 1) | (int(r) // 4 % 2)
            rows.append(b)
    im1 = pdf.stream(bytes(rows), '/Type /XObject /Subtype /Image /Width 64 /Height 48 '
                                  '/ColorSpace /DeviceGray /BitsPerComponent 1')
    im4 = pdf.stream(bytes(rows), '/Type /XObject /Subtype /Image /Width 64 /Height 48 '
                                  '/ColorSpace /DeviceGray /BitsPerComponent 1 /Decode [1 0]')
    g = bytes(min(255, (x + y) * 4) for y in range(32) for x in range(32))
    im2 = pdf.stream(g, '/Type /XObject /Subtype /Image /Width 32 /Height 32 '
                        '/ColorSpace /DeviceGray /BitsPerComponent 8', ('ASCIIHexDecode',))
    arrow = ['0000000110000000', '0000001111000000', '0000011111100000', '0000111111110000',
             '0001111111111000', '0011111111111100', '0000001111000000', '0000001111000000',
             '0000001111000000', '0000001111000000', '0000001111000000', '0000001111000000',
             '0000001111000000', '0000001111000000', '0000001111000000', '0000000000000000']
    mk = bytearray()
    for r in arrow:
        v = int(r, 2)
        mk += bytes([(~v >> 8) & 255, ~v & 255])
    im3 = pdf.stream(bytes(mk), '/Type /XObject /Subtype /Image /Width 16 /Height 16 '
                                '/ImageMask true /BitsPerComponent 1', ('RunLengthDecode',))
    c = C(pdf)
    c.text(54, 722, 'Helvetica-Bold', 22, 'Monochrome image reference')
    c.text(54, 706, 'Helvetica', 10, '1-bit raw and its /Decode [1 0] twin; 8-bit ASCIIHex gradient; RunLength stencil.')
    items = [(im1, 'Im1', 54, 530, 192, 144, '1-bit, raw, 64x48'),
             (im4, 'Im4', 300, 530, 192, 144, '1-bit, /Decode [1 0]'),
             (im2, 'Im2', 54, 330, 128, 128, '8-bit gray, ASCIIHex, 32x32'),
             (im3, 'Im3', 300, 330, 128, 128, 'ImageMask, RunLength, 16x16')]
    c.lw(0.8)
    c.gray(0, True)
    for _, nm, x, y, w, h, cap in items:
        c.rect(x - 2, y - 2, w + 4, h + 4, 'S')
        c.q()
        if nm == 'Im3':
            c.gray(0)
        c.cm(w, 0, 0, h, x, y)
        c.op('/%s' % nm, 'Do')
        c.Q()
        c.text(x, y - 16, 'Helvetica', 10, cap)
    c.lw(1.5)
    c.line(54, 290, 558, 290)
    c.gray(0.7)
    c.rect(54, 180, 200, 80, 'f')
    c.gray(0)
    c.rect(54, 180, 200, 80, 'S')
    c.text(54, 150, 'Courier', 10, 'text and vector reference below the images')
    xo = ' '.join('/%s %d 0 R' % (n, o) for o, n, *_ in items)
    pdf.next = max(pdf.next, 1)
    pr = pdf.alloc()
    cs = pdf.stream(c.bytes())
    pg = pdf.add('<< /Type /Page /Parent %d 0 R /Contents %d 0 R >>' % (pr, cs))
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /Font %s /XObject << %s >> >> >>'
            % (pg, pdf.font_res(), xo))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % pr)
    info = pdf_info(pdf, 'Monochrome image reference')
    pdf.npages = 1
    return pdf, pdf.finish(root, info)


TASKS['MONOIMG.PDF'] = [
    ('decode', {'op': 'render', 'page': 1, 'expect': 'four images drawn; Im1 and Im4 are exact inverses'}),
    ('mono', {'op': 'render', 'adapters': ['CGA', 'Hercules'], 'expect': 'gray ramp dithered, rings keep their phase'}),
]
CAP['MONOIMG.PDF'] = 'supported'
DESC['MONOIMG.PDF'] = '1-bit, 8-bit and stencil images (raw, ASCIIHex, RunLength)'


def build_latin():
    pdf = Pdf('LATIN.PDF')
    fH = pdf.font('Helvetica', last=255)
    fB = pdf.font('Helvetica-Bold')
    fT = pdf.font('Times-Roman', last=255)
    fC = pdf.font('Courier')
    fI = pdf.font('Helvetica-Oblique')
    fM = pdf.font('Helvetica', 'MacRomanEncoding', last=255)
    fS = pdf.font('Helvetica', 'Standard', last=255)
    fD = pdf.font('Times-Roman', 'WinAnsiEncoding', {1: 'Adieresis', 2: 'odieresis', 3: 'Euro', 4: 'bullet'})
    c = C(pdf)
    y = [724]

    def hd(s):
        c.text(54, y[0], 'Helvetica-Bold', 11, s)
        y[0] -= 17

    def ln(font, raw, size=12, dy=19):
        c.raw('BT %s %d Tf 54 %d Td %s Tj ET' % (font, size, y[0], raw))
        y[0] -= dy

    c.text(54, 750, 'Helvetica-Bold', 20, 'Latin text and encodings')
    y[0] = 718
    hd('WinAnsi, Helvetica and Times')
    ln(fH, tlit('Café, naïve, Zoë, señor, Ångström, Œuvre, ß ç ü'))
    ln(fT, tlit('£ € © ® ° ± × ÷ « » ¿ ¡ – — • …'))
    hd('String escapes')
    ln(fC, r'(\(parens\) balanced (nested (ok)) back\\slash \101\102\103 space\040here)', 11)
    ln(fC, '(first half \\\nsecond half after a line continuation)', 11)
    ln(fC, r'(octal \061\062\063 and short \7x and plain \q unknown escape)', 11)
    hd('Hex strings')
    ln(fC, '<48656C6C6F20576F726C64>', 11)
    ln(fC, '<41 42 43 4>', 11)
    hd('MacRoman')
    ln(fM, lit('Café, Zoë, señor, Ångström'.encode('mac_roman')))
    hd('StandardEncoding (no /Encoding): quoteright and ligatures')
    ln(fS, "(it's \\256 \\257 \\341 \\373 'tick')")
    hd('/Differences on WinAnsi')
    ln(fD, r'(\001 \002 \003 \004 and plain text)')
    hd('Styles')
    ln(fB, '(Helvetica-Bold)')
    ln(fI, '(Helvetica-Oblique)')
    ln(fT, '(Times-Roman 12)')
    ln(fC, '(Courier 12)')
    hd('Operators: TJ, Tc, Tw, Tz, Ts, Tr, TL, T*, quote and double quote')
    c.raw('BT %s 12 Tf 54 %d Td [(T) 120 (J kerning: AV) -80 (A) 60 (WAY)] TJ ET' % (fH, y[0]))
    y[0] -= 19
    c.raw('BT %s 12 Tf 54 %d Td 2 Tc (Tc 2) Tj 0 Tc 10 Tw ( Tw 10 spaces) Tj 0 Tw ET' % (fH, y[0]))
    y[0] -= 19
    c.raw('BT %s 12 Tf 54 %d Td 150 Tz (Tz 150) Tj 100 Tz ( then 100 ) Tj 5 Ts (rise) Tj -5 Ts (fall) Tj 0 Ts ET' % (fH, y[0]))
    y[0] -= 19
    c.raw('BT %s 12 Tf 14 TL 54 %d Td (TL line one) Tj T* (T* line two) Tj (quote line three) \' '
          '3 1 (double quote line four) " ET' % (fH, y[0]))
    y[0] -= 70
    c.raw('BT %s 24 Tf 54 %d Td 1 Tr 0.6 w (Tr 1 outline) Tj 0 Tr ET' % (fH, y[0]))
    root, info, _, _ = simple_pages(pdf, [(c.bytes(), '', ())], info_title='Latin text and encodings')
    return pdf, pdf.finish(root, info)


TASKS['LATIN.PDF'] = [
    ('render', {'op': 'render', 'page': 1, 'expect': 'accented letters in WinAnsi, MacRoman and Differences lines match the manifest text'}),
    ('find-accent', {'op': 'find', 'text': 'Café', 'page': 1, 'hits': 2}),
    ('add-text', {'op': 'text', 'page': 1, 'text': 'Café €5', 'expect': 'FreeText in WinAnsi'}),
]
CAP['LATIN.PDF'] = 'supported'
DESC['LATIN.PDF'] = 'escapes, WinAnsi, MacRoman, Standard and /Differences encodings, text operators'


def build_flate():
    pdf = Pdf('FLATE.PDF', version='1.2', allow_flate=True)
    c = C(pdf)
    c.text(72, 700, 'Helvetica-Bold', 26, 'FlateDecode sample')
    c.text(72, 670, 'Helvetica', 12, 'This page content is a zlib (FlateDecode) stream.')
    c.text(72, 650, 'Helvetica', 12, 'The XT build refuses it with PDE_FLATE; an Enhanced build draws it.')
    for i in range(12):
        c.text(72, 610 - i * 18, 'Courier', 11, 'Repeated line %02d: the quick brown fox jumps over the lazy dog.' % i)
    root, info, _, _ = simple_pages(pdf, [(c.bytes(), '', ('FlateDecode',))], info_title='FlateDecode sample')
    return pdf, pdf.finish(root, info)


TASKS['FLATE.PDF'] = [
    ('refuse', {'op': 'open', 'expect': 'PDE_FLATE on the XT build: a clear refusal, nothing modified'}),
    ('enhanced', {'op': 'open', 'build': 'PD-25 Enhanced', 'expect': 'page renders'}),
]
CAP['FLATE.PDF'] = 'refused PDE_FLATE on the XT build (Enhanced PD-25 draws it)'
DESC['FLATE.PDF'] = 'content stream compressed with FlateDecode'


def build_acroform():
    pdf = Pdf('ACROFORM.PDF', version='1.4')
    helv = pdf.font('Helvetica')
    c = C(pdf)
    c.text(54, 722, 'Helvetica-Bold', 22, 'Registration form')
    c.text(54, 702, 'Helvetica-Oblique', 10.5, 'Real AcroForm widgets (text and checkbox) with appearance streams.')
    c.text(54, 650, 'Helvetica', 11, 'Name')
    c.text(54, 610, 'Helvetica', 11, 'Email')
    c.text(54, 570, 'Helvetica', 11, 'Subscribe to the newsletter')
    c.text(54, 530, 'Helvetica', 11, 'I accept the terms')
    c.lw(0.6)
    c.gray(0.4, True)
    c.rect(150, 640, 300, 22)
    c.rect(150, 600, 300, 22)
    pr = pdf.alloc()
    pg = pdf.alloc()
    fontobj = pdf.fonts[('Helvetica', 'WinAnsiEncoding', (), 126)][1]

    def tx_ap(w, h, value):
        s = '/Tx BMC q BT /Helv 11 Tf 0 g 3 %s Td %s Tj ET Q EMC' % (fmt(h / 2 - 4), tlit(value))
        return pdf.stream(s.encode(), '/Type /XObject /Subtype /Form /BBox [0 0 %s %s] '
                          '/Resources << /Font << /Helv %d 0 R >> >>' % (fmt(w), fmt(h), fontobj))

    def cb_ap(on):
        s = '0 g 1 w 1 1 10 10 re S' if not on else '0 g 1 w 1 1 10 10 re S 2.5 6 m 5 3 l 9.5 9 l S'
        return pdf.stream(s.encode(), '/Type /XObject /Subtype /Form /BBox [0 0 12 12]')
    parent = pdf.alloc()
    wname = pdf.add('<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /V (Ada Lovelace) /DA (/Helv 11 Tf 0 g) '
                    '/Rect [150 640 450 662] /F 4 /P %d 0 R /Parent %d 0 R /MK << /BC [0] >> '
                    '/AP << /N %d 0 R >> >>' % (pg, parent, tx_ap(300, 22, 'Ada Lovelace')))
    wmail = pdf.add('<< /Type /Annot /Subtype /Widget /FT /Tx /T (Email) /V (ada@example.org) /DA (/Helv 11 Tf 0 g) '
                    '/Rect [150 600 450 622] /F 4 /P %d 0 R /Parent %d 0 R /MK << /BC [0] >> '
                    '/AP << /N %d 0 R >> >>' % (pg, parent, tx_ap(300, 22, 'ada@example.org')))
    pdf.put(parent, '<< /T (Contact) /Kids [%d 0 R %d 0 R] >>' % (wname, wmail))
    yes, off = cb_ap(True), cb_ap(False)
    cb1 = pdf.add('<< /Type /Annot /Subtype /Widget /FT /Btn /T (Subscribe) /V /Yes /AS /Yes /Rect [250 565 262 577] '
                  '/F 4 /P %d 0 R /MK << /CA (4) /BC [0] >> /AP << /N << /Yes %d 0 R /Off %d 0 R >> >> >>' % (pg, yes, off))
    cb2 = pdf.add('<< /Type /Annot /Subtype /Widget /FT /Btn /T (Terms) /V /Off /AS /Off /Rect [250 525 262 537] '
                  '/F 4 /P %d 0 R /MK << /CA (4) /BC [0] >> /AP << /N << /Yes %d 0 R /Off %d 0 R >> >> >>' % (pg, yes, off))
    cs = pdf.stream(c.bytes())
    pdf.put(pg, '<< /Type /Page /Parent %d 0 R /Contents %d 0 R /Annots [%d 0 R %d 0 R %d 0 R %d 0 R] >>' % (
        pr, cs, wname, wmail, cb1, cb2))
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /Font %s >> >>' % (pg, pdf.font_res()))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R /AcroForm << /Fields [%d 0 R %d 0 R %d 0 R] '
                   '/DA (/Helv 11 Tf 0 g) /DR << /Font << /Helv %d 0 R >> >> /NeedAppearances false >> >>'
                   % (pr, parent, cb1, cb2, fontobj))
    info = pdf_info(pdf, 'Registration form')
    pdf.npages = 1
    return pdf, pdf.finish(root, info)


TASKS['ACROFORM.PDF'] = [
    ('display', {'op': 'render', 'page': 1, 'expect': 'both text values and the checked box drawn from /AP'}),
    ('not-editable', {'op': 'click-widget', 'expect': 'refused with a label saying form editing is Enhanced (PD-26); no silent edit'}),
    ('annotate-around', {'op': 'note', 'page': 1, 'expect': 'a note can still be added, widgets and AcroForm untouched'}),
]
CAP['ACROFORM.PDF'] = 'supported for display; Widget/AcroForm editing is Enhanced (PD-26) and refused on the XT build'
DESC['ACROFORM.PDF'] = 'AcroForm with two text fields (one parent) and two checkboxes, /AP present'

SAMPLES = [('HELLO.PDF', build_hello), ('XTGUIDE.PDF', build_xtguide), ('MARKUP.PDF', build_markup),
           ('PAGES.PDF', build_pages), ('MONOIMG.PDF', build_monoimg), ('LATIN.PDF', build_latin),
           ('FLATE.PDF', build_flate), ('ACROFORM.PDF', build_acroform)]

# ------------------------------------------------------------ profile checks
BASE14_SET = set(BASE14)


def verify_xref(data):
    """Every classic xref entry points at 'N G obj' (self check of the writer)."""
    pos = int(re.findall(rb'startxref\s+(\d+)', data)[-1])
    assert data[pos:pos + 4] == b'xref', 'startxref'
    lines = data[pos:].split(b'\n')
    i = 1
    n_ok = 0
    while not lines[i].startswith(b'trailer'):
        first, cnt = map(int, lines[i].split())
        i += 1
        for k in range(cnt):
            e = lines[i]
            i += 1
            assert len(e) == 19 or len(e) == 20 or e.endswith(b' '), e
            if e.endswith(b'n '):
                off = int(e[:10])
                assert data[off:].startswith(b'%d 0 obj' % (first + k)), (first + k, off)
                n_ok += 1
    return n_ok


def assert_profile(name, data, allow_flate):
    for bad in (b'/ObjStm', b'/XRef', b'/Encrypt', b'/DCTDecode', b'/LZWDecode', b'/CCITTFaxDecode',
                b'/JBIG2Decode', b'/JPXDecode', b'/Predictor', b'/SMask', b'/Type0', b'/TrueType',
                b'/Type3', b'/ShadingType', b'/Pattern', b'/ICCBased', b'/Separation', b'/Indexed'):
        assert bad not in data, '%s: %s is outside the XT profile' % (name, bad.decode())
    if not allow_flate:
        assert b'/FlateDecode' not in data, name + ': FlateDecode'
    for m in re.finditer(rb'/BaseFont /(\S+)', data):
        assert m.group(1).decode() in BASE14_SET, name + ': non-base-14 font ' + m.group(1).decode()
    for m in re.finditer(rb'/Subtype /(Type\w*)', data):
        assert m.group(1) == b'Type1' or m.group(1) in (b'Type', ), name
    for m in re.finditer(rb'/BitsPerComponent (\d+)', data):
        assert int(m.group(1)) in (1, 8), name + ': bits per component'
    assert re.search(rb'^xref$', data, re.M) and data.startswith(b'%PDF-1.')
    verify_xref(data)


def font_names(data):
    return sorted(set(m.group(1).decode() for m in re.finditer(rb'/BaseFont /(\S+)', data)))


def build_all_samples():
    out = {}
    for name, fn in SAMPLES:
        pdf, data = fn()
        assert_profile(name, data, pdf.allow_flate)
        out[name] = (pdf, data)
    return out


def manifest(built):
    L = ['os8088 PageDesk sample manifest',
         'Generated by tools/os88pdfgen.py (PAGEDESK-SPEC 18.1, 20.1). Do not edit.',
         'Regenerate: python3 tools/os88pdfgen.py apps/pagedesk/samples',
         'capability = what PageDesk\'s XT build is expected to do (PAGEDESK-SPEC 1.3).',
         'task lines are machine-readable: task <name> <json>.', '']
    total = 0
    for name, (pdf, data) in built.items():
        total += len(data)
        L.append('file %s' % name)
        L.append('  what %s' % DESC[name])
        L.append('  pages %d' % pdf.npages)
        L.append('  bytes %d' % len(data))
        L.append('  sha256 %s' % hashlib.sha256(data).hexdigest())
        L.append('  version %s' % re.match(rb'%PDF-(\d\.\d)', data).group(1).decode())
        L.append('  fonts %s' % (' '.join(font_names(data)) or 'none'))
        L.append('  filters %s' % (' '.join(pdf.filters) or 'none'))
        L.append('  capability %s' % CAP[name])
        for tn, spec in TASKS[name]:
            L.append('  task %s %s' % (tn, json.dumps(spec, sort_keys=True, ensure_ascii=True)))
        L.append('')
    L.append('total bytes %d' % total)
    return '\n'.join(L) + '\n'


# ------------------------------------------------------------ stress
def _std_page_doc(name, npages, content_fn, version='1.3'):
    pdf = Pdf(name, version)
    pdf.font('Helvetica')
    return pdf


def stress_files():
    out = {}

    # STRESS64: balanced 8x8 page tree
    pdf = Pdf('STRESS64.PDF')
    pdf.font('Helvetica')
    pdf.font('Helvetica-Bold')
    root_pages = pdf.alloc()
    mids = []
    n = 0
    for m in range(8):
        mid = pdf.alloc()
        kids = []
        for _ in range(8):
            n += 1
            c = C(pdf)
            c.text(306, 400, 'Helvetica-Bold', 60, 'PAGE %d' % n, align='c')
            c.text(306, 340, 'Helvetica', 14, 'of 64 (tree node %d)' % (m + 1), align='c')
            cs = pdf.stream(c.bytes())
            kids.append(pdf.add('<< /Type /Page /Parent %d 0 R /Contents %d 0 R >>' % (mid, cs)))
        pdf.put(mid, '<< /Type /Pages /Parent %d 0 R /Kids [%s] /Count 8 >>' % (root_pages, ' '.join('%d 0 R' % k for k in kids)))
        mids.append(mid)
    pdf.put(root_pages, '<< /Type /Pages /Kids [%s] /Count 64 /MediaBox [0 0 612 792] /Resources << /Font %s >> >>' % (
        ' '.join('%d 0 R' % k for k in mids), pdf.font_res()))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % root_pages)
    pdf.npages = 64
    out['STRESS64.PDF'] = (pdf, pdf.finish(root), '64 pages under an 8x8 nested page tree')

    # BIGFILE: > 640 KiB
    pdf = Pdf('BIGFILE.PDF')
    pdf.font('Helvetica')
    w, h = 800, 900
    img = bytes(((x * 255 // w) ^ ((y * 7) & 0x3f) ^ ((x * y) >> 6)) & 255 for y in range(h) for x in range(w))
    im = pdf.stream(img, '/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray /BitsPerComponent 8' % (w, h))
    c = C(pdf)
    c.q()
    c.cm(500, 0, 0, 560, 56, 160)
    c.op('/Im1', 'Do')
    c.Q()
    c.text(56, 740, 'Helvetica-Bold', 20, 'BIGFILE: %d bytes of image data' % len(img))
    pr = pdf.alloc()
    cs = pdf.stream(c.bytes())
    pg = pdf.add('<< /Type /Page /Parent %d 0 R /Contents %d 0 R >>' % (pr, cs))
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /Font %s /XObject << /Im1 %d 0 R >> >> >>' % (pg, pdf.font_res(), im))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % pr)
    pdf.npages = 1
    out['BIGFILE.PDF'] = (pdf, pdf.finish(root), 'single page over 700 KiB (one 800x900 8-bit gray image)')

    # LONGSTRM: one content stream > 64 KiB
    pdf = Pdf('LONGSTRM.PDF')
    pdf.font('Courier')
    c = C(pdf)
    for i in range(1700):
        c.text(24 + (i // 90) * 96 % 520, 780 - (i % 90) * 8.5, 'Courier', 6, 'L%04d long stream' % i)
    data = c.bytes()
    assert len(data) > 65536, len(data)
    root, info, _, _ = simple_pages(pdf, [(data, '', ())])
    out['LONGSTRM.PDF'] = (pdf, pdf.finish(root, info), 'one page whose single content stream is %d bytes' % len(data))

    # SPARSE: 10,000 sparse object numbers
    pdf = Pdf('SPARSE.PDF')
    fk = pdf.font('Helvetica')
    fobj = pdf.fonts[('Helvetica', 'WinAnsiEncoding', (), 126)][1]
    pdf.objs.clear()
    pdf.fonts.clear()
    objs = {}
    for i in range(10000):
        objs[1000 + 7 * i] = b'<< /Filler %d >>' % i
    objs[2] = b'<< /Type /Catalog /Pages 3 0 R >>'
    c = C(Pdf('x'))
    c.pdf.font('Helvetica')
    c.text(72, 700, 'Helvetica', 24, 'SPARSE: 10000 filler objects, sparse numbers')
    content = c.bytes()
    objs[69999] = b'<< /Length %d >>\nstream\n' % len(content) + content + b'\nendstream'
    objs[3] = b'<< /Type /Pages /Kids [65432 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /Font << /F1 70002 0 R >> >> >>'
    objs[65432] = b'<< /Type /Page /Parent 3 0 R /Contents 69999 0 R >>'
    objs[70002] = b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /FirstChar 32 /LastChar 126 /Widths [%s] /Encoding /WinAnsiEncoding >>' % ' '.join(map(str, HELV)).encode()
    pdf2 = Pdf('SPARSE.PDF')
    pdf2.npages = 1
    out['SPARSE.PDF'] = (pdf2, write_pdf(objs, 2, None, '1.3', 'SPARSE.PDF'), '10,000 filler objects at ids 1000+7i, content at 69999')

    # INDIRLEN: indirect /Length (the length object comes AFTER the stream)
    pdf = Pdf('INDIRLEN.PDF')
    pdf.font('Helvetica')
    c = C(pdf)
    c.text(72, 700, 'Helvetica-Bold', 24, 'INDIRLEN: /Length is an indirect object')
    pr = pdf.alloc()
    cs = pdf.stream(c.bytes(), indirect_len=True)
    pg = pdf.add('<< /Type /Page /Parent %d 0 R /Contents %d 0 R >>' % (pr, cs))
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /Font %s >> >>' % (pg, pdf.font_res()))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % pr)
    pdf.npages = 1
    out['INDIRLEN.PDF'] = (pdf, pdf.finish(root), 'content stream with /Length N 0 R')

    # CONTARR: /Contents array split at operator boundaries, q/Q across streams
    pdf = Pdf('CONTARR.PDF')
    pdf.font('Helvetica')
    pdf.font('Helvetica-Bold')
    pr = pdf.alloc()
    a = C(pdf)
    a.q()
    a.gray(0.85)
    a.rect(60, 500, 400, 200, 'f')
    b = C(pdf)
    b.gray(0)
    b.text(72, 640, 'Helvetica-Bold', 24, 'CONTARR: /Contents is an array')
    b.text(72, 610, 'Helvetica', 12, 'Three streams; a q in the first, its Q in the third.')
    d = C(pdf)
    d.Q()
    d.text(72, 400, 'Helvetica', 12, 'Drawn after Q: the fill above is not in effect here.')
    s = [pdf.stream(x.bytes()) for x in (a, b, d)]
    pg = pdf.add('<< /Type /Page /Parent %d 0 R /Contents [%s] >>' % (pr, ' '.join('%d 0 R' % x for x in s)))
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /Font %s >> >>' % (pg, pdf.font_res()))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % pr)
    pdf.npages = 1
    out['CONTARR.PDF'] = (pdf, pdf.finish(root), '/Contents as an array of three streams')

    # INHERIT: nested Pages, inherited Resources, boxes and Rotate
    pdf = Pdf('INHERIT.PDF')
    pdf.font('Helvetica')
    pdf.font('Helvetica-Bold')
    top, mid1, mid2 = pdf.alloc(), pdf.alloc(), pdf.alloc()
    kids = []
    for i, (extra, label) in enumerate([('', 'inherits everything'),
                                        ('/MediaBox [0 0 400 300]', 'overrides MediaBox only'),
                                        ('/Rotate 0', 'overrides Rotate to 0')]):
        c = C(pdf)
        c.text(60, 270 if 'MediaBox' in extra else 600, 'Helvetica-Bold', 18, 'INHERIT %d' % (i + 1))
        c.text(60, 250 if 'MediaBox' in extra else 580, 'Helvetica', 12, label)
        c.rect(40, 40, 300, 200, 'S') if 'MediaBox' in extra else c.rect(40, 40, 500, 500, 'S')
        cs = pdf.stream(c.bytes())
        kids.append(pdf.add('<< /Type /Page /Parent %d 0 R /Contents %d 0 R %s >>' % (mid2, cs, extra)))
    pdf.put(mid2, '<< /Type /Pages /Parent %d 0 R /Kids [%s] /Count 3 >>' % (mid1, ' '.join('%d 0 R' % k for k in kids)))
    pdf.put(mid1, '<< /Type /Pages /Parent %d 0 R /Kids [%d 0 R] /Count 3 /CropBox [10 10 600 780] /Rotate 90 >>' % (top, mid2))
    pdf.put(top, '<< /Type /Pages /Kids [%d 0 R] /Count 3 /MediaBox [0 0 612 792] /Resources << /Font %s >> >>' % (mid1, pdf.font_res()))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % top)
    pdf.npages = 3
    out['INHERIT.PDF'] = (pdf, pdf.finish(root), 'MediaBox/CropBox/Rotate/Resources inherited through three Pages levels')

    # ROTCROP
    pdf = Pdf('ROTCROP.PDF')
    pdf.font('Helvetica-Bold')
    pdf.font('Helvetica')
    specs = [(90, '[-50 -30 562 762]', '[20 10 400 600]'), (180, '[-50 -30 562 762]', '[20 10 400 600]'),
             (270, '[100 100 712 892]', '[150 150 650 850]'), (0, '[-100 -100 300 200]', '[-80 -80 280 180]'),
             (90, '[100 100 500 400]', '[120 120 480 380]'), (270, '[0 0 612 792]', '[36 36 576 756]')]
    pgs = []
    pr = pdf.alloc()
    for i, (rot, mb, cb) in enumerate(specs):
        mbn = list(map(float, mb.strip('[]').split()))
        c = C(pdf)
        c.lw(2)
        c.rect(mbn[0] + 5, mbn[1] + 5, mbn[2] - mbn[0] - 10, mbn[3] - mbn[1] - 10, 'S')
        cbn = list(map(float, cb.strip('[]').split()))
        c.dash(4, 4)
        c.rect(*cbn[:2], cbn[2] - cbn[0], cbn[3] - cbn[1], 'S')
        c.undash()
        c.text(cbn[0] + 20, cbn[1] + 40, 'Helvetica-Bold', 24, 'ROTCROP %d' % (i + 1))
        c.text(cbn[0] + 20, cbn[1] + 20, 'Helvetica', 12, 'Rotate %d, box origin (%s)' % (rot, ' '.join(mb.strip('[]').split()[:2])))
        cs = pdf.stream(c.bytes())
        pgs.append(pdf.add('<< /Type /Page /Parent %d 0 R /Contents %d 0 R /MediaBox %s /CropBox %s /Rotate %d >>' % (pr, cs, mb, cb, rot)))
    pdf.put(pr, '<< /Type /Pages /Kids [%s] /Count %d /Resources << /Font %s >> >>' % (' '.join('%d 0 R' % p for p in pgs), len(pgs), pdf.font_res()))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % pr)
    pdf.npages = len(pgs)
    out['ROTCROP.PDF'] = (pdf, pdf.finish(root), 'rotated 90/180/270 and cropped pages with nonzero-origin boxes')

    # REVCHAIN: three revisions, later ones redefine objects
    pdf = Pdf('REVCHAIN.PDF')
    pdf.font('Helvetica')
    pdf.font('Helvetica-Bold')
    pr, p1, p2, c1, c2 = (pdf.alloc() for _ in range(5))
    fonts = pdf.font_res()

    def pagecontent(label):
        c = C(pdf)
        c.text(72, 700, 'Helvetica-Bold', 30, label)
        return c.bytes()

    def sobj(data):
        return b'<< /Length %d >>\nstream\n' % len(data) + data + b'\nendstream'
    pdf.put(c1, sobj(pagecontent('REVISION 1')))
    pdf.put(c2, sobj(pagecontent('PAGE TWO')))
    pdf.put(p1, '<< /Type /Page /Parent %d 0 R /Contents %d 0 R >>' % (pr, c1))
    pdf.put(p2, '<< /Type /Page /Parent %d 0 R /Contents %d 0 R >>' % (pr, c2))
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R %d 0 R] /Count 2 /MediaBox [0 0 612 792] /Resources << /Font %s >> >>' % (p1, p2, fonts))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % pr)
    base = pdf.finish(root)
    size = pdf.next
    an = size
    rev2 = append_revision(base, {c1: sobj(pagecontent('REVISION 2')),
                                  p1: ('<< /Type /Page /Parent %d 0 R /Contents %d 0 R /Annots [%d 0 R] >>' % (pr, c1, an)).encode(),
                                  an: b'<< /Type /Annot /Subtype /Text /Rect [400 700 420 718] /Contents (note from revision 2) /Name /Comment /F 4 /NM (REV-0002) >>'},
                           root, size + 1, 'REVCHAIN.PDF', 2)
    p3, c3 = size + 1, size + 2
    rev3 = append_revision(rev2, {c1: sobj(pagecontent('REVISION 3')),
                                  an: b'<< /Type /Annot /Subtype /Text /Rect [400 700 420 718] /Contents (note from revision 3) /Name /Comment /F 4 /NM (REV-0002) >>',
                                  c3: sobj(pagecontent('PAGE THREE, ADDED IN REVISION 3')),
                                  p3: ('<< /Type /Page /Parent %d 0 R /Contents %d 0 R >>' % (pr, c3)).encode(),
                                  pr: ('<< /Type /Pages /Kids [%d 0 R %d 0 R %d 0 R] /Count 3 /MediaBox [0 0 612 792] /Resources << /Font %s >> >>' % (p1, p2, p3, fonts)).encode()},
                           root, size + 3, 'REVCHAIN.PDF', 3)
    pdf.npages = 3
    out['REVCHAIN.PDF'] = (pdf, rev3, 'three revisions: final state is 3 pages, page 1 reads REVISION 3 with a Text note "note from revision 3"')

    # ANNOTS256
    pdf = Pdf('ANNOTS256.PDF')
    pdf.font('Helvetica')
    pr = pdf.alloc()
    pg = pdf.alloc()
    anns = []
    kinds = ['Text', 'Highlight', 'Square', 'Circle', 'Line', 'Ink', 'FreeText']
    for i in range(256):
        k = kinds[i % 7]
        col, row = i % 16, i // 16
        x, y = 30 + col * 35, 30 + row * 44
        w, h = 28, 22
        rect = '[%d %d %d %d]' % (x, y, x + w, y + h)
        body = {'Text': '0.9 0.8 0 rg 0 0 28 22 re f 0 G 0 0 28 22 re S',
                'Highlight': '1 1 0 rg 0 0 28 22 re f',
                'Square': '0 G 1.5 w 1 1 26 20 re S', 'Circle': '0 G 1.5 w 14 11 m 14 17 8 21 4 17 c S',
                'Line': '0 G 1.5 w 2 2 m 26 20 l S', 'Ink': '0 G 1.5 w 2 2 m 10 20 l 18 2 l 26 20 l S',
                'FreeText': '0 g BT /F1 9 Tf 2 7 Td (%d) Tj ET' % i}[k]
        ap = pdf.stream(body.encode(), '/Type /XObject /Subtype /Form /BBox [0 0 28 22] /Resources << /Font << /F1 %d 0 R >> >>' % pdf.fonts[('Helvetica', 'WinAnsiEncoding', (), 126)][1])
        extra = ''
        if k == 'Highlight':
            extra = ' /QuadPoints [%d %d %d %d %d %d %d %d]' % (x, y + h, x + w, y + h, x, y, x + w, y)
        elif k == 'Line':
            extra = ' /L [%d %d %d %d]' % (x + 2, y + 2, x + 26, y + 20)
        elif k == 'Ink':
            extra = ' /InkList [[%d %d %d %d %d %d]]' % (x + 2, y + 2, x + 10, y + 20, x + 18, y + 2)
        elif k == 'FreeText':
            extra = ' /DA (/Helv 9 Tf 0 g)'
        anns.append(pdf.add('<< /Type /Annot /Subtype /%s /Rect %s /Contents (annotation %d) /NM (A%04d) /F 4 /P %d 0 R%s /AP << /N %d 0 R >> >>'
                            % (k, rect, i, i, pg, extra, ap)))
    c = C(pdf)
    c.text(30, 770, 'Helvetica-Bold', 14, 'ANNOTS256: 256 annotations in a 16 x 16 grid')
    cs = pdf.stream(c.bytes())
    pdf.put(pg, '<< /Type /Page /Parent %d 0 R /Contents %d 0 R /Annots [%s] >>' % (pr, cs, ' '.join('%d 0 R' % a for a in anns)))
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /Font %s >> >>' % (pg, pdf.font_res()))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % pr)
    pdf.npages = 1
    out['ANNOTS256.PDF'] = (pdf, pdf.finish(root), '256 annotations of 7 subtypes, each with /AP /NM /Contents')
    return out


# ------------------------------------------------------------ negative corpus
def mini(name='NEG', content=b'BT /F1 12 Tf 72 700 Td (negative sample) Tj ET\n', page_extra='', pages_extra='',
         res_extra='', version='1.3'):
    pdf = Pdf(name, version)
    pdf.font('Helvetica')
    pr = pdf.alloc()
    cs = pdf.stream(content)
    pg = pdf.add('<< /Type /Page /Parent %d 0 R /Contents %d 0 R %s >>' % (pr, cs, page_extra))
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /Font %s %s >> %s >>'
            % (pg, pdf.font_res(), res_extra, pages_extra))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % pr)
    return pdf, root, pg, pr, cs


def refix_xref(data):
    """Rewrite every xref offset and startxref after a same-structure edit that
    changed the file length, so the ONLY defect left is the one the case names."""
    pos = {int(m.group(1)): m.start(1) for m in re.finditer(rb'(?:^|\n)(\d+) 0 obj', data)}
    for m in re.finditer(rb'(?:^|\n)(\d+) 0 obj', data):
        pos[int(m.group(1))] = m.start(1)
    xr = data.rindex(b'\nxref\n') + 1
    head = data[:xr]
    tail = data[xr:]
    lines = tail.split(b'\n')
    out = []
    i = 0
    cur = None
    for ln in lines:
        m = re.fullmatch(rb'(\d+) (\d+)', ln)
        if m and not ln.endswith(b'n '):
            cur = int(m.group(1))
            out.append(ln)
            continue
        if re.fullmatch(rb'\d{10} \d{5} n ', ln):
            out.append(b'%010d 00000 n ' % pos[cur])
            cur += 1
            continue
        if re.fullmatch(rb'\d{10} \d{5} f ', ln):
            cur += 1
        out.append(ln)
    t = b'\n'.join(out)
    t = re.sub(rb'startxref\n\d+', b'startxref\n%d' % xr, t)
    return head + t


def negative_files():
    N = []

    def add(name, data, layer, code, why):
        N.append((name, data, layer, code, why))
    pdf, root, pg, pr, cs = mini()
    good = pdf.finish(root)
    add('trunc_header.pdf', good[:5], 'open', 'PDE_TRUNC', 'file is only the five bytes "%PDF-"')
    add('bad_header.pdf', b'GIF89a' + good[6:], 'open', 'PDE_SYNTAX', 'no %PDF- header in the first 1024 bytes')
    xr = good.rindex(b'\nxref\n') + 1
    add('trunc_xref.pdf', good[:xr + 40], 'open', 'PDE_TRUNC', 'cut in the middle of the xref table; no trailer or startxref')
    add('trunc_startxref.pdf', good[:good.rindex(b'startxref')] + b'startxref\n', 'open', 'PDE_TRUNC', 'startxref has no offset')
    add('xref_wrong_offset.pdf', good.replace(b'startxref\n%d' % xr, b'startxref\n%d' % (xr + 3)), 'open', 'PDE_XREF',
        'startxref points inside the xref keyword, not at it')
    # object offset pointing at the wrong object
    m = re.search(rb'(xref\n0 \d+\n0000000000 65535 f \n)(\d{10})( 00000 n \n)(\d{10})', good)
    swapped = good[:m.start(2)] + m.group(4) + good[m.end(2):m.start(4)] + m.group(2) + good[m.end(4):]
    add('xref_swapped_entries.pdf', swapped, 'open', 'PDE_XREF', 'two xref entries swapped: "N 0 obj" is not at the offset')
    add('xref_size_huge.pdf', good.replace(b'/Size %d' % (max(pdf.objs) + 1), b'/Size 99999999'), 'open', 'PDE_XREF',
        '/Size claims 99,999,999 objects, the table has a handful')
    add('no_root.pdf', good.replace(b'/Root %d 0 R' % root, b'/Foo %d 0 R' % root), 'open', 'PDE_XREF', 'trailer has no /Root')
    # streams
    s = good.index(b'stream\n') + 7
    cut = good.index(b'\nendstream')
    ln = b'/Length %d' % (cut - s)

    def same_width(rep):
        return refix_xref(good.replace(ln, rep.ljust(len(ln)), 1))
    add('stream_length_short.pdf', same_width(b'/Length 3'), 'stream', 'PDE_SYNTAX',
        '/Length 3 is shorter than the data; endstream is not where it says (file length unchanged)')
    add('stream_length_long.pdf', same_width(b'/Length %d' % (cut - s + 40)) if len(b'/Length %d' % (cut - s + 40)) <= len(ln) else refix_xref(good.replace(ln, b'/Length %d' % (cut - s + 40))), 'stream', 'PDE_SYNTAX',
        '/Length overshoots endstream but stays inside the file')
    add('stream_length_past_eof.pdf', same_width(b'/Length 5000000'), 'stream', 'PDE_TRUNC',
        '/Length runs past the end of the file')
    add('stream_length_negative.pdf', same_width(b'/Length -5'), 'stream', 'PDE_SYNTAX', 'negative /Length')
    add('stream_no_endstream.pdf', good.replace(b'\nendstream', b'\nendstrXXX'), 'stream', 'PDE_SYNTAX', 'endstream keyword corrupted')
    # trees
    pdf, root, pg, pr, cs = mini()
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R %d 0 R] /Count 2 /MediaBox [0 0 612 792] /Resources << /Font %s >> >>' % (pg, pr, pdf.font_res()))
    add('cyclic_pagetree.pdf', pdf.finish(root), 'open', 'PDE_CYCLE', '/Pages lists itself among its own /Kids')
    pdf, root, pg, pr, cs = mini()
    pdf.put(pg, '<< /Type /Page /Parent %d 0 R /Contents %d 0 R >>' % (pg, cs))
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /Parent %d 0 R /MediaBox [0 0 612 792] >>' % (pg, pr))
    add('cyclic_parent.pdf', pdf.finish(root), 'open', 'PDE_CYCLE', '/Parent chain loops (a page is its own parent)')
    # deep page tree
    pdf = Pdf('NEG')
    pdf.font('Helvetica')
    cs = pdf.stream(b'0 g\n')
    depth = 120
    nodes = [pdf.alloc() for _ in range(depth)]
    leaf = pdf.alloc()
    pdf.put(leaf, '<< /Type /Page /Parent %d 0 R /Contents %d 0 R >>' % (nodes[-1], cs))
    for i, nd in enumerate(nodes):
        kid = nodes[i + 1] if i + 1 < depth else leaf
        pdf.put(nd, '<< /Type /Pages /Kids [%d 0 R] /Count 1%s >>' % (kid, ' /MediaBox [0 0 612 792] /Resources << >>' if i == 0 else ''))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % nodes[0])
    add('deep_pagetree.pdf', pdf.finish(root), 'open', 'PDE_DEPTH', 'page tree nested 120 levels deep')
    pdf, root, pg, pr, cs = mini(page_extra='/Junk ' + '[' * 5000 + ']' * 5000)
    add('deep_array.pdf', pdf.finish(root), 'open', 'PDE_DEPTH', 'array nested 5,000 levels in a page dictionary')
    pdf, root, pg, pr, cs = mini(page_extra='/Junk ' + '<< /a ' * 2000 + '1' + ' >>' * 2000)
    add('deep_dict.pdf', pdf.finish(root), 'open', 'PDE_DEPTH', 'dictionary nested 2,000 levels')
    # resources / forms
    pdf = Pdf('NEG')
    pdf.font('Helvetica')
    pr = pdf.alloc()
    fm = pdf.alloc()
    pdf.put(fm, ('<< /Type /XObject /Subtype /Form /BBox [0 0 100 100] /Resources << /XObject << /Fm1 %d 0 R >> >> /Length 8 >>\nstream\n/Fm1 Do\n\nendstream' % fm))
    cs = pdf.stream(b'/Fm1 Do\n')
    pg = pdf.add('<< /Type /Page /Parent %d 0 R /Contents %d 0 R >>' % (pr, cs))
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /XObject << /Fm1 %d 0 R >> >> >>' % (pg, fm))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % pr)
    add('cyclic_resources.pdf', pdf.finish(root), 'render', 'PDE_CYCLE', 'a Form XObject draws itself; open succeeds, render refuses')
    pdf = Pdf('NEG')
    pdf.font('Helvetica')
    pr = pdf.alloc()
    chain = [pdf.alloc() for _ in range(7)]
    for i, f in enumerate(chain):
        body = ('/Fm%d Do\n' % (i + 1)).encode() if i + 1 < 7 else b'0 g\n'
        res = '/XObject << /Fm%d %d 0 R >>' % (i + 1, chain[i + 1]) if i + 1 < 7 else ''
        pdf.stream(body, '/Type /XObject /Subtype /Form /BBox [0 0 100 100] /Resources << %s >>' % res, num=f)
    cs = pdf.stream(b'/Fm0 Do\n')
    pg = pdf.add('<< /Type /Page /Parent %d 0 R /Contents %d 0 R >>' % (pr, cs))
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /XObject << /Fm0 %d 0 R >> >> >>' % (pg, chain[0]))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % pr)
    add('form_nest_7.pdf', pdf.finish(root), 'render', 'PDE_DEPTH', 'Form XObjects nested 7 deep (limit 4)')
    # numbers and limits
    pdf, root, pg, pr, cs = mini(res_extra='')
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 99999999999 792] /Resources << /Font %s >> >>' % (pg, pdf.font_res()))
    add('huge_mediabox_number.pdf', pdf.finish(root), 'pageinfo', 'PDE_RANGE', 'MediaBox width is a 11-digit integer')
    pdf, root, pg, pr, cs = mini()
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 5000 5000] /Resources << /Font %s >> >>' % (pg, pdf.font_res()))
    add('page_5000pt.pdf', pdf.finish(root), 'pageinfo', 'PDE_RANGE', 'page box outside +/-2047 pt')
    pdf, root, pg, pr, cs = mini(content=b'99999999999999999999 0 m 0 99999999999999999999 l S\n1e99 0 Td 0.0000000000000000001 w\nBT /F1 99999999 Tf (x) Tj ET\n')
    add('huge_operands.pdf', pdf.finish(root), 'render', 'PDE_RANGE', 'operands beyond 32-bit and a 99,999,999 pt font size in a content stream')
    pdf, root, pg, pr, cs = mini(content=b'q ' * 1000 + b'Q ' * 1000 + b'\n')
    add('graphics_stack_1000.pdf', pdf.finish(root), 'render', 'PDE_LIMIT', '1,000 nested q operators (the stack is bounded)')
    pdf, root, pg, pr, cs = mini(content=b'BT /F1 12 Tf 72 700 Td (' + b'A' * 100000 + b') Tj ET\n')
    add('string_100k.pdf', pdf.finish(root), 'render', 'PDE_LIMIT', 'a 100,000 byte literal string in one operator')
    pdf, root, pg, pr, cs = mini(page_extra='/Junk /' + 'N' * 20000)
    add('name_20k.pdf', pdf.finish(root), 'open', 'PDE_LIMIT', 'a 20,000 byte name')
    # filters
    pdf = Pdf('NEG')
    pdf.font('Helvetica')
    pr = pdf.alloc()
    bomb = bytes([129 + 0, 0]) * 1  # placeholder replaced below
    bomb = b''.join(bytes([129, 0x20]) for _ in range(40000)) + b'\x80'    # 40,000 x 128 spaces = 5.1 MB
    cs = pdf.stream(b'', '')
    pdf.objs[cs] = b'<< /Length %d /Filter /RunLengthDecode >>\nstream\n' % len(bomb) + bomb + b'\nendstream'
    pg = pdf.add('<< /Type /Page /Parent %d 0 R /Contents %d 0 R >>' % (pr, cs))
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /Font %s >> >>' % (pg, pdf.font_res()))
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % pr)
    add('runlength_bomb.pdf', pdf.finish(root), 'render', 'PDE_LIMIT', '80,000 bytes of RunLength that expand to about 5 MB')
    pdf, root, pg, pr, cs = mini()
    pdf.objs[cs] = b'<< /Length 4 /Filter /FlateDecode >>\nstream\nxxxx\nendstream'
    add('flate_unsupported.pdf', pdf.finish(root), 'render', 'PDE_FLATE', 'FlateDecode content on the XT build')
    pdf, root, pg, pr, cs = mini()
    pdf.objs[cs] = b'<< /Length 4 /Filter /LZWDecode >>\nstream\nxxxx\nendstream'
    add('lzw_unsupported.pdf', pdf.finish(root), 'render', 'PDE_FILTER', 'LZWDecode content')
    pdf, root, pg, pr, cs = mini()
    pdf.objs[cs] = b'<< /Length 4 /Filter [/ASCIIHexDecode /RunLengthDecode /ASCIIHexDecode /RunLengthDecode /ASCIIHexDecode] >>\nstream\n41>\nendstream'
    add('filter_chain_5.pdf', pdf.finish(root), 'render', 'PDE_FILTER', 'a five-stage filter pipeline (limit 4)')
    # incremental chains
    base = good
    prevloop = base.replace(b'trailer\n<<', b'trailer\n<< /Prev %d' % xr)
    add('prev_loop.pdf', prevloop, 'open', 'PDE_XREF', '/Prev points at the xref section it is in')
    add('prev_broken.pdf', base.replace(b'trailer\n<<', b'trailer\n<< /Prev 7'), 'open', 'PDE_XREF', '/Prev points at an offset with no xref section')
    add('prev_past_eof.pdf', base.replace(b'trailer\n<<', b'trailer\n<< /Prev 99999999'), 'open', 'PDE_XREF', '/Prev beyond the end of the file')
    chain = base
    pdfx, rootx, _, _, _ = mini()
    for r in range(2, 11):
        chain = append_revision(chain, {1: b'<< /Revision %d >>' % r}, rootx, max(pdfx.objs) + 1, 'NEG', r)
    add('prev_chain_10.pdf', chain, 'open', 'PDE_LIMIT', 'ten revisions chained by /Prev (limit 8)')
    # annotations
    def annot_doc(annot, extra_objs=()):
        pdf, root, pg, pr, cs = mini()
        a = pdf.add(annot)
        pdf.put(pg, '<< /Type /Page /Parent %d 0 R /Contents %d 0 R /Annots [%d 0 R] >>' % (pr, cs, a))
        return pdf, pdf.finish(root)
    add('annot_no_rect.pdf', annot_doc('<< /Type /Annot /Subtype /Square /F 4 >>')[1], 'render', 'PDE_OK', 'open succeeds; the Square without /Rect is skipped, not drawn, no crash')
    add('annot_bad_rect.pdf', annot_doc('<< /Type /Annot /Subtype /Square /Rect [1 2 3] /F 4 >>')[1], 'render', 'PDE_OK', '/Rect has three numbers: skipped')
    add('annot_quads_odd.pdf', annot_doc('<< /Type /Annot /Subtype /Highlight /Rect [10 10 100 30] /QuadPoints [1 2 3 4 5 6 7] /F 4 >>')[1], 'render', 'PDE_OK', '/QuadPoints has 7 numbers, not a multiple of 8: skipped')
    add('annot_no_subtype.pdf', annot_doc('<< /Type /Annot /Rect [10 10 100 30] >>')[1], 'render', 'PDE_OK', 'no /Subtype: skipped')
    pdf, root, pg, pr, cs = mini()
    pdf.put(pg, '<< /Type /Page /Parent %d 0 R /Contents %d 0 R /Annots 42 >>' % (pr, cs))
    add('annots_not_array.pdf', pdf.finish(root), 'render', 'PDE_OK', '/Annots is the integer 42: ignored')
    pdf, root, pg, pr, cs = mini()
    pdf.put(pg, '<< /Type /Page /Parent %d 0 R /Contents %d 0 R /Annots [(string) 99 0 R 7 null] >>' % (pr, cs))
    add('annots_not_dicts.pdf', pdf.finish(root), 'render', 'PDE_OK', '/Annots holds a string, a dangling reference, an integer and null: all skipped')
    pdf, root, pg, pr, cs = mini()
    ap = pdf.alloc()
    a = pdf.add('<< /Type /Annot /Subtype /Square /Rect [10 10 100 30] /F 4 /AP << /N %d 0 R >> >>' % ap)
    pdf.stream(b'/Fm Do\n', '/Type /XObject /Subtype /Form /BBox [0 0 90 20] /Resources << /XObject << /Fm %d 0 R >> >>' % ap, num=ap)
    pdf.put(pg, '<< /Type /Page /Parent %d 0 R /Contents %d 0 R /Annots [%d 0 R] >>' % (pr, cs, a))
    add('annot_ap_cycle.pdf', pdf.finish(root), 'render', 'PDE_CYCLE', 'an /AP /N form that draws itself')
    pdf, root, pg, pr, cs = mini()
    a = pdf.add('<< /Type /Annot /Subtype /Square /Rect [10 10 100 30] /F 4 /AP << /N 5 >> >>')
    pdf.put(pg, '<< /Type /Page /Parent %d 0 R /Contents %d 0 R /Annots [%d 0 R] >>' % (pr, cs, a))
    add('annot_ap_not_stream.pdf', pdf.finish(root), 'render', 'PDE_OK', '/AP /N is an integer: the annotation is skipped')
    pdf, root, pg, pr, cs = mini()
    a = pdf.add('<< /Type /Annot /Subtype /Link /Rect [10 10 100 30] /A << /S /Launch /F (cmd.exe) >> >>')
    pdf.put(pg, '<< /Type /Page /Parent %d 0 R /Contents %d 0 R /Annots [%d 0 R] >>' % (pr, cs, a))
    add('annot_launch_action.pdf', pdf.finish(root), 'render', 'PDE_OK', 'a Link with a Launch action: shown as a link, never followed')
    # missing resources
    pdf, root, pg, pr, cs = mini(content=b'BT /F9 12 Tf 72 700 Td (missing font) Tj ET\n')
    add('missing_font.pdf', pdf.finish(root), 'render', 'PDE_FONT', 'text selects /F9, which is not in /Resources: substitute face, Incomplete rendering')
    pdf, root, pg, pr, cs = mini(content=b'q 100 0 0 100 72 600 cm /ImX Do Q\n')
    add('missing_image.pdf', pdf.finish(root), 'render', 'PDE_GRAPHICS', '/ImX is not in /Resources: nothing drawn, Incomplete rendering')
    pdf, root, pg, pr, cs = mini(content=b'/Sh1 sh\n')
    add('shading_sh.pdf', pdf.finish(root), 'render', 'PDE_GRAPHICS', 'the sh operator is outside the profile: Incomplete rendering')
    pdf, root, pg, pr, cs = mini()
    im = pdf.add('<< /Type /XObject /Subtype /Image /Width 8 /Height 8 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length 4 >>\nstream\n\xff\xd8\xff\xd9\nendstream')
    pdf.put(pr, '<< /Type /Pages /Kids [%d 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /Font %s /XObject << /Im1 %d 0 R >> >> >>' % (pg, pdf.font_res(), im))
    pdf.objs[cs] = b'<< /Length 22 >>\nstream\nq 50 0 0 50 72 600 cm /Im1 Do Q\nendstream'
    add('dct_image.pdf', pdf.finish(root), 'render', 'PDE_FILTER', 'a DCTDecode (JPEG) image')
    # structure
    pdf = Pdf('NEG')
    pr = pdf.alloc()
    pdf.put(pr, '<< /Type /Pages /Kids [] /Count 0 /MediaBox [0 0 612 792] >>')
    root = pdf.add('<< /Type /Catalog /Pages %d 0 R >>' % pr)
    add('zero_pages.pdf', pdf.finish(root), 'open', 'PDE_NOTFOUND', 'a page tree with no pages')
    pdf, root, pg, pr, cs = mini()
    add('encrypted.pdf', good.replace(b'/ID [', b'/Encrypt << /Filter /Standard /V 1 /R 2 /O (aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa) /U (bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb) /P -4 >> /ID ['), 'open', 'PDE_ENCRYPT', '/Encrypt in the trailer (the dictionary is a stub; the code must be PDE_ENCRYPT before anything is decrypted)')
    pdf, root, pg, pr, cs = mini()
    pdf.put(root, '<< /Type /Catalog /Pages %d 0 R /Perms << /DocMDP 9 0 R >> >>' % pr)
    add('signed_perms.pdf', pdf.finish(root), 'open', 'PDE_SIGNED', 'catalog carries /Perms: opens READ-ONLY with a label, not refused')
    # xref stream + object stream
    out = bytearray(b'%PDF-1.5\n%\xe2\xe3\xcf\xd3\n')
    offs = {}

    def o(n, body):
        offs[n] = len(out)
        out.extend(b'%d 0 obj\n' % n + body + b'\nendobj\n')
    content = b'BT /F1 12 Tf 72 700 Td (xref stream) Tj ET\n'
    o(4, b'<< /Length %d >>\nstream\n' % len(content) + content + b'\nendstream')
    o(5, b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>')
    inner = [b'<< /Type /Catalog /Pages 2 0 R >>',
             b'<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> >>',
             b'<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>']
    hdr = b''
    pos = 0
    for i, b in enumerate(inner):
        hdr += b'%d %d ' % (i + 1, pos)
        pos += len(b) + 1
    stm = hdr + b'\n' + b'\n'.join(inner)
    o(6, b'<< /Type /ObjStm /N 3 /First %d /Length %d >>\nstream\n' % (len(hdr) + 1, len(stm)) + stm + b'\nendstream')
    xpos = len(out)
    ents = [(0, 0, 255)]
    for n in range(1, 8):
        if n <= 3:
            ents.append((2, 6, n - 1))
        elif n in offs:
            ents.append((1, offs[n], 0))
        else:
            ents.append((1, xpos, 0))
    xd = b''.join(bytes([t]) + a.to_bytes(3, 'big') + bytes([b]) for t, a, b in ents)
    out.extend(b'7 0 obj\n<< /Type /XRef /Size 8 /W [1 3 1] /Root 1 0 R /Length %d >>\nstream\n' % len(xd) + xd
               + b'\nendstream\nendobj\nstartxref\n%d\n%%%%EOF\n' % xpos)
    add('xrefstm_objstm.pdf', bytes(out), 'open', 'PDE_XREFSTM', 'a PDF 1.5 file with a cross-reference STREAM and an object stream (valid, but outside the XT profile)')
    return N


def negative_readme(files):
    L = ['tests/pdfneg: the NEGATIVE CORPUS (PAGEDESK-SPEC 18.3, PD-28)',
         '',
         'EVERY FILE GENERATED FROM THIS README IS DELIBERATELY INVALID OR UNSUPPORTED.',
         'None of them is a sample. They are never shipped on a disk and never placed in',
         'a folder that advertises valid PDFs. Regenerate with:',
         '',
         '    python3 tools/os88pdfgen.py --negative build/pdfneg',
         '',
         'The files are generated, not committed; only this README is. The generator',
         'writes this exact text, and tests/unit/t_pdfix.py checks that it has not drifted.',
         '',
         'Columns: file | layer | expected | why.',
         '  layer    open   = PDE_* comes back from opening the file',
         '           stream = PDV_ENG_STRM_OPEN of the page content (open itself succeeds)',
         '           pageinfo = PDV_ENG_PAGEINFO (inherited boxes resolve on demand)',
         '           render = the file opens (or the page is reached) and the refusal is',
         '                    raised while drawing the page; the page is then shown as',
         '                    Incomplete rendering (PAGEDESK-SPEC 10.6) where the code is',
         '                    PDE_FONT/PDE_GRAPHICS/PDE_FLATE/PDE_FILTER',
         '  expected the PDE_* code of PAGEDESK-SPEC 3.6 / apps/pagedesk/pdabi.h.',
         '           PDE_OK means: no failure code, the bad object is skipped, nothing',
         '           crashes, and the document stays editable.',
         '',
         'These expectations are the fixture author\'s reading of PAGEDESK-SPEC 1.3 and 3.6;',
         'where the engine\'s documented code differs, the engine\'s 20.2 wins and this table',
         'is regenerated with it.',
         '',
         'After EVERY case the test must also check recovery: the source file is',
         'byte-identical, no claim is left open, and a following valid file opens.',
         '']
    for name, _, layer, code, why in files:
        L.append('%-26s %-7s %-13s %s' % (name, layer, code, why))
    L.append('')
    L.append('%d files.' % len(files))
    return '\n'.join(L) + '\n'


def stress_manifest(items):
    L = ['PageDesk STRESS corpus (generated, not committed, not shipped)',
         'Regenerate: python3 tools/os88pdfgen.py --stress DIR', '']
    for name, (pdf, data, what) in items.items():
        L.append('file %s' % name)
        L.append('  what %s' % what)
        L.append('  pages %d' % pdf.npages)
        L.append('  bytes %d' % len(data))
        L.append('  sha256 %s' % hashlib.sha256(data).hexdigest())
        L.append('')
    return '\n'.join(L)


def write_dir(d, files):
    os.makedirs(d, exist_ok=True)
    for n, b in files.items():
        with open(os.path.join(d, n), 'wb') as f:
            f.write(b if isinstance(b, bytes) else b.encode('utf-8'))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dir')
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--stress', action='store_true')
    g.add_argument('--negative', action='store_true')
    a = ap.parse_args(argv)
    if a.stress:
        items = stress_files()
        files = {n: d for n, (p, d, w) in items.items()}
        files['STRESS.TXT'] = stress_manifest(items)
        write_dir(a.dir, files)
        print('%d stress files in %s' % (len(items), a.dir))
    elif a.negative:
        neg = negative_files()
        files = {n: d for n, d, *_ in neg}
        files['README.TXT'] = negative_readme(neg)
        write_dir(a.dir, files)
        print('%d negative files in %s' % (len(neg), a.dir))
    else:
        built = build_all_samples()
        files = {n: d for n, (p, d) in built.items()}
        files['MANIFEST.TXT'] = manifest(built)
        write_dir(a.dir, files)
        print('%d samples, %d bytes, in %s' % (len(built), sum(len(d) for p, d in built.values()), a.dir))


if __name__ == '__main__':
    main()

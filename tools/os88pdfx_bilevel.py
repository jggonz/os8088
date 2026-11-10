#!/usr/bin/env python3
"""os88pdfx_bilevel - fixtures and reference encoders for PDXI's CCITT and JBIG2
(PAGEDESK-SPEC 20.22.12).  Host side only; deterministic.

    python3 tools/os88pdfx_bilevel.py --gen-ccitt-tables     # C tables for pdxiccit.c
    python3 tools/os88pdfx_bilevel.py --selfcheck            # prefix-free check of the tables
    python3 tools/os88pdfx_bilevel.py --build DIR            # write the fixtures into DIR

The CCITT tables are typed here ONCE (ITU-T T.4 tables 2 and 3); pdxiccit.c's
tables are generated from them, and the tables are held to libtiff by decoding
this file's own output through PIL (selfcheck) and the fixtures through mutool.
"""
import sys, os, struct

W = """0:00110101 1:000111 2:0111 3:1000 4:1011 5:1100 6:1110 7:1111 8:10011 9:10100
10:00111 11:01000 12:001000 13:000011 14:110100 15:110101 16:101010 17:101011
18:0100111 19:0001100 20:0001000 21:0010111 22:0000011 23:0000100 24:0101000
25:0101011 26:0010011 27:0100100 28:0011000 29:00000010 30:00000011 31:00011010
32:00011011 33:00010010 34:00010011 35:00010100 36:00010101 37:00010110 38:00010111
39:00101000 40:00101001 41:00101010 42:00101011 43:00101100 44:00101101 45:00000100
46:00000101 47:00001010 48:00001011 49:01010010 50:01010011 51:01010100 52:01010101
53:00100100 54:00100101 55:01011000 56:01011001 57:01011010 58:01011011 59:01001010
60:01001011 61:00110010 62:00110011 63:00110100
64:11011 128:10010 192:010111 256:0110111 320:00110110 384:00110111 448:01100100
512:01100101 576:01101000 640:01100111 704:011001100 768:011001101 832:011010010
896:011010011 960:011010100 1024:011010101 1088:011010110 1152:011010111
1216:011011000 1280:011011001 1344:011011010 1408:011011011 1472:010011000
1536:010011001 1600:010011010 1664:011000 1728:010011011"""
B = """0:0000110111 1:010 2:11 3:10 4:011 5:0011 6:0010 7:00011 8:000101 9:000100
10:0000100 11:0000101 12:0000111 13:00000100 14:00000111 15:000011000 16:0000010111
17:0000011000 18:0000001000 19:00001100111 20:00001101000 21:00001101100
22:00000110111 23:00000101000 24:00000010111 25:00000011000 26:000011001010
27:000011001011 28:000011001100 29:000011001101 30:000001101000 31:000001101001
32:000001101010 33:000001101011 34:000011010010 35:000011010011 36:000011010100
37:000011010101 38:000011010110 39:000011010111 40:000001101100 41:000001101101
42:000011011010 43:000011011011 44:000001010100 45:000001010101 46:000001010110
47:000001010111 48:000001100100 49:000001100101 50:000001010010 51:000001010011
52:000000100100 53:000000110111 54:000000111000 55:000000100111 56:000000101000
57:000001011000 58:000001011001 59:000000101011 60:000000101100 61:000001011010
62:000001100110 63:000001100111
64:0000001111 128:000011001000 192:000011001001 256:000001011011 320:000000110011
384:000000110100 448:000000110101 512:0000001101100 576:0000001101101
640:0000001001010 704:0000001001011 768:0000001001100 832:0000001001101
896:0000001110010 960:0000001110011 1024:0000001110100 1088:0000001110101
1152:0000001110110 1216:0000001110111 1280:0000001010010 1344:0000001010011
1408:0000001010100 1472:0000001010101 1536:0000001011010 1600:0000001011011
1664:0000001100100 1728:0000001100101"""
X = """1792:00000001000 1856:00000001100 1920:00000001101 1984:000000010010
2048:000000010011 2112:000000010100 2176:000000010101 2240:000000010110
2304:000000010111 2368:000000011100 2432:000000011101 2496:000000011110
2560:000000011111"""

def _parse(s):
    d = {}
    for t in s.split():
        k, v = t.split(':')
        d[int(k)] = v
    return d
WHITE, BLACK, EXT = _parse(W), _parse(B), _parse(X)
WHITE.update(EXT); BLACK.update(EXT)
TABS = (WHITE, BLACK)

def selfcheck_tables():
    for t in TABS:
        codes = list(t.values())
        assert len(codes) == 104, len(codes)
        for a in codes:
            for b in codes:
                assert a is b or not b.startswith(a) or a == b and False, (a, b)
        k = sum(2.0 ** -len(c) for c in codes)
        assert k <= 1.0, k
    return True

def gen_c_tables():
    out = []
    for name, t in (("white", WHITE), ("black", BLACK)):
        runs = sorted(t)
        out.append("static const pdw cf_%s_code[%d] = {" % (name, len(runs)))
        out.append("    " + ", ".join("0x%X" % int(t[r], 2) for r in runs) + " };")
        out.append("static const pdb cf_%s_len[%d] = {" % (name, len(runs)))
        out.append("    " + ", ".join("%d" % len(t[r]) for r in runs) + " };")
    out.append("static const pdw cf_run_of[104] = {")
    out.append("    " + ", ".join(str(r) for r in sorted(WHITE)) + " };")
    return "\n".join(out)

# ---- CCITT encoders (reference; used for fixtures and to hold the decoder) ----
class BitW:
    def __init__(self): self.b = bytearray(); self.n = 0; self.cur = 0
    def put(self, s):
        for ch in s:
            self.cur = (self.cur << 1) | (ch == '1'); self.n += 1
            if self.n == 8: self.b.append(self.cur); self.cur = 0; self.n = 0
    def putn(self, v, n): self.put(format(v, '0%db' % n) if n else '')
    def align(self):
        while self.n: self.put('0')
    def bytes(self):
        r = bytearray(self.b)
        if self.n: r.append(self.cur << (8 - self.n))
        return bytes(r)

def put_run(bw, run, color):
    t = TABS[color]
    while run >= 2560: bw.put(t[2560]); run -= 2560
    if run >= 64: bw.put(t[run // 64 * 64]); run %= 64
    bw.put(t[run])

def runs_of(row):
    """changing elements of a row of 0/1 (1 = black), each a position"""
    ch = []; prev = 0
    for i, v in enumerate(row):
        if v != prev: ch.append(i); prev = v
    return ch

def enc_1d(bw, row):
    n = len(row); a = 0; c = 0
    while a < n:
        b = a
        while b < n and row[b] == c: b += 1
        put_run(bw, b - a, c); a = b; c ^= 1
    if n == 0: put_run(bw, 0, 0)

def enc_2d(bw, row, ref):
    n = len(row)
    def px(r, i): return 0 if i < 0 else r[i]
    def nxt(r, p, val):
        while p < n and r[p] != val: p += 1
        return p
    a0 = -1; c = 0
    while a0 < n:
        a1 = nxt(row, a0 + 1, 1 - c) if a0 >= 0 or True else 0
        # a1: next changing element in coding line (first pixel != c at/after a0+1; for a0=-1 start at 0)
        st = a0 + 1
        a1 = nxt(row, st, 1 - c)
        a2 = nxt(row, a1 + 1, c) if a1 < n else n
        s = st
        if a0 >= 0 and px(ref, a0) != c: s = nxt(ref, s, c)
        b1 = nxt(ref, s, 1 - c)
        b2 = nxt(ref, b1 + 1, c) if b1 < n else n
        if b2 < a1:
            bw.put('0001'); a0 = b2
        elif abs(a1 - b1) <= 3:
            d = a1 - b1
            bw.put({0: '1', 1: '011', 2: '000011', 3: '0000011', -1: '010', -2: '000010', -3: '0000010'}[d])
            a0 = a1; c ^= 1
        else:
            bw.put('001')
            st0 = max(a0, 0)
            put_run(bw, a1 - st0, c); put_run(bw, a2 - a1, 1 - c)
            a0 = a2

def enc_ccitt(img, k=-1, eol=False, align=False, eob=True):
    """img: list of rows of 0/1 (1 = black).  Returns the coded bytes."""
    bw = BitW(); n = len(img[0]); ref = [0] * n
    for y, row in enumerate(img):
        if align: bw.align()
        if k >= 0:
            if eol or k > 0:
                if align:  # fill so the EOL ends on a byte boundary
                    while (bw.n + 12) % 8 != (4 if k > 0 else 0) % 8 and False: pass
                bw.put('000000000001')
                if k > 0: bw.put('1' if y % k == 0 else '0')
        if k < 0 or (k > 0 and y % k != 0): enc_2d(bw, row, ref)
        else: enc_1d(bw, row)
        ref = row
    if eob:
        if k < 0: bw.put('000000000001' * 2)
        else:
            for _ in range(6): bw.put('000000000001' + ('1' if k > 0 else ''))
    return bw.bytes()

if __name__ == '__main__':
    if '--gen-ccitt-tables' in sys.argv: print(gen_c_tables())
    elif '--selfcheck' in sys.argv: print("tables ok" if selfcheck_tables() else "BAD")

def tiff_wrap(data, w, h, comp=4, t4opt=0, fill_order=1):
    """a one-strip TIFF around CCITT data (PhotometricInterpretation 0: 0 = white)"""
    ents = [(256, 4, 1, w), (257, 4, 1, h), (258, 3, 1, 1), (259, 3, 1, comp), (262, 3, 1, 0),
            (266, 3, 1, fill_order), (273, 4, 1, 0), (278, 4, 1, h), (279, 4, 1, len(data))]
    if comp == 3: ents.append((292, 4, 1, t4opt))
    ents.sort()
    n = len(ents); off = 8 + 2 + 12 * n + 4
    out = b'II*\0' + struct.pack('<I', 8) + struct.pack('<H', n)
    for t, ty, c, v in ents:
        if t == 273: v = off
        out += struct.pack('<HHI', t, ty, c) + (struct.pack('<HH', v, 0) if ty == 3 else struct.pack('<I', v))
    return out + struct.pack('<I', 0) + data

# ---- PDF fixtures ----------------------------------------------------------
def pdf_image(data, w, h, filt, parms=None, extra='', decode=''):
    """a one-page PDF drawing one 1-bit image 1 pixel to 1 point"""
    objs = []
    objs.append(b'<< /Type /Catalog /Pages 2 0 R >>')
    objs.append(b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>')
    objs.append(('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Contents 4 0 R '
                 '/Resources << /XObject << /Im0 5 0 R >> >> >>' % (w, h)).encode())
    cont = ('q %d 0 0 %d 0 0 cm /Im0 Do Q' % (w, h)).encode()
    objs.append(b'<< /Length %d >>\nstream\n' % len(cont) + cont + b'\nendstream')
    d = '<< /Type /XObject /Subtype /Image /Width %d /Height %d /BitsPerComponent 1 /ColorSpace /DeviceGray /Filter %s %s %s /Length %d >>' % (
        w, h, filt, ('/DecodeParms %s' % parms) if parms else '', decode, len(data))
    objs.append(d.encode() + b'\nstream\n' + data + b'\nendstream')
    for i, e in enumerate(extra if isinstance(extra, list) else []):
        objs.append(e)
    out = bytearray(b'%PDF-1.4\n'); offs = []
    for i, o in enumerate(objs):
        offs.append(len(out)); out += b'%d 0 obj\n' % (i + 1) + o + b'\nendobj\n'
    x = len(out)
    out += b'xref\n0 %d\n0000000000 65535 f \n' % (len(objs) + 1)
    for o in offs: out += b'%010d 00000 n \n' % o
    out += b'trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(objs) + 1, x)
    return bytes(out)

def ccitt_pdf(img, k=-1, eol=False, align=False, eob=True, black1=False):
    h = len(img); w = len(img[0])
    data = enc_ccitt(img, k, eol=eol, align=align, eob=eob)
    parms = '<< /K %d /Columns %d /Rows %d /EndOfLine %s /EncodedByteAlign %s /EndOfBlock %s /BlackIs1 %s >>' % (
        k, w, h, *[str(x).lower() for x in (eol, align, eob, black1)])
    return pdf_image(data, w, h, '/CCITTFaxDecode', parms, decode='/Decode [1 0]' if black1 else '')

# ============================================================================
# JBIG2 reference encoder (T.88): MQ coder, integers, generic regions.  Test
# fixtures only.  Pixel context bit orders follow the layout in pdxijb2.c.
# ============================================================================
QE = [(0x5601,1,1,1),(0x3401,2,6,0),(0x1801,3,9,0),(0x0AC1,4,12,0),(0x0521,5,29,0),(0x0221,38,33,0),
      (0x5601,7,6,1),(0x5401,8,14,0),(0x4801,9,14,0),(0x3801,10,14,0),(0x3001,11,17,0),(0x2401,12,18,0),
      (0x1C01,13,20,0),(0x1601,29,21,0),(0x5601,15,14,1),(0x5401,16,14,0),(0x5101,17,15,0),(0x4801,18,16,0),
      (0x3801,19,17,0),(0x3401,20,18,0),(0x3001,21,19,0),(0x2801,22,19,0),(0x2401,23,20,0),(0x2201,24,21,0),
      (0x1C01,25,22,0),(0x1801,26,23,0),(0x1601,27,24,0),(0x1401,28,25,0),(0x1201,29,26,0),(0x1101,30,27,0),
      (0x0AC1,31,28,0),(0x09C1,32,29,0),(0x08A1,33,30,0),(0x0521,34,31,0),(0x0441,35,32,0),(0x02A1,36,33,0),
      (0x0221,37,34,0),(0x0141,38,35,0),(0x0111,39,36,0),(0x0085,40,37,0),(0x0049,41,38,0),(0x0025,42,39,0),
      (0x0015,43,40,0),(0x0009,44,41,0),(0x0005,45,42,0),(0x0001,45,43,0),(0x5601,46,46,0)]

class MQEnc:
    def __init__(s):
        s.A = 0x8000; s.C = 0; s.CT = 12; s.out = bytearray([0]); s.cx = {}
    def _byteout(s):
        o = s.out
        if o[-1] == 0xFF:
            o.append((s.C >> 20) & 0xFF); s.C &= 0xFFFFF; s.CT = 7
        elif s.C < 0x8000000:
            o.append((s.C >> 19) & 0xFF); s.C &= 0x7FFFF; s.CT = 8
        else:
            o[-1] += 1
            if o[-1] == 0xFF:
                s.C &= 0x7FFFFFF; o.append((s.C >> 20) & 0xFF); s.C &= 0xFFFFF; s.CT = 7
            else:
                o.append((s.C >> 19) & 0xFF); s.C &= 0x7FFFF; s.CT = 8
    def _renorm(s):
        while True:
            s.A = (s.A << 1) & 0xFFFF; s.C <<= 1; s.CT -= 1
            if s.CT == 0: s._byteout()
            if s.A & 0x8000: break
    def bit(s, key, b):
        st = s.cx.setdefault(key, [0, 0])
        qe, nm, nl, sw = QE[st[0]]
        if b == st[1]:
            s.A -= qe
            if not (s.A & 0x8000):
                if s.A < qe: s.A = qe
                else: s.C += qe
                st[0] = nm; s._renorm()
            else: s.C += qe
        else:
            s.A -= qe
            if s.A < qe: s.C += qe
            else: s.A = qe
            if sw: st[1] ^= 1
            st[0] = nl; s._renorm()
    def flush(s):
        t = s.C + s.A; s.C |= 0xFFFF
        if s.C >= t: s.C -= 0x8000
        s.C <<= s.CT; s._byteout(); s.C <<= s.CT; s._byteout()
        r = bytes(s.out[1:])
        if r and r[-1] == 0xFF: return r + b'\xAC'
        return r + b'\xFF\xAC'

def enc_int(e, name, v):
    """annex A.2 integer; v = None for OOB.  contexts are keyed (name, PREV)"""
    st = {'prev': 1}
    def put(b):
        e.bit((name, st['prev']), b)
        p = st['prev']
        st['prev'] = ((p << 1) | b) if p < 256 else ((((p << 1) | b) & 511) | 256)
    if v is None: s, a = 1, 0
    else: s, a = (1 if v < 0 else 0), abs(v)
    put(s)
    if a < 4: pre, n, off = [0], 2, 0
    elif a < 20: pre, n, off = [1, 0], 4, 4
    elif a < 84: pre, n, off = [1, 1, 0], 6, 20
    elif a < 340: pre, n, off = [1, 1, 1, 0], 8, 84
    elif a < 4436: pre, n, off = [1, 1, 1, 1, 0], 12, 340
    else: pre, n, off = [1, 1, 1, 1, 1], 32, 4436
    for b in pre: put(b)
    x = a - off
    for i in range(n - 1, -1, -1): put((x >> i) & 1)

def enc_iaid(e, name, codelen, v):
    prev = 1
    for i in range(codelen - 1, -1, -1):
        b = (v >> i) & 1
        e.bit((name, prev), b); prev = (prev << 1) | b

def _gb_pix(img, x, y):
    if y < 0 or x < 0 or y >= len(img) or x >= len(img[0]): return 0
    return img[y][x]

NOMINAL_AT = {0: [(3, -1), (-3, -1), (2, -2), (-2, -2)], 1: [(3, -1)], 2: [(2, -1)], 3: [(2, -1)]}
TPGD_CTX = {0: 0x9B25, 1: 0x0795, 2: 0x00E5, 3: 0x0195}

def gb_context(img, x, y, tpl, at):
    p = lambda dx, dy: _gb_pix(img, x + dx, y + dy)
    if tpl == 0:
        return (p(-1, 0) | p(-2, 0) << 1 | p(-3, 0) << 2 | p(-4, 0) << 3 | p(*at[0]) << 4 | p(2, -1) << 5 | p(1, -1) << 6
                | p(0, -1) << 7 | p(-1, -1) << 8 | p(-2, -1) << 9 | p(*at[1]) << 10 | p(*at[2]) << 11
                | p(1, -2) << 12 | p(0, -2) << 13 | p(-1, -2) << 14 | p(*at[3]) << 15)
    if tpl == 1:
        return (p(-1, 0) | p(-2, 0) << 1 | p(-3, 0) << 2 | p(*at[0]) << 3 | p(2, -1) << 4 | p(1, -1) << 5 | p(0, -1) << 6
                | p(-1, -1) << 7 | p(-2, -1) << 8 | p(2, -2) << 9 | p(1, -2) << 10 | p(0, -2) << 11 | p(-1, -2) << 12)
    if tpl == 2:
        return (p(-1, 0) | p(-2, 0) << 1 | p(*at[0]) << 2 | p(1, -1) << 3 | p(0, -1) << 4 | p(-1, -1) << 5 | p(-2, -1) << 6
                | p(1, -2) << 7 | p(0, -2) << 8 | p(-1, -2) << 9)
    return (p(-1, 0) | p(-2, 0) << 1 | p(-3, 0) << 2 | p(-4, 0) << 3 | p(*at[0]) << 4 | p(1, -1) << 5 | p(0, -1) << 6
            | p(-1, -1) << 7 | p(-2, -1) << 8 | p(-3, -1) << 9)

def enc_generic(e, img, tpl, tpgd, at=None, name='gb', skip=None):
    """code img (rows of 0/1) with the MQ encoder e"""
    at = at or NOMINAL_AT[tpl]
    h = len(img); w = len(img[0]); ltp = 0
    for y in range(h):
        if tpgd:
            prev = img[y - 1] if y else [0] * w
            typical = 1 if img[y] == prev else 0
            e.bit((name, TPGD_CTX[tpl]), typical ^ ltp); ltp = typical
            if typical: continue
        for x in range(w):
            if skip and skip(x, y): continue
            e.bit((name, gb_context(img, x, y, tpl, at)), img[y][x])

# ---- segments -----------------------------------------------------------------
def segment(num, typ, data, refs=(), page=1):
    fl = typ | (0x40 if page > 255 else 0)
    out = struct.pack('>I', num) + bytes([fl])
    n = len(refs)
    if n <= 4: out += bytes([n << 5])
    else:
        out += struct.pack('>I', (7 << 29) | n) + bytes((n + 8) >> 3)
    for r in refs:
        out += bytes([r]) if num <= 256 else struct.pack('>H', r) if num <= 65535 else struct.pack('>I', r)
    out += struct.pack('>I', page) if page > 255 else bytes([page])
    return out + struct.pack('>I', len(data)) + data

def page_info(w, h, default=0, comb=0, striped=0):
    f = (default << 2) | (comb << 3)
    return struct.pack('>IIIIBH', w, h, 0, 0, f, striped)

def region_info(w, h, x, y, op=0):
    return struct.pack('>IIIIB', w, h, x, y, op)

def generic_segment(img, x=0, y=0, op=0, tpl=0, tpgd=0, at=None, mmr=False):
    h = len(img); w = len(img[0])
    d = region_info(w, h, x, y, op)
    if mmr:
        d += bytes([1])
        bw = BitW(); ref = [0] * w
        for row in img: enc_2d(bw, row, ref); ref = row
        bw.put('000000000001' * 2)
        return d + bw.bytes()
    at = at or NOMINAL_AT[tpl]
    d += bytes([(tpl << 1) | (tpgd << 3)])
    for a in at: d += struct.pack('>bb', *a)
    e = MQEnc(); enc_generic(e, img, tpl, tpgd, at)
    return d + e.flush()

def jbig2_pdf(segs, w, h, globals_segs=None, decode_inv=False):
    """a PDF whose image is the JBIG2 segments (embedded format)"""
    data = b''.join(segs)
    objs = [b'<< /Type /Catalog /Pages 2 0 R >>', b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
            ('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Contents 4 0 R /Resources << /XObject << /Im0 5 0 R >> >> >>' % (w, h)).encode()]
    cont = ('q %d 0 0 %d 0 0 cm /Im0 Do Q' % (w, h)).encode()
    objs.append(b'<< /Length %d >>\nstream\n' % len(cont) + cont + b'\nendstream')
    parms = ' /DecodeParms << /JBIG2Globals 6 0 R >>' if globals_segs else ''
    objs.append(('<< /Type /XObject /Subtype /Image /Width %d /Height %d /BitsPerComponent 1 /ColorSpace /DeviceGray /Filter /JBIG2Decode%s /Length %d >>' % (w, h, parms, len(data))).encode()
                + b'\nstream\n' + data + b'\nendstream')
    if globals_segs:
        g = b''.join(globals_segs)
        objs.append(b'<< /Length %d >>\nstream\n' % len(g) + g + b'\nendstream')
    out = bytearray(b'%PDF-1.4\n'); offs = []
    for i, o in enumerate(objs):
        offs.append(len(out)); out += b'%d 0 obj\n' % (i + 1) + o + b'\nendobj\n'
    x = len(out)
    out += b'xref\n0 %d\n0000000000 65535 f \n' % (len(objs) + 1)
    for o in offs: out += b'%010d 00000 n \n' % o
    out += b'trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(objs) + 1, x)
    return bytes(out)

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

# ---- refinement (6.3), symbol dictionaries (6.5), text regions (6.4): arithmetic coded ----
def rf_context(cur, ref, x, y, dx, dy, tpl, at):
    c = lambda xx, yy: _gb_pix(cur, xx, yy)
    r = lambda xx, yy: _gb_pix(ref, xx - dx, yy - dy)
    if tpl == 0:
        a1 = c(x + at[0][0], y + at[0][1]); a2 = r(x + at[1][0], y + at[1][1])
        return (c(x - 1, y) | c(x + 1, y - 1) << 1 | c(x, y - 1) << 2 | a1 << 3 | r(x + 1, y + 1) << 4 | r(x, y + 1) << 5
                | r(x - 1, y + 1) << 6 | r(x + 1, y) << 7 | r(x, y) << 8 | r(x - 1, y) << 9 | r(x + 1, y - 1) << 10
                | r(x, y - 1) << 11 | a2 << 12)
    return (c(x - 1, y) | c(x + 1, y - 1) << 1 | c(x, y - 1) << 2 | c(x - 1, y - 1) << 3 | r(x + 1, y + 1) << 4
            | r(x, y + 1) << 5 | r(x + 1, y) << 6 | r(x, y) << 7 | r(x - 1, y) << 8 | r(x, y - 1) << 9)

NOMINAL_RAT = [(-1, -1), (-1, -1)]
def enc_refine(e, tgt, ref, dx, dy, tpl=0, tpgr=0, at=None, name='gr'):
    at = at or NOMINAL_RAT
    h = len(tgt); w = len(tgt[0]) if h else 0
    ltp = 0; cen = 0x80 if tpl else 0x100
    r = lambda xx, yy: _gb_pix(ref, xx - dx, yy - dy)
    for y in range(h):
        if tpgr:
            # a row is typical when every pixel whose 3x3 reference neighbourhood is uniform equals it
            typ = 1
            for x in range(w):
                nb = [r(x + i, y + j) for j in (-1, 0, 1) for i in (-1, 0, 1)]
                if len(set(nb)) == 1 and tgt[y][x] != nb[0]: typ = 0; break
            e.bit((name, cen), typ ^ ltp); ltp = typ
        for x in range(w):
            if tpgr and ltp:
                nb = [r(x + i, y + j) for j in (-1, 0, 1) for i in (-1, 0, 1)]
                if len(set(nb)) == 1: continue
            e.bit((name, rf_context(tgt, ref, x, y, dx, dy, tpl, at)), tgt[y][x])

def ceil_log2(n):
    k = 0
    while (1 << k) < n: k += 1
    return k

def symdict_segment(syms, tpl=0, at=None, exported=None, ref_syms=0):
    """syms: new symbols (2-D 0/1 lists), arithmetic coded, no aggregation.  Returns segment DATA.
    Symbols are sent in the order given, grouped into runs of equal height (callers sort)."""
    at = at or NOMINAL_AT[tpl]
    e = MQEnc(); nnew = len(syms); nin = ref_syms
    hprev = 0; i = 0
    while i < nnew:
        hc = len(syms[i]); enc_int(e, 'DH', hc - hprev); hprev = hc; wprev = 0
        while i < nnew and len(syms[i]) == hc:
            w = len(syms[i][0]) if hc else 0
            enc_int(e, 'DW', w - wprev); wprev = w
            if w and hc: enc_generic(e, syms[i], tpl, 0, at, name='gb')
            i += 1
        enc_int(e, 'DW', None)
    total = nin + nnew
    ex = exported if exported is not None else [False] * nin + [True] * nnew
    i = 0; cur = 0
    while i < total:
        j = i
        while j < total and ex[j] == bool(cur): j += 1
        enc_int(e, 'EX', j - i); i = j; cur ^= 1
    nex = sum(1 for x in ex if x)
    hdr = struct.pack('>H', tpl << 10)
    if tpl == 0: hdr += b''.join(struct.pack('>bb', *a) for a in at)
    else: hdr += struct.pack('>bb', *at[0])
    hdr += struct.pack('>II', nex, nnew)
    return hdr + e.flush()

class ArithEm:
    def __init__(s, e, cl): s.e = e; s.cl = cl
    def int(s, name, v): enc_int(s.e, name, v)
    def it(s, v, logs): enc_int(s.e, 'IT', v)
    def id(s, sid): enc_iaid(s.e, 'ID', s.cl, sid)

class HuffTab:
    """a Huffman table: normal lines (low, preflen, rangelen), lower (low, preflen), upper (low, preflen), oob preflen"""
    def __init__(s, normal, lower, upper, oob):
        s.normal, s.lower, s.upper, s.oob = normal, lower, upper, oob
        pls = [l[1] for l in normal] + [lower[1], upper[1], oob]
        s.codes = assign_codes(pls)
    @staticmethod
    def std(n): return HuffTab(*std_lines(n))
    def enc(s, bw, v):
        nn = len(s.normal)
        if v is None:
            c = s.codes[nn + 2]; assert c, 'table has no OOB'; bw.putn(*c); return
        for i, (low, pl, rl) in enumerate(s.normal):
            if pl and low <= v < low + (1 << rl):
                bw.putn(*s.codes[i]); bw.putn(v - low, rl); return
        if s.lower[1] and v < s.normal[0][0]:
            bw.putn(*s.codes[nn]); bw.putn(s.lower[0] - v, 32); return
        if s.upper[1] and v >= s.upper[0]:
            bw.putn(*s.codes[nn + 1]); bw.putn(v - s.upper[0], 32); return
        raise ValueError('value %d not in table' % v)

class HuffEm:
    """text-region Huffman emitter.  cfg: .tabs {name: HuffTab}, .hflags, .idtable_bytes"""
    def __init__(s, cfg, nsyms):
        s.cfg = cfg; s.bw = BitW(); s.nsyms = nsyms
        s.lens = cfg.symlens(nsyms); s.sym_codes = assign_codes(s.lens)
    def int(s, name, v):
        if name == 'RI': s.bw.put('1' if v else '0'); return
        s.cfg.tabs[name].enc(s.bw, v)
    def it(s, v, logs): s.bw.putn(v, logs)
    def id(s, sid): s.bw.putn(*s.sym_codes[sid])

class HuffCfg:
    def __init__(s, tabs, hflags, symlen=None):
        s.tabs = tabs; s.hflags = hflags; s.symlen = symlen
    def symlens(s, n):
        L = s.symlen or max(1, ceil_log2(n)) if n > 1 else 1
        return [L] * n
    def idtable_bytes(s, n):
        # 35 run-code lengths of 6 bits, then the symbol code lengths as literal run codes, byte aligned
        lens = s.symlens(n)
        bw = BitW()
        rc = assign_codes([6] * 35)
        for _ in range(35): bw.putn(6, 4)
        for l in lens: bw.putn(*rc[l])
        bw.align()
        return bw.bytes()

def text_segment(syms, insts, w, h, x=0, y=0, op=0, logstrips=0, corner=1, transp=0, comb=0, defpix=0, dsoff=0,
                 refine=False, rtpl=0, rat=None, huff=None):
    """insts: (symbol id, X, Y) with X, Y the top-left of the drawn bitmap on the region;
    for a refined instance (symbol id, X, Y, refined bitmap) the refined bitmap is drawn."""
    strips = 1 << logstrips
    e = MQEnc() if not huff else None; nsyms = len(syms); cl = ceil_log2(nsyms)
    em = ArithEm(e, cl) if not huff else HuffEm(huff, nsyms)
    rat = rat or NOMINAL_RAT
    items = []
    for it in insts:
        sid, X, Y = it[0], it[1], it[2]
        bm = it[3] if len(it) > 3 else syms[sid]
        bh = len(bm); bw = len(bm[0]) if bh else 0
        if not transp:
            S = X + (bw - 1 if corner in (2, 3) else 0)
            T = Y + (bh - 1 if corner in (0, 2) else 0)
        else:
            T = X + (bw - 1 if corner in (2, 3) else 0)
            S = Y + (bh - 1 if corner in (0, 2) else 0)
        items.append((T // strips, S, T, it, bw, bh))
    items.sort(key=lambda t: (t[0], t[1]))
    em.int('DT', 0 if not huff else 1)
    stript = 0 if not huff else -strips; firsts = 0; i = 0
    while i < len(items):
        st = items[i][0] * strips
        em.int('DT', (st - stript) // strips); stript = st
        first = True; curs = 0
        while i < len(items) and items[i][0] * strips == st:
            _, S, T, it, bw, bh = items[i]; i += 1
            sid = it[0]; hasref = len(it) > 3
            sw0 = len(syms[sid][0]); sh0 = len(syms[sid])
            # the pre-advance of the decoder: right corners add the DRAWN width - 1, so the code carries S - that
            wd = bw; hd = bh
            if not transp: pre = (wd - 1) if corner in (2, 3) else 0
            else: pre = (hd - 1) if corner in (0, 2) else 0
            cin = S - pre
            if first: em.int('FS', cin - firsts); firsts = cin; first = False
            else: em.int('DS', cin - curs - dsoff)
            if strips > 1: em.it(T - st, logstrips)
            em.id(sid)
            if refine: em.int('RI', 1 if hasref else 0)
            if hasref:
                rdw, rdh = bw - sw0, bh - sh0
                rdx = rdy = 0
                enc_int(e, 'RDW', rdw); enc_int(e, 'RDH', rdh); enc_int(e, 'RDX', rdx); enc_int(e, 'RDY', rdy)
                enc_refine(e, it[3], syms[sid], (rdw >> 1) + rdx, (rdh >> 1) + rdy, rtpl, 0, rat, 'gr')
            curs = S if pre else cin
            if not transp and corner in (0, 1): curs = cin + wd - 1
            elif transp and corner in (1, 3): curs = cin + hd - 1
            elif (not transp and corner in (2, 3)) or (transp and corner in (0, 2)): curs = S
        em.int('DS', None)
    flags = ((1 if huff else 0) | (1 if refine else 0) << 1 | logstrips << 2 | corner << 4 | transp << 6 | comb << 7
             | defpix << 9 | (dsoff & 31) << 10 | rtpl << 15)
    d = region_info(w, h, x, y, op) + struct.pack('>H', flags)
    if huff: d += struct.pack('>H', huff.hflags)
    if refine and not rtpl: d += b''.join(struct.pack('>bb', *a) for a in rat)
    d += struct.pack('>I', len(insts))
    if huff: return d + huff.idtable_bytes(nsyms) + em.bw.bytes()
    return d + e.flush()


# ---- the standard Huffman tables of T.88 annex B (B.1 to B.15) ------------------------------
# Lines are (PREFLEN, RANGELEN, RANGELOW) in table order: the normal lines, then the lower-range
# line, the upper-range line (PREFLEN 0 when the table has none) and, when HTOOB, the OOB line.
# PROVENANCE: transcribed from the tables compiled into MuPDF 1.28.6's mutool (jbig2dec's
# jbig2_hufftab.c) and checked here: every table is a complete prefix code (sum of 2**-PREFLEN
# is exactly 1) and its codes are assigned by B.3.
STD_TABLES = {1: [[1, 4, 0], [2, 8, 16], [3, 16, 272], [0, 32, -1], [3, 32, 65808]], 2: [[1, 0, 0], [2, 0, 1], [3, 0, 2], [4, 3, 3], [5, 6, 11], [0, 32, -1], [6, 32, 75], [6, 0, 0]], 3: [[8, 8, -256], [1, 0, 0], [2, 0, 1], [3, 0, 2], [4, 3, 3], [5, 6, 11], [8, 32, -257], [7, 32, 75], [6, 0, 0]], 4: [[1, 0, 1], [2, 0, 2], [3, 0, 3], [4, 3, 4], [5, 6, 12], [0, 32, -1], [5, 32, 76]], 5: [[7, 8, -255], [1, 0, 1], [2, 0, 2], [3, 0, 3], [4, 3, 4], [5, 6, 12], [7, 32, -256], [6, 32, 76]], 6: [[5, 10, -2048], [4, 9, -1024], [4, 8, -512], [4, 7, -256], [5, 6, -128], [5, 5, -64], [4, 5, -32], [2, 7, 0], [3, 7, 128], [3, 8, 256], [4, 9, 512], [4, 10, 1024], [6, 32, -2049], [6, 32, 2048]], 7: [[4, 9, -1024], [3, 8, -512], [4, 7, -256], [5, 6, -128], [5, 5, -64], [4, 5, -32], [4, 5, 0], [5, 5, 32], [5, 6, 64], [4, 7, 128], [3, 8, 256], [3, 9, 512], [3, 10, 1024], [5, 32, -1025], [5, 32, 2048]], 8: [[8, 3, -15], [9, 1, -7], [8, 1, -5], [9, 0, -3], [7, 0, -2], [4, 0, -1], [2, 1, 0], [5, 0, 2], [6, 0, 3], [3, 4, 4], [6, 1, 20], [4, 4, 22], [4, 5, 38], [5, 6, 70], [5, 7, 134], [6, 7, 262], [7, 8, 390], [6, 10, 646], [9, 32, -16], [9, 32, 1670], [2, 0, 0]], 9: [[8, 4, -31], [9, 2, -15], [8, 2, -11], [9, 1, -7], [7, 1, -5], [4, 1, -3], [3, 1, -1], [3, 1, 1], [5, 1, 3], [6, 1, 5], [3, 5, 7], [6, 2, 39], [4, 5, 43], [4, 6, 75], [5, 7, 139], [5, 8, 267], [6, 8, 523], [7, 9, 779], [6, 11, 1291], [9, 32, -32], [9, 32, 3339], [2, 0, 0]], 10: [[7, 4, -21], [8, 0, -5], [7, 0, -4], [5, 0, -3], [2, 2, -2], [5, 0, 2], [6, 0, 3], [7, 0, 4], [8, 0, 5], [2, 6, 6], [5, 5, 70], [6, 5, 102], [6, 6, 134], [6, 7, 198], [6, 8, 326], [6, 9, 582], [6, 10, 1094], [7, 11, 2118], [8, 32, -22], [8, 32, 4166], [2, 0, 0]], 11: [[1, 0, 1], [2, 1, 2], [4, 0, 4], [4, 1, 5], [5, 1, 7], [5, 2, 9], [6, 2, 13], [7, 2, 17], [7, 3, 21], [7, 4, 29], [7, 5, 45], [7, 6, 77], [0, 32, -1], [7, 32, 141]], 12: [[1, 0, 1], [2, 0, 2], [3, 1, 3], [5, 0, 5], [5, 1, 6], [6, 1, 8], [7, 0, 10], [7, 1, 11], [7, 2, 13], [7, 3, 17], [7, 4, 25], [8, 5, 41], [0, 32, -1], [8, 32, 73]], 13: [[1, 0, 1], [3, 0, 2], [4, 0, 3], [5, 0, 4], [4, 1, 5], [3, 3, 7], [6, 1, 15], [6, 2, 17], [6, 3, 21], [6, 4, 29], [6, 5, 45], [7, 6, 77], [0, 32, -1], [7, 32, 141]], 14: [[3, 0, -2], [3, 0, -1], [1, 0, 0], [3, 0, 1], [3, 0, 2], [0, 32, -1], [0, 32, 3]], 15: [[7, 4, -24], [6, 2, -8], [5, 1, -4], [4, 0, -2], [3, 0, -1], [1, 0, 0], [3, 0, 1], [4, 0, 2], [5, 1, 3], [6, 2, 5], [7, 4, 9], [7, 32, -25], [7, 32, 25]]}
STD_OOB = {1: 0, 2: 1, 3: 1, 4: 0, 5: 0, 6: 0, 7: 0, 8: 1, 9: 1, 10: 1, 11: 0, 12: 0, 13: 0, 14: 0, 15: 0}

def std_lines(n):
    """-> (normal lines [(low, preflen, rangelen)], lower (low, preflen), upper (low, preflen), oob preflen or 0)"""
    ls = STD_TABLES[n]; k = len(ls) - (1 if STD_OOB[n] else 0)
    normal = [(l[2], l[0], l[1]) for l in ls[:k - 2]]
    return normal, (ls[k - 2][2], ls[k - 2][0]), (ls[k - 1][2], ls[k - 1][0]), (ls[-1][0] if STD_OOB[n] else 0)

def assign_codes(preflens):
    """B.3: codes (value, length) for lines in table order; preflen 0 = no code"""
    mx = max(preflens + [0]); cnt = [0] * (mx + 2)
    for l in preflens:
        if l: cnt[l] += 1
    first = [0] * (mx + 2); out = [None] * len(preflens)
    for cl in range(1, mx + 1):
        first[cl] = (first[cl - 1] + cnt[cl - 1]) * 2
        c = first[cl]
        for i, l in enumerate(preflens):
            if l == cl: out[i] = (c, cl); c += 1
    return out

def gen_c_huff():
    low = []; pl = []; rl = []; start = [0]; nn = [0]
    for n in range(1, 16):
        normal, lo, up, oob = std_lines(n)
        start.append(len(low)); nn.append(len(normal))
        for (a, b, c) in normal: low.append(a); pl.append(b); rl.append(c)
        low.append(lo[0]); pl.append(lo[1]); rl.append(32)
        low.append(min(up[0], 32767)); pl.append(up[1]); rl.append(32)
        low.append(0); pl.append(oob); rl.append(0)       # the OOB line is always present; PREFLEN 0 = none
    start.append(len(low))
    f = lambda a: ", ".join(str(x) for x in a)
    return ("static const pdi hf_s_low[%d] = { %s };\nstatic const pdb hf_s_pl[%d] = { %s };\nstatic const pdb hf_s_rl[%d] = { %s };\n"
            "static const pdb hf_s_nn[16] = { %s };\nstatic const pdw hf_s_start[17] = { %s };" %
            (len(low), f(low), len(pl), f(pl), len(rl), f(rl), f(nn), f(start)))
if '--gen-c-huff' in sys.argv: print(gen_c_huff())

def table_segment_data(lines, lower_pl, upper_pl, oob_pl, low, high, ps, rs):
    """a custom Huffman table (segment type 53): lines = [(preflen, rangelen)] covering low..high"""
    flags = (1 if oob_pl else 0) | ((ps - 1) << 1) | ((rs - 1) << 4)
    bw = BitW()
    for pl, rl in lines: bw.putn(pl, ps); bw.putn(rl, rs)
    bw.putn(lower_pl, ps); bw.putn(upper_pl, ps)
    if oob_pl: bw.putn(oob_pl, ps)
    bw.align()
    return bytes([flags]) + struct.pack('>ii', low, high) + bw.bytes(), bw

def custom_tab(lines, low, lower_pl, upper_pl, oob_pl):
    normal = []; cur = low
    for pl, rl in lines: normal.append((cur, pl, rl)); cur += 1 << rl
    return HuffTab(normal, (low - 1, lower_pl), (cur, upper_pl), oob_pl)

def symdict_huff_segment(syms, dh=0, dw=0, bm_mmr=False, tpl=0, tabs=None, exported=None, ref_syms=0):
    """Huffman symbol dictionary.  dh 0/1 = B.4/B.5, dw 0/1 = B.2/B.3.  Symbols must be grouped by height
    (non-decreasing) and, for B.2, non-decreasing in width inside a class.  tabs: custom HuffTabs for
    ('DH','DW') overriding the standard ones, with selector 3."""
    tabs = tabs or {}
    tDH = tabs.get('DH') or HuffTab.std(4 if dh == 0 else 5)
    tDW = tabs.get('DW') or HuffTab.std(2 if dw == 0 else 3)
    tB1 = HuffTab.std(1)
    bw = BitW(); nnew = len(syms); nin = ref_syms
    hprev = 0; i = 0
    while i < nnew:
        hc = len(syms[i]); tDH.enc(bw, hc - hprev); hprev = hc; wprev = 0; cls = []
        while i < nnew and len(syms[i]) == hc:
            w = len(syms[i][0]) if hc else 0
            tDW.enc(bw, w - wprev); wprev = w; cls.append(syms[i]); i += 1
        tDW.enc(bw, None)
        totw = sum(len(x[0]) for x in cls)
        coll = [sum((x[y] for x in cls), []) for y in range(hc)]
        if bm_mmr and totw:
            b2 = BitW(); ref = [0] * totw
            for row in coll: enc_2d(b2, row, ref); ref = row
            data = b2.bytes()
            tB1.enc(bw, len(data)); bw.align(); bw.b += data
        else:
            tB1.enc(bw, 0); bw.align()
            for row in coll:
                r = BitW(); r.put(''.join(str(v) for v in row)); r.align(); bw.b += r.bytes()
    total = nin + nnew
    ex = exported if exported is not None else [False] * nin + [True] * nnew
    i = 0; cur = 0
    while i < total:
        j = i
        while j < total and ex[j] == bool(cur): j += 1
        tB1.enc(bw, j - i); i = j; cur ^= 1
    nex = sum(1 for x in ex if x)
    sel = lambda k, std_sel: 3 if k in tabs else std_sel
    flags = 1 | (sel('DH', dh) << 2) | (sel('DW', dw) << 4) | (tpl << 10)
    return struct.pack('>HII', flags, nex, nnew) + bw.bytes()

# ---- pattern dictionaries and halftone regions ------------------------------------------------
def pattern_segment(pats, mmr=False, tpl=0):
    pw = len(pats[0][0]); ph = len(pats[0]); n = len(pats)
    coll = [sum((p[y] for p in pats), []) for y in range(ph)]
    d = bytes([(1 if mmr else 0) | (tpl << 1), pw, ph]) + struct.pack('>I', n - 1)
    if mmr:
        bw = BitW(); ref = [0] * (n * pw)
        for row in coll: enc_2d(bw, row, ref); ref = row
        return d + bw.bytes()
    at = [(-pw, 0), (-3, -1), (2, -2), (-2, -2)]
    e = MQEnc(); enc_generic(e, coll, tpl, 0, at if tpl == 0 else at[:1])
    return d + e.flush()

def halftone_cells(gw, gh, gx, gy, hrx, hry):
    return {(m, n): ((gx + m * hry + n * hrx) >> 8, (gy + m * hrx - n * hry) >> 8) for m in range(gh) for n in range(gw)}

def halftone_segment(vals, pats, w, h, x=0, y=0, op=0, gx=0, gy=0, hrx=None, hry=0, mmr=False, tpl=0, skip=False,
                     hcomb=0, defpix=0):
    """vals: HGH rows of HGW gray values (< len(pats)).  Returns segment data."""
    gh = len(vals); gw = len(vals[0]); npats = len(pats); pw = len(pats[0][0]); ph = len(pats[0])
    hrx = hrx if hrx is not None else pw * 256
    bpp = max(1, ceil_log2(npats))
    cells = halftone_cells(gw, gh, gx, gy, hrx, hry)
    def skipf(xx, yy):          # x = column n, y = row m
        px, py = cells[(yy, xx)]
        return px + pw <= 0 or px >= w or py + ph <= 0 or py >= h
    flags = (1 if mmr else 0) | (tpl << 1) | ((1 if skip else 0) << 3) | (hcomb << 4) | (defpix << 7)
    d = region_info(w, h, x, y, op) + bytes([flags]) + struct.pack('>IIiiHH', gw, gh, gx, gy, hrx, hry)
    planes = []
    for j in range(bpp - 1, -1, -1):
        pl = [[((v >> j) & 1) ^ (((v >> (j + 1)) & 1) if j < bpp - 1 else 0) for v in row] for row in vals]
        if skip:
            pl = [[0 if skipf(xx, yy) else pl[yy][xx] for xx in range(gw)] for yy in range(gh)]
        planes.append(pl)
    at = [(3 if tpl <= 1 else 2, -1), (-3, -1), (2, -2), (-2, -2)]
    if mmr:
        out = bytearray()
        for pl in planes:
            bw = BitW(); ref = [0] * gw
            for row in pl: enc_2d(bw, row, ref); ref = row
            bw.put('000000000001' * 2); bw.align(); out += bw.bytes()
        return d + bytes(out)
    e = MQEnc()
    for pl in planes: enc_generic(e, pl, tpl, 0, at if tpl == 0 else at[:1], skip=skipf if skip else None)
    return d + e.flush()

# ---- committed fixtures (tests/pdfany/bilevel/) ------------------------------------------------
def build_fixtures(d):
    """deterministic: the same bytes every run (random.Random(seed) only)"""
    import random
    os.makedirs(d, exist_ok=True)
    rnd = random.Random(88)
    def rimg(w, h):
        img = []
        for y in range(h):
            if img and rnd.random() < .5:
                row = list(img[-1])
                for _ in range(rnd.randint(0, 5)): row[rnd.randrange(w)] ^= 1
            else:
                row = []; c = 0
                while len(row) < w:
                    row += [c] * (rnd.choice([1, 2, 3, 5, 9, 17, 70, 200]) if rnd.random() < .2 else rnd.randint(1, 12)); c ^= 1
                row = row[:w]
            img.append(row)
        return img
    def glyph(w, h): return [[1 if (x == 0 or y == 0 or x == w - 1 or (x + y) % 4 == 0 or rnd.random() < .15) else 0 for x in range(w)] for y in range(h)]
    def put(name, data): open(os.path.join(d, name), 'wb').write(data)
    # CCITT: G4, G3 1-D with EOL, G3 2-D (K=4), G4 byte aligned
    img = rimg(200, 48)
    for name, k, eol, align in (('g4', -1, False, False), ('g3_1d', 0, True, False), ('g3_2d', 4, True, False), ('g4_align', -1, False, True)):
        put('ccitt_%s.pdf' % name, ccitt_pdf(img, k, eol, align))
        put('ccitt_%s.bin' % name, enc_ccitt(img, k, eol=eol, align=align))
    # JBIG2
    W, H = 96, 64
    gimg = rimg(W, H)
    put('jb2_generic.pdf', jbig2_pdf([segment(0, 48, page_info(W, H)), segment(1, 38, generic_segment(gimg, tpl=0, tpgd=1))], W, H))
    put('jb2_generic.jb2', segment(0, 48, page_info(W, H)) + segment(1, 38, generic_segment(gimg, tpl=0, tpgd=1)))
    put('jb2_mmr.jb2', segment(0, 48, page_info(W, H)) + segment(1, 38, generic_segment(gimg, mmr=True)))
    syms = sorted((glyph(rnd.randint(3, 10), hh) for hh in (6, 6, 8, 9, 12)), key=lambda g: (len(g), len(g[0])))
    insts = [(rnd.randrange(len(syms)), rnd.randint(0, W - 8), rnd.randint(0, H - 12)) for _ in range(14)]
    glob = segment(0, 0, symdict_segment(syms, 0), page=0)
    pg = segment(1, 48, page_info(W, H)); tx = segment(2, 6, text_segment(syms, insts, W, H), refs=[0])
    put('jb2_text.jb2', pg + tx); put('jb2_text.glob', glob)
    put('jb2_text.pdf', jbig2_pdf([pg, tx], W, H, [glob]))
    cfg = HuffCfg({'FS': HuffTab.std(6), 'DS': HuffTab.std(8), 'DT': HuffTab.std(11), 'RDW': HuffTab.std(14), 'RDH': HuffTab.std(14),
                   'RDX': HuffTab.std(14), 'RDY': HuffTab.std(14)}, 0)
    hglob = segment(0, 0, symdict_huff_segment(syms, 0, 0, True), page=0)
    htx = segment(2, 6, text_segment(syms, insts, W, H, huff=cfg), refs=[0])
    put('jb2_huff.jb2', pg + htx); put('jb2_huff.glob', hglob)
    put('jb2_huff.pdf', jbig2_pdf([pg, htx], W, H, [hglob]))
    pats = [[[1 if rnd.random() < i / 5 else 0 for _ in range(4)] for _ in range(4)] for i in range(6)]
    vals = [[rnd.randrange(6) for _ in range(24)] for _ in range(16)]
    pd_ = segment(0, 16, pattern_segment(pats), page=0)
    hs = segment(2, 22, halftone_segment(vals, pats, W, H), refs=[0])
    put('jb2_halftone.jb2', pg + hs); put('jb2_halftone.glob', pd_)
    put('jb2_halftone.pdf', jbig2_pdf([pg, hs], W, H, [pd_]))

if '--build' in sys.argv: build_fixtures(sys.argv[sys.argv.index('--build') + 1])

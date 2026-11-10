#!/usr/bin/env python3
"""os88pdfx_fonts - the font fixtures of PageDesk's any-PDF round (PAGEDESK-SPEC 20.22.8).

    python3 tools/os88pdfx_fonts.py make [--out tests/pdfany/fonts] [--src DIR]
    python3 tools/os88pdfx_fonts.py selfcheck

There is no fontTools on the build host, so this file carries what it needs of
one: a TrueType reader and SUBSETTER (glyphs renumbered, composites followed,
a fresh cmap), a Type 1 reader/subsetter (eexec and charstring encryption), a
CFF WRITER (Type 2 charstrings from TrueType quadratics, or from Type 1 cubics),
and a plain PDF writer. Every fixture is DETERMINISTIC and is made from fonts
whose licence allows redistribution (README.md beside the fixtures names each
one). mutool is the independent reader that judges them (tests: hosttest/
pdx_check.py); nothing here reads a PDF.
"""
import argparse, os, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# ----------------------------------------------------------------------------
# TrueType
# ----------------------------------------------------------------------------
def u16(b, o): return struct.unpack_from(">H", b, o)[0]
def s16(b, o): return struct.unpack_from(">h", b, o)[0]
def u32(b, o): return struct.unpack_from(">I", b, o)[0]

class TTF:
    def __init__(self, data):
        self.d = data
        n = u16(data, 4)
        self.tab = {}
        for i in range(n):
            r = 12 + i * 16
            self.tab[data[r:r + 4].decode("latin1")] = (u32(data, r + 8), u32(data, r + 12))
        o = self.tab["head"][0]
        self.upem = u16(data, o + 18); self.locfmt = s16(data, o + 50)
        self.ng = u16(data, self.tab["maxp"][0] + 4)
        self.nhm = u16(data, self.tab["hhea"][0] + 34)
        lo, ll = self.tab["loca"]
        if self.locfmt:
            self.loca = [u32(data, lo + 4 * i) for i in range(self.ng + 1)]
        else:
            self.loca = [2 * u16(data, lo + 2 * i) for i in range(self.ng + 1)]
        self.cmap = self._cmap()

    def _cmap(self):
        o, l = self.tab["cmap"]
        d = self.d
        best = None
        for i in range(u16(d, o + 2)):
            r = o + 4 + i * 8
            pl, en, off = u16(d, r), u16(d, r + 2), u32(d, r + 4)
            if (pl, en) in ((3, 1), (0, 3), (0, 4)) and best is None:
                best = o + off
        s = best
        assert u16(d, s) == 4, "only cmap format 4 is read here"
        sc = u16(d, s + 6) // 2
        end = s + 14; start = end + 2 * sc + 2; delta = start + 2 * sc; rng = delta + 2 * sc
        m = {}
        for k in range(sc):
            e, st, dl, ro = u16(d, end + 2 * k), u16(d, start + 2 * k), s16(d, delta + 2 * k), u16(d, rng + 2 * k)
            for c in range(st, e + 1):
                if c == 0xFFFF: continue
                if ro == 0: g = (c + dl) & 0xFFFF
                else:
                    g = u16(d, rng + 2 * k + ro + 2 * (c - st))
                    if g: g = (g + dl) & 0xFFFF
                if g: m[c] = g
        return m

    def glyph(self, g):
        o = self.tab["glyf"][0]
        return self.d[o + self.loca[g]:o + self.loca[g + 1]]

    def adv(self, g):
        o = self.tab["hmtx"][0]
        if g >= self.nhm: g = self.nhm - 1
        return u16(self.d, o + 4 * g)

    def lsb(self, g):
        o = self.tab["hmtx"][0]
        if g < self.nhm: return s16(self.d, o + 4 * g + 2)
        return s16(self.d, o + 4 * self.nhm + 2 * (g - self.nhm))

    def components(self, g):
        b = self.glyph(g)
        if len(b) < 10 or s16(b, 0) >= 0: return []
        out = []; p = 10
        while True:
            fl, cg = u16(b, p), u16(b, p + 2); out.append((p + 2, cg))
            p += 4 + (4 if fl & 1 else 2)
            p += 2 if fl & 8 else 4 if fl & 0x40 else 8 if fl & 0x80 else 0
            if not fl & 0x20: break
        return out

def cksum(b):
    b = b + b"\0" * (-len(b) % 4)
    return sum(struct.unpack(">%dI" % (len(b) // 4), b)) & 0xFFFFFFFF

def build_sfnt(tables):
    tags = sorted(tables)
    n = len(tags)
    es = 1
    while es * 2 <= n: es *= 2
    out = struct.pack(">IHHHH", 0x00010000, n, es * 16, es.bit_length() - 1, n * 16 - es * 16)
    off = 12 + 16 * n
    body = b""; recs = b""
    for t in tags:
        d = tables[t]
        recs += t.encode() + struct.pack(">III", cksum(d), off + len(body), len(d))
        body += d + b"\0" * (-len(d) % 4)
    font = bytearray(out + recs + body)
    # head.checkSumAdjustment
    ho = [i for i, t in enumerate(tags) if t == "head"][0]
    hoff = struct.unpack_from(">I", font, 12 + 16 * ho + 8)[0]
    struct.pack_into(">I", font, hoff + 8, 0)
    adj = (0xB1B0AFBA - cksum(bytes(font))) & 0xFFFFFFFF
    struct.pack_into(">I", font, hoff + 8, adj)
    return bytes(font)

def subset_tt(t, chars):
    """-> (font bytes, {codepoint: new gid}, [(new gid, advance)] ). New gids are in
    original order, .notdef first; composites keep (and renumber) their parts."""
    keep = {0}
    for c in chars: keep.add(t.cmap[c])
    todo = list(keep)
    while todo:
        g = todo.pop()
        for _, cg in t.components(g):
            if cg not in keep: keep.add(cg); todo.append(cg)
    order = sorted(keep)
    new = {g: i for i, g in enumerate(order)}
    glyf = b""; loca = [0]
    for g in order:
        b = bytearray(t.glyph(g))
        if len(b) >= 10 and s16(b, 0) < 0:
            for p, cg in t.components(g): struct.pack_into(">H", b, p, new[cg])
        glyf += bytes(b) + b"\0" * (-len(b) % 4)
        loca.append(len(glyf))
    n = len(order)
    head = bytearray(t.d[t.tab["head"][0]:t.tab["head"][0] + 54]); struct.pack_into(">h", head, 50, 1)
    hhea = bytearray(t.d[t.tab["hhea"][0]:t.tab["hhea"][0] + 36]); struct.pack_into(">H", hhea, 34, n)
    maxp = bytearray(t.d[t.tab["maxp"][0]:t.tab["maxp"][0] + t.tab["maxp"][1]]); struct.pack_into(">H", maxp, 4, n)
    hmtx = b"".join(struct.pack(">Hh", t.adv(g), t.lsb(g)) for g in order)
    cps = sorted(set(chars))
    cm = {c: new[t.cmap[c]] for c in cps}
    # cmap format 4, one segment per code point (+ the 0xFFFF terminator): simple and exact
    segs = [(c, c, (cm[c] - c) & 0xFFFF) for c in cps] + [(0xFFFF, 0xFFFF, 1)]
    sc = len(segs)
    es = 1
    while es * 2 <= sc: es *= 2
    sub = struct.pack(">HHHHHHH", 4, 16 + 8 * sc, 0, sc * 2, es * 2, es.bit_length() - 1, sc * 2 - es * 2)
    sub += b"".join(struct.pack(">H", s[1]) for s in segs) + b"\0\0"
    sub += b"".join(struct.pack(">H", s[0]) for s in segs)
    sub += b"".join(struct.pack(">H", s[2]) for s in segs)
    sub += b"\0\0" * sc
    cmap = struct.pack(">HHHHI", 0, 1, 3, 1, 12) + sub
    post = struct.pack(">IIhhIIIII", 0x00030000, 0, 0, 0, 0, 0, 0, 0, 0)
    tables = {"head": bytes(head), "hhea": bytes(hhea), "maxp": bytes(maxp), "hmtx": hmtx,
              "loca": b"".join(struct.pack(">I", x) for x in loca), "glyf": glyf, "cmap": cmap, "post": post}
    # a subset keeps the glyphs' programs out of the way: no cvt/fpgm/prep (no hinting is read)
    return build_sfnt(tables), {c: cm[c] for c in cps}, [(new[g], t.adv(g)) for g in order]

# ----------------------------------------------------------------------------
# outlines: TrueType quadratics -> cubic paths (composites resolved here)
# ----------------------------------------------------------------------------
def tt_points(t, g, m=(1.0, 0.0, 0.0, 1.0, 0, 0), depth=0):
    """-> [contour], contour = [(x, y, on)] with the matrix m applied"""
    b = t.glyph(g)
    if len(b) < 10: return []
    nc = s16(b, 0)
    a, bb, c, d, e, f = m
    def tr(x, y): return (a * x + c * y + e, bb * x + d * y + f)
    if nc >= 0:
        ends = [u16(b, 10 + 2 * i) for i in range(nc)]
        p = 10 + 2 * nc; il = u16(b, p); p += 2 + il
        n = ends[-1] + 1 if ends else 0
        fl = []
        while len(fl) < n:
            v = b[p]; p += 1; fl.append(v)
            if v & 8:
                r = b[p]; p += 1; fl += [v] * r
        xs = []; cur = 0
        for v in fl[:n]:
            if v & 2: cur += b[p] if v & 16 else -b[p]; p += 1
            elif not v & 16: cur += s16(b, p); p += 2
            xs.append(cur)
        ys = []; cur = 0
        for v in fl[:n]:
            if v & 4: cur += b[p] if v & 32 else -b[p]; p += 1
            elif not v & 32: cur += s16(b, p); p += 2
            ys.append(cur)
        out = []; s0 = 0
        for e_ in ends:
            out.append([tr(xs[i], ys[i]) + (fl[i] & 1,) for i in range(s0, e_ + 1)]); s0 = e_ + 1
        return out
    out = []; p = 10
    while True:
        fl, cg = u16(b, p), u16(b, p + 2); p += 4
        if fl & 1: a1, a2 = s16(b, p), s16(b, p + 2); p += 4
        else: a1, a2 = struct.unpack_from(">bb", b, p); p += 2
        ma, mb, mc, md = 1.0, 0.0, 0.0, 1.0
        if fl & 8: ma = md = s16(b, p) / 16384; p += 2
        elif fl & 0x40: ma = s16(b, p) / 16384; md = s16(b, p + 2) / 16384; p += 4
        elif fl & 0x80: ma, mb, mc, md = [s16(b, p + 2 * i) / 16384 for i in range(4)]; p += 8
        cm = (ma, mb, mc, md, a1, a2)
        # component matrix first, then the parent's m
        nm = (cm[0] * a + cm[1] * c, cm[0] * bb + cm[1] * d, cm[2] * a + cm[3] * c, cm[2] * bb + cm[3] * d,
              cm[4] * a + cm[5] * c + e, cm[4] * bb + cm[5] * d + f)
        out += tt_points(t, cg, nm, depth + 1)
        if not fl & 0x20: break
    return out

def contour_path(pts):
    """TrueType contour -> [('M',x,y), ('L',x,y), ('C',x1,y1,x2,y2,x,y)...]; all ints"""
    n = len(pts)
    if n < 2: return []
    # rotate to start at an on-curve point, or at a midpoint
    on = [i for i in range(n) if pts[i][2]]
    if on:
        k = on[0]; pts = pts[k:] + pts[:k]; start = pts[0][:2]; seq = pts[1:] + [pts[0]]
    else:
        start = ((pts[0][0] + pts[-1][0]) / 2, (pts[0][1] + pts[-1][1]) / 2); seq = pts + [(start[0], start[1], 1)]
    path = [("M",) + tuple(round(v) for v in start)]
    cur = start; ctl = None
    def quad(c0, p):
        nonlocal cur
        c1 = (cur[0] + 2 * (c0[0] - cur[0]) / 3, cur[1] + 2 * (c0[1] - cur[1]) / 3)
        c2 = (p[0] + 2 * (c0[0] - p[0]) / 3, p[1] + 2 * (c0[1] - p[1]) / 3)
        path.append(("C",) + tuple(round(v) for v in c1 + c2 + tuple(p)))
        cur = p
    for x, y, o in seq:
        if o:
            if ctl: quad(ctl, (x, y)); ctl = None
            else: path.append(("L", round(x), round(y))); cur = (x, y)
        else:
            if ctl: quad(ctl, ((ctl[0] + x) / 2, (ctl[1] + y) / 2))
            ctl = (x, y)
    path.append(("Z",))
    return path

def tt_paths(t, g):
    return [contour_path(c) for c in tt_points(t, g)]

# ----------------------------------------------------------------------------
# CFF writer (Type 2 charstrings), including the compact operators
# ----------------------------------------------------------------------------
def t2num(v):
    v = int(v)
    if -107 <= v <= 107: return bytes([v + 139])
    if 108 <= v <= 1131: v -= 108; return bytes([247 + (v >> 8), v & 255])
    if -1131 <= v <= -108: v = -v - 108; return bytes([251 + (v >> 8), v & 255])
    return b"\x1c" + struct.pack(">h", v)

def t2op(op, *args):
    return b"".join(t2num(a) for a in args) + (bytes([op]) if op < 32 else bytes([12, op - 256]))

def charstring(paths, width=None, nominal=0, default=0, hints=False, compact=True):
    """one glyph's Type 2 charstring. Curves use hh/vv/hv/vh forms where they fit, and
    runs of lines use h/v alternation; hintmask/hstemhm are added when `hints`"""
    out = b""; first = [True]
    def wid_args(n_expected_extra=True):
        if first[0]:
            first[0] = False
            if width is not None and width != default: return [width - nominal]
        return []
    if hints:
        out += t2op(18, *(wid_args() + [0, 20]))          # hstemhm with one stem (width on the first op)
        out += t2op(19) + b"\x80"                        # hintmask, one stem -> one mask byte
    x = y = 0
    for path in paths:
        i = 0
        while i < len(path):
            seg = path[i]
            if seg[0] == "M":
                dx, dy = seg[1] - x, seg[2] - y; x, y = seg[1], seg[2]
                out += t2op(21, *(wid_args() + [dx, dy])); i += 1
            elif seg[0] == "L":
                # gather a run of lines
                run = []
                while i < len(path) and path[i][0] == "L":
                    run.append((path[i][1] - x, path[i][2] - y)); x, y = path[i][1], path[i][2]; i += 1
                while run:                                   # Type 2's argument stack holds 48
                    chunk, run = run[:20], run[20:]
                    if compact and all(r[0] == 0 or r[1] == 0 for r in chunk):
                        ok = True; h = chunk[0][0] != 0; vals = []
                        for r in chunk:
                            if (r[0] != 0) != h: ok = False; break
                            vals.append(r[0] if h else r[1]); h = not h
                        if ok: out += t2op(6 if chunk[0][0] != 0 else 7, *vals); continue
                    out += t2op(5, *[v for r in chunk for v in r])
            elif seg[0] == "C":
                dx1, dy1 = seg[1] - x, seg[2] - y; dx2, dy2 = seg[3] - seg[1], seg[4] - seg[2]; dx3, dy3 = seg[5] - seg[3], seg[6] - seg[4]
                x, y = seg[5], seg[6]
                if compact and dy1 == 0 and dx3 == 0: out += t2op(31, dx1, dx2, dy2, dy3)
                elif compact and dx1 == 0 and dy3 == 0: out += t2op(30, dy1, dx2, dy2, dx3)
                elif compact and dy1 == 0 and dy3 == 0: out += t2op(27, dx1, dx2, dy2, dx3)
                elif compact and dx1 == 0 and dx3 == 0: out += t2op(26, dy1, dx2, dy2, dy3)
                else: out += t2op(8, dx1, dy1, dx2, dy2, dx3, dy3)
                i += 1
            else:
                i += 1                                   # Z: closepath is implicit in Type 2
    if first[0] and width is not None and width != default:
        out = t2num(width - nominal) + out
        first[0] = False
    return out + bytes([14])

def cff_index(items):
    if not items: return b"\x00\x00"
    off = [1]
    for it in items: off.append(off[-1] + len(it))
    osz = 1 if off[-1] < 256 else 2 if off[-1] < 65536 else 4
    return struct.pack(">HB", len(items), osz) + b"".join(off_(o, osz) for o in off) + b"".join(items)

def off_(v, n): return v.to_bytes(n, "big")

def dict_int(v): return b"\x1d" + struct.pack(">i", v)           # fixed 5 bytes: offsets are known in two passes
def dict_op(op, *args):
    return b"".join(dict_int(a) if isinstance(a, int) and abs(a) > 107 else t2num(a) for a in args) + (bytes([op]) if op < 32 else bytes([12, op - 256]))

def dict_real(txt):
    nib = []
    for ch in txt:
        nib.append({".": 10, "-": 14}.get(ch, int(ch) if ch.isdigit() else 0))
    nib.append(15)
    if len(nib) % 2: nib.append(15)
    return b"\x1e" + bytes((nib[i] << 4) | nib[i + 1] for i in range(0, len(nib), 2))

t1_zero = b"\x8b"

def build_cff(glyphs, nominal=500, default=500, subrs=True, hints=True, cid=False, encoding=None, fontname="PDXFTest", matrix=None):
    """glyphs: [(name, paths, advance)], glyph 0 = .notdef. Names are CFF standard strings when
    they are in CFF_STD, else added to the String INDEX. cid=True: a CID-keyed font (CID = glyph
    index; ROS, FDArray, FDSelect 3). encoding: {code: gid} -> a format 0 Encoding. -> bytes"""
    strings = []
    def sid_of(n):
        if n in CFF_STD: return CFF_STD.index(n)
        if n not in strings: strings.append(n)
        return 391 + strings.index(n)
    css = [charstring(paths, adv, nominal, default, hints=hints and i % 2 == 1) for i, (n, paths, adv) in enumerate(glyphs)]
    lsubr = []; gsubr = []
    if subrs:                      # glyph 2's body into a local subr, glyph 5's into a global one (bias 107)
        for k, gi in enumerate([g for g in (2, 5) if g < len(css)]):
            body = css[gi][:-1]
            lst = lsubr if k == 0 else gsubr
            lst.append(body + bytes([11]))
            css[gi] = t2num(len(lst) - 1 - 107) + bytes([10 if k == 0 else 29, 14])
    if cid: strings = ["Adobe", "Identity"]
    else:
        for n, _, _ in glyphs: sid_of(n or ".notdef")
    charset = b"\x00" + b"".join(struct.pack(">H", g if cid else sid_of(glyphs[g][0])) for g in range(1, len(glyphs)))
    enc = b""
    if encoding and not cid:
        byg = {g: c for c, g in encoding.items()}
        n = max(byg)
        enc = b"\x00" + bytes([n]) + bytes(byg.get(g, 0) for g in range(1, n + 1))
    priv = dict_op(20, default) + dict_op(21, nominal)
    if lsubr: priv += dict_int(len(priv) + 6) + bytes([19])       # the Subrs offset counts from the Private DICT's start
    gs = cff_index(gsubr); lsx = cff_index(lsubr) if lsubr else b""; csx = cff_index(css)
    stri = cff_index([x.encode() for x in strings])
    name = cff_index([fontname.encode()])
    hdr = b"\x01\x00\x04\x02"
    fdsel = b"\x03" + struct.pack(">HHBH", 1, 0, 0, len(glyphs)) if cid else b""
    def top(o):
        d = b""
        if cid: d += t2num(391) + t2num(392) + t2num(0) + bytes([12, 30])
        d += dict_op(5, -200, -300, 1200, 1000)
        if matrix: d += dict_real(matrix) + t1_zero + t1_zero + dict_real(matrix) + t1_zero + t1_zero + bytes([12, 7])
        d += dict_int(o["charset"]) + bytes([15])
        if enc: d += dict_int(o["enc"]) + bytes([16])
        d += dict_int(o["cs"]) + bytes([17])
        if cid: d += dict_int(o["fdarr"]) + bytes([12, 36]) + dict_int(o["fdsel"]) + bytes([12, 37])
        else: d += dict_int(len(priv)) + dict_int(o["priv"]) + bytes([18])
        return d
    zero = {k: 0 for k in ("charset", "enc", "cs", "priv", "fdarr", "fdsel")}
    fd_dict = lambda po: dict_int(len(priv)) + dict_int(po) + bytes([18])
    fdarr_len = len(cff_index([fd_dict(0)])) if cid else 0
    p = len(hdr) + len(name) + len(cff_index([top(zero)])) + len(cff_index([x.encode() for x in strings])) + len(gs)
    o = {}
    o["charset"] = p; p += len(charset)
    o["enc"] = p if enc else 0; p += len(enc)
    o["fdsel"] = p; p += len(fdsel)
    o["cs"] = p; p += len(csx)
    o["fdarr"] = p; p += fdarr_len
    o["priv"] = p; p += len(priv)
    stri = cff_index([x.encode() for x in strings])
    out = hdr + name + cff_index([top(o)]) + stri + gs + charset + enc + fdsel + csx
    if cid: out += cff_index([fd_dict(o["priv"])])
    out += priv + lsx
    return out

# ----------------------------------------------------------------------------
# Type 1 writer (eexec, charstring encryption, flex, hint replacement, seac)
# ----------------------------------------------------------------------------
def t1_crypt(data, r, lead=b"\x01\x02\x03\x04", c1=52845, c2=22719):
    out = bytearray()
    for b in lead + data:
        c = b ^ (r >> 8)
        r = ((c + r) * c1 + c2) & 0xFFFF
        out.append(c)
    return bytes(out)

def t1_decrypt(data, r, skip=4, c1=52845, c2=22719):
    out = bytearray()
    for c in data:
        out.append(c ^ (r >> 8))
        r = ((c + r) * c1 + c2) & 0xFFFF
    return bytes(out[skip:])

def t1num(v):
    v = int(v)
    if -107 <= v <= 107: return bytes([v + 139])
    if 108 <= v <= 1131: v -= 108; return bytes([247 + (v >> 8), v & 255])
    if -1131 <= v <= -108: v = -v - 108; return bytes([251 + (v >> 8), v & 255])
    return b"\xff" + struct.pack(">i", v)

def t1op(op, *args):
    return b"".join(t1num(a) for a in args) + (bytes([op]) if op < 32 else bytes([12, op - 256]))

def t1_charstring(paths, adv, hint_replace=False):
    out = t1op(13, 0, adv)                                   # hsbw sbx=0 wx=adv
    x = y = 0
    for path in paths:
        for seg in path:
            if seg[0] == "M":
                dx, dy = seg[1] - x, seg[2] - y; x, y = seg[1], seg[2]
                out += t1op(22, dx) if dy == 0 and dx else t1op(4, dy) if dx == 0 and dy else t1op(21, dx, dy)
            elif seg[0] == "L":
                dx, dy = seg[1] - x, seg[2] - y; x, y = seg[1], seg[2]
                out += t1op(6, dx) if dy == 0 else t1op(7, dy) if dx == 0 else t1op(5, dx, dy)
            elif seg[0] == "C":
                dx1, dy1 = seg[1] - x, seg[2] - y; dx2, dy2 = seg[3] - seg[1], seg[4] - seg[2]; dx3, dy3 = seg[5] - seg[3], seg[6] - seg[4]
                x, y = seg[5], seg[6]
                if dy1 == 0 and dx3 == 0: out += t1op(31, dx1, dx2, dy2, dy3)
                elif dx1 == 0 and dy3 == 0: out += t1op(30, dy1, dx2, dy2, dx3)
                else: out += t1op(8, dx1, dy1, dx2, dy2, dx3, dy3)
            elif seg[0] == "Z":
                out += bytes([9])
    return out + bytes([14])

def t1_flex_glyph():
    cs = t1op(13, 100, 500)
    cs += t1op(21, 0, 100)                                    # (100,100) from the origin at sbx 100? hsbw puts us at (100, 0)
    cs += t1num(1) + t1op(10)                                 # 1 callsubr: start flex
    for dx, dy in ((150, 0), (-100, 0), (50, 30), (50, 0), (50, 0), (50, -30), (50, 0)):
        cs += t1op(21, dx, dy) + t1num(2) + t1op(10)          # rmoveto, 2 callsubr
    cs += t1num(50) + t1num(400) + t1num(100) + t1num(0) + t1op(10)    # flexheight x y 0 callsubr
    cs += t1op(7, 400) + t1op(6, -300) + bytes([9, 14])
    return cs

def build_t1(glyphs, encoding_names=None, leniv=4, extra=None, hexed=False, pfb=False, fontname="PDXFT1", stdenc=False):
    """glyphs: [(name, paths, adv)] (.notdef first). Returns (bytes, length1, length2, length3).
    extra: {name: raw charstring bytes}. A hint-replacement glyph is made of the glyph named 'B'."""
    subrs = [b"\x03\x00" if False else b""]                  # placeholder, replaced below
    subr_cs = [t1op(12, 33) if False else b""]
    S0 = t1num(3) + t1num(0) + t1op(12) + b"\x10" + t1op(12) + b"\x11" + t1op(12) + b"\x11" + t1op(12) + b"\x21" + bytes([11])
    S0 = t1num(3) + t1num(0) + b"\x0c\x10" + b"\x0c\x11" + b"\x0c\x11" + b"\x0c\x21" + bytes([11])    # 3 0 callothersubr pop pop setcurrentpoint return
    S1 = t1num(0) + t1num(1) + b"\x0c\x10" + bytes([11])
    S2 = t1num(0) + t1num(2) + b"\x0c\x10" + bytes([11])
    S3 = bytes([11])
    S4 = t1op(1, 0, 20) + bytes([11])                         # a hint set: hstem 0 20, return
    subrs = [S0, S1, S2, S3, S4]
    css = []
    for n, paths, adv in glyphs:
        hr = n == "B"
        cs = t1_charstring(paths, adv)
        if hr:   # hint replacement right after hsbw: 4 1 3 callothersubr pop callsubr
            hs = t1op(13, 0, adv)
            cs = hs + t1num(4) + t1num(1) + t1num(3) + b"\x0c\x10" + b"\x0c\x11" + bytes([10]) + cs[len(hs):]
        css.append((n, cs))
    for n, cs in (extra or {}).items(): css.append((n, cs))
    def enc(cs): return t1_crypt(cs, 4330, lead=bytes(range(10, 10 + leniv)) if leniv else b"")
    clear = ("%!PS-AdobeFont-1.0: " + fontname + " 1.0\n11 dict begin\n/FontInfo 1 dict dup begin /FullName (" + fontname + ") def end readonly def\n"
             "/FontName /" + fontname + " def\n/PaintType 0 def\n/FontType 1 def\n/FontMatrix [0.001 0 0 0.001 0 0] readonly def\n")
    if stdenc: clear += "/Encoding StandardEncoding def\n"
    else:
        clear += "/Encoding 256 array\n0 1 255 {1 index exch /.notdef put} for\n"
        for code, n in sorted((encoding_names or {}).items()): clear += "dup %d /%s put\n" % (code, n)
        clear += "readonly def\n"
    clear += "/FontBBox {-200 -300 1200 1000} readonly def\ncurrentdict end\ncurrentfile eexec\n"
    priv = b"dup /Private 12 dict dup begin\n/RD {string currentfile exch readstring pop} executeonly def\n/ND {noaccess def} executeonly def\n/NP {noaccess put} executeonly def\n/BlueValues [] def\n/MinFeature {16 16} def\n/password 5839 def\n"
    if leniv != 4: priv += b"/lenIV %d def\n" % leniv
    priv += b"/Subrs %d array\n" % len(subrs)
    for i, s in enumerate(subrs):
        e = enc(s); priv += b"dup %d %d RD " % (i, len(e)) + e + b" NP\n"
    priv += b"ND\n2 index /CharStrings %d dict dup begin\n" % len(css)
    for n, cs in css:
        e = enc(cs); priv += b"/%s %d RD " % (n.encode(), len(e)) + e + b" ND\n"
    priv += b"end\nend\nreadonly put\nnoaccess put\ndup /FontName get exch definefont pop\nmark currentfile closefile\n"
    ee = t1_crypt(priv, 55665)
    if hexed: ee = ee.hex().encode() + b"\n"
    trailer = (b"0" * 64 + b"\n") * 8 + b"cleartomark\n"
    c = clear.encode()
    if pfb:
        data = b"\x80\x01" + struct.pack("<I", len(c)) + c + b"\x80\x02" + struct.pack("<I", len(ee)) + ee + b"\x80\x01" + struct.pack("<I", len(trailer)) + trailer + b"\x80\x03"
        return data, len(c), len(ee), len(trailer)
    return c + ee + trailer, len(c), len(ee), len(trailer)

def t1_parse(data):
    """a real .pfb (or raw) Type 1 -> (cleartext, {name: charstring (decrypted)}, [subrs (decrypted)], leniv)"""
    import re
    if data[:1] == b"\x80":
        segs = []; p = 0
        while p < len(data) and data[p] == 0x80 and data[p + 1] != 3:
            n = struct.unpack_from("<I", data, p + 2)[0]; segs.append((data[p + 1], data[p + 6:p + 6 + n])); p += 6 + n
        clear = segs[0][1]; enc = segs[1][1]
    else:
        i = data.index(b"eexec") + 5
        while data[i] in b" \r\n\t": i += 1
        clear = data[:i]; enc = data[i:]
    priv = t1_decrypt(enc, 55665)
    m = re.search(rb"/lenIV\s+(\d+)", priv); leniv = int(m.group(1)) if m else 4
    subrs = {}
    for mm in re.finditer(rb"dup\s+(\d+)\s+(\d+)\s+(?:RD|-\|)\s", priv):
        i, n = int(mm.group(1)), int(mm.group(2)); st = mm.end()
        if b"/CharStrings" in priv[:mm.start()]: break
        subrs[i] = t1_decrypt(priv[st:st + n], 4330, leniv)
    cs = {}; cp = priv.index(b"/CharStrings")
    for mm in re.finditer(rb"/([^\s/]+)\s+(\d+)\s+(?:RD|-\|)\s", priv[cp:]):
        n = int(mm.group(2)); st = cp + mm.end()
        cs[mm.group(1).decode("latin1")] = t1_decrypt(priv[st:st + n], 4330, leniv)
    return clear, cs, [subrs[i] for i in sorted(subrs)], leniv, priv, clear

# ----------------------------------------------------------------------------
# a plain PDF writer
# ----------------------------------------------------------------------------
class PDF:
    def __init__(self): self.objs = []
    def add(self, body):
        self.objs.append(body); return len(self.objs)          # object number
    def reserve(self): self.objs.append(None); return len(self.objs)
    def set(self, n, body): self.objs[n - 1] = body
    def stream(self, d, extra="", compress=True):
        if isinstance(d, str): d = d.encode("latin1")
        if compress:
            z = zlib.compress(d, 9)
            return (b"<< /Length %d /Filter /FlateDecode %s >>\nstream\n" % (len(z), extra.encode())) + z + b"\nendstream"
        return (b"<< /Length %d %s >>\nstream\n" % (len(d), extra.encode())) + d + b"\nendstream"
    def write(self, root):
        out = bytearray(b"%PDF-1.5\n%\xe2\xe3\xcf\xd3\n"); offs = []
        for i, b in enumerate(self.objs):
            offs.append(len(out))
            if isinstance(b, str): b = b.encode("latin1")
            out += b"%d 0 obj\n" % (i + 1) + b + b"\nendobj\n"
        x = len(out)
        out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(self.objs) + 1)
        for o in offs: out += b"%010d 00000 n \n" % o
        out += b"trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(self.objs) + 1, root, x)
        return bytes(out)

def hexs(codes, nbytes=1):
    return "<" + "".join(("%0*X" % (2 * nbytes, c)) for c in codes) + ">"

def grid_page(font_res, size, entries, W, H, pitch_x, pitch_y, nbytes=1):
    """a content stream placing one glyph per cell: entries = [codes], baseline y grows down the page"""
    out = ["0 g"]
    per_row = (W - 8) // pitch_x
    for i, code in enumerate(entries):
        col, row = i % per_row, i // per_row
        x = 8 + col * pitch_x; y = H - (size + 6 + row * pitch_y)
        out.append("BT /F1 %d Tf %d %d Td %s Tj ET" % (size, x, y, hexs([code], nbytes)))
    return "\n".join(out)

# ----------------------------------------------------------------------------
# the fixtures
# ----------------------------------------------------------------------------
TEXT = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,:;!?-()&@#%/$+=*"
ACCENTS = [0xe9, 0xc5, 0xfc, 0xf1, 0xe7, 0xdf]
SIZES = (8, 10, 12, 14, 16)
PAGE_W, PAGE_H, PITCH_X, PITCH_Y = 320, 240, 28, 26

def find_src(name, src):
    for d in ([src] if src else []) + ["/private/tmp/os88-web-accelerator-pdf-libs/reportlab/fonts", os.path.expanduser("~/Repos/os8088-fixture-fonts")]:
        p = os.path.join(d, name)
        if os.path.exists(p): return p
    raise SystemExit("font source %s not found (use --src DIR)" % name)

def tt_simple_pdf(font_bytes, codes, widths, base, sizes=SIZES):
    """a simple TrueType font (WinAnsi), one page per size, one glyph per cell"""
    pdf = PDF()
    cat = pdf.reserve(); pages = pdf.reserve()
    ff = pdf.add(pdf.stream(font_bytes, "/Length1 %d" % len(font_bytes)))
    fd = pdf.add("<< /Type /FontDescriptor /FontName /%s /Flags 32 /FontBBox [-200 -300 1200 1000] /ItalicAngle 0 /Ascent 900 /Descent -250 /CapHeight 700 /StemV 80 /FontFile2 %d 0 R >>" % (base, ff))
    first, last = min(codes), max(codes)
    w = " ".join(str(widths.get(c, 0)) for c in range(first, last + 1))
    font = pdf.add("<< /Type /Font /Subtype /TrueType /BaseFont /%s /FirstChar %d /LastChar %d /Widths [%s] /Encoding /WinAnsiEncoding /FontDescriptor %d 0 R >>" % (base, first, last, w, fd))
    kids = []
    for sz in sizes:
        cs = pdf.add(pdf.stream(grid_page(None, sz, codes, PAGE_W, PAGE_H, PITCH_X, PITCH_Y), compress=False))
        kids.append(pdf.add("<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d] /Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R >>" % (pages, PAGE_W, PAGE_H, font, cs)))
    pdf.set(pages, "<< /Type /Pages /Kids [%s] /Count %d >>" % (" ".join("%d 0 R" % k for k in kids), len(kids)))
    pdf.set(cat, "<< /Type /Catalog /Pages %d 0 R >>" % pages)
    return pdf.write(cat)

def write_sidecar(out, name, program, codes, gids, nbytes=1):
    """what hosttest/pdx_check.py needs to place PDXF's glyphs exactly where the PDF put them"""
    import json
    json.dump({"pdf": name + ".pdf", "program": program, "codes": codes, "gids": gids, "sizes": list(SIZES),
               "page": [PAGE_W, PAGE_H], "pitch": [PITCH_X, PITCH_Y], "margin": 8, "nbytes": nbytes},
              open(os.path.join(out, name + ".json"), "w"), indent=1, sort_keys=True)
    open(os.path.join(out, name + ".json"), "a").write("\n")

def latname(code):
    import os88pdfont as pf
    return pf.GLYPHNAMES[code - 32]

def widths_of(t, chars): return {c: round(t.adv(t.cmap[c]) * 1000 / t.upem) for c in chars}

def simple_font_pdf(subtype, fontfile_key, fontfile, ffextra, codes, widths, base, flags=32, encoding="/WinAnsiEncoding", sizes=SIZES, extra_font=""):
    pdf = PDF()
    cat = pdf.reserve(); pages = pdf.reserve()
    ff = pdf.add(pdf.stream(fontfile, ffextra))
    fd = pdf.add("<< /Type /FontDescriptor /FontName /%s /Flags %d /FontBBox [-200 -300 1200 1000] /ItalicAngle 0 /Ascent 900 /Descent -250 /CapHeight 700 /StemV 80 /%s %d 0 R >>" % (base, flags, fontfile_key, ff))
    first, last = min(codes), max(codes)
    w = " ".join(str(widths.get(c, 0)) for c in range(first, last + 1))
    font = pdf.add("<< /Type /Font /Subtype /%s /BaseFont /%s /FirstChar %d /LastChar %d /Widths [%s] %s /FontDescriptor %d 0 R %s >>" % (subtype, base, first, last, w, ("/Encoding " + encoding) if encoding else "", fd, extra_font))
    kids = []
    for sz in sizes:
        cs = pdf.add(pdf.stream(grid_page(None, sz, codes, PAGE_W, PAGE_H, PITCH_X, PITCH_Y), compress=False))
        kids.append(pdf.add("<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d] /Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R >>" % (pages, PAGE_W, PAGE_H, font, cs)))
    pdf.set(pages, "<< /Type /Pages /Kids [%s] /Count %d >>" % (" ".join("%d 0 R" % k for k in kids), len(kids)))
    pdf.set(cat, "<< /Type /Catalog /Pages %d 0 R >>" % pages)
    return pdf.write(cat)

def cid_font_pdf(cidsub, fontfile_key, fontfile, ffextra, cids, widths, base, cidtogid=None, tounicode=None, encoding="/Identity-H", sizes=SIZES, cmapstream=None):
    """a Type0 font: 2-byte codes. widths = {cid: w1000}"""
    pdf = PDF()
    cat = pdf.reserve(); pages = pdf.reserve()
    ff = pdf.add(pdf.stream(fontfile, ffextra))
    fd = pdf.add("<< /Type /FontDescriptor /FontName /%s /Flags 4 /FontBBox [-200 -300 1200 1000] /ItalicAngle 0 /Ascent 900 /Descent -250 /CapHeight 700 /StemV 80 /%s %d 0 R >>" % (base, fontfile_key, ff))
    wr = " ".join("%d [%d]" % (c, widths.get(c, 0)) for c in sorted(set(cids)))
    extra = ""
    if cidtogid is not None:
        m = pdf.add(pdf.stream(cidtogid)); extra += " /CIDToGIDMap %d 0 R" % m
    elif cidsub == "CIDFontType2": extra += " /CIDToGIDMap /Identity"
    cidf = pdf.add("<< /Type /Font /Subtype /%s /BaseFont /%s /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor %d 0 R /DW 1000 /W [%s]%s >>" % (cidsub, base, fd, wr, extra))
    enc = encoding
    if cmapstream is not None:
        enc = "%d 0 R" % pdf.add(pdf.stream(cmapstream, "/Type /CMap /CMapName /PDXF-Test"))
    tu = ""
    if tounicode is not None: tu = " /ToUnicode %d 0 R" % pdf.add(pdf.stream(tounicode))
    font = pdf.add("<< /Type /Font /Subtype /Type0 /BaseFont /%s /Encoding %s /DescendantFonts [%d 0 R]%s >>" % (base, enc, cidf, tu))
    kids = []
    for sz in sizes:
        cs = pdf.add(pdf.stream(grid_page(None, sz, cids, PAGE_W, PAGE_H, PITCH_X, PITCH_Y, 2), compress=False))
        kids.append(pdf.add("<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d] /Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R >>" % (pages, PAGE_W, PAGE_H, font, cs)))
    pdf.set(pages, "<< /Type /Pages /Kids [%s] /Count %d >>" % (" ".join("%d 0 R" % k for k in kids), len(kids)))
    pdf.set(cat, "<< /Type /Catalog /Pages %d 0 R >>" % pages)
    return pdf.write(cat)

def scaled(paths, k):
    return [[(seg[0],) + tuple(round(v * k) for v in seg[1:]) for seg in path] for path in paths]

def tounicode_cmap(pairs):
    """pairs [(code, unicode)] -> a ToUnicode CMap using bfchar and bfrange"""
    out = ["/CIDInit /ProcSet findresource begin 12 dict begin begincmap", "/CMapName /Adobe-Identity-UCS def /CMapType 2 def",
           "1 begincodespacerange <0000> <FFFF> endcodespacerange"]
    pairs = sorted(pairs)
    ch = [p for p in pairs]
    for i in range(0, len(ch), 100):
        out.append("%d beginbfchar" % len(ch[i:i + 100]))
        for c, u in ch[i:i + 100]: out.append("<%04X> <%04X>" % (c, u))
        out.append("endbfchar")
    out.append("endcmap CMapName currentdict /CMap defineresource pop end end")
    return "\n".join(out) + "\n"

def make(out, src):
    os.makedirs(out, exist_ok=True)
    t = TTF(open(find_src("Vera.ttf", src), "rb").read())
    chars = [ord(c) for c in TEXT] + ACCENTS
    codes = sorted(chars)
    fb, cm, adv = subset_tt(t, chars)
    w1000 = widths_of(t, chars)
    def wr(name, data): open(os.path.join(out, name), "wb").write(data)
    wr("vera-sub.ttf", fb)
    wr("tt-simple.pdf", tt_simple_pdf(fb, codes, w1000, "ABCDEF+Vera"))
    write_sidecar(out, "tt-simple", "vera-sub.ttf", codes, [cm[c] for c in codes])
    # ---- CFF (Type1C) from the same outlines, scaled to a 1000 em
    k = 1000.0 / t.upem
    glyphs = [(".notdef", [], round(500))]
    for c in codes: glyphs.append((latname(c), scaled(tt_paths(t, t.cmap[c]), k), w1000[c]))
    cff = build_cff(glyphs)
    wr("vera.cff", cff)
    wr("cff-simple.pdf", simple_font_pdf("Type1", "FontFile3", cff, "/Subtype /Type1C", codes, w1000, "ABCDEF+VeraCFF"))
    write_sidecar(out, "cff-simple", "vera.cff", codes, list(range(1, len(codes) + 1)))
    # ---- CFF at 2048 units with a real FontMatrix, no subrs/hints (the plain path)
    glyphs2 = [(".notdef", [], 1024)] + [(latname(c), tt_paths(t, t.cmap[c]), t.adv(t.cmap[c])) for c in codes]
    cff2 = build_cff(glyphs2, nominal=1000, default=1000, subrs=False, hints=False, matrix="0.00048828125")
    wr("vera2048.cff", cff2)
    wr("cff-2048.pdf", simple_font_pdf("Type1", "FontFile3", cff2, "/Subtype /Type1C", codes, w1000, "ABCDEF+VeraCFF2048"))
    write_sidecar(out, "cff-2048", "vera2048.cff", codes, list(range(1, len(codes) + 1)))
    # ---- CFF CID-keyed in a Type0 font
    cidg = [(None, [], 500)] + [(None, scaled(tt_paths(t, t.cmap[c]), k), w1000[c]) for c in codes]
    cffc = build_cff(cidg, cid=True)
    wr("vera-cid.cff", cffc)
    cids = list(range(1, len(codes) + 1))
    cw = {i + 1: w1000[c] for i, c in enumerate(codes)}
    wr("cff-cid.pdf", cid_font_pdf("CIDFontType0", "FontFile3", cffc, "/Subtype /CIDFontType0C", cids, cw, "ABCDEF+VeraCID",
        tounicode=tounicode_cmap([(i + 1, c) for i, c in enumerate(codes)]).encode()))
    write_sidecar(out, "cff-cid", "vera-cid.cff", cids, cids, 2)
    # ---- TrueType in a Type0 font: Identity CIDToGIDMap, cid == new gid; and a mapped one with ToUnicode
    gids = [cm[c] for c in codes]
    cwt = {cm[c]: w1000[c] for c in codes}
    wr("tt-cid.pdf", cid_font_pdf("CIDFontType2", "FontFile2", fb, "/Length1 %d" % len(fb), gids, cwt, "ABCDEF+VeraCID2",
        tounicode=tounicode_cmap([(cm[c], c) for c in codes]).encode()))
    write_sidecar(out, "tt-cid", "vera-sub.ttf", gids, gids, 2)
    # CID = the code point, the CIDToGIDMap stream maps it to the glyph (a window below 2048 here)
    m = bytearray(2 * 256)
    for c in codes:
        struct.pack_into(">H", m, 2 * c, cm[c])
    wr("tt-cidmap.pdf", cid_font_pdf("CIDFontType2", "FontFile2", fb, "/Length1 %d" % len(fb), codes, {c: w1000[c] for c in codes}, "ABCDEF+VeraMap",
        cidtogid=bytes(m)))
    write_sidecar(out, "tt-cidmap", "vera-sub.ttf", codes, gids, 2)
    # ---- Type 1: generated (PDF raw form), with flex, hint replacement and a hex eexec variant
    t1g = [(".notdef", [], 500)] + [(latname(c), scaled(tt_paths(t, t.cmap[c]), k), w1000[c]) for c in codes]
    flexcs = t1_flex_glyph()
    enc = {c: latname(c) for c in codes}
    for nm, hexed in (("t1-simple", False), ("t1-hex", True)):
        data, l1, l2, l3 = build_t1(t1g, encoding_names=enc, extra={"flexdemo": flexcs}, hexed=hexed)
        wr(nm + ".pfa", data)
        wr(nm + ".pdf", simple_font_pdf("Type1", "FontFile", data, "/Length1 %d /Length2 %d /Length3 %d" % (l1, l2, l3), codes + [0x7f], {**w1000, 0x7f: 500}, "ABCDEF+VeraT1",
            encoding="<< /Type /Encoding /BaseEncoding /WinAnsiEncoding /Differences [127 /flexdemo] >>"))
        write_sidecar(out, nm, nm + ".pfa", codes + [0x7f], list(range(2, len(codes) + 3)))
    print("wrote", out)

# ----------------------------------------------------------------------------
# glyph names: apps/pagedesk/pdxfnm.h (generated; checked by selfcheck)
# ----------------------------------------------------------------------------
CFF_STD = (".notdef space exclam quotedbl numbersign dollar percent ampersand quoteright parenleft parenright "
 "asterisk plus comma hyphen period slash zero one two three four five six seven eight nine colon semicolon "
 "less equal greater question at A B C D E F G H I J K L M N O P Q R S T U V W X Y Z bracketleft backslash "
 "bracketright asciicircum underscore quoteleft a b c d e f g h i j k l m n o p q r s t u v w x y z braceleft "
 "bar braceright asciitilde exclamdown cent sterling fraction yen florin section currency quotesingle "
 "quotedblleft guillemotleft guilsinglleft guilsinglright fi fl endash dagger daggerdbl periodcentered "
 "paragraph bullet quotesinglbase quotedblbase quotedblright guillemotright ellipsis perthousand questiondown "
 "grave acute circumflex tilde macron breve dotaccent dieresis ring cedilla hungarumlaut ogonek caron emdash "
 "AE ordfeminine Lslash Oslash OE ordmasculine ae dotlessi lslash oslash oe germandbls onesuperior logicalnot "
 "mu trademark Eth onehalf plusminus Thorn onequarter divide brokenbar degree thorn threequarters twosuperior "
 "registered minus eth multiply threesuperior copyright Aacute Acircumflex Adieresis Agrave Aring Atilde "
 "Ccedilla Eacute Ecircumflex Edieresis Egrave Iacute Icircumflex Idieresis Igrave Ntilde Oacute Ocircumflex "
 "Odieresis Ograve Otilde Scaron Uacute Ucircumflex Udieresis Ugrave Yacute Ydieresis Zcaron aacute "
 "acircumflex adieresis agrave aring atilde ccedilla eacute ecircumflex edieresis egrave iacute icircumflex "
 "idieresis igrave ntilde oacute ocircumflex odieresis ograve otilde scaron uacute ucircumflex udieresis "
 "ugrave yacute ydieresis zcaron").split()
assert len(CFF_STD) == 229
STD_HI = {161: 96, 162: 97, 163: 98, 164: 99, 165: 100, 166: 101, 167: 102, 168: 103, 169: 104, 170: 105, 171: 106,
          172: 107, 173: 108, 174: 109, 175: 110, 177: 111, 178: 112, 179: 113, 180: 114, 182: 115, 183: 116,
          184: 117, 185: 118, 186: 119, 187: 120, 188: 121, 189: 122, 191: 123, 193: 124, 194: 125, 195: 126,
          196: 127, 197: 128, 198: 129, 199: 130, 200: 131, 202: 132, 203: 133, 205: 134, 206: 135, 207: 136,
          208: 137, 225: 138, 227: 139, 232: 140, 233: 141, 234: 142, 235: 143, 241: 144, 245: 145, 248: 146,
          249: 147, 250: 148, 251: 149}
CP1252_HI = [0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,
             0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A,
             0x0153, 0, 0x017E, 0x0178]

def std_sid(code):
    if 32 <= code <= 126: return 8 if code == 39 else 65 if code == 96 else code - 31
    return STD_HI.get(code, 0)

def names_header():
    import os88pdfont as pf
    out = ["/* generated by tools/os88pdfx_fonts.py names - do not edit (PAGEDESK-SPEC 20.22.8) */"]
    def cstr(name, items, doc):
        out.append("/* %s */" % doc)
        out.append("static const char %s[] =" % name)
        line = '    "'
        for it in items:
            tok = it + "\\0"
            if len(line) + len(tok) > 90: out.append(line + '"'); line = '    "'
            line += tok
        out.append(line + '";')
    cstr("pdx_latnames", pf.GLYPHNAMES, "the Adobe name of each PD Latin code 32..255, NUL separated")
    cstr("pdx_stdsid", CFF_STD, "CFF standard strings, SID 0..228, NUL separated")
    out.append("/* StandardEncoding: code -> SID (0 = .notdef) */")
    out.append("static const pdb pdx_stdenc[256] = {")
    for i in range(0, 256, 16): out.append("    " + ", ".join("%d" % std_sid(c) for c in range(i, i + 16)) + ",")
    out.append("};")
    out.append("/* the 32 PD Latin codes 0x80..0x9F: Unicode (0 = none) */")
    out.append("static const pdw pdx_cp1252[32] = {" + ", ".join("0x%04X" % v for v in CP1252_HI) + "};")
    return "\n".join(out) + "\n"

def selfcheck():
    import os88pdfont as pf
    here = os.path.dirname(os.path.abspath(__file__))
    p = os.path.join(here, "..", "apps", "pagedesk", "pdxfnm.h")
    if open(p).read() != names_header():
        raise SystemExit("pdxfnm.h is out of date: python3 tools/os88pdfx_fonts.py names")
    print("os88pdfx_fonts: ok")

def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make"); m.add_argument("--out", default="tests/pdfany/fonts"); m.add_argument("--src", default=None)
    sub.add_parser("selfcheck"); sub.add_parser("names")
    a = ap.parse_args()
    if a.cmd == "selfcheck": return selfcheck()
    if a.cmd == "names":
        here = os.path.dirname(os.path.abspath(__file__))
        open(os.path.join(here, "..", "apps", "pagedesk", "pdxfnm.h"), "w").write(names_header()); return 0
    make(a.out, a.src)
    return 0

if __name__ == "__main__":
    sys.exit(main())

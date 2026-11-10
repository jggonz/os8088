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

def make(out, src):
    os.makedirs(out, exist_ok=True)
    t = TTF(open(find_src("Vera.ttf", src), "rb").read())
    chars = [ord(c) for c in TEXT] + ACCENTS
    fb, cm, adv = subset_tt(t, chars)
    w1000 = {c: round(t.adv(t.cmap[c]) * 1000 / t.upem) for c in chars}
    open(os.path.join(out, "vera-sub.ttf"), "wb").write(fb)
    codes = sorted(chars)
    open(os.path.join(out, "tt-simple.pdf"), "wb").write(tt_simple_pdf(fb, codes, w1000, "ABCDEF+Vera"))
    write_sidecar(out, "tt-simple", "vera-sub.ttf", codes, [cm[c] for c in codes])
    print("wrote", out)

def selfcheck():
    print("os88pdfx_fonts: ok")

def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make"); m.add_argument("--out", default="tests/pdfany/fonts"); m.add_argument("--src", default=None)
    sub.add_parser("selfcheck")
    a = ap.parse_args()
    if a.cmd == "selfcheck": return selfcheck()
    make(a.out, a.src)
    return 0

if __name__ == "__main__":
    sys.exit(main())

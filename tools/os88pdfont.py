#!/usr/bin/env python3
"""os88pdfont.py - PageDesk's document faces, PDF width tables and PDRES.BIN
(docs/PAGEDESK-SPEC.md 11.2, 11.3, 20.4).

  fetch   download the pinned Liberation TTFs and Adobe Core14 AFMs into
          build/pdfetch/ (URL + SHA-256, verified; never run by make)
  draw    rasterise the TTFs into the COMMITTED strike sources under
          apps/pagedesk/fonts/*.t88, and extract the AFM advance widths into
          apps/pagedesk/fonts/widths.txt (needs Pillow with FreeType)
  build   pack the committed sources (+ art, via tools/os88pdart.py) into
          PDRES.BIN.  Pure Python, no network, no Pillow.  Run by make.
  proof   render sample lines of every strike to PNGs (needs Pillow)
  check   re-read PDRES.BIN and verify it against the sources

The strike text files are the SOURCE once drawn: hand-fix a glyph there.
"""
import argparse, hashlib, os, struct, sys, tarfile, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONTS = os.path.join(ROOT, "apps", "pagedesk", "fonts")
FETCH = os.path.join(ROOT, "build", "pdfetch")
sys.path.insert(0, HERE)

LIB_URL = ("https://github.com/liberationfonts/liberation-fonts/files/7261482/"
           "liberation-fonts-ttf-2.1.5.tar.gz")
LIB_SHA = "7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0"
LIB_DIR = "liberation-fonts-ttf-2.1.5"
AFM_BASE = ("https://raw.githubusercontent.com/apache/pdfbox/"
            "a0e38de6ce94103032c3806735c7de817833d8e5/pdfbox/src/main/resources/"
            "org/apache/pdfbox/resources/afm/")
AFMS = {
 "Courier-Bold": "1e86f0a949e316c08df32adc2fc2e2a92ed9902ffd54e3516e5a66a32db856da",
 "Courier-BoldOblique": "e1349f5f306b16338972eaf7f43d7913535cc075a15189b5f3efc841aee3d02e",
 "Courier-Oblique": "9e3494b4abb127a6586c095a377924514dd6956973f8f3b0bd7d6d00fcc89ea6",
 "Courier": "e72833dda79161e3aaf385adfeb59c61c89a4a3e184f27d21c178056eea30e17",
 "Helvetica-Bold": "59ac93091bc7e76eb6baf60210526a4df88fe107dd5e13eb0ae70c9fc6d91fb9",
 "Helvetica-BoldOblique": "1f848276efd76c0e5020ef98ccb459005f5a022f56cb8579e52c98e33ffab03b",
 "Helvetica-Oblique": "8c00ac46ca5963df22d4fc1153ea1246c8778475820378640c8d54d4fd3a46fe",
 "Helvetica": "5654761eab868ccb3080122accf13b4fb79edf37af09e1c57a881c35c334ee03",
 "Times-Bold": "37efa084183acfb06f45ad73bd75f6dd09b20d1e498c66ce1b9aa72b00db4ac7",
 "Times-BoldItalic": "1bf83cee5300d4de1ce90681a2a2d70fd1876c664e91c1c4c6b6b5b12873b6da",
 "Times-Italic": "2f34d3bc49530532daa690aec165b68368162d4c27d24fbe6a3fb24aae49a74a",
 "Times-Roman": "b6dbde33196ff61f051f8468fb81a0dece63997a80b7fba058e8295f49480026",
}
MUSTREAD = ("MustRead.html",
            "b226bfc00e1b8b8a80c7b3cfbbc322d13b4b0401f94cdeafdd93b2210ad802eb")

FAMILIES = ["sans", "serif", "mono"]
TTF = {"sans": "LiberationSans", "serif": "LiberationSerif", "mono": "LiberationMono"}
AFMNAME = {"sans": ("Helvetica", "-Bold", "-Oblique", "-BoldOblique"),
           "serif": ("Times", "-Bold", "-Italic", "-BoldItalic"),
           "mono": ("Courier", "-Bold", "-Oblique", "-BoldOblique")}
STYLES = ["r", "b", "i", "bi"]            # style byte = bit0 bold, bit1 italic
STYLEBITS = {"r": 0, "b": 1, "i": 2, "bi": 3}
STYLETTF = {"r": "Regular", "b": "Bold", "i": "Italic", "bi": "BoldItalic"}
SIZES = [6, 7, 8, 9, 10, 12, 14, 16, 20, 24]
# Sizes with a TRUE italic strike; the other sizes' italic record shares the
# upright strike's data and sets flag bit 0 (the renderer synthesises).
ITALIC_SIZES = [14, 16]
# The CORE bank is what a 360KB disk carries; MORE is loaded on demand.
CORE_SIZES = [8, 10, 12]
FIRST, LAST = 32, 255

GLYPHNAMES = """space exclam quotedbl numbersign dollar percent ampersand quotesingle
parenleft parenright asterisk plus comma hyphen period slash zero one two three four five
six seven eight nine colon semicolon less equal greater question at A B C D E F G H I J K
L M N O P Q R S T U V W X Y Z bracketleft backslash bracketright asciicircum underscore
grave a b c d e f g h i j k l m n o p q r s t u v w x y z braceleft bar braceright
asciitilde bullet Euro bullet quotesinglbase florin quotedblbase ellipsis dagger daggerdbl
circumflex perthousand Scaron guilsinglleft OE bullet Zcaron bullet bullet quoteleft
quoteright quotedblleft quotedblright bullet endash emdash tilde trademark scaron
guilsinglright oe bullet zcaron Ydieresis space exclamdown cent sterling currency yen
brokenbar section dieresis copyright ordfeminine guillemotleft logicalnot hyphen
registered macron degree plusminus twosuperior threesuperior acute mu paragraph
periodcentered cedilla onesuperior ordmasculine guillemotright onequarter onehalf
threequarters questiondown Agrave Aacute Acircumflex Atilde Adieresis Aring AE Ccedilla
Egrave Eacute Ecircumflex Edieresis Igrave Iacute Icircumflex Idieresis Eth Ntilde
Ograve Oacute Ocircumflex Otilde Odieresis multiply Oslash Ugrave Uacute Ucircumflex
Udieresis Yacute Thorn germandbls agrave aacute acircumflex atilde adieresis aring ae
ccedilla egrave eacute ecircumflex edieresis igrave iacute icircumflex idieresis eth
ntilde ograve oacute ocircumflex otilde odieresis divide oslash ugrave uacute ucircumflex
udieresis yacute thorn ydieresis""".split()
assert len(GLYPHNAMES) == LAST - FIRST + 1, len(GLYPHNAMES)

def uni(code):
    """Unicode char a Windows-1252 code draws; undefined codes draw a bullet."""
    if code in (127, 129, 141, 143, 144, 157):
        return "•"
    return bytes([code]).decode("cp1252")

def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()

def get(url, dest, want):
    if os.path.exists(dest) and sha(dest) == want:
        return
    print("fetch", url)
    data = urllib.request.urlopen(url, timeout=60).read()
    if hashlib.sha256(data).hexdigest() != want:
        sys.exit("SHA-256 mismatch for " + url)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    open(dest, "wb").write(data)

def cmd_fetch(a):
    tgz = os.path.join(FETCH, "liberation.tgz")
    get(LIB_URL, tgz, LIB_SHA)
    with tarfile.open(tgz) as t:
        t.extractall(os.path.join(FETCH, "lib"), filter="data")
    for n, h in AFMS.items():
        get(AFM_BASE + n + ".afm", os.path.join(FETCH, "afm", n + ".afm"), h)
    get(AFM_BASE + MUSTREAD[0], os.path.join(FETCH, "afm", MUSTREAD[0]), MUSTREAD[1])
    print("fetched into", FETCH)

# ---------------------------------------------------------------- strike text
def strike_name(fam, st, size):
    return "%s-%s-%02d.t88" % (fam, st, size)

def ttf_path(fam, st):
    return os.path.join(FETCH, "lib", LIB_DIR, "%s-%s.ttf" % (TTF[fam], STYLETTF[st]))

def raster(font, code, size):
    """-> (w, lb, rows) ink-trimmed; rows are strings of '.'/'#', baseline-relative
    top is returned separately as asc-above (rows above baseline count)."""
    from PIL import Image, ImageDraw
    pad = size
    img = Image.new("L", (size * 4, size * 4), 0)
    d = ImageDraw.Draw(img)
    d.fontmode = "1"
    ox, oy = pad, size * 2
    d.text((ox, oy), uni(code), font=font, fill=255, anchor="ls")
    px = img.load()
    W, H = img.size
    bm = [[1 if px[x, y] > 127 else 0 for x in range(W)] for y in range(H)]
    ys = [y for y in range(H) if any(bm[y])]
    if not ys:
        return 0, 0, 0, []
    xs = [x for x in range(W) if any(bm[y][x] for y in range(H))]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    rows = ["".join("#" if bm[y][x] else "." for x in range(x0, x1 + 1))
            for y in range(y0, y1 + 1)]
    return x1 - x0 + 1, x0 - ox, oy - y0, rows   # w, lb, rows-above-baseline of top row, rows

def cmd_draw(a):
    from PIL import ImageFont
    os.makedirs(FONTS, exist_ok=True)
    total = 0
    for fam in FAMILIES:
        for st in STYLES:
            for size in SIZES:
                if st in ("i", "bi") and size not in ITALIC_SIZES:
                    continue
                path = ttf_path(fam, st)
                if not os.path.exists(path):
                    sys.exit("missing %s - run: os88pdfont.py fetch" % path)
                font = ImageFont.truetype(path, size, layout_engine=ImageFont.Layout.BASIC)
                gl = {c: raster(font, c, size) for c in range(FIRST, LAST + 1)}
                asc = max([g[2] for g in gl.values() if g[3]] + [1])
                desc = max([len(g[3]) - g[2] for g in gl.values() if g[3]] + [0])
                out = ["# PageDesk document face strike (docs/PAGEDESK-SPEC.md 11.2).",
                       "# Drawn once by: python3 tools/os88pdfont.py draw   (Pillow/FreeType, mono hinting)",
                       "# SOURCE   %s-%s.ttf, Liberation Fonts 2.1.5 (SIL OFL 1.1), see fonts/LICENSES.txt"
                       % (TTF[fam], STYLETTF[st]),
                       "# THE ART IS THE SOURCE from here on: hand-fix a glyph below and `build` packs it.",
                       "# glyph <code> w <width> lb <left bearing> top <rows above baseline of the first ink row>",
                       "# followed by its ink rows ('#' ink, '.' paper); absent rows are paper; w 0 = blank.",
                       "family %s" % fam, "style %s" % st, "size %d" % size,
                       "ascent %d" % asc, "descent %d" % desc, ""]
                for c in range(FIRST, LAST + 1):
                    w, lb, top, rows = gl[c]
                    out.append("glyph %d w %d lb %d top %d" % (c, w, lb, top))
                    out.extend(rows)
                text = "\n".join(out) + "\n"
                open(os.path.join(FONTS, strike_name(fam, st, size)), "w").write(text)
                total += len(text)
    print("strike sources: %d bytes of text" % total)
    cmd_afm(a)

# --------------------------------------------------------------------- widths
def parse_afm(path):
    w = {}
    for line in open(path, encoding="latin-1"):
        if line.startswith("C "):
            f = [p.strip() for p in line.split(";")]
            wx = int(f[1].split()[1])
            name = f[2].split()[1]
            w[name] = wx
    return w

def cmd_afm(a):
    out = ["# Adobe Core14 AFM advance widths (1/1000 em), codes 32..255 in Windows-1252",
           "# order, one face per block, 16 codes a row.  Extracted by",
           "#   python3 tools/os88pdfont.py draw      (from the pinned AFMs, see fonts/AFM-NOTICE.txt)",
           "# Source: Apache PDFBox a0e38de6 pdfbox/src/main/resources/.../afm/<face>.afm, SHA-256 in",
           "# tools/os88pdfont.py.  Copyright (c) Adobe Systems Incorporated; the notice travels with",
           "# this file (AFM-NOTICE.txt).  A code whose glyph name has no AFM entry is 0.", ""]
    for fam in FAMILIES:
        for st in STYLES:
            n = AFMNAME[fam][0] + AFMNAME[fam][STYLEBITS[st]] if STYLEBITS[st] else AFMNAME[fam][0]
            if fam == "serif" and st == "r":
                n = "Times-Roman"
            if STYLEBITS[st] == 0 and fam != "serif":
                n = AFMNAME[fam][0]
            elif STYLEBITS[st]:
                n = AFMNAME[fam][0] + AFMNAME[fam][STYLEBITS[st]]
            path = os.path.join(FETCH, "afm", n + ".afm")
            if not os.path.exists(path):
                sys.exit("missing %s - run: os88pdfont.py fetch" % path)
            w = parse_afm(path)
            miss = [g for g in GLYPHNAMES if g not in w]
            if miss:
                sys.exit("%s lacks %s" % (n, miss))
            vals = [w[g] for g in GLYPHNAMES]
            out.append("face %s %s %s" % (fam, st, n))
            for i in range(0, len(vals), 16):
                out.append(" ".join(str(v) for v in vals[i:i + 16]))
            out.append("")
    open(os.path.join(FONTS, "widths.txt"), "w").write("\n".join(out))
    html = open(os.path.join(FETCH, "afm", MUSTREAD[0]), encoding="latin-1").read()
    import re
    para = re.search(r"This file and the 14.*?AFM files\.", html, re.S).group(0)
    open(os.path.join(FONTS, "AFM-NOTICE.txt"), "w").write(
        "Adobe Core 14 AFM files - notice (from the MustRead.html that accompanies them,\n"
        "SHA-256 %s)\n\n%s\n\n"
        "Copyright (c) 1985-1997 Adobe Systems Incorporated.  All Rights Reserved.\n"
        "PostScript is a registered trademark of Adobe Systems Incorporated.\n\n"
        "os8088 note: this tree does not distribute the AFM files themselves.  It carries\n"
        "only the advance-width numbers extracted from them (widths.txt), packed into\n"
        "PDRES.BIN.  That extraction is a modification, and is prominently noted here and in\n"
        "widths.txt's header.\n" % (MUSTREAD[1], para))
    print("widths.txt written")

# ---------------------------------------------------------------------- build
def parse_strike(path):
    meta = {}
    glyphs = {}
    cur = None
    for line in open(path):
        line = line.rstrip("\n")
        if not line or line.startswith("# "):
            continue
        p = line.split()
        if p[0] == "glyph":
            code = int(p[1])
            cur = {"w": int(p[3]), "lb": int(p[5]), "top": int(p[7]), "rows": []}
            glyphs[code] = cur
        elif p[0] in ("family", "style", "size", "ascent", "descent"):
            meta[p[0]] = p[1]
        else:
            assert cur is not None and set(line) <= set(".#") and len(line) == cur["w"], \
                "%s: bad row %r" % (path, line)
            cur["rows"].append(line)
    for k in ("family", "style", "size", "ascent", "descent"):
        assert k in meta, "%s lacks %s" % (path, k)
    return meta, glyphs

def pack_strike(meta, glyphs):
    asc, desc = int(meta["ascent"]), int(meta["descent"])
    nrows = asc + desc
    recs = b""
    bits = bytearray()
    for c in range(FIRST, LAST + 1):
        g = glyphs[c]
        w = g["w"]
        if w == 0:
            recs += struct.pack("<BbH", 0, 0, 0)
            continue
        bpr = (w + 7) // 8
        first = asc - g["top"]
        assert 0 <= first and first + len(g["rows"]) <= nrows, \
            "glyph %d does not fit %s rows" % (c, meta)
        off = len(bits)
        assert off < 65536, "strike bitmap over 64KB"
        recs += struct.pack("<BbH", w, g["lb"], off)
        for y in range(nrows):
            r = y - first
            row = g["rows"][r] if 0 <= r < len(g["rows"]) else "." * w
            v = int(row.ljust(bpr * 8, ".").replace("#", "1").replace(".", "0"), 2)
            bits += v.to_bytes(bpr, "big")
    return recs + bytes(bits)

def load_strikes():
    S = {}
    for fam in FAMILIES:
        for st in STYLES:
            for size in SIZES:
                p = os.path.join(FONTS, strike_name(fam, st, size))
                if os.path.exists(p):
                    S[(fam, st, size)] = parse_strike(p)
    return S

def collect_strikes(sizes):
    """-> (records, blobs): records (fam, style, size, asc, desc, flags, blob index)"""
    S = load_strikes()
    blobs, recs, cache = [], [], {}
    for fam in FAMILIES:
        for size in sizes:
            for st in STYLES:
                key = (fam, st, size)
                flags = 0
                if key not in S:               # synthesised italic: share the upright strike
                    key = (fam, st.replace("i", "") or "r", size)
                    if key not in S:
                        key = (fam, "r", size)
                    flags = 1
                meta, gl = S[key]
                if key not in cache:
                    cache[key] = len(blobs)
                    d = pack_strike(meta, gl)
                    blobs.append(d + b"\0" * (len(d) % 2))
                recs.append((fam, STYLEBITS[st], size, int(meta["ascent"]), int(meta["descent"]),
                             flags, cache[key]))
    return recs, blobs

def build_widths():
    faces = {}
    cur = None
    for line in open(os.path.join(FONTS, "widths.txt")):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("face "):
            p = line.split()
            cur = faces.setdefault((p[1], p[2]), [])
        else:
            cur.extend(int(x) for x in line.split())
    out = b"PDWD" + struct.pack("<HH", 1, 12)
    for fam in FAMILIES:
        for st in STYLES:
            v = faces[(fam, st)]
            assert len(v) == LAST - FIRST + 1
            out += struct.pack("<%dH" % len(v), *v)
    return out        # 8 + 12*224*2; face index = family*4 + style, style 0..3 as bank bytes

def build_names():
    """Adobe glyph name -> Windows-1252 code, sorted bytewise: {u8 len, name, u8 code}*, 0."""
    m = {}
    for i, n in enumerate(GLYPHNAMES):
        code = FIRST + i
        if n in ("space", "hyphen", "bullet"):
            code = {"space": 32, "hyphen": 45, "bullet": 149}[n]
        m.setdefault(n, code)
    out = b""
    for n in sorted(m, key=lambda x: x.encode()):
        out += bytes([len(n)]) + n.encode() + bytes([m[n]])
    return out + b"\0"

K_FONTS, K_WIDTHS, K_ICONS, K_NAMES, K_STRIKES, K_INDEX = 1, 3, 4, 5, 6, 7
def build_res(core_only=True, art=True):
    """Layout (D3): directory | PDFB bank (header, strike records, PDFX, widths, names)
    | strike data | icons.  The first INDEX bytes are what the primary loads."""
    recs, blobs = collect_strikes(CORE_SIZES if core_only else [x for x in SIZES if x not in CORE_SIZES])
    n = len(recs)
    nent = 6
    D = (8 + 12 * nent + 15) & ~15                    # the bank starts paragraph aligned
    wid = build_widths() if core_only else b""        # PDFONTX.RES leaves them to PDRES.BIN
    names = build_names() if core_only else b""
    widths_off = 16 * n + 8 + 8 if core_only else 0   # from the bank start (0 = absent)
    names_off = widths_off + len(wid) if core_only else 0
    tail = 16 * n + 8 + 8
    bank_len = tail + len(wid) + len(names)
    index_len = D + bank_len
    data_off = (index_len + 15) & ~15
    offs, o = [], data_off
    for d in blobs:
        offs.append(o)
        o += len(d)
    strikes = b"".join(blobs)
    icons_off = (o + 15) & ~15
    icons = build_atlas_safe(art and core_only)
    bank = b"PDFB" + struct.pack("<HH", 1, n)
    for fam, st, size, asc, desc, flags, bi in recs:
        bank += struct.pack("<BBBBBBBBII", FAMILIES.index(fam), st, size, asc, desc, FIRST, LAST,
                            flags, offs[bi], len(blobs[bi]))
    bank += b"PDFX" + struct.pack("<HH", widths_off, names_off)
    bank += wid + names
    assert len(bank) == bank_len
    ent = [(K_FONTS, D, bank_len), (K_WIDTHS, D + widths_off if wid else 0, len(wid)),
           (K_NAMES, D + names_off if names else 0, len(names)), (K_STRIKES, data_off, len(strikes)),
           (K_ICONS, icons_off, len(icons)), (K_INDEX, 0, index_len)]
    assert len(ent) == nent
    head = b"PDRS" + struct.pack("<HH", 1, nent)
    for k, off, ln in ent:
        head += struct.pack("<HHII", k, 0, off, ln)
    head += b"\0" * (D - len(head))
    blob = head + bank + b"\0" * (data_off - index_len) + strikes
    blob += b"\0" * (icons_off - len(blob)) + icons
    parts = {"index": index_len, "dir": D, "strike records": 16 * n, "widths": len(wid),
             "names": len(names), "strike data": len(strikes), "icons": len(icons)}
    return blob, parts

def build_atlas_safe(art):
    if not art:
        return b""
    import os88pdart
    return os88pdart.build_atlas()

def cmd_build(a):
    blob, parts = build_res(core_only=not a.extra)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    open(a.out, "wb").write(blob)
    print("PDRES.BIN %d bytes%s: " % (len(blob), " (extra sizes, no icons)" if a.extra else " (core sizes 8/10/12)")
          + ", ".join("%s %d" % kv for kv in parts.items()))

# ---------------------------------------------------------------- proof/check
def read_res(blob):
    assert blob[:4] == b"PDRS"
    ver, n = struct.unpack_from("<HH", blob, 4)
    ents = {}
    for i in range(n):
        k, f, o, l = struct.unpack_from("<HHII", blob, 8 + 12 * i)
        ents[k] = (o, l)
    return ents

def bank_strikes(blob):
    ents = read_res(blob)
    D = ents[1][0]
    assert blob[D:D + 4] == b"PDFB"
    n = struct.unpack_from("<H", blob, D + 6)[0]
    for i in range(n):
        fam, st, size, asc, desc, f, l, flags, off, ln = struct.unpack_from("<BBBBBBBBII", blob, D + 8 + 16 * i)
        yield dict(fam=fam, st=st, size=size, asc=asc, desc=desc, first=f, last=l,
                   flags=flags, data=blob[off:off + ln])

def blit_text(strike, text, widths_scale=None):
    """-> list of rows (strings) drawing text with the bitmap's own advance."""
    asc, desc = strike["asc"], strike["desc"]
    nr = asc + desc
    d = strike["data"]
    n = strike["last"] - strike["first"] + 1
    base = 4 * n
    canvas = [[0] * (len(text) * (strike["size"] + 2) + 8) for _ in range(nr)]
    x = 2
    for ch in text:
        c = ord(ch.encode("cp1252")) if ch != "•" else 149
        w, lb, off = struct.unpack_from("<BbH", d, 4 * (c - strike["first"]))
        bpr = (w + 7) // 8
        for y in range(nr):
            for xx in range(w):
                byte = d[base + off + y * bpr + xx // 8]
                if byte >> (7 - xx % 8) & 1:
                    canvas[y][x + lb + xx] = 1
        x += max(w + lb, 0) + 1 + (strike["size"] // 4 if w == 0 else 1)
    return canvas

def cmd_proof(a):
    from PIL import Image
    out = a.out
    os.makedirs(out, exist_ok=True)
    sample = "Hamburgefonts 0123 The quick brown fox jumps over lazy dogs. \u00c9\u00e9\u00fc"
    for name, path in (("core", a.res), ("more", a.extra)):
        blob = open(path, "rb").read()
        pick = lambda s: True
        rows = []
        for s in bank_strikes(blob):
            if s["flags"] & 1 or not pick(s):
                continue
            rows.append(blit_text(s, sample))
        W = max(len(r[0]) for r in rows)
        H = sum(len(r) + 3 for r in rows)
        img = Image.new("L", (W, H), 255)
        px = img.load()
        y0 = 0
        for c in rows:
            for y, row in enumerate(c):
                for x, v in enumerate(row):
                    if v:
                        px[x, y0 + y] = 0
            y0 += len(c) + 3
        img = img.convert("1").resize((W * 2, H * 2), Image.NEAREST)
        img.save(os.path.join(out, "strikes-%s.png" % name))
        print("strikes-%s.png %dx%d" % (name, W, H))
    import os88pdart
    os88pdart.proof(out)

def cmd_check(a):
    blob = open(a.res, "rb").read()
    ents = read_res(blob)
    D = ents[1][0]
    assert D % 16 == 0
    n = struct.unpack_from("<H", blob, D + 6)[0]
    assert blob[D + 8 + 16 * n:D + 12 + 16 * n] == b"PDFX"
    wo, no = struct.unpack_from("<HH", blob, D + 12 + 16 * n)
    full = ents[3][1] != 0
    if full:
        assert D + wo == ents[3][0] and D + no == ents[5][0]
        assert ents[3][1] == 12 * 224 * 2 + 8
    else:
        assert wo == 0 and no == 0
    assert ents[7] == (0, ents[7][1]) and ents[7][1] <= ents[6][0]
    for s in bank_strikes(blob):
        assert s["first"] == 32 and s["last"] == 255 and len(s["data"]) >= 4 * 224
    nm = blob[ents[5][0]:ents[5][0] + ents[5][1]]
    i, prev = 0, b""
    while full and nm[i]:
        l = nm[i]; name = nm[i + 1:i + 1 + l]; assert name > prev; prev = name; i += l + 2
    assert i + 1 == len(nm) or not full
    print("%s ok: index %d bytes (limit 8192)%s" % (os.path.basename(a.res), ents[7][1], "" if ents[7][1] <= 8192 else "  OVER 8 KiB"))

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("fetch").set_defaults(f=cmd_fetch)
    sub.add_parser("draw").set_defaults(f=cmd_draw)
    b = sub.add_parser("build"); b.add_argument("-o", "--out", default=os.path.join(ROOT, "build", "PDRES.BIN")); b.add_argument("--extra", action="store_true", help="PDFONTX.RES: the sizes PDRES.BIN does not carry"); b.set_defaults(f=cmd_build)
    pr = sub.add_parser("proof"); pr.add_argument("--res", default=os.path.join(ROOT, "build", "PDRES.BIN"))
    pr.add_argument("--extra", default=os.path.join(ROOT, "build", "PDFONTX.RES")); pr.add_argument("-o", "--out", default=os.path.expanduser("~/Repos/pagedesk-logs/shots/fonts")); pr.set_defaults(f=cmd_proof)
    c = sub.add_parser("check"); c.add_argument("--res", default=os.path.join(ROOT, "build", "PDRES.BIN")); c.set_defaults(f=cmd_check)
    a = p.parse_args()
    a.f(a)

if __name__ == "__main__":
    main()

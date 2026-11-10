#!/usr/bin/env python3
"""os88pdkit.py - PageDesk's face kit (docs/PAGEDESK-SPEC.md 20.21.2).

The bars and panels of PageDesk's face draw their captions and pictures out of
ONE block, PDRES kind 9 ('PDUK'), which os88pdfont.py's `build` places right
after the INDEX so the primary's one read at launch loads both:

    'PDUK', u16 version 1, u16 font count f, u16 icon count n
    f x 8 bytes {u8 advance, u8 rows, u8 ascent, u8 glyph count, u16 offset, u16 0}
    n x 32 bytes: 16 rows of two bytes, bit 7 of the first the leftmost pixel
    font data: glyph count x rows bytes, bit 7 leftmost, 1 = ink

Fonts, from the committed Liberation Mono strikes (20.4.2) - nothing is drawn
here that the strikes do not already say, except the five drawing marks:

    0  mono bold 10 px     captions                advance 6
    1  mono regular 10 px  text (cards, notes)     advance 6
    2  mono bold 12 px     panel titles            advance 7

Glyph k is code 32 + k for k < 95; then the marks at CP437's codes, so the
kernel's 8x8 face (the face without the kit) draws the same thing:
0x10 right triangle, 0x11 left triangle, 0x1F down triangle, 0xFA middle dot,
0x9E a multiplication cross.

    python3 tools/os88pdkit.py --selfcheck
    python3 tools/os88pdkit.py --header build/pdkit.h
    python3 tools/os88pdkit.py --png DIR        (needs Pillow) previews, x4
"""
import argparse, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ART = os.path.join(ROOT, "apps", "pagedesk", "art", "face.kit")
sys.path.insert(0, HERE)

FONTS = [("mono", "b", 10, 6), ("mono", "r", 10, 6), ("mono", "b", 12, 7)]
MARKS = [0x10, 0x11, 0x1F, 0xFA, 0x9E]          # glyphs 95..99
NGLYPH = 95 + len(MARKS)
KIT_MAX = 4608                                   # PK_KIT_MAX in build/pdkit.h: the
                                                 # primary's read past the INDEX


# ------------------------------------------------------------------- icons --
def load_art(path=ART):
    """-> [(name, [16 strings of 16])] in file order"""
    icons, names, cur = [], {}, None
    for n, line in enumerate(open(path), 1):
        line = line.rstrip("\n")
        if not line or line == "#" or line.startswith("# "):
            if cur is not None and len(cur[1]) not in (0, 16):
                raise SystemExit("%s:%d: icon %s has %d rows" % (path, n, cur[0], len(cur[1])))
            continue
        p = line.split()
        if p[0] == "icon":
            cur = (p[1], [])
            icons.append(cur)
        elif p[0] == "mirror":
            src = [r for k, r in icons if k == p[2]]
            if not src:
                raise SystemExit("%s:%d: mirror of unknown %s" % (path, n, p[2]))
            icons.append((p[1], [r[::-1] for r in src[0]]))
            cur = None
        else:
            if cur is None or set(line) - set("#.") or len(line) != 16:
                raise SystemExit("%s:%d: bad row %r" % (path, n, line))
            cur[1].append(line)
    for k, rows in icons:
        if len(rows) != 16:
            raise SystemExit("%s: icon %s has %d rows" % (path, k, len(rows)))
        if k in names:
            raise SystemExit("%s: icon %s twice" % (path, k))
        names[k] = 1
    return icons


def pack_icon(rows):
    out = b""
    for r in rows:
        out += int(r.replace("#", "1").replace(".", "0"), 2).to_bytes(2, "big")
    return out


# ------------------------------------------------------------------- fonts --
def marks(adv, rows, base, xh):
    """the five drawing marks, sized to the face: base = the baseline row,
    xh = the x-height in rows. -> {code: [rows strings of adv]}"""
    def blank():
        return [["."] * adv for _ in range(rows)]
    out = {}
    mid = base - xh // 2                         # the x-height's middle row
    h = 5 if adv >= 6 else 3
    # right / left triangles: h rows tall, (h+1)/2 wide, centred on mid
    for code, right in ((0x10, True), (0x11, False)):
        g = blank()
        w = (h + 1) // 2
        x0 = (adv - w) // 2
        for i in range(h):
            k = min(i, h - 1 - i) + 1
            for j in range(k):
                x = x0 + j if right else x0 + w - 1 - j
                g[mid - h // 2 + i][x] = "#"
        out[code] = g
    # down triangle: 5 wide, 3 tall, its middle row on mid
    g = blank()
    w = min(adv - 1, 5)
    x0 = (adv - w) // 2
    for i in range((w + 1) // 2):
        for j in range(i, w - i):
            g[mid - 1 + i][x0 + j] = "#"
    out[0x1F] = g
    # middle dot: 2x2 on mid
    g = blank()
    x0 = (adv - 2) // 2
    for i in (0, 1):
        for j in (0, 1):
            g[mid - 1 + i][x0 + j] = "#"
    out[0xFA] = g
    # cross: a w x w X centred on mid
    g = blank()
    w = min(adv - 1, 5)
    x0 = (adv - w) // 2
    for i in range(w):
        g[mid - w // 2 + i][x0 + i] = "#"
        g[mid - w // 2 + i][x0 + w - 1 - i] = "#"
    out[0x9E] = g
    return {c: ["".join(r) for r in g] for c, g in out.items()}


def build_font(fam, st, size, adv):
    """-> (rows, ascent, [NGLYPH lists of row strings])"""
    import os88pdfont as F
    meta, gl = F.parse_strike(os.path.join(F.FONTS, F.strike_name(fam, st, size)))
    asc = int(meta["ascent"])
    lo, hi = 99, -99
    for c in range(32, 127):
        g = gl[c]
        if g["w"]:
            top = asc - g["top"]
            lo, hi = min(lo, top), max(hi, top + len(g["rows"]) - 1)
    rows = hi - lo + 1
    base = asc - lo                              # the baseline, in cell rows
    if rows > 16:
        raise SystemExit("%s-%s-%d: %d rows, over the band's 16" % (fam, st, size, rows))
    glyphs = []
    for c in range(32, 127):
        g = gl[c]
        cell = [["."] * adv for _ in range(rows)]
        if g["w"]:
            lb = g["lb"]
            if lb < 0:
                lb = 0                           # '_' and 'Y' hang left a pixel
            if lb + g["w"] > adv:
                raise SystemExit("%s-%s-%d: glyph %d is %d wide at %d, over the advance %d"
                                 % (fam, st, size, c, g["w"], lb, adv))
            top = asc - g["top"] - lo
            for y, r in enumerate(g["rows"]):
                for x, ch in enumerate(r):
                    if ch == "#":
                        cell[top + y][lb + x] = "#"
        glyphs.append(["".join(r) for r in cell])
    xg = gl[ord("x")]
    xh = xg["top"] if xg["w"] else base // 2
    mk = marks(adv, rows, base, xh)
    for c in MARKS:
        glyphs.append(mk[c])
    return rows, base, glyphs


def pack_glyphs(glyphs):
    out = b""
    for g in glyphs:
        for r in g:
            out += int(r.ljust(8, ".").replace("#", "1").replace(".", "0"), 2).to_bytes(1, "big")
    return out


# --------------------------------------------------------------------- kit --
def build_kit():
    icons = load_art()
    fonts = [build_font(*f) for f in FONTS]
    head = b"PDUK" + struct.pack("<HHH", 1, len(fonts), len(icons))
    recs_len = 8 * len(fonts)
    off = len(head) + recs_len + 32 * len(icons)
    recs, data = b"", b""
    for (fam, st, size, adv), (rows, base, gl) in zip(FONTS, fonts):
        recs += struct.pack("<BBBBHH", adv, rows, base, len(gl), off + len(data), 0)
        data += pack_glyphs(gl)
    kit = head + recs + b"".join(pack_icon(r) for _, r in icons) + data
    if len(kit) > KIT_MAX:
        raise SystemExit("the kit is %d bytes, over PDU_KIT_MAX %d" % (len(kit), KIT_MAX))
    return kit


def header_text():
    icons = load_art()
    out = ["/* GENERATED by tools/os88pdkit.py from apps/pagedesk/art/face.kit -",
           " * do not edit. PageDesk's face kit (docs/PAGEDESK-SPEC.md 20.21.2). */",
           "#ifndef PDKIT_H", "#define PDKIT_H",
           "#define PK_NICONS %d" % len(icons),
           "#define PK_NFONTS %d" % len(FONTS),
           "#define PK_KIT_MAX %du  /* the read past the INDEX (pdumod.c) */" % KIT_MAX]
    for i, (k, _) in enumerate(icons):
        out.append("#define PK_I_%s %d" % (k.upper(), i))
    for i, (fam, st, size, adv) in enumerate(FONTS):
        out.append("#define PK_F_%s%s%d %d   /* advance %d */" % (fam.upper(), st.upper(), size, i, adv))
    out += ["#define PK_CAP  0  /* captions */", "#define PK_TXT  1  /* text */",
            "#define PK_TTL  2  /* titles */",
            "#define PK_SYS  3  /* the kernel's 8x8, always (pdkit.inc) */", "#endif", ""]
    return "\n".join(out)


def selfcheck():
    icons = load_art()
    kit = build_kit()
    assert kit[:4] == b"PDUK"
    ver, nf, ni = struct.unpack_from("<HHH", kit, 4)
    assert (ver, nf, ni) == (1, len(FONTS), len(icons))
    for i, (_, rows) in enumerate(icons):
        assert kit[10 + 8 * nf + 32 * i:10 + 8 * nf + 32 * i + 32] == pack_icon(rows)
        assert any("#" in r for r in rows), "icon %d is blank" % i
    seen = {}
    for i, (k, rows) in enumerate(icons):
        b = pack_icon(rows)
        assert b not in seen, "icons %s and %s are identical" % (seen.get(b), k)
        seen[b] = k
    for j in range(nf):
        adv, rows, base, ng, off, z = struct.unpack_from("<BBBBHH", kit, 10 + 8 * j)
        assert ng == NGLYPH and 0 < base <= rows <= 16 and adv <= 8 and z == 0
        assert off + ng * rows <= len(kit)
        cells = set()
        for g in range(1, 95):                   # every printable but space inked, distinct
            cell = kit[off + g * rows:off + (g + 1) * rows]
            assert any(cell), "font %d glyph %d is blank" % (j, 32 + g)
            assert cell not in cells, "font %d glyph %d repeats another" % (j, 32 + g)
            cells.add(cell)
        assert not any(kit[off:off + rows]), "font %d space is inked" % j
        for g in range(95, NGLYPH):
            assert any(kit[off + g * rows:off + (g + 1) * rows]), "font %d mark %d blank" % (j, g)
    print("os88pdkit: kit %d bytes, %d fonts, %d icons - ok" % (len(kit), nf, ni))


def previews(d):
    from PIL import Image
    os.makedirs(d, exist_ok=True)
    icons = load_art()
    im = Image.new("L", (18 * 8 + 2, 18 * ((len(icons) + 7) // 8) + 2), 200)
    for i, (_, rows) in enumerate(icons):
        ox, oy = 2 + 18 * (i % 8), 2 + 18 * (i // 8)
        for y, r in enumerate(rows):
            for x, ch in enumerate(r):
                im.putpixel((ox + x, oy + y), 0 if ch == "#" else 255)
    im.resize((im.width * 4, im.height * 4), Image.NEAREST).save(os.path.join(d, "kit-icons.png"))
    for j, f in enumerate(FONTS):
        rows, base, gl = build_font(*f)
        adv = f[3]
        txt = [chr(c) for c in range(32, 127)] + ["\x10", "\x11", "\x1f", "\xfa", "\x9e"]
        per = 34
        im = Image.new("L", (adv * per + 4, (rows + 2) * ((len(txt) + per - 1) // per) + 4), 200)
        for i, g in enumerate(gl):
            ox, oy = 2 + adv * (i % per), 2 + (rows + 2) * (i // per)
            for y, r in enumerate(g):
                for x, ch in enumerate(r):
                    im.putpixel((ox + x, oy + y), 0 if ch == "#" else 255)
        im.resize((im.width * 4, im.height * 4), Image.NEAREST).save(
            os.path.join(d, "kit-font%d.png" % j))
    print("previews in", d)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--header")
    ap.add_argument("--png")
    a = ap.parse_args()
    if a.selfcheck:
        selfcheck()
    if a.header:
        t = header_text()
        if not os.path.exists(a.header) or open(a.header).read() != t:
            open(a.header, "w").write(t)
    if a.png:
        previews(a.png)


if __name__ == "__main__":
    main()

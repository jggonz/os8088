#!/usr/bin/env python3
"""os88pdart.py - PageDesk's icons, cursors and document glyph (PAGEDESK-SPEC 11.6,
17.6, 20.4).  Pure Python.

The pixel sources are the hand-drawn text grids under apps/pagedesk/art/*.art.
  python3 tools/os88pdart.py build [--inc-dir DIR]   writes pdart_appicon.inc and
        pdart_docglyph.inc (icon.inc-style: `OS88_ICON16` / %include / `OS88_ICON16_END`
        and `OS88_DOCGLYPH8` / %include / `OS88_DOCGLYPH8_END`) and prints the sizes
  python3 tools/os88pdart.py check                   validates every grid
  python3 tools/os88pdart.py proof [-o DIR]          icon sheet PNG (needs Pillow)
tools/os88pdfont.py build imports build_atlas() to pack the PDIC part of PDRES.BIN.

Grid legend: '#' black, 'o' white (mask only), '.' transparent; `vga` block: one hex
digit a pixel (VGA 16 colours), '.' transparent.  1bpp data is therefore two values only.
"""
import argparse, glob, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ART = os.path.join(ROOT, "apps", "pagedesk", "art")

def parse():
    icons = []          # dict(name, kind, w, h, hot, mono rows, vga rows)
    for path in sorted(glob.glob(os.path.join(ART, "*.art"))):
        cur = None
        mode = None
        for ln, line in enumerate(open(path), 1):
            line = line.rstrip("\n")
            if not line or line.startswith("# "):
                continue
            p = line.split()
            if p[0] in ("icon", "glyph"):
                cur = dict(kind=p[0], name=p[1], w=int(p[2]), h=int(p[3]), hot=(0, 0),
                           mono=[], vga=[], src="%s:%d" % (os.path.basename(path), ln))
                if "hot" in p:
                    i = p.index("hot")
                    cur["hot"] = (int(p[i + 1]), int(p[i + 2]))
                icons.append(cur)
                mode = "mono"
            elif line == "vga":
                mode = "vga"
            else:
                assert cur, "%s:%d row outside an icon" % (path, ln)
                cur[mode].append(line)
    for ic in icons:
        for m in ("mono", "vga"):
            rows = ic[m]
            if rows:
                assert len(rows) == ic["h"], "%s: %s has %d rows" % (ic["src"], ic["name"], len(rows))
                for r in rows:
                    assert len(r) == ic["w"], "%s: %s row %r" % (ic["src"], ic["name"], r)
        legal = {"mono": ".#o", "vga": ".0123456789ABCDEFabcdef"}
        if ic["kind"] == "glyph":
            legal["mono"] = ".#"
        for m in legal:
            for r in ic[m]:
                assert set(r) <= set(legal[m]), "%s: %s bad pixel in %r" % (ic["src"], ic["name"], r)
        assert len(ic["name"]) <= 11
        assert ic["w"] <= 16 and ic["h"] <= 16 and ic["w"] % 8 == 0
    names = [i["name"] for i in icons]
    assert len(names) == len(set(names)), "duplicate icon name"
    return icons

def words(ic):
    """16-wide: (mask words, data words), bit 15 leftmost; 8-wide glyph: (None, bytes)."""
    m, d = [], []
    for r in ic["mono"]:
        r = r.ljust(16, ".")
        m.append(sum(1 << (15 - i) for i, c in enumerate(r) if c in "#o"))
        d.append(sum(1 << (15 - i) for i, c in enumerate(r) if c == "#"))
    return m, d

def mono_blob(ic):
    if ic["w"] == 8:
        return bytes(int(r.replace("#", "1").replace(".", "0"), 2) for r in ic["mono"])
    m, d = words(ic)
    return struct.pack("<%dH" % (2 * ic["h"]), *(m + d))

def vga_blob(ic):
    if not ic["vga"]:
        return b""
    mask = struct.pack("<%dH" % ic["h"], *[
        sum(1 << (15 - i) for i, c in enumerate(r.ljust(16, ".")) if c != ".") for r in ic["vga"]])
    px = bytearray()
    for r in ic["vga"]:
        for i in range(0, ic["w"], 2):
            a, b = r[i], r[i + 1]
            px.append((0 if a == "." else int(a, 16)) << 4 | (0 if b == "." else int(b, 16)))
    return mask + bytes(px)

def build_atlas():
    """PDIC: 'PDIC', u16 version 1, u16 count, count x 20-byte entries
         +0 name[12] NUL padded, +12 u8 w, +13 u8 h, +14 u8 hot x, +15 u8 hot y,
         +16 u16 mono offset, +18 u16 vga offset (0 none); offsets from the atlas start.
       mono: 16 wide = h mask words then h data words (OS88_ICON16's order, bit 15 left),
             8 wide (document glyph) = h bytes.   vga: h mask words (1 = opaque) then
             h rows of w/2 bytes, high nibble left."""
    icons = parse()
    n = len(icons)
    off = 8 + 20 * n
    ent, body = b"", b""
    for ic in icons:
        mo = off + len(body)
        body += mono_blob(ic)
        vo = 0
        vb = vga_blob(ic)
        if vb:
            vo = off + len(body)
            body += vb
        assert mo < 65536 and vo < 65536
        ent += struct.pack("<12sBBBBHH", ic["name"].encode(), ic["w"], ic["h"],
                           ic["hot"][0], ic["hot"][1], mo, vo)
    return b"PDIC" + struct.pack("<HH", 1, n) + ent + body

def read_atlas(blob):
    n = struct.unpack_from("<H", blob, 6)[0]
    out = {}
    for i in range(n):
        nm, w, h, hx, hy, mo, vo = struct.unpack_from("<12sBBBBHH", blob, 8 + 20 * i)
        out[nm.rstrip(b"\0").decode()] = dict(w=w, h=h, hot=(hx, hy), mono=mo, vga=vo)
    return out

def write_inc(d):
    icons = {i["name"]: i for i in parse()}
    os.makedirs(d, exist_ok=True)
    m, dd = words(icons["appicon"])
    with open(os.path.join(d, "pdart_appicon.inc"), "w") as f:
        f.write("; generated by tools/os88pdart.py from apps/pagedesk/art/app.art - do not edit\n")
        f.write("; 16 mask words (white underlay) then 16 data words (black), bit 15 leftmost\n")
        for blk in (m, dd):
            for i in range(0, 16):
                f.write("        dw 0x%04X\n" % blk[i])
    g = icons["docglyph"]
    with open(os.path.join(d, "pdart_docglyph.inc"), "w") as f:
        f.write("; generated by tools/os88pdart.py from apps/pagedesk/art/app.art - do not edit\n")
        f.write("; the 8x8 document glyph, one byte a row, bit 7 leftmost, 1 = ink\n")
        for b in mono_blob(g):
            f.write("        db 0x%02X\n" % b)

def proof(out):
    from PIL import Image
    icons = parse()
    cols = 8
    cell = 40
    rows = (len(icons) + cols - 1) // cols
    pal = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0), (170, 0, 170),
           (170, 85, 0), (170, 170, 170), (85, 85, 85), (85, 85, 255), (85, 255, 85),
           (85, 255, 255), (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]
    for variant in ("mono", "vga"):
        img = Image.new("RGB", (cols * cell, rows * cell * 1), (128, 128, 128))
        px = img.load()
        for k, ic in enumerate(icons):
            ox, oy = (k % cols) * cell + 4, (k // cols) * cell + 4
            src = ic["vga"] if variant == "vga" and ic["vga"] else ic["mono"]
            isv = variant == "vga" and ic["vga"]
            for y, r in enumerate(src):
                for x, c in enumerate(r):
                    if c == ".":
                        continue
                    if isv:
                        col = pal[int(c, 16)]
                    else:
                        col = (0, 0, 0) if c == "#" else (255, 255, 255)
                    px[ox + x, oy + y] = col
        img = img.resize((img.width * 3, img.height * 3), Image.NEAREST)
        img.save(os.path.join(out, "icons-%s.png" % variant))
        print("icons-%s.png" % variant)

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build"); b.add_argument("--inc-dir", default=os.path.join(ROOT, "build"))
    sub.add_parser("check")
    pr = sub.add_parser("proof"); pr.add_argument("-o", "--out", default=os.path.expanduser("~/Repos/pagedesk-logs/shots/fonts"))
    a = p.parse_args()
    if a.cmd == "check":
        ic = parse()
        blob = build_atlas()
        # only two pixel values in every 1bpp asset: data is subset of mask, and the
        # converter only ever emits those two planes
        for i in ic:
            if i["w"] == 16:
                m, d = words(i)
                assert all(dd & ~mm == 0 for mm, dd in zip(m, d))
        print("art ok: %d assets, atlas %d bytes" % (len(ic), len(blob)))
    elif a.cmd == "build":
        write_inc(a.inc_dir)
        print("atlas %d bytes; includes in %s" % (len(build_atlas()), a.inc_dir))
    elif a.cmd == "proof":
        os.makedirs(a.out, exist_ok=True)
        proof(a.out)

if __name__ == "__main__":
    main()

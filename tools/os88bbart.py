#!/usr/bin/env python3
"""os88bbart.py - Breadboard XT's face art (SPEC.md 130.6.3).

THE TEXT ART IS THE SOURCE. `apps/breadxt/art/sprites.txt` holds every
picture as rows of one character per EGA colour, and the type is WIN16's
committed font art (`apps/win16/fonts/*.txt`), shared as source rather than
copied (Wine's System and MS Sans Serif strikes, LGPL-2.1-or-later).

    draft MASTER.png -o sprites.txt   quantise the codex master's cells once
    build -o BBART.BIN                pack sprites.txt, fonts, help.txt (make)
    --selfcheck                       every ID once, sizes, no key colour
    png DIR                           zoomed previews of every sprite + fonts

BBART.BIN, all little-endian:
    'BA', count, then count words: offset of each art.h ID (0 = absent)
    sprite: db w, db h, rows of packed 4bpp (hi nibble left), stride (w+1)/2,
            colour 13 (light magenta) = transparent
    text:   NUL-terminated lines, in order, then an empty one (BA_HELP)
    font:   db height, db ascent, db first (32), db n (95), n widths,
            n words: glyph offset from the font record (0 = absent),
            each glyph db top, db rows (its first and count of non-blank
            rows), then each row as TWO bytes, MSB = the leftmost pixel - so
            the drawing core reads a row whole and never walks bits
Only sprites.txt, art.h and the font art are read by `build`; no image file,
no network, so make needs nothing but python3.
"""
import argparse, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ART_H = os.path.join(ROOT, "apps", "breadxt", "art.h")
SPRITES = os.path.join(ROOT, "apps", "breadxt", "art", "sprites.txt")
HELP = os.path.join(ROOT, "apps", "breadxt", "art", "help.txt")
TEXTS = {"BA_HELP": HELP}
FONTDIR = os.path.join(ROOT, "apps", "win16", "fonts")

# One character per EGA index; '.' is index 13, the transparency key.
CHARS = "kbgcrmnldBGCR.yw"
EGA = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0),
       (170, 0, 170), (170, 85, 0), (170, 170, 170), (85, 85, 85),
       (85, 85, 255), (85, 255, 85), (85, 255, 255), (255, 85, 85),
       (255, 85, 255), (255, 255, 85), (255, 255, 255)]
KEY = 13

# ID -> font art file. TEXT faces are the dialog (MS Sans Serif bold)
# strikes; BIG are the System strikes.
FONTS = {"BA_F_TEXT13": "dlg13", "BA_F_TEXT10": "dlg10",
         "BA_F_TEXT08": "dlg08", "BA_F_BIG16": "sys16",
         "BA_F_BIG12": "sys12"}

# The codex master's 8x8 grid (art/PROMPT.md): (row, col) -> ID, w, h.
MASTER = {
    (0, 0): ("BA_T_SELECT", 16, 16), (0, 1): ("BA_T_WIRE", 16, 16),
    (0, 2): ("BA_T_PROBE", 16, 16), (0, 3): ("BA_T_ROTATE", 16, 16),
    (0, 4): ("BA_T_UNDO", 16, 16), (0, 5): ("BA_T_RUN", 16, 16),
    (0, 6): ("BA_T_PAUSE", 16, 16), (0, 7): ("BA_T_STEP", 16, 16),
    (1, 0): ("BA_T_RESET", 16, 16), (1, 1): ("BA_DOC", 12, 14),
    (1, 2): ("BA_WAVE", 20, 11), (1, 3): ("BA_GROUND", 16, 12),
    (1, 4): ("BA_POWER", 20, 10), (1, 5): ("BA_CLOCK", 18, 12),
    (1, 6): ("BA_INPUT", 16, 14), (1, 7): ("BA_LAMP", 14, 14),
    (2, 0): ("BA_P_WIRE", 24, 12), (2, 1): ("BA_P_RES", 24, 12),
    (2, 2): ("BA_P_CAP", 24, 12), (2, 3): ("BA_P_LED", 24, 14),
    (2, 4): ("BA_P_SWITCH", 24, 14), (2, 5): ("BA_P_555", 24, 14),
    (2, 6): ("BA_P_IC", 28, 14), (2, 7): ("BA_P_POT", 24, 14),
    (3, 0): ("BA_B_RESH", 20, 7), (3, 1): ("BA_B_RESV", 7, 20),
    (3, 2): ("BA_B_ECAP", 12, 16), (3, 3): ("BA_B_CCAP", 10, 10),
    (3, 4): ("BA_B_LEDOFF", 10, 12), (3, 5): ("BA_B_LEDON", 10, 12),
    (3, 6): ("BA_B_SWOFF", 14, 8), (3, 7): ("BA_B_SWON", 14, 8),
    (4, 0): ("BA_I_RES", 48, 14), (4, 1): ("BA_B_POT", 12, 12),
    (4, 2): ("BA_B_INPUT", 10, 10), (4, 3): ("BA_B_LAMPOFF", 10, 10),
    (4, 4): ("BA_B_LAMPON", 10, 10), (4, 5): ("BA_B_RAYS", 16, 14),
}


def fail(msg):
    sys.exit(f"os88bbart: {msg}")


def read_ids():
    ids = []
    for m in re.finditer(r"^#define (BA_\w+) (\d+)$", open(ART_H).read(), re.M):
        name, n = m.group(1), int(m.group(2))
        if name == "BA_COUNT":
            if n != len(ids):
                fail(f"art.h: BA_COUNT is {n}, {len(ids)} IDs precede it")
            return ids
        if n != len(ids):
            fail(f"art.h: {name} is {n}, expected {len(ids)} (IDs are dense, in order)")
        ids.append(name)
    fail("art.h: no BA_COUNT")


def read_sprites(path=SPRITES):
    """sprite NAME W H / H rows of W characters."""
    out, lines = {}, open(path).read().split("\n")
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        i += 1
        if not line or line.startswith("#"):
            continue
        m = re.match(r"sprite (BA_\w+) (\d+) (\d+)$", line)
        if not m:
            fail(f"{path}:{i}: expected `sprite NAME W H`, got {line!r}")
        name, w, h = m.group(1), int(m.group(2)), int(m.group(3))
        if name in out:
            fail(f"{path}:{i}: {name} twice")
        rows = []
        for r in range(h):
            if i >= len(lines):
                fail(f"{path}: {name} ends early")
            row = lines[i].rstrip()
            i += 1
            if len(row) != w or any(c not in CHARS for c in row):
                fail(f"{path}:{i}: {name} row {r} is {row!r}, want {w} of {CHARS!r}")
            rows.append([CHARS.index(c) for c in row])
        out[name] = (w, h, rows)
    return out


def read_font(name):
    path = os.path.join(FONTDIR, name + ".txt")
    text = open(path).read().split("\n")
    height = ascent = None
    glyphs, i = {}, 0
    while i < len(text):
        line = text[i]
        i += 1
        m = re.match(r"face (\d+) (\d+)", line)
        if m:
            height, ascent = int(m.group(1)), int(m.group(2))
            continue
        m = re.match(r"char (\d+) .* (\d+)$", line)
        if m:
            code, w = int(m.group(1)), int(m.group(2))
            rows = []
            for _ in range(height):
                row = text[i]
                i += 1
                rows.append([1 if c == "#" else 0 for c in row[:w].ljust(w, ".")])
            glyphs[code] = (w, rows)
    if height is None or sorted(glyphs) != list(range(32, 127)):
        fail(f"{path}: want a face line and glyphs 32..126")
    return height, ascent, glyphs


def pack_sprite(w, h, rows):
    b = bytearray([w, h])
    stride = (w + 1) // 2
    for row in rows:
        r = row + [KEY] * (stride * 2 - w)
        for x in range(0, stride * 2, 2):
            b.append((r[x] << 4) | r[x + 1])
    return bytes(b)


def pack_font(name, extra=None):
    """extra: optional {code: (w, rows)} for codes 127, 128, ... appended
    after 126 (Byteopoly's bullet, SPEC.md 131.10.7); without it the record
    is byte-identical to what it always was."""
    height, ascent, glyphs = read_font(name)
    codes = list(range(32, 127))
    for c in sorted(extra or {}):
        if c != codes[-1] + 1:
            fail(f"{name}: extra glyph {c} does not follow {codes[-1]}")
        glyphs[c] = extra[c]
        codes.append(c)
    n = len(codes)
    head = bytearray([height, ascent, 32, n])
    head += bytes(glyphs[c][0] for c in codes)
    table_at = len(head)
    head += bytes(2 * n)
    body = bytearray()
    for k, c in enumerate(codes):
        w, rows = glyphs[c]
        if w > 16:
            fail(f"{name}: glyph {c} is {w} wide; a row is two bytes")
        lit = [j for j, row in enumerate(rows) if any(row)]
        if not lit:
            continue                    # 0 offset: advance only (a space)
        off = table_at + 2 * n + len(body)
        struct.pack_into("<H", head, table_at + 2 * k, off)
        top, last = lit[0], lit[-1]
        body += bytes([top, last - top + 1])
        for row in rows[top:last + 1]:
            v = 0
            for i in range(16):
                v = (v << 1) | (row[i] if i < len(row) else 0)
            body += bytes([v >> 8, v & 255])
    return bytes(head + body)


def pack_text(path):
    """Lines (not # comments), each NUL-terminated, in order, then one more
    NUL: the guest finds the record's end by an empty line."""
    out = bytearray()
    for line in open(path).read().split("\n"):
        if line.startswith("#") or not line:
            continue
        if len(line) > 47 or any(not 32 <= ord(c) < 127 for c in line):
            fail(f"{path}: {line!r}: printable ASCII, 47 characters at most")
        out += line.encode("ascii") + b"\0"
    return bytes(out + b"\0")


def build(out_path):
    ids = read_ids()
    sprites = read_sprites()
    for name in sprites:
        if name not in ids:
            fail(f"sprites.txt defines {name}, which art.h does not list")
    blobs = {}
    for name in ids:
        if name in FONTS:
            blobs[name] = pack_font(FONTS[name])
        elif name in TEXTS:
            blobs[name] = pack_text(TEXTS[name])
        elif name in sprites:
            blobs[name] = pack_sprite(*sprites[name])
        else:
            fail(f"{name} has no sprite in sprites.txt and is not a font")
    head = bytearray(b"BA" + struct.pack("<H", len(ids)))
    head += bytes(2 * len(ids))
    body = bytearray()
    for k, name in enumerate(ids):
        off = len(head) + len(body)
        if off & 1:
            body.append(0)
            off += 1
        struct.pack_into("<H", head, 4 + 2 * k, off)
        body += blobs[name]
    data = bytes(head + body)
    if len(data) > 32768:
        fail(f"BBART.BIN is {len(data)} bytes; the draw core indexes it with 15-bit offsets")
    with open(out_path, "wb") as f:
        f.write(data)
    print(f"os88bbart: {out_path}: {len(data)} bytes, {len(ids)} entries "
          f"({sum(len(blobs[n]) for n in ids if n in FONTS)} font, "
          f"{sum(len(blobs[n]) for n in ids if n in TEXTS)} text, "
          f"{sum(len(blobs[n]) for n in ids if n not in FONTS and n not in TEXTS)} sprite)")


def selfcheck():
    ids = read_ids()
    sprites = read_sprites()
    for name in ids:
        if name in FONTS:
            read_font(FONTS[name])
            continue
        if name in TEXTS:
            if len(pack_text(TEXTS[name]).split(b"\0")) < 10:
                fail(f"selfcheck: {name} has under 8 lines")
            continue
        if name not in sprites:
            fail(f"selfcheck: {name} missing from sprites.txt")
        w, h, rows = sprites[name]
        if w > 64 or h > 32 or w < 1 or h < 1:
            fail(f"selfcheck: {name} is {w}x{h}; the core draws up to 64x32")
        if all(p == KEY for row in rows for p in row):
            fail(f"selfcheck: {name} is empty")
        want = [v for v in MASTER.values() if v[0] == name]
        if want and (want[0][1], want[0][2]) != (w, h):
            fail(f"selfcheck: {name} is {w}x{h}, the face lays it out as {want[0][1]}x{want[0][2]}")
    print(f"os88bbart: selfcheck OK ({len(ids)} IDs, {len(sprites)} sprites)")


def nearest(rgb):
    r, g, b = rgb[:3]
    best = min(range(16), key=lambda k: (EGA[k][0] - r) ** 2 + (EGA[k][1] - g) ** 2 + (EGA[k][2] - b) ** 2)
    return best


def draft(master, out_path):
    from PIL import Image
    im = Image.open(master).convert("RGB")
    W, H = im.size
    cw, ch = W / 8, H / 8
    px = im.load()
    out = ["# Breadboard XT face sprites (SPEC.md 130.6.3). THE SOURCE.",
           "# One character per EGA colour: " + " ".join(f"{c}={k}" for k, c in enumerate(CHARS)),
           "# ('.' = 13, transparent). Drafted by `tools/os88bbart.py draft` from",
           "# art/master.png (codex image generation, art/PROMPT.md); then finished",
           "# by hand. Edit here; make reads only this file.", ""]
    for (row, col), (name, w, h) in sorted(MASTER.items()):
        x0, y0 = int(col * cw), int(row * ch)
        x1, y1 = int((col + 1) * cw), int((row + 1) * ch)
        # The sprite's extent: every pixel that is not the magenta key.
        mx0, my0, mx1, my1 = x1, y1, x0, y0
        for y in range(y0 + 2, y1 - 2):
            for x in range(x0 + 2, x1 - 2):
                if nearest(px[x, y]) != KEY:
                    mx0, my0 = min(mx0, x), min(my0, y)
                    mx1, my1 = max(mx1, x + 1), max(my1, y + 1)
        if mx1 <= mx0:
            rows = ["." * w for _ in range(h)]
        else:
            bw, bh = mx1 - mx0, my1 - my0
            scale = max(bw / w, bh / h)
            ox = mx0 - (w * scale - bw) / 2
            oy = my0 - (h * scale - bh) / 2
            rows = []
            for ty in range(h):
                line = ""
                for tx in range(w):
                    votes = [0] * 16
                    sx0, sy0 = ox + tx * scale, oy + ty * scale
                    n = max(2, int(scale))
                    for j in range(n):
                        for i in range(n):
                            x = int(sx0 + (i + 0.5) * scale / n)
                            y = int(sy0 + (j + 0.5) * scale / n)
                            if x0 <= x < x1 and y0 <= y < y1:
                                votes[nearest(px[x, y])] += 1
                            else:
                                votes[KEY] += 1
                    total = sum(votes)
                    if votes[KEY] * 2 >= total:
                        line += "."
                    else:
                        votes[KEY] = -1
                        line += CHARS[votes.index(max(votes))]
                rows.append(line)
        out.append(f"sprite {name} {w} {h}")
        out += rows
        out.append("")
    with open(out_path, "w") as f:
        f.write("\n".join(out))
    print(f"os88bbart: drafted {len(MASTER)} sprites into {out_path}")


def png(outdir):
    from PIL import Image, ImageDraw
    os.makedirs(outdir, exist_ok=True)
    sprites = read_sprites()
    Z = 6
    names = sorted(sprites, key=lambda n: read_ids().index(n))
    cols = 6
    cell = 64 * Z // 2
    sheet = Image.new("RGB", (cols * (cell + 8), ((len(names) + cols - 1) // cols) * (cell + 20)), (255, 85, 255))
    d = ImageDraw.Draw(sheet)
    for k, name in enumerate(names):
        w, h, rows = sprites[name]
        z = min(Z, cell // max(w, h))
        cx, cy = (k % cols) * (cell + 8), (k // cols) * (cell + 20)
        for y, row in enumerate(rows):
            for x, p in enumerate(row):
                if p != KEY:
                    d.rectangle([cx + x * z, cy + y * z, cx + x * z + z - 1, cy + y * z + z - 1], fill=EGA[p])
        d.text((cx, cy + cell + 4), name[3:], fill=(0, 0, 0))
    sheet.save(os.path.join(outdir, "sprites.png"))
    # Actual-size strip on a white ground, as the face will show them.
    strip = Image.new("RGB", (sum(sprites[n][0] + 4 for n in names) + 4, 40), (255, 255, 255))
    x = 4
    for name in names:
        w, h, rows = sprites[name]
        for yy, row in enumerate(rows):
            for xx, p in enumerate(row):
                if p != KEY:
                    strip.putpixel((x + xx, 4 + yy), EGA[p])
        x += w + 4
    strip.resize((strip.width * 3, strip.height * 3), Image.NEAREST).save(os.path.join(outdir, "actual.png"))
    for fid, fname in FONTS.items():
        height, ascent, glyphs = read_font(fname)
        text = "Breadboard XT  Parts Resistor 68 kohm 5.00 V  Simulation time: 2.40 s"
        width = sum(glyphs[ord(c)][0] for c in text) + 8
        img = Image.new("RGB", (width, height + 4), (255, 255, 255))
        x = 4
        for c in text:
            w, rows = glyphs[ord(c)]
            for yy, row in enumerate(rows):
                for xx, bit in enumerate(row):
                    if bit:
                        img.putpixel((x + xx, 2 + yy), (0, 0, 0))
            x += w
        img.resize((img.width * 3, img.height * 3), Image.NEAREST).save(os.path.join(outdir, f"font-{fname}.png"))
    print(f"os88bbart: previews in {outdir}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", nargs="?", choices=["draft", "build", "png"])
    ap.add_argument("arg", nargs="?")
    ap.add_argument("-o", "--out")
    ap.add_argument("--selfcheck", action="store_true")
    a = ap.parse_args()
    if a.selfcheck:
        selfcheck()
    if a.cmd == "draft":
        draft(a.arg, a.out or SPRITES)
    elif a.cmd == "build":
        if not a.out:
            fail("build needs -o")
        build(a.out)
    elif a.cmd == "png":
        png(a.arg or "build/bbart-png")
    elif not a.selfcheck:
        ap.print_help()


if __name__ == "__main__":
    main()

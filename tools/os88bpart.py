#!/usr/bin/env python3
"""os88bpart.py - Byteopoly's art part, BPART.BIN (SPEC.md 131.10).

THE TEXT ART IS THE SOURCE: apps/byteopoly/art/sprites.txt (one character per
EGA colour), art/help.txt, and WIN16's committed font art. This tool shares
tools/os88bbart.py's packers (Breadboard XT's BBART.BIN, SPEC.md 130.6.3) as
imports - the layout is the same, only the magic is 'BP'.

    build -o BPART.BIN          pack sprites.txt, fonts, help.txt (make)
    build2 -o BPART2.BIN        pack art2.h's part: sprites2.txt, tx-*.txt, scenes, music (make)
    txh                         rewrite apps/byteopoly/bptx.h from art/tx-*.txt
    --selfcheck                 every art.h ID once, sizes, key colour, twins
    draft [-o sprites.txt]      quantise art/master*.png's pictures once
    mono-draft                  (re)derive every missing _M twin into sprites.txt
    png DIR [--mono]            zoomed previews of every sprite

Only build and --selfcheck are run by make; they need nothing but python3.
"""
import argparse, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import os88bbart as bb                      # noqa: E402  (packers, fonts, palette)

ROOT = os.path.dirname(HERE)
ART_H = os.path.join(ROOT, "apps", "byteopoly", "art.h")
ARTDIR = os.path.join(ROOT, "apps", "byteopoly", "art")
SPRITES = os.path.join(ARTDIR, "sprites.txt")
HELP = os.path.join(ARTDIR, "help.txt")
UITEXT = os.path.join(ARTDIR, "uitext.txt")
def help_lines(path):
    """help.txt: '# ' comment lines and blanks skipped; '#KEYS' is a real line."""
    out = []
    for line in open(path).read().split("\n"):
        if not line or (line.startswith("#") and line != "#KEYS"):
            continue
        if len(line) > 47 or any(not 32 <= ord(c) < 127 for c in line):
            fail(f"{path}: {line!r}: printable ASCII, 47 characters at most")
        out.append(line)
    return out


def pack_itext(lines):
    """An INDEXED text record (SPEC.md 131.10.7): dw n, n word offsets from the
    record's start, then the n lines, each NUL-terminated."""
    n = len(lines)
    head = bytearray(struct.pack("<H", n))
    body = bytearray()
    for s in lines:
        head += struct.pack("<H", 2 + 2 * n + len(body))
        body += s.encode("ascii") + b"\0"
    return bytes(head + body)


def pack_text(path):
    return pack_itext(help_lines(path))


TEXTS = {"BP_A_HELP": HELP, "BP_A_UITEXT": UITEXT}
FONTS = {"BP_F_TEXT13": "dlg13", "BP_F_TEXT10": "dlg10", "BP_F_TEXT08": "dlg08",
         "BP_F_BIG16": "sys16", "BP_F_BIG12": "sys12"}
CHARS, EGA, KEY = bb.CHARS, bb.EGA, bb.KEY
MAXBYTES = 65000        # the part is read into one segment; offsets are 16 bits
# The core's 2x2 mono pattern for each colour (apps/breadxt/draw.inc _bx_mono):
# bit0 = even row/even x, bit1 = even row/odd x, bit2 = odd row/even x, bit3 = odd/odd.
BX_MONO = [0, 0, 0, 0, 0, 0, 9, 7, 6, 0, 15, 15, 15, 15, 15, 15]

PROPS = ["PET", "VIC20", "C64", "POWER", "TRS80", "A800", "ZX", "APPLE2",
         "APPLE2E", "MAC", "BBS", "PC", "XT", "AT", "AMIGA", "ST", "386", "486"]
SIZES = {"S": (32, 24), "L": (48, 32), "T": (16, 12)}


def fail(msg):
    sys.exit(f"os88bpart: {msg}")


def read_ids(path=ART_H):
    ids = []
    for m in re.finditer(r"^#define (BP_[AF]_\w+) (\d+)\s*$", open(path).read(), re.M):
        name, n = m.group(1), int(m.group(2))
        if name == "BP_A_COUNT":
            if n != len(ids):
                fail(f"art.h: BP_A_COUNT is {n}, {len(ids)} IDs precede it")
            return ids
        if n != len(ids):
            fail(f"art.h: {name} is {n}, expected {len(ids)} (IDs are dense, in order)")
        ids.append(name)
    fail("art.h: no BP_A_COUNT")


def read_sprites(path=SPRITES):
    out, lines, i = {}, open(path).read().split("\n"), 0
    while i < len(lines):
        line = lines[i].strip()
        i += 1
        if not line or line.startswith("#"):
            continue
        m = re.match(r"sprite (BP_A_\w+) (\d+) (\d+)$", line)
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


def sprite_text(name, w, h, rows):
    return [f"sprite {name} {w} {h}"] + ["".join(CHARS[p] for p in r) for r in rows] + [""]


def write_sprites(sprites, path=SPRITES, header=None):
    ids = read_ids()
    out = header or []
    for n in ids:
        if n in sprites:
            out += sprite_text(n, *sprites[n])
    with open(path, "w") as f:
        f.write("\n".join(out))


HEADER = ["# Byteopoly sprites (SPEC.md 131.10). THE SOURCE: edit here; make reads only this.",
          "# One character per EGA colour: " + " ".join(f"{c}={k}" for k, c in enumerate(CHARS)),
          "# ('.' = 13, transparent). Drafted once by `tools/os88bpart.py draft` from",
          "# art/master*.png (codex image generation, art/PROMPT.md), then finished by",
          "# hand. `_M` twins are black and white only (SPEC.md 131.10.5).", ""]


def build(out_path):
    ids = read_ids()
    sprites = read_sprites()
    for name in sprites:
        if name not in ids:
            fail(f"sprites.txt defines {name}, which art.h does not list")
    blobs = {}
    for name in ids:
        if name in FONTS:
            blobs[name] = bb.pack_font(FONTS[name])
        elif name in TEXTS:
            blobs[name] = pack_text(TEXTS[name])
        elif name in sprites:
            blobs[name] = bb.pack_sprite(*sprites[name])
        else:
            fail(f"{name} has no sprite in sprites.txt and is not a font or text")
    head = bytearray(b"BP" + struct.pack("<H", len(ids)))
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
    if len(data) > MAXBYTES:
        fail(f"BPART.BIN is {len(data)} bytes, over {MAXBYTES}")
    with open(out_path, "wb") as f:
        f.write(data)
    fonts = sum(len(blobs[n]) for n in ids if n in FONTS)
    text = sum(len(blobs[n]) for n in ids if n in TEXTS)
    print(f"os88bpart: {out_path}: {len(data)} bytes, {len(ids)} entries "
          f"({fonts} font, {text} text, {len(data) - fonts - text} sprite)")


# --- mono ---------------------------------------------------------------

def mono_lit(p, x, y):
    """Is colour p lit at (x, y) through the core's 2x2 pattern?"""
    return (BX_MONO[p] >> ((y & 1) * 2 + (x & 1))) & 1


def mono_twin(w, h, rows, tiny=False):
    """A black-and-white redraw. Light colours and light grey go white; black
    stays black. A TINY picture is too small for a dither, so every dark
    colour (screens, keys, shading, LEDs) goes black over a white case, which
    keeps the silhouette and the features. A bigger one keeps each dark
    chromatic fill as a 50% checker so it no longer merges with its outline."""
    out = []
    for y in range(h):
        r = []
        for x in range(w):
            p = rows[y][x]
            if p == KEY:
                r.append(KEY)
            elif p == 0:
                r.append(0)
            elif p in (7, 10, 11, 12, 14, 15):
                r.append(15)
            elif tiny:
                r.append(0 if p in (1, 2, 3, 4, 5, 6, 8, 9) else 15)
            else:
                r.append(0 if (x + y) & 1 else 15)
        out.append(r)
    return out


def mono_view(w, h, rows):
    """Rows as the core draws them on 1bpp: 0 black, 15 white, KEY kept."""
    return [[KEY if rows[y][x] == KEY else (15 if mono_lit(rows[y][x], x, y) else 0)
             for x in range(w)] for y in range(h)]


# --- selfcheck ----------------------------------------------------------

def selfcheck():
    ids = read_ids()
    sprites = read_sprites()
    seen = set()
    for name in ids:
        if name in seen:
            fail(f"selfcheck: {name} twice in art.h")
        seen.add(name)
        if name in FONTS:
            bb.read_font(FONTS[name])
            continue
        if name in TEXTS:
            if name == "BP_A_HELP":
                lines = help_lines(TEXTS[name]) if os.path.exists(TEXTS[name]) else []
                if "#KEYS" not in lines or lines.index("#KEYS") < 16 or len(lines) < 40:
                    fail("selfcheck: BP_A_HELP needs rules lines, a #KEYS line, then key lines")
            continue
        if name not in sprites:
            fail(f"selfcheck: {name} missing from sprites.txt")
        w, h, rows = sprites[name]
        if not (1 <= w <= 96 and 1 <= h <= 96):
            fail(f"selfcheck: {name} is {w}x{h}")
        if all(p == KEY for r in rows for p in r):
            fail(f"selfcheck: {name} is empty")
        if name.startswith("BP_A_M_"):
            bad = {p for r in rows for p in r if p not in (0, 15, KEY)}
            if bad:
                fail(f"selfcheck: {name} is a mono twin but uses colours {sorted(bad)}")
        # the key colour may only be a background: opaque pixels must touch
        # the sprite (no lone island of 13 inside is detectable; check edges)
        if not any(p != KEY for p in rows[0] + rows[-1] + [r[0] for r in rows] + [r[-1] for r in rows]):
            pass            # fully padded sprite: allowed
        want = SIZE_RULES.get(name)
        if want and (w, h) != want:
            fail(f"selfcheck: {name} is {w}x{h}, planned {want[0]}x{want[1]}")
    extra = set(sprites) - set(ids)
    if extra:
        fail(f"selfcheck: sprites.txt has unknown {sorted(extra)}")
    read_ids2()
    read_sprites2()
    txh(check=True)
    print(f"os88bpart: selfcheck OK ({len(ids)} IDs, {len(sprites)} sprites, bptx.h current)")


def size_rules():
    r = {}
    for k, p in enumerate(PROPS):
        for t, (w, h) in SIZES.items():
            r[f"BP_A_{t}_{p}"] = (w, h)
    for n in ("FLOPPY", "MOUSE", "JOYSTICK", "CASSETTE"):
        r[f"BP_A_TOK_{n}"] = (16, 16)
    for n in range(1, 7):
        r[f"BP_A_DIE{n}"] = (24, 24)
    r.update({"BP_A_RAM": (8, 6), "BP_A_WORKSHOP": (12, 10), "BP_A_MORTGAGE": (12, 10),
              "BP_A_MASCOT": (64, 64), "BP_A_CARD_FF": (48, 64), "BP_A_CARD_SE": (48, 64),
              "BP_A_CABLE": (32, 12), "BP_A_KEYCAP": (16, 16), "BP_A_COINS": (16, 16),
              "BP_A_LOCK": (16, 16)})
    for n in ("BOOT", "REPAIR", "SWAP", "CRASH", "FLOPPY", "SYSERR", "DUTY", "EWASTE"):
        r[f"BP_A_{n}"] = (32, 24)
        r[f"BP_A_T_{n}"] = (16, 12)
    r["BP_A_CARD_FF_S"] = r["BP_A_CARD_SE_S"] = (24, 32)
    return r


SIZE_RULES = size_rules()
for _n in list(SIZE_RULES):
    SIZE_RULES["BP_A_M_" + _n[5:]] = SIZE_RULES[_n]


# --- draft from the codex masters --------------------------------------

# Each sheet: file -> list of pictures in reading order; a picture is a list
# of (ID, w, h) cut from the same drawing (a computer is cut three times).
def prop_cut(p):
    return [(f"BP_A_L_{p}", 48, 32), (f"BP_A_S_{p}", 32, 24), (f"BP_A_T_{p}", 16, 12)]


SHEETS = [
    ("master1.png", [prop_cut(p) for p in PROPS]),
    ("master2.png", [
        [("BP_A_TOK_FLOPPY", 16, 16)], [("BP_A_TOK_MOUSE", 16, 16)],
        [("BP_A_TOK_JOYSTICK", 16, 16)], [("BP_A_TOK_CASSETTE", 16, 16)],
        [("BP_A_COINS", 16, 16)], [("BP_A_LOCK", 16, 16)], [("BP_A_KEYCAP", 16, 16)],
        [("BP_A_DIE1", 24, 24)],
        [("BP_A_DIE2", 24, 24)], [("BP_A_DIE3", 24, 24)], [("BP_A_DIE4", 24, 24)],
        [("BP_A_DIE5", 24, 24)], [("BP_A_DIE6", 24, 24)],
        [("BP_A_RAM", 8, 6)], [("BP_A_WORKSHOP", 12, 10)], [("BP_A_MORTGAGE", 12, 10)],
        [("BP_A_BOOT", 32, 24)], [("BP_A_REPAIR", 32, 24)], [("BP_A_SWAP", 32, 24)],
        [("BP_A_CRASH", 32, 24)], [("BP_A_FLOPPY", 32, 24)], [("BP_A_SYSERR", 32, 24)],
        [("BP_A_DUTY", 32, 24)], [("BP_A_EWASTE", 32, 24)], [("BP_A_CABLE", 32, 12)]]),
    # master4 replaces four near-twins of master1 (Qqa2-20): Apple IIe, PC AT, Atari ST, 486
    ("master4.png", [prop_cut("APPLE2E"), prop_cut("AT"), prop_cut("ST"), prop_cut("486")]),
    ("master3.png", [[("BP_A_MASCOT", 64, 64)], [("BP_A_CARD_FF", 48, 64)],
                     [("BP_A_CARD_SE", 48, 64)]]),
]


def find_pictures(im):
    """Bounding boxes of the pictures on a sheet, in reading order: connected
    runs of non-key pixels on a coarse grid, dilated so a picture's parts join."""
    W, H = im.size
    px = im.load()
    B = 4
    gw, gh = W // B, H // B
    on = [[False] * gw for _ in range(gh)]
    for gy in range(gh):
        for gx in range(gw):
            n = 0
            for dy in (0, B - 1):
                for dx in (0, B - 1):
                    if bb.nearest(px[gx * B + dx, gy * B + dy]) != KEY:
                        n += 1
            on[gy][gx] = n >= 2
    R = 2                                # dilation, in grid cells
    lab = [[0] * gw for _ in range(gh)]
    boxes = []
    for sy in range(gh):
        for sx in range(gw):
            if on[sy][sx] and not lab[sy][sx]:
                k = len(boxes) + 1
                stack, box = [(sx, sy)], [sx, sy, sx, sy]
                lab[sy][sx] = k
                while stack:
                    x, y = stack.pop()
                    box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
                    for ny in range(max(0, y - R), min(gh, y + R + 1)):
                        for nx in range(max(0, x - R), min(gw, x + R + 1)):
                            if on[ny][nx] and not lab[ny][nx]:
                                lab[ny][nx] = k
                                stack.append((nx, ny))
                boxes.append(box)
    big = [b for b in boxes if (b[2] - b[0] + 1) * (b[3] - b[1] + 1) > 40]
    for b in boxes:                      # a small fragment (a mouse's tail) joins the nearest picture
        if b not in big and big:
            cx, cy = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
            j = min(range(len(big)), key=lambda k: abs(cx - (big[k][0] + big[k][2]) / 2) + abs(cy - (big[k][1] + big[k][3]) / 2))
            o = big[j]
            big[j] = [min(o[0], b[0]), min(o[1], b[1]), max(o[2], b[2]), max(o[3], b[3])]
    boxes = [(b[0] * B, b[1] * B, (b[2] + 1) * B, (b[3] + 1) * B) for b in big]
    boxes.sort(key=lambda b: (b[1] + b[3]) / 2)
    rows, cur = [], []
    for b in boxes:
        if cur and (b[1] + b[3]) / 2 - sum((c[1] + c[3]) / 2 for c in cur) / len(cur) > 0.4 * max(b[3] - b[1], 40):
            rows.append(cur)
            cur = []
        cur.append(b)
    if cur:
        rows.append(cur)
    out = []
    for r in rows:
        out += sorted(r, key=lambda b: b[0])
    return out


def cut(im, box, w, h):
    """Quantise the picture in `box` to w x h EGA indices: area vote per
    target pixel, black outlines favoured so a thin line survives."""
    px = im.load()
    x0, y0, x1, y1 = box
    bw, bh = x1 - x0, y1 - y0
    scale = max(bw / w, bh / h)
    ox = x0 - (w * scale - bw) / 2
    oy = y0 - (h * scale - bh) / 2
    n = max(3, int(scale))
    rows = []
    for ty in range(h):
        row = []
        for tx in range(w):
            votes = [0.0] * 16
            for j in range(n):
                for i in range(n):
                    x = int(ox + tx * scale + (i + 0.5) * scale / n)
                    y = int(oy + ty * scale + (j + 0.5) * scale / n)
                    if x0 <= x < x1 and y0 <= y < y1:
                        votes[bb.nearest(px[x, y])] += 1.5 if bb.nearest(px[x, y]) == 0 else 1
                    else:
                        votes[KEY] += 1
            total = sum(votes)
            if votes[KEY] * 2 >= total:
                row.append(KEY)
            else:
                votes[KEY] = -1
                row.append(votes.index(max(votes)))
        rows.append(row)
    return rows


def despeckle(w, h, rows):
    """Drop opaque pixels with no opaque 4-neighbour (a lone pixel is noise)."""
    out = [r[:] for r in rows]
    for y in range(h):
        for x in range(w):
            if rows[y][x] == KEY:
                continue
            nb = [(x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)]
            if not any(0 <= a < w and 0 <= b < h and rows[b][a] != KEY for a, b in nb):
                out[y][x] = KEY
    return out


def draft(out_path):
    from PIL import Image
    sprites = {}
    for fname, cuts in SHEETS:
        im = Image.open(os.path.join(ARTDIR, fname)).convert("RGB")
        boxes = find_pictures(im)
        if len(boxes) != len(cuts):
            fail(f"{fname}: found {len(boxes)} pictures, expected {len(cuts)}; boxes={boxes}")
        for box, group in zip(boxes, cuts):
            for name, w, h in group:
                sprites[name] = (w, h, despeckle(w, h, cut(im, box, w, h)))
    write_sprites(sprites, out_path, HEADER)
    print(f"os88bpart: drafted {len(sprites)} sprites into {out_path}")


# --- previews ------------------------------------------------------------

def png(outdir, mono=False):
    from PIL import Image, ImageDraw
    os.makedirs(outdir, exist_ok=True)
    sprites = read_sprites()
    ids = read_ids()
    names = [n for n in ids if n in sprites]

    def show(name):
        w, h, rows = sprites[name]
        return (w, h, mono_view(w, h, rows)) if mono else sprites[name]

    def colour(p):
        return (255, 255, 255) if (mono and p == 15) else (0, 0, 0) if mono else EGA[p]

    groups = [("props", [n for n in names if re.match(r"BP_A_[SLT]_", n)]),
              ("rest", [n for n in names if not re.match(r"BP_A_[SLT]_", n)])]
    tag = "mono" if mono else "colour"
    for gname, group in groups:
        Z = 4
        cols = 6
        cw = max(show(n)[0] for n in group) * Z + 10
        ch = max(show(n)[1] for n in group) * Z + 22
        sheet = Image.new("RGB", (cols * cw, ((len(group) + cols - 1) // cols) * ch),
                          (255, 85, 255) if not mono else (190, 190, 190))
        d = ImageDraw.Draw(sheet)
        for k, name in enumerate(group):
            w, h, rows = show(name)
            cx, cy = (k % cols) * cw + 4, (k // cols) * ch + 2
            for y, row in enumerate(rows):
                for x, p in enumerate(row):
                    if p != KEY:
                        d.rectangle([cx + x * Z, cy + y * Z, cx + x * Z + Z - 1, cy + y * Z + Z - 1],
                                    fill=colour(p))
            d.text((cx, cy + h * Z + 3), name[5:], fill=(0, 0, 0))
        sheet.save(os.path.join(outdir, f"{gname}-{tag}.png"))
    # actual-size strip on white, tripled, as the face shows them
    strip = Image.new("RGB", (cols * 0 + 1200, 12 * 30 + 200), (255, 255, 255))
    x = y = 4
    for name in names:
        w, h, rows = show(name)
        if x + w + 4 > 400:
            x, y = 4, y + 66
        for yy, row in enumerate(rows):
            for xx, p in enumerate(row):
                if p != KEY and y + yy < strip.height:
                    strip.putpixel((x + xx, y + yy), colour(p))
        x += w + 4
    strip.crop((0, 0, 400, min(strip.height, y + 70))).resize((1200, 3 * min(strip.height, y + 70)),
                                                              Image.NEAREST).save(
        os.path.join(outdir, f"actual-{tag}.png"))
    if mono:                    # each twin beside what the core makes of its colour original
        pairs = [(n, "BP_A_" + n[len("BP_A_M_"):]) for n in names if n.startswith("BP_A_M_")]
        Z = 6
        cw = max(sprites[a][0] for a, _ in pairs) * Z * 2 + 24
        ch = max(sprites[a][1] for a, _ in pairs) * Z + 8
        cols = 3
        sheet = Image.new("RGB", (cols * cw, ((len(pairs) + cols - 1) // cols) * ch), (190, 190, 190))
        d = ImageDraw.Draw(sheet)
        for k, (tw, base) in enumerate(pairs):
            ox, oy = (k % cols) * cw + 4, (k // cols) * ch + 4
            for j, nm in enumerate((base, tw)):
                w, h, rows = sprites[nm]
                for y, row in enumerate(mono_view(w, h, rows)):
                    for x, p in enumerate(row):
                        if p != KEY:
                            xx = ox + j * (w * Z + 12) + x * Z
                            d.rectangle([xx, oy + y * Z, xx + Z - 1, oy + y * Z + Z - 1],
                                        fill=(255, 255, 255) if p else (0, 0, 0))
        sheet.save(os.path.join(outdir, "twins-mono.png"))
    print(f"os88bpart: {tag} previews in {outdir}")


def mono_draft():
    sprites = read_sprites()
    ids = read_ids()
    for n in ids:
        if n.startswith("BP_A_M_"):
            base = "BP_A_" + n[len("BP_A_M_"):]
            if base in sprites and n not in sprites:       # hand-finished twins are kept
                w, h, rows = sprites[base]
                sprites[n] = (w, h, mono_twin(w, h, rows, base.startswith("BP_A_T_")))
    write_sprites(sprites, SPRITES, HEADER)
    print("os88bpart: mono twins derived")


# --- the second art part, BPART2.BIN (SPEC.md 131.10.7) --------------------

ART2_H = os.path.join(ROOT, "apps", "byteopoly", "art2.h")
SPRITES2 = os.path.join(ARTDIR, "sprites2.txt")
BPTX_H = os.path.join(ROOT, "apps", "byteopoly", "bptx.h")
MAXBYTES2 = 57344       # 56 KB unpacked: one part, one segment, one claim
TX_FILES = ["app", "panels", "setup", "face", "side", "save", "link", "rules"]   # BP_A2_TX_APP + r
TX_LIMIT = 63           # bx_str's slot: 63 characters and a NUL
TX_LINES = 512          # a handle's line field is nine bits
TX_HANDLE = 0x1000      # handle = 1000h + (r << 9) + line


def defines(*paths):
    """Every `#define NAME <int>` of the art headers: the symbols scenes and
    music records may name (DECISIONS D1)."""
    out = {}
    for path in paths:
        for m in re.finditer(r"^#define (\w+)\s+(\d+)\b", open(path).read(), re.M):
            out[m.group(1)] = int(m.group(2))
    return out


def read_ids2():
    d = {k: v for k, v in defines(ART2_H).items() if k.startswith("BP_A2_")}
    base, count = d.get("BP_A2_BASE"), d.get("BP_A2_COUNT")
    if base != 256 or not count or count <= base or count > 512:
        fail("art2.h: BP_A2_BASE must be 256 and BP_A2_COUNT in 257..512")
    ids = {}
    for k, v in d.items():
        if k in ("BP_A2_BASE", "BP_A2_COUNT"):
            continue
        if not base <= v < count:
            fail(f"art2.h: {k} = {v} is outside {base}..{count - 1}")
        if v in ids:
            fail(f"art2.h: {k} and {ids[v]} are both {v}")
        ids[v] = k
    return base, count, ids


def read_sprites2():
    if not os.path.exists(SPRITES2):
        return {}
    out, lines, i = {}, open(SPRITES2).read().split("\n"), 0
    while i < len(lines):
        line = lines[i].strip()
        i += 1
        if not line or line.startswith("#"):
            continue
        m = re.match(r"sprite (BP_A2_\w+) (\d+) (\d+)$", line)
        if not m:
            fail(f"{SPRITES2}:{i}: expected `sprite BP_A2_NAME W H`, got {line!r}")
        name, w, h = m.group(1), int(m.group(2)), int(m.group(3))
        if name in out:
            fail(f"{SPRITES2}:{i}: {name} twice")
        if not (1 <= w <= 255 and 1 <= h <= 255):
            fail(f"{SPRITES2}:{i}: {name} is {w}x{h}")
        rows = []
        for r in range(h):
            row = lines[i].rstrip() if i < len(lines) else ""
            i += 1
            if len(row) != w or any(c not in CHARS for c in row):
                fail(f"{SPRITES2}:{i}: {name} row {r} is {row!r}, want {w} of {CHARS!r}")
            rows.append([CHARS.index(c) for c in row])
        out[name] = (w, h, rows)
    return out


def c_unescape(s, where):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        n = s[i + 1:i + 2]
        if n in "\\\"'":
            out.append(n)
            i += 2
        elif n == "n":
            out.append("\n")
            i += 2
        elif n.isdigit():
            m = re.match(r"[0-7]{1,3}", s[i + 1:])
            out.append(chr(int(m.group(0), 8)))
            i += 1 + len(m.group(0))
        else:
            fail(f"{where}: unknown escape \\{n}")
    return "".join(out)


def read_tx(r):
    """art/tx-<file>.txt: `NAME "text"` lines, in handle order."""
    path = os.path.join(ARTDIR, f"tx-{TX_FILES[r]}.txt")
    if not os.path.exists(path):
        return path, []
    out = []
    for k, line in enumerate(open(path).read().split("\n"), 1):
        if not line.strip() or line.startswith("#"):
            continue
        m = re.match(r'(T[A-Z]_\w+) "((?:[^"\\]|\\.)*)"\s*$', line)
        if not m:
            fail(f"{path}:{k}: expected `TX_NAME \"text\"`, got {line!r}")
        s = c_unescape(m.group(2), f"{path}:{k}")
        if len(s) > TX_LIMIT or any(not 32 <= ord(c) < 127 for c in s):
            fail(f"{path}:{k}: {m.group(1)}: printable ASCII, {TX_LIMIT} characters at most")
        out.append((m.group(1), m.group(2), s))
    if len(out) > TX_LINES:
        fail(f"{path}: {len(out)} lines, over {TX_LINES}")
    return path, out


def all_tx():
    seen, recs = {}, []
    for r in range(len(TX_FILES)):
        path, lines = read_tx(r)
        for name, _, _ in lines:
            if name in seen:
                fail(f"{path}: {name} is also in {seen[name]}")
            seen[name] = path
        recs.append(lines)
    return recs


def bptx_text():
    out = ["/* GENERATED by tools/os88bpart.py from apps/byteopoly/art/tx-*.txt -",
           " * do not edit: change the .txt and run `python3 tools/os88bpart.py txh`.",
           " * A UI string is a TEXT HANDLE (SPEC.md 131.10.7): 1000h + (record << 9)",
           " * + line, resolved where the text is read (bx_text, bx_twidth, bx_str).",
           " * The host harness (BP_HOST) gets the literal instead. */",
           "#ifndef BYTEOPOLY_BPTX_H", "#define BYTEOPOLY_BPTX_H",
           "#ifdef BP_HOST", "#define BPT(h, s) (s)", "#define bx_str(s) (s)", "#else",
           "#define BPT(h, s) ((const char *)(h))",
           "const char *bx_str(const char *s);", "#endif"]
    for r, lines in enumerate(all_tx()):
        out.append(f"/* tx-{TX_FILES[r]}.txt: record {460 + r} */")
        for k, (name, raw, _) in enumerate(lines):
            out.append(f'#define {name} BPT(0x{TX_HANDLE + (r << 9) + k:04X}, "{raw}")')
    out.append("#endif")
    return "\n".join(out) + "\n"


def txh(check=False):
    want = bptx_text()
    have = open(BPTX_H).read() if os.path.exists(BPTX_H) else ""
    if check:
        if have != want:
            fail("apps/byteopoly/bptx.h is stale: run `python3 tools/os88bpart.py txh`")
        return
    if have != want:
        with open(BPTX_H, "w") as f:
            f.write(want)
    print(f"os88bpart: {BPTX_H} {'unchanged' if have == want else 'written'}")


def build2(out_path):
    base, count, ids = read_ids2()
    txh(check=True)
    blobs, kinds = {}, {}
    sprites = read_sprites2()
    byname = {v: k for k, v in ids.items()}
    for name, spr in sprites.items():
        if name not in byname:
            fail(f"sprites2.txt defines {name}, which art2.h does not list")
        blobs[byname[name]] = bb.pack_sprite(*spr)
        kinds[byname[name]] = "sprite"
    for r, lines in enumerate(all_tx()):
        tid = defines(ART2_H).get(f"BP_A2_TX_{TX_FILES[r].upper()}")
        if tid != 460 + r:
            fail(f"art2.h: BP_A2_TX_{TX_FILES[r].upper()} must be {460 + r}")
        blobs[tid] = pack_itext([s for _, _, s in lines])
        kinds[tid] = "text"
    if os.path.exists(os.path.join(HERE, "os88gk.py")):      # the kit's scenes (D1)
        import glob
        import os88gk
        paths = sorted(glob.glob(os.path.join(ARTDIR, "scenes-*.txt")))
        if paths:
            for sid, rec in os88gk.compile_scenes(paths, defines(ART_H, ART2_H)).items():
                if not base <= sid < count:
                    fail(f"a scene's ID {sid} is outside art2.h's {base}..{count - 1}")
                if sid in blobs:
                    fail(f"scene ID {sid} already has a record")
                blobs[sid] = rec
                kinds[sid] = "scene"
    # ---- sound lane ----
    # The music and effects records (SPEC.md 132.8): the sound lane's
    # sound_records(symbols) -> {BP_A2_MUSIC: ..., BP_A2_SFX: ...}, defined
    # above `# --- mono`, {} when os88gkmus or the .txt files are absent.
    if "sound_records" in globals():
        for sid, rec in sound_records(defines(ART_H, ART2_H)).items():
            if not base <= sid < count or sid in blobs:
                fail(f"music record ID {sid} is outside art2.h or already taken")
            blobs[sid] = rec
            kinds[sid] = "music"
    # ---- end sound lane ----
    n = count - base
    head = bytearray(b"B2" + struct.pack("<H", n)) + bytes(2 * n)
    body = bytearray()
    for i in range(n):
        if base + i not in blobs:
            continue
        off = len(head) + len(body)
        if off & 1:
            body.append(0)
            off += 1
        struct.pack_into("<H", head, 4 + 2 * i, off)
        body += blobs[base + i]
    data = bytes(head + body)
    if len(data) > MAXBYTES2:
        fail(f"BPART2.BIN is {len(data)} bytes, over {MAXBYTES2}")
    with open(out_path, "wb") as f:
        f.write(data)
    by = {}
    for i, k in kinds.items():
        by[k] = by.get(k, 0) + len(blobs[i])
    print(f"os88bpart: {out_path}: {len(data)} bytes, {len(blobs)} of {n} entries ("
          + ", ".join(f"{v} {k}" for k, v in sorted(by.items())) + ")")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", nargs="?", choices=["draft", "build", "build2", "txh", "png", "mono-draft"])
    ap.add_argument("arg", nargs="?")
    ap.add_argument("-o", "--out")
    ap.add_argument("--mono", action="store_true")
    ap.add_argument("--selfcheck", action="store_true")
    a = ap.parse_args()
    if a.selfcheck:
        selfcheck()
    if a.cmd == "draft":
        draft(a.out or SPRITES)
    elif a.cmd == "build":
        if not a.out:
            fail("build needs -o")
        build(a.out)
    elif a.cmd == "build2":
        if not a.out:
            fail("build2 needs -o")
        build2(a.out)
    elif a.cmd == "txh":
        txh()
    elif a.cmd == "png":
        png(a.arg or "build/bpart-png", a.mono)
    elif a.cmd == "mono-draft":
        mono_draft()
    elif not a.selfcheck:
        ap.print_help()


if __name__ == "__main__":
    main()

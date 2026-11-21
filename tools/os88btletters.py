#!/usr/bin/env python3
"""Select the typing tutor's native lettering from committed PageDesk strikes.

Development converter. Make uses the selected ASCII masks in apps/boratype/art.
For the gold title supply the owner's VGA lesson PNG with --reference.
"""
import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "apps/boratype/art"


def strike(size, height, baseline, name, reference_glyphs=None):
    source = ROOT / f"apps/pagedesk/fonts/serif-b-{size:02d}.t88"
    text = source.read_text()
    lines = [f"# From {source.relative_to(ROOT)}. See apps/pagedesk/fonts/OFL-Liberation.txt.",
             f"face {height} {baseline}", ""]
    for match in re.finditer(r"^glyph (\d+) w (\d+) lb (-?\d+) top (-?\d+)\n((?:[.#]+\n)*)", text, re.M):
        code, width, _, top = map(int, match.groups()[:4])
        if not 32 <= code <= 126:
            continue
        if reference_glyphs and chr(code) in reference_glyphs:
            rows = reference_glyphs[chr(code)]
            lines.append(f"char {code} {chr(code)!r} {len(rows[0]) + 1}")
            lines.extend(row + "." for row in rows)
            lines.append("")
            continue
        rows = match[5].splitlines()
        ink = min(width, 15)
        advance = ink + 1 if ink else size // 3
        lines.append(f"char {code} {chr(code)!r} {advance}")
        for y in range(height):
            row = y - baseline + top
            if 0 <= row < len(rows):
                lines.append("".join(rows[row][x * width // ink] for x in range(ink)) + ".")
            else:
                lines.append("." * advance)
        lines.append("")
    (ART / f"{name}.txt").write_text("\n".join(lines) + "\n")


def masthead(path):
    from PIL import Image
    # Only the 19-letter title above the navigation bar, no surrounding UI.
    with Image.open(path) as source:
        if source.size != (1448, 1086):
            raise ValueError("Expected the supplied 1448x1086 VGA lesson reference")
        pixels = source.convert("RGB").crop((230, 5, 1230, 62))
        mask = Image.new("L", pixels.size)
        mask.putdata([255 if r > 150 and g > 125 and b < 115 else 0 for r,g,b in pixels.getdata()])
        mask = mask.resize((432,24),Image.Resampling.BOX)
        bits = ["".join("#" if mask.getpixel((x,y)) >= 114 else "." for x in range(mask.width))
                for y in range(mask.height)]
    (ART/"masthead.mask").write_text("\n".join(bits)+"\n")


def key_legends(path):
    from PIL import Image
    # Centre of each printed key legend. The bounded crop excludes the bevel.
    keyboard = [
        ("1234567890", 646, (206,311,418,523,628,735,842,949,1055,1161)),
        ("QWERTYUIOP", 726, (249,357,464,572,679,785,887,994,1099,1206)),
        ("ASDFGHJKL;", 806, (276,381,491,597,705,811,918,1024,1130,1232)),
        ("ZXCVBNM", 878, (323,430,545,656,767,880,986)),
    ]
    out = []
    with Image.open(path) as source:
        for keys, y, centers in keyboard:
            for key, x in zip(keys, centers):
                crop = source.convert("RGB").crop((x-28,y-23,x+29,y+23))
                mask = Image.new("L", crop.size)
                mask.putdata([255 if max(rgb) < 68 else 0 for rgb in crop.getdata()])
                mask = mask.crop(mask.getbbox())
                width = min(15, max(3, round(13*mask.width/mask.height)))
                mask = mask.resize((width, 13), Image.Resampling.BOX)
                out.append(key)
                out.append("."*width)
                out.extend("".join("#" if mask.getpixel((i,j)) >= 96 else "." for i in range(width))
                           for j in range(13))
                out.extend(["."*width, "."*width])
    (ART/"key-legend.mask").write_text("\n".join(out)+"\n")


def badges(path):
    from PIL import Image, ImageOps
    import os88bbart as bb
    boxes = [(136,814,238,900), (402,815,520,898), (679,814,782,899),
             (939,818,1049,899), (1206,814,1326,900)]
    out = []
    with Image.open(path) as source:
        if source.size != (1448, 1086):
            raise ValueError("Expected the supplied VGA records reference")
        for box in boxes:
            cell = ImageOps.contain(source.convert("RGB").crop(box), (24, 24), Image.Resampling.BOX)
            icon = Image.new("RGB", (24,24), (4,3,55))
            icon.paste(cell, ((24-cell.width)//2, (24-cell.height)//2))
            for y in range(24):
                row = []
                for x in range(24):
                    r,g,b = icon.getpixel((x,y))
                    if r<40 and g<40 and b>25 and b>max(r,g)*1.3:
                        row.append(".")
                    else:
                        palette = (i for i in range(16) if i != bb.KEY)
                        index = min(palette, key=lambda i: sum((a-v)**2 for a,v in zip(bb.EGA[i], (r,g,b))))
                        row.append(bb.CHARS[index])
                out.append("".join(row))
    (ART/"badges.mask").write_text("\n".join(out)+"\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference",type=Path)
    parser.add_argument("--records", type=Path)
    args = parser.parse_args()
    if args.reference:
        masthead(args.reference)
        key_legends(args.reference)
    legend = (ART/"key-legend.mask").read_text().splitlines()
    glyphs = {legend[i]: legend[i+1:i+17] for i in range(0,len(legend),17)}
    strike(12,10,8,"label")
    strike(16,16,13,"keytype", glyphs)
    # Enlarge the selected reference capitals for headings without crossing
    # the shared renderer's two-byte row stride.
    large = {}
    for key, rows in glyphs.items():
        width=min(15, round(len(rows[0])*1.4))
        large[key]=["."*width]*2
        large[key].extend("".join(rows[1+y*13//18][x*len(rows[0])//width] for x in range(width))
                          for y in range(18))
        large[key].extend(["."*width]*4)
    strike(24,24,19,"large",large)
    if args.records:
        badges(args.records)

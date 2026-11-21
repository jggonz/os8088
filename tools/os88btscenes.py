#!/usr/bin/env python3
"""Pack the four supplied VGA scene illustrations as window-renderer sprites.

Normal builds read committed native-size artwork. The optional --select mode
takes a directory of the original references and needs Pillow.
"""
import argparse
from pathlib import Path
import struct

import os88bbart as bb

ART = Path(__file__).resolve().parents[1] / "apps/boratype/art/scene-cards.txt"
WIDTH, HEIGHT = 144, 96
SCENES = (
    ("lesson", "Borat’s Retro VGA Typing Lesson.png", (10, 124, 368, 402)),
    ("record", "Borat’s Typing Records and Achievements-6.png", (915, 203, 1427, 602)),
    ("result", "Borat's typing challenge results-4.png", (13, 201, 548, 659)),
    ("settings", "Borat Teaches Typing_ Sound and Display-5.png", (807, 595, 1090, 880)),
)


def select(directory):
    from PIL import Image, ImageOps
    palette = Image.new("P", (1, 1))
    palette.putpalette([v for i in range(256) for v in bb.EGA[i if i < 16 and i != bb.KEY else 0]])
    out = []
    for name, filename, crop in SCENES:
        with Image.open(directory / filename) as source:
            if source.size != (1448, 1086):
                raise ValueError(f"Unexpected dimensions for {filename}")
            panel = ImageOps.fit(source.convert("RGB").crop(crop), (WIDTH, HEIGHT),
                                 method=Image.Resampling.LANCZOS, centering=(0.5, 0.25))
            image = panel.quantize(palette=palette, dither=Image.Dither.FLOYDSTEINBERG)
            out.append(name)
            for y in range(HEIGHT):
                row = []
                for x in range(WIDTH):
                    index = image.getpixel((x, y))
                    row.append(bb.CHARS[index if index < 16 and index != bb.KEY else 0])
                out.append("".join(row))
    ART.write_text("\n".join(out) + "\n")


def build_scene_bank():
    """The last BTAN bank reserves bytes 16..23 for these four sprite offsets."""
    lines = ART.read_text().splitlines()
    if len(lines) != len(SCENES) * (1 + HEIGHT):
        raise ValueError("Expected four complete scene cards")
    data = bytearray(2*len(SCENES))
    for i, (name, _, _) in enumerate(SCENES):
        at = i*(HEIGHT+1)
        rows = lines[at+1:at+1+HEIGHT]
        if lines[at] != name or any(len(row) != WIDTH or set(row)-set(bb.CHARS) for row in rows):
            raise ValueError(f"Malformed scene card {name}")
        struct.pack_into("<H", data, 2*i, 16+len(data))
        data += bb.pack_sprite(WIDTH, HEIGHT, [[bb.CHARS.index(c) for c in row] for row in rows])
    return bytes(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--select", type=Path)
    args = parser.parse_args()
    if args.select:
        select(args.select)
    print(f"Four VGA scene cards, {len(build_scene_bank())} bytes")

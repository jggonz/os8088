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
SCENES = (
    ("lesson", "Borat's home row lesson selection-2.png", (950, 126, 1436, 450), (208, 120)),
    ("record", "Borat’s Typing Records and Achievements-6.png", (915, 203, 1427, 602), (208, 104)),
    ("result", "Borat's typing challenge results-4.png", (13, 201, 548, 659), (176, 120)),
    ("settings", "Borat Teaches Typing_ Sound and Display-5.png", (807, 595, 1436, 880), (240, 88)),
)


def select(directory):
    from PIL import Image
    palette = Image.new("P", (1, 1))
    palette.putpalette([v for i in range(256) for v in bb.EGA[i if i < 16 and i != bb.KEY else 0]])
    out = []
    for name, filename, crop, (width,height) in SCENES:
        with Image.open(directory / filename) as source:
            if source.size != (1448, 1086):
                raise ValueError(f"Unexpected dimensions for {filename}")
            panel = source.convert("RGB").crop(crop).resize((width, height), Image.Resampling.LANCZOS)
            image = panel.quantize(palette=palette, dither=Image.Dither.FLOYDSTEINBERG)
            out.append(name)
            for y in range(height):
                row = []
                for x in range(width):
                    index = image.getpixel((x, y))
                    row.append(bb.CHARS[index if index < 16 and index != bb.KEY else 0])
                out.append("".join(row))
    ART.write_text("\n".join(out) + "\n")


def build_scene_bank():
    """BTAN bytes16..43: four scenes, five color/five gray16px badges."""
    lines = ART.read_text().splitlines()
    data = bytearray(2*(len(SCENES)+10))
    at = 0
    for i, (name, _, _, (width,height)) in enumerate(SCENES):
        rows = lines[at+1:at+1+height]
        if len(rows)!=height or lines[at]!=name or any(len(row)!=width or set(row)-set(bb.CHARS) for row in rows):
            raise ValueError(f"Malformed scene card {name}")
        struct.pack_into("<H", data, 2*i, 16+len(data))
        if width % 8 or any("." in row for row in rows):
            raise ValueError(f"Opaque byte-aligned pixels required for {name}")
        pixels = [bb.CHARS.index(c) for row in rows for c in row]
        data.extend((width, height))
        for plane in range(4):
            for at_pixel in range(0, len(pixels), 8):
                data.append(sum(((pixel >> plane) & 1) << (7 - bit)
                                for bit, pixel in enumerate(pixels[at_pixel:at_pixel+8])))
        at += height+1
    if at != len(lines):
        raise ValueError("Unexpected data after the last scene")
    badges = (ART.parent / 'badges.mask').read_text().splitlines()
    if len(badges)!=120 or any(len(row)!=24 or set(row)-set(bb.CHARS) for row in badges):
        raise ValueError('badges must be five24x24 palette masks')
    for gray in range(2):
        for i in range(5):
            pixels = [[bb.CHARS.index(badges[i*24+y*24//16][x*24//16]) for x in range(16)] for y in range(16)]
            if gray:
                pixels = [[c if c==bb.KEY else 8 if sum(bb.EGA[c])<255 else 7 if sum(bb.EGA[c])<570 else 15 for c in row] for row in pixels]
            struct.pack_into('<H', data, 2*(len(SCENES)+gray*5+i), 16+len(data))
            data += bb.pack_sprite(16,16,pixels)
    return bytes(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--select", type=Path)
    args = parser.parse_args()
    if args.select:
        select(args.select)
    print(f"Four VGA scene cards, {len(build_scene_bank())} bytes")

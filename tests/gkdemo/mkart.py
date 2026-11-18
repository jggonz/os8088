#!/usr/bin/env python3
"""mkart.py - GKDART.BIN, the game kit gallery's art part (SPEC.md 132).

The core's art layout (SPEC.md 130.6.3, tools/os88bbart.py's packers): 'GK',
a count, a word offset per gkd.h ID, then the records - three WIN16 fonts,
two hand-drawn sprites and one scene compiled by tools/os88gk.py.

    mkart.py -o GKDART.BIN
"""
import argparse, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bbart as bb          # noqa: E402
import os88gk                   # noqa: E402

FONTS = {"GD_F_TEXT13": "dlg13", "GD_F_TEXT10": "dlg10", "GD_F_BIG16": "sys16"}


def fail(msg):
    sys.exit(f"gkdemo/mkart: {msg}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", required=True)
    a = ap.parse_args()
    hdr = os.path.join(HERE, "gkd.h")
    sym = os88gk.read_symbols([hdr])
    ids = sorted((v, k) for k, v in sym.items() if k != "GD_A_COUNT")
    if [v for v, _ in ids] != list(range(sym["GD_A_COUNT"])):
        fail("gkd.h: IDs are dense from 0 and GD_A_COUNT follows them")
    sprites = {}
    lines = open(os.path.join(HERE, "sprites.txt")).read().split("\n")
    i = 0
    while i < len(lines):
        m = re.match(r"sprite (\w+) (\d+) (\d+)$", lines[i].strip())
        i += 1
        if not m:
            continue
        w, h = int(m.group(2)), int(m.group(3))
        rows = []
        for r in range(h):
            row = lines[i + r]
            if len(row) != w or any(c not in bb.CHARS for c in row):
                fail(f"sprites.txt:{i + r + 1}: {m.group(1)} row {r}: want {w} of {bb.CHARS!r}")
            rows.append([bb.CHARS.index(c) for c in row])
        i += h
        sprites[m.group(1)] = bb.pack_sprite(w, h, rows)
    try:
        scenes = os88gk.compile_scenes([os.path.join(HERE, "scenes.txt")], sym)
    except os88gk.GkError as e:
        fail(str(e))
    head = bytearray(b"GK" + struct.pack("<H", len(ids)) + bytes(2 * len(ids)))
    body = bytearray()
    for k, (v, name) in enumerate(ids):
        if name in FONTS:
            blob = bb.pack_font(FONTS[name])
        elif name in sprites:
            blob = sprites[name]
        elif v in scenes:
            blob = scenes[v]
        else:
            fail(f"{name}: no font, sprite or scene")
        off = len(head) + len(body)
        if off & 1:
            body.append(0)
            off += 1
        struct.pack_into("<H", head, 4 + 2 * k, off)
        body += blob
    data = bytes(head + body)
    with open(a.o, "wb") as f:
        f.write(data)
    print(f"gkdemo/mkart: {a.o}: {len(data)} bytes, scene {len(scenes[sym['GD_S_LANDING']])} bytes")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""os88bpart.py - Byteopoly's art part, BPART.BIN (SPEC.md 131.10).

FOUNDATION STUB. It reads apps/byteopoly/art.h and writes the header with
every offset 0, which the drawing core reads as "absent": sprites draw
nothing and text falls back to the kernel's 8x8 font. The art lane replaces
this with the real packer (sprites.txt, the WIN16 font strikes, help text),
sharing tools/os88bbart.py's packing code rather than copying it.

    build -o BPART.BIN     write the part
    --selfcheck            every ID once, in order
"""
import argparse, os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART_H = os.path.join(ROOT, "apps", "byteopoly", "art.h")


def read_ids(path=ART_H):
    ids = {}
    for line in open(path):
        m = re.match(r"#define\s+(BP_[AF]_[A-Z0-9_]+)\s+(\d+)\s*$", line)
        if m:
            ids[m.group(1)] = int(m.group(2))
    count = ids.pop("BP_A_COUNT")
    order = sorted(ids.items(), key=lambda kv: kv[1])
    if [v for _, v in order] != list(range(count)):
        sys.exit("os88bpart: art.h ids are not exactly 0..%d" % (count - 1))
    return count, [k for k, _ in order]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["build"])
    ap.add_argument("-o", dest="out", required=True)
    ap.add_argument("--selfcheck", action="store_true")
    a = ap.parse_args()
    count, _names = read_ids()
    data = b"BP" + struct.pack("<H", count) + b"\0\0" * count
    with open(a.out, "wb") as f:
        f.write(data)


if __name__ == "__main__":
    main()

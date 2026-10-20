#!/usr/bin/env python3
"""MSEG's WIDE parts - the same module, padded until the carve passes 64KB.

    python3 tests/multiseg/mkwide.py noise|text <bytes> <in.bin> <out.bin>

SPEC.md 20.12.11 lets a parted package's eager carve run past one segment.
tests/multiseg.py --wide is the row that says it does, and it needs a carve
that is past 64KB at BOTH ends: packed, which is what op_read moves, and
unpacked, which is what op_claim cuts and op_unpack walks. So the plain part
1 is padded with NOISE, which cannot get smaller, and the compressed part 2
with TEXT, which can - 179 sectors either way, 146 packed with part 2
compressed (measured when this was written; the row asserts > 128 on both
sides rather than either figure, so a change to the modules does not break
it).

Padding goes AFTER the module, past everything it checks: the signature, the
far-callable entry and the summed data area all sit where they did, and only
where the NEXT part lands moves - which is the arithmetic under test.

Deterministic: a fixed LCG, so the image is reproducible byte for byte like
every other in build/ (CLAUDE.md, "Nothing in build/ is tracked").
"""
import sys


def lcg(seed):
    x = seed
    while True:
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        yield x >> 16


def noise(n, seed=77):
    g = lcg(seed)
    return bytes(next(g) & 0xFF for _ in range(n))


def text(n, seed=78):
    """words from a small vocabulary with short runs of noise between them -
    packs to about two thirds, so a compressed row of it is still big"""
    g = lcg(seed)
    words = [bytes(97 + next(g) % 26 for _ in range(3 + next(g) % 6))
             for _ in range(500)]
    out = bytearray()
    while len(out) < n:
        if next(g) % 100 < 30:
            out += bytes(next(g) & 0xFF for _ in range(1 + next(g) % 11))
        else:
            out += words[next(g) % 500] + b" "
    return bytes(out[:n])


def main():
    if len(sys.argv) != 5 or sys.argv[1] not in ("noise", "text"):
        sys.exit(__doc__.strip().splitlines()[2].strip())
    kind, n, src, dst = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    pad = (noise if kind == "noise" else text)(n)
    with open(src, "rb") as f:
        body = f.read()
    with open(dst, "wb") as f:
        f.write(body + pad)


if __name__ == "__main__":
    main()

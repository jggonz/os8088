#!/usr/bin/env python3
"""wadelta - WireArena's keyframes DELTA-coded for the disk.

A keyframe is NFACE facing rows of NJ (x, y) byte pairs (tools/arenagen.py's
wa_pose), and a joint moves little from one facing to the next and from one
keyframe to the next. LZ sees none of that: the raw keyframes pack to ~80%.
Each byte less the one a ROW (WA_NJ * 2) before it, and that again less the
one a KEYFRAME (WA_FRAMESZ) before it, packs to under half - ~5KB off the
package. The game undoes it once, in place, at entry (wirearena.asm's
wa_undelta: a running sum a stride, the two in either order - they commute).

  python3 tools/wadelta.py --tab build/watab.inc --pose  IN OUT
        the POSE part (wapose.bin): every byte of it, the cold keyframes
  python3 tools/wadelta.py --tab build/watab.inc --part  IN OUT
        the RENDERER part (wrpart.bin): the hot keyframes, at the offset
        its header's WR_H_POSE word names (apps/wirearena/wafb.inc)

IN stays the image the assembler made - what the game holds once decoded,
and what every tool that maps a build reads; OUT is only what the package
carries. Deterministic, and refuses rather than guesses: a header that is
not the renderer's, or a length that is not whole keyframes, is an error.
"""
import re, sys

WR_H_POSE = 20                      # (apps/wirearena/wafb.inc)


def consts(tab):
    src = open(tab).read()
    want = ("WA_NJ", "WA_FRAMESZ", "WA_KHOT", "WA_NFRAMES")
    got = {k: int(v) for k, v in re.findall(r"^(WA_[A-Z]+)\s+equ\s+(\d+)", src, re.M)
           if k in want}
    for k in want:
        if k not in got:
            raise SystemExit("wadelta: %s has no %s" % (tab, k))
    return got


def code(b, strides):
    b = bytearray(b)
    for s in strides:
        for i in range(len(b) - 1, s - 1, -1):
            b[i] = (b[i] - b[i - s]) & 0xFF
    return b


def undo(b, strides):               # wa_undelta's loops, byte for byte
    b = bytearray(b)
    for s in strides:
        for i in range(s, len(b)):
            b[i] = (b[i] + b[i - s]) & 0xFF
    return b


def main(argv):
    if len(argv) != 5 or argv[0] != "--tab" or argv[2] not in ("--pose", "--part"):
        raise SystemExit(__doc__)
    c = consts(argv[1])
    src, dst = argv[3], argv[4]
    img = bytearray(open(src, "rb").read())
    strides = (c["WA_NJ"] * 2, c["WA_FRAMESZ"])
    if argv[2] == "--pose":
        at, n = 0, (c["WA_NFRAMES"] - c["WA_KHOT"]) * c["WA_FRAMESZ"]
        if len(img) != n:
            raise SystemExit("wadelta: %s is %d bytes, not the %d keyframes' %d"
                             % (src, len(img), c["WA_NFRAMES"] - c["WA_KHOT"], n))
    else:
        if img[:6] != b"WRPART":
            raise SystemExit("wadelta: %s is not the renderer part" % src)
        at = img[WR_H_POSE] | img[WR_H_POSE + 1] << 8
        n = c["WA_KHOT"] * c["WA_FRAMESZ"]
        if at < WR_H_POSE + 2 or at + n > len(img):
            raise SystemExit("wadelta: %s's WR_H_POSE (%d) does not hold %d bytes of "
                             "keyframes" % (src, at, n))
    body = img[at:at + n]
    z = code(body, strides)
    if undo(z, strides) != body:
        raise SystemExit("wadelta: the coding did not round-trip - a bug here")
    img[at:at + n] = z
    open(dst, "wb").write(img)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

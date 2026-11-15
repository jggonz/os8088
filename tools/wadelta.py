#!/usr/bin/env python3
"""wadelta - WireArena's keyframes CODED for the disk.

A keyframe is NFACE facing rows of NJ (x, y) byte pairs (tools/arenagen.py's
wa_pose, pre-projected for each facing). LZ sees none of their shape, and
the raw keyframes pack to ~80%. Three facts it cannot see, and this coding
takes out:

  - the second half of the facings is the first MIRRORED: facing k + NFACE/2
    is the view turned half round, so each joint's x is negated and its y
    the same (arenagen's projection is symmetric - checked here, and a
    keyframe that is not so is refused, never coded lossily);
  - a joint moves little from one facing to the next (a ROW, WA_NJ * 2);
  - ...and little from one keyframe to the next (WA_FRAMESZ).

So the file holds, a keyframe at a time, its first half's rows each less the
row before (the first row as it is), the second half as zeros, and then the
whole of it less the keyframe before. That packs the pose part from 7,946
bytes to ~2,200 and the renderer part's hot keyframes from ~2,300 to ~600.
The game undoes it once, in place, at entry (wirearena.asm's wa_undelta: the
keyframes' running sum, then per keyframe the rows' and the mirror).

  python3 tools/wadelta.py --tab build/watab.inc --pose  IN OUT
        the POSE part (wapose.bin): every byte of it, the cold keyframes
  python3 tools/wadelta.py --tab build/watab.inc --part  IN OUT
        the RENDERER part (wrpart.bin): the hot keyframes, at the offset
        its header's WR_H_POSE word names (apps/wirearena/wafb.inc)

IN stays the image the assembler made - what the game holds once decoded,
and what every tool that maps a build reads; OUT is only what the package
carries. Deterministic, and refuses rather than guesses: a header that is not
the renderer's, a length that is not whole keyframes, or a facing that is
not its opposite's mirror is an error.
"""
import re, sys

WR_H_POSE = 20                      # (apps/wirearena/wafb.inc)


def consts(tab):
    src = open(tab).read()
    want = ("WA_NJ", "WA_NFACE", "WA_FRAMESZ", "WA_KHOT", "WA_NFRAMES")
    got = {k: int(v) for k, v in re.findall(r"^(WA_[A-Z]+)\s+equ\s+(\d+)", src, re.M)
           if k in want}
    for k in want:
        if k not in got:
            raise SystemExit("wadelta: %s has no %s" % (tab, k))
    if got["WA_FRAMESZ"] != got["WA_NFACE"] * got["WA_NJ"] * 2 or got["WA_NFACE"] % 2:
        raise SystemExit("wadelta: a keyframe is not NFACE rows of NJ byte pairs")
    return got


def code(b, F, R, what):
    b = bytearray(b); H = F // 2
    for f in range(0, len(b), F):       # the mirror: checked, then zeros
        for i in range(f + H, f + F):
            j = i - H
            if b[i] != ((-b[j]) & 0xFF if (j - f) % 2 == 0 else b[j]):
                raise SystemExit("wadelta: %s keyframe %d: facing %d is not facing %d "
                                 "mirrored (x negated, y the same) - the coding is "
                                 "lossless or nothing" % (what, f // F, (i - f) // R,
                                                         (j - f) // R))
            b[i] = 0
    for f in range(0, len(b), F):       # the rows, in the first half
        for i in range(f + H - 1, f + R - 1, -1):
            b[i] = (b[i] - b[i - R]) & 0xFF
    for i in range(len(b) - 1, F - 1, -1):  # the keyframes
        b[i] = (b[i] - b[i - F]) & 0xFF
    return b


def undo(b, F, R):                  # wa_undelta's loops, byte for byte
    b = bytearray(b); H = F // 2
    for i in range(F, len(b)):
        b[i] = (b[i] + b[i - F]) & 0xFF
    for f in range(0, len(b), F):
        for i in range(f + R, f + H):
            b[i] = (b[i] + b[i - R]) & 0xFF
        for i in range(f + H, f + F):
            b[i] = (-b[i - H]) & 0xFF if (i - f) % 2 == 0 else b[i - H]
    return b


def main(argv):
    if len(argv) != 5 or argv[0] != "--tab" or argv[2] not in ("--pose", "--part"):
        raise SystemExit(__doc__)
    c = consts(argv[1])
    src, dst = argv[3], argv[4]
    img = bytearray(open(src, "rb").read())
    F, R = c["WA_FRAMESZ"], c["WA_NJ"] * 2
    if argv[2] == "--pose":
        at, n = 0, (c["WA_NFRAMES"] - c["WA_KHOT"]) * F
        if len(img) != n:
            raise SystemExit("wadelta: %s is %d bytes, not the %d keyframes' %d"
                             % (src, len(img), c["WA_NFRAMES"] - c["WA_KHOT"], n))
    else:
        if img[:6] != b"WRPART":
            raise SystemExit("wadelta: %s is not the renderer part" % src)
        at = img[WR_H_POSE] | img[WR_H_POSE + 1] << 8
        n = c["WA_KHOT"] * F
        if at < WR_H_POSE + 2 or at + n > len(img):
            raise SystemExit("wadelta: %s's WR_H_POSE (%d) does not hold %d bytes of "
                             "keyframes" % (src, at, n))
    body = img[at:at + n]
    z = code(body, F, R, src)
    if undo(z, F, R) != body:
        raise SystemExit("wadelta: the coding did not round-trip - a bug here")
    img[at:at + n] = z
    open(dst, "wb").write(img)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

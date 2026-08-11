#!/usr/bin/env python3
"""Look at what subcheck/ptcheck captured, and at where two runs disagree.

    python3 tools/rawdiff.py show DIR                 # every step -> a PNG
    python3 tools/rawdiff.py show DIR 03-drag.raw     # ...or just one
    python3 tools/rawdiff.py diff DIR-A DIR-B         # bboxes + diff PNGs
    python3 tools/rawdiff.py zoom DIR-A DIR-B NAME X1 Y1 X2 Y2 [Z]

`diff`'s PASS/FAIL answer lives in subcheck.py; this is the half that tells you
WHERE and WHAT, which is the half that identifies a defect. Three findings this
round came out of it and none of them was visible in a pixel count:

  A COUNT MISATTRIBUTES. 12,663 differing pixels looked like a redraw defect and
  were two runs with the windows in DIFFERENT PLACES - a dropped mouse packet.
  The bbox said so at once, being window-shaped and offset.

  AND IT CANNOT NAME THE SHAPE. 45 differing pixels over a dock tile were the
  MOUSE ARROW, present in one capture and absent in the other; zoomed, the red
  was arrow-shaped and unmistakable. Both gates mask the cursor now.

  AND IT CANNOT SAY WHICH SIDE IS WRONG. `show` renders each run on its own,
  which is how "the minimized window is still on screen" was read off a capture
  - the diff mask alone says only that the two disagree.

PNGs are written beside the captures. 1bpp captures render black/white; a VGA
capture is rgb24 and renders as its own red channel, which is enough to see a
shape and not a colour proof.
"""
import os
import struct
import sys
import zlib

MASK_Y = 18                     # subcheck/ptcheck drop the menu bar


def load(path):
    d = open(path, "rb").read()
    w, bpp = struct.unpack_from("<HH", d)
    body = d[4:]
    return w, bpp, body, len(body) // (w * bpp)


def png(path, w, h, rows):
    def chunk(t, data):
        c = t + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I",
                                                             zlib.crc32(c))
    raw = b"".join(b"\0" + r for r in rows)
    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def px(body, w, bpp, x, y):
    return body[(y * w + x) * bpp]


def steps(d):
    return sorted(f for f in os.listdir(d) if f.endswith(".raw"))


def show(d, only=None):
    for name in steps(d):
        if only and name != only:
            continue
        w, bpp, body, h = load(os.path.join(d, name))
        rows = []
        for y in range(h):
            r = bytearray()
            for x in range(w):
                v = 255 if px(body, w, bpp, x, y) else 0
                r += bytes((v, v, v))
            rows.append(bytes(r))
        out = os.path.join(d, name[:-4] + ".png")
        png(out, w, h, rows)
        print("%s  %dx%d -> %s" % (name, w, h, out))


def diff(a, b, zoomargs=None):
    fa, fb = steps(a), steps(b)
    if fa != fb:
        raise SystemExit("rawdiff: different steps\n  %r\n  %r" % (fa, fb))
    for name in fa:
        wa, bpa, pa, ha = load(os.path.join(a, name))
        wb, bpb, pb, hb = load(os.path.join(b, name))
        if (wa, bpa, ha) != (wb, bpb, hb):
            raise SystemExit("rawdiff: %s geometry differs" % name)
        xs, ys, rows = [], [], []
        for y in range(ha):
            r = bytearray()
            for x in range(wa):
                va, vb = px(pa, wa, bpa, x, y), px(pb, wb, bpb, x, y)
                if va != vb:
                    xs.append(x)
                    ys.append(y + MASK_Y)
                    r += b"\xff\x00\x00"            # red: they disagree
                else:
                    v = 255 if va else 0
                    r += bytes((v, v, v))
            rows.append(bytes(r))
        if not xs:
            print("%-24s 0" % name)
            continue
        print("%-24s %6d  bbox x %d..%d  y %d..%d" %
              (name, len(xs), min(xs), max(xs), min(ys), max(ys)))
        out = os.path.join(b, name[:-4] + "-diff.png")
        png(out, wa, ha, rows)
        print("%-24s        -> %s" % ("", out))


def zoom(a, b, name, x1, y1, x2, y2, z=8):
    """Both runs' pixels side by side, enlarged - B's differences in red.

    Coordinates are the GUEST's, y included: the captures start at MASK_Y and
    this adds it back, so a bbox from `diff` can be pasted straight in.
    """
    wa, bpa, pa, ha = load(os.path.join(a, name))
    _, _, pb, _ = load(os.path.join(b, name))
    for tag, marked in (("A", False), ("B", True)):
        rows = []
        for y in range(y1, y2 + 1):
            r = bytearray()
            for x in range(x1, x2 + 1):
                yy = y - MASK_Y
                if not (0 <= yy < ha and 0 <= x < wa):
                    continue
                va, vb = px(pa, wa, bpa, x, yy), px(pb, wa, bpa, x, yy)
                if marked and va != vb:
                    col = (255, 0, 0)
                else:
                    v = 255 if (vb if marked else va) else 0
                    col = (v, v, v)
                r += bytes(col) * z
            for _ in range(z):
                rows.append(bytes(r))
        out = os.path.join(b, "%s-zoom-%s.png" % (name[:-4], tag))
        png(out, (x2 - x1 + 1) * z, (y2 - y1 + 1) * z, rows)
        print("%s -> %s" % (tag, out))


def main():
    v = sys.argv[1:]
    if len(v) >= 2 and v[0] == "show":
        show(v[1], v[2] if len(v) > 2 else None)
    elif len(v) == 3 and v[0] == "diff":
        diff(v[1], v[2])
    elif len(v) >= 8 and v[0] == "zoom":
        zoom(v[1], v[2], v[3], *[int(x) for x in v[4:8]],
             **({"z": int(v[8])} if len(v) > 8 else {}))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()

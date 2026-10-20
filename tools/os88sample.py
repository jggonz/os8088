#!/usr/bin/env python3
"""os88sample.py - draw Paint's sample picture and write it as a 1bpp BMP.

The other four sample documents on the office disk (SPEC.md 24.6) are TEXT
and are committed as source: PAPER.TEX, GUIDE.TEX, SALES.SLK and WRITING.MD
can all be read and corrected in a diff.  A picture cannot, so this one is
GENERATED, for the reason tools/os88logo.py gives for the logo and fonts/*.f8
gives for the faces: a bitmap's defects are entirely visual, and a blob in
the tree is a thing nobody can review or re-derive.  Run it, look at the PNG,
change a number, look again.

WHAT IT DRAWS: the five marks Paint makes - a line, a box, a filled box, an
oval and a freehand curve - each in its own tile with its name under it, and
the app's own name over the top.  A sample document on this system documents
the application it opens in (WELCOME.DOC is a letter Word sets, GUIDE.TEX is
TeXPad's markup written up in that markup, BROWSER.HTM is the browser's
manual), and this is that rule for a program whose format is pixels.

THREE BOUNDS, all hard:

  * THE CANVAS.  Paint's canvas IS its content and pt_adopt CROPS rather
    than scales (SPEC.md 42), so a picture bigger than the smallest adapter
    can show loses its edges there.  CGA's 640x200 desktop band is the
    floor and it gives 594 x 110 - the same bound os88logo.py draws the
    logo inside, and for the same reason.  See MAXW/MAXH.
  * THE DEPTH.  1bpp, because pt_bmp_in takes 1, 4, 8 or 24 and one bit is
    both the smallest file and the only depth that is already right on a
    Hercules and a CGA.  The two palette entries are a dark/light pair, so
    pt_mono2 recognises it and SPEC.md 42.23's one-bit canvas path takes it
    whole; a 1bpp BMP whose palette is red on blue would pass the depth
    test and fail that one.
  * THE MARKS.  Solid ink or the 50% dither, nothing else - SPEC.md 39.4's
    classes, so nothing here has to survive a colour reduction.

The pixel is not square on two adapters of three (1.00 on VGA, 1.55 on
Hercules, 2.40 on CGA), so the drawing is rectangles and straight lines
wherever it can be: those are the marks that stay themselves when the pixel
stretches.  --png writes the preview at all three, which is the only way to
choose the proportions.

  python3 tools/os88sample.py -o build/SAMPLE.BMP
  python3 tools/os88sample.py -o build/SAMPLE.BMP --png build/sample
"""

import argparse
import os
import struct
import sys
import zlib

# What the smallest supported screen can show, from Paint's own arithmetic -
# os88logo.py's LOGO_MAXW/LOGO_MAXH, which carries the derivation.  The
# height is drawn to MAXH exactly; the width is well inside MAXW, for the
# reason below.
MAXW, MAXH = 594, 110

# 448 - PT_CW_DEF, the canvas a fresh Paint starts with - so opening this
# picture leaves the window exactly the size the app had already chosen.
#
# IT WAS 466 FOR A WHILE, and that was a recorded workaround for a Paint
# defect rather than a preference: a picture that did not GROW Paint's window
# decoded perfectly into the canvas and was then never drawn, so 448 - the
# one width that leaves a fresh CGA window alone - opened white under a toast
# saying "Opened".  SPEC.md 11.90.3.2 is the fix and the account: wm_resize
# withholds the damage rect from a window that did not grow (11.90.3.1),
# which is correct, and pt_onwake resizes AFTER a load has replaced the whole
# canvas, which 11.90.3.1's safety argument predated.  pt_adopt raises
# [pt_cvnew] now and pt_dmg_get spends it.
#
# So the width is BACK to 448, which is what 24.6.2.1 said should happen when
# the defect was fixed - and it is worth more here than a nicer number: the
# shipped sample is now a picture in the class that used to fail, so a
# machine that boots the office disk and opens it exercises the regression.
# tests/paintnogrow.py is the row that asserts it.
W, H = 448, 110

INK, PAPER = 1, 0

FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    os.pardir, "fonts", "tallx.f8")


class Bitmap:
    """A one-bit page, row-major, origin top-left."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [bytearray(w) for _ in range(h)]

    def set(self, x, y, v=INK):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = v

    def hline(self, x0, x1, y, v=INK):
        for x in range(min(x0, x1), max(x0, x1) + 1):
            self.set(x, y, v)

    def vline(self, x, y0, y1, v=INK):
        for y in range(min(y0, y1), max(y0, y1) + 1):
            self.set(x, y, v)

    def rect(self, x0, y0, x1, y1, v=INK):
        self.hline(x0, x1, y0, v)
        self.hline(x0, x1, y1, v)
        self.vline(x0, y0, y1, v)
        self.vline(x1, y0, y1, v)

    def dither(self, x0, y0, x1, y1):
        """The desktop's 50% checkerboard - SPEC.md 39.4's grey."""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, INK if (x + y) & 1 else PAPER)

    def line(self, x0, y0, x1, y1, v=INK):
        """Bresenham, the same shape gfx_line runs."""
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.set(x0, y0, v)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def ellipse(self, cx, cy, rx, ry, v=INK):
        """Midpoint ellipse, outline only - four-way symmetric, so the two
        halves cannot disagree by a pixel the way two independent arcs can."""
        x, y = 0, ry
        rx2, ry2 = rx * rx, ry * ry
        d = ry2 - rx2 * ry + rx2 // 4
        dx, dy = 0, 2 * rx2 * y
        while dx < dy:
            for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
                self.set(cx + sx * x, cy + sy * y, v)
            x += 1
            dx += 2 * ry2
            if d < 0:
                d += ry2 + dx
            else:
                y -= 1
                dy -= 2 * rx2
                d += ry2 + dx - dy
        d = (ry2 * (x * x + x) + rx2 * (y - 1) * (y - 1) - rx2 * ry2 + 2) // 4
        while y >= 0:
            for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
                self.set(cx + sx * x, cy + sy * y, v)
            y -= 1
            dy -= 2 * rx2
            if d > 0:
                d += rx2 - dy
            else:
                x += 1
                dx += 2 * ry2
                d += rx2 - dy + dx


def load_font():
    """The tree's own 8x8 face, so the labels are the machine's letters and
    not the host's - os88logo.py's icon sheet borrows it the same way."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from os88font import parse
    return parse(FONT)


def text(bm, font, s, x, y, scale=1):
    """One 8x8 cell a character, `scale` whole pixels a pixel.  Whole
    pixels only: a fractional scale is a resampled glyph, and a resampled
    8x8 glyph is a smear."""
    for ch in s:
        gl = font.get(ord(ch))
        if gl is not None:
            for row in range(8):
                bits = gl[row]
                for col in range(8):
                    if bits & (0x80 >> col):
                        for sy in range(scale):
                            for sx in range(scale):
                                bm.set(x + col * scale + sx,
                                       y + row * scale + sy)
        x += 8 * scale
    return x


def textw(s, scale=1):
    return len(s) * 8 * scale


def centred(bm, font, s, cx, y, scale=1):
    return text(bm, font, s, cx - textw(s, scale) // 2, y, scale)


# --- the five tiles ---------------------------------------------------------
# Each draws inside its own (x, y, w, h) box and knows nothing about the
# layout, so the strip below can be re-spaced without touching any of them.

def tile_line(bm, x, y, w, h):
    """A fan of straight lines - what the Line tool leaves."""
    for i in range(5):
        bm.line(x + 4, y + h - 5, x + 4 + (w - 9) * i // 4, y + 4)


def tile_box(bm, x, y, w, h):
    """Nested rectangles - the Box tool, hollow."""
    bm.rect(x + 4, y + 4, x + w - 5, y + h - 5)
    bm.rect(x + 12, y + 11, x + w - 13, y + h - 12)


def tile_fill(bm, x, y, w, h):
    """A rectangle in the 50% dither, framed solid: the two greys this
    machine has, side by side."""
    bm.dither(x + 5, y + 5, x + w - 6, y + h - 6)
    bm.rect(x + 4, y + 4, x + w - 5, y + h - 5)


def tile_oval(bm, x, y, w, h):
    """Two concentric ovals - the Oval tool, and the one mark here that
    the pixel aspect visibly stretches."""
    cx, cy = x + w // 2, y + h // 2
    bm.ellipse(cx, cy, w // 2 - 5, h // 2 - 5)
    bm.ellipse(cx, cy, w // 4, h // 4)


def tile_free(bm, x, y, w, h):
    """A freehand stroke: a decaying wave, plotted as the chords a hand
    would leave rather than as a curve, because that is what pt_seg gets
    (SPEC.md 42.8)."""
    import math
    pts = []
    for i in range(0, w - 8, 3):
        t = i / float(w - 9)
        yy = math.sin(t * 3.0 * math.pi) * (1.0 - 0.55 * t)
        pts.append((x + 4 + i, int(round(y + h / 2.0 + yy * (h / 2.0 - 6)))))
    for a, b in zip(pts, pts[1:]):
        bm.line(a[0], a[1], b[0], b[1])


TILES = [("Line", tile_line), ("Box", tile_box), ("Fill", tile_fill),
         ("Oval", tile_oval), ("Free", tile_free)]


def draw():
    bm = Bitmap(W, H)
    font = load_font()

    bm.rect(0, 0, W - 1, H - 1)                 # the page's own edge
    centred(bm, font, "os8088 Paint", W // 2, 6, 2)
    centred(bm, font, "a sample drawing - open it, change it, save it",
            W // 2, 26)

    # The strip: five tiles across the width inside a 12px margin, with the
    # gap taken from what is left rather than fixed, so changing the tile
    # count re-spaces instead of overflowing.
    margin, tw = 12, 76
    span = W - 2 * margin
    gap = (span - len(TILES) * tw) // (len(TILES) - 1)
    ty, th = 40, 52
    for i, (name, fn) in enumerate(TILES):
        tx = margin + i * (tw + gap)
        fn(bm, tx, ty, tw, th)
        centred(bm, font, name, tx + tw // 2, ty + th + 3)
    return bm


# --- the BMP ----------------------------------------------------------------

def bmp1(bm):
    """A 1bpp BITMAPINFOHEADER BMP, bottom-up, two palette entries.

    BOTTOM-UP and not top-down, though pt_bmp_in reads either (biHeight
    negative): bottom-up is what every writer of the era emitted, so it is
    what a file arriving from another machine looks like, and a sample
    should be an ordinary file rather than a convenient one.

    THE PALETTE IS BLACK THEN WHITE, so bit set = white.  That is the BMP
    convention, and it is also the polarity SPEC.md 42.23 stores the
    one-bit canvas in - which is not a coincidence: agreeing with the
    format is what lets the packed arm `rep movsw` a row instead of
    complementing it.
    """
    stride = ((W + 31) // 32) * 4
    pix = bytearray()
    for y in range(H - 1, -1, -1):
        row = bytearray(stride)
        for x in range(W):
            if bm.px[y][x] == PAPER:            # PAPER is white, and white
                row[x >> 3] |= 0x80 >> (x & 7)  # is palette entry 1
        pix += row
    off = 14 + 40 + 8
    hdr = (b"BM" + struct.pack("<IHHI", off + len(pix), 0, 0, off)
           + struct.pack("<IiiHHIIiiII", 40, W, H, 1, 1, 0, len(pix),
                         2835, 2835, 2, 0)
           + b"\x00\x00\x00\x00" + b"\xff\xff\xff\x00")
    return hdr + pix


def png(path, bm, sx, sy):
    """The preview, at one adapter's pixel aspect.  Grey scanlines, one
    byte a pixel: there is no Pillow in a fresh container and this picture
    is two colours (os88logo.py's _png, same reasoning)."""
    w, h = int(W * sx), int(H * sy)
    raw = bytearray()
    for y in range(h):
        row = bm.px[min(H - 1, int(y / sy))]
        raw.append(0)
        raw += bytes(0 if row[min(W - 1, int(x / sx))] == INK else 255
                     for x in range(w))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--out", required=True, help="the .BMP to write")
    ap.add_argument("--png", metavar="STEM",
                    help="also write STEM-{vga,herc,cga}.png previews")
    args = ap.parse_args()

    # A BOUND CHECKED RATHER THAN HOPED FOR: if the floor adapter's canvas
    # ever shrinks, this must be a failed BUILD and not a quietly cropped
    # picture that only a CGA owner ever sees.
    if W > MAXW or H > MAXH:
        sys.exit("os88sample: %dx%d is past the floor adapter's %dx%d canvas"
                 % (W, H, MAXW, MAXH))

    bm = draw()
    data = bmp1(bm)
    with open(args.out, "wb") as fh:
        fh.write(data)
    print("os88sample: %dx%d 1bpp -> %s (%d bytes)"
          % (W, H, args.out, len(data)))

    if args.png:
        for name, sx in (("vga", 1.00), ("herc", 1.55), ("cga", 2.40)):
            png("%s-%s.png" % (args.png, name), bm, sx, 1.0)
        print("os88sample: previews at %s-{vga,herc,cga}.png" % args.png)


if __name__ == "__main__":
    main()

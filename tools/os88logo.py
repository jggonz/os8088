#!/usr/bin/env python3
"""os88logo.py - draw the os8088 logo and write it as a monochrome GIF87a.

The artwork is GENERATED rather than committed, for the reason fonts/*.f8 is
ASCII art rather than hex: a picture's defects are entirely visual, and a
4KB LZW blob in the tree is a thing nobody can review or re-derive.  Run it
and look at the PNG; change a number and look again.

What it draws (SPEC.md 63): an 8088 in its 40-pin DIP, sitting on the
desktop's 50% dither, with "os8088" knocked out of the package in white.
The chip is the OS's own iconography already - it is what the System menu
draws, and what About shows - and every mark in it is one of SPEC.md 39.4's
three classes, so nothing has to survive a colour reduction.

Three bounds it is drawn inside, and all three are hard:

  * THE SCREEN.  Paint's canvas IS its content (SPEC.md 42), so a picture
    wider or taller than the biggest canvas the screen can show is CROPPED
    by pt_adopt, not scaled.  The smallest of the three adapters bounds it,
    and that is CGA on height: 594 x 110.  See LOGO_MAXW/LOGO_MAXH.
  * THE FILE.  4KB, asked for, and checked here rather than hoped for.
  * THE DECODER.  Paint reads GIF87a with a global colour table, LZW
    minimum code size 2..8, non-interlaced (SPEC.md 42) - so the palette is
    two entries and the minimum code size is 2, which is the floor GIF
    itself sets and not a choice.

The pixel is not square on two adapters out of three, and no bitmap can be
right on all of them: a row is 1.0 pixel wide on VGA (640x480 in 4:3), 1.55
on Hercules (720x348) and 2.40 on CGA (640x200).  --png writes the preview
at all three, which is the only way to choose the proportions.

  python3 tools/os88logo.py -o build/OS8088.GIF --png build/logo
"""

import argparse
import math
import struct
import sys
import zlib

# What the smallest supported screen can show, from Paint's own arithmetic
# (pt_geom): the canvas is the screen less the chrome, and CGA's 640x200
# desktop band is the floor.  640 - PT_CHROME_W(46) = 594, and the dock row
# 176 - PT_WIN_Y(24) - PT_CHROME_H(42) = 110.  Both are confirmed on a
# cycle-accurate CGA 5150: a fresh Paint there reports a 448x110 canvas.
LOGO_MAXW = 594
LOGO_MAXH = 110

# The height is drawn to LOGO_MAXH exactly - 110 is the tallest a picture
# can be and still open uncropped on every adapter, and there is no reason
# to be shorter.  If that bound ever moves, the check in main() makes it a
# failed BUILD rather than a quietly cropped logo.
#
# This logo is what found SPEC.md 42.9's bug: Paint used to floor a resize
# at PT_SZ_END = 128 - "tall enough to still show the size boxes" - so a
# 110-row picture opened into a 128-row canvas with 18 rows of white under
# it that were not in the file.  A control's convenience was setting the
# minimum size of a PICTURE.  The floor is the kernel's WMIN_H now, so any
# size opens at its own size and nothing here has to be chosen around it.

LOGO_MAXBYTES = 4096

INK = 1                                  # black
PAPER = 0                                # white

# Pixel aspect (width of one pixel in units of its height) on each adapter,
# assuming the 4:3 tube every one of these cards was built for.
ADAPTERS = (("vga", 640, 480), ("herc", 720, 348), ("cga", 640, 200))


# -----------------------------------------------------------------------------
# a 1-bit canvas
# -----------------------------------------------------------------------------
class Bitmap:
    def __init__(self, w, h, fill=PAPER):
        self.w, self.h = w, h
        self.px = [[fill] * w for _ in range(h)]

    def put(self, x, y, v):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = v

    def rect(self, x0, y0, x1, y1, v):
        """A filled rect, inclusive at both ends - the kernel's convention."""
        for y in range(max(0, y0), min(self.h - 1, y1) + 1):
            row = self.px[y]
            for x in range(max(0, x0), min(self.w - 1, x1) + 1):
                row[x] = v

    def dither(self, x0, y0, x1, y1, phase=0):
        """SPEC.md 39.4's 50% class: the desktop's own ground."""
        for y in range(max(0, y0), min(self.h - 1, y1) + 1):
            row = self.px[y]
            for x in range(max(0, x0), min(self.w - 1, x1) + 1):
                row[x] = INK if ((x + y + phase) & 1) == 0 else PAPER

    def rrect(self, x0, y0, x1, y1, r, v):
        """A filled rounded rect.  The corner is a quarter disc, tested at
        the pixel's centre so the two ends of a run are symmetric."""
        for y in range(max(0, y0), min(self.h - 1, y1) + 1):
            for x in range(max(0, x0), min(self.w - 1, x1) + 1):
                cx = cy = None
                if x < x0 + r:
                    cx = x0 + r
                elif x > x1 - r:
                    cx = x1 - r
                if y < y0 + r:
                    cy = y0 + r
                elif y > y1 - r:
                    cy = y1 - r
                if cx is not None and cy is not None:
                    dx, dy = x - cx, y - cy
                    if dx * dx + dy * dy > r * r:
                        continue
                self.px[y][x] = v

    def disc(self, cx, cy, t, v):
        """The pen: a disc of DIAMETER t centred on a real coordinate, with
        the pixel's own centre at +0.5.  A pen rather than an outline is what
        makes every stroke in the wordmark the same weight by construction."""
        r = t / 2.0
        for y in range(int(cy - r - 1), int(cy + r + 2)):
            for x in range(int(cx - r - 1), int(cx + r + 2)):
                dx, dy = x + 0.5 - cx, y + 0.5 - cy
                if dx * dx + dy * dy <= r * r:
                    self.put(x, y, v)

    def arc(self, cx, cy, rx, ry, t, a0, a1, v=INK):
        """Drag the pen round an elliptical arc, a0..a1 in degrees, y down.
        Returns the two endpoints, because the S is two arcs and a stroke
        between them and nothing else should be deriving those points.

        Sampled by arc length rather than by angle: at 8:1 the two ends of a
        flat ellipse move at very different rates, and a fixed angular step
        leaves the fast part beaded."""
        steps = max(8, int((abs(a1 - a0) / 360.0) * 7.0 * (rx + ry)))
        pts = []
        for i in range(steps + 1):
            a = math.radians(a0 + (a1 - a0) * i / float(steps))
            pts.append((cx + rx * math.cos(a), cy + ry * math.sin(a)))
        for x, y in pts:
            self.disc(x, y, t, v)
        return pts[0], pts[-1]

    def stroke(self, p0, p1, t, v=INK):
        """The pen along a straight segment - the S's waist, and nothing
        else in the wordmark."""
        n = max(2, int(2 * max(abs(p1[0] - p0[0]), abs(p1[1] - p0[1]))) + 1)
        for i in range(n + 1):
            f = i / float(n)
            self.disc(p0[0] + (p1[0] - p0[0]) * f,
                      p0[1] + (p1[1] - p0[1]) * f, t, v)

    def blit_mask(self, src, x, y, v):
        """Stamp src's INK pixels into self as v.  The glyphs are drawn on
        their own paper and stamped, so a knocked-out wordmark and a printed
        one are the same code with a different ink."""
        for sy in range(src.h):
            for sx in range(src.w):
                if src.px[sy][sx] == INK:
                    self.put(x + sx, y + sy, v)


# -----------------------------------------------------------------------------
# the letterforms
#
# Four glyphs carry the wordmark - o, s, 0 and 8 - and all four are the same
# pen dragged round a different path, which is the whole reason they are
# generated: at this size the eye reads UNEVEN WEIGHT long before it reads a
# wrong curve, and four hand-drawn glyphs cannot be kept in weight with each
# other through a change of size.  fonts/*.f8's 8x8 faces are hand-drawn for
# the opposite reason - at 8 rows there is no curve to get right (SPEC.md
# 6.2), only which of six pixels to light.
#
# The path is a CENTRELINE, so every box is inset by half the stroke.
# -----------------------------------------------------------------------------
def glyph_o(w, h, t):
    """'o' and '0' are one ellipse.  A digit zero this round is a deliberate
    choice and not an oversight: a slashed or narrowed zero exists to be told
    apart from a capital O, and there is no O in 'os8088'."""
    g = Bitmap(w, h)
    r = t / 2.0
    g.arc(w / 2.0, h / 2.0, (w - t) / 2.0, (h - 1 - t) / 2.0, t, 0, 360)
    return g


def glyph_eight(w, h, t):
    """Two bowls tangent at the waist, the upper one smaller and narrower.
    An 8 with equal bowls reads as top-heavy, which is why type has been
    drawn this way since Gutenberg; on a 1bpp screen it is not subtle."""
    g = Bitmap(w, h)
    r = t / 2.0
    lo, hi = r, h - 1 - r
    waist = lo + (hi - lo) * 0.455
    rx = (w - t) / 2.0
    g.arc(w / 2.0, (lo + waist) / 2.0, rx * 0.86, (waist - lo) / 2.0, t, 0, 360)
    g.arc(w / 2.0, (waist + hi) / 2.0, rx, (hi - waist) / 2.0, t, 0, 360)
    return g


def glyph_s(w, h, t):
    """Two arcs and the diagonal between them.  The top bowl is open at the
    lower left and the bottom bowl at the upper right, so the waist runs
    down-and-right between those two terminals - which is the one thing an S
    built out of two closed bowls can never have, and why the first version
    of this drew a lozenge."""
    g = Bitmap(w, h)
    r = t / 2.0
    lo, hi = r, h - 1 - r
    waist = lo + (hi - lo) * 0.5
    rx = (w - t) / 2.0
    _, top_end = g.arc(w / 2.0, (lo + waist) / 2.0, rx, (waist - lo) / 2.0,
                       t, -22, -232)
    bot_start, _ = g.arc(w / 2.0, (waist + hi) / 2.0, rx, (hi - waist) / 2.0,
                         t, -52, 158)
    g.stroke(top_end, bot_start, t)
    return g


def wordmark(cap, xh, stroke, gap):
    """'os8088' on one baseline: the digits at cap height and the two
    letters at x-height, which is what makes it a word rather than a part
    number.  A digit is narrower than it is tall - the counters go to slits
    otherwise, and a slit is the first thing a 1bpp screen loses."""
    dw = cap * 4 // 5
    ow = xh * 8 // 7
    sw = xh * 6 // 7                        # an s is the narrowest of the four
    glyphs = [
        (glyph_o(ow, xh, stroke), cap - xh),
        (glyph_s(sw, xh, stroke), cap - xh),
        (glyph_eight(dw, cap, stroke), 0),
        (glyph_o(dw, cap, stroke), 0),
        (glyph_eight(dw, cap, stroke), 0),
        (glyph_eight(dw, cap, stroke), 0),
    ]
    total = sum(g.w for g, _ in glyphs) + gap * (len(glyphs) - 1)
    out = Bitmap(total, cap)
    x = 0
    for g, dy in glyphs:
        out.blit_mask(g, x, dy, INK)
        x += g.w + gap
    return out


# -----------------------------------------------------------------------------
# the composition
# -----------------------------------------------------------------------------
# The type, which everything else is sized from: the wordmark is the logo
# and the package is a frame around it, so picking the package first is how
# you end up with a chip with a small word floating in it.
CAP, XHEIGHT, STROKE, GAP = 44, 35, 7, 10

# ...and the frame, as a budget.  Read down a column of the picture: dither,
# the keyline, a leg, the package, a leg, the keyline, dither.
MARGIN_X, MARGIN_Y = 22, 15       # how much desktop shows around the chip
KEYLINE = 3                       # white gap between the chip and the dither
LEG_H, LEG_W, PINS = 8, 9, 20     # an 8088 is a 40-pin DIP: 20 legs a side
PAD_X, PAD_Y = 78, 7              # package edge to wordmark
NOTCH_R = 11                      # the orientation notch in the left edge


def draw_logo():
    wm = wordmark(CAP, XHEIGHT, STROKE, GAP)

    # The package, then the picture around it.  Deriving the canvas from the
    # type is what keeps the two in proportion through a change of CAP - and
    # what makes the SCREEN bound a check on the result rather than a
    # constraint the layout has to be hand-fitted to.
    body_w = wm.w + 2 * PAD_X
    body_h = CAP + 2 * PAD_Y
    w = body_w + 2 * (KEYLINE + MARGIN_X)
    h = body_h + 2 * (LEG_H + KEYLINE + MARGIN_Y)

    b = Bitmap(w, h)
    b.dither(0, 0, w - 1, h - 1)

    bx0, by0 = MARGIN_X + KEYLINE, MARGIN_Y + KEYLINE + LEG_H
    bx1, by1 = bx0 + body_w - 1, by0 + body_h - 1

    # The keyline.  A black package straight onto the dither has no edge of
    # its own: at CGA's 2.4:1 the checkerboard is a mid grey and the two read
    # as one texture, which is SPEC.md 39.4's reduction seen from the other
    # side.  It covers the legs too, so a leg is as separated as the body.
    b.rect(bx0 - KEYLINE, by0 - LEG_H - KEYLINE, bx1 + KEYLINE,
           by1 + LEG_H + KEYLINE, PAPER)

    for i in range(PINS):          # legs on the LONG edges, as a DIP has them
        lx = bx0 + (body_w * (2 * i + 1)) // (2 * PINS) - LEG_W // 2
        b.rect(lx, by0 - LEG_H, lx + LEG_W - 1, by0 - 1, INK)
        b.rect(lx, by1 + 1, lx + LEG_W - 1, by1 + LEG_H, INK)

    b.rrect(bx0, by0, bx1, by1, 4, INK)

    # The orientation notch, in a short edge, where every DIP carries it.  It
    # is cut back to PAPER rather than to the dither: a half-disc of
    # checkerboard this small is noise and not a notch.
    ncy = (by0 + by1) // 2
    for y in range(ncy - NOTCH_R, ncy + NOTCH_R + 1):
        for x in range(bx0, bx0 + NOTCH_R + 1):
            dx, dy = x - bx0, y - ncy
            if dx * dx + dy * dy <= NOTCH_R * NOTCH_R:
                b.put(x, y, PAPER)

    # The wordmark, knocked out of the package.  Nudged right of centre by
    # what the notch takes, so the two ends of the word have the same amount
    # of package beside them - which is the balance the eye actually reads.
    b.blit_mask(wm, bx0 + PAD_X + NOTCH_R // 2,
                by0 + (body_h - CAP) // 2, PAPER)
    return b


# -----------------------------------------------------------------------------
# GIF87a, two colours
#
# The LZW here is giflib's ordering, deliberately: the code is emitted at the
# CURRENT width and the width grows afterwards, which is what puts the
# encoder and Paint's pt_gdec one entry apart in the same direction.  Getting
# that backwards produces a file most decoders still read and this one does
# not, which is the worst way to be wrong.
# -----------------------------------------------------------------------------
def lzw_encode(pixels, min_code_size=2):
    clear, eoi = 1 << min_code_size, (1 << min_code_size) + 1
    out, bits, nbits = bytearray(), 0, 0

    def emit(code, width):
        nonlocal bits, nbits
        bits |= code << nbits
        nbits += width
        while nbits >= 8:
            out.append(bits & 0xFF)
            bits >>= 8
            nbits -= 8

    def reset():
        return {(-1, c): c for c in range(clear)}, eoi + 1, min_code_size + 1

    table, nxt, width = reset()
    emit(clear, width)

    prefix = -1
    for c in pixels:
        key = (prefix, c)
        if key in table:
            prefix = table[key]
            continue
        if prefix >= 0:
            emit(prefix, width)
            if nxt >= (1 << width) and width < 12:
                width += 1
            if nxt < 4096:
                table[key] = nxt
                nxt += 1
            else:
                emit(clear, width)
                table, nxt, width = reset()
        prefix = c
    if prefix >= 0:
        emit(prefix, width)
        if nxt >= (1 << width) and width < 12:
            width += 1
    emit(eoi, width)
    if nbits:
        out.append(bits & 0xFF)

    blocks = bytearray()
    for i in range(0, len(out), 255):
        chunk = out[i:i + 255]
        blocks.append(len(chunk))
        blocks += chunk
    blocks.append(0)
    return bytes(blocks)


def gif87a(bm):
    """Two entries, white then black, so index 0 is paper.  Paint maps each
    entry through pt_map16 to the nearest of the 16 palette colours, and
    pure black and pure white are the two that cannot go anywhere else -
    which is what makes this survive SPEC.md 39.4's reduction unchanged."""
    hdr = b"GIF87a" + struct.pack("<HHBBB", bm.w, bm.h, 0x80, 0, 0)
    hdr += bytes((255, 255, 255, 0, 0, 0))
    hdr += b"," + struct.pack("<HHHHB", 0, 0, bm.w, bm.h, 0)
    hdr += bytes((2,))
    px = [v for row in bm.px for v in row]
    return hdr + lzw_encode(px, 2) + b";"


# -----------------------------------------------------------------------------
# previews - no Pillow in a fresh container, so the PNG is written by hand
# -----------------------------------------------------------------------------
def write_png(path, bm, sx, sy):
    w, h = int(bm.w * sx), int(bm.h * sy)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        row = bm.px[min(bm.h - 1, int(y / sy))]
        raw += bytes(0 if row[min(bm.w - 1, int(x / sx))] == INK else 255
                     for x in range(w))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)
    return w, h


def main():
    ap = argparse.ArgumentParser(description="the os8088 logo, as a mono GIF")
    ap.add_argument("-o", "--output", metavar="OUT.GIF",
                    help="where to write the GIF")
    ap.add_argument("--png", metavar="PREFIX",
                    help="also write <PREFIX>-<adapter>.png previews, each "
                         "at that adapter's pixel aspect")
    args = ap.parse_args()

    bm = draw_logo()
    if bm.w > LOGO_MAXW or bm.h > LOGO_MAXH:
        sys.exit("os88logo: %dx%d is past the %dx%d a CGA desktop can show; "
                 "Paint would crop it" % (bm.w, bm.h, LOGO_MAXW, LOGO_MAXH))

    gif = gif87a(bm)
    if len(gif) > LOGO_MAXBYTES:
        sys.exit("os88logo: %d bytes, over the %d-byte budget"
                 % (len(gif), LOGO_MAXBYTES))

    if args.output:
        open(args.output, "wb").write(gif)
    if args.png:
        for name, sw, sh in ADAPTERS:
            aspect = (4.0 / 3.0) / (float(sw) / float(sh))   # pixel w over h
            pw, ph = write_png("%s-%s.png" % (args.png, name), bm,
                               3.0, 3.0 / aspect)
            print("%s-%s.png  %dx%d  (pixel aspect %.2f)"
                  % (args.png, name, pw, ph, aspect))

    ink = sum(row.count(INK) for row in bm.px)
    print("os88logo: %s %dx%d, %d bytes of %d, %.0f%% ink"
          % (args.output or "(not written)", bm.w, bm.h, len(gif),
             LOGO_MAXBYTES, 100.0 * ink / (bm.w * bm.h)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

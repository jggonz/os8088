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

It also cuts the same logo SQUARE, for the places that want one and crop it
to a circle - a chat server's icon, a favicon, an app tile (SPEC.md 63.6).
Those are re-cuts of this logo and of the loading screen, not a second one,
and nothing ships or commits them either.

  python3 tools/os88logo.py -o build/OS8088.GIF --png build/logo
  python3 tools/os88logo.py --icons build/marks --sheet build/marks.png
"""

import argparse
import math
import os
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

    def dither(self, x0, y0, x1, y1, phase=0, cell=1):
        """SPEC.md 39.4's 50% class: the desktop's own ground.

        `cell` is 1 for anything that will be shown at the size it was drawn,
        which is the screen and therefore the logo.  A mark that a client
        resamples to whatever size it likes wants 2: a 1px checkerboard put
        through an arbitrary scale factor beats against it and moires, and
        the ground stops reading as a texture and starts reading as noise."""
        for y in range(max(0, y0), min(self.h - 1, y1) + 1):
            row = self.px[y]
            for x in range(max(0, x0), min(self.w - 1, x1) + 1):
                row[x] = INK if (((x // cell) + (y // cell) + phase) & 1) == 0 \
                    else PAPER

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
# the square marks (SPEC.md 63.6)
#
# A chat server, a favicon and an app tile all want the logo in a SQUARE, and
# most of them crop it to a circle on top of that.  The wide logo cannot be
# used there: inscribe a 4.2:1 word in a circle and the word is a quarter of
# the diameter, which is unreadable at the 40px a sidebar draws it at.
#
# So these are the same logo re-cut, not a second one: the same package,
# legs, notch, keyline and dither, the same four letterforms from the same
# pen, on two baselines instead of one.  The two alternates are the loading
# screen's own artwork (kernel/splash.inc) for the same reason - a mark that
# is already on the machine's screen beats one invented for a chat client.
#
# They are drawn on a 128px grid and scaled by WHOLE numbers, so a 512px icon
# is this picture with 4x4 pixels.  That is the same choice --png makes for
# the previews and it is deliberate: the chunky pixel is what says the mark
# came off a 1984 screen, and an icon resampled smoothly says the opposite.
# Everything stays inside ICON_SAFE of the centre, which is the circle a
# client crops to less a margin, because a mark that touches the crop reads
# as a mark that was cropped by accident.
# -----------------------------------------------------------------------------
ICON_G = 128                      # the design grid; every icon is a multiple
ICON_SAFE = 56                    # ...and nothing is drawn past this radius

ICAP, IXH, ISTROKE, IGAP = 18, 14, 3, 4     # the type, at 0.4 of the logo's
ILEAD = 4                                   # ...and what separates the lines
IPAD_X, IPAD_Y = 5, 7             # package edge to wordmark
ILEG_H, ILEG_W, IPINS = 6, 3, 20  # still a 40-pin DIP: 20 legs a side
IKEYLINE = 2
INOTCH_R = 7


def wordmark_stacked(cap, xh, stroke, gap, lead):
    """wordmark()'s six glyphs on TWO baselines - 'os' centred over '8088'.

    The digits keep the cap height and the letters the x-height, so the two
    lines are the one word they are in the wide logo rather than a stack of
    two.  Centred on each other and not left-aligned: the word is inside a
    package that is symmetric about both axes, and the eye reads the block
    against the package before it reads the letters against each other."""
    dw = cap * 4 // 5
    ow, sw = xh * 8 // 7, xh * 6 // 7
    top = [glyph_o(ow, xh, stroke), glyph_s(sw, xh, stroke)]
    bot = [glyph_eight(dw, cap, stroke), glyph_o(dw, cap, stroke),
           glyph_eight(dw, cap, stroke), glyph_eight(dw, cap, stroke)]
    tw = sum(g.w for g in top) + gap * (len(top) - 1)
    bw = sum(g.w for g in bot) + gap * (len(bot) - 1)
    out = Bitmap(max(tw, bw), xh + lead + cap)
    for row, roww, y in ((top, tw, 0), (bot, bw, xh + lead)):
        x = (out.w - roww) // 2
        for g in row:
            out.blit_mask(g, x, y, INK)
            x += g.w + gap
    return out


def icon_chip():
    """The logo, square: the package with the wordmark knocked out of it."""
    wm = wordmark_stacked(ICAP, IXH, ISTROKE, IGAP, ILEAD)
    body_w = wm.w + 2 * IPAD_X + INOTCH_R       # the notch gets a pad of its
    body_h = wm.h + 2 * IPAD_Y                  # own, so the word keeps IPAD_X
    b = Bitmap(ICON_G, ICON_G)                  # of package on both sides
    b.dither(0, 0, ICON_G - 1, ICON_G - 1, cell=2)

    chip_w = body_w + 2 * IKEYLINE               # ...and the whole chip, which
    chip_h = body_h + 2 * (ILEG_H + IKEYLINE)   # is what the crop sees
    corner = math.hypot(chip_w / 2.0, chip_h / 2.0)
    if corner > ICON_SAFE:
        sys.exit("os88logo: the chip's corner is %.1f from the centre, past "
                 "ICON_SAFE %d. A client crops this to a circle of %d, and a "
                 "mark that reaches the crop reads as one cropped by accident "
                 "- take it out of the type, which everything else is sized "
                 "from." % (corner, ICON_SAFE, ICON_G // 2))

    bx0, by0 = (ICON_G - body_w) // 2, (ICON_G - body_h) // 2
    bx1, by1 = bx0 + body_w - 1, by0 + body_h - 1
    b.rect(bx0 - IKEYLINE, by0 - ILEG_H - IKEYLINE,     # the keyline, over the
           bx1 + IKEYLINE, by1 + ILEG_H + IKEYLINE, PAPER)   # legs as well
    for i in range(IPINS):
        lx = bx0 + (body_w * (2 * i + 1)) // (2 * IPINS) - ILEG_W // 2
        b.rect(lx, by0 - ILEG_H, lx + ILEG_W - 1, by0 - 1, INK)
        b.rect(lx, by1 + 1, lx + ILEG_W - 1, by1 + ILEG_H, INK)
    b.rrect(bx0, by0, bx1, by1, 3, INK)

    ncy = (by0 + by1) // 2                              # the orientation notch
    for y in range(ncy - INOTCH_R, ncy + INOTCH_R + 1):
        for x in range(bx0, bx0 + INOTCH_R + 1):
            dx, dy = x - bx0, y - ncy
            if dx * dx + dy * dy <= INOTCH_R * INOTCH_R:
                b.put(x, y, PAPER)

    b.blit_mask(wm, bx0 + IPAD_X + INOTCH_R, by0 + IPAD_Y, PAPER)
    return b


def spl_digit(b, x, y, w, h, t, middle, v=INK):
    """One loading-screen glyph, which is kernel/splash.inc's spl_rowmk: two
    verticals, a top and a bottom bar, and - for an 8 rather than a 0 - the
    middle one at row 20.  Not a typeface: the splash draws these itself,
    before any font is resident to draw them with."""
    b.rect(x, y, x + t - 1, y + h - 1, v)
    b.rect(x + w - t, y, x + w - 1, y + h - 1, v)
    b.rect(x, y, x + w - 1, y + t - 1, v)
    b.rect(x, y + h - t, x + w - 1, y + h - 1, v)
    if middle:
        b.rect(x, y + (h - t) // 2, x + w - 1, y + (h - t) // 2 + t - 1, v)


def icon_boot():
    """The loading screen: the spinner's "8088" over its progress bar, white
    on black, which is what mode 12h and spl_span between them leave."""
    gw, gh, t, gap = 19, 36, 4, 5
    b = Bitmap(ICON_G, ICON_G, INK)
    total = 4 * gw + 3 * gap                    # corners at 55.6 of ICON_SAFE
    x0 = (ICON_G - total) // 2
    for i, d in enumerate("8088"):
        spl_digit(b, x0 + i * (gw + gap), 36, gw, gh, t, d == "8", PAPER)
    by, bh = 84, 11                             # the trough, and the bar in it
    b.rect(x0, by, x0 + total - 1, by + bh - 1, PAPER)
    b.rect(x0 + 2, by + 2, x0 + total - 3, by + bh - 3, INK)
    b.rect(x0 + 4, by + 4, x0 + 4 + int(total * 0.58), by + bh - 5, PAPER)
    return b


def icon_monogram():
    """The same four glyphs stacked two by two - the mark for the sizes where
    a word of any kind is gone.  It is a re-arrangement of the splash's own
    drawing rather than a new one, and it is the only mark here that still
    resolves at 40px: four rings, and the one without a waist is the 0."""
    gw, gh, t, gap = 32, 36, 5, 8
    b = Bitmap(ICON_G, ICON_G)
    x0 = (ICON_G - (2 * gw + gap)) // 2
    y0 = (ICON_G - (2 * gh + gap)) // 2
    for i, d in enumerate("8088"):
        spl_digit(b, x0 + (i % 2) * (gw + gap), y0 + (i // 2) * (gh + gap),
                  gw, gh, t, d == "8")
    return b


ICONS = {
    "chip": (icon_chip, "the logo itself, re-cut square"),
    "boot": (icon_boot, "the loading screen's wordmark and bar"),
    "monogram": (icon_monogram, "the splash's glyphs, stacked 2x2"),
}


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
def _png(path, w, h, rows):
    """Eight-bit grey scanlines out, one byte a pixel and no filtering.
    There is no Pillow in a fresh container, and every picture here is one
    of two colours or a grey between them, so this is all of the format
    that is needed - a colour type nothing writes is one nothing tests."""
    raw = bytearray()
    for r in rows:
        raw.append(0)
        raw += r

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)
    return w, h


def write_png(path, bm, sx, sy):
    w, h = int(bm.w * sx), int(bm.h * sy)
    rows = []
    for y in range(h):
        row = bm.px[min(bm.h - 1, int(y / sy))]
        rows.append(bytes(0 if row[min(bm.w - 1, int(x / sx))] == INK else 255
                          for x in range(w)))
    return _png(path, w, h, rows)


def icon_scaled(bm, scale, inverted=False):
    """The mark at `scale` whole pixels per pixel, as grey scanlines."""
    ink, paper = (255, 0) if inverted else (0, 255)
    rows = []
    for row in bm.px:
        line = bytearray()
        for v in row:
            line += bytes([ink if v == INK else paper]) * scale
        rows.extend([bytes(line)] * scale)
    return rows


def icon_crop(rows, n, size, bg):
    """Box-average an n x n grey image down to size, over `bg`, with an
    antialiased round crop: what a chat client does to a server icon, done
    here so the answer can be LOOKED at before the mark is chosen."""
    out = []
    r = size / 2.0
    for oy in range(size):
        y0 = oy * n // size
        y1 = max(y0 + 1, (oy + 1) * n // size)
        line = bytearray()
        for ox in range(size):
            x0 = ox * n // size
            x1 = max(x0 + 1, (ox + 1) * n // size)
            acc = cnt = 0
            for sy in range(y0, y1):
                row = rows[sy]
                for sx in range(x0, x1):
                    acc += row[sx]
                    cnt += 1
            hit = 0
            for j in range(4):                  # 4x4 coverage, so the circle
                for i in range(4):              # has an edge and not a stair
                    dx = ox + (i + 0.5) / 4.0 - r
                    dy = oy + (j + 0.5) / 4.0 - r
                    if dx * dx + dy * dy <= r * r:
                        hit += 1
            a = hit / 16.0
            line.append(int(round((float(acc) / cnt) * a + bg * (1.0 - a))))
        out.append(bytes(line))
    return out


def icon_sheet(path, names, inverted=False):
    """Every mark, round-cropped, at the three sizes a chat client shows one
    at, on both of its themes.  This is the instrument for CHOOSING: the
    marks differ at 128px and it is at 40 that most of them stop working."""
    from os88font import parse          # the tree's 8x8 face, for labels only
    font = parse(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              os.pardir, "fonts", "tallx.f8"))
    SIZES = (128, 72, 40)
    GROUND, DARK, LIGHT = 30, 51, 255
    PAD, GAP, LBL = 16, 18, 8 * 9

    panel_w = PAD + sum(SIZES) + GAP * (len(SIZES) - 1) + PAD
    rowh = SIZES[0] + 22
    panel_h = 22 + len(names) * rowh + PAD // 2
    w = PAD + LBL + 2 * panel_w + GAP + PAD
    h = 34 + panel_h + PAD
    canvas = [bytearray([GROUND]) * w for _ in range(h)]

    def fill(x, y, bw, bh, g):
        for j in range(y, min(h, y + bh)):
            canvas[j][x:x + bw] = bytes([g]) * bw

    def caption(text, ox, oy, g):
        for n, ch in enumerate(text):
            gl = font.get(ord(ch))
            if gl is None:
                continue
            for r in range(8):
                for k in range(8):
                    if gl[r] & (0x80 >> k):
                        canvas[oy + r][ox + n * 8 + k] = g

    caption("os8088 marks - as a chat client crops them", PAD, 12, 225)
    px0 = PAD + LBL
    fill(px0, 34, panel_w, panel_h, DARK)
    fill(px0 + panel_w + GAP, 34, panel_w, panel_h, LIGHT)
    caption("dark theme", px0 + PAD, 40, 150)
    caption("light theme", px0 + panel_w + GAP + PAD, 40, 120)

    y = 34 + 22
    for name in names:
        rows = icon_scaled(ICONS[name][0](), 4, inverted)
        caption(name, PAD, y + SIZES[0] // 2 - 4, 225)
        for panel, bg in ((px0, DARK), (px0 + panel_w + GAP, LIGHT)):
            x = panel + PAD
            for size in SIZES:
                paste = icon_crop(rows, ICON_G * 4, size, bg)
                for j in range(size):
                    oy = y + (SIZES[0] - size) // 2 + j
                    canvas[oy][x:x + size] = paste[j]
                x += size + GAP
        y += rowh
    _png(path, w, h, [bytes(r) for r in canvas])
    return w, h


def main():
    ap = argparse.ArgumentParser(description="the os8088 logo, as a mono GIF")
    ap.add_argument("-o", "--output", metavar="OUT.GIF",
                    help="where to write the GIF")
    ap.add_argument("--png", metavar="PREFIX",
                    help="also write <PREFIX>-<adapter>.png previews, each "
                         "at that adapter's pixel aspect")
    ap.add_argument("--icons", metavar="DIR",
                    help="also write the square marks (SPEC.md 63.6) into "
                         "DIR, each in both polarities")
    ap.add_argument("--icon-sizes", metavar="N[,N...]", default="512",
                    help="what to write them at, whole multiples of the "
                         "%d-px grid (default 512)" % ICON_G)
    ap.add_argument("--mark", action="append", choices=sorted(ICONS),
                    help="restrict --icons and --sheet to this mark, "
                         "repeatable (default: all of them)")
    ap.add_argument("--sheet", metavar="OUT.PNG",
                    help="write the pick-one contact sheet: every mark, "
                         "round-cropped, at the sizes a client shows one at")
    ap.add_argument("--inverted", action="store_true",
                    help="draw the contact sheet in the inverted polarity")
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

    marks = args.mark or sorted(ICONS)
    if args.icons:
        sizes = [int(n) for n in args.icon_sizes.split(",")]
        for size in sizes:
            if size % ICON_G:
                sys.exit("os88logo: %d is not a whole multiple of the %d-px "
                         "grid - a mark here is scaled by whole pixels, which "
                         "is what keeps it a picture off a screen"
                         % (size, ICON_G))
        if not os.path.isdir(args.icons):
            os.makedirs(args.icons)
        for name in marks:
            mark = ICONS[name][0]()
            for size in sizes:
                for tag, inv in (("", False), ("-inverted", True)):
                    out = os.path.join(args.icons, "os8088-%s-%d%s.png"
                                       % (name, size, tag))
                    _png(out, size, size, icon_scaled(mark, size // ICON_G, inv))
                    print("os88logo: %s  %dx%d  %s"
                          % (out, size, size, ICONS[name][1]))
    if args.sheet:
        w, h = icon_sheet(args.sheet, marks, args.inverted)
        print("os88logo: %s  %dx%d  contact sheet" % (args.sheet, w, h))
    return 0


if __name__ == "__main__":
    sys.exit(main())

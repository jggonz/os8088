#!/usr/bin/env python3
"""glyphread - read TEXT off the 1bpp glass against the kernel's own glyphs.

NOT A ROW: a library, imported by the rows whose subject is what a control
SAYS - tests/inststate.py, bigvol.py, hdmap.py and hdsize.py.

The kernel's 8x8 glyph table comes out of the guest ([font_seg]:[font_base],
the ROM's own table font_init found - SPEC.md 6), the expected string is
rendered from it on the host, and the result is SEARCHED for inside a rect of
the CGA framebuffer. A pixel count or a "did these bytes change" diff passes
on a control that went blank, greyed, or picked up the wrong string; this
cannot.

It was tests/instrest.py's apparatus, written for the hard-disk Install
window's action button (SPEC.md 52.10.6.1). That window is gone (SPEC.md
52.10.4.4) and the row with it; the apparatus is what four other rows use.
"""
FONT_FIRST = 32                     # font.inc: glyphs 32..126, 8 bytes each
FONT_N = 95


def glyph_table(m):
    """The table font_init FOUND - [font_seg]:[font_base], the ROM's own
    (SPEC.md 6) - read where the renderers read it."""
    seg = int.from_bytes(m.read(m.sym("font_seg"), 2), "little")
    off = int.from_bytes(m.read(m.sym("font_base"), 2), "little")
    return m.readseg(seg, off, FONT_N * 8)   # (a selector on kern_mega)


def render(text, tab):
    """`text` as rows of 0/1, 8 rows by 8*len - 1 = INK.

    The kernel's font is a fixed 8x8 cell and `font_width_x` is 8 a character,
    so there is no advance to model: the string is its glyphs side by side.
    """
    out = []
    for r in range(8):
        row = []
        for ch in text:
            b = tab[(ord(ch) - FONT_FIRST) * 8 + r]
            row.extend((b >> bit) & 1 for bit in range(7, -1, -1))
        out.append(row)
    return out


def screen(m):
    """The CGA framebuffer as rows of 0/1, one entry a pixel.  1 = LIT."""
    w, h, rows = m.vram("cga")
    return w, h, rows


def find(rows, x0, y0, w, h, want):
    """Is `want` (rows of INK bits) drawn anywhere inside this rect?

    SEARCHED rather than checked at a computed pen, because os88ui_btn centres
    a label and this test must not carry a second copy of that arithmetic -
    which is the thing that would silently go stale if the button ever moved.

    A lit framebuffer pixel is WHITE.  The button's interior is filled CWHITE
    and the caption is drawn in CBLACK over it, so an ink pixel reads 0 and
    the ground reads 1: the match is against `1 - ink`.
    """
    gh, gw = len(want), len(want[0])
    for y in range(y0, y0 + h - gh + 1):
        for x in range(x0, x0 + w - gw + 1):
            for r in range(gh):
                row, wr = rows[y + r], want[r]
                if any(row[x + c] != 1 - wr[c] for c in range(gw)):
                    break
            else:
                return x, y
    return None


def ink(rows, x0, y0, w, h):
    """Dark pixels in a rect - what a drawn control has and bare window has
    none of."""
    return sum(1 for y in range(y0, y0 + h)
               for x in range(x0, x0 + w) if not rows[y][x])


# Bare desktop, clear of the window under test (the installer's was x 63..363),
# of the drive-zone column (x 526.. on CGA - SPEC.md 26.1) and of the menu bar.
PARK = (500, 30)


def park(mo):
    """Take the POINTER off the thing about to be read.

    The arrow is 8x12 and the kernel draws it into the framebuffer (SPEC.md
    7.1), so a cursor left where the last click landed is ink ON the control -
    and the last click often lands in the middle of the very control whose
    caption is read next. It cost tests/instrest.py a run: the caption really
    did say `Restart` and the match failed on four rows the arrow was sitting
    in.
    """
    mo.to(*PARK)

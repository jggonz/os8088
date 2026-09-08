#!/usr/bin/env python3
"""DOES THE COMPOSED PALETTE DRAW WHAT THE PER-BUTTON PATH DREW? (SPEC.md 42.26.1)

    make && python3 tests/paintpal.py [--machine os8088_5150_herc_gla]

SPEC.md 42.26.1 puts a whole ROW of two tool buttons - wells, frames and
glyphs - into one 42-wide one-bit band and blits it once, where SPEC.md 42.26
drew each button with a fill, a frame and a sprite pass.  24 drawing calls
become 4 and `pt_draw_pal` reads 111.0 -> 33.0 ms (PERFORMANCE.md Set 116).
Every pixel of it is composed by hand here rather than by the kernel's
primitives, so the question is whether the hand agrees with them.

**THE ORACLE IS THE OTHER PATH, ON THE SAME MACHINE, IN THE SAME BOOT.**
`gfx_blit1` is `call far / ret` in `.text`, and `stc / ret` written over its
first two bytes makes the kernel answer CF = 1 - which is exactly what
kern_small answers (SPEC.md 5.4.2) and is the refusal `pt_draw_pal` already
has a second path for.  So one boot draws the palette both ways and the two
pictures are compared to the pixel.  That is better than a golden image: it
compares the new code against the code it replaced rather than against
somebody's screenshot, and it cannot go stale when a glyph is redrawn.

THE CAPTURE IS COORDINATE-FREE, deliberately.  MartyPC's rendered frame does
not sit 1:1 on the desktop's coordinates - there is a border, and the offset
cost a session's worth of confusion - so nothing here converts a window
position into a framebuffer position.  Both arms capture the SAME generous
rect around the window, so any constant offset cancels; all that is required
is that the rect CONTAIN the palette, which the control below proves.

THREE ROUNDS, and the third is the one that makes the first two mean
something:

  plain    the palette as it opens - one button in hand, seven not
  greyed   [pt_havefill] poked to 0 with the fill tool in hand, which is
           SPEC.md 42.6.2's disabled glyph: the band has to lay SPEC.md
           39.4's dither itself, in the phase the kernel would have used,
           and that is the one thing in the composition with no primitive
           behind it
  CONTROL  the same capture with a DIFFERENT tool in hand, which must
           DIFFER - because a comparison that cannot fail is not a test, and
           a rect that missed the palette would pass the first two rounds
           perfectly
"""
import argparse
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(HERE)
FAILS = []


class Paint(object):
    """Paint open on a booted machine, with its bss reachable by name."""

    def __init__(self, ui):
        self.ui = ui
        self.m = ui.m
        self.pm = dispapps._map("paint")
        self.w = ui.path("B:/APPS/PAINT.O88")
        got = dispapps.pkg_seg(self.m, 0)
        if got is None:
            sys.exit("paintpal: PAINT.O88 did not open")
        self.seg = got[1]
        self.img = dispapps.img_size("paint")

    def _addr(self, name):
        return ((self.seg << 4) + self.img
                + self.pm[name] - self.pm["os88_image_end"])

    def poke(self, name, value):
        self.m.write(self._addr(name), bytes([value]))

    def peek(self, name):
        return self.m.read(self._addr(name), 1)[0]

    def tool_of(self, glyph):
        """The tool index whose pt_ic_tab entry is `glyph`."""
        want = self.pm[glyph]
        base = (self.seg << 4) + self.pm["pt_ic_tab"]
        raw = self.m.read(base, 32)
        for i in range(16):
            if int.from_bytes(raw[i * 2:i * 2 + 2], "little") == want:
                return i
        sys.exit("paintpal: %s is in no pt_ic_tab entry" % glyph)

    def repaint(self):
        """Zoom out and back: two whole W_PAINTs, at the same geometry either
        side.  A window DRAG will not do it - on a 1bpp adapter a short move
        is served from the raise cache (SPEC.md 11.96.11) and the content is
        never repainted at all."""
        r = self.w.x, self.w.y
        self.ui.mo.dblclick(r[0] + 60, r[1] + 9)
        self.ui.settle()
        self.ui.mo.dblclick(r[0] + 60, r[1] + 9)
        self.ui.settle()
        self.ui.mo.to(4, 4)
        self.ui.settle()

    def shot(self):
        """The window's neighbourhood, generously - see the header."""
        fw, fh, fb = self.m.fbuf(card=0)
        x0, y0 = max(0, self.w.x - 24), max(0, self.w.y - 24)
        x1, y1 = min(fw, self.w.x + 140), min(fh, self.w.y + 160)
        out = bytearray()
        for y in range(y0, y1):
            o = (y * fw + x0) * 3
            out += fb[o:o + (x1 - x0) * 3]
        return bytes(out)


def both_ways(p, label):
    """Draw the palette with the band and with the path its refusal owes."""
    blit1 = os88sym.linear("gfx_blit1")
    keep = p.m.read(blit1, 2)
    p.repaint()
    band = p.shot()
    p.m.write(blit1, bytes([0xF9, 0xC3]))        # stc / ret: CF = 1, as
    p.repaint()                                  # kern_small answers
    fallback = p.shot()
    p.m.write(blit1, keep)
    same = band == fallback
    print("   %-8s band %s  per-button %s  %s"
          % (label, hashlib.sha256(band).hexdigest()[:12],
             hashlib.sha256(fallback).hexdigest()[:12],
             "IDENTICAL" if same else "*** THEY DIFFER ***"))
    if not same:
        n = sum(1 for a, b in zip(band, fallback) if a != b)
        FAILS.append("%s: the composed palette differs from the per-button "
                     "path in %d of %d bytes (SPEC.md 42.26.1)"
                     % (label, n, len(band)))
    return band


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    print()
    with os88ui.boot(a.image, apps=a.apps, machine=a.machine,
                     verbose=False) as ui:
        p = Paint(ui)
        if not p.peek("pt_mono"):
            sys.exit("paintpal: this machine is not 1bpp, so SPEC.md "
                     "42.26.1's band is not the path under test")
        tool = p.peek("pt_tool")
        plain = both_ways(p, "plain")

        # --- the greyed fill tool: the one thing with no primitive behind it.
        # PT_T_FILL is an `equ` a macro made, so nasm's map has no such
        # symbol - but `pt_ic_fill` is a LABEL and pt_ic_tab is the order the
        # same list produced, so the index is read off the table rather than
        # mirrored here. That also tracks PTF_CLIP, which inserts a tool ahead
        # of it (SPEC.md 42.22.1).
        fill = p.tool_of("pt_ic_fill")
        p.poke("pt_havefill", 0)
        p.poke("pt_tool", fill)
        both_ways(p, "greyed")
        p.poke("pt_havefill", 1)

        # --- ...and a capture that MUST differ, or the two above proved
        # nothing about a rect that never held the palette
        p.poke("pt_tool", tool ^ 1)                  # its neighbour, which
                                                     # every build has
        p.repaint()
        other = p.shot()
        print("   control  a different tool in hand: %s"
              % ("DIFFERS, as it must" if other != plain
                 else "*** IDENTICAL - this rect is not the palette ***"))
        if other == plain:
            FAILS.append("CONTROL: selecting another tool changed nothing in "
                         "the captured rect, so this row is not looking at "
                         "the palette and its passes mean nothing")
        p.poke("pt_tool", tool)

    for f in FAILS:
        print("paintpal: " + f)
    if FAILS:
        print("paintpal: FAIL")
        return 1
    print("paintpal: PASS - the composed row draws what the fill, the frame "
          "and the sprite pass drew, greyed glyph included")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

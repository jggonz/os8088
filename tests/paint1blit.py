#!/usr/bin/env python3
"""THE TWO PATHS A ONE-BIT CANVAS REACHES THE SCREEN BY (SPEC.md 42.23.4)

    make && make small smallapps && python3 tests/paint1blit.py

`OSAPI_GFX_BLIT1` takes exactly the band a one-bit canvas holds, so on
`kern_big` a repaint is a `rep movsw` into the framebuffer.  `kern_small`
carries the SLOT AND NOT THE BODY (SPEC.md 5.4.2), answers CF = 1, and Paint
expands each row into `pt_line` as packed 4bpp for `gfx_blit4` instead.

**BOTH PATHS ARE DRIVEN ON ONE KERNEL, AND THE ORACLE IS THE FILE.**  Which
path runs is chosen by the CANVAS WIDTH: the fast path rounds the blit up to
a multiple of 8 and gives up when that would reach past the picture, so a
208-wide fixture takes `gfx_blit1` and a 204-wide one takes the row loop.
That is better than running the two kernels against each other - it compares
each against what the file actually says rather than against the other's
opinion - and it needs no `kern_small` boot, whose B: drive the harness
cannot open.

WHICH PATH RAN IS ASKED OF THE PATH'S OWN SIDE EFFECT, not of a breakpoint:
the fallback expands each row into `pt_line` and the fast path never touches
it, so a sentinel written there survives one and not the other.  That needs no
instrumentation in the product and no `bp_exec` at all - which matters,
because a window DRAG will not provoke either path: on a 1bpp adapter Paint
banks the WHOLE content (SPEC.md 11.96.11), so a drag is served from the raise
cache and neither blit runs.  A stroke and Ctrl+Z is what repaints out of the
canvas (tests/paintundo.py's own argument).

**THE ORACLE IS THE CANVAS AND NOT THE SCREEN**, and that is a limitation
worth stating rather than hiding.  Paint's window shares the desktop with the
file browser that launched it, so a screen sample taken at `[pt_cx0]` runs
into whatever is stacked above and the comparison measures z-order.  What is
on the glass IS covered - paint1bpp, paint1load, paintwipe and paintsu all
read pixels - and what none of them can say is which of the two paths drew
them.

**THE FIXTURE IS BUILT HERE**, and what it has to be is a pure function of
what is under test: a pattern whose every byte differs from its neighbours, so
a band blitted one row out of step, one byte along, or with its stride's sign
wrong cannot come back looking right.  A flat picture would pass all three.

WHY THE STRIDE'S SIGN IS THE POINT.  The canvas is stored bottom-up because it
IS the BMP, so the stride handed to `gfx_blit1` is NEGATIVE.  SPEC.md 42.23.4
once refused the fast path on the ground that `gfx_blit1_x` skips rows with
`mul bp` and `MUL` is unsigned - wrong, because both sites discard the high
half and the low 16 bits of a multiply are the same either way.  This row is
what makes that a checked fact rather than a second piece of reasoning.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(HERE)
S = os88sym.linear
H = 96
FAILS = []


def fixture(w, name):
    """A 1bpp BMP whose every byte differs from its neighbours."""
    stride = ((w + 7) // 8 + 3) & ~3
    rows = [bytes(((x * 53 + y * 149 + 7) & 0xFF) for x in range(stride))
            for y in range(H)]
    hdr = struct.pack("<2sIHHI", b"BM", 62 + stride * H, 0, 0, 62)
    hdr += struct.pack("<IiiHHIIIIII", 40, w, H, 1, 1, 0, stride * H, 0, 0, 2, 0)
    hdr += bytes([0, 0, 0, 0, 255, 255, 255, 0])
    open("/tmp/" + name, "wb").write(hdr + b"".join(reversed(rows)))
    return "/tmp/" + name


def expect(w):
    """The picture as pixels: [y][x] = 1 white, 0 black. 1 IS WHITE (42.23.1)."""
    stride = ((w + 7) // 8 + 3) & ~3
    out = []
    for y in range(H):
        row = bytes(((x * 53 + y * 149 + 7) & 0xFF) for x in range(stride))
        out.append([(row[x >> 3] >> (7 - (x & 7))) & 1 for x in range(w)])
    return out


def arm(label, name, w, want_fast, machine):
    """Open one fixture; establish WHICH path drew it, and that it is right."""
    img = dispapps.img_size("paint")

    def off(seg, n):
        return (seg << 4) + img + dispapps.bss_off("paint", n)

    def w16(m, seg, n, k=2):
        return int.from_bytes(m.read(off(seg, n), k), "little")

    apps = os88marty.scratch_disk("/tmp/p1b_%d.img" % w,
                                  "APPS:build/paint.o88",
                                  "MEDIA:" + fixture(w, name))
    with os88marty.launch("build/os8088-360.img", apps=apps,
                          machine=machine) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, bx, by, "MEDIA")
        os88marty.settle(m)
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, os88marty.settle,
                                                bx, by,
                                                dispcp.row_of(m, S, name)))
        mo.dblclick(rx, ry)
        m.advance(frames=400)
        m.run()
        os88marty.settle(m)
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            FAILS.append("%s: %s did not open Paint" % (label, name))
            return
        seg = got[1]
        if not w16(m, seg, "pt_1bpp", 1):
            FAILS.append("%s: the canvas is not one bit" % label)
            return
        cw, ch = w16(m, seg, "pt_cw"), w16(m, seg, "pt_ch")
        cx0, cy0 = w16(m, seg, "pt_cx0"), w16(m, seg, "pt_cy0")
        stride = w16(m, seg, "pt_stride")

        # --- WHICH PATH RAN, asked of the path's own side effect rather than
        # of a breakpoint. The fallback expands each row into pt_line; the
        # fast path never touches it. A sentinel there survives one and not
        # the other, and it needs no instrumentation in the product.
        #
        # A WINDOW DRAG WILL NOT DO IT: on a 1bpp adapter Paint banks the
        # WHOLE content (SPEC.md 11.96.11), so a drag is served from the raise
        # cache and NEITHER blit runs - measured, 0 stops for both. A stroke
        # and Ctrl+Z is what repaints out of the canvas (tests/paintundo.py).
        LINE = off(seg, "pt_line")
        SENT = bytes([0xA5]) * 64
        m.write(off(seg, "pt_thick"), bytes([3]))
        m.advance(frames=4)
        m.run()
        sx, sy = cx0 + 24, cy0 + 24
        mo.to(sx, sy)
        os88marty.settle(m)
        if mo.where()[2] & 1:
            mo._edge(False)
        mo._edge(True)
        mo.to(sx + 100, sy + 30, l=True)
        mo._edge(False)
        os88marty.settle(m)
        mo.to(4, 4)
        os88marty.settle(m)
        m.write(LINE, SENT)
        m.advance(frames=2)
        m.run()
        m.ctrl("KeyZ")                          # undo: repaints out of the canvas
        m.advance(frames=300)
        m.run()
        os88marty.settle(m)
        fell_back = m.read(LINE, 64) != SENT
        # ...and the canvas is now the UNDONE state, which is the picture as
        # loaded. Comparing after a redo instead would differ from the file by
        # exactly the stroke this row drew to provoke the repaint - 544 pixels
        # of it, identically in both arms, which reads like a decoder bug and
        # is the test marking its own homework.
        mo.to(4, 4)
        os88marty.settle(m)

        # --- ...AND THAT THE PICTURE IS THE FILE. The oracle is the CANVAS
        # rather than the screen, deliberately: Paint's window shares the
        # desktop with the file browser that launched it, so a screen sample
        # taken at [pt_cx0] runs into whatever is stacked above and the
        # comparison measures z-order. What the screen shows IS covered -
        # paint1bpp, paint1load, paintwipe and paintsu all read pixels - and
        # what none of them can say is WHICH of 42.23.4's two paths drew them.
        # That is this row's job and the sentinel above is how it does it.
        want = expect(w)
        bad = 0
        for y in range(min(ch, H)):
            roff = int.from_bytes(m.read(off(seg, "pt_rowoff") + y * 2, 2),
                                  "little")
            rseg = int.from_bytes(m.read(off(seg, "pt_rowseg") + y * 2, 2),
                                  "little")
            row = m.read((rseg << 4) + roff, stride)
            for x in range(min(cw, w)):
                if ((row[x >> 3] >> (7 - (x & 7))) & 1) != want[y][x]:
                    bad += 1
        print("   %-9s %dx%d canvas, %s, %d canvas pixels differ from the file"
              % (label, cw, ch,
                 "the FALLBACK ran" if fell_back else "the FAST PATH ran", bad))
        if bad:
            FAILS.append("%s: %d canvas pixels differ from the file" % (label, bad))
        if want_fast and fell_back:
            FAILS.append("%s: pt_line was written, so the expansion FALLBACK "
                         "ran on a canvas whose width is a multiple of 8 - "
                         "the fast path is not being taken (SPEC.md 42.23.4)"
                         % label)
        if not want_fast and not fell_back:
            FAILS.append("%s: pt_line was untouched, so gfx_blit1 took a "
                         "width off the byte grid - rounding it up paints "
                         "canvas padding past the picture" % label)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    arm("byte grid", "PAT8.BMP", 208, True, a.machine)
    arm("off grid", "PAT7.BMP", 204, False, a.machine)

    for f in FAILS:
        print("paint1blit: " + f)
    if FAILS:
        print("paint1blit: FAIL")
        return 1
    print("paint1blit: PASS - gfx_blit1 and the expansion fallback each draw "
          "the file, and the width is what picks between them")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

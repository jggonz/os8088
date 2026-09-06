#!/usr/bin/env python3
"""DOES A 1bpp BMP LOAD, ON BOTH ADAPTERS? (SPEC.md 42.23.6)

    make && python3 tests/paint1load.py [--machine os8088_xt_vga]

SPEC.md 42.23.6's load rule is that a **colourless file** opens one bit deep
whatever the adapter is - so a monochrome BMP on a VGA gives a one-bit canvas
on a machine whose every other canvas is four planes.  That is the one path
in 42.23 no other row reaches: `pt_line_put`'s bit arm, `pt_fmtpick` running
before `pt_adopt`, and `pt_blit_1`'s expansion drawing a one-bit canvas
through a COLOUR renderer.  It is an ASSOC open (SPEC.md 54.5), so
`pt_fmtpick` runs on the first canvas the instance ever lays out.

**The fixture is built here rather than committed**, because what it has to
be is a pure function of what the reader is being asked: a 1bpp BMP with the
standard `{0 = black, 1 = white}` palette and a pattern whose every byte is
different from its neighbours, so a row read one byte early or one bit out of
phase cannot come back looking right.  `tools/os88disk.py` is deterministic,
so the disk is too.

**The oracle is the CANVAS against the FILE and not the screen.**  A one-bit
canvas is a byte-for-byte copy of a 1bpp BMP's pixel rows - that is the whole
of 42.23.2 - so the assertion is an `==` on the rows themselves, which no
repaint, dither or reduction can flatter.  The screen is checked only for
being drawn at all.

Two things that WOULD pass a pixel comparison and are asserted separately:
the depth Paint chose (a colour file's depth is not this file's), and
`[pt_trunc]` being CLEAR - nothing was reduced here, so File > Save must
still be allowed to overwrite the original (42.16).
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
W, H = 128, 48                  # a multiple of 32 wide, so the stride is exact
NAME = "MONO1.BMP"
BMP = "/tmp/" + NAME
IMG = "/tmp/paint1load.img"


def _rows():
    """H rows of ceil(W/8) bytes, every byte different from its neighbours."""
    stride = ((W + 7) // 8 + 3) & ~3
    out = []
    for y in range(H):
        r = bytearray(stride)
        for x in range(stride):
            r[x] = (x * 37 + y * 101 + 13) & 0xFF
        out.append(bytes(r))
    return stride, out


def _fixture():
    stride, rows = _rows()
    hdr = struct.pack("<2sIHHI", b"BM", 62 + stride * H, 0, 0, 62)
    hdr += struct.pack("<IiiHHIIIIII", 40, W, H, 1, 1, 0, stride * H,
                       0, 0, 2, 0)
    hdr += bytes([0, 0, 0, 0, 255, 255, 255, 0])        # black, white, BGRA
    assert len(hdr) == 62, len(hdr)
    open(BMP, "wb").write(hdr + b"".join(reversed(rows)))   # bottom-up
    return stride, rows


def _boff(seg, name):
    return ((seg << 4) + dispapps.img_size("paint")
            + dispapps.bss_off("paint", name))


def _w(m, seg, name, n=2):
    return int.from_bytes(m.read(_boff(seg, name), n), "little")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    stride, rows = _fixture()
    apps = os88marty.scratch_disk(IMG, "APPS:build/paint.o88",
                                  "MEDIA:" + BMP)
    fails = []

    with os88marty.launch(a.image, apps=apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        # An ASSOC OPEN out of the file window rather than Paint's own File >
        # Open (tests/paintsu.py's route): SPEC.md 54.5 launches the package
        # with the file's name, so pt_fmtpick runs on the FIRST canvas this
        # instance ever lays out - which is the case that matters, and the one
        # a second load into an existing canvas cannot reach.
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MEDIA")
        os88marty.settle(m)
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        x, y = dispcp.row_xy(wx, wy,
                             dispcp.scroll_to(m, mo, S, os88marty.settle,
                                              wx, wy,
                                              dispcp.row_of(m, S, NAME)))
        mo.dblclick(x, y)
        m.advance(frames=400)
        m.run()
        os88marty.settle(m)
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            sys.exit("paint1load: %s did not open Paint" % NAME)
        seg = got[1]

        one = _w(m, seg, "pt_1bpp", 1)
        cw, ch = _w(m, seg, "pt_cw"), _w(m, seg, "pt_ch")
        st, hdr = _w(m, seg, "pt_stride"), _w(m, seg, "pt_hdrsz")
        trunc = _w(m, seg, "pt_trunc", 1)
        print("   loaded: 1bpp=%d %dx%d stride %d hdr %d trunc %d"
              % (one, cw, ch, st, hdr, trunc))
        if not one:
            fails.append("a colourless BMP did NOT give a one-bit canvas "
                         "(SPEC.md 42.23.6)")
        if (cw, ch) != (W, H):
            fails.append("the canvas is %dx%d, the file is %dx%d"
                         % (cw, ch, W, H))
        if st != stride:
            fails.append("stride %d, the file's is %d" % (st, stride))
        if trunc:
            fails.append("[pt_trunc] is set, but nothing was reduced or "
                         "cropped here - File > Save must still be allowed "
                         "(SPEC.md 42.16)")
        if not fails:
            base = _w(m, seg, "pt_base")
            bad = 0
            for y in range(min(ch, H)):
                off = int.from_bytes(
                    m.read(_boff(seg, "pt_rowoff") + y * 2, 2), "little")
                sg = int.from_bytes(
                    m.read(_boff(seg, "pt_rowseg") + y * 2, 2), "little")
                if m.read((sg << 4) + off, stride) != rows[y]:
                    bad += 1
            print("   canvas against the file: %d of %d rows differ"
                  % (bad, min(ch, H)))
            if bad:
                fails.append("%d canvas row(s) are not the file's bytes - a "
                             "one-bit canvas IS a 1bpp BMP's pixel rows "
                             "(SPEC.md 42.23.2)" % bad)

    for f in fails:
        print("paint1load: " + f)
    if fails:
        print("paint1load: FAIL")
        return 1
    print("paint1load: PASS - a 1bpp BMP loads to a canvas that is the "
          "file's own bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

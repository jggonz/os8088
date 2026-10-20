#!/usr/bin/env python3
"""DOES A LOAD THAT DOES NOT GROW THE WINDOW REACH THE GLASS? (SPEC.md 11.90.3.2)

    make && python3 tests/paintnogrow.py [--machine os8088_xt_vga]

§11.90.3.1 lets `wm_resize` answer `wm_damage` with the EMPTY rect when a
window did not grow and its origin did not move - nothing painted over what
survived, so the app owes its content nothing.  That is true of every resize
Paint made when it was written (the size boxes, the full-screen exit) and
FALSE of the two §54.10 added: `pt_onwake` and `pt_ondlg` resize the window
*after* a load has replaced the whole canvas.  The picture decoded perfectly,
`pt_blit_dmg` drew nothing over it, and the toast said `Opened` on a white
window.

**THE TWO ARMS ARE THE ROW**, and the second is what makes the first mean
anything.  `PT_CW_DEF` is 448, so a 448-wide picture leaves the CGA window
exactly the size it already was and is the failing case; a 466-wide one GROWS
it, takes `wm_resize`'s `.norz` arm, gets a real damage rect and drew
correctly all along.  A Paint that draws no picture at all fails both; the
defect this row is for fails ONLY the first, which a one-arm row would have
reported as "Paint is broken" or missed entirely.

The oracle is the SCREEN and not the canvas.  tests/paint1load.py already
compares the canvas against the file byte for byte and passed throughout -
the canvas was always right - so a second reading of it would be green with
the window still white.  The fixture is therefore a picture whose value is
in what it looks like: a solid black band across the middle rows and white
above and below it, so one screen row inside the band and one outside it
answer "drawn", "blank" and "stale" apart from each other.

Both arms are the same height, so on CGA (where `pt_chmax` clamps the fresh
canvas to the picture's own height) the ONLY difference between them is the
width, and on VGA both shrink the height - which is the same bug by the other
axis, and why `--machine` is a real second row rather than a duplicate.
"""
import argparse
import tempfile
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(HERE)
H = 96
BAND1, BAND2 = 32, 63           # the black band's first and last rows
# 448 is PT_CW_DEF, so it never grows the window on either adapter: on CGA it
# leaves it exactly as it was and on VGA it SHRINKS it, the fresh canvas there
# being 448x280. 466 grows the width on both.
ARMS = ((448, "NOGROW.BMP", "the window does NOT grow"),
        (466, "GROWS.BMP", "it grows - the positive control"))
TMP = tempfile.gettempdir()


def _fixture(w, name):
    """A 1bpp BMP: rows BAND1..BAND2 solid black, every other row white."""
    stride = ((w + 31) // 32) * 4
    rows = []
    for y in range(H):
        rows.append(bytes([0x00] * stride) if BAND1 <= y <= BAND2
                    else bytes([0xFF] * stride))
    hdr = struct.pack("<2sIHHI", b"BM", 62 + stride * H, 0, 0, 62)
    hdr += struct.pack("<IiiHHIIIIII", 40, w, H, 1, 1, 0, stride * H,
                       0, 0, 2, 0)
    hdr += bytes([0, 0, 0, 0, 255, 255, 255, 0])        # black, white, BGRA
    assert len(hdr) == 62, len(hdr)
    p = os.path.join(TMP, name)
    open(p, "wb").write(hdr + b"".join(reversed(rows)))  # bottom-up
    return p


def _boff(seg, name):
    return ((seg << 4) + dispapps.img_size("paint")
            + dispapps.bss_off("paint", name))


def _w(m, seg, name, n=2):
    return int.from_bytes(m.read(_boff(seg, name), n), "little")


def _dark(px, w, y, x1, x2):
    """How many of the pixels in row y, x1..x2 are dark."""
    return sum(1 for x in range(x1, x2 + 1) if px[(y * w + x) * 3] < 128)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    files = ["APPS:build/paint.o88"]
    for w, name, _ in ARMS:
        files.append("MEDIA:" + _fixture(w, name))
    apps = os88marty.scratch_disk(tempfile.gettempdir() + "/paintnogrow.img", *files)
    fails = []

    for w, name, why in ARMS:
        # AN ASSOC OPEN (SPEC.md 54.5) rather than File > Open: it is the
        # route pt_onwake owns, and it is a fresh instance every time - so
        # the canvas this load lands in is PT_CW_DEF's and not the previous
        # arm's, which is the whole premise of the width table.
        with os88ui.boot(a.image, apps=apps, machine=a.machine) as ui:
            m = ui.m
            ui.open_drive("B")
            ui.open("MEDIA")
            ui.open(name)
            ui.settle()
            got = dispapps.pkg_seg(m, 0)
            if got is None:
                fails.append("%s did not open Paint at all" % name)
                continue
            seg = got[1]
            cw, ch = _w(m, seg, "pt_cw"), _w(m, seg, "pt_ch")
            cx0, cy0 = _w(m, seg, "pt_cx0"), _w(m, seg, "pt_cy0")
            if (cw, ch) != (w, H):
                fails.append("%s: the canvas is %dx%d, the file is %dx%d - "
                             "this row's premise is that the LOAD worked and "
                             "only the drawing did not"
                             % (name, cw, ch, w, H))
                continue
            sw, sh, px = m.fbuf()
            x1, x2 = cx0, min(cx0 + cw, sw) - 1
            n = x2 - x1 + 1
            mid = _dark(px, sw, cy0 + (BAND1 + BAND2) // 2, x1, x2)
            above = _dark(px, sw, cy0 + BAND1 // 2, x1, x2)
            print("   %-11s %3d wide, %-31s band row %d/%d dark, "
                  "white row %d/%d dark" % (name, w, why, mid, n, above, n))
            # 90%: the pointer sits on the canvas and the marquee-free canvas
            # is otherwise uniform, so a few pixels either way are the arrow.
            if mid < n * 9 // 10:
                fails.append("%s (%s): the picture's black band is %d of %d "
                             "pixels dark on the glass - the canvas holds it "
                             "and the screen does not (SPEC.md 11.90.3.2)"
                             % (name, why, mid, n))
            if above > n // 10:
                fails.append("%s (%s): %d of %d pixels are dark on a row the "
                             "picture leaves WHITE - what is on the glass is "
                             "not this picture" % (name, why, above, n))

    for f in fails:
        print("paintnogrow: " + f)
    if fails:
        print("paintnogrow: FAIL")
        return 1
    print("paintnogrow: PASS - a load reaches the glass whether or not the "
          "window grew")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

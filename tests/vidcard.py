#!/usr/bin/env python3
"""OSAPI_WM_RESIZE draws the pointer out of the way - SPEC.md 11.1.2 - and
the caption names the video, as long as the bar allows - 98.4.3.

    make && python3 tests/vidcard.py [--machine NAME]

The Video Player's info card (`i`) GROWS the window, and it does so from
OSAPI_WM_ONWAKE - the one callback that runs with no gfx lock held (SPEC.md
13). The slot took no lock either, so the grown window was drawn over the
pointer with no hide promised: the arrow vanished, and the next move put
back the desktop it had saved - a dark square in the middle of the card.

So: a 400 x 145 Hercules clip, the pointer parked on the desktop just right
of the window where the card will open, `i`, then the pointer moved away.
Two questions:

1. IS THE POINTER STILL DRAWN over the card after the resize? It was
   not - it had been painted over.
2. IS THE CARD WHOLE where the pointer was? The same region is taken again
   after the card is closed and reopened with the pointer far away, which
   is the card drawn with nothing on top of it, and the two must be
   identical.

And the caption, which the same resize redraws: the clip's title is 35
characters, the most the player keeps, so in the 418-wide window it is
'Video - <title>' (45 characters fit) and in the 706-wide one with the card
out 'Video Player - <title>'.

Broken on purpose - the slot pointed straight at wm_resize again, the old
unlocked door - it FAILS on both pointer questions.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88vid as vid, os88geom as geom  # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

WB, H, NF, FPS = 50, 145, 20, 10
TITLE = "The Longest Title This Player Shows"     # 35: VP_COLS, the most


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(tmp):
    """20 frames of a 400 x 145 Hercules picture, silent"""
    paths = []
    for f in range(NF):
        cv = bytearray(WB * H)
        for y in range(H):
            cv[y * WB:(y + 1) * WB] = bytes([0x55 if (y + f) & 1 else 0xAA]) \
                * WB
        p = os.path.join(tmp, "f%03d.pbm" % f)
        vid._write_pbm(p, WB, H, bytes(cv))
        paths.append(p)
    out = os.path.join(tmp, "CLIP.V88")
    vid.encode_frames(paths, out, FPS, None, "herc", TITLE)
    vid.verify_v88(out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp)
        disk = os.path.join(tmp, "vidcard.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine) as ui:
            m = ui.m
            w = ui.path("B:/CLIP.V88")
            wrec = ui._S("wm_wins") + w.i * geom.WIN_SIZE

            def rect():
                r = m.read(wrec, geom.WIN_SIZE)
                return (u16(r, geom.W_X), u16(r, geom.W_Y), u16(r, geom.W_W),
                        u16(r, geom.W_H))

            base = u16(m.read(wrec, geom.WIN_SIZE), geom.W_SEG) << 4

            def rb(n):
                return m.read(base + syms[n], 1)[0]

            def caption():
                tp = u16(m.read(wrec, geom.WIN_SIZE), geom.W_TITLE)
                b = bytes(m.read(base + tp, 64))
                return b[:b.index(0)].decode("latin-1")

            def says(want, what):
                got = caption()
                print("   the caption %s: %r" % (what, got))
                if got != want:
                    bad.append("the caption %s is %r, not %r (SPEC.md 98.4.3)"
                               % (what, got, want))

            os88marty.until(m, lambda mm: u16(m.read(base + syms["vp_ploads"],
                                                     2)) >= 1,
                            "the poster", poll=0.3, limit=300, guest=60)
            x0, y0, w0, h0 = rect()
            # 418 wide: 45 characters, so 'Video Player - ' + 35 does not fit
            says("Video - " + TITLE, "at %d wide" % w0)
            px, py = x0 + w0 + 40, y0 + 60        # desktop, where it will grow
            cell = (px - 2, py - 2, px + 14, py + 18)

            def region(rows):
                return [tuple(rows[y][cell[0]:cell[2]])
                        for y in range(cell[1], cell[3])]

            ui.mo.to(px, py)
            os88marty.pace(m, 1.5)
            m.type_text("i")
            os88marty.until(m, lambda mm: rb("vp_card") == 1 and
                            rect()[2] > w0, "the card open and the window "
                            "grown", poll=0.3, limit=120, guest=20)
            os88marty.pace(m, 2.0)
            x1, y1, w1, h1 = rect()
            says("Video Player - " + TITLE, "at %d wide" % w1)
            print("   window %dx%d -> %dx%d, the pointer at %d,%d"
                  % (w0, h0, w1, h1, px, py))
            if not (x1 <= px < x1 + w1 and y1 <= py < y1 + h1):
                bad.append("the grown window does not cover the pointer - "
                           "the row asks nothing (window %r)" % (rect(),))
            # 1: the arrow over the card - checked below, once the card drawn
            # alone is known: the region with the pointer on it must differ
            _, _, rows0 = m.vram(None)
            ui.mo.to(px + 150, py + 100)
            os88marty.pace(m, 1.5)
            _, _, after = m.vram(None)
            got = region(after)
            # 2: the card drawn with nothing on top: close and reopen it with
            # the pointer far away
            m.type_text("i")
            os88marty.until(m, lambda mm: rb("vp_card") == 0 and
                            rect()[2] == w0, "the card closed", poll=0.3,
                            limit=120, guest=20)
            m.type_text("i")
            os88marty.until(m, lambda mm: rb("vp_card") == 1 and
                            rect()[2] == w1, "the card open again", poll=0.3,
                            limit=120, guest=20)
            os88marty.pace(m, 2.0)
            _, _, ref = m.vram(None)
            want = region(ref)
            if rect() != (x1, y1, w1, h1):
                bad.append("the card reopened elsewhere (%r against %r) - the "
                           "comparison would be meaningless"
                           % (rect(), (x1, y1, w1, h1)))
            drawn = region(rows0) != want
            print("   the pointer over the card after the resize: %s"
                  % ("drawn" if drawn else "NOT DRAWN"))
            if not drawn:
                bad.append("the pointer is not drawn over the grown window - "
                           "the resize painted over it (SPEC.md 11.1.2)")
            d = sum(1 for ra, rbw in zip(got, want)
                    for pa, pb in zip(ra, rbw) if pa != pb)
            print("   where the pointer was, after it moved: %d pixels of %d "
                  "differ from the card drawn alone" % (d, 16 * 20))
            if d:
                bad.append("the pointer put back what it saved BEFORE the "
                           "resize: %d pixels of the card are wrong where it "
                           "stood (SPEC.md 11.1.2)" % d)
    if bad:
        for b in bad:
            print("FAIL:", b)
        return 1
    print("OK: the resize hid the pointer and it came back over the card")
    return 0


if __name__ == "__main__":
    sys.exit(main())

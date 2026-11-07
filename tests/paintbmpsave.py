#!/usr/bin/env python3
"""A BMP SAVED OFF A PLANAR CANVAS IS A BMP (SPEC.md 42.13.5).

    make && python3 tests/paintbmpsave.py [--machine os8088_xt_vga]

On a colour adapter Paint holds its canvas as FOUR PLANES (SPEC.md 42.13), and
every codec but one reaches it through `pt_line_get`/`pt_line_put`. The BMP
SAVE was the one: it wrote the canvas as the file, because before 42.13 the
canvas WAS the file. So on VGA and EGA a picture saved as `.BMP` was four
quarter-width bit-planes under a 4bpp header - it reopened as four squeezed
copies with fringes, and the user's picture was gone from the disk. GIF saves
were fine, CGA and Hercules (packed, one bit) were fine, and every BMP row in
this directory ran on a 1bpp machine.

THE ORACLE IS PAINT'S OWN GIF. A sixteen-colour picture made here is opened
(every colour in every plane, see main), then saved twice off the same
canvas - `Save as Bmp` and `Save as Gif` - and the B: image is flushed and
read back ON THE HOST. Both files are decoded by PIL to RGB and must agree
pixel for pixel. The GIF writer goes through `pt_line_get` and is the path
42.13 said was right; a BMP that matches it is the canvas, whatever Paint's
colour mapping (`pt_map16`) did to the original.

Two preconditions are asserted rather than assumed, because either would make
the comparison pass about nothing: `[pt_planar]` is 1 (the canvas really is
four planes on this machine), and the two files differ from each other in
BYTES (a BMP and a GIF of the same picture cannot be the same file).

VERIFIED RED with pt_save's .bmp arm put back to writing the canvas raw: the
BMP decodes to the right SIZE and differs from the GIF at most of its pixels.
"""
import argparse
import io
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88build                                             # noqa: E402
import os88marty                                             # noqa: E402
import os88ui                                                # noqa: E402
from paintmove import pkg_syms                               # noqa: E402

I_STATE, I_SPTR, I_NAME, I_RECSZ, INST_MAX = 0, 6, 12, 32, 12
PW, PH = 200, 64                        # the fixture: 25 byte columns a plane row


def fail(msg):
    print("paintbmpsave: FAIL: %s" % msg)
    sys.exit(1)


def root_file(img, name):
    """One ROOT file of a FAT12 image, or None (os88fat reads the root)."""
    import os88fat
    try:
        return os88fat.Fat12(img).read(name)
    except KeyError:
        return None


def paint_seg(m, S):
    raw = m.read(S("inst_tab"), I_RECSZ * INST_MAX)
    for i in range(INST_MAX):
        r = raw[i * I_RECSZ:(i + 1) * I_RECSZ]
        nm = bytes(r[I_NAME:I_NAME + 16]).split(b"\0")[0]
        if r[I_STATE] == 1 and nm == b"PAINT":
            return r[I_SPTR] | (r[I_SPTR + 1] << 8)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    a = ap.parse_args()
    from PIL import Image

    sysimg = os88build.at("build/os8088-360.img")
    tmp = tempfile.mkdtemp(prefix="paintbmpsave-")
    apps = os.path.join(tmp, "apps.img")
    back = os.path.join(tmp, "back.img")
    # THE FIXTURE IS SIXTEEN COLOURS, MADE HERE. build/OS8088.GIF has two
    # table entries and SPEC.md 42.23.6 opens that ONE BIT DEEP on any
    # adapter, so it never reaches the planes (dispapps.colour_gif's note).
    # A 16-entry GIF whose every pixel is (3x + 5y) mod 16 puts every colour
    # in every plane and changes colour at nearly every pixel, so a plane
    # mix-up cannot land on a pixel that happens to agree. It is DERIVED
    # rather than committed (CONTRIBUTING.md 6), and its palette is the
    # sixteen-colour CGA/EGA one, which Paint's own sixteen are.
    pic = os.path.join(tmp, "COL16.GIF")
    im = Image.new("P", (PW, PH))
    pal = []
    for i in range(16):
        lo = 0x55 if i & 8 else 0
        pal += [lo + (0xAA if i & 4 else 0), lo + (0xAA if i & 2 else 0),
                lo + (0xAA if i & 1 else 0)]
    pal[6 * 3 + 1] = 0x55                   # brown, as a CGA draws it
    im.putpalette(pal)
    im.putdata([(3 * x + 5 * y) % 16 for y in range(PH) for x in range(PW)])
    im.save(pic)
    # THE PICTURE AT THE ROOT and no MEDIA folder, so the Save chooser opens
    # where the document is and both files land where os88fat reads them
    subprocess.run(["python3", "tools/os88disk.py", "-o", apps, "--size",
                    "360", os88build.at("build/paint.o88"), pic], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)
    syms = pkg_syms(os.path.join(ROOT, "apps/paint/paint.asm"))

    with os88ui.boot(sysimg, apps=apps, machine=a.machine, quick=True) as ui:
        m = ui.m
        ui.path("B:/COL16.GIF")             # Paint, by association
        if not paint_seg(m, ui.sym):
            fail("no live instance named PAINT after opening COL16.GIF")

        # THE DECODE IS SECONDS OF GUEST TIME (seconds on the XT), so the wait is on Paint's own word: the canvas in its planar
        # format and the UI quiet. [pt_planar] is set when the colour file is
        # ADOPTED; ui_done then waits the decode and its repaint out.
        def planar_now():
            sg = paint_seg(m, ui.sym)
            return sg and m.read((sg << 4) + syms["pt_planar"], 1)[0]
        try:
            os88marty.until(m, lambda _: planar_now(), "Paint's canvas to "
                            "become planar", limit=600.0)
        except os88marty.MartyError:
            pass
        os88marty.ui_done(m, limit=600.0)
        seg = paint_seg(m, ui.sym)
        planar = m.read((seg << 4) + syms["pt_planar"], 1)[0]
        print("paintbmpsave: Paint is up, [pt_planar] = %d" % planar)
        if planar != 1:
            fail("[pt_planar] is %d on %s - the canvas is not four planes "
                 "here, so this row would be testing the packed path"
                 % (planar, a.machine))
        for item, name in (("Save as Bmp", "T.BMP"), ("Save as Gif", "T.GIF")):
            ui.menu_pick("File", item)
            ui.chooser_save(name)
            ui.wait_toast(says="Saved", limit=600.0)
            os88marty.ui_done(m, limit=600.0)
            seg = paint_seg(m, ui.sym)      # the region is movable
            print("paintbmpsave: %s -> %s" % (item, name))
        if m.read((seg << 4) + syms["pt_planar"], 1)[0] != 1:
            fail("the canvas is no longer planar after the BMP save - the "
                 "conversion for the write was not undone (SPEC.md 42.13.5)")
        m.pause()
        m.flush(1, back)

    bmp, gif = root_file(back, "T.BMP"), root_file(back, "T.GIF")
    if bmp is None or gif is None:
        fail("B: holds T.BMP %s and T.GIF %s after the saves"
             % ("yes" if bmp else "NO", "yes" if gif else "NO"))
    if bmp == gif:
        fail("T.BMP and T.GIF are the same bytes")
    bi = Image.open(io.BytesIO(bmp)).convert("RGB")
    gi = Image.open(io.BytesIO(gif)).convert("RGB")
    print("paintbmpsave: T.BMP %d bytes %dx%d, T.GIF %d bytes %dx%d"
          % (len(bmp), bi.width, bi.height, len(gif), gi.width, gi.height))
    if bi.size != gi.size:
        fail("T.BMP is %dx%d and T.GIF %dx%d" % (bi.size + gi.size))
    bp, gp = bi.load(), gi.load()
    bad = [(x, y) for y in range(bi.height) for x in range(bi.width)
           if bp[x, y] != gp[x, y]]
    colours = len(gi.getcolors(256) or ())
    print("paintbmpsave: %d colours in the picture, %d of %d pixels differ"
          % (colours, len(bad), bi.width * bi.height))
    if colours < 4:
        fail("the picture has %d colours, too few for a plane mix-up to show"
             % colours)
    if bad:
        x, y = bad[0]
        fail("T.BMP differs from Paint's own T.GIF at %d pixels, first at "
             "(%d,%d): %r against %r - the BMP save wrote the PLANAR canvas "
             "raw (SPEC.md 42.13.5)" % (len(bad), x, y, bp[x, y], gp[x, y]))
    # ...AND BOTH ARE THE PICTURE THAT WAS OPENED: one RGB per fixture
    # index, sixteen different ones - so a GIF writer that had gone wrong the
    # same way as the BMP could not make the pair agree on a wrong answer
    seen = {}
    for y in range(PH):
        for x in range(PW):
            seen.setdefault((3 * x + 5 * y) % 16, set()).add(bp[x, y])
    if any(len(v) != 1 for v in seen.values()) or \
            len(set(next(iter(v)) for v in seen.values())) != 16:
        fail("the saved picture is not the one opened: fixture index -> "
             "saved colour is %r, and it must be sixteen distinct colours one "
             "each" % {k: sorted(v) for k, v in seen.items()})
    print("paintbmpsave: ok - the BMP saved off a planar canvas is the "
          "picture, pixel for pixel against the GIF")
    return 0


if __name__ == "__main__":
    sys.exit(main())

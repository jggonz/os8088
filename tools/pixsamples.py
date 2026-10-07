#!/usr/bin/env python3
"""PiXEL's sample gallery (SPEC.md 106.7, PIXEL-PLAN decision 14).

    python3 tools/pixsamples.py --check            # the committed files are these
    python3 tools/pixsamples.py --regen SRC_DIR    # remake them (needs Pillow)

THE OUTPUTS ARE COMMITTED AND THIS IS NOT A BUILD STEP. `make` never runs
--regen: the gallery under apps/pixel/samples/ is checked in, so a tree with no
Pillow builds every disk, and a release is rebuilt byte for byte from the tree
alone. --check is what the build runs - it reads nothing but the committed
files and compares each with the SHA-256 pinned below, so a sample that was
edited, truncated or re-encoded by accident fails the fast tier rather than
shipping.

THE PICTURES ARE ORIGINAL. They were generated for this project (nine 1200x896
scenes, apps/pixel/samples/README.TXT says how) and are not committed at that
size: --regen reduces each to at most 640x480 and re-encodes it into one of the
formats PiXEL reads, so the gallery shows every decoder something - baseline
JPEG at 4:2:0 and 4:4:4, a progressive one, a greyscale one, a 256-colour GIF,
an 8-bit and a truecolour PNG, a 24-bit BMP and an 8-bit PCX. The tenth,
BOUNCE.GIF, has no scene: `bounce()` below draws its ten frames (SPEC.md
106.25's animation), so --regen makes it from nothing.

Pillow's encoders are not promised stable across its versions, so a --regen on
another machine may well produce different bytes. That is expected: re-pin the
hashes with --pin and commit both, and say so in the commit.
"""
import argparse
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "apps", "pixel", "samples")

# name, source stem, size, how
GALLERY = [
    ("VACATION.JPG", "vacation", (640, 480), "jpeg q85 420"),
    ("FLOWER.JPG",   "flower",   (640, 480), "jpeg q80 444"),
    ("ROOM.JPG",     "room",     (640, 480), "jpeg q80 progressive"),
    ("LAKE.JPG",     "lake",     (320, 240), "jpeg q85 grey"),
    ("CAT.GIF",      "cat",      (320, 240), "gif 256"),
    ("BALLOONS.PNG", "balloons", (320, 240), "png 8-bit palette"),
    ("HOUSE.PNG",    "house",    (320, 240), "png truecolour"),
    ("MOUNTAIN.BMP", "mountains", (256, 192), "bmp 24-bit"),
    ("CITY.PCX",     "city",     (320, 240), "pcx 8-bit"),
    ("BOUNCE.GIF",   None,       (96, 72),   "gif animated"),
]

# THE 360KB DISK'S SUBSET (SPEC.md 106.7): what fits beside the package on
# 354 one-KB clusters with room to spare - one of each family, smallest first
SUBSET_360 = ["VACATION.JPG", "LAKE.JPG", "CAT.GIF", "BALLOONS.PNG",
              "BOUNCE.GIF"]
# ...and the 720KB one's: everything but the truecolour PNG, the largest file,
# which the 8-bit PNG stands in for there (the whole gallery is 729KB and that
# volume holds ~710KB). The Makefile spells all three lists out - PX_SAMPLES,
# PX_SAMPLES_720, PX_SAMPLES_360 - and --check compares them with these, so a
# list edited in one place and not the other fails the build
SUBSET_720 = ["VACATION.JPG", "FLOWER.JPG", "ROOM.JPG", "LAKE.JPG",
              "CAT.GIF", "BALLOONS.PNG", "MOUNTAIN.BMP", "CITY.PCX",
              "BOUNCE.GIF"]

# --pin writes these. The --check gate reads nothing else.
PINS = {
    "VACATION.JPG": "3f10a7dd746b7a06ad889f8a06de54c410b242ab2026daf620667002296fdab1",
    "FLOWER.JPG": "1926272c1723ba406837232372930761724a8b878c3cadb204e6a019d26c0fbe",
    "ROOM.JPG": "1cb9d3b8a8d3e57f14e0ee61d24c09564143e83fd1624fd3943391da195f5a33",
    "LAKE.JPG": "4e4b5d2ce8a71e928a7eb5ecc62d1e494e36860085fb6736e08e0223a3812117",
    "CAT.GIF": "97e82da1de59be48788ef4a309145dfb83180c020dd9a6978dd6dab39530abe5",
    "BALLOONS.PNG": "5b5badca4382070d0525ad1ace785ae454a01faf72c6cafe94829fd8db4db3e0",
    "HOUSE.PNG": "08c8271856815644810642ab59f9ef6486a37a6a13c294c38cee53bb669b3ef9",
    "MOUNTAIN.BMP": "ce1177df39609c0d7e0e79a100300e33a666cc9ec11b2e19a0400e3edf3fb7a1",
    "CITY.PCX": "719ea82e9b80ee701133a7a125a15dd584e599f5823c67b6a9146922f8ce0508",
    "BOUNCE.GIF": "8b96774a6de8165ffccba6f6084d0d1ff7ebd3b97988462abfd421a6255fb5f4",
}


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def fit(im, size):
    """Crop to the target's aspect from the centre, then reduce."""
    from PIL import Image
    w, h = im.size
    tw, th = size
    if w * th > h * tw:                     # too wide: trim the sides
        nw = h * tw // th
        x = (w - nw) // 2
        im = im.crop((x, 0, x + nw, h))
    else:                                   # too tall: trim top and bottom
        nh = w * th // tw
        y = (h - nh) // 2
        im = im.crop((0, y, w, y + nh))
    return im.resize(size, Image.LANCZOS)


def bounce(size, n=10):
    """BOUNCE.GIF (SPEC.md 106.25): a beach ball crossing the sand, drawn
    here rather than reduced from a scene - n frames of an 8-colour picture,
    a tenth of a second each, looping for ever, each frame only the rect it
    changed (disposal 1: Pillow's own crop of the difference)"""
    import math
    from PIL import Image, ImageDraw
    w, h = size
    pal = [(96, 160, 224), (240, 216, 160), (208, 40, 40), (250, 250, 250),
           (150, 120, 80), (40, 40, 60), (255, 220, 64), (120, 190, 240)]
    frames = []
    for k in range(n):
        im = Image.new("P", (w, h), 0)
        im.putpalette(sum(pal, ()))
        d = ImageDraw.Draw(im)
        d.rectangle((0, 56, w - 1, h - 1), fill=1)          # the sand
        d.ellipse((74, 5, 90, 21), fill=6)                  # the sun
        t = k / n
        x = 11 + int(74 * t)
        up = abs(math.sin(math.pi * 2 * t))
        y, r, sw = 47 - int(36 * up), 7, 10 - int(4 * up)
        d.ellipse((x - sw, 58, x + sw, 62), fill=4)         # its shadow
        d.ellipse((x - r, y - r, x + r, y + r), fill=2)
        for a0 in (30, 210):                                # the stripes
            d.pieslice((x - r, y - r, x + r, y + r), a0 + 30 * k,
                       a0 + 60 + 30 * k, fill=3)
        d.ellipse((x - r, y - r, x + r, y + r), outline=5)
        frames.append(im)
    return frames


def regen(src):
    from PIL import Image
    os.makedirs(OUT, exist_ok=True)
    for name, stem, size, how in GALLERY:
        if stem is None:
            dst = os.path.join(OUT, name)
            fr = bounce(size)
            fr[0].save(dst, "GIF", save_all=True, append_images=fr[1:],
                       duration=100, loop=0, optimize=False, disposal=1)
            print(f"  {name:13s} {os.path.getsize(dst):7,d} bytes  {size[0]}x{size[1]}  {how}")
            continue
        path = None
        for ext in (".jpg", ".png", ".jpeg"):
            p = os.path.join(src, stem + ext)
            if os.path.exists(p):
                path = p
                break
        if path is None:
            sys.exit(f"pixsamples: no source for {name} ({stem}.*) in {src}")
        im = fit(Image.open(path).convert("RGB"), size)
        dst = os.path.join(OUT, name)
        if how == "jpeg q85 420":
            im.save(dst, "JPEG", quality=85, subsampling=2, optimize=False)
        elif how == "jpeg q80 444":
            im.save(dst, "JPEG", quality=80, subsampling=0, optimize=False)
        elif how == "jpeg q80 progressive":
            im.save(dst, "JPEG", quality=80, subsampling=2, progressive=True)
        elif how == "jpeg q85 grey":
            im.convert("L").save(dst, "JPEG", quality=85)
        elif how == "gif 256":
            im.quantize(256, dither=Image.Dither.FLOYDSTEINBERG).save(dst, "GIF")
        elif how == "png 8-bit palette":
            im.quantize(256, dither=Image.Dither.FLOYDSTEINBERG).save(
                dst, "PNG", optimize=True)
        elif how == "png truecolour":
            im.save(dst, "PNG", optimize=True)
        elif how == "bmp 24-bit":
            im.save(dst, "BMP")
        elif how == "pcx 8-bit":
            im.quantize(256, dither=Image.Dither.FLOYDSTEINBERG).save(dst, "PCX")
        else:
            sys.exit(f"pixsamples: no recipe {how!r}")
        print(f"  {name:13s} {os.path.getsize(dst):7,d} bytes  {size[0]}x{size[1]}  {how}")


def check():
    bad = 0
    total = 0
    if not PINS:
        sys.exit("pixsamples: no pins - run --pin after --regen")
    for name, *_ in GALLERY:
        p = os.path.join(OUT, name)
        if not os.path.exists(p):
            print(f"pixsamples: {name} is missing from apps/pixel/samples/")
            bad += 1
            continue
        total += os.path.getsize(p)
        if sha(p) != PINS.get(name):
            print(f"pixsamples: {name} is not the pinned file "
                  f"(re-encoded? re-pin with --pin and say so in the commit)")
            bad += 1
    extra = sorted(set(os.listdir(OUT)) - {g[0] for g in GALLERY}
                   - {"README.TXT"})
    for e in extra:
        print(f"pixsamples: {e} in apps/pixel/samples/ is not in the gallery")
        bad += 1
    bad += makefile_lists()
    if bad:
        sys.exit(1)
    print(f"pixsamples: {len(GALLERY)} pictures, {total:,} bytes, all pinned")


def makefile_lists():
    """The Makefile's three sample lists against GALLERY and the subsets."""
    want = {"PX_SAMPLES": [g[0] for g in GALLERY],
            "PX_SAMPLES_720": SUBSET_720, "PX_SAMPLES_360": SUBSET_360}
    text = open(os.path.join(HERE, "..", "Makefile")).read().replace("\\\n", " ")
    bad = 0
    for var, names in want.items():
        got = None
        for line in text.split("\n"):
            if line.startswith(var + " :="):
                got = [os.path.basename(w) for w in line.split(":=", 1)[1].split()]
        if got is None:
            print(f"pixsamples: the Makefile has no {var}")
            bad += 1
        elif got != names:
            print(f"pixsamples: the Makefile's {var} is {got}, and this file says {names}")
            bad += 1
    return bad


def pin():
    me = os.path.abspath(__file__)
    text = open(me).read()
    lines = ["PINS = {"]
    for name, *_ in GALLERY:
        lines.append(f'    "{name}": "{sha(os.path.join(OUT, name))}",')
    lines.append("}")
    a = text.index("PINS = {")
    b = text.index("}", a) + 1
    open(me, "w").write(text[:a] + "\n".join(lines) + text[b:])
    print("pixsamples: pinned", len(GALLERY))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--regen", metavar="SRC_DIR")
    ap.add_argument("--pin", action="store_true")
    a = ap.parse_args()
    if a.regen:
        regen(a.regen)
    if a.pin:
        pin()
    if a.check:
        check()


if __name__ == "__main__":
    main()

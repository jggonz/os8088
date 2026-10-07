#!/usr/bin/env python3
"""PiXEL's JPEG fixtures (SPEC.md 106.19, PIXEL-PLAN decision 15).

    python3 tools/pixjpeg.py --check        # the committed files are these (FAST)
    python3 tools/pixjpeg.py --verify       # pixelsim on every one, every scale,
                                            # and Pillow's PSNR where Pillow is
    python3 tools/pixjpeg.py --regen        # remake them (needs Pillow and cjpeg)

THE OUTPUTS ARE COMMITTED AND THIS IS NOT A BUILD STEP. A pure-Python JPEG
ENCODER would be more code than the decoder it tests (the plan's decision 15),
so the good fixtures were made once by Pillow and libjpeg-turbo's cjpeg - every
sampling PiXEL reads, restart markers, sizes that are no multiple of an MCU,
the eight EXIF orientations, 16-bit quantisation tables, progressive scripts
with refinement scans, and the four kinds of JPEG PiXEL refuses by name - and
are committed under tests/pixel/, each pinned by its SHA-256. --check reads
nothing else and is the fast row; tools/pixcorpus.py derives the HOSTILE half
from these files in pure Python (truncations, lying lengths, broken tables,
bad progressions), so no hostile file needs committing.

--regen on another machine may well write different bytes (Pillow's and
libjpeg's encoders are not promised stable across versions). That is expected:
re-pin with --pin and commit both, saying so.

--verify is the soak row `pixjpegref`: every good fixture through pixelsim at
every scale it may be shown at, every refused one refused with its number, and
- when Pillow is installed - each decode against Pillow's own at PSNR > 30 dB
(a box average of Pillow's picture below 1/1, and its EXIF orientation applied
with ImageOps.exif_transpose). Without Pillow that half SKIPS and says why.
"""
import argparse
import hashlib
import io
import math
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "tests", "pixel")
SAMPLES = os.path.join(HERE, "..", "apps", "pixel", "samples")
sys.path.insert(0, HERE)

# name, what, verdict (0 good, else pixelsim's PXD_* number). "src" is the
# crop it is made from: (sample, x, y, w, h)
CROP = ("VACATION.JPG", 236, 160, 40, 32)
FIXTURES = [
    ("J444.JPG",    "Pillow q85 4:4:4 40x24", 0),
    ("J422.JPG",    "Pillow q85 4:2:2 40x24", 0),
    ("J420.JPG",    "Pillow q85 4:2:0 40x24", 0),
    ("J411.JPG",    "cjpeg q80 -sample 4x1 40x24", 0),
    ("J440.JPG",    "cjpeg q80 -sample 1x2 40x24", 0),
    ("JGREY.JPG",   "cjpeg q85 -grayscale 40x24", 0),
    ("JODD.JPG",    "cjpeg q85 4:2:0 37x23", 0),
    ("JODD411.JPG", "cjpeg q85 -sample 4x1 37x23", 0),
    ("JRST.JPG",    "cjpeg q85 4:2:0 -restart 1B 40x32", 0),
    ("JRSTR.JPG",   "cjpeg q85 4:2:0 -restart 1 (rows) 40x32", 0),
    ("JRSTG.JPG",   "cjpeg q85 -grayscale -restart 3B 40x24", 0),
    ("JQ16.JPG",    "cjpeg -quality 3: 16-bit DQT, SOF1", 0),
    ("JQ100.JPG",   "cjpeg -quality 100 4:4:4", 0),
    ("JOPT.JPG",    "cjpeg q85 -optimize 4:2:0", 0),
    ("JRGB.JPG",    "cjpeg -rgb: components R, G, B", 0),
    ("JPROG.JPG",   "cjpeg q85 -progressive 4:2:0 40x32", 0),
    ("JPROGG.JPG",  "cjpeg q85 -progressive -grayscale 40x24", 0),
    ("JPROG444.JPG", "cjpeg q85 -progressive 4:4:4 37x23", 0),
    ("JPROGR.JPG",  "cjpeg q85 -progressive -restart 2B 40x32", 0),
    ("JPBIG.JPG",   "cjpeg q85 -progressive 4:2:0 128x96", 0),
    ("JBIG.JPG",    "Pillow q85 4:2:0 128x96", 0),
    ("JO1.JPG",     "Pillow q85 4:2:0 24x16, EXIF orientation 1", 0),
    ("JO2.JPG",     "...orientation 2", 0),
    ("JO3.JPG",     "...orientation 3", 0),
    ("JO4.JPG",     "...orientation 4", 0),
    ("JO5.JPG",     "...orientation 5", 0),
    ("JO6.JPG",     "...orientation 6", 0),
    ("JO7.JPG",     "...orientation 7", 0),
    ("JO8.JPG",     "...orientation 8", 0),
    ("JEXBIG.JPG",  "orientation 6 in a 6 KB EXIF: the frame header past the head", 0),
    ("JARITH.JPG",  "cjpeg -arithmetic: refused", 4),
    ("J12BIT.JPG",  "cjpeg -precision 12: refused", 3),
    ("JLOSSL.JPG",  "cjpeg -lossless 1: refused", 4),
    ("JSCANS.JPG",  "cjpeg -scans: a baseline picture in three scans, refused", 4),
]

# --pin writes these. The --check gate reads nothing else.
PINS = {
    "J444.JPG": "b56c59481b745075a81489cba99993b1722f1577434d40bc38e68c9fd4164523",
    "J422.JPG": "a80b75ec298ce6a9eac40ac380e2c579a74a0eabe47f9962d4c2a27e10923b9d",
    "J420.JPG": "260d241e6e4ee69bb022ee7b9fe39bcc740b6423fe890da10d6c47a037763cc5",
    "J411.JPG": "24056cd9b634edbac08132f8717e4276bea1fc931a635606653101bf1b4d038d",
    "J440.JPG": "8b03dd219bbd9e56594bf683998c8b05cfbf646d5134548092dc26b44a7c04fe",
    "JGREY.JPG": "32012d23331c7f8b234a1e27fa1f9a8bc129527581e475bc0f9486ea767d800e",
    "JODD.JPG": "71fa37f12502be90a12e92f707a1aca1f0870f4bfa0297920dad656d814a36c3",
    "JODD411.JPG": "f32c27ba29cb195eab52bb2c85314eba4779367d3c66e3310b54d438f1ba7d81",
    "JRST.JPG": "7fb04fb0407574866d54bb73cf40d75cc377b4d165c4b1dcfbb7c3229ecfb48a",
    "JRSTR.JPG": "65e162f63fe66e684a7b0220c90ee939144bb31d888daae4aadaa68b3393c728",
    "JRSTG.JPG": "cec966f2f5a9381caecbb0af2768d61aea1c0a0bc9b62f2248ce415d1e812332",
    "JQ16.JPG": "333bc4fa48809b59b97a93bb77f323947f6ce35c166c1ce963cc2a0b444214dd",
    "JQ100.JPG": "310a9fcb85d2b8ed4356d14aa941e1dec9b446bd9785293f66d86f9437b296f3",
    "JOPT.JPG": "6638a5acac707c95e44578ec3c0e2c30ea3cae0c7358227e2ab9645c52652221",
    "JRGB.JPG": "3a107d8f2c6a19de7f2476e251a278304e1a3b3c6a7e64326b35d664a57ddc6d",
    "JPROG.JPG": "855543a8da2bad4362b0b6d81f52bbc67e7dfd886ba5e3ab37bf80dc50df64b7",
    "JPROGG.JPG": "58a222c0e633fdd24243328afe1254652d68e86603539c5e5269f3b066e27cb1",
    "JPROG444.JPG": "039267fa08c7264494fce80cebf1fe436fd1a49663019dd08ff3921724efdcf8",
    "JPROGR.JPG": "95307626d30f08fd23f262b9cd650b356faadeadd25a24a8c1483aa47472e5be",
    "JPBIG.JPG": "cfa96f9f35b12d0095f3cddd26de9d41fdac03126a6e311f8e1bd7718bec16ac",
    "JBIG.JPG": "f405cb9dfbdfadd9d6515274ece0bc1206245206e1b17ff636127b11da22b888",
    "JO1.JPG": "801dccbaf1319cbc019f06a8aae17390865f5e213685d800c8be66c17cf92a32",
    "JO2.JPG": "a3e7f738196534cc01223ab467e110b12d58bd244d34561780acf9adb3f888c6",
    "JO3.JPG": "f4a9850f0f52b238b4d7c39bfabb1373e04685eeb14dadb5764cbb5f08b05191",
    "JO4.JPG": "1ea42a95f46b414fb17ccb8c352e056b0680d6dddaa6e383bd0212f5ce27acfa",
    "JO5.JPG": "35e5b1280109144e1c03107883e4c977b82ebe004e0b0f310a3e1cd7662a351b",
    "JO6.JPG": "63fbcb0f082418b5327b51edac38b0e4d40a79f2e5a7f8a6aebd1e68181fbf01",
    "JO7.JPG": "d8240b741ad5802ebf4b2f90eb501241301c3ae9e56f1c088f70c77fd4b416d6",
    "JO8.JPG": "b2021def2160f8e80ea01d590e933f4671d54becfe1753d0b1cad25e84ee446c",
    "JEXBIG.JPG": "b643715924193d6b450ba385417c838d5d400782fa8236cad98b6020af28df07",
    "JARITH.JPG": "fdf4ee7dd8c34592a72370585d8dd6a3814c869a8f1e0599d13e229f30eb735e",
    "J12BIT.JPG": "1d1bccd2e9a2213e06e1c0a11cdff3b08a14eef59a4b56e941e5c4f4b67a552a",
    "JLOSSL.JPG": "c807f6df84628e56f097cf3fcf24ec004b9f52ffd11130d7eb323b06bf294e31",
    "JSCANS.JPG": "4cde10c3586f3b9f013296a74face885c1d51db7f67448795ac0009b14229af2",
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def path(name):
    return os.path.join(OUT, name)


# =============================================================================
# --regen (Pillow and cjpeg)
# =============================================================================
def regen():
    from PIL import Image
    src = Image.open(os.path.join(SAMPLES, CROP[0])).convert("RGB")
    x, y, w, h = CROP[1:]
    big = src.crop((x, y, x + w, y + h))
    os.makedirs(OUT, exist_ok=True)
    tmp = tempfile.mkdtemp()

    def crop(w, h, grey=False):
        im = big.crop((0, 0, w, h))
        return im.convert("L") if grey else im

    def pil(name, im, **kw):
        b = io.BytesIO()
        im.save(b, "JPEG", **kw)
        open(path(name), "wb").write(b.getvalue())

    def cj(name, im, *args):
        ppm = os.path.join(tmp, "in.ppm" if im.mode == "RGB" else "in.pgm")
        im.save(ppm)
        subprocess.run(["cjpeg"] + list(args) + ["-outfile", path(name), ppm],
                       check=True)

    pil("J444.JPG", crop(40, 24), quality=85, subsampling=0)
    pil("J422.JPG", crop(40, 24), quality=85, subsampling=1)
    pil("J420.JPG", crop(40, 24), quality=85, subsampling=2)
    cj("J411.JPG", crop(40, 24), "-quality", "80", "-sample", "4x1,1x1,1x1")
    cj("J440.JPG", crop(40, 24), "-quality", "80", "-sample", "1x2,1x1,1x1")
    cj("JGREY.JPG", crop(40, 24, True), "-quality", "85")
    cj("JODD.JPG", crop(37, 23), "-quality", "85", "-sample", "2x2,1x1,1x1")
    cj("JODD411.JPG", crop(37, 23), "-quality", "85", "-sample", "4x1,1x1,1x1")
    cj("JRST.JPG", crop(40, 32), "-quality", "85", "-sample", "2x2,1x1,1x1",
       "-restart", "1B")
    cj("JRSTR.JPG", crop(40, 32), "-quality", "85", "-sample", "2x2,1x1,1x1",
       "-restart", "1")
    cj("JRSTG.JPG", crop(40, 24, True), "-quality", "85", "-restart", "3B")
    cj("JQ16.JPG", crop(40, 24), "-quality", "3")
    cj("JQ100.JPG", crop(40, 24), "-quality", "100", "-sample", "1x1,1x1,1x1")
    cj("JOPT.JPG", crop(40, 24), "-quality", "85", "-optimize")
    cj("JRGB.JPG", crop(40, 24), "-quality", "85", "-rgb")
    cj("JPROG.JPG", crop(40, 32), "-quality", "85", "-progressive")
    cj("JPROGG.JPG", crop(40, 24, True), "-quality", "85", "-progressive")
    cj("JPROG444.JPG", crop(37, 23), "-quality", "85", "-progressive",
       "-sample", "1x1,1x1,1x1")
    cj("JPROGR.JPG", crop(40, 32), "-quality", "85", "-progressive",
       "-restart", "2B")
    bigsrc = src.crop((160, 120, 288, 216))
    cj("JPBIG.JPG", bigsrc, "-quality", "85", "-progressive")
    pil("JBIG.JPG", bigsrc, quality=85, subsampling=2)
    o = big.crop((0, 0, 24, 16))
    for k in range(1, 9):
        ex = Image.Exif()
        ex[0x0112] = k
        pil("JO%d.JPG" % k, o, quality=85, subsampling=2, exif=ex.tobytes())
    ex = Image.Exif()
    ex[0x0112] = 6
    ex[0x010E] = "PiXEL " * 1000        # ImageDescription: 6 KB of EXIF
    pil("JEXBIG.JPG", o, quality=85, subsampling=2, exif=ex.tobytes())
    cj("JARITH.JPG", crop(40, 24), "-quality", "85", "-arithmetic")
    cj("J12BIT.JPG", crop(40, 24), "-quality", "85", "-precision", "12")
    cj("JLOSSL.JPG", crop(40, 24), "-lossless", "1")
    scans = os.path.join(tmp, "scans.txt")
    open(scans, "w").write("0: 0 63 0 0;\n1: 0 63 0 0;\n2: 0 63 0 0;\n")
    cj("JSCANS.JPG", crop(40, 24), "-quality", "85", "-scans", scans)
    print("pixjpeg: wrote %d fixtures in %s" % (len(FIXTURES), OUT))


def pin():
    src = open(__file__).read()
    lines = ["PINS = {"]
    for name, _, _ in FIXTURES:
        lines.append('    "%s": "%s",' % (name, sha(open(path(name), "rb").read())))
    lines.append("}")
    i = src.index("PINS = {")
    j = src.index("}", i) + 1
    open(__file__, "w").write(src[:i] + "\n".join(lines) + src[j:])
    print("pixjpeg: pinned %d" % len(FIXTURES))


def fixtures():
    """[(name, bytes, verdict)] - the committed good and named-refusal set."""
    return [(n, open(path(n), "rb").read(), v) for n, _, v in FIXTURES]


# =============================================================================
# --check (the fast row) and --verify (soak)
# =============================================================================
def check():
    bad = 0
    names = [n for n, _, _ in FIXTURES]
    if sorted(names) != sorted(PINS):
        print("pixjpeg: FAIL the fixture list and the pins disagree")
        bad += 1
    for n in names:
        try:
            d = open(path(n), "rb").read()
        except OSError:
            print("pixjpeg: FAIL %s is missing" % n)
            bad += 1
            continue
        if sha(d) != PINS.get(n):
            print("pixjpeg: FAIL %s is not the pinned file" % n)
            bad += 1
    if bad:
        return 1
    print("pixjpeg: %d fixtures, every one the pinned file" % len(names))
    return 0


def psnr(a, b):
    se = sum((x - y) ** 2 for x, y in zip(a, b))
    return 99.0 if se == 0 else 10 * math.log10(255.0 * 255 * len(a) / se)


def pillow_ref(data, s, grey):
    """Pillow's decode, upright, box-averaged to 1/2^s (floor sizes)."""
    from PIL import Image, ImageOps
    im = ImageOps.exif_transpose(Image.open(io.BytesIO(data)))
    im = im.convert("L" if grey else "RGB")
    w, h = im.size
    n = 1 << s
    nch = 1 if grey else 3
    raw = im.tobytes()
    out = bytearray()
    for y in range(h >> s):
        for x in range(w >> s):
            for c in range(nch):
                t = 0
                for j in range(n):
                    o = ((y * n + j) * w + x * n) * nch + c
                    t += sum(raw[o + i * nch] for i in range(n))
                out.append((t + (n * n >> 1)) // (n * n))
    return bytes(out)


def verify():
    import pixelsim as P
    try:
        import PIL  # noqa: F401
        have_pil = True
    except ImportError:
        have_pil = False
    bad = 0
    worst = 99.0
    for name, data, want in fixtures():
        try:
            p = P.jpeg_header(data, len(data))
            scales = range(p.smin, 4)
        except P.Refused as e:
            scales = [0]
        for s in scales:
            try:
                p = P.decode(data, "JPG", s)
                got = 0
            except P.Refused as e:
                got, p = e.code, None
            if got != want:
                print("pixjpeg: FAIL %s at 1/%d: pixelsim %d (%s), want %d"
                      % (name, 1 << s, got, P.PXD_WORDS[got], want))
                bad += 1
                continue
            if p is None or not have_pil:
                continue
            mine = b"".join(r for _, r in p.rows)
            q = psnr(mine, pillow_ref(data, s, p.rf == P.RF_GREY))
            worst = min(worst, q)
            if q <= 30.0:
                print("pixjpeg: FAIL %s at 1/%d: %.2f dB against Pillow"
                      % (name, 1 << s, q))
                bad += 1
    if bad:
        return 1
    print("pixjpeg: every fixture pixelsim's verdict at every scale; %s"
          % ("the worst against Pillow %.2f dB" % worst if have_pil else
             "Pillow is not installed, so the PSNR half SKIPPED"))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--regen", action="store_true")
    ap.add_argument("--pin", action="store_true")
    a = ap.parse_args()
    if a.regen:
        regen()
        return 0
    if a.pin:
        pin()
        return 0
    if a.verify:
        return verify()
    return check()


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""megapixel - PiXEL's masters, work claims and decoders OVER 64KB under
kern_mega's protected mode (docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md MC7, SPEC.md
106.8 and 50.6.11).

    make && make mega && python3 tests/megapixel.py [--keep] [--shipped]

    --shipped   B: holds the SHIPPED PIXEL.O88 (build/pixel.o88) instead of
                the kern_mega arm: the same checks, its parts' stores
                through CS completed by the kernel one #GP at a time (SPEC.md
                20.18 - correct and slower, which is what the arm is for)

Under kern_mega every claim is ONE LDT selector with a 64KB limit, so the
paragraph arithmetic PiXEL addressed its claims with - a master row at
`[MSEG] + (y x mw >> 7 << 3)`, the work claim cut into sub-buffers by
paragraph offsets, the PNG scanlines at the scratch + PZ_WPARA, the JPEG
progressive store at the scratch + its paragraphs, a ring slot and an
in-memory file's windows at a sum - named descriptors that are somebody
else's. PiXEL reaches a master through a TABLE of OSAPI_SEG SEG_SUB windows
32KB apart (pxrec.inc), and every other sub-buffer through a SEG_SUB of its
own. Boots kern_mega under Bochs at its default memory with B: holding
`make mega`'s PIXEL.O88 (the kern_mega arm) and:

  1. BALLOONS.PNG is opened by double-click (a BMP or a GIF is Paint's by
     association) and Space walks on through CAT.GIF to DEEP.BMP, an 8-bit 600x400 picture (a 234 KB
     master, eight windows), decoded at 1/1, with the VIEW claim (17-31KB)
     above 1MB - PiXEL declares OS88_HIGHMEM, which retired its line in
     tests/megahigh.txt (red without the bit: it sits at 058000h); its master, read through the
     claim's own descriptor, is the file's indices to the byte; the shown
     master's window table (px_mwc) is made of that claim, window k based
     32 KB x k in; and its histogram's tail holds the file's counts;
  2. ROTATE (the tool, a click): a quarter turn into a second master (the
     destination's table, px_mwd, and pe_rot90's 64KB carry, which under
     protected mode is two windows on) - the file turned, to the byte; the
     right button turns it back - the file again;
  3. SAVE AS (Ctrl+S, the card, the dialog) writes OUT.BMP from the master
     row by row (pxwrite's pw_row): the file on B: is the master;
  4. Space walks the folder on through FLOWER.JPG (640x480 baseline),
     HOUSE.PNG (truecolour: the inflate's scanlines past its window) and
     ROOM.JPG (PROGRESSIVE: the coefficient store past 64KB, in 64KB
     windows) - each decodes (the record HAS a picture, the worker idle) and
     the canvas shows a picture;
     - each master pixelsim's (tools/pixelsim.py, pxdecode's reference)
     byte for byte at the scale the guest took;
  5. ROOM.JPG again at 1/8 (Revert with the floor poked, pxdecode's way):
     the progressive store's other arm, a word a block - pixelsim's master;
  6. no `kern_mega CLOSED` and no `kern_mega FAULT` line, at any step.

THE FAILING CASES (docs/WRITING-TESTS.md 1), each tried when this was
written: on the parent commit PiXEL opens NOTHING (its disk claims' 512-byte
test read a selector's bits and refused every one, so the record stays
empty and every step after fails); px_mrow's old `MSEG + (O >> 7 << 3)` put
back fails 1; pe_rot90's carry left unpatched under protected mode fails 2.
"""
import os
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
os.environ["OS88_DEFINES"] = "KERN_MEGA"
import os88bochs  # noqa: E402
import os88sym  # noqa: E402
import pixelsim  # noqa: E402

FAILS = []
BW, BH = 600, 400                   # the fixture: an 8-bit BMP over 64KB
WALK = ("FLOWER.JPG", "HOUSE.PNG", "ROOM.JPG")
CANVAS = (40, 75, 440, 430)         # the canvas, in a shot's pixels
PXR = dict(MW=32, MH=34, SCL=36, PMODE=37, MSEG=38, MKB=40, HAVE=43)
MWT_KEY, MWT_WIN = 0, 6


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)
    return cond


def index(x, y):
    return (x * 7 + y * 3 + ((x * y) >> 5)) & 255


def fixture(path):
    """DEEP.BMP: 8 bits a pixel, a 256-colour palette, bottom-up - every
    index a function of (x, y), so a row out of place or a window aimed
    wrong is a mismatch."""
    stride = (BW + 3) & ~3
    pal = b"".join(struct.pack("<BBBB", (i * 37) & 255, (i * 91) & 255,
                               (i * 151) & 255, 0) for i in range(256))
    rows = []
    for fy in range(BH):
        y = BH - 1 - fy
        rows.append(bytes(index(x, y) for x in range(BW)) +
                    bytes(stride - BW))
    pix = b"".join(rows)
    off = 14 + 40 + 1024
    with open(path, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", off + len(pix), 0, 0, off) +
                struct.pack("<IiiHHIIiiII", 40, BW, BH, 1, 8, 0, len(pix),
                            2835, 2835, 256, 0) + pal + pix)


def picture():
    return [bytes(index(x, y) for x in range(BW)) for y in range(BH)]


def turned_cw(rows):
    """D[y][x] = S[h-1-x][y] (SPEC.md 106.24's quarter turn, clockwise)"""
    h, w = len(rows), len(rows[0])
    return [bytes(rows[h - 1 - x][y] for x in range(h)) for y in range(w)]


def fat12_file(img, name):
    """A file off the ROOT of a FAT12 floppy image, by its 8.3 name."""
    d = open(img, "rb").read()
    bps, spc = struct.unpack_from("<H", d, 11)[0], d[13]
    res, nfat = struct.unpack_from("<H", d, 14)[0], d[16]
    nroot = struct.unpack_from("<H", d, 17)[0]
    spf = struct.unpack_from("<H", d, 22)[0]
    fat = d[res * bps:(res + spf) * bps]
    root = (res + nfat * spf) * bps
    data = root + nroot * 32
    nm, _, ext = name.upper().partition(".")
    key = (nm.ljust(8) + ext.ljust(3)).encode()
    for i in range(root, data, 32):
        e = d[i:i + 32]
        if e[:11] == key:
            c, size = struct.unpack_from("<HI", e, 26)
            out = b""
            while 2 <= c < 0xFF8:
                o = data + (c - 2) * spc * bps
                out += d[o:o + spc * bps]
                w = struct.unpack_from("<H", fat, c * 3 // 2)[0]
                c = w >> 4 if c & 1 else w & 0xFFF
            return out[:size]
    return None


def bmp_rows(b):
    """An 8-bit BMP's rows, top-down, its indices only."""
    off = struct.unpack_from("<I", b, 10)[0]
    w, h = struct.unpack_from("<ii", b, 18)
    bpp = struct.unpack_from("<H", b, 28)[0]
    if bpp != 8:
        return None
    stride = (w + 3) & ~3
    rows = [b[off + r * stride:off + r * stride + w] for r in range(abs(h))]
    return rows[::-1] if h > 0 else rows


class Pixel(object):
    """PiXEL's state, read through its own descriptor (OS88_HIGH)."""

    def __init__(self, box, pkg):
        self.box = box
        self.map = os88sym._parse_map(os.path.join(ROOT, "build",
                                                   "pixel.map"))[0]
        # the map is build/pixel.bin's, and the guest runs `make mega`'s
        # PIXEL.O88 - the same package with the arm's parts: refuse a map
        # of another build rather than read its bss at the wrong offsets
        o88 = open(pkg, "rb").read(12)
        pbin = open(os.path.join(ROOT, "build", "pixel.bin"), "rb").read(12)
        if o88[8:12] != pbin[8:12]:
            sys.exit("megapixel: %s is not build/pixel.bin's package "
                     "(image and bss %r, %r) - `make mega` first"
                     % (pkg, o88[8:12], pbin[8:12]))

    def read(self, label, n, plus=0):
        return self.box.pkg_read("PiXEL", self.map[label] + plus, n)

    def word(self, label, plus=0):
        return struct.unpack("<H", self.read(label, 2, plus))[0]

    def byte(self, label, plus=0):
        return self.read(label, 1, plus)[0]

    def rec(self):
        r = self.read("px_cur", 52)
        return dict((k, r[v] if k in ("SCL", "PMODE", "HAVE") else
                     struct.unpack_from("<H", r, v)[0])
                    for k, v in PXR.items())

    def poke(self, label, data):
        a = self.box.pkg_base("PiXEL") + self.map[label]
        self.box.dbg(["setpmem 0x%x 1 0x%x" % (a + i, b)
                      for i, b in enumerate(data)])

    def name(self):
        return self.read("px_cur", 13).split(b"\0")[0].decode("latin-1")

    def idle(self):
        return (self.byte("px_busy") == 0 and self.byte("px_job") == 0
                and self.byte("px_pcon") == 0)

    def lin(self, seg):
        return self.box.sel_base(seg) if seg & 4 else seg * 16

    def master(self):
        r = self.rec()
        base = self.lin(r["MSEG"])
        n = r["MW"] * r["MH"]
        raw = b""
        while len(raw) < n:
            k = min(8192, n - len(raw))
            raw += self.box.read(base + len(raw), k)
        return r, base, [raw[y * r["MW"]:(y + 1) * r["MW"]]
                         for y in range(r["MH"])]


def main():
    keep = "--keep" in sys.argv
    pkg = (os.path.join(ROOT, "build", "pixel.o88") if "--shipped" in
           sys.argv else os.path.join(ROOT, "build", "megak", "apps",
                                      "pixel.o88"))
    out = os.path.join(ROOT, "build", "megapixel" +
                       ("-ship" if "--shipped" in sys.argv else ""))
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    big = os.path.join(out, "DEEP.BMP")
    fixture(big)
    disk = os.path.join(out, "pics.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "1440", pkg,
                    big] + [os.path.join(ROOT, "apps", "pixel", "samples", s)
                            for s in ("BALLOONS.PNG", "CAT.GIF") + WALK],
                   check=True, stdout=subprocess.DEVNULL)
    t0 = time.time()
    box = os88bochs.Box(out, [], bimg=disk, btype="1_44",
                        display=os.environ.get("OS88_BOCHS_DISPLAY")
                        or os88bochs.take_display())
    px = Pixel(box, pkg)

    def settle(what, secs=600):
        box.ui_done(what, secs=secs)
        box.until(px.idle, what + " (PiXEL idle)", secs, poll=0.5)
        box.ui_done(what, secs=secs)

    def e9bad():
        return [l for l in box.lines() if l.startswith("kern_mega CLOSED")
                or l.startswith("kern_mega FAULT")]

    def canvas(name):
        from PIL import Image
        shot = os.path.join(out, name + ".png")
        box.shot(shot)
        im = Image.open(shot).convert("RGB").crop(CANVAS)
        cols = sorted(im.getcolors(1 << 16) or [(0, None)], reverse=True)
        return 1.0 - cols[0][0] / float(im.size[0] * im.size[1])

    def against(name):
        """the shown master against tools/pixelsim.py's emit() of the same
        file at the scale the guest took (pxdecode's comparison, SPEC.md
        106.13): every decoder part's addressing, byte for byte"""
        r, base, rows = px.master()
        data = open(os.path.join(ROOT, "apps", "pixel", "samples", name),
                    "rb").read()
        p = pixelsim.decode(data, name.rsplit(".", 1)[1], r["SCL"])
        sm, mw, mh, mode, _ = pixelsim.emit(p, r["SCL"])
        got = b"".join(rows)
        bad = [i for i in range(min(len(sm), len(got))) if sm[i] != got[i]]
        want((r["MW"], r["MH"], r["PMODE"]) == (mw, mh, mode) and not bad,
             "%s at 1/%d: the master is pixelsim's, byte for byte (%dx%d "
             "mode %d against %dx%d mode %d, %d bytes differ)"
             % (name, 1 << r["SCL"], r["MW"], r["MH"], r["PMODE"], mw, mh,
                mode, len(bad)))

    exp = picture()
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            print("FAIL: kern_mega never reached protected mode")
            return 1
        box.up()
        box.capture()
        box.dbl(591, 105, "B:'s Disk window")
        print("1. BALLOONS.PNG opened, and Space on to DEEP.BMP", flush=True)
        box.dbl(165, 123, "BALLOONS.PNG")   # (the listing's first row; a
                                            # BMP or a GIF is Paint's)
        box.until(lambda: px.box.pkg_base("PiXEL") is not None, "PiXEL",
                  120, poll=0.5)
        settle("BALLOONS.PNG's decode")
        want(px.name() == "BALLOONS.PNG" and canvas("BALLOONS") > 0.03,
             "BALLOONS.PNG opened in PiXEL (%s) and shown" % px.name())
        box.key("space")
        settle("CAT.GIF's decode")
        want(px.name() == "CAT.GIF" and canvas("CAT") > 0.03,
             "Space: CAT.GIF (%s) and shown" % px.name())
        box.key("space")
        settle("DEEP.BMP's decode")
        want(px.name() == "DEEP.BMP", "Space walked on to DEEP.BMP (%s)"
             % px.name())
        want(not e9bad(), "no CLOSED or FAULT opening DEEP.BMP %s" % e9bad())
        r, base, rows = px.master()
        want((r["MW"], r["MH"], r["SCL"], r["HAVE"]) == (BW, BH, 0, 1),
             "a 1/1 master of %dx%d (%r)" % (BW, BH, r))
        want(r["MSEG"] & 4 != 0, "the master's claim is an LDT selector "
             "(%04X)" % r["MSEG"])
        vsel, wsel = px.word("px_vseg"), px.word("px_wbase")
        vb, wb = px.lin(vsel), px.lin(wsel)
        print("megapixel: view claim %04X at %06X, work claim %04X at %06X"
              % (vsel, vb, wsel, wb), flush=True)
        want(vb >= 0x100000, "the view claim (64KB or less) is above 1MB at "
             "%06X: PiXEL declares OS88_HIGHMEM (tests/megahigh.txt)" % vb)
        bad = [y for y in range(min(BH, len(rows))) if rows[y] != exp[y]]
        want(len(rows) == BH and not bad, "the master is the file's indices "
             "(%d rows, %d wrong, first %s)" % (len(rows), len(bad), bad[:3]))
        if want("px_mwc" in px.map, "PiXEL has its window tables (px_mwc)"):
            mwc = px.read("px_mwc", MWT_WIN + 64)
            key = struct.unpack_from("<H", mwc, MWT_KEY)[0]
            wins = struct.unpack_from("<32H", mwc, MWT_WIN)
            nwin = (r["MKB"] + 31) // 32
            want(key == r["MSEG"], "px_mwc is made of the shown master "
                 "(%04X, %04X)" % (key, r["MSEG"]))
            wb = [px.lin(w) - base for w in wins[:nwin]]
            want(nwin >= 8 and wb == [32768 * k for k in range(nwin)],
                 "its %d windows are 32KB apart in the claim (%s)"
                 % (nwin, wb))
        tail = ((BW * BH + 15) // 16 + 7) // 8 * 8 * 16
        cnt = struct.unpack("<256I", box.read(base + tail, 1024))
        hist = [0] * 256
        for row in exp:
            for v in row:
                hist[v] += 1
        want(list(cnt) == hist, "the histogram's tail holds the file's "
             "counts (pl_hcount through the windows, the tail a SEG_SUB)")

        print("2. Rotate: a quarter turn and back")
        box.key("r")
        box.ui_done("the Rotate tool")
        box.goto(240, 250)
        box.xd("click", 1)
        settle("the clockwise turn")
        want(not e9bad(), "no CLOSED or FAULT turning it %s" % e9bad())
        r, base, rows = px.master()
        cw = turned_cw(exp)
        bad = [y for y in range(min(len(cw), len(rows))) if rows[y] != cw[y]]
        want((r["MW"], r["MH"]) == (BH, BW) and not bad,
             "turned clockwise: %dx%d, %d rows wrong (first %s)"
             % (r["MW"], r["MH"], len(bad), bad[:3]))
        box.goto(240, 250)
        box.xd("click", 3)
        settle("the anticlockwise turn")
        want(not e9bad(), "no CLOSED or FAULT turning it back %s" % e9bad())
        r, base, rows = px.master()
        bad = [y for y in range(min(BH, len(rows))) if rows[y] != exp[y]]
        want((r["MW"], r["MH"]) == (BW, BH) and not bad,
             "turned back: %dx%d, %d rows wrong" % (r["MW"], r["MH"],
                                                    len(bad)))

        print("3. Save As OUT.BMP")
        box.key("ctrl+s")
        box.until(lambda: px.byte("px_pcon") == 6, "the Save As card", 60)
        box.ui_done("the card")
        box.key("Return")
        box.ui_done("the card's OK", secs=120)
        time.sleep(2)
        box.ui_done("the dialog", secs=120)
        box.key(*(["BackSpace"] * 12))
        box.type("OUT.BMP")
        box.ui_done("typed")
        box.key("Return")
        settle("the save", 600)
        want(not e9bad(), "no CLOSED or FAULT saving %s" % e9bad())

        print("4. Space through %s" % ", ".join(WALK), flush=True)
        seen = []                       # (from OUT.BMP, which the save
        for _ in range(len(WALK) + 6):  # made the picture: the folder's
            if all(w in seen for w in WALK):   # order wraps round to them)
                break
            box.key("space")
            settle("the next picture", 900)
            s = px.name()
            seen.append(s)
            if s not in WALK:
                continue
            r = px.rec()
            pic = canvas(s)
            want(not e9bad() and r["HAVE"] == 1 and pic > 0.03,
                 "%s: decoded at 1/%d, %dx%d, the canvas %.0f%% picture %s"
                 % (s, 1 << r["SCL"], r["MW"], r["MH"], 100 * pic, e9bad()))
            against(s)
        want(all(w in seen for w in WALK), "the walk reached %s (%s)"
             % (", ".join(WALK), seen))

        print("5. ROOM.JPG again at 1/8 (Revert, the floor poked)",
              flush=True)
        px.poke("px_cur", b"ROOM.JPG".ljust(13, b"\0"))
        px.poke("px_zreq", b"\x03")
        box.key("ctrl+r")
        box.until(lambda: px.byte("px_busy") or px.byte("px_job")
                  or px.rec()["SCL"] == 3, "the re-open to start", 60,
                  poll=0.2)
        settle("ROOM.JPG at 1/8", 900)
        r = px.rec()
        want(not e9bad() and px.name() == "ROOM.JPG" and r["SCL"] == 3,
             "ROOM.JPG re-opened at 1/8 (%s at 1/%d) %s"
             % (px.name(), 1 << r["SCL"], e9bad()))
        against("ROOM.JPG")
    except RuntimeError as e:
        box.shot(os.path.join(out, "stopped.png"))
        want(False, "the run stopped: %s (%s)" % (e, [i[1] for i in
                                                     box.instances()]))
    finally:
        box.stop()
    got = fat12_file(os.path.join(out, "b.img"), "OUT.BMP")
    rows = bmp_rows(got) if got else None
    want(rows == exp, "OUT.BMP on B: is the master to the byte (%s)" %
         ("missing" if got is None else "%d bytes" % len(got)))
    want(not e9bad(), "no CLOSED and no FAULT on E9")
    print("megapixel: %.0fs under Bochs" % (time.time() - t0))
    for f in FAILS:
        print("FAIL: " + f)
    if not FAILS and not keep:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())

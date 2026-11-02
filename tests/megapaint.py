#!/usr/bin/env python3
"""megapaint - a Paint canvas OVER 64KB under kern_mega's protected mode
(docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md MC7, SPEC.md 42 and 50.6.11).

    make && make mega && python3 tests/megapaint.py [--keep]

Under kern_mega every claim is an LDT SELECTOR with a 64KB limit, so the row
table Paint built for a canvas over 64KB - a segment per 128-byte granule,
`[pt_base] + n` - named descriptors that do not exist. pt_layout cuts such a
canvas into SEG_SUB windows 32KB apart instead (and the undo image the same
way, through windows of its own), pt_orowset and pt_srowset read through the
same windows, pt_cvmove looks every row up rather than stepping granules,
and Save Bmp writes the whole canvas in ONE OSAPI_FILE_WRITE, which the file
layer carries past the selector's 64KB by stepping its window (dskw_hicarry,
KERN-MEGA-MEMORY-PLAN MC7). Boots kern_mega under Bochs at 16MB with
a 580x380 four-bit BMP on C: (111KB, so the staging claim is over 64KB too)
and B: the kern_mega apps disk, and:

  5. OPENS the BMP by association (B:'s PAINT.O88): every sampled pixel of
     the canvas is the file's - pt_srowset through windows, pt_layout's;
  1. MAXIMIZES: the canvas grows to 590x390 ([pt_cvparas] > 1000h), laid
     out in more than one window, every pt_cwin entry an LDT selector, and
     the picture came with it (pt_cvmove from the old claim);
  2. two STROKES, down the whole height and across the TOP rows - the top of
     the picture is the END of the file, the last window - are black in the
     canvas, read through the claim's own descriptor;
  3. UNDO takes the second back out to the byte, REDO puts it back (the undo
     image's windows, pt_uwinof);
  4. RESTORE then MAXIMIZE - an in-place shrink and a grow - keep the
     picture pixel for pixel;
  6. no `kern_mega CLOSED` and no `kern_mega FAULT` line;
  7. SAVE BMP: the file on C: (read off the image afterwards) is the canvas
     to the byte, all 115,440 bytes of pixels.

Broken on purpose (docs/WRITING-TESTS.md 1), all three seen when the row was
written: the parent commit, whose row table is granules on a selector, has
Paint closed as the BMP opens (`kern_mega CLOSED PAINT 0D`); pt_uwinof
answering the canvas's own window fails 3 (the undo copies the canvas onto
itself); and the kernel without dskw_hicarry fails 7, the file DIFFERENT
from byte 65,536 - the canvas's DIB header written again there, because the
file layer moved only the offset of a selector buffer and wrapped at its
64KB limit (seen red on the kernel before the fix).
"""
import os
import shutil
import struct
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
os.environ["OS88_DEFINES"] = "KERN_MEGA"
import os88bochs  # noqa: E402
from os88fixture import need  # noqa: E402
import dispapps  # noqa: E402

FAILS = []
BW, BH = 580, 380                   # the fixture: a 4bpp BMP over 64KB


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)
    return cond


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def fixture(path):
    """A 4bpp BMP, every pixel derived from (x, y) so a row out of place or a
    window aimed wrong shows as a mismatch: colour (x // 7 + y // 5) & 15."""
    stride = ((BW * 4 + 31) // 32) * 4
    pal = b"".join(struct.pack("<BBBB", b, g, r, 0) for r, g, b in (
        (0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0),
        (170, 0, 170), (170, 85, 0), (170, 170, 170), (85, 85, 85),
        (85, 85, 255), (85, 255, 85), (85, 255, 255), (255, 85, 85),
        (255, 85, 255), (255, 255, 85), (255, 255, 255)))
    rows = []
    for fy in range(BH):
        y = BH - 1 - fy             # bottom-up
        r = bytearray(stride)
        for x in range(BW):
            c = (x // 7 + y // 5) & 15
            if x & 1:
                r[x >> 1] |= c
            else:
                r[x >> 1] |= c << 4
        rows.append(bytes(r))
    pix = b"".join(rows)
    off = 14 + 40 + 64
    hdr = b"BM" + struct.pack("<IHHI", off + len(pix), 0, 0, off)
    dib = struct.pack("<IiiHHIIiiII", 40, BW, BH, 1, 4, 0, len(pix),
                      2835, 2835, 16, 0)
    with open(path, "wb") as f:
        f.write(hdr + dib + pal + pix)
    return os.path.getsize(path)


class Paint(object):
    """Paint's state, read through its own LDT descriptor (OS88_HIGH)."""

    def __init__(self, box, name="PAINT"):
        self.box, self.name = box, name
        self.isz = dispapps.img_size("paint")
        self.pm = dispapps._map("paint")

    def off(self, label):
        return self.pm[label]

    def word(self, label, base=None):
        b = base if base is not None else self.box.pkg_base(self.name)
        return u16(self.box.read(b + self.off(label), 2))

    def words(self, labels):
        b = self.box.pkg_base(self.name)
        got = self.box.reads([(b + self.off(l), 2) for l in labels])
        return dict((l, u16(g)) for l, g in zip(labels, got))

    def canvas_rows(self):
        """{y: bytes of row y} for the whole picture, read through the
        canvas claim's own descriptor (a linear base, then flat bytes)."""
        w = self.words(["pt_base", "pt_ch", "pt_stride", "pt_hdrsz"])
        lin = self.box.sel_base(w["pt_base"])
        n = w["pt_ch"] * w["pt_stride"]
        raw = b""
        while len(raw) < n:
            k = min(4096, n - len(raw))
            raw += self.box.read(lin + w["pt_hdrsz"] + len(raw), k)
        st, ch = w["pt_stride"], w["pt_ch"]
        return dict((y, raw[(ch - 1 - y) * st:(ch - y) * st])
                    for y in range(ch)), w


def colour(row, x, planar, bpr):
    """The colour of pixel x in one canvas row, packed or four planes."""
    if planar:
        bit, c = 0x80 >> (x & 7), 0
        for p in range(4):
            if row[p * bpr + (x >> 3)] & bit:
                c |= 1 << p
        return c
    b = row[x >> 1]
    return (b >> 4) if not x & 1 else b & 15


class _Guest(object):
    """os88ui's reader over a Bochs: a segment that is a SELECTOR is read
    through its descriptor (a package's menu strings live in its region)."""

    def __init__(self, box):
        self.box = box

    def read(self, a, n):
        return self.box.read(a, n)

    def sym(self, name):
        return self.box.sym(name)

    def readseg(self, seg, off, n):
        base = self.box.sel_base(seg) if seg & 4 else seg * 16
        return self.box.read(base + off, n)


def menu_pick(box, title, item):
    """os88ui.menu_pick's gesture on a Bochs: the bar off menu_bar[], the
    item confirmed by `menu_sel` before the release."""
    import os88geom as g
    import os88ui
    ui = os88ui.UI(_Guest(box), mouse=object(), verbose=False)
    cells = ui.menus()
    cell = [i for i, c in enumerate(cells) if c[0].upper() == title.upper()]
    if len(cell) != 1:
        raise RuntimeError("no %r on the bar %r" % (title, [c[0] for c in
                                                           cells]))
    t, x0, x1, items = cells[cell[0]]
    k = [i for i, (it, _) in enumerate(items) if it.upper() == item.upper()]
    if len(k) != 1:
        raise RuntimeError("no %r in %s: %r" % (item, t, items))
    box.goto((x0 + x1) // 2, g.MBAR_H // 2)
    box.xd("mousedown", 1)
    box.guest_sleep(0.6)                # GUEST seconds (Box.guest_sleep)
    y1 = u16(box.read(box.sym("menu_y1"), 2))
    mx = u16(box.read(box.sym("menu_x1"), 2))
    box.goto(mx + 8, y1 + 1 + k[0] * g.MENU_ITEM_H + g.MENU_ITEM_H // 2)
    sel = u16(box.read(box.sym("menu_sel"), 2))
    box.xd("mouseup", 1)
    if sel != k[0]:
        raise RuntimeError("%s -> %s: menu_sel %d, not %d" % (t, item, sel,
                                                             k[0]))
    return box.ui_done("%s -> %s" % (t, item), secs=180)


def fat_file(img, path):
    """A file off the FIRST PARTITION of a hard-disk image, FAT12 or FAT16,
    by a path of 8.3 names - enough of a reader for one saved picture."""
    d = open(img, "rb").read()
    lba = struct.unpack_from("<I", d, 0x1BE + 8)[0]
    v = d[lba * 512:]
    bps, spc = struct.unpack_from("<H", v, 11)[0], v[13]
    res, nfat = struct.unpack_from("<H", v, 14)[0], v[16]
    nroot = struct.unpack_from("<H", v, 17)[0]
    tot = struct.unpack_from("<H", v, 19)[0] or struct.unpack_from(
        "<I", v, 32)[0]
    spf = struct.unpack_from("<H", v, 22)[0]
    fat = v[res * bps:(res + spf) * bps]
    root = (res + nfat * spf) * bps
    data = root + nroot * 32
    ncl = (tot - data // bps) // spc
    f16 = ncl >= 4085

    def nxt(c):
        if f16:
            return struct.unpack_from("<H", fat, c * 2)[0]
        w = struct.unpack_from("<H", fat, c * 3 // 2)[0]
        return w >> 4 if c & 1 else w & 0xFFF

    def chain(c):
        out = b""
        while 2 <= c < (0xFFF8 if f16 else 0xFF8):
            o = data + (c - 2) * spc * bps
            out += v[o:o + spc * bps]
            c = nxt(c)
        return out
    entries = v[root:data]
    for part in path.split("/"):
        nm, _, ext = part.upper().partition(".")
        key = (nm.ljust(8) + ext.ljust(3)).encode()
        for i in range(0, len(entries), 32):
            e = entries[i:i + 32]
            if e[:11] == key:
                c = struct.unpack_from("<H", e, 26)[0]
                size = struct.unpack_from("<I", e, 28)[0]
                body = chain(c)
                entries = body if e[11] & 0x10 else body[:size]
                break
        else:
            raise RuntimeError("no %s on C:" % path)
    return entries


def expect(x, y):
    """The fixture's colour at picture (x, y)."""
    return (x // 7 + y // 5) & 15


def title(box, pt):
    """Where to double-click Paint's title bar (its window record)."""
    import os88geom as g
    rec = box.read(g.KERNEL_SEG * 16 + pt.word("pt_win"), 8)
    return u16(rec, g.W_X) + 60, u16(rec, g.W_Y) + 7


def picture(pt, rows, w, planar, xs, ys):
    """How many of the sampled pixels are NOT the fixture's."""
    bpr = w["pt_stride"] // 4 if planar else 0
    return sum(colour(rows[y], x, planar, bpr) != expect(x, y)
               for y in ys for x in xs)



def firstdiff(a, b):
    """the first offset where A and B differ (the shorter's end if none)"""
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i
    return min(len(a), len(b))

def main():
    keep = "--keep" in sys.argv
    for p in ("build/mega.img", "build/megaapps.img", "build/megarun.o88",
              "build/mbr.bin", "build/paint.o88"):
        need(p)                 # (build/megak/kernel.map is mega.img's)
    out = tempfile.mkdtemp(prefix="megapaint-")
    bmp = os.path.join(out, "BIGPIC.BMP")
    size = fixture(bmp)
    print("megapaint: fixture %dx%d, %d bytes" % (BW, BH, size), flush=True)
    box = os88bochs.Box(out, [],
                        bimg=os.path.join(ROOT, "build", "megaapps.img"),
                        btype="1_44", put=("PICS:" + bmp,))
    t0 = time.time()
    saved = None
    try:
        box.up()
        box.capture()
        # --- 5 first: OPEN THE BMP by association (B:'s PAINT.O88) --------
        box.dbl(591, 160, "C:'s Disk window")
        box.dbl(150, 123, "PICS's listing")
        box.goto(160, 139)
        box.dblclick()
        box.ui_done("Paint opening BIGPIC.BMP", secs=180)
        box.shot(os.path.join(out, "opened.png"))
        pt = Paint(box)
        want(box.pkg_base("PAINT") is not None, "Paint is running")
        rows, w = pt.canvas_rows()
        planar = box.read(box.pkg_base("PAINT") + pt.off("pt_planar"), 1)[0]
        print("   opened %dx%d stride %d, %d paragraphs, %s" % (
            pt.word("pt_cw"), w["pt_ch"], w["pt_stride"],
            pt.word("pt_cvparas"), "planar" if planar else "packed"),
            flush=True)
        xs, ys = range(0, BW, 3), range(0, BH)
        bad = picture(pt, rows, w, planar, xs, ys)
        want(pt.word("pt_cw") == BW and w["pt_ch"] == BH and bad == 0,
             "5. BIGPIC.BMP (%d bytes, staged over 64KB) is in the canvas: "
             "%d of %d sampled pixels wrong" % (size, bad, len(xs) * len(ys)))
        # --- 1. MAXIMIZE ---------------------------------------------------
        box.dbl(*title(box, pt), what="the maximize")
        box.shot(os.path.join(out, "maximized.png"))
        w = pt.words(["pt_cw", "pt_ch", "pt_cvparas", "pt_cx0", "pt_cy0",
                      "pt_stride", "pt_nwin"])
        planar = box.read(box.pkg_base("PAINT") + pt.off("pt_planar"), 1)[0]
        cwin = box.read(box.pkg_base("PAINT") + pt.off("pt_cwin"),
                        2 * max(1, w["pt_nwin"]))
        sels = [u16(cwin, 2 * i) for i in range(w["pt_nwin"])]
        print("   maximized %dx%d stride %d, %d paragraphs, %d windows %s" % (
            w["pt_cw"], w["pt_ch"], w["pt_stride"], w["pt_cvparas"],
            w["pt_nwin"], " ".join("%04X" % s for s in sels)), flush=True)
        want(w["pt_cvparas"] > 0x1000 and w["pt_nwin"] > 1 and
             all(s & 4 for s in sels),
             "1. the canvas is over 64KB (%d paragraphs) and laid out in %d "
             "LDT windows" % (w["pt_cvparas"], w["pt_nwin"]))
        rows, w2 = pt.canvas_rows()
        bad = picture(pt, rows, w2, planar, xs, ys)
        want(bad == 0, "1. ...and the picture came with it (pt_cvmove, "
             "pt_orowset): %d sampled pixels wrong" % bad)
        cw, ch, cx0, cy0 = w["pt_cw"], w["pt_ch"], w["pt_cx0"], w["pt_cy0"]
        # --- 2. TWO STROKES: down the whole height, across the top ---------
        before = rows
        vx, hy = cw // 3, 6
        box.goto(cx0 + vx, cy0 + 2)
        box.xd("mousedown", 1)
        for y in range(2, ch - 2, 40):
            box.goto(cx0 + vx, cy0 + y)
        box.goto(cx0 + vx, cy0 + ch - 3)
        box.xd("mouseup", 1)
        box.ui_done("the vertical stroke")
        mid = pt.canvas_rows()[0]
        box.goto(cx0 + 10, cy0 + hy)
        box.xd("mousedown", 1)
        for x in range(10, cw - 10, 60):
            box.goto(cx0 + x, cy0 + hy)
        box.goto(cx0 + cw - 11, cy0 + hy)
        box.xd("mouseup", 1)
        box.ui_done("the horizontal stroke")
        rows, w3 = pt.canvas_rows()
        bpr = w3["pt_stride"] // 4 if planar else 0
        vy, hx = range(4, ch - 5), range(14, cw - 14)
        v = sum(colour(rows[y], vx, planar, bpr) == 0 for y in vy)
        h = sum(colour(rows[hy], x, planar, bpr) == 0 for x in hx)
        want(v == len(vy) and h == len(hx),
             "2. the strokes are in the canvas, black: %d of %d rows down, %d "
             "of %d columns across the top" % (v, len(vy), h, len(hx)))
        box.shot(os.path.join(out, "stroke.png"))
        after = rows
        # --- 3. UNDO, REDO: the canvas back to the byte ---------------------
        box.key("ctrl+z")
        box.ui_done("the undo")
        box.shot(os.path.join(out, "undo.png"))
        rows = pt.canvas_rows()[0]
        diff = sum(rows[y] != mid[y] for y in rows)
        want(diff == 0, "3. Ctrl+Z takes the horizontal stroke back out: %d "
             "rows differ from before it" % diff)
        box.key("ctrl+z")
        box.ui_done("the redo")
        rows = pt.canvas_rows()[0]
        diff = sum(rows[y] != after[y] for y in rows)
        want(diff == 0, "3. ...and Ctrl+Z again puts it back: %d rows differ"
             % diff)
        # --- 4. RESTORE, MAXIMIZE: an in-place shrink, then a grow ---------
        box.dbl(*title(box, pt), what="the restore")
        rows, w4 = pt.canvas_rows()
        cw4, ch4 = pt.word("pt_cw"), w4["pt_ch"]
        bpr = w4["pt_stride"] // 4 if planar else 0
        kept = [(x, y) for y in range(min(ch, ch4)) for x in range(0, min(
            cw, cw4), 5)]
        bad = sum(colour(rows[y], x, planar, bpr) !=
                  colour(after[y], x, planar, w3["pt_stride"] // 4
                         if planar else 0) for x, y in kept)
        want(ch4 < ch and bad == 0, "4. the restore (%dx%d, from %dx%d) kept "
             "the picture: %d of %d sampled pixels wrong" % (
                 cw4, ch4, cw, ch, bad, len(kept)))
        box.dbl(*title(box, pt), what="the maximize")
        rows, w5 = pt.canvas_rows()
        bpr = w5["pt_stride"] // 4 if planar else 0
        bad = sum(colour(rows[y], x, planar, bpr) !=
                  colour(after[y], x, planar, w3["pt_stride"] // 4
                         if planar else 0) for x, y in kept)
        want(w5["pt_ch"] == ch and bad == 0, "4. ...and the maximize after it "
             "(%dx%d): %d of %d sampled pixels wrong" % (
                 pt.word("pt_cw"), w5["pt_ch"], bad, len(kept)))
        box.shot(os.path.join(out, "final.png"))
        del before
        # --- 7. SAVE: one OSAPI_FILE_WRITE of the whole canvas, over 64KB,
        # out of a selector (the kernel splits it at the descriptor's limit)
        menu_pick(box, "File", "Save Bmp")
        saved_rows, ws = pt.canvas_rows()
        saved = b"".join(saved_rows[y] for y in
                         range(ws["pt_ch"] - 1, -1, -1))
        saved_dims = (pt.word("pt_cw"), ws["pt_ch"], ws["pt_hdrsz"])
    except Exception as e:      # noqa: BLE001 - the machine stopped
        want(False, "the run: %s" % e)
    finally:
        bad = box.faults()
        want(not bad, "6. no fault%s" % "".join(" | " + l for l in bad[:3]))
        box.stop()
    if saved is not None:
        try:
            f = fat_file(os.path.join(out, "c.img"), "PICS/BIGPIC.BMP")
            w_, h_ = struct.unpack_from("<ii", f, 18)
            off = struct.unpack_from("<I", f, 10)[0]
            want((w_, h_) == saved_dims[:2] and off == saved_dims[2] and
                 f[off:] == saved, "7. Save Bmp wrote the canvas: %dx%d, %d "
                 "bytes of pixels, %s" % (w_, h_, len(f) - off,
                                          "identical" if f[off:] == saved
                                          else "DIFFERENT from file byte %d"
                                          % (off + firstdiff(f[off:], saved))))
        except Exception as e:      # noqa: BLE001
            want(False, "7. the saved file: %s" % e)
        print("megapaint: %.0fs under Bochs" % (time.time() - t0))
        if keep:
            print("megapaint: the boot is in " + out)
        else:
            shutil.rmtree(out, ignore_errors=True)
    for f in FAILS:
        print("FAIL megapaint: " + f)
    print("megapaint: %s" % ("FAILED" if FAILS else "PASS"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""os88pix: build a .PIX picture archive for Frotz (SPEC.md 61.7).

    python3 tools/os88pix.py -o OUT.PIX 1=cover.png 5=map.jpg
    python3 tools/os88pix.py -o OUT.PIX --blorb Bronze.zblorb
    python3 tools/os88pix.py --dump OUT.PIX

`N=FILE` puts FILE in the archive as picture number N; `--blorb` takes every
picture resource out of an IFF Blorb and numbers them the way the Blorb's own
resource index does, which is what a v6 story's `@draw_picture` will ask for.

**apps/frotz/zpic.inc defines this format and this tool matches it, never the
other way round.** The guest does no decoding at all: a picture is drawn by
one `OSAPI_GFX_BLIT4` call straight out of the archive, because
`@draw_picture` runs on the worker task and a worker may not touch a file slot
or `OSAPI_MEM_*` (SPEC.md 20.6). Everything expensive therefore happens here.

Three consequences shape the output and none of them is negotiable:

  - **Packed 4bpp, two pixels per byte, high nibble leftmost**, in os8088's
    fixed 16-colour palette. There is no per-picture palette and no
    transparency, because the blitter writes every pixel it is given.
  - **Every pixel block starts on a 16-byte boundary**, so the guest addresses
    it as `claim_segment + (offset >> 4)` with offset 0.
  - **stride * height must be under 64KB.** The block is one segment at offset
    zero, so a bigger picture would wrap SI mid-draw. Oversized pictures are
    refused here rather than emitted and refused there.

Reading the source images needs no third-party library: PNG is decoded from
zlib (which is stdlib) and anything else is handed to macOS's `sips` to become
a PNG first. That keeps the toolchain to what tools/setup-macos.sh already
installs.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

MAGIC = b"O8PIX"
FMT_VER = 1
ENT = 16                  # directory entry size, and zpic.inc refuses anything else
HDR = 16
MAXPIC = 255
ALIGN = 16
BLOCK_MAX = 0xFFFF        # stride * height must fit one segment

# os8088's fixed 16-colour palette (standard EGA/VGA indices, apps/os88api.inc).
PALETTE = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00), (0x00, 0xAA, 0xAA),
    (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA), (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA),
    (0x55, 0x55, 0x55), (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55), (0xFF, 0xFF, 0xFF),
]


def fail(msg):
    print(f"os88pix: error: {msg}", file=sys.stderr)
    sys.exit(1)


# --- reading a source image --------------------------------------------------

def png_decode(data):
    """Minimal PNG reader: 8-bit RGB/RGBA/grey/palette, non-interlaced.

    Enough for what a Blorb or a cover scan actually contains, and it refuses
    everything else rather than guessing - a wrong guess here is a picture
    that draws as noise, which is exactly the failure SPEC.md 47 is about.
    """
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        fail("not a PNG")
    off, idat, plte, trns = 8, [], None, None
    w = h = depth = ctype = interlace = None
    while off + 8 <= len(data):
        ln = struct.unpack(">I", data[off:off + 4])[0]
        typ = data[off + 4:off + 8]
        body = data[off + 8:off + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE":
            plte = body
        elif typ == b"tRNS":
            trns = body
        elif typ == b"IDAT":
            idat.append(body)
        elif typ == b"IEND":
            break
        off += 12 + ln
    if w is None:
        fail("PNG has no IHDR")
    if interlace:
        fail("interlaced PNG is not supported; save it non-interlaced")
    if depth != 8:
        fail(f"PNG bit depth {depth} is not supported; 8 is")
    nch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if nch is None:
        fail(f"PNG colour type {ctype} is not supported")

    raw = zlib.decompress(b"".join(idat))
    stride = w * nch
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for _ in range(h):
        ft = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        for i in range(stride):
            a = line[i - nch] if i >= nch else 0
            b = prev[i]
            c = prev[i - nch] if i >= nch else 0
            if ft == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ft == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ft == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
            elif ft != 0:
                fail(f"PNG filter type {ft} is not valid")
        out += line
        prev = line

    # Normalise to RGB triples.
    px = []
    for i in range(w * h):
        s = out[i * nch:(i + 1) * nch]
        if ctype == 0:
            px.append((s[0], s[0], s[0]))
        elif ctype == 4:
            px.append((s[0], s[0], s[0]))
        elif ctype == 2:
            px.append((s[0], s[1], s[2]))
        elif ctype == 6:
            px.append((s[0], s[1], s[2]))
        else:
            if plte is None:
                fail("paletted PNG with no PLTE")
            j = s[0] * 3
            px.append((plte[j], plte[j + 1], plte[j + 2]))
    return w, h, px


def load_image(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return png_decode(data)
    # Anything else goes through sips on macOS, or Pillow where sips is not on
    # PATH (e.g. Linux). The temp PNG is deleted, and neither decoder is
    # non-deterministic - but they are not identical to each other. Flat
    # colour, paletted GIF and TIFF quantise to the same bytes either way; a
    # photographic JPEG does not, because the chroma upsampling difference
    # survives the 16-colour reduction. The one archive that reaches is
    # BRONZE.PIX, whose source is Bronze.zblorb's JPEG cover, and it is
    # therefore reproducible per host rather than across hosts. Nothing
    # compares those bytes today: it lands only on build/zork2.img, built on
    # demand from stories that are never committed.
    if shutil.which("sips"):
        with tempfile.TemporaryDirectory() as td:
            tmp = os.path.join(td, "conv.png")
            r = subprocess.run(["sips", "-s", "format", "png", path, "--out", tmp],
                               capture_output=True)
            if r.returncode != 0 or not os.path.exists(tmp):
                fail(f"cannot read {path}: not a PNG, and sips could not convert it\n"
                     f"  {r.stderr.decode(errors='replace').strip()}")
            with open(tmp, "rb") as fh:
                return png_decode(fh.read())
    try:
        from PIL import Image
    except ImportError:
        fail(f"cannot read {path}: not a PNG, and neither sips nor Pillow "
             f"is available to convert it")
    with tempfile.TemporaryDirectory() as td:
        tmp = os.path.join(td, "conv.png")
        Image.open(path).convert("RGB").save(tmp, "PNG")
        with open(tmp, "rb") as fh:
            return png_decode(fh.read())


# --- quantising to the 16-colour palette -------------------------------------

def quantise(px, w, h, maxw, maxh):
    """Box-filter down to fit, then nearest-colour in the fixed palette.

    Ordered dithering was tried and left out: at 4bpp over a 16-colour palette
    it turns a photographic cover into visible crosshatch on a 640x480 screen,
    and on the two 1bpp adapters (SPEC.md 39.4) the whole thing rounds to a
    dither anyway. Flat nearest-colour reads better on all three.
    """
    sx = max(1, -(-w // maxw)) if maxw else 1
    sy = max(1, -(-h // maxh)) if maxh else 1
    s = max(sx, sy)
    nw, nh = max(1, w // s), max(1, h // s)
    out = []
    for y in range(nh):
        row = []
        for x in range(nw):
            r = g = b = n = 0
            for yy in range(y * s, min((y + 1) * s, h)):
                for xx in range(x * s, min((x + 1) * s, w)):
                    p = px[yy * w + xx]
                    r += p[0]; g += p[1]; b += p[2]; n += 1
            r, g, b = r // n, g // n, b // n
            best, bd = 0, 1 << 30
            for i, (pr, pg, pb) in enumerate(PALETTE):
                d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
                if d < bd:
                    best, bd = i, d
            row.append(best)
        out.append(row)
    return nw, nh, out


def pack4(rows, w, h):
    """Two pixels per byte, HIGH NIBBLE LEFTMOST - what OSAPI_GFX_BLIT4 reads."""
    stride = (w + 1) // 2
    buf = bytearray(stride * h)
    for y in range(h):
        base = y * stride
        row = rows[y]
        for x in range(0, w, 2):
            hi = row[x] & 15
            lo = row[x + 1] & 15 if x + 1 < w else 0
            buf[base + (x >> 1)] = (hi << 4) | lo
    return stride, bytes(buf)


# --- Blorb -------------------------------------------------------------------

def blorb_pictures(path):
    """Every Pict resource, numbered as the Blorb's own index numbers them."""
    with open(path, "rb") as fh:
        d = fh.read()
    if d[:4] != b"FORM" or d[8:12] != b"IFRS":
        fail(f"{path}: not a Blorb")
    chunks, off = {}, 12
    ridx = None
    while off + 8 <= len(d):
        cid = d[off:off + 4]
        ln = struct.unpack(">I", d[off + 4:off + 8])[0]
        body = d[off + 8:off + 8 + ln]
        if cid == b"RIdx":
            ridx = body
        chunks[off + 8] = (cid, body)
        off += 8 + ln + (ln & 1)
    if ridx is None:
        fail(f"{path}: no resource index")
    n = struct.unpack(">I", ridx[:4])[0]
    out = []
    for i in range(n):
        usage, num, start = struct.unpack(">4sII", ridx[4 + i * 12:16 + i * 12])
        if usage != b"Pict":
            continue
        cid = d[start:start + 4]
        ln = struct.unpack(">I", d[start + 4:start + 8])[0]
        out.append((num, cid, d[start + 8:start + 8 + ln]))
    return out


# --- the archive -------------------------------------------------------------

def build(entries, out_path, release, maxw, maxh):
    """entries: list of (number, w, h, stride, pixel bytes)."""
    if not entries:
        fail("no pictures to write")
    if len(entries) > MAXPIC:
        fail(f"{len(entries)} pictures; the format holds {MAXPIC}")
    entries.sort(key=lambda e: e[0])

    dir_off = HDR
    body_off = dir_off + ENT * len(entries)
    body_off = (body_off + ALIGN - 1) & ~(ALIGN - 1)

    blocks, offs = [], []
    cur = body_off
    for (_, _, h, stride, data) in entries:
        if stride * h > BLOCK_MAX:
            fail(f"a {stride}x{h} block is {stride * h} bytes; one segment holds "
                 f"{BLOCK_MAX}. Scale it down with --max-width/--max-height.")
        offs.append(cur)
        blocks.append(data)
        cur = (cur + len(data) + ALIGN - 1) & ~(ALIGN - 1)

    buf = bytearray(cur)
    buf[0:5] = MAGIC
    buf[5] = FMT_VER
    struct.pack_into("<HHH", buf, 6, len(entries), release, ENT)
    for i, (num, w, h, stride, data) in enumerate(entries):
        struct.pack_into("<HHHHI", buf, dir_off + i * ENT, num, w, h, stride, offs[i])
    for o, b in zip(offs, blocks):
        buf[o:o + len(b)] = b

    with open(out_path, "wb") as fh:
        fh.write(buf)
    print(f"os88pix: {out_path} - {len(entries)} picture(s), release {release}, "
          f"{len(buf)} bytes")
    for (num, w, h, stride, _), o in zip(entries, offs):
        print(f"  #{num:<4} {w:>4}x{h:<4} stride {stride:<4} at +{o}")


def dump(path):
    with open(path, "rb") as fh:
        d = fh.read()
    if d[:5] != MAGIC:
        fail(f"{path}: not a .PIX (want {MAGIC!r})")
    ver = d[5]
    n, release, ent = struct.unpack("<HHH", d[6:12])
    print(f"{path}: version {ver}, {n} picture(s), release {release}, entry {ent}")
    if ver != FMT_VER or ent != ENT:
        fail("this tool writes version 1 with 16-byte entries; refusing to guess")
    for i in range(n):
        # unpack_from, not a slice: the entry is ENT (16) bytes and the fields
        # are the first 12 of them - the last 4 are the reserved word.
        num, w, h, stride, off = struct.unpack_from("<HHHHI", d, HDR + i * ENT)
        ok = "ok" if off % ALIGN == 0 and off + stride * h <= len(d) else "BAD"
        print(f"  #{num:<4} {w:>4}x{h:<4} stride {stride:<4} at +{off:<8} {ok}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--output", metavar="OUT.PIX")
    ap.add_argument("--blorb", metavar="FILE", help="take every Pict resource from a Blorb")
    ap.add_argument("--release", type=int, default=1,
                    help="release number @picture_data reports (default 1)")
    ap.add_argument("--max-width", type=int, default=320)
    ap.add_argument("--max-height", type=int, default=200)
    ap.add_argument("--dump", metavar="FILE", help="describe an existing archive and exit")
    ap.add_argument("pics", nargs="*", metavar="N=FILE")
    args = ap.parse_args()

    if args.dump:
        dump(args.dump)
        return
    if not args.output:
        fail("-o is required")

    sources = []
    if args.blorb:
        with tempfile.TemporaryDirectory() as td:
            for num, cid, body in blorb_pictures(args.blorb):
                ext = {b"PNG ": "png", b"JPEG": "jpg"}.get(cid)
                if ext is None:
                    print(f"os88pix: picture {num} is a {cid.decode('latin1')} "
                          f"chunk - skipped")
                    continue
                p = os.path.join(td, f"{num}.{ext}")
                with open(p, "wb") as fh:
                    fh.write(body)
                sources.append((num, p))
            sources = [(n, *prepare(p, args)) for n, p in sources]
    for spec in args.pics:
        if "=" not in spec:
            fail(f"'{spec}' is not N=FILE")
        n, _, path = spec.partition("=")
        sources.append((int(n), *prepare(path, args)))

    build([(n, w, h, stride, data) for (n, w, h, stride, data) in sources],
          args.output, args.release, args.max_width, args.max_height)


def prepare(path, args):
    w, h, px = load_image(path)
    nw, nh, rows = quantise(px, w, h, args.max_width, args.max_height)
    stride, data = pack4(rows, nw, nh)
    return nw, nh, stride, data


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""t_lemdat: read the 26 DOS Lemmings data files a SECOND time, independently,
and hold tools/os88lem.py's output against what this reading says.

SPEC.md 92.2: "Every compressed section in all 21 compressed files decodes on
the first attempt - 101 sections, every XOR checksum matching its header, every
decompressed length equalling the header's, both `unused` words zero, and each
file's section lengths summing to exactly the file length with no slack. That
is measured, not assumed, and tests/unit/t_lemdat.py is the standing check."

INDEPENDENCE IS THE POINT, so the decoder here is written from LEMMINGS.TS's
semantics and not from tools/os88lem.py's:

  * the container walk is Lemmings.ts file-container.ts read(): a 10-byte
    header, big-endian words, `compressedSize = size - HEADER_SIZE`, and the
    out-of-sync test on `size`
  * the bit reader is Lemmings.ts bit-reader.ts: it starts on the payload's
    LAST byte with `bufferLen = initialBufferLen`, shifts the buffer right one
    bit at a time, and ACCUMULATES THE CHECKSUM AS IT GOES - XOR of every byte
    it actually pulled in. os88lem.py instead XORs the whole payload up front.
    The two agree only if the stream consumed exactly the payload and nothing
    else, so this is the stronger reading of the same claim
  * the six encodings are unpack-file-part.ts doUnpacking()'s switch, in that
    file's own order, with bit-writer.ts's back-to-front output
  * the 120-level order is transcribed a SECOND time here from Lemmings.ts's
    config.json encoding (fileId*10 + partIndex, negative = use the odd
    table). Two transcriptions that agree are evidence; one is a typo waiting
    to happen

It then reads what the converter WROTE and checks it against this reading:
every band's magic, version, part count and declared length; the manifest's
120 entries against the raw records this file decoded for itself, with
ODDTABLE applied here rather than trusted; and every name and size against
SPEC.md 92.3.2's limits.

SKIPS cleanly - exit 0, saying why - when build/lemdata.stamp is absent. A
clone with nasm and python3 builds every floppy this project ships, and the
26 data files are fetched and never committed (SPEC.md 92.2), so a red row
there would be reporting on the box rather than on the tree.

    python3 tests/unit/t_lemdat.py
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

DATA = os.path.join(ROOT, "build", "lemdata")
STAMP = os.path.join(ROOT, "build", "lemdata.stamp")
BANDS = os.path.join(ROOT, "build", "lemband")

FAILURES = []


def check(cond, msg):
    if not cond:
        FAILURES.append(msg)
    return cond


# ---------------------------------------------------------------------------
# 1. The container and the bit stream, Lemmings.ts's way
# ---------------------------------------------------------------------------

HEADER_SIZE = 10


class Part(object):
    """Lemmings.ts unpack-file-part.ts."""

    __slots__ = ("index", "offset", "init_len", "checksum", "dec_size",
                 "comp_size", "unknown0", "unknown1", "size")


def container(blob, name):
    """Lemmings.ts file-container.ts read()."""
    parts = []
    pos = 0
    while pos + HEADER_SIZE < len(blob):
        p = Part()
        p.index = len(parts)
        p.offset = pos + HEADER_SIZE
        p.init_len = blob[pos]
        p.checksum = blob[pos + 1]
        (p.unknown1, p.dec_size, p.unknown0,
         p.size) = struct.unpack_from(">HHHH", blob, pos + 2)
        p.comp_size = p.size - HEADER_SIZE
        if p.offset < 0 or p.size > 0xFFFFFF or p.size < 10:
            check(False, "%s: out of sync at section %d" % (name, p.index))
            break
        parts.append(p)
        pos += p.size
    return parts, pos


class BitReader(object):
    """Lemmings.ts bit-reader.ts, including its running checksum."""

    __slots__ = ("buf", "pos", "buffer", "buflen", "checksum", "start")

    def __init__(self, blob, offset, length, init_buffer_len):
        self.buf = blob
        self.start = offset
        self.pos = offset + length - 1
        self.buffer = blob[self.pos]
        self.buflen = init_buffer_len
        self.checksum = self.buffer

    def read(self, bit_count):
        result = 0
        for _ in range(bit_count):
            if self.buflen <= 0:
                self.pos -= 1
                if self.pos < self.start:
                    raise ValueError("bit stream ran off the front")
                b = self.buf[self.pos]
                self.buffer = b
                self.checksum ^= b
                self.buflen = 8
            self.buflen -= 1
            result = (result << 1) | (self.buffer & 1)
            self.buffer >>= 1
        return result

    def eof(self):
        return self.buflen <= 0 and self.pos <= self.start


class BitWriter(object):
    """Lemmings.ts bit-writer.ts - the output is filled back to front."""

    __slots__ = ("data", "pos", "reader")

    def __init__(self, reader, out_length):
        self.data = bytearray(out_length)
        self.pos = out_length
        self.reader = reader

    def copy_raw(self, length):
        if self.pos - length < 0:
            raise ValueError("copyRawData: out of out buffer")
        for _ in range(length):
            self.pos -= 1
            self.data[self.pos] = self.reader.read(8)

    def copy_referenced(self, length, offset_bits):
        offset = self.reader.read(offset_bits) + 1
        if self.pos + offset > len(self.data):
            raise ValueError("copyReferencedData: offset out of range")
        if self.pos - length < 0:
            raise ValueError("copyReferencedData: out of out buffer")
        for _ in range(length):
            self.pos -= 1
            self.data[self.pos] = self.data[self.pos + offset]

    def eof(self):
        return self.pos <= 0


def unpack(blob, part):
    """Lemmings.ts unpack-file-part.ts doUnpacking()."""
    r = BitReader(blob, part.offset, part.comp_size, part.init_len)
    w = BitWriter(r, part.dec_size)
    while not w.eof() and not r.eof():
        if r.read(1) == 0:
            if r.read(1) == 0:                  # 00
                w.copy_raw(r.read(3) + 1)
            else:                               # 01
                w.copy_referenced(2, 8)
        else:
            k = r.read(2)
            if k == 0:                          # 100
                w.copy_referenced(3, 9)
            elif k == 1:                        # 101
                w.copy_referenced(4, 10)
            elif k == 2:                        # 110
                w.copy_referenced(r.read(8) + 1, 12)
            else:                               # 111
                w.copy_raw(r.read(8) + 9)
    return bytes(w.data), r.checksum


COMPRESSED = (["LEVEL%03d.DAT" % i for i in range(10)]
              + ["VGAGR%d.DAT" % i for i in range(5)]
              + ["VGASPEC%d.DAT" % i for i in range(4)]
              + ["MAIN.DAT"])
UNCOMPRESSED = ["GROUND%dO.DAT" % i for i in range(5)] + ["ODDTABLE.DAT"]


def read_all():
    """Every compressed section, decoded and checked. Returns {name: [bytes]}."""
    out = {}
    nsections = 0
    for name in COMPRESSED:
        with open(os.path.join(DATA, name), "rb") as fh:
            blob = fh.read()
        parts, consumed = container(blob, name)
        check(consumed == len(blob),
              "%s: %d bytes of %d consumed by %d sections, DAT says the "
              "sections tile the file" % (name, consumed, len(blob),
                                          len(parts)))
        sections = []
        for p in parts:
            nsections += 1
            data, checksum = unpack(blob, p)
            check(checksum == p.checksum,
                  "%s section %d: the bit reader's running XOR is 0x%02X and "
                  "the header says 0x%02X" % (name, p.index, checksum,
                                              p.checksum))
            check(len(data) == p.dec_size,
                  "%s section %d: %d bytes out, the header says %d"
                  % (name, p.index, len(data), p.dec_size))
            check(p.unknown0 == 0 and p.unknown1 == 0,
                  "%s section %d: unused words are %d and %d, DAT says both "
                  "are zero" % (name, p.index, p.unknown1, p.unknown0))
            sections.append(data)
        out[name] = sections
    check(nsections == 101,
          "%d compressed sections in the 21 compressed files; SPEC.md 92.2 "
          "says 101" % nsections)
    print("  101 sections: %d decoded, every running checksum, every declared "
          "length, both unused words zero, no slack in any file" % nsections)
    return out


# ---------------------------------------------------------------------------
# 2. GROUNDxO, and the terrain mask identity
# ---------------------------------------------------------------------------


def read_ground(path):
    with open(path, "rb") as fh:
        blob = fh.read()
    check(len(blob) == 1056, "%s is %d bytes, not 1056"
          % (os.path.basename(path), len(blob)))
    objects = []
    for i in range(16):
        (flags, first, last, w, h, fsize, mask_off, u1, u2, tl, tt, tw, th,
         eff, base, prev, u3, snd) = struct.unpack_from(
             "<HBBBBHHHHHHBBBHHHB", blob, i * 28)
        objects.append(dict(index=i, w=w, h=h, frames=last, first=first,
                            fsize=fsize, mask_off=mask_off, base=base,
                            effect=eff, tl=tl, tt=tt, tw=tw, th=th, sound=snd))
    terrain = []
    for i in range(64):
        w, h, image, mask, _u = struct.unpack_from("<BBHHH", blob, 448 + i * 8)
        terrain.append(dict(index=i, w=w, h=h, image=image, mask=mask))
    return objects, terrain


def check_terrain(sections):
    """The mask IS plane 3, over all 273 used pieces in all five sets.

    GRND says so - "the last plane (that adds 8 to a pixel) is equal to the
    mask plane ... the mask location is indeed identical to the last plane's
    location" - and SPEC.md 92.3.1 makes it the port's ruling on terrain bpp.
    What makes it checkable rather than restatable is that the pieces must
    then TILE VGAGR section 0 with no gap and no overlap at four planes each.
    """
    total_pieces = 0
    for s in range(5):
        objects, terrain = read_ground(os.path.join(DATA, "GROUND%dO.DAT" % s))
        terr_sec = sections["VGAGR%d.DAT" % s][0]
        obj_sec = sections["VGAGR%d.DAT" % s][1]
        spans = []
        for t in terrain:
            if t["w"] == 0 or t["h"] == 0:
                continue
            total_pieces += 1
            plane = t["w"] * t["h"] // 8
            check(t["mask"] - t["image"] == plane * 3,
                  "set %d terrain %d: mask_loc - image_loc is %d, and three "
                  "planes of %dx%d is %d"
                  % (s, t["index"], t["mask"] - t["image"], t["w"], t["h"],
                     plane * 3))
            check(t["w"] % 16 == 0,
                  "set %d terrain %d is %d wide; SPEC.md 92.3.1 says every "
                  "terrain width in every set is a multiple of 16"
                  % (s, t["index"], t["w"]))
            spans.append((t["image"], plane * 4, t["index"]))
        spans.sort()
        cursor = 0
        for (start, length, idx) in spans:
            check(start == cursor,
                  "set %d terrain %d starts at %d and the piece before it "
                  "ended at %d - the pieces do not tile section 0"
                  % (s, idx, start, cursor))
            cursor = start + length
        check(cursor == len(terr_sec),
              "set %d: the terrain pieces cover %d bytes and VGAGR section 0 "
              "is %d" % (s, cursor, len(terr_sec)))

        # And the objects: five planes a frame, the fifth the mask.
        for o in objects:
            if o["w"] == 0 or o["h"] == 0:
                continue
            plane = o["w"] * o["h"] // 8
            check(o["fsize"] == plane * 5,
                  "set %d object %d: frame_data_size %d, and five planes of "
                  "%dx%d is %d" % (s, o["index"], o["fsize"], o["w"], o["h"],
                                   plane * 5))
            check(o["mask_off"] == plane * 4,
                  "set %d object %d: mask_offset %d, and four planes is %d"
                  % (s, o["index"], o["mask_off"], plane * 4))
            check(o["base"] + o["frames"] * o["fsize"] <= len(obj_sec),
                  "set %d object %d runs past VGAGR section 1"
                  % (s, o["index"]))
    check(total_pieces == 273,
          "%d used terrain pieces over the five sets; SPEC.md 92.3.1 says 273"
          % total_pieces)
    print("  terrain: %d pieces, mask == plane 3 on every one, every width a "
          "multiple of 16, the pieces tile VGAGR section 0 exactly in all "
          "five sets" % total_pieces)


# ---------------------------------------------------------------------------
# 3. The four special pictures
# ---------------------------------------------------------------------------


def rle(buf, pos):
    """SPEC's byte RLE, transcribed here rather than shared."""
    out = bytearray()
    while pos < len(buf):
        c = buf[pos]
        if c == 0x80:
            return bytes(out), pos + 1
        if c < 0x80:
            out += buf[pos + 1:pos + 1 + c + 1]
            pos += 2 + c
        else:
            out += bytes([buf[pos + 1]]) * (257 - c)
            pos += 2
    return bytes(out), pos


def check_specials(sections):
    for i in range(4):
        parts = sections["VGASPEC%d.DAT" % i]
        check(len(parts) == 1,
              "VGASPEC%d.DAT has %d DAT sections; SPEC says 1"
              % (i, len(parts)))
        data = parts[0]
        chunks = []
        pos = 40                                    # 24 VGA + 16 EGA bytes
        while pos < len(data) and len(chunks) < 4:
            chunk, pos = rle(data, pos)
            chunks.append(chunk)
        check(len(chunks) == 4,
              "VGASPEC%d.DAT gave %d RLE chunks; SPEC says 4" % (i, len(chunks)))
        for n, chunk in enumerate(chunks):
            check(len(chunk) == 14400,
                  "VGASPEC%d.DAT chunk %d is %d bytes; SPEC says 14400"
                  % (i, n, len(chunk)))
        check(pos == len(data),
              "VGASPEC%d.DAT has %d bytes left after the fourth chunk, which "
              "is what would say the 40-byte palette guess is wrong"
              % (i, len(data) - pos))
    print("  specials: four pictures, each one DAT section, each exactly 4 "
          "chunks of 14,400 bytes with nothing left over")


# ---------------------------------------------------------------------------
# 4. The 120-level order, against BOTH reference tables
# ---------------------------------------------------------------------------
#
# A SECOND transcription of Lemmings.ts public/data/config.json `level.order`
# for gametype LEMMINGS: fileId*10 + partIndex, a negative sign meaning "use
# the odd table" (TS level-index-resolve.ts:47-51). It is checked against
# os88lem.py's own copy of the C table, which came from lemmings_3ds
# gamespecific.c:172 - two readers, two encodings, one order.

TS_ORDER = [
    # Fun
    [91, 95, 96, 92, 93, 94, 97, -6, -12, -32,
     -42, -7, 16, -17, -22, -24, -27, -43, -51, -63,
     -84, 13, -41, -57, -60, -71, -46, -61, -65, -82],
    # Tricky
    [0, -16, -21, -30, -31, -33, -34, -47, -62, -73,
     -77, -80, -83, 2, -91, -93, -94, -95, -97, 3,
     5, 6, 7, 10, 11, 12, 14, 15, 20, 17],
    # Taxing
    [22, 23, 24, 25, 26, 27, 30, 31, 32, 33,
     34, 35, 36, 37, 1, 40, 41, 42, 43, 44,
     45, 46, 47, 50, 51, 52, 53, 54, 21, 67],
    # Mayhem
    [55, 56, 57, 60, 61, 62, 63, 64, 65, 66,
     -67, 70, 71, 72, 73, 74, 75, 76, 77, -92,
     80, 4, 81, 82, 83, 84, 85, 86, 87, 90],
]

RATINGS = ["Fun", "Tricky", "Taxing", "Mayhem"]


def ts_entry(rating, n):
    v = TS_ORDER[rating][n]
    return abs(v) // 10, abs(v) % 10, v < 0


def check_order(lem):
    for rating in range(4):
        check(len(TS_ORDER[rating]) == 30,
              "the TS order has %d entries for %s, not 30"
              % (len(TS_ORDER[rating]), RATINGS[rating]))
        for n in range(30):
            c = lem.POSITION_OF_CLASSIC_LEVEL[rating * 30 + n]
            cf, cn, cm = lem.decode_position(c)
            tf, tn, tm = ts_entry(rating, n)
            check((cf, cn, cm) == (tf, tn, tm),
                  "%s %d: the C table says file %d section %d odd=%s and the "
                  "TS table says %d/%d/%s"
                  % (RATINGS[rating], n + 1, cf, cn, cm, tf, tn, tm))
    print("  order: 120 entries, the C table and a second transcription of "
          "the TS table agreeing on file, section and odd-table flag for "
          "every one")


# ---------------------------------------------------------------------------
# 5. What the converter WROTE, against what this file read
# ---------------------------------------------------------------------------

NAME_ALPHABET = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
FILEMAX = 64512


def check_bands(sections, lem, out_dir, geom):
    """Every band's header against its own bytes, and the manifest against
    the raw records THIS file decoded."""
    if not os.path.isdir(out_dir):
        return False
    names = sorted(os.listdir(out_dir))
    blobs = {}
    for name in names:
        with open(os.path.join(out_dir, name), "rb") as fh:
            blobs[name] = fh.read()
        stem, _, ext = name.partition(".")
        ok = (1 <= len(stem) <= 8 and 1 <= len(ext) <= 3
              and all(ch in NAME_ALPHABET for ch in stem + ext))
        check(ok, "%s/%s is not an uppercase 8.3 name in [A-Z0-9_-] "
                  "(SPEC.md 92.3.2)" % (geom, name))
        check(len(blobs[name]) <= FILEMAX,
              "%s/%s is %d bytes and WIRE_FILEMAX is %d"
              % (geom, name, len(blobs[name]), FILEMAX))
        if ext == "LEM":
            check(len(blobs[name]) % 512 == 0,
                  "%s/%s is %d bytes and every band is 512-padded"
                  % (geom, name, len(blobs[name])))

    def bank(base, magic):
        """Reassemble a bank from its parts and check what it claims."""
        parts = [n for n in names
                 if n == base + ".LEM" or n.startswith(base + "_")]
        if not parts:
            return None
        parts.sort()
        body = b"".join(blobs[n] for n in parts)
        check(body[0:4] == magic,
              "%s/%s starts %r, not %r" % (geom, parts[0], body[0:4], magic))
        version = struct.unpack_from("<H", body, 4)[0]
        check(version == 1, "%s/%s is version %d, not 1"
              % (geom, parts[0], version))
        return parts, body

    # --- the style banks --------------------------------------------------
    for s in range(5):
        got = bank("LEMGR%d" % s, b"LGRB")
        if got is None:
            continue
        parts, body = got
        nterr, nobj, nparts = body[7], body[8], body[9]
        totlen = struct.unpack_from("<I", body, 10)[0]
        check(nparts == len(parts),
              "%s/LEMGR%d claims %d parts and %d files carry it"
              % (geom, s, nparts, len(parts)))
        check(totlen <= len(body) and len(body) - totlen < 512,
              "%s/LEMGR%d declares %d logical bytes and its parts hold %d"
              % (geom, s, totlen, len(body)))
        objects, terrain = read_ground(os.path.join(DATA, "GROUND%dO.DAT" % s))
        want_t = sum(1 for t in terrain if t["w"] and t["h"])
        want_o = sum(1 for o in objects if o["w"] and o["h"])
        check(nterr == want_t, "%s/LEMGR%d claims %d terrain pieces and "
                               "GROUND%dO.DAT has %d used slots"
              % (geom, s, nterr, s, want_t))
        check(nobj == want_o, "%s/LEMGR%d claims %d objects and GROUND%dO.DAT "
                              "has %d used slots" % (geom, s, nobj, s, want_o))
        # Every used piece's pixels, byte for byte, against VGAGR section 0.
        terr_sec = sections["VGAGR%d.DAT" % s][0]
        obj_sec = sections["VGAGR%d.DAT" % s][1]
        for t in terrain:
            if not (t["w"] and t["h"]):
                continue
            row = 128 + t["index"] * 8
            w, h = body[row], body[row + 1]
            off = struct.unpack_from("<I", body, row + 2)[0]
            n = struct.unpack_from("<H", body, row + 6)[0]
            check((w, h, n) == (t["w"], t["h"], t["w"] * t["h"] // 2),
                  "%s/LEMGR%d terrain %d: band says %dx%d/%d, GROUNDxO says "
                  "%dx%d/%d" % (geom, s, t["index"], w, h, n, t["w"], t["h"],
                                t["w"] * t["h"] // 2))
            check(body[off:off + n] == terr_sec[t["image"]:t["image"] + n],
                  "%s/LEMGR%d terrain %d: the band's pixels are not VGAGR "
                  "section 0's" % (geom, s, t["index"]))
        for o in objects:
            if not (o["w"] and o["h"]):
                continue
            row = 640 + o["index"] * 16
            off = struct.unpack_from("<I", body, row + 12)[0]
            n = o["frames"] * o["fsize"]
            check((body[row], body[row + 1], body[row + 2], body[row + 3])
                  == (o["w"], o["h"], o["frames"], o["effect"]),
                  "%s/LEMGR%d object %d: the band's metadata is not "
                  "GROUNDxO's" % (geom, s, o["index"]))
            check(body[off:off + n] == obj_sec[o["base"]:o["base"] + n],
                  "%s/LEMGR%d object %d: the band's frames are not VGAGR "
                  "section 1's" % (geom, s, o["index"]))

    # --- the MAIN bank ----------------------------------------------------
    got = bank("LEMMAIN", b"LMNB")
    if got is not None:
        parts, body = got
        nitems = body[6]
        check(nitems == 10, "%s/LEMMAIN carries %d items, not 10"
              % (geom, nitems))
        main = sections["MAIN.DAT"]
        # item 3 is MAIN section 0 whole; item 4 is section 1 whole.
        for (item, want) in ((3, main[0]), (4, main[1])):
            for i in range(nitems):
                row = 32 + i * 16
                if body[row] != item:
                    continue
                off, n = struct.unpack_from("<II", body, row + 8)
                check(n == len(want),
                      "%s/LEMMAIN item %d is %d bytes and MAIN's section is %d"
                      % (geom, item, n, len(want)))
                check(body[off:off + n] == want,
                      "%s/LEMMAIN item %d is not MAIN's own bytes"
                      % (geom, item))

    # --- the special pictures ---------------------------------------------
    for i in range(4):
        got = bank("LEMSP%d" % i, b"LSPB")
        if got is None:
            continue
        parts, body = got
        w, h = struct.unpack_from("<HH", body, 12)
        check((w, h, body[16]) == (960, 160, 4),
              "%s/LEMSP%d says %dx%d at %dbpp, not 960x160 at 4"
              % (geom, i, w, h, body[16]))
        totlen = struct.unpack_from("<I", body, 8)[0]
        check(totlen == 512 + 960 * 160 * 4 // 8,
              "%s/LEMSP%d declares %d bytes and 512 + four planes is %d"
              % (geom, i, totlen, 512 + 960 * 160 * 4 // 8))
        # Plane 3 must be the OR of planes 0..2 (C import_level.c:590-592).
        plane = 960 * 160 // 8
        p0 = body[512:512 + plane]
        p1 = body[512 + plane:512 + 2 * plane]
        p2 = body[512 + 2 * plane:512 + 3 * plane]
        p3 = body[512 + 3 * plane:512 + 4 * plane]
        check(bytes(a | b | c for (a, b, c) in zip(p0, p1, p2)) == p3,
              "%s/LEMSP%d: plane 3 is not the OR of planes 0-2" % (geom, i))

    # --- the manifest, against records this file decoded -------------------
    man = blobs.get("LEMMAN.LEM")
    if man is None:
        check(False, "%s has no LEMMAN.LEM" % geom)
        return True
    check(man[0:8] == b"OS88LEM\0", "%s/LEMMAN.LEM has the wrong magic" % geom)
    version, nlevels, lesize, leoff = struct.unpack_from("<HHHH", man, 8)
    check((version, lesize, leoff) == (1, 64, 0),
          "%s/LEMMAN.LEM says version %d, %d-byte entries at %d; the format "
          "is 1 / 64 / 0 - the entries are in the RATING files now"
          % (geom, version, lesize, leoff))
    check(nlevels == 120, "%s/LEMMAN.LEM counts %d levels, not 120"
          % (geom, nlevels))
    check(len(man) == 512,
          "%s/LEMMAN.LEM is %d bytes; it is a 512-byte header and nothing "
          "else now" % (geom, len(man)))

    ratings = []
    for r in range(4):
        blob = blobs.get("LEMR%d.LEM" % r)
        if not check(blob is not None, "%s has no LEMR%d.LEM" % (geom, r)):
            return True
        check(len(blob) == 2048,
              "%s/LEMR%d.LEM is %d bytes; 30 entries of 64 padded is 2048"
              % (geom, r, len(blob)))
        ratings.append(blob)

    # This disk's own clusters and the cost table, which the greyed rows'
    # templates are filled from and which nothing else would notice going
    # stale.
    diskclus = struct.unpack_from("<H", man, 56)[0]
    want_clus = {"1440": 2847, "1200": 2371, "720": 713, "360": 354}[geom]
    check(diskclus == want_clus,
          "%s/LEMMAN.LEM says this disk has %d clusters and the FAT12 "
          "arithmetic says %d" % (geom, diskclus, want_clus))
    cbytes = {"1440": 512, "1200": 512, "720": 1024, "360": 1024}[geom]
    for n in range(9):
        row = 60 + n * 8
        nb, nc, nparts, present = struct.unpack_from("<IHBB", man, row)
        base = ("LEMGR%d" % n) if n < 5 else ("LEMSP%d" % (n - 5))
        check(nparts >= 1, "%s/LEMMAN cost row %d (%s) claims %d parts"
              % (geom, n, base, nparts))
        check(nb % 512 == 0,
              "%s/LEMMAN cost row %d (%s) says %d bytes and every part is "
              "512-padded" % (geom, n, base, nb))
        if present:
            got = [x for x in names
                   if x == base + ".LEM" or x.startswith(base + "_")]
            check(len(got) == nparts,
                  "%s/LEMMAN says %s is here in %d parts and %d files carry "
                  "it" % (geom, base, nparts, len(got)))
            check(sum(len(blobs[x]) for x in got) == nb,
                  "%s/LEMMAN says %s is %d bytes and its files are %d"
                  % (geom, base, nb, sum(len(blobs[x]) for x in got)))
            check(nc == sum((len(blobs[x]) + cbytes - 1) // cbytes
                            for x in got),
                  "%s/LEMMAN says %s costs %d clusters and its files cost %d"
                  % (geom, base, nc,
                     sum((len(blobs[x]) + cbytes - 1) // cbytes
                         for x in got)))
        else:
            check(not [x for x in names if x.startswith(base)],
                  "%s/LEMMAN says %s is not here and a file of that name is"
                  % (geom, base))
    sb, sc = struct.unpack_from("<IH", man, 132)
    check(sb == sum(struct.unpack_from("<I", man, 60 + (5 + i) * 8)[0]
                    for i in range(4)),
          "%s/LEMMAN's four-specials byte total is not the sum of its own "
          "rows" % geom)
    check(sc == sum(struct.unpack_from("<H", man, 60 + (5 + i) * 8 + 4)[0]
                    for i in range(4)),
          "%s/LEMMAN's four-specials cluster total is not the sum of its own "
          "rows" % geom)

    # The 80 raw records and ODDTABLE, read here.
    records = []
    for f in range(10):
        records += sections["LEVEL%03d.DAT" % f]
    with open(os.path.join(DATA, "ODDTABLE.DAT"), "rb") as fh:
        odd = fh.read()
    check(len(odd) == 80 * 56, "ODDTABLE.DAT is %d bytes, not 80 x 56"
          % len(odd))

    styles_bits, spec_bits = man[16], man[17]
    lev_blobs = {}
    for n in range(10):
        blob = blobs.get("LEMLV%d.LEM" % n)
        if blob is not None:
            lev_blobs[n] = blob

    for i in range(nlevels):
        ent = ratings[i // 30]
        e = (i % 30) * 64
        rating, number = ent[e], ent[e + 1]
        style, special = ent[e + 2], ent[e + 3]
        rate, count, save, mins = struct.unpack_from("<HHHH", ent, e + 4)
        skills = list(ent[e + 12:e + 20])
        rawfile, rawsect = ent[e + 22], ent[e + 23]
        flags, missing = ent[e + 24], ent[e + 25]
        oddflag = struct.unpack_from("<H", ent, e + 26)[0]
        name = ent[e + 32:e + 64].split(b"\0")[0].decode("latin-1")

        check(rating == i // 30 and number == i % 30,
              "%s/LEMR entry %d says %s %d and the play order says %s %d"
              % (geom, i, RATINGS[rating] if rating < 4 else rating,
                 number + 1, RATINGS[i // 30], i % 30 + 1))

        tf, tn, tm = ts_entry(i // 30, i % 30)
        slot = tf * 8 + tn
        rec = bytearray(records[slot])
        if tm:
            rec[0:24] = odd[slot * 56:slot * 56 + 24]
            rec[0x7E0:0x800] = odd[slot * 56 + 24:slot * 56 + 56]
        rec = bytes(rec)

        def u16(o):
            return (rec[o] << 8) | rec[o + 1]

        check(oddflag == (1 if tm else 0),
              "%s/LEMR entry %d: oddtable flag %d, the order says %s"
              % (geom, i, oddflag, tm))
        check((rate, count, save, mins)
              == (u16(0), u16(2), u16(4), u16(6)),
              "%s/LEMR entry %d (%s): header %s, the record says %s"
              % (geom, i, name, (rate, count, save, mins),
                 (u16(0), u16(2), u16(4), u16(6))))
        check(skills == [rec[9 + 2 * k] for k in range(8)],
              "%s/LEMR entry %d (%s): skills %s, the record says %s"
              % (geom, i, name, skills, [rec[9 + 2 * k] for k in range(8)]))
        check(name == rec[0x7E0:0x800].decode("latin-1").rstrip(),
              "%s/LEMR entry %d: name %r, the record says %r"
              % (geom, i, name, rec[0x7E0:0x800].decode("latin-1").rstrip()))
        check(style == u16(0x1A),
              "%s/LEMR entry %d (%s): style %d, the record says %d"
              % (geom, i, name, style, u16(0x1A)))
        ext = u16(0x1C)
        check(special == (ext - 1 if ext else 0xFF),
              "%s/LEMR entry %d (%s): special %d, the record's extended set "
              "is %d" % (geom, i, name, special, ext))

        if flags & 1:
            check(styles_bits & (1 << style),
                  "%s/LEMR entry %d (%s) is playable and its style %d is "
                  "not on this disk" % (geom, i, name, style))
            if special != 0xFF:
                check(spec_bits & (1 << special),
                      "%s/LEMR entry %d (%s) is playable and special "
                      "picture %d is not on this disk"
                      % (geom, i, name, special))
            check(missing == 0,
                  "%s/LEMR entry %d (%s) is playable and names missing "
                  "string %d" % (geom, i, name, missing))
            blob = lev_blobs.get(rawfile)
            if check(blob is not None,
                     "%s/LEMR entry %d (%s) points at LEMLV%d.LEM, which "
                     "is not on this disk" % (geom, i, name, rawfile)):
                on_disk = blob[rawsect * 2048:(rawsect + 1) * 2048]
                # The disk carries the PLAIN record: ODDTABLE is folded into
                # this entry, not into the shared record (40 of the 80 slots
                # are read both ways).
                check(on_disk == records[slot],
                      "%s/LEMR entry %d (%s): LEMLV%d.LEM section %d is not "
                      "raw record %d" % (geom, i, name, rawfile, rawsect, slot))
        else:
            check(missing in (lem.SID["MISS_STYLE"], lem.SID["MISS_SPEC"],
                              lem.SID["MISS_ROOM"]),
                  "%s/LEMR entry %d (%s) is greyed with string id %d, and "
                  "the only three a level row may name are MISS_STYLE, "
                  "MISS_SPEC and MISS_ROOM" % (geom, i, name, missing))
            check(rawfile == 0xFF,
                  "%s/LEMR entry %d (%s) is not playable and still points "
                  "at a record" % (geom, i, name))

    # The string band must hold every id the entries name.
    band = blobs.get("LEMSTR.LEM")
    if check(band is not None, "%s has no LEMSTR.LEM" % geom):
        check(band[0:4] == b"LSTR", "%s/LEMSTR.LEM has the wrong magic" % geom)
        nstr = struct.unpack_from("<H", band, 4)[0]
        check(nstr == len(lem.STRINGS),
              "%s/LEMSTR.LEM holds %d strings and the table has %d - the "
              "band is the same on every geometry now"
              % (geom, nstr, len(lem.STRINGS)))
        for i in range(nstr):
            off = struct.unpack_from("<H", band, 6 + 2 * i)[0]
            check(0 < off < len(band) or i == 0,
                  "%s/LEMSTR.LEM string %d points at %d" % (geom, i, off))
            check(band.find(b"\0", off) >= 0,
                  "%s/LEMSTR.LEM string %d is not NUL-terminated" % (geom, i))
        for i in range(nlevels):
            missing = ratings[i // 30][(i % 30) * 64 + 25]
            check(missing < nstr,
                  "%s/LEMR entry %d names string %d and the band holds %d"
                  % (geom, i, missing, nstr))
        # %s is the only marker the package substitutes by hand, so nothing
        # in the band may look like another format.
        text = band[6 + 2 * nstr:]
        for i in range(len(text) - 1):
            if text[i:i + 1] == b"%":
                check(text[i + 1:i + 2] in (b"s", b" "),
                      "%s/LEMSTR.LEM has a %% followed by %r and %%s is the "
                      "only marker" % (geom, text[i + 1:i + 2]))
    print("  %s: %d files, every band's header true of its own bytes, all "
          "120 manifest entries against records read here" % (geom, len(names)))
    return True


# ---------------------------------------------------------------------------


def main():
    if not os.path.exists(STAMP):
        print("t_lemdat: SKIP - %s is not there.\n"
              "  The 26 DOS Lemmings data files are fetched and never "
              "committed (SPEC.md 92.2, CONTRIBUTING.md 6). Run\n"
              "    python3 tools/getlemmings.py\n"
              "  or, with no network,\n"
              "    python3 tools/getlemmings.py --from <dir-or-zip>\n"
              "  and this row checks them." % os.path.relpath(STAMP, ROOT))
        return 0

    import os88lem as lem

    print("t_lemdat: an independent second reading of %s"
          % os.path.relpath(DATA, ROOT))
    sections = read_all()
    check_terrain(sections)
    check_specials(sections)
    check_order(lem)

    looked = False
    for geom in ("1440", "1200", "720", "360"):
        if check_bands(sections, lem, os.path.join(BANDS, geom), geom):
            looked = True
    if not looked:
        print("  bands: none in %s - run `python3 tools/os88lem.py` and this "
              "row checks them too" % os.path.relpath(BANDS, ROOT))

    if FAILURES:
        print("\nt_lemdat: %d FAILURES" % len(FAILURES))
        for f in FAILURES[:40]:
            print("  %s" % f)
        if len(FAILURES) > 40:
            print("  ... and %d more" % (len(FAILURES) - 40))
        return 1
    print("t_lemdat: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""os88lem: convert the 26 DOS Lemmings (1991) data files into the band files
the LEMMINGS package reads (SPEC.md 92.3, docs/lemband-format.md).

    python3 tools/os88lem.py                       # convert all four geometries
    python3 tools/os88lem.py --geom 720            # just one
    python3 tools/os88lem.py --selfcheck           # the host-side gate
    python3 tools/os88lem.py --fixture DIR         # the committed synthetic set

THE 8088 DOES NO BIT-STREAM DECODING AT ANY POINT (SPEC.md 92.3). Everything
that needs a decoder happens here: the DAT container's backward bit stream, the
2,048-byte level record with Lemmix's bit unpacking, GROUNDxO, VGAGRx,
VGASPECx's second-level byte RLE, MAIN.DAT's seven sections, and ODDTABLE.DAT
folded in so the machine never sees that file and never learns its rule.

DETERMINISTIC: the same input rebuilds the same bytes. `--selfcheck` proves it
by building twice and comparing.

WHERE THE READERS COME FROM. The DAT/GROUNDxO/level/VGASPEC readers below are
lemtool/lemdat.py's, reused rather than re-derived (docs/LEMMINGS-PORT-PLAN.md
decision 9), with SPEC.md 92.3.1's nine rulings applied where the three
reimplementations disagree:

  world is 1584x160          not 1600 (lemmings_3ds and Lemmix over Lemmings.ts)
  terrain terminator         SKIP a 0xFFFF slot, never break - it is Taxing 27
                             (`Call in the bomb squad`) and breaking renders it
                             with 68 pieces instead of 395
  steel bit layout           x = the HIGH 9 bits of word 0, y = the low 7,
                             width = the HIGH nibble of byte 2
  ODDTABLE indexing          file*8 + section, the record's position on disk
  GROUNDxO endianness        little
  terrain x mask             12 bits
  terrain y boundary         y >= 0x100 before subtracting 516
  object slot in use         a non-zero record
  terrain bpp                four planes, plane 3 being the mask

Format tags in the comments below, as lemdat.py uses them:

  DAT   lemmings_3ds/doc/data/lemmings_dat_file_format.txt              (ccexplore)
  LVL   lemmings_3ds/doc/data/lemmings_lvl_file_format.txt              (rt)
  GRND  lemmings_3ds/doc/data/lemmings_vgagrx_dat_groundxo_dat_file_format.txt
  MAIN  lemmings_3ds/doc/data/lemmings_main_dat_file_format.txt
  SPEC  lemmings_3ds/doc/data/lemmings_vgaspecx_dat_file_format.txt
  C     lemmings_3ds/src/import/*.c
  TS    Lemmings.ts/src/game/resources/**
  LX    Lemmix/src/*.pas

============================================================================
THE OUTPUT, band by band
============================================================================

docs/lemband-format.md pins LEMMAN.LEM, LEMSTR.LEM and the level files, and
names the style, MAIN and special banks without giving their insides. The
insides are pinned here, and three of them are forced rather than chosen:

THREE THINGS THE PINNED DOCUMENT CANNOT SAY, all reported rather than papered
over (and a fourth, the ODDTABLE fold, under LEVEL RECORDS below):

 1. NO STYLE BANK FITS 64,512 BYTES. A bank is its palette, its terrain pieces
    and its object frames, and that is 78,732 bytes for the smallest set and
    96,804 for set 1. So every style bank is written as numbered PARTS, which
    is SPEC.md 92.3.2's own mechanism for exactly this ("a bank that would
    exceed it ... is written as numbered parts") and the same mechanism the
    special pictures already need. The names follow the document's own
    special-picture spelling: `LEMGR<n>_<part>.LEM`. A bank that fits in one
    part keeps its plain name, so LEMMAN, LEMSTR, LEMLVn and LEMMAIN are
    unaffected.

 2. `setbytes` CANNOT BE A WORD. The document gives offset 18 two bytes for
    "bytes of converted data on this disk", and this disk carries about
    950,000 of them. Offset 18 is written as KILOBYTES, rounded up, and the
    exact 32-bit count goes at offset 42 - inside the block the document
    reserves and zeroes, so nothing moves and a reader that ignores it is
    still right.

 3. A GREYED LEVEL ROW NEEDS NUMBERS THE PACKAGE CANNOT MEASURE. Its
    `missing` byte names one of three FIXED templates - LEMS_MISS_STYLE,
    LEMS_MISS_SPEC, LEMS_MISS_ROOM - whose only marker is %s, because there
    is no printf in this C and the package substitutes by hand. A greyed row
    is asking about a bank that is NOT on the disk, so its size cannot be
    read off the disk: LEMMAN.LEM carries a nine-row COST TABLE, present
    banks and absent ones alike, at offset 60. build_manifest() below has the
    layout. That is also what makes LEMSTR.LEM the same bytes on every
    geometry, so the ids in build/lemstr.h never move.

STYLE BANK - LEMGR<n>[_<part>].LEM, magic "LGRB"
    0   4  "LGRB"
    4   2  version 1
    6   1  style 0..4
    7   1  nterrain, used pieces (the table still has all 64 rows)
    8   1  nobjects, used (all 16 rows)
    9   1  parts
   10   4  totlen, logical bytes before the last part's 512 pad
   14   2  terrain table offset = 128
   16   2  object table offset = 640
   18   2  pixel data offset = 1024
   20  12  reserved zero
   32  24  vga_custom palette, 8 entries x 3 bytes, 6-bit components as stored
   56  24  vga_standard
   80  24  vga_preview
  104  24  reserved zero
  128 512  terrain table, 64 rows of 8:
              0 1 w   pixels; 0 = unused slot. Every width in every set is
                              already a multiple of 16 (measured), so planar
                              rounding costs nothing
              1 1 h
              2 4 off from the START of the logical bank
              6 2 nbytes = w*h/2
  640 256  object table, 16 rows of 16:
              0 1 w        0 = unused slot
              1 1 h
              2 1 frames
              3 1 effect      GRND trigger_effect_id: 0 none, 1 exit, 4 trap,
                              5 drown, 6 disintegrate, 7 one-way left,
                              8 one-way right, 9 steel
              4 2 trigleft    GRND, x4 resolution, as stored
              6 2 trigtop     as stored; the reader subtracts 4 (GRND: top*4-4)
              8 1 trigw       x4 resolution, a stored 0 MEANING 256
              9 1 trigh       likewise
             10 1 sound       trap sound id
             11 1 startframe
             12 4 off         from the START of the logical bank
  896 128  reserved zero
 1024 ...  pixel data: every USED terrain piece in slot order, then every used
           object's frames in slot order.
             a terrain piece is FOUR planes of w*h/8 bytes and plane 3 IS the
               mask (GRND, and measured: mask_loc - image_loc == w*h*3/8 for
               every one of the 273 used pieces in all five sets). That makes
               the stored nibble the mode-0Dh colour directly: planes 0-2 are
               the style's 0-7 and plane 3 lifts it into the level's 8-15
             an object frame is FIVE planes of w*h/8 - four colour and a mask
               (measured: frame_data_size == w*h*5/8 for every used object in
               all five sets, and mask_offset == w*h*4/8 within the frame)

MAIN BANK - LEMMAIN.LEM, magic "LMNB"
    0   4  "LMNB"
    4   2  version 1
    6   1  nitems
    7   1  parts
    8   4  totlen
   12  20  reserved zero
   32 256  item table, 16 rows of 16:
              0 1 id      see below
              1 1 bpp
              2 2 w
              4 2 h
              6 2 frames
              8 4 off     from the START of the logical bank
             12 4 nbytes
  288 224  reserved zero
  512 ...  the item bytes, in id order

    id  item                             source                    bytes
     0  skill panel, 320x40 4bpp         MAIN section 6 offset 0    6,400
     1  green status font, 38x 8x16 3bpp MAIN section 6 0x1900      1,824
     2  skill-count digits, 20x 8x8 1bpp MAIN section 2 0x1900        160
     3  lemming animations, 337 frames   MAIN section 0            21,104
     4  destruction masks + countdown    MAIN section 1               388
     5  rating signs, 4x 72x27 4bpp      MAIN section 4 0x65E4      3,888
     6  purple font, 94x 16x16 3bpp      MAIN section 4 0x69B0      9,024
     7  brown background, 320x104 2bpp   MAIN section 3 offset 0    8,320
     8  animation registration table     30 rows of 8 (below)         240
     9  mask registration table          6 rows of 8                   48

    A registration row is: 0 1 frames, 1 1 w, 2 1 h, 3 1 bpp,
    4 2 off (from the start of item 3 / item 4), 6 2 bytes per frame.
    Item 5's four signs are stored in NAME ORDER - Fun, Tricky, Taxing,
    Mayhem - which is the REVERSE of MAIN's own order (MAIN section 7,
    LX GameScreen.Menu.pas PaintCurrentSection), so the reader indexes them
    by rating and does not have to know that.

SPECIAL PICTURE - LEMSP<n>_<part>.LEM, magic "LSPB"
    0   4  "LSPB"
    4   2  version 1
    6   1  index 0..3
    7   1  parts
    8   4  totlen
   12   2  w = 960
   14   2  h = 160
   16   1  bpp = 4
   17  15  reserved zero
   32  24  vga palette, 8 entries x 3 bytes, 6-bit components as stored
   56  16  the 16 EGA bytes, as stored (meaning unconfirmed by any reader)
   72 440  reserved zero
  512 ...  76,800 bytes: four planes of 19,200. Planes 0-2 are the picture's
           own 3bpp (SPEC: "unlike the interactive objects and terrain
           bitmaps, the planar bitmaps here is 3 bpp") and plane 3 is built
           here as their OR, which is C import_level.c:590-592's missing
           fourth plane and makes the mapping 0 -> 0, 1-7 -> 9-15 fall out of
           the nibble. Drawn at x = 304, y = 0 in the 1584x160 world.

LEVEL RECORDS - LEMLV<n>.LEM, no header: eight 2,048-byte records back to back.
The records are REPACKED per geometry - a 360KB disk carries two graphic sets
and only the levels that use them, so it carries only the records those levels
need, and the manifest's rawfile/rawsect name the file THIS disk has.

ODDTABLE IS FOLDED INTO THE MANIFEST AND NOT INTO THE RECORD, and that is the
fourth thing the pinned document could not know. 40 of the 80 raw slots are read
BOTH ways - plainly for one level and overridden for another - so the override
cannot live in the shared record, and duplicating the record to carry it would
cost 81,920 bytes, which is 80 of a 720KB disk's 713 clusters. The 64-byte
level entry already carries every field ODDTABLE overwrites - the rate, the
counts, the time, the eight skills and the name - so the entry carries them
with the override applied and the record is read for its terrain, objects and
steel, which ODDTABLE never touches. The machine still never sees ODDTABLE.DAT
and never learns its rule, which is what SPEC.md 92.3 asks for.
"""
import argparse
import filecmp
import os
import shutil
import struct
import sys
import tempfile

# ---------------------------------------------------------------------------
# 0. Limits and geometry
# ---------------------------------------------------------------------------

# SPEC.md 92.3.2: WIRE_FILEMAX (SPEC.md 88.13) and RD_FILEMAX (92.6.9.10, one
# device along) - every offset inside a RAM-disk file is a word. The converter
# refuses on it rather than discovering it at publication.
FILEMAX = 64512
# SPEC.md 88.13: the whole archive, the first size refused.
ARCMAX = 1048575
# SPEC.md 92.3.2: directory rows over the whole RAM-disk volume.
RD_MAXENT = 96

SECTOR = 512

# tools/os88disk.py:124's GEOMETRY, and the FAT12 arithmetic around it:
# reserved 1 + 2 FATs + the root directory, the rest divided by spc.
#   (total sectors, sectors per cluster, FAT sectors, root entries)
GEOM = {
    1440: (2880, 1, 9, 224),
    1200: (2400, 1, 7, 224),
    720: (1440, 2, 3, 112),
    360: (720, 2, 2, 112),
}
GEOM_ORDER = [1440, 1200, 720, 360]


def data_clusters(geom):
    tot, spc, fatsecs, root_ent = GEOM[geom]
    root_secs = root_ent * 32 // SECTOR
    return (tot - 1 - 2 * fatsecs - root_secs) // spc


def cluster_bytes(geom):
    return GEOM[geom][1] * SECTOR


# SPEC.md 92.9: "Package plus overlay is ~57,000 bytes = 56 clusters". Plus
# LEVELS.TXT and README.TXT, which are small and are still files.
PKG_RESERVE = 57000 + 4096

# SPEC.md 92.3.1: the world, and the five sets.
WORLD_W, WORLD_H = 1584, 160
STYLE_NAMES = ["Dirt", "Fire", "Squasher", "Pillar", "Crystal"]
RATINGS = ["Fun", "Tricky", "Taxing", "Mayhem"]

LEVELS_PER_FILE = 8          # SPEC.md 92.3.2: grouped eight to a file
LVL_SIZE = 2048
ENTRY_SIZE = 64              # docs/lemband-format.md, level entry
LEVELS_PER_RATING = 30
RATING_FILE = 2048           # 30 x 64 = 1,920, padded
MANIFEST_HEADER = 512

SPEC_W, SPEC_H, SPEC_BPP = 960, 160, 3
SPEC_CHUNK_BYTES = SPEC_W * 40 * SPEC_BPP // 8      # 14,400
SPEC_PALETTE_BYTES = 40
SPEC_X, SPEC_Y = 304, 0


class Refused(Exception):
    """A limit this converter refuses on, named with its arithmetic."""


def fail(msg):
    print("os88lem: error: " + msg, file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# 1. The DAT container - lemtool/lemdat.py's reader
# ---------------------------------------------------------------------------
#
# DAT: "Each compressed data section consists of a 10-byte header and then the
# actual compressed data". The header's words are BIG-endian, and its
# compressed_data_size counts the 10 header bytes.

HEADER_SIZE = 10


class Section(object):
    __slots__ = ("index", "offset", "bits", "checksum", "unused1", "unused2",
                 "dec_size", "comp_total", "raw", "data", "checksum_ok")


def dat_sections(blob, path):
    out = []
    pos = 0
    while pos + HEADER_SIZE <= len(blob):
        s = Section()
        s.index = len(out)
        s.offset = pos
        s.bits = blob[pos]
        s.checksum = blob[pos + 1]
        (s.unused1, s.dec_size, s.unused2,
         s.comp_total) = struct.unpack_from(">HHHH", blob, pos + 2)
        if s.comp_total < HEADER_SIZE:
            raise Refused("%s section %d: compressed_data_size %d < 10"
                          % (path, s.index, s.comp_total))
        s.raw = blob[pos + HEADER_SIZE: pos + s.comp_total]
        if len(s.raw) != s.comp_total - HEADER_SIZE:
            raise Refused("%s section %d: truncated" % (path, s.index))
        # DAT: "The checksum is an exclusive or of all the bytes in the actual
        # compressed data (ie. excluding the header)."
        x = 0
        for b in s.raw:
            x ^= b
        s.checksum_ok = (x == s.checksum)
        s.data = None
        out.append(s)
        pos += s.comp_total
    return out


class _BitStream(object):
    """DAT: the bit stream runs BACKWARDS through the payload, LSB first out
    of each byte, and only `bits_in_first_byte` bits come out of the payload's
    LAST byte."""

    __slots__ = ("buf", "pos", "mask", "start_bits")

    def __init__(self, payload, bits_in_first_byte):
        self.buf = payload
        self.pos = len(payload) - 1
        self.mask = 1
        self.start_bits = bits_in_first_byte

    def bit(self):
        if self.start_bits == 0 or self.mask == 0:
            if self.pos < 1:
                raise Refused("bit stream underrun")
            self.mask = 1
            self.pos -= 1
            self.start_bits = -1
        elif self.start_bits > 0:
            self.start_bits -= 1
        v = 1 if (self.buf[self.pos] & self.mask) else 0
        self.mask = (self.mask << 1) & 0xFF
        return v

    def bits(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | self.bit()
        return v


def decompress(section):
    """DAT's six encodings; the output is built back to front."""
    if section.data is not None:
        return section.data
    if not 0 <= section.bits <= 8:
        raise Refused("num_bits_in_first_byte = %d out of range" % section.bits)
    out = bytearray(section.dec_size)
    bs = _BitStream(section.raw, section.bits)
    p = section.dec_size

    while p > 0:
        if bs.bit():
            kind = bs.bits(2) + 2               # 1xx -> 100/101/110/111
        else:
            kind = bs.bit()                     # 00 -> 0, 01 -> 1
        if kind == 0:                           # DAT #1: 00 nnn xxxxxxxx...
            n = bs.bits(3) + 1
            if p - n < 0:
                raise Refused("raw chunk underruns the output")
            for _ in range(n):
                p -= 1
                out[p] = bs.bits(8)
            continue
        if kind == 1:                           # DAT #2: 01 mmmmmmmm
            n, off = 2, bs.bits(8) + 1
        elif kind == 2:                         # DAT #3: 100 mmmmmmmmm
            n, off = 3, bs.bits(9) + 1
        elif kind == 3:                         # DAT #4: 101 mmmmmmmmmm
            n, off = 4, bs.bits(10) + 1
        elif kind == 4:                         # DAT #5: 110 nnnnnnnn mmmm*12
            n = bs.bits(8) + 1
            off = bs.bits(12) + 1
        else:                                   # DAT #6: 111 nnnnnnnn xxx...
            n = bs.bits(8) + 9
            if p - n < 0:
                raise Refused("raw chunk underruns the output")
            for _ in range(n):
                p -= 1
                out[p] = bs.bits(8)
            continue
        if p - n < 0:
            raise Refused("reference underruns the output")
        for _ in range(n):                      # last byte first, so an
            p -= 1                              # overlapping run works
            out[p] = out[p + off]
    section.data = bytes(out)
    return section.data


def read_dat(path):
    with open(path, "rb") as fh:
        blob = fh.read()
    secs = dat_sections(blob, path)
    for s in secs:
        if not s.checksum_ok:
            raise Refused("%s section %d: XOR checksum mismatch"
                          % (path, s.index))
        decompress(s)
        if len(s.data) != s.dec_size:
            raise Refused("%s section %d: %d bytes out, header says %d"
                          % (path, s.index, len(s.data), s.dec_size))
    used = sum(s.comp_total for s in secs)
    if used != len(blob):
        raise Refused("%s: %d bytes of %d consumed by %d sections"
                      % (path, used, len(blob), len(secs)))
    return secs


# ---------------------------------------------------------------------------
# 2. GROUNDxO.DAT - not compressed (GRND)
# ---------------------------------------------------------------------------

GROUND_SIZE = 1056


class ObjectInfo(object):
    __slots__ = ("index", "animation_flags", "start_frame", "end_frame",
                 "width", "height", "frame_data_size", "mask_offset",
                 "unknown1", "unknown2", "trigger_left", "trigger_top",
                 "trigger_width", "trigger_height", "trigger_effect",
                 "base_loc", "preview_image_index", "unknown3", "sound")

    @property
    def used(self):
        # SPEC.md 92.3.1: a non-zero record. GRND: "You can probably detect an
        # unused slot if the bitmap width or height is 0."
        return self.width != 0 and self.height != 0

    @property
    def frames(self):
        # GRND: end_animation_frame_index is in effect the frame count, which
        # is how C import_level.c:132 uses it.
        return self.end_frame


class TerrainInfo(object):
    __slots__ = ("index", "width", "height", "image_loc", "mask_loc",
                 "unknown1")

    @property
    def used(self):
        return self.width != 0 and self.height != 0


class Ground(object):
    __slots__ = ("objects", "terrain", "ega_custom", "ega_standard",
                 "ega_preview", "vga_custom", "vga_standard", "vga_preview")


def read_ground(path):
    with open(path, "rb") as fh:
        blob = fh.read()
    if len(blob) != GROUND_SIZE:
        raise Refused("%s is %d bytes, GRND says %d"
                      % (path, len(blob), GROUND_SIZE))
    g = Ground()
    g.objects = []
    # SPEC.md 92.3.1: little-endian. The readers only APPEAR to disagree -
    # Lemmings.ts's readWordBE() is little-endian and its readWord() is big,
    # so the names are inverted and the behaviour agrees. Confirmed by the
    # offsets summing to the section sizes exactly.
    for i in range(16):
        o = ObjectInfo()
        (o.animation_flags, o.start_frame, o.end_frame, o.width, o.height,
         o.frame_data_size, o.mask_offset, o.unknown1, o.unknown2,
         o.trigger_left, o.trigger_top, o.trigger_width, o.trigger_height,
         o.trigger_effect, o.base_loc, o.preview_image_index, o.unknown3,
         o.sound) = struct.unpack_from("<HBBBBHHHHHHBBBHHHB", blob, i * 28)
        o.index = i
        g.objects.append(o)
    g.terrain = []
    for i in range(64):
        t = TerrainInfo()
        (t.width, t.height, t.image_loc, t.mask_loc,
         t.unknown1) = struct.unpack_from("<BBHHH", blob, 448 + i * 8)
        t.index = i
        g.terrain.append(t)
    p = 960
    g.ega_custom = list(blob[p:p + 8])
    g.ega_standard = list(blob[p + 8:p + 16])
    g.ega_preview = list(blob[p + 16:p + 24])
    p += 24
    g.vga_custom = blob[p:p + 24]
    g.vga_standard = blob[p + 24:p + 48]
    g.vga_preview = blob[p + 48:p + 72]
    return g


# ---------------------------------------------------------------------------
# 3. The 2,048-byte level record (LVL), with SPEC.md 92.3.1's rulings
# ---------------------------------------------------------------------------


class Level(object):
    __slots__ = ("release_rate", "num_lemmings", "num_to_rescue",
                 "time_limit", "skills", "start_x", "ground_set",
                 "extended_set", "superlemming", "objects", "terrain",
                 "steel", "name")


def parse_level(rec):
    """LVL, all multi-byte fields BIG-endian (C reads them byte<<8|byte)."""
    if len(rec) != LVL_SIZE:
        raise Refused("level record is %d bytes, LVL says 2048" % len(rec))
    L = Level()

    def u16(o):
        return (rec[o] << 8) | rec[o + 1]

    L.release_rate = u16(0x00)
    L.num_lemmings = u16(0x02)
    L.num_to_rescue = u16(0x04)
    L.time_limit = u16(0x06)                    # minutes
    # LVL 0x0008-0x0017, 2 bytes each, only the low byte used. The order is
    # climber, floater, bomber, blocker, builder, basher, miner, digger -
    # which is already the skill panel's order (LX Dos.Consts.pas:104-118
    # gives Slower, Faster, then those eight, then Pause and Nuke), so the
    # band stores them unpermuted.
    L.skills = [rec[0x09 + 2 * i] for i in range(8)]
    L.start_x = u16(0x18)
    L.ground_set = u16(0x1A)
    L.extended_set = u16(0x1C)                  # non-zero => VGASPEC(n-1)
    L.superlemming = (u16(0x1E) != 0)

    L.objects = []
    for i in range(32):
        b = 0x20 + 8 * i
        x = u16(b) - 16                         # LVL: 0x0010 = 0
        y = u16(b + 2)
        if y >= 0x8000:
            y -= 0x10000
        obj_id = u16(b + 4)
        flags = u16(b + 6)
        if flags == 0:                          # SPEC.md 92.3.1: a non-zero
            continue                            # record is what "in use" means
        L.objects.append((i, x, y, obj_id, flags))

    # SPEC.md 92.3.1: SKIP a slot whose x word is 0xFFFF, never BREAK. It is
    # Taxing 27 (`Call in the bomb squad`), whose slot 68 holds 0xFFFF22A6 and
    # whose slots 69-399 hold 327 further normal entries; breaking renders it
    # with 68 pieces instead of 395.
    L.terrain = []
    for i in range(400):
        v = struct.unpack_from(">I", rec, 0x120 + 4 * i)[0]
        if (v >> 16) == 0xFFFF:
            continue
        x = ((v >> 16) & 0x0FFF) - 16           # 12-bit mask (TS and Lemmix)
        yv = (v >> 7) & 0x01FF
        y = yv - (516 if yv >= 0x100 else 4)    # y >= 0x100 before -516
        L.terrain.append((i, x, y, v & 0x3F, (v >> 28) & 0x0F))

    # LVL steel: 32 slots of 4 bytes from 0x0760, the last byte always 0.
    # SPEC.md 92.3.1: x from the HIGH 9 bits of word 0, y from the low 7,
    # width the HIGH nibble of byte 2. Lemmings.ts is wrong here.
    L.steel = []
    for i in range(32):
        raw = rec[0x760 + 4 * i: 0x760 + 4 * i + 4]
        if raw == b"\x00\x00\x00\x00":
            continue
        L.steel.append((i,
                        (((raw[0] << 1) | (raw[1] >> 7)) - 4) * 4,
                        (raw[1] & 0x7F) * 4,
                        ((raw[2] >> 4) + 1) * 4,
                        ((raw[2] & 0x0F) + 1) * 4))

    L.name = rec[0x7E0:0x800].decode("latin-1").rstrip()
    return L


# ---------------------------------------------------------------------------
# 4. ODDTABLE.DAT and the 120-level order
# ---------------------------------------------------------------------------
#
# SPEC.md 92.3.1: indexed by file*8 + section, the record's position on disk.
# A record is the first 24 bytes of an .LVL (rate, count, save, time, the 8
# skill counts) plus the 32-byte name written over 0x07E0
# (C import_level.c:429-431).

ODD_RECORD_SIZE = 56


def read_oddtable(path):
    with open(path, "rb") as fh:
        blob = fh.read()
    if len(blob) % ODD_RECORD_SIZE:
        raise Refused("%s is %d bytes, not a whole number of 56-byte records"
                      % (path, len(blob)))
    return [blob[i * ODD_RECORD_SIZE:(i + 1) * ODD_RECORD_SIZE]
            for i in range(len(blob) // ODD_RECORD_SIZE)]


def apply_oddtable(rec, odd):
    """Overwrite the first 24 bytes and the 32-byte name at 0x07E0."""
    out = bytearray(rec)
    out[0:24] = odd[0:24]
    out[0x7E0:0x800] = odd[24:56]
    return bytes(out)


# C gamespecific.c:172 position_of_classic_level[], via
#   #define LEVEL(file, section, modified) \
#       ((file)<<3 | (section) | ((modified) ? 0x80 : 0x00))
def _LEVEL(f, s, m):
    return (f << 3) | s | (0x80 if m else 0)


POSITION_OF_CLASSIC_LEVEL = [
    # Fun
    _LEVEL(9, 1, 0), _LEVEL(9, 5, 0), _LEVEL(9, 6, 0), _LEVEL(9, 2, 0),
    _LEVEL(9, 3, 0), _LEVEL(9, 4, 0), _LEVEL(9, 7, 0), _LEVEL(0, 6, 1),
    _LEVEL(1, 2, 1), _LEVEL(3, 2, 1), _LEVEL(4, 2, 1), _LEVEL(0, 7, 1),
    _LEVEL(1, 6, 0), _LEVEL(1, 7, 1), _LEVEL(2, 2, 1), _LEVEL(2, 4, 1),
    _LEVEL(2, 7, 1), _LEVEL(4, 3, 1), _LEVEL(5, 1, 1), _LEVEL(6, 3, 1),
    _LEVEL(8, 4, 1), _LEVEL(1, 3, 0), _LEVEL(4, 1, 1), _LEVEL(5, 7, 1),
    _LEVEL(6, 0, 1), _LEVEL(7, 1, 1), _LEVEL(4, 6, 1), _LEVEL(6, 1, 1),
    _LEVEL(6, 5, 1), _LEVEL(8, 2, 1),
    # Tricky
    _LEVEL(0, 0, 0), _LEVEL(1, 6, 1), _LEVEL(2, 1, 1), _LEVEL(3, 0, 1),
    _LEVEL(3, 1, 1), _LEVEL(3, 3, 1), _LEVEL(3, 4, 1), _LEVEL(4, 7, 1),
    _LEVEL(6, 2, 1), _LEVEL(7, 3, 1), _LEVEL(7, 7, 1), _LEVEL(8, 0, 1),
    _LEVEL(8, 3, 1), _LEVEL(0, 2, 0), _LEVEL(9, 1, 1), _LEVEL(9, 3, 1),
    _LEVEL(9, 4, 1), _LEVEL(9, 5, 1), _LEVEL(9, 7, 1), _LEVEL(0, 3, 0),
    _LEVEL(0, 5, 0), _LEVEL(0, 6, 0), _LEVEL(0, 7, 0), _LEVEL(1, 0, 0),
    _LEVEL(1, 1, 0), _LEVEL(1, 2, 0), _LEVEL(1, 4, 0), _LEVEL(1, 5, 0),
    _LEVEL(2, 0, 0), _LEVEL(1, 7, 0),
    # Taxing
    _LEVEL(2, 2, 0), _LEVEL(2, 3, 0), _LEVEL(2, 4, 0), _LEVEL(2, 5, 0),
    _LEVEL(2, 6, 0), _LEVEL(2, 7, 0), _LEVEL(3, 0, 0), _LEVEL(3, 1, 0),
    _LEVEL(3, 2, 0), _LEVEL(3, 3, 0), _LEVEL(3, 4, 0), _LEVEL(3, 5, 0),
    _LEVEL(3, 6, 0), _LEVEL(3, 7, 0), _LEVEL(0, 1, 0), _LEVEL(4, 0, 0),
    _LEVEL(4, 1, 0), _LEVEL(4, 2, 0), _LEVEL(4, 3, 0), _LEVEL(4, 4, 0),
    _LEVEL(4, 5, 0), _LEVEL(4, 6, 0), _LEVEL(4, 7, 0), _LEVEL(5, 0, 0),
    _LEVEL(5, 1, 0), _LEVEL(5, 2, 0), _LEVEL(5, 3, 0), _LEVEL(5, 4, 0),
    _LEVEL(2, 1, 0), _LEVEL(6, 7, 0),
    # Mayhem
    _LEVEL(5, 5, 0), _LEVEL(5, 6, 0), _LEVEL(5, 7, 0), _LEVEL(6, 0, 0),
    _LEVEL(6, 1, 0), _LEVEL(6, 2, 0), _LEVEL(6, 3, 0), _LEVEL(6, 4, 0),
    _LEVEL(6, 5, 0), _LEVEL(6, 6, 0), _LEVEL(6, 7, 1), _LEVEL(7, 0, 0),
    _LEVEL(7, 1, 0), _LEVEL(7, 2, 0), _LEVEL(7, 3, 0), _LEVEL(7, 4, 0),
    _LEVEL(7, 5, 0), _LEVEL(7, 6, 0), _LEVEL(7, 7, 0), _LEVEL(9, 2, 1),
    _LEVEL(8, 0, 0), _LEVEL(0, 4, 0), _LEVEL(8, 1, 0), _LEVEL(8, 2, 0),
    _LEVEL(8, 3, 0), _LEVEL(8, 4, 0), _LEVEL(8, 5, 0), _LEVEL(8, 6, 0),
    _LEVEL(8, 7, 0), _LEVEL(9, 0, 0),
]

# TS public/data/config.json "level.order", as fileId*10 + partIndex with a
# negative sign meaning "use the odd table" (TS level-index-resolve.ts:47-51).
# The independent second reading of the same 120 entries.
TS_LEVEL_ORDER = [
    [91, 95, 96, 92, 93, 94, 97, -6, -12, -32, -42, -7, 16, -17, -22, -24,
     -27, -43, -51, -63, -84, 13, -41, -57, -60, -71, -46, -61, -65, -82],
    [0, -16, -21, -30, -31, -33, -34, -47, -62, -73, -77, -80, -83, 2, -91,
     -93, -94, -95, -97, 3, 5, 6, 7, 10, 11, 12, 14, 15, 20, 17],
    [22, 23, 24, 25, 26, 27, 30, 31, 32, 33, 34, 35, 36, 37, 1, 40, 41, 42,
     43, 44, 45, 46, 47, 50, 51, 52, 53, 54, 21, 67],
    [55, 56, 57, 60, 61, 62, 63, 64, 65, 66, -67, 70, 71, 72, 73, 74, 75, 76,
     77, -92, 80, 4, 81, 82, 83, 84, 85, 86, 87, 90],
]


def decode_position(p):
    """C import_level.c:407-410."""
    return (p & 0x78) >> 3, p & 0x07, bool(p & 0x80)


def check_level_order():
    """The two reference tables must agree on all 120, entry for entry."""
    bad = []
    for rating in range(4):
        for i in range(30):
            cf, cn, cm = decode_position(POSITION_OF_CLASSIC_LEVEL[rating * 30 + i])
            t = TS_LEVEL_ORDER[rating][i]
            tf, tn, tm = abs(t) // 10, abs(t) % 10, t < 0
            if (cf, cn, cm) != (tf, tn, tm):
                bad.append("%s %d: C %d/%d/%s, TS %d/%d/%s"
                           % (RATINGS[rating], i + 1, cf, cn, cm, tf, tn, tm))
    return bad


# ---------------------------------------------------------------------------
# 5. VGASPECx.DAT - one DAT section, then SPEC's byte RLE
# ---------------------------------------------------------------------------


def decompress_rle(buf, pos):
    """SPEC: 0x00-0x7F = (n+1) raw bytes follow; 0x81-0xFF = (257-n) copies of
    the next byte; 0x80 ends a chunk."""
    out = bytearray()
    while pos < len(buf):
        c = buf[pos]
        if c == 0x80:
            return bytes(out), pos + 1
        if c < 0x80:
            n = c + 1
            out += buf[pos + 1: pos + 1 + n]
            pos += 1 + n
        else:
            n = 257 - c
            out += bytes([buf[pos + 1]]) * n
            pos += 2
    return bytes(out), pos


def read_vgaspec(path):
    secs = read_dat(path)
    if len(secs) != 1:
        raise Refused("%s has %d DAT sections, SPEC says 1"
                      % (path, len(secs)))
    data = secs[0].data
    chunks = []
    pos = SPEC_PALETTE_BYTES
    while pos < len(data) and len(chunks) < 4:
        chunk, pos = decompress_rle(data, pos)
        chunks.append(chunk)
    if len(chunks) != 4 or any(len(c) != SPEC_CHUNK_BYTES for c in chunks):
        raise Refused("%s: %d chunks of %s, SPEC says 4 of %d"
                      % (path, len(chunks), [len(c) for c in chunks],
                         SPEC_CHUNK_BYTES))
    if pos != len(data):
        raise Refused("%s: %d bytes left after the fourth chunk"
                      % (path, len(data) - pos))
    return data[0:24], data[24:40], chunks


def spec_planes(chunks):
    """Assemble the four 19,200-byte planes of a 960x160 picture.

    Each chunk is 40 rows as THREE consecutive planes of 960*40/8 = 4,800
    bytes. Plane 3 is built here as the OR of the other three, which is C
    import_level.c:590-592's missing fourth plane: the mask is "any non-zero
    pixel", and with it the stored nibble is the mode-0Dh colour directly
    under the 0 -> 0, 1-7 -> 9-15 mapping.
    """
    band = SPEC_W * 40 // 8                     # 4,800
    planes = [bytearray(), bytearray(), bytearray(), bytearray()]
    for chunk in chunks:
        for p in range(3):
            planes[p] += chunk[p * band:(p + 1) * band]
        planes[3] += bytes(a | b | c for (a, b, c) in
                           zip(chunk[0:band], chunk[band:2 * band],
                               chunk[2 * band:3 * band]))
    for p in planes:
        assert len(p) == SPEC_W * SPEC_H // 8
    return b"".join(bytes(p) for p in planes)


# ---------------------------------------------------------------------------
# 6. MAIN.DAT - the frame tables (MAIN sections 0 and 1)
# ---------------------------------------------------------------------------
#
# (name, frames, w, h, bpp). MAIN section 3 gives section 0's offsets and they
# are the running sum of frames*w*h*bpp/8, which proves planar frames carry no
# row padding. The 30-entry order is Lemmings.ts's own sequential registration
# list (TS lemmings-sprite.ts:22-49), cross-checked against Lemmix
# Styles.Base.pas TLemmingAnimationSet.InitMetadata.

MAIN_ANIMS = [
    ("walking-r", 8, 16, 10, 2), ("jumping-r", 1, 16, 10, 2),
    ("walking-l", 8, 16, 10, 2), ("jumping-l", 1, 16, 10, 2),
    ("digging", 16, 16, 14, 3), ("climbing-r", 8, 16, 12, 2),
    ("climbing-l", 8, 16, 12, 2), ("drowning", 16, 16, 10, 2),
    ("postclimb-r", 8, 16, 12, 2), ("postclimb-l", 8, 16, 12, 2),
    ("building-r", 16, 16, 13, 3), ("building-l", 16, 16, 13, 3),
    ("bashing-r", 32, 16, 10, 3), ("bashing-l", 32, 16, 10, 3),
    ("mining-r", 24, 16, 13, 3), ("mining-l", 24, 16, 13, 3),
    ("falling-r", 4, 16, 10, 2), ("falling-l", 4, 16, 10, 2),
    ("preumbrella-r", 4, 16, 16, 3), ("umbrella-r", 4, 16, 16, 3),
    ("preumbrella-l", 4, 16, 16, 3), ("umbrella-l", 4, 16, 16, 3),
    ("splatting", 16, 16, 10, 2), ("exiting", 8, 16, 13, 2),
    ("fried", 14, 16, 14, 4), ("blocking", 16, 16, 10, 2),
    ("shrugging-r", 8, 16, 10, 2), ("shrugging-l", 8, 16, 10, 2),
    ("ohnoing", 16, 16, 10, 2), ("explosion", 1, 32, 32, 3),
]

MAIN_MASKS = [
    ("bashmask-r", 4, 16, 10, 1), ("bashmask-l", 4, 16, 10, 1),
    ("minemask-r", 2, 16, 13, 1), ("minemask-l", 2, 16, 13, 1),
    ("explodemask", 1, 16, 22, 1), ("countdown", 10, 8, 8, 1),
]

# MAIN section 3's own offset table for section 0, transcribed from the doc.
# selfcheck() reproduces it by summing the frame table, which is the check
# that "no row padding" is a measurement rather than an assumption.
MAIN_ANIM_DOC_OFFSETS = [
    0x0000, 0x0140, 0x0168, 0x02A8, 0x02D0, 0x0810, 0x0990, 0x0B10, 0x0D90,
    0x0F10, 0x1090, 0x1570, 0x1A50, 0x21D0, 0x2950, 0x30A0, 0x37F0, 0x3890,
    0x3930, 0x3AB0, 0x3C30, 0x3DB0, 0x3F30, 0x41B0, 0x4350, 0x4970, 0x4BF0,
    0x4D30, 0x4E70, 0x50F0,
]

# MAIN section 7: the four rating signs live in section 4 in the REVERSE of
# the name order. Stored here in name order so the reader indexes by rating.
SIGN_OFFSETS_MAIN_ORDER = [0x65E4, 0x6218, 0x5E4C, 0x5A80]   # Fun..Mayhem
SIGN_W, SIGN_H, SIGN_BPP = 72, 27, 4
PURPLE_OFF, PURPLE_N = 0x69B0, 94
PURPLE_W, PURPLE_H, PURPLE_BPP = 16, 16, 3
STATUS_OFF, STATUS_N = 0x1900, 38
STATUS_W, STATUS_H, STATUS_BPP = 8, 16, 3
DIGIT_OFF, DIGIT_N = 0x1900, 20
DIGIT_W, DIGIT_H, DIGIT_BPP = 8, 8, 1
PANEL_W, PANEL_H, PANEL_BPP = 320, 40, 4
BROWN_W, BROWN_H, BROWN_BPP = 320, 104, 2


def frame_bytes(w, h, bpp):
    """Lemmings planar bitmaps have NO row padding: one plane is w*h bits."""
    return w * h * bpp // 8


# ---------------------------------------------------------------------------
# 7. The string table
# ---------------------------------------------------------------------------
#
# SPEC.md 92.3: "the literals ... live in a 512-padded band that lem_str()
# reads into a small bss scratch. They are in the DATA, not in the package
# image, and that is what makes 92.6's budget close."
#
# SPEC.md 92.2 on the posture: the original's on-screen wording is lifted
# VERBATIM, because a paraphrase produces a knock-off. Each row names the
# reference file that defines it (SPEC.md 92.1's authority table). CR inside a
# string is 0x0D and separates the two lines of a result text.
#
# The ids are GENERATED into build/lemstr.h, so apps/lemmings and this table
# cannot drift: a renamed string is a compile error, not a wrong sentence on
# the glass.

CR = "\r"

# (id name, text, the reference that defines it)
STRINGS = [
    ("NONE", "",
     "the sentinel: a level entry's `missing` is 0 when the level is playable"),

    # --- the About card, and what the launcher calls itself ---------------
    # SPEC.md 92.1: the About card names the two principals and points at
    # README.TXT, because the eight-line SCredits block plus product, version
    # and a port line is already past the twelve-row ceiling (SPEC.md 73.12).
    ("PROD", "Lemmings", "SPEC.md 92 - the product"),
    ("PORTLINE",
     "A native reimplementation for the 8086, built from the original's own "
     "data files.",
     "this port's own text (SPEC.md 92, the port line)"),
    ("CREDIT1", "Lemmings By DMA Design",
     "Lemmix src/Base.Strings.pas SCredits, the 'Original credits...' block"),
    ("CREDIT2", "Programming By Russell Kay",
     "Lemmix src/Base.Strings.pas SCredits, the 'Original credits...' block"),
    # THE RIGHTSHOLDER IS NAMED ON THE CARD AND NOT ONLY IN README.TXT. The
    # card is capped at twelve rows (SPEC.md 92.1) and uses nine, one of them a
    # blank spacer, so the row costs the reader nothing and the CGA card - which
    # is the narrow one - does not grow. apps/cword/cwovl.c puts "Copyright (C)
    # Microsoft Corporation" on cword's card and this port's licence posture is
    # cword's (SPEC.md 92.2, decision 4): the credits are lifted verbatim and
    # the attribution is on the glass, not only beside it.
    ("COPYRIGHT", "Copyright 1991 Psygnosis Ltd.",
     "Lemmix src/Base.Strings.pas SCredits, the 'Original credits...' block"),
    ("README", "See README.TXT for the full credits.",
     "this port's own text; SPEC.md 92.1's About row and SPEC.md 92.2's "
     "attribution posture"),

    # --- the four rating names --------------------------------------------
    # Lemmix src/Styles.Dos.pas TDosOrigLevelSystem SectionTable, and
    # Lemmings.ts config.json level.groups. Both give these four words.
    ("RATING0", "Fun", "Lemmix src/Styles.Dos.pas SectionTable"),
    ("RATING1", "Tricky", "Lemmix src/Styles.Dos.pas SectionTable"),
    ("RATING2", "Taxing", "Lemmix src/Styles.Dos.pas SectionTable"),
    ("RATING3", "Mayhem", "Lemmix src/Styles.Dos.pas SectionTable"),

    # --- the five graphic set names, for the preview's Style line ---------
    # LVL 0x001A's own list: "0x0000 is dirt, 0x0001 is fire, 0x0002 is
    # squasher, 0x0003 is pillar, 0x0004 is crystal". Neither Lemmix nor
    # Lemmings.ts names the DOS sets at all, so the LVL document is the only
    # reference there is - but its case is not: it is prose, and this is a
    # VALUE beside "Rating Fun" on the same pane and beside the same five
    # names in LEVELS.TXT, which this same converter writes capitalised.
    # "Style dirt" over "Rating Fun" was the port disagreeing with itself.
    #
    # The line itself is a stated divergence from Lemmix, which puts the
    # style PACK's description there (GameScreen.Preview.pas
    # StyleDecriptor = Style.StyleInformation.Description). For the DOS
    # original that is "Lemmings" on all 120 rows and says nothing, so this
    # port carries the GRAPHIC SET instead (SPEC.md 92.1's Style row).
    ("STYLE0", "Dirt", "lemmings_lvl_file_format.txt, bytes 0x001A-0x001B"),
    ("STYLE1", "Fire", "lemmings_lvl_file_format.txt, bytes 0x001A-0x001B"),
    ("STYLE2", "Squasher", "lemmings_lvl_file_format.txt, bytes 0x001A-0x001B"),
    ("STYLE3", "Pillar", "lemmings_lvl_file_format.txt, bytes 0x001A-0x001B"),
    ("STYLE4", "Crystal", "lemmings_lvl_file_format.txt, bytes 0x001A-0x001B"),

    # --- the preview screen's eight format strings ------------------------
    # Lemmix src/Base.Strings.pas:411-418, verbatim. The layout is
    # src/GameScreen.Preview.pas GetScreenLinesAndColors - 16 lines 16px
    # apart from y=82, the ten-space indent on the value rows, line 15
    # centred.
    ("PV_LEVEL", "Level %s %s",
     "Lemmix src/Base.Strings.pas SPreviewScreen_Level_ss"),
    ("PV_NUMBER", "Number of Lemmings %s",
     "Lemmix src/Base.Strings.pas SPreviewScreen_NumberOfLemmings_s"),
    ("PV_SAVED", "%s To Be Saved",
     "Lemmix src/Base.Strings.pas SPreviewScreen_ToBeSaved_s"),
    ("PV_RATE", "Release Rate %s",
     "Lemmix src/Base.Strings.pas SPreviewScreen_ReleaseRate_s"),
    ("PV_TIME", "Time %s Minutes",
     "Lemmix src/Base.Strings.pas SPreviewScreen_Time_s"),
    ("PV_RATING", "Rating %s",
     "Lemmix src/Base.Strings.pas SPreviewScreen_Rating_s"),
    ("PV_STYLE", "Style %s",
     "Lemmix src/Base.Strings.pas SPreviewScreen_Style_s"),
    ("PV_PRESS", "Press mouse button to continue",
     "Lemmix src/Base.Strings.pas SPreviewScreen_PressMouseButtonToContinue"),
    ("PV_INDENT", "          ",
     "Lemmix src/GameScreen.Preview.pas IndentStr - the ten spaces the value "
     "rows are indented by"),

    # --- the launcher's four state rows, and Play -------------------------
    # SPEC.md 92: "four state rows (Mode / Music / Level Code / Save
    # Progress) that exist so 47's greying has a control to sit on".
    ("ROW_MODE", "Mode", "SPEC.md 92 - the launcher's state rows"),
    ("ROW_MUSIC", "Music", "SPEC.md 92 - the launcher's state rows"),
    ("ROW_CODE", "Level Code...",
     "SPEC.md 92 - the launcher's state rows. SPEC writes it with an "
     "ellipsis character; three full stops is what latin-1 and the OS font "
     "have"),
    ("ROW_SAVE", "Save Progress", "SPEC.md 92 - the launcher's state rows"),
    ("PLAY", "Play", "SPEC.md 92 - the Play action"),

    # --- every greying fact in SPEC.md 92.8's table ------------------------
    # SPEC.md 47: grey a FACT, never a guess. Taken from the table's own
    # cells with two mechanical substitutions and nothing else: an em dash
    # becomes " - " because latin-1 has no em dash, and the markdown code
    # spans lose their backticks. Where a cell carries a sentence about the
    # port's INTERNALS rather than about the control, that sentence is left
    # out and this comment says which.
    ("GREY_MUSIC",
     "ADLIB.DAT is a compiled x86 sound driver plus its data, not a score - "
     "the game installs an interrupt vector at its offset 0 and calls into "
     "it - and there is no FM score path in this OS. It is also not in the "
     "26-file data set this port fetches at all.",
     "SPEC.md 92.8, the greyed table, Music - the whole cell"),
    ("GREY_CODE",
     "The original's access-code algorithm lives in the game's executable, "
     "not in any of its data files. Lemmix's ten-letter code is Lemmix's "
     "own - an MD5 of the 2048-byte record - and is not the code a 1991 "
     "player knows. Progress is kept in SYSTEM/APPDATA/LEMMINGS.SAV "
     "instead.",
     "SPEC.md 92.8, the greyed table, Level Code. The cell's last sentence "
     "is about the postview's own AddLineFeed and not about this row, so it "
     "is not in the string"),
    ("GREY_MODE",
     "kernel/fsx.inc gives an EGA 0x000F - the CGA-compatible modes only - "
     "so mode 0Dh has no FSXM_* id on that row and a sixteen-colour card "
     "gets four colours. The bit that greys the row is the bit fsx_mode "
     "would refuse on.",
     "SPEC.md 92.8, the greyed table, the 320x200x16 mode - the whole cell"),
    # ...AND THE SAME ROW IS GREYED ON TWO OTHER CARDS, FOR TWO OTHER
    # REASONS. SPEC.md 47's rule is that a greyed control names the fact the
    # ACTION would refuse on, and the fact above is about an EGA: shown on a
    # CGA it says a sixteen-colour card gets four colours, which is false
    # about the hardware in front of the reader. apps/lemmings/lemtab.c picks
    # the row from the adapter kind fsx_caps answered with.
    ("GREY_MODE_CGA",
     "kernel/fsx.inc gives a CGA 0x000F - the four modes an IBM CGA has - so "
     "mode 0Dh has no FSXM_* id on that row. 320x200 is four colours on this "
     "card because that is what the card is, not because anything was taken "
     "away. The bit that greys the row is the bit fsx_mode would refuse on.",
     "SPEC.md 92.8, the greyed table, the 320x200x16 mode, read off "
     "kernel/fsx.inc's fsx_capstab CGA row (0x000F)"),
    ("GREY_MODE_HERC",
     "kernel/fsx.inc gives a Hercules 0x0011 - mode 7's text page and its own "
     "720x348 graphics - so mode 0Dh has no FSXM_* id on that row. This card "
     "has one bit per pixel and no colour at all, and the game is drawn "
     "through the shadow backend instead. The bit that greys the row is the "
     "bit fsx_mode would refuse on.",
     "SPEC.md 92.8, the greyed table, the 320x200x16 mode, read off "
     "kernel/fsx.inc's fsx_capstab Hercules row (0x0011)"),
    ("GREY_KEYS",
     "An 83-key XT keyboard has no F11 or F12 key, and its Pause is "
     "Ctrl-NumLock, which the BIOS spins on internally and never returns.",
     "SPEC.md 92.8, the greyed table, F11 / F12 / Pause - the whole cell "
     "but its cross-reference"),
    ("GREY_EGA",
     "The original's screens are 640x350 and FSXM_CGA640 is 640x200, which "
     "holds the 40-character lines at their true width but 12 of the 22 "
     "rows - so the blank-line spacing is compressed out.",
     "SPEC.md 92.8, the greyed table, the preview and postview LAYOUT on "
     "CGA and EGA - the whole cell but its cross-reference"),
    ("GREY_SAVE",
     "The live CD cannot be written; the session is played and the result "
     "is shown, and nothing is recorded.",
     "SPEC.md 92.8, the greyed table, Save Progress on a read-only medium - "
     "the whole cell but its cross-reference"),
    ("GREY_SOUND",
     "os88_snd_caps() answered no tone capability - the same predicate "
     "os88_snd_tone would refuse on.",
     "SPEC.md 92.8, the greyed table, sound - the whole cell"),

    # --- wave 2: the two refusals a LAUNCH can make -----------------------
    # Both are said IN A WINDOW, before SPEC.md 53's bracket is entered,
    # because a refusal cannot be said in a foreign mode (SPEC.md 92.5) and
    # a kernel toast in one paints desktop geometry into the game's
    # framebuffer. NO_MEM is a TEMPLATE with two %s markers, in the order
    # the comment gives; NO_BANK takes one.
    ("NO_MEM",
     "Not enough memory: this level needs %s KB and %s KB is free.",
     "SPEC.md 92.6.1's claim arithmetic, said on the glass (WEAVE-SPEC 1.4's "
     "precedent). The two markers are the total this adapter's claims come "
     "to and os88_mem_largest_kb(). IT IS SHORT ON PURPOSE: this one is said "
     "as a TOAST, which is an inverse-video strip at the right end of the "
     "menu bar (SPEC.md 59) - the medium is a line and not a paragraph, so "
     "the sentence carries the fact and the arithmetic and nothing else"),
    ("NO_BANK",
     "%s could not be read - it has to be in this folder.",
     "SPEC.md 19.2.1 and 73.14: an overlay and its data are resolved in the "
     "LAUNCHING instance's own directory. The one marker is the band file's "
     "name, and it is a toast for NO_MEM's reason"),
    # ...and the THIRD, which used to borrow the Mode row's sentence and so
    # said the opposite of the refusal: on a Hercules that string ends "the
    # game is drawn through the shadow backend instead", i.e. it told the
    # reader the game DOES play, offered as the reason it had just refused to.
    # SPEC.md 47's rule is that a refusal names the fact the ACTION refused
    # on, and the action here is os88_fsx_mode() answering no.
    ("NO_MODE",
     "This display cannot be borrowed for the game. os88_fsx_mode() refused "
     "the graphics mode this adapter would have played in - the same bit "
     "os88_fsx_caps() answers with, and the same one that greys the mode row "
     "on the level list. Nothing was loaded and nothing was changed.",
     "SPEC.md 47 and 53.1: the launch's own refusal, named on the slot that "
     "refused rather than borrowed from the greyed Mode row (which is about "
     "COLOUR and, on a Hercules, says the game plays)"),
    # The one word the status line carries while a level composes. It is
    # CAPITALS because MAIN.DAT's status font has no lowercase glyph at all
    # (lemfont.inc), and short because the field Lemmix writes it in is 14
    # cells (Game.SkillPanel.pas:501).
    ("LOADING",
     "LOADING",
     "SPEC.md 92.7.1 conclusion 2: composing a level is ~25 s on an XT and "
     "inside the bracket nothing can be said - so it is said in the game's "
     "own font, on the panel, before the wait starts"),

    # --- the three "not on this disk" templates ---------------------------
    # A level entry's `missing` byte names one of these three and nothing
    # else. They are TEMPLATES: %s is the only marker and the package
    # substitutes it by hand, in the order the comment gives. Every number
    # they need is in LEMMAN.LEM's cost table, which is there for them.
    ("MISS_STYLE",
     "%s is not on a %s disk: the %s graphic set is %s bytes = %s clusters "
     "of this disk's %s, with %s already spent.",
     "SPEC.md 92.8's own example sentence, one row down. The seven markers "
     "are: the band's file name, the geometry (LEMMAN.LEM offset 22), the "
     "style name (LEMS_STYLE0..4), the bank's bytes and clusters (LEMMAN's "
     "cost table rows 0-4), this disk's clusters (LEMMAN offset 56) and the "
     "clusters already spent (LEMMAN offset 20)"),
    ("MISS_SPEC",
     "%s is not on a %s disk: the four special pictures are %s bytes = %s "
     "clusters of this disk's %s, with %s already spent.",
     "SPEC.md 92.8's own example sentence, verbatim in shape. The six "
     "markers are: the band's file name, the geometry, all four pictures' "
     "bytes and clusters (LEMMAN offsets 132 and 136), this disk's clusters "
     "and the clusters already spent"),
    ("MISS_ROOM",
     "This level's own record is not on a %s disk: the graphic sets and the "
     "levels that fit took %s of this disk's %s clusters, and a group of "
     "eight more records costs %s.",
     "the third reason, which SPEC.md 92.8's table does not have a row for "
     "because 92.9 states it as the 360KB rule instead: two graphic sets "
     "and ONLY THE LEVELS THAT USE THEM. Some of those levels still do not "
     "fit, and a level dropped for room is a different fact from a level "
     "whose graphic set is absent. The four markers are: the geometry, the "
     "clusters spent (LEMMAN offset 20), this disk's clusters (offset 56) "
     "and a level group's cost (offset 58)"),

    # --- the postview screen (waves 3 and 4) ------------------------------
    # Lemmix src/Base.Strings.pas:423-460; layout src/GameScreen.Postview.pas
    # GetScreenText - all lines centred, the first at y=16, 16px pitch.
    ("PO_TIMEUP", "Your time is up!",
     "Lemmix src/Base.Strings.pas SPostviewScreen_YourTimeIsUp"),
    ("PO_ACCOUNTEDFOR", "All lemmings accounted for.",
     "Lemmix src/Base.Strings.pas SPostviewScreen_AllLemmingsAccountedFor"),
    ("PO_YOURESCUED", "You rescued %s",
     "Lemmix src/Base.Strings.pas SPostviewScreen_YouRescued_s"),
    ("PO_YOUNEEDED", "You needed  %s",
     "Lemmix src/Base.Strings.pas SPostviewScreen_YouNeeded_s"),

    # The nine result texts, and the tier that earns each (Lemmix
    # src/GameScreen.Postview.pas GetResultText): 100 -> 8; 0 -> 0;
    # < target/2 -> 1; < target-5 -> 2; < target-1 -> 3; = target-1 -> 4;
    # = target -> 5; < target+20 -> 6; else 7. Lemmix wins over
    # lemmings_3ds/src/ingame.c show_result, whose thresholds differ.
    # PO_RESULT3 CARRIES A LITERAL PER CENT SIGN - "that few % extra" - which
    # is the original's own wording and is not a marker: the sequence is
    # "% e", never "%s".
    ("PO_RESULT0", "ROCK BOTTOM! I hope for your sake" + CR
     + "that you nuked that level.",
     "Lemmix src/Base.Strings.pas SPostviewScreen_Result0"),
    ("PO_RESULT1", "Better rethink your strategy before" + CR
     + "you try this level again!",
     "Lemmix src/Base.Strings.pas SPostviewScreen_Result1"),
    ("PO_RESULT2", "A little more practice on this level" + CR
     + "is definitely recommended.",
     "Lemmix src/Base.Strings.pas SPostviewScreen_Result2"),
    ("PO_RESULT3", "You got pretty close that time." + CR
     + "Now try again for that few % extra.",
     "Lemmix src/Base.Strings.pas SPostviewScreen_Result3"),
    ("PO_RESULT4", "OH NO, So near and yet so far (teehee)" + CR
     + "Maybe this time.....",
     "Lemmix src/Base.Strings.pas SPostviewScreen_Result4"),
    ("PO_RESULT5", "RIGHT ON. You can't get much closer" + CR
     + "than that. Let's try the next...",
     "Lemmix src/Base.Strings.pas SPostviewScreen_Result5"),
    ("PO_RESULT6", "That level seemed no problem to you on" + CR
     + "that attempt. Onto the next....",
     "Lemmix src/Base.Strings.pas SPostviewScreen_Result6"),
    ("PO_RESULT7", "You totally stormed that level!" + CR
     + "Let's see if you can storm the next...",
     "Lemmix src/Base.Strings.pas SPostviewScreen_Result7"),
    ("PO_RESULT8", "Superb! You rescued every lemmings on" + CR
     + "that level. Can you do it again....",
     "Lemmix src/Base.Strings.pas SPostviewScreen_Result8"),

    # The Mayhem 30 congratulation. Its middle line is exactly 40 characters
    # = 640 pixels of the purple font, which is the second confirmation of
    # the 640x350 surface (SPEC.md 92.1).
    ("PO_CONGRATS",
     CR + CR + "Congratulations!" + CR + CR + CR + CR + CR
     + "Everybody here at DMA Design salutes you" + CR
     + "as a MASTER Lemmings player. Not many" + CR
     + "people will complete the Mayhem levels," + CR
     + "you are definitely one of the elite" + CR
     + CR + CR + CR + CR + CR + "Now hold your breath for the data disk",
     "Lemmix src/Base.Strings.pas SPostviewScreen_CongratulationOrig"),

    # The footers. Lemmix src/GameScreen.Postview.pas:189 force-positions
    # them with AddLineFeed(18 - CountChar(CR)), so dropping the two
    # access-code lines does not move them (SPEC.md 92.8).
    ("PO_NEXTLEVEL", "Press left mouse button for next level",
     "Lemmix src/Base.Strings.pas SPostviewScreen_PressLeftMouseForNextLevel"),
    ("PO_RETRYLEVEL", "Press left mouse button to retry level",
     "Lemmix src/Base.Strings.pas SPostviewScreen_PressLeftMouseToRetryLevel"),
    ("PO_MENU", "Press right mouse button for menu",
     "Lemmix src/Base.Strings.pas SPostviewScreen_PressRightMouseForMenu"),
    ("PO_CONTINUE", "Press mouse button to continue",
     "Lemmix src/Base.Strings.pas SPostviewScreen_PressMouseToContinue"),

    # --- the in-game status line (wave 3) ---------------------------------
    # 40 characters. The five field WRITE offsets are 1-14 / 19-23 / 27-31 /
    # 36-37 / 39-40, from THE FIVE SETTERS at Lemmix
    # src/Game.SkillPanel.pas:501-563 and not from the comment block at
    # :250-255, which is one column off. A character outside the 38-glyph
    # set - the '.' and the '_' here included - draws an 8x16 BLACK CELL
    # rather than a glyph (src/Game.SkillPanel.pas DrawNewStr).
    ("STATUS_TEMPLATE", "..............OUT_.....IN_.....TIME_.-..",
     "Lemmix src/Base.Strings.pas:351 SGame_ToolBar_TextTemplate"),

    # --- the word shown for the lemming under the cursor (wave 3) ---------
    # Lemmix src/Game.pas:3645-3653 and the ORDER the tests run in: climber
    # and floater -> ATHLETE, climber -> CLIMBER, floater -> FLOATER, else
    # the action word. Lemmix wins over lemmings_3ds's
    # get_lemming_description, which tests the action first and calls a
    # shrugging climber BUILDER. Stored UPPERCASE because MAIN section 6's
    # font has no lowercase glyph; Lemmix's mixed case is undone by its own
    # UpCase at draw time.
    ("LEM_ATHLETE", "ATHLETE", "Lemmix src/Base.Strings.pas SAthlete"),
    ("LEM_CLIMBER", "CLIMBER", "Lemmix src/Base.Strings.pas SClimber"),
    ("LEM_FLOATER", "FLOATER", "Lemmix src/Base.Strings.pas SFloater"),
    ("LEM_WALKER", "WALKER", "Lemmix src/Base.Strings.pas SWalker"),
    ("LEM_JUMPER", "JUMPER", "Lemmix src/Base.Strings.pas SJumper"),
    ("LEM_DIGGER", "DIGGER", "Lemmix src/Base.Strings.pas SDigger"),
    ("LEM_DROWNER", "DROWNER", "Lemmix src/Base.Strings.pas SDrowner"),
    ("LEM_HOISTER", "HOISTER", "Lemmix src/Base.Strings.pas SHoister"),
    ("LEM_BUILDER", "BUILDER", "Lemmix src/Base.Strings.pas SBuilder"),
    ("LEM_BASHER", "BASHER", "Lemmix src/Base.Strings.pas SBasher"),
    ("LEM_MINER", "MINER", "Lemmix src/Base.Strings.pas SMiner"),
    ("LEM_FALLER", "FALLER", "Lemmix src/Base.Strings.pas SFaller"),
    ("LEM_SPLATTER", "SPLATTER", "Lemmix src/Base.Strings.pas SSplatter"),
    ("LEM_EXITER", "EXITER", "Lemmix src/Base.Strings.pas SExiter"),
    ("LEM_FRIER", "FRIER", "Lemmix src/Base.Strings.pas SVaporizer"),
    ("LEM_BLOCKER", "BLOCKER", "Lemmix src/Base.Strings.pas SBlocker"),
    ("LEM_SHRUGGER", "SHRUGGER", "Lemmix src/Base.Strings.pas SShrugger"),
    ("LEM_OHNOER", "OHNOER", "Lemmix src/Base.Strings.pas SOhnoer"),
    ("LEM_BOMBER", "BOMBER", "Lemmix src/Base.Strings.pas SExploder"),
]

STRING_NAMES = [row[0] for row in STRINGS]
assert len(set(STRING_NAMES)) == len(STRING_NAMES), "duplicate string id name"
assert STRING_NAMES[0] == "NONE", "id 0 must be the empty sentinel"

# id by name, for the three `missing` reasons and for the fixture.
SID = {name: i for (i, name) in enumerate(STRING_NAMES)}

# %s IS THE ONLY MARKER. The package substitutes it by hand - there is no
# printf in this C - so nothing else in any string may look like one. Checked
# here rather than trusted, because a string that grows a %d is a wrong
# sentence on the glass and nothing else would notice.
for _name, _text, _why in STRINGS:
    for _i in range(len(_text) - 1):
        if _text[_i] == "%":
            assert _text[_i + 1] in "s ", (
                "string %s has %%%s in it and %%s is the only marker the "
                "package substitutes" % (_name, _text[_i + 1]))


def build_string_band():
    """LEMSTR.LEM - the same bytes on every geometry.

        0   4  "LSTR"
        4   2  count
        6   2*count  offsets, each from the START OF THE FILE
       ...      the strings, in id order, each NUL-terminated

    There are no per-disk strings any more. A level entry's `missing` names
    LEMS_MISS_STYLE, LEMS_MISS_SPEC or LEMS_MISS_ROOM, which are TEMPLATES,
    and the numbers they need are in LEMMAN.LEM's cost table.
    """
    texts = [row[1] for row in STRINGS]
    body = bytearray()
    offsets = []
    base = 4 + 2 + 2 * len(texts)
    for t in texts:
        offsets.append(base + len(body))
        body += t.encode("latin-1") + b"\0"
    out = bytearray(b"LSTR")
    out += struct.pack("<H", len(texts))
    for off in offsets:
        out += struct.pack("<H", off)
    out += body
    if len(out) > 0xFFFF:
        raise Refused("LEMSTR.LEM is %d bytes and its offsets are words"
                      % len(out))
    # THE PACKAGE'S BUFFER IS THE REAL CEILING AND IT FAILS HERE, BY NAME.
    # apps/lemmings/lemmings.c reads this band WHOLE into LEM_STRBUF bytes of
    # bss and REFUSES a read that filled the buffer, because a band that
    # filled it may have been truncated and a truncated directory indexes
    # strings that are not there.  The band is written through pad512(), so
    # the sizes it can take are 512 apart; without this assertion the row of
    # this table that crosses the buffer would ship, and every LEMMINGS disk
    # in every geometry would come up with no level names, no rating tabs and
    # the sentence "The converted level data is not in this folder." - which
    # names the wrong cause, because the bands are all there.  Keep the number
    # equal to LEM_STRBUF in apps/lemmings/lemmings.c.
    LEM_STRBUF = 5120
    if len(pad512(bytes(out))) >= LEM_STRBUF:
        raise Refused(
            "LEMSTR.LEM is %d bytes padded to %d, and apps/lemmings reads it "
            "whole into LEM_STRBUF = %d bytes of bss: raise BOTH, or the "
            "package refuses its own band at launch"
            % (len(out), len(pad512(bytes(out))), LEM_STRBUF))
    return bytes(out), len(texts)


def write_lemstr_h(path):
    """build/lemstr.h - the ids apps/lemmings #includes."""
    lines = [
        "/* lemstr.h - GENERATED by tools/os88lem.py. Do not edit.",
        " *",
        " * The string ids in LEMSTR.LEM (docs/lemband-format.md, SPEC.md",
        " * 92.3). apps/lemmings #includes this so the band and the program",
        " * cannot drift: a renamed string is a compile error here, not a",
        " * wrong sentence on the glass.",
        " *",
        " * LEMS_COUNT is how many there are, and LEMSTR.LEM is the same",
        " * band on every geometry. A level entry's `missing` byte is one of",
        " * LEMS_MISS_STYLE, LEMS_MISS_SPEC and LEMS_MISS_ROOM, or 0; those",
        " * three are TEMPLATES whose only marker is %s, and every number",
        " * they need is in LEMMAN.LEM's cost table.",
        " */",
        "#ifndef LEMSTR_H",
        "#define LEMSTR_H",
        "",
    ]
    width = max(len(n) for n in STRING_NAMES) + 6
    for i, (name, _, why) in enumerate(STRINGS):
        lines.append("/* %s */" % why)
        lines.append("#define %-*s %d" % (width, "LEMS_" + name, i))
    lines.append("")
    lines.append("#define %-*s %d" % (width, "LEMS_COUNT", len(STRINGS)))
    lines.append("")
    lines.append("#endif /* LEMSTR_H */")
    text = "\n".join(lines) + "\n"
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(path, "w") as fh:
        fh.write(text)
    return text


# ---------------------------------------------------------------------------
# 8. Band files: the 64,512 cap, the parts, and the 512 pad
# ---------------------------------------------------------------------------

NAME_ALPHABET = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")


def check_name(name):
    """SPEC.md 92.3.2: every name uppercase 8.3 in [A-Z0-9_-]."""
    stem, dot, ext = name.partition(".")
    if not dot or not 1 <= len(stem) <= 8 or not 1 <= len(ext) <= 3:
        raise Refused("%r is not an 8.3 name" % name)
    for ch in stem + ext:
        if ch not in NAME_ALPHABET:
            raise Refused("%r has %r in it, and the alphabet is [A-Z0-9_-]"
                          % (name, ch))
    return name


def pad512(data):
    if len(data) % SECTOR:
        data = data + b"\0" * (SECTOR - len(data) % SECTOR)
    return data


def split_parts(logical):
    """Cut a logical bank into parts of at most FILEMAX bytes.

    FILEMAX is itself a multiple of 512 (64,512 = 126 sectors), so every part
    but the last is already aligned and only the last needs padding. That is
    what lets the package read part after part into ONE claim at successive
    512-aligned offsets with one os88_file_read_seg each (SPEC.md 92.3.2).
    """
    parts = [logical[i:i + FILEMAX] for i in range(0, len(logical), FILEMAX)]
    if not parts:
        parts = [b""]
    return [pad512(p) for p in parts]


def part_names(base, nparts):
    """A bank that fits in one part keeps its plain name; a bank that does not
    is numbered, which is docs/lemband-format.md's own spelling for the
    special pictures (LEMSP<n>_<part>.LEM)."""
    if nparts == 1:
        return [check_name(base + ".LEM")]
    return [check_name("%s_%d.LEM" % (base, i)) for i in range(nparts)]


class Disk(object):
    """The files one geometry carries, and the arithmetic over them."""

    def __init__(self, geom):
        self.geom = geom
        self.files = []                 # (name, bytes) in write order

    def add(self, name, data):
        check_name(name)
        if len(data) > FILEMAX:
            raise Refused("%s is %d bytes and WIRE_FILEMAX is %d"
                          % (name, len(data), FILEMAX))
        self.files.append((name, data))

    def add_bank(self, base, logical):
        parts = split_parts(logical)
        names = part_names(base, len(parts))
        for name, data in zip(names, parts):
            self.add(name, data)
        return len(parts)

    @property
    def nbytes(self):
        return sum(len(d) for (_, d) in self.files)

    def nclusters(self, extra_bytes=0):
        cb = cluster_bytes(self.geom)
        n = sum((len(d) + cb - 1) // cb for (_, d) in self.files)
        return n + (extra_bytes + cb - 1) // cb


# ---------------------------------------------------------------------------
# 9. Reading the whole set
# ---------------------------------------------------------------------------


class DataSet(object):
    """Everything decoded, once, so the four geometries share the work."""

    def __init__(self, src):
        self.src = src
        self.records = []               # 80 raw 2,048-byte records
        for f in range(10):
            secs = read_dat(os.path.join(src, "LEVEL%03d.DAT" % f))
            if len(secs) != 8:
                raise Refused("LEVEL%03d.DAT has %d sections, not 8"
                              % (f, len(secs)))
            for s in secs:
                if len(s.data) != LVL_SIZE:
                    raise Refused("LEVEL%03d.DAT section %d is %d bytes, not "
                                  "2048" % (f, s.index, len(s.data)))
                self.records.append(s.data)

        self.odd = read_oddtable(os.path.join(src, "ODDTABLE.DAT"))
        if len(self.odd) != 80:
            raise Refused("ODDTABLE.DAT has %d records; SPEC.md 92.3 says 80, "
                          "one per raw slot" % len(self.odd))

        self.grounds = [read_ground(os.path.join(src, "GROUND%dO.DAT" % s))
                        for s in range(5)]
        self.vgagr = []
        for s in range(5):
            secs = read_dat(os.path.join(src, "VGAGR%d.DAT" % s))
            if len(secs) != 2:
                raise Refused("VGAGR%d.DAT has %d sections, not 2"
                              % (s, len(secs)))
            self.vgagr.append((secs[0].data, secs[1].data))

        self.specs = [read_vgaspec(os.path.join(src, "VGASPEC%d.DAT" % i))
                      for i in range(4)]
        self.main = [s.data for s in read_dat(os.path.join(src, "MAIN.DAT"))]
        if len(self.main) != 7:
            raise Refused("MAIN.DAT has %d sections; MAIN says 7"
                          % len(self.main))

        self.levels = self._build_levels()
        self._banks = {}

    def _build_levels(self):
        """The 120 in play order, with ODDTABLE folded in."""
        bad = check_level_order()
        if bad:
            raise Refused("the two reference level orders disagree: "
                          + "; ".join(bad))
        out = []
        for i, pos in enumerate(POSITION_OF_CLASSIC_LEVEL):
            f, sec, modified = decode_position(pos)
            slot = f * 8 + sec              # SPEC.md 92.3.1: ODDTABLE index
            rec = self.records[slot]
            if modified:
                rec = apply_oddtable(rec, self.odd[slot])
            L = parse_level(rec)
            out.append({
                "index": i,
                "rating": i // 30,
                "number": i % 30,
                "slot": slot,
                "odd": modified,
                "record": rec,
                "level": L,
                "style": L.ground_set,
                "special": (L.extended_set - 1) if L.extended_set else None,
            })
        return out

    # --- the banks --------------------------------------------------------
    #
    # Memoised: the four geometries share the work, and choose_two_styles()
    # below trial-plans all ten pairs of graphic sets.

    def bank(self, kind, n):
        key = (kind, n)
        if key not in self._banks:
            self._banks[key] = {
                "style": self.style_bank,
                "spec": self.special_bank,
                "main": lambda _n: self.main_bank(),
            }[kind](n)
        return self._banks[key]

    def style_bank(self, s):
        """LEMGR<s>: palette, terrain pieces, object metadata and frames."""
        g = self.grounds[s]
        terr_src, obj_src = self.vgagr[s]
        head = bytearray(1024)
        head[0:4] = b"LGRB"
        struct.pack_into("<H", head, 4, 1)
        head[6] = s
        head[9] = 0                                 # parts, filled below
        struct.pack_into("<HHH", head, 14, 128, 640, 1024)
        head[32:56] = g.vga_custom
        head[56:80] = g.vga_standard
        head[80:104] = g.vga_preview

        data = bytearray()
        nterr = 0
        for t in g.terrain:
            row = 128 + t.index * 8
            if not t.used:
                continue
            nterr += 1
            # Four planes of w*h/8, plane 3 the mask - and the mask is FREE,
            # because mask_loc IS plane 3's location (GRND, and measured:
            # mask_loc - image_loc == w*h*3/8 for all 273 used pieces).
            n = t.width * t.height // 2
            if t.mask_loc - t.image_loc != t.width * t.height * 3 // 8:
                raise Refused("set %d terrain %d: mask_loc - image_loc is %d, "
                              "not w*h*3/8 = %d"
                              % (s, t.index, t.mask_loc - t.image_loc,
                                 t.width * t.height * 3 // 8))
            if t.image_loc + n > len(terr_src):
                raise Refused("set %d terrain %d runs past VGAGR section 0"
                              % (s, t.index))
            if t.width % 16:
                raise Refused("set %d terrain %d is %d wide; every width in "
                              "every set is a multiple of 16"
                              % (s, t.index, t.width))
            head[row] = t.width
            head[row + 1] = t.height
            struct.pack_into("<I", head, row + 2, 1024 + len(data))
            struct.pack_into("<H", head, row + 6, n)
            data += terr_src[t.image_loc:t.image_loc + n]

        nobj = 0
        for o in g.objects:
            row = 640 + o.index * 16
            if not o.used:
                continue
            nobj += 1
            # FIVE planes: four colour and a mask (measured -
            # frame_data_size == w*h*5/8 and mask_offset == w*h*4/8 within
            # the frame, for every used object in all five sets).
            per = o.width * o.height * 5 // 8
            if o.frame_data_size != per:
                raise Refused("set %d object %d: frame_data_size %d, not "
                              "w*h*5/8 = %d"
                              % (s, o.index, o.frame_data_size, per))
            if o.mask_offset != o.width * o.height * 4 // 8:
                raise Refused("set %d object %d: mask_offset %d, not w*h*4/8"
                              % (s, o.index, o.mask_offset))
            n = o.frames * per
            if o.base_loc + n > len(obj_src):
                raise Refused("set %d object %d runs past VGAGR section 1"
                              % (s, o.index))
            head[row] = o.width
            head[row + 1] = o.height
            head[row + 2] = o.frames
            head[row + 3] = o.trigger_effect
            struct.pack_into("<HH", head, row + 4,
                             o.trigger_left, o.trigger_top)
            head[row + 8] = o.trigger_width & 0xFF
            head[row + 9] = o.trigger_height & 0xFF
            head[row + 10] = o.sound
            head[row + 11] = o.start_frame
            struct.pack_into("<I", head, row + 12, 1024 + len(data))
            data += obj_src[o.base_loc:o.base_loc + n]

        head[7] = nterr
        head[8] = nobj
        logical = bytes(head) + bytes(data)
        struct.pack_into("<I", head, 10, len(logical))
        head[9] = (len(logical) + FILEMAX - 1) // FILEMAX
        return bytes(head) + bytes(data)

    def main_bank(self):
        """LEMMAIN: the MAIN.DAT-derived bank."""
        s0, s1, s2, s3, s4, _s5, s6 = self.main

        def reg_table(rows):
            out = bytearray()
            off = 0
            for (_name, frames, w, h, bpp) in rows:
                per = frame_bytes(w, h, bpp)
                out += struct.pack("<BBBBHH", frames, w, h, bpp, off, per)
                off += frames * per
            return bytes(out), off

        anim_tab, anim_len = reg_table(MAIN_ANIMS)
        mask_tab, mask_len = reg_table(MAIN_MASKS)
        if anim_len != len(s0):
            raise Refused("the animation table sums to %d and MAIN section 0 "
                          "is %d" % (anim_len, len(s0)))
        if mask_len != len(s1):
            raise Refused("the mask table sums to %d and MAIN section 1 is %d"
                          % (mask_len, len(s1)))

        panel = s6[0:frame_bytes(PANEL_W, PANEL_H, PANEL_BPP)]
        statfont = s6[STATUS_OFF:STATUS_OFF + STATUS_N
                      * frame_bytes(STATUS_W, STATUS_H, STATUS_BPP)]
        digits = s2[DIGIT_OFF:DIGIT_OFF + DIGIT_N
                    * frame_bytes(DIGIT_W, DIGIT_H, DIGIT_BPP)]
        signbytes = frame_bytes(SIGN_W, SIGN_H, SIGN_BPP)
        signs = b"".join(s4[o:o + signbytes] for o in SIGN_OFFSETS_MAIN_ORDER)
        purple = s4[PURPLE_OFF:PURPLE_OFF + PURPLE_N
                    * frame_bytes(PURPLE_W, PURPLE_H, PURPLE_BPP)]
        brown = s3[0:frame_bytes(BROWN_W, BROWN_H, BROWN_BPP)]

        items = [
            (0, PANEL_BPP, PANEL_W, PANEL_H, 1, panel),
            (1, STATUS_BPP, STATUS_W, STATUS_H, STATUS_N, statfont),
            (2, DIGIT_BPP, DIGIT_W, DIGIT_H, DIGIT_N, digits),
            (3, 0, 16, 0, len(MAIN_ANIMS), s0),
            (4, 1, 16, 0, len(MAIN_MASKS), s1),
            (5, SIGN_BPP, SIGN_W, SIGN_H, 4, signs),
            (6, PURPLE_BPP, PURPLE_W, PURPLE_H, PURPLE_N, purple),
            (7, BROWN_BPP, BROWN_W, BROWN_H, 1, brown),
            (8, 0, 8, 0, len(MAIN_ANIMS), anim_tab),
            (9, 0, 8, 0, len(MAIN_MASKS), mask_tab),
        ]
        for (item_id, _, _, _, _, blob) in items:
            if not blob:
                raise Refused("MAIN bank item %d came out empty" % item_id)

        head = bytearray(512)
        head[0:4] = b"LMNB"
        struct.pack_into("<H", head, 4, 1)
        head[6] = len(items)
        data = bytearray()
        for i, (item_id, bpp, w, h, frames, blob) in enumerate(items):
            row = 32 + i * 16
            head[row] = item_id
            head[row + 1] = bpp
            struct.pack_into("<HHH", head, row + 2, w, h, frames)
            struct.pack_into("<II", head, row + 8, 512 + len(data), len(blob))
            data += blob
        logical = bytes(head) + bytes(data)
        struct.pack_into("<I", head, 8, len(logical))
        head[7] = (len(logical) + FILEMAX - 1) // FILEMAX
        return bytes(head) + bytes(data)

    def special_bank(self, i):
        """LEMSP<i>: a 960x160 picture at 4bpp planar."""
        pal, ega, chunks = self.specs[i]
        pixels = spec_planes(chunks)
        if len(pixels) != SPEC_W * SPEC_H * 4 // 8:
            raise Refused("special %d assembled to %d bytes, not %d"
                          % (i, len(pixels), SPEC_W * SPEC_H * 4 // 8))
        head = bytearray(512)
        head[0:4] = b"LSPB"
        struct.pack_into("<H", head, 4, 1)
        head[6] = i
        struct.pack_into("<HH", head, 12, SPEC_W, SPEC_H)
        head[16] = 4
        head[32:56] = pal
        head[56:72] = ega
        logical = bytes(head) + pixels
        struct.pack_into("<I", head, 8, len(logical))
        head[7] = (len(logical) + FILEMAX - 1) // FILEMAX
        return bytes(head) + pixels


# ---------------------------------------------------------------------------
# 10. Planning one geometry
# ---------------------------------------------------------------------------


def plan_geometry(ds, geom, reserve=PKG_RESERVE, styles=None):
    """Decide what this disk carries, then build every file for it.

    SPEC.md 92.9, decided in CLUSTERS and not in bytes:
      1.44MB and 1.2MB  everything
      720KB             everything except the four VGASPEC pictures
      360KB             two graphic sets and only the levels that use them
    """
    clus = data_clusters(geom)
    cb = cluster_bytes(geom)

    specials = list(range(4))
    if styles is None:
        styles = list(range(5))
        if geom == 720:
            specials = []
        elif geom == 360:
            specials = []
            styles = choose_two_styles(ds, geom, reserve)
    elif geom in (720, 360):
        specials = []

    disk = Disk(geom)
    notes = []

    # --- the banks, so their cost is known before the levels --------------
    style_parts = [0] * 5
    for s in styles:
        style_parts[s] = disk.add_bank("LEMGR%d" % s, ds.bank("style", s))
    spec_parts = [0] * 4
    for i in specials:
        spec_parts[i] = disk.add_bank("LEMSP%d" % i, ds.bank("spec", i))
    disk.add_bank("LEMMAIN", ds.bank("main", 0))

    # --- which levels are playable here -----------------------------------
    # A level entry's `missing` names one of the three FIXED templates and
    # nothing else, so LEMSTR.LEM is the same band on every geometry and the
    # ids in build/lemstr.h never move. The numbers the templates want are
    # in LEMMAN.LEM's cost table.
    playable = []
    for lev in ds.levels:
        entry = dict(lev)
        entry["missing"] = 0
        if lev["special"] is not None and lev["special"] not in specials:
            entry["missing"] = SID["MISS_SPEC"]
        # A special level still needs its ground set's OBJECTS - the exit and
        # the entrances - so an absent style grounds it either way.
        if entry["missing"] == 0 and lev["style"] not in styles:
            entry["missing"] = SID["MISS_STYLE"]
        entry["playable"] = (entry["missing"] == 0)
        playable.append(entry)

    # --- the raw records, REPACKED eight to a file ------------------------
    # WHAT A LEVEL FILE HOLDS, and why ODDTABLE is not folded into it.
    # ODDTABLE overwrites a record's first 24 bytes and its 32-byte name -
    # the release rate, the counts, the time, the eight skills and the title.
    # It never touches the terrain, the objects or the steel. 40 of the 120
    # levels take that override, and 40 of the 80 raw slots are read BOTH
    # ways: plainly for one level and overridden for another. So the override
    # cannot live in the shared record - it would be the wrong header for
    # whichever of the two read it second - and folding it in by duplicating
    # the record would cost 40 x 2,048 = 81,920 bytes, which on a 720KB disk
    # is 80 of its 713 clusters.
    #
    # It is folded into the MANIFEST instead, which is where every one of
    # those fields already lives: the 64-byte level entry carries the rate,
    # the counts, the time, the skills and the name with the override already
    # applied, and the machine reads them there. The record on the disk is
    # read for its terrain, its objects and its steel, and those are the same
    # bytes either way. So the machine still never sees ODDTABLE.DAT and never
    # learns its rule (SPEC.md 92.3), which is the whole of what that
    # sentence asks for.
    #
    # The records are packed per geometry: a 360KB disk carries two graphic
    # sets and would otherwise spend 160 clusters carrying 80 records for the
    # levels it can run.
    def pack_records():
        order, seen = [], set()
        for e in playable:
            if e["playable"] and e["slot"] not in seen:
                seen.add(e["slot"])
                order.append(e["slot"])
        return order

    def level_clusters(n):
        nfiles = (n + LEVELS_PER_FILE - 1) // LEVELS_PER_FILE
        per = (LEVELS_PER_FILE * LVL_SIZE + cb - 1) // cb
        return nfiles, nfiles * per

    # LEMMAN (a 512-byte header), the four rating files (2,048 each) and
    # LEMSTR, whose sizes are all known before the loop.
    head_clusters = ((MANIFEST_HEADER + cb - 1) // cb
                     + 4 * ((RATING_FILE + cb - 1) // cb)
                     + (len(build_string_band()[0]) + cb - 1) // cb)

    while True:
        order = pack_records()
        nlevfiles, lev_clusters = level_clusters(len(order))
        if nlevfiles > 10:
            raise Refused("%d level records need %d LEMLVn.LEM files and the "
                          "name has one digit" % (len(order), nlevfiles))
        if disk.nclusters(reserve) + lev_clusters + head_clusters <= clus:
            break
        live = [e for e in playable if e["playable"]]
        if not live:
            raise Refused("a %s disk cannot hold the banks at all: %d "
                          "clusters of %d before a single level"
                          % (geom_name(geom), disk.nclusters(reserve), clus))
        # Drop from the END of the play order, so what survives is a run the
        # player starts at Fun 1 and works through.
        victim = live[-1]
        victim["playable"] = False
        victim["missing"] = SID["MISS_ROOM"]

    slot_at = {slot: i for (i, slot) in enumerate(order)}
    for f in range((len(order) + LEVELS_PER_FILE - 1) // LEVELS_PER_FILE):
        chunk = order[f * LEVELS_PER_FILE:(f + 1) * LEVELS_PER_FILE]
        blob = b"".join(ds.records[slot] for slot in chunk)
        blob += b"\0" * (LEVELS_PER_FILE * LVL_SIZE - len(blob))
        disk.add("LEMLV%d.LEM" % f, blob)
    nlevfiles = (len(order) + LEVELS_PER_FILE - 1) // LEVELS_PER_FILE

    for e in playable:
        if e["playable"]:
            idx = slot_at[e["slot"]]
            e["rawfile"] = idx // LEVELS_PER_FILE
            e["rawsect"] = idx % LEVELS_PER_FILE
        else:
            e["rawfile"] = 0xFF
            e["rawsect"] = 0xFF

    # --- one index file per RATING (docs/lemband-format.md) ---------------
    # Not a layout preference: os88_file_read_at() is the only slot that
    # reads PART of a file and its offset and cap must each be a whole
    # number of CLUSTERS, which is 512 bytes here, 1,024 there and something
    # else again on the RAM disk The Wire unpacks into. One rating out of one
    # index cannot be expressed under that rule on every medium; a whole
    # small file read with os88_file_read() has no alignment rule at all.
    for rating in range(4):
        rows = [e for e in playable if e["rating"] == rating]
        if len(rows) != LEVELS_PER_RATING:
            raise Refused("rating %d has %d levels, not %d"
                          % (rating, len(rows), LEVELS_PER_RATING))
        body = b"".join(level_entry(e) for e in rows)
        # 30 x 64 = 1,920, padded to 2,048 - a fixed size on every disk, so
        # the package's bss for one rating is a constant.
        disk.add("LEMR%d.LEM" % rating,
                 body + b"\0" * (RATING_FILE - len(body)))

    strband, nstrings = build_string_band()
    disk.add("LEMSTR.LEM", pad512(strband))

    manifest = build_manifest(ds, geom, playable, styles, specials,
                              style_parts, spec_parts, len(strband),
                              nstrings, nlevfiles, disk, reserve)
    disk.files.insert(0, ("LEMMAN.LEM", pad512(manifest)))

    disk.add("LEVELS.TXT", levels_txt(geom, playable, styles, specials,
                                      disk, clus, reserve))
    return disk, playable, styles, specials


def geom_name(geom):
    return {1440: "1.44MB", 1200: "1.2MB", 720: "720KB", 360: "360KB"}[geom]


def geom_tag(geom):
    return {1440: "1440K", 1200: "1200K", 720: "720K", 360: "360K"}[geom]


def choose_two_styles(ds, geom, reserve):
    """SPEC.md 92.9: a 360KB disk carries TWO graphic sets and only the levels
    that use them. Which two is decided by TRIAL-PLANNING all ten pairs and
    counting the levels each pair actually leaves playable - not by counting
    the levels each pair covers, which is a different and wrong number: a set
    reaching more levels can cost enough clusters that fewer of them fit.
    Ties go to the cheaper pair, then to the lower set numbers, so the answer
    is the same on every run.
    """
    best = None
    for a in range(5):
        for b in range(a + 1, 5):
            disk, levels, _st, _sp = plan_geometry(ds, geom, reserve, [a, b])
            n = sum(1 for e in levels if e["playable"])
            key = (n, -disk.nclusters(reserve), -a, -b)
            if best is None or key > best[0]:
                best = (key, [a, b])
    if best is None:
        raise Refused("no pair of graphic sets to choose from")
    return best[1]


def level_entry(e):
    """One 64-byte level entry, docs/lemband-format.md.

    ODDTABLE is already applied to every field it overwrites - the rate, the
    counts, the time, the eight skills and the name - which is where the
    fold lives (see plan_geometry).
    """
    L = e["level"]
    row = bytearray(ENTRY_SIZE)
    row[0] = e["rating"]
    row[1] = e["number"]
    row[2] = e["style"]
    row[3] = 0xFF if e["special"] is None else e["special"]
    struct.pack_into("<HHHH", row, 4, L.release_rate, L.num_lemmings,
                     L.num_to_rescue, L.time_limit)
    row[12:20] = bytes(L.skills)
    struct.pack_into("<H", row, 20, L.start_x)
    row[22] = e["rawfile"]
    row[23] = e["rawsect"]
    row[24] = 1 if e["playable"] else 0
    if e["missing"] > 0xFF:
        raise Refused("string id %d does not fit the entry's `missing` byte"
                      % e["missing"])
    row[25] = e["missing"]
    struct.pack_into("<H", row, 26, 1 if e["odd"] else 0)
    # The field is 32 bytes and the original's name is 32 bytes, so a name
    # that fills it has NO terminator: "NUL padded" means padded when it is
    # short. Four of the 120 are exactly 32 characters (`A task for blockers
    # and bombers` among them) and reserving a terminator ate the last
    # letter of each.
    name = L.name.rstrip().encode("latin-1")[:32]
    row[32:32 + len(name)] = name
    return bytes(row)


def build_manifest(ds, geom, levels, styles, specials, style_parts,
                   spec_parts, strbytes, nstrings, nlevfiles, disk, reserve):
    """LEMMAN.LEM - the 512-byte header and nothing else.

    The 120 entries moved out into LEMR0-3.LEM, so `leoff` is the offset of
    the first entry IN A RATING FILE, which is 0.

    THE COST TABLE, and why it is here. A level row that is greyed names
    LEMS_MISS_STYLE, LEMS_MISS_SPEC or LEMS_MISS_ROOM, and those are
    templates whose only marker is %s: the package fills them itself,
    because there is no printf in this C. Every number they ask for is
    written here, for the ABSENT banks as well as the present ones - a
    greyed row is asking about a bank that is not on the disk, so its cost
    cannot be measured from the disk.

    Offsets 42 upward are inside the block docs/lemband-format.md reserves
    and zeroes, so nothing the document pins moves:

      42   4  setbytes32  the exact byte count that offset 18 gives in KB
      46   5  stparts0..4 parts per style bank, 0 = not on this disk
      51   1  reserved
      52   2  nlevfiles   how many LEMLVn.LEM files this disk carries
      54   2  nstrings    how many strings LEMSTR.LEM holds
      56   2  diskclus    this disk's data clusters (713 on a 720KB disk)
      58   2  levclus     what one group of eight level records costs here
      60  72  costs       NINE rows of 8, whether the bank is here or not:
                            0 4 bytes     the bank's size, 512-padded parts
                            4 2 clusters  what it costs on THIS disk
                            6 1 parts     how many files it is
                            7 1 present   1 if it is on this disk
                          rows 0-4 the style banks, rows 5-8 the specials
     132   4  specbytes   all four special pictures together, bytes
     136   2  specclus    ...and what they cost on this disk
     138 374  reserved, zero
    """
    cb = cluster_bytes(geom)
    clus = data_clusters(geom)
    head = bytearray(MANIFEST_HEADER)
    head[0:8] = b"OS88LEM\0"
    struct.pack_into("<HHHH", head, 8, 1, len(levels), ENTRY_SIZE, 0)
    head[16] = sum(1 << s for s in styles)
    head[17] = sum(1 << i for i in specials)

    total = disk.nbytes + MANIFEST_HEADER
    clusters = disk.nclusters(reserve) + (MANIFEST_HEADER + cb - 1) // cb

    # docs/lemband-format.md gives offset 18 two bytes and this disk carries
    # about 950,000 of them, so 18 is KILOBYTES rounded up and the exact
    # 32-bit count is at 42.
    struct.pack_into("<H", head, 18, min((total + 1023) // 1024, 0xFFFF))
    struct.pack_into("<H", head, 20, min(clusters, 0xFFFF))
    tag = geom_tag(geom).encode("ascii")
    head[22:22 + len(tag)] = tag
    struct.pack_into("<H", head, 30, strbytes)
    struct.pack_into("<H", head, 32, 5)
    for i in range(4):
        struct.pack_into("<H", head, 34 + 2 * i, spec_parts[i])
    struct.pack_into("<I", head, 42, total)
    for s in range(5):
        head[46 + s] = style_parts[s]
    struct.pack_into("<HH", head, 52, nlevfiles, nstrings)
    struct.pack_into("<H", head, 56, clus)
    struct.pack_into("<H", head, 58,
                     (LEVELS_PER_FILE * LVL_SIZE + cb - 1) // cb)

    def cost(kind, n):
        parts = split_parts(ds.bank(kind, n))
        return (sum(len(p) for p in parts),
                sum((len(p) + cb - 1) // cb for p in parts), len(parts))

    for s in range(5):
        nb, nc, np = cost("style", s)
        row = 60 + s * 8
        struct.pack_into("<IHBB", head, row, nb, nc, np,
                         1 if s in styles else 0)
    spec_bytes = spec_clus = 0
    for i in range(4):
        nb, nc, np = cost("spec", i)
        row = 60 + (5 + i) * 8
        struct.pack_into("<IHBB", head, row, nb, nc, np,
                         1 if i in specials else 0)
        spec_bytes += nb
        spec_clus += nc
    struct.pack_into("<IH", head, 132, spec_bytes, spec_clus)
    return bytes(head)


def levels_txt(geom, levels, styles, specials, disk, clus, reserve):
    """The player's own copy of the arithmetic (SPEC.md 92.9). CRLF, because a
    .TXT on a FAT floppy is expected to have it (SPEC.md 19)."""
    out = []
    out.append("LEMMINGS - %s DISK" % geom_name(geom))
    out.append("=" * 28)
    out.append("")
    playable = sum(1 for e in levels if e["playable"])
    out.append("This disk carries %d of the 120 levels, %d of the five "
               "graphic sets and %d of the four special pictures."
               % (playable, len(styles), len(specials)))
    out.append("")
    out.append("A level whose graphic set or special picture is not here is "
               "still in the chooser and is greyed with the reason. The "
               "reason is the arithmetic, not an apology.")
    out.append("")
    out.append("GRAPHIC SETS")
    out.append("-" * 28)
    for s in range(5):
        out.append("  %-9s %s" % (STYLE_NAMES[s],
                                  "on this disk" if s in styles else "not here"))
    out.append("")
    out.append("SPECIAL PICTURES")
    out.append("-" * 28)
    for i in range(4):
        out.append("  VGASPEC%d  %s"
                   % (i, "on this disk" if i in specials else "not here"))
    absent = [s for s in range(5) if s not in styles]
    if absent or len(specials) < 4:
        out.append("")
        out.append("WHY NOT")
        out.append("-" * 28)
        out.append("")
        out.append("A floppy holds what it holds. The graphic sets and the "
                   "special pictures are the big things on this disk, and "
                   "what is missing is missing because the clusters ran "
                   "out - the chooser says the same arithmetic on the row "
                   "of every level it cannot start.")
    out.append("")
    out.append("THE ARITHMETIC")
    out.append("-" * 28)
    out.append("")
    out.append("  data files      %d" % len(disk.files))
    out.append("  converted bytes %d" % disk.nbytes)
    out.append("  clusters used   %d of %d" % (disk.nclusters(reserve), clus))
    out.append("  cluster size    %d bytes" % cluster_bytes(geom))
    out.append("")
    out.append("PLAYABLE HERE")
    out.append("-" * 28)
    for e in levels:
        if e["playable"]:
            # TRIMMED, the way every other surface shows a level name:
            # lemui.c's lem_row_level() and lemovl.c's ovl_fmt() both call
            # lem_trim() on Lemmix's own rule (GameScreen.Preview.pas calls
            # Title.Trim before it draws). The 64-byte ENTRY keeps the
            # original's centring spaces - that is the record, and the record
            # is not edited - but this file is a column of names a reader
            # reads, and untrimmed it is ragged.
            out.append("  %-6s %2d  %s"
                       % (RATINGS[e["rating"]], e["number"] + 1,
                          e["level"].name.strip()))
    out.append("")
    return ("\r\n".join(out) + "\r\n").encode("ascii", "replace")


# ---------------------------------------------------------------------------
# 11. Writing, and the manifest print
# ---------------------------------------------------------------------------


def write_disk(disk, out_dir):
    """Write the plan, and take away anything a PREVIOUS plan left.

    A geometry that carries fewer level files this time - the 360KB disk's
    count moves with the pair of graphic sets chosen - would otherwise leave
    a stale LEMLVn.LEM beside the new ones, and the manifest names the file
    by number, so the stale one is indistinguishable from a live one until
    the wrong level loads.
    """
    os.makedirs(out_dir, exist_ok=True)
    keep = {name for (name, _) in disk.files}
    for stale in sorted(os.listdir(out_dir)):
        if stale not in keep:
            os.remove(os.path.join(out_dir, stale))
    for name, data in disk.files:
        with open(os.path.join(out_dir, name), "wb") as fh:
            fh.write(data)
    return [(name, len(data)) for (name, data) in disk.files]


def print_manifest(geom, disk, levels, styles, specials, reserve, verbose):
    clus = data_clusters(geom)
    cb = cluster_bytes(geom)
    used = disk.nclusters(reserve)
    playable = sum(1 for e in levels if e["playable"])
    print("  %-7s %4d files  %9d bytes  %5d of %4d clusters (%d B each)  "
          "%3d/120 levels  %d styles  %d specials"
          % (geom_name(geom), len(disk.files), disk.nbytes, used, clus, cb,
             playable, len(styles), len(specials)))
    if used > clus:
        raise Refused("a %s disk needs %d clusters and has %d"
                      % (geom_name(geom), used, clus))
    if len(disk.files) > RD_MAXENT:
        raise Refused("%d files and RD_MAXENT is %d (SPEC.md 92.3.2)"
                      % (len(disk.files), RD_MAXENT))
    if verbose:
        for name, data in disk.files:
            print("      %-13s %7d  %4d clusters"
                  % (name, len(data), (len(data) + cb - 1) // cb))


# ---------------------------------------------------------------------------
# 12. The fixture
# ---------------------------------------------------------------------------
#
# SPEC.md 92.12: the host checks run against a COMMITTED synthetic fixture, so
# `make lemmings` needs no network. It carries NO bytes derived from the
# original data files - synthetic levels with invented names and a stub style
# bank - plus the real string band, because the strings are the port's own
# text and the reference's verbatim on-screen wording rather than decoded
# data (SPEC.md 92.2's posture on strings).

# (rating, number, style, rate, count, save, minutes, skills, startx, name,
#  missing) - `missing` 0 is playable, and the second row is GREYED with
# LEMS_MISS_SPEC so the host harness has a greyed row to draw. Both names are
# invented; neither is any of the original's 120.
FIXTURE_LEVELS = [
    (0, 0, 0, 50, 10, 1, 5, [1, 2, 3, 4, 5, 6, 7, 8], 160,
     "Fixture one - a flat ledge", 0),
    (0, 3, 1, 75, 20, 10, 3, [0, 0, 5, 0, 10, 0, 0, 5], 320,
     "Fixture two - the greyed row", "MISS_SPEC"),
]


def build_fixture(out_dir):
    """Two synthetic levels, a stub style bank, and the real string band."""
    os.makedirs(out_dir, exist_ok=True)
    files = []

    # Two synthetic 2,048-byte records. Big-endian like the real thing, with
    # invented terrain: one 32x16 slab per level, terrain id 0, so the record
    # exercises the reader without carrying a byte of anyone's data.
    records = []
    for (rating, number, style, rate, count, save, mins, skills, startx,
         name, _missing) in FIXTURE_LEVELS:
        rec = bytearray(LVL_SIZE)
        struct.pack_into(">HHHH", rec, 0, rate, count, save, mins)
        for i, v in enumerate(skills):
            struct.pack_into(">H", rec, 8 + 2 * i, v)
        struct.pack_into(">HHHH", rec, 0x18, startx, style, 0, 0)
        # one object: an exit at (64, 100), slot 0
        struct.pack_into(">HHHH", rec, 0x20, 64 + 16, 100, 0, 0x000F)
        # terrain slots: fill with 0xFF, then two real pieces
        for i in range(400):
            struct.pack_into(">I", rec, 0x120 + 4 * i, 0xFFFFFFFF)
        for i, (tx, ty) in enumerate(((80, 120), (240, 120))):
            v = (((tx + 16) & 0x0FFF) << 16) | (((ty + 4) & 0x1FF) << 7) | 0
            struct.pack_into(">I", rec, 0x120 + 4 * i, v)
        rec[0x7E0:0x800] = name.ljust(32).encode("ascii")[:32]
        records.append(bytes(rec))

    blob = b"".join(records)
    blob += b"\0" * (LEVELS_PER_FILE * LVL_SIZE - len(blob))
    files.append(("LEMLV0.LEM", blob))

    # A stub style bank: the header, two terrain slots and one object, with
    # invented pixels (a solid slab and a checkerboard).
    head = bytearray(1024)
    head[0:4] = b"LGRB"
    struct.pack_into("<H", head, 4, 1)
    head[6] = 0
    struct.pack_into("<HHH", head, 14, 128, 640, 1024)
    for i in range(8):                       # a palette of eight greys
        head[32 + 3 * i] = head[33 + 3 * i] = head[34 + 3 * i] = i * 9
        head[56 + 3 * i] = head[57 + 3 * i] = head[58 + 3 * i] = i * 9
        head[80 + 3 * i] = head[81 + 3 * i] = head[82 + 3 * i] = i * 9
    data = bytearray()
    for slot, (w, h, fill) in enumerate(((32, 16, 0xFF), (16, 16, 0xAA))):
        n = w * h // 2
        row = 128 + slot * 8
        head[row] = w
        head[row + 1] = h
        struct.pack_into("<I", head, row + 2, 1024 + len(data))
        struct.pack_into("<H", head, row + 6, n)
        data += bytes([fill]) * n
    head[7] = 2
    ow, oh, oframes = 32, 16, 2
    per = ow * oh * 5 // 8
    row = 640
    head[row] = ow
    head[row + 1] = oh
    head[row + 2] = oframes
    head[row + 3] = 1                        # GRND effect 1 = exit
    struct.pack_into("<HH", head, row + 4, 16, 16)
    head[row + 8] = 8
    head[row + 9] = 4
    struct.pack_into("<I", head, row + 12, 1024 + len(data))
    data += bytes([0x5A]) * (per * oframes)
    head[8] = 1
    logical = bytes(head) + bytes(data)
    struct.pack_into("<I", head, 10, len(logical))
    head[9] = 1
    logical = bytes(head) + bytes(data)
    if len(logical) > FILEMAX:
        raise Refused("the fixture style stub grew past the cap")
    files.append(("LEMGR0.LEM", pad512(logical)))

    # A stub MAIN bank: the header, the item table, and the two items wave 2
    # draws - the 320x40 4bpp skill panel and the 38-glyph 8x16 3bpp status
    # font. Both are INVENTED, like every other pixel in this directory: the
    # panel is a frame with a minimap well in it and the font is a set of
    # legible-enough blocks, so the harness models the raster rather than
    # refusing on a bank that is not there. Nothing here derives from
    # MAIN.DAT.
    mhead = bytearray(512)
    mhead[0:4] = b"LMNB"
    struct.pack_into("<H", mhead, 4, 1)
    mhead[6] = 2
    mhead[7] = 1
    mdata = bytearray()

    # item 0: 320x40, four planes of 1,600 bytes. Plane 3 alone is set over
    # the whole panel and cleared inside the minimap's well, so a reader sees
    # nibble 8 for the panel and 0 for the well - two of the three colour
    # CLASSES the 1bpp adapters draw (SPEC.md 39.4).
    panel = bytearray(4 * 1600)
    for row in range(40):
        for byte in range(40):
            panel[3 * 1600 + row * 40 + byte] = 0xFF
    for row in range(18, 38):                    # DosMiniMapCorners' well
        for byte in range(26, 39):
            panel[3 * 1600 + row * 40 + byte] = 0x00
    struct.pack_into("<BBHHH", mhead, 32, 0, 4, 320, 40, 1)
    struct.pack_into("<II", mhead, 32 + 8, len(mdata), len(panel))
    mdata += panel

    # item 1: 38 glyphs, 8x16, three planes of 16 bytes each. Glyph n is a
    # box with n vertical bars in it, which is not readable as a character
    # and is not meant to be: what the harness asserts is the INDEX MAP and
    # the delta-draw, and a real bank replaces these bytes without changing
    # one line of the reader.
    font = bytearray()
    for g in range(38):
        for plane in range(3):
            for row in range(16):
                if row == 0 or row == 15:
                    font.append(0xFF if plane == 1 else 0x00)
                else:
                    font.append(((g + 1) * 0x11) & 0xFF if plane == 1 else 0)
    struct.pack_into("<BBHHH", mhead, 48, 1, 3, 8, 16, 38)
    struct.pack_into("<II", mhead, 48 + 8, len(mdata), len(font))
    mdata += font

    mlogical = bytes(mhead) + bytes(mdata)
    struct.pack_into("<I", mhead, 8, len(mlogical))
    mlogical = bytes(mhead) + bytes(mdata)
    if len(mlogical) > FILEMAX:
        raise Refused("the fixture MAIN stub grew past the cap")
    files.append(("LEMMAIN.LEM", pad512(mlogical)))

    strband, nstrings = build_string_band()
    files.append(("LEMSTR.LEM", pad512(strband)))

    # LEMR0.LEM: the two levels, one playable and one greyed. The other three
    # ratings are written empty-but-present, which is what a real disk does
    # for a rating it carries no levels of.
    for rating in range(4):
        body = bytearray()
        for i, (rt, number, style, rate, count, save, mins, skills, startx,
                name, missing) in enumerate(FIXTURE_LEVELS):
            if rt != rating:
                continue
            row = bytearray(ENTRY_SIZE)
            row[0] = rt
            row[1] = number
            row[2] = style
            row[3] = 0xFF if missing != "MISS_SPEC" else 2
            struct.pack_into("<HHHH", row, 4, rate, count, save, mins)
            row[12:20] = bytes(skills)
            struct.pack_into("<H", row, 20, startx)
            row[22] = 0 if missing == 0 else 0xFF
            row[23] = i if missing == 0 else 0xFF
            row[24] = 1 if missing == 0 else 0
            row[25] = 0 if missing == 0 else SID[missing]
            struct.pack_into("<H", row, 26, 0)
            blob32 = name.encode("ascii")[:32]
            row[32:32 + len(blob32)] = blob32
            body += row
        if len(body) > RATING_FILE:
            raise Refused("rating %d needs %d bytes and a rating file is %d"
                          % (rating, len(body), RATING_FILE))
        files.append(("LEMR%d.LEM" % rating,
                      bytes(body) + b"\0" * (RATING_FILE - len(body))))

    # LEMMAN.LEM: the header alone, with a cost table the greyed row's
    # template can be filled from. The figures are the FIXTURE's, invented
    # like everything else here, but they are shaped exactly as a real
    # disk's so the substitution path is exercised rather than skipped.
    head = bytearray(MANIFEST_HEADER)
    head[0:8] = b"OS88LEM\0"
    struct.pack_into("<HHHH", head, 8, 1, len(FIXTURE_LEVELS), ENTRY_SIZE, 0)
    head[16] = 0x01
    head[17] = 0x00
    total = sum(len(d) for (_, d) in files) + MANIFEST_HEADER
    struct.pack_into("<H", head, 18, (total + 1023) // 1024)
    struct.pack_into("<H", head, 20, (total + 511) // 512)
    head[22:27] = b"1440K"
    struct.pack_into("<H", head, 30, len(strband))
    struct.pack_into("<H", head, 32, 1)
    struct.pack_into("<I", head, 42, total)
    head[46] = 1
    struct.pack_into("<HH", head, 52, 1, nstrings)
    struct.pack_into("<H", head, 56, 2847)          # a 1.44MB disk's clusters
    struct.pack_into("<H", head, 58, 32)            # a level group's cost
    struct.pack_into("<IHBB", head, 60, 2048, 4, 1, 1)      # style 0, here
    for s_i in range(1, 5):
        struct.pack_into("<IHBB", head, 60 + s_i * 8, 78848, 154, 2, 0)
    for i in range(4):
        struct.pack_into("<IHBB", head, 60 + (5 + i) * 8, 77312, 151, 2, 0)
    struct.pack_into("<IH", head, 132, 4 * 77312, 4 * 151)
    files.insert(0, ("LEMMAN.LEM", pad512(bytes(head))))

    for name, data in files:
        check_name(name)
        if len(data) > FILEMAX:
            raise Refused("fixture %s is %d bytes" % (name, len(data)))
        with open(os.path.join(out_dir, name), "wb") as fh:
            fh.write(data)
    return files


# ---------------------------------------------------------------------------
# 13. --selfcheck
# ---------------------------------------------------------------------------


def selfcheck(src, verbose):
    """Deterministic self-consistency, and it runs with NO original data.

    apps/lemmings/build.sh runs this against the committed fixture with no
    network at all (SPEC.md 92.12), so the half that needs the 26 files is
    added only when they are there.
    """
    problems = []
    print("os88lem: selfcheck")

    # 1. the two reference level orders
    bad = check_level_order()
    problems += bad
    print("  120-level order: C position_of_classic_level[] against TS "
          "config.json level.order - %s"
          % ("agree on all 120" if not bad else "%d disagree" % len(bad)))

    # 2. MAIN section 0's doc offsets are the running sum of the frame table,
    #    which is what proves planar frames carry no row padding.
    run = 0
    for (entry, doc_off) in zip(MAIN_ANIMS, MAIN_ANIM_DOC_OFFSETS):
        if run != doc_off:
            problems.append("MAIN s0 %s: computed 0x%04X, the doc says 0x%04X"
                            % (entry[0], run, doc_off))
        run += entry[1] * frame_bytes(entry[2], entry[3], entry[4])
    print("  MAIN section 0: 30 animations, 337 frames, %d bytes - the doc's "
          "own offset table reproduced by summing" % run)

    # 3. every string id name is legal C and unique; the header regenerates
    for name in STRING_NAMES:
        if not name.replace("_", "").isalnum() or name[0].isdigit():
            problems.append("string id name %r is not a C identifier" % name)
    band, n = build_string_band()
    band2, n2 = build_string_band()
    if band != band2:
        problems.append("the string band is not deterministic")
    print("  strings: %d ids, %d bytes, %d padded" % (n, len(band),
                                                      len(pad512(band))))

    # 4. the fixture, built twice into two directories, byte for byte
    with tempfile.TemporaryDirectory() as tmp:
        a = os.path.join(tmp, "a")
        b = os.path.join(tmp, "b")
        build_fixture(a)
        build_fixture(b)
        names = sorted(os.listdir(a))
        if names != sorted(os.listdir(b)):
            problems.append("the fixture is not deterministic (file list)")
        match, mismatch, errors = filecmp.cmpfiles(a, b, names, shallow=False)
        if mismatch or errors:
            problems.append("the fixture is not deterministic: %s %s"
                            % (mismatch, errors))
        for name in names:
            try:
                check_name(name)
            except Refused as exc:
                problems.append(str(exc))
            size = os.path.getsize(os.path.join(a, name))
            if size > FILEMAX:
                problems.append("fixture %s is over the %d cap"
                                % (name, FILEMAX))
            if name.endswith(".LEM") and size % SECTOR:
                problems.append("fixture %s is not 512-padded" % name)
        print("  fixture: %d files, rebuilt byte-identical, every name legal, "
              "every file under %d" % (len(names), FILEMAX))

    # 5. with the data present, the real thing: all four geometries, twice
    if src and os.path.isdir(src) and os.path.exists(
            os.path.join(src, "MAIN.DAT")):
        ds = DataSet(src)
        print("  data: 26 files read, 120 levels, 5 style banks, 4 specials")
        first = {}
        for pass_no in (1, 2):
            for geom in GEOM_ORDER:
                disk, levels, styles, specials = plan_geometry(ds, geom)
                sig = tuple((n, d) for (n, d) in disk.files)
                if pass_no == 1:
                    first[geom] = sig
                    for name, data in disk.files:
                        check_name(name)
                        if len(data) > FILEMAX:
                            problems.append("%s/%s is %d bytes, over the %d "
                                            "cap" % (geom, name, len(data),
                                                     FILEMAX))
                        # Only a BAND is padded: LEVELS.TXT is text for
                        # the reader and NUL padding would be junk at the end
                        # of it.
                        if name.endswith(".LEM") and len(data) % SECTOR:
                            problems.append("%s/%s is not 512-padded"
                                            % (geom, name))
                elif first[geom] != sig:
                    problems.append("%s rebuilt differently" % geom_name(geom))
            # A second DataSet, so the determinism claim covers the decoders
            # and not only the writers.
            if pass_no == 1:
                ds = DataSet(src)
        print("  conversion: four geometries built twice, byte-identical, "
              "every band under %d and 512-padded" % FILEMAX)
    else:
        print("  conversion: SKIPPED - no data in %s (this is the path "
              "apps/lemmings/build.sh takes)" % (src or "build/lemdata"))

    if problems:
        for p in problems:
            print("  PROBLEM: %s" % p)
        return 1
    print("os88lem: selfcheck passed")
    return 0


# ---------------------------------------------------------------------------
# 14. main
# ---------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-s", "--src", metavar="DIR", default="build/lemdata",
                    help="where getlemmings.py put the 26 files")
    ap.add_argument("-o", "--output", metavar="DIR", default="build/lemband",
                    help="a subdirectory per geometry lands here")
    ap.add_argument("--geom", metavar="N", type=int, action="append",
                    choices=GEOM_ORDER,
                    help="convert only this geometry (repeatable)")
    ap.add_argument("--header", metavar="FILE", default="build/lemstr.h",
                    help="where the generated LEMS_* ids go")
    ap.add_argument("--selfcheck", action="store_true",
                    help="the host-side gate; runs with or without the data")
    ap.add_argument("--fixture", metavar="DIR",
                    help="write the committed synthetic set and exit")
    ap.add_argument("--reserve", metavar="BYTES", type=int,
                    default=PKG_RESERVE,
                    help="bytes to leave for the package, the overlay and "
                         "README.TXT (default %d)" % PKG_RESERVE)
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="list every file with its size and cluster cost")
    args = ap.parse_args()

    try:
        if args.fixture:
            # build/lemstr.h is written here TOO: `make lemmings` must build
            # with no fetch, and the fixture is all it has (SPEC.md 92.12).
            header = write_lemstr_h(args.header)
            files = build_fixture(args.fixture)
            print("os88lem: fixture -> %s" % args.fixture)
            for name, data in files:
                print("  %-13s %6d" % (name, len(data)))
            print("os88lem: %d string ids -> %s (%d bytes)"
                  % (len(STRINGS), args.header, len(header)))
            return 0

        if args.selfcheck:
            return selfcheck(args.src, args.verbose)

        if not os.path.exists(os.path.join(args.src, "MAIN.DAT")):
            fail("no data in %s - run\n"
                 "    python3 tools/getlemmings.py\n"
                 "  or, with no network,\n"
                 "    python3 tools/getlemmings.py --from <dir-or-zip>"
                 % args.src)

        header = write_lemstr_h(args.header)
        print("os88lem: %d string ids -> %s (%d bytes)"
              % (len(STRINGS), args.header, len(header)))

        ds = DataSet(args.src)
        geoms = args.geom or GEOM_ORDER
        print("os88lem: per-geometry manifest "
              "(clusters as well as bytes, SPEC.md 92.3)")
        archive = 0
        for geom in geoms:
            disk, levels, styles, specials = plan_geometry(
                ds, geom, args.reserve)
            print_manifest(geom, disk, levels, styles, specials,
                           args.reserve, args.verbose)
            write_disk(disk, os.path.join(args.output, str(geom)))
            if geom == 1440:
                archive = disk.nbytes
        if archive:
            whole = archive + args.reserve
            print("os88lem: whole archive %d bytes of WIRE_ARCMAX %d "
                  "(%d spare) - converted data %d plus %d reserved for the "
                  "package, the overlay and README.TXT"
                  % (whole, ARCMAX, ARCMAX - whole, archive, args.reserve))
            if whole > ARCMAX:
                raise Refused("the archive is %d bytes and WIRE_ARCMAX is %d"
                              % (whole, ARCMAX))
        return 0
    except Refused as exc:
        fail(str(exc))


if __name__ == "__main__":
    sys.exit(main())

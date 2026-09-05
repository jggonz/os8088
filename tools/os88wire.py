#!/usr/bin/env python3
"""The Wire's catalog and its archives: pack, verify, dump, and cut a picture.

    python3 tools/os88wire.py --pack data/wire.json --pkgdir build/ \\
                              --picdir build/wirepic --out build/catalog.bin
    python3 tools/os88wire.py --verify build/catalog.bin [--pkgdir build/]
    python3 tools/os88wire.py --dump build/catalog.bin
    python3 tools/os88wire.py --pic shot.png --crop 96,120 --out HELLO.PIC

    python3 tools/os88wire.py --archive build/RUNCPM.WPK \\
                              --srcdir build/runcpm-disk --home RUNCPM \\
                              --program RUNCPM.O88 [--order A0.list]
    python3 tools/os88wire.py --verify build/RUNCPM.WPK
    python3 tools/os88wire.py --dump build/RUNCPM.WPK

`--verify` and `--dump` take EITHER format and the magic says which.

SPEC.md 88.2, 88.3 and 88.13 are the formats. This file is the OS repo's
writer and its reader; the website's `tools/wire.py` is an INDEPENDENT SECOND
WRITER of the same bytes, and `--verify` run against the site's published
`catalog.bin` and `.WPK` files is the cross-check between them - the
arrangement `tests/unit/t_wab.py` has over a `.WAB`, one format along.

**THE VERIFIER DOES NOT COMPARE A GENERIC ICON.** A record whose package
declares an `OS88_ICON16` (header flags bit 0) must carry exactly those 64
bytes and `--verify --pkgdir` says so. A record whose package declares none
carries a generic, and SPEC.md 88.2 leaves which generic to the writer - so
two independent writers may choose differently and both be right. What is
checked there is the one thing that is a fault either way: an icon with no
pixels in it, which `icon_draw_x` accepts and draws as nothing.

**EVERY OFFSET BELOW IS MIRRORED IN apps/thewire/wcat.inc and
apps/thewire/warc.inc**, and
`tests/unit/t_wire.py` compares the files in the FAST tier. There is no
linker in this tree, so a format that lives on both sides of a wire is a
number typed out twice; the comparison is what stops the two spellings
drifting, exactly as `tests/unit/t_mirror.py` does for the constants the
kernel and the SDK share.

STDLIB ONLY, and that is a requirement rather than a habit: the website's CI
is a bare Python 3.12 and its packer has to be able to say the same bytes.
`zlib` is in the standard library, which is the whole of what a PNG reader
needs.
"""
import argparse
import json
import os
import struct
import sys
import zlib

# --- the 32-byte header (SPEC.md 88.2) ---------------------------------------
WC_MAGIC = 0
WC_VER = 4
WC_RSZ = 5
WC_N = 6
WC_HSZ = 8
WC_SCOFF = 10
WC_SCN = 12
WC_DATE = 14
WIRE_HDR = 32

# --- one 256-byte record ------------------------------------------------------
WC_STEM = 0
WC_TITLE = 8
WC_KIND = 32
WC_TIER = 33
WC_FLAGS = 34
WC_NSIDE = 35
WC_SIDE0 = 36
WC_SIZE = 38
WC_TOTAL = 42
WC_NEEDKB = 46
WC_ICON = 48
WC_DESC = 112
WIRE_REC = 256
WC_DESCN = 5
WC_DESCW = 28

# --- one 16-byte sidecar entry ------------------------------------------------
WC_SCNAME = 0
WC_SCSIZE = 12
WIRE_SC = 16

# --- WC_FLAGS -----------------------------------------------------------------
WF_DISK = 1
WF_PIC = 2
WF_NEW = 4
WF_FLOPPY = 8
WF_ARC = 16

# --- WC_KIND ------------------------------------------------------------------
WK_PROGRAM = 0
WK_GAME = 1
WK_UTILITY = 2
WK_DOCUMENT = 3
WK_UPDATE = 4
WK_SHARED = 5

# --- what the reader will hold ------------------------------------------------
WIRE_VER = 1
WIRE_CATMAX = 16384
WIRE_FILEMAX = 64512
WIRE_NMAX = 255
WIRE_SCMAX = 8
# 0x100000, one megabyte: the reader's sanity bound on an ARCHIVE record's
# WC_SIZE. WIRE_FILEMAX is the wrong bound there and refusing on it is what
# the machine did to the site's real RUNCPM record at 231,463 bytes - an
# archive is a DWORD length and the first body over 64KB by design (SPEC.md
# 88.14's WK_ARC). The field still needs a ceiling, because Content-Length
# arrives before the body and the progress figure is computed from it.
#
# **IT IS THE FIRST SIZE REFUSED**, not the last accepted: `wcat.inc` says an
# archive's WC_SIZE must be BELOW it, which on the machine is one compare on
# the high word rather than three on the pair. So the tests here are `>=` and
# an archive of exactly 1,048,576 bytes is refused by both sides.
WIRE_ARCMAX = 1048576

# --- the picture (SPEC.md 88.3) -----------------------------------------------
WIRE_PICW = 128
WIRE_PICH = 64
WIRE_PICB = 16
WIRE_PICSZ = 1024

# --- the archive (SPEC.md 88.13), mirrored from apps/thewire/warc.inc ---------
# The 32-byte header.
WA_MAGIC = 0
WA_VER = 4
WA_FLAGS = 5
WA_N = 6
WA_TOTAL = 8
WA_NEEDKB = 12
WA_MAXENT = 14
WA_HOME = 16
WARC_HDR = 32

# One 64-byte entry header, followed at once by its body.
WA_METHOD = 0
WA_DEPTH = 1
WA_STORED = 4
WA_SIZE = 8
WA_PATH = 16
WARC_ENT = 64
WARC_SLOT = 12
WARC_SLOTS = 4
WARC_DEPTH = 3

# WA_FLAGS.
WAH_PROGRAM = 1

# WA_METHOD.
WAM_STORED = 0
WAM_LZSS = 1

# What the reader will hold.
WARC_VER = 1
WARC_NMAX = 255
WARC_DISTMAX = 4096
WARC_LENMIN = 3
WARC_LENMAX = 18

MAGIC = b"WIRE"
AMAGIC = b"WPAK"
KINDS = {"program": WK_PROGRAM, "game": WK_GAME, "utility": WK_UTILITY,
         "document": WK_DOCUMENT, "update": WK_UPDATE, "shared": WK_SHARED}
TIERS = ("8088/8086", "286", "386", "486+")


class Refused(Exception):
    """A rule in SPEC.md 88.2 that the input broke.

    Every one of these is a sentence a person can act on. The writer refuses
    rather than emitting a catalog the machine will then refuse, because the
    machine's refusal is one line in a status cell and this one can name the
    entry, the field and the limit.
    """


# =============================================================================
# THE PACKAGE HEADER (SPEC.md 20.2) - what the writer reads out of an .o88
# =============================================================================
O88_MAGIC = b"O8"
O88_F_ICON = 1
O88_F_PARTS = 4


def o88_header(blob, name):
    """(flags, image_size, bss) out of an .o88, or Refused."""
    if len(blob) < 32 or blob[0:2] != O88_MAGIC:
        raise Refused("%s is not an .o88 (no O8 magic)" % name)
    flags = blob[3]
    size = struct.unpack_from("<H", blob, 8)[0]
    bss = struct.unpack_from("<H", blob, 10)[0]
    return flags, size, bss


# The icon a record gets when its package declares none, and it IS the
# kernel's own `ico_app16` (kernel/icons.inc) - a diamond outline with a
# filled square at its centre, over a solid diamond mask. That is what the
# Disk window already draws for a package with no OS88_ICON16, so a program
# looks the same in the Wire's list as it does in the folder it comes from.
#
# It is BAKED IN rather than parsed out of kernel/icons.inc, and that is
# forced: the website's packer runs in a checkout that has no kernel in it.
# `tests/unit/t_wire.py` compares these two lists against `ico_app16`'s own
# `dw` rows in the fast tier, which is this tree's standing arrangement for a
# constant that has to live in two places (t_mirror's argument).
GENERIC_ICON_MASK = [
    0x0180, 0x03C0, 0x07E0, 0x0FF0, 0x1FF8, 0x3FFC, 0x7FFE, 0xFFFF,
    0xFFFF, 0x7FFE, 0x3FFC, 0x1FF8, 0x0FF0, 0x07E0, 0x03C0, 0x0180,
]
GENERIC_ICON_DATA = [
    0x0180, 0x0240, 0x0420, 0x0810, 0x1008, 0x2004, 0x43C2, 0x83C1,
    0x83C1, 0x43C2, 0x2004, 0x1008, 0x0810, 0x0420, 0x0240, 0x0180,
]


def icon_of(blob, name):
    """The 64 bytes a record's WC_ICON carries: 16 mask words then 16 data."""
    flags, _, _ = o88_header(blob, name)
    if flags & O88_F_ICON:
        if len(blob) < 96:
            raise Refused("%s declares an icon and is shorter than 96 bytes"
                          % name)
        return blob[32:96]
    return struct.pack("<32H", *(GENERIC_ICON_MASK + GENERIC_ICON_DATA))


# =============================================================================
# TEXT - every field is ASCII 0x20..0x7E, and the WRITER is what enforces it
# =============================================================================
def ascii_field(text, width, what, pad=b"\0"):
    b = text.encode("ascii", "strict") if isinstance(text, str) else text
    for ch in b:
        if ch < 0x20 or ch > 0x7E:
            raise Refused("%s has a byte 0x%02X in it, and the machine's font "
                          "is ASCII 0x20..0x7E (SPEC.md 88.2)" % (what, ch))
    if len(b) > width - (1 if pad == b"\0" else 0):
        raise Refused("%s is %d bytes and the field holds %d"
                      % (what, len(b), width - (1 if pad == b"\0" else 0)))
    return b + pad * (width - len(b))


def wrap(text, cols, lines, what):
    """The description, PRE-WRAPPED BY THE WRITER (SPEC.md 88.2).

    The machine wraps nothing, which is why the record is five fixed fields
    and not one string: wrapping on an 8088 is a per-paint cost for an answer
    that never changes. A summary that does not fit is REFUSED rather than
    cut - a sentence that stops mid-word is worse than an error at build time.
    """
    out, cur = [], ""
    for word in text.split():
        if len(word) > cols:
            raise Refused("%s contains the %d-character word %r and a line "
                          "holds %d" % (what, len(word), word, cols))
        trial = word if not cur else cur + " " + word
        if len(trial) <= cols:
            cur = trial
        else:
            out.append(cur)
            cur = word
    if cur:
        out.append(cur)
    if len(out) > lines:
        raise Refused("%s wraps to %d lines at %d columns and the record "
                      "holds %d (SPEC.md 88.2)" % (what, len(out), cols, lines))
    return out


# =============================================================================
# PACK
# =============================================================================
def pack(manifest, pkgdir, picdir, date=None):
    entries = manifest.get("entries")
    if not entries:
        raise Refused("the manifest has no entries")
    if len(entries) > WIRE_NMAX:
        raise Refused("%d entries; the format holds %d" % (len(entries),
                                                           WIRE_NMAX))
    date = date or manifest.get("date") or "00000000"
    if len(date) != 8 or not date.isdigit():
        raise Refused("the catalog date is %r and the field is 'YYYYMMDD'"
                      % date)

    recs, sides = [], []
    seen = set()
    for e in entries:
        stem = e["stem"].upper()
        if not stem or len(stem) > 8:
            raise Refused("stem %r is not 1..8 characters" % stem)
        for ch in stem:
            if not (ch.isdigit() or ("A" <= ch <= "Z") or ch in "_-"):
                raise Refused("stem %r has %r in it; A-Z 0-9 _ - only"
                              % (stem, ch))
        if stem in seen:
            raise Refused("stem %r appears twice" % stem)
        seen.add(stem)

        fl = e.get("flags") or {}
        flags = 0
        if fl.get("new"):
            flags |= WF_NEW
        if fl.get("floppy_only"):
            flags |= WF_FLOPPY

        # --- AN ARCHIVE RECORD (SPEC.md 88.2's WF_ARC, 88.13) ---------------
        # The manifest says `"archive": {"file": "RUNCPM.WPK"}` where a
        # program says `"files": [...]`, and every figure in the record then
        # comes out of the .WPK's own header rather than out of the manifest:
        # a second place to type WC_TOTAL is a second place for it to be
        # wrong, and Add to Disk's free-space check is decided on it.
        arc = e.get("archive")
        if arc is not None:
            if e.get("files"):
                raise Refused("%s names both files and an archive; an archive "
                              "IS the transfer (SPEC.md 88.13)" % stem)
            if fl.get("needs_disk"):
                raise Refused("%s is an archive and needs_disk: WF_DISK is "
                              "clear on every WF_ARC record (SPEC.md 88.2) - "
                              "the tree it unpacks IS its files on a disk")
            afile = arc.get("file") or (stem + ".WPK")
            apath = os.path.join(pkgdir, afile)
            if not os.path.exists(apath):
                raise Refused("%s names the archive %s and it is not in %s"
                              % (stem, afile, pkgdir))
            ablob = open(apath, "rb").read()
            abad = arc_verify(ablob, afile)
            if abad:
                raise Refused("%s: %s" % (stem, abad[0]))
            ahdr, aents = arc_read(ablob, afile)
            # **THE WRITER SETS WF_FLOPPY WITH WF_ARC** (SPEC.md 88.2): a Wire
            # from before 88.13 does not know bit 4, greys both buttons with
            # the floppy reason, and never fetches a .O88 that is not there.
            flags |= WF_ARC | WF_FLOPPY
            wc_size = len(ablob)
            wc_total = ahdr["total"]
            wc_needkb = ahdr["needkb"]
            icon = arc_icon(ahdr, aents, afile)
            if arc.get("icon"):
                ipath = os.path.join(pkgdir, arc["icon"])
                if not os.path.exists(ipath):
                    raise Refused("%s's archive names the icon file %s and it "
                                  "is not in %s" % (stem, arc["icon"], pkgdir))
                icon = icon_of(open(ipath, "rb").read(), arc["icon"])
            files, sizes = [], []
        else:
            files = list(e.get("files") or [])
            if not files:
                raise Refused("%s names no files" % stem)
            blobs, sizes = [], []
            for f in files:
                path = os.path.join(pkgdir, f)
                if not os.path.exists(path):
                    raise Refused("%s names %s and it is not in %s"
                                  % (stem, f, pkgdir))
                b = open(path, "rb").read()
                blobs.append(b)
                sizes.append(len(b))

            # WF_DISK IS DERIVED AND NOT DECLARED. It is "this program's files
            # must be on a disk", and there are exactly three ways to be that:
            # a sidecar beside the .o88, a package that carries PARTS (header
            # flags bit 2, SPEC.md 20.12 - it reads them out of its own FILE),
            # or something the manifest says outright. Deriving it is what
            # makes "a sidecar without a WF_DISK" unrepresentable rather than
            # refused.
            hflags, _, _ = o88_header(blobs[0], files[0])
            if len(files) > 1 or (hflags & O88_F_PARTS) or fl.get("needs_disk"):
                flags |= WF_DISK

            for f, n in zip(files, sizes):
                if n == 0:
                    raise Refused("%s is empty" % f)
                if n > WIRE_FILEMAX and not (flags & WF_FLOPPY):
                    raise Refused("%s is %d bytes, over WIRE_FILEMAX (%d), and the "
                                  "entry is not floppy_only - the Wire moves a "
                                  "file in ONE claim and ONE write (SPEC.md 88.2)"
                                  % (f, n, WIRE_FILEMAX))
            wc_size = sizes[0]
            wc_total = sum(sizes)
            wc_needkb = 0
            icon = icon_of(blobs[0], files[0])

        if picdir and os.path.exists(os.path.join(picdir, stem + ".PIC")):
            pic = open(os.path.join(picdir, stem + ".PIC"), "rb").read()
            if len(pic) != WIRE_PICSZ:
                raise Refused("%s.PIC is %d bytes and a picture is %d "
                              "(SPEC.md 88.3)" % (stem, len(pic), WIRE_PICSZ))
            flags |= WF_PIC

        kind = e.get("kind", 0)
        if isinstance(kind, str):
            if kind not in KINDS:
                raise Refused("%s has kind %r; %s" % (stem, kind,
                                                      "/".join(KINDS)))
            kind = KINDS[kind]
        if not 0 <= kind <= 255:
            raise Refused("%s has kind %r" % (stem, kind))
        tier = e.get("tier", 0)
        if tier not in (0, 1, 2, 3):
            raise Refused("%s has tier %r; 0..3 (SPEC.md 88.2)" % (stem, tier))

        desc = e.get("description") or e.get("summary") or ""
        lines = wrap(desc, WC_DESCW - 1, WC_DESCN, "%s's description" % stem)

        side0 = len(sides)
        for f, n in zip(files[1:], sizes[1:]):
            sides.append((f.upper(), n))
        # An archive has NO SIDECARS - n = 0 on every WF_ARC record (SPEC.md
        # 88.2). Its extra files are entries in the stream, not a second
        # fetch, which is the whole point of the format.
        nside = max(0, len(files) - 1)
        if nside > WIRE_SCMAX:
            raise Refused("%s has %d sidecars and the record holds %d"
                          % (stem, nside, WIRE_SCMAX))

        r = bytearray(WIRE_REC)
        r[WC_STEM:WC_STEM + 8] = ascii_field(stem, 8, "%s's stem" % stem,
                                             pad=b" ")
        r[WC_TITLE:WC_TITLE + 24] = ascii_field(e["title"], 24,
                                                "%s's title" % stem)
        r[WC_KIND] = kind
        r[WC_TIER] = tier
        r[WC_FLAGS] = flags
        r[WC_NSIDE] = nside
        struct.pack_into("<H", r, WC_SIDE0, side0 if nside else 0)
        struct.pack_into("<I", r, WC_SIZE, wc_size)
        struct.pack_into("<I", r, WC_TOTAL, wc_total)
        struct.pack_into("<H", r, WC_NEEDKB, wc_needkb)
        r[WC_ICON:WC_ICON + 64] = icon
        for i, line in enumerate(lines):
            off = WC_DESC + i * WC_DESCW
            r[off:off + WC_DESCW] = ascii_field(line, WC_DESCW,
                                                "%s's description" % stem)
        recs.append(bytes(r))

    if len(sides) * WIRE_SC > 0xFFFF:
        raise Refused("the sidecar table does not fit a 16-bit offset")

    scoff = WIRE_HDR + len(recs) * WIRE_REC
    hdr = bytearray(WIRE_HDR)
    hdr[WC_MAGIC:WC_MAGIC + 4] = MAGIC
    hdr[WC_VER] = WIRE_VER
    hdr[WC_RSZ] = WIRE_REC // 16
    struct.pack_into("<H", hdr, WC_N, len(recs))
    struct.pack_into("<H", hdr, WC_HSZ, WIRE_HDR)
    struct.pack_into("<H", hdr, WC_SCOFF, scoff)
    struct.pack_into("<H", hdr, WC_SCN, len(sides))
    hdr[WC_DATE:WC_DATE + 8] = date.encode("ascii")

    tab = bytearray()
    for name, n in sides:
        e = bytearray(WIRE_SC)
        e[WC_SCNAME:WC_SCNAME + 12] = ascii_field(name, 12,
                                                  "sidecar name %s" % name)
        struct.pack_into("<I", e, WC_SCSIZE, n)
        tab += e

    out = bytes(hdr) + b"".join(recs) + bytes(tab)
    if len(out) > WIRE_CATMAX:
        raise Refused("the catalog is %d bytes and WIRE_CATMAX is %d - the "
                      "machine claims that much before a Content-Length "
                      "exists, so it is a hard ceiling (SPEC.md 88.2)"
                      % (len(out), WIRE_CATMAX))
    return out


# =============================================================================
# VERIFY - every rule the 8088's wr_catck enforces, and the ones only a host
# can (that a size matches the file, that an icon matches the package)
# =============================================================================
def verify(blob, pkgdir=None):
    bad = []

    def no(msg):
        bad.append(msg)

    def icon_check(tag, got, pkg, whose):
        """The record's 64 bytes against the package that owns them.

        `pkg` is the .o88 image the record describes - the published file, or
        an archive's program entry decoded out of the stream - or None when
        there is no program to ask.
        """
        if pkg is not None:
            try:
                hflags, _, _ = o88_header(pkg, whose)
            except Refused as ex:
                no("%s: %s" % (tag, ex))
                return
            if hflags & O88_F_ICON:
                # IT DECLARES ONE, so the record must carry THAT one:
                # anything else shows a program under another program's
                # picture, and the row is drawn from these 64 bytes alone.
                if got != pkg[32:96]:
                    no("%s: %s declares an OS88_ICON16 and the record carries "
                       "different bytes" % (tag, whose))
                return
        # IT DECLARES NONE, so the record carries a GENERIC one - and SPEC.md
        # 88.2 says "or the site's generic program icon", which is a choice
        # the writer makes rather than a value this can compare against. Two
        # writers may pick different generics and both be right, so what is
        # checked is the only thing that is actually a fault: an icon with no
        # pixels in it. icon_draw_x accepts an all-zero record and draws
        # nothing at all, so a blank one is a row that silently has no
        # picture rather than a refusal anybody sees.
        if not any(got):
            no("%s: %s declares no icon and the record's generic is 64 zero "
               "bytes - the row would draw nothing at all" % (tag, whose))
        elif not any(got[32:]):
            no("%s: the generic icon's DATA rows are all zero, so the row "
               "draws a blank white block" % tag)

    if len(blob) < WIRE_HDR:
        return ["shorter than the %d-byte header" % WIRE_HDR]
    if len(blob) > WIRE_CATMAX:
        no("%d bytes, over WIRE_CATMAX %d" % (len(blob), WIRE_CATMAX))
    if blob[WC_MAGIC:WC_MAGIC + 4] != MAGIC:
        no("magic is %r and not %r" % (blob[0:4], MAGIC))
    if blob[WC_VER] != WIRE_VER:
        no("format version %d and not %d" % (blob[WC_VER], WIRE_VER))
    if blob[WC_RSZ] != WIRE_REC // 16:
        no("record size %d sixteens and not %d" % (blob[WC_RSZ],
                                                   WIRE_REC // 16))
    hsz = struct.unpack_from("<H", blob, WC_HSZ)[0]
    if hsz != WIRE_HDR:
        no("header size %d and not %d" % (hsz, WIRE_HDR))
    n = struct.unpack_from("<H", blob, WC_N)[0]
    if not 1 <= n <= WIRE_NMAX:
        no("record count %d, and it must be 1..%d" % (n, WIRE_NMAX))
        return bad
    if WIRE_HDR + n * WIRE_REC > len(blob):
        no("%d records do not fit in %d bytes" % (n, len(blob)))
        return bad
    date = blob[WC_DATE:WC_DATE + 8]
    if not date.decode("latin1").isdigit():
        no("the catalog date is %r and the field is eight digits" % date)

    scoff = struct.unpack_from("<H", blob, WC_SCOFF)[0]
    scn = struct.unpack_from("<H", blob, WC_SCN)[0]
    if scn and scoff + scn * WIRE_SC > len(blob):
        no("the %d-entry sidecar table at %d runs past the end"
           % (scn, scoff))
        scn = 0

    seen = set()
    for i in range(n):
        o = WIRE_HDR + i * WIRE_REC
        stem = blob[o + WC_STEM:o + WC_STEM + 8].rstrip(b" ").decode("latin1")
        tag = "record %d (%s)" % (i, stem or "?")
        if not stem:
            no("%s has no stem" % tag)
        if stem in seen:
            no("%s: the stem appears twice" % tag)
        seen.add(stem)
        for ch in stem:
            if not (ch.isdigit() or "A" <= ch <= "Z" or ch in "_-"):
                no("%s: %r is not A-Z 0-9 _ -" % (tag, ch))
        title = blob[o + WC_TITLE:o + WC_TITLE + 24]
        if b"\0" not in title:
            no("%s: the title is not NUL-terminated inside its 24" % tag)
        for field, what in ((title, "title"),
                            (blob[o + WC_DESC:o + WC_DESC + WC_DESCN
                                  * WC_DESCW], "description")):
            for ch in field.split(b"\0")[0]:
                if ch < 0x20 or ch > 0x7E:
                    no("%s: the %s has a byte 0x%02X in it" % (tag, what, ch))
                    break
        if blob[o + WC_TIER] > 3:
            no("%s: tier %d" % (tag, blob[o + WC_TIER]))
        flags = blob[o + WC_FLAGS]
        if flags & ~(WF_DISK | WF_PIC | WF_NEW | WF_FLOPPY | WF_ARC):
            no("%s: unknown flag bits 0x%02X" % (tag, flags))
        arc = bool(flags & WF_ARC)
        needkb = struct.unpack_from("<H", blob, o + WC_NEEDKB)[0]
        if arc:
            # SPEC.md 88.2's three rules for an archive record, and the third
            # is the COMPATIBILITY one: a Wire from before 88.13 does not know
            # bit 4 and would otherwise fetch a /wire/pkg/<STEM>.O88 that does
            # not exist. WF_FLOPPY beside it greys both buttons instead.
            if blob[o + WC_NSIDE]:
                no("%s: WF_ARC with %d sidecars, and an archive's n is 0 - "
                   "its extra files are entries in the stream (SPEC.md 88.2)"
                   % (tag, blob[o + WC_NSIDE]))
            if flags & WF_DISK:
                no("%s: WF_ARC with WF_DISK, and 88.2 clears bit 0 on an "
                   "archive - the tree it unpacks IS its files on a disk, so "
                   "Load Program is the point rather than the refusal" % tag)
            if not flags & WF_FLOPPY:
                no("%s: WF_ARC without WF_FLOPPY. The writer sets bit 3 WITH "
                   "bit 4 (SPEC.md 88.2) so that a Wire from before 88.13 "
                   "greys the row with the floppy reason instead of fetching "
                   "a <STEM>.O88 that is not published" % tag)
        elif needkb:
            no("%s: WC_NEEDKB is %d on a record that is not an archive, and "
               "88.2 says zero otherwise" % (tag, needkb))
        nside = blob[o + WC_NSIDE]
        if nside > WIRE_SCMAX:
            no("%s: %d sidecars, and the record holds %d"
               % (tag, nside, WIRE_SCMAX))
        side0 = struct.unpack_from("<H", blob, o + WC_SIDE0)[0]
        if nside and side0 + nside > scn:
            no("%s: sidecars %d..%d are outside a %d-entry table"
               % (tag, side0, side0 + nside - 1, scn))
        if nside and not flags & WF_DISK:
            no("%s: it has sidecars and no WF_DISK, so Load Program would "
               "run it with its files nowhere" % tag)
        size = struct.unpack_from("<I", blob, o + WC_SIZE)[0]
        total = struct.unpack_from("<I", blob, o + WC_TOTAL)[0]
        if size == 0:
            no("%s: the package size is 0" % tag)
        if arc:
            # AN ARCHIVE IS BOUNDED BY WIRE_ARCMAX AND NOT BY WIRE_FILEMAX.
            # WC_SIZE is a dword and an archive is the first body over 64KB by
            # design, so the 63KB bound is simply the wrong one here - it is
            # what refused the site's real RUNCPM record at 231,463 bytes.
            # There is still a ceiling, because Content-Length arrives before
            # the body and the progress figure is computed from it.
            if size >= WIRE_ARCMAX:
                no("%s: the archive is %d bytes and WIRE_ARCMAX is %d, which "
                   "is the FIRST size refused and not the last accepted"
                   % (tag, size, WIRE_ARCMAX))
        elif size > WIRE_FILEMAX and not flags & WF_FLOPPY:
            no("%s: %d bytes, over WIRE_FILEMAX %d, without WF_FLOPPY"
               % (tag, size, WIRE_FILEMAX))
        if total < size and not arc:
            # NOT ON AN ARCHIVE: WC_SIZE is the .WPK on the wire and WC_TOTAL
            # is what it unpacks to, and a tree of many small files carries
            # 96 bytes of container per entry - so the packed file really can
            # be the larger of the two, and it is not a fault.
            no("%s: WC_TOTAL %d is under WC_SIZE %d" % (tag, total, size))
        if arc and needkb < (total + 1023) // 1024:
            no("%s: WC_NEEDKB is %d and %d unpacked bytes need at least %d KB "
               "- a RAM disk sized on it would fill before the tree landed"
               % (tag, needkb, total, (total + 1023) // 1024))
        tot = size
        for j in range(nside):
            e = scoff + (side0 + j) * WIRE_SC
            if e + WIRE_SC > len(blob):
                continue
            nm = blob[e:e + 12]
            if b"\0" not in nm:
                no("%s: sidecar %d's name is not NUL-terminated" % (tag, j))
            sname = nm.split(b"\0")[0].decode("latin1")
            sz = struct.unpack_from("<I", blob, e + WC_SCSIZE)[0]
            tot += sz
            if sz == 0 or (sz > WIRE_FILEMAX and not flags & WF_FLOPPY):
                no("%s: sidecar %s is %d bytes" % (tag, sname, sz))
            if pkgdir:
                # THE SIDECARS ARE CROSS-CHECKED TOO, and they are the half
                # that matters more: the .O88 is fetched under the name the
                # USER chose in the Save dialog, but a sidecar is written
                # under the name in this table and its package looks for
                # exactly that. A size that disagrees with the published file
                # is a transfer wr_hdrdone refuses AFTER the .O88 has already
                # landed, which leaves half a program on somebody's disk.
                sp = os.path.join(pkgdir, sname)
                if not os.path.exists(sp):
                    no("%s: sidecar %s is not in %s" % (tag, sname, pkgdir))
                elif os.path.getsize(sp) != sz:
                    no("%s: sidecar %s is %d bytes in the table and %d on "
                       "disk" % (tag, sname, sz, os.path.getsize(sp)))
        if total != tot and not arc:
            no("%s: WC_TOTAL is %d and the .O88 plus its sidecars is %d - "
               "Add to Disk's free-space check is decided on that figure, so "
               "a wrong one refuses a disk that would have fitted, or fills "
               "one that will not" % (tag, total, tot))

        if pkgdir and arc:
            # The published file is <STEM>.WPK and not <STEM>.O88 (SPEC.md
            # 88.2), and the three figures the record carries are all copies
            # of the archive header's - so the cross-check is against that
            # header rather than against the manifest that produced both.
            path = os.path.join(pkgdir, stem + ".WPK")
            if not os.path.exists(path):
                no("%s: no %s.WPK in %s" % (tag, stem, pkgdir))
            else:
                ab = open(path, "rb").read()
                if len(ab) != size:
                    no("%s: WC_SIZE is %d and %s.WPK is %d - wr_hdrdone "
                       "checks Content-Length against it and refuses the "
                       "transfer" % (tag, size, stem, len(ab)))
                for m in arc_verify(ab, "%s.WPK" % stem):
                    no("%s: %s" % (tag, m))
                try:
                    ahdr, aents = arc_read(ab, "%s.WPK" % stem)
                except Refused:
                    ahdr = None
                if ahdr is not None:
                    if total != ahdr["total"]:
                        no("%s: WC_TOTAL is %d and the archive's own unpacked "
                           "total is %d" % (tag, total, ahdr["total"]))
                    if needkb != ahdr["needkb"]:
                        no("%s: WC_NEEDKB is %d and the archive's own figure "
                           "is %d - Load Program sizes the RAM disk store on "
                           "the record and fills it from the stream"
                           % (tag, needkb, ahdr["needkb"]))
                    icon_check(tag, blob[o + WC_ICON:o + WC_ICON + 64],
                               aents[-1]["data"]
                               if ahdr["flags"] & WAH_PROGRAM else None,
                               "the archive's program entry")
        elif pkgdir:
            path = os.path.join(pkgdir, stem + ".O88")
            alt = os.path.join(pkgdir, stem.lower() + ".o88")
            path = path if os.path.exists(path) else alt
            if not os.path.exists(path):
                no("%s: no %s.O88 in %s" % (tag, stem, pkgdir))
            else:
                b = open(path, "rb").read()
                if len(b) != size:
                    no("%s: WC_SIZE is %d and the file is %d"
                       % (tag, size, len(b)))
                icon_check(tag, blob[o + WC_ICON:o + WC_ICON + 64], b,
                           "the package")
    return bad


# =============================================================================
# DUMP
# =============================================================================
def flagstr(f):
    return "".join(c for c, b in zip("DPNFA", (WF_DISK, WF_PIC, WF_NEW,
                                               WF_FLOPPY, WF_ARC))
                   if f & b) or "-"


def dump(blob, out=sys.stdout):
    n = struct.unpack_from("<H", blob, WC_N)[0]
    scoff = struct.unpack_from("<H", blob, WC_SCOFF)[0]
    scn = struct.unpack_from("<H", blob, WC_SCN)[0]
    print("WIRE v%d, %d records, %d sidecars, %s, %d bytes"
          % (blob[WC_VER], n, scn,
             blob[WC_DATE:WC_DATE + 8].decode("latin1"), len(blob)), file=out)
    print("%-9s %-24s %-9s %-5s %6s %7s %6s %s"
          % ("STEM", "TITLE", "TIER", "FLAGS", "SIZE", "TOTAL", "NEEDKB",
             "SIDECARS"), file=out)
    for i in range(n):
        o = WIRE_HDR + i * WIRE_REC
        stem = blob[o:o + 8].rstrip(b" ").decode("latin1")
        title = blob[o + WC_TITLE:o + WC_TITLE + 24].split(b"\0")[0]
        nside = blob[o + WC_NSIDE]
        side0 = struct.unpack_from("<H", blob, o + WC_SIDE0)[0]
        names = []
        for j in range(nside):
            e = scoff + (side0 + j) * WIRE_SC
            names.append(blob[e:e + 12].split(b"\0")[0].decode("latin1"))
        print("%-9s %-24s %-9s %-5s %6d %7d %6d %s"
              % (stem, title.decode("latin1"), TIERS[blob[o + WC_TIER] & 3],
                 flagstr(blob[o + WC_FLAGS]),
                 struct.unpack_from("<I", blob, o + WC_SIZE)[0],
                 struct.unpack_from("<I", blob, o + WC_TOTAL)[0],
                 struct.unpack_from("<H", blob, o + WC_NEEDKB)[0],
                 ",".join(names)), file=out)
        for k in range(WC_DESCN):
            d = blob[o + WC_DESC + k * WC_DESCW:
                     o + WC_DESC + (k + 1) * WC_DESCW].split(b"\0")[0]
            if d:
                print("          | %s" % d.decode("latin1"), file=out)


# =============================================================================
# THE ARCHIVE - <STEM>.WPK, a folder tree in ONE stream (SPEC.md 88.13)
#
# The win is the CONNECTION and not the ratio. On a stop-and-wait TCP over a
# 4.77 MHz 8088, RunCPM's master disk is fifty-nine handshakes and fifty-nine
# slow starts as fifty-nine files, and one of each as an archive. Compression
# is the smaller half and is in the format because it is nearly free once the
# container exists.
#
# **METHOD 1 IS PINNED BY ITS DECODER**, not by this encoder. The website's
# `tools/wire.py` is a second writer and need not pick the same matches; what
# both owe is a stream `lzss_decode` below reproduces byte for byte, and that
# is the direction `tests/unit/t_wire.py` checks - encode, decode, compare.
# =============================================================================
def lzss_encode(src, chain=48):
    """SPEC.md 88.13's method 1: greedy matching with a one-step lazy check.

    Distance 1..WARC_DISTMAX, length WARC_LENMIN..WARC_LENMAX, groups of a
    flag byte and up to eight items, bit 0 first, a SET bit a literal. There
    is no window buffer on either side: the 8088 decodes into the whole file's
    claim and back-references are copies within it, so a distance is simply an
    offset back into what has already been written.

    `chain` bounds how many candidate positions a match search walks. It is a
    ratio-against-time knob and nothing in the format: the machine never runs
    this code, and the fast tier's budget is what it is for.
    """
    n = len(src)
    heads = {}
    items = []

    def insert(k):
        if k + WARC_LENMIN <= n:
            heads.setdefault(src[k:k + WARC_LENMIN], []).append(k)

    def match(pos):
        """(length, distance) of the best match at `pos`, or (0, 0)."""
        if pos + WARC_LENMIN > n:
            return 0, 0
        lst = heads.get(src[pos:pos + WARC_LENMIN])
        if not lst:
            return 0, 0
        limit = min(WARC_LENMAX, n - pos)
        best_l, best_d, tried = 0, 0, 0
        # Most recent first, so the distance only grows and the first one out
        # of range ends the walk.
        for cand in reversed(lst):
            d = pos - cand
            if d > WARC_DISTMAX:
                break
            tried += 1
            if tried > chain:
                break
            ln = 0
            while ln < limit and src[cand + ln] == src[pos + ln]:
                ln += 1
            if ln > best_l:
                best_l, best_d = ln, d
                if ln == limit:
                    break
        return (best_l, best_d) if best_l >= WARC_LENMIN else (0, 0)

    pos = 0
    while pos < n:
        ln, dist = match(pos)
        insert(pos)                 # AFTER the search: a distance of 0 is not
        if ln:                      # representable, so pos may not match pos
            ln2, _ = match(pos + 1)
            if ln2 > ln:
                # LAZY: a longer match one byte along pays for the literal.
                items.append((True, src[pos]))
                pos += 1
                continue
            for k in range(pos + 1, pos + ln):
                insert(k)
            items.append((False, (dist, ln)))
            pos += ln
        else:
            items.append((True, src[pos]))
            pos += 1

    out = bytearray()
    for i in range(0, len(items), 8):
        group = items[i:i + 8]
        flag = 0
        for k, (lit, _) in enumerate(group):
            if lit:
                flag |= 1 << k
        out.append(flag)
        for lit, v in group:
            if lit:
                out.append(v)
            else:
                dist, ln = v
                out.append((dist - 1) & 0xFF)
                out.append((((dist - 1) >> 8) << 4) | (ln - WARC_LENMIN))
    # NO PADDING ITEMS. The decoder stops on the output's length whatever is
    # left of the group, and then refuses stored bytes it did not consume - so
    # a padded last group is a stream that decodes and is then refused.
    return bytes(out)


def lzss_decode(body, want, what="the entry"):
    """SPEC.md 88.13's decoder, and the four refusals it pins.

    This is the REFERENCE: the 8088's unpacker is a resumable state machine
    over the same rules, and `--verify` is this one run over every entry. The
    refusals are the format's, not this file's - a distance past the start of
    the output, a copy that would pass its end, a stream that ends before the
    output is complete, and stored bytes left over when it is.
    """
    out = bytearray()
    p, n = 0, len(body)
    while len(out) < want:
        if p >= n:
            raise Refused("%s: the stream ends %d bytes short of its %d"
                          % (what, want - len(out), want))
        flag = body[p]
        p += 1
        for bit in range(8):
            if len(out) >= want:
                break
            if flag & (1 << bit):
                if p >= n:
                    raise Refused("%s: the stream ends inside a group, %d "
                                  "bytes short of its %d"
                                  % (what, want - len(out), want))
                out.append(body[p])
                p += 1
            else:
                if p + 2 > n:
                    raise Refused("%s: the stream ends inside a pair, %d "
                                  "bytes short of its %d"
                                  % (what, want - len(out), want))
                b0, b1 = body[p], body[p + 1]
                p += 2
                dist = (((b1 >> 4) << 8) | b0) + 1
                ln = (b1 & 15) + WARC_LENMIN
                if dist > len(out):
                    raise Refused("%s: a distance of %d at output byte %d "
                                  "reaches before the start of the entry"
                                  % (what, dist, len(out)))
                if len(out) + ln > want:
                    raise Refused("%s: a %d-byte copy at output byte %d would "
                                  "pass the entry's %d"
                                  % (what, ln, len(out), want))
                s = len(out) - dist
                # ONE BYTE AT A TIME, which is what makes a distance shorter
                # than the length a RUN - the decoder reads what it has just
                # written. `out[s:s+ln]` is a different format.
                for k in range(ln):
                    out.append(out[s + k])
    if p != n:
        raise Refused("%s: %d stored bytes are left over when the output is "
                      "complete" % (what, n - p))
    return bytes(out)


# --- names: an UPPERCASE 8.3, because the tree lands on a FAT12 volume --------
ARC_NAME_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
ARC_RESERVED = ({"CON", "PRN", "AUX", "NUL"}
                | {"COM%d" % i for i in range(1, 10)}
                | {"LPT%d" % i for i in range(1, 10)})


def arc_name(name, what, folder=False):
    """One path slot: 1..8 stem, 0..3 extension, A-Z 0-9 _ - and nothing else.

    **A TWELVE-CHARACTER NAME FILLS THE SLOT** (SPEC.md 88.13): the slot is
    NUL-padded when the name is shorter and carries no NUL at all when it is
    12, and the reader copies at most twelve bytes and stops at a NUL, which
    is `wr_sputn`'s rule. The first draft of 88.13 said NUL-TERMINATED, and
    five of the RunCPM master disk's 77 files are twelve characters
    (`CONSOLE7.COM`, `LEFT-OFF.TXT`), so the format's own motivating example
    would have been unpackable. The catalog's `WC_SCNAME` is NOT this - it
    stays NUL-terminated inside its 12 (SPEC.md 88.2) and `ascii_field`
    enforces that.

    The writer refuses a lowercase name rather than upper-casing it, and that
    is deliberate: `readme` and `README` are one FAT12 directory entry, so a
    silent fold is two source files landing on top of each other, which the
    machine cannot see and the person who packed it cannot either. The folder
    slots take no extension, which is `tools/os88disk.py`'s `folder83` rule -
    a directory ENTRY is 8.3 at every level.
    """
    if not name:
        raise Refused("%s is empty" % what)
    if name != name.upper():
        raise Refused("%s is %r and every name in an archive is UPPERCASE - "
                      "it lands in a FAT12 directory, where a fold would put "
                      "two files on one entry (SPEC.md 88.13)" % (what, name))
    stem, dot, ext = name.partition(".")
    if folder and dot:
        raise Refused("%s is %r and a folder slot takes no extension" % (what,
                                                                        name))
    if not 1 <= len(stem) <= 8 or len(ext) > 3:
        raise Refused("%s is %r and an 8.3 name is a 1..8 stem and a 0..3 "
                      "extension" % (what, name))
    if stem in ARC_RESERVED:
        raise Refused("%s is %r and %s is a reserved DOS device name"
                      % (what, name, stem))
    for ch in stem + ext:
        if ch not in ARC_NAME_CHARS:
            raise Refused("%s is %r; A-Z 0-9 _ - only" % (what, name))
    if len(name) > WARC_SLOT:
        # Unreachable through the 8.3 check above - 8 + 1 + 3 is 12 - and here
        # because the slot's width is what the format actually pins.
        raise Refused("%s is %d bytes and a slot is %d"
                      % (what, len(name), WARC_SLOT))
    return name


def arc_slot(name):
    """A name in its 12 bytes: NUL-padded when short, FULL when it is 12."""
    return name.encode("ascii") + b"\0" * (WARC_SLOT - len(name))


def needkb_of(sizes):
    """The RAM disk KB a tree needs: the sum of ceil(size / 1024).

    PER ENTRY and not over the total, because a RAM disk store <= 2MB has a
    1KB extent (SPEC.md 62.9.10) and every file rounds up on its own. A tree
    of sixty 100-byte files needs 60KB there and 6KB of bytes.
    """
    return sum((n + 1023) // 1024 for n in sizes)


# =============================================================================
# THE ARCHIVE WRITER
# =============================================================================
def arc_scan(srcdir):
    """[(folders, name)] for every file under `srcdir`, sorted."""
    out = []
    for root, dirs, files in os.walk(srcdir):
        dirs.sort()
        rel = os.path.relpath(root, srcdir)
        parts = [] if rel == "." else rel.split(os.sep)
        for f in sorted(files):
            out.append((tuple(parts), f))
    return out


def arc_order_file(path):
    """A curated order: one relative path a line, '#' a comment."""
    out = []
    for raw in open(path):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.replace("\\", "/").strip("/").split("/")
        out.append((tuple(parts[:-1]), parts[-1]))
    if not out:
        raise Refused("%s names no entries" % path)
    return out


def archive(srcdir, home=None, program=None, order=None):
    """SPEC.md 88.13's writer. Deterministic: no timestamps, stable order."""
    if not os.path.isdir(srcdir):
        raise Refused("%s is not a directory" % srcdir)
    present = set(arc_scan(srcdir))
    if not present:
        raise Refused("%s has no files in it" % srcdir)

    if order:
        want = arc_order_file(order)
        seen = set()
        for key in want:
            if key in seen:
                raise Refused("%s names %s twice"
                              % (order, "/".join(key[0] + (key[1],))))
            seen.add(key)
            if key not in present:
                raise Refused("%s names %s and it is not under %s"
                              % (order, "/".join(key[0] + (key[1],)), srcdir))
        ents = want
    else:
        ents = sorted(present)

    if len(ents) > WARC_NMAX:
        raise Refused("%d entries; the format's count is 1..%d (SPEC.md 88.13)"
                      % (len(ents), WARC_NMAX))

    # --- the program entry, and why it also decides the GROUP order ----------
    prog = None
    if program:
        p = program.replace("\\", "/").strip("/").split("/")
        prog = (tuple(p[:-1]), p[-1])
        if prog not in ents:
            raise Refused("--program names %s and it is not one of the "
                          "entries" % program)
        if prog[0]:
            raise Refused("--program names %s and a program entry is at DEPTH "
                          "0: OSAPI_PKG_RUN runs it with the instance's "
                          "directory on the tree, which is where its overlay "
                          "and sidecars have to be (SPEC.md 88.14)" % program)
        if not prog[1].endswith(".O88"):
            raise Refused("--program names %s and a package is a .O88"
                          % program)
        if order:
            # The caller curated the order, so the caller owns this rule too -
            # moving the entry would be editing their list behind them.
            if ents[-1] != prog:
                raise Refused("--program names %s and it is entry %d of %d; "
                              "the program entry is LAST, so the claim that "
                              "holds it at the end of the transfer is what "
                              "OSAPI_PKG_RUN launches (SPEC.md 88.14)"
                              % (program, ents.index(prog) + 1, len(ents)))
        else:
            # SORTED puts the depth-0 group FIRST (an empty tuple sorts before
            # every other), and lifting one file out of it to the end would
            # leave that folder appearing twice - which is exactly the
            # grouping rule below. So the root group goes LAST as a whole,
            # with the program the last of it. Both of 88.13's ordering rules
            # then hold at once.
            root = [e for e in ents if not e[0] and e != prog]
            rest = [e for e in ents if e[0]]
            ents = rest + root + [prog]

    # --- GROUPED BY FOLDER (SPEC.md 88.13) ----------------------------------
    # The reader banks the folder it is standing in, so a tree in this order
    # costs one folder change per FOLDER rather than one per file - and a
    # folder change on a floppy is OSAPI_FILE_FIND walks and a GOTO, each
    # priced in int 13h calls (PERFORMANCE.md).
    groups = []
    for folders, _ in ents:
        if not groups or groups[-1] != folders:
            if folders in groups:
                raise Refused("the folder %s appears in two runs of entries; "
                              "88.13 groups by folder so the reader enters "
                              "each one once" % ("/".join(folders) or "<root>"))
            groups.append(folders)

    hometxt = ""
    if home:
        hometxt = arc_name(home, "the home folder %r" % home, folder=True)

    bodies, sizes, hdrs = [], [], []
    for folders, name in ents:
        if len(folders) > WARC_DEPTH:
            raise Refused("%s is %d folders deep and WARC_DEPTH is %d - the "
                          "path is %d slots and the last of them is the file "
                          "name" % ("/".join(folders + (name,)), len(folders),
                                    WARC_DEPTH, WARC_SLOTS))
        for f in folders:
            arc_name(f, "the folder %r in %s"
                     % (f, "/".join(folders + (name,))), folder=True)
        arc_name(name, "the file name in %s" % "/".join(folders + (name,)))

        raw = open(os.path.join(srcdir, *(folders + (name,))), "rb").read()
        if len(raw) > WIRE_FILEMAX:
            raise Refused("%s is %d bytes and WIRE_FILEMAX is %d - the reader "
                          "makes ONE claim for the largest entry and writes "
                          "each file with ONE OSAPI_FILE_WRITE (SPEC.md 88.13)"
                          % ("/".join(folders + (name,)), len(raw),
                             WIRE_FILEMAX))
        enc = lzss_encode(raw) if raw else b""
        if enc and len(enc) < len(raw):
            method, body = WAM_LZSS, enc
        else:
            # NOT SMALLER IS NOT WORTH IT: a stored entry costs the reader no
            # decode at all, and an entry that grew would cost wire as well.
            method, body = WAM_STORED, raw

        e = bytearray(WARC_ENT)
        e[WA_METHOD] = method
        e[WA_DEPTH] = len(folders)
        struct.pack_into("<I", e, WA_STORED, len(body))
        struct.pack_into("<I", e, WA_SIZE, len(raw))
        for k, slot in enumerate(list(folders) + [name]):
            e[WA_PATH + k * WARC_SLOT:WA_PATH + (k + 1) * WARC_SLOT] = \
                arc_slot(slot)
        hdrs.append(bytes(e))
        bodies.append(body)
        sizes.append(len(raw))

    hdr = bytearray(WARC_HDR)
    hdr[WA_MAGIC:WA_MAGIC + 4] = AMAGIC
    hdr[WA_VER] = WARC_VER
    hdr[WA_FLAGS] = WAH_PROGRAM if prog else 0
    struct.pack_into("<H", hdr, WA_N, len(ents))
    struct.pack_into("<I", hdr, WA_TOTAL, sum(sizes))
    struct.pack_into("<H", hdr, WA_NEEDKB, needkb_of(sizes))
    struct.pack_into("<H", hdr, WA_MAXENT, max(sizes))
    hdr[WA_HOME:WA_HOME + WARC_SLOT] = arc_slot(hometxt)

    out = bytearray(hdr)
    for e, b in zip(hdrs, bodies):
        out += e
        out += b
    if len(out) >= WIRE_ARCMAX:
        # The same bound `arc_verify` and the catalog's WC_SIZE check use, so
        # the writer never hands back an archive that cannot be published.
        raise Refused("the archive is %d bytes and WIRE_ARCMAX is %d, the "
                      "FIRST size refused - the "
                      "record's WC_SIZE is checked against Content-Length "
                      "before the body arrives, so this one has no path "
                      "through the machine (SPEC.md 88.2). Split the tree, or "
                      "curate it with --order" % (len(out), WIRE_ARCMAX))
    return bytes(out)


# =============================================================================
# THE ARCHIVE READER - SPEC.md 88.13's checks, written to REFUSE
# =============================================================================
def arc_read(blob, name="the archive", decode=True):
    """(header, entries) or Refused.

    Every check here is one the 8088's reader makes as the bytes arrive, and
    it says `That archive is not understood` for all of them at once. This
    one names the entry and the field, because a host can.
    """
    def no(msg):
        raise Refused("%s: %s" % (name, msg))

    def slot(buf, off, what, folder=False, allow_empty=False):
        # AT MOST TWELVE BYTES, STOPPING AT A NUL (SPEC.md 88.13): a slot with
        # no NUL in it is a twelve-character name and not a fault. What is
        # still a fault is a byte AFTER the NUL - the padding is zero, and a
        # reader that stops at the first NUL would never see what follows it.
        s = bytes(buf[off:off + WARC_SLOT])
        txt = s.split(b"\0")[0]
        if s[len(txt):] != b"\0" * (WARC_SLOT - len(txt)):
            no("%s has bytes after its NUL" % what)
        if not txt:
            if allow_empty:
                return ""
            no("%s is empty" % what)
        return arc_name(txt.decode("latin1"), what, folder=folder)

    if len(blob) < WARC_HDR:
        no("%d bytes, shorter than the %d-byte header" % (len(blob), WARC_HDR))
    if bytes(blob[WA_MAGIC:WA_MAGIC + 4]) != AMAGIC:
        no("magic is %r and not %r" % (bytes(blob[0:4]), AMAGIC))
    if blob[WA_VER] != WARC_VER:
        no("format version %d and not %d" % (blob[WA_VER], WARC_VER))
    hflags = blob[WA_FLAGS]
    if hflags & ~WAH_PROGRAM:
        no("header flag bits 0x%02X, and only WAH_PROGRAM is defined"
           % hflags)
    n = struct.unpack_from("<H", blob, WA_N)[0]
    if not 1 <= n <= WARC_NMAX:
        no("entry count %d, and it must be 1..%d" % (n, WARC_NMAX))
    if bytes(blob[28:WARC_HDR]) != b"\0" * 4:
        no("the header's +28 is not zero")
    maxent = struct.unpack_from("<H", blob, WA_MAXENT)[0]
    if maxent > WIRE_FILEMAX:
        no("the largest-entry figure is %d and WIRE_FILEMAX is %d - it is the "
           "ONE claim the reader makes for the whole transfer"
           % (maxent, WIRE_FILEMAX))
    hdr = {"flags": hflags, "n": n,
           "total": struct.unpack_from("<I", blob, WA_TOTAL)[0],
           "needkb": struct.unpack_from("<H", blob, WA_NEEDKB)[0],
           "maxent": maxent,
           "home": slot(blob, WA_HOME, "%s's home folder" % name,
                        folder=True, allow_empty=True)}

    ents, p = [], WARC_HDR
    for i in range(n):
        if p + WARC_ENT > len(blob):
            no("entry %d's header runs past the end of the file" % i)
        e = blob[p:p + WARC_ENT]
        tag = "entry %d" % i
        method = e[WA_METHOD]
        if method not in (WAM_STORED, WAM_LZSS):
            no("%s has method %d" % (tag, method))
        depth = e[WA_DEPTH]
        if depth > WARC_DEPTH:
            no("%s has depth %d and WARC_DEPTH is %d" % (tag, depth,
                                                         WARC_DEPTH))
        if bytes(e[2:4]) != b"\0\0" or bytes(e[12:16]) != b"\0" * 4:
            no("%s has a reserved field that is not zero" % tag)
        stored = struct.unpack_from("<I", e, WA_STORED)[0]
        size = struct.unpack_from("<I", e, WA_SIZE)[0]
        if size > WIRE_FILEMAX:
            no("%s is %d bytes unpacked and WIRE_FILEMAX is %d"
               % (tag, size, WIRE_FILEMAX))
        if size > maxent:
            no("%s is %d bytes and the header's largest-entry figure is %d - "
               "the claim was made from that figure alone"
               % (tag, size, maxent))
        folders = tuple(slot(e, WA_PATH + k * WARC_SLOT,
                             "%s's folder slot %d" % (tag, k), folder=True)
                        for k in range(depth))
        fname = slot(e, WA_PATH + depth * WARC_SLOT, "%s's file name" % tag)
        for k in range(depth + 1, WARC_SLOTS):
            if bytes(e[WA_PATH + k * WARC_SLOT:
                       WA_PATH + (k + 1) * WARC_SLOT]) != b"\0" * WARC_SLOT:
                no("%s: path slot %d is past the file name and is not all-NUL"
                   % (tag, k))
        p += WARC_ENT
        if p + stored > len(blob):
            no("%s's %d-byte body runs past the end of the file"
               % (tag, stored))
        body = bytes(blob[p:p + stored])
        p += stored
        if method == WAM_STORED and stored != size:
            no("%s is stored and its body is %d bytes for an unpacked %d"
               % (tag, stored, size))
        data = None
        if decode:
            if method == WAM_STORED:
                data = body
            else:
                data = lzss_decode(body, size, "%s (%s)"
                                   % (tag, "/".join(folders + (fname,))))
        ents.append({"method": method, "depth": depth, "stored": stored,
                     "size": size, "folders": folders, "name": fname,
                     "data": data})
    if p != len(blob):
        no("%d bytes are left over after the last entry's body"
           % (len(blob) - p))
    return hdr, ents


def arc_verify(blob, name="the archive"):
    """[complaints]. Empty is a good archive."""
    if len(blob) >= WIRE_ARCMAX:
        # FIRST, and before the structure: the whole file is what the record's
        # WC_SIZE will claim and what Content-Length is checked against, so an
        # archive over the bound cannot be published whatever is inside it.
        return ["%s: %d bytes and WIRE_ARCMAX is %d, the FIRST size refused "
                "- WC_SIZE is checked "
                "against Content-Length before the body, and the progress "
                "figure is computed from it (SPEC.md 88.2)"
                % (name, len(blob), WIRE_ARCMAX)]
    try:
        hdr, ents = arc_read(blob, name)
    except Refused as e:
        # The entries are read in sequence, so a structural fault makes
        # everything after it meaningless: one honest complaint, not fifty
        # guessed ones.
        return [str(e)]

    bad = []
    sizes = [e["size"] for e in ents]
    if hdr["total"] != sum(sizes):
        bad.append("%s: the header's unpacked total is %d and the entries add "
                   "to %d - Add to Disk's free-space check is decided on that "
                   "figure (SPEC.md 88.2's WC_TOTAL)"
                   % (name, hdr["total"], sum(sizes)))
    if hdr["needkb"] != needkb_of(sizes):
        bad.append("%s: the header's need-KB is %d and the entries need %d at "
                   "a 1KB extent - Load Program sizes the RAM disk on it "
                   "(SPEC.md 88.14)" % (name, hdr["needkb"],
                                        needkb_of(sizes)))
    if hdr["maxent"] != max(sizes):
        bad.append("%s: the header's largest entry is %d and the largest is "
                   "%d" % (name, hdr["maxent"], max(sizes)))

    groups, seen = [], set()
    for e in ents:
        if not groups or groups[-1] != e["folders"]:
            if e["folders"] in seen:
                bad.append("%s: the folder %s appears in two runs of entries, "
                           "so the reader enters it twice - 88.13 groups by "
                           "folder" % (name,
                                       "/".join(e["folders"]) or "<root>"))
            groups.append(e["folders"])
            seen.add(e["folders"])
    paths = ["/".join(e["folders"] + (e["name"],)) for e in ents]
    for i, path in enumerate(paths):
        if path in paths[:i]:
            bad.append("%s: %s appears twice, and the second write lands on "
                       "the first" % (name, path))

    if hdr["flags"] & WAH_PROGRAM:
        last = ents[-1]
        if last["depth"] or not last["name"].endswith(".O88"):
            bad.append("%s: WAH_PROGRAM is set and the last entry is %s - it "
                       "is the .O88 at depth 0 that OSAPI_PKG_RUN launches "
                       "(SPEC.md 88.14)" % (name, paths[-1]))
        elif last["data"] is not None:
            try:
                o88_header(last["data"], paths[-1])
            except Refused as e:
                bad.append("%s: %s" % (name, e))
    return bad


def arc_icon(hdr, ents, name):
    """The 64 bytes a WF_ARC record's WC_ICON carries.

    An archive's picture is its PROGRAM's own OS88_ICON16 when it declares
    one, for the reason a package's is: the Disk window draws that icon for
    the file the tree lands as, and a row showing something else puts one
    program under two pictures. An archive with no WAH_PROGRAM - a CP/M game
    into RUNCPM/A/1 - has no program to ask, so it gets the generic.
    """
    if hdr["flags"] & WAH_PROGRAM and ents[-1]["data"] is not None:
        try:
            return icon_of(ents[-1]["data"], "%s's program entry" % name)
        except Refused:
            pass
    return struct.pack("<32H", *(GENERIC_ICON_MASK + GENERIC_ICON_DATA))


def arc_dump(blob, name, out=sys.stdout):
    hdr, ents = arc_read(blob, name)
    print("WPAK v%d, %d entries, %d bytes packed, %d unpacked, %d KB needed, "
          "largest %d%s%s"
          % (WARC_VER, hdr["n"], len(blob), hdr["total"], hdr["needkb"],
             hdr["maxent"],
             ", home %s" % hdr["home"] if hdr["home"] else ", no home",
             ", WAH_PROGRAM" if hdr["flags"] & WAH_PROGRAM else ""), file=out)
    print("%-4s %-6s %8s %8s %5s  %s"
          % ("#", "METHOD", "STORED", "UNPACKED", "%", "PATH"), file=out)
    for i, e in enumerate(ents):
        pct = (100 * e["stored"] // e["size"]) if e["size"] else 100
        print("%-4d %-6s %8d %8d %4d%%  %s"
              % (i, "LZSS" if e["method"] == WAM_LZSS else "stored",
                 e["stored"], e["size"], pct,
                 "/".join(e["folders"] + (e["name"],))), file=out)


# =============================================================================
# THE PICTURE - a stdlib PNG reader, and a 1:1 CROP (SPEC.md 88.3)
#
# The site's captures are what the machine drew: 1-bit grayscale off a
# Hercules or a CGA, 4-bit palette off a VGA. So the reader handles bit depths
# 1, 2, 4 and 8 for grayscale and palette and 8-bit truecolour, which covers
# every screenshot this project produces and nothing it does not.
#
# It is a CROP and never a scale. A scaled 16-colour UI is mush; a crop is the
# program, at the pixels it was drawn with.
# =============================================================================
def png_read(path):
    """(width, height, getpixel) where getpixel(x, y) -> (r, g, b)."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise Refused("%s is not a PNG" % path)
    pos, idat, plte, hdr, trns = 8, [], None, None, None
    while pos + 8 <= len(data):
        ln, typ = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE":
            plte = body
        elif typ == b"tRNS":
            trns = body
        elif typ == b"IDAT":
            idat.append(body)
        elif typ == b"IEND":
            break
        pos += 12 + ln
    if hdr is None:
        raise Refused("%s has no IHDR" % path)
    w, h, depth, ctype, comp, filt, interlace = hdr
    if interlace:
        raise Refused("%s is interlaced; the site's captures are not" % path)
    if ctype not in (0, 2, 3, 6):
        raise Refused("%s has colour type %d" % (path, ctype))
    chans = {0: 1, 2: 3, 3: 1, 6: 4}[ctype]
    if ctype in (2, 6) and depth != 8:
        raise Refused("%s is %d-bit truecolour; 8 only" % (path, depth))
    if depth not in (1, 2, 4, 8):
        raise Refused("%s is %d bits a sample" % (path, depth))

    raw = zlib.decompress(b"".join(idat))
    bpp = max(1, chans * depth // 8)
    stride = (w * chans * depth + 7) // 8
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        ft = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                c = prev[i - bpp] if i >= bpp else 0
                b = prev[i]
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif ft != 0:
            raise Refused("%s uses filter %d" % (path, ft))
        rows.append(bytes(line))
        prev = line

    maxv = (1 << depth) - 1

    def sample(row, i):
        if depth == 8:
            return row[i]
        per = 8 // depth
        return (row[i // per] >> (8 - depth * (i % per + 1))) & maxv

    def getpixel(x, y):
        row = rows[y]
        if ctype == 3:
            idx = sample(row, x)
            if plte is None or idx * 3 + 3 > len(plte):
                return (0, 0, 0)
            return tuple(plte[idx * 3:idx * 3 + 3])
        if ctype == 0:
            v = sample(row, x) * 255 // maxv
            return (v, v, v)
        v = row[x * chans:x * chans + 3]
        return (v[0], v[1], v[2])

    return w, h, getpixel


def cut(path, x0, y0):
    """WIRE_PICW x WIRE_PICH at (x0, y0), 1 = ink (SPEC.md 88.3)."""
    w, h, get = png_read(path)
    if x0 < 0 or y0 < 0 or x0 + WIRE_PICW > w or y0 + WIRE_PICH > h:
        raise Refused("a %dx%d crop at (%d, %d) does not fit a %dx%d image"
                      % (WIRE_PICW, WIRE_PICH, x0, y0, w, h))
    out = bytearray(WIRE_PICSZ)
    for y in range(WIRE_PICH):
        for x in range(WIRE_PICW):
            r, g, b = get(x0 + x, y0 + y)
            # Rec. 601 luma, and the threshold is the MIDPOINT: everything
            # this reads is a two-tone or sixteen-colour UI, so there is no
            # dither to preserve and nothing in the middle to lose.
            if (r * 299 + g * 587 + b * 114) // 1000 < 128:
                out[y * WIRE_PICB + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


# =============================================================================
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pack", metavar="MANIFEST.json")
    ap.add_argument("--pkgdir", metavar="DIR",
                    help="with --pack, where the .o88 files and sidecars are, "
                         "under the names the manifest gives; with --verify, "
                         "the PUBLISHED /wire/pkg/ directory, where each one "
                         "is named <STEM>.O88")
    ap.add_argument("--picdir", metavar="DIR",
                    help="where <STEM>.PIC files are; a stem with one gets "
                         "WF_PIC")
    ap.add_argument("--date", metavar="YYYYMMDD")
    ap.add_argument("--verify", metavar="FILE",
                    help="a catalog.bin or a .WPK; the magic says which")
    ap.add_argument("--dump", metavar="FILE",
                    help="a catalog.bin or a .WPK; the magic says which")
    ap.add_argument("--archive", metavar="OUT.WPK",
                    help="pack --srcdir's tree into this archive "
                         "(SPEC.md 88.13)")
    ap.add_argument("--srcdir", metavar="DIR",
                    help="with --archive, the folder tree to pack; every file "
                         "and folder name an uppercase 8.3")
    ap.add_argument("--home", metavar="NAME",
                    help="with --archive, the folder the whole tree lands "
                         "under ('RUNCPM'). Omitted, it lands where the "
                         "dialog points")
    ap.add_argument("--program", metavar="FILE",
                    help="with --archive, the .O88 at depth 0 that Load "
                         "Program runs when the tree has landed; it is "
                         "written LAST and sets WAH_PROGRAM")
    ap.add_argument("--order", metavar="LIST.txt",
                    help="with --archive, a curated order: one relative path "
                         "a line, '#' a comment. It also SELECTS - a file "
                         "under --srcdir that it does not name is left out")
    ap.add_argument("--pic", metavar="IN.png")
    ap.add_argument("--crop", metavar="X,Y", default="0,0")
    ap.add_argument("-o", "--out", metavar="OUT")
    a = ap.parse_args()

    try:
        if a.pack:
            if not a.pkgdir or not a.out:
                sys.exit("os88wire: --pack needs --pkgdir and --out")
            m = json.load(open(a.pack))
            blob = pack(m, a.pkgdir, a.picdir, a.date)
            with open(a.out, "wb") as f:
                f.write(blob)
            n = struct.unpack_from("<H", blob, WC_N)[0]
            print("os88wire: %s - %d records, %d bytes of %d"
                  % (a.out, n, len(blob), WIRE_CATMAX))
            bad = verify(blob)          # THE WRITER CHECKS ITS OWN OUTPUT.
            if bad:                     # A packer and a verifier that never
                for b in bad:           # meet is two readings of one format,
                    print("  FAIL: " + b)   # which is the drift this file
                return 1                    # exists to prevent.
            return 0                        #
                                            # NOT with --pkgdir: that
                                            # cross-check is against the
                                            # PUBLISHED /wire/pkg/, where a
                                            # file is named <STEM>.O88, and
                                            # --pkgdir here is the BUILD tree,
                                            # where it is named whatever the
                                            # Makefile calls it
        if a.archive:
            if not a.srcdir:
                sys.exit("os88wire: --archive needs --srcdir")
            blob = archive(a.srcdir, a.home, a.program, a.order)
            with open(a.archive, "wb") as f:
                f.write(blob)
            hdr, _ = arc_read(blob, a.archive)
            print("os88wire: %s - %d entries, %d bytes of %d unpacked (%d%%), "
                  "%d KB needed"
                  % (a.archive, hdr["n"], len(blob), hdr["total"],
                     100 * len(blob) // max(1, hdr["total"]), hdr["needkb"]))
            bad = arc_verify(blob, a.archive)   # THE WRITER CHECKS ITS OWN
            for b in bad:                       # OUTPUT, for --pack's reason
                print("  FAIL: " + b)
            return 1 if bad else 0
        if a.verify:
            blob = open(a.verify, "rb").read()
            # THE MAGIC SAYS WHICH. Both formats are this file's and both are
            # read off the same /wire/ tree, so one flag rather than two is
            # one fewer way to point the wrong reader at a file.
            if blob[:4] == AMAGIC:
                bad = arc_verify(blob, os.path.basename(a.verify))
            else:
                bad = verify(blob, a.pkgdir)
            for b in bad:
                print("FAIL: " + b)
            print("os88wire: %s %s" % (a.verify, "FAILED" if bad else "ok"))
            return 1 if bad else 0
        if a.dump:
            blob = open(a.dump, "rb").read()
            if blob[:4] == AMAGIC:
                arc_dump(blob, os.path.basename(a.dump))
            else:
                dump(blob)
            return 0
        if a.pic:
            if not a.out:
                sys.exit("os88wire: --pic needs --out")
            x, y = (int(v) for v in a.crop.split(","))
            with open(a.out, "wb") as f:
                f.write(cut(a.pic, x, y))
            print("os88wire: %s - %dx%d at (%d, %d), %d bytes"
                  % (a.out, WIRE_PICW, WIRE_PICH, x, y, WIRE_PICSZ))
            return 0
    except Refused as e:
        print("os88wire: refused - %s" % e, file=sys.stderr)
        return 1
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())

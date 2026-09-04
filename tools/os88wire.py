#!/usr/bin/env python3
"""The Wire's catalog: pack it, verify it, dump it, and cut a picture for it.

    python3 tools/os88wire.py --pack data/wire.json --pkgdir build/ \\
                              --picdir build/wirepic --out build/catalog.bin
    python3 tools/os88wire.py --verify build/catalog.bin [--pkgdir build/]
    python3 tools/os88wire.py --dump build/catalog.bin
    python3 tools/os88wire.py --pic shot.png --crop 96,120 --out HELLO.PIC

SPEC.md 88.2 and 88.3 are the format. This file is the OS repo's writer and
its reader; the website's `tools/wire.py` is an INDEPENDENT SECOND WRITER of
the same bytes, and `--verify` run against the site's published
`catalog.bin` is the cross-check between them - the arrangement
`tests/unit/t_wab.py` has over a `.WAB`, one format along.

**THE VERIFIER DOES NOT COMPARE A GENERIC ICON.** A record whose package
declares an `OS88_ICON16` (header flags bit 0) must carry exactly those 64
bytes and `--verify --pkgdir` says so. A record whose package declares none
carries a generic, and SPEC.md 88.2 leaves which generic to the writer - so
two independent writers may choose differently and both be right. What is
checked there is the one thing that is a fault either way: an icon with no
pixels in it, which `icon_draw_x` accepts and draws as nothing.

**EVERY OFFSET BELOW IS MIRRORED IN apps/thewire/wcat.inc**, and
`tests/unit/t_wire.py` compares the two files in the FAST tier. There is no
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

# --- the picture (SPEC.md 88.3) -----------------------------------------------
WIRE_PICW = 128
WIRE_PICH = 64
WIRE_PICB = 16
WIRE_PICSZ = 1024

MAGIC = b"WIRE"
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

        fl = e.get("flags") or {}
        flags = 0
        if fl.get("new"):
            flags |= WF_NEW
        if fl.get("floppy_only"):
            flags |= WF_FLOPPY

        # WF_DISK IS DERIVED AND NOT DECLARED. It is "this program's files
        # must be on a disk", and there are exactly three ways to be that: a
        # sidecar beside the .o88, a package that carries PARTS (header flags
        # bit 2, SPEC.md 20.12 - it reads them out of its own FILE), or
        # something the manifest says outright. Deriving it is what makes
        # "a sidecar without a WF_DISK" unrepresentable rather than refused.
        hflags, _, _ = o88_header(blobs[0], files[0])
        if len(files) > 1 or (hflags & O88_F_PARTS) or fl.get("needs_disk"):
            flags |= WF_DISK

        if picdir and os.path.exists(os.path.join(picdir, stem + ".PIC")):
            pic = open(os.path.join(picdir, stem + ".PIC"), "rb").read()
            if len(pic) != WIRE_PICSZ:
                raise Refused("%s.PIC is %d bytes and a picture is %d "
                              "(SPEC.md 88.3)" % (stem, len(pic), WIRE_PICSZ))
            flags |= WF_PIC

        for f, n in zip(files, sizes):
            if n == 0:
                raise Refused("%s is empty" % f)
            if n > WIRE_FILEMAX and not (flags & WF_FLOPPY):
                raise Refused("%s is %d bytes, over WIRE_FILEMAX (%d), and the "
                              "entry is not floppy_only - the Wire moves a "
                              "file in ONE claim and ONE write (SPEC.md 88.2)"
                              % (f, n, WIRE_FILEMAX))

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
        nside = len(files) - 1
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
        struct.pack_into("<I", r, WC_SIZE, sizes[0])
        struct.pack_into("<I", r, WC_TOTAL, sum(sizes))
        r[WC_ICON:WC_ICON + 64] = icon_of(blobs[0], files[0])
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
        if flags & ~(WF_DISK | WF_PIC | WF_NEW | WF_FLOPPY):
            no("%s: unknown flag bits 0x%02X" % (tag, flags))
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
        if size > WIRE_FILEMAX and not flags & WF_FLOPPY:
            no("%s: %d bytes, over WIRE_FILEMAX %d, without WF_FLOPPY"
               % (tag, size, WIRE_FILEMAX))
        if total < size:
            no("%s: WC_TOTAL %d is under WC_SIZE %d" % (tag, total, size))
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
        if total != tot:
            no("%s: WC_TOTAL is %d and the .O88 plus its sidecars is %d - "
               "Add to Disk's free-space check is decided on that figure, so "
               "a wrong one refuses a disk that would have fitted, or fills "
               "one that will not" % (tag, total, tot))

        if pkgdir:
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
                got = blob[o + WC_ICON:o + WC_ICON + 64]
                try:
                    hflags, _, _ = o88_header(b, path)
                except Refused as ex:
                    no("%s: %s" % (tag, ex))
                    hflags = None
                if hflags is None:
                    pass
                elif hflags & O88_F_ICON:
                    # IT DECLARES ONE, so the record must carry THAT one:
                    # anything else shows a program under another program's
                    # picture, and the row is drawn from these 64 bytes alone.
                    if got != b[32:96]:
                        no("%s: the package declares an OS88_ICON16 and the "
                           "record carries different bytes" % tag)
                else:
                    # IT DECLARES NONE, so the record carries a GENERIC one -
                    # and SPEC.md 88.2 says "or the site's generic program
                    # icon", which is a choice the writer makes rather than a
                    # value this can compare against. Two writers may pick
                    # different generics and both be right, so what is checked
                    # is the only thing that is actually a fault: an icon with
                    # no pixels in it. icon_draw_x accepts an all-zero record
                    # and draws nothing at all, so a blank one is a row that
                    # silently has no picture rather than a refusal anybody
                    # sees.
                    if not any(got):
                        no("%s: the package declares no icon and the record's "
                           "generic is 64 zero bytes - the row would draw "
                           "nothing at all" % tag)
                    elif not any(got[32:]):
                        no("%s: the generic icon's DATA rows are all zero, so "
                           "the row draws a blank white block" % tag)
    return bad


# =============================================================================
# DUMP
# =============================================================================
def flagstr(f):
    return "".join(c for c, b in zip("DPNF", (WF_DISK, WF_PIC, WF_NEW,
                                              WF_FLOPPY)) if f & b) or "-"


def dump(blob, out=sys.stdout):
    n = struct.unpack_from("<H", blob, WC_N)[0]
    scoff = struct.unpack_from("<H", blob, WC_SCOFF)[0]
    scn = struct.unpack_from("<H", blob, WC_SCN)[0]
    print("WIRE v%d, %d records, %d sidecars, %s, %d bytes"
          % (blob[WC_VER], n, scn,
             blob[WC_DATE:WC_DATE + 8].decode("latin1"), len(blob)), file=out)
    print("%-9s %-24s %-9s %-5s %5s %6s %s"
          % ("STEM", "TITLE", "TIER", "FLAGS", "SIZE", "TOTAL", "SIDECARS"),
          file=out)
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
        print("%-9s %-24s %-9s %-5s %5d %6d %s"
              % (stem, title.decode("latin1"), TIERS[blob[o + WC_TIER] & 3],
                 flagstr(blob[o + WC_FLAGS]),
                 struct.unpack_from("<I", blob, o + WC_SIZE)[0],
                 struct.unpack_from("<I", blob, o + WC_TOTAL)[0],
                 ",".join(names)), file=out)
        for k in range(WC_DESCN):
            d = blob[o + WC_DESC + k * WC_DESCW:
                     o + WC_DESC + (k + 1) * WC_DESCW].split(b"\0")[0]
            if d:
                print("          | %s" % d.decode("latin1"), file=out)


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
    ap.add_argument("--verify", metavar="CATALOG.bin")
    ap.add_argument("--dump", metavar="CATALOG.bin")
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
        if a.verify:
            blob = open(a.verify, "rb").read()
            bad = verify(blob, a.pkgdir)
            for b in bad:
                print("FAIL: " + b)
            print("os88wire: %s %s" % (a.verify, "FAILED" if bad else "ok"))
            return 1 if bad else 0
        if a.dump:
            dump(open(a.dump, "rb").read())
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

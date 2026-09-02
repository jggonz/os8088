#!/usr/bin/env python3
"""os88iso: wrap the live hard-disk image in a bootable CD (SPEC.md 80.2).

    python3 tools/os88iso.py -o OUT.iso --boot-image os8088-usb.img
                             [--file NAME=PATH ...]

The output is an ISO9660 volume with an El Torito boot catalog whose one
entry is media type 4 - HARD DISK EMULATION: the BIOS presents the image as
int 13h drive 80h, loads its MBR at 0000:7C00, and every read the MBR, the
volume boot record and the kernel make is serviced out of the file. Nothing
that runs can tell the CD from the USB stick the same image is written to,
which is the point - one image, two wrappers (SPEC.md 80.2).

The boot image must therefore BE a partitioned hard-disk image - what
`os88disk.py --hdd` builds - and this refuses anything without an MBR
signature and an active type-04h entry, because El Torito hard-disk
emulation is defined over exactly that shape and a BIOS derives the
emulated geometry from that table.

The image also appears in the ISO's root directory as an ordinary file
(OS8088HD.IMG), beside whatever --file adds - so a host that mounts the CD
can copy the raw image off it and write a stick without a second download.

Deliberately NOT here, so this stays reviewable and deterministic:
- No Rock Ridge, no Joliet: 8.3 upper-case names are what this project's
  own volumes use, and every file this carries is one already.
- No directories: the handful of files ride in the root.
- No isohybrid MBR on the ISO itself: the raw image for a USB stick is
  os8088-usb.img, carried INSIDE this ISO and in the release zip beside it,
  so overloading the ISO to be both would be a second copy of the boot
  chain to keep in step (SPEC.md 80.2).

Everything is fixed - timestamps, volume id, layout order - so the same
inputs produce the same bytes, exactly like every disk image here.
"""
import argparse
import os
import struct
import sys

LBS = 2048                       # ISO9660 logical block
VOLUME_ID = b"OS8088"
BOOT_NAME = "OS8088HD.IMG"
# One fixed timestamp for every field, os88disk.py's FIXED_DATE restated:
# 2026-01-01 00:00:00, zone 0.
YMDHMS = (2026, 1, 1, 0, 0, 0)


def fail(msg):
    sys.stderr.write("os88iso: error: %s\n" % msg)
    sys.exit(1)


def both16(n):
    return struct.pack("<H", n) + struct.pack(">H", n)


def both32(n):
    return struct.pack("<I", n) + struct.pack(">I", n)


def dir_date():
    y, mo, d, h, mi, s = YMDHMS
    return bytes([y - 1900, mo, d, h, mi, s, 0])


def vol_date():
    return b"%04d%02d%02d%02d%02d%02d00" % YMDHMS + b"\x00"


def dirent(name, extent, size, is_dir):
    """One directory record. `name` is bytes: b'\\x00' and b'\\x01' are the
    '.' and '..' links, anything else an 8.3;1 file identifier."""
    rec = bytearray()
    rec += b"\x00"                               # length, patched below
    rec += b"\x00"                               # extended attr length
    rec += both32(extent)
    rec += both32(size)
    rec += dir_date()
    rec += bytes([2 if is_dir else 0])           # flags
    rec += b"\x00\x00"                           # unit size, gap
    rec += both16(1)                             # volume sequence number
    rec += bytes([len(name)]) + name
    if len(rec) & 1:
        rec += b"\x00"                           # pad to even length
    rec[0] = len(rec)
    return bytes(rec)


def name_iso(name):
    """Validate an 8.3 name and return its ISO file identifier."""
    up = name.upper()
    stem, _, ext = up.partition(".")
    if not 1 <= len(stem) <= 8 or not ext or len(ext) > 3:
        fail("'%s' is not an 8.3 NAME.EXT" % name)
    ok = set(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
    if any(c not in ok for c in (stem + ext).encode("ascii", "replace")):
        fail("'%s' has characters outside A-Z 0-9 _" % name)
    return (up + ";1").encode()


def check_boot_image(img):
    """El Torito hard-disk emulation is defined over a partitioned image:
    an MBR whose active entry the BIOS reads the emulated geometry from.
    Refuse anything else here, where the message can say what is wrong -
    booted, the same defect is a machine that hangs with a dark screen."""
    if len(img) < 512 or img[510:512] != b"\x55\xaa":
        fail("boot image has no MBR signature - os88disk.py --hdd builds "
             "the image this wraps (SPEC.md 80.1)")
    for i in range(4):
        e = img[446 + i * 16:446 + i * 16 + 16]
        if e[0] == 0x80:
            if e[4] != 0x04:
                fail("active partition is type 0x%02X, not the 04h "
                     "(FAT16 under 32MB) SPEC.md 80.1 pins" % e[4])
            return
    fail("boot image has no active partition entry")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--boot-image", required=True,
                    help="the os88disk.py --hdd image; El Torito boots it "
                         "as an emulated hard disk and it is listed in the "
                         "root as %s" % BOOT_NAME)
    ap.add_argument("--file", action="append", default=[], metavar="NAME=PATH",
                    help="another file for the root directory (repeatable), "
                         "e.g. README.TXT=build/readme.txt")
    a = ap.parse_args()

    boot = open(a.boot_image, "rb").read()
    check_boot_image(boot)

    files = []                                   # (iso id, bytes, is_boot)
    for spec in a.file:
        nm, sep, path = spec.partition("=")
        if not sep:
            fail("--file wants NAME=PATH, not %r" % spec)
        files.append((name_iso(nm), open(path, "rb").read(), False))
    files.append((name_iso(BOOT_NAME), boot, True))
    files.sort(key=lambda f: f[0])               # ISO9660 orders the record

    # --- layout, in whole logical blocks -------------------------------------
    # 0..15 system area, 16 PVD, 17 boot record, 18 terminator, 19 boot
    # catalog, 20/21 the two path tables, 22 the root directory, then the
    # file extents in record order.
    pvd_lba, brvd_lba, term_lba = 16, 17, 18
    cat_lba, ptl_lba, ptm_lba, root_lba = 19, 20, 21, 22
    next_lba = 23
    extents = []
    for _, body, _ in files:
        extents.append(next_lba)
        next_lba += (len(body) + LBS - 1) // LBS
    total = next_lba

    root = b""
    root += dirent(b"\x00", root_lba, LBS, True)
    root += dirent(b"\x01", root_lba, LBS, True)
    for (fid, body, _), ext in zip(files, extents):
        root += dirent(fid, ext, len(body), False)
    if len(root) > LBS:
        fail("more root entries than one directory sector holds")
    root = root.ljust(LBS, b"\x00")

    # --- the two path tables: one record each, the root ----------------------
    ptl = bytes([1, 0]) + struct.pack("<I", root_lba) + \
        struct.pack("<H", 1) + b"\x00\x00"
    ptm = bytes([1, 0]) + struct.pack(">I", root_lba) + \
        struct.pack(">H", 1) + b"\x00\x00"
    pt_size = len(ptl)

    # --- the primary volume descriptor ---------------------------------------
    pvd = bytearray(LBS)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b" " * 32                        # system id
    pvd[40:72] = VOLUME_ID.ljust(32)             # volume id
    pvd[80:88] = both32(total)                   # volume space size
    pvd[120:124] = both16(1)                     # volume set size
    pvd[124:128] = both16(1)                     # volume sequence number
    pvd[128:132] = both16(LBS)                   # logical block size
    pvd[132:140] = both32(pt_size)               # path table size
    struct.pack_into("<I", pvd, 140, ptl_lba)    # L path table
    struct.pack_into(">I", pvd, 148, ptm_lba)    # M path table
    pvd[156:190] = dirent(b"\x00", root_lba, LBS, True)
    pvd[190:318] = b" " * 128                    # volume set id
    pvd[318:446] = b"OS8088".ljust(128)          # publisher
    pvd[446:574] = b"TOOLS/OS88ISO.PY".ljust(128)  # data preparer
    pvd[574:702] = b"OS8088".ljust(128)          # application
    pvd[702:739] = b" " * 37                     # copyright file id
    pvd[739:776] = b" " * 37                     # abstract file id
    pvd[776:813] = b" " * 37                     # bibliographic file id
    pvd[813:830] = vol_date()                    # created
    pvd[830:847] = vol_date()                    # modified
    pvd[847:864] = b"0" * 16 + b"\x00"           # expires: never
    pvd[864:881] = vol_date()                    # effective
    pvd[881] = 1                                 # file structure version

    # --- the El Torito boot record and catalog (SPEC.md 80.2) ----------------
    brvd = bytearray(LBS)
    brvd[0] = 0
    brvd[1:6] = b"CD001"
    brvd[6] = 1
    brvd[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\x00")
    struct.pack_into("<I", brvd, 0x47, cat_lba)

    term = bytearray(LBS)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1

    val = bytearray(32)
    val[0] = 1                                   # validation entry
    val[1] = 0                                   # platform: 80x86
    val[4:28] = b"os8088".ljust(24, b"\x00")
    val[30:32] = b"\x55\xaa"
    ck = -sum(struct.unpack("<16H", bytes(val))) & 0xFFFF
    struct.pack_into("<H", val, 28, ck)
    boot_lba = extents[[i for i, f in enumerate(files) if f[2]][0]]
    ent = bytearray(32)
    ent[0] = 0x88                                # bootable
    ent[1] = 0x04                                # media: HARD DISK emulation
    struct.pack_into("<H", ent, 2, 0)            # load segment: default 7C0h
    ent[4] = 0x04                                # system type = the partition
                                                 # type byte, as the spec asks
    struct.pack_into("<H", ent, 6, 1)            # load the MBR's one sector
    struct.pack_into("<I", ent, 8, boot_lba)
    cat = (bytes(val) + bytes(ent)).ljust(LBS, b"\x00")

    # --- write ---------------------------------------------------------------
    out = bytearray(total * LBS)

    def put(lba, blob):
        out[lba * LBS:lba * LBS + len(blob)] = blob

    put(pvd_lba, pvd)
    put(brvd_lba, brvd)
    put(term_lba, term)
    put(cat_lba, cat)
    put(ptl_lba, ptl)
    put(ptm_lba, ptm)
    put(root_lba, root)
    for (_, body, _), ext in zip(files, extents):
        put(ext, body)

    with open(a.output, "wb") as f:
        f.write(out)
    print("os88iso: %s  %d sectors (%.1fMB), %d file(s), boot image %s at "
          "LBA %d, hard-disk emulation"
          % (a.output, total, total * LBS / 1e6, len(files),
             BOOT_NAME, boot_lba))
    return 0


if __name__ == "__main__":
    sys.exit(main())

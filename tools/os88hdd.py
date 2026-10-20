#!/usr/bin/env python3
"""Build a BOOTABLE hard-disk image for testing SPEC.md 52.10's boot chain.

This is a TEST FIXTURE, not the installer. The installer is HDD.DRV's own
Install button (SPEC.md 52.10.4) and it does this work inside the running OS,
on the machine's real disk. What this exists for is that the boot chain -
MBR, volume boot record, kernel - has to be testable BEFORE the installer
exists and INDEPENDENTLY of it: if a disk built here boots and one built by
the installer does not, the fault is in the installer, and without this there
is no way to make that distinction.

It writes what SPEC.md 52.10.4 says the installer writes, in the same order
and for the same reasons:

  * the MBR: boot/mbr.asm's 446 bytes, one partition entry, status 80h
  * a FAT16 volume in that partition
  * the VBR: boot/boothd.asm with this volume's BPB over its first 62 bytes
    and the kernel's sector count patched into the word at offset 508
  * KERNEL.SYS FIRST and CONTIGUOUS from cluster 2, which the VBR requires
    because it reads the kernel as a flat run rather than walking a chain
  * ...and then whatever --file names, in the volume's root. A boot volume
    with no HDD.DRV on it is a machine that cannot LOAD the hard-disk driver
    (drv_load reads the system volume, which on an installed machine is the
    hard disk), so the whole of SPEC.md 52.4's mounting is out of reach
    without this - including 52.10.3's rule that the driver must not mount
    the boot partition a second time

MartyPC wants a fixed VHD, which is the raw sectors plus a 512-byte footer.
With --template this COPIES an existing one - MartyPC's bundled
default_xtide.vhd - and overwrites the data area, so the geometry in the footer
and the geometry the emulated controller reports cannot disagree. WITHOUT one
it writes the footer itself (vhd_new: the fixed-disk footer the template
carries, field for field, with this disk's sizes and CHS) over a zeroed data
area - which is what the video encoder's window does (SPEC.md 98.2.12.1), on
a machine that has never built MartyPC. A geometry
other than the template's (--spt/--heads/--cyls) gets the template's footer
with its sizes, CHS and checksum REWRITTEN to match, and a data area exactly
that long: an image cut for an MFM controller at 17 sectors a track with a
footer still saying 26 is a disk every reader of the footer lays out wrongly.
--raw drops the footer, for an emulator that takes a flat .img.

--noboot makes the same disk with NOTHING to boot: no kernel, and an MBR and a
volume boot record that are os88disk's "Not a bootable disk" stub over the
BPB, the partition not marked active. It is formatted exactly like the
bootable one, so HDD.DRV mounts it on a machine booted off its floppy
(SPEC.md 52.4) - which is what the video encoder's window makes when it is
run outside a built tree, where there is no kernel to put on a disk (SPEC.md
98.2.12.1).

--st11 lays the disk out the way a SEAGATE ST11 card (ST11M/ST11R) does, read
off drives its own low-level format prepared (86Box: an ST11R with an ST-238R
at 615/4/26, and an ST11M with an ST-225 at 615/4/17 - the same record and
the same layout, the geometry and the drive's name aside). The card keeps a 40-byte parameter record - magic DA BE, the
cylinders big-endian, heads, sectors, then fields copied verbatim - in
sectors 1 and 2 of heads 0 and 1 of cylinder 0, and hides that whole
cylinder: the BIOS's sector 0 is physical cylinder 1. It hands the BIOS two
cylinders fewer than the drive has, which is what os8088's own installer
partitioned (615 -> 63,726 sectors from LBA 26). Without the record the card
calls the drive unformatted; with the volume at physical LBA 0 every read is
one cylinder off.

  python3 tools/os88hdd.py --template build/martypc/run/media/hdds/default_xtide.vhd \\
                           --out /tmp/boot.vhd --kernel build/kernel.sys \\
                           --vbr build/boothd.bin --mbr build/mbr.bin
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from os88disk import CZ_HINT, CZ_H_MARK, CZ_H_HI, CZ_H_LO, BOOT_STUB  # noqa: E402

SECTOR = 512
BOOTHD_KSECS = 508          # boot/boothd.asm's pinned patch offset
HP_TBL = 446                # drivers/hdd/part.inc's HP_TBL
VOL_ID = 0x1A2B3C4D
# Attributes: SPEC.md 19.6. KERNEL.SYS is hidden+system, and read-only here
# because a fixture never installs over itself - the driver's installer
# deliberately leaves that bit clear so a re-install can replace the file
# (SPEC.md 19.6.1).
A_RDONLY, A_HIDDEN, A_SYS = 0x01, 0x02, 0x04


def fail(msg):
    sys.stderr.write("os88hdd: error: %s\n" % msg)
    sys.exit(1)


def name83(nm):
    """An 8.3 name as the 11 padded bytes a directory entry carries."""
    stem, _, ext = nm.upper().partition(".")
    if not stem or len(stem) > 8 or len(ext) > 3 or "." in ext:
        fail("%r is not an 8.3 name" % nm)
    return (stem.ljust(8) + ext.ljust(3)).encode("latin1")


def attrs_of(name):
    """SPEC.md 19.6's attributes, by extension - what the INSTALLER would
    give the same file, because a fixture that hides less than the installer
    does is a fixture testing a volume nobody will ever have."""
    ext = name[8:11]
    if ext == b"DRV":
        return A_RDONLY | A_HIDDEN | A_SYS
    if ext == b"CFG":
        return A_HIDDEN | A_SYS             # the kernel REWRITES this one
    return 0


def chs(lba, spt, heads):
    """LBA as the 3-byte CHS an MBR entry carries. Clamped to the maximum
    CHS can express, which is what every DOS-era tool does for a partition
    that reaches past cylinder 1023."""
    c, r = divmod(lba, spt * heads)
    h, s = divmod(r, spt)
    s += 1
    if c > 1023:
        c, h, s = 1023, heads - 1, spt
    return bytes([h & 0xFF, (s & 0x3F) | ((c >> 2) & 0xC0), c & 0xFF])


def pick_layout(tot):
    """A FAT16 layout for `tot` sectors: the smallest cluster that keeps the
    count inside FAT16's range, which is the shape drivers/hdd/fmt.inc's own
    capacity table produces."""
    for spc in (4, 8, 16, 32, 64):
        rsvd, nfats, root_ent = 1, 2, 512
        root_secs = (root_ent * 32 + SECTOR - 1) // SECTOR
        fatsz = 1
        for _ in range(64):                     # converges in two or three
            data = tot - rsvd - nfats * fatsz - root_secs
            nclus = data // spc
            need = ((nclus + 2) * 2 + SECTOR - 1) // SECTOR
            if need == fatsz:
                break
            fatsz = need
        if 4085 <= nclus < 65525:
            return spc, rsvd, nfats, root_ent, root_secs, fatsz, nclus
    fail("no FAT16 layout fits %d sectors" % tot)


def the_file(path):
    """The kernel's bytes - refusing the IMAGE where the FILE was meant.

    `kernel.bin` is what the kernel IS and `kernel.sys` is what goes on a
    volume (docs/plans/O88-COMPRESSION-PLAN.md); since `PKGZ ?= lz4` they are
    different bytes and different LENGTHS. The Makefile settles it in one
    place - every rule that puts a kernel on a disk names `$(KERNFILE)` - but
    a caller of this script has no such variable and has to type it, and
    tests/hibernate.py typed `kernel.bin` for a cycle. Nothing complained:
    the VHD took 208 sectors of unpacked image under a boot record built for
    167 of packed, and the machine reached a loading screen and sat there for
    the whole 360-second budget with no message at all.

    So the check is HERE, once, rather than in each caller. It is not a guess
    about the name: a packed tree has both files and they differ, so being
    handed the one that is not the sibling `kernel.sys` is decidable. With
    KZIP off the two are a copy of each other and this says nothing.
    """
    blob = open(path, "rb").read()
    sib = os.path.join(os.path.dirname(os.path.abspath(path)), "kernel.sys")
    if os.path.abspath(path) != sib and os.path.exists(sib):
        want = open(sib, "rb").read()
        if want != blob:
            fail("%s is the IMAGE, not the FILE: %s is %d bytes and %s is "
                 "%d. A boot record is built from the FILE (the Makefile's "
                 "$(KERNFILE)), so a volume carrying the other one boots to "
                 "a loading screen and stops. Pass %s."
                 % (path, path, len(blob), sib, len(want), sib))
    return blob


# The drive name each ST11 format wrote, by geometry: an ST-238R on an ST11R
# and an ST-225 on an ST11M, both read off disks the card itself formatted.
ST11_NAMES = {(615, 4, 26): b"SEAGATE30M", (615, 4, 17): b"SEAGATEST225"}


def st11_record(cyls, heads, spt):
    """The Seagate ST11's parameter record, as its low-level format wrote it
    (see the module comment). The geometry is ours and the name follows it
    (ST11_NAMES); the option bytes and the serial are copied - they read the
    same on the ST11R's disk and the ST11M's."""
    name = ST11_NAMES.get((cyls, heads, spt), b"SEAGATE")
    return (b"\xDA\xBE" + struct.pack(">HBB", cyls, heads, spt) +
            bytes.fromhex("000003060003ffff") +
            name.ljust(15) + b"\x0012345     ")


def vhd_new(seed):
    """A fixed-VHD footer of our own: the template's fields - cookie, the
    fixed-disk features and version, no dynamic header, creator "os88" on
    "Wi2k", type 2 - with a zero timestamp and a UUID from SEED, so the
    toolchain stays deterministic. vhd_footer() then sizes it"""
    import hashlib
    f = bytearray(SECTOR)
    f[0:8] = b"conectix"
    struct.pack_into(">II", f, 8, 2, 0x00010000)
    struct.pack_into(">Q", f, 16, 0xFFFFFFFFFFFFFFFF)
    f[28:32] = b"os88"
    struct.pack_into(">I", f, 32, 0x00010000)
    f[36:40] = b"Wi2k"
    struct.pack_into(">I", f, 60, 2)            # fixed
    f[68:84] = hashlib.md5(seed).digest()
    return f


def vhd_footer(footer, total, cyls, heads, spt):
    """The template's fixed-VHD footer with its sizes, CHS and checksum made
    to describe TOTAL sectors at CYLS/HEADS/SPT. Byte-identical to the
    template's own when the geometry is the template's."""
    f = bytearray(footer)
    size = total * SECTOR
    struct.pack_into(">Q", f, 40, size)         # original size
    struct.pack_into(">Q", f, 48, size)         # current size
    struct.pack_into(">HBB", f, 56, cyls, heads, spt)
    struct.pack_into(">I", f, 64, 0)
    struct.pack_into(">I", f, 64, ~sum(f) & 0xFFFFFFFF)
    return bytes(f)


def st11_vhd(a, disk, footer, pcyls):
    """The last of the build, shared with --wrap: the card's layout when
    --st11 (its hidden cylinder and parameter record), and the footer"""
    ptotal = a.spt * a.heads * a.cyls
    if a.st11:
        cyl = a.spt * a.heads * SECTOR
        ptotal = pcyls * a.heads * a.spt
        phys = bytearray(ptotal * SECTOR)
        phys[cyl:cyl + len(disk)] = disk
        rec = st11_record(pcyls, a.heads, a.spt)
        for lba in (0, 1, a.spt, a.spt + 1):    # head 0 and head 1, S1 and S2
            phys[lba * SECTOR:lba * SECTOR + len(rec)] = rec
        disk = phys
    if not a.raw:
        disk += vhd_footer(footer, ptotal, pcyls, a.heads, a.spt)
    return disk


def wrap(a):
    """--wrap: a disk os88disk.py built - folders and all - under the card's
    layout and a footer. Its size must be the BIOS's view of this geometry
    exactly, which is what makes the card's hidden cylinder land right"""
    pcyls = a.cyls
    if a.st11:
        a.cyls -= 2
    disk = bytearray(open(a.wrap, "rb").read())
    want = a.spt * a.heads * a.cyls * SECTOR
    if len(disk) != want:
        fail("%s is %d bytes; %d/%d/%d is %d - build it with os88disk.py "
             "--hdd --geometry %d/%d/%d" % (a.wrap, len(disk), a.cyls,
                                             a.heads, a.spt, want, a.cyls,
                                             a.heads, a.spt))
    if disk[510:512] != b"\x55\xAA":
        fail("%s has no MBR" % a.wrap)
    footer = vhd_new(("%d/%d/%d%s" % (pcyls, a.heads, a.spt, " st11" if
                                      a.st11 else "")).encode())
    open(a.out, "wb").write(bytes(st11_vhd(a, disk, footer, pcyls)))
    print("os88hdd: %s  %s wrapped, %d/%d/%d%s%s" % (
        a.out, a.wrap, pcyls, a.heads, a.spt, ", ST11" if a.st11 else "",
        "" if a.raw else ", VHD"))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--template", help="a VHD to take the footer (and the "
                    "data area's first bytes) from; none: a footer of our own "
                    "over zeroes")
    ap.add_argument("--out", required=True)
    ap.add_argument("--kernel")
    ap.add_argument("--vbr")
    ap.add_argument("--mbr")
    ap.add_argument("--noboot", action="store_true",
                    help="formatted and NOT bootable: no kernel, and the "
                    "not-bootable stub in the MBR and the VBR (no --kernel, "
                    "--vbr or --mbr)")
    ap.add_argument("--spt", type=int, default=26)
    ap.add_argument("--heads", type=int, default=4)
    ap.add_argument("--cyls", type=int, default=615)
    ap.add_argument("--raw", action="store_true",
                    help="a flat image of exactly the geometry, no VHD footer")
    ap.add_argument("--st11", action="store_true",
                    help="a Seagate ST11 card's layout: its parameter record "
                    "in cylinder 0, the volume from cylinder 1, two "
                    "cylinders fewer for the BIOS")
    ap.add_argument("--wrap", metavar="IMG",
                    help="no volume of our own: IMG is a whole partitioned "
                    "disk as the BIOS sees it (os88disk.py --hdd --geometry, "
                    "which lays out FOLDERS) - put it under the card's "
                    "layout (--st11) and a VHD footer, and nothing else")
    ap.add_argument("--file", action="append", default=[], metavar="NAME=PATH",
                    help="another file for the volume's root, e.g. "
                         "HDD.DRV=build/hdd.drv (repeatable)")
    a = ap.parse_args()
    if a.wrap:
        if a.noboot or a.kernel or a.vbr or a.mbr or a.file or a.template:
            fail("--wrap takes only the geometry, --st11, --raw and --out")
        return wrap(a)
    if a.noboot:
        if a.kernel or a.vbr or a.mbr:
            fail("--noboot takes no --kernel, --vbr or --mbr")
    elif not (a.kernel and a.vbr and a.mbr):
        fail("a bootable disk needs --kernel, --vbr and --mbr (or --noboot)")

    extras = []
    for spec in a.file:
        nm, sep, path = spec.partition("=")
        if not sep:
            fail("--file wants NAME=PATH, not %r" % spec)
        extras.append((name83(nm), open(path, "rb").read()))

    pcyls = a.cyls                              # the drive's own
    if a.st11:
        a.cyls -= 2                             # ...and what the BIOS gets
    total = a.spt * a.heads * a.cyls
    if a.template:
        tmpl = open(a.template, "rb").read()
        if len(tmpl) < total * SECTOR:
            fail("template is %d bytes, smaller than the geometry's %d"
                 % (len(tmpl), total * SECTOR))
        footer = bytearray(tmpl[-SECTOR:])
        if footer[:8] != b"conectix":
            fail("%s has no VHD footer" % a.template)
        disk = bytearray(tmpl[:total * SECTOR])  # the data area, cut to the
                                                # geometry: the footer is
                                                # rewritten below to match
    else:
        footer = vhd_new(("%d/%d/%d%s" % (pcyls, a.heads, a.spt, " st11" if
                                          a.st11 else "")).encode())
        disk = bytearray(total * SECTOR)

    if a.noboot:
        # The stub is org-dependent - its message is at 7C59h - and both
        # sectors are loaded at 0:7C00, so it sits at 62 in each behind a
        # jmp short over the BPB (the MBR's BPB bytes are zero)
        stub = bytearray(SECTOR)
        stub[0:3] = b"\xEB\x3C\x90"
        stub[62:62 + len(BOOT_STUB)] = BOOT_STUB
        mbr = bytes(stub[:HP_TBL])
        vbr = bytearray(stub)
        vbr[3:11] = b"MSDOS5.0"
        vbr[510:512] = b"\x55\xAA"
        kernel, ksecs = None, 0
    else:
        mbr = open(a.mbr, "rb").read()
        if len(mbr) != HP_TBL:
            fail("%s is %d bytes, not %d" % (a.mbr, len(mbr), HP_TBL))
        vbr = bytearray(open(a.vbr, "rb").read())
        if len(vbr) != SECTOR:
            fail("%s is %d bytes, not %d" % (a.vbr, len(vbr), SECTOR))
        kernel = the_file(a.kernel)
        ksecs = (len(kernel) + SECTOR - 1) // SECTOR

    # --- the partition: from LBA = spt (the end of the MBR's own track, the
    # era convention drivers/hdd/part.inc follows) to the end of the disk.
    base = a.spt
    psecs = total - base
    if psecs > 65535:
        psecs = (65535 // a.spt) * a.spt        # the kernel's volume ceiling
    spc, rsvd, nfats, root_ent, root_secs, fatsz, nclus = pick_layout(psecs)
    fat_lba = rsvd
    root_lba = rsvd + nfats * fatsz
    data_lba = root_lba + root_secs

    # --- the volume boot record: our own BPB over boothd.asm's first 62 -----
    struct.pack_into("<H", vbr, 11, SECTOR)
    vbr[13] = spc
    struct.pack_into("<H", vbr, 14, rsvd)
    vbr[16] = nfats
    struct.pack_into("<H", vbr, 17, root_ent)
    struct.pack_into("<H", vbr, 19, psecs if psecs < 0x10000 else 0)
    vbr[21] = 0xF8                              # fixed disk
    struct.pack_into("<H", vbr, 22, fatsz)
    struct.pack_into("<H", vbr, 24, a.spt)
    struct.pack_into("<H", vbr, 26, a.heads)
    struct.pack_into("<I", vbr, 28, base)       # BPB_HiddSec - the partition
                                                # base, and what the VBR adds
                                                # to every LBA (SPEC.md 52.10.2)
    vbr[36] = 0x80                              # BS_DrvNum
    vbr[38] = 0x29
    struct.pack_into("<I", vbr, 39, VOL_ID)
    vbr[43:54] = b"OS8088     "
    vbr[54:62] = b"FAT16   "
    if kernel:
        struct.pack_into("<H", vbr, BOOTHD_KSECS, ksecs)  # the ONE patch site
    if vbr[510:512] != b"\x55\xAA":
        fail("the VBR lost its signature")

    # --- the FAT and the root, with KERNEL.SYS's chain first ---------------
    # The kernel is cluster 2 onward and CONTIGUOUS, which the VBR requires;
    # every --file lands after it, in the order it was named.
    fat = bytearray(fatsz * SECTOR)
    struct.pack_into("<H", fat, 0, 0xFFF8)
    struct.pack_into("<H", fat, 2, 0xFFFF)
    root = bytearray(root_secs * SECTOR)
    laid = []                                   # (first cluster, bytes)
    nextc, nent = 2, 0

    def add(name, blob, attr):
        nonlocal nextc, nent
        if any(root[i * 32:i * 32 + 11] == name for i in range(nent)):
            # (the video encoder's window once named KERNEL.SYS as a --file
            # as well as --kernel, and the disk had two: nothing here or in
            # os88disk --verify-hdd said so)
            fail("%s is on the volume twice" % name.decode("latin1"))
        n = max(1, (len(blob) + spc * SECTOR - 1) // (spc * SECTOR))
        if nextc + n > nclus + 2:
            fail("%s does not fit the volume" % name)
        if nent * 32 >= len(root):
            fail("the root directory is full at %s" % name)
        for i in range(n):
            c = nextc + i
            struct.pack_into("<H", fat, c * 2, 0xFFFF if i == n - 1 else c + 1)
        e = bytearray(32)
        e[0:11] = name
        e[11] = attr
        struct.pack_into("<H", e, 22, 0)            # time
        struct.pack_into("<H", e, 24, ((2026 - 1980) << 9) | (1 << 5) | 1)
        struct.pack_into("<H", e, 26, nextc)
        struct.pack_into("<I", e, 28, len(blob))
        # THE DIRECTORY HINT, os88disk.dirent's rule (SPEC.md 20.14.1): a
        # 'CZ' file carries its unpacked size in the entry's spare bytes, and
        # drv_find / mod_need size their claims from it. Without it a packed
        # driver or module on this volume reads as PLAIN and its loader
        # refuses the 'CZ' magic - which is what the hibernate row saw the
        # day the drivers became containers and this tool did not follow.
        # **`un` AND NOT `n`.** This used to spell the unpacked size `n`,
        # which is the CLUSTER COUNT three lines down - so `nextc` advanced by
        # a BYTE COUNT after every 'CZ' file and the next one landed thousands
        # of clusters along. Nothing caught it because the volume stayed
        # SELF-CONSISTENT: the FAT chain, the directory entry and the data all
        # used the same wrong number, so a four-file fixture was merely very
        # sparse and booted perfectly. It surfaced as `DOS.O88 does not fit the
        # volume` on a 32MB disk holding 173 KB.
        if len(blob) >= 8 and blob[:2] == b"CZ" and blob[3] == 0 \
                and blob[2] in (0, 1):
            un = int.from_bytes(blob[4:8], "little")
            if un >= (1 << 24):
                fail("%s: expands to %d bytes and the hint carries 24 bits"
                     % (name, un))
            e[CZ_H_MARK] = CZ_HINT + blob[2]
            e[CZ_H_HI] = un >> 16
            struct.pack_into("<H", e, CZ_H_LO, un & 0xFFFF)
        root[nent * 32:nent * 32 + 32] = e
        laid.append((nextc, blob))
        nextc += n
        nent += 1

    if kernel:
        add(b"KERNEL  SYS", kernel, A_RDONLY | A_HIDDEN | A_SYS)
    for name, blob in extras:
        add(name, blob, attrs_of(name))

    # --- lay the volume down -----------------------------------------------
    def put(lba, blob):
        off = (base + lba) * SECTOR
        disk[off:off + len(blob)] = blob

    put(0, bytes(vbr))
    for i in range(nfats):
        put(fat_lba + i * fatsz, bytes(fat))
    put(root_lba, bytes(root))
    for c, blob in laid:
        put(data_lba + (c - 2) * spc, blob + b"\x00" * (-len(blob) % SECTOR))

    # --- and the MBR, LAST: it is the commit (SPEC.md 52.10.4) -------------
    sec0 = bytearray(SECTOR)
    sec0[0:HP_TBL] = mbr
    ent = bytearray(16)
    ent[0] = 0x00 if a.noboot else 0x80         # active: only if it boots
    ent[1:4] = chs(base, a.spt, a.heads)
    ent[4] = 0x06 if psecs >= 65536 or psecs * SECTOR >= 32 << 20 else 0x04
    ent[5:8] = chs(base + psecs - 1, a.spt, a.heads)
    struct.pack_into("<I", ent, 8, base)
    struct.pack_into("<I", ent, 12, psecs)
    sec0[HP_TBL:HP_TBL + 16] = ent
    sec0[510:512] = b"\x55\xAA"
    disk[0:SECTOR] = sec0

    disk = st11_vhd(a, disk, footer, pcyls)
    open(a.out, "wb").write(bytes(disk))
    print("os88hdd: %s  %d/%d/%d, partition at LBA %d for %d sectors (%dMB), "
          "%s, %d-sector clusters, %s"
          % (a.out, a.cyls, a.heads, a.spt, base, psecs,
             psecs * SECTOR // (1 << 20), "FAT16", spc,
             "KERNEL.SYS %d sectors at LBA %d" % (ksecs, base + data_lba)
             if kernel else "NOT BOOTABLE"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

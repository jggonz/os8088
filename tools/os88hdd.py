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
Rather than generate a footer, this COPIES an existing one - MartyPC's bundled
default_xtide.vhd - and overwrites the data area, so the geometry in the footer
and the geometry the emulated controller reports cannot disagree.

  python3 tools/os88hdd.py --template build/martypc/run/media/hdds/default_xtide.vhd \\
                           --out /tmp/boot.vhd --kernel build/kernel.bin \\
                           --vbr build/boothd.bin --mbr build/mbr.bin
"""
import argparse
import struct
import sys

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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--template", required=True, help="a VHD to take the footer and geometry from")
    ap.add_argument("--out", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--vbr", required=True)
    ap.add_argument("--mbr", required=True)
    ap.add_argument("--spt", type=int, default=26)
    ap.add_argument("--heads", type=int, default=4)
    ap.add_argument("--cyls", type=int, default=615)
    ap.add_argument("--file", action="append", default=[], metavar="NAME=PATH",
                    help="another file for the volume's root, e.g. "
                         "HDD.DRV=build/hdd.drv (repeatable)")
    a = ap.parse_args()

    extras = []
    for spec in a.file:
        nm, sep, path = spec.partition("=")
        if not sep:
            fail("--file wants NAME=PATH, not %r" % spec)
        extras.append((name83(nm), open(path, "rb").read()))

    tmpl = open(a.template, "rb").read()
    total = a.spt * a.heads * a.cyls
    if len(tmpl) < total * SECTOR:
        fail("template is %d bytes, smaller than the geometry's %d"
             % (len(tmpl), total * SECTOR))
    disk = bytearray(tmpl)                      # footer and all: we only
                                                # overwrite the data area

    mbr = open(a.mbr, "rb").read()
    if len(mbr) != HP_TBL:
        fail("%s is %d bytes, not %d" % (a.mbr, len(mbr), HP_TBL))
    vbr = bytearray(open(a.vbr, "rb").read())
    if len(vbr) != SECTOR:
        fail("%s is %d bytes, not %d" % (a.vbr, len(vbr), SECTOR))
    kernel = open(a.kernel, "rb").read()
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
    struct.pack_into("<H", vbr, BOOTHD_KSECS, ksecs)    # the ONE patch site
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
        root[nent * 32:nent * 32 + 32] = e
        laid.append((nextc, blob))
        nextc += n
        nent += 1

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
    ent[0] = 0x80                               # active
    ent[1:4] = chs(base, a.spt, a.heads)
    ent[4] = 0x06 if psecs >= 65536 or psecs * SECTOR >= 32 << 20 else 0x04
    ent[5:8] = chs(base + psecs - 1, a.spt, a.heads)
    struct.pack_into("<I", ent, 8, base)
    struct.pack_into("<I", ent, 12, psecs)
    sec0[HP_TBL:HP_TBL + 16] = ent
    sec0[510:512] = b"\x55\xAA"
    disk[0:SECTOR] = sec0

    open(a.out, "wb").write(bytes(disk))
    print("os88hdd: %s  %d/%d/%d, partition at LBA %d for %d sectors (%dMB), "
          "%s, %d-sector clusters, KERNEL.SYS %d sectors at LBA %d"
          % (a.out, a.cyls, a.heads, a.spt, base, psecs,
             psecs * SECTOR // (1 << 20), "FAT16", spc, ksecs, base + data_lba))
    return 0


if __name__ == "__main__":
    sys.exit(main())

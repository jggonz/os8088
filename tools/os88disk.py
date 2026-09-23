#!/usr/bin/env python3
"""os88disk: build (or verify) a FAT data floppy image from .o88 packages.

    python3 tools/os88disk.py -o OUT.img --size {1440,1200,720,360}
                             [--folder PATH ...] [[DIR[/DIR...]:]PKG.o88 ...]
    python3 tools/os88disk.py -o OUT.img --hdd --mbr MBR.bin
                             --boot BOOTHD.bin --kernel KERNEL.bin [...]
    python3 tools/os88disk.py --verify IMG
    python3 tools/os88disk.py --verify-hdd HDD.img
    python3 tools/os88disk.py --retarget HDD.img --geometry [C/]H/S -o OUT.img

The image is a canonical DOS FAT floppy (SPEC.md section 19): boot sector
with a full BPB (OEM "MSDOS5.0", boot signature 0x29, fixed serial
0x88000888, label "OS8088APPS "), two FAT copies, a root directory whose
first entry is the volume label (attr 0x08 -- the kernel filters it, so
package indices start at 0), then one attr-0x20 entry per package in
argument order with its exact byte size (never padded -- the loader's
truncation guard depends on it) and fixed timestamps (date 0x5C21 =
2026-01-01, time 0). File data is allocated contiguously from cluster 2 in
argument order; every field is fixed, so rebuilds are byte-identical.
Directory display names are the host filenames (8.3, uppercased).

Geometries: 1440 (1.44MB, FAT12), 1200 (1.2MB 5.25" HD, FAT12), 720 (720KB
3.5" DD, FAT12) and 360 (360KB, FAT12) are the shipped disks, and now the
only ones. 720 and 360 are the SAME 9 spt / 2 heads track shape and differ
only in cylinder count (80 vs 40), which is why they share a boot sector:
boot/boot.asm knows a geometry as -DSPT/-DHEADS and nothing else, and the
BPB that does differ is written here. 1200 is 15 spt on those same 80
cylinders, so it needs a boot sector of its own (-DSPT=15) and gets one -
build/boot120.bin - but nothing else about it is new: 2,371 clusters of 512
bytes, a 7-sector FAT inside DSK_FAT_SECS, and spt 15 is already in the
kernel's mount-rule 11 whitelist. A 2880 (2.88MB ED, 5,698 clusters => FAT16)
geometry lived here so the kernel's FAT16 path had a positive test; it went
when DSK_FAT_SECS fell to 10 sectors, which is below the 16 a FAT has to
have to be FAT16 at all, so mount rule 10 (SPEC.md 18.2) now turns every
FAT16 volume away and there is nothing left to test with.

--hdd (SPEC.md 80.1) builds a bootable HARD-DISK image instead of a floppy:
boot/mbr.asm's 446 bytes and one active partition entry at LBA 0,
boot/boothd.asm as the volume boot record with this volume's BPB over its
first 62 bytes and the kernel sector count in the pinned word at offset 508
(SPEC.md 52.10.2), and a FAT16 volume laid out by the same code as every
floppy - KERNEL.SYS first and contiguous, the folder tree, the attribute
rules and the warm ASSOC.DAT are all the one implementation. It is what
SPEC.md 52.10.4's installer writes, made at build time: the live-USB image,
and the boot image inside the live CD (SPEC.md 80.2). The geometry is fixed
(16 heads x 63 spt x 65 cylinders, partition base 63) and the partition
entry's CHS and LBA columns describe the same sectors under it, because a
BIOS booting a USB stick derives its virtual geometry from that table.
--verify-hdd checks the result exactly as it checks an installer's disk.

Boot-sector stub (offset 62): fixed hand-assembled bytes that print
"Not a bootable disk. Press any key." via int 10h AH=0Eh teletype, wait
with int 16h AH=00h, and reboot via int 19h. It uses org-correct absolute
addressing for 0x7C00: DS is set to 0 explicitly and the message pointer
is the absolute offset 0x7C59, so it works whether the BIOS enters at
0000:7C00 or 07C0:0000 (the only jumps are short/relative).

--verify is a standalone structural fsck (usable on foreign-written
disks): BPB validation per SPEC.md's rule table, FAT type detection from
the cluster count, FAT1==FAT2, a chain walk for every root entry with a
length-vs-size check, cross-link and lost-cluster detection.

--scramble (test-only) allocates the cluster chains round-robin
interleaved across files: a legally fragmented image for chain-walk tests.

Zero packages is legal (an empty disk). Fails with exit 1 + stderr on more
than 32 packages (the kernel listing cap), data over capacity, duplicate
or invalid 8.3 names, or an invalid .o88. --deep-folders lifts the 32-entry
cap for SUBFOLDERS only: it is a listing cap (the Disk window shows the
first 31 of such a folder), and the file API walks every entry - a CP/M
drive folder on the RUNCPM disk holds 78 (SPEC.md 71.3).
"""
import argparse
import os
import hashlib
import struct
import sys

SECTOR = 512
def _listing_cap():
    """DSK_NENT, READ OUT OF THE KERNEL rather than restated here.

    It was `MAX_FILES = 32` with a comment pointing at SPEC.md section 19, and
    that is a MIRROR of a kernel constant in a host tool - the class of bug
    `tests/unit/t_mirror.py` exists for.  When DSK_NENT doubled, this file went
    on refusing a 60-file disk the kernel would have listed perfectly well, and
    the message it refused with named a number that was no longer true.

    Parsed rather than imported, because this tool must keep working on a tree
    with no assembler: the fallback is the value the kernel shipped with, which
    is wrong in the SAFE direction (it refuses a disk that would have worked
    rather than building one the kernel cannot list).

    **AND IT IS PER-KERNEL SINCE SPEC.md 22.6.2** - 64 on kern_big, 32 on
    kern_small, which has no DOS box to list a DOS directory for.  So the
    file holds two `DSK_NENT equ` lines behind a `%ifndef KERN_SMALL` and
    this returns BOTH, kern_big's first, because that is the default every
    caller wants and `--kern-small` is what selects the other.  Returning
    the smaller for every disk would refuse a 40-file kern_big folder the
    kernel lists perfectly well, which is the exact bug the paragraph above
    is about.
    """
    import re as _re
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "kernel", "dskwin.inc")
    try:
        with open(src) as f:
            m = _re.findall(r"^DSK_NENT\s+equ\s+(\d+)", f.read(), _re.M)
        if m:
            return int(m[0]), int(m[-1])
    except OSError:
        pass
    return 32, 32


MAX_FILES, SMALL_FILES = _listing_cap()   # kernel listing cap, big and small
                                          # (SPEC.md section 19, 25.8.1)
VOL_LABEL = b"OS8088APPS "    # 11 bytes, BS_VolLab == root label entry
SYS_LABEL = b"OS8088SYS  "    # ...and what a --boot/--kernel disk is called
VOL_ID = 0x88000888           # the FALLBACK serial, and the value every
                              # os8088 volume used to carry. See vol_id().
FIXED_DATE = 0x5C21           # 2026-01-01 in FAT date encoding
FIXED_TIME = 0x0000
NAME_CHARS = frozenset(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")

# size -> (spt, heads, total sectors, sec/clus, FAT sectors, root entries,
#          media byte)   -- mirrors SPEC.md section 19's geometry table
def fatcap(size, cap):
    """(spc, fatsz) for `size` with a FAT of at most `cap` sectors.

    SPEC.md 51.0's kern_small caps DSK_FAT_SECS at 2 - it mounts nothing whose
    FAT is bigger - and the naive reading of that is "so it only gets 360KB
    disks". It does not have to: a FAT is sized by the CLUSTER COUNT, not by
    the disk, so raising sectors-per-cluster brings any geometry under the cap.
    A 1.44MB floppy with 4KB clusters declares a 2-sector FAT and is still
    1.44MB, still FAT12, and still a volume any host OS mounts.

    What it costs is cluster slack - a 100-byte file occupies 4KB on that disk
    - which on a floppy carrying twenty packages of 5-48KB is a few percent,
    and buys the geometry back whole.

    Powers of two only (mount rule 4), and the search is upward from the
    standard value so a size that already fits is left EXACTLY as DOS would
    have formatted it.
    """
    spt, heads, tot, spc, fatsz, root_ent, media = GEOMETRY[size]
    while spc <= 64:
        lay = Layout(spc, 1, 2, root_ent, tot, fatsz)
        need = ((lay.nclus + 2) * 3 + 1) // 2       # FAT12: 1.5 bytes an entry
        need = (need + SECTOR - 1) // SECTOR
        if need <= cap:
            # re-derive with the FAT it actually needs, then check it still
            # fits: shrinking the FAT moves data_lba down and can only ADD
            # clusters, so one more pass settles it.
            lay = Layout(spc, 1, 2, root_ent, tot, max(need, 1))
            n2 = (((lay.nclus + 2) * 3 + 1) // 2 + SECTOR - 1) // SECTOR
            if n2 <= cap:
                return spc, max(n2, 1)
        spc *= 2
    fail(f"no cluster size brings a {size}KB volume under a {cap}-sector FAT")


GEOMETRY = {
    1440: (18, 2, 2880, 1, 9, 224, 0xF0),
    1200: (15, 2, 2400, 1, 7, 224, 0xF9),
    720: (9, 2, 1440, 2, 3, 112, 0xF9),
    360: (9, 2, 720, 2, 2, 112, 0xFD),
}

# --hdd (SPEC.md 80.1): ONE fixed hard-disk shape, not a knob. 65 cylinders
# puts the partition at 65,457 sectors - just under the kernel's
# 65,535-sector volume ceiling (SPEC.md 52.10.3) and under 32MB, so the
# partition type is 04h. The base is one track, the era convention
# drivers/hdd/part.inc follows. 16x63 is for the consumer the floppies never
# had: a BIOS booting a USB stick or an El Torito hard disk derives its
# virtual geometry from the partition table's own CHS fields, so the entry
# is written CHS/LBA-consistent under exactly this shape.
HDD_SPT, HDD_HEADS, HDD_CYLS = 63, 16, 65
HDD_BASE = HDD_SPT                       # the MBR's own track
HDD_TOT = HDD_SPT * HDD_HEADS * HDD_CYLS # 65,520 sectors, the whole image
HDD_PSECS = HDD_TOT - HDD_BASE           # 65,457 - the partition
HDD_ROOT_ENT = 512                       # what drivers/hdd/fmt.inc formats
HDD_LABEL = b"OS8088LIVE "
BOOTHD_KSECS = 508                       # boot/boothd.asm's pinned patch word
HP_TBL = 446                             # the partition table's offset

# Hand-assembled A.4 stub (verified against nasm; see module docstring):
#   xor ax,ax / mov ds,ax / mov si,0x7C59
#   .print: lodsb / or al,al / jz .wait / mov ah,0Eh / mov bx,7 /
#           int 10h / jmp .print
#   .wait:  xor ah,ah / int 16h / int 19h
#   msg:    db 13,10,"Not a bootable disk. Press any key.",13,10,0
BOOT_STUB = bytes.fromhex(
    "31c08ed8be597cac08c07409b40ebb0700cd10ebf230e4cd16cd19"
    "0d0a4e6f74206120626f6f7461626c65206469736b2e2050726573"
    "7320616e79206b65792e0d0a00")


def fail(msg: str) -> None:
    print(f"os88disk: error: {msg}", file=sys.stderr)
    sys.exit(1)


def validate_o88(path: str) -> bytes:
    """Read and validate one .o88 file; return its bytes."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        fail(f"cannot read {path}: {e}")
    if len(data) < 32:
        fail(f"{path}: too short for an os8088 header ({len(data)} bytes)")
    magic, = struct.unpack_from("<H", data, 0)
    if magic != 0x384F:
        fail(f"{path}: bad magic 0x{magic:04X} (not a .o88 package)")
    if data[2] != 3:
        fail(f"{path}: format version {data[2]}; this is the v3 toolchain "
             "(rebuild the package)")
    parts = bool(data[3] & 4)          # flags bit 2 (SPEC.md 20.12)
    if len(data) > 0xFFFF and not parts:
        fail(f"{path}: {len(data)} bytes overflows the 16-bit size field")
    comp = bool(data[3] & 8)           # flags bit 3 (SPEC.md 20.13)
    image = struct.unpack_from("<H", data, 8)[0]
    lo = 32 if comp else max(32, len(data))
    if not 32 <= image <= (0xFFFF if comp else len(data)):
        fail(f"{path}: header image size {image} out of range")
    if comp:
        # COMPRESSED: `image` keeps meaning the UNPACKED bytes, so the file is
        # SHORTER than it by design (SPEC.md 20.13.2) - which is the very case
        # the truncated-file guard below refuses without the bit.
        if image <= len(data):
            fail(f"{path}: flags bit 3 is set but the image ({image}) is not "
                 f"bigger than the file ({len(data)}) - a compressed package "
                 "that saved nothing should have been shipped uncompressed")
    elif image != len(data) and not parts:
        fail(f"{path}: header image size {image} != file size {len(data)}; "
             "a v3 package has no relocation table (run os88pkg.py first)")
    # WITH flags bit 2 the file is longer than the image ON PURPOSE and the
    # tail is the package's own (SPEC.md 20.12) - so image <= file is the
    # whole rule, the 16-bit ceiling belongs to the IMAGE and not to the file,
    # and the mount types such a file up to PKG_FILE_HI (SPEC.md 19.1).
    if parts and len(data) >= (1 << 20):
        fail(f"{path}: {len(data)} bytes; PKG_FILE_HI caps a package file at "
             "1MB (SPEC.md 19.1)")
    hname = data[16:32].split(b"\0", 1)[0]
    if not hname:
        fail(f"{path}: empty name field in header")
    if any(not 0x20 <= b <= 0x7E for b in hname):
        fail(f"{path}: non-printable bytes in header name field")
    if data[3] & 1 and len(data) < 96:     # embedded icon (SPEC.md 20.2)
        fail(f"{path}: flags bit 0 set but no icon block "
             "(run os88pkg.py first)")
    return data


RESERVED_STEMS = ({"CON", "PRN", "AUX", "NUL"}
                  | {f"COM{i}" for i in range(1, 10)}
                  | {f"LPT{i}" for i in range(1, 10)})


def name83(path: str, seen: set) -> bytes:
    """Derive the 11-byte FAT short name from the host filename."""
    base = os.path.basename(path).upper()
    stem, _, ext = base.partition(".")
    if not 1 <= len(stem) <= 8 or len(ext) > 3:
        fail(f"{path}: '{base}' is not a valid 8.3 name "
             "(stem 1-8 chars, extension 0-3)")
    # Any 8.3 extension is legal now that a disk may carry DATA next to its
    # programs (SPEC.md 24). The kernel still only marks *.O88 entries
    # loadable, so a data file simply lists with the generic icon and does
    # nothing on a double-click - which is the behaviour a non-package has
    # always had for any file a host OS put there.
    if stem in RESERVED_STEMS:
        fail(f"{path}: '{stem}' is a reserved DOS device name")
    for ch in (stem + ext).encode("ascii", "replace"):
        if ch not in NAME_CHARS:
            fail(f"{path}: '{base}' has characters outside A-Z 0-9 _ -")
    name = stem.encode().ljust(8) + ext.encode().ljust(3)
    if name in seen:
        fail(f"{path}: duplicate 8.3 name '{base}'")
    seen.add(name)
    return name


class Layout:
    """Derived FAT layout, shared by the builder and the verifier."""

    def __init__(self, spc, rsvd, nfats, root_ent, tot, fatsz):
        self.spc, self.rsvd, self.nfats = spc, rsvd, nfats
        self.root_ent, self.tot, self.fatsz = root_ent, tot, fatsz
        self.root_secs = (root_ent * 32 + SECTOR - 1) // SECTOR
        self.fat_lba = rsvd
        self.root_lba = rsvd + nfats * fatsz
        self.data_lba = self.root_lba + self.root_secs
        self.nclus = (tot - self.data_lba) // spc      # CountOfClusters
        self.maxclus = self.nclus + 1                  # highest valid number
        self.fat12 = self.nclus < 4085
        self.cluster_bytes = spc * SECTOR

    @property
    def type_name(self):
        return "FAT12" if self.fat12 else "FAT16"


class Fat:
    """One FAT copy: 12- or 16-bit entries in fatsz*512 bytes."""

    def __init__(self, lay: Layout, media: int):
        self.buf = bytearray(lay.fatsz * SECTOR)
        self.fat12 = lay.fat12
        self.set(0, (0xF00 if self.fat12 else 0xFF00) | media)
        self.set(1, 0xFFF if self.fat12 else 0xFFFF)

    def set(self, n: int, v: int) -> None:
        if self.fat12:
            off = n * 3 // 2
            if n & 1:
                self.buf[off] = (self.buf[off] & 0x0F) | ((v << 4) & 0xF0)
                self.buf[off + 1] = (v >> 4) & 0xFF
            else:
                self.buf[off] = v & 0xFF
                self.buf[off + 1] = (self.buf[off + 1] & 0xF0) | \
                    ((v >> 8) & 0x0F)
        else:
            struct.pack_into("<H", self.buf, n * 2, v)


def fat_get(buf: bytes, fat12: bool, n: int) -> int:
    if fat12:
        off = n * 3 // 2
        w = buf[off] | (buf[off + 1] << 8)
        return (w >> 4) & 0xFFF if n & 1 else w & 0xFFF
    return struct.unpack_from("<H", buf, n * 2)[0]


KERNEL_NAME = b"KERNEL  SYS"   # the kernel, as a file (SPEC.md 19.3)

# Directory attributes. A system disk hides the things that are the KERNEL's
# business and nobody else's, from DOS and from os8088's own file manager
# alike (SPEC.md 19.6) - one mechanism, because both filter the same two bits:
# DOS's DIR hides hidden|system by default, and disk_mount drops those entries
# from the listing (SPEC.md 19).
A_RDONLY = 0x01
A_HIDDEN = 0x02
A_SYS    = 0x04
A_ARCH   = 0x20
A_SYSTEM = A_RDONLY | A_HIDDEN | A_SYS      # KERNEL.SYS and every *.DRV
A_LOCKED = A_RDONLY | A_ARCH                # visible, but not yours to delete


ASC_NAME  = b"ASSOC   DAT"   # SPEC.md 54.7: the volume's icon + assoc cache
ASC_MAGIC = b"OS88AC"
ASC_VER   = 2                # rows carry the glyph column (SPEC.md 54.3.2);
                             # the kernel reads version 1 too, nothing
                             # writes it any more
ASC_HDR   = 16
ASC_ROW   = 88               # stem 8 + size 2 + cluster 2 + 4 rsvd + icon 64
                             # + document glyph 8
ASC_ROWICO  = 16             # the icon's offset inside a row
ASC_ROWGLY  = 80             # ...and the glyph's: the eight bytes the package
                             # SHIPS (flags bit 5, at 112 in its file), or
                             # all zero = it ships none and the kernel
                             # reduces the icon (SPEC.md 54.3)
ASC_ROWCLUS = 10             # the folder the program lives in (0 = root),
                             # patched in after cluster assignment (SPEC.md
                             # 54.7.1) - it costs the file nothing, the row
                             # having reserved six bytes since it was written
ASC_NAPP  = 32
ASC_NEXT  = 24

# The kernel's own built-in association stems (kernel/assoc.inc assoc_stem).
# Mirrored here ONLY to order the rows, so that if the cap ever bites it is an
# icon row that is lost and never an association. Being out of date costs a
# cached icon, never correctness - which is why it is a plain list and not a
# generated one.
ASC_DEFAULT_STEMS = (b"PAINT", b"NOTEPAD", b"TRACKER", b"ARTFUL", b"DOS")


def build_assoc(groups):
    """ASSOC.DAT for this volume (SPEC.md 54.7 / 54.7.1).

    A warm cache, written HERE because this tool already places every package
    and therefore already knows its stem, its size, its icon and - since
    54.7.1 - the FOLDER it ends up in. A shipped disk arrives with the mount's
    per-package icon read already answered, and with every association on the
    disk locatable from one root mount.

    Returns (body, rowdirs): the bytes, and the folder key of each app row.
    The CLUSTER field is left 0 here and patched by the caller once the
    directory chains exist - which is the whole of the two-pass, and it works
    because only the row's CONTENTS depend on the layout, never its size.

    Rows are ordered association-bearing first (a header declaration, or a
    stem the kernel already knows), so that if ASC_NAPP ever bites it is an
    icon that is lost and not the ability to open a document.

    An iconless package still gets a row, holding 64 zero bytes - the all-zero
    "no icon" sentinel the kernel already understands, so caching the ABSENCE
    saves that read too. The glyph column is the same shape one field along
    (SPEC.md 54.3.2): a package that SHIPS its document glyph (flags bit 5)
    has it copied here so a cache HIT still wears it, and one that does not
    carries eight zero bytes, which the kernel reads as "reduce the icon".
    """
    cand, exts = [], []
    for key in groups:
        for name11, body, _ in groups[key]:
            if name11[8:11] != b"O88":
                continue
            if len(body) < 32 or body[0:2] != b"O8" or body[2] != 3:
                continue
            flags = body[3]
            icon = body[32:96] if flags & 1 and len(body) >= 96 else bytes(64)
            glyph = (body[112:120] if flags & 0x20 and flags & 3 == 3
                     and len(body) >= 128 else bytes(8))
            decl = []
            if flags & 2:                       # a header declaration (54.6)
                base = 96 if flags & 1 else 32
                if len(body) >= base + 16:
                    for i in range(min(body[base], 5)):
                        e = body[base + 1 + 3 * i:base + 4 + 3 * i]
                        if e != b"O88":
                            decl.append(e)
            stem = name11[0:8]
            known = stem.rstrip() in ASC_DEFAULT_STEMS
            cand.append((not (decl or known), stem, len(body) & 0xFFFF,
                         icon, glyph, decl, key))
    # stable: the ordering key is only the association flag, so argument order
    # survives inside each half and a rebuild is byte-identical
    cand.sort(key=lambda c: c[0])
    if len(cand) > ASC_NAPP:
        # ON STDERR, and that is not a style choice. assoc_clusters() is
        # IMPORTED by tools/getruncpm.py to price the cache, and getruncpm's
        # --select output is read by a `sel="$(...)"` shell substitution in
        # the Makefile - so a note on stdout becomes a FILE NAME in the
        # argument list. It arrives as `'OS88DISK:' is not a valid 8.3 name`,
        # from the disk builder, about a disk that is fine. It went unseen
        # while the everything disk had 32 packages or fewer, and fired the
        # day AUDIO.O88 made it 33.
        print(f"os88disk: assoc: {len(cand)} packages, caching "
              f"{ASC_NAPP} - {len(cand) - ASC_NAPP} harvested the slow way",
              file=sys.stderr)
        cand = cand[:ASC_NAPP]
    apps, rowdirs = [], []
    for _, stem, size, icon, glyph, decl, key in cand:
        idx = len(apps)
        apps.append((stem, size, icon, glyph))
        rowdirs.append(key)
        for e in decl:
            if len(exts) < ASC_NEXT:
                exts.append((e, idx))
    if not apps:
        return bytearray(), []
    buf = bytearray(ASC_HDR + ASC_ROW * len(apps) + 4 * len(exts))
    buf[0:6] = ASC_MAGIC
    buf[6], buf[7], buf[8] = ASC_VER, len(apps), len(exts)
    for i, (stem, size, icon, glyph) in enumerate(apps):
        o = ASC_HDR + i * ASC_ROW
        buf[o:o + 8] = stem
        struct.pack_into("<H", buf, o + 8, size)
        buf[o + ASC_ROWICO:o + ASC_ROWICO + 64] = icon
        buf[o + ASC_ROWGLY:o + ASC_ROWGLY + 8] = glyph
    eo = ASC_HDR + ASC_ROW * len(apps)
    for i, (e, ix) in enumerate(exts):
        buf[eo + 4 * i:eo + 4 * i + 3] = e
        buf[eo + 4 * i + 3] = ix
    return buf, rowdirs


def sys_attr(name11: bytes, boot: bool) -> int:
    """An entry's attributes. Only a SYSTEM disk locks anything down: a
    data disk is the user's and everything on it is an ordinary file.

    The rule is by EXTENSION so it needs no maintenance as drivers are added:
    a `.DRV` on the boot disk is kernel machinery and disappears, anything
    else is visible but read-only, because the boot disk holds nothing a user
    should be deleting by accident. SYSTEM.CFG is not here - the kernel
    creates that one itself, and stamps it the same way (SPEC.md 51.5).

    It is by DISK and by name, never by directory: TASKMGR.O88 moved from the
    boot disk's root into SYSTEM/ (SPEC.md 28.3) and is the same file it was,
    so the stamp follows it rather than staying behind with the folder it
    left."""
    if name11 == ASC_NAME:
        return A_HIDDEN | A_SYS     # the kernel rewrites it, so not read-only
    if not boot:
        return A_ARCH
    return A_SYSTEM if name11.endswith(b"DRV") else A_LOCKED


# THE COMPRESSION HINT (docs/plans/O88-COMPRESSION-PLAN.md 15). A FAT12/16 entry is
# 32 bytes and os8088 writes only six of its fields, so these four are zeroed
# on create and never read: +12 (NT case flags), +13 (creation tenths) and
# +20..21, which SPEC.md 19.1 already documents as "FstClusHI - FAT32-only per
# spec, ignored". The directory sector is read anyway, so knowing that a file
# is compressed - and how big it expands to - costs NO extra I/O at mount, per
# listing or per entry, which is what lets a listing stay exactly as fast as it
# was.
#
# **IT IS A CACHE AND THE FILE'S OWN 'CZ' HEADER IS THE AUTHORITY.** A foreign
# tool that rewrites the entry may drop these bytes; a missing hint reads as
# "not compressed", and if that were TRUSTED the kernel would hand an
# application compressed bytes. So the read path checks the header too.
CZ_HINT = 0x5A                     # +12: a value a conformant FAT writer never
                                   # puts there, so it cannot be forged by
                                   # luck - AND THE FORMAT RIDES IN IT, so
                                   # 0x5A is LZ4 and 0x5B is LZB (SPEC.md
                                   # 20.14.2.4). The kernel's capacity
                                   # decision happens before any data I/O and
                                   # the two formats get different answers
                                   # there, so a hint that could not tell them
                                   # apart would have to read the file - the
                                   # one thing this hint exists to avoid
CZ_H_MARK, CZ_H_HI, CZ_H_LO = 12, 13, 20


def dirent(name11: bytes, attr: int, clus: int, size: int,
           body: bytes = None) -> bytes:
    e = bytearray(32)
    e[0:11] = name11
    e[11] = attr
    # NT byte 0, CrtTimeTenth 0; every timestamp fixed for determinism.
    struct.pack_into("<HHH", e, 14, FIXED_TIME, FIXED_DATE, FIXED_DATE)
    struct.pack_into("<HHHHI", e, 20, 0, FIXED_TIME, FIXED_DATE, clus, size)
    if body is not None and len(body) >= 8 and body[:2] == b"CZ" \
            and body[3] == 0 and body[2] in (0, 1):
        n = int.from_bytes(body[4:8], "little")
        if n >= (1 << 24):
            fail(f"{name11!r}: a compressed file expands to {n} bytes and the "
                 "directory hint carries 24 bits")
        e[CZ_H_MARK] = CZ_HINT + body[2]    # ...the mark, PLUS the format
        e[CZ_H_HI] = n >> 16
        struct.pack_into("<H", e, CZ_H_LO, n & 0xFFFF)
    return bytes(e)


def vol_id(content: bytes) -> int:
    """BS_VolID for a volume holding `content` - DERIVED, not pinned.

    **THIS IS THE ONLY THING THAT TELLS TWO os8088 DISKS APART.** SPEC.md
    18.8.2's `dsk_bpb_sig` signs LBA 0 and nothing else, and SPEC.md 18.95's
    sector cache and SPEC.md 18.8's FAT window are both keyed on that
    signature - so two volumes whose boot sectors are byte-identical are one
    volume as far as the running machine is concerned. Swap one for the other
    and every cached sector, the FAT window included, stays valid against a
    platter it did not come from: the listing is the old disk's, a file
    "cannot be read", and a write puts the old disk's FAT onto the new one.

    A fixed serial made every non-bootable disk of a geometry identical -
    MEASURED, 23 of them signing 0x2D68 - which on a 360KB machine is every
    data floppy the project ships. 18.8.2 called that residual "accepted
    deliberately" on the grounds that "a full mount re-validates"; the full
    mount does re-read LBA 0 and does bypass the cache, and it re-reads 512
    bytes that are the same 512 bytes, so the re-validation could never have
    caught it. The ground was wrong when it was written, not made wrong later.

    Deriving it from the volume's own bytes keeps the property the pin was
    for - the same inputs build the same image, byte for byte - and drops the
    one it never should have had. The digest is over the FAT, the root
    directory and the data area, which is the whole volume EXCEPT this sector,
    so there is no circularity to resolve.

    Two volumes with identical content get the same serial, which is correct:
    they are the same disk, and nothing on the machine could act on a
    difference that does not exist.
    """
    d = hashlib.sha256(content).digest()
    v = struct.unpack("<I", d[:4])[0]
    return v or VOL_ID                           # 0 is a legal serial but
                                                 # reads as "unset" to tools


def boot_sector(spt, heads, tot, spc, fatsz, root_ent, media,
                lay: Layout, code: bytes = None, label: bytes = None,
                hidden: int = 0, drvnum: int = 0, ksecs: int = 0,
                volid: int = None) -> bytes:
    """One BPB, three uses. `code` is os8088's own 512-byte boot sector -
    boot/boot.asm's on a floppy, boot/boothd.asm's under --hdd: either way
    its first three bytes are already EB 3C 90 and bytes 62.. are its
    loader, so the BPB is written into the hole between them and everything
    else is left exactly as nasm assembled it. Without `code` the sector
    carries the not-bootable stub instead.

    `hidden`/`drvnum`/`ksecs` are the hard-disk volume boot record's three
    extras (SPEC.md 52.10.2): the partition base boothd.asm adds to every
    LBA, BS_DrvNum 80h, and the kernel sector count in the pinned word at
    offset 508 that the installer would otherwise patch."""
    bs = bytearray(code if code else SECTOR)
    if code:
        if len(code) != SECTOR:
            fail(f"boot code is {len(code)} bytes, not {SECTOR}")
        if code[0:3] != b"\xEB\x3C\x90":
            fail("boot code does not open with `jmp short 0x3E / nop` - "
                 "see boot/boot.asm's BPB_END")
    bs[0:3] = b"\xEB\x3C\x90"                   # jmp short 0x3E; nop
    bs[3:11] = b"MSDOS5.0"                      # interop OEM name
    struct.pack_into("<H", bs, 11, SECTOR)      # BPB_BytsPerSec
    bs[13] = spc                                # BPB_SecPerClus
    struct.pack_into("<H", bs, 14, lay.rsvd)    # BPB_RsvdSecCnt
    bs[16] = lay.nfats                          # BPB_NumFATs
    struct.pack_into("<H", bs, 17, root_ent)    # BPB_RootEntCnt
    struct.pack_into("<H", bs, 19, tot)         # BPB_TotSec16
    bs[21] = media                              # BPB_Media
    struct.pack_into("<H", bs, 22, fatsz)       # BPB_FATSz16
    struct.pack_into("<H", bs, 24, spt)         # BPB_SecPerTrk
    struct.pack_into("<H", bs, 26, heads)       # BPB_NumHeads
    # On a floppy BPB_HiddSec, BS_DrvNum and BPB_TotSec32 stay 0.
    struct.pack_into("<I", bs, 28, hidden)      # BPB_HiddSec
    bs[36] = drvnum                             # BS_DrvNum
    bs[38] = 0x29                               # BS_BootSig
    struct.pack_into("<I", bs, 39,
                     VOL_ID if volid is None else volid)   # BS_VolID
    bs[43:54] = label or VOL_LABEL              # BS_VolLab
    bs[54:62] = b"FAT12   " if lay.fat12 else b"FAT16   "
    if not code:
        bs[62:62 + len(BOOT_STUB)] = BOOT_STUB
    if ksecs:
        struct.pack_into("<H", bs, BOOTHD_KSECS, ksecs)
    bs[510:512] = b"\x55\xAA"
    return bytes(bs)


def hdd_layout(tot: int) -> Layout:
    """The FAT16 layout for `tot` sectors: the smallest cluster that keeps
    the count inside FAT16's range - the shape drivers/hdd/fmt.inc's own
    capacity table produces, and the one tools/os88hdd.py builds for the
    installer's test fixture (SPEC.md 52.10)."""
    root_secs = (HDD_ROOT_ENT * 32 + SECTOR - 1) // SECTOR
    for spc in (4, 8, 16, 32, 64):
        fatsz = 1
        nclus = 0
        for _ in range(64):                     # converges in two or three
            data = tot - 1 - 2 * fatsz - root_secs
            nclus = data // spc
            need = ((nclus + 2) * 2 + SECTOR - 1) // SECTOR
            if need == fatsz:
                break
            fatsz = need
        if 4085 <= nclus < 65525:
            return Layout(spc, 1, 2, HDD_ROOT_ENT, tot, fatsz)
    fail(f"no FAT16 layout fits {tot} sectors")


def hdd_chs(lba: int, heads: int = HDD_HEADS, spt: int = HDD_SPT) -> bytes:
    """LBA as the 3-byte CHS field of a partition entry, under a geometry -
    the fixed --hdd one unless --retarget says otherwise. Never clamped: the
    build's 65 cylinders is far inside CHS range and hdd_retarget refuses a
    cylinder past 1023 before calling this. The two columns agreeing IS the
    contract (SPEC.md 80.1) - a BIOS booting this image derives its virtual
    geometry from these fields."""
    c, r = divmod(lba, spt * heads)
    h, s = divmod(r, spt)
    return bytes([h, ((s + 1) & 0x3F) | ((c >> 2) & 0xC0), c & 0xFF])


def chs_lba(field: bytes, heads: int, spt: int) -> int:
    """The inverse: a partition entry's 3-byte CHS field as an LBA under a
    geometry, for --verify-hdd's agreement test."""
    h = field[0]
    s = field[1] & 0x3F
    c = ((field[1] & 0xC0) << 2) | field[2]
    return (c * heads + h) * spt + (s - 1)


def parse_geometry(text: str):
    """'HEADS/SPT' or 'CYLS/HEADS/SPT' -> (cyls or None, heads, spt).
    ValueError, never sys.exit: the imager catches this and goes back to
    its prompt."""
    parts = text.strip().replace("x", "/").replace("X", "/").split("/")
    try:
        nums = [int(p) for p in parts]
    except ValueError:
        raise ValueError(f"geometry {text!r}: want HEADS/SPT, digits only")
    if len(nums) == 2:
        cyls, (heads, spt) = None, nums
    elif len(nums) == 3:
        cyls, heads, spt = nums
    else:
        raise ValueError(f"geometry {text!r}: want HEADS/SPT or C/H/S")
    if not 1 <= heads <= 255:
        raise ValueError(f"heads {heads}: int 13h carries 1..255")
    if not 1 <= spt <= 63:
        raise ValueError(f"sectors per track {spt}: int 13h carries 1..63")
    if cyls is not None and not 1 <= cyls <= 65535:
        raise ValueError(f"cylinders {cyls}: a drive's own count is a word")
    return cyls, heads, spt


def hdd_retarget(img: bytes, heads: int, spt: int) -> bytes:
    """SPEC.md 80.5: rewrite the ten bytes of a --hdd image that name a
    geometry - each partition entry's two CHS columns and the volume's
    BPB_SecPerTrk/BPB_NumHeads - to `heads` x `spt`, and nothing else. The
    LBA layout does not move: a ROM that reports this geometry (an XTIDE
    card, say) then divides every LBA the same way the writer did, which is
    field note 33's invariant with the imager as the writer.

    ValueError on anything but a well-formed --hdd image whose table and
    volumes already agree: this is for images this tool built, and a
    foreign disk is refused rather than quietly rewritten. Cylinders are
    never clamped - a partition whose last sector needs cylinder 1024 or
    more under the new shape is refused, since the field cannot carry it."""
    parse_geometry(f"{heads}/{spt}")             # the same range rules
    if len(img) < 2 * SECTOR or len(img) % SECTOR:
        raise ValueError("not a sector-multiple image of at least 2 sectors")
    if img[510:512] != b"\x55\xaa":
        raise ValueError("sector 0 carries no boot signature: not an MBR")
    out = bytearray(img)
    nsec = len(img) // SECTOR
    touched = 0
    for i in range(4):
        o = HP_TBL + i * 16
        ent = img[o:o + 16]
        typ = ent[4]
        lba, cnt = struct.unpack("<II", ent[8:16])
        if not typ and not cnt:
            continue
        if not cnt or lba + cnt > nsec:
            raise ValueError(f"partition {i}: {cnt} sectors at LBA {lba} "
                             f"does not lie inside the {nsec}-sector image")
        v = lba * SECTOR
        bs = img[v:v + SECTOR]
        if bs[510:512] != b"\x55\xaa":
            raise ValueError(f"partition {i}: no boot record at LBA {lba}")
        hid, = struct.unpack_from("<I", bs, 28)
        bspt, bheads = struct.unpack_from("<HH", bs, 24)
        if hid != lba:
            raise ValueError(f"partition {i}: the volume says it starts at "
                             f"LBA {hid} and the table at {lba}")
        if not bspt or not bheads:
            raise ValueError(f"partition {i}: the volume's BPB has no "
                             "geometry to retarget")
        for name, field, want in (("start", ent[1:4], lba),
                                  ("end", ent[5:8], lba + cnt - 1)):
            if chs_lba(field, bheads, bspt) != want:
                raise ValueError(f"partition {i}: the {name} CHS column and "
                                 f"the LBA column disagree under the "
                                 f"volume's own {bheads}x{bspt}; not an "
                                 "image this tool built")
        last = lba + cnt - 1
        if last // (heads * spt) > 1023:
            raise ValueError(f"partition {i}: its last sector, LBA {last}, "
                             f"needs cylinder {last // (heads * spt)} under "
                             f"{heads}x{spt} and the CHS field stops at 1023")
        out[o + 1:o + 4] = hdd_chs(lba, heads, spt)
        out[o + 5:o + 8] = hdd_chs(last, heads, spt)
        struct.pack_into("<HH", out, v + 24, spt, heads)
        touched += 1
    if not touched:
        raise ValueError("no partition in the table")
    return bytes(out)


def retarget(args) -> int:
    """`--retarget IMG --geometry HEADS/SPT -o OUT`: the dd user's form of
    what the imager does in memory (SPEC.md 80.5). A leading cylinder count
    is accepted so a C/H/S line can be pasted whole; it is checked against
    the image - the card must HOLD the image under that geometry - and not
    written anywhere, because nothing on the disk names a cylinder count."""
    try:
        with open(args.retarget, "rb") as f:
            img = f.read()
    except OSError as e:
        fail(f"cannot read {args.retarget}: {e}")
    try:
        cyls, heads, spt = parse_geometry(args.geometry)
        if cyls is not None and cyls * heads * spt < len(img) // SECTOR:
            raise ValueError(f"{cyls}/{heads}/{spt} is {cyls * heads * spt} "
                             f"sectors and the image is {len(img) // SECTOR}")
        out = hdd_retarget(img, heads, spt)
    except ValueError as e:
        fail(f"retarget {args.retarget}: {e}")
    try:
        with open(args.output, "wb") as f:
            f.write(out)
    except OSError as e:
        fail(f"cannot write {args.output}: {e}")
    print(f"os88disk: {args.output}: {args.retarget} retargeted to "
          f"{heads} heads x {spt} sectors per track"
          + (f" ({cyls} cylinders)" if cyls else ""))
    return 0


def read_data_file(path: str) -> bytes:
    """Read one non-.O88 data file: shipped as-is, no package validation."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        fail(f"cannot read {path}: {e}")
    if not data:
        fail(f"{path}: empty")
    return data


def folder83(name: str) -> bytes:
    """Derive the 11-byte FAT short name of a subdirectory (no extension)."""
    up = name.upper()
    if not 1 <= len(up) <= 8:
        fail(f"folder '{name}': 1-8 characters (an 8.3 stem, no extension)")
    if up in RESERVED_STEMS:
        fail(f"folder '{name}': reserved DOS device name")
    for ch in up.encode("ascii", "replace"):
        if ch not in NAME_CHARS:
            fail(f"folder '{name}': characters outside A-Z 0-9 _ -")
    return up.encode().ljust(11)


def folder_key(spec: str) -> str:
    """Normalise a folder specification to its key: 'system/dos' -> the
    key 'SYSTEM/DOS'. A key is the path from the root with '/' between the
    components and '' for the root itself, so a folder's parent is one
    rpartition away and no second structure records the tree.

    Every component is validated as an 8.3 stem, which is what makes a
    nested folder exactly as constrained as a top-level one - the kernel
    walks '..' off the disk (SPEC.md 19.2) and so has no idea how deep it
    is, but a directory ENTRY is 8.3 at every level."""
    parts = spec.replace("\\", "/").split("/")
    if any(not p for p in parts):
        fail(f"folder '{spec}': empty path component")
    for p in parts:
        folder83(p)                              # per component, not per path
    return "/".join(p.upper() for p in parts)


def folder_parent(key: str) -> str:
    """The key of this folder's parent ('' = the root)."""
    return key.rpartition("/")[0]


def folder_leaf(key: str) -> str:
    """The folder's own name - what its directory ENTRY is called."""
    return key.rpartition("/")[2]


def split_spec(arg: str):
    """'GAMES:build/mines.o88' -> ('GAMES', 'build/mines.o88'), and
    'SYSTEM/DOS:build/os88net.com' -> ('SYSTEM/DOS', ...)."""
    folder, sep, path = arg.partition(":")
    if not sep:
        return None, arg
    if not path:
        fail(f"'{arg}': folder prefix with no package after it")
    return folder.upper(), path


def read_blob(path: str, what: str) -> bytes:
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError as e:
        fail(f"cannot read {what} {path}: {e}")


def build(args) -> int:
    mbr = b""
    if args.hdd:
        # A hard-disk image (SPEC.md 80.1) is a SYSTEM disk by construction:
        # its whole point is booting, so the three parts of the chain are
        # required rather than optional the way --boot/--kernel are.
        if not (args.boot and args.kernel and args.mbr):
            fail("--hdd needs --mbr (mbr.bin), --boot (boothd.bin) "
                 "and --kernel")
        mbr = read_blob(args.mbr, "MBR boot code")
        if len(mbr) != HP_TBL:
            fail(f"{args.mbr} is {len(mbr)} bytes, not {HP_TBL}")
        spt, heads, tot, media = HDD_SPT, HDD_HEADS, HDD_PSECS, 0xF8
    else:
        spt, heads, tot, spc, fatsz, root_ent, media = GEOMETRY[args.size]
        if args.fatcap:
            spc, fatsz = fatcap(args.size, args.fatcap)

    # --- the system disk (SPEC.md 19.3) --------------------------------------
    # The kernel is an ordinary FILE, KERNEL.SYS, allocated FIRST and
    # CONTIGUOUSLY from cluster 2 - so its first sector is where the data area
    # begins, which is arithmetic boot/boot.asm can do from the BPB, and a
    # flat run of reads stands in for walking a cluster chain in 512 bytes.
    #
    # It used to live in the RESERVED AREA, with BPB_RsvdSecCnt covering it.
    # That is a legal use of the field and os8088 read it perfectly, but DOS
    # does not honour a floppy's BPB - it builds one from its own table of
    # standard formats, so it put the FAT and root directory at the standard
    # offsets and read the kernel as file system: garbled entries and 52,224
    # bytes free, on PC-DOS 3.30 and MS-DOS 5.00 alike. RsvdSecCnt is 1 now,
    # like every other floppy, and there is nothing left for DOS to get wrong.
    boot = label = None
    kern = b""
    ksecs = 0
    if args.boot or args.kernel:
        if not (args.boot and args.kernel):
            fail("--boot and --kernel go together (a system disk needs both)")
        boot = read_blob(args.boot, "boot sector")
        kern = read_blob(args.kernel, "kernel")
        ksecs = (len(kern) + SECTOR - 1) // SECTOR
        label = SYS_LABEL
    if args.hdd:
        label = HDD_LABEL
        lay = hdd_layout(tot)
        spc, fatsz, root_ent = lay.spc, lay.fatsz, lay.root_ent
    else:
        lay = Layout(spc, 1, 2, root_ent, tot, fatsz)

    # Group by folder, keeping first-appearance order. Names are checked for
    # duplicates PER DIRECTORY: two folders may each hold a MINES.O88.
    groups: dict = {}                            # folder ('' = root) -> list
    seen: dict = {}                              # folder -> set of name11

    # A folder comes into existence because something named it, and naming
    # SYSTEM/DOS names SYSTEM too - so `ensure` walks up, and a parent is
    # therefore always created BEFORE its children. Everything below reads
    # that ordering: the directory chains are laid out in it, and a folder's
    # entry in its parent is written from the same list.
    def ensure(key: str):
        if key in groups:
            return
        if key:
            ensure(folder_parent(key))
        groups[key] = []
        seen[key] = set()

    # --folder makes a folder with NOTHING in it. A folder normally comes into
    # existence because a package named it, which cannot express the one case
    # the system disk needs: MEDIA is where the Standard File dialog opens
    # (SPEC.md 38.10) and the boot floppy has no media on it to put there. An
    # empty directory is one cluster holding '.' and '..', so the code below
    # needs no special case - only a key with an empty list.
    for folder in args.folder:
        ensure(folder_key(folder))

    raw_paths = set(args.raw)
    for arg in args.packages:
        folder, path = split_spec(arg)
        key = folder_key(folder) if folder else ""
        ensure(key)
        name11 = name83(path, seen[key])
        # Only *.O88 entries are packages; anything else ships as data, so a
        # disk can carry a module, a picture or a text file next to the
        # programs that read it (SPEC.md 24). The 8.3 name, the per-directory
        # cap and the duplicate check all apply the same either way.
        if path.lower().endswith(".o88") and path not in raw_paths:
            data = validate_o88(path)
        else:
            data = read_data_file(path)
        nclusters = (len(data) + lay.cluster_bytes - 1) // lay.cluster_bytes
        groups[key].append((name11, data, nclusters))

    # Root order: every folder first, in first-appearance order, then the
    # root-level packages. Only a TOP-LEVEL folder costs a root entry; a
    # nested one costs an entry in its parent instead, which is the whole of
    # what nesting changes about the directory arithmetic below.
    dirs = [k for k in groups if k]
    kids = {k: [c for c in dirs if folder_parent(c) == k] for k in dirs}
    root_dirs = [k for k in dirs if not folder_parent(k)]
    root_files = groups.get("", [])

    # A warm ASSOC.DAT (SPEC.md 54.7), from the packages on this disk. It is
    # built before any cluster is assigned, which it can be because its rows
    # are keyed by (stem, size) and carry no layout at all.
    asc, asc_rowdirs = build_assoc(groups)
    if asc:
        root_files = root_files + [(
            ASC_NAME, asc,
            max(1, (len(asc) + lay.cluster_bytes - 1) // lay.cluster_bytes))]
    # A folder and a file are two entries in the same directory, so they can
    # collide - `SYSTEM/DOS` beside a file called `DOS` writes one name twice
    # and the volume is broken in a way no FAT rule catches. The per-file
    # duplicate check cannot see it: `seen` holds files, and a folder is not
    # one. This is the second half of it, and it is new because a folder had
    # nowhere but the root to be until folders nested.
    for key in list(dirs) + [""]:
        taken = dict.fromkeys(n for n, _, _ in groups.get(key, []))
        for c in (kids[key] if key else root_dirs):
            n = folder83(folder_leaf(c))
            if n in taken:
                fail(f"'{folder_leaf(c)}' is both a folder and a file in "
                     f"{key or 'the root'}")
            taken[n] = None

    # ...and WHICH kernel is asked, since SPEC.md 22.6.2 made DSK_NENT
    # per-build: a disk written for kern_small is listed by a 32-entry
    # listing and one written for kern_big by a 64-entry one. The cap is the
    # kernel's number either way - neither is restated here - and the four
    # recipes that pass `--kern-small` are exactly the four that pass
    # `--fatcap 2`, which is the same build saying the same thing about its
    # other disk constant.
    cap = SMALL_FILES if args.kern_small else MAX_FILES
    for key in dirs:
        shown = len(kids[key]) + sum(
            1 for n, _, _ in groups[key]
            if not sys_attr(n, bool(boot)) & A_HIDDEN)
        if shown > cap and not args.deep_folders:
            fail(f"{shown} listed entries in folder {key}; the kernel "
                 f"lists at most {cap} per directory (--deep-folders "
                 f"if this folder is a data store the file API walks, not "
                 f"one the Disk window shows)")
    # MAX_FILES is a DISPLAY cap, so only what the kernel would list counts
    # against it: a hidden system file (SPEC.md 19.6) never takes a listing
    # slot. It still takes a directory slot, which is the second check.
    shown = len(root_dirs) + sum(1 for n, _, _ in root_files
                                 if not sys_attr(n, bool(boot)) & A_HIDDEN)
    if shown > cap:
        fail(f"{shown} listed root entries; the kernel lists "
             f"at most {cap} per directory")

    # A folder's own directory is a cluster chain like any other file: two
    # link entries ('.', '..'), one entry per subfolder, and its files,
    # rounded up to clusters.
    # ...or to the slots --dir-slots asked for. THE KERNEL DOES NOT GROW A
    # DIRECTORY (SPEC.md 18.5: FERR_DIRFULL when the scan reaches the end
    # of the chain with no reusable slot), so a folder that programs will
    # SAVE INTO ships with spare slots, or the first few saves after the
    # last one fill it - RUNCPM's A\0 is built with room for the files a
    # CP/M session makes (SPEC.md 71.3).
    min_slots = {}
    for spec in args.dir_slots:
        key, sep, n = spec.rpartition("=")
        if not sep or not n.isdigit():
            fail(f"--dir-slots wants FOLDER=N, not '{spec}'")
        min_slots[folder_key(key)] = int(n)
    for key in min_slots:
        if key not in dirs:
            fail(f"--dir-slots {key}: no such folder on this disk")
    dir_nclus = {k: max(1, (max(2 + len(kids[k]) + len(groups[k]),
                                min_slots.get(k, 0)) * 32
                            + lay.cluster_bytes - 1)
                        // lay.cluster_bytes) for k in dirs}

    files = [f for k in dirs for f in groups[k]] + root_files
    kclus = (len(kern) + lay.cluster_bytes - 1) // lay.cluster_bytes
    need = sum(n for _, _, n in files) + sum(dir_nclus.values()) + kclus
    if need > lay.nclus:
        fail(f"packages need {need} clusters; disk holds {lay.nclus}")
    # The root directory is a fixed number of 32-byte slots: the volume
    # label, KERNEL.SYS on a boot disk, then every folder and root file.
    slots = 1 + (1 if boot else 0) + len(root_dirs) + len(root_files)
    if slots > root_ent:
        fail(f"{slots} root entries; the root directory holds {root_ent}")

    # THE KERNEL FIRST, from cluster 2 and contiguous, so its first sector is
    # the data area's first sector - which is what boot/boot.asm derives from
    # the BPB. Never scrambled: --scramble exists to prove the cluster walker
    # copes with fragmentation, and the boot sector has no cluster walker.
    nxt = 2
    kchain = list(range(nxt, nxt + kclus))
    nxt += kclus

    # Then the directory chains, contiguously and in root order, so a folder's
    # listing is one seek away from the root's.
    dir_chains = {}
    for k in dirs:
        dir_chains[k] = list(range(nxt, nxt + dir_nclus[k]))
        nxt += dir_nclus[k]

    # Then the file chains: contiguous in argument order, or round-robin
    # interleaved under --scramble (legally fragmented).
    chains = [[] for _ in files]
    if args.scramble:
        while any(len(c) < f[2] for c, f in zip(chains, files)):
            for c, f in zip(chains, files):
                if len(c) < f[2]:
                    c.append(nxt)
                    nxt += 1
    else:
        for c, f in zip(chains, files):
            c.extend(range(nxt, nxt + f[2]))
            nxt += f[2]

    # PASS 2 of ASSOC.DAT (SPEC.md 54.7.1): the folder each program lives in,
    # now that the directory chains exist. `asc` is a bytearray and `files`
    # holds the same object, so patching here reaches the bytes `put` writes -
    # and only the row CONTENTS depend on the layout, never the row COUNT, so
    # nothing above this had to know.
    for i, key in enumerate(asc_rowdirs):
        struct.pack_into("<H", asc, ASC_HDR + i * ASC_ROW + ASC_ROWCLUS,
                         0 if not key else dir_chains[key][0])

    fat = Fat(lay, media)
    data_area = bytearray((tot - lay.data_lba) * SECTOR)

    def link(chain):
        for k, cl in enumerate(chain):
            fat.set(cl, chain[k + 1] if k + 1 < len(chain)
                    else (0xFFF if lay.fat12 else 0xFFFF))

    def put(chain, body):
        link(chain)
        for k, cl in enumerate(chain):
            dst = (cl - 2) * lay.cluster_bytes
            chunk = body[k * lay.cluster_bytes:(k + 1) * lay.cluster_bytes]
            data_area[dst:dst + len(chunk)] = chunk

    if kchain:
        put(kchain, kern)
    for chain, (_, body, _) in zip(chains, files):
        put(chain, body)

    # Each folder's directory contents, now that its members have clusters.
    at = 0
    for k in dirs:
        raw = bytearray(len(dir_chains[k]) * lay.cluster_bytes)
        parent = folder_parent(k)
        raw[0:32] = dirent(b".".ljust(11), 0x10, dir_chains[k][0], 0)
        # '..' is the PARENT's first cluster, with the FAT convention that a
        # parent of the root is written 0 - which is what dsk_dotdot reads to
        # go up, so a nested folder needs nothing else to be navigable.
        raw[32:64] = dirent(b"..".ljust(11), 0x10,
                            dir_chains[parent][0] if parent else 0, 0)
        slot = 2
        for c in kids[k]:
            raw[slot * 32:(slot + 1) * 32] = dirent(
                folder83(folder_leaf(c)), 0x10, dir_chains[c][0], 0)
            slot += 1
        for i, (name11, body, _) in enumerate(groups[k]):
            off = (slot + i) * 32
            raw[off:off + 32] = dirent(name11, sys_attr(name11, boot),
                                       chains[at + i][0], len(body), body)
        at += len(groups[k])
        put(dir_chains[k], bytes(raw))

    root = bytearray(lay.root_secs * SECTOR)
    root[0:32] = dirent(label or VOL_LABEL, 0x08, 0, 0)  # label first: the
    slot = 1                                     # kernel filters it, so the
    if boot:                                     # first listed entry is 0
        root[slot * 32:(slot + 1) * 32] = dirent(
            KERNEL_NAME, A_SYSTEM, kchain[0], len(kern))
        slot += 1
    for k in root_dirs:
        root[slot * 32:(slot + 1) * 32] = dirent(
            folder83(folder_leaf(k)), 0x10, dir_chains[k][0], 0)
        slot += 1
    for i, (name11, body, _) in enumerate(root_files):
        chain = chains[len(files) - len(root_files) + i]
        root[slot * 32:(slot + 1) * 32] = dirent(
            name11, sys_attr(name11, boot), chain[0], len(body), body)
        slot += 1

    # THE SERIAL IS DERIVED FROM WHAT IS ON THE VOLUME (vol_id): it is the
    # only field that tells two os8088 disks of one geometry apart, and the
    # machine's whole swap detector is a signature over this sector.
    body = bytes(fat.buf + fat.buf + root + data_area)
    image = bytearray(boot_sector(spt, heads, tot, spc, fatsz, root_ent,
                                  media, lay, boot, label,
                                  hidden=HDD_BASE if args.hdd else 0,
                                  drvnum=0x80 if args.hdd else 0,
                                  ksecs=ksecs if args.hdd else 0,
                                  volid=vol_id(body)))
    image += body
    assert len(image) == tot * SECTOR

    if args.hdd:
        # The MBR sector in front of the volume: boot/mbr.asm's 446 bytes,
        # one active entry whose CHS and LBA columns agree (SPEC.md 80.1),
        # type 04h - the partition is under 32MB by construction - and the
        # rest of the MBR's track left zero, which is where the era left it.
        sec0 = bytearray(SECTOR)
        sec0[0:HP_TBL] = mbr
        ent = bytearray(16)
        ent[0] = 0x80                            # active
        ent[1:4] = hdd_chs(HDD_BASE)
        ent[4] = 0x04                            # FAT16 under 32MB
        ent[5:8] = hdd_chs(HDD_BASE + HDD_PSECS - 1)
        struct.pack_into("<I", ent, 8, HDD_BASE)
        struct.pack_into("<I", ent, 12, HDD_PSECS)
        sec0[HP_TBL:HP_TBL + 16] = ent
        sec0[510:512] = b"\x55\xAA"
        image = bytes(sec0) + bytes((HDD_BASE - 1) * SECTOR) + bytes(image)
        assert len(image) == HDD_TOT * SECTOR

    try:
        with open(args.output, "wb") as f:
            f.write(image)
    except OSError as e:
        fail(f"cannot write {args.output}: {e}")

    geom = (f"{HDD_CYLS}/{HDD_HEADS}/{HDD_SPT} hdd, partition at LBA "
            f"{HDD_BASE} for {HDD_PSECS} sectors" if args.hdd
            else f"{args.size}KB, {spt} spt")
    print(f"os88disk: {args.output} ({geom}, "
          f"{lay.type_name}) {len(files)} file(s)"
          + (f" in {len(dirs)} folder(s)" if dirs else "")
          + f", {need}/{lay.nclus} clusters"
          + (f", KERNEL.SYS {ksecs} sectors at LBA {lay.data_lba}"
             if boot else "")
          + (", scrambled" if args.scramble and files else ""))
    return 0


def verify_hdd(path: str) -> int:
    """Structural fsck for a PARTITIONED image - a hard disk, not a floppy.

    `--verify` is a floppy's: it fails a BPB whose geometry is not one of six
    real floppy shapes, and a FAT bigger than DSK_FAT_SECS. Neither rule
    applies to a driver-backed volume (SPEC.md 18.7/52.4), and the machine
    this project is calibrated against has a 20MB ST-225 in it, so there was
    no way to answer "is the hard disk corrupt?" without writing a throwaway
    script - which is how a throwaway answer gets trusted.

    **THE MBR IS FOUND, NOT ASSUMED AT LBA 0.** A Seagate ST-11M reserves the
    first sectors of the drive for itself and presents the one after them as
    the BIOS's LBA 0, so a raw image of that drive has its partition table 68
    sectors in and every partition LBA is relative to there. Read the raw
    sector 0 of one and you get the controller's own geometry block - which
    looks exactly like a destroyed MBR, and was read as one for a while. The
    tell is that the partition's `hidden` field agrees with its table entry
    only at the right offset, and that is what this searches for.
    """
    errors = []
    try:
        with open(path, "rb") as f:
            img = f.read()
    except OSError as e:
        fail(f"cannot read {path}: {e}")

    def sig_at(base):
        o = base * SECTOR
        return len(img) >= o + SECTOR and img[o + 510:o + 512] == b"\x55\xaa"

    def parts_at(base):
        o = base * SECTOR
        out = []
        for i in range(4):
            e = img[o + 446 + i * 16:o + 446 + i * 16 + 16]
            typ = e[4]
            lba, cnt = struct.unpack("<II", e[8:16])
            # The physical extent is base + lba + cnt: partition LBAs are
            # relative to the table's own sector on these images, so a bound
            # that omits base admits a partition reaching past a truncated
            # image - which then dies in psec() with an IndexError traceback
            # instead of a diagnostic.
            if typ and cnt and base + lba + cnt <= len(img) // SECTOR:
                out.append((i, typ, lba, cnt))
        return out

    base = None
    for cand in range(0, 129):               # 68 on an ST-11M; 0 on anything
        if not sig_at(cand):                 # sane. The agreement test below
            continue                         # is what makes the search safe
        ps = parts_at(cand)
        if not ps:
            continue
        ok = True
        for _, _, lba, _ in ps:
            b = img[(cand + lba) * SECTOR:(cand + lba) * SECTOR + SECTOR]
            if len(b) < SECTOR or b[510:512] != b"\x55\xaa":
                ok = False
                break
            hid, = struct.unpack_from("<I", b, 28)
            if hid != lba:                   # the volume's own opinion of
                ok = False                   # where it starts must match the
                break                        # table's, or this is not it
        if ok:
            base = cand
            break
    if base is None:
        fail(f"{path}: no partition table whose entries agree with their "
             "volumes, at any offset in the first 128 sectors")
    if base:
        print(f"os88disk: verify-hdd: {path}: partition table at physical "
              f"sector {base} - {base} reserved sectors in front of it "
              "(an ST-11M-style controller area)")

    total_files = 0
    for idx, typ, plba, pcnt in parts_at(base):
        o = (base + plba) * SECTOR
        bs = img[o:o + SECTOR]
        bps, = struct.unpack_from("<H", bs, 11)
        spc = bs[13]
        rsvd, = struct.unpack_from("<H", bs, 14)
        nfats = bs[16]
        root_ent, = struct.unpack_from("<H", bs, 17)
        tot, = struct.unpack_from("<H", bs, 19)
        fatsz, = struct.unpack_from("<H", bs, 22)
        if not tot:
            tot, = struct.unpack_from("<I", bs, 32)
        rootsecs = (root_ent * 32 + bps - 1) // bps
        data_lba = rsvd + nfats * fatsz + rootsecs
        nclus = (tot - data_lba) // spc if spc else 0
        fat16 = nclus >= 4085
        print(f"os88disk: verify-hdd: {path}: partition {idx} type 0x{typ:02X} "
              f"at LBA {plba}, {pcnt} sectors: {'FAT16' if fat16 else 'FAT12'}, "
              f"{nclus} clusters of {spc * bps} bytes")
        if bps != 512:
            errors.append(f"partition {idx}: BPB_BytsPerSec {bps} != 512")
            continue

        # SPEC.md 80.5: the entry's CHS columns must describe the sectors the
        # volume's own BPB geometry says they are - the MBR reads by the
        # column and the boot record divides by the BPB, and a disk on which
        # the two disagree is field note 33's. An entry whose end is past
        # cylinder 1023 cannot say, and is skipped rather than failed.
        bspt, bheads = struct.unpack_from("<HH", bs, 24)
        ent = img[(base * SECTOR) + 446 + idx * 16:(base * SECTOR) + 446 + idx * 16 + 16]
        if bspt and bheads:
            for name, field, want in (("start", ent[1:4], plba),
                                      ("end", ent[5:8], plba + pcnt - 1)):
                if want // (bheads * bspt) > 1023:
                    continue
                got = chs_lba(field, bheads, bspt)
                if got != want:
                    errors.append(f"partition {idx}: {name} CHS column is "
                                  f"LBA {got} under the volume's own "
                                  f"{bheads}x{bspt}, the LBA column says "
                                  f"{want}")
        else:
            errors.append(f"partition {idx}: BPB_SecPerTrk/BPB_NumHeads "
                          f"{bspt}/{bheads} - the boot record divides by "
                          "these (SPEC.md 52.10.2)")

        def psec(n, count=1):
            a = o + n * SECTOR
            return img[a:a + SECTOR * count]

        f1 = psec(rsvd, fatsz)
        f2 = psec(rsvd + fatsz, fatsz) if nfats > 1 else f1

        def ent(fat, n):
            if fat16:
                return struct.unpack_from("<H", fat, n * 2)[0]
            b = n + n // 2
            v = fat[b] | (fat[b + 1] << 8)
            return (v >> 4) if (n & 1) else (v & 0xFFF)

        eoc = 0xFFF8 if fat16 else 0xFF8
        if nfats > 1:
            d = [n for n in range(nclus + 2) if ent(f1, n) != ent(f2, n)]
            if d:
                errors.append(f"partition {idx}: FAT1 and FAT2 differ at "
                              f"{len(d)} entries, first {d[:8]}")

        owner = {}

        def chain(start, name):
            c, n, seen = start, 0, set()
            while True:
                if c < 2 or c > nclus + 1:
                    if c and c < eoc:
                        errors.append(f"{name}: chain ends on cluster "
                                      f"0x{c:04X}, outside 2..{nclus + 1}")
                    return n
                if c in seen:
                    errors.append(f"{name}: CLUSTER LOOP at {c}")
                    return n
                if c in owner:
                    errors.append(f"{name}: CROSS-LINKED with {owner[c]} "
                                  f"at cluster {c}")
                    return n
                seen.add(c)
                owner[c] = name
                n += 1
                c = ent(f1, c)

        def walk(start, path_, is_root):
            nonlocal total_files
            if is_root:
                d = psec(rsvd + nfats * fatsz, rootsecs)
            else:
                d, c, guard, seen = b"", start, 0, set()
                while 2 <= c <= nclus + 1 and guard < 4096:
                    if c in seen:
                        errors.append(f"{path_}: DIRECTORY CHAIN LOOP at {c}")
                        break
                    seen.add(c)
                    d += psec(data_lba + (c - 2) * spc, spc)
                    c = ent(f1, c)
                    guard += 1
            subs = []
            for i in range(0, len(d), 32):
                e = d[i:i + 32]
                if not e or e[0] == 0:
                    break
                if e[0] == 0xE5:
                    continue
                attr = e[11]
                if attr & 0x08 and not attr & 0x10:
                    continue
                nm = e[0:8].decode("latin-1").rstrip()
                ex = e[8:11].decode("latin-1").rstrip()
                if nm in (".", ".."):
                    continue
                clus, = struct.unpack_from("<H", e, 26)
                size, = struct.unpack_from("<I", e, 28)
                full = path_ + nm + ("." + ex if ex else "")
                if attr & 0x10:
                    if clus:
                        chain(clus, full + "/ (dir)")
                    subs.append((clus, full + "/"))
                else:
                    total_files += 1
                    got = chain(clus, full) if clus else 0
                    want = (size + spc * bps - 1) // (spc * bps)
                    if got != want:
                        errors.append(f"{full}: size {size} needs {want} "
                                      f"cluster(s), the chain has {got}")
            for c, p in subs:
                walk(c, p, False)

        walk(0, "/", True)

    for e in errors:
        print(f"os88disk: verify-hdd: {path}: {e}", file=sys.stderr)
    if errors:
        print(f"os88disk: verify-hdd FAILED: {path}: {len(errors)} problem(s)",
              file=sys.stderr)
        return 1
    print(f"os88disk: verify-hdd OK: {path}: {total_files} file(s), FATs "
          "agree, no loops, no cross-links, every chain matches its size")
    return 0


def verify(path: str) -> int:
    """Standalone structural fsck; exit 0 iff the image is coherent."""
    errors = []

    def bad(msg):
        errors.append(msg)

    def note(msg):
        print(f"os88disk: verify: {path}: note: {msg}", file=sys.stderr)

    try:
        with open(path, "rb") as f:
            img = f.read()
    except OSError as e:
        fail(f"cannot read {path}: {e}")
    if len(img) < SECTOR:
        fail(f"{path}: shorter than one sector")
    bs = img[0:SECTOR]

    # BPB validation, in SPEC.md's rule order (one failure = unmountable).
    if struct.unpack_from("<H", bs, 510)[0] != 0xAA55:
        fail(f"{path}: no 0xAA55 boot signature")
    if bs[0] not in (0xEB, 0xE9):
        fail(f"{path}: BS_jmpBoot[0] 0x{bs[0]:02X} not EB/E9")
    bps, = struct.unpack_from("<H", bs, 11)
    if bps != 512:
        fail(f"{path}: BPB_BytsPerSec {bps} != 512")
    spc = bs[13]
    if spc not in (1, 2, 4, 8, 16, 32, 64, 128):
        fail(f"{path}: BPB_SecPerClus {spc} not a power of two")
    rsvd, = struct.unpack_from("<H", bs, 14)
    if rsvd < 1:
        fail(f"{path}: BPB_RsvdSecCnt 0")
    nfats = bs[16]
    if nfats not in (1, 2):
        fail(f"{path}: BPB_NumFATs {nfats}")
    root_ent, = struct.unpack_from("<H", bs, 17)
    if not 1 <= root_ent <= 512 or (root_ent * 32) % SECTOR:
        fail(f"{path}: BPB_RootEntCnt {root_ent} invalid")
    tot, = struct.unpack_from("<H", bs, 19)
    if tot == 0:
        fail(f"{path}: BPB_TotSec16 0 (>= 32MB media; 16-bit LBA bound)")
    media = bs[21]
    if media != 0xF0 and not 0xF8 <= media <= 0xFF:
        fail(f"{path}: BPB_Media 0x{media:02X} not spec-legal")
    fatsz, = struct.unpack_from("<H", bs, 22)
    if not 1 <= fatsz <= 10:
        fail(f"{path}: BPB_FATSz16 {fatsz} outside 1..10 (DSK_FAT_SECS)")
    spt, = struct.unpack_from("<H", bs, 24)
    if spt not in (8, 9, 15, 18, 21, 36):
        fail(f"{path}: BPB_SecPerTrk {spt} not a real floppy geometry")
    heads, = struct.unpack_from("<H", bs, 26)
    if heads not in (1, 2):
        fail(f"{path}: BPB_NumHeads {heads}")
    if tot > spt * heads * 80:
        fail(f"{path}: BPB_TotSec16 {tot} > {spt}x{heads}x80 "
             "(CHS-unreachable sectors)")
    lay = Layout(spc, rsvd, nfats, root_ent, tot, fatsz)
    if lay.data_lba + spc > tot:
        fail(f"{path}: no data clusters (FirstDataSec {lay.data_lba})")
    if not 1 <= lay.nclus < 65525:
        fail(f"{path}: CountOfClusters {lay.nclus} out of FAT12/16 range")
    fat_bytes = ((lay.nclus + 2) * 3 + 1) // 2 if lay.fat12 \
        else (lay.nclus + 2) * 2
    if fat_bytes > fatsz * SECTOR:
        fail(f"{path}: FAT too small: needs {fat_bytes} bytes, "
             f"has {fatsz * SECTOR}")
    if len(img) < tot * SECTOR:
        fail(f"{path}: image is {len(img)} bytes; BPB claims "
             f"{tot * SECTOR}")

    fats = [img[(lay.fat_lba + i * fatsz) * SECTOR:
                (lay.fat_lba + (i + 1) * fatsz) * SECTOR]
            for i in range(nfats)]
    if nfats == 2 and fats[0] != fats[1]:
        bad("FAT1 != FAT2")
    fat = fats[0]
    eoc_min = 0xFF8 if lay.fat12 else 0xFFF8
    bad_mark = 0xFF7 if lay.fat12 else 0xFFF7

    root = img[lay.root_lba * SECTOR:lay.data_lba * SECTOR]
    used = {}                                    # cluster -> owner name
    nfiles = 0

    def walk(first, expect, name):
        """Chain walk; expect = cluster count for files, None for dirs.
        Returns the cluster list, or None if the chain is broken."""
        if first == 0:
            if expect:
                bad(f"{name}: size needs {expect} cluster(s) but first "
                    "cluster is 0")
            return []
        chain, cur = [], first
        while True:
            if not 2 <= cur <= lay.maxclus:
                bad(f"{name}: cluster {cur} outside 2..{lay.maxclus}")
                return None
            if cur in used:
                bad(f"{name}: cluster {cur} cross-linked with "
                    f"{used[cur]}")
                return None
            used[cur] = name
            chain.append(cur)
            nxt = fat_get(fat, lay.fat12, cur)
            if nxt >= eoc_min:
                break
            if nxt == bad_mark or not 2 <= nxt <= lay.maxclus:
                bad(f"{name}: cluster {cur} links to invalid {nxt:#x}")
                return None
            cur = nxt
        if expect is not None and len(chain) != expect:
            bad(f"{name}: chain has {len(chain)} cluster(s), size "
                f"implies {expect}")
        return chain

    def cluster_bytes_of(chain):
        return b"".join(
            img[(lay.data_lba + (c - 2) * lay.spc) * SECTOR:
                (lay.data_lba + (c - 2) * lay.spc + lay.spc) * SECTOR]
            for c in chain)

    def scan_dir(raw, prefix):
        """Classify one directory's raw entry bytes; recurse into
        subdirectories so their files' clusters count as used (foreign
        OSes create dirs freely - the kernel filters them, the fsck must
        still account for them)."""
        nonlocal nfiles
        for i in range(0, len(raw) - 31, 32):
            e = raw[i:i + 32]
            if e[0] == 0x00:
                break                            # end-of-directory marker
            if e[0] == 0xE5:
                continue                         # deleted
            if e[0] == 0x2E:
                continue                         # '.' / '..' self-references
            attr = e[11]
            if attr & 0x3F == 0x0F:
                continue                         # LFN entry
            if attr & 0x08:
                continue                         # volume label
            name = prefix + (
                e[0:8].decode("ascii", "replace").rstrip() + "." +
                e[8:11].decode("ascii", "replace").rstrip()).rstrip(".")
            clus_hi, = struct.unpack_from("<H", e, 20)
            clus, = struct.unpack_from("<H", e, 26)
            size, = struct.unpack_from("<I", e, 28)
            if CZ_HINT <= e[CZ_H_MARK] <= CZ_HINT + 1:  # ...either format
                # ...not FstClusHI at all: the COMPRESSION HINT (SPEC.md
                # 20.14.1) writes its low word there and its high byte at +13,
                # which is why the note below has to test the mark first - a
                # deliberate field reported as an anomaly is a note nobody
                # reads by the fourth file.
                unpacked = clus_hi | (e[CZ_H_HI] << 16)
                if unpacked <= size:
                    note(f"{name}: compression hint says it expands to "
                         f"{unpacked} and the file is {size} - the kernel "
                         "refuses that as FERR_IO")
            elif clus_hi:
                note(f"{name}: nonzero FstClusHI {clus_hi:#x} ignored "
                     "(FAT32-only field; the kernel ignores it too)")
            if attr & 0x10:
                chain = walk(clus, None, name + "/")
                if chain:
                    scan_dir(cluster_bytes_of(chain), name + "/")
                continue
            nfiles += 1
            walk(clus,
                 (size + lay.cluster_bytes - 1) // lay.cluster_bytes,
                 name)

    scan_dir(root, "")

    lost = [c for c in range(2, lay.maxclus + 1)
            if c not in used
            and fat_get(fat, lay.fat12, c) not in (0, bad_mark)]
    if lost:
        bad(f"{len(lost)} lost cluster(s): "
            + ", ".join(str(c) for c in lost[:8])
            + ("..." if len(lost) > 8 else ""))

    if errors:
        for msg in errors:
            print(f"os88disk: verify: {path}: {msg}", file=sys.stderr)
        sys.exit(1)
    print(f"os88disk: verify OK: {path} ({lay.type_name}, {lay.nclus} "
          f"clusters, {nfiles} file(s), {len(used)} cluster(s) in use)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Build or verify a FAT data floppy of .o88 packages and data files.")
    ap.add_argument("-o", "--output", metavar="OUT.img",
                    help="floppy image to write")
    ap.add_argument("--fatcap", type=int, default=0, metavar="N",
                    help="format with a FAT of at most N sectors, raising "
                         "sectors-per-cluster to get there (SPEC.md 51.0): "
                         "what lets kern_small, whose DSK_FAT_SECS is 2, "
                         "still read a 720KB or 1.44MB disk")
    ap.add_argument("--size", type=int, choices=(1440, 1200, 720, 360),
                    help="disk size in KB: 1440 (18 spt), 1200 (15 spt, "
                         "5.25\" HD), 720 or 360 (9 spt; 80 and 40 "
                         "cylinders)")
    ap.add_argument("--hdd", action="store_true",
                    help="build a bootable HARD-DISK image instead of a "
                         "floppy (SPEC.md 80.1): MBR + partition table, "
                         "boothd.bin as the volume boot record, one FAT16 "
                         "partition. Needs --mbr, --boot and --kernel; "
                         "excludes --size")
    ap.add_argument("--mbr", metavar="MBR.bin",
                    help="with --hdd: boot/mbr.asm's 446 bytes of MBR boot "
                         "code (build/mbr.bin)")
    ap.add_argument("--raw", metavar="PATH", action="append", default=[],
                    help="TEST ONLY: ship this *.O88 path verbatim, with no "
                         "package validation. A gate that needs a file the "
                         "MOUNT will type as a package and the LOADER must "
                         "then refuse - SPEC.md 19.1's size rule - needs a "
                         "*.O88 that is deliberately not a package, and "
                         "validate_o88 exists to make that unbuildable. The "
                         "path must also appear as a positional; this flag "
                         "only says which of them skips the check. Nothing "
                         "shipped uses it (--scramble's precedent)")
    ap.add_argument("--scramble", action="store_true",
                    help="fragment cluster chains round-robin (test only)")
    ap.add_argument("--verify-hdd", metavar="IMG",
                    help="structural fsck of a PARTITIONED image (a hard "
                         "disk). Finds the partition table even behind an "
                         "ST-11M-style reserved area, and allows the FAT16 "
                         "and geometry a driver volume has and a floppy "
                         "cannot")
    ap.add_argument("--verify", metavar="IMG",
                    help="structural fsck of an existing image (no build)")
    ap.add_argument("--retarget", metavar="IMG",
                    help="rewrite a --hdd image's partition-entry CHS "
                         "columns and BPB heads/spt to --geometry, into "
                         "-o (SPEC.md 80.5): the LBA layout does not move. "
                         "For a card whose ROM reports its OWN geometry "
                         "(an XTIDE CompactFlash) rather than deriving one "
                         "from the table. Needs --geometry and -o")
    ap.add_argument("--geometry", metavar="[C/]H/S",
                    help="with --retarget: heads/sectors-per-track, or a "
                         "whole C/H/S line - the cylinders are checked "
                         "against the image's size and written nowhere")
    ap.add_argument("--boot", metavar="BOOT.bin",
                    help="os8088's own 512-byte boot sector: makes this a "
                         "bootable SYSTEM disk (needs --kernel)")
    ap.add_argument("--kernel", metavar="KERNEL.bin",
                    help="the kernel, placed in the FAT reserved area so the "
                         "boot sector's raw LBA read still finds it")
    ap.add_argument("--folder", metavar="PATH", action="append", default=[],
                    help="create this folder even if no file names it "
                         "(repeatable); each component an 8.3 stem with no "
                         "extension, '/' between them for a nested one")
    ap.add_argument("--kern-small", action="store_true",
                    help="this disk is for kern_small, whose listing holds "
                         "DSK_NENT = 32 entries rather than kern_big's 64 "
                         "(SPEC.md 22.6.2). Goes with --fatcap 2")
    ap.add_argument("--deep-folders", action="store_true",
                    help="allow more than the kernel's 32-entry LISTING cap "
                         "in a subfolder (never the root): the Disk window "
                         "shows the first 31, the file API - OSAPI_FILE_FIND "
                         "and every name-taking cell - reaches them all "
                         "(SPEC.md 19, 71.3: a CP/M drive folder)")
    ap.add_argument("--dir-slots", metavar="FOLDER=N", action="append",
                    default=[],
                    help="size this folder's directory for at least N "
                         "entries ('.' and '..' included), rounded up to "
                         "whole clusters (repeatable): the kernel does not "
                         "grow a directory (SPEC.md 18.5), so a folder "
                         "programs save into ships with spare slots")
    ap.add_argument("packages", metavar="[DIR[/DIR...]:]PKG.o88", nargs="*",
                    help="package files, in directory order "
                         "(none = empty disk); the prefix is the folder to "
                         "put it in, and naming a nested one makes every "
                         "folder above it too")
    args = ap.parse_args()

    if args.retarget or args.geometry:
        if not (args.retarget and args.geometry and args.output):
            ap.error("--retarget needs --geometry and -o")
        if args.size or args.scramble or args.packages or args.folder \
                or args.dir_slots or args.verify or args.verify_hdd \
                or args.hdd:
            ap.error("--retarget takes only --geometry and -o")
        return retarget(args)
    if args.verify_hdd:
        if args.output or args.size or args.scramble or args.packages \
                or args.folder or args.dir_slots or args.verify or args.hdd:
            ap.error("--verify-hdd takes no other arguments")
        return verify_hdd(args.verify_hdd)
    if args.verify:
        if args.output or args.size or args.scramble or args.packages \
                or args.folder or args.dir_slots or args.hdd:
            ap.error("--verify takes no other arguments")
        return verify(args.verify)
    if args.size and args.hdd:
        ap.error("--size and --hdd are mutually exclusive")
    if not args.output or not (args.size or args.hdd):
        ap.error("-o and --size (or --hdd) are required to build")
    return build(args)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""os88disk: build (or verify) a FAT data floppy image from .o88 packages.

    python3 tools/os88disk.py -o OUT.img --size {1440,720,360}
                             [--folder PATH ...] [[DIR[/DIR...]:]PKG.o88 ...]
    python3 tools/os88disk.py --verify IMG
    python3 tools/os88disk.py --verify-hdd HDD.img

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

Geometries: 1440 (1.44MB, FAT12), 720 (720KB 3.5" DD, FAT12) and 360
(360KB, FAT12) are the shipped disks, and now the only ones. 720 and 360
are the SAME 9 spt / 2 heads track shape and differ only in cylinder count
(80 vs 40), which is why they share a boot sector: boot/boot.asm knows a
geometry as -DSPT/-DHEADS and nothing else, and the BPB that does differ is
written here. A 2880 (2.88MB ED, 5,698 clusters => FAT16)
geometry lived here so the kernel's FAT16 path had a positive test; it went
when DSK_FAT_SECS fell to 10 sectors, which is below the 16 a FAT has to
have to be FAT16 at all, so mount rule 10 (SPEC.md 18.2) now turns every
FAT16 volume away and there is nothing left to test with.

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
import struct
import sys

SECTOR = 512
MAX_FILES = 32                # kernel listing cap (SPEC.md section 19)
VOL_LABEL = b"OS8088APPS "    # 11 bytes, BS_VolLab == root label entry
SYS_LABEL = b"OS8088SYS  "    # ...and what a --boot/--kernel disk is called
VOL_ID = 0x88000888           # fixed serial -> deterministic images
FIXED_DATE = 0x5C21           # 2026-01-01 in FAT date encoding
FIXED_TIME = 0x0000
NAME_CHARS = frozenset(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")

# size -> (spt, heads, total sectors, sec/clus, FAT sectors, root entries,
#          media byte)   -- mirrors SPEC.md section 19's geometry table
GEOMETRY = {
    1440: (18, 2, 2880, 1, 9, 224, 0xF0),
    720: (9, 2, 1440, 2, 3, 112, 0xF9),
    360: (9, 2, 720, 2, 2, 112, 0xFD),
}

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
    if len(data) > 0xFFFF:
        fail(f"{path}: {len(data)} bytes overflows the 16-bit size field")
    image = struct.unpack_from("<H", data, 8)[0]
    if not 32 <= image <= len(data):
        fail(f"{path}: header image size {image} out of range")
    if image != len(data):
        fail(f"{path}: header image size {image} != file size {len(data)}; "
             "a v3 package has no relocation table (run os88pkg.py first)")
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
ASC_VER   = 1
ASC_HDR   = 16
ASC_ROW   = 80               # stem 8 + size 2 + cluster 2 + 4 rsvd + icon 64
ASC_ROWICO  = 16             # the icon's offset inside a row
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
ASC_DEFAULT_STEMS = (b"PAINT", b"NOTEPAD", b"TRACKER", b"ARTFUL")


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
    saves that read too.
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
                         icon, decl, key))
    # stable: the ordering key is only the association flag, so argument order
    # survives inside each half and a rebuild is byte-identical
    cand.sort(key=lambda c: c[0])
    if len(cand) > ASC_NAPP:
        print(f"os88disk: assoc: {len(cand)} packages, caching "
              f"{ASC_NAPP} - {len(cand) - ASC_NAPP} harvested the slow way")
        cand = cand[:ASC_NAPP]
    apps, rowdirs = [], []
    for _, stem, size, icon, decl, key in cand:
        idx = len(apps)
        apps.append((stem, size, icon))
        rowdirs.append(key)
        for e in decl:
            if len(exts) < ASC_NEXT:
                exts.append((e, idx))
    if not apps:
        return bytearray(), []
    buf = bytearray(ASC_HDR + ASC_ROW * len(apps) + 4 * len(exts))
    buf[0:6] = ASC_MAGIC
    buf[6], buf[7], buf[8] = ASC_VER, len(apps), len(exts)
    for i, (stem, size, icon) in enumerate(apps):
        o = ASC_HDR + i * ASC_ROW
        buf[o:o + 8] = stem
        struct.pack_into("<H", buf, o + 8, size)
        buf[o + ASC_ROWICO:o + ASC_ROWICO + 64] = icon
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


def dirent(name11: bytes, attr: int, clus: int, size: int) -> bytes:
    e = bytearray(32)
    e[0:11] = name11
    e[11] = attr
    # NT byte 0, CrtTimeTenth 0; every timestamp fixed for determinism.
    struct.pack_into("<HHH", e, 14, FIXED_TIME, FIXED_DATE, FIXED_DATE)
    struct.pack_into("<HHHHI", e, 20, 0, FIXED_TIME, FIXED_DATE, clus, size)
    return bytes(e)


def boot_sector(spt, heads, tot, spc, fatsz, root_ent, media,
                lay: Layout, code: bytes = None, label: bytes = None) -> bytes:
    """One BPB, two uses. `code` is os8088's own 512-byte boot sector: its
    first three bytes are already EB 3C 90 and bytes 62.. are its loader, so
    the BPB is written into the hole between them and everything else is left
    exactly as nasm assembled it. Without `code` the sector carries the
    not-bootable stub instead."""
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
    # BPB_HiddSec, BPB_TotSec32 stay 0; BS_DrvNum 0; BS_Reserved1 0.
    bs[38] = 0x29                               # BS_BootSig
    struct.pack_into("<I", bs, 39, VOL_ID)      # BS_VolID
    bs[43:54] = label or VOL_LABEL              # BS_VolLab
    bs[54:62] = b"FAT12   " if lay.fat12 else b"FAT16   "
    if not code:
        bs[62:62 + len(BOOT_STUB)] = BOOT_STUB
    bs[510:512] = b"\x55\xAA"
    return bytes(bs)


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
    spt, heads, tot, spc, fatsz, root_ent, media = GEOMETRY[args.size]

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

    for arg in args.packages:
        folder, path = split_spec(arg)
        key = folder_key(folder) if folder else ""
        ensure(key)
        name11 = name83(path, seen[key])
        # Only *.O88 entries are packages; anything else ships as data, so a
        # disk can carry a module, a picture or a text file next to the
        # programs that read it (SPEC.md 24). The 8.3 name, the per-directory
        # cap and the duplicate check all apply the same either way.
        if path.lower().endswith(".o88"):
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

    for key in dirs:
        shown = len(kids[key]) + sum(
            1 for n, _, _ in groups[key]
            if not sys_attr(n, bool(boot)) & A_HIDDEN)
        if shown > MAX_FILES and not args.deep_folders:
            fail(f"{shown} listed entries in folder {key}; the kernel "
                 f"lists at most {MAX_FILES} per directory (--deep-folders "
                 f"if this folder is a data store the file API walks, not "
                 f"one the Disk window shows)")
    # MAX_FILES is a DISPLAY cap, so only what the kernel would list counts
    # against it: a hidden system file (SPEC.md 19.6) never takes a listing
    # slot. It still takes a directory slot, which is the second check.
    shown = len(root_dirs) + sum(1 for n, _, _ in root_files
                                 if not sys_attr(n, bool(boot)) & A_HIDDEN)
    if shown > MAX_FILES:
        fail(f"{shown} listed root entries; the kernel lists "
             f"at most {MAX_FILES} per directory")

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
                                       chains[at + i][0], len(body))
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
            name11, sys_attr(name11, boot), chain[0], len(body))
        slot += 1

    image = bytearray(boot_sector(spt, heads, tot, spc, fatsz, root_ent,
                                  media, lay, boot, label))
    image += fat.buf + fat.buf                   # FAT2 = FAT1
    image += root
    image += data_area
    assert len(image) == tot * SECTOR

    try:
        with open(args.output, "wb") as f:
            f.write(image)
    except OSError as e:
        fail(f"cannot write {args.output}: {e}")

    print(f"os88disk: {args.output} ({args.size}KB, {spt} spt, "
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
            if clus_hi:
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
    ap.add_argument("--size", type=int, choices=(1440, 720, 360),
                    help="disk size in KB: 1440 (18 spt), 720 or 360 (9 spt; "
                         "80 and 40 cylinders)")
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

    if args.verify_hdd:
        if args.output or args.size or args.scramble or args.packages \
                or args.folder or args.dir_slots or args.verify:
            ap.error("--verify-hdd takes no other arguments")
        return verify_hdd(args.verify_hdd)
    if args.verify:
        if args.output or args.size or args.scramble or args.packages \
                or args.folder or args.dir_slots:
            ap.error("--verify takes no other arguments")
        return verify(args.verify)
    if not args.output or not args.size:
        ap.error("-o and --size are required to build")
    return build(args)


if __name__ == "__main__":
    sys.exit(main())

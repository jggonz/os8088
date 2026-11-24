#!/usr/bin/env python3
"""os88zip: build Iomega Zip disk images on the host, and read files back out.

    python3 tools/os88zip.py mk OUT.img [--kind ibm|super|zip250|zip250super]
                                        [--label NAME] [--file DEST=HOSTFILE ...]
    python3 tools/os88zip.py ls IMG [PATH] [-r]     list a directory
    python3 tools/os88zip.py get IMG PATH [OUT]     extract a file (stdout if no OUT)
    python3 tools/os88zip.py put IMG PATH HOSTFILE  write a file into the image
    python3 tools/os88zip.py mkdir IMG PATH
    python3 tools/os88zip.py check IMG              fsck: exit 0 iff coherent
    python3 tools/os88zip.py info IMG               kind, partition table, BPB
    python3 tools/os88zip.py clone SRC DST          copy, keeping the holes
    python3 tools/os88zip.py qa DIR [--only NAME]   the QA reference disks

SPEC.md 138.8. This is the REFERENCE for ZIP.DRV's QA (docs/plans/ZIP-PLAN.md):
what the guest wrote is read back out of the host file by `Vol` here, and what
the guest is made to read was written by `Vol` here - two different programs
on either side of the driver, neither of them the driver.

THE FOUR KINDS (SPEC.md 138.4):

  ibm         196,608 sectors (96 MB, the Zip 100): an MBR at sector 0 with ONE
              partition, in ENTRY 4 (type 06h, active, starting at sector 32 -
              cylinder 0 head 1, in Iomega's 64-head 32-sector geometry), a
              FAT16 volume inside it. This is how IBM-format Zip disks come
              from the factory, and entry 4 is the point: a scan that stops at
              entry 1 finds nothing.
  super       the same 196,608 sectors with the FAT16 BPB at sector 0 and no
              partition table at all (a "superfloppy", how an Apple or a
              Windows "format as floppy" leaves one)
  zip250      489,532 sectors (the Zip 250), partitioned like `ibm`
  zip250super the Zip 250 as a superfloppy

An image is a SPARSE file: only the sectors that carry something are written,
so a 100 MB disk is a few hundred KB on the host. The FAT code underneath is
tools/os88disk.py's (`Layout`, `Fat`, `fat_get`, `boot_sector`, `dirent`,
`hdd_layout`, `hdd_chs`) - this file adds what a floppy builder does not need:
a partition table, FAT16 volumes written AND read in place, subdirectories,
and an fsck of the result. Timestamps are os88disk's fixed ones, so an image
is the same bytes every time it is built.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import os88disk as D                                        # noqa: E402

SECTOR = 512
HEADS, SPT = 64, 32                 # Iomega's geometry: 2,048 sectors a cylinder
PART_START = 32                     # the IBM-format partition's first sector
ROOT_ENT = 512

KINDS = {
    "ibm":         {"total": 196608, "mbr": True},
    "super":       {"total": 196608, "mbr": False},
    "zip250":      {"total": 489532, "mbr": True},
    "zip250super": {"total": 489532, "mbr": False},
}
FAT16_TYPES = (0x01, 0x04, 0x06, 0x0E)      # what the driver accepts (138.4)
A_RO, A_HID, A_SYS, A_LABEL, A_DIR, A_ARCH = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20
EOC = 0xFFFF                                # end of chain written by this tool


class ZipError(Exception):
    pass


def _fail(msg):
    raise ZipError(msg)


# --- names -------------------------------------------------------------------

def _name11(part):
    """'readme.txt' -> b'README  TXT' (8.3, upper case; refuses what FAT cannot
    hold rather than inventing a short name)."""
    stem, dot, ext = part.upper().partition(".")
    if not 1 <= len(stem) <= 8 or len(ext) > 3 or "." in ext:
        _fail("'%s' is not an 8.3 name" % part)
    ok = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-!#$%&'()@^`{}~")
    for ch in stem + ext:
        if ch not in ok:
            _fail("'%s' has a character FAT short names do not allow" % part)
    return stem.encode().ljust(8) + ext.encode().ljust(3)


def _show11(n):
    stem = n[:8].decode("ascii", "replace").rstrip()
    ext = n[8:11].decode("ascii", "replace").rstrip()
    return stem + ("." + ext if ext else "")


def _split(path):
    return [p for p in path.replace("\\", "/").split("/") if p]


# --- the image file: sectors, sparse -----------------------------------------

class Img:
    """A disk image opened for sector access. Writes of an all-zero sector to a
    region the file does not hold yet are skipped, which is what keeps it
    sparse."""

    def __init__(self, path, create_sectors=None):
        if create_sectors is not None:
            self.f = open(path, "w+b")
            self.f.truncate(create_sectors * SECTOR)
        else:
            self.f = open(path, "r+b")
        self.f.seek(0, 2)
        self.sectors = self.f.tell() // SECTOR
        self.path = path

    def read(self, lba, n=1):
        self.f.seek(lba * SECTOR)
        d = self.f.read(n * SECTOR)
        return d + bytes(n * SECTOR - len(d))

    def write(self, lba, data):
        if len(data) % SECTOR:
            data = data + bytes(SECTOR - len(data) % SECTOR)
        if lba + len(data) // SECTOR > self.sectors:
            _fail("write past the end of the disk (sector %d)" % lba)
        # holes stay holes
        for i in range(0, len(data), SECTOR):
            blk = data[i:i + SECTOR]
            if not any(blk):
                cur = self.read(lba + i // SECTOR)
                if not any(cur):
                    continue
            self.f.seek((lba + i // SECTOR) * SECTOR)
            self.f.write(blk)

    def close(self):
        self.f.flush()
        self.f.close()


# --- a FAT16 volume ------------------------------------------------------------

class Vol:
    """A FAT16 volume inside an image: format it, or open what is there."""

    def __init__(self, img, base, lay, media, label, serial):
        self.img, self.base, self.lay = img, base, lay
        self.media, self.label, self.serial = media, label, serial
        self.spc = lay.spc
        self.cbytes = lay.cluster_bytes
        self.fat = None
        self.dirty = False
        self.next_free = 2

    # -- construct ---------------------------------------------------------

    @classmethod
    def format(cls, img, base, nsec, label=b"ZIP        ", heads=HEADS, spt=SPT,
               serial=0x20261124):
        lay = D.hdd_layout(nsec)
        v = cls(img, base, lay, 0xF8, label, serial)
        bs = D.boot_sector(spt, heads, nsec, lay.spc, lay.fatsz, ROOT_ENT,
                           0xF8, lay, label=label, hidden=base, drvnum=0x80,
                           volid=serial)
        img.write(base, bs)
        v.fat = bytearray(lay.fatsz * SECTOR)
        struct.pack_into("<HH", v.fat, 0, 0xFF00 | 0xF8, 0xFFFF)
        v.dirty = True
        # the volume label, the first entry of the root
        v._put_dirent_at(base + lay.root_lba, 0, label, A_LABEL, 0, 0)
        v.flush()
        return v

    @classmethod
    def open(cls, img, base=0):
        bs = img.read(base)
        if bs[510:512] != b"\x55\xAA":
            _fail("no 55AA signature in the volume's first sector")
        bps, = struct.unpack_from("<H", bs, 11)
        spc, rsvd, nfats = bs[13], struct.unpack_from("<H", bs, 14)[0], bs[16]
        root_ent, tot16 = struct.unpack_from("<HH", bs, 17)
        fatsz, = struct.unpack_from("<H", bs, 22)
        tot32, = struct.unpack_from("<I", bs, 32)
        tot = tot16 or tot32
        if bps != SECTOR or spc == 0 or spc & (spc - 1) or nfats not in (1, 2) \
                or not fatsz or not tot:
            _fail("not a FAT volume (BPB: bps %d spc %d fats %d fatsz %d)"
                  % (bps, spc, nfats, fatsz))
        lay = D.Layout(spc, rsvd, nfats, root_ent, tot, fatsz)
        if lay.fat12:
            _fail("a FAT12 volume: this tool reads FAT16 (a Zip disk's)")
        media = bs[21]
        label = bs[43:54]
        serial, = struct.unpack_from("<I", bs, 39)
        v = cls(img, base, lay, media, label, serial)
        v.fat = bytearray(img.read(base + lay.fat_lba, fatsz))
        return v

    # -- the FAT -------------------------------------------------------------

    def get(self, n):
        return D.fat_get(self.fat, False, n)

    def set(self, n, val):
        struct.pack_into("<H", self.fat, n * 2, val)
        self.dirty = True

    def chain(self, first):
        out, seen = [], set()
        c = first
        while 2 <= c <= self.lay.maxclus:
            if c in seen:
                _fail("a loop in a cluster chain at %d" % c)
            seen.add(c)
            out.append(c)
            c = self.get(c)
            if c >= 0xFFF8:
                return out
            if c < 2 or c == 0xFFF7:
                _fail("a chain runs into %#06x" % c)
        _fail("a chain leaves the volume at %#06x" % c)

    def alloc(self, n):
        out = []
        c = self.next_free
        top = self.lay.maxclus
        scanned = 0
        while len(out) < n:
            if scanned > top:
                for x in out:
                    self.set(x, 0)
                _fail("the disk is full")
            if c > top:
                c = 2
            if self.get(c) == 0 and c not in out:
                out.append(c)
            c += 1
            scanned += 1
        self.next_free = c
        for a, b in zip(out, out[1:]):
            self.set(a, b)
        self.set(out[-1], EOC)
        return out

    def free_chain(self, first):
        for c in self.chain(first):
            self.set(c, 0)
        self.next_free = 2

    def free_clusters(self):
        return sum(1 for c in range(2, self.lay.maxclus + 1) if self.get(c) == 0)

    def flush(self):
        if not self.dirty:
            return
        for i in range(self.lay.nfats):
            self.img.write(self.base + self.lay.fat_lba + i * self.lay.fatsz,
                           bytes(self.fat))
        self.dirty = False

    # -- clusters -----------------------------------------------------------

    def clba(self, c):
        return self.base + self.lay.data_lba + (c - 2) * self.spc

    def read_clusters(self, chain):
        return b"".join(self.img.read(self.clba(c), self.spc) for c in chain)

    def write_data(self, chain, data):
        data = data + bytes(len(chain) * self.cbytes - len(data))
        i = 0
        while i < len(chain):
            j = i
            while j + 1 < len(chain) and chain[j + 1] == chain[j] + 1:
                j += 1
            self.img.write(self.clba(chain[i]),
                           data[i * self.cbytes:(j + 1) * self.cbytes])
            i = j + 1

    # -- directories --------------------------------------------------------

    def _dir_sectors(self, first):
        """[(lba, ...)] of a directory: the root's fixed run, or a chain."""
        if first == 0:
            return [self.base + self.lay.root_lba + i
                    for i in range(self.lay.root_secs)]
        out = []
        for c in self.chain(first):
            out += [self.clba(c) + i for i in range(self.spc)]
        return out

    def entries(self, first=0):
        """Every live entry of a directory: dicts with name11, name, attr,
        clus, size and (lba, off) where it lives."""
        out = []
        for lba in self._dir_sectors(first):
            sec = self.img.read(lba)
            for off in range(0, SECTOR, 32):
                e = sec[off:off + 32]
                if e[0] == 0:
                    return out
                if e[0] == 0xE5 or e[11] & 0x0F == 0x0F:
                    continue
                clus, size = struct.unpack_from("<H", e, 26)[0], \
                    struct.unpack_from("<I", e, 28)[0]
                hi, = struct.unpack_from("<H", e, 20)
                out.append({"name11": bytes(e[:11]), "name": _show11(e[:11]),
                            "attr": e[11], "clus": clus | (hi << 16),
                            "size": size, "lba": lba, "off": off})
        return out

    def _put_dirent_at(self, lba, off, name11, attr, clus, size):
        sec = bytearray(self.img.read(lba))
        sec[off:off + 32] = D.dirent(name11, attr, clus, size)
        self.img.write(lba, bytes(sec))

    def _find_slot(self, first):
        """The (lba, off) of a free slot in a directory, growing a subdirectory
        by one cluster when it is full."""
        for lba in self._dir_sectors(first):
            sec = self.img.read(lba)
            for off in range(0, SECTOR, 32):
                if sec[off] in (0, 0xE5):
                    return lba, off
        if first == 0:
            _fail("the root directory is full (%d entries)" % ROOT_ENT)
        last = self.chain(first)[-1]
        new, = self.alloc(1)
        self.set(last, new)
        self.write_data([new], b"")
        return self._find_slot(first)

    def lookup(self, parts):
        """The entry for a path (a list of names), or None. A path with no
        parts is the root and answers {'clus': 0, 'attr': A_DIR}."""
        cur = {"clus": 0, "attr": A_DIR, "name": "", "size": 0}
        for p in parts:
            if not cur["attr"] & A_DIR:
                return None
            want = _name11(p)
            for e in self.entries(cur["clus"]):
                if e["name11"] == want and not e["attr"] & A_LABEL:
                    cur = e
                    break
            else:
                return None
        return cur

    # -- files ----------------------------------------------------------------

    def read_file(self, path):
        e = self.lookup(_split(path))
        if e is None:
            _fail("%s: no such file" % path)
        if e["attr"] & A_DIR:
            _fail("%s: is a directory" % path)
        if e["size"] == 0:
            return b""
        data = self.read_clusters(self.chain(e["clus"]))
        return data[:e["size"]]

    def mkdir(self, path):
        parts = _split(path)
        if not parts:
            return
        parent = self.lookup(parts[:-1])
        if parent is None or not parent["attr"] & A_DIR:
            _fail("%s: no such directory" % "/".join(parts[:-1]))
        have = self.lookup(parts)
        if have is not None:
            if not have["attr"] & A_DIR:
                _fail("%s: exists and is a file" % path)
            return
        name = _name11(parts[-1])
        c, = self.alloc(1)
        pc = parent["clus"]
        blank = bytearray(self.cbytes)
        blank[0:32] = D.dirent(b".          ", A_DIR, c, 0)
        blank[32:64] = D.dirent(b"..         ", A_DIR, pc, 0)
        self.write_data([c], bytes(blank))
        lba, off = self._find_slot(pc)
        self._put_dirent_at(lba, off, name, A_DIR, c, 0)
        self.flush()

    def write_file(self, path, data):
        parts = _split(path)
        if not parts:
            _fail("no file name")
        for i in range(1, len(parts)):          # make the folders on the way
            self.mkdir("/".join(parts[:i]))
        parent = self.lookup(parts[:-1])
        name = _name11(parts[-1])
        old = self.lookup(parts)
        if old is not None:
            if old["attr"] & A_DIR:
                _fail("%s: is a directory" % path)
            if old["size"]:
                self.free_chain(old["clus"])
            slot = (old["lba"], old["off"])
        else:
            slot = self._find_slot(parent["clus"])
        first = 0
        if data:
            n = -(-len(data) // self.cbytes)
            chain = self.alloc(n)
            self.write_data(chain, data)
            first = chain[0]
        self._put_dirent_at(slot[0], slot[1], name, A_ARCH, first, len(data))
        self.flush()

    def walk(self, first=0, prefix=""):
        for e in self.entries(first):
            if e["attr"] & A_LABEL or e["name"] in (".", ".."):
                continue
            yield prefix + e["name"], e
            if e["attr"] & A_DIR:
                yield from self.walk(e["clus"], prefix + e["name"] + "/")

    # -- fsck -----------------------------------------------------------------

    def check(self):
        """A list of problems; empty means coherent."""
        bad = []
        lay = self.lay
        for i in range(1, lay.nfats):
            other = self.img.read(self.base + lay.fat_lba + i * lay.fatsz,
                                  lay.fatsz)
            if other != bytes(self.fat):
                bad.append("FAT copy %d differs from copy 0" % (i + 1))
        if self.get(0) & 0xFF != self.media:
            bad.append("FAT[0] media byte %#x != BPB %#x"
                       % (self.get(0) & 0xFF, self.media))
        used = {}

        def claim(first, what, dirp, nbytes):
            try:
                ch = self.chain(first)
            except ZipError as e:
                bad.append("%s: %s" % (what, e))
                return []
            for c in ch:
                if c in used:
                    bad.append("%s: cluster %d also belongs to %s"
                               % (what, c, used[c]))
                used[c] = what
            if not dirp:
                need = -(-nbytes // self.cbytes)
                if len(ch) != need:
                    bad.append("%s: %d bytes need %d clusters, chain has %d"
                               % (what, nbytes, need, len(ch)))
            return ch

        def scan(first, prefix, parent):
            try:
                ents = self.entries(first)
            except ZipError as e:
                bad.append("%s: %s" % (prefix or "/", e))
                return
            names = set()
            for e in ents:
                if e["attr"] & A_LABEL:
                    continue
                full = prefix + e["name"]
                if e["name"] in (".", ".."):
                    want = first if e["name"] == "." else parent
                    if e["clus"] != want:
                        bad.append("%s: points at cluster %d, want %d"
                                   % (full, e["clus"], want))
                    continue
                if e["name11"] in names:
                    bad.append("%s: duplicate name" % full)
                names.add(e["name11"])
                if e["attr"] & A_DIR:
                    claim(e["clus"], full + "/", True, 0)
                    scan(e["clus"], full + "/", first)
                elif e["size"] == 0:
                    if e["clus"]:
                        bad.append("%s: empty file with cluster %d"
                                   % (full, e["clus"]))
                else:
                    claim(e["clus"], full, False, e["size"])
        scan(0, "", 0)
        for c in range(2, lay.maxclus + 1):
            if self.get(c) not in (0, 0xFFF7) and c not in used:
                bad.append("cluster %d is allocated but belongs to nothing"
                           % c)
                break
        return bad


# --- images ------------------------------------------------------------------

def _chs_pair(lba):
    return D.hdd_chs(min(lba, 1023 * HEADS * SPT + HEADS * SPT - 1), HEADS, SPT)


def mbr_sector(start, length):
    """An MBR whose ONLY partition is entry 4 (type 06h, active)."""
    m = bytearray(SECTOR)
    m[0:3] = b"\xEB\x3C\x90"
    msg = b"Not a bootable disk.\r\n"
    m[3:3 + len(msg)] = msg
    ent = bytearray(16)
    ent[0] = 0x80
    ent[1:4] = _chs_pair(start)
    ent[4] = 0x06
    ent[5:8] = _chs_pair(start + length - 1)
    struct.pack_into("<II", ent, 8, start, length)
    m[446 + 3 * 16:446 + 4 * 16] = ent
    m[510:512] = b"\x55\xAA"
    return bytes(m)


def make(path, kind="ibm", label="ZIP", serial=0x20261124):
    if kind not in KINDS:
        _fail("kind %r: one of %s" % (kind, ", ".join(KINDS)))
    k = KINDS[kind]
    img = Img(path, create_sectors=k["total"])
    lab = label.upper().encode().ljust(11)[:11]
    if k["mbr"]:
        img.write(0, mbr_sector(PART_START, k["total"] - PART_START))
        return Vol.format(img, PART_START, k["total"] - PART_START, lab,
                          serial=serial)
    return Vol.format(img, 0, k["total"], lab, serial=serial)


def open_image(path):
    """(Vol, kind) for an image: a FAT boot sector at sector 0 is a
    superfloppy, else the first partition entry (1..4) of a FAT type - the
    driver's own order (SPEC.md 138.4)."""
    img = Img(path)
    s0 = img.read(0)
    if s0[510:512] != b"\x55\xAA":
        _fail("%s: no 55AA signature in sector 0" % path)
    if s0[0] in (0xEB, 0xE9) and struct.unpack_from("<H", s0, 11)[0] == SECTOR \
            and s0[13] and not s0[13] & (s0[13] - 1) and s0[16] in (1, 2) \
            and struct.unpack_from("<H", s0, 14)[0] and struct.unpack_from("<H", s0, 22)[0]:
        return Vol.open(img, 0), "super"
    for i in range(4):
        e = s0[446 + i * 16:462 + i * 16]
        if e[4] in FAT16_TYPES:
            start, = struct.unpack_from("<I", e, 8)
            return Vol.open(img, start), "mbr(entry %d)" % (i + 1)
    _fail("%s: neither a FAT boot sector nor a FAT partition" % path)


def clone(src, dst):
    """Copy an image keeping its holes (a Zip image is 100 MB of mostly
    nothing, and a row copies one per run)."""
    n = 0
    with open(src, "rb") as a, open(dst, "wb") as b:
        a.seek(0, 2)
        size = a.tell()
        b.truncate(size)
        a.seek(0)
        off = 0
        step = 1 << 20
        while off < size:
            blk = a.read(step)
            if not blk:
                break
            for i in range(0, len(blk), 4096):
                part = blk[i:i + 4096]
                if any(part):
                    b.seek(off + i)
                    b.write(part)
                    n += len(part)
            off += len(blk)
    return n


# --- the QA reference disks ----------------------------------------------------

def pattern(n, seed):
    """n deterministic bytes (a 32-bit LCG: not a multiple of any sector or
    cluster size when n is not, which is the point)."""
    out = bytearray(n)
    x = seed & 0xFFFFFFFF
    for i in range(n):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        out[i] = (x >> 16) & 0xFF
    return bytes(out)


BIG200 = 200000
README = (b"This disk was written on the host by tools/os88zip.py.\r\n"
          b"If you can read this in os8088, the Zip drive works.\r\n")
HELLO = b"Hello from the Zip disk.\r\n"
NOTE = b"A file one folder down.\r\n"
OTHER = b"This is the OTHER disk: its FAT is not the first disk's.\r\n"

QA = {
    # name: (kind, label, {path: bytes})
    "zip100.img": ("ibm", "ZIP100", {
        "README.TXT": README, "HELLO.TXT": HELLO, "DOCS/NOTE.TXT": NOTE,
        "BIG200.BIN": pattern(BIG200, 0x20200)}),
    "zip100b.img": ("ibm", "ZIPOTHER", {
        "OTHER.TXT": OTHER, "B_ONLY.TXT": b"only on disk B\r\n"}),
    "zip100blank.img": ("ibm", "ZIPBLANK", {}),
    "zip100super.img": ("super", "ZIPSUPER", {
        "README.TXT": README, "HELLO.TXT": HELLO}),
    "zip250.img": ("zip250", "ZIP250", {
        "README.TXT": README, "BIG200.BIN": pattern(BIG200, 0x20250)}),
}


def build_qa(outdir, only=None):
    os.makedirs(outdir, exist_ok=True)
    made = []
    for name, (kind, label, files) in QA.items():
        if only and name != only:
            continue
        final = os.path.join(outdir, name)
        tmp = final + ".tmp"
        v = make(tmp, kind, label)
        for p, data in files.items():
            v.write_file(p, data)
        bad = v.check()
        v.img.close()
        if bad:
            os.remove(tmp)
            _fail("%s: the fresh image fails its own fsck: %s" % (name, bad))
        os.replace(tmp, final)
        made.append(final)
    if only and not made:
        _fail("%s: not a QA image (%s)" % (only, ", ".join(QA)))
    return made


# --- the command line ----------------------------------------------------------

def _human(n):
    return "%d" % n


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="op", required=True)
    p = sub.add_parser("mk")
    p.add_argument("out")
    p.add_argument("--kind", default="ibm", choices=sorted(KINDS))
    p.add_argument("--label", default="ZIP")
    p.add_argument("--file", action="append", default=[], metavar="DEST=HOST")
    p = sub.add_parser("ls")
    p.add_argument("img")
    p.add_argument("path", nargs="?", default="")
    p.add_argument("-r", action="store_true")
    p = sub.add_parser("get")
    p.add_argument("img")
    p.add_argument("path")
    p.add_argument("out", nargs="?")
    p = sub.add_parser("put")
    p.add_argument("img")
    p.add_argument("path")
    p.add_argument("host")
    p = sub.add_parser("mkdir")
    p.add_argument("img")
    p.add_argument("path")
    p = sub.add_parser("check")
    p.add_argument("img")
    p = sub.add_parser("info")
    p.add_argument("img")
    p = sub.add_parser("clone")
    p.add_argument("src")
    p.add_argument("dst")
    p = sub.add_parser("qa")
    p.add_argument("dir")
    p.add_argument("--only")
    a = ap.parse_args(argv)

    try:
        if a.op == "mk":
            v = make(a.out, a.kind, a.label)
            for spec in a.file:
                dest, _, host = spec.partition("=")
                with open(host, "rb") as f:
                    v.write_file(dest, f.read())
            bad = v.check()
            v.img.close()
            if bad:
                _fail("fsck: " + "; ".join(bad))
            print("%s: %s, %d sectors" % (a.out, a.kind, KINDS[a.kind]["total"]))
        elif a.op == "qa":
            for p in build_qa(a.dir, a.only):
                print(p)
        elif a.op == "clone":
            print("%s -> %s: %d bytes of data" % (a.src, a.dst, clone(a.src, a.dst)))
        else:
            v, kind = open_image(a.img)
            if a.op == "ls":
                e = v.lookup(_split(a.path))
                if e is None:
                    _fail("%s: no such directory" % a.path)
                if not e["attr"] & A_DIR:
                    print("%12d  %s" % (e["size"], e["name"]))
                elif a.r:
                    base = "/".join(_split(a.path))
                    for name, e2 in v.walk(e["clus"], base + "/" if base else ""):
                        print("%12s  %s" % ("<DIR>" if e2["attr"] & A_DIR
                                            else e2["size"], name))
                else:
                    for e2 in v.entries(e["clus"]):
                        if e2["attr"] & A_LABEL or e2["name"] in (".", ".."):
                            continue
                        print("%12s  %s" % ("<DIR>" if e2["attr"] & A_DIR
                                            else e2["size"], e2["name"]))
            elif a.op == "get":
                data = v.read_file(a.path)
                if a.out:
                    with open(a.out, "wb") as f:
                        f.write(data)
                    print("%s: %d bytes" % (a.out, len(data)))
                else:
                    sys.stdout.buffer.write(data)
            elif a.op == "put":
                with open(a.host, "rb") as f:
                    v.write_file(a.path, f.read())
                bad = v.check()
                if bad:
                    _fail("fsck: " + "; ".join(bad))
            elif a.op == "mkdir":
                v.mkdir(a.path)
            elif a.op == "check":
                bad = v.check()
                for b in bad:
                    print("os88zip: check: %s: %s" % (a.img, b), file=sys.stderr)
                if bad:
                    return 1
                print("%s: ok (%s, %d clusters free of %d)"
                      % (a.img, kind, v.free_clusters(), v.lay.nclus))
            elif a.op == "info":
                s0 = v.img.read(0)
                print("image   %s: %d sectors" % (a.img, v.img.sectors))
                print("layout  %s" % kind)
                for i in range(4):
                    e = s0[446 + i * 16:462 + i * 16]
                    if kind != "super" and any(e):
                        st, ln = struct.unpack_from("<II", e, 8)
                        print("entry %d type %02X%s start %d length %d"
                              % (i + 1, e[4], " active" if e[0] == 0x80 else "",
                                 st, ln))
                lay = v.lay
                print("volume  %s at sector %d: %d sectors, %d per cluster, "
                      "FAT %d sectors x%d, root %d entries, %d clusters"
                      % (lay.type_name, v.base, lay.tot, lay.spc, lay.fatsz,
                         lay.nfats, lay.root_ent, lay.nclus))
    except ZipError as e:
        print("os88zip: error: %s" % e, file=sys.stderr)
        return 1
    except OSError as e:
        print("os88zip: error: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

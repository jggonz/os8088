#!/usr/bin/env python3
"""tools/os88zip.py, the Zip disk image builder and reader (SPEC.md 138.8).

    python3 tests/unit/t_zipimg.py

ZIP.DRV's QA reads what this tool wrote (claim 2 of docs/plans/ZIP-PLAN.md) and
checks what the guest wrote by reading it back out through this tool (claim 3),
so a mistake HERE is a mistake in the reference, and it surfaces as a guest
that "fails" on a perfectly good disk or "passes" on a corrupt one. Five things
go wrong silently:

  * THE PARTITION IS IN THE WRONG ENTRY. An IBM-format Zip disk carries its
    FAT16 partition in ENTRY 4 (SPEC.md 138.4); a builder that puts it in
    entry 1 gives the driver's "scan all four" nothing to prove.
  * A GEOMETRY IS WRONG: 196,608 and 489,532 sectors, the BPB's hidden-sector
    count equal to the partition start, FAT16 (not FAT12) and not so many
    clusters that it is FAT32.
  * THE FILE IS NOT SPARSE, and a row copying a 250 MB image per run is a row
    that never finishes.
  * THE READER AGREES WITH THE WRITER AND WITH NOTHING ELSE. So the file is read
    back here by a SECOND reader, written below from the FAT spec and sharing no
    code with tools/os88zip.py (nor os88disk's), and a third, independent,
    host fsck can be asked once by hand (the PR notes the result).
  * THE FSCK CANNOT FAIL. Every defect it claims to find is injected and must be
    found (a positive control, docs/WRITING-TESTS.md 1).

Break it on purpose: move the partition to entry 1 in mbr_sector() and the
layout check fails; drop the second FAT's write in Vol.flush() and `check`'s
copy comparison does.
"""
import hashlib
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, eq, done                        # noqa: E402
import os88zip as Z                                        # noqa: E402

SEC = 512


# --- the second reader: raw bytes, the FAT spec, nothing from the tool --------

class Raw(object):
    """Read a FAT16 file out of an image knowing nothing about os88zip."""

    def __init__(self, path):
        self.f = open(path, "rb")
        s0 = self.sector(0)
        assert s0[510:512] == b"\x55\xAA"
        base = 0
        if not (struct.unpack_from("<H", s0, 11)[0] == 512 and s0[16] in (1, 2)
                and s0[0] in (0xEB, 0xE9)):
            base = None
            for i in range(4):                      # the order the driver scans
                e = s0[446 + 16 * i:462 + 16 * i]
                if e[4] in (1, 4, 6, 14):
                    base = struct.unpack_from("<I", e, 8)[0]
                    break
            assert base is not None, "no FAT partition"
        self.base = base
        b = self.sector(base)
        self.spc = b[13]
        rsvd = struct.unpack_from("<H", b, 14)[0]
        nfat = b[16]
        rootn = struct.unpack_from("<H", b, 17)[0]
        fatsz = struct.unpack_from("<H", b, 22)[0]
        self.fat = b"".join(self.sector(base + rsvd + i) for i in range(fatsz))
        self.root = base + rsvd + nfat * fatsz
        self.data = self.root + (rootn * 32 + 511) // 512

    def sector(self, n):
        self.f.seek(n * SEC)
        d = self.f.read(SEC)
        return d + bytes(SEC - len(d))

    def clus(self, c):
        out = b""
        for i in range(self.spc):
            out += self.sector(self.data + (c - 2) * self.spc + i)
        return out

    def nextc(self, c):
        return struct.unpack_from("<H", self.fat, 2 * c)[0]

    def dirents(self, c):
        raw = b""
        if c == 0:
            n = (self.data - self.root)
            raw = b"".join(self.sector(self.root + i) for i in range(n))
        else:
            while c < 0xFFF8:
                raw += self.clus(c)
                c = self.nextc(c)
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 8:
                continue
            nm = e[:8].rstrip().decode() + ("." + e[8:11].rstrip().decode()
                                            if e[8:11].strip() else "")
            yield nm, e[11], struct.unpack_from("<H", e, 26)[0], \
                struct.unpack_from("<I", e, 28)[0]

    def read(self, path):
        c = 0
        parts = path.split("/")
        for p in parts[:-1]:
            for nm, at, cl, sz in self.dirents(c):
                if nm == p.upper() and at & 0x10:
                    c = cl
                    break
            else:
                raise KeyError(path)
        for nm, at, cl, sz in self.dirents(c):
            if nm == parts[-1].upper() and not at & 0x10:
                out = b""
                while sz and cl < 0xFFF8:
                    out += self.clus(cl)
                    cl = self.nextc(cl)
                return out[:sz]
        raise KeyError(path)


def sparse_ok(path):
    st = os.stat(path)
    if not hasattr(st, "st_blocks"):
        return True
    return st.st_blocks * 512 < 8 * 1024 * 1024


def main():
    tmp = tempfile.mkdtemp(prefix="t_zipimg")

    def p(name):
        return os.path.join(tmp, name)

    # ---- 1. the layouts -----------------------------------------------------
    for kind, k in Z.KINDS.items():
        v = Z.make(p(kind + ".img"), kind)
        v.img.close()
        size = os.path.getsize(p(kind + ".img"))
        eq(size, k["total"] * SEC, "%s: the image is %d sectors" % (kind, k["total"]),
           "a Zip 100 is 196,608 sectors and a Zip 250 489,532 (SPEC.md 138.4)")
        check(sparse_ok(p(kind + ".img")), "%s: the file is sparse" % kind,
              "a row copies one per run; 250 MB of zeros each time never finishes")
        s0 = open(p(kind + ".img"), "rb").read(SEC)
        eq(s0[510:512], b"\x55\xAA", "%s: sector 0 is signed" % kind)
        if k["mbr"]:
            ents = [s0[446 + 16 * i:462 + 16 * i] for i in range(4)]
            for i in range(3):
                check(not any(ents[i]), "%s: partition entry %d is empty" % (kind, i + 1),
                      "an IBM-format Zip disk has its one partition in ENTRY 4, and a "
                      "scan that stops early must find nothing in 1..3")
            e = ents[3]
            eq(e[4], 0x06, "%s: entry 4 is type 06h (FAT16B)" % kind)
            eq(e[0], 0x80, "%s: ...and active" % kind)
            start, length = struct.unpack_from("<II", e, 8)
            eq(start, 32, "%s: the partition starts at sector 32" % kind)
            eq(start + length, k["total"], "%s: ...and runs to the end of the disk" % kind)
            bpb = open(p(kind + ".img"), "rb").read(start * SEC + SEC)[start * SEC:]
            hidden = struct.unpack_from("<I", bpb, 28)[0]
            eq(hidden, 32, "%s: BPB_HiddSec is the partition start" % kind)
            tot = struct.unpack_from("<I", bpb, 32)[0]
            eq(tot, length, "%s: BPB_TotSec32 is the partition's length" % kind)
        else:
            bpb = s0
            eq(struct.unpack_from("<I", bpb, 28)[0], 0, "%s: no hidden sectors" % kind)
            eq(struct.unpack_from("<I", bpb, 32)[0], k["total"],
               "%s: BPB_TotSec32 is the whole disk" % kind)
            check(s0[0] in (0xEB, 0xE9), "%s: the BPB opens with a jump" % kind,
                  "the driver's superfloppy test is 'a jump, then 512 bytes per sector' (138.4)")
        eq(struct.unpack_from("<H", bpb, 11)[0], 512, "%s: 512 bytes a sector" % kind)
        eq(bpb[16], 2, "%s: two FATs" % kind)
        eq(bpb[54:62], b"FAT16   ", "%s: labelled FAT16" % kind)
        v2, how = Z.open_image(p(kind + ".img"))
        eq(v2.lay.fat12, False, "%s: FAT16, by cluster count" % kind)
        check(4085 <= v2.lay.nclus < 65525, "%s: %d clusters, a FAT16 count" % (kind, v2.lay.nclus))
        eq(how, "mbr(entry 4)" if k["mbr"] else "super", "%s: open_image finds it as %s" % (kind, how))
        eq(v2.check(), [], "%s: an empty disk passes its own fsck" % kind)
        v2.img.close()

    # ---- 2. files in and out, every size that has bitten a FAT writer -------
    v = Z.make(p("files.img"), "ibm")
    cb = v.cbytes
    sizes = [0, 1, 25, 511, 512, 513, cb - 1, cb, cb + 1, 2 * cb, 200000, 1000003]
    want = {}
    for i, n in enumerate(sizes):
        name = "F%02d.BIN" % i
        want[name] = Z.pattern(n, 1000 + i)
        v.write_file(name, want[name])
    for i, n in enumerate((3, cb, 70000)):
        name = "SUB%d/DEEP/G%d.DAT" % (i, i)
        want[name] = Z.pattern(n, 5000 + i)
        v.write_file(name, want[name])
    eq(v.check(), [], "the disk with a dozen files and nested folders passes fsck")
    for name, data in want.items():
        eq(v.read_file(name), data, "read back through the tool: %s (%d bytes)" % (name, len(data)))
    v.img.close()
    raw = Raw(p("files.img"))
    for name, data in want.items():
        eq(raw.read(name), data,
           "read back by the SECOND reader: %s (%d bytes)" % (name, len(data)),
           "the tool agreeing with itself is not evidence the FAT is right")

    # ---- 3. overwrite shrinks and grows, and a directory that must grow -----
    v, _ = Z.open_image(p("files.img"))
    free0 = v.free_clusters()
    v.write_file("F10.BIN", b"short now")
    check(v.free_clusters() > free0 + 90, "overwriting 200,000 bytes with 9 frees its clusters",
          "a leak here fills a QA disk after a few saves")
    eq(v.read_file("F10.BIN"), b"short now", "...and reads back")
    big = Z.pattern(300000, 77)
    v.write_file("F10.BIN", big)
    eq(v.read_file("F10.BIN"), big, "...then grows to 300,000 bytes and reads back")
    for i in range(150):                      # > 64 entries a 2 KB cluster holds
        v.write_file("MANY/N%03d.TXT" % i, b"%d\r\n" % i)
    eq(len([e for e in v.entries(v.lookup(["MANY"])["clus"])
            if e["name"] not in (".", "..")]), 150, "a folder of 150 files lists all 150")
    eq(v.check(), [], "...and the disk still passes fsck")
    free1 = v.free_clusters()
    v.img.close()
    v, _ = Z.open_image(p("files.img"))
    eq(v.free_clusters(), free1, "reopened, the free count is the same (the FAT was flushed)")
    eq(v.read_file("MANY/N149.TXT"), b"149\r\n", "...and the last file of the grown folder reads")
    v.img.close()

    # ---- 4. the fsck can fail (positive controls) ---------------------------
    def corrupt(what, fn, expect):
        v, _ = Z.open_image(p("files.img"))
        fn(v)
        bad = v.check()
        check(any(expect in b for b in bad), "fsck finds %s" % what,
              "an fsck that cannot fail certifies a corrupt guest write", got=bad, want=expect)
        v.img.close()

    import shutil
    shutil.copyfile(p("files.img"), p("keep.img"))

    def second_fat(v):
        v.img.write(v.base + v.lay.fat_lba + v.lay.fatsz, bytes(SEC))
    corrupt("the second FAT differing", second_fat, "differs")
    shutil.copyfile(p("keep.img"), p("files.img"))

    def crosslink(v):
        e = v.lookup(["F05.BIN"])
        f = v.lookup(["F06.BIN"])
        v.set(e["clus"], f["clus"])
        v.flush()
    corrupt("a cross-linked chain", crosslink, "also belongs")
    shutil.copyfile(p("keep.img"), p("files.img"))

    def lost(v):
        v.set(v.lay.maxclus, 0xFFFF)
        v.flush()
    corrupt("an allocated cluster nothing owns", lost, "belongs to nothing")
    shutil.copyfile(p("keep.img"), p("files.img"))

    def short(v):
        e = v.lookup(["F09.BIN"])
        v._put_dirent_at(e["lba"], e["off"], e["name11"], 0x20, e["clus"], e["size"] + 5 * v.cbytes)
    corrupt("a size longer than its chain", short, "need")

    # ---- 5. which partition the reader takes ---------------------------------
    for slot, typ, ok in ((1, 0x06, True), (3, 0x0E, True), (0, 0x07, False)):
        path = p("slot%d.img" % slot)
        shutil.copyfile(p("keep.img"), path)
        with open(path, "r+b") as f:
            s0 = bytearray(f.read(SEC))
            ent = bytes(s0[446 + 48:446 + 64])
            s0[446 + 48:446 + 64] = bytes(16)
            e = bytearray(ent)
            e[4] = typ
            s0[446 + 16 * slot:462 + 16 * slot] = e
            f.seek(0)
            f.write(s0)
        try:
            v, how = Z.open_image(path)
            eq(how, "mbr(entry %d)" % (slot + 1), "a FAT partition in entry %d is found there" % (slot + 1))
            v.img.close()
            check(ok, "entry %d type %02Xh opens" % (slot + 1, typ))
        except Z.ZipError:
            check(not ok, "entry %d type %02Xh is refused" % (slot + 1, typ),
                  "type 07h is NTFS/HPFS: not a disk os8088 can read")

    # ---- 6. clone keeps the holes; the QA set is deterministic --------------
    n = Z.clone(p("keep.img"), p("clone.img"))
    check(open(p("keep.img"), "rb").read() == open(p("clone.img"), "rb").read(),
          "a clone is byte-identical to its source")
    check(sparse_ok(p("clone.img")) and n < 8 * 1024 * 1024, "...and sparse (%d bytes of data)" % n)

    def sha(path):
        h = hashlib.sha1()
        with open(path, "rb") as f:
            for blk in iter(lambda: f.read(1 << 20), b""):
                h.update(blk)
        return h.hexdigest()
    a, b = p("qa1"), p("qa2")
    names = [os.path.basename(x) for x in Z.build_qa(a)]
    Z.build_qa(b)
    eq(sorted(names), sorted(Z.QA), "the QA set is the five disks docs/plans/ZIP-PLAN.md names")
    for nm in names:
        eq(sha(os.path.join(a, nm)), sha(os.path.join(b, nm)),
           "%s is the same bytes every build" % nm,
           "a QA disk that differs run to run cannot be compared")
    r = Raw(os.path.join(a, "zip100.img"))
    eq(r.read("README.TXT"), Z.README, "the QA disk's README.TXT, by the second reader")
    eq(r.read("DOCS/NOTE.TXT"), Z.NOTE, "...DOCS/NOTE.TXT")
    eq(r.read("BIG200.BIN"), Z.pattern(Z.BIG200, 0x20200), "...and BIG200.BIN (200,000 bytes)")
    r = Raw(os.path.join(a, "zip100super.img"))
    eq(r.read("HELLO.TXT"), Z.HELLO, "the superfloppy's HELLO.TXT, by the second reader")
    r = Raw(os.path.join(a, "zip250.img"))
    eq(r.read("BIG200.BIN"), Z.pattern(Z.BIG200, 0x20250), "the Zip 250's BIG200.BIN, by the second reader")

    shutil.rmtree(tmp, ignore_errors=True)
    done("t_zipimg")


main()

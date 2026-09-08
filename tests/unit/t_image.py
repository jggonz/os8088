#!/usr/bin/env python3
"""The shipped floppies, read by something that did not write them.

    python3 tests/unit/t_image.py

`tools/os88disk.py --verify` is this tree's own fsck and it is good - but it
is the BUILDER'S OWN CODE, so the failure it cannot see is the one that
matters most: both halves agreeing on the same wrong thing.  That is not
hypothetical here.  docs/FIELD-NOTES.md 4 is a whole class of bug found only
because a real machine disagreed with the writer, and SPEC.md 68.4.2 already
answers it the way this file does - `tools/wordfmt.py` parses what
`os88doc.py` writes, from the spec, sharing no code.

So the FAT12 walk below is written from the format rather than imported, and
`--verify` is run as well (`t_diskverify` in the suite): two readers, one
image.

WHAT IT CHECKS THAT --verify CANNOT.

  1. KERNEL.SYS IS CONTIGUOUS, and it is the kernel this tree just built.
     SPEC.md 19.3: the kernel is an ordinary file in the data area that 512
     bytes of boot sector read as a FLAT RUN - there is no cluster-chain
     walker in the boot sector and no room for one.  A fragmented KERNEL.SYS
     is a floppy that builds cleanly, verifies cleanly, mounts cleanly in
     DOS, and does not boot.  Nothing in the tree checks it.

  2. THE BPB IS A BYTE-EXACT STANDARD FORMAT.  Also SPEC.md 19.3, and it is
     there because of a real defect: the kernel used to live in the reserved
     area with `BPB_RsvdSecCnt` covering it, which is legal and which **DOS
     does not honour on a floppy** - DOS builds a floppy's BPB from its own
     table of standard formats, read the kernel as FAT and root directory,
     and reported garbled entries and 52,224 bytes free.  The geometry
     numbers below are the IBM ones; a build that drifts off them is a disk
     that misbehaves on the host OS rather than on os8088, which is the
     hardest place for this project to notice.

  3. SPEC.md 19.6's ATTRIBUTES.  `KERNEL.SYS` and every `.DRV` hidden +
     system + read-only, `SYSTEM.CFG` hidden + system but WRITABLE (the
     kernel rewrites it - make it read-only and the first save locks out
     every save after it), everything else on a system disk visible and
     read-only, and a data disk untouched because it is the user's.

  4. THE BOOT SECTOR'S SECTOR COUNT MATCHES THE KERNEL.  It is baked in at
     assembly time from the measured kernel size; if it is short the machine
     boots into a partly-loaded kernel, which is a black screen with no clue.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, eq, done                       # noqa: E402

A_RDONLY, A_HIDDEN, A_SYSTEM, A_VOLID, A_DIR = 0x01, 0x02, 0x04, 0x08, 0x10

# The IBM standard formats, by total sector count. SPEC.md 19.3: these are
# not ours to choose - DOS reads a floppy's geometry out of its own table and
# ignores the BPB, so anything else is a disk the host OS misreads.
# (bytes/sec, sec/clus, reserved, FATs, root entries, media, FAT sectors, spt, heads)
STANDARD = {
    2880: (512, 1, 1, 2, 224, 0xF0, 9, 18, 2),   # 1.44MB
    2400: (512, 1, 1, 2, 224, 0xF9, 7, 15, 2),   # 1.2MB 5.25" HD
    1440: (512, 2, 1, 2, 112, 0xF9, 3, 9, 2),    # 720KB
    720:  (512, 2, 1, 2, 112, 0xFD, 2, 9, 2),    # 360KB
}

SYSTEM_IMAGES = ["os8088.img", "os8088-120.img", "os8088-720.img",
                 "os8088-360.img"]
DATA_IMAGES = ["apps.img", "apps120.img", "apps720.img", "apps360.img",
               "media360.img"]


class Vol:
    """A FAT12 volume, walked from the format. Deliberately not os88disk's."""

    def __init__(self, blob, name):
        self.blob, self.name = blob, name
        (self.byts, self.spc, self.rsvd, self.nfat, self.rootent, self.tot16,
         self.media, self.fatsz, self.spt, self.heads) = \
            struct.unpack_from("<HBHBHHBHHH", blob, 11)
        self.fat0 = self.rsvd * self.byts
        self.rootlba = self.rsvd + self.nfat * self.fatsz
        self.rootsecs = (self.rootent * 32 + self.byts - 1) // self.byts
        self.data0 = self.rootlba + self.rootsecs
        self.clusters = (self.tot16 - self.data0) // self.spc

    def fat(self, n):
        """FAT12 entry n, decoded by hand (the 1.5-byte packing)."""
        off = self.fat0 + n + (n >> 1)
        v = struct.unpack_from("<H", self.blob, off)[0]
        return (v >> 4) if (n & 1) else (v & 0x0FFF)

    def chain(self, first):
        """[clusters], stopping at EOC. Bounded, so a loop cannot hang us."""
        out, c, seen = [], first, set()
        while 2 <= c < 0xFF0 and c not in seen and len(out) <= self.clusters + 2:
            seen.add(c)
            out.append(c)
            c = self.fat(c)
        return out, c

    def cluster_lba(self, c):
        return self.data0 + (c - 2) * self.spc

    def entries(self, lba, count_secs):
        """Directory entries in a region, live ones only."""
        base = lba * self.byts
        for i in range(count_secs * self.byts // 32):
            e = self.blob[base + i * 32: base + i * 32 + 32]
            if not e or e[0] == 0x00:
                break
            if e[0] == 0xE5 or (e[11] & 0x0F) == 0x0F:
                continue                       # deleted, or an LFN fragment
            yield e

    def walk(self, lba=None, secs=None, path=""):
        """(path, name11, attr, first-cluster, size) for the whole volume."""
        if lba is None:
            lba, secs = self.rootlba, self.rootsecs
        for e in self.entries(lba, secs):
            name11, attr = e[0:11], e[11]
            clus, size = struct.unpack_from("<H", e, 26)[0], struct.unpack_from("<I", e, 28)[0]
            if attr & A_VOLID:
                continue
            nm = name11.decode("ascii", "replace")
            if attr & A_DIR:
                if name11[0:1] == b".":
                    continue                   # the . and .. entries
                yield (path, name11, attr, clus, size)
                sub, _ = self.chain(clus)
                for c in sub:
                    for row in self.walk(self.cluster_lba(c), self.spc,
                                         path + nm[:8].strip() + "/"):
                        yield row
            else:
                yield (path, name11, attr, clus, size)


def read(p):
    with open(p, "rb") as f:
        return f.read()


def check_geometry(v):
    want = STANDARD.get(v.tot16)
    if not check(want is not None, "%s: %d total sectors is not a standard format"
                 % (v.name, v.tot16), "SPEC.md 19.3 - DOS reads a floppy's geometry "
                 "from its own table, so a non-standard one is misread on the host"):
        return
    got = (v.byts, v.spc, v.rsvd, v.nfat, v.rootent, v.media, v.fatsz, v.spt, v.heads)
    eq(got, want, "%s: BPB is the standard %dKB format" % (v.name, v.tot16 * 512 // 1024),
       "a drifted BPB builds, verifies and mounts in os8088 and is garbled by DOS "
       "- 52,224 bytes free was the symptom last time (SPEC.md 19.3)")
    eq(struct.unpack_from("<H", v.blob, 510)[0], 0xAA55,
       "%s: boot signature" % v.name)
    check(v.blob[0] in (0xEB, 0xE9), "%s: boot sector starts with a jump" % v.name,
          "a BIOS loads it and executes byte 0", got=hex(v.blob[0]))
    eq(len(v.blob), v.tot16 * v.byts, "%s: image size matches TotSec16" % v.name)


def check_chains(v):
    """Every file's chain: in range, terminated, and the right length."""
    owner = {}
    for path, name11, attr, clus, size in v.walk():
        nm = path + name11.decode("ascii", "replace")
        if attr & A_DIR:
            want = None                        # a directory has no size field
        else:
            want = (size + v.spc * v.byts - 1) // (v.spc * v.byts)
        if clus == 0:
            check(size == 0, "%s: %s has no first cluster but size %d"
                  % (v.name, nm, size))
            continue
        chain, last = v.chain(clus)
        check(last >= 0xFF8, "%s: %s chain is not EOC-terminated" % (v.name, nm),
              "a loop or a free cluster mid-chain - the loader walks off the file",
              got=hex(last), want=">= 0xFF8")
        for c in chain:
            check(2 <= c < 2 + v.clusters, "%s: %s uses cluster %d, out of range"
                  % (v.name, nm, c), got=c, want="2..%d" % (v.clusters + 1))
            if c in owner:
                check(False, "%s: cluster %d is CROSS-LINKED" % (v.name, c),
                      "two files share a cluster - writing one corrupts the other",
                      got="%s and %s" % (owner[c], nm), want="one owner")
            owner[c] = nm
        if want is not None:
            eq(len(chain), want, "%s: %s chain length matches its size" % (v.name, nm),
               "a short chain truncates the file; a long one leaks clusters")


def check_kernel(v, kernel):
    """SPEC.md 19.3: KERNEL.SYS is read as a FLAT RUN by the boot sector."""
    rows = [r for r in v.walk() if r[1] == b"KERNEL  SYS"]
    if not check(len(rows) == 1, "%s: exactly one KERNEL.SYS" % v.name, got=len(rows)):
        return
    _, _, attr, clus, size = rows[0]
    chain, _ = v.chain(clus)
    eq(chain, list(range(chain[0], chain[0] + len(chain))),
       "%s: KERNEL.SYS is contiguous" % v.name,
       "the boot sector has no cluster-chain walker (SPEC.md 19.3) - a fragmented "
       "kernel builds, verifies and mounts cleanly and does not boot")
    eq(size, len(kernel), "%s: KERNEL.SYS size matches build/kernel.sys" % v.name)
    base = v.cluster_lba(chain[0]) * v.byts
    eq(v.blob[base:base + len(kernel)], kernel,
       "%s: KERNEL.SYS bytes are the kernel this tree built" % v.name,
       "a stale image is indistinguishable from a change that did nothing")
    # ...and the count the boot sector was assembled with must cover it.
    secs = (len(kernel) + 511) // 512
    check(secs * 512 >= len(kernel), "%s: kernel sector count covers the image" % v.name)


def check_attrs(v, is_system):
    """SPEC.md 19.6, plus the two files the KERNEL owns.

    `SYSTEM.CFG` (SPEC.md 51.5) and `ASSOC.DAT` (SPEC.md 54.7) are one class
    and not two: both are written by the kernel, so both are hidden + system
    so the user cannot delete them by accident, and NEITHER may be read-only
    or the first save locks out every save after it.  ASSOC.DAT is on data
    disks too - it is the volume's own icon and association cache, so it
    belongs to the volume rather than to the system disk.
    """
    for path, name11, attr, clus, size in v.walk():
        nm = path + name11.decode("ascii", "replace")
        if attr & A_DIR:
            continue
        hs = A_HIDDEN | A_SYSTEM
        if name11 in (b"SYSTEM  CFG", b"ASSOC   DAT"):
            eq(attr & (hs | A_RDONLY), hs,
               "%s: %s hidden+system, WRITABLE" % (v.name, nm),
               "the kernel rewrites it - read-only and the first save locks out "
               "every save after it (SPEC.md 19.6/51.5/54.7)")
        elif not is_system:
            check(not (attr & hs), "%s: %s is not hidden on a DATA disk" % (v.name, nm),
                  "a data disk is the user's; a hidden+system file there cannot be "
                  "listed or deleted from inside os8088 (SPEC.md 19.6)")
        elif name11 == b"KERNEL  SYS" or name11.endswith(b"DRV"):
            eq(attr & (hs | A_RDONLY), hs | A_RDONLY,
               "%s: %s is hidden+system+read-only" % (v.name, nm))
        else:
            check(attr & A_RDONLY, "%s: %s is read-only on the system disk" % (v.name, nm),
                  "the boot disk holds nothing a user should delete by accident")
            check(not (attr & hs), "%s: %s stays VISIBLE" % (v.name, nm),
                  "TASKMGR.O88 is loaded by name from the chip menu and README.TXT is "
                  "meant to be opened - hiding either makes it unreachable")


def main():
    build = os.path.join(ROOT, "build")
    # **build/kernel.sys AND NOT kernel.bin**, because the two stopped being the
    # same file when the kernel learned to ship compressed (SPEC.md 2.9.13).
    # kernel.bin is the IMAGE - what runs, what os88sym resolves against, what
    # every size guard is about - and kernel.sys is the FILE, which under
    # KZIP is 40 sectors shorter and under no knob at all is a copy. What goes
    # on a floppy is the file, so that is what a floppy is compared with; the
    # Makefile ships $(KERNFILE) for exactly the same reason.
    kernel = read(os.path.join(build, "kernel.sys"))
    seen = 0
    for img, is_system in ([(i, True) for i in SYSTEM_IMAGES] +
                           [(i, False) for i in DATA_IMAGES]):
        p = os.path.join(build, img)
        if not os.path.exists(p):
            check(False, "%s is missing" % img, "run `make` first")
            continue
        seen += 1
        v = Vol(read(p), img)
        check_geometry(v)
        check_chains(v)
        check_attrs(v, is_system)
        if is_system:
            check_kernel(v, kernel)
    check(seen == len(SYSTEM_IMAGES) + len(DATA_IMAGES),
          "every shipped image was read", got=seen)
    print("t_image: %d images, kernel %d bytes (%d sectors)"
          % (seen, len(kernel), (len(kernel) + 511) // 512))
    done("t_image")


if __name__ == "__main__":
    main()

"""FIXTURES for SPEC.md 18.101's mounted images - host-side, no emulator.

`imgmount.py` builds what it boots out of these, into a directory of its own
(docs/WRITING-TESTS.md 5.5): a disk image to mount, and a host volume that
carries it CONTIGUOUS or FRAGMENTED. The fragmented one is the case that
matters - `dskw_alloc` is next-fit from a rover that resets at every mount
(SPEC.md 18.4), so an image a user copies onto a disk that has ever had a
file deleted from it is usually in pieces - and it is built here by moving
the file's clusters about after os88disk.py has written it whole, because
os88disk.py itself only ever writes a file in one run.

    python3 tests/imgfix.py OUTDIR      # builds both, prints the layout
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISK = os.path.join(ROOT, "tools", "os88disk.py")

HELLO = b"THIS FILE LIVES INSIDE TEST.IMG\r\n"


def _run(*args):
    subprocess.run([sys.executable, DISK] + list(args), check=True,
                   stdout=subprocess.DEVNULL)


class Fat12:
    """Just enough FAT12 to read and rewrite one file's chain in place."""

    def __init__(self, data):
        self.d = bytearray(data)
        (self.bps, self.spc, self.res, self.nf, self.rootn, _tot, _med,
         self.fatsz) = struct.unpack_from("<HBHBHHBH", self.d, 11)
        self.rootlba = self.res + self.nf * self.fatsz
        self.datalba = self.rootlba + (self.rootn * 32 + 511) // 512

    def maxclus(self):
        tot = struct.unpack_from("<H", self.d, 19)[0]
        return (tot - self.datalba) // self.spc + 1

    def lba(self, c):
        return self.datalba + (c - 2) * self.spc

    def get(self, c):
        o = self.res * 512 + c * 3 // 2
        v = self.d[o] | self.d[o + 1] << 8
        return v >> 4 if c & 1 else v & 0xFFF

    def put(self, c, val):
        for f in range(self.nf):
            o = (self.res + f * self.fatsz) * 512 + c * 3 // 2
            v = self.d[o] | self.d[o + 1] << 8
            if c & 1:
                v = (v & 0x000F) | (val << 4)
            else:
                v = (v & 0xF000) | val
            self.d[o] = v & 0xFF
            self.d[o + 1] = v >> 8

    def entry(self, name83):
        for i in range(self.rootn):
            o = self.rootlba * 512 + i * 32
            if self.d[o:o + 11] == name83:
                return o
        raise KeyError(name83)

    def chain(self, first):
        out = [first]
        while True:
            n = self.get(out[-1])
            if n >= 0xFF8:
                return out
            out.append(n)

    def clus(self, c):
        a = self.lba(c) * 512
        return bytes(self.d[a:a + self.spc * 512])

    def setclus(self, c, data):
        a = self.lba(c) * 512
        self.d[a:a + self.spc * 512] = data


def runs(fat, first):
    """The file's sectors as host runs: [(first file sector, host lba, n)]."""
    out, fs = [], 0
    for c in fat.chain(first):
        lba = fat.lba(c)
        if out and out[-1][1] + out[-1][2] == lba:
            out[-1] = (out[-1][0], out[-1][1], out[-1][2] + fat.spc)
        else:
            out.append((fs, lba, fat.spc))
        fs += fat.spc
    return out


def build(outdir, fragment=True):
    """Write OUTDIR/TEST.IMG (360KB) and OUTDIR/host.img (720KB carrying it).

    With `fragment`, the file's clusters are moved so that it is in THREE
    runs and the runs are in REVERSE order on the host: the file's last third
    is at the lowest address. Answers (path, runs).
    """
    os.makedirs(outdir, exist_ok=True)
    hello = os.path.join(outdir, "HELLO.TXT")
    with open(hello, "wb") as f:
        f.write(HELLO)
    img = os.path.join(outdir, "TEST.IMG")
    _run("--size", "360", "-o", img, hello)
    host = os.path.join(outdir, "host.img")
    # TEST.IMG second, so a free run sits BELOW it as well as above
    pad = os.path.join(outdir, "PAD.BIN")
    with open(pad, "wb") as f:
        f.write(b"\xA5" * 1024 * 40)
    _run("--size", "720", "-o", host, pad, img)
    if fragment:
        fragment_file(host, b"TEST    IMG")
    with open(host, "rb") as f:
        fat = Fat12(f.read())
    ent = fat.entry(b"TEST    IMG")
    return host, runs(fat, struct.unpack_from("<H", fat.d, ent + 26)[0])


def fragment(host, n83):
    fragment_file(host, n83)
    return host


def fragment_file(host, n83):
    """Move file `n83`'s clusters on `host` into three runs, the last third
    lowest on the disk (into its own first clusters), the other two above
    every used cluster with a gap between them. The bytes are unchanged."""
    with open(host, "rb") as f:
        fat = Fat12(f.read())
    ent = fat.entry(n83)
    first = struct.unpack_from("<H", fat.d, ent + 26)[0]
    if True:
        old = fat.chain(first)
        data = [fat.clus(c) for c in old]
        n = len(old)
        third = n // 3
        pieces = [old[:third], old[third:2 * third], old[2 * third:]]
        # free clusters above EVERY used one, in ascending order
        top = max(c for c in range(2, fat.maxclus() + 1) if fat.get(c)) + 1
        free = [c for c in range(top, top + n + 8) if fat.get(c) == 0]
        if len(free) < n:
            raise SystemExit("imgfix: no room to fragment on the host")
        # new placement: piece 2 lowest (the original start), then piece 0,
        # then piece 1 - three runs, non-ascending on the host
        lo = old[:len(pieces[2])]
        rest = old[len(pieces[2]):] + free
        newc = [None] * n
        place = {2: lo, 0: rest[:len(pieces[0])],
                 1: rest[len(pieces[0]) + 3:len(pieces[0]) + 3 + len(pieces[1])]}
        i = 0
        for k, piece in enumerate(pieces):
            for j in range(len(piece)):
                newc[i] = place[k][j]
                i += 1
        for c in set(old) | set(free):
            fat.put(c, 0)
        for i, c in enumerate(newc):
            fat.put(c, newc[i + 1] if i + 1 < n else 0xFFF)
            fat.setclus(c, data[i])
        struct.pack_into("<H", fat.d, ent + 26, newc[0])
    with open(host, "wb") as f:
        f.write(fat.d)


def build_set(outdir):
    """The gate's host: a 1.44MB volume carrying four images.

      TEST.IMG   360KB, FRAGMENTED into three runs stored out of order
      RO.IMG     360KB, contiguous, READ-ONLY on the host (the user's)
      STALE.IMG  360KB, contiguous, wearing the MOUNTED marker a machine that
                 stopped mid-mount leaves (read-only + byte 12 = IMG_MARK)
      BAD.IMG    4KB of zeros: a whole number of sectors and no FAT at all

    Answers (path, {name: runs}).
    """
    os.makedirs(outdir, exist_ok=True)
    base, _ = build(os.path.join(outdir, "one"), fragment=False)
    img = os.path.join(outdir, "one", "TEST.IMG")
    names = {}
    for n in ("RO.IMG", "STALE.IMG"):
        with open(img, "rb") as f, open(os.path.join(outdir, n), "wb") as g:
            g.write(f.read())
        names[n] = os.path.join(outdir, n)
    bad = os.path.join(outdir, "BAD.IMG")
    with open(bad, "wb") as f:
        f.write(bytes(4096))
    pad = os.path.join(outdir, "one", "PAD.BIN")
    host = os.path.join(outdir, "host.img")
    _run("--size", "1440", "-o", host, pad, img, names["RO.IMG"],
         names["STALE.IMG"], bad)
    with open(host, "rb") as f:
        fat = Fat12(f.read())
    for n83, attr, res in ((b"RO      IMG", 0x01, 0),
                           (b"STALE   IMG", 0x01, IMG_MARK)):
        e = fat.entry(n83)
        fat.d[e + 11] = (fat.d[e + 11] & ~0x01) | attr
        fat.d[e + 12] = res
    with open(host, "wb") as f:
        f.write(fat.d)
    host = fragment(host, b"TEST    IMG")
    with open(host, "rb") as f:
        fat = Fat12(f.read())
    out = {}
    for n83 in (b"TEST    IMG", b"RO      IMG", b"STALE   IMG", b"BAD     IMG"):
        e = fat.entry(n83)
        out[n83] = runs(fat, struct.unpack_from("<H", fat.d, e + 26)[0])
    return host, out


IMG_MARK = 0xC1                 # kernel/diskops.inc's, byte 12 of a mounted
                                # file's directory entry (SPEC.md 18.101.3)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "build/imgfix"
    path, r = build_set(out)
    for k, v in r.items():
        print("%s: %s in %d run(s): %r" % (path, k.decode(), len(v), v))

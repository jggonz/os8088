#!/usr/bin/env python3
"""Unpack what a period installer and a CD-ROM title carry, so their programs
can be read by tools/os88ne.py and tools/w16reach.py (WIN16-ANY-PLAN O3).

    w16unpack.py blast FILE OUT           an InstallShield 3 single compressed
                                          file (_INST16.EX_): its header, then
                                          a PKWARE DCL "implode" stream
    w16unpack.py is3 FILE.LIB OUTDIR      an InstallShield 3 archive
                                          (magic 13 5D 65 8C: PROGRAM.LIB,
                                          SYSTEM.LIB, DATA.Z)
    w16unpack.py szdd FILE OUT            COMPRESS.EXE's SZDD (a Microsoft
                                          Setup Toolkit disk's *.EX_, *.DL_)
    w16unpack.py iso IMAGE [OUTDIR [EXT...]]
                                          list an ISO 9660 or High Sierra
                                          image; with OUTDIR, copy out the
                                          files ending in EXT (all without)
    w16unpack.py track BIN OUT.ISO SECTORS  the first SECTORS raw 2352-byte
                                          MODE1 sectors of a .bin, cooked to
                                          2048 (track 1 of a mixed-mode CD)

Only used on downloads in win16ref/, which is git-ignored. The DCL decoder is
a port of Mark Adler's zlib contrib/blast/blast.c (zlib licence).
"""
import os, struct, sys

# --- PKWARE DCL implode (blast) ---------------------------------------------

MAXBITS = 13


class _Huff:
    def __init__(self, rep):
        lens = []
        for b in rep:
            lens += [b & 15] * ((b >> 4) + 1)
        self.count = [0] * (MAXBITS + 1)
        for n in lens:
            self.count[n] += 1
        offs = [0] * (MAXBITS + 1)
        for i in range(1, MAXBITS):
            offs[i + 1] = offs[i] + self.count[i]
        self.symbol = [0] * len(lens)
        for s, n in enumerate(lens):
            if n:
                self.symbol[offs[n]] = s
                offs[n] += 1


_LIT = _Huff(bytes([11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8,
                    9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24, 12, 11, 7, 9, 11, 12, 6, 7, 22, 5,
                    7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12,
                    8, 12, 5, 38, 5, 38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27,
                    44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61, 12, 45,
                    44, 173]))
_LEN = _Huff(bytes([2, 35, 36, 53, 38, 23]))
_DIST = _Huff(bytes([2, 20, 53, 230, 247, 151, 248]))
_BASE = [3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264]
_EXTRA = [0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8]


class _Blast:
    def __init__(self, data, pos):
        self.d, self.p, self.buf, self.cnt = data, pos, 0, 0

    def bits(self, need):
        val = self.buf
        while self.cnt < need:
            if self.p >= len(self.d):
                raise EOFError
            val |= self.d[self.p] << self.cnt
            self.p += 1
            self.cnt += 8
        self.buf = val >> need
        self.cnt -= need
        return val & ((1 << need) - 1)

    def decode(self, h):
        code = first = index = 0
        length = 1
        buf, left = self.buf, self.cnt
        while True:
            while left:
                code |= (buf & 1) ^ 1
                buf >>= 1
                count = h.count[length]
                if code < first + count:
                    self.buf, self.cnt = buf, (self.cnt - length) & 7
                    return h.symbol[index + code - first]
                index += count
                first = (first + count) << 1
                code <<= 1
                length += 1
                left -= 1
            left = MAXBITS + 1 - length
            if left == 0:
                raise ValueError("ran out of codes")
            if self.p >= len(self.d):
                raise EOFError
            buf = self.d[self.p]
            self.p += 1
            left = min(left, 8)

    def run(self):
        lit, dictb = self.bits(8), self.bits(8)
        if lit > 1 or not 4 <= dictb <= 6:
            raise ValueError("not a DCL stream")
        out = bytearray()
        while True:
            if self.bits(1):
                sym = self.decode(_LEN)
                n = _BASE[sym] + self.bits(_EXTRA[sym])
                if n == 519:
                    return bytes(out)
                s = 2 if n == 2 else dictb
                dist = (self.decode(_DIST) << s) + self.bits(s) + 1
                if dist > len(out):
                    raise ValueError("distance too far")
                for _ in range(n):
                    out.append(out[-dist])
            else:
                out.append(self.decode(_LIT) if lit else self.bits(8))


def blast(data, pos=0):
    return _Blast(data, pos).run()


def is3_single(d):
    """_INST16.EX_: magic 2A AB 79 D8, a 255-byte header with Stirling's
    copyright in it, then the stream. The header's length is found rather than
    trusted: the first offset that decodes is the stream."""
    if d[:4] != b"\x2a\xab\x79\xd8":
        sys.exit("not an InstallShield 3 compressed file")
    for off in range(4, 512):
        try:
            out = blast(d, off)
        except (ValueError, EOFError, IndexError):
            continue
        if out[:2] == b"MZ" or len(out) > 64:
            return out
    sys.exit("no DCL stream in the first 512 bytes")


def is3_entries(d):
    """An InstallShield 3 archive: the file count at +0Ch, the directory table
    at +29h, the directory count at +31h. Each directory names its file
    count; each file entry has its sizes and offset at +3, its own length at
    +17h, and its name, length-prefixed, at +1Dh."""
    if d[:4] != b"\x13\x5d\x65\x8c":
        sys.exit("not an InstallShield 3 archive")
    nfiles = struct.unpack_from("<H", d, 0x0C)[0]
    p = struct.unpack_from("<I", d, 0x29)[0]
    ndirs = struct.unpack_from("<H", d, 0x31)[0]
    dirs = []
    for _ in range(ndirs):
        cnt, elen, nlen = struct.unpack_from("<HHH", d, p)
        dirs.append((cnt, d[p + 6:p + 6 + nlen].decode("latin1")))
        p += elen
    out = []
    for cnt, dname in dirs:
        for _ in range(cnt):
            usize, csize, off = struct.unpack_from("<III", d, p + 3)
            elen = struct.unpack_from("<H", d, p + 0x17)[0]
            nlen = d[p + 0x1D]
            name = d[p + 0x1E:p + 0x1E + nlen].decode("latin1")
            out.append((os.path.join(dname, name) if dname else name, usize, csize, off))
            p += elen
    if len(out) != nfiles:
        sys.exit(f"directory lists {len(out)} files, header says {nfiles}")
    return out


def is3_extract(path, outdir):
    d = open(path, "rb").read()
    os.makedirs(outdir, exist_ok=True)
    for name, usize, csize, off in is3_entries(d):
        data = blast(d[off:off + csize])
        if len(data) != usize:
            sys.exit(f"{name}: {len(data)} bytes, directory says {usize}")
        dest = os.path.join(outdir, name)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(data)
        print("%-24s %8d" % (name, usize))


# --- SZDD --------------------------------------------------------------------

def szdd(d):
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "tools"))
    from w16font import szdd as _szdd
    return _szdd(d)


# --- CD images ---------------------------------------------------------------

def iso_walk(img):
    """Every file of an ISO 9660 or a High Sierra image, as (path, lba, size).
    High Sierra (1992 discs, Multimedia Beethoven's among them) names itself
    "CDROM" at +9 of sector 16 rather than "CD001" at +1, keeps the root's
    record at +180 rather than +156, and a record's flags at +24 rather than
    +25; a record's name length is at +32 in both."""
    pvd = img[16 * 2048:17 * 2048]
    hsg = pvd[9:14] == b"CDROM"
    if not hsg and pvd[1:6] != b"CD001":
        sys.exit("neither ISO 9660 nor High Sierra")
    root = pvd[180:214] if hsg else pvd[156:190]
    flag = 24 if hsg else 25
    out = []

    def walk(lba, size, path):
        d = img[lba * 2048:lba * 2048 + size]
        i = 0
        while i < len(d):
            n = d[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048
                continue
            r = d[i:i + n]
            ext, ln = struct.unpack_from("<I", r, 2)[0], struct.unpack_from("<I", r, 10)[0]
            name = r[33:33 + r[32]]
            if name not in (b"\0", b"\1"):
                p = path + "/" + name.decode("latin1").split(";")[0]
                if r[flag] & 2:
                    walk(ext, ln, p)
                else:
                    out.append((p, ext, ln))
            i += n

    walk(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "")
    return hsg, out


def track(binpath, outpath, sectors):
    with open(binpath, "rb") as f, open(outpath, "wb") as o:
        for _ in range(sectors):
            s = f.read(2352)
            if len(s) != 2352:
                sys.exit("the .bin ends before the track does")
            o.write(s[16:2064])


def main(a):
    if not a:
        sys.exit(__doc__)
    c = a[0]
    if c == "blast":
        open(a[2], "wb").write(is3_single(open(a[1], "rb").read()))
    elif c == "is3":
        is3_extract(a[1], a[2])
    elif c == "szdd":
        open(a[2], "wb").write(szdd(open(a[1], "rb").read()))
    elif c == "iso":
        img = open(a[1], "rb").read()
        hsg, files = iso_walk(img)
        print("High Sierra" if hsg else "ISO 9660", len(files), "files")
        for p, lba, size in files:
            if len(a) > 2:
                if len(a) > 3 and not p.upper().endswith(tuple(x.upper() for x in a[3:])):
                    continue
                dest = os.path.join(a[2], p.strip("/").replace("/", os.sep))
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with open(dest, "wb") as f:
                    f.write(img[lba * 2048:lba * 2048 + size])
            print("%-48s %10d" % (p, size))
    elif c == "track":
        track(a[1], a[2], int(a[3]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])

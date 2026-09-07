#!/usr/bin/env python3
"""Read one file out of a FAT12 floppy image, on the host.

    python3 tools/nifatcat.py IMG [FOLDER] NAME.EXT

apps/infones/hosttest/nisystest.sh's other half: the NITEST build writes its
result beside itself on the B: floppy and this is how the host reads it back.
It is an INDEPENDENT reader - it shares nothing with tools/os88disk.py, whose
own `--verify` walks a disk it built - and it is deliberately tiny: it does
not create, delete or write anything.

Grepping the image for the text instead would be wrong and this project has
been caught by it once already (`.claude/skills/port-to-os8088/LESSONS.md`
10): the first grep found the PACKAGE's own string literals, which are on the
same disk.
"""
import struct
import sys


def read(img, folder, name):
    d = open(img, "rb").read()
    bps = struct.unpack_from("<H", d, 11)[0]
    spc = d[13]
    rsvd = struct.unpack_from("<H", d, 14)[0]
    nfat = d[16]
    nroot = struct.unpack_from("<H", d, 17)[0]
    spf = struct.unpack_from("<H", d, 22)[0]
    fat = d[rsvd * bps: (rsvd + spf) * bps]
    root = (rsvd + nfat * spf) * bps
    rootlen = nroot * 32
    data = root + rootlen

    def chain(cl):
        out = []
        while 2 <= cl < 0xFF0:
            out.append(cl)
            i = cl * 3 // 2
            v = fat[i] | (fat[i + 1] << 8)
            cl = (v >> 4) if (cl & 1) else (v & 0xFFF)
        return out

    def bytes_of(cl, n):
        b = b""
        for c in chain(cl):
            o = data + (c - 2) * spc * bps
            b += d[o: o + spc * bps]
        return b[:n] if n else b

    def entries(blob):
        for i in range(0, len(blob), 32):
            e = blob[i:i + 32]
            if not e or e[0] in (0x00, 0xE5) or (e[11] & 0x0F) == 0x0F:
                continue
            nm = e[0:8].decode("latin1").rstrip()
            ex = e[8:11].decode("latin1").rstrip()
            yield (nm + ("." + ex if ex else ""), e[11],
                   struct.unpack_from("<H", e, 26)[0],
                   struct.unpack_from("<I", e, 28)[0])

    blob = d[root:root + rootlen]
    if folder:
        for nm, attr, cl, sz in entries(blob):
            if nm.upper() == folder.upper() and (attr & 0x10):
                blob = bytes_of(cl, 0)
                break
        else:
            raise SystemExit("nifatcat: no folder %s" % folder)
    for nm, attr, cl, sz in entries(blob):
        if nm.upper() == name.upper() and not (attr & 0x10):
            return bytes_of(cl, sz)
    raise SystemExit("nifatcat: no file %s" % name)


a = sys.argv[1:]
if len(a) == 2:
    sys.stdout.write(read(a[0], None, a[1]).decode("latin1"))
elif len(a) == 3:
    sys.stdout.write(read(a[0], a[1], a[2]).decode("latin1"))
else:
    raise SystemExit(__doc__)

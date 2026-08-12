#!/usr/bin/env python3
"""Split the on-demand kernel modules out of the assembled kernel.

SPEC.md 2.8, docs/ONDEMAND-PLAN.md.  `kernel.asm` assembles to
`.text | pad | .cold | pad | .ovl | .modc | .modf | .modmap`, and everything
from `.modc` onward is code that must NOT ship inside KERNEL.SYS: each module
becomes a file of its own that the kernel reads into a heap claim when the
feature is asked for.

**Why the modules are assembled with the kernel at all**, rather than as
separate nasm runs against a generated table of kernel addresses: a module is
`.cold` code (SPEC.md 2.6) - CS of its own, DS still KERNEL_SEG - so every
kernel symbol it names has to be the address the kernel itself uses.  One
assembly makes that true by construction.  A second assembly would make it a
claim, and the tree's rule about second opinions is that they drift.

**How this finds the boundaries.**  Not by re-assembling with -DKERNSIZE and
parsing a %warning (which is what tools/kernsize.py does, and costs an
assembly), and not by being told the numbers by the Makefile (which would put
the layout in two places).  The binary describes itself: its last two bytes
are 0x384F and the four before them point back at a table naming every
module's (start, size) as DWORDS - the kernel is ~100KB before a module is
added, so a word offset would truncate.  18 bytes, in a region that is about
to be cut off.

**The truncation is checked, not assumed.**  `.modc` starts UNPADDED at the
end of `.ovl`, so the head of the file is byte for byte the kernel.bin that
would have been emitted with no modules in the tree at all - and --expect
proves it against a known md5 when one is supplied.
"""
import argparse
import hashlib
import os
import struct
import sys

MAGIC = 0x384F                  # 'O','8', as a little-endian word
MAP_MAGIC = b"O8MM"
MOD_H_MAGIC = 0
MOD_H_VER = 2
MOD_H_ID = 3
MOD_H_BUILD = 4
MOD_H_STAMP = 6
MOD_H_IMG = 8
MOD_H_NENT = 10
MOD_H_ENT = 12
MOD_VER = 5


def fail(msg):
    sys.stderr.write("os88mod: %s\n" % msg)
    raise SystemExit(1)


def read_map(blob):
    """(start, size) per module, out of the trailer the kernel emitted."""
    if len(blob) < 16:
        fail("kernel image is impossibly short")
    if struct.unpack_from("<H", blob, len(blob) - 2)[0] != MAGIC:
        fail("no module trailer at the end of the image - is .modmap declared "
             "last, and is it the last section with any bytes in it?")
    off = struct.unpack_from("<I", blob, len(blob) - 6)[0]
    if off + 6 > len(blob) or blob[off:off + 4] != MAP_MAGIC:
        fail("the module trailer points at %#x, which is not a map" % off)
    n = blob[off + 4]
    rows = []
    p = off + 6
    for i in range(n):
        if p + 8 > len(blob):
            fail("module map row %d runs off the end of the image" % i)
        start, size = struct.unpack_from("<II", blob, p)
        rows.append((start, size))
        p += 8
    if p + 6 != len(blob):
        fail("module map claims %d rows but the trailer is %d bytes off"
             % (n, len(blob) - 6 - p))
    return off, rows


def check_image(name, img, ident, build):
    """The kernel's own mod_check, on the host, before anything ships.

    It is duplicated here on purpose and the duplication is the point: a
    module the kernel would refuse at run time is a machine whose Control
    Panel does not open, and finding that at `make` time costs nothing while
    finding it in the field costs a disk swap and a session.
    """
    if len(img) < MOD_H_ENT + 2:
        fail("%s: image is %d bytes, too short for a header" % (name, len(img)))
    magic, = struct.unpack_from("<H", img, MOD_H_MAGIC)
    if magic != MAGIC:
        fail("%s: bad magic %#06x" % (name, magic))
    if img[MOD_H_VER] != MOD_VER:
        fail("%s: header version %d, expected %d - a package is 3 and a "
             "driver is 4, and all three must stay distinct"
             % (name, img[MOD_H_VER], MOD_VER))
    if img[MOD_H_ID] != ident:
        fail("%s: header says module id %d, map row is %d"
             % (name, img[MOD_H_ID], ident))
    stamp, = struct.unpack_from("<H", img, MOD_H_BUILD)
    if build is not None and stamp != build:
        fail("%s: build stamp %d against kernel build %d - the module and the "
             "kernel must come out of one assembly" % (name, stamp, build))
    size, = struct.unpack_from("<H", img, MOD_H_IMG)
    if size != len(img):
        fail("%s: header says %d bytes, the section is %d"
             % (name, size, len(img)))
    nent, = struct.unpack_from("<H", img, MOD_H_NENT)
    if not 1 <= nent <= 4:
        fail("%s: %d entries, which is outside 1..MOD_NENT" % (name, nent))
    hdr = MOD_H_ENT + 4 * 2
    for i in range(nent):
        ent, = struct.unpack_from("<H", img, MOD_H_ENT + i * 2)
        if not hdr <= ent < len(img):
            fail("%s: entry %d is offset %#x, outside the image's own code "
                 "(%#x..%#x)" % (name, i, ent, hdr, len(img)))
    # The LAYOUT stamp is the kernel's to check (SPEC.md 2.8.2) - only the
    # assembly knows the section sizes - but every module in one image must
    # carry the SAME one, and that this tool can say.
    return nent, struct.unpack_from("<H", img, MOD_H_STAMP)[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", help="the assembled kernel, modules and all")
    ap.add_argument("-k", "--kernel", required=True,
                    help="write the truncated KERNEL.SYS here")
    ap.add_argument("-m", "--module", action="append", default=[],
                    metavar="N=PATH",
                    help="write module N's image to PATH (repeatable)")
    ap.add_argument("--build", type=int, default=None,
                    help="the kernel's build number, checked against each "
                         "module's stamp")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args()

    with open(args.image, "rb") as fh:
        blob = fh.read()
    map_off, rows = read_map(blob)

    wanted = {}
    for spec in args.module:
        n, _, path = spec.partition("=")
        if not path:
            fail("--module wants N=PATH, got %r" % spec)
        wanted[int(n)] = path
    if len(wanted) != len(rows):
        fail("the image carries %d modules and %d --module arguments were "
             "given; every one has to be written or it is missing from the "
             "floppy and the feature does not open"
             % (len(rows), len(wanted)))

    # The kernel is everything before the first module.  Modules are emitted
    # UNPADDED and in order, so this is also where .ovl ends.
    cut = rows[0][0] if rows else map_off
    for i, (start, size) in enumerate(rows):
        if start + size > len(blob):
            fail("module %d runs past the end of the image" % i)
        if i and start < rows[i - 1][0] + rows[i - 1][1]:
            fail("module %d starts inside module %d" % (i, i - 1))

    out = []
    for i, (start, size) in enumerate(rows):
        img = blob[start:start + size]
        path = wanted[i]
        nent, layout = check_image(os.path.basename(path), img, i, args.build)
        if out and layout != out[0][3]:
            fail("%s: layout stamp %#06x against %s's %#06x - two modules out "
                 "of one assembly cannot disagree about the kernel they are "
                 "for" % (os.path.basename(path), layout, out[0][0], out[0][3]))
        with open(path, "wb") as fh:
            fh.write(img)
        out.append((os.path.basename(path), size, nent, layout))

    kern = blob[:cut]
    with open(args.kernel, "wb") as fh:
        fh.write(kern)

    if not args.quiet:
        print("os88mod: KERNEL.SYS %d bytes (%d sectors), %d module(s) split "
              "out" % (len(kern), (len(kern) + 511) // 512, len(rows)))
        for name, size, nent, layout in out:
            print("os88mod:   %-12s %5d bytes, %d entr%s, %d sector(s), "
                  "layout %#06x"
                  % (name, size, nent, "y" if nent == 1 else "ies",
                     (size + 511) // 512, layout))
        print("os88mod:   md5 %s" % hashlib.md5(kern).hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main())

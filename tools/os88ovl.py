#!/usr/bin/env python3
"""Split a package's on-demand module out of its assembled image.

SPEC.md 68.10. The Word package assembles to `.text | .modc`, and everything
from `.modc` onward is code that must NOT ship inside WORD.O88: it becomes a
file of its own that the package reads into a heap claim when the feature is
asked for, and frees when it is done.

**Why the module is assembled WITH the package** rather than as a second nasm
run: the module keeps `DS` = the package's segment and reaches the document
through it (SPEC.md 68.10), so every symbol it names has to be the address the
package itself uses. One assembly makes that true by construction. This is
kernel SPEC.md 2.8's shape and its reasoning verbatim - "a second assembly
would make it a claim, and the tree's rule about second opinions is that they
drift."

**How this finds the boundary, without the layout living in two places.** It
does not need a trailer and it is not told by the Makefile. NASM coalesces
every `.text` fragment and places `.modc` after all of them, so the module
starts exactly where the resident image ends - and the resident image's length
is already in the package header at +8, where OS88_HEADER put it and where
tools/os88pkg.py and the loader both read it. So the file describes its own cut
point in a field that three other things already depend on being right.

That is checked rather than assumed: the header's +8 must land inside the file
and must leave a non-empty tail, or this refuses. A package with no `.modc` at
all has no tail and is refused too - being asked to cut a module out of an
image that has none is a build-order mistake, not a no-op.

**`--pad-bss` is the PARTS form of the same cut** (SPEC.md 68.10, 20.12.10).
A package whose image becomes PART 0 of a parted `.O88` has its bss shipped
inside it, because the kernel does not zero a part - so the resident half is
written as image followed by the header's bss in zeros, which is the length
tools/os88pkg.py holds a program part to. Word declares its bss as running up
to where part 1 is assembled, so that length is also where the tail belongs
in memory: the two files are the segment, cut in two.
"""
import argparse
import struct
import sys

MAGIC = 0x384F                  # 'O','8' as a little-endian word
H_MAGIC, H_VER, H_BASE, H_ENTRY, H_IMAGE, H_BSS = 0, 2, 4, 6, 8, 10


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('image', help='the assembled package, e.g. build/word.bin')
    ap.add_argument('-o', '--overlay', required=True, help='the .OVL to write')
    ap.add_argument('--trim', required=True,
                    help='the resident image to write (what os88pkg.py takes)')
    ap.add_argument('--pad-bss', action='store_true',
                    help='append the header\'s bss to the resident image as '
                         'zeros: it is a PART, which the kernel does not zero')
    args = ap.parse_args()

    raw = open(args.image, 'rb').read()
    if len(raw) < 32:
        sys.exit('os88ovl: %s is too short to be a package' % args.image)
    if struct.unpack_from('<H', raw, H_MAGIC)[0] != MAGIC:
        sys.exit('os88ovl: %s does not start with the O8 magic' % args.image)

    cut = struct.unpack_from('<H', raw, H_IMAGE)[0]
    if cut > len(raw):
        sys.exit('os88ovl: header says the image is %d bytes and the file is '
                 'only %d' % (cut, len(raw)))
    if cut == len(raw):
        sys.exit('os88ovl: %s has no .modc tail - nothing to cut. Either the '
                 'module section is empty or this ran on an already-trimmed '
                 'image' % args.image)

    head, tail = raw[:cut], raw[cut:]
    if args.pad_bss:
        head += bytes(struct.unpack_from('<H', raw, H_BSS)[0])
    open(args.trim, 'wb').write(head)
    open(args.overlay, 'wb').write(tail)
    sys.stderr.write('os88ovl: %s -> %s (%d resident) + %s (%d tail)\n'
                     % (args.image, args.trim, len(head),
                        args.overlay, len(tail)))


if __name__ == '__main__':
    main()

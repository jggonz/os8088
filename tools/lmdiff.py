#!/usr/bin/env python3
"""Name the first byte two .WAB bundles differ at, and the SECTION it is in.

WEAVE-SPEC 11.1's gate is byte identity between `weavesim --pack` and LOOM's
on-machine pack, and 12.4 says a compare that fails must name "the first
differing offset and section". "The bundles differ" is not a finding: a
packer disagreement is almost always one field, and which section it lands in
says which of the five compilers to open.

    python3 tools/lmdiff.py REF.WAB GOT.WAB

Prints nothing and exits 0 when the two are identical; otherwise prints the
offset, the section, the two bytes and sixteen bytes of context from each,
and exits 1. It reads the SECTION TABLE of the reference bundle only - the
other one may be malformed, which is itself the answer.
"""
import struct
import sys

SEC = {1: "UISTREAM", 2: "PROPS", 3: "CODE", 4: "ATOMS", 5: "FXCODE",
       6: "CELLS", 7: "SPRITES", 8: "ICON", 9: "SOURCE"}


def sections(b):
    """[(type, offset, length)] from the header at +12 and the table at +32."""
    if len(b) < 32 or b[:4] != b"WAB\x1a":
        return []
    n = b[12]
    out = []
    for i in range(n):
        row = 32 + 8 * i
        if row + 8 > len(b):
            break
        t, _, off, ln, _x = struct.unpack_from("<BBHHH", b, row)
        out.append((t, off, ln))
    return out


def where(secs, off, nsec):
    if off < 32:
        return "the 32-byte header (WEAVE-SPEC 2.2), field at +%d" % off
    if off < 32 + 8 * nsec:
        i = (off - 32) // 8
        return "the section table (2.3), row %d field +%d" % (i, (off - 32) % 8)
    for t, so, ln in secs:
        if so <= off < so + ln:
            return "%s (2.%d), +%d of %d" % (SEC.get(t, "type %d" % t),
                                             3 + t, off - so, ln)
        if so <= off < so + ln + 16:
            return "the 0x00 padding after %s (2.3)" % SEC.get(t, t)
    return "past every section - a tail the format does not have (2.3)"


def dump(b, off):
    lo = max(0, off - 4)
    hi = min(len(b), off + 12)
    return " ".join("%02X" % b[i] for i in range(lo, hi))


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: lmdiff.py REF.WAB GOT.WAB")
    a = open(sys.argv[1], "rb").read()
    b = open(sys.argv[2], "rb").read()
    secs = sections(a)
    nsec = a[12] if len(a) > 12 else 0
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            print("    first difference at +0x%04X (%d), in %s"
                  % (i, i, where(secs, i, nsec)))
            print("    reference %02X   loom %02X" % (a[i], b[i]))
            print("    ref: %s" % dump(a, i))
            print("    got: %s" % dump(b, i))
            sys.exit(1)
    if len(a) != len(b):
        print("    the first %d bytes agree; the reference is %d bytes and "
              "LOOM's is %d" % (n, len(a), len(b)))
        print("    that is %s" % where(secs, n, nsec))
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Build the four `*.O88` files the loader's two size fences are about.

    python3 tests/pkgbig/mkfix.py build/pkgbig

None of them is a package and none is meant to be. Both rules under test
refuse a file before it can be one, so what a gate needs is exactly a `*.O88`
that is not a program.

SPEC.md 19.1's rule types a file by its EXTENSION and its SIZE, before
anything reads a byte of it (`tests/pkgbig.py`):

  BIGPKG.O88   70,144 bytes  - over APP_MAX_SIZE, under PKG_FILE_HI's 1MB.
                               The mount must type it 1 and the LOADER must
                               refuse it, saying `Too large`.
  HUGE.O88  1,048,576 bytes  - exactly PKG_FILE_HI << 16, the first size the
                               mount itself refuses. It must type 0 and reach
                               the loader as `Bad package`.

The pair is the experiment and neither file alone is one: BIGPKG passing on
its own is also what a rule that types EVERYTHING as a package looks like,
and HUGE passing on its own is also what the old `high word == 0` rule looks
like. Only both together say the bound is where it is meant to be.

SPEC.md 21 steps 4 and 6's WRITE BOUND is the second fence, and it is the one
`ld_check_hdr`'s `add dx, ax / jc .toobig` enforces (`tests/pkgfence.py`).
Both operands are separately bounded at APP_MAX_SIZE, so their sum reaches
0x1E000 - seventeen bits - and the compare that used to stand alone there
read a WRAPPED value:

  BSSWRAP.O88  61,440 bytes  - image = bss = 0xF000. The sum wraps to 0xE000,
                               which is BELOW the bound, so the old fence let
                               it through: a 56KB claim and 61,440 bytes read
                               into it, 4KB past the end.
  BSSWORST.O88 61,440 bytes  - image = 0xF000, bss = 0x1001. The sum wraps to
                               1: a ONE KILOBYTE claim, a 120-sector read and
                               60,416 bytes written through whatever
                               mem_claim_hi placed under it - a resident
                               package's code, because it places top-down.

Both must answer LD_EBIG, and this pair is an experiment too: the first is
the symmetric case anybody would try, and the second is the one that shows
the wrap is not a small overrun but an unbounded one.

Every header here is a valid v3 header - magic, version, the dispatcher, a
name - so that the SIZE is the only thing any of them could be refused for.

**The image word is `total & 0xFFFF`, not `min(total, 0xFFFF)`.** It used to
be the clamp, which quietly moved BIGPKG's refusal: 0xFFFF is over
APP_MAX_SIZE, so `ld_check_hdr` refused it on the image bound and the 32-bit
high-word test this fixture exists for was never reached. Truncation is what
the field would really hold, and it is what makes the high word the deciding
comparison. For the two fence fixtures the truncation is an identity - both
files are 0xF000 bytes long - which is what lets them past `cmp ax, [ld_fsz]`
and down to the fence itself.
"""
import os
import struct
import sys

APP_MAX_SIZE = 0xF000           # SPEC.md 3 - the primary segment's image+bss
PKG_FILE_HI = 16                # SPEC.md 3 - the mount's file bound, <1MB

BIG = 70144                     # 137 sectors: over 60KB, well under 1MB
HUGE = PKG_FILE_HI << 16        # 1,048,576 - the first size the mount refuses
FENCE = APP_MAX_SIZE            # 61,440 - image == file, so only bss decides


def header(name: str, total: int, bss: int = 0) -> bytes:
    """A v3 header (SPEC.md 20.2) whose image word is the file's low word.

    The field is 16 bits and two of these files are not, so it cannot always
    be the truth - but TRUNCATION is the lie the format itself would tell,
    where a clamp is one this script invents. See the module docstring: the
    clamp is what made BIGPKG's row assert something other than its subject.
    """
    h = bytearray(32)
    struct.pack_into("<HBBHHHH", h, 0,
                     0x384F,                # magic 'O8'
                     3,                     # version
                     0,                     # flags: no icon, no assoc, no parts
                     0,                     # link base
                     0x20,                  # entry: first byte after the header
                     total & 0xFFFF,        # image size - see the docstring
                     bss)                   # bss
    h[12:16] = bytes((0xFF, 0xD5, 0xCB, 0x00))     # the dispatcher
    h[16:16 + len(name)] = name.encode("ascii")
    return bytes(h)


def write(path: str, name: str, total: int, bss: int = 0) -> None:
    body = header(name, total, bss) + b"\x90" * (total - 32)
    assert len(body) == total
    with open(path, "wb") as f:
        f.write(body)
    print("mkfix: %-13s %9d bytes  image %#06x  bss %#06x"
          % (os.path.basename(path), total, total & 0xFFFF, bss))


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "build/pkgbig"
    os.makedirs(out, exist_ok=True)
    write(os.path.join(out, "BIGPKG.O88"), "BIGPKG", BIG)
    write(os.path.join(out, "HUGE.O88"), "HUGE", HUGE)
    write(os.path.join(out, "BSSWRAP.O88"), "BSSWRAP", FENCE, APP_MAX_SIZE)
    write(os.path.join(out, "BSSWORST.O88"), "BSSWORST", FENCE, 0x1001)
    return 0


if __name__ == "__main__":
    sys.exit(main())

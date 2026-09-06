#!/usr/bin/env python3
"""Paint's three ink classes ARE the kernel's `gfx_inktab` (SPEC.md 39.4, 42.23.1).

    python3 tests/unit/t_inktab.py

A canvas of one bit stores what a 1bpp SCREEN shows, which means Paint has to
agree with the kernel about what each of the sixteen colours looks like there:
solid black, solid white, or the 50% dither.  The kernel says it once, in
`kernel/viddet.inc`'s `gfx_inktab` - 00 / 01 / FF a colour - and Paint says it
again as two bit-masks, `pt_bitEO` and `pt_bitOE`, because a package cannot
read a kernel table at assembly time and reading it at run time would cost a
far call per pixel.

**THE FIRST VERSION OF THOSE MASKS WAS A GUESS AND IT WAS WRONG**, which is
why this file exists.  It was one word, `PT_LIT16` = colours 7..15 white, on
the reasoning that the bright half lights up.  `gfx_inktab` says six of them -
light grey, dark grey, light blue, light green, light cyan and light magenta -
are the DITHER class and only light red, yellow and white are solid.  So a
one-bit canvas stored six colours as flat white that every 1bpp screen in the
system draws as a checkerboard, and nothing in the tree compared the two.

It is a `db` table rather than an `equ`, so `tests/unit/t_mirror.py` - which
maintains its own list by taking every `NAME equ VALUE` - cannot see it.  That
is the whole reason this is a file of its own rather than a row there.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KERN = os.path.join(ROOT, "kernel", "viddet.inc")
PAINT = os.path.join(ROOT, "apps", "paint", "paint.asm")


def inktab():
    """The kernel's 00/01/FF per colour, from the db rows after the label."""
    src = open(KERN).read()
    m = re.search(r"^gfx_inktab:\s*$", src, re.M)
    if not m:
        sys.exit("t_inktab: kernel/viddet.inc has no gfx_inktab label")
    out = []
    for line in src[m.end():].split("\n"):
        line = line.split(";")[0].strip()
        if not line:
            if out:
                break
            continue
        if not line.startswith("db "):
            break
        for tok in line[3:].split(","):
            out.append(int(tok.strip(), 16))
    if len(out) != 16:
        sys.exit("t_inktab: gfx_inktab read as %d entries, wanted 16 - the "
                 "table's shape moved and this reader did not" % len(out))
    return out


def table(label, want_rows):
    """The `db` rows following `label:` in paint.asm, as lists of ints.

    `want_rows` is how many 16-byte halves the label is expected to carry -
    one for pt_cls16, two for each of the bit tables, which are laid out
    twice so pt_line_put's decoder needs no per-pixel test for the phase.
    """
    src = open(PAINT).read()
    m = re.search(r"^%s:\s*$" % label, src, re.M)
    if not m:
        sys.exit("t_inktab: apps/paint/paint.asm has no `%s:` label" % label)
    rows, cur = [], []
    for line in src[m.end():].split("\n"):
        line = line.split(";")[0].strip()
        if not line:
            continue
        if not line.startswith("db "):
            break
        cur = [int(t.strip(), 0) for t in line[3:].split(",")]
        if len(cur) != 16:
            sys.exit("t_inktab: %s has a db row of %d entries, wanted 16"
                     % (label, len(cur)))
        rows.append(cur)
        if len(rows) == want_rows:
            break
    if len(rows) != want_rows:
        sys.exit("t_inktab: %s carries %d rows of 16, wanted %d"
                 % (label, len(rows), want_rows))
    return rows


def main():
    tab = inktab()
    for v in tab:
        if v not in (0x00, 0x01, 0xFF):
            sys.exit("t_inktab: gfx_inktab holds %02X, which is not one of "
                     "SPEC.md 39.4's three classes" % v)
    # gfx_inktab's three classes, and the two bit tables they generate.
    #   cls  0 solid black, 1 the 50% dither, 2 solid white
    #   E    lit where (x+y) is EVEN  -> cls != 0 (white, and the dither's
    #                                    lit phase - sw_pbit's `parity XOR 1`)
    #   O    lit where (x+y) is ODD   -> cls == 2 (white only)
    cls = {0x00: 0, 0x01: 1, 0xFF: 2}
    want_cls = [cls[v] for v in tab]
    want_E = [1 if c else 0 for c in want_cls]
    want_O = [1 if c == 2 else 0 for c in want_cls]

    fails = []
    got_cls = table("pt_cls16", 1)[0]
    if got_cls != want_cls:
        bad = [i for i in range(16) if got_cls[i] != want_cls[i]]
        fails.append("pt_cls16 disagrees with gfx_inktab on colour(s) %s "
                     "(it says %s, the table says %s)"
                     % (bad, [got_cls[i] for i in bad],
                        [want_cls[i] for i in bad]))

    # ...and both orders, because pt_line_put picks the base by the row's
    # parity and reads the second half through `or al, 16`
    for label, halves in (("pt_bitEO", (want_E, want_O)),
                          ("pt_bitOE", (want_O, want_E))):
        got = table(label, 2)
        for n, (g, wnt) in enumerate(zip(got, halves)):
            if g != wnt:
                bad = [i for i in range(16) if g[i] != wnt[i]]
                fails.append("%s half %d disagrees with gfx_inktab on "
                             "colour(s) %s (it says %s, wanted %s)"
                             % (label, n, bad, [g[i] for i in bad],
                                [wnt[i] for i in bad]))
    print("t_inktab: gfx_inktab -> black %s"
          % [i for i, v in enumerate(tab) if v == 0x00])
    print("t_inktab:               dither %s"
          % [i for i, v in enumerate(tab) if v == 0x01])
    print("t_inktab:               white %s"
          % [i for i, v in enumerate(tab) if v == 0xFF])
    for f in fails:
        print("  FAIL: " + f)
    if fails:
        print("t_inktab: %d FAILED - Paint's tables are a MIRROR of the "
              "kernel's (SPEC.md 42.23.1); change them together" % len(fails))
        return 1
    print("t_inktab: PASS - all four 16-byte halves are gfx_inktab, "
          "colour for colour")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""The document page is GENERATED now, so its 32 words are checked against a
golden list rather than read off the screen (SPEC.md 54.3).

    python3 tests/unit/t_assocpage.py

`assoc_page` used to be 32 `dw` words of `.text` that `assoc_compose` copied
whole.  It is now `assoc_pg_seed` - seven irregular words - plus two run
constants, laid down as `rep movsw 3 / rep stosw 13 / rep movsw 4 /
rep stosw 11 / stosw`.  That is 50 bytes of `.text` for 25 of `.cold`, and it
moves the page from DATA, which a diff shows you, into CODE, which it does
not.

WHY THIS EXISTS AS ITS OWN ROW.  `tests/assocglyph.py` is the gate on the
glass, and two of its three assertions compare this kernel against ITSELF -
the seeded glyph against the harvested one, and the cold capture against the
warm one.  A generator that composes the same WRONG page every time satisfies
both of them cleanly, and the icon it is wrong about is on every document in
the system.  Its third assertion (`--ref`) closes that, but only against a
capture taken BEFORE the change, on a 1bpp adapter, under an emulator - which
is a scheduling constraint and a fifteen-minute run, not something a `make`
can do.

So the DATA half is proved here instead, on the host, in a fifth of a second:
read the seed and the two constants out of `kernel/assoc.inc`, replay the five
runs in Python, and compare the result word for word against the thirty-two
words the file carried before the change - which are written out below and are
the only copy of them left in the tree.  A seed word, a run length or a
constant that changes now fails `make` rather than the glass.

What it does NOT cover, and what `tests/assocglyph.py --ref` therefore still
owns: DF, ES, the `push di`/`pop di` around the runs, and the `.inset` loop
that ORs the glyph in afterwards.  This is a proof about the bytes, not about
the routine that emits them.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from harness import check, eq, done            # noqa: E402

ASSOC = os.path.join(HERE, "..", "..", "kernel", "assoc.inc")

# The thirty-two words `assoc_page` carried, transcribed from the `dw` lines
# that F-assoc-09 deleted.  This list is the AUTHORITY - the kernel no longer
# has a copy of it anywhere.  16 mask words then 16 data words, bit 15 = the
# leftmost pixel (SPEC.md 54.3).
GOLDEN = [
    0x3FE0, 0x3FF0, 0x3FF8, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC,
    0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC,
    0x3FE0, 0x2030, 0x2028, 0x203C, 0x2004, 0x2004, 0x2004, 0x2004,
    0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x3FFC,
]


def source():
    return open(ASSOC).read()


def seed(src):
    """The `dw` words between `assoc_pg_seed:` and the blank line after it."""
    m = re.search(r"^assoc_pg_seed:\s*$(.*?)^\s*$", src, re.M | re.S)
    if not m:
        return None
    words = []
    for line in m.group(1).splitlines():
        line = line.split(";")[0]
        d = re.match(r"\s*dw\s+(.*)$", line)
        if d:
            words += [int(x.strip(), 0) for x in d.group(1).split(",") if x.strip()]
    return words


def equ(src, name):
    m = re.search(r"^%s\s+equ\s+(0x[0-9A-Fa-f]+|\d+)" % name, src, re.M)
    return int(m.group(1), 0) if m else None


def runs(src):
    """The five run lengths, read out of assoc_compose in source order.

    Deliberately parsed rather than hard-coded: a `mov cx, 13` that becomes a
    `mov cx, 12` is exactly the edit this row exists to catch, and a constant
    typed here as well would agree with itself.
    """
    m = re.search(r"^assoc_compose:(.*?)^\s*call assoc_glyph_di", src, re.M | re.S)
    if not m:
        return None
    body, out = m.group(1), []
    cx = None
    for line in body.splitlines():
        line = line.split(";")[0]
        c = re.match(r"\s*mov\s+cx\s*,\s*(\d+)\s*$", line)
        if c:
            cx = int(c.group(1))
            continue
        r = re.match(r"\s*rep\s+(movsw|stosw)\s*$", line)
        if r:
            out.append((r.group(1), cx))
            cx = None
            continue
        if re.match(r"\s*stosw\s*$", line):
            out.append(("stosw", 1))
    return out


def main():
    src = source()

    s = seed(src)
    check(s is not None, "assoc_pg_seed is present and is `dw` words",
          "the page frame is generated off this seed; without it there is "
          "nothing for the runs to copy")
    if s is None:
        done("assocpage")
        return
    eq(len(s), 7, "the seed is seven words",
       "three for the mask's cut corner, four for the body's dog-ear - the "
       "only words in the page that are not one of the two run constants")

    mask = equ(src, "ASSOC_PGMASK")
    body = equ(src, "ASSOC_PGBODY")
    eq(mask, 0x3FFC, "ASSOC_PGMASK is 3FFC",
       "a full-width row: the page's two side edges, ink to ink")
    eq(body, 0x2004, "ASSOC_PGBODY is 2004",
       "the same row with a white interior - the ground the glyph is OR'd on")

    r = runs(src)
    check(r is not None, "assoc_compose's runs are readable",
          "this row replays them; if the shape of the routine has changed so "
          "that they cannot be found, the replay below proves nothing")
    if r is None or mask is None or body is None:
        done("assocpage")
        return
    eq([(k, n) for k, n in r],
       [("movsw", 3), ("stosw", 13), ("movsw", 4), ("stosw", 11), ("stosw", 1)],
       "the five runs are movsw 3, stosw 13, movsw 4, stosw 11, stosw",
       "32 words in total: a run length that drifts either overruns the "
       "64-byte destination or leaves the tail of the page uninitialised")

    # Replay, exactly as the 8086 does it: movsw walks the seed, stosw repeats
    # whatever the `mov ax` above it loaded.  The constants alternate
    # mask, body, mask - which is why the last word needs its own `stosw`.
    out, si = [], 0
    const = [None, mask, None, body, mask]
    for i, (kind, n) in enumerate(r):
        if kind == "movsw":
            out += s[si:si + n]
            si += n
        else:
            out += [const[i]] * n
    eq(si, len(s), "the runs consume the whole seed",
       "a seed word the runs never copy is a byte of .text paying for nothing")
    eq(len(out), 32, "the runs emit 32 words",
       "icon_draw16's body is 16 mask words then 16 data words; anything else "
       "writes past assoc_compose's 64-byte destination")
    eq(["%04X" % w for w in out], ["%04X" % w for w in GOLDEN],
       "the generated page is the page that used to be stored",
       "this list is the only copy of the original 32 words left in the tree. "
       "A generator that composes the same wrong page every time passes both "
       "of assocglyph.py's self-consistency assertions, and the icon it is "
       "wrong about is on every document in the system")

    check("assoc_page" not in src, "nothing still names assoc_page",
          "the stored table is gone; a leftover reference would assemble "
          "against whatever label sorts next")

    done("assocpage")


main()

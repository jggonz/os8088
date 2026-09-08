#!/usr/bin/env python3
"""docs/plans/O88-COMPRESSION-PLAN.md 13 wave 0 - the compression formats round-trip.

`tools/os88lz.py` is the REFERENCE implementation of both formats and the
kernel's decoders are the copy, so this row is what makes that claim mean
something: everything it compresses, it decompresses back to the same bytes.

It is the FAST tier, so the corpus is small and fixed. **The point of the
fixed part is the awkward cases, not the volume** - an empty file, one byte,
a file shorter than LZ4's 12-byte match limit, a run of one value long enough
to need extended lengths, and incompressible noise are each a place a length
field or an end-of-block rule is got wrong, and none of them appears in a real
package. `python3 tools/os88lz.py --selfcheck` is the same check over every
binary the tree builds and runs in soak (`lzfmt-all`).

**The in-place margin is asserted here too, at ZERO** (SPEC.md 20.13.7). The
loader reads a compressed image into the TOP of the region it is about to
decompress into, `image - file` above the base, so the writer must never
overtake the reader; every stream ends in a raw tail cut at the peak of
(produced - consumed), which is what makes exactly that placement enough,
and `os88lz.in_place_margin` simulates the decode to check it. A cut that
went wrong would otherwise be found by the loader, at run time, on a machine.

**And the `CZ` container's two refusals** (SPEC.md 20.14.5), because both are
silent: a file that does not get smaller and a file whose PACKED form reaches
64KB are stored plain, the second because `lz_decomp_x` reads its source inside
one segment. BEVERLY.MOD is the subject that matters and it is checked here as
a whole - 116,085 bytes through the container and back - which is also the only
place the tree checks a stream whose OUTPUT crosses a segment without booting
a machine to do it.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import os88lz                                             # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
MARGIN_MAX = 0           # the loader reserves NOTHING above image - file:
                         # the tail is what makes that enough, so a stream
                         # that needs more must say so HERE and not by
                         # corrupting a neighbour's region


def corpus():
    """the awkward cases, then whatever of the real tree happens to be built"""
    yield "empty", b""
    yield "one byte", b"A"
    yield "shorter than MFLIMIT", b"0123456789"
    yield "exactly MFLIMIT", b"0123456789ab"
    yield "one long run", b"\x00" * 5000
    yield "two long runs", b"\xAA" * 3000 + b"\x55" * 3000
    yield "long literal run", bytes((i * 37 + i // 251) & 0xFF
                                    for i in range(600))
    # deterministic pseudo-noise: nothing matches, so every token is literal
    x = 12345
    noise = bytearray()
    for _ in range(4000):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        noise.append((x >> 16) & 0xFF)
    yield "incompressible noise", bytes(noise)
    yield "noise then its own copy", bytes(noise) + bytes(noise)
    for name in ("hello.o88", "mines.o88", "calc.o88"):
        p = os.path.join(ROOT, "build", name)
        if os.path.exists(p):
            yield name, open(p, "rb").read()


# The numbers each side of the compression boundary has to agree on. They are
# spelled DIFFERENTLY on the two sides - the kernel names a decoder's margin,
# a host tool names what it refuses on, and the parts SDK names what it reads
# a run HIGH by - so tests/unit/t_mirror.py, which matches by NAME, cannot see
# any of them. This is the same job for this one
# family, and every row is a pair that assembles and runs cleanly while
# disagreeing.
MIRROR = [
    ("kernel/lz.inc", "LZ_LZ4", "tools/os88lz.py", "LZ4"),
    ("kernel/lz.inc", "LZ_LZB", "tools/os88lz.py", "LZB"),
    ("kernel/lz.inc", "LZ_LZ4", "apps/os88api.inc", "OSAPI_LZ_LZ4"),
    ("kernel/lz.inc", "LZ_LZB", "apps/os88api.inc", "OSAPI_LZ_LZB"),
    ("kernel/disk.inc", "DSK_CZ_MARK", "tools/os88disk.py", "CZ_HINT"),
    ("kernel/disk.inc", "DSK_CZ_HDR", "tools/os88lz.py", "CZ_HDR"),
    ("kernel/disk.inc", "DSK_R_CZM", "tools/os88disk.py", "CZ_H_MARK"),
    ("kernel/disk.inc", "DSK_R_CZH", "tools/os88disk.py", "CZ_H_HI"),
    ("kernel/disk.inc", "DSK_R_CZL", "tools/os88disk.py", "CZ_H_LO"),
]
TUPLE = re.compile(r"^([A-Z_0-9]+(?:\s*,\s*[A-Z_0-9]+)+)\s*=\s*"
                   r"([0-9A-Fa-fxX]+(?:\s*,\s*[0-9A-Fa-fxX]+)+)\s*(?:#|$)",
                   re.M)


def _int(t):
    return int(t, 16) if t.lower().startswith("0x") else int(t, 0)


def constant(rel, name):
    """One integer constant out of a nasm `equ` or a Python assignment.

    The Python side has to understand `A, B = 0, 1` as well as `A = 0`,
    because both spellings are in the tools this checks and a parser that
    quietly failed to find one would report the pair as unchecked - which is
    the answer this whole function exists to avoid giving silently."""
    src = open(os.path.join(ROOT, rel)).read()
    if rel.endswith((".inc", ".asm")):
        pat = r"^%s\s+equ\s+([0-9A-Fa-fxX]+)\s*(?:;|$)" % re.escape(name)
    else:
        pat = r"^%s\s*=\s*([0-9A-Fa-fxX]+)\s*(?:#|$)" % re.escape(name)
    mm = re.search(pat, src, re.M)
    if mm:
        return _int(mm.group(1))
    if not rel.endswith((".inc", ".asm")):
        for tm in TUPLE.finditer(src):
            names = [x.strip() for x in tm.group(1).split(",")]
            if name in names:
                vals = [x.strip() for x in tm.group(2).split(",")]
                if len(vals) == len(names):
                    return _int(vals[names.index(name)])
    return None


def mirrors():
    out = []
    for af, an, bf, bn in MIRROR:
        a, b = constant(af, an), constant(bf, bn)
        if a is None or b is None:
            out.append("%s:%s / %s:%s - one of them is no longer there, so "
                       "nothing is checking the other" % (af, an, bf, bn))
        elif a != b:
            out.append("%s:%s = %d but %s:%s = %d - there is no linker here, "
                       "and both sides assemble and run while disagreeing"
                       % (af, an, a, bf, bn, b))
    return out


def main():
    fails = []
    worst = 0
    n = 0
    for name, data in corpus():
        n += 1
        for fmt in (os88lz.LZ4, os88lz.LZB):
            tag = "%s/%s" % (name, os88lz.NAMES[fmt])
            try:
                z = os88lz.compress(data, fmt)
                back = os88lz.decompress(z, fmt, len(data))
            except Exception as e:                        # noqa: BLE001
                fails.append("%s: raised %s" % (tag, e))
                continue
            if back != data:
                fails.append("%s: round trip differs (%d -> %d -> %d bytes)"
                             % (tag, len(data), len(z), len(back)))
        if data:
            for fmt in (os88lz.LZ4, os88lz.LZB):
                m = os88lz.in_place_margin(data, fmt)
                worst = max(worst, m)
                if m > MARGIN_MAX:
                    fails.append("%s/%s: in-place margin %d exceeds the %d "
                                 "the loader reserves"
                                 % (name, os88lz.NAMES[fmt], m, MARGIN_MAX))

    # --- the CZ container, and the file the feature is for -----------------
    beverly = os.path.join(ROOT, "apps", "tracker", "beverly.mod")
    if os.path.exists(beverly):
        plain = open(beverly, "rb").read()
        blob, did = os88lz.cz_wrap(plain)
        if not did:
            fails.append("BEVERLY.MOD no longer compresses: SPEC.md 20.14.5's "
                         "whole case rests on it")
        elif os88lz.cz_unwrap(blob) != plain:
            fails.append("BEVERLY.MOD does not survive the CZ container")
        else:
            print("t_lzfmt: BEVERLY.MOD %d -> %d bytes (%.1f%%), margin %d, "
                  "output crosses %d segments"
                  % (len(plain), len(blob), 100.0 * len(blob) / len(plain),
                     os88lz.in_place_margin(plain, os88lz.LZ4),
                     len(plain) >> 16))

    # a packed form at or past 64KB is stored PLAIN, not compressed: the
    # decoder's source lives in one segment (SPEC.md 20.14.5). Noise is what
    # makes a big packed form, so this is noise long enough to prove it.
    x, big = 999, bytearray()
    while len(big) < 200000:
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        big.append((x >> 16) & 0xFF)
    blob, did = os88lz.cz_wrap(bytes(big))
    if did:
        fails.append("a %d-byte packed form was accepted: the decoder reads "
                     "its source inside ONE segment" % (len(blob) - 8))
    elif blob != bytes(big):
        fails.append("a refused cz_wrap did not return the input unchanged")

    fails += mirrors()

    print("t_lzfmt: %d subjects x 2 formats, worst in-place margin %d bytes"
          % (n, worst))
    for f in fails:
        print("  FAIL: " + f)
    if fails:
        print("t_lzfmt: %d FAILED - tools/os88lz.py is the reference the "
              "kernel's decoders copy; a break here is a break everywhere"
              % len(fails))
        return 1
    print("t_lzfmt: PASS - both formats round-trip every subject")
    return 0


if __name__ == "__main__":
    sys.exit(main())

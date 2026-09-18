#!/usr/bin/env python3
"""Write a ProTracker M.K. module of a CHOSEN SIZE (SPEC.md 45.3.2).

    python3 tools/os88mkmod.py -o build/BIGMOD.MOD --kb 397
    python3 tools/os88mkmod.py --selfcheck

WHY THIS EXISTS. The heap question SPEC.md 66.4.3 was built for only shows up
at a size, and the sizes that show it are hundreds of KB - a module big enough
that a 640KB machine with three programs open cannot fund it in one run.
`apps/tracker/beverly.mod` is 114KB and every other module in the world is
somebody's copyrighted file, which CONTRIBUTING.md 6 keeps out of this tree.
So the module the gate needs is GENERATED, and the only property the gate
cares about is its length.

**IT IS A REAL MODULE AND NOT A BLOB**, because a file Tracker refuses at
`mp_load` would exercise the claim and then fail the load, which is a green
row reporting on a machine that never played anything. It carries 31 sample
headers, a pattern order, notes in pattern 0 and a sawtooth in sample 1, and
`--selfcheck` re-reads what it wrote against the same invariants
`apps/tracker/trkplay.inc`'s `mp_load` checks - length, the magic at 1080, a
song length of 1..128, and `1084 + 1024*P <= L` over the WHOLE order table,
which is the one that catches a generator that shortened the file and left
the orders alone.

THE SIZE IS EXACT. 1084 + 1024*P + the sample bytes lands on the requested
figure to the byte, with one pad byte appended for an odd target - a MOD
reader stops at the last sample's stated end, so a trailing byte is not part
of the format and not part of the arithmetic either.
"""
import argparse
import os
import struct
import sys

HDR = 1084                      # 20 title + 31*30 samples + 130 + 4 magic
NSAMP = 31
PATSZ = 1024                    # 64 rows * 4 channels * 4 bytes
MAXWORDS = 0xFFFF               # a sample length field is 16 bits of WORDS


def _cell(sample, period, effect=0, param=0):
    """One 4-byte ProTracker cell."""
    return bytes(((sample & 0xF0) | ((period >> 8) & 0x0F),
                  period & 0xFF,
                  ((sample & 0x0F) << 4) | (effect & 0x0F),
                  param & 0xFF))


# C-2 D-2 E-2 G-2 in ProTracker's period table - enough that a row change is
# audible and the mixer has a real period to step with.
NOTES = (428, 381, 339, 285)


def _patterns(n):
    """`n` patterns; pattern 0 plays sample 1 on channel 0 every 8 rows."""
    out = bytearray()
    for p in range(n):
        for row in range(64):
            for ch in range(4):
                if p == 0 and ch == 0 and row % 8 == 0:
                    out += _cell(1, NOTES[(row // 8) % len(NOTES)])
                else:
                    out += _cell(0, 0)
    assert len(out) == n * PATSZ
    return bytes(out)


def _sawtooth(n):
    """`n` bytes of signed 8-bit ramp - not silence, and not a constant."""
    if n <= 0:
        return b""
    period = 64
    return bytes(((i % period) * 4 - 128) & 0xFF for i in range(n))


def _split(total, n, cap):
    """`total` bytes over `n` samples, each EVEN and at most `cap`."""
    if total < 0:
        raise ValueError("no room for sample data")
    if total > n * cap:
        raise ValueError("%d bytes needs more than %d samples of %d"
                         % (total, n, cap))
    out, left = [], total
    for i in range(n):
        want = min(cap, ((left // (n - i)) // 2) * 2)
        out.append(want)
        left -= want
    # the remainder is even by construction (every share was), so it fits in
    # whichever sample still has room
    for i in range(n):
        room = min(cap - out[i], left)
        out[i] += room
        left -= room
    assert left == 0, left
    return out


def build(size, npat=8, title=b"os8088 heap test"):
    """A complete M.K. module of exactly `size` bytes."""
    pad = size & 1
    body = size - pad
    room = body - HDR - npat * PATSZ
    lens = _split(room, NSAMP, MAXWORDS * 2)

    out = bytearray()
    out += title[:20].ljust(20, b"\0")
    for i, n in enumerate(lens):
        name = ("sample %d" % (i + 1)).encode("ascii")[:22].ljust(22, b"\0")
        out += name
        out += struct.pack(">H", n // 2)        # length in WORDS, big-endian
        out += bytes((0, 64))                   # finetune, volume
        out += struct.pack(">HH", 0, 1)         # loop start / length (1 = off)
    assert len(out) == 20 + NSAMP * 30
    out += bytes((npat, 0x7F))                  # song length, restart
    order = bytes(range(npat)) + bytes(128 - npat)
    out += order                                # ...and 128 - npat zeroes,
                                                # which is why P is max+1 over
                                                # the WHOLE table and not the
                                                # song length
    out += b"M.K."
    assert len(out) == HDR
    out += _patterns(npat)
    for n in lens:
        out += _sawtooth(n)
    out += b"\0" * pad
    assert len(out) == size, (len(out), size)
    return bytes(out)


def check(b, why=""):
    """mp_load's own gates, re-derived from apps/tracker/trkplay.inc."""
    p = ("os88mkmod: " + why + ": ") if why else "os88mkmod: "
    if len(b) < HDR:
        raise SystemExit(p + "under 1084 bytes - mp_load answers 'not a MOD'")
    if b[1080:1084] not in (b"M.K.", b"M!K!", b"4CHN", b"FLT4"):
        raise SystemExit(p + "no known magic at 1080")
    songlen = b[950]
    if not 1 <= songlen <= 128:
        raise SystemExit(p + "song length %d is not 1..128" % songlen)
    P = max(b[952:1080]) + 1
    ext = HDR + PATSZ * P
    if ext > len(b):
        raise SystemExit(p + "the order table names %d pattern(s), so the "
                         "block ends at %d and the file is %d - mp_load "
                         "answers 'truncated'" % (P, ext, len(b)))
    stated = sum(struct.unpack(">H", b[20 + i * 30 + 22:20 + i * 30 + 24])[0]
                 * 2 for i in range(NSAMP))
    return {"size": len(b), "patterns": P, "songlen": songlen,
            "sample_bytes": stated, "title": b[:20].split(b"\0")[0].decode(
                "latin-1")}


def selfcheck():
    """Sizes that have each broken a generator of this shape once."""
    rows = [
        (HDR + PATSZ, 1, "one pattern and no samples at all"),
        (120 * 1024, 8, "beverly's rough size"),
        (406354, 8, "SLINGER.MOD to the byte - the odd-length case"),
        (397 * 1024, 8, "the round 397KB the gate asks for"),
        (640 * 1024, 16, "bigger than any 8086 heap, so the cap is exercised"),
        (HDR + PATSZ * 128 + 2, 128, "every order used, one word of sample"),
    ]
    for size, npat, why in rows:
        b = build(size, npat=npat)
        if len(b) != size:
            raise SystemExit("os88mkmod: %s: wrote %d bytes, wanted %d"
                             % (why, len(b), size))
        info = check(b, why)
        print("  ok  %7d bytes  %3d pattern(s)  %7d sample bytes   %s"
              % (info["size"], info["patterns"], info["sample_bytes"], why))
    print("os88mkmod: selfcheck ok - %d size(s)" % len(rows))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--out", help="where to write the module")
    ap.add_argument("--kb", type=int, help="size in KB")
    ap.add_argument("--size", type=int, help="size in BYTES")
    ap.add_argument("--patterns", type=int, default=8)
    ap.add_argument("--title", default="os8088 heap test")
    ap.add_argument("--selfcheck", action="store_true")
    a = ap.parse_args()
    if a.selfcheck:
        selfcheck()
        return 0
    if not a.out:
        ap.error("-o is required unless --selfcheck")
    if (a.kb is None) == (a.size is None):
        ap.error("give exactly one of --kb and --size")
    size = a.size if a.size is not None else a.kb * 1024
    b = build(size, npat=a.patterns, title=a.title.encode("latin-1"))
    info = check(b, os.path.basename(a.out))
    d = os.path.dirname(a.out)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(a.out, "wb") as f:
        f.write(b)
    print("os88mkmod: %s (%d bytes = %d KB, %d patterns, %d sample bytes)"
          % (a.out, info["size"], (info["size"] + 1023) // 1024,
             info["patterns"], info["sample_bytes"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

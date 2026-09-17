#!/usr/bin/env python3
"""build/radrows.inc: tests/unit/radrows.py's hostile-file table as NASM data.

    python3 tests/radtest/mkrows.py build/radrows.inc

It also writes build/radgate/FAN.RAD, FANALL.RAD and HEAVY.RAD - the
fan-out tunes of SPEC.md 96.4.5 deviation 5 and 34.13.6 - for the disks.

One record a row: dw length, dw prefix, then the prefix's bytes - the rest of
the file is zeros, which is how the three rows past 49KB fit a package.
RADGATE's `v` key rebuilds each file in a claim and hands it to verb 4.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "unit"))
import radrows                                           # noqa: E402


def main():
    outdir = os.path.dirname(os.path.abspath(sys.argv[1]))
    for name in ("FAN", "FANALL", "HEAVY"):         # the fan-out tunes, as files
        open(os.path.join(outdir, "radgate", name + ".RAD"), "wb").write(
            getattr(radrows, name))
    out = []
    for name, data, _want, _opl3 in radrows.ROWS:
        body = data.rstrip(b"\0")
        out.append("; %s" % name.replace("\n", " "))
        out.append("    dw %d, %d" % (len(data), len(body)))
        for i in range(0, len(body), 16):
            out.append("    db " + ", ".join("0x%02X" % b for b in body[i:i + 16]))
    open(sys.argv[1], "w").write("\n".join(out) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

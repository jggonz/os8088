#!/usr/bin/env python3
"""nitrace: diff INFONES's 2A03 trace against nestest.log (SPEC.md 91.14.4).

    python3 tools/nitrace.py <serial-capture> <nestest.log> [--limit N]

apps/infones/hosttest/nicputest.asm runs `nestest.nes` from $C000 one
instruction at a time and emits a TWELVE-BYTE RECORD per instruction over the
serial port, before each instruction runs:

    0  2  PC          6  1  P
    2  1  the opcode  7  1  S
    3  1  A           8  4  the running cycle count, little-endian
    4  1  X
    5  1  Y

and `nestest.log` is 8,991 lines of exactly those columns:

    C000  4C F5 C5  JMP $C5F5   A:00 X:00 Y:00 P:24 SP:FD PPU:  0, 21 CYC:7

**THE FIRST DIFFERING LINE NAMES THE EXACT INSTRUCTION THAT IS WRONG**, which
is the whole reason this ROM is the gate a 6502 is built against: a suite that
answers "failed" tells you that something is broken, and this tells you which
opcode, in which addressing mode, with which flag.

The PPU column is NOT compared. This port's PPU is a SCANLINE state machine
(SPEC.md 91.5) and its dot column is not a promise it makes; CYC is, and CYC
is what gates the page-cross penalties, the taken-branch penalties and the
read-modify-write costs.
"""
import argparse
import re
import struct
import sys

REC = 12
HDR = b"NICPUTRACE\n"
END = b"NICPUEND"

LINE = re.compile(
    r"^([0-9A-F]{4})\s+([0-9A-F]{2})"           # PC and the opcode byte
    r".*?A:([0-9A-F]{2})\s+X:([0-9A-F]{2})\s+Y:([0-9A-F]{2})"
    r"\s+P:([0-9A-F]{2})\s+SP:([0-9A-F]{2})"
    r".*?CYC:(\d+)")


def parse_log(path, limit):
    out = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = LINE.match(line.rstrip("\r\n"))
            if not m:
                if line.strip():
                    raise SystemExit("nitrace: cannot parse: %r" % line[:70])
                continue
            out.append((int(m.group(1), 16), int(m.group(2), 16),
                        int(m.group(3), 16), int(m.group(4), 16),
                        int(m.group(5), 16), int(m.group(6), 16),
                        int(m.group(7), 16), int(m.group(8)), line.rstrip()))
            if limit and len(out) >= limit:
                break
    return out


def parse_cap(path):
    data = open(path, "rb").read()
    i = data.find(HDR)
    if i < 0:
        raise SystemExit("nitrace: the capture has no NICPUTRACE header - the "
                         "harness did not reach the trace loop")
    data = data[i + len(HDR):]
    j = data.rfind(END)
    if j >= 0:
        data = data[:j]
    data = data.rstrip(b"\r\n")
    n = len(data) // REC
    out = []
    for k in range(n):
        pc, op, a, x, y, p, s, cl, ch = struct.unpack_from(
            "<HBBBBBBHH", data, k * REC)
        out.append((pc, op, a, x, y, p, s, (ch << 16) | cl))
    return out, (j >= 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("capture")
    ap.add_argument("log")
    ap.add_argument("--limit", type=int, default=0)
    a = ap.parse_args()

    got, finished = parse_cap(a.capture)
    want = parse_log(a.log, a.limit)
    if a.limit:
        got = got[:a.limit]
    n = min(len(got), len(want))
    if n == 0:
        print("nitrace: the capture holds no records at all")
        return 1

    names = ("PC", "opcode", "A", "X", "Y", "P", "SP", "CYC")
    for i in range(n):
        g = got[i]
        w = want[i]
        for f in range(8):
            if g[f] != w[f]:
                print("nitrace: line %d of %d DIFFERS in %s"
                      % (i + 1, len(want), names[f]))
                print("  log   %s" % w[8])
                print("  ours  %04X  %02X        A:%02X X:%02X Y:%02X "
                      "P:%02X SP:%02X CYC:%d"
                      % (g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7]))
                if i:
                    print("  the instruction BEFORE it, which was right:")
                    print("        %s" % want[i - 1][8])
                return 1
    if len(got) < len(want):
        print("nitrace: %d record(s) against the log's %d - the run stopped "
              "early" % (len(got), len(want)))
        return 1
    if not finished:
        print("nitrace: the capture has no NICPUEND trailer - the run was cut "
              "off")
        return 1
    print("nitrace: %d instructions, every PC, opcode, A, X, Y, P, SP and CYC "
          "identical to nestest.log" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())

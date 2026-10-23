#!/usr/bin/env python3
"""Diff RealDMG's core against Peanut-GB, one instruction at a time.

    python3 tools/gbtrace.py ROM [--count N] [--context K]

docs/plans/REALDMG-PLAN.md wave 1's differential gate. The reference is
Peanut-GB at tools/gbprof.py's pinned commit, built with tools/gbprof/gbtrace.c
so it prints the registers before every instruction. The core under test is
an RDG_TRACE build booted bare in QEMU through tests/rdgcore/rdgimg.py, whose
rdg_trline sends the same 14-byte record on COM1 (PC, A, F, BC, DE, HL, SP and
the time in M-cycles, so the first wrong cycle count is a difference too). QEMU only counts work, which is all
this asks of it (docs/TESTING.md). The two streams are compared as they
arrive, so ten million instructions need no 300MB files. An RDG_TRACE core
skips no idle loop, so every iteration is in both streams.

Exit 0 if the first N instructions agree; otherwise prints the first
difference with K lines of shared history and exits 1.
"""
import argparse
import os
import select
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gbprof  # noqa: E402

REC = 14


def fmt(rec):
    if rec is None:
        return "(stream ended)"
    pc, a, f, bc, de, hl, sp, t = struct.unpack("<HBBHHHHH", rec)
    return "%04X  %02X %02X %04X %04X %04X %04X %04X" % (pc, a, f, bc, de, hl, sp, t)


def read_core(stream, n, stall=20.0):
    """n bytes of the core's stream, or fewer if it stops for `stall` seconds -
    a core that has died (a bad opcode halts the harness) is a difference,
    never a hang."""
    fd, out = stream.fileno(), b""
    while len(out) < n:
        r, _, _ = select.select([fd], [], [], stall)
        if not r:
            break
        b = os.read(fd, n - len(out))
        if not b:
            break
        out += b
    return out


def build_ref():
    gbprof.patch(gbprof.fetch())
    exe = os.path.join(gbprof.WORK, "gbtrace")
    cc = os.environ.get("CC", "cc")
    subprocess.check_call([cc, "-O2", "-w", "-I", gbprof.WORK, "-o", exe,
                           os.path.join(ROOT, "tools", "gbprof", "gbtrace.c")])
    return exe


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("rom")
    ap.add_argument("--count", type=int, default=10_000_000)
    ap.add_argument("--context", type=int, default=12)
    ap.add_argument("--patch", action="append", metavar="OFF:HEX",
                    help="break the core on purpose (tests/rdgcore/rdgimg.py --patch)")
    a = ap.parse_args()
    ref = build_ref()
    img = os.path.join(ROOT, "build", "realdmg", "trace.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tests/rdgcore/rdgimg.py"),
                           a.rom, "-o", img, "--mode", "trace", "--frames", "65535"]
                          + sum([["--patch", p] for p in a.patch or ()], []),
                          stdout=subprocess.DEVNULL)
    rp = subprocess.Popen([ref, a.rom, str(a.count)], stdout=subprocess.PIPE,
                          bufsize=1 << 20)
    qp = subprocess.Popen(["qemu-system-i386", "-drive", "file=%s,format=raw,if=floppy" % img,
                           "-boot", "a", "-display", "none", "-serial", "stdio",
                           "-monitor", "none", "-no-reboot"],
                          stdout=subprocess.PIPE, stdin=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL, bufsize=0)
    n, t0, bad, hist = 0, time.time(), None, b""
    chunk = REC * 8192
    try:
        while n < a.count and bad is None:
            want = min(chunk, (a.count - n) * REC)
            r = rp.stdout.read(want)
            c = read_core(qp.stdout, len(r)) if r else b""
            if not r:
                break
            if c == r:
                n += len(r) // REC
                hist = r[-a.context * REC:]
                if n // 1_000_000 != (n - len(r) // REC) // 1_000_000:
                    print("gbtrace: %d agree (%.0fs)" % (n, time.time() - t0), flush=True)
                continue
            for i in range(0, len(r), REC):
                if c[i:i + REC] != r[i:i + REC]:
                    hist = (hist + r[:i])[-a.context * REC:]
                    n += i // REC
                    bad = (r[i:i + REC], c[i:i + REC] if len(c) >= i + REC else None)
                    break
    finally:
        qp.kill()
        rp.kill()
        qp.wait()
        rp.wait()
    hist = [hist[i:i + REC] for i in range(0, len(hist), REC)]
    if bad is None:
        print("gbtrace: %s - the first %d instructions agree with Peanut-GB (%.0fs)"
              % (os.path.basename(a.rom), n, time.time() - t0))
        return 0
    print("gbtrace: %s DIFFERS at instruction %d" % (os.path.basename(a.rom), n))
    print("            PC    A  F  BC   DE   HL   SP   TIME")
    for h in hist:
        print("  both      " + fmt(h))
    print("  Peanut-GB " + fmt(bad[0]))
    print("  RealDMG   " + fmt(bad[1]))
    return 1


if __name__ == "__main__":
    sys.exit(main())

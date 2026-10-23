#!/usr/bin/env python3
"""RealDMG's core against Peanut-GB, instruction for instruction.

    python3 tests/gbtrace.py [--count N]

docs/plans/REALDMG-PLAN.md wave 2's `gbtrace` row: tools/gbtrace.py over the
first N instructions (default 1,000,000, about two minutes) of Blargg's
combined cpu_instrs, every register and the M-cycle clock compared before
every instruction. The plan's wave 1 measurement is 10,000,000; that is
`--count 10000000` and about twenty minutes, which is why the row takes a
tenth of it.

THE NEGATIVE CONTROL: the same run with JR NZ broken - its handler replaced
by a bare dispatch, so it neither jumps nor consumes its operand - must
DIFFER, and early. A diff that cannot see that sees nothing
(docs/WRITING-TESTS.md 1).
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JRNZ_NOP = "2000:8A3446FFE2"       # mov dh,[si] / inc si / jmp dx


def gbtrace(rom, count, patch=None):
    cmd = [sys.executable, os.path.join(ROOT, "tools/gbtrace.py"), rom, "--count", str(count)]
    if patch:
        cmd += ["--patch", patch]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--count", type=int, default=1_000_000)
    a = ap.parse_args()
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/getgbtest.py")])
    rom = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools/getgbtest.py"),
                                   "--path", "cpu_instrs/cpu_instrs"], text=True).strip()
    rc, out = gbtrace(rom, a.count)
    print(out.rstrip())
    crc, cout = gbtrace(rom, 100_000, JRNZ_NOP)
    caught = crc != 0 and "DIFFERS" in cout
    print("control: JR NZ broken - %s" % ("differs, as it must" if caught
                                          else "AGREED - the diff cannot go red"))
    ok = rc == 0 and caught
    print("gbtrace: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

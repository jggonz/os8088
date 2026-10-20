#!/usr/bin/env python3
"""SPKBENCH on MartyPC - the reference the owner's machines are read against.

    make spkbench && python3 tests/spkbench.py [--machine NAME]

Boots the 360 KB system disk with build/spkbench360.img in B:, opens
SPKBENCH.O88, presses R and waits for it to save its report, then prints
the report out of the package's own memory (tests/benchlib.inc's arena) - so
no file has to be flushed off the emulated floppy to read it.

What must hold (a machine that ran the bench at all):
  1. the shaper did work with the speaker shut, and at every rate the ISR's
     share of the machine is between 5% and 90% - a share of 0 is a door that
     never opened, one of 100% a workload that never ran;
  2. the share RISES with the rate (4,800 < 5,512 < 8,000): the ISR runs
     once a sample, so a bench whose shares do not rise measured something
     else;
  3. every one of the ten RAM banks has its row.

Broken on purpose - sp_brk's `call os88spk_go` replaced by three nops, so
the door never opens: every share reads 0 and 1 and 2 FAIL. What the row
does NOT gate is the numbers themselves: it proves the bench RUNS and its
shares have the right shape. The measurement is the field run, read against
the MartyPC reference printed here (SPEC.md 45.25.1).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui                               # noqa: E402
import os88geom as geom                                # noqa: E402
from cycweb import pkg_syms                            # noqa: E402

MACHINE = "os8088_5150_herc_gla"


def u16(b, o=0):
    return b[o] | b[o + 1] << 8


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    ap.add_argument("--ibm", action="store_true",
                    help="run the IBM ROM itself where the machine has a "
                    "GLaBIOS twin (the ROM is not in the tree)")
    ap.add_argument("--apps", default="build/spkbench360.img")
    ap.add_argument("--define", action="append", default=[],
                    help="a define the disk's SPKBENCH was built with, so its "
                    "symbols are the running package's")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("tests/spkbench/spkbench.asm", ("apps/", "tests/"),
                       a.define)
    bad = []
    with os88ui.boot("build/os8088-360.img", apps=a.apps,
                     machine=a.machine,
                     why_ibm="the owner's own 5150 ROM" if a.ibm else None) \
            as ui:
        m = ui.m
        w = ui.path("B:/SPKBENCH.O88")
        rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = (rec[geom.W_SEG] | rec[geom.W_SEG + 1] << 8) << 4
        rb = lambda n: m.read(base + syms[n], 1)[0]
        m.type_text("r")
        os88marty.until(m, lambda mm: rb("bl_saved") == 1, "the report",
                        poll=0.5, limit=600.0, guest=120.0)
        n = u16(m.read(base + syms["bl_nrow"], 2))
        idx = m.read(base + syms["bl_idx"], 2 * n)
        lines = []
        for i in range(n):
            off = base + syms["bl_arena"] + u16(idx, 2 * i)
            lines.append(m.read(off, 90).split(b"\0")[0].decode("ascii",
                                                               "replace"))
    for ln in lines:
        print("   | " + ln)
    val = {}
    rate = None
    shares = []
    for ln in lines:
        f = ln.split()
        if not f:
            continue
        try:
            v = int(f[-1])
        except ValueError:
            continue
        if ln.startswith("rate, Hz"):
            rate = v
        elif "ISR share" in ln:
            shares.append((rate, v))
        elif "Tracker assumes" in ln:
            val.setdefault("assume", []).append(v)
        elif ln.startswith("shaper spans (256) done"):
            val["n0"] = v
    banks = sum(1 for ln in lines if ln.startswith("bank "))
    asm = val.get("assume", [])
    print("   the shaper, speaker shut: %s spans; the ISR's share: %s"
          % (val.get("n0"), ", ".join(
              "%d Hz %.1f%% (Tracker assumes %.1f)"
              % (r, s / 10.0, (asm[i] if i < len(asm) else 0) / 10.0)
              for i, (r, s) in enumerate(shares))))
    if not val.get("n0") or len(shares) != 3 or \
            not all(50 <= s <= 900 for r, s in shares):
        bad.append("1: the speaker rows did not run as they should")
    if len(shares) == 3 and not shares[0][1] < shares[1][1] < shares[2][1]:
        bad.append("2: the ISR's share does not rise with the rate")
    if banks != 10:
        bad.append("3: %d RAM bank rows, not 10" % banks)
    for b in bad:
        print("   FAIL " + b)
    print("spkbench: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

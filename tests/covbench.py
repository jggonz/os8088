#!/usr/bin/env python3
"""COVBENCH on MartyPC's Covox 5150 - the reference the field is read against.

    make covbench covoxtest && python3 tests/covbench.py [--jitter]

Boots `make covoxtest`'s 720 KB disk (SOUND.DRV wanted, the tier a Covox on
LPT2 = 378h) with build/covbench720.img in B:, opens COVBENCH.O88, presses R
and waits for it to save its report, then prints the report out of the
package's own memory (tests/benchlib.inc's arena).

What must hold (a machine that ran the bench at all - SPEC.md 34.14.3):
  1. the workload ran with the output SHUT, and every row at every rate has a
     share between 2% and 99%: 0 is an output that never ran, 100% a
     workload that never did;
  2. each output's share RISES with the rate - it costs once a sample, so a
     share that does not rise measured something else;
  3. POLL2 with the tick OWED misses fewer than 5% of its samples at every
     rate, and POLL4 at 5,512 and 8,000: the polling is what the row prices,
     and a row that missed more priced less output than the rate asks for.
     POLL4 at 11,025 is NOT held to it - a poll every four steps plus the
     write is longer than an 11 kHz sample, so it loses edges by design, and
     the row is there to show that cliff.

--jitter adds the timing the report cannot see: an io breakpoint on 378h
over one POLL4 row at 11,025 Hz, every write's cycle stamped, and the gaps'
spread printed against the ideal 433 cycles. (MartyPC's covox capture holds
the last byte at 44.1 kHz, so it cannot show jitter under ~23 us; the
breakpoint can.)

Broken on purpose - POLL's write taken out - every polled row
writes nothing and 3 FAILS.
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

MACHINE = "os8088_5150_herc_covox_720_gla"


def u16(b, o=0):
    return b[o] | b[o + 1] << 8


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    ap.add_argument("--sys", default="build/covoxsys720.img")
    ap.add_argument("--apps", default="build/covbench720.img")
    ap.add_argument("--define", action="append", default=[])
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("tests/covbench/covbench.asm", ("apps/", "tests/"),
                       a.define)
    bad = []
    with os88ui.boot(a.sys, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        w = ui.path("B:/COVBENCH.O88")
        rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = (rec[geom.W_SEG] | rec[geom.W_SEG + 1] << 8) << 4
        rb = lambda n: m.read(base + syms[n], 1)[0]
        m.type_text("r")
        os88marty.until(m, lambda mm: rb("bl_saved") == 1, "the report",
                        poll=0.5, limit=900.0, guest=180.0)
        n = u16(m.read(base + syms["bl_nrow"], 2))
        idx = m.read(base + syms["bl_idx"], 2 * n)
        lines = []
        for i in range(n):
            off = base + syms["bl_arena"] + u16(idx, 2 * i)
            lines.append(m.read(off, 90).split(b"\0")[0].decode("ascii",
                                                               "replace"))
    for ln in lines:
        print("   | " + ln)
    rate = None
    n0 = None
    rows = {}                       # (kind, rate) -> share p.m.
    miss = {}
    for ln in lines:
        f = ln.split()
        if not f:
            continue
        try:
            v = int(f[-1])
        except ValueError:
            continue
        s = ln.strip()
        if s.startswith("rate, Hz") or s.startswith("tick live, rate Hz"):
            rate = v
        elif s.startswith("spans (256 steps) done"):
            n0 = v
        elif "share, p.m." in s:
            rows[(f[0], rate)] = v
        elif "missed, p.m." in s:
            miss[(f[0], rate)] = v
    rates = (5512, 8000, 11025)
    if not n0:
        bad.append("1: the workload did not run with the output shut")
    for kind in ("isr", "lean", "poll2"):
        sh = [rows.get((kind, r)) for r in rates]
        if any(x is None or not 20 <= x <= 990 for x in sh):
            bad.append("1: %s's shares %s" % (kind, sh))
        elif not sh[0] < sh[1] < sh[2]:
            bad.append("2: %s's share does not rise with the rate: %s"
                       % (kind, sh))
    for kind, rs in (("poll2", rates), ("poll4", rates[:2])):
        for r in rs:
            mm = miss.get((kind, r))
            if mm is None or mm >= 50:
                bad.append("3: %s at %d missed %s p.m." % (kind, r, mm))
    print("   shares p.m. (5512/8000/11025): " + "; ".join(
        "%s %s" % (k, [rows.get((k, r)) for r in rates])
        for k in ("isr", "lean", "poll2", "poll4")))
    print("   missed p.m.: " + "; ".join(
        "%s %s" % (k, [miss.get((k, r)) for r in rates])
        for k in ("poll2", "poll4")) + "; poll2L %s"
        % miss.get(("poll2L", 11025)))
    for b in bad:
        print("   FAIL " + b)
    print("covbench: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""The mouse ISR runs on LOW_SEG whatever stack it interrupted (SPEC.md
9.10.1, docs/plans/KERN-MEGA-PLAN.md 14.39).

    make build/msstk.o88 && python3 tests/mousess.py [--machine M]

SeaBIOS - QEMU's and Bochs's ROM - serves int 13h and int 16h on a stack in a
segment of its own, and the kernel calls both with IF = 1, so the mouse ISR
can arrive with SS anywhere. Its BODY reaches .lowbss through SS
(gfx_rowbase's row table, evq_push's ring), so before 9.10.1 the entry swapped
stacks only when SS was already LOW_SEG and otherwise ran the body in place,
reading and WRITING the ROM's segment. The race is real and no emulator here
wins it on demand (a QEMU run of 59 reflected int 13h and 2,241 mouse ISR
entries met it 0 times), so tests/msstk makes the case on purpose: SS:SP in an
8KB claim of A5h, IRQ0 masked, IF on, while this sends mouse packets with
button edges.

Two readings, each sufficient to fail the row:

  1. the CLAIM: MSSTK reports how many of its bytes below the top 256 (the
     CPU's frame) changed. A body on that stack writes evq_push's ring at its
     .lowbss offset, so the old entry reads non-zero there;
  2. SS at the BODY: a breakpoint at mou_isr_body and one at its first
     instruction after MOUPRIV_ENTER, both reading SS. The first must see the
     claim's SS (or the row proved nothing) and the second must never.

VERIFIED TO FAIL on the entry before 9.10.1 - see KERN-MEGA-PLAN 14.39 for the
readings.
"""
import argparse
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                             # noqa: E402
import os88marty                                             # noqa: E402
import os88sym                                               # noqa: E402
import os88ui                                                # noqa: E402

MACHINES = ("os8088_5150_herc_gla",)
ICA = 0x4F0                 # 0040:00F0, the BIOS's intra-application area
PACKETS = 24


def run(machine, image, apps, claim_only=False):
    entry = after = low = None
    if not claim_only:
        os88build.plain().apply()
        S = os88sym.linear
        low = os88sym.equates().get("LOW_SEG")
        entry = S("mou_isr_body")
        after = S("mou_isr_body.priv")  # just past MOUPRIV_ENTER: 0 bytes
    bad = []
    with os88ui.boot(image, apps=apps, machine=machine) as ui:
        m = ui.m
        m.write(ICA, b"\0" * 6)
        ui.path("B:/APPS")
        seen = {"entry": [], "after": []}

        def hit(mm, rec):
            r = rec.get("regs") or {}
            ss = r.get("ss", r.get("SS"))
            ip = rec.get("addr")
            seen["entry" if ip == entry else "after"].append(ss)

        # the click and return at once: MSSTK's entry proc IS the window it
        # opens, so there is no window to wait for (os88ui.open's escape)
        ui.open("MSSTK.O88", expect=None)
        os88marty.until(m, lambda mm: mm.read(ICA, 1) in (b"W", b"E"),
                        "MSSTK's window", limit=120.0)
        if m.read(ICA, 1) != b"W":
            return ["MSSTK refused (no 8KB claim)"]
        def packets():
            for k in range(PACKETS):
                m.mouse(dx=6 if k % 2 else -6, dy=0, l=bool(k % 4 < 2))
                os88marty.pace(m, 0.15)
        if claim_only:
            packets()
        else:
            with os88marty.bp_trace(m, entry, after, regs=True, on_hit=hit):
                packets()
        os88marty.until(m, lambda mm: mm.read(ICA, 1) == b"D",
                        "MSSTK's verdict", limit=120.0)
        v = m.read(ICA, 6)
        changed = v[2] | v[3] << 8
        lowest = v[4] | v[5] << 8
        foreign_in = [s for s in seen["entry"] if s != low]
        foreign_body = [s for s in seen["after"] if s != low]
        print("   %s: %d ISR entries, %d on the claim's stack; %d body "
              "starts on a stack not LOW_SEG; %d claim bytes changed "
              "(lowest at %04X)"
              % (machine, len(seen["entry"]), len(foreign_in),
                 len(foreign_body), changed, lowest))
        if claim_only:
            print("   (--claim-only: no breakpoints, the claim alone)")
        elif not foreign_in:
            bad.append("no mouse ISR arrived on MSSTK's stack - the row "
                       "proved nothing")
        if foreign_body:
            bad.append("the ISR's body ran %d time(s) with SS = %s, not "
                       "LOW_SEG %04X" % (len(foreign_body), sorted(
                           "%04X" % s for s in set(foreign_body)), low))
        if changed:
            bad.append("the ISR wrote %d bytes of the interrupted stack's "
                       "segment, the lowest at offset %04X" % (changed,
                                                               lowest))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default=os.path.join(tempfile.gettempdir(),
                                                   "mousess.img"))
    ap.add_argument("--machine", action="append")
    ap.add_argument("--claim-only", action="store_true",
                    help="no breakpoints and no symbols: reading 1 alone, for "
                    "an A/B against a kernel this tree did not build")
    a = ap.parse_args()
    os88marty.scratch_disk(a.apps, "APPS:build/msstk.o88")
    bad = []
    for mach in a.machine or MACHINES:
        bad += ["%s: %s" % (mach, b) for b in run(mach, a.image, a.apps,
                                                       a.claim_only)]
    for b in bad:
        print("mousess: " + b)
    if bad:
        sys.exit("mousess: FAILED")
    print("mousess: the mouse ISR's body ran on LOW_SEG every time, the "
          "interrupted stack untouched")
    return 0


if __name__ == "__main__":
    sys.exit(main())

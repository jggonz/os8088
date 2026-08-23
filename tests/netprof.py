#!/usr/bin/env python3
"""The ETHPROF=1 driver, exercised - nothing ran it until a bracket bug shipped.

    python3 tests/netprof.py

The stage profiler (SPEC.md 72.15) is compiled in only under ETHPROF=1, and
for a whole release no test ever booted that build: tests/ethernet.py grew an
ether_defines() hook for exactly this and the hook had zero callers. What
that cost was ether-2 of the post-squash review: prof_beg/prof_end ran
`cmp byte [prof_on], 0` BEFORE `pushf`, so the caller's flags died on both
the on and OFF paths - every verb's CF answer corrupted, a refusing socket
verb read as success, in any profiled build, with profiling merely compiled
in and switched off. The plain build was untouched, which is why nothing
green ever went red.

So this harness is the OFF-path gate: it builds the ethertest disk with
ETHPROF=1 and drives tests/ethernet.py's whole flow through it - DHCP, the
ring claim, the browser's page fetch. On the unfixed driver the fetch dies
on the first corrupted CF; on the fixed one the profiled driver is
indistinguishable from the plain one, which is the contract (SPEC.md 72.15:
"a machine that is not asked, pays only the compare").

The ON path (prof_on=1 during a transfer, counters read back) is netbench's
job under `make netbench` and stays there - MartyPC has no NIC and QEMU's
milliseconds are the host's, so the numbers are only real on 86Box or the
5150; here only the CALLS would be checkable, and the OFF path is where the
regression lived.

UNREGISTERED (tests/unit/t_registry.py says why): it needs QEMU, and it
leaves KNOB builds of ether.drv and the ethertest disk in build/ - the next
plain `make` restores them (ETHSTAMP is stamp-tracked), but a suite row
running between the two would test the wrong driver.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, "tools")

import ethernet                                            # noqa: E402


def main():
    # The knob travels by ENVIRONMENT on purpose: ethernet.main() re-runs
    # `make ethertest` itself (its image must be rebuilt, not checked - see
    # its own comment), and env is how the knob survives that inner make.
    os.environ["ETHPROF"] = "1"
    ethernet.ether_defines("ETHPROF")
    print("netprof: driving tests/ethernet.py against the ETHPROF=1 driver")
    print("         (the OFF path: profiling compiled in, switched off)")
    sys.argv = ["ethernet.py"]
    try:
        ethernet.main()
    except SystemExit as e:
        if e.code not in (None, 0):
            print("netprof: the profiled driver FAILED the plain driver's "
                  "gate - the brackets are not transparent")
            raise
    print("netprof: ok - the profiled driver passed the plain driver's gate")
    print("netprof: NOTE build/ now holds the ETHPROF driver and disk; the "
          "next `make` puts the shipped ones back")
    return 0


if __name__ == "__main__":
    sys.exit(main())

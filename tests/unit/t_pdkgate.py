#!/usr/bin/env python3
"""Every kernel call a PageDesk module makes goes through its gate
(PAGEDESK-SPEC 20.15, DECISIONS D23).

    python3 tests/unit/t_pdkgate.py [build-dir]

A module verb runs on the private module stack, and the kernel cannot run
there (it reaches .lowbss through SS), so apps/pagedesk/pdkgate.inc routes
every `call OSAPI_*` through a slot that swaps back up to the UI stack. The
assembly already refuses a slot it does not know (KERNEL_SEG is re-defined to
an undefined symbol after the table); this reads the BUILT modules and holds
the shape that cannot be seen in the source: every far call to KERNEL_SEG
(9A off 60 00) sits in a slot - a near call before it to ONE routine (pdk_up)
and a jump after it to ONE routine (pdk_dn) - in PDENG, PDREND, PDWRT and the
stand-ins, whichever are built. ~0.1 s. SKIPs when no module is built.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KSEG = 0x0060


def slots(data):
    """(far-call offset, near-call target, jump target) for every 9A ...60 00"""
    out = []
    for i in range(3, len(data) - 6):
        if data[i] != 0x9A or struct.unpack_from("<H", data, i + 3)[0] != KSEG:
            continue
        up = None
        if data[i - 3] == 0xE8:
            up = (i + struct.unpack_from("<h", data, i - 2)[0]) & 0xFFFF
        dn = None
        j = i + 5
        if data[j] == 0xEB:
            dn = (j + 2 + struct.unpack_from("<b", data, j + 1)[0]) & 0xFFFF
        elif data[j] == 0xE9:
            dn = (j + 3 + struct.unpack_from("<h", data, j + 1)[0]) & 0xFFFF
        out.append((i, up, dn))
    return out


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build")
    mods = [os.path.join(build, n) for n in
            ("PDENG.MOD", "PDREND.MOD", "PDWRT.MOD",
             "pdstub/PDENG.MOD", "pdstub/PDREND.MOD", "pdstub/PDWRT.MOD")]
    seen = [m for m in mods if os.path.exists(m)]
    if not seen:
        print("SKIP: no PageDesk module built under " + build)
        return 0
    for m in seen:
        data = open(m, "rb").read()
        name = os.path.relpath(m, build)
        s = slots(data)
        ups = {u for _, u, _ in s}
        dns = {d for _, _, d in s}
        bad = [hex(i) for i, u, d in s if u is None or d is None]
        check(not bad, "%s: every far call to KERNEL_SEG is in a gate slot%s"
              % (name, "" if not bad else " - bare at " + ", ".join(bad)))
        check(len(ups) <= 1 and len(dns) <= 1,
              "%s: %d slot(s), one pdk_up and one pdk_dn (%s / %s)"
              % (name, len(s), sorted(hex(u) for u in ups if u is not None),
                 sorted(hex(d) for d in dns if d is not None)))
    return done("t_pdkgate")


if __name__ == "__main__":
    sys.exit(main())

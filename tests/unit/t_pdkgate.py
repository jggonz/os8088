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
(9A off 60 00), and every RARE DOOR call (CD 81 index, SPEC.md 20.3.4), sits in a slot - a near call before it to ONE routine (pdk_up)
and a jump after it to ONE routine (pdk_dn) - in PDENG, PDREND, PDWRT, the slot's
occupants (PDXK PDXG PDXF PDXI PDXJ) and the stand-ins, whichever are built. ~0.1 s. SKIPs when no module is built.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KSEG = 0x0060


RARE = 0x81                                 # os88api.inc's OS88_RARE


def slots(data):
    """(call offset, near-call target, jump target) for every kernel call: a
    far call 9A ...60 00 to a table cell, or the RARE DOOR's `int 81h` + its
    index byte (SPEC.md 20.3.4) - a door call never names KERNEL_SEG, so the
    far-call scan alone would not see one made bare on the deep stack"""
    out = []
    for i in range(3, len(data) - 6):
        if data[i] == 0x9A and struct.unpack_from("<H", data, i + 3)[0] == KSEG:
            j = i + 5
        elif data[i] == 0xCD and data[i + 1] == RARE:
            j = i + 3
        else:
            continue
        up = None
        if data[i - 3] == 0xE8:
            up = (i + struct.unpack_from("<h", data, i - 2)[0]) & 0xFFFF
        dn = None
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
             "pdstub/PDENG.MOD", "pdstub/PDREND.MOD", "pdstub/PDWRT.MOD")
            # the slot's occupants (PAGEDESK-SPEC 20.22.2.4), real and stand-in
            + tuple(d + "PDX%s.MOD" % k for d in ("", "pdstub/") for k in "KGFIJ")]
    seen = [m for m in mods if os.path.exists(m)]
    # PageDesk is not in `all`, so a module under build/ can be a leftover
    # from before the gate existed; one older than pdkgate.inc is that, and
    # is skipped by name rather than failed (the next `make pagedesk`
    # rebuilds it - every .MOD depends on pdkgate.inc)
    gate = os.path.join(ROOT, "apps", "pagedesk", "pdkgate.inc")
    gm = os.path.getmtime(gate) if os.path.exists(gate) else 0
    for m in [m for m in seen if os.path.getmtime(m) < gm]:
        print("SKIP %s: built before pdkgate.inc (stale; make pagedesk)"
              % os.path.relpath(m, build))
    seen = [m for m in seen if os.path.getmtime(m) >= gm]
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
        check(not bad, "%s: every kernel call (far or door) is in a gate slot%s"
              % (name, "" if not bad else " - bare at " + ", ".join(bad)))
        check(len(ups) <= 1 and len(dns) <= 1,
              "%s: %d slot(s), one pdk_up and one pdk_dn (%s / %s)"
              % (name, len(s), sorted(hex(u) for u in ups if u is not None),
                 sorted(hex(d) for d in dns if d is not None)))
    # PDREND's second code segment (PAGEDESK-SPEC 20.22.7.9) runs under a
    # different CS, so a near call to pdk_up cannot reach the gate from it:
    # moved code reaches the kernel only through a resident routine (its
    # cc_ovv_* shims), and the part must hold no far call to KERNEL_SEG at all
    for c in (os.path.join(build, "PDRENDC.MOD"),):
        if os.path.exists(c) and os.path.getmtime(c) >= gm:
            s = slots(open(c, "rb").read())
            check(not s, "%s: no kernel call in the second code segment%s"
                  % (os.path.relpath(c, build),
                     "" if not s else " - at " + ", ".join(hex(i) for i, _, _ in s)))
    return done("t_pdkgate")


if __name__ == "__main__":
    sys.exit(main())

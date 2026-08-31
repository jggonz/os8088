#!/usr/bin/env python3
"""kern_big fully RESIDES in 128KB at the desktop (kernel.asm rule 3)

    make && python3 tests/kernresident.py [--machine os8088_xt_vga]

The memory rule set is three sentences and each names a different quantity:

  1. kern_small BOOTS on 128KB.        guard 5, MIN_RAM_KB
  2. kern_big BOOTS on 196KB.          guard 5, MIN_RAM_KB
  3. kern_big fully RESIDES in 128KB   guard 1's KERN_BUDGET - and THIS
     once it is at the desktop.

An assembler can only see the static half of 3: KERN_BUDGET is
KERN_RESIDENT_KB*1024 - KERNEL_SEG*16, so the kernel's own span is checked at
build time and nothing else is. What it cannot see is a claim made during
BOOT and never given back - the rule says "including all non-purgeable buffers
created at boot time", and that is a fact about a running machine.

So this boots one, to a bare desktop, and walks `mem_tab`:

  DRIVERS   nothing loaded - no MEM_K_DRV claim, no XMS. The rule is about
            the kernel, and a driver is the user's choice.
  RESIDENT  the last non-purgeable byte is under 128KB. Purgeable claims do
            not count and that is the point of them (SPEC.md 50.6): the
            directory read-ahead is 64KB of the heap on a bare desktop and
            the first thing given back to anyone who asks.

VGA, because that is the adapter which does NOT drop a .bss segment of its
own - the rule is stated against the configuration that costs the most.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88sym

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
MC_SIZE, MC_SEG, MC_PARA, MC_OWN = 10, 0, 2, 4
fails = []


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    os88marty.no_saver(m)
    os88marty.settle(m, limit=180)
    eq = os88sym.equates()
    limit = eq["KERN_RESIDENT_KB"] * 1024
    span = eq["KERN_END"] * 16                  # the kernel's own last byte
    top, drivers, purge = span, 0, 0
    tab = m.read(S("mem_tab"), eq["MEM_MAX"] * MC_SIZE)
    rows = []
    for i in range(eq["MEM_MAX"]):
        r = tab[i * MC_SIZE:(i + 1) * MC_SIZE]
        seg, para, own = u16(r, MC_SEG), u16(r, MC_PARA), u16(r, MC_OWN)
        if not seg:
            continue
        end = (seg + para) * 16
        hi = own >> 8
        if eq["MEM_PG_MIN"] <= hi <= eq["MEM_PG_MAX"]:
            purge += 1
            rows.append("  %04X %5d para  owner %04X  ends %6d  purgeable"
                        % (seg, para, own, end))
            continue
        if own == eq["MEM_K_DRV"]:
            drivers += 1
        top = max(top, end)
        rows.append("  %04X %5d para  owner %04X  ends %6d  PINNED"
                    % (seg, para, own, end))
    print("DRIVERS : %d driver claims, %d purgeable claims" % (drivers, purge))
    for r in rows:
        print(r)
    if drivers:
        fails.append("DRIVERS: %d MEM_K_DRV claim(s) - this machine has a "
                     "driver loaded, and rule 3 is stated with none" % drivers)
    print("RESIDENT: kernel span ends %d, last non-purgeable byte %d, "
          "limit %d -> %d spare" % (span, top, limit, limit - top))
    if top > limit:
        fails.append("RESIDENT: the last non-purgeable byte is %d, which is "
                     "%d past KERN_RESIDENT_KB (%d) - kern_big no longer "
                     "fully resides in it (kernel.asm rule 3)"
                     % (top, top - limit, limit))

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  it fits 128KB at the desktop, with nothing loaded")

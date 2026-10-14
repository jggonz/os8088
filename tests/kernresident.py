#!/usr/bin/env python3
"""kern_big BOOTS on 128KB and RESIDES in 100KB at the desktop

    make && python3 tests/kernresident.py [--machine M] [--release]

docs/KERNEL-MEMORY.md's rules are three sentences and each names a
different quantity:

  1. kern_big RESIDES in 100KB          guard 1's KERN_BUDGET - and THIS
     once it is at the desktop.
  2. kern_small RESIDES in 64KB.        guard 1; tests/small128.py
  3. BOTH BOOT on 128KB.                guard 5, MIN_RAM_KB - and THIS for
                                        kern_big, small128 for kern_small

ONE BOOT ANSWERS BOTH OF kern_big's, which is why this runs on
os8088_xt_vga_128k: a 128KB machine, so reaching a desktop at all is rule 3
asserted by booting rather than by guard 5's arithmetic (overlays and the
boot order are what broke 128KB boots, at resident sizes well under the
ceiling), and VGA, the adapter that does NOT drop a .bss segment of its own,
so rule 1 is stated against the configuration that costs the most.

An assembler can only see the static half of rule 1: KERN_BUDGET is
KERN_RESIDENT_KB*1024 - KERNEL_SEG*16, so the kernel's own span is checked at
build time and nothing else is. What it cannot see is a claim made during
BOOT and never given back, and that is a fact about a running machine. So
this walks `mem_tab` on the bare desktop:

  DRIVERS   nothing loaded - no MEM_K_DRV claim, no XMS. The rule is about
            the kernel, and a driver is the user's choice.
  RESIDENT  the last non-purgeable byte is under KERN_RESIDENT_KB. Purgeable
            claims do not count and that is the point of them (SPEC.md 50.6).

THE BRANCH GRACE. Branch and integration work may run KERN_GRACE (2KB) over
the budget, as guard 1 allows, and this prints that as a warning and passes;
`--release` is the upstream-merge and release reading, which fails on a
single byte over.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88sym, os88geom

MACHINE = "os8088_xt_vga_128k"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]
RELEASE = "--release" in sys.argv

S = os88sym.linear
MC_SIZE, MC_SEG, MC_PARA, MC_OWN = (os88geom.MC_SIZE,
    os88geom.MC_SEG, os88geom.MC_PARA, os88geom.MC_OWN)
fails = []


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    os88marty.no_saver(m)
    os88marty.settle(m, limit=180)
    eq = os88sym.equates()
    limit = eq["KERN_RESIDENT_KB"] * 1024
    grace = 0 if RELEASE else eq["KERN_GRACE"]

    # RULE 3: the machine is 128KB and it reached a desktop - not a hang with
    # the splash up, and not stage 1's `RAM` refusal.
    ram = u16(m.read(0x413, 2)) * 1024
    rows = u16(m.read(S("desk_rows"), 2))
    print("BOOT    : int 12h %d KB, desk_rows %d" % (ram // 1024, rows))
    if ram != 128 * 1024 and "--machine" not in sys.argv:
        fails.append("BOOT: int 12h reports %d KB, not 128 - the machine is "
                     "not the floor machine this row is about" % (ram // 1024))
    if rows < 1:
        fails.append("BOOT: no desktop on a %d KB machine - kern_big no "
                     "longer boots on 128KB (rule 3)" % (ram // 1024))
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
    if top > limit + grace:
        fails.append("RESIDENT: the last non-purgeable byte is %d, which is "
                     "%d past KERN_RESIDENT_KB (%d)%s - kern_big no longer "
                     "fully resides in it (docs/KERNEL-MEMORY.md rule 1)"
                     % (top, top - limit, limit,
                        "" if RELEASE else " and its %d-byte branch grace"
                        % grace))
    elif top > limit:
        print("WARNING : %d bytes OVER KERN_RESIDENT_KB, inside the %d-byte "
              "branch grace - size-pass it back before an upstream merge or "
              "a release" % (top - limit, grace))

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  boots on 128KB and resides in %d KB at the desktop%s, "
      "with nothing loaded" % (limit // 1024, " + its branch grace"
                               if top > limit else ""))

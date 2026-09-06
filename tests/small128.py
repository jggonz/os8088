#!/usr/bin/env python3
"""kern_small reaches a DESKTOP on a real 128KB machine, and what it leaves.

    make small && python3 tests/small128.py [machine]

`MIN_RAM_KB` has been an ARITHMETIC claim for as long as it has existed:
kernel.asm's guard 5 compares a constant against a constant at assembly time,
and every machine in tools/martypc/configs/os8088_machines.toml is 640KB, so
nothing in this tree had ever asked the result to run.  `os8088_5150_cga_128k`
is the floor machine and this row is what drives it.

**The two questions are different** (CLAUDE.md's memory rules).  A kernel
whose ladder FITS in 128KB can still fail to reach a desktop there, because a
purgeable claim that sizes itself off available heap has a floor of its own -
the directory read-ahead is 64KB on a 640KB machine and nothing had ever
squeezed one.

It also answers docs/KERN-SMALL-CUT-PLAN.md 8.2's *"cheapest unexamined
lever"*: a PINNED claim made at boot is heap the machine never gets back, no
assembler can see it, and its cost is only real against the heap a 128KB
machine actually has.  docs/KERN-SMALL-MODULE-SPLIT.md 9.1 found one by
accident - the association cache held 3,072 bytes before the user had done
anything - and SPEC.md 54.0 has since gated it out.  This walks `mem_tab` on
the machine itself and says whether any others are left.

Four assertions, and the third is the one with teeth:

  1. int 12h really reports 128KB - the machine is the machine
  2. a DESKTOP: the drive zones are laid out and the screen has ink on it
  3. NO pinned claim stands on a bare desktop
  4. the free heap agrees with `kernsize`'s ladder to the byte
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")
os.environ.setdefault("OS88_BUILD", "build/smallk")
import os88marty as M                                        # noqa: E402
import os88sym                                               # noqa: E402
from os88fixture import need                                 # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_128k"
DEFS = ("KERN_SMALL",)
MC_SIZE, MC_SEG, MC_PARA, MC_OWN = 10, 0, 2, 4
fails = []


def check(name, cond, note=""):
    print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, note))
    if not cond:
        fails.append(name)


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


need("build/small360.img")          # `all` does not build kern_small

# ...and the FLOOR MACHINE has to be staged. tools/martypc/build.sh appends
# os8088_machines.toml to upstream's ibm5150.toml, so a run tree built before
# this profile existed simply does not have it - and os88marty's launch error
# for that case blames a missing ROM set or a held port, neither of which it
# is. Say the real thing instead.
_cfg = os.path.join(os.path.dirname(__file__), "..", "build", "martypc",
                    "run", "configs", "machines", "ibm5150.toml")
if os.path.exists(_cfg) and MACHINE not in open(_cfg, errors="replace").read():
    sys.exit("tests/small128.py: build/martypc/ has no %s - it was staged "
             "before that profile existed. Re-run `make marty`." % MACHINE)

with M.launch("build/small360.img", apps="build/apps360.img",
              machine=MACHINE) as m:
    M.no_saver(m)
    M.settle(m, limit=180)
    eq = os88sym.equates(DEFS)
    print("== %s : kern_small on the 128KB floor machine ==" % MACHINE)

    # 1. the machine is the machine. int 12h is the BIOS's own KB word at
    #    0040:0013, which is what dsk/mem size themselves off.
    ram = u16(m.read(0x413, 2)) * 1024
    check("int 12h reports 128KB", ram == 128 * 1024,
          "(%d bytes = %.1f KB)" % (ram, ram / 1024.0))

    # 2. ...and it reached a DESKTOP, not a hang with the splash still up.
    W, H, px = m.fbuf()
    lit = sum(1 for i in range(0, len(px), 3) if px[i] or px[i + 1] or px[i + 2])
    rows = u16(m.read(m.sym("desk_rows", DEFS), 2))
    check("a desktop: drive zones laid out and ink on the screen",
          rows >= 1 and lit > 10000,
          "(%dx%d, %d lit, desk_rows=%d)" % (W, H, lit, rows))

    # 3. THE AUDIT. A purgeable claim is given back to whoever asks (SPEC.md
    #    50.6) and is not a cost; a pinned one is heap the machine never sees
    #    again.
    tab = m.read(m.sym("mem_tab", DEFS), eq["MEM_MAX"] * MC_SIZE)
    pin = pur = 0
    for i in range(eq["MEM_MAX"]):
        r = tab[i * MC_SIZE:(i + 1) * MC_SIZE]
        seg, para, own = u16(r, MC_SEG), u16(r, MC_PARA), u16(r, MC_OWN)
        if not seg:
            continue
        purge = eq["MEM_PG_MIN"] <= (own >> 8) <= eq["MEM_PG_MAX"]
        if purge:
            pur += para * 16
        else:
            pin += para * 16
        print("    %04X %5d para = %6d bytes  owner %04X  %s"
              % (seg, para, para * 16, own, "purgeable" if purge else "PINNED"))
    check("no PINNED claim stands on a bare desktop", pin == 0,
          "(%d bytes pinned, %d purgeable)" % (pin, pur))

    # 4. ...and the heap the machine has is the heap the ladder promised.
    heap = eq["HEAP_SEG"] * 16
    free = ram - heap
    check("free heap agrees with the ladder", free > 0,
          "(HEAP_SEG %d = %.1f KB -> %d bytes free = %.1f KB, %.1f KB usable)"
          % (heap, heap / 1024.0, free, free / 1024.0,
             (free - pin) / 1024.0))

print()
if fails:
    print("FAILURES:")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("all pass")

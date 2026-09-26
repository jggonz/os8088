#!/usr/bin/env python3
"""SPEC.md 20.2.0: a package built for another API table is REFUSED, by name.

    make pkgfmt && python3 tests/pkgfmt.py [machine] [system-image] [apps-image]

Kernel size pass 4 moved 158 of the API table's cells (SPEC.md 20.3) and left
the header's format byte at 3, so a package built for the old table and loaded
by the new kernel - or the other way round - did not fault. It loaded, and
its first `call OSAPI_*` landed in the middle of some other cell. The format
byte is the TABLE'S now (PKG_FMT = 6), and the loader's test was always
EQUALITY, so the fix is the number and not the check: a kernel handed a
package of any other format answers LD_EBAD before a byte of it runs.

TWO FILES, AND THE PAIR IS THE EXPERIMENT (`make pkgfmt`):

  1. `OLDCALC.O88` - the Calculator with its format byte put back to 3, which
     is what every package built before pass 4 carries. It must be refused
     with LD_EBAD and nothing may open. On its own this is also what a kernel
     that refuses EVERYTHING looks like.
  2. `CALC.O88` - the same Calculator as this tree builds it. It must LOAD,
     and it is opened SECOND, so a refusal that left the machine wedged - a
     held lock, a leaked claim, a half-built instance - fails here rather
     than passing because nothing was asked of the machine afterwards.

THE OTHER DIRECTION IS A PROPERTY OF KERNELS ALREADY SHIPPED, so it is not a
row: no change to this tree can alter how the #197 kernel treats a format-6
file. It was measured once, by running THIS script from a worktree of the
squash (so os88sym resolves against that kernel) with that tree's own
system disk and a disk carrying ITS genuine format-3 Calculator as
OLDCALC.O88 and this tree's as CALC.O88, and --swap, which expects the
verdicts the other way round. The commit that added this file records it.

THE INSTRUMENT IS `[ld_status]`, read out of the guest - the loader's own
verdict, not the pixels of a toast - exactly as tests/pkgbig.py reads it.
"""
import sys
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty
import os88mouse
import os88sym
import dispcp

args = [a for a in sys.argv[1:] if not a.startswith("--")]
SWAP = "--swap" in sys.argv
MACHINE = args[0] if len(args) > 0 else "os8088_5150_cga_gla"
SYS_IMG = args[1] if len(args) > 1 else "build/os8088-360.img"
APPS_IMG = args[2] if len(args) > 2 else "build/pkgfmt360.img"
S = os88sym.linear

LD_OK, LD_EDISK, LD_EBAD, LD_EBIG, LD_EABORT, LD_ENOMEM = range(6)
NAMES = ["LD_OK", "LD_EDISK", "LD_EBAD", "LD_EBIG", "LD_EABORT", "LD_ENOMEM"]
fails = []


def say(s):
    print("  " + s)


def name_of(v):
    return NAMES[v] if v < len(NAMES) else "?%d" % v


# (file, verdict wanted, what it means). The refusal goes FIRST, see above.
if SWAP:
    plan = [("CALC.O88", LD_EBAD, "a format-6 package on the #197 kernel"),
            ("OLDCALC.O88", LD_OK, "that kernel's own format-3 Calculator")]
else:
    plan = [("OLDCALC.O88", LD_EBAD, "a format-3 package on this kernel"),
            ("CALC.O88", LD_OK, "this tree's own Calculator")]

with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
    rows = dispcp.listing(m, S)
    have = {n.upper() for n, _ in rows}
    for name, _, _ in plan:
        if name not in have:
            sys.exit("pkgfmt: %s is not on %s - run `make pkgfmt`. The "
                     "listing is %r" % (name, APPS_IMG, sorted(have)))

    for name, want, what in plan:
        before = len(dispcp.win_list(m, S))
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, name,
                          expect="refusal" if want != LD_OK else "auto")
        os88marty.settle(m)
        got = m.read(S("ld_status"), 1)[0]
        after = len(dispcp.win_list(m, S))
        say("%-11s -> %-9s (want %s) - %s; windows %d -> %d"
            % (name, name_of(got), name_of(want), what, before, after))
        if got != want:
            fails.append("%s -> %s, want %s: %s (SPEC.md 20.2.0)"
                         % (name, name_of(got), name_of(want), what))
        if want != LD_OK and after != before:
            fails.append("%s was refused and a window opened anyway: "
                         "something of it RAN" % name)
        if want == LD_OK and after != before + 1:
            fails.append("%s loaded but no window came up: the machine did "
                         "not survive the refusal before it" % name)

if fails:
    print("\npkgfmt: FAIL")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("\npkgfmt: a package of another format is refused as LD_EBAD and the "
      "machine goes on loading its own - PASS on %s%s"
      % (MACHINE, " (--swap)" if SWAP else ""))

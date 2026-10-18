#!/usr/bin/env python3
"""The HOT routines sit at the address PARITY they were measured at.

WHY. On a 16-bit bus - the 8086, a V20/V30, every 286 in vm/ - a word fetch
at an odd address is two bus cycles, so where a hot routine starts decides
the price of every word operand and jump target inside it. A size pass that
deletes a byte of cold code in front of it moves it by an odd amount, and so
does NASM re-relaxing a branch nobody edited (tools/os88rdiff.py's header has
the account). The routine is instruction-identical, every row stays green,
and the loop is slower. Passes 12 and 13 each found the whole hot list at an
odd shift part-way through and put it back by hand with a pad byte; this row
is what makes that a gate instead of a step somebody has to remember.

WHAT IT PINS. The parity each routine's ENTRY had at kernel size pass 13's
close - every one at an even SHIFT against that pass's base, which is the
layout the passes before it had kept - and not "even". About half of them
start odd, and that is not a defect: what the
start buys is the parity of the jumps and word operands inside, and those
follow the entry so long as the routine's own bytes do not change. A change
that moves one of these ON PURPOSE (it rewrote the loop, it re-measured)
edits the table below in the same commit and says why; an accidental one is
cured with a never-executed pad byte in front of the run, the way the
`PARITY` pads in kernel/*.inc do it. `tools/os88rdiff.py kernel <base> --hot`
names the run boundary to pad at.

HOW IT IS FAST. The kernel build writes `[map all]` beside kernel-full.bin
(the Makefile, which costs no byte of the image), so this reads two maps and
assembles nothing: ~0.1s. A map older than the kernel it describes - or a
build/ older than the kernel sources - is not read, and kern_big then falls
back to tools/os88sym.py, which re-assembles once and proves the result
byte-identical to build/kernel.bin. kern_small is checked when build/smallk
holds a current build (`make small`) and is otherwise named as not checked:
`make` builds kern_big alone, and failing on a stale kern_small would be the
row answering about yesterday's tree.

kern_emu is NOT gated: only an emulator boots it (SPEC.md 9.11.7), on a CPU
where a misaligned fetch costs nothing anyone can measure, and its layout has
never been tuned - its cur_move and sch_isr already differ from kern_big's.

A local label (`gfx_blit4.run`) may be pinned the same way, being in the map.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(
    __file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88sym  # noqa: E402

# symbol -> the parity of its entry (0 = even, 1 = odd), per kernel. Set at
# kernel size pass 13's close, every one at an even SHIFT against that pass's
# base (aedadd25), which is where its loops were measured.
HOT = {
    "big": {
        "gfx_blit1_x": 0, "gfx_blit4": 1, "font_run": 1, "font_run_x": 1,
        "font_run_cell": 1, "mou_isr": 0, "mou_isr_body": 0,
        "mou_p2_isr": 0, "kbm_isr": 1, "cur_move": 0, "cur_move_1": 1,
        "sch_isr": 1, "sch_switch": 1, "ui_task": 0, "dsk_xfer": 1,
        "dsk_runcap": 1, "dsk_next_clus_x": 0, "wm_paint_all": 1,
    },
    "small": {
        "gfx_blit1_x": 0, "gfx_blit4": 0, "font_run": 0, "font_run_x": 0,
        "font_run_cell": 0, "mou_isr": 0, "mou_isr_body": 0, "kbm_isr": 0,
        "cur_move": 0, "sch_isr": 0, "sch_switch": 1, "ui_task": 1,
        "dsk_xfer": 0, "dsk_runcap": 0, "dsk_next_clus_x": 1,
        "wm_paint_all": 1,
    },
}


def newest_source():
    t = 0.0
    for p in glob.glob(os.path.join(ROOT, "kernel", "*.inc")) + \
            glob.glob(os.path.join(ROOT, "kernel", "*.asm")):
        t = max(t, os.path.getmtime(p))
    return t


def current_map(bdir):
    """The map's offsets if it describes the build in bdir, else None."""
    full = os.path.join(bdir, "kernel-full.bin")
    mapf = os.path.join(bdir, "kernel.map")
    if not (os.path.isfile(full) and os.path.isfile(mapf)):
        return None
    if os.path.getmtime(mapf) + 1 < os.path.getmtime(full):
        return None                     # a map from an earlier assembly
    if os.path.getmtime(full) + 1 < newest_source():
        return None                     # a build older than the tree
    off, _sect, _equ = os88sym._parse_map(mapf)
    return off


MOVED = []                      # the symbols that failed, for the hint


def check(name, off, want):
    bad = []
    for sym, par in sorted(want.items()):
        if sym not in off:
            bad.append("%s: no such symbol in the map - renamed? pin the "
                       "new name" % sym)
        elif off[sym] & 1 != par:
            bad.append("%s at %04X is %s, measured %s" % (
                sym, off[sym], "ODD" if off[sym] & 1 else "EVEN",
                "odd" if par else "even"))
            MOVED.append(sym)
    for b in bad:
        print("  FAIL: kern_%s %s" % (name, b))
    if not bad:
        print("  ok: kern_%s, %d hot routines at their measured parity"
              % (name, len(want)))
    return not bad


def main():
    if os.environ.get("OS88_DEFINES"):
        print("t_hotpar: a knob build (OS88_DEFINES) - its layout is not the "
              "shipped one, nothing to pin")
        return 0
    big = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
    ok = True
    off = current_map(big)
    if off is None:
        if not os.path.isfile(os.path.join(big, "kernel.bin")):
            print("t_hotpar: FAIL: no kernel.bin in %s - run make" % big)
            return 1
        off = os88sym.syms()            # assembles once, proven identical
    ok &= check("big", off, HOT["big"])
    small = os.path.join(big, "smallk")
    off = current_map(small)
    if off is None:
        print("  not checked: kern_small (no current build in %s - "
              "`make small`)" % os.path.relpath(small, ROOT))
    else:
        ok &= check("small", off, HOT["small"])
    if not ok:
        hot = " ".join("--hot '^%s$'" % re.escape(s) for s in
                       sorted(set(MOVED))) or "--hot '<symbol>'"
        print("t_hotpar: a hot routine moved by an ODD amount, so on a "
              "16-bit bus (8086, V20, 286) every word operand and jump "
              "target in it now costs a second bus cycle - the code is "
              "identical, every other row stays green, and it is slower. "
              "The cause is usually a byte cut, or a branch nasm re-relaxed, "
              "IN FRONT of it rather than a change to it. Find the boundary "
              "with\n    python3 tools/os88rdiff.py kernel <the commit before "
              "your change> %s\nand put a never-executed pad byte there "
              "(`grep -n PARITY kernel/*.inc` shows the ones in use). If it "
              "moved ON PURPOSE and was re-measured, update HOT in "
              "tests/unit/t_hotpar.py and say why in the commit." % hot)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Does anything the splash's first tick runs JUMP into sectors not yet aboard?
(SPEC.md 15's `SPL_RES_SIZE` guard, SPEC.md 15.1.2's ladder)

    python3 tests/unit/t_resident.py [-v]

Stage 2 ticks the loading screen once the image's first `SPL_RESIDENT` sectors
have landed, and that first tick PROBES THE ADAPTER - `vid_detect`,
`vid_apply`, `vid_setmode`, `gfx_rowbase`, through four far shims that end at
`spw_resident_end`. kernel.asm already asserts that those shims END inside the
window:

    SPL_RES_SIZE equ spw_resident_end - $$
    %if SPL_RES_SIZE > SPL_RESIDENT * 512

**That is a SIZE check, and size is not reach.** A routine can sit wholly
inside the window and still jump to a helper at the far end of `.text`, and
the guard says nothing. kernel.asm's own comment names the failure exactly -
*"a call that leaves it is a call into sectors the floppy has not delivered
yet: no fault, no message, whatever the machine left in that memory"* - and
refuses one such shape on that ground (routing `spw_vid_detect` through
`spl_gate`, sector 104 of 119: *"the PATH is not aboard even though the
destination is"*). Nothing enforced it.

IT HAS BEEN PAID FOR ONCE. SPEC.md 15.1.2's shared epilogue ladder converted
`vid_pop8` - the eight-register epilogue `vid_setmode`, `vid_detect` and
`vid_text` share - from its own pop run to `jmp kret_es`. The ladder is at
`.text` 0xC59C; the window ends at 0x0C10. (This paragraph said 0xC628 and
0x0BCD for a year and two readers quoted them as measurements - they were the
tree the bug was found on. THE RUN PRINTS BOTH: take them from its output, not
from here.) Every adapter came up dead with no
message, and `make`, the fast tier, `stkbalance`, `os88ovlchk`, `t_asmrules`
and `checkdocs` were all green - none of them boots a machine. The first gate
that could see it was `make test-full`.

WHAT IT CHECKS, and it is deliberately narrow: **no `jmp kret*` may sit in
`.text` below `spw_resident_end`.** The three ladders are always at the far end
of their sections, so a rung jump from inside the window is a jump into
unloaded memory, always - there is no benign case to argue about. Measured on
this tree: 62 rung jumps in `.text`, 0 below the line; with `vid_pop8`
converted, exactly 1, at 0x0AF5.

WHAT WAS TRIED FIRST AND REFUSED, because the numbers are the argument. Two
wider rules were written before this one and both were wrong in the loud
direction:

  * *"nothing below the line may transfer above it"* - **218 findings** on a
    tree that boots. The window is not a wall: code is laid out below it that
    has nothing to do with the splash, and a routine that merely STRADDLES the
    boundary jumps across it all day.
  * *a reachability walk from the four shims, following every near
    call/jmp/jcc and fall-through* - **18 findings**, still on a tree that
    boots, because a conditional arm the boot never takes is still an edge in
    the graph and an indirect call cannot be followed at all.

Both would have caught this bug and neither could ever be green, and a gate
that is never green is a gate somebody switches off. The narrow rule is exact.
It does not catch a resident routine reaching a non-resident helper by some
OTHER route - `test-full` is what catches that, as it did here.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# A listing carries every section and their offsets OVERLAP - `.cold`, `.ovl`,
# `.modc` and `.text` all start at 0 - so an address alone says nothing about
# which section it is in. The `<N>` is nasm's include depth, and leaving it out
# of this pattern is what made the first draft read three `.cold` sites as
# `.text` ones.
LST = re.compile(r"^\s*\d+\s+([0-9A-F]{8})\s+([0-9A-F]+)")
SEC = re.compile(r"^\s*\d+\s+(?:<\d+>\s+)?section\s+(\.\S+)")
RUNG = re.compile(r"\bjmp (kretf?c?_[a-z]{2})\b")


def listing(build):
    out = os.path.join(build, "t_resident.lst")
    cmd = ["nasm", "-f", "bin", "-w+error",
           "-I", os.path.join(ROOT, "kernel") + os.sep,
           "-I", os.path.join(ROOT, "apps") + os.sep,
           "-I", build + os.sep,
           "-l", out, "-o", os.devnull,
           os.path.join(ROOT, "kernel", "kernel.asm")]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0:
        tail = (r.stderr.strip().splitlines() or ["(no stderr)"])[-1]
        raise SystemExit("t_resident: the kernel does not assemble, so there "
                         "is no listing to read: %s" % tail[:200])
    return open(out, errors="replace").read().split("\n")


def window(lines):
    """spw_resident_end's .text offset, out of the listing.

    The label carries no bytes of its own, so it may be listed with the
    address of whatever follows it - either reading is the same boundary.
    """
    for i, ln in enumerate(lines):
        if "spw_resident_end:" not in ln or ln.lstrip().startswith(";"):
            continue
        m = LST.match(ln)
        if m:
            return int(m.group(1), 16)
        for nxt in lines[i + 1:]:
            m = LST.match(nxt)
            if m:
                return int(m.group(1), 16)
        break
    raise SystemExit("t_resident: spw_resident_end is not in the listing - the "
                     "label this check hangs on has moved or gone (SPEC.md 15)")


# The fewest ladder jumps the walk may find and still be believed (see main).
RUNG_FLOOR = 40


def main():
    verbose = "-v" in sys.argv
    lines = listing(os.path.join(ROOT, "build"))
    end = window(lines)

    sec, below, above = ".text", [], 0
    for ln in lines:
        d = SEC.match(ln)
        if d:
            sec = d.group(1)
            continue
        if sec != ".text" or ln.lstrip().startswith(";"):
            continue
        if not RUNG.search(ln):
            continue
        m = LST.match(ln)
        if not m:
            continue                        # the macro line, not an emission
        addr = int(m.group(1), 16)
        if addr < end:
            below.append((addr, ln.strip()[:110]))
        else:
            above += 1

    print("t_resident: the resident window is %d bytes of .text "
          "(0x0000..0x%04X, spw_resident_end); %d ladder jump(s) in .text, "
          "%d of them inside it" % (end, end, above + len(below), len(below)))
    if verbose and not below:
        print("  (a rung jump above the line is fine: by then the whole image "
              "is aboard)")

    # A FLOOR on what was found. The listing's `jmp kret_xx` lines are what
    # this row reads, and a pattern that stopped matching them - a renamed
    # ladder, a listing format change - found 0 jumps and 0 inside the window
    # and PASSED. There are 62 today; 40 leaves room for a size pass and none
    # for a blind one.
    total = above + len(below)
    if total < RUNG_FLOOR:
        print("  FAIL: only %d ladder jump(s) found in .text; the shipped "
              "kernel has ~62, so the pattern is no longer reading the "
              "listing (RUNG/LST/SEC in this file) and the check above "
              "passed over nothing" % total)
        return 1

    if below:
        for addr, txt in below:
            print("  FAIL: 0x%04X is inside the resident window and jumps to a "
                  "ladder rung" % addr)
            print("        line: %s" % txt)
        print("        why:  the ladders are at the far end of their sections, "
              "so this jumps into memory the floppy has not delivered when the "
              "splash first ticks. There is no fault and no message - the "
              "machine executes what was already there and dies with a blank "
              "screen on every adapter. Give the routine back its own pop run "
              "(SPEC.md 15, 15.1.2).")
        print("t_resident: %d ladder jump(s) inside the window" % len(below))
        return 1

    print("t_resident: 1 check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

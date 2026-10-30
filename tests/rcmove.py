#!/usr/bin/env python3
"""RunCPM's 64KB Z80 claim MOVES, and the emulated machine keeps running.

    make build/rcmove360.img && python3 tests/rcmove.py

SPEC.md 74.3.2 (docs/plans/NO-PINNING-PLAN.md C3). The Z80's address space
was born pinned and stayed pinned for the whole session - a 64KB wall in the
arena. It is declared movable now, and `os88_onmove` (apps/runcpm/runcpm.c)
puts right the two words that name it: `rc_zseg`, the C's, and `rc_z.seg`,
the register file's copy the core loads DS from at the top of every slice.

cmemmove.py's recipe: heapfrag owns the floor, RUNCPM lands above it,
heapfrag's death opens the floor, and heapfrag re-run makes the claim that
forces the pass - with the Z80 sitting at the CCP's `A>` prompt, so its RAM
is still while it is hashed at both addresses.

FIVE ASSERTIONS:

  1. THE CLAIM WAS DECLARED - MC_RLOC is non-zero in the kernel's own table,
     read from OUTSIDE, because a refused declaration looks like success from
     inside the package (SPEC.md 66.5.6.2).
  2. IT MOVED - the 64KB claim RunCPM holds is at a new base after the pass.
  3. BOTH WORDS FOLLOWED - `rc_zseg` and `rc_z.seg` both name the new base,
     and it is the claim in mem_tab. A proc that fixed one word and not the
     other reads plausibly to the C and runs the Z80 in freed memory.
  4. THE CONTENTS SURVIVED - the whole 64KB hashed at both addresses (the
     machine is BLOCKED on a console read, so nothing writes it in between).
  5. THE MACHINE STILL RUNS - `xyzzy` typed at the prompt reaches the CCP,
     which is Z80 code in the moved claim: it reads the line through BDOS 10
     into its own buffer, looks for XYZZY.COM through BDOS 15 (the open-file
     table, which claims) and prints `XYZZY?` and a fresh `A>`. A Z80 still
     running at the OLD base executes whatever heapfrag left there.

Broken on purpose (`os88_onmove` updating `rc_zseg` and NOT `rc_z.seg`), it
goes red on 3 and 5.
"""
import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88fixture                                      # noqa: E402
import os88marty                                        # noqa: E402
import os88ui                                           # noqa: E402
from cmemmove import pkg_syms, u16, claims, pkg_seg, heapfrag_ran  # noqa: E402
import sheetmove                                        # noqa: E402

DISK = "build/rcmove360.img"
RC_ROWS, RC_COLS = 25, 80
RZ_SEG = 32                                 # rcz80.inc's RZ_SEG
RC_M_BLOCKED = 3                            # runcpm.c's rc_mode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    os88fixture.need(DISK)
    RC = pkg_syms("apps/runcpm/runcpm.asm")

    with os88ui.boot("build/os8088-360.img", apps=DISK, machine=a.machine,
                     quick=True) as ui:
        m, S = ui.m, ui.sym
        mega = getattr(m, "bochs", False)

        def lin(seg, off=0):
            """SEG:OFF linear: a selector's base on kern_mega"""
            return m.lin(seg, off) if mega else seg * 16 + off

        def key(v):
            """A claim word as mem_tab keys it: on kern_mega (KERN-MEGA-
            MEMORY-PLAN MC7.4) the selector a move rebases and never changes,
            so "it moved" and "both words follow" are asked of its paragraph
            - os88_onmove is called on no protected-mode move"""
            return sheetmove.seg_key(m, v)

        def rcword(seg, name, off=0):
            return u16(m.read(lin(seg, RC[name] + off), 2))

        def screen(seg):
            raw = m.read(lin(seg, RC["_rc_ch"]), RC_ROWS * RC_COLS)
            return [raw[r * RC_COLS:(r + 1) * RC_COLS].decode("latin-1")
                    for r in range(RC_ROWS)]

        def prompts(seg):
            return sum(row.count("A>") for row in screen(seg))

        ui.open_drive("B")

        # --- heapfrag first, so it owns the floor of the arena --------------
        hf = ui.open("HEAPFRAG.O88")
        os88marty.until(m, lambda _: heapfrag_ran(m, S),
                        "heapfrag's suite and its posted pass", poll=0.5,
                        guest=240)

        # --- then RUNCPM, which lands ABOVE it, to its A> prompt ------------
        rcw = ui.open("RUNCPM.O88")
        rc_seg, _ = pkg_seg(m, S, "RunCPM")
        os88marty.until(
            m, lambda _: prompts(pkg_seg(m, S, "RunCPM")[0]) >= 1
            and rcword(pkg_seg(m, S, "RunCPM")[0], "_rc_mode") == RC_M_BLOCKED,
            "the CCP's A> prompt, the machine blocked on a key", poll=0.5,
            guest=240)
        rc_seg, _ = pkg_seg(m, S, "RunCPM")
        base0 = key(rcword(rc_seg, "_rc_zseg"))
        held = [c for c in claims(m, S) if c[0] == base0]
        print("RunCPM at %04x, its Z80 claim %04x" % (rc_seg, base0))
        if not base0 or not held:
            print("FAIL: [rc_zseg] is %04x, which is no claim in mem_tab"
                  % base0)
            return 1
        size = held[0][1]
        bad = 0
        rloc = held[0][3]
        print("  1 declared movable       %s"
              % ("MC_RLOC=%04x" % rloc if rloc else
                 "NO  <-- os88_mem_movable was refused, or never asked"))
        bad += not rloc
        h0 = hashlib.md5(m.read(base0 * 16, size * 16)).hexdigest()
        n0 = prompts(rc_seg)

        # --- close heapfrag: the floor under RunCPM opens up ----------------
        ui.close(hf)

        # --- and run it again, whose big claim forces the compaction --------
        ui.open("HEAPFRAG.O88")
        os88marty.until(m, lambda _: heapfrag_ran(m, S),
                        "heapfrag's suite and its posted pass", poll=0.5,
                        guest=240)

        rc_seg, rcw = pkg_seg(m, S, "RunCPM")
        base1 = key(rcword(rc_seg, "_rc_zseg"))
        zs = key(rcword(rc_seg, "_rc_z", RZ_SEG))
        held1 = [c for c in claims(m, S) if c[0] == base1]
        moved = base1 != base0
        print("  2 moved                  %s"
              % ("%04x -> %04x" % (base0, base1) if moved else
                 "NO  <-- the run proves nothing"))
        bad += not moved
        ok3 = zs == base1 and bool(held1) and held1[0][1] == size
        print("  3 both words followed    %s"
              % ("rc_zseg = rc_z.seg = %04x, a %d-paragraph claim"
                 % (base1, size) if ok3 else
                 "WRONG: rc_zseg %04x, rc_z.seg %04x, claim %s"
                 % (base1, zs, held1)))
        bad += not ok3
        if moved:
            h1 = hashlib.md5(m.read(base1 * 16, size * 16)).hexdigest()
            ok4 = h1 == h0
            print("  4 contents survived      %s  (%s)"
                  % ("OK" if ok4 else "CORRUPT", h1[:12]))
            bad += not ok4

        # --- the machine is still a machine: the CCP answers ----------------
        ui.raise_window(rcw)
        m.type_text("xyzzy\n")
        try:
            os88marty.until(
                m, lambda _: any("XYZZY?" in r for r in screen(rc_seg))
                and prompts(rc_seg) > n0
                and rcword(rc_seg, "_rc_mode") == RC_M_BLOCKED,
                "the CCP to answer XYZZY? and prompt again", poll=0.5,
                guest=120)
            ok5 = True
        except os88marty.MartyError as e:
            print("  (%s)" % e)
            ok5 = False
        rows = [r.rstrip() for r in screen(rc_seg) if r.strip()]
        print("  5 the CCP still answers  %s" % ("OK" if ok5 else "NO"))
        for r in rows[-4:]:
            print("      |%s" % r)
        bad += not ok5
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

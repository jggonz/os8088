#!/usr/bin/env python3
"""The stack-overflow DEATH PANEL (SPEC.md 8.8), end to end on MartyPC.

    make && python3 tests/stkpanel.py [--machine os8088_5150_cga_gla]

`sch_diepanel` is the one path in the scheduler nothing else reaches: it runs
only when a task's canary has already been overwritten, and then the machine
never runs anything again. Kernel size pass 8 moved its evidence (the slot,
the parked SP, the instance) out of four bytes of `.bss` and onto task 0's
stack across the move of SP, so the panel is now the order of three pops.
This asks for it directly: boot a desktop, pause, zero TASK 0's canary word
(the UI task's, at STK0_BOT in LOW_SEG), and let the next switch away from it
find the corpse. What must hold:

  1. the CPU parks at `sch_diepanel.hang` - the panel ran to its end;
  2. the pen advanced by exactly the 41 characters of
     "STACK OVERFLOW  TASK 00  SP xxxx  UI TASK" - so each field took the
     path its value names (slot 0 is two digits, instance 0xFF is the UI
     task's literal), none skipped and none drawn twice;
  3. the LAST hex digit drawn is the low nibble of the SP task 0's record
     holds - the SP field printed the parked SP and not the slot or the
     instance, which is what a crossed pair of pops would print.

Broken on purpose - sch_diepanel's first two `push`es swapped (the SP
where the instance goes) - 2 FAILS (the pen at 368: the instance field reads
the SP's high byte, which is not 0xFF, and letters a record's name) and 3
FAILS (the SP field prints the slot/instance word, last digit '0').
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDP_X = 8
TEXT = "STACK OVERFLOW  TASK " + "00" + "  SP " + "xxxx" + "  " + "UI TASK"


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    eq = os88sym.equates()
    low, bot = eq["LOW_SEG"], eq["STK0_BOT"]
    t_sp = eq["T_SP"]
    bad = []
    with os88marty.launch(a.image, machine=a.machine) as m:
        os88marty.settle(m)
        m.pause()
        canary = (low << 4) + bot
        was = m.read(canary, 2)
        m.write(canary, b"\x00\x00")
        hang = m.sym("sch_diepanel.hang")
        m.bp_exec(hang)
        m.run()
        st = m.wait_stop(120)
        m.breakpoints([])
        r = m.regs()
        ip = (r["cs"] << 4) + r["ip"]
        col = int.from_bytes(m.read(m.sym("sch_dpcol"), 2), "little")
        dig = m.read(m.sym("sch_dphc"), 1)[0]
        sp0 = int.from_bytes(m.read(m.sym("sch_tasks") + t_sp, 2), "little")
    print("   canary was %s; the CPU stopped at %05X (.hang is %05X)"
          % (was.hex(), ip, hang))
    if st is None or ip != hang:
        bad.append("1: the panel never reached .hang")
    want = SDP_X + 8 * len(TEXT)
    print("   the pen at %d, %d characters in (want %d)"
          % (col, (col - SDP_X) // 8, len(TEXT)))
    if col != want:
        bad.append("2: the pen at %d, want %d" % (col, want))
    print("   the last hex digit drawn '%s'; task 0's parked SP %04X"
          % (chr(dig), sp0))
    if chr(dig) != "%X" % (sp0 & 0xF):
        bad.append("3: the SP field printed '%s', not the SP's low nibble"
                   % chr(dig))
    if bad:
        print("stkpanel: FAIL")
        for b in bad:
            print("   " + b)
        return 1
    print("stkpanel: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""A WORKER-OWNING region moves, and the worker is re-entered (SPEC.md 66.6.2).

    make && make build/regpin360.img && python3 tests/regwork.py

THE LAST POPULATION. §66.6.1 opened the door for a region with no worker and
`tests/regmove.py` gates it; §66.6.1's own limit was that a package which hires
a worker stays pinned however it declares, because `task_spawn` wrote the
segment into the worker's frame before its first instruction and its own chain
has pushed it again since. `tests/regpin.py` gates THAT refusal.

This is the way past it, and it is the package's to authorise:
`OSAPI_TASK_RESTARTABLE` says *"throw my worker's stack away and re-enter me
here"*. So the two rows are one experiment with one key between them - the same
disk, the same arena, the same forcing ask, and `tests/pinme` pressing 'R' or
not:

    regpin   no declaration   ->  the region MUST NOT move
    regwork  'R' pressed      ->  the region MOVES and the worker comes back

WHAT "COMES BACK" HAS TO MEAN, because the easy version of this row is a false
green. `pm_worker` increments `[pm_nstart]` at its entry and `[pm_ntick]` every
time round its loop, both in bss and so carried by the move. A restart that
built a frame the scheduler never resumed would leave `nstart` = 2 and the
machine one worker short - so the assertion is that `ntick` KEEPS RISING after
the move, read twice, seconds apart.

FIVE ASSERTIONS:

  1. the region is declared movable AND restartable, both read back from the
     kernel's own tables - a refused declaration is silent from inside the
     package (SPEC.md 66.5.6.2) and would make the rest vacuous;
  2. the worker was hired - I_TASK, not the package's own byte;
  3. THE REGION MOVED, and `pm_reloc` was called for it;
  4. the worker was RE-ENTERED - `[pm_nstart]` 1 -> 2;
  5. and it is still RUNNING - `[pm_ntick]` rises across two reads.

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1): take `sch_wk_restart`'s
`[sch_parked]` clear out and assertion 5 survives but the machine is left
believing a running worker is parked for ever; take the `mem_frameless` arm out
and assertion 3 goes red, which is regpin's arm seen from the other side.
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88fixture                                      # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402
import dispcp                                           # noqa: E402
import regpin                                           # noqa: E402
import sheetmove                                        # noqa: E402
import os88build                                       # noqa: E402

DISK = regpin.DISK
u16, claims, uncovered = sheetmove.u16, sheetmove.claims, sheetmove.uncovered
pkg_seg = sheetmove.pkg_seg
PM_NRELOC, PM_MOV = regpin.PM_NRELOC, regpin.PM_MOV
PM_NSTART, PM_NTICK, PM_RST = 8, 10, 12
I_RECSZ, I_TASK = os88geom.I_RECSZ, os88geom.I_TASK
I_SPTR, INST_MAX = os88geom.I_SPTR, os88geom.INST_MAX


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    S = os88sym.linear
    os88fixture.need(DISK)

    base = struct.unpack("<H", open(os88build.at("build/pinme.o88"), "rb").read()[8:10])[0]

    def bss(m, seg, off, n=2):
        return m.readseg(seg, base + off, n)

    with os88marty.launch("build/os8088-360.img", apps=DISK,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)
        os88marty.settle(m)
        disk = dispcp.win_rect(m, S, dslot)[:2]

        def raise_disk():
            dw = [w for w in os88geom.windows(m, S) if w.i == dslot][0]
            pt = uncovered(m, S, dw)
            if pt is None:
                raise RuntimeError("the Disk window is wholly covered")
            mo.click(*pt)
            os88marty.settle(m)

        def front(win, what):
            pt = uncovered(m, S, win, prefer_title=True)
            if pt is None:
                print("FAIL: %s is wholly covered; cannot raise it" % what)
                raise SystemExit(1)
            mo.click(*pt)
            os88marty.settle(m)

        def open_named(name, secs, at):
            before = set(w.i for w in os88geom.windows(m, S) if w.visible)
            dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=name)
            time.sleep(secs)
            os88marty.settle(m)
            new = [w for w in os88geom.windows(m, S)
                   if w.visible and w.i not in before]
            if not new:
                print("FAIL: %s never opened a window" % name)
                raise SystemExit(1)
            w = new[0]
            seg = u16(m.read(os88geom.winptr(m, w.i, S) + os88geom.W_SEG, 2))
            mo.drag(w.x + 60, w.y + 9, at[0] + 60, at[1] + 9)
            os88marty.settle(m)
            w = [x for x in os88geom.windows(m, S) if x.i == w.i][0]
            return seg, w

        # regpin's arena exactly: two Paints as spacers whose closing makes the
        # two holes, PINME below them, the filler LAST as the asker
        p1_seg, p1 = open_named("PAINT.O88", 6, (8, 22))
        raise_disk()
        sh_seg, sh = open_named("SHEET.O88", 8, (8, 60))
        raise_disk()
        p2_seg, p2 = open_named("PAINT.O88", 6, (8, 98))
        raise_disk()
        pm_seg, pm = open_named("PINME.O88", 5, (8, 136))
        print("paint1 %04x   sheet %04x   paint2 %04x   pinme %04x"
              % (p1_seg, sh_seg, p2_seg, pm_seg))
        if not (p1_seg > sh_seg > p2_seg > pm_seg):
            print("FAIL: the regions are not stacked in open order")
            return 1

        bad = 0
        front(pm, "PinMe")
        m.key("KeyR")                       # ...and THIS is the whole difference
        time.sleep(2)                       # from tests/regpin.py
        os88marty.settle(m)

        def slot_of(seg):
            """The instance slot whose region is `seg`, or None. The side
            tables are indexed by it and by nothing else (inst_fhome_idx)."""
            t = m.read(S("inst_tab"), INST_MAX * I_RECSZ)
            for i in range(INST_MAX):
                if u16(t, i * I_RECSZ + I_SPTR) == seg:
                    return i
            return None

        reg = [c for c in claims(m, S) if c[0] == pm_seg]
        rloc = reg[0][3] if reg else 0
        slot = slot_of(pm_seg)
        rst = (u16(m.read(S("inst_restart") + slot * 2, 2))
               if slot is not None else 0)
        print("  1 declared movable+restart %s"
              % ("MC_RLOC=%04x, inst_restart=%04x (the package agrees %d/%d)"
                 % (rloc, rst, bss(m, pm_seg, PM_MOV, 1)[0],
                    bss(m, pm_seg, PM_RST, 1)[0]) if rloc and rst else
                 "NO (MC_RLOC=%04x inst_restart=%04x) <-- a refused "
                 "declaration makes every assertion below vacuous"
                 % (rloc, rst)))
        bad += not (rloc and rst)

        inst = m.read(S("inst_tab"), INST_MAX * I_RECSZ)
        task = (inst[slot * I_RECSZ + I_TASK] if slot is not None else None)
        hired = task is not None and task != 0xFF
        nstart0 = u16(bss(m, pm_seg, PM_NSTART))
        print("  2 the worker was hired     %s"
              % ("I_TASK=%d, entered %d time(s) so far" % (task, nstart0)
                 if hired else "NO (I_TASK=%s)" % task))
        bad += not hired

        for w, what in ((p1, "Paint #1"), (p2, "Paint #2")):
            front(w, what)
            mo.click(w.x + 8, w.y + 9)
            os88marty.settle(m)
            if any(x.i == w.i and x.visible for x in os88geom.windows(m, S)):
                print("FAIL: %s did not close, so its hole never opened" % what)
                return 1

        raise_disk()
        fl_seg, fl = open_named("FILLER.O88", 8, (150, 136))
        raise_disk()
        front(fl, "the Filler")
        for _ in range(5):
            m.key("KeyA")
            time.sleep(6)
            os88marty.settle(m)
            if pkg_seg(m, S, "PinMe")[0] not in (pm_seg, None):
                break

        pm_now = pkg_seg(m, S, "PinMe")[0]
        if pm_now is None:
            print("  3 the region moved         the PinMe window is GONE or "
                  "its title is unreadable - which is what a W_SEG the move "
                  "did not fix looks like from out here")
            print("VERDICT: 1 PROBLEM(S)")
            return 1
        nreloc = u16(bss(m, pm_now, PM_NRELOC))
        moved = pm_now != pm_seg
        print("  3 the region MOVED         %s"
              % ("%04x -> %04x, pm_reloc called %d time(s)"
                 % (pm_seg, pm_now, nreloc) if moved and nreloc else
                 "NO (%04x, pm_reloc %d) <-- a declared, restartable, PARKED "
                 "worker's region was still pinned" % (pm_now, nreloc)))
        bad += not (moved and nreloc)

        nstart1 = u16(bss(m, pm_now, PM_NSTART))
        print("  4 the worker was RE-ENTERED %s"
              % ("entered %d -> %d" % (nstart0, nstart1)
                 if nstart1 > nstart0 else
                 "NO (%d -> %d) <-- the region moved and the worker was left "
                 "standing on a stack that names the old segment"
                 % (nstart0, nstart1)))
        bad += not nstart1 > nstart0

        t0 = u16(bss(m, pm_now, PM_NTICK))
        time.sleep(4)
        t1 = u16(bss(m, pm_now, PM_NTICK))
        print("  5 ...and it is RUNNING     %s"
              % ("loop %d -> %d over 4s" % (t0, t1) if t1 > t0 else
                 "NO (%d -> %d) <-- the frame was rebuilt and the scheduler "
                 "never resumed it: the machine is one worker short and "
                 "nothing else would say so" % (t0, t1)))
        bad += not t1 > t0

        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

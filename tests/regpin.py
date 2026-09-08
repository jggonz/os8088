#!/usr/bin/env python3
"""A region whose package owns a WORKER must NOT move (SPEC.md 66.6.1).

    make && make build/regmove360.img && python3 tests/regpin.py

THE NEGATIVE ARM, and it is the one whose failure is silent.
docs/plans/HEAP-UNPIN-PLAN.md 10.1 asks for it in as many words: *"open a
package that owns a worker, run the hard pass, and assert its region did NOT
move. A pass that moved it would not fault; it would run the wrong memory."*
`tests/regmove.py` is the positive half and proves a declared, idle region
moves and keeps working. Nothing until now proved the refusal.

WHY IT NEEDS AN INSTRUMENT, AND WHY THAT INSTRUMENT IS NOT THE FILLER. The
subject has to be BOTH declared movable and worker-owning, and no shipped
package is: SHEET declares and hires nobody, every package that hires a worker
declares nothing. The obvious move is to give `tests/filler` both, and it does
not work - a package reaches `mem_claim` only from inside its OWN callback, so
when the filler's forcing ask triggers a compaction `[wm_pkgd]` is 1 and
`wm_pkgs[0]` is the filler's own segment, and `mem_frameless` refuses its
region for having a frame in it. That is correct and it is not this test:
built that way, with the worker pin REMOVED from the kernel, the filler's
region still stood still and the row stayed green. (This is
docs/plans/HEAP-UNPIN-PLAN.md 10.9's own finding met from the other side.)

So the asker and the subject are two packages. `tests/pinme` is the subject and
does nothing else: declare at entry, hire a do-nothing worker at the first
paint, count relocations. The declaration is checked here rather than assumed
(`MC_RLOC` out of the kernel's own table), because a refused declaration looks
identical from inside the package (SPEC.md 66.5.6.2) and would make every
assertion below vacuous.

WHY SHEET IS STILL IN THE ROOM. "The region did not move" is worth nothing on
its own - it is also what a pass that never ran says, and what a pass with
nowhere to move anything says. SHEET is the control: it is declared, it owns no
worker, and it sits in the same arena, so the run asserts PINME's region stood
still WHILE SHEET'S MOVED. One without the other is not a result.

WHY TWO PAINTS, AND IT IS THE OTHER HALF OF THE EXPERIMENT. The first version
of this row was ALSO a false green for a second, unrelated reason, and it is
regmove.py's own stated one a step along: a region is claimed top-down, so
whichever package opens into the top of a hole lands flush against the ceiling
and CANNOT MOVE whatever any predicate says. The subject needs a hole ABOVE it.
So two Paint instances are opened as spacers - one above SHEET, one between
SHEET and PINME - and closing both leaves exactly that shape:

    ceiling [ PAINT#1 ][ SHEET ][ PAINT#2 ][ PINME ] ......... free
    closed  [  hole   ][ SHEET ][  hole   ][ PINME ] ......... free
    then    [ FILLER  ][ SHEET ][  hole   ][ PINME ][ fill ] 8KB

The filler opens LAST, into the top of the first hole, and its fill takes the
low arena down to 8KB - so the free space is three runs and an ask bigger than
any of them is one only a merge can answer. The descending pass then has
somewhere to put SHEET (assertion 4) and somewhere it WOULD put PINME
(assertion 3). With the `I_TASK` test removed from `mem_frameless`, PINME packs
up into the second hole and the row goes red, which is the check that says this
row is worth having (docs/WRITING-TESTS.md 1).

FOUR ASSERTIONS:

  1. the filler's region is declared movable at all - MC_RLOC from mem_tab;
  2. the worker was really hired - I_TASK in the instance record, not the
     package's own byte, because a refused OSAPI_TASK_SPAWN is a normal
     outcome and the package would look identical;
  3. THE FILLER'S REGION DID NOT MOVE, and `fl_reloc` was never called - the
     second is what tells "it did not move" from "it moved and the kernel
     skipped the proc", which are the same base only by luck;
  4. SHEET'S REGION DID - so the pass ran, reached the descending arm, and was
     capable of moving a region in this very arena.

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1): take the `I_TASK` test out of
`mem_frameless` (kernel/memory.inc) and assertion 3 goes red - the filler's
region packs with the rest.
"""
import argparse
import os
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
import sheetmove                                        # noqa: E402
import os88build                                       # noqa: E402

DISK = "build/regpin360.img"
u16, claims, uncovered = sheetmove.u16, sheetmove.claims, sheetmove.uncovered
pkg_seg, park = sheetmove.pkg_seg, sheetmove.park

PM_NRELOC = 4           # tests/pinme/pinme.asm's bss offsets, from its tail
PM_WK = 6
PM_MOV = 7
# ...and the KERNEL's, imported and never retyped (tools/os88geom.py's header):
# the first draft of this row had I_TASK = 8 where the kernel says 3, which
# tests/unit/t_mirror.py caught and a run would not have - assertion 2 would
# have read a neighbouring byte and called a hired worker missing.
I_RECSZ, I_TASK = os88geom.I_RECSZ, os88geom.I_TASK
I_SPTR, INST_MAX = os88geom.I_SPTR, os88geom.INST_MAX


def pm_bss(m, seg, off, n=2):
    """A PINME bss byte or word. Its bss follows the image, and `image` in the
    .o88 header is where that starts - read off the file rather than guessed,
    because it moves whenever the package does."""
    return m.readseg(seg, pm_bss.base + off, n)


def inst_task(m, S, seg):
    """I_TASK for the instance whose region is `seg`, or None."""
    inst = m.read(S("inst_tab"), INST_MAX * I_RECSZ)
    for i in range(INST_MAX):
        if u16(inst, i * I_RECSZ + I_SPTR) == seg:
            return inst[i * I_RECSZ + I_TASK]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    S = os88sym.linear
    os88fixture.need(DISK)

    import struct
    pm_bss.base = struct.unpack(
        "<H", open(os88build.at("build/pinme.o88"), "rb").read()[8:10])[0]

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
            """Open `name` and return (region segment, window) for the window
            that was not there before - two Paints share a title, so the NEW
            slot is the only way to tell them apart."""
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
            # out of the way, so every later title bar is clickable
            mo.drag(w.x + 60, w.y + 9, at[0] + 60, at[1] + 9)
            os88marty.settle(m)
            w = [x for x in os88geom.windows(m, S) if x.i == w.i][0]
            return seg, w

        # ceiling [ PAINT#1 ][ SHEET ][ PAINT#2 ][ PINME ] - see the header
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
            print("FAIL: the regions are not stacked in open order, so "
                  "closing the two Paints does not make the two holes this "
                  "row is about")
            return 1

        bad = 0
        reg = [c for c in claims(m, S) if c[0] == pm_seg]
        rloc = reg[0][3] if reg else 0
        print("  1 pinme declared movable   %s"
              % ("MC_RLOC=%04x, and the package agrees (%d)"
                 % (rloc, pm_bss(m, pm_seg, PM_MOV, 1)[0]) if rloc else
                 "NO  <-- OSAPI_MEM_MOVABLE was refused; every assertion "
                 "below would be vacuous"))
        bad += not rloc

        task = inst_task(m, S, pm_seg)
        hired = task is not None and task != 0xFF
        print("  2 the worker was hired     %s"
              % ("I_TASK=%d (the package's own byte says %d)"
                 % (task, pm_bss(m, pm_seg, PM_WK, 1)[0]) if hired else
                 "NO  <-- I_TASK=%s, so the pin under test is not armed"
                 % task))
        bad += not hired

        # --- the two holes ---------------------------------------------------
        for w, what in ((p1, "Paint #1"), (p2, "Paint #2")):
            front(w, what)
            mo.click(w.x + 8, w.y + 9)                  # its close box
            os88marty.settle(m)
            if any(x.i == w.i and x.visible for x in os88geom.windows(m, S)):
                print("FAIL: %s did not close, so its hole never opened"
                      % what)
                return 1

        # --- THE FILLER LAST, and it is the ASKER, never the subject ---------
        raise_disk()
        fl_seg, fl = open_named("FILLER.O88", 8, (150, 136))
        print("filler %04x  (the asker)" % fl_seg)
        raise_disk()
        front(fl, "the Filler")
        for _ in range(5):
            m.key("KeyA")
            time.sleep(6)
            os88marty.settle(m)
            if pkg_seg(m, S, "Sheet")[0] not in (sh_seg, None):
                break

        pm_now = pkg_seg(m, S, "PinMe")[0]
        sh_now = pkg_seg(m, S, "Sheet")[0]
        nreloc = u16(pm_bss(m, pm_now or pm_seg, PM_NRELOC))
        ok3 = pm_now == pm_seg and nreloc == 0
        print("  3 pinme region STAYED      %s"
              % ("%04x, pm_reloc called %d times" % (pm_now, nreloc) if ok3
                 else "MOVED %04x -> %s (pm_reloc called %d times) <-- a "
                 "region whose worker holds the segment on its stack was "
                 "packed" % (pm_seg, "%04x" % pm_now if pm_now else "GONE",
                             nreloc)))
        bad += not ok3

        moved = sh_now is not None and sh_now != sh_seg
        print("  4 sheet region MOVED       %s"
              % ("%04x -> %04x, so the pass ran and could move a region"
                 % (sh_seg, sh_now) if moved else
                 "NO (%s) <-- the pass moved nothing, so assertion 3 proves "
                 "nothing either" % ("%04x" % sh_now if sh_now else "GONE")))
        bad += not moved

        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""How deep has TASK 0's stack actually been? (SPEC.md 15.1)

    make && python3 tests/stk0water.py [machine]

`STK0_SIZE` is 4x a measured 246-byte high-water mark, and SPEC.md 15.1 says
in as many words: **"Redo the fill probe before lowering either."** This is
that probe, automated - it used to be a hand edit to `kmain` and a hand read
afterwards, which is why it has been run once.

Task 0's stack is the one worth the trouble. It is where the UI task runs, so
it carries the DEEPEST kernel paths there are (a full `wm_paint_all` with a
menu open), and it is the one stack `sch_switch`'s canary SKIPS - so nothing
at run time will ever tell you it was too small. `tools/stkwater.py` cannot
read it: that reads the slices `task_spawn` fills, and task 0 owns no slice.

**How.** Break at `kmain`'s `sti` - the instruction after `mov sp, STK0_TOP`,
so SS:SP is live and the stack is at its shallowest - fill everything BELOW
SP with 0xCC, let go, drive the machine as hard as this harness can, and read
the region back. The deepest byte that is no longer 0xCC is the water mark.
ISR frames are included by construction: the tick and both mouse handlers run
on whichever stack they interrupt, and until SPEC.md 9.10/8.5 that was this
one.

**The number is expected to have IMPROVED since 246**, which is the point of
re-running it: that figure was taken with ISR frames included, and SPEC.md
9.10 has since moved both mouse ISRs onto a private stack (~48 bytes) and 8.5
the ROM's `int 08h` chain (~50).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88marty as M                                        # noqa: E402
import os88sym                                               # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
from os88geom import WIN_SIZE, MAX_WIN                        # noqa: E402
import struct                                                # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
DEFS = tuple(d for d in os.environ.get("OS88_DEFINES", "").replace(",", " ").split() if d)
FILL = 0xCC
SYS = os.environ.get("OS88_SYSIMG", "build/os8088-360.img")
APPS = os.environ.get("OS88_APPSIMG", "build/apps360.img")


def wins(m, S):
    blob = m.read(S("wm_wins"), WIN_SIZE * MAX_WIN)
    return [tuple(struct.unpack_from("<H", blob, i * WIN_SIZE + o)[0]
                  for o in (2, 4, 6, 8))
            for i in range(MAX_WIN)
            if struct.unpack_from("<H", blob, i * WIN_SIZE)[0] & 2]


eq = os88sym.equates(DEFS)
LOW = eq["LOW_SEG"] * 16
BOT = LOW + eq["KLOW_SIZE"]              # the lowest byte STK0 may reach
TOP = LOW + eq["STK0_TOP"] + 2           # ...and one past the top
SIZE = eq["STK0_SIZE"]

with M.launch(SYS, apps=APPS, machine=MACHINE) as m:
    S = lambda n: m.sym(n, DEFS)                              # noqa: E731
    M.settle(m, limit=180)
    M.no_saver(m)                                             # SPEC.md 79
    print("== %s : task 0's stack, %d bytes at %#07x..%#07x ==" %
          (MACHINE, SIZE, BOT, TOP))

    # --- WAIT UNTIL TASK 0 IS ASLEEP, then fill only what is DEMONSTRABLY
    #     below its frame.
    #
    # The first version read sch_tasks[0].T_SP and filled to 32 below it. That
    # is a RACE: T_SP is only current while the task is switched out, and if
    # task 0 happens to be running the word holds whatever it was at the last
    # switch - far shallower than the live SP. Filling against a stale value
    # wrote over live frames, and what came back was a machine whose mouse
    # would not move, which reads like a harness bug and is not one.
    #
    # So three things are checked, not one: the CPU is NOT on task 0
    # ([sch_cur] != 0), task 0 is SLEEPING (T_STATE = 2, which SPEC.md 8.1.2
    # makes the normal state of an idle ui_task), and its saved SP is inside
    # the top MARGIN bytes. Only then is everything below MARGIN provably free.
    T_STATE, T_SP, T_SIZE = 0, 2, 8
    MARGIN = 64
    for _ in range(40):
        cur = m.read(S("sch_cur"), 1)[0]
        st = m.read(S("sch_tasks") + T_STATE, 1)[0]
        sp = int.from_bytes(m.read(S("sch_tasks") + T_SP, 2), "little")
        if cur != 0 and st == 2 and eq["STK0_TOP"] - MARGIN < sp <= eq["STK0_TOP"]:
            break
        M.settle(m)
    else:
        sys.exit("task 0 never settled asleep with a shallow frame "
                 "(sch_cur=%d, T_STATE=%d, SP=%04X)" % (cur, st, sp))
    # ...and START ABOVE THE CANARY. `575a` (SCH_MAGIC) sits at the bottom of
    # task 0's stack as it does at the bottom of every slice, and filling over
    # it is what a stack overflow looks like to the kernel: the machine halts,
    # `settle` returns happily because the screen is static, and the only
    # symptom a harness sees is a pointer that will not move.
    #
    # SPEC.md 15.1 still says task 0's is "the one stack sch_switch's canary
    # skips". IT IS NOT, since SPEC.md 8.7's sch_stkbase: that table holds
    # STK0_BOT at slot 0, sched_init seeds it with SCH_MAGIC, and sch_switch
    # compares it on EVERY switch and falls to sch_stkdie. This probe is how
    # that was noticed - the machine died the moment the fill reached the
    # magic word, on a shipping kernel.
    lo = BOT + 2
    hi = TOP - MARGIN
    m.write(lo, bytes([FILL]) * (hi - lo))
    print("   task 0 asleep at SP=%04X (%d bytes in use); filled %#07x..%#07x"
          % (sp, TOP - (LOW + sp), lo, hi))
    print("   = the bottom %d bytes of %d, so anything reaching them is water"
          % (hi - lo, SIZE))

    # --- drive it as hard as this harness can ------------------------------
    mo = Mouse(marty=m)
    vw = int.from_bytes(m.read(S("vid_w"), 2), "little")
    step = int.from_bytes(m.read(S("desk_zstep"), 2), "little")
    h1 = int.from_bytes(m.read(S("desk_zh1"), 2), "little")
    dy = 32 + step + h1 // 2
    mo.dblclick(vw - 40, dy); M.settle(m)                     # a Disk window
    w = wins(m, S)
    if w:
        x, y = w[-1][0] + w[-1][2] // 2, w[-1][1] + 9
        mo.menu(x, y, x + 40, y + 20); M.settle(m)            # ...dragged
        mo.menu(x + 40, y + 20, x, y); M.settle(m)
    # Every menu OPENED and closed again, by pressing and releasing ON THE BAR
    # so that nothing is picked. Releasing inside the pane SELECTS an item,
    # which launched the About box and left the machine animating for ever;
    # `settle` then reports the screen never settling, which reads as a hang
    # and is a mis-aimed click.
    #
    # This is the path worth driving: SPEC.md 15.1 names "a full wm_paint_all
    # with a menu open" as the deepest thing the UI task does, and the menu
    # save-under (SPEC.md 12.4) is taken and dropped on every one of these.
    for mx in (20, 60, 100, 140):
        mo.menu(mx, 8, mx, 8)                                 # open, then close
        M.settle(m)

    # ...and the two DEEPEST things SPEC.md 15.1's original drive had that a
    # Disk window and a menu do not: a PACKAGE LAUNCH (loader, instance
    # record, window creation and the first paint, all on this stack) and the
    # CONTROL PANEL, which on kern_small is an on-demand module and so adds
    # mod_need, a mount and a disk read underneath the same frame. Without
    # these the probe reads 234 and SPEC's harder drive read 246, and the gap
    # IS these.
    w = wins(m, S)
    if w:
        d = w[0]
        mo.dblclick(d[0] + 40, d[1] + 18 + 30); M.settle(m)   # launch one
        w2 = wins(m, S)
        if len(w2) > len(w):
            a = w2[-1]
            mo.menu(a[0] + a[2] // 2, a[1] + 9,
                    a[0] + a[2] // 2 + 24, a[1] + 29); M.settle(m)
    mo.menu(20, 8, 20, 8); M.settle(m)

    # --- read the water ----------------------------------------------------
    blob = m.read(lo, hi - lo)
    print("   bottom 32 bytes after the drive: %s" % blob[:32].hex())
    runs = [i for i, b in enumerate(blob) if b != FILL]
    if runs:
        print("   disturbed offsets: first %d, then %s ... last %d (%d total)"
              % (runs[0], runs[1:8], runs[-1], len(runs)))
    deep = next((i for i, b in enumerate(blob) if b != FILL), None)
    if deep is None:
        print()
        print("   the whole fill SURVIVED: task 0 never came below %d bytes"
              % MARGIN)
        print("   -> STK0_SIZE 512 leaves at least %d bytes spare (%.1fx)"
              % (512 - MARGIN, 512.0 / MARGIN))
        used = "<= %d" % MARGIN
    else:
        used = len(blob) - deep + MARGIN
        print()
        print("   deepest byte touched: %d of %d used (%.1f%%), %d spare"
              % (used, SIZE, 100.0 * used / SIZE, SIZE - used))
        for cand in (1024, 768, 512, 384, 256):
            print("   STK0_SIZE %5d -> %.2fx margin%s"
                  % (cand, cand / float(used),
                     "   <-- today" if cand == SIZE else
                     ("   TOO SMALL" if cand <= used else "")))

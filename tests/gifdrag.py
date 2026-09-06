#!/usr/bin/env python3
"""The field's freeze: the heap page open while PAINT holds OS8088.GIF.

    make && python3 tests/gifdrag.py [machine]

The recipe is the report, verbatim (SPEC.md 8.7.4): Task Manager on the HEAP
page, a Disk window on `A:\\MEDIA`, `OS8088.GIF` opened through the
association, then the Paint window dragged again and again. On a real IBM 5150
that halted the machine dead - `sch_switch` found the Task Manager worker's
canary gone and took `sch_stkdie`'s `cli`/`hlt`, with nothing on the glass to
say so.

**IT ASSERTS THE MARGIN, NOT THE SURVIVAL.** "The machine did not freeze" is
what every run before the bug was reported also said: the emulator's interrupt
floor is 32 bytes where SPEC.md 8.7 sizes against 64 on real iron, so the same
walk that died in the field reads 180 of 192 here and passes. What this file
watches is the WATER - `task_spawn` fills every slice with 0xCC (SPEC.md 8.3),
so each one carries its own high mark - and it fails when any slice is fuller
than `BAR`, which is the emulator's honest question: *is there still room for
the 32 bytes of interrupt frame this machine is not charging?*

Task 0's stack is filled and read the way tests/stk0water.py does it, because
the UI task carries the other half of this recipe - `tm_click` and `tm_paint`
run there, and they are DEEPER than the worker (114 and 102 against 96).
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88marty as M                                        # noqa: E402
import os88sym                                               # noqa: E402
import os88geom                                              # noqa: E402
import stkwater                                              # noqa: E402
from os88mouse import Mouse                                  # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla"
DEFS = tuple(d for d in os.environ.get("OS88_DEFINES", "").replace(",", " ").split() if d)
FILL = 0xCC
SYS = os.environ.get("OS88_SYSIMG", "build/os8088-360.img")
APPS = os.environ.get("OS88_APPSIMG", "build/apps360.img")
NDRAG = int(os.environ.get("OS88_DRAGS", "8"))

# How full a slice may get HERE. The emulator's floor is 32 (slot 1's own
# reading) and the design floor is 64, so a slice that is 80% full on this
# machine has ~20% of itself left for a term it is being undercharged by half.
BAR = 0.80

eq = os88sym.equates(DEFS)
LOW = eq["LOW_SEG"] * 16
BOT = LOW + eq["KLOW_SIZE"]
TOP = LOW + eq["STK0_TOP"] + 2
SIZE = eq["STK0_SIZE"]
MARGIN = 64
T_STATE, T_SP = 0, 2

FAILED = []


def say(*a):
    print(*a)
    sys.stdout.flush()


def fail(m):
    FAILED.append(m)
    say("  FAIL %s" % m)


def tick(m, frames):
    """Let the guest run and leave it running.

    NOT M.settle: the Task Manager repaints twice a second for ever, so a
    screen-stillness wait never returns once its window is up.
    """
    m.advance(frames=frames)
    m.run()


with M.launch(SYS, apps=APPS, machine=MACHINE) as m:
    S = lambda n: m.sym(n, DEFS)                              # noqa: E731
    M.settle(m, limit=180)
    M.no_saver(m)
    mo = Mouse(marty=m)
    vw = int.from_bytes(m.read(S("vid_w"), 2), "little")
    vh = int.from_bytes(m.read(S("vid_h"), 2), "little")
    SIZES = stkwater.slice_sizes(DEFS)
    NSLOT = stkwater.slots(DEFS)
    say("== %s : %dx%d, task 0 %d bytes, %d worker slice(s) %s =="
        % (MACHINE, vw, vh, SIZE, NSLOT,
           "/".join(str(v) for v in SIZES[:NSLOT])))

    lo, hi = BOT + 2, TOP - MARGIN

    def refill():
        """Fill below task 0's frame, once it is provably asleep."""
        for _ in range(40):
            cur = m.read(S("sch_cur"), 1)[0]
            st = m.read(S("sch_tasks") + T_STATE, 1)[0]
            sp = int.from_bytes(m.read(S("sch_tasks") + T_SP, 2), "little")
            if cur != 0 and st == 2 and eq["STK0_TOP"] - MARGIN < sp <= eq["STK0_TOP"]:
                m.write(lo, bytes([FILL]) * (hi - lo))
                return True
            tick(m, 30)
        return False

    def slices():
        mem = m.read(S("sch_stacks"), sum(SIZES[:NSLOT]))
        out = []
        for slot, used, free in stkwater.water(mem, NSLOT, SIZES):
            if used is None:
                continue
            sz = SIZES[slot - 1]
            out.append((used / float(sz), slot, used, sz))
        out.sort(reverse=True)
        return out

    def step(label):
        blob = m.read(lo, hi - lo)
        deep = next((i for i, b in enumerate(blob) if b != FILL), None)
        used = MARGIN if deep is None else len(blob) - deep + MARGIN
        canary = int.from_bytes(m.read(BOT, 2), "little")
        sl = slices()
        say("  %-22s t0 %3d/%d(%2.0f%%) canary %04X | %s"
            % (label, used, SIZE, 100.0 * used / SIZE, canary,
               "  ".join("s%d %d/%d(%.0f%%)" % (n, u, z, 100 * p)
                         for p, n, u, z in sl[:3])))
        if canary != 0x5A57:
            fail("%s: task 0's canary is gone - it overran STK0_SIZE" % label)
        if used > SIZE * BAR:
            fail("%s: task 0 is %d of %d (%.0f%%), over the %.0f%% bar"
                 % (label, used, SIZE, 100.0 * used / SIZE, 100 * BAR))
        for p, n, u, z in sl:
            if p > BAR:
                fail("%s: slot %d is %d of its %d-byte slice (%.0f%%), over "
                     "the %.0f%% bar - and this machine's interrupt floor is "
                     "HALF the one SPEC.md 8.7 sizes against (SPEC.md 8.7.4)"
                     % (label, n, u, z, 100 * p, 100 * BAR))
        a = m.status()
        time.sleep(2)
        if m.status()["instructions"] == a["instructions"]:
            fail("%s: the guest retired no instructions - a cli/hlt, which is "
                 "sch_stkdie (SPEC.md 8.8 draws the panel that says so)" % label)
            m.shot("build/gifdrag-freeze.png", rendered=True)
        return used

    if not refill():
        sys.exit("gifdrag: task 0 never settled asleep with a shallow frame")
    step("after boot")

    mo.menu(12, 8, 60, 60); tick(m, 90)                      # chip -> Tasks
    for _ in range(2):                                       # ...to the heap page
        mo.click(vw // 2 - 60, vh - 60); tick(m, 60)
    step("heap page")

    mo.dblclick(vw - 30, 45); tick(m, 240)                   # drive A
    step("disk window")
    mo.dblclick(140, 178); tick(m, 240)                      # MEDIA
    step("MEDIA")
    refill()
    mo.dblclick(160, 146); tick(m, 1200)                     # OS8088.GIF -> PAINT
    step("paint open")

    import random
    random.seed(1)
    for n in range(NDRAG):
        w = next((w for w in os88geom.windows(m)
                  if w.visible and w.title.startswith("Paint")), None)
        if w is None:
            fail("the Paint window is gone after %d drag(s)" % n)
            break
        fx, fy = w.x + w.w // 2, w.y + 4          # its real caption strip
        tx = max(4, min(vw - 8, fx + random.choice((-90, -50, 40, 90, 140))))
        ty = max(22, min(vh - 30, fy + random.choice((-12, 30, 60, -40, 90))))
        mo.drag(fx, fy, tx, ty)
        tick(m, 400)
        if step("drag %d" % (n + 1)) is None:
            break

    if FAILED:
        say("gifdrag: %d FAILED" % len(FAILED))
        for f in FAILED:
            say("  " + f)
        sys.exit(1)
    say("gifdrag: pass - no slice over %.0f%% through the whole recipe"
        % (100 * BAR))

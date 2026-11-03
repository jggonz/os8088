#!/usr/bin/env python3
"""The Task Manager's MAX RUN is not walled by the Task Manager (SPEC.md 28.4.1)

    make && python3 tests/tmmaxrun.py [--machine os8088_xt_vga]

MAX RUN is the largest single claim the machine would hand out. The page
asks it from inside its own callback, and a package standing in its region
is the one claim the compactor cannot move - so asked plainly
(OSAPI_MEM_P's MEMP_AVAIL) the page counted ITSELF as a wall, and a hole
above its region read as fragmentation that no other program would ever
meet. The page asks the WHAT-IF instead (OSAPI_MEM_COMPACT, MEMC_WHATIF_P,
SPEC.md 66.4.3.2): planned as if its own region could move, which it can -
it is declared movable and its worker restartable.

  WALL   the Calculator opened FIRST, so its region is claimed above the
         Task Manager's (regions are claimed top-down), then closed: a hole
         stands between the Task Manager's region and the top. MAX RUN must
         equal AVAIL - nothing else on a bare 360KB boot pins anything - where
         the plain ask read AVAIL less the Calculator's region. DETAIL is
         clicked before the read, so the figure is the CALLBACK's - the UI
         task inside the page's own code, which is where the plain ask walls
         it; the worker's refresh never did

Read off the page's own bss ([tm_avl], [tm_maxrun], paragraphs) and its claim
snapshot, which places the two regions and the hole.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88ui, os88marty, dispapps

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

MEM_MAX, CLS_RECSZ = 32, 6                  # apps/os88api.inc
TITLE_H = 18
fails = []

with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                 machine=MACHINE, quick=True) as ui:
    ui.still(0)
    m = ui.m

    ui.path("B:/APPS/CALC.O88")             # FIRST: the top of the heap
    calc = ui.window("Calculator")
    ui.path("A:/SYSTEM/TASKMGR.O88")        # ...and this under it
    got, i = None, 0
    while dispapps.pkg_seg(m, i) is not None:
        got = dispapps.pkg_seg(m, i)
        i += 1
    if got is None:
        sys.exit("the Task Manager did not open - %r" % ui.titles())
    slot, tseg = got
    P = os88marty.Pkg(m, slot, image=dispapps.img_size("taskmgr"))

    def off(name):
        return dispapps.bss_off("taskmgr", name)

    w = [x for x in ui.windows() if x.i == slot][0]
    ui.mo.click(w.x + 20, w.y + TITLE_H + 40)       # onto the memory page
    ui._wait(lambda: P.byte(off("tm_view")) == 1, "the memory page", guest=10)

    def regions():
        snap = P.read(off("tm_claims"), MEM_MAX * CLS_RECSZ)
        out = []
        for r in range(MEM_MAX):
            o = r * CLS_RECSZ
            seg = int.from_bytes(snap[o:o + 2], "little")
            para = int.from_bytes(snap[o + 2:o + 4], "little")
            if seg:
                out.append((seg, para))
        return sorted(out)

    ui._wait(lambda: P.word(off("tm_avl")) != 0, "the page's figures",
             guest=10)
    avl0 = P.word(off("tm_avl"))
    ui.close(calc)
    ui._wait(lambda: P.word(off("tm_avl")) > avl0,
             "AVAIL growing by the Calculator's region", guest=15,
             snapshot=lambda: "avl %04X" % P.word(off("tm_avl")))
    # A CLICK, which is the UI task inside the page's own callback - where
    # the plain ask walls the region. The worker's refresh does not: the
    # plan answers "parked" for every worker (SPEC.md 66.4.3.5)
    TMM_HDR_Y = 66                          # apps/taskmgr/taskmgr.asm
    cx, cy = P.word(off("tm_cx")), P.word(off("tm_cy"))
    hy = cy + TMM_HDR_Y - P.word(off("tm_xoff")) + 4
    det = P.byte(off("tm_detail"))
    ui.mo.click(cx + 8 + 16, hy)            # the DETAIL switch: tm_lclick
    ui._wait(lambda: P.byte(off("tm_detail")) != det,
             "DETAIL switched (a paint in the callback)", guest=10)
    ui.still(1.0)
    avl, mrun = P.word(off("tm_avl")), P.word(off("tm_maxrun"))
    tm = [(s, p) for s, p in regions() if s == int(tseg)]
    top = max(s + p for s, p in regions())
    print("task manager region %s; highest claim ends %04X" % (
        ["%04X+%04X" % r for r in tm], top))
    print("AVAIL %04Xh (%.1fK)  MAX RUN %04Xh (%.1fK)  hole %d paragraphs"
          % (avl, avl / 64, mrun, mrun / 64, avl - avl0))
    if not tm:
        fails.append("no claim at the Task Manager's segment %04X"
                     % int(tseg))
    elif tm[0][0] + tm[0][1] == top:
        print("(the Task Manager's region is the highest claim: the hole is "
              "above it, between it and the top of the heap)")
    if mrun != avl:
        fails.append("MAX RUN %.1fK is not AVAIL %.1fK: the page walled "
                     "itself" % (mrun / 64, avl / 64))

print()
if fails:
    for f in fails:
        print("FAIL:", f)
    sys.exit(1)
print("tmmaxrun: ok")

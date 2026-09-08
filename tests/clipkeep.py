#!/usr/bin/env python3
"""wm_clip_set drops the raise cache only when there is something to draw
   (SPEC.md 11.96.18)

    make && python3 tests/clipkeep.py [--machine os8088_xt_vga]

`wm_clip_set` drops the drawer's raise cache - SPEC.md 11.96's blanket safety
net, and the right thing for a window that is about to draw. It ran at the TOP
of the routine, before the occlusion walk, so a window that is visible but
WHOLLY COVERED destroyed its cache and was told two calls later that there was
nothing to draw. Every app with a background painter did it on every poll,
which docs/plans/completed/SAVEUNDER-LIVE-PLAN.md 3.1 measured as the single thing defeating
the whole save-under proposal - and costed as a rework of every candidate app.
It is a reorder in one place, +6 bytes.

  PARTIAL   partly covered, the worker's poll DOES reach wm_su_drop for this
            window - it is drawing its visible fragments, so the glass is
            being kept current and the banked copy is the frame before.
  WHOLLY    wholly covered, the same poll does NOT. That is the change.

**IT ASSERTS THE CALL, NOT THE CACHE WORD, and that is deliberate.** Reading
`wm_su_segs` cannot answer this question here: whether a bank lands at all is
sensitive to the observation - `mo.dblclick` and one read reports the cache
alive, a tight advance/run sampling loop reports it dead from ~130 ms, and
arming a breakpoint just after the gesture stops the bank happening at all.
An exec breakpoint on `wm_su_drop` with BX compared against the window record
is stable in every run, needs no WF_SAVEU forced into the record, and is the
reorder's claim word for word. SPEC.md 11.96.18.1 is the record of the three
readings and why the cache word is not the instrument.

THE SUBJECT IS THE TASK MANAGER ON ITS PERFORMANCE VIEW, and that is chosen
rather than convenient: its worker arms a clip every TM_INT whether or not it
has anything to draw, which is 3.1's own worked example of the shape.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp, dispcorner, dispapps

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
KERNEL_SEG = 0x60
POLLS = 8                               # TM_INT is 9 ticks, ~0.5 s each
fails = []


def tick(mm, card=None):
    mm.advance(frames=110)
    mm.run()


def zord(m):
    n = m.read(S("wm_zn"), 1)[0]
    return list(m.read(S("wm_zord"), max(n, 1)))[:n]


def drops(m, ours, frames=110, rounds=POLLS):
    """Run `rounds` worker intervals with wm_su_drop armed; count the calls
    whose BX is OUR window, and the calls for anybody else."""
    m.bp_exec("wm_su_drop")
    at = m.sym("wm_su_drop")
    mine, other = 0, 0
    for _ in range(rounds):
        m.run()
        for _ in range(40):
            if not m.stopped():
                m.advance(frames=frames // 8)
            if not m.stopped():
                continue
            r = m.regs()
            here = ((r["cs"] & 0xFFFF) << 4) + (r["ip"] & 0xFFFF)
            if here != at:
                break
            if (r["bx"] & 0xFFFF) == ours:
                mine += 1
            else:
                other += 1
            m.run()
    m.breakpoints([])
    m.run()
    return mine, other


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    os88marty.no_saver(m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    dx, dy, dw, dh = dispcp.win_rect(m, S, disk)
    dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "SYSTEM")
    dispcp.open_named(m, mo, S, tick, dx, dy, "TASKMGR.O88")
    tick(m)
    got, i = None, 0
    while dispapps.pkg_seg(m, i) is not None:
        got, i = dispapps.pkg_seg(m, i), i + 1
    if got is None:
        sys.exit("the Task Manager did not open - %r" % os88geom.windows(m))
    slot, seg = got
    w = [x for x in os88geom.windows(m) if x.i == slot][0]
    if "Task" not in w.title:
        sys.exit("the newest package window is %r" % w.title)
    ours = S("wm_wins") - (KERNEL_SEG << 4) + slot * os88geom.WIN_SIZE
    print("SUBJECT : %s (%d,%d) %dx%d  record %04X"
          % (w.title, w.x, w.y, w.w, w.h, ours))

    # --- PARTIAL -----------------------------------------------------------
    mo.click(dx + 30, dy + 9)
    mo.to(*dispcorner.PARK)
    os88marty.until(m, lambda mm: zord(mm)[-1] == disk,
                    "the drive window to come to the front", limit=90)
    tick(m)
    part = not (dx <= w.x and dy <= w.y
                and dx + dw >= w.x + w.w and dy + dh >= w.y + w.h)
    mine, other = drops(m, ours)
    print("PARTIAL : %d wm_su_drop calls for us, %d for others (partly "
          "covered=%s)" % (mine, other, part))
    if not part:
        fails.append("SETUP: the Disk window covers the Task Manager wholly, "
                     "so PARTIAL is not testing a partial cover")
    if not mine:
        fails.append("PARTIAL: the worker's poll never reached wm_su_drop for "
                     "this window - it should, because it IS drawing its "
                     "visible fragments and the glass is being kept current")

    # --- WHOLLY ------------------------------------------------------------
    # ZOOM the cover: a double-click on a title bar takes the whole desktop
    # band (SPEC.md 11.95), so this needs no navigation - which matters,
    # because nothing on this desktop ever settles.
    mo.dblclick(dx + 40, dy + 9)
    mo.to(*dispcorner.PARK)
    tick(m)
    big = [x for x in os88geom.windows(m) if x.i == disk][0]
    whole = (big.x <= w.x and big.y <= w.y
             and big.x + big.w >= w.x + w.w and big.y + big.h >= w.y + w.h)
    mine, other = drops(m, ours)
    print("WHOLLY  : %d wm_su_drop calls for us, %d for others (zoomed cover "
          "(%d,%d) %dx%d wholly=%s)"
          % (mine, other, big.x, big.y, big.w, big.h, whole))
    if not whole:
        fails.append("WHOLLY: the zoomed window does not cover the Task "
                     "Manager, so this leg is a second partial cover")
    if mine:
        fails.append("WHOLLY: %d wm_su_drop calls for this window across %d "
                     "worker polls - wm_clip_set is dropping the cache before "
                     "it discovers there is nothing to draw (SPEC.md 11.96.18)"
                     % (mine, POLLS))

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  dropped when it can draw, left alone when it cannot")

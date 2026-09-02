#!/usr/bin/env python3
"""A marked window does not paint where something above it is about to
   (SPEC.md 11.3.3)

    make && python3 tests/dmgcull.py [--machine os8088_xt_vga]

SPEC.md 11.91's damage pass redraws a marked window WHOLE and lets the windows
above it paint over the part they own. It is correct and it is seconds of
visible rubbish on a 4.77MHz machine: drag a window further over the Task
Manager and its whole obscured section is lettered and then covered.

11.3.3's cull confines W_PAINT to the region nothing above will cover, and
rounds a glyph cell OUTWARD rather than dropping it - which is what makes an
armed region safe here where 11.97.1 says it is not, because the pixels
outside belong to a window painted over them a few instructions later.

WHAT IT COUNTS IS CELLS PUT ON THE GLASS, and that distinction is the whole
gate. A culled cell is still a CALL: `font_run` is entered and then paints
nothing, so a call count says the fix did nothing. So this reads the run's
length and the armed clip region at every font_run the package makes, and
works out which of its cells the region will actually let through.

Measured A/B, dragging a window further over the Task Manager's memory page:

    before   83 runs, 479 cells painted, 452 of them inside the rect the
             mover was about to occupy
    after   323 runs,  53 cells painted,  26 of them inside it

The run count goes UP because a chunk the region cut zeroes its own check word
and is retried (28.2) - 240 more calls that paint nothing, ~47us each, against
426 cells at ~900us that are no longer painted at all.

  CULLED    the cells landing under the mover must be a small fraction of the
            452 that used to.
  DREW      ...and it must still have painted the part of itself that stays
            visible, or the leg is passed by drawing nothing at all.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp, dispcorner, dispapps
import dispcells

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
fails = []
KSEG = os88geom.KERNEL_SEG
TITLE_H = 18
STEP = 8
WCR_SZ = 8


def tick(mm, card=None):
    mm.advance(frames=110)
    mm.run()


def view(m, seg):
    return m.read((seg << 4) + dispapps.img_size("taskmgr")
                  + dispapps.bss_off("taskmgr", "tm_view"), 1)[0]


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    os88marty.no_saver(m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    dx, dy, _, _ = dispcp.win_rect(m, S, disk)
    dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "SYSTEM")
    dispcp.open_named(m, mo, S, tick, dx, dy, "TASKMGR.O88")
    tick(m)
    got, i = None, 0
    while dispapps.pkg_seg(m, i) is not None:
        got, i = dispapps.pkg_seg(m, i), i + 1
    slot, seg = got
    w = [x for x in os88geom.windows(m) if x.i == slot][0]
    for _ in range(4):
        if view(m, seg) == 1:
            break
        mo.click(w.x + 20, w.y + TITLE_H + 40)
        mo.to(*dispcorner.PARK)
        tick(m)
    d = [x for x in os88geom.windows(m) if x.i == disk][0]
    mo.click(d.x + 30, d.y + 9)
    mo.to(*dispcorner.PARK)
    os88marty.settle(m, limit=120)
    d = [x for x in os88geom.windows(m) if x.i == disk][0]
    NX, NY = d.x + 40, d.y + 60
    LAND = (NX, NY, NX + d.w - 1, NY + d.h - 1)

    c = dispcells.Cells(m, mo, land=LAND)
    HAVE_CULL = c.cull_sym is not None
    c.drag(d.x + 60, d.y + 9, d.x + 100, d.y + 69)
    c.pump(500, 4)
    runs, fills, cells, inside = c.take()
    c.close()
    now = [x for x in os88geom.windows(m) if x.i == disk][0]
    print("CULLED  : %d runs, %d cells painted, %d of them inside the rect "
          "the mover was about to occupy %r" % (runs, cells, inside, LAND))
    if not HAVE_CULL:
        fails.append("SETUP: this kernel has no wm_dmg_cull - SPEC.md 11.3.3 "
                     "is not built, so there is nothing to gate")
    if abs(now.x - NX) > 8 or abs(now.y - NY) > 8:
        fails.append("SETUP: the drag did not land - the cover is at %r and "
                     "%r was wanted" % ((now.x, now.y), (NX, NY)))
    if runs < 40:
        fails.append("SETUP: only %d runs - the Task Manager did not repaint "
                     "at the drop, so nothing was culled or not culled"
                     % runs)
    if inside > 100:
        fails.append("CULLED: %d cells landed under the mover, against the 26 "
                     "this measured when 11.3.3 went in and the 452 before it "
                     "- the cull is not confining W_PAINT" % inside)
    print("DREW    : %d cells outside it, which is the part that stays visible"
          % (cells - inside))
    if cells - inside <= 0:
        fails.append("DREW: it painted NOTHING outside the mover's rect - the "
                     "cull is refusing everything, and CULLED is passed by a "
                     "window that draws nothing at all")

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  it does not paint where the mover is about to land")

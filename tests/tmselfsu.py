#!/usr/bin/env python3
"""The Task Manager does not repaint for ITS OWN raise cache (SPEC.md 28.8.1)

    make && python3 tests/tmselfsu.py [--machine os8088_xt_vga]

28.8's loop, at the one place 28.11 could not reach. Taking a cache is a heap
event; these two pages report heap events; 11.96.18 drops the cache of a
PARTIALLY covered window the moment it paints. So: the cache appears, the page
has changed, the page paints, the cache is gone - and it is never used.

Measured before the fix, with the memory page open: wm_su_segs empty either
side of a drag, wm_su_drop firing for its slot every interval, and 527 cells -
about 505 ms on the target - to come back from a reveal.

  OWNSEG   11.96.3.1's cell answers: our own window's slot names OUR segment,
           a kernel window's names KERNEL_SEG, and an empty slot refuses. The
           exclusion is built on this, so it is asserted before it is used
  QUIET    drag a cover PARTIALLY over the memory page and let it settle: the
           Task Manager must still be HOLDING a cache afterwards. Before, its
           own next interval saw the claim, painted, and dropped it
  CELLS    ...and it put no cells of its own on the glass for it
  OTHERS   ...but somebody ELSE's cache is NOT masked: drag a THIRD window
           over a second one - neither of them this one - so it banks a cache
           while the memory page sits untouched and fully visible, and the
           page must still see that as a change. A range mask passes QUIET
           and CELLS and fails here, which is the whole of why the cut is the
           self-reference and not the range

Cells, not calls: a culled cell is still a call and a cut chunk retries.
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
KSEG = os88geom.KERNEL_SEG
TITLE_H = 18
MAX_WIN = 12
fails = []


def tick(mm, card=None):
    mm.advance(frames=110)
    mm.run()


class Own(dispcells.Cells):
    """Cells, but only the ones OUR package drew, and wm_su_* beside them."""

    def __init__(self, mm, mmo):
        self.occl = self.took = self.dropped = 0
        dispcells.Cells.__init__(self, mm, mmo)
        for nm in ("wm_su_occl", "wm_su_take", "wm_su_drop"):
            self.at[mm.sym(nm)] = nm
        mm.bp_exec("font_run_x", "gfx_fill", "wm_su_occl", "wm_su_take",
                   "wm_su_drop")
        mm.run()

    def on_stop(self, r, sym):
        if sym == "wm_su_occl":
            self.occl += 1
        elif sym == "wm_su_take":
            self.took += 1
        elif sym == "wm_su_drop":
            self.dropped += 1
        else:
            dispcells.Cells.on_stop(self, r, sym)


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    os88marty.no_saver(m)

    def su(slot):
        b = m.read(S("wm_su_segs") + slot * 2, 2)
        return b[0] | (b[1] << 8)

    def W(i):
        return [x for x in os88geom.windows(m) if x.i == i][0]

    def drag(x0, y0, x1, y1):
        mo.to(x0, y0); mo._edge(True); mo.to(x1, y1, l=True); mo._edge(False)
        mo.to(*dispcorner.PARK); os88marty.settle(m, limit=120)

    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    dx, dy, _, _ = dispcp.win_rect(m, S, disk)
    dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "APPS")
    dispcp.open_named(m, mo, S, tick, dx, dy, "CALC.O88")
    for _ in range(4):
        tick(m)
    calc = [x.i for x in os88geom.windows(m) if x.i != disk][-1]
    c = [x for x in os88geom.windows(m) if x.i == calc][0]
    mo.to(c.x + 30, c.y + 9); mo._edge(True)        # ...out of the Disk
    mo.to(400 + 30, 320 + 9, l=True); mo._edge(False)   # window's list, or
    mo.to(*dispcorner.PARK)                             # the clicks below
    os88marty.settle(m, limit=120)                      # land on IT
    d0 = [x for x in os88geom.windows(m) if x.i == disk][0]
    mo.click(d0.x + 30, d0.y + 9)
    mo.to(*dispcorner.PARK)
    os88marty.settle(m, limit=120)
    dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "..")
    dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "SYSTEM")
    dispcp.open_named(m, mo, S, tick, dx, dy, "TASKMGR.O88")
    for _ in range(4):
        tick(m)
    got, i = None, 0
    while dispapps.pkg_seg(m, i) is not None:
        got, i = dispapps.pkg_seg(m, i), i + 1
    if got is None:
        sys.exit("the Task Manager did not open - %r" % os88geom.windows(m))
    slot, seg = got

    def view():
        return m.read((seg << 4) + dispapps.img_size("taskmgr")
                      + dispapps.bss_off("taskmgr", "tm_view"), 1)[0]

    w = W(slot)
    for _ in range(4):
        if view() == 1:
            break
        mo.click(w.x + 20, w.y + TITLE_H + 40)
        mo.to(*dispcorner.PARK)
        tick(m)
    if view() != 1:
        sys.exit("could not reach the memory page (view %d)" % view())

    # --- OWNSEG ------------------------------------------------------------
    # The cell is 11.96.3.1's and the exclusion is built on it, so read it
    # back through the kernel's own table rather than trusting the routine.
    def wseg(i):
        r = m.read(S("wm_wins") + i * os88geom.WIN_SIZE, os88geom.WIN_SIZE)
        used = (r[0] | (r[1] << 8)) & 1
        sg = r[22] | (r[23] << 8)
        return used, (sg or KSEG)

    used, ours = wseg(slot)
    dused, dseg = wseg(disk)
    free = [i for i in range(MAX_WIN) if not wseg(i)[0]]
    ok = (used and ours == seg and dused and dseg == KSEG and free)
    print("OWNSEG  : slot %d -> %04X (our package %04X), Disk slot %d -> %04X "
          "(KERNEL_SEG %04X), %d free slot(s)"
          % (slot, ours, seg, disk, dseg, KSEG, len(free)))
    if not ok:
        fails.append("OWNSEG: the window table does not carry what 11.96.3.1 "
                     "publishes - our slot must name our segment and a kernel "
                     "window's must name KERNEL_SEG")

    # Park all three clear of each other: memory page top right, Disk bottom
    # left, Calculator top left - the OTHERS leg needs two windows that are
    # not this one, or the page moves and proves nothing.
    c = W(calc);  drag(c.x + 30, c.y + 9, 30 + 30, 30 + 9)
    w = W(slot);  drag(w.x + 30, w.y + 9, 380 + 30, 40 + 9)
    d = W(disk);  drag(d.x + 30, d.y + 9, 8 + 30, 250 + 9)
    for _ in range(6):
        tick(m)
    os88marty.settle(m, limit=120)

    # --- QUIET + CELLS -----------------------------------------------------
    # A drag that covers the memory page PARTIALLY: 11.96.16.2 banks it, and
    # the question is whether its own next interval bins it again.
    # UP TO THREE DROPS, and the retry is the accepted drag race rather than
    # slack: the dragged window's own drag cache is SOMEBODY ELSE's claim
    # appearing, which 28.8.1 deliberately still repaints for - and that
    # repaint takes our cache with it (11.96.18). It fires only when
    # tm_quiet's unlocked sample lands inside the drop's lock hold, so a
    # second drop settles it. What is NOT retried is CELLS below.
    held = 0
    for attempt in range(3):
        w, d = W(slot), W(disk)
        p = Own(m, mo)
        if attempt:
            p.drag(d.x + 30, d.y + 9, 8 + 30, 250 + 9)      # off again first
            d = W(disk)
        p.drag(d.x + 30, d.y + 9, w.x + w.w // 2 + 30, w.y + 60 + 9)
        p.pump(900, 4)
        p.close()
        os88marty.settle(m, limit=120)
        held = su(slot)
        if held:
            break
    d, w = W(disk), W(slot)
    whole = (d.x <= w.x and d.y <= w.y
             and d.x + d.w >= w.x + w.w and d.y + d.h >= w.y + w.h)
    print("QUIET   : dropped over it partially (wholly=%s) after %d drop(s) "
          "- TM holds %04X" % (whole, attempt + 1, held))
    if whole:
        fails.append("QUIET: the cover is TOTAL, which is 11.96.18's other "
                     "case - the cache stands there whatever tm_quiet does, "
                     "so this proves nothing")
    if not held:
        fails.append("QUIET: the Task Manager holds no cache after the drop, "
                     "so its own interval saw the claim and dropped it - "
                     "28.8.1's loop, still running")

    # ...and let three more of its intervals go by. tm_quiet runs on each.
    q = Own(m, mo)
    q.pump(700, 8)
    q.close()
    print("CELLS   : %d cells over the intervals after the drop, %d drop(s), "
          "TM still holds %04X" % (q.cells, q.dropped, su(slot)))
    if q.cells:
        fails.append("CELLS: the Task Manager painted %d cell(s) with nothing "
                     "on the machine changing but its own cache" % q.cells)
    if not su(slot):
        fails.append("CELLS: it lost the cache over the following intervals")

    # --- OTHERS ------------------------------------------------------------
    # Somebody else's cache must STILL be news. Put the cover back where it
    # came from, then drag the CALCULATOR over the Disk window - neither of
    # them is this window, which sits untouched and fully visible throughout.
    drag(d.x + 30, d.y + 9, 8 + 30, 250 + 9)
    for _ in range(8):
        tick(m)
    os88marty.settle(m, limit=120)
    d, c = W(disk), W(calc)
    o = Own(m, mo)
    o.drag(c.x + 30, c.y + 9, d.x + 40 + 30, d.y + 20 + 9)
    o.pump(900, 6)
    o.close()
    os88marty.settle(m, limit=120)
    print("OTHERS  : Calculator dragged over the Disk window - Disk holds "
          "%04X, TM holds %04X, %d cells + %d fills"
          % (su(disk), su(slot), o.cells, o.fills))
    if not su(disk):
        fails.append("OTHERS: the Disk window banked nothing, so this leg "
                     "cannot say whether the page reacts to it")
    elif not (o.cells or o.fills):
        fails.append("OTHERS: the page drew nothing for ANOTHER window's "
                     "cache - that is a mask on the range, and 28.8.1 says "
                     "why it is the wrong cut")

    print()
    for f in fails:
        print("FAIL  " + f)
    if not fails:
        print("PASS  it ignores its own cache and still sees everyone else's")
    sys.exit(1 if fails else 0)

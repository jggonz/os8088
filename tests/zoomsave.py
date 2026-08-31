#!/usr/bin/env python3
"""A window ZOOMED or DRAGGED over another banks it (SPEC.md 11.96.16.2)

    make && python3 tests/zoomsave.py [--machine os8088_xt_vga]

11.96.16's pass banks the windows an incoming window is about to land on, and
it had exactly one caller: wm_show_b, a window that APPEARS. So a window
covered by the front one being zoomed over it was banked by nothing at all,
and the raise back was always a full repaint - on the most natural way there
is to cover a window completely. Measured before: `wm_su_take` 0 times for the
covered window across a maximize.

  BANK      zoom a window over the Task Manager, and wm_su_take fires for the
            TASK MANAGER - not just for the window doing the zooming.
  RESTORE   un-zoom, and wm_su_occl fires for it: the cache was still there
            and the reveal was answered from it. That call sits in wm_su_try
            past every refusal in wm_su_ck, so reaching it IS the restore.
  SAME      ...and the glass equals a forced full repaint, which is what stops
            RESTORE being a cache full of the coverer's own pixels. Getting
            11.96.16.2's two rects the wrong way round banks exactly that, and
            it is the failure the ordering exists to prevent.
  DRAGGED   ...and the OTHER caller: wm_rz_paint is the funnel for every
            GEOMETRY change and a drag is not one, so ui_drag has its own
            hook.

BOTH RETRY THE GESTURE, up to three times, and that is not slack: the
breakpoint pump loses hits. Measured directly - wm_su_occl reported without
wm_su_try, and wm_su_occl is reachable through no other path - so a run can
observe nothing where the kernel did everything right. Repeating the same
gesture is a fair way to ask again, and the leg still fails if the event
never happens at all. The pump is what wants fixing; until it is, this is
what keeps the gate honest instead of noisy (it failed 2 runs in 3 before,
on the build BEFORE 28.8.1 as well).

BOTH ASK WHETHER wm_su_precover RAN, not whether it TOOK. wm_su_bank banks at
the end of every wm_draw_win and SPEC.md 28.8.1 stopped the Task Manager
binning that on its next interval, so the covered window normally HAS a valid
cache when the zoom or the drop arrives - and wm_su_precover's
`wm_su_ck / jnc .have` then correctly does nothing. Counting takes reads that
as the hook being broken. The other state cannot be arranged from outside
either: every click that would make it drop the cache goes through
wm_draw_win, which banks it again on the way out. Reaching the call is the
whole of what 11.96.16.2 adds; take the hook away and it never happens.

THE TASK MANAGER IS THE SUBJECT because it is the window that promises
(28.11) and the one the report came from. The cover is the drive window, which
makes no promise of its own - so every wm_su_take counted here is about the
covered window and there is nothing to filter out.
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
WF_SAVEU = 0x20
fails = []


def tick(mm, card=None):
    mm.advance(frames=110)
    mm.run()


def view(m, seg):
    return m.read((seg << 4) + dispapps.img_size("taskmgr")
                  + dispapps.bss_off("taskmgr", "tm_view"), 1)[0]


def drag(mo, x0, y0, x1, y1):
    """Press, move, release - os88mouse has click and dblclick and no drag."""
    mo.to(x0, y0)
    mo._edge(True)
    mo.to(x1, y1, l=True)
    mo._edge(False)


def zord(m):
    n = m.read(S("wm_zn"), 1)[0]
    return list(m.read(S("wm_zord"), max(n, 1)))[:n]


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
    if got is None:
        sys.exit("the Task Manager did not open - %r" % os88geom.windows(m))
    slot, seg = got
    w = [x for x in os88geom.windows(m) if x.i == slot][0]
    if "Task" not in w.title:
        sys.exit("the newest package window is %r" % w.title)
    ours = S("wm_wins") - (KSEG << 4) + slot * os88geom.WIN_SIZE
    for _ in range(4):                          # the MEMORY page (SPEC.md 28)
        if view(m, seg) == 1:
            break
        mo.click(w.x + 20, w.y + TITLE_H + 40)
        mo.to(*dispcorner.PARK)
        tick(m)
    if view(m, seg) != 1:
        sys.exit("could not reach the memory page (view %d)" % view(m, seg))

    # THE COVER HAS TO BE CLEAR OF IT BEFORE THE ZOOM, or wm_obscured refuses
    # the bank and is right to: a window already sitting on the Task Manager
    # would be banked as the Task Manager's own content. At the sizes these
    # two open at they overlap, and neither fits beside the other alone, so
    # both move - the list to the right edge and the drive window to the left.
    drag(mo, w.x + w.w - 24, w.y + 9, 620, w.y + 9)
    drag(mo, dx + 30, dy + 9, 30, dy + 9)
    mo.to(*dispcorner.PARK)
    os88marty.settle(m, limit=120)
    w = [x for x in os88geom.windows(m) if x.i == slot][0]
    d = [x for x in os88geom.windows(m) if x.i == disk][0]
    clear = (d.x + d.w <= w.x or w.x + w.w <= d.x
             or d.y + d.h <= w.y or w.y + w.h <= d.y)

    # ...and the cover in FRONT, with the cache wm_front takes on the way past
    # SPENT: its worker drops one every interval it draws (11.96.18), and a
    # cache still standing here would answer RESTORE without the zoom having
    # banked anything at all.
    mo.click(d.x + 30, d.y + 9)
    mo.to(*dispcorner.PARK)
    os88marty.until(m, lambda mm: zord(mm)[-1] == disk,
                    "the drive window to come to the front", limit=90)
    for _ in range(4):
        tick(m)
    print("SETUP   : %r behind, %r in front, clear=%s"
          % ((w.x, w.y, w.w, w.h), (d.x, d.y, d.w, d.h), clear))
    if not clear:
        fails.append("SETUP: the two windows still overlap, so wm_obscured "
                     "refuses the bank and BANK cannot pass however 11.96.16.2 "
                     "behaves")

    # WHAT THESE TWO LEGS ASSERT, AND WHY IT IS NOT `A TAKE FIRED`.
    # wm_su_bank takes a cache at the end of every wm_draw_win, and since
    # SPEC.md 28.8.1 the Task Manager keeps one instead of binning it on its
    # next interval - so the covered window usually HAS a valid cache when the
    # zoom or the drop arrives, and wm_su_precover's `wm_su_ck / jnc .have`
    # then correctly does nothing: re-taking it would be a blit for a picture
    # already held. Counting takes reads that as the hook being broken.
    #
    # There is no way to arrange the other state from outside either: every
    # click that would make it drop the cache goes through wm_draw_win, which
    # banks it again on the way out.
    #
    # So what is asserted is that the operation REACHES wm_su_precover for our
    # window - which is the whole of what 11.96.16.2 adds, and which
    # wm_rz_paint cannot do for a drag - and that the window holds a cache
    # afterwards. Take the hook out and the call never happens.
    def held(i):
        b = m.read(S("wm_su_segs") + i * 2, 2)
        return b[0] | (b[1] << 8)

    # --- BANK --------------------------------------------------------------
    b = dispcells.Bp(m, mo, {"wm_su_take": "di", "wm_su_occl": "di",
                             "wm_su_precover": "di"}, ours)
    b.dblclick(d.x + 60, d.y + 9)               # zoom it over everything
    b.pump(900, 4)
    n = b.take()
    b.close()
    mo.to(*dispcorner.PARK)
    os88marty.settle(m, limit=120)
    big = [x for x in os88geom.windows(m) if x.i == disk][0]
    whole = (big.x <= w.x and big.y <= w.y
             and big.x + big.w >= w.x + w.w and big.y + big.h >= w.y + w.h)
    print("BANK    : zoomed to %r, wholly=%s, TM holds %04X - %r"
          % ((big.x, big.y, big.w, big.h), whole, held(slot), n))
    if not whole:
        fails.append("SETUP: the zoom does not wholly cover the Task Manager, "
                     "so there is nothing here for a cache to answer")
    pre = n.get("wm_su_precover/us", 0) + n.get("wm_su_precover/other", 0)
    if not pre:
        fails.append("BANK: wm_su_precover never ran - the zoom asked nobody "
                     "whether it was about to cover them, which is what "
                     "SPEC.md 11.96.16.2 exists to fix")
    if not held(slot):
        fails.append("BANK: the Task Manager holds no cache after the zoom, "
                     "so the reveal below has nothing to come off")

    # --- RESTORE -----------------------------------------------------------
    was = held(slot)                            # ...and what it is holding as
    b = dispcells.Bp(m, mo, {"wm_su_take": "di", "wm_su_occl": "di",
                             "wm_su_try": "di", "wm_su_ck": "di"}, ours)
    n = {}
    # TWO WAYS THIS LEG USED TO LOSE THE ANSWER, and neither was the kernel:
    # it failed 2 runs in 3 on the build BEFORE 28.8.1 too.
    #
    # A DOUBLE-CLICK CAN MISS with breakpoints armed - the pump services
    # between the four edges and the two presses can fall outside the window,
    # so nothing un-zooms, nothing is revealed, and wm_su_occl cannot fire
    # whatever the cache did. Hence the retry on the RECT.
    #
    # And THE REVEAL CAN LAND AFTER A FIXED PUMP. Un-zooming repaints most of
    # the screen with five breakpoints' worth of servicing in the way, so a
    # budget that is enough on a quiet machine is not enough on a busy one -
    # and the restore then happens after b.close(), unarmed and uncounted.
    # So the pump runs until the answer arrives rather than for a fixed count.
    for _ in range(3):
        b.dblclick(big.x + 60, big.y + 9)       # ...down
        for _ in range(8):
            b.pump(600, 4)
            if b.n.get("wm_su_occl/us"):
                break
        n = b.take()
        if n.get("wm_su_occl/us"):
            break
        cur = [x for x in os88geom.windows(m) if x.i == disk][0]
        if (cur.x, cur.y, cur.w, cur.h) != (big.x, big.y, big.w, big.h):
            b.dblclick(cur.x + 60, cur.y + 9)   # it DID un-zoom and we saw
            b.pump(900, 4)                      # nothing: zoom it again and
            b.take()                            # ask the same question again
            for _ in range(4):
                tick(m)
            big = [x for x in os88geom.windows(m) if x.i == disk][0]
    b.close()
    mo.to(*dispcorner.PARK)
    os88marty.settle(m, limit=120)
    cw, _, after = m.fbuf()
    fin = [x for x in os88geom.windows(m) if x.i == disk][0]
    print("RESTORE : un-zoomed to %r, TM held %04X going in - %r"
          % ((fin.x, fin.y, fin.w, fin.h), was, n))
    if (fin.x, fin.y, fin.w, fin.h) == (big.x, big.y, big.w, big.h):
        fails.append("RESTORE: the window never un-zoomed, so nothing was "
                     "revealed - the double-click did not land")
    if not n.get("wm_su_occl/us"):
        fails.append("RESTORE: wm_su_occl never fired for the Task Manager - "
                     "the cache the zoom took did not survive to answer the "
                     "reveal, so nothing was saved by taking it")

    # --- SAME --------------------------------------------------------------
    saved = []
    for x in dispcp.win_list(m, S):
        a = S("wm_wins") + x * os88geom.WIN_SIZE + os88geom.W_FLAGS
        f = m.read(a, 2)
        saved.append((a, f))
        m.write(a, bytes([f[0] & ~WF_SAVEU & 0xFF, f[1]]))
    m.write(S("cp_dirty"), b"\x01")
    os88marty.settle(m, limit=120)
    for a, f in saved:
        m.write(a, f)
    os88marty.settle(m, limit=120)
    _, _, honest = m.fbuf()

    def rect(fb):
        return b"".join(fb[(y * cw + w.x) * 3:(y * cw + w.x + w.w) * 3]
                        for y in range(w.y, w.y + w.h))

    diff = sum(1 for a, b2 in zip(rect(after), rect(honest)) if a != b2)
    print("SAME    : %d subpixels differ from a forced full repaint" % diff)
    if diff:
        fails.append("SAME: %d subpixels differ - what came back off the cache "
                     "is not this window's own picture (SPEC.md 11.96.16.2 on "
                     "getting the two rects the wrong way round)" % diff)

    # --- DRAGGED -----------------------------------------------------------
    w = [x for x in os88geom.windows(m) if x.i == slot][0]
    d = [x for x in os88geom.windows(m) if x.i == disk][0]
    for _ in range(4):                          # the cache wm_front took, spent
        tick(m)
    c = dispcells.Bp(m, mo, {"wm_su_precover": "di"}, ours)
    n2, here = {}, (d.x, d.y)
    for _ in range(3):                          # ...and back and forth, for
        c.drag(here[0] + 60, here[1] + 9, w.x + 40, w.y + 40)   # the reason
        c.pump(900, 4)                          # RESTORE retries above
        n2 = c.take()
        if n2.get("wm_su_precover/us") or n2.get("wm_su_precover/other"):
            break
        cur = [x for x in os88geom.windows(m) if x.i == disk][0]
        c.drag(cur.x + 60, cur.y + 9, 7 + 60, 80 + 9)
        c.pump(600, 4)
        c.take()
        here = (7, 80)
    c.close()
    mo.to(*dispcorner.PARK)
    os88marty.settle(m, limit=120)
    now = [x for x in os88geom.windows(m) if x.i == disk][0]
    over = not (now.x + now.w <= w.x or w.x + w.w <= now.x
                or now.y + now.h <= w.y or w.y + w.h <= now.y)
    pre2 = n2.get("wm_su_precover/us", 0) + n2.get("wm_su_precover/other", 0)
    print("DRAGGED : dropped at %r over it=%s, TM holds %04X - %d "
          "wm_su_precover" % ((now.x, now.y), over, held(slot), pre2))
    if not over:
        fails.append("SETUP: the drop does not land on the Task Manager, so "
                     "there is nothing for the drag hook to bank")
    if not pre2:
        fails.append("DRAGGED: the drop never reached wm_su_precover - "
                     "wm_rz_paint is the funnel for every GEOMETRY change and "
                     "a drag is not one, so without ui_drag's own hook "
                     "(SPEC.md 11.96.16.2) nothing asks at all")
    # NOT `and it holds one afterwards`: the drop leaves the DRAGGED window
    # holding a drag cache it promised for (wm_dc_done keeps it), which is a
    # claim appearing - somebody ELSE's, so SPEC.md 28.8.1 has the Task
    # Manager paint for it, and 11.96.18 drops its own under that paint. The
    # bank is still right and the next reveal is still cheaper; what is not
    # assertable here is the state a settle later.

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  a zoom banks what it covers, and the reveal comes off the cache")

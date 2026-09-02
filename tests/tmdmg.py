#!/usr/bin/env python3
"""A partial repaint LETTERS only the part (SPEC.md 28.10.2)

    make && python3 tests/tmdmg.py [--machine os8088_xt_vga]

SPEC.md 28.10 made the Task Manager fill only the part of its content
`wm_damage` named. The DRAWING was still whole, and 28.10.2 recorded that as
unfinished: a window moved off this one repainted every row of a 19-row list
to repair a strip. Measured A/B on the memory page, counting the CELLS this
window puts on the glass: **225 before the bands, 0 after**, against 549 for a
forced whole repaint of the same page. (Zero is legitimate here and SAME is
what keeps it honest: the mover covers MORE of this window than it did, so
nothing of it was uncovered and nothing is owed.)

The structure to fix it was already there. 28.2 made a row FIVE independently
checked chunks and every one comes through `tm_elchk`, so the rule the chunk
loop wants is one clause wider: **draw this chunk if its key changed, OR if
the damage took its pixels.** `[tm_dmg]` is that rect and `tm_dmg_hit` is the
clause.

IT COUNTS CELLS PUT ON THE GLASS (`tests/dispcells.py`), and pixels could not
answer the question at all: a redraw of the same characters changes nothing on
the glass, `m.flicker` sees a still screen either way, and that is exactly how
this cost went unnoticed. Nor could a CALL count, since SPEC.md 11.3.3's cull
landed - a culled cell is still a call, and a chunk the region cut retries
(28.2), so calls go up while pixels go down. This gate counted calls for one
day and 11.3.3 broke it the same evening.

  STRIP     move a window off part of it and count the calls the repaint
            issues: `font_run` for the rows and the captions, `gfx_fill` for
            the maps and the bars.
  WHOLE     then force a full repaint and count those. STRIP must be a
            fraction of it, on BOTH counts.
  SAME      ...and the glass must be identical afterwards. That is what stops
            STRIP being won by drawing nothing.

THREE THINGS THIS GATE HAS TO GET RIGHT, each of which cost a run:

* **The MEMORY page, not the heap page.** The heap page's rows ARE the claim
  table, so anything that opens, closes or covers a window changes most of
  them on the data alone and the band can save nothing. Measured: closing the
  cover over the heap page drew 96 chunks of 100. The memory page's rows are
  instances, which a window MOVING does not touch.
* **A DRAG, not a close.** Closing the cover ends an instance, which moves
  the memory page's rows for the same reason. `ui_drag` moves the window once,
  at the drop, so a drag is a single damage event. What that event hands this
  window is the UNION of the cover's old and new rects clipped to the content
  - so the saving here is the Y half of the band, the rows below the cover;
  the X half pays where the damage really is a vertical strip, which is a
  window closing or minimizing off one side.
* **`font_run_x` reached through `api_font_run`.** API slot 0x0258 is an X
  STUB straight to `font_run_x`, so a breakpoint on the kernel's own
  `font_run` counts 2 calls for a whole repaint of this window; and the
  return address is what separates OUR calls from the Finder window redrawing
  its own file names in the same pass.

THE CACHE IS SPOILED FIRST, deliberately. Since 28.11 an uncover can be
answered by the raise cache - `tm_update` and no ground at all - which would
win STRIP without the band being consulted once. `WF_SAVEU` cleared in the
record while the window is covered is `wm_su_ck`'s first refusal and frees
nothing.
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
TITLE_H = 18
WF_SAVEU = 0x20
STEP = 8
SHIFT = 60                              # px to drag the cover to the right
fails = []


def tick(mm, card=None):
    mm.advance(frames=110)
    mm.run()


def view(m, seg):
    return m.read((seg << 4) + dispapps.img_size("taskmgr")
                  + dispapps.bss_off("taskmgr", "tm_view"), 1)[0]


def zord(m):
    n = m.read(S("wm_zn"), 1)[0]
    return list(m.read(S("wm_zord"), max(n, 1)))[:n]


def spoil(m, slot):
    """Clear WF_SAVEU in the record: wm_su_ck asks wm_dc_ok first, so the
    restore misses, and a poke is not wm_saveu so no claim is freed and no
    figure this page draws moves (SPEC.md 28.11.2)."""
    a = S("wm_wins") + slot * os88geom.WIN_SIZE + os88geom.W_FLAGS
    f = m.read(a, 2)
    m.write(a, bytes([f[0] & ~WF_SAVEU & 0xFF, f[1]]))


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

    for _ in range(4):                          # the MEMORY page (SPEC.md 28)
        if view(m, seg) == 1:
            break
        mo.click(w.x + 20, w.y + TITLE_H + 40)
        mo.to(*dispcorner.PARK)
        tick(m)
    if view(m, seg) != 1:
        sys.exit("could not reach the memory page (view %d)" % view(m, seg))
    x1, y1, x2, y2 = w.content
    box = (x2 - x1 + 1) * (y2 - y1 + 1)

    dwin = [x for x in os88geom.windows(m) if x.i == disk][0]
    mo.click(dwin.x + 30, dwin.y + 9)
    mo.to(*dispcorner.PARK)
    os88marty.until(m, lambda mm: zord(mm)[-1] == disk,
                    "the drive window to cover it", limit=90)
    os88marty.settle(m, limit=120)
    ox1, oy1 = max(x1, dwin.x), max(y1, dwin.y)
    ox2, oy2 = min(x2, dwin.x + dwin.w - 1), min(y2, dwin.y + dwin.h - 1)
    if ox2 <= ox1 or oy2 <= oy1:
        sys.exit("the drive window does not cover the Task Manager at all")
    spoil(m, slot)

    # --- STRIP -------------------------------------------------------------
    c = dispcells.Cells(m, mo)
    ok = c.drag(dwin.x + 30, dwin.y + 9, dwin.x + 30 + SHIFT, dwin.y + 9)
    c.pump()
    r_strip, f_strip, n_strip, _ = c.take()
    c.close()
    moved = [x for x in os88geom.windows(m) if x.i == disk][0]
    tick(m)
    cw, _, after = m.fbuf()
    print("STRIP   : cover %dx%d over rows %d..%d of %d..%d - %d cells "
          "painted (%d runs, %d fills)"
          % (dwin.w, dwin.h, oy1, oy2, y1, y2, n_strip, r_strip, f_strip))
    # NOT an exact landing: SPEC.md 11.95.2's x snap puts a window's content
    # on a byte boundary, so a 60px drag lands 56 across.
    if not ok or abs(moved.x - dwin.x - SHIFT) > 8:
        fails.append("SETUP: the drag did not land - the cover moved %d px, "
                     "about %d was wanted" % (moved.x - dwin.x, SHIFT))

    # --- WHOLE -------------------------------------------------------------
    # No mouse in this one: [cp_dirty] is the one flag whose only consumer is
    # wm_paint_all, and WF_SAVEU is cleared so a cache cannot answer it.
    saved = []
    for x in dispcp.win_list(m, S):
        a = S("wm_wins") + x * os88geom.WIN_SIZE + os88geom.W_FLAGS
        f = m.read(a, 2)
        saved.append((a, f))
        m.write(a, bytes([f[0] & ~WF_SAVEU & 0xFF, f[1]]))
    m.write(S("cp_dirty"), b"\x01")
    c = dispcells.Cells(m, mo)
    c.pump()
    r_whole, f_whole, n_whole, _ = c.take()
    c.close()
    for a, f in saved:
        m.write(a, f)
    os88marty.settle(m, limit=120)
    _, _, honest = m.fbuf()
    print("WHOLE   : %d cells painted for the same page (%d runs, %d fills)"
          % (n_whole, r_whole, f_whole))
    if n_whole < 400:
        fails.append("WHOLE: only %d cells for a full repaint - the reference "
                     "is wrong, so STRIP has nothing to be a fraction of"
                     % n_whole)
    elif n_strip >= n_whole * 3 // 4:
        fails.append("STRIP: %d cells painted against a whole repaint's %d - "
                     "the damage band is not being consulted, or it is forcing "
                     "what the damage never reached (SPEC.md 28.10.2)"
                     % (n_strip, n_whole))

    # --- SAME --------------------------------------------------------------
    def rect(fb):
        return b"".join(fb[(y * cw + w.x) * 3:(y * cw + w.x + w.w) * 3]
                        for y in range(w.y, w.y + w.h))

    diff = sum(1 for a, b in zip(rect(after), rect(honest)) if a != b)
    print("SAME    : %d subpixels differ from a forced full repaint" % diff)
    if diff:
        fails.append("SAME: %d subpixels differ - the band skipped a chunk "
                     "whose pixels the damage had taken" % diff)

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  it letters the strip, and only the strip")

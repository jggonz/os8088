#!/usr/bin/env python3
"""The history graph is DAMAGE-GATED and RUN-CODED (SPEC.md 28.10.3)

    make && python3 tests/tmgraph.py [--machine os8088_5150_herc_gla]

`tm_draw_perf` used to walk all `TM_GW` columns on every paint whatever the
damage said, at up to two primitive calls each - 432 of them for a 216x40 box.
Uncovering the RAM bar UNDERNEATH the graph therefore redrew the whole graph,
which is 333.9 ms of a Hercules bought for the ~16 ms of bar that was asked
for.  28.10.3 gives the graph the band test the rows have had since 28.10.2,
clamps it to the damaged COLUMNS, and coalesces neighbouring columns of equal
height into one fill.

`tm_grun` is the whole of the graph's drawing - `tm_col` is a one-column call
to it and `tm_draw_perf` reaches it through `tm_graph` - so COUNTING IT is the
assertion in three of the four legs.  The sweep gap (`tm_gapcol`) is the one
shape that does not come through it, and it is one column.

  BAR       cover only the rows BELOW the graph - the RAM bar and the list -
            and uncover them.  The graph must draw NOTHING: 0 runs.  This is
            the reported gesture, and it was 216 columns before 28.10.3.
  CLAMP     then poke the ring to a COMB - no two neighbours equal, so
            nothing can coalesce and a run IS a column - cover the graph's
            left-hand columns only, and drag the cover straight DOWN.  The
            count is then the number of columns the walk touched, and it must
            not exceed the ones the cover's x range spans.
            TWO THINGS THIS LEG HAD TO BE TOLD, each of which cost a run and
            each of which made it green while testing something else
            (docs/WRITING-TESTS.md 13).  DOWN and not sideways, because
            `wm_paint_dmg` is handed the UNION of where the cover was and
            where it is (SPEC.md 11.91) - a sideways drag damages every column
            it crossed and there is no x half left to test.  And the COMB,
            because an idle machine's history coalesces the WHOLE graph to
            around 22 runs, which is comfortably under any bound a partial
            strip could set: with the clamp deleted the leg still passed.
  SWAP      cycle the view back round to this page.  `tm_draw_full` white-
            fills the content first, so the graph is owed ALL of it, and a
            count of 0 here is 28.10.3's own hazard: a band gate in front of
            a path whose damage rect nobody set.
  COALESCE  finally, poke the history ring to a CONSTANT and force a whole
            repaint: a flat graph is one run each side of the sweep gap, so
            the count is a handful and not TM_GW.  Poked rather than waited
            for, because a load history is not something a gate may assume.

IT ASSERTS CALLS AND NOT PIXELS, which is SPEC.md 28.11.2's own finding about
this window: every instrument that changes the machine changes the picture, so
a forced repaint cannot be a reference.  The performance page is the worst of
them - it advances a column and a percentage every `TM_INT` BY CONSTRUCTION -
so two captures of it never agree and `settle` never returns at all.  CLAMP is
what keeps BAR from passing vacuously: the same gesture eight rows further up
must draw, or the gate is measuring a graph that stopped drawing entirely.

THE COVER IS PLACED BY ITS FRAME, not by its content: `wm_paint_dmg` is handed
the union of where the window WAS and where it IS (SPEC.md 11.91), and those
are frame rects.  A cover whose content clears the graph but whose title bar
does not is a BAR leg that legitimately draws columns.
"""
import os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp, dispcorner, dispapps

MACHINE = "os8088_5150_herc_gla"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
WF_SAVEU = 0x20
SHIFT = 80                              # px to drag the cover sideways
FLAT = 20                               # the height COALESCE fills the ring with
fails = []
tr_grun = [0, 0]                        # (tm_grun, tm_col), filled once the
                                        # package is loaded and its segment known
SRC = os.path.join(os.path.dirname(__file__), "..", "apps", "taskmgr",
                   "taskmgr.asm")


def equ(name):
    """A constant out of the package's own source, so it cannot drift."""
    m = re.search(r"^%s\s+equ\s+([0-9]+)\s*(;.*)?$" % name,
                  open(SRC).read(), re.M)
    if not m:
        sys.exit("tmgraph: taskmgr.asm has no plain `%s equ <n>`" % name)
    return int(m.group(1))


TM_GF_Y1, TM_GF_Y2 = equ("TM_GF_Y1"), equ("TM_GF_Y2")
TM_BAR_Y1, TM_RW = equ("TM_BAR_Y1"), equ("TM_RW")
TM_GW = TM_RW - 7                       # taskmgr.asm: TM_GW equ TM_RW - 7


TM_NVIEW = 3                            # taskmgr.asm: perf, memory, heap


def tick(mm, card=None):
    mm.advance(frames=110)
    mm.run()


def gwait(m, secs):
    """Let the guest run for `secs` of ITS OWN time.

    `settle` cannot be used on this page - it animates for ever - and `tick`
    cannot be used inside a bp_trace, because a stop landing in the middle of
    an `advance` ends it early and the pump resumes past the bound.  A poll on
    the guest's own cycle counter is neither.
    """
    t0 = m.status()["cycles"]
    while m.status()["cycles"] - t0 < secs * os88marty.GUEST_HZ:
        time.sleep(0.05)


def runs(tr):
    """The runs the GRAPH BODY issued: every tm_grun that was not tm_col's."""
    return sum(1 for h in tr.hits if h["addr"] == tr_grun[0]) - \
           sum(1 for h in tr.hits if h["addr"] == tr_grun[1])


def zord(m):
    n = m.read(S("wm_zn"), 1)[0]
    return list(m.read(S("wm_zord"), max(n, 1)))[:n]


def spoil_all(m):
    """Clear WF_SAVEU everywhere and return what to put back.  tmdmg.py's
    reason: a cache answering the repaint would win a leg without the band
    being consulted once, and a host poke is not `wm_saveu` so nothing is
    freed and no figure this page draws moves."""
    saved = []
    for x in dispcp.win_list(m, S):
        a = S("wm_wins") + x * os88geom.WIN_SIZE + os88geom.W_FLAGS
        f = m.read(a, 2)
        saved.append((a, f))
        m.write(a, bytes([f[0] & ~WF_SAVEU & 0xFF, f[1]]))
    return saved


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
    w = [q for q in os88geom.windows(m) if q.i == slot][0]
    if "Task" not in w.title:
        sys.exit("the newest package window is %r" % w.title)
    bss = (seg << 4) + dispapps.img_size("taskmgr")
    if m.read(bss + dispapps.bss_off("taskmgr", "tm_view"), 1)[0] != 0:
        sys.exit("this gate is about the PERFORMANCE page")

    tmap = dispapps._map("taskmgr")
    grun = (seg << 4) + tmap["tm_grun"]
    gcol = (seg << 4) + tmap["tm_col"]      # the WORKER's two columns an
                                            # interval come through here, and
                                            # they arrive whatever the damage
                                            # said - so the graph body's own
                                            # runs are grun MINUS col
    tr_grun[0], tr_grun[1] = grun, gcol

    # --- PUT THE WINDOW AT THE TOP OF THE SCREEN BEFORE MEASURING ANYTHING ---
    # BAR needs to place a 200px cover ENTIRELY below the graph, and the room
    # to do that is the screen's, not the window's - but where the graph sits
    # on the screen was a function of the window's HEIGHT, because `wm_fit`
    # pins this window's BOTTOM to the last row above the dock.  So a row
    # leaving the Task Manager's list left the graph lower, and the cover -
    # whose own bottom is clamped to that same row - could no longer get
    # under it.
    #
    # It did not fail cleanly either.  At the height this window had when the
    # leg was written the cover landed EXACTLY ONE ROW below the graph, which
    # is zero margin: the leg passed, and passed, and then leaked a single
    # graph run on a run that was no different (0 runs and 1 run over two
    # runs of the same build).  Then SPEC.md 20.9 retired the `Disk bufs` row,
    # TM_PREF_H fell by one TM_ROW_H, and one row of margin became minus one.
    #
    # Dragging to the top makes the margin the SCREEN's - 29 rows on a 348-row
    # Hercules - so neither this window's height nor its placement policy can
    # reach this gate again.
    mo.drag(w.x + 30, w.y + 9, w.x + 30, 12)    # 12 clamps to the WM's own
    mo.to(*dispcorner.PARK)                     # ceiling, whatever it is
    tick(m)
    w = [q for q in os88geom.windows(m) if q.i == slot][0]

    cx1, cy1, cx2, cy2 = w.content
    gy1, gy2 = cy1 + TM_GF_Y1, cy1 + TM_GF_Y2       # the graph band, absolute
    print("TASKMGR : %r content %d..%d x %d..%d; graph rows %d..%d, bar at %d"
          % (w.title, cx1, cx2, cy1, cy2, gy1, gy2, cy1 + TM_BAR_Y1))

    def place(x, y):
        c = [q for q in os88geom.windows(m) if q.i == disk][0]
        mo.drag(c.x + 30, c.y + 9, c.x + 30 + (x - c.x), c.y + 9 + (y - c.y))
        mo.to(*dispcorner.PARK)
        tick(m)
        return [q for q in os88geom.windows(m) if q.i == disk][0]

    def uncover(x, y, what, dx=SHIFT, dy=0, before=None):
        """Put the cover at (x,y), drag it off by (dx,dy), count the runs."""
        c = place(x, y)
        if c.x > cx2 or c.x + c.w - 1 < cx1 or c.y > cy2 or c.y + c.h - 1 < cy1:
            fails.append("SETUP(%s): the cover landed at (%d,%d) %dx%d and "
                         "does not overlap the Task Manager"
                         % (what, c.x, c.y, c.w, c.h))
            return None, c
        if zord(m)[-1] != disk:
            fails.append("SETUP(%s): the cover is not the front window" % what)
        if before is not None:
            before()
        with os88marty.bp_trace(m, grun, gcol, poll=0.002, cap=1200) as tr:
            mo.drag(c.x + 30, c.y + 9, c.x + 30 + dx, c.y + 9 + dy, settle=0)
            gwait(m, 2.0)                   # STAY IN THE BLOCK until the
        return runs(tr), c                  # repaint has actually run

    # --- BAR: uncover only what is BELOW the graph ---------------------------
    n_bar, c = uncover(cx1 - 20, gy2 + 1, "BAR")
    if n_bar is not None:
        print("BAR     : cover %dx%d at (%d,%d), top %d rows under the graph "
              "- %d graph runs" % (c.w, c.h, c.x, c.y, c.y - gy2, n_bar))
        if c.y <= gy2:
            fails.append("SETUP(BAR): the cover landed at y=%d, which is not "
                         "clear of the graph's last row %d" % (c.y, gy2))
        elif n_bar:
            fails.append("BAR: %d graph runs for damage that starts %d rows "
                         "BELOW the graph - the band gate is not being "
                         "consulted (SPEC.md 28.10.3)" % (n_bar, c.y - gy2))

    # --- CLAMP: uncover a NARROW x strip that does cross the graph -----------
    def comb():
        m.write(bss + dispapps.bss_off("taskmgr", "tm_hist"),
                bytes([FLAT, FLAT + 6] * (TM_GW // 2)))

    cw_ = [q for q in os88geom.windows(m) if q.i == disk][0].w
    n_run, c = uncover(max(0, cx1 + 80 - (cw_ - 1)), gy1 - 30, "CLAMP", 0, 60,
                       before=comb)
    if n_run is not None:
        gx1 = cx1 + 7                                   # the interior's first x
        cols = min(c.x + c.w - 1, cx1 + TM_RW - 1) - max(c.x, gx1) + 1 + 8
        print("CLAMP   : cover %dx%d at (%d,%d) dragged DOWN - %d runs over a "
              "COMB for the <=%d columns its x range spans, of %d"
              % (c.w, c.h, c.x, c.y, n_run, cols, TM_GW))
        if not n_run:
            fails.append("CLAMP: 0 graph runs for damage crossing rows %d..%d "
                         "- the gate is refusing work it owes" % (gy1, gy2))
        elif n_run > cols:
            fails.append("CLAMP: %d runs for damage whose x range spans at "
                         "most %d columns - the damaged x range is not "
                         "clamping the walk (SPEC.md 28.10.3)" % (n_run, cols))

    # --- SWAP: a view switch owes the graph everything ----------------------
    # tm_click cycles 0 -> 1 -> 2 -> 0 and tm_clear_content white-fills before
    # tm_draw_full, so arriving back here must redraw the whole graph.  This
    # is the leg that fails if tm_draw_full stops calling tm_dmg_all.
    m.write(bss + dispapps.bss_off("taskmgr", "tm_hist"),
            bytes([FLAT]) * TM_GW)      # ...and out of CLAMP's comb, or three
                                        # whole redraws are 215 runs apiece
    mo.click(w.x + 40, w.y + 9)         # RAISE it first: ui.inc feeds clicks
    mo.to(*dispcorner.PARK)             # to the front window only, so a
    tick(m)                             # raising click never reaches tm_click
    with os88marty.bp_trace(m, grun, gcol, poll=0.002, cap=1200) as tr:
        for _ in range(TM_NVIEW):
            mo.click(cx1 + 20, cy1 + 40)
            mo.to(*dispcorner.PARK)
            gwait(m, 2.0)
        n_swap = runs(tr)
    view = m.read(bss + dispapps.bss_off("taskmgr", "tm_view"), 1)[0]
    print("SWAP    : %d clicks back to view %d - %d graph runs on the way"
          % (TM_NVIEW, view, n_swap))
    if view != 0:
        fails.append("SETUP(SWAP): %d clicks left the page on view %d"
                     % (TM_NVIEW, view))
    elif not n_swap:
        fails.append("SWAP: 0 graph runs across a full cycle of the views - "
                     "tm_draw_full is not telling the band gate that it owes "
                     "everything (SPEC.md 28.10.3)")

    # --- COALESCE: a FLAT ring is one run each side of the sweep gap ---------
    saved = spoil_all(m)
    m.write(bss + dispapps.bss_off("taskmgr", "tm_hist"),
            bytes([FLAT]) * TM_GW)
    m.write(S("cp_dirty"), b"\x01")
    with os88marty.bp_trace(m, grun, gcol, poll=0.002, cap=1200) as tr:
        gwait(m, 3.0)
    n_flat = runs(tr)
    for a, f in saved:
        m.write(a, f)
    # One run each side of the gap is 2; the worker pushes a live sample every
    # TM_INT while this runs, and each one that differs from FLAT splits a run,
    # so the bar is a handful rather than exactly two.
    print("COALESCE: %d runs for a FLAT ring of %d columns" % (n_flat, TM_GW))
    if not n_flat:
        fails.append("COALESCE: 0 runs for a forced whole repaint - the graph "
                     "drew nothing at all")
    elif n_flat > 12:
        fails.append("COALESCE: %d runs for a ring that is one height all the "
                     "way across - neighbouring columns of equal height are "
                     "not being coalesced (SPEC.md 28.10.3)" % n_flat)

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  the graph draws what the damage took, and no more")

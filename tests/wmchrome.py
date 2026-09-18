#!/usr/bin/env python3
"""Chrome that is WHOLLY obstructed is not drawn (SPEC.md 11.97.3, 11.97.4).

    make marty && make
    python3 tests/wmchrome.py
    python3 tests/wmchrome.py --part shadow --machine os8088_5150_cga_gla

Reported from the field with a photograph: *"sometimes when resizing, dragging
and dropping or otherwise causing a window to show over another window, the
shadow chrome and sometimes title chrome is redrawn - and if the window on top
itself doesn't redraw, it sticks around. It was going to be fully obstructed
anyway."*  The reporter's own repro is part 1 here, near enough verbatim: a
Disk window, Paint over it, Paint's grow box dragged inward until it refuses.

PART 3 IS THE OTHER HALF OF THE SAME REPORT and asks a different KIND of
question. Parts 1 and 2 fence the pixels; part 3 asks why there was any work
to fence. A refused resize leaves all four of the window's words exactly as
they were, so nothing was uncovered and nothing new was drawn - and SPEC.md
11.91.5 is `ui_grow` returning without a damage pass at all. That cannot be
measured in pixels, because after parts 1 and 2 the glass is already right: it
is measured by ARMING `wm_paint_dmg` and asking whether it is entered with a
vacated rect standing (SPEC.md 11.91.2's `[wm_dmg_stwin]`), which only
`ui_drag` and `ui_grow` ever set.

THE MEASUREMENT IS tests/wmartifact.py's and so is the reason for it: neither
artifact is visible in a screenshot, because a screenshot has nothing to
disagree with. The glass is stale, not corrupt, and it stays stale until
something repaints it. So the question both parts ask is

    is what is on the glass RIGHT NOW what a full repaint would draw?

and the answer is a pixel count. Before SPEC.md 11.97.3 part 1 reads hundreds
of px and EVERY ONE of them is the Disk window's own drop-shadow L - 520 in the
reporter's own geometry, fewer when Paint's floor makes this cut the window
down to fit inside it. Before 11.97.4 part 2 reads 4,063 px of that window's
title strip standing on Paint's canvas.

WHY THE TRIGGER IS A REFUSED RESIZE AND NOT A DRAG. What makes the chrome
*stick* rather than flash is that the window on top does not paint over it:
`ui_grow` marks everything under the window's mousedown rect as damaged BEFORE
the negotiation, and a refusal then satisfies SPEC.md 11.90.3's pure-shrink
test - origin unmoved, neither axis wider - which sets `[wm_dmg_rzwin]` and
tells the app it owes its content NOTHING. A drag shows the same draw as a
flash and repairs it a frame later.

THREE THINGS THAT COST TIME HERE, all of them worth not re-deriving:

  * **`_ink` starts 64 px in, and that is not a margin.** Paint's TOOL PALETTE
    is the left ~48 px of its frame, so a stroke begun at `x + 20` presses a
    TOOL and lays no ink at all - after which Paint shrinks freely, the test's
    refusal never happens, and the run reads like the artifact being absent.
    That is the whole of why an early version of this file flaked, and the
    failure is silent at the point it happens.
  * **A drag must hold still before it lets go.** `ui_grow` samples
    `[mouse_x]` once a TICK and tests for the release FIRST, so a release in
    the same pass as the last move commits the size from the pass BEFORE it.
    Dragged without the pause, Paint came back at ~85% of its size per drag,
    three drags running - which reads exactly like a window with a minimum
    nobody can name.
  * **The CONTROL is reported and not asserted.** It is not always clean and
    what is in it is not this row's: on the kernel BEFORE either fix it reads
    15 px of a vacated shadow row, or 78 px of a listing icon, which are
    SPEC.md 11.97's deferred content work and 11.97.4's closing paragraph.
    `diff` ends in a forced repaint, so the control also CLEANS the glass -
    which is what makes the measurement after it a clean one. Both parts
    assert their OWN artifact's locus, in the control as well as after.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty, os88sym, os88ui                                 # noqa: E402

S = os88sym.linear
MBAR_H = 20
TITLE_H = 18

# Paint's own chrome, from apps/paint/paint.asm - so the ink below reaches the
# canvas's LAST pixel rather than an estimate of it. PT_CV_X is the canvas's
# left inset, PT_CHROME_W/H what the frame costs the canvas on each axis, and
# TITLE_H+1 the top inset, so the canvas is
#     (x + PT_CV_X, y + TITLE_H + 1) .. (x + w - 3, y + h - 24)
# and eight pixels short of that corner is a shrink Paint will still ACCEPT,
# which is a test that proves nothing and says so (it asserts the refusal).
PT_CV_X = 48
PT_CHROME_W = PT_CV_X + 2
PT_CHROME_H = 22 + 1 + 19

fails = []
skips = []


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def frame(m, card):
    """The glass, one byte per pixel. VGA is four planes behind the Graphics
    Controller and not flat-readable, so there it is the CARD's own raster."""
    if card == "vga":
        w, h, px = m.fbuf()
        return w, h, bytes(px[i] for i in range(0, len(px), 3))
    w, h, rows = m.vram(card)
    return w, h, bytes(b for r in rows for b in r)


def full_repaint(m):
    """Make the GUEST repaint, and leave the machine paused."""
    m.cmd(cmd="run")
    m.write(S("cp_dirty"), b"\x01")
    for _ in range(400):
        time.sleep(0.05)
        if m.read(S("cp_dirty"), 1)[0] == 0:
            break
    else:
        raise RuntimeError("ui_task never drained [cp_dirty]")
    os88marty.settle(m)
    m.cmd(cmd="pause")


def diff(m, card, tag, show=6):
    """Pixels on which the glass and a forced repaint disagree."""
    os88marty.settle(m)
    m.cmd(cmd="pause")
    w, h, before = frame(m, card)
    full_repaint(m)
    _, _, after = frame(m, card)
    d = [i for i in range(len(before)) if before[i] != after[i]]
    # THE CLOCK. A forced repaint takes real wall time, so a run can straddle
    # the menu bar's once-a-minute change: two glyph cells at the top right,
    # nothing to do with any window.
    clk = u16(m.read(S("vid_clk_hx"), 2))
    d = [i for i in d if not (i // w < MBAR_H and i % w >= clk)]
    pts = [(i % w, i // w) for i in d]
    if pts:
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        print("   %-40s %5d px   x %d..%d  y %d..%d"
              % (tag, len(pts), min(xs), max(xs), min(ys), max(ys)))
        print("      %s%s" % ("  ".join("(%d,%d)" % p for p in pts[:show]),
                              " ..." if len(pts) > show else ""))
    else:
        print("   %-40s     0 px" % tag)
    m.cmd(cmd="run")
    return pts


def _u16w(ui, name):
    return u16(ui.m.read(S(name), 2))


def _same(a, b):
    return (a.x, a.y, a.w, a.h) == (b.x, b.y, b.w, b.h)


def _drag_grow(ui, w, tw, th):
    """One grow-box drag, CONFIRMED against guest state at both ends.

    A drag that did not happen leaves the window's size unchanged, which is
    exactly what a REFUSAL looks like - and telling those two apart is what
    this whole test turns on. So neither end is assumed and neither is timed:

      * `ui_grow`'s first instruction is `mov [ui_dragwin], bx`, so zeroing
        that word and reading it back says whether the press reached the grow
        box AT ALL.
      * `[ui_dragw]`/`[ui_dragh]` are the TRACKED outline, and this WAITS FOR
        THEM TO SETTLE before letting go rather than sleeping a fixed time.
        `ui_grow` samples `[mouse_x]` once a TICK and tests for the release
        FIRST, so a release in the same pass as the last move commits the size
        from the pass before it - and a fixed sleep is exactly the thing
        docs/plans/SOAK-PARALLEL.md 1 says a loaded box shortens: this row
        passed run after run by hand and failed inside a four-lane soak, on a
        drag whose moves the guest never got to.

    Paint declares no minimum of its own, so `wm_min_axd` clamps the outline to
    the SYSTEM floor (SPEC.md 11.100.2) - far below anything Paint will accept.
    A settled outline that is still the window's own size therefore means the
    moves were lost, never that the window is as small as it will go.

    AND `l=True` IS NOT DECORATION. `Mouse.to` sends the button UP unless it is
    told otherwise, so a move between a press and a release is a RELEASE: every
    move here cancelled the drag, `ui_grow` took its `evq_mup` on the first
    pass and committed the size it started with - which is a grab that reads
    perfectly, an outline that never moved, and a window that did not change
    size, i.e. a refusal this test had not earned. It was intermittent only
    because a sleep between the moves sometimes let `ui_grow` sample
    `[mouse_x]` before the release packet arrived. `os88ui._grab` has always
    passed it; this is the one drag in the tree written by hand."""
    w = ui._refresh(w)
    mo = ui.mo
    ui.m.write(S("ui_dragwin"), b"\0\0")
    gx, gy = w.x + w.w - 7, w.y + w.h - 7        # the 13x13 box's centre
    tx, ty = w.x + tw - 7, w.y + th - 7
    mo.to(gx, gy)
    mo._edge(True)
    for t in range(1, 7):
        mo.to(gx + (tx - gx) * t // 6, gy + (ty - gy) * t // 6, l=True)
    last = None
    for _ in range(80):                         # ...until the outline STOPS
        time.sleep(0.15)                        # moving, which is the guest
        now = (_u16w(ui, "ui_dragw"), _u16w(ui, "ui_dragh"))
        if now == last:
            break
        last = now
    mo._edge(False)
    ui.settle()
    ok = (_u16w(ui, "ui_dragwin") != 0
          and last is not None and last[0] < w.w and last[1] < w.h)
    return ui._refresh(w), ok


def _grow(ui, w, tw, th, tries=5):
    """...and retry a drag the guest did not see."""
    for _ in range(tries):
        got, ok = _drag_grow(ui, w, tw, th)
        if ok:
            return got
    raise RuntimeError(
        "the grow box at (%d,%d) could not be dragged in %d tries: "
        "ui_dragwin = %d, the outline settled at %dx%d and the window is %dx%d"
        % (w.x + w.w - 7, w.y + w.h - 7, tries, _u16w(ui, "ui_dragwin"),
           _u16w(ui, "ui_dragw"), _u16w(ui, "ui_dragh"), w.w, w.h))


def _floor(ui, w, who, tries=6):
    """Put a window on its own smallest size, and say what that is.

    The floor is the WINDOW's answer and never a number this test may hold:
    Paint's depends on the artwork it is carrying (SPEC.md 42.6.5), which is
    the point - once it is there, the drag AFTER it is refused by arithmetic
    and this test has a refusal it did not have to get lucky for. One
    unchanged answer ends it, because `_grow` has already established that the
    drag REACHED the guest."""
    for _ in range(tries):
        was = w
        w = _grow(ui, w, 40, 40)
        if _same(w, was):
            break
    print("   %s floored at (%d,%d) %dx%d" % (who, w.x, w.y, w.w, w.h))
    return w


def _ink(ui, w):
    """Draw corner to corner of Paint's canvas, so its floor is the artwork's.

    It starts past the TOOL PALETTE - a stroke begun at `x + 20` presses a
    TOOL and lays no ink at all - and it STAYS INSIDE THE FRAME: the stroke is
    the application's, so a pointer that leaves Paint stops laying ink wherever
    it left rather than at the canvas corner, which is also a target off the
    screen on a 640x200 CGA desktop and one os88mouse raises on.

    THREE strokes and not one: the diagonal pins the corner, and the two
    sweeps pin the extreme column and the extreme row on their own, so a
    dropped move in the middle of one cannot quietly shorten the artwork's
    bounding box - which is the quantity `pt_sizeask` answers on.

    NOTHING HERE HAS TO BE EXACT, and an earlier version of this file that
    needed it to be spent its time there: the ink decides WHERE Paint's floor
    is and `_floor` then goes and finds it, so ink eight pixels short of the
    canvas corner costs eight pixels of window and not a failed run."""
    mo = ui.mo
    x0, y0 = w.x + PT_CV_X + 2, w.y + TITLE_H + 3
    x1 = w.x + w.w - PT_CHROME_W + PT_CV_X - 1      # the canvas's last column
    y1 = w.y + w.h - PT_CHROME_H + TITLE_H          # ...and its last row
    if x1 <= x0 or y1 <= y0:
        return
    for ax, ay, bx, by in ((x0, y0, x1, y1),        # the diagonal
                           (x0, y1, x1, y1),        # ...the extreme ROW
                           (x1, y0, x1, y1)):       # ...and the extreme COLUMN
        mo.to(ax, ay)
        mo._edge(True)
        for t in range(1, 9):
            mo.to(ax + (bx - ax) * t // 8, ay + (by - ay) * t // 8, l=True)
        os88marty.guest_sleep(ui.m, 0.5)    # HOLD STILL before letting go, for
        mo._edge(False)                     # the reason a grow-box drag does:
        os88marty.guest_sleep(ui.m, 0.3)    # the application is a tick behind
                                            # the pointer. GUEST seconds, so a
                                            # loaded box cannot shorten it
    time.sleep(0.5)
    ui.settle()


def _refuse(ui, p, part):
    """The trigger: one more inward drag, which the floor makes a REFUSAL.

    SPEC.md 11.97.3's mechanism in one call - `ui_grow` marks everything under
    the mousedown rect as damaged BEFORE asking, and the refusal then sets
    `[wm_dmg_rzwin]`, so the window on top owes its content nothing."""
    was = ui._refresh(p)
    got = _grow(ui, p, 120, 90)
    print("   the inward drag left Paint (%d,%d) %dx%d (was %dx%d)"
          % (got.x, got.y, got.w, got.h, was.w, was.h))
    if not _same(got, was):
        fails.append("%s needs a REFUSED resize and Paint accepted one: "
                     "%dx%d -> %dx%d, so it was not on its floor"
                     % (part, was.w, was.h, got.w, got.h))
        return None
    # AND THE TOAST IS WAITED OUT. A refusal says so in its own strip (SPEC.md
    # 42.6.5), which is a window: measure while it is up and it comes and goes
    # BETWEEN the glass and the forced repaint, which reads as scattered stale
    # pixels having nothing to do with any window's chrome.
    for _ in range(40):
        if not ui.toast()[1]:
            break
        time.sleep(0.5)
    ui.settle()
    return got


def _shadow_px(pts, q):
    """Which of `pts` are on window q's drop-shadow L - the column x+w for rows
    y+1..y+h and the row y+h for columns x+1..x+w (SPEC.md 11.95.2)."""
    return [p for p in pts
            if (p[1] == q.y + q.h and q.x < p[0] <= q.x + q.w)
            or (p[0] == q.x + q.w and q.y < p[1] <= q.y + q.h)]


def _title_px(pts, q):
    return [p for p in pts
            if q.y <= p[1] < q.y + TITLE_H and q.x <= p[0] < q.x + q.w]


def _session(a):
    card = "vga" if "vga" in a.machine else (
        "cga" if "cga" in a.machine and "both" not in a.machine else "herc")
    return card, os88ui.boot(a.image, apps=a.apps, machine=a.machine)


# =============================================================================
# PART 1 - a drop shadow whose every pixel is inside the window above it
# =============================================================================
def part_shadow(a):
    """The reporter's own repro, near enough verbatim.

    On a 640x480 VGA it needs no window moved: a Disk window opens at (103,80)
    322x200 and Paint over it at (71,24) 498x322, so the Disk window's frame
    AND its drop shadow are already wholly inside Paint. On a 640x200 CGA
    desktop Paint is 152 rows and the Disk window 155, so it is cut down and
    moved in - which is why the fit is COMPUTED here and not assumed.

    Wholly inside is the geometry SPEC.md 11.97.3 is about, and the reason
    `wm_covered` skips the window at `.dfull` and leaves the shadow-only path
    to draw the L on its own."""
    print("\n=== part 1: a WHOLLY covered drop shadow (%s) ===\n" % a.machine)
    card, session = _session(a)
    with session as ui:
        m = ui.m
        ui.open_drive("B")
        ui.open("APPS")
        d = ui.windows()[-1]

        p = ui.open("PAINT.O88")
        time.sleep(2)
        ui.settle()
        _ink(ui, ui._refresh(p))            # the reporter's own step...
        p = _floor(ui, p, "Paint")          # ...and then down to the floor it
                                            # pins, so the drag below is a
                                            # refusal and not a coin toss
        d = ui._refresh(d)

        def fits(q):
            return (p.x <= q.x and p.x + p.w - 1 >= q.x + q.w
                    and p.y <= q.y and p.y + p.h - 1 >= q.y + q.h)

        if not fits(d):                     # ...cut it down and move it in
            ui.raise_window(d)              # (a Disk window has no negotiator,
                                            # so one landed drag does it)
            d = _grow(ui, d, p.w - 40, p.h - TITLE_H - 20)
            ui.move_window(d, p.x + 8, p.y + TITLE_H + 4)
            ui.raise_window(p)
            ui.settle()
            p, d = ui._refresh(p), ui._refresh(d)
        inside = fits(d)
        print("   Paint (%d,%d) %dx%d   Disk (%d,%d) %dx%d"
              % (p.x, p.y, p.w, p.h, d.x, d.y, d.w, d.h))
        print("   the Disk window's frame AND shadow are inside Paint: %s"
              % inside)
        if not inside:
            # A DESKTOP TOO SHORT TO STACK TWO WINDOWS IS NOT A FAILURE, and
            # saying so is the difference between a row that reports what it
            # saw and one that reports what it wanted. Part 2 needs only the
            # title strip covered and runs on every adapter.
            print("   -> SKIP: this desktop (%dx%d) cannot put one of these "
                  "windows wholly inside the other"
                  % (_u16w(ui, "vid_w"), _u16w(ui, "vid_h")))
            skips.append("part 1 on %s: the desktop cannot host the geometry"
                         % a.machine)
            return

        ui.mo.to(8, MBAR_H + 8)             # park it: the pointer is drawn
        base = diff(m, card, "CONTROL before the resize")
        if _shadow_px(base, d):
            fails.append("part 1's control already has %d px on the shadow L, "
                         "so what is measured below is not the resize"
                         % len(_shadow_px(base, d)))

        if _refuse(ui, p, "part 1") is None:
            return
        ui.mo.to(8, MBAR_H + 8)
        got = diff(m, card, "after the refused shrink")
        on = _shadow_px(got, ui._refresh(d))
        print("   -> %d stale px, %d of them the Disk window's own shadow L"
              % (len(got), len(on)))
        if on:
            fails.append("part 1: %d px of a WHOLLY covered drop shadow are "
                         "standing on the window above it (SPEC.md 11.97.3)"
                         % len(on))
        elif got:
            print("   (%d px elsewhere - not this row's, see the header)"
                  % len(got))


# =============================================================================
# PART 2 - a title strip whose every pixel is inside the window above it
# =============================================================================
def part_title(a):
    """The other half of the report, and the case `wm_covered` cannot catch.

    The Disk window is moved down so that its TITLE STRIP alone lies under
    Paint and its body hangs out below: `wm_covered` asks about the whole
    window, answers "some of it shows", and `wm_draw_win` then draws the title
    bar with no region armed over it at all (SPEC.md 11.97.1)."""
    print("\n=== part 2: a WHOLLY covered title strip (%s) ===\n" % a.machine)
    card, session = _session(a)
    with session as ui:
        m = ui.m
        ui.open_drive("B")
        ui.open("APPS")
        d = ui.windows()[-1]

        p = ui.open("PAINT.O88")
        time.sleep(2)
        ui.settle()
        _ink(ui, ui._refresh(p))
        p = _floor(ui, p, "Paint")          # FIRST: the Disk window is placed
                                            # against Paint's FINAL rect, so a
                                            # resize after the placement cannot
                                            # take the title strip out from
                                            # under it
        d = ui._refresh(d)
        ui.move_window(d, d.x, p.y + p.h - TITLE_H - 6)
        ui.raise_window(p)
        ui.settle()
        p, d = ui._refresh(p), ui._refresh(d)
        covers = (p.x <= d.x and p.x + p.w - 1 >= d.x + d.w - 1
                  and p.y <= d.y and p.y + p.h - 1 >= d.y + TITLE_H - 1
                  and p.y + p.h - 1 < d.y + d.h)
        print("   Paint (%d,%d) %dx%d   Disk (%d,%d) %dx%d"
              % (p.x, p.y, p.w, p.h, d.x, d.y, d.w, d.h))
        print("   Paint covers the whole title strip, body exposed: %s"
              % covers)
        if not covers:
            fails.append("part 2's geometry was not reached: Paint does not "
                         "wholly cover the Disk window's title strip with its "
                         "body left showing")
            return

        ui.mo.to(8, MBAR_H + 8)
        base = diff(m, card, "CONTROL before the resize")
        if _title_px(base, d):
            fails.append("part 2's control already has %d px in the title "
                         "strip, so what is measured below is not the resize"
                         % len(_title_px(base, d)))

        if _refuse(ui, p, "part 2") is None:
            return
        ui.mo.to(8, MBAR_H + 8)
        got = diff(m, card, "after the refused shrink")
        d = ui._refresh(d)
        ttl = _title_px(got, d)
        print("   -> %d stale px, %d of them in the covered title strip"
              % (len(got), len(ttl)))
        if ttl:
            fails.append("part 2: %d px of a WHOLLY covered title strip are "
                         "standing on the window above it (SPEC.md 11.97.4)"
                         % len(ttl))
        # THE REST IS NOT THIS ROW'S and SPEC.md 11.97.4 says why: SPEC.md
        # 11.3.3's cull rounds a straddling CELL outward on the argument that
        # something above repaints it in the same pass, and 11.90.3's
        # pure-shrink window is the case where nothing does. Reported rather
        # than asserted, because the fix for it is 11.97's deferred
        # per-fragment content work.
        rest = len(got) - len(ttl)
        if rest > 0:
            print("   (%d px elsewhere - the CULL's own premise, SPEC.md "
                  "11.97.4's closing paragraph, not asserted here)" % rest)



# =============================================================================
# PART 3 - a refused resize repaints NOTHING (SPEC.md 11.91.5)
# =============================================================================
def _dmg_hit(mm, rec):
    """Read the two one-shots WHILE THE GUEST IS STOPPED at `wm_paint_dmg`.

    `[wm_dmg_stwin]` is the window whose vacated rect is armed and is set by
    `ui_drag` and `ui_grow` alone (SPEC.md 11.91.2); `[wm_dmg_rzwin]` is
    11.90.3's pure-shrink window. Both are one-shots spent inside the pass, so
    they are only readable here - a microsecond after the resume they are 0
    and the record would say nothing."""
    return (u16(mm.read(S("wm_dmg_stwin"), 2)),
            u16(mm.read(S("wm_dmg_rzwin"), 2)))


def _vacating_passes(tr):
    """...and how many of the stops carried an armed vacated rect."""
    return [h for h in tr.hits if h.get("hit") and h["hit"][0]]


def part_nodmg(a):
    """A resize that changed nothing damages nothing.

    THE ASSERTION IS A COUNT AND NOT A PIXEL, and that is the point: with
    SPEC.md 11.97 in place the glass after a refused resize is already correct,
    so a pixel diff cannot tell "the chrome was redrawn identically" from "the
    chrome was never redrawn". What is being removed here is the WORK - the
    resized window's own title bar composed again by `wm_dmg_wins`'s
    mark-by-pointer, and two drawing calls of drop shadow on every neighbour
    under its L.

    THE CONTROL IS A RESIZE THE APPLICATION ACCEPTS, taken first and on the
    same window, because a count of zero proves nothing without one: an armed
    breakpoint that never fires reads identically to a symbol that was never
    reached, and this file has already paid once for a trigger that silently
    did not happen (see `_ink` in the header).

    THE TOAST IS WHY THE FILTER IS THERE. A refusal says so in its own strip
    (SPEC.md 42.6.5), which is a window, so `wm_paint_dmg` IS entered during
    this gesture - just not by `ui_grow`. `[wm_dmg_stwin]` is what tells them
    apart, and it also catches the half of 11.91.5 that is easiest to get
    wrong: the vacated rect is a one-shot whose keeper is `wm_dmg_wins`, so an
    early return that forgets to disarm it hands the TOAST's own damage pass a
    rect describing somebody else's window - which shows up here as the very
    count that was meant to be zero."""
    print("\n=== part 3: a REFUSED resize repaints nothing (%s) ===\n"
          % a.machine)
    card, session = _session(a)
    with session as ui:
        m = ui.m
        ui.open_drive("B")
        ui.open("APPS")
        ui.open("PAINT.O88")
        time.sleep(2)
        ui.settle()
        p = ui._refresh(ui.windows()[-1])
        print("   Paint (%d,%d) %dx%d" % (p.x, p.y, p.w, p.h))

        # --- the CONTROL: an inward drag on an EMPTY canvas, which is taken --
        was = p
        with os88marty.bp_trace(m, "wm_paint_dmg", on_hit=_dmg_hit) as tr:
            p = _grow(ui, p, max(p.w - 80, 120), max(p.h - 60, 90))
        acc = _vacating_passes(tr)
        print("   CONTROL  an ACCEPTED resize %dx%d -> %dx%d: %d damage "
              "pass(es), %d of them vacating"
              % (was.w, was.h, p.w, p.h, tr.count(), len(acc)))
        if _same(p, was):
            fails.append("part 3's control needs an ACCEPTED resize and Paint "
                         "refused one at %dx%d, so the zero below would prove "
                         "nothing" % (was.w, was.h))
            return
        if not acc:
            fails.append("part 3's control: an accepted resize %dx%d -> %dx%d "
                         "reached wm_paint_dmg with a vacated rect armed 0 "
                         "times, so the breakpoint is measuring nothing"
                         % (was.w, was.h, p.w, p.h))
            return

        # --- ...and now the same drag on a floor the artwork pins ------------
        _ink(ui, ui._refresh(p))
        p = _floor(ui, p, "Paint")
        was = ui._refresh(p)
        with os88marty.bp_trace(m, "wm_paint_dmg", on_hit=_dmg_hit) as tr:
            got = _grow(ui, p, 120, 90)
            os88marty.guest_sleep(m, 1.5)   # the toast and whatever it costs
        ref = _vacating_passes(tr)
        print("   REFUSED  the inward drag left Paint (%d,%d) %dx%d (was "
              "%dx%d): %d damage pass(es), %d of them vacating"
              % (got.x, got.y, got.w, got.h, was.w, was.h,
                 tr.count(), len(ref)))
        if not _same(got, was):
            fails.append("part 3 needs a REFUSED resize and Paint accepted "
                         "one: %dx%d -> %dx%d, so it was not on its floor"
                         % (was.w, was.h, got.w, got.h))
            return
        for h in ref:
            print("      a vacating pass at %s: [wm_dmg_stwin] = 0x%04X, "
                  "[wm_dmg_rzwin] = 0x%04X" % (h["name"], h["hit"][0],
                                               h["hit"][1]))
        if ref:
            fails.append("part 3: a resize that changed NOTHING still ran %d "
                         "damage pass(es) with a vacated rect armed - the "
                         "window under the hand redraws its own chrome and "
                         "every neighbour under its shadow L draws that "
                         "(SPEC.md 11.91.5)" % len(ref))
        else:
            print("   -> no damage pass carried a vacated rect: nothing was "
                  "repainted and the one-shot was disarmed")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--part", choices=("shadow", "title", "nodmg", "both"),
                    default="both")
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args()

    if a.part in ("shadow", "both"):
        part_shadow(a)
    if a.part in ("title", "both"):
        part_title(a)
    if a.part in ("nodmg", "both"):
        part_nodmg(a)

    print()
    for k in skips:
        print("SKIP: %s" % k)
    for f in fails:
        print("FAIL: %s" % f)
    print("wmchrome: %s"
          % ("ok" if not fails else "%d failure(s)" % len(fails)))
    sys.exit(1 if fails else 0)


main()

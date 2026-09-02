#!/usr/bin/env python3
"""DESKBENCH - what the ordinary things cost on a BUSY desktop, per adapter.

    make && python3 tests/deskbench.py                    # CGA
    python3 tests/deskbench.py --machine os8088_5150_herc_gla
    python3 tests/deskbench.py --machine os8088_xt_vga
    python3 tests/deskbench.py --all --json out.json      # all three

This is a MEASUREMENT and not a gate: no threshold is asserted, because a
number that fails a build when something gets slower teaches nobody anything
(tests/wirefps says the same). It prints, and PERFORMANCE.md is where a figure
is kept. What it DOES assert is its own scene - see "the scene is checked"
below - because a run that set the desktop up differently is not slower or
faster than the last one, it is a different measurement wearing the same name.

--- why a fixed scene ------------------------------------------------------

Every other harness here prices a PRIMITIVE (`gfxbench`), a package's own loop
(`wirefps`), or one operation in isolation (`paintgif`). None of them answers
the question a person actually has, which is what the machine feels like with
four windows open - and the reason none of them does is that "four windows
open" is not reproducible unless somebody writes down exactly which four.

So this is the standard scene, and the rule that shaped it is that EVERY
WINDOW IN IT CAN BE PUT BACK IN THE SAME STATE:

  z-order, bottom to top
    1. the CONTROL PANEL, on the Display page      background, partly visible
    2. a DISK window on A:                         background, partly visible
    3. NOTE PAD holding README.TXT, stretched to
       the desktop's full height                   beside Paint, under it
    4. PAINT holding MEDIA/OS8088.GIF              on top

Nothing here is hand-drawn, typed, scrolled to an arbitrary place, or timed
out of a network. Two files that ship on the system disk, both opened through
their ASSOCIATION (SPEC.md 54) so the launch path is one double-click, and two
windows the kernel draws itself. That is the whole selection rule: a live
drawing surface would be a better test of drawing and a worse benchmark,
because the second run would not be measuring the first run's picture.

--- what a row measures ----------------------------------------------------

`m.flicker` samples the glass once per DISPLAYED frame (PERFORMANCE.md Part
3.1). Each action is injected with the machine PAUSED - which is that call's
own contract - so the input lands inside the capture window instead of racing
it, and then:

  ms         the span from the FIRST frame that changed to the LAST, in guest
             milliseconds off MartyPC's cycle counter. Not host time, and not
             the whole capture: an injected mouse packet is ~0.5 guest seconds
             in flight at 1200 baud (SPEC.md 7.3.1) and those frames are still,
             so they fall outside the span rather than into it.
  changed    pixels the redraw actually moved, summed over the span. This is
             the SIZE of the operation and it is what makes ms comparable -
             the same ms over twice the pixels is twice as good.
  transient  pixels that changed and changed BACK inside the span: the flash
             (Part 1's second invisible defect). A composited redraw writes
             every pixel once and reads 0 here; an erase-then-draw pair reads
             its own area. This is the column that catches a change nothing
             else can see.
  frames     how many displayed frames the span covered, and `trunc` if the
             screen was still moving when the capture ran out.

READ `frames` BEFORE BELIEVING `ms`. One displayed frame is 16.7 guest
milliseconds, and PERFORMANCE.md Part 2 prices a single 78-cell text row on
this machine at 71 - so any repaint of real size is TENS of frames, and a row
that reports a hundred thousand changed pixels in four of them has been cut
short by the burst rule rather than being fast. The Hercules `full-screen
redraw` row does exactly that as this is written: 78.7 ms over 4 frames for
110,937 pixels, against the CGA's 600.6 over 36 for 120,895. Do not quote it.

--- the scene is CHECKED, not assumed --------------------------------------

Four windows, each one's rect printed, and for each of the three background
windows a point on its title bar that no window above it covers - found by
walking the z-order rather than by arithmetic somebody did once. If a window
did not open, did not land where it was put, or is wholly buried, the run
FAILS and prints the geometry instead of printing numbers. A benchmark that
quietly measures three windows because the fourth did not open is worse than
no benchmark.

The Display page is HIDDEN on a single-adapter machine (SPEC.md 39.11.1), so
on a one-card CGA or Hercules the Control Panel opens on the page named by
--cp-page-fallback instead. Which page was used is printed and goes in the
JSON: the scene is then still identical to itself across runs on that adapter,
which is what a comparison needs.

--- and every action is PAIRED with its inverse ----------------------------

Paint moves out and back, Note Pad comes forward and Paint goes forward again,
the menu drops and is dismissed, Note Pad pages down and pages up. That is not
tidiness: it means the scene at the end of the run is the scene at the start,
so the rows do not depend on the order they ran in and a row can be added in
the middle without moving every number below it. It also doubles the samples
for free - a move out and a move back are two measurements of a window move.

Prefix db_ in the JSON; the file's own functions are unprefixed.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
from os88geom import MBAR_H, TITLE_H                        # noqa: E402

S = os88sym.linear
CPU_HZ = 4772727.0                      # 14.31818 MHz / 3, the 8088's own

# The three machines, one per adapter (SPEC.md 39). GLaBIOS variants because
# the IBM 5150 ROM is not redistributable and is absent from a fresh container;
# the ROM is not part of what is being measured.
MACHINES = [("cga", "os8088_5150_cga_gla"),
            ("herc", "os8088_5150_herc_gla"),
            ("vga", "os8088_xt_vga")]

GIF, TXT, MEDIA = "OS8088.GIF", "README.TXT", "MEDIA"
# The scene's proportions. Fractions of the live screen, so the layout is the
# same SHAPE on 640x200, 720x348 and 640x480 rather than the same numbers.
DB_CASC = 32                            # title bar left exposed per
                                        # background window, in pixels
DB_PAINT_W = 0.55                       # Paint: this much of the screen wide
DB_PAINT_H = 0.72                       # ...and of the desktop band tall
DB_QUIET = 12                           # frames under peak/8 that END a
                                        # redraw's burst - see measure()
DB_MAXBURST = 240                       # ...and the longest a burst may be
                                        # before the row says TRUNCATED
DB_FLIGHT = 24                          # frames of a mouse packet's flight to
                                        # spend before a capture - see release()


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


# -----------------------------------------------------------------------------
# measuring one action
# -----------------------------------------------------------------------------
def measure(m, name, trigger, frames=400, burst="first"):
    """Inject `trigger` with the machine paused, then price what it drew.

    `burst` says WHICH END of the capture the redraw is at, and it is not a
    detail - it is the difference between a stable row and a row that reports
    50 ms one run and 600 the next.

    A press on a title bar raises the window AND starts ui_drag, whose XOR
    outline is redrawn once a tick for as long as the button is down: on a
    PRESS row the redraw comes first and the outline is a suffix, so the span
    is the FIRST burst. On a RELEASE row the button has been down since before
    the capture opened, so the outline is a PREFIX - it runs while the release
    packet is in flight, ~0.5 guest seconds at 1200 baud (SPEC.md 7.3.1) - and
    the commit is the last thing that happens, so the span is the LAST burst.
    A single rule for both got the prefix on one kind and the suffix on the
    other, and inflated whichever it got.

    A burst ends at DB_QUIET consecutive frames under peak/8. Not peak/4: a
    window repaint on a 4.77 MHz 8088 is hundreds of milliseconds -
    PERFORMANCE.md Part 2 prices ONE 78-cell text row at 71 - so it spans
    twenty frames or more at 16.7 guest ms each and its per-frame rate is
    nowhere near its peak for most of them. At peak/4 the full-screen redraw
    came back as 33 ms and 2 frames, which is one frame of a repaint that
    measurably takes 600.

    Everything inside the span is summed, quiet frames included, so the totals
    are the operation's and not the threshold's. `tail` counts frames that
    kept moving outside it - a large tail on a press row is the button still
    being down - and a row whose `transient` approaches its `changed` is an
    outline being XORed rather than a window being drawn.
    """
    m.pause()
    trigger()
    fl = m.flicker(frames=frames)
    m.run()
    per = fl.get("per_frame", [])
    ch = [p["changed"] for p in per]
    if not any(ch):
        return dict(row=name, frames=0, ms=0.0, changed=0, transient=0,
                    trunc=False, dead=True, tail=0,
                    per_frame=[[p["changed"], p["transient"]] for p in per])
    peak = max(ch)
    thr = max(64, peak // 8)
    live = [i for i, c in enumerate(ch) if c >= thr]
    if burst == "last":
        b = live[-1]
        a, quiet = b, 0
        for i in range(b - 1, -1, -1):
            if ch[i] >= thr:
                a, quiet = i, 0
            else:
                quiet += 1
                if quiet >= DB_QUIET:
                    break
    else:
        a = live[0]
        b, quiet = a, 0
        for i in range(a + 1, min(len(ch), a + DB_MAXBURST)):
            if ch[i] >= thr:
                b, quiet = i, 0
            else:
                quiet += 1
                if quiet >= DB_QUIET:
                    break
    c0 = per[a - 1]["cycles"] if a else 0
    cyc = per[b]["cycles"] - c0
    return dict(row=name, frames=b - a + 1, ms=1000.0 * cyc / CPU_HZ,
                changed=sum(ch[a:b + 1]),
                transient=sum(p["transient"] for p in per[a:b + 1]),
                tail=sum(1 for c in (ch[:a] + ch[b + 1:]) if c),
                trunc=(b - a + 1 >= DB_MAXBURST) or (b >= len(per) - 1),
                dead=False,
                per_frame=[[p["changed"], p["transient"]] for p in per])


# -----------------------------------------------------------------------------
# the scene
# -----------------------------------------------------------------------------
def rects(m):
    """Every visible window's rect, in Z-ORDER, backmost first.

    OFF wm_zord AND NOT off dispcp.win_list, which is SLOT order - "newest
    last" - and says nothing about stacking. Read the wrong way this file
    still runs and still passes its own z-order check, and the screenshot
    shows the Control Panel on top of everything: every raise after the first
    clicked a title bar that the first raise had just covered, and
    exposed_title agreed because it was looking at the windows BELOW rather
    than above. wm_zord[0] is backmost (kernel/wm.inc).
    """
    n = m.read(S("wm_zn"), 1)[0]
    z = m.read(S("wm_zord"), max(n, 1))[:n]
    live = set(dispcp.win_list(m, S))
    return [(i, dispcp.win_rect(m, S, i)) for i in z if i in live]


def exposed_title(zs, i):
    """A point on window i's title bar that no window ABOVE it covers.

    Walked rather than computed: the layout below places windows by dragging
    them, a drag lands where wm_place says it may (SPEC.md 11.100), and a
    click point derived from where a window was ASKED to go is a click that
    can miss. Returns None if the window's whole bar is buried, which is a
    scene failure and not something to work around.
    """
    _, (x, y, w, h) = zs[i]
    above = [r for _, r in zs[i + 1:]]
    ty = y + TITLE_H // 2
    for px in range(x + 6, x + w - 6, 2):
        if not any(ax <= px < ax + aw and ay <= ty < ay + ah
                   for ax, ay, aw, ah in above):
            return px, ty
    return None


def drag_to(m, mo, win_xy, to_xy, settle):
    """Move a window by its title bar and let the commit finish."""
    mo.drag(win_xy[0], win_xy[1], to_xy[0], to_xy[1])
    settle(m)


def raise_win(m, mo, slot, settle, why):
    """Bring `slot` to the front by clicking an EXPOSED part of its title bar.

    Needed before every navigation in the Disk window, and it is not
    housekeeping: dispcp.open_named finds the row by reading kernel state and
    then clicks where that row IS, so with another window over the list the
    click lands on that window instead. The symptom is not an error - the
    listing simply does not change, and the NEXT open_named says the file it
    wants "is not in this folder" while naming the folder it never left.
    """
    zs = rects(m)
    i = [j for j, (sl, _) in enumerate(zs) if sl == slot]
    if not i:
        raise RuntimeError("no window %04x to raise (%s)" % (slot, why))
    pt = exposed_title(zs, i[0])
    if pt is None:
        raise RuntimeError("cannot raise %04x for %s - its title bar is "
                           "wholly buried: %r" % (slot, why, [r for _, r in zs]))
    mo.click(*pt)
    settle(m)


def build(m, mo, settle, cp_fallback, say):
    """Put the standard desktop up, and answer what is in it.

    THE OPENING ORDER *IS* THE Z-ORDER, and there is no raise loop at the end.
    A placement drag raises the window it grabs, so placing each window the
    moment it opens leaves the stack in placement order - Control Panel, Disk,
    Note Pad, Paint - with nothing to fix up afterwards. Two orderings that do
    not work, both measured getting here:

      - open everything, then place it. Paint is 512x152 on a 640x200 CGA - it
        IS the desktop - so from the moment it opens no other title bar is
        reachable and there is nothing left to drag.
      - build the stack by raising each window bottom-up at the end. Raising
        the Control Panel to the BOTTOM puts it on top for one step, and every
        window whose exposed strip was to its right is buried at exactly that
        moment. It fails on the second raise, every time.

    ONLY NOTE PAD IS RESIZED, and only to the desktop's full height so that it
    is showing as much of README.TXT as the adapter allows - which is the
    point of having it in the scene at all, and matters most on a Hercules or
    a VGA where its default height is a fraction of the band. Paint keeps the
    size it opens at, so the picture is drawn the same way every run.

    (Both apps ARE resizable; neither says so in its template. They call
    `OSAPI_WM_SIZABLE` from the entry proc, so grepping the sources for
    `WF_SIZABLE` finds nothing and concludes the opposite - it did here.)

    A left-edge CASCADE for the two background windows. On a CGA the band is
    152 rows and a Disk window is 155, so it fills the desktop and there is no
    strip BELOW anything to hold a title bar; horizontal room is the only room
    there is. DB_CASC pixels of each is left out to the left of the one above.
    """
    w = u16(m.read(S("vid_w"), 2))
    h = u16(m.read(S("vid_h"), 2))
    dock = u16(m.read(S("vid_dock_y0"), 2))
    top, bot = MBAR_H + 2, dock - 2
    say("screen %dx%d, desktop band %d..%d" % (w, h, top, bot))

    def place(slot, x, why):
        """Drag `slot` so its left edge is at x and its bar is on the top row.

        Grabbed at a point exposed_title FOUND, not at the bar's midpoint: by
        the time Paint is placed it is under two other windows and its middle
        is not reachable.
        """
        zs = rects(m)
        i = [j for j, (sl, _) in enumerate(zs) if sl == slot]
        if not i:
            raise RuntimeError("no window %04x to place (%s)" % (slot, why))
        pt = exposed_title(zs, i[0])
        if pt is None:
            raise RuntimeError("cannot place %s - %04x's title bar is wholly "
                               "buried: %r" % (why, slot, [r for _, r in zs]))
        rx, ry, rw, rh = zs[i[0]][1]
        mo.drag(pt[0], pt[1], pt[0] + (x - rx), top + TITLE_H // 2)
        settle(m)
        got = dispcp.win_rect(m, S, slot)
        say("  %-8s %3d x %3d -> x %3d y %3d" % (why, got[2], got[3],
                                                 got[0], got[1]))
        return got

    # --- 1. the Control Panel, on the Display page where there is one --------
    hide = m.read(S("cp_hide"), 1)[0]
    page = dispcp.CP_IVID
    if hide & (1 << page):
        page = cp_fallback
        say("Display page hidden (SPEC.md 39.11.1) - Control Panel on record "
            "%d instead" % page)
    dispcp.open_panel(m, mo, S, settle, page=page)
    if dispcp._cp_win(m, S) is None:
        raise RuntimeError("the Control Panel did not open")
    cslot = cpw_slot(m, S)
    place(cslot, 0, "panel")

    # --- 2. a Disk window on the SYSTEM disk, which carries both files -------
    dispcp.open_drive(m, mo, S, settle, "A")
    disk = dispcp.win_list(m, S)[-1]
    place(disk, DB_CASC, "disk")

    # --- 3. Paint, through MEDIA/OS8088.GIF's association (SPEC.md 54) ------
    # THE DEEPER FOLDER FIRST: the Disk window is unobstructed right now, and
    # after Note Pad opens over it every navigation costs a raise.
    dx, dy = dispcp.win_rect(m, S, disk)[:2]
    dispcp.open_named(m, mo, S, settle, dx, dy, MEDIA)
    dx, dy = dispcp.win_rect(m, S, disk)[:2]
    before = set(dispcp.win_list(m, S))
    dispcp.open_named(m, mo, S, settle, dx, dy, GIF)
    paint = new_window(m, before, "Paint", GIF)

    # --- 4. Note Pad, through README.TXT's ----------------------------------
    # The Disk window has to come forward to be navigated: open_named finds the
    # row by reading kernel state and then clicks where that row IS, so with
    # another window over the list the click lands on that window instead. The
    # symptom is not an error - the listing does not change, and the NEXT
    # open_named says the file it wants "is not in this folder" while naming
    # the folder it never left.
    raise_win(m, mo, disk, settle, "to walk back out of MEDIA")
    dx, dy = dispcp.win_rect(m, S, disk)[:2]
    dispcp.open_row(m, mo, S, settle, dx, dy, row=0, expect="..")
    dx, dy = dispcp.win_rect(m, S, disk)[:2]
    before = set(dispcp.win_list(m, S))
    dispcp.open_named(m, mo, S, settle, dx, dy, TXT)
    note = new_window(m, before, "Note Pad", TXT)

    # ...stretched to the desktop's full height, while it is still the front
    # window - which is what wm_hit needs for the grow box (SPEC.md 13.5
    # region 4). Its width is left alone: it is what decides how much of the
    # scene Paint can overlap, and it is checked below.
    nx, ny, nw, nh = dispcp.win_rect(m, S, note)
    mo.drag(nx + nw - 4, ny + nh - 4, nx + nw - 4, bot)
    settle(m)

    # --- 5. ...and the two placements that also settle the z-order ----------
    nw, nh = dispcp.win_rect(m, S, note)[2:]
    say("  notepad stretched to %d x %d (band is %d)" % (nw, nh, bot - top))
    pw = dispcp.win_rect(m, S, paint)[2]
    note_x = max(0, w - nw - 4)
    paint_x = 2 * DB_CASC
    if paint_x + pw <= note_x:
        raise RuntimeError("Paint (%d wide at %d) does not reach Note Pad (at "
                           "%d) on a %d-wide screen - the scene wants them to "
                           "overlap" % (pw, paint_x, note_x, w))
    if paint_x + pw >= note_x + nw:
        raise RuntimeError("Paint (%d..%d) swallows Note Pad (%d..%d) - there "
                           "is no strip of its title bar left to click"
                           % (paint_x, paint_x + pw, note_x, note_x + nw))
    place(note, note_x, "notepad")
    place(paint, paint_x, "paint")
    return dict(w=w, h=h, dock=dock, top=top, bot=bot, cp_page=page,
                cp=cslot, disk=disk, note=note, paint=paint)


def cpw_slot(m, S_):
    """The Control Panel's window slot, as dispcp finds it."""
    got = tuple(dispcp._cp_win(m, S_))
    for s in dispcp.win_list(m, S_):
        if dispcp.win_rect(m, S_, s)[:2] == got:
            return s
    raise RuntimeError("the Control Panel's window is in no slot: %r" % (got,))


def new_window(m, before, what, via, limit=90.0):
    """The window that appeared, WAITED FOR rather than assumed.

    dispcp.open_named settles, and settling is not the same question: a launch
    through an association reads the package off the floppy, claims its
    memory and decodes a picture, and the screen can sit perfectly still in
    the middle of that. Measured on a 640x480 VGA, where Paint's canvas is
    four times a CGA's: the row said "Paint did not open" while a probe that
    waited ten more guest seconds found the window every time. So this waits
    for the thing it is looking for, which is tests/paintgif.py's rule -
    the whole question is how long it takes, so a fixed wait either truncates
    the slow case or pads the fast one.
    """
    t = time.time()
    while time.time() - t < limit:
        now = [w for w in dispcp.win_list(m, S) if w not in before]
        if now:
            return now[-1]
        m.advance(frames=60)
        m.run()
    raise RuntimeError("%s did not open on %s's association in %.0fs "
                       "(SPEC.md 54)" % (what, via, limit))


def check(m, sc, say):
    """The scene, asserted. Returns the click points the rows below use."""
    zs = rects(m)
    if len(zs) != 4:
        raise RuntimeError("the scene is %d windows, not 4: %r"
                           % (len(zs), [r for _, r in zs]))
    order = [s for s, _ in zs]
    want = [sc["cp"], sc["disk"], sc["note"], sc["paint"]]
    if order != want:
        raise RuntimeError("z-order is %r, wanted CP/Disk/Note/Paint %r"
                           % (order, want))
    pts = {}
    for name, i in (("cp", 0), ("disk", 1), ("note", 2)):
        pt = exposed_title(zs, i)
        if pt is None:
            raise RuntimeError("%s's title bar is wholly buried: %r"
                               % (name, [r for _, r in zs]))
        pts[name] = pt
    _, pr = zs[3]
    pts["paint"] = (pr[0] + pr[2] // 2, pr[1] + TITLE_H // 2)
    for name, i in (("cp", 0), ("disk", 1), ("note", 2), ("paint", 3)):
        say("  %-6s %-22s title click %r"
            % (name, "x %d y %d w %d h %d" % zs[i][1], pts[name]))
    return pts


# -----------------------------------------------------------------------------
# the rows
# -----------------------------------------------------------------------------
def drag_hold(m, mo, frm, to):
    """Press at `frm` and walk the outline to `to`, LEAVING THE BUTTON DOWN.

    Raw relative packets rather than mo.to(..., l=True), and as few of them as
    the move needs. Every packet is a chance for the 1200-baud UART to drop
    one, and a dropped one mid-drag does not look like a dropped one: ui_drag
    falls back to the button LEVEL, sees it clear, and commits the move early -
    so the release injected afterwards lands on nothing and the row comes back
    NOTHING DREW. Measured, on exactly that row.

    No settle anywhere in here either: settle waits for the screen to stop
    changing and an XOR outline never does.
    """
    mo.to(*frm)
    mo._edge(True)
    dx, dy = to[0] - frm[0], to[1] - frm[1]
    while dx or dy:
        sx = max(-100, min(100, dx))
        sy = max(-100, min(100, dy))
        m.mouse(sx, sy, l=True)
        dx -= sx
        dy -= sy
        m.advance(frames=45)
    if not (mo.where()[2] & 1):
        raise RuntimeError("the drag ended before the release - a packet was "
                           "dropped and ui_drag committed on the level")


def rows(m, mo, sc, pts, settle, say):
    """The measured actions. Every one is PAIRED with its inverse, so the
    scene at the end is the scene at the start and a row can be added in the
    middle without moving every number below it."""
    out = []
    press = lambda: m.mouse(0, 0, l=True)

    def release():
        """Let go, and SKIP THE PACKET'S FLIGHT before the capture starts.

        A Microsoft packet is ~0.5 guest seconds down a 1200-baud line
        (SPEC.md 7.3.1), and the button is still down for all of it - so a
        capture opened at the release begins with twenty-odd frames of
        ui_drag's XOR outline and only then the commit. The outline's frames
        and the commit's tail are the same magnitude, so which one a threshold
        catches depends on where the packet happened to land: the same row
        measured 50 ms and 300 ms on two consecutive runs of this file.
        DB_FLIGHT frames of the flight are spent here instead, so the capture
        opens on the commit and the ordinary first-burst rule applies.
        """
        m.mouse(0, 0, l=False)
        m.advance(frames=DB_FLIGHT)

    def release_now():
        """...and the same edge WITHOUT skipping the flight, for a release
        that is not ending a drag. A menu is press-drag-release (SPEC.md 12.2)
        and there is no outline in front of it, so spending the flight here
        spends the dismissal too: the row came back NOTHING DREW."""
        m.mouse(0, 0, l=False)

    def park(pt):
        """Park the pointer and let the screen settle, so the press is the
        only thing inside the capture window."""
        mo.to(*pt)
        settle(m)

    def grab(slot, why):
        """A point on `slot`'s title bar that is exposed RIGHT NOW.

        Recomputed per row and never cached. The scene moves during the run -
        Paint is dragged and put back, the fullscreen round trip repaints
        everything - so a click point worked out when the scene was built can
        end up on top of a different window entirely. Measured: after Paint
        moved +57 and its move-back silently failed, every "raise" row was
        clicking Paint's own title and reported a 1,364-pixel redraw for
        raising a window that was already in front.
        """
        zs = rects(m)
        i = [j for j, (sl, _) in enumerate(zs) if sl == slot]
        if not i:
            raise RuntimeError("no window %04x to %s" % (slot, why))
        pt = exposed_title(zs, i[0])
        if pt is None:
            raise RuntimeError("cannot %s - %04x's title bar is buried: %r"
                               % (why, slot, [r for _, r in zs]))
        return pt

    def raise_row(label, slot, frames=600):
        """A raise, measured on the RELEASE - because that is when it happens.

        The press raises it AND starts ui_drag; the release only ends a
        drag of zero length. So the press is what to capture, and the outline
        that follows it is what measure()'s quiet-run test is for.

        BOTH ENDS ARE CHECKED, because the two ways this row can lie are
        silent. It must not already be frontmost - a no-op raise redraws the
        title stripe and nothing else, ~1,400 pixels, which reads as a
        gloriously fast raise. And it must BE frontmost afterwards.
        """
        before = [sl for sl, _ in rects(m)]
        if before[-1] == slot:
            raise RuntimeError("%s: it is already the front window, so there "
                               "is nothing to raise (z-order %r)"
                               % (label, before))
        park(grab(slot, label))
        r = measure(m, label, press, frames=frames)
        release_now()
        m.run()
        settle(m)
        now = [sl for sl, _ in rects(m)]
        if now[-1] != slot:
            raise RuntimeError("%s did not raise it - z-order is %r, was %r"
                               % (label, now, before))
        return r

    # --- 1. A FULL-SCREEN REDRAW, both ways --------------------------------
    # Paint's own fullscreen bracket (SPEC.md 42), reached with `f`. Going in
    # is one window drawn over the whole glass; coming OUT is the
    # `wm_paint_all` fsx_restore owes the desktop - the desktop dither, the
    # drive zones, the dock, the menu bar and all four windows - which is the
    # full-screen redraw this scene exists to price.
    #
    # NOT THE SCREEN SAVER, which was the first thing tried and is the obvious
    # lever (SPEC.md 79.6's wake is a wm_paint_all too). It measures its own
    # animation: an injected keypress is ~0.5 guest seconds in flight and the
    # saver is drawing throughout, so the span starts on a cube face rather
    # than on the wake and comes back 1,451 ms and 2,027,560 transient pixels
    # of which none are the redraw.
    out.append(measure(m, "Paint to fullscreen",
                       lambda: m.type_text("f"), frames=600))
    settle(m)
    out.append(measure(m, "full-screen redraw (fullscreen exit)",
                       lambda: m.type_text("f"), frames=600))
    settle(m)
    n = len(rects(m))
    if n != 4:
        raise RuntimeError("the fullscreen round trip left %d windows, not 4 "
                           "- the scene is no longer the scene" % n)

    # --- 2. MOVING PAINT, out and back -------------------------------------
    # A drag is an XOR OUTLINE while the button is down (ui_drag in
    # kernel/ui.inc) and the window is committed on the RELEASE, so the
    # release is the whole redraw and the tracking is not in it. The pointer
    # is walked with the button held - cheap, it only moves an outline - and
    # only the release is injected into the capture.
    px, py, pw, ph = dispcp.win_rect(m, S, sc["paint"])
    dx = max(24, min(96, sc["w"] - px - pw - 8))
    dy = max(12, min(48, sc["bot"] - py - ph - 4))
    say("  paint moves by (%+d, %+d) and back" % (dx, dy))
    home = (px, py)
    for label, (mx, my) in (("move Paint away", (dx, dy)),
                            ("move Paint back", (-dx, -dy))):
        # THE GRAB POINT IS READ FRESH each time. A drag lands where wm_place
        # says it may (SPEC.md 11.100), not where it was aimed, so the point
        # to grab for the move BACK is derived from where the window actually
        # is - not from where the first drag was told to put it.
        rx, ry = dispcp.win_rect(m, S, sc["paint"])[:2]
        frm = grab(sc["paint"], label)
        drag_hold(m, mo, frm, (frm[0] + mx, frm[1] + my))
        out.append(measure(m, label, release, frames=600))
        settle(m)
        got = dispcp.win_rect(m, S, sc["paint"])[:2]
        if got == (rx, ry):
            raise RuntimeError("%s moved nothing - Paint is still at %r"
                               % (label, got))
    end = dispcp.win_rect(m, S, sc["paint"])[:2]
    if end != home:
        # wm_place SNAPS (SPEC.md 11.100), so a delta and its negation do not
        # compose to zero. Deterministic, and it would still drift a few
        # pixels a run - so the window is put back on its home square before
        # anything else is measured against it.
        say("  ...Paint landed on %r rather than %r; putting it back" %
            (end, home))
        frm = grab(sc["paint"], "put Paint back")
        drag_hold(m, mo, frm, (frm[0] + home[0] - end[0],
                               frm[1] + home[1] - end[1]))
        release()
        m.run()
        settle(m)
        say("  ...Paint is at %r" % (dispcp.win_rect(m, S, sc["paint"])[:2],))

    # --- 3. RAISING, from one window deep and from three -------------------
    # Note Pad is directly under Paint; the Disk window is under both of those
    # AND is the widest thing in the scene. The pair says whether a raise
    # costs what it UNCOVERS or what it draws.
    out.append(raise_row("raise Note Pad", sc["note"]))
    out.append(raise_row("raise Disk window", sc["disk"]))
    out.append(raise_row("raise Control Panel", sc["cp"]))

    # --- 4. MOVING A CONTROL-HEAVY WINDOW ----------------------------------
    # The same operation as row 2 over a completely different content: Paint
    # is a bitmap blit, the Control Panel is a list, a frame and a page of
    # radio buttons and captions. Reading the two ms columns against the two
    # `changed` columns is what says whether a redraw is priced by its pixels
    # or by its call count (PERFORMANCE.md Part 1).
    #
    # ...AND IT IS MOVED WHILE IT IS IN FRONT, which the raise above has just
    # made it. Moved from the back it is a 24-pixel strip of title bar between
    # two other windows, and a 64-pixel move puts that strip under Paint - so
    # the move BACK has nothing left to grab. Measured: "cannot move it back -
    # 0000's title bar is buried".
    # A SMALL delta, and the reason is the CGA again: the Control Panel is 320
    # wide on a 640-wide screen with Paint at 63..575 and Note Pad at 375..635
    # over it, so a 64-pixel move leaves Paint with 8 pixels of title bar and
    # the row after this one cannot find anything to click. 24 leaves 24.
    cw = dispcp.win_rect(m, S, sc["cp"])[2]
    cdx = 24
    chome = dispcp.win_rect(m, S, sc["cp"])[:2]
    for label, mx in (("move Control Panel", cdx), ("move it back", -cdx)):
        rx, ry = dispcp.win_rect(m, S, sc["cp"])[:2]
        frm = grab(sc["cp"], label)
        drag_hold(m, mo, frm, (frm[0] + mx, frm[1]))
        out.append(measure(m, label, release, frames=600))
        settle(m)
        got = dispcp.win_rect(m, S, sc["cp"])[:2]
        say("  %-20s %r -> %r" % (label, (rx, ry), got))
        if got == (rx, ry):
            raise RuntimeError("%s moved nothing" % label)
    got = dispcp.win_rect(m, S, sc["cp"])[:2]
    if got != chome:
        frm = grab(sc["cp"], "put the panel back")
        drag_hold(m, mo, frm, (frm[0] + chome[0] - got[0],
                               frm[1] + chome[1] - got[1]))
        release()
        m.run()
        settle(m)
        say("  panel put back to %r"
            % (dispcp.win_rect(m, S, sc["cp"])[:2],))
    # ...and Paint goes back to the front UNMEASURED. It is the one window in
    # the scene whose repaint spreads thinly enough per frame to be the same
    # magnitude as its own drag outline, so the span cannot be separated from
    # the button being down and the row came back 9,946 ms and TRUNCATED. A
    # plain click restores the scene without pretending to price it.
    mo.click(*grab(sc["paint"], "put Paint back in front"))
    settle(m)
    z = [sl for sl, _ in rects(m)]
    if z[-1] != sc["paint"]:
        raise RuntimeError("Paint is not back in front: z-order %r" % z)

    # --- 5. A MENU, dropped and dismissed ----------------------------------
    # SPEC.md 12.2's damage path: the drop saves what it covers and the
    # dismissal puts it back, so the pair is the same pixels twice and the
    # `transient` column is the one to read.
    #
    # A menu is press-drag-release (SPEC.md 12.2), so the two halves are the
    # two edges of ONE click: the press drops it, the release over the title
    # dismisses it without choosing anything. The pair covers the same pixels
    # twice, which is what makes `transient` the column to read here.
    mx, my = 8 + 20, MBAR_H // 2
    park((mx, my))
    out.append(measure(m, "menu drop (front window)", press, frames=400))
    out.append(measure(m, "menu dismiss", release_now, frames=400))
    m.run()
    settle(m)
    n = len(rects(m))
    if n != 4:
        raise RuntimeError("the menu pair left %d windows, not 4 - something "
                           "in it was CHOSEN rather than dismissed" % n)
    return out


# -----------------------------------------------------------------------------
def run_one(a, label, machine, say):
    settle = os88marty.settle
    with os88marty.launch(a.image, apps=a.apps, machine=machine,
                          timeout=a.timeout) as m:
        settle(m)
        mo = os88mouse.Mouse(marty=m)
        say("building the scene on %s" % machine)
        sc = build(m, mo, settle, a.cp_page_fallback, say)
        pts = check(m, sc, say)
        fw, fh, fb = m.fbuf()
        if a.shots:
            os88marty.write_png_rgb(
                os.path.join(a.shots, "deskbench-%s.png" % label), fw, fh, fb)
        # ...off the RENDERED framebuffer and not m.vram(), which is the 1bpp
        # cards' own memory and comes back empty on a planar VGA: the scene
        # printed "0 lit pixels" there, which reads as a blank screen.
        lit = sum(1 for i in range(0, len(fb), 3) if fb[i] > 127)
        say("  scene up: %d lit pixels" % lit)
        rs = rows(m, mo, sc, pts, settle, say)
    return dict(adapter=label, machine=machine, lit=lit,
                w=sc["w"], h=sc["h"], cp_page=sc["cp_page"], rows=rs)


def report(res):
    print()
    print("  %-38s %8s %7s %10s %10s %5s"
          % ("%s (%dx%d)" % (res["adapter"], res["w"], res["h"]),
             "ms", "frames", "changed", "transient", "tail"))
    print("  " + "-" * 84)
    for r in res["rows"]:
        flag = " TRUNCATED" if r["trunc"] else (" NOTHING DREW" if r["dead"]
                                               else "")
        print("  %-38s %8.1f %7d %10d %10d %5d%s"
              % (r["row"], r["ms"], r["frames"], r["changed"], r["transient"],
                 r.get("tail", 0), flag))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINES[0][1])
    ap.add_argument("--all", action="store_true",
                    help="one run per adapter, which is what the scene is for")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--json", default=None)
    ap.add_argument("--shots", default=None)
    ap.add_argument("--timeout", type=float, default=600.0)
    ap.add_argument("--cp-page-fallback", type=int, default=0,
                    help="which Control Panel record to open when the Display "
                         "page is hidden (SPEC.md 39.11.1)")
    a = ap.parse_args(argv)

    want = MACHINES if a.all else [
        (dict((v, k) for k, v in MACHINES).get(a.machine, "?"), a.machine)]
    allres, bad = [], 0
    for label, machine in want:
        print("=== %s: %s" % (label, machine))
        try:
            res = run_one(a, label, machine, lambda s: print("  " + s))
        except Exception as e:                              # noqa: BLE001
            print("  SCENE FAILED: %s" % e)
            bad += 1
            continue
        allres.append(res)
        report(res)
    if a.json:
        json.dump(allres, open(a.json, "w"), indent=1)
        print("\ndeskbench: %s" % a.json)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""SPEC.md 31.1.2: scrolling the Drivers list draws the LIST once, not thrice.

    python3 tests/drvscroll.py [machine] [image]

Reported off the panel: clicking the down arrow "appears to refresh twice in
its current state before drawing the new scrolled down state". SPEC.md
13.8.3's pressed look reached this page as `cp_drv_boxes` - EVERY row's
checkbox - on both edges of every press, and `os88ui_glyph` white-fills its
box before it draws a pixel, so the four rows that had not changed blinked
twice in front of a scroll that had not happened yet. The arrows meanwhile are
not in `cp_drv_boxes` at all and drew nothing: the page was repainting the
controls that had not moved on behalf of the one that had.

Run it against the kernel before the fix and after - the numbers are the
report, and the checks are what a build has to pass either way:

    python3 tests/drvscroll.py os8088_5150_cga_gla <ref image>
    python3 tests/drvscroll.py os8088_5150_cga_gla build/os8088-360.img

THE ROW BAND IS THE ASSERTION. A press on an arrow may draw the arrow and
nothing else, so the four checkboxes beside it are compared pixel for pixel
across the press - which is one number that cannot be argued with, where a
flicker count over the whole screen mixes in the mouse pointer's own cell
(SPEC.md 7.1: the arrow is drawn INTO the framebuffer, so every gfx_lock hold
takes it off and puts it back).

  A  the press does not touch the four rows beside the arrow
  B  ...and costs one frame rather than a list's worth of glyphs
  C  the arrow is DRAWN held (SPEC.md 13.8), and sliding off puts back
     exactly the pixels that were there - the cell is idempotent
  D  one click is one row of scroll: not two, and not none
  E  the scrolled pane matches a FORCED full repaint, pixel for pixel
  F  a greyed arrow at the end stop draws nothing and moves nothing
  G  a driver ROW still draws its own box held, and only its own
"""
import sys
import time

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M                                        # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
from os88geom import WIN_SIZE, MAX_WIN                       # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
IMG = sys.argv[2] if len(sys.argv) > 2 else "build/os8088-360.img"

TITLE_H = 18
CP_CW, CP_CH = 318, 132
CP_RX = 96                          # ctrl.inc: the pane's left edge
CP_I0Y, CP_IROWH, CP_IDRV = 6, 14, 2
CP_DVIS, CP_DBY1, CP_DROWH = 4, 20, 26
CP_DSX1, CP_DSW, CP_DSAH = 200, 14, 14
CP_DBYN = CP_DBY1 + CP_DROWH * CP_DVIS
PRESS_MS = 60.0                     # B's bar: the fix costs one frame (16.6 ms
                                    # on a CGA) and the four glyphs it replaces
                                    # are ~35-50 ms EACH on this machine

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


def panel(m):
    """The panel's frame rect, matched on W_TITLE (dispcp's rule)."""
    want = m.sym("cp_ttl") - (M.KERNEL_SEG << 4)
    b = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    for i in range(MAX_WIN):
        r = b[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, 0) & 3 == 3 and _u16(r, 10) == want:
            return tuple(_u16(r, o) for o in (2, 4, 6, 8))
    return None


def quiet(m, s=15.0):
    try:
        M.settle(m, limit=s)
    except M.MartyError:
        pass


def band(m, rect):
    """The rect's pixels, so two samples can be COMPARED and not just counted."""
    w, h, rows = m.vram()
    return bytes(rows[y][x] for y in range(rect[1], rect[3] + 1)
                 for x in range(rect[0], rect[2] + 1))


def ndiff(a, b):
    return sum(1 for x, y in zip(a, b) if x != y)


def capture(m, down, frames=40):
    """One button EDGE with the glass sampled per DISPLAYED frame.

    The packet is clocked in while the machine is PAUSED, so the edge lands
    inside the capture window instead of racing it - the flicker instrument's
    own rule, and why this cannot go through os88mouse's proven `_edge`: that
    one polls the guest's published mouse_btn, and a paused guest publishes
    nothing.
    """
    m.pause()
    m.mouse(0, 0, l=down)
    r = m.flicker(frames=frames)
    m.run()
    quiet(m)
    moved = [f for f in r["per_frame"] if f["changed"]]
    span = ((moved[-1]["cycles"] - moved[0]["cycles"])
            / (r["cpu_mhz"] * 1000.0) if len(moved) > 1 else 0.0)
    # THE UNION OF THE FLASHING RECTS, which is what says WHERE a redraw that
    # changed no pixel happened - a control drawn twice to the same value is
    # invisible to a pixel diff and unmistakable here.
    bb = None
    for f in r["per_frame"]:
        b = f["bbox"]
        if not f["transient"] or not b:
            continue
        bb = b if bb is None else [min(bb[0], b[0]), min(bb[1], b[1]),
                                   max(bb[2], b[2]), max(bb[3], b[3])]
    return {"frames": len(moved), "span": span, "flashbb": bb,
            "changed": max((f["changed"] for f in moved), default=0),
            "flash": max((f["transient"] for f in moved), default=0)}


def hits(bb, rect):
    """Does a flashing rect reach into `rect` at all?"""
    return bool(bb) and not (bb[0] > rect[2] or bb[2] < rect[0]
                             or bb[1] > rect[3] or bb[3] < rect[1])


def show(what, r):
    print("  %-24s %2d frame(s) = %6.1f ms   changed %5d   flash %5d  %s"
          % (what, r["frames"], r["span"], r["changed"], r["flash"],
             r["flashbb"]))


print(f"drvscroll: {MACHINE} on {IMG}\n")
with M.launch(IMG, machine=MACHINE) as m:
    mo = Mouse(marty=m)
    if m.read(m.sym("vid_kind"), 1)[0] == 0:      # VID_VGA: four planes, so
        sys.exit("drvscroll reads the 1bpp framebuffer: "   # no flat vram
                 "run it on CGA or Hercules")

    mo.menu(12, 8, 40, 8 + 18 + 1 + 14)          # System > Control Panel
    quiet(m)
    w = panel(m)
    if not w:
        sys.exit("no Control Panel window")
    cx, cy = w[0] + 1, w[1] + TITLE_H
    print(f"  panel frame {w}, content ({cx},{cy})\n")

    mo.click(cx + 8, cy + CP_I0Y + CP_IDRV * CP_IROWH + 6)   # the Drivers item
    quiet(m)

    ax = cx + CP_RX + CP_DSX1
    dnx, dny = ax + CP_DSW // 2, cy + CP_DBYN - CP_DSAH // 2
    upy = cy + CP_DBY1 + CP_DSAH // 2
    dncell = (ax, cy + CP_DBYN - CP_DSAH, ax + CP_DSW - 1, cy + CP_DBYN - 1)
    rows = (cx + CP_RX, cy + CP_DBY1, ax - 1, cy + CP_DBYN - 1)
    pane = (cx + CP_RX, cy, cx + CP_CW - 1, min(cy + CP_CH, w[1] + w[3]) - 1)
    dtop = m.sym("cp_dtop")
    # THE POINTER PARKED CLEAR OF THE PANE, because it is drawn into the
    # framebuffer (SPEC.md 7.1) and would otherwise be the difference between
    # two samples taken with it in different places.
    park = (w[0] - 40, cy + 4)

    # --- F: the UP arrow at the top is greyed, and a click on it is nothing --
    mo.to(dnx, upy)
    time.sleep(0.8)
    before = band(m, pane)
    t0 = m.read(dtop, 1)[0]
    gup, gdn = capture(m, True), capture(m, False)
    show("greyed UP press", gup)
    show("greyed UP release", gdn)
    n = ndiff(before, band(m, pane))
    check("F: a greyed arrow draws nothing", n == 0, f"({n} px)")
    check("F: ...and does not repaint to say so",
          gup["span"] < PRESS_MS and gdn["span"] < PRESS_MS,
          f"({gup['span']:.1f} / {gdn['span']:.1f} ms)")
    check("F: ...and scrolls nothing", m.read(dtop, 1)[0] == t0,
          f"(cp_dtop {t0} -> {m.read(dtop, 1)[0]})")

    # --- A/B/C: the DOWN arrow, pressed and then CANCELLED by sliding off ----
    mo.to(*park)
    time.sleep(0.8)
    idle = band(m, dncell)              # the cell with the pointer elsewhere
    rows_before = band(m, rows)

    mo.to(dnx, dny)
    time.sleep(0.5)
    press = capture(m, True)
    show("DOWN press", press)
    held = band(m, dncell)
    check("C: the arrow is drawn HELD", ndiff(idle, held) > 60,
          f"({ndiff(idle, held)} px differ)")
    n = ndiff(rows_before, band(m, rows))
    check("A: the press leaves the rows' pixels alone", n == 0, f"({n} px)")
    check("A: ...and does not REDRAW them either",
          not hits(press["flashbb"], rows),
          f"(flash {press['flashbb']} vs rows {list(rows)})")
    check("B: the press costs one frame, not a list of glyphs",
          press["span"] < PRESS_MS, f"({press['span']:.1f} ms)")

    mo.to(*park, l=True)                # slide OFF while held: cancelled
    time.sleep(1.0)
    mo._edge(False)
    quiet(m)
    n = ndiff(idle, band(m, dncell))
    check("C: sliding off puts the cell back exactly", n == 0, f"({n} px)")
    check("C: ...and a cancelled press scrolls nothing",
          m.read(dtop, 1)[0] == t0, f"(cp_dtop {m.read(dtop, 1)[0]})")

    # --- D/E: the scroll itself ---------------------------------------------
    mo.to(dnx, dny)
    time.sleep(0.5)
    capture(m, True)
    rel = capture(m, False, frames=90)
    show("DOWN release + scroll", rel)
    check("D: one click is one row", m.read(dtop, 1)[0] == t0 + 1,
          f"(cp_dtop {t0} -> {m.read(dtop, 1)[0]})")

    # --- G: a driver ROW is still the control it always was -----------------
    r0 = (cx + CP_RX, cy + CP_DBY1, ax - 1, cy + CP_DBY1 + CP_DROWH - 1)
    rest = (cx + CP_RX, cy + CP_DBY1 + CP_DROWH, ax - 1, cy + CP_DBYN - 1)
    mo.to(*park)
    quiet(m)
    r0_idle, rest_idle = band(m, r0), band(m, rest)
    mo.to(cx + CP_RX + 10, cy + CP_DBY1 + CP_DROWH // 2)
    time.sleep(0.5)
    rp = capture(m, True)
    show("ROW 0 press", rp)
    check("G: the row's own box is drawn held",
          ndiff(r0_idle, band(m, r0)) > 20,
          f"({ndiff(r0_idle, band(m, r0))} px differ)")
    check("G: ...and the other three rows are not redrawn",
          not hits(rp["flashbb"], rest),
          f"(flash {rp['flashbb']} vs rows 1..3 {list(rest)})")
    mo.to(*park, l=True)                # cancel: no driver is loaded by this
    time.sleep(1.0)
    mo._edge(False)
    quiet(m)
    n = ndiff(rest_idle, band(m, rest))
    check("G: a cancelled row press leaves the rest alone", n == 0, f"({n} px)")

    mo.to(*park)
    quiet(m)
    after = band(m, pane)
    mo.click(cx + 8, cy + CP_I0Y + 6)                        # away...
    quiet(m)
    mo.click(cx + 8, cy + CP_I0Y + CP_IDRV * CP_IROWH + 6)   # ...and back
    mo.to(*park)
    quiet(m)
    n = ndiff(after, band(m, pane))
    check("E: the scrolled pane matches a full repaint", n == 0, f"({n} px)")

print()
if fails:
    sys.exit("drvscroll: FAILED - " + ", ".join(fails))
print("drvscroll: all checks passed")

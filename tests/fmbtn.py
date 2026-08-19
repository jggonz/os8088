#!/usr/bin/env python3
"""SPEC.md 22.18: the Disk window's two header buttons fire on the RELEASE.

    python3 tests/fmbtn.py [machine]

`Refresh` and the view toggle were the last two controls in the kernel's own
UI drawn by hand, and they acted on the press. Neither has a safe prefix
action (13.6) - Refresh is a mount and the toggle relays the whole window - so
a mis-aimed press did the thing with no way to think better of it.

The signal is the VIEW BIT (FS_VIEW in the window's own state block), because
that is one bit of kernel memory rather than a picture - tests/mouseup.py's
discipline. Refresh is deliberately NOT the button under test for the firing
cases: a re-list of a folder that has not changed is invisible in memory and
on the glass, so a build that had stopped dispatching would pass every case.

  A  press the toggle, slide OFF it, release   -> the view is UNCHANGED
  B  ...and the button came back UP while held (the tracking edge, 13.8.1)
  C  press AND release on it                   -> the view FLIPPED
  D  a press draws it DOWN

...plus the half that is not about events at all: the painter and the hit
test used to describe the rectangle TWICE, in two coordinate systems, so E
asserts that where the button is DRAWN is where it is CLICKABLE - a click
aimed at the drawn frame's own corners.
"""
import sys
import time

sys.path.insert(0, "tools")
import os88marty as M
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
W_FLAGS, W_X, W_Y, W_W, W_H, W_TITLE = 0, 2, 4, 6, 8, 10
TITLE_H = 18

# files.inc's fm_btab, mirrored deliberately: a gate that derives its
# coordinates from the kernel is testing the kernel against itself.
FM_BTAB = {"refresh": (76, 67), "view": (142, 135)}
FM_BTN_W, FM_BTN_H = 63, 14

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


def disk_win(m):
    """The Disk window, by its kind's title - there can be four."""
    blob = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    for i in range(MAX_WIN):
        r = blob[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, W_FLAGS) & 2 and _u16(r, W_H) > 60:
            return i, tuple(_u16(r, o) for o in (W_X, W_Y, W_W, W_H))
    return None, None


def brect(w, which):
    """fm_brect's answer, computed the way fm_btab says. ABSOLUTE, inclusive."""
    cx, cy = w[0] + 1, w[1] + TITLE_H
    cw = w[2] - 2
    rgt = cx + cw - 1
    need, back = FM_BTAB[which]
    if cw < need:
        return None
    x1 = rgt - back
    y1 = cy + 2
    return (x1, y1, x1 + FM_BTN_W - 1, y1 + FM_BTN_H - 1)


def view_bit(m, slot):
    """FS_VIEW out of the acting window's state block.

    [fm_vp] is a NEAR offset in KERNEL_SEG and m.sym answers a LINEAR address,
    so the segment base has to go back on by hand - mixing the two reads 0x600
    bytes low, lands inside the kernel image, and answers a plausible 0
    forever. That cost a whole debugging round here: three cases read as
    "the release does not fire" against a kernel that was firing correctly.
    """
    vp = int.from_bytes(m.read(m.sym("fm_vp"), 2), "little")
    return m.read((M.KERNEL_SEG << 4) + vp + FS_VIEW_OFS, 1)[0]


def lit(m, mono, rect):
    if mono:
        w, h, rows = m.vram()
        return sum(rows[y][x] for y in range(rect[1], rect[3] + 1)
                   for x in range(rect[0], rect[2] + 1))
    w, h, px = m.fbuf()
    n = 0
    for y in range(rect[1], rect[3] + 1):
        for x in range(rect[0], rect[2] + 1):
            i = (y * w + x) * 3
            if px[i] or px[i + 1] or px[i + 2]:
                n += 1
    return n


with M.launch("build/os8088-360.img", apps="build/apps360.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    mono = m.video()["type"] in ("cga", "mda", "herc")
    print(f"== {MACHINE} : Disk window header buttons (SPEC.md 22.18) ==")

    # FS_VIEW is an equ, so it has no symbol - take it from the source, which
    # is one place and the same place the kernel takes it from.
    import re
    src = open("kernel/files.inc").read()
    def equ(n):
        return int(re.search(r"^%s\s+equ\s+(\d+)" % n, src, re.M).group(1))
    FS_VIEW_OFS, FS_SEL_OFS, FS_SCRL_OFS = (equ("FS_VIEW"), equ("FS_SEL"),
                                            equ("FS_SCRL"))

    vw = int.from_bytes(m.read(m.sym("vid_w"), 2), "little")
    mo.dblclick(vw - 40, 46)                # a drive zone opens a Disk window
    M.settle(m)
    slot, w = disk_win(m)
    check("a Disk window opened", w is not None, f"{w}")
    if not w:
        sys.exit("no window")

    r = brect(w, "view")
    check("...wide enough for the view toggle", r is not None,
          f"(content {w[2]-2}px, needs {FM_BTAB['view'][0]})")
    if not r:
        sys.exit("window too narrow - widen the template or grow it here")

    mid = ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)
    was = view_bit(m, slot)

    # --- D: the press draws it DOWN ---------------------------------------
    # The INTERIOR, not the whole rect, and a HALVING rather than a
    # difference. A pressed 63x14 button turns ~700 lit pixels into ~40; the
    # mouse arrow parked on it is ~15, and a `!=` test passes on those alone -
    # which it did, against a build whose down state was not drawing at all
    # (fm_brect eats AX and fm_btn1 was reading the id after calling it).
    # That is PERFORMANCE.md Part 3.1's own trap: a count that cannot tell the
    # thing under test from the pointer sitting on it.
    inner = (r[0] + 1, r[1] + 1, r[2] - 1, r[3] - 1)
    mo.to(mid[0], r[1] - 30)                # park off the band first
    time.sleep(0.8)
    up = lit(m, mono, inner)
    mo.to(*mid)
    mo._edge(True)
    time.sleep(0.9)
    down = lit(m, mono, inner)
    check("a press draws the toggle DOWN", down * 2 < up,
          f"({up} lit upright, {down} held - the interior, so a "
          f"black fill more than halves it)")

    # --- B: it comes back UP while still held, off the button --------------
    mo.to(r[0] - 60, mid[1], l=True)        # l=True: STILL HELD
    time.sleep(1.2)
    off = lit(m, mono, inner)
    check("...and back UP when the pointer slides off it", off == up,
          f"({off} lit, upright is {up})")

    # --- A: the release lands off it: NOTHING fires ------------------------
    mo._edge(False)
    M.settle(m)
    check("a slide-off release does NOT flip the view",
          view_bit(m, slot) == was, f"(FS_VIEW = {view_bit(m, slot)})")

    # --- C: press AND release on it: the view flips ------------------------
    # The one that says the feature did not eat the feature it decorates.
    mo.to(*mid)
    mo._edge(True)
    time.sleep(0.7)
    mo._edge(False)
    M.settle(m)
    check("press-and-release ON it DOES flip the view",
          view_bit(m, slot) != was,
          f"(FS_VIEW = {view_bit(m, slot)}, was {was})")

    # --- E: drawn == clickable, at the CORNERS -----------------------------
    # The half that is not about events: the painter measured back from
    # [fm_rgt] and the hit test forward from [fm_cw], two arithmetics for one
    # rectangle. A click one pixel inside each corner of the DRAWN frame has
    # to fire, and one pixel outside must not.
    now = view_bit(m, slot)
    for name, pt in (("top-left", (r[0] + 1, r[1] + 1)),
                     ("bottom-right", (r[2] - 1, r[3] - 1))):
        mo.to(*pt)
        mo._edge(True)
        time.sleep(0.6)
        mo._edge(False)
        M.settle(m)
        check(f"...a click just inside the {name} corner fires",
              view_bit(m, slot) != now, f"(FS_VIEW = {view_bit(m, slot)})")
        now = view_bit(m, slot)

    mo.to(r[0] - 2, r[1] + 1)
    mo._edge(True)
    time.sleep(0.6)
    mo._edge(False)
    M.settle(m)
    check("...and one pixel OUTSIDE the left edge does not",
          view_bit(m, slot) == now, f"(FS_VIEW = {view_bit(m, slot)})")

    # --- F: the rest of the ladder still falls through --------------------
    # The button block sits at the TOP of fm_onclick's ladder and a point it
    # does not claim falls through to the scroll bar and then the rows. A
    # conversion that swallowed everything would pass every case above.
    if view_bit(m, slot) != 0:                  # back to list view: the grid
        mo.click(*mid)                          # has no scroll bar to click
        M.settle(m)
    cx, cy = w[0] + 1, w[1] + TITLE_H
    sel = int.from_bytes(m.read((M.KERNEL_SEG << 4) +
                                int.from_bytes(m.read(m.sym("fm_vp"), 2),
                                               "little") + FS_SEL_OFS, 2),
                         "little")
    mo.click(cx + 40, cy + 30)                  # the first file row
    M.settle(m)
    now = int.from_bytes(m.read((M.KERNEL_SEG << 4) +
                                int.from_bytes(m.read(m.sym("fm_vp"), 2),
                                               "little") + FS_SEL_OFS, 2),
                         "little")
    check("...and a click on a ROW still selects", now != sel,
          f"(FS_SEL {sel:#06x} -> {now:#06x})")

    scr = m.read((M.KERNEL_SEG << 4) +
                 int.from_bytes(m.read(m.sym("fm_vp"), 2), "little")
                 + FS_SCRL_OFS, 2)
    mo.click(cx + w[2] - 2 - 7, cy + 27)        # the scroll bar's up arrow,
    M.settle(m)                                 # at the top: inert but LEGAL
    check("...and a click on the scroll bar is still reached",
          m.read((M.KERNEL_SEG << 4) +
                 int.from_bytes(m.read(m.sym("fm_vp"), 2), "little")
                 + FS_SCRL_OFS, 2) == scr,
          "(at the top stop, so it moves nothing - what is asserted is that "
          "it does not fire a BUTTON)")

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")

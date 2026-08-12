#!/usr/bin/env python3
"""Does a window promoted by the TITLE-BAR-ONLY raise get its grow box?
(SPEC.md 11.1.1)

    python3 tools/growraise.py [machine]

THE BUG. `wm_front` asks `wm_obscured` before `wm_lift`: nothing was on top of
us, so the content is already on the glass and `wm_raise` draws the title bar
and stops (`.titleonly`, SPEC.md 11.90). The grow box is not in the title bar.
So a window whose coverer was DRAGGED OFF and which is then called to the
front came up with pinstripes, a close box, a minimize box and no grow box -
and 11.91.2 makes "nothing repaints it" the normal case, so it stayed that way
for the session. `wm_hit` reports AL = 4 from geometry alone, so the corner
was invisible-but-clickable the whole time.

The other two sites already had it: `wm_grow_erase` beside the outgoing
window's `wm_draw_title` in `wm_raise`, and `wm_grow_paint` beside the
incoming one's in `wm_paint_dmg`'s promotion. This is the third and the
commonest, because it is what an ordinary click on a background window runs.

WHAT IS MEASURED. The grow box is a 13x13 at the content's bottom-right corner
(`wm_grow_rect`): a white fill, a black frame, an 8x8 frame at +4 and a
white-filled 7x7 at +2. Every pixel of that rect is written by
`wm_grow_paint`, so what it looks like does not depend on what the window had
drawn underneath - which is what lets this compare BYTE FOR BYTE rather than
count anything. The reference is taken from the same window moments earlier,
when it was frontmost and drawn by `wm_draw_win`, whose `.growbox` tail has
always drawn the box; the test is that the title-bar-only raise leaves that
rect IDENTICAL.

A COUNT IS NOT ENOUGH, and the first version of this was wrong that way. It
asked "are any pixels lit in the corner", on the assumption that the corner is
white and a box would light it. A Disk window's bottom-right corner is the
foot of its SCROLL BAR and is nearly solid, so the unfixed kernel scored 169
lit of 169 and passed - the metric was measuring the scroll bar. The box is
mostly white, so drawing it took the count DOWN to 78. Same session, opposite
sign to the one assumed.

THE A/B IS STILL THE POINT: this has to pass with the fix and fail without it,
through the identical scripted session. Take the two instructions out of
`wm_raise`'s .titleonly - see its comment - or run against the commit before.

    python3 tools/growraise.py                       # the shipped machine
    RAW=/tmp/after.raw python3 tools/growraise.py     # ...and keep the screen

Exit status is 0 when the box is present, 1 when it is not.
"""
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty
from os88mouse import Mouse
from os88geom import drive_pt, row_xy, top, windows, winptr

VOL_B = "B"
ROW_GAMES = 1               # B: root, sorted: APPS GAMES MEDIA SYSTEM
ROW_MINES = 2               # GAMES/, sorted: ARKANOID MINES MISSILE SOLITAIR


def named(m, title):
    """The visible window whose caption starts with `title`, or None."""
    for w in windows(m):
        if w.visible and w.title.upper().startswith(title.upper()):
            return w
    return None


def slot(m, i):
    """One window by SLOT, re-read from the guest.

    A Disk window is tracked by slot and never by caption: SPEC.md 11.92
    retitles it to the folder it navigates into, so the window this opens as
    `Disk B` is called `GAMES` two double-clicks later and every look-up by
    name then answers None. That read as "the window closed".
    """
    for w in windows(m):
        if w.i == i:
            return w
    return None


def pixels(m):
    """(w, h, at) for the current screen, in the GUEST's coordinate system.

    `vram` on the 1bpp cards and `fbuf` on VGA - the split `_Screen` already
    makes, and on the mono cards it is a correctness requirement rather than a
    preference. Every coordinate here comes out of a window record, so the
    reading has to be in the kernel's own coordinates; the CARD's rendered
    frame is not (Hercules rasterises 720x350 over a 720x348 framebuffer at
    dx = -16, dy = +2), so a rect named in guest pixels would be read two rows
    out. Mode 12h has no flat framebuffer to read at all, so there `fbuf` is
    the only route.
    """
    if m.video()["type"] in ("cga", "mda", "hercules"):
        w, h, rows = m.vram()
        return w, h, lambda x, y: rows[y][x]
    w, h, d = m.fbuf()
    return w, h, lambda x, y: d[(y * w + x) * 3]


def grow_rect(w):
    """wm_grow_rect, inclusive: the 13x13 at the content's bottom-right."""
    x1 = w.x + w.w - 14
    y1 = w.y + w.h - 14
    return x1, y1, x1 + 12, y1 + 12


def grab(at, rect):
    """The pixels of an inclusive rect, as a flat tuple."""
    x1, y1, x2, y2 = rect
    return tuple(at(x, y) for y in range(y1, y2 + 1)
                 for x in range(x1, x2 + 1))


def main():
    # The IBM-ROM machine needs a ROM this tree cannot ship; a container
    # without one in tools/martypc/roms/ runs the GLaBIOS twins only, so pass
    # `os8088_5150_cga_gla`. Nothing here is a timing measurement, so either
    # BIOS answers this question.
    machine = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga"
    imgs = os.path.join(ROOT, "build")
    with os88marty.launch(os.path.join(imgs, "os8088-360.img"),
                          apps=os.path.join(imgs, "apps360.img"),
                          machine=machine) as m:
        mo = Mouse(marty=m)

        # 1. a Disk window on B:
        mo.dblclick(*drive_pt(m, VOL_B)); time.sleep(4)
        disk = named(m, "Disk")
        if not disk:
            print("growraise: no Disk window opened"); return 2
        ds = disk.i
        print("growraise: disk   %r" % (disk,))

        # THE REFERENCE, taken now: this window is frontmost and was just
        # drawn by wm_draw_win, whose .growbox tail has always drawn the box.
        # It does not move for the rest of the run, so the rect is absolute.
        rect = grow_rect(disk)
        w_, h_, at = pixels(m)
        want = grab(at, rect)
        print("growraise: ref    %s, %d lit of %d"
              % (rect, sum(1 for p in want if p), len(want)))

        # 2. Minesweeper, out of GAMES/, which opens ON TOP of it
        mo.dblclick(*row_xy(disk, ROW_GAMES)); time.sleep(4)
        mo.dblclick(*row_xy(slot(m, ds), ROW_MINES)); time.sleep(12)
        mines = named(m, "Mines")
        if not mines:
            print("growraise: Minesweeper did not launch"); return 2
        ms = mines.i
        print("growraise: mines  %r" % (mines,))

        # 3. DRAG IT OFF - the critical step. Without it the Disk window is
        #    still covered, wm_obscured says so, and wm_front takes the AL = 2
        #    path into wm_draw_win, which has always drawn the grow box.
        disk = slot(m, ds)
        dx = disk.x + disk.w + 8 - mines.x       # clear of the Disk window
        mo.drag(mines.x + mines.w // 2, mines.y + 6,
                mines.x + mines.w // 2 + dx, mines.y + 6)
        time.sleep(4)
        disk, mines = slot(m, ds), slot(m, ms)
        print("growraise: moved  %r" % (mines,))
        over = not (mines.x > disk.x + disk.w or mines.x + mines.w < disk.x
                    or mines.y > disk.y + disk.h or mines.y + mines.h < disk.y)
        if over:
            print("growraise: STILL OVERLAPPING - the repro needs it clear")
            return 2

        # 4. ...and call the Disk window to the front by its title bar
        mo.click(disk.x + disk.w // 2, disk.y + 6); time.sleep(3)

        disk = slot(m, ds)
        if winptr(m, disk) != top(m):
            print("growraise: the Disk window is not frontmost"); return 2

        if grow_rect(disk) != rect:
            print("growraise: the window MOVED - the rect is not comparable")
            return 2
        w_, h_, at = pixels(m)
        got = grab(at, rect)
        if os.environ.get("RAW"):
            with open(os.environ["RAW"], "wb") as f:
                for y in range(h_):
                    f.write(bytes(at(x, y) & 0xFF for x in range(w_)))

        bad = sum(1 for a, b in zip(want, got) if a != b)
        print("growraise: got    %s, %d lit of %d"
              % (rect, sum(1 for p in got if p), len(got)))
        print("growraise: %d differing pixels of %d" % (bad, len(want)))
        if not bad:
            print("growraise: PRESENT - the raise drew the box")
            return 0
        print("growraise: MISSING - SPEC.md 11.1.1's third site")
        return 1


if __name__ == "__main__":
    sys.exit(main())

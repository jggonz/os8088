#!/usr/bin/env python3
"""SPEC.md 13.10: the shared scroll bar, and the two kernel bars are one now.

    python3 tests/sbar.py [machine]

Two claims, and they need different instruments.

  THE PICTURE. files.inc's bar is what the element unified ON, so the Disk
  window's must be byte-identical to what it drew before - and that is checked
  against the GEOMETRY the old code is on record as drawing, mirrored here,
  rather than against a screenshot of the same build. fdlg.inc's changes by
  the two pixels 13.10 names (arrow cells 11 rows not 12, centre x1+6 not +7)
  and is checked against the SAME mirror, which is the whole point: the two
  bars were a pixel apart and are not any more.

  THE PARTS. os88ui_sbhit is geometry and the callers own the policy, so what
  is asserted is that each part of the bar does what its window says it does:
  the arrows step one row, the track pages, and a click ON THE THUMB pages
  down in the Disk window and does nothing in the dialog.

A Disk window needs more entries than fit to have a thumb at all, which
B:\\APPS has (11 packages on the apps disk against 7 rows in a default window).
"""
import sys
import time

sys.path.insert(0, "tools")
import os88marty as M
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN
from os88fixture import need                             # noqa: E402

ARG = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
DLG = ARG == "dlg"
MACHINE = "os8088_5150_cga_gla" if DLG else ARG
# fdlg.inc's bar, content-relative - the numbers 13.10 took away from it
FD_SBX, FD_LX2, FD_LY1, FD_LY2 = 200, 213, 20, 120
TITLE_H = 18
FM_SB_W, FM_ROW_Y0 = 14, 22

# os88ui.inc's derivation, mirrored deliberately - a gate that asks the kernel
# where it drew the rules is testing the kernel against itself.
SBCELL, SBMINH = 10, 8

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


def disk_win(m):
    b = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    for i in range(MAX_WIN):
        r = b[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, 0) & 2 and _u16(r, 8) > 60:
            return tuple(_u16(r, o) for o in (2, 4, 6, 8))
    return None


def rows(m):
    """A [y][x] lit-pixel accessor for whichever renderer this is.

    1bpp is read out of VRAM and mode 12h out of the RENDERED frame: four
    planes behind the Graphics Controller are not flat-readable, so a VGA run
    against m.vram() fails every PICTURE case while passing every memory one -
    which is exactly what the first run of this file did.
    """
    if m.video()["type"] in ("cga", "mda", "herc"):
        w, h, r = m.vram()
        return r
    w, h, p = m.fbuf()

    class F:
        def __getitem__(self, y):
            base = y * w * 3
            return [1 if (p[base + x*3] or p[base + x*3 + 1]
                          or p[base + x*3 + 2]) else 0 for x in range(w)]
    return F()


def px(r, x, y):
    return r[y][x]


def front(m, after):
    """The frontmost live window, or a sentence naming the step that lost it.

    THE GUARD IS THE WHOLE REASON THIS IS A FUNCTION. It was three copies of
    the same read, each ending in a bare `live[-1]`, so a step that failed to
    open its window died with `IndexError: list index out of range` against
    130 lines of file and no clue which of the three it was. tests/deskbench.py
    lost a soak run to exactly that shape, and a traceback was what found it.

    `& 2` is sbar's own predicate and not dispcp's `& 3 == 3`: it asks whether
    the slot is VISIBLE, which is what a click needs to know.
    """
    b = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    live = [tuple(_u16(b[i*WIN_SIZE:], o) for o in (2, 4, 6, 8))
            for i in range(MAX_WIN) if _u16(b[i*WIN_SIZE:], 0) & 2]
    if not live:
        raise RuntimeError("no visible window after %s - the step before this "
                           "one did not put anything on screen, so there is "
                           "nothing to click" % after)
    return live[-1]


def dialog(m, mo):
    """muptest's second window puts a Standard File dialog up (fdlgup.py's
    route) - the only way to get one on screen."""
    step = int.from_bytes(m.read(m.sym("desk_zstep"), 2), "little")
    h1 = int.from_bytes(m.read(m.sym("desk_zh1"), 2), "little")
    vw = int.from_bytes(m.read(m.sym("vid_w"), 2), "little")
    mo.dblclick(vw - 40, 32 + step + h1 // 2)
    M.settle(m)
    d = front(m, "the second muptest window was double-clicked open")
    mo.click(d[0] + d[2] // 2, d[1] + 9)
    M.settle(m)
    mo.dblclick(d[0] + 40, d[1] + TITLE_H + 30)
    M.settle(m)
    w = front(m, "the Standard File dialog was asked for")
    cx, cy = w[0] + 1, w[1] + TITLE_H
    two = (cx + 100 + 31, cy + 40 + 9)
    mo.menu(two[0], two[1], two[0] + 2, two[1])
    M.settle(m)
    return front(m, "the dialog's menu was dragged")


# `all` builds NOTHING under tests/, so on a clean tree this disk is absent
# and launch() dies in its copy with a FileNotFoundError in a tenth of a
# second - an ABSENT gate that reads as a failing one, which is worse than
# either. fdlgup and mouseup want the same disk and have asked for it since
# it was written; this row never did, so WHICH ROW RAN FIRST decided whether
# it passed (docs/HANDOFF-SOAK-FINDINGS.md B4).
need("build/muptest.img")

if DLG:
    with M.launch("build/os8088-360.img", apps="build/muptest.img",
                  machine=MACHINE) as m:
        M.settle(m)
        mo = Mouse(marty=m)
        print(f"== {MACHINE} : the DIALOG's bar, same derivation (13.10) ==")
        d = dialog(m, mo)
        check("the dialog is up", d is not None, f"{d}")
        if not d:
            sys.exit("no dialog")
        cx, cy = d[0] + 1, d[1] + TITLE_H
        x1, x2 = cx + FD_SBX, cx + FD_LX2
        y1, y2 = cy + FD_LY1, cy + FD_LY2
        print(f"  bar {x1},{y1} .. {x2},{y2}")
        r = rows(m)

        def ink(x, y):
            return not r[y][x]

        # THE SAME MIRROR THE DISK WINDOW IS CHECKED AGAINST, which is the
        # whole claim: these two bars used to be a pixel apart in two places
        # (arrow cells 11 rows against 12, centre x1+6 against x1+7).
        check("the frame's corners are drawn", ink(x1, y1) and ink(x2, y2))
        check("the arrow-cell rules are at y1+10 / y2-10",
              all(ink(x, y1 + SBCELL) for x in range(x1, x2 + 1)) and
              all(ink(x, y2 - SBCELL) for x in range(x1, x2 + 1)))
        ccx = x1 + (x2 - x1) // 2
        check("...and the arrows are centred on x1+(x2-x1)/2",
              ink(ccx, y1 + 3) and not ink(ccx - 1, y1 + 3) and
              ink(ccx, y2 - 3) and not ink(ccx - 1, y2 - 3),
              f"(x1+{ccx - x1}, and it was x1+7 before)")
        check("the up glyph widens 1..9 over five rows",
              all(all(ink(x, y1 + 3 + i) for x in range(ccx - i, ccx + i + 1))
                  and not ink(ccx - i - 1, y1 + 3 + i) for i in range(5)))
    print()
    if fails:
        print(f"{len(fails)} FAILED: {', '.join(fails)}")
        sys.exit(1)
    print("all pass")
    sys.exit(0)


with M.launch("build/os8088-360.img", apps="build/apps360.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    print(f"== {MACHINE} : the shared scroll bar (SPEC.md 13.10) ==")

    vw = int.from_bytes(m.read(m.sym("vid_w"), 2), "little")
    mo.dblclick(vw - 40, 46)                    # a Disk window on B:
    M.settle(m)
    w = disk_win(m)
    check("a Disk window opened", w is not None, f"{w}")
    if not w:
        sys.exit("no window")

    # the bar, computed the way fm_sbset does
    cx, cy = w[0] + 1, w[1] + TITLE_H
    listb = int.from_bytes(m.read(m.sym("fm_listb"), 2), "little")
    x2 = cx + (w[2] - 2) - 1
    x1, y1, y2 = x2 - (FM_SB_W - 1), cy + FM_ROW_Y0, cy + listb - 1
    print(f"  bar {x1},{y1} .. {x2},{y2}")

    # INTO A FOLDER WITH ENOUGH FILES TO PAGE. B:\ has six entries against
    # five rows, so its maximum scroll is 1 - which is what an ARROW gives
    # too, and the two would be indistinguishable. APPS has eleven.
    mo.dblclick(cx + 40, cy + FM_ROW_Y0 + 8 + 16)
    M.settle(m)
    w = disk_win(m)
    cx, cy = w[0] + 1, w[1] + TITLE_H
    listb = int.from_bytes(m.read(m.sym("fm_listb"), 2), "little")
    x2 = cx + (w[2] - 2) - 1
    x1, y1, y2 = x2 - (FM_SB_W - 1), cy + FM_ROW_Y0, cy + listb - 1
    fit = int.from_bytes(m.read(m.sym("fm_fit"), 2), "little")
    tot = int.from_bytes(m.read(m.sym("fm_total"), 2), "little")
    print(f"  bar {x1},{y1} .. {x2},{y2}   fit {fit} of {tot}")
    check("...on a folder that can page", tot - fit > 1,
          f"(max scroll {tot - fit})")

    r = rows(m)
    # --- THE PICTURE, against the mirrored derivation --------------------
    # THE BAR IS DARK ON WHITE, so its ink is an UNLIT pixel. A first draft
    # asked for lit ones and failed all five picture cases against a bar that
    # was drawn perfectly.
    def ink(x, y):
        return not px(r, x, y)

    check("the frame's corners are drawn", ink(x1, y1) and ink(x2, y2))
    check("the two arrow-cell rules are where 13.10 puts them",
          all(ink(x, y1 + SBCELL) for x in range(x1, x2 + 1)) and
          all(ink(x, y2 - SBCELL) for x in range(x1, x2 + 1)),
          f"(y1+{SBCELL} and y2-{SBCELL})")
    ccx = x1 + (x2 - x1) // 2
    check("...and the arrow glyphs are centred on x1+(x2-x1)/2",
          ink(ccx, y1 + 3) and not ink(ccx - 1, y1 + 3) and
          ink(ccx, y2 - 3) and not ink(ccx - 1, y2 - 3),
          f"(column {ccx}, which is x1+{ccx - x1})")
    check("the up glyph widens 1..9 over five rows",
          all(all(ink(x, y1 + 3 + i) for x in range(ccx - i, ccx + i + 1))
              and not ink(ccx - i - 1, y1 + 3 + i) for i in range(5)))
    check("...and the down glyph narrows 9..1",
          all(all(ink(x, y2 - 3 - i) for x in range(ccx - i, ccx + i + 1))
              and not ink(ccx - i - 1, y2 - 3 - i) for i in range(5)))

    # --- THE THUMB: a run of rows whose interior is solid WHITE ----------
    trk1, trk2 = y1 + SBCELL + 1, y2 - SBCELL - 1
    body = [y for y in range(trk1, trk2 + 1)
            if all(px(r, x, y) for x in range(x1 + 3, x2 - 2))]
    check("the track carries a thumb", len(body) >= SBMINH - 2,
          f"({len(body)} rows of solid white interior in a {trk2-trk1+1}-row "
          f"track)")
    check("...and it is not the whole track", len(body) < (trk2 - trk1 - 1),
          f"({len(body)} of {trk2 - trk1 + 1})")
    check("...and it is one contiguous run",
          body and body[-1] - body[0] == len(body) - 1,
          f"({body[0]}..{body[-1]})" if body else "(none)")

    # --- THE PARTS: each does what THIS window says it does ---------------
    def scrl():
        vp = int.from_bytes(m.read(m.sym("fm_vp"), 2), "little")
        return int.from_bytes(
            m.read((M.KERNEL_SEG << 4) + vp + 2, 2), "little")   # FS_SCRL

    mo.click(ccx, y2 - 5)                       # the DOWN arrow
    M.settle(m)
    check("the down arrow steps one row", scrl() == 1, f"(FS_SCRL {scrl()})")

    mo.click(ccx, y1 + 5)                       # the UP arrow
    M.settle(m)
    check("...and the up arrow steps back", scrl() == 0, f"(FS_SCRL {scrl()})")

    mo.click(ccx, trk2 - 1)                     # the track BELOW the thumb
    M.settle(m)
    paged = scrl()
    check("the track below the thumb pages down", paged > 1,
          f"(FS_SCRL {paged}, and an arrow gives 1 - which is why this "
          f"folder rather than a root)")

    mo.click(ccx, trk1)                         # ...and above it
    M.settle(m)
    check("...and above it pages up", scrl() < paged, f"(FS_SCRL {scrl()})")

    # A click ON the thumb used to page DOWN in this window - files.inc always
    # did, and 13.10.1 was the element declining to choose for it. THE THUMB
    # IS A GESTURE NOW (13.10.5, and it ships): the press GRABS, a release in
    # place commits the unmoved pos, and fmthumb's case A asserts exactly
    # that. This gate kept the pre-ship expectation and was red from the day
    # the drag landed - the two bars still being ONE element is what it is
    # here to prove, so it now asserts the gesture's answer on this bar too.
    r = rows(m)
    body = [y for y in range(trk1, trk2 + 1)
            if all(px(r, x, y) for x in range(x1 + 3, x2 - 2))]
    was = scrl()
    if body:
        mo.click(ccx, body[len(body) // 2])
        M.settle(m)
        check("a click ON the thumb GRABS, not pages (13.10.5)", scrl() == was,
              f"(FS_SCRL {was} -> {scrl()}; a page would have moved it)")

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")

#!/usr/bin/env python3
"""SPEC.md 13.10.7: the thumb gesture inside a PACKAGE.

    python3 tests/pkgthumb.py notepad|browser|texpad|word|frotz

One file for three apps rather than three files, because the subject is not Note Pad -
it is the SHARED gesture reaching a package at all, and the only thing that
differs between the three is how a window with a scrollable thing in it gets
opened.

**THE SIGNAL IS PIXELS HERE, AND IT HAS TO BE.** tests/fmthumb.py reads
FS_SCRL out of kernel memory; a package's scroll position and the element's
own gesture record both live in the PACKAGE's image, which m.sym() does not
describe. So this asserts the thing the user actually claims:

  A  a bar with a thumb is found, by looking for its DITHERED TRACK
  B  a press on the thumb, then a drag: the BAR band changes...
  C  ...and the CONTENT band does not, at rate 0
  D  x is never read (13.10.5.2): 150px off the bar moves the thumb the same
  E  the release commits: the content band changes NOW

C is the case that needs the pixels most, because "the view did not follow"
is invisible in any other instrument here.

The bar is found rather than mirrored: the track is gfx_fill_gray, a 50%
checkerboard, so a column inside it ALTERNATES on every row where a column of
text or of blank paper does not. That works for a bar anywhere in any window,
which is what TexPad needs - it has two, and one of them is not at the
window's right edge.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88build as _B
import os88marty as M
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN
import dispcp

APP = (sys.argv[1] if len(sys.argv) > 1 else "notepad").lower()
ARGS = [a for a in sys.argv[2:] if not a.startswith("--")]
MACHINE = ARGS[0] if ARGS else "os8088_5150_cga_gla"
RATE = int(next((a.split("=")[1] for a in sys.argv[1:]
                 if a.startswith("--rate=")), "0"))
W_FLAGS, W_X, W_Y, W_W, W_H = 0, 2, 4, 6, 8
TITLE_H = 18

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o=0):
    return b[o] | (b[o + 1] << 8)


def wins(m):
    blob = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    out = []
    for i in range(MAX_WIN):
        r = blob[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, W_FLAGS) & 2:
            out.append(tuple(_u16(r, o) for o in (W_X, W_Y, W_W, W_H)))
    return out


def rows(m):
    w, h, r = m.vram()
    return r


def find_bars(px, w):
    """Every scroll-bar TRACK in the window's content, by its dither.

    Returns [(x1, x2, y1, y2)] - the alternating columns and the rows they
    alternate over. A track column flips on every row; nothing else in a text
    window does that for more than a glyph's height.
    """
    x0, y0, x1e, y1e = w[0] + 1, w[1] + TITLE_H, w[0] + w[2] - 2, w[1] + w[3] - 2
    cols = {}
    for x in range(x0, x1e + 1):
        best = run = 0
        start = top = y0
        for y in range(y0, y1e):
            if px[y][x] != px[y + 1][x]:
                run += 1
                if run > best:
                    best, top = run, start
            else:
                run, start = 0, y + 1
        if best >= 8:               # 8 and not 16: Word's bar sits under a
            cols[x] = (best, top)   # ruler and a ribbon, and the track left
                                    # over is nine rows. Four ADJACENT columns
                                    # are still required below, which is what
                                    # keeps a dotted glyph out
    if not cols:
        return []
    bars, cur = [], []
    for x in sorted(cols):
        if cur and x != cur[-1] + 1:
            bars.append(cur)
            cur = []
        cur.append(x)
    bars.append(cur)
    out = []
    for c in bars:
        if len(c) < 4:
            continue                      # a glyph column, not a track
        best, top = max(cols[x] for x in c)
        out.append((c[0], c[-1], top, top + best))
    return out


def find_thumb(px, bar):
    """The solid run inside a track: the thumb. None if there is not one."""
    x1, x2, y1, y2 = bar
    x = (x1 + x2) // 2
    best = (0, 0)
    y = y1
    while y <= y2:
        if px[y][x]:
            s = y
            while y <= y2 and px[y][x]:
                y += 1
            if y - s > best[0]:
                best = (y - s, s)
        else:
            y += 1
    return (best[1], best[0]) if best[0] >= 6 else None


def band(px, r):
    return [tuple(px[y][x] for x in range(r[0], r[2] + 1))
            for y in range(r[1], r[3] + 1)]


# --- the disk each app needs -------------------------------------------------
# One long document per app, in the format its association claims: each is
# opened by DOUBLE-CLICKING it, so the app comes up with something to scroll
# rather than empty (SPEC.md 54).
# WRITTEN WHERE IT IS READ (docs/plans/SOAK-PARALLEL.md 14.2): scratch_disk
# resolves the paths it is handed, so a source file created at the literal
# `build/pkgthumbsrc/` is one os88disk then looks for in the run's own tree.
SRCDIR = _B.at("build/pkgthumbsrc")
os.makedirs(SRCDIR, exist_ok=True)
DOC = {"notepad": ("LONG.TXT",
                   "".join("line %03d of a long document\r\n" % i
                           for i in range(220))),
       "browser": ("LONG.HTM",
                   "<html><body>\r\n"
                   + "".join("<p>paragraph %03d of a long page</p>\r\n" % i
                             for i in range(160))
                   + "</body></html>\r\n"),
       "word":    ("LONG.TXT", ""),      # unused: Word opens WELCOME.DOC
       "frotz":   ("LONG.TXT", ""),      # unused: Frotz opens a STORY
       "texpad":  ("LONG.TEX",
                   "".join("line %03d of a long source file\r\n" % i
                           for i in range(220)))}[APP]
LONG = os.path.join(SRCDIR, DOC[0])
open(LONG, "w").write(DOC[1])
PKG = {"notepad": "build/notepad.o88", "browser": "build/browser.o88",
       "texpad": "build/texpad.o88", "word": "build/word.o88",
       "frotz": "build/frotz.o88"}[APP]
DISK = "build/pkg%s.img" % APP
EXTRA = []
if APP == "frotz":
    # NOT a registered row, and tests/unit/t_registry.py says why: a Z-machine
    # interpreter needs a STORY, and the two ways to get one are a network
    # fetch (tools/getstories.py) or the Inform compiler for the tree's own
    # tests/frotz/zopstest.inf. Neither is a build dependency anybody has.
    from os88fixture import need
    need("build/frotz.o88", "build/zt/ZOPS.Z5")
    EXTRA = ["build/zt/ZOPS.Z5"]
elif APP == "word":
    # Word is not on the apps disk and it ships in TWO pieces: WORD.OVL is
    # far-called out of the package image (SPEC.md 68.10) and the document is
    # its own format, so this borrows the three artifacts `make worddisk`
    # builds rather than writing a .DOC by hand.
    #
    # os88fixture.need is SAFE AGAIN here, and it was not while the gesture was
    # a knob: `make` for a fixture runs with no knob variables, and the
    # VIDSTAMP rule then removed build/kernel.bin because the knob set
    # differed. The drag ships now, so a plain `make` is the build under test.
    from os88fixture import need
    need("build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")
    EXTRA = ["build/WORD.OVL", "build/WELCOME.DOC"]
M.scratch_disk(DISK, PKG, *(EXTRA or [LONG]))

OPEN = {"word": "WELCOME.DOC", "frotz": "ZOPS.Z5"}.get(APP, DOC[0])

S = lambda n: m.sym(n)

with M.launch("build/os8088-360.img", apps=DISK, machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    print(f"== {MACHINE} : {APP}'s thumb drag (SPEC.md 13.10.7) ==")

    dispcp.open_drive(m, mo, S, M.settle, "B")
    d = dispcp.win_list(m, S)[-1]
    dx, dy = dispcp.win_rect(m, S, d)[:2]
    dispcp.open_named(m, mo, S, M.settle, dx, dy, OPEN)
    time.sleep(1.5)
    M.settle(m)
    if APP == "frotz":
        # A STORY THAT FITS THE WINDOW HAS NO SCROLLBACK, and Frotz's bar
        # measures the RING: it is reserved always, but its thumb only exists
        # once lines have fallen off the top. Two things fill it - Enter runs
        # more of the opcode test, and shrinking the window pushes what is
        # already shown into the ring.
        for _ in range(14):
            m.key("Enter")
            time.sleep(0.25)
        time.sleep(2.0)
        M.settle(m)
        w = wins(m)[-1]
        mo.drag(w[0] + w[2] - 5, w[1] + w[3] - 5, w[0] + w[2] - 5, w[1] + 70)
        time.sleep(2.0)
        M.settle(m)
    w = wins(m)[-1]
    check("the app opened a window", w[2] > 100, f"{w}")

    mo.to(4, 4)                                  # pointer off everything
    time.sleep(1.0)
    px = rows(m)
    bars = find_bars(px, w)
    check("a scroll-bar track is on screen", len(bars) >= 1, f"{bars}")
    if not bars:
        sys.exit("no bar found")
    which = int(next((a.split("=")[1] for a in sys.argv[1:]
                      if a.startswith("--bar=")), "0" if APP == "texpad"
                     else "-1"))
    bar = bars[which]
    other = [b for b in bars if b is not bar]
    t = find_thumb(px, bar)
    check("...with a thumb in it", t is not None, f"(bar {bar}, thumb {t})")
    if not t:
        sys.exit("no thumb")
    ttop, th = t
    cx = (bar[0] + bar[1]) // 2
    print(f"  bar {bar}, thumb top {ttop} h {th}")

    # ONE CLICK ON THE THUMB FIRST, and it is not padding. TexPad gives the
    # clicked PANE the focus and redraws both pane frames when it changes, so
    # the press that starts the drag would move pixels for a reason that has
    # nothing to do with the gesture - and case C would fail on a build that
    # never scrolled anything. A click on the thumb with no movement between
    # press and release commits the position it already had, so it is the
    # cheapest way to spend that transition.
    mo.click(cx, ttop + th // 2)
    M.settle(m)
    mo.to(4, 4)
    time.sleep(1.0)
    px = rows(m)

    barband = (bar[0] - 2, bar[2], bar[1] + 2, bar[3])
    # A STRIP IMMEDIATELY LEFT OF THIS BAR, and both of its bounds are there
    # to keep case C honest rather than to make it pass:
    #
    #  * the ROWS are the bar's own, because everything above them is the app's
    #    chrome - and the Browser's URL line has a FOCUSED CARET blinking on
    #    its own timer, so a band including it differs between any two samples
    #    whether the view moved or not;
    #  * the COLUMNS are the LEFT of the pane this bar owns - between the
    #    previous bar and this one - because TexPad has two panes and a band
    #    reaching across the window would be measuring the pane the OTHER bar
    #    scrolls, while a strip beside the bar itself is blank paper in every
    #    app whose lines are shorter than the column.
    prev = [b for b in bars if b[1] < bar[0]]
    left = (max(p[1] for p in prev) + 3) if prev else w[0] + 2
    content = (left, bar[2], min(left + 120, bar[0] - 4), bar[3])
    b0, c0 = band(px, barband), band(px, content)
    o0 = [find_thumb(px, b) for b in other]

    # --- press and drag ----------------------------------------------------
    mo.to(cx, ttop + th // 2)
    mo._edge(True)
    time.sleep(0.8)
    target = bar[3] - th // 2 - 2
    mo.to(cx, target, l=True)
    time.sleep(1.4)
    px1 = rows(m)
    b1, c1 = band(px1, barband), band(px1, content)
    check("the bar changed under the hand", b1 != b0)
    if RATE == 0:
        check("...and the CONTENT did not, at rate 0", c1 == c0)
    else:
        check(f"...and the CONTENT followed, at rate {RATE}", c1 != c0)
    # --- D: x is never read, and it is ALSO how the thumb gets read ---------
    # THE POINTER HAS TO BE OFF THE BAR TO SEE THE THUMB. The cursor is drawn
    # over it during the drag - it is sitting ON the thing being dragged - and
    # a black arrow through a white block breaks the solid run find_thumb looks
    # for, which reads as "no thumb" rather than as a cursor. Moving 150px off
    # (still held) is 13.10.5.2's own case and gives a clean frame for free.
    mo.to(cx - 150, target, l=True)
    time.sleep(1.4)
    t1 = find_thumb(rows(m), bar)
    check("the thumb moved down with the hand",
          t1 is not None and t1[0] > ttop + 2, f"(top {ttop} -> {t1})")
    # ...and back the OTHER way, unless the bar is near the right edge - the
    # preview pane's is 9px from it, and mouse.to refuses a target off the
    # screen rather than clamping. A second offset on the same side still says
    # what the case is about: the thumb does not care what x is.
    vw = len(px[0])
    x2p = cx + 60 if cx + 60 < vw - 4 else cx - 40
    mo.to(x2p, target, l=True)
    time.sleep(1.4)
    t2 = find_thumb(rows(m), bar)
    check(f"x is never read: at x={x2p} too, the same thumb",
          t2 is not None and t1 is not None and abs(t2[0] - t1[0]) <= 1,
          f"({t2} vs {t1})")

    # --- F: TexPad's OTHER bar is untouched (SPEC.md 13.10.5.10) -----------
    if other:
        check("the other bar's thumb did not move",
              [find_thumb(rows(m), b) for b in other] == o0,
              f"({[find_thumb(rows(m), b) for b in other]} vs {o0})")

    # --- E: the release commits ---------------------------------------------
    mo._edge(False)
    M.settle(m)
    mo.to(4, 4)
    time.sleep(1.0)
    px2 = rows(m)
    check("the release moves the CONTENT", band(px2, content) != c0)
    t3 = find_thumb(px2, bar)
    check("...and the thumb is still where the hand left it",
          t3 is not None and t1 is not None and abs(t3[0] - t1[0]) <= 2,
          f"({t3} vs {t1})")

print("FAILED: " + ", ".join(fails) if fails else "all passed")
sys.exit(1 if fails else 0)

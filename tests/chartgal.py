#!/usr/bin/env python3
"""The Gallery actually DRAWS seven different charts (SPEC.md 82.4, 82.6)

    make && python3 tests/chartgal.py

THE FIRST CHART TEST THAT LOOKS AT PIXELS. `chartname` and `chartread` both
read numbers out of the package's bss, and **no amount of that can see the
drawing**: `ch_type` dispatches Area, Bar, Column, Line, Pie, Scatter and
Combination through `ch_draw`, and a dispatch that silently did nothing would
leave `ct_valcnt`, `ct_mincol` and `ct_wvals` all perfectly correct. That is
the defect class SPEC.md 82.15 and 82.18.2 are both instances of - a feature
asserted in words with nothing driving it - and the whole drawing half of
this package was in it.

IT READS THE CANVAS, NOT THE SCREEN. §82.1's offscreen buffer is 240x160 at
4bpp, CH_STRIDE 120, pixels from CH_PXOFF - the buffer BOTH halves draw into,
which is the thing under test and is also what `File > Export as BMP...`
writes. Reading it out of `ct_chartseg` needs no export dialog, no floppy to
extract from and no screen geometry, and it is the same pixels either way.

  1 INK        every type drew something. A type that cleared the canvas and
               returned would pass every bss check ever written
  2 DISTINCT   ...AND NO TWO TYPES DREW THE SAME PICTURE. This is the check:
               a ct_gal_map that sent two items to one CH_T_, or a ch_draw
               arm that fell through to its neighbour, leaves every number in
               the package right and two entries of a seven-entry menu doing
               the same thing
  3 ORIENTED   Column is vertical and Bar is horizontal - Excel's naming, the
               opposite of the intuitive one (82.4) - so the two must be
               TRANSPOSED, not merely different
  4 FILLED     Area is Line with each column filled down to the axis (82.4,
               the same walk), so Area must carry strictly more ink
  5 ROUND      Pie's ink is a DISC (82.6 sweeps filled radii from a centre):
               the centre of its own bounding box is inked and all four
               CORNERS of that box are not, which no rectangle chart manages

TWO METRICS HERE WERE WRONG THE FIRST TIME and the canvas said so, which is
worth leaving written down. "How many columns and how many rows carry any
ink" separates nothing: a column chart's tallest bar reaches the top of the
canvas so every row has ink, and the axis labels (82.7, colour 0) spread ink
across the top so every column does too - both charts measured 240x160. The
RUN LENGTHS below are the real transposition. And the pie is an ELLIPSE,
94x70 on this data and not a circle, so a near-square test on its bounding
box fails on a correct picture where "centre inked, corners not" does not.
"""
import hashlib
import os
import subprocess
import time
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88fixture                                           # noqa: E402
import os88ui                                                # noqa: E402
import os88mouse                                             # noqa: E402
import os88geom                                              # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
from harness import check, done                              # noqa: E402
from paintmove import pkg_syms                               # noqa: E402

DISK = "build/chartgal.img"
DOC = "GAL.SLK"
# Two columns, because Scatter and Combination need a second series (82.8).
COLA = [10.0, 20.0, 30.0, 40.0]
COLB = [7.0, 3.0, 9.0, 1.0]
# chart.asm's own, mirrored - plain `equ`s are not in the symbol map
CH_W, CH_H, CH_STRIDE, CH_PXOFF = 240, 160, 120, 118
TYPES = ["Area", "Bar", "Column", "Line", "Pie", "Scatter", "Combination"]


def build_disk():
    os88fixture.need("build/chart.o88", "build/os8088-360.img")
    cs = {}
    for r, (a, b) in enumerate(zip(COLA, COLB)):
        cs[(r, 0)] = a
        cs[(r, 1)] = b
    open(os.path.join(ROOT, "build", DOC), "wb").write(F.write_sylk(cs))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "build/chart.o88",
                    os.path.join("build", DOC)],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)


def unpack(raw):
    """CH_H rows of CH_STRIDE packed 4bpp bytes -> rows of CH_W indices.

    High nibble is the LEFT pixel (82.1), and the rows are top-down in the
    buffer - BMP's bottom-up order is ch_bmp_write's doing and happens at
    export, not here.
    """
    rows = []
    for y in range(CH_H):
        line = raw[y * CH_STRIDE:(y + 1) * CH_STRIDE]
        px = []
        for b in line:
            px.append(b >> 4)
            px.append(b & 0x0F)
        rows.append(px[:CH_W])
    return rows


GROUND, BARCOL = 15, 1                  # ch_prep clears to white; the bars
                                        # and Line/Area's ink are colour 1,
                                        # and the axis labels are colour 0


def ink(rows, only=None):
    """Which pixels are not the ground - or, with `only`, one colour of them.

    `only` matters for the orientation check: the labels are colour 0 and
    would otherwise count as part of a bar.
    """
    if only is None:
        return [[1 if p != GROUND else 0 for p in r] for r in rows]
    return [[1 if p == only else 0 for p in r] for r in rows]


def longest(msk):
    """(longest horizontal run, longest vertical run) of set pixels.

    THIS is the transposition and the marginals are not - see the header.
    A vertical bar's longest vertical run is its height and its longest
    horizontal run is its width, whatever else is on the canvas.
    """
    h = v = 0
    for y in range(CH_H):
        run = 0
        for x in range(CH_W):
            run = run + 1 if msk[y][x] else 0
            h = max(h, run)
    for x in range(CH_W):
        run = 0
        for y in range(CH_H):
            run = run + 1 if msk[y][x] else 0
            v = max(v, run)
    return h, v


def box(msk):
    xs = [x for x in range(CH_W) for y in range(CH_H) if msk[y][x]]
    ys = [y for y in range(CH_H) for x in range(CH_W) if msk[y][x]]
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def main():
    os.chdir(ROOT)
    build_disk()
    S = pkg_syms("apps/chart/chart.asm")
    u16 = lambda b: b[0] | (b[1] << 8)

    with os88ui.boot(os.path.join(ROOT, "build", "os8088-360.img"),
                     apps=DISK, machine="os8088_xt_vga") as ui:
        m = ui.m
        mo = os88mouse.Mouse(marty=m)
        w = ui.path("B:/CHART.O88")
        seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym) + os88geom.W_SEG, 2))
        ui.settle()                     # ...and not before: without it the
                                        # first menu_pick raced the Disk
                                        # window still being in front, and
                                        # picked ITS File menu ('Open', 'New
                                        # Folder...', 'Compress'...)
        ui.menu_pick("File", "Open...")
        ui.chooser_open(DOC)
        ui.settle()

        canvas = u16(m.readseg(seg, S["ct_chartseg"], 2))
        if not canvas:
            sys.exit("chartgal: ct_chartseg is 0 - no canvas was claimed")

        # A Gallery pick REDRAWS THE WHOLE 240x160 4bpp canvas and then
        # repaints the window, and Pie's fill sweeps radii at half-degree
        # steps (82.6) - on a 4.77MHz 8088 that outruns menu_pick's default
        # 8 guest seconds, and the run died on the Pie with the bar not
        # responding. ui.settle() does not cover it either: it watches for
        # the picture to stop changing, and a canvas being computed is not
        # changing yet.
        def screen():
            """A hash of the rendered frame - the cheapest "has the window
            been painted yet" there is."""
            wpx, hpx, data = m.fbuf()
            return hashlib.md5(bytes(data)).digest()

        def grab():
            """One read of the whole canvas, with the guest STOPPED.

            19,200 bytes is not one round trip, so a running guest can redraw
            underneath a read and hand back a frame that never existed.
            """
            m.pause()
            try:
                return bytes(m.readseg(canvas, CH_PXOFF, CH_STRIDE * CH_H))
            finally:
                m.run()

        def stable(baseline):
            """The canvas after a redraw: wait for it to CHANGE, then settle.

            ui.settle() is not enough on its own and the difference is this
            row's whole reliability: settle watches the SCREEN stop changing,
            and a canvas still being computed has not reached the screen -
            the frame is already still, so settle returns before the drawing
            even starts. Reading on that gave a half-swept pie, under a
            mutation that cannot touch Pie at all.

            Waiting for a CHANGE first is what removes that: it proves the
            redraw has begun before we start asking whether it has finished.
            That wait is bounded and falling out of it is not an error - two
            types CAN draw the same picture, and when they do it is check 2's
            business to say so, not this helper's to hang on.

            AN EARLIER VERSION TOOK THE FIRST PICTURE SEEN TWICE instead of
            settling, and tolerated never seeing one. That made BOTH of the
            checks that compare pictures unreliable, and in opposite
            directions: check 5 failed on a pie read mid-sweep (centre
            blank), and check 2 - loosened to a pixel threshold so the
            half-swept pie would not forge a match - then MISSED the
            ct_gal_map mutation on one run, because the two identical charts
            had been read at different moments and drifted past the
            threshold. A gate that intermittently fails wastes an afternoon;
            one that intermittently MISSES is worse than not existing.
            """
            for _ in range(60):             # ...it has started
                cur = grab()
                if baseline is None or cur != baseline:
                    break
                time.sleep(0.25)
            # ...AND THEN THE BLIT HAS HAPPENED. This is the part two
            # earlier versions got wrong, and the reason is 82.1's whole
            # point: THE CANVAS IS RASTERISED OFFSCREEN, so while the render
            # runs the SCREEN DOES NOT CHANGE. settle() therefore returns
            # immediately - mid-render - and so does settle() placed after a
            # canvas-change test, which is what read a pie whose bounding box
            # grew 94 -> 113 -> 133 px wide across runs with its centre still
            # blank. The screen carries no signal DURING an offscreen render.
            #
            # What it does carry is the END of one: the finished canvas is
            # blitted to the window, and that changes the screen. So wait for
            # the NEXT screen change after the canvas started moving - the
            # menu's own save-under restore is long since done by then - and
            # only then let it settle.
            before = screen()
            for _ in range(240):
                if screen() != before:
                    break
                time.sleep(0.25)
            ui.settle(quiet=1.0, stable=3)
            return grab(), True
            # ...and it has finished: THE FIRST PICTURE SEEN TWICE, which is
            # not the same as two reads running agreeing. Chart re-renders
            # the canvas on a repaint and a paused read lands wherever the
            # redraw had got to, so an in-progress frame is never seen twice
            # while the finished one is seen every time the guest is idle.
            # Demanding two CONSECUTIVE matches made this row intermittent -
            # it passed alone, failed in the suite beside two other emulators
            # and then passed again, with 60 of 19,200 bytes moving each
            # time. Counting repeats instead needs the idle state to come up
            # twice in sixty tries and does not care what lands between.

        mo.to(620, 470)                 # the pointer OFF the window before
                                        # any of this: it is parked once and
                                        # never moved again, so nothing the
                                        # mouse does can be mistaken for the
                                        # renderer
        shots, raws, last = {}, {}, grab()
        for t in TYPES:
            ui.menu_pick("Gallery", t, limit=60.0)
            mo.to(620, 470)             # the pointer stays parked, so nothing
            last, _ = stable(last)      # the mouse does looks like the render
            raws[t] = unpack(last)
            shots[t] = ink(raws[t])

        counts = dict((t, sum(map(sum, msk))) for t, msk in shots.items())
        blank = [t for t in TYPES if counts[t] == 0]
        check(not blank, "1 INK",
              "every Gallery type drew something into the canvas. A type "
              "that cleared it and returned passes every check the other two "
              "chart rows can make, because they read numbers",
              got="ink per type %r" % (counts,),
              want="all seven non-zero")

        same = []                       # EXACT, not a threshold: see stable()
        for i, a in enumerate(TYPES):
            for b in TYPES[i + 1:]:
                if shots[a] == shots[b]:
                    same.append("%s==%s" % (a, b))
        check(not same, "2 DISTINCT",
              "no two types drew the SAME picture. ct_gal_map is indexed by "
              "the menu item and 82.4 says to keep the two in step; a map "
              "with a repeated entry, or a ch_draw arm falling through to "
              "its neighbour, leaves every number in the package correct "
              "and two menu items doing one thing",
              got=("identical pairs: %r" % same) if same
                  else "seven distinct pictures",
              want="no two alike")

        ch, cv = longest(ink(raws["Column"], only=BARCOL))
        bh, bv = longest(ink(raws["Bar"], only=BARCOL))
        check(cv > ch and bh > bv, "3 ORIENTED",
              "Column is the VERTICAL chart and Bar the horizontal one - "
              "Excel's naming, which is the opposite of the intuitive one "
              "(82.4). Four vertical bars span many rows and few columns; "
              "four horizontal ones span many columns and few rows. Merely "
              "'different', which check 2 asks, would not tell a Bar drawn "
              "vertically from a correct one",
              got="Column runs h=%d v=%d, Bar runs h=%d v=%d"
                  % (ch, cv, bh, bv),
              want="Column v>h, Bar h>v")

        check(counts["Area"] > counts["Line"], "4 FILLED",
              "Area IS Line with each column filled down to the axis - the "
              "same ch_seg_draw walk (82.4) - so it must carry strictly "
              "more ink. Equal ink would mean the fill never ran",
              got="Area %d, Line %d" % (counts["Area"], counts["Line"]),
              want="Area > Line")

        bx = box(shots["Pie"])
        x1, y1, x2, y2 = bx
        wpx, hpx = x2 - x1 + 1, y2 - y1 + 1
        cx, cy = (x1 + x2) // 2, (y1 + y2) // 2
        pie = shots["Pie"]
        corners = [pie[y][x] for y in (y1, y2) for x in (x1, x2)]
        check(pie[cy][cx] == 1 and not any(corners), "5 ROUND",
              "Pie's ink is a DISC: 82.6 sweeps filled radii from a centre "
              "at half-degree steps, so the centre of its own bounding box "
              "is inked and all four CORNERS of that box are not. A "
              "rectangle chart fills its corners. Deliberately NOT a "
              "near-square test - the pie is an ellipse, %dx%d here, and a "
              "circle test fails on a correct picture" % (wpx, hpx),
              got="box %dx%d at (%d,%d), centre %s, corners %r"
                  % (wpx, hpx, x1, y1,
                     "inked" if pie[cy][cx] else "blank", corners),
              want="centre inked, four corners blank")
    done("chartgal")


if __name__ == "__main__":
    sys.exit(main())

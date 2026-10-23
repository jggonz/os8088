#!/usr/bin/env python3
"""SHEET's window RESIZES, and its grid re-lays itself to the new box
   (SPEC.md 81.112, 11.1)

    make && python3 tests/sheetsize.py

81.112 sets WF_SIZABLE and changes nothing else about the layout, on the
claim that SHEET never held a size: `sh_geom` asks OSAPI_WM_CONTENT and
OSAPI_WM_GEOM on every call, `sh_paint` calls `sh_geom` on every repaint, and
the column and row walks then fill slots until the pixels run out (81.56,
81.60). A claim like that is exactly the sort that is true of the code and
false of the screen, so this drags the grow box and reads both.

  1 SIZABLE   the window record carries WF_SIZABLE at all
  2 SHRUNK    dragging the grow box in moves W_W/W_H, and sh_cw/sh_ch with
              them - the package's own idea of its box followed the kernel's
  3 FEWER     ...and sh_vcols/sh_vrows FELL. This is the claim: the walks
              re-ran against the new content rect rather than against a
              number from the template
  4 INSIDE    the status strip's top rule is solid ink all the way to the
              NEW content right edge, and is no longer a rule at the old one.
              11.94.5.1 is the reason this leg exists: eleven of
              thirteen templates in this tree size their layout from the
              width they ASKED for, so a shrunk window draws that layout over
              its own right border - and every count in check 3 can be right
              while that happens, because the counts are the package's
              numbers and this is the glass
  5 FLOOR     ask for the smallest window the glass allows, and SHEET still
              holds its five strips plus at least one column and one row.
              81.112 declares that floor with OSAPI_WM_MINSIZE, because
              WMIN_W/WMIN_H are 96x64 for the whole machine - where this
              window's content is 45px tall, the grid clamps to nothing and
              the horizontal bar is computed inside the formula bar. The leg
              asks for the CONSEQUENCE and not for the two numbers, so it
              cannot go stale against them
  6 GREW      drag the corner back to where it started and the counts come
              back. A one-way clamp passes 2-5: it shrinks and stays shrunk

THE CONTROL is `OSAPI_WM_SIZABLE` with AL=0: 1, 2, 3 and 4 all fail and 5
passes, because a window that never resizes ends with the counts it started
with. That run is also what caught this file packing a STALE build/sheet.o88 -
it passed all five legs until `os88fixture.need` went in below.

The grow box itself is checked by tests/sheetmove.py's 4a, on the raise that
used to erase it.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88fixture                                           # noqa: E402
import os88marty as M                                        # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
import os88geom                                              # noqa: E402
import os88sym                                               # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
from paintmove import pkg_syms                               # noqa: E402

WORK = "build/sheetsize"
DISK = "build/sheetsize.img"
NAME = "SIZE.SLK"
SB_H = 16                               # sheet.asm: SH_SB_H


def main():
    os.chdir(os.path.join(HERE, ".."))
    os.makedirs(WORK, exist_ok=True)
    # MAKE THE PACKAGE FIRST. This row packs its own floppy out of
    # build/sheet.o88 rather than naming a Makefile target, so without this
    # it tests whatever was last built - which it did: the control below
    # (OSAPI_WM_SIZABLE with AL=0) passed all five legs against a stale
    # .o88, exactly the vacuous pass os88fixture.need exists to stop.
    os88fixture.need("build/sheet.o88", "build/CHART.OVL",
                     "build/MACRO.OVL", SF.SYS)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk({(r, c): float(r * 10 + c)
                                        for r in range(8) for c in range(5)}))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL", src], check=True,
                   stdout=subprocess.DEVNULL)
    S = os88sym.linear
    sym = pkg_syms("apps/sheet/sheet.asm")
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        ds = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, ds)
        dispcp.open_named(m, mo, S, M.settle, wx, wy, name=NAME)
        M.settle(m, limit=240)

        sl = dispcp.win_list(m, S, check=False)[-1]
        seg = m.read(S("wm_wins") + sl * os88geom.WIN_SIZE
                     + os88geom.W_SEG, 2)
        seg = seg[0] | (seg[1] << 8)

        def word(n):
            v = m.readseg(seg, sym[n], 2)
            return v[0] | (v[1] << 8)

        def win():
            return [w for w in os88geom.windows(m, S) if w.i == sl][0]

        def state(tag):
            """The package's four geometry words and the two walk counts,
            beside the kernel's rect. sh_geom writes all six."""
            w = win()
            st = dict(rect=(w.x, w.y, w.w, w.h),
                      ox=word("sh_ox"), oy=word("sh_oy"),
                      cw=word("sh_cw"), ch=word("sh_ch"),
                      cols=word("sh_vcols"), rows=word("sh_vrows"))
            print("   %-6s rect %r content %r %dx%d slots"
                  % (tag, st["rect"], (st["ox"], st["oy"], st["cw"], st["ch"]),
                     st["cols"], st["rows"]))
            return st

        def park():
            mo.to(4, 4)
            M.settle(m)

        def rule(st, upto=None):
            """The status strip's top rule, as (solid, run_end).

            `solid` is whether every pixel of it from the content's left to
            `upto` (default this state's own right edge) is ink: the rule is
            one GFX_HLINE across the content, so where it reaches is where
            SHEET believes its right edge is. `run_end` is where the ink
            actually stops, for the printout - it lands TWO past the content
            edge in every state, the window's own right border and the column
            beyond it, which is why this leg asks about a span and not about
            a tolerance on one number."""
            fw, _, rgb = m.fbuf()
            y = st["oy"] + st["ch"] - SB_H
            right = st["ox"] + st["cw"] - 1 if upto is None else upto
            ink = lambda x: rgb[(y * fw + x) * 3] < 128
            solid = all(ink(x) for x in range(st["ox"] + 1, right + 1))
            x = st["ox"] + 4
            while x + 1 < fw and ink(x + 1):
                x += 1
            return solid, x

        park()
        a = state("before")
        a_solid, a_end = rule(a)
        w0 = win()
        check(bool(w0.flags & os88geom.WF_SIZABLE), "1 SIZABLE",
              "OSAPI_WM_SIZABLE AL=1 after WM_CREATE: without the bit "
              "wm_hit never reports AL=4 and the drag below is a no-op on "
              "the desktop",
              got="W_FLAGS %04x" % w0.flags, want="WF_SIZABLE set")

        # IN by 160x24 from the grow box's middle - wm_grow_rect is
        # (W_X+W_W-14, W_Y+W_H-14)..(-2,-2), so -8 is its centre
        gx, gy = w0.x + w0.w - 8, w0.y + w0.h - 8
        mo.drag(gx, gy, gx - 160, gy - 24)
        M.settle(m, limit=240)
        park()
        b = state("shrunk")
        b_solid, b_end = rule(b)
        # ...and the twenty pixels ending where the rule USED to stop. They
        # are outside the shrunk window now, so a solid run there is the rule
        # still being drawn at the width SHEET asked for. Twenty and not one,
        # because the desktop's hatch is a dither and any single pixel of it
        # may legitimately be ink
        fw, _, rgb = m.fbuf()
        ry = b["oy"] + b["ch"] - SB_H
        old_right = a["ox"] + a["cw"] - 1
        stale = all(rgb[(ry * fw + x) * 3] < 128
                    for x in range(old_right - 19, old_right + 1))

        check(b["rect"][2] < a["rect"][2] and b["rect"][3] < a["rect"][3]
              and b["cw"] < a["cw"] and b["ch"] < a["ch"], "2 SHRUNK",
              "the kernel's W_W/W_H and the package's sh_cw/sh_ch both fell. "
              "11.94.5 snaps the width to a multiple of 8, so this asks the "
              "DIRECTION and not an exact number",
              got="%r -> %r, content %dx%d -> %dx%d"
                  % (a["rect"][2:], b["rect"][2:], a["cw"], a["ch"],
                     b["cw"], b["ch"]), want="both smaller")
        check(b["cols"] < a["cols"] and b["rows"] < a["rows"], "3 FEWER",
              "sh_geom's walks fill slots until the pixels run out, so a "
              "smaller content rect must yield fewer. Equal counts mean the "
              "layout came from the template's 560x380 and not from "
              "OSAPI_WM_CONTENT",
              got="%dx%d -> %dx%d" % (a["cols"], a["rows"], b["cols"],
                                      b["rows"]), want="both fewer")
        check(a_solid and b_solid and not stale and b_end < a_end,
              "4 INSIDE",
              "the strip's rule is one GFX_HLINE across the content, so how "
              "far it reaches is where SHEET believes its right edge is. A "
              "package that keeps drawing at the width it ASKED for leaves "
              "the rule past the new border, over the desktop (11.94.5.1)",
              got="solid to the new edge %s (was %s); ink stops at %d (was "
                  "%d); the 20px ending at the old edge %d are %s"
                  % (b_solid, a_solid, b_end, a_end, old_right,
                     "STILL SOLID" if stale else "not a rule any more"),
              want="solid to the new edge, nothing left at the old one")

        # ...then ask for the SMALLEST window there is, by dragging the
        # corner up and left past the window's own origin. wm_fit clamps it
        # to the declared floor on the way.
        w1 = win()
        mo.drag(w1.x + w1.w - 8, w1.y + w1.h - 8, w1.x + 2, w1.y + 2)
        M.settle(m, limit=240)
        park()
        f = state("floor")
        check(f["cols"] >= 1 and f["rows"] >= 1, "5 FLOOR",
              "the smallest SHEET that is still a spreadsheet: the five "
              "strips, the column header, one column and one row. Without "
              "OSAPI_WM_MINSIZE the drag reaches WMIN_W x WMIN_H, where "
              "sh_geom clamps the grid to nothing and the horizontal bar is "
              "computed inside the formula bar",
              got="%dx%d slots in a %dx%d content" % (f["cols"], f["rows"],
                                                      f["cw"], f["ch"]),
              want="at least 1x1")

        # ...and back OUT, to the corner the window started at. CLAMPED to
        # the glass, because when the shrink never happened - which is what
        # the control does, OSAPI_WM_SIZABLE with AL=0 - that corner is where
        # the pointer already is and anything past it is off the screen,
        # where the harness raises instead of letting the legs report.
        fw, fh, _ = m.fbuf()
        w2 = win()
        mo.drag(w2.x + w2.w - 8, w2.y + w2.h - 8,
                min(a["rect"][0] + a["rect"][2] - 8, fw - 2),
                min(a["rect"][1] + a["rect"][3] - 8, fh - 2))
        M.settle(m, limit=240)
        park()
        c = state("grown")
        check(c["cols"] >= a["cols"] and c["rows"] >= a["rows"], "6 GREW",
              "a one-way clamp passes every leg above: it shrinks and then "
              "stays shrunk",
              got="%dx%d slots, from the floor's %dx%d"
                  % (c["cols"], c["rows"], f["cols"], f["rows"]),
              want="back to at least %dx%d" % (a["cols"], a["rows"]))
        M.write_png_rgb(os.path.join(WORK, "grown.png"), *m.fbuf())
    done("sheetsize")


if __name__ == "__main__":
    main()

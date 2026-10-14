#!/usr/bin/env python3
"""SPEC.md 107.3, 39.4, 47: the Desk Calendar on BOTH 1bpp adapters - the
layout each one gets, today drawn inverse, the selected day's frame, and a
greyed menu item still legible where grey rounds to black.

    make && python3 tests/deskcal1bpp.py [--leg cga|herc] [--shots DIR]

The SHIPPED 360KB system disk with a state file holding one appointment on
the 20th, on MartyPC's 5150 - with a CGA (os8088_5150_cga, its GLaBIOS twin)
and with a Hercules card (os8088_5150_herc_gla). Both framebuffers are one
bit a pixel and the harness reads them whole (`vram`), so every assertion is
about PIXELS: bit 1 is ground, bit 0 is ink. The clock is Wed 14 Oct 2026.

  A  the frame is the adapter's: COMPACT 210 x 139 on the CGA's 640x200,
     TALL 210 x 255 on the Hercules' 720x348 (SPEC.md 107.3's PREFER row).
  B  TODAY IS INVERSE: the 14th's field is mostly ink (its ground), a plain
     day's (the 13th) mostly ground - state carried by the picture, not by a
     colour neither adapter has.
  C  THE SELECTED DAY'S FRAME: a click on the 20th puts an unbroken ink
     rectangle on that field's outer columns and the gap rows round it, and
     takes the 14th's off - its gap rows are ground again, and its field is
     still inverse (dc_reselect re-ran it).
  D  THE 20th's MARK: its mark cell shows the `*` (ink in the cell), the
     13th's does not.
  E  A GREYED ITEM IS LEGIBLE: the Calendar menu held open with nothing
     selected - Edit Item and Delete Item greyed `(no item)`, Show Tasks
     greyed on TALL - and every item's band in the dropped menu carries ink,
     the greyed ones included - never a blank band (the pictures are what a
     reader judges "legible" by; --shots keeps them).

`--shots DIR` writes the screen of each leg, whole and with the menu down.

**BREAK IT ON PURPOSE**: draw today's field with DC_INK instead of DC_INV in
dc_field and B fails; drop dc_reselect's frame erase and C finds the 14th
still framed.
"""
import argparse
import os
import struct
import sys
import tempfile
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88dia     # noqa: E402
from deskcaledit import Cal, TITLE, disk_with   # noqa: E402

TODAY = (2026, 10, 14)
DOW1 = 4                        # 1 October 2026 is a Thursday

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def field(c, day, tall):
    """(x0, y0) of day's field on the glass: content x 16 + 24 * column,
    the week row's top (SPEC.md 107.3's table)."""
    ox, oy = c.origin()
    i = day + DOW1 - 1
    w0, pitch = (24, 10) if tall else (18, 9)
    return ox + 16 + 24 * (i % 7), oy + w0 + pitch * (i // 7)


def ink(rows, x0, y0, w, h):
    return sum(1 for y in range(y0, y0 + h) for x in range(x0, x0 + w)
               if not rows[y][x])


def perimeter(x0, y0):
    """dc_frame's rectangle: x0..x0+23, y0-1..y0+8."""
    pts = set()
    for x in range(x0, x0 + 24):
        pts.add((x, y0 - 1))
        pts.add((x, y0 + 8))
    for y in range(y0 - 1, y0 + 9):
        pts.add((x0, y))
        pts.add((x0 + 23, y))
    return pts


def shot(m, kind, path):
    w, h, rows = m.vram(kind)
    rgb = bytearray()
    for y in range(h):
        for x in range(w):
            v = 255 if rows[y][x] else 0
            rgb += bytes((v, v, v))
    os88marty.write_png_rgb(path, w, h, bytes(rgb))


def leg(tmp, machine, kind, shots):
    print("== %s (%s)" % (kind, machine))
    fx = os88dia.dat(os88dia.deskcal([((2026, 10, 20), 600, "Review")],
                                     lead=0),
                     x=359, y=40, screen=(640, 480))
    img = disk_with(tmp, "1bpp-%s.img" % kind, fx)
    tall = kind == "herc"
    with os88ui.boot(img, machine=machine) as ui:
        m = ui.m
        c = Cal(ui, tmp, check=check)
        w = ui.window(TITLE)
        os88marty.pace(m, 2.0)

        print("A: the adapter's layout")
        want = (210, 255) if tall else (210, 139)
        check((w.w, w.h) == want, "%s: the frame is %dx%d (%s), want %r"
              % (kind, w.w, w.h, "TALL" if tall else "COMPACT", want))

        def frame():
            ui.mo.to(8, 100)            # the pointer is drawn INTO the
            os88marty.pace(m, 1.0)      # framebuffer: keep it off the window
            return m.vram(kind)[2]

        rows = frame()
        if shots:
            shot(m, kind, os.path.join(shots, "deskcal-%s.png" % kind))

        print("B: today inverse")
        tx, ty = field(c, 14, tall)
        px, py = field(c, 13, tall)
        t_ink = ink(rows, tx, ty, 24, 8) / 192.0
        p_ink = ink(rows, px, py, 24, 8) / 192.0
        check(t_ink > 0.6, "%s: the 14th's field is mostly INK (%.0f%%)"
              % (kind, 100 * t_ink))
        check(p_ink < 0.3, "%s: the 13th's is mostly ground (%.0f%%)"
              % (kind, 100 * p_ink))

        print("C: the selected day's frame")
        gaps14 = [(x, ty - 1) for x in range(tx, tx + 24)] + \
                 [(x, ty + 8) for x in range(tx, tx + 24)]
        check(all(not rows[y][x] for x, y in gaps14),
              "%s: the 14th, selected at launch, has its frame's gap rows"
              % kind)
        sx, sy = field(c, 20, tall)
        c.front()
        ui.mo.click(sx + 12, sy + 4)
        os88marty.pace(m, 1.5)
        check(c.bss(os88dia.DESKCAL_BSS_SEL)[0] == 20,
              "%s: the 20th is selected" % kind)
        rows = frame()
        per = perimeter(sx, sy)
        lit = [p for p in per if rows[p[1]][p[0]]]
        check(not lit, "%s: an unbroken ink rectangle round the 20th (%d of "
              "%d perimeter pixels are ground)" % (kind, len(lit), len(per)))
        back = [p for p in gaps14 if not rows[p[1]][p[0]]]
        check(not back, "%s: the 14th's gap rows are ground again (%d ink)"
              % (kind, len(back)))
        t_ink = ink(rows, tx, ty, 24, 8) / 192.0
        check(t_ink > 0.6, "%s: ...and its field is still inverse (%.0f%%)"
              % (kind, 100 * t_ink))

        print("D: the 20th's mark")
        check(ink(rows, sx + 1, sy, 6, 8) > 0,
              "%s: the 20th's mark cell carries the `*`" % kind)
        check(ink(rows, px + 1, py, 6, 8) == 0,
              "%s: the 13th's mark cell is ground" % kind)

        print("E: a greyed item is legible")
        c.key("Escape")                 # nothing selected: Edit / Delete grey
        os88marty.pace(m, 1.0)
        cal = [mm for mm in ui.menus() if mm[0] == "Calendar"]
        check(bool(cal), "%s: a Calendar menu on the bar" % kind)
        if cal:
            title, x0, x1, items = cal[0]
            greyed = [t for t, on in items if not on]
            check("Edit Item (no item)" in greyed
                  and "Delete Item (no item)" in greyed,
                  "%s: Edit and Delete Item are greyed with their reason (%r)"
                  % (kind, greyed))
            before = frame()
            ui.mo.to((x0 + x1) // 2, 5)
            ui.mo._edge(True)           # PRESSED and held: the menu stays down
            os88marty.pace(m, 1.5)
            w_, h_, after = m.vram(kind)
            if shots:
                shot(m, kind, os.path.join(shots, "deskcal-%s-menu.png"
                                           % kind))
            ch = [(x, y) for y in range(h_) for x in range(w_)
                  if before[y][x] != after[y][x] and y > 12
                  and x >= x0 - 4]   # not the pointer's old place at x 8
            if check(bool(ch), "%s: the menu dropped" % kind):
                bx0 = min(p[0] for p in ch)
                bx1 = max(p[0] for p in ch)
                by0 = min(p[1] for p in ch)
                by1 = max(p[1] for p in ch)
                band = (by1 - by0 + 1) // len(items)
                inks = []
                for k in range(len(items)):
                    y0 = by0 + k * band
                    inks.append(ink(after, bx0 + 4, y0 + 2, bx1 - bx0 - 8,
                                    max(band - 4, 1)))
                print("      menu %dx%d at (%d,%d), %d bands of %d: ink %r"
                      % (bx1 - bx0 + 1, by1 - by0 + 1, bx0, by0, len(items),
                         band, inks))
                blank = [items[k][0] for k in range(len(items))
                         if inks[k] == 0]
                check(not blank, "%s: every item's band carries ink, the "
                      "greyed ones included (blank: %r)" % (kind, blank))
            ui.mo.to(8, 150, l=True)    # off the menu, then released: no pick
            os88marty.pace(m, 0.5)
            ui.mo._edge(False)
            os88marty.pace(m, 1.0)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", choices=("both", "cga", "herc"), default="both")
    ap.add_argument("--shots")
    a = ap.parse_args(argv)
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="deskcal1bpp-")
    try:
        if a.leg in ("both", "cga"):
            leg(tmp, os88marty.machine("os8088_5150_cga"), "cga", a.shots)
        if a.leg in ("both", "herc"):
            leg(tmp, "os8088_5150_herc_gla", "herc", a.shots)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("deskcal1bpp: %s" % ("PASS" if not fails else
                               "FAIL (%d): %s" % (len(fails),
                                                  "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

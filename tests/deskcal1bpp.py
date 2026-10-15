#!/usr/bin/env python3
"""SPEC.md 110.3, 39.4, 47: the Desk Calendar WIDGET on BOTH 1bpp adapters -
the layout each one gets, today's inverse box, the selected day's 1-px
outline, a day's dot, the gaps as the desktop's ground, and a greyed menu
item still legible on the widget's own (focused) bar.

    make && python3 tests/deskcal1bpp.py [--leg cga|herc] [--shots DIR]

The SHIPPED 360KB system disk with a state file holding one appointment on
the 20th, on MartyPC's 5150 - with a CGA (os8088_5150_cga, its GLaBIOS twin)
and with a Hercules card (os8088_5150_herc_gla). Both framebuffers are one
bit a pixel and the harness reads them whole (`vram`), so every assertion is
about PIXELS: bit 1 is ground, bit 0 is ink. The clock is Wed 14 Oct 2026.
Coordinates are the layout's (tools/os88dia.py's DESKCAL_GEO).

  A  the frame is the layout's by the screen's HEIGHT: COMPACT 178 x 146 on
     the CGA's 640x200, MID 178 x 242 on the Hercules' 720x348 (v2; v1's
     PREFER pair gave Hercules TALL).
  B  TODAY IS INVERSE: the 14th's box is mostly ink, a plain day's (the
     13th) mostly ground - state carried by the picture, not by a colour.
  C  THE SELECTED DAY'S OUTLINE: a press on the 20th puts an unbroken 1-px
     ink rectangle round its box with ground inside it, and today's box
     stays inverse.
  D  THE 20th's DOT: ink under its number (COMPACT: BESIDE it, touching
     nothing, with the row under the number paper); the 13th has none.
  E  THE GAPS ARE THE DESKTOP: every gap row between the cards is the
     desktop's dither, pixel for pixel the desktop 180 columns left.
  F  A GREYED ITEM IS LEGIBLE: the widget owns the bar, nothing selected -
     Edit Item and Delete Item greyed `(no item)` - and every item's band
     in the dropped Calendar menu carries ink, the greyed ones included.

`--shots DIR` writes the screen of each leg, whole and with the menu down.

**BREAK IT ON PURPOSE**: compose today with black digits on white in
dc_cday1 and B fails; fill the selected day's box and C fails.
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

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def ink(rows, x0, y0, w, h):
    return sum(1 for y in range(y0, y0 + h) for x in range(x0, x0 + w)
               if not rows[y][x])


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
                                     lead=0))
    img = disk_with(tmp, "1bpp-%s.img" % kind, fx)
    with os88ui.boot(img, machine=machine) as ui:
        m = ui.m
        c = Cal(ui, tmp, check=check)
        w = ui.window(TITLE)
        os88marty.pace(m, 2.0)
        G = os88dia.DESKCAL_GEO[c.li()]
        fx0, fy0 = c.face()
        grid = list(c.bss(os88dia.DESKCAL_BSS_GRID, 42))

        def field(day):
            i = grid.index(day)
            return fx0 + 4 + 24 * (i % 7), fy0 + G["W0"] + G["WP"] * (i // 7)

        def box(day):
            fx, r = field(day)
            return os88dia.deskcal_box(G, fx)[0], r - G["BOXT"], r

        def dot(day):
            fx, r = field(day)
            x, dy, dw, dh = os88dia.deskcal_dot(G, fx, day)
            return x, r + dy, dw, dh

        bw = os88dia.deskcal_box(G, 0)[1]

        print("A: the layout by the screen's height")
        want = (178, 242) if kind == "herc" else (178, 146)
        check((w.w, w.h) == want and c.li() == (1 if kind == "herc" else 2),
              "%s: the frame is %dx%d (layout %d), want %r"
              % (kind, w.w, w.h, c.li(), want))

        def frame():
            ui.mo.to(8, 100)            # the pointer is drawn INTO the
            os88marty.pace(m, 1.0)      # framebuffer: keep it off the window
            return m.vram(kind)[2]

        rows = frame()
        if shots:
            shot(m, kind, os.path.join(shots, "deskcal-%s.png" % kind))
        hb = G["BOXH"]

        print("B: today inverse")
        tx, ty, _ = box(14)
        px, py, _ = box(13)
        t_ink = ink(rows, tx, ty, bw, hb) / (float(bw) * hb)
        p_ink = ink(rows, px, py, bw, hb) / (float(bw) * hb)
        check(t_ink > 0.6, "%s: the 14th's box is mostly INK (%.0f%%)"
              % (kind, 100 * t_ink))
        check(p_ink < 0.3, "%s: the 13th's is mostly ground (%.0f%%)"
              % (kind, 100 * p_ink))

        print("C: the selected day's outline")
        c.front()
        c.press(c.day_xy(20))
        check(c.bss(os88dia.DESKCAL_BSS_SEL)[0] == 20,
              "%s: the 20th is selected" % kind)
        rows = frame()
        sx, sy, sr = box(20)
        per = set()
        for x in range(sx, sx + bw):
            per.add((x, sy))
            per.add((x, sy + hb - 1))
        for y in range(sy, sy + hb):
            per.add((sx, y))
            per.add((sx + bw - 1, y))
        lit = [p for p in per if rows[p[1]][p[0]]]
        check(not lit, "%s: an unbroken ink outline round the 20th (%d of "
              "%d perimeter pixels are ground)" % (kind, len(lit), len(per)))
        inside = [(sx + 1, sy + 1), (sx + bw - 2, sy + 1),
                  (sx + 1, sy + hb - 2), (sx + bw - 2, sy + hb - 2)]
        check(all(rows[y][x] for x, y in inside),
              "%s: ...one pixel wide: ground inside its corners" % kind)
        t_ink = ink(rows, tx, ty, bw, hb) / (float(bw) * hb)
        check(t_ink > 0.6, "%s: today's box is still inverse (%.0f%%)"
              % (kind, 100 * t_ink))

        print("D: the 20th's dot")
        dx, dy, dw, dh = dot(20)
        check(ink(rows, dx, dy, dw, dh) == dw * dh,
              "%s: the 20th's dot is ink" % kind)
        dx, dy, dw, dh = dot(13)
        check(ink(rows, dx, dy, dw, dh) == 0,
              "%s: the 13th has none" % kind)
        if G["DOTY"] & os88dia.DESKCAL_DOTBY:
            # COMPACT: the dot BESIDE the number touches nothing - its
            # ring of pixels is paper, and the row under every week
            # (r + 8) is paper too, so no mark reaches the next week
            dx, dy, dw, dh = dot(20)
            ring = [(x, y) for x in range(dx - 1, dx + dw + 1)
                    for y in range(dy - 1, dy + dh + 1)
                    if not (dx <= x < dx + dw and dy <= y < dy + dh)]
            check(all(rows[y][x] for x, y in ring),
                  "%s: nothing touches the 20th's dot (%d of %d ring "
                  "pixels ink)" % (kind, sum(1 for x, y in ring
                                             if not rows[y][x]), len(ring)))
            fx20, r20 = field(20)
            check(ink(rows, fx20, r20 + 8, 24, 1) == 0,
                  "%s: the row under the 20th's number is paper" % kind)

        print("E: the gaps are the desktop's ground")
        y, bad = fy0, []
        for h, gap in G["cards"]:
            for gy in range(y + h, y + h + gap):
                for gx in range(fx0, fx0 + 176):
                    if rows[gy][gx] != rows[gy][gx - 180]:
                        bad.append((gx, gy))
            y += h + gap
        check(not bad, "%s: every gap pixel is the desktop's (%d off)"
              % (kind, len(bad)))

        print("F: a greyed item is legible, on the widget's own bar")
        c.front()
        os88marty.pace(m, 1.0)
        cal = [mm for mm in ui.menus() if mm[0] == "Calendar"]
        check(bool(cal), "%s: the widget owns the bar: a Calendar menu on it"
              % kind)
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
                    yy = by0 + k * band
                    inks.append(ink(after, bx0 + 4, yy + 2, bx1 - bx0 - 8,
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

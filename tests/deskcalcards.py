#!/usr/bin/env python3
"""SPEC.md 110.3 (v2): the Desk Calendar's THREE CARDS, probed pixel by pixel
on all three adapters.

    make && python3 tests/deskcalcards.py --leg vga|cga|herc

The SHIPPED 360KB system disk with the owner's mockup in its state file -
today Mon 24 August 2026, 09:00 Team sync and 14:30 Dentist on it, dots on
the 6th and the 14th, `Read mail` done and `Fix mouse driver` not, and
`Back up files` (the 23rd, 21:00) DUE - on MartyPC's VGA XT (TALL), its
5150 with a CGA (COMPACT) and with a Hercules card (MID). Every coordinate
is the layout's, out of tools/os88stik.py's DESKCAL_GEO (t_stik holds it to
the package's own dc_geos); every pixel is read off the framebuffer.

  A  the frame is 178 wide and the face's height for the layout (355 / 242 /
     146), at the default place: face x = screen width - 264.
  B  every card's four corners are PAPER, and every GAP row - and both
     ground columns - is the DESKTOP's own ground, pixel for pixel the
     desktop 180 columns to the left (the same dither phase).
  C  TODAY, the 24th: its box's four corners INK (the inverse box), and
     white digit pixels inside it.
  D  the DOT of the 6th: its 2 px x DOTH rows ink, the row under it paper -
     under the number on TALL and MID, BESIDE it on COMPACT, whose row under
     the number is paper too (a 9-row pitch: r + 8 touched the next week).
  E  the BELL: card 3's (the CGA slot's reminder page, which a DUE reminder
     took), pixel for pixel the layout's bit record.
  F  the TICK: Read mail's box is pixel for pixel the TICKED record, Fix
     mouse driver's the plain one (the CGA slot paged to its tasks first,
     by a press on its heading - twice: day, then tasks).
  G  a press on the 20th: its box is a 1-px OUTLINE - corners ink, the
     pixel inside each corner paper - and today keeps its inverse box.

**BREAK IT ON PURPOSE**: draw the dot one row lower in dc_cday1 and D fails;
compose the plain box for a done task and F fails; fill the selected day's
box instead of outlining it and G fails.
"""
import argparse
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88stik    # noqa: E402
from deskcaledit import Cal, disk_with     # noqa: E402

B = os88stik
T = (2026, 8, 24)
MACHINES = {"vga": ("os8088_xt_vga", 0), "cga": ("os8088_5150_cga_gla", 2),
            "herc": ("os8088_5150_herc_gla", 1)}
# dcdraw.inc's records (GFXE_BITS: a word a row, bit 15 leftmost)
BELL = {0: (16, (0x0180, 0x07E0, 0x0C30, 0x1818, 0x1008, 0x1008, 0x1008,
                 0x1008, 0x300C, 0x2004, 0x6006, 0xC003, 0xFFFF, 0x03C0,
                 0x0180)),
        1: (16, (0x0180, 0x07E0, 0x0C30, 0x1818, 0x1008, 0x1008, 0x300C,
                 0x6006, 0xC003, 0xFFFF, 0x03C0, 0x0180)),
        2: (8, (0x1800, 0x3C00, 0x4200, 0x4200, 0x4200, 0x8100, 0xFF00,
                0x1800))}
BOX = {0: (11, (0xFFE0,) + (0x8020,) * 9 + (0xFFE0,),
           (0xFFE0, 0x8020, 0x80E0, 0x81A0, 0xA320, 0xB620, 0x9C20, 0x8820,
            0x8020, 0x8020, 0xFFE0)),
       1: (9, (0xFF80,) + (0x8080,) * 7 + (0xFF80,),
           (0xFF80, 0x8080, 0x8380, 0x8680, 0xAC80, 0xB880, 0x9080, 0x8080,
            0xFF80)),
       2: (8, (0xFF00,) + (0x8100,) * 6 + (0xFF00,),
           (0xFF00, 0x8100, 0x8700, 0x8D00, 0xD900, 0xB100, 0x8100, 0xFF00))}

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def fixture():
    appts = [((2026, 8, 6), 600, "Lunch"),
             ((2026, 8, 14), 600, "Haircut"),
             (T, 540, "Team sync", True, True),
             (T, 870, "Dentist", True, True),
             ((2026, 8, 23), 1260, "Back up files", True, True, 0, True)]
    tasks = [("Read mail", True), ("Fix mouse driver", False)]
    return B.dat(B.deskcal(appts, tasks, lead=0))


class Glass(object):
    """The framebuffer, read once: ink(x, y) is True for a black pixel,
    px(x, y) the raw value for a ground compare."""

    def __init__(self, m):
        vt = m.cmd(cmd="video")["type"]
        if vt in ("cga", "mda", "hercules"):
            self.w, self.h, rows = m.vram()
            self.px = lambda x, y: bool(rows[y][x])
            self.ink = lambda x, y: not rows[y][x]
        else:
            self.w, self.h, fb = m.fbuf()
            fb = bytes(fb)
            self.px = lambda x, y: fb[3 * (y * self.w + x):3 * (y * self.w
                                                              + x) + 3]
            self.ink = lambda x, y: fb[3 * (y * self.w + x)] < 128 \
                and fb[3 * (y * self.w + x) + 1] < 128


def rec_bad(gl, x, y, rec, w):
    bad = []
    for r, bits in enumerate(rec):
        for c in range(w):
            if gl.ink(x + c, y + r) != bool(bits & (0x8000 >> c)):
                bad.append((x + c, y + r))
    return bad


def park(ui):
    ui.mo.to(20, 30)
    os88marty.guest_sleep(ui.m, 1.0)


def run(leg):
    machine, li = MACHINES[leg]
    tmp = tempfile.mkdtemp(prefix="deskcalcards-")
    try:
        img = disk_with(tmp, "cards.img", fixture())
        with os88ui.boot(img, machine=os88marty.machine(machine)) as ui:
            m = ui.m
            m.write(ui._S("clk_sec"), bytes([0, 58, 20]))
            c = Cal(ui, tmp, check=check, today=T)
            check(c.li() == li, "%s takes layout %d (stik_li %d)"
                  % (leg, li, c.li()))
            g, G = c.g(), os88stik.DESKCAL_GEO[li]
            os88marty.guest_sleep(m, 2.0)
            park(ui)
            gl = Glass(m)
            w = c.win()
            fx, fy = c.face()

            print("A: the frame")
            check((w.w, w.h) == (178, G["face"][1]) and fx == gl.w - 264,
                  "178 x %d at face x %d = width - 264 (%r)"
                  % (G["face"][1], gl.w - 264, (w.x, w.y, w.w, w.h)))

            print("B: card corners paper; gaps and columns the desktop's")
            y = fy
            badc, badg = [], []
            for h, gap in G["cards"]:
                for px, py in ((fx, y), (fx + 175, y), (fx, y + h - 1),
                               (fx + 175, y + h - 1)):
                    if gl.ink(px, py):
                        badc.append((px, py))
                for gy in range(y + h, y + h + gap):
                    for gx in range(fx - 1, fx + 177):
                        if gl.px(gx, gy) != gl.px(gx - 180, gy):
                            badg.append((gx, gy))
                y += h + gap
            for gy in range(fy, fy + G["face"][1]):
                for gx in (fx - 1, fx + 176):
                    if gl.px(gx, gy) != gl.px(gx - 180, gy):
                        badg.append((gx, gy))
            check(not badc, "every card's corners are paper (%r)" % badc[:4])
            check(not badg, "every gap and both ground columns are the "
                  "desktop's ground (%d off%s)"
                  % (len(badg), ", first %r" % (badg[0],) if badg else ""))

            grid = list(c.bss(B.DESKCAL_BSS_GRID, 42))

            def field(day):
                i = grid.index(day)
                return (fx + 4 + 24 * (i % 7),
                        fy + G["W0"] + G["WP"] * (i // 7))

            def box(day):
                f, r = field(day)
                return os88stik.deskcal_box(G, f)[0], r - G["BOXT"], r

            bw = os88stik.deskcal_box(G, 0)[1]

            print("C: today, inverse")
            x0, y0, r = box(24)
            hb = G["BOXH"]
            corners = [(x0, y0), (x0 + bw - 1, y0), (x0, y0 + hb - 1),
                       (x0 + bw - 1, y0 + hb - 1)]
            check(all(gl.ink(*p) for p in corners),
                  "the 24th's box corners are ink: the inverse box")
            white = sum(1 for xx in range(x0 + 2, x0 + 18)
                        for yy in range(r, r + 8) if not gl.ink(xx, yy))
            check(white > 10, "...with white digits inside it (%d paper "
                  "pixels)" % white)

            print("D: the dot of the 6th")
            f6, r = field(6)
            ddx, ddy, ddw, ddh = os88stik.deskcal_dot(G, f6, 6)
            dy = r + ddy
            dot = [(ddx + i, dy + j) for i in range(ddw) for j in range(ddh)]
            check(all(gl.ink(*p) for p in dot), "its %d x %d dot is ink at "
                  "x + %d, row + %d" % (ddw, ddh, ddx - f6, ddy))
            check(not any(gl.ink(ddx + i, dy + ddh) for i in range(ddw)),
                  "...and the row under it is paper")
            if G["DOTY"] & os88stik.DESKCAL_DOTBY:
                check(not any(gl.ink(f6 + i, r + 8) for i in range(24)),
                      "...and so is the row under the 6th's number: no mark "
                      "reaches the next week (COMPACT)")

            print("E: the bell")
            belw, brec = BELL[li]   # NOT bw: G below reads the box width
            bx, by = c.at(c.remc(), G["BELLX"], G["BELLY"])
            if li == 2:
                check(c.pane() == 2, "the CGA slot shows the reminder page "
                      "a DUE reminder took (pane %d)" % c.pane())
            bad = rec_bad(gl, bx, by, brec, belw)
            check(not bad, "the bell is the layout's record, every pixel (%d "
                  "off)" % len(bad))

            print("F: the tick")
            if li == 2:
                for _ in range(2):
                    c.front()
                    c.press(c.heading())
                check(c.pane() == 1, "the slot paged to its tasks (%d)"
                      % c.pane())
                park(ui)
                gl = Glass(m)
            bsz, plain, ticked = BOX[li]
            x, y = c.at(1, 8, G["T0"])
            bad = rec_bad(gl, x, y, ticked, bsz)
            check(not bad, "Read mail's box is the TICKED record (%d off)"
                  % len(bad))
            bad = rec_bad(gl, x, y + G["TP"], plain, bsz)
            check(not bad, "Fix mouse driver's is the plain one (%d off)"
                  % len(bad))

            print("G: the 20th selected: an outline")
            c.front()
            c.press(c.day_xy(20))
            check(c.bss(B.DESKCAL_BSS_SEL)[0] == 20, "the 20th is selected")
            park(ui)
            gl = Glass(m)
            x0, y0, r = box(20)
            corners = [(x0, y0), (x0 + bw - 1, y0), (x0, y0 + hb - 1),
                       (x0 + bw - 1, y0 + hb - 1)]
            inner = [(x0 + 1, y0 + 1), (x0 + bw - 2, y0 + 1),
                     (x0 + 1, y0 + hb - 2), (x0 + bw - 2, y0 + hb - 2)]
            check(all(gl.ink(*p) for p in corners)
                  and not any(gl.ink(*p) for p in inner),
                  "its box is a 1-px outline: corners ink, inside paper "
                  "(paper corners %r, inked inside %r)"
                  % ([p for p in corners if not gl.ink(*p)],
                     [p for p in inner if gl.ink(*p)]))
            x0, y0, r = box(24)
            check(gl.ink(x0, y0) and gl.ink(x0 + 1, y0 + 1),
                  "...and today keeps its inverse box")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", choices=("vga", "cga", "herc", "all"),
                    default="all")
    a = ap.parse_args()
    for leg in (("vga", "cga", "herc") if a.leg == "all" else (a.leg,)):
        print("== %s" % leg)
        run(leg)
    print("deskcalcards: %s" % ("PASS" if not fails else
                                "FAIL (%d): %s" % (len(fails),
                                                   "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

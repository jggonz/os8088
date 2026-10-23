#!/usr/bin/env python3
"""The pulldown leaves nothing behind it (SPEC.md 81.98).

    make && python3 tests/sheetmtail.py

SHEET's Data menu has twelve items, and on a 200-row screen its panel hangs
PAST the window's bottom edge, over the dock. Closing it used to repaint the
window and nothing else, so the rows below the window kept the menu's last
items painted on them for the rest of the session (docs/plans/
SHEET-GAPS-PLAN.md section 0.1).

The fix banks what the panel covers and writes it back. This gate:

  1. photographs the screen, holds Data open and reads the panel's rect out
     of SHEET's own memory - which PROVES the panel overhangs the window
     (a panel inside it would make the rest of this pass vacuously), and that
     the bank was taken;
  2. releases it off the panel, so nothing runs, and compares every row below
     the window with the photograph: they must be identical;
  3. drags the window DOWN, so the same menu runs off the bottom of the
     SCREEN. The panel is NOT slid up (81.98: a slid bar menu covers its own
     title and lands under the pointer, where a plain click would fire an
     item), so the bank must clip at the screen's last row - and closing it
     must still leave nothing.

`--card vga` runs the FOUR-PLANE path, which the CGA arms never reach: the
bank's size is planes x rows x byte columns and CGA has one plane. On the
VGA machine (640x480) it asserts, through the card's own rendered
framebuffer, that opening and closing Data leaves ZERO differing pixels -
and then, as tests/atmenusu.py does, pokes the bank flag to 0 while the
panel is up so the close takes the REPAINT fallback, and checks that too.

`--card herc` runs the same two photographs on the Hercules machine
(720x348): one plane like CGA, but the four-bank interleave and the wider
row are its own, and a CGA pass says nothing about them.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty as M                                       # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
import os88sym                                              # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import os88marty                                            # noqa: E402
from os88geom import WIN_SIZE, W_SEG                            # noqa: E402
from paintmove import pkg_syms                                  # noqa: E402

WORK = "build/sheetmtail"               # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmtail.img"
NAME = "TAIL.SLK"
DATA = (311, 45)                        # the Data menu: sheetdb's, sheetform's
SCREEN_H = 200
PARK = (600, 120)                       # in the grid, clear of the panel
DROP = 24                               # how far arm 3 drags the window down


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk({(0, 0): 1.0, (1, 0): 'tail'}))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def fdiff(a, b, w, h, y0):
    """differing pixels in rows y0.. of two fbuf() rgb images, and their box"""
    n, box = 0, None
    for row in range(y0, h):
        o = row * w * 3
        if a[o:o + w * 3] == b[o:o + w * 3]:
            continue
        for col in range(w):
            i = o + col * 3
            if a[i:i + 3] != b[i:i + 3]:
                n += 1
                box = ((col, row, col, row) if box is None else
                       (min(box[0], col), min(box[1], row),
                        max(box[2], col), max(box[3], row)))
    return n, box


# the photographed arms: machine, screen, and what the bank is on that card
CARDS = {"vga": ("os8088_xt_vga", (640, 480), "mode 12h, FOUR planes"),
         # 350, not the kernel's 348: fbuf is the card's APERTURE (mode 7
         # rasters 720x350), the same note tests/dispcheck.py carries
         "herc": ("os8088_5150_herc_gla", (720, 350),
                  "Hercules page 0, ONE plane, four interleaved banks")}


def main_card(card):
    """exact restore and the repaint fallback, photographed on `card`"""
    machine, size, what = CARDS[card]
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    sym = pkg_syms("apps/sheet/sheet.asm")
    got = {}
    with M.launch(SF.SYS, apps=DISK, machine=machine) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        ds = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, ds)
        dispcp.open_named(m, mo, S, M.settle, wx, wy, name=NAME)
        M.settle(m, limit=240)

        def seg():
            sl = dispcp.win_list(m, S, check=False)[-1]
            b = m.read(S("wm_wins") + sl * WIN_SIZE + W_SEG, 2)
            return b[0] | (b[1] << 8)

        def word(name):
            v = m.readseg(seg(), sym[name], 2)
            return v[0] | (v[1] << 8)

        x, y, w, h = dispcp.win_rect(m, S, dispcp.win_list(m, S,
                                                            check=False)[-1])
        # Data's title, where the CGA calibration puts it relative to the
        # window (its bar is the window's own, so the offset carries), and
        # the bar's middle row: content top (y + 18) + half of SHEET's 14
        data = (x + (DATA[0] - 55), y + 18 + 7)
        park = (x + w - 40, y + h // 2)

        def cycle(tag, refuse):
            mo.to(*park)
            M.settle(m)
            before = m.fbuf()
            mo.to(*data)
            mo._edge(True)
            M.settle(m)
            opened = word("sh_mopen") & 0xFF
            banked = word("sh_mbanked") & 0xFF
            rect = (word("sh_mrx1"), word("sh_mry1"), word("sh_mrx2"),
                    word("sh_mry2"))
            os88marty.write_png_rgb(os.path.join(WORK, "%s-%s-held.png"
                                                 % (card, tag)), *m.fbuf())
            if refuse:                          # what a refused save leaves
                m.write(seg() * 16 + sym["sh_mbanked"], b"\x00")
            mo.to(*park, l=True)                # button down: off the panel
            mo._edge(False)
            M.settle(m, limit=120)
            after = m.fbuf()
            os88marty.write_png_rgb(os.path.join(WORK, "%s-%s-after.png"
                                                 % (card, tag)), *after)
            vw, vh = before[0], before[1]
            n, box = fdiff(before[2], after[2], vw, vh, 20)
            return dict(opened=opened, banked=banked, rect=rect, n=n,
                        box=box, size=(vw, vh), win=(x, y, w, h))

        got["bank"] = cycle("bank", False)
        bank_after = m.fbuf()
        got["fall"] = cycle("fall", True)
        fall_after = m.fbuf()
        vw, vh = bank_after[0], bank_after[1]
        got["ab"] = fdiff(bank_after[2], fall_after[2], vw, vh, 20)

    a, f = got["bank"], got["fall"]
    print("   %s %r, window %r, panel %r" % (card, a["size"], a["win"],
                                           a["rect"]))
    check(a["size"] == size, "the machine is %s, its frame %dx%d" % ((card,) + size),
          "%s: %s" % (machine, what), got=a["size"], want=size)
    check(a["opened"] == 4, "Data's panel opened", "sh_mopen names Data",
          got=a["opened"], want=4)
    check(a["banked"] == 1, "...and the bank was taken (%s)" % what,
          "sh_mbank sized it planes x rows x byte columns, under the "
          "staging claim's 32 KB", got=a["banked"], want=1)
    sb = (a["win"][1] + a["win"][3] - 14, a["win"][1] + a["win"][3] - 1)
    inbar = a["box"] is None or (a["box"][1] >= sb[0] - 2
                                 and a["box"][3] <= sb[1])
    check(inbar, "closing it restores everything the panel covered, on %s" % card,
          "from the card's own rendered framebuffer: the only pixels that "
          "may change are the STATUS BAR's, which the click that opened the "
          "menu cleared - a wrong plane count or rect would show anywhere",
          got="%d px, box %r" % (a["n"], a["box"]),
          want="none outside rows %d..%d" % sb)
    inside = a["rect"][3] <= a["win"][1] + a["win"][3] - 1
    ab_n, ab_box = got["ab"]
    check(inside, "the panel is inside the window here, so the two paths "
          "can be compared", "this screen has room for Data; the tail "
          "is CGA's case and the CGA arms cover it", got=a["rect"],
          want="bottom <= %d" % (a["win"][1] + a["win"][3] - 1))
    check(ab_n == 0, "the bank path lands on the SAME PIXELS as the repaint",
          "one photograph after each close: the restore plus the bar and "
          "status redraw against a full repaint - every pixel below the "
          "kernel's bar must agree. It is the check that found the stale "
          "status line", got="%d px, box %r" % (ab_n, ab_box), want=0)
    done("sheetmtail --card %s" % card)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", choices=("cga",) + tuple(CARDS), default="cga")
    card = ap.parse_args().card
    if card in CARDS:
        return main_card(card)
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    sym = pkg_syms("apps/sheet/sheet.asm")
    res = {}
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

        def sheet_slot():
            return dispcp.win_list(m, S, check=False)[-1]

        def word(name):
            at = S("wm_wins") + sheet_slot() * WIN_SIZE + W_SEG
            b = m.read(at, 2)
            v = m.readseg(b[0] | (b[1] << 8), sym[name], 2)
            return v[0] | (v[1] << 8)

        def screen():
            _, _, rr = m.vram("cga")
            return [bytes(r) for r in rr]

        def shot(tag, rows):
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rows)

        def arm(tag, x, y):
            """hold the menu at (x, y), then release it off the panel"""
            mo.to(*PARK)
            M.settle(m)
            before = screen()
            mo.to(x, y)
            mo._edge(True)
            M.settle(m)
            rect = (word("sh_mrx1"), word("sh_mry1"), word("sh_mrx2"),
                    word("sh_mry2"))
            banked = (word("sh_mbanked") & 0xFF, word("sh_mbky2"))
            held = screen()
            shot(tag + "-held", held)
            mo.to(*PARK, l=True)                # button still down: off it
            mo._edge(False)
            M.settle(m, limit=120)
            after = screen()
            shot(tag + "-after", after)
            return before, held, after, rect, banked

        def below(rows, y0):
            return rows[y0:SCREEN_H]

        # --- 1 and 2: the default window ---------------------------------
        x, y, w, h = dispcp.win_rect(m, S, sheet_slot())
        y2 = y + h - 1
        before, held, after, rect, banked = arm("1", *DATA)
        res["a"] = dict(win=(x, y, w, h), rect=rect, banked=banked,
                        drawn=below(held, y2 + 1) != below(before, y2 + 1),
                        kept=below(after, y2 + 1) == below(before, y2 + 1),
                        y2=y2)

        # --- 3: the window dragged down, so the panel must slide ----------
        mo.drag(x + w // 2, y + 8, x + w // 2, y + 8 + DROP)
        M.settle(m, limit=120)
        x, y, w, h = dispcp.win_rect(m, S, sheet_slot())
        y2 = y + h - 1
        before, held, after, rect, banked = arm("3", DATA[0], DATA[1] + DROP)
        res["b"] = dict(win=(x, y, w, h), rect=rect, banked=banked,
                        kept=below(after, y2 + 1) == below(before, y2 + 1),
                        y2=y2)

    a, b = res["a"], res["b"]
    ra, rb = a["rect"], b["rect"]
    check(ra[3] > a["y2"], "Data's panel hangs PAST the window's bottom edge",
          "otherwise the rest of this passes by default: the defect is only "
          "ever below the window", got="panel to row %d, window to %d"
          % (ra[3], a["y2"]), want="the panel's bottom beyond the window's")
    check(a["banked"][0] == 1, "...and the pixels under it were banked",
          "sh_mbank took the save-under; 0 would mean the old repaint path",
          got=a["banked"][0], want=1)
    check(a["drawn"], "the panel was drawn below the window while held",
          "the rows below the window changed while it was open",
          got=a["drawn"], want=True)
    check(a["kept"], "closing it leaves NOTHING below the window",
          "every row below the window is identical to before it opened - "
          "the defect left the last items painted over the dock",
          got=a["kept"], want=True)
    natural = b["win"][1] + 18 + 14     # content top + SHEET's bar height
    check(b["win"][1] > a["win"][1], "the window really moved down",
          "or the arm below would test the default window twice",
          got="y %d -> %d" % (a["win"][1], b["win"][1]), want="larger")
    check(rb[1] == natural and rb[3] > SCREEN_H - 1,
          "a panel that runs off the SCREEN is not slid up",
          "it hangs under its own title, past row %d; sliding it would cover "
          "the title and put an item under the pointer" % (SCREEN_H - 1),
          got="panel rows %d..%d" % (rb[1], rb[3]),
          want="%d..(> %d)" % (natural, SCREEN_H - 1))
    check(b["banked"] == (1, SCREEN_H - 1), "...so the bank clips at the "
          "screen's last row", "sh_mbky2: GFX_SAVE is never asked for rows "
          "the screen does not have", got=b["banked"], want=(1, SCREEN_H - 1))
    check(b["kept"], "...and closing it still leaves nothing below the "
          "window", "it covered more of what is not SHEET's, and all of it "
          "came back", got=b["kept"], want=True)
    print("   window %r -> %r; panel %r -> %r" % (a["win"], b["win"], ra, rb))
    done("sheetmtail")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""SHEET's repaint stays inside SHEET (SPEC.md 81.105, upstream issue #152).

    make && python3 tests/sheetclip.py

sh_repaint had NO clip region armed, and almost none of its fifty callers is
a W_PAINT, so its fill and every glyph landed on whatever window was stacked
above SHEET. Data > Chart Column... is the case that shows it, and it IS the
test: the Chart window is created at (0,0), over SHEET's top-left corner,
shown, painted - and then sh_docmd_chart repaints SHEET, in that order.

On the VGA machine, off the card's rendered frame:

  1. photograph SHEET where the Chart is about to land: its ink (header
     letters, gridlines, the numbers) is the signature SHEET would leave
  2. Data > Chart Column... over A1:A3: the Chart opens ON TOP of SHEET and
     overlaps it (the arm is not vacuous)
  3. in that overlap, SHEET's ink is GONE: where SHEET had a black pixel, the
     Chart's content is not simply that black pixel again
  4. and the Chart's FRAME is whole where it lies over SHEET - which is where
     the fork shows the defect. sh_repaint's own tail repaints a dirty,
     uncovered Chart's CONTENT after SHEET draws, so the content survived;
     the frame is the kernel's and nothing redraws it, and SHEET's white
     fill took its right and bottom edges

Not "repaint SHEET again while the Chart is up": any click on SHEET first
RAISES it over the Chart, which is the window manager doing its job, and the
Chart is in front so keys go to it.
"""
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

WORK = "build/sheetclip"
DISK = "build/sheetclip.img"
NAME = "CLIP.SLK"
TITLES = ["File", "Edit", "Formula", "Format", "Data", "Options", "Macro",
          "Sheets", "Help"]
TITLE_H = 18


def main():
    os.chdir(os.path.join(HERE, ".."))
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk({(0, 0): 30.0, (1, 0): 80.0,
                                        (2, 0): 50.0}))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL", src], check=True,
                   stdout=subprocess.DEVNULL)
    S = os88sym.linear
    got = {}
    with M.launch(SF.SYS, apps=DISK, machine="os8088_xt_vga") as m:
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
        x0, y0, w0, h0 = dispcp.win_rect(m, S, sl)
        X, x = {}, x0 + 1
        for t in TITLES:
            X[t] = x + (8 * len(t) + 16) // 2
            x += 8 * len(t) + 16
        bar = y0 + 18 + 7
        item = lambda t, i: (X[t] + 6, y0 + 18 + 14 + 2 + 12 * i + 6)
        cell = lambda r, c: (x0 + 1 + 40 + 72 * c + 36,
                             y0 + 18 + 14 + 26 + 14 * r + 7)
        park = (x0 + w0 - 30, y0 + h0 - 40)
        mo.click(*cell(0, 0))
        M.settle(m)
        mo.to(*park)
        M.settle(m)
        before = m.fbuf()
        M.write_png_rgb(os.path.join(WORK, "1-sheet.png"), *before)
        mo.menu(X["Data"], bar, *item("Data", 10))     # Chart Column... -
                                                      # 9 until 81.114 put
                                                      # Table... in at 8
        M.settle(m, limit=120)
        mo.to(*park)
        M.settle(m)
        cw = dispcp.win_list(m, S, check=False)[-1]
        cx, cy, cww, chh = dispcp.win_rect(m, S, cw)
        got["chart"] = (cw != sl, (cx, cy, cww, chh), (x0, y0, w0, h0))
        ox1, oy1 = max(cx + 1, x0), max(cy + TITLE_H, y0)
        ox2, oy2 = min(cx + cww - 2, x0 + w0 - 1), min(cy + chh - 2, y0 + h0 - 1)
        got["overlap"] = (ox1, oy1, ox2, oy2)
        after = m.fbuf()
        M.write_png_rgb(os.path.join(WORK, "2-chart.png"), *after)

    w = before[0]
    ink = lambda img, x, y: max(img[2][(y * w + x) * 3:(y * w + x) * 3 + 3]) < 64
    # BLACK in every channel: the Chart's bars are dark BLUE, which a
    # red-channel test called ink and counted as SHEET's
    ox1, oy1, ox2, oy2 = got["overlap"]
    was = kept = 0
    for y in range(oy1, oy2 + 1):
        for x in range(ox1, ox2 + 1):
            if ink(before, x, y):
                was += 1
                kept += ink(after, x, y)
    got["ink"] = (kept, was)
    # THE FRAME, which is where the fork shows it: sh_repaint's own tail
    # repaints a dirty, uncovered Chart's CONTENT after SHEET draws, so the
    # content survives - but a window's frame is the kernel's, nothing
    # redraws it, and SHEET's white fill erased its right and bottom edges
    cx, cy, cww, chh = got["chart"][1]
    edge = ([(cx + cww - 1, y) for y in range(cy, cy + chh)] +
            [(x, cy + chh - 1) for x in range(cx, cx + cww)])
    edge = [(x, y) for x, y in edge if x >= ox1 and y >= oy1]
    got["frame"] = (sum(ink(after, x, y) for x, y in edge), len(edge))
    print("   chart %r over SHEET %r, overlap %r" % (got["chart"][1],
                                                   got["chart"][2],
                                                   got["overlap"]))
    ox1, oy1, ox2, oy2 = got["overlap"]
    check(got["chart"][0] and ox2 > ox1 + 20 and oy2 > oy1 + 20,
          "the Chart window opens on top of SHEET and overlaps it",
          "the arm is not vacuous", got=got["overlap"])
    kept, was = got["ink"]
    check(was > 500, "SHEET had ink where the Chart landed",
          "the signature to look for", got=was, want="> 500")
    check(kept * 4 < was,
          "SHEET's ink is not painted back over the Chart",
          "sh_repaint arms OSAPI_WM_CLIP_SET on its own window (81.105): "
          "the Chart's content stands, and what coincides is only the "
          "Chart's own ink. Before, SHEET's last repaint put its grid back",
          got="%d of %d SHEET-ink pixels still black" % (kept, was),
          want="under a quarter")
    fb, fn = got["frame"]
    check(fn > 100 and fb == fn,
          "the Chart's frame is whole where it lies over SHEET",
          "its right and bottom edges, black to the pixel. The kernel draws "
          "a frame and nothing redraws it, so SHEET's fill erased them",
          got="%d of %d edge pixels black" % (fb, fn), want="all")
    done("sheetclip")


if __name__ == "__main__":
    main()

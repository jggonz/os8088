#!/usr/bin/env python3
"""SHEET's status bar in Excel's shape (SPEC.md 81.104).

    make && python3 tests/sheetstatus.py

Excel 2.1d's status bar has a DIVIDER between the message and the indicator
block, and draws NUM INVERTED, white in a black box. SHEET had neither. On the
cycle-accurate CGA (one bit deep: what an inverted box is must survive there),
from SHEET's own geometry words and the card's rendered frame:

  1. the divider: every interior row of the strip is black at
     right - SH_SB_DIVX
  2. the indicator box: its 1px ring is solid black, and inside it the run is
     black ground with WHITE glyph pixels - an inverted NUM, not a plain one
  3. the message is still there and still black on white ("Ready")
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
from os88geom import WIN_SIZE, W_SEG                            # noqa: E402
from paintmove import pkg_syms                                  # noqa: E402

WORK = "build/sheetstatus"
DISK = "build/sheetstatus.img"
NAME = "STAT.SLK"
SB_H, DIVX, INDX = 16, 96, 88          # sheet.asm: SH_SB_H, SH_SB_DIVX, right-88


def main():
    os.chdir(os.path.join(HERE, ".."))
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk({(0, 0): 1.0}))
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
        seg = m.read(S("wm_wins") + sl * WIN_SIZE + W_SEG, 2)
        seg = seg[0] | (seg[1] << 8)
        word = lambda n: (lambda v: v[0] | (v[1] << 8))(m.readseg(seg, sym[n], 2))
        ox, oy, cw, ch = (word("sh_ox"), word("sh_oy"), word("sh_cw"),
                          word("sh_ch"))
        mo.to(5, 195)                       # the pointer off the strip
        M.settle(m)
        w, h, rgb = m.fbuf()
        M.write_png_rgb(os.path.join(WORK, "status.png"), w, h, rgb)

    black = lambda x, y: rgb[(y * w + x) * 3] < 128
    top = oy + ch - SB_H                   # the strip's hline
    right = ox + cw
    print("   content", (ox, oy, cw, ch), "strip rows", top, "..", oy + ch - 1)
    dx = right - DIVX
    div = [black(dx, y) for y in range(top + 1, oy + ch)]
    check(all(div), "the divider runs down the strip's interior",
          "column right - SH_SB_DIVX, every row below the hline",
          got="%d of %d black" % (sum(div), len(div)), want="all")
    beside = [black(dx - 1, y) for y in range(top + 1, oy + ch)]
    check(not all(beside), "...and is ONE column, not a band",
          got="column left of it: %d black" % sum(beside))
    ix, iy = right - INDX, top + 4
    ring = ([(x, iy - 1) for x in range(ix - 1, ix + 25)] +
            [(x, iy + 8) for x in range(ix - 1, ix + 25)] +
            [(ix - 1, y) for y in range(iy - 1, iy + 9)] +
            [(ix + 24, y) for y in range(iy - 1, iy + 9)])
    rb = sum(black(x, y) for x, y in ring)
    check(rb == len(ring), "the indicator's ring is solid black",
          "the 1px GFX_FRAME just outside the run", got="%d of %d" %
          (rb, len(ring)), want="all")
    lit = sum(not black(x, y) for y in range(iy, iy + 8)
              for x in range(ix, ix + 24))
    gaps = [black(ix + 8 * c + 7, y) for c in range(3) for y in range(iy, iy + 8)]
    check(lit > 0 and all(gaps),
          "inside it NUM is INVERTED: white glyphs, black between them",
          "FONT_RUN, ink CWHITE on CBLACK. The GAPS are the test: column 7 of "
          "every 8x8 cell is the inter-letter space, black only when the "
          "ground is - the kernel's glyphs are heavy, so a white-pixel "
          "count alone cannot tell inverted from plain",
          got="%d white; gap columns %d of %d black" % (lit, sum(gaps),
                                                          len(gaps)))
    msg = [black(x, y) for y in range(iy, iy + 8) for x in range(ox + 4,
                                                                 ox + 44)]
    check(0 < msg.count(True) < len(msg) // 2,
          "the message is still black on white", "'Ready', left of it",
          got="%d black of %d" % (msg.count(True), len(msg)))
    done("sheetstatus")


if __name__ == "__main__":
    main()

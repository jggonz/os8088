#!/usr/bin/env python3
"""SHEET's pulldowns carry Excel's group separators (SPEC.md 81.109).

    make && python3 tests/sheetmsep.py

A 1px line in the gap after each row sh_msep marks - Excel 2.1d's groups,
read off its captures - drawn INSIDE the existing 12px pitch, so no row moves
(the owner's choice over Word's 5px band: every gate that clicks a row by
its pitch stays valid).

On the cycle-accurate CGA, each built-in menu held open, off the glass and
SHEET's own panel words (sh_mrx1/2, sh_mry1, sh_mcnt):

  - where a line is expected, the whole interior span of row_top + 10 is ink
  - where none is, that span is NOT a line (the gap stays paper)
  - and the rows did not move: the panel is exactly 12 * n + 4 tall
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

WORK = "build/sheetmsep"
DISK = "build/sheetmsep.img"
NAME = "SEP.SLK"
TITLES = ["File", "Edit", "Formula", "Format", "Data", "Options", "Macro"]
# the rows a line FOLLOWS, per menu - sheet.asm's sh_msep, restated as the
# captures read (a transcription the gate checks, not a copy of the table)
SEPS = {"File": {1}, "Edit": {0, 6, 8}, "Formula": {2, 3, 4},
        # Data's last rule separates EXCEL'S list from this app's three chart
        # items, so it moves whenever a command goes in above it: 81.82's
        # Parse pushed it to 8 and 81.114's Table... to 9.
        "Format": {4, 6}, "Data": {0, 5, 6, 9}, "Options": {1, 2},
        "Macro": {1}}
SH_MI_H = 12


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
    got = {}
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
        x0, y0, w0, h0 = dispcp.win_rect(m, S, sl)
        seg = m.read(S("wm_wins") + sl * WIN_SIZE + W_SEG, 2)
        seg = seg[0] | (seg[1] << 8)
        word = lambda n: (lambda v: v[0] | (v[1] << 8))(m.readseg(seg, sym[n], 2))
        park = (5, 195)
        x = x0 + 1
        for t in TITLES:
            wcell = 8 * len(t) + 16
            mo.to(x + wcell // 2, y0 + 18 + 7)
            x += wcell
            mo._edge(True)
            M.settle(m)
            mo.to(*park, l=True)
            M.settle(m)
            px1, py1 = word("sh_mrx1"), word("sh_mry1")
            px2, py2 = word("sh_mrx2"), word("sh_mry2")
            n = word("sh_mcnt")
            w, h, rgb = m.fbuf()
            M.write_png_rgb(os.path.join(WORK, "%s.png" % t.lower()), w, h, rgb)
            ink = lambda xx, yy: max(rgb[(yy * w + xx) * 3:(yy * w + xx) * 3 + 3]) < 64
            lines = set()
            for i in range(n - 1):
                yy = py1 + 2 + SH_MI_H * i + 10
                span = [ink(xx, yy) for xx in range(px1 + 1, px2)]
                if all(span):
                    lines.add(i)
                elif any(span[len(span) // 4:3 * len(span) // 4]):
                    lines.add(("partial", i))
            got[t] = (lines, py2 - py1 + 1, n)
            mo._edge(False)
            M.settle(m)

    for t in TITLES:
        lines, hgt, n = got[t]
        check(lines == SEPS[t],
              "%s: a line after rows %s and nowhere else" % (t, sorted(SEPS[t])),
              "each is solid across the panel's interior at row_top + 10",
              got=sorted(lines, key=str), want=sorted(SEPS[t]))
        check(hgt == SH_MI_H * n + 4,
              "%s: the panel is 12 * %d + 4 tall - no row moved" % (t, n),
              got=hgt, want=SH_MI_H * n + 4)
    done("sheetmsep")


if __name__ == "__main__":
    main()

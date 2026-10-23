#!/usr/bin/env python3
"""The Options menu in Excel's order, and its two dialogs (SPEC.md 81.106).

    make && python3 tests/sheetopts.py

Excel 2.1d's Options menu, less the print items and Short Menus (decided
out), is Display..., Freeze Panes, Protect Document, Calculation..., Calculate
Now, Workspace.... SHEET's had Gridlines and Formulas as two relabelling
toggles, Freeze Panes last, no Workspace, and its A1/R1C1 switch on the
Formula menu under Excel's name for something else. In Excel, Gridlines and
Formulas are check boxes in Display... and R1C1 is one in Workspace... - the
Border dialog's shape, so they are that engine's kinds 1 and 2.

On the cycle-accurate CGA, off the glass (tests/glass.py) and SHEET's bytes:

  1. the pulldown reads Excel's six, in Excel's order
  2. Display... opens as kind 1 with the LIVE state: Formulas off, Gridlines on
  3. unchecking Gridlines and checking Formulas, then OK, sets both flags
  4. Workspace... opens as kind 2; checking R1C1 and OK switches the
     reference style - and the reference box on the glass says so
  5. Formula > Reference is greyed (MENU_DIS), as Excel's is when no formula
     is being edited
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
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402
from os88geom import WIN_SIZE, W_SEG                            # noqa: E402
from paintmove import pkg_syms                                  # noqa: E402

WORK = "build/sheetopts"
DISK = "build/sheetopts.img"
WANT = ["Display...", "Freeze Panes", "Protect Document", "Calculation...",
        "Calculate Now   F9", "Workspace..."]      # F9's caption, 81.101
SH_MI_H = 12
REFBOX = (56, 57, 122, 71)              # tests/sheethide.py's


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, "SHIN.SLK")
    open(src, "wb").write(F.write_sylk({(0, 0): 1.0}))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "APPS:build/sheet.o88",
                    "APPS:build/CHART.OVL", "APPS:build/MACRO.OVL",
                    "APPS:" + src], check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    sym = pkg_syms("apps/sheet/sheet.asm")
    got = {}
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        S = lambda n: m.sym(n)
        table = glass.glyphs(m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        mo.dblclick(*SF.APPS_FOLDER)
        M.settle(m)
        SF.open_shin(m, mo)
        M.settle(m, limit=180)
        sl = dispcp.win_list(m, S, check=False)[-1]
        seg = m.read(S("wm_wins") + sl * WIN_SIZE + W_SEG, 2)
        seg = seg[0] | (seg[1] << 8)
        byte = lambda n: m.readseg(seg, sym[n], 1)[0]
        word = lambda n: (lambda v: v[0] | (v[1] << 8))(m.readseg(seg, sym[n], 2))
        opt = SF.OPTIONS_MENU
        park = (634, 180)

        # 1: hold the pulldown open and read its rows
        mo.to(*opt)
        mo._edge(True)
        M.settle(m)
        mo.to(*park, l=True)
        M.settle(m)
        x1, y1, x2 = word("sh_mrx1"), word("sh_mry1"), word("sh_mrx2")
        _, _, r = m.vram("cga")
        M.write_png(os.path.join(WORK, "1-menu.png"), 640, 200, r)
        got["rows"] = [glass.cell_text(r, (x1 + 16 - 1, y1 + 2 + SH_MI_H * i,
                                           x2 - 8, y1 + 2 + SH_MI_H * (i + 1)),
                                       table) for i in range(len(WANT))]
        mo._edge(False)
        M.settle(m)

        # 2/3: Display... - read its opening state, then flip both boxes
        got["disp0"] = (byte("sh_gridlines"), byte("sh_showformulas"))
        mo.menu(opt[0], opt[1], opt[0] + 17, 57 + 2)
        M.settle(m)
        got["dkind"] = (word("sh_bdlg_win") != 0, byte("sh_bdlg_kind"),
                        byte("sh_bdlg_sel"))
        _, _, r = m.vram("cga")
        M.write_png(os.path.join(WORK, "2-display.png"), 640, 200, r)
        dw = dispcp.win_list(m, S, check=False)[-1]
        x, y, _, _ = dispcp.win_rect(m, S, dw)
        cx, cy = x + 1, y + 18
        for box in (SF.DISP_GRIDLINES, SF.DISP_FORMULAS):
            mo.click(cx + SF.BDLG_GX1 + 12,
                     cy + SF.BDLG_ROWTOP + SF.BDLG_ROWH * box + 4)
            M.settle(m)
        mo.click(cx + (SF.BDLG_GX2 + SF.BDLG_W) // 2, cy + 30)
        M.settle(m)
        got["disp1"] = (byte("sh_gridlines"), byte("sh_showformulas"),
                        word("sh_bdlg_win"))

        # 4: Workspace... - R1C1
        mo.menu(opt[0], opt[1], opt[0] + 17, 57 + 12 * 5 + 2)
        M.settle(m)
        got["wkind"] = (word("sh_bdlg_win") != 0, byte("sh_bdlg_kind"),
                        byte("sh_bdlg_sel"))
        _, _, r = m.vram("cga")
        M.write_png(os.path.join(WORK, "3-workspace.png"), 640, 200, r)
        ww = dispcp.win_list(m, S, check=False)[-1]
        x, y, _, _ = dispcp.win_rect(m, S, ww)
        cx, cy = x + 1, y + 18
        mo.click(cx + SF.BDLG_GX1 + 12, cy + SF.BDLG_ROWTOP + 4)
        M.settle(m)
        mo.click(cx + (SF.BDLG_GX2 + SF.BDLG_W) // 2, cy + 30)
        M.settle(m)
        mo.to(*park)
        M.settle(m)
        got["a1"] = byte("sh_a1style")
        _, _, r = m.vram("cga")
        M.write_png(os.path.join(WORK, "4-r1c1.png"), 640, 200, r)
        got["ref"] = glass.cell_text(r, REFBOX, table)

        # 5: Formula > Reference's string starts with MENU_DIS
        f = sym["sh_i_formula"]
        ptr = (lambda v: v[0] | (v[1] << 8))(m.readseg(seg, f + 4, 2))
        got["refdis"] = m.readseg(seg, ptr, 1)[0]

    print("  ", got)
    rows = [(t or "").strip() for t in got["rows"]]
    check(rows == WANT, "the Options pulldown reads Excel's six, in order",
          "Display..., Freeze Panes second, Workspace... last",
          got=rows, want=WANT)
    check(got["disp0"] == (1, 0) and got["dkind"] == (True, 1, 0b10),
          "Display... opens as the engine's kind 1, showing the live state",
          "bit 0 Formulas (off), bit 1 Gridlines (on)",
          got=got["dkind"], want=(True, 1, 2))
    check(got["disp1"] == (0, 1, 0),
          "its OK sets both: Gridlines off, Formulas on, and it closes",
          got=got["disp1"], want=(0, 1, 0))
    check(got["wkind"] == (True, 2, 0),
          "Workspace... opens as kind 2, R1C1 unchecked",
          got=got["wkind"], want=(True, 2, 0))
    check(got["a1"] == 1 and got["ref"] == "R1C1",
          "checking R1C1 switches the reference style - the box reads R1C1",
          "sh_a1style, which Formula > Reference used to flip",
          got=(got["a1"], got["ref"]), want=(1, "R1C1"))
    check(got["refdis"] == 1, "Formula > Reference is greyed (MENU_DIS)",
          "as Excel's is when no formula is being edited; mid-formula it "
          "cycles the reference at the caret (81.108, tests/sheetrefcyc)", got=got["refdis"], want=1)
    done("sheetopts")


if __name__ == "__main__":
    main()

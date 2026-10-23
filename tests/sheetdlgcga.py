#!/usr/bin/env python3
"""SHEET's tall dialogs keep their height on a CGA (SPEC.md 81.100).

    make && python3 tests/sheetdlgcga.py

A CGA's desktop band is 155 rows, and wm_fit cuts any window taller than it
(SPEC.md 11.93). SHEET's dialogs are FIXED layouts and the tall ones were
having their records cut while the package went on drawing every row - the
radio kinds' OK and Cancel landed through the bottom frame, outside the
window's clicks, and a close left them painted on the desktop (found
photographing 1.8 on CGA).

THE RADIO COLUMN IS SIZED PER KIND NOW (SPEC.md 81.115): 57 + 16 rows, so the
four-row kinds are 121 and only GALLERY, at seven rows and 169, is still
taller than the band. Gallery is the one that has to be driven here, and
Alignment joins the list (147) and Data Form (153) as a control - a change
that shrank the band or grew them would show. Border (159) is the other tall
one and is not a radio dialog.

For each, on the cycle-accurate CGA machine, read out of the window table:
  - its height is the template's own (WF_KEEPH asked for it), and
  - it still ends on the display (y + h <= 200);
then close it and compare every row below SHEET's window with the photograph
taken before it opened: nothing may be left behind.
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
import os88marty                                            # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402

WORK = "build/sheetdlgcga"
DISK = "build/sheetdlgcga.img"
NAME = "CGA.SLK"
CGA_H = 200
TITLES = ["File", "Edit", "Formula", "Format", "Data", "Options", "Macro",
          "Sheets", "Help"]
# A radio dialog's height, DERIVED the way sheet.asm derives it, so a change
# to the engine's geometry moves this with it instead of staling it: 81.115's
# SH_FDLG_H is GY1 + (ROWTOP - GY1 + rows*ROWH + 4) + BMARG + TITLE_H + 1.
SH_DLG_BMARG = 8
TITLE_H_ = 18


def fdlg_h(rows):
    return (SF.SH_FDLG_GY1
            + (SF.SH_FDLG_ROWTOP - SF.SH_FDLG_GY1 + rows * SF.SH_FDLG_ROWH + 4)
            + SH_DLG_BMARG + TITLE_H_ + 1)


# (menu, row, what, the template's height: SH_*_H, derived in sheet.asm)
DIALOGS = [("Data", 11, "radio (Gallery)", fdlg_h(7)),
           ("Format", 1, "radio (Alignment)", fdlg_h(4)),
           ("Format", 0, "list (Number)", 147),
           ("Format", 3, "Border", 159),
           ("Data", 0, "Data Form", 153)]


def main():
    os.chdir(os.path.join(HERE, ".."))
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk({(0, 0): 'Name', (0, 1): 'Qty',
                                        (1, 0): 'a', (1, 1): 1.0}))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL", src], check=True,
                   stdout=subprocess.DEVNULL)
    S = os88sym.linear
    got = []
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
        X, x = {}, x0 + 1
        for t in TITLES:
            X[t] = x + (8 * len(t) + 16) // 2
            x += 8 * len(t) + 16
        bar = y0 + 18 + 7
        item = lambda t, i: (X[t] + 6, y0 + 18 + 14 + 2 + 12 * i + 6)
        park = (x0 + w0 - 30, y0 + 18 + 14 + 26 + 7)     # grid row 1, right
        below = y0 + h0 + 1                  # first row under SHEET's frame
        print("   SHEET at", (x0, y0, w0, h0))

        # Data Form needs a database range, as Excel's does: A1:B2
        cell = lambda r, c: (x0 + 1 + 40 + 72 * c + 36,
                             y0 + 18 + 14 + 26 + 14 * r + 7)
        mo.drag(*cell(0, 0), *cell(1, 1))
        M.settle(m)
        mo.menu(X["Data"], bar, *item("Data", 4))           # Set Database
        M.settle(m)
        for t, i, what, want in DIALOGS:
            mo.to(*park)
            M.settle(m)
            before = m.fbuf()
            mo.menu(X[t], bar, *item(t, i))
            M.settle(m, limit=120)
            mo.to(*park)
            M.settle(m, limit=60)
            top = dispcp.win_list(m, S, check=False)[-1]
            rect = dispcp.win_rect(m, S, top) if top != sl else None
            os88marty.write_png_rgb(os.path.join(
                WORK, "%s.png" % what.split()[0]), *m.fbuf())
            if rect:                         # close box, then a grid click
                mo.click(rect[0] + 9, rect[1] + 9)   # takes the recovery
                M.settle(m, limit=60)                # path for the ones that
                mo.click(*park)                      # only hide
                M.settle(m, limit=60)
            after = m.fbuf()
            vw = before[0]
            a = before[2][below * vw * 3:CGA_H * vw * 3]
            b = after[2][below * vw * 3:CGA_H * vw * 3]
            nd = sum(1 for k in range(0, len(a), 3) if a[k:k + 3] != b[k:k + 3])
            left = dispcp.win_list(m, S, check=False)
            got.append((what, want, rect, nd, left[-1] == sl if left else None))

    for what, want, rect, nd, back in got:
        print("  ", what, rect, "debris px", nd)
        check(rect is not None, "%s opened" % what, "a window above SHEET's",
              got=rect)
        if rect is None:
            continue
        check(rect[3] == want, "%s keeps its full height" % what,
              "the template's %d rows (WF_KEEPH where it is taller than the "
              "band's 155)" % want,
              got=rect[3], want=want)
        check(rect[1] + rect[3] <= CGA_H, "...and still ends on the display",
              "y + h <= %d, or its bottom row is drawn nowhere" % CGA_H,
              got=rect[1] + rect[3], want="<= %d" % CGA_H)
        check(nd == 0 and back, "closing %s leaves nothing below SHEET" % what,
              "every row under SHEET's frame against the photograph taken "
              "before it opened", got="%d px, SHEET on top %r" % (nd, back),
              want="0 px, True")
    done("sheetdlgcga")


if __name__ == "__main__":
    main()

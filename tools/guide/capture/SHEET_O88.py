"""Sheet's screenshots, for tools/guide/manuals/SHEET_O88.md.

    python3 tools/guide/capture/run.py SHEET_O88 --apps build/office360.img

Sheet draws its own menu bar inside its window, which ui.menu_pick cannot
reach, so `pick`/`hold` below drive it the way tests/sheetbtn.py does: every
coordinate is read off Sheet's own state through a map of a re-assembly.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty  # noqa: E402
import os88geom  # noqa: E402

SH_MPAD, SH_MI_H = 8, 12                 # apps/sheet/sheet.asm
FILE, EDIT, FORMULA, FORMAT, DATA = 0, 1, 2, 3, 4
# rows, from sh_i_formula / sh_i_format / sh_i_data in apps/sheet/sheet.asm
FM_PFUNC, FM_GOTO = 1, 5
FT_NUMBER, FT_COLW = 0, 6                # Cell Protection... sits at 4
DT_CHART = 10                            # Form, Find, Extract... come first


def u16(b):
    return b[0] | (b[1] << 8)


def sheet_syms():
    src = os.path.join(ROOT, "apps/sheet/sheet.asm")
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "t.asm"), os.path.join(d, "t.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", ROOT + "/apps/",
                        "-I", ROOT + "/apps/sheet/",
                        "-o", os.path.join(d, "t.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


def capture(ui, shot):
    m, mo = ui.m, ui.mo
    S = sheet_syms()
    ui.path("B:/MEDIA/SALES.SLK")          # the association opens Sheet on it
    w = ui.window("Sheet")
    seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym) + os88geom.W_SEG, 2))
    ui.settle()

    def rd(sym):
        return u16(m.readseg(seg, S[sym], 2))

    def press(menu):
        x = rd("sh_ox")
        for i in range(menu):
            x += u16(m.readseg(seg, S["sh_mw"] + i * 2, 2)) + SH_MPAD * 2
        mo.to(x + SH_MPAD + 2, rd("sh_oy") + 6)
        os88marty.settle(m)
        mo._edge(True)
        m.advance(frames=12)
        m.run()

    def over(item):
        mo.to(rd("sh_mrx1") + 12, rd("sh_mry1") + 2 + item * SH_MI_H + 6, l=True)
        m.advance(frames=12)
        m.run()

    def release():
        mo._edge(False)
        os88marty.settle(m)
        ui.settle()

    def pick(menu, item):
        press(menu)
        over(item)
        release()

    def button(rec, k):
        """Click button k (0 = OK, 1 = Cancel) of a dialog's record."""
        rects = u16(m.readseg(seg, S[rec], 2))
        x0, y0, x1, y1 = (u16(m.readseg(seg, rects + k * 8 + i * 2, 2))
                          for i in range(4))
        mo.click((x0 + x1) // 2, (y0 + y1) // 2)
        os88marty.settle(m)
        ui.settle()

    def park():
        """Move the pointer out of the picture."""
        mo.to(632, 440)
        os88marty.settle(m)

    def keys(*names):
        for k in names:
            m.key(k)
        ui.settle()

    def goto(ref):
        pick(FORMULA, FM_GOTO)             # Goto...
        keys(*["Backspace"] * 8)
        m.type_text(ref)
        keys("Enter")

    # wider columns so the labels read: Format > Column Width... = 10 sets
    # the SELECTED columns, so select A1:C1 first
    goto("A1")
    for _ in range(2):
        m._shifted("ArrowRight", True)
    ui.settle()
    pick(FORMAT, FT_COLW)
    keys(*["Backspace"] * 4)
    m.type_text("10")
    keys("Enter")
    goto("C2")
    park()
    shot("sheet-window", window="Sheet", pad=8)

    # the Formula menu, held open on Paste Function...
    press(FORMULA)
    over(FM_PFUNC)
    shot("sheet-menu", window="Sheet", pad=8)
    release()                              # ...which opens Paste Function
    shot("sheet-functions", window="Sheet", pad=8)
    button("sh_ldlg_btrec", 1)

    # a formula, typed: the formula bar shows it as it is entered
    goto("E6")
    m.type_text("Spread")
    keys("Tab")
    m.type_text("=MAX(B2:B13)-MIN(B2:B13)")
    park()
    shot("sheet-formula", window="Sheet", pad=8)
    keys("Enter")

    # Format > Number... on the spread: the list of number formats
    goto("F6")
    pick(FORMAT, FT_NUMBER)
    park()
    shot("sheet-number", window="Sheet", pad=8)
    button("sh_ldlg_btrec", 1)

    # Data > Chart Column... on Revenue
    goto("C2")
    pick(DATA, DT_CHART)
    park()
    shot("sheet-chart")

#!/usr/bin/env python3
"""Edit > Undo, and Redo (SPEC.md 81.57).

    make && python3 tests/sheetundo.py

SHEET's Edit menu opened on "Can't Undo", greyed, and meant it: every entry
and every command was final. Excel 2.1's Undo is one level - the last cell
entry, or the last command of the Edit menu or Data Sort - and then offers
Redo; what it cannot reverse (a format, a name, a note) ends it.

Everything here is driven through the real menus and read back off the glass
(tests/glass.py), with a SYLK save at the end for what the glass cannot say:

  1. an ENTRY typed into A3 is undone, and redone
  2. a PASTE of A1:A2 into C1:C2 is undone - both cells, not the last
  3. a CLEAR of B1, through its dialog, is undone - the label comes back
  4. an INSERT of a column at A is undone - A1 is itself again
  5. a paste, then a FORMAT (a column width): Undo is "Can't Undo" now, so
     choosing it must
     change nothing - a snapshot older than the format would have put the
     paste back, silently, and the format with it
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty as M                                       # noqa: E402
import os88flush                                            # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402

WORK = "build/sheetundo"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetundo.img"
CELLS = {(0, 0): 1.0, (1, 0): 2.0, (0, 1): 'x'}
EDIT = (123, 45)
ITEM = lambda i: (140, 57 + 12 * i + 2)     # the Edit menu's rows, measured
UNDO, COPY, PASTE, CLEAR, INSERT = 0, 2, 3, 4, 8


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, "SHIN.SLK")
    open(src, "wb").write(F.write_sylk(CELLS))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "APPS:build/sheet.o88",
                    "APPS:build/CHART.OVL", "APPS:build/MACRO.OVL", "APPS:" + src],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    seen = {}
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        table = glass.glyphs(m)
        dispcp.open_drive(m, mo, lambda n: m.sym(n), M.settle, letter="B")
        M.settle(m)
        mo.dblclick(*SF.APPS_FOLDER)
        M.settle(m)
        SF.open_shin(m, mo)
        M.settle(m, limit=180)
        w, h, rows = m.vram("cga")
        g = glass.grid(w, h, rows)
        if not g:
            check(False, "the grid is on the glass", "no grid")
            done("sheetundo")
            return
        ys, xs = g
        box = lambda r, c: (xs[c], ys[r], xs[c + 1], ys[r + 1])
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)

        def edit(i):
            mo.menu(EDIT[0], EDIT[1], *ITEM(i))
            M.settle(m)

        def look(tag, cells):
            """what each cell SHOWS now, read from an unselected spot"""
            mo.click(*at(3, 5))             # F4: empty, and not read
            M.settle(m)
            mo.to(634, 180)
            M.settle(m)
            _, _, rows = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rows)
            seen[tag] = {k: glass.cell_text(rows, box(*k), table, xs, ys)
                         for k in cells}

        # 1: an entry, undone and redone
        mo.click(*at(2, 0))
        M.settle(m)
        m.type_text("3\n")
        M.settle(m)
        look("1-typed", [(2, 0)])
        edit(UNDO)
        look("1-undone", [(2, 0)])
        edit(UNDO)                          # Redo, in the same place
        look("1-redone", [(2, 0)])
        # 2: a paste of TWO cells, undone - A1:A2 into C1:C2. Paste makes its
        # cells through sh_commit, one at a time; were each of them to take a
        # snapshot of its own, Undo would take back the LAST cell only, which
        # a one-cell paste could never show
        mo.click(*at(0, 0))
        M.settle(m)
        m.key("ShiftLeft", down=True, up=False)
        mo.click(*at(1, 0))
        m.key("ShiftLeft", down=False, up=True)
        M.settle(m)
        edit(COPY)
        mo.click(*at(0, 2))
        M.settle(m)
        edit(PASTE)
        look("2-pasted", [(0, 2), (1, 2)])
        edit(UNDO)
        look("2-undone", [(0, 2), (1, 2)])
        # 3: a clear, through its dialog, undone
        mo.click(*at(0, 1))
        M.settle(m)
        edit(CLEAR)
        mo.click(*SF.fdlg_xy(m, 0))                 # All
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m)
        look("3-cleared", [(0, 1)])
        edit(UNDO)
        look("3-undone", [(0, 1)])
        # 4: an inserted column, undone
        mo.click(*at(1, 0))
        M.settle(m)
        edit(INSERT)
        mo.click(*SF.fdlg_xy(m, 1))                 # Column
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m)
        look("4-inserted", [(0, 0), (0, 1)])
        edit(UNDO)
        look("4-undone", [(0, 0), (0, 1)])
        # 5: a paste, then a format: Undo can no longer reach the paste
        mo.click(*at(0, 0))
        M.settle(m)
        edit(COPY)
        mo.click(*at(0, 3))
        M.settle(m)
        edit(PASTE)                         # D1 = 1
        # Column Width, NOT Alignment: Alignment's dialog ends Undo at its own
        # OK (sh_ud_kind), so it could not tell whether the MENU ends it -
        # which Number, Border and the widths rely on. The first version used
        # Alignment and passed with that drop taken out. E's width, so D -
        # whose box was measured at the start - stays where it was
        mo.click(*at(0, 4))
        M.settle(m)
        mo.menu(SF.FORMAT_MENU[0], SF.FORMAT_MENU[1],
                SF.FORMAT_MENU[0] + 15, 57 + 12 * 6 + 2)   # Column Width...
        M.settle(m)
        m.type_text("\b\b\b9\n")
        M.settle(m)
        edit(UNDO)                          # "Can't Undo": nothing happens
        look("5-after", [(0, 3)])
        mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0], SF.SAVE_AS[1])
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['slk']))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m, limit=120)
        mo.click(*SF.SAVE_BUTTON)
        before = open(os.path.join(WORK, "SHIN.SLK"), "rb").read()
        data = None
        for _ in range(60):
            v = os88flush.Flush(marty=m).volume(1)
            if "SHIN.SLK" in v.names() and v.read("SHIN.SLK") != before:
                data = v.read("SHIN.SLK")
                break
            M.settle(m, quiet=2.0, stable=2, limit=60)

    s = lambda tag, k: seen.get(tag, {}).get(k)
    check(s("1-typed", (2, 0)) == '3', "A3 takes the typed 3",
          "it shows %r" % (s("1-typed", (2, 0)),))
    check(s("1-undone", (2, 0)) is None, "Undo Entry empties A3 again",
          "it shows %r" % (s("1-undone", (2, 0)),))
    check(s("1-redone", (2, 0)) == '3', "...and Redo Entry puts the 3 back",
          "it shows %r" % (s("1-redone", (2, 0)),))
    check(s("2-pasted", (0, 2)) == '1' and s("2-pasted", (1, 2)) == '2',
          "A1:A2 pastes into C1:C2", "C1, C2 show %r, %r"
          % (s("2-pasted", (0, 2)), s("2-pasted", (1, 2))))
    check(s("2-undone", (0, 2)) is None and s("2-undone", (1, 2)) is None,
          "Undo Paste empties BOTH cells, not only the last one pasted",
          "C1, C2 show %r, %r" % (s("2-undone", (0, 2)), s("2-undone", (1, 2))))
    check(s("3-cleared", (0, 1)) is None, "Clear All empties B1",
          "B1 shows %r" % (s("3-cleared", (0, 1)),))
    check(s("3-undone", (0, 1)) == 'x', "Undo Clear gives B1 its label back",
          "B1 shows %r" % (s("3-undone", (0, 1)),))
    check(s("4-inserted", (0, 1)) == '1', "Insert Column at A moves A1 to B1",
          "B1 shows %r" % (s("4-inserted", (0, 1)),))
    check(s("4-undone", (0, 0)) == '1' and s("4-undone", (0, 1)) == 'x',
          "Undo Insert puts A1 and B1 back", "they show %r and %r"
          % (s("4-undone", (0, 0)), s("4-undone", (0, 1))))
    check(s("5-after", (0, 3)) == '1', "a FORMAT ends Undo: choosing it then "
          "leaves the paste into D1 where it is",
          "D1 shows %r" % (s("5-after", (0, 3)),))
    got = F.read_sylk(data) if data else {}
    want = {(0, 0): 1.0, (1, 0): 2.0, (2, 0): 3.0, (0, 1): 'x', (0, 3): 1.0}
    bad = {k: (v, got.get(k)) for k, v in want.items() if got.get(k) != v}
    extra = [k for k in got if k not in want]
    check(data is not None and not bad and not extra,
          "the saved document is exactly what the undo and redo left",
          "differs at %r, extra %r" % (bad, extra))
    done("sheetundo")


if __name__ == "__main__":
    main()

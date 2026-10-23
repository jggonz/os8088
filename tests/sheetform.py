#!/usr/bin/env python3
"""Data > Form - one record at a time, in a dialog (SPEC.md 81.71.5).

    make && python3 tests/sheetform.py

Form is the one database command that touches no criteria at all: it reads
the DATABASE name and nothing else, and it is a whole dialog engine of its
own - the sixth. It is also the first dialog in this app to live in
CHART.OVL (81.71.5.1), reached through resident thunks, so this row gates
THAT arrangement as much as the feature: a callback that ran before the
module was in would paint an empty window.

The database (A1:C4) is three records over three fields:

    Name    Type   Amount
    Apple   Fruit  10
    Beet    Veg    20
    Cherry  Fruit  30

  1. Form opens on `1 of 3` with the field NAMES read out of the header row
     and Apple's own values beside them
  2. Next walks to `2 of 3` and Beet's values - the counter and the fields
     move together
  3. Typing over the focused field and pressing Enter writes the change back
     to the CELL, which the saved file then carries: A3 is Plum, not Beet
  4. New appends a record and grows DATABASE to cover it, so the counter
     reads `4 of 4`
  5. Close leaves the user's own selection where it was and not on whichever
     field was last edited - sh_df_savefld banks it across sh_commit, and
     without that the cursor ends up in the middle of the database

THE DIALOG IS FOUND, NOT DERIVED. It is a top-level window centred on the
live screen, and the arithmetic for that ("(screen - W) / 2, clamped under
the menu bar") does NOT agree with where it actually lands - it was six
pixels out vertically and five horizontally the first time this was written
that way, which reads as "every field is empty" rather than as a coordinate
being wrong. So the counter is SEARCHED for by its own `n of m` shape and
everything else is measured from it, which is also the standing rule about
never trusting a remembered pull-down offset, one dialog along.
"""
import os
import re
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
import os88sym                                              # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402

WORK = "build/sheetform"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetform.img"
NAME = "FORM.SLK"
DATA = (311, 45)
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)
FORM, SET_DB = 0, 4                     # 81.71's Excel-order Data menu

# --- sheet.asm's own SH_DF_*, restated. Only the OFFSETS are used: the
# origin comes off the glass (see the module docstring).
DF_ROWTOP, DF_ROWH = 20, 16
DF_LBLX, DF_BOXX1 = 8, 104
DF_BTW, DF_BTGAP, DF_BTY1, DF_BTY2 = 64, 8, 108, 126
PREV, NEXT, NEW, DELETE, CLOSE = 0, 1, 2, 3, 4

DB = {
    (0, 0): 'Name',   (0, 1): 'Type',  (0, 2): 'Amount',
    (1, 0): 'Apple',  (1, 1): 'Fruit', (1, 2): 10.0,
    (2, 0): 'Beet',   (2, 1): 'Veg',   (2, 2): 20.0,
    (3, 0): 'Cherry', (3, 1): 'Fruit', (3, 2): 30.0,
}
TYPED = 'Plum'                          # what gets typed over Beet's Name


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(DB))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    seen = {}
    data = None
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
        w, h, rows = m.vram("cga")
        g = glass.grid(w, h, rows)
        if not g:
            check(False, "the grid is on the glass", "no grid")
            done("sheetform")
            return
        ys, xs = g
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)
        table = glass.glyphs(m)

        def text(rw, x1, ytop, wide=80):
            """the text in a strip, trying each placement of the glyph row"""
            for dy in (-2, -1, 0, 1, 2):
                t = glass.cell_text(rw, (x1, ytop + dy, x1 + wide,
                                         ytop + dy + 13), table)
                if t:
                    return t
            return None

        def find(rw):
            """the form's `n of m` counter, wherever the window manager put
            it. Returns (label column x, counter's glyph top)."""
            for y in range(34, 60):
                for x in range(128, 168):
                    t = glass.cell_text(rw, (x, y, x + 72, y + 13), table)
                    if t and re.fullmatch(r"\d+ of \d+", t):
                        return x, y
            return None

        found = [None]

        def look(tag):
            mo.to(634, 190)
            M.settle(m)
            _, _, rw = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rw)
            g = find(rw)
            if not g:
                seen[tag] = {}
                return
            lx, cy = g
            found[0] = g
            row = lambda i: cy + 19 + i * DF_ROWH
            seen[tag] = {
                'n': text(rw, lx, cy, 72),
                'l0': text(rw, lx, row(0)),
                'l1': text(rw, lx, row(1)),
                'l2': text(rw, lx, row(2)),
                'v1': text(rw, lx + DF_BOXX1 - DF_LBLX + 4, row(1)),
                'v2': text(rw, lx + DF_BOXX1 - DF_LBLX + 4, row(2)),
            }

        def btn(i):
            """button i's centre, from the origin the counter just gave"""
            lx, cy = found[0]
            ox, oy = lx - DF_LBLX, cy - 4
            return (ox + 4 + i * (DF_BTW + DF_BTGAP) + DF_BTW // 2,
                    oy + (DF_BTY1 + DF_BTY2) // 2)

        # --- A1:C4 -> DATABASE, through the real menu on a real selection ---
        mo.click(*at(0, 0))
        M.settle(m)
        m.key("ShiftLeft", down=True, up=False)
        mo.click(*at(3, 2))
        m.key("ShiftLeft", down=False, up=True)
        M.settle(m)
        menu = lambda i: (mo.menu(DATA[0], DATA[1], *ITEM(DATA[0], i)),
                          M.settle(m))
        menu(SET_DB)
        mo.click(*at(0, 0))                 # a known selection to come back to
        M.settle(m)

        menu(FORM)
        M.settle(m, limit=180)
        look("1-open")
        mo.click(*btn(NEXT))
        M.settle(m)
        look("2-next")
        # type over the focused field - Name, on record 2 - and ENTER, which
        # is "the first field of the next record" and so commits it
        m.type_text("\b" * 8 + TYPED + "\n")
        M.settle(m, limit=120)
        look("3-typed")
        mo.click(*btn(NEW))
        M.settle(m, limit=120)
        look("4-new")
        mo.click(*btn(CLOSE))
        M.settle(m, limit=180)
        mo.to(634, 190)
        M.settle(m)
        _, _, rw = m.vram("cga")
        M.write_png(os.path.join(WORK, "5-closed.png"), 640, 200, rw)
        seen["ref"] = glass.cell_text(rw, (56, 57, 122, 71), table)

        before = open(os.path.join(WORK, NAME), "rb").read()
        mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0], SF.SAVE_AS[1])
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['slk']))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m, limit=120)
        mo.click(*SF.SAVE_BUTTON)
        for _ in range(90):
            v = os88flush.Flush(marty=m).volume(1)
            if NAME in v.names() and v.read(NAME) != before:
                data = v.read(NAME)
                break
            M.settle(m, quiet=2.0, stable=2, limit=60)

    s = lambda tag, k: seen.get(tag, {}).get(k)
    check(s("1-open", 'n') == '1 of 3',
          "Form opens on the first record, and says which of how many",
          "the counter reads %r" % (s("1-open", 'n'),))
    check((s("1-open", 'l0'), s("1-open", 'l1'), s("1-open", 'l2'))
          == ('Name', 'Type', 'Amount'),
          "...with the field names read out of the database's header row",
          "they read %r" % ((s("1-open", 'l0'), s("1-open", 'l1'),
                             s("1-open", 'l2')),))
    check(s("1-open", 'v1') == 'Fruit' and s("1-open", 'v2') == '10',
          "...and Apple's own values beside them",
          "Type and Amount show %r, %r"
          % (s("1-open", 'v1'), s("1-open", 'v2')))
    check(s("2-next", 'n') == '2 of 3' and s("2-next", 'v1') == 'Veg',
          "Next walks to the second record: the counter and the fields move "
          "together", "the form reads %r / %r"
          % (s("2-next", 'n'), s("2-next", 'v1')))
    check(s("4-new", 'n') == '4 of 4',
          "New appends a record and grows DATABASE to cover it",
          "the counter reads %r" % (s("4-new", 'n'),))
    check(seen.get("ref") == 'A1',
          "Close leaves the user's own selection where it was, not on the "
          "field that was last edited",
          "the reference box reads %r" % (seen.get("ref"),))
    check(data is not None, "SHEET saved the sheet", "FORM.SLK never changed")
    got = F.read_sylk(data) if data else {}
    check(got.get((2, 0)) == TYPED,
          "what was typed into the form reached the CELL: A3 is %r" % TYPED,
          "A3 holds %r" % (got.get((2, 0)),))
    check(got.get((2, 1)) == 'Veg' and got.get((2, 2)) == 20.0,
          "...and only that field of the record changed",
          "B3, C3 hold %r, %r" % (got.get((2, 1)), got.get((2, 2))))
    done("sheetform")


if __name__ == "__main__":
    main()

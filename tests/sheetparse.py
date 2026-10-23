#!/usr/bin/env python3
"""Data > Parse splits one column at FIXED CHARACTER POSITIONS (SPEC.md 81.82).

    make && python3 tests/sheetparse.py

Parse is not CSV. CSV splits on a delimiter; Parse splits at character
columns, because the data it exists for is a report another program PRINTED,
where the fields line up because they were laid out for a printer. The
fixture is exactly that - three records, name in columns 0-11, quantity
right-aligned in 12-15, price in 18-22, each row padded differently:

    New York    1234  45.60
    Jones        987  12.05
    Brown         42   7.50

Four things are checked, and two of them separate this from a delimiter
split that would otherwise look identical:

  1. **'New York' survives as ONE field.** Guess's rule is TWO spaces: a
     single space is inside a field, a run ends it. Splitting on every space
     would put 'New' and 'York' in different columns and shift every field
     after them - the single most likely wrong implementation, and the one
     this fixture is shaped to catch.
  2. **The boundaries come from the FIRST cell and are applied to all**, so
     rows 2 and 3 - which pad differently and would tokenise differently -
     must still yield 987/42 in the same column. A build that re-guessed per
     row would pass check 1 and fail this.
  3. **The fields are TYPED by their spelling**, as a CSV field is (81.40):
     1234 and 45.60 come back as NUMBERS, the names as labels.
  4. **No brackets is a refusal, not a no-op that eats the column.** Clear
     then OK must leave the sheet exactly as it was, with a marker cell
     proving the save that shows it landed.
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
import os88sym                                              # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402

WORK = "build/sheetparse"               # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetparse.img"
NAME = "SHIN.SLK"
DATA = (311, 45)                        # the Data menu, sheetsort's own
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)
PARSE = 9                               # 81.114 put Table... in at 8, so this
                                        # moved from 8 to 9 - and Parse in
                                        # EXCEL'S place, after
                                        # Series - so SHEET's own three chart
                                        # items moved down one (81.82)
# MEASURED off the open dialog, not stepped: sh_idlg's content origin is
# (187, 67) on a 640x200 screen, and these two sit on the CANCEL ROW at
# content-relative x 8..72 and 80..144 (SH_IDLG_G1X1/G2X1).
GUESS, CLEAR = (227, 131), (299, 131)

ROWS = ['New York    1234  45.60',      # name 0-7,  qty 12-15, price 18-22
        'Jones        987  12.05',      # ...each padded differently, which is
        'Brown         42   7.50']      # the whole point of check 2
WANT = [('New York', 1234.0, 45.6),
        ('Jones', 987.0, 12.05),
        ('Brown', 42.0, 7.5)]


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    cells = {(r, 0): v for r, v in enumerate(ROWS)}
    open(src, "wb").write(F.write_sylk(cells))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "build/sheet.o88",
                    "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    saved = {}
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
        mo.to(634, 180)
        M.settle(m)
        w, h, rows = m.vram("cga")
        g = glass.grid(w, h, rows)
        if not g:
            check(False, "the grid is on the glass", "no grid")
            done("sheetparse")
            return
        ys, xs = g
        at = lambda r, c: ((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)

        def select(r1, c1, r2, c2):
            mo.click(*at(r1, c1))
            M.settle(m)
            if (r1, c1) != (r2, c2):
                m.key("ShiftLeft", down=True, up=False)
                mo.click(*at(r2, c2))
                m.key("ShiftLeft", down=False, up=True)
                M.settle(m)

        def parse(button):
            mo.menu(DATA[0], DATA[1], *ITEM(DATA[0], PARSE))
            M.settle(m, limit=120)
            mo.click(*button)
            M.settle(m, limit=60)
            m.type_text("\n")           # OK: the dialog's own Enter, so no
            M.settle(m, limit=120)      # fourth button needs calibrating

        def save(tag):
            before = saved.get("last") or open(os.path.join(WORK, NAME),
                                               "rb").read()
            mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1],
                    SF.SAVE_AS[0], SF.SAVE_AS[1])
            M.settle(m)
            mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['slk']))
            M.settle(m)
            mo.click(*SF.fdlg_okxy(m))
            M.settle(m, limit=120)
            mo.click(*SF.SAVE_BUTTON)
            for _ in range(60):
                v = os88flush.Flush(marty=m).volume(1)
                if NAME in v.names() and v.read(NAME) != before:
                    saved[tag] = saved["last"] = v.read(NAME)
                    return
                M.settle(m, quiet=2.0, stable=2, limit=60)

        # --- 1: Clear, then OK. No brackets is a refusal ----------------------
        select(0, 0, 2, 0)
        parse(CLEAR)
        select(0, 5, 0, 5)              # the marker, as in sheetjust: a save
        m.type_text("9\n")              # that never happened looks exactly
        M.settle(m)                     # like a refusal that worked
        save("refused")

        # --- 2: Guess, then OK ------------------------------------------------
        select(0, 0, 2, 0)
        parse(GUESS)
        save("parsed")

    ref = F.read_sylk(saved["refused"]) if "refused" in saved else {}
    check(ref.get((0, 5)) == 9.0, "the marker proves that save landed",
          "the refusal check below asserts cells are UNCHANGED, which is "
          "also what a save that never happened shows. This is its control",
          got=ref.get((0, 5)), want=9.0)
    check(ref.get((0, 0)) == ROWS[0] and (0, 1) not in ref,
          "Clear then OK REFUSES - the column is whole and nothing is "
          "written beside it",
          "a parse with no fields must not run. The dangerous shape is a "
          "worker that writes zero fields and clears the row on the way, "
          "which would destroy the column it was asked to split",
          got=[ref.get((0, 0)), ref.get((0, 1))], want=[ROWS[0], None])

    out = F.read_sylk(saved["parsed"]) if "parsed" in saved else {}
    got = [tuple(out.get((r, c)) for c in range(3)) for r in range(3)]
    check(got[0] == WANT[0],
          "'New York' stays ONE field - Guess's rule is TWO spaces",
          "the single most likely wrong implementation splits on every "
          "space, which puts 'New' and 'York' in different columns and "
          "shifts every field after them. A single space is inside a field; "
          "a RUN of them ends it",
          got=got[0], want=WANT[0])
    check(got[1] == WANT[1] and got[2] == WANT[2],
          "...and the boundaries from the FIRST cell apply to all of them",
          "rows 2 and 3 pad differently - 8 and 9 spaces after the name - so "
          "they tokenise differently and cut identically. A build that "
          "re-guessed per row would pass the check above and fail this one",
          got=got[1:], want=WANT[1:])
    nums = [type(got[r][c]) for r in range(3) for c in (1, 2)]
    check(all(t is float for t in nums),
          "a field that spells a number IS a number",
          "the field's spelling decides, exactly as a CSV field's does "
          "(81.40) - otherwise Parse produces a grid of labels that look "
          "right and will not add up",
          got=[n.__name__ for n in nums], want="all float")
    done("sheetparse")


if __name__ == "__main__":
    main()

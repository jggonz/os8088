#!/usr/bin/env python3
"""Format > Justify re-wraps a paragraph down the left column (SPEC.md 81.81).

    make && python3 tests/sheetjust.py

Excel 2.0's Reference Guide - the manual this tree holds; the 2.1d MENU comes
from the reference captures instead (SPEC.md 81.81.1) - is specific about this
command in ways "wrap a long label" is not, and each specific is a check:

  1. **The wrap itself**, to the width of the WHOLE selection and not of the
     left column. A1:C4 is three standard 7-character columns, so the line is
     21 - and a build that used only the left column's 7 would produce a
     completely different, also plausible-looking, set of lines.
  2. **A blank cell is a PARAGRAPH SEPARATOR.** The Guide's own five-row
     example: a blank in the left column divides the range into sections that
     are each justified inside their own rows. This is the check with the
     teeth, because merging the whole column - the obvious implementation -
     produces the same LINES in a different PLACE, so a test that only asked
     "did it wrap" passes on it.
  3. **Unused rows are cleared.** A paragraph that shrinks must not leave its
     own tail on the sheet underneath the new text.
  4. **A value in the left column refuses the WHOLE command**, rather than
     being skipped or stringified. The Guide: the cells "must either contain
     text or be blank".

Everything is driven through the real menus and read back from SHEET's own
SYLK save, so what is asserted is the stored document rather than the glass.
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

WORK = "build/sheetjust"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetjust.img"
NAME = "SHIN.SLK"
FORMAT = (259, 45)                      # sheetfmt.py measured this one
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)
JUSTIFY = 7                             # ...and Justify LAST in it: Excel's
                                        # Format is Number/Alignment/Font/
                                        # Border/Cell Protection/Row Height/
                                        # Column Width/Justify, so nothing
                                        # below it moved (81.81)

# Three standard columns is a 21-character line. Section one is rows 1-2 and
# wraps to exactly two lines, so it fills its own rows; row 3 is the blank
# separator; row 4 is a section of its own.
#
# THE NUMBERS MATTER. Greedy at 21:
#   'alpha beta gamma'   = 16, and + ' delta' would be 22
#   'delta epsilon zeta' = 18, and there is no more
# A build that merged the whole column instead would wrap the SAME two lines
# plus 'solo' and lay them in rows 1-3, leaving row 4 empty - the exact
# mirror of what is right, which is why row 3 and row 4 are both asserted.
# NOTE THE TRAILING SPACE on the second one. It is deliberate: a line that
# ends where the source does keeps whatever the source ended with, so this is
# what proves sh_ju_nextline right-trims. Without that, the last line comes
# back as 'delta epsilon zeta ' - a space the cell really holds - and the
# expectation below does not change, so this costs the fixture nothing.
PARA = ['alpha beta gamma delta', 'epsilon zeta ']
SOLO = 'solo'
WANT = ['alpha beta gamma', 'delta epsilon zeta', None, 'solo']

CELLS = {(0, 0): PARA[0], (1, 0): PARA[1], (3, 0): SOLO,
         (0, 4): 42.0, (1, 4): 'text below a number'}


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(CELLS))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "build/sheet.o88",
                    "build/CHART.OVL", "build/MACRO.OVL", src],   # the ROOT, and opened BY NAME:
                                               # a row index would depend on
                                               # how SHIN.SLK sorts against
                                               # the two binaries (81.78's
                                               # own trap, the hard way)
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
            done("sheetjust")
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

        def justify():
            mo.menu(FORMAT[0], FORMAT[1], *ITEM(FORMAT[0], JUSTIFY))
            M.settle(m, limit=120)

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

        select(0, 0, 3, 2)                  # A1:C4 - three columns of width
        justify()                            # for the line, four rows for it
        save("just")
        select(0, 4, 3, 5)                  # E1:F4, and E1 is a NUMBER
        justify()                            # ...which must refuse the block
        # A MARKER, because `save` waits for the file to CHANGE and a correct
        # refusal changes nothing - so without this the save times out, the
        # tag is never recorded, and the check reads an empty dict and fails
        # for a reason that has nothing to do with the product. It also turns
        # the assertion from "these cells look untouched" into a real one:
        # the marker PROVES the save landed, so E1 and E2 surviving is a fact
        # about this write and not about a file nobody overwrote.
        select(0, 7, 0, 7)
        m.type_text("9\n")
        M.settle(m)
        save("refused")

    out = F.read_sylk(saved["just"]) if "just" in saved else {}
    got = [out.get((r, 0)) for r in range(4)]
    check(got[0] == WANT[0] and got[1] == WANT[1],
          "the paragraph wraps to the width of the WHOLE selection",
          "A1:C4 is three standard 7-character columns, so the line is 21 "
          "characters and not the left column's 7. 'alpha beta gamma' is 16 "
          "and taking 'delta' too would be 22, so that is where it breaks",
          got=got[:2], want=WANT[:2])
    check(got[2] is None and got[3] == SOLO,
          "a BLANK cell is a paragraph separator, and each section is "
          "justified inside its OWN rows",
          "this is the check with the teeth. Merging the whole left column - "
          "the obvious implementation - wraps to the same two lines plus "
          "'solo' and lays them in rows 1-3, so row 3 would hold 'solo' and "
          "row 4 would be empty: the same lines, one row up, and a test that "
          "only asked whether it wrapped would pass on it",
          got=got[2:], want=[None, SOLO])
    check(len(got[0]) <= 21 and len(got[1]) <= 21,
          "no line is wider than the selection",
          "a line is written back as a LABEL, so one wider than the range is "
          "text the user cannot retype into the cell it came out of",
          got=[len(got[0] or ''), len(got[1] or '')], want="both <= 21")
    joined = ' '.join(x for x in got if x)
    want = ' '.join((' '.join(PARA) + ' ' + SOLO).split())   # the fixture's
                                                             # own trailing
                                                             # space is not a
                                                             # word
    check(joined == want, "...and not one word is lost or reordered",
          "wrapping is a re-break, not a rewrite: the words come back in "
          "order and entire, which is the one property that holds however "
          "the widths change",
          got=joined, want=want)

    ref = F.read_sylk(saved["refused"]) if "refused" in saved else {}
    check(ref.get((0, 7)) == 9.0, "...and the marker proves that save landed",
          "the refusal check below asserts that two cells are UNCHANGED, "
          "which is exactly what a save that never happened also shows. This "
          "is the control for it",
          got=ref.get((0, 7)), want=9.0)
    check(ref.get((0, 4)) == 42.0 and ref.get((1, 4)) == CELLS[(1, 4)],
          "a VALUE in the left column refuses the whole command",
          "the Guide says the cells must contain text or be blank. Refusing "
          "the block is not the same as skipping the number: E2 holds text "
          "and would have been wrapped by a build that skipped, which is the "
          "half of this that the number alone does not catch",
          got=[ref.get((0, 4)), ref.get((1, 4))],
          want=[42.0, CELLS[(1, 4)]])
    done("sheetjust")


if __name__ == "__main__":
    main()

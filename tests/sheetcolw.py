#!/usr/bin/env python3
"""Each column its own width (SPEC.md 81.56).

    make && python3 tests/sheetcolw.py

SHEET had ONE column width for the whole sheet, set by Format > Column
Width, and an Excel file's COLWIDTH records were skipped - so a worksheet
laid out for its data opened at seven characters a column, and the formats of
81.55 filled with '#' wherever a column had been widened for them.

  1. The HOST writes a SYLK file with Walden's F;W width records - A twenty
     characters, C three - and SHEET opens it. The grid's own lines are
     measured off the glass (tests/glass.py), and what each cell shows is
     read by the kernel's glyphs: A1's label cut at twenty characters and
     running on into B1, C1's number too wide for three and filled with '#'.
  2. A click in the middle of C2, the narrow one, and a value typed: it must
     land in C2. The hit-test walks the widths now; it divided by one.
  3. Format > Column Width on B3, 10: B's lines move, and only B's.
  4. Save As SYLK and Save As Normal: the widths come back as F;W records and
     as BIFF COLWIDTH records, column by column.
  5. Edit > Insert, Column, at A: every width moves right with its column.
"""
import os
import re
import struct
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

WORK = "build/sheetcolw"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetcolw.img"
# No 'l' and no 'I' in what is read: this BIOS face draws the two alike but
# for their top row, and a glyph's top row lies under its cell's line
CELLS = {(0, 0): 'A wider heading spanning', (0, 2): 123456.0,
         (0, 3): 'Hi', (1, 0): 12345.678, (1, 1): 'x'}
# 1-based, as SYLK counts. A is TWENTY, so C's middle is 228 pixels in -
# which one width of 56 would have put in E: the hit-test cannot pass this by
# dividing, as it could at twelve (96 + 56 + 12 = 164, C either way)
WIDTHS = {1: 20, 3: 3}
# Format's 7th item. Its items are TWELVE pixels apart from y=57 - measured
# off a held-open menu, not assumed: 59 + 11 * 6 landed on Row Height
COLW_ITEM = (SF.FORMAT_MENU[0] + 15, 57 + 12 * 6 + 2)
EDIT_MENU = (123, 45)
INSERT_ITEM = (140, 57 + 12 * 8 + 2)    # Edit's 9th since 81.99 took Repeat out
COLUMN_ROW = 1                          # the Insert dialog's second radio


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    body = F.write_sylk(CELLS).decode('latin-1').split('\r\n')
    # ID first, then the F;W records - where SHEET's own writer puts them
    body[1:1] = ['F;W%d %d %d' % (c, c, w) for c, w in sorted(WIDTHS.items())]
    src = os.path.join(WORK, "SHIN.SLK")
    open(src, "wb").write('\r\n'.join(body).encode('latin-1'))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "APPS:build/sheet.o88",
                    "APPS:build/CHART.OVL", "APPS:build/MACRO.OVL", "APPS:" + src],
                   check=True, stdout=subprocess.DEVNULL)


def sylk_widths(data):
    out = {}
    for line in data.decode('latin-1').split('\r\n'):
        m = re.match(r'F;W(\d+) (\d+) (\d+)$', line)
        if m:
            a, b, w = map(int, m.groups())
            for c in range(a, b + 1):
                out[c] = w
    return out


def biff_widths(data):
    out, i = {}, 0
    while i + 4 <= len(data):
        op, ln = struct.unpack_from('<HH', data, i)
        b = data[i + 4:i + 4 + ln]
        if op == 0x0024 and ln >= 4:
            for c in range(b[0], b[1] + 1):
                out[c + 1] = struct.unpack_from('<H', b, 2)[0] / 256.0
        elif op == 0x000A:
            break
        i += 4 + ln
    return out


def save(m, mo, kind, name, before=None):
    mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0], SF.SAVE_AS[1])
    M.settle(m)
    mo.click(*SF.fdlg_xy(m, SF.FMT_ROW[kind]))
    M.settle(m)
    mo.click(*SF.fdlg_okxy(m))
    M.settle(m, limit=120)
    mo.click(*SF.SAVE_BUTTON)
    for _ in range(60):
        v = os88flush.Flush(marty=m).volume(1)
        if name in v.names() and v.read(name) != before:
            return v.read(name)
        M.settle(m, quiet=2.0, stable=2, limit=60)
    return None


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    shown, gaps1, gaps3, sylk, biff, sylk2 = {}, [], [], None, None, None
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

        def glass_now():
            mo.to(634, 180)             # the pointer off the grid: it is ink
            M.settle(m)
            w, h, rows = m.vram("cga")
            return rows, glass.grid(w, h, rows)

        def centre(g, r, c):
            ys, xs = g
            return (xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2

        rows, g = glass_now()
        M.write_png(os.path.join(WORK, "0-opened.png"), 640, 200, rows)
        ok = bool(g) and len(g[0]) >= 4 and len(g[1]) >= 6
        check(ok, "the grid is on the glass", "no grid found")
        if not ok:
            done("sheetcolw")
            return
        # --- 2 first, which moves the selection off A1 before anything reads
        # A1: C2, the three-character column, clicked in its middle
        mo.click(*centre(g, 1, 2))
        M.settle(m)
        m.type_text("7\n")
        M.settle(m)
        rows, g = glass_now()
        w, h, _ = 640, 200, None
        M.write_png(os.path.join(WORK, "1-open.png"), 640, 200, rows)
        ys, xs = g
        gaps1 = [b - a for a, b in zip(xs, xs[1:])][:5]
        box = lambda r, c: (xs[c], ys[r], xs[c + 1], ys[r + 1])
        for k in ((0, 0), (0, 1), (0, 2), (0, 3), (1, 0)):
            shown[k] = glass.cell_text(rows, box(*k), table, xs, ys)
        # --- 3: Format > Column Width on B3 --------------------------------
        mo.click(*centre(g, 2, 1))
        M.settle(m)
        mo.menu(SF.FORMAT_MENU[0], SF.FORMAT_MENU[1], *COLW_ITEM)
        M.settle(m)
        m.type_text("\b\b\b10\n")
        M.settle(m)
        rows, g = glass_now()
        M.write_png(os.path.join(WORK, "2-widened.png"), 640, 200, rows)
        if g:
            gaps3 = [b - a for a, b in zip(g[1], g[1][1:])][:5]
        # --- 4 ------------------------------------------------------------
        before = open(os.path.join(WORK, "SHIN.SLK"), "rb").read()
        sylk = save(m, mo, 'slk', "SHIN.SLK", before)
        biff = save(m, mo, 'bif', "SHIN.BIF")
        # --- 5: Insert a column at A: every width moves right with its column
        rows, g = glass_now()
        if g:
            mo.click(*centre(g, 2, 0))
            M.settle(m)
        mo.menu(EDIT_MENU[0], EDIT_MENU[1], *INSERT_ITEM)
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, COLUMN_ROW))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m)
        sylk2 = save(m, mo, 'slk', "SHIN.SLK", sylk)

    check(gaps1[:4] == [160, 56, 24, 56],
          "the columns are 20, 7, 3 and 7 characters wide on the glass",
          "the gaps between their lines are %r pixels" % (gaps1,))
    for k, want in (((0, 0), 'A wider heading span'), ((0, 1), 'ning'),
                    ((0, 2), '###'), ((0, 3), 'Hi'), ((1, 0), '12345.678')):
        check(shown.get(k) == want, "%s%d shows %r" % (chr(65 + k[1]),
                                                        k[0] + 1, want),
              "it shows %r" % (shown.get(k),))
    check(gaps3[:4] == [160, 80, 24, 56],
          "Column Width 10 on B3 widens B, and only B",
          "the gaps are %r" % (gaps3,))
    got = F.read_sylk(sylk) if sylk else {}
    check(got.get((1, 2)) == 7.0, "the click in C2's middle typed into C2",
          "C2 holds %r; B2 %r, D2 %r" % (got.get((1, 2)), got.get((1, 1)),
                                        got.get((1, 3))))
    want = {1: 20, 2: 10, 3: 3}
    check(sylk is not None and sylk_widths(sylk) == want,
          "Save As SYLK writes F;W for A, B and C", "it wrote %r"
          % (sylk_widths(sylk) if sylk else None,))
    check(biff is not None and biff_widths(biff) == {k: float(v)
                                                      for k, v in want.items()},
          "Save As Normal writes COLWIDTH for A, B and C", "it wrote %r"
          % (biff_widths(biff) if biff else None,))
    moved = sylk_widths(sylk2) if sylk2 else None
    check(moved == {2: 20, 3: 10, 4: 3},
          "Insert Column at A moves every width right with its column",
          "the widths are %r" % (moved,))
    done("sheetcolw")


if __name__ == "__main__":
    main()

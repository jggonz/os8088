#!/usr/bin/env python3
"""Excel 2.1d's twenty-one number formats (SPEC.md 81.55).

    make && python3 tests/sheetnumfmt.py

SHEET had four - General, $#,##0, #,##0 and 0% - two bits of the format
byte. A date showed as its serial, a BIFF file's other formats read back as
General, and TEXT() knew nothing past '$ , 0 # . %'. The engine behind all of
them now is one (sh_fmtcode), drawing Excel's own codes; this gate holds the
grid, the Format Number list and the file to it. TEXT() - the same engine
through the evaluator - is tests/sheeteval.py's.

  1. The HOST writes a BIFF3 file: one XF per format, twenty cells naming
     them. SHEET opens it, and each cell's TEXT is read off the glass by the
     kernel's own glyphs (tests/glass.py) - what a person sees, exactly. A
     cell is seven characters wide, so a code whose result is wider fills
     with '#', as Excel's does, and a General number too wide takes fewer
     digits rather than showing part of itself.
  2. Format > Number on an EMPTY cell, "0.00" off the list, and then a value
     typed into it: the format was waiting (a cell with no record had nowhere
     to keep one, and the old dialog skipped it).
  3. Save As Normal: every cell names the XF of its own format again, and the
     file carries Excel's twenty-one FORMAT records in Excel's order.
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty as M                                       # noqa: E402
import os88flush                                            # noqa: E402
import os88geom                                             # noqa: E402
import os88sym                                              # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import sheetxl2 as X2                                        # noqa: E402
import glass                                                 # noqa: E402

WORK = "build/sheetnumfmt"              # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetnumfmt.img"
EXCEL21 = ['General', '0', '0.00', '#,##0', '#,##0.00', '$#,##0 ;($#,##0)',
           '$#,##0 ;[Red]($#,##0)', '$#,##0.00 ;($#,##0.00)',
           '$#,##0.00 ;[Red]($#,##0.00)', '0%', '0.00%', '0.00E+00',
           'm/d/yy', 'd-mmm-yy', 'd-mmm', 'mmm-yy', 'h:mm AM/PM',
           'h:mm:ss AM/PM', 'h:mm', 'h:mm:ss', 'm/d/yy h:mm']
D = 32888.0                             # 15 January 1990, a Monday
FULL = '#' * 7                          # a cell of hash: seven characters
# (row, col): (value, Excel's format id, what the cell must show)
CASES = {
    (0, 0): (3.14159, 2, '3.14'),
    (0, 1): (1234.5, 3, '1,235'),
    (0, 2): (1234.5, 4, FULL),          # 1,234.50 is eight
    (0, 3): (1234.5, 5, '$1,235'),      # and a space, for the ')' below
    (0, 4): (-12.0, 5, '($12)'),        # the negative SECTION
    (0, 5): (0.256, 9, '26%'),
    (0, 6): (0.256, 10, '25.60%'),
    (0, 7): (12345.678, 11, FULL),      # 1.23E+04 is eight
    (1, 0): (D, 12, '1/15/90'),
    (1, 1): (D, 14, '15-Jan'),
    (1, 2): (D, 15, 'Jan-90'),
    (1, 3): (0.75, 16, '6:00 PM'),
    (1, 4): (0.75, 18, '18:00'),
    (1, 5): (D, 13, FULL),              # 15-Jan-90 is nine
    (1, 6): (2.5, 1, '3'),
    (1, 7): (1234.0, 6, '$1,234'),      # [Red]: no colour here, the text is
    (2, 0): (D + 0.75, 20, FULL),       # the same
    (2, 1): (1234.5, 7, FULL),
    (2, 2): (0.7500579, 19, FULL),      # 18:00:05 is eight
    (2, 3): (123456789.0, 0, None),     # General: see check_general
}
TYPED = (2, 7)                          # H3: formatted empty, then typed into.
# ...and H4, which is formatted while empty and LEFT that way (SPEC.md 81.77).
# A cell with a format and no value lives only in the border table's sixth
# byte, and both BIFF passes used to walk the CELL ARRAY alone - so it was
# invisible to the XF scan and to the record writer, and the format went out
# with the file saying nothing about it.
LEFTBLANK = (3, 7)
# ...and F3, which the HOST's fixture carries as a BLANK record - a format on
# a cell with no value, which is what SHEET now writes and could not read back
# (81.77). Typing into it afterwards is the proof: the format has to have been
# waiting in the border table for the typed value to come out formatted.
READBLANK, READFMT = (2, 5), 2          # format 2 is "0.00"
                                        # Not the bottom row: Enter moves the
                                        # selection down, and off the fourth
                                        # row the view scrolls under the boxes


def rec(op, body):
    return struct.pack('<HH', op, len(body)) + body


def biff3():
    ids = sorted({f for _, f, _ in CASES.values()})
    xf_of = {f: i for i, f in enumerate(ids)}
    out = rec(0x0209, struct.pack('<HHH', 0x0300, 0x0010, 0))
    out += rec(0x0231, struct.pack('<HHH', 200, 0, 0x7FFF) + b'\x04Helv')
    for f in ids:                       # font 0, format f, locked, the rest 0
        out += rec(0x0243, bytes([0, f, 1, 0]) + bytes(8))
    for (r, c), (v, f, _) in sorted(CASES.items()):
        out += rec(0x0203, struct.pack('<HHHd', r, c, xf_of[f], v))
    # ...and the BLANK record (81.77): a format, an XF, and no value at all
    out += rec(0x0201, struct.pack('<HHH', READBLANK[0], READBLANK[1],
                                   xf_of[READFMT]))
    return out + rec(0x000A, b'')


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, "NF.BIF")
    open(src, "wb").write(biff3())
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def formats(data):
    """The FORMAT records of a BIFF3 stream, in order."""
    out, i = [], 0
    while i + 4 <= len(data):
        op, ln = struct.unpack_from('<HH', data, i)
        b = data[i + 4:i + 4 + ln]
        if op == 0x001E:
            out.append(b[1:1 + b[0]].decode('latin-1'))
        elif op == 0x000A:
            break
        i += 4 + ln
    return out


def blank_xfs(data):
    """{(row, col): ixfe} for BLANK records (0x0201) in a BIFF3 stream.

    Its own reader rather than sheetxl2's biff3_xfs, and deliberately: that
    one resolves an XF to a FORMAT ID, which is more than this needs, and the
    question here is simply whether the record EXISTS at all. Before 81.77 it
    did not, so there was nothing to resolve.
    """
    out, i = {}, 0
    while i + 4 <= len(data):
        op, ln = struct.unpack_from('<HH', data, i)
        b = data[i + 4:i + 4 + ln]
        if op == 0x0201 and ln >= 6:
            r, c, xf = struct.unpack_from('<HHH', b, 0)
            out[(r, c)] = xf
        elif op == 0x000A:
            break
        i += 4 + ln
    return out


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        table = glass.glyphs(m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        ds = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, ds)
        dispcp.open_named(m, mo, S, M.settle, wx, wy, name="NF.BIF")
        M.settle(m, limit=240)
        w, h, rows = m.vram("cga")
        g = glass.grid(w, h, rows)
        if g and len(g[0]) >= 5 and len(g[1]) >= 9:
            # H3 SELECTED, which is empty and the cell part 2 formats: the
            # selection's heavier border would sit across A1's text
            ys, xs = g
            r, c = TYPED
            mo.click((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)
            M.settle(m)
        mo.to(634, 180)                 # the pointer off the grid: it is ink
        M.settle(m)
        w, h, rows = m.vram("cga")
        M.write_png(os.path.join(WORK, "1-open.png"), w, h, rows)
        g = glass.grid(w, h, rows)
        ok = bool(g) and len(g[0]) >= 5 and len(g[1]) >= 9
        check(ok, "the grid is on the glass", "see 1-open.png")
        if not ok:
            done("sheetnumfmt")
            return
        ys, xs = g
        box = lambda r, c: (xs[c], ys[r], xs[c + 1], ys[r + 1])
        shown = {k: glass.cell_text(rows, box(*k), table, xs, ys)
                 for k in CASES}

        # --- 2: Format Number on the empty, selected H3, then a value --------
        mo.menu(SF.FORMAT_MENU[0], SF.FORMAT_MENU[1],
                SF.FORMAT_MENU[0] + 15, 59)             # Format's first item
        M.settle(m)
        dlg = [x for x in os88geom.windows(m, S)
               if x.visible and x.title.startswith("Format Number")]
        check(bool(dlg), "Format > Number opens Excel's list",
              "no 'Format Number' window")
        if dlg:
            d = dlg[-1]
            cx, cy = d.x + 1, d.y + 18  # the content origin, below the title
            mo.click(cx + 60, cy + 22 + 2 + 2 * 12 + 6)  # row 2: "0.00"
            M.settle(m)
            mo.click(cx + 292, cy + 32)                  # OK
            M.settle(m)
            m.type_text("3.14159\n")
            M.settle(m)
        mo.to(634, 180)
        M.settle(m)
        w, h, rows = m.vram("cga")
        M.write_png(os.path.join(WORK, "2-typed.png"), w, h, rows)
        typed = glass.cell_text(rows, box(*TYPED), table, xs, ys)

        # --- 2b: a SECOND empty cell, formatted and LEFT EMPTY (81.77) -------
        # The same dialog and the same list row, because what is under test is
        # not WHICH format but whether a cell that never gets a value survives
        # the save carrying one.
        r, c = LEFTBLANK
        mo.click((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)
        M.settle(m)
        mo.menu(SF.FORMAT_MENU[0], SF.FORMAT_MENU[1],
                SF.FORMAT_MENU[0] + 15, 59)
        M.settle(m)
        d2 = [x for x in os88geom.windows(m, S)
              if x.visible and x.title.startswith("Format Number")]
        check(bool(d2), "Format > Number opens on the second empty cell",
              "no 'Format Number' window for H4")
        if d2:
            d = d2[-1]
            cx, cy = d.x + 1, d.y + 18
            mo.click(cx + 60, cy + 22 + 2 + 2 * 12 + 6)  # row 2: "0.00"
            M.settle(m)
            mo.click(cx + 292, cy + 32)                  # OK - and NO typing
            M.settle(m)
        mo.to(634, 180)
        M.settle(m)

        # --- 2c: the READ side - F3's format came in on a BLANK record -------
        r, c = READBLANK
        mo.click((xs[c] + xs[c + 1]) // 2, (ys[r] + ys[r + 1]) // 2)
        M.settle(m)
        m.type_text("3.14159\n")
        M.settle(m)
        mo.to(634, 180)
        M.settle(m)
        w, h, rows = m.vram("cga")
        M.write_png(os.path.join(WORK, "2c-readblank.png"), w, h, rows)
        readblank = glass.cell_text(rows, box(*READBLANK), table, xs, ys)

        # --- 3: Save As Normal -------------------------------------------------
        mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0], SF.SAVE_AS[1])
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['bif']))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m, limit=120)
        mo.click(*SF.SAVE_BUTTON)
        before = open(os.path.join(WORK, "NF.BIF"), "rb").read()
        data = None
        for _ in range(60):
            v = os88flush.Flush(marty=m).volume(1)
            if "NF.BIF" in v.names() and v.read("NF.BIF") != before:
                data = v.read("NF.BIF")
                # THE SAVED FILE IS KEPT, the way the screenshots are: a gate
                # about what a save WROTE cannot be diagnosed from the fixture
                # it read, and those are both called NF.BIF.
                open(os.path.join(WORK, "saved.bif"), "wb").write(data)
                break
            M.settle(m, quiet=2.0, stable=2, limit=60)

    for (r, c), (v, f, want) in sorted(CASES.items()):
        name = "%s%d" % (chr(65 + c), r + 1)
        got = shown[(r, c)]
        if want is None:                # General, too wide: fewer digits,
            ok = (got is not None and got != '1234567'   # never a PART of it
                  and len(got) <= 7 and 'E' in got.upper())
            check(ok, "%s: General 123456789 fits by its exponent" % name,
                  "the cell shows %r" % (got,))
            continue
        check(got == want, "%s: %r by %r shows %r" % (name, v, EXCEL21[f],
                                                       want),
              "the cell shows %r" % (got,))
    check(typed == '3.14', "H3, formatted 0.00 while EMPTY, shows 3.14159 "
          "as 3.14", "it shows %r" % (typed,))
    check(data is not None, "Save As Normal wrote NF.BIF", "no new NF.BIF")
    xf = X2.biff3_xfs(data) if data else {}
    wrong = [(k, f, xf.get(k, (None,))[0]) for k, (_, f, _) in CASES.items()
             if xf.get(k, (None,))[0] != f]
    check(not wrong, "every cell names an XF of its own format again",
          "cell, authored id, written id: %r" % wrong[:4])
    check(xf.get(TYPED, (None,))[0] == 2, "...H3's typed value included",
          "H3 names format %r" % (xf.get(TYPED, (None,))[0],))
    check(readblank == '3.14',
          "F3's format arrived on a BLANK record and was WAITING",
          "the host's fixture gives F3 an XF and no value. SHEET writes such "
          "a record now, and until 81.77 could not read one back - so its own "
          "file lost the format on the way IN as well as on the way out. "
          "3.14159 typed into it has to come out as 3.14",
          got="F3 shows %r" % (readblank,), want="'3.14'")

    # --- 81.77: the formatted EMPTY cell ----------------------------------
    bl = blank_xfs(data) if data else {}
    check(LEFTBLANK in bl,
          "a cell formatted while EMPTY is written as a BLANK record",
          "H4 was given a format and never a value. Its format lives in the "
          "border table's sixth byte and nowhere else, and both BIFF passes "
          "walked the cell array alone - so the cell was in the file not at "
          "all and the format was lost with no message",
          got="BLANK records at %r" % (sorted(bl),), want="one at H4")
    if LEFTBLANK in bl:
        check(bl[LEFTBLANK] >= 64,
              "...naming an XF that CARRIES the format, not a base one",
              "sh_biff_ixfe answers the plain format byte - zero, for a cell "
              "with no record - unless sh_xfp_scan registered the (format, "
              "border, number format) triple. The 64 base XFs are the format "
              "byte itself, so anything below 64 here means the scan never "
              "saw this cell either",
              got=bl[LEFTBLANK], want=">= 64")

    got = formats(data) if data else []
    check(got == EXCEL21, "the file carries Excel's 21 FORMAT records, in "
          "order", "it carries %r" % (got,))
    done("sheetnumfmt")


if __name__ == "__main__":
    main()

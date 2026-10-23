#!/usr/bin/env python3
"""Do SHEET's three file formats say what they mean? (SPEC.md 81.38)

    make && python3 tests/sheetfmt.py

SHEET reads and writes BIFF3, SYLK and DIF, and until this existed **nothing
outside SHEET had ever read one of its files**.  A format was working because
SHEET could open what SHEET had written.

That is the weakest evidence there is about an interchange format, because a
writer and a reader that share a misunderstanding agree with each other
perfectly.  The round trip passes, every number comes back, and the file is
one no other program can open.  Both defects this test found on its first run
are of exactly that shape, and neither was visible from inside SHEET.

**So the input is authored on the HOST.**  `tools/os88sheetfmt.py` writes a
SYLK file from the published grammar - not from `sheet.asm` - and the guest is
handed that.  A defect in SHEET's reader therefore cannot be cancelled out by
the matching defect in its writer, which is the one thing a save-then-load
round trip can never see.

The shape of a run:

  1. A 360KB disk carrying SHEET, `CHART.OVL` and the host's `SHIN.SLK`.
  2. **The file is opened by DOUBLE-CLICKING IT**, not through File>Open.
     SHEET claims `.SLK` and `.DIF` (SPEC.md 20.9), so the association starts
     the app with the document already loaded - which removes the file
     dialog, and with it every coordinate that would otherwise have to be
     calibrated to open the thing at all.
  3. File > Save As... three times, once per format.  The dialog asks for the
     FORMAT and derives the extension itself (SPEC.md 81.36), so the name is
     never typed: `SHIN.BIF`, `SHIN.SLK`, `SHIN.DIF` at the root of B:, with
     the input left untouched inside `APPS`.
  4. `os88flush` takes the floppy back off the emulator and the host reads all
     three.

WHAT EACH FORMAT CAN AND CANNOT CARRY, because a comparison that ignores this
is testing the wrong thing:

  * **DIF cannot say WHICH error a cell holds.**  Its value indicators are V,
    NA, TRUE, FALSE and ERROR - one ERROR for all seven.  So any error
    compares equal to any error in DIF, and only BIFF and SYLK are held to the
    specific one.
  * **DIF has no empty cell.**  It is a rectangle of tuples, so the cells past
    the end of a short column are written as NA.  Those are ignored rather
    than read as errors.
  * **A quoted TRUE in SYLK is ambiguous** and the format cannot fix it:
    Walden requires logical values to be quoted, which makes them
    indistinguishable from the text "TRUE".  SHEET reads it as the logical
    (81.51), so BIFF and DIF must carry a logical; SYLK's own output, which
    cannot say either way, accepts either reading.
  * **Numbers compare with a tolerance.**  BIFF keeps the bits of a double;
    SYLK and DIF keep a decimal rendering of it.

Run it against a SHEET built before the two fixes and steps 5 and 6 fail:
that is the A/B, and it is what says this test contains its own cases.
"""
import os
import struct
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

KINDS = ('bif', 'slk', 'dif', 'csv', 'txt', 'dbf')
MACHINE = "os8088_5150_cga_gla"
SYS = "build/os8088-360.img"
DISK = "build/sheetfmt.img"

# The cells the HOST authors.  One of everything the value model has that a
# text format can express, plus the two that turned out to matter:
#   B3 carries a SEMICOLON, which SYLK reserves as its field separator and
#      escapes by doubling.
#   A5 carries an ERROR, whose code BIFF numbers differently from the
#      ERROR.TYPE worksheet function.
CELLS = {
    # DATABASE-SHAPED ON PURPOSE: row 0 names the fields and rows 1..3 are
    # records, which is Excel's own database-range convention and the only
    # shape .DBF can carry. A ragged sheet would test a fixed-width typed
    # format against something it is not for.
    (0, 0): 'NAME',   (0, 1): 'QTY', (0, 2): 'PRICE',      (0, 3): 'FLAG',
    (1, 0): 'Widget', (1, 1): 12.0,  (1, 2): 1.5,          (1, 3): ('bool', True),
    (2, 0): 'a;b',    (2, 1): 7.0,   (2, 2): -2.25,        (2, 3): ('err', '#DIV/0!'),
    (3, 0): 'Gadget', (3, 1): 3.0,   (3, 2): 1234567.89,   (3, 3): 'x',
    # Column 4 is FORMULAS, authored as SYLK ;E expressions, and it is here
    # for the FORMULA record rather than for its values: two ordinary ones
    # and one VOLATILE one, so the option-flags check below has both cases.
    (0, 4): 'CALC',
    (1, 4): ('formula', 'B2+1', 13.0),
    (2, 4): ('formula', 'B3+1', 8.0),
    (3, 4): ('formula', 'RAND()', 0.5),
}

# The one cell whose value cannot be pinned, and the two that must not be
# volatile.  RAND is the only function this app emits that is both volatile
# and always numeric - NOW answers #N/A on a machine with no RTC, and every
# machine here is one (SPEC.md 81.43.1).
VOLATILE_CELL = (3, 4)
PLAIN_FORMULA = (1, 4)

# Which columns .DBF must make numeric: a dBASE field has ONE type for the
# whole column, so a column is N only if every record in it is a number.
def _isnum(v):
    return isinstance(v, float) or (isinstance(v, tuple) and v[0] == 'formula'
                                    and isinstance(v[2], float))


DBF_NUM = {c for c in range(5)
           if all(_isnum(CELLS.get((r, c))) for r in (1, 2, 3))}

# Calibrated by holding each menu open and photographing it, per the standing
# rule about pull-down offsets.  A missed click does not corrupt anything: the
# file simply is not written, and step 4 says which one.
FILE_MENU = (75, 45)
SAVE_AS = (90, 92)                  # File's 4th item, pitch 11 from y=59
# Format > Cell Protection..., item 4 on the same bar.  Every sh_fdlg dialog
# is one fixed size centred on the screen whatever its row count, so the
# radio column and OK below are the SAME coordinates the format dialog uses
# and did not need calibrating again (81.47).
FORMAT_MENU = (259, 45)
CELL_PROT = (259 + 34, 59 + 11 * 4)
# Sheets is the eighth menu.  Its x is FILE_MENU's plus the same span the VGA
# machine measures between them, which holds because the bar is one string in
# one face; the item pitch is the File menu's own 11 from y=59.
SHEETS_MENU = (75 + 424, 45)
SHEET2 = (75 + 424 + 34, 59 + 11 * 1)
# FMT_RADIO_X, FMT_Y and FMT_OK ARE GONE (81.115), and their absence is the
# point. They were screen absolutes - x 246, the rows at 55/71/87/103/119/135,
# OK at (267, 170) - and they were right only while every kind of this dialog
# was one size and so opened in one place. 81.115 gave each kind its own
# height and moved the buttons to a stacked column, and a constant that is
# wrong by a row does not fail: it clicks a LIVE row and the test reports the
# feature broken. That had already happened once here, when DBF was added and
# a pitch of 14 silently saved a .TXT.
#
# fdlg_xy(m, row) and fdlg_okxy(m) ask the window instead. Deleting the names
# rather than re-measuring them is what stops the next geometry change
# re-arming this.
# THE CHOOSER IS A DISK WINDOW SINCE THE 2026-10-14 MERGE (upstream 2657efea,
# SPEC.md 38.3), so its buttons are a COLUMN down the right edge and not a row
# along the bottom. (340, 65) was the old centre-bottom Save and now lands
# inside the FILE LIST: the click selects nothing, the chooser stays up, the
# save never happens and every later assertion reads None. 38 of 56 sheet rows
# failed that way, and the 18 that passed are exactly the ones that never open
# a chooser.
#
# DERIVED, not eyeballed - tools/os88ui.py's chooser_button_xy, read off the
# live window ('Save As' at (119,20,322,155) on this machine):
#     rgt = (w.x + 1) + (w.w - 2 - FM_CHCOLW) - 1
#     (rgt + 5 + FM_BTN_W//2,  (w.y + TITLE_H + 1) + 2 + 20*k + FM_BTN_H//2)
# k = 0 Save/Open, 1 Cancel, 2 Drive -> (403, 48), (403, 68), (403, 88).
# A row that wants this computed rather than pinned has that helper; this is
# one constant for 42 call sites on one machine, which is what it has always
# been.
SAVE_BUTTON = (403, 48)
APPS_FOLDER = (140, 67)

# SHEET's extensions against os88sheetfmt's names for the grammars.
READER = {'bif': 'biff', 'slk': 'sylk', 'dif': 'dif',
          'csv': 'csv', 'txt': 'txt', 'dbf': 'dbf'}


def build_disk():
    import subprocess
    open("build/SHIN.SLK", "wb").write(F.write_sylk(CELLS))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "APPS:build/sheet.o88",
                    "APPS:build/CHART.OVL", "APPS:build/MACRO.OVL", "APPS:build/SHIN.SLK"],
                   check=True, stdout=subprocess.DEVNULL)


def want(kind, key):
    """What this format is allowed to come back with for a cell."""
    if key == VOLATILE_CELL:
        return 'volatile-any-number'      # RAND: a number in [0,1), no more
    v = CELLS[key]
    if isinstance(v, tuple) and v[0] == 'formula':
        v = v[2]                          # a formula compares as its VALUE:
                                          # every format here writes the
                                          # result, and only BIFF also carries
                                          # the tokens
    if kind == 'dbf':
        # Row 0 becomes the FIELD NAMES, so it returns as text either way.
        # Below it, a column is numeric only if the whole column is.
        if key[0] == 0:
            return v
        if key[1] in DBF_NUM:
            return v
        if isinstance(v, tuple):
            return v[1] if v[0] == 'err' else ('TRUE' if v[1] else 'FALSE')
        return v
    if kind in ('csv', 'txt') and isinstance(v, tuple):
        # NEITHER FORMAT HAS A TYPE FIELD. An error goes out as its own
        # spelling and comes back as the TEXT of it; a logical likewise. That
        # is the format's limit, not the app's, and the file is still right.
        return v[1] if v[0] == 'err' else ('TRUE' if v[1] else 'FALSE')
    if kind == 'dif' and isinstance(v, tuple) and v[0] == 'err':
        return 'any-error'
    if isinstance(v, tuple) and v[0] == 'bool':
        # The ambiguity is introduced when the INPUT is read: SYLK cannot tell
        # a logical from the text "TRUE", so SHEET decides, and every output
        # inherits the decision.  Since 81.51 it decides the LOGICAL - the
        # spelling rule, as when one is typed - so BIFF (a BOOLERR) and DIF
        # (its TRUE indicator), which can both say so, are held to it.  They
        # took either while SHEET had no logical type to decide with.  SYLK's
        # own output quotes both alike and stays loose.
        if kind in ('bif', 'dif'):
            return v
        return 'bool-or-text'
    return v


def agrees(kind, expect, got):
    if expect == 'volatile-any-number':
        if isinstance(got, tuple) and got and got[0] == 'formula':
            got = got[2]                  # BIFF and SYLK hand back the
                                          # expression as well as the value,
                                          # and it is the value that varies
        if kind in ('csv', 'txt', 'dbf') and isinstance(got, str):
            try:
                got = float(got)          # these three have no type field
            except ValueError:
                return False
        return isinstance(got, float) and 0.0 <= got < 1.0
    if expect == 'any-error':
        return isinstance(got, tuple) and got and got[0] == 'err'
    if expect == 'bool-or-text':
        return got == ('bool', True) or got == 'TRUE'
    return F.close(expect, got)


def cell_xfs(data):
    """{(row, col): ixfe} for every cell record, and [XF_TYPE_PROT] per XF.

    81.47: a cell whose format byte is its whole formatting names one of the
    64 base XFs, and one that also carries a border or a non-default
    protection names an extra XF written after them.
    """
    cells, xfs = {}, []
    i, sheet = 0, 0
    while i + 4 <= len(data):
        op, ln = struct.unpack_from("<HH", data, i)
        if ln == 0 and op == 0:
            break
        b = data[i + 4:i + 4 + ln]
        if op in (0x0243, 0x0443) and ln >= 12:
            xfs.append(b[2] & 3)                      # bit0 locked, bit1 hidden
        elif op == 0x000A:                            # EOF ends a substream
            sheet += 1
        elif op in (0x0201, 0x027E, 0x0203, 0x0204, 0x0205,
                    0x0206, 0x0406) and ln >= 6:
            # 0x0201 IS BLANK, and it was missing from this list: a cell with
            # a format and no value (SPEC.md 81.77) is written as one, so
            # without it the very record that carries a formatted empty
            # cell's XF was invisible to the only reader that checks XFs.
            r, c, xf = struct.unpack_from("<HHH", b, 0)
            # KEYED BY SHEET TOO. It was (row, col), which is fine for a
            # single stream and silently wrong for a workbook: Sheet 2's A1 is
            # also (0, 0), so it overwrote Sheet 1's and the protection check
            # below read the wrong cell's XF. The workbook arm found this the
            # first time it ran.
            cells[(sheet, r, c)] = xf
        i += 4 + ln
    return cells, xfs


def _first_sheet(cells):
    """{(row, col): ixfe} for whichever substream actually holds cells.

    A BIFF3 stream has one; a BIFF4 workbook opens with a GLOBALS substream
    that has none, so the sheet index of the first real cell is not a constant
    and must not be assumed.
    """
    if not cells:
        return {}
    first = min(s for s, _, _ in cells)
    return {(r, c): x for (s, r, c), x in cells.items() if s == first}


def recalc_flags(data):
    """{(row, col): option flags} for every FORMULA record in a BIFF stream.

    5.50: BIFF3-4 FORMULA is 0206H/0406H, and the option flags sit at offset
    14 of the body with bit 0 = "Recalculate always".  3.11's legend requires
    that bit whenever the token array contains a VOLATILE function - of the
    five (RAND, NOW, INDIRECT, OFFSET, CELL) this app can emit three.  Written
    with the bit clear, as every record was until 81.44, a =RAND() saved by
    SHEET opens in Excel frozen at the value SHEET cached.
    """
    out = {}
    i, sheet = 0, 0
    while i + 4 <= len(data):
        op, ln = struct.unpack_from("<HH", data, i)
        if ln == 0 and op == 0:
            break
        if op == 0x000A:                    # EOF ends a substream
            sheet += 1
        elif op in (0x0206, 0x0406) and i + 4 + ln <= len(data) and ln >= 18:
            b = data[i + 4:i + 4 + ln]
            row, col = struct.unpack_from("<HH", b, 0)
            out[(sheet, row, col)] = struct.unpack_from("<H", b, 14)[0]
        i += 4 + ln
    return _first_sheet(out)


def open_shin(m, mo, name="SHIN.SLK"):
    """Open the fixture BY NAME in the front Disk window.

    dispcp.open_named, which its own docstring calls the only way a test
    should name a file. Every row that opens SHIN used to double-click the
    THIRD row, on the arithmetic that CHART.OVL and SHEET.O88 sort ahead of
    it - and SPEC.md 81.94's MACRO.OVL sorts between those two, so the third
    row became SHEET.O88 itself and seven rows opened an empty sheet.
    """
    S = lambda n: m.sym(n)
    ds = dispcp.win_list(m, S)[-1]
    wx, wy, _, _ = dispcp.win_rect(m, S, ds)
    dispcp.open_named(m, mo, S, M.settle, wx, wy, name=name)


# =============================================================================
# SHEET'S OWN MENU BAR, driven BY NAME (SPEC.md 81.99, 81.114.7)
#
# The bar in SHEET's window is the package's own, not the kernel's, so
# os88ui.menu_pick cannot reach it and every row that drives it has carried
# measured pixel constants and a counted item index. Three rows broke at once
# when Data > Table... was inserted at index 8 and pushed Parse to 9 - and
# breaking is the GOOD outcome: a shifted index that lands on another live
# item runs the wrong command, and the row then reports the feature as broken.
#
# This asks the guest instead, and nothing here can go stale:
#
#   the MENU index   from [sh_mtabp], whose entries are 6 bytes and whose
#                    second word is the items pointer - so the menu is found
#                    by the ADDRESS of its own item array
#   the ITEM index   from the assembler's map: sh_i_edit.clear's offset minus
#                    sh_i_edit's, over two. The labels are in the source
#                    beside the items they name (sh_i_edit's own rows), so an
#                    inserted item moves the label with it
#   the title's x    from [sh_mw], the per-title widths sh_mboxof adds up
#   the item's y     from [sh_mry1] and SH_MI_H, read WITH THE BUTTON DOWN
#                    once the pull-down is on screen
#
# ...and it ASSERTS [sh_mopen] mid-hold, so a press that landed on the wrong
# title fails here rather than three steps later.
# =============================================================================
SH_MPAD, SH_MBAR_H, SH_MI_H = 8, 14, 12     # sheet.asm's own three


def _w(m, seg, off):
    v = m.readseg(seg, off, 2)
    return v[0] | (v[1] << 8)


def menu_index(m, seg, sym, items):
    """The bar position of the menu whose items array is `items`."""
    tab = _w(m, seg, sym["sh_mtabp"])
    n = _w(m, seg, sym["sh_mcount"])
    want = sym[items]
    for i in range(n):
        if _w(m, seg, tab + i * 6 + 2) == want:
            return i
    raise AssertionError("%s is not on the bar (%d titles)" % (items, n))


def item_index(sym, items, name):
    """`items`'s row number for the label `items`.`name`."""
    return (sym["%s.%s" % (items, name)] - sym[items]) // 2


def menu_pick(m, mo, seg, sym, items, name, settle=None):
    """Press the title that owns `items`, drag to its `name` row, release."""
    mi = menu_index(m, seg, sym, items)
    ii = item_index(sym, items, name)
    x = _w(m, seg, sym["sh_ox"])
    for k in range(mi):
        x += _w(m, seg, sym["sh_mw"] + k * 2) + SH_MPAD * 2
    x += (_w(m, seg, sym["sh_mw"] + mi * 2) + SH_MPAD * 2) // 2
    y = _w(m, seg, sym["sh_oy"]) + SH_MBAR_H // 2

    def aim(_mo):
        got = m.readseg(seg, sym["sh_mopen"], 1)[0]
        assert got == mi, ("the press opened menu %d, wanted %d (%s)"
                           % (got, mi, items))
        ix = (_w(m, seg, sym["sh_mrx1"]) + _w(m, seg, sym["sh_mrx2"])) // 2
        iy = _w(m, seg, sym["sh_mry1"]) + 2 + ii * SH_MI_H + SH_MI_H // 2
        return ix, iy

    mo.menu(x, y, 0, 0, settle=settle, aim=aim)


# THE TABLE LIST read straight out of the guest (SPEC.md 81.114.7/.9): the
# third carve of the border claim, SH_T_SZ=18 a record, and the first SIXTEEN
# bytes are BIFF3's DATATABLE body field for field. Reading the RECORD is how
# a row asserts that a rectangle followed the grid - the cells moving is a
# different fact, and the whole defect 81.114.9 fixes is the two disagreeing.
SH_T_SZ = 18
SH_TBL_NONE = 0xFFFF


def tbl_rec(m, seg, sym, i=0):
    """Table `i` as a dict, or None when there is no such record."""
    if i >= _w(m, seg, sym["sh_ntbl"]):
        return None
    base = _w(m, seg, sym["sh_tblseg"])
    b = m.readseg(base, i * SH_T_SZ, SH_T_SZ)
    g = lambda o: b[o] | (b[o + 1] << 8)
    return {"r1": g(0), "r2": g(2), "c1": b[4], "c2": b[5], "flags": g(6),
            "ir": g(8), "ic": g(10), "ir2": g(12), "ic2": g(14),
            "sheet": b[16]}


def tbl_rect(m, seg, sym, i=0):
    """Just (r1, r2, c1, c2) - the whole RANGE, formulas and values and all."""
    r = tbl_rec(m, seg, sym, i)
    return None if r is None else (r["r1"], r["r2"], r["c1"], r["c2"])


# THE CELL RECORDS read straight out of the guest (SPEC.md 81, 81.114.8): a
# sorted sparse array of SH_C_SZ=20 at [sh_cellseg] - packed row/sheet, col,
# flags, format, type, aux, then the IEEE-754 double.
#
# STORED, NOT PUBLISHED, and for a Data > Table result cell those are the
# same thing: it holds a stored NUMBER, which is the whole reason an ordinary
# recalculation does nothing for it. Do NOT read a FORMULA cell this way and
# call it the answer - the record's value is whatever the last evaluation
# left there, which is 81's "stored vs published" trap.
SH_C_SZ, SH_C_TYPE, SH_C_VAL = 20, 6, 8
SH_T_BLANK, SH_T_NUM, SH_T_TEXT, SH_T_BOOL, SH_T_ERR = 0, 1, 2, 3, 4


def cell_val(m, seg, sym, col, row, sheet=0):
    """The number stored in (col,row), or None when no cell record is."""
    base = _w(m, seg, sym["sh_cellseg"])
    n = _w(m, seg, sym["sh_ncells"])
    want = row | (sheet << 14)
    for i in range(n):
        off = i * SH_C_SZ
        if _w(m, base, off) == want and _w(m, base, off + 2) == col:
            if m.readseg(base, off + SH_C_TYPE, 1)[0] != SH_T_NUM:
                return None
            return struct.unpack("<d", m.readseg(base, off + SH_C_VAL, 8))[0]
    return None


# The BORDER TABLE read straight out of the guest (SPEC.md 81.55, 81.114.7):
# a sorted sparse array of SH_BT_SZ=6 records at [sh_bordseg] - row packed
# with the sheet, column, then the byte that carries the four border edges,
# the shade, the two protection bits and SH_TBL_RESULT. Reading it directly
# is how a row asserts a MARK rather than inferring one from what the guard
# then did, which is a different fact and can agree for the wrong reason.
SH_BT_SZ = 6
SH_TBL_RESULT, SH_PROT_UNLOCK, SH_PROT_HIDDEN = 0x80, 0x20, 0x40


def bt_byte(m, seg, sym, col, row, sheet=0):
    """The border byte stored for (col,row), or None when no record is."""
    base = _w(m, seg, sym["sh_bordseg"])
    n = _w(m, seg, sym["sh_nbord"])
    want = row | (sheet << 14)
    for i in range(n):
        off = i * SH_BT_SZ
        if (_w(m, base, off) == want) and (_w(m, base, off + 2) == col):
            return m.readseg(base, off + 4, 1)[0]
    return None


# The radio dialog (sh_fdlg, SPEC.md 81.74.2) answered out of its OWN rects:
# sh_fdlg_paint writes OK's and Cancel's into [sh_fdlg_btrects] as it draws
# them, screen-absolute, so there is nothing here to measure or to keep in
# agreement with the dialog's derived height (SH_FDLG_H's comment is what
# happens when there is).
# 81.115 moved this engine to Excel's shape - a titled group box with OK and
# Cancel STACKED at the top right - so the rows sit inside the box and the
# dialog is sized to ITS OWN row count rather than to the tallest kind's.
SH_FDLG_GX1, SH_FDLG_GY1, SH_FDLG_GX2 = 10, 12, 200
SH_FDLG_ROWTOP, SH_FDLG_ROWH = SH_FDLG_GY1 + 14, 16
SH_FDLG_BTX, SH_FDLG_BTW, SH_FDLG_BTH = SH_FDLG_GX2 + 10, 54, 16

# The File Format dialog's rows, BY NAME. The y they used to be clicked at was
# a screen absolute (55 + 16 * row) that was correct only while every kind of
# this dialog was the same size and so opened in the same place - the comment
# in tests/sheetcalc.py said exactly that, and 81.115 made it false. Ask the
# window where it is instead.
FMT_ROW = {'bif': 0, 'slk': 1, 'dif': 2, 'csv': 3, 'txt': 4, 'dbf': 5}


def _fdlg_content(m):
    """The front window's content origin - the dialog's, when one is up.

    Kernel-side on purpose: most of the rows that drive these dialogs are
    glass-and-file tests with no package symbols at all, so a helper that
    wanted [sh_fdlg_ox] would have to teach thirty-five files how to find
    SHEET's segment. The dialog is the frontmost window and the kernel's own
    record says where it is.
    """
    import os88geom as geom
    addr = geom.top(m)
    assert addr, "no window is frontmost: the dialog never opened"
    x, y = struct.unpack_from("<HH", bytes(m.read(addr + geom.W_X, 4)), 0)
    return x + 1, y + geom.TITLE_H


def fdlg_xy(m, row):
    """Where a radio dialog's row `row` IS, derived from the live window."""
    ox, oy = _fdlg_content(m)
    return (ox + SH_FDLG_GX1 + 12,
            oy + SH_FDLG_ROWTOP + SH_FDLG_ROWH * row + 6)


def fdlg_okxy(m):
    """...and where its OK button is - the top of the stacked pair."""
    ox, oy = _fdlg_content(m)
    return (ox + SH_FDLG_BTX + SH_FDLG_BTW // 2,
            oy + SH_FDLG_GY1 + SH_FDLG_BTH // 2)


# sheet.asm's SH_CALC_* - the MODE values, which are the radio rows and are
# also GET.DOCUMENT(14)'s numbering less one (81.114.8).
CALC_AUTO, CALC_AUEX, CALC_MANUAL = 0, 1, 2


def radio_row(m, seg, sym, items, label, cap=8):
    """The row of `items` that shows the string at `label`.

    A radio dialog's rows are an array of label POINTERS (sh_fd_i_calc and
    its kin), so the row number is derivable from the name of the text it
    shows, and a row inserted above it moves the answer with it.

    MEASURED NECESSARY: 81.114.8 put Excel's "Automatic except Tables"
    between Automatic and Manual, and two rows that had written `MANUAL = 1`
    went on clicking row 1 - which is a LIVE row, so both silently selected
    the new middle mode and reported the feature as broken. That is the
    dialog-shaped twin of the menu index that shifted when Data > Table...
    went in at 8, and it is the same answer: ask, do not count.
    """
    want = sym[label]
    base = sym[items]
    for i in range(cap):
        if _w(m, seg, base + i * 2) == want:
            return i
    raise AssertionError("%s is not one of %s's first %d rows"
                         % (label, items, cap))


def fdlg_ok(m, mo, seg, sym, row=None):
    """Pick `row` if asked (None keeps the default) and press OK."""
    if row is not None:
        mo.click(_w(m, seg, sym["sh_fdlg_ox"]) + SH_FDLG_GX1 + 12,
                 _w(m, seg, sym["sh_fdlg_oy"]) + SH_FDLG_ROWTOP
                 + SH_FDLG_ROWH * row + 6)
        M.settle(m)
    b = sym["sh_fdlg_btrects"]
    x1, y1, x2, y2 = (_w(m, seg, b + k) for k in (0, 2, 4, 6))
    assert x2 > x1 and y2 > y1, ("the dialog never painted its buttons: %r"
                                 % ((x1, y1, x2, y2),))
    mo.click((x1 + x2) // 2, (y1 + y2) // 2)


# SPEC.md 81.106: Options > Display... is the check-box engine's kind 1, and
# its geometry is the Border dialog's (sheet.asm's SH_BDLG_*): a box's glyph
# at content (GX1 + 8, ROWTOP + i * ROWH), OK stacked right at (GX2 + 10 ..
# W - 10, 20 .. 40). Rows: 0 Formulas, 1 Gridlines, Excel's order
OPTIONS_MENU = (371, 45)                    # the CGA machine's Options title
BDLG_GX1, BDLG_GX2, BDLG_W, BDLG_ROWTOP, BDLG_ROWH = 10, 104, 190, 26, 18
DISP_FORMULAS, DISP_GRIDLINES = 0, 1


def display_toggle(m, mo, box, title=OPTIONS_MENU, row=0):
    """Options > Display..., flip ONE box, OK. `title` is the Options menu's
    title on this machine and `row` Display's row in it (SH_OI_DISPLAY).
    The dialog is found through the window table, not assumed where it opens.
    Returns True when a dialog opened and was answered."""
    S = lambda n: m.sym(n)
    before = dispcp.win_list(m, S, check=False)
    mo.menu(title[0], title[1], title[0] + 17, 57 + 12 * row + 2)
    M.settle(m)
    wins = dispcp.win_list(m, S, check=False)
    if len(wins) <= len(before) and wins[-1:] == before[-1:]:
        return False
    x, y, _, _ = dispcp.win_rect(m, S, wins[-1])
    cx, cy = x + 1, y + 18                  # the content origin
    mo.click(cx + BDLG_GX1 + 8 + 4, cy + BDLG_ROWTOP + BDLG_ROWH * box + 4)
    M.settle(m)
    mo.click(cx + (BDLG_GX2 + 10 + BDLG_W - 10) // 2, cy + 30)
    M.settle(m)
    return True


def main():
    build_disk()
    with M.launch(SYS, apps=DISK, machine=MACHINE) as m:
        M.settle(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, lambda n: m.sym(n), M.settle, letter="B")
        M.settle(m)
        mo.dblclick(*APPS_FOLDER)
        M.settle(m)
        open_shin(m, mo)                # the ASSOCIATION opens it
        M.settle(m, limit=180)

        # A1 is the selected cell on load, so this needs no cell click:
        # mark it UNLOCKED, which travels in the same byte as a border and
        # through the same (format, border) pair table, so it exercises the
        # whole of 81.47's extra-XF mechanism without the Border dialog.
        mo.menu(FORMAT_MENU[0], FORMAT_MENU[1], CELL_PROT[0], CELL_PROT[1])
        M.settle(m)
        mo.click(*fdlg_xy(m, 1))                  # row 1 = Unlocked. THIS IS
                                                  # Cell Protection, not File
                                                  # Format: it borrowed
                                                  # FMT_Y['slk'] only because
                                                  # that row happened to be
                                                  # the same y
        M.settle(m)
        mo.click(*fdlg_okxy(m))
        M.settle(m, limit=120)

        for kind in KINDS:
            mo.menu(FILE_MENU[0], FILE_MENU[1], SAVE_AS[0], SAVE_AS[1])
            M.settle(m)
            mo.click(*fdlg_xy(m, FMT_ROW[kind]))
            M.settle(m)
            mo.click(*fdlg_okxy(m))
            M.settle(m, limit=120)
            mo.click(*SAVE_BUTTON)
            M.settle(m, limit=180)

        # --- THE WORKBOOK ARM, last so it disturbs nothing above ----------
        # Everything so far is one sheet, which is the only shape a BIFF3
        # stream has.  Put two cells on Sheet 2 and save Normal again: SHEET
        # writes a BIFF4 WORKBOOK the moment a second grid is used (81.10.5),
        # and until 81.47.7 the host could not read one at all - which is the
        # blind spot 81.47.6's bug lived in.
        #
        # No cell click is needed for the same reason the protection step
        # needs none: switching sheets leaves a cell selected, and typing goes
        # to it.  SHIN.BIF is overwritten, and the checks above are unharmed
        # because read_biff answers with the FIRST sheet either way.
        mo.menu(SHEETS_MENU[0], SHEETS_MENU[1], SHEET2[0], SHEET2[1])
        M.settle(m)
        m.type_text("77"); m.key("Enter"); M.settle(m)
        m.type_text("88"); m.key("Enter"); M.settle(m)
        mo.menu(FILE_MENU[0], FILE_MENU[1], SAVE_AS[0], SAVE_AS[1])
        M.settle(m)
        mo.click(*fdlg_xy(m, FMT_ROW['bif']))
        M.settle(m)
        mo.click(*fdlg_okxy(m))
        M.settle(m, limit=120)
        mo.click(*SAVE_BUTTON)
        M.settle(m, limit=240)

        vol = os88flush.Flush(marty=m).volume(1)
        names = vol.names()
        got = {}
        biff_raw = book_raw = None
        for kind in KINDS:
            name = 'SHIN.%s' % kind.upper()
            check(name in names, "%s written" % name,
                  "%s is not on the disk - the save for it did not happen "
                  "(names: %s)" % (name, names))
            if name in names:
                raw = vol.read(name)
                if kind == 'bif':
                    biff_raw = raw
                    book_raw = raw
                got[kind] = F.read(name, data=raw, kind=READER[kind])

    for kind in KINDS:
        if kind not in got:
            continue
        cells = got[kind]
        bad = []
        for key in sorted(CELLS):
            expect = want(kind, key)
            if key not in cells:
                bad.append('%r missing' % (key,))
            elif not agrees(kind, expect, cells[key]):
                bad.append('%r: authored %r, %s says %r'
                           % (key, CELLS[key], kind, cells[key]))
        check(not bad, "%s round trip" % kind.upper(),
              "the host wrote a SYLK file, SHEET read it and wrote %s, and "
              "the host disagrees about %d cell(s): %s"
              % (kind.upper(), len(bad), '; '.join(bad)))

    if book_raw is not None:
        try:
            book = F.read_biff_book(book_raw)
        except Exception as e:                      # noqa: BLE001 - reported
            book = []
            check(False, "the workbook reads back",
                  "read_biff_book refused SHIN.BIF: %s" % e)
        if book:
            check(len(book) == 2,
                  "a second sheet makes it a WORKBOOK",
                  "SHIN.BIF came back as %d sheet(s) - %r. Two grids in use "
                  "must produce a BIFF4 workbook (81.10.5); one sheet means "
                  "either the sheet switch did not happen or the writer did "
                  "not notice" % (len(book), [n for n, _ in book]))
        if len(book) == 2:
            first = book[0][1]
            check(first.get((1, 1)) == 12.0,
                  "...whose first sheet is still the authored one",
                  "sheet 1 B2 reads %r, wanted the 12.0 the host authored - "
                  "the workbook must not disturb the sheet that was already "
                  "there" % (first.get((1, 1)),))
            second = book[1][1]
            vals = sorted(v for v in second.values()
                          if isinstance(v, float))
            check(vals == [77.0, 88.0],
                  "...and the second carries its own cells",
                  "sheet 2 holds %r, wanted [77.0, 88.0]. This is the arm "
                  "that 81.47.6 needed and did not have: a bug that writes "
                  "sheet 2's cells against sheet 1 shows up here and nowhere "
                  "else" % (vals,))

    if biff_raw is not None:
        cells, xfs = cell_xfs(biff_raw)
        check(len(xfs) > 64,
              "an unlocked cell gets an XF of its own",
              "the file carries %d XF records; 81.47 writes 64 base ones and "
              "then one per (format, border/protection) pair, so A1 being "
              "unlocked should have produced a 65th" % len(xfs))
        a1 = _first_sheet(cells).get((0, 0))
        check(a1 is not None and a1 >= 64 and not (xfs[a1] & 1),
              "...and that XF says the cell is unlocked",
              "A1 names XF %r, whose XF_TYPE_PROT is %r - wanted an index at "
              "64 or above with the locked bit CLEAR"
              % (a1, None if a1 is None or a1 >= len(xfs) else xfs[a1]))
        ctl = _first_sheet(cells).get((1, 1))
        check(ctl is not None and ctl < 64 and (xfs[ctl] & 1),
              "a cell nobody touched stays locked, on a base XF",
              "B2 names XF %r, whose XF_TYPE_PROT is %r - wanted an index "
              "below 64 with the locked bit SET, which is Excel's default and "
              "what every XF this app wrote before 81.47 got wrong"
              % (ctl, None if ctl is None or ctl >= len(xfs) else xfs[ctl]))

        flags = recalc_flags(biff_raw)
        check(len(flags) >= 3,
              "the formula column is written as FORMULA records",
              "found %d FORMULA record(s), wanted the three in column 4 - a "
              "token array SHEET declines to emit falls back to a plain "
              "NUMBER record, and then there is nothing here to carry the "
              "flag (%r)" % (len(flags), sorted(flags)))
        v = flags.get(VOLATILE_CELL)
        check(v is not None and (v & 1),
              "a volatile formula sets Recalculate always",
              "the =RAND() cell's FORMULA option flags are %r, wanted bit 0 "
              "set (5.50) - Excel would show SHEET's cached value forever"
              % (v,))
        q = flags.get(PLAIN_FORMULA)
        check(q is not None and not (q & 1),
              "an ordinary formula does not",
              "the =B2+1 cell's flags are %r, wanted bit 0 clear - a blanket "
              "'always recalculate' is not the fix, it just moves the "
              "question" % (q,))

    done("sheetfmt")


if __name__ == "__main__":
    main()

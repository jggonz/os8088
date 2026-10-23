#!/usr/bin/env python3
"""What SHEET's READERS make of a logical value (SPEC.md 81.51).

    make && python3 tests/sheetbool.py

tests/sheeteval.py holds what the evaluator answers and tests/sheetfmt.py
what the writers say; this row is the other direction. Until 81.51 SHEET had
no logical type, so every reader flattened one: a BIFF BOOLERR became the
number 1, DIF's TRUE indicator left the cell BLANK, a CSV field reading TRUE
became a label, and a dBASE L field a one-letter label.

The host authors one small file per format - DIF, CSV, dBASE and BIFF3, by
hand from the published layouts rather than from sheet.asm - SHEET opens each
and saves it as Normal (BIFF3), and the host reads that back. BIFF is the
witness because it is the one format whose record SAYS which it is: a
BOOLERR with the flag clear is a logical, a LABEL is text and a NUMBER is a
number. SYLK would not do - Walden quotes a logical exactly as it quotes
the text "TRUE".

Every reader keeps its format's own word for it and nothing looser:
  DIF    the TRUE and FALSE value indicators
  CSV    a field spelled TRUE or FALSE, any case - what typing it does, and
         what Excel does with the field; TRUEX stays a label
  dBASE  an L field: T or Y, F or N, either case; '?' is not yet known, and
         a blank cell
  BIFF   BOOLERR with the flag clear (and an error BOOLERR, set, still an
         error - the same record)
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
import os88sheetfmt as F                                     # noqa: E402
import os88sym                                              # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
import os88ui                                                # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402

WORK = "build/sheetbool"                # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetbool.img"
TR, FA = ('bool', True), ('bool', False)
CRLF = "\r\n"
# SHEET claims .SLK, .DIF and .BIF (SPEC.md 54.6), so the first file opens on a
# double-click and the rest through File > Open, picked from its LIST: the
# name field under it takes no typing there. The list is alphabetical, six
# rows at a 16-pixel pitch, and grows by one with every save - so the files
# are taken in the order below, which keeps each one inside those six rows
OPEN_ITEM = (SF.FILE_MENU[0] + 15, 59 + 11)     # File's 2nd item
LIST_X, LIST_Y0, LIST_DY, LIST_ROWS = 150, 67, 16, 6
OPEN_BUTTON = SF.SAVE_BUTTON                    # the same dialog, the same place


def dif(rows):
    """DIF as Walden lays it out: the four header items, then one BOT tuple
    per row, a numeric item being `0,value` and its indicator line."""
    ncols = max(len(r) for r in rows)
    out = ["TABLE", "0,1", '""', "VECTORS", "0,%d" % ncols, '""',
           "TUPLES", "0,%d" % len(rows), '""', "DATA", "0,0", '""']
    for r in rows:
        out += ["-1,0", "BOT"]
        for v in r:
            if v is True or v is False:
                out += ["0,%d" % int(v), "TRUE" if v else "FALSE"]
            else:
                out += ["0,%s" % v, "V"]
    out += ["-1,0", "EOD"]
    return (CRLF.join(out) + CRLF).encode("latin-1")


def dbf(fields, recs):
    """dBASE III: a 32-byte header, a 32-byte descriptor per field, 0DH,
    then each record as a deletion flag and its fixed-width fields."""
    hlen = 32 + 32 * len(fields) + 1
    rlen = 1 + sum(w for _, _, w in fields)
    out = struct.pack('<B3BIHH20x', 0x03, 126, 9, 10, len(recs), hlen, rlen)
    for name, ty, w in fields:
        out += struct.pack('<11sc4xBB14x', name.encode(), ty.encode(), w, 0)
    out += b'\x0d'
    for r in recs:
        out += b' ' + b''.join(v.encode().rjust(w) if ty == 'N'
                               else v.encode().ljust(w)
                               for v, (_, ty, w) in zip(r, fields))
    return out + b'\x1a'


def rec(op, body):
    return struct.pack('<HH', op, len(body)) + body


def bif():
    """BIFF3: BOF, three BOOLERR cells - TRUE, FALSE and #DIV/0! (07H, with
    the error flag set) - and EOF."""
    cells = ((0, 1, 0), (1, 0, 0), (2, 0x07, 1))
    body = b''.join(rec(0x0205, struct.pack('<HHHBB', 0, c, 0, v, e))
                    for c, v, e in cells)
    return (rec(0x0209, struct.pack('<HHH', 0x0300, 0x0010, 0)) + body
            + rec(0x000A, b''))


FILES = {                               # in the order they are opened
    "LX.BIF": bif(),
    "LB.DBF": dbf([("OK", 'L', 1), ("QTY", 'N', 5)],
                  [("T", "1"), ("n", "2"), ("?", "3"), ("y", "4")]),
    "LC.CSV": b"TRUE,false,5,TRUEX\r\n",
    "LD.DIF": dif([[True, False, 5]]),
}
# (row, col) -> what the saved BIFF must hold; None = no cell at all
WANT = {
    "LD.DIF": {(0, 0): TR, (0, 1): FA, (0, 2): 5.0},
    "LC.CSV": {(0, 0): TR, (0, 1): FA, (0, 2): 5.0, (0, 3): 'TRUEX'},
    # row 0 is the field NAMES, as SHEET's dBASE reader lays a table out
    "LB.DBF": {(0, 0): 'OK', (1, 0): TR, (2, 0): FA, (3, 0): None,
               (4, 0): TR, (1, 1): 1.0},
    "LX.BIF": {(0, 0): TR, (0, 1): FA, (0, 2): ('err', '#DIV/0!')},
}


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    for n, data in FILES.items():
        open(os.path.join(WORK, n), "wb").write(data)
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL", "build/MACRO.OVL"]
                   + [os.path.join(WORK, n) for n in FILES],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    saved = {}
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)                   # four saves outlast its timeout
        mo = Mouse(marty=m)
        ui = os88ui.UI(m, mouse=mo)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        ds = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, ds)
        vol = lambda: os88flush.Flush(marty=m).volume(1)
        for i, name in enumerate(FILES):
            if i == 0:
                dispcp.open_named(m, mo, S, M.settle, wx, wy, name=name)
            else:
                # what the dialog lists: the kernel's species filter drops
                # hidden and system files (ASSOC.DAT) from every listing
                shown = sorted(e.name for e in vol().listdir()
                               if not e.is_system and not e.is_dir)
                row = shown.index(name)
                check(row < LIST_ROWS, "%s is on the Open list's first page"
                      % name, "row %d of %s" % (row, shown))
                mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], *OPEN_ITEM)
                M.settle(m)
                ui.chooser_open(name)   # BY NAME: the chooser is a Disk
                                        # window since the 2026-10-14 merge
                                        # (38.3), so LIST_* and OPEN_BUTTON
                                        # were both the old geometry
            M.settle(m, limit=240)
            out = name.split('.')[0] + ".BIF"
            before = FILES.get(out)         # LX.BIF is saved over itself
            mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0],
                    SF.SAVE_AS[1])
            M.settle(m)
            mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['bif']))
            M.settle(m)
            mo.click(*SF.fdlg_okxy(m))
            M.settle(m, limit=120)
            mo.click(*SF.SAVE_BUTTON)
            for _ in range(60):
                v = vol()
                if out in v.names() and v.read(out) != before:
                    saved[name] = v.read(out)
                    break
                M.settle(m, quiet=2.0, stable=2, limit=60)
            if os.environ.get("SHEETBOOL_SHOT"):
                w, h, rows = m.vram("cga")
                M.write_png(os.path.join(WORK, name + ".png"), w, h, rows)
    for name, want in WANT.items():
        data = saved.get(name)
        check(data is not None, "%s opened and saved as Normal" % name,
              "no new %s.BIF on B:" % name.split('.')[0])
        got = F.read_biff(data) if data else {}
        for key, w in sorted(want.items()):
            g = got.get(key)
            ok = (g is None if w is None
                  else F.close(w, g) if isinstance(w, float) else g == w)
            check(ok, "%s %s%d reads as %r" % (name, chr(65 + key[1]),
                                               key[0] + 1, w),
                  "SHEET saved %r there" % (g,))
    done("sheetbool")


if __name__ == "__main__":
    main()

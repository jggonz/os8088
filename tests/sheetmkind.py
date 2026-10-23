#!/usr/bin/env python3
"""A macro sheet reopens as a macro sheet (macro plan wave 5, SPEC.md 81.97).

    make && python3 tests/sheetmkind.py

The host writes two BIFF2 files, the way Excel 2.1d does: MAC.BIF, whose BOF
says dt 0040H (a macro sheet), and WS.BIF, whose BOF says 0010H. MAC.BIF's
formulas are the reader half's four cases:

  A1  SELECT("B2")      ptg 58H: a COMMAND, numbered in the Cetab, where
                        6DH is SELECT - and in the Ftab 6DH is something else
  A2  ECHO(FALSE)       ptg 42H, an EXTENSION function (wave 2), whose name
                        lives in CHART.OVL's own table and not sh_functab
  A3  DEREF(B1)         ptg 41H: an extension function with a FIXED count,
                        which only shm_mxargc knows
  A4  RETURN()          ptg 42H, one of the original twenty
  A5  a truncated 58H    refused, and keeps its cached 7
  A6  =IF($A$5,1,2)      which must still be read through the Ftab: the flag
                         a 58H sets must not outlive its own token (81.97.4)

SHEET opens MAC.BIF and saves it Normal, then opens WS.BIF through File >
Open in the same window and saves that. The host reads both saves: the first
must say 0040H and carry all four formulas, the second 0010H - the kind
belongs to the document, so opening a worksheet has to put it back.

Then the OWNERSHIP of Options > Formulas, read out of SHEET's own bytes: a
macro sheet turns it on and closing it turns it off again, but one the USER
turned on for a worksheet must survive both.
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
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
import os88sym                                              # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
from os88geom import WIN_SIZE, W_SEG                            # noqa: E402
from paintmove import pkg_syms                                  # noqa: E402

WORK = "build/sheetmkind"               # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmkind.img"
OPEN_ITEM = (SF.FILE_MENU[0] + 15, 59 + 11)     # sheetxl2's own
OPTIONS = (370, 45)                             # its menu: Formulas is a
                                                # box in Display... (81.106)
LIST_X, LIST_Y0, LIST_DY, LIST_ROWS = 150, 67, 16, 6
R = 0xC000
FALSE8 = bytes([1, 0, 0, 0, 0, 0, 0xFF, 0xFF])  # a cached logical FALSE
WANT = ['SELECT("B2")', 'ECHO(FALSE)', 'DEREF(B1)', 'RETURN()',
        None, 'IF($A$5,1,2)']


def rec(op, body):
    return struct.pack('<HH', op, len(body)) + body


def formula(r, c, toks, cached=FALSE8):
    return rec(0x0006, struct.pack('<HH', r, c) + bytes([0x40, 0, 0])
               + cached + b'\x00' + bytes([len(toks)]) + toks)


def book(dt, cells):
    return (rec(0x0009, struct.pack('<HH', 0x0002, dt)) + b''.join(cells)
            + rec(0x000A, b''))


def mac():
    ref = bytes([0x44]) + struct.pack('<HB', R | 0, 1)          # B1
    return book(0x0040, [
        formula(0, 0, b'\x17\x02B2' + b'\x58\x01\x6d'),         # SELECT
        formula(1, 0, b'\x1d\x00' + b'\x42\x01\x57'),           # ECHO
        formula(2, 0, ref + b'\x41\x5a'),                       # DEREF
        formula(3, 0, b'\x42\x00\x37'),                         # RETURN
        # 81.97.4: a TRUNCATED 58H - its count and index cut off - is refused
        # and keeps its value. The formula BELOW it must not then be read
        # through the Cetab, where 01H is OPEN and the Ftab's 01H is IF
        formula(4, 0, b'\x58', cached=struct.pack('<d', 7.0)),
        formula(5, 0, bytes([0x44]) + struct.pack('<HB', 4, 0)
                + b'\x1e\x01\x00' + b'\x1e\x02\x00' + b'\x42\x03\x01'),
    ])


def ws():
    num = rec(0x0003, struct.pack('<HH', 0, 0) + bytes([0x40, 0, 0])
              + struct.pack('<d', 42.0))
    return book(0x0010, [num])


def bofs(data):
    """every BOF's dt, in order: a BIFF4 workbook has one per substream"""
    out, i = [], 0
    while i + 4 <= len(data):
        op, ln = struct.unpack_from('<HH', data, i)
        if op in (0x0009, 0x0209, 0x0409) and ln >= 4:
            out.append(struct.unpack_from('<H', data, i + 6)[0])
        i += 4 + ln
    return out


def bof_dt(data):
    if not data or len(data) < 8:
        return None
    op, ln, vers, dt = struct.unpack_from('<HHHH', data, 0)
    return dt if op in (0x0009, 0x0209, 0x0409) else None


def main():
    os.chdir(os.path.join(HERE, ".."))
    os.makedirs(WORK, exist_ok=True)
    files = {"MAC.BIF": mac(), "WS.BIF": ws()}
    for n, d in files.items():
        open(os.path.join(WORK, n), "wb").write(d)
    # the host's own reader agrees with the fixture before SHEET sees it
    host = F.read_biff(files["MAC.BIF"])
    host_f = [(host.get((r, 0))[1]
               if isinstance(host.get((r, 0)), tuple) else None)
              for r in range(6)]
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL"] + [os.path.join(WORK, n) for n in files],
                   check=True, stdout=subprocess.DEVNULL)
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
        vol = lambda: os88flush.Flush(marty=m).volume(1)

        def shot(tag):
            _, _, rr = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + ".png"), 640, 200, rr)

        def save(out, before):
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
                    return v.read(out)
                M.settle(m, quiet=2.0, stable=2, limit=60)
            return None

        sym = pkg_syms("apps/sheet/sheet.asm")

        def sheet_byte(name):
            """one of SHEET's own bytes, off its window's W_SEG"""
            sl = dispcp.win_list(m, S, check=False)[-1]
            at = S("wm_wins") + sl * WIN_SIZE + W_SEG
            b = m.read(at, 2)
            return m.readseg(b[0] | (b[1] << 8), sym[name], 1)[0]

        def reopen(name):
            shown = sorted(e.name for e in vol().listdir()
                           if not e.is_system and not e.is_dir)
            mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], *OPEN_ITEM)
            M.settle(m)
            mo.click(LIST_X, LIST_Y0 + LIST_DY * shown.index(name))
            M.settle(m)
            mo.click(*SF.SAVE_BUTTON)           # Open: the same dialog's button
            M.settle(m, limit=240)

        dispcp.open_named(m, mo, S, M.settle, wx, wy, name="MAC.BIF")
        M.settle(m, limit=240)
        shot("1-macro")
        own = [(sheet_byte("sh_dockind"), sheet_byte("sh_showformulas"))]
        saved["MAC"] = save("MAC.BIF", files["MAC.BIF"])
        shown = sorted(e.name for e in vol().listdir()
                       if not e.is_system and not e.is_dir)
        row = shown.index("WS.BIF")
        mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], *OPEN_ITEM)
        M.settle(m)
        mo.click(LIST_X, LIST_Y0 + LIST_DY * row)
        M.settle(m)
        mo.click(*SF.SAVE_BUTTON)               # Open: the same dialog's button
        M.settle(m, limit=240)
        shot("2-worksheet")
        own.append((sheet_byte("sh_dockind"), sheet_byte("sh_showformulas")))
        saved["WS"] = save("WS.BIF", files["WS.BIF"])
        # ...and again with Formulas the USER's own
        SF.display_toggle(m, mo, SF.DISP_FORMULAS, OPTIONS)  # 81.106
        own.append((sheet_byte("sh_dockind"), sheet_byte("sh_showformulas")))
        reopen("MAC.BIF")
        own.append((sheet_byte("sh_dockind"), sheet_byte("sh_showformulas")))
        reopen("WS.BIF")
        own.append((sheet_byte("sh_dockind"), sheet_byte("sh_showformulas")))
        shot("3-userformulas")
        # a macro sheet with a SECOND sheet is saved as a BIFF4 WORKBOOK
        # (81.10.5), and every sheet substream's own BOF must say it too
        reopen("MAC.BIF")
        mo.menu(SF.SHEETS_MENU[0], SF.SHEETS_MENU[1], SF.SHEET2[0],
                SF.SHEET2[1])
        M.settle(m)
        m.type_text("77")
        m.key("Enter")
        M.settle(m)
        saved["WB"] = save("MAC.BIF", saved["MAC"])

    for k, d in saved.items():
        if d:
            open(os.path.join(WORK, k + ".out.bif"), "wb").write(d)
    check(host_f == WANT, "the host reads the fixture's four formulas",
          "tools/os88sheetfmt.py's decoder, independently of SHEET's",
          got=host_f, want=WANT)
    check(saved.get("MAC") is not None and saved.get("WS") is not None,
          "SHEET saved both, Normal", "", got=sorted(k for k, v in
                                                     saved.items() if v))
    check(bof_dt(saved.get("MAC")) == 0x40, "the macro sheet's save says so: "
          "BOF dt 0040H", "read from its BOF; the kind came off MAC.BIF's own",
          got=bof_dt(saved.get("MAC")), want=0x40)
    got = F.read_biff(saved.get("MAC") or b'')
    fml = lambda d, r: (d.get((r, 0))[1]
                        if isinstance(d.get((r, 0)), tuple) else None)
    got_f = [fml(got, r) for r in range(6)]
    check(got_f[4] is None and got.get((4, 0)) == 7.0,
          "a truncated 58H token is refused and keeps its value",
          "the token array ends after the ptg, so there is no index to read",
          got=got.get((4, 0)), want=7.0)
    check(got_f[5] == WANT[5], "...and the formula after it is still read "
          "through the Ftab", "the ptg's \"this is a command\" flag must not "
          "outlive its own token: Cetab 01H is OPEN, Ftab 01H is IF",
          got=got_f[5], want=WANT[5])
    check(got_f == WANT, "all four formulas came back as formulas",
          "a command by ptg 58H, an extension function variable and fixed, "
          "and RETURN - SHEET's decoder read each, and its writer wrote it",
          got=got_f, want=WANT)
    check(bof_dt(saved.get("WS")) == 0x10, "a worksheet opened next is a "
          "worksheet again: BOF dt 0010H", "the kind is the document's, and "
          "opening another one puts it back", got=bof_dt(saved.get("WS")),
          want=0x10)
    check(own[:3] == [(2, 1), (0, 0), (0, 1)],
          "a macro sheet turns Formulas on, and closing it turns them off",
          "kind 2 = the formulas on show are the DOCUMENT's doing; then a "
          "worksheet, then Options > Formulas by hand",
          got=own[:3], want=[(2, 1), (0, 0), (0, 1)])
    check(own[3:] == [(1, 1), (0, 1)],
          "...but Formulas the USER turned on survives a macro sheet",
          "kind 1 = they were already on, so the document did not turn them "
          "on and closing it must not turn them off",
          got=own[3:], want=[(1, 1), (0, 1)])
    wb = bofs(saved.get("WB") or b'')
    check(len(wb) >= 3 and wb[0] == 0x0100 and set(wb[1:]) == {0x0040},
          "a macro sheet saved as a WORKBOOK says so in every substream",
          "the globals BOF stays 0100H and each sheet's says 0040H - the "
          "BIFF4 writer is a second BOF site, and only the per-sheet one "
          "takes the kind", got=wb, want="[0x100, 0x40, 0x40, ...]")
    done("sheetmkind")


if __name__ == "__main__":
    main()

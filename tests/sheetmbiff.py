#!/usr/bin/env python3
"""A macro cell's FORMULA survives a Normal save (SPEC.md 81.83).

    make && python3 tests/sheetmbiff.py

Until 81.83 every one of 81.63's twenty macro functions was 0xFF in
`sh_rpn_fid`, so a Normal save wrote the cell's cached VALUE and the macro
was gone. SYLK carried it; BIFF did not. 81.68 recorded the blocker: Excel
numbers macro COMMANDS through the Command Equivalents Table and macro
FUNCTIONS through the ordinary one, and no reference on hand had the former.
docs/ms-xls.pdf does.

This gate is byte-level on purpose, because "the formula came back" is a
weaker claim than the one that matters:

  1. **The tokens are the ones Excel 2.1d writes.** SELECT and RETURN are
     checked against the bytes in a macro sheet real Excel wrote, which ships
     in this tree as build/sheetxl2/excel21d/KWWHAT.CPM. Not "a formula
     round-trips through our own reader" - our reader and writer can agree
     with each other and both be wrong, which is the whole reason 81.68
     refused to guess.
  2. **Both tables, and both ptgs.** SELECT/BEEP are Cetab and go out as ptg
     0x58; RETURN/HALT are Ftab and go out as 0x42. The split is not
     guessable from the names - GOTO and RETURN look exactly like commands
     and are functions - so a build that put all twenty in one table passes
     no check here.
  3. **A dotted name survives the lexer.** CALCULATE.NOW is one of three
     whose '.' used to end the identifier, so they could not be written even
     once the numbers were known.
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

WORK = "build/sheetmbiff"               # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetmbiff.img"
NAME = "SHIN.SLK"
OUT = "SHIN.BIF"
XLREF = "build/sheetxl2/A0.XLS"         # Excel 2.1d's own macro sheet, expanded
                                         # by sheetxl2 from KWWHAT.CPM

PTG_CE, PTG_FN = 0x58, 0x42             # the two tFuncVar forms (81.83)
PTG_F1 = 0x41                           # ...and tFunc, for a FIXED-arity one,
                                         # which carries no count byte. Every
                                         # Cetab entry is variable (a command
                                         # brings its own count); an Ftab one
                                         # follows the document's arity
# cell -> (formula text, expected ptg, expected index). The two marked REAL
# are the ones also asserted against Excel's own file below.
CASES = [
    ('SELECT("R2C1")', PTG_CE, 0x6D),   # REAL: 22 of these in KWWHAT
    ('RETURN()',       PTG_FN, 0x37),   # REAL: 8 of these in KWWHAT
    ('HALT()',         PTG_FN, 0x36),
    ('BEEP()',         PTG_CE, 0x00),
    ('CALCULATE.NOW()', PTG_CE, 0x1F),  # dotted AND 13 characters
    ('ACTIVE.CELL()',   PTG_F1, 0x5E),  # dotted and 11: isolates the two,
]                                       # and FIXED, so it is tFunc not
                                         # tFuncVar - the arity half of 81.83.4


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    cells = {(r, 0): ('formula', f, 0.0) for r, (f, _, _) in enumerate(CASES)}
    open(src, "wb").write(F.write_sylk(cells))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "build/sheet.o88",
                    "build/CHART.OVL", "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)


def fn_tokens(data):
    """every (ptg, cargs, index) function token, per FORMULA record, in order.

    BIFF2 and BIFF3 lay a FORMULA out differently and BOTH are read here:
    Excel 2.1d's own file is BIFF2 (BOF 0009H) and SHEET writes BIFF3 (0209H,
    deliberately - a reader is backward compatible and not forward).  After
    the 7-byte cell header and the 8-byte result, BIFF2 has one flag byte and
    a one-byte cce; BIFF3 has two and a word.  The function INDEX is one byte
    in both (BIFF4 is where it becomes a word).
    """
    biff3 = len(data) >= 2 and struct.unpack_from('<H', data, 0)[0] == 0x0209
    # BIFF3 RENUMBERS the cell records - FORMULA is 0206H there and 0006H in
    # BIFF2 - and lays one out differently: its header is rw/col/ixfe (6) and
    # BIFF2's is rw/col/rgbAttr (7), then both carry the 8-byte result, then
    # BIFF3 has two flag bytes and a WORD cce against BIFF2's one and one.
    # Both land the tokens at +18 and +17.
    formula = 0x0206 if biff3 else 0x0006
    hdr = 18 if biff3 else 17
    out = {}
    i = 0
    while i + 4 <= len(data):
        op, ln = struct.unpack_from('<HH', data, i)
        if i + 4 + ln > len(data):
            break
        p = i + 4
        if op == formula and ln >= hdr:
            rw, col = struct.unpack_from('<HH', data, p)
            cce = (struct.unpack_from('<H', data, p + 16)[0] if biff3
                   else data[p + 16])
            r = data[p + hdr:p + hdr + cce]
            toks, j = [], 0
            while j < len(r):
                t = r[j]
                if t == 0x17 and j + 1 < len(r):          # tStr: cch, bytes
                    j += 2 + r[j + 1]
                    continue
                if t in (PTG_CE, PTG_FN) and j + 2 < len(r):
                    toks.append((t, r[j + 1], r[j + 2]))
                    j += 3
                    continue
                if t == PTG_F1 and j + 1 < len(r):       # tFunc: no count
                    toks.append((t, None, r[j + 1]))
                    j += 2
                    continue
                if t == 0x1D:
                    j += 2
                elif t == 0x1E:
                    j += 3
                elif t == 0x1F:
                    j += 9
                else:
                    j += 1
            if toks:
                out[(rw, col)] = toks
        i += 4 + ln
    return out


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    S = os88sym.linear
    got = None
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
        # Save As -> Normal. The format radio's first row IS Normal (81.40).
        mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0], SF.SAVE_AS[1])
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['bif']))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m, limit=120)
        mo.click(*SF.SAVE_BUTTON)
        for _ in range(60):
            v = os88flush.Flush(marty=m).volume(1)
            if OUT in v.names():
                got = v.read(OUT)
                break
            M.settle(m, quiet=2.0, stable=2, limit=60)

    if got:
        open(os.path.join(WORK, OUT), 'wb').write(got)   # kept as evidence
    check(got is not None, "SHEET saved a Normal (BIFF) file",
          "everything below reads that file; without it there is nothing to "
          "assert and the rest would pass vacuously", want=OUT)
    toks = fn_tokens(got or b'')
    for r, (text, ptg, idx) in enumerate(CASES):
        t = toks.get((r, 0))
        name = text.split('(')[0]
        table = "Cetab" if ptg == PTG_CE else "Ftab"
        table += " tFunc" if ptg == PTG_F1 else ""
        check(t is not None and t[-1][0] == ptg and t[-1][2] == idx,
              "%s goes out as %s (ptg 0x%02X, index 0x%02X)"
              % (name, table, ptg, idx),
              "before 81.83 every macro function was 0xFF in sh_rpn_fid, so "
              "the cell went out as its cached VALUE and the macro was lost. "
              "The table is not guessable from the name: GOTO and RETURN read "
              "as commands and are functions",
              got=t, want=(ptg, '*', idx))

    # --- the check with the teeth: Excel's OWN bytes ------------------------
    if os.path.exists(XLREF):
        xl = fn_tokens(open(XLREF, 'rb').read())
        seen = {}
        for cell, ts in xl.items():
            for t in ts:
                seen[(t[0], t[2])] = seen.get((t[0], t[2]), 0) + 1
        for name, ptg, idx, least in (("SELECT", PTG_CE, 0x6D, 10),
                                      ("RETURN", PTG_FN, 0x37, 4)):
            n = seen.get((ptg, idx), 0)
            check(n >= least,
                  "...and %s's ptg/index are what real Excel 2.1d writes" % name,
                  "this is the check with the teeth, and the only one that "
                  "cannot be satisfied by our reader and writer agreeing with "
                  "each other. build/sheetxl2/A0.XLS is a macro sheet off an "
                  "original Excel 2.1d distribution disk; these are its own "
                  "bytes for the same function",
                  got="%d occurrence(s) of ptg 0x%02X idx 0x%02X" % (n, ptg, idx),
                  want=">= %d" % least)
    else:
        # sheetxl2 writes it only from the Excel 2.1d distribution archive
        # (its EXCEL21D_7Z), and SKIPS its arm B with a notice where that is
        # absent - so this is the same notice, not a failure, on such a box
        print("sheetmbiff: Excel 2.1d comparison SKIPPED - %s is absent "
              "(tests/sheetxl2.py writes it only when EXCEL21D_7Z is on hand)"
              % XLREF)
    done("sheetmbiff")


if __name__ == "__main__":
    main()

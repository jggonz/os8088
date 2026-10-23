#!/usr/bin/env python3
"""A CELL NOTE SURVIVES A SAVE (SPEC.md 81.119).

    make && python3 tests/sheetnote.py

SHEET has had cell notes since stage 3.0b and could not save one. No writer
in sheet.asm read [sh_nnote] and nothing emitted BIFF's NOTE (001CH), so a
note made here was lost on every save in every format - while main's Sheet,
which this fork's SHEET would replace, has kept them since its 81.22.3.
81.119 ports both halves.

This is 81.38's shape with the ends swapped, and deliberately so: the host
cannot AUTHOR a note (tools/os88sheetfmt.py writes no NOTE record), so the
GUEST writes the file and the HOST reads it with a parser of its own, ten
lines that know nothing about sheet.asm. Then the file goes back in and the
note is read off the note table. Three readings, and each one fails alone:

  1. the host finds a NOTE record in the file, at the right cell, with the
     right text - the WRITER, against an independent reader
  2. the reopened sheet's Note dialog loads that text back - the READER
  3. ...and the cell is the one it was attached to, not merely some cell

WHY THE HOST'S PARSER IS WORTH THE TEN LINES: a writer and a reader that
share a defect cancel it. If sheet.asm wrote the length in the wrong place
and read it from the same wrong place, step 2 would pass on its own and the
file would still be one no other spreadsheet could read.
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
import os88ui                                                # noqa: E402
import glass                                                 # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
from os88geom import WIN_SIZE, W_SEG                          # noqa: E402
from paintmove import pkg_syms                                # noqa: E402

WORK = "build/sheetnote"
DISK = "build/sheetnote.img"
NAME = "NOTE.SLK"
SAVED = "NOTE.BIF"
CELLS = {(0, 0): 'Item', (0, 1): 'Cost', (1, 0): 'Rope', (1, 1): 12.5}
NOTE = "hi"                     # typed a character at a time, as the dialog
CELL = (1, 1)                   # B2, the cell the note hangs on: col, row


def notes_in(data):
    """Every NOTE (001CH) in a BIFF stream, as {(col, row): text}.

    Knows nothing about sheet.asm: opcode, length, then the body SPEC.md's
    own table gives as row(2), col(2), cch(2), the bytes.
    """
    out, i = {}, 0
    while i + 4 <= len(data):
        op, ln = struct.unpack_from("<HH", data, i)
        body = data[i + 4:i + 4 + ln]
        if op == 0x001C and ln >= 6:
            row, col, cch = struct.unpack_from("<HHH", body, 0)
            out[(col, row)] = body[6:6 + cch].decode("latin-1")
        elif op == 0x000A:                      # EOF
            break
        i += 4 + ln
    return out


def main():
    os.chdir(os.path.join(HERE, ".."))
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk(CELLS))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)
    S = os88sym.linear
    sym = pkg_syms("apps/sheet/sheet.asm")
    saved, reloaded, ncells, sel = None, None, None, None

    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        SF.open_shin(m, mo, NAME)
        M.settle(m, limit=240)
        sl = dispcp.win_list(m, S, check=False)[-1]
        seg = m.read(S("wm_wins") + sl * WIN_SIZE + W_SEG, 2)
        seg = seg[0] | (seg[1] << 8)
        word = lambda n: SF._w(m, seg, sym[n])

        def top():
            w = dispcp.win_list(m, S, check=False)
            return w[-1] if w else None

        def park():
            mo.to(634, 180)
            M.settle(m)

        # --- select B2 and hang a note on it -------------------------------
        # CLICKED ON THE GLASS, not Formula > Goto: the first version used
        # Goto and the selection never moved, so the note went on A1 and the
        # row asserted B2 against a writer that was right. The check below
        # reads [sh_selcol]/[sh_selrow] for exactly that reason - a gate that
        # believes its own navigation is a gate that reports the wrong half.
        g = glass.grid(*m.vram("cga"))
        if not g:
            check(False, "the grid is on the glass", "no grid found")
            done("sheetnote")
            return
        ys, xs = g
        mo.click((xs[CELL[0]] + xs[CELL[0] + 1]) // 2,
                 (ys[CELL[1]] + ys[CELL[1] + 1]) // 2)
        M.settle(m)

        sel = (word("sh_selcol"), word("sh_selrow"))
        SF.menu_pick(m, mo, seg, sym, "sh_i_formula", "note")
        M.settle(m, limit=180)
        for ch in NOTE:
            m.type_text(ch)
            M.guest_sleep(m, 0.3)
        M.settle(m)
        t = top()
        if t is not None and t != sl:
            nx, ny, _, _ = dispcp.win_rect(m, S, t)
            mo.click(nx + 1 + 254, ny + 18 + 34)     # OK
            M.settle(m, limit=180)
        ncells = word("sh_nnote")
        park()

        # --- Save As Normal, and read the file off the floppy --------------
        SF.menu_pick(m, mo, seg, sym, "sh_i_file", "saveas")
        M.settle(m, limit=180)
        mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['bif']))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m, limit=180)
        mo.click(*SF.SAVE_BUTTON)
        for _ in range(60):
            v = os88flush.Flush(marty=m).volume(1)
            if SAVED in v.names():
                saved = v.read(SAVED)
                if saved:
                    break
            M.settle(m, quiet=2.0, stable=2, limit=60)

        # --- and READ IT BACK: the note comes back into the dialog ---------
        if saved:
            SF.menu_pick(m, mo, seg, sym, "sh_i_file", "open")
            M.settle(m, limit=180)
            os88ui.UI(m, mouse=mo).chooser_open(SAVED)   # BY NAME: the
            M.settle(m, limit=300)                       # chooser is a Disk
                                                         # window (81.105)
            SF.menu_pick(m, mo, seg, sym, "sh_i_formula", "goto")
            M.settle(m, limit=120)
            m.type_text("B2\n")
            M.settle(m, limit=120)
            SF.menu_pick(m, mo, seg, sym, "sh_i_formula", "note")
            M.settle(m, limit=180)
            reloaded = bytes(m.readseg(seg, sym["sh_notetext"],
                                       len(NOTE) + 1)).split(b"\0")[0]
            M.write_png_rgb(os.path.join(WORK, "1-reloaded.png"), *m.fbuf())

    got = notes_in(saved) if saved else {}
    print("   the selection the note was made on:", sel)
    print("   [sh_nnote] after the dialog:", ncells)
    print("   NOTE records the host found:", got)
    print("   the reopened dialog shows:  ", reloaded)

    check(sel == CELL, "the note was made on B2 and not on A1",
          "the row's own navigation, asserted: the first version used "
          "Formula > Goto, the selection never moved, and it reported a "
          "writer that was right as wrong", got=sel, want=CELL)
    check(ncells == 1, "the note is in the table before the save",
          "the dialog's OK stores it - without this the rest tests nothing",
          got=ncells, want=1)
    check(saved is not None, "Save As Normal wrote the file")
    check(got.get(CELL) == NOTE,
          "the host finds the NOTE in the file, on B2, with its text",
          "a parser that knows nothing about sheet.asm - so a writer and a "
          "reader sharing a defect cannot cancel it",
          got=got, want={CELL: NOTE})
    check(reloaded == NOTE.encode(),
          "...and reopening the file loads the note back",
          "the reader half: sh_nt_set through the same door a typed note takes",
          got=reloaded, want=NOTE.encode())
    done("sheetnote")


if __name__ == "__main__":
    main()

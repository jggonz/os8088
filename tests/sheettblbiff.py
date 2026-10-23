#!/usr/bin/env python3
"""A Data > Table survives SAVE AND LOAD, in BIFF (SPEC.md 81.114.10)

    make && python3 tests/sheettblbiff.py

Until this stage a table saved to any format kept its NUMBERS and silently
stopped being a table. Excel writes one as a FORMULA record per RESULT cell
whose whole token array is a single tTbl, plus one DATATABLE (0236H in BIFF3)
immediately after the first FORMULA record of the table's range.

THE HOST AUTHORS THE INPUT, which is 81's format-gate rule and the reason
this row is shaped the way it is: a file SHEET never wrote cannot have a
writer defect in it, so a reader defect has nothing to cancel against. The
gate that round-trips through the app alone passes while BOTH halves are
wrong in the same direction - that trap has already been caught twice here.

  1 READ      SHEET opens a host-authored BIFF and has the table: every
              field of the record, compared against what the host put in
  2 MARKED    ...and its result cells are PROTECTED, so a loaded table is a
              table and not just numbers that look like one
  3 LIVE      ...and Calculate Now re-runs it correctly, which is the proof
              that what was read is a working table rather than a stored one
  4 DTOUT     SHEET's own save carries a DATATABLE, every field asserted
  5 TTBL      ...and every result cell is a FORMULA whose only token is a
              tTbl naming the table's corner
  6 VALUES    ...and the numbers came back

THE INPUT CELL IS A COLUMN ONE, deliberately. The format names a one-input
table's single cell in the FIRST pair whichever kind it is, with option bit 2
giving the orientation; this app keeps the row input first and the column
input second ALWAYS. So the column-input shape is the one where a verbatim
copy of the sixteen bytes is WRONG in both directions - and it is the common
shape, which is what makes it the one to test.
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
import os88sym                                               # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402
import glass                                                 # noqa: E402
from paintmove import pkg_syms                                # noqa: E402
from os88geom import WIN_SIZE, W_SEG                          # noqa: E402

WORK = "build/sheettblbiff"
DISK = "build/sheettblbiff.img"
NAME = "TBL.BIF"
# SAVED OVER THE SAME NAME, deliberately: the chooser is a Disk window now
# (SPEC.md 38.3) and naming a NEW file is a second thing to drive and a
# second thing to get wrong. The host authored these bytes, so "the file
# differs from what went in" is an exact test that SHEET wrote it.

# The table the host authors: B1:D3, formulas across the top row, values down
# the left column, and a COLUMN input cell at A1.
R1, R2, C1, C2 = 0, 2, 1, 3
IN_R, IN_C = 0, 0                       # A1
WANT = {(1, 2): 8.0, (1, 3): 14.0,      # C2 D2  (input 4)
        (2, 2): 18.0, (2, 3): 19.0}     # C3 D3  (input 9)


def _rec(rid, body):
    return struct.pack("<HH", rid, len(body)) + body


def _ref(roww, col):
    """tRef, value class. 0C000H is a fully relative A1-style reference."""
    return bytes([0x44]) + struct.pack("<HB", roww, col)


def _num(x):
    return bytes([0x1F]) + struct.pack("<d", x)


def _cell(rid, r, c, tail):
    return _rec(rid, struct.pack("<HHH", r, c, 0) + tail)


def _formula(r, c, cached, tok):
    return _cell(0x0206, r, c, struct.pack("<d", cached)
                 + struct.pack("<HH", 0, len(tok)) + tok)


def _ttbl(r, c, cached):
    """A result cell: the cached number, and one tTbl naming (R1, C1)."""
    return _formula(r, c, cached,
                    bytes([0x02]) + struct.pack("<HH", R1, C1))


def author():
    """A BIFF3 stream with a table in it, built here and not by SHEET."""
    # flags: one-dimensional (bit 3 clear) with the input data in the COLUMN
    # to the left (bit 2 clear), which is this fixture's shape.
    flags = 0
    dt = _rec(0x0236, struct.pack("<HH", R1, R2) + bytes([C1, C2])
              + struct.pack("<HHHHH", flags, IN_R, IN_C, 0, 0))
    out = [_rec(0x0209, struct.pack("<HH", 0x0300, 0x0010)),
           _cell(0x0203, 0, 0, struct.pack("<d", 0.0)),          # A1 = 0
           # C1 = A1*2 - the FIRST formula record of the range, so the
           # DATATABLE goes immediately after it, which is the format's rule
           _formula(0, 2, 0.0, _ref(0xC000, 0) + _num(2.0) + b"\x05"),
           dt,
           _formula(0, 3, 10.0, _ref(0xC000, 0) + _num(10.0) + b"\x03"),
           _cell(0x0203, 1, 1, struct.pack("<d", 4.0)),          # B2
           _cell(0x0203, 2, 1, struct.pack("<d", 9.0)),          # B3
           ]
    for (r, c), v in sorted(WANT.items()):
        out.append(_ttbl(r, c, v))
    out.append(_rec(0x000A, b""))
    return b"".join(out)


def main():
    os.chdir(os.path.join(HERE, ".."))
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    authored = author()
    open(src, "wb").write(authored)
    # the host's own reader must agree with what the host just built, or the
    # fixture is wrong before SHEET has seen it
    t = F.read_biff_tables(authored)[0][0]
    assert (t["r1"], t["r2"], t["c1"], t["c2"]) == (R1, R2, C1, C2), t
    assert len(F.read_biff_ttbl(authored)) == 4, F.read_biff_ttbl(authored)

    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL", src],
                   check=True, stdout=subprocess.DEVNULL)
    S = os88sym.linear
    sym = pkg_syms("apps/sheet/sheet.asm")
    got = {}
    saved = None

    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        SF.open_shin(m, mo, NAME)
        M.settle(m, limit=300)
        sl = dispcp.win_list(m, S, check=False)[-1]
        seg = m.read(S("wm_wins") + sl * WIN_SIZE + W_SEG, 2)
        seg = seg[0] | (seg[1] << 8)
        mo.to(634, 180)
        M.settle(m)

        got["n"] = SF._w(m, seg, sym["sh_ntbl"])
        got["rec"] = SF.tbl_rec(m, seg, sym, 0)
        got["mark"] = SF.bt_byte(m, seg, sym, 2, 1)       # C2
        got["val"] = [SF.cell_val(m, seg, sym, c, r)
                      for (r, c) in sorted(WANT)]

        # --- 3 LIVE: Calculate Now re-runs what was READ -------------------
        SF.menu_pick(m, mo, seg, sym, "sh_i_options", "calcnow")
        M.settle(m, limit=300)
        got["after"] = [SF.cell_val(m, seg, sym, c, r)
                        for (r, c) in sorted(WANT)]

        # --- 4-6: SHEET's own save, read back by the host ------------------
        mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0], SF.SAVE_AS[1])
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['bif']))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m, limit=120)
        mo.click(*SF.SAVE_BUTTON)
        for _ in range(60):
            v = os88flush.Flush(marty=m).volume(1)
            if NAME in v.names() and v.read(NAME) != authored:
                saved = v.read(NAME)
                break
            M.settle(m, quiet=2.0, stable=2, limit=60)

    r = got["rec"]
    want = (R1, R2, C1, C2, SF.SH_TBL_NONE, SF.SH_TBL_NONE, IN_R, IN_C)
    gotr = None if r is None else (r["r1"], r["r2"], r["c1"], r["c2"],
                                   r["ir"], r["ic"], r["ir2"], r["ic2"])
    check(got["n"] == 1 and gotr == want, "1 READ",
          "SHEET opens a BIFF THE HOST WROTE and has the table - every field "
          "against what went in. The input cell is the sharp one: the format "
          "put it in the FIRST pair with option bit 2 saying it is a COLUMN "
          "input, and this app wants a column input in the SECOND pair with "
          "NONE in the first. Read verbatim it would claim a ROW input cell "
          "and substitute along the wrong axis",
          got="n=%d %r" % (got["n"], gotr), want=repr(want))
    check(got["mark"] is not None and (got["mark"] & SF.SH_TBL_RESULT),
          "2 MARKED",
          "...and the loaded table's result cells are PROTECTED. The mark is "
          "set by one pass after the whole stream is in, never as the record "
          "arrives: a DATATABLE follows the FIRST formula of its range, so "
          "most of the cells it describes are still ahead of it in the file",
          got="C2 border byte %r" % (got["mark"],), want="bit 7 set")
    wantv = [WANT[k] for k in sorted(WANT)]
    check(got["val"] == wantv and got["after"] == wantv, "3 LIVE",
          "the cached results came back, AND Calculate Now re-runs the "
          "loaded table to the same numbers. That is what says the record "
          "was read into a WORKING table rather than merely stored: a "
          "re-run drives off the rectangle and the input cell, so either "
          "being wrong shows up here as different numbers",
          got="%r then %r" % (got["val"], got["after"]), want=repr(wantv))

    if not saved:
        check(False, "4 DTOUT", "SHEET never wrote the file back",
              got="no save", want="a BIFF file")
        done("sheettblbiff")
        return
    tabs = F.read_biff_tables(saved).get(0, [])
    t = tabs[0] if tabs else None
    gott = None if t is None else (t["r1"], t["r2"], t["c1"], t["c2"],
                                   t["flags"], t["ir"], t["ic"],
                                   t["ir2"], t["ic2"])
    wantt = (R1, R2, C1, C2, 0, IN_R, IN_C, 0, 0)
    check(len(tabs) == 1 and gott == wantt, "4 DTOUT",
          "SHEET's own save carries ONE DATATABLE, every field asserted - "
          "including the translation back: the column input cell that lives "
          "in this app's second pair has to go out in the format's FIRST "
          "one, with the second pair left to the two-dimensional case",
          got="%d record(s), %r" % (len(tabs), gott), want=repr(wantt))
    tt = F.read_biff_ttbl(saved)
    check(sorted(tt) == sorted(WANT) and set(tt.values()) == {(R1, C1)},
          "5 TTBL",
          "...and every RESULT cell is a FORMULA whose only token is a tTbl "
          "naming the table's corner. Asserted as the token's own BYTES: a "
          "check that merely noticed the formula would not decode is "
          "satisfied by any corrupt array at all",
          got=repr(sorted(tt.items())),
          want="the four result cells, all naming %r" % ((R1, C1),))
    cs = F.read_biff(saved)
    back = {k: F.scalar(cs.get(k)) for k in sorted(WANT)}
    check(all(F.close(back[k] if back[k] is not None else -1e30, v)
              for k, v in WANT.items()), "6 VALUES",
          "...and the numbers came back, which is what a reader that knows "
          "NEITHER record still gets: the cached result is in the FORMULA "
          "record, and that is why this is the faithful shape rather than a "
          "private one",
          got=repr(back), want=repr(dict(sorted(WANT.items()))))
    done("sheettblbiff")


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""CHART's three readers are ONE reader, or the chart is quietly wrong (82.18)

    make && python3 tests/chartread.py

CHART opens SYLK, DIF and BIFF, and until this row **only SYLK had ever been
executed by a test** - `tests/chartname.py` opens one `.SLK` and nothing else.
The other two are a few hundred bytes of hand-written record walking apiece
(`ct_read_dif`, `ct_read_biff`, `ct_rkdec`), and a decoder that is wrong does
not fail: it charts a number. That is the same shape as the defect that made
`chartname` exist, where 82.15's filter never ran and every chart was simply
the wrong column.

THE FIXTURE IS ONE DATASET WRITTEN THREE WAYS, and the assertion that matters
is that the three AGREE. A reader is checked against its siblings rather than
against a number recomputed here, because `ch_scale` normalises a series into
signed words against `[ch_e10]` and restating that arithmetic on the host
would be testing this file's copy of it, not CHART's.

The host writes all three and then **reads each one back with
tools/os88sheetfmt.py before the guest ever sees it**. That is the rule
81.? set for SHEET's own format gate and it matters as much here: if the DIF
and BIFF writers below are wrong, the fixture is wrong, and a wrong fixture
that CHART happens to parse the same wrong way would pass. The round trip
fails on the host, at build time, where it is readable.

THE VALUES ARE CHOSEN TO BE AWKWARD. Column A is 12.5, -3.25, 1000.0, 0.125:
a fraction, a NEGATIVE, three orders of magnitude between the largest and the
smallest, and nothing that survives being truncated to an integer. Column B
is 7, 3, 9, 1 - whole numbers, so that BIFF carries them as **RK** records
while column A's fractions must be **NUMBER** records.

AND THE BIFF FILE SPELLS COLUMN A FOUR DIFFERENT WAYS ON PURPOSE: one NUMBER,
one FORMULA with a cached number, one RK and one more NUMBER. Every record
kind `ct_read_biff` knows carries a value in this one file, and they must all
come out the same as the SYLK's four. **A fifth cell is a FORMULA whose
cached result is NOT a number** - BIFF marks a cached string, boolean or
error by setting the result's top word to `0xFFFF`, which no finite double
has - and 82.18 says it must be SKIPPED. If it is not, `ct_valcnt` is five
and a NaN's mantissa is charted as a data point. That arm was "verified
against a file Sheet wrote and the host decoded" and had no gate.

  1 SYLK       the baseline reader, and the only one with a test before this
  2 DIF        the same four values, from the text format
  3 BIFF       the same four again - and this file alone exercises ct_rkdec
  4 AGREE      ...AND ALL THREE PUBLISHED THE SAME SCALED SERIES. This is the
               check: 1-3 only say a reader produced four numbers, and a
               decoder that drops a sign or misplaces a decimal point still
               produces four numbers
  5 SHAPE      the published words still carry the DATA - one negative, at
               index 1, and the largest magnitude at index 2 - so a run where
               all three readers agreed on rubble cannot pass
  6 NAMES      a name belongs to the FILE: the SYLK carries one, and after the
               BIFF (which carries none) is opened the menu offers none
  7 COLUMN     Data > Column B charts column B (82.12) - the untested twin of
               82.15's range filter, and the same shape of mechanism
  8 BOTH       ...AND IT STILL DOES WHEN THE FILE HAS A NAME IN IT. The Data
               menu's eight slots are names FIRST and "Column X" for the rest
               (ct_labels_names: "so a half-filled menu says what the rest
               still are"), so a one-name file offers PICK, Column B, Column
               C... The handler decides which KIND a slot is by whether the
               file has any names at all, not by which slot was picked
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88fixture                                           # noqa: E402
import os88ui                                                # noqa: E402
import os88geom                                              # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
from harness import check, done                              # noqa: E402
from paintmove import pkg_syms                               # noqa: E402

DISK = "build/chartread.img"
SLK, DIF, BIF = "READ.SLK", "READ.DIF", "READ.BIF"
NAME = "PICK"
# Column A is what Automatic charts - the LOWEST column (82.12) - so these are
# the four values every check below is about. See the header for why they are
# these four.
COLA = [12.5, -3.25, 1000.0, 0.125]
COLB = [7.0, 3.0, 9.0, 1.0]
# In the BIFF file only: which record kind carries each of column A's four,
# and a fifth cell that is a formula answering text, which must be skipped.
BIFF_A_KIND = ["number", "formula", "rk", "number"]
BIFF_DEAD = (len(COLA), 0)              # row 4, column A: '#DIV/0!' cached
CH_MAXBARS = 40                         # chart.asm's own, mirrored: a plain
                                        # `equ` is not in the symbol map


def cells():
    out = {}
    for r, (a, b) in enumerate(zip(COLA, COLB)):
        out[(r, 0)] = a
        out[(r, 1)] = b
    return out


# -----------------------------------------------------------------------------
# The two formats tools/os88sheetfmt.py can read but not write. Both are
# written to the FORMAT, not to CHART's reader - a fixture shaped around the
# thing under test proves nothing - and both are read back below.
# -----------------------------------------------------------------------------
def num(v):
    """A plain decimal, which is what both text formats carry."""
    s = "%.10g" % v
    return s


def write_dif(cs):
    rows = max(r for r, _ in cs) + 1
    cols = max(c for _, c in cs) + 1
    out = ["TABLE", "0,1", '"OS88TEST"',
           "VECTORS", "0,%d" % cols, '""',
           "TUPLES", "0,%d" % rows, '""',
           "DATA", "0,0", '""']
    for r in range(rows):
        out += ["-1,0", "BOT"]
        for c in range(cols):
            if (r, c) in cs:
                out += ["0,%s" % num(cs[(r, c)]), "V"]
    out += ["-1,0", "EOD"]
    return ("\n".join(out) + "\n").encode("ascii")


def _rec(rid, body):
    return struct.pack("<HH", rid, len(body)) + body


def _rk_rec(r, c, v):
    rk = ((int(v) << 2) | 2) & 0xFFFFFFFF
    return _rec(0x027E, struct.pack("<HHHI", r, c, 0, rk))


def _num_rec(r, c, v):
    return _rec(0x0203, struct.pack("<HHH", r, c, 0) + struct.pack("<d", v))


def _fml_rec(r, c, v):
    """FORMULA (BIFF3): row, col, xf, the CACHED RESULT, flags, cce, tokens.

    v is a float for a numeric result, or None for one that is not - which
    BIFF spells as the result's top word being 0xFFFF and the first byte
    saying which kind. 2 is an error; its code 0x07 is #DIV/0!. No token
    array is written (cce = 0): CHART never looks at the expression, it reads
    the cached result, which is the whole of what 82.18 is about.
    """
    if v is None:
        res = struct.pack("<BBBBBBH", 2, 0, 0x07, 0, 0, 0, 0xFFFF)
    else:
        res = struct.pack("<d", v)
    return _rec(0x0206, struct.pack("<HHH", r, c, 0) + res
                + struct.pack("<HH", 0, 0))


def write_biff(cs):
    """A BIFF3 single-sheet stream: BOF, the cells, EOF.

    Column A is written one record kind at a time (BIFF_A_KIND) so that every
    kind ct_read_biff knows is exercised by one file; everything else is an
    RK if it is a whole number and a NUMBER if it is not, which is what SHEET
    itself does. RK's bit 1 means "the top 30 bits are a signed integer";
    bit 0 (the /100 form) is deliberately not used, so a wrong shift here
    cannot cancel against a wrong divide there.
    """
    # BIFF3 BOF: version, then the substream type. 0x0010 is a worksheet;
    # 0x0100 would make it a workbook globals stream and the host walker
    # would then expect a SHEETHDR directory behind it.
    out = [_rec(0x0209, struct.pack("<HH", 0x0300, 0x0010))]
    for (r, c), v in sorted(cs.items()):
        v = float(v)
        kind = BIFF_A_KIND[r] if c == 0 and r < len(BIFF_A_KIND) else (
            "rk" if v.is_integer() and -(1 << 29) <= int(v) < (1 << 29)
            else "number")
        out.append({"rk": _rk_rec, "number": _num_rec,
                    "formula": _fml_rec}[kind](r, c, v))
    # ...and the cell 82.18 says must never become a bar
    out.append(_fml_rec(BIFF_DEAD[0], BIFF_DEAD[1], None))
    out.append(_rec(0x000A, b""))
    return b"".join(out)


def write_sylk(cs, name=None):
    sylk = F.write_sylk(cs).decode("ascii")
    if name is None:
        return sylk.encode("ascii")
    # The one record the host writer cannot emit, spliced in before the
    # terminator exactly as tests/chartname.py does it.
    body = sylk.rstrip("\r\n")
    assert body.endswith("\nE"), repr(body[-8:])
    return (body[:-1] + "NN;N%s;ER1C2:R4C2\n" % name + "E\n").encode("ascii")


def verify(path, data, kind, want):
    """Read the fixture back with the HOST's reader before the guest sees it.

    A writer defect here would otherwise reach the guest as a data defect and
    be read as CHART's.
    """
    got = {"sylk": F.read_sylk, "dif": F.read_dif, "biff": F.read_biff}[kind](data)
    flat = {}
    for k, v in got.items():
        if isinstance(v, tuple) and len(v) == 3 and v[0] == "formula":
            v = v[2]                    # the CACHED RESULT, which is the
        if isinstance(v, (int, float)):  # only thing CHART reads
            flat[k] = float(v)
        elif k != BIFF_DEAD:
            sys.exit("chartread: the %s fixture has a non-numeric cell at %r "
                     "that the row does not account for: %r" % (kind, k, v))
    bad = [(k, flat.get(k), want[k]) for k in sorted(want)
           if k not in flat or not F.close(flat[k], want[k])]
    if bad:
        sys.exit("chartread: the %s fixture does not read back as written "
                 "(%s): %r" % (kind, path, bad[:4]))
    extra = sorted(set(flat) - set(want))
    if extra:
        sys.exit("chartread: the %s fixture carries cells nobody asked for: %r"
                 % (kind, extra[:4]))


def build_disk():
    # The package FIRST: this row packs its own floppy out of build/chart.o88
    # and pkg_syms reads the SOURCE, so a source newer than the binary gives
    # bss addresses shifted by however much it grew (tests/chartname.py's
    # header has the full account of that trap).
    os88fixture.need("build/chart.o88", "build/os8088-360.img")
    cs = cells()
    files = [(SLK, write_sylk(cs, NAME), "sylk"),
             (DIF, write_dif(cs), "dif"),
             (BIF, write_biff(cs), "biff")]
    paths = []
    for nm, data, kind in files:
        p = os.path.join(ROOT, "build", nm)
        open(p, "wb").write(data)
        verify(p, data, kind, cs)
        paths.append(os.path.join("build", nm))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "build/chart.o88"] + paths,
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(ROOT)
    build_disk()
    S = pkg_syms("apps/chart/chart.asm")
    u16 = lambda b: b[0] | (b[1] << 8)
    s16 = lambda b: u16(b) - 0x10000 if u16(b) & 0x8000 else u16(b)

    with os88ui.boot(os.path.join(ROOT, "build", "os8088-360.img"),
                     apps=DISK, machine="os8088_xt_vga") as ui:
        m = ui.m
        w = ui.path("B:/CHART.O88")
        seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym) + os88geom.W_SEG, 2))
        ui.settle()

        rd = lambda s: u16(m.readseg(seg, S[s], 2))
        rds = lambda s: s16(m.readseg(seg, S[s], 2))
        rdb = lambda s: m.readseg(seg, S[s], 1)[0]

        def published():
            """What the DRAWING reads, not what the file said.

            The raw doubles live in the staging claim, which ct_read_by_ext
            frees before it returns (82.14); ct_wvals and [ch_e10] are the
            package's own and are what survives a load, so they are what a
            gate can compare. That is also the right thing to compare -
            82.13's exponent is part of the answer.
            """
            n = rd("ct_valcnt")
            n = min(n, CH_MAXBARS)
            vals = [s16(m.readseg(seg, S["ct_wvals"] + 2 * i, 2))
                    for i in range(n)]
            return (rd("ct_valcnt"), rd("ct_mincol"), rds("ch_e10"), vals)

        def load(doc):
            ui.menu_pick("File", "Open...")
            ui.chooser_open(doc)
            ui.settle()
            return published()

        slk = load(SLK)
        check(slk[0] == len(COLA) and slk[1] == 0, "1 SYLK",
              "the baseline reader: four cells of the LOWEST column, which "
              "is A (82.12's Automatic)",
              got="%d value(s), column %d" % (slk[0], slk[1]),
              want="%d value(s), column 0" % len(COLA))
        names_after_sylk = rdb("ct_nnames")

        dif = load(DIF)
        check(dif[0] == len(COLA) and dif[1] == 0, "2 DIF",
              "ct_read_dif walks BOT/V pairs and parses each number out of "
              "the text - a reader no test has executed until this row",
              got="%d value(s), column %d" % (dif[0], dif[1]),
              want="%d value(s), column 0" % len(COLA))

        bif = load(BIF)
        check(bif[0] == len(COLA) and bif[1] == 0, "3 BIFF",
              "ct_read_biff walks real record headers, and THIS file alone "
              "exercises ct_rkdec - and column A is written one record kind "
              "at a time, so NUMBER, FORMULA and RK all carry a value here. "
              "The file also holds a FIFTH cell in column A, a FORMULA whose "
              "cached result is #DIV/0! (top word 0xFFFF): 82.18 says it is "
              "skipped, and a count of %d is that assertion - reading the "
              "bytes under it would chart a NaN's mantissa" % len(COLA),
              got="%d value(s), column %d" % (bif[0], bif[1]),
              want="%d value(s), column 0" % len(COLA))

        check(slk == dif == bif, "4 AGREE",
              "...AND THE THREE PUBLISHED THE SAME SERIES, exponent and all. "
              "This is the check the three above cannot be: a decoder that "
              "drops a sign, shifts an RK by the wrong amount or loses a "
              "decimal point still hands ct_record four numbers, and 1-3 "
              "would all pass on four wrong ones",
              got="SYLK %r / DIF %r / BIFF %r" % (slk, dif, bif),
              want="three identical readings")

        n, _, e10, vals = slk
        negs = [i for i, v in enumerate(vals) if v < 0]
        big = max(range(len(vals)), key=lambda i: abs(vals[i])) if vals else -1
        check(negs == [1] and big == 2, "5 SHAPE",
              "the published words still carry the DATA's shape - COLA is "
              "%r, so exactly one value is negative and it is the second, "
              "and the largest magnitude is the third. Without this, three "
              "readers that agreed on rubble would pass check 4" % (COLA,),
              got="negative at %r, largest at %d, words %r" % (negs, big, vals),
              want="negative at [1], largest at 2")

        check(names_after_sylk == 1 and rdb("ct_nnames") == 0, "6 NAMES",
              "a name belongs to the FILE (82.17). The SYLK carries one NN "
              "record; the BIFF carries no DEFINEDNAME, so after it the Data "
              "menu must offer none. ct_nnames used to be cleared inside "
              "ct_read_sylk alone, which left a BIFF file showing the "
              "previous SYLK file's ranges",
              got="%d after the SYLK, %d after the BIFF"
                  % (names_after_sylk, rdb("ct_nnames")),
              want="1 after the SYLK, 0 after the BIFF")

        # --- Data > Column, on the file that has no names in it -------------
        ui.menu_pick("Data", "Column B")
        ui.settle()
        col = published()
        check(rd("ct_wantcol") == 2 and rd("ct_wantrng") == 0
              and col[1] == 1, "7 COLUMN",
              "Data > Column B stores the 1-based COLUMN and charts it "
              "(82.12). This is 82.15's filter one branch along - the pair "
              "ct_wantcol/ct_wantrng - and only the range half had a test",
              got="wantcol %d, wantrng %d, column %d"
                  % (rd("ct_wantcol"), rd("ct_wantrng"), col[1]),
              want="wantcol 2, wantrng 0, column 1")
        check(col[1] != bif[1], "7a ...AND IT CHANGED THE ANSWER",
              "the control: Automatic charted column A, so a row that only "
              "asked whether picking a column charted SOMETHING would pass "
              "against a choice that never reached the reader",
              got="Automatic %d, Column B %d" % (bif[1], col[1]),
              want="different columns")

        # --- ...and on the file that DOES have a name ----------------------
        # The menu now reads PICK, Column B, Column C...: slot 0 is the name
        # and ct_labels_names leaves the rest saying what they still are.
        load(SLK)
        ui.menu_pick("Data", "Column B")
        ui.settle()
        check(rd("ct_wantcol") == 2 and rd("ct_wantrng") == 0, "8 BOTH",
              "the SAME menu item must mean the same thing when the file "
              "also has a name. ct_labels_names fills slot 0 with PICK and "
              "slots 1-7 with Column B..Column H, so this item IS a column - "
              "but the handler tests `cmp byte [ct_nnames], 0` and routes "
              "EVERY slot to ct_wantrng once the file has any name at all. "
              "A wantrng of 2 selects name record #1, which no file defined",
              got="wantcol %d, wantrng %d"
                  % (rd("ct_wantcol"), rd("ct_wantrng")),
              want="wantcol 2, wantrng 0")
    done("chartread")


if __name__ == "__main__":
    sys.exit(main())

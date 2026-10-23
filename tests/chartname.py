#!/usr/bin/env python3
"""CHART charts the range Sheet NAMED, and not the lowest column (SPEC.md 82.15)

    make && python3 tests/chartname.py

THE FIRST TEST CHART HAS EVER HAD, and it exists because of what that cost.
82.15 said "`ct_record` then admits only cells inside the chosen rectangle",
and the filter NEVER RAN ONCE: `ct_reset_series` zeroed `ct_wantrng` and every
reader called it first, so `Data > <a name>` set the request, re-read the
file, and `ct_record` tested a zero. Every chart was the lowest column's,
exactly as before the feature. It shipped that way for two cycles with the
section asserting the opposite, because there was no `tests/chart*.py` and no
chart row in `tests/suite.py` - nothing to say otherwise.

THE FIXTURE IS BUILT SO THE TWO ANSWERS DIFFER. Column A is 10,20,30,40 and
column B is 7,3,9,1, and the only name in the file - `PICK` - is B1:B4:

  * Automatic charts the LOWEST column, which is A, so `ct_mincol` is 0.
  * `Data > PICK` must chart B, so `ct_mincol` is 1.

Without the filter the second reads 0 as well, which is check 5 - and check 5
is the whole point. A row that only asserted "picking the name charts
something" would have passed against the broken code, because it did chart
something: the wrong column, every time.

The values are NOT proportional between the columns on purpose. ch_scale
normalises a series into words against [ch_e10], so 10,20,30,40 and 1,2,3,4
would produce the SAME scaled words with a different exponent and a check on
the drawn numbers could not tell the columns apart. `ct_mincol` is the
column itself and cannot be confused that way.

  1 CHARTED    the file read and four cells charted
  2 AUTOMATIC  with no choice made, the series is the lowest column (A)
  3 NAMED      the name reached the Data menu's eight slots, as its TEXT
  4 REQUESTED  picking it stores the request (ct_wantrng) AND the request
               survives the re-read it asks for - which is the half the old
               ct_reset_series destroyed
  5 FILTERED   ...AND THE SERIES IS COLUMN B. Separate from 4 because they
               fail for different reasons: 4 says the choice did not survive,
               5 says the cells were never filtered by it
  6 BACK       Automatic returns to column A, so nothing is sticky
"""
import os
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

DISK = "build/chartname.img"
DOC = "NAMED.SLK"
NAME = "PICK"
# column A (X1) and column B (X2); NOT proportional - see the header
COLA = [10.0, 20.0, 30.0, 40.0]
COLB = [7.0, 3.0, 9.0, 1.0]
CT_NAME_MAX = 12                        # chart.asm's own, mirrored: a plain
                                        # `equ` is not in the package's symbol
                                        # map, which carries the bss chain


def build_disk():
    # MAKE THE PACKAGE FIRST. This row packs its own floppy out of
    # build/chart.o88 rather than naming a Makefile target, so without this it
    # tests whatever was last built - and pkg_syms reads the SOURCE, so a
    # source newer than the binary gives symbol addresses shifted by however
    # many bytes it grew: every bss word in the chain hangs off
    # os88_image_end. The mutation run that proved this row can fail read
    # ct_mincol as 3000 for exactly that reason, which looks like a defect in
    # CHART and is a defect in the harness.
    os88fixture.need("build/chart.o88", "build/os8088-360.img")
    cells = {}
    for r, (a, b) in enumerate(zip(COLA, COLB)):
        cells[(r, 0)] = a
        cells[(r, 1)] = b
    sylk = F.write_sylk(cells).decode("ascii")
    # ...and the one record the host writer cannot emit. SHEET writes it as
    # "NN;N<name>;E<ref>" with an ABSOLUTE R1C1 reference, which is the only
    # form ct_parse_nn reads (a name is a fixed place, so there is no
    # relative form to accept).
    nn = "NN;N%s;ER1C2:R4C2\n" % NAME
    body = sylk.rstrip("\r\n")
    assert body.endswith("\nE"), repr(body[-8:])   # the terminator, no EOL
    sylk = body[:-1] + nn + "E\n"
    open(os.path.join(ROOT, "build", DOC), "w").write(sylk)
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "build/chart.o88",
                    os.path.join("build", DOC)],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(ROOT)
    build_disk()
    S = pkg_syms("apps/chart/chart.asm")
    u16 = lambda b: b[0] | (b[1] << 8)

    with os88ui.boot(os.path.join(ROOT, "build", "os8088-360.img"),
                     apps=DISK, machine="os8088_xt_vga") as ui:
        m = ui.m
        w = ui.path("B:/CHART.O88")
        seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym) + os88geom.W_SEG, 2))
        ui.settle()

        rd = lambda s: u16(m.readseg(seg, S[s], 2))
        rdb = lambda s: m.readseg(seg, S[s], 1)[0]

        def label(i):
            """Slot i of the Data menu, which is a BUFFER the kernel re-reads
            each time the menu drops (82.15's relabel-in-place)."""
            at = S["ct_labels"] + i * (CT_NAME_MAX + 1)
            raw = bytes(m.readseg(seg, at, CT_NAME_MAX + 1))
            return raw.split(b"\0")[0].decode("ascii", "replace")

        ui.menu_pick("File", "Open...")
        ui.chooser_open(DOC)
        ui.settle()

        charted = rd("ct_valcnt")
        auto_col = rd("ct_mincol")
        check(charted == len(COLA), "1 CHARTED",
              "the reader found the file and kept a series",
              got=charted, want=len(COLA))
        check(auto_col == 0, "2 AUTOMATIC",
              "with no choice made ct_record keeps the LOWEST column, which "
              "is A - the behaviour 82.15's filter sits in front of",
              got=auto_col, want=0)
        check(rdb("ct_nnames") == 1 and label(0) == NAME, "3 NAMED",
              "ct_parse_nn read the NN record and the Data menu's first slot "
              "was relabelled in place",
              got="%d name(s), slot 0 = %r" % (rdb("ct_nnames"), label(0)),
              want="1 name, %r" % NAME)

        ui.menu_pick("Data", NAME)
        ui.settle()
        want = rd("ct_wantrng")
        named_col = rd("ct_mincol")
        check(want == 1, "4 REQUESTED",
              "picking the name stores the 1-based choice",
              got=want, want=1)
        check(named_col == 1, "5 FILTERED",
              "...AND ct_record ADMITTED ONLY THE RECTANGLE, so the series "
              "is column B. This is the check 82.15 never had: the request "
              "was stored by the menu and then ZEROED by ct_reset_series at "
              "the top of the re-read, before any cell was offered, so the "
              "chart was column A every time. 4 and 5 are separate because "
              "they fail for different reasons - 4 says the choice never "
              "survived to be read, 5 says the cells were never filtered - "
              "and the old defect failed both",
              got=named_col, want=1)
        check(named_col != auto_col, "5a ...AND IT CHANGED THE ANSWER",
              "the control. Automatic and the name must disagree, or this "
              "row would pass against a filter that never ran - which is "
              "exactly how the defect survived two cycles",
              got="Automatic %d, %s %d" % (auto_col, NAME, named_col),
              want="different columns")

        ui.menu_pick("Data", "Automatic")
        ui.settle()
        back = rd("ct_mincol")
        check(back == 0 and rd("ct_wantrng") == 0, "6 BACK",
              "Automatic clears both the column and the range choice",
              got="col %d, wantrng %d" % (back, rd("ct_wantrng")),
              want="col 0, wantrng 0")
    done("chartname")


if __name__ == "__main__":
    sys.exit(main())

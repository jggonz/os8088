#!/usr/bin/env python3
"""CHART keeps the TWO lowest columns, and demotes (SPEC.md 82.8, 82.8.1)

    make && python3 tests/chart2ser.py

Scatter and Combination are the only types that read two arrays, and 82.8
says that is why they waited: *"it is a data-model change, not a drawing
one"*. `tests/chartgal.py` proves the two of them DRAW differently from the
other five; nothing proved the data model under them, which is the half that
was actually hard.

THE SUBTLE PART IS THE DEMOTION, and it is why this row exists. `ct_record`
keeps the two lowest-numbered columns with a three-way test repeated one
level along, and 82.8.1 states the rule for the case where a column arrives
that is lower than the series already being built: it **demotes** that series
to second rather than throwing it away, *"it is the next-lowest by
construction"*. A file in plain row-major order never exercises that - the
lowest column is in the first row and nothing ever supersedes it - so the
fixture below is written to put a HIGHER column first.

  1 TWO        a three-column file keeps columns A and B, and drops C
  2 ORDERED    ...and each series holds the right COLUMN's values, which a
               pair of counts cannot say
  3 DEMOTE     a file whose first cells are column B, with column A appearing
               only in a later row, ends with A as series one and B as series
               two - the path 82.8.1 describes and row-major order hides
  4 ALONE      one column means ct_t2cnt is ZERO, which is 82.8's "zero in
               ch_cnt2 means there is no second" and is what both types test
               instead of refusing
  5 SCALES     ch_max and ch_max2 are independent (82.8: "forcing one maximum
               on both flattens whichever has the smaller range"), so two
               columns with different ranges must not share one. It is read
               on a SCATTER, because ch_prep2 runs in the two arms of ch_draw
               that spend the second scale and nowhere else - on the Column
               chart a file opens as, [ch_max2] is still zero
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

DISK = "build/chart2ser.img"
THREE, DEMOTE, ONE = "THREE.SLK", "DEMOTE.SLK", "ONE.SLK"

# Three columns whose RANGES differ by a decade, so check 5 has something to
# say: A tops out at 40, B at 9. C exists only to be dropped.
COLA = [10.0, 20.0, 30.0, 40.0]
COLB = [7.0, 3.0, 9.0, 1.0]
COLC = [500.0, 600.0, 700.0, 800.0]


def write(name, cells):
    open(os.path.join(ROOT, "build", name), "wb").write(F.write_sylk(cells))
    return os.path.join("build", name)


def build_disk():
    os88fixture.need("build/chart.o88", "build/os8088-360.img")
    three = {}
    for r, (a, b, c) in enumerate(zip(COLA, COLB, COLC)):
        three[(r, 0)], three[(r, 1)], three[(r, 2)] = a, b, c

    # THE DEMOTION FIXTURE. Column B occupies rows 0..3 and column A appears
    # only in rows 4..7, so the reader meets B first and has a series built
    # out of it before anything from A arrives. A then supersedes it and B
    # must become series two. In THREE.SLK, which is plain row-major, A is in
    # the very first row and this path is never taken.
    dem = {}
    for r, b in enumerate(COLB):
        dem[(r, 1)] = b
    for r, a in enumerate(COLA):
        dem[(r + len(COLB), 0)] = a

    one = dict(((r, 0), a) for r, a in enumerate(COLA))
    paths = [write(THREE, three), write(DEMOTE, dem), write(ONE, one)]
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "build/chart.o88"] + paths,
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(ROOT)
    build_disk()
    S = pkg_syms("apps/chart/chart.asm")
    u16 = lambda b: b[0] | (b[1] << 8)
    s16 = lambda v: v - 0x10000 if v & 0x8000 else v

    with os88ui.boot(os.path.join(ROOT, "build", "os8088-360.img"),
                     apps=DISK, machine="os8088_xt_vga") as ui:
        m = ui.m
        w = ui.path("B:/CHART.O88")
        seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym) + os88geom.W_SEG, 2))
        ui.settle()

        rd = lambda s: u16(m.readseg(seg, S[s], 2))

        def words(sym, n):
            """ch_scale's output for a series - what the drawing reads."""
            return [s16(u16(m.readseg(seg, S[sym] + 2 * i, 2)))
                    for i in range(n)]

        def load(doc):
            ui.menu_pick("File", "Open...")
            ui.chooser_open(doc)
            ui.settle()
            return (rd("ct_tcnt"), rd("ct_mincol"),
                    rd("ct_t2cnt"), rd("ct_mincol2"))

        n1, c1, n2, c2 = load(THREE)
        check((n1, c1, n2, c2) == (len(COLA), 0, len(COLB), 1), "1 TWO",
              "a three-column file keeps the TWO lowest - A as series one "
              "and B as series two - and drops C entirely. 82.8.1: the "
              "reader kept only the lowest column before Scatter and "
              "Combination needed a second",
              got="series one %d cells of column %d, series two %d of %d"
                  % (n1, c1, n2, c2),
              want="4 of column 0, 4 of column 1")

        # The counts above would pass if BOTH series held column A, so the
        # values are what says which column each one actually took. ch_scale
        # normalises against a shared [ch_e10], and these two columns are not
        # proportional, so their scaled words cannot coincide.
        w1, w2 = words("ct_wvals", n1), words("ct_w2vals", n2)
        rising = all(x < y for x, y in zip(w1, w1[1:]))
        check(rising and not all(x < y for x, y in zip(w2, w2[1:])),
              "2 ORDERED",
              "...AND EACH SERIES HOLDS ITS OWN COLUMN'S VALUES. Column A is "
              "%r and rises throughout; column B is %r and does not. Two "
              "counts cannot tell series two from a second copy of series "
              "one, and ct_record's demote path copies one array over the "
              "other, so this is the check that says the copy took the "
              "right source" % (COLA, COLB),
              got="series one %r, series two %r" % (w1, w2),
              want="series one rising, series two not")

        n1, c1, n2, c2 = load(DEMOTE)
        d1, d2 = words("ct_wvals", n1), words("ct_w2vals", n2)
        check((n1, c1, n2, c2) == (len(COLA), 0, len(COLB), 1), "3 DEMOTE",
              "the fixture puts column B in rows 0-3 and column A only in "
              "rows 4-7, so the reader builds a series out of B and then "
              "meets a LOWER column. 82.8.1 says it demotes rather than "
              "discards - B is the next-lowest by construction - so the "
              "answer must be the same as the row-major file's. A plain "
              "file never reaches this path, which is why check 1 cannot "
              "stand in for it",
              got="series one %d cells of column %d, series two %d of %d"
                  % (n1, c1, n2, c2),
              want="4 of column 0, 4 of column 1")

        check(all(x < y for x, y in zip(d1, d1[1:]))
              and d2 == w2, "3a ...AND THE DEMOTED VALUES ARE B's",
              "the counts above say a series of four exists in column 1; "
              "they do NOT say the demotion copied the right array into it. "
              "It is a `rep movsw` between two buffers and a swapped pair of "
              "pointers passes every count. So series two's scaled words are "
              "compared against the SAME column read the ordinary way (check "
              "2's w2, off the row-major file), and series one must hold "
              "rising column A. MEASURED NECESSARY: reversing that copy "
              "leaves checks 1-5 all passing without this one",
              got="demoted series one %r, series two %r (row-major B was %r)"
                  % (d1, d2, w2),
              want="series one rising, series two identical to B")

        n1, c1, n2, c2 = load(ONE)
        check(n1 == len(COLA) and n2 == 0, "4 ALONE",
              "one column leaves ct_t2cnt ZERO. 82.8: \"zero in ch_cnt2 "
              "means there is no second\", which Scatter and Combination "
              "both TEST rather than refuse - a scatter against the row "
              "index, and a combination that is honestly a column chart",
              got="series one %d, series two %d" % (n1, n2),
              want="4 and 0")

        load(THREE)
        # ...AND PICK A TYPE THAT USES THE SECOND SCALE. ch_prep2 is called
        # from the Scatter and Combination arms of ch_draw and from nowhere
        # else, so [ch_max2] is computed by the types that SPEND it rather
        # than by the reader: on a Column chart - the default this file opens
        # as - it is simply still zero, which is what this check first read
        # and wrongly reported as a shared maximum.
        ui.menu_pick("Gallery", "Scatter", limit=60.0)
        ui.settle()
        mx1, mx2 = rd("ch_max"), rd("ch_max2")
        check(mx1 != mx2 and mx2 != 0, "5 SCALES",
              "ch_max and ch_max2 are INDEPENDENT. 82.8: a scatter's axes "
              "measure different things, and \"forcing one maximum on both "
              "flattens whichever has the smaller range into the axis\". "
              "Column A tops out at %g and column B at %g, so one shared "
              "maximum would show here as two equal words"
              % (max(COLA), max(COLB)),
              got="ch_max %d, ch_max2 %d (on a Scatter)" % (mx1, mx2),
              want="two different non-zero maxima")
    done("chart2ser")


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""The series' decimal exponent, at both ends and in the middle (SPEC.md 82.13)

    make && python3 tests/chartexp.py

Every value the chart draws is a signed 16-bit integer, and the thing being
charted is a double. `ch_scale` bridges that with ONE decision per series: an
exponent E that puts the largest absolute value as close to 32767 as it will
go, capped at CH_E10_MAX = 4 and CH_E10_MIN = -9. Only the labels need to
know, and `[ch_e10]` is how they are told.

82.13 is specific about what E buys, and each clause below is one check:

  1 FRACTIONS  positive E keeps them - 44.1 becomes 4410 at E=2. Before this
               existed a column of 43.6 and 44.1 drew bars of 43 and 44
  2 LARGE      negative E is what makes a big series chartable AT ALL -
               3,400,000 becomes 3400 at E=-3, which is 82.13's own example.
               "Not a fidelity improvement; a whole range of data that could
               not previously be drawn"
  3 CAPPED     E stops at CH_E10_MAX. A column topping out at 0.004 would
               like E=6; it gets 4, and the largest word is 40 rather than
               4000
  4 ROUNDED    scaling ROUNDS rather than truncates (fp_round then fp_a2i):
               0.66666 at E=4 is 6666.6, which must come out 6667. "fp_a2i
               alone is short by up to a whole unit at the top of the scale,
               which is a visible pixel error on the tallest bar" - and one
               unit is all that separates the two answers, so this check is
               the whole of that sentence
  5 WHOSE      TWO SERIES, ONE EXPONENT WORD. ct_finalize scales series TWO
               first and series ONE last, so [ch_e10] is left holding the
               series the value axis is drawn from. With a tens column and a
               millions column the two exponents are 2 and -3, so reversing
               that order is not a subtle difference - it is the axis labels
               reading a different quantity from the bars

THE WORDS ARE CHECKED, NOT JUST E. An exponent on its own says what the
labels will claim; ct_wvals says what the bars will be. A scale that picked
the right E and then wrote the wrong words would draw a correct axis against
wrong heights, which is the shape of the defect 82.13 exists to have fixed.
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

DISK = "build/chartexp.img"
CH_E10_MAX, CH_E10_MIN = 4, -9          # os88chart.inc's own, mirrored

# name, column, the exponent 82.13's rule gives it, and why that one
CASES = [
    ("FRAC.SLK",  [44.1, 12.3, 7.5, 43.6],                     2,
     "44.1e3 is 44,100 and over 32767, so E stops at 2"),
    ("BIG.SLK",   [3400000.0, 1200000.0, 900000.0, 2500000.0], -3,
     "3.4e6 scaled by 1e-2 is 34,000 and over 32767, so E is -3"),
    ("CAP.SLK",   [0.001, 0.002, 0.003, 0.004],                CH_E10_MAX,
     "0.004 would like E=6 (4000); CH_E10_MAX holds it to 4 (40)"),
    ("ROUND.SLK", [0.66666, 0.5, 0.25, 0.1],                   4,
     "0.66666e4 is 6666.6 - the rounding case"),
]
# The two-series file: a tens column and a millions column, so series one's
# exponent and series two's are nowhere near each other.
ORDER = "ORDER.SLK"
ORD_A = [10.0, 20.0, 30.0, 40.0]
ORD_B = [3400000.0, 1200000.0, 900000.0, 2500000.0]


def scaled(vals, e):
    """What ch_scale should write: value * 10^E, ROUNDED (82.13)."""
    out = []
    for v in vals:
        x = v * (10.0 ** e)
        # round-half-away-from-zero, which is fp_round's rule, not Python's
        # round-half-to-even - 6666.5 would differ and 0.5 cases are exactly
        # what check 4 is about
        out.append(int(x + (0.5 if x >= 0 else -0.5)))
    return out


def build_disk():
    os88fixture.need("build/chart.o88", "build/os8088-360.img")
    paths = []
    for name, col, _e, _why in CASES:
        cells = dict(((r, 0), v) for r, v in enumerate(col))
        open(os.path.join(ROOT, "build", name), "wb").write(F.write_sylk(cells))
        paths.append(os.path.join("build", name))
    cells = {}
    for r, (a, b) in enumerate(zip(ORD_A, ORD_B)):
        cells[(r, 0)], cells[(r, 1)] = a, b
    open(os.path.join(ROOT, "build", ORDER), "wb").write(F.write_sylk(cells))
    paths.append(os.path.join("build", ORDER))
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

        rd = lambda s: s16(u16(m.readseg(seg, S[s], 2)))

        def words(sym, n):
            return [s16(u16(m.readseg(seg, S[sym] + 2 * i, 2)))
                    for i in range(n)]

        def load(doc):
            ui.menu_pick("File", "Open...")
            ui.chooser_open(doc)
            ui.settle()

        names = ["1 FRACTIONS", "2 LARGE", "3 CAPPED", "4 ROUNDED"]
        for (doc, col, want_e, why), label in zip(CASES, names):
            load(doc)
            got_e = rd("ch_e10")
            got_w = words("ct_wvals", rd("ct_valcnt"))
            want_w = scaled(col, want_e)
            check(got_e == want_e and got_w == want_w, label,
                  "%r: %s. The WORDS are checked too, not just E - a scale "
                  "that picked the right exponent and wrote the wrong words "
                  "would draw a correct axis against wrong bars"
                  % (col, why),
                  got="E %d, words %r" % (got_e, got_w),
                  want="E %d, words %r" % (want_e, want_w))

        load(ORDER)
        e_one = rd("ch_e10")
        want_one = 2                    # the tens column; series two's is -3
        check(e_one == want_one, "5 WHOSE",
              "TWO SERIES, ONE EXPONENT WORD. ct_finalize scales series TWO "
              "first and series ONE last, so [ch_e10] is left holding the "
              "series the value axis is drawn from (82.13). Column A is a "
              "tens column (E=2) and column B a millions column (E=-3), so "
              "reversing those two calls is not a rounding difference - the "
              "axis would be labelled in millions beside bars drawn from "
              "tens",
              got="ch_e10 %d" % e_one,
              want="ch_e10 %d (series one's, not series two's -3)" % want_one)
    done("chartexp")


if __name__ == "__main__":
    sys.exit(main())

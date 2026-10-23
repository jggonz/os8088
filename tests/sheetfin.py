#!/usr/bin/env python3
"""SHEET's thirteen financial functions, computed by SHEET and checked by the
host (SPEC.md 81.37).

    make && python3 tests/sheetfin.py

WHY IT EXISTS: the financial family is the second tenant of CHART.OVL
(SPEC.md 82.16.10), and that move was not allowed to happen until something
could say it changed nothing - the rule 82.16.9 learned when the file-format
move failed twice and only the round-trip gate named why. This is that gate
for this family: written and passed against the RESIDENT code first, then
held against the moved one.

WHAT IT DRIVES is sheetfmt.py's route. The host authors a SYLK file of
formulas, SHEET opens it by association and saves it as SYLK, and the host
reads the values back off the floppy. Every formula is authored with a
DELIBERATELY WRONG cached value (-999). A SYLK cell carries both the
expression and the last answer, so a reader that trusted the answer instead
of recalculating would hand the host's own number back and pass - which is
the vacuous shape this tree has met more than once. -999 is not the answer to
anything here, so a pass means SHEET computed every one.

THE EXPECTATIONS ARE THE HOST'S OWN ARITHMETIC, from the definitions and not
from SHEET: the closed forms directly, RATE and IRR by bisection. Their
figures agree with the hand-verified tables in SPEC.md 81.37.2-81.37.8, which
were checked against Python the same way.

THE NESTING CASES are the ones the move is actually about. A financial
function's arguments are parsed by the RESIDENT evaluator, so
`=PMT(0.01,60,PV(...))` and `=SUM(PMT(...),1)` leave the module, re-enter the
evaluator, and - for the first - come back into the module while it is still
on the stack. The first must refuse with #VALUE! (sh_fnbusy: the argument
store is not banked per nesting level); the second must answer and leave the
parse where SUM expects it.
"""
import math
import os
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
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402

MACHINE = SF.MACHINE
SYS = SF.SYS
# THIS ROW'S OWN PATHS (docs/WRITING-TESTS.md 5.5). The runner runs three rows
# at once, and sheetfmt.py writes build/SHIN.SLK and build/sheetfmt.img - so
# the host file lives in a directory of its own and the image has its own
# name. The file keeps the name SHIN.SLK ON THE FLOPPY, because that is what
# puts it in the same listing row sheetfmt.py's coordinates double-click.
WORK = "build/sheetfin"
DISK = "build/sheetfin.img"
WRONG = -999.0

# IRR's range: Excel's own worked example, a 70,000 outlay and five inflows.
FLOWS = [-70000.0, 12000.0, 15000.0, 18000.0, 21000.0, 26000.0]
# ...and MIRR's, in column B: Microsoft's own documented example, whose
# published answer (12.61%) is the one figure in this file that did not come
# from arithmetic here. It is what caught the host's MIRR being right and
# SPEC.md 81.37.8's being wrong in the same direction as SHEET's.
MFLOWS = [-120000.0, 39000.0, 30000.0, 21000.0, 37000.0, 46000.0]
MIRR_PUBLISHED = 0.1261


# --- the host's arithmetic --------------------------------------------------
def fv(r, n, p, pv=0.0, t=0):
    if r == 0:
        return -(pv + p * n)
    f = (1 + r) ** n
    return -(pv * f + p * (1 + r * t) * (f - 1) / r)


def pv(r, n, p, fv_=0.0, t=0):
    if r == 0:
        return -(fv_ + p * n)
    f = (1 + r) ** n
    return -(fv_ + p * (1 + r * t) * (f - 1) / r) / f


def pmt(r, n, pv_, fv_=0.0, t=0):
    if r == 0:
        return -(pv_ + fv_) / n
    f = (1 + r) ** n
    return -(pv_ * f + fv_) * r / ((1 + r * t) * (f - 1))


def nper(r, p, pv_, fv_=0.0, t=0):
    if r == 0:
        return -(pv_ + fv_) / p
    a = p * (1 + r * t) / r
    return math.log((a - fv_) / (a + pv_)) / math.log(1 + r)


def ipmt(r, per, n, pv_, fv_=0.0, t=0):
    if t == 1 and per == 1:
        return 0.0                  # paid before any interest has accrued
    assert t == 0
    return fv(r, per - 1, pmt(r, n, pv_, fv_, t), pv_) * r


def ppmt(r, per, n, pv_, fv_=0.0, t=0):
    return pmt(r, n, pv_, fv_, t) - ipmt(r, per, n, pv_, fv_, t)


def npv(r, vals):
    return sum(v / (1 + r) ** (i + 1) for i, v in enumerate(vals))


def _bisect(f, lo, hi):
    flo = f(lo)
    for _ in range(200):
        mid = (lo + hi) / 2
        fm = f(mid)
        if (fm < 0) == (flo < 0):
            lo, flo = mid, fm
        else:
            hi = mid
    return (lo + hi) / 2


def irr(vals):
    return _bisect(lambda r: sum(v / (1 + r) ** i for i, v in enumerate(vals)),
                   -0.9, 1.0)


def rate(n, p, pv_, fv_=0.0, t=0):
    return _bisect(lambda r: pv_ * (1 + r) ** n
                   + p * (1 + r * t) * ((1 + r) ** n - 1) / r + fv_,
                   1e-9, 1.0)


def mirr(vals, fr, rr):
    n = len(vals)
    pos = [v if v > 0 else 0.0 for v in vals]
    neg = [v if v < 0 else 0.0 for v in vals]
    return ((-npv(rr, pos) * (1 + rr) ** n) / (npv(fr, neg) * (1 + fr))
            ) ** (1.0 / (n - 1)) - 1


def ddb(cost, salv, life, per):
    book = cost
    dep = 0.0
    for _ in range(per):
        dep = min(book * 2 / life, max(book - salv, 0.0))
        book -= dep
    return dep


NUM, VAL = ('err', '#NUM!'), ('err', '#VALUE!')

# (expression, expected, relative tolerance). Iterative answers get a looser
# one: SHEET stops at 1e-10 on the rate, the host bisects to the last bit.
CASES = [
    ('SLN(10000,1000,5)',            (10000 - 1000) / 5,                 1e-9),
    ('SYD(10000,1000,5,1)',          9000 * 5 * 2 / 30,                  1e-9),
    ('SYD(10000,1000,5,5)',          9000 * 1 * 2 / 30,                  1e-9),
    ('PMT(0.01,60,-20000)',          pmt(0.01, 60, -20000),              1e-9),
    ('PMT(0.01,60,-20000,0,1)',      pmt(0.01, 60, -20000, 0, 1),        1e-9),
    ('PV(0.01,60,-500)',             pv(0.01, 60, -500),                 1e-9),
    ('FV(0.01,60,-500)',             fv(0.01, 60, -500),                 1e-9),
    ('FV(0,10,-100)',                fv(0, 10, -100),                    1e-9),
    ('NPV(0.1,100,200,300)',         npv(0.1, [100, 200, 300]),          1e-9),
    ('NPER(0.01,-500,20000)',        nper(0.01, -500, 20000),            1e-9),
    ('NPER(0,-100,1000)',            nper(0, -100, 1000),                1e-9),
    ('DDB(10000,1000,5,1)',          ddb(10000, 1000, 5, 1),             1e-9),
    ('DDB(10000,1000,5,2)',          ddb(10000, 1000, 5, 2),             1e-9),
    ('DDB(10000,1000,5,5)',          ddb(10000, 1000, 5, 5),             1e-9),
    ('IPMT(0.01,2,60,20000)',        ipmt(0.01, 2, 60, 20000),           1e-9),
    ('IPMT(0.01,1,60,20000,0,1)',    0.0,                                1e-9),
    ('PPMT(0.01,60,60,20000)',       ppmt(0.01, 60, 60, 20000),          1e-9),
    ('RATE(60,-500,20000)',          rate(60, -500, 20000),              1e-6),
    ('RATE(10,100,800)',             NUM,                                0),
    ('IRR(A1:A6)',                   irr(FLOWS),                         1e-6),
    ('IRR(A1:A6,0.5)',               irr(FLOWS),                         1e-6),
    ('MIRR(A1:A6,0.1,0.12)',         mirr(FLOWS, 0.1, 0.12),             1e-9),
    ('MIRR(B1:B6,0.1,0.12)',         mirr(MFLOWS, 0.1, 0.12),            1e-9),
    ('IRR(A2:A6)',                   NUM,                                0),
    ('PMT(0.01,60,PV(0.01,60,-500))', VAL,                               0),
    ('SUM(PMT(0.01,60,-20000),1)',   pmt(0.01, 60, -20000) + 1,          1e-9),
    ('PMT(0.01,60,-20000)*2',        pmt(0.01, 60, -20000) * 2,          1e-9),
]
COL = 2                     # column C; the flows are A1:A6


def cells():
    out = {(i, 0): v for i, v in enumerate(FLOWS)}
    out.update({(i, 1): v for i, v in enumerate(MFLOWS)})
    for i, (expr, _, _) in enumerate(CASES):
        out[(i, COL)] = ('formula', expr, WRONG)
    return out


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, "SHIN.SLK")
    open(src, "wb").write(F.write_sylk(cells()))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "APPS:build/sheet.o88",
                    "APPS:build/CHART.OVL", "APPS:build/MACRO.OVL", "APPS:" + src],
                   check=True, stdout=subprocess.DEVNULL)


def wait_for(m, name, budget=600.0):
    """Run until NAME is on B:, or BUDGET seconds of settling pass.

    M.settle waits for the SCREEN to stop changing, and a save that first
    recalculates every formula draws nothing while it works - so settle
    answered "quiet" while RATE and IRR were still iterating, and the disk was
    read before the file existed. The file is the event this row is about, so
    it is what it waits for. Answers the Flush volume it last read."""
    spent = 0.0
    while True:
        vol = os88flush.Flush(marty=m).volume(1)
        if name in vol.names() or spent >= budget:
            return vol
        spent += M.settle(m, quiet=2.0, stable=2, limit=60) or 4.0


def shot(m, tag):
    """SHEETFIN_SHOT=1 keeps the glass at each step in build/sheetfin/ -
    the way to read a run that saved nothing rather than a wrong number."""
    if os.environ.get("SHEETFIN_SHOT"):
        w, h, rows = m.vram("cga")
        M.write_png(os.path.join(WORK, "%s.png" % tag), w, h, rows)


def agrees(expect, got, tol):
    if isinstance(got, tuple) and got and got[0] == 'formula':
        got = got[2]
    if isinstance(expect, tuple):
        return got == expect
    if not isinstance(got, float):
        return False
    return abs(got - expect) <= tol * max(1.0, abs(expect), abs(got))


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    with M.launch(SYS, apps=DISK, machine=MACHINE) as m:
        M.settle(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, lambda n: m.sym(n), M.settle, letter="B")
        M.settle(m)
        mo.dblclick(*SF.APPS_FOLDER)
        M.settle(m)
        SF.open_shin(m, mo)           # the ASSOCIATION opens it
        M.settle(m, limit=240)              # RATE and IRR iterate on a 5150
        shot(m, "1-loaded")

        mo.menu(SF.FILE_MENU[0], SF.FILE_MENU[1], SF.SAVE_AS[0], SF.SAVE_AS[1])
        M.settle(m)
        mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['slk']))
        M.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        M.settle(m, limit=120)
        shot(m, "2-savedialog")
        mo.click(*SF.SAVE_BUTTON)
        M.settle(m, limit=180)
        shot(m, "3-saved")

        vol = wait_for(m, 'SHIN.SLK')
        names = vol.names()
        check('SHIN.SLK' in names, "SHIN.SLK written back",
              "not on the disk after Save As SYLK (names: %s)" % names)
        raw = vol.read('SHIN.SLK') if 'SHIN.SLK' in names else b''
        # The file SHEET wrote, kept: two builds that should compute the same
        # thing can then be compared byte for byte (`cmp`), which is a
        # stronger statement than every value agreeing within a tolerance.
        open(os.path.join(WORK, "SHIN.OUT"), "wb").write(raw)
        got = F.read('SHIN.SLK', data=raw, kind='sylk') if raw else {}

    check(abs(mirr(MFLOWS, 0.1, 0.12) - MIRR_PUBLISHED) < 5e-5,
          "the host's MIRR reproduces Excel's published 12.61%",
          "the reference formula itself is wrong: it gives %r for Microsoft's "
          "worked example" % mirr(MFLOWS, 0.1, 0.12))
    stale = [expr for i, (expr, _, _) in enumerate(CASES)
             if agrees(WRONG, got.get((i, COL)), 1e-12)]
    check(not stale, "SHEET recalculated every formula",
          "%d cell(s) came back as the -999 the host authored - SHEET wrote "
          "the cached value instead of computing it, so nothing below "
          "measured anything: %s" % (len(stale), stale))
    for i, (expr, expect, tol) in enumerate(CASES):
        g = got.get((i, COL))
        kept = isinstance(g, tuple) and g and g[0] == 'formula'
        check(agrees(expect, g, tol) and kept, "=%s" % expr,
              "SHEET says %r, the host says %r%s"
              % (g[2] if kept else g, expect,
                 "" if kept else " - and the cell is no longer a formula"))
    done("sheetfin")


if __name__ == "__main__":
    main()

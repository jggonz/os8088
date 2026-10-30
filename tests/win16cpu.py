#!/usr/bin/env python3
"""win16cpu - the CPU floor on the glass (SPEC.md 124.4, 124.17,
docs/plans/WIN16-PLAN.md S11).

    python3 tests/win16cpu.py            QEMU, a 386 (row win16cpu)
    python3 tests/win16cpu.py --marty    MartyPC: a stock 8088, then a V20
                                         (row win16cpuxt)

*As an XT owner, the Windows programs my CPU can run, run, and the rest
tell me plainly what they need.* Windows 3.0's three 8086 accessories -
CALC, CLOCK and REVERSI - each launched and taken to its message loop with
its window up; Windows 3.1's Calculator, an 80186 program, beside them:

  QEMU (a 386)     all four run; WIN16's census never sweeps them (the CPU
                   answers 186 code itself);
  a stock 8088     the three run, each SWEPT first by WIN16's own census
                   (SPEC.md 124.4.1) and passed; W31CALC is refused with
                   SPEC.md 124.4's sentence in WIN16's window;
  a NEC V20        W31CALC runs, since a V20 runs 186 code.

It prints each launch's cost on the XT, census included: the double-click
to the program idle in GetMessage. WinMine's own refusal on an 8088 is
`win16minesxt`'s. The programs are Microsoft's and come from
win16ref/samples/ by the win16-reference skill; none there is a SKIP.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88geom as geom                                       # noqa: E402
from win16mines import QDrive, MDrive, b, XT, V20, TICK_MS    # noqa: E402
from win16fpu import Prog, open_b, pkg_syms, ticks, title, gone  # noqa: E402

W16S_REFUSED = 2
W16S_RUNNING = 3
W16S_ENDED = 4
REFUSAL = "Needs a V20, 80186 or better."
EIGHT = ("CALC.EXE", "CLOCK.EXE", "REVERSI.EXE")      # Windows 3.0's, 8086
ONE86 = "W31CALC.EXE"                                 # Windows 3.1's, 186


def progs():
    have = [p for p in EIGHT + (ONE86,)
            if os.path.exists(b(os.path.join("win16", p)))]
    return have


def bdisk(out, size, names):
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", out,
                    "--size", str(size), b("win16.o88"), b("win16/DOS.O88")]
                   + [b(os.path.join("win16", n)) for n in names]
                   + ["--folder", "SYSTEM/APPDATA", "--folder",
                      "SYSTEM/APPDATA/WIN16"],
                   check=True, capture_output=True)


def raise_(d, ui, w):
    """win16calc's: the window to the front at a point on the glass"""
    top = ui.front()
    if top is not None and top.i == w.i:
        return
    pt, _ = ui._raise_point(w)
    d.click(*pt)
    d.wait(lambda: (lambda f: f is not None and f.i == w.i)(ui.front()),
           30, "B:'s window to come to the front")
    d.idle(2.0)


def at_loop(p):
    """idle in its message loop. W16S_RUN is set BEFORE w16_load, so on an
    8088 - where the read, the relocation and the census are seconds - the
    state alone is a program still loading; "Running" is written after"""
    return (p.pb("w16_state") == W16S_RUNNING and p.text("w16_l2") == "Running"
            and p.pb("w16_inprog") == 0)


def run1(d, ui, sym, S, dw, name, want, bad, tag):
    """launch one program: want "run" (its message loop, its window up, the
    census as `census` says) or "refuse" (SPEC.md 124.4's sentence)"""
    raise_(d, ui, dw)           # the last program's window may cover its row
    if hasattr(ui.m, "status"):  # MartyPC: a CGA Disk window shows five rows,
        ui.scroll_to(ui.entry(name, dw)[0], win=dw)   # and the 4th program
        d.idle(2.0)                                     # is below them
    p = Prog(d, ui, sym, S, name)
    t0 = ticks(d)
    p.launch(dw)
    d.wait(lambda: p.pb("w16_state") in (W16S_REFUSED, W16S_ENDED)
           or at_loop(p), 900, "%s to run or be refused" % name)
    t = ticks(d) - t0
    st = p.pb("w16_state")
    l1, l2 = p.text("w16_l1"), p.text("w16_l2")
    cpu186 = p.pb("w16_cpu186")
    print("%s %-12s state %d, %s; double-click to %s %d ticks (%.1f s)%s"
          % (tag, name, st, "186 code runs here" if cpu186 else "an 8086",
             "its message loop" if st == W16S_RUNNING else "the verdict",
             t, t * TICK_MS / 1000, "" if st == W16S_RUNNING
             else " - %r / %r" % (l1, l2)))
    if want == "refuse":
        if st != W16S_REFUSED or REFUSAL not in (l1 + l2):
            bad.append("%s %s: not refused with %r (state %d, %r / %r)"
                       % (tag, name, REFUSAL, st, l1, l2))
        return p
    if st != W16S_RUNNING:
        bad.append("%s %s: did not run (state %d, %r / %r)" % (tag, name, st, l1, l2))
        return p
    d.idle(4.0)
    w = [x for x in ui.windows() if x.i == p.w.i]
    if not w or not w[0].visible:
        bad.append("%s %s: running, but its window is not up" % (tag, name))
        return p
    title(ui, p)
    d.click(*geom.close_xy(p.w.x, p.w.y, p.w.w, m=d, sym=sym))
    d.wait(lambda: gone(ui, p), 120, "%s's window to close" % name)
    d.idle(4.0)
    return p


def qemu_leg(bad, names):
    import os88ui
    import xmcheck
    work = tempfile.mkdtemp(prefix="w16u-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        bdisk(bimg, 1440, names)
        d = QDrive(work, bimg, False)
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = pkg_syms()
        dw = open_b(d, ui, xmcheck.sym)
        for n in names:
            run1(d, ui, xmcheck.sym, S, dw, n, "run", bad, "386")
        d.p.close()
    finally:
        shutil.rmtree(work, ignore_errors=True)


def marty_leg(bad, names):
    import os88ui
    work = tempfile.mkdtemp(prefix="w16ux-", dir=os.path.join(ROOT, "build"))
    try:
        bimg = os.path.join(work, "b360.img")
        bdisk(bimg, 360, names)
        S = pkg_syms()
        with os88ui.boot(b("os8088-360.img"), apps=bimg, machine=XT,
                         verbose=False) as ui:
            d = MDrive(ui)
            dw = open_b(d, ui, ui.sym)
            for n in names:
                run1(d, ui, ui.sym, S, dw, n,
                     "refuse" if n == ONE86 else "run", bad, "8088")
        if ONE86 in names:
            with os88ui.boot(b("os8088-360.img"), apps=bimg, machine=V20,
                             verbose=False) as ui:
                d = MDrive(ui)
                dw = open_b(d, ui, ui.sym)
                run1(d, ui, ui.sym, S, dw, ONE86, "run", bad, "V20 ")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--marty", action="store_true")
    a = ap.parse_args()
    names = progs()
    if os.environ.get("W16CPU_ONLY"):
        names = [x for x in names if x in os.environ["W16CPU_ONLY"].split(",")]
    if not names:
        print("win16cpu: SKIP - none of %s in build/win16/ (the "
              "win16-reference skill copies Microsoft's there)"
              % ", ".join(EIGHT + (ONE86,)))
        return 77
    bad = []
    if a.marty:
        marty_leg(bad, names)
    else:
        qemu_leg(bad, names)
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16cpu: %s" % ("an 8088 runs the 8086 programs and names "
                                "what the rest need; a V20 runs those"
                                if a.marty else "a 386 runs all of them"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

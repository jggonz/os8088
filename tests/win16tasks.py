#!/usr/bin/env python3
"""win16tasks - one instance, several programs (SPEC.md 124.28,
docs/plans/WIN16-ANY-PLAN.md S21).

    python3 tests/win16tasks.py            (row win16tasks)
    python3 tests/win16tasks.py --keep     leave the QEMU for a look

*As a user, a Help menu opens Windows Help beside the program, and Program
Manager starts Notepad.*

TASKS.EXE (tests/win16/tasks.c, `make win16fixtures`) starts TASKB.EXE
(tests/win16/taskb.c) in its own WIN16 with WinExec, a command tail and an
nCmdShow, and the two check each other: posts both ways, PostAppMessage to
a task with no window, a SendMessage answered inside the sender's call with
a send back the other way inside it, a PostMessage and a SendMessage to
HWND_BROADCAST that reach both programs' windows, and a DDE conversation
from WM_DDE_INITIATE to WM_DDE_TERMINATE whose data is a GMEM_DDESHARE
block that must outlive the task that made it. TASKB is closed and TASKS
goes on alone; then TASKS starts TASKB again with ShellExecute and ends,
and that TASKB, holding WIN16's window now, puts up the MessageBox this row
reads: "TASKS OK", or each check that failed on either side.

The row also watches the kernel's claim records while the programs run:
the instance's (WIN16's, with every task's memory) are at most the eight
an owner may hold (SPEC.md 124.25.1), and when the MessageBox is closed and
the instance ends, the heap is the one before the launch: every task's
claim and block went back.
"""
import argparse
import os
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
from win16mines import QDrive, b                              # noqa: E402
import win16fpu as F                                          # noqa: E402
from win16desk import claims, heap_is                         # noqa: E402

NAMES = ["TASKS.EXE", "TASKB.EXE"]
WANT = "TASKS OK"
MAXCLAIMS = 8                   # SPEC.md 50: an owner's claims at most


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    for n in NAMES:
        if not os.path.exists(b(os.path.join("win16", n))):
            print("win16tasks: SKIP - no build/win16/%s (`make win16fixtures`)" % n)
            return 77
    import os88ui
    import xmcheck
    sym = xmcheck.sym
    bad = []
    work = tempfile.mkdtemp(prefix="w16t-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, NAMES)
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, sym)
        d.idle(2.0)
        m0 = claims(d, sym)
        print("0. B: open, %d heap claims before TASKS" % len(m0))
        p = F.Prog(d, ui, sym, S, NAMES[0])
        p.launch(dw)
        own = [c[2] for c in claims(d, sym) if c[0] == p.seg]
        if not own:
            bad.append("no claim record at WIN16's segment %04x" % p.seg)
            own = [None]
        seen = {"max": 0, "tasks": 0}

        def ready():
            n = sum(1 for c in claims(d, sym) if c[2] == own[0])
            seen["max"] = max(seen["max"], n)
            seen["tasks"] = max(seen["tasks"], p.pb("w16_ntk"))
            return p.waiting_msgbox(F.msgrow())
        d.wait(ready, 600, "TASKS's MessageBox")
        d.idle(3.0)
        st = p.pb("w16_state")
        print("1. state %d, at most %d tasks and %d claims of the instance's "
              "seen" % (st, seen["tasks"], seen["max"]))
        if st in (F.W16S_REFUSED, F.W16S_ENDED):
            bad.append("WIN16 %s it: %s / %s" % (
                "refused" if st == F.W16S_REFUSED else "ended",
                p.text("w16_l1"), p.text("w16_l2")))
        else:
            got = p.msgtext()
            print("   " + got.replace("\n", "\n   "))
            if got != WANT:
                bad.append("the MessageBox says %r, not %r" % (got, WANT))
            if seen["tasks"] < 2:
                bad.append("never more than %d task in the instance" % seen["tasks"])
            if seen["max"] > MAXCLAIMS:
                bad.append("%d claims of the instance's, past the %d an owner "
                           "may hold" % (seen["max"], MAXCLAIMS))
            d.key("Enter")
            d.wait(lambda: p.pb("w16_state") == F.W16S_ENDED or F.gone(ui, p),
                   60, "the instance to end after its MessageBox")
            d.idle(6.0)
            heap_is(d, sym, m0, "2. the instance ended", bad)
        if not a.keep:
            d.p.close()
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16tasks: TASKS's and TASKB's checks passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

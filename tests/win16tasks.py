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

That the instance held TWO tasks at once is read off WIN16's task table at
the MessageBox, a quiescent moment, and not sampled while they run: TASKS,
the instance's first program, has record 0 (`w16_cur` starts at 0) and
WinExec and ShellExecute take the first FREE record, so the TASKB left
holding the MessageBox is in a record other than 0 only if it was started
while TASKS still held record 0 (and TASKS's own GetNumTasks() == 2,
"two", says the same from inside). The row used to poll `w16_ntk` from the
host every tenth of a second for a high-water mark of 2, and on a fast
host QEMU ran the whole two-task stretch between two polls: "never more
than 1 task in the instance" with TASKS OK on the glass, two runs in three
alone.
"""
import argparse
import os
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88geom as geom                                       # noqa: E402
from win16mines import QDrive, b, u16                         # noqa: E402
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
        own = [None]                # the instance's slot: its image's owner
        raw = d.read(sym("inst_tab"), geom.INST_MAX * 32)
        for i in range(geom.INST_MAX):
            r = raw[i * 32:(i + 1) * 32]
            if r[0] == 1 and u16(r[6:8]) == p.seg:
                own = [i]
        if own[0] is None:
            bad.append("no live instance at WIN16's segment %04x" % p.seg)
        seen = {"max": 0}

        def ready():
            # the image's claim is the slot's; every claim WIN16 makes is
            # stamped with the segment it runs in (SPEC.md 50.3, mem_own).
            # A SAMPLE, so it can miss a peak and never invent one: the
            # bound below is a check that may pass short, not a measurement
            n = sum(1 for c in claims(d, sym) if c[2] in (own[0], p.seg))
            seen["max"] = max(seen["max"], n)
            return p.waiting_msgbox(F.msgrow())
        d.wait(ready, 600, "TASKS's MessageBox")
        d.idle(3.0)
        st = p.pb("w16_state")
        raw = d.read((p.seg << 4) + S["w16_task"], F.W16_MAXTASK * F.W16K_SIZE)
        live = [i for i in range(F.W16_MAXTASK)
                if raw[i * F.W16K_SIZE + F.W16K_ST]]
        print("1. state %d, task records live %s (current %d), at most %d "
              "claims of the instance's seen" % (st, live, p.pb("w16_cur"),
                                                  seen["max"]))
        if st in (F.W16S_REFUSED, F.W16S_ENDED):
            bad.append("WIN16 %s it: %s / %s" % (
                "refused" if st == F.W16S_REFUSED else "ended",
                p.text("w16_l1"), p.text("w16_l2")))
        else:
            got = p.msgtext()
            print("   " + got.replace("\n", "\n   "))
            if got != WANT:
                bad.append("the MessageBox says %r, not %r" % (got, WANT))
            if len(live) != 1 or live[0] == 0:
                bad.append("the MessageBox's TASKB is in task record(s) %s: "
                           "one task, in a record other than TASKS's 0, is "
                           "one started while TASKS still ran in the "
                           "instance (SPEC.md 124.28.1)" % live)
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

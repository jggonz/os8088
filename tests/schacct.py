#!/usr/bin/env python3
"""SPEC.md 8.1.1: the scheduler charges a slice only when the task CHANGES.

    python3 tests/schacct.py

`sch_switch` picks round-robin from `cur+1` and falls back to *resuming the
outgoing task* when nothing else is ready, so a switch that finds nothing else
runnable resumes the task that was already running.  Charging a slice to task X
and then carrying on running task X is a no-op, and it used to cost 500 cycles
1,048 times a second - 10.9% of a 4.77 MHz 8088 on an idle desktop.

AND SPEC.md 8.1.2 THEN SUBSUMED IT, which this file has to say plainly because
it is the reason its strongest row is gone.  A blocked `ui_task` and a Task
Manager that sleeps its interval leave the machine switching **53.8 times a
second**, so there is almost nothing left to skip: reverting 8.1.1 today moves
accounting from 53.6/s to 72.2/s - **0.56% of the machine to 0.76%** - where
before 8.1.2 the same revert was 10.94%, and 14.98% with the page open.

So 8.1.1 is still correct, still exact and still ten bytes, and it is **no
longer load-bearing**.  What is left here is a CORRECTNESS check rather than a
performance one: that the books balance, that the charge lands on the task
that ran, and that a gross regression - the unconditional call on a machine
that switches a lot - is still caught.  Row 4 arms itself only when there IS a
spinning task to make the strong version meaningful, and says so when there is
not; on today's shipped disk there is not one, because the Task Manager was
the last of them.

THE FAILURE THIS CATCHES IS SILENT IN BOTH DIRECTIONS, which is why it is a
row rather than a note in the plan document:

  * The compare going away, or being inverted, puts the 10.9% back and NOTHING
    ELSE CHANGES.  Every counter still reads the same value - that is the whole
    point of the rule, that it is exact - so no screen, no snapshot and no
    other test can tell.  Only the RATE says so.
  * The compare being right and the accounting being wrong is the opposite
    shape: the rates look perfect and the Task Manager quietly reports the
    wrong task.  So the books are checked too, against the emulator's own
    cycle counter, which is an authority outside the kernel's arithmetic.

THREE ASSERTIONS, and the first exists so the other two cannot fail
confusingly:

  1. THE QUIET CASE.  A bare desktop charges at a tick-bounded rate.  It
     cannot separate 54 from 72 and does not try; what it catches is the
     1,044/s world, which is what an unconditional call plus any spinning
     task looks like.
  2. THE SAME, with a second task open.
  3. THE STRONG FORM, IF IT CAN BE ARMED.  When something on the machine is
     spinning - `sch_resume` well over 500/s - `sch_account` must stay down
     at the tick's rate, which is a 28:1 separation.  Nothing on the shipped
     disk spins any more, so this reports rather than asserts; it is left in
     because the day something does, this is the row that will see it.
  4. NOTHING IS LOST.  The whole elapsed window is still billed to somebody -
     a skipped charge has to roll its interval into the next one rather than
     drop it, and this is the direct test of that.
  5. THE BOOKS.  With the Task Manager open - two runnable tasks, where every
     switch IS a real change and this rule saves nothing by design - the
     per-task cycles still account for the whole of the elapsed time.
     `sch_cycles` counts PIT cycles at 1.19318 MHz against the 8088's
     4.772727, exactly 4:1, so the emulator's counter prices the kernel's
     arithmetic from outside it.

ON A 5150 UNDER MARTYPC, because every number here is a cycle count at
4.77 MHz and QEMU cannot time anything (docs/TESTING.md).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty                                             # noqa: E402
import os88mouse                                             # noqa: E402
import os88sym                                               # noqa: E402

S = os88sym.linear
HZ = 4772727.0                  # the 5150's, which is what MartyPC counts in
PIT_PER_CPU = 4                 # 14.31818/12 against 14.31818/3, exactly

# The chip menu and its Task Manager item - MENU-BAR coordinates, the same on
# every adapter because the bar is.
CHIP, MI_TASKS = (12, 8), (60, 60)

# The tick is 18.2065 Hz. sch_isr charges on every one of them, and since
# SPEC.md 8.1.2 the two real switches a tick - into the idle task and back -
# charge as well, so the floor is ~55/s rather than ~18. The bound is
# deliberately loose: what it has to separate is 55 from 1,541, and a
# tolerance tight enough to argue about an extra switch would fail on a
# machine that happened to have something else runnable during the window.
TICK_HZ = 18.2065
RATE_MAX = 150.0                # /s. ~3x the floor, 10x below the switch rate
SPIN_MIN = 500.0                # /s of sch_resume before row 2 means anything

FAILED = []


def say(*a):
    print(*a)
    sys.stdout.flush()


def check(cond, what, why="", got=None, want=None):
    if cond:
        say("  ok   %s" % what)
        return True
    m = what
    if got is not None or want is not None:
        m += "\n        want: %s\n        got:  %s" % (want, got)
    if why:
        m += "\n        why:  " + why
    FAILED.append(m)
    say("  FAIL %s" % what)
    return False


def report():
    if FAILED:
        say("schacct: %d FAILED" % len(FAILED))
        for m in FAILED:
            say("  FAIL: %s" % m)
        sys.exit(1)
    say("schacct: pass")
    sys.exit(0)


def cycles(m):
    """sch_cycles[], read with the guest stopped (the ISR writes them)."""
    raw = m.readseg(0x60, S("sch_cycles") - 0x600, 32)
    return [struct.unpack_from("<I", raw, i * 4)[0] for i in range(8)]


def hits_per_second(m, name, secs=1.0):
    """How often `name` is entered, over `secs` of GUEST time."""
    m.bp_exec(name)
    n, c0 = 0, m.status()["cycles"]
    while True:
        m.run()
        if m.wait_stop(5.0) is None:
            break                       # nothing reached it at all
        if m.status()["cycles"] - c0 > secs * HZ:
            break
        n += 1
    span = (m.status()["cycles"] - c0) / HZ
    m.bp_exec()
    m.run()                             # leave the guest RUNNING, or the next
    return n / span, span               # mouse move happens on a stopped one


def resume_changes(m, n=200):
    """Of `n` consecutive sch_resume entries, how many changed sch_cur?"""
    off = S("sch_cur") - 0x600
    m.bp_exec("sch_resume")
    seq = []
    for _ in range(n):
        m.run()
        if m.wait_stop(5.0) is None:
            break
        seq.append(m.readseg(0x60, off, 1)[0])
    m.bp_exec()
    m.run()
    chg = sum(1 for i in range(1, len(seq)) if seq[i] != seq[i - 1])
    return len(seq), chg


def books(m, secs=3.0):
    """Every task's cycles over a window, against the emulator's own count."""
    m.pause()
    a, c0 = cycles(m), m.status()["cycles"]
    m.run()
    m.advance(cycles=int(secs * HZ))
    m.run()
    m.pause()
    b, c1 = cycles(m), m.status()["cycles"]
    m.run()
    d = [(b[i] - a[i]) & 0xFFFFFFFF for i in range(8)]
    return d, sum(d) * float(PIT_PER_CPU) / (c1 - c0)


def main(argv):
    os.chdir(ROOT)
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine="os8088_5150_cga_gla") as m:

        # --- 1. the bare desktop, FIRST, while it really is bare ----------
        rate, span = hits_per_second(m, "sch_account")
        check(rate <= RATE_MAX,
              "a bare desktop charges %.1f/s (it charged 1,044 before 8.1.1, "
              "and 18 before 8.1.2 gave it two real switches a tick)" % rate,
              "the floor is sch_isr's own charge plus the two switches in and "
              "out of the idle task. Well above that means the unconditional "
              "call is back; well below means sch_isr has stopped charging, "
              "and it is sch_isr's charge that bounds the stale interval to "
              "one tick and so makes the whole skip safe. IT IS MEASURED "
              "BEFORE ANYTHING IS OPENED, because a Task Manager window on "
              "the screen is a spinning worker and not a bare desktop",
              got="%.1f /s" % rate, want="<= %.1f" % RATE_MAX)

        # --- 2/3. the same with a window open, and the strong form if the
        #          machine will give it to us ------------------------------
        mo = os88mouse.Mouse(marty=m)
        mo.menu(CHIP[0], CHIP[1], MI_TASKS[0], MI_TASKS[1])
        m.advance(frames=240)
        m.run()

        rate, span = hits_per_second(m, "sch_account")
        check(rate <= RATE_MAX,
              "and with a window open it still charges %.1f/s" % rate,
              "before SPEC.md 8.1.1 this state was 1,430/s and 14.98% of the "
              "machine; before 8.1.2 removed the Task Manager's spin as well "
              "it was 1,541 switches a second with 6 of 200 changing",
              got="%.1f /s" % rate, want="<= %.1f" % RATE_MAX)

        switches = hits_per_second(m, "sch_resume")[0]
        if switches >= SPIN_MIN:
            n, chg = resume_changes(m)
            check(chg <= n * 0.2,
                  "THE STRONG FORM: %.0f switches/s, %d of %d change, and "
                  "sch_account stays at %.1f/s" % (switches, chg, n, rate),
                  "something is spinning, so SPEC.md 8.1.1 has switches to "
                  "skip and this is the %.0f:1 separation the rule is worth "
                  "when a machine is busy" % (switches / max(rate, 1)),
                  got="%d of %d changed" % (chg, n),
                  want="<= %d" % int(n * 0.2))
        else:
            say("  --   the strong form is NOT ARMED: %.0f switches/s, and it "
                "needs %.0f" % (switches, SPIN_MIN))
            say("       Nothing on the shipped disk spins since SPEC.md 28.7 "
                "retired the Task Manager's")
            say("       worker loop - which is the point of that change, not "
                "a gap in this one.")

        # --- 4/5. the books, and that nothing is lost ---------------------
        d, bal = books(m)
        live = {i: v for i, v in enumerate(d) if v}
        check(0.95 <= bal <= 1.02,
              "the books account for the elapsed time (%.1f%%, tasks %s)"
              % (bal * 100, sorted(live)),
              "sch_cycles counts PIT cycles at exactly a quarter of the "
              "8088's clock, so the emulator's counter prices the kernel's "
              "arithmetic from outside it. Under 95%% means a slice is being "
              "dropped - which is what a change-compare gets wrong when it "
              "skips a switch that was real; over 102%% means one is being "
              "charged twice. THIS is the row that says a skipped charge "
              "rolled its interval into the next one instead of vanishing",
              got="%.1f%%" % (bal * 100), want="95..102%")

        check(len(live) >= 2,
              "more than one task is billed (%s)"
              % ", ".join("task %d %.1f%%" % (i, 100.0 * v / max(1, sum(d)))
                          for i, v in sorted(live.items())),
              "one task holding the whole total with the Task Manager open "
              "means the charge is landing on the wrong slot - the .pick "
              "charge must happen BEFORE [sch_cur] is written, not after")

    report()


main(sys.argv[1:])

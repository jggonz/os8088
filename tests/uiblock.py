#!/usr/bin/env python3
"""SPEC.md 8.1.2: ui_task BLOCKS, and waking it back up is not slower.

    python3 tests/uiblock.py

The UI task used to go round its poll loop 1,134 times a second and find
nothing to do 56 times in 57.  It now sleeps until the next tick, or until an
ISR wakes it, and the machine spends an idle desktop HALTED in the idle task.

TWO THINGS CAN GO WRONG HERE AND ONLY ONE OF THEM IS VISIBLE.

The first is that the blocking stops working - a wake path that stops waking,
a scan that stops picking the idle task - and the machine quietly goes back to
spinning.  Nothing on screen changes; the desktop looks and behaves exactly
the same.  Only the RATES say so, which is rows 1-3.

The second is the one that matters to a person: THE LOST WAKEUP (SPEC.md
8.1.2.3).  An ISR that fires while a pass is running has already delivered its
event; if the pass then sleeps anyway, the event sits there until the tick.
Measured before the guard existed, that was 1 sample in 14 at 54.15 ms against
a 5.20 ms median - a whole tick, on a real mouse event, and exactly the
visible hitch this design has to avoid.  A median cannot see it.  Row 4 is a
MAXIMUM for that reason, and it is the row to read when someone reports that
the pointer or the menus feel "sticky".

The comparison it is written against, 40 samples each, from the mouse ISR
finishing a packet to the next ui_task pass (the 1200-baud serial line's own
22.5 ms of packet transit excluded - both kernels pay it):

    make NOUIBLOCK=1  (the spin)   median 5.31 ms   min 4.44   max 6.57
    default (blocking)             median 5.18 ms   min 5.03   max 5.90

The blocking kernel is better on every statistic, which is why the bound below
is generous rather than tight: what it has to separate is 6 ms from 55.

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
HZ = 4772727.0
PIT_PER_CPU = 4

# The tick is 18.2065 Hz. What this has to separate is 18 from 1,134.
PASS_MAX = 30.0                 # /s
# A tick is 54.9 ms and the guard's job is to keep every wake far below one.
# 15 ms is 2.5x the worst either kernel measured and a third of a tick.
LAT_MAX = 15.0                  # ms
IDLE_MIN = 90.0                 # % of a bare desktop the idle task must hold

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
        say("uiblock: %d FAILED" % len(FAILED))
        for m in FAILED:
            say("  FAIL: %s" % m)
        sys.exit(1)
    say("uiblock: pass")
    sys.exit(0)


def cycles(m):
    raw = m.readseg(0x60, S("sch_cycles") - 0x600, 32)
    return [struct.unpack_from("<i", raw, i * 4)[0] for i in range(8)]


def hits_per_second(m, name, secs=2.0):
    m.bp_exec(name)
    n, c0 = 0, m.status()["cycles"]
    while True:
        m.run()
        if m.wait_stop(5.0) is None:
            break
        if m.status()["cycles"] - c0 > secs * HZ:
            break
        n += 1
    span = (m.status()["cycles"] - c0) / HZ
    m.bp_exec()
    m.run()
    return n / max(span, 1e-9)


def wake_latency(m, n=24):
    """From the mouse ISR drawing the pointer to the next ui_task pass.

    cur_move is called by mou_apply, in the ISR, on every motion packet
    (SPEC.md 7.1.2) - so it marks the instant the kernel has the event and
    everything after it is the SCHEDULER's share.
    """
    mo = os88mouse.Mouse(marty=m)
    mo.to(300, 120)
    m.advance(frames=30)
    m.run()
    out = []
    for i in range(n):
        m.bp_exec("cur_move")
        m.pause()
        m.mouse(dx=2, dy=1)
        m.run()
        if m.wait_stop(5.0) is None:
            m.bp_exec()
            m.run()
            continue
        c0 = m.status()["cycles"]
        m.bp_exec("blk_pass")
        m.run()
        if m.wait_stop(5.0):
            out.append((m.status()["cycles"] - c0) / HZ * 1e3)
        m.bp_exec()
        m.run()
        # dither the phase within the tick, or every sample lands alike
        m.advance(cycles=int(HZ * 0.009 * (1 + (i % 5))))
        m.run()
    return sorted(out)


def main(argv):
    os.chdir(ROOT)
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine="os8088_5150_cga_gla") as m:

        slot = m.readseg(0x60, S("sch_idleslot") - 0x600, 1)[0]
        check(slot != 0xFF,
              "the idle task got a slot (%d)" % slot,
              "sch_idle_start found no free task slot, so sch_switch's "
              "fallback is still 'resume the outgoing task' - which with a "
              "ui_task that can SLEEP means resuming a sleeper. Everything "
              "below would fail, and this says why")

        rate = hits_per_second(m, "blk_pass")
        check(rate <= PASS_MAX,
              "ui_task runs at the TICK rate, not the spin rate (%.1f/s)"
              % rate,
              "the loop is back to polling. Nothing on screen changes when "
              "this regresses - the desktop looks and behaves the same - so "
              "this rate is the only thing that can see it",
              got="%.1f /s" % rate, want="<= %.1f (it spun at 1,134)" % PASS_MAX)

        m.pause()
        a, c0 = cycles(m), m.status()["cycles"]
        m.run()
        m.advance(cycles=int(3.0 * HZ))
        m.run()
        m.pause()
        b, c1 = cycles(m), m.status()["cycles"]
        m.run()
        d = [b[i] - a[i] for i in range(8)]
        el = c1 - c0
        idle = 100.0 * d[slot] * PIT_PER_CPU / el
        ui = 100.0 * d[0] * PIT_PER_CPU / el
        check(idle >= IDLE_MIN,
              "a bare desktop is HALTED in the idle task (%.1f%%, ui_task "
              "%.1f%%)" % (idle, ui),
              "the idle task is not being picked, or something else is "
              "runnable and spinning. A package worker that yield-spins will "
              "do this and it is not a kernel bug - check what is open",
              got="idle %.1f%%, ui_task %.1f%%" % (idle, ui),
              want=">= %.0f%% idle" % IDLE_MIN)

        bal = sum(d) * float(PIT_PER_CPU) / el
        check(0.95 <= bal <= 1.02,
              "the books still balance (%.1f%% of elapsed)" % (bal * 100),
              "blocking must not lose time: an interval that spans a hlt is "
              "still an interval, and SPEC.md 8.1.1's change-compare bills it "
              "to whoever was running",
              got="%.1f%%" % (bal * 100), want="95..102%")

        lat = wake_latency(m)
        if check(len(lat) >= 12, "the pointer sweep produced samples (%d)"
                                 % len(lat),
                 "cur_move never fired, so the mouse ISR is not drawing the "
                 "pointer and this test is measuring nothing"):
            check(lat[-1] <= LAT_MAX,
                  "NO LOST WAKEUP: worst wake %.2f ms (median %.2f, n=%d)"
                  % (lat[-1], lat[len(lat) // 2], len(lat)),
                  "SPEC.md 8.1.2.3. A wake posted during a pass that the pass "
                  "then sleeps through waits for the TICK, and a tick is "
                  "54.9 ms - which is the visible hitch. It is a TAIL and a "
                  "median cannot see it: before the guard existed the median "
                  "was 5.20 ms and one sample in 14 was 54.15",
                  got="max %.2f ms" % lat[-1],
                  want="<= %.1f ms (a tick is 54.9)" % LAT_MAX)

    report()


main(sys.argv[1:])

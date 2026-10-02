#!/usr/bin/env python3
"""SPEC.md 14.7: the Timer has NO TASK, and still keeps time and still draws.

    python3 tests/tmrnotask.py [machine]

The Timer ran a task in a 128-byte slice (SCH_BUILTIN_STK) that was sized from
Bounce and never measured. Its digit line takes font_run's per-cell path the
moment a window cuts it (SPEC.md 14.4), 90 bytes down, and an IRQ0 on top of
that read 112 of 128 on this container's floor of 32 - so 132 on vm/pc5150's
52, which is the STACK OVERFLOW TASK 02 Timer the field photographed. 14.7
moved the clock onto the window's one-shot timer (SPEC.md 13.9), which runs on
the UI task's own stack, so the Timer holds no slice at all.

Three legs, each the thing the change could have broken:

  A  opening a Timer spawns NO task - the task table is read, not assumed.
     A kind row that names a task again is what this catches.
  B  the clock KEEPS TIME with no task: TMR_SEC over a stretch of guest time
     agrees with the guest's own cycle counter to a second. A W_ONTIMER that
     is installed but never re-armed fires once and stops, and this is that.
  C  SPEC.md 14.4's geometry still draws: another window's top edge 3 rows
     into the digit line, and the three rows above the edge CHANGE as the
     seconds move while the five below it - the covering window - do NOT.
     The first is the redraw surviving the move to the UI task; the second is
     the clip (armed by the handler itself, 13.9's environment disarms it).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402
import os88ui                                               # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla"
E = os88sym.equates(())
T_SIZE, MAX_TASKS = E["T_SIZE"], E["MAX_TASKS"]
TMR_SEC, TMR_RUN = E["TMR_SEC"], E["TMR_RUN"]
TITLE_H, DY, DX = E["TITLE_H"], E["APP_TMR_DY"], E["APP_TMR_DX"]
CUT = 3                                     # rows of the line left showing

fails = []


def say(*a):
    print(*a, flush=True)


def check(name, cond, note=""):
    say("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, note))
    if not cond:
        fails.append("%s %s" % (name, note))


def tasks(m):
    t = m.read(os88sym.linear("sch_tasks"), T_SIZE * MAX_TASKS)
    return [i for i in range(MAX_TASKS) if t[i * T_SIZE]]


def tstate(m):
    """The first pool slot is the one a lone Timer takes."""
    return m.read(os88sym.linear("app_tmr_pool"), 4)


def wait(m, cond, what, guest):
    """until(), with a miss reported as a FAIL rather than raised: a Timer
    whose clock never fires is exactly what B exists to catch, and a traceback
    would name the harness instead of the kernel."""
    try:
        os88marty.until(m, cond, what, poll=0.1, guest=guest)
        return True
    except os88marty.MartyError as e:
        fails.append("waited %.0f guest seconds for %s: %s" % (guest, what, e))
        say("  [FAIL] waited for %s - %s" % (what, str(e)[:120]))
        return False


def secs(m):
    h, mi, s = tstate(m)[:3]
    return h * 3600 + mi * 60 + s


def line(m, t):
    """The digit line's 8 rows, cells 0..7, as a tuple per row."""
    _, _, rows = m.vram()
    x0, y0 = t.x + 1 + DX, t.y + TITLE_H + DY
    return [tuple(rows[y][x0:x0 + 64]) for y in range(y0, y0 + 8)]


with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                 machine=MACHINE) as ui:
    m = ui.m
    say("== %s : the Timer has no task (SPEC.md 14.7) ==" % MACHINE)
    if m.video()["type"] not in ("cga", "mda", "herc"):
        sys.exit("tmrnotask: leg C reads a 1bpp framebuffer; %s is %s"
                 % (MACHINE, m.video()["type"]))

    # --- A ------------------------------------------------------------------
    t0 = tasks(m)
    ui.menu_pick("Builtins", "Timer")
    t = ui.wait_window("Timer")
    t1 = tasks(m)
    check("A  opening a Timer spawns no task", t1 == t0,
          "(task slots %s before, %s after)" % (t0, t1))
    check("A  ...and it is running", tstate(m)[TMR_RUN] == 1,
          "(TMR_RUN = %d)" % tstate(m)[TMR_RUN])

    # --- B ------------------------------------------------------------------
    wait(m, lambda mm: secs(mm) >= 1, "the Timer's first second", 10.0)
    s0, c0 = secs(m), m.status()["cycles"]
    os88marty.guest_sleep(m, 12.0)
    s1, c1 = secs(m), m.status()["cycles"]
    guest = (c1 - c0) / 4772727.0
    check("B  the clock keeps time with no task", abs((s1 - s0) - guest) <= 1.5,
          "(%d seconds counted over %.2f guest seconds)" % (s1 - s0, guest))

    # --- C ------------------------------------------------------------------
    ui.open_drive("A")
    d = ui.disk_window()
    ui.move_window(d, t.x - 40, t.y + TITLE_H + DY + CUT)
    t = ui.window("Timer")
    check("C  the Disk window is in front and cuts the digit line",
          ui.front() is not None and ui.front().title.startswith("Disk")
          and ui.disk_window().y == t.y + TITLE_H + DY + CUT,
          "(front %s, timer %s)" % (ui.front(), t))
    os88marty.guest_sleep(m, 1.0)
    b0, s0 = line(m, t), secs(m)
    wait(m, lambda mm: secs(mm) >= s0 + 2, "two more seconds", 10.0)
    os88marty.guest_sleep(m, 0.8)            # the firing that drew them
    b1 = line(m, t)
    above = sum(1 for r in range(CUT) for i in range(64) if b0[r][i] != b1[r][i])
    below = sum(1 for r in range(CUT, 8) for i in range(64)
                if b0[r][i] != b1[r][i])
    check("C  the rows above the edge follow the clock", above > 0,
          "(%d pixels changed in the %d visible rows, seconds %d -> %d)"
          % (above, CUT, s0, secs(m)))
    check("C  ...and the covered rows are not drawn on", below == 0,
          "(%d pixels changed under the Disk window)" % below)

say()
for f in fails:
    say("  FAIL: " + f)
say("tmrnotask: %s" % ("FAILED" if fails else "ok"))
sys.exit(1 if fails else 0)

#!/usr/bin/env python3
"""A tick inside the ROM's int 09h must not split a key from its modifier
(SPEC.md 9.8.3).

    make marty && make && python3 tests/kbdpreempt.py

`soak -k pxdecode` failed twice the same way: PiXEL was sent Ctrl+R, never
opened anything, and the machine sat idle with the BIOS ring empty. The key
had arrived as a PLAIN 'r' (the Rotate tool's letter), with Ctrl left stuck
on in 0040:0017. GLaBIOS's int 09h sends its EOI and `sti`s in its first ten
instructions and only then reads KB_FLAG. A tick landing there ran inside
the handler, either ~3,500 cycles of sch_isr or a task switch that parked it.
The keyboard's next byte comes a millisecond after the acknowledge
regardless, so R's handler nested, read KB_FLAG with Ctrl not yet set, and
stored 1372h.

That needs a tick within a few dozen cycles of one point in a key's
handler, so it shows up once in thousands of keystrokes and waiting for it
is hopeless. This row PUTS the tick there instead. Each trial stops at
sch_isr, advances to `d` cycles before the tick after next, queues Ctrl
down, R, Ctrl up, and reads the ring slot the ROM writes. `d` sweeps
past the handler's whole length.

TWO LEGS:

  A. the sweep. Every trial stores Ctrl+R (1312h) and leaves Ctrl released.
     The control: the tick must land inside the keyboard path on enough
     trials (it is late there - kbm_isr has IF = 0, and with the fix the ROM
     runs with IRQ0 masked), and never by more than a handler's length, so
     an empty sweep or a mask that is never lifted is red too.
  B. Pause still works. GLaBIOS's Ctrl+NumLock waits INSIDE int 09h for
     the next key to nest a second handler, so IRQ1 must stay open through
     the mask, or the machine stops right there. One key ends the Pause, the
     tick runs again, and Ctrl+R is still Ctrl+R. (The BIOS tick is printed
     and not asserted. It stands still while paused, because IRQ0 is masked
     there, and SPEC.md 9.8.3 says why that is the pause.)

RED, both legs, measured:
  A. On the tree before SPEC.md 9.8.3 (236d0af0), the first trial at a
     532-cycle lead stored a plain 'r' (1372h) with Ctrl stuck. Every trial
     after it read KB_FLAG 04h, so only 90 of 261 passed.
  B. Masking IRQ1 with the rest (`or al, 0FFh`) never ends the Pause.
Holding only the switch, under [sch_lock] (the first attempt), was WORSE:
46 of 80 trials failed, because the nested tick still stretched the handler
past the millisecond.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty as M                                          # noqa: E402
import os88ui                                                  # noqa: E402

SYS = os.environ.get("OS88_SYSIMG", "build/os8088.img")
APPS = os.environ.get("OS88_APPSIMG", "build/apps.img")
MACHINE = "os8088_xt_vga_144"           # GLaBIOS: the ROM that EOIs early
TICK = 65536 * 4                        # CPU cycles a tick: the kernel keeps
                                        # channel 0 at the BIOS's divisor here
SWEEP = range(400, 2400, 7)             # the measured windows were 535-611
                                        # and 1781: past both, at a step
                                        # finer than either is wide
HANDLER = 6000                          # cycles: no tick is held longer than
                                        # one keyboard handler (~2,500)
CTRL_R = 0x1312

fails = []


def check(leg, ok, msg):
    print("  %s %s: %s" % ("ok  " if ok else "FAIL", leg, msg), flush=True)
    if not ok:
        fails.append(leg)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


with os88ui.boot(SYS, apps=APPS, machine=MACHINE) as ui:
    m = ui.m
    isr = m.sym("sch_isr")

    def to_tick():
        """Stop at the next tick; answer its cycle."""
        m.bp_exec(isr)
        mark = m.go()
        if m.wait_stop(guest=5.0, poll=0.002, since=mark) != "breakpoint":
            sys.exit("kbdpreempt: no tick reached sch_isr in 5 guest seconds")
        return m.status()["cycles"]

    def drained():
        """ui_task has the key out of the ring: the trial is over."""
        return u16(m.read(0x41A, 2)) == u16(m.read(0x41C, 2))

    def ctrl_r(mark=True):
        tail = u16(m.read(0x41C, 2))
        if mark:
            m.write(0x400 + tail, b"\0\0")      # the slot this key lands in
        m.key("ControlLeft", down=True, up=False)
        m.key("KeyR")
        m.key("ControlLeft", down=False, up=True)
        return tail

    print("== kbdpreempt on %s: %d trials ==" % (MACHINE, len(SWEEP)))
    m.pause()
    bad, late, leads = [], [], []
    for d in SWEEP:
        c0 = to_tick()
        m.bp_exec()
        m.advance(cycles=2 * TICK - d)
        due = c0 + 2 * TICK
        # `advance` stops on an instruction, a few hundred cycles past what
        # it was asked for out of a breakpoint: the LEAD is what it gave
        lead = due - m.status()["cycles"]
        if lead < 64:
            continue                    # the tick is already behind us
        leads.append(lead)
        tail = ctrl_r()
        c1 = to_tick()                  # the tick the keystroke straddles
        late.append(c1 - due)
        m.bp_exec()
        m.advance(cycles=3 * TICK)      # four bytes a millisecond apart
        got = u16(m.read(0x400 + tail, 2))
        flag = m.read(0x417, 1)[0]
        if got != CTRL_R or flag & 0x04:
            bad.append((lead, got, flag))
        m.run()
        M.until(m, lambda mm: drained(), "the key to be taken", poll=0.005,
                guest=10.0)
        m.pause()
    check("A", not bad and len(leads) > len(SWEEP) // 2,
          "%d of %d trials (leads %d-%d cycles) stored Ctrl+R with Ctrl "
          "released%s" % (len(leads) - len(bad), len(leads), min(leads),
                          max(leads),
                          "; NOT at " + ", ".join("lead %d %04X KB_FLAG %02X"
                                                  % b for b in bad[:6])
                          if bad else ""))
    held = [x for x in late if x > 0]
    check("A ctl", len(held) >= 10 and max(late) < HANDLER,
          "the tick landed inside the keyboard path on %d trials, held at "
          "most %d cycles (want >= 10, and < %d)"
          % (len(held), max(late), HANDLER))

    # --- B: Pause, which waits inside int 09h for a nested key ------------
    m.bp_exec()
    m.run()
    m.key("ControlLeft", down=True, up=False)
    m.key("NumLock")
    m.key("ControlLeft", down=False, up=True)
    M.until(m, lambda mm: mm.read(0x418, 1)[0] & 0x08, "the ROM's Pause",
            poll=0.01, guest=5.0)
    t0 = u16(m.read(0x46C, 2))
    M.pace(m, 1.0)
    t1 = u16(m.read(0x46C, 2))
    m.key("KeyA")                        # any key ends a Pause (and is spent)
    M.until(m, lambda mm: not mm.read(0x418, 1)[0] & 0x08,
            "the Pause to end on a key", poll=0.01, guest=5.0)
    t2 = u16(m.read(0x46C, 2))
    M.until(m, lambda mm: u16(mm.read(0x46C, 2)) != t2, "the tick to run "
            "again", poll=0.01, guest=5.0)
    M.until(m, lambda mm: drained(), "the ring to drain", poll=0.01,
            guest=10.0)
    tail = ctrl_r()
    M.until(m, lambda mm: drained() and mm.read(0x400 + tail, 2) != b"\0\0",
            "Ctrl+R after the Pause", poll=0.01, guest=10.0)
    got = u16(m.read(0x400 + tail, 2))
    check("B", got == CTRL_R,
          "paused (BIOS tick %d -> %d over a guest second), ended by a key, "
          "ticking again; Ctrl+R after it stored %04X" % (t0, t1, got))

print("kbdpreempt: %s" % ("ok" if not fails else "FAILED " + " ".join(fails)))
sys.exit(1 if fails else 0)

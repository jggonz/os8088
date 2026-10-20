#!/usr/bin/env python3
"""A scripted key BURST must not nest int 09h (SPEC.md 9.8.2).

    make marty && make && python3 tests/kbdburst.py

**What this row guards is the HARNESS, not the kernel**, and that is the
whole finding behind it. An agent sent 70 Backspaces into Font Viewer with
`m.key()` and no advance between them, and the machine stopped in
`sch_stkdie` with the UI task's stack - and ~1KB of .lowbss beneath it -
holding one 22-byte GLaBIOS int 09h frame after another, every one
interrupted a few instructions after the ROM's `sti`. MartyPC took one queued
key event per `machine.run()` call and handed each byte to the PPI the moment
the last was acknowledged; `advance` calls `run()` every quarter of a
millisecond, so a scripted burst arrived at a rate no keyboard's serial line
can carry, and every byte interrupted the handler that had just acknowledged
the one before it. tools/martypc/patches/14-keyboard-byte-pacing.patch paces
both to one byte a millisecond, which is the physical floor; SPEC.md 9.8.2 is
why real hardware cannot get there and why the kernel needs nothing.

The row exists so that losing that patch - a MartyPC pin bump, a patch that
stops applying and is dropped - is a red row naming itself rather than a
phantom stack overflow somebody spends a day on.

THREE LEGS:

  A. a 10-key burst deletes EXACTLY ten characters - the positive control:
     pacing that DROPPED keys would pass leg B by never delivering them.
  B. the 70-key burst on Inconsol (the original repro) does not reach
     sch_stkdie, and the BIOS buffer drains.
  C. the machine still answers: one more Backspace, at a human pace, deletes
     one more character.

Leg B's character count is NOT asserted: Inconsol redraws slower than 70 keys
arrive, the 15-entry BIOS buffer fills, and kbd_ovflow (SPEC.md 9.8) drops
the newest input by design. How many survive depends on the redraw, not on
anything this row is about.

Red against an unpatched MartyPC: rebuild it without patch 14 (`git -C
build/martypc/src apply -R tools/martypc/patches/14-*.patch` and `cargo build
-p martypc_headless --release` there) and leg B stops in sch_stkdie.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import dispapps                                                # noqa: E402
import os88ui                                                  # noqa: E402

# THE TYPEFACES ARE ON office360.img (SPEC.md 24.3.1.5): the 360KB system
# disk gave SYSTEM/FONTS and the Font Viewer to it, so B: is that disk and
# the repro's folder is B:/SYSTEM/FONTS, where it was A:'s
SYS = os.environ.get("OS88_SYSIMG", "build/os8088-360.img")
APPS = os.environ.get("OS88_APPSIMG", "build/office360.img")
MACHINE = "os8088_xt_vga"
FRAMES = 70                     # ~1 s of guest at the VGA's 70 Hz

fails = []


def check(leg, ok, msg):
    print("  %s %s: %s" % ("ok  " if ok else "FAIL", leg, msg), flush=True)
    if not ok:
        fails.append(leg)


with os88ui.boot(SYS, apps=APPS, machine=MACHINE) as ui:
    m = ui.m
    ui.path("B:/SYSTEM/FONTS")
    ui.open("CHARTER.F88")              # the open-by-association path the
    m.advance(frames=FRAMES * 10)       # report took; Inconsol is three down
    m.run()
    for _ in range(3):
        m.key("ArrowDown")
        m.advance(frames=FRAMES * 6)
        m.run()

    found = dispapps.pkg_seg(m, -1)
    if found is None:
        sys.exit("kbdburst: no package window - Font Viewer did not open")
    seg = found[1]
    off = dispapps.sym("fontview", "fv_textlen")

    def textlen():
        return m.readseg(seg, off, 1)[0]

    die = m.sym("sch_stkdie")

    def dead():
        r = m.regs()
        return ((r["cs"] << 4) + r["ip"]) & 0xFFFFF == die

    def burst(n, frames):
        m.bp_exec("sch_stkdie")
        for _ in range(n):
            m.key("Backspace")
        m.advance(frames=frames)
        stopped = dead()
        m.bp_exec()
        if not stopped:
            m.run()
        return stopped

    n0 = textlen()
    print("== kbdburst on %s: specimen %d characters ==" % (MACHINE, n0))
    if n0 < 10 + 15 + 1:
        sys.exit("kbdburst: specimen too short (%d) for the legs" % n0)

    died = burst(10, FRAMES * 3)
    n1 = textlen()
    check("A", not died and n0 - n1 == 10,
          "10-key burst deleted %d (want 10)%s"
          % (n0 - n1, ", and DIED in sch_stkdie" if died else ""))

    died = burst(70, FRAMES * 5)
    head, tail = (int.from_bytes(m.read(0x41A + i, 2), "little") for i in (0, 2))
    n2 = textlen()
    check("B", not died and head == tail,
          "70-key burst %s; BIOS buffer head %04x tail %04x; deleted %d"
          % ("STOPPED IN sch_stkdie - keys reached the PPI faster than a "
             "keyboard can send them (patch 14?)" if died else "survived",
             head, tail, n1 - n2))

    if not died:
        m.key("Backspace")
        m.advance(frames=FRAMES)
        m.run()
        n3 = textlen()
        check("C", n2 - n3 == 1 or (n2 == 0 and n3 == 0),
              "one paced Backspace deleted %d (want 1)" % (n2 - n3))
    else:
        check("C", False, "not reached - the machine is dead")

print("kbdburst: %s" % ("FAIL " + " ".join(fails) if fails else "ok"))
sys.exit(1 if fails else 0)

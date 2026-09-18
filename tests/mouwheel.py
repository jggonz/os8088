#!/usr/bin/env python3
"""SPEC.md 9.5.4: a WHEEL mouse's FOURTH byte must not break the packet run.

    make && python3 tests/mouwheel.py

A Microsoft packet is three bytes - a header with bit 6 SET, then two with bit
6 CLEAR - and `mou_byte`'s phase machine decoded exactly that.  An
IntelliMouse-compatible wheel mouse sends FOUR, the last carrying the wheel
delta and the middle button, and it has bit 6 CLEAR like the two before it.

So it arrived back at phase 0, fell through `.chk2` to *"a byte with bit 6
clear arriving between packets is a thing the protocol cannot produce"*, and
ZEROED `[mou_run]`.  Every packet.  The run could never exceed 1, and on a
two-port machine - where `[mou_need]` is `MOU_LOCKN` = 8 - the contest was
UNWINNABLE BY CONSTRUCTION: the mouse streamed perfectly and the kernel threw
the evidence away as fast as it arrived (docs/FIELD-NOTES.md 44).

NOTHING ELSE IN THIS TREE CAN SEE THAT.  Every emulated mouse here is a
three-byte part and the field 5150's is a real Microsoft three-button, so the
defect needs a wheel mouse, a two-port machine and a cold boot at the same
time - and it is invisible the moment anything else has settled the port,
because `mou_claim` then returns at its first compare and the run is never
read again.  That is exactly why the reporter's own workaround (plug a plain
mouse in first, hot-swap the wheel one) made it work perfectly.

THE BYTES ARE THE REPORTER'S OWN, read off `MOUROUND=1`'s panel: `43 3F 00 00`
in arrival order - header (dx = -1, dy = 0, buttons up), X low, Y low, and
then the wheel byte.

THE INSTRUMENT IS INJECTION, not a mouse.  MartyPC has no wheel mouse to
attach, so each byte is handed to the REAL `mou_byte` in the guest: park the
CPU at it with AL = the byte and BX = the port row, with a return address
pushed at a breakpoint, and run.  That tests the shipped kernel's own decode
rather than a model of it, which matters here because the defect was one
compare deep in a phase machine.

FOUR READINGS, and the last two are what stop the first two passing vacuously:

  1. **Four-byte packets accumulate a run.**  Five wheel packets must take
     `[mou_run]` to 5.  VERIFIED TO FAIL: the kernel before SPEC.md 9.5.4
     reads **0** here, not 1 - the fourth byte zeroes the very increment its
     own packet just made.
  2. **...and they SETTLE the port.**  `[mou_need]` at `MOU_LOCKN`, eight
     packets, and `[mou_seen]` must go 1.  This is the user-visible claim and
     the one the reporter's machine failed; reading 1 alone would not say the
     contest completes.
  3. **A plain three-byte mouse is unchanged.**  Five must still read 5.  The
     regression control: phase 3 is entered by every mouse now, so a fix that
     broke the three-byte path would be a far worse bug than the one it cures.
  4. **A genuine stray byte still breaks the run.**  A fifth bit-6-clear byte
     after the wheel byte must zero it.  The negative control, and the rule
     9.5.4 relaxes: it admits exactly ONE such byte per packet and no more, so
     a kernel that simply deleted the run-break would pass 1-3 and fail here.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))

import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

IMG = "build/os8088-360.img"
APPS = "build/apps360.img"
MACHINE = "os8088_5150_cga_gla"
ROW = 2                                                     # COM2, the reporter's port

P4 = [0x43, 0x3F, 0x00, 0x00]       # the reporter's own packet, plus its wheel byte
P3 = [0x43, 0x3F, 0x00]             # ...and a plain three-byte mouse's
STRAY = P4 + [0x11]                 # ...and a SECOND bit-6-clear byte after it


def say(s):
    print(s, flush=True)


def main(argv):
    sym = os88sym.syms()
    kseg = os88marty.KERNEL_SEG
    k = kseg * 16
    fail = []
    m = os88marty.launch(IMG, apps=APPS, machine=MACHINE, boot=False)
    try:
        m.go()
        os88marty.guest_sleep(m, 25)                        # a desktop, and the
        m.pause()                                           # identify window over
        regs = m.regs()
        ss, sp0 = regs["ss"], regs["sp"]
        ret = sym["mou_eoi"]                                # any address to break on
        m.bp_exec(k + ret)

        def poke(name, val, off=0):
            m.write(k + sym[name] + off, bytes([val]))

        def peek(name, off=0):
            return m.read(k + sym[name] + off, 1)[0]

        def feed(seq):
            """hand each byte to the guest's own mou_byte"""
            for b in seq:
                sp = (sp0 - 8) & 0xFFFF                     # clear of anything live
                m.write(ss * 16 + sp, struct.pack("<H", ret))
                m.cmd(cmd="park", cs=kseg, ip=sym["mou_byte"])
                for reg, v in (("ax", b), ("bx", ROW), ("ds", kseg),
                               ("es", kseg), ("ss", ss), ("sp", sp)):
                    m.setreg(reg, v)
                m.go()
                if not m.wait_stop(limit=10):
                    raise SystemExit("mouwheel: guest never returned from mou_byte")

        def arm(need):
            poke("mou_drain", 0)
            poke("mou_seen", 0)
            poke("mou_ptr", 0)
            poke("mou_need", need, ROW)
            poke("mou_run", 0, ROW)
            poke("mou_phase", 0, ROW)

        # 1. four-byte packets accumulate a run
        arm(99)                                             # never settle: watch the RUN
        feed(P4 * 5)
        run4 = peek("mou_run", ROW)
        if run4 != 5:
            fail.append("4-byte wheel x5 -> mou_run %d, want 5 "
                        "(the pre-9.5.4 kernel reads 0)" % run4)

        # 2. ...and they settle the port at MOU_LOCKN
        lockn = 8
        arm(lockn)
        feed(P4 * lockn)
        seen, ptr = peek("mou_seen"), peek("mou_ptr")
        if seen != 1 or ptr != 1:
            fail.append("4-byte wheel x%d -> seen %d ptr %d, want 1 1 "
                        "(the contest never completes)" % (lockn, seen, ptr))

        # 3. a plain three-byte mouse is unchanged
        arm(99)
        feed(P3 * 5)
        run3 = peek("mou_run", ROW)
        if run3 != 5:
            fail.append("3-byte plain x5 -> mou_run %d, want 5 "
                        "(REGRESSION: phase 3 broke the ordinary path)" % run3)

        # 4. a genuine stray byte still breaks the run
        arm(99)
        feed(STRAY)
        runs = peek("mou_run", ROW)
        if runs != 0:
            fail.append("stray byte after the wheel byte -> mou_run %d, want 0 "
                        "(9.5.4 must admit exactly ONE per packet)" % runs)
    finally:
        m.close()

    say("")
    if fail:
        say("mouwheel: %d FAILED" % len(fail))
        for f in fail:
            say("  FAIL: %s" % f)
        return 1
    say("mouwheel: wheel run %d, settled seen/ptr, plain run %d, stray run %d"
        % (run4, run3, runs))
    say("mouwheel: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

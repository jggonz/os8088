#!/usr/bin/env python3
"""The boot ZEROES `[ticks]` - it does not inherit what was in RAM.

    make && python3 tests/tickzero.py

`ticks` is `.bss`, and nothing clears `.bss`: `sched_init`'s
`mov word [ticks], 0` is the whole of what makes the count start at 0, and
`sched_init` seeds `[sch_pit_last]` on exactly that assumption (high word =
[ticks] = 0). Kernel size pass 5 folded that one instruction into the end of
the comment above it, and no row noticed, because every emulator here powers
on with ZEROED RAM - the defect needs a machine whose memory is not.

So this row MAKES the RAM dirty where it matters. It stops the boot on
`sched_init`'s entry, writes 0x8000 into `[ticks]`, lets the boot finish and
reads the count at the desktop: a boot is a few hundred ticks, so a healthy
kernel reads well under 0x8000 and one that skipped the store reads 0x8000
plus the boot. 0x8000 rather than 0xFFxx because the latter WRAPS to a small
number inside the boot and would pass the broken kernel.

Broken on purpose (the store turned back into a comment) it FAILS naming the
count it read.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

MACHINE = "os8088_5150_cga_gla"
IMG = os.path.join(ROOT, "build", "os8088-360.img")
APPS = os.path.join(ROOT, "build", "apps360.img")
JUNK = 0x8000


def main():
    S = os88sym.linear
    lin_ticks = S("ticks")
    with os88marty.launch(IMG, apps=APPS, machine=MACHINE, boot=False) as m:
        m.breakpoints([{"type": "exec", "addr": S("sched_init")}])
        m.run()
        if m.wait_stop(guest=120.0) != "breakpoint":
            raise SystemExit("tickzero: the boot never reached sched_init")
        m.write(lin_ticks, JUNK.to_bytes(2, "little"))
        m.breakpoints([])
        m.run()

        def booted(_):
            return m.read(S("desk_rows"), 2) != b"\0\0" and \
                m.read(S("spl_live"), 1)[0] == 0
        os88marty.until(m, booted, "the boot to finish", poll=0.05,
                        guest=300.0)
        t = int.from_bytes(m.read(lin_ticks, 2), "little")
    print("  [ticks] = %d at the desktop, after %d was planted at sched_init"
          % (t, JUNK))
    if t >= JUNK:
        print("FAIL: [ticks] kept the %d planted before sched_init - the "
              "boot does not zero it (sched_init's `mov word [ticks], 0`)"
              % JUNK)
        return 1
    print("tickzero: the boot zeroes [ticks]")
    return 0


if __name__ == "__main__":
    sys.exit(main())

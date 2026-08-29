#!/usr/bin/env python3
"""The kernel load actually CROSSES A HEAD, and the canary says so (SPEC.md 18.93.3).

    make && python3 tests/cylrun.py

SPEC.md 18.91.1's whole win is that one `int 13h` may carry a CYLINDER rather
than a track, and nothing in this tree ever asserted that it did. Three separate
things have to hold for it, and each of them fails SILENTLY - the boot still
reaches a desktop, because 18.93's shorten-and-reload is designed to rescue
exactly this:

 1. stage 2 computes the cylinder bound correctly (SPT * HEADS),
 2. 18.93.2's `push sp` gate widens `[b2_runmax]` to it on an 8088, and
 3. 18.93.1's canary compares the KSIG probe EQUAL after the load - so every
    run that flipped head at EOT came back in LBA order.

`boot_cylrun`, at the fixed word 0060:0004, is written on the one path where
all three held, and is zero on every other. So the whole assertion is `!= 0`.

WHAT IT CAUGHT. 18.93.3: the run bound was multiplied out of `CX` and `DH`, and
SPEC.md 2.9.8 moved `int 10h AH=01h` - which takes the cursor shape in CX - to
three instructions above it. `[b2_runmax]` came out 0x2000, `read_run`'s second
bound won every time, the first call of the boot asked for 125 sectors across a
cylinder, the FDC refused it, and the reload quietly loaded a correct kernel at
the track bound. The desktop appeared. `make test-full` passed. What was lost
was 18.91.1 ENTIRELY, on every machine, for a cycle - and the only outward sign
was three failed reads at the top of a boot that already sounds like a floppy.

It runs on MartyPC because the gate at (2) is a real 8088's `push sp` and this
is the only 8088 here. The Hercules twin is arbitrary - nothing below looks at
the screen - and it is the cheapest machine to boot.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

MACHINE = "os8088_5150_herc_gla"
IMG = os.path.join(ROOT, "build", "os8088-360.img")
CYLRUN_AT = 0x0004          # SPEC.md 18.93.1's word, KERNEL_SEG-relative
SPT = 9                     # what build/os8088-360.img is
HEADS = 2


def main():
    lin_live = os88sym.linear("spl_live")
    lin_entry = os88sym.linear("cold_entry")

    with os88marty.launch(IMG, apps=os.path.join(ROOT, "build", "apps360.img"),
                          machine=MACHINE, boot=0) as m:
        started = False
        for _ in range(6000):
            m.advance(frames=1)
            live = m.read(lin_live, 1)[0]
            if not started:
                if live != 1 or m.read(lin_entry, 1)[0] != 0xE9:
                    continue
                started = True
            elif live == 0:
                break
        else:
            raise SystemExit("cylrun: this machine never finished booting, so "
                             "nothing below would mean what it says")
        cylrun = int.from_bytes(m.readseg(0x0060, CYLRUN_AT, 2), "little")

    print("  boot_cylrun[0060:%04X] = %d" % (CYLRUN_AT, cylrun))
    if cylrun == 0:
        raise SystemExit(
            "cylrun: FAIL - boot_cylrun is 0, so no run of the kernel load was "
            "ever proved to have crossed a head. The boot still worked: "
            "SPEC.md 18.93's shorten-and-reload is what makes this quiet. One "
            "of three things did not hold - stage 2's cylinder bound "
            "(18.93.3), the `push sp` gate that widens it (18.93.2), or the "
            "canary's own compare (18.93.1) - and 18.91.1's 2.2s is being paid "
            "for and not collected on every machine.")
    if cylrun != SPT * HEADS:
        raise SystemExit(
            "cylrun: FAIL - boot_cylrun is %d and this disk's cylinder is %d "
            "sectors (%d x %d). The canary passed, so the load is CORRECT; the "
            "bound it proved is not the one 18.91.1 asks for."
            % (cylrun, SPT * HEADS, SPT, HEADS))
    print("  ok  the load crossed a head %d sectors at a time and the KSIG "
          "probe compared equal" % cylrun)


if __name__ == "__main__":
    main()

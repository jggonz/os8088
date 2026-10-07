#!/usr/bin/env python3
"""A stopped floppy motor puts the widget up BEFORE the spin-up (SPEC.md
12.8.3.1).

    make && python3 tests/fpgcold.py

Reported off an 86Box 286: double-click a drive and for about a second
NOTHING happens - no busy pointer, no progress widget, no drive light - and
then all three at once.  That second is the BIOS spinning the motor up inside
the first int 13h (an AT ROM waits the parameter table's byte 10 for it), and
the first transfer of a mount is the one-sector boot read, which FPG_WARM's
sector count let through without a word.

MartyPC's ROMs do not wait for a spin-up on a read, so the second itself is
not reproducible here - what is, exactly, is the ORDER: with every motor
stopped, `fpg_arm` and `cur_busy_on` must come before the first int 13h of
the mount.  On the kernel before 12.8.3.1 they come after it (193 ms into the
open on this machine; after the whole spin-up on the 286).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M                                       # noqa: E402
import os88ui                                               # noqa: E402


def main():
    m = M.launch(os.path.join(ROOT, "build/os8088-360.img"),
                 apps=os.path.join(ROOT, "build/media360.img"),
                 machine=M.machine("os8088_5150_cga"))
    try:
        ui = os88ui.UI(m, verbose=False)
        ui.ready(limit=240)
        M.guest_sleep(m, 4.0)          # past the 2 s motor-off delay
        bits = m.read(0x43F, 1)[0]
        if bits & 0x0F:
            print("fpgcold: FAIL: a motor is still on (0040:003F = %02X), so "
                  "the case under test never arose" % bits)
            return 1
        with M.bp_trace(m, "fpg_arm", {"type": "int", "addr": 0x13}) as tr:
            ui.open_drive("B")
        order = ["arm" if h["name"] == "fpg_arm" else "int13" for h in tr.hits]
        print("fpgcold: first events of the open: %s" % " ".join(order[:4]))
        if not order or order[0] != "arm":
            print("fpgcold: FAIL: the widget armed after the first int 13h, "
                  "so the spin-up inside it is a second with nothing on the "
                  "screen (SPEC.md 12.8.3.1)")
            return 1
        print("fpgcold: ok")
        return 0
    finally:
        m.quit()


if __name__ == "__main__":
    sys.exit(main())

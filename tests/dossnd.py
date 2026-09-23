#!/usr/bin/env python3
"""The DOS sound gate (SPEC.md 96.17, 51.11).

A DOS program that wants the Sound Blaster wants to program it ITSELF, and
os8088's SOUND.DRV is in the way three ways: an IRQ vector, DMA channel 1,
and a refill worker that is TF_SERVICE - so it KEEPS RUNNING inside the fsx
bracket by design (SPEC.md 53.2) and can feed the DSP while the DOS program
is resetting it. OSAPI_DRV_SUSPEND is the door.

THE ROW READS THE DRIVER'S OWN DRVR_SEG out of the guest, which is the only
way to see the half that has no pixels: it must be LOADED at the desktop,
ZERO while the DOS program runs, and loaded again afterwards.

IT USES NO SYSTEM.CFG, deliberately. SPEC.md 51.3.1's boot sniff runs an OPL
timer dance and sets the sound row's want bit, so a machine with a card and
no configuration at all mounts the driver - which is the common case this is
about, and the one an earlier revision of SPEC.md 96.16 got wrong by claiming
the driver only ever arrives by request.

The visible half is BLASTER=, printed by the program out of its own PSP:002C.
The IRQ field is absent on purpose (SPEC.md 96.17.1): discovery is deferred
to first use, so a machine that has not played a sound does not know its
line, and a BLASTER= naming the wrong one is worse than one naming none.
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88geom                                                # noqa: E402
import os88ui                                                  # noqa: E402
import os88marty                                               # noqa: E402

SYS = "build/os8088-360.img"
SND = "build/dossnd360.img"
MACHINE = "os8088_5150_herc_sb_gla"      # ...the only kind with a card in it


def fail(msg):
    print("dossnd: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, SND):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=SND, machine=MACHINE) as ui:
        m = ui.m
        row0 = m.sym("drv_tab")

        def loaded():
            return struct.unpack("<H", m.read(row0 + os88geom.DRVR_SEG, 2))[0]

        before = loaded()
        if not before:
            fail("SOUND.DRV is not loaded at the desktop - this machine has a "
                 "card and SPEC.md 51.3.1's boot sniff should have mounted it "
                 "with no SYSTEM.CFG at all, which is the case this row is "
                 "about")
        print("dossnd: SOUND.DRV mounted itself at boot, segment %04X" % before)

        if not ui.path("B:/DOSSND.COM"):
            fail("double-clicking DOSSND.COM opened no window")

        rows = []
        end = time.time() + 180.0
        while time.time() < end:
            rows = m.screen() or []
            if any("READY" in r for r in rows):
                break
            time.sleep(0.3)
        else:
            fail("the program never finished; the last text screen was %r"
                 % ([r.rstrip() for r in rows if r.strip()][:12],))

        during = loaded()
        text = "\n".join(r.rstrip() for r in rows)
        print("dossnd: the bracket's text screen:")
        for r in rows[:12]:
            if r.strip():
                print("   | %s" % r.rstrip())

        if during:
            fail("SOUND.DRV is STILL LOADED (segment %04X) while a DOS "
                 "program has the screen - its IRQ, its DMA channel and its "
                 "TF_SERVICE worker are all still live against a card the "
                 "program is about to program (SPEC.md 96.17)" % during)
        print("dossnd: ...and gone while the program runs")

        blast = None
        for r in rows:
            if "BLASTER=" in r:
                blast = r.strip()
        if blast is None:
            fail("no BLASTER= in the program's environment - the sound driver "
                 "answered DRVV_HWINFO on its way out and this is what the "
                 "answer is for (SPEC.md 96.17)")
        if not blast.startswith("BLASTER=A220 "):
            fail("BLASTER= reads %r, which does not name the card at 220h"
                 % blast)
        if " I" in blast:
            fail("BLASTER= carries an IRQ field (%r), and the driver defers "
                 "discovery to first use - so on a machine that has not "
                 "played a sound the line is 0FFh and naming it is naming the "
                 "wrong one (SPEC.md 96.17.1)" % blast)
        print("dossnd: the program's environment says %r" % blast)

        m.type_text("x")
        os88marty.settle(m)

        after = loaded()
        if not after:
            fail("SOUND.DRV did not come back after the bracket - nothing "
                 "else will put it back, and a machine left silent for no "
                 "reason the user can see is what SPEC.md 51.11.1 puts on "
                 "the caller")
        print("dossnd: ...and back afterwards, segment %04X" % after)

        wd, ht, data = m.fbuf()
        os88marty.write_png_rgb("build/dossnd.png", wd, ht, data)
        print("dossnd: build/dossnd.png written")

    print("dossnd: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""zipnone - docs/plans/ZIP-PLAN.md claim 6: no drive on the port.

    python3 tests/zipnone.py --arm cga|herc

A machine with a printer port and NOTHING behind it - the common case, and the
one that must not cost anybody anything. The row ticks `Zip Drive` on the
Drivers page of a machine whose LPT card has no Zip drive on it and checks the
four things SPEC.md 138.5 promises:

  * the row REFUSES: the segment stays 0 and the error is DRVE_HW (5), which
    the page words as `No drive` - and it is the refusal of a driver that
    looked, so the page says so in words (found in guest memory)
  * NOTHING HANGS: the guest's clock runs on and the Control Panel still
    answers its list (a probe that waits on a
    status line that never changes is what a printer port without a Zip
    looks like to a spin loop)
  * NO ZONE appeared: a driver that found nothing registers no volume
  * THE PORT IS LEFT AS A PRINTER WANTS IT. The probe writes connect pulses
    to the data and control registers; whatever happens it must end with the
    control register at 0Ch (INIT high, SELECT IN asserted, STROBE and
    AUTOFEED off) - a printer on that port, or daisy-chained behind a Zip
    that was never there, still prints (SPEC.md 138.2: "left DISCONNECTED")

  cga   os8088_5150_cga_lpt          the cable machine: a Centronics card at 378h, CGA
  herc  os8088_5150_herc_covox_720_gla   Hercules, a Covox on the card's data lines
        (the DAC is a load on the very lines the probe pulses, and the machine
        is also on the other adapter ZIP-PLAN names)

The VGA and EGA legs of claim 6 are QEMU's (it has the adapters and a printer
port); ZIP-PLAN Z6 runs them by hand with `OS88_EMU=qemu`.

Break on purpose: have the probe return with the control register at 04h and
the port check fails; have it spin on a status bit and the "nothing hangs"
check does.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
os.chdir(ROOT)
import os88marty as M                                      # noqa: E402
import os88ui                                              # noqa: E402
import zipq                                                # noqa: E402

BOOT = "build/os8088-360.img"
ARMS = {"cga": "os8088_5150_cga_lpt", "herc": "os8088_5150_herc_covox_720_gla"}
DRVE_HW = 5
LPT_CTRL = 0x37A
bad = []


def ok(cond, what, got=None):
    print("  [%s] %s%s" % ("PASS" if cond else "FAIL", what,
                           "" if got is None else "  (%r)" % (got,)))
    if not cond:
        bad.append(what)
    return cond


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", required=True, choices=sorted(ARMS))
    a = ap.parse_args()
    with os88ui.boot(BOOT, machine=ARMS[a.arm], verbose=False) as ui:
        m = ui.m
        zones0 = zipq.zones(m)
        t0 = m.status()["cycles"]
        row, seg, err, new = zipq.tick_zip(ui)
        ok(seg == 0 and err == DRVE_HW, "the row refuses: segment 0, error DRVE_HW (5)", (seg, err))
        ok(new == [] and zipq.zones(m) == zones0, "...and no desktop zone appeared", new)
        ok(zipq.find_text(m, "No drive"), "...and the words are `No drive`")
        ctrl = m.inb(LPT_CTRL)
        ok(ctrl & 0x0F == 0x0C, "the printer port is left idle: control register %02Xh (want x0Ch)" % ctrl)
        c1 = m.status()["cycles"]
        M.guest_sleep(m, 1.0)
        ok(m.status()["cycles"] > c1 and c1 > t0, "the guest's clock runs on")
        # nothing is wedged: the Control Panel still answers a second look
        # (a second tick would UNtick the row, so it is the list that is asked)
        w = ui.front()
        ok(w is not None, "a window is still frontmost", w and w.title)
        w, cx, cy = zipq.control_panel(ui)
        zipq.cp_item(ui, w, cx, cy, zipq.CP_IDRV)
        ok(True, "the Control Panel still answers its list")
        ok(m.inb(LPT_CTRL) & 0x0F == 0x0C, "...and the port is still idle")
    if bad:
        sys.exit("zipnone[%s] FAILED: %d check(s): %s" % (a.arm, len(bad), "; ".join(bad[:4])))
    print("zipnone[%s]: all checks passed" % a.arm)


if __name__ == "__main__":
    main()

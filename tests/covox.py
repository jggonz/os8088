#!/usr/bin/env python3
"""THE COVOX'S ANNOUNCEMENT (SPEC.md 34.14): SOUND.DRV on a parallel port.

    python3 tests/covox.py --arm drv|auto|nolpt

A Covox Speech Thing is eight resistors on a printer port's data lines and
answers nothing, so SOUND.DRV cannot detect one. What it CAN find is the
port, and what it publishes is split along exactly that line:

  drv    MartyPC's Covox machine (os8088_5150_herc_covox_720_gla: a 5150, a
         Centronics card at 378h, a Covox on it and NO sound card) booting
         `make covoxtest`'s disk, whose SYSTEM.CFG asks for SOUND.DRV and
         sets the sound tier to SND_RT_LPT + 1, a Covox on LPT2. LPT2 and not
         LPT1 because the Hercules carries its own printer port at 3BCh, and
         that is LPT1 - the configuration that retired "the first port that
         answers" (SPEC.md 34.14). The driver must ATTACH on the ports alone
         (the fourth reason to), publish BOTH as choices (DSV_TIERS bits 4
         and 5), take the tier, publish SND_CAP_LPTDAC, and hold 378h as the
         DAC's port - read off the kernel's copy of the service table and
         the driver's own image
  auto   the same machine, `make miditest`'s disk: SOUND.DRV wanted and the
         tier left AUTO. The driver attaches and the ROW is live, but the
         cap must NOT be published - a port is not a Covox until somebody
         says so, and a package that played the DAC on a guess would be
         silent on every machine with a printer and no ladder
  nolpt  covoxsys360 (a Covox on LPT1) on the CGA 5150, which has NO
         parallel port - not even an adapter's, which is why it is not the
         Hercules machine - and no card: nothing to attach to, so the driver
         refuses and the kernel's tier byte stays where SYSTEM.CFG put it,
         with nothing published

The packages' own legs - each plays its subject through the DAC and is
checked off MartyPC's Covox capture - live with each package's test:
tests/midirack.py --arm covox, tests/trkspk.py --leg covox,
tests/apspk.py --covox, tests/vidspk.py --covox.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)
import os88marty as M          # noqa: E402
import os88ui                  # noqa: E402

COVOX_M = "os8088_5150_herc_covox_720_gla"
PLAIN_M = "os8088_5150_cga_gla"         # CGA: no adapter port either
ARMS = {
    "drv": (COVOX_M, "build/covoxsys720.img"),
    "auto": (COVOX_M, "build/midisys720.img"),
    "nolpt": (PLAIN_M, "build/covoxsys360.img"),
}
DSV_CAPS, DSV_TIERS = 0, 14             # drivers/os88drv.inc
SND_CAP_LPTDAC = 0x0100                 # apps/os88api.inc
SND_RT_LPT = 4                          # kernel/snd.inc: + n = LPTn+1
DRVR_SEG = 2                            # kernel/driver.inc: a drv_tab row


def fail(msg):
    print("FAIL %s" % msg, flush=True)
    sys.exit(1)


def u16(b):
    return b[0] | b[1] << 8


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", required=True, choices=sorted(ARMS))
    a = ap.parse_args()
    machine, img = ARMS[a.arm]
    with os88ui.boot(img, machine=machine, settle=False) as ui:
        m = ui.m
        caps = u16(m.read(m.sym("drv_svc") + DSV_CAPS, 2))
        tiers = u16(m.read(m.sym("drv_svc") + DSV_TIERS, 2))
        owner = u16(m.read(m.sym("drv_owner"), 2))
        route = m.read(m.sym("snd_route"), 1)[0]
        print("      drv_owner %04X, caps %04X, tiers %04X, snd_route %d"
              % (owner, caps, tiers, route), flush=True)
        if a.arm == "nolpt":
            if owner or caps:
                fail("nolpt: a driver is published (owner %04X, caps %04X) on "
                     "a machine with no card and no parallel port" % (owner,
                                                                      caps))
            if route != SND_RT_LPT:
                fail("nolpt: snd_route %d - SYSTEM.CFG's tier was not kept "
                     "for the machine the disk goes home to" % route)
            print("PASS nolpt: no port, no card: SOUND.DRV refused, the Covox "
                  "tier kept and nothing published", flush=True)
            return 0
        if not owner:
            fail("%s: SOUND.DRV did not attach - a parallel port alone must "
                 "be a reason to (SPEC.md 34.14)" % a.arm)
        if tiers & (7 << SND_RT_LPT) != 3 << SND_RT_LPT:
            fail("%s: DSV_TIERS %04X - want the Covox choices LPT1 (the "
                 "Hercules' 3BCh) and LPT2 (378h) and not LPT3"
                 % (a.arm, tiers))
        if a.arm == "auto":
            if caps & SND_CAP_LPTDAC:
                fail("auto: SND_CAP_LPTDAC published with the tier AUTO - a "
                     "port is not a Covox until the user says so")
            print("PASS auto: attached on the port, the Covox row live "
                  "(tiers %04X), the DAC NOT announced" % tiers, flush=True)
            return 0
        if route != SND_RT_LPT + 1:
            fail("drv: snd_route %d, want SND_RT_LPT + 1 (LPT2) from "
                 "SYSTEM.CFG" % route)
        if not caps & SND_CAP_LPTDAC:
            fail("drv: caps %04X - the Covox tier did not publish "
                 "SND_CAP_LPTDAC" % caps)
        # THE PORT, as a package asks it: the driver's own state, since
        # SNDV_DACINFO answers [cvx_port] while the cap is up
        seg = u16(m.readseg(M.KERNEL_SEG, owner + DRVR_SEG, 2))
        img = bytes(m.readseg(seg, 0, 0x2400))   # the image: cvx_ports,
        at = img.find(bytes([0xBC, 3, 0x78, 3, 0, 0]))   # then cvx_port
        port = u16(img[at + 6:at + 8]) if at >= 0 else None
        if port != 0x378:
            fail("drv: the driver's port is %r, want 378h" % port)
        print("PASS drv: SOUND.DRV attached on the port alone, tier Covox, "
              "SND_CAP_LPTDAC published, the DAC at %03Xh" % port, flush=True)
        return 0


if __name__ == "__main__":
    sys.exit(main())

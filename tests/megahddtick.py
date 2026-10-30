#!/usr/bin/env python3
"""megahddtick - HDD.DRV ticked in from the Control Panel mid-session on
kern_mega, which hard-froze the machine (KERN-MEGA-PLAN 14.76).

    make mega megarun && python3 tests/megahddtick.py [--keep]

The owner, on 86Box's mr286 with an IDE disk: boot kern_mega, open Control
Panel > Drivers, tick Hard Drive - whether the driver had been unticked in
the same session or was simply not wanted at boot - and the machine stops
dead: no pointer, no keys. Under Bochs the frozen CPU sits at IP 0084 of
HDD.DRV's CODE ALIAS with IF set and IRQ0 still ticking, and the bytes it is
running are build/mbr.bin's - its `hlt / jmp $-1` after "No active
partition". The driver's own image had been overwritten from offset 0 by
the disk's MBR.

How: on a mid-session load every claim is a kern_mega selector (MEMORY-PLAN
MC7), so hd_at's rung-1 pairing proof - LBA 0 through the BIOS into a 1KB
OSAPI_MEM_CLAIM_DMA at ES:0 - went through hd_raw's bounce. The bounce of
the day copied IN from DS:BX and OUT to DS:BX (`push ds / pop es`), i.e.
HDD.DRV's own image at offset 0, so the sector it had just read landed on
the driver's header and code; the kernel's next call into the driver ran
the MBR. A boot-time load never meets it: the boot loads drivers in real
mode, ES a plain paragraph, and the bounce is never taken. The fix is
f479d0f9's (the installer's 07BE fault, tests/megainst.py): the bounce
copies the CALLER's ES:BX, CX sectors of it. This row is the same defect
reached by the second door, which that row does not walk.

Under Bochs at 16MB, a C: on native IDE, HDD.DRV wanted: Control Panel >
Drivers, untick Hard Drive (drv_tab's row goes to 0), tick it again (the
row comes back), and then: the UI must take input; the image's 32-byte
header must be build/hdd.drv's and not the MBR; its device table (the
rung-1 pairing's answer) must read as it did at boot; C:'s Disk window must
open; and no `kern_mega FAULT`/`CLOSED` line.

Broken on purpose (docs/WRITING-TESTS.md 1): with f479d0f9^'s hd_raw
(drivers/hdd/mount.inc) it fails at the tick - "the UI never took input
again" and the image's head is the MBR's (`fa 31 c0 8e d8 ...`).
"""
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
import os88bochs  # noqa: E402
import os88drv  # noqa: E402
from megashed import menu  # noqa: E402

APPS = os.path.join(ROOT, "build", "megaapps.img")
HDD = os.path.join(ROOT, "build", "hdd.drv")
MBR = os.path.join(ROOT, "build", "mbr.bin")
CFG_HDD = 1 << 1                # drv_cfgbit: HDD.DRV is bit 1
DRVR_SZ, DRVR_SEG = 16, 2       # kernel/driver.inc's row
HDD_ROW = 1                     # drv_tab's order (SPEC.md 31.1)
CP_DRIVERS = (192, 183)         # a fresh Control Panel's page list, Bochs
HDD_BOX = (265, 200)            # the Drivers page's Hard Drive check box
C_ICON = (591, 160)
HEAD = 32                       # the header; HDD.DRV's data follows it
DEVS = 2 + 12                   # hd_ndev, hd_sel and the first device row
                                # (drivers/hdd/hdcom.inc, right after it)


class Fail(Exception):
    pass


def main():
    keep = "--keep" in sys.argv
    for f in (APPS, HDD, MBR, os.path.join(ROOT, "build", "mega.img"),
              os.path.join(ROOT, "build", "megarun.o88")):
        if not os.path.exists(f):
            sys.exit("megahddtick: %s is missing - `make mega megarun` "
                     "first" % os.path.relpath(f, ROOT))
    with open(HDD, "rb") as f:
        want = os88drv.image_unwrap(f.read())[:HEAD]
    with open(MBR, "rb") as f:
        mbr = f.read()[:HEAD]
    d = tempfile.mkdtemp(prefix="megahddtick-")
    t0 = time.time()
    box = os88bochs.Box(d, [], cfg=CFG_HDD, bimg=APPS, btype="1_44")
    bad = []

    def faulted():
        f = [l for l in box.lines()
             if l.startswith(("kern_mega FAULT", "kern_mega CLOSED"))]
        if f:
            raise Fail(f[0])

    def seg():
        return int.from_bytes(box.read(box.sym("drv_tab") + HDD_ROW * DRVR_SZ
                                       + DRVR_SEG, 2), "little")

    def image(n):
        s = seg()
        return s, box.read(box.sel_base(s) if s & 4 else s << 4, n)

    def tick(what, cond):
        box.goto(*HDD_BOX)
        box.xd("click", 1)
        box.until(lambda: cond(seg()), what, 60.0)
        faulted()

    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            raise Fail("kern_mega never reached protected mode")
        box.up()
        box.capture()
        if not seg():
            raise Fail("HDD.DRV was not loaded at boot - nothing to untick")
        boot = image(HEAD + DEVS)[1][HEAD:]
        if not boot[0]:
            raise Fail("HDD.DRV found no device at boot - is C: there?")
        menu(box, 35)                   # Control Panel
        box.ui_done("the Control Panel", secs=120)
        box.click(*CP_DRIVERS, what="the Drivers page")
        tick("Hard Drive to unload", lambda s: s == 0)
        box.ui_done("the untick")
        print("   ok    unticked: drv_tab's Hard Drive row is 0", flush=True)
        tick("Hard Drive to load again", lambda s: s != 0)
        try:
            box.ui_done("the tick", secs=40)
        except RuntimeError:
            raise Fail("the UI never took input again after the tick - the "
                       "owner's hard freeze")
        finally:
            s, img = image(HEAD + DEVS)
            head = img[:HEAD]
            if img[HEAD:] != boot:
                bad.append("HDD.DRV's device table after the tick (%s) is "
                           "not the boot's (%s) - the rung-1 pairing came "
                           "out differently" % (img[HEAD:].hex(" "),
                                                boot.hex(" ")))
            else:
                print("   ok    the device table is the boot's (kind %d, "
                      "base %03Xh)" % (boot[2], boot[12] | boot[13] << 8),
                      flush=True)
            if head == want:
                print("   ok    HDD.DRV's image head is intact (seg %04X)"
                      % s, flush=True)
            else:
                bad.append("HDD.DRV's image head (seg %04X) is %s - %s" % (
                    s, head[:8].hex(" "),
                    "the disk's MBR, read over the driver" if head == mbr
                    else "not build/hdd.drv's image"))
        print("   ok    ticked: the UI takes input again", flush=True)
        box.dbl(*C_ICON, what="C:'s Disk window")
        faulted()
        print("   ok    C:'s Disk window opened", flush=True)
    except (Fail, RuntimeError) as e:
        bad.append(str(e))
        box.shot(os.path.join(d, "fail.png"))
    finally:
        box.stop()
    print("megahddtick: %.0fs" % (time.time() - t0))
    if bad:
        for b in bad:
            print("FAIL: %s" % b)
        print("megahddtick: the boot's files are in %s" % d)
        return 1
    if not keep:
        shutil.rmtree(d, ignore_errors=True)
    else:
        print("megahddtick: kept %s" % d)
    print("megahddtick: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

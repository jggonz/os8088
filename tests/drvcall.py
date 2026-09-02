#!/usr/bin/env python3
"""Can a PACKAGE reach a DRIVER? (SPEC.md 20.11, docs/NET-STACK-PLAN.md stage A)

    make && make drvcalltest && python3 tests/drvcall.py [--adapter cga|herc]

Three assertions, and the middle one is the reason the cell is an X stub.

1. REFUSAL FIRST. With no DRVC_FILE driver published, OSAPI_DRV_CALL must
   answer CF=1 and the package must report '--'. This runs BEFORE the Ram Disk
   is ticked, which is what makes assertion 2 mean something: a check that
   only ever sees the passing state cannot fail for the reason it claims.

2. THE DRIVER GOT THE PACKAGE'S SEGMENT. RDPV_UPCASE is handed ES:SI into the
   package's own image and upper-cases it IN PLACE. Every other driver entry
   point in the machine is handed KERNEL_SEG in ES and could not have touched
   those bytes, so 'HELLO WORLD' coming back is the whole of what separates
   "the call arrived" from "the call arrived usefully".

3. THE DRIVER'S OWN FENCE. A verb number RAMDISK.DRV does not have must be
   refused, not dispatched: an unchecked verb into a jump table is a wild near
   call inside the driver.

Read out of the package's IMAGE rather than off the screen. The three lines
are drawn too, and a screenshot would prove they were drawn - it would not
prove which bytes they carry, and 'HELLO WORLD' against 'hello world' is
eleven pixels of difference per glyph on a 1bpp adapter.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
from os88fixture import need                           # noqa: E402

S = os88sym.linear

_IBMROM = os.path.exists("tools/martypc/roms/"
                         "BIOS_IBM5150_27OCT82_1501476_U33.BIN")
MACHINE = ({"cga": "os8088_5150_cga", "herc": "os8088_5150_herc"} if _IBMROM
           else {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla"})

RD_ROW = 3                          # drv_tab row 3 is the RAM disk: the
                                    # Ethernet card came forward to row 2
                                    # when the page was reordered (31.1)
DRVR_SZ, DRVR_SEG = 16, 2
CP_I0Y, CP_IROWH, CP_IDRV = 6, 14, 2
CP_RX, CP_DBY1, CP_DROWH = 96, 20, 26


def say(*a):
    print(*a)
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    ap.add_argument("--shot", default=None)
    a = ap.parse_args()

    need("drvcalltest")            # `all` builds nothing under tests/

    fails = []
    img = os.path.getsize("build/drvcall.bin")
    with os88marty.launch("build/os8088-360.img",
                          apps="build/drvcall360.img",
                          machine=MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        if not wins:
            sys.exit("drvcall: no Disk window after double-clicking B:")
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        say("B: lists %r" % [r[0] for r in dispcp.listing(m, S)])

        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "DRVCALL.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("drvcall: DRVCALL.O88 did not open a window")
        dcwin = wins2[-1]
        rec = m.read(S("wm_wins") + dcwin * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        say("package at %04X" % pseg)

        def lines():
            """The three report strings, out of the package's own image."""
            raw = m.readseg(pseg, 0, img)
            out = []
            for tag in (b"Ping: ", b"Upcase: ", b"Bad verb: "):
                i = raw.find(tag)
                if i < 0:
                    sys.exit("drvcall: %r is not in the image - the package "
                             "is not the one this test was built for" % tag)
                out.append(raw[i:raw.index(b"\0", i)].decode("latin-1"))
            return out

        # --- 1: no driver published yet ------------------------------------
        # No driver row is wanted by default (SPEC.md 51.3), so this is the
        # machine as it boots and the refusal is the machine's own answer.
        before = lines()
        for s in before:
            say("  before: %s" % s)
        if before[0] != "Ping: --":
            fails.append("with no driver published the ping answered %r - the "
                         "kernel dispatched something it should have refused"
                         % before[0])
        if not before[1].endswith("n=00") or "HELLO" in before[1]:
            fails.append("upcase ran with no driver published: %r" % before[1])
        if not before[2].endswith("refused "):
            fails.append("the bad-verb check reads %r before any driver is "
                         "loaded, which cannot be right either way"
                         % before[2])

        # --- tick the Ram Disk ---------------------------------------------
        mo.menu(8, 8, 8, 40)                        # chip menu -> Control Panel
        os88marty.settle(m)
        cps = [w for w in dispcp.win_list(m, S)]
        cp = None
        for w in cps:
            x, y, ww, hh = dispcp.win_rect(m, S, w)
            if ww >= 280 and hh >= 100:
                cp = (w, x, y)
        if cp is None:
            sys.exit("drvcall: no Control Panel window")
        _, cx, cy = cp
        x0, y0 = cx + 1, cy + 18
        mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)
        os88marty.settle(m)
        mo.click(x0 + CP_RX + 40,
                 y0 + CP_DBY1 + RD_ROW * CP_DROWH + CP_DROWH // 2)
        time.sleep(6)
        os88marty.settle(m)
        row = m.read(S("drv_tab") + RD_ROW * DRVR_SZ + DRVR_SEG, 2)
        seg = row[0] | (row[1] << 8)
        say("RAMDISK.DRV at %04X" % seg)
        if not seg:
            sys.exit("drvcall: the Ram Disk did not load - nothing below can "
                     "be measured")
        mo.click(cx + 8, cy + 9)                    # close the panel (31.8)
        os88marty.settle(m)

        # --- 2 and 3: click the package's window to re-probe ----------------
        dx, dy, dw, dh = dispcp.win_rect(m, S, dcwin)
        mo.click(dx + dw // 2, dy + dh - 8)
        os88marty.settle(m)
        after = lines()
        for s in after:
            say("  after:  %s" % s)

        if after[0] != "Ping: DR":
            fails.append("the ping answered %r with the driver loaded - the "
                         "package did not reach it" % after[0])
        if after[1] != "Upcase: HELLO WORLD n=11":
            fails.append("upcase came back %r - the driver was not handed the "
                         "package's own segment in ES" % after[1])
        if not after[2].endswith("refused "):
            fails.append("the driver DISPATCHED a verb it does not have: %r"
                         % after[2])

        if a.shot:
            sw, sh, srows = (m.vram("herc") if a.adapter == "herc"
                             else m.vram("cga"))
            os88marty.write_png(a.shot, sw, sh, srows)
            say("wrote %s (%dx%d)" % (a.shot, sw, sh))

    for f in fails:
        say("FAIL: " + f)
    say("drvcall: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""The Ethernet Setup window: an address set by hand, and remembered (72.7).

    make && make ethertest && python3 tests/ethcfg.py

tests/ethernet.py proves DHCP, which is how a machine normally gets an
address. This proves the OTHER case - a LAN with no DHCP server, or a
crossover cable to one other PC - and it exists because every part of it fails
QUIETLY: a window that draws but takes no keystroke, a field whose rect and
whose hit test have drifted apart, an Ok that parses nothing, and a setting
that is applied and never written.

FIVE ASSERTIONS, in the order a user meets them.

1. THE DEFAULT IS AUTOMATIC. A freshly built image comes up in DHCP with
   slirp's address, which is what says this feature is an addition rather than
   a replacement.

2. THE WINDOW OPENS AND IS SEEDED FROM THE LIVE ADDRESSES. `Set Up` puts up a
   216x141 window whose four fields already hold what DHCP got - which is the
   thing a user opens this to edit.

3. TYPING REACHES THE FIELD AND `Ok` APPLIES IT. Keystrokes go through the
   8255 and int 09h into W_ONKEY, and the driver's LIVE addresses change - read
   out of its own image, not off the screen.

4. THE MODE IS A MODE. `eth_mode` is Manual, the page says so instead of
   `Bound`, and Renew is greyed - because on a manual machine there is no
   server being asked, which is SPEC.md 47 rule 3's kind of fact.

5. **IT SURVIVES A REBOOT.** The Control Panel is CLOSED (SPEC.md 31.8: no
   setting reaches a disk on a click), the machine is restarted, and the
   address is still there. That is the assertion the feature is FOR: without
   it the user retypes an address every boot, which is the opposite of easier.

Then it puts the machine back on Automatic and reboots again, because a test
that leaves a manual address on the image would make the NEXT run of
tests/ethernet.py pass while proving nothing about DHCP. That one is
self-cleaning too, but a gate that dirties the tree for its neighbour is a
gate that will eventually be run in the wrong order.
"""
import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
sys.path.insert(0, "/home/user/os8088")
import dispcp                                          # noqa: E402
from ethernet import (Qemu, ether_syms, u16, dotted, S, Mouse,  # noqa: E402
                      type_url)
import os88qemu                                              # noqa: E402

# the Control Panel's own geometry, as tests/ethernet.py reads it
CP_I0Y, CP_IROWH, CP_RX = 6, 14, 96
# **THE ETHERNET ROW IS FOUND, NOT COUNTED.** It is the first DRIVER page, so
# it sits at [cp_nst] - the static/driver boundary (SPEC.md 31.9) - and that
# number is not a constant: on a machine with one adapter the Display page is
# not drawn at all (SPEC.md 31.10.1), so it is 5 on VGA and 4 on CGA. Counting
# five rows here would click `Sound` on the machine this OS is calibrated
# against and then fail somewhere else entirely.
# the Ethernet page's two buttons (drivers/ether/etherui.inc)
EU_BX, EU_B2X, EU_BW, EU_BY, EU_BH = 2, 76, 68, 108, 16
# the Setup window (drivers/ether/ethcfg.inc)
EC_FW, EC_FH = 216, 141
EC_R2X, EC_F0Y, EC_FPIT, EC_FHT = 108, 20, 20, 14
EC_B1X, EC_B2X, EC_BY, EC_BW, EC_BH = 72, 140, 106, 60, 16
TITLE_H = 18
NEWIP = "10.0.2.77"


def say(*a):
    print(*a)
    sys.stdout.flush()


def boot():
    if os.path.exists("build/qemu.pid"):
        try:
            os.kill(int(open("build/qemu.pid").read().strip()), 15)
            time.sleep(1.0)
        except (OSError, ValueError):
            pass
    for f in ("build/qmp.sock", "build/qemu.pid"):
        if os.path.exists(f):
            os.remove(f)
    # `make test` DAEMONISES the emulator, so it outlives this script
    # unless somebody kills it - and the somebody is us (os88qemu).
    os88qemu.own()
    r = subprocess.run(["make", "test", "ETHER=1",
                        "TESTIMG=build/ether360.img"],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("ethcfg: make test failed:\n" + r.stdout + r.stderr)
    return Qemu()


def qmp(*cmds):
    subprocess.run(["python3", "tools/qmp.py", "build/qmp.sock"] + list(cmds),
                   check=True, capture_output=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=None)
    a = ap.parse_args()
    fails = []

    # A FRESH IMAGE, for the reason tests/ethernet.py rebuilds one: QEMU mounts
    # it writable and this test's whole subject is a file the guest writes.
    if os.path.exists("build/ether360.img"):
        os.remove("build/ether360.img")
    r = subprocess.run(["make", "ethertest"], capture_output=True, text=True)
    if r.returncode:
        sys.exit("ethcfg: make ethertest failed:\n" + r.stdout + r.stderr)

    syms = ether_syms()
    m, mo = boot(), Mouse()
    seg = 0
    for _ in range(150):
        time.sleep(0.4)
        seg = u16(m.read(S("drv_tab") + 2 * 16 + 2, 2))
        if seg:
            break
    if not seg:
        m.quit()
        sys.exit("ethcfg: ETHER.DRV never attached")

    def db(n):
        return m.readseg(seg, syms[n], 1)[0]

    def dip(n):
        return dotted(m.readseg(seg, syms[n], 4))

    cp_ether = m.read(S("cp_nst"), 1)[0]

    time.sleep(10)                              # let DHCP finish
    say("boot 1: mode %d, addr %s" % (db("eth_mode"), dip("eth_ip")))
    if db("eth_mode") != 0:
        fails.append("a fresh image did not come up Automatic")
    if dip("eth_ip") != "10.0.2.15":
        fails.append("DHCP did not bind on the first boot (%s), so nothing "
                     "below is testing what it claims" % dip("eth_ip"))

    # --- the Control Panel, its Ethernet page, and Set Up ------------------
    qmp("sleep 0.1")
    subprocess.run(["python3", "tools/mouse.py", "build/qmp.sock",
                    "down", "8", "8"], check=True, capture_output=True)
    time.sleep(0.4)
    subprocess.run(["python3", "tools/mouse.py", "build/qmp.sock",
                    "to", "8", "40"], check=True, capture_output=True)
    time.sleep(0.4)
    subprocess.run(["python3", "tools/mouse.py", "build/qmp.sock", "up"],
                   check=True, capture_output=True)
    time.sleep(3)
    cp = None
    for w in dispcp.win_list(m, S):
        x, y, ww, hh = dispcp.win_rect(m, S, w)
        if ww >= 280 and hh >= 100:
            cp = (x, y)
    if cp is None:
        m.quit()
        sys.exit("ethcfg: no Control Panel window")
    cx, cy = cp
    x0, y0 = cx + 1, cy + TITLE_H
    mo.click(x0 + 40, y0 + CP_I0Y + cp_ether * CP_IROWH + 7)
    time.sleep(2)
    mo.click(x0 + CP_RX + EU_B2X + EU_BW // 2, y0 + EU_BY + EU_BH // 2)
    time.sleep(2.5)

    sw = [w for w in dispcp.win_list(m, S)
          if dispcp.win_rect(m, S, w)[2] == EC_FW]
    if not sw:
        m.quit()
        sys.exit("ethcfg: `Set Up` opened no window - and a window that is "
                 "CLAMPED by wm_fit would not be this width either")
    wx, wy = dispcp.win_rect(m, S, sw[0])[:2]
    ctx, cty = wx + 1, wy + TITLE_H + 1
    say("setup window at (%d,%d)" % (wx, wy))

    # --- Manual, then retype the address -----------------------------------
    mo.click(ctx + EC_R2X + 6, cty + 2 + 6)
    time.sleep(1.5)
    mo.click(ctx + 120, cty + EC_F0Y + EC_FHT // 2)
    time.sleep(0.8)
    qmp(*(["sendkey end", "sleep 0.1"] +
          ["sendkey backspace", "sleep 0.05"] * 16))
    type_url(NEWIP)
    time.sleep(1)
    if a.shot:
        subprocess.run(["python3", "tools/shot.py", "build/qmp.sock", a.shot],
                       check=True)
        say("wrote %s" % a.shot)

    mo.click(ctx + EC_B1X + EC_BW // 2, cty + EC_BY + EC_BH // 2)   # Ok
    time.sleep(2)
    say("after Ok: mode %d, addr %s, mask %s, router %s"
        % (db("eth_mode"), dip("eth_ip"), dip("eth_mask"), dip("eth_gw")))
    if db("eth_mode") != 1:
        fails.append("Ok did not switch the driver to Manual")
    if dip("eth_ip") != NEWIP:
        fails.append("the address is %s and %s was typed - the keystrokes did "
                     "not reach the field, or Ok did not parse it"
                     % (dip("eth_ip"), NEWIP))
    if dip("eth_mask") != "255.255.255.0":
        fails.append("the subnet mask came out %s: a field that was NOT edited "
                     "did not survive Ok" % dip("eth_mask"))
    if [w for w in dispcp.win_list(m, S)
            if dispcp.win_rect(m, S, w)[2] == EC_FW]:
        fails.append("the Setup window is still up after Ok")

    # --- close the panel: THAT is what writes it (SPEC.md 31.8) ------------
    mo.click(cx + 8, cy + 9)
    time.sleep(5)
    m.quit()
    time.sleep(2)

    # --- and again --------------------------------------------------------
    m = boot()
    seg = 0
    for _ in range(150):
        time.sleep(0.4)
        seg = u16(m.read(S("drv_tab") + 2 * 16 + 2, 2))
        if seg:
            break
    time.sleep(6)
    say("boot 2: mode %d, addr %s, mask %s, router %s, name %s"
        % (db("eth_mode"), dip("eth_ip"), dip("eth_mask"), dip("eth_gw"),
           dip("eth_dns")))
    if db("eth_mode") != 1 or dip("eth_ip") != NEWIP:
        fails.append("the setting did not survive a reboot: mode %d, addr %s. "
                     "The panel's close is what writes ETHER.CFG, so either "
                     "the write failed or DRVV_READY did not read it back"
                     % (db("eth_mode"), dip("eth_ip")))

    # --- put it back, so the next gate is not testing this one's leftovers -
    say("restoring Automatic")
    subprocess.run(["python3", "tools/mouse.py", "build/qmp.sock",
                    "down", "8", "8"], check=True, capture_output=True)
    time.sleep(0.4)
    subprocess.run(["python3", "tools/mouse.py", "build/qmp.sock",
                    "to", "8", "40"], check=True, capture_output=True)
    time.sleep(0.4)
    subprocess.run(["python3", "tools/mouse.py", "build/qmp.sock", "up"],
                   check=True, capture_output=True)
    time.sleep(3)
    cp = None
    for w in dispcp.win_list(m, S):
        x, y, ww, hh = dispcp.win_rect(m, S, w)
        if ww >= 280 and hh >= 100:
            cp = (x, y)
    if cp:
        cx, cy = cp
        x0, y0 = cx + 1, cy + TITLE_H
        mo.click(x0 + 40, y0 + CP_I0Y + cp_ether * CP_IROWH + 7)
        time.sleep(2)
        mo.click(x0 + CP_RX + EU_B2X + EU_BW // 2, y0 + EU_BY + EU_BH // 2)
        time.sleep(2.5)
        sw = [w for w in dispcp.win_list(m, S)
              if dispcp.win_rect(m, S, w)[2] == EC_FW]
        if sw:
            wx, wy = dispcp.win_rect(m, S, sw[0])[:2]
            ctx, cty = wx + 1, wy + TITLE_H + 1
            mo.click(ctx + 2 + 6, cty + 2 + 6)          # Automatic
            time.sleep(1.5)
            mo.click(ctx + EC_B1X + EC_BW // 2, cty + EC_BY + EC_BH // 2)
            time.sleep(2)
        mo.click(cx + 8, cy + 9)
        time.sleep(5)
    say("back to: mode %d, addr %s" % (db("eth_mode"), dip("eth_ip")))
    if db("eth_mode") != 0:
        fails.append("Automatic did not take: the mode is still %d"
                     % db("eth_mode"))
    if dip("eth_ip") != "10.0.2.15":
        fails.append("Automatic left the address at %s: choosing it must RUN a "
                     "DHCP exchange, and nothing else pumps the ring while a "
                     "Control Panel click holds the lock (SPEC.md 72.2.1)"
                     % dip("eth_ip"))
    m.quit()

    for f in fails:
        say("FAIL: " + f)
    say("ethcfg: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

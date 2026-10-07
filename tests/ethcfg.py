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

# THIS TREE'S root, DERIVED - never a hard-coded path. A literal is right in the
# checkout it was written in and wrong in a git worktree, which is how parallel
# work is done here: os88sym re-assembles ROOT/kernel/kernel.asm and compares it
# against ROOT/build/kernel.bin, so a literal ROOT answers about a DIFFERENT
# kernel from the image being booted.
_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))
sys.path.insert(0, _OS88_ROOT)
import dispcp                                          # noqa: E402
import os88geom                                        # noqa: E402
from ethernet import (Qemu, ether_syms, u16, dotted, S, Mouse,  # noqa: E402
                      type_url)
import os88qemu                                              # noqa: E402
import os88build                                       # noqa: E402

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
            pid = int(open("build/qemu.pid").read().strip())
            os.kill(pid, 15)
            os88qemu.gone(pid)
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


# EVERY WAIT IN THIS ROW IS ON THE GUEST'S CLOCK (tests/os88qemu.py) - on the
# window or the driver word the next line reads where there is one, and a
# tick-counted pause of the old length where there is not.
def drvseg(m):
    """ETHER.DRV's segment out of drv_tab, once it is there (60 guest s)."""
    os88qemu.acted(m, lambda: u16(m.read(S("drv_tab") + 2 * 16 + 2, 2)) != 0,
                   secs=60, what="ETHER.DRV's drv_tab row", poll=0.4)
    return u16(m.read(S("drv_tab") + 2 * 16 + 2, 2))


def find_cp(m):
    cp = None
    for w in dispcp.win_list(m, S):
        x, y, ww, hh = dispcp.win_rect(m, S, w)
        if ww >= 280 and hh >= 100:
            cp = (x, y)
    return cp


EC_TITLE = b"Ethernet Setup"             # drivers/ether/ethcfg.inc's ec_s_title


def setup_wins(m):
    """The Setup window(s), matched on the TITLE through W_SEG:W_TITLE.

    It was matched on W_W == EC_FW, and the window is not that wide: the
    kernel puts a window's CONTENT on a multiple of 8 (SPEC.md 11.94), so the
    template's (60, 30, 216, 141) comes up at (55, 30, 218, 141) - and this
    row reported "`Set Up` opened no window" with the window on the screen.
    A title is the one field the kernel does not adjust."""
    out = []
    for w in dispcp.win_list(m, S):
        rec = m.read(S("wm_wins") + w * os88geom.WIN_SIZE, os88geom.WIN_SIZE)
        seg = u16(rec[os88geom.W_SEG:os88geom.W_SEG + 2])
        tp = u16(rec[os88geom.W_TITLE:os88geom.W_TITLE + 2])
        if seg and m.readseg(seg, tp, len(EC_TITLE) + 1) == EC_TITLE + b"\0":
            out.append(w)
    return out


def open_cp(m):
    """Chip menu -> Control Panel, and wait for its window."""
    subprocess.run(["python3", "tools/mouse.py", "build/qmp.sock",
                    "down", "8", "8"], check=True, capture_output=True)
    os88qemu.pace(m, 0.4)
    subprocess.run(["python3", "tools/mouse.py", "build/qmp.sock",
                    "to", "8", "40"], check=True, capture_output=True)
    os88qemu.pace(m, 0.4)
    subprocess.run(["python3", "tools/mouse.py", "build/qmp.sock", "up"],
                   check=True, capture_output=True)
    if os88qemu.acted(m, lambda: find_cp(m) is not None, secs=15,
                      what="the Control Panel window", poll=0.25):
        os88qemu.pace(m, 1)             # ...and its first paint
    return find_cp(m)


def open_setup(m, mo, cx, cy, cp_ether):
    """The Ethernet page, then its Set Up window."""
    x0, y0 = cx + 1, cy + TITLE_H
    mo.click(x0 + 40, y0 + CP_I0Y + cp_ether * CP_IROWH + 7)
    os88qemu.ui_done(m, S, cap=2.0)     # the page swap: the UI, finished
    mo.click(x0 + CP_RX + EU_B2X + EU_BW // 2, y0 + EU_BY + EU_BH // 2)
    if os88qemu.acted(m, lambda: bool(setup_wins(m)), secs=10,
                      what="the Set Up window", poll=0.25):
        os88qemu.pace(m, 0.5)
    return setup_wins(m)


def quit_and_wait(m):
    """quit, and have QEMU GONE before the image is reused - a host wait on
    a host thing, by the process table."""
    try:
        pid = int(open("build/qemu.pid").read().strip())
    except (OSError, ValueError):
        pid = None
    m.quit()
    if pid:
        os88qemu.gone(pid, 2.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=None)
    a = ap.parse_args()
    fails = []

    # A FRESH IMAGE, for the reason tests/ethernet.py rebuilds one: QEMU mounts
    # it writable and this test's whole subject is a file the guest writes.
    if os.path.exists(os88build.at("build/ether360.img")):
        os.remove("build/ether360.img")
    r = subprocess.run(["make", "ethertest"], capture_output=True, text=True)
    if r.returncode:
        sys.exit("ethcfg: make ethertest failed:\n" + r.stdout + r.stderr)

    syms = ether_syms()
    m, mo = boot(), Mouse()
    seg = drvseg(m)
    if not seg:
        m.quit()
        sys.exit("ethcfg: ETHER.DRV never attached")

    def db(n):
        return m.readseg(seg, syms[n], 1)[0]

    def dip(n):
        return dotted(m.readseg(seg, syms[n], 4))

    cp_ether = m.read(S("cp_nst"), 1)[0]

    os88qemu.acted(m, lambda: db("dhcp_st") == 3, secs=30,   # DH_BOUND
                   what="DHCP bound", poll=0.4)
    say("boot 1: mode %d, addr %s" % (db("eth_mode"), dip("eth_ip")))
    if db("eth_mode") != 0:
        fails.append("a fresh image did not come up Automatic")
    if dip("eth_ip") != "10.0.2.15":
        fails.append("DHCP did not bind on the first boot (%s), so nothing "
                     "below is testing what it claims" % dip("eth_ip"))

    # --- the Control Panel, its Ethernet page, and Set Up ------------------
    os88qemu.pace(m, 0.1)
    cp = open_cp(m)
    if cp is None:
        m.quit()
        sys.exit("ethcfg: no Control Panel window")
    cx, cy = cp
    sw = open_setup(m, mo, cx, cy, cp_ether)
    if not sw:
        m.quit()
        sys.exit("ethcfg: `Set Up` opened no window - and a window that is "
                 "CLAMPED by wm_fit would not be this width either")
    wx, wy = dispcp.win_rect(m, S, sw[0])[:2]
    ctx, cty = wx + 1, wy + TITLE_H + 1
    say("setup window at (%d,%d)" % (wx, wy))

    # --- Manual, then retype the address -----------------------------------
    mo.click(ctx + EC_R2X + 6, cty + 2 + 6)
    os88qemu.pace(m, 1.5)
    mo.click(ctx + 120, cty + EC_F0Y + EC_FHT // 2)
    os88qemu.pace(m, 0.8)
    # the spacing is the keyboard's, a QEMU device on the host's clock; the
    # pause after it is the guest's
    qmp(*(["sendkey end", "sleep 0.1"] +
          ["sendkey backspace", "sleep 0.05"] * 16))
    type_url(NEWIP)
    os88qemu.pace(m, 1)
    if a.shot:
        subprocess.run(["python3", "tools/shot.py", "build/qmp.sock", a.shot],
                       check=True)
        say("wrote %s" % a.shot)

    mo.click(ctx + EC_B1X + EC_BW // 2, cty + EC_BY + EC_BH // 2)   # Ok
    os88qemu.acted(m, lambda: db("eth_mode") == 1 and dip("eth_ip") == NEWIP
                   and not setup_wins(m), secs=10, what="Ok taken", poll=0.25)
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
    if setup_wins(m):
        fails.append("the Setup window is still up after Ok")

    # --- close the panel: THAT is what writes it (SPEC.md 31.8) ------------
    mo.click(cx + 8, cy + 9)
    # the close is the WRITE: the window going is the handler running, and
    # the UI going idle behind it is the floppy done (two seconds the cap)
    os88qemu.acted(m, lambda: find_cp(m) is None, secs=15,
                   what="the Control Panel closing", poll=0.25)
    os88qemu.ui_done(m, S, cap=2.0, what="ETHER.CFG's write")
    quit_and_wait(m)

    # --- and again --------------------------------------------------------
    m = boot()
    seg = drvseg(m)
    os88qemu.acted(m, lambda: db("eth_mode") == 1 and dip("eth_ip") == NEWIP,
                   secs=10, what="the saved setting", poll=0.25)
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
    cp = open_cp(m)
    if cp:
        cx, cy = cp
        sw = open_setup(m, mo, cx, cy, cp_ether)
        if sw:
            wx, wy = dispcp.win_rect(m, S, sw[0])[:2]
            ctx, cty = wx + 1, wy + TITLE_H + 1
            mo.click(ctx + 2 + 6, cty + 2 + 6)          # Automatic
            os88qemu.pace(m, 1.5)
            mo.click(ctx + EC_B1X + EC_BW // 2, cty + EC_BY + EC_BH // 2)
            os88qemu.ui_done(m, S, cap=2.0)     # Ok, and its DHCP exchange
        mo.click(cx + 8, cy + 9)
        # ...and Automatic RUNS a DHCP exchange: the address coming back is
        # the guest's own answer (seven seconds was the old wait's total)
        os88qemu.acted(m, lambda: db("eth_mode") == 0
                       and dip("eth_ip") == "10.0.2.15", secs=10,
                       what="Automatic re-bound", poll=0.25)
        # ...and the panel's write behind it, on the UI task
        os88qemu.ui_done(m, S, cap=2.0, what="ETHER.CFG's write")
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

#!/usr/bin/env python3
"""A network card and an MPU-401 in one machine (SPEC.md 72.9.1, 34.13.1).

    make ethertest && python3 tests/netcoexist.py [--sys IMG] [--arm NAME]

THE FIELD REPORT: an NE1000/NE2000 and an MPU-401 set up together on their
own ports and IRQs, and the network was gone; take the MPU out and it came
back. Nothing was overlapping on the bus. What overlapped was the PROBES:

- ETHER.DRV sweeps seven 32-port windows, 300h first and 320h second, and a
  card at 320h has its DATA port at 330h - which is where an MPU-401 lives. An
  MPU's data port reads its last byte, FEh after the ACK to its reset, so the
  "PROM" came back FE:FE:FE:FE:FE:FE: unicast, every pair agreeing, plausible
  by every test the probe had. The driver attached to a card that is not
  there, and never reached the real one further down the sweep.
- And the probe WROTE to whatever answered first, so an MPU at its other
  address, 300h, took the 8390's set-up into its data port.
- SOUND.DRV's MPU probe goes the other way. With no MPU at 330h it tries 300h,
  which is an NE2000's factory address, and its RESET (FFh into base + 1)
  lands in the 8390's PSTART. At boot that is harmless, because the sound
  driver is row 0 and ETHER.DRV resets the card after it - but a Sound tick in
  the Control Panel with the network up is that write into a live card.

QEMU BY NAME, for tests/ethernet.py's reason: MartyPC has no network card.
QEMU has no MPU-401 either, so the MPU is `isa-debugcon` with readback=0xFE -
one port that reads FEh on every read and records every byte written to it,
which is the one property of an MPU the probes ever met. The NIC's register
writes come from QEMU's own `ne2000_ioport_write` trace.

THREE ARMS, one machine each:

  mpu330   the report. The card at 340h, the MPU latch at 330h. The driver
           must bind 340h - not the phantom at 320h - and DHCP must bind
           10.0.2.15. Nothing may be written to the MPU's data port.
  mpu300   the card at 340h, the MPU latch at 300h, which the sweep visits
           FIRST. Not one byte to the MPU, and the card at 340h still found.
  sndtick  the card at 300h, no MPU anywhere, the network up - then Sound is
           ticked in the Control Panel. Its MPU probe must not write the
           card's base + 1 (offset 1 in the trace), and the attach must still
           happen (QEMU's LPT1 is a Covox candidate, so the driver comes up).

Each arm FAILS on the drivers before the fix, which is the point of having
three rather than one: mpu330 bound 320h, mpu300 wrote 21 22 0A into the
MPU, and sndtick wrote FFh to PSTART.

AND A FOURTH, for SPEC.md 31.15 and 51.9.2's port pin, end to end:

  pinned   the card at 2A0h, which the sweep never tries, so the first boot
           must REFUSE the driver. Then the Drivers page's Ports view (its
           heading row's button), stepped to 2A0h by its arrow - exactly one
           byte of drv_cfg changes, to 50h - and the panel closed, which
           writes SYSTEM.CFG:
           read back on the HOST, its 'EP' record is ver 1, one byte, 50h.
           Then a reset, and the driver must find the card at 2A0h and bind
           10.0.2.15 over it. That is the view, the file, OSAPI_DRV_CFG
           answering from DRVV_ATTACH and the driver using the answer.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88build                                            # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88ui                                               # noqa: E402
import xmcheck                                              # noqa: E402
import ethernet                                             # noqa: E402
import rdpreserve as rp                                     # noqa: E402
from rdxms import Machine, menu_pick                        # noqa: E402

S = xmcheck.sym
SND_ROW, ETH_ROW = 0, ethernet.ETH_ROW
DRVR_SZ, DRVR_SEG = ethernet.DRVR_SZ, ethernet.DRVR_SEG
DH_BOUND = ethernet.DH_BOUND

ARMS = {
    #           card    latch   tick Sound
    "mpu330":  (0x340, 0x330, False),
    "mpu300":  (0x340, 0x300, False),
    "sndtick": (0x300, None,  True),
    "pinned":  (0x2A0, None,  False),
}
PIN_2A0 = (0x2A0 - 0x200) // 2  # SPEC.md 51.9.2: (port - 200h) / 2
CP_IDRV = 2                     # kernel/ctrl.inc: the Drivers page's RECORD
CP_IX, CP_I0Y, CP_IROWH = 6, 6, 14  # ...the item list (tests/dispcp.py)
CPD_PBX1, CPD_PBY1 = 148, 2     # ...its heading row's Ports button
CPP_R0Y, CPP_BH = 24, 14        # ...and the Ports view: row 0's box
CPP_AX2 = 112 + 43 + 4 + 14 + 2     # CPP_BX1 + 43 + 4 + CP_DSW + 2: its down
DRVE_HW = 5                     # driver.inc: "No hardware found"
DRVR_ERR = 14

fails = []


def say(*a):
    print(*a)
    sys.stdout.flush()


def check(arm, ok, what):
    say("  %s %s: %s" % ("ok  " if ok else "FAIL", arm, what))
    if not ok:
        fails.append("%s: %s" % (arm, what))


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def writes(trace):
    """(offset, value) per register write the NIC took, in order."""
    out = []
    if not os.path.exists(trace):
        return out
    for line in open(trace):
        if "ne2000_ioport_write" not in line:
            continue
        f = dict(t.split("=") for t in line.split() if "=" in t)
        out.append((int(f["addr"], 16), int(f["val"], 16)))
    return out


def pin_session(q, arm, row_seg, sysimg):
    """The first boot: refused, the Ports view set, the panel closed, reset."""
    check(arm, row_seg(ETH_ROW) == 0, "with the card at 2A0h the sweep finds "
          "nothing and the driver is refused")
    err = q.read(S("drv_tab") + ETH_ROW * DRVR_SZ + DRVR_ERR, 1)[0]
    check(arm, err == DRVE_HW, "...and its row says no hardware (DRVE %d)"
          % err)
    ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
    menu_pick(q, ui, "Apple", "Control")
    box = {}

    def got():
        try:
            box["w"] = ui.window("Control Panel")
            return box["w"].visible
        except os88ui.UIError:
            return False
    q.need(got, "the Control Panel", 20)
    q.done(4.0)
    cp = box["w"]
    x0, y0 = cp.x + 1, cp.y + rp.TITLE_H
    hide = q.read(S("cp_hide"), 1)[0]
    row = sum(1 for r in range(CP_IDRV) if not (hide & (1 << r)))
    if q.read(S("cp_sel"), 1)[0] != CP_IDRV:    # (a refused boot driver may
        q.click(x0 + CP_IX + 30,                # have opened it there already)
                y0 + 1 + CP_I0Y + row * CP_IROWH + CP_IROWH // 2)
    q.need(lambda: q.read(S("cp_sel"), 1)[0] == CP_IDRV,
           "the Drivers page to be selected", 20)
    q.done(4.0)
    px, py = x0 + rp.CP_RX, y0              # the pane's corner
    q.click(px + CPD_PBX1 + 20, py + CPD_PBY1 + 8)  # Ports: on the RELEASE
    q.need(lambda: q.read(S("cp_dview"), 1)[0] == 1,
           "the Drivers page's Ports view", 20)
    q.done(4.0)
    cfg0 = q.read(S("drv_cfg"), 128)
    for _ in range(3):                      # Auto -> 240h -> 280h -> 2A0h
        q.click(px + CPP_AX2 + 7, py + CPP_R0Y + CPP_BH // 2)
    cfg1 = q.read(S("drv_cfg"), 128)
    moved = [(i, cfg0[i], cfg1[i]) for i in range(128) if cfg0[i] != cfg1[i]]
    check(arm, len(moved) == 1 and moved[0][2] == PIN_2A0,
          "three presses on the down arrow changed exactly one settings "
          "byte, to %02Xh (%s)" % (PIN_2A0, ", ".join(
              "+%d %02X->%02X" % m for m in moved) or "none"))
    shot = os.environ.get("NCX_SHOT")
    if shot:
        subprocess.run([sys.executable, "tools/shot.py", q.sock, shot],
                       check=False)
    q.click(*geom.close_xy(cp.x, cp.y))
    q.need(lambda: not any(w.visible and w.title == "Control Panel"
                           for w in ui.windows()), "the panel to close", 20)
    q.done(6.0)                             # ...and SYSTEM.CFG written
    raw = subprocess.run(["mtype", "-i", sysimg, "::SYSTEM.CFG"],
                         capture_output=True).stdout
    rec = cfg_record(raw, b"EP")
    check(arm, rec == (1, bytes([PIN_2A0])),
          "SYSTEM.CFG, read on the host, carries 'EP' ver 1 = %02Xh (%r)"
          % (PIN_2A0, rec))
    q.hmp("system_reset")
    time.sleep(1.0)


def cfg_record(raw, key):
    """(ver, data) of `key` in a SYSTEM.CFG (SPEC.md 51.5), or None."""
    if raw[:6] != b"O88CFG":
        return None
    i = 10
    while i + 4 <= len(raw):
        k = raw[i:i + 2]
        if k == b"\0\0":
            return None
        ver, ln = raw[i + 2], raw[i + 3]
        if k == key:
            return ver, raw[i + 4:i + 4 + ln]
        i += 4 + ln
    return None


def run_arm(arm, sysimg_src, apps_src, esyms):
    card, latch, tick = ARMS[arm]
    say("netcoexist: %s - card at %03Xh, MPU latch %s%s"
        % (arm, card, "at %03Xh" % latch if latch else "none",
           ", then Sound ticked" if tick else ""))
    work = tempfile.mkdtemp(prefix="ncx-")      # short: a socket path
    try:
        sysimg = os.path.join(work, "sys.img")  # WRITABLE copies: the guest
        apps = os.path.join(work, "b.img")      # writes SYSTEM.CFG on a tick
        shutil.copy(sysimg_src, sysimg)
        shutil.copy(apps_src, apps)
        trace = os.path.join(work, "nic.trace")
        mpu = os.path.join(work, "mpu.bin")
        extra = ["-netdev", "user,id=n0",
                 "-device", "ne2k_isa,netdev=n0,iobase=0x%x,irq=3" % card,
                 "-trace", "enable=ne2000_ioport_write", "-D", trace]
        if latch:
            extra += ["-chardev", "file,id=mpu,path=" + mpu,
                      "-device", "isa-debugcon,iobase=0x%x,chardev=mpu,"
                      "readback=0xfe" % latch]
        q = Machine(work, sysimg, apps, 16, extra)
        for _ in range(150):
            if os.path.exists(q.sock):
                break
            time.sleep(0.2)                 # the socket is a HOST event

        def row_seg(row):
            return u16(q.read(S("drv_tab") + row * DRVR_SZ + DRVR_SEG, 2))

        xmcheck.wait_desktop(q.sock, "netcoexist")
        if arm == "pinned":
            pin_session(q, arm, row_seg, sysimg)
            xmcheck.wait_desktop(q.sock, "netcoexist")
            q.wait(lambda: row_seg(ETH_ROW) != 0, "ETHER.DRV after the "
                   "reset", 30)
        seg = row_seg(ETH_ROW)
        check(arm, seg != 0, "ETHER.DRV attached")
        if not seg:
            return
        base = u16(q.readseg(seg, esyms["eth_base"], 2))
        mac = q.readseg(seg, esyms["eth_mac"], 6)
        check(arm, base == card, "the card was found at %03Xh, where it is "
              "(the driver says %03Xh, id %s)" % (card, base, mac.hex()))
        q.wait(lambda: q.readseg(seg, esyms["dhcp_st"], 1)[0] == DH_BOUND,
               "DHCP to bind", 30)
        ip = q.readseg(seg, esyms["eth_ip"], 4)
        check(arm, ethernet.dotted(ip) == "10.0.2.15",
              "DHCP bound 10.0.2.15 over the card (got %s)"
              % ethernet.dotted(ip))

        if tick:
            before = len(writes(trace))
            ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
            menu_pick(q, ui, "Apple", "Control")
            box = {}

            def got():
                try:
                    box["w"] = ui.window("Control Panel")
                    return box["w"].visible
                except os88ui.UIError:
                    return False
            q.need(got, "the Control Panel", 20)
            q.done(4.0)
            cp = box["w"]
            x0, y0 = cp.x + 1, cp.y + rp.TITLE_H
            q.click(x0 + 40, y0 + rp.CP_I0Y + rp.CP_IDRV * rp.CP_IROWH + 7)
            q.click(x0 + rp.CP_RX + 40,
                    y0 + rp.CP_DBY1 + SND_ROW * rp.CP_DROWH
                    + rp.CP_DROWH // 2)
            q.wait(lambda: row_seg(SND_ROW) != 0, "SOUND.DRV to load", 30)
            q.done(4.0)
            check(arm, row_seg(SND_ROW) != 0, "SOUND.DRV attached on the "
                  "tick (the probes ran)")
            late = writes(trace)[before:]
            hit = [w for w in late if w[0] == 1]
            check(arm, not hit, "nothing wrote the live card's base + 1 "
                  "after the tick (%d register writes since, %s)"
                  % (len(late), ", ".join("+1=%02X" % v for _, v in hit)
                     or "none at +1"))
            check(arm, row_seg(ETH_ROW) == seg, "ETHER.DRV still attached")

        if latch:
            got_b = open(mpu, "rb").read() if os.path.exists(mpu) else b""
            check(arm, not got_b, "not one byte written to the MPU's data "
                  "port (%d: %s)" % (len(got_b), got_b[:16].hex() or "-"))
        q.close()
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sys", default="build/ether360.img",
                    help="the system disk: SYSTEM.CFG wanting ETHER.DRV")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--arm", choices=sorted(ARMS), action="append")
    a = ap.parse_args()
    os.chdir(ROOT)
    for p in (a.sys, a.apps, "build/ether.bin"):
        if not os.path.exists(os88build.at(p)):
            sys.exit("netcoexist: no %s - run `make ethertest`" % p)
    esyms = ethernet.ether_syms()
    for arm in a.arm or ["mpu330", "mpu300", "sndtick", "pinned"]:
        run_arm(arm, os88build.at(a.sys), os88build.at(a.apps), esyms)
    for f in fails:
        say("  FAIL: " + f)
    say("netcoexist: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

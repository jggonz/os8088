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
"""
import argparse
import os
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88build                                            # noqa: E402
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
}

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
    for arm in a.arm or ["mpu330", "mpu300", "sndtick"]:
        run_arm(arm, os88build.at(a.sys), os88build.at(a.apps), esyms)
    for f in fails:
        say("  FAIL: " + f)
    say("netcoexist: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

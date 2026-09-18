#!/usr/bin/env python3
"""The DOS box's packet driver, over a real card (SPEC.md 96.23, 72.22).

    make && make ethertest && make dospkt && python3 tests/dospkt.py

**QEMU'S, AND ON THE CLOSED LIST FOR THE SAME REASON tests/ethernet.py IS**:
MartyPC has no network card of any kind, so the emulator this tree develops on
cannot host `ETHER.DRV` at all. Every assertion here is about BEHAVIOUR and
none is about speed.

WHAT IT ASSERTS, AND WHY IT READS THE DRIVER RATHER THAN THE SCREEN.

The box has no windowed text yet (wave 6), so a DOS program's output dies with
the bracket - and the text screen under the bracket is the desktop's own
framebuffer read as characters, which is unreadable whether or not the program
worked. So the evidence is `ETHER.DRV`'s own counters, read out of the running
driver the way tests/ethernet.py reads `dhcp_st`:

1. THE CLAIM WAS TAKEN AND GIVEN BACK. `eth_nrawtx`/`eth_nraw` are zeroed by
   NETV_RAW and survive the release, so a non-zero pair after the program has
   exited is proof the whole lifetime ran. `eth_raw` back at 0 is the release.

2. A FRAME WENT OUT. `eth_nrawtx` >= 1 - the probe's ARP request for the
   gateway, built from the station address the claim handed back.

3. FRAMES CAME BACK, THROUGH THE UP-CALL. `eth_nraw` >= 1. This is the half
   nothing else here can reach: the probe never polls the driver, so a frame
   it counted arrived because our INT 08h chain pulled it off the ring and
   called the client's receiver - which is what a Crynwr client expects and
   what SPEC.md 96.23.4 is about.

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1): take the DRVC_NET skip out of
drv_suspend_x (kernel/driver.inc) and rebuild - the driver is unloaded at the
bracket and every counter below stays 0.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))

import dispcp                                                 # noqa: E402
import ethernet as eth                                       # noqa: E402
import os88build                                             # noqa: E402
import os88qemu                                              # noqa: E402

SYS = "build/ether360.img"
PKT = "build/dospkt360.img"


def say(s):
    print(s, flush=True)


def fail(msg):
    say("dospkt: FAILED - " + msg)
    sys.exit(1)


def main():
    for p in (SYS, PKT):
        if not os.path.exists(os88build.at(p)):
            fail("%s is missing - run `make ethertest && make dospkt`" % p)

    syms = eth.ether_syms()
    for n in ("eth_raw", "eth_nraw", "eth_nrawtx", "eth_mac"):
        if n not in syms:
            fail("%s is not in the driver map - SPEC.md 72.22's verbs are the "
                 "thing under test and this build has not got them" % n)

    if os.path.exists("build/qemu.pid"):
        try:
            os.kill(int(open("build/qemu.pid").read().strip()), 15)
            time.sleep(1.0)
        except (OSError, ValueError):
            pass
    for f in ("build/qmp.sock", "build/qemu.pid"):
        if os.path.exists(f):
            os.remove(f)

    # `make test` DAEMONISES the emulator, so it outlives this script unless
    # somebody kills it - and the somebody is us (tests/os88qemu.py).
    os88qemu.own()
    r = subprocess.run(["make", "test", "ETHER=1", "TESTIMG=" + SYS,
                        "TESTAPPS=" + PKT], capture_output=True, text=True)
    if r.returncode:
        fail("make test failed:\n" + r.stdout + r.stderr)

    m = eth.Qemu()
    mouse = eth.Mouse()
    try:
        # --- the card, before anything is asked of it ----------------------
        seg = 0
        for _ in range(150):
            time.sleep(0.4)
            row = m.read(eth.S("drv_tab") + eth.ETH_ROW * eth.DRVR_SZ
                         + eth.DRVR_SEG, 2)
            seg = eth.u16(row)
            if seg:
                break
        if not seg:
            fail("ETHER.DRV never attached - no card was found, or SYSTEM.CFG "
                 "did not ask for it")
        say("dospkt: ETHER.DRV at %04X" % seg)

        def dw(name):
            return eth.u16(m.readseg(seg, syms[name], 2))

        def db(name):
            return m.readseg(seg, syms[name], 1)[0]

        mac = m.readseg(seg, syms["eth_mac"], 6)
        say("dospkt: station address %s" % ":".join("%02X" % b for b in mac))

        # --- run the probe --------------------------------------------------
        # **BY NAME, NEVER BY COORDINATE** (CLAUDE.md; tests/dispcp.py's
        # open_named). The first version clicked row 1 at a remembered y,
        # which is DOSPKT.COM only while it is the sole file on the disk -
        # MTCPDIR= puts DHCP.EXE in front of it alphabetically (SPEC.md 19.4
        # sorts by name), so the row launched the wrong program and reported
        # it as the packet driver never being found.
        dispcp.open_drive(m, mouse, eth.S, eth.settle, "B")
        wins = dispcp.win_list(m, eth.S)
        wx, wy = dispcp.win_rect(m, eth.S, wins[-1])[:2]
        dispcp.open_named(m, mouse, eth.S, eth.settle, wx, wy, "DOSPKT.COM")
        say("dospkt: launched; the probe holds the screen on int 16h")

        # it sends, then waits ~3s of BIOS ticks for the reply
        deadline = time.time() + 60.0
        nrx = ntx = 0
        while time.time() < deadline:
            time.sleep(1.0)
            ntx, nrx = dw("eth_nrawtx"), dw("eth_nraw")
            if ntx and nrx:
                break

        say("dospkt: raw frames out %d, in %d, claim held %d"
            % (ntx, nrx, db("eth_raw")))
        # The door-call count is what tells "the driver answered wrongly"
        # from "nobody is calling it", and the two are indistinguishable from
        # inside the guest. It is printed rather than asserted: the poll rate
        # is a property of the host's speed here, not of the box.
        say("dospkt: door calls %d, frames off the ring %d, dropped %d"
            % (dw("eth_ncall"), dw("eth_nrx"), dw("eth_ndrop")))

        # --- LET THE PROBE OUT, AND READ THE STATE RATHER THAN SLEEP ------
        # It holds the screen on int 16h, so Enter is what ends it - but what
        # happens next is a whole bracket teardown, and how long that takes is
        # the guest's business. A fixed sleep here was 3 seconds and passed
        # until the probe grew a second phase (SPEC.md 96.26.8's do_listen),
        # at which point Enter arrived while it was still waiting on a tick
        # and assertion 3 read a claim that was simply not released YET -
        # reported as `dos_pkt_shut did not run on that path`, about a path
        # that runs perfectly. So: poll the byte the assertion is about.
        m.hmp("sendkey ret")
        for _ in range(60):
            time.sleep(0.5)
            if not db("eth_raw"):
                break

        fails = []
        if not ntx:
            fails.append("no raw frame was ever transmitted: NETV_RAWTX was "
                         "never reached, or the packet driver was not found "
                         "at any vector in 60h..80h")
        if not nrx:
            fails.append("no raw frame was ever received: the INT 08h poll "
                         "never ran, the ring was never drained, or the "
                         "gateway never answered the ARP")
        held = db("eth_raw")
        if held:
            fails.append("the raw claim is STILL HELD after the program "
                         "exited - dos_pkt_shut did not run on that path, and "
                         "our own stack is switched off for the session")

        for f in fails:
            say("dospkt: " + f)
        say("dospkt: %s" % ("FAILED" if fails else "ok"))
        return 1 if fails else 0
    finally:
        m.quit()


if __name__ == "__main__":
    sys.exit(main())

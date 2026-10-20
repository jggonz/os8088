#!/usr/bin/env python3
"""A DOS packet-driver client over the PARALLEL CABLE (SPEC.md 96.26).

    make && make dospkt && python3 tests/doscable.py

**THE ARM THAT IS ABOUT THE WIRE.** `dosxlat` asks whether SPEC.md 96.26's
translation is a correct TCP endpoint, and forces it over a card because that
is the only wire an emulator here can drive fast. This asks the question that
one cannot: does the same translation work when what is underneath it really
is the cable it was written for - where `net_find` picks `NET.DRV` with no
knob at all, and where §96.23's raw path does not exist, because `NETV_RAW` is
one of the three verbs the cable refuses (§72.22.3).

WHAT IS REAL AND WHAT IS NOT, which is `tests/socktest.py`'s arrangement one
layer up. The GUEST is a cycle-accurate 4.77 MHz 8088 running the shipped
kernel, a real `NET.DRV`, the real DOS box and a real Crynwr client. The CABLE
is MartyPC's parallel port driven a nibble at a time by
`tests/lptlink/partner.py`. And the far side's TCP is not modelled at all -
`partner.SocketBox` is real host sockets.

**THE FAR SIDE REDIRECTS ONE ADDRESS, AND SAYS SO.** The probe dials
`10.0.2.2:8099` because that is where QEMU's slirp puts the host and the same
binary has to work on the card arm. Nothing routes there in a container, so
the `SocketBox` here records what it was asked for and connects to this
process's own listener instead. That is not a weaker assertion than a real
connect - it is a stronger one, because the recorded string is the dotted quad
the translation FORMATTED out of an IP header (§96.26.5), and a connect that
merely succeeded would not check it. It is also what the real far side does by
construction: `os88net.com` resolves and connects on our behalf.

FIVE ASSERTIONS.

0. THE ROUTE IS THE CABLE, with no knob. `[dos_pkt_xl]` is 1 and `[net_cls]`
   is `DRVC_FILE` - not `DRVC_NET`, which is the CARD (the header comment said
   "the parallel link" for a cycle and a route compare was written the wrong
   way round off it).
1. THE DRIVER SURVIVES THE BRACKET. `NET.DRV`'s `drv_tab` row still holds a
   segment while the DOS program is running, which is `drv_suspend_x`'s
   `DRVC_FILE` skip doing its job - without it the translation's very first
   verb reaches nothing.
2. THE VERB CROSSED THE WIRE. The far side was asked to open `10.0.2.2:8099`,
   which means `dn_tcp_open` built the address, `OSAPI_DRV_CALL` reached
   `NET.DRV`, and the command went down the cable and came back.
3. THE CLIENT GOT ITS HANDSHAKE. The probe's own record of every TCP segment
   its receiver was handed contains a `SYN|ACK` - so the connection came up,
   `dn_pump` saw `NSK_UP` over the cable, and the segment reached the client.
4. AND THE PAYLOAD, both ways: the far side saw the probe's `GET` and the
   probe counted the exact answer back.

**IT IS EXACT RATHER THAN FAST** - every nibble is debug-server round trips
with the emulator stepped in between - so the payload is measured in tens of
bytes, not a page, and this row lives in `soak`.
"""
import os
import socket
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "lptlink"))

import dispapps                                           # noqa: E402
import dispcp                                             # noqa: E402
import dosmap                                             # noqa: E402
import os88build                                          # noqa: E402
import os88marty                                          # noqa: E402
import os88mouse                                          # noqa: E402
import os88sym                                            # noqa: E402
import partner as P                                       # noqa: E402

S = os88sym.linear

SYS = "build/os8088-360.img"        # the STOCK system disk: it carries
                                    # NET.DRV and DOS.O88 in APPS/, and no
                                    # driver row is wanted by default
                                    # (SPEC.md 51.3), so the Control Panel
                                    # click below is both the request and the
                                    # moment the cable is first spoken to
PKT = "build/dospkt360.img"
MACHINE = "os8088_5150_cga_lpt"     # GLaBIOS on purpose: nothing here is a
                                    # timing question - the wire is
                                    # bit-banged and the BIOS never sees it
NET_ROW = 4                         # drv_tab row 4 is the parallel link
DRVR_SZ, DRVR_SEG, DRVR_CLASS = 16, 2, 12     # kernel/driver.inc's row -
                                              # and CLASS is 12, not 0, which
                                              # is DRVR_DISP and happened to
                                              # hold 12 as well
CP_I0Y, CP_IROWH, CP_IDRV = 6, 14, 2
CP_RX = 96

DST = "10.0.2.2"                    # what the probe dials, and what the far
DPORT = 8099                        # side below must be ASKED for

RESP = (b"HTTP/1.0 200 OK\r\n"
        b"Content-Length: 7\r\n"
        b"\r\n"
        b"cable\r\n")               # SMALL ON PURPOSE: every byte of this
                                    # crosses the cable a nibble at a time
                                    # with the emulator stepped between them

DRVC_FILE, DRVC_NET = 5, 4
F_SYN, F_PSH, F_RST = 0x02, 0x08, 0x04


def say(*a):
    print(*a)
    sys.stdout.flush()


class Redirect(P.SocketBox):
    """The far side, with one address redirected and RECORDED.

    See the module docstring: the recorded string is the assertion, and the
    redirect is what the real far end does anyway.
    """

    def __init__(self, port):
        P.SocketBox.__init__(self)
        self.port = port
        self.asked = []

    def open(self, host, port):
        self.asked.append((host, port))
        if host == DST and port == DPORT:
            return P.SocketBox.open(self, "127.0.0.1", self.port)
        return P.SocketBox.open(self, host, port)


class Inbound(threading.Thread):
    """The host end of an INBOUND connection: connect to the port the DOS
    program is listening on, and see whether it serves us (SPEC.md 96.26.8).

    **IT RETRIES**, because the port is not bound when this starts: the guest
    binds it inside `dn_lsn_init`, which runs at the second launch, which
    happens inside `Partner.serve` while this thread is asleep. A one-shot
    connect would race the emulator and lose every time.
    """

    REQ = b"GET / HTTP/1.0\r\n\r\n"

    def __init__(self, port, deadline=600.0):
        threading.Thread.__init__(self)
        self.daemon = True
        self.port, self.deadline = port, deadline
        self.got = None
        self.tries = 0
        self.err = None

    def run(self):
        t0 = time.time()
        while time.time() - t0 < self.deadline:
            s = socket.socket()
            s.settimeout(5.0)               # THE CONNECT's, so a refused port
            try:                            # comes back fast enough to retry
                self.tries += 1
                s.connect(("127.0.0.1", self.port))
            except OSError:
                s.close()
                time.sleep(0.25)
                continue
            # **AND THE READ's IS THE WHOLE DEADLINE**, which is the thing
            # that has to be said out loud: the answer is composed by a
            # cycle-accurate 8088 whose every wire nibble is a debug round
            # trip, so the round trip from our request to its reply is
            # MINUTES of host wall clock. At 20 seconds this timed out, closed
            # the socket, and the far side went NSK_CLOSING - so the box
            # closed the flow before the DOS program's reply was ever sent,
            # and the row failed with "the host got nothing back" about a
            # program that had answered perfectly well. docs/WRITING-TESTS.md
            # names this family: a timeout nobody measured against the guest.
            s.settimeout(max(30.0, self.deadline - (time.time() - t0)))
            out = b""
            try:
                s.sendall(self.REQ)
                while len(out) < 512:
                    b = s.recv(256)
                    if not b:
                        break
                    out += b
                    # **AND THE TAIL IS SHORT**, because the answer does not
                    # end in a close: the probe replies and then holds the
                    # screen on int 16h, so a loop waiting for EOF or 512
                    # bytes waits for the whole long timeout above after a
                    # 24-byte reply has already arrived - and the caller,
                    # which joins with a few seconds, reads `got` as None and
                    # reports that nothing came back. What we have after one
                    # segment IS the answer; this only collects a split one.
                    s.settimeout(5.0)
            except OSError as e:                # a tail timeout is the
                self.err = e                    # ORDINARY ending, so `out`
            finally:                            # is kept either way
                s.close()
            self.got = out
            return


def free_port():
    """A port nothing on this box holds, chosen HERE rather than fixed.

    The guest is told it through [dos_ebuf] and binds it on the far side, so
    a constant would make two runs of this row on one box collide - which is
    exactly what `--marty-jobs` does.
    """
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Listener(threading.Thread):
    """One connection, one fixed answer, the request line recorded."""

    def __init__(self):
        threading.Thread.__init__(self)
        self.daemon = True
        self.got = []
        self.s = socket.socket()
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("127.0.0.1", 0))
        self.port = self.s.getsockname()[1]
        self.s.listen(4)

    def run(self):
        while True:
            try:
                c, _ = self.s.accept()
            except OSError:
                return
            try:
                c.settimeout(120)
                req = b""
                while b"\r\n\r\n" not in req and len(req) < 4096:
                    b = c.recv(512)
                    if not b:
                        break
                    req += b
                self.got.append(req.split(b"\r\n")[0].decode("latin-1"))
                c.sendall(RESP)
            except OSError:
                pass
            finally:
                c.close()


def tree():
    """The far side's file volume: the link mounts one whether or not
    anything is going to browse it, so it has to be there."""
    t = P.FileTree()
    t.add(0, "READ.ME", content=b"a link volume\r\n")
    return t


def main():
    for p in (SYS, PKT):
        if not os.path.exists(os88build.at(p)):
            say("doscable: FAILED - %s is missing; run `make && make dospkt`"
                % p)
            return 1

    dm = dosmap.package()
    pm = dosmap.probe()
    # **THE MAPS ARE CHECKED BEFORE THE EMULATOR STARTS.** Every read below is
    # an offset out of one of them, and a name that has moved comes back as a
    # KeyError two hundred seconds into the row - or, worse, as a plausible
    # number if the read were guarded. dosxlat does the same, for the same
    # reason.
    for n in ("dos_ebuf", "dos_pkt_xl", "net_cls", "dos_ldpsp"):
        if n not in dm:
            say("doscable: FAILED - the DOS box has no %s; SPEC.md 96.20's "
                "environment page and 96.26's route are what this row reads"
                % n)
            return 1
    for n in ("flaglog", "nflag", "rxdata", "rxfirst", "narp", "gwmac",
              "isyn", "ldata", "lfirst"):
        if n not in pm:
            say("doscable: FAILED - the probe has no %s: "
                "tests/dostrap/dospkt.asm is not the one this row reads" % n)
            return 1
    fails = []
    lsn, seen2, port = [], [], 0       # the inbound run's, so a stall
    drop = pin = pend = 0              # below still reports rather than raises
    lis = Listener()
    lis.start()
    box = Redirect(lis.port)
    ft = tree()
    say("doscable: far side on 127.0.0.1:%d, %d bytes of answer"
        % (lis.port, len(RESP)))

    with os88marty.launch(SYS, apps=PKT, machine=MACHINE) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        p = P.Partner(m)

        # --- the B: window first, while the wire is quiet -----------------
        # Opening it is a floppy mount and a listing, both of which are many
        # millions of guest cycles: doing it before the cable is up means the
        # partner never has to step the guest through it.
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        bx, by = dispcp.win_rect(m, S, wins[-1])[:2]

        # --- mount NET.DRV, with the far end answering the handshake -------
        # A PAUSED RELEASE: since SPEC.md 13.8.3 a panel control arms on the
        # press and acts on the release, so it is the release that runs
        # net_connect - and anything that lets the guest run after it spends
        # the handshake into a wire nobody is holding.
        mo.menu(8, 8, 8, 40)                    # chip menu -> Control Panel
        os88marty.settle(m)
        cp = None
        for w in dispcp.win_list(m, S):
            x, y, ww, hh = dispcp.win_rect(m, S, w)
            if ww >= 280 and hh >= 100:
                cp = (x, y)
        if cp is None:
            say("doscable: FAILED - no Control Panel window")
            return 1
        cx, cy = cp
        x0, y0 = cx + 1, cy + 18
        mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)
        os88marty.settle(m)

        # ROW 4 IS BELOW THE FOLD (SPEC.md 31.1.1): the pane shows four and
        # os88net is the fifth, so the list is scrolled to it and what is
        # clicked is its VISIBLE index.
        vis = dispcp.drv_show(mo, x0, y0, NET_ROW,
                              lambda: os88marty.settle(m))
        mo.to(x0 + CP_RX + 40, dispcp.drv_row_y(y0, vis))
        m.pause()
        p.sync()
        p.click_paused_release()
        p.hello(P.NET_VER_SOCK)                 # the version byte IS the
                                                # socket probe (SPEC.md 62.11)
        p.allow(60000000)
        seen = p.serve(ft, limit=24, idle=8000000, sox=box)
        say("doscable: connect handshake served: %r" % "".join(seen))
        row = m.read(S("drv_tab") + NET_ROW * DRVR_SZ, DRVR_SZ)
        seg = row[DRVR_SEG] | (row[DRVR_SEG + 1] << 8)
        cls = row[DRVR_CLASS]
        say("doscable: NET.DRV at %04X, class %d" % (seg, cls))
        if not seg:
            say("doscable: FAILED - the link driver did not load")
            return 1

        # Close the panel and let the guest run: the wire is idle between
        # commands and NET.DRV has no worker, so nothing touches it here.
        m.run()
        mo.click(cx + 8, cy + 9)
        os88marty.settle(m)

        # --- and now the DOS program ---------------------------------------
        # **THE DOUBLE-CLICK IS RUN AND THE PAUSE IS IMMEDIATE.** A package
        # launch cannot be a paused click - the two presses have to be inside
        # the kernel's double-click window, which needs guest cycles between
        # them - so instead the machine is stopped the instant it has been
        # asked, and the partner steps it through the whole launch from there.
        # Nothing between the click and the pause touches the wire: the box's
        # own net_find and dn_init are a driver-table lookup and a memset.
        row = dispcp.row_of(m, S, "DOSPKT.COM")     # RAISES on a miss, so a
        rx, ry = dispcp.row_xy(bx, by, row)         # click can never land on
        say("doscable: DOSPKT.COM is row %d at (%d,%d)" % (row, rx, ry))
        mo.dblclick(rx, ry, settle=0.0)             # whatever sorted there
        m.pause()
        p.sync()
        p.allow(2000000000)                     # a LIFETIME ceiling, re-anchored
                                                # for this phase; see allow()

        # --- THE LAUNCH IS STEPPED COARSELY, because nothing in it is on the
        # wire. A floppy mount, DOS.O88, the bracket and DOSPKT.COM are tens
        # of millions of guest cycles, and `_await_strobe`'s 400-cycle step
        # would spend tens of thousands of debug round trips watching a line
        # the guest is not driving. idle_until_wire stops the moment it is.
        touched = p.idle_until_wire(300000000)
        say("doscable: the launch cost %d step(s); the guest %s the port"
            % (p.spent, "touched" if touched else "has NOT touched"))
        try:
            seen = p.serve(ft, limit=600, idle=8000000, sox=box)
            say("doscable: served: %r" % "".join(seen))
        except P.LinkTimeout as e:
            say("doscable: the wire STALLED: %s" % e)
            say("   partner saw: %r" % "".join(p.log))
            fails.append("the wire stalled: %s" % e)
        for line in box.log:
            say("   " + line)

        m.run()
        os88marty.settle(m)

        # --- read the box and the probe out of the guest --------------------
        g = dispapps.pkg_seg(m, 0)
        pseg = g[1] if g else None
        if pseg is None:
            say("doscable: FAILED - no DOS window to read")
            return 1
        xl = m.read((pseg << 4) + dm["dos_pkt_xl"], 1)[0]
        ncls = m.read((pseg << 4) + dm["net_cls"], 1)[0]
        psp = int.from_bytes(m.read((pseg << 4) + dm["dos_ldpsp"], 2),
                             "little")
        log = b""
        nf = rxdata = rxfirst = narp = 0
        if psp:
            nf = int.from_bytes(m.read((psp << 4) + pm["nflag"], 2), "little")
            log = m.read((psp << 4) + pm["flaglog"], 8)[:nf]
            rxdata = int.from_bytes(m.read((psp << 4) + pm["rxdata"], 2),
                                    "little")
            rxfirst = int.from_bytes(m.read((psp << 4) + pm["rxfirst"], 2),
                                     "little")
            narp = int.from_bytes(m.read((psp << 4) + pm["narp"], 2), "little")
        say("doscable: [dos_pkt_xl]=%d [net_cls]=%d psp=%04X"
            % (xl, ncls, psp))
        say("doscable: FLAGS %s" % " ".join("%02X" % b for b in log))
        say("doscable: ARP %d, data %d, first %04X" % (narp, rxdata, rxfirst))
        say("doscable: the far side was asked for %r" % (box.asked,))
        say("doscable: ...and saw %r" % (lis.got,))

        # =====================================================================
        # THE OTHER DIRECTION (SPEC.md 96.26.8)
        # =====================================================================
        # A listening client sends NOTHING a packet driver could be read for,
        # so the box is told which port to forward through the environment
        # page. **IT IS POKED AND NOT TYPED**, deliberately: tests/dosargs.py
        # already asserts that a row typed on that page reaches the program's
        # environment block verbatim (SPEC.md 96.20), so typing it again here
        # would re-test the UI at the cost of a third of this row's runtime -
        # and what is under test is dn_lsn_init reading it, not the field.
        #
        # The SECOND run is what picks it up: dn_lsn_init runs at the launch,
        # so the environment has to be in place before one, and the first
        # launch is over.
        port = free_port()
        row = ("OS88LISTEN=%d" % port).encode("ascii") + b"\0"
        m.key("Enter")                          # let the probe off int 16h
        os88marty.settle(m)
        m.write((pseg << 4) + dm["dos_ebuf"], row)
        back = m.read((pseg << 4) + dm["dos_ebuf"], len(row))
        say("doscable: [dos_ebuf] is now %r, port %d"
            % (back.split(b"\0")[0].decode("latin-1"), port))
        inb = Inbound(port)
        inb.start()

        # --- ...AND ENTER RUNS IT AGAIN, which is the whole point ----------
        # The Enter above only got the probe off its int 16h; this one is the
        # re-launch, and it is tests/dosargs.py's own sequence (run, come back
        # to the window, change something, Enter). The first version of this
        # row had the comment and not the keystroke, so the second run never
        # happened and `served: ''` was the only sign.
        #
        # **THE PAUSE IS THE VERY NEXT CALL**, with no settle between: on this
        # launch the first wire command is dn_lsn_init's NETV_LISTEN, which
        # dos_pkt_bufs reaches in the bracket's own setup - far earlier than
        # the outbound SYN, which came after a whole program load. A settle
        # here would hand the guest seconds and the listen would go into a
        # cable nobody is holding.
        m.key("Enter")
        m.pause()
        p.sync()
        p.allow(4000000000)
        p.idle_until_wire(300000000)
        try:
            seen2 = p.serve(ft, limit=900, idle=8000000, sox=box)
            say("doscable: second run served: %r" % "".join(seen2))
        except P.LinkTimeout as e:
            say("doscable: the wire STALLED on the inbound run: %s" % e)
            fails.append("the wire stalled on the inbound run: %s" % e)
        for line in box.log[len(box.log) - 40:]:
            say("   " + line)
        m.run()
        os88marty.settle(m)

        psp2 = int.from_bytes(m.read((pseg << 4) + dm["dos_ldpsp"], 2),
                              "little")
        isyn = ldata = lfirst = 0
        if psp2:
            isyn = int.from_bytes(m.read((psp2 << 4) + pm["isyn"], 2), "little")
            ldata = int.from_bytes(m.read((psp2 << 4) + pm["ldata"], 2),
                                   "little")
            lfirst = int.from_bytes(m.read((psp2 << 4) + pm["lfirst"], 2),
                                    "little")
        inb.join(timeout=30.0)      # the tail timeout above, with room
        say("doscable: LSN %d, LDATA %d, LFIRST %04X (psp %04X)"
            % (isyn, ldata, lfirst, psp2))
        say("doscable: the host connected %d time(s) and was served %r%s"
            % (inb.tries, inb.got,
               "" if inb.err is None else " (%r)" % (inb.err,)))

        # --- AND THE BOX'S OWN SIDE OF IT, which is what separates the two
        # ways this can fail. `LSN 0` alone cannot say whether dn_lsn_init
        # never parsed the environment row, or parsed it and had NETV_LISTEN
        # refused, or took a handle and never saw a connection - and those are
        # three different bugs in three different files. The rows live in the
        # network CLAIM (SPEC.md 96.26.6), so they are read through
        # [dos_pkt_bseg] with the offsets dosmap now takes off nasm's absolute
        # equates rather than a test transcribing them.
        bseg = int.from_bytes(m.read((pseg << 4) + dm["dos_pkt_bseg"], 2),
                              "little")
        if bseg:
            raw = m.read((bseg << 4) + dm["dn_lsn"],
                         dm["DN_NLSN"] * dm["DN_L_SIZE"])
            for i in range(dm["DN_NLSN"]):
                r = raw[i * dm["DN_L_SIZE"]:(i + 1) * dm["DN_L_SIZE"]]
                if r[dm["DN_L_USED"]]:
                    lsn.append((r[dm["DN_L_HAND"]],
                                int.from_bytes(r[dm["DN_L_PORT"]:
                                                 dm["DN_L_PORT"] + 2],
                                               "little")))
        say("doscable: [dos_pkt_bseg]=%04X, listeners (handle, port) = %r"
            % (bseg, lsn))

        # ...AND WHAT THE DRIVER COUNTED, which is the third way this can
        # fail and the one neither reading above can see: a frame the box
        # STAGED and the client REFUSED. dos_pkt_deliver's two-call up-call
        # asks the client for a buffer and a 0:0 answer is a legal reply that
        # costs the frame - `.dropped` is the same arm as "nobody registered
        # for this type", and get_statistics has a field for it (§96.23.3).
        drop = pin = 0
        if pseg:
            drop = int.from_bytes(
                m.read((pseg << 4) + dm["dos_pkt_stats"] + dm["PKS_DROP"], 4),
                "little")
            pin = int.from_bytes(
                m.read((pseg << 4) + dm["dos_pkt_stats"] + dm["PKS_PIN"], 4),
                "little")
        say("doscable: the driver counted %d frame(s) in and %d dropped"
            % (pin, drop))

        # ...AND WHETHER A FRAME IS STILL STAGED, which is the fourth outcome
        # and the worst-behaved: dn_pump returns early while [dn_pend] is set
        # (a staged SYN IS that pump's frame), so a frame the poll never comes
        # to collect freezes EVERY flow, not only the inbound one.
        pend, praw, pbusy, flows = 0, 0, 0, []
        if bseg:
            pend = int.from_bytes(m.read((bseg << 4) + dm["dn_pend"], 2),
                                  "little")
            raw = m.read((bseg << 4) + dm["dn_flows"],
                         dm["DN_NFLOW"] * dm["DN_F_SIZE"])
            for i in range(dm["DN_NFLOW"]):
                r = raw[i * dm["DN_F_SIZE"]:(i + 1) * dm["DN_F_SIZE"]]
                if r[dm["DN_F_USED"]]:
                    flows.append((r[dm["DN_F_HAND"]], r[dm["DN_F_ST"]]))
        if pseg:
            praw = m.read((pseg << 4) + dm["dos_pkt_raw"], 1)[0]
            pbusy = m.read((pseg << 4) + dm["dos_pkt_busy"], 1)[0]
        say("doscable: [dn_pend]=%d [dos_pkt_raw]=%d [dos_pkt_busy]=%d "
            "flows (handle, state) = %r" % (pend, praw, pbusy, flows))

    # --- 0: the route ------------------------------------------------------
    if not xl:
        fails.append("[dos_pkt_xl] is 0: the box took the CARD path on a "
                     "machine with no card, so net_find or dos_pkt_bufs' "
                     "route compare is wrong")
    if ncls != DRVC_FILE:
        fails.append("[net_cls] is %d and the cable is DRVC_FILE (%d); "
                     "DRVC_NET (%d) is the CARD" % (ncls, DRVC_FILE, DRVC_NET))
    # --- 1: the driver survived the bracket --------------------------------
    if not seg:
        fails.append("NET.DRV's row is empty: drv_suspend_x unloaded it at "
                     "the bracket, so the translation had nothing to call")
    # --- 2: the verb crossed the wire --------------------------------------
    if (DST, DPORT) not in box.asked:
        fails.append("the far side was never asked for %s:%d - it saw %r, so "
                     "either NETV_OPEN never reached NET.DRV or the address "
                     "dn_tcp_open formatted is not the one in the IP header"
                     % (DST, DPORT, box.asked))
    # --- 3: the client got its handshake -----------------------------------
    if not narp:
        fails.append("no ARP reply reached the client, so it never addressed "
                     "an IP frame to us at all")
    if not any(b & F_SYN for b in log):
        fails.append("no SYN|ACK: the connection did not come up over the "
                     "cable, or dn_pump never saw NSK_UP")
    if any(b & F_RST for b in log):
        fails.append("a RESET reached the client (flags %s)"
                     % " ".join("%02X" % b for b in log))
    # --- 4: and the payload, both ways -------------------------------------
    if not lis.got:
        fails.append("the request never reached the far side's listener")
    elif not lis.got[0].startswith("GET / HTTP"):
        fails.append("the far side was sent %r rather than the probe's "
                     "request" % lis.got[0])
    if rxdata != len(RESP):
        fails.append("the client was handed %d payload bytes and the far "
                     "side wrote %d" % (rxdata, len(RESP)))
    if rxfirst != (RESP[0] << 8 | RESP[1]):
        fails.append("the payload starts %04X and the answer starts %r"
                     % (rxfirst, RESP[:2]))

    # --- 5: the INBOUND direction (SPEC.md 96.26.8) ------------------------
    # THE LISTENER ROW IS CHECKED FIRST, because it splits the failure: no row
    # means dn_lsn_init never read the environment row or NETV_LISTEN was
    # refused, and a row with nothing arriving on it means the far end never
    # accepted. Without it `LSN 0` is one message for three bugs in three
    # different files.
    if not lsn:
        fails.append("the box took NO listener: dn_lsn_init did not find "
                     "'OS88LISTEN=%d' in [dos_ebuf], or NETV_LISTEN was "
                     "refused by NET.DRV - the wire served %r, and an 'l' is "
                     "what a listen looks like" % (port, "".join(seen2)))
    elif port not in [q for _, q in lsn]:
        fails.append("the box is listening on %r and the host connected to "
                     "%d" % (lsn, port))
    if not isyn:
        fails.append("no inbound SYN reached the client (%d frame(s) in, %d "
                     "dropped, [dn_pend]=%d): a DROP means the box staged the "
                     "SYN and the client refused the buffer, a PEND means it "
                     "staged one the poll never collected - and neither means "
                     "dn_accept never staged it at all"
                     % (pin, drop, pend))
    if ldata != len(Inbound.REQ):
        fails.append("the DOS program was handed %d request bytes and the "
                     "host sent %d - so the handshake completed and the DATA "
                     "did not, which is dn_synack_in's ACK"
                     % (ldata, len(Inbound.REQ)))
    if lfirst != (Inbound.REQ[0] << 8 | Inbound.REQ[1]):
        fails.append("the request starts %04X and the host sent %r"
                     % (lfirst, Inbound.REQ[:2]))
    if not inb.got:
        fails.append("the host got nothing back from the DOS program: it "
                     "connected %d time(s)" % inb.tries)
    elif not inb.got.startswith(b"HTTP/1.0 200 OK"):
        fails.append("the host was served %r rather than the probe's reply"
                     % inb.got[:40])

    for f in fails:
        say("doscable: " + f)
    say("doscable: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

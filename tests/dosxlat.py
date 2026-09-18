#!/usr/bin/env python3
"""The DOS box's CABLE TRANSLATION, one whole TCP connection (SPEC.md 96.26).

    python3 tests/dosxlat.py

**QEMU'S**, on CLAUDE.md's closed list for tests/ethernet.py's reason: the
socket verbs underneath the translation are `ETHER.DRV`'s, and MartyPC has no
network card of any kind.

WHAT IT ASSERTS, AND WHY THE PROBE IS THE INSTRUMENT.

§96.26's translation is a TCP endpoint: the DOS client's frames are terminated
here and re-opened as a socket, so the question is not "did a frame go out" -
that is `dospkt`'s question, over a card - but "does a client's own stack get
what TCP owes it". Nothing outside the client can answer that, because every
counter in the box and the driver reads correct while the client hears nothing:
that is exactly how three register defects hid (SPEC.md 96.26.3).

So DOSPKT.COM runs one connection by hand and BANKS WHAT IT HEARD -
`flaglog` is the flags byte of every TCP segment its receiver was handed, in
order - and this row reads those bytes out of the running program. The
handshake, the data and the close are then three assertions about one array:

    FLAGS 12 10 10 18 11
           |  |     |  |
           |  |     |  +-- FIN|ACK: the far side closed and we said so
           |  |     +----- PSH|ACK carrying the whole HTTP response
           |  +----------- ACK
           +-------------- SYN|ACK, sent when NETV_STATUS reports NSK_UP

THE FAR SIDE IS THIS PROCESS. A listener on 10.0.2.2:8099 - QEMU's slirp puts
the host there - writes a FIXED response, so the byte count is one the test
chose rather than one it has to trust. `http.server` would not do: its
`Server:` header carries the Python version, so the length changes with the
interpreter and the assertion would be a different number on every box.

**DOSNETCARD=1, IN A PRIVATE TREE.** `net_find` prefers the card and §96.23's
raw path is strictly better there, so on any machine an emulator here can host
the translation would never run at all. The knob forces it; the tree keeps it
out of `build/`, where a stock `make` would silently put the other arm of the
package on the disk and the row would test the card path while reporting on
this one (that happened, and both readings were correct for the build actually
on the floppy).

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1): put `dn_pump`'s loop counter
back in `CL` (apps/dos/dosnet.inc) - `NETV_STATUS` answers in `CX`, the tick
handler then takes longer than a tick, and FLAGS comes back empty. Or take the
`.unknown` arm out of `dn_tcp_in` and the log gains a trailing `14`.
"""
import os
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))

import dispcp                                                 # noqa: E402
import dispapps                                               # noqa: E402
import dosmap                                                 # noqa: E402
import ethernet as eth                                        # noqa: E402
import os88build                                              # noqa: E402
import os88qemu                                               # noqa: E402

PORT = 8099
RESP = (b"HTTP/1.0 200 OK\r\n"
        b"Content-Type: text/plain\r\n"
        b"Content-Length: 30\r\n"
        b"\r\n"
        b"os8088 dos cable translation\r\n")

F_FIN, F_SYN, F_RST, F_PSH, F_ACK = 0x01, 0x02, 0x04, 0x08, 0x10


def say(s):
    print(s, flush=True)


class Listener(object):
    """One connection, one fixed answer, and the request line recorded.

    A raw socket rather than http.server: the body has to be a length this
    test can assert, and a `Server:` header naming the interpreter is not one.
    """

    def __init__(self):
        self.got = []
        self.s = socket.socket()
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("0.0.0.0", PORT))
        self.s.listen(4)
        self.t = threading.Thread(target=self._run)
        self.t.daemon = True
        self.t.start()

    def _run(self):
        while True:
            try:
                c, _ = self.s.accept()
            except OSError:
                return
            try:
                c.settimeout(10.0)
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
                c.close()               # HTTP/1.0: the close is the EOF the
                                        # translation turns into a FIN

    def stop(self):
        try:
            self.s.close()
        except OSError:
            pass


def main():
    t = os88build.tree("DOSNETCARD=1",
                       targets=("ether360.img", "dospkt360.img")).apply()
    sysimg, pkt = t.img("ether360.img"), t.img("dospkt360.img")
    for p in (sysimg, pkt):
        if not os.path.exists(p):
            say("dosxlat: FAILED - %s was not built" % p)
            return 1

    syms = eth.ether_syms()
    dm = dosmap.package("DOSNET_CARD")
    pm = dosmap.probe()
    for n in ("flaglog", "nflag", "rxdata", "rxfirst", "narp", "gwmac",
              "statbuf"):
        if n not in pm:
            say("dosxlat: FAILED - the probe has no %s: "
                "tests/dostrap/dospkt.asm is not the one this row reads" % n)
            return 1

    lis = Listener()
    say("dosxlat: listening on %d, %d bytes of answer" % (PORT, len(RESP)))
    os88qemu.kill()
    os88qemu.own()
    r = subprocess.run(["make", "test", "ETHER=1", "TESTIMG=" + sysimg,
                        "TESTAPPS=" + pkt], capture_output=True, text=True)
    if r.returncode:
        say("dosxlat: FAILED - make test:\n" + r.stdout + r.stderr)
        return 1

    m = eth.Qemu()
    mouse = eth.Mouse()
    try:
        seg = 0
        for _ in range(150):
            time.sleep(0.4)
            seg = eth.u16(m.read(eth.S("drv_tab") + eth.ETH_ROW * eth.DRVR_SZ
                                 + eth.DRVR_SEG, 2))
            if seg:
                break
        if not seg:
            say("dosxlat: FAILED - ETHER.DRV never attached; no card was "
                "found, or SYSTEM.CFG did not ask for it")
            return 1
        say("dosxlat: ETHER.DRV at %04X" % seg)

        # BY NAME, NEVER BY COORDINATE (CLAUDE.md; tests/dispcp.py)
        dispcp.open_drive(m, mouse, eth.S, eth.settle, "B")
        wins = dispcp.win_list(m, eth.S)
        wx, wy = dispcp.win_rect(m, eth.S, wins[-1])[:2]
        dispcp.open_named(m, mouse, eth.S, eth.settle, wx, wy, "DOSPKT.COM")

        pseg = None
        for _ in range(40):
            time.sleep(0.5)
            g = dispapps.pkg_seg(m, 0)
            if g:
                pseg = g[1]
                break
        if pseg is None:
            say("dosxlat: FAILED - no package window: DOS.O88 never launched")
            return 1

        # --- THE ROUTE, before anything is concluded from the silence -----
        # [dos_pkt_xl] is the one byte that says which of §96.26.1's two paths
        # the package took. Without it a row on a stock build asserts the
        # translation and reads the CARD's behaviour, which is the failure the
        # private tree exists to prevent - checked rather than assumed.
        xl = m.read((pseg << 4) + dm["dos_pkt_xl"], 1)[0]
        if not xl:
            say("dosxlat: FAILED - [dos_pkt_xl] is 0: this package took the "
                "CARD path, so DOSNET_CARD was not compiled in")
            return 1

        # A .COM: CS = the PSP and the probe is `org 100h`, so its map value
        # IS the offset from the PSP.
        psp = 0
        for _ in range(20):
            time.sleep(0.5)
            psp = eth.u16(m.read((pseg << 4) + dm["dos_ldpsp"], 2))
            if psp:
                break
        if not psp:
            say("dosxlat: FAILED - [dos_ldpsp] is 0: no program was loaded")
            return 1

        def pw(n):
            return eth.u16(m.read((psp << 4) + pm[n], 2))

        # The whole exchange is one tick-paced connection; ~30s is generous
        # against the probe's own 5s and 8s waits.
        log = b""
        for _ in range(60):
            time.sleep(1.0)
            n = pw("nflag")
            log = m.read((psp << 4) + pm["flaglog"], 8)[:n]
            if any(b & F_FIN for b in log):
                break                   # the close is the last thing that
                                        # happens, so waiting for anything
                                        # else would be a fixed sleep
        gw = m.read((psp << 4) + pm["gwmac"], 6)
        st = m.read((psp << 4) + pm["statbuf"], 24)
        stat = [int.from_bytes(st[i * 4:i * 4 + 4], "little") for i in range(6)]
        say("dosxlat: FLAGS %s" % " ".join("%02X" % b for b in log))
        say("dosxlat: ARP %d, gateway %s, data %d, first %04X"
            % (pw("narp"), ":".join("%02X" % b for b in gw),
               pw("rxdata"), pw("rxfirst")))
        say("dosxlat: the far side saw %r" % (lis.got,))
        say("dosxlat: STATS in %d/%dB, out %d/%dB, errin %d, dropped %d"
            % (stat[0], stat[2], stat[1], stat[3], stat[4], stat[5]))

        fails = []
        if not pw("narp"):
            fails.append("no ARP reply reached the client: dn_arp answered "
                         "nothing, or the up-call never ran")
        if all(b == 0 for b in gw):
            fails.append("the client learnt no gateway address, so it never "
                         "addressed a frame to us at all")
        if not any(b & F_SYN for b in log):
            fails.append("no SYN|ACK: the client's SYN was not turned into a "
                         "connection, or dn_pump never saw NSK_UP")
        if not any(b & F_PSH for b in log):
            fails.append("no segment carried data back to the client")
        if not any(b & F_FIN for b in log):
            fails.append("the connection was never closed: the far side's EOF "
                         "did not become a FIN")
        if any(b & F_RST for b in log):
            fails.append("a RESET reached the client (flags %s) - a transfer "
                         "that completed must not end in one"
                         % " ".join("%02X" % b for b in log))
        if not lis.got:
            fails.append("the request never reached the far side: nothing "
                         "was accepted on port %d" % PORT)
        elif not lis.got[0].startswith("GET / HTTP"):
            fails.append("the far side was sent %r rather than the probe's "
                         "request" % lis.got[0])
        if pw("rxdata") != len(RESP):
            fails.append("the client was handed %d payload bytes and the far "
                         "side wrote %d" % (pw("rxdata"), len(RESP)))
        if pw("rxfirst") != (RESP[0] << 8 | RESP[1]):
            fails.append("the payload starts %04X and the answer starts %r"
                         % (pw("rxfirst"), RESP[:2]))
        # --- and get_statistics' own record (SPEC.md 96.23.3) -------------
        # LOOSE ON PURPOSE: the exact counts depend on how many segments the
        # exchange took, which is the far side's business. What is asserted is
        # that the record is KEPT - it carried two diagnostic counters written
        # over bytes_out and errors_in for a cycle, and a client reading that
        # was told nonsense by a published call.
        if stat[1] < 3:
            fails.append("get_statistics counted %d packets out and the probe "
                         "sent at least 4 (SYN, ARP, ACK, the request)"
                         % stat[1])
        if stat[0] < len(log):
            fails.append("get_statistics counted %d packets in and the client "
                         "was handed %d TCP segments alone" % (stat[0], len(log)))
        if stat[3] < stat[1] * 40:
            fails.append("bytes out is %d for %d packets, which is less than "
                         "one IP header each - the field is not being kept"
                         % (stat[3], stat[1]))
        if stat[4]:
            fails.append("errors_in is %d: nothing in the box can report one, "
                         "so the field is being written by something else"
                         % stat[4])

        for f in fails:
            say("dosxlat: " + f)
        say("dosxlat: %s" % ("FAILED" if fails else "ok"))
        return 1 if fails else 0
    finally:
        lis.stop()
        m.quit()


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Fetch a page over the cable (SPEC.md 62.11, docs/NET-STACK-PLAN.md stage B).

    make && make socktest && python3 tests/socktest.py [--adapter cga]

WHAT IS REAL HERE AND WHAT IS NOT, because the answer is better than it looks.
The GUEST is a cycle-accurate 4.77 MHz 8088 running the shipped kernel and a
real NET.DRV. The CABLE is MartyPC's parallel port driven a nibble at a time
by tests/lptlink/partner.py, exact rather than fast. And the FAR SIDE'S TCP is
not modelled at all - partner.py's SocketBox is real host sockets, so the page
this test checks really did cross one, out of an ordinary HTTP server started
by this file on 127.0.0.1.

What that does NOT cover is the wire's verdict - timings, levels, a real
cable - which is still the 5150's question (SPEC.md 62.10.3).

FIVE ASSERTIONS, and the third and fourth are the ones with teeth.

0. IT REACHED THE RIGHT DRIVER. NETV_IDENT answers 'NL' - the package asks
   before anything else, because BH is a CLASS and RAMDISK.DRV is DRVC_FILE
   too, so `open a socket` and `upper-case this string` are the same number at
   the same address (SPEC.md 20.11.1). The state machine reports NETE_VERB and
   stops if the signature is wrong, so a failure here shows up as `FAILED`
   rather than as nonsense.

1. THE LINK COMES UP AND THE SOCKET VERBS ARE THERE. NETV_STATE answers
   NSTF_PORT | NSTF_LINK | NSTF_SOCK, the last from the handshake's version
   byte alone.

2. THE PAGE ARRIVES WHOLE AND CORRECT. Not "some bytes came back": the exact
   length the server sent, and the reply's first bytes read `HTTP/1.0_200_OK`.
   A transport that dropped or duplicated a nibble would still deliver
   something plausible.

3. A ZERO-LENGTH RECV IS NOT THE END. The server is deliberately made to send
   the body in TWO writes with a pause between them, so the guest is
   guaranteed at least one NETV_RECV that delivers nothing while the socket is
   still NSK_UP. A package that read that as end-of-stream would report a
   short page here rather than in the field.

4. THE HANDLE IS GIVEN BACK. After NETV_CLOSE, NETV_STATE reports all
   NET_SOCKS free again.
"""
import argparse
import os
import socket
import sys
import threading
import time

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
sys.path.insert(0, "/home/user/os8088/tests/lptlink")
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
import partner as P                                    # noqa: E402

S = os88sym.linear

MACHINE = "os8088_5150_cga_lpt"     # GLaBIOS on purpose: nothing here is a
                                    # timing question - the wire is bit-banged
                                    # and the BIOS never sees it
NET_ROW = 4                         # drv_tab row 4 is the parallel link,
                                    # last since the page was reordered (31.1)
DRVR_SZ, DRVR_SEG = 16, 2
CP_I0Y, CP_IROWH, CP_IDRV = 6, 14, 2
CP_RX, CP_DBY1, CP_DROWH = 96, 20, 26
PORT = 8088                         # ...what socktest.asm's SK_PORT names

BODY = (b"<html><head><title>os8088</title></head>\r\n"
        b"<body><h1>Fetched over a parallel cable</h1>\r\n"
        b"<p>Every byte of this crossed a LapLink nibble at a time.</p>\r\n"
        b"</body></html>\r\n")


def say(*a):
    print(*a)
    sys.stdout.flush()


class Server(threading.Thread):
    """An HTTP server of exactly one page, in TWO WRITES.

    The split is the whole reason this is not http.server: assertion 3 needs a
    moment when the socket is connected, the peer has not closed, and there is
    nothing to read - which is what makes a zero-length NETV_RECV happen at
    all. A server that wrote the page in one go might never produce one, and
    the test would pass without ever checking the thing it is named for.

    **AND THE GAP IS GATED ON THE BOX, NOT ON A CLOCK.** The first version
    slept 1.5 s between the two writes and the guest never saw an empty read:
    under a stepped partner one wire exchange costs SECONDS of host time, so
    the body was always there before the guest got round to asking. A
    time-based gap in a harness whose two ends run on different clocks is a
    condition you hope for, and this one is waited for - the body goes out
    once SocketBox has actually served a zero-length read on a live socket.
    """

    def __init__(self, box):
        threading.Thread.__init__(self, daemon=True)
        self.box = box
        self.gap = False            # ...did the condition ever arise?
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("127.0.0.1", PORT))
        self.s.listen(2)
        self.request = None
        self.sent = 0

    def reply(self):
        head = (b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                b"Content-Length: %d\r\n\r\n" % len(BODY))
        return head, BODY

    def run(self):
        try:
            c, _ = self.s.accept()
        except OSError:
            return
        c.settimeout(30)
        try:
            req = b""
            while b"\r\n\r\n" not in req:
                b = c.recv(1024)
                if not b:
                    break
                req += b
            self.request = req
            head, body = self.reply()
            c.sendall(head)
            self.sent += len(head)
            want = self.box.empty_up + 1        # ...the gap assertion 3 lives
            t0 = time.time()                    # in, waited for and not slept
            while self.box.empty_up < want and time.time() - t0 < 240:
                time.sleep(0.05)
            self.gap = self.box.empty_up >= want
            c.sendall(body)
            self.sent += len(body)
        finally:
            c.close()


def tree():
    """The far side's file volume: the link mounts one whether or not
    anything is going to browse it, so it has to be there."""
    t = P.FileTree()
    t.add(0, "READ.ME", content=b"a link volume\r\n")
    return t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=None)
    a = ap.parse_args()

    fails = []
    img = os.path.getsize("build/socktest.bin")
    box = P.SocketBox()
    srv = Server(box)
    srv.start()
    say("http server on 127.0.0.1:%d, %d byte body" % (PORT, len(BODY)))

    ft = tree()
    with os88marty.launch("build/os8088-360.img",
                          apps="build/socktest360.img",
                          machine=MACHINE) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        p = P.Partner(m)

        # --- launch the package first, while the wire is quiet -------------
        # It comes up IDLE and touches nothing: the fetch does not begin until
        # it is clicked, which is what lets that click be a PAUSED one.
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "SOCKTEST.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("socktest: SOCKTEST.O88 did not open a window")
        skwin = wins2[-1]
        rec = m.read(S("wm_wins") + skwin * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        say("package at %04X" % pseg)

        def report():
            raw = m.readseg(pseg, 0, img)
            out = {}
            for key, tag in (("state", b"State: "), ("sock", b"Sock:  "),
                             ("got", b"Got: "), ("head", b"Head: ")):
                i = raw.find(tag)
                out[key] = raw[i:raw.index(b"\0", i)].decode("latin-1")
            return out

        # --- tick the link driver, with the far end answering --------------
        # No driver row is wanted by default (SPEC.md 51.3), so this click is
        # both the request and the moment the cable is spoken to. It has to be
        # a PAUSED click, and the RELEASE form of one: since SPEC.md 13.8.3 a
        # panel control arms on the press and acts on the release, so it is
        # the release that runs net_connect and anything that lets the guest
        # run after it spends the handshake into a wire nobody is holding.
        # The click on the package's own window below is the other case and
        # keeps the press-only form - a package's W_ONCLICK still fires on
        # MDOWN.
        mo.menu(8, 8, 8, 40)                    # chip menu -> Control Panel
        os88marty.settle(m)
        cp = None
        for w in dispcp.win_list(m, S):
            x, y, ww, hh = dispcp.win_rect(m, S, w)
            if ww >= 280 and hh >= 100:
                cp = (x, y)
        if cp is None:
            sys.exit("socktest: no Control Panel window")
        cx, cy = cp
        x0, y0 = cx + 1, cy + 18
        mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)
        os88marty.settle(m)

        # ROW 4 IS BELOW THE FOLD (SPEC.md 31.1.1): the pane shows four and
        # os88net is the fifth, so the list is scrolled to it first and what
        # is clicked is its VISIBLE index.
        vis = dispcp.drv_show(mo, x0, y0, NET_ROW,
                              lambda: os88marty.settle(m))
        mo.to(x0 + CP_RX + 40, dispcp.drv_row_y(y0, vis))
        m.pause()
        p.sync()
        p.click_paused_release()
        p.hello(P.NET_VER_SOCK)                 # ...the version byte IS the
                                                # socket probe (SPEC.md 62.11)
        p.allow(60000000)                       # `budget` is a LIFETIME total
                                                # and `spent` never resets, so
                                                # a session in phases re-anchors
                                                # between them - see allow()
        seen = p.serve(ft, limit=24, idle=8000000, sox=box)
        say("connect handshake served: %r" % "".join(seen))
        row = m.read(S("drv_tab") + NET_ROW * DRVR_SZ + DRVR_SEG, 2)
        seg = row[0] | (row[1] << 8)
        say("NET.DRV at %04X" % seg)
        if not seg:
            sys.exit("socktest: the link driver did not load")

        # Close the panel, and let the guest run: the wire is idle between
        # commands and NET.DRV has no worker, so nothing touches it here.
        m.run()
        mo.click(cx + 8, cy + 9)
        os88marty.settle(m)

        # --- and now the fetch ---------------------------------------------
        # The same paused-click trick: the worker's very next turn after the
        # click is NETV_OPEN, which is a wire command.
        sx, sy, sw, sh = dispcp.win_rect(m, S, skwin)
        mo.to(sx + sw // 2, sy + sh - 10)
        m.pause()
        p.sync()
        p.click_paused()
        p.allow(400000000)                      # the fetch is hundreds of
                                                # exchanges under a stepped
                                                # emulator; see allow()
        try:
            seen = p.serve(ft, limit=400, idle=12000000, sox=box)
            say("fetch served: %r" % "".join(seen))
        except P.LinkTimeout as e:
            # DUMP WHAT WE HAVE RATHER THAN A TRACEBACK. A stall on this wire
            # is diagnosed from what was asked and answered, and a traceback
            # says only which nibble it died on.
            say("fetch STALLED: %s" % e)
            say("   partner saw: %r" % "".join(p.log))
            for line in box.log:
                say("   " + line)
            fails.append("the wire stalled mid-fetch: %s" % e)
        else:
            for line in box.log:
                say("   " + line)

        m.run()
        os88marty.settle(m)
        r = report()
        for k in ("state", "sock", "got", "head"):
            say("  %s" % r[k])

        if a.shot:
            sw2, sh2, srows = m.vram("cga")
            os88marty.write_png(a.shot, sw2, sh2, srows)
            say("wrote %s" % a.shot)

    # --- 1: the link, and the socket verbs behind it -----------------------
    want = P.NSTF_PORT | P.NSTF_LINK | P.NSTF_SOCK
    got = int(r["state"][7:9], 16)
    if got != want:
        fails.append("NETV_STATE answered %02X, wanted %02X (port|link|sock)"
                     % (got, want))

    # --- 2: the page, whole and correct ------------------------------------
    if "done" not in r["sock"]:
        fails.append("the fetch ended in %r, not `done`" % r["sock"].strip())
    head, body = srv.reply()
    n = int(r["got"][5:10])
    if n != len(head) + len(body):
        fails.append("%d bytes arrived, the server sent %d"
                     % (n, len(head) + len(body)))
    if not r["head"].startswith("Head: HTTP/1.0 200 OK"):
        fails.append("the reply's first bytes are %r" % r["head"])
    if srv.request is None or not srv.request.startswith(b"GET /demo.htm "):
        fails.append("the server saw request %r" % srv.request)

    # --- 3: the split write really did produce an empty read ----------------
    # `-> 0 (state 2)` is a recv that delivered NOTHING while the socket was
    # still NSK_UP. It is the state a package must not read as an end, and the
    # server's split write is what guarantees at least one.
    recvs = [l for l in box.log if l.startswith("RECV")]
    say("%d RECV(s), %d empty-while-up, server gap %s"
        % (len(recvs), box.empty_up, "yes" if srv.gap else "NO"))
    if not recvs:
        fails.append("no RECV reached the far side at all")
    if not srv.gap:
        fails.append("the server gave up waiting for a zero-length read on a "
                     "live socket - assertion 3 did not run")
    elif len(recvs) < 3:
        fails.append("the page arrived in %d recv(s): the package stopped at "
                     "the first short read" % len(recvs))

    # --- 4: the handle came back -------------------------------------------
    free = r["state"][15:16]
    if free != str(P.NET_SOCKS):
        fails.append("%s of %d handles free after the close - one leaked"
                     % (free, P.NET_SOCKS))

    for f in fails:
        say("FAIL: " + f)
    say("socktest: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""The BROWSER fetches a page over the cable (SPEC.md 71, stage D).

    make && make browsertest && python3 tests/brfetch.py

tests/socktest.py proves the TRANSPORT with a package that does nothing but
count bytes. This proves the APPLICATION: a URL typed into the location bar,
an HTTP GET over a bit-banged parallel cable, and the reply through the same
parser, layout and painter a floppy page goes through.

THREE ASSERTIONS.

1. THE REQUEST IS WHAT A SERVER EXPECTS. Not "something arrived": the exact
   request line and a Host: header, read off the socket by the HTTP server
   this file starts. A browser that sent a malformed GET would still get an
   answer out of a forgiving server and fail against every real one.

2. THE PAGE IS RENDERED. Ink appears in the content box and the browser's own
   line count is non-zero - so the bytes reached br_parse and br_layout, not
   merely the claim.

3. THE BROWSER AGREES. `br_nstate` is BN_DONE and `br_nlines` is non-zero,
   read out of its bss - so the bytes reached br_parse and br_layout rather
   than merely the claim, and the application is not reporting a failure over
   a page that rendered. **State and not a string**: the first version looked
   for `Ready` in the image, which is a constant that is there whether or not
   the fetch worked, so it passed over a run that had failed.

The far end is tests/lptlink/partner.py's SocketBox - REAL host sockets - so
the page crossed one. The WIRE's verdict is still the 5150's (SPEC.md
62.10.3).
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

MACHINE = "os8088_5150_cga_lpt"
NET_ROW = 4                        # ...last since the reorder (SPEC.md 31.1)
DRVR_SZ, DRVR_SEG = 16, 2
CP_I0Y, CP_IROWH, CP_IDRV = 6, 14, 2
CP_RX, CP_DBY1, CP_DROWH = 96, 20, 26
PORT = 8089
PATH = "/net.htm"

PAGE = (b"<html><head><title>Over the wire</title></head><body>\r\n"
        b"<h1>Fetched over a parallel cable</h1>\r\n"
        b"<p>Every byte of this page crossed a LapLink cable one nibble at a\r\n"
        b"time, into a 4.77 MHz 8088, through the same parser a floppy page\r\n"
        b"goes through.</p>\r\n"
        b"<table border=1><tr><th>Part</th><th>Rate</th></tr>\r\n"
        b"<tr><td>The cable</td><td>3741 B/s</td></tr>\r\n"
        b"<tr><td>Its own floppy</td><td>21307 B/s</td></tr></table>\r\n"
        b"</body></html>\r\n")


def say(*a):
    print(*a)
    sys.stdout.flush()


class Server(threading.Thread):
    """One page, and it REMEMBERS THE REQUEST - which is assertion 1."""

    def __init__(self):
        threading.Thread.__init__(self, daemon=True)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("127.0.0.1", PORT))
        self.s.listen(2)
        self.request = None

    def run(self):
        try:
            c, _ = self.s.accept()
        except OSError:
            return
        c.settimeout(60)
        try:
            req = b""
            while b"\r\n\r\n" not in req and len(req) < 4096:
                b = c.recv(1024)
                if not b:
                    break
                req += b
            self.request = req
            c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                      b"Content-Length: %d\r\n\r\n" % len(PAGE) + PAGE)
        finally:
            c.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=None)
    a = ap.parse_args()

    fails = []
    img = os.path.getsize("build/browser.bin")
    # THE DISK IS BUILT FROM THE PACKAGE AND CAN BE OLDER THAN IT. That is not
    # hypothetical: this test's first real run drove a browser two commits
    # behind, reported `no such host` for a URL the bar was holding correctly,
    # and looked exactly like a parser bug. `make build/browser.o88` does not
    # rebuild the image, and nothing said so.
    if (os.path.getmtime("build/brtest360.img")
            < os.path.getmtime("build/browser.o88")):
        sys.exit("brfetch: build/brtest360.img is OLDER than browser.o88 - "
                 "run `make browsertest` (this would have tested the wrong "
                 "binary and blamed the browser)")
    srv = Server()
    srv.start()
    url = "http://127.0.0.1:%d%s" % (PORT, PATH)
    say("http server on %s, %d byte page" % (url, len(PAGE)))

    box = P.SocketBox()
    ft = P.FileTree()
    ft.add(0, "READ.ME", content=b"a link volume\r\n")

    with os88marty.launch("build/os8088-360.img", apps="build/brtest360.img",
                          machine=MACHINE) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        p = P.Partner(m)

        # --- the browser first, empty: it touches no wire until asked ------
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "BROWSER.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("brfetch: BROWSER.O88 did not open a window")
        bw = wins2[-1]
        rec = m.read(S("wm_wins") + bw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        say("browser at %04X" % pseg)

        # THE BROWSER'S OWN STATE, at bss offsets derived from the image
        # size the way tests/brtest.py already reads br_url. Reading a STRING
        # CONSTANT instead is the trap this test fell into first time round:
        # `Ready` is in the image whether or not the fetch worked, so the
        # status assertion passed over a run that had failed.
        BR_NLINES, BR_NSTATE = 38, 946
        BN_DONE, BN_ERR = 6, 7

        def bss_b(off):
            return m.readseg(pseg, img + off, 1)[0]

        def bss_w(off):
            d = m.readseg(pseg, img + off, 2)
            return d[0] | (d[1] << 8)

        def field(tag):
            raw = m.readseg(pseg, 0, img + 3000)
            i = raw.find(tag)
            if i < 0:
                return None
            return raw[i:raw.index(b"\0", i)].decode("latin-1")

        # --- tick the link driver, with the far end answering --------------
        mo.menu(8, 8, 8, 40)
        os88marty.settle(m)
        cp = None
        for w in dispcp.win_list(m, S):
            x, y, ww, hh = dispcp.win_rect(m, S, w)
            if ww >= 280 and hh >= 100:
                cp = (x, y)
        if cp is None:
            sys.exit("brfetch: no Control Panel window")
        cx, cy = cp
        x0, y0 = cx + 1, cy + 18
        mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)
        os88marty.settle(m)
        # ROW 4 IS BELOW THE FOLD (SPEC.md 31.1.1) - scroll, then click the
        # VISIBLE index.
        vis = dispcp.drv_show(mo, x0, y0, NET_ROW,
                              lambda: os88marty.settle(m))
        mo.to(x0 + CP_RX + 40, dispcp.drv_row_y(y0, vis))
        m.pause()
        p.sync()
        p.click_paused_release()
        p.hello(P.NET_VER_SOCK)
        p.allow(60000000)
        seen = p.serve(ft, limit=24, idle=8000000, sox=box)
        say("connect served: %r" % "".join(seen))
        row = m.read(S("drv_tab") + NET_ROW * DRVR_SZ + DRVR_SEG, 2)
        if not (row[0] | (row[1] << 8)):
            sys.exit("brfetch: the link driver did not load")
        m.run()
        mo.click(cx + 8, cy + 9)                    # close the panel (31.8)
        os88marty.settle(m)

        # --- type the URL into the location bar ----------------------------
        # The bar is NOT the top strip of the content: SPEC.md 71.6's toolbar
        # - Back, Forward, Reload and the state - is above it. Clicking the old
        # row landed on Forward, so the bar never took focus and the URL was
        # typed into the page.
        BR_LPAD, BR_TBH, BR_TBG, BR_LBAR = 3, 13, 1, 15
        bx0, by0, bww, bhh = dispcp.win_rect(m, S, bw)
        mo.click(bx0 + 60,
                 by0 + dispcp.TITLE_H + 1 + BR_LPAD + BR_TBH + BR_TBG
                 + BR_LBAR // 2)
        os88marty.settle(m)
        m.type_text(url)
        os88marty.settle(m)
        typed = field(b"http://127.0.0.1")
        say("location bar holds %r" % typed)
        if typed is None:
            fails.append("the URL never reached the location bar")

        # RETURN IS DELIVERED TO A PAUSED MACHINE. br_go runs on the very next
        # UI pass and the worker's first turn after it is a wire command, so
        # anything that lets the guest run first spends the fetch into a cable
        # nobody is holding - socktest.py's click_paused, one key along.
        m.pause()
        p.sync()
        m.key("Enter")
        p.allow(400000000)
        try:
            seen = p.serve(ft, limit=600, idle=12000000, sox=box)
            say("fetch served: %r" % "".join(seen))
        except P.LinkTimeout as e:
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

        # --- what is on the screen, and what the browser believes ----------
        cx0, cy0 = bx0 + 1, by0 + dispcp.TITLE_H + 1
        cw0, ch0 = bww - 2, bhh - dispcp.TITLE_H - 1
        w0, h0, rows = m.vram("cga")
        flat = bytes(b for r in rows for b in r)
        stride = len(flat) // h0
        ink = sum(1 for yy in range(cy0 + 34, min(cy0 + ch0, h0))
                  for xx in range(cx0, min(cx0 + cw0, stride))
                  if not flat[yy * stride + xx])
        say("content: %d ink pixels below the bar" % ink)
        if ink < 1500:
            fails.append("%d ink pixels under the location bar - the page did "
                         "not render (an EMPTY browser measures ~1200 here: "
                         "the scroll bar and the bar's own frame)" % ink)

        nstate, nlines = bss_b(BR_NSTATE), bss_w(BR_NLINES)
        say("br_nstate = %d, br_nlines = %d" % (nstate, nlines))
        if nstate == BN_ERR:
            fails.append("the browser's own state is BN_ERR: the fetch failed "
                         "and it knows")
        elif nstate != BN_DONE:
            fails.append("the browser's own state is %d, not BN_DONE - the "
                         "fetch never completed" % nstate)
        if nlines < 5:
            fails.append("br_nlines is %d: the page never reached the layout"
                         % nlines)

        if a.shot:
            os88marty.write_png(a.shot, w0, h0, rows)
            say("wrote %s" % a.shot)

    # --- 1: the request the server actually saw ----------------------------
    req = srv.request
    say("server saw: %r" % (req.split(b"\r\n")[0] if req else None))
    if req is None:
        fails.append("the server was never connected to")
    else:
        if not req.startswith(b"GET %s HTTP/1.0\r\n" % PATH.encode()):
            fails.append("the request line is %r" % req.split(b"\r\n")[0])
        if b"Host: 127.0.0.1" not in req:
            fails.append("no Host: header - every 1.1 server needs one and a "
                         "1.0 virtual host does too")

    for f in fails:
        say("FAIL: " + f)
    say("brfetch: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

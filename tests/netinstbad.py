#!/usr/bin/env python3
"""INSTALL's release check against a server that has NO release (SPEC.md
117.10.3): what os8088.com answered before the tree was published.

    make httpmounttest build/install-gate.o88 && python3 tests/netinstbad.py

QEMU by name: the network card (docs/TESTING.md, item 5). tests/netinst.py's
machine, but the host at 10.0.2.2:8096 answers SYSTEM/OS88VER.TXT with
something that is not a release, one boot per answer:

  cf404    Cloudflare's own 404 - HTTP/1.1, ~1.5KB of headers (a long
           content-security-policy, Report-To, NEL), text/html, an 8,821-byte
           body, Connection: keep-alive, and the connection held open
           afterwards. What the owner's machine was answered with, reported
           as "release HTTP/1.1, newer".
  html200  200 OK with an HTML "Not found" page as the body.
  r301     a redirect with no body.
  short    'HTTP/1.1 200' and almost nothing else: a reply shorter than the
           status check reads.
  rel404   a 404 whose body IS release-shaped (99991231): only the status
           check refuses this one, so it is that check's own leg.

Each must leave [in_net] = NET_FAIL ('no answer'), never OLD/SAME/NEW - and
the source page must not offer the network. A real release (99991231) served
last is the positive control: it must read NET_NEW, so a row that cannot
reach the server cannot pass by failing every leg the same way.

THE DEFECT this row was written for was neither check - both were right -
but in_netck loading NET_FAIL into BL BEFORE calling in_vget, which spends
BL on its socket verbs: a refusal stored the last NETV_* code as the answer,
RECV (5) reading as NET_NEW and STATUS (3) as NET_OLD. Put the `mov bl,
NET_FAIL` back above the call and cf404, r301, short and rel404 go red.
BREAK IT ON PURPOSE the other two ways as well: in_vparse's `.bad` answering
anything but NET_FAIL takes html200 red, and the status check in in_vget
taken out takes rel404 red.
"""
import os
import shutil
import socket
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import httpmount as HM                                        # noqa: E402
import netinst as NI                                          # noqa: E402
import os88build                                              # noqa: E402
import os88qemu                                              # noqa: E402
import xmcheck                                                # noqa: E402

PORT = NI.PORT
NET_FAIL, NET_NEW = 2, 5

CF_HDRS = (
    b"Date: Sat, 25 Oct 2026 12:00:00 GMT\r\n"
    b"Content-Type: text/html; charset=utf-8\r\n"
    b"Content-Length: 8821\r\n"
    b"Connection: keep-alive\r\n"
    b"Server: cloudflare\r\n"
    b"CF-RAY: 8d2f0c1e9a7b4c21-AMS\r\n"
    b"CF-Cache-Status: DYNAMIC\r\n"
    b"Cache-Control: public, max-age=0, must-revalidate\r\n"
    b"Content-Security-Policy: default-src 'self'; script-src 'self' "
    b"'unsafe-inline' https://static.cloudflareinsights.com "
    b"https://challenges.cloudflare.com; style-src 'self' 'unsafe-inline' "
    b"https://fonts.googleapis.com; font-src 'self' https://fonts.gstatic.com; "
    b"img-src 'self' data: https:; connect-src 'self' "
    b"https://cloudflareinsights.com https://os8088.com wss://os8088.com; "
    b"frame-src https://challenges.cloudflare.com; worker-src 'self' blob:; "
    b"object-src 'none'; base-uri 'self'; form-action 'self'; "
    b"frame-ancestors 'none'; upgrade-insecure-requests\r\n"
    b"Report-To: {\"endpoints\":[{\"url\":\"https:\\/\\/a.nel.cloudflare.com"
    b"\\/report\\/v4?s=Zq3kR2b%2FyXc8m9PqL0w1vT7nH5sJ4dF6gK8aB2eC3iU9oY1tR5"
    b"wE7qA0zX4cV6bN8mM2lK4jH6gF8dS0aP2oI4uY6tR8eW0qZ2xC4vB6nM8%3D\"}],"
    b"\"group\":\"cf-nel\",\"max_age\":604800}\r\n"
    b"NEL: {\"success_fraction\":0,\"report_to\":\"cf-nel\",\"max_age\":604800}\r\n"
    b"Strict-Transport-Security: max-age=31536000; includeSubDomains\r\n"
    b"X-Content-Type-Options: nosniff\r\n"
    b"X-Frame-Options: DENY\r\n"
    b"Referrer-Policy: strict-origin-when-cross-origin\r\n"
    b"Permissions-Policy: accelerometer=(), camera=(), geolocation=(), "
    b"gyroscope=(), magnetometer=(), microphone=(), payment=(), usb=()\r\n"
    b"Vary: Accept-Encoding\r\n"
    b"alt-svc: h3=\":443\"; ma=86400\r\n")
HTML = (b"<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
        b"<title>Not found -- os8088</title><link rel=\"canonical\" "
        b"href=\"https://os8088.com/404\"></head><body>")
HTML = HTML + b" " * (8821 - len(HTML) - 14) + b"</body></html>"

ANSWERS = {
    "cf404": (b"HTTP/1.1 404 Not Found\r\n" + CF_HDRS + b"\r\n" + HTML, True),
    "html200": (b"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
                b"Content-Length: %d\r\n\r\n" % len(HTML) + HTML, False),
    "r301": (b"HTTP/1.1 301 Moved Permanently\r\nLocation: "
             b"https://os8088.com/net/big/SYSTEM/OS88VER.TXT\r\n"
             b"Content-Length: 0\r\n\r\n", False),
    "short": (b"HTTP/1.1 200\r\n", False),
    "rel404": (b"HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\n"
               b"99991231\r\n", False),
    "release": (b"HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n"
                b"99991231\r\nA line of what is new.\r\n", False),
}


class Server(object):
    """One raw answer per connection; `hold` keeps the socket open after it,
    the way a keep-alive server does."""

    def __init__(self):
        self.mode = None
        self.gets = []
        self.s = socket.socket()
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        os88qemu.lease_port(PORT)       # (SOAK-PARALLEL.md 17.3)
        self.s.bind(("127.0.0.1", PORT))
        self.s.listen(4)
        threading.Thread(target=self.loop, daemon=True).start()

    def loop(self):
        while True:
            try:
                c, _ = self.s.accept()
            except OSError:
                return
            threading.Thread(target=self.one, args=(c,), daemon=True).start()

    def one(self, c):
        try:
            c.settimeout(10)
            req = b""
            while b"\r\n\r\n" not in req:
                d = c.recv(512)
                if not d:
                    break
                req += d
            self.gets.append(req.split(b"\r\n")[0])
            body, hold = ANSWERS[self.mode]
            c.sendall(body)
            if hold:
                time.sleep(15)
        except OSError:
            pass
        finally:
            c.close()

    def close(self):
        self.s.close()


def main():
    gate = os88build.at("build/install-gate.o88")
    syms = NI.gate_syms()
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    hd = os.path.join(tmp, "hd.img")
    open(hd, "wb").truncate(4 << 20)
    srv = Server()
    q = None
    bad = []
    try:
        for mode in ("cf404", "html200", "r301", "short", "rel404", "release"):
            srv.mode = mode
            n0 = len(srv.gets)
            q = NI.machine(tmp, hd, gate)
            S = xmcheck.sym
            ui = HM.os88ui.UI(q, mouse=object(), sym=S, verbose=False)
            xmcheck.dblclick(q.sock, *HM.geom.drive_pt(q, "A", S))
            q.need(lambda: HM.front_on(ui, 0) is not None, "A:'s window", 60)
            q.done()
            w = HM.front_on(ui, 0)
            before = {x[0] for x in NI.HI.wins(q)}
            i, _ = ui.entry("INSTALL.O88", w)
            if os88qemu.MEGA:           # mega.img's twin: below the fold
                i = NI.onscreen(q, ui, w, i)    # (netinst.onscreen)
            xmcheck.dblclick(q.sock, *ui.row_xy(w, i - ui.scroll(w)))
            q.need(lambda: NI.install_seg(q, before)[0] is not None,
                   "INSTALL", 60)
            seg, _ = NI.install_seg(q, before)
            rd = lambda n: q.readseg(seg, syms[n], 1)[0]       # noqa: E731
            q.need(lambda: rd("in_net") != NI.NET_LOOK,
                   "the network's answer", 90)
            net, src = rd("in_net"), rd("in_src")
            head = q.readseg(seg, syms["in_vbuf"], 12)
            asked = len(srv.gets) - n0
            print("   %-8s in_net %d, source row %d, in_vbuf %r, %d GET(s)"
                  % (mode, net, src, head, asked))
            want = NET_NEW if mode == "release" else NET_FAIL
            if not asked:
                bad.append("%s: the server was never asked" % mode)
            if net != want:
                bad.append("%s: in_net %d, not %d%s" % (
                    mode, net, want, " - a reply that is not a release was "
                    "taken for one (SPEC.md 117.10.3)" if want == NET_FAIL
                    else ""))
            if want == NET_FAIL and src != 1:
                bad.append("%s: the network row is the default" % mode)
            q.close()
            q = None
    finally:
        if q:
            q.close()
        srv.close()
        shutil.rmtree(tmp, ignore_errors=True)
    if bad:
        for b in bad:
            print("netinstbad: FAIL - " + b)
        sys.exit(1)
    print("netinstbad: ok")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""The proxy answered over a SOCKET, in the guest's own wire format.

    python3 tests/proxytest.py

`os88proxy.py --selfcheck` checks the RENDER: fixtures in, legal pages out.
This checks the SERVER, and it does it by speaking exactly what
apps/browser/brnet.inc speaks - `GET <path> HTTP/1.0`, a `Host:` header, a
`User-Agent: os8088`, read until close - and by refusing anything `br_split`
would refuse. A page that is perfect and arrives down a socket the browser
cannot drive is not a page.

FOUR ASSERTIONS, and each is a failure os8088 could not report:

  1. every request the browser would make FITS. A path over BR_PATHMAX-1 is
     truncated by br_split, so it fetches something else and says nothing.
  2. every answer is legal for the guest - the same gate --selfcheck runs, on
     the bytes that actually crossed the socket rather than on an internal
     render.
  3. every link on a served page CAN BE FOLLOWED, resolved the way br_resolve
     resolves it: root-relative against the host we are on. A minted token
     that has been forgotten is a dead end.
  4. a form SUBMITS: the URL is composed the way br_submit composes it -
     action, '?', encoded pairs - and the far side sees the query.

It needs no network and no emulator: the origin server, the proxy and the
browser-shaped client are all in this file.
"""
import os
import re
import socket
import sys
import json
import threading
import time
import urllib.parse
import http.server

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import os88proxy as P                                            # noqa: E402

ORIGIN_PORT = 8199
PROXY_PORT = 8188
FIXTURE = os.path.join(HERE, "proxy", "modern.htm")
FRONT = os.path.join(HERE, "proxy", "reddit-front.htm")
THREAD = os.path.join(HERE, "proxy", "reddit-thread.htm")
USER, PASSWD = "os8088user", "correct-horse-battery"
COOKIE = "reddit_session=abc123"


class Origin(http.server.BaseHTTPRequestHandler):
    """A 2020s web server - and, on the reddit paths, old.reddit itself.

    It answers the legacy login endpoint the way reddit does (an `errors`
    list, and a cookie on success) and it serves the REAL captures for the
    front page and the thread, so the whole route runs: sign in, get the
    listing, click a comment count, get the thread.
    """
    protocol_version = "HTTP/1.0"
    seen = []
    logged_in = False

    def log_message(self, *a):
        pass

    def _send(self, body, ctype="text/html; charset=utf-8", extra=None):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or []):
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        form = urllib.parse.parse_qs(self.rfile.read(n).decode())
        Origin.seen.append("POST " + self.path)
        user = (form.get("user") or [""])[0]
        pw = (form.get("passwd") or [""])[0]
        if user == USER and pw == PASSWD:
            Origin.logged_in = True
            self._send(json.dumps({"json": {"errors": [], "data": {
                "modhash": "x", "cookie": "y", "need_https": True}}}).encode(),
                "application/json", [("Set-Cookie", COOKIE + "; Path=/")])
        else:
            self._send(json.dumps({"json": {"errors": [
                ["WRONG_PASSWORD", "invalid password", "passwd"]]}}).encode(),
                "application/json")

    def do_GET(self):
        Origin.seen.append(self.path)
        if self.path.startswith("/api/me.json"):
            # What `whoami` asks now - a few hundred bytes instead of the
            # 300KB front page it used to read the name out of.
            body = (json.dumps({"data": {"name": USER}}) if Origin.logged_in
                    else json.dumps({"data": None})).encode()
            return self._send(body, "application/json")
        if "/comments/" in self.path:      # BEFORE the /r/ test: a permalink
            return self._send(open(THREAD, "rb").read())   # starts /r/<sub>/
        if self.path == "/" or self.path.startswith("/r/"):
            raw = open(FRONT, "rb").read()
            if not Origin.logged_in:
                # logged out: the header carries no user, which is what
                # `whoami` reads. Nothing else about the page changes.
                raw = raw.replace(b'<span class="user">', b'<span class="nouser">')
            return self._send(raw)
        if self.path.startswith("/search"):
            body = ("<html><head><title>Results</title></head><body><main>"
                    "<h1>Results</h1><p>query was: %s</p>"
                    "<p><a href='/page.html'>Back to the article</a></p>"
                    "</main></body></html>" % self.path).encode()
        elif self.path in ("/", "/page.html"):
            body = open(FIXTURE, "rb").read()
        else:
            body = b"<html><body><main><p>a small page</p></main></body></html>"
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def guest_fetch(path, host="127.0.0.1", port=PROXY_PORT):
    """br_req's request, br_take's ceiling, br_split's refusal."""
    if len(path) > P.MAX_PATH:
        raise AssertionError("BR_PATHMAX: %r is %d bytes and the browser "
                             "carries %d" % (path, len(path), P.MAX_PATH))
    # `Host:` WITHOUT THE PORT, because that is what br_req sends: br_split
    # takes the port off into br_port and the header carries the bare host.
    # The test used to send `host:port`, which is why it never caught a form
    # action built on the header alone - the one bug that made a Submit fail
    # instantly on the field machine.
    req = ("GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: os8088\r\n\r\n"
           % (path, host)).encode()
    s = socket.create_connection((host, port), 15)
    s.sendall(req)
    buf = b""
    while True:
        blk = s.recv(4096)
        if not blk:
            break
        buf += blk
        if len(buf) > P.HARD_BODY + 8192:
            break
    s.close()
    head, _, body = buf.partition(b"\r\n\r\n")
    assert head[:8] in (b"HTTP/1.0", b"HTTP/1.1"), head[:32]
    return int(head[9:12]), body[:P.HARD_BODY]


def hrefs(body):
    return [h.decode() for h in re.findall(rb'href="([^"]*)"', body)]


def as_path(href):
    """br_resolve: absolute is taken as it stands, /path is this host plus it,
    and anything else this test does not follow."""
    if href.startswith("http://"):
        rest = href.split("/", 3)
        return "/" + (rest[3] if len(rest) > 3 else "")
    return href if href.startswith("/") else None


def main():
    fails = []

    def check(name, body, opts):
        for bad in P.check_output(body, opts, name):
            fails.append(bad)
            print("   GATE: %s" % bad)

    threading.Thread(
        target=lambda: http.server.ThreadingHTTPServer(
            ("127.0.0.1", ORIGIN_PORT), Origin).serve_forever(),
        daemon=True).start()

    opts = P.parse_args(["--port", str(PROXY_PORT), "--bind", "127.0.0.1",
                         "--link-file", ""])
    proxy = P.Proxy(opts)
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", PROXY_PORT), P.Handler)
    httpd.proxy = proxy
    httpd.daemon_threads = True
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    time.sleep(0.3)

    code, home = guest_fetch("/")
    print("GET /                     %3d  %5d bytes  %d links"
          % (code, len(home), len(hrefs(home))))
    check("home", home, opts)

    t0 = time.time()
    code, page = guest_fetch("/h/127.0.0.1:%d/page.html" % ORIGIN_PORT)
    print("GET the article           %3d  %5d bytes  (%.2fs, %d on the origin)"
          % (code, len(page), time.time() - t0, os.path.getsize(FIXTURE)))
    check("article", page, opts)
    if b"cookie" in page.lower() or b"Privacy" in page:
        fails.append("boilerplate survived the strip")
    if b"<table>" not in page:
        fails.append("no table on a page with three of them")

    seen = set()
    for href in hrefs(page):
        path = as_path(href)
        if not path or path in seen:
            continue
        seen.add(path)
        # HERMETIC: a token pointing off this machine is not followed. The
        # fixture carries real outbound links on purpose (they are what
        # link-rewriting is FOR) and a test that fetched them would be a test
        # of somebody else's uptime.
        tok = path[3:].split("?")[0] if path.startswith(("/l/", "/x/")) else ""
        ent = proxy.links.get(tok) if tok else None
        if ent and ent[0] == "u" and "127.0.0.1" not in ent[1]:
            print("   skip   %-22s (points off this machine)" % path[:22])
            continue
        if len(path) > P.MAX_PATH:
            fails.append("emitted %r, %d bytes" % (path, len(path)))
            continue
        code, body = guest_fetch(path)
        print("   follow %-22s %3d  %5d bytes" % (path[:22], code, len(body)))
        check(path, body, opts)
        if b"Link expired" in body or b"No such page" in body:
            fails.append("%s is a dead end on a page the proxy itself wrote"
                         % path)

    m = re.search(rb'<form action="([^"]*)"', page)
    if not m:
        fails.append("the GET form did not survive")
    else:
        act = m.group(1).decode()
        if act.startswith("http://") and ":%d" % PROXY_PORT not in act:
            fails.append("the form action %r has no port - it will go to 80 "
                         "and be refused instantly" % act)
        if len(act) > P.MAX_ACTION:
            fails.append("action is %d bytes, BR_ACTMAX carries %d"
                         % (len(act), P.MAX_ACTION))
        qpath = as_path(act) + "?q=os8088&section=archive"
        code, res = guest_fetch(qpath)
        print("SUBMIT %-24s %3d  %5d bytes" % (qpath[:24], code, len(res)))
        check("submit", res, opts)
        if not any("q=os8088" in s for s in Origin.seen):
            fails.append("the origin never saw the query: %r" % Origin.seen)

    # ONE form, FOUR fields: the fixture carries two forms and eleven inputs
    if page.count(b"<form") != 1:
        fails.append("%d forms on one page; the guest holds one field table"
                     % page.count(b"<form"))

    # ---------------------------------------------------------------------
    # The first-class site: the listing and a thread, off the local origin,
    # which serves the REAL captures on reddit's own paths.
    # ---------------------------------------------------------------------
    old = "http://127.0.0.1:%d" % ORIGIN_PORT
    P.RedditSite.hosts = ("127.0.0.1",)
    P.RedditSite.OLD = old
    # BOTH hosts, because the proxy asks for www by default now (PROXY-PLAN
    # 16) and a stand-in that answers as only one of them sends the test out
    # to the real internet - where it fails for a reason that has nothing to
    # do with this tree.
    P.RedditSite.WWW = old

    code, listing = guest_fetch("/rd")
    print("THE LISTING               %3d  %5d bytes" % (code, len(listing)))
    check("listing", listing, opts)
    if listing.count(b"comments</a>") < 5:
        fails.append("not a listing: %d comment links"
                     % listing.count(b"comments</a>"))
    if b"Popular" not in listing:
        fails.append("no categories row on the listing")

    # THE HOME PAGE: one form with TWO boxes, and one link a line. The guest
    # holds exactly one form, so an address box and a search box that are two
    # forms would silently become one of them.
    if home.count(b"<form") != 1:
        fails.append("%d forms on the home page; the guest holds one"
                     % home.count(b"<form"))
    if home.count(b'type="text"') != 2:
        fails.append("the home page has %d text boxes, not two"
                     % home.count(b'type="text"'))
    for want in (b">Address", b">Search", b'name="u"', b'name="q"'):
        if want not in home:
            fails.append("the home page has no %r" % want.decode())
    if b'href="/rd/"' not in home:
        fails.append("the home page does not offer the reddit front page")
    if b"old.reddit.com" in home:
        fails.append("the login-wall bookmark is back on the home page")
    if b"/in" in home or b"Sign in" in home:
        fails.append("the sign-in is back: it was removed, not hidden")

    # ...and BOTH boxes work, address first.
    code, went = guest_fetch("/s?u=127.0.0.1:%d/page&q=ignored" % ORIGIN_PORT)
    print("ADDRESS BOX               %3d  %5d bytes" % (code, len(went)))
    check("address", went, opts)
    if b"a small page" not in went:
        fails.append("the address box did not fetch the address: %r"
                     % went[:200])
    code, sought = guest_fetch("/s?q=os8088")
    check("search", sought, opts)
    if not any("q=os8088" in x for x in Origin.seen):
        fails.append("the search box never reached the engine: %r"
                     % Origin.seen[-3:])

    # a comment count on the listing leads to the thread
    tok = None
    for href in hrefs(listing):
        t = href[3:] if href.startswith("/l/") else ""
        ent = proxy.links.get(t) if t else None
        if ent and ent[0] == "u" and "/comments/" in ent[1]:
            tok = href
            break
    if not tok:
        fails.append("no comment link on the listing")
    else:
        code, thread = guest_fetch(tok)
        print("GET a thread              %3d  %5d bytes" % (code, len(thread)))
        check("thread", thread, opts)
        if b"<hr>" not in thread:
            fails.append("no rule between the post and the comments")
        if b"points " not in thread:
            fails.append("no comment taglines in the thread")
        if b"&gt;" not in thread:
            fails.append("nothing is indented: the thread reads flat")

    print("\n%s" % ("FAILURES:\n  " + "\n  ".join(fails) if fails
                    else "proxytest: all good"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""ETHER.DRV: a card, a stack, and the browser over it (SPEC.md 72.9).

    make && make ethertest && make browsertest && python3 tests/ethernet.py

**THIS ONE IS QEMU'S, AND THAT IS NOT A LAPSE.** CLAUDE.md's rule is MartyPC
first with a short list of exceptions, and this is on it for the plainest
possible reason: MartyPC has no network card of any kind, so the emulator this
tree develops on cannot host this driver at all. QEMU's `ne2k_isa` on
`-netdev user` is the only harness there is - a gateway at 10.0.2.2, DHCP
handing out 10.0.2.15, DNS at 10.0.2.3.

What QEMU costs is what it always costs: the machine is not an 8088 and no
timing here means anything. So every assertion below is about BEHAVIOUR and
none is about speed. There is no number in this file for PERFORMANCE.md to
want off the 5150, which is the honest position rather than a limitation.

SIX ASSERTIONS, and they climb the stack.

1. THE CARD. A base was found, a station address came out of the PROM, and it
   is a plausible one - not all zero, not all ones, not multicast.

1b. THE RINGS. `sk_seg` is non-zero and the ladder landed on its TOP rung
   (SPEC.md 72.13.2). This machine has the whole heap free when the driver
   attaches, so anything lower is the ladder failing and not the machine
   refusing - and the field spent a round unable to tell those apart, because
   nothing anywhere said which rung had been taken.

2. THE ADDRESS. `dhcp_st` is DH_BOUND and `eth_ip` is 10.0.2.15, the gateway
   10.0.2.2 and the name server 10.0.2.3 - all three read out of the driver's
   own image. That is DISCOVER, OFFER, REQUEST and ACK over the card, so it
   already proves transmit, receive, the broadcast path, UDP and the option
   walker before a socket exists.

3. THE SOCKET VERBS ANSWER FROM A CARD. The browser fetches a page over TCP -
   ARP for the gateway, a three-way handshake, a GET, a windowed drain and a
   close - and the request line arrives at a REAL host socket, read by the
   HTTP server this file starts.

4. THE PAGE RENDERS. `br_nstate` is BN_DONE and `br_nlines` is non-zero, read
   out of the browser's bss - so the bytes reached br_parse and br_layout and
   the application is not reporting a failure over a page that drew.

5. NOT ONE BYTE OF THE BROWSER IS ABOUT ETHERNET. The `.o88` under test is the
   one `make` builds for the shipped floppy, and the only thing it was told is
   a URL. That is stage E's whole claim (docs/plans/completed/NET-STACK-PLAN.md).

The driver is asked for by a SYSTEM.CFG that `make ethertest` puts on the disk,
so nothing here drives the Control Panel: the driver is attached and DHCP has
run before the first paint, and the gate reads state rather than clicking.
"""
import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

# THIS TREE'S root, DERIVED - never a hard-coded path. A literal is right in the
# checkout it was written in and wrong in a git worktree, which is how parallel
# work is done here: os88sym re-assembles ROOT/kernel/kernel.asm and compares it
# against ROOT/build/kernel.bin, so a literal ROOT answers about a DIFFERENT
# kernel from the image being booted.
_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))
import dispcp                                          # noqa: E402
import os88sym                                         # noqa: E402
import os88geom                                        # noqa: E402
import os88qemu                                              # noqa: E402
import os88build                                       # noqa: E402

S = os88sym.linear
SOCK = os.environ.get("QMP_SOCK", "build/qmp.sock")
PORT = 8090
PATH = "/eth.htm"
ENTITY_ACCENTS = (
    ("Agrave", "A"), ("Aacute", "A"), ("Acirc", "A"),
    ("Atilde", "A"), ("Auml", "A"), ("Aring", "A"), ("AElig", "A"),
    ("agrave", "a"), ("aacute", "a"), ("acirc", "a"),
    ("atilde", "a"), ("auml", "a"), ("aring", "a"), ("aelig", "a"),
    ("Ccedil", "C"), ("ccedil", "c"),
    ("Egrave", "E"), ("Eacute", "E"), ("Ecirc", "E"), ("Euml", "E"),
    ("egrave", "e"), ("eacute", "e"), ("ecirc", "e"), ("euml", "e"),
    ("Igrave", "I"), ("Iacute", "I"), ("Icirc", "I"), ("Iuml", "I"),
    ("igrave", "i"), ("iacute", "i"), ("icirc", "i"), ("iuml", "i"),
    ("Ntilde", "N"), ("ntilde", "n"),
    ("Ograve", "O"), ("Oacute", "O"), ("Ocirc", "O"),
    ("Otilde", "O"), ("Ouml", "O"), ("Oslash", "O"),
    ("ograve", "o"), ("oacute", "o"), ("ocirc", "o"),
    ("otilde", "o"), ("ouml", "o"), ("oslash", "o"),
    ("Ugrave", "U"), ("Uacute", "U"), ("Ucirc", "U"), ("Uuml", "U"),
    ("ugrave", "u"), ("uacute", "u"), ("ucirc", "u"), ("uuml", "u"),
    ("Yacute", "Y"), ("Yuml", "Y"), ("yacute", "y"), ("yuml", "y"),
)
URL = "http://10.0.2.2:%d%s" % (PORT, PATH)

SK_RXMAX, SK_TXMAX = 8192, 1024         # tcp.inc's top rung (SPEC.md 72.13)
ETH_ROW, DRVR_SZ, DRVR_SEG = 2, 16, 2   # drv_tab row 2 since SPEC.md 31.1's
                                        # reorder (sound, hdd, ETHER, ram, net)
DH_BOUND = 3
BN_DONE, BN_ERR = 6, 7
BR_SRCSEG, BR_DOCSEG = 24, 26
BR_SRCLEN, BR_SRCKB, BR_DOCKB, BR_DOCLEN, BR_NLINES = 30, 32, 34, 36, 38
BR_NSTATE = 946
BR_TRUNC = 53
BR_NMSG = 958

PAGE = (b"<html><head><title>Over the card</title></head><body>\r\n"
        b"<h1>Fetched over an NE2000</h1>\r\n"
        b"<p>Every byte of this page crossed an emulated ISA Ethernet card,\r\n"
        b"through ARP, a three-way handshake and a windowed drain, into the\r\n"
        b"same parser a floppy page goes through.</p>\r\n"
        b"<table border=1><tr><th>Path</th><th>Rate</th></tr>\r\n"
        b"<tr><td>The cable</td><td>3741 B/s</td></tr>\r\n"
        b"<tr><td>Its own floppy</td><td>21307 B/s</td></tr></table>\r\n"
        b"</body></html>\r\n")


def say(*a):
    print(*a)
    sys.stdout.flush()


# -----------------------------------------------------------------------------
# The driver's own symbols, the way tools/os88sym.py does the kernel's: a
# temporary copy with a [map] directive, and the result is REFUSED unless the
# binary it describes is byte-identical to the one on the disk under test. A
# map of a different build is worse than no map - every offset is plausible.
# -----------------------------------------------------------------------------
_ETHDEF = []


def ether_defines(*names):
    """The -D set ether_syms() assembles with, set once beside the knob.

    A knob build of the driver is a DIFFERENT driver, and the check below
    refuses a map that does not match `build/ether.bin` byte for byte - so a
    harness driving `ETHPROF=1` has to say so here or every lookup is refused.
    os88sym.default_defines() is the same arrangement for the kernel.
    """
    global _ETHDEF
    _ETHDEF = list(names)


def ether_syms():
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "e.asm")
        mp = os.path.join(d, "e.map")
        out = os.path.join(d, "e.bin")
        shutil.copy("drivers/ether/ether.asm", src)
        with open(src, "a") as f:
            f.write("\n[map all %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + ["-D" + n for n in _ETHDEF]
                       + ["-I", "drivers/ether/", "-I", "drivers/net/",
                          "-I", "drivers/", "-I", "apps/", "-o", out, src],
                       check=True)
        if open(out, "rb").read() != open(os88build.at("build/ether.bin"), "rb").read():
            sys.exit("ethernet: the mapped build is not build/ether.bin - "
                     "every offset it names would be plausible and wrong")
        syms = {}
        for line in open(mp):
            m = re.match(r"^\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$",
                         line)
            if m:
                syms[m.group(3)] = int(m.group(1), 16)
        return syms


# -----------------------------------------------------------------------------
# QEMU, over QMP. `pmemsave` is the memory read: HMP's `xp` prints a dump that
# has to be parsed back out of text, and this hands over the bytes.
# -----------------------------------------------------------------------------
class Qemu:
    """QMP, ONE CONNECTION AT A TIME AND NEVER A PERSISTENT ONE.

    `-qmp unix:...,server,nowait` serves a SINGLE client, so a monitor
    connection held open across the test makes every `tools/mouse.py` and
    `tools/qmp.py` subprocess sit in the listen backlog for ever. It connects,
    it does not error, and it does not return - which reads exactly like the
    guest having hung on the click. This class therefore opens and closes per
    command; four round trips on a Unix socket is nothing beside the emulator.
    """

    def __init__(self, path=SOCK):
        self.path = path
        self.tmp = tempfile.mkdtemp()
        for _ in range(150):                # ...but wait for it to exist
            try:
                self.hmp("info status")
                return
            except OSError:
                time.sleep(0.2)
        raise RuntimeError("no QMP socket at %s" % path)

    def hmp(self, cmd):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.path)
        f = s.makefile("rw")

        def send(obj):
            f.write(json.dumps(obj) + "\n")
            f.flush()
            while True:
                line = f.readline()
                if not line:
                    raise RuntimeError("QMP closed")
                msg = json.loads(line)
                if "event" in msg:
                    continue
                return msg
        try:
            json.loads(f.readline())
            send({"execute": "qmp_capabilities"})
            r = send({"execute": "human-monitor-command",
                      "arguments": {"command-line": cmd}})
            return r.get("return", "")
        finally:
            f.close()
            s.close()

    # --- SENDKEY, and it lives HERE rather than in a subclass ----------------
    # tests/dispcp.py's scroller drives a list with ArrowDown/End, and it is
    # the shared navigation every gate reuses. tests/ftpd.py worked out the
    # mapping and put it on its OWN Qemu, so ethernet.py - the file that
    # DEFINES this class and calls dispcp.open_named itself - had no `key` at
    # all and died with AttributeError the moment it opened a package by name.
    # One method on the base class is what ftpd's own comment already said it
    # was: "the whole of what a QEMU-hosted gate is missing to reuse that
    # scroller."
    QKEYS = {"ArrowDown": "down", "ArrowUp": "up", "Home": "home",
             "End": "end", "PageDown": "pgdn", "PageUp": "pgup",
             "Tab": "tab", "Enter": "ret"}

    def key(self, name):
        if name not in self.QKEYS:
            raise KeyError("no QMP sendkey name for %r" % name)
        self.hmp("sendkey " + self.QKEYS[name])
        os88qemu.pace(self, 0.05)           # the GUEST's time: os88qemu.py

    def read(self, linear, n):
        p = os.path.join(self.tmp, "m.bin")
        # THE FILENAME IS QUOTED, and it has to be: HMP parses an unquoted
        # `/tmp/...` as an EXPRESSION and answers "invalid char 't'", which
        # reads as a bad address rather than a bad argument.
        self.hmp('pmemsave 0x%X %d "%s"' % (linear, n, p))
        return open(p, "rb").read()

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)

    def quit(self):
        try:
            self.hmp("quit")
        except Exception:
            pass


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def gsleep(secs):
    """`secs` of the GUEST's clock, for a caller holding no Qemu - one
    tools/qmp.py `gsleep`, which counts the BIOS ticks (tests/os88qemu.py's
    reason: a loaded box cannot shorten it the way it shortens a sleep)."""
    subprocess.run(["python3", "tools/qmp.py", SOCK, "gsleep %s" % secs],
                   check=True, capture_output=True)


def dotted(b):
    return ".".join(str(x) for x in b)


class Mouse:
    """tools/mouse.py, one process per action - QEMU's msmouse is 1200 baud
    and the pacing inside that tool is what makes a drag or a click land."""

    def run(self, *args):
        subprocess.run(["python3", "tools/mouse.py", SOCK] + list(args),
                       check=True, capture_output=True)

    def click(self, x, y):
        self.run("click", str(x), str(y))
        gsleep(0.4)

    def dblclick(self, x, y):
        # TWO `click`s ARE NOT A DOUBLE-CLICK (CLAUDE.md): the detectors
        # compare birth ticks in a 9-tick window and two processes are far too
        # slow. Position, then both presses down one QMP connection. The
        # spacing INSIDE the pair stays host time: it is well inside the
        # window either way, and a tick-rounded one would eat into it.
        self.run("to", str(x), str(y))
        subprocess.run(["python3", "tools/qmp.py", SOCK,
                        "mouse_button 1", "sleep 0.08", "mouse_button 0",
                        "sleep 0.12",
                        "mouse_button 1", "sleep 0.08", "mouse_button 0",
                        "gsleep 0.4"],
                       check=True, capture_output=True)


def settle(m, card=None):
    """What a settle was on QEMU - two seconds of the GUEST's time - as a
    CEILING: it ends as soon as the UI task has finished with everything it
    was given (os88qemu.ui_done), which after most gestures is a tick or two.
    A caller whose next step needs a WORKER or the network to have moved
    waits on that state; this only promises the UI."""
    os88qemu.ui_done(m, S, cap=2.0)


# --- QEMU's sendkey names for the characters a URL needs ---------------------
KEYS = {".": "dot", "/": "slash", ":": "shift-semicolon", "-": "minus",
        "_": "shift-minus", "~": "shift-grave_accent", "?": "shift-slash",
        "=": "equal", "&": "shift-7", " ": "spc"}


def type_url(text):
    cmds = []
    for ch in text:
        if ch.isalnum():
            k = ch.lower() if ch.isalpha() else ch
            cmds += ["sendkey " + ("shift-" + k if ch.isupper() else k)]
        elif ch in KEYS:
            cmds += ["sendkey " + KEYS[ch]]
        else:
            sys.exit("ethernet: no sendkey mapping for %r" % ch)
        cmds += ["gsleep 0.06"]         # the guest's time (tools/qmp.py)
    subprocess.run(["python3", "tools/qmp.py", SOCK] + cmds,
                   check=True, capture_output=True)


class Server(threading.Thread):
    """One page, and it REMEMBERS THE REQUEST - which is assertion 3."""

    def __init__(self, page=PAGE, chunk_size=0, chunk_delay=0.0,
                 random_chunks_seed=None, cookie_fixture=False,
                 form_frame_fixture=False, non_html_fixture=False,
                 status_fixture=None, target_fixture=False,
                 charset_fixture=False, multi_target_fixture=False,
                 cookie_path_fixture=False, password_fixture=False,
                 password_get_fixture=False):
        threading.Thread.__init__(self, daemon=True)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("0.0.0.0", PORT))
        self.s.listen(4)
        self.request = None
        self.requests = []
        self.page = page
        self.chunk_size = chunk_size
        self.chunk_delay = chunk_delay
        self.random_chunks_seed = random_chunks_seed
        self.cookie_fixture = cookie_fixture
        self.form_frame_fixture = form_frame_fixture
        self.non_html_fixture = non_html_fixture
        self.status_fixture = status_fixture
        self.target_fixture = target_fixture
        self.charset_fixture = charset_fixture
        self.multi_target_fixture = multi_target_fixture
        self.cookie_path_fixture = cookie_path_fixture
        self.password_fixture = password_fixture
        self.password_get_fixture = password_get_fixture

    def run(self):
        while True:
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
                if self.password_fixture and b"\r\n\r\n" in req:
                    head, body = req.split(b"\r\n\r\n", 1)
                    match = re.search(rb"(?im)^Content-Length:\s*(\d+)", head)
                    needed = int(match.group(1)) if match else 0
                    while len(body) < needed:
                        b = c.recv(min(1024, needed - len(body)))
                        if not b:
                            break
                        body += b
                    req = head + b"\r\n\r\n" + body
                if self.request is None:
                    self.request = req
                self.requests.append(req)
                target_path = req.split(b"\r\n", 1)[0].split()[1]
                if self.password_fixture and target_path == b"/eth.htm":
                    method = b"get" if self.password_get_fixture else b"post"
                    body = (b'<html><body><form method="' + method
                            + b'" action="/capture">'
                            b'<input type="password" name="secret" value="seed" size="12">'
                            b'<input type="submit" name="go" value="Send">'
                            b'</form></body></html>')
                    response = (b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                                b"Content-Length: %d\r\n\r\n" % len(body) + body)
                elif self.password_fixture:
                    body = b"<html><body>POST-OK</body></html>"
                    response = (b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                                b"Content-Length: %d\r\n\r\n" % len(body) + body)
                elif self.cookie_path_fixture and len(self.requests) == 1:
                    response = (b"HTTP/1.0 302 Found\r\n"
                                b"Location: /cookie-next.htm\r\n"
                                b"Set-Cookie: session=xt; Path=\"/restricted\"\r\n"
                                b"Content-Length: 0\r\n\r\n")
                elif self.multi_target_fixture:
                    if target_path in (b"/eth.htm", b"/multi.htm"):
                        body = (b'<html><frameset cols="50%,50%">'
                                b'<frame src="/left.htm" name="left">'
                                b'<frame src="/right.htm" name="right">'
                                b'</frameset></html>')
                    elif target_path == b"/left.htm":
                        body = (b'<html><head><base target="right"></head>'
                                b'<body><a href="/target.htm">CHANGE RIGHT</a>'
                                b'</body></html>')
                    elif target_path == b"/target.htm":
                        body = b'<html><body>TARGET-CONTENT</body></html>'
                    else:
                        body = b'<html><body>RIGHT-OLD</body></html>'
                    response = (b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                                b"Content-Length: %d\r\n\r\n" % len(body) + body)
                elif (self.target_fixture
                        and (len(self.requests) == 1
                             or target_path == b"/top.htm")):
                    body = (b"<html><frameset cols=\"100%\"><frame "
                            b"src=\"/child.htm\" name=\"child\"></frameset></html>")
                    response = (b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                                b"Content-Length: %d\r\n\r\n" % len(body) + body)
                elif self.target_fixture:
                    body = (b"<html><head><base target=\"_top\"></head><body>"
                            b"<a href=\"/top.htm\">Top</a><br>"
                            b"<a href=\"/self.htm\" target=\"_self\">Self</a><br>"
                            b"<a href=\"/named.htm\" target=\"child\">Named</a><br>"
                            b"<a href=\"/missing.htm\" target=\"not_a_frame\">Missing target</a>"
                            b"</body></html>")
                    response = (b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                                b"Content-Length: %d\r\n\r\n" % len(body) + body)
                elif self.charset_fixture:
                    body = (b"<html><head><meta charset=iso-8859-1></head><body>"
                            + b"A" * 2070
                            + b"<p>HEADER-UTF8 caf\xc3\xa9</p>"
                            + b"<p>ENTITY-LATIN: &#227; &#231; &atilde; &Aacute; &euml;</p>"
                            + b"<p>ENTITY-EXTRA: &agrave; &Atilde; &Yacute; &thorn; &THORN; &bull; &hellip; &lsquo; &rdquo; &dagger; &permil; &iexcl; &uuml; &euro;</p>"
                            + b"<p>ENTITY-ACCENT: " + b" ".join(b"&" + name.encode("ascii") + b";" for name, _ in ENTITY_ACCENTS) + b"</p>"
                            + b"<p>CRLF-LEFT\r\nCRLF-RIGHT</p>"
                            + b"</body></html>")
                    response = (b"HTTP/1.0 200 OK\r\n"
                                b"Content-Type: text/html; charset=\"UTF-8\"\r\n"
                                b"Content-Length: %d\r\n\r\n" % len(body) + body)
                elif self.status_fixture in (204, 304) and len(self.requests) == 1:
                    reason = b"No Content" if self.status_fixture == 204 else b"Not Modified"
                    response = (b"HTTP/1.0 %d " % self.status_fixture + reason
                                + b"\r\nContent-Length: 0\r\n\r\n")
                elif self.status_fixture == 404 and len(self.requests) == 1:
                    body = b"<html><body>NOTFOUND-BODY</body></html>"
                    response = (b"HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\n"
                                b"Content-Length: %d\r\n\r\n" % len(body) + body)
                elif self.non_html_fixture and len(self.requests) == 1:
                    body = b"\x00\x01\xffBINARY-MUST-NOT-BE-HTML\x00"
                    response = (b"HTTP/1.0 200 OK\r\n"
                                b"Content-Type: application/octet-stream\r\n"
                                b"Content-Length: %d\r\n\r\n" % len(body)
                                + body)
                elif self.cookie_fixture and len(self.requests) == 1:
                    response = (b"HTTP/1.0 302 Found\r\n"
                                b"Location: /cookie-next.htm#cookie-done\r\n"
                                b"Set-Cookie: session=xt; Path=/\r\n"
                                b"Content-Length: 0\r\n\r\n")
                elif self.form_frame_fixture and target_path == b"/eth.htm":
                    body = (b"<html><frameset cols=\"100%\"><frame "
                            b"src=\"/child.htm\" name=\"child\"></frameset></html>")
                    response = (b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                                b"Content-Length: %d\r\n\r\n" % len(body) + body)
                elif self.form_frame_fixture:
                    body = (b"<html><head><base href=\"/childbase/\"></head>"
                            b"<body><form action=\"send\"><input type=\"submit\" "
                            b"name=\"go\" value=\"Send\"></form></body></html>")
                    response = (b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                                b"Content-Length: %d\r\n\r\n" % len(body) + body)
                else:
                    response = (b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                                b"Content-Length: %d\r\n\r\n" % len(self.page)
                                + self.page)
                if self.random_chunks_seed is not None:
                    import random
                    rng = random.Random(self.random_chunks_seed)
                    pos = 0
                    while pos < len(response):
                        n = rng.randint(1, 257)
                        c.sendall(response[pos:pos + n])
                        pos += n
                        if self.chunk_delay:
                            time.sleep(self.chunk_delay)
                elif self.chunk_size > 0 and self.chunk_delay > 0:
                    for pos in range(0, len(response), self.chunk_size):
                        c.sendall(response[pos:pos + self.chunk_size])
                        time.sleep(self.chunk_delay)
                else:
                    c.sendall(response)
            except OSError:
                pass
            finally:
                c.close()


def stale(img, src):
    return os.path.getmtime(img) < os.path.getmtime(src)


MC_SIZE = os88geom.MC_SIZE
MEM_MAX = 32


def claims(m, S):
    """Every live heap claim: (base, paragraphs, owner, dma, rloc, hi).

    HI IS MC_DMA's BIT 15 (MC_DMA_HI), and the record is os88geom's MC_SIZE.
    It was a byte of its own at +10 until the record shrank to ten bytes, and
    this row, unregistered then, went on reading byte 10 and died on an
    IndexError nobody saw. The layout comes from the kernel's own equates."""
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    out = []
    for i in range(MEM_MAX):
        r = raw[i * MC_SIZE:(i + 1) * MC_SIZE]
        if u16(r, 0):
            dma = u16(r, 6)
            out.append((u16(r, 0), u16(r, 2), u16(r, 4),
                        dma & os88geom.MC_DMA_HEAD, u16(r, 8),
                        1 if dma & os88geom.MC_DMA_HI else 0))
    return sorted(out)


def browser_state(m, pseg, img):
    """The page arenas and their logical usage, from the app's own BSS."""
    def word(offset):
        return u16(m.readseg(pseg, img + offset, 2))
    # br_srclen_hi is appended after the Browser's current BSS records. Resolve
    # it from NASM's map instead of pinning a second copy of its moving offset
    # in this test.
    offsets = {}
    with open("build/browser.map", encoding="ascii") as f:
        for line in f:
            fields = line.split()
            if len(fields) >= 3 and fields[-1] in (
                    "br_srclen_hi", "br_srcpos", "br_lnkn", "br_lnkseg",
                    "br_lnkoff", "br_lnktoff", "br_framenameoff",
                    "br_frametargetoff", "br_linecap", "br_doclen_hi",
                    "br_parse_started", "br_skipraw", "br_docfull",
                    "br_anchorcount", "br_anchorname", "br_anchorlo",
                    "br_top",
                    "br_cookievalid", "br_cookievalue", "br_cookiehost",
                    "br_host", "br_httpcharsetseen", "br_http_utf8"):
                offsets[fields[-1]] = int(fields[0], 16) - img
    if any(name not in offsets or offsets[name] < 0 for name in
           ("br_srclen_hi", "br_srcpos", "br_lnkn", "br_lnkseg",
            "br_lnkoff", "br_lnktoff", "br_framenameoff",
            "br_frametargetoff", "br_linecap", "br_doclen_hi",
            "br_parse_started", "br_skipraw", "br_docfull",
            "br_anchorcount", "br_anchorname", "br_anchorlo", "br_top")):
        raise RuntimeError("Browser logical source symbols are missing from build/browser.map")
    source_length = word(BR_SRCLEN) | (word(offsets["br_srclen_hi"]) << 16)
    parser_position = (word(offsets["br_srcpos"])
                       | (word(offsets["br_srcpos"] + 2) << 16))
    link_count = word(offsets["br_lnkn"])
    link_segment = word(offsets["br_lnkseg"])
    root_base_offset = None
    with open("build/browser.map", encoding="ascii") as f:
        for line in f:
            fields = line.split()
            if len(fields) >= 3 and fields[-1] == "br_rootbaseoff":
                root_base_offset = int(fields[0], 16) - img
                break
    if root_base_offset is None:
        raise RuntimeError("Browser root BASE HREF symbol is missing from build/browser.map")
    if root_base_offset < 0:
        raise RuntimeError("Browser root BASE HREF symbol is outside the package image")
    base_offset = word(root_base_offset)
    base_href = None
    if link_segment and base_offset != 0xFFFF:
        base_href = m.readseg(link_segment, base_offset, 128).split(b"\0", 1)[0].decode(
            "ascii", errors="replace")
    link_hrefs = []
    link_targets = []
    if link_segment:
        for i in range(link_count):
            link_offset = word(offsets["br_lnkoff"] + i * 2)
            raw = m.readseg(link_segment, link_offset, 64)
            link_hrefs.append(raw.split(b"\0", 1)[0].decode(
                "ascii", errors="replace"))
            target_offset = word(offsets["br_lnktoff"] + i * 2)
            if target_offset == 0xFFFF:
                link_targets.append(None)
            else:
                target = m.readseg(link_segment, target_offset, 64)
                link_targets.append(target.split(b"\0", 1)[0].decode(
                    "ascii", errors="replace"))
    frame_names = []
    frame_targets = []
    if link_segment:
        for i in range(8):
            name_offset = word(offsets["br_framenameoff"] + i * 2)
            target_offset = word(offsets["br_frametargetoff"] + i * 2)
            frame_names.append(None if name_offset == 0xFFFF else
                               m.readseg(link_segment, name_offset, 64)
                               .split(b"\0", 1)[0].decode("ascii", errors="replace"))
            frame_targets.append(None if target_offset == 0xFFFF else
                                 m.readseg(link_segment, target_offset, 64)
                                 .split(b"\0", 1)[0].decode("ascii", errors="replace"))
    anchor_count = m.readseg(pseg, img + offsets["br_anchorcount"], 1)[0]
    anchors = []
    if link_segment:
        for i in range(anchor_count):
            off = word(offsets["br_anchorname"] + i * 2)
            name = m.readseg(link_segment, off, 64).split(b"\0", 1)[0]
            lo = word(offsets["br_anchorlo"] + i * 2)
            hi = word(offsets["br_anchorlo"] + 64 + i * 2)
            anchors.append((off, name.decode("ascii", errors="replace"),
                            (hi << 16) | lo))
    return {
        "source_segment": word(BR_SRCSEG),
        "top_line": word(offsets["br_top"]),
        "cookie_valid": m.readseg(pseg, img + offsets["br_cookievalid"], 1)[0],
        "cookie_value": m.readseg(pseg, img + offsets["br_cookievalue"], 96)
            .split(b"\0", 1)[0].decode("ascii", errors="replace"),
        "cookie_host": m.readseg(pseg, img + offsets["br_cookiehost"], 64)
            .split(b"\0", 1)[0].decode("ascii", errors="replace"),
        "http_charset_seen": m.readseg(pseg, img + offsets["br_httpcharsetseen"], 1)[0],
        "http_utf8": m.readseg(pseg, img + offsets["br_http_utf8"], 1)[0],
        "request_host": m.readseg(pseg, img + offsets["br_host"], 64)
            .split(b"\0", 1)[0].decode("ascii", errors="replace"),
        "source_capacity_kb": word(BR_SRCKB),
        "source_length": source_length,
        "parser_source_position": parser_position,
        "parser_started": m.readseg(pseg, img + offsets["br_parse_started"], 1)[0],
        "raw_skip": m.readseg(pseg, img + offsets["br_skipraw"], 1)[0],
        "document_full": m.readseg(pseg, img + offsets["br_docfull"], 1)[0],
        "link_count": link_count,
        "link_hrefs": link_hrefs,
        "link_targets": link_targets,
        "frame_names": frame_names,
        "frame_targets": frame_targets,
        "base_href": base_href,
        "anchors": anchors,
        "document_segment": word(BR_DOCSEG),
        "document_capacity_kb": word(BR_DOCKB),
        "document_length": (word(BR_DOCLEN)
                             | (word(offsets["br_doclen_hi"]) << 16)),
        "display_lines": word(BR_NLINES),
        "line_capacity_bytes": word(offsets["br_linecap"]),
        "truncated": m.readseg(pseg, img + BR_TRUNC, 1)[0],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=None)
    ap.add_argument("--page", default=None,
                    help="serve this HTML body to the NE2000 guest instead of the small default")
    ap.add_argument("--stream-fixture", action="store_true",
                    help="serve a generated 7 KiB HTML parser-boundary fixture")
    ap.add_argument("--cookie-fixture", action="store_true",
                    help="verify Set-Cookie is sent on a same-host redirect")
    ap.add_argument("--cookie-path-fixture", action="store_true",
                    help="verify a Path-scoped cookie is withheld outside its path")
    ap.add_argument("--form-base-fixture", action="store_true",
                    help="click a GET form and verify ACTION resolves against BASE HREF")
    ap.add_argument("--frame-form-base-fixture", action="store_true",
                    help="verify a child-frame form uses its own BASE HREF")
    ap.add_argument("--form-reset-fixture", action="store_true",
                    help="verify both reset controls restore defaults without submitting")
    ap.add_argument("--password-fixture", action="store_true",
                    help="verify masked password display and POST-only submission")
    ap.add_argument("--password-get-fixture", action="store_true",
                    help="verify password controls in GET forms are refused")
    ap.add_argument("--non-html-fixture", action="store_true",
                    help="verify a declared binary response is drained, not parsed as HTML")
    ap.add_argument("--target-fixture", action="store_true",
                    help="verify per-frame BASE TARGET and explicit target precedence")
    ap.add_argument("--multi-target-fixture", action="store_true",
                    help="verify BASE TARGET selects a named child in a multi-frame page")
    ap.add_argument("--charset-header-fixture", action="store_true",
                    help="verify HTTP UTF-8 charset overrides conflicting META charset")
    ap.add_argument("--http-status-fixture", type=int, choices=(204, 304, 404),
                    help="verify empty 204/304 handling or an HTML 404 response")
    ap.add_argument("--response-chunk-size", type=int, default=0,
                    help="send a large test response in chunks of this many bytes")
    ap.add_argument("--response-delay-ms", type=float, default=0,
                    help="pause this many milliseconds between response chunks")
    ap.add_argument("--random-response-chunks", type=int, default=None,
                    help="split the response into deterministic 1..257-byte chunks using this seed")
    ap.add_argument("--expect-incremental", action="store_true",
                    help="require the parser to advance before the server finishes sending")
    ap.add_argument("--snapshot", default=None,
                    help="write before/after heap-claim snapshots as JSON")
    ap.add_argument("--expect-source-bytes", type=int, default=None,
                    help="assert an intentional source cutoff for a large-page test")
    ap.add_argument("--expect-source-capacity-kb", type=int, default=None,
                    help="assert the Browser's final source claim capacity")
    ap.add_argument("--expect-lines-at-least", type=int, default=None,
                    help="assert that layout reached this many display lines")
    ap.add_argument("--expect-document-sentinel", default=None,
                    help="assert these ASCII bytes survived formatting in the guest")
    ap.add_argument("--allow-source-cutoff", action="store_true",
                    help="accept a safe-memory cutoff and verify the retained prefix")
    ap.add_argument("--keep", action="store_true",
                    help="leave QEMU running for a look afterwards")
    a = ap.parse_args()
    fails = []

    # **THE SYSTEM IMAGE IS REBUILT, NOT CHECKED.** QEMU mounts it WRITABLE and
    # the OS writes to it: the Setup window's `ETHER.CFG` (SPEC.md 72.7) is
    # exactly the file this test must not find, because a manual address left
    # behind by tests/ethcfg.py would make this one pass while proving nothing
    # about DHCP. Staleness is not the hazard here - a dirty image is - and
    # `make` cannot see the difference, because the guest's write leaves the
    # image NEWER than everything it was built from.
    if os.path.exists(os88build.at("build/ether360.img")):
        os.remove("build/ether360.img")
    r = subprocess.run(["make", "ethertest"], capture_output=True, text=True)
    if r.returncode:
        sys.exit("ethernet: make ethertest failed:\n" + r.stdout + r.stderr)

    img, src = "build/brtest360.img", "build/browser.o88"
    if not os.path.exists(img):
        sys.exit("ethernet: no %s - run `make browsertest`" % img)
    if stale(img, src):
        sys.exit("ethernet: %s is OLDER than %s - run `make browsertest` (this "
                 "would have tested the wrong binary)" % (img, src))

    syms = ether_syms()
    for n in ("eth_base", "eth_mac", "eth_ip", "eth_gw", "eth_dns", "dhcp_st",
              "eth_nrx", "eth_ntx", "eth_nseg", "eth_ndrop", "eth_novw"):
        if n not in syms:
            sys.exit("ethernet: %s is not in the map" % n)

    if sum(bool(x) for x in (a.page, a.stream_fixture, a.cookie_fixture,
                             a.cookie_path_fixture,
                             a.form_base_fixture, a.frame_form_base_fixture,
                             a.form_reset_fixture, a.password_fixture,
                             a.password_get_fixture,
                             a.non_html_fixture, a.http_status_fixture is not None,
                             a.target_fixture, a.multi_target_fixture,
                             a.charset_header_fixture)) > 1:
        sys.exit("ethernet: choose only one page fixture")
    if a.stream_fixture:
        page = (b'<html><head><meta charset=utf-8><base href="/base/">'
                b'<title>Stream title</title>'
                b'</head><body><a href="#tail">GOTO TAIL</a>'
                b'<p id="top">' + b'A' * 1950 + b'</p><!--' + b'C' * 80
                + b'--><p>caf\xc3\xa9</p><p>' + b'B' * 5000
                + b'</p><script><p>SCRIPT-MUST-STAY-HIDDEN</p>' + b'Z' * 70
                + b'</script><a name="tail">TAIL-SENTINEL</a>'
                + b'<p>STREAMDONE &amp; END</p></body></html>'
                b'<broken')
    elif a.cookie_fixture:
        page = (b"<html><body>COOKIEOK<br>LINE2<br>LINE3<br>LINE4<br>"
                b"LINE5<br>LINE6<br><a name=\"cookie-done\">DONE</a>"
                b"</body></html>")
    elif a.cookie_path_fixture:
        page = b"<html><body>PATH-COOKIE-OK</body></html>"
    elif a.form_base_fixture:
        page = (b"<html><head><base href=\"/base/\"></head><body>"
                b"<form action=\"send\"><button type=\"submit\" "
                b"name=\"go\" value=\"Send\">Send</button></form>"
                b"</body></html>")
    elif a.frame_form_base_fixture:
        page = b"<html><frameset cols=\"100%\"><frame src=\"/child.htm\" name=\"child\"></frameset></html>"
    elif a.form_reset_fixture:
        page = (b'<html><body><form action="/must-not-submit">'
                b'<label for="q">Query label</label><br>'
                b'<input name="q" id="q" value="original" size="12"><br>'
                b'<input type="checkbox" name="c" checked>Check<br>'
                b'<input type="radio" name="r" value="one" checked>One'
                b'<input type="radio" name="r" value="two">Two<br>'
                b'<select name="s"><option value="a">Alpha'
                b'<option value="b" selected>Beta</select><br>'
                b'<input type="reset" value="Reset input"><br>'
                b'<button type="reset">Reset button</button>'
                b'</form></body></html>')
    elif a.password_fixture or a.password_get_fixture:
        page = b""
    elif a.non_html_fixture:
        page = b""
    elif a.http_status_fixture is not None:
        page = b""
    elif a.target_fixture:
        page = b"<html><frameset cols=\"100%\"><frame src=\"/child.htm\" name=\"child\"></frameset></html>"
    elif a.multi_target_fixture:
        page = (b'<html><frameset cols="50%,50%">'
                b'<frame src="/left.htm" name="left">'
                b'<frame src="/right.htm" name="right">'
                b'</frameset></html>')
    elif a.charset_header_fixture:
        page = b""
    else:
        page = open(a.page, "rb").read() if a.page else PAGE
    if a.response_chunk_size < 0 or a.response_delay_ms < 0:
        sys.exit("ethernet: response chunk size and delay must be non-negative")
    srv = Server(page, a.response_chunk_size, a.response_delay_ms / 1000.0,
                 a.random_response_chunks, a.cookie_fixture,
                 a.frame_form_base_fixture, a.non_html_fixture,
                 a.http_status_fixture, a.target_fixture,
                 a.charset_header_fixture, a.multi_target_fixture,
                 a.cookie_path_fixture,
                 a.password_fixture or a.password_get_fixture,
                 a.password_get_fixture)
    srv.start()
    say("http server on %s, %d byte page%s"
        % (URL, len(page), " from " + a.page if a.page else ""))

    # A SURVIVOR KEEPS THE SOCKET, so the new machine cannot bind and every
    # read below would come from the OLD one - which reads as a change that
    # did nothing (CLAUDE.md). Kill it by PID out of the pidfile and never
    # with `pkill -f qemu`, whose pattern matches the calling shell.
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
    cmd = ["make", "test", "ETHER=1", "TESTIMG=build/ether360.img",
           "TESTAPPS=build/brtest360.img"]
    if os.environ.get("ETHDUMP"):       # every frame either way, to a pcap -
        cmd.append("ETHDUMP=" + os.environ["ETHDUMP"])   # see the Makefile
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit("ethernet: make test failed:\n" + r.stdout + r.stderr)

    m = Qemu()
    snapshots = {}
    try:
        # --- 1: a card, and an address ------------------------------------
        # EVERY WAIT BELOW IS IN THE GUEST'S SECONDS (tests/os88qemu.py) and on
        # the word the next line reads - the budgets are what the host-clock
        # loops they replace allowed on an idle box.
        def drvseg():
            return u16(m.read(S("drv_tab") + ETH_ROW * DRVR_SZ + DRVR_SEG, 2))
        os88qemu.acted(m, lambda: drvseg() != 0, secs=60,
                       what="ETHER.DRV's drv_tab row", poll=0.4)
        seg = drvseg()
        if not seg:
            m.quit()
            sys.exit("ethernet: ETHER.DRV never attached - no card was found "
                     "at any of its bases, or SYSTEM.CFG did not ask for it")
        say("ETHER.DRV at %04X" % seg)

        def db(name):
            return m.readseg(seg, syms[name], 1)[0]

        def dw(name):
            return u16(m.readseg(seg, syms[name], 2))

        def dip(name):
            return m.readseg(seg, syms[name], 4)

        # DRVR_SEG IS ARMED BEFORE THE IMAGE IS EVEN READ (drv_load: the claim
        # goes into the row so that drv_call works during attach), so the poll
        # above can catch the row a whole floppy read and a bus sweep ahead of
        # the card. A machine that really has none leaves this 0 for ever, so a
        # bounded wait still catches it and no longer reports the window.
        os88qemu.acted(m, lambda: dw("eth_base") != 0, secs=24,
                       what="[eth_base]", poll=0.4)
        base = dw("eth_base")
        mac = m.readseg(seg, syms["eth_mac"], 6)
        say("card at %04X, id %s" % (base, mac.hex()))
        if base == 0:
            fails.append("eth_base is 0: the driver attached with no card")
        if not any(mac) or all(b == 0xFF for b in mac):
            fails.append("the station address is %s, which is what a floating "
                         "bus reads as" % mac.hex())
        if mac[0] & 1:
            fails.append("the station address %s is MULTICAST, so it is not a "
                         "station address" % mac.hex())

        # --- 1b. THE RINGS, AND WHICH RUNG THE LADDER LANDED ON ------------
        # SPEC.md 72.13.2. The rung was chosen in silence and the field could
        # only GUESS which one it got - which is how a change that did nothing
        # and a change that never ran look the same. This machine has a whole
        # heap free at attach, so the ceiling is the only right answer, and a
        # floor here is the ladder failing rather than the machine refusing.
        skseg, rxcap, txcap = dw("sk_seg"), dw("sk_rxcap"), dw("sk_txcap")
        say("rings at %04X, rx %d tx %d" % (skseg, rxcap, txcap))
        if not skseg:
            fails.append("sk_seg is 0: the rings were never claimed, so every "
                         "socket verb is refusing")
        if rxcap != SK_RXMAX:
            fails.append("the receive ring is %d and not %d on a machine with "
                         "the heap empty at attach: the ladder stepped down "
                         "for a reason that is not memory (SPEC.md 72.13.2)"
                         % (rxcap, SK_RXMAX))
        if txcap != SK_TXMAX:
            fails.append("the send ring is %d and not %d: the TX size follows "
                         "the RX rung and these two have come apart"
                         % (txcap, SK_TXMAX))

        # --- 1c. ...AND THE POOL DECLARED ITSELF MOVABLE (SPEC.md 66.4.1) --
        # docs/plans/HEAP-UNPIN-PLAN.md 5.1. This is the largest single block
        # in 2.0's wall - 14KB of driver furniture claimed from the top and
        # pinned there for the session - and it was pinned by a comment
        # ("the card's own descriptors point into these rings and it DMAs into
        # them") that is false for an NE2000: the 8390's DMA engines are
        # internal, the host side is `in al, dx` / `stosb`, and this driver
        # hooks no vector at all.
        #
        # IT IS READ FROM mem_tab AND NOT FROM THE DRIVER, because that is the
        # only place a REFUSED declaration shows. mem_movable's fence is
        # "yours, or not at all", so a wrong owner returns CF=1, writes no
        # MC_RLOC, and looks identical from inside the driver - which is
        # exactly how SPEC.md 66.5.6.2's silent refusal went unnoticed for a
        # release.
        pool = [c for c in claims(m, S) if c[0] == skseg]
        if len(pool) != 1:
            fails.append("no claim in mem_tab starts at sk_seg %04X, so the "
                         "rings are not where the driver says they are"
                         % skseg)
        else:
            base_, para_, own_, dma_, rloc_, hi_ = pool[0]
            say("pool claim %04X %dKB owner %04X%s%s"
                % (base_, para_ // 64, own_,
                   " MOVABLE" if rloc_ else " PINNED",
                   " HI" if hi_ else " low"))
            if not rloc_:
                fails.append("the socket pool's MC_RLOC is 0: the "
                             "OSAPI_MEM_MOVABLE call in sk_claim was REFUSED "
                             "and the driver cannot tell (SPEC.md 66.5.6.2)")
            if not hi_:
                fails.append("the socket pool's MC_DMA_HI is 0: it came in "
                             "through the low door, so SPEC.md 66.4.1's "
                             "descending pass will never reach it")

        os88qemu.acted(m, lambda: db("dhcp_st") == DH_BOUND, secs=24,
                       what="DHCP bound", poll=0.3)     # DHCP_WAIT: 110 ticks
        st, ip, gw, dns = db("dhcp_st"), dip("eth_ip"), dip("eth_gw"), \
            dip("eth_dns")
        if db("eth_mode") != 0:
            fails.append("eth_mode is %d and not Automatic on a freshly built "
                         "image - an ETHER.CFG was left on the disk, so what "
                         "bound is not DHCP" % db("eth_mode"))
        say("dhcp %d, addr %s, router %s, name %s"
            % (st, dotted(ip), dotted(gw), dotted(dns)))
        if st != DH_BOUND:
            fails.append("dhcp_st is %d and not DH_BOUND: no address was "
                         "acquired, so nothing above it can have worked" % st)
        if dotted(ip) != "10.0.2.15":
            fails.append("the address is %s and slirp hands out 10.0.2.15"
                         % dotted(ip))
        if dotted(gw) != "10.0.2.2":
            fails.append("the router is %s and slirp's is 10.0.2.2"
                         % dotted(gw))
        if dotted(dns) != "10.0.2.3":
            fails.append("the name server is %s and slirp's is 10.0.2.3"
                         % dotted(dns))

        # --- 2: the browser, and it knows nothing about any of this -------
        dispcp.open_drive(m, Mouse(), S, settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, Mouse(), S, settle, wx, wy, "BROWSER.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            m.quit()
            sys.exit("ethernet: BROWSER.O88 did not open a window")
        bw = wins2[-1]
        rec = m.read(S("wm_wins") + bw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = u16(rec, 22)
        say("browser at %04X" % pseg)
        browser_image_size = os.path.getsize(os88build.at("build/browser.bin"))

        snapshots = {
            "browser_open": {
                "claims": claims(m, S),
                "browser": browser_state(m, pseg, browser_image_size),
            }
        }

        # **THE BAR IS ALREADY FOCUSED AND ALREADY HOLDS `http://`** (SPEC.md
        # 71.7): the browser opens it that way ON PURPOSE, because the scheme
        # is not optional (71.2) and a new reader who typed a bare host used to
        # get `Bad URL`. So there is nothing to click and nothing to retype -
        # what goes in is the URL WITHOUT its scheme, onto the caret the
        # browser has already parked at the end.
        #
        # Clicking it first is what this did before, and it was wrong twice
        # over: the click moved the caret into the MIDDLE of the `http://`
        # already there, and then the full URL was typed on top - so the bar
        # read `http:/http://10.0.2.2:8090/eth.htm/`, the browser refused it
        # correctly, and the gate reported the STACK as broken over a fetch
        # that was never asked for. Nothing below assertion 2 could pass.
        os88qemu.pace(m, 1.0)
        type_url(URL[len("http://"):])
        os88qemu.pace(m, 1.0)
        subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey ret"],
                       check=True, capture_output=True)

        if a.expect_incremental:
            # This must happen while the server still has response bytes to
            # send. A final-only parser would leave this cursor at zero.
            observed = {}
            def parser_advancing():
                state = browser_state(m, pseg, browser_image_size)
                net_state = m.readseg(pseg, browser_image_size + BR_NSTATE, 1)[0]
                active = (state["parser_started"] != 0
                          and state["parser_source_position"] > 0
                          and state["source_length"] < len(page)
                          and net_state not in (BN_DONE, BN_ERR))
                if active:
                    observed.update(state)
                    observed["net_state"] = net_state
                return active
            os88qemu.acted(m, parser_advancing, secs=90,
                           what="parser progress before HTTP EOF", poll=0.25)
            if not observed:
                fails.append("the response finished before the incremental parser advanced")
            else:
                say("incremental parser advanced to source byte %d before EOF "
                    "(network state %d; cursor and received length are sampled "
                    "separately)"
                    % (observed["parser_source_position"], observed["net_state"]))

        wait_secs = (300 if a.allow_source_cutoff else
                     90 if len(page) > 32 * 1024 else 30)
        os88qemu.acted(m, lambda: m.readseg(pseg, browser_image_size + BR_NSTATE, 1)[0]
                       in (BN_DONE, BN_ERR), secs=wait_secs, what="br_nstate",
                       poll=0.5)
        nstate = m.readseg(pseg, browser_image_size + BR_NSTATE, 1)[0]
        nlines = u16(m.readseg(pseg, browser_image_size + BR_NLINES, 2))
        # This is deliberately the byte count the guest actually retained,
        # not Content-Length from the host. It exposes a source cap that still
        # lets the short prefix render and report BN_DONE.
        bstate = browser_state(m, pseg, browser_image_size)
        source_len = bstate["source_length"]
        if a.target_fixture:
            deadline = time.time() + 10
            while len(srv.requests) < 2 and time.time() < deadline:
                time.sleep(0.1)
            by_href = dict(zip(bstate["link_hrefs"], bstate["link_targets"]))
            say("target fixture links: %r" % list(zip(bstate["link_hrefs"],
                                                        bstate["link_targets"])))
            expected_targets = {"/top.htm": "_top",
                                "/self.htm": "_self",
                                "/named.htm": "child",
                                "/missing.htm": "not_a_frame"}
            if any(by_href.get(href) != target
                   for href, target in expected_targets.items()):
                fails.append("BASE TARGET/explicit TARGET metadata differs: %r"
                             % by_href)
            if not bstate["frame_names"] or bstate["frame_names"][0] != "child":
                fails.append("FRAME NAME was not retained for target lookup: %r"
                             % bstate["frame_names"][:2])
            if not bstate["frame_targets"] or bstate["frame_targets"][0] != "_top":
                fails.append("child BASE TARGET was not retained: %r"
                             % bstate["frame_targets"][:2])
            if len(srv.requests) != 2:
                fails.append("target metadata fixture loaded %d documents, expected frameset+child"
                             % len(srv.requests))
            target_syms = {}
            with open("build/browser.map", encoding="ascii") as f:
                for line in f:
                    fields = line.split()
                    if len(fields) >= 3 and fields[-1] in ("br_tfocus", "br_tlinkidx"):
                        target_syms[fields[-1]] = int(fields[0], 16) - browser_image_size
            if any(n not in target_syms for n in ("br_tfocus", "br_tlinkidx")):
                fails.append("target keyboard state symbols are missing")
            else:
                def focus_link(index):
                    for _ in range(12):
                        m.key("Tab")
                        focus = m.readseg(pseg, browser_image_size
                                          + target_syms["br_tfocus"], 1)[0]
                        current = u16(m.readseg(pseg, browser_image_size
                                               + target_syms["br_tlinkidx"], 2))
                        if focus == 3 and current == index:
                            return True
                    return False

                if not focus_link(1):
                    fails.append("Tab did not reach the child BASE TARGET=_top link")
                else:
                    m.key("Enter")
                    deadline = time.time() + 15
                    while len(srv.requests) < 3 and time.time() < deadline:
                        time.sleep(0.1)
                    if len(srv.requests) < 3:
                        state = m.readseg(pseg, browser_image_size + BR_NSTATE, 1)[0]
                        msgptr = u16(m.readseg(pseg, browser_image_size + BR_NMSG, 2))
                        msg = m.readseg(pseg, msgptr, 48).split(b"\0", 1)[0]
                        fails.append("child BASE TARGET=_top did not navigate: "
                                     "state=%d message=%r" % (state, msg))
                    elif srv.requests[2].split(b"\r\n", 1)[0] != b"GET /top.htm HTTP/1.0":
                        fails.append("child BASE TARGET=_top fetched %r"
                                     % srv.requests[2].split(b"\r\n", 1)[0])
                    else:
                        deadline = time.time() + 15
                        while len(srv.requests) < 4 and time.time() < deadline:
                            time.sleep(0.1)
                        if len(srv.requests) < 4:
                            fails.append("_top frameset did not fetch its child")
                        elif not focus_link(2):
                            fails.append("Tab did not reach child TARGET=_self")
                        else:
                            m.key("Enter")
                            deadline = time.time() + 15
                            while len(srv.requests) < 5 and time.time() < deadline:
                                time.sleep(0.1)
                            if len(srv.requests) < 5:
                                fails.append("single-frame TARGET=_self did not navigate")
                            elif srv.requests[4].split(b"\r\n", 1)[0] != b"GET /top.htm HTTP/1.0":
                                fails.append("child TARGET=_self did not reload its frameset: %r"
                                             % srv.requests[4].split(b"\r\n", 1)[0])
                            else:
                                deadline = time.time() + 15
                                while len(srv.requests) < 6 and time.time() < deadline:
                                    time.sleep(0.1)
                                if (len(srv.requests) < 6
                                        or srv.requests[5].split(b"\r\n", 1)[0]
                                        != b"GET /self.htm HTTP/1.0"):
                                    fails.append("child TARGET=_self did not load the selected child: %r"
                                                 % [r.split(b"\r\n", 1)[0] for r in srv.requests])
                                elif not focus_link(3):
                                    fails.append("Tab did not reach the matching named-frame target")
                                else:
                                    m.key("Enter")
                                    deadline = time.time() + 15
                                    while len(srv.requests) < 8 and time.time() < deadline:
                                        time.sleep(0.1)
                                    if len(srv.requests) < 8:
                                        state = m.readseg(pseg, browser_image_size + BR_NSTATE, 1)[0]
                                        msgptr = u16(m.readseg(pseg, browser_image_size + BR_NMSG, 2))
                                        msg = m.readseg(pseg, msgptr, 48).split(b"\0", 1)[0]
                                        fails.append("matching child TARGET did not navigate: "
                                                     "state=%d message=%r" % (state, msg))
                                    elif ([r.split(b"\r\n", 1)[0] for r in srv.requests[6:8]]
                                          != [b"GET /top.htm HTTP/1.0",
                                              b"GET /named.htm HTTP/1.0"]):
                                        fails.append("matching child TARGET did not reload the selected frame: %r"
                                                     % [r.split(b"\r\n", 1)[0] for r in srv.requests[6:8]])
                                    elif not focus_link(4):
                                        fails.append("Tab did not reach the unmatched named target")
                                    else:
                                        m.key("Enter")
                                        deadline = time.time() + 15
                                        while len(srv.requests) < 9 and time.time() < deadline:
                                            time.sleep(0.1)
                                        if (len(srv.requests) < 9
                                                or srv.requests[8].split(b"\r\n", 1)[0]
                                                != b"GET /missing.htm HTTP/1.0"):
                                            state = m.readseg(pseg, browser_image_size + BR_NSTATE, 1)[0]
                                            msgptr = u16(m.readseg(pseg, browser_image_size + BR_NMSG, 2))
                                            msg = m.readseg(pseg, msgptr, 48).split(b"\0", 1)[0]
                                            fails.append("unmatched named TARGET did not fall back to top-level navigation: "
                                                         "state=%d message=%r requests=%r"
                                                         % (state, msg,
                                                            [r.split(b"\r\n", 1)[0]
                                                             for r in srv.requests]))
        if a.multi_target_fixture:
            deadline = time.time() + 15
            while len(srv.requests) < 3 and time.time() < deadline:
                time.sleep(0.1)
            request_lines = [r.split(b"\r\n", 1)[0] for r in srv.requests]
            say("multi-target links: %r; frames: %r"
                % (list(zip(bstate["link_hrefs"], bstate["link_targets"])),
                   bstate["frame_names"]))
            if request_lines[:3] != [b"GET /eth.htm HTTP/1.0",
                                     b"GET /left.htm HTTP/1.0",
                                     b"GET /right.htm HTTP/1.0"]:
                fails.append("multi-frame fixture did not load both children: %r"
                             % request_lines)
            target_syms = {}
            with open("build/browser.map", encoding="ascii") as f:
                for line in f:
                    fields = line.split()
                    if len(fields) >= 3 and fields[-1] in ("br_tfocus", "br_tlinkidx"):
                        target_syms[fields[-1]] = int(fields[0], 16) - browser_image_size
            if any(n not in target_syms for n in ("br_tfocus", "br_tlinkidx")):
                fails.append("multi-target keyboard state symbols are missing")
            else:
                selected = False
                for _ in range(12):
                    m.key("Tab")
                    focus = m.readseg(pseg, browser_image_size
                                      + target_syms["br_tfocus"], 1)[0]
                    current = u16(m.readseg(pseg, browser_image_size
                                           + target_syms["br_tlinkidx"], 2))
                    if focus == 3 and current == 2:
                        selected = True
                        break
                if not selected:
                    fails.append("Tab did not skip both frame sources to the child link")
                else:
                    m.key("Enter")
                    deadline = time.time() + 20
                    while len(srv.requests) < 6 and time.time() < deadline:
                        time.sleep(0.1)
                    request_lines = [r.split(b"\r\n", 1)[0] for r in srv.requests]
                    expected = [b"GET /eth.htm HTTP/1.0",
                                b"GET /left.htm HTTP/1.0",
                                b"GET /right.htm HTTP/1.0",
                                b"GET /eth.htm HTTP/1.0",
                                b"GET /left.htm HTTP/1.0",
                                b"GET /target.htm HTTP/1.0"]
                    if request_lines[:6] != expected:
                        state = m.readseg(pseg, browser_image_size + BR_NSTATE, 1)[0]
                        msgptr = u16(m.readseg(pseg, browser_image_size + BR_NMSG, 2))
                        msg = m.readseg(pseg, msgptr, 48).split(b"\0", 1)[0]
                        debug_syms = {}
                        with open("build/browser.map", encoding="ascii") as f:
                            for line in f:
                                fields = line.split()
                                if len(fields) >= 3 and fields[-1] in (
                                        "br_frameoverrideurl", "br_frameurl",
                                        "br_frameoverrideidx", "br_frameoverridevalid",
                                        "br_targetframe"):
                                    debug_syms[fields[-1]] = int(fields[0], 16) - browser_image_size
                        debug = {}
                        for name, off in debug_syms.items():
                            size = 80 if name.endswith("url") else 1
                            data = m.readseg(pseg, browser_image_size + off, size)
                            debug[name] = (data.split(b"\0", 1)[0] if size == 80 else data[0])
                        say("multi-target debug state: %r" % debug)
                        fails.append("BASE TARGET did not replace named child while retaining frameset: requests=%r state=%d message=%r"
                                     % (request_lines, state, msg))
        say("br_nstate = %d, br_nlines = %d, br_srclen = %d"
            % (nstate, nlines, source_len))
        no_page_error = (a.non_html_fixture
                         or a.http_status_fixture in (204, 304))
        if no_page_error and (nstate != BN_ERR or source_len != 0):
            fails.append("declared non-HTML response was retained or parsed: "
                         "state=%d source=%d" % (nstate, source_len))
        if a.http_status_fixture == 404 and (nstate != BN_DONE or source_len == 0):
            fails.append("HTML 404 body was not retained as a navigable error page: "
                         "state=%d source=%d" % (nstate, source_len))
        if a.charset_header_fixture:
            if nstate != BN_DONE or source_len < 2048:
                fails.append("HTTP charset fixture did not complete the incremental page: "
                             "state=%d source=%d" % (nstate, source_len))
            if bstate["http_charset_seen"] != 1 or bstate["http_utf8"] != 1:
                fails.append("Content-Type UTF-8 parameter was not retained: %r, %r"
                             % (bstate["http_charset_seen"], bstate["http_utf8"]))
            dlen = bstate["document_length"]
            doc = bytes(m.readseg(bstate["document_segment"], 0, dlen))
            if b"ENTITY-LATIN: a c a A e" not in doc:
                fails.append("numeric/named Latin-1 entities were not folded "
                             "under UTF-8: %r" % doc[-160:])
            if b"ENTITY-EXTRA: a A Y p P * . ' \" + % ! u ?" not in doc:
                fails.append("expanded classic entity references were not folded correctly: %r" % doc[-240:])
            accent_expected = b"ENTITY-ACCENT: " + b" ".join(value.encode("ascii") for _, value in ENTITY_ACCENTS)
            if accent_expected not in doc:
                fails.append("the complete case-sensitive Latin-1 accent set did not fold: %r" % doc[-500:])
            if b"CRLF-LEFT CRLF-RIGHT" not in doc:
                fails.append("CRLF inside P did not collapse to one HTML space: %r"
                             % doc[-200:])
        snapshots["after_fetch"] = {
            "claims": claims(m, S),
            "browser": bstate,
            "br_nstate": nstate,
            "br_nlines": nlines,
            "br_srclen": source_len,
            "response_bytes": len(page),
        }
        if (a.expect_source_bytes is not None
                and source_len != a.expect_source_bytes):
            fails.append("the guest retained %d source bytes, expected %d"
                         % (source_len, a.expect_source_bytes))
        if (a.expect_source_capacity_kb is not None
                and bstate["source_capacity_kb"] != a.expect_source_capacity_kb):
            fails.append("the source claim is %d KiB, expected %d KiB"
                         % (bstate["source_capacity_kb"],
                            a.expect_source_capacity_kb))
        if (a.expect_lines_at_least is not None
                and nlines < a.expect_lines_at_least):
            fails.append("layout reached %d lines, expected at least %d"
                         % (nlines, a.expect_lines_at_least))
        if a.expect_document_sentinel is not None:
            needle = a.expect_document_sentinel.encode("ascii")
            dseg = bstate["document_segment"]
            dlen = bstate["document_length"]
            chunks = []
            for pos in range(0, dlen, 0x4000):
                count = min(0x4000, dlen - pos)
                seg = (dseg + (pos >> 4)) & 0xFFFF
                chunks.append(m.readseg(seg, pos & 15, count))
            formatted = b"".join(chunks)
            if needle not in formatted:
                fails.append("formatted document is %d bytes but does not contain %r; tail=%r, raw_skip=%d, full=%d, parser=%d"
                             % (dlen, needle, formatted[-80:],
                                bstate["raw_skip"], bstate["document_full"],
                                bstate["parser_source_position"]))
            if a.stream_fixture:
                if bstate["base_href"] != "http://10.0.2.2:%d/base/" % PORT:
                    fails.append("BASE HREF resolved to %r, expected the active host and /base/"
                                 % bstate["base_href"])
                anchor_names = [name for _, name, _ in bstate["anchors"]]
                if "top" not in anchor_names or "tail" not in anchor_names:
                    fails.append("NAME/ID anchors were not indexed: %r" % bstate["anchors"])
                elif dict((name, pos) for _, name, pos in bstate["anchors"])["tail"] <= \
                        dict((name, pos) for _, name, pos in bstate["anchors"])["top"]:
                    fails.append("the later NAME anchor did not retain a later document offset: %r"
                                 % bstate["anchors"])
                if "#tail" not in bstate["link_hrefs"]:
                    fails.append("fragment link was not retained in the link table")
                if b"cafe" not in formatted:
                    fails.append("a UTF-8 character split across receives did not fold to 'cafe'")
                if b"SCRIPT-MUST-STAY-HIDDEN" in formatted:
                    fails.append("raw SCRIPT contents were parsed as document text")
                if b"<broken" not in formatted:
                    fails.append("an unfinished final tag was discarded instead of kept as text")
        if a.cookie_fixture:
            if len(srv.requests) != 2:
                fails.append("cookie redirect made %d HTTP requests, expected two"
                             % len(srv.requests))
            elif b"Cookie: session=xt" not in srv.requests[1]:
                fails.append("same-host redirect omitted Set-Cookie (guest valid=%d value=%r host=%r request-host=%r): %r"
                             % (bstate["cookie_valid"], bstate["cookie_value"],
                                bstate["cookie_host"], bstate["request_host"],
                                srv.requests[1].split(b"\r\n\r\n", 1)[0]))
            if len(srv.requests) == 2:
                request_line = srv.requests[1].split(b"\r\n", 1)[0]
                if request_line != b"GET /cookie-next.htm HTTP/1.0":
                    fails.append("fragment was not stripped from the HTTP request line: %r"
                                 % request_line)
            if bstate["top_line"] == 0:
                fails.append("redirect URL fragment did not move the viewport to its anchor")
        if a.cookie_path_fixture:
            if len(srv.requests) != 2:
                fails.append("Path cookie redirect made %d HTTP requests, expected two"
                             % len(srv.requests))
            elif b"Cookie: session=xt" in srv.requests[1]:
                fails.append("Path=/restricted cookie leaked to /cookie-next.htm")
        if a.form_base_fixture or a.frame_form_base_fixture:
            # The single submit control is at the start of this small page.
            # Locate its actual document line and cell from the Browser's line
            # arena, then click through the normal GUI path.
            browser_offsets = {}
            with open("build/browser.map", encoding="ascii") as f:
                for line in f:
                    fields = line.split()
                    if len(fields) >= 3 and fields[-1] in (
                            "br_subs", "br_lseg", "br_cx", "br_cy", "br_top"):
                        browser_offsets[fields[-1]] = int(fields[0], 16) - browser_image_size
            if any(name not in browser_offsets for name in
                   ("br_subs", "br_lseg", "br_cx", "br_cy", "br_top")):
                fails.append("form-base fixture could not locate Browser layout symbols")
            else:
                dseg = bstate["document_segment"]
                subofs = u16(m.readseg(pseg, browser_image_size
                                       + browser_offsets["br_subs"], 2))
                subhi = u16(m.readseg(pseg, browser_image_size
                                      + browser_offsets["br_subs"] + 2, 2))
                target = (subhi << 16) | subofs
                if a.form_base_fixture:
                    target += 1       # click inside the text, after the opening cell
                lseg = u16(m.readseg(pseg, browser_image_size
                                     + browser_offsets["br_lseg"], 2))
                row = None
                col = 0
                for i in range(nlines):
                    rec = m.readseg(lseg, i * 10, 10)
                    lo = u16(rec, 0) | (u16(rec, 2) << 16)
                    hi = u16(rec, 4) | (u16(rec, 6) << 16)
                    if lo <= target < hi:
                        row = i
                        for pos in range(lo, target):
                            ch = m.readseg(dseg + (pos >> 4), pos & 15, 1)[0]
                            if 0x20 <= ch <= 0x7E:
                                col += 1
                        col += rec[8]
                        break
                if row is None:
                    fails.append("submit button offset %d is not on a display line" % target)
                else:
                    cx = u16(m.readseg(pseg, browser_image_size
                                       + browser_offsets["br_cx"], 2))
                    cy = u16(m.readseg(pseg, browser_image_size
                                       + browser_offsets["br_cy"], 2))
                    top = u16(m.readseg(pseg, browser_image_size
                                        + browser_offsets["br_top"], 2))
                    if a.frame_form_base_fixture:
                        deadline = time.time() + 10
                        while len(srv.requests) < 2 and time.time() < deadline:
                            time.sleep(0.1)
                    Mouse().click(cx + col * 8 + 3, cy + (row - top) * 8 + 3)
                    deadline = time.time() + 15
                    expected_count = 4 if a.frame_form_base_fixture else 2
                    while len(srv.requests) < expected_count and time.time() < deadline:
                        time.sleep(0.1)
                    if len(srv.requests) < expected_count:
                        fails.append("clicking submit did not issue a second request")
                    else:
                        request_line = srv.requests[-1].split(b"\r\n", 1)[0]
                        expected_line = (b"GET /childbase/send?go=Send HTTP/1.0"
                                         if a.frame_form_base_fixture else
                                         b"GET /base/send?go=Send HTTP/1.0")
                        if request_line != expected_line:
                            fails.append("form ACTION ignored its BASE HREF: %r"
                                         % request_line)
        if a.form_reset_fixture:
            names = ("br_subs", "br_flds", "br_fn", "br_subn", "br_fcur",
                     "br_lseg", "br_cx", "br_cy", "br_top")
            offsets = {}
            with open("build/browser.map", encoding="ascii") as f:
                for line in f:
                    fields = line.split()
                    if len(fields) >= 3 and fields[-1] in names:
                        offsets[fields[-1]] = int(fields[0], 16) - browser_image_size
            if any(name not in offsets for name in names):
                fails.append("reset fixture could not locate Browser form symbols")
            else:
                mouse = Mouse()
                dseg = bstate["document_segment"]
                lseg = u16(m.readseg(pseg, browser_image_size + offsets["br_lseg"], 2))
                cx0 = u16(m.readseg(pseg, browser_image_size + offsets["br_cx"], 2))
                cy0 = u16(m.readseg(pseg, browser_image_size + offsets["br_cy"], 2))
                top0 = u16(m.readseg(pseg, browser_image_size + offsets["br_top"], 2))
                def click_doc(docpos):
                    for row0 in range(nlines):
                        rec0 = m.readseg(lseg, row0 * 10, 10)
                        lo0 = u16(rec0, 0) | (u16(rec0, 2) << 16)
                        hi0 = u16(rec0, 4) | (u16(rec0, 6) << 16)
                        if lo0 <= docpos < hi0:
                            col0 = 0
                            for pos0 in range(lo0, docpos):
                                ch0 = m.readseg(dseg + (pos0 >> 4), pos0 & 15, 1)[0]
                                if 0x20 <= ch0 <= 0x7E:
                                    col0 += 1
                            mouse.click(cx0 + (rec0[8] + col0) * 8 + 3,
                                        cy0 + (row0 - top0) * 8 + 3)
                            return True
                    return False

                field_base = browser_image_size + offsets["br_flds"]
                if m.readseg(pseg, browser_image_size + offsets["br_fn"], 1)[0] != 5:
                    fails.append("reset fixture did not create five fields")
                else:
                    field0 = m.readseg(pseg, field_base, 40)
                    textpos = u16(field0, 0) | (u16(field0, 2) << 16)
                    if not click_doc(textpos):
                        fails.append("reset fixture text field is outside the visible lines")
                    else:
                        m.hmp("sendkey z")
                        gsleep(0.2)
                        if m.readseg(pseg, field_base + 5, 1)[0] <= len(b"original"):
                            fails.append("could not alter the text field before reset")
                        sub_base = browser_image_size + offsets["br_subs"]
                        sub_count = m.readseg(pseg, browser_image_size + offsets["br_subn"], 1)[0]
                        reset_offsets = []
                        label_offsets = []
                        for idx0 in range(sub_count):
                            sub0 = m.readseg(pseg, sub_base + idx0 * 18, 18)
                            if sub0[9] == 1:
                                reset_offsets.append(u16(sub0, 0) | (u16(sub0, 2) << 16))
                            elif sub0[9] == 2:
                                label_offsets.append(u16(sub0, 0) | (u16(sub0, 2) << 16))
                        if len(reset_offsets) != 2:
                            fails.append("expected INPUT and BUTTON reset spans, found %d"
                                         % len(reset_offsets))
                        else:
                            if len(label_offsets) != 1 or not click_doc(label_offsets[0] + 1):
                                fails.append("LABEL for=id span could not be clicked")
                            elif m.readseg(pseg, browser_image_size
                                           + offsets["br_fcur"], 1)[0] != 0:
                                fails.append("LABEL for=id did not focus its text control")
                            def mutate_controls():
                                for fi in (1, 3, 4):
                                    fld = m.readseg(pseg, field_base + fi * 40, 40)
                                    fpos = u16(fld, 0) | (u16(fld, 2) << 16)
                                    if not click_doc(fpos):
                                        fails.append("form control is outside the visible lines")
                                        return False
                                return True

                            def defaults_restored():
                                field0 = m.readseg(pseg, field_base, 40)
                                textpos = u16(field0, 0) | (u16(field0, 2) << 16)
                                textlen = field0[5]
                                shown = bytes(m.readseg(dseg + (textpos >> 4),
                                                        textpos & 15, 12))
                                if textlen != 8 or shown[:8] != b"original":
                                    return False
                                f1 = m.readseg(pseg, field_base + 40, 40)
                                f2 = m.readseg(pseg, field_base + 80, 40)
                                f3 = m.readseg(pseg, field_base + 120, 40)
                                f4 = m.readseg(pseg, field_base + 160, 40)
                                selected = bytes(f4[20:32]).split(b"\0", 1)[0]
                                return (f1[5] == 1 and f2[5] == 1 and
                                        f3[5] == 0 and f4[5] == 4 and
                                        selected == b"b")

                            if mutate_controls():
                                gsleep(0.2)
                                if m.readseg(pseg, field_base + 40 + 5, 1)[0] != 0:
                                    fails.append("checkbox did not toggle before reset")
                                if m.readseg(pseg, field_base + 120 + 5, 1)[0] != 1:
                                    fails.append("radio choice did not change before reset")
                                if m.readseg(pseg, field_base + 160 + 5, 1)[0] != 5:
                                    fails.append("select did not cycle before reset")
                                for resetpos in reset_offsets:
                                    if not click_doc(resetpos + 1):
                                        fails.append("reset control is outside the visible lines")
                                        break
                                    gsleep(0.2)
                                    if not defaults_restored():
                                        fails.append("reset control did not restore text, choice and select defaults")
                                        break
                                    if resetpos == reset_offsets[0] and mutate_controls():
                                        gsleep(0.2)
                            if len(srv.requests) != 1:
                                fails.append("reset submitted the form over HTTP")
        if a.password_fixture:
            names = ("br_flds", "br_fn", "br_subs", "br_subn", "br_lseg",
                     "br_lbuf", "br_cx", "br_cy", "br_top")
            offsets = {}
            with open("build/browser.map", encoding="ascii") as f:
                for line in f:
                    fields = line.split()
                    if len(fields) >= 3 and fields[-1] in names:
                        offsets[fields[-1]] = int(fields[0], 16) - browser_image_size
            if any(name not in offsets for name in names):
                fails.append("password fixture could not locate Browser field symbols")
            else:
                field_base = browser_image_size + offsets["br_flds"]
                if m.readseg(pseg, browser_image_size + offsets["br_fn"], 1)[0] != 1:
                    fails.append("POST password control was not retained")
                else:
                    fld = m.readseg(pseg, field_base, 40)
                    fieldpos = u16(fld, 0) | (u16(fld, 2) << 16)
                    if fld[20] != 1 or fld[5] != 4:
                        fails.append("password flag or initial value length is wrong")
                    dseg = bstate["document_segment"]
                    if bytes(m.readseg(dseg + (fieldpos >> 4), fieldpos & 15, 4)) != b"seed":
                        fails.append("password value was not retained for form submission")
                    lseg = u16(m.readseg(pseg, browser_image_size
                                         + offsets["br_lseg"], 2))
                    cx0 = u16(m.readseg(pseg, browser_image_size + offsets["br_cx"], 2))
                    cy0 = u16(m.readseg(pseg, browser_image_size + offsets["br_cy"], 2))
                    top0 = u16(m.readseg(pseg, browser_image_size + offsets["br_top"], 2))

                    def click_doc(docpos):
                        for row0 in range(nlines):
                            rec0 = m.readseg(lseg, row0 * 10, 10)
                            lo0 = u16(rec0, 0) | (u16(rec0, 2) << 16)
                            hi0 = u16(rec0, 4) | (u16(rec0, 6) << 16)
                            if lo0 <= docpos < hi0:
                                col0 = 0
                                for pos0 in range(lo0, docpos):
                                    ch0 = m.readseg(dseg + (pos0 >> 4), pos0 & 15, 1)[0]
                                    if 0x20 <= ch0 <= 0x7E or ch0 == 0x7F:
                                        col0 += 1
                                mouse = Mouse()
                                mouse.click(cx0 + (rec0[8] + col0) * 8 + 3,
                                            cy0 + (row0 - top0) * 8 + 3)
                                return True
                        return False

                    if click_doc(fieldpos):
                        m.hmp("sendkey x")
                        gsleep(0.2)
                        lbuf = bytes(m.readseg(pseg, browser_image_size
                                               + offsets["br_lbuf"], 98))
                        if b"*****" not in lbuf or b"seed" in lbuf:
                            fails.append("password line was not masked in the paint buffer")
                        sub_base = browser_image_size + offsets["br_subs"]
                        sub_count = m.readseg(pseg, browser_image_size
                                              + offsets["br_subn"], 1)[0]
                        submitpos = None
                        for idx0 in range(sub_count):
                            sub0 = m.readseg(pseg, sub_base + idx0 * 18, 18)
                            if sub0[9] == 0:
                                submitpos = u16(sub0, 0) | (u16(sub0, 2) << 16)
                                break
                        if submitpos is None or not click_doc(submitpos + 1):
                            fails.append("password form submit button was not clickable")
                        else:
                            deadline = time.time() + 15
                            while len(srv.requests) < 2 and time.time() < deadline:
                                time.sleep(0.1)
                            if len(srv.requests) < 2:
                                fails.append("password form did not issue its POST")
                            else:
                                request = srv.requests[-1]
                                head, body = request.split(b"\r\n\r\n", 1)
                                if not head.startswith(b"POST /capture HTTP/1.0"):
                                    fails.append("password was not submitted by POST")
                                if body != b"secret=seedx&go=Send":
                                    fails.append("password POST body did not match the submitted controls")
                                if b"seedx" in head or b"seedx" in request.split(b"\r\n", 1)[0]:
                                    fails.append("password appeared in the request URL or headers")
                    else:
                        fails.append("password field is outside the visible line records")
        if a.password_get_fixture:
            with open("build/browser.map", encoding="ascii") as f:
                fn_offset = next((int(line.split()[0], 16) - browser_image_size
                                  for line in f if line.split()[-1:] == ["br_fn"]), None)
            if fn_offset is None:
                fails.append("GET password fixture could not locate Browser field count")
            elif m.readseg(pseg, browser_image_size + fn_offset, 1)[0] != 0:
                fails.append("GET password control was retained instead of refused")
            if len(srv.requests) != 1:
                fails.append("GET password fixture unexpectedly issued another request")
        if (a.page and a.expect_source_bytes is None and not a.allow_source_cutoff
                and source_len != len(page)):
            fails.append("the guest retained %d/%d source bytes"
                         % (source_len, len(page)))
        if a.allow_source_cutoff:
            if not 0 < source_len < len(page):
                fails.append("expected a non-empty source cutoff below %d bytes, got %d"
                             % (len(page), source_len))
            if bstate["source_capacity_kb"] * 1024 < source_len:
                fails.append("the source claim is smaller than its retained byte count")
            if bstate["parser_source_position"] != source_len:
                fails.append("the parser stopped at %d after retaining %d bytes"
                             % (bstate["parser_source_position"], source_len))
            if not bstate["truncated"]:
                fails.append("source growth was cut at %d bytes but the Browser "
                             "did not mark the page truncated" % source_len)
            msgptr = u16(m.readseg(pseg, browser_image_size + BR_NMSG, 2))
            message = m.readseg(pseg, msgptr, 32).split(b"\0", 1)[0]
            if message != b"Page truncated":
                fails.append("source cutoff status is %r, expected 'Page truncated'"
                             % message.decode("ascii", "replace"))
        if a.page and os.path.basename(a.page) == "boundaryui100k.htm":
            expected_links = ["/target/16k", "/target/72k"]
            if bstate["link_hrefs"] != expected_links:
                fails.append("the parser retained boundary hrefs %r, expected %r"
                             % (bstate["link_hrefs"], expected_links))
        if source_len > 65536 and source_len <= len(page):
            # Every 64 KiB alias must contain its exact contiguous source
            # window, not merely advance the logical receive counter. Verify
            # both sides of each seam and the final bytes, including bank 2.
            srcseg = bstate["source_segment"]
            for boundary in range(65536, source_len, 65536):
                before_seg = (srcseg + ((boundary - 65536) // 16)) & 0xFFFF
                after_seg = (srcseg + (boundary // 16)) & 0xFFFF
                before = m.readseg(before_seg, 0xFFF0, 16)
                after = m.readseg(after_seg, 0, 16)
                if before != page[boundary - 16:boundary]:
                    fails.append("source bytes before the %d KiB alias boundary differ"
                                 % (boundary // 1024))
                if after != page[boundary:boundary + 16]:
                    fails.append("source bytes after the %d KiB alias boundary differ"
                                 % (boundary // 1024))
            tail_bank, tailoff = divmod(source_len, 65536)
            if tailoff >= 16:
                tail_seg = (srcseg + (tail_bank * 0x1000)) & 0xFFFF
                tail = m.readseg(tail_seg, tailoff - 16, 16)
                if tail != page[source_len - 16:source_len]:
                    fails.append("source bytes at the end of the final alias differ")
            if bstate["parser_source_position"] != source_len:
                fails.append("the parser stopped at logical source byte %d/%d"
                             % (bstate["parser_source_position"], source_len))
        say("frames rx %d tx %d, segments %d, dropped %d, overruns %d"
            % (dw("eth_nrx"), dw("eth_ntx"), dw("eth_nseg"),
               dw("eth_ndrop"), dw("eth_novw")))
        if nstate == BN_ERR and not no_page_error:
            fails.append("the browser's own state is BN_ERR: the fetch failed "
                         "and it knows")
        elif nstate != BN_DONE and not no_page_error and a.http_status_fixture != 404:
            fails.append("the browser's own state is %d, not BN_DONE - the "
                         "fetch never completed" % nstate)
        if not no_page_error and nlines < (1 if (a.cookie_fixture
                           or a.cookie_path_fixture or a.form_base_fixture
                           or a.frame_form_base_fixture
                           or a.password_fixture or a.password_get_fixture
                           or a.http_status_fixture == 404
                           or a.target_fixture
                           or a.multi_target_fixture) else 5):
            fails.append("br_nlines is %d: the page never reached the layout"
                         % nlines)
        if not a.cookie_fixture and dw("eth_novw"):
            fails.append("%d ring overrun(s): the pump is not keeping up"
                         % dw("eth_novw"))

        if a.shot:
            settle(m)
            subprocess.run(["python3", "tools/shot.py", SOCK, a.shot],
                           check=True)
            say("wrote %s" % a.shot)
    finally:
        if a.snapshot:
            os.makedirs(os.path.dirname(a.snapshot) or ".", exist_ok=True)
            with open(a.snapshot, "w") as f:
                json.dump(snapshots, f, indent=2)
                f.write("\n")
            say("wrote heap snapshot %s" % a.snapshot)
        if not a.keep:
            m.quit()

    # --- 3: the request the server actually saw ---------------------------
    req = srv.request
    say("server saw: %r" % (req.split(b"\r\n")[0] if req else None))
    if a.cookie_fixture:
        say("cookie fixture requests: %r"
            % [r.split(b"\r\n\r\n", 1)[0] for r in srv.requests])
    if a.form_base_fixture or a.frame_form_base_fixture:
        say("form BASE HREF requests: %r"
            % [r.split(b"\r\n", 1)[0] for r in srv.requests])
    if req is None:
        fails.append("the server was never connected to: nothing reached the "
                     "host through the card")
    else:
        if not req.startswith(b"GET %s HTTP/1.0\r\n" % PATH.encode()):
            fails.append("the request line is %r" % req.split(b"\r\n")[0])
        if b"Host: 10.0.2.2" not in req:
            fails.append("no Host: header")

    for f in fails:
        say("FAIL: " + f)
    say("ethernet: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

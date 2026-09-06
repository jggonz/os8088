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
   a URL. That is stage E's whole claim (docs/NET-STACK-PLAN.md).

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

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import dispcp                                          # noqa: E402
import os88sym                                         # noqa: E402
import os88qemu                                              # noqa: E402

S = os88sym.linear
SOCK = "build/qmp.sock"
PORT = 8090
PATH = "/eth.htm"
URL = "http://10.0.2.2:%d%s" % (PORT, PATH)

SK_RXMAX, SK_TXMAX = 8192, 1024         # tcp.inc's top rung (SPEC.md 72.13)
ETH_ROW, DRVR_SZ, DRVR_SEG = 2, 16, 2   # drv_tab row 2 since SPEC.md 31.1's
                                        # reorder (sound, hdd, ETHER, ram, net)
DH_BOUND = 3
BN_DONE, BN_ERR = 6, 7
BR_NLINES, BR_NSTATE = 38, 946

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
        if open(out, "rb").read() != open("build/ether.bin", "rb").read():
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
        time.sleep(0.4)

    def dblclick(self, x, y):
        # TWO `click`s ARE NOT A DOUBLE-CLICK (CLAUDE.md): the detectors
        # compare birth ticks in a 9-tick window and two processes are far too
        # slow. Position, then both presses down one QMP connection.
        self.run("to", str(x), str(y))
        subprocess.run(["python3", "tools/qmp.py", SOCK,
                        "mouse_button 1", "sleep 0.08", "mouse_button 0",
                        "sleep 0.12",
                        "mouse_button 1", "sleep 0.08", "mouse_button 0"],
                       check=True, capture_output=True)
        time.sleep(0.4)


def settle(m, card=None):
    time.sleep(2.0)


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
        cmds += ["sleep 0.06"]
    subprocess.run(["python3", "tools/qmp.py", SOCK] + cmds,
                   check=True, capture_output=True)


class Server(threading.Thread):
    """One page, and it REMEMBERS THE REQUEST - which is assertion 3."""

    def __init__(self):
        threading.Thread.__init__(self, daemon=True)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("0.0.0.0", PORT))
        self.s.listen(4)
        self.request = None

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
                if self.request is None:
                    self.request = req
                c.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                          b"Content-Length: %d\r\n\r\n" % len(PAGE) + PAGE)
            except OSError:
                pass
            finally:
                c.close()


def stale(img, src):
    return os.path.getmtime(img) < os.path.getmtime(src)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=None)
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
    if os.path.exists("build/ether360.img"):
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

    srv = Server()
    srv.start()
    say("http server on %s, %d byte page" % (URL, len(PAGE)))

    # A SURVIVOR KEEPS THE SOCKET, so the new machine cannot bind and every
    # read below would come from the OLD one - which reads as a change that
    # did nothing (CLAUDE.md). Kill it by PID out of the pidfile and never
    # with `pkill -f qemu`, whose pattern matches the calling shell.
    if os.path.exists("build/qemu.pid"):
        try:
            os.kill(int(open("build/qemu.pid").read().strip()), 15)
            time.sleep(1.0)
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
    try:
        # --- 1: a card, and an address ------------------------------------
        seg = 0
        for _ in range(150):
            time.sleep(0.4)
            row = m.read(S("drv_tab") + ETH_ROW * DRVR_SZ + DRVR_SEG, 2)
            seg = u16(row)
            if seg:
                break
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
        base = 0
        for _ in range(60):
            base = dw("eth_base")
            if base:
                break
            time.sleep(0.4)
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

        for _ in range(80):                     # DHCP_WAIT is 110 ticks
            if db("dhcp_st") == DH_BOUND:
                break
            time.sleep(0.3)
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
        time.sleep(1.0)
        type_url(URL[len("http://"):])
        time.sleep(1.0)
        subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey ret"],
                       check=True, capture_output=True)

        img = os.path.getsize("build/browser.bin")
        nstate = nlines = 0
        for _ in range(60):
            time.sleep(0.5)
            nstate = m.readseg(pseg, img + BR_NSTATE, 1)[0]
            nlines = u16(m.readseg(pseg, img + BR_NLINES, 2))
            if nstate in (BN_DONE, BN_ERR):
                break
        say("br_nstate = %d, br_nlines = %d" % (nstate, nlines))
        say("frames rx %d tx %d, segments %d, dropped %d, overruns %d"
            % (dw("eth_nrx"), dw("eth_ntx"), dw("eth_nseg"),
               dw("eth_ndrop"), dw("eth_novw")))
        if nstate == BN_ERR:
            fails.append("the browser's own state is BN_ERR: the fetch failed "
                         "and it knows")
        elif nstate != BN_DONE:
            fails.append("the browser's own state is %d, not BN_DONE - the "
                         "fetch never completed" % nstate)
        if nlines < 5:
            fails.append("br_nlines is %d: the page never reached the layout"
                         % nlines)
        if dw("eth_novw"):
            fails.append("%d ring overrun(s): the pump is not keeping up"
                         % dw("eth_novw"))

        if a.shot:
            subprocess.run(["python3", "tools/shot.py", SOCK, a.shot],
                           check=True)
            say("wrote %s" % a.shot)
    finally:
        if not a.keep:
            m.quit()

    # --- 3: the request the server actually saw ---------------------------
    req = srv.request
    say("server saw: %r" % (req.split(b"\r\n")[0] if req else None))
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

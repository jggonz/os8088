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

    def __init__(self, page=PAGE, chunk_size=0, chunk_delay=0.0):
        threading.Thread.__init__(self, daemon=True)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("0.0.0.0", PORT))
        self.s.listen(4)
        self.request = None
        self.page = page
        self.chunk_size = chunk_size
        self.chunk_delay = chunk_delay

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
                response = (b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                            b"Content-Length: %d\r\n\r\n" % len(self.page)
                            + self.page)
                if self.chunk_size > 0 and self.chunk_delay > 0:
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
                    "br_lnkoff", "br_linecap", "br_doclen_hi"):
                offsets[fields[-1]] = int(fields[0], 16) - img
    if any(name not in offsets or offsets[name] < 0 for name in
           ("br_srclen_hi", "br_srcpos", "br_lnkn", "br_lnkseg",
            "br_lnkoff", "br_linecap", "br_doclen_hi")):
        raise RuntimeError("Browser logical source symbols are missing from build/browser.map")
    source_length = word(BR_SRCLEN) | (word(offsets["br_srclen_hi"]) << 16)
    parser_position = (word(offsets["br_srcpos"])
                       | (word(offsets["br_srcpos"] + 2) << 16))
    link_count = word(offsets["br_lnkn"])
    link_segment = word(offsets["br_lnkseg"])
    link_hrefs = []
    if link_segment:
        for i in range(link_count):
            link_offset = word(offsets["br_lnkoff"] + i * 2)
            raw = m.readseg(link_segment, link_offset, 64)
            link_hrefs.append(raw.split(b"\0", 1)[0].decode(
                "ascii", errors="replace"))
    return {
        "source_segment": word(BR_SRCSEG),
        "source_capacity_kb": word(BR_SRCKB),
        "source_length": source_length,
        "parser_source_position": parser_position,
        "link_count": link_count,
        "link_hrefs": link_hrefs,
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
    ap.add_argument("--response-chunk-size", type=int, default=0,
                    help="send a large test response in chunks of this many bytes")
    ap.add_argument("--response-delay-ms", type=float, default=0,
                    help="pause this many milliseconds between response chunks")
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

    page = open(a.page, "rb").read() if a.page else PAGE
    if a.response_chunk_size < 0 or a.response_delay_ms < 0:
        sys.exit("ethernet: response chunk size and delay must be non-negative")
    srv = Server(page, a.response_chunk_size, a.response_delay_ms / 1000.0)
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

        wait_secs = 90 if len(page) > 32 * 1024 else 30
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
        say("br_nstate = %d, br_nlines = %d, br_srclen = %d"
            % (nstate, nlines, source_len))
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
            if needle not in b"".join(chunks):
                fails.append("formatted document is %d bytes but does not contain %r"
                             % (dlen, needle))
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

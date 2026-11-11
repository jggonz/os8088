#!/usr/bin/env python3
"""B4: a driver that declares OS88_DRV_WAKES stays loaded across a hibernate
and is WOKEN, not read off the disk again (SPEC.md 51.2.5, 72.5.3, 87.6).

    python3 tests/b4wake.py ether          # b4ether: kept, woken, lease kept
    python3 tests/b4wake.py swap           # b4ethswap: another card at resume
    python3 tests/b4wake.py lease          # b4lease: ACK, NAK and silence
    python3 tests/b4wake.py ether --red    # the failing case (below)

**QEMU, BY THE CLOSED LIST** (CLAUDE.md, docs/TESTING.md entry 5): the
subject is ETHER.DRV and MartyPC has no network card of any kind. The machine
is tests/p2hiber.py's - the shipped system floppy with a SYSTEM.CFG wanting
SOUND, HDD.DRV and the os88net cable (hmadrv.WANT) plus ETHER.DRV (bit 4), a
FAT16 C: on IDE the image is written to, QEMU's PS/2 mouse the pointer - with
QEMU's `ne2k_isa` at 300h on a `-netdev socket` whose other end is THIS FILE.

**NOT SLIRP, AND THAT IS THE POINT OF THE ROW.** Slirp's DHCP server never
says NAK and never stays silent, and it cannot be asked what it was sent. The
network here is a few dozen lines of Python behind QEMU's stream socket
(4-byte big-endian length, then the frame): it answers ARP for its own
address, runs DHCP (10.7.0.1 serving 10.7.0.15/24) and RECORDS every DHCP
message the guest sends, with the phase it arrived in - so "no DISCOVER after
the wake" and "the REQUEST carried no option 54" are assertions about bytes
on the wire, not about a state byte that a reload would also have set.

The cycle, every leg: boot, ETHER.DRV attached and BOUND to 10.7.0.15 (the
row 2 segment, the station address and the lease read out of the driver);
Hibernate... from the System menu, Enter, the image reaches C: on the host,
QEMU killed (the power-off), a fresh QEMU - on the SAME station address, or
another one for `swap` - resumes by itself ([hb_resumes]).

`ether` (b4ether) then asserts:
  1. [hb_drvmask] has NO bit for row 2: the sweep put ETHER.DRV to SLEEP and
     kept it rather than unloading it (SOUND's bit, row 0, is set - the
     sweep ran and the declared driver was the one it did not take);
  2. the row is loaded, its station address the one it hibernated with;
  3. since the wake the wire carried NO DHCPDISCOVER - a reloaded driver's
     READY starts with one - and the first DHCPREQUEST is RFC 2131's
     INIT-REBOOT: option 50 the address held, NO option 54, ciaddr 0;
  4. eth_ip is still 10.7.0.15 with nothing pumped yet: the lease came home
     in the picture and nothing waited for a server.

`swap` (b4ethswap): the second QEMU's card has another MAC, so WAKE must
refuse (CF = 1), the row is unloaded and READ AGAIN (SPEC.md 51.2.5's
fallback): [hb_drvmask] still has no bit 2 (it WAS kept at the sweep), the
row is loaded with the NEW station address, the wire carried a DISCOVER after
the wake (the fresh READY) and no INIT-REBOOT REQUEST (the woken driver's,
which a skipped compare would have sent from the wrong card).

`lease` (b4lease): three cycles in one boot's session, the network answering
the wake's REQUEST three ways (SPEC.md 72.5.3). Nothing pumps a driver no
package is using (72.2.1), so after each resume BROWSER.O88 off
network360.img fetches http://10.7.0.1/ - ARP is answered and the SYN is
dropped, so the browser keeps asking NETV_* (every verb pumps) while the
answer is read:
  ack     -> DH_BOUND, eth_ip 10.7.0.15, no DISCOVER;
  nak     -> the address cleared and a DISCOVER: the server then offers
             10.7.0.16, so DH_BOUND on .16;
  silent  -> the INIT-REBOOT REQUEST retried and never answered, and the
             driver ends DH_BOUND on the address it held - where an ordinary
             REQUEST's silence is DH_FAIL (4).

THE FAILING CASE (docs/WRITING-TESTS.md 1). `--red` clears OS88_DRV_WAKES in
the RUNNING ETHER.DRV's header (+11 bit 7, poked through QEMU's GDB stub
before the hibernate): the sweep then unloads it as before B4 and `ether`
goes red on 1 (bit 2 set) and 3 (a DISCOVER, from READY's blocking DHCP) -
the whole feature, off one bit. Reasoned rather than poked: `swap` is red if
eth_wake's MAC compare is skipped (no DISCOVER; an INIT-REBOOT REQUEST from
the new card), and `lease`'s silent cycle is red if dhcp_timer loses its
`dhcp_wk` arm (dhcp_st 4, DH_FAIL).

Needs QEMU 9 (`qemu9`), as p2hiber: QEMU 8.2 aborts taking an interrupt on
the resume stub's stack in the text framebuffer.
"""
import atexit
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88qemu    # noqa: E402
import os88sym     # noqa: E402
import hmacold     # noqa: E402
import hmadrv      # noqa: E402
import hmahiber    # noqa: E402
import xmcheck     # noqa: E402
import ethernet    # noqa: E402  (ether_syms: the driver's own labels)

S = os88sym.linear
B = os.path.join(ROOT, "build")

ETH_ROW, DRVR_SZ, DRVR_SEG = 2, 16, 2   # drv_tab row 2: sound, hdd, ETHER
SND_ROW = 0
ETH_CFGBIT = 4                          # drv_cfgbit's, not the row (Makefile)
DH_BOUND, DH_FAIL = 3, 4
DRV_H_WAKES = 11                        # kernel/driver.inc: +10's high byte

MAC1 = "52:54:00:b4:00:01"
MAC2 = "52:54:00:b4:00:02"              # swap's "another card"
SRV_MAC = bytes.fromhex("525400b400fe")
SRV_IP = bytes([10, 7, 0, 1])
LEASE1 = bytes([10, 7, 0, 15])
LEASE2 = bytes([10, 7, 0, 16])          # what a NAK'd machine is offered next
MASK = bytes([255, 255, 255, 0])

DHCPDISCOVER, DHCPOFFER, DHCPREQUEST, DHCPACK, DHCPNAK = 1, 2, 3, 5, 6


def dotted(b):
    return ".".join(str(x) for x in b)


def csum(data):
    if len(data) & 1:
        data += b"\0"
    s = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


# -----------------------------------------------------------------------------
# The network: the other end of QEMU's `-netdev socket`, one guest at a time
# -----------------------------------------------------------------------------
class Net(threading.Thread):
    """ARP for SRV_IP, DHCP, and a log of every DHCP message the guest sent.

    `phase` tags what is logged ("boot" or "wake"); `mode` is how a REQUEST
    in the wake phase is answered: "ack", "nak" (and the NEXT exchange is
    offered LEASE2) or "silent" (INIT-REBOOT requests are ignored)."""

    def __init__(self):
        super().__init__(daemon=True)
        self.ls = socket.socket()
        self.ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.ls.bind(("127.0.0.1", 0))
        self.ls.listen(2)
        self.port = self.ls.getsockname()[1]
        self.phase, self.mode = "boot", "ack"
        self.lease = LEASE1
        self.log = []                   # (phase, type, has54, opt50, ciaddr, chaddr)
        self.lock = threading.Lock()
        self.conn = None

    def run(self):
        while True:
            try:
                c, _ = self.ls.accept()
            except OSError:
                return
            self.conn = c
            try:
                self.serve(c)
            except OSError:
                pass
            self.conn = None

    def serve(self, c):
        buf = b""
        while True:
            d = c.recv(65536)
            if not d:
                return
            buf += d
            while len(buf) >= 4:
                n = struct.unpack("!I", buf[:4])[0]
                if len(buf) < 4 + n:
                    break
                self.frame(buf[4:4 + n])
                buf = buf[4 + n:]

    def send(self, frame):
        c = self.conn
        if c is not None:
            if len(frame) < 60:
                frame += bytes(60 - len(frame))
            c.sendall(struct.pack("!I", len(frame)) + frame)

    def frame(self, f):
        if len(f) < 14:
            return
        dst, src, typ = f[:6], f[6:12], struct.unpack("!H", f[12:14])[0]
        if typ == 0x0806 and len(f) >= 42:          # ARP
            op = struct.unpack("!H", f[20:22])[0]
            if op == 1 and f[38:42] == SRV_IP:
                rep = (src + SRV_MAC + b"\x08\x06"
                       + struct.pack("!HHBBH", 1, 0x0800, 6, 4, 2)
                       + SRV_MAC + SRV_IP + f[22:28] + f[28:32])
                self.send(rep)
            return
        if typ != 0x0800 or len(f) < 34:
            return
        ihl = (f[14] & 15) * 4
        if f[23] != 17:                             # UDP only; a SYN is dropped
            return
        u = 14 + ihl
        dport = struct.unpack("!H", f[u + 2:u + 4])[0]
        if dport != 67:
            return
        self.dhcp(src, f[u + 8:])

    @staticmethod
    def opts(bp):
        out, i = {}, 240
        while i < len(bp):
            o = bp[i]
            if o == 255:
                break
            if o == 0:
                i += 1
                continue
            if i + 1 >= len(bp):
                break
            ln = bp[i + 1]
            out[o] = bp[i + 2:i + 2 + ln]
            i += 2 + ln
        return out

    def dhcp(self, chmac, bp):
        if len(bp) < 240 or bp[0] != 1 or bp[236:240] != b"\x63\x82\x53\x63":
            return
        o = self.opts(bp)
        if 53 not in o:
            return
        t = o[53][0]
        with self.lock:
            self.log.append((self.phase, t, 54 in o, o.get(50), bp[12:16],
                             bp[28:34]))
        xid = bp[4:8]
        if t == DHCPDISCOVER:
            self.reply(xid, bp[28:34], DHCPOFFER, self.lease)
        elif t == DHCPREQUEST:
            reboot = 54 not in o
            if self.phase == "wake" and reboot:
                if self.mode == "silent":
                    return
                if self.mode == "nak":
                    self.lease = LEASE2
                    self.reply(xid, bp[28:34], DHCPNAK, bytes(4))
                    return
            self.reply(xid, bp[28:34], DHCPACK, o.get(50, self.lease))

    def reply(self, xid, chaddr, typ, yi):
        bp = bytearray(240)
        bp[0], bp[1], bp[2] = 2, 1, 6
        bp[4:8] = xid
        bp[10] = 0x80                               # BROADCAST
        bp[16:20] = yi
        bp[20:24] = SRV_IP
        bp[28:34] = chaddr
        bp[236:240] = b"\x63\x82\x53\x63"
        bp += bytes([53, 1, typ, 54, 4]) + SRV_IP
        if typ != DHCPNAK:
            bp += (bytes([51, 4]) + struct.pack("!I", 86400)
                   + bytes([1, 4]) + MASK + bytes([3, 4]) + SRV_IP
                   + bytes([6, 4]) + SRV_IP)
        bp += b"\xff"
        udp = struct.pack("!HHHH", 67, 68, 8 + len(bp), 0) + bytes(bp)
        ip = bytearray(struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(udp),
                                   0, 0, 64, 17, 0, SRV_IP, b"\xff" * 4))
        ip[10:12] = struct.pack("!H", csum(bytes(ip)))
        self.send(b"\xff" * 6 + SRV_MAC + b"\x08\x00" + bytes(ip) + udp)

    def since_wake(self):
        with self.lock:
            return [e for e in self.log if e[0] == "wake"]


# -----------------------------------------------------------------------------
# The machine: p2hiber's, with the card on the network above
# -----------------------------------------------------------------------------
class Q(object):
    def __init__(self, tmp, sysimg, apps, hd, net, mac):
        self.sock = os.path.join(tmp, "qmp.sock")
        if os.path.exists(self.sock):
            os.remove(self.sock)
        pid = os.path.join(tmp, "qemu.pid")
        os88qemu.start(
            ["qemu-system-i386", "-machine", "pc,vmport=off", "-m", "128",
             "-audiodev", "none,id=snd", "-device", "sb16,audiodev=snd",
             "-netdev", "socket,id=n0,connect=127.0.0.1:%d" % net.port,
             "-device", "ne2k_isa,netdev=n0,iobase=0x300,irq=3,mac=%s" % mac,
             "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % hd,
             "-drive", "file=%s,format=raw,if=floppy" % sysimg, "-boot", "a",
             "-drive", "file=%s,format=raw,if=floppy,index=1" % apps,
             "-display", "none", "-qmp", "unix:%s,server,nowait" % self.sock,
             "-daemonize", "-pidfile", pid], check=True)
        os88qemu.own(pid, self.sock)
        self.pidn = int(open(pid).read().strip())
        atexit.register(self.close)
        for _ in range(150):
            if os.path.exists(self.sock):
                break
            time.sleep(0.2)             # the socket appearing is a HOST event

    def close(self):
        if self.pidn:
            try:
                os.kill(self.pidn, signal.SIGTERM)
                os88qemu.gone(self.pidn)
            except OSError:
                pass
            self.pidn = None

    def read(self, linear, n):
        return bytes(xmcheck.read_bytes(self.sock, linear, n))

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)

    def hmp(self, cmd):
        return xmcheck.qmp(self.sock, cmd)


def word(q, name):
    b = q.read(S(name), 2)
    return b[0] | b[1] << 8


def ethseg(q):
    b = q.read(S("drv_tab") + ETH_ROW * DRVR_SZ + DRVR_SEG, 2)
    return b[0] | b[1] << 8


def disks(tmp):
    sysimg = os.path.join(tmp, "sys.img")
    shutil.copy(os.path.join(B, "os8088.img"), sysimg)
    cfg = os.path.join(tmp, "SYSTEM.CFG")
    with open(cfg, "wb") as f:
        f.write(b"O88CFG\0\0" + struct.pack("<H", 3) + b"DW" + bytes([1, 2])
                + struct.pack("<H", hmadrv.WANT | (1 << ETH_CFGBIT))
                + b"\0\0")
    subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg, cfg],
                   check=True, capture_output=True)
    apps = os.path.join(tmp, "net.img")
    shutil.copy(os.path.join(B, "network360.img"), apps)
    hd = os.path.join(tmp, "hd.img")
    note = os.path.join(tmp, "HELLO.TXT")
    with open(note, "w") as f:
        f.write("hello from C:\r\n")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", hd, "--hdd",
                    "--mbr", os.path.join(B, "mbr.bin"),
                    "--boot", os.path.join(B, "boothd.bin"),
                    "--kernel", os.path.join(B, "kernel.sys"), note],
                   check=True, capture_output=True)
    return sysimg, apps, hd


def main():
    os.chdir(ROOT)
    leg = sys.argv[1] if len(sys.argv) > 1 else "ether"
    red = "--red" in sys.argv
    if leg not in ("ether", "swap", "lease"):
        sys.exit("b4wake: the leg is ether, swap or lease")
    for need in ("os8088.img", "network360.img", "mbr.bin", "boothd.bin",
                 "kernel.sys", "ether.bin"):
        if not os.path.exists(os.path.join(B, need)):
            sys.exit("b4wake: no build/%s - run `make`" % need)
    syms = ethernet.ether_syms()
    fails = []

    def want(cond, what):
        print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
        if not cond:
            fails.append(what)

    net = Net()
    net.start()
    modes = ("ack", "nak", "silent") if leg == "lease" else ("ack",)
    with tempfile.TemporaryDirectory(dir=B) as tmp:
        sysimg, apps, hd = disks(tmp)
        q = Q(tmp, sysimg, apps, hd, net, MAC1)
        xmcheck.wait_desktop(q.sock, "b4wake")
        os88qemu.ui_done(q, S, cap=2.0)
        if not os88qemu.acted(q, lambda: ethseg(q) != 0, secs=60,
                              what="ETHER.DRV's drv_tab row", poll=0.4):
            sys.exit("b4wake: ETHER.DRV never attached - nothing below means "
                     "anything")
        seg = os88qemu.quiesce(q, lambda: ethseg(q), secs=0.5, stable=3,
                               what="ETHER.DRV's segment")

        def db(s, name):
            return q.readseg(s, syms[name], 1)[0]

        def dip(s, name, n=4):
            return q.readseg(s, syms[name], n)

        os88qemu.acted(q, lambda: db(seg, "dhcp_st") == DH_BOUND, secs=24,
                       what="DHCP bound", poll=0.3)
        if dip(seg, "eth_ip") != LEASE1:
            sys.exit("b4wake: the boot bound %s, not %s off this file's "
                     "server - nothing below means anything"
                     % (dotted(dip(seg, "eth_ip")), dotted(LEASE1)))
        mac0 = dip(seg, "eth_mac", 6)
        print("   ETHER.DRV at %04X, %s, %s" % (seg, mac0.hex(),
                                                 dotted(LEASE1)), flush=True)
        ui = hmacold.QUI(q, mouse=hmacold.QMouse(q), sym=S, verbose=False)
        held = LEASE1
        browser = False
        for n, mode in enumerate(modes, 1):
            if red:                     # THE FAILING CASE: the bit off, live
                lin = (seg << 4) + DRV_H_WAKES
                os88qemu.poke(q.hmp, lin, bytes([q.read(lin, 1)[0] & 0x7F]))
            net.phase, net.mode = "boot", mode
            ui.menu_pick("Apple", "Hibernate...")
            ui.wait_window("Hibernate")
            os88qemu.ui_done(q, S, cap=1.0)
            xmcheck.qmp(q.sock, "sendkey ret")
            if not os88qemu.acted(q, lambda: q.read(0x449, 1)[0] in (3, 7),
                                  secs=180, what="the hibernated sentence"):
                q.close()
                sys.exit("b4wake: the hibernate never reached text mode "
                         "(toast %r)" % (ui.toast(),))
            want(hmahiber.host_wait(lambda: {"HIBERNAT.PTR", "HIBERNAT.IMG"}
                                    <= hmahiber.on_c(hd)),
                 "%d.0 both hibernate files are on C: before the power-off" % n)
            q.close()                   # the power-off
            net.phase = "wake"
            q = Q(tmp, sysimg, apps, hd, net, MAC2 if leg == "swap" else MAC1)
            ui = hmacold.QUI(q, mouse=hmacold.QMouse(q), sym=S, verbose=False)
            if not os88qemu.acted(q, lambda: word(q, "hb_resumes") == n,
                                  secs=180, what="[hb_resumes] to reach %d" % n):
                want(False, "%d.0 the machine resumed" % n)
                break
            os88qemu.acted(q, lambda: q.read(S("sch_lock"), 1)[0] == 0,
                           secs=30, what="the wake's last step")
            mask = word(q, "hb_drvmask")
            seg = ethseg(q)
            want(not mask & (1 << ETH_ROW) and mask & (1 << SND_ROW),
                 "%d.1 the sweep KEPT ETHER.DRV: [hb_drvmask] %04X has row 0 "
                 "(SOUND, unloaded) and not row 2" % (n, mask))
            want(seg != 0, "%d.2 ETHER.DRV is loaded at %04X" % (n, seg))
            if not seg:
                break
            mac = dip(seg, "eth_mac", 6)
            if leg != "swap":           # the wake's REQUEST is a HOST event
                hmahiber.host_wait(lambda: any(e[1] == DHCPREQUEST for e in
                                               net.since_wake()), secs=5.0)
            log = net.since_wake()
            disc = [e for e in log if e[1] == DHCPDISCOVER]
            reqs = [e for e in log if e[1] == DHCPREQUEST]
            reboot = [e for e in reqs if not e[2]]
            if leg == "swap":
                want(mac == bytes.fromhex(MAC2.replace(":", "")),
                     "%d.3 the row holds the NEW card's address %s (the wake "
                     "refused the other card and a fresh driver was read)"
                     % (n, mac.hex()))
                os88qemu.acted(q, lambda: any(e[1] == DHCPDISCOVER for e in
                                              net.since_wake()), secs=20,
                               what="a DISCOVER", poll=0.3)
                log = net.since_wake()
                want(any(e[1] == DHCPDISCOVER for e in log),
                     "%d.4 a DISCOVER after the wake: the fresh driver's READY"
                     % n)
                want(not any(e[1] == DHCPREQUEST and not e[2] for e in log),
                     "%d.5 no INIT-REBOOT REQUEST: the woken driver did not "
                     "claim the lease from another card" % n)
                continue
            want(mac == mac0, "%d.3 the station address came home: %s"
                 % (n, mac.hex()))
            want(not disc, "%d.4 no DISCOVER since the wake (%d seen): "
                 "READY's blocking DHCP did not run" % (n, len(disc)))
            r0 = reboot[0] if reboot else None
            want(r0 is not None and r0[3] == held and r0[4] == bytes(4),
                 "%d.5 the wake asked ONCE for %s in the INIT-REBOOT form "
                 "(no option 54, ciaddr 0): %r" % (n, dotted(held), r0))
            want(dip(seg, "eth_ip") == held,
                 "%d.6 eth_ip is still %s before anything pumps"
                 % (n, dotted(held)))
            if leg != "lease":
                continue
            # --- the pump: a package's verbs, so the answer is read --------
            if not browser:
                ui.path("B:/BROWSER.O88")
                os88qemu.pace(q, 1.0)
                os88qemu.type_text(q.sock, "10.7.0.1/")
                browser = True
            xmcheck.qmp(q.sock, "sendkey ret")
            if mode == "ack":
                ok = lambda: (db(seg, "dhcp_st") == DH_BOUND
                              and dip(seg, "eth_ip") == held)
            elif mode == "nak":
                ok = lambda: (db(seg, "dhcp_st") == DH_BOUND
                              and dip(seg, "eth_ip") == LEASE2)
            else:
                ok = lambda: (db(seg, "dhcp_st") == DH_BOUND
                              and len([e for e in net.since_wake()
                                       if e[1] == DHCPREQUEST]) >= 2)
            c = os88qemu.Clock(q)
            while not ok() and c.secs() < 30.0 and not c.stalled():
                if c.secs() > 8.0 and int(c.secs()) % 8 == 0:
                    xmcheck.qmp(q.sock, "sendkey ret")  # keep the verbs coming
                time.sleep(0.3)
            st, ip = db(seg, "dhcp_st"), dip(seg, "eth_ip")
            log = net.since_wake()
            if mode == "ack":
                want(st == DH_BOUND and ip == held,
                     "%d.7 ACK: DH_BOUND on %s (dhcp_st %d, %s)"
                     % (n, dotted(held), st, dotted(ip)))
            elif mode == "nak":
                want(any(e[1] == DHCPDISCOVER for e in log),
                     "%d.7 NAK: the address went and a DISCOVER followed" % n)
                want(st == DH_BOUND and ip == LEASE2,
                     "%d.8 ...and the server's new offer bound: %s (dhcp_st "
                     "%d)" % (n, dotted(ip), st))
                held = LEASE2
            else:
                nreq = len([e for e in log if e[1] == DHCPREQUEST and not e[2]])
                want(nreq >= 2, "%d.7 silence: the INIT-REBOOT REQUEST was "
                     "retried (%d sent)" % (n, nreq))
                want(st == DH_BOUND and ip == held,
                     "%d.8 silence KEEPS the address: DH_BOUND on %s (dhcp_st "
                     "%d - DH_FAIL is %d)" % (n, dotted(held), st, DH_FAIL))
                want(not any(e[1] == DHCPDISCOVER for e in log),
                     "%d.9 ...and never started over" % n)
        q.close()
    if fails:
        print("b4wake %s: %d FAILED" % (leg, len(fails)))
        return 1
    print("b4wake %s: ok" % leg)
    return 0


if __name__ == "__main__":
    sys.exit(main())

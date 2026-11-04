#!/usr/bin/env python3
"""PCIETHER.DRV: an RTL8139 on kern_mega, a stack, and the browser over it
(SPEC.md 125, docs/plans/PCIETHER-PLAN.md S8).

    make mega && make pciethertest browsertest && python3 tests/pciether.py

**THIS ONE IS QEMU'S, AND THE REASONS ARE ETHERNET.PY'S PLUS ONE.** MartyPC
has no network card of any kind; Bochs has no RTL8139 (tests/brmega.py gives
kern_mega an NE2000 there, which is ETHER.DRV's card and not this one's); and
86Box's RTL8139C+ (`vm/486-pciether`) cannot be keyed or asserted against
(docs/86BOX.md), so it is where a person LOOKS. QEMU's `rtl8139` is a PCI
device behind SeaBIOS's PCI BIOS - the `int 1Ah` B1h service SPEC.md 125.3
reaches - on the same slirp network tests/ethernet.py uses: a gateway at
10.0.2.2, DHCP handing out 10.0.2.15, DNS at 10.0.2.3.

What QEMU costs is what it always costs: the machine is not an 8088 (nor a
period 386) and no timing here means anything. Every assertion is about
BEHAVIOUR; there is no number in this file for PERFORMANCE.md to want.

**WHY kern_mega UNDER QEMU AND NOT BOCHS.** The Makefile's M1 note: QEMU
executes 16-bit protected mode correctly, and it is lenient where a real 286
faults - which does not matter here, because a driver that finds a PCI card
needs a 386 (SPEC.md 125.3) and QEMU's CPU is one. What matters is that the
card exists. Under kern_mega a claim is named by an LDT SELECTOR, so a driver's
`drv_tab` segment word and the Browser's `wm_wins` segment word are selectors:
`MQemu.lin` decodes one through the GDT/LDT bases `info registers` prints and
one 8-byte descriptor read (the descriptor layout is the 286's; byte 7 is the
386's high base byte and is 0 for every selector kern_mega makes).

FIVE ASSERTIONS, climbing the stack as tests/ethernet.py does.

1. THE DRIVER ATTACHED, AND IT FOUND A CARD. Exactly one `drv_tab` row has a
   segment, and the driver's `eth_mac` is a plausible station address. QEMU
   gives an rtl8139 the same 52:54:00 prefix it gives every NIC it makes, and
   that is asserted too, because a MAC read out of the wrong registers is
   all-zero, all-ones or garbage - never QEMU's.
2. THE ADDRESS. `dhcp_st` is DH_BOUND, `eth_ip` is 10.0.2.15, the gateway
   10.0.2.2, the name server 10.0.2.3: DISCOVER, OFFER, REQUEST and ACK over
   the card, so transmit, receive, the broadcast path, UDP and the option
   walker all work before a socket exists.
3. A TCP FETCH FROM THE HOST. The Browser (the shipped BROWSER.O88, which
   knows nothing about PCI) is opened off B: and handed a URL; the request
   line arrives at a REAL host socket served by this file - ARP for the
   gateway, a handshake, a GET, a windowed drain and a close, over the
   RTL8139's one receive ring and one transmit buffer.
4. THE PAGE RENDERS. `br_nstate` is BN_DONE and `br_nlines` is non-zero.
5. THE COUNTERS AGREE. `eth_nrx` and `eth_ntx` are non-zero, and `eth_novw`
   (receive-overflow resets, SPEC.md 125.4) is 0 for a page of this size.

THE DRIVER'S SYMBOLS come from assembling drivers/pciether/pciether.asm and
REFUSING the map unless the result is byte-identical to build/pciether.bin
(tests/ethernet.py's `ether_syms`). A knob or define that the Makefile adds
to the driver's rule must be named in $PCIETHER_DEFINES (space separated, no
-D) or every lookup is refused - a map of a different build is worse than none.
$PCIETHER_ROW names the `drv_tab` row if the one-segment rule is not enough.

THE DRIVER IS ASKED FOR BY A SYSTEM.CFG that `make pciethertest` puts on the
disk, so nothing here drives the Control Panel: it is attached and DHCP has
run before the first paint.

NEVER RUN BY A WRITER WHILE A BATCH IS OPEN (CLAUDE.md, Testing): this row was
written against QEMU's behaviour as measured by one boot of plain kern_mega
with the rtl8139 attached (`info pci`: 10ec:8139, BAR0 I/O at 0xc000; the GDT
and LDT decode checked against CS=0060 = base 0x600) and has not been run.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, "tools"))
sys.path.insert(0, os.path.join(_ROOT, "tests"))
# WHICH KERNEL THE SYMBOL MAP DESCRIBES - before os88sym is first imported
# (tests/brmega.py's line). kern_mega's, not kern_big's: build/megak/
os.environ["OS88_BUILD"] = os.path.join(_ROOT, "build", "megak")

import ethernet as eth                                   # noqa: E402
import dispcp                                            # noqa: E402
import os88fixture                                       # noqa: E402
import os88qemu                                          # noqa: E402
import os88sym                                           # noqa: E402

S = os88sym.linear
say = eth.say
u16 = eth.u16
dotted = eth.dotted

PORT = eth.PORT                          # 8090: the shared lease serialises
PATH = "/pci.htm"
URL = "http://10.0.2.2:%d%s" % (PORT, PATH)
DRVR_SZ, DRVR_SEG = eth.DRVR_SZ, eth.DRVR_SEG
DRV_ROWS = 12                            # rows read when DRV_MAX is not an equate
DH_BOUND, BN_DONE, BN_ERR = eth.DH_BOUND, eth.BN_DONE, eth.BN_ERR
TI = 4                                   # a selector's table-indicator bit

PAGE = (b"<html><head><title>Over the RTL8139</title></head><body>\r\n"
        b"<h1>Fetched over a PCI card</h1>\r\n"
        b"<p>Every byte of this page crossed an emulated Realtek 8139 found\r\n"
        b"by the PCI BIOS, through ARP, a three-way handshake and a windowed\r\n"
        b"drain, into the same parser a floppy page goes through.</p>\r\n"
        b"</body></html>\r\n")


class MQemu(eth.Qemu):
    """tests/ethernet.py's Qemu, reading through kern_mega's descriptors.

    A kernel symbol is a plain linear address (kern_mega's kernel segment is
    the same 0x600 the real-mode kernel has, which the one look verified);
    a DRIVER's or a PACKAGE's segment word is an LDT selector, so `readseg`
    resolves it through `sel_base` first."""

    def __init__(self, path=None):
        eth.Qemu.__init__(self, path)
        self._ldt = self._gdt = None

    def _tables(self):
        if self._gdt is None:
            txt = self.hmp("info registers")
            g = re.search(r"^GDT=\s*([0-9a-f]+)\s+([0-9a-f]+)", txt, re.M)
            l = re.search(r"^LDT=([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)",
                          txt, re.M)
            if not g or not l:
                raise RuntimeError("no GDT/LDT in `info registers`: %r"
                                   % txt[:200])
            self._gdt = (int(g.group(1), 16), int(g.group(2), 16))
            self._ldt = (int(l.group(2), 16), int(l.group(3), 16))
        return self._gdt, self._ldt

    def sel_base(self, sel):
        """The linear base of selector `sel`, or None when the table does
        not hold it (the caller then treats the word as a paragraph)"""
        (gb, gl), (lb, ll) = self._tables()
        base, limit = (lb, ll) if sel & TI else (gb, gl)
        off = sel & 0xFFF8
        if off + 7 > limit:
            return None
        d = self.read(base + off, 8)
        return d[2] | d[3] << 8 | d[4] << 16 | d[7] << 24

    def lin(self, seg, off=0):
        b = self.sel_base(seg) if seg & TI else None
        return (b if b is not None else (seg & 0xFFFF) << 4) + off

    def readseg(self, seg, off, n):
        return self.read(self.lin(seg, off), n)


def map_syms(path):
    """label -> address from an nasm [map all] file (browser.map's shape)"""
    out = {}
    with open(path, encoding="ascii") as f:
        for line in f:
            fl = line.split()
            if len(fl) >= 3 and re.fullmatch(r"[0-9A-Fa-f]+", fl[0]):
                out[fl[-1]] = int(fl[0], 16)
    return out


def pciether_syms():
    """drivers/pciether/pciether.asm's symbols, REFUSED unless the assembly
    is byte-identical to build/pciether.bin (ether_syms' rule)"""
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "p.asm")
        mp = os.path.join(d, "p.map")
        out = os.path.join(d, "p.bin")
        shutil.copy("drivers/pciether/pciether.asm", src)
        with open(src, "a") as f:
            f.write("\n[map all %s]\n" % mp)
        defs = ["-D" + n for n in os.environ.get("PCIETHER_DEFINES", "").split()]
        subprocess.run(["nasm", "-f", "bin", "-w+error"] + defs
                       + ["-I", "drivers/pciether/", "-I", "drivers/ether/",
                          "-I", "drivers/net/", "-I", "drivers/",
                          "-I", "apps/", "-o", out, src], check=True)
        if open(out, "rb").read() != open(
                eth.os88build.at("build/pciether.bin"), "rb").read():
            sys.exit("pciether: the mapped build is not build/pciether.bin "
                     "(a define in the Makefile's rule? name it in "
                     "$PCIETHER_DEFINES) - every offset it names would be "
                     "plausible and wrong")
        return map_syms(mp)


def main():
    fails = []
    # B: IS THE BROWSER'S SCRATCH DISK (`make browsertest`), 1.44MB because
    # the system disk is. A: is `make pciethertest`'s, copied by make_test so
    # a Setup-window ETHER.CFG written by a run never reaches the built image
    os88fixture.need("build/pciethertest.img", "build/brtest.img",
                     "build/megak/kernel.map", "build/pciether.bin",
                     "build/browser.map")
    syms = pciether_syms()
    for n in ("eth_mac", "eth_ip", "eth_gw", "eth_dns", "dhcp_st",
              "eth_nrx", "eth_ntx", "eth_novw"):
        if n not in syms:
            sys.exit("pciether: %s is not in the driver's map" % n)
    bsyms = map_syms(eth.os88build.at("build/browser.map"))
    for n in ("br_nstate", "br_nlines"):
        if n not in bsyms:
            sys.exit("pciether: %s is not in build/browser.map" % n)

    srv = eth.Server(PAGE)
    srv.start()
    say("http server on %s, %d byte page" % (URL, len(PAGE)))

    os88qemu.make_test("PCIETHER=1", "TESTIMG=build/pciethertest.img",
                       "TESTAPPS=build/brtest.img")
    m = MQemu()
    try:
        # --- 1: a card, and an address ------------------------------------
        try:                    # DRV_MAX is an `equ`: the kernel's own count
            nrows = int(os88sym.equates()["DRV_MAX"])
        except Exception:       # noqa: BLE001 - not an exported equate
            nrows = DRV_ROWS

        def rows():
            raw = m.read(S("drv_tab"), nrows * DRVR_SZ)
            return [u16(raw, i * DRVR_SZ + DRVR_SEG) for i in range(nrows)]

        def pci_row():
            if os.environ.get("PCIETHER_ROW"):
                return int(os.environ["PCIETHER_ROW"])
            live = [i for i, s in enumerate(rows()) if s]
            return live[-1] if live else None

        os88qemu.kernel_up(m, S, secs=180)
        os88qemu.acted(m, lambda: pci_row() is not None, secs=90,
                       what="PCIETHER.DRV's drv_tab row", poll=0.4)
        row = pci_row()
        if row is None:
            m.quit()
            sys.exit("pciether: PCIETHER.DRV never attached - no RTL8139 was "
                     "found by the PCI BIOS, or SYSTEM.CFG's bit is not the "
                     "kernel's (make pciethertest PCIETHER_CFGBIT=<n>)")
        live = [i for i, s in enumerate(rows()) if s]
        if len(live) != 1:
            fails.append("%d drv_tab rows hold a segment (%s): the gate disk "
                         "asks for one driver" % (len(live), live))
        seg = os88qemu.quiesce(m, lambda: rows()[row], secs=0.5, stable=3,
                               what="PCIETHER.DRV's segment")
        say("PCIETHER.DRV: row %d, selector/segment %04X, base %06X"
            % (row, seg, m.lin(seg)))

        def db(n):
            return m.readseg(seg, syms[n], 1)[0]

        def dw(n):
            return u16(m.readseg(seg, syms[n], 2))

        def dip(n):
            return m.readseg(seg, syms[n], 4)

        os88qemu.acted(m, lambda: any(m.readseg(seg, syms["eth_mac"], 6)),
                       secs=24, what="the station address", poll=0.4)
        mac = m.readseg(seg, syms["eth_mac"], 6)
        say("station address %s" % mac.hex())
        if not any(mac) or all(b == 0xFF for b in mac):
            fails.append("the station address is %s, which is what a "
                         "floating bus reads as" % mac.hex())
        if mac[0] & 1:
            fails.append("the station address %s is MULTICAST" % mac.hex())
        if mac[:3] != bytes((0x52, 0x54, 0x00)):
            fails.append("the station address %s is not QEMU's 52:54:00:..., "
                         "so IDR0..5 was not what was read" % mac.hex())

        # --- 2: DHCP -------------------------------------------------------
        os88qemu.acted(m, lambda: db("dhcp_st") == DH_BOUND, secs=24,
                       what="DHCP bound", poll=0.3)
        st, ip, gw, dns = db("dhcp_st"), dip("eth_ip"), dip("eth_gw"), \
            dip("eth_dns")
        say("dhcp %d, addr %s, router %s, name %s"
            % (st, dotted(ip), dotted(gw), dotted(dns)))
        if st != DH_BOUND:
            fails.append("dhcp_st is %d and not DH_BOUND: no address was "
                         "acquired over the card" % st)
        for what, got, want in (("address", ip, "10.0.2.15"),
                                ("router", gw, "10.0.2.2"),
                                ("name server", dns, "10.0.2.3")):
            if dotted(got) != want:
                fails.append("the %s is %s and slirp's is %s"
                             % (what, dotted(got), want))

        # --- 3, 4: the Browser, and it knows nothing about any of this -----
        mo = eth.Mouse()
        dispcp.open_drive(m, mo, S, eth.settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, eth.settle, wx, wy, "BROWSER.O88")
        c = os88qemu.Clock(m)
        while (len(dispcp.win_list(m, S)) <= len(wins) and c.secs() < 30.0
               and not c.stalled()):
            time.sleep(0.1)
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            m.quit()
            sys.exit("pciether: BROWSER.O88 did not open a window")
        rec = m.read(S("wm_wins") + wins2[-1] * dispcp.WIN_SIZE,
                     dispcp.WIN_SIZE)
        pseg = u16(rec, 22)
        say("browser at %04X (base %06X)" % (pseg, m.lin(pseg)))

        # THE BAR HOLDS `http://` ALREADY (tests/ethernet.py): type the rest
        os88qemu.pace(m, 1.0)
        eth.type_url(URL[len("http://"):])
        os88qemu.pace(m, 1.0)
        subprocess.run(["python3", "tools/qmp.py", eth.SOCK, "sendkey ret"],
                       check=True, capture_output=True)

        def nstate():
            return m.readseg(pseg, bsyms["br_nstate"], 1)[0]

        os88qemu.acted(m, lambda: nstate() in (BN_DONE, BN_ERR), secs=60,
                       what="br_nstate", poll=0.5)
        nlines = u16(m.readseg(pseg, bsyms["br_nlines"], 2))
        say("browser: state %d, %d lines" % (nstate(), nlines))
        if nstate() != BN_DONE:
            fails.append("the fetch ended in state %d, not BN_DONE" % nstate())
        if nlines == 0:
            fails.append("br_nlines is 0: the bytes never reached the layout")

        # --- 5: the counters ----------------------------------------------
        nrx, ntx, novw = dw("eth_nrx"), dw("eth_ntx"), dw("eth_novw")
        say("counters: rx %d tx %d overflow-resets %d" % (nrx, ntx, novw))
        if not nrx or not ntx:
            fails.append("eth_nrx %d / eth_ntx %d: the card moved no frames"
                         % (nrx, ntx))
        if novw:
            fails.append("%d receive-overflow resets for a one-page fetch "
                         "(SPEC.md 125.4)" % novw)
        m.quit()
    except Exception:
        try:
            m.quit()
        except Exception:
            pass
        raise

    # --- the request the server actually saw ------------------------------
    req = srv.request
    say("server saw: %r" % (req.split(b"\r\n")[0] if req else None))
    if req is None:
        fails.append("the server was never connected to: nothing reached "
                     "the host through the card")
    else:
        if not req.startswith(b"GET %s HTTP/1.0\r\n" % PATH.encode()):
            fails.append("the request line is %r" % req.split(b"\r\n")[0])
        if b"Host: 10.0.2.2" not in req:
            fails.append("no Host: header")

    for f in fails:
        say("FAIL: " + f)
    say("pciether: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

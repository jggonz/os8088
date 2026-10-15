#!/usr/bin/env python3
"""THE PICOMEM, without one - SPEC.md 34.10.1 and 72.2.

    python3 tests/picomem.py [ne|sb]

No emulator in this tree has a PicoMEM, so this row runs the driver code that
talks to one - AS IT SHIPS, assembled from the tree - under an x86 core
(unicorn) whose port and interrupt hooks are a model of the card, written
from the firmware's own source (FreddyVRetro/ISA-PicoMEM, src/ne2000/ne2000.c
and the shipped src/rom/pmbios.bin's multiplexer, disassembled).

  ne  ETHER.DRV's 8390 core (tests/picomem/nehx.asm %includes ne2000.inc):
      probe, init, one transmit, two receives and thirty 900-byte frames
      round the ring, against TWO cards -
        * the PicoMEM's NE2000: an UN-DOUBLED PROM (so the pair test says
          8-bit) over the NE2000's packet memory, 32KB from 0x4000 - writes
          below it dropped, reads FFh. Must come out on the NE2000 map
          (ring 0x46..0x80) and move every frame byte for byte;
        * an EMPTY slot on a floating 8088 bus, answering AAh from the data
          window and C3h from the registers - what a 5150 showed as an
          NE2000 at AAAAAAAAAAAA. Must be REFUSED;
        * a genuine NE1000: 8KB at 0x2000. Must KEEP the NE1000 map
          (0x26..0x40) - the fix may not move a real card.
  sb  SOUND.DRV's PicoMEM attach (tests/picomem/sbhx.asm %includes
      picomem.inc, and sbl_f_irqdisc + its tables CUT OUT of sb.inc): with
      the card's own IRQ on 7 and on 5, pm_init must never offer that line,
      must learn it through PM BIOS function 0 (int 13h AX=6000h) and open
      its mask bit; sbl_f_irqdisc must then hook the SB on the line the card
      took WITHOUT the F2h probe (no DSP write, no tick read) and leave the
      multiplexer's vector exactly where the card's BIOS put it. And with no
      card at all: no write to 2A0h..2A2h, no int 13h, and discovery runs as
      it always did. And SPEC.md 34.10.6's keep-alive: pm_tick published
      only on a card, an address write to 388h at least twice in any 4s
      while a channel is keyed, none while nothing is, and sbl_tick still
      reached every tick (PM_KEEPT past the firmware's window goes red).

Broken on purpose (each measured): the old ne2000.inc puts the PicoMEM's
ring at 0x20 - the transmit comes from outside the card's RAM and every
receive lands outside it, which is the field report exactly (tx counted, rx
never); taking the %ifdef PICOMEM skip out of sbl_f_irqdisc makes the sb leg
FAIL on the probe running and the SB line never being hooked.

Needs nasm and the `unicorn` Python module (pip install unicorn).
"""
import os
import random
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, \
        UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_MEM_READ
    from unicorn.x86_const import UC_X86_INS_IN, UC_X86_INS_OUT, \
        UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, \
        UC_X86_REG_SS, UC_X86_REG_IP, UC_X86_REG_AX, UC_X86_REG_BX, \
        UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_EFLAGS, UC_X86_REG_SI
except ImportError:
    print("SKIP: the unicorn module is not installed (pip install unicorn)")
    sys.exit(0)

SEG = 0x1000                    # the harness's CS=DS=ES=SS: clear of the IVT
BASE = SEG << 4                 # and the BIOS data area the code reads
FAILS = []


def check(cond, what):
    print(("  ok    " if cond else "  FAIL  ") + what)
    if not cond:
        FAILS.append(what)


def assemble(tmp, src, incs, name):
    """nasm `src` with a symbol map; returns (image, {symbol: offset})."""
    wrap = os.path.join(tmp, name + ".asm")
    mapf = os.path.join(tmp, name + ".map")
    with open(wrap, "w") as f:
        f.write("[map symbols %s]\n%%include \"%s\"\n" % (mapf, src))
    out = os.path.join(tmp, name + ".com")
    cmd = ["nasm", "-f", "bin", "-w+error"]
    for i in incs:
        cmd += ["-I", i.rstrip("/") + "/"]
    subprocess.run(cmd + ["-o", out, wrap], check=True)
    syms = {}
    for line in open(mapf):
        m = re.match(r"\s*([0-9A-F]+)\s+([0-9A-F]+)\s+(\S+)\s*$", line)
        if m:
            syms[m.group(3)] = int(m.group(2), 16)
    return open(out, "rb").read(), syms


class Box:
    """One 16-bit machine: the harness at SEG:0100, ports and int 13h hooked."""

    def __init__(self, image, syms, inb, outb, int13=None):
        self.syms = syms
        self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        self.mu.mem_map(0, 0x100000)
        self.mu.mem_write(BASE + 0x100, image)
        self.int13_calls = 0

        def hin(uc, port, size, ud):
            return inb(port) & 0xFF

        def hout(uc, port, size, val, ud):
            outb(port, val & 0xFF)

        def hint(uc, intno, ud):
            if intno == 0x13 and int13:
                self.int13_calls += 1
                int13(self)

        def hcode(uc, addr, size, ud):
            if uc.mem_read(addr, 1) == b"\xf4":
                uc.emu_stop()
        self.mu.hook_add(UC_HOOK_INSN, hin, None, 1, 0, UC_X86_INS_IN)
        self.mu.hook_add(UC_HOOK_INSN, hout, None, 1, 0, UC_X86_INS_OUT)
        self.mu.hook_add(UC_HOOK_INTR, hint)
        self.mu.hook_add(UC_HOOK_CODE, hcode)

    def go(self, entry):
        mu = self.mu
        for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            mu.reg_write(r, SEG)
        mu.reg_write(UC_X86_REG_SP, 0xFFF0)
        off = self.syms[entry] if isinstance(entry, str) else entry
        mu.emu_start(BASE + off, BASE + 0xFFFF, count=5_000_000)

    def rd(self, name, n=1):
        return bytes(self.mu.mem_read(BASE + self.syms[name], n))

    def b(self, name):
        return self.rd(name)[0]

    def w(self, name):
        return int.from_bytes(self.rd(name, 2), "little")

    def lin(self, addr, n):
        return bytes(self.mu.mem_read(addr, n))

    def put(self, addr, data):
        self.mu.mem_write(addr, data)


# =============================================================================
# leg ne - the 8390
# =============================================================================
class NIC:
    """The 8390 as the PicoMEM's ne2000.c behaves (or a real NE1000)."""

    def __init__(self, kind, mac):
        if kind == "picomem":           # BX_NE2K_MEMSTART 16K, MEMSIZ 32K
            self.memstart, self.memsize = 0x4000, 32 * 1024
            self.prom = bytes(mac) + bytes(6) + b"\x57" * 20
        else:                           # NE1000: 8KB at 0x2000
            self.memstart, self.memsize = 0x2000, 8 * 1024
            self.prom = bytes(mac) + bytes(8) + b"\x42\x42" + bytes(16)
        self.mem = bytearray(self.memsize)
        self.sent = []
        self.reset()

    def reset(self):
        self.stop, self.start, self.pg, self.rdma, self.isr = 1, 0, 0, 4, 0x80
        self.pstart = self.pstop = self.bnry = self.tpsr = self.tbcr = 0
        self.rstart = self.raddr = self.rbytes = self.curr = 0
        self.rcr = self.tcr = 0
        self.par = [0] * 6

    def chipr(self, a):
        if a < 32:
            return self.prom[a]
        if self.memstart <= a < self.memstart + self.memsize:
            return self.mem[a - self.memstart]
        return 0xFF

    def chipw(self, a, v):
        if self.memstart <= a < self.memstart + self.memsize:
            self.mem[a - self.memstart] = v

    def adv(self):
        self.raddr = (self.raddr + 1) & 0xFFFF
        if self.raddr == (self.pstop << 8):
            self.raddr = self.pstart << 8
        self.rbytes = max(0, self.rbytes - 1)
        if self.rbytes == 0:
            self.isr |= 0x40

    def inb(self, off):
        if off == 0x1F:
            self.reset()
            return 0xFF
        if off >= 0x10:
            v = self.chipr(self.raddr)
            self.adv()
            return v
        if off == 0:
            return (self.pg << 6) | (self.rdma << 3) | (self.start << 1) | self.stop
        if self.pg == 0:
            return {3: self.bnry, 7: self.isr, 8: self.raddr & 0xFF,
                    9: self.raddr >> 8}.get(off, 0)
        if self.pg == 1:
            if 1 <= off <= 6:
                return self.par[off - 1]
            if off == 7:
                return self.curr
        return 0

    def outb(self, off, v):
        if off == 0x1F:
            return
        if off >= 0x10:
            if self.rbytes:
                self.chipw(self.raddr, v)
                self.adv()
            return
        if off == 0:
            if v & 0x38 == 0:
                v |= 0x20
            if v & 1:
                self.isr |= 0x80
                self.stop = 1
            else:
                self.stop = 0
            self.rdma = (v & 0x38) >> 3
            if (v & 2) and not self.start:
                self.isr &= 0x7F
            self.start = 1 if v & 2 else 0
            self.pg = (v & 0xC0) >> 6
            if v & 4 and not self.tcr:
                a = self.tpsr * 256 - self.memstart
                ok = 0 <= a and a + self.tbcr <= self.memsize
                self.sent.append(bytes(self.mem[a:a + self.tbcr]) if ok else None)
                self.isr |= 0x02
            return
        if self.pg == 0:
            if off == 1:
                self.pstart = v
            elif off == 2:
                self.pstop = v
            elif off == 3:
                self.bnry = v
            elif off == 4:
                self.tpsr = v
            elif off == 5:
                self.tbcr = (self.tbcr & 0xFF00) | v
            elif off == 6:
                self.tbcr = (self.tbcr & 0xFF) | (v << 8)
            elif off == 7:
                self.isr &= ~(v & 0x7F)
            elif off == 8:
                self.rstart = (self.rstart & 0xFF00) | v
                self.raddr = self.rstart
            elif off == 9:
                self.rstart = (self.rstart & 0xFF) | (v << 8)
                self.raddr = self.rstart
            elif off == 10:
                self.rbytes = (self.rbytes & 0xFF00) | v
            elif off == 11:
                self.rbytes = (self.rbytes & 0xFF) | (v << 8)
            elif off == 12:
                self.rcr = v
            elif off == 13:
                self.tcr = (v & 6) >> 1
        elif self.pg == 1:
            if 1 <= off <= 6:
                self.par[off - 1] = v
            elif off == 7:
                self.curr = v

    def rx_frame(self, buf):
        """ne2000.c's ne2000_rx_frame, with its pointer arithmetic."""
        if self.stop or self.pstart == 0:
            return "stopped"
        n = len(buf)
        pages = (n + 4 + 4 + 255) // 256
        if self.curr < self.bnry:
            avail = self.bnry - self.curr
        else:
            avail = (self.pstop - self.pstart) - (self.curr - self.bnry)
        if avail <= pages:
            return "full"
        if buf[:6] != b"\xff" * 6 and buf[:6] != bytes(self.par):
            return "filtered"
        nxt = self.curr + pages
        if nxt >= self.pstop:
            nxt -= self.pstop - self.pstart
        data = bytes([0x01, nxt, (n + 4) & 0xFF, (n + 4) >> 8]) + buf
        start = self.curr * 256 - self.memstart
        if start < 0:
            return "outside the card's RAM"
        if nxt > self.curr or self.curr + pages == self.pstop:
            self.mem[start:start + len(data)] = data
        else:
            end = (self.pstop - self.curr) * 256
            self.mem[start:start + end] = data[:end]
            ps = self.pstart * 256 - self.memstart
            self.mem[ps:ps + len(data) - end] = data[end:]
        self.curr = nxt
        self.isr |= 0x01
        return "ok"


def leg_ne(tmp):
    image, syms = assemble(tmp, os.path.join(HERE, "picomem", "nehx.asm"),
                           [os.path.join(ROOT, "drivers", "ether"),
                            os.path.join(ROOT, "drivers")], "nehx")
    mac = [0x28, 0xCD, 0xC1, 0x01, 0x02, 0x03]
    for kind, want in (("picomem", (1, 0x40, 0x46, 0x80)),
                       ("ne1000", (0, 0x20, 0x26, 0x40))):
        print("ne: %s" % kind)
        nic = NIC(kind, mac)
        box = Box(image, syms,
                  lambda p: nic.inb(p - 0x300) if 0x300 <= p < 0x320 else 0xFF,
                  lambda p, v: nic.outb(p - 0x300, v) if 0x300 <= p < 0x320
                  else None)
        box.go(0x100)
        got = (box.b("eth_word"), box.b("eth_txpg"), box.b("eth_pstart"),
               box.b("eth_pstop"))
        check(box.w("res_flags") & 1 == 0, "probe finds the card")
        check(box.rd("eth_mac", 6) == bytes(mac), "MAC read from the PROM")
        check(got == want, "map word=%d tx=%02X ring=%02X..%02X (want %d %02X "
              "%02X..%02X)" % (got + want))
        check(bytes(nic.par) == bytes(mac) and (nic.pstart, nic.pstop) ==
              want[2:], "the card was programmed with it")

        frame = bytes(range(100))
        box.put(BASE + syms["eth_txb"], frame)
        box.go("entry_tx")
        check(nic.sent[-1:] == [frame], "a transmit puts the frame on the wire")

        bad = []
        frames = [b"\xff" * 6 + bytes(8) + bytes((j * 7) & 0xFF for j in range(300)),
                  bytes(mac) + bytes(8) + bytes((j * 3) & 0xFF for j in range(300))]
        frames += [b"\xff" * 6 + bytes(8) + bytes((k * 13 + j) & 0xFF
                                                  for j in range(900))
                   for k in range(30)]
        for k, f in enumerate(frames):
            r = nic.rx_frame(f)
            box.go("entry_rx")
            n = box.w("res_len")
            ok = (r == "ok" and box.w("res_flags") & 1 == 0 and
                  box.lin(BASE + syms["eth_rxb"], n)[:len(f)] == f)
            if not ok:
                bad.append("frame %d: card %s" % (k, r))
                break
        check(not bad, "broadcast, unicast and 30 x 900 bytes round the ring "
              "arrive intact%s" % ((" - " + bad[0]) if bad else ""))

    # AN EMPTY SLOT. An 8088's undriven bus answers with a byte it carried a
    # moment before - an instruction the prefetcher fetched, or what the last
    # store wrote - so which byte depends on bus timing this model cannot
    # reproduce. It takes the shape the FIELD showed instead (FIELD-NOTES 66):
    # the data window answering AAh (`stosb`, and the byte each stosb then
    # writes back) and the register file a byte with bit 7 set (C3h, `ret`),
    # which passed RST and the PROM tests as an NE2000 at AAAAAAAAAAAA.
    print("ne: an empty slot (the floating 8088 bus the 5150 showed)")
    box = Box(image, syms,
              lambda p: (0xAA if p - 0x300 >= 0x10 else 0xC3)
              if 0x300 <= p < 0x320 else 0xFF,
              lambda p, v: None)
    box.go(0x100)
    check(box.w("res_flags") & 1 == 1, "an empty slot is NOT a card (probe "
          "CF=%d, MAC %s)" % (box.w("res_flags") & 1, box.rd("eth_mac", 6).hex()))


# =============================================================================
# leg sb - the attach and the IRQ
# =============================================================================
def sbslice(tmp):
    """sb.inc's candidate tables and sbl_f_irqdisc, cut out as they ship."""
    src = open(os.path.join(ROOT, "drivers", "sound", "sb.inc")).read().splitlines()
    out, take = [], False
    for line in src:
        if line.startswith("sbl_dsc_irqn:") or line.startswith("sbl_f_irqdisc:"):
            take = True
        if take:
            out.append(line)
            if line.startswith("SBL_DSC_N"):
                take = False
            elif out and out[0] != line and line.startswith("; ====") and \
                    any(l.startswith("sbl_f_irqdisc:") for l in out):
                out.pop()
                take = False
    assert any(l.startswith("sbl_f_irqdisc:") for l in out), "slice lost"
    with open(os.path.join(tmp, "sbslice.inc"), "w") as f:
        f.write("\n".join(out) + "\n")


BIOS = 0xC800                   # the PM BIOS segment int 13h reports
PMVEC = 0x2C16                  # the multiplexer's offset in the shipped ROM
DEFVEC = (0xFF53, 0xF000)       # what the ROM leaves on a line nobody hooked


def leg_sb(tmp):
    sbslice(tmp)
    image, syms = assemble(tmp, os.path.join(HERE, "picomem", "sbhx.asm"),
                           [os.path.join(ROOT, "drivers", "sound"), tmp],
                           "sbhx")
    # (the card's own IRQ, CMS on 220h) - the second is the 5150's 2.x as its
    # own Devices page showed it, CMS holding 220h-22Fh (SPEC.md 34.10.2)
    # ...and how fast channel 1's count falls while the stream plays, in
    # bytes per bus cycle (the port B reads pm_runaway paces itself by): the
    # field 286's 1.x races (FIELD-NOTES 66), real DMA at 44 kHz moves ~5
    # bytes in its window, and only the race may mask the channel
    for pmirq, cms, rate in ((7, False, 0), (7, True, 1 / 20),
                             (5, False, 1.0), (None, False, 1.0)):
        print("sb: %s" % (("PicoMEM on IRQ %d%s, channel 1 %s" % (
            pmirq, ", CMS on 220h" if cms else "",
            {0: "still", 1.0: "RACING"}.get(rate, "at a DSP's pace")))
                          if pmirq else "no PicoMEM in the machine"))
        st = {"pic": 0xFF, "ramp": 0x37, "args": [0, 0], "cmds": [],
              "pmw": 0, "dma": [], "cnt": 0x3FFF * 1.0, "ff": 0}

        def inb(p):
            if p == 0x21:
                return st["pic"]
            if p == 0x08:           # the 8237's status: its read clears the
                st["tc08"] = st.get("tc08", 0) + 1      # TC bits, so never
                return 0
            if p == 0x61:           # port B: one bus cycle, no side effect
                st["cnt"] -= rate
                return 0
            if p == 0x03:           # channel 1's count, lo then hi
                v = int(st["cnt"]) & 0xFFFF
                st["ff"] ^= 1
                return v & 0xFF if st["ff"] else v >> 8
            if pmirq and p == 0x2A3:
                st["ramp"] = (st["ramp"] + 1) & 0xFF
                return st["ramp"]
            if pmirq and p == 0x2A0:
                return 0            # READY
            if pmirq and p in (0x2A1, 0x2A2):
                return st["args"][p - 0x2A1]
            return 0xFF

        def outb(p, v):
            if p == 0x388:
                st["opl"] = st.get("opl", 0) + 1
            if p == 0x21:
                st["pic"] = v
            elif p == 0x0C:
                st["ff"] = 0
            elif p in (0x02, 0x03, 0x0A, 0x0B, 0x83):
                st["dma"].append((p, v))
            elif 0x2A0 <= p <= 0x2A2:
                st["pmw"] += 1
                if not pmirq:
                    return
                if p != 0x2A0:
                    st["args"][p - 0x2A1] = v
                    return
                arg = st["args"][0] | (st["args"][1] << 8)
                st["cmds"].append((v, arg))
                ans = 0
                if v == 0x77 and (arg & 0xFF) == pmirq:
                    ans = 1         # dev_sbdsp_set_irq_dma refuses BV_IRQ
                if v == 0x78 and arg > 1 and cms and \
                        0x220 >> 3 <= arg >> 3 <= 0x22F >> 3:
                    ans = 0x10      # dev_sbdsp_install: CMDERR_PORTUSED
                if v == 0x78 and arg > 1 and ans == 0:
                    st["sbport"] = arg  # where the card installed the DSP
                st["args"] = [ans & 0xFF, ans >> 8]

        def int13(box):
            mu = box.mu
            if mu.reg_read(UC_X86_REG_AX) == 0x6000 and \
                    mu.reg_read(UC_X86_REG_DX) == 0x1234:
                mu.reg_write(UC_X86_REG_AX, 0x2A0)
                mu.reg_write(UC_X86_REG_BX, BIOS)
                mu.reg_write(UC_X86_REG_CX, (pmirq << 8) | 0x0F)
                mu.reg_write(UC_X86_REG_DX, 0xAA55)

        box = Box(image, syms, inb, outb, int13 if pmirq else None)
        for line in (3, 5, 7):          # the IVT the ROMs left
            cell = (8 + line) * 4
            vec = (PMVEC, BIOS) if line == pmirq else DEFVEC
            box.put(cell, vec[0].to_bytes(2, "little") + vec[1].to_bytes(2, "little"))
        box.put(0x46C, b"\0\0")
        box.go(0x100)

        # THE KEEP-ALIVE (SPEC.md 34.10.6): the card disables its AdLib mix
        # after 4s without an ADDRESS write, so a held note must see one
        # inside that - and an idle chip must see none
        box.go("entry_pub")
        pub = int.from_bytes(box.rd("res_svc", 16)[6:8], "little")
        if not pmirq:
            check(pub == 0, "no PicoMEM: pm_tick is not published")
        else:
            check(pub == syms["pm_tick"], "pm_tick is published as DSV_TICK")

            def ticks(n):
                st["opl"] = 0
                sbt = box.b("res_sbt")
                for _ in range(n):
                    box.go("entry_keep")
                return st["opl"], (box.b("res_sbt") - sbt) & 0xFF
            w, t = ticks(73)            # 4 seconds: nothing keyed
            check(w == 0 and t == 73, "idle chip: no 388h write in 4s, and "
                  "sbl_tick still ran every tick (%d writes, %d ticks)" % (w, t))
            box.put(BASE + syms["opl_b0"] + 8, b"\x31")   # the tone voice keyed
            w, _ = ticks(73)
            check(w >= 2, "a keyed note: %d address writes in 4s (want >= 2,"
                  " the firmware's window being 4s)" % w)
            box.put(BASE + syms["opl_b0"] + 8, b"\x11")
            w, _ = ticks(73)
            check(w == 0, "keyed off: the writes stop (%d)" % w)

        if not pmirq:
            check(not st["dma"], "no PicoMEM: attach leaves the 8237 alone")
            box.go("entry_race")
            check(not st["dma"] and box.b("sbl_drqmask") == 0,
                  "no PicoMEM: pm_runaway leaves the 8237 alone too")
            check(st["pmw"] == 0, "no PicoMEM: not one write to 2A0h..2A2h")
            check(box.int13_calls == 0, "no PicoMEM: no int 13h AX=6000h")
            check(box.b("pm_up") == 0, "no PicoMEM: pm_up stays 0")
            box.go("entry_disc")
            check(box.b("res_dspwr") > 0,
                  "no PicoMEM: discovery runs as it always did (F2h issued)")
            continue

        sb_irq = 5 if pmirq == 7 else 7
        offered = [a & 0xFF for c, a in st["cmds"] if c == 0x77]
        check(box.b("pm_up") == 1, "the DSP was enabled")
        check(box.b("pm_pmirq") == pmirq, "the card's own line learned from "
              "PM BIOS function 0 (%d)" % box.b("pm_pmirq"))
        check(pmirq not in offered, "the card's own line never offered to "
              "the SB (offered %s)" % offered)
        check(box.b("pm_irq") == sb_irq, "the SB took IRQ %d" % box.b("pm_irq"))
        check(st["pic"] & (1 << pmirq) == 0, "the card's line is UNMASKED")
        want = 0x240 if cms else 0x220
        check(st.get("sbport") == want, "the DSP installed at %03Xh (want "
              "%03Xh; asked %s)" % (st.get("sbport", 0), want,
                                    ["%03X" % a for c, a in st["cmds"] if c == 0x78]))

        check(not st["dma"], "attach leaves the 8237 alone (%s)" % st["dma"])
        race = rate >= 1
        box.go("entry_shut")
        check(not st["dma"] and box.b("sbl_drqmask") == 0,
              "the stream's PM_RCHK ticks spent: pm_runaway asks nothing")
        box.go("entry_race")
        check(box.b("sbl_drqmask") == (4 if race else 0) and
              (((0x0A, 0x05) in st["dma"]) == race),
              "channel 1 %s: %s (sbl_drqmask %d, %s)"
              % ("racing" if race else "paced or still",
                 "MASKED, and kept so" if race else "left open",
                 box.b("sbl_drqmask"), st["dma"]))
        check(not st.get("tc08"), "the delay never reads the 8237's status "
              "(%d reads of 08h)" % st.get("tc08", 0))
        box.go("entry_disc")
        cell = lambda n: (int.from_bytes(box.lin((8 + n) * 4, 2), "little"),
                          int.from_bytes(box.lin((8 + n) * 4 + 2, 2), "little"))
        check(box.b("res_al") == 1 and box.b("sbl_irq") == sb_irq,
              "sbl_f_irqdisc answers IRQ %d" % box.b("sbl_irq"))
        check(box.b("res_dspwr") == 0 and box.b("res_ticks") == 0,
              "...without the F2h probe (DSP writes %d, tick reads %d)"
              % (box.b("res_dspwr"), box.b("res_ticks")))
        check(cell(sb_irq) == (syms["sbl_isr"], SEG),
              "sbl_isr is on IRQ %d's vector" % sb_irq)
        check(cell(pmirq) == (PMVEC, BIOS),
              "the multiplexer's vector is untouched (%04X:%04X)"
              % (cell(pmirq)[1], cell(pmirq)[0]))
        check(st["pic"] & ((1 << pmirq) | (1 << sb_irq)) == 0,
              "both lines open at the 8259 (mask %02X)" % st["pic"])
        check(box.rd("sbl_oldvec", 4) == DEFVEC[0].to_bytes(2, "little") +
              DEFVEC[1].to_bytes(2, "little"), "the SB line's old vector saved")


# =============================================================================
# leg kern - the kernel's two PicoMEM checks
# =============================================================================
def kslice(tmp):
    """kernel/disk.inc's dsk_fdd_pmemu and dsk_fdd_park_x (and the DSK_PMEM_WARM
    macro the park expands), cut out as they ship."""
    src = open(os.path.join(ROOT, "kernel", "disk.inc")).read().splitlines()
    out = []

    def cut(start, stop):
        i = next(n for n, l in enumerate(src) if l.startswith(start))
        j = next(n for n in range(i + 1, len(src)) if stop(src[n]))
        out.extend(src[i:j])
    # the ramp both restarts expand (the desktop's and kern_dos's kd_leave)
    cut("%macro DSK_PMEM_WARM", lambda l: l.startswith("%endmacro"))
    out.append("%endmacro")
    cut("dsk_fdd_pmemu:", lambda l: l.startswith("%endif"))
    cut("dsk_fdd_park_x:", lambda l: l.startswith("%endif"))
    with open(os.path.join(tmp, "kslice.inc"), "w") as f:
        f.write("\n".join(out) + "\n")


def leg_kern(tmp):
    kslice(tmp)
    image, syms = assemble(tmp, os.path.join(HERE, "picomem", "khx.asm"),
                           [tmp], "khx")
    for pm, attr in ((True, (0x80, 0x81)), (True, (0x80, 0x00)), (False, None)):
        print("kern: %s" % ("PicoMEM, floppy attributes %02X %02X" % attr
                            if pm else "no PicoMEM in the machine"))
        st = {"ramp": 0x91, "pmw": 0}

        def inb(p):
            if pm and p == 0x2A3:
                st["ramp"] = (st["ramp"] + 1) & 0xFF
                return st["ramp"]
            return 0x5A                 # a stale bus byte: it never counts

        def int13(box):
            mu = box.mu
            if mu.reg_read(UC_X86_REG_AX) == 0x6000 and \
                    mu.reg_read(UC_X86_REG_DX) == 0x1234:
                mu.reg_write(UC_X86_REG_BX, BIOS)
                mu.reg_write(UC_X86_REG_DX, 0xAA55)
        box = Box(image, syms, inb, lambda p, v: None, int13)
        if pm:                          # the card's config, 16KB above it
            box.put((BIOS + 0x400) * 16 + 0xB2, bytes(attr))
        for unit in (0, 1):
            box.go("entry_emu%d" % unit)
            served = box.w("res_flags") & 1 == 0
            want = bool(pm and attr[unit] & 0x80)
            check(served == want, "unit %d %s by the card" % (
                unit, "served" if want else "NOT served"))
        if not pm:
            check(box.int13_calls == 0, "no PicoMEM: no int 13h AX=6000h")
        # Restart: a PicoMEM machine warm-resets; any other parks and int 19h's
        box.put(0xFFFF0, b"\xf4")      # the reset vector: stop there
        calls = []
        box.mu.hook_add(UC_HOOK_INTR, lambda uc, n, ud: calls.append(n))
        box.put(0x472, b"\0\0")
        box.go("entry_park")
        ip = box.mu.reg_read(UC_X86_REG_IP)
        cs = box.mu.reg_read(UC_X86_REG_CS)
        flag = int.from_bytes(box.lin(0x472, 2), "little")
        if pm:
            check(cs * 16 + ip in (0xFFFF0, 0xFFFF1) and flag == 0x1234,
                  "Restart warm-resets (1234h at 0040:0072, FFFF:0000) - "
                  "CS:IP %04X:%04X flag %04X" % (cs, ip, flag))
            check(0x19 not in calls and 0x13 not in calls,
                  "...with no park and no int 19h (ints %s)" % calls)
        else:
            check(flag == 0 and 0x19 in calls and calls.count(0x13) == 2,
                  "Restart parks both claimed units and int 19h's as before "
                  "(ints %s, flag %04X)" % (calls, flag))


# =============================================================================
# leg mouse - USBMOUSE.DRV's PicoMEM backend (SPEC.md 9.12.7)
# =============================================================================
DSEG = 0x2000                   # where the model loads the driver
FEED = 0x0060 * 16 + 0x0449     # OSAPI_MOUSE_FEED's cell: KERNEL_SEG:0449h


def leg_mouse(tmp):
    image, syms = assemble(tmp, os.path.join(ROOT, "drivers", "usbmouse",
                                             "usbmouse.asm"),
                           [os.path.join(ROOT, "drivers", "usbmouse"),
                            os.path.join(ROOT, "drivers"),
                            os.path.join(ROOT, "apps")], "um")
    for pm in (True, False):
        print("mouse: %s" % ("no CH375, a PicoMEM on IRQ 7" if pm else
                             "neither a CH375 nor a PicoMEM"))
        st = {"ramp": 0x10, "fn": [], "feeds": [], "chained": 0,
              "imr": 0xBC,      # a 5150 ROM's IMR: IRQ 7 among the masked
              "vec10": None, "vec11": None}
        mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        mu.mem_write(DSEG * 16, image)
        mu.mem_write(FEED, b"\xcb")             # retf
        mu.mem_write(0x700, b"\xcf")            # the vector we chain to: iret
        mu.mem_write(0x33 * 4, (0x0700).to_bytes(2, "little") + bytes(2))
        # IRQ 7's vector is the PM BIOS's multiplexer, and the line MASKED -
        # function 0 leaves a hooked line as it finds it, so the driver must
        # open it as SOUND.DRV's pm_init does
        mu.mem_write(0x0F * 4, (0x0100).to_bytes(2, "little") +
                     BIOS.to_bytes(2, "little"))
        # far call to the header's dispatcher (+12: `call bp / retf`) with BP
        # = the entry, as drv_call does, then stop
        stub = b"\xbd" + syms["um_entry"].to_bytes(2, "little") + \
            b"\x9a" + (12).to_bytes(2, "little") + \
            DSEG.to_bytes(2, "little") + b"\xf4"
        mu.mem_write(0x500, stub)
        # int 33h AX=0060h as the card's IRQ does it, then stop - dispatched
        # through the IVT by hand (pushf; call far [cs:00CCh]), unicorn
        # treating `int` as a hook rather than a vector
        mu.mem_write(0x520, b"\x9c\x2e\xff\x1e\xcc\x00\xf4")

        vec = lambda: (int.from_bytes(mu.mem_read(0xCC, 2), "little"),
                       int.from_bytes(mu.mem_read(0xCE, 2), "little"))

        def hin(uc, port, size, ud):
            if pm and port == 0x2A3:
                st["ramp"] = (st["ramp"] + 1) & 0xFF
                return st["ramp"]
            if port == 0x21:
                return st["imr"]
            return 0xFF

        def hout(uc, port, size, value, ud):
            if port == 0x21:
                st["imr"] = value & 0xFF

        def hint(uc, n, ud):
            if n != 0x13:
                return
            ax, dx = uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_DX)
            if pm and dx == 0x1234 and ax >> 8 == 0x60:
                st["fn"].append(ax & 0xFF)
                if ax in (0x6010, 0x6011):
                    # what int 33h holds when the card's mouse goes on or
                    # off: a report raised at that instant is delivered
                    # through it (docs/FIELD-NOTES.md 66)
                    st["vec%02x" % (ax & 0xFF)] = vec()
                if ax == 0x6000:
                    uc.reg_write(UC_X86_REG_BX, BIOS)
                    uc.reg_write(UC_X86_REG_CX, 0x0700)
                    uc.reg_write(UC_X86_REG_DX, 0xAA55)

        def hcode(uc, addr, size, ud):
            if addr == FEED:
                sx = lambda r: r - 0x10000 if r & 0x8000 else r
                st["feeds"].append((sx(uc.reg_read(UC_X86_REG_AX)),
                                    sx(uc.reg_read(UC_X86_REG_BX)),
                                    uc.reg_read(UC_X86_REG_CX) & 0xFF,
                                    uc.reg_read(UC_X86_REG_DS)))
            elif addr == 0x700:
                st["chained"] += 1
            elif uc.mem_read(addr, 1) == b"\xf4":
                uc.emu_stop()
        mu.hook_add(UC_HOOK_INSN, hin, None, 1, 0, UC_X86_INS_IN)
        mu.hook_add(UC_HOOK_INSN, hout, None, 1, 0, UC_X86_INS_OUT)
        mu.hook_add(UC_HOOK_INTR, hint)
        mu.hook_add(UC_HOOK_CODE, hcode)

        def run(at, regs={}):
            for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES,
                      UC_X86_REG_SS):
                mu.reg_write(r, DSEG if r == UC_X86_REG_DS else 0)
            mu.reg_write(UC_X86_REG_SP, 0x7000)
            for r, v in regs.items():
                mu.reg_write(r, v)
            mu.emu_start(at, 0xFFFFF, count=1_000_000)
        ROW = 0xA5E9                                # a drv_tab row's offset
        run(0x500, {UC_X86_REG_AX: 0, UC_X86_REG_BX: ROW})   # DRVV_ATTACH
        cf = mu.reg_read(UC_X86_REG_EFLAGS) & 1
        # BX IS THE ROW on every verb (usbmouse.asm's entry note): drv_attach
        # and drv_unload read it the instruction after the call returns
        check(mu.reg_read(UC_X86_REG_BX) == ROW,
              "attach gives BX, the row, back (%04Xh)"
              % mu.reg_read(UC_X86_REG_BX))
        if not pm:
            check(cf == 1 and mu.reg_read(UC_X86_REG_AX) & 0xFF == 5,
                  "refused DRVE_HW (5), as with no CH375 before: CF=%d AL=%d"
                  % (cf, mu.reg_read(UC_X86_REG_AX) & 0xFF))
            check(not st["fn"] and vec() == (0x700, 0),
                  "no PicoMEM call made and int 33h untouched")
            check(st["imr"] == 0xBC, "and no IRQ line opened")
            continue
        check(cf == 0, "attached")
        check(st["fn"] == [0x00, 0x10], "PM BIOS detect then mouse ON (%s)"
              % ["%02Xh" % f for f in st["fn"]])
        check(vec() == (syms["um_pm33"], DSEG), "int 33h is the driver's")
        check(st["vec10"] == (syms["um_pm33"], DSEG),
              "...and was ALREADY when the card's mouse went on - a report "
              "waiting since boot is raised at that instant, and 0000:0000 "
              "is the vector table (int 33h then: %04X:%04X)"
              % (st["vec10"][1], st["vec10"][0]))
        check(st["imr"] == 0x3C, "the card's line, IRQ 7, opened and nothing "
              "else touched (IMR %02Xh)" % st["imr"])

        def report(btn, dx, dy):
            run(0x520, {UC_X86_REG_AX: 0x0060, UC_X86_REG_DX: btn,
                        UC_X86_REG_BX: dx & 0xFFFF, UC_X86_REG_CX: dy & 0xFFFF})
        report(1, 10, -6)
        report(1, 3, 1)
        # 10,-6 halves to 5,-3 exactly; 3,1 to 1,0 carrying 1 and 1
        check(st["feeds"][:2] == [(5, -3, 1, DSEG), (1, 0, 1, DSEG)],
              "two reports fed halved with the carry, left down, DS = the "
              "driver's for the fence: %s" % st["feeds"][:2])
        n = len(st["feeds"])
        report(1, 0, 0)
        check(len(st["feeds"]) == n, "a report that changes nothing is not fed")
        run(0x520, {UC_X86_REG_AX: 0x0000})
        check(st["chained"] == 1, "any other int 33h function is chained")
        st["fn"].clear()
        run(0x500, {UC_X86_REG_AX: 1, UC_X86_REG_BX: ROW})   # DRVV_DETACH
        check(mu.reg_read(UC_X86_REG_BX) == ROW,
              "detach gives BX, the row, back (%04Xh) - um_release zeroes it, "
              "and drv_unload then freed kernel offset 0 as a row and left "
              "the tick on (FIELD-NOTES 66)" % mu.reg_read(UC_X86_REG_BX))
        check(st["fn"] == [0x11], "detach turns the card's mouse OFF (%s)"
              % ["%02Xh" % f for f in st["fn"]])
        check(st["feeds"][-1:] and st["feeds"][-1][:3] == (0, 0, 0),
              "...releases the held button")
        check(st["vec11"] == (syms["um_pm33"], DSEG),
              "...BEFORE int 33h is given back, so no report lands on the old "
              "vector")
        check(vec() == (0x700, 0), "...and gives int 33h back")

# =============================================================================
# leg pmdma - tests/pmdma, the field instrument (docs/FIELD-NOTES.md 66)
# =============================================================================
# The instrument asks a PicoMEM 1.x where it copied a DMA transfer from. Its
# answer is only worth sending to the field if it names the RIGHT fault, so
# this runs the package's own pd_run against a model of the 1.x's half:
# dev_dma.cpp's snooper (one flip-flop of its own, reset by a 0Ch it sees, a
# page per channel at 83h), the copy isa_dma.cpp asks for (parameter block at
# BIOS:4364h, the bytes at BIOS:5800h) and the DSP commands the run sends -
# with the two faults the field could be showing switched in: a page register
# the card never sees, and a card that drops a write arriving too soon after
# the last one.
PMPRM = 0x4164                  # the parameter block on the FIELD card's ROM
                                # (November 2025), 200h below the source's -
                                # PMDMA finds it by the ROM's copy loop
PMSIG = bytes.fromhex("2ec43e6841" "2e8b0e6c41" "2ec5366441"
                      "fce309d1e97301a4e302f3a5" "2ec6060c4001")
PDSEG = 0x2000                  # the package's segment: page 2, so a page the
                                # card never sees cannot read as right by luck


class PmModel:
    def __init__(self, fault):
        self.fault = fault
        self.rng = random.Random(3)  # "addr": which writes it mishears - a
                                        # seed whose first E start is misheard,
                                        # so the retry is what the row tests
        self.ff = False
        self.base = [0] * 4
        self.cnt = [0] * 4
        self.page = [0] * 4
        self.seen = False       # any address/count write ever snooped
        self.ramp = 0x40
        self.q = []             # DSP output queue
        self.cmd = []           # DSP command being collected
        self.rst = 0
        self.icount = 0
        self.last = -100        # icount of the last snooped write
        self.auto = False       # an auto-init stream is running
        self.blk = 0
        self.mode = 0
        self.pos = 0            # bytes into the cycle
        self.sent = 0           # bytes since the last block IRQ
        self.irq_due = 0
        self.single = False     # the stream is ONE single-cycle transfer
        self.lowbyte = 0        # "early": the even byte of the last read

    def snoop(self, port, val, uc=None):
        if self.fault == "fast" and self.icount - self.last < 4:
            self.last = self.icount
            return              # too soon after the last one: missed
        self.last = self.icount
        if self.fault == "addr" and self.rng.random() < 0.3:
            val = port & 0xFF   # the field 1.x: the ADDRESS, sampled as data
        if self.fault == "early" and val & 0x40:
            val = self.lowbyte  # ...or sampled EARLY: what SD0-7 last held,
                                # the even byte of the last data read
        if self.fault == "fw":  # the os8088-pm114 firmware's log, version 2
            early = port & 0xFF if val & 0x40 else val  # stock read misheard
            b = BIOS * 16 + 0x5C00
            n = int.from_bytes(uc.mem_read(b + 10, 2), "little")
            raw = (port & 0x3FF) << 12          # an I/O cycle: A19-A10 zero
            smp = b"".join(bytes([early if k < 2 else val, 0x08])
                           for k in range(16))  # IOW, then the byte arrives
            uc.mem_write(b + 16 + (n % 22) * 40,
                         bytes([port & 0xFF, early, n & 0xFF, port >> 8]) +
                         raw.to_bytes(4, "little") + smp)
            uc.mem_write(b + 10, ((n + 1) & 0xFFFF).to_bytes(2, "little"))
            uc.mem_write(b + 12, (0x190).to_bytes(2, "little"))
            mode = uc.mem_read(b + 8, 1)[0]
            val = early if not mode else smp[2 * (mode - 1)]
        if port <= 7:
            ch = port >> 1
            self.ff = not self.ff
            reg = self.base if port % 2 == 0 else self.cnt
            if self.ff:
                reg[ch] = (reg[ch] & 0xFF00) | val
            else:
                reg[ch] = (reg[ch] & 0x00FF) | (val << 8)
                self.seen = True
        elif port == 0x0C:
            self.ff = False
        elif port == 0x0B and (val & 3) == 1:
            self.mode = val
        elif port == 0x83 and self.fault != "page":
            self.page[1] = val

    def dsp_cmd(self, uc, val):
        self.cmd.append(val)
        c = self.cmd
        need = {0xE1: 1, 0xD3: 1, 0x40: 2, 0x14: 3, 0x48: 3}.get(c[0], 1)
        if len(c) < need:
            return
        self.cmd = []
        if c[0] == 0xE1:
            self.q += [2, 1]
        elif c[0] == 0x48:
            self.blk = (c[1] | (c[2] << 8)) + 1
        elif c[0] == 0x1C and self.seen:
            self.auto, self.pos, self.sent = True, 0, 0
            self.single = False
        elif c[0] in (0xDA, 0xD0):
            self.auto = False
        elif c[0] == 0x14 and self.seen:
            n = c[1] | (c[2] << 8)
            n += 1
            if n <= 64:                 # PMDMA's probes: copied at once
                self.copy(uc, (self.page[1] << 16) + self.base[1], n)
            else:                       # a block of a chained stream
                self.auto, self.single = True, True
                self.pos, self.sent, self.blk = 0, 0, n

    def copy(self, uc, src, n, left=None):
        src &= 0xFFFFF                  # a 20-bit card: page 83h is page 3
        b = BIOS * 16
        if left is None:
            left = self.cnt[1] - n      # the count the 8237 is left with
        uc.mem_write(b + PMPRM, bytes([src & 0xF, 0]) +
                     ((src >> 4) & 0xFFFF).to_bytes(2, "little") +
                     (0x5800).to_bytes(2, "little") +
                     BIOS.to_bytes(2, "little") +
                     n.to_bytes(2, "little") +
                     ((src + n) & 0xFFFF).to_bytes(2, "little") +
                     (left & 0xFFFF).to_bytes(2, "little"))
        uc.mem_write(b + 0x5800, bytes(uc.mem_read(src, n)))

    def stream(self, uc):
        """One 128-byte step of an auto-init stream: the copy, and the block
        IRQ when a block is done (returned - the harness delivers it)."""
        size = self.cnt[1] + 1
        base = (self.page[1] << 16) + self.base[1]
        if self.single and self.pos >= size:
            self.auto = False           # one transfer, done
            return False
        if self.pos >= size:            # the auto-init reload
            self.pos = 0
            if self.fault == "reload":
                base = self.page[1] << 16   # ...to the page's start
                self.base[1] = 0
        n = min(128, size - self.pos)
        self.copy(uc, base + self.pos, n, size - 1 - self.pos - n)
        self.pos += n
        self.sent += n
        if self.blk and self.sent >= self.blk:
            self.sent -= self.blk
            if self.single:
                self.auto = False       # the IRQ ends it; the ISR re-arms
            return True
        return False


def leg_pmsnd(tmp):
    """PMDMA's S page against a fake driver table: the row it must find,
    SOUND.DRV's own image under it (build/sound.bin, so the offsets are this
    build's), and a counter the 'IRQs' bump while it waits its second."""
    kinc = os.path.join(ROOT, "build", "pmdma_k.inc")
    subprocess.run([sys.executable,
                    os.path.join(HERE, "pmdma", "mkkern.py"), kinc], check=True)
    k = {}
    for ln in open(kinc):
        m = re.match(r"PDK_(\w+) equ (0x[0-9A-Fa-f]+|\d+)", ln)
        if m:
            k[m.group(1)] = int(m.group(2), 0)
    image, syms = assemble(tmp, os.path.join(HERE, "pmdma", "pmdma.asm"),
                           [os.path.join(ROOT, "apps"), HERE,
                            os.path.join(ROOT, "build")], "pmdma")
    snd = open(os.path.join(ROOT, "build", "sound.bin"), "rb").read()
    KSEG, SSEG = 0x60, 0x5000
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x100000)
    mu.mem_write(PDSEG * 16, image)
    mu.mem_write(SSEG * 16, snd)
    tab = KSEG * 16 + k["DRV_TAB"]
    for r in range(k["DRV_MAX"]):           # row 0 a disk, row 2 the sound
        row = tab + r * k["DRVR_SIZE"]
        cls, seg = {0: (2, 0x4000), 2: (k["DRVC_SOUND"], SSEG)}.get(r, (0, 0))
        mu.mem_write(row + k["DRVR_CLASS"], bytes([cls]))
        mu.mem_write(row + k["DRVR_SEG"], seg.to_bytes(2, "little"))

    def put(name, val, n):
        mu.mem_write(SSEG * 16 + k[name], val.to_bytes(n, "little"))
    put("SBL_STR_ACT", 1, 1)
    put("SBL_HALF", 2048, 2)
    put("SBL_TC", 0xA6, 1)
    put("SBL_CONSUMED", 100, 2)
    stub = b"\xbd" + syms["pd_snd"].to_bytes(2, "little") + \
        b"\x9a" + (12).to_bytes(2, "little") + \
        PDSEG.to_bytes(2, "little") + b"\xf4"
    mu.mem_write(0x500, stub)
    n = {"i": 0, "end": 0x500 + len(stub) - 1}

    def hcode(uc, addr, size, ud):
        n["i"] += 1
        if n["i"] % 3000 == 0:
            t = int.from_bytes(uc.mem_read(0x46C, 2), "little")
            uc.mem_write(0x46C, ((t + 1) & 0xFFFF).to_bytes(2, "little"))
            a = SSEG * 16 + k["SBL_CONSUMED"]    # a byte played a tick
            v = int.from_bytes(uc.mem_read(a, 2), "little")
            uc.mem_write(a, ((v + 1) & 0xFFFF).to_bytes(2, "little"))
        if addr == n["end"]:
            uc.emu_stop()
    mu.hook_add(UC_HOOK_CODE, hcode)
    for r in (UC_X86_REG_CS, UC_X86_REG_ES):
        mu.reg_write(r, 0)
    mu.reg_write(UC_X86_REG_DS, PDSEG)
    mu.reg_write(UC_X86_REG_SS, 0x7000)
    mu.reg_write(UC_X86_REG_SP, 0xFFF0)
    mu.reg_write(UC_X86_REG_EFLAGS, 0x0202)
    mu.emu_start(0x500, 0xFFFFF, count=20_000_000)
    mem = bytes(mu.mem_read(PDSEG * 16, 0x10000))
    text = [t.decode() for t in re.findall(rb"[ -~]{4,}", mem[len(image):])]
    for t in text:
        if t.startswith(("S ", "  ")):
            print("    | " + t)
    row = lambda lab: next((t for t in text if t.strip().startswith(lab)), "")
    check(row("stream open").split()[-3:] == ["0001", "0001", "0000"],
          "S finds SOUND.DRV's row and reads its record: %s"
          % row("stream open"))
    check(row("block (half) bytes").split()[-3] == "0800" and
          row("time constant").split()[-3] == "00A6",
          "S reads the half and the time constant at this build's offsets")
    con = row("bytes played").split()[-3:]
    check(con and 0x64 <= int(con[0], 16) <= 0x67 and
          0x10 <= int(con[2], 16) <= 0x14,
          "S takes two snapshots a second apart: played %s" % con)
    check(row("8237 ch1 count x8") and row("loop passes in that second"),
          "S samples the live 8237 and counts the CPU it got")
    # M: channel 1 masked now, and SOUND.DRV told to keep it masked
    outs = []
    mu.hook_add(UC_HOOK_INSN, lambda uc, port, size, val, ud:
                outs.append((port, val & 0xFF)), None, 1, 0, UC_X86_INS_OUT)
    stub2 = b"\xb3m\xbd" + syms["pd_mask"].to_bytes(2, "little") + \
        stub[3:]                        # mov bl,'m' / mov bp,pd_mask / call
    mu.mem_write(0x580, stub2)          # (a fresh address: unicorn keeps the
    n["end"] = 0x580 + len(stub2) - 1   # first stub's translation cached)
    for r in (UC_X86_REG_CS, UC_X86_REG_ES):
        mu.reg_write(r, 0)
    mu.reg_write(UC_X86_REG_DS, PDSEG)
    mu.reg_write(UC_X86_REG_SP, 0xFFF0)
    mu.emu_start(0x580, 0xFFFFF, count=2_000_000)
    dq = mu.mem_read(SSEG * 16 + k["SBL_DRQMASK"], 1)[0]
    check((0x0A, 0x05) in outs and dq == 4,
          "M masks channel 1 at once and sets sbl_drqmask (%s, %d)"
          % (outs, dq))

def leg_pmdma(tmp):
    # pmdma_k.inc is the D page's offsets into THIS build (mkkern.py): the
    # one `make build/pmdma_k.inc` wrote, and the row needs it there
    kinc = os.path.join(ROOT, "build", "pmdma_k.inc")
    if not os.path.exists(kinc):
        subprocess.run([sys.executable,
                        os.path.join(HERE, "pmdma", "mkkern.py"), kinc],
                       check=True)
    image, syms = assemble(tmp, os.path.join(HERE, "pmdma", "pmdma.asm"),
                           [os.path.join(ROOT, "apps"), HERE,
                            os.path.join(ROOT, "build")], "pmdma")
    for fault, verdict, rows, everdict in (
            (None, "every way copies from the right place",
             ["RIGHT"] * 4, "the stream behaves"),
            ("page", "never sees the page register",
             ["WRONG PAGE ONLY"] * 4, "COPIES LEAVE THE RING"),
            ("fast", "back-to-back 8237 writes are missed",
             [None, "RIGHT", None, None], "the stream behaves"),
            ("addr", None, [None] * 4, None),
            ("early", None, [None] * 4, None),
            ("fw", None, [None] * 4, None),
            ("reload", "every way copies from the right place",
             ["RIGHT"] * 4, "COPIES LEAVE THE RING")):
        print("pmdma: card %s" % (fault and {"page": "blind to 83h",
                                             "addr": "hearing the port "
                                             "number as data",
                                             "early": "sampling a write "
                                             "before its data",
                                             "fw": "carrying the os8088-pm114 "
                                             "firmware's log",
                                             "fast": "dropping close writes",
                                             "reload": "reloading auto-init "
                                             "from the page's start"
                                             }[fault] or "healthy"))
        pm = PmModel(fault)
        mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        mu.mem_write(PDSEG * 16, image)
        mu.mem_write(BIOS * 16 + 0x100, PMSIG)
        if fault == "fw":
            mu.mem_write(BIOS * 16 + 0x5C00, b"DMALOG2\0")
        irqs = {"soft": 0}
        pic = {"imr": 0xBC, "opened": 0}
        stub = b"\xbd" + syms["pd_run"].to_bytes(2, "little") + \
            b"\x9a" + (12).to_bytes(2, "little") + \
            PDSEG.to_bytes(2, "little") + b"\xf4"
        mu.mem_write(0x500, stub)
        stub_end = {"at": 0x500 + len(stub) - 1}

        def hin(uc, port, size, ud):
            if port == 0x2A3:
                pm.ramp = (pm.ramp + 1) & 0xFF
                return pm.ramp
            if port == 0x22E:
                return 0x80 if pm.q else 0
            if port == 0x22A:
                return pm.q.pop(0) if pm.q else 0xFF
            if port == 0x22C:
                return 0
            if port == 0x83:
                return pm.page[1]
            if port == 0x21:
                return pic["imr"]
            if port == 0x20:
                return 0             # in service: nothing - the model's IRQs
                                     # are the multiplexer's software `int`
            return 0xFF

        def hout(uc, port, size, val, ud):
            val &= 0xFF
            if port <= 0x0C or port == 0x83:
                pm.snoop(port, val, uc)
            elif port == 0x226:
                if pm.rst and not val:
                    pm.q = [0xAA]
                    pm.cmd = []
                    pm.auto = False     # a reset stops whatever was playing
                pm.rst = val
            elif port == 0x22C:
                pm.dsp_cmd(uc, val)
            elif port == 0x21:
                pic["opened"] |= pic["imr"] & ~val
                pic["imr"] = val

        def hint(uc, n, ud):
            if n == 0x13 and uc.reg_read(UC_X86_REG_AX) == 0x6000:
                uc.reg_write(UC_X86_REG_BX, BIOS)
                uc.reg_write(UC_X86_REG_CX, 0x0500)
                uc.reg_write(UC_X86_REG_DX, 0xAA55)

        def hcode(uc, addr, size, ud):
            pm.icount += 1
            if pm.icount % 3000 == 0:          # the tick, at 0040:006C
                t = int.from_bytes(uc.mem_read(0x46C, 2), "little")
                uc.mem_write(0x46C, ((t + 1) & 0xFFFF).to_bytes(2, "little"))
            if pm.auto and pm.icount % 400 == 0 and pm.stream(uc):
                pm.irq_due += 1
            fl = uc.reg_read(UC_X86_REG_EFLAGS)
            if pm.irq_due and not pic["imr"] & 0x80 and fl & 0x200:
                # the multiplexer's software `int 0Fh` - the SB on IRQ 7, the
                # card on 5 - delivered at this instruction boundary
                pm.irq_due -= 1
                irqs["soft"] += 1
                cs, ip = uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP)
                sp = uc.reg_read(UC_X86_REG_SP)
                ss = uc.reg_read(UC_X86_REG_SS)
                sp = (sp - 6) & 0xFFFF
                uc.mem_write(ss * 16 + sp, ip.to_bytes(2, "little") +
                             cs.to_bytes(2, "little") +
                             (fl & 0xFFFF).to_bytes(2, "little"))
                uc.reg_write(UC_X86_REG_SP, sp)
                uc.reg_write(UC_X86_REG_EFLAGS, fl & ~0x200)
                vec = uc.mem_read(0x0F * 4, 4)
                uc.reg_write(UC_X86_REG_CS, int.from_bytes(vec[2:], "little"))
                uc.reg_write(UC_X86_REG_IP, int.from_bytes(vec[:2], "little"))
                return
            if addr == stub_end["at"]:
                uc.emu_stop()
        def hread(uc, access, addr, size, value, ud):
            pm.lowbyte = uc.mem_read(addr & ~1, 1)[0]
        mu.hook_add(UC_HOOK_INSN, hin, None, 1, 0, UC_X86_INS_IN)
        mu.hook_add(UC_HOOK_INSN, hout, None, 1, 0, UC_X86_INS_OUT)
        mu.hook_add(UC_HOOK_INTR, hint)
        mu.hook_add(UC_HOOK_CODE, hcode)
        if fault == "early":            # only the bytes pd_emit reads them
            lo = PDSEG * 16 + min(syms["pd_seq"], syms["pd_echo"])  # from:
            hi = PDSEG * 16 + max(syms["pd_seq"], syms["pd_echo"]) + 32
            mu.hook_add(UC_HOOK_MEM_READ, hread, None, lo, hi)  # every read
                                        # was 2.5 minutes of the leg
        for r in (UC_X86_REG_CS, UC_X86_REG_ES):
            mu.reg_write(r, 0)
        mu.reg_write(UC_X86_REG_DS, PDSEG)
        mu.reg_write(UC_X86_REG_SS, 0x7000)
        mu.reg_write(UC_X86_REG_SP, 0xFFF0)
        mu.reg_write(UC_X86_REG_EFLAGS, 0x0202)
        mu.mem_write(0x0F * 4, (0x0700).to_bytes(2, "little") + bytes(2))
        mu.mem_write(0x700, b"\xcf")          # the line's vector before: iret
        mu.emu_start(0x500, 0xFFFFF, count=40_000_000)
        check(mu.mem_read(0x0F * 4, 4) == b"\x00\x07\x00\x00",
              "int 0Fh given back as it was found")
        # the card delivers on an UNMASKED line only, so the streams open
        # the SB's candidates bar the card's own - and close them again
        check(pic["opened"] == 0x88 and pic["imr"] == 0xBC,
              "IRQ 3 and 7 opened for the streams, IMR back to BCh "
              "(opened %02Xh, now %02Xh)" % (pic["opened"], pic["imr"]))

        mem = bytes(mu.mem_read(PDSEG * 16, 0x10000))
        text = [t.decode() for t in re.findall(rb"[ -~]{4,}", mem[len(image):])]
        got = [t for t in text if "card copied from" in t]
        if fault is None:
            for t in text:
                if t.startswith(("card BIOS", "DSP base", "programmed",
                                 "  ", "A ", "B ", "C ", "D ", "VERDICT")):
                    print("    | " + t)
        check(len(got) >= 4, "four variants reported (%d)" % len(got))
        for i, want in enumerate(rows):
            row = got[i] if i < len(got) else ""
            if fault in ("addr", "early", "fw"):
                break                   # which variant it mishears is luck
            if want is None:
                check("RIGHT" not in row, "variant %s is not RIGHT: %s"
                      % ("ABCD"[i], row.strip()))
            else:
                check(want in row, "variant %s reads %s: %s"
                      % ("ABCD"[i], want, row.strip()))
        if fault is None:
            data = [t for t in text if "bytes intact" in t]
            check(all(" 64 of 64" in t for t in data[:4]) and len(data) >= 4,
                  "all 64 bytes arrive on a healthy card")
        v = [t for t in text if t.startswith("VERDICT")]
        if verdict:
            check(v and verdict in v[-1], "verdict names it: %s"
                  % (v[-1] if v else "none"))
        # G: each field swept, one character a copy (pmdma.asm's key)
        g = {}
        for t in text:
            m = re.match(r"  (lo n5, count 3F|lo n5, ECHOED|count hi n, lo 3F|"
                         r"count hi n, ECHOED|count lo nF, ECHOED|"
                         r"lo n5, EARLY \(stock\)|lo n5, LATE|count hi n, LATE|"
                         r"count lo nF, LATE)"
                         r"\s+((?:\S\S )+)", t + " ")
            if m:
                g[m.group(1)] = "".join(m.group(2).split())
        check(len(g) == (1 if fault == "fw" else 5) and
              all(len(v) == 32 for v in g.values()),
              "G reports its sweeps, 32 copies each: %s" % g)
        allg = "".join(g.values())
        if fault is None:
            check(set(allg) == {"."}, "G: a healthy card hears every copy")
            hd = [t for t in text if re.match(r"  lo [4C]5 heard as", t)]
            check(len(hd) == 2 and all(re.search(r"55[4C]5\s+page\s+0002\s+"
                                                  r"len\s+0040", t)
                                       for t in hd),
                  "G says what the card heard for 45 and C5: %s" % hd)
        if fault == "page":
            check(set(g.get("lo n5, count 3F", "")) == {"P"},
                  "G: a card blind to 83h is wrong in the page, and not as "
                  "a port number: %s" % g.get("lo n5, count 3F"))
        if fault == "addr":
            check(set(allg) & set("lhp*C") and not set(allg) & set("LHP"),
                  "G: the card's mistakes are port numbers, every one: %s"
                  % allg)
        if fault is None:
            nw = [t for t in text if t.startswith("  after 1st copy")]
            check(nw and nw[-1].split()[-1] == "077F",
                  "E prints the count the card left: 2047 - 128 = 077Fh: %s"
                  % (nw[-1].strip() if nw else "none"))
        if fault == "early":
            # the shape the field card has, made exact: every plain row is
            # wrong somewhere, every ECHOED row is right, and E is heard on
            # its first echoed start
            check(all(set(v) != {"."} for k, v in g.items()
                      if "ECHOED" not in k) and
                  all(set(v) == {"."} for k, v in g.items()
                      if "ECHOED" in k),
                  "G: an early-sampling card is cured by the echo: %s" % g)
            tr = [t for t in text if "starts until heard" in t]
            check(tr and tr[-1].split()[-1] == "2",
                  "E is heard on start 2, the first echoed one: %s"
                  % (tr[-1].strip() if tr else "none"))
            ex = [t for t in text if t.startswith("E:")]
            check(ex and "the stream behaves" in ex[-1],
                  "E's echoed stream behaves: %s" % (ex[-1] if ex else "none"))
        if fault == "fw":
            # the card's own log, v2: the stock sweep, then each write's
            # latched address and its 16-sample timeline, twice
            check(set(g.get("lo n5, EARLY (stock)", "")) - set("."),
                  "G: the stock read mishears: %s" % g)
            ctl = [t for t in text
                   if re.match(r"\s+ctrl(\s+[0-9A-F]{2}){16}\s*$", t)]
            dat = [t for t in text if re.match(r"  [0-3][0-9A-F]{3} [0-9A-F]{5} "
                                               r"e[0-9A-F]{2} :( [0-9A-F]{2}){16}$",
                                               t.rstrip())]
            check(len(ctl) == 32 and len(dat) == 32,
                  "G prints 16 logged writes twice, data and ctrl lines "
                  "(%d data, %d ctrl)" % (len(dat), len(ctl)))
            pg = [t for t in dat if t.startswith("  0083 00083 ")]
            check(pg and all(t.split()[-1] == "02" for t in pg),
                  "G shows the page write's timeline, ending on the page "
                  "the card should hear: %s" % pg[:1])
            tk = [t for t in text if "16 samples took" in t]
            check(tk and tk[0].split()[-1] == "0190",
                  "G prints the sampling time: %s" % tk[:1])
            # P: the firmware's disk-loop pump switch at BIOS:5C09, flipped
            # and flipped back, found through the same card checks R makes
            got = []
            for i in range(2):
                st = b"\xb3p\xbd" + syms["pd_pump"].to_bytes(2, "little") + \
                    b"\x9a" + (12).to_bytes(2, "little") + \
                    PDSEG.to_bytes(2, "little") + b"\xf4"
                at = 0x900 + i * 0x20
                mu.mem_write(at, st)
                stub_end["at"] = at + len(st) - 1
                mu.reg_write(UC_X86_REG_CS, 0)
                mu.reg_write(UC_X86_REG_DS, PDSEG)
                mu.reg_write(UC_X86_REG_SP, 0xFFF0)
                mu.emu_start(at, 0xFFFFF, count=2_000_000)
                got.append(mu.mem_read(BIOS * 16 + 0x5C09, 1)[0])
            check(got == [1, 0], "P flips the card's pump switch and back "
                  "(%s)" % got)
            # A: the firmware's audio health record at BIOS:5F80 ('AH01'),
            # printed as 32-bit hex and then cleared by the card on request
            ah = BIOS * 16 + 0x5F80
            mu.mem_write(ah, b"AH01" + bytes(48))
            mu.mem_write(ah + 8, (0x1234).to_bytes(4, "little"))      # empties
            mu.mem_write(ah + 20, (0x00056789).to_bytes(4, "little")) # gap us
            st = b"\xb3a\xbd" + syms["pd_ah"].to_bytes(2, "little") + \
                b"\x9a" + (12).to_bytes(2, "little") + \
                PDSEG.to_bytes(2, "little") + b"\xf4"
            mu.mem_write(0x980, st)
            stub_end["at"] = 0x980 + len(st) - 1
            mu.reg_write(UC_X86_REG_CS, 0)
            mu.reg_write(UC_X86_REG_DS, PDSEG)
            mu.reg_write(UC_X86_REG_SP, 0xFFF0)
            mu.emu_start(0x980, 0xFFFFF, count=2_000_000)
            mem = bytes(mu.mem_read(PDSEG * 16, 0x10000))
            tx = [t.decode() for t in re.findall(rb"[ -~]{4,}",
                                                  mem[len(image):])]
            emp = [t for t in tx if t.strip().startswith("FIFO empty")]
            gap = [t for t in tx if t.strip().startswith("longest gap")]
            check(emp and emp[-1].split()[-1] == "00001234" and
                  gap and gap[-1].split()[-1] == "00056789" and
                  mu.mem_read(ah + 4, 1)[0] == 1,
                  "A reads the card's audio health and asks it cleared "
                  "(%s / %s)" % (emp[-1:] , gap[-1:]))
            # B: the 'AH02' firmware's sound path switch at BIOS:5F85,
            # flipped and back - and A, on that firmware, names the path
            mu.mem_write(ah, b"AH02" + bytes(48))
            got = []
            for i in range(2):
                st = b"\xb3b\xbd" + syms["pd_bloc"].to_bytes(2, "little") + \
                    b"\x9a" + (12).to_bytes(2, "little") + \
                    PDSEG.to_bytes(2, "little") + b"\xf4"
                at = 0x9C0 + i * 0x20
                mu.mem_write(at, st)
                stub_end["at"] = at + len(st) - 1
                mu.reg_write(UC_X86_REG_CS, 0)
                mu.reg_write(UC_X86_REG_DS, PDSEG)
                mu.reg_write(UC_X86_REG_SP, 0xFFF0)
                mu.emu_start(at, 0xFFFFF, count=2_000_000)
                got.append(mu.mem_read(ah + 5, 1)[0])
            check(got == [1, 0], "B flips the card's sound path switch and "
                  "back (%s)" % got)
            mu.mem_write(ah + 5, b"\x01")
            st = b"\xb3a\xbd" + syms["pd_ah"].to_bytes(2, "little") + \
                b"\x9a" + (12).to_bytes(2, "little") + \
                PDSEG.to_bytes(2, "little") + b"\xf4"
            mu.mem_write(0xA00, st)
            stub_end["at"] = 0xA00 + len(st) - 1
            mu.reg_write(UC_X86_REG_CS, 0)
            mu.reg_write(UC_X86_REG_DS, PDSEG)
            mu.reg_write(UC_X86_REG_SP, 0xFFF0)
            mu.emu_start(0xA00, 0xFFFFF, count=2_000_000)
            mem = bytes(mu.mem_read(PDSEG * 16, 0x10000))
            tx = [t.decode() for t in re.findall(rb"[ -~]{4,}",
                                                  mem[len(image):])]
            hd = [i for i, t in enumerate(tx) if t.startswith("A  THE CARD")]
            md = tx[hd[0] + 1:hd[0] + 3] if hd else []
            check(len(md) == 2 and md[0].startswith("B: copies") and
                  "(stock)" in md[0] and
                  md[1].strip().startswith("FIFO empty"),
                  "A on 'AH02' names the sound path, then the record: %s"
                  % md[:1])
            # ...and on 'AH03' it adds the output queue's two rows
            mu.mem_write(ah, b"AH03")
            mu.mem_write(ah + 52, (0x4321).to_bytes(4, "little"))
            mu.mem_write(ah + 56, (0x21).to_bytes(4, "little"))
            mu.mem_write(ah + 60, (0x30).to_bytes(4, "little"))
            mu.mem_write(ah + 64, (0x2345).to_bytes(4, "little"))
            st = b"\xb3a\xbd" + syms["pd_ah"].to_bytes(2, "little") + \
                b"\x9a" + (12).to_bytes(2, "little") + \
                PDSEG.to_bytes(2, "little") + b"\xf4"
            mu.mem_write(0xA40, st)
            stub_end["at"] = 0xA40 + len(st) - 1
            mu.reg_write(UC_X86_REG_CS, 0)
            mu.reg_write(UC_X86_REG_DS, PDSEG)
            mu.reg_write(UC_X86_REG_SP, 0xFFF0)
            mu.emu_start(0xA40, 0xFFFFF, count=2_000_000)
            mem = bytes(mu.mem_read(PDSEG * 16, 0x10000))
            tx = [t.decode() for t in re.findall(rb"[ -~]{4,}",
                                                  mem[len(image):])]
            hd = [i for i, t in enumerate(tx) if t.startswith("A  THE CARD")]
            rows = tx[hd[0]:] if hd else []
            sil = [t for t in rows if t.strip().startswith("speaker fed")]
            slm = [t for t in rows if t.strip().startswith("...the longest")]
            qbf = [t for t in rows if t.strip().startswith("mixes run inside")]
            hep = [t for t in rows if t.strip().startswith("samples DROPPED")]
            cl = [i for i, t in enumerate(rows) if t.strip().startswith("(cleared")]
            check(sil and sil[0].split()[-1] == "00004321" and
                  slm and slm[0].split()[-1] == "00000021" and
                  qbf and qbf[0].split()[-1] == "00000030" and
                  hep and hep[0].split()[-1] == "00002345" and cl and
                  rows.index(hep[0]) < cl[0],
                  "A on 'AH03' adds the speaker's silence rows: %s %s"
                  % (sil[:1], slm[:1]))
            # ...and 'AH04' the silence by what core 0 ran, after them
            mu.mem_write(ah, b"AH04")
            mu.mem_write(ah + 68 + 4 * 8, (0x0777).to_bytes(4, "little"))
            mu.mem_write(ah + 68 + 4 * 11, (0x0011).to_bytes(4, "little"))
            st = b"\xb3a\xbd" + syms["pd_ah"].to_bytes(2, "little") + \
                b"\x9a" + (12).to_bytes(2, "little") + \
                PDSEG.to_bytes(2, "little") + b"\xf4"
            mu.mem_write(0xA80, st)
            stub_end["at"] = 0xA80 + len(st) - 1
            mu.reg_write(UC_X86_REG_CS, 0)
            mu.reg_write(UC_X86_REG_DS, PDSEG)
            mu.reg_write(UC_X86_REG_SP, 0xFFF0)
            mu.emu_start(0xA80, 0xFFFFF, count=2_000_000)
            mem = bytes(mu.mem_read(PDSEG * 16, 0x10000))
            tx = [t.decode() for t in re.findall(rb"[ -~]{4,}",
                                                  mem[len(image):])]
            hd = [i for i, t in enumerate(tx) if t.startswith("A  THE CARD")]
            rows = tx[hd[0]:] if hd else []
            sw = [t for t in rows if t.strip().startswith("silent while:")]
            hp = [i for i, t in enumerate(rows) if t.strip().startswith("samples DROPPED")]
            check(len(sw) == 12 and "SD card wait" in sw[8] and
                  sw[8].split()[-1] == "00000777" and
                  sw[11].split()[-1] == "00000011" and hp and
                  rows.index(sw[0]) > hp[0],
                  "A on 'AH04' adds silence by site, after the AH03 rows: %s"
                  % sw[8:9])
            # ...and 'AH05' where the last silence and the longest run fell
            mu.mem_write(ah, b"AH05")
            mu.mem_write(ah + 116, (0x0A15).to_bytes(4, "little"))
            mu.mem_write(ah + 120, (0x0614).to_bytes(4, "little"))
            st = b"\xb3a\xbd" + syms["pd_ah"].to_bytes(2, "little") + \
                b"\x9a" + (12).to_bytes(2, "little") + \
                PDSEG.to_bytes(2, "little") + b"\xf4"
            mu.mem_write(0xAC0, st)
            stub_end["at"] = 0xAC0 + len(st) - 1
            mu.reg_write(UC_X86_REG_CS, 0)
            mu.reg_write(UC_X86_REG_DS, PDSEG)
            mu.reg_write(UC_X86_REG_SP, 0xFFF0)
            mu.emu_start(0xAC0, 0xFFFFF, count=2_000_000)
            mem = bytes(mu.mem_read(PDSEG * 16, 0x10000))
            tx = [t.decode() for t in re.findall(rb"[ -~]{4,}",
                                                  mem[len(image):])]
            hd = [i for i, t in enumerate(tx) if t.startswith("A  THE CARD")]
            rows = tx[hd[0]:] if hd else []
            sl = [t for t in rows if t.strip().startswith("last silence")]
            sm = [t for t in rows if t.strip().startswith("longest run:")]
            sw = [i for i, t in enumerate(rows) if t.strip().startswith("silent while:")]
            check(sl and sl[0].split()[-1] == "00000A15" and
                  sm and sm[0].split()[-1] == "00000614" and sw and
                  rows.index(sl[0]) > sw[-1],
                  "A on 'AH05' names where the last silence and the longest "
                  "run fell: %s %s" % (sl[:1], sm[:1]))
            # K: the firmware's 'DK01' disk record at BIOS:5BC0, read as
            # decimals and cleared (pd_drec; K's timed rows want a kernel)
            dk = BIOS * 16 + 0x5BC0
            mu.mem_write(dk, b"DK01" + bytes(60))
            mu.mem_write(dk + 12, (25600).to_bytes(4, "little"))   # sectors
            mu.mem_write(dk + 32, (187).to_bytes(4, "little"))     # SD us/sect
            mu.mem_write(PDSEG * 16 + syms["pd_kseg"], BIOS.to_bytes(2, "little"))
            st = b"\xb3k\xbd" + syms["pd_drec"].to_bytes(2, "little") + \
                b"\x9a" + (12).to_bytes(2, "little") + \
                PDSEG.to_bytes(2, "little") + b"\xf4"
            mu.mem_write(0xB00, st)
            stub_end["at"] = 0xB00 + len(st) - 1
            mu.reg_write(UC_X86_REG_CS, 0)
            mu.reg_write(UC_X86_REG_DS, PDSEG)
            mu.reg_write(UC_X86_REG_SP, 0xFFF0)
            mu.emu_start(0xB00, 0xFFFFF, count=4_000_000)
            mem = bytes(mu.mem_read(PDSEG * 16, 0x10000))
            tx = [t.decode() for t in re.findall(rb"[ -~]{4,}",
                                                  mem[len(image):])]
            rows = tx
            se = [t for t in rows if t.startswith("sectors read")]
            sd = [t for t in rows if t.startswith("SD us per sector")]
            check(se and se[-1].split()[-1] == "25600"
                  and sd and sd[-1].split()[-1] == "187" and
                  any("MULTI-SECTOR" in t for t in rows) and
                  mu.mem_read(dk + 5, 1)[0] == 1,
                  "K reads the card's 'DK01' disk record and asks it cleared:"
                  " %s %s" % (se[-1:], sd[-1:]))
            got = []
            for i in range(2):
                st = b"\xb3f\xbd" + syms["pd_dflip"].to_bytes(2, "little") + \
                    b"\x9a" + (12).to_bytes(2, "little") + \
                    PDSEG.to_bytes(2, "little") + b"\xf4"
                at = 0xB40 + i * 0x20
                mu.mem_write(at, st)
                stub_end["at"] = at + len(st) - 1
                mu.reg_write(UC_X86_REG_CS, 0)
                mu.reg_write(UC_X86_REG_DS, PDSEG)
                mu.reg_write(UC_X86_REG_SP, 0xFFF0)
                mu.emu_start(at, 0xFFFFF, count=2_000_000)
                got.append(mu.mem_read(dk + 4, 1)[0])
            check(got == [1, 0], "F flips the card's disk read path and back "
                  "(%s)" % got)
            # I's derived line: (row - twin) hundredths of a us over 512
            # bytes, as ns - 1,300.00 us against 200.00 us is 2,148 ns a byte
            tw = PDSEG * 16 + syms["pd_t_rr"]
            mu.mem_write(tw, (20000).to_bytes(4, "little"))
            st = b"\xbe" + syms["pd_isa_l9"].to_bytes(2, "little") + \
                b"\xb8" + (130000 & 0xFFFF).to_bytes(2, "little") + \
                b"\xba" + (130000 >> 16).to_bytes(2, "little") + \
                b"\xbb" + syms["pd_t_rr"].to_bytes(2, "little") + \
                b"\xbd" + syms["pd_isa_net"].to_bytes(2, "little") + \
                b"\x9a" + (12).to_bytes(2, "little") + \
                PDSEG.to_bytes(2, "little") + b"\xf4"
            mu.mem_write(0xB80, st)
            stub_end["at"] = 0xB80 + len(st) - 1
            mu.reg_write(UC_X86_REG_CS, 0)
            mu.reg_write(UC_X86_REG_DS, PDSEG)
            mu.reg_write(UC_X86_REG_SP, 0xFFF0)
            mu.emu_start(0xB80, 0xFFFFF, count=2_000_000)
            mem = bytes(mu.mem_read(PDSEG * 16, 0x10000))
            tx = [t.decode() for t in re.findall(rb"[ -~]{4,}",
                                                  mem[len(image):])]
            ns = [t for t in tx if t.startswith("card read, ns a byte")]
            check(ns and ns[-1].split()[-1] == "2148",
                  "I nets a row against its RAM twin, ns a byte: %s" % ns[-1:])
            # benchlib's BL_NUMBERED: a save goes to the first NAMEn.EXT the
            # disk has not got. OSAPI_FILE_READ's cell answers out of `disk`:
            # FERR_NOENT (4) for a name not on it, FERR_BIG (10) for one that
            # is - a zero-byte read of a non-empty file, as the kernel does
            fr = 0x0060 * 16 + 0x0109
            mu.mem_write(fr, b"\xcb")            # retf, after the hook answers
            disk = set()

            def hfile(uc, addr, size, ud):
                name = bytes(uc.mem_read(PDSEG * 16 +
                                         uc.reg_read(UC_X86_REG_SI), 13))
                name = name.split(b"\0")[0].decode("latin-1")
                fl = uc.reg_read(UC_X86_REG_EFLAGS) | 1
                uc.reg_write(UC_X86_REG_EFLAGS, fl)
                uc.reg_write(UC_X86_REG_AX, 10 if name in disk else 4)
            hk = mu.hook_add(UC_HOOK_CODE, hfile, None, fr, fr)

            def numname(name):
                src = PDSEG * 16 + syms["pd_s_ifile"]
                mu.mem_write(src, name.encode() + b"\0")
                st = b"\xbe" + syms["pd_s_ifile"].to_bytes(2, "little") + \
                    b"\xbd" + syms["bl_numname"].to_bytes(2, "little") + \
                    b"\x9a" + (12).to_bytes(2, "little") + \
                    PDSEG.to_bytes(2, "little") + b"\xf4"
                mu.mem_write(0xBC0, st)
                stub_end["at"] = 0xBC0 + len(st) - 1
                mu.reg_write(UC_X86_REG_CS, 0)
                mu.reg_write(UC_X86_REG_DS, PDSEG)
                mu.reg_write(UC_X86_REG_SP, 0xFFF0)
                mu.emu_start(0xBC0, 0xFFFFF, count=2_000_000)
                si = mu.reg_read(UC_X86_REG_SI)
                got = bytes(mu.mem_read(PDSEG * 16 + si, 13))
                return got.split(b"\0")[0].decode("latin-1")
            seq = []
            for i in range(3):                  # three saves on an empty disk
                seq.append(numname("PMBUS.TXT"))
                disk.add(seq[-1])
            disk |= {"VIDDISK%d.TXT" % n for n in range(1, 10)}
            disk.discard("VIDDISK4.TXT")        # a gap is reused, first
            seq.append(numname("VIDDISK.TXT"))
            disk.add("VIDDISK4.TXT")
            seq.append(numname("VIDDISK.TXT"))  # ...and past 9 the base is cut
            disk |= {"VIDDIS%d.TXT" % n for n in range(10, 100)}
            seq.append(numname("VIDDISK.TXT"))  # all 99 taken: the plain name
            check(seq == ["PMBUS1.TXT", "PMBUS2.TXT", "PMBUS3.TXT",
                          "VIDDISK4.TXT", "VIDDIS10.TXT", "VIDDISK.TXT"],
                  "a numbered save takes the first NAMEn.EXT not on the "
                  "disk, cut to 8.3: %s" % seq)
            mu.mem_write(PDSEG * 16 + syms["pd_s_ifile"], b"PMBUS.TXT\0")
            mu.hook_del(hk)
        if fault == "addr":
            tr = [t for t in text if "starts until heard" in t]
            check(tr and int(tr[-1].split()[-1]) > 1,
                  "E needed more than one start, and counted them: %s"
                  % (tr[-1].strip() if tr else "none"))
            ex = [t for t in text if t.startswith("E:")]
            check(ex and "LEAVE THE RING" not in ex[-1],
                  "E's verified start keeps the stream in the ring: %s"
                  % (ex[-1] if ex else "none"))
        e = [t for t in text if t.startswith("E:")]
        if everdict:
            check(e and everdict in e[-1], "the stream's verdict: %s"
                  % (e[-1] if e else "none"))
        if fault == "page":
            # where the stray copy went is printed: page 0, not the ring's 2
            fo = [t for t in text if "first outside from" in t]
            if fo:
                print("    | " + fo[0])
            check(fo and fo[0].split()[3].startswith("0"),
                  "the first stray copy is placed: %s"
                  % (fo[0].strip() if fo else "none"))
        if fault is None:
            for t in text:
                if t.startswith(("E ", "G ", "   C ", "  addr", "  page",
                                 "  lo ", "  count", "  high", "  addr lo", "   1 ", "   6 ", "  starts", "  after",
                                 "  copies",
                                 "  ...", "  bytes not", "  block IRQs",
                                 "E:")):
                    print("    | " + t)
            check(irqs["soft"] > 0, "block IRQs were delivered (%d)"
                  % irqs["soft"])
            hw = [t for t in text if t.startswith("  ...as real IRQs")]
            check(hw and hw[-1].split()[-1] == "0",
                  "software ints are not scored as real IRQs: %s"
                  % (hw[-1] if hw else "none"))


def main():
    legs = sys.argv[1:] or ["ne", "sb", "kern", "mouse", "pmdma", "pmsnd"]
    with tempfile.TemporaryDirectory() as tmp:
        if "ne" in legs:
            leg_ne(tmp)
        if "sb" in legs:
            leg_sb(tmp)
        if "kern" in legs:
            leg_kern(tmp)
        if "mouse" in legs:
            leg_mouse(tmp)
        if "pmdma" in legs:
            leg_pmdma(tmp)
        if "pmsnd" in legs:
            leg_pmsnd(tmp)
    if FAILS:
        print("picomem: FAIL (%d)" % len(FAILS))
        return 1
    print("picomem: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

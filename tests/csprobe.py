#!/usr/bin/env python3
"""REDLINE's chipset probes and writes against a model of each board
(SPEC.md 103.6.1-103.6.5).

apps/redline/csrows.inc is assembled on its own (tests/csprobe/harness.asm)
and cs_identify is run under Unicorn with every IN and OUT trapped. A probe is
read-only except for the index bytes 103.6.1 allows, and on a board that is
NOT its chipset it is still running - so the property that matters is the
CROSS-MATRIX: every probe against every model, and the ports it touched.

Checked, per board and per CPU class:
  * the row found is the board's, and no other row matches it;
  * no port outside 20h/21h (read), 22h/23h, ECh/EDh, 1EDh/1EFh is touched;
  * EEh and EFh are never touched (a read of EFh resets a SCAMP or TOPCAT);
  * 22h is written only 40h or 60h-67h (OCW2 NOP / specific EOI on an idle
    level, to an 8259 that 22h aliases), ECh only 00h, 1EDh only 04h and the
    index it found;
  * an 8088/8086 touches NO port at all;
  * a 386 never touches 22h/23h, and a 286 whose 22h/23h mirror the 8259
    never writes them;
  * the BIOS fingerprint is CRC-16/CCITT (init FFFFh) of F000:0000-FFFF.

Break it on purpose: make cs_id_neat write 0x68 to 22h, or drop the CPU_286
test in cs_identify, and the matrix goes red naming the board and the port.

THE WRITE SIDE (tests/csprobe/feharness.asm: csfe.inc and csapply.inc):
  * every FE3001 write is 63h=04h, 72h, 73h, 63h=84h, with interrupts OFF
    at each OUT, and no other port is written;
  * the verified sweep against a board whose reads go wrong below R4 = T,
    for every T: it finds the fastest even step that reads right, leaves R4
    there, and interrupts are never enabled while R4 sits below T;
  * REDLINE.CFG: Save's bytes are 103.6.4's layout with this machine's
    fingerprint, and Apply writes NOTHING unless every field of the
    fingerprint, the signature, the length, the row and the knob check out.
Break it: make cs_fe_try set the verdict after its popf, or drop the model
byte from cs_cfg_load's compare, and this goes red.
"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT,
                                   UC_X86_REG_AX, UC_X86_REG_EFLAGS,
                                   UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_DS,
                                   UC_X86_REG_ES, UC_X86_REG_SS)
except ImportError:
    print("SKIP: unicorn not installed (pip install unicorn)")
    sys.exit(0)

ROWS = ["NO", "F3", "CQ", "SM", "HL", "SC", "SX", "NE", "W7", "O3"]
CSO = {0: "notrun", 1: "skipcpu", 2: "skipali", 3: "absent", 4: "FOUND", 5: "pick"}


def build(src="tests/csprobe/harness.asm"):
    d = tempfile.mkdtemp(prefix="csprobe")
    out, lst = os.path.join(d, "h.bin"), os.path.join(d, "h.lst")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                           "-l", lst, "-o", out, src],
                          cwd=ROOT)
    syms = {}
    import re
    pend = []
    for ln in open(lst, errors="replace"):
        m = re.match(r"\s*\d+\s+([0-9A-F]{8})\s", ln)
        lab = re.search(r"(?:^|\s)([A-Za-z_]\w*):", ln[40:])
        if lab:
            pend.append(lab.group(1))
        if m:
            for p in pend:
                syms.setdefault(p, int(m.group(1), 16) + 0x100)
            pend = []
    return open(out, "rb").read(), syms


_CRCT = []
for _i in range(256):
    _c = _i << 8
    for _ in range(8):
        _c = ((_c << 1) ^ 0x1021) if _c & 0x8000 else _c << 1
    _CRCT.append(_c & 0xFFFF)


def crc16(data):
    """CRC-16/CCITT, init FFFFh - 103.6.1's fingerprint (table-driven)."""
    c = 0xFFFF
    for b in data:
        c = ((c << 8) & 0xFFFF) ^ _CRCT[(c >> 8) ^ b]
    return c


def forge(rom, want, at=0x100):
    """Make crc16(rom) == want by changing the two bytes at `at`. Over a
    fixed length the CRC is AFFINE in the data, so flipping bit k of those
    two bytes XORs a fixed vector into it: sixteen CRCs give the matrix and
    Gaussian elimination over GF(2) the flips. This is how the allowlist is
    tested with no BIOS dump in the tree."""
    rom = bytearray(rom)
    base = crc16(rom)
    cols = []
    for k in range(16):
        r = bytearray(rom)
        r[at + k // 8] ^= 1 << (k % 8)
        cols.append(crc16(r) ^ base)
    need, rows = base ^ want, []
    for bit in range(16):               # one equation per CRC bit
        rows.append(sum(((cols[k] >> bit) & 1) << k for k in range(16))
                    | (((need >> bit) & 1) << 16))
    piv = []
    for col in range(16):
        r = next((i for i in range(len(piv), 16)
                  if (rows[i] >> col) & 1), None)
        if r is None:
            continue
        i = len(piv)
        rows[i], rows[r] = rows[r], rows[i]
        for j in range(16):
            if j != i and (rows[j] >> col) & 1:
                rows[j] ^= rows[i]
        piv.append(col)
    for i, col in enumerate(piv):
        if (rows[i] >> 16) & 1:
            rom[at + col // 8] ^= 1 << (col % 8)
    assert crc16(rom) == want
    return bytes(rom)


class Board:
    """Unmapped ports float FFh; the 8259 answers 20h/21h with full decode."""
    name = "plain AT"
    row = "NO"
    aliased = False

    def __init__(self):
        self.imr, self.irr = 0xB8, 0x00
        self.idx = {}

    def rd(self, p):
        if p == 0x20:
            return self.irr
        if p == 0x21:
            return self.imr
        if self.aliased and p == 0x22:
            return self.irr
        if self.aliased and p == 0x23:
            return self.imr
        return self.read(p)

    def wr(self, p, v):
        self.write(p, v)

    def read(self, p):
        return 0xFF

    def write(self, p, v):
        pass


class Aliased(Board):
    name = "AT, 22h/23h alias the 8259"
    aliased = True


class Indexed(Board):
    """index port / data port, registers as a dict; unknown index floats."""
    iport = dport = None
    regs = {}
    idx_reads = False

    def __init__(self):
        super().__init__()
        self.i = 0

    def read(self, p):
        if p == self.iport:
            return self.i if self.idx_reads else 0xFF
        if p == self.dport:
            return self.regs.get(self.i, 0xFF)
        return 0xFF

    def write(self, p, v):
        if p == self.iport:
            self.i = v


def mk(cls_name, row, iport, dport, regs, idx_reads=False):
    return type(cls_name, (Indexed,), dict(name=cls_name, row=row, iport=iport,
                                           dport=dport, regs=regs,
                                           idx_reads=idx_reads))


BOARDS = [Board, Aliased,
          mk("VLSI SCAMP 311", "SM", 0xEC, 0xED, {0: 0xD6}, True),
          mk("VLSI SCAMP 310", "SM", 0xEC, 0xED, {0: 0xD1}, True),
          mk("VLSI SCAMP II", "SM", 0xEC, 0xED, {0: 0x6A}, True),
          mk("VLSI TOPCAT 320", "SM", 0xEC, 0xED, {0: 0xE0}, True),
          mk("Headland HT12", "HL", 0x1ED, 0x1EF, {4: 0x10}, True),
          mk("C&T SCAT", "SC", 0x22, 0x23, {0x40: 0x01}),
          mk("C&T SCAT 82C836", "SC", 0x22, 0x23, {0x40: 0x04}),
          mk("C&T SCATsx", "SX", 0x22, 0x23, {0x40: 0x13}),
          mk("C&T NEAT", "NE", 0x22, 0x23,
             {0x60: 0x00, 0x61: 0x5D, 0x62: 0x3C, 0x64: 0x20}),
          mk("C&T NEAT (rev 0)", "NE", 0x22, 0x23,
             {0x60: 0x40, 0x62: 0x04, 0x64: 0x00}),
          ]

ALLOWED_RD = {0x20, 0x21, 0x22, 0x23, 0xEC, 0xED, 0x1ED, 0x1EF}


def run(img, syms, board, tier, rom):
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x100000)
    mu.mem_write(0x100, img)
    mu.mem_write(0xF0000, rom)
    mu.mem_write(syms["h_tier"], bytes([tier]))
    for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
        mu.reg_write(r, 0)
    mu.reg_write(UC_X86_REG_SP, 0xFFF0)
    log = []

    def hin(uc, port, size, user):
        v = board.rd(port)
        log.append(("in", port, v))
        return v

    def hout(uc, port, size, value, user):
        log.append(("out", port, value & 0xFF))
        board.wr(port, value & 0xFF)

    mu.hook_add(UC_HOOK_INSN, hin, None, 1, 0, UC_X86_INS_IN)
    mu.hook_add(UC_HOOK_INSN, hout, None, 1, 0, UC_X86_INS_OUT)
    end = syms["h_start"] + 3            # the hlt after the call
    mu.emu_start(syms["h_start"], end, count=5_000_000)
    pres = mu.mem_read(syms["cs_pres"], len(ROWS))
    found = mu.mem_read(syms["cs_found"], 1)[0]
    crc = int.from_bytes(mu.mem_read(syms["cs_crc"], 2), "little")
    return log, list(pres), found, crc


def main():
    img, syms = build()
    rom = bytearray(os.urandom(0x10000))
    rom[0xFFF5:0xFFFD] = b"10/13/26"
    rom[0xFFFE] = 0xFC
    fails = []
    checked = 0

    def bad(msg):
        fails.append(msg)
        print("FAIL", msg)

    for tier, tname in ((0, "8088"), (1, "286"), (2, "386")):
        for B in BOARDS:
            b = B()
            log, pres, found, crc = run(img, syms, b, tier, bytes(rom))
            tag = f"{tname} on {b.name}"
            checked += 1
            if crc != crc16(rom):
                bad(f"{tag}: fingerprint {crc:04X}, want {crc16(rom):04X}")
            ports = {p for _, p, _ in log}
            if tier == 0 and log:
                bad(f"{tag}: an 8088 touched ports {sorted(map(hex, ports))}")
            for d, p, v in log:
                if p in (0xEE, 0xEF):
                    bad(f"{tag}: touched {p:X}h")
                if d == "in" and p not in ALLOWED_RD:
                    bad(f"{tag}: read port {p:X}h")
                if d == "out":
                    ok = ((p == 0x22 and (v == 0x40 or 0x60 <= v <= 0x67))
                          or (p == 0xEC and v == 0x00)
                          or (p == 0x1ED))
                    if not ok:
                        bad(f"{tag}: wrote {v:02X}h to port {p:X}h")
            if tier == 2 and ports & {0x22, 0x23}:
                bad(f"{tag}: a 386 touched 22h/23h")
            if tier == 1 and b.aliased and any(d == "out" and p in (0x22, 0x23)
                                               for d, p, _ in log):
                bad(f"{tag}: wrote 22h/23h while they alias the 8259")
            # which row should be found on this CPU class
            want = b.row
            if tier == 0 and want != "NO":
                want = "NO"
            if tier == 2 and want in ("SC", "SX", "NE"):
                want = "NO"
            if ROWS[found] != want:
                bad(f"{tag}: found {ROWS[found]}, want {want} "
                    f"({' '.join(r + '=' + CSO[s] for r, s in zip(ROWS, pres))})")
            nfound = sum(1 for s in pres if s == 4)
            if nfound > 1:
                bad(f"{tag}: {nfound} rows matched one board "
                    f"({' '.join(r for r, s in zip(ROWS, pres) if s == 4)})")
            if tier == 1 and b.aliased and any(
                    pres[ROWS.index(r)] != 2 for r in ("SC", "SX", "NE")):
                bad(f"{tag}: the 22h rows were not skipped as aliased")
    # THE FE3600 ALLOWLIST: the Packard Bell Victory's BIOS (RetroWeb's
    # dump, which is not in the tree) is CRC 2016h dated 01/15/88, and the
    # owner's own Victory - another ROM, the same date - BA37h. A ROM
    # forged to that CRC and date is found as the FE3600 on every CPU class,
    # because the FE3600 is identified by its BIOS alone and touches no port;
    # the same ROM a day out, or a CRC out, is not
    vic = bytearray(os.urandom(0x10000))
    vic[0xFFF5:0xFFFD] = b"01/15/88"
    vic[0xFFFE] = 0xFC
    for tier in (1, 2):
        for date, want, row in ((b"01/15/88", 0x2016, "F3"),
                                (b"01/15/88", 0xBA37, "F3"),
                                (b"01/16/88", 0xBA37, "NO"),
                                (b"01/16/88", 0x2016, "NO"),
                                (b"01/15/88", 0x2017, "NO")):
            vic[0xFFF5:0xFFFD] = date
            rom = forge(vic, want)
            log, pres, found, crc = run(img, syms, Board(), tier, rom)
            checked += 1
            if ROWS[found] != row:
                bad("tier %d, BIOS %04X %s: found %s, want %s"
                    % (tier, want, date.decode(), ROWS[found], row))
    # the Compaq signature
    rq = bytearray(rom)
    rq[0xFFEA:0xFFF0] = b"COMPAQ"
    log, pres, found, crc = run(img, syms, Board(), 1, bytes(rq))
    checked += 1
    if ROWS[found] != "CQ":
        bad(f"286 Compaq ROM: found {ROWS[found]}")
    checked += write_side(bad)
    print(f"csprobe: {checked} runs, {len(fails)} failures")
    return 1 if fails else 0


# --------------------------------------------------------------------------
# THE WRITE SIDE
# --------------------------------------------------------------------------
def fe_dec(v):
    return ((v >> 4) & 0xC) | ((v >> 2) & 3)


class FEBoard:
    """An FE3001: 63h lock, 72h/73h pointer and value, all write-only; reads
    of the option ROM and the screen go WRONG while R4 < T, and so does a
    PicoMEM's 2A3h counter when one is fitted."""

    def __init__(self, T, mu, pico=False):
        self.T, self.mu, self.pico = T, mu, pico
        self.locked, self.ptr, self.r4 = True, None, 12
        self.bad, self.log, self.ctr = [], [], 0
        self.rom = bytes([0x55, 0xAA, 0x40]) + bytes((i * 37 + 11) & 0xFF
                                                     for i in range(509))
        self.scr = bytes((i * 13 + 5) & 0xFF for i in range(512))
        mu.mem_write(0xC0000, self.rom)
        mu.mem_write(0xA0000, self.scr)
        self.wrong = False

    def apply(self):
        wrong = self.r4 < self.T
        if wrong != self.wrong:
            self.wrong = wrong
            x = (lambda b: bytes(c ^ 0x5A for c in b)) if wrong else (lambda b: b)
            self.mu.mem_write(0xC0000 + 3, x(self.rom[3:]))
            self.mu.mem_write(0xA0000, x(self.scr))

    book = 10
    fdc = 0x80                      # an idle 765's main status: RQM
    page = 0x00                     # DMA page register 85h

    def io_rd(self, p):
        """The two I/O references every board has: 3F4h and 85h. Both read
        wrong while the timing is too short."""
        if p == 0x3F4:
            return (self.fdc ^ 0x40) if self.wrong else self.fdc
        if p == 0x85:
            return (self.page ^ 0x5A) if self.wrong else self.page
        return None

    def rd(self, p):
        if p == 0x2A3 and self.pico:
            self.ctr = (self.ctr + (7 if self.wrong else 1)) & 0xFF
            return self.ctr
        v = self.io_rd(p)
        if v is not None:
            return v
        self.bad.append("read port %Xh" % p)
        return 0xFF

    def wr(self, p, v, iflag):
        if p == 0x85:               # the page register: written, not logged
            self.page = v           # (the sweep's I/O write check)
            return
        self.log.append((p, v))
        if iflag:
            self.bad.append("OUT %Xh with interrupts ON" % p)
        if p == 0x63:
            if v not in (0x04, 0x84):
                self.bad.append("63h written %02Xh" % v)
            self.locked = v == 0x84
        elif p in (0x72, 0x73):
            if self.locked:
                self.bad.append("%Xh written while locked" % p)
            if p == 0x72:
                self.ptr = fe_dec(v)
            elif self.ptr == 4:
                self.r4 = fe_dec(v)
                self.apply()
        else:
            self.bad.append("wrote port %Xh" % p)


class NEBoard(FEBoard):
    """A C&T NEAT: 22h index, 23h data, a FRESH 22h write before every 23h
    access [NEAT p29]; its registers read back. 62h bits 3:2 are the 8-bit
    waits (00 = 2); reads go WRONG while they are below T. Only 62h may be
    written as data, only with interrupts off, and every other bit of it
    must survive the write. The PB286LP's own values."""

    book = 4

    def __init__(self, T, mu, pico=False):
        self.T, self.mu, self.pico = T, mu, pico
        self.bad, self.log, self.ctr = [], [], 0
        self.regs = {0x60: 0x00, 0x61: 0x45, 0x62: 0x58, 0x64: 0x20}
        self.idx, self.fresh = None, False
        self.rom = bytes([0x55, 0xAA, 0x40]) + bytes((i * 37 + 11) & 0xFF
                                                     for i in range(509))
        self.scr = bytes((i * 13 + 5) & 0xFF for i in range(512))
        mu.mem_write(0xC0000, self.rom)
        mu.mem_write(0xA0000, self.scr)
        self.wrong = False
        self.r4 = self.ws()

    def ws(self):
        return ((self.regs[0x62] >> 2) & 3) + 2

    def apply(self):
        self.r4 = self.ws()
        wrong = self.r4 < self.T
        if wrong != self.wrong:
            self.wrong = wrong
            x = (lambda b: bytes(c ^ 0x5A for c in b)) if wrong else (lambda b: b)
            self.mu.mem_write(0xC0000 + 3, x(self.rom[3:]))
            self.mu.mem_write(0xA0000, x(self.scr))

    def rd(self, p):
        if p == 0x23:
            if not self.fresh:
                self.bad.append("23h read without a fresh 22h")
            self.fresh = False
            return self.regs.get(self.idx, 0xFF)
        return FEBoard.rd(self, p)

    def wr(self, p, v, iflag):
        if p == 0x85:
            self.page = v
            return
        self.log.append((p, v))
        if p == 0x22:
            if not 0x60 <= v <= 0x67:
                self.bad.append("22h index %02Xh" % v)
            self.idx, self.fresh = v, True
        elif p == 0x23:
            if iflag:
                self.bad.append("23h written with interrupts ON")
            if not self.fresh or self.idx != 0x62:
                self.bad.append("23h written at index %s" % self.idx)
            elif (v ^ self.regs[0x62]) & 0xF3:
                self.bad.append("62h's other bits changed: %02Xh -> %02Xh"
                                % (self.regs[0x62], v))
            else:
                self.regs[0x62] = v
                self.apply()
            self.fresh = False
        else:
            self.bad.append("wrote port %Xh" % p)


class Rig:
    def __init__(self, img, syms, board_cls, *a, tier=1, rom=None, **kw):
        self.syms = syms
        mu = self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        mu.mem_write(0x100, img)
        if rom is not None:
            mu.mem_write(0xF0000, rom)
        mu.mem_write(syms["h_tier"], bytes([tier]))
        for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            mu.reg_write(r, 0)
        self.b = (board_cls(*a, mu=mu, **kw) if board_cls in (FEBoard, NEBoard)
                  else board_cls())
        b = self.b

        def hin(uc, port, size, user):
            return b.rd(port)

        def hout(uc, port, size, value, user):
            if isinstance(b, FEBoard):     # and the NEAT, its subclass
                b.wr(port, value & 0xFF, uc.reg_read(UC_X86_REG_EFLAGS) & 0x200)
            else:
                b.wr(port, value & 0xFF)

        mu.hook_add(UC_HOOK_INSN, hin, None, 1, 0, UC_X86_INS_IN)
        mu.hook_add(UC_HOOK_INSN, hout, None, 1, 0, UC_X86_INS_OUT)

    def call(self, name, ax=0, watch_if=False):
        mu, s = self.mu, self.syms
        mu.reg_write(UC_X86_REG_SP, 0xFFF0)
        mu.mem_write(0xFFF0, s["h_ret"].to_bytes(2, "little"))
        mu.reg_write(UC_X86_REG_AX, ax)
        mu.reg_write(UC_X86_REG_EFLAGS, 0x202)
        h = None
        if watch_if:
            b, prev = self.b, [1]

            def code(uc, addr, size, user):
                f = uc.reg_read(UC_X86_REG_EFLAGS) & 0x200
                if f and not prev[0] and b.r4 < b.T:
                    b.bad.append("interrupts on at R4 %d, below %d" % (b.r4, b.T))
                prev[0] = f
            h = mu.hook_add(UC_HOOK_CODE, code)
        mu.emu_start(s[name], s["h_ret"], count=20_000_000)
        if h is not None:
            mu.hook_del(h)
        return (mu.reg_read(UC_X86_REG_AX),
                mu.reg_read(UC_X86_REG_EFLAGS) & 1)

    def rb(self, name, n=1):
        return bytes(self.mu.mem_read(self.syms[name], n))

    def wb(self, name, data):
        self.mu.mem_write(self.syms[name], bytes(data))


def write_side(bad):
    img, syms = build("tests/csprobe/feharness.asm")
    runs = 0
    # 1. one set: the sequence, exactly
    r = Rig(img, syms, FEBoard, 0)
    r.call("cs_fe_set", ax=8)
    runs += 1
    want = [(0x63, 0x04), (0x72, 0x40), (0x73, 0x80), (0x63, 0x84)]
    if r.b.log != want:
        bad("cs_fe_set(8) wrote %s, want %s" % (
            [(hex(p), hex(v)) for p, v in r.b.log], [(hex(p), hex(v)) for p, v in want]))
    for v in range(16):
        r = Rig(img, syms, FEBoard, 0)
        r.call("cs_fe_set", ax=v)
        if r.b.r4 != v or r.b.bad:
            bad("cs_fe_set(%d): R4 %s, %s" % (v, r.b.r4, r.b.bad))
    # 2. the sweep, for every threshold, with and without a PicoMEM
    for pico in (False, True):
        for T in range(2, 13):
            r = Rig(img, syms, FEBoard, T, pico=pico)
            ax, cf = r.call("h_sweep_pm" if pico else "h_sweep", watch_if=True)
            runs += 1
            fast = ax & 0xFF
            # home is the model's R4 12, which it runs; past the book's 10
            # nothing passes and the sweep goes HOME, not to the book's value
            want = min([c for c in (10, 8, 6, 4) if c >= T] or [12])
            fail = r.rb("cs_sw_fail")[0]
            wfail = 10 if want == 12 else (want - 2 if want > 4 else 0)
            tag = "sweep T=%d%s" % (T, " +PicoMEM" if pico else "")
            if fast != want or r.b.r4 != want:
                bad("%s: fastest %d, R4 left %d, want %d" % (tag, fast, r.b.r4, want))
            if fail != wfail:
                bad("%s: failed at %d, want %d" % (tag, fail, wfail))
            # a step that failed names what read wrong - in this model every
            # read goes wrong at once, so at least the screen (1) and the ROM
            # (16), and the PicoMEM's count (64) where there is one; a step
            # that passed names nothing
            log = r.rb("cs_sw_log", 6 * 12)
            want = 0x11 | (0x40 if pico else 0)
            for i in range(r.rb("cs_sw_n")[0]):
                v, w = log[i * 12], log[i * 12 + 1]
                if (v == fail) != bool(w) or (w and w & want != want):
                    bad("%s: step %d's why is %02Xh" % (tag, v, w))
            if r.b.bad:
                bad("%s: %s" % (tag, sorted(set(r.b.bad))[:4]))
    # 2b. THE I/O CHECKS ALONE: a board whose memory never reads wrong but
    # whose 3F4h, or whose 85h write, does - the sweep must stop on each
    for port in (0x3F4, 0x85):
        r = Rig(img, syms, FEBoard, 8)
        b = r.b
        orig_apply = b.apply

        def io_only(b=b):
            b.wrong = b.r4 < b.T    # the I/O goes wrong, the memory does not
        b.apply = io_only
        keep = b.io_rd

        def one(p, keep=keep, b=b, port=port):
            v = keep(p)
            if v is not None and p != port:
                return keep.__self__.fdc if p == 0x3F4 else b.page
            return v
        b.io_rd = one
        ax, cf = r.call("h_sweep", watch_if=True)
        runs += 1
        why = r.rb("cs_sw_log", 6 * 12)
        if ax & 0xFF != 8 or r.rb("cs_sw_fail")[0] != 6:
            bad("I/O check %Xh: fastest %d, failed at %d, want 8 and 6"
                % (port, ax & 0xFF, r.rb("cs_sw_fail")[0]))
        # the log names the check that failed, and only that one: steps 10,
        # 8 and 6, so R4 6's why is the third record's second byte
        wwhy = 2 if port == 0x3F4 else 4      # CSW_FDC, CSW_PAGE
        if why[0 * 12 + 1] or why[1 * 12 + 1] or why[2 * 12 + 1] != wwhy:
            bad("I/O check %Xh: the log's why is %s, want 0, 0, %d"
                % (port, [why[i * 12 + 1] for i in range(3)], wwhy))
        b.apply = orig_apply
    # 2c. THE NEAT: its set preserves 62h's other bits, reads back, and its
    # sweep stops at the first wrong read for every threshold
    for v in range(2, 6):
        r = Rig(img, syms, NEBoard, 0)
        r.call("cs_ne_set", ax=v)
        runs += 1
        if r.b.regs[0x62] != (0x58 & 0xF3) | ((v - 2) << 2) or r.b.bad:
            bad("cs_ne_set(%d): 62h = %02Xh, %s" % (v, r.b.regs[0x62], r.b.bad))
        ax, cf = r.call("cs_ne_get")
        if ax & 0xFF != v or (ax >> 8) != 1:
            bad("cs_ne_get after %d: %04Xh" % (v, ax))
    # home 4 (the PB286LP's BIOS) for thresholds 2-4; and a board whose BIOS
    # runs 5 (62h = 5Ch) that cannot run the book's 4 must go back to 5
    for T, home in ((2, 4), (3, 4), (4, 4), (5, 5)):
        r = Rig(img, syms, NEBoard, T)
        if home == 5:
            r.b.regs[0x62] = 0x5C
            r.b.apply()
        ax, cf = r.call("h_sweep_ne", watch_if=True)
        runs += 1
        fast = ax & 0xFF
        want = min([c for c in (4, 3, 2) if c >= T] or [home])
        tried = [c for c in (4, 3, 2)]
        wfail = next((c for c in tried if c < T), 0)
        tag = "NEAT sweep T=%d" % T
        if fast != want or r.b.ws() != want:
            bad("%s: fastest %d, waits left %d, want %d" % (tag, fast, r.b.ws(), want))
        if r.rb("cs_sw_fail")[0] != wfail:
            bad("%s: failed at %d, want %d" % (tag, r.rb("cs_sw_fail")[0], wfail))
        if r.b.bad:
            bad("%s: %s" % (tag, sorted(set(r.b.bad))[:4]))
    for ax_, what in (((6 << 8) | 1, "6 waits"), ((1 << 8) | 1, "1 wait"),
                      ((3 << 8) | 4, "knob 4")):
        r = Rig(img, syms, NEBoard, 0)
        ax, cf = r.call("cs_ap_ne", ax=ax_)
        runs += 1
        if not cf or r.b.log:
            bad("cs_ap_ne took %s: CF %d, wrote %s" % (what, cf, r.b.log))
    # 3. REDLINE.CFG
    rom = bytearray(os.urandom(0x10000))
    rom[0xFFF5:0xFFFD] = b"03/12/88"
    rom[0xFFFE] = 0xFC
    crc = crc16(rom)
    r = Rig(img, syms, Board, tier=1, rom=bytes(rom))
    r.call("cs_identify")
    r.call("cs_cfg_new", ax=(3 << 8) | 1)          # row F3, method 3
    r.call("cs_cfg_knob", ax=(8 << 8) | 4)         # R4 = 8
    ax, cf = r.call("cs_cfg_save")
    runs += 1
    wlen = int.from_bytes(r.rb("h_wlen", 2), "little")
    saved = r.rb("h_file", wlen)
    want = (b"RLCFG\0\0\0" + b"F3" + bytes([3, 1]) + crc.to_bytes(2, "little")
            + b"03/12/88" + bytes([0xFC, 0]) + bytes([4, 8]))
    if cf or saved != want:
        bad("Save wrote %r, want %r" % (saved, want))

    def apply(data, tier=1, expect=0, r4=None, what=""):
        nonlocal runs
        rr = Rig(img, syms, FEBoard, 0, tier=tier, rom=bytes(rom))
        rr.wb("h_tier", [tier])
        rr.call("cs_identify")         # its probes are the matrix's subject
        rr.b.log.clear()
        rr.b.bad.clear()
        rr.wb("h_file", data.ljust(64, b"\0"))
        rr.wb("h_flen", len(data).to_bytes(2, "little"))
        ax, cf = rr.call("cs_cfg_load")
        if not cf:
            ax, cf = rr.call("cs_cfg_apply")
        runs += 1
        got = ax & 0xFF if cf else 0
        if got != expect:
            bad("Apply %s: answer %d, want %d" % (what, got, expect))
        if expect and rr.b.log:
            bad("Apply %s: refused and still wrote %s" % (what, rr.b.log))
        if r4 is not None and rr.b.r4 != r4:
            bad("Apply %s: R4 %s, want %d" % (what, rr.b.r4, r4))
        if rr.b.bad:
            bad("Apply %s: %s" % (what, rr.b.bad))

    apply(want, r4=8, what="the saved file")
    apply(b"", expect=1, what="no file")
    apply(b"RLCFX" + want[5:], expect=2, what="a bad signature")
    apply(want + b"\4\6", expect=2, what="a length the count does not say")
    apply(want[:11] + b"\0" + want[12:24], expect=2, what="no knobs")
    for off, what in ((12, "the CRC"), (14, "the date"), (22, "the model byte")):
        m = bytearray(want)
        m[off] ^= 1
        apply(bytes(m), expect=3, what="another board (%s)" % what)
    apply(want, tier=0, expect=5, what="on an 8088")
    apply(want[:8] + b"SC" + want[10:], expect=4, what="a row with no apply")
    apply(want[:8] + b"ZZ" + want[10:], expect=4, what="an unknown row")
    apply(want[:24] + bytes([5, 8]), expect=6, what="a knob FE3600 has not got")
    apply(want[:24] + bytes([4, 3]), expect=6, what="R4 below the floor")

    # 4. R4 READ OFF THE BUS (SPEC.md 103.6.3.1): the Victory's own three
    # readings at 16 MHz must name the R4 each was taken at, every reading a
    # clock either side of them must too (the classes are two clocks apart),
    # an odd setting reads as the next even one, and "nothing measured" is a
    # refusal rather than a number
    r = Rig(img, syms, FEBoard, 0)

    def tenths(ns, mhz100=1600):
        return ns * mhz100 // 10000

    cases = [(915, 14), (651, 10), (523, 8)]    # as the Victory read them
    for r4 in (4, 6, 8, 10, 12, 14):            # each class's centre, and
        centre = (r4 * 10 + 4) * 625 // 100     # 0.9 of a clock either side
        cases += [(centre - 56, r4), (centre, r4), (centre + 56, r4)]
    for ns, r4 in cases:
        ax, cf = r.call("cs_fewaits", ax=tenths(ns))
        runs += 1
        if cf or (ax & 0xFF) != r4:
            bad("cs_fewaits(%d ns at 16 MHz) = %s, want R4 %d"
                % (ns, "refused" if cf else ax & 0xFF, r4))
    # ...and from the 8-bit I/O read (cs_fewaitsio): the Victory's floppy
    # status read at R4 14 and 8, and every class centre (R4 + 2.7 clocks)
    # 0.9 of a clock either side
    io = [(1060, 14), (668, 8)]
    for r4 in (4, 6, 8, 10, 12, 14):
        centre = (r4 * 10 + 27) * 625 // 100
        io += [(centre - 56, r4), (centre, r4), (centre + 56, r4)]
    for ns, r4 in io:
        ax, cf = r.call("cs_fewaitsio", ax=ns * 1596 // 10000)
        runs += 1
        if cf or (ax & 0xFF) != r4:
            bad("cs_fewaitsio(%d ns at 15.96 MHz) = %s, want R4 %d"
                % (ns, "refused" if cf else ax & 0xFF, r4))
    ax, cf = r.call("cs_fewaitsio", ax=0xFFFF)
    runs += 1
    if not cf:
        bad("cs_fewaitsio answered %d for no measurement" % (ax & 0xFF))
    ax, cf = r.call("cs_fewaits", ax=94 + 10)   # R4 9 runs as 10
    runs += 1
    if cf or (ax & 0xFF) % 2:
        bad("cs_fewaits gave an odd R4 (%d)" % (ax & 0xFF))
    ax, cf = r.call("cs_fewaits", ax=0xFFFF)
    runs += 1
    if not cf:
        bad("cs_fewaits answered %d for no measurement" % (ax & 0xFF))
    return runs


if __name__ == "__main__":
    sys.exit(main())

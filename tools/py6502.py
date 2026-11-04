#!/usr/bin/env python3
"""tools/py6502.py - an NMOS 6502 in Python, for the host side of FAST6502
(docs/plans/FAST6502-PLAN.md): the trace m65aot.py takes its ROM seeds from,
and an independent reference for the translator's gate.

    python3 tools/py6502.py --dormann build/a2tests/6502_functional_test.bin

runs Klaus Dormann's functional test to its success trap ($3469) and is this
file's own check. The 151 legal opcodes only (an illegal one raises Jam), the
decimal mode by the NMOS rules tools/c64dec.py documents, and the cycle costs
of apps/m6502/m65gen.py's table with the page-cross and branch penalties.

A machine is a CPU over a bus: `read(addr)` and `write(addr, value)`; the
default bus is 64KB of flat RAM. `trace` (a set) collects every PC an
instruction started at.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "apps", "m6502"))
from m65gen import OPS, PENALTY                       # noqa: E402

N, V, U, B, D, I, Z, C = 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01


class Jam(Exception):
    pass


class CPU:
    def __init__(self, mem=None):
        self.mem = mem if mem is not None else bytearray(65536)
        self.a = self.x = self.y = 0
        self.s = 0xFD
        self.p = U | I
        self.pc = 0
        self.cycles = 0
        self.trace = None
        self.dyn = None     # (with trace) the PCs RTS, JMP (ind), BRK and an
                            # interrupt landed on: m65aot.py's entries

    # --- the bus ---------------------------------------------------------
    def read(self, a):
        return self.mem[a & 0xFFFF]

    def write(self, a, v):
        self.mem[a & 0xFFFF] = v & 0xFF

    def rd16(self, a):
        return self.read(a) | (self.read((a + 1) & 0xFFFF) << 8)

    # --- helpers ---------------------------------------------------------
    def fetch(self):
        v = self.read(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        return v

    def nz(self, v):
        self.p = (self.p & ~(N | Z)) | (v & N) | (0 if v else Z)
        return v

    def push(self, v):
        self.write(0x100 | self.s, v)
        self.s = (self.s - 1) & 0xFF

    def pop(self):
        self.s = (self.s + 1) & 0xFF
        return self.read(0x100 | self.s)

    def ea(self, mode):
        """the effective address, and whether an index carried a page"""
        if mode == "zp":
            return self.fetch(), False
        if mode == "zpx":
            return (self.fetch() + self.x) & 0xFF, False
        if mode == "zpy":
            return (self.fetch() + self.y) & 0xFF, False
        if mode == "abs":
            lo = self.fetch()
            return lo | (self.fetch() << 8), False
        if mode in ("absx", "absy"):
            lo = self.fetch()
            base = lo | (self.fetch() << 8)
            a = (base + (self.x if mode == "absx" else self.y)) & 0xFFFF
            return a, (a ^ base) & 0xFF00 != 0
        if mode == "indx":
            z = (self.fetch() + self.x) & 0xFF
            return self.read(z) | (self.read((z + 1) & 0xFF) << 8), False
        if mode == "indy":
            z = self.fetch()
            base = self.read(z) | (self.read((z + 1) & 0xFF) << 8)
            a = (base + self.y) & 0xFFFF
            return a, (a ^ base) & 0xFF00 != 0
        raise ValueError(mode)

    def adc(self, m):
        c = self.p & C
        if self.p & D:
            # the NMOS rules (tools/c64dec.py): Z from the binary sum, N and V
            # from the high nibble before its adjustment, C after it
            binsum = self.a + m + c
            lo = (self.a & 0x0F) + (m & 0x0F) + c
            if lo > 9:
                lo += 6
            hi = (self.a >> 4) + (m >> 4) + (1 if lo > 0x0F else 0)
            z = (binsum & 0xFF) == 0
            n = (hi << 4) & 0x80
            v = (~(self.a ^ m) & (self.a ^ (hi << 4)) & 0x80) != 0
            if hi > 9:
                hi += 6
            cy = hi > 0x0F
            self.a = ((hi << 4) | (lo & 0x0F)) & 0xFF
            self.p = (self.p & ~(N | V | Z | C)) | n | (V if v else 0) | \
                (Z if z else 0) | (C if cy else 0)
            return
        r = self.a + m + c
        v = (~(self.a ^ m) & (self.a ^ r) & 0x80) != 0
        self.p = (self.p & ~(V | C)) | (V if v else 0) | (C if r > 0xFF else 0)
        self.a = self.nz(r & 0xFF)

    def sbc(self, m):
        c = self.p & C
        r = self.a - m - (1 - c)
        v = ((self.a ^ m) & (self.a ^ r) & 0x80) != 0
        flags = (self.p & ~(V | C | N | Z)) | (V if v else 0) | \
            (C if r >= 0 else 0) | (r & 0x80) | (0 if r & 0xFF else Z)
        if self.p & D:
            # every FLAG is the binary subtraction's; only A is adjusted
            lo = (self.a & 0x0F) - (m & 0x0F) - (1 - c)
            hi = (self.a >> 4) - (m >> 4)
            if lo < 0:
                lo -= 6
                hi -= 1
            if hi < 0:
                hi -= 6
            self.a = ((hi << 4) | (lo & 0x0F)) & 0xFF
        else:
            self.a = r & 0xFF
        self.p = flags

    def cmp(self, reg, m):
        r = reg - m
        self.p = (self.p & ~(N | Z | C)) | (r & 0x80) | \
            (0 if r & 0xFF else Z) | (C if r >= 0 else 0)

    def branch(self, cond):
        d = self.fetch()
        if not cond:
            return 0
        if d & 0x80:
            d -= 0x100
        t = (self.pc + d) & 0xFFFF
        extra = 2 if (t ^ self.pc) & 0xFF00 else 1
        self.pc = t
        return extra

    def rmw(self, mn, v):
        if mn == "ASL":
            self.p = (self.p & ~C) | (C if v & 0x80 else 0)
            v = (v << 1) & 0xFF
        elif mn == "LSR":
            self.p = (self.p & ~C) | (v & 1)
            v >>= 1
        elif mn == "ROL":
            c = self.p & C
            self.p = (self.p & ~C) | (C if v & 0x80 else 0)
            v = ((v << 1) | c) & 0xFF
        elif mn == "ROR":
            c = self.p & C
            self.p = (self.p & ~C) | (v & 1)
            v = (v >> 1) | (0x80 if c else 0)
        elif mn == "INC":
            v = (v + 1) & 0xFF
        elif mn == "DEC":
            v = (v - 1) & 0xFF
        return self.nz(v)

    # --- one instruction -------------------------------------------------
    def step(self):
        if self.trace is not None:
            self.trace.add(self.pc)
        at = self.pc
        op = self.fetch()
        if op not in OPS:
            self.pc = at
            raise Jam("opcode $%02X at $%04X" % (op, at))
        mn, mode, cyc = OPS[op]
        extra = 0
        if mn in ("LDA", "LDX", "LDY", "ORA", "AND", "EOR", "ADC", "SBC",
                  "CMP", "CPX", "CPY", "BIT"):
            if mode == "imm":
                m = self.fetch()
            else:
                a, crossed = self.ea(mode)
                if crossed and mode in PENALTY:
                    extra = 1
                m = self.read(a)
            if mn == "LDA":
                self.a = self.nz(m)
            elif mn == "LDX":
                self.x = self.nz(m)
            elif mn == "LDY":
                self.y = self.nz(m)
            elif mn == "ORA":
                self.a = self.nz(self.a | m)
            elif mn == "AND":
                self.a = self.nz(self.a & m)
            elif mn == "EOR":
                self.a = self.nz(self.a ^ m)
            elif mn == "ADC":
                self.adc(m)
            elif mn == "SBC":
                self.sbc(m)
            elif mn == "CMP":
                self.cmp(self.a, m)
            elif mn == "CPX":
                self.cmp(self.x, m)
            elif mn == "CPY":
                self.cmp(self.y, m)
            elif mn == "BIT":
                self.p = (self.p & ~(N | V | Z)) | (m & (N | V)) | \
                    (0 if self.a & m else Z)
        elif mn in ("STA", "STX", "STY"):
            a, _ = self.ea(mode)
            self.write(a, {"STA": self.a, "STX": self.x, "STY": self.y}[mn])
        elif mn in ("ASL", "LSR", "ROL", "ROR", "INC", "DEC"):
            if mode == "acc":
                self.a = self.rmw(mn, self.a)
            else:
                a, _ = self.ea(mode)
                self.write(a, self.rmw(mn, self.read(a)))
        elif mode == "rel":
            f = self.p
            cond = {"BPL": not f & N, "BMI": f & N, "BVC": not f & V,
                    "BVS": f & V, "BCC": not f & C, "BCS": f & C,
                    "BNE": not f & Z, "BEQ": f & Z}[mn]
            extra = self.branch(cond)
        elif mn == "JMP":
            if mode == "abs":
                self.pc = self.fetch() | (self.fetch() << 8)
            else:
                ptr = self.fetch() | (self.fetch() << 8)
                hi = (ptr & 0xFF00) | ((ptr + 1) & 0xFF)     # the page wrap
                self.pc = self.read(ptr) | (self.read(hi) << 8)
                if self.dyn is not None:
                    self.dyn.add(self.pc)
        elif mn == "JSR":
            lo = self.fetch()
            ret = self.pc                       # on the high byte: PC + 2 - 1
            self.push(ret >> 8)
            self.push(ret & 0xFF)
            self.pc = lo | (self.read(ret) << 8)
        elif mn == "RTS":
            lo = self.pop()
            self.pc = ((lo | (self.pop() << 8)) + 1) & 0xFFFF
            if self.dyn is not None:
                self.dyn.add(self.pc)
        elif mn == "RTI":
            self.p = (self.pop() & ~B) | U
            lo = self.pop()
            self.pc = lo | (self.pop() << 8)
            # (NOT an entry: an RTI from an interrupt lands wherever it struck,
            # and marking those would cut every straight run of ROM into
            # pieces - the core takes such a return to the next jump)
        elif mn == "BRK":
            self.pc = (self.pc + 1) & 0xFFFF
            self.push(self.pc >> 8)
            self.push(self.pc & 0xFF)
            self.push(self.p | B | U)
            self.p |= I
            self.pc = self.rd16(0xFFFE)
            if self.dyn is not None:
                self.dyn.add(self.pc)
        elif mn == "PHA":
            self.push(self.a)
        elif mn == "PHP":
            self.push(self.p | B | U)
        elif mn == "PLA":
            self.a = self.nz(self.pop())
        elif mn == "PLP":
            self.p = (self.pop() & ~B) | U
        elif mn in ("TAX", "TAY", "TXA", "TYA", "TSX", "TXS"):
            if mn == "TAX":
                self.x = self.nz(self.a)
            elif mn == "TAY":
                self.y = self.nz(self.a)
            elif mn == "TXA":
                self.a = self.nz(self.x)
            elif mn == "TYA":
                self.a = self.nz(self.y)
            elif mn == "TSX":
                self.x = self.nz(self.s)
            else:
                self.s = self.x
        elif mn in ("INX", "INY", "DEX", "DEY"):
            if mn[2] == "X":
                self.x = self.nz((self.x + (1 if mn[0] == "I" else -1)) & 0xFF)
            else:
                self.y = self.nz((self.y + (1 if mn[0] == "I" else -1)) & 0xFF)
        elif mn in ("CLC", "SEC", "CLI", "SEI", "CLV", "CLD", "SED"):
            bit = {"C": C, "I": I, "V": V, "D": D}[mn[2]]
            self.p = (self.p | bit) if mn[0] == "S" else (self.p & ~bit)
        elif mn == "NOP":
            pass
        else:
            raise ValueError(mn)
        self.cycles += cyc + extra
        return cyc + extra


def dormann(path, trap=0x3469, limit=120_000_000, trace=False):
    cpu = CPU(bytearray(open(path, "rb").read()))
    if trace:
        cpu.trace = set()
        cpu.dyn = set()
    cpu.pc = 0x0400
    last, same = -1, 0
    n = 0
    while n < limit:
        pc = cpu.pc
        cpu.step()
        n += 1
        if cpu.pc == pc:
            break
    return cpu, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dormann")
    ap.add_argument("--trace-out", help="write the PCs in --trace-range Dormann ran")
    ap.add_argument("--trace-range", default="0400:4000")
    a = ap.parse_args()
    if a.dormann:
        cpu, n = dormann(a.dormann, trace=bool(a.trace_out))
        if a.trace_out:
            lo, hi = (int(v, 16) for v in a.trace_range.split(":"))
            with open(a.trace_out, "w") as f:
                f.write("# Dormann's executed PCs - tools/py6502.py\n")
                for pc in sorted(p for p in cpu.trace if lo <= p < hi):
                    f.write("%04X%s\n" % (pc, " D" if pc in cpu.dyn else ""))
        print("py6502: Dormann settled at $%04X after %d instructions, %d cycles"
              % (cpu.pc, n, cpu.cycles))
        return 0 if cpu.pc == 0x3469 else 1


if __name__ == "__main__":
    sys.exit(main())

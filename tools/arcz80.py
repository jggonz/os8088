#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/arcz80.py - a Zilog Z80 in Python, for ARCADE's reference
# models (docs/ARCADE-SPEC.md section 7)
#
# The CPU only: a board supplies rd(addr), wr(addr, v), io_in(port) and
# io_out(port, v), and calls irq(vector_byte) / step(). Every documented
# instruction and flag, and the undocumented ones the arcade boards use
# (SLL, the IX/IY half registers); X and Y (bits 3 and 5 of F) are kept as
# the real chip leaves them on the common paths but nothing here compares
# them - ARCADE's translation does not keep them (ARCADE-SPEC 7.3).
#
# T-states per instruction are the documented counts, so a board can time its
# interrupts the way the hardware does.
# =============================================================================

SF, ZF, YF, HF, XF, PF, NF, CF = 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01

PARITY = [0] * 256
for _i in range(256):
    PARITY[_i] = PF if bin(_i).count('1') % 2 == 0 else 0
SZP = [0] * 256
for _i in range(256):
    SZP[_i] = (_i & (SF | YF | XF)) | (ZF if _i == 0 else 0) | PARITY[_i]

# main-page T-states (conditional ones: the not-taken count; taken adds)
T_MAIN = [
    4, 10, 7, 6, 4, 4, 7, 4, 4, 11, 7, 6, 4, 4, 7, 4,
    8, 10, 7, 6, 4, 4, 7, 4, 12, 11, 7, 6, 4, 4, 7, 4,
    7, 10, 16, 6, 4, 4, 7, 4, 7, 11, 16, 6, 4, 4, 7, 4,
    7, 10, 13, 6, 11, 11, 10, 4, 7, 11, 13, 6, 4, 4, 7, 4,
] + [4] * 64 + [4] * 64 + [
    5, 10, 10, 10, 10, 11, 7, 11, 5, 10, 10, 0, 10, 17, 7, 11,
    5, 10, 10, 11, 10, 11, 7, 11, 5, 4, 10, 11, 10, 0, 7, 11,
    5, 10, 10, 19, 10, 11, 7, 11, 5, 4, 10, 4, 10, 0, 7, 11,
    5, 10, 10, 4, 10, 11, 7, 11, 5, 6, 10, 4, 10, 0, 7, 11,
]
for _o in range(0x40, 0x80):
    T_MAIN[_o] = 7 if (_o & 7) == 6 or ((_o >> 3) & 7) == 6 else 4
T_MAIN[0x76] = 4
for _o in range(0x80, 0xC0):
    T_MAIN[_o] = 7 if (_o & 7) == 6 else 4


class Z80:
    def __init__(self, board):
        self.b = board
        self.a = self.f = self.b_ = self.c = self.d = self.e = self.h = self.l = 0
        self.a2 = self.f2 = self.b2 = self.c2 = self.d2 = self.e2 = self.h2 = self.l2 = 0
        self.ix = self.iy = 0xFFFF
        self.sp = 0xFFFF
        self.pc = 0
        self.i = self.r = 0
        self.iff1 = self.iff2 = 0
        self.im = 0
        self.halted = False
        self.ei_delay = False
        self.cyc = 0
        self.q = 0                  # (unused: the X/Y of SCF/CCF on NMOS)

    # --- the register file by Z80 encoding ---------------------------------
    def r8(self, n, hl=None):
        if n == 0: return self.b_
        if n == 1: return self.c
        if n == 2: return self.d
        if n == 3: return self.e
        if n == 4: return (hl >> 8) if hl is not None else self.h
        if n == 5: return (hl & 255) if hl is not None else self.l
        if n == 7: return self.a
        raise ValueError(n)

    def w8(self, n, v, ix=None):
        v &= 255
        if n == 0: self.b_ = v
        elif n == 1: self.c = v
        elif n == 2: self.d = v
        elif n == 3: self.e = v
        elif n == 4:
            if ix == 'ix': self.ix = (self.ix & 0xFF) | (v << 8)
            elif ix == 'iy': self.iy = (self.iy & 0xFF) | (v << 8)
            else: self.h = v
        elif n == 5:
            if ix == 'ix': self.ix = (self.ix & 0xFF00) | v
            elif ix == 'iy': self.iy = (self.iy & 0xFF00) | v
            else: self.l = v
        elif n == 7: self.a = v

    @property
    def bc(self): return (self.b_ << 8) | self.c
    @property
    def de(self): return (self.d << 8) | self.e
    @property
    def hl(self): return (self.h << 8) | self.l

    def set_bc(self, v): self.b_, self.c = (v >> 8) & 255, v & 255
    def set_de(self, v): self.d, self.e = (v >> 8) & 255, v & 255
    def set_hl(self, v): self.h, self.l = (v >> 8) & 255, v & 255

    def rp(self, n, xy=None):
        if n == 0: return self.bc
        if n == 1: return self.de
        if n == 2: return self.ix if xy == 'ix' else self.iy if xy == 'iy' else self.hl
        return self.sp

    def set_rp(self, n, v, xy=None):
        v &= 0xFFFF
        if n == 0: self.set_bc(v)
        elif n == 1: self.set_de(v)
        elif n == 2:
            if xy == 'ix': self.ix = v
            elif xy == 'iy': self.iy = v
            else: self.set_hl(v)
        else: self.sp = v

    # --- memory ------------------------------------------------------------
    def rd(self, a): return self.b.rd(a & 0xFFFF)
    def wr(self, a, v): self.b.wr(a & 0xFFFF, v & 255)
    def rd16(self, a): return self.rd(a) | (self.rd(a + 1) << 8)
    def wr16(self, a, v): self.wr(a, v & 255); self.wr(a + 1, (v >> 8) & 255)

    def fetch(self):
        v = self.rd(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        return v

    def fetch16(self):
        v = self.rd16(self.pc)
        self.pc = (self.pc + 2) & 0xFFFF
        return v

    def disp(self):
        d = self.fetch()
        return d - 256 if d & 128 else d

    def push(self, v):
        self.sp = (self.sp - 2) & 0xFFFF
        self.wr16(self.sp, v)

    def pop(self):
        v = self.rd16(self.sp)
        self.sp = (self.sp + 2) & 0xFFFF
        return v

    # --- the ALU -------------------------------------------------------------
    def add8(self, v, cy=0):
        a = self.a
        r = a + v + cy
        f = SZP[r & 255] & ~PF
        f |= HF if ((a & 15) + (v & 15) + cy) > 15 else 0
        f |= PF if ((a ^ ~v) & (a ^ r) & 0x80) else 0
        f |= CF if r > 255 else 0
        self.a = r & 255
        self.f = f

    def sub8(self, v, cy=0, store=True):
        a = self.a
        r = a - v - cy
        f = (SZP[r & 255] & ~PF) | NF
        f |= HF if ((a & 15) - (v & 15) - cy) < 0 else 0
        f |= PF if ((a ^ v) & (a ^ r) & 0x80) else 0
        f |= CF if r < 0 else 0
        if store:
            self.a = r & 255
        else:
            f = (f & ~(YF | XF)) | (v & (YF | XF))
        self.f = f

    def alu(self, op, v):
        if op == 0: self.add8(v)
        elif op == 1: self.add8(v, self.f & CF)
        elif op == 2: self.sub8(v)
        elif op == 3: self.sub8(v, self.f & CF)
        elif op == 4: self.a &= v; self.f = SZP[self.a] | HF
        elif op == 5: self.a ^= v; self.f = SZP[self.a]
        elif op == 6: self.a |= v; self.f = SZP[self.a]
        else: self.sub8(v, 0, False)

    def inc8(self, v):
        r = (v + 1) & 255
        self.f = (self.f & CF) | (SZP[r] & ~PF) | (HF if (v & 15) == 15 else 0) | \
            (PF if v == 0x7F else 0)
        return r

    def dec8(self, v):
        r = (v - 1) & 255
        self.f = (self.f & CF) | (SZP[r] & ~PF) | NF | (HF if (v & 15) == 0 else 0) | \
            (PF if v == 0x80 else 0)
        return r

    def add16(self, a, v):
        r = a + v
        self.f = (self.f & (SF | ZF | PF)) | ((r >> 8) & (YF | XF)) | \
            (HF if ((a & 0xFFF) + (v & 0xFFF)) > 0xFFF else 0) | (CF if r > 0xFFFF else 0)
        return r & 0xFFFF

    def adc16(self, v):
        a = self.hl
        cy = self.f & CF
        r = a + v + cy
        f = (SF if r & 0x8000 else 0) | (ZF if (r & 0xFFFF) == 0 else 0) | \
            ((r >> 8) & (YF | XF)) | \
            (HF if ((a & 0xFFF) + (v & 0xFFF) + cy) > 0xFFF else 0) | \
            (PF if ((a ^ ~v) & (a ^ r) & 0x8000) else 0) | (CF if r > 0xFFFF else 0)
        self.f = f
        self.set_hl(r)

    def sbc16(self, v):
        a = self.hl
        cy = self.f & CF
        r = a - v - cy
        f = (SF if r & 0x8000 else 0) | (ZF if (r & 0xFFFF) == 0 else 0) | NF | \
            ((r >> 8) & (YF | XF)) | \
            (HF if ((a & 0xFFF) - (v & 0xFFF) - cy) < 0 else 0) | \
            (PF if ((a ^ v) & (a ^ r) & 0x8000) else 0) | (CF if r < 0 else 0)
        self.f = f
        self.set_hl(r)

    def rot(self, op, v):
        c = self.f & CF
        if op == 0: cy = v >> 7; r = ((v << 1) | cy) & 255          # RLC
        elif op == 1: cy = v & 1; r = (v >> 1) | (cy << 7)          # RRC
        elif op == 2: cy = v >> 7; r = ((v << 1) | c) & 255          # RL
        elif op == 3: cy = v & 1; r = (v >> 1) | (c << 7)            # RR
        elif op == 4: cy = v >> 7; r = (v << 1) & 255                # SLA
        elif op == 5: cy = v & 1; r = (v >> 1) | (v & 0x80)          # SRA
        elif op == 6: cy = v >> 7; r = ((v << 1) | 1) & 255          # SLL
        else: cy = v & 1; r = v >> 1                                 # SRL
        self.f = SZP[r] | cy
        return r

    def daa(self):
        a = self.a
        f = self.f
        corr = 0
        cy = f & CF
        if (f & HF) or (a & 15) > 9:
            corr |= 6
        if cy or a > 0x99:
            corr |= 0x60
            cy = CF
        if f & NF:
            h = HF if (f & HF) and (a & 15) < 6 else 0
            r = (a - corr) & 255
        else:
            h = HF if (a & 15) > 9 else 0
            r = (a + corr) & 255
        self.a = r
        self.f = SZP[r] | h | (f & NF) | cy

    def cond(self, n):
        f = self.f
        return (not f & ZF, bool(f & ZF), not f & CF, bool(f & CF),
                not f & PF, bool(f & PF), not f & SF, bool(f & SF))[n]

    # --- interrupts ----------------------------------------------------------
    def irq(self, vec):
        """Take a maskable interrupt if IFF1 and not straight after EI.
        Returns the T-states it cost, 0 if it was not taken."""
        if not self.iff1 or self.ei_delay:
            return 0
        if self.halted:
            self.halted = False
            self.pc = (self.pc + 1) & 0xFFFF
        self.iff1 = self.iff2 = 0
        self.r = (self.r & 0x80) | ((self.r + 1) & 0x7F)
        self.push(self.pc)
        if self.im == 2:
            self.pc = self.rd16((self.i << 8) | (vec & 0xFE))
            return 19
        if self.im == 1:
            self.pc = 0x38
            return 13
        self.pc = vec & 0x38
        return 13

    # --- one instruction -----------------------------------------------------
    def step(self):
        """Run one instruction; return its T-states."""
        self.ei_delay = False
        if self.halted:
            return 4
        self.r = (self.r & 0x80) | ((self.r + 1) & 0x7F)
        op = self.fetch()
        if op == 0xCB:
            return self.cb(None, 0)
        if op == 0xED:
            return self.ed()
        if op in (0xDD, 0xFD):
            return self.xy('ix' if op == 0xDD else 'iy')
        return self.main(op, None)

    def main(self, op, xy):
        """The unprefixed page; xy = 'ix'/'iy' under a DD/FD prefix."""
        t = T_MAIN[op]
        if 0x40 <= op < 0x80:
            if op == 0x76:
                self.halted = True
                self.pc = (self.pc - 1) & 0xFFFF
                return 4
            d, s = (op >> 3) & 7, op & 7
            if d == 6 or s == 6:
                addr = self.addr_hl(xy)
                if d == 6:
                    self.wr(addr, self.r8(s))         # H, L stay H, L
                else:
                    self.w8(d, self.rd(addr))
                return t + (8 if xy else 0)
            v = self.r8(s, self.rp(2, xy) if xy else None)
            self.w8(d, v, xy)
            return t
        if 0x80 <= op < 0xC0:
            s = op & 7
            if s == 6:
                v = self.rd(self.addr_hl(xy))
                t += 8 if xy else 0
            else:
                v = self.r8(s, self.rp(2, xy) if xy else None)
            self.alu((op >> 3) & 7, v)
            return t
        x = op
        if x == 0x00: return t
        if x == 0x08:
            self.a, self.a2 = self.a2, self.a
            self.f, self.f2 = self.f2, self.f
            return t
        if x == 0x10:
            d = self.disp()
            self.b_ = (self.b_ - 1) & 255
            if self.b_:
                self.pc = (self.pc + d) & 0xFFFF
                return 13
            return 8
        if x == 0x18:
            d = self.disp()
            self.pc = (self.pc + d) & 0xFFFF
            return t
        if x in (0x20, 0x28, 0x30, 0x38):
            d = self.disp()
            if self.cond((x >> 3) & 3):
                self.pc = (self.pc + d) & 0xFFFF
                return 12
            return 7
        if x & 0xCF == 0x01:
            self.set_rp(x >> 4, self.fetch16(), xy)
            return t + (4 if xy else 0)
        if x & 0xCF == 0x09:
            p = x >> 4
            self.set_rp(2, self.add16(self.rp(2, xy), self.rp(p, xy)), xy)
            return t + (4 if xy else 0)
        if x == 0x02: self.wr(self.bc, self.a); return t
        if x == 0x12: self.wr(self.de, self.a); return t
        if x == 0x0A: self.a = self.rd(self.bc); return t
        if x == 0x1A: self.a = self.rd(self.de); return t
        if x == 0x22: self.wr16(self.fetch16(), self.rp(2, xy)); return t + (4 if xy else 0)
        if x == 0x2A: self.set_rp(2, self.rd16(self.fetch16()), xy); return t + (4 if xy else 0)
        if x == 0x32: self.wr(self.fetch16(), self.a); return t
        if x == 0x3A: self.a = self.rd(self.fetch16()); return t
        if x & 0xCF == 0x03:
            p = x >> 4
            self.set_rp(p, self.rp(p, xy) + 1, xy)
            return t + (4 if xy else 0)
        if x & 0xCF == 0x0B:
            p = x >> 4
            self.set_rp(p, self.rp(p, xy) - 1, xy)
            return t + (4 if xy else 0)
        if x & 0xC7 in (0x04, 0x05):
            r = (x >> 3) & 7
            fn = self.inc8 if (x & 7) == 4 else self.dec8
            if r == 6:
                addr = self.addr_hl(xy)
                self.wr(addr, fn(self.rd(addr)))
                return 11 + (12 if xy else 0)
            self.w8(r, fn(self.r8(r, self.rp(2, xy) if xy else None)), xy)
            return t
        if x & 0xC7 == 0x06:
            r = (x >> 3) & 7
            if r == 6:
                addr = self.addr_hl(xy)
                self.wr(addr, self.fetch())
                return 10 + (9 if xy else 0)
            self.w8(r, self.fetch(), xy)
            return t
        if x == 0x07:
            cy = self.a >> 7
            self.a = ((self.a << 1) | cy) & 255
            self.f = (self.f & (SF | ZF | PF)) | (self.a & (YF | XF)) | cy
            return t
        if x == 0x0F:
            cy = self.a & 1
            self.a = (self.a >> 1) | (cy << 7)
            self.f = (self.f & (SF | ZF | PF)) | (self.a & (YF | XF)) | cy
            return t
        if x == 0x17:
            cy = self.a >> 7
            self.a = ((self.a << 1) | (self.f & CF)) & 255
            self.f = (self.f & (SF | ZF | PF)) | (self.a & (YF | XF)) | cy
            return t
        if x == 0x1F:
            cy = self.a & 1
            self.a = (self.a >> 1) | ((self.f & CF) << 7)
            self.f = (self.f & (SF | ZF | PF)) | (self.a & (YF | XF)) | cy
            return t
        if x == 0x27: self.daa(); return t
        if x == 0x2F:
            self.a ^= 255
            self.f = (self.f & (SF | ZF | PF | CF)) | HF | NF | (self.a & (YF | XF))
            return t
        if x == 0x37:
            self.f = (self.f & (SF | ZF | PF)) | CF | (self.a & (YF | XF))
            return t
        if x == 0x3F:
            c = self.f & CF
            self.f = (self.f & (SF | ZF | PF)) | (HF if c else 0) | (0 if c else CF) | \
                (self.a & (YF | XF))
            return t
        if x & 0xC7 == 0xC0:
            if self.cond((x >> 3) & 7):
                self.pc = self.pop()
                return 11
            return 5
        if x & 0xCF == 0xC1:
            p = (x >> 4) & 3
            v = self.pop()
            if p == 3:
                self.a, self.f = v >> 8, v & 255
            else:
                self.set_rp(p, v, xy)
            return t + (4 if xy else 0)
        if x & 0xCF == 0xC5:
            p = (x >> 4) & 3
            self.push((self.a << 8) | self.f if p == 3 else self.rp(p, xy))
            return t + (4 if xy else 0)
        if x & 0xC7 == 0xC2:
            a = self.fetch16()
            if self.cond((x >> 3) & 7):
                self.pc = a
            return t
        if x == 0xC3: self.pc = self.fetch16(); return t
        if x & 0xC7 == 0xC4:
            a = self.fetch16()
            if self.cond((x >> 3) & 7):
                self.push(self.pc)
                self.pc = a
                return 17
            return 10
        if x == 0xCD:
            a = self.fetch16()
            self.push(self.pc)
            self.pc = a
            return t
        if x & 0xC7 == 0xC6:
            self.alu((x >> 3) & 7, self.fetch())
            return t
        if x & 0xC7 == 0xC7:
            self.push(self.pc)
            self.pc = x & 0x38
            return t
        if x == 0xC9: self.pc = self.pop(); return t
        if x == 0xD9:
            self.b_, self.b2 = self.b2, self.b_
            self.c, self.c2 = self.c2, self.c
            self.d, self.d2 = self.d2, self.d
            self.e, self.e2 = self.e2, self.e
            self.h, self.h2 = self.h2, self.h
            self.l, self.l2 = self.l2, self.l
            return t
        if x == 0xD3: self.b.io_out(self.fetch(), self.a); return t
        if x == 0xDB: self.a = self.b.io_in(self.fetch()); return t
        if x == 0xE3:
            v = self.rd16(self.sp)
            self.wr16(self.sp, self.rp(2, xy))
            self.set_rp(2, v, xy)
            return t + (4 if xy else 0)
        if x == 0xE9: self.pc = self.rp(2, xy); return t + (4 if xy else 0)
        if x == 0xEB:
            de, hl = self.de, self.hl
            self.set_de(hl)
            self.set_hl(de)
            return t
        if x == 0xF3: self.iff1 = self.iff2 = 0; return t
        if x == 0xFB:
            self.iff1 = self.iff2 = 1
            self.ei_delay = True
            return t
        if x == 0xF9: self.sp = self.rp(2, xy); return t + (2 if xy else 0)
        raise Exception('Z80: %02X at %04X' % (op, (self.pc - 1) & 0xFFFF))

    def addr_hl(self, xy):
        if xy:
            return (self.rp(2, xy) + self.disp()) & 0xFFFF
        return self.hl

    def xy(self, xy):
        self.r = (self.r & 0x80) | ((self.r + 1) & 0x7F)
        op = self.fetch()
        if op == 0xCB:
            d = self.disp()
            return self.cb(xy, d)
        if op in (0xDD, 0xFD, 0xED):
            self.pc = (self.pc - 1) & 0xFFFF       # the prefix is a NOP
            return 4
        # H and L under the prefix are IXH/IXL, except beside (IX+d)
        if 0x40 <= op < 0x80 and op != 0x76 and ((op & 7) == 6 or ((op >> 3) & 7) == 6):
            return self.main(op, xy)
        return self.main(op, xy) + 4

    def cb(self, xy, d):
        op = self.fetch()
        r = op & 7
        if xy:
            addr = (self.rp(2, xy) + d) & 0xFFFF
            v = self.rd(addr)
        elif r == 6:
            addr = self.hl
            v = self.rd(addr)
        else:
            v = self.r8(r)
        g = op >> 6
        n = (op >> 3) & 7
        if g == 0:
            v = self.rot(n, v)
        elif g == 1:
            bit = v & (1 << n)
            self.f = (self.f & CF) | HF | (0 if bit else (ZF | PF)) | \
                (SF if n == 7 and bit else 0) | (v & (YF | XF))
            return 20 if xy else 12 if r == 6 else 8
        elif g == 2:
            v &= ~(1 << n)
        else:
            v |= 1 << n
        if xy or r == 6:
            self.wr(addr, v)
            if xy and r != 6:
                self.w8(r, v)
            return 23 if xy else 15
        self.w8(r, v)
        return 8

    def ed(self):
        self.r = (self.r & 0x80) | ((self.r + 1) & 0x7F)
        op = self.fetch()
        if op & 0xC7 == 0x40:
            v = self.b.io_in(self.c)
            if op != 0x70:
                self.w8((op >> 3) & 7, v)
            self.f = (self.f & CF) | SZP[v]
            return 12
        if op & 0xC7 == 0x41:
            n = (op >> 3) & 7
            self.b.io_out(self.c, 0 if n == 6 else self.r8(n))
            return 12
        if op & 0xCF == 0x42: self.sbc16(self.rp(op >> 4 & 3)); return 15
        if op & 0xCF == 0x4A: self.adc16(self.rp(op >> 4 & 3)); return 15
        if op & 0xCF == 0x43: self.wr16(self.fetch16(), self.rp(op >> 4 & 3)); return 20
        if op & 0xCF == 0x4B: self.set_rp(op >> 4 & 3, self.rd16(self.fetch16())); return 20
        if op & 0xC7 == 0x44:
            v = self.a
            self.a = 0
            self.sub8(v)
            return 8
        if op & 0xC7 == 0x45:               # RETN / RETI
            self.iff1 = self.iff2
            self.pc = self.pop()
            return 14
        if op & 0xC7 == 0x46:
            self.im = (0, 0, 1, 2)[(op >> 3) & 3]
            return 8
        if op == 0x47: self.i = self.a; return 9
        if op == 0x4F: self.r = self.a; return 9
        if op == 0x57:
            self.a = self.i
            self.f = (self.f & CF) | (SZP[self.a] & ~PF) | (PF if self.iff2 else 0)
            return 9
        if op == 0x5F:
            self.a = self.r
            self.f = (self.f & CF) | (SZP[self.a] & ~PF) | (PF if self.iff2 else 0)
            return 9
        if op in (0x67, 0x6F):              # RRD / RLD
            m = self.rd(self.hl)
            if op == 0x67:
                nm = ((self.a & 15) << 4) | (m >> 4)
                self.a = (self.a & 0xF0) | (m & 15)
            else:
                nm = ((m << 4) | (self.a & 15)) & 255
                self.a = (self.a & 0xF0) | (m >> 4)
            self.wr(self.hl, nm)
            self.f = (self.f & CF) | SZP[self.a]
            return 18
        if op in (0xA0, 0xA8, 0xB0, 0xB8):  # LDI LDD LDIR LDDR
            step = 1 if op in (0xA0, 0xB0) else -1
            v = self.rd(self.hl)
            self.wr(self.de, v)
            self.set_hl(self.hl + step)
            self.set_de(self.de + step)
            self.set_bc(self.bc - 1)
            n = (v + self.a) & 255
            self.f = (self.f & (SF | ZF | CF)) | (PF if self.bc else 0) | \
                (n & XF) | ((n << 4) & YF)
            if op >= 0xB0 and self.bc:
                self.pc = (self.pc - 2) & 0xFFFF
                return 21
            return 16
        if op in (0xA1, 0xA9, 0xB1, 0xB9):  # CPI CPD CPIR CPDR
            step = 1 if op in (0xA1, 0xB1) else -1
            v = self.rd(self.hl)
            r = (self.a - v) & 255
            h = HF if (self.a & 15) < (v & 15) else 0
            self.set_hl(self.hl + step)
            self.set_bc(self.bc - 1)
            self.f = (self.f & CF) | (SZP[r] & (SF | ZF)) | h | NF | \
                (PF if self.bc else 0)
            if op >= 0xB0 and self.bc and r:
                self.pc = (self.pc - 2) & 0xFFFF
                return 21
            return 16
        if op in (0xA2, 0xAA, 0xB2, 0xBA, 0xA3, 0xAB, 0xB3, 0xBB):
            step = 1 if op & 8 == 0 else -1
            if op & 1:
                self.b.io_out(self.c, self.rd(self.hl))
            else:
                self.wr(self.hl, self.b.io_in(self.c))
            self.set_hl(self.hl + step)
            self.b_ = (self.b_ - 1) & 255
            self.f = (self.f & CF) | (ZF if self.b_ == 0 else 0) | NF
            if op >= 0xB0 and self.b_:
                self.pc = (self.pc - 2) & 0xFFFF
                return 21
            return 16
        return 8                             # the rest are NOPs (ED NOP)

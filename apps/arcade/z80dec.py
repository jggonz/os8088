#!/usr/bin/env python3
# =============================================================================
# os8088 - apps/arcade/z80dec.py - Z80 instruction decoding for the translator
# (docs/ARCADE-SPEC.md section 7.2)
#
# decode(rom, pc) -> Op: the instruction's length, its prefix, its opcode,
# its operands, its control-flow successors and which flag groups it reads
# and sets. No translation here: apps/arcade/arcz80t.py turns an Op into 8086.
# =============================================================================

UNKNOWN = -1

# Flag groups the liveness pass tracks (ARCADE-SPEC 7.3). P is P/V; N and H
# are read only by DAA (and PUSH AF, which reads everything).
C, Z, P, H, N = 1, 2, 4, 8, 16
ALL = C | Z | P | H | N
CCUSE = [Z, Z, C, C, P, P, Z, Z]      # NZ Z NC C PO PE P M


class Op:
    __slots__ = ('pc', 'n', 'pfx', 'op', 'imm', 'd', 'succ', 'use', 'kill',
                 'pv', 'nval', 'call', 'lin', 'lout', 'poll', 'weight', 'nin')

    def __repr__(self):
        return '<%04X %s%02X n%d>' % (self.pc, self.pfx or '', self.op, self.n)


def _s8(v):
    return v - 256 if v & 128 else v


def decode(rom, pc):
    o = Op()
    o.pc = pc
    o.pfx = None
    o.d = None
    o.imm = None
    o.use = o.kill = 0
    o.pv = None          # what P/V means when this sets it: 'par', 'ovf', 'bc', 'iff'
    o.nval = None        # N after it: 0, 1 or None (untouched)
    o.call = False
    o.poll = False
    o.weight = 1
    o.lin = o.lout = 0
    o.nin = None
    b0 = rom[pc]
    at = pc
    if b0 in (0xDD, 0xFD):
        o.pfx = 'ix' if b0 == 0xDD else 'iy'
        at += 1
        b1 = rom[at]
        if b1 == 0xCB:
            o.d = _s8(rom[at + 1])
            o.op = 0x100 | rom[at + 2]       # CB page, via (XY+d)
            o.n = 4
            _flags_cb(o)
            o.succ = [pc + o.n]
            return o
        b0 = b1
    if b0 == 0xCB and o.pfx is None:
        o.op = 0x100 | rom[at + 1]
        o.n = 2
        _flags_cb(o)
        o.succ = [pc + 2]
        return o
    if b0 == 0xED and o.pfx is None:
        o.op = 0x200 | rom[at + 1]
        o.n = 2
        x = rom[at + 1]
        if x in (0x43, 0x53, 0x63, 0x73, 0x4B, 0x5B, 0x6B, 0x7B):
            o.imm = rom[at + 2] | (rom[at + 3] << 8)
            o.n = 4
        _flags_ed(o)
        nxt = pc + o.n
        if x in (0x45, 0x4D, 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D):
            o.succ = [UNKNOWN]               # RETN / RETI
        else:
            o.succ = [nxt]
        return o
    o.op = b0
    at += 1
    xy = o.pfx is not None
    # (XY+d) operand?
    hasd = xy and (b0 in (0x34, 0x35, 0x36) or
                   (0x40 <= b0 < 0x80 and b0 != 0x76 and ((b0 & 7) == 6 or ((b0 >> 3) & 7) == 6)) or
                   (0x80 <= b0 < 0xC0 and (b0 & 7) == 6))
    if hasd:
        o.d = _s8(rom[at])
        at += 1
    if b0 in (0x01, 0x11, 0x21, 0x31, 0x22, 0x2A, 0x32, 0x3A, 0xC3, 0xCD) or \
            (b0 & 0xC7) in (0xC2, 0xC4):
        o.imm = rom[at] | (rom[at + 1] << 8)
        at += 2
    elif (b0 & 0xC7) == 0x06 or (b0 & 0xC7) == 0xC6 or b0 in (0xD3, 0xDB, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38):
        o.imm = rom[at]
        at += 1
    o.n = at - pc
    nxt = pc + o.n
    _flags_main(o)
    if b0 in (0xC3,):
        o.succ = [o.imm]
    elif b0 == 0x18:
        o.succ = [(nxt + _s8(o.imm)) & 0xFFFF]
    elif b0 in (0x20, 0x28, 0x30, 0x38, 0x10):
        o.succ = [(nxt + _s8(o.imm)) & 0xFFFF, nxt]
    elif (b0 & 0xC7) == 0xC2:
        o.succ = [o.imm, nxt]
    elif b0 == 0xCD:
        o.succ = [o.imm]
        o.call = True
    elif (b0 & 0xC7) == 0xC4:
        o.succ = [o.imm, nxt]
        o.call = True
    elif (b0 & 0xC7) == 0xC7:
        o.succ = [b0 & 0x38]
        o.call = True
    elif b0 == 0xC9 or b0 == 0xE9:
        o.succ = [UNKNOWN]
    elif (b0 & 0xC7) == 0xC0:
        o.succ = [UNKNOWN, nxt]
    else:
        o.succ = [nxt]
    return o


def target(o):
    """The jump/call target of a direct branch, else None."""
    b = o.op
    if o.op >= 0x100:
        return None
    if b in (0xC3, 0xCD) or (b & 0xC7) in (0xC2, 0xC4):
        return o.imm
    if b in (0x18, 0x20, 0x28, 0x30, 0x38, 0x10):
        return (o.pc + o.n + _s8(o.imm)) & 0xFFFF
    if (b & 0xC7) == 0xC7:
        return b & 0x38
    return None


def _flags_main(o):
    b = o.op
    if 0x80 <= b < 0xC0 or (b & 0xC7) == 0xC6:
        alu = (b >> 3) & 7
        o.kill = ALL
        o.use = C if alu in (1, 3) else 0
        o.pv = 'ovf' if alu in (0, 1, 2, 3, 7) else 'par'
        o.nval = 1 if alu in (2, 3, 7) else 0
    elif (b & 0xC7) in (0x04, 0x05):
        o.kill = Z | P | H | N
        o.pv = 'ovf'
        o.nval = 1 if (b & 7) == 5 else 0
    elif (b & 0xCF) == 0x09:
        o.kill = C | H | N
        o.nval = 0
    elif b in (0x07, 0x0F):
        o.kill = C | H | N
        o.nval = 0
    elif b in (0x17, 0x1F):
        o.use = C
        o.kill = C | H | N
        o.nval = 0
    elif b == 0x27:
        o.use = C | H | N
        o.kill = C | Z | P | H
        o.pv = 'par'
    elif b == 0x2F:
        o.kill = H | N
        o.nval = 1
    elif b == 0x37:
        o.kill = C | H | N
        o.nval = 0
    elif b == 0x3F:
        o.use = C
        o.kill = C | H | N
        o.nval = 0
    elif b == 0xF5:
        o.use = ALL
    elif b == 0xF1:
        o.kill = ALL
    elif b == 0x08:
        o.use = ALL                  # EX AF,AF': the flags go to memory
        o.kill = ALL
    elif (b & 0xC7) in (0xC0, 0xC2, 0xC4):
        o.use = CCUSE[(b >> 3) & 7]
    elif b in (0x20, 0x28):
        o.use = Z
    elif b in (0x30, 0x38):
        o.use = C


def _flags_cb(o):
    x = o.op & 0xFF
    g = x >> 6
    if g == 0:
        o.kill = ALL
        o.pv = 'par'
        o.nval = 0
        if ((x >> 3) & 7) in (2, 3):
            o.use = C
    elif g == 1:
        o.kill = Z | P | H | N       # S too, in Z's group
        o.pv = 'bit'
        o.nval = 0


def _flags_ed(o):
    x = o.op & 0xFF
    if (x & 0xC7) == 0x40:           # IN r,(C)
        o.kill = Z | P | H | N
        o.pv = 'par'
        o.nval = 0
    elif (x & 0xCF) in (0x42, 0x4A):  # SBC/ADC HL,rr
        o.use = C
        o.kill = ALL
        o.pv = 'ovf'
        o.nval = 1 if (x & 0xCF) == 0x42 else 0
    elif (x & 0xC7) == 0x44:         # NEG
        o.kill = ALL
        o.pv = 'ovf'
        o.nval = 1
    elif x in (0x57, 0x5F):          # LD A,I / A,R
        o.kill = Z | P | H | N
        o.pv = 'iff'
        o.nval = 0
    elif x in (0x67, 0x6F):          # RRD / RLD
        o.kill = Z | P | H | N
        o.pv = 'par'
        o.nval = 0
    elif x in (0xA0, 0xA8, 0xB0, 0xB8):
        o.kill = P | H | N
        o.pv = 'bc'
        o.nval = 0
    elif x in (0xA1, 0xA9, 0xB1, 0xB9):
        o.kill = Z | P | H | N
        o.pv = 'bc'
        o.nval = 1

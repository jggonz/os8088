#!/usr/bin/env python3
# =============================================================================
# os8088 - apps/arcade/arcz80t.py - the Z80 ahead-of-time translator, for the
# Namco Pac-Man board (docs/ARCADE-SPEC.md section 7)
#
#     python3 apps/arcade/arcz80t.py ROM SEEDS OUT.asm CODE.bin AUX.bin
#
# arcaot.py's method and arcaot.py's runtime (arcrt.inc: the PC table, the
# header, arc_enter and the ways back), for a Z80: every instruction the walk
# reaches becomes a label and 8086 code, and the file is assembled until its
# short branches settle. The board's helpers are arcz80rt.inc.
#
# THE REGISTER MAP (ARCADE-SPEC 7.1) is the 8080's: A=AL, BC=CX, DE=DX,
# HL=BX, SP=SP with SS=DS=the view, the flags IN FLAGS. The Z80's extras are
# view cells: IX, IY, the alternate set, I, IM, the vector, IFF2.
#
# THE FLAGS (ARCADE-SPEC 7.3). S Z H C are the 8086's own after the ALU ops.
# P/V is parity after a logical op, as PF is, and OVERFLOW after arithmetic,
# so where P is live after one of those, OF is copied into PF. N is read only
# by DAA, so it is not kept at all: each DAA is translated as DAA or DAS by
# the last instruction that set N on every path into it.
#
# PAC-MAN'S MEMORY MAP is written into every store (ARCADE-SPEC 7.4): RAM
# 4C00-4FFF is one MOV, video 4000-47FF compares and lists its TILE, the I/O
# at 5000-50FF calls arc_iow, ROM and 4800-4BFF take nothing.
# =============================================================================
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import z80dec                       # noqa: E402
from z80dec import C, Z, P, H, N, ALL, UNKNOWN   # noqa: E402
import arcaot                       # noqa: E402  (assemble)

R8 = ['ch', 'cl', 'dh', 'dl', 'bh', 'bl', None, 'al']
RP = ['cx', 'dx', 'bx', 'sp']
CC = ['nz', 'z', 'nc', 'c', 'po', 'pe', 'ns', 's']
INV = {'nz': 'z', 'z': 'nz', 'nc': 'c', 'c': 'nc', 'po': 'pe', 'pe': 'po',
       'ns': 's', 's': 'ns'}
ALU = ['add', 'adc', 'sub', 'sbb', 'and', 'xor', 'or', 'cmp']
FL = C | Z | P | H               # the groups the 8086's FLAGS hold
FLAGBASE = 'PM_TFLAG'            # aux: a byte a tile, at the tile's index
XYC = {'ix': 'ST_IX', 'iy': 'ST_IY'}


class Refuse(Exception):
    """An instruction this translator does not take: a walk that meets one
    is wrong (a speculative walk is dropped), and real code meeting one is a
    build failure naming it."""


# --- the walk ----------------------------------------------------------------
RST_RET = {}                        # vector -> bytes after the RST it returns
                                    #   to, from the seeds file: Pac-Man's RST
                                    #   28h and 30h carry 2 and 3 data bytes


def rst_return(o):
    """Where a CALL or RST returns, or None if the seeds do not say."""
    if not o.call:
        return None
    if (o.op & 0xC7) != 0xC7:
        return o.pc + o.n
    k = RST_RET.get(o.op & 0x38)
    return None if k is None else o.pc + k


def walk(rom, seeds):
    code = {}
    work = [0x0000, 0x0038, 0x0066] + list(seeds)
    while work:
        pc = work.pop()
        while pc not in code and pc < 0x4000:
            o = z80dec.decode(rom, pc)
            check(o)
            code[pc] = o
            for s in o.succ:
                if s != UNKNOWN and s not in code:
                    work.append(s)
            r = rst_return(o)              # a CALL's return site, and an
            if r is not None:              #   RST's where the seeds say: its
                work.append(r)             #   inline data is not code
            if len(o.succ) == 1 and o.succ[0] != pc + o.n:
                break
            pc += o.n
    return code


def check(o):
    """Raise Refuse for what translate() does not take."""
    x = o.op
    if o.pfx and x < 0x100:
        if x in (0x24, 0x25, 0x26, 0x2C, 0x2D, 0x2E) or \
                (0x40 <= x < 0x80 and o.d is None and x != 0x76 and
                 ((x & 7) in (4, 5) or ((x >> 3) & 7) in (4, 5))) or \
                (0x80 <= x < 0xC0 and o.d is None and (x & 7) in (4, 5)):
            raise Refuse('IXH/IXL')
    if x >= 0x200:
        e = x & 0xFF
        if e in (0xA1, 0xA9, 0xA2, 0xAA, 0xB2, 0xBA,
                 0xA3, 0xAB, 0xB3, 0xBB, 0x67, 0x6F, 0x57, 0x5F):
            raise Refuse('ED %02X' % e)
        if (e & 0xC7) in (0x40, 0x41):
            raise Refuse('IN/OUT (C)')
    if x == 0xDB:
        raise Refuse('IN A,(n)')
    if x == 0xD3 and o.imm != 0:
        raise Refuse('OUT (%02X),A' % o.imm)


def speculate(rom, code):
    """arcaot.speculate()'s rule for the Z80: every word in the ROM's data
    that points below 4000h, walked tentatively and kept only whole."""
    owner = {}
    for o in code.values():
        for k in range(o.pc, o.pc + o.n):
            owner[k] = o.pc
    added = 0
    for a in range(0, 0x4000 - 1):
        if a in owner or a + 1 in owner:
            continue
        w = rom[a] | (rom[a + 1] << 8)
        if w >= 0x4000 or w in code or w in owner:
            continue
        trial = {}
        work = [w]
        ok = True
        while work and ok:
            pc = work.pop()
            while pc not in code and pc not in trial:
                if pc >= 0x4000 or pc in owner:
                    ok = pc not in owner or owner[pc] == pc
                    break
                try:
                    o = z80dec.decode(rom, pc)
                    check(o)
                except (Refuse, IndexError):
                    ok = False
                    break
                if pc + o.n > 0x4000 or any(k in owner and owner[k] != pc
                                            for k in range(pc, pc + o.n)) \
                        or any(k in trial for k in range(pc + 1, pc + o.n)) \
                        or o.op == 0x76:
                    ok = False
                    break
                if any(t != UNKNOWN and t >= 0x4000 for t in o.succ):
                    ok = False              # a branch out of the ROM: data
                    break
                trial[pc] = o
                for t in o.succ:
                    if t != UNKNOWN and t not in code and t not in trial:
                        work.append(t)
                r = rst_return(o)
                if r is not None:
                    work.append(r)
                if len(o.succ) == 1 and o.succ[0] != pc + o.n:
                    break
                pc += o.n
            if len(trial) > 400:
                ok = False
        if ok and trial:
            for pc, o in trial.items():
                code[pc] = o
                for k in range(pc, pc + o.n):
                    owner[k] = pc
            added += len(trial)
    return added


def liveness(code):
    for o in code.values():
        o.lin = ALL
    changed = True
    while changed:
        changed = False
        for pc in sorted(code, reverse=True):
            o = code[pc]
            out = 0
            for s in o.succ:
                out |= ALL if s == UNKNOWN or s not in code else code[s].lin
            lin = o.use | (out & ~o.kill & ALL)
            if out != o.lout or lin != o.lin:
                o.lout, o.lin = out, lin
                changed = True


def daa_kinds(code):
    """For each DAA: 'add' or 'sub', by the last N-setter on every path in.
    Unknown (an entry from a RET, the start) is reported and taken as add."""
    preds = {}
    for o in code.values():
        for s in o.succ:
            if s != UNKNOWN:
                preds.setdefault(s, []).append(o.pc)
    out = {}
    unsure = []
    for o in code.values():
        if o.op != 0x27 or o.pfx:
            continue
        seen = set()
        found = set()
        work = list(preds.get(o.pc, []))
        if not work:
            found.add(None)
        while work:
            p = work.pop()
            if p in seen:
                continue
            seen.add(p)
            q = code[p]
            if q.nval is not None:
                found.add(q.nval)
                continue
            ps = preds.get(p, [])
            if not ps or len(seen) > 64:
                found.add(None)
                continue
            work.extend(ps)
        if found == {1}:
            out[o.pc] = 'sub'
        else:
            out[o.pc] = 'add'
            if found != {0}:
                unsure.append(o.pc)
    return out, unsure


def mark_polls(code):
    pcs = sorted(code)
    for o in code.values():
        t = z80dec.target(o)
        if t is None or o.call:
            continue
        if t <= o.pc and t in code:
            head = code[t]
            head.poll = True
            span = [p for p in pcs if t <= p <= o.pc]
            w = len(span) + 15 * sum(1 for p in span if code[p].call)
            head.weight = max(head.weight, min(127, w))
        if o.op == 0x76:
            o.poll = True


# --- emitting ------------------------------------------------------------------
def guard(live, body):
    return (['lahf'] + body + ['sahf']) if live & FL else body


def pv_fix(o):
    """OF into PF, where P is live after an arithmetic op (ARCADE-SPEC 7.3)."""
    if not (o.lout & P) or o.pv != 'ovf':
        return []
    return ['jo .o', 'lahf', 'and ah, 0xFB', 'sahf', 'jmp short .e',
            '.o:', 'lahf', 'or ah, 0x04', 'sahf', '.e:']


def szp_after_rot(x, o):
    """S Z P after a rotate the 8086 leaves them alone on, CY kept."""
    if not (o.lout & (Z | P)):
        return []
    return ['lahf', 'test %s, 0xFF' % x if x.startswith('byte') else 'test %s, %s' % (x, x),
            'rcr ah, 1']


MARK = ['and si, 0x03FF', 'cmp byte [es:si+PM_TFLAG], 0', 'jne .d',
        'mov byte [es:si+PM_TFLAG], 1', 'mov [es:di], si', 'lea di, [di+2]']


def store(areg, hb, src, live):
    """A byte store of src (a register, or 'byte 0xNN') through BX or SI.
    hb = the byte register holding the address's high byte, or None to
    compare SI as a word."""
    if hb:
        lo = ['cmp %s, 0x4C' % hb, 'jb .lo', 'cmp %s, 0x50' % hb, 'jae .io']
        vid = ['cmp %s, 0x40' % hb, 'jb .d', 'cmp %s, 0x48' % hb, 'jae .d']
    else:
        lo = ['cmp si, 0x4C00', 'jb .lo', 'cmp si, 0x5000', 'jae .io']
        vid = ['cmp si, 0x4000', 'jb .d', 'cmp si, 0x4800', 'jae .d']
    body = lo + ['mov [%s], %s' % (areg, src), 'jmp short .d', '.lo:'] + vid + \
        ['cmp [%s], %s' % (areg, src), 'je .d', 'mov [%s], %s' % (areg, src)]
    if areg == 'bx':
        body.append('mov si, bx')
    body += MARK + ['jmp short .d', '.io:', 'mov [ST_SAVE], ax']
    if src != 'al':
        body.append('mov al, %s' % src)
    if areg == 'bx':
        body.append('mov si, bx')
    body += ['mov word [ST_RETIO], .b', 'jmp arc_iow', '.b:', 'mov ax, [ST_SAVE]', '.d:']
    return guard(live, body)


def store_const(a, src):
    """A store to a fixed address: classified here, not at run time."""
    a &= 0xFFFF
    if 0x4C00 <= a < 0x5000:
        return ['mov [0x%04X], %s' % (a, src)]
    if 0x4000 <= a < 0x4800:
        k = a & 0x3FF
        return ['cmp [0x%04X], %s' % (a, src), 'je .d', 'mov [0x%04X], %s' % (a, src),
                'cmp byte [es:0x%04X+PM_TFLAG], 0' % k, 'jne .d',
                'mov byte [es:0x%04X+PM_TFLAG], 1' % k,
                'mov word [es:di], 0x%04X' % k, 'lea di, [di+2]', '.d:']
    if 0x5000 <= a < 0x5100:
        body = ['mov [ST_SAVE], ax']
        if src != 'al':
            body.append('mov al, %s' % src)
        return body + ['mov si, 0x%04X' % a, 'mov word [ST_RETIO], .b', 'jmp arc_iow',
                       '.b:', 'mov ax, [ST_SAVE]']
    return []                       # ROM, 4800-4BFF: nothing there


def vmark(hb, areg):
    """After a read-modify-write of memory: list the tile if it is video."""
    body = ['cmp %s, 0x40' % hb, 'jb .d', 'cmp %s, 0x48' % hb, 'jae .d'] if hb else \
        ['cmp si, 0x4000', 'jb .d', 'cmp si, 0x4800', 'jae .d']
    if areg == 'bx':
        body.append('mov si, bx')
    return body + MARK + ['.d:']


def jmp_to(t, code):
    t &= 0x7FFF                     # A15 is not decoded on the board
    return 'jmp L%04X' % t if t in code else 'jmp arc_miss_%04X' % t


def ret_seq():
    return ['pop si', 'lahf', 'shl si, 1', 'sahf', 'jmp word [es:si]']


def call_seq(ret, t, code):
    return ['mov si, 0x%04X' % ret, 'push si', jmp_to(t, code)]


def operand(o, n):
    """(pre, operand, store-address info) for register-field n of o: a
    register, or memory through (HL) / (IX+d)."""
    if n != 6:
        return [], R8[n], None
    if o.pfx:
        # A15 is not decoded on the board, and Pac-Man does point IX at
        # C3D4 for 43D4 (its text routine at 2C65): mask it, guarding the
        # AND where the flags are live into the instruction (ARCADE-SPEC 7.4)
        return guard(o.lin, ['mov si, [%s]' % XYC[o.pfx], 'lea si, [si+%d]' % o.d,
                             'and si, 0x7FFF']), 'byte [si]', ('si', None)
    return [], 'byte [bx]', ('bx', 'bh')


def translate(o, code, longjcc, daa):
    x = o.op
    out = o.lout
    xy = o.pfx
    if x >= 0x200:
        return translate_ed(o, code)
    if x >= 0x100:
        return translate_cb(o)
    # --- LD r,r' / LD r,(HL) / LD (HL),r ------------------------------------
    if 0x40 <= x < 0x80 and x != 0x76:
        d, s = (x >> 3) & 7, x & 7
        if d == 6:
            pre, _, (areg, hb) = operand(o, 6)
            return pre + store(areg, hb, R8[s], out)
        if s == 6:
            pre, opnd, _ = operand(o, 6)
            return pre + ['mov %s, %s' % (R8[d], opnd)]
        return [] if d == s else ['mov %s, %s' % (R8[d], R8[s])]
    if 0x80 <= x < 0xC0:
        pre, opnd, _ = operand(o, x & 7)
        return pre + ['%s al, %s' % (ALU[(x >> 3) & 7], opnd)] + pv_fix(o)
    if (x & 0xC7) == 0xC6:
        return ['%s al, 0x%02X' % (ALU[(x >> 3) & 7], o.imm)] + pv_fix(o)
    if x == 0x00:
        return []
    if x == 0x08:
        return ['lahf', 'xchg ax, [ST_AF2]', 'sahf']
    if x == 0x10:                                   # DJNZ
        t = z80dec.target(o)
        if out & FL:
            return ['lahf', 'dec ch', 'jz .n', 'sahf', jmp_to(t, code), '.n:', 'sahf']
        if o.pc in longjcc or t not in code:
            return ['dec ch', 'jz .j', jmp_to(t, code), '.j:']
        return ['dec ch', 'jnz L%04X' % t]
    if x == 0x18:
        return [jmp_to(z80dec.target(o), code)]
    if x in (0x20, 0x28, 0x30, 0x38) or (x & 0xC7) == 0xC2:
        cc = CC[(x >> 3) & 3] if x < 0x40 else CC[(x >> 3) & 7]
        t = z80dec.target(o)
        if o.pc in longjcc or t not in code:
            return ['j%s .j' % INV[cc], jmp_to(t, code), '.j:']
        return ['j%s L%04X' % (cc, t)]
    if (x & 0xCF) == 0x01:
        p = x >> 4
        if p == 2 and xy:
            return ['mov word [%s], 0x%04X' % (XYC[xy], o.imm)]
        return ['mov %s, 0x%04X' % (RP[p], o.imm)]
    if (x & 0xCF) == 0x09:                          # ADD HL/IX,rr
        p = x >> 4
        if xy:
            src = 'si' if p == 2 else RP[p]
            body = ['mov si, [%s]' % XYC[xy], 'add si, %s' % src, 'mov [%s], si' % XYC[xy]]
            if out & (Z | P):
                return ['lahf', 'shr ah, 1'] + body[:2] + ['rcl ah, 1', body[2], 'sahf']
            return body
        if out & (Z | P):
            return ['lahf', 'shr ah, 1', 'add bx, %s' % RP[p], 'rcl ah, 1', 'sahf']
        return ['add bx, %s' % RP[p]]
    if x == 0x02:
        return ['mov si, cx'] + store('si', 'ch', 'al', out)
    if x == 0x12:
        return ['mov si, dx'] + store('si', 'dh', 'al', out)
    if x == 0x0A:
        return ['mov si, cx', 'mov al, [si]']
    if x == 0x1A:
        return ['mov si, dx', 'mov al, [si]']
    if x == 0x22:                                   # LD (nn),HL / IX
        src = 'bx' if not xy else None
        return store16_const(o.imm, src, xy, out)
    if x == 0x2A:
        if xy:
            return ['mov si, [0x%04X]' % o.imm, 'mov [%s], si' % XYC[xy]]
        return ['mov bx, [0x%04X]' % o.imm]
    if x == 0x32:
        return guard(out, store_const(o.imm, 'al')) if not (0x4C00 <= o.imm < 0x5000) \
            else store_const(o.imm, 'al')
    if x == 0x3A:
        return ['mov al, [0x%04X]' % o.imm]
    if (x & 0xCF) in (0x03, 0x0B):                  # INC/DEC rr
        p = x >> 4
        op = 'inc' if (x & 0xCF) == 0x03 else 'dec'
        if p == 2 and xy:
            return guard(out, ['%s word [%s]' % (op, XYC[xy])])
        if p == 2:
            return ['lea bx, [bx%s1]' % ('+' if op == 'inc' else '-')]
        return guard(out, ['%s %s' % (op, RP[p])])
    if (x & 0xC7) in (0x04, 0x05):                  # INC/DEC r, (HL), (IX+d)
        r = (x >> 3) & 7
        op = 'inc' if (x & 7) == 4 else 'dec'
        if r == 6:
            pre, opnd, (areg, hb) = operand(o, 6)
            return pre + ['%s %s' % (op, opnd)] + pv_fix(o) + guard(out, vmark(hb, areg))
        return ['%s %s' % (op, R8[r])] + pv_fix(o)
    if (x & 0xC7) == 0x06:                          # LD r,n
        r = (x >> 3) & 7
        if r == 6:
            pre, _, (areg, hb) = operand(o, 6)
            return pre + store(areg, hb, 'byte 0x%02X' % o.imm, out)
        return ['mov %s, 0x%02X' % (R8[r], o.imm)]
    if x == 0x07: return ['rol al, 1']
    if x == 0x0F: return ['ror al, 1']
    if x == 0x17: return ['rcl al, 1']
    if x == 0x1F: return ['rcr al, 1']
    if x == 0x27:
        return ['das' if daa.get(o.pc) == 'sub' else 'daa']
    if x == 0x2F: return ['not al']
    if x == 0x37: return ['stc']
    if x == 0x3F: return ['cmc']
    if x == 0x76:
        return ['mov si, 0x%04X' % (o.pc + 1), 'jmp arc_halt']
    if (x & 0xC7) == 0xC0:
        return ['j%s .r' % INV[CC[(x >> 3) & 7]]] + ret_seq() + ['.r:']
    if (x & 0xCF) == 0xC1:
        p = (x >> 4) & 3
        if p == 3:
            return ['pop ax', 'xchg al, ah', 'sahf']
        if p == 2 and xy:
            return ['pop word [%s]' % XYC[xy]]
        return ['pop %s' % RP[p]]
    if (x & 0xCF) == 0xC5:
        p = (x >> 4) & 3
        if p == 3:
            return ['lahf', 'xchg al, ah', 'push ax', 'mov al, ah']
        if p == 2 and xy:
            return ['push word [%s]' % XYC[xy]]
        return ['push %s' % RP[p]]
    if x == 0xC3:
        return [jmp_to(o.imm, code)]
    if (x & 0xC7) == 0xC4:
        return ['j%s .k' % INV[CC[(x >> 3) & 7]]] + call_seq(o.pc + 3, o.imm, code) + ['.k:']
    if x == 0xCD:
        return call_seq(o.pc + 3, o.imm, code)
    if (x & 0xC7) == 0xC7:
        return call_seq(o.pc + 1, x & 0x38, code)
    if x == 0xC9:
        return ret_seq()
    if x == 0xD9:
        return ['xchg cx, [ST_BC2]', 'xchg dx, [ST_DE2]', 'xchg bx, [ST_HL2]']
    if x == 0xD3:
        return ['mov [ST_VEC], al']
    if x == 0xE3:
        if xy:
            return ['mov si, sp', 'xchg bx, [%s]' % XYC[xy], 'xchg bx, [si]',
                    'xchg bx, [%s]' % XYC[xy]]
        return ['mov si, sp', 'xchg bx, [si]']
    if x == 0xE9:
        src = 'mov si, [%s]' % XYC[xy] if xy else 'mov si, bx'
        return ['lahf', 'sub bp, byte 16', 'jg .p', 'mov si, 0x%04X' % o.pc,
                'jmp arc_pollsi_ah', '.p:', src, 'shl si, 1', 'sahf', 'jmp word [es:si]']
    if x == 0xEB:
        return ['xchg bx, dx']
    if x == 0xF3:
        return ['mov byte [ST_INTE], 0', 'mov byte [ST_IFF2], 0']
    if x == 0xFB:
        return ['mov byte [ST_INTE], 1', 'mov byte [ST_IFF2], 1'] + guard(out, [
            'cmp byte [ST_PEND], 0', 'je .e', 'mov bp, 1', 'mov byte [ST_EIX], 1', '.e:'])
    if x == 0xF9:
        return ['mov sp, [%s]' % XYC[xy]] if xy else ['mov sp, bx']
    raise Refuse('main %02X' % x)


def store16_const(a, src, xy, out):
    if 0x4C00 <= a and a + 1 < 0x5000:
        if xy:
            return ['mov si, [%s]' % XYC[xy], 'mov [0x%04X], si' % a]
        return ['mov [0x%04X], %s' % (a, src)]
    body = two(store_const(a, 'bl'), store_const(a + 1, 'bh'))
    if xy:                          # IX's value in BX for the length of it:
        body = ['xchg bx, [%s]' % XYC[xy]] + body + ['xchg bx, [%s]' % XYC[xy]]
    return guard(out, body)


def two(first, second):
    """Two stores in one instruction: the second's local labels renamed."""
    out = list(first)
    for ln in second:
        out.append(re.sub(r'(?<![\w\]])\.([a-z])\b', r'.\g<1>2', ln))
    return out


def translate_cb(o):
    x = o.op & 0xFF
    g, n, r = x >> 6, (x >> 3) & 7, x & 7
    out = o.lout
    if o.pfx:
        pre = guard(o.lin, ['mov si, [%s]' % XYC[o.pfx], 'lea si, [si+%d]' % o.d,
                            'and si, 0x7FFF'])
        opnd, hb, areg = 'byte [si]', None, 'si'
        if r != 6:
            raise Refuse('DD CB with a register copy')
    elif r == 6:
        pre, opnd, hb, areg = [], 'byte [bx]', 'bh', 'bx'
    else:
        pre, opnd, hb, areg = [], R8[r], None, None
    mem = areg is not None
    if g == 0:
        ins = {0: ['rol %s, 1' % opnd], 1: ['ror %s, 1' % opnd],
               2: ['rcl %s, 1' % opnd], 3: ['rcr %s, 1' % opnd],
               4: ['shl %s, 1' % opnd], 5: ['sar %s, 1' % opnd],
               6: ['stc', 'rcl %s, 1' % opnd], 7: ['shr %s, 1' % opnd]}[n]
        body = pre + ins
        if n in (0, 1, 2, 3, 6):
            body += szp_after_rot(opnd, o)
        if mem:
            body += guard(out, vmark(hb, areg))
        return body
    if g == 1:
        test = 'test %s, 0x%02X' % (opnd, 1 << n)
        if out & C:
            return pre + ['lahf', test, 'rcr ah, 1']
        return pre + [test]
    op = 'and' if g == 2 else 'or'
    mask = (~(1 << n)) & 0xFF if g == 2 else 1 << n
    body = pre + guard(out, ['%s %s, 0x%02X' % (op, opnd, mask)] +
                       (vmark(hb, areg) if mem else []))
    return body


def translate_ed(o, code):
    e = o.op & 0xFF
    out = o.lout
    if (e & 0xCF) in (0x42, 0x4A):              # SBC/ADC HL,rr
        p = (e >> 4) & 3
        op = 'sbb' if (e & 0xCF) == 0x42 else 'adc'
        return ['%s bx, %s' % (op, RP[p])] + pv_fix(o)
    if (e & 0xCF) == 0x43:                      # LD (nn),rr
        p = (e >> 4) & 3
        if p == 2:
            return store16_const(o.imm, 'bx', None, out)
        if 0x4C00 <= o.imm < 0x4FFF:
            return ['mov [0x%04X], %s' % (o.imm, RP[p])]
        if p == 3:
            raise Refuse('LD (nn),SP outside RAM')
        lo, hi = {0: ('cl', 'ch'), 1: ('dl', 'dh')}[p]
        return guard(out, two(store_const(o.imm, lo), store_const(o.imm + 1, hi)))
    if (e & 0xCF) == 0x4B:                      # LD rr,(nn)
        return ['mov %s, [0x%04X]' % (RP[(e >> 4) & 3], o.imm)]
    if (e & 0xC7) == 0x44:                      # NEG
        return ['neg al'] + pv_fix(o)
    if (e & 0xC7) == 0x45:                      # RETN / RETI
        return ['mov ah, [ST_IFF2]', 'mov [ST_INTE], ah'] + ret_seq()
    if (e & 0xC7) == 0x46:
        return ['mov byte [ST_IM], %d' % (0, 0, 1, 2)[(e >> 3) & 3]]
    if e == 0x47:
        return ['mov [ST_I], al']
    if e == 0x4F:
        return ['mov [ST_R], al']
    if e in (0xB0, 0xB8):                       # LDIR / LDDR
        body = ['mov word [ST_RET], .b', 'jmp arc_%s' % ('ldir' if e == 0xB0 else 'lddr'), '.b:']
        if out & P:
            body += ['lahf', 'and ah, 0xFB', 'sahf']   # P/V = 0: BC ran out
        return body
    if e in (0xA0, 0xA8):                       # LDI / LDD: P/V made there
        return ['mov word [ST_RET], .b', 'jmp arc_%s' % ('ldi' if e == 0xA0 else 'ldd'), '.b:']
    if e in (0xB1, 0xB9):                       # CPIR / CPDR: S Z H P/V made
        return ['mov word [ST_RET], .b',                         # there
                'jmp arc_%s' % ('cpir' if e == 0xB1 else 'cpdr'), '.b:']
    if (e & 0xC7) in (0x40, 0x41) or e in (0x57, 0x5F, 0x67, 0x6F):
        raise Refuse('ED %02X' % e)
    return []                                   # the ED NOPs


# --- the file ------------------------------------------------------------------
def emit(code, longjcc, daa, romname):
    out = ['; GENERATED by apps/arcade/arcz80t.py from %s - never committed' % romname,
           'cpu 8086', 'bits 16', 'org 0', '%include "arcdefs.inc"', '',
           '%include "arcrt.inc"', '%include "arcz80rt.inc"', '']
    misses = set()
    for pc in sorted(code):
        o = code[pc]
        out.append('L%04X:' % pc)
        if o.poll:
            dec = '    dec bp' if o.weight == 1 else '    sub bp, byte %d' % o.weight
            if o.lin & FL:          # (no CALL: ARCADE-SPEC 7.5)
                out += ['    lahf', dec, '    jg .c', '    mov si, 0x%04X' % pc,
                        '    jmp arc_pollsi_ah', '.c:', '    sahf']
            else:
                out += [dec, '    jg .c', '    mov si, 0x%04X' % pc,
                        '    jmp arc_pollsi', '.c:']
        for line in translate(o, code, longjcc, daa):
            m = re.search(r'arc_miss_([0-9A-F]{4})', line)
            if m:
                misses.add(int(m.group(1), 16))
            out.append(line if line.endswith(':') else '    ' + line)
        nxt = pc + o.n
        if nxt not in code and nxt in o.succ:
            out.append('    jmp arc_miss_%04X' % nxt)
            misses.add(nxt)
    for pc in sorted(misses):
        out += ['arc_miss_%04X:' % pc, '    mov si, 0x%04X' % ((pc & 0x7FFF) * 2),
                '    jmp arc_miss']
    out += ['arc_code_end:', '']
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('seeds')
    ap.add_argument('out', help='the NASM source, for reading')
    ap.add_argument('bin', help='the code part')
    ap.add_argument('aux', help="aux's PC table, 32KB")
    ap.add_argument('--nasm', default='nasm')
    ap.add_argument("--aux-kb", type=int, default=50,
                    help="aux's size: the table, the list and the tile flags (9400h)")
    ap.add_argument('--no-speculate', action='store_true')
    a = ap.parse_args()
    img = open(a.rom, 'rb').read()
    rom = img[:0x4000]
    seeds = []
    for line in open(a.seeds):
        line = line.split('#')[0].strip()
        if line.startswith('RST '):
            vec, k = line[4:].split()
            RST_RET[int(vec, 16)] = int(k)
        elif line:
            seeds.append(int(line, 16))
    try:
        code = walk(rom, seeds)
    except Refuse as e:
        raise SystemExit('arcz80t: the walk met what no translation takes: %s' % e)
    spec = 0 if a.no_speculate else speculate(rom, code)
    liveness(code)
    mark_polls(code)
    daa, unsure = daa_kinds(code)
    for pc in unsure:
        print('arcz80t: DAA at %04X: N not known on every path in; taken as after an add'
              % pc)
    romname = os.path.abspath(a.rom)
    asm, binary, syms, longjcc = arcaot.settle(lambda lj: emit(code, lj, daa, romname),
                                               a.nasm, [HERE], 'arcz80t')
    open(a.out, 'w').write(asm)
    size = arcaot.finish(binary, syms, code, a.bin, a.aux, 'arcz80t', a.aux_kb * 1024)
    print('arcz80t: %d instructions (%d of them speculative), %d poll points, '
          '%d long branches, %d bytes of code, DAA %s -> %s'
          % (len(code), spec, sum(1 for o in code.values() if o.poll), len(longjcc), size,
             ' '.join('%04X:%s' % kv for kv in sorted(daa.items())), a.out))


if __name__ == '__main__':
    main()

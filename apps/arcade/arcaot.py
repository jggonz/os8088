#!/usr/bin/env python3
# =============================================================================
# os8088 - apps/arcade/arcaot.py - the 8080 ahead-of-time translator
#
# Translates a whole arcade ROM to 8086 code before the game runs
# (docs/ARCADE-SPEC.md section 3): every 8080 instruction the walk reaches
# becomes a label and a few 8086 instructions, and a table of 16,384 words -
# the first 32KB of aux - maps every 8080 PC to its translation, for RET,
# PCHL and the host's interrupts. The output is the code part of ARCADE.O88
# and the table:
#
#     python3 apps/arcade/arcaot.py ROM SEEDS OUT.asm CODE.bin AUX.bin
#
# It assembles the file itself, as many times as it takes: an 8086 Jcc is
# short only, so a branch NASM reports out of range is re-emitted inverted
# around a near JMP and the file assembled again. Then it writes the code
# part and aux's PC table, from the symbol map. The ROM is never committed,
# so neither is anything this writes.
#
# THE REGISTER MAP (ARCADE-SPEC 3.1): A=AL, BC=CX, DE=DX, HL=BX, SP=SP with
# SS=DS=the view, so an 8080 PUSH is an 8086 PUSH. AH is scratch. The 8080's
# flags live IN the 8086 FLAGS register between instructions - LAHF's layout
# is the 8080 PSW's bit for bit - and are saved (LAHF/SAHF) only around a
# sequence of ours that would clobber them while the 8080 still needs them,
# which the liveness pass below decides. BP counts poll points down to the
# next return to the host (section 3.4).
# =============================================================================
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

C, Z = 1, 2                     # the two flag groups: CY, and S Z AC P
ALL = C | Z
UNKNOWN = -1                    # a successor we cannot see (RET, PCHL)

R8 = ['ch', 'cl', 'dh', 'dl', 'bh', 'bl', None, 'al']   # B C D E H L M A
RP = ['cx', 'dx', 'bx', 'sp']
CC = ['nz', 'z', 'nc', 'c', 'po', 'pe', 'ns', 's']      # 8080 cond -> 8086
INV = {'nz': 'z', 'z': 'nz', 'nc': 'c', 'c': 'nc', 'po': 'pe', 'pe': 'po',
       'ns': 's', 's': 'ns'}
CCUSE = [Z, Z, C, C, Z, Z, Z, Z]
ALU = ['add', 'adc', 'sub', 'sbb', 'and', 'xor', 'or', 'cmp']

# The Midway 8080 B&W board's ports (MAME mw8080bw, `invaders`). Each is the
# 8086 the instruction becomes; names are the view's cells in arcdefs.inc.
IN_PORTS = {
    0: ['mov al, [V_IN0]'],
    1: ['mov al, [V_IN1]'],
    2: ['mov al, [V_IN2]'],
    # IN 3, the shift register at its offset, INLINE: it is 30 reads a frame
    # (tools/arcsim.py) and the CALL with its PUSHF was 7% of an 8088's
    # time in play (MEASURED, MartyPC's 5150). OUT 2 stores the offset
    # masked, so no AND is needed here; the SHL is the only flag-setter
    3: ['push cx', 'mov cl, [V_SHOFF]', 'mov si, [V_SHLO]', 'shl si, cl',
        'pop cx', 'mov ax, si', 'mov al, ah'],
}
FLAGSAFE_IN = {0, 1, 2}
OUT_PORTS = {
    2: ['mov ah, al', 'and ah, 7', 'mov [V_SHOFF], ah'],
    3: ['mov [V_SND3], al'],
    4: ['mov ah, [V_SHHI]', 'mov [V_SHLO], ah', 'mov [V_SHHI], al'],
    5: ['mov [V_SND5], al'],
    6: [],                      # the watchdog: nothing to feed
}
VRAM_LO, VRAM_HI = 0x2400, 0x4000
# THE ROM, as the CPU sees it (ARCADE-SPEC 3.2): Space Invaders' 0000-1FFFh,
# or a Taito colour board's 0000-1FFFh and 4000-4FFFh (MAME 8080bw). main()
# sets these from the image's size; the PC table covers 0..TABLE_PCS-1.
ROM_REGIONS = [(0x0000, 0x2000)]
TABLE_PCS = 0x4000


def board_rom(img, name='the image'):
    """The CPU's 64KB from a board image, ROM_REGIONS and TABLE_PCS set from
    it: Space Invaders' 8KB, or a Taito colour board's 22KB - 0000-4FFFh,
    then the colour maps, which are not the CPU's - whose second region ends
    at the last 2KB chip that is not all zeros (Part II has one, Lunar
    Rescue two)."""
    global TABLE_PCS
    if len(img) == 0x2000:
        ROM_REGIONS[:] = [(0x0000, 0x2000)]
    elif len(img) == 0x5800:
        top = 0x5000
        while top > 0x4000 and not any(img[top - 0x800:top]):
            top -= 0x800
        ROM_REGIONS[:] = [(0x0000, 0x2000), (0x4000, top)]
    else:
        raise SystemExit('arcaot: %s is neither an 8KB nor a 22KB board image' % name)
    TABLE_PCS = max(0x4000, ROM_REGIONS[-1][1])
    return bytes(img[:0x5000]) + bytes(0x10000 - min(len(img), 0x5000))


def in_rom(a):
    return any(lo <= a < hi for lo, hi in ROM_REGIONS)


def region_of(a):
    for lo, hi in ROM_REGIONS:
        if lo <= a < hi:
            return hi
    return None


def oplen(o):
    if o in (0x01, 0x11, 0x21, 0x31, 0x22, 0x2a, 0x32, 0x3a, 0xc3, 0xcb,
             0xcd, 0xdd, 0xed, 0xfd) or (o & 0xc7) in (0xc2, 0xc4):
        return 3
    if (o & 0xc7) in (0x06, 0xc6) or o in (0xd3, 0xdb):
        return 2
    return 1


class Ins:
    __slots__ = ('pc', 'op', 'n', 'imm', 'succ', 'use', 'kill',
                 'lin', 'lout', 'poll', 'weight')

    def __init__(self, rom, pc):
        self.pc = pc
        self.op = rom[pc]
        self.n = oplen(self.op)
        self.imm = None
        if self.n == 2:
            self.imm = rom[pc + 1]
        elif self.n == 3:
            self.imm = rom[pc + 1] | (rom[pc + 2] << 8)
        self.use = 0
        self.kill = 0
        self.poll = False
        self.weight = 1
        self.lin = 0
        self.lout = 0
        o = self.op
        nxt = pc + self.n
        succ = [nxt]
        if o in (0xc3, 0xcb):
            succ = [self.imm]
        elif (o & 0xc7) == 0xc2:
            succ = [self.imm, nxt]
            self.use = CCUSE[(o >> 3) & 7]
        elif o in (0xcd, 0xdd, 0xed, 0xfd):
            succ = [self.imm]             # the callee; its RET is UNKNOWN
        elif (o & 0xc7) == 0xc4:
            succ = [self.imm, nxt]
            self.use = CCUSE[(o >> 3) & 7]
        elif (o & 0xc7) == 0xc7:
            succ = [o & 0x38]
        elif o in (0xc9, 0xd9):
            succ = [UNKNOWN]
        elif (o & 0xc7) == 0xc0:
            succ = [UNKNOWN, nxt]
            self.use = CCUSE[(o >> 3) & 7]
        elif o == 0xe9:
            succ = [UNKNOWN]
        if 0x80 <= o < 0xc0 or (o & 0xc7) == 0xc6:
            alu = (o >> 3) & 7
            self.kill = ALL
            if alu in (1, 3):
                self.use = C
        elif (o & 0xc7) in (0x04, 0x05):
            self.kill = Z
        elif (o & 0xcf) == 0x09:
            self.kill = C
        elif o in (0x07, 0x0f):
            self.kill = C
        elif o in (0x17, 0x1f):
            self.use = C
            self.kill = C
        elif o == 0x27:
            self.use = ALL
            self.kill = ALL
        elif o == 0x37:
            self.kill = C
        elif o == 0x3f:
            self.use = C
            self.kill = C
        elif o == 0xf5:
            self.use = ALL
        elif o == 0xf1:
            self.kill = ALL
        self.succ = succ


def walk(rom, seeds):
    code = {}
    work = [0x0000, 0x0008, 0x0010] + list(seeds)
    while work:
        pc = work.pop()
        while pc not in code and in_rom(pc):
            i = Ins(rom, pc)
            code[pc] = i
            for s in i.succ:
                if s != UNKNOWN and s not in code:
                    work.append(s)
            if i.op in (0xcd, 0xdd, 0xed, 0xfd) or (i.op & 0xc7) == 0xc7:
                work.append(pc + i.n)        # the return site: walked, but
                                             # reached by a RET, not an edge
            if len(i.succ) == 1 and i.succ[0] != pc + i.n:
                break
            pc += i.n
    for i in code.values():                  # an instruction running into
        for s in i.succ:                     # one we did not decode is two
            if s != UNKNOWN and s not in code and in_rom(s):
                raise SystemExit('arcaot: %04X runs into %04X, which the walk '
                                 'did not reach' % (i.pc, s))
    return code


def speculate(rom, code):
    """Seeds no run and no walk found: every word in the ROM's DATA - the
    bytes no decoded instruction covers - that points below 2000h, walked
    TENTATIVELY. A walk is kept only if every instruction it decodes is
    whole, overlaps no instruction already decoded, and uses only the
    board's ports; it may run into known code and stop there. An object's
    handler pointer in a table the game copies to RAM (Space Invaders keeps
    six at 1B13 + 16n and 1BC3, and the model's script reached five) is
    what this finds (ARCADE-SPEC 3.2)."""
    owner = {}
    for i in code.values():
        for k in range(i.pc, i.pc + i.n):
            owner[k] = i.pc
    added = 0
    for a in (k for lo, hi in ROM_REGIONS for k in range(lo, hi - 1)):
        if a in owner or a + 1 in owner:
            continue
        w = rom[a] | (rom[a + 1] << 8)
        if not in_rom(w) or w in code or w in owner:
            continue
        trial = {}
        work = [w]
        ok = True
        while work and ok:
            pc = work.pop()
            while pc not in code and pc not in trial:
                if not in_rom(pc) or pc in owner:
                    ok = pc not in owner or owner[pc] == pc
                    break
                i = Ins(rom, pc)
                o = i.op
                if pc + i.n > region_of(pc) or any(k in owner and owner[k] != pc
                                            for k in range(pc, pc + i.n)) \
                        or any(k in trial for k in range(pc + 1, pc + i.n)):
                    ok = False
                    break
                if (o == 0xd3 and i.imm not in OUT_PORTS) or \
                        (o == 0xdb and i.imm not in IN_PORTS) or o == 0x76:
                    ok = False
                    break
                if any(t != UNKNOWN and not in_rom(t) for t in i.succ):
                    ok = False              # a jump out of the ROM: data
                    break
                trial[pc] = i
                for t in i.succ:
                    if t != UNKNOWN and t not in code and t not in trial:
                        work.append(t)
                if is_call(o):
                    work.append(pc + i.n)
                if len(i.succ) == 1 and i.succ[0] != pc + i.n:
                    break
                pc += i.n
            if len(trial) > 400:
                ok = False
        if ok and trial:
            for pc, i in trial.items():
                code[pc] = i
                for k in range(pc, pc + i.n):
                    owner[k] = pc
            added += len(trial)
    return added


def liveness(code):
    for i in code.values():
        i.lin = ALL
    changed = True
    while changed:
        changed = False
        for pc in sorted(code, reverse=True):
            i = code[pc]
            out = 0
            for s in i.succ:
                out |= ALL if s == UNKNOWN or s not in code else code[s].lin
            lin = i.use | (out & ~i.kill & ALL)
            if out != i.lout or lin != i.lin:
                i.lout = out
                i.lin = lin
                changed = True


def is_call(o):
    return o in (0xcd, 0xdd, 0xed, 0xfd) or (o & 0xc7) in (0xc4, 0xc7)


def mark_polls(code):
    """A poll point at every backward branch's target, WEIGHED by its loop:
    the instructions from the head to the branch, plus 15 for each CALL in
    them. The budget counts weights, so it is roughly 8080 instructions
    whatever the code's shape - a spin-wait polls every few instructions and
    a main loop every few hundred, and a count of POLLS sized in one ran a
    hundred times too long in the other (ARCADE-SPEC 5.2, MEASURED)."""
    pcs = sorted(code)
    for i in code.values():
        o = i.op
        if o in (0xc3, 0xcb) or (o & 0xc7) == 0xc2:
            if i.imm <= i.pc:
                head = code[i.imm]
                head.poll = True
                span = [p for p in pcs if i.imm <= p <= i.pc]
                w = len(span) + 15 * sum(1 for p in span if is_call(code[p].op))
                head.weight = max(head.weight, min(127, w))
        if o == 0x76:
            i.poll = True


# SPIN LOOPS (ARCADE-SPEC 3.7, the plan's idle rule): a loop that loads A
# afresh at its head, touches nothing but A and the flags after - no carry
# read, so every pass is the same pass - stores nothing, and leaves only by
# its own backward branch can end only when an INTERRUPT changes what it
# reads. Its backward branch becomes a HLT that resumes at the head: the host
# then sleeps to the next interrupt instead of the 8088 running the wait at
# full speed, which on Lunar Rescue was 63% of the machine and long runs
# that dropped frames (MEASURED, MartyPC's 5150). The branch's PC -> the head.
SPINS = {}
SPIN_HEAD = {0x3a, 0x0a, 0x1a, 0xdb, 0x3e} | {0x78 + r for r in range(8) if r != 6} | {0x7e}
SPIN_BODY = (SPIN_HEAD | set(range(0x80, 0x88)) | set(range(0x90, 0x98))
             | set(range(0xa0, 0xc0)) | {0xc6, 0xd6, 0xe6, 0xee, 0xf6, 0xfe}
             | {0x07, 0x0f, 0x2f, 0x00})


def find_spins(code):
    out = {}
    for i in code.values():
        o = i.op
        if not ((o & 0xc7) == 0xc2 or o in (0xc3, 0xcb)) or i.imm > i.pc:
            continue
        head = code.get(i.imm)
        if head is None or head.op not in SPIN_HEAD:
            continue
        pc, ok = i.imm, True
        while pc < i.pc:
            j = code.get(pc)
            if j is None or not (j.op in SPIN_BODY or (j.op == 0xd3 and j.imm == 6)):
                ok = False
                break
            pc += j.n
        if ok and pc == i.pc:
            out[i.pc] = i.imm
    return out


def guard(live, body):
    return (['lahf'] + body + ['sahf']) if live else body


MARK = ['and si, 0xFF1F', 'cmp byte [es:si+AUX_FLAGS], 0', 'jne .d',
        'mov byte [es:si+AUX_FLAGS], 1', 'mov [es:di], si', 'lea di, [di+2]']


def vstore(hreg, addr, src, live):
    """A store through BX or SI (addr) of src - a register, or 'byte 0xNN'.

    RAM (H below 24h) is one MOV, the branch taken. VRAM compares first: a
    store that changes nothing is not made - 79% of them in play (MEASURED,
    tools/arcsim.py) - and one that does puts its CELL's key on the write
    list the first time only. The key is the address AND FF1Fh, the cell's
    band and column, and the offset of the cell's own flag byte in aux, so
    the dedupe is one compare and the host draws exactly the list
    (ARCADE-SPEC 4.1). ES:DI is the list's next free word. Everything but
    the MOV touches FLAGS, so it is guarded where they are live."""
    body = ['cmp %s, 0x24' % hreg, 'jb .r',
            'cmp [%s], %s' % (addr, src), 'je .d',
            'mov [%s], %s' % (addr, src)]
    if addr == 'bx':
        body.append('mov si, bx')
    body += MARK + ['jmp short .d', '.r:', 'mov [%s], %s' % (addr, src), '.d:']
    return guard(live, body)


def vmark(live):
    """INR M / DCR M: the byte changed; mark its cell if it is VRAM."""
    return guard(live, ['cmp bh, 0x24', 'jb .d', 'mov si, bx'] + MARK + ['.d:'])


def vconst_store(a, src_reg):
    """STA nn: decided by the assembler, nothing at run time for RAM."""
    if not (VRAM_LO <= a < VRAM_HI):
        return ['mov [0x%04X], %s' % (a, src_reg)]
    k = a & 0xFF1F
    return ['cmp [0x%04X], %s' % (a, src_reg), 'je .d', 'mov [0x%04X], %s' % (a, src_reg),
            'cmp byte [es:0x%04X+AUX_FLAGS], 0' % k, 'jne .d',
            'mov byte [es:0x%04X+AUX_FLAGS], 1' % k,
            'mov word [es:di], 0x%04X' % k, 'lea di, [di+2]', '.d:']


def vconst(a):
    """SHLD nn's two bytes, when they are VRAM: marked, changed or not."""
    if not (VRAM_LO <= a < VRAM_HI):
        return []
    k = a & 0xFF1F
    return ['cmp byte [es:0x%04X+AUX_FLAGS], 0' % k, 'jne .q%04X' % k,
            'mov byte [es:0x%04X+AUX_FLAGS], 1' % k,
            'mov word [es:di], 0x%04X' % k, 'lea di, [di+2]', '.q%04X:' % k]


def ret_seq():
    return ['pop si', 'lahf', 'shl si, 1', 'sahf', 'jmp word [es:si]']


def call_seq(ret, target, code):
    return ['mov si, 0x%04X' % ret, 'push si', jmp_to(target, code)]


def jmp_to(target, code):
    return 'jmp L%04X' % target if target in code else 'jmp arc_miss_%04X' % target


def translate(i, code, longjcc):
    o = i.op
    out = i.lout
    L = []
    if 0x40 <= o < 0x80 and o != 0x76:
        d, s = (o >> 3) & 7, o & 7
        if d == 6:
            L = vstore('bh', 'bx', R8[s], out)
        elif s == 6:
            L = ['mov %s, [bx]' % R8[d]]
        elif d != s:
            L = ['mov %s, %s' % (R8[d], R8[s])]
        return L
    if 0x80 <= o < 0xc0:
        s = o & 7
        src = 'byte [bx]' if s == 6 else R8[s]
        return ['%s al, %s' % (ALU[(o >> 3) & 7], src)]
    if (o & 0xc7) == 0xc6:
        return ['%s al, 0x%02X' % (ALU[(o >> 3) & 7], i.imm)]
    if o in (0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38):
        return []
    if (o & 0xcf) == 0x01:
        return ['mov %s, 0x%04X' % (RP[o >> 4], i.imm)]
    if (o & 0xcf) in (0x03, 0x0b):
        r = RP[o >> 4]
        inc = 'inc' if (o & 0xcf) == 0x03 else 'dec'
        if r == 'bx':
            return ['lea bx, [bx%s1]' % ('+' if inc == 'inc' else '-')]
        return guard(out, ['%s %s' % (inc, r)])
    if (o & 0xcf) == 0x09:
        r = RP[o >> 4]
        if out & Z:
            return ['lahf', 'shr ah, 1', 'add bx, %s' % r, 'rcl ah, 1', 'sahf']
        return ['add bx, %s' % r]
    if (o & 0xc7) in (0x04, 0x05):
        r = (o >> 3) & 7
        inc = 'inc' if (o & 7) == 4 else 'dec'
        if r == 6:
            return ['%s byte [bx]' % inc] + vmark(out)
        return ['%s %s' % (inc, R8[r])]
    if (o & 0xc7) == 0x06:
        r = (o >> 3) & 7
        if r == 6:
            return vstore('bh', 'bx', 'byte 0x%02X' % i.imm, out)
        return ['mov %s, 0x%02X' % (R8[r], i.imm)]
    if o == 0x02:
        return ['mov si, cx'] + vstore('ch', 'si', 'al', out)
    if o == 0x12:
        return ['mov si, dx'] + vstore('dh', 'si', 'al', out)
    if o == 0x0a:
        return ['mov si, cx', 'mov al, [si]']
    if o == 0x1a:
        return ['mov si, dx', 'mov al, [si]']
    if o == 0x22:
        return ['mov [0x%04X], bx' % i.imm] + guard(out, vconst(i.imm) + vconst(i.imm + 1))
    if o == 0x2a:
        return ['mov bx, [0x%04X]' % i.imm]
    if o == 0x32:
        return guard(out, vconst_store(i.imm, 'al')) if VRAM_LO <= i.imm < VRAM_HI \
            else vconst_store(i.imm, 'al')
    if o == 0x3a:
        return ['mov al, [0x%04X]' % i.imm]
    if o == 0x07:
        return ['rol al, 1']
    if o == 0x0f:
        return ['ror al, 1']
    if o == 0x17:
        return ['rcl al, 1']
    if o == 0x1f:
        return ['rcr al, 1']
    if o == 0x27:
        return ['daa']
    if o == 0x2f:
        return ['not al']
    if o == 0x37:
        return ['stc']
    if o == 0x3f:
        return ['cmc']
    if o == 0x76:
        return ['mov si, 0x%04X' % (i.pc + 1), 'jmp arc_halt']
    if (o & 0xc7) == 0xc0:
        return ['j%s .r' % INV[CC[(o >> 3) & 7]]] + ret_seq() + ['.r:']
    if (o & 0xcf) == 0xc1:
        p = (o >> 4) & 3
        if p == 3:
            return ['pop ax', 'xchg al, ah', 'sahf']
        return ['pop %s' % RP[p]]
    if (o & 0xcf) == 0xc5:
        p = (o >> 4) & 3
        if p == 3:
            return ['lahf', 'xchg al, ah', 'push ax', 'mov al, ah']
        return ['push %s' % RP[p]]
    if i.pc in SPINS:                       # a spin loop's branch: HLT
        halt = ['mov si, 0x%04X' % SPINS[i.pc], 'jmp arc_halt']
        if o in (0xc3, 0xcb):
            return halt
        return ['j%s .n' % INV[CC[(o >> 3) & 7]]] + halt + ['.n:']
    if (o & 0xc7) == 0xc2:
        cc = CC[(o >> 3) & 7]
        if i.pc in longjcc or i.imm not in code:
            return ['j%s .j' % INV[cc], jmp_to(i.imm, code), '.j:']
        return ['j%s L%04X' % (cc, i.imm)]
    if o in (0xc3, 0xcb):
        return [jmp_to(i.imm, code)]
    if (o & 0xc7) == 0xc4:
        cc = CC[(o >> 3) & 7]
        return ['j%s .k' % INV[cc]] + call_seq(i.pc + 3, i.imm, code) + ['.k:']
    if o in (0xcd, 0xdd, 0xed, 0xfd):
        return call_seq(i.pc + 3, i.imm, code)
    if (o & 0xc7) == 0xc7:
        return call_seq(i.pc + 1, o & 0x38, code)
    if o in (0xc9, 0xd9):
        return ret_seq()
    if o == 0xd3:
        if i.imm not in OUT_PORTS:
            raise SystemExit('arcaot: OUT %02X at %04X: no such port' % (i.imm, i.pc))
        body = list(OUT_PORTS[i.imm])
        if out and any(x.startswith('and') for x in body):
            return ['pushf'] + body + ['popf']
        return body
    if o == 0xdb:
        if i.imm not in IN_PORTS:
            raise SystemExit('arcaot: IN %02X at %04X: no such port' % (i.imm, i.pc))
        body = list(IN_PORTS[i.imm])
        if i.imm in FLAGSAFE_IN or not out:
            return body
        return ['pushf'] + body + ['popf']
    if o == 0xe3:
        return ['mov si, sp', 'xchg bx, [si]']
    if o == 0xe9:
        return (['lahf', 'sub bp, byte 16', 'jg .p', 'call arc_poll_ah',
                 'dw 0x%04X' % i.pc, '.p:', 'mov si, bx', 'shl si, 1', 'sahf',
                 'jmp word [es:si]'])
    if o == 0xeb:
        return ['xchg bx, dx']
    if o == 0xf3:
        return ['mov byte [ST_INTE], 0']
    if o == 0xfb:                   # EI: and if the host has an interrupt
        return ['mov byte [ST_INTE], 1'] + guard(out, [   # waiting, end the run
            'cmp byte [ST_PEND], 0', 'je .e', 'mov bp, 1',          # at the next poll
            'mov byte [ST_EIX], 1', '.e:'])
    if o == 0xf9:
        return ['mov sp, bx']
    raise SystemExit('arcaot: no translation for %02X at %04X' % (o, i.pc))


PROBES = []


# --- LOOP IDIOMS (ARCADE-SPEC 3.6) ---------------------------------------------
# A loop whose whole body is one of these shapes, ending in DCR B / JNZ to its
# own head, is run as ONE 8086 loop with no poll inside: B bounds it at 256
# turns. Each leaves exactly the registers, memory and FLAGS the instruction-
# by-instruction translation would - the final DCR B's S Z P with the CY the
# body last set - so the liveness pass, tests/arclock.py and the rest of the
# code cannot tell. The shapes are matched by their BYTES wherever they occur,
# never by address. MEASURED (tools/arcsim.py, Space Invaders in play): the
# copy is 18% of the interrupt handlers' instructions; the count is a third
# of every instruction the board runs.
def _jnz_to(rom, at, target):
    return rom[at] == 0xC2 and rom[at + 1] | (rom[at + 2] << 8) == target


def idiom(rom, pc):
    """(kind, length, exit) when a loop idiom starts at pc, else None."""
    b = rom[pc:pc + 11]
    # LDAX D / MOV M,A / INX H / INX D / DCR B / JNZ pc - a block copy
    if b[:5] == bytes([0x1A, 0x77, 0x23, 0x13, 0x05]) and _jnz_to(rom, pc + 5, pc):
        return ('copy', 8, None)
    # MOV A,M / ANA A / JZ pc+6 / INR C / INX H / DCR B / JNZ pc - C += the
    # bytes that are not zero
    if (b[:3] == bytes([0x7E, 0xA7, 0xCA]) and rom[pc + 3] | (rom[pc + 4] << 8) == pc + 6
            and b[5:8] == bytes([0x0C, 0x23, 0x05]) and _jnz_to(rom, pc + 8, pc)):
        return ('count', 11, None)
    # MOV A,M / ANA A / JNZ x / INX H / DCR B / JNZ pc - find the first that is
    if (b[:3] == bytes([0x7E, 0xA7, 0xC2]) and b[5:7] == bytes([0x23, 0x05])
            and _jnz_to(rom, pc + 7, pc)):
        return ('scan', 10, rom[pc + 3] | (rom[pc + 4] << 8))
    return None


def idiom_code(kind, pc, n, exit_, code, live):
    end = pc + n
    if kind == 'copy':
        # CY survives the body; the loop's own CMP for VRAM does not, so
        # the entry flags ride in AH and the last DCR's are made from them
        # all of it below VRAM (and not wrapping): REP MOVSB, ES = the view
        # for the length of it; else byte by byte with each VRAM cell's key.
        # The entry CY rides the stack; the last DCR B's flags are made from it
        return ['pushf', 'mov si, dx', 'push cx',
                'mov al, ch', 'sub al, 1', 'mov al, 0', 'adc al, 0',  # B=0: 256
                'mov cl, ch', 'mov ch, al', 'mov ax, bx', 'add ax, cx',
                'jc .slow', 'cmp ax, 0x2400', 'ja .slow',
                'push es', 'push di', 'mov di, ds', 'mov es, di', 'mov di, bx',
                'cld', 'rep movsb', 'mov bx, di', 'pop di', 'pop es', 'pop cx',
                'mov al, [si-1]', 'jmp .done',
                '.slow:', 'pop cx',
                '.i:', 'mov al, [si]', 'cmp bh, 0x24', 'jb .r',
                'cmp [bx], al', 'je .n', 'mov [bx], al', 'push si', 'mov si, bx',
                'and si, 0xFF1F', 'cmp byte [es:si+AUX_FLAGS], 0', 'jne .m',
                'mov byte [es:si+AUX_FLAGS], 1', 'mov [es:di], si', 'lea di, [di+2]',
                '.m:', 'pop si', 'jmp short .n', '.r:', 'mov [bx], al',
                '.n:', 'inc bx', 'inc si', 'dec ch', 'jnz .i',
                '.done:', 'mov ch, 0', 'mov dx, si', 'popf', 'lahf',
                'and ah, 0x01', 'or ah, 0x44', 'sahf',
                jmp_to(end, code)]
    if kind == 'count':
        return ['.i:', 'mov al, [bx]', 'and al, al', 'jz .z', 'inc cl',
                '.z:', 'inc bx', 'dec ch', 'jnz .i', jmp_to(end, code)]
    if kind == 'scan':
        return ['.i:', 'mov al, [bx]', 'and al, al', 'jnz .f', 'inc bx',
                'dec ch', 'jnz .i', jmp_to(end, code), '.f:', jmp_to(exit_, code)]
    raise SystemExit(kind)


def find_idioms(rom, code):
    """The idioms whose every instruction the walk decoded where we expect,
    and whose inside nothing else jumps into."""
    found = {}
    targets = {}
    for i in code.values():
        for s in i.succ:
            if s != UNKNOWN:
                targets.setdefault(s, set()).add(i.pc)
    for pc in code:
        r = idiom(rom, pc)
        if not r:
            continue
        kind, n, ex = r
        inner = set(range(pc + 1, pc + n))
        ok = all((t not in inner) or (srcs <= set(range(pc, pc + n)))
                 for t, srcs in targets.items())
        if ok:
            found[pc] = r
    return found


IDIOMS = {}


def emit(code, longjcc, romname):
    out = ['; GENERATED by apps/arcade/arcaot.py from %s - never committed' % romname,
           'cpu 8086', 'bits 16', 'org 0', '%include "arcdefs.inc"', '',
           '%include "arcrt.inc"', '']
    misses = set()
    for pc in sorted(code):
        i = code[pc]
        out.append('L%04X:' % pc)
        if pc in PROBES:
            out += ['    lahf', '    inc word [V_PROBE+%d]' % (2 * PROBES.index(pc)), '    sahf']
        if i.poll:
            dec = '    dec bp' if i.weight == 1 else '    sub bp, byte %d' % i.weight
            if i.lin:
                out += ['    lahf', dec, '    jg .c', '    call arc_poll_ah',
                        '    dw 0x%04X' % pc, '.c:', '    sahf']
            else:
                out += [dec, '    jg .c', '    call arc_poll',
                        '    dw 0x%04X' % pc, '.c:']
        if pc in IDIOMS:
            kind, n, ex = IDIOMS[pc]
            body = idiom_code(kind, pc, n, ex, code, i.lout)
        else:
            body = translate(i, code, longjcc)
        for line in body:
            m = re.search(r'arc_miss_([0-9A-F]{4})', line)
            if m:
                misses.add(int(m.group(1), 16))
            out.append(('    ' + line) if not line.endswith(':') else line)
        nxt = pc + i.n
        if nxt not in code and len(i.succ) and nxt in i.succ:
            out.append('    jmp arc_miss_%04X' % nxt)
            misses.add(nxt)
    for pc in sorted(misses):
        out += ['arc_miss_%04X:' % pc, '    mov si, 0x%04X' % (pc * 2), '    jmp arc_miss']
    out += ['arc_code_end:', '']
    return '\n'.join(out) + '\n'


def assemble(asm, nasm, incdirs, keep=None):
    """Assemble asm; with keep, return (rc, err, binary, {label: offset})."""
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, 'a.asm')
        binf = os.path.join(td, 'a.bin')
        mapf = os.path.join(td, 'a.map')
        open(src, 'w').write(('[map symbols %s]\n' % mapf if keep else '') + asm)
        cmd = [nasm, '-f', 'bin', '-w+error', '-o', binf, src]
        for d in incdirs:
            cmd[1:1] = ['-I', d + '/']
        r = subprocess.run(cmd, capture_output=True, text=True)
        if not keep:
            return r.returncode, r.stderr
        if r.returncode:
            return r.returncode, r.stderr, None, None
        syms = {}
        for ln in open(mapf):
            f = ln.split()
            if len(f) == 3 and re.match(r'^[0-9A-Fa-f]+$', f[0]) and \
                    re.match(r'^[0-9A-Fa-f]+$', f[1]):
                syms[f[2]] = int(f[1], 16)
        return 0, '', open(binf, 'rb').read(), syms


def settle(emit_fn, nasm, incdirs, name):
    """Assemble emit_fn(longjcc) until no short branch is out of range;
    an out-of-range one is re-emitted inverted around a near JMP.
    Returns (asm, binary, symbols, longjcc)."""
    longjcc = set()
    for _ in range(20):
        asm = emit_fn(longjcc)
        rc, err = assemble(asm, nasm, incdirs)
        if rc == 0:
            rc, err, binary, syms = assemble(asm, nasm, incdirs, keep=True)
            return asm, binary, syms, longjcc
        lines = asm.split('\n')
        bad = set()
        for m in re.finditer(r'a\.asm:(\d+): error: short jump is out of range', err):
            k = int(m.group(1)) - 1
            while k >= 0 and not re.match(r'L[0-9A-F]{4}:$', lines[k]):
                k -= 1
            bad.add(int(lines[k][1:5], 16))
        if not bad:
            sys.stderr.write(err)
            raise SystemExit('%s: the translation does not assemble' % name)
        longjcc |= bad
    raise SystemExit('%s: the short jumps never settled' % name)


def finish(binary, syms, code_pcs, out_bin, out_aux, name, aux_size=0x8000,
           table_pcs=0x4000):
    """Write the code part and aux's PC table (ARCADE-SPEC 2.2): word n is
    the offset of the translation of PC n, or arc_miss's."""
    if len(binary) > 0x10000:
        raise SystemExit('%s: the code part is %d bytes, over a segment' % (name, len(binary)))
    miss = syms['arc_miss']
    table = bytearray()
    for pc in range(table_pcs):
        off = syms.get('L%04X' % pc, miss) if pc in code_pcs else miss
        table += bytes([off & 255, off >> 8])
    open(out_bin, 'wb').write(binary)
    open(out_aux, 'wb').write(bytes(table) + bytes(max(0, aux_size - len(table))))
    return len(binary)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('seeds')
    ap.add_argument('out', help='the NASM source, for reading')
    ap.add_argument('bin', help='the code part')
    ap.add_argument('aux', help="aux's PC table, 32KB")
    ap.add_argument('--nasm', default='nasm')
    ap.add_argument('--aux-kb', type=int, default=48,
                    help="aux's size: the table, then the list and the flags")
    ap.add_argument('--no-speculate', action='store_true',
                    help='no tentative walks from data words')
    ap.add_argument('--no-spins', action='store_true',
                    help='run spin loops as loops, not as a HLT')
    ap.add_argument('--no-idioms', action='store_true',
                    help='translate every loop instruction by instruction')
    ap.add_argument('--probe', type=lambda x: int(x, 16), action='append', default=[],
                    help='count arrivals at this 8080 PC in the word at V_PROBE '
                         '(an instrument build only: tests/arcspeed.py)')
    a = ap.parse_args()
    rom = board_rom(open(a.rom, 'rb').read(), a.rom)
    seeds = []
    for line in open(a.seeds):
        line = line.split('#')[0].strip()
        if line:
            seeds.append(int(line, 16))
    PROBES[:] = a.probe
    code = walk(rom, seeds)
    spec = 0 if a.no_speculate else speculate(rom, code)
    liveness(code)
    mark_polls(code)
    SPINS.clear()
    if not a.no_spins:
        SPINS.update(find_spins(code))
    IDIOMS.clear()
    if not a.no_idioms:
        IDIOMS.update(find_idioms(rom, code))
    romname = os.path.abspath(a.rom)
    asm, binary, syms, longjcc = settle(lambda lj: emit(code, lj, romname),
                                        a.nasm, [HERE], 'arcaot')
    open(a.out, 'w').write(asm)
    size = finish(binary, syms, code, a.bin, a.aux, 'arcaot', a.aux_kb * 1024,
                  TABLE_PCS)
    polls = sum(1 for i in code.values() if i.poll)
    print('arcaot: %d instructions (%d of them speculative), %d poll points, '
          '%d long branches, %d bytes of code, spins %s, idioms %s -> %s'
          % (len(code), spec, polls, len(longjcc), size,
             ' '.join('%04X' % p for p in sorted(SPINS.values())) or 'none',
             ' '.join('%s@%04X' % (v[0], k) for k, v in sorted(IDIOMS.items())) or 'none',
             a.out))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""apps/m6502/m65aot.py - a 6502 ROM translated to 8086 ahead of time
(docs/plans/FAST6502-PLAN.md wave 3; APPLE2-SPEC 4.7)

    python3 apps/m6502/m65aot.py --machine apple2 --rom APPLE2.ROM \\
        --seeds a2rom.seeds -o build/apple2/a2aot.asm --table build/apple2/a2aotpc.bin

Every ROM PC the seeds name (tools/a2trace.py's executed PCs), and every PC a
static walk from them reaches, becomes a label and a few 8086 instructions in
ONE code segment; a table of words, one per ROM PC, maps a PC to its
translation (0 = none). The fast core (m65gen.py) far-jumps here when a
control transfer lands on a ROM PC the table names; this code jumps within
itself while the 6502 stays in translated ROM, and far-jumps back to the core
(`xr_resume`, `xr_slice`, fixed offsets there) when it leaves.

THE CONVENTIONS ARE THE CORE'S (m65gen.py's header): A = AL, N Z C in AH in
lahf's layout, X = CL, Y = CH, S = DI as $0100+S, BP the countdown, DS the
guest image, the frame on the stack with SP = F. SI is NOT the PC here - the
PC is where the code is - and is set only on the way out.

TIME: each instruction subtracts its own cycles before its body, so every
translated PC is an entry point; the countdown is TESTED at every control
transfer, where an exhausted run leaves with SI = the PC it would run next.
A run may therefore finish a straight stretch of ROM past its budget: the
cycles are all counted (`ran = asked - cnt` stays exact) and on the Apple II,
which has no interrupt source, nothing can see the difference (APPLE2-SPEC
4.7). The clock a soft switch reads is exact, as in the core.

FLAGS: N and Z are computed only where something may read them - a later
instruction before they are written again, a branch, an exit. C and V are
always computed.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from m65gen import OPS, PENALTY, READS, RMW, STORES, REG, APPLE2, Gen  # noqa: E402
from m65gen import XR_RESUME, XR_SLICE, C64, MAXBLK, F_EXTRA            # noqa: E402

LEN = {"imp": 1, "acc": 1, "imm": 2, "zp": 2, "zpx": 2, "zpy": 2, "rel": 2,
       "abs": 3, "absx": 3, "absy": 3, "ind": 3, "indx": 2, "indy": 2}
WRITES_NZ = {"LDA", "LDX", "LDY", "ORA", "AND", "EOR", "ADC", "SBC", "CMP",
             "CPX", "CPY", "BIT", "ASL", "LSR", "ROL", "ROR", "INC", "DEC",
             "TAX", "TAY", "TXA", "TYA", "TSX", "PLA", "INX", "INY", "DEX",
             "DEY", "PLP", "RTI"}
TRANSFERS = {"JMP", "JSR", "RTS", "RTI", "BRK"}
# what this translator leaves to the core: it exits there and the core runs it
LEAVE = {"BRK", "SED"}
                            # PLP and RTI leave when they set D, and the core's
                            # ct_rom will not enter with it set, so ADC and SBC
                            # need no test)
# ...and these are the core's only while the interrupt byte is up (its own
# handlers test it first): a run of the accurate core then takes the one-
# instruction delay. Otherwise they are a flag and a pull - and Microsoft
# BASIC's divide loop runs PHP/PLP once a bit
IRQ_GATED = {"CLI", "SEI", "PLP", "RTI"}


class Rom:
    """the bytes translated: one range (the Apple II's ROM, a test range of
    RAM) or several (the C64's BASIC and KERNAL, with I/O between them)"""

    def __init__(self, data, base, more=()):
        self.data, self.base = data, base
        self.segs = [(base, data)] + list(more)

    def has(self, a):
        return any(b <= a < b + len(d) for b, d in self.segs)

    def b(self, a):
        for b, d in self.segs:
            if b <= a < b + len(d):
                return d[a - b]
        raise IndexError(a)

    def w(self, a):
        return self.b(a) | (self.b(a + 1) << 8)


def decode(rom, pc):
    """(mnemonic, mode, cycles, operand, length) or None for an opcode the
    core hands back, or an instruction running off the ROM"""
    op = rom.b(pc)
    if op not in OPS:
        return None
    mn, mode, cyc = OPS[op]
    n = LEN[mode]
    if not rom.has(pc + n - 1):
        return None
    opnd = None
    if n == 2:
        opnd = rom.b(pc + 1)
    elif n == 3:
        opnd = rom.w(pc + 1)
    return mn, mode, cyc, opnd, n


def walk(rom, seeds):
    """every PC reachable from the seeds by fall-through, branch, JMP and JSR
    inside the ROM - safe to over-reach: a PC's translation is that PC's
    instruction whatever reached it, and a PC nothing jumps to costs space"""
    todo, seen = [s for s in seeds if rom.has(s)], set()
    while todo:
        pc = todo.pop()
        if pc in seen or not rom.has(pc):
            continue
        d = decode(rom, pc)
        if d is None:
            continue
        seen.add(pc)
        mn, mode, cyc, opnd, n = d
        nxt = pc + n
        if mn in ("JMP", "RTS", "RTI", "BRK"):
            if mn == "JMP" and mode == "abs":
                todo.append(opnd)
            continue
        if mode == "rel":
            todo.append((nxt + (opnd - 256 if opnd & 0x80 else opnd)) & 0xFFFF)
        if mn == "JSR":
            todo.append(opnd)
        todo.append(nxt)
    return seen


class Aot:
    def __init__(self, prof, rom, pcs, mine=None, bank=None, dyn=()):
        """PCS is every PC translated; MINE the ones this segment holds (the
        C64's BASIC and KERNAL are a segment each, BANK 0 and 1)"""
        self.p, self.rom, self.pcs = prof, rom, pcs
        self.mine = pcs if mine is None else mine
        self.dyn = set(dyn)             # (the trace's RTS / JMP (ind) landings)
        self.bank = bank
        self.out = []
        self.n = 0
        self.ctl = False
        self.g = Gen(prof)      # the memory helpers come from the core's own
        self.g.out = self.out   # ...written into THIS file, in place
        self.c64 = prof["name"] == "c64"
        self.exact = bool(prof.get("exact"))
        self.fa = prof["aot_frame"]     # the frame's translation words
        self.cur = None                 # the PC being translated
        self.ctls = []                  # (--range: the gate's ADC sites)
        self.tail = []

    def L(self, *l):
        self.out.extend(l)

    def lab(self):
        self.n += 1
        return ".a%d" % self.n

    def PV(self):
        return "0x%04X" % self.p["pv"]

    # --- leaving ---------------------------------------------------------
    def to(self, target, check=True):
        """control goes to TARGET: a jump within this code if it is
        translated, else out to the core at that PC; CHECK the countdown
        first (the instruction's cycles are already taken)"""
        if check and not self.exact:
            x = self.lab()
            self.L("    jle %s" % x)
            self.go(target)
            self.L("%s: mov si, 0x%04X" % (x, target), "    jmp xa_slice")
        else:
            self.go(target)

    def go(self, target):
        if target in self.mine:
            self.L("    jmp p_%04X" % target)
        elif target in self.pcs:
            # THE OTHER BANK'S SEGMENT, through the table (a_far). The KERNAL
            # into BASIC only while BASIC is mapped in; the other way needs no
            # test - BASIC in means the KERNAL in (c64io.c's map)
            k = self.lab()
            if self.cur >= 0xE000:
                self.L("    cmp byte [SCR+0x36], 1", "    jne %s" % k)
            self.L("    mov bx, 0x%04X" % target, "    jmp a_far",
                   "%s: mov si, 0x%04X" % (k, target), "    jmp xa_resume")
        else:
            self.L("    mov si, 0x%04X" % target, "    jmp xa_resume")

    def far(self, fo):
        """BX = an offset in the segment the frame's word FO names: there"""
        self.L("    mov dx, bx", "    mov bx, sp",
               "    push word [ss:bx+%d]" % fo, "    push dx",
               "    xor dl, dl", "    retf")

    def frame_of(self, bank):
        return self.fa + (6 if bank else 0)     # BASIC's code, the KERNAL's

    def bail(self, cyc):
        """a label that leaves to the core at THIS instruction, its cycles
        given back: what the core hands back is decided after the address"""
        b = self.lab()
        self.tail.append(["%s: add bp, %d" % (b, self.pre[self.cur]),
                          "    mov si, 0x%04X" % self.cur, "    jmp xa_resume"])
        return b

    def known_rom(self, a):
        """the C64: a ROM byte this code may fold - its own bank, which is in
        while this runs, and the KERNAL from BASIC"""
        if self.cur is None or not self.rom.has(a):
            return False
        if 0xA000 <= self.cur < 0xC000:
            return 0xA000 <= a < 0xC000 or a >= 0xE000
        if self.cur >= 0xE000:
            return a >= 0xE000
        return False

    # --- flags -----------------------------------------------------------
    def nz(self, r, live):
        if live:
            self.L("    sahf", "    inc %s" % r, "    dec %s" % r, "    lahf")

    def v_from_of(self):
        # V from the 8086's OF, inline (where it is live: vliveness())
        v, k = self.lab(), self.lab()
        self.L("    jo %s" % v, "    and byte [%s], 0xBF" % self.PV(), "    jmp short %s" % k,
               "%s: or byte [%s], 0x40" % (v, self.PV()), "%s:" % k)

    # --- memory, with the address known when the mode allows -------------
    def ea(self, mode, opnd, penalty):
        L = self.L
        if mode == "zpx":
            L("    mov bl, 0x%02X" % opnd, "    add bl, cl", "    xor bh, bh")
        elif mode == "zpy":
            L("    mov bl, 0x%02X" % opnd, "    add bl, ch", "    xor bh, bh")
        elif mode in ("absx", "absy"):
            r = "cl" if mode == "absx" else "ch"
            L("    mov bx, 0x%04X" % opnd, "    add bl, %s" % r)
            if penalty:
                k = self.lab()
                L("    jnc %s" % k, "    inc bh", "    dec bp", "%s:" % k)
            else:
                L("    adc bh, 0")
        elif mode in ("indx", "indy"):
            if mode == "indx":
                L("    mov bl, 0x%02X" % opnd, "    add bl, cl", "    xor bh, bh")
                L("    mov dl, [bx]", "    inc bl", "    mov bh, [bx]",
                  "    mov bl, dl", "    xor dl, dl")
            elif opnd != 0xFF:
                L("    mov bx, [0x%04X]" % opnd)           # one word
            else:
                L("    mov bl, [0x00FF]", "    mov bh, [0x0000]")   # the wrap
            if mode == "indy":
                L("    add bl, ch")
                if penalty:
                    k = self.lab()
                    L("    jnc %s" % k, "    inc bh", "    dec bp", "%s:" % k)
                else:
                    L("    adc bh, 0")
        else:
            raise ValueError(mode)

    def span_ram(self, mode, opnd):
        """the indexed address cannot leave RAM below $C000 / the ROM"""
        if mode in ("absx", "absy") and opnd + 255 < 0xC000:
            return "ram"
        if mode in ("absx", "absy") and opnd >= 0xD000 and opnd + 255 <= 0xFFFF:
            return "rom"
        return None

    def read(self, mode, opnd, dst, penalty, cyc=0):
        """DST = the operand. The core's read path where the address is not
        known; otherwise the one instruction it comes to"""
        L = self.L
        if self.c64:
            return self.read_c64(mode, opnd, dst, penalty, cyc)
        if mode == "imm":
            L("    mov %s, 0x%02X" % (dst, opnd))
            return
        if mode == "zp":
            L("    mov %s, [0x%04X]" % (dst, opnd))
            return
        if mode == "abs":
            if opnd < 0xC000:
                L("    mov %s, [0x%04X]" % (dst, opnd))
            elif opnd >= 0xD000:
                L("    mov %s, [0x%04X]" % (dst, opnd))     # (the image's ROM)
            elif opnd >= 0xC200:
                L("    mov %s, 0xFF" % dst)                         # slot space
            else:
                self.g.later = []
                L("    mov bx, 0x%04X" % opnd)
                self.g.read("abs", dst, 0)
                for blk in self.g.later:
                    self.tail.append(blk)
            return
        self.ea(mode, opnd, penalty)
        if mode in ("zpx", "zpy") or self.span_ram(mode, opnd):
            L("    mov %s, [bx]" % dst)
            return
        # RAM inline, everything else through ONE shared helper (ra_any):
        # the core's ladder written once rather than at every site
        k, x = self.lab(), self.lab()
        L("    cmp bh, 0xC0", "    jae %s" % x, "    mov %s, [bx]" % dst, "%s:" % k)
        self.tail.append(["%s: call ra_any" % x]
                         + (["    mov %s, dh" % dst] if dst != "dh" else [])
                         + ["    jmp %s" % k])

    def read_c64(self, mode, opnd, dst, penalty, cyc):
        """the C64: RAM below $A000 and at $C000-$CFFF; above, the bank map,
        through the core's rd_hi (DL = 0: the cycles are already taken)"""
        L = self.L
        if mode == "imm":
            L("    mov %s, 0x%02X" % (dst, opnd))
            return
        if mode == "zp":
            L("    mov %s, [0x%04X]" % (dst, opnd))
            return
        if mode == "abs":
            if opnd < 0xA000 or 0xC000 <= opnd < 0xD000:
                L("    mov %s, [0x%04X]" % (dst, opnd))
            elif self.known_rom(opnd):
                L("    mov %s, 0x%02X" % (dst, self.rom.b(opnd)))
            else:
                L("    mov bx, 0x%04X" % opnd, "    call rd_hi")
                if dst != "dh":
                    L("    mov %s, dh" % dst)
            return
        if mode == "indx":
            # a pointer in $FF/$00/$01 is the core's, as the core says
            b = self.bail(cyc)
            L("    mov bl, 0x%02X" % opnd, "    add bl, cl",
              "    cmp bl, 2", "    jb %s" % b, "    cmp bl, 0xFF", "    je %s" % b,
              "    xor bh, bh",
              "    mov dl, [bx]", "    inc bl", "    mov bh, [bx]",
              "    mov bl, dl", "    xor dl, dl")
        else:
            self.ea(mode, opnd, penalty)
        if mode in ("zpx", "zpy"):
            L("    mov %s, [bx]" % dst)
            return
        if mode in ("absx", "absy"):
            c0, c1 = self.rclass(opnd), self.rclass(min(opnd + 255, 0xFFFF))
            if c0 == c1 == "ram":
                L("    mov %s, [bx]" % dst)
                return
            if c0 == c1 and c0 in ("B", "K"):
                # A TABLE IN A BANK THAT IS MAPPED IN (this code runs from it,
                # or from BASIC with the KERNAL): through its segment, inline
                L("    mov si, sp", "    push es",
                  "    mov es, [ss:si+%d]" % (F_EXTRA + (2 if c0 == "B" else 6)),
                  "    mov %s, [es:bx]" % dst, "    pop es")
                return
        k, x = self.lab(), self.lab()
        L("    cmp bh, 0xA0", "    jae %s" % x, "    mov %s, [bx]" % dst, "%s:" % k)
        self.tail.append(["%s: call %s" % (x, "rd_ctx" if self.bank is not None else "rd_hi")]
                         + (["    mov %s, dh" % dst] if dst != "dh" else [])
                         + ["    jmp %s" % k])

    def rclass(self, a):
        """the C64: what a read of A is while this code runs - RAM, a bank
        known to be mapped in ("B", "K"), or the map's to say (None)"""
        if a < 0xA000 or 0xC000 <= a < 0xD000:
            return "ram"
        if self.bank is None:
            return None
        if 0xA000 <= a < 0xC000 and self.bank == 0:
            return "B"
        if a >= 0xE000:
            return "K"
        return None

    def cut(self):
        """after a write that called out: c64_cut() leaves the countdown at
        -1 and the core ends the run after this instruction - so does this"""
        x = self.lab()
        self.tail.append(["%s: mov si, 0x%04X" % (x, self.nxt), "    jmp xa_slice"])
        self.L("    or bp, bp", "    jle %s" % x)

    def mark_const(self, a):
        """c64_mark for an address known here: the page's dirty bit and "the
        core wrote" inline, and the call only inside the watch window (the
        KERNAL's keyboard wait stores to $0292 on every pass)"""
        from m65gen import S_DIRTY, S_ANY, S_WATLO, S_WATHI
        pg, k = a >> 8, self.lab()
        if self.p.get("jit") and (0x04 <= pg < 0xA0 or 0xC0 <= pg < 0xD0):
            # A PAGE THE RECOMPILER MAY TRANSLATE (C64-SPEC 4.9.2): the
            # whole mark, whose CODE test this inline one does not make
            self.L("    mov bx, 0x%04X" % a, "    call c64_mark")
            return
        self.L("    or byte [SCR+0x%02X], 0x%02X" % (S_DIRTY + (pg >> 3), 0x80 >> (pg & 7)),
               "    mov byte [SCR+0x%02X], 1" % S_ANY,
               "    cmp word [SCR+0x%02X], 0x%04X" % (S_WATLO, a), "    ja %s" % k,
               "    cmp word [SCR+0x%02X], 0x%04X" % (S_WATHI, a), "    jb %s" % k,
               "    mov bx, 0x%04X" % a, "    call c64_mark",
               "%s:" % k)

    def write_c64(self, mode, opnd, src, cyc):
        """the C64: $00/$01 (the processor port) is the core's; RAM below
        $D000 with the damage mark; above it the core's wr_hi"""
        L = self.L
        if mode == "zp":
            L("    mov [0x%04X], %s" % (opnd, src))         # (>= 2: insn's)
            return
        if mode == "abs":
            if opnd < 0xD000:
                L("    mov [0x%04X], %s" % (opnd, src))
                self.mark_const(opnd)
            else:
                L("    mov bx, 0x%04X" % opnd)
                if src != "dh":
                    L("    mov dh, %s" % src)
                L("    call wr_hi")
                self.cut()
            return
        if mode in ("zpx", "zpy"):
            self.ea(mode, opnd, False)
            L("    cmp bl, 2", "    jb %s" % self.bail(cyc), "    mov [bx], %s" % src)
            return
        if mode == "indx":
            b = self.bail(cyc)
            L("    mov bl, 0x%02X" % opnd, "    add bl, cl",
              "    cmp bl, 2", "    jb %s" % b, "    cmp bl, 0xFF", "    je %s" % b,
              "    xor bh, bh",
              "    mov dl, [bx]", "    inc bl", "    mov bh, [bx]",
              "    mov bl, dl", "    xor dl, dl")
        else:
            self.ea(mode, opnd, False)
        if not (mode in ("absx", "absy") and opnd >= 2):
            L("    cmp bx, 2", "    jb %s" % self.bail(cyc))
        k, x = self.lab(), self.lab()
        if mode in ("absx", "absy") and opnd + 255 < 0xD000:
            L("    mov [bx], %s" % src, "    call c64_mark")
            return
        L("    cmp bh, 0xD0", "    jae %s" % x,
          "    mov [bx], %s" % src, "    call c64_mark", "%s:" % k)
        c = self.lab()
        self.tail.append(["%s:" % x] + (["    mov dh, %s" % src] if src != "dh" else [])
                         + ["    call wr_hi", "    or bp, bp", "    jg %s" % k,
                            "%s: mov si, 0x%04X" % (c, self.nxt), "    jmp xa_slice"])

    def write(self, mode, opnd, src, cyc=0):
        L = self.L
        if self.c64:
            return self.write_c64(mode, opnd, src, cyc)
        (a0, a1), (b0, b1) = self.p["video"]
        if mode == "zp":
            L("    mov [0x%04X], %s" % (opnd, src))
            return
        if mode == "abs":
            if opnd < 0xC000:
                L("    mov [0x%04X], %s" % (opnd, src))
                pg = opnd >> 8
                if a0 <= pg < a1 or b0 <= pg < b1:
                    L("    mov bx, 0x%04X" % opnd, "    call vid_mark")
            elif opnd < 0xC100:
                L("    mov bx, 0x%04X" % opnd)
                if src != "dh":
                    L("    mov dh, %s" % src)
                L("    xor dl, dl", "    call wr_io")
            # (and above $C0FF a write is dropped: nothing at all)
            return
        self.ea(mode, opnd, False)
        if mode in ("zpx", "zpy"):
            L("    mov [bx], %s" % src)
            return
        if src != "dh":
            L("    mov dh, %s" % src)
        L("    call wa_any")               # (the core's write path, once)

    def push(self, r):
        self.L("    mov [di], %s" % r, "    dec di", "    or di, 0x0100")

    def pop(self, r):
        self.L("    inc di", "    and di, 0x01FF", "    or di, 0x0100",
               "    mov %s, [di]" % r)

    def rts_to(self):
        """BX is the 6502's next PC (after RTS, or JMP through RAM), the
        countdown's flags just set: through the table if it is in the ROM,
        else out to the core"""
        if self.exact:
            return self.rts_to_exact()
        out, ex, res = self.lab(), self.lab(), self.lab()
        self.L("    jle %s" % out,
               "    cmp bx, 0x%04X" % self.rom.base, "    jb %s" % ex,
               "    mov si, bx",
               "    shl bx, 1",                 # (ES is the PC table: the
               "    mov bx, [es:bx+0x%04X]" % ((-self.rom.base * 2) & 0xFFFF),
               "    xor dl, dl",                # core's ct_rom put it there)
               "    test bx, bx", "    jz %s" % res,
               "    jmp bx",
               "%s: mov si, bx" % out, "    jmp xa_slice",
               "%s: mov si, bx" % ex,
               "%s: jmp xa_resume" % res)

    def rts_to_exact(self):
        """...on an exact profile: into the table only with more than MAXBLK
        left, since the target may be inside a block; and on the C64 only
        into a bank that is mapped in. ES is the table throughout"""
        ex, res = self.lab(), self.lab()
        L = self.L
        L("    cmp bp, %d" % MAXBLK, "    jle %s" % ex)
        if self.c64 and self.bank is not None:
            # A TRANSLATION RUNS ONLY WHILE ITS BANK IS MAPPED, and BASIC in
            # means the KERNAL in - so the one return that needs the map is
            # the KERNAL's into BASIC
            k = self.lab()
            L("    mov si, bx",
              "    sub bh, 0xA0", "    cmp bh, 0x20", "    jae %s" % k)
            if self.bank == 1:
                L("    cmp byte [SCR+0x36], 1", "    jne %s" % res)     # MAPA
            L("    shl bx, 1", "    mov bx, [es:bx]",
              "    test bx, bx", "    jz %s" % res)
            if self.bank == 0:
                L("    jmp bx")
            else:
                self.far(self.frame_of(0))
            L("%s: cmp bh, 0x40" % k, "    jb %s" % res,            # $C000-$DFFF
              "    cmp bh, 0x60", "    jae %s" % res,             # below $A000
              "    sub bh, 0x20",
              "    shl bx, 1", "    mov bx, [es:bx]",
              "    test bx, bx", "    jz %s" % res)
            if self.bank == 1:
                L("    jmp bx")
            else:
                self.far(self.frame_of(1))
            L("%s: mov si, bx" % ex,
              "%s: jmp xa_resume" % res)
            return
        else:
            lo = self.rom.base
            hi = lo + len(self.rom.data)
            L("    cmp bx, 0x%04X" % lo, "    jb %s" % ex,
              "    cmp bx, 0x%04X" % hi, "    jae %s" % ex,
              "    mov si, bx", "    sub bx, 0x%04X" % lo)
        L("    shl bx, 1",
          "    mov bx, [es:bx]",
          "    test bx, bx", "    jz %s" % res,
          "    jmp bx",
          "%s: mov si, bx" % ex,
          "%s: jmp xa_resume" % res)

    # --- one instruction -------------------------------------------------
    def insn(self, pc, live_nz):
        mn, mode, cyc, opnd, n = decode(self.rom, pc)
        L = self.L
        nxt = pc + n
        self.cur, self.nxt = pc, nxt
        self.L("p_%04X:" % pc)
        if os.environ.get("M65AOT_TRACE") == "all":
            # DEBUG ONLY: every translated PC into a 128-word ring at $C300
            # (the Apple II test image's slot space), its index at $C2FE
            L("    pushf", "    push bx", "    mov bx, [0xC2FE]",
              "    mov word [bx+0xC300], 0x%04X" % pc,
              "    add bx, 2", "    and bx, 0xFF", "    mov [0xC2FE], bx",
              "    pop bx", "    popf")
        if self.exact and pc in self.leaders:
            # THE BLOCK'S COST, tested before it runs: the core runs a block
            # the budget cannot cover, and ends the run where it would
            k = self.lab()
            L("    cmp bp, %d" % self.leaders[pc], "    jg %s" % k,
              "    mov si, 0x%04X" % pc, "    jmp xa_resume", "%s:" % k)
        if self.c64 and self.static_core(mn, mode, opnd):
            L("    mov si, 0x%04X" % pc, "    jmp xa_resume")
            return False
        if mn in LEAVE:
            L("    mov si, 0x%04X" % pc, "    jmp xa_resume")
            return False
        if mn in IRQ_GATED:
            from m65gen import S_IRQ
            k = self.lab()
            L("    cmp byte [SCR+0x%02X], 0" % S_IRQ, "    je %s" % k,
              "    mov si, 0x%04X" % pc, "    jmp xa_resume", "%s:" % k)
        if not self.c64 and mn in RMW and mode not in ("acc", "zp", "zpx") and \
                not (mode == "abs" and opnd < 0xC000) and \
                not (mode == "absx" and opnd + 255 < 0xC000):
            L("    mov si, 0x%04X" % pc, "    jmp xa_resume")   # I/O or ROM
            return False
        if pc in self.gstart:
            # ONE SUBTRACT A GROUP (blocks()'s neighbour, groups()): the whole
            # straight run's cycles, which an exit inside it gives back
            L("    sub bp, %d" % self.pre[pc])
        if mn in READS:
            dst = {"LDA": "al", "LDX": "cl", "LDY": "ch"}.get(mn, "dh")
            self.read(mode, opnd, dst, mode in PENALTY, cyc)
            src = dst
            if mn in ("LDA", "LDX", "LDY"):
                self.nz(dst, live_nz)
            elif mn in ("ORA", "AND", "EOR"):
                L("    %s al, %s" % ({"ORA": "or", "AND": "and", "EOR": "xor"}[mn], src))
                self.nz("al", live_nz)
            elif mn == "ADC":
                L("    sahf")
                if self.p.get("gate"):
                    # THE GATE'S HANDLE (the cputest harnesses' translated
                    # legs): every ADC's and CMP's 8086 opcode is listed in
                    # ctl_list, and the control breaks each - ADC becomes OR
                    self.ctls.append("..@ctl%d" % len(self.ctls))
                    L("%s:" % self.ctls[-1])
                L("    adc al, %s" % src, "    lahf")
                if self.vlive[pc]:
                    self.v_from_of()
            elif mn == "SBC":
                L("    sahf", "    cmc", "    sbb al, %s" % src, "    cmc", "    lahf")
                if self.vlive[pc]:
                    self.v_from_of()
            elif mn in ("CMP", "CPX", "CPY"):
                reg = {"CMP": "al", "CPX": "cl", "CPY": "ch"}[mn]
                if self.p.get("gate"):
                    # (the gate breaks a CMP into an ADC - every part of
                    # Dormann checks its results with CMP)
                    self.ctls.append("..@ctl%d" % len(self.ctls))
                    L("%s:" % self.ctls[-1])
                L("    cmp %s, %s" % (reg, src), "    cmc", "    lahf")
            elif mn == "BIT":
                nzl, n0 = self.lab(), self.lab()
                L("    and ah, 0x3F", "    test al, %s" % src, "    jnz %s" % nzl,
                  "    or ah, 0x40",
                  "%s: test %s, 0x80" % (nzl, src), "    jz %s" % n0,
                  "    or ah, 0x80",
                  "%s: mov dl, %s" % (n0, src), "    and dl, 0x40",
                  "    and byte [%s], 0xBF" % self.PV(),
                  "    or [%s], dl" % self.PV(), "    xor dl, dl")
        elif mn in STORES:
            self.write(mode, opnd, STORES[mn], cyc)
            self.chr_guard(mn, mode, opnd)
        elif mn in RMW:
            self.chr_guard(mn, mode, opnd)
            if mode == "acc":
                self.shift(mn, "al", live_nz)
            elif mode == "zp" and mn in ("INC", "DEC", "ASL", "LSR"):
                m = "byte [0x%04X]" % opnd
                if mn in ("INC", "DEC"):
                    L("    sahf", "    %s %s" % (mn.lower(), m), "    lahf")
                else:
                    L("    %s %s, 1" % ("shl" if mn == "ASL" else "shr", m), "    lahf")
            else:
                if mode == "zp":
                    L("    mov bx, 0x%04X" % opnd)
                elif mode == "abs":
                    L("    mov bx, 0x%04X" % opnd)
                else:
                    self.ea(mode, opnd, False)
                if self.c64 and mode == "zpx":
                    L("    cmp bl, 2", "    jb %s" % self.bail(cyc))
                elif self.c64 and mode == "absx" and not (2 <= opnd and opnd + 255 < 0xA000):
                    b = self.bail(cyc)
                    L("    cmp bh, 0xA0", "    jae %s" % b, "    cmp bx, 2", "    jb %s" % b)
                L("    mov dh, [bx]")
                self.shift(mn, "dh", True)
                L("    mov [bx], dh")
                if self.c64:
                    if mode == "abs":
                        self.mark_const(opnd)
                    elif mode == "absx":
                        L("    call c64_mark")
                    return True
                (a0, a1), (b0, b1) = self.p["video"]
                if mode == "abs":
                    pg = opnd >> 8
                    if a0 <= pg < a1 or b0 <= pg < b1:
                        L("    call vid_mark")
                elif mode == "absx":
                    k, v = self.lab(), self.lab()
                    # a display page, decided as the core decides it
                    L("    cmp bh, 0x%02X" % b1, "    jae %s" % k,
                      "    cmp bh, 0x%02X" % b0, "    jae %s" % v,
                      "    cmp bh, 0x%02X" % a1, "    jae %s" % k,
                      "    cmp bh, 0x%02X" % a0, "    jb %s" % k,
                      "%s: call vid_mark" % v, "%s:" % k)
        elif mode == "rel":
            f, mask, not_taken = {
                "BPL": ("ah", 0x80, "jnz"), "BMI": ("ah", 0x80, "jz"),
                "BVC": ("pv", 0x40, "jnz"), "BVS": ("pv", 0x40, "jz"),
                "BCC": ("ah", 0x01, "jnz"), "BCS": ("ah", 0x01, "jz"),
                "BNE": ("ah", 0x40, "jnz"), "BEQ": ("ah", 0x40, "jz")}[mn]
            target = (nxt + (opnd - 256 if opnd & 0x80 else opnd)) & 0xFFFF
            nt = self.lab()
            where = "byte [%s]" % self.PV() if f == "pv" else "ah"
            L("    test %s, 0x%02X" % (where, mask), "    %s %s" % (not_taken, nt))
            L("    sub bp, %d" % (2 if (target ^ nxt) & 0xFF00 else 1))
            # ONLY A BACKWARD BRANCH CAN LOOP, so only it tests the countdown:
            # a forward one makes progress through the ROM and the next
            # transfer tests it
            self.to(target, check=target <= pc)
            L("%s:" % nt)
            return True
        elif mn == "JMP":
            if mode == "abs":
                if opnd in (self.p.get("chrget") or {}):
                    # A TAIL CALL INTO CHRGET (the C64's comma test at $AEFF
                    # ends `JMP $0073`): the native routine, whose RTS is the
                    # caller's return, already on the stack
                    L("    jmp %s" % self.p["chrget"][opnd])
                    return False
                if opnd <= pc and not self.exact:   # only a backward one can loop
                    L("    or bp, bp")
                self.to(opnd, check=opnd <= pc)
                return False
            # (ind): the pointer is in RAM or the ROM, with the NMOS page wrap
            ptr = opnd
            hi = (ptr & 0xFF00) | ((ptr + 1) & 0xFF)
            if self.c64 and (ptr >= 0xA000 or hi >= 0xA000 or ptr < 2):
                L("    add bp, %d" % self.pre[pc], "    mov si, 0x%04X" % pc, "    jmp xa_resume")
                return False
            if not self.c64 and (0xC000 <= ptr < 0xD000 or 0xC000 <= hi < 0xD000):
                L("    add bp, %d" % self.pre[pc], "    mov si, 0x%04X" % pc, "    jmp xa_resume")
                return False
            L("    mov bl, [0x%04X]" % ptr, "    mov bh, [0x%04X]" % hi,
              "    or bp, bp")
            self.rts_to()
            return False
        elif mn == "JSR":
            # the return address pushed as one word, inline; a_jsr where S is
            # at the stack page's edge
            sl, back = self.lab(), self.lab()
            L("    cmp di, 0x0101", "    jb %s" % sl,
              "    mov word [di-1], 0x%04X" % ((pc + 2) & 0xFFFF), "    sub di, 2",
              "%s:" % back)
            self.tail.append(["%s: mov dx, 0x%04X" % (sl, (pc + 2) & 0xFFFF),
                              "    call a_jsr", "    jmp %s" % back])
            # NO TEST: a call makes no loop by itself - every loop has a
            # backward branch, a backward JMP or an RTS in it, which test
            if opnd in (self.p.get("chrget") or {}):
                L("    jmp %s" % self.p["chrget"][opnd])
            else:
                self.to(opnd, check=False)
            return False
        elif mn == "RTS":
            L("    jmp a_rts")
            return False
        elif mn == "PHA":
            self.push("al")
        elif mn == "PHP":
            # P out of AH (N, Z, C) through t_getp, and V, B, D, I out of PV:
            # the core's getp, without the call and the rotates
            L("    mov bl, ah", "    xor bh, bh", "    mov dl, [cs:bx+t_getp]",
              "    mov bh, [%s]" % self.PV(), "    and bh, 0x5C", "    or dl, bh",
              "    or dl, 0x10")
            self.push("dl")
            L("    xor dl, dl")
        elif mn == "PLA":
            self.pop("al")
            self.nz("al", live_nz)
        elif mn in ("TAX", "TAY", "TXA", "TYA"):
            d, s = REG[mn[2]], REG[mn[1]]
            L("    mov %s, %s" % (d, s))
            self.nz(d, live_nz)
        elif mn == "TSX":
            L("    mov bx, di", "    mov cl, bl")
            self.nz("cl", live_nz)
        elif mn == "TXS":
            L("    mov bl, cl", "    mov bh, 1", "    mov di, bx")
        elif mn in ("INX", "INY", "DEX", "DEY"):
            r = REG[mn[2]]
            op = "inc" if mn[0] == "I" else "dec"
            if live_nz:
                L("    sahf", "    %s %s" % (op, r), "    lahf")
            else:
                L("    %s %s" % (op, r))
        elif mn in ("CLC", "SEC"):
            L("    and ah, 0xFE" if mn == "CLC" else "    or ah, 0x01")
        elif mn in ("CLV", "CLD", "CLI"):
            L("    and byte [%s], 0x%02X" % (self.PV(), 0xFF ^ {"CLV": 0x40, "CLD": 0x08, "CLI": 0x04}[mn]))
        elif mn == "SEI":
            L("    or byte [%s], 0x04" % self.PV())
        elif mn == "PLP":
            self.pop("dl")
            self.setp_inline()
            x = self.lab()
            L("    test byte [%s], 0x08" % self.PV(), "    jnz %s" % x)
            self.tail.append(["%s: mov si, 0x%04X" % (x, nxt), "    jmp xa_resume"])
        elif mn == "RTI":
            self.pop("dl")
            self.setp_inline()
            self.pop("bl")
            self.pop("bh")
            x = self.lab()
            L("    test byte [%s], 0x08" % self.PV(), "    jz %s" % x,
              "    mov si, bx", "    jmp xa_resume", "%s:" % x)
            L("    or bp, bp")
            self.rts_to()
            return False
        elif mn == "NOP":
            pass
        else:
            raise ValueError(mn)
        return True                 # falls through to the next PC

    def setp_inline(self):
        """DL = a pulled P -> AH through t_setp, and PV's V, B, D, I (the
        core's setp, without the call and the rotates); DL 0 after"""
        self.L("    mov bl, dl", "    xor bh, bh", "    mov ah, [cs:bx+t_setp]",
               "    and dl, 0x5C", "    and byte [%s], 0xA3" % self.PV(),
               "    or [%s], dl" % self.PV(), "    xor dl, dl")

    def chr_guard(self, mn, mode, opnd):
        """a store that may write the CHRGET routine (other than TXTPTR, which
        it writes itself) clears PV's "checked" bit (emit_chrget)"""
        if not self.p.get("chrget") or not (mn in STORES or (mn in RMW and mode != "acc")):
            return
        B = min(self.p["chrget"])
        lo, hi, T = B, B + 23, B + 7
        if mode in ("zp", "abs"):
            hit = lo <= opnd <= hi and opnd not in (T, T + 1)
        elif mode in ("absx", "absy"):
            hit = opnd <= hi and opnd + 255 >= lo
        else:
            hit = True                      # zp,X wraps; a pointer: anywhere
        if hit:
            self.L("    and byte [%s], 0x7F" % self.PV())

    def static_core(self, mn, mode, opnd):
        """the C64: an instruction whose operand alone says the core runs it
        - a write to the processor port, a read-modify-write of what may not
        be RAM, a pointer in $FF/$00/$01"""
        writes = mn in STORES or (mn in RMW and mode != "acc")
        if writes and mode in ("zp", "abs") and opnd < 2:
            return True
        if mn in RMW and mode == "abs" and opnd >= 0xA000:
            return True
        return mode == "indy" and opnd in (0x00, 0x01, 0xFF)

    def may_exit(self, mn, mode, opnd):
        """the C64: may this instruction leave for the core before it writes
        N and Z (a bail on its address, or a write that called out)"""
        if self.static_core(mn, mode, opnd):
            return True
        if mn in STORES or (mn in RMW and mode not in ("acc", "zp")):
            return True
        return mode in ("indx", "indy")

    def shift(self, mn, r, live):
        L = self.L
        if mn == "ASL":
            L("    shl %s, 1" % r, "    lahf")
        elif mn == "LSR":
            L("    shr %s, 1" % r, "    lahf")
        elif mn in ("ROL", "ROR"):
            L("    sahf", "    %s %s, 1" % ("rcl" if mn == "ROL" else "rcr", r))
            if live:
                L("    inc %s" % r, "    dec %s" % r)
            L("    lahf")
        elif mn in ("INC", "DEC"):
            L("    sahf", "    %s %s" % (mn.lower(), r), "    lahf")

    def emit_chrget(self):
        """APPLESOFT'S CHRGET, NATIVE (APPLE2-SPEC 4.7). The routine the cold
        start copies into $00B1-$00C8 - INC TXTPTR / BNE / INC TXTPTR+1 /
        LDA (TXTPTR) / CMP #':' / BCS RTS / CMP #' ' / BEQ CHRGET / SEC /
        SBC #'0' / SEC / SBC #$D0 / RTS - run here with the same memory
        writes, cycles and final flags, path by path, when JSR $00B1 or JSR
        $00B7 is made from translated code. Entered with the return address
        pushed and the JSR's cycles taken. Only while the zero-page bytes
        ARE that routine (a patched CHRGET, or decimal mode, goes to the
        core at the same PC, which runs whatever is there)."""
        L, PV = self.L, self.PV()
        # THE SAME ROUTINE IN BOTH BASICS (Microsoft's), at $00B1 in
        # Applesoft and $0073 on the C64: TXTPTR is its 8th and 9th bytes
        B = min(self.p["chrget"])
        T = B + 7
        sig = [(B, 0xE6 | T << 8), (B + 2, 0x02D0), (B + 4, 0xE6 | (T + 1) << 8),
               (B + 9, 0x3AC9), (B + 11, 0x0AB0), (B + 13, 0x20C9), (B + 15, 0xEFF0),
               (B + 17, 0xE938), (B + 19, 0x3830), (B + 21, 0xD0E9)]
        top = 0xA0 if self.c64 else 0xC0
        # THE SIGNATURE IS CHECKED ONCE, NOT AT EVERY CALL: PV's bit 7 says
        # "checked", and it is cleared by every entry from the core (m65gen's
        # ct_rom - code that ran there may have patched the routine) and by
        # every translated store that can reach it (chr_guard). It was a
        # dozen compares a character of program text.
        L("a_chrsig:")
        for a, w in sig:
            L("    cmp word [0x%04X], 0x%04X" % (a, w), "    jne .r")
        L("    cmp byte [0x%04X], 0xAD" % (B + 6), "    jne .r",
          "    cmp byte [0x%04X], 0x60" % (B + 23), "    jne .r",
          "    or byte [%s], 0x80" % PV,
          "    cmp al, al",                     # (ZF: it is the routine)
          ".r: ret")
        for entry, lab in ((B, "a_chrget"), (B + 6, "a_chrgot")):
            L("%s:" % lab,
              "    test byte [%s], 0x80" % PV, "    jnz .ok",
              "    call a_chrsig", "    jne .core",
              ".ok:")
            if self.exact:
                # the whole routine's worst path is 38 cycles: the core runs
                # it where that does not fit, so the run ends where it would
                L("    cmp bp, 40", "    jle .core")
            L("    jmp %s" % ("a_chrbody.inc" if entry == B else "a_chrbody.lda"))
            L(".core: mov si, 0x%04X" % entry, "    jmp xa_resume")
        loop = (["    cmp bp, 40", "    jle .loopx", "    jmp .inc",
                 ".loopx: mov si, 0x%04X" % B, "    jmp xa_resume"] if self.exact else
                ["    jle .loopx", "    jmp .inc",
                 ".loopx: mov si, 0x%04X" % B, "    jmp xa_slice"])
        L(*["a_chrbody:",
          ".inc:",
          "    sub bp, 5",
          "    inc byte [0x%04X]" % T,          # INC TXTPTR
          "    jz .hi",
          "    sub bp, 3",                      # BNE, taken
          "    jmp short .lda",
          ".hi: sub bp, 7",                     # BNE not (2), INC TXTPTR+1 (5)
          "    inc byte [0x%04X]" % (T + 1),
          ".lda:",
          "    sub bp, 6",                      # LDA abs (4), CMP # (2)
          "    mov bx, [0x%04X]" % T,
          "    cmp bh, 0x%02X" % top, "    jae .rd",
          "    mov al, [bx]",
          ".got:",
          "    cmp al, 0x3A", "    jae .ge",
          "    sub bp, 4",                      # BCS not (2), CMP # (2)
          "    cmp al, 0x20", "    jne .digit",
          "    sub bp, 3"] + loop + [           # BEQ taken: the space loop
          ".digit:",
          "    sub bp, 10",                     # BEQ not, SEC, SBC, SEC, SBC
          "    or ah, 0x01", "    sahf", "    cmc", "    sbb al, 0x30", "    cmc", "    lahf",
          "    or ah, 0x01", "    sahf", "    cmc", "    sbb al, 0xD0", "    cmc", "    lahf",
          "    call a_vof",
          "    jmp short .rts",
          ".ge:",
          "    sub bp, 3",                      # BCS taken
          "    cmp al, 0x3A", "    cmc", "    lahf",   # CMP #':''s N Z C
          ".rts:",
          "    sub bp, 6",
          "    jmp a_rts",
          ".rd: call %s" % ("rd_hi" if self.c64 else "ra_any"),
          "    mov al, dh",
          "    jmp .got"])

    # --- the whole ------------------------------------------------------
    def liveness(self):
        """NZ live-out per PC: does anything after this instruction, before
        N and Z are written again, read them - on the fall-through path; any
        other successor (a branch, a jump, an exit) counts as reading them"""
        live = {}
        for pc in sorted(self.pcs, reverse=True):
            mn, mode, cyc, opnd, n = decode(self.rom, pc)
            nxt = pc + n
            if mn in TRANSFERS or mode == "rel" or mn in LEAVE or nxt not in self.pcs:
                live[pc] = True
                continue
            if self.exact and nxt in self.leaders:
                live[pc] = True                 # its block test may leave
                continue
            nmn, nmode, _, nopnd = decode(self.rom, nxt)[:4]
            if self.c64 and self.may_exit(nmn, nmode, nopnd):
                live[pc] = True
                continue
            if nmode == "rel" or nmn == "PHP" or nmn in LEAVE \
                    or nmn in IRQ_GATED \
                    or nmn in TRANSFERS:
                live[pc] = True                 # it reads them, or leaves
            elif nmn in RMW and nmode not in ("acc", "zp", "zpx"):
                live[pc] = True                 # (it may exit to the core)
            elif nmn in WRITES_NZ and nmn not in ("BIT",):
                # it writes N and Z without reading them: dead here - unless
                # it could leave before writing them (a decimal ADC, or a
                # read through the core's ladder that returns, which keeps
                # them) - only the ADC/SBC case exits, and those read C
                live[pc] = False
            else:
                live[pc] = live.get(nxt, True)
        return live

    def vliveness(self):
        """V live-out per PC, NZ's rule for the overflow flag: read by BVC,
        BVS and PHP, written by ADC, SBC, BIT, CLV and PLP - and any exit
        counts as a read"""
        live = {}
        for pc in sorted(self.pcs, reverse=True):
            mn, mode, cyc, opnd, n = decode(self.rom, pc)
            nxt = pc + n
            if mn in TRANSFERS or mode == "rel" or mn in LEAVE or nxt not in self.pcs \
                    or (self.exact and nxt in self.leaders):
                live[pc] = True
                continue
            nmn, nmode, _, nopnd = decode(self.rom, nxt)[:4]
            if nmn in ("BVC", "BVS", "PHP") or nmn in LEAVE or nmn in IRQ_GATED \
                    or nmn in TRANSFERS or nmode == "rel" \
                    or (self.c64 and self.may_exit(nmn, nmode, nopnd)) \
                    or (not self.c64 and nmn in RMW and nmode not in ("acc", "zp", "zpx")):
                live[pc] = True
            elif nmn in ("ADC", "SBC", "BIT", "CLV"):
                live[pc] = False
            else:
                live[pc] = live.get(nxt, True)
        return live

    def cost(self, pc):
        """the most an instruction can take: its cycles, a page crossed, a
        branch taken across one"""
        mn, mode, cyc = decode(self.rom, pc)[:3]
        return cyc + (1 if mode in PENALTY and mn in READS else 0) + \
            (2 if mode == "rel" else 0)

    def falls(self, pc):
        """does control reach the next PC in order (a branch not taken does)"""
        mn, mode, cyc, opnd, n = decode(self.rom, pc)
        if mn in TRANSFERS or mn in LEAVE:
            return False
        return not (self.c64 and self.static_core(mn, mode, opnd))

    def static_exit(self, pc):
        """does this instruction always leave for the core, before it runs"""
        mn, mode, cyc, opnd, n = decode(self.rom, pc)
        if mn in LEAVE:
            return True
        if self.c64:
            return self.static_core(mn, mode, opnd)
        return mn in RMW and mode not in ("acc", "zp", "zpx") and \
            not (mode == "abs" and opnd < 0xC000) and \
            not (mode == "absx" and opnd + 255 < 0xC000)

    def io_capable(self, pc):
        """may this instruction's access reach a device, whose clock is the
        countdown at THIS instruction's end - so nothing after it may be
        prepaid"""
        mn, mode, cyc, opnd, n = decode(self.rom, pc)
        if not (mn in READS or mn in STORES or mn in RMW):
            return False
        if mode in ("imm", "zp", "zpx", "zpy", "acc"):
            return False
        if mn in RMW:
            return False                    # (never a device: the core's)
        span = (opnd, min(opnd + 255, 0xFFFF)) if mode in ("absx", "absy") else \
            (opnd, opnd) if mode == "abs" else None
        if span is None:
            return True                     # through a pointer: anywhere
        if self.c64:
            if mn in STORES:
                return span[1] >= 0xD000
            saved, self.cur = self.cur, pc
            try:
                c0, c1 = self.rclass(span[0]), self.rclass(span[1])
                if mode == "abs" and self.known_rom(opnd):
                    return False
            finally:
                self.cur = saved
            return not (c0 == c1 and c0 in ("ram", "B", "K"))
        if mn in STORES:
            return not (span[1] < 0xC000 or span[0] >= 0xC100)
        if mode == "abs":
            return 0xC000 <= opnd < 0xC200
        return self.span_ram(mode, opnd) is None

    def groups(self):
        """THE STRAIGHT RUNS one `sub bp` pays for: -> (the PCs that start
        one, PC -> the cycles prepaid from it to its group's end). A group
        ends at a branch or a transfer, at an access that may reach a device
        (its clock is the countdown at that instruction), and around an
        instruction that may leave for the core before it runs. Only a
        group's start may be entered from outside - the PC table names no
        other - so a target, a block's test, and a PC the trace saw RTS or
        JMP (ind) land on (seeds marked D) each start one."""
        targets = set()
        for pc in self.pcs:
            mn, mode, cyc, opnd, n = decode(self.rom, pc)
            if mode == "rel":
                targets.add((pc + n + (opnd - 256 if opnd & 0x80 else opnd)) & 0xFFFF)
            elif mn in ("JMP", "JSR") and mode == "abs":
                targets.add(opnd)
        start = set()
        prev = None
        for pc in sorted(self.pcs):
            mn, mode, cyc, opnd, n = decode(self.rom, pc)
            fresh = prev is None or prev + decode(self.rom, prev)[4] != pc
            if not fresh:
                pmn, pmode = decode(self.rom, prev)[:2]
                fresh = (pmn in TRANSFERS or pmode == "rel" or pmn in IRQ_GATED
                         or self.static_exit(prev) or self.io_capable(prev))
            if fresh or pc in targets or pc in self.dyn or pc in self.leaders \
                    or mn in IRQ_GATED or self.static_exit(pc):
                start.add(pc)
            prev = pc
        pre = {}
        nxt_start = None
        for pc in sorted(self.pcs, reverse=True):
            mn, mode, cyc, opnd, n = decode(self.rom, pc)
            own = 0 if self.static_exit(pc) else cyc
            nx = pc + n
            if nx in self.pcs and nx not in start:
                pre[pc] = own + pre[nx]
            else:
                pre[pc] = own
        return start, pre

    def blocks(self):
        """EXACT PROFILES: the PCs whose translation tests the countdown, ->
        what the code from there to the next block can take at most.

        A PC entered from OUTSIDE the translation (the core's ct_rom) or
        through the table (RTS, JMP through a pointer) is entered only with
        more than MAXBLK left, so it needs no test of its own. What does is
        a PC a STATIC jump enters - JMP, JSR, a branch backwards, a branch
        forwards out of its own block - and one that a block too long for
        MAXBLK falls into. Within a block control only moves forwards, and
        what it skips it does not pay, so the block's straight sum bounds
        every path through it."""
        into, targets, fwd = set(), set(), []
        for pc in self.pcs:
            mn, mode, cyc, opnd, n = decode(self.rom, pc)
            nxt = pc + n
            if mode == "rel":
                t = (nxt + (opnd - 256 if opnd & 0x80 else opnd)) & 0xFFFF
                if t <= pc:
                    targets.add(t)
                else:
                    fwd.append((pc, t))
            elif mn in ("JMP", "JSR") and mode == "abs":
                targets.add(opnd)
            if self.falls(pc):
                into.add(nxt)
        checked = targets & self.pcs
        while True:
            block_of, cost, start, total = {}, {}, None, 0
            for pc in sorted(self.pcs):
                c = self.cost(pc)
                if pc in checked or pc not in into or start is None:
                    start, total = pc, 0
                elif total + c > MAXBLK:
                    checked.add(pc)             # fallen into: it must test
                    start, total = pc, 0
                total += c
                block_of[pc] = start
                cost[start] = total
            more = {t for src, t in fwd
                    if t in self.pcs and t not in checked and block_of[t] != block_of[src]}
            if not more:
                return {pc: cost[pc] for pc in checked}
            checked |= more

    def emit(self):
        L = self.L
        p = self.p
        self.leaders = self.blocks() if self.exact else {}
        self.gstart, self.pre = self.groups()
        F_CORE = self.fa + 4
        L("; GENERATED by apps/m6502/m65aot.py --machine %s - do not edit" % p["name"],
          "cpu 8086", "bits 16", "org 0",
          "SCR  equ 0x%04X" % p["scr"],
          "SCRD equ 0x%04X - 0x10000" % p["scr"], "")
        # the doors back into the core: a far jump through the frame's word
        L("xa_resume:",                 # SI = the PC to run next
          "    xor dl, dl",
          "    mov bx, sp",
          "    push word [ss:bx+%d]" % F_CORE,
          "    mov bx, 0x%04X" % XR_RESUME, "    push bx", "    retf",
          "xa_resume_si:",              # (SI already set)
          "    jmp xa_resume",
          "xa_slice:",                  # SI = the PC, the budget spent
          "    xor dl, dl",
          "    mov bx, sp",
          "    push word [ss:bx+%d]" % F_CORE,
          "    mov bx, 0x%04X" % XR_SLICE, "    push bx", "    retf", "")
        # the core's own helpers, generated by the core's generator - and its
        # read and write paths ONCE, as near procs: BX the address (above
        # $BFFF for a read), DH the byte, the cycles already counted
        # (CALLED, so the doors they reach are one call deeper than a
        # handler's: rd_io2/wr_io2 read the frame two bytes further on)
        if self.c64:
            return self.emit_rest()
        mark = len(self.out)
        L("ra_any:")
        self.g.later = []
        self.g.read("abs", "dh", 0)
        L("    ret")
        for blk in self.g.later:
            L(*[("    ret" if l.startswith("    jmp ") and l == blk[-1] else l) for l in blk])
        L("wa_any:")
        self.g.later = []
        self.g.write("abs", "dh", 0, None)
        L("    ret")
        for blk in self.g.later:
            L(*[("    ret" if l.startswith("    jmp ") and l == blk[-1] else l) for l in blk])
        self.out[mark:] = [l.replace("call rd_io", "call rd_io2").replace("call wr_io", "call wr_io2")
                           for l in self.out[mark:]]
        from m65gen import F_IORD, F_IOWR
        io2 = self.g.apple_io("    call far [bp+%d]" % (6 + F_IORD),
                              "    call far [bp+%d]" % (6 + F_IOWR))
        L(*[l.replace("rd_io:", "rd_io2:").replace("wr_io:", "wr_io2:") for l in io2])
        if self.p.get("chrget"):
            self.emit_chrget()
        return self.emit_rest()

    def emit_rest(self):
        L = self.L
        if self.c64:
            # (the C64's memory paths are the core's own rd_hi and wr_hi,
            # called from this code at the depth a handler calls them)
            if self.p.get("chrget"):
                self.emit_chrget()
            if self.bank is not None:
                # rd_ctx - BX >= $A000 -> DH, the bank map as this segment's
                # code knows it: its own bank is mapped in, and the KERNAL
                # whenever BASIC is. The rest is rd_hi's (jumped to: the
                # depth is the caller's)
                L("rd_ctx:",
                  "    cmp bh, 0xC0", "    jae .c")
                if self.bank == 0:
                    L("    mov si, sp", "    push es",
                      "    mov es, [ss:si+%d]" % (2 + F_EXTRA + 2),
                      "    mov dh, [es:bx]", "    pop es", "    ret")
                else:
                    L("    jmp rd_hi")
                L(".c: cmp bh, 0xE0", "    jae .k",
                  "    cmp bh, 0xD0", "    jae .io",
                  "    mov dh, [bx]", "    ret",
                  ".io: jmp rd_hi",
                  ".k: mov si, sp", "    push es",
                  "    mov es, [ss:si+%d]" % (2 + F_EXTRA + 6),
                  "    mov dh, [es:bx]", "    pop es", "    ret")
                # a_far - BX = a PC of the OTHER bank, mapped in and a block's
                # start (its own test): its translation, in its segment
                L("a_far:",
                  "    mov si, bx",
                  "    sub bh, 0x%02X" % (0xC0 if self.bank == 0 else 0xA0),
                  "    shl bx, 1", "    mov bx, [es:bx]",
                  "    test bx, bx", "    jz .res")
                self.far(self.frame_of(1 - self.bank))
                L(".res: jmp xa_resume")
        # P <-> AH, as tables in this segment (PHP and PLP, inline): AH is
        # lahf's N (7), Z (6) and C (0); P's are N (7), Z (1) and C (0)
        L("t_getp:", *["    db " + ", ".join("0x%02X" % ((a & 0x81) | ((a & 0x40) >> 5) | 0x20)
                                          for a in range(r, r + 16)) for r in range(0, 256, 16)])
        L("t_setp:", *["    db " + ", ".join("0x%02X" % ((p & 0x81) | ((p & 0x02) << 5))
                                          for p in range(r, r + 16)) for r in range(0, 256, 16)])
        # V from the 8086's OF, once (ADC, SBC)
        L("a_vof:",
          "    jo .v",
          "    and byte [%s], 0xBF" % self.PV(), "    ret",
          ".v: or byte [%s], 0x40" % self.PV(), "    ret")
        # JSR's push (DX = the return address less one) and RTS, once
        slow = self.lab()
        L("a_jsr:",
          "    cmp di, 0x0101", "    jb .slow",
          "    mov [di-1], dx", "    sub di, 2",
          "    xor dl, dl", "    or bp, bp", "    ret",
          ".slow:")
        self.push("dh")
        self.push("dl")
        L("    xor dl, dl", "    or bp, bp", "    ret",
          "a_rts:",
          "    cmp di, 0x01FE", "    jae .slow",
          "    mov bx, [di+1]", "    add di, 2",
          ".j: inc bx", *([] if self.exact else ["    or bp, bp"]))
        self.rts_to()
        L(".slow:")
        self.pop("bl")
        self.pop("bh")
        L("    jmp .j")
        t = self.g.tails()
        L(*t["io"])
        L(*t["vid"])
        L(*t["pflags"])
        live = self.liveness()
        self.vlive = self.vliveness()
        self.tail = []
        prev_falls = False
        last = None
        for pc in sorted(self.mine):
            if prev_falls and last is not None and last != pc:
                # (a fall-through that is not the next PC in order: an
                # instruction whose operand is another's opcode - Microsoft
                # BASIC's `BIT` skip, $AD8D CLC / $AD8E BIT $38 over $AD8F SEC
                # - goes on in the translation when it is here)
                if last in self.mine:
                    L("    jmp p_%04X" % last)
                else:
                    L("    mov si, 0x%04X" % last, "    jmp xa_resume")
            prev_falls = self.insn(pc, live[pc])
            last = pc + decode(self.rom, pc)[4]
            if self.tail:
                # OUT-OF-LINE blocks go where nothing falls into them: after a
                # jump around them when this instruction falls through
                skip = self.lab()
                if prev_falls:
                    L("    jmp short %s" % skip)
                for blk in self.tail:
                    L(*blk)
                if prev_falls:
                    L("%s:" % skip)
            self.tail = []
        if prev_falls:
            L("    mov si, 0x%04X" % last, "    jmp xa_resume")
        if self.p.get("gate"):
            L("ctl_list:", *["    dw %s" % c for c in self.ctls], "    dw 0")
        return "\n".join(self.out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="apple2", choices=["apple2", "c64"])
    ap.add_argument("--rom", required=True)
    ap.add_argument("--seeds", required=True)
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--table", required=True, help="the PC table's binary")
    ap.add_argument("--range", help="LO:HI hex - translate this part of a 64KB "
                    "IMAGE instead of a ROM (a2cputest's Dormann)")
    a = ap.parse_args()
    prof = APPLE2 if a.machine == "apple2" else C64
    if a.range:
        lo, hi = (int(v, 16) for v in a.range.split(":"))
        rom = Rom(open(a.rom, "rb").read()[lo:hi], lo)
        prof = dict(prof, aot=(lo, hi), chrget=None, gate=True)
        seeds = []
    elif a.machine == "c64":
        # C64.ROM is the package's part 0: the KERNAL at 0, BASIC at $2000
        # (c64cpu.inc's C64_ROMS_*)
        img = open(a.rom, "rb").read()
        rom = Rom(img[0x2000:0x4000], 0xA000, [(0xE000, img[0:0x2000])])
        seeds = [rom.w(0xFFFC), rom.w(0xFFFE), rom.w(0xFFFA)]
    else:
        rom = Rom(open(a.rom, "rb").read()[:0x3000], 0xD000)
        seeds = [rom.w(0xFFFC), rom.w(0xFFFE), rom.w(0xFFFA)]
    # A SEED is `XXXX`, or `XXXX D` where RTS, JMP (ind), BRK or an interrupt
    # landed (a group must start there: groups())
    dyn = set()
    for l in open(a.seeds):
        f = l.split()
        if f and not f[0].startswith("#"):
            seeds.append(int(f[0], 16))
            if len(f) > 1 and f[1] == "D":
                dyn.add(int(f[0], 16))
    pcs = walk(rom, seeds)
    # THE KEYIN LOOP STAYS THE CORE'S: it runs it as an exact counter while
    # no key is latched (m65gen.py's kbd_idle), which no translation beats
    if not a.range and a.machine == "apple2":
        pcs -= {0xFD1B, 0xFD1D, 0xFD1F, 0xFD21, 0xFD24}
    # DRIVE 8'S TRAP (C64-SPEC 11.5): the package patches a JAM byte over the
    # KERNAL's LDA #$60 at $F4C4, and the core must meet it - a translation of
    # the ROM's own bytes there would run straight past
    if not a.range and a.machine == "c64":
        pcs -= {0xF4C4}
    elif a.range:
        # A RANGE OF RAM CAN BE WRITTEN, which a ROM cannot: an instruction
        # whose bytes a fixed-address store in the program writes (Dormann's
        # branch test rewrites a BEQ's offset at $04E6) is left to the core,
        # which reads what is there. A store through an index or a pointer
        # is not seen - this mode is the gate's, on a program that has none
        written = set()
        for pc in pcs:
            mn, mode, cyc, opnd, n = decode(rom, pc)
            if (mn in STORES or mn in RMW) and mode == "abs" and rom.has(opnd):
                written.add(opnd)
        smc = {pc for pc in pcs
               if any(pc <= w < pc + decode(rom, pc)[4] for w in written)}
        pcs -= smc
        if smc:
            print("m65aot: %d PC(s) written by the program left to the core: %s"
                  % (len(smc), " ".join("$%04X" % p for p in sorted(smc))))
    # ONE SEGMENT, or on the C64 one per bank - BASIC's to -o, the KERNAL's
    # to the same name with a `k` (each its own part: together they pass
    # what a part can hold)
    import re
    import subprocess
    if a.machine == "c64" and not a.range:
        outs = [(a.out, {p for p in pcs if p < 0xC000}, 0),
                (os.path.splitext(a.out)[0] + "k.asm", {p for p in pcs if p >= 0xE000}, 1)]
    else:
        outs = [(a.out, None, None)]
    labs, sizes = {}, []
    for out, mine, bank in outs:
        aot = Aot(prof, rom, pcs, mine, bank, dyn)
        src = aot.emit()
        # ASSEMBLED HERE, for the symbol map the PC table is made from
        bin_out = os.path.splitext(out)[0] + ".bin"
        map_out = os.path.splitext(out)[0] + ".map"
        with open(out, "w") as f:
            f.write("[map symbols %s]\n" % map_out + src)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", bin_out, out], check=True)
        size = os.path.getsize(bin_out)
        sizes.append(size)
        if size > 0xFE00:
            raise SystemExit("m65aot: %s is %d bytes and a part stops at 65,024"
                             % (bin_out, size))
        for line in open(map_out):
            m = re.match(r"\s*([0-9A-F]+)\s+([0-9A-F]+)\s+p_([0-9A-F]{4})\s*$", line)
            if m and (mine is None or int(m.group(3), 16) in mine) \
                    and int(m.group(3), 16) in aot.gstart:
                labs[int(m.group(3), 16)] = int(m.group(2), 16)
    # THE TABLE: a word per PC of each range, the ranges one after another
    # (the C64's: BASIC's 8K, then the KERNAL's - the core's ct_rom)
    tab = bytearray(2 * sum(len(d) for b, d in rom.segs))
    for pc, off in labs.items():
        if off == 0:
            raise SystemExit("m65aot: a translation at offset 0 reads as none")
        i = 0
        for b, d in rom.segs:
            if b <= pc < b + len(d):
                i += pc - b
                break
            i += len(d)
        tab[2 * i:2 * i + 2] = off.to_bytes(2, "little")
    with open(a.table, "wb") as f:
        f.write(tab)
    print("m65aot: %d seeds, %d ROM PCs translated, %s bytes; the PC table %d "
          "entries -> %s" % (len(seeds), len(pcs), " + ".join(map(str, sizes)),
                             len(labs), a.table))


if __name__ == "__main__":
    sys.exit(main())

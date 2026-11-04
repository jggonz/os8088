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
from m65gen import XR_RESUME, XR_SLICE                                  # noqa: E402
F_AOT, F_TAB, F_CORE = 24, 26, 28   # the Apple II's frame words (m65gen.py)

LEN = {"imp": 1, "acc": 1, "imm": 2, "zp": 2, "zpx": 2, "zpy": 2, "rel": 2,
       "abs": 3, "absx": 3, "absy": 3, "ind": 3, "indx": 2, "indy": 2}
WRITES_NZ = {"LDA", "LDX", "LDY", "ORA", "AND", "EOR", "ADC", "SBC", "CMP",
             "CPX", "CPY", "BIT", "ASL", "LSR", "ROL", "ROR", "INC", "DEC",
             "TAX", "TAY", "TXA", "TYA", "TSX", "PLA", "INX", "INY", "DEX",
             "DEY", "PLP", "RTI"}
TRANSFERS = {"JMP", "JSR", "RTS", "RTI", "BRK"}
# what this translator leaves to the core: it exits there and the core runs it
LEAVE = {"BRK", "CLI", "SEI", "PLP", "RTI", "SED"}


class Rom:
    def __init__(self, data, base):
        self.data, self.base = data, base
        self.top = base + len(data)

    def has(self, a):
        return self.base <= a < self.top

    def b(self, a):
        return self.data[a - self.base]

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
    def __init__(self, prof, rom, pcs):
        self.p, self.rom, self.pcs = prof, rom, pcs
        self.out = []
        self.n = 0
        self.ctl = False
        self.g = Gen(prof)      # the memory helpers come from the core's own
        self.g.out = self.out   # ...written into THIS file, in place

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
        if check:
            x = self.lab()
            self.L("    jle %s" % x)
            self.go(target)
            self.L("%s: mov si, 0x%04X" % (x, target), "    jmp xa_slice")
        else:
            self.go(target)

    def go(self, target):
        if target in self.pcs:
            self.L("    jmp p_%04X" % target)
        else:
            self.L("    mov si, 0x%04X" % target, "    jmp xa_resume")

    # --- flags -----------------------------------------------------------
    def nz(self, r, live):
        if live:
            self.L("    sahf", "    inc %s" % r, "    dec %s" % r, "    lahf")

    def v_from_of(self):
        self.L("    call a_vof")            # (call keeps OF)

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

    def read(self, mode, opnd, dst, penalty):
        """DST = the operand. The core's read path where the address is not
        known; otherwise the one instruction it comes to"""
        L = self.L
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

    def write(self, mode, opnd, src):
        L = self.L
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

    # --- one instruction -------------------------------------------------
    def insn(self, pc, live_nz):
        mn, mode, cyc, opnd, n = decode(self.rom, pc)
        L = self.L
        nxt = pc + n
        self.L("p_%04X:" % pc)
        if mn in LEAVE or (mn in ("ADC", "SBC")) and False:
            L("    mov si, 0x%04X" % pc, "    jmp xa_resume")
            return False
        if mn in ("ADC", "SBC"):
            # decimal: the core's (it hands it back); binary: here
            k = self.lab()
            L("    test byte [%s], 0x08" % self.PV(), "    jz %s" % k,
              "    mov si, 0x%04X" % pc, "    jmp xa_resume", "%s:" % k)
        if mn in RMW and mode not in ("acc", "zp", "zpx") and \
                not (mode == "abs" and opnd < 0xC000) and \
                not (mode == "absx" and opnd + 255 < 0xC000):
            L("    mov si, 0x%04X" % pc, "    jmp xa_resume")   # I/O or ROM
            return False
        L("    sub bp, %d" % cyc)
        if mn in READS:
            dst = {"LDA": "al", "LDX": "cl", "LDY": "ch"}.get(mn, "dh")
            self.read(mode, opnd, dst, mode in PENALTY)
            src = dst
            if mn in ("LDA", "LDX", "LDY"):
                self.nz(dst, live_nz)
            elif mn in ("ORA", "AND", "EOR"):
                L("    %s al, %s" % ({"ORA": "or", "AND": "and", "EOR": "xor"}[mn], src))
                self.nz("al", live_nz)
            elif mn == "ADC":
                L("    sahf")
                if mode == "imm" and not self.ctl:
                    # THE GATE'S HANDLE (a2cputest's translated legs): the
                    # first ADC # translated, which its control makes an OR
                    self.ctl = True
                    L("ctl_adc:")
                L("    adc al, %s" % src, "    lahf")
                self.v_from_of()
            elif mn == "SBC":
                L("    sahf", "    cmc", "    sbb al, %s" % src, "    cmc", "    lahf")
                self.v_from_of()
            elif mn in ("CMP", "CPX", "CPY"):
                reg = {"CMP": "al", "CPX": "cl", "CPY": "ch"}[mn]
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
            self.write(mode, opnd, STORES[mn])
        elif mn in RMW:
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
                L("    mov dh, [bx]")
                self.shift(mn, "dh", True)
                L("    mov [bx], dh")
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
                if opnd <= pc:          # only a backward one can loop
                    L("    or bp, bp")
                self.to(opnd, check=opnd <= pc)
                return False
            # (ind): the pointer is in RAM or the ROM, with the NMOS page wrap
            ptr = opnd
            hi = (ptr & 0xFF00) | ((ptr + 1) & 0xFF)
            if 0xC000 <= ptr < 0xD000 or 0xC000 <= hi < 0xD000:
                L("    add bp, %d" % cyc, "    mov si, 0x%04X" % pc, "    jmp xa_resume")
                return False
            L("    mov bl, [0x%04X]" % ptr, "    mov bh, [0x%04X]" % hi,
              "    or bp, bp")
            self.rts_to()
            return False
        elif mn == "JSR":
            L("    mov dx, 0x%04X" % ((pc + 2) & 0xFFFF), "    call a_jsr")
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
            L("    call getp", "    or dl, 0x10")
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
        elif mn in ("CLV", "CLD"):
            L("    and byte [%s], 0x%02X" % (self.PV(), 0xFF ^ (0x40 if mn == "CLV" else 0x08)))
        elif mn == "NOP":
            pass
        else:
            raise ValueError(mn)
        return True                 # falls through to the next PC

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
        sig = [(0xB1, 0xB8E6), (0xB3, 0x02D0), (0xB5, 0xB9E6), (0xBA, 0x3AC9),
               (0xBC, 0x0AB0), (0xBE, 0x20C9), (0xC0, 0xEFF0), (0xC2, 0xE938),
               (0xC4, 0x3830), (0xC6, 0xD0E9)]
        for entry, lab in ((0xB1, "a_chrget"), (0xB7, "a_chrgot")):
            L("%s:" % lab)
            for a, w in sig:
                L("    cmp word [0x%04X], 0x%04X" % (a, w), "    jne .core")
            L("    cmp byte [0x00B7], 0xAD", "    jne .core",
              "    cmp byte [0x00C8], 0x60", "    jne .core",
              "    test byte [%s], 0x08" % PV, "    jnz .core")
            L("    jmp %s" % ("a_chrbody.inc" if entry == 0xB1 else "a_chrbody.lda"))
            L(".core: mov si, 0x%04X" % entry, "    jmp xa_resume")
        L("a_chrbody:",
          ".inc:",
          "    sub bp, 5",
          "    inc byte [0x00B8]",              # INC TXTPTR
          "    jz .hi",
          "    sub bp, 3",                      # BNE, taken
          "    jmp short .lda",
          ".hi: sub bp, 7",                     # BNE not (2), INC TXTPTR+1 (5)
          "    inc byte [0x00B9]",
          ".lda:",
          "    sub bp, 6",                      # LDA abs (4), CMP # (2)
          "    mov bx, [0x00B8]",
          "    cmp bh, 0xC0", "    jae .rd",
          "    mov al, [bx]",
          ".got:",
          "    cmp al, 0x3A", "    jae .ge",
          "    sub bp, 4",                      # BCS not (2), CMP # (2)
          "    cmp al, 0x20", "    jne .digit",
          "    sub bp, 3",                      # BEQ taken: the space loop
          "    jle .loopx",
          "    jmp .inc",
          ".loopx: mov si, 0x00B1", "    jmp xa_slice",
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
          ".rd: call ra_any",
          "    mov al, dh",
          "    jmp .got")

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
            nmn, nmode = decode(self.rom, nxt)[:2]
            if nmode == "rel" or nmn in ("PHP", "ADC", "SBC") or nmn in LEAVE \
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

    def emit(self):
        L = self.L
        p = self.p
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
          ".j: inc bx", "    or bp, bp")
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
        self.tail = []
        prev_falls = False
        last = None
        for pc in sorted(self.pcs):
            if prev_falls and last is not None and last != pc:
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
        return "\n".join(self.out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="apple2", choices=["apple2"])
    ap.add_argument("--rom", required=True)
    ap.add_argument("--seeds", required=True)
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--table", required=True, help="the PC table's binary")
    ap.add_argument("--range", help="LO:HI hex - translate this part of a 64KB "
                    "IMAGE instead of a ROM (a2cputest's Dormann)")
    a = ap.parse_args()
    prof = APPLE2
    if a.range:
        lo, hi = (int(v, 16) for v in a.range.split(":"))
        rom = Rom(open(a.rom, "rb").read()[lo:hi], lo)
        prof = dict(APPLE2, aot=(lo, hi), chrget=None)
        seeds = []
    else:
        rom = Rom(open(a.rom, "rb").read()[:0x3000], 0xD000)
        seeds = [rom.w(0xFFFC), rom.w(0xFFFE), rom.w(0xFFFA)]
    seeds += [int(l, 16) for l in open(a.seeds) if l.strip() and not l.startswith("#")]
    pcs = walk(rom, seeds)
    # THE KEYIN LOOP STAYS THE CORE'S: it runs it as an exact counter while
    # no key is latched (m65gen.py's kbd_idle), which no translation beats
    if not a.range:
        pcs -= {0xFD1B, 0xFD1D, 0xFD1F, 0xFD21, 0xFD24}
    else:
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
    src = Aot(prof, rom, pcs).emit()
    # ASSEMBLED HERE, for the symbol map the PC table is made from
    import re
    import subprocess
    bin_out = os.path.splitext(a.out)[0] + ".bin"
    map_out = os.path.splitext(a.out)[0] + ".map"
    with open(a.out, "w") as f:
        f.write("[map symbols %s]\n" % map_out + src)
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", bin_out, a.out], check=True)
    size = os.path.getsize(bin_out)
    if size > 0xFE00:
        raise SystemExit("m65aot: the translation is %d bytes and a part stops "
                         "at 65,024" % size)
    labs = {}
    for line in open(map_out):
        m = re.match(r"\s*([0-9A-F]+)\s+([0-9A-F]+)\s+p_([0-9A-F]{4})\s*$", line)
        if m:
            labs[int(m.group(3), 16)] = int(m.group(2), 16)
    tab = bytearray(2 * len(rom.data))
    for pc, off in labs.items():
        if off == 0:
            raise SystemExit("m65aot: a translation at offset 0 reads as none")
        tab[2 * (pc - rom.base):2 * (pc - rom.base) + 2] = off.to_bytes(2, "little")
    with open(a.table, "wb") as f:
        f.write(tab)
    print("m65aot: %d seeds, %d ROM PCs translated, %d bytes; the PC table %d "
          "entries -> %s, %s" % (len(seeds), len(pcs), size, len(labs), bin_out, a.table))


if __name__ == "__main__":
    sys.exit(main())

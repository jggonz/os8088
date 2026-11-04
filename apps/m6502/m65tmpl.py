#!/usr/bin/env python3
"""apps/m6502/m65tmpl.py - the C64 recompiler's TEMPLATES (C64-SPEC 4.9;
docs/plans/C64-RECOMPILER-PLAN.md wave 1)

    python3 apps/m6502/m65tmpl.py --check --selftest -o build/c64/c64tmpl.bin

Every legal opcode becomes a run of 8086 bytes the recompiler COPIES into its
code cache and PATCHES: the fast core's handler for it (m65gen.py), with the
operand bytes fetched through ES:SI made immediates, the dispatch taken off,
and the core's hand-backs turned into exits that resume the core AT THIS
INSTRUCTION - which then runs it exactly as it always did, hand-back and all.
So a translated block computes what the fast core computes, a template at a
time, and the fast core's gates hold the recompiler to it.

HOW THE PATCH SITES ARE FOUND. Every template is assembled twice: once at
origin 0 with one set of SENTINEL values for the things only the translator
knows (the operand, this PC, the next PC, a branch's target...), once at
origin $1111 with another set. A byte that differs is a site: the sentinel it
holds says which, and a word that moved by exactly -$1111 is a near jump or
call into the PRELUDE, which the translator relocates. Any other difference,
or a template whose length differs between the two, fails the build - so a
site cannot be missed by hand.

THE PRELUDE is the code every block shares, at a fixed place in the cache
segment: the doors back into the core (xa_resume, xa_slice, xa_go), the
core's memory paths (rd_hi, wr_hi), its flag packers (getp, setp) and the
damage mark, which here also tests the CODE map - a store into a page holding
translated code leaves the block at the next instruction (jm_hit).

THE CONVENTIONS ARE THE CORE'S (m65gen.py's header) with SI free: the PC is
where the code is, and SI is loaded only on the way out. Each template takes
its own cycles off BP - at its end, as the core's handler does, or before the
damage mark where it stores - and the block's head tests the whole block's
worst case before it runs (HEAD), so the run ends exactly where the core's
would. DL is 0 between templates.

    --check      every legal opcode is a template, a terminator, or the
                 core's; every site accounted for (always done; this flag
                 prints the census)
    --selftest   patch every template for random operands, PCs and cache
                 offsets, and compare it with nasm's own assembly of that
                 template at that offset with those values - the translator's
                 arithmetic, proven on every opcode
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from m65gen import (OPS, PENALTY, READS, RMW, STORES, REG, C64, Gen,  # noqa: E402
                    XR_RESUME, XR_SLICE, S_DIRTY, S_ANY, S_LASTPG, S_IRQ,
                    S_WLO, S_WHI, S_WATLO, S_WATHI)
from m65aot import LEN                                                 # noqa: E402

# --- the cache segment (C64-SPEC 4.9.1) -------------------------------------
JC_CODE = 0x0000        # 32 bytes: the CODE map, a bit a page (DIRTY's layout)
JC_FLUSH = 0x0020       # nonzero: a write found code - flush before the next lookup
JC_PRE = 0x0100         # the prelude
F_CORE = 38             # the frame's word for the core's segment (C64-SPEC 4.8)

# --- the site kinds, as the translator reads them ---------------------------
K_OPB, K_OPW, K_PC, K_NEXT, K_TARGET, K_RET, K_TCYC, K_COST, K_REL = range(1, 10)
KNAME = {K_OPB: "OPB", K_OPW: "OPW", K_PC: "PC", K_NEXT: "NEXT",
         K_TARGET: "TARGET", K_RET: "RET", K_TCYC: "TCYC", K_COST: "COST",
         K_REL: "REL"}
WORD_KINDS = (K_OPW, K_PC, K_NEXT, K_TARGET, K_RET, K_COST, K_REL)
# the two sentinel sets: every byte differs between them, and no two values
# of one set share a byte pattern a site could be mistaken for
SENT = {
    "A": dict(OPB=0x5A, OPW=0x9A5B, PC=0xA35C, NEXT=0xB45D, TARGET=0xC55E,
              RET=0xD65F, TCYC=0x13, COST=0x7160),
    "B": dict(OPB=0x2C, OPW=0x6E1D, PC=0x581E, NEXT=0x4A2F, TARGET=0x3B4D,
              RET=0x2C6E, TCYC=0x24, COST=0x1E7F),
}
ORG = {"A": 0x0000, "B": 0x1111}

# --- what the translator does with each opcode ------------------------------
# KIND: a template run in line, one that ENDS the block (it leaves through
# xa_go), or the core's (the block ends before it, resuming the core there)
KD_LINE, KD_TERM, KD_CORE = 1, 2, 3
# SEL: when the translator may take variant 1, the fast one, for the operand
# it sees - the rule is variant(); variant 0 is right for any operand
SEL_NONE, SEL_ABSRD, SEL_ABSWR, SEL_ABSRMW, SEL_IDXRD, SEL_IDXWR, SEL_IDXRMW, \
    SEL_ZPWR, SEL_INDY = range(9)
CORE_ONLY = {"BRK", "RTI"}              # ...and JMP (ind), below


def ram(a):
    """RAM under every bank map: below $A000, and $C000-$CFFF"""
    return a < 0xA000 or 0xC000 <= a < 0xD000


def selector(op):
    mn, mode, cyc = OPS[op]
    if mode == "abs" and mn in READS:
        return SEL_ABSRD
    if mode == "abs" and mn in STORES:
        return SEL_ABSWR
    if mode == "abs" and mn in RMW:
        return SEL_ABSRMW
    if mode in ("absx", "absy"):
        return SEL_IDXRD if mn in READS else SEL_IDXWR if mn in STORES else SEL_IDXRMW
    if mode == "zp" and (mn in STORES or mn in RMW):
        return SEL_ZPWR
    if mode == "indy":
        return SEL_INDY
    return SEL_NONE


def variant(op, opnd):
    """the template the translator copies for OP with this operand: 1 where
    the operand alone proves the fast path (RAM, never the processor port,
    never a device), else 0. THE TRANSLATOR'S RULE - its 8086 form follows
    this one, and the self-test holds both templates to the operand"""
    s = selector(op)
    top = opnd + 255
    if s == SEL_ABSRD:
        return int(ram(opnd))
    if s == SEL_ABSWR:
        return int(2 <= opnd < 0xD000)
    if s == SEL_ABSRMW:
        return int(opnd >= 2 and ram(opnd))
    if s == SEL_IDXRD:
        return int(top < 0xA000 or (opnd >= 0xC000 and top < 0xD000))
    if s == SEL_IDXWR:
        return int(opnd >= 2 and top < 0xD000)
    if s == SEL_IDXRMW:
        return int(opnd >= 2 and (top < 0xA000 or (opnd >= 0xC000 and top < 0xD000)))
    if s == SEL_ZPWR:
        return int(opnd >= 2)
    if s == SEL_INDY:
        return int(opnd not in (0x00, 0x01, 0xFF))
    return 0


def kind(op):
    mn, mode, cyc = OPS[op]
    if mn in CORE_ONLY or (mn == "JMP" and mode == "ind"):
        return KD_CORE
    if mode == "rel" or mn in ("JMP", "JSR", "RTS"):
        return KD_TERM
    return KD_LINE


def maxcyc(op):
    """the most OP can take, for the block's head: its cycles, a page
    crossed, a branch taken across one"""
    mn, mode, cyc = OPS[op]
    return cyc + (1 if mode in PENALTY and mn in READS else 0) + \
        (2 if mode == "rel" else 0)


class T(Gen):
    """the core's generator, writing templates: its flag and ALU code is
    used as it is (alu, shift, nz, v_from_of, push, pop)"""

    def __init__(self):
        Gen.__init__(self, C64)
        self.cold = []
        self.n = 0

    def lab(self):
        self.n += 1
        return ".t%d" % self.n

    # --- leaving ---------------------------------------------------------
    def bail(self):
        """a cold exit that resumes the core AT this instruction, its cycles
        untaken: the core then runs it as it always does"""
        b = self.lab()
        self.cold.append(["%s: mov si, strict word PC" % b, "    jmp near xa_resume"])
        return b

    def end(self, cyc):
        """the instruction's cycles, its last act"""
        self.L("    sub bp, %d" % cyc)

    def end_called(self, cyc):
        """...after a call into a device, which may have cut the run
        (c64_cut): the countdown tested, as the core's next() does"""
        x = self.lab()
        self.L("    sub bp, %d" % cyc, "    jg %s" % x,
               "    mov si, strict word NEXT", "    jmp near xa_slice", "%s:" % x)

    # --- addressing ------------------------------------------------------
    def ea_t(self, mode, penalty):
        L = self.L
        if mode in ("zpx", "zpy"):
            L("    mov bl, strict byte OPB", "    add bl, %s" % ("cl" if mode == "zpx" else "ch"),
              "    xor bh, bh")
        elif mode in ("absx", "absy"):
            L("    mov bx, strict word OPW", "    add bl, %s" % ("cl" if mode == "absx" else "ch"))
            self.carry(penalty)
        elif mode == "indx":
            b = self.bail()
            L("    mov bl, strict byte OPB", "    add bl, cl",
              "    cmp bl, 2", "    jb %s" % b, "    cmp bl, 0xFF", "    je %s" % b,
              "    xor bh, bh",
              "    mov dl, [bx]", "    inc bl", "    mov bh, [bx]",
              "    mov bl, dl", "    xor dl, dl")
        elif mode == "indy":
            if self.var:
                # (variant 1: the pointer is not $FF, $00 or $01 - one word)
                L("    mov bx, [OPW]")
            else:
                b = self.bail()
                L("    mov bx, strict word OPW",
                  "    cmp bl, 2", "    jb %s" % b, "    cmp bl, 0xFF", "    je %s" % b,
                  "    mov dl, [bx]", "    inc bl", "    mov bh, [bx]",
                  "    mov bl, dl", "    xor dl, dl")
            L("    add bl, ch")
            self.carry(penalty)
        else:
            raise ValueError(mode)

    def carry(self, penalty):
        if penalty:
            k = self.lab()
            self.L("    jnc %s" % k, "    inc bh", "    dec bp", "%s:" % k)
        else:
            self.L("    adc bh, 0")

    # --- memory ----------------------------------------------------------
    def read_t(self, mode, dst, cyc):
        """DST = the operand; returns whether a device may have been called"""
        L = self.L
        if mode == "imm":
            L("    mov %s, strict byte OPB" % dst)
            return False
        if mode == "zp":
            L("    mov %s, [OPW]" % dst)
            return False
        if mode == "abs" and self.var:
            L("    mov %s, [OPW]" % dst)
            return False
        if mode == "abs":
            L("    mov bx, strict word OPW")
        else:
            self.ea_t(mode, mode in PENALTY)
        if mode in ("zpx", "zpy") or (self.var and mode in ("absx", "absy")):
            L("    mov %s, [bx]" % dst)
            return False
        # THE CORE'S LADDER: RAM below $A000 in line, the rest rd_hi's
        hi, k = self.lab(), self.lab()
        L("    cmp bh, 0xA0", "    jae %s" % hi, "    mov %s, [bx]" % dst, "%s:" % k)
        self.cold.append(["%s: mov dl, %d" % (hi, cyc), "    call near rd_hi", "    xor dl, dl"]
                         + (["    mov %s, dh" % dst] if dst != "dh" else [])
                         + ["    jmp %s" % k])
        return True

    def write_t(self, mode, src, cyc):
        """the byte SRC to the operand, and the instruction's end: the damage
        mark - and with it the CODE test - is the last thing it does"""
        L = self.L
        if mode == "zp":
            if self.var:
                L("    mov [OPW], %s" % src)
            else:
                L("    mov bx, strict word OPW",
                  "    cmp bl, 2", "    jb %s" % self.bail(), "    mov [bx], %s" % src)
            self.end(cyc)
            return
        if mode in ("zpx", "zpy"):
            self.ea_t(mode, False)
            L("    cmp bl, 2", "    jb %s" % self.bail(), "    mov [bx], %s" % src)
            self.end(cyc)
            return
        if mode == "abs" and self.var:
            L("    mov [OPW], %s" % src)
            self.end(cyc)
            L("    mov bx, strict word OPW", "    mov si, strict word NEXT",
              "    call near jm_mark")
            return
        if mode == "abs":
            L("    mov bx, strict word OPW")
        else:
            self.ea_t(mode, False)
        if self.var and mode in ("absx", "absy"):
            L("    mov [bx], %s" % src)
            self.end(cyc)
            L("    mov si, strict word NEXT", "    call near jm_mark")
            return
        wh, k = self.lab(), self.lab()
        L("    cmp bx, 2", "    jb %s" % self.bail(),
          "    cmp bh, 0xD0", "    jae %s" % wh,
          "    mov [bx], %s" % src)
        self.end(cyc)
        L("    mov si, strict word NEXT", "    call near jm_mark", "%s:" % k)
        x = self.lab()
        self.cold.append(["%s:" % wh] + (["    mov dh, %s" % src] if src != "dh" else [])
                         + ["    mov dl, %d" % cyc, "    mov si, strict word NEXT",
                            "    call near wr_hi", "    xor dl, dl",
                            "    sub bp, %d" % cyc, "    jg %s" % k,
                            "%s: jmp near xa_slice" % x])

    # --- one template ----------------------------------------------------
    def template(self, op, var):
        """the lines of OP's template, variant VAR"""
        mn, mode, cyc = OPS[op]
        self.out, self.cold, self.var = [], [], var
        L = self.L
        if mn in ("ADC", "SBC"):
            L("    test byte [%s], 0x08" % self.PV(), "    jnz %s" % self.bail())
        if mn in ("CLI", "SEI", "PLP"):
            L("    cmp byte [%s], 0" % self.scr(S_IRQ), "    jne %s" % self.bail())
        called = False
        if mn in READS:
            dst = {"LDA": "al", "LDX": "cl", "LDY": "ch"}.get(mn, "dh")
            called = self.read_t(mode, dst, cyc)
            self.alu(mn, dst)
            (self.end_called if called else self.end)(cyc)
        elif mn in STORES:
            self.write_t(mode, STORES[mn], cyc)
        elif mn in RMW and mode == "acc":
            self.shift(mn, "al")
            self.end(cyc)
        elif mn in RMW and mode in ("zp", "zpx"):
            if mode == "zp" and var:
                L("    mov bx, strict word OPW")
            elif mode == "zp":
                L("    mov bx, strict word OPW", "    cmp bl, 2", "    jb %s" % self.bail())
            else:
                self.ea_t(mode, False)
                L("    cmp bl, 2", "    jb %s" % self.bail())
            if mn in ("INC", "DEC"):
                L("    sahf", "    %s byte [bx]" % mn.lower(), "    lahf")
            elif mn in ("ASL", "LSR"):
                L("    %s byte [bx], 1" % ("shl" if mn == "ASL" else "shr"), "    lahf")
            else:
                L("    mov dh, [bx]")
                self.shift(mn, "dh")
                L("    mov [bx], dh")
            self.end(cyc)
        elif mn in RMW:
            # RAM: in line. Anything else is the core's (C64-SPEC 4.7: its
            # read-modify-write above $A000 reads through the bank map, and
            # I/O is the accurate core's dummy write)
            if mode == "abs":
                L("    mov bx, strict word OPW")
            else:
                self.ea_t(mode, False)
            if not var:
                b, r = self.bail(), self.lab()
                L("    cmp bx, 2", "    jb %s" % b,
                  "    cmp bh, 0xA0", "    jb %s" % r,
                  "    cmp bh, 0xC0", "    jb %s" % b,
                  "    cmp bh, 0xD0", "    jae %s" % b,
                  "%s:" % r)
            L("    mov dh, [bx]")
            self.shift(mn, "dh")
            L("    mov [bx], dh")
            self.end(cyc)
            L("    mov si, strict word NEXT", "    call near jm_mark")
        elif mode == "rel":
            self.branch_t(mn)
        else:
            self.misc_t(mn, mode, cyc)
        return self.close()

    def close(self):
        out = self.out
        if self.cold:
            k = self.lab()
            if not out or not re.match(r"\s+jmp\s+near\s", out[-1]):
                out.append("    jmp short %s" % k)
            for blk in self.cold:
                out.extend(blk)
            out.append("%s:" % k)
        return out

    def branch_t(self, mn):
        reg, mask, not_taken = {
            "BPL": ("ah", 0x80, "jnz"), "BMI": ("ah", 0x80, "jz"),
            "BVC": ("pv", 0x40, "jnz"), "BVS": ("pv", 0x40, "jz"),
            "BCC": ("ah", 0x01, "jnz"), "BCS": ("ah", 0x01, "jz"),
            "BNE": ("ah", 0x40, "jnz"), "BEQ": ("ah", 0x40, "jz")}[mn]
        nt = self.lab()
        where = "byte [%s]" % self.PV() if reg == "pv" else "ah"
        # (TCYC is the taken branch's cycles, 3 or 4 by the target's page)
        self.L("    test %s, 0x%02X" % (where, mask), "    %s %s" % (not_taken, nt),
               "    sub bp, strict byte TCYC", "    mov si, strict word TARGET",
               "    jmp near xa_go",
               "%s: sub bp, 2" % nt, "    mov si, strict word NEXT", "    jmp near xa_go")

    def misc_t(self, mn, mode, cyc):
        L = self.L
        if mn == "JMP":
            L("    sub bp, %d" % cyc, "    mov si, strict word TARGET", "    jmp near xa_go")
        elif mn == "JSR":
            slow, j = self.lab(), self.lab()
            L("    cmp di, 0x0101", "    jb %s" % slow,
              "    mov word [di-1], strict word RET", "    sub di, 2",
              "%s: sub bp, %d" % (j, cyc), "    mov si, strict word TARGET",
              "    jmp near xa_go",
              "%s: mov dx, strict word RET" % slow)
            self.push("dh")
            self.push("dl")
            L("    xor dl, dl", "    jmp short %s" % j)
        elif mn == "RTS":
            slow, j = self.lab(), self.lab()
            L("    cmp di, 0x01FE", "    jae %s" % slow,
              "    mov si, [di+1]", "    add di, 2",
              "%s: inc si" % j, "    sub bp, %d" % cyc, "    jmp near xa_go",
              "%s:" % slow)
            self.pop("bl")
            self.pop("bh")
            L("    mov si, bx", "    jmp short %s" % j)
        elif mn == "PHA":
            self.push("al")
            self.end(cyc)
        elif mn == "PHP":
            L("    call near jgetp", "    or dl, 0x10")
            self.push("dl")
            L("    xor dl, dl")
            self.end(cyc)
        elif mn == "PLA":
            self.pop("al")
            self.nz("al")
            self.end(cyc)
        elif mn == "PLP":
            self.pop("dl")
            L("    call near jsetp", "    xor dl, dl")
            self.end(cyc)
        elif mn in ("TAX", "TAY", "TXA", "TYA"):
            d, s = REG[mn[2]], REG[mn[1]]
            L("    mov %s, %s" % (d, s))
            self.nz(d)
            self.end(cyc)
        elif mn == "TSX":
            L("    mov bx, di", "    mov cl, bl")
            self.nz("cl")
            self.end(cyc)
        elif mn == "TXS":
            L("    mov bl, cl", "    mov bh, 1", "    mov di, bx")
            self.end(cyc)
        elif mn in ("INX", "INY", "DEX", "DEY"):
            r = REG[mn[2]]
            L("    sahf", "    %s %s" % ("inc" if mn[0] == "I" else "dec", r), "    lahf")
            self.end(cyc)
        elif mn in ("CLC", "SEC"):
            L("    and ah, 0xFE" if mn == "CLC" else "    or ah, 0x01")
            self.end(cyc)
        elif mn in ("CLV", "CLD", "SED", "CLI", "SEI"):
            bit = {"CLV": 0x40, "CLD": 0x08, "SED": 0x08, "CLI": 0x04, "SEI": 0x04}[mn]
            if mn.startswith("CL"):
                L("    and byte [%s], 0x%02X" % (self.PV(), 0xFF ^ bit))
            else:
                L("    or byte [%s], 0x%02X" % (self.PV(), bit))
            self.end(cyc)
        elif mn == "NOP":
            self.end(cyc)
        else:
            raise ValueError(mn)

    # --- the specials ----------------------------------------------------
    def special(self, name):
        self.out, self.cold, self.var = [], [], 0
        L = self.L
        if name == "HEAD":
            # THE BLOCK'S WORST CASE, tested before it runs: a budget that
            # cannot cover it leaves the block to the core, which runs it an
            # instruction at a time and ends the run where it must
            k = self.lab()
            L("    cmp bp, strict word COST", "    jg %s" % k,
              "    mov si, strict word PC", "    jmp near xa_resume", "%s:" % k)
        elif name == "XCORE":
            # the block ends at an instruction that is the core's
            L("    mov si, strict word PC", "    jmp near xa_resume")
        elif name == "XGO":
            # ...or at its length limit, going on at PC
            L("    mov si, strict word PC", "    or bp, bp", "    jmp near xa_go")
        else:
            raise ValueError(name)
        return self.close()

    # --- the prelude -----------------------------------------------------
    def prelude(self):
        """the shared code at JC_PRE: assembled once, copied in once"""
        P, S = self.PV(), self.scr
        self.out = []
        L = self.L
        L("xa_resume:",                 # SI = the PC the core runs next
          "    xor dl, dl",
          "    mov bx, sp",
          "    push word [ss:bx+%d]" % F_CORE,
          "    mov bx, 0x%04X" % XR_RESUME, "    push bx", "    retf",
          "xa_slice:",                  # SI = the PC, the budget spent
          "    xor dl, dl",
          "    mov bx, sp",
          "    push word [ss:bx+%d]" % F_CORE,
          "    mov bx, 0x%04X" % XR_SLICE, "    push bx", "    retf",
          # xa_go - control goes to SI, the countdown's flags just set: the
          # run's end, or (wave 2) the block there, or the core
          "xa_go:",
          "    jle xa_slice",
          "    jmp short xa_resume")
        L(*[l.replace("jmp c64_mark", "jmp jm_mark") for l in self.c64_io()])
        # (renamed: SETP is a 386 mnemonic, which an EQU cannot be named)
        L(*[l.replace("getp:", "jgetp:").replace("setp:", "jsetp:")
            for l in self.tails()["pflags"]])
        # jm_mark - the damage mark (m65gen.py's c64_mark, the same bits)
        # AND THE CODE TEST. BX was just written, SI is the next PC, DL the
        # cycles of this instruction not yet taken (0 but from wr_hi). A page
        # whose CODE bit is set asks for the flush and leaves the block at SI
        # - the store done, the window kept - after the mark's whole work.
        L("jm_mark:",
          "    cmp byte [%s], 2" % S(S_ANY), "    jne .slow",
          "    cmp bh, [%s]" % S(S_LASTPG), "    je .win",
          ".slow:",
          "    push bx", "    push dx",
          "    mov dl, bh", "    and dl, 7",
          "    mov dh, 0x80",
          "    xchg cl, dl", "    shr dh, cl", "    xchg cl, dl",
          "    mov bl, bh", "    xor bh, bh",
          "    shr bl, 1", "    shr bl, 1", "    shr bl, 1",
          "    or [bx + SCRD + 0x%02X], dh" % S_DIRTY,
          "    test [cs:bx + 0x%04X], dh" % JC_CODE,
          "    pop dx", "    pop bx",
          "    mov byte [%s], 2" % S(S_ANY),
          "    mov [%s], bh" % S(S_LASTPG),
          "    jnz .hit",
          ".win:")
        win = ["    cmp bx, [%s]" % S(S_WATLO), "    jb .out",
               "    cmp bx, [%s]" % S(S_WATHI), "    ja .out",
               "    cmp bx, [%s]" % S(S_WLO), "    jae .nolo",
               "    mov [%s], bx" % S(S_WLO),
               ".nolo:",
               "    cmp bx, [%s]" % S(S_WHI), "    jbe .out",
               "    mov [%s], bx" % S(S_WHI),
               ".out:"]
        L(*win, "    ret")
        L(".hit:",
          # (the mark's flags were the test's: MOVs keep them)
          "    mov byte [cs:0x%04X], 1" % JC_FLUSH,
          *[w.replace(".out", ".hout").replace(".nolo", ".hnolo") for w in win],
          "    add sp, 2",                  # this block is not returned to
          "    push ax", "    mov al, dl", "    xor ah, ah",
          "    sub bp, ax", "    pop ax",     # the cycles wr_hi had not taken
          "    xor dl, dl",
          "    or bp, bp",
          "    jle xa_slice",
          "    jmp near xa_resume")
        return self.out


# --- assembling -------------------------------------------------------------
HDR = ["cpu 8086", "bits 16"]


def nasm(src, tag):
    with tempfile.TemporaryDirectory() as d:
        a, b, m = (os.path.join(d, x) for x in ("t.asm", "t.bin", "t.map"))
        with open(a, "w") as f:
            f.write("[map symbols %s]\n" % m + src)
        r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", b, a],
                           capture_output=True, text=True)
        if r.returncode:
            raise SystemExit("m65tmpl: nasm failed on %s:\n%s" % (tag, r.stderr))
        syms = {}
        for line in open(m):
            mm = re.match(r"\s*([0-9A-F]+)\s+([0-9A-F]+)\s+(\S+)\s*$", line)
            if mm:
                syms[mm.group(3)] = int(mm.group(2), 16)
        return open(b, "rb").read(), syms


def scr_equs():
    return ["SCR  equ 0x%04X" % C64["scr"], "SCRD equ 0x%04X - 0x10000" % C64["scr"]]


def build_prelude():
    t = T()
    body = t.prelude()
    src = "\n".join(HDR + ["org 0x%04X" % JC_PRE] + scr_equs() + body) + "\n"
    data, syms = nasm(src, "the prelude")
    labs = {k: v for k, v in syms.items() if re.match(r"^[a-z_]+$", k)}
    return data, labs


def templates():
    """[(key, lines)]: key = (op, var), or a special's name"""
    t = T()
    out = []
    for op in sorted(OPS):
        k = kind(op)
        if k == KD_CORE:
            continue
        out.append(((op, 0), t.template(op, 0)))
        if selector(op) != SEL_NONE:
            out.append(((op, 1), t.template(op, 1)))
    for name in ("HEAD", "XCORE", "XGO"):
        out.append((name, t.special(name)))
    return out


def tname(key):
    return "tp_%s" % (key if isinstance(key, str) else "%02X_%d" % key)


def assemble(tpls, labs, org, sent):
    """every template in one source, at ORG, with these sentinel values"""
    lines = HDR + ["org 0x%04X" % org] + scr_equs()
    lines += ["%s equ 0x%04X" % (k, v) for k, v in sorted(labs.items())]
    lines += ["%%define %s 0x%04X" % (k, v) for k, v in sent.items()]
    for key, body in tpls:
        lines += ["%s:" % tname(key)] + body + ["%s_e:" % tname(key)]
    data, syms = nasm("\n".join(lines) + "\n", "the templates")
    return data, syms


def find_sites(tpls, labs):
    """(key, bytes at offset 0 normalised, [(pos, kind)]) for every template"""
    a, sa = assemble(tpls, labs, ORG["A"], SENT["A"])
    b, sb = assemble(tpls, labs, ORG["B"], SENT["B"])
    rows = []
    for key, _ in tpls:
        n = tname(key)
        ta, tb = sa[n] - ORG["A"], sb[n] - ORG["B"]
        la, lb = sa[n + "_e"] - sa[n], sb[n + "_e"] - sb[n]
        if la != lb:
            raise SystemExit("m65tmpl: %s is %d bytes at one origin and %d at the "
                             "other - a site changed an encoding" % (n, la, lb))
        if la > 255:
            raise SystemExit("m65tmpl: %s is %d bytes; a site's offset is a byte" % (n, la))
        x, y = bytearray(a[ta:ta + la]), b[tb:tb + lb]
        sites, i = [], 0
        while i < la:
            if x[i] == y[i]:
                i += 1
                continue
            found = None
            if i + 1 < la:
                wa, wb = x[i] | x[i + 1] << 8, y[i] | y[i + 1] << 8
                for kd in (K_OPW, K_PC, K_NEXT, K_TARGET, K_RET, K_COST):
                    nm = KNAME[kd]
                    if wa == SENT["A"][nm] and wb == SENT["B"][nm]:
                        found = (kd, 2)
                if found is None and (wa - wb) & 0xFFFF == (ORG["B"] - ORG["A"]) & 0xFFFF:
                    found = (K_REL, 2)
                    # NORMALISED: as if the template began at offset 0, so
                    # the translator subtracts where it actually put it
                    x[i:i + 2] = ((wa + ta) & 0xFFFF).to_bytes(2, "little")
            if found is None:
                for kd in (K_OPB, K_TCYC):
                    if x[i] == SENT["A"][KNAME[kd]] and y[i] == SENT["B"][KNAME[kd]]:
                        found = (kd, 1)
            if found is None:
                raise SystemExit("m65tmpl: %s differs at +%d (%02X/%02X) and no site "
                                 "explains it" % (n, i, x[i], y[i]))
            sites.append((i, found[0]))
            if found[0] != K_REL:
                x[i:i + found[1]] = bytes(found[1])       # (a site holds 0)
            i += found[1]
        rows.append((key, bytes(x), sites))
    return rows


# --- the part ---------------------------------------------------------------
# 0  dw "JT", version
# 4  dw the prelude's offset, its length (copied to JC_PRE in the cache)
# 8  dw the opcode table: 256 x 8 bytes - db 6502 length, KIND, SEL, the
#       worst-case cycles; dw variant 0's record, variant 1's (0 = none)
# 10 dw HEAD's record, XCORE's, XGO's
# a record: dw its bytes' offset; db their length; db the sites; then a site
#       a pair, db its offset in the template, db its KIND
VERSION = 1


def pack(prelude, rows):
    recs = {}
    blob = bytearray(16)
    blob[16:16] = prelude
    pre_off = 16
    optab = len(blob)
    blob += bytes(256 * 8)
    for key, data, sites in rows:
        roff = len(blob)
        boff = roff + 4 + 2 * len(sites)
        blob += boff.to_bytes(2, "little") + bytes([len(data), len(sites)])
        for pos, kd in sites:
            blob += bytes([pos, kd])
        blob += data
        recs[key] = roff
    for op in range(256):
        e = optab + op * 8
        if op not in OPS:
            blob[e:e + 8] = bytes([1, KD_CORE, 0, 0, 0, 0, 0, 0])
            continue
        mn, mode, cyc = OPS[op]
        r0, r1 = recs.get((op, 0), 0), recs.get((op, 1), 0)
        blob[e:e + 8] = bytes([LEN[mode], kind(op), selector(op), maxcyc(op)]) + \
            r0.to_bytes(2, "little") + r1.to_bytes(2, "little")
    hdr = b"JT" + VERSION.to_bytes(2, "little") + pre_off.to_bytes(2, "little") + \
        len(prelude).to_bytes(2, "little") + optab.to_bytes(2, "little") + \
        b"".join(recs[n].to_bytes(2, "little") for n in ("HEAD", "XCORE", "XGO"))
    blob[0:len(hdr)] = hdr
    if len(blob) > 0xFE00:
        raise SystemExit("m65tmpl: %d bytes, and a part stops at 65,024" % len(blob))
    return bytes(blob)


def check(rows):
    """every legal opcode is the core's, a terminator or in line - with a
    variant 0, and a variant 1 exactly where it has a selector"""
    have = {k for k, _, _ in rows}
    for op in OPS:
        k = kind(op)
        if k == KD_CORE:
            assert (op, 0) not in have
            continue
        assert (op, 0) in have, "$%02X has no template" % op
        assert ((op, 1) in have) == (selector(op) != SEL_NONE), "$%02X" % op
    for key, data, sites in rows:
        kinds = [kd for _, kd in sites]
        if isinstance(key, tuple):
            mn, mode, cyc = OPS[key[0]]
            ops = kinds.count(K_OPB) + kinds.count(K_OPW)
            want = 0 if mode in ("imp", "acc") or mn in ("RTS",) else 1
            if mode == "rel":
                want = 0                       # (the target says it all)
            if mn in ("JMP", "JSR"):
                want = 0
            assert (ops > 0) == bool(want), "%s: operand sites %s" % (tname(key), kinds)


# --- the self-test: patched == assembled ------------------------------------
def patch(data, sites, at, vals):
    """the translator's arithmetic, in Python"""
    out = bytearray(data)
    for pos, kd in sites:
        if kd == K_REL:
            w = (out[pos] | out[pos + 1] << 8) - at
            out[pos:pos + 2] = (w & 0xFFFF).to_bytes(2, "little")
        elif kd in WORD_KINDS:
            out[pos:pos + 2] = (vals[KNAME[kd]] & 0xFFFF).to_bytes(2, "little")
        else:
            out[pos] = vals[KNAME[kd]] & 0xFF
    return bytes(out)


def selftest(tpls, rows, labs, seed=8088):
    rng = random.Random(seed)
    for trial in range(3):
        at = rng.randrange(0x1000, 0xF000)
        vals = dict(OPB=rng.randrange(256), OPW=rng.randrange(0x10000),
                    PC=rng.randrange(0x10000), NEXT=rng.randrange(0x10000),
                    TARGET=rng.randrange(0x10000), RET=rng.randrange(0x10000),
                    TCYC=rng.choice((3, 4)), COST=rng.randrange(0x8000))
        # each template at its own offset AT in a source of its own would be
        # 400 assemblies: instead every template at AT + its place, and the
        # same offsets given to the patcher
        data, syms = assemble(tpls, labs, at, vals)
        for key, tb, sites in rows:
            n = tname(key)
            off = syms[n]
            want = data[off - at:off - at + len(tb)]
            got = patch(tb, sites, off, vals)
            if got != want:
                raise SystemExit("m65tmpl: self-test: %s patched at $%04X is not what "
                                 "nasm assembles there" % (n, off))
    return 3 * len(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out")
    ap.add_argument("--inc", help="the equates the translator assembles against")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    prelude, labs = build_prelude()
    tpls = templates()
    rows = find_sites(tpls, labs)
    check(rows)
    if a.check:
        nsite = sum(len(s) for _, _, s in rows)
        core = sorted(op for op in OPS if kind(op) == KD_CORE)
        print("m65tmpl: %d legal opcodes - %d in line, %d end a block, %d the core's "
              "(%s); %d templates, %d sites, prelude %d bytes"
              % (len(OPS), sum(kind(o) == KD_LINE for o in OPS),
                 sum(kind(o) == KD_TERM for o in OPS), len(core),
                 " ".join("$%02X" % o for o in core), len(rows), nsite, len(prelude)))
    if a.selftest:
        print("m65tmpl: self-test - %d templates at 3 offsets each, patched, match "
              "nasm" % (selftest(tpls, rows, labs) // 3))
    if a.out:
        blob = pack(prelude, rows)
        with open(a.out, "wb") as f:
            f.write(blob)
        print("m65tmpl: %s, %d bytes" % (a.out, len(blob)))
    if a.inc:
        with open(a.inc, "w") as f:
            f.write("; GENERATED by apps/m6502/m65tmpl.py - do not edit\n")
            for nm, v in (("JC_CODE", JC_CODE), ("JC_FLUSH", JC_FLUSH), ("JC_PRE", JC_PRE),
                          ("JT_VERSION", VERSION)):
                f.write("%s equ 0x%04X\n" % (nm, v))
            for kd, nm in KNAME.items():
                f.write("JK_%s equ %d\n" % (nm, kd))
            for nm, v in (("KD_LINE", KD_LINE), ("KD_TERM", KD_TERM), ("KD_CORE", KD_CORE)):
                f.write("%s equ %d\n" % (nm, v))
            for i, nm in enumerate(("NONE", "ABSRD", "ABSWR", "ABSRMW", "IDXRD", "IDXWR",
                                    "IDXRMW", "ZPWR", "INDY")):
                f.write("SEL_%s equ %d\n" % (nm, i))
            for nm in sorted(labs):
                f.write("JP_%s equ 0x%04X\n" % (nm, labs[nm]))


if __name__ == "__main__":
    sys.exit(main())

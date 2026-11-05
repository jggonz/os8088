#!/usr/bin/env python3
"""apps/m6502/m65gen.py - the FAST 6502 core's generator (docs/plans/completed/FAST6502-PLAN.md)

    python3 apps/m6502/m65gen.py --machine apple2 -o build/apple2/a2fast.asm
    python3 apps/m6502/m65gen.py --machine c64 -o build/c64/c64fast.asm

Writes one NASM source for a flat segment: the handler for opcode X at
X x 256, dispatched by `mov dh,[si] / inc si / jmp dx` with DL = 0, and the
shared tails (the entry, the exits, the I/O call-outs, the damage marker) in
the slots of opcodes this core does not run. Nothing it writes is committed.
APPLE2-SPEC section 4.6 and C64-SPEC section 4.7 are the contracts.

THE REGISTER PLAN:
    A = AL    N, Z, C in AH, lahf's layout (N bit 7, Z bit 6, C bit 0)
    X = CL    Y = CH    PC = SI    S = DI as $0100+S
    BX = the effective address    DH = data    DL = 0 at every dispatch
    BP = the cycle countdown       DS = the guest's 64KB
    ES = the segment PC is fetched through (the C64's: DS on the Apple II)
    V, D, I and B in the flag byte PV, at their P positions

THE FRAME. Everything the core keeps that is not the 6502 is on the caller's
stack, so the core never writes its own segment (which kern_mega gives it as
a read-only code selector). At handler level, SP = F:
    F+0 the machine record     F+2 the package's DS   F+4..F+10 saved regs
    F+12 the far return        F+16 the read door     F+20 the write door
    F+24.. the profile's own words (the C64's fetch segment per region)
The package pushes F+24.. and the doors before the far call; `retf` drops them.

EVERY HANDLER ENDS `sub bp,cycles / jle exit / mov dh,[si] / inc si / jmp dx`,
so the run stops before the instruction that finds the budget spent - the
accurate core's rule exactly, and `ran = asked - cnt` is exact.

WHAT IT DOES NOT RUN IT HANDS BACK: reason 2 (x_hb1) runs ONE instruction on
the accurate core - every illegal opcode, BRK, decimal ADC/SBC, and the
profile's own cases; reason 4 (x_hbi) is the interrupt flag's - CLI, SEI, PLP
or RTI while the interrupt byte is up, or an interrupt pending at entry - and
the accurate core gets a short run, so its one-instruction delays hold; reason
3 (x_hbr) is a REGION, a PC this core cannot fetch from.
"""
import argparse
import sys

# ---------------------------------------------------------------------------
# The legal NMOS opcodes: op -> (mnemonic, mode, base cycles). Every other
# opcode is handed back. Cycles are the accurate cores' table (a2_cyc,
# c64_cyc), which --check-cycles compares against this one on every build.
# ---------------------------------------------------------------------------
OPS = {}


def _ops(text):
    for row in text.split(";"):
        row = row.split()
        if not row:
            continue
        op, mn, mode, cyc = row
        OPS[int(op, 16)] = (mn, mode, int(cyc))


_ops("""
00 BRK imp 7; 01 ORA indx 6; 05 ORA zp 3; 06 ASL zp 5; 08 PHP imp 3;
09 ORA imm 2; 0A ASL acc 2; 0D ORA abs 4; 0E ASL abs 6; 10 BPL rel 2;
11 ORA indy 5; 15 ORA zpx 4; 16 ASL zpx 6; 18 CLC imp 2; 19 ORA absy 4;
1D ORA absx 4; 1E ASL absx 7; 20 JSR abs 6; 21 AND indx 6; 24 BIT zp 3;
25 AND zp 3; 26 ROL zp 5; 28 PLP imp 4; 29 AND imm 2; 2A ROL acc 2;
2C BIT abs 4; 2D AND abs 4; 2E ROL abs 6; 30 BMI rel 2; 31 AND indy 5;
35 AND zpx 4; 36 ROL zpx 6; 38 SEC imp 2; 39 AND absy 4; 3D AND absx 4;
3E ROL absx 7; 40 RTI imp 6; 41 EOR indx 6; 45 EOR zp 3; 46 LSR zp 5;
48 PHA imp 3; 49 EOR imm 2; 4A LSR acc 2; 4C JMP abs 3; 4D EOR abs 4;
4E LSR abs 6; 50 BVC rel 2; 51 EOR indy 5; 55 EOR zpx 4; 56 LSR zpx 6;
58 CLI imp 2; 59 EOR absy 4; 5D EOR absx 4; 5E LSR absx 7; 60 RTS imp 6;
61 ADC indx 6; 65 ADC zp 3; 66 ROR zp 5; 68 PLA imp 4; 69 ADC imm 2;
6A ROR acc 2; 6C JMP ind 5; 6D ADC abs 4; 6E ROR abs 6; 70 BVS rel 2;
71 ADC indy 5; 75 ADC zpx 4; 76 ROR zpx 6; 78 SEI imp 2; 79 ADC absy 4;
7D ADC absx 4; 7E ROR absx 7; 81 STA indx 6; 84 STY zp 3; 85 STA zp 3;
86 STX zp 3; 88 DEY imp 2; 8A TXA imp 2; 8C STY abs 4; 8D STA abs 4;
8E STX abs 4; 90 BCC rel 2; 91 STA indy 6; 94 STY zpx 4; 95 STA zpx 4;
96 STX zpy 4; 98 TYA imp 2; 99 STA absy 5; 9A TXS imp 2; 9D STA absx 5;
A0 LDY imm 2; A1 LDA indx 6; A2 LDX imm 2; A4 LDY zp 3; A5 LDA zp 3;
A6 LDX zp 3; A8 TAY imp 2; A9 LDA imm 2; AA TAX imp 2; AC LDY abs 4;
AD LDA abs 4; AE LDX abs 4; B0 BCS rel 2; B1 LDA indy 5; B4 LDY zpx 4;
B5 LDA zpx 4; B6 LDX zpy 4; B8 CLV imp 2; B9 LDA absy 4; BA TSX imp 2;
BC LDY absx 4; BD LDA absx 4; BE LDX absy 4; C0 CPY imm 2; C1 CMP indx 6;
C4 CPY zp 3; C5 CMP zp 3; C6 DEC zp 5; C8 INY imp 2; C9 CMP imm 2;
CA DEX imp 2; CC CPY abs 4; CD CMP abs 4; CE DEC abs 6; D0 BNE rel 2;
D1 CMP indy 5; D5 CMP zpx 4; D6 DEC zpx 6; D8 CLD imp 2; D9 CMP absy 4;
DD CMP absx 4; DE DEC absx 7; E0 CPX imm 2; E1 SBC indx 6; E4 CPX zp 3;
E5 SBC zp 3; E6 INC zp 5; E8 INX imp 2; E9 SBC imm 2; EA NOP imp 2;
EC CPX abs 4; ED SBC abs 4; EE INC abs 6; F0 BEQ rel 2; F1 SBC indy 5;
F5 SBC zpx 4; F6 INC zpx 6; F8 SED imp 2; F9 SBC absy 4; FD SBC absx 4;
FE INC absx 7
""")

PENALTY = {"absx", "absy", "indy"}      # read modes that pay an index carry
READS = {"LDA", "LDX", "LDY", "ORA", "AND", "EOR", "ADC", "SBC", "CMP",
         "CPX", "CPY", "BIT"}
RMW = {"ASL", "LSR", "ROL", "ROR", "INC", "DEC"}
STORES = {"STA": "al", "STX": "cl", "STY": "ch"}
REG = {"A": "al", "X": "cl", "Y": "ch"}
ZP = ("zp", "zpx", "zpy")

# ---------------------------------------------------------------------------
# The machines
# ---------------------------------------------------------------------------
APPLE2 = dict(
    name="apple2",
    scr=0xC100,                 # the accurate core's scratch page, A2_SCR_BASE
    pv=0xC138,                  # the flag byte (APPLE2-SPEC 4.6)
    fetch="",                   # PC is fetched through DS: the image holds the ROM
    io_lo=0xC0, io_hi=0xD0,     # $C000-$CFFF: reads above $C1FF are the image
    io_call=0xC0,               # the page whose reads and writes call out
    ram_top=0xC0,               # a write at or above this page is not RAM
    video=((0x04, 0x0C), (0x20, 0x60)),     # the pages the flush reads
    # A NEW PC IN [$BFFE, $D000) IS HANDED BACK: $C000-$CFFF is I/O and slot
    # space, and an instruction at $BFFE or $BFFF takes operand bytes from the
    # soft switches, which the image cannot answer. (Fall-through or a branch
    # inside page $BF into $BFFE is not caught - the stated deviation.)
    ct_lo=0xBFFE,
    ct_top=False,               # $FFFE-$FFFF are the ROM's RTI and a hand-back
    kb_latch=True,              # $C000-$C00F answered from KBV (a2io.c)
    aot=(0xD000, 0x10000),      # the main ROM, translated (m65aot.py, 4.7)
    chrget={0x00B1: "a_chrget", 0x00B7: "a_chrgot"},  # ...and CHRGET native
    frame_extra=3,              # F+24 its code, F+26 its PC table, F+28 us
    aot_frame=24,
)
C64 = dict(
    name="c64",
    scr=0xFFC0,                 # the accurate core's scratch, C64_SCR_BASE
    pv=0xFFF9,                  # its one spare byte: writes there are dropped
    fetch="es:",                # PC through ES: RAM, or a ROM bank's segment
    ct_top=True,                # $FFFE-$FFFF can be RAM holding anything
    frame_extra=10,             # the fetch segment of each region, F+24..F+31,
                                # and the CHARGEN's, F+32 (C64-SPEC 4.7); then
                                # BASIC's translation F+34, the PC table F+36,
                                # this core F+38, the KERNAL's translation
                                # F+40 (4.8); the recompiler's segment F+42
                                # (4.9, 0: none)
    aot=(0xA000, 0x10000),      # BASIC and the KERNAL, translated (m65aot.py)
    aot_frame=34,
    chrget={0x0073: "a_chrget", 0x0079: "a_chrgot"},
    exact=True,                 # the translation keeps the run's end exact
    jit=True,                   # ...and the recompiler's door (4.9)
)
# THE LONGEST BLOCK a translation may run without testing the countdown
# (m65aot.py, exact profiles): a PC entered from outside its block's start is
# entered only with more than this left
MAXBLK = 120

# the accurate cores' scratch offsets (a2cpu.inc, c64cpu.inc - the same block)
S_DIRTY, S_CARRY, S_DEAD, S_IRQ = 0x00, 0x22, 0x24, 0x2A
S_WLO, S_WHI, S_ANY, S_WATLO, S_WATHI = 0x2C, 0x2E, 0x30, 0x32, 0x34
S_LASTPG = 0x31                 # the C64's: the page this core last marked
S_MAPA, S_MAPD, S_MAPE = 0x36, 0x37, 0x38      # the C64's bank map
# ...and the Apple II's own, beyond them in its page (APPLE2-SPEC 4.6)
S_KBV, S_KBOK, S_IDLEOK = 0x4A, 0x4B, 0x4C

# the machine record - the same first ten words in _a2_m and _c64_m
AM_PC, AM_A, AM_X, AM_Y, AM_S, AM_P, AM_CNT, AM_REASON = 4, 6, 8, 10, 12, 14, 16, 18

# the frame (the module's header)
F_REC, F_PKGDS, F_IORD, F_IOWR, F_EXTRA = 0, 2, 16, 20, 24

RSN_SLICE, RSN_HB1, RSN_HBR, RSN_HBI = 0, 2, 3, 4
ENTRY = 0x0210                  # in opcode $02's slot, after its hand-back stub
# A PART IS AT MOST 65,024 BYTES (os88pkg.py: its length is a sector count in a
# word), and the slots of $FE and $FF begin at $FE00. So the part is the
# segment's first $FE00 bytes; the package copies it into a 64KB claim of its
# own and writes those two slots as near jumps: $FE (INC abs,X) to its body in
# a spare slot, $FF (not a legal opcode) to the hand-back. The two targets are
# a table at FIXUP, which the package reads out of the part.
XR_RESUME, XR_SLICE = 0x7280, 0x72A0     # the translation's doors back
PART_LEN = 0xFE00
FIXUP = 0x0203                  # dw h_FE body, x_hb1 - in $02's slot

# THE RECOMPILER (C64-SPEC 4.9): its segment is the frame's F+42; this core
# reads a byte a page at PGMAP in its OWN segment, which the recompiler
# writes - 0 the core's alone, 1 sampled for heat, $80 blocks start here -
# and far-jumps to its hook (JIT_HOOK) with DH a weight; the CODE map and
# the flush request are the recompiler's, at JC_CODE and JC_FLUSH
F_JIT = 42
JIT_HOOK = 0x00F0               # in the recompiler's segment
PGMAP = 0xFF10                  # in this core's segment, past the $FF slot
JC_CODE, JC_FLUSH = 0x0000, 0x0020
JC_FINE = 0x6A00                # ...its byte map, a bit a guest byte to $CFFF
JC_HITA = 0x0060                # ...and the address a write found code at
JIT_SAMPLE = 0x1F               # a transfer is a sample when BP & this is 0
JIT_W_SAMPLE, JIT_W_BLOCK = 16, 1

# the C64's regions, as WORD indices into the frame's fetch segments: what a
# PC's page is fetched through depends on the bank map only for these
C64_REGION = [0] * 256
for _p in range(0xA0, 0xC0):
    C64_REGION[_p] = 2          # BASIC, or RAM
for _p in range(0xD0, 0xE0):
    C64_REGION[_p] = 4          # I/O, CHARGEN or RAM
for _p in range(0xE0, 0x100):
    C64_REGION[_p] = 6          # KERNAL, or RAM


class Gen:
    def __init__(self, prof):
        self.p = prof
        self.c64 = prof["name"] == "c64"
        self.F = prof["fetch"]
        self.out = []
        self.n = 0

    def scr(self, off):
        return "SCR+0x%02X" % off

    def L(self, *lines):
        self.out.extend(lines)

    def lab(self):
        self.n += 1
        return ".g%d" % self.n

    def fetch_op(self):
        return ["    mov dh, [%ssi]" % self.F, "    inc si", "    jmp dx"]

    # --- the end of every handler ----------------------------------------
    def next(self, cyc):
        """charge CYC and dispatch the next opcode, or leave through x_slice"""
        x = self.lab()
        self.L("    sub bp, %d" % cyc, "    jle %s" % x, *self.fetch_op())
        self.L("%s: jmp x_slice" % x)

    def next_ct(self, cyc):
        """...after a control transfer: SI is a new PC, which may be one this
        core cannot fetch from (handed back), and on the C64 may need another
        fetch segment. BX is free here."""
        x, hb = self.lab(), self.lab()
        self.L("    sub bp, %d" % cyc, "    jle %s" % x)
        self.ct_check(hb)
        self.L(*self.aot_or_fetch(hook=True))
        self.L("%s: jmp x_hbr" % hb, "%s: jmp x_slice" % x)

    def next_taken(self, cyc, back=False):
        """...after a branch taken inside its page: on a profile with a
        translation, the target goes through ct_rom as a jump's does - the
        translation's table names only the starts of its straight runs
        (m65aot.py's groups()), so a loop the core entered in the middle
        rejoins it at its next branch rather than at its next jump"""
        if not self.p.get("aot"):
            return self.next(cyc)
        x = self.lab()
        self.L("    sub bp, %d" % cyc, "    jle %s" % x)
        self.L(*self.aot_or_fetch(hook=back))
        self.L("%s: jmp x_slice" % x)

    def loop_collapse(self, op):
        """THE C64's SELF-LOOPS, RUN AS ONE STEP (C64-SPEC 4.7.2). `DEC abs`,
        `DEX` or `DEY` followed by a BNE back onto itself is a delay loop
        whose whole length is known before it runs: n passes of the counter's
        value (0 is 256), 9 cycles a pass for DEC abs and 5 for DEX/DEY, less
        the 1 the last, untaken BNE saves. When the budget holds all of it,
        the counter becomes 0, Z is set and N clear (C untouched, as DEC
        leaves it), BP is charged exactly that, and the core goes on past the
        BNE - the same cycle, the same registers, the same flags as running
        it. The Portland CUG Christmas demo's `DEC $C333 / BNE` spent a
        quarter of a 286 a pass at a time. DEC abs only on plain RAM - below
        $A000 or in $C000-$CFFF - and the page marked once. Not enough
        budget: the ordinary handler below, a pass at a time."""
        L = self.L
        no = self.lab()
        if op == 0xCE:
            ram = self.lab()
            L("    mov bx, [es:si]",
              "    cmp word [es:si+2], 0xFBD0",      # BNE -5: back to the DEC
              "    jne %s" % no,
              "    cmp bh, 0xA0", "    jb %s" % ram,
              "    cmp bh, 0xC0", "    jb %s" % no,
              "    cmp bh, 0xD0", "    jae %s" % no,
              "%s:" % ram,
              "    cmp bx, 2", "    jb %s" % no,
              "    mov dl, [bx]")
            per, skip = 9, 4
        else:
            L("    cmp word [es:si], 0xFDD0",          # BNE -3: back to the DEX
              "    jne %s" % no,
              "    mov dl, %s" % ("cl" if op == 0xCA else "ch"))
            per, skip = 5, 2
        nz = self.lab()
        L("    xor dh, dh",
          "    test dl, dl", "    jnz %s" % nz,
          "    inc dh",                               # 0 is 256 passes
          "%s:" % nz,
          "    push bx",
          "    mov bx, dx")                           # n
        if per == 9:
            L("    shl dx, 1", "    shl dx, 1", "    shl dx, 1")   # 8n
        else:
            L("    shl dx, 1", "    shl dx, 1")                   # 4n
        L("    add dx, bx",                           # 9n or 5n
          "    dec dx",                               # ...less the last BNE's 1
          "    pop bx",
          "    cmp bp, dx", "    jg %s" % (no + "_go"),
          # NOT ALL OF IT FITS: AS MANY WHOLE PASSES AS DO (C64-SPEC
          # 4.7.2). k = (BP - 1) / per leaves the countdown above 0 after
          # each, as running them would, and k < n because the whole did
          # not fit; the counter goes down by k and the next pass is the
          # ordinary handler's, which recomputes N and Z. A run's budget is
          # often a raster line or two, far less than a 256-pass loop
          "    push ax", "    push cx",
          "    mov ax, bp", "    dec ax", "    xor dx, dx",
          "    mov cx, %d" % per, "    div cx",
          "    pop cx",
          "    or ax, ax", "    jz %s" % (no + "_p0"),
          "    mov dx, ax")
        if op == 0xCE:
            L("    sub [bx], dl", "    call c64_mark")
        else:
            L("    sub %s, dl" % ("cl" if op == 0xCA else "ch"))
        L("    shl ax, 1", "    shl ax, 1")
        if per == 9:
            L("    shl ax, 1")
        L("    add ax, dx",                           # k x per
          "    sub bp, ax",
          "%s_p0: pop ax" % no,
          "    xor dx, dx",
          "    jmp %s" % no,
          "%s_go:" % no,
          "    sub bp, dx")
        if op == 0xCE:
            L("    mov byte [bx], 0", "    call c64_mark")
        else:
            L("    mov %s, 0" % ("cl" if op == 0xCA else "ch"))
        L("    mov dh, 1", "    sahf", "    dec dh", "    lahf",   # Z=1, N=0
          "    add si, %d" % skip,
          "    xor dl, dl")
        L(*self.fetch_op())
        L("%s:" % no)

    def jit_hook(self):
        """THE RECOMPILER'S DOOR (C64-SPEC 4.9.2), at a control transfer to
        a RAM PC: the page's PGMAP byte says whether to look - always where a
        block starts on the page, one transfer in 32 (by the countdown's low
        bits) where it may, never where it is off or the page volatile"""
        f = self.lab()
        return ["    mov bx, si", "    mov bl, bh", "    xor bh, bh",
                "    mov bl, [cs:bx+0x%04X]" % PGMAP,
                "    or bl, bl", "    jz %s" % f, "    jmp ct_jitq",
                "%s:" % f]

    def aot_or_fetch(self, hook=False):
        """the next opcode - or, where the profile has a ROM translated ahead
        of time (m65aot.py) and SI is in it, ct_rom, which enters the
        translation when the PC table names one; and with HOOK, a RAM PC
        through the recompiler's door"""
        if not self.p.get("aot"):
            return self.fetch_op()
        go = self.lab()
        lo, hi = self.p["aot"]
        top = [] if hi >= 0x10000 else ["    cmp si, 0x%04X" % hi, "    jae %s" % go]
        if self.c64 and (lo, hi) == (0xA000, 0x10000):
            # ...AND $C000-$CFFF, which is RAM under every C64 bank map and
            # where a program keeps its own loops: a taken BNE there went to
            # ct_rom only to find ES = DS, 6.6% of a 286 under the Portland
            # CUG demo's delay loop (C64-SPEC 4.8)
            ct = self.lab()
            top = ["    cmp si, 0xD000", "    jae %s" % ct,
                   "    cmp si, 0xC000", "    jae %s" % go,
                   "%s:" % ct]
        jit = self.jit_hook() if hook and self.p.get("jit") else []
        return ["    cmp si, 0x%04X" % lo, "    jb %s" % go] + top + [
                "    jmp ct_rom",
                "%s:" % go] + jit + self.fetch_op()

    def ct_check(self, hb):
        if self.c64:
            self.L(*self.c64_fes(hb))
        else:
            # ONE unsigned range test, so neither a RAM nor a ROM target
            # takes a jump
            lo, hi = self.p["ct_lo"], self.p["io_hi"] << 8
            self.L("    lea bx, [si+0x%04X]" % ((0x10000 - lo) & 0xFFFF),
                   "    cmp bx, 0x%04X" % (hi - lo), "    jb %s" % hb)
        if self.p["ct_top"]:
            # `mov bx,[si]` at $FFFF faults a 286: a PC there is handed back
            self.L("    cmp si, 0xFFFE", "    jae %s" % hb)

    # --- flags -----------------------------------------------------------
    def PV(self):
        return "0x%04X" % self.p["pv"]

    def nz(self, r):
        """N and Z from R, C kept: sahf puts C in CF, inc/dec leave CF alone"""
        self.L("    sahf", "    inc %s" % r, "    dec %s" % r, "    lahf")

    def v_from_of(self):
        v1, vd = self.lab(), self.lab()
        self.L("    jo %s" % v1,
               "    and byte [%s], 0xBF" % self.PV(), "    jmp short %s" % vd,
               "%s: or byte [%s], 0x40" % (v1, self.PV()), "%s:" % vd)

    # --- addressing: the effective address into BX ------------------------
    def ea(self, mode, penalty, pen, handback):
        L, F = self.L, self.F
        if mode == "zp":
            L("    mov bl, [%ssi]" % F, "    inc si", "    xor bh, bh")
        elif mode in ("zpx", "zpy"):
            L("    mov bl, [%ssi]" % F, "    inc si",
              "    add bl, %s" % ("cl" if mode == "zpx" else "ch"), "    xor bh, bh")
        elif mode in ("abs", "absx", "absy"):
            L("    mov bx, [%ssi]" % F, "    add si, 2")
            if mode != "abs":
                self.index("cl" if mode == "absx" else "ch", penalty, pen)
        elif mode in ("indx", "indy"):
            L("    mov bl, [%ssi]" % F, "    inc si")
            if mode == "indx":
                L("    add bl, cl")
            L("    xor bh, bh")
            if self.c64:
                # a pointer in $FF/$00/$01 reads the processor port: the
                # accurate core's (the pointer read has no side effect)
                L("    cmp bl, 2", "    jb %s" % handback(1),
                  "    cmp bl, 0xFF", "    je %s" % handback(1))
            L("    mov dl, [bx]", "    inc bl", "    mov bh, [bx]",
              "    mov bl, dl", "    xor dl, dl")
            if mode == "indy":
                self.index("ch", penalty, pen)
        else:
            raise ValueError(mode)

    def index(self, reg, penalty, pen):
        if penalty:
            back = self.lab()
            self.L("    add bl, %s" % reg, "    jc %s" % pen[0], "%s:" % back)
            pen.append(back)
        else:
            self.L("    add bl, %s" % reg, "    adc bh, 0")

    def pen_tail(self, pen):
        if len(pen) == 2:
            self.L("%s: inc bh" % pen[0], "    dec bp", "    jmp short %s" % pen[1])

    # --- memory ----------------------------------------------------------
    def read(self, mode, dst, cyc, idle=False):
        """DST = the byte at BX (zero page is always RAM; the C64's $00/$01
        read-back is kept in RAM by its package while this core runs)"""
        if mode in ZP:
            self.L("    mov %s, [bx]" % dst)
            return
        hi, k = self.lab(), self.lab()
        top = 0xA0 if self.c64 else self.p["io_lo"]
        # RAM FALLS THROUGH: the one compare, not taken
        self.L("    cmp bh, 0x%02X" % top, "    jae %s" % hi,
               "    mov %s, [bx]" % dst, "%s:" % k)
        if self.c64:
            blk = ["%s: mov dl, %d" % (hi, cyc), "    call rd_hi", "    xor dl, dl"]
            blk += (["    mov %s, dh" % dst] if dst != "dh" else []) + ["    jmp %s" % k]
            self.later.append(blk)
            return
        io, call = self.lab(), self.lab()
        blk = ["%s: cmp bh, 0x%02X" % (hi, self.p["io_call"] + 2), "    jb %s" % io,
               "    mov %s, [bx]" % dst, "    jmp %s" % k,         # the ROM
               "%s:" % io]
        if self.p.get("kb_latch"):
            # THE KEYBOARD LATCH, here and not in a call (a2io.c mirrors it)
            blk += ["    cmp bx, 0x%04X" % ((self.p["io_call"] << 8) + 0x10),
                    "    jae %s" % call,
                    "    cmp byte [%s], 0" % self.scr(S_KBOK), "    je %s" % call,
                    "    mov %s, [%s]" % (dst, self.scr(S_KBV))]
            if idle:
                # THE ROM'S KEYIN LOOP WITH NO KEY: this BIT is the one at
                # $FD21 (SI past it), and the loop can only spin until the
                # budget ends - kbd_idle runs it as a counter
                blk += ["    test %s, 0x80" % dst, "    jnz %s" % k,
                        "    cmp si, 0xFD24", "    jne %s" % k,
                        "    cmp bp, 19", "    jle %s" % k,          # a pass must fit
                        "    cmp byte [%s], 0" % self.scr(S_IDLEOK), "    je %s" % k,
                        "    jmp kbd_idle"]
            blk += ["    jmp %s" % k]
        blk += ["%s: mov dl, %d" % (call, cyc), "    call rd_io", "    xor dl, dl"]
        blk += (["    mov %s, dh" % dst] if dst != "dh" else []) + ["    jmp %s" % k]
        self.later.append(blk)

    def write(self, mode, src, cyc, handback):
        """the byte SRC to BX: RAM, the I/O call-out, or dropped; and the
        damage mark"""
        L = self.L
        if mode in ZP:
            if self.c64:
                # $00/$01 is the processor port: a write there re-banks
                L("    cmp bl, 2", "    jb %s" % handback(1))
            L("    mov [bx], %s" % src)
            return
        if self.c64:
            wd, wh = self.lab(), self.lab()
            consumed = 1 if mode in ("indx", "indy") else 2
            L("    cmp bx, 2", "    jb %s" % handback(consumed),
              "    cmp bh, 0xD0", "    jae %s" % wh,
              "    mov [bx], %s" % src,
              "    call c64_mark",
              "%s:" % wd)
            self.later.append(["%s:" % wh]
                              + (["    mov dh, %s" % src] if src != "dh" else [])
                              + ["    mov dl, %d" % cyc, "    call wr_hi",
                                 "    xor dl, dl", "    jmp %s" % wd])
            return
        wd, wv, wh = self.lab(), self.lab(), self.lab()
        (a0, a1), (b0, b1) = self.p["video"]
        L("    cmp bh, 0x%02X" % self.p["ram_top"], "    jae %s" % wh,
          "    mov [bx], %s" % src,
          "    cmp bh, 0x%02X" % b1, "    jae %s" % wd,
          "    cmp bh, 0x%02X" % b0, "    jae %s" % wv,
          "    cmp bh, 0x%02X" % a1, "    jae %s" % wd,
          "    cmp bh, 0x%02X" % a0, "    jb %s" % wd,
          "%s: call vid_mark" % wv, "%s:" % wd)
        self.later.append(["%s:" % wh]
                          + (["    mov dh, %s" % src] if src != "dh" else [])
                          + ["    mov dl, %d" % cyc, "    call wr_io",
                             "    xor dl, dl", "    jmp %s" % wd])

    def push(self, r):
        self.L("    mov [di], %s" % r, "    dec di", "    or di, 0x0100")

    def pop(self, r):
        self.L("    inc di", "    and di, 0x01FF", "    or di, 0x0100",
               "    mov %s, [di]" % r)

    # --- one handler -----------------------------------------------------
    def handler(self, op):
        mn, mode, cyc = OPS[op]
        self.later = []
        hb = {}                 # operand bytes consumed -> its hand-back label

        def handback(consumed=0):
            """a hand-back from THIS slot, CONSUMED operand bytes after the
            opcode: it rewinds SI to the opcode for the accurate core"""
            if consumed not in hb:
                hb[consumed] = self.lab()
            return hb[consumed]

        L, F = self.L, self.F
        pen = [self.lab()] if mode in PENALTY and mn in READS else []
        if self.c64 and op in (0xCE, 0xCA, 0x88):
            self.loop_collapse(op)

        if mn == "BRK":
            L("    jmp x_hb1")
            return
        if mn in ("ADC", "SBC"):
            L("    test byte [%s], 0x08" % self.PV(), "    jnz %s" % handback())
        hbi = None
        if mn in ("CLI", "SEI", "PLP", "RTI"):
            hbi = self.lab()
            L("    cmp byte [%s], 0" % self.scr(S_IRQ), "    jne %s" % hbi)

        if mn in READS:
            if mode == "imm":
                L("    mov dh, [%ssi]" % F, "    inc si")
                src = "dh"
            else:
                self.ea(mode, mode in PENALTY, pen, handback)
                dst = {"LDA": "al", "LDX": "cl", "LDY": "ch"}.get(mn, "dh")
                self.read(mode, dst, cyc,
                          idle=(mn == "BIT" and mode == "abs" and self.p.get("kb_latch")))
                src = dst
            self.alu(mn, src)
            self.next(cyc)
        elif mn in STORES:
            self.ea(mode, False, pen, handback)
            self.write(mode, STORES[mn], cyc, handback)
            self.next(cyc)
        elif mn in RMW and mode == "acc":
            self.shift(mn, "al")
            self.next(cyc)
        elif mn in RMW and mode in ZP:
            self.ea(mode, False, pen, handback)
            if self.c64:
                L("    cmp bl, 2", "    jb %s" % handback(1))
            if mn in ("INC", "DEC"):
                L("    sahf", "    %s byte [bx]" % mn.lower(), "    lahf")
            elif mn in ("ASL", "LSR"):
                L("    %s byte [bx], 1" % ("shl" if mn == "ASL" else "shr"), "    lahf")
            else:
                L("    mov dh, [bx]")
                self.shift(mn, "dh")
                L("    mov [bx], dh")
            self.next(cyc)
        elif mn in RMW and self.c64:
            # THE C64's READ-MODIFY-WRITE ABOVE $A000 IS THIS CORE'S, BUT FOR
            # I/O. It handed the whole of $A000-$FFFF back, one instruction at
            # a time - and $C000-$CFFF is RAM under every bank map, where a
            # program keeps its counters: a `DEC $C333 / BNE` delay loop ran
            # at 3% (the Portland CUG Christmas demo). rd_hi reads what the
            # map shows there - BASIC, the KERNAL, the CHARGEN or RAM - and
            # the write path puts the result in RAM under a ROM, which is the
            # 6510's own rule. I/O stays the accurate core's: the dummy write
            # of the unmodified value is a real access ($D019's ack).
            self.ea(mode, False, pen, handback)
            hi, k, rd = self.lab(), self.lab(), self.lab()
            L("    cmp bh, 0xA0", "    jae %s" % hi,
              "    cmp bx, 2", "    jb %s" % handback(2),
              "    mov dh, [bx]", "%s:" % k)
            self.later.append([
                "%s: cmp bh, 0xD0" % hi, "    jb %s" % rd,
                "    cmp bh, 0xE0", "    jae %s" % rd,
                "    cmp byte [%s], 4" % self.scr(S_MAPD),
                "    je %s" % handback(2),
                "%s: mov dl, %d" % (rd, cyc), "    call rd_hi", "    xor dl, dl",
                "    jmp %s" % k])
            self.shift(mn, "dh")
            self.write(mode, "dh", cyc, handback)
            self.next(cyc)
        elif mn in RMW:
            # a read-modify-write of anything but plain RAM is the accurate
            # core's: I/O (the dummy write)
            self.ea(mode, False, pen, handback)
            top = self.p["ram_top"]
            L("    cmp bh, 0x%02X" % top, "    jae %s" % handback(2))
            L("    mov dh, [bx]")
            self.shift(mn, "dh")
            self.write(mode, "dh", cyc, handback)
            self.next(cyc)
        elif mode == "rel":
            self.branch(mn)
        else:
            self.misc(mn, mode, cyc, handback)

        self.pen_tail(pen)
        for blk in self.later:
            L(*blk)
        if hbi:
            L("%s: jmp x_hbi" % hbi)
        for consumed, lab in sorted(hb.items()):
            # x_hb1 takes off the opcode byte itself
            if consumed:
                L("%s: sub si, %d" % (lab, consumed), "    jmp x_hb1")
            else:
                L("%s: jmp x_hb1" % lab)

    def alu(self, mn, src):
        L = self.L
        if mn in ("LDA", "LDX", "LDY"):
            reg = {"LDA": "al", "LDX": "cl", "LDY": "ch"}[mn]
            if src != reg:
                L("    mov %s, %s" % (reg, src))
            self.nz(reg)
        elif mn in ("ORA", "AND", "EOR"):
            L("    %s al, %s" % ({"ORA": "or", "AND": "and", "EOR": "xor"}[mn], src))
            self.nz("al")
        elif mn == "ADC":
            L("    sahf", "    adc al, %s" % src, "    lahf")
            self.v_from_of()
        elif mn == "SBC":
            L("    sahf", "    cmc", "    sbb al, %s" % src, "    cmc", "    lahf")
            self.v_from_of()
        elif mn in ("CMP", "CPX", "CPY"):
            reg = {"CMP": "al", "CPX": "cl", "CPY": "ch"}[mn]
            L("    cmp %s, %s" % (reg, src), "    cmc", "    lahf")
        elif mn == "BIT":
            nz, n0 = self.lab(), self.lab()
            L("    and ah, 0x3F",                        # N and Z go; C stays
              "    test al, %s" % src, "    jnz %s" % nz,
              "    or ah, 0x40",
              "%s: test %s, 0x80" % (nz, src), "    jz %s" % n0,
              "    or ah, 0x80",
              "%s: mov dl, %s" % (n0, src), "    and dl, 0x40",
              "    and byte [%s], 0xBF" % self.PV(),
              "    or [%s], dl" % self.PV(), "    xor dl, dl")

    def shift(self, mn, r):
        L = self.L
        if mn == "ASL":
            L("    shl %s, 1" % r, "    lahf")
        elif mn == "LSR":
            L("    shr %s, 1" % r, "    lahf")
        elif mn in ("ROL", "ROR"):
            L("    sahf", "    %s %s, 1" % ("rcl" if mn == "ROL" else "rcr", r),
              "    inc %s" % r, "    dec %s" % r, "    lahf")
        elif mn in ("INC", "DEC"):
            L("    sahf", "    %s %s" % (mn.lower(), r), "    lahf")

    def branch(self, mn):
        L, F = self.L, self.F
        reg, mask, not_taken = {
            "BPL": ("ah", 0x80, "jnz"), "BMI": ("ah", 0x80, "jz"),
            "BVC": ("pv", 0x40, "jnz"), "BVS": ("pv", 0x40, "jz"),
            "BCC": ("ah", 0x01, "jnz"), "BCS": ("ah", 0x01, "jz"),
            "BNE": ("ah", 0x40, "jnz"), "BEQ": ("ah", 0x40, "jz")}[mn]
        nt, back, xf, xb, done = (self.lab() for _ in range(5))
        where = "byte [%s]" % self.PV() if reg == "pv" else "ah"
        L("    mov dh, [%ssi]" % F, "    inc si",
          "    test %s, 0x%02X" % (where, mask), "    %s %s" % (not_taken, nt),
          "    mov bx, si",
          "    test dh, dh", "    js %s" % back,
          "    add bl, dh", "    jc %s" % xf,
          "    mov si, bx")
        self.next_taken(3)
        L("%s: add bl, dh" % back, "    jnc %s" % xb, "    mov si, bx")
        self.next_taken(3, back=True)
        L("%s:" % nt)
        self.next(2)
        # ACROSS A PAGE: the extra cycle, and the target checked as a jump's
        L("%s: inc bh" % xf, "    jmp short %s" % done,
          "%s: dec bh" % xb,
          "%s: mov si, bx" % done)
        self.next_ct(4)

    def misc(self, mn, mode, cyc, handback):
        L, P, F = self.L, self.PV(), self.F
        if mn == "JMP" and mode == "abs":
            L("    mov si, [%ssi]" % F)
            self.next_ct(cyc)
        elif mn == "JMP":                       # (ind), with the NMOS page wrap
            L("    mov bx, [%ssi]" % F,
              "    cmp bh, 0x%02X" % (0xA0 if self.c64 else self.p["io_lo"]),
              "    jae %s" % handback())
            if self.c64:
                L("    cmp bx, 2", "    jb %s" % handback())
            L("    mov dl, [bx]", "    inc bl", "    mov dh, [bx]",
              "    mov si, dx", "    xor dl, dl")
            self.next_ct(cyc)
        elif mn == "JSR":
            # ONE WORD onto the stack while S is not at the page's bottom
            slow, j = self.lab(), self.lab()
            L("    mov bx, [%ssi]" % F, "    inc si",
              "    cmp di, 0x0101", "    jb %s" % slow,
              "    mov [di-1], si", "    sub di, 2",
              "%s: mov si, bx" % j)
            self.next_ct(cyc)
            L("%s: mov dx, si" % slow)
            self.push("dh")
            self.push("dl")
            L("    xor dl, dl", "    jmp %s" % j)
        elif mn == "RTS":
            # ...and one word off it while S is not at the top
            slow, j = self.lab(), self.lab()
            L("    cmp di, 0x01FE", "    jae %s" % slow,
              "    mov si, [di+1]", "    add di, 2",
              "%s: inc si" % j)
            self.next_ct(cyc)
            L("%s:" % slow)
            self.pop("bl")
            self.pop("bh")
            L("    mov si, bx", "    jmp %s" % j)
        elif mn == "RTI":
            self.pop("dl")
            L("    call setp", "    xor dl, dl")
            self.pop("bl")
            self.pop("bh")
            L("    mov si, bx")
            self.next_ct(cyc)
        elif mn == "PHA":
            self.push("al")
            self.next(cyc)
        elif mn == "PHP":
            L("    call getp", "    or dl, 0x10")
            self.push("dl")
            L("    xor dl, dl")
            self.next(cyc)
        elif mn == "PLA":
            self.pop("al")
            self.nz("al")
            self.next(cyc)
        elif mn == "PLP":
            self.pop("dl")
            L("    call setp", "    xor dl, dl")
            self.next(cyc)
        elif mn in ("TAX", "TAY", "TXA", "TYA"):
            d, s = REG[mn[2]], REG[mn[1]]
            L("    mov %s, %s" % (d, s))
            self.nz(d)
            self.next(cyc)
        elif mn == "TSX":
            L("    mov bx, di", "    mov cl, bl")
            self.nz("cl")
            self.next(cyc)
        elif mn == "TXS":
            L("    mov bl, cl", "    mov bh, 1", "    mov di, bx")
            self.next(cyc)
        elif mn in ("INX", "INY", "DEX", "DEY"):
            r = REG[mn[2]]
            L("    sahf", "    %s %s" % ("inc" if mn[0] == "I" else "dec", r), "    lahf")
            self.next(cyc)
        elif mn in ("CLC", "SEC"):
            L("    and ah, 0xFE" if mn == "CLC" else "    or ah, 0x01")
            self.next(cyc)
        elif mn in ("CLV", "CLD", "SED", "CLI", "SEI"):
            bit = {"CLV": 0x40, "CLD": 0x08, "SED": 0x08, "CLI": 0x04, "SEI": 0x04}[mn]
            if mn.startswith("CL"):
                L("    and byte [%s], 0x%02X" % (P, 0xFF ^ bit))
            else:
                L("    or byte [%s], 0x%02X" % (P, bit))
            self.next(cyc)
        elif mn == "NOP":
            self.next(cyc)
        else:
            raise ValueError(mn)

    # --- the shared tails ------------------------------------------------
    def tails(self):
        """name -> lines, each placed in an unused opcode's slot"""
        P, S, F = self.PV(), self.scr, self.F
        retn = 8 + 2 * self.p["frame_extra"]
        t = {}
        t["entry"] = [
            # FAR-called, with the doors (and the profile's words) pushed:
            # AX = cycles, DX = the guest's segment, ES = the package's data
            # segment, BX = its machine record
            "entry:",
            "    push bp", "    push si", "    push di", "    push ds",
            "    push es", "    push bx",                # F+2, F+0
            "    mov ds, dx",
            "    mov bp, ax",
            "    mov word [%s], 0" % S(S_CARRY),
            "    mov si, [es:bx+%d]" % AM_PC,
            "    mov cl, [es:bx+%d]" % AM_X,
            "    mov ch, [es:bx+%d]" % AM_Y,
            "    mov di, [es:bx+%d]" % AM_S,
            "    and di, 0x00FF", "    or di, 0x0100",
            "    mov dl, [es:bx+%d]" % AM_P,
            "    mov al, [es:bx+%d]" % AM_A,
            "    call setp",
            "    xor dl, dl",
            "    push ds", "    pop es",                  # (the Apple II's fetch)
        ] + (self.tracelog(0xE1E1) if self.p.get("aot") else []) + [
            # an interrupt pending at the top of a run is the accurate core's
            "    test byte [%s], 0x02" % S(S_IRQ), "    jnz .hbi",
            "    test byte [%s], 0x01" % S(S_IRQ), "    jz .ok",
            "    test byte [%s], 0x04" % P, "    jz .hbi",
            ".ok:",
            "    cmp bp, 0", "    jle .slice",
        ]
        hb = ".hbr"
        if self.c64:
            t["entry"] += self.c64_fes(".hbr")
        else:
            lo, hi = self.p["ct_lo"], self.p["io_hi"] << 8
            t["entry"] += ["    lea bx, [si+0x%04X]" % ((0x10000 - lo) & 0xFFFF),
                           "    cmp bx, 0x%04X" % (hi - lo), "    jb .hbr"]
        if self.p["ct_top"]:
            t["entry"] += ["    cmp si, 0xFFFE", "    jae .hbr"]
        t["entry"] += self.aot_or_fetch(hook=True) + [
            ".slice: jmp x_slice",
            ".hbi: jmp x_hbi0",
            ".hbr: jmp x_hbr",
        ]
        t["exits"] = [
            "x_hb1:",                       # SI is past the opcode: rewind it
            "    dec si",
            "    mov dl, %d" % RSN_HB1, "    jmp short x_out",
            "x_hbi:",                       # the interrupt flag's, from a handler
            "    dec si",
            "x_hbi0:",                      # ...and from the entry (SI at the opcode)
            "    mov dl, %d" % RSN_HBI, "    jmp short x_out",
            "x_hbr:",
            "    mov dl, %d" % RSN_HBR, "    jmp short x_out",
            "x_slice:",
            "    mov dl, %d" % RSN_SLICE,
            "x_out:",
            "    add bp, [%s]" % S(S_CARRY),
            "    mov word [%s], 0" % S(S_CARRY),
            "    mov [%s], bp" % S(S_DEAD),
            "    pop bx",                   # F+0 the record
            "    pop es",                   # F+2 the package's DS
            "    xor dh, dh",
            "    mov [es:bx+%d], dx" % AM_REASON,
            "    mov [es:bx+%d], si" % AM_PC,
            "    mov [es:bx+%d], bp" % AM_CNT,
            "    mov dl, al", "    mov [es:bx+%d], dx" % AM_A,
            "    mov dl, cl", "    mov [es:bx+%d], dx" % AM_X,
            "    mov dl, ch", "    mov [es:bx+%d], dx" % AM_Y,
            "    mov dx, di", "    and dx, 0x00FF", "    mov [es:bx+%d], dx" % AM_S,
            "    call getp", "    xor dh, dh", "    mov [es:bx+%d], dx" % AM_P,
            "    mov ax, [es:bx+%d]" % AM_REASON,
            "    pop ds", "    pop di", "    pop si", "    pop bp",
            "    retf %d" % retn,
        ]
        t["pflags"] = [
            # getp - AH (N,Z,C) and PV (V,B,D,I) -> a 6502 P byte in DL
            "getp:",
            "    push ax",
            "    mov dl, ah", "    and dl, 0x81",
            "    and ah, 0x40", "    rol ah, 1", "    rol ah, 1", "    rol ah, 1",
            "    or dl, ah",
            "    mov al, [%s]" % P, "    and al, 0x5C", "    or dl, al",
            "    or dl, 0x20",
            "    pop ax",
            "    ret",
            # setp - a 6502 P byte in DL -> AH and PV's four bits (PV's other
            # bits are the package's: the C64 keeps its two flags there)
            "setp:",
            "    push dx",
            "    mov ah, dl", "    and ah, 0x81",
            "    and dl, 0x02", "    ror dl, 1", "    ror dl, 1", "    ror dl, 1",
            "    or ah, dl",
            "    pop dx", "    push dx",
            "    and dl, 0x5C",
            "    and byte [%s], 0xA3" % P, "    or [%s], dl" % P,
            "    pop dx",
            "    ret",
        ]
        # the far doors, read out of the frame: a tail near-called from a
        # handler sees F at BP+4 once it has pushed BP
        # (a tail near-called from a handler pushes BP - the countdown - and
        # sees it at [bp], its return at [bp+2] and F at [bp+4]: the clock is
        # read and written there around a call-out, and the doors are F's)
        door_rd = "    call far [bp+%d]" % (4 + F_IORD)
        door_wr = "    call far [bp+%d]" % (4 + F_IOWR)
        if self.c64:
            t["io"] = self.c64_io()
        else:
            t["io"] = self.apple_io(door_rd, door_wr)
        if self.p.get("kb_latch"):
            t["idle"] = [
                # kbd_idle - THE AUTOSTART ROM'S KEYIN LOOP, run as a counter:
                #   $FD1B INC $4E (5) / BNE $FD21 (3, or 2 and INC $4F (5)) /
                #   $FD21 BIT $C000 (4) / BPL $FD1B (3)
                # Entered from the BIT at $FD21 with no key in the latch,
                # which cannot change inside a run (a2io.c's mirror). Whole
                # passes from that BIT back to it cost 15 cycles, or 19 when
                # $4E wraps, and change nothing but $4E/$4F and flags the
                # next BIT overwrites - so they are taken while one fits with
                # a cycle to spare, and the BIT is then run for real.
                # a2f_image arms IDLEOK only when the ROM holds these bytes.
                "kbd_idle:",
                ".loop:",
                "    cmp bp, 19", "    jle .out",
                "    sub bp, 15",
                "    inc byte [0x004E]", "    jnz .loop",
                "    sub bp, 4",
                "    inc byte [0x004F]",
                "    jmp short .loop",
                ".out:",
                "    mov si, 0xFD21",
            ] + self.fetch_op()
        mark = [
            # the damage mark - BX was just written: the page's bit, "the core
            # wrote something", and the write window when BX is in the watch
            # range (the accurate cores' write path, section 7.5 / 9.2)
        ]
        if self.c64:
            # THE SAME PAGE AS THIS CORE'S LAST MARK, since the flush took the
            # set, is already marked: only the window is left to do. A
            # `DEC $C333 / BNE` delay loop marked one page tens of thousands of
            # times a second, 15% of a 286 (C64-SPEC 4.7). The proof is ANY =
            # 2, which only THIS path writes: the accurate core and the
            # package's writers write 1 and the take writes 0, and either
            # breaks the chain, so a stale page can never be skipped
            if not self.p.get("jit"):
                mark += ["    cmp byte [%s], 2" % S(S_ANY), "    jne .slow",
                         "    cmp bh, [%s]" % S(S_LASTPG), "    je .win",
                         ".slow:"]
            else:
                # ...AND ON A PAGE HOLDING TRANSLATED CODE, ANY = 3: the same
                # page again needs only the byte test (C64-SPEC 4.9.3) - a
                # delay loop's counter kept between a demo's instructions
                # cost this mark's whole slow path every pass
                mark += ["    cmp bh, [%s]" % S(S_LASTPG), "    jne .slow",
                         "    cmp byte [%s], 2" % S(S_ANY), "    je .win",
                         "    cmp byte [%s], 3" % S(S_ANY), "    jne .slow",
                         "    push bx", "    push dx",
                         "    push bp", "    mov bp, sp",
                         "    mov dl, [bp+5]",
                         "    mov bp, [bp+%d]" % (8 + F_JIT),
                         "    or bp, bp", "    jz .nj",
                         "    push es", "    mov es, bp",
                         "    jmp .fine",
                         ".slow:"]
        mark += [
            "    push bx", "    push dx",
            "    mov dl, bh", "    and dl, 7",
            "    mov dh, 0x80",                  # the mask: 0x80 >> (page & 7)
            "    xchg cl, dl", "    shr dh, cl", "    xchg cl, dl",
            "    mov bl, bh", "    xor bh, bh",
            "    shr bl, 1", "    shr bl, 1", "    shr bl, 1",
            "    or [bx + SCRD + 0x%02X], dh" % S_DIRTY]
        if self.p.get("jit"):
            # THE CODE TEST (C64-SPEC 4.9.3): a page holding translated code
            # asks the recompiler for its flush, naming the page. The frame
            # is 8 bytes up (the return, BX, DX, BP); the page is the
            # caller's BH, at [bp+5]
            # A CODE PAGE IS NOT A HIT BY ITSELF: the byte's own bit in the
            # byte map decides (4.9.3) - demos keep their variables between
            # their instructions - and a code page keeps its own same-page
            # short cut (ANY 3), which still makes the byte test
            mark += ["    push bp", "    mov bp, sp",
                     "    mov dl, [bp+5]",
                     "    mov bp, [bp+%d]" % (8 + F_JIT),
                     "    or bp, bp", "    jz .nj",
                     "    push es", "    mov es, bp",
                     "    test [es:bx+0x%04X], dh" % JC_CODE, "    jz .nc",
                     ".fine:",
                     "    mov bp, sp",
                     "    mov bx, [bp+6]",              # the address
                     "    push cx",
                     "    mov cl, bl", "    and cl, 7",
                     "    mov ch, 0x80", "    shr ch, cl",
                     "    shr bx, 1", "    shr bx, 1", "    shr bx, 1",
                     "    test [es:bx+0x%04X], ch" % JC_FINE,
                     "    pop cx",
                     "    jz .cp",
                     "    mov bx, [bp+6]",
                     "    mov [es:0x%04X], bx" % JC_HITA,
                     "    mov [es:0x%04X], dl" % JC_FLUSH,
                     "    mov byte [es:0x%04X], 1" % (JC_FLUSH + 1),
                     ".cp: pop es", "    pop bp", "    pop dx", "    pop bx",
                     "    mov byte [%s], 3" % S(S_ANY),
                     "    mov [%s], bh" % S(S_LASTPG),
                     "    jmp .win",
                     ".nc: pop es",
                     ".nj: pop bp"]
        mark += [
            "    pop dx", "    pop bx",
            "    mov byte [%s], %d" % (S(S_ANY), 2 if self.c64 else 1)]
        if self.c64:
            mark += ["    mov [%s], bh" % S(S_LASTPG), ".win:"]
        mark += [
            "    cmp bx, [%s]" % S(S_WATLO), "    jb .out",
            "    cmp bx, [%s]" % S(S_WATHI), "    ja .out",
            "    cmp bx, [%s]" % S(S_WLO), "    jae .nolo",
            "    mov [%s], bx" % S(S_WLO),
            ".nolo:",
            "    cmp bx, [%s]" % S(S_WHI), "    jbe .out",
            "    mov [%s], bx" % S(S_WHI),
            ".out:",
            "    ret",
        ]
        t["vid"] = [("c64_mark:" if self.c64 else "vid_mark:")] + mark
        return t

    def aot_tails(self):
        if self.c64:
            return self.aot_tails_c64()
        return self.aot_tails_apple()

    def aot_tails_c64(self):
        """THE C64's doors (C64-SPEC 4.8). c64_fes has set ES to SI's fetch
        segment: DS for RAM, a ROM bank's own segment where BASIC or the
        KERNAL is mapped in - which is the one fact that says the translation
        of that PC is the instruction there. The run's end stays exact: a PC
        is entered only with more than MAXBLK left, and every block of the
        translation tests what it costs before it runs (m65aot.py)."""
        fa = self.p["aot_frame"]
        lo, hi = self.p["aot"]
        rom = (lo, hi) == (0xA000, 0x10000)
        lines = ["ct_rom:"]
        if rom:
            lines += ["    mov bx, es", "    mov dx, ds",
                      "    cmp bx, dx", "    je .fetch"]   # RAM under the ROM
        lines += [
            "    cmp bp, %d" % MAXBLK, "    jle .fetch",
            # (a translation never runs in decimal mode: m65aot.py)
            "    test byte [%s], 0x08" % self.PV(), "    jnz .fetch",
            "    mov dx, es",
            "    mov bx, sp",
            "    cmp word [ss:bx+%d], 0" % fa, "    je .fetch",
            "    mov es, [ss:bx+%d]" % (fa + 2),
            "    mov bx, si"]
        go = ["    shl bx, 1",
              "    mov bx, [es:bx]",
              "    test bx, bx", "    jz .none",
              # (the translation's CHRGET is unchecked again: code that ran
              # here may have patched it - m65aot.py's emit_chrget)
              "    and byte [%s], 0x7F" % self.PV(),
              "    xor dl, dl",
              "    mov si, sp"]
        if rom:
            # the table: BASIC's 8K PCs, then the KERNAL's; and each bank's
            # translation in a segment of its own, F+34 and F+40
            lines += ["    sub bh, 0xA0", "    cmp bh, 0x20", "    jb .b",
                      "    sub bh, 0x20"] + go + [
                      "    push word [ss:si+%d]" % (fa + 6), "    push bx", "    retf",
                      ".b:"]
        else:
            lines += ["    sub bx, 0x%04X" % lo]
        lines += go + [
            "    push word [ss:si+%d]" % fa,
            "    push bx",
            "    retf",
            ".none: mov es, dx",
            ".fetch: xor dl, dl"] + self.fetch_op()
        hb = self.lab()
        res = ["    xor dl, dl"] + self.c64_fes(hb)
        if self.p["ct_top"]:
            res += ["    cmp si, 0xFFFE", "    jae %s" % hb]
        res += self.fetch_op() + ["%s: jmp x_hbr" % hb]
        return lines + [
            # xr_resume - from the translation, SI the PC to run next: the
            # check any new PC gets, and no table (the translation came here
            # because that PC has none, or its block did not fit the budget)
            "    times 0x%04X-($-$$) db 0xCC" % XR_RESUME,
            "xr_resume:",
            "    jmp xr_resume_body",
            "    times 0x%04X-($-$$) db 0xCC" % XR_SLICE,
            "xr_slice:",
            "    jmp x_slice",
            "xr_resume_body:"] + res

    def jit_tails(self):
        """the recompiler's door, shared (C64-SPEC 4.9.2)"""
        lines = [
                # ct_jitq - BL the page's PGMAP byte, not 0: a page where
                # blocks start is looked at every time; one that may is
                # sampled, by the countdown's low bits
                "ct_jitq:",
                "    mov dh, %d" % JIT_W_BLOCK,
                "    or bl, bl", "    js ct_jit",
                "    test bp, 0x%02X" % JIT_SAMPLE, "    jnz ct_jitf",
                "    mov dh, %d" % JIT_W_SAMPLE,
                # ct_jit - to the recompiler's hook, SI the PC, DH its weight
                "ct_jit:",
                "    mov bx, sp",
                "    push word [ss:bx+%d]" % F_JIT,
                "    mov bx, 0x%04X" % JIT_HOOK,
                "    push bx",
                "    retf",
                "ct_jitf: xor dl, dl"] + self.fetch_op()
        return lines

    def tracelog(self, marker):
        """DEBUG ONLY (M65AOT_TRACE): MARKER and SI into m65aot's ring"""
        import os
        if not os.environ.get("M65AOT_TRACE"):
            return []
        return ["    pushf", "    push bx", "    mov bx, [0xC2FE]",
                "    mov word [bx+0xC300], 0x%04X" % marker,
                "    mov [bx+0xC302], si",
                "    mov [bx+0xC304], di",
                "    push ax", "    mov ax, [di+1]", "    mov [bx+0xC306], ax",
                "    mov [bx+0xC308], bp", "    pop ax",
                "    add bx, 10", "    and bx, 0xFF", "    mov [0xC2FE], bx",
                "    pop bx", "    popf"]

    def aot_tails_apple(self):
        """THE TRANSLATED ROM's doors (m65aot.py; APPLE2-SPEC 4.7). The frame
        carries the translation's code segment at F+24 (0: none), its PC
        table's at F+26 and this core's own at F+28."""
        lo = self.p["aot"][0]
        return [
            # ct_rom - SI is a ROM PC this core was about to fetch: the PC
            # table's word for it, and a far jump there if it names one
            "ct_rom:"] + self.tracelog(0xCCCC) + [
            "    mov bx, sp",
            "    cmp word [ss:bx+%d], 0" % self.p["aot_frame"], "    je .fetch",
            # (a translation never runs in decimal mode: m65aot.py)
            "    test byte [%s], 0x08" % self.PV(), "    jnz .fetch",
            # ES = the PC table, and the translation keeps it there for its
            # own lookups (this core's Apple II path never reads ES)
            "    mov es, [ss:bx+%d]" % (self.p["aot_frame"] + 2),
            "    mov bx, si", "    shl bx, 1",
            "    mov bx, [es:bx+0x%04X]" % ((-lo * 2) & 0xFFFF),
            "    test bx, bx", "    jz .fetch",
            "    and byte [%s], 0x7F" % self.PV(),    # (CHRGET unchecked)
        ] + (["    inc word [0xC2FC]"] if self.tracelog(0) else []) + [
            "    mov si, sp",
            "    push word [ss:si+%d]" % self.p["aot_frame"],
            "    push bx",
            "    retf",
            ".fetch:",
        ] + self.fetch_op() + [
            # xr_resume (fixed: m65aot.py's XR_RESUME) - from the translation,
            # SI the PC to run next, DL 0: into the interpreter there, after
            # the check any new PC gets (no ROM lookup: the translation came
            # here because that PC has none, or is not in the ROM)
            "    times 0x%04X-($-$$) db 0xCC" % XR_RESUME,
            "xr_resume:",
            "    jmp xr_res_t",
            "    times 0x%04X-($-$$) db 0xCC" % XR_SLICE,
            "xr_slice:",                    # ...and the budget spent there
            "    jmp x_slice",
            "xr_res_t:"] + self.tracelog(0xEEEE) + [
            "    xor dl, dl",
            "    lea bx, [si+0x%04X]" % ((0x10000 - self.p["ct_lo"]) & 0xFFFF),
            "    cmp bx, 0x%04X" % ((self.p["io_hi"] << 8) - self.p["ct_lo"]),
            "    jb .hbr",
        ] + self.fetch_op() + [
            ".hbr: jmp x_hbr",
        ]

    def apple_io(self, door_rd, door_wr):
        S = self.scr
        return [
            # rd_io - BX in $C000-$C1FF (DL = this instruction's cycles) -> DH.
            # $C0xx calls out, with the clock as the accurate core would have
            # it there; $C1xx is slot space and answers $FF.
            "rd_io:",
            "    cmp bh, 0x%02X" % self.p["io_call"], "    jne .ff",
            "    push bp", "    mov bp, sp",
            "    push ax",
            "    mov al, dl", "    xor ah, ah",
            "    mov dx, [bp]", "    sub dx, ax", "    mov [%s], dx" % S(S_DEAD),
            door_rd,
            "    push dx",                  # (DH is the answer)
            "    mov dx, [%s]" % S(S_DEAD), "    add dx, ax", "    mov [bp], dx",
            "    pop dx",
            "    pop ax",
            "    pop bp",
            "    ret",
            ".ff:",
            "    mov dh, 0xFF",
            "    ret",
            # wr_io - DH to BX at or above $C000: $C0xx calls out, the rest drops
            "wr_io:",
            "    cmp bh, 0x%02X" % self.p["io_call"], "    jne .drop",
            "    push bp", "    mov bp, sp",
            "    push ax",
            "    push dx",
            "    mov al, dl", "    xor ah, ah",
            "    mov dx, [bp]", "    sub dx, ax", "    mov [%s], dx" % S(S_DEAD),
            "    pop dx",
            door_wr,
            "    mov dx, [%s]" % S(S_DEAD), "    add dx, ax", "    mov [bp], dx",
            "    pop ax",
            "    pop bp",
            ".drop:",
            "    ret",
        ]

    def c64_fes(self, hb):
        """ES = the fetch segment for a new PC in SI, out of the frame's
        region words (0 = hand back). Below $A000 it is always RAM - DS - with
        no table; at $A000 and up the region table, whose entries are WORD
        indices from page $A0. BP is the countdown, borrowed around the read."""
        r0, done = self.lab(), self.lab()
        return ["    mov bx, si",
                "    cmp bh, 0xA0", "    jb %s" % r0,
                "    mov bl, bh", "    xor bh, bh",
                "    mov bl, [cs:bx+c64_region-0xA0]",
                "    push bp", "    mov bp, sp", "    add bp, bx",
                "    mov bx, [bp+%d]" % (2 + F_EXTRA),          # (no [bp+bx])
                "    pop bp",
                "    test bx, bx", "    jz %s" % hb,
                "    mov es, bx", "    jmp short %s" % done,
                "%s: mov bx, ds" % r0, "    mov es, bx",
                "%s:" % done]

    def c64_io(self):
        S = self.scr
        # THE C64'S READS AT $A000 AND UP and WRITES AT $D000 AND UP, by the
        # bank map the package's c64io.c keeps in MAPA/MAPD/MAPE (C64-SPEC 3.3)
        return [
            # rd_hi - BX >= $A000, DL = this instruction's cycles -> DH
            "rd_hi:",
            "    cmp bh, 0xC0", "    jb .a",
            "    cmp bh, 0xD0", "    jb .ram",
            "    cmp bh, 0xE0", "    jb .d",
            "    cmp byte [%s], 2" % S(S_MAPE), "    jne .ram",       # KERNAL?
            "    push es",
            "    push bp", "    mov bp, sp",
            "    mov es, [bp+%d]" % (6 + F_EXTRA + 6),                # region 3
            "    pop bp",
            "    mov dh, [es:bx]",
            "    pop es",
            "    ret",
            ".a:",
            "    cmp byte [%s], 1" % S(S_MAPA), "    jne .ram",       # BASIC?
            "    push es",
            "    push bp", "    mov bp, sp",
            "    mov es, [bp+%d]" % (6 + F_EXTRA + 2),                # region 1
            "    pop bp",
            "    mov dh, [es:bx]",
            "    pop es",
            "    ret",
            ".ram:",
            "    mov dh, [bx]",
            "    ret",
            ".d:",
            "    cmp byte [%s], 4" % S(S_MAPD), "    je .io",         # I/O?
            "    cmp byte [%s], 3" % S(S_MAPD), "    jne .ram",       # CHARGEN?
            "    push es",
            "    push bp", "    mov bp, sp",
            "    mov es, [bp+%d]" % (6 + F_EXTRA + 8),                # the CHARGEN
            "    pop bp",
            "    mov dh, [es:bx]",
            "    pop es",
            "    ret",
            ".io:",
            "    push bp", "    mov bp, sp",
            "    push ax",
            "    mov al, dl", "    xor ah, ah",
            "    mov dx, [bp]", "    sub dx, ax", "    mov [%s], dx" % S(S_DEAD),
            "    push es",
            "    call far [bp+%d]" % (4 + F_IORD),
            "    pop es",
            "    push dx",                  # (DH is the answer)
            "    mov dx, [%s]" % S(S_DEAD), "    add dx, ax", "    mov [bp], dx",
            "    pop dx",
            "    pop ax",
            "    pop bp",
            "    ret",
            # wr_hi - DH to BX >= $D000: I/O calls out, $FFC0-$FFF9 (the
            # accurate core's scratch) drops, and the rest is RAM, marked
            "wr_hi:",
            "    cmp bh, 0xE0", "    jae .e",
            "    cmp byte [%s], 4" % S(S_MAPD), "    je .io",
            ".ram:",
            "    mov [bx], dh",
            "    jmp c64_mark",
            ".e:",
            "    cmp bx, 0x%04X" % self.p["scr"], "    jb .ram",
            "    cmp bx, 0x%04X" % (self.p["scr"] + 0x3A), "    jae .ram",
            "    ret",
            ".io:",
            "    push bp", "    mov bp, sp",
            "    push ax",
            "    push dx",
            "    mov al, dl", "    xor ah, ah",
            "    mov dx, [bp]", "    sub dx, ax", "    mov [%s], dx" % S(S_DEAD),
            "    pop dx",
            "    push es",
            "    call far [bp+%d]" % (4 + F_IOWR),
            "    pop es",
            "    mov dx, [%s]" % S(S_DEAD), "    add dx, ax", "    mov [bp], dx",
            "    pop ax",
            "    pop bp",
            "    ret",
        ]

    def emit(self):
        p, L = self.p, self.L
        L("; GENERATED by apps/m6502/m65gen.py --machine %s - do not edit" % p["name"],
          "cpu 8086", "bits 16", "org 0",
          "SCR  equ 0x%04X" % p["scr"],
          "SCRD equ 0x%04X - 0x10000" % p["scr"],
          "")
        tails = self.tails()
        place = {0x02: "entry", 0x12: "exits", 0x22: "pflags", 0x32: "io",
                 0x42: "vid", 0x52: "idle", 0x62: "region", 0x72: "aot",
                 0x92: "jit"}
        if self.p.get("aot"):
            tails["aot"] = self.aot_tails()
        if self.p.get("jit"):
            tails["jit"] = self.jit_tails()
        if self.c64:
            tails["region"] = ["c64_region:"] + [
                "    db " + ", ".join(str(v) for v in C64_REGION[i:i + 16])
                for i in range(0xA0, 256, 16)]
        for op in range(PART_LEN >> 8):
            L("", "; --- $%02X %s" % (op, " ".join(map(str, OPS.get(op, ("hand back",))))),
              "    times 0x%04X-($-$$) db 0xCC" % (op * 256),
              "h_%02X:" % op)
            if op in OPS:
                self.handler(op)
            else:
                L("    jmp x_hb1")
                if op == 0x02:
                    L("    dw h_FE, x_hb1          ; FIXUP: the $FE and $FF slots",
                      "    times 0x%04X-($-$$) db 0xCC" % ENTRY)
                if op == 0x03:              # $FE's body, which $FE00 jumps to
                    L("h_FE:")
                    self.handler(0xFE)
                if place.get(op) in tails:
                    L(*tails[place[op]])
        L("", "    times 0x%04X-($-$$) db 0xCC" % PART_LEN,
          "; ASSERTED: the fixup table and the entry are where the package",
          "; looks for them (a negative TIMES is an assembly error)",
          "    times -((entry - $$ - 0x%04X) * (entry - $$ - 0x%04X)) db 0" % (ENTRY, ENTRY),
          "    times -((h_02 - $$ + 3 - 0x%04X) * (h_02 - $$ + 3 - 0x%04X)) db 0" % (FIXUP, FIXUP))
        assert set(tails) <= set(place.values()), "a tail with no slot"
        assert all(op not in OPS for op in place)
        return "\n".join(self.out) + "\n"


def check_cycles(path):
    """the cycle table in a2cpu.inc/c64cpu.inc must agree with OPS"""
    text = open(path).read()
    rows = text.split("a2_cyc:" if "a2_cyc:" in text else "c64_cyc:")[1].split("\n")
    vals = []
    for line in rows[1:]:
        line = line.split(";")[0].strip()
        if not line.startswith("db"):
            break
        vals += [int(v) for v in line[2:].split(",")]
    bad = [op for op, (_, _, c) in OPS.items() if vals[op] != c]
    if bad:
        raise SystemExit("m65gen: cycle table disagrees at %s" %
                         ", ".join("$%02X" % b for b in bad))
    print("m65gen: %d legal opcodes agree with %s" % (len(OPS), path))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="apple2", choices=["apple2", "c64"])
    ap.add_argument("-o", "--out")
    ap.add_argument("--check-cycles")
    ap.add_argument("--aot-range", help="LO:HI hex - the translated range, for a "
                    "test core (a2cputest's translated Dormann)")
    a = ap.parse_args()
    if a.check_cycles:
        check_cycles(a.check_cycles)
    if a.out:
        assert len(OPS) == 151, len(OPS)
        prof = APPLE2 if a.machine == "apple2" else C64
        if a.aot_range:
            lo, hi = (int(v, 16) for v in a.aot_range.split(":"))
            prof = dict(prof, aot=(lo, hi), chrget=None)
        src = Gen(prof).emit()
        with open(a.out, "w") as f:
            f.write(src)


if __name__ == "__main__":
    sys.exit(main())

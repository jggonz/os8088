#!/usr/bin/env python3
"""apps/m6502/m65gen.py - the FAST 6502 core's generator (docs/plans/FAST6502-PLAN.md)

    python3 apps/m6502/m65gen.py --machine apple2 -o build/apple2/a2fast.asm

Writes one NASM source for a flat 64KB segment: the handler for opcode X at
X x 256, dispatched by `mov dh,[si] / inc si / jmp dx` with DL = 0, and the
shared tails (the entry, the exits, the I/O call-outs, the damage marker) in
the slots of opcodes this core does not run. Nothing it writes is committed.

THE REGISTER PLAN (APPLE2-SPEC section 4.6):
    A = AL    N, Z, C in AH, lahf's layout (N bit 7, Z bit 6, C bit 0)
    X = CL    Y = CH    PC = SI    S = DI as $0100+S
    BX = the effective address    DH = data    DL = 0 at every dispatch
    BP = the cycle countdown       DS = the guest's 64KB image
    V, D, I and B in the scratch page's PV byte, at their P positions

EVERY HANDLER ENDS `sub bp,cycles / jle exit / mov dh,[si] / inc si / jmp dx`,
so the run stops before the instruction that finds the budget spent - the
accurate core's rule exactly, and `ran = asked - cnt` is exact.

WHAT IT DOES NOT RUN IT HANDS BACK (x_hb1): every illegal opcode, BRK, ADC and
SBC with D set, the I-flag instructions while an interrupt line is up, and a
read-modify-write above $BFFF. The accurate core runs that one instruction on
the same machine record. A control transfer into $C000-$CFFF (or to $FFFE and
up, where an operand word would wrap the segment) hands back the REGION
(x_hbr), and the accurate core runs until PC leaves it.
"""
import argparse
import sys

# ---------------------------------------------------------------------------
# The legal NMOS opcodes: op -> (mnemonic, mode, base cycles). Every other
# opcode is handed back. Cycles are the accurate core's table (a2_cyc), which
# tools check against this one (m65gen.py --check-cycles FILE).
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

# the read modes that pay a cycle when the index carries into the high byte
PENALTY = {"absx", "absy", "indy"}
READS = {"LDA", "LDX", "LDY", "ORA", "AND", "EOR", "ADC", "SBC", "CMP",
         "CPX", "CPY", "BIT"}
RMW = {"ASL", "LSR", "ROL", "ROR", "INC", "DEC"}
STORES = {"STA": "al", "STX": "cl", "STY": "ch"}
REG = {"A": "al", "X": "cl", "Y": "ch"}

# ---------------------------------------------------------------------------
# The machine profile: the guest's map, and the scratch page both cores share
# ---------------------------------------------------------------------------
APPLE2 = dict(
    name="apple2",
    scr=0xC100,                 # the accurate core's scratch page, A2_SCR_BASE
    io_lo=0xC0, io_hi=0xD0,     # $C000-$CFFF: reads above $C1FF are the image
    io_call=0xC0,               # the page whose reads and writes call out
    ram_top=0xC0,               # a write at or above this page is not RAM
    video=((0x04, 0x0C), (0x20, 0x60)),     # the pages the flush reads
    # A NEW PC AT OR ABOVE THIS (and below $D000, or at $FFFE and up) IS
    # HANDED BACK: $C000-$CFFF is I/O and slot space, and an instruction at
    # $BFFE or $BFFF takes operand bytes from the soft switches, which the
    # image cannot answer. (Fall-through or a branch inside page $BF into
    # $BFFE is not caught - APPLE2-SPEC section 4.6's stated deviation.)
    ct_lo=0xBFFE,
    kb_latch=True,              # $C000-$C00F answered from S_KBV (a2io.c)
)

# scratch offsets - the first block is the accurate core's (a2cpu.inc), the
# second is this core's and the wrapper's (APPLE2-SPEC section 4.6)
S_DIRTY, S_CARRY, S_DEAD, S_IRQ = 0x00, 0x22, 0x24, 0x2A
S_WLO, S_WHI, S_ANY, S_WATLO, S_WATHI = 0x2C, 0x2E, 0x30, 0x32, 0x34
S_PV, S_RSN, S_HBK, S_CUTF = 0x38, 0x39, 0x3A, 0x3B
S_PKGDS, S_REC, S_IORD, S_IOWR, S_SPSAVE = 0x3C, 0x3E, 0x40, 0x44, 0x48
S_KBV, S_KBOK = 0x4A, 0x4B      # the keyboard latch, mirrored by a2io.c

# the machine record (a2cpu.inc's _a2_m)
AM_PC, AM_A, AM_X, AM_Y, AM_S, AM_P, AM_CNT, AM_REASON = 4, 6, 8, 10, 12, 14, 16, 18

RSN_SLICE, RSN_HB1, RSN_HBR = 0, 2, 3
ENTRY = 0x0210                  # in opcode $02's slot, after its hand-back stub
# A PART IS AT MOST 65,024 BYTES (os88pkg.py: its length is a sector count in a
# word), and the slots of $FE and $FF begin at $FE00. So the part is the
# segment's first $FE00 bytes, and the package copies it into a 64KB claim of
# its own and writes those two slots as near jumps: $FE (INC abs,X) to its
# body in a spare slot, $FF (not a legal opcode) to the hand-back. The two
# targets are a table at FIXUP, which the package reads out of the part.
PART_LEN = 0xFE00
FIXUP = 0x0203                  # dw h_FE body, x_hb1 - in $02's slot


class Gen:
    def __init__(self, prof):
        self.p = prof
        self.out = []
        self.n = 0

    def scr(self, off):
        return "SCR+0x%02X" % off

    def L(self, *lines):
        self.out.extend(lines)

    def lab(self):
        self.n += 1
        return ".g%d" % self.n

    # --- the end of every handler ----------------------------------------
    def next(self, cyc):
        """charge CYC and dispatch the next opcode, or leave through x_slice"""
        x = self.lab()
        self.L("    sub bp, %d" % cyc, "    jle %s" % x,
               "    mov dh, [si]", "    inc si", "    jmp dx",
               "%s: jmp x_slice" % x)

    def next_ct(self, cyc):
        """...after a control transfer: SI is a new PC, which may be in the
        I/O region (or at the segment's end), and that is handed back"""
        x, tc, go, hb = self.lab(), self.lab(), self.lab(), self.lab()
        self.L("    sub bp, %d" % cyc, "    jle %s" % x,
               "    cmp si, 0x%04X" % self.p["ct_lo"], "    jae %s" % tc,
               "%s: mov dh, [si]" % go, "    inc si", "    jmp dx",
               "%s: cmp si, 0x%04X" % (tc, self.p["io_hi"] << 8), "    jb %s" % hb,
               "    cmp si, 0xFFFE", "    jb %s" % go,
               "%s: jmp x_hbr" % hb,
               "%s: jmp x_slice" % x)

    # --- flags -----------------------------------------------------------
    def nz(self, r):
        """N and Z from R, C kept: sahf puts C in CF, inc/dec leave CF alone"""
        self.L("    sahf", "    inc %s" % r, "    dec %s" % r, "    lahf")

    def v_from_of(self):
        v1, vd = self.lab(), self.lab()
        self.L("    jo %s" % v1,
               "    and byte [%s], 0xBF" % self.scr(S_PV), "    jmp short %s" % vd,
               "%s: or byte [%s], 0x40" % (v1, self.scr(S_PV)), "%s:" % vd)

    # --- addressing: the effective address into BX ------------------------
    def ea(self, mode, penalty, pen_lab):
        """BX = the effective address. On a PENALTY mode an index carry jumps
        to PEN_LAB (which charges the cycle and comes back)"""
        L = self.L
        if mode == "zp":
            L("    mov bl, [si]", "    inc si", "    xor bh, bh")
        elif mode in ("zpx", "zpy"):
            L("    mov bl, [si]", "    inc si",
              "    add bl, %s" % ("cl" if mode == "zpx" else "ch"), "    xor bh, bh")
        elif mode in ("abs", "absx", "absy"):
            L("    mov bx, [si]", "    add si, 2")
            if mode != "abs":
                self.index("cl" if mode == "absx" else "ch", penalty, pen_lab)
        elif mode == "indx":
            L("    mov bl, [si]", "    inc si", "    add bl, cl", "    xor bh, bh",
              "    mov dl, [bx]", "    inc bl", "    mov bh, [bx]",
              "    mov bl, dl", "    xor dl, dl")
        elif mode == "indy":
            L("    mov bl, [si]", "    inc si", "    xor bh, bh",
              "    mov dl, [bx]", "    inc bl", "    mov bh, [bx]",
              "    mov bl, dl", "    xor dl, dl")
            self.index("ch", penalty, pen_lab)
        else:
            raise ValueError(mode)

    def index(self, reg, penalty, pen_lab):
        if penalty:
            back = self.lab()
            self.L("    add bl, %s" % reg, "    jc %s" % pen_lab[0], "%s:" % back)
            pen_lab.append(back)
        else:
            self.L("    add bl, %s" % reg, "    adc bh, 0")

    def pen_tail(self, pen_lab):
        if len(pen_lab) == 2:
            self.L("%s: inc bh" % pen_lab[0], "    dec bp", "    jmp short %s" % pen_lab[1])

    # --- memory ----------------------------------------------------------
    def read(self, mode, dst, cyc):
        """DST = the byte at BX (zero page and the stack are always RAM)"""
        if mode in ("zp", "zpx", "zpy"):
            self.L("    mov %s, [bx]" % dst)
            return
        r, k, io = self.lab(), self.lab(), self.lab()
        self.L("    cmp bh, 0x%02X" % self.p["io_lo"], "    jb %s" % r,
               "    cmp bh, 0x%02X" % (self.p["io_call"] + 2), "    jb %s" % io,
               "%s: mov %s, [bx]" % (r, dst), "%s:" % k)
        self.later.append(["%s: mov dl, %d" % (io, cyc), "    call rd_io",
                           "    xor dl, dl"]
                          + (["    mov %s, dh" % dst] if dst != "dh" else [])
                          + ["    jmp %s" % k])

    def write(self, mode, src, cyc):
        """the byte SRC to BX: RAM, the I/O call-out, or dropped; and the
        damage mark on the display pages"""
        if mode in ("zp", "zpx", "zpy"):
            self.L("    mov [bx], %s" % src)
            return
        wd, wv, wh = self.lab(), self.lab(), self.lab()
        (a0, a1), (b0, b1) = self.p["video"]
        self.L("    cmp bh, 0x%02X" % self.p["ram_top"], "    jae %s" % wh,
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

        L = self.L
        pen = [self.lab()] if mode in PENALTY and mn in READS else []
        operand = mode not in ("imp", "acc", "rel")

        if mn == "BRK":
            L("    jmp x_hb1")
            return
        if mn in ("ADC", "SBC"):
            L("    test byte [%s], 0x08" % self.scr(S_PV), "    jnz %s" % handback())
        if mn in ("CLI", "SEI", "PLP", "RTI"):
            L("    cmp byte [%s], 0" % self.scr(S_IRQ), "    jne %s" % handback())

        if mn in READS:
            if mode == "imm":
                L("    mov dh, [si]", "    inc si")
                src = "dh"
            else:
                self.ea(mode, mode in PENALTY, pen)
                dst = {"LDA": "al", "LDX": "cl", "LDY": "ch"}.get(mn, "dh")
                self.read(mode, dst, cyc)
                src = dst
            self.alu(mn, src)
            self.next(cyc)
        elif mn in STORES:
            self.ea(mode, False, pen)
            self.write(mode, STORES[mn], cyc)
            self.next(cyc)
        elif mn in RMW and mode == "acc":
            self.shift(mn, "al")
            self.next(cyc)
        elif mn in RMW:
            self.ea(mode, False, pen)
            if mode not in ("zp", "zpx"):
                L("    cmp bh, 0x%02X" % self.p["ram_top"], "    jae %s" % handback(2))
            L("    mov dh, [bx]")
            self.shift(mn, "dh")
            self.write(mode, "dh", cyc)
            self.next(cyc)
        elif mode == "rel":
            self.branch(mn)
        else:
            self.misc(mn, mode, cyc, handback)

        self.pen_tail(pen)
        for blk in self.later:
            L(*blk)
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
              "    and byte [%s], 0xBF" % self.scr(S_PV),
              "    or [%s], dl" % self.scr(S_PV), "    xor dl, dl")

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
        L = self.L
        test = {"BPL": ("ah", 0x80, "jnz"), "BMI": ("ah", 0x80, "jz"),
                "BVC": ("pv", 0x40, "jnz"), "BVS": ("pv", 0x40, "jz"),
                "BCC": ("ah", 0x01, "jnz"), "BCS": ("ah", 0x01, "jz"),
                "BNE": ("ah", 0x40, "jnz"), "BEQ": ("ah", 0x40, "jz")}[mn]
        reg, mask, not_taken = test
        nt, back, xf, xb, done = (self.lab() for _ in range(5))
        where = "byte [%s]" % self.scr(S_PV) if reg == "pv" else "ah"
        L("    mov dh, [si]", "    inc si",
          "    test %s, 0x%02X" % (where, mask), "    %s %s" % (not_taken, nt),
          "    mov bx, si",
          "    test dh, dh", "    js %s" % back,
          "    add bl, dh", "    jc %s" % xf,
          "    mov si, bx")
        self.next(3)
        L("%s: add bl, dh" % back, "    jnc %s" % xb, "    mov si, bx")
        self.next(3)
        L("%s:" % nt)
        self.next(2)
        L("%s: inc bh" % xf, "    jmp short %s" % done,
          "%s: dec bh" % xb,
          "%s: mov si, bx" % done)
        self.next_ct(4)

    def misc(self, mn, mode, cyc, handback):
        L, P = self.L, self.scr(S_PV)
        if mn == "JMP" and mode == "abs":
            L("    mov si, [si]")
            self.next_ct(cyc)
        elif mn == "JMP":                       # (ind), with the NMOS page wrap
            L("    mov bx, [si]",
              "    cmp bh, 0x%02X" % self.p["io_lo"], "    jae %s" % handback(),
              "    mov dl, [bx]", "    inc bl", "    mov dh, [bx]",
              "    mov si, dx", "    xor dl, dl")
            self.next_ct(cyc)
        elif mn == "JSR":
            L("    mov bx, [si]", "    inc si", "    mov dx, si")
            self.push("dh")
            self.push("dl")
            L("    xor dl, dl", "    mov si, bx")
            self.next_ct(cyc)
        elif mn == "RTS":
            self.pop("bl")
            self.pop("bh")
            L("    inc bx", "    mov si, bx")
            self.next_ct(cyc)
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
        P, S = self.scr(S_PV), self.scr
        t = {}
        t["entry"] = [
            # far-called: AX = cycles, DX = the guest image's segment,
            # ES = the package's data segment, BX = its machine record
            "entry:",
            "    push bp", "    push si", "    push di", "    push ds", "    push es",
            "    mov ds, dx",
            "    mov [%s], es" % S(S_PKGDS), "    mov [%s], bx" % S(S_REC),
            "    mov [%s], sp" % S(S_SPSAVE),
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
            # an interrupt pending at the top of a run is the accurate core's
            "    test byte [%s], 0x02" % S(S_IRQ), "    jnz .hb",
            "    test byte [%s], 0x01" % S(S_IRQ), "    jz .ok",
            "    test byte [%s], 0x04" % P, "    jz .hb",
            ".ok:",
            "    cmp si, 0x%04X" % self.p["ct_lo"], "    jb .go",
            "    cmp si, 0x%04X" % (self.p["io_hi"] << 8), "    jb .hbr",
            "    cmp si, 0xFFFE", "    jae .hbr",
            ".go:",
            "    cmp bp, 0", "    jle .slice",
            "    mov dh, [si]", "    inc si", "    jmp dx",
            ".slice: jmp x_slice",
            ".hb: jmp x_hb0",
            ".hbr: jmp x_hbr",
        ]
        t["exits"] = [
            "x_hb1:",                       # SI is past the opcode: rewind it
            "    dec si",
            "x_hb0:",
            "    mov byte [%s], %d" % (S(S_RSN), RSN_HB1), "    jmp short x_out",
            "x_hbr:",
            "    mov byte [%s], %d" % (S(S_RSN), RSN_HBR), "    jmp short x_out",
            "x_slice:",
            "    mov byte [%s], %d" % (S(S_RSN), RSN_SLICE),
            "x_out:",
            "    add bp, [%s]" % S(S_CARRY),
            "    mov word [%s], 0" % S(S_CARRY),
            "    mov [%s], bp" % S(S_DEAD),
            "    mov es, [%s]" % S(S_PKGDS),
            "    mov bx, [%s]" % S(S_REC),
            "    mov [es:bx+%d], si" % AM_PC,
            "    mov [es:bx+%d], bp" % AM_CNT,
            "    xor dh, dh",
            "    mov dl, al", "    mov [es:bx+%d], dx" % AM_A,
            "    mov dl, cl", "    mov [es:bx+%d], dx" % AM_X,
            "    mov dl, ch", "    mov [es:bx+%d], dx" % AM_Y,
            "    mov dx, di", "    and dx, 0x00FF", "    mov [es:bx+%d], dx" % AM_S,
            "    call getp", "    xor dh, dh", "    mov [es:bx+%d], dx" % AM_P,
            "    mov dl, [%s]" % S(S_RSN), "    mov [es:bx+%d], dx" % AM_REASON,
            "    mov ax, dx",
            "    mov sp, [%s]" % S(S_SPSAVE),
            "    pop es", "    pop ds", "    pop di", "    pop si", "    pop bp",
            "    retf",
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
            # setp - a 6502 P byte in DL -> AH and PV
            "setp:",
            "    push dx",
            "    mov ah, dl", "    and ah, 0x81",
            "    and dl, 0x02", "    ror dl, 1", "    ror dl, 1", "    ror dl, 1",
            "    or ah, dl",
            "    pop dx", "    push dx",
            "    and dl, 0x5C", "    mov [%s], dl" % P,
            "    pop dx",
            "    ret",
        ]
        t["io"] = [
            # rd_io - BX in $C000-$C1FF (DL = this instruction's cycles) -> DH.
            # $C0xx calls out, with the clock as the accurate core would have
            # it there; $C1xx is slot space and answers $FF.
            "rd_io:",
            "    cmp bh, 0x%02X" % self.p["io_call"], "    jne .ff",
        ] + ([
            # THE KEYBOARD LATCH, answered here while a2io.c says it may be
            # ($C000-$C00F is the latch alone while no paste is in flight)
            "    cmp bl, 0x10", "    jae .call",
            "    cmp byte [%s], 0" % S(S_KBOK), "    je .call",
            "    mov dh, [%s]" % S(S_KBV),
            "    ret",
            ".call:",
        ] if self.p.get("kb_latch") else []) + [
            "    push ax",
            "    mov al, dl", "    xor ah, ah",
            "    mov dx, bp", "    sub dx, ax", "    mov [%s], dx" % S(S_DEAD),
            "    call far [%s]" % S(S_IORD),
            "    mov bp, [%s]" % S(S_DEAD), "    add bp, ax",
            "    pop ax",
            "    ret",
            ".ff:",
            "    mov dh, 0xFF",
            "    ret",
            # wr_io - DH to BX at or above $C000: $C0xx calls out, the rest drops
            "wr_io:",
            "    cmp bh, 0x%02X" % self.p["io_call"], "    jne .drop",
            "    push ax",
            "    push dx",
            "    mov al, dl", "    xor ah, ah",
            "    mov dx, bp", "    sub dx, ax", "    mov [%s], dx" % S(S_DEAD),
            "    pop dx",
            "    call far [%s]" % S(S_IOWR),
            "    mov bp, [%s]" % S(S_DEAD), "    add bp, ax",
            "    pop ax",
            ".drop:",
            "    ret",
        ]
        t["vid"] = [
            # vid_mark - BX was just written on a display page: the page's bit,
            # "the core wrote something", and the write window when BX is in
            # the watch range - the accurate core's a2_wr_bx, kept to the
            # pages the flush reads
            "vid_mark:",
            "    push bx", "    push dx",
            "    mov dl, bh", "    and dl, 7",
            "    mov dh, 0x80",                  # a2_dmask: 0x80 >> (page & 7)
            "    xchg cl, dl", "    shr dh, cl", "    xchg cl, dl",
            "    mov bl, bh", "    xor bh, bh",
            "    shr bl, 1", "    shr bl, 1", "    shr bl, 1",
            "    or [bx + SCRD + 0x%02X], dh" % S_DIRTY,
            "    pop dx", "    pop bx",
            "    mov byte [%s], 1" % S(S_ANY),
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
        return t

    def emit(self):
        p = self.p
        L = self.L
        L("; GENERATED by apps/m6502/m65gen.py --machine %s - do not edit" % p["name"],
          "cpu 8086", "bits 16", "org 0",
          "SCR  equ 0x%04X" % p["scr"],
          "SCRD equ 0x%04X - 0x10000" % p["scr"],
          "")
        tails = self.tails()
        # the slots of opcodes this core hands back hold the shared tails
        spare = [op for op in range(256) if op not in OPS]
        place = {0x02: "entry", 0x12: "exits", 0x22: "pflags", 0x32: "io",
                 0x42: "vid"}
        for op in range(256):
            if op >= PART_LEN >> 8:
                break
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
                if op in place:
                    L(*tails[place[op]])
        L("", "    times 0x%04X-($-$$) db 0xCC" % PART_LEN,
          "; ASSERTED: the fixup table and the entry are where the package",
          "; looks for them (a negative TIMES is an assembly error)",
          "    times -((entry - $$ - 0x%04X) * (entry - $$ - 0x%04X)) db 0" % (ENTRY, ENTRY),
          "    times -((h_02 - $$ + 3 - 0x%04X) * (h_02 - $$ + 3 - 0x%04X)) db 0" % (FIXUP, FIXUP))
        assert set(place.values()) == set(tails), "a tail with no slot"
        assert all(op in spare for op in place)
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
    ap.add_argument("--machine", default="apple2", choices=["apple2"])
    ap.add_argument("-o", "--out")
    ap.add_argument("--check-cycles")
    a = ap.parse_args()
    if a.check_cycles:
        check_cycles(a.check_cycles)
    if a.out:
        assert len(OPS) == 151, len(OPS)
        src = Gen(APPLE2).emit()
        with open(a.out, "w") as f:
            f.write(src)


if __name__ == "__main__":
    sys.exit(main())

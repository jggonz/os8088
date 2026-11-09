#!/usr/bin/env python3
"""tools/c64trace.py - which C64 ROM addresses run (FAST6502, C64-SPEC 4.8)

    python3 tools/c64trace.py build/c64-rom/C64.ROM -o build/c64/c64rom.seeds

tools/a2trace.py one machine along. Boots the KERNAL on tools/py6502.py's
6502 with the least of a C64 around it - 64K of RAM under the processor
port's banking, a raster counter for CINT's PAL/NTSC test, CIA 1's timer
interrupt every 1/60 s so the KERNAL's IRQ handler (the clock, the cursor,
the keyboard scan) runs, every key row reading "none" - types the BASIC
programs below into the keyboard buffer at the editor's wait loop, runs them,
and writes every BASIC and KERNAL PC an instruction started at. Those are
m65aot.py's seeds: C64 BASIC dispatches its statements through RTS on
addresses it pushes, which no static walk can follow. A ROM PC that is not a
seed is simply not translated, and the fast core runs it - slower, never wrong.

C64.ROM is the package's part 0 layout (c64cpu.inc's C64_ROMS_*): the
KERNAL at 0, BASIC at $2000, the CHARGEN at $4000.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from py6502 import CPU, Jam                     # noqa: E402

PROGRAMS = [
    # arithmetic, the functions, printing
    "PRINT 6*7, SQR(2), 1E9/7, -3^3, 2^0.5",
    "PRINT SIN(1);COS(1);TAN(1);ATN(1);LOG(10);EXP(1)",
    "PRINT INT(-2.5);ABS(-3);SGN(-2);RND(1)>0;RND(-1)",
    "PRINT 1/3;1E38*10>0;12345678.9;.000001234;-0.5",
    # strings
    'A$="HELLO WORLD":PRINT LEFT$(A$,5);MID$(A$,7,3);RIGHT$(A$,2);LEN(A$)',
    'B$=STR$(3.25)+CHR$(65):PRINT B$;ASC("Z");VAL("12.5E1");A$<B$',
    'C$="":FOR I=1 TO 20:C$=C$+CHR$(64+I):NEXT:PRINT C$',
    # the program, its control flow and data
    "NEW",
    "10 DIM A(20),B$(5),C%(3)",
    "20 FOR I=0 TO 20 STEP 2:A(I)=I*I:NEXT I",
    "30 GOSUB 200:ON 2 GOTO 40,50",
    '40 PRINT "NO"',
    "50 READ X,Y$:IF X>2 THEN PRINT Y$;X",
    "60 RESTORE:DEF FN F(Z)=Z*Z+1:PRINT FN F(3)",
    '70 IF A(4)=16 AND NOT 0 OR 0 THEN PRINT "OK"',
    "80 X=PEEK(53280):POKE 53281,6:POKE 1024,1:C%(1)=-5:WAIT 1,32",
    '90 PRINT CHR$(18);"REV";CHR$(146);TAB(5);SPC(2);POS(0);FRE(0)',
    "100 X=0:FOR J=1 TO 50:X=X+J*1.5:NEXT:PRINT X;TI>=0;TI$",
    "110 GET K$:PRINT K$;",
    "120 B$(1)=\"Q\":PRINT B$(1),C%(1)*2;STATUS;CHR$(147)",
    "130 FOR K=1 TO 30:PRINT K;:NEXT:PRINT",
    "140 END",
    '200 PRINT "SUB";:RETURN',
    '300 DATA 5,"DATA"',
    "LIST",
    "RUN",
    "LIST 10-50",
    "CLR:CONT",
    "RUN 130",
]

ROM_KERNAL, ROM_BASIC, ROM_CHARGEN = 0x0000, 0x2000, 0x4000
EDITOR_WAIT = 0xE5CD                    # LDA $C6 / ... / BEQ $E5CD
IRQ_EVERY = 17045                       # CIA 1's timer A, as IOINIT sets it


class C64(CPU):
    """the bus: RAM, the processor port at $00/$01 choosing BASIC, KERNAL,
    I/O and the CHARGEN (the PLA's eight cases without a cartridge), a VIC
    raster counter, CIA 1's interrupt flag and an idle keyboard"""

    def __init__(self, rom):
        super().__init__(bytearray(65536))
        self.rom = rom
        self.ddr, self.port = 0x2F, 0x37
        self.icr = 0

    def bank(self):
        return (self.port | ~self.ddr) & 7

    def read(self, a):
        a &= 0xFFFF
        if a == 0:
            return self.ddr
        if a == 1:
            return (self.port & self.ddr) | (0x17 & ~self.ddr)
        b = self.bank()
        if 0xA000 <= a < 0xC000 and b & 3 == 3:
            return self.rom[ROM_BASIC + a - 0xA000]
        if a >= 0xE000 and b & 2:
            return self.rom[ROM_KERNAL + a - 0xE000]
        if 0xD000 <= a < 0xE000 and b & 3:
            if not b & 4:
                return self.rom[ROM_CHARGEN + a - 0xD000]
            return self.io(a)
        return self.mem[a]

    def io(self, a):
        if a == 0xD012:
            return (self.cycles // 63) % 312 & 0xFF
        if a == 0xD011:
            return 0x1B | (0x80 if (self.cycles // 63) % 312 > 255 else 0)
        if a in (0xDC00, 0xDC01):
            return 0xFF                 # no key down, no joystick
        if a == 0xDC0D:
            v, self.icr = self.icr, 0
            return v
        return 0

    def write(self, a, v):
        a &= 0xFFFF
        v &= 0xFF
        if a == 0:
            self.ddr = v
        elif a == 1:
            self.port = v
        if 0xD000 <= a < 0xE000 and self.bank() & 3 and self.bank() & 4:
            return                      # I/O
        self.mem[a] = v

    def irq(self):
        self.push(self.pc >> 8)
        self.push(self.pc & 0xFF)
        self.push((self.p & ~0x10) | 0x20)
        self.p |= 0x04
        self.pc = self.rd16(0xFFFE)
        self.cycles += 7
        if self.dyn is not None:
            self.dyn.add(self.pc)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--screen", action="store_true", help="print the text screen at the end")
    a = ap.parse_args()
    rom = open(a.rom, "rb").read()
    m = C64(rom)
    m.trace = set()
    m.dyn = set()                       # (an entry by RTS, JMP (ind) or
                                        # an interrupt: written `XXXX D`)
    m.pc = m.rd16(0xFFFC)
    keys = []
    for line in PROGRAMS:
        keys += [ord(c) for c in line] + [13]
    next_irq = IRQ_EVERY
    idle = steps = 0
    while steps < 60_000_000:
        try:
            m.step()
        except Jam as e:
            raise SystemExit("c64trace: %s" % e)
        steps += 1
        if m.cycles >= next_irq:
            next_irq += IRQ_EVERY
            m.icr = 0x81
        if m.icr and not m.p & 0x04:
            m.irq()
        if m.pc == EDITOR_WAIT and m.mem[0xC6] == 0:
            if keys:
                m.mem[0x0277] = keys.pop(0)
                m.mem[0xC6] = 1
            else:
                idle += 1
                if idle > 200:
                    break
    if a.screen or keys:
        for r in range(25):
            row = m.mem[0x400 + r * 40:0x400 + r * 40 + 40]
            print("".join(chr(c + 64) if 1 <= c <= 26 else chr(c) if 32 <= c < 64
                          else " " for c in row))
    if keys:
        raise SystemExit("c64trace: %d keys never typed - the editor did not "
                         "come back to $%04X" % (len(keys), EDITOR_WAIT))
    pcs = sorted(pc for pc in m.trace if 0xA000 <= pc < 0xC000 or pc >= 0xE000)
    with open(a.out, "w") as f:
        f.write("# C64.ROM's executed PCs - tools/c64trace.py, %d instructions\n" % steps)
        for pc in pcs:
            f.write("%04X%s\n" % (pc, " D" if pc in m.dyn else ""))
    print("c64trace: %d instructions, %d ROM PCs -> %s" % (steps, len(pcs), a.out))


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""tools/a2trace.py - which Apple II ROM addresses run (FAST6502, APPLE2-SPEC 4.7)

    python3 tools/a2trace.py build/apple2-rom/APPLE2.ROM -o build/apple2/a2rom.seeds

Boots the Autostart ROM on tools/py6502.py's 6502 with the least of an Apple
II+ around it - 48K of RAM, the keyboard latch and strobe, every other soft
switch reading 0 - types the BASIC programs below at the `]` prompt, runs
them, and writes every ROM PC an instruction started at, one hex address a
line. Those are m65aot.py's SEEDS: the Applesoft interpreter dispatches its
statements and functions through RTS on addresses it pushes, which no static
walk of the ROM can follow. A ROM PC that is not a seed is simply not
translated, and the fast core runs it - slower, never wrong.

The programs are the seeds' whole input, so they are chosen to reach what
programs do: every statement Applesoft has that does not need a disk or a
cassette, the numeric and string functions, lo-res and hi-res graphics.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from py6502 import CPU, Jam                     # noqa: E402

PROGRAMS = [
    # arithmetic, the functions, printing
    "PRINT 6*7, SQR(2), 1E9/7, -3^3, 2^0.5",
    "PRINT SIN(1); COS(1); TAN(1); ATN(1); LOG(10); EXP(1)",
    "PRINT INT(-2.5); ABS(-3); SGN(-2); RND(1) > 0; 7 MOD 0" .replace(" MOD 0", ""),
    "PRINT 1/3; 1E38*10 > 0; 12345678.9; .000001234",
    # strings
    'A$="HELLO WORLD":PRINT LEFT$(A$,5);MID$(A$,7,3);RIGHT$(A$,2);LEN(A$)',
    'B$=STR$(3.25)+CHR$(65):PRINT B$;ASC("Z");VAL("12.5E1")',
    'C$="":FOR I=1 TO 20:C$=C$+CHR$(64+I):NEXT:PRINT C$',
    # the program, its control flow and data
    "NEW",
    "10 DIM A(20),B$(5)",
    "20 FOR I=0 TO 20 STEP 2:A(I)=I*I:NEXT I",
    "30 GOSUB 200:ON 2 GOTO 40,50",
    "40 PRINT \"NO\"",
    "50 READ X,Y$:IF X>2 THEN PRINT Y$;X",
    "60 RESTORE:DEF FN F(Z)=Z*Z+1:PRINT FN F(3)",
    "70 IF A(4)=16 AND NOT 0 OR 0 THEN PRINT \"OK\"",
    "80 X=PEEK(232):POKE 768,X:CALL -936",
    "90 HTAB 5:VTAB 10:INVERSE:PRINT \"INV\":NORMAL:FLASH:PRINT \"F\":NORMAL",
    "100 GR:COLOR=3:PLOT 5,5:HLIN 1,20 AT 7:VLIN 1,20 AT 9:TEXT",
    "110 HGR:HCOLOR=3:HPLOT 0,0 TO 100,100 TO 200,10:TEXT",
    "120 B$(1)=\"Q\":PRINT B$(1);FRE(0)>0;POS(0);SCRN(5,5)",
    "130 X=0:FOR J=1 TO 50:X=X+J*1.5:NEXT:PRINT X",
    "140 END",
    "200 PRINT \"SUB\";:RETURN",
    "300 DATA 5,\"DATA\"",
    "LIST",
    "RUN",
    "LIST 10,50",
    "PRINT SPC(3);TAB(10);\"X\"",
]


class Apple(CPU):
    """the bus: RAM below $C000, the latch at $C000 and the strobe at $C010,
    slot space $FF, the ROM above $CFFF"""

    def __init__(self, rom):
        super().__init__(bytearray(65536))
        self.rom = rom
        self.keys = []
        self.latch = 0

    def read(self, a):
        a &= 0xFFFF
        if a < 0xC000:
            return self.mem[a]
        if a >= 0xD000:
            return self.rom[a - 0xD000]
        if a < 0xC010:
            if not self.latch & 0x80 and self.keys:
                self.latch = self.keys.pop(0) | 0x80
            return self.latch
        if a < 0xC020:
            self.latch &= 0x7F
            return 0
        if a < 0xC100:
            return 0
        return 0xFF

    def write(self, a, v):
        a &= 0xFFFF
        if a < 0xC000:
            self.mem[a] = v & 0xFF
        elif 0xC010 <= a < 0xC020:
            self.latch &= 0x7F


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--screen", action="store_true", help="print the text page at the end")
    a = ap.parse_args()
    rom = open(a.rom, "rb").read()[:0x3000]
    m = Apple(rom)
    m.trace = set()
    m.dyn = set()                       # (an entry by RTS, JMP (ind) or
                                        # an interrupt: written `XXXX D`)
    m.pc = m.rd16(0xFFFC)
    for line in PROGRAMS:
        m.keys += [ord(c) for c in line] + [13]
    idle = 0
    steps = 0
    while steps < 40_000_000:
        try:
            m.step()
        except Jam as e:
            raise SystemExit("a2trace: %s" % e)
        steps += 1
        if m.pc == 0xFD1B and not m.keys and not m.latch & 0x80:
            idle += 1
            if idle > 2000:
                break
    rompcs = sorted(pc for pc in m.trace if pc >= 0xD000)
    with open(a.out, "w") as f:
        f.write("# APPLE2.ROM's executed PCs - tools/a2trace.py, %d instructions\n" % steps)
        for pc in rompcs:
            f.write("%04X%s\n" % (pc, " D" if pc in m.dyn else ""))
    print("a2trace: %d instructions, %d ROM PCs -> %s" % (steps, len(rompcs), a.out))
    if a.screen:
        for r in range(24):
            base = 0x400 + (r & 7) * 128 + (r >> 3) * 40
            print("".join(chr(b & 0x7F) if 32 <= (b & 0x7F) < 127 else " "
                          for b in m.mem[base:base + 40]))


if __name__ == "__main__":
    sys.exit(main())

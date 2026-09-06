#!/usr/bin/env python3
"""OS88NET.COM unpacks to OS88NET, by RUNNING the stub (SPEC.md 62.12).

    python3 tests/unit/t_sfx.py

The shipped OS88NET.COM is a self-extracting archive: `build/os88net.raw` is
the program, tools/os88sfx.py packs it, and the ~96 bytes of
drivers/net/os88sfx.asm in front of the payload put it back on the DOS machine
at the far end of SPEC.md 62's cable.  Nothing in this container runs DOS, so
without this file the packer, the stub and the format would all be checked by
the same author's arithmetic and nothing else.

**THAT IS EXACTLY HOW OS88NET.COM SHIPPED BROKEN TWICE.**  tests/dosstub's
opening records it: the DOS end was assembled, packaged and sent to the field
twice without one instruction of it ever being executed, and came back
"returns to prompt instantly with nothing printed".  A packer whose output is
checked only by its own decoder is that same shape - the two agree by
construction and neither is the thing that ships.

So this EXECUTES THE SHIPPED BYTES.  The interpreter below is a few dozen
opcodes of 8086 - only the ones os88sfx.asm actually emits - and it runs
build/os88net.com out of a simulated 64KB segment exactly as DOS enters a
.COM: the file at 0x100, DS = ES = SS = the PSP, and IP at the first byte.  It
passes when execution reaches 0x100 again with the whole of
build/os88net.raw sitting there, byte for byte.

WHAT THAT CATCHES that the packer's own round trip cannot: a stub that
relocates the wrong range, an offset built the wrong way round, a `jmp` that
is relative where it had to be absolute, a bit buffer whose refill eats the
carry it was about to return - which is the one worth naming, because it does
not fail at the first bit.  It fails at the ninth, in a stream that keeps
decoding into plausible rubbish, hundreds of bytes after the mistake.

WHAT IT DOES NOT CATCH: whether the far machine has the memory (the packer
asserts that, and the staged body ends below the program's own buffers,
which main() checks against SEG_CEILING below), and whether the unpacked program then
WORKS, which is `make dosstub`'s job and MartyPC's.  This says the archive is
an archive of the right thing and that the code in front of it is the code
that opens it.

THE INTERPRETER IS DELIBERATELY NARROW.  It refuses an opcode it does not
know, loudly, naming the offset - so it can never quietly "pass" a stub that
grew an instruction it does not model.  A wider one would be a worse test: an
8086 written to be general is a second large thing to be wrong, and the whole
value here is that this one is small enough to read in full.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, eq, done                       # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, "build")

SEG = 0x10000                    # a .COM owns one 64KB segment
ORG = 0x100                      # ...and DOS enters it here
SEG_CEILING = 0xF000             # os88net.asm's own ceiling for its buffers:
                                 # DOS's stack lives above this
MAX_STEPS = 4_000_000            # generous: the real run is ~1.4M instructions


class Trap(Exception):
    pass


class CPU:
    """Just enough 8086 to run drivers/net/os88sfx.asm, and no more.

    One segment, so no segment registers to model - every access below is
    already the only one the stub can make.  Flags are carry and zero only,
    which is all it branches on, and each is set exactly where the hardware
    sets it: `inc` and `dec` leave CF ALONE, which is the property note 3 of
    os88sfx.asm depends on, so getting it wrong here would hide the bug this
    file is most for.
    """

    def __init__(self, mem, ip, sp=0xFFFC):
        self.m = mem
        self.r = {"ax": 0, "bx": 0, "cx": 0, "dx": 0, "si": 0, "di": 0,
                  "sp": sp, "bp": 0}
        self.ip = ip
        self.cf = 0
        self.zf = 0
        self.df = 0
        self.steps = 0

    # --- memory and registers ---------------------------------------------
    def b(self, a):
        return self.m[a & 0xFFFF]

    def setb(self, a, v):
        self.m[a & 0xFFFF] = v & 0xFF

    def fetch8(self):
        v = self.m[self.ip]
        self.ip = (self.ip + 1) & 0xFFFF
        return v

    def fetch16(self):
        lo = self.fetch8()
        return lo | (self.fetch8() << 8)

    def s8(self):
        v = self.fetch8()
        return v - 256 if v & 0x80 else v

    def push(self, v):
        self.r["sp"] = (self.r["sp"] - 2) & 0xFFFF
        self.setb(self.r["sp"], v & 0xFF)
        self.setb(self.r["sp"] + 1, (v >> 8) & 0xFF)

    def pop(self):
        v = self.b(self.r["sp"]) | (self.b(self.r["sp"] + 1) << 8)
        self.r["sp"] = (self.r["sp"] + 2) & 0xFFFF
        return v

    def lo(self, n):
        return self.r[n] & 0xFF

    def setlo(self, n, v):
        self.r[n] = (self.r[n] & 0xFF00) | (v & 0xFF)

    def sethi(self, n, v):
        self.r[n] = (self.r[n] & 0x00FF) | ((v & 0xFF) << 8)

    # --- one instruction ---------------------------------------------------
    def step(self):
        at = self.ip
        op = self.fetch8()

        if op == 0x1E:                       # push ds  (the value is not
            self.push(0)                     # modelled: one segment)
        elif op == 0x07:                     # pop es
            self.pop()
        elif op == 0xFC:                     # cld
            self.df = 0
        elif op == 0xFD:                     # std
            self.df = 1
        elif 0xB8 <= op <= 0xBF:             # mov r16, imm16
            self.r[["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"][op - 0xB8]] \
                = self.fetch16()
        elif 0xB0 <= op <= 0xB7:             # mov r8, imm8
            i = op - 0xB0
            (self.setlo if i < 4 else self.sethi)(
                ["ax", "cx", "dx", "bx"][i & 3], self.fetch8())
        elif op == 0xA4:                     # movsb
            self.movsb()
        elif op == 0xF3:                     # rep <movsb>
            nxt = self.fetch8()
            if nxt != 0xA4:
                raise Trap("rep prefix on unmodelled opcode %#04x at %#06x"
                           % (nxt, at))
            while self.r["cx"]:
                self.movsb()
                self.r["cx"] = (self.r["cx"] - 1) & 0xFFFF
        elif op == 0xAC:                     # lodsb
            self.setlo("ax", self.b(self.r["si"]))
            self.r["si"] = (self.r["si"] + (-1 if self.df else 1)) & 0xFFFF
        elif op == 0xE8:                     # call rel16
            d = self.fetch16()
            self.push(self.ip)
            self.ip = (self.ip + (d - 65536 if d & 0x8000 else d)) & 0xFFFF
        elif op == 0xC3:                     # ret
            self.ip = self.pop()
        elif op == 0xE9:                     # jmp rel16
            d = self.fetch16()
            self.ip = (self.ip + (d - 65536 if d & 0x8000 else d)) & 0xFFFF
        elif op == 0xEB:                     # jmp rel8
            d = self.s8()                    # fetch FIRST: the displacement is
            self.ip = (self.ip + d) & 0xFFFF  # relative to the byte after it
        elif op in (0x72, 0x73, 0x74, 0x75):  # jc/jb, jnc/jae, jz, jnz
            d = self.s8()
            take = {0x72: self.cf, 0x73: not self.cf,
                    0x74: self.zf, 0x75: not self.zf}[op]
            if take:
                self.ip = (self.ip + d) & 0xFFFF
        elif op == 0x92:                     # xchg ax, dx
            self.r["ax"], self.r["dx"] = self.r["dx"], self.r["ax"]
        elif 0x40 <= op <= 0x47:             # inc r16 - LEAVES CF ALONE
            n = ["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"][op - 0x40]
            self.r[n] = (self.r[n] + 1) & 0xFFFF
            self.zf = self.r[n] == 0
        elif 0x48 <= op <= 0x4F:             # dec r16 - LEAVES CF ALONE
            n = ["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"][op - 0x48]
            self.r[n] = (self.r[n] - 1) & 0xFFFF
            self.zf = self.r[n] == 0
        elif 0x50 <= op <= 0x57:             # push r16
            self.push(self.r[["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"][op - 0x50]])
        elif 0x58 <= op <= 0x5F:             # pop r16
            self.r[["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"][op - 0x58]] = self.pop()
        elif op == 0xD1:                     # shr r16, 1 / shl r16, 1
            self.shift1(self.fetch8(), at)
        elif op == 0xFF:                     # jmp r16 (the absolute one)
            m = self.fetch8()
            if m != 0xE0:
                raise Trap("unmodelled FF /%d at %#06x" % ((m >> 3) & 7, at))
            self.ip = self.r["ax"]
        elif op in (0x89, 0x8B, 0x29, 0x2B, 0x31, 0x33, 0x11, 0x13, 0x39, 0x3B,
                    0x88, 0x8A):
            self.alu_rm(op, at)
        elif op == 0x81 or op == 0x83:       # grp1 r/m16, imm
            self.grp1(op, at)
        else:
            raise Trap("unmodelled opcode %#04x at %#06x" % (op, at))

        self.steps += 1
        if self.steps > MAX_STEPS:
            raise Trap("ran away: %d instructions without reaching %#06x"
                       % (self.steps, ORG))

    def movsb(self):
        self.setb(self.r["di"], self.b(self.r["si"]))
        d = -1 if self.df else 1
        self.r["si"] = (self.r["si"] + d) & 0xFFFF
        self.r["di"] = (self.r["di"] + d) & 0xFFFF

    def shift1(self, modrm, at):
        if modrm >> 6 != 3:
            raise Trap("shift on memory at %#06x" % at)
        n = ["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"][modrm & 7]
        kind = (modrm >> 3) & 7
        v = self.r[n]
        if kind == 5:                        # shr
            self.cf = v & 1
            v >>= 1
        elif kind == 4:                      # shl
            self.cf = 1 if v & 0x8000 else 0
            v = (v << 1) & 0xFFFF
        else:
            raise Trap("unmodelled D1 /%d at %#06x" % (kind, at))
        self.r[n] = v
        self.zf = v == 0

    def rm(self, modrm, at):
        """Only the forms os88sfx.asm emits: a register, or [si]."""
        mod, rm = modrm >> 6, modrm & 7
        if mod == 3:
            return ("r", ["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"][rm])
        if mod == 0 and rm == 4:
            return ("m", self.r["si"])
        raise Trap("unmodelled addressing mod=%d rm=%d at %#06x" % (mod, rm, at))

    def alu_rm(self, op, at):
        modrm = self.fetch8()
        w = op not in (0x88, 0x8A)
        regs = ["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"] if w else \
               ["ax", "cx", "dx", "bx", "ax", "cx", "dx", "bx"]
        ri = (modrm >> 3) & 7
        reg = regs[ri]
        kind, ref = self.rm(modrm, at)

        def read_rm():
            if kind == "r":
                return self.r[ref] if w else \
                    (self.lo(ref) if ri < 4 or True else 0)
            return self.b(ref)

        def rm8():
            i = modrm & 7
            n = ["ax", "cx", "dx", "bx", "ax", "cx", "dx", "bx"][i]
            return (self.r[n] >> (0 if i < 4 else 8)) & 0xFF

        def write_rm(v):
            if kind == "r":
                self.r[ref] = v & 0xFFFF
            else:
                self.setb(ref, v)

        def reg8():
            return (self.r[reg] >> (0 if ri < 4 else 8)) & 0xFF

        def set_reg8(v):
            (self.setlo if ri < 4 else self.sethi)(reg, v)

        if op == 0x8A:                       # mov r8, r/m8
            set_reg8(rm8() if kind == "r" else self.b(ref))
            return
        if op == 0x88:                       # mov r/m8, r8
            v = reg8()
            if kind == "r":
                i = modrm & 7
                n = ["ax", "cx", "dx", "bx", "ax", "cx", "dx", "bx"][i]
                (self.setlo if i < 4 else self.sethi)(n, v)
            else:
                self.setb(ref, v)
            return
        if op == 0x89:                       # mov r/m16, r16
            write_rm(self.r[reg])
            return
        if op == 0x8B:                       # mov r16, r/m16
            self.r[reg] = read_rm()
            return

        a = self.r[reg] if op in (0x2B, 0x33, 0x13, 0x3B) else read_rm()
        b = read_rm() if op in (0x2B, 0x33, 0x13, 0x3B) else self.r[reg]
        if op in (0x29, 0x2B):               # sub
            res = (a - b) & 0xFFFF
            self.cf = 1 if a < b else 0
        elif op in (0x31, 0x33):             # xor
            res = a ^ b
            self.cf = 0
        elif op in (0x11, 0x13):             # adc
            t = a + b + self.cf
            self.cf = 1 if t > 0xFFFF else 0
            res = t & 0xFFFF
        elif op in (0x39, 0x3B):             # cmp - result discarded
            res = (a - b) & 0xFFFF
            self.cf = 1 if a < b else 0
            self.zf = res == 0
            return
        else:
            raise Trap("unmodelled alu %#04x at %#06x" % (op, at))
        self.zf = res == 0
        if op in (0x2B, 0x33, 0x13):
            self.r[reg] = res
        else:
            write_rm(res)

    def grp1(self, op, at):
        modrm = self.fetch8()
        kind, ref = self.rm(modrm, at)
        imm = self.fetch16() if op == 0x81 else self.s8() & 0xFFFF
        a = self.r[ref] if kind == "r" else self.b(ref)
        k = (modrm >> 3) & 7
        if k == 7:                           # cmp
            self.cf = 1 if a < imm else 0
            self.zf = ((a - imm) & 0xFFFF) == 0
            return
        if k == 5:                           # sub
            res = (a - imm) & 0xFFFF
            self.cf = 1 if a < imm else 0
        elif k == 0:                         # add
            t = a + imm
            self.cf = 1 if t > 0xFFFF else 0
            res = t & 0xFFFF
        else:
            raise Trap("unmodelled grp1 /%d at %#06x" % (k, at))
        self.zf = res == 0
        if kind == "r":
            self.r[ref] = res
        else:
            self.setb(ref, res)


def main():
    com = os.path.join(BUILD, "os88net.com")
    raw = os.path.join(BUILD, "os88net.raw")
    for p in (com, raw):
        if not os.path.exists(p):
            print("t_sfx: SKIP - %s not built (make build/os88net.com)"
                  % os.path.relpath(p, ROOT))
            return 0

    packed = open(com, "rb").read()
    want = open(raw, "rb").read()

    eq(packed[0] != 0, True,
       "OS88NET.COM does not start with a zero byte",
       "DOS enters a .COM at its FIRST byte; a 0 there is `add [bx+si],al`")
    check(len(packed) < len(want),
          "the packed .COM (%d) is smaller than the program (%d)"
          % (len(packed), len(want)),
          "the archive exists to save room on the apps disk; if it is not "
          "smaller it is only costing the far machine time")

    # --- the staging arithmetic os88sfx.asm's notes 2 and 3 rest on ---------
    stage = ORG + len(want)
    top = stage + len(packed)
    check(top <= SEG_CEILING,
          "the staged body ends at %#06x, at or below %#06x" % (top, SEG_CEILING),
          "a .COM is ONE 64KB segment with DOS's stack at the top of it")

    # --- and now RUN IT, the way DOS would --------------------------------
    mem = bytearray(SEG)
    mem[ORG:ORG + len(packed)] = packed
    cpu = CPU(mem, ORG)
    try:
        while True:
            if cpu.ip == ORG and cpu.steps:      # back at the entry: unpacked
                break
            cpu.step()
    except Trap as e:
        check(False, "the stub ran to completion", str(e))
        done("t_sfx")

    got = bytes(mem[ORG:ORG + len(want)])
    if not eq(got, want, "the stub reconstructs build/os88net.raw exactly",
              "the far machine runs whatever came out of here; a stream that "
              "decodes to plausible rubbish is the failure this catches"):
        done("t_sfx")

    check(cpu.r["di"] == ORG + len(want),
          "it stopped with di at %#06x, one past the image" % (ORG + len(want)))

    print("t_sfx: ran %d instructions of real 8086; %d packed -> %d bytes "
          "(%.1f%%, %d saved), staged %#06x..%#06x"
          % (cpu.steps, len(packed), len(want),
             100.0 * len(packed) / len(want), len(want) - len(packed),
             stage, top))
    done("t_sfx")


if __name__ == "__main__":
    main()

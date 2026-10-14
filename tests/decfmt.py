#!/usr/bin/env python3
"""apps/os88dec.inc, the decimal formatter, run on the host against Python.

    python3 tests/decfmt.py [-v]

The include (SPEC.md 20.5.2) is assembled into a stub seven ways - every
capability alone and in the combinations a package asks for: the 16-bit core
with the signed and field arms, the 32-bit core, and each core WITH thousands
separators (which dec_u16 and dec_s16 then share) - and every entry is run
in Unicorn's 8086 core over a grid of values and checked against Python's own
formatting: dec_u16, dec_s16, dec_u32 and dec_fld at every width from 1 to 7,
including the widths too narrow for the number (which keep the LOW digits and
never write in front of DI). Each call must keep every register but DI, put DI
where the contract says, and write nothing outside the bytes it owns.

Milliseconds, where a package on MartyPC would be minutes a value.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX,
                                   UC_X86_REG_CX, UC_X86_REG_DX,
                                   UC_X86_REG_SI, UC_X86_REG_DI,
                                   UC_X86_REG_BP, UC_X86_REG_SP,
                                   UC_X86_REG_CS, UC_X86_REG_DS,
                                   UC_X86_REG_ES, UC_X86_REG_SS,
                                   UC_X86_REG_IP, UC_X86_REG_FLAGS)
except ImportError:                     # pragma: no cover
    print("decfmt: cannot run - unicorn is not installed (pip install unicorn)")
    sys.exit(2)

SEG = 0x1000
BUF = 0x5000
PRE = 16                                # guard bytes in front of the buffer
FILL = 0xA5

# Each entry is `call <it>` (3 bytes) + `hlt` at a 4-byte stride.
ENTRIES = ("dec_u16", "dec_s16", "dec_fld", "dec_u32")
VARIANTS = {
    "w16":    ("DEC_S16", "DEC_FLD"),
    "w32":    ("DEC_S16", "DEC_FLD", "DEC_U32"),
    "sep16":  ("DEC_U16", "DEC_SEP"),
    "sep32":  ("DEC_S16", "DEC_FLD", "DEC_U32", "DEC_SEP"),
    "sepw":   ("DEC_U32", "DEC_SEP"),
    "fld":    ("DEC_FLD",),
    "u32":    ("DEC_U32",),
}
REGS = (("AX", UC_X86_REG_AX), ("BX", UC_X86_REG_BX), ("CX", UC_X86_REG_CX),
        ("DX", UC_X86_REG_DX), ("SI", UC_X86_REG_SI), ("BP", UC_X86_REG_BP),
        ("DS", UC_X86_REG_DS), ("ES", UC_X86_REG_ES), ("SS", UC_X86_REG_SS),
        ("SP", UC_X86_REG_SP))


NEEDS = {"dec_u16": ("DEC_U16",), "dec_s16": ("DEC_S16",),
         "dec_fld": ("DEC_FLD",), "dec_u32": ("DEC_U32",)}


def implied(defines):
    have = set(defines)
    if "DEC_S16" in have:
        have.add("DEC_U16")
    return have


def has(have, entry):
    return all(d in have for d in NEEDS[entry])


def build(tmp, defines):
    have = implied(defines)
    src = ["cpu 8086", "org 0"]
    src += ["%%define %s" % d for d in defines]
    for e in ENTRIES:
        if not has(have, e):
            src += ["    hlt", "    hlt", "    hlt", "    hlt"]
            continue
        src += ["    call %s" % e, "    hlt"]
    src.append('%include "os88dec.inc"')
    asm = os.path.join(tmp, "decstub.asm")
    open(asm, "w").write("\n".join(src) + "\n")
    binf = os.path.join(tmp, "decstub.bin")
    subprocess.run(["nasm", "-f", "bin", "-w+error",
                    "-I", os.path.join(ROOT, "apps") + os.sep,
                    "-o", binf, asm], check=True)
    return open(binf, "rb").read()


class Stub:
    def __init__(self, code):
        self.code = code
        self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        self.mu.mem_map(0, 1 << 20)
        self.mu.mem_write(SEG * 16, code)

    def call(self, entry, ax=0, cx=0, dx=0):
        mu = self.mu
        base = SEG * 16
        mu.mem_write(base + BUF - PRE, bytes([FILL]) * (PRE + 64))
        regs = {"AX": ax, "BX": 0x2222, "CX": cx, "DX": dx, "SI": 0x6666,
                "BP": 0x5555, "DS": SEG, "ES": 0x0060, "SS": SEG,
                "SP": 0xFFF0}
        for nm, r in REGS:
            mu.reg_write(r, regs[nm])
        mu.reg_write(UC_X86_REG_DI, BUF)
        mu.reg_write(UC_X86_REG_CS, SEG)
        mu.reg_write(UC_X86_REG_FLAGS, 0x0002)
        off = 4 * ENTRIES.index(entry)
        mu.reg_write(UC_X86_REG_IP, off)
        bad = []
        try:
            mu.emu_start(base + off, base + off + 3, count=5000)
        except Exception as e:           # a runaway that ate the stub
            self.__init__(self.code)
            return 0, bytes(64), ["crashed: %s" % e]
        if mu.reg_read(UC_X86_REG_IP) != off + 3:
            bad.append("did not return")
        for nm, r in REGS:
            got = mu.reg_read(r)
            if got != regs[nm]:
                bad.append("%s came back %04X, not %04X" % (nm, got, regs[nm]))
        mem = bytes(mu.mem_read(base + BUF - PRE, PRE + 64))
        if any(b != FILL for b in mem[:PRE]):
            bad.append("wrote in front of DI")
        return mu.reg_read(UC_X86_REG_DI) - BUF, mem[PRE:], bad


def sep3(n):
    return "{:,}".format(n)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    rng = random.Random(8088)
    w16 = sorted({0, 1, 9, 10, 11, 99, 100, 999, 1000, 1001, 9999, 10000,
                  12345, 32767, 32768, 32769, 65534, 65535}
                 | {rng.randrange(65536) for _ in range(40)})
    w32 = sorted({0, 1, 65535, 65536, 99999, 100000, 999999, 1000000,
                  123456789, 1234567890, 4294967295}
                 | {rng.randrange(1 << 32) for _ in range(40)})
    fails = []
    calls = 0
    bdir = os.path.join(ROOT, "build")
    os.makedirs(bdir, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=bdir, prefix="decfmt-") as tmp:
        stubs = {k: Stub(build(tmp, v)) for k, v in VARIANTS.items()}

    def check(var, entry, want, n_di, args):
        nonlocal calls
        calls += 1
        di, mem, bad = stubs[var].call(entry, **args)
        tag = "%s %s(%s)" % (var, entry, ", ".join("%s=%d" % kv
                                                   for kv in args.items()))
        fails.extend("%s: %s" % (tag, b) for b in bad)
        got = mem[:len(want)].decode("latin-1")
        tail = mem[len(want):]
        if entry == "dec_fld":
            spill = tail                  # no NUL: nothing past the field
        else:
            if tail[:1] != b"\0":
                fails.append("%s: no NUL after %r" % (tag, got))
            spill = tail[1:]
        if got != want:
            fails.append("%s is %r, want %r" % (tag, got, want))
        if any(b != FILL for b in spill):
            fails.append("%s wrote past its end: %r" % (tag, mem))
        if di != n_di:
            fails.append("%s left DI at +%d, want +%d" % (tag, di, n_di))
        if a.verbose:
            print("decfmt %-4s %-34s %r" % (var, tag, got))

    for var, defs in VARIANTS.items():
        have = implied(defs)
        fmt = sep3 if "DEC_SEP" in have else str
        for v in w16:
            if has(have, "dec_u16"):
                s = fmt(v)
                check(var, "dec_u16", s, len(s), {"ax": v})
            sv = v - 65536 if v >= 32768 else v
            s = ("-" + fmt(-sv)) if sv < 0 else fmt(sv)
            if has(have, "dec_s16"):
                check(var, "dec_s16", s, len(s), {"ax": v})
            for w in range(1, 8):
                d = str(v)[-w:]
                if has(have, "dec_fld"):
                    check(var, "dec_fld", d.rjust(w), w, {"ax": v, "cx": w})
        for v in w32:
            if has(have, "dec_u32"):
                s = fmt(v)
                check(var, "dec_u32", s, len(s),
                      {"ax": v & 0xFFFF, "dx": v >> 16})
    for f in fails[:40]:
        print("decfmt: FAIL " + f)
    print("decfmt: %d calls over %d builds, %d failure(s)"
          % (calls, len(VARIANTS), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

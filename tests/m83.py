#!/usr/bin/env python3
"""apps/os88m83.inc, the 8.3 mangle, run on the host against its reference.

    python3 tests/m83.py [-v]

SPEC.md 77.20's rule had two implementations - FTPD's `fd_mangle83` and
Telnet's `tz_mangle` - and `tests/telzm.py` stopped them drifting by putting
Telnet's on a wire and reading `[tz_name]` back. They had drifted anyway, in
the corners no wire reached (`x. ` came out `X~1.` on one side; a `\\` was a
separator on one side and an illegal byte on the other). There is ONE copy now,
`apps/os88m83.inc`, and both packages assemble it - so this row tests THE
implementation, not an agreement between two:

  * every row of `tools/os88bbs.py`'s MANGLE83_CASES - all SIXTEEN, including
    the four (two pathnames, `...`, the empty name) that no sender can put on a
    wire and telzm therefore never asserts;
  * the corners the old copies disagreed on, in EXTRA below;
  * that every result is a FIXED POINT (the round trip LIST -> RETR depends on
    it), and agrees with `os88bbs.mangle83` itself, which is the reference;
  * that the routine keeps every register, DS and SP, and writes nothing past
    the NUL it ends the name with.

The include is assembled into a stub - `call m83_mangle / hlt` and the file -
and run in Unicorn's 8086 core, which is milliseconds a case where a package
on MartyPC would be minutes and would need a NIC MartyPC has not got.
telzm still asserts the same table THROUGH Telnet, on the machine, which is
the integration half: that the package hands the right bytes in and stores
what comes out.
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bbs                                             # noqa: E402

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
    print("m83: SKIP - unicorn is not installed (pip install unicorn)")
    sys.exit(77)

SEG = 0x1000                            # code, data and stack: one segment
SRC = 0x4000                            # the pathname
DST = 0x5000                            # the 13-byte output, and a guard band
GUARD = 32
FILL = 0xA5

# The corners the two old copies disagreed on, and a few the table lacks.
EXTRA = (
    ("x. ", "X~1"),                     # Telnet wrote X~1. - a dot, no ext
    ("x.!!!!", "X~1.!!!"),
    ("a\\b.txt", "b.txt"),              # FTPD did not cut at a backslash
    ("dir/", "FILE~1"),                 # a trailing separator: an empty leaf
    ("ABCDEFGH.TXT", "ABCDEFGH.TXT"),   # exactly 8.3, the widest legal
    ("ABCDEFGHI.TXT", "ABCDEF~1.TXT"),  # one over in the stem
    ("ABCDEFGH.TXTX", "ABCDEF~1.TXT"),  # one over in the extension
    ("very long folder/banana split.mod", "BANANA~1.MOD"),
    ("\xe9t\xe9.txt", "T~1.TXT"),       # a high byte is not legal
    ("A.", "A~1"),                      # a trailing dot
    (".", "FILE~1"),
    ("my.dir/long name", "LONGNA~1"),   # a FOLDER's dot is not the leaf's
)


def build(out):
    asm = os.path.join(out, "m83stub.asm")
    with open(asm, "w") as fh:
        fh.write("cpu 8086\norg 0\n    call m83_mangle\n    hlt\n"
                 "%include \"os88m83.inc\"\n")
    binf = os.path.join(out, "m83stub.bin")
    subprocess.run(["nasm", "-f", "bin", "-w+error",
                    "-I", os.path.join(ROOT, "apps") + os.sep,
                    "-o", binf, asm], check=True)
    return open(binf, "rb").read()


REGNAMES = ("AX", "BX", "CX", "DX", "SI", "DI", "BP", "DS", "ES", "SS", "SP",
            "CS")


def run(code, name):
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 1 << 20)
    base = SEG * 16
    mu.mem_write(base, code)
    src = name.encode("latin-1") + b"\0"
    mu.mem_write(base + SRC, src)
    mu.mem_write(base + DST, bytes([FILL]) * (13 + GUARD))
    regs = ((UC_X86_REG_AX, 0x1111), (UC_X86_REG_BX, 0x2222),
            (UC_X86_REG_CX, 0x3333), (UC_X86_REG_DX, 0x4444),
            (UC_X86_REG_SI, SRC), (UC_X86_REG_DI, DST),
            (UC_X86_REG_BP, 0x5555), (UC_X86_REG_DS, SEG),
            (UC_X86_REG_ES, 0x0060), (UC_X86_REG_SS, SEG),
            (UC_X86_REG_SP, 0xFFF0), (UC_X86_REG_CS, SEG))
    for r, v in regs:
        mu.reg_write(r, v)
    mu.reg_write(UC_X86_REG_FLAGS, 0x0002)          # DF=0, SPEC.md 1
    mu.reg_write(UC_X86_REG_IP, 0)
    hlt = 3                             # call rel16 is three bytes
    mu.emu_start(base, base + hlt, count=200000)
    bad = []
    ip = mu.reg_read(UC_X86_REG_IP)
    if ip != hlt:
        bad.append("did not return (IP %04X)" % ip)
    for (r, v), nm in zip(regs, REGNAMES):
        got = mu.reg_read(r)
        if got != v:
            bad.append("%s came back %04X, not %04X" % (nm, got, v))
    out = bytes(mu.mem_read(base + DST, 13 + GUARD))
    if 0 not in out[:13]:
        bad.append("no NUL in the 13-byte buffer: %r" % out[:13])
        return None, bad
    n = out.index(0)
    if any(b != FILL for b in out[n + 1:]):
        bad.append("wrote past its NUL: %r" % out)
    if bytes(mu.mem_read(base + SRC, len(src))) != src:
        bad.append("the source was written to")
    return out[:n].decode("latin-1"), bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    bdir = os.path.join(ROOT, "build")
    os.makedirs(bdir, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=bdir, prefix="m83-") as tmp:
        code = build(tmp)
    fails = []
    rows = list(os88bbs.MANGLE83_CASES) + list(EXTRA)
    for src, want in rows:
        ref = os88bbs.mangle83(src)
        if ref != want:
            fails.append("the REFERENCE disagrees with this row: "
                         "mangle83(%r) = %r, want %r" % (src, ref, want))
        got, bad = run(code, src)
        fails += ["%r: %s" % (src, b) for b in bad]
        if got is None:
            continue
        again, bad = run(code, got)
        fails += ["%r (second pass): %s" % (got, b) for b in bad]
        ok = got == want and again == got
        if a.verbose or not ok:
            print("m83 %-4s %-36r -> %-15r%s" % (
                "ok" if ok else "FAIL", src, got,
                "" if again == got else " -> %r NOT A FIXED POINT" % again))
        if got != want:
            fails.append("m83_mangle(%r) is %r, want %r" % (src, got, want))
        elif again != got:
            fails.append("m83_mangle(%r) is not a fixed point: %r -> %r"
                         % (src, got, again))
    for f in fails:
        print("m83: FAIL " + f)
    print("m83: %d rows (%d of the reference table, %d corners), %d failure(s)"
          % (len(rows), len(os88bbs.MANGLE83_CASES), len(EXTRA), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

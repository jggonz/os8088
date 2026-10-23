#!/usr/bin/env python3
"""x3cycmarty.py - check tools/x3cyc.py's 8088 model against MartyPC's
cycle-accurate 5150, on the XT backend's two largest kernels (XT-STUDY.md
'Cycle model', 'Calibration').

    python3 tools/x3cycmarty.py            # needs nasm and build/martypc/run

A DESIGN-DECIDING measurement, taken once and recorded in XT-STUDY.md; not a
suite row. No os8088 code runs: the machine boots GLaBIOS on a floppy whose
boot sector spins (so the BIOS has programmed the PIT and the refresh DMA,
which the 8088's real speed includes), the debugger pauses it, writes a
kernel repeated R times at 2000:0000 and zeroed tables at 3000:0000 (so no
branch or shift depends on data), PARKs the CPU on it (registers cleared,
IF = 0, the prefetch queue flushed) and runs it to a breakpoint on `done`.
The cycle delta at two R values gives the per-unit slope, which drops the
setup and the queue's first fill.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import x3cyc  # noqa: E402
import x3xtkern as K  # noqa: E402

PRELUDE = """cpu 8086
bits 16
org 0
%assign i 0
%rep 16
ROW %+ i equ 1000h + i * 512
%assign i i + 1
%endrep
RSTRIDE equ 64
HLO equ 9000h
HHI equ 9100h
LLO equ 9200h
LHI equ 9300h
start:
    mov ax,3000h
    mov ds,ax
    mov ax,4000h
    mov es,ax
    cld
    xor bx,bx
    xor si,si
    mov di,200h
"""


def body_macro(lines):
    """The kernel as a macro whose `clip` is macro-local (a jo there is a
    short jump, and a not-taken one costs the same wherever it points)."""
    out = ["%macro BODY 0"]
    for ln in lines:
        code = ln.split(";", 1)[0].rstrip()
        if not code:
            continue
        code = code.replace("clip", "%%clip")
        out.append("    " + code)
    out += ["%%clip:", "%endmacro"]
    return "\n".join(out)


def assemble(lines, reps):
    src = PRELUDE.replace("start:", body_macro(lines) + "\nstart:")
    if any("[di+" in l for l in lines):
        src += "    mov di,200h\n"
    src += "%%rep %d\n    BODY\n%%endrep\ndone:\n    nop\n    jmp done\n" % reps
    with tempfile.TemporaryDirectory() as d:
        a = os.path.join(d, "k.asm")
        o = os.path.join(d, "k.bin")
        l_ = os.path.join(d, "k.lst")
        open(a, "w").write(src)
        subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-o", o, "-l", l_, a])
        b = open(o, "rb").read()
        lst = open(l_).read()
    return b, len(b) - 3            # `done`: the nop before the jmp short


def boot_image(path):
    sec = bytearray(512)
    sec[0:2] = b"\xeb\xfe"         # jmp $
    sec[510:512] = b"\x55\xaa"
    img = bytes(sec) + bytes(368640 - 512)
    open(path, "wb").write(img)


def measure(m, binary, done):
    m.pause()
    m.write(0x30000, bytes(65536))
    m.write(0x40000, bytes(65536))
    m.write(0x20000, binary)
    m.bp_exec(0x20000 + done)
    m.cmd(cmd="park", cs=0x2000, ip=0)
    c0 = m.status()["cycles"]
    m.run()
    if m.wait_stop(120.0) is None or m.status()["ip"] != done:
        raise RuntimeError("the kernel never reached done (ip=%04x)" % m.status()["ip"])
    return m.status()["cycles"] - c0


KERNELS = [
    ("window, 16 taps, outputs j and 32-j paired (two outputs)", K.win_pair(16, "lodsw"), 4, 20),
    ("window, 8 taps paired [si+d8] (two outputs)", K.win_pair(8, "rel8"), 4, 20),
    ("Lee DCT-32, byte-split multiplies (one slot)", K.lee_code(32, K.mul_split())[0], 1, 4),
    ("byte-split multiply with its load and store",
     ["mov ax,[di+4]"] + K.mul_split() + ["mov [di+6],dx"], 8, 40),
]


def main():
    import os88marty
    out = []
    with tempfile.TemporaryDirectory() as d:
        img = os.path.join(d, "spin.img")
        boot_image(img)
        with os88marty.launch(img, machine="os8088_5150_cga_gla", boot=6.0,
                              label="x3cycmarty") as m:
            for name, lines, r1, r2 in KERNELS:
                b1, d1 = assemble(lines, r1)
                b2, d2 = assemble(lines, r2)
                c1 = measure(m, b1, d1)
                c2 = measure(m, b2, d2)
                marty = (c2 - c1) / float(r2 - r1)
                model = x3cyc.cost(lines, reps=3, warm=2) if len(lines) < 400 else \
                    x3cyc.cost(lines, reps=2, warm=1)
                out.append((name, marty, model))
    print("%-58s %10s %10s %7s" % ("kernel", "MartyPC", "model", "err"))
    for name, mt, md in out:
        print("%-58s %10.1f %10.1f %+6.1f%%" % (name, mt, md, 100 * (md - mt) / mt))
    return 0


if __name__ == "__main__":
    sys.exit(main())

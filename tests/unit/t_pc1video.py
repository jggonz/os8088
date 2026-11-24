#!/usr/bin/env python3
"""Execute the PC1 refresh probe/transaction, including refusal and IF state.

Assemble the shared production macro, run it with SS != DS, and intercept I/O.
This verifies the software protocol, not the physical Yamaha output frequency.
"""
import pathlib
import struct
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX,
    UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI,
    UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES,
    UC_X86_REG_SS, UC_X86_REG_EFLAGS)

ROOT = pathlib.Path(__file__).resolve().parents[2]
REGS = (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
        UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_ES)


def assemble(directory, mutation=False):
    source = (ROOT / 'kernel/pc1video.inc').read_text()
    if mutation:
        assert 'mov ah, 0x81' in source
        source = source.replace('mov ah, 0x81', 'mov ah, 0x89')
    asm = pathlib.Path(directory) / 'probe.asm'
    binary = pathlib.Path(directory) / 'probe.bin'
    asm.write_text('cpu 8086\nbits 16\norg 0\n'
                   'dw p_apply, p_probe\nCPU_8086 equ 0\n'
                   'cpu_tier equ 0x800\n' + source + '\nPC1_VIDEO p\n')
    subprocess.run(['nasm', '-f', 'bin', '-w+error', '-o', str(binary), str(asm)],
                   check=True)
    return binary.read_bytes()


def run(code, hz=60, rom=0xFE44, tier=0, ports=(0xC0, 0xD0), enabled=True):
    m = Uc(UC_ARCH_X86, UC_MODE_16)
    m.mem_map(0, 0x110000)
    m.mem_write(0x10000, code)
    m.mem_write(0x20800, bytes([tier]))
    m.mem_write(0xFFFFD, struct.pack('<H', rom))
    for r, v in ((UC_X86_REG_CS, 0x1000), (UC_X86_REG_DS, 0x2000),
                 (UC_X86_REG_SS, 0x3000), (UC_X86_REG_SP, 0x7FFE)):
        m.reg_write(r, v)
    for r, v in zip(REGS, (0xA000 | hz, 0x1234, 0x2345, 0x3456,
                          0x4567, 0x5678, 0x6789, 0x789A)):
        m.reg_write(r, v)
    initial = [m.reg_read(r) for r in REGS]
    m.reg_write(UC_X86_REG_EFLAGS, 0x202 if enabled else 2)
    m.mem_write(0x37FFE, struct.pack('<H', 0x900))
    reads, writes = [], []

    def inp(uc, port, size, data):
        assert size == 1
        reads.append(port)
        return {0xFFFB: ports[0], 0xFFF8: ports[1]}[port]

    def out(uc, port, size, value, data):
        assert size == 1 and not (uc.reg_read(UC_X86_REG_EFLAGS) & 0x200)
        writes.append((port, value))

    m.hook_add(UC_HOOK_INSN, inp, None, 1, 0, UC_X86_INS_IN)
    m.hook_add(UC_HOOK_INSN, out, None, 1, 0, UC_X86_INS_OUT)
    m.emu_start(0x10000 + struct.unpack_from('<H', code)[0], 0x10900, count=200)
    assert m.reg_read(UC_X86_REG_SP) == 0x8000, 'stack unbalanced'
    assert [m.reg_read(r) for r in REGS] == initial, 'register clobbered'
    assert bool(m.reg_read(UC_X86_REG_EFLAGS) & 0x200) == enabled
    return reads, writes, bool(m.reg_read(UC_X86_REG_EFLAGS) & 1)


def main():
    with tempfile.TemporaryDirectory(prefix='pc1video-') as d:
        code = assemble(d)
        cases = 0
        for rom in (0xFE44, 0xFE49, 0xFE4A):
            for hz, value in ((50, 0x89), (60, 0x81)):
                for enabled in (False, True):
                    reads, writes, refused = run(code, hz=hz, rom=rom, enabled=enabled)
                    assert reads == [0xFFFB, 0xFFF8]
                    assert writes == [(0xDD, 0x65), (0xDE, value)] and not refused
                    cases += 1
        for kw, expected in (({'rom': 0xFE00}, []), ({'tier': 1}, []),
                             ({'tier': 2}, []), ({'ports': (0xFF, 0xD0)}, [0xFFFB]),
                             ({'ports': (0xC0, 0xFF)}, [0xFFFB, 0xFFF8]),
                             ({'hz': 0}, []), ({'hz': 59}, []), ({'hz': 255}, [])):
            reads, writes, refused = run(code, **kw)
            assert reads == expected and not writes and refused, kw
            cases += 1
        # Deliberately break 60 Hz: the same independent expected transaction
        # must distinguish it from 50 Hz (WRITING-TESTS section 1).
        broken = assemble(d, mutation=True)
        assert run(broken)[1] != [(0xDD, 0x65), (0xDE, 0x81)]
    print(f'pc1video: {cases} execution cases passed; wrong-frequency mutation detected')


if __name__ == '__main__':
    main()

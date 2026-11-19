#!/usr/bin/env python3
"""Optional glyph provider and legacy-copy lifetime, on assembled guest code.

The final keyboard soak registers this host instruction test. It exercises
the actual keyfont.inc, actual FONT_GLYPHS routine and each renderer's glyph
index sequence. No OS boot, host waits or private implementation model.
"""

from pathlib import Path
import re
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
    UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_SP,
    UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
    UC_X86_REG_IP, UC_X86_REG_EFLAGS,
)

ROOT = Path(__file__).resolve().parents[1]
KSEG, MSEG, SSEG = 0x1000, 0x3000, 0x6000
BASE = MSEG << 4
LAST, SPAN, FSEG, FBASE, KLFP = 0x100, 0x101, 0x102, 0x104, 0x108
GENERAL = (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
           UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP)


def u16(data):
    return int.from_bytes(data, "little")


def source():
    provider = (ROOT / "kernel/keyfont.inc").read_text()
    provider = provider.replace("section .modqb", "section .bss nobits align=1")
    provider = provider.replace("section .modq", "section .text")
    kernel = (ROOT / "kernel/kernel.asm").read_text()
    api = kernel.split("osapi_font_glyphs:\n", 1)[1].split(
        "; ---- osapi_file_dfree", 1)[0]
    font = (ROOT / "kernel/font.inc").read_text()
    indexes = []
    for label, end, out in (
        ("font_char:", "xchg ax, si", "si"),
        ("font_run_x:", "add ax, dx", "ax"),
        ("font_run_cell:", "xchg ax, si", "si"),
        ("font_str_run:", "add ax, di", "ax"),
    ):
        part = font.split(label, 1)[1]
        part = part.split("sub al, FONT_FIRST", 1)[1]
        part = part.split(end, 1)[0]
        # Range branches precede the arithmetic in the run walkers. Their
        # bounds have already been established by the requested byte here.
        lines = [l for l in ("sub al, FONT_FIRST" + part).splitlines()
                 if not re.match(r"\s*(?:cmp|ja)\b", l)]
        indexes.append(("\n".join(lines) + "\n    " + end + "\n    ret\n", out))
    pre = """cpu 8086
bits 16
FONT_FIRST equ 32
FONT_LAST equ 126
FONT_BYTES equ 760
font_last equ 0x100
font_span equ 0x101
font_seg equ 0x102
font_base equ 0x104
KLFP equ 0x108
KL_CMD equ 4
gfx_lock_flag equ 0x112
gfx_lock_own equ 0x113
sch_cur equ 0x114
KERNEL_SEG equ 0x1000
cw_gfx_lock equ 0x20
cw_gfx_unlock equ 0x21
section .text vstart=0
"""
    text = pre + provider + "\nsection .text\nosapi_font_glyphs:\n" + api
    text += "\napi_end:\n"
    for i, (body, _out) in enumerate(indexes):
        text += f"ix_{i}:\n" + body
    return text, indexes


def assemble(directory):
    asm, indexes = source()
    src, binary, symbols = (directory / x for x in ("font.asm", "font.bin", "font.map"))
    src.write_text(asm + f"\n[map symbols {symbols}]\n")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", str(ROOT / "kernel") + "/",
                    "-o", str(binary), str(src)], check=True)
    names = {}
    for line in symbols.read_text().splitlines():
        match = re.match(r"\s*([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$", line)
        if match:
            names[match[3]] = int(match[2], 16)
    return binary.read_bytes(), names, indexes


def machine(code, names, original_seg=0x4000, original_base=0x200):
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 2 * 1024 * 1024)
    mu.mem_write(BASE, code)
    original = bytes((i * 29 + 7) & 255 for i in range(760))
    mu.mem_write((original_seg << 4) + original_base, original)
    mu.mem_write((KSEG << 4) + LAST, bytes([126, 94]))
    mu.mem_write((KSEG << 4) + FSEG, original_seg.to_bytes(2, "little"))
    mu.mem_write((KSEG << 4) + FBASE, original_base.to_bytes(2, "little"))
    mu.mem_write((KSEG << 4) + KLFP + 4, b"\x00\x00" + MSEG.to_bytes(2, "little"))
    # The helpers use the existing kernel mutex; these far-return gates
    # instrument calls to it without importing a second kernel into this
    # instruction test. Their state mirrors only ownership, not scheduling.
    mu.mem_write((KSEG << 4) + 0x20, b"\xcb\xcb")
    mu.mem_write((KSEG << 4) + 0x112, b"\x00\xff\x00")
    mu.font_lock_calls = []

    def mutex_gate(guest, address, _size, _data):
        if address == (KSEG << 4) + 0x20:
            guest.font_lock_calls.append("lock")
            guest.mem_write((KSEG << 4) + 0x112, b"\x01\x00")
        elif address == (KSEG << 4) + 0x21:
            guest.font_lock_calls.append("unlock")
            guest.mem_write((KSEG << 4) + 0x112, b"\x00\xff")

    mu.hook_add(UC_HOOK_CODE, mutex_gate)
    return mu, original


def invoke(mu, offset, inputs=None):
    for i, reg in enumerate(GENERAL):
        mu.reg_write(reg, 0x1111 * (i + 1))
    for reg, value in ((UC_X86_REG_CS, MSEG), (UC_X86_REG_DS, KSEG),
                       (UC_X86_REG_ES, MSEG), (UC_X86_REG_SS, SSEG),
                       (UC_X86_REG_SP, 0x8FFE), (UC_X86_REG_EFLAGS, 0x202)):
        mu.reg_write(reg, value)
    if inputs:
        for reg, value in inputs.items():
            mu.reg_write(reg, value)
    mu.mem_write((SSEG << 4) + 0x8FFE, b"\xff\xff")
    before = {r: mu.reg_read(r) for r in GENERAL}
    mu.emu_start(BASE + offset, BASE + 0xFFFF, count=100000)
    assert mu.reg_read(UC_X86_REG_IP) == 0xFFFF, "guest helper did not return"
    assert mu.reg_read(UC_X86_REG_SP) == 0x9000, "stack not balanced"
    return before


def check_provider(code, names):
    for original_seg, original_base in ((0xF000, 0x200), (0x4000, 0), (0x2000, 0x700)):
        mu, original = machine(code, names, original_seg, original_base)
        before = invoke(mu, names["klf_enable"])
        assert all(mu.reg_read(r) == v for r, v in before.items()), "enable clobbered a register"
        assert not mu.reg_read(UC_X86_REG_EFLAGS) & 1
        assert bytes(mu.mem_read(BASE + names["klf_table"], 760)) == original
        assert bytes(mu.mem_read(BASE + names["klf_table"] + 760, 8)) == bytes(8)
        assert bytes(mu.mem_read((KSEG << 4) + LAST, 2)) == b"\xff\xdf"
        assert u16(mu.mem_read((KSEG << 4) + FSEG, 2)) == MSEG
        assert u16(mu.mem_read((KSEG << 4) + FBASE, 2)) == names["klf_table"]
        assert mu.font_lock_calls == ["lock", "unlock"]
        # Independently pinned authored shapes at their CP850 byte positions.
        for value, rows in {
            0x82: (0x08, 0x10, 0x38, 0x44, 0x7C, 0x40, 0x38, 0),  # e acute
            0xA4: (0x18, 0x64, 0x78, 0x44, 0x44, 0x44, 0x44, 0),  # n tilde
            0xFF: (0, 0, 0, 0, 0, 0, 0, 0),                        # nbsp
        }.items():
            address = BASE + names["klf_table"] + (value - 32) * 8
            assert bytes(mu.mem_read(address, 8)) == bytes(rows), hex(value)
        invoke(mu, names["klf_enable"])  # idempotent, do not save our own table
        before = invoke(mu, names["klf_disable"])
        assert all(mu.reg_read(r) == v for r, v in before.items()), "disable clobbered a register"
        assert bytes(mu.mem_read((KSEG << 4) + LAST, 2)) == b"\x7e\x5e"
        assert u16(mu.mem_read((KSEG << 4) + FSEG, 2)) == original_seg
        assert u16(mu.mem_read((KSEG << 4) + FBASE, 2)) == original_base
    mu, _original = machine(code, names)
    mu.mem_write((KSEG << 4) + 0x112, b"\x01\x00")  # this UI callback owns it
    invoke(mu, names["klf_enable"])
    invoke(mu, names["klf_disable"])
    assert mu.font_lock_calls == [], "provider re-entered the drawing mutex"
    assert bytes(mu.mem_read((KSEG << 4) + 0x112, 2)) == b"\x01\x00"


def check_copy_snapshot(code, names):
    mu, _original = machine(code, names)
    invoke(mu, names["klf_enable"])
    seen = []
    start, end = names["osapi_font_glyphs"], names["api_end"]

    def snapshot_guard(guest, address, size, _data):
        relative = address - BASE
        # pushf and cli themselves precede the guarded snapshot. Every
        # instruction from its first pointer read until POPF must have IF=0.
        if start + 2 <= relative < end:
            opcode = bytes(guest.mem_read(address, 1))
            if opcode == b"\x9d":
                seen.append(relative)
            elif not seen:
                assert not guest.reg_read(UC_X86_REG_EFLAGS) & 0x200, "preemptible font snapshot"

    hook = mu.hook_add(UC_HOOK_CODE, snapshot_guard)
    invoke(mu, start)
    mu.hook_del(hook)
    assert seen, "snapshot guard did not observe POPF"
    assert mu.reg_read(UC_X86_REG_AX) == 0x7E20
    assert mu.reg_read(UC_X86_REG_CX) == 8
    assert mu.reg_read(UC_X86_REG_DX) == 0x4000
    assert mu.reg_read(UC_X86_REG_SI) == 0x200
    assert mu.reg_read(UC_X86_REG_ES) == MSEG
    # The original pointer remains valid after provider disable/drop.
    invoke(mu, names["klf_disable"])
    invoke(mu, start)
    assert mu.reg_read(UC_X86_REG_AX) == 0x7E20
    assert mu.reg_read(UC_X86_REG_DX) == 0x4000
    assert mu.reg_read(UC_X86_REG_SI) == 0x200


def check_indexes(code, names, indexes):
    for i, (_body, out) in enumerate(indexes):
        for value in (32, 126, 128, 159, 160, 181, 255):
            mu, _original = machine(code, names)
            mu.mem_write((KSEG << 4) + FBASE, (0x700).to_bytes(2, "little"))
            invoke(mu, names[f"ix_{i}"], {UC_X86_REG_AX: 0xFF00 | value,
                                          UC_X86_REG_DX: 0x700, UC_X86_REG_DI: 0x700})
            reg = UC_X86_REG_SI if out == "si" else UC_X86_REG_AX
            assert mu.reg_read(reg) == 0x700 + (value - 32) * 8, (i, value)


def main():
    with tempfile.TemporaryDirectory(prefix="os88-kbd-font-") as directory:
        code, names, indexes = assemble(Path(directory))
        check_provider(code, names)
        check_copy_snapshot(code, names)
        check_indexes(code, names, indexes)
        # A missing CLI must actually make the snapshot guard go red.
        broken = bytearray(code)
        at = names["osapi_font_glyphs"]
        assert broken[at:at+2] == b"\x9c\xfa"
        broken[at+1] = 0x90
        try:
            check_copy_snapshot(bytes(broken), names)
        except AssertionError as error:
            assert "preemptible" in str(error), error
        else:
            raise AssertionError("negative control: missing CLI was not detected")
    print("kbdlayoutfont: provider, stable ASCII snapshot, unsigned glyph indexes and negative control passed")


if __name__ == "__main__":
    main()

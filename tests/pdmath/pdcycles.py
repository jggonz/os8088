#!/usr/bin/env python3
"""
os8088 - tests/pdmath/pdcycles.py      8088 cost ESTIMATE of pdmath.inc's hot paths

An ESTIMATE, not a measurement: the routine is run in Unicorn and every executed
instruction is costed as  max(4.34 x its bytes, its clocks)  (the 8088's 4-clock
bus fetch floor, PERFORMANCE.md Part 2), with MUL/DIV at the 8088 manual's
figures (MUL r16 ~ 125, DIV r16 ~ 155 clocks, +~12 for a memory operand), a
memory operand otherwise ~ +9 clocks of effective address, and pushes/pops/
calls/rets at their manual figures.  1 clock = 0.2096 us at 4.77 MHz.  It prints
the instructions and clocks per call so a change that makes a routine slower
shows up as a number moving.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pdmath_diff as D
from unicorn import UC_HOOK_CODE

CLK = 0.2096

def cost(b, size):
    base = 4.34 * size
    op = b[0]
    i = 0
    while op in (0x26, 0x2E, 0x36, 0x3E, 0xF2, 0xF3) and i + 1 < len(b):   # prefixes
        i += 1; op = b[i]
    modrm = b[i + 1] if i + 1 < len(b) else 0
    mem = (modrm >> 6) != 3
    ea = 9 if mem else 0
    if op in (0xF6, 0xF7):
        reg = (modrm >> 3) & 7
        if reg == 4: return max(base, 125 + (12 if mem else 0))        # mul
        if reg == 6: return max(base, 155 + (12 if mem else 0))        # div
        if reg == 3 or reg == 2: return max(base, 3 + ea + (16 if mem else 0))
    if op in (0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57): return max(base, 15)
    if op in (0x58, 0x59, 0x5A, 0x5B, 0x5D, 0x5E, 0x5F): return max(base, 12)
    if op == 0xE8: return max(base, 23)
    if op in (0xC3, 0xC2): return max(base, 20)
    if op in (0xE2, ): return max(base, 17)
    if 0x70 <= op <= 0x7F or op in (0xEB, 0xE9, 0xE3): return max(base, 16)
    if op in (0xD0, 0xD1): return max(base, 2 + ea + (15 if mem else 0))   # shift by 1
    if op in (0xD2, 0xD3): return max(base, 8 + 4 * 4)                     # shift by CL
    if op in (0xA4, 0xA5): return max(base, 18)
    return max(base, 3 + ea + (6 if mem and op in (0x88, 0x89, 0x8A, 0x8B, 0x01, 0x03, 0x29, 0x2B, 0x39, 0x3B) else 0))

def run(H, name, args, setup=None):
    emu = H.emu; uc = emu.uc
    if setup: setup(emu)
    tot = [0.0, 0, 0xFFF0]
    from unicorn.x86_const import UC_X86_REG_SP
    def hook(u, addr, size, ud):
        b = bytes(u.mem_read(addr, min(size, 4)))
        tot[0] += cost(b, size); tot[1] += 1
        sp = u.reg_read(UC_X86_REG_SP)
        if sp < tot[2]: tot[2] = sp
    h = uc.hook_add(UC_HOOK_CODE, hook)
    emu.call(name, args)
    uc.hook_del(h)
    return tot[0], tot[1], tot[2]

def main():
    asm, so = D.build()
    H = D.Harness(asm, so)
    emu = H.emu
    def pk(*vals):
        return b"".join(D.pk32(v) for v in vals)
    base = D.DSEG << 4
    cases = []
    A3 = [D.SCR, D.SCR + 4, D.SCR + 8]
    def case(label, name, args, *puts):
        cases.append((label, name, args, puts))
    case("fx_mul (1.5 * 2.5)", "pdm_fx_mul", A3, (4, pk(0x00018000, 0x00028000)))
    case("fx_div, divisor < 1.0 (3 DIVs)", "pdm_fx_div", A3, (4, pk(0x00018000, 0x00000003)))
    case("fx_div, divisor >= 1.0 (48 steps)", "pdm_fx_div", A3, (4, pk(0x00018000, 0x00028000)))
    case("fx_add", "pdm_fx_add", A3, (4, pk(0x00018000, 0x00028000)))
    mats = (24, pk(0x10000, 0, 0, 0x10000, 0x40000, 0x50000) + pk(0x20000, 0x8000, 0x8000, 0x20000, 0x10000, 0x10000))
    case("mat_mul", "pdm_mat_mul", [D.SCR, D.SCR + 24, D.SCR + 48], mats)
    case("mat_apply", "pdm_mat_apply", [D.SCR, D.SCR + 4, D.SCR + 24], mats, (0, pk(0x30000, 0x40000)))
    def view(rot, zoom, an, ad):
        return b"".join(D.pk16(t) for t in (0, 0, 9792, 12672, rot, zoom, an, ad, 10, 20, 0, 0, 0, 0))
    for label, rot, zoom, an, ad in (("view_fwd, aspect 1:1", 0, 100, 1, 1),
                                     ("view_fwd, aspect 5:6, rot 90", 90, 150, 5, 6),
                                     ("view_fwd, aspect 480:640", 0, 100, 480, 640)):
        case(label, "pdm_view_fwd", [D.SCR, D.SCR + 2, D.SCR + 16, 5000, 7000], (16, view(rot, zoom, an, ad)))
    case("view_inv, aspect 1:1", "pdm_view_inv", [D.SCR, D.SCR + 2, D.SCR + 16, 300, 200], (16, view(0, 100, 1, 1)))
    case("view_inv, aspect 480:640", "pdm_view_inv", [D.SCR, D.SCR + 2, D.SCR + 16, 300, 200], (16, view(0, 100, 480, 640)))
    case("view_size", "pdm_view_size", [D.SCR, D.SCR + 2, D.SCR + 16], (16, view(0, 100, 1, 1)))
    case('fx_parse("-123.456")', "pdm_fx_parse", [D.SCR + 32, D.SCR, 8], (0, b"-123.456"))
    case("fx_todec(1.5)", "pdm_fx_todec", [D.SCR, D.SCR + 64], (64, pk(0x00018000)))
    case("crc16, 64 bytes", "pdm_crc16", [0xFFFF, D.SCR + 64, 64], (64, bytes(range(64))))
    print("%-42s %8s %9s %9s %7s" % ("routine (incl. the call's own return)", "instr", "clocks", "us @4.77", "stack B"))
    for label, name, args, puts in cases:
        clk, n, sp = run(H, name, args, lambda e, puts=puts: [e.mem_w(base + D.SCR + o, d) for o, d in puts])
        # bytes below the first argument, return address included
        print("%-42s %8d %9.0f %9.0f %7d" % (label, n, clk, clk * CLK, 0xFFF0 - 2 * len(args) - sp))

if __name__ == "__main__":
    main()

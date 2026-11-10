#!/usr/bin/env python3
"""
os8088 - tests/pdcrypt/pdk_stack.py       how deep PDXK's stack really goes (PAGEDESK-SPEC 20.22.5)

The deep stack budget is 1,776 bytes from a module door's entry (PAGEDESK-SPEC 20.15). os88pdstk.py bounds it
statically; this is the measured side: the shipping PDXK.MOD runs in Unicorn (pdc_target.Machine) with every
instruction's SP watched, through the nested PDENG DICT_GET calls that read /Encrypt and three KEY_AUTH slices
(SHA-512 included), for an RC4, an AES-128 and an R6 file. Prints the deepest SP below the door's entry.
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pdc_target as T
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_SP

def trace(fx, slices):
    m = T.Machine(open(os.path.join(T.WORK, fx), "rb").read())
    lo = [0xFFFF]
    def hook(u, addr, size, ud):
        sp = u.reg_read(UC_X86_REG_SP)
        if sp < lo[0]: lo[0] = sp
    m.uc.hook_add(UC_HOOK_CODE, hook)
    m.wr(0, T.init_struct().ljust(1024, b"\0")); m.door("PDENG", 0, T.XBSEG)
    m.wr(0, T.init_struct().ljust(1024, b"\0")); m.door("PDXK", 0xC0, T.XBSEG)
    op = bytearray(32); op[0:7] = b"DOC.PDF"
    while True:
        m.wr(0, bytes(op)); ax, cf, _ = m.door("PDENG", 1); op[:] = m.rd(0, 32)
        if not cf and ax == T.PDE_BUSY: continue
        break
    cr = bytearray(402)
    for i in range(slices):
        m.wr(0, bytes(cr)); ax, cf, _ = m.door("PDXK", 0xC1); cr[:] = m.rd(0, 402)
        if cf or ax != T.PDE_BUSY: break
    return 0xEFFC - lo[0]

if __name__ == "__main__":
    r = [trace("RC4_40.PDF", 1), trace("AES_128.PDF", 1), trace("AES_256_R6.PDF", 3)]
    print("PDXK deepest below the door's entry: RC4-40 %d, AES-128 %d, AES-256 R6 (params, 3 slices, SHA-512) %d bytes (budget 1,776)" % tuple(r))
    sys.exit(0 if max(r) < 1776 else 1)

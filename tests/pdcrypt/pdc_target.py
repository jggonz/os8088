#!/usr/bin/env python3
"""
os8088 - tests/pdcrypt/pdc_target.py       the SHIPPING PDENG.MOD and PDXK.MOD, run (PAGEDESK-SPEC 20.22.5)

pdc_e2e.py holds the C against qpdf on the host; this file holds the COMPILED 8086 modules to the host:
build/PDENG.MOD and build/PDXK.MOD are loaded into Unicorn's real-mode x86 at their own segments, the
kernel stub of hosttest/pde_kstub.asm (READ_SEQ with its contract enforced, FILE_FIND, DFREE: real 8086
code at KERNEL_SEG) serves the PDF, SS != DS everywhere, and the sequence the primary will use runs
through the module doors: INIT, OPEN -> PDE_PASSWORD, PDXK's KEY_AUTH (calling PDENG's door itself
for CRYPTINFO), SETKEY, OPEN, then every stream's CRC and the Info strings. The lines must equal the
host driver's (`pdc_e2e dump`) for the same file.

    python3 tests/pdcrypt/pdc_target.py [FIXTURE ...]        (default: one of each algorithm)
"""
import os, struct, subprocess, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_MEM_WRITE
from unicorn.x86_const import *

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, "build")
WORK = os.path.join(BUILD, "pdcrypt", "e2e")
E2E = os.path.join(BUILD, "pdc_e2e")
KSEG, ESEG, KXSEG, XBSEG, SSEG, HALTSEG = 0x0060, 0x1000, 0x2000, 0x2800, 0x2900, 0x0F00
CACHE, ARENA, TABLES, FLATE, PDFSEG = 0x3000, 0x3400, 0x3800, 0x4000, 0x6000
MEM = 0x100000
PDE_BUSY, PDE_PASSWORD = 65, 23

def lin(seg, off=0): return (seg << 4) + off

class Machine:
    def __init__(self, pdf):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, MEM)
        ks = os.path.join(BUILD, "pdcrypt", "pde_kstub.bin")
        os.makedirs(os.path.dirname(ks), exist_ok=True)
        subprocess.check_call(["nasm", "-f", "bin", "-o", ks, os.path.join(ROOT, "apps/pagedesk/hosttest/pde_kstub.asm")])
        self.uc.mem_write(lin(KSEG), open(ks, "rb").read())
        self.uc.mem_write(lin(KSEG, 0x500), struct.pack("<HHHHH", len(pdf) & 0xFFFF, len(pdf) >> 16, 1, 0, PDFSEG))
        self.uc.mem_write(lin(PDFSEG), pdf)
        self.uc.mem_write(lin(HALTSEG), b"\xF4")
        self.mods = {}
        for name, seg in (("PDENG", ESEG), ("PDXK", KXSEG)):
            d = open(os.path.join(BUILD, name + ".MOD"), "rb").read()
            m, abi, size, bss = struct.unpack_from("<HHHH", d)
            assert size == len(d), (name, size, len(d))
            self.uc.mem_write(lin(seg), d)
            self.mods[name] = (seg, size + bss)
        self.insns = 0
        # Nothing a module does may write the interrupt vectors, the BIOS data area or the kernel stub: on the
        # machine the cipher area is the ARENA (20.22.5), and an unset _pdc_cseg (ES = 0) put Td0 over the vectors
        self.low = []
        def low_write(uc, access, addr, size, value, data):
            if len(self.low) < 4:
                self.low.append("%04x:%04x wrote %d at %05x" % (uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP), size, addr))
        self.uc.hook_add(UC_HOOK_MEM_WRITE, low_write, begin=0, end=lin(KSEG) + 0x4FF)

    def rd(self, off, n, seg=XBSEG): return bytes(self.uc.mem_read(lin(seg, off), n))
    def wr(self, off, data, seg=XBSEG): self.uc.mem_write(lin(seg, off), bytes(data))

    def door(self, mod, verb, bx=0, cx=0, dx=0):
        seg = self.mods[mod][0]
        uc = self.uc
        for r, v in ((UC_X86_REG_DS, 0x1234), (UC_X86_REG_ES, 0x4321), (UC_X86_REG_SS, SSEG), (UC_X86_REG_BP, 0xBEEF),
                     (UC_X86_REG_SI, 0x5151), (UC_X86_REG_DI, 0xD1D1), (UC_X86_REG_AX, verb), (UC_X86_REG_BX, bx),
                     (UC_X86_REG_CX, cx), (UC_X86_REG_DX, dx), (UC_X86_REG_EFLAGS, 0x2), (UC_X86_REG_CS, seg)):
            uc.reg_write(r, v)
        sp = 0xF000
        uc.mem_write(lin(SSEG, sp - 4), struct.pack("<HH", 0, HALTSEG))    # the far return: HALTSEG:0
        uc.reg_write(UC_X86_REG_SP, sp - 4)
        uc.emu_start(lin(seg, 8), lin(HALTSEG), count=2_000_000_000)
        rd = uc.reg_read
        assert not self.low, "%s verb %02x wrote low memory: %s" % (mod, verb, self.low)
        assert rd(UC_X86_REG_SP) == sp and rd(UC_X86_REG_SS) == SSEG, "stack not restored"
        assert rd(UC_X86_REG_DS) == 0x1234 and rd(UC_X86_REG_ES) == 0x4321, "DS/ES not restored"
        assert rd(UC_X86_REG_BP) == 0xBEEF and rd(UC_X86_REG_SI) == 0x5151 and rd(UC_X86_REG_DI) == 0xD1D1, "BP/SI/DI not kept"
        assert not rd(UC_X86_REG_EFLAGS) & 0x400, "DF set"
        return rd(UC_X86_REG_AX), rd(UC_X86_REG_EFLAGS) & 1, rd(UC_X86_REG_DX)

def init_struct(cpu=0):
    w = [0] * 37
    w[0] = XBSEG
    w[1], w[2] = 8, ESEG                              # eng_off, eng_seg
    w[3], w[4] = CACHE, 16
    w[5], w[6] = ARENA, 11                              # 8 KiB of visited bits + the cipher area (20.22.5.1)
    w[7], w[8] = TABLES, 11
    w[21], w[22], w[23] = 1, FLATE, 86                # flags PDXI_FLATE, flate_seg, flate_kb
    w[34] = cpu
    return struct.pack("<37H", *w)

def run(fx, pw):
    path = os.path.join(WORK, fx)
    pdf = open(path, "rb").read()
    m = Machine(pdf)
    out = []
    m.wr(0, init_struct().ljust(1024, b"\0"))
    assert m.door("PDENG", 0x00, XBSEG)[1] == 0, "ENG INIT"
    m.wr(0, init_struct().ljust(1024, b"\0"))
    assert m.door("PDXK", 0xC0, XBSEG)[1] == 0, "XK INIT"
    op = bytearray(32); op[0:7] = b"DOC.PDF"
    def open_loop():
        while True:
            m.wr(0, bytes(op))
            ax, cf, _ = m.door("PDENG", 0x01)
            op[:] = m.rd(0, 32)
            if not cf and ax == PDE_BUSY: continue
            return ax, cf
    ax, cf = open_loop()
    slices = 0
    if cf and ax == PDE_PASSWORD:
        cr = bytearray(402)
        pwb = pw.encode()
        struct.pack_into("<H", cr, 232, len(pwb)); cr[234:234 + len(pwb)] = pwb
        while True:
            m.wr(0, bytes(cr))
            ax, cf, _ = m.door("PDXK", 0xC1)
            cr[:] = m.rd(0, 402)
            slices += 1
            if not cf and ax == PDE_BUSY: continue
            break
        if cf: return ["AUTH rc=%d" % ax], m
        keylen = struct.unpack_from("<H", cr, 394)[0]
        out.append("AUTH ok auth=%d keylen=%d key=%s slices=%d" % (struct.unpack_from("<H", cr, 396)[0], keylen, bytes(cr[362:362 + keylen]).hex(), slices))
        m.wr(0, bytes(cr))
        ax, cf, _ = m.door("PDENG", 0x14)
        assert not cf, "SETKEY %d" % ax
        ax, cf = open_loop()
    assert not cf, "OPEN rc=%d" % ax
    # trailer, then every stream by object number, and the Info strings
    ax, cf, _ = m.door("PDENG", 0x0C)
    tr = m.rd(0, 56)
    root, info = struct.unpack_from("<H", tr, 2)[0], struct.unpack_from("<H", tr, 6)[0]
    size = struct.unpack_from("<H", tr, 0)[0]
    out.append("TRAILER root=%d info=%d" % (root, info))
    for o in range(1, min(size, 400)):
        sx = bytearray(18); struct.pack_into("<H", sx, 0, o)
        m.wr(0, bytes(sx))
        ax, cf, _ = m.door("PDENG", 0x06)                 # STRM_OPEN
        if cf:
            if os.environ.get('PDC_V'): print('   STRM_OPEN', o, 'rc', ax)
            continue
        total, crc = 0, 0xFFFF
        flt = struct.unpack_from("<H", m.rd(0, 18), 12)[0]
        while True:
            ax, cf, _ = m.door("PDENG", 0x07, 0x5800, 0, 512)      # STRM_READ into segment 0x5800 (past the Flate claim)
            assert not cf, ("STRM_READ", o, ax)
            if not ax: break
            data = m.rd(0, ax, 0x5800)
            total += ax
            for b in data:
                crc ^= b << 8
                for _ in range(8): crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
        m.door("PDENG", 0x08)
        out.append("STREAM %d bytes=%d crc=%04x flt=%d" % (o, total, crc, flt & ~0x20))
    # RAW_COPY PDRC_PLAIN of every object in 37-byte pieces (the same numbers as the host's `plain` command)
    for o in range(1, min(size, 400)):
        rw = bytearray(18); struct.pack_into("<HH", rw, 0, o, 0); struct.pack_into("<HHHH", rw, 8, 0x5800, 0, 37, 0); struct.pack_into("<H", rw, 16, 1)
        total, crc, flag, bad = 0, 0xFFFF, 0, 0
        while True:
            m.wr(0, bytes(rw)); ax, cf, _ = m.door("PDENG", 0x0B)
            if cf: bad = ax; break
            rw[:] = m.rd(0, 18)
            got = struct.unpack_from("<H", rw, 14)[0]; flag = (struct.unpack_from("<H", rw, 16)[0] & 0x100) != 0
            if not got: break
            for b in m.rd(0, got, 0x5800):
                crc ^= b << 8
                for _ in range(8): crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
            total += got
        if not bad: out.append("PLAIN %d bytes=%d crc=%04x flag=%d" % (o, total, crc, flag))
    return out, m

def host_lines(fx, pw):
    t = subprocess.run([E2E, "dump", os.path.join(WORK, fx), pw or "-"], capture_output=True, text=True).stdout.splitlines()
    scratch = os.path.join(BUILD, "pdcrypt", "plain_tmp"); os.makedirs(scratch, exist_ok=True)
    pl = subprocess.run([E2E, "plain", os.path.join(WORK, fx), pw or "-", scratch, "37"], capture_output=True, text=True).stdout.splitlines()
    return [l for l in t if l.startswith(("AUTH", "TRAILER", "STREAM"))] + [l for l in pl if l.startswith("PLAIN") and "bytes=" in l]

def main():
    names = sys.argv[1:] or ["RC4_40.PDF", "RC4_128.PDF", "RC4_V4.PDF", "AES_128.PDF", "AES_128_NM.PDF", "AES_256_R5.PDF",
                              "AES_128_PW.PDF", "GUIDE_RC4.PDF", "AES_256_R6.PDF"]
    fails = 0
    for fx in names:
        pw = "secret" if fx.endswith("_PW.PDF") else ""
        got, m = run(fx, pw)
        want = host_lines(fx, pw)
        def cut(l):                       # the AUTH lines carry different extras: compare auth, key length and the key
            if not l.startswith("AUTH ok"): return l
            f = dict(p.split("=") for p in l.split()[2:] if "=" in p)
            return "AUTH auth=%s keylen=%s key=%s" % (f["auth"], f["keylen"], f["key"])
        ga = [cut(l) for l in got]; wa = [cut(l) for l in want]
        if ga != wa:
            fails += 1
            print("FAIL %s: target and host differ: %s" % (fx, [(a, b) for a, b in zip(ga, wa) if a != b][:3] or (len(ga), len(wa))))
            if os.environ.get("PDC_V"): print("  target:", ga, "\n  host:  ", wa)
        else:
            print("ok   %-16s target == host (%d lines)" % (fx, len(ga)))
    print("pdc_target: %d failures" % fails)
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())

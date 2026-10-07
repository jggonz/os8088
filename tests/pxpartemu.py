#!/usr/bin/env python3
"""PxPart: one of PiXEL's LINKED parts run on the host, in an 8086 emulator.

    from pxpartemu import PxPart
    p = PxPart("build/pxedit.bin")
    p.pal = [...]; p.master(m, w, h); ax, cf = p.decode(cl)

The EDIT and WRITE parts (SPEC.md 106.24) are arithmetic - a palette in, a
master in, a master or a file's bytes out - and the guest's answer must be
tools/pixelsim.py's to the byte. Running each operation through MartyPC is
minutes a case; through Unicorn's x86 core it is milliseconds, so the host
leg of tests/pxedit.py and tests/pxsave.py runs every operation on a grid
of pictures, sizes and palettes here first, and the emulator leg then proves
the same part does the same thing inside the package on the machine.

THE PACKAGE IS FAKED, NOT LOADED: a segment holding the header's stamp (the
image and bss sizes a linked part checks before it touches a variable,
SPEC.md 106.20) and the bss variables at the addresses build/pxlink.inc
names. Every OSAPI cell is a `retf` - but OSAPI_GET_TICKS answers a clock
that moves, OSAPI_TASK_SLEEP and OSAPI_TASK_ALIVE call back into Python (the
"UI task" of a test, which may drain a writer's slots), and OSAPI_WM_WAKE is
counted. Unicorn is not a declared dependency: a host without it skips the
host leg and says so (`available()`).
"""
import os
import re
import struct

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX,
                                   UC_X86_REG_CX, UC_X86_REG_DX,
                                   UC_X86_REG_SI, UC_X86_REG_DI,
                                   UC_X86_REG_BP, UC_X86_REG_SP,
                                   UC_X86_REG_CS, UC_X86_REG_DS,
                                   UC_X86_REG_ES, UC_X86_REG_SS,
                                   UC_X86_REG_IP, UC_X86_REG_FLAGS)
    HAVE = True
except ImportError:                     # pragma: no cover
    HAVE = False

KSEG = 0x0060                           # apps/os88api.inc's KERNEL_SEG
PKG = 0x1000                            # the package's segment
PART = 0x2000                           # the part's
STK = 0x3000                            # a stack
HEAP = 0x4000                           # claims from here up (past the stack)
STUB = 0x0050                           # where the far call is made from


def available():
    return HAVE


def link_syms(path=None):
    path = path or os.path.join(ROOT, "build", "pxlink.inc")
    syms = {}
    for line in open(path):
        m = re.match(r"\s*(\w+)\s+equ\s+(0x[0-9A-Fa-f]+|\d+)", line)
        if m:
            syms[m.group(1)] = int(m.group(2), 0)
    return syms


def api_cells():
    cells = {}
    for line in open(os.path.join(ROOT, "apps", "os88api.inc")):
        m = re.match(r"%define\s+(OSAPI_\w+)\s+KERNEL_SEG:(0x[0-9A-Fa-f]+)", line)
        if m:
            cells[m.group(1)] = int(m.group(2), 16)
    return cells


class PxPart:
    def __init__(self, part, link=None):
        self.syms = link_syms(link)
        self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        self.mu.mem_map(0, 1 << 20)
        self.code = open(part, "rb").read()
        self.mu.mem_write(PART * 16, self.code)
        hdr = struct.pack("<HH", self.syms["PXL_IMAGE"], self.syms["PXL_BSS"])
        self.mu.mem_write(PKG * 16 + 8, hdr)
        self.cells = api_cells()
        for off in self.cells.values():
            self.mu.mem_write(KSEG * 16 + off, b"\xCB")        # retf
        self.ticks = 0
        self.wakes = 0
        self.alive = 0
        self.on_alive = None            # the "UI task": called at ALIVE/SLEEP
        self.heap = HEAP
        self.mu.hook_add(UC_HOOK_CODE, self._cell,
                         begin=KSEG * 16, end=KSEG * 16 + 0xFFFF)
        self.steps = 0

    # --- the kernel, as far as a part can tell ----------------------------
    def _cell(self, mu, addr, size, _):
        off = addr - KSEG * 16
        if off == self.cells["OSAPI_GET_TICKS"]:
            self.ticks = (self.ticks + 1) & 0xFFFF
            mu.reg_write(UC_X86_REG_AX, self.ticks)
        elif off == self.cells["OSAPI_WM_WAKE"]:
            self.wakes += 1
        elif off in (self.cells["OSAPI_TASK_ALIVE"],
                     self.cells["OSAPI_TASK_SLEEP"]):
            self.alive += 1
            self.ticks = (self.ticks + 1) & 0xFFFF
            if self.on_alive:
                self.on_alive(self)

    # --- memory -------------------------------------------------------------
    def claim(self, nbytes):
        seg = self.heap
        self.heap += (nbytes + 15) // 16 + 1
        assert self.heap < 0xA000, "the fake heap is full"
        self.mu.mem_write(seg * 16, b"\0" * nbytes)
        return seg

    def w8(self, name, v, k=0):
        self.mu.mem_write(PKG * 16 + self.syms[name] + k, bytes((v & 255,)))

    def w16(self, name, v, k=0):
        self.mu.mem_write(PKG * 16 + self.syms[name] + k,
                          struct.pack("<H", v & 0xFFFF))

    def r8(self, name, k=0):
        return self.mu.mem_read(PKG * 16 + self.syms[name] + k, 1)[0]

    def r16(self, name, k=0):
        return struct.unpack("<H", self.mu.mem_read(
            PKG * 16 + self.syms[name] + k, 2))[0]

    def wbytes(self, name, data, k=0):
        self.mu.mem_write(PKG * 16 + self.syms[name] + k, bytes(data))

    def rbytes(self, name, n, k=0):
        return bytes(self.mu.mem_read(PKG * 16 + self.syms[name] + k, n))

    def seg_write(self, seg, data, off=0):
        self.mu.mem_write(seg * 16 + off, bytes(data))

    def seg_read(self, seg, n, off=0):
        return bytes(self.mu.mem_read(seg * 16 + off, n))

    # --- the picture ----------------------------------------------------------
    PXR_MW, PXR_MH, PXR_SCL, PXR_PMODE, PXR_MSEG = 32, 34, 36, 37, 38

    def set_pal(self, pal):
        flat = b"".join(bytes(c) for c in pal) + b"\0" * (768 - 3 * len(pal))
        self.wbytes("px_pal", flat)

    def pal(self):
        b = self.rbytes("px_pal", 768)
        return [tuple(b[3 * i:3 * i + 3]) for i in range(256)]

    def set_master(self, m, w, h, counts=None, tail=2048):
        seg = self.claim(w * h + 15 + tail)
        self.seg_write(seg, m)
        self.w16("px_cur", w, self.PXR_MW)
        self.w16("px_cur", h, self.PXR_MH)
        self.w16("px_cur", seg, self.PXR_MSEG)
        if counts is not None:
            self.seg_write(self.tail_of(seg, w, h),
                           b"".join(struct.pack("<I", c) for c in counts))
        return seg

    @staticmethod
    def tail_of(seg, w, h):
        return seg + (w * h + 15) // 16

    # --- a call ---------------------------------------------------------------
    def vector(self, k):
        return struct.unpack_from("<H", self.code, 6 + 2 * k)[0]

    def call(self, vec, ax=0, bx=0, cx=0, dx=0, si=0, di=0, es=None,
             limit=400_000_000):
        mu = self.mu
        # the stub: call far PART:vec ; hlt
        stub = b"\x9A" + struct.pack("<HH", self.vector(vec), PART) + b"\xF4"
        mu.mem_write(STUB * 16, stub)
        mu.mem_write(PART * 16 + 4, struct.pack("<H", PKG))     # PXP_PKG
        for r, v in ((UC_X86_REG_AX, ax), (UC_X86_REG_BX, bx),
                     (UC_X86_REG_CX, cx), (UC_X86_REG_DX, dx),
                     (UC_X86_REG_SI, si), (UC_X86_REG_DI, di),
                     (UC_X86_REG_BP, 0), (UC_X86_REG_DS, PKG),
                     (UC_X86_REG_ES, PKG if es is None else es),
                     (UC_X86_REG_SS, STK), (UC_X86_REG_SP, 0xFFF0),
                     (UC_X86_REG_CS, STUB), (UC_X86_REG_FLAGS, 0x0002)):
            mu.reg_write(r, v)
        mu.reg_write(UC_X86_REG_IP, 0)
        mu.emu_start(STUB * 16, STUB * 16 + len(stub) - 1, count=limit)
        cs = mu.reg_read(UC_X86_REG_CS)
        ip = mu.reg_read(UC_X86_REG_IP)
        if cs * 16 + ip != STUB * 16 + len(stub) - 1:
            raise RuntimeError("the part did not return: CS:IP %04X:%04X"
                               % (cs, ip))
        if mu.reg_read(UC_X86_REG_SP) != 0xFFF0:
            raise RuntimeError("the stack moved: SP %04X"
                               % mu.reg_read(UC_X86_REG_SP))
        if mu.reg_read(UC_X86_REG_DS) != PKG:
            raise RuntimeError("DS came back %04X"
                               % mu.reg_read(UC_X86_REG_DS))
        return (mu.reg_read(UC_X86_REG_AX),
                bool(mu.reg_read(UC_X86_REG_FLAGS) & 1))

    def decode(self, cl, **kw):
        return self.call(1, cx=cl, **kw)

#!/usr/bin/env python3
"""The HMA's pool keeps its order across a reload - SPEC.md 51.1.3.1.

    python3 tests/hmapool.py

SOUND.DRV cuts its own pool block at attach (MEMHMA_KEEP) to what its
DRV_H_KEEP says - 7KB with no mixer, 10KB with one. That only stays a gain if
the cut cannot strand it: a hibernate sweeps SOUND.DRV and loads it again
(SPEC.md 87.4 step 1) with HDD.DRV and NET.DRV still in the pool, and so does
the Drivers page, and a reload needs the WHOLE image before its attach can
cut it again. So the tail places from two ends: an image that declares
OS88_DRV_FOOT lowest-first, every other image and every buffer highest-first,
and the cut is the foot of the one free run between them.

This runs the SHIPPED build/coldhma.sys whole at FFFF:its TAIL_AT under
unicorn and drives its door (FFFF:rom_modfix, rom_id +28) the way the kernel
and the drivers do, with the shipped SOUND.DRV, HDD.DRV and NET.DRV images:

  1. SOUND.DRV placed (ROMF_HPLACE) at the pool's foot, and cut
     (ROMF_HCLAIM, CX = 8000h | keep) - for snd_keep and snd_keepmix both;
  2. HDD.DRV and NET.DRV placed at the TOP, so the free space is one run;
  3. SOUND.DRV's 8KB double buffer (OSAPI_MEM_HMA) from the top: in the pool
     with no mixer, refused with one - the pool is 24,560 bytes;
  4. the buffer back, SOUND.DRV dropped (ROMF_HDROP) as a hibernate's sweep
     does, and placed again: at the SAME base, and the pool's records what
     they were at boot.

Broken on purpose: OS88_DRV_FOOT taken out of sound.asm (every image
highest-first) and hb_alloc's two ends collapsed to one (every block
lowest-first, as before this) - each strands the reload.

Needs the `unicorn` Python module and a built tree (make).
"""
import os
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX,
                               UC_X86_REG_DX, UC_X86_REG_SP, UC_X86_REG_IP,
                               UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES,
                               UC_X86_REG_SS, UC_X86_REG_EFLAGS)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88drv      # noqa: E402

BUILD = os.environ.get("OS88_BUILD", os.path.join(ROOT, "build"))
ORG = 0xC000                    # DRV_HMA_ORG
HB_LO, HB_HI = 0x0010, 0x6000   # the pool: FFFF:0010 to `.cold`
ROMF_HPLACE, ROMF_HCLAIM, ROMF_HDROP = 4, 5, 7
MEMHMA_KEEP = 0x8000
STUB_SEG = 0x1000               # call far [door] / hlt
SSEG = 0x1800
LOAD = {"sound": 0x2000, "hdd": 0x3000, "net": 0x4000}   # the "claims"
FAILS = []


def check(cond, what):
    print("  %s  %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def image(name):
    """What the kernel hands HPLACE: the loaded image, bss zeroed"""
    img = os88drv.image_unwrap(open(os.path.join(BUILD, name + ".drv"),
                                    "rb").read())
    return img + bytes(img[31] * 16)


def mapsym(name, sym):
    for line in open(os.path.join(BUILD, name + ".hma.map")):
        p = line.split()
        if len(p) == 3 and p[2] == sym:
            return int(p[0], 16)
    sys.exit("hmapool: no %s in build/%s.hma.map" % (sym, name))


class Pool:
    def __init__(self):
        d = open(os.path.join(BUILD, "coldhma.sys"), "rb").read()
        if d[:8] != b"OS88ROM\0" or d[8] != 3:
            sys.exit("hmapool: build/coldhma.sys is not an HMA tail")
        self.at, self.htab = struct.unpack_from("<HH", d, 10)
        self.door = struct.unpack_from("<H", d, 28)[0]
        mu = self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x200000)
        mu.mem_write(0xFFFF0 + self.at, d)
        mu.mem_write(STUB_SEG << 4, b"\xFF\x1E\x10\x00\xF4" + bytes(11)
                     + struct.pack("<HH", self.door, 0xFFFF))
        for n, seg in LOAD.items():
            mu.mem_write(seg << 4, image(n))

    def call(self, al, cx=0, bx=0, dx=0, es=0):
        mu = self.mu
        for r, v in ((UC_X86_REG_AX, al), (UC_X86_REG_BX, bx),
                     (UC_X86_REG_CX, cx), (UC_X86_REG_DX, dx),
                     (UC_X86_REG_ES, es), (UC_X86_REG_DS, STUB_SEG),
                     (UC_X86_REG_CS, STUB_SEG), (UC_X86_REG_SS, SSEG),
                     (UC_X86_REG_SP, 0xFFF0)):
            mu.reg_write(r, v)
        mu.reg_write(UC_X86_REG_EFLAGS, 0x0002)
        mu.emu_start(STUB_SEG << 4, (STUB_SEG << 4) + 4, count=5_000_000)
        if mu.reg_read(UC_X86_REG_IP) != 4:
            sys.exit("hmapool: the door did not return")
        return (mu.reg_read(UC_X86_REG_EFLAGS) & 1,
                mu.reg_read(UC_X86_REG_AX), mu.reg_read(UC_X86_REG_DX))

    def place(self, name):
        cf, ax, _dx = self.call(ROMF_HPLACE, es=LOAD[name])
        return None if cf else ax

    def table(self):
        t = bytes(self.mu.mem_read(0xFFFF0 + self.htab, 48))
        return sorted(struct.unpack_from("<HHH", t, k)
                      for k in range(0, 48, 6) if struct.unpack_from("<H", t, k)[0])


def base_of(seg):
    return (seg - (0xFFFF - (ORG >> 4))) << 4


def run(arm, keep):
    print("hmapool: %s (SOUND.DRV keeps %d)" % (arm, keep))
    p = Pool()
    snd = p.place("sound")
    check(snd is not None and base_of(snd) == HB_LO,
          "SOUND.DRV placed at the pool's foot (%s)"
          % (None if snd is None else "%04X" % base_of(snd)))
    if snd is None:
        return
    cf, _, _ = p.call(ROMF_HCLAIM, cx=MEMHMA_KEEP | keep, bx=snd)
    check(not cf, "...and cut to %d bytes (MEMHMA_KEEP)" % keep)
    hdd, net = p.place("hdd"), p.place("net")
    ends = [b + n for b, n, s in p.table() if s in (hdd, net)]
    check(hdd is not None and net is not None and len(ends) == 2
          and max(ends) == HB_HI,
          "HDD.DRV and NET.DRV placed from the TOP: %r"
          % [(hex(b), n) for b, n, s in p.table()])
    boot = p.table()
    used = sum(n for _b, n, _s in boot)
    run_ = HB_HI - HB_LO - used
    cf, ax, buf = p.call(ROMF_HCLAIM, cx=8192, bx=snd)
    want = run_ >= 8192
    check(bool(not cf) == want,
          "the 8KB double buffer: %s with %d free in one run"
          % ("in the pool" if not cf else "refused - the heap's", run_))
    if not cf:
        p.call(ROMF_HCLAIM, cx=0, bx=snd, dx=buf)
    check(p.table() == boot, "...and given back: the pool as it was")
    p.call(ROMF_HDROP, dx=snd)
    again = p.place("sound")
    check(again == snd,
          "dropped and placed again (a hibernate's sweep, the Drivers page): "
          "%s, where the boot put it (%04X)"
          % ("refused - the heap" if again is None else "%04X" % again, snd))
    if again == snd:
        p.call(ROMF_HCLAIM, cx=MEMHMA_KEEP | keep, bx=snd)
        check(p.table() == boot, "...and cut again: the pool is the boot's")


def main():
    keep = mapsym("sound", "snd_keep") - ORG
    keepmix = mapsym("sound", "snd_keepmix") - ORG
    run("no mixer", keep)
    run("a mixer", keepmix)
    if FAILS:
        print("hmapool: %d FAILED" % len(FAILS))
        return 1
    print("hmapool: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

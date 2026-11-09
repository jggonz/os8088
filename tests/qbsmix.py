#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/qbsmix.py - Q*bert's sound board, sample for sample
# (ARCADE-SPEC 13)
#
#     python3 tests/qbsmix.py [--frames N]
#
# ARCADE's board (apps/arcade/arcqbs.inc: the RIOT, the hooks, the per-frame
# latch, the per-sample mix; over apps/arcade/arc6502.inc) under Unicorn, on
# the sound program from build/arcade-rom/qbert.zip, against tools/qbsnd.py's
# CPU6502 and Riot driven the SAME way: powered on in runs of 2,000 cycles;
# then, a frame at a time, the game's byte at 580Ah taken (FFh put back) and
# strobed into port A, and 181 samples each of 81 cycles and 28,490/65,536
# more, the IRQ line held through a run and refreshed after every RIOT access.
# Every sample of every frame must be the same - the DAC as the stream gets
# it. The commands are the ones Q*bert sends in attract mode and a game
# (tests/qbnative.py --sndlog), several of them twice, one over another.
# =============================================================================
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
os.chdir(ROOT)
import qbsnd                        # noqa: E402

CPS, FRAC, BOOT, SPF = 81, 28490, 178977, 181
# frame -> the byte the game writes at 5802h (580Ah after the patch)
SCRIPT = {2: 0xF4, 40: 0xF5, 60: 0xDA, 140: 0xED, 200: 0xE6, 230: 0xF4,
          231: 0xF4, 300: 0xD9, 380: 0xDC, 420: 0xF0, 470: 0xF2, 500: 0xF3,
          540: 0xE7, 580: 0xE9, 600: 0xD3}

SEG_IMG, SEG_STK, SEG_HDR, SEG_6502, SEG_G, SEG_OUT = 0x1000, 0x2000, 0x3000, 0x5000, 0x6000, 0x7000


class Ref(qbsnd.Board):
    """qbsnd's board, run the way ARCADE runs its own: by budget with a debt,
    the IRQ line a level held through a run and refreshed after a RIOT
    access."""

    def __init__(self, rom):
        super().__init__(rom)
        self.debt = 0
        self.line = False
        self.dacv = 0x80

    def read(self, a):
        v = super().read(a)
        if (a & 0x7200) == 0x0200:
            self.line = self.riot.irq()
        return v

    def write(self, a, v):
        if (a & 0x7000) == 0x1000:
            self.dacv = v
        super().write(a, v)
        if (a & 0x7200) == 0x0200:
            self.line = self.riot.irq()

    def m6run(self, budget):
        if budget <= self.debt:
            self.debt -= budget
            return
        target = budget - self.debt
        c0 = self.cpu.cyc
        while self.cpu.cyc - c0 < target:
            self.cpu.irq_line = self.line
            self.cpu.step()
        self.debt = self.cpu.cyc - c0 - target


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=640)
    a = ap.parse_args()
    zpath = os.path.join(ROOT, 'build', 'arcade-rom', 'qbert.zip')
    if not os.path.exists(zpath):
        print('qbsmix: SKIP - no build/arcade-rom/qbert.zip (make arcaderom)')
        return 0
    z = zipfile.ZipFile(zpath)
    snd = z.read('qb-snd1.bin') + z.read('qb-snd2.bin')
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
    from unicorn.x86_const import (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES,
                                   UC_X86_REG_SS, UC_X86_REG_SP, UC_X86_REG_CX,
                                   UC_X86_REG_DI)
    td = tempfile.mkdtemp(prefix='qbsmix-')
    out = os.path.join(td, 'qbsmixt.bin')
    subprocess.run(['nasm', '-f', 'bin', '-w+error', '-I', 'apps/arcade/', '-I', 'tests/',
                    'tests/qbsmixt.asm', '-o', out], check=True)
    img = open(out, 'rb').read()
    if img[:4] != b'QBS1':
        raise SystemExit('qbsmix: the wrapper has no header')
    h = struct.unpack_from('<14H', img, 4)
    prep, tick, mix, stop, c_rimgc, c_rimg, c_gseg, c_seg, c_cf, c_cyc, c_dac, c_bad = h[:12]
    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, 0x100000)
    uc.mem_write(SEG_IMG * 16, img)
    hdr = bytearray(32)
    struct.pack_into('<H', hdr, 18, 1)              # a .ROM past 64KB
    uc.mem_write(SEG_HDR * 16, bytes(hdr))
    uc.mem_write((SEG_HDR + 2) * 16 + 0x10000, snd)  # the chips at 10000h
    for cell, v in ((c_rimgc, SEG_HDR), (c_rimg, SEG_HDR + 2), (c_gseg, SEG_G),
                    (c_seg, SEG_6502)):
        uc.mem_write(SEG_IMG * 16 + cell, struct.pack('<H', v))
    uc.mem_write(SEG_G * 16 + 0x580A, b'\xff')

    def call(entry, cx=0, di=0):
        for r in (UC_X86_REG_CS, UC_X86_REG_DS):
            uc.reg_write(r, SEG_IMG)
        uc.reg_write(UC_X86_REG_ES, SEG_OUT)
        uc.reg_write(UC_X86_REG_SS, SEG_STK)
        uc.reg_write(UC_X86_REG_SP, 0xFFFE)
        uc.reg_write(UC_X86_REG_CX, cx)
        uc.reg_write(UC_X86_REG_DI, di)
        uc.emu_start(SEG_IMG * 16 + entry, SEG_IMG * 16 + stop)
    call(prep)
    if uc.mem_read(SEG_IMG * 16 + c_cf, 1)[0]:
        print('qbsmix: FAIL - qbs_prep refused')
        return 1
    ref = Ref(snd)
    ref.cpu.reset()
    ref.riot.pa_in = 0x40
    left = BOOT
    while left >= 0:
        ref.line = ref.riot.irq()
        ref.m6run(2000)
        left -= 2000
    frac = 0
    bad = []
    first_sound = None
    total = 0
    for f in range(a.frames):
        if f in SCRIPT:
            uc.mem_write(SEG_G * 16 + 0x580A, bytes([SCRIPT[f]]))
            code = (~SCRIPT[f]) & 0x3F
            ref.riot.strobe(code | 0xC0)
            ref.riot.strobe(code | 0x40)
            ref.line = ref.riot.irq()
        call(tick)
        call(mix, cx=SPF, di=0)
        got = bytes(uc.mem_read(SEG_OUT * 16, SPF))
        want = bytearray()
        for _ in range(SPF):
            ref.line = ref.riot.irq()
            frac += FRAC
            b = CPS + (frac >> 16)
            frac &= 0xFFFF
            ref.m6run(b)
            want.append(ref.dacv)
        total += SPF
        if first_sound is None and len(set(want)) > 1:
            first_sound = f
        if got != bytes(want):
            k = next(i for i in range(SPF) if got[i] != want[i])
            bad.append('frame %d sample %d: %02X, wanted %02X (cycles %d against %d)'
                       % (f, k, got[k], want[k],
                          struct.unpack('<I', bytes(uc.mem_read(SEG_IMG * 16 + c_cyc, 4)))[0],
                          ref.cpu.cyc))
            break
        if uc.mem_read(SEG_G * 16 + 0x580A, 1)[0] != 0xFF:
            bad.append('frame %d: the latch was not put back to FFh' % f)
            break
    nbad = struct.unpack('<H', bytes(uc.mem_read(SEG_IMG * 16 + c_bad, 2)))[0]
    if nbad:
        bad.append('the core met %d undocumented opcodes' % nbad)
    if first_sound is None:
        bad.append('no command made a sound: the test is not testing')
    for b in bad:
        print('qbsmix: FAIL - ' + b)
    if bad:
        return 1
    print('qbsmix: %d frames, %d samples at 10,989 Hz, %d commands: every sample the '
          'reference\'s (sound from frame %d)' % (a.frames, total, len(SCRIPT), first_sound))
    print('qbsmix: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())

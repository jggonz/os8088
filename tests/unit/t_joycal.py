#!/usr/bin/env python3
"""JOYSTICK.DRV's calibration arithmetic, run as it ships against SPEC 137.3.

    python3 tests/unit/t_joycal.py

joy_prep and joy_calpos are the driver's whole mapping from a PIT count to a
position -127..+127, and joy_dir turns one into the direction bits. The
soak row `joystick` proves them on a machine with a stick, to a tolerance a
real timer forces. This row proves them EXACTLY and in milliseconds: it
assembles drivers/joy/joy.asm to a scratch directory (the map is the
symbol table), loads the image under unicorn, and runs the routines on a grid
of calibrations x raw counts against a Python statement of SPEC 137.3 that
shares no code with the assembly:

  * dead zone = floor(half * dz / 100), each side of the centre;
  * beyond it, floor(127 * (distance - dz) / (half - dz)), clamped at 127;
    inside it 0; JOY_NONE is 0; low side negative (left and up);
  * an UNcalibrated axis takes its first count as the centre, MIN 0 and MAX
    twice it, and answers 0 (the next count is then measured against that);
  * an end on the wrong side of the centre is pulled to it (joy_prep);
  * a direction bit is |position| >= JOY_HALF, 8-way, + = right / down;

A real constants check rides along: the timeout must cover a 100 kohm stick
with margin (25.2us + 0.011us/ohm) and stay under the 4.17 ms byte time of an
8250 at 2400 baud (CLAUDE.md, retrace waits), because a timeout outside that
window fails on hardware and nowhere here.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import joyasm                                           # noqa: E402
equ = joyasm.equ

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX,
                                   UC_X86_REG_DX, UC_X86_REG_SI,
                                   UC_X86_REG_DI, UC_X86_REG_BP,
                                   UC_X86_REG_SP, UC_X86_REG_CS,
                                   UC_X86_REG_DS, UC_X86_REG_ES,
                                   UC_X86_REG_SS, UC_X86_REG_EFLAGS)
except ImportError:
    print("SKIP: the unicorn module is not installed")
    sys.exit(0)

SEG = 0x1000
BASE = SEG << 4
RET = 0xFFF8
NONE = 0xFFFF
FAILS = []


def check(ok, what):
    if not ok:
        FAILS.append(what)
        print("  FAIL  " + what)


def assemble():
    return joyasm.assemble()


def model_pos(raw, mn, cen, mx, dz):
    """SPEC 137.3, written from the text: an axis already centred."""
    if raw == NONE:
        return 0
    mn = min(mn, cen)
    mx = max(mx, cen)
    if raw >= cen:
        dist, half = raw - cen, mx - cen
        sign = 1
    else:
        dist, half = cen - raw, cen - mn
        sign = -1
    z = half * dz // 100
    if dist <= z:
        return 0
    if half - z <= 0 or dist - z >= half - z:
        return sign * 127
    return sign * (127 * (dist - z) // (half - z))


def model_dir(x, y):
    return ((1 if x <= -64 else 0) | (2 if x >= 64 else 0) |
            (4 if y <= -64 else 0) | (8 if y >= 64 else 0))


def main():
    image, sym = assemble()
    if "--break" in sys.argv:           # the red arm: must FAIL
        image, at = joyasm.sign_break(image, sym)
        print("red arm: joy_calpos's neg al at %04X is two nops" % at)
    tmo = equ("drivers/joy/joypkg.inc", "JOY_TMO")
    JCAL = {k: equ("drivers/joy/joypkg.inc", "JCAL_" + k)
            for k in ("MIN", "CEN", "MAX", "DZL", "DZH", "SZ")}
    JB_DZ = equ("drivers/joy/joypkg.inc", "JOYB_DZ")

    # --- the constants against the physics ----------------------------------
    us = tmo * 0.838
    full = 25.2 + 0.011 * 100000
    check(us >= 1.5 * full, "JOY_TMO is %.0f us: under 1.5x a 100 kohm stick's "
          "%.0f us - a stick with a worn pot would time out" % (us, full))
    check(us < 4170, "JOY_TMO is %.0f us: past the 4.17 ms byte time of an "
          "8250 at 2400 baud, so a sample would drop serial bytes" % us)

    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x100000)
    mu.mem_write(BASE, image + bytes(0x800))
    cal = BASE + sym["joy_cal"]
    blob = BASE + sym["joy_blob"]

    def call(name, regs=None):
        for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            mu.reg_write(r, SEG)
        seed = {UC_X86_REG_AX: 0, UC_X86_REG_BX: 0x2222, UC_X86_REG_CX: 0x3333,
                UC_X86_REG_DX: 0x4444, UC_X86_REG_SI: 0x5555,
                UC_X86_REG_DI: 0x6666, UC_X86_REG_BP: 0x7777}
        seed.update(regs or {})
        for r, v in seed.items():
            mu.reg_write(r, v)
        mu.reg_write(UC_X86_REG_SP, 0xFFF0)
        mu.mem_write(BASE + 0xFFF0, bytes((RET & 255, RET >> 8)))
        mu.reg_write(UC_X86_REG_EFLAGS, 0x0002)
        mu.emu_start(BASE + sym[name], BASE + RET, count=100_000)
        kept = [n for n, r in (("BX", UC_X86_REG_BX), ("CX", UC_X86_REG_CX),
                               ("DX", UC_X86_REG_DX), ("SI", UC_X86_REG_SI),
                               ("DI", UC_X86_REG_DI), ("BP", UC_X86_REG_BP))
                if mu.reg_read(r) != seed[r]]
        return mu.reg_read(UC_X86_REG_AX), kept

    def setcal(mn, cen, mx, dz):
        mu.mem_write(blob + JB_DZ, bytes((dz,)))
        w = lambda o, v: mu.mem_write(cal + o, bytes((v & 255, v >> 8)))  # noqa
        w(JCAL["MIN"], mn)
        w(JCAL["CEN"], cen)
        w(JCAL["MAX"], mx)
        call("joy_prep", {UC_X86_REG_SI: sym["joy_cal"]})

    def rd(o):
        v = bytes(mu.mem_read(cal + o, 2))
        return v[0] | v[1] << 8

    n = 0
    cals = [(0, 1000, 2000), (100, 640, 1300), (300, 700, 1500),
            (50, 1342, 1400), (0, 575, 1150)]
    for mn, cen, mx in cals:
        for dz in (0, 8, 15, 30):
            setcal(mn, cen, mx, dz)
            check(rd(JCAL["DZL"]) == (cen - mn) * dz // 100 and
                  rd(JCAL["DZH"]) == (mx - cen) * dz // 100,
                  "joy_prep(%d,%d,%d dz=%d): dead zone counts %d/%d"
                  % (mn, cen, mx, dz, rd(JCAL["DZL"]), rd(JCAL["DZH"])))
            raws = sorted(set([0, 1, mn, mn + 1, cen - 1, cen, cen + 1, mx - 1,
                               mx, mx + 1, mx + 500, NONE,
                               cen - (cen - mn) * dz // 100,
                               cen + (mx - cen) * dz // 100 + 1] +
                              list(range(0, mx + 200, 37))))
            for raw in raws:
                if raw < 0 or raw > NONE:
                    continue
                al, kept = call("joy_calpos", {UC_X86_REG_AX: raw,
                                                 UC_X86_REG_SI: sym["joy_cal"]})
                got = al & 255
                got = got - 256 if got > 127 else got
                want = model_pos(raw, mn, cen, mx, dz)
                n += 1
                check(got == want and not kept,
                      "joy_calpos raw=%d cal=(%d,%d,%d) dz=%d%%: %d, SPEC "
                      "137.3 says %d%s" % (raw, mn, cen, mx, dz, got, want,
                                           " (clobbered %s)" % kept if kept else ""))

    # --- an UNcalibrated axis centres on its first count ----------------------
    setcal(0, 0, 0, 8)                       # CEN = 0: not calibrated
    al, _ = call("joy_calpos", {UC_X86_REG_AX: 600, UC_X86_REG_SI: sym["joy_cal"]})
    check(al & 255 == 0 and rd(JCAL["CEN"]) == 600 and rd(JCAL["MIN"]) == 0
          and rd(JCAL["MAX"]) == 1200,
          "an uncalibrated axis did not take 600 as its centre, MIN 0, MAX 1200 "
          "(CEN %d MIN %d MAX %d)" % (rd(JCAL["CEN"]), rd(JCAL["MIN"]), rd(JCAL["MAX"])))
    al, _ = call("joy_calpos", {UC_X86_REG_AX: 1200, UC_X86_REG_SI: sym["joy_cal"]})
    check(al & 255 == model_pos(1200, 0, 600, 1200, 8),
          "after auto-centring, 1200 answers %d, not %d" % (al & 255,
                                                           model_pos(1200, 0, 600, 1200, 8)))
    setcal(0, 0, 0, 8)
    al, _ = call("joy_calpos", {UC_X86_REG_AX: NONE, UC_X86_REG_SI: sym["joy_cal"]})
    check(al & 255 == 0 and rd(JCAL["CEN"]) == 0,
          "JOY_NONE centred an uncalibrated axis (CEN %d)" % rd(JCAL["CEN"]))

    # --- directions --------------------------------------------------------------
    for x in (-127, -64, -63, 0, 63, 64, 127):
        for y in (-127, -64, -63, 0, 63, 64, 127):
            al, _ = call("joy_dir", {UC_X86_REG_AX: ((y & 255) << 8) | (x & 255)})
            n += 1
            check(al & 255 == model_dir(x, y), "joy_dir(%d,%d) = %d, expected %d"
                  % (x, y, al & 255, model_dir(x, y)))

    print("t_joycal: %d points, %d failed" % (n, len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())

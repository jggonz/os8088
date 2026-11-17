#!/usr/bin/env python3
"""PDREND's second code segment (PAGEDESK-SPEC 20.22.7.9) run as the 8086 runs
it: the BUILT PDREND.MOD and PDRENDC.MOD in Unicorn, in claims of their own,
driven through the door at +8 the way the primary drives it.

  * before PDV_REND_CODE every verb is refused (PDE_RANGE, CF = 1);
  * a part that is not this build's (wrong magic, wrong size) is refused;
  * bound, INIT / SLOT / STATUS run - all three are moved code, so each is a
    resident -> part far call, INIT's pd_fcopy_in and pdr_zero are part ->
    resident (cc_ovthunk) and its xc_adopt part -> part (cc_ovmthunk); SLOT's
    effect is read back through STATUS (swaps = 1), so a wrong vector is a
    wrong answer and not only a crash;
  * the part MOVED (copied to another claim, the old one filled with int3)
    and bound again: everything still answers, from the new copy;
  * no kernel call (a far call to 0060:xxxx) on any of these paths.

    pdr_ovltest.py <build dir>
"""
import os, struct, sys
try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
                                   UC_X86_REG_CS, UC_X86_REG_IP, UC_X86_REG_SS, UC_X86_REG_SP,
                                   UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_FLAGS)
except Exception as e:
    print('pdr_ovltest: SKIP - unicorn missing (%s)' % e); sys.exit(0)

b = sys.argv[1] if len(sys.argv) > 1 else 'build'
mod = open(os.path.join(b, 'PDREND.MOD'), 'rb').read()
part = open(os.path.join(b, 'PDRENDC.MOD'), 'rb').read()
img, bss = struct.unpack_from('<HH', mod, 4)
PDV_INIT, PDV_SLOT, PDV_STATUS, PDV_CODE = 0x40, 0x48, 0x46, 0x49
PDE_RANGE = 32
MSEG, CSEG, CSEG2, XSEG, SSEG, STUB = 0x2000, 0x3000, 0x5000, 0x4000, 0x6000, 0x0050
fails, n = [], 0
def check(name, cond, why=''):
    global n
    n += 1
    print(('  ok   ' if cond else '  FAIL ') + name + ('' if cond else ' ' + why))
    if not cond: fails.append(name)

uc = Uc(UC_ARCH_X86, UC_MODE_16); uc.mem_map(0, 0x100000)
uc.mem_write(MSEG << 4, mod + bytes(bss))
uc.mem_write(CSEG << 4, part)
# the caller: call far MSEG:0008 ; hlt
uc.mem_write(STUB << 4, bytes([0x9A, 8, 0, MSEG & 0xFF, MSEG >> 8, 0xF4]))
kcall = []
def hook(u, addr, size, _):
    if 0x600 <= addr < 0x600 + 0x10000: kcall.append(addr); u.emu_stop()
    if u.mem_read(addr, 1) == b'\xcc': kcall.append(-addr); u.emu_stop()
uc.hook_add(UC_HOOK_CODE, hook)

def door(al, bx=0, cx=0, dx=0):
    for r, v in ((UC_X86_REG_AX, al), (UC_X86_REG_BX, bx), (UC_X86_REG_CX, cx), (UC_X86_REG_DX, dx),
                 (UC_X86_REG_SS, SSEG), (UC_X86_REG_SP, 0xFFFE), (UC_X86_REG_DS, 0x1111), (UC_X86_REG_ES, 0x2222),
                 (UC_X86_REG_CS, 0), (UC_X86_REG_FLAGS, 0x0002)):
        uc.reg_write(r, v)
    uc.emu_start(STUB << 4, (STUB << 4) + 5, count=2_000_000)
    ip = (uc.reg_read(UC_X86_REG_CS) << 4) + uc.reg_read(UC_X86_REG_IP)
    return (uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_FLAGS) & 1, ip == (STUB << 4) + 5,
            uc.reg_read(UC_X86_REG_DS) == 0x1111 and uc.reg_read(UC_X86_REG_ES) == 0x2222)

def status():
    uc.mem_write(XSEG << 4, b'\xAA' * 64)
    ax, cf, back, segs = door(PDV_STATUS)
    rs = uc.mem_read(XSEG << 4, 42)
    return ax, cf, back, struct.unpack_from('<HHHHH', rs, 0)[0:1] + struct.unpack_from('<HHHH', rs, 34)

ax, cf, back, segs = door(PDV_STATUS)
check('unbound: STATUS refused (PDE_RANGE, CF), returned, DS/ES kept', back and cf and ax == PDE_RANGE and segs, '%d %d %d' % (ax, cf, back))
uc.mem_write(CSEG2 << 4, b'\0' * 16)
ax, cf, back, _ = door(PDV_CODE, CSEG2, CSEG2)
check('a part with no stamp is refused', back and cf and ax == PDE_RANGE)
bad = bytearray(part); struct.pack_into('<H', bad, 2, len(part) + 2); uc.mem_write(CSEG2 << 4, bytes(bad[:4]))
ax, cf, back, _ = door(PDV_CODE, CSEG2, CSEG2)
check("another build's part (size differs) is refused", back and cf and ax == PDE_RANGE)
ax, cf, back, _ = door(PDV_INIT, XSEG)
check('still unbound after the refusals: INIT refused', back and cf and ax == PDE_RANGE)
# the negative control: a code selector that is not where the part is - the
# stamp is checked through CX, so it binds, and the first moved call must land
# in the int3 the wrong claim holds
uc.mem_write(CSEG2 << 4, b'\xCC' * len(part))
door(PDV_CODE, CSEG2, CSEG)
door(PDV_INIT, XSEG)
check('negative control: a wrong code selector is caught (int3 in the wrong claim)', any(a < 0 for a in kcall), str(kcall))
kcall.clear()
ax, cf, back, segs = door(PDV_CODE, CSEG, CSEG)
check('PDV_REND_CODE binds this build\'s part', back and not cf and ax == 0 and segs, '%d %d' % (ax, cf))

uc.mem_write(XSEG << 4, b'\0' * 128)
ax, cf, back, _ = door(PDV_INIT, XSEG)
check('INIT (moved; calls resident and moved code) answers OK', back and not cf and ax == 0 and not kcall, '%d %d %s' % (ax, cf, kcall))
ax, cf, back, st = status()
check('STATUS (moved) writes a fresh status: nothing skipped, no swaps', back and not cf and st == (0, 0, 0, 0, 0), str(st))
ax, cf, back, _ = door(PDV_SLOT, 1, 0x0010, 0x7000)
check('SLOT (moved) answers OK', back and not cf and ax == 0)
ax, cf, back, st = status()
check('STATUS reads SLOT\'s effect: one swap', back and not cf and st[4] == 1, str(st))

# the claim moves: a copy elsewhere, the old one int3, bound again
uc.mem_write(CSEG2 << 4, part); uc.mem_write(CSEG << 4, b'\xCC' * len(part))
ax, cf, back, _ = door(PDV_CODE, CSEG2, CSEG2)
check('bound again after the part moved', back and not cf and ax == 0)
ax, cf, back, _ = door(PDV_SLOT, 2, 0x0010, 0x7100)
ax2, cf2, back2, st = status()
check('the moved part answers SLOT and STATUS (two swaps), the old copy never run',
      back and back2 and not cf and not cf2 and st[4] == 2 and not kcall, '%s %s' % (st, kcall))
check('no kernel call on any path', not kcall, str(kcall))
print('pdr_ovltest: %d checks, %d failed (PDREND image %d + bss %d, PDRENDC %d)' % (n, len(fails), img, bss, len(part)))
sys.exit(1 if fails else 0)

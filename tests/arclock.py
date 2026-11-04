#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arclock.py - ARCADE's translated code, in LOCKSTEP with the
# reference model (docs/ARCADE-SPEC.md sections 6.1 and 7.6)
#
#     python3 tests/arclock.py CODE.bin AUX.bin ROM [--board invaders|pacman]
#                              [--frames N]
#
# CODE.bin is a code part, AUX.bin the PC table the translator wrote beside
# it, ROM the board image both came from. The code part runs under Unicorn's
# 8086 with the view, aux and stack laid out as the package lays them out,
# and the board's model (tools/arcsim.py for the 8080 board, tools/arcpac.py
# for Pac-Man's) runs beside it. At every guest instruction boundary - every
# entry of the PC table the code reaches - the two must agree on the PC, A,
# BC, DE, HL, SP (and IX, IY on the Z80); on the flags wherever the guest is
# about to READ them; and once a frame on every byte of RAM and video.
# Interrupts go in on the same instruction in both, at the model's T-states:
# the translated code does not count cycles (the package's clock is the PC's,
# ARCADE-SPEC 5), so the harness takes the model's word for WHEN.
#
# What is not compared, and why:
#   * AC/H after a logical op or a subtract on the 8080, N on the Z80, and X
#     and Y: only DAA reads them, and ARCADE-SPEC 3.5 and 7.3 say what DAA
#     sees. The flags are compared where an instruction reads them, and in a
#     pushed F only S Z P C.
#   * flags DEAD at an interrupted instruction: the translation may leave
#     garbage there, legally; the model takes the 8086's.
#   * the dead space under the stack, where the translation's own CALLs push.
#
# Needs `pip install unicorn`. Exit 0 = every boundary agreed.
# =============================================================================
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'apps', 'arcade'))

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE   # noqa: E402
from unicorn import x86_const as X  # noqa: E402

CSEG, VSEG, ASEG = 0x1000, 0x2000, 0x3000


def defs():
    """Constants straight out of arcdefs.inc, so the two cannot drift."""
    out = {}
    for ln in open(os.path.join(ROOT, 'apps', 'arcade', 'arcdefs.inc')):
        f = ln.split(';')[0].split()
        if len(f) == 3 and f[1] == 'equ':
            try:
                out[f[0]] = int(f[2], 0)
            except ValueError:
                pass
    return out


D = defs()


def seeds_of(name):
    out = []
    for x in open(os.path.join(ROOT, 'apps', 'arcade', name + '.seeds')):
        x = x.split('#')[0].strip()
        if x.startswith('IDLE '):
            continue
        if x.startswith('RST '):
            import arcz80t
            vec, k = x[4:].split()
            arcz80t.RST_RET[int(vec, 16)] = int(k)
        elif x:
            out.append(int(x, 16))
    return out


class Invaders:
    ramlo, ramhi = 0x2000, 0x4000
    sp0 = 0x2400

    def __init__(self, img, name='invaders'):
        import arcsim
        import arcaot
        self.arcsim = arcsim
        self.m = arcsim.M(img, name)
        self.m.sp = self.sp0
        self.img = img
        rom = arcaot.board_rom(img)             # the CPU's view of it
        arcaot.set_ports(name)
        self.rom = rom
        self.table_pcs = arcaot.TABLE_PCS
        self.half = 1
        self.end = arcsim.HALF
        self.frame = 0
        flow = arcaot.walk(rom, seeds_of(name))
        arcaot.speculate(rom, flow)
        arcaot.liveness(flow)
        arcaot.mark_polls(flow)
        self.idioms = {pc: v[1] for pc, v in arcaot.find_idioms(rom, flow).items()}
        self.live = {pc: (i.lin, i.use) for pc, i in flow.items()}
        self.C, self.Z, self.ALL = arcaot.C, arcaot.Z, arcaot.ALL

    def view(self):
        return self.img[:0x5000]

    def setup(self, u):
        b = self.m.board
        u.mem_write(VSEG * 16 + D['V_IN0'], bytes([b['in0'], b['in1'], b['in2']]))

    def regs(self):
        m = self.m
        return (m.pc, m.a, (m.b << 8) | m.c, (m.d << 8) | m.e, (m.h << 8) | m.l, m.sp)

    def f(self):
        return self.m.f()

    def setf(self, v):
        self.m.setf(v)

    def reads(self, pc):
        use = self.live.get(pc, (0, 0))[1]
        mask = (0x01 if use & self.C else 0) | (0xC4 if use & self.Z else 0)
        if self.rom[pc] == 0x27:
            mask |= 0x10
        return mask

    def lin(self, pc):
        lin = self.live.get(pc, (self.ALL, 0))[0]
        return (0x01 if lin & self.C else 0) | (0xD4 if lin & self.Z else 0)

    def deadstack(self):
        return 0x2300

    def step(self, pc, u):
        """Step the model across one translated boundary; return the
        interrupt it took after it as (return PC, vector PC), or None."""
        m = self.m
        m.step()
        if pc in self.idioms:
            while pc <= m.pc < pc + self.idioms[pc]:
                m.step()
        if m.cyc < self.end:
            return None
        return self._cross(u)

    def wait_halt(self, u, resume):
        """The 8086 side reached arc_halt, resuming at `resume`: a HLT, or a
        spin loop's branch (arcaot.py's SPINS). Run the model - its HLT, or
        its spin - until the interrupt goes in; a spin's only where the
        8086 side will resume, the loop's head, a few T-states late at most
        (every pass is the same, so nothing else can differ)."""
        m = self.m
        while True:
            if m.halted:
                m.cyc = max(m.cyc, self.end)
            if m.cyc >= self.end and (m.halted or m.pc == resume):
                took = self._cross(u)
                if took:
                    return took
                continue
            m.step()

    def _cross(self, u):
        m = self.m
        n = self.half
        took = (m.pc, n * 8) if m.ie else None
        m.interrupt(n)
        self.half = 3 - n
        self.end += self.arcsim.HALF
        if n == 2:
            self.frame += 1
            m.in0, m.in1, m.in2 = self.arcsim.script(self.frame, m.board)
            # the 8086 side gets them when the interrupt goes IN, not now:
            # it has still to run the instruction the model just stepped,
            # and an IN there would read the next frame's (main's redirect)
            self.pending = bytes([m.in0, m.in1, m.in2])
            self.newframe = True
        return took

    def ram(self):
        return bytearray(self.m.m[self.ramlo:self.ramhi])


class PacMan:
    ramlo, ramhi = 0x4000, 0x5000
    sp0 = 0xFFFF

    def __init__(self, img):
        import arcpac
        import arcz80t
        import z80dec
        self.arcpac = arcpac
        self.z = z80dec
        self.b = arcpac.Board(img)
        self.rom = img[:0x4000]
        self.frame = 0
        self.end = arcpac.FRAME_T
        flow = arcz80t.walk(self.rom, seeds_of('pacman'))
        arcz80t.speculate(self.rom, flow)
        arcz80t.liveness(flow)
        self.flow = flow

    def view(self):
        v = bytearray(0x6400)
        v[0:0x4000] = self.rom
        v[0x4800:0x4C00] = b'\xBF' * 0x400
        v[0x5000] = self.b.in0
        v[0x5040] = self.b.in1
        v[0x5080] = self.arcpac.DSW1
        v[0x50C0] = 0xFF
        c = self.b.cpu
        v[D['ST_IX']:D['ST_IX'] + 4] = bytes([c.ix & 255, c.ix >> 8, c.iy & 255, c.iy >> 8])
        return bytes(v)

    def setup(self, u):
        pass

    def regs(self):
        c = self.b.cpu
        return (c.pc, c.a, c.bc, c.de, c.hl, c.sp)

    def xy(self):
        return (self.b.cpu.ix, self.b.cpu.iy)

    def f(self):
        return self.b.cpu.f

    def setf(self, v):
        self.b.cpu.f = v

    def reads(self, pc):
        o = self.flow.get(pc)
        if o is None:
            return 0
        z = self.z
        mask = (0x01 if o.use & z.C else 0) | (0xC0 if o.use & z.Z else 0) | \
            (0x04 if o.use & z.P else 0)
        if o.op == 0x27 and not o.pfx:
            mask |= 0x10
        return mask

    def lin(self, pc):
        o = self.flow.get(pc)
        lin = o.lin if o else self.z.ALL
        z = self.z
        return (0x01 if lin & z.C else 0) | (0xC0 if lin & z.Z else 0) | \
            (0x04 if lin & z.P else 0) | (0x10 if lin & z.H else 0)

    def deadstack(self):
        return max(self.ramlo, self.b.cpu.sp - 32)

    def step(self, pc, u):
        c = self.b.cpu
        rep = self.rom[pc] == 0xED and self.rom[pc + 1] in (0xB0, 0xB1, 0xB8, 0xB9)
        c.cyc += c.step()
        while rep and c.pc == pc:           # LDIR and kin: one boundary on the
            c.cyc += c.step()               #   8086 side, so one here, and an
        return self._after(u)               #   interrupt waits for the end

    def _after(self, u):
        b = self.b
        c = b.cpu
        if c.cyc >= self.end:
            self.end += self.arcpac.FRAME_T
            if b.irq_en:
                b.pending = True
            self.frame += 1
            b.in0, b.in1 = self.arcpac.script(self.frame)
            u.mem_write(VSEG * 16 + 0x5000, bytes([b.in0]))
            u.mem_write(VSEG * 16 + 0x5040, bytes([b.in1]))
            self.newframe = True
        if b.pending and c.iff1 and not c.ei_delay:
            ret = (c.pc + 1) & 0xFFFF if c.halted else c.pc
            t = c.irq(b.vec)
            if t:
                b.pending = False
                c.cyc += t
                return (ret, c.pc)
        return None

    def wait_halt(self, u):
        """The 8086 side reached arc_halt: run the model's HALT until it
        takes the interrupt."""
        c = self.b.cpu
        while True:
            c.cyc += c.step()
            took = self._after(u)
            if took:
                return took

    def ram(self):
        r = bytearray(self.b.m[self.ramlo:self.ramhi])
        r[0x800:0xC00] = b'\xBF' * 0x400       # 4800-4BFF: what reads give
        return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('code')
    ap.add_argument('aux')
    ap.add_argument('rom')
    ap.add_argument('--board', default='invaders',
                    choices=('invaders', 'invadpt2', 'lrescue', 'gunfight', 'seawolf',
                             'pacman'))
    ap.add_argument('--frames', type=int, default=600)
    ap.add_argument('--marks', action='store_true',
                    help='hold the write list to the screen: every VRAM cell that '
                         'changed in a frame must be on it (an 8080 board)')
    a = ap.parse_args()
    code = open(a.code, 'rb').read()
    aux = open(a.aux, 'rb').read()
    img = open(a.rom, 'rb').read()
    bd = PacMan(img) if a.board == 'pacman' else Invaders(img, a.board)
    bd.newframe = False
    npcs = getattr(bd, 'table_pcs', 0x4000)
    table = [aux[2 * p] | (aux[2 * p + 1] << 8) for p in range(npcs)]
    miss = table[0x3FFF]
    at = {}
    for pc, off in enumerate(table):
        if off != miss:
            at.setdefault(off, []).append(pc)
    # arc_halt, by its bytes: LAHF / MOV BYTE [ST_WHY], ARCX_HALT
    sig = bytes([0x9F, 0xC6, 0x06, D['ST_WHY'] & 255, D['ST_WHY'] >> 8, D['ARCX_HALT']])
    halt_entry = code.find(sig)
    # arc_miss: LAHF / SHR SI,1 / MOV BYTE [ST_WHY], ARCX_MISS
    msig = bytes([0x9F, 0xD1, 0xEE, 0xC6, 0x06, D['ST_WHY'] & 255, D['ST_WHY'] >> 8,
                  D['ARCX_MISS']])
    miss_entry = code.find(msig)

    u = Uc(UC_ARCH_X86, UC_MODE_16)
    u.mem_map(0, 0x100000)
    u.mem_write(CSEG * 16, code)
    u.mem_write(VSEG * 16, bd.view())
    u.mem_write(ASEG * 16, aux)
    u.reg_write(X.UC_X86_REG_CS, CSEG)
    for r in (X.UC_X86_REG_DS, X.UC_X86_REG_SS):
        u.reg_write(r, VSEG)
    u.reg_write(X.UC_X86_REG_ES, ASEG)
    u.reg_write(X.UC_X86_REG_SP, bd.sp0)
    u.reg_write(X.UC_X86_REG_BP, 0x7FFF)
    u.reg_write(X.UC_X86_REG_DI, D['AUX_LIST'])
    u.reg_write(X.UC_X86_REG_EFLAGS, 0x0002)
    bd.setup(u)
    st = {'n': 0, 'fail': None, 'redirect': None, 'halt': False, 'done': False}

    def regs8086():
        g = u.reg_read
        ax = g(X.UC_X86_REG_AX)
        return ((ax & 255, g(X.UC_X86_REG_CX), g(X.UC_X86_REG_DX),
                 g(X.UC_X86_REG_BX), g(X.UC_X86_REG_SP)),
                g(X.UC_X86_REG_EFLAGS) & 0xD5)

    def fail(msg):
        st['fail'] = msg
        u.emu_stop()
        return True

    seen = {'vram': bytes(u.mem_read(VSEG * 16 + 0x2400, 0x1C00))}

    def check_marks():
        """--marks: the host's half (ARCADE-SPEC 4.1). Every 8x8 cell whose
        VRAM changed since the last frame must be on the write list (or
        flagged, from a list kept in DI that a run has not saved yet); then
        the flags cleared and the list emptied, as ar_render does."""
        vram = bytes(u.mem_read(VSEG * 16 + 0x2400, 0x1C00))
        di = u.reg_read(X.UC_X86_REG_DI)
        lst = bytes(u.mem_read(ASEG * 16 + D['AUX_LIST'], max(0, di - D['AUX_LIST'])))
        keys = {lst[i] | (lst[i + 1] << 8) for i in range(0, len(lst), 2)}
        flags = bytes(u.mem_read(ASEG * 16 + D['AUX_FLAGS'] + 0x2400, 0x1C00))
        old = seen['vram']
        for o in range(0x1C00):
            if vram[o] != old[o]:
                key = 0x2400 + (o & 0xFF1F)
                if key not in keys:
                    return ('frame %d: VRAM %04X changed (%02X -> %02X) and its cell %04X '
                            'is not on the write list (flag %d)'
                            % (bd.frame, 0x2400 + o, old[o], vram[o], key, flags[key - 0x2400]))
        for k in keys:
            u.mem_write(ASEG * 16 + D['AUX_FLAGS'] + k, b'\x00')
        u.reg_write(X.UC_X86_REG_DI, D['AUX_LIST'])
        seen['vram'] = vram
        return None

    def compare_ram():
        ram = bytearray(u.mem_read(VSEG * 16 + bd.ramlo, bd.ramhi - bd.ramlo))
        ref = bd.ram()
        sp = bd.regs()[5]
        for k in range(bd.deadstack(), sp):         # under the SP: dead, and
            if bd.ramlo <= k < bd.ramhi:            #   our own CALLs push there
                ram[k - bd.ramlo] = ref[k - bd.ramlo] = 0
        for k in range(sp, min(sp + 64, bd.ramhi)):  # a pushed F: only S Z P C
            if k >= bd.ramlo and (ram[k - bd.ramlo] ^ ref[k - bd.ramlo]) & 0xC5 == 0:
                ram[k - bd.ramlo] = ref[k - bd.ramlo]
        if ram != ref:
            k = next(i for i in range(len(ram)) if ram[i] != ref[i])
            return 'frame %d: memory differs first at %04X: %02X, the model %02X' % (
                bd.frame, bd.ramlo + k, ram[k], ref[k])
        return None

    def one(uc, pc):
        if st['redirect']:
            if pc != st['redirect'][0]:
                return fail('interrupted at %04X, the model at %04X' % (pc, st['redirect'][0]))
            uc.emu_stop()
            return True
        if getattr(bd, 'pending', None):        # the next frame's inputs, once
            uc.mem_write(VSEG * 16 + D['V_IN0'], bd.pending)   # the model's
            bd.pending = None                   #   step is behind both sides
        if bd.newframe:
            bd.newframe = False
            msg = compare_ram() or (check_marks() if a.marks else None)
            if msg:
                return fail(msg)
            if bd.frame >= a.frames:
                st['done'] = True
                uc.emu_stop()
                return True
        uc.reg_write(X.UC_X86_REG_BP, 0x7FFF)       # no poll ever exits
        if not a.marks:
            uc.reg_write(X.UC_X86_REG_DI, D['AUX_LIST'])    # nothing draws here
        got, fl = regs8086()
        want = bd.regs()
        if pc != want[0]:
            return fail('PC %04X, the model is at %04X' % (pc, want[0]))
        if got != want[1:]:
            return fail('at %04X: A BC DE HL SP %s, the model %s' % (
                pc, ' '.join('%X' % v for v in got), ' '.join('%X' % v for v in want[1:])))
        if isinstance(bd, PacMan):
            xy = bytes(uc.mem_read(VSEG * 16 + D['ST_IX'], 4))
            gx = (xy[0] | (xy[1] << 8), xy[2] | (xy[3] << 8))
            if gx != bd.xy():
                return fail('at %04X: IX IY %04X %04X, the model %04X %04X'
                            % ((pc,) + gx + bd.xy()))
        mask = bd.reads(pc)
        if (fl & mask) != (bd.f() & mask):
            return fail('at %04X: flags %02X, the model %02X (mask %02X)'
                        % (pc, fl, bd.f(), mask))
        st['n'] += 1
        st['last'] = pc
        if os.environ.get('ARCPCS'):
            lo, hi = (int(x, 16) for x in os.environ['ARCPCS'].split('-'))
            if lo <= pc <= hi:
                print('arclock: %04X frame %d A=%02X F=%02X IX=%04X' % (
                    pc, bd.frame, want[1], bd.f(), bd.xy()[0] if hasattr(bd, 'xy') else 0))
        took = bd.step(pc, uc)
        if took:
            st['redirect'] = took
        return False

    trail = []

    def hook(uc, addr, size, _):
        off = addr - CSEG * 16
        if os.environ.get('ARCTRAIL'):
            trail.append(off)
            del trail[:-int(os.environ['ARCTRAIL'])]
        if off == miss_entry:
            fail('a jump to %04X, which has no translation (the guest last at %04X)'
                 % (uc.reg_read(X.UC_X86_REG_SI) >> 1, st.get('last', 0)))
            return
        if off == halt_entry:
            st['halt'] = True
            st['halt_pc'] = uc.reg_read(X.UC_X86_REG_SI)
            uc.emu_stop()
            return
        pcs = at.get(off)
        if pcs is None:
            return
        want = bd.regs()[0]
        if not st['redirect'] and want not in pcs:
            fail('the 8086 side is at %s, the model at %04X'
                 % ('/'.join('%04X' % p for p in pcs), want))
            return
        if st['redirect'] and st['redirect'][0] in pcs:
            one(uc, st['redirect'][0])      # PCs sharing an offset (a NOP's
            return                          #   and the next): the one the
        for pc in pcs:                      #   model is at was jumped to
            if pc == bd.regs()[0] or st['redirect']:
                if one(uc, pc):
                    return

    u.hook_add(UC_HOOK_CODE, hook, begin=CSEG * 16, end=CSEG * 16 + len(code))
    if os.environ.get('ARCWATCH'):            # a guest address's writes, logged
        from unicorn import UC_HOOK_MEM_WRITE
        wa = VSEG * 16 + int(os.environ['ARCWATCH'], 16)

        def watch(uc, access, addr, size, value, _):
            if addr <= wa < addr + size:
                print('arclock: write %02X at %04X by %04X (the guest at %04X, frame %d)'
                      % ((value >> (8 * (wa - addr))) & 255, wa - VSEG * 16,
                         uc.reg_read(X.UC_X86_REG_IP), st.get('last', 0), bd.frame))
        u.hook_add(UC_HOOK_MEM_WRITE, watch, begin=wa - 1, end=wa)
    ip = table[0]
    while True:
        try:
            u.emu_start(CSEG * 16 + ip, 0x100000, count=0)
        except Exception as e:                  # a crash on the 8086 side
            st['fail'] = '%s at %04X:%04X, the guest last at %04X' % (
                e, u.reg_read(X.UC_X86_REG_CS), u.reg_read(X.UC_X86_REG_IP),
                st.get('last', 0))
        if st['fail'] and trail:
            print('arclock: the last 8086 offsets: ' + ' '.join('%04X' % t for t in trail[-60:]))
            if os.environ.get('ARCTRAILFILE'):
                open(os.environ['ARCTRAILFILE'], 'w').write('\n'.join('%04X' % t for t in trail))
        if st['fail']:
            print('arclock: FAIL after %d instructions, frame %d: %s'
                  % (st['n'], bd.frame, st['fail']))
            return 1
        if st['done']:
            break
        if st['halt']:
            st['halt'] = False
            if isinstance(bd, PacMan):
                st['redirect'] = bd.wait_halt(u)
            else:
                st['redirect'] = bd.wait_halt(u, st['halt_pc'])
        if st['redirect']:
            ret, vec = st['redirect']
            st['redirect'] = None
            if getattr(bd, 'pending', None):
                u.mem_write(VSEG * 16 + D['V_IN0'], bd.pending)
                bd.pending = None
            live = bd.lin(ret)
            fl = u.reg_read(X.UC_X86_REG_EFLAGS) & 0xD5
            bd.setf((bd.f() & live) | (fl & ~live & 0xD5) | (bd.f() & 0x2A & 0))
            sp = (u.reg_read(X.UC_X86_REG_SP) - 2) & 0xFFFF
            u.reg_write(X.UC_X86_REG_SP, sp)
            u.mem_write(VSEG * 16 + sp, bytes([ret & 255, ret >> 8]))
            u.mem_write(VSEG * 16 + D['ST_INTE'], b'\x00')
            ip = table[vec]
            continue
        print('arclock: the 8086 side stopped at %04X:%04X with nothing to say'
              % (u.reg_read(X.UC_X86_REG_CS), u.reg_read(X.UC_X86_REG_IP)))
        return 1
    print('arclock: %d guest instructions in lockstep over %d frames, all agree'
          % (st['n'], bd.frame))
    return 0


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/arcsim.py  (wave 0 form: a measuring model, not yet the oracle)
#
# An Intel 8080 plus the Midway 8080 B&W board (Space Invaders), in Python:
# ARCADE's reference model (docs/ARCADE-SPEC.md section 6). It runs the ROM
# image tools/getarcrom.py writes, two interrupts a frame at fixed T-states,
# with a scripted coin, start and play (SCRIPT below).
#
#     python3 -I tools/arcsim.py stats  ROM [--frames N] [--pbm OUT]
#         the per-frame load: instructions, VRAM writes, dirty 8x8 cells,
#         the share of time in the interrupt handlers, the hottest PCs
#         (docs/plans/ARCADE-EMU-PLAN.md section 2 is its output)
#     python3 -I tools/arcsim.py seeds  ROM [--frames N] [-o FILE]
#         every target a PCHL or a RET reached that is not the instruction
#         after a CALL: the entry points a static walk of the ROM cannot
#         see, which apps/arcade/arcaot.py is handed as seeds
#     python3 -I tools/arcsim.py digest ROM --at F[,F...] [--pbm-prefix P]
#         the SHA-1 of VRAM (2400-3FFF) after frame F, and its screen
#
# No ROM is committed; this reads build/arcade-rom/INVADERS.ROM.
# =============================================================================
import argparse, collections, hashlib, sys

PAR = [0] * 256
for i in range(256):
    PAR[i] = 4 if bin(i).count('1') % 2 == 0 else 0

CYC = [
 4,10,7,5,5,5,7,4,4,10,7,5,5,5,7,4, 4,10,7,5,5,5,7,4,4,10,7,5,5,5,7,4,
 4,10,16,5,5,5,7,4,4,10,16,5,5,5,7,4, 4,10,13,5,10,10,10,4,4,10,13,5,5,5,7,4,
 5,5,5,5,5,5,7,5,5,5,5,5,5,5,7,5, 5,5,5,5,5,5,7,5,5,5,5,5,5,5,7,5,
 5,5,5,5,5,5,7,5,5,5,5,5,5,5,7,5, 7,7,7,7,7,7,7,7,5,5,5,5,5,5,7,5,
 4,4,4,4,4,4,7,4,4,4,4,4,4,4,7,4, 4,4,4,4,4,4,7,4,4,4,4,4,4,4,7,4,
 4,4,4,4,4,4,7,4,4,4,4,4,4,4,7,4, 4,4,4,4,4,4,7,4,4,4,4,4,4,4,7,4,
 5,10,10,10,11,11,7,11,5,10,10,10,11,17,7,11, 5,10,10,10,11,11,7,11,5,10,10,10,11,17,7,11,
 5,10,10,18,11,11,7,11,5,5,10,4,11,17,7,11, 5,10,10,4,11,11,7,11,5,5,10,4,11,17,7,11]

# THE BOARDS (ARCADE-SPEC 5.3, 9): Midway's Space Invaders, and Taito's
# colour boards - the same board with ROM at 4000h too and a colour map PROM
# (MAME 8080bw.cpp). in0/in1/in2 are the ports at rest; coin is IN 1's bit 0
# pressed, active high on our Space Invaders (as it has always been run) and
# active low on Taito's (MAME); `both` puts player 1's fire, left and right
# on IN 2 as well as IN 1 (an upright cabinet's wiring).
BOARDS = {
    'invaders': dict(size=0x2000, io='mw', in0=0x00, in1=0x08, in2=0x00, coin_low=False, both=False),
    'invadpt2': dict(size=0x5800, io='mw', in0=0xF4, in1=0x81, in2=0x00, coin_low=True, both=True),
    'lrescue':  dict(size=0x5800, io='mw', in0=0xFF, in1=0x89, in2=0x00, coin_low=True, both=True),
    # Midway's landscape boards: Gun Fight's IN 0/1 are each gunman's stick,
    # aim and trigger and IN 2 the coin, the start and the DIPs; Sea Wolf's
    # IN 1 is the periscope and the trigger, IN 2 the coin and the start
    'gunfight': dict(size=0x1000, io='gunfight', in0=0x30, in1=0x30, in2=0x10),
    'seawolf':  dict(size=0x1000, io='seawolf', in0=0x40, in1=0x60, in2=0x00),
}
# the positional controls' encoders (MAME mw8080bw.cpp's remap tables, the
# field then inverted): Gun Fight's 7-position aim, Sea Wolf's 30-position
# periscope
GF_AIM = (0x06, 0x02, 0x00, 0x04, 0x05, 0x01, 0x03)
SW_SCOPE = (0x1e, 0x1c, 0x1d, 0x19, 0x18, 0x1a, 0x1b, 0x13, 0x12, 0x10,
            0x11, 0x15, 0x14, 0x16, 0x17, 0x07, 0x06, 0x04, 0x05, 0x01,
            0x00, 0x02, 0x03, 0x0b, 0x0a, 0x08, 0x09, 0x0d, 0x0c, 0x0e)


def gf_aim(pos):
    return ((~GF_AIM[pos]) & 7) << 4


def sw_scope(pos):
    return (~SW_SCOPE[pos]) & 0x1f
ROMTOP = 0x5000             # a colour board's image: the CPU's 0000-4FFFh,
                            #   then the colour maps (not the CPU's)


def is_rom(a):
    """ROM in the CPU's view: 0000-1FFFh and 4000-5FFFh (MAME mw8080bw)."""
    a &= 0x7fff
    return a < 0x2000 or 0x4000 <= a < 0x6000


class M:
    def __init__(s, rom, board='invaders'):
        s.board = BOARDS[board]
        s.m = bytearray(0x10000); s.m[0:min(len(rom), ROMTOP)] = rom[:ROMTOP]
        s.prom = bytes(rom[ROMTOP:])
        s.a=s.b=s.c=s.d=s.e=s.h=s.l=0; s.sp=0; s.pc=0
        s.fs=s.fz=s.fac=s.fp=s.fc=0
        s.ie=0; s.cyc=0; s.halted=0
        s.shift=0; s.shoff=0; s.in0=s.board['in0']; s.in1=s.board['in1']; s.in2=s.board['in2']
        s.vw=0; s.vwchg=0; s.vdirty=set(); s.ios=collections.Counter()
        s.romw=0; s.mirror=0; s.sound=[]; s.inisr=0; s.isrsp=0; s.pchl=set(); s.intpc=set()
        s.lamp1=0; s.lamp2=0
    def rd(s,a):
        a &= 0x7fff
        if a >= 0x6000: s.mirror += 1; a -= 0x4000      # RAM's mirror
        return s.m[a]
    def wr(s,a,v):
        a &= 0x7fff
        if a >= 0x6000: s.mirror += 1; a -= 0x4000
        if a < 0x2000 or a >= 0x4000: s.romw += 1; return
        if a >= 0x2400:
            s.vw += 1
            if s.m[a] != v:
                s.vwchg += 1; o=a-0x2400; s.vdirty.add((o>>8)*32 + (o&31))
        s.m[a] = v
    def f(s):
        return (s.fs<<7)|(s.fz<<6)|(s.fac<<4)|s.fp|2|s.fc
    def setf(s,v):
        s.fs=(v>>7)&1; s.fz=(v>>6)&1; s.fac=(v>>4)&1; s.fp=v&4; s.fc=v&1
    def szp(s,v): s.fs=v>>7; s.fz=1 if v==0 else 0; s.fp=PAR[v]
    def getr(s,r):
        return (s.b,s.c,s.d,s.e,s.h,s.l,None,s.a)[r] if r!=6 else s.rd((s.h<<8)|s.l)
    def setr(s,r,v):
        if r==0: s.b=v
        elif r==1: s.c=v
        elif r==2: s.d=v
        elif r==3: s.e=v
        elif r==4: s.h=v
        elif r==5: s.l=v
        elif r==6: s.wr((s.h<<8)|s.l,v)
        else: s.a=v
    def rp(s,p):
        return ((s.b<<8)|s.c, (s.d<<8)|s.e, (s.h<<8)|s.l, s.sp)[p]
    def srp(s,p,v):
        v&=0xffff
        if p==0: s.b,s.c=v>>8,v&255
        elif p==1: s.d,s.e=v>>8,v&255
        elif p==2: s.h,s.l=v>>8,v&255
        else: s.sp=v
    def push(s,v): s.sp=(s.sp-2)&0xffff; s.wr(s.sp,v&255); s.wr(s.sp+1,v>>8)
    def pop(s): v=s.rd(s.sp)|(s.rd(s.sp+1)<<8); s.sp=(s.sp+2)&0xffff; return v
    def imm8(s): v=s.rd(s.pc); s.pc=(s.pc+1)&0xffff; return v
    def imm16(s): v=s.rd(s.pc)|(s.rd(s.pc+1)<<8); s.pc=(s.pc+2)&0xffff; return v
    def alu(s,op,v):
        a=s.a
        if op==0 or op==1:
            cy = s.fc if op==1 else 0
            r=a+v+cy; s.fac=1 if ((a&15)+(v&15)+cy)>15 else 0; s.fc=r>>8; r&=255; s.a=r; s.szp(r)
        elif op==2 or op==3 or op==7:
            cy = s.fc if op==3 else 0
            r=a-v-cy; s.fac=1 if ((a&15)-(v&15)-cy)>=0 else 0; s.fc=1 if r<0 else 0; r&=255
            s.szp(r)
            if op!=7: s.a=r
        elif op==4: r=a&v; s.fac=1 if ((a|v)&8) else 0; s.fc=0; s.a=r; s.szp(r)
        elif op==5: r=a^v; s.fac=0; s.fc=0; s.a=r; s.szp(r)
        else: r=a|v; s.fac=0; s.fc=0; s.a=r; s.szp(r)
    def cond(s,c):
        return (not s.fz, s.fz, not s.fc, s.fc, not s.fp, s.fp, not s.fs, s.fs)[c]
    def shifted(s):
        return ((s.shift << s.shoff) >> 8) & 255
    def io_in(s,p):
        s.ios['in%d'%p]+=1
        io = s.board['io']
        if io == 'mw':
            if p==0: return s.in0
            if p==1: return s.in1
            if p==2: return s.in2
            if p==3: return s.shifted()
            return 0
        p &= 3                                  # 3 address lines, IN mirrored
        if io == 'gunfight':
            return (s.in0, s.in1, s.in2, s.shifted())[p]
        if p == 0:                              # Sea Wolf: the shifter, reversed
            return int('{:08b}'.format(s.shifted())[::-1], 2)
        return (0, s.in0, s.in1, s.shifted())[p]
    def io_out(s,p,v):
        s.ios['out%d'%p]+=1
        io = s.board['io']
        if io == 'mw':
            if p==2: s.shoff=v&7
            elif p==4: s.shift=(v<<8)|(s.shift>>8)
            elif p in (3,5): s.sound.append((p,v))
        elif io == 'gunfight':                  # no decoder: three AND gates
            p &= 7
            if p & 1: s.sound.append((p,v))
            if p & 2: s.shoff=v&7
            if p & 4: s.shift=(v<<8)|(s.shift>>8)
        else:
            p &= 7
            if p==1: s.lamp1=v
            elif p==2: s.lamp2=v
            elif p==3: s.shift=(v<<8)|(s.shift>>8)
            elif p==4: s.shoff=v&7
            elif p==5: s.sound.append((p,v))
    def interrupt(s,n):
        if not s.ie: return False
        s.ie=0; s.halted=0; s.isrsp=s.sp; s.intpc.add(s.pc); s.push(s.pc); s.pc=n*8; s.cyc+=11; s.inisr=1; return True
    def step(s):
        op=s.imm8(); s.cyc+=CYC[op]
        if 0x40<=op<0x80:
            if op==0x76: s.halted=1; return op
            s.setr((op>>3)&7, s.getr(op&7)); return op
        if 0x80<=op<0xc0:
            s.alu((op>>3)&7, s.getr(op&7)); return op
        x=op
        if x in (0,8,0x10,0x18,0x20,0x28,0x30,0x38): return op
        if x&0xcf==0x01: s.srp(x>>4, s.imm16()); return op
        if x&0xcf==0x03: p=x>>4; s.srp(p, s.rp(p)+1); return op
        if x&0xcf==0x0b: p=x>>4; s.srp(p, s.rp(p)-1); return op
        if x&0xcf==0x09:
            r=s.rp(2)+s.rp(x>>4); s.fc=r>>16; s.srp(2,r); return op
        if x&0xc7==0x04:
            r=(x>>3)&7; v=(s.getr(r)+1)&255; s.fac=1 if v&15==0 else 0; s.setr(r,v); s.szp(v); return op
        if x&0xc7==0x05:
            r=(x>>3)&7; v=(s.getr(r)-1)&255; s.fac=0 if v&15==15 else 1; s.setr(r,v); s.szp(v); return op
        if x&0xc7==0x06: r=(x>>3)&7; s.setr(r, s.imm8()); return op
        if x==0x02: s.wr(s.rp(0),s.a); return op
        if x==0x12: s.wr(s.rp(1),s.a); return op
        if x==0x0a: s.a=s.rd(s.rp(0)); return op
        if x==0x1a: s.a=s.rd(s.rp(1)); return op
        if x==0x22: a=s.imm16(); s.wr(a,s.l); s.wr(a+1,s.h); return op
        if x==0x2a: a=s.imm16(); s.l=s.rd(a); s.h=s.rd(a+1); return op
        if x==0x32: s.wr(s.imm16(),s.a); return op
        if x==0x3a: s.a=s.rd(s.imm16()); return op
        if x==0x07: s.fc=s.a>>7; s.a=((s.a<<1)|s.fc)&255; return op
        if x==0x0f: s.fc=s.a&1; s.a=(s.a>>1)|(s.fc<<7); return op
        if x==0x17: c=s.fc; s.fc=s.a>>7; s.a=((s.a<<1)|c)&255; return op
        if x==0x1f: c=s.fc; s.fc=s.a&1; s.a=(s.a>>1)|(c<<7); return op
        if x==0x27:
            a=s.a; cor=0; c=s.fc
            if (a&15)>9 or s.fac: cor|=6
            if a>0x99 or c or ((a>>4)>=9 and (a&15)>9): cor|=0x60; c=1
            s.alu(0,cor); s.fc=c; return op
        if x==0x2f: s.a^=255; return op
        if x==0x37: s.fc=1; return op
        if x==0x3f: s.fc^=1; return op
        if x&0xc7==0xc0:
            if s.cond((x>>3)&7): s.pc=s.pop(); s.cyc+=6; s.pchl.add(s.pc)
            return op
        if x&0xcf==0xc1:
            v=s.pop(); p=(x>>4)&3
            if p==3: s.a=v>>8; s.setf(v&255)
            else: s.srp(p,v)
            return op
        if x&0xcf==0xc5:
            p=(x>>4)&3
            s.push((s.a<<8)|s.f() if p==3 else s.rp(p)); return op
        if x&0xc7==0xc2:
            a=s.imm16()
            if s.cond((x>>3)&7): s.pc=a
            return op
        if x==0xc3 or x==0xcb: s.pc=s.imm16(); return op
        if x&0xc7==0xc4:
            a=s.imm16()
            if s.cond((x>>3)&7): s.push(s.pc); s.pc=a; s.cyc+=6
            return op
        if x in (0xcd,0xdd,0xed,0xfd): a=s.imm16(); s.push(s.pc); s.pc=a; return op
        if x&0xc7==0xc6: s.alu((x>>3)&7, s.imm8()); return op
        if x&0xc7==0xc7: s.push(s.pc); s.pc=x&0x38; return op
        if x==0xc9 or x==0xd9: s.pc=s.pop(); s.pchl.add(s.pc); return op
        if x==0xd3: s.io_out(s.imm8(), s.a); return op
        if x==0xdb: s.a=s.io_in(s.imm8()); return op
        if x==0xe3:
            v=s.rd(s.sp)|(s.rd(s.sp+1)<<8); s.wr(s.sp,s.l); s.wr(s.sp+1,s.h); s.h,s.l=v>>8,v&255; return op
        if x==0xe9: s.pc=s.rp(2); s.pchl.add(s.pc); return op
        if x==0xeb: s.d,s.e,s.h,s.l=s.h,s.l,s.d,s.e; return op
        if x==0xf3: s.ie=0; return op
        if x==0xfb: s.ie=1; return op
        if x==0xf9: s.sp=s.rp(2); return op
        raise Exception('op %02x' % op)

HALF = 2000000 // 120          # T-states between the two interrupts


def script(fr, board=None):
    """The input ports for frame fr: coin at 300, 1P start at 360, then fire
    and move. A second coin at 3000 and a 2P start at 3060 reach the
    two-player code on the boards that have one. Out (in0, in1, in2)."""
    b = board or BOARDS['invaders']
    if b['io'] == 'gunfight':
        in2 = b['in2'] | (0x40 if 300 <= fr < 305 else 0) | (0x80 if 360 <= fr < 365 else 0)
        in0, in1 = gf_aim(3), gf_aim(3)
        if fr >= 420:
            in0 = gf_aim((fr // 70) % 7) | (0x01, 0x08, 0x02, 0x04)[(fr // 50) % 4]
            in0 |= 0x80 if (fr // 25) % 3 == 0 else 0
            in1 = gf_aim(6 - (fr // 90) % 7) | (0x04, 0x01, 0x08, 0x02)[(fr // 65) % 4]
            in1 |= 0x80 if (fr // 33) % 3 == 0 else 0
        return in0, in1, in2
    if b['io'] == 'seawolf':
        in1 = b['in1'] | (0x01 if 300 <= fr < 305 else 0) | (0x02 if 360 <= fr < 365 else 0)
        pos = 15
        if fr >= 420:
            k = (fr // 4) % 58
            pos = k if k < 30 else 57 - k
        in0 = b['in0'] | sw_scope(pos) | (0x20 if fr >= 420 and (fr // 30) % 2 else 0)
        return in0, in1, b['in2']
    v = b['in1']
    ctl = 0
    if 300 <= fr < 305 or 3000 <= fr < 3005 or 3010 <= fr < 3015:
        v ^= 0x01                               # the coin, either polarity
    if 360 <= fr < 365:
        v |= 0x04
    if 3060 <= fr < 3065:
        v |= 0x02
    if fr >= 420:
        if (fr // 20) % 2:
            ctl |= 0x10
        ctl |= 0x20 if (fr // 90) % 2 else 0x40
    return b['in0'], v | ctl, b['in2'] | (ctl if b['both'] else 0)


def run(m, frames, each=None):
    """Run `frames` frames; each(fr, m, n) after each frame, n instructions."""
    for fr in range(frames):
        m.in0, m.in1, m.in2 = script(fr, m.board)
        n = 0
        c0 = m.cyc
        for half in (1, 2):
            end = c0 + HALF * half
            while m.cyc < end:
                if m.halted:
                    m.cyc = end
                    break
                pc = m.pc
                op = m.step()
                n += 1
                if m.inisr and m.sp == m.isrsp:
                    m.inisr = 0
                if each and each[1]:
                    each[1](m, pc, op, fr)
            m.interrupt(half)
        if each and each[0]:
            each[0](fr, m, n)


def pbm(m, path):
    with open(path, 'w') as f:
        f.write('P1\n224 256\n')
        for y in range(256):
            row = []
            for x in range(224):
                b = m.m[0x2400 + x * 32 + (255 - y) // 8]
                row.append('1' if (b >> ((255 - y) & 7)) & 1 else '0')
            f.write(' '.join(row) + '\n')


def vram_sha1(m):
    return hashlib.sha1(bytes(m.m[0x2400:0x4000])).hexdigest()


def cmd_stats(rom, a):
    m = M(rom, a.board)
    per = []
    isr = [0, 0]
    hot = collections.Counter()
    ops = collections.Counter()
    st = {'vw': 0, 'vc': 0}

    def frame(fr, m, n):
        per.append((fr, n, m.vw - st['vw'], m.vwchg - st['vc'], len(m.vdirty)))
        st['vw'] = m.vw
        st['vc'] = m.vwchg
        m.vdirty = set()

    def instr(m, pc, op, fr):
        if fr >= 420:
            ops[op] += 1
            isr[0 if m.inisr else 1] += 1
            hot[pc] += 1
    run(m, a.frames, (frame, instr))
    play = [p for p in per if p[0] >= 420]
    attract = [p for p in per if 60 <= p[0] < 300]
    for name, rows in (('attract', attract), ('play', play)):
        if not rows:
            continue
        print(name, len(rows), 'frames')
        for i, what in ((1, 'instructions/frame'), (2, 'vram writes/frame '),
                        (3, 'vram changed/frame'), (4, 'dirty 8x8 cells   ')):
            v = sorted(r[i] for r in rows)
            print('  %s avg %.0f p50 %d p90 %d max %d' % (
                what, sum(v) / len(v), v[len(v) // 2], v[len(v) * 9 // 10], v[-1]))
    tot = sum(hot.values())
    if tot:
        print('play: interrupt handlers %.1f%% of instructions' % (100 * isr[0] / tot))
        for pc, n in hot.most_common(12):
            print('  %04x %5.1f%%  op %02x' % (pc, 100 * n / tot, m.m[pc]))
        print('avg T-states/instr in play %.2f' % (
            sum(CYC[o] * n for o, n in ops.items()) / sum(ops.values())))
    print('rom writes', m.romw, 'RAM mirror accesses', m.mirror, 'io', dict(m.ios))
    if a.pbm:
        pbm(m, a.pbm)


def cmd_seeds(rom, a):
    m = M(rom, a.board)
    after = set()                   # the return sites the walk already has

    def instr(m, pc, op, fr):
        if op in (0xcd, 0xdd, 0xed, 0xfd) or (op & 0xc7) in (0xc4, 0xc7):
            after.add(pc + (1 if (op & 0xc7) == 0xc7 else 3))
    run(m, a.frames, (None, instr))
    lines = ['# PCHL and RET targets tools/arcsim.py reached in %d frames of its script.' % a.frames,
             '# Addresses only: no ROM byte. apps/arcade/arcaot.py walks from each.']
    # (an interrupt's return PC is not taken out: one that is ALSO a PCHL
    # target nothing else reaches is a seed - Gun Fight's 03C9)
    lines += ['%04X' % p for p in sorted(m.pchl - after) if is_rom(p) and p < ROMTOP]
    out = '\n'.join(lines) + '\n'
    if a.output:
        open(a.output, 'w').write(out)
    else:
        sys.stdout.write(out)


def cmd_idles(rom, a):
    """IDLE RETURNS (ARCADE-SPEC 3.8): a RET (or a taken Rcc) that hands
    control back to where it came from with nothing changed - the registers
    and the flags, SP, and every RAM byte as it was when the CPU was last at
    the return's target, with no interrupt between - a pass of a wait the
    game's main loop makes through a dispatcher and a task. One whose passes
    are all but always idle (busy no more than 2% of them) and carry 5% or
    more of everything the CPU runs is printed as `IDLE pc`: arcaot.py makes
    its return a HLT resuming at the popped address, and tests/arclock.py
    holds the translation to the model's real spinning."""
    m = M(rom, a.board)
    journal = []
    ow = m.wr

    def wr(adr, v):
        aa = adr & 0x7fff
        if aa >= 0x6000:
            aa -= 0x4000
        if 0x2000 <= aa < 0x4000:
            journal.append((aa, m.m[aa]))
        ow(adr, v)
    m.wr = wr
    ints = [0]
    oi = m.interrupt

    def interrupt(n):
        r = oi(n)
        if r:
            ints[0] += 1
        return r
    m.interrupt = interrupt
    snap = {}
    idle = collections.Counter()
    busy = collections.Counter()
    share = collections.Counter()
    count = [0]

    def regs():
        return (m.a, m.b, m.c, m.d, m.e, m.h, m.l, m.sp, m.fs, m.fz, m.fp, m.fc)

    def instr(mm, pc, op, fr):
        count[0] += 1
        if op == 0xc9 or ((op & 0xc7) == 0xc0 and m.pc != pc + 1):
            sv = snap.get(m.pc)
            if sv and sv[2] == ints[0]:
                first = {}
                for adr, old in journal[sv[1]:]:
                    first.setdefault(adr, old)
                if sv[0] == regs() and all(m.m[adr] == old for adr, old in first.items()):
                    idle[pc] += 1
                    share[pc] += count[0] - sv[3]
                else:
                    busy[pc] += 1
        snap[m.pc] = (regs(), len(journal), ints[0], count[0])
    run(m, a.frames, (None, instr))
    for pc in sorted(idle):
        if busy[pc] <= idle[pc] * 0.02 and share[pc] >= 0.05 * count[0]:
            print('IDLE %04X    # %d idle passes, %d busy, %.0f%% of the run'
                  % (pc, idle[pc], busy[pc], 100.0 * share[pc] / count[0]))


def cmd_digest(rom, a):
    at = sorted(int(x) for x in a.at.split(','))
    m = M(rom, a.board)

    def frame(fr, m, n):
        if fr + 1 in at:
            print('%d %s' % (fr + 1, vram_sha1(m)))
            if a.pbm_prefix:
                pbm(m, '%s%d.pbm' % (a.pbm_prefix, fr + 1))
    run(m, at[-1], (frame, None))


def main():
    ap = argparse.ArgumentParser(description='ARCADE reference model (8080 + Midway board)')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('stats')
    p.add_argument('rom')
    p.add_argument('--frames', type=int, default=1500)
    p.add_argument('--pbm')
    p = sub.add_parser('seeds')
    p.add_argument('rom')
    p.add_argument('--frames', type=int, default=6000)
    p.add_argument('-o', '--output')
    p = sub.add_parser('idles')
    p.add_argument('rom')
    p.add_argument('--frames', type=int, default=1500)
    p = sub.add_parser('digest')
    p.add_argument('rom')
    p.add_argument('--at', required=True)
    p.add_argument('--pbm-prefix')
    for p in sub.choices.values():
        p.add_argument('--board', choices=sorted(BOARDS), default='invaders')
    a = ap.parse_args()
    rom = open(a.rom, 'rb').read()
    if len(rom) != BOARDS[a.board]['size']:
        sys.exit('arcsim: %s is %d bytes, not the %s board image' % (a.rom, len(rom), a.board))
    {'stats': cmd_stats, 'seeds': cmd_seeds, 'digest': cmd_digest,
     'idles': cmd_idles}[a.cmd](rom, a)


if __name__ == '__main__':
    main()

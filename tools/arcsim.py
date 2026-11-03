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
#         after a CALL or an interrupt's return address: the entry points a static walk of the ROM cannot
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

class M:
    def __init__(s, rom):
        s.m = bytearray(0x10000); s.m[0:len(rom)] = rom
        s.a=s.b=s.c=s.d=s.e=s.h=s.l=0; s.sp=0; s.pc=0
        s.fs=s.fz=s.fac=s.fp=s.fc=0
        s.ie=0; s.cyc=0; s.halted=0
        s.shift=0; s.shoff=0; s.in1=0x08; s.in2=0x00
        s.vw=0; s.vwchg=0; s.vdirty=set(); s.ios=collections.Counter()
        s.romw=0; s.sound=[]; s.inisr=0; s.isrsp=0; s.pchl=set(); s.intpc=set()
    def rd(s,a): return s.m[a & 0x3fff]
    def wr(s,a,v):
        a &= 0x3fff
        if a < 0x2000: s.romw += 1; return
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
    def io_in(s,p):
        s.ios['in%d'%p]+=1
        if p==1: return s.in1
        if p==2: return s.in2
        if p==3: return ((s.shift << s.shoff) >> 8) & 255
        return 0
    def io_out(s,p,v):
        s.ios['out%d'%p]+=1
        if p==2: s.shoff=v&7
        elif p==4: s.shift=(v<<8)|(s.shift>>8)
        elif p in (3,5): s.sound.append((p,v))
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


def script(fr):
    """IN 1 for frame fr: coin at 300, 1P start at 360, then fire and move.
    A second coin at 3000 and a 2P start at 3060 reach the two-player code."""
    v = 0x08
    if 300 <= fr < 305 or 3000 <= fr < 3005 or 3010 <= fr < 3015:
        v |= 0x01
    if 360 <= fr < 365:
        v |= 0x04
    if 3060 <= fr < 3065:
        v |= 0x02
    if fr >= 420:
        if (fr // 20) % 2:
            v |= 0x10
        v |= 0x20 if (fr // 90) % 2 else 0x40
    return v


def run(m, frames, each=None):
    """Run `frames` frames; each(fr, m, n) after each frame, n instructions."""
    for fr in range(frames):
        m.in1 = script(fr)
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
    m = M(rom)
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
    print('rom writes', m.romw, 'io', dict(m.ios))
    if a.pbm:
        pbm(m, a.pbm)


def cmd_seeds(rom, a):
    m = M(rom)
    after = set()                   # the return sites the walk already has

    def instr(m, pc, op, fr):
        if op in (0xcd, 0xdd, 0xed, 0xfd) or (op & 0xc7) in (0xc4, 0xc7):
            after.add(pc + (1 if (op & 0xc7) == 0xc7 else 3))
    run(m, a.frames, (None, instr))
    lines = ['# PCHL and RET targets tools/arcsim.py reached in %d frames of its script.' % a.frames,
             '# Addresses only: no ROM byte. apps/arcade/arcaot.py walks from each.']
    lines += ['%04X' % p for p in sorted(m.pchl - after - m.intpc) if p < 0x2000]
    out = '\n'.join(lines) + '\n'
    if a.output:
        open(a.output, 'w').write(out)
    else:
        sys.stdout.write(out)


def cmd_digest(rom, a):
    at = sorted(int(x) for x in a.at.split(','))
    m = M(rom)

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
    p = sub.add_parser('digest')
    p.add_argument('rom')
    p.add_argument('--at', required=True)
    p.add_argument('--pbm-prefix')
    a = ap.parse_args()
    rom = open(a.rom, 'rb').read()
    if len(rom) != 0x2000:
        sys.exit('arcsim: %s is %d bytes, not an 8KB board image' % (a.rom, len(rom)))
    {'stats': cmd_stats, 'seeds': cmd_seeds, 'digest': cmd_digest}[a.cmd](rom, a)


if __name__ == '__main__':
    main()

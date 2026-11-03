#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/arcsim.py  (wave 0 form: a measuring model, not yet the oracle)
#
# An Intel 8080 plus the Midway 8080 B&W board (Space Invaders), in Python.
# It runs the real ROM with a scripted coin, start and play, and prints the
# per-frame load: instructions, VRAM writes, dirty 8x8 cells, the share of
# time in the interrupt handlers, and the hottest PCs. The final frame's
# screen is written as a portrait PBM.
#
#     python3 -I tools/arcsim.py <invaders.zip> [frames=1200] [out.pbm]
#
# The zip is MAME's `invaders` set (docs/plans/ARCADE-EMU-PLAN.md section 9
# pins it). No ROM is committed. These figures are section 2 of that plan.
# Wave 1 grows this into the reference model: traces, frame digests and
# input replay.
# =============================================================================
import sys, zipfile, collections

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
        s.romw=0; s.sound=[]; s.inisr=0; s.isrsp=0
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
        s.ie=0; s.halted=0; s.isrsp=s.sp; s.push(s.pc); s.pc=n*8; s.cyc+=11; s.inisr=1; return True
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
            if s.cond((x>>3)&7): s.pc=s.pop(); s.cyc+=6
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
        if x==0xc9 or x==0xd9: s.pc=s.pop(); return op
        if x==0xd3: s.io_out(s.imm8(), s.a); return op
        if x==0xdb: s.a=s.io_in(s.imm8()); return op
        if x==0xe3:
            v=s.rd(s.sp)|(s.rd(s.sp+1)<<8); s.wr(s.sp,s.l); s.wr(s.sp+1,s.h); s.h,s.l=v>>8,v&255; return op
        if x==0xe9: s.pc=s.rp(2); return op
        if x==0xeb: s.d,s.e,s.h,s.l=s.h,s.l,s.d,s.e; return op
        if x==0xf3: s.ie=0; return op
        if x==0xfb: s.ie=1; return op
        if x==0xf9: s.sp=s.rp(2); return op
        raise Exception('op %02x' % op)

def main():
    z = zipfile.ZipFile(sys.argv[1])
    rom = b''.join(z.read('invaders.' + c) for c in 'hgfe')
    m = M(rom)
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 1200
    HALF = 2000000 // 120
    isr=[0,0]; mainhot=collections.Counter(); hot = collections.Counter(); ops = collections.Counter()
    per = []
    for fr in range(frames):
        # script: coin at frame 300, start at 360, then play: hold fire, wiggle
        m.in1 = 0x08
        if 300 <= fr < 305: m.in1 |= 0x01
        if 360 <= fr < 365: m.in1 |= 0x04
        if fr >= 420:
            if (fr // 20) % 2: m.in1 |= 0x10
            m.in1 |= 0x20 if (fr // 90) % 2 else 0x40
        n0 = 0; c0 = m.cyc; vw0 = m.vw; vc0 = m.vwchg; m.vdirty = set()
        hot0 = collections.Counter()
        for half in (1, 2):
            end = c0 + HALF * half
            while m.cyc < end:
                if m.halted: m.cyc = end; break
                pc = m.pc; op = m.step(); n0 += 1
                if m.inisr and m.sp == m.isrsp: m.inisr = 0
                if fr >= 420:
                    ops[op] += 1
                    if m.inisr: isr[0]+=1
                    else: mainhot[pc]+=1; isr[1]+=1
                    hot[pc] += 1
            m.interrupt(half)
        per.append((fr, n0, m.vw - vw0, m.vwchg - vc0, len(m.vdirty)))
    play = [p for p in per if p[0] >= 420]
    attract = [p for p in per if 60 <= p[0] < 300]
    for name, rows in (('attract', attract), ('play', play)):
        if not rows: continue
        ins = sorted(r[1] for r in rows); vw = sorted(r[2] for r in rows)
        vc = sorted(r[3] for r in rows); dc = sorted(r[4] for r in rows)
        def st(v): return 'avg %.0f p50 %d p90 %d max %d' % (sum(v)/len(v), v[len(v)//2], v[len(v)*9//10], v[-1])
        print(name, len(rows), 'frames')
        print('  instructions/frame', st(ins))
        print('  vram writes/frame ', st(vw))
        print('  vram changed/frame', st(vc))
        print('  dirty 8x8 cells   ', st(dc))
    tot = sum(hot.values())
    print('play: top PCs (share of instructions)')
    for pc, n in hot.most_common(25):
        print('  %04x %5.1f%%  op %02x' % (pc, 100 * n / tot, m.m[pc]))
    print('isr share %.1f%%' % (100*isr[0]/sum(isr)))
    for pc,n in mainhot.most_common(12): print('  main %04x %5.1f%%' % (pc,100*n/sum(isr)))
    print('rom writes', m.romw, 'io', dict(m.ios))
    print('avg cycles/instr in play %.2f' % (sum(CYC[o]*n for o, n in ops.items()) / sum(ops.values())))
    # dump screen of last frame for a sanity check
    with open(sys.argv[3] if len(sys.argv) > 3 else 'screen.pbm', 'w') as f:
        f.write('P1\n224 256\n')
        for y in range(256):
            row = []
            for x in range(224):
                b = m.m[0x2400 + x * 32 + (255 - y) // 8]
                row.append('1' if (b >> ((255 - y) & 7)) & 1 else '0')
            f.write(' '.join(row) + '\n')

main()

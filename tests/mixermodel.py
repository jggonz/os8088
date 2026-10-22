#!/usr/bin/env python3
"""SPEC.md 34.15: shipped mixer code against Creative chapter 4's bit maps.

This is a host CPU-model row, not an audio measurement. The card below is
independent of the driver's descriptors. Run only in the final soak union:
    PYTHONPATH=build/testdeps python3 tools/os88test.py soak -k mixerhost

SS differs from DS. Every mixer I/O must have interrupts masked. Each exposed
source is checked at all 101 percentages, including asymmetric stereo, native
quantization, invalid-write atomicity, omitted channels and exact detach
restoration. Floating/stuck buses must refuse. A deliberately corrupted SB Pro
descriptor must fail, proving the oracle can reject broken driver code.
"""
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX,
    UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI,
    UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES,
    UC_X86_REG_SS, UC_X86_REG_EFLAGS)

ROOT = Path(__file__).resolve().parents[1]
BASE, SEG, STACK, STOP = 0x20000, 0x2000, 0x9000, 0xFF00
REGS = {'ax':UC_X86_REG_AX, 'bx':UC_X86_REG_BX, 'cx':UC_X86_REG_CX,
        'dx':UC_X86_REG_DX, 'si':UC_X86_REG_SI, 'di':UC_X86_REG_DI,
        'bp':UC_X86_REG_BP}
# Explicit masks/locations from Creative's HW programming guide, chapter 4.
# (left register, right or None, native maximum, low-bit position, packed)
CHANNELS = {
    1: [(2,None,7,1,False), (10,None,3,1,False),
        (6,None,7,1,False), (8,None,7,1,False)],
    2: [(0x22,None,7,1,True), (4,None,7,1,True),
        (0x26,None,7,1,True), (0x28,None,7,1,True),
        (0x2E,None,7,1,True), (10,None,3,0,False)],
    3: [(0x30,0x31,31,3,False), (0x32,0x33,31,3,False),
        (0x34,0x35,31,3,False), (0x36,0x37,31,3,False),
        (0x38,0x39,31,3,False), (0x3A,None,31,3,False),
        (0x3B,None,3,6,False), (0x46,0x47,15,4,False),
        (0x44,0x45,15,4,False)]}


def assemble(tmp):
    out, mp = Path(tmp)/'sound.bin', Path(tmp)/'sound.map'
    subprocess.run(['nasm','-f','bin','-w+error','-DPICOMEM',
        '--before',f'[map symbols {mp}]','-I','apps/','-I','drivers/',
        '-I','drivers/sound/','-o',str(out),'drivers/sound/sound.asm'],
        cwd=ROOT,check=True)
    syms={}
    for line in mp.read_text().splitlines():
        m=re.fullmatch(r'\s*[0-9A-F]+\s+([0-9A-F]+)\s+(\S+)\s*',line)
        if m: syms[m[2]]=int(m[1],16)
    return out.read_bytes(),syms


class Card:
    def __init__(self, kind, broken=None):
        self.kind,self.broken=kind,broken
        self.index=0
        self.masks={}
        for l,r,n,shift,packed in CHANNELS.get(kind,[]):
            self.masks[l]=(n<<shift) | ((n<<(shift+4)) if packed else 0)
            if r is not None: self.masks[r]=n<<shift
        self.reg={i:((i*37+23)&m) for i,m in self.masks.items()}
        # Neighboring control state must not be changed by a volume client.
        self.reg.update({0x0C:0x28,0x0E:0x22,0x3C:0x1F,0x3D:0x15,
                         0x3E:0x0B,0x80:4,0x81:2})
        self.initial=dict(self.reg)
        self.writes=[]
        self.mu=None

    def inp(self, uc, port, size, _):
        assert port==0x225 and size==1, f'unexpected read {port:x}/{size}'
        assert not uc.reg_read(UC_X86_REG_EFLAGS)&0x200, 'unprotected mixer read'
        if self.broken=='stuck': return 6
        if not self.kind: return 0xFF
        return self.reg.get(self.index,0xFF)

    def out(self, uc, port, size, value, _):
        assert size==1 and port in (0x224,0x225), 'unexpected port write'
        assert not uc.reg_read(UC_X86_REG_EFLAGS)&0x200, 'unprotected mixer write'
        if port==0x224:
            self.index=value
            return
        self.writes.append((self.index,value))
        if self.kind and not self.broken:
            assert self.index in self.masks, f'wrote control register {self.index:x}'
            self.reg[self.index]=value&self.masks[self.index]


class Driver:
    def __init__(self,image,syms,card):
        self.s,self.card=syms,card
        self.mu=mu=Uc(UC_ARCH_X86,UC_MODE_16)
        mu.mem_map(0,0x100000)
        mu.mem_write(BASE,image)
        mu.mem_write(BASE+STOP,b'\xF4')
        mu.hook_add(UC_HOOK_INSN,card.inp,None,1,0,UC_X86_INS_IN)
        mu.hook_add(UC_HOOK_INSN,card.out,None,1,0,UC_X86_INS_OUT)
        mu.hook_add(UC_HOOK_CODE,lambda u,a,z,d:u.emu_stop() if a==BASE+STOP else None)
        self.put('sbl_base',struct.pack('<H',0x220))
        self.put('sbl_verhi',bytes([card.kind+1 if card.kind else 2]))

    def put(self,name,data): self.mu.mem_write(BASE+self.s[name],data)
    def read(self,name,n=1): return bytes(self.mu.mem_read(BASE+self.s[name],n))
    def call(self,name,**kw):
        mu=self.mu
        for r in (UC_X86_REG_CS,UC_X86_REG_DS): mu.reg_write(r,SEG)
        mu.reg_write(UC_X86_REG_ES,0x60)
        mu.reg_write(UC_X86_REG_SS,STACK)
        mu.reg_write(UC_X86_REG_SP,0xFFEC)
        mu.mem_write(STACK*16+0xFFEC,struct.pack('<H',STOP))
        mu.reg_write(UC_X86_REG_EFLAGS,0x202)
        for k,r in REGS.items(): mu.reg_write(r,kw.get(k,0x7575))
        mu.emu_start(BASE+self.s[name],BASE+0x10000,count=100000)
        assert mu.reg_read(UC_X86_REG_SP)==0xFFEE, f'{name}: unbalanced stack'
        assert mu.reg_read(UC_X86_REG_EFLAGS)&0x200, f'{name}: IF not restored'
        out={k:mu.reg_read(r) for k,r in REGS.items()}
        out['cf']=mu.reg_read(UC_X86_REG_EFLAGS)&1
        for k in ('si','di','bp'):
            assert out[k]==kw.get(k,0x7575),f'{name} clobbers {k}'
        return out


def exercise(image,syms,kind,verbose=True):
    c=Card(kind);d=Driver(image,syms,c)
    d.call('mix_probe')
    assert c.reg==c.initial, 'probe changed mixer state'
    a=d.call('mix_info')
    assert not a['cf'] and a['ax']==kind
    assert a['cx']==(1<<len(CHANNELS[kind]))-1
    for ch,(l,r,n,shift,packed) in enumerate(CHANNELS[kind]):
        stereo=r is not None or packed
        for left in range(101):
            right=100-left
            before=dict(c.reg)
            a=d.call('mix_set',ax=ch,cx=left|(right<<8))
            assert not a['cf']
            ln=(left*n+50)//100;rn=(right*n+50)//100
            expected=ln<<shift
            if packed: expected=(ln<<(shift+4))|(rn<<shift)
            assert c.reg[l]==expected,(kind,ch,left,c.reg[l],expected)
            if r is not None: assert c.reg[r]==rn<<shift
            for reg in before:
                if reg not in (l,r): assert c.reg[reg]==before[reg],f'changed neighbor {reg:x}'
            a=d.call('mix_get',ax=ch)
            lp=(ln*100+n//2)//n
            rp=(rn*100+n//2)//n if stereo else lp
            assert not a['cf'] and a['cx']==lp|(rp<<8),(kind,ch,left,a)
            assert a['dx']==(n<<8)|(int(stereo))|(2 if ch>=7 else 0)
        for cx in (101,101<<8,0xFFFF):
            before=dict(c.reg);wc=len(c.writes)
            a=d.call('mix_set',ax=ch,cx=cx)
            assert a['cf'] and a['ax']==8 and c.reg==before and len(c.writes)==wc
    for ch in range(len(CHANNELS[kind]),256):
        before=dict(c.reg);wc=len(c.writes)
        for fn in ('mix_get','mix_set'):
            a=d.call(fn,ax=ch,cx=0x3232)
            assert a['cf'] and a['ax']==8
        assert c.reg==before and len(c.writes)==wc
    d.call('mix_detach')
    assert c.reg==c.initial,'detach failed to restore original registers'
    assert d.call('mix_info')['cf']
    if verbose: print(f'  CT{(1335,1345,1745)[kind-1]}: all sources, percentages, refusals, IF/stack, restore',flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='os88-mixer-') as tmp:
        image,syms=assemble(tmp)
        for k in (1,2,3): exercise(image,syms,k)
        for broken in (None,'stuck'):
            c=Card(0,broken);d=Driver(image,syms,c)
            d.call('mix_probe')
            assert d.read('mix_kind')==b'\0'
            for fn in ('mix_info','mix_get','mix_set'):
                a=d.call(fn,ax=0,cx=0x6464)
                assert a['cf'] and a['ax']==7
            assert c.reg==c.initial
        # Wrong register: an accidental write into filter/stereo control must
        # be caught by the independent card map, rather than pass as a value.
        bad=bytearray(image);bad[syms['mix_1345']]=0x0E
        try: exercise(bytes(bad),syms,2,False)
        except AssertionError: print('  negative control: bad SB Pro register rejected')
        else: raise AssertionError('negative control passed')
    print('mixerhost: PASS')

if __name__=='__main__': main()

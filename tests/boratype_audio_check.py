#!/usr/bin/env python3
"""Boratype real8086 voice bridge, malformed packs, score length checks.
Host test only: OS stream service replaced by an observable grant/ring sink;
this is not evidence of audible PCM or a real Sound Blaster guest run.
"""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import os88btvoice as voice
import os88gkmus as music
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_CODE
from unicorn.x86_const import *

def pack():
    clips=[(0,1,2048),(1,2,6144)]
    off=16+56*len(clips);rows=b'';pcm=b''
    for ident,tag,n in clips:
        rows+=struct.pack('<BBBBHH48s',ident,tag,1,1,off,n,b'test reaction')
        pcm+=bytes((i*17+ident)%256 for i in range(n));off+=n
    data=bytearray(struct.pack('<4sBBHHH4s',b'BTV1',len(clips),0,8000,off,0,b'\0'*4)+rows+pcm)
    struct.pack_into('<H',data,10,voice.checksum(data));return bytes(data)

def checked_bad(data):
    try:voice.check(data)
    except voice.VoiceError:return
    raise AssertionError('bad pack accepted by host reader')

class Machine:
 def __init__(self,blob):
    self.u=Uc(UC_ARCH_X86,UC_MODE_16);self.u.mem_map(0,0x100000)
    self.u.mem_write(0x20000,blob);self.syms=struct.unpack_from('<10H',blob)
    self.calls=[];self.staged=bytearray(4096);self.consumed=0;self.fail='';self.live=False
    self.u.hook_add(UC_HOOK_INTR,self.intr)
    self.u.hook_add(UC_HOOK_CODE,self.code)
 def intr(self,u,number,_):
    self.calls.append(number)
    if number==0xf0:
        u.reg_write(UC_X86_REG_AX,0 if self.fail=='caps' else 5)
        u.reg_write(UC_X86_REG_BX,1)
    elif number==0xf1:
        ax=u.reg_read(UC_X86_REG_AX);v=ax&255
        self.calls.append((v,u.reg_read(UC_X86_REG_CX)))
        if v==7:
            if ax&0xff00:self.live=False
            else:u.reg_write(UC_X86_REG_SI,0x800)
        if v==6:
            n=u.reg_read(UC_X86_REG_CX);dest=u.reg_read(UC_X86_REG_DI)-0x800
            src=u.reg_read(UC_X86_REG_DS)*16+u.reg_read(UC_X86_REG_SI)
            assert 0<=dest<=4096-n
            self.staged[dest:dest+n]=u.mem_read(src,n)
        u.reg_write(UC_X86_REG_AX,0)
        if v==0:
            self.live=True;u.reg_write(UC_X86_REG_AX,0x100)
        if v==2:self.live=False
        if v==3:u.reg_write(UC_X86_REG_DX,self.consumed)
        if self.fail==str(v):
            u.reg_write(UC_X86_REG_AX,4)
            u.reg_write(UC_X86_REG_EFLAGS,u.reg_read(UC_X86_REG_EFLAGS)|1)
            return
    else:raise AssertionError(f'unexpected interrupt{number}')
    u.reg_write(UC_X86_REG_EFLAGS,u.reg_read(UC_X86_REG_EFLAGS)&~1)
 def code(self,u,address,size,_):
    if address==0x2ff00:u.emu_stop()
 def call(self,index,*args):
    u=self.u;u.reg_write(UC_X86_REG_CS,0x2000);u.reg_write(UC_X86_REG_DS,0x2000)
    u.reg_write(UC_X86_REG_ES,0x60);u.reg_write(UC_X86_REG_SS,0x800)
    u.reg_write(UC_X86_REG_SP,0xf000);u.reg_write(UC_X86_REG_BP,0xaabb)
    u.mem_write(0x17000,struct.pack('<'+'H'*(len(args)+1),0xff00,*args))
    u.emu_start(0x20000+self.syms[index],0x2ff01,count=500000)
    assert u.reg_read(UC_X86_REG_IP)==0xff00
    assert u.reg_read(UC_X86_REG_DS)==0x2000 and u.reg_read(UC_X86_REG_ES)==0x60
    assert u.reg_read(UC_X86_REG_BP)==0xaabb
    return u.reg_read(UC_X86_REG_AX)
 def data(self,data):self.u.mem_write(0x40000,data)

def main():
 good=pack();voice.check(good)
 with tempfile.TemporaryDirectory() as temp:
    temp=Path(temp)
    asm=temp/'voice.asm';binary=temp/'voice.bin'
    asm.write_text('''cpu 8086
%include "os88api.inc"
%xdefine OSAPI_SND_CAPS fake_caps
%xdefine OSAPI_SND_STREAM fake_stream
GKM_VSZ equ 14
section .text
 dw _bt_vvalidate,_bt_vplay,_bt_vtick,_bt_vstop,_bt_vset,_bt_mhold,btv_live,btv_granted,_bt_vmove,btv_seg
%include "boratype/bt_voice.inc"
fake_caps: int 0xf0
 ret
fake_stream: int 0xf1
 ret
_gkm_play: ret
_gkm_stop: ret
gkm_fxend: ret
section .bss
gkm_v: resb 42
gkm_cur: resw 1
gkm_loop: resb 1
section .text
''')
    subprocess.run(['nasm','-f','bin','-w+error','-I',str(ROOT/'apps')+'/',
                    '-o',str(binary),str(asm)],check=True)
    machine=Machine(binary.read_bytes());machine.data(good)
    assert machine.call(0,0x4000,len(good))==1
    malformed=[]
    for pos in (0,2,4,5,7,8,10,12,16,17,18,19,20,22,24):
        bad=bytearray(good);bad[pos]^=255
        # Recompute integrity to exercise structure, not just checksum.
        if pos!=10:struct.pack_into('<H',bad,10,voice.checksum(bad))
        malformed.append(bytes(bad))
    malformed.extend((good[:-1],good+b'x',b'',good[:20]))
    for data in malformed:
        checked_bad(data);machine.data(data or b'\0');assert machine.call(0,0x4000,len(data))==0
    machine.data(good);machine.call(4,0x4000)
    assert machine.call(1,10)==0 # no event, no grant
    assert machine.call(1,2)==1
    assert machine.live and machine.staged==good[16+112+2048:16+112+2048+4096]
    assert machine.call(1,1)==0 # coalesce while active
    # A compaction changes the immutable source base, never the ring's
    # published bytes or logical offsets. Remove the old source to prove
    # the following refill reads the relocated block.
    machine.u.mem_write(0x50000,good)
    machine.call(8,0x5000)
    assert machine.u.mem_read(0x20000+machine.syms[9],2)==b'\x00\x50'
    machine.u.mem_write(0x40000,bytes(len(good)))
    machine.consumed=2048;assert machine.call(2)==1
    assert machine.staged[:2048]==good[-2048:]
    machine.consumed=6144;assert machine.call(2)==0
    assert not machine.live and machine.u.mem_read(0x20000+machine.syms[7],1)==b'\0'
    for failure in ('caps','7','6','0'):
        machine=Machine(binary.read_bytes());machine.data(good);machine.call(4,0x4000);machine.fail=failure
        assert machine.call(1,1)==0
        assert machine.u.mem_read(0x20000+machine.syms[7],1)==b'\0'
    machine=Machine(binary.read_bytes());machine.data(good);machine.call(4,0x4000)
    assert machine.call(1,2)==1;machine.fail='3';assert machine.call(2)==0;assert not machine.live
    # Structural attack must really be detected; verify oracle mutation.
    assert voice.check(good)[0]['id']==0
    music.compile_music(ROOT/'apps/boratype/art/music.txt')
    music.compile_sfx(ROOT/'apps/boratype/art/sfx.txt')
 print('boratype audio: host+8086 readers reject19malformedpacks; real8086 stage/feed/finish/ownership/failure contracts pass')
if __name__=='__main__':main()

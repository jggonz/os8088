#!/usr/bin/env python3
"""Actual instruction checks for provider suspend/rearm and REAL glyph borrowing.

Use the built normal/mega module bytes and assemble the actual HIBER/FSX
source fragments with deterministic font-selection/segment-service stubs.
This cheaply isolates header corruption, lost owner stamps, stale compose
state and bracket-only bounds. OS boots remain separate registered rows.
Each corruption guard has a deliberate negative control.
"""
from pathlib import Path
import re, struct, subprocess, tempfile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn import x86_const as X
ROOT=Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0,str(ROOT/"tools"))
import os88build
KS,MS,SS=0x60,0x7000,0x6000
R=[X.UC_X86_REG_AX,X.UC_X86_REG_BX,X.UC_X86_REG_CX,X.UC_X86_REG_DX,X.UC_X86_REG_SI,X.UC_X86_REG_DI,X.UC_X86_REG_BP,X.UC_X86_REG_DS,X.UC_X86_REG_ES]
def w(x):return struct.pack('<H',x)
class Image:
 def __init__(self,kind):
  b=Path(os88build.at('build/'+kind if kind else 'build')).resolve(); t=(b/'kernel.map').read_text();self.image=(b/'kernel-full.bin').read_bytes()
  self.sym={m[3]:int(m[2],16) for m in re.finditer(r'^\s*([0-9A-F]+)\s+([0-9A-F]+)\s+(\S+)$',t,re.M)}
  self.eq={m[2]:int(m[1],16) for m in re.finditer(r'^([0-9A-F]+)\s+(\S+)$',t,re.M)}
  self.sec={m[4]:(int(m[1],16),int(m[2],16),int(m[3],16)) for m in re.finditer(r'^\s*([0-9A-F]+)\s+([0-9A-F]+)\s+[0-9A-F]+\s+([0-9A-F]+)\s+(?:progbits|nobits)\s+(\S+)$',t,re.M)}
 def v(self,n):return self.sym[n]
def setup(k,seg=0x4000,base=0):
 u=Uc(UC_ARCH_X86,UC_MODE_16);u.mem_map(0,0x110000)
 for name,load in (('.text',KS*16),('.modq',MS*16)):
  va,off,n=k.sec[name];u.mem_write(load+va,k.image[off:off+n])
 def kb(n,data):u.mem_write(KS*16+k.v(n),data)
 def mb(n,data):u.mem_write(MS*16+k.v(n),data)
 kb('font_seg',w(seg));kb('font_base',w(base));kb('font_last',b'\x7e\x5e');kb('gfx_lock_flag',b'\x01');kb('gfx_lock_own',b'\x00');kb('sch_cur',b'\x00')
 kb('KLFP',struct.pack('<HHHH',k.v('klm_scan'),MS,k.v('klm_control'),MS))
 original=bytes((i*29+7)&255 for i in range(760));u.mem_write(seg*16+base,original)
 u.mem_write(0x417,b'\x42\x40');u.mem_write(0x496,b'\x12')
 u.mem_write(0xa0000,b'\xf4')
 return u,kb,mb,original
def invoke(u,seg,off,ax=0,dx=0,es=MS,near=False):
 vals=[ax,0x1234,0x2345,dx,0x4567,0x5678,0x6789,KS,es]
 for r,v in zip(R,vals):u.reg_write(r,v)
 for r,v in ((X.UC_X86_REG_CS,seg),(X.UC_X86_REG_SS,SS),(X.UC_X86_REG_SP,0xff00-(2 if near else 4)),(X.UC_X86_REG_EFLAGS,0x202)):u.reg_write(r,v)
 if near:
  u.mem_write(SS*16+0xfefe,w(0xfff0));u.mem_write(seg*16+0xfff0,b'\xf4');end=seg*16+0xfff0
 else:u.mem_write(SS*16+0xfefc,struct.pack('<HH',0,0xa000));end=0xa0000
 u.emu_start(seg*16+off,end,count=50000)
 assert u.reg_read(X.UC_X86_REG_SP)==0xff00,'unbalanced stack'
 assert u.reg_read(X.UC_X86_REG_CS)==(seg if near else 0xa000),'did not return'
 return u.reg_read(X.UC_X86_REG_AX)
def assemble(k,body,extra=''):
 names=['font_seg','font_base','font_last','KLFP']
 text='cpu 8086\nbits 16\norg 0\nFONT_FIRST equ 32\nFONT_LAST equ 126\nFONT_BYTES equ 760\nKL_CMD equ 4\nKLC_SUSPEND equ 6\nKLC_REARM equ 7\n'
 text+='\n'.join(f'{n} equ {k.v(n)}' for n in names)+'\n'+extra+'\nentry:\n'+body+'\nretf\n'
 with tempfile.TemporaryDirectory() as d:
  p=Path(d);(p/'t.asm').write_text(text);subprocess.run(['nasm','-f','bin','-w+error','-o',str(p/'t.bin'),str(p/'t.asm')],check=True)
  return (p/'t.bin').read_bytes()
def control(k,u,verb,owner=0):return invoke(u,MS,k.v('klm_control'),verb,owner<<8)
def hibernate(k):
 raw=(ROOT/'kernel/hiber.inc').read_text();block=raw[raw.index('    mov al, KLC_SUSPEND\n'):];block=block[:block.index('    mov byte [hb_mode]')].replace('%endif\n','%endif\n',1)
 # The selected block ends the outer BAKED_FONT conditional opened above it.
 block='%ifndef BAKED_FONT\n'+block
 for layout in (0,2):
  for original_seg,original_base in ((0x4000,0),(0xf000,0x200)):
   u,kb,mb,old=setup(k,original_seg,original_base)
   control(k,u,0x0201 if layout else 2)
   holds=bytes(u.mem_read(MS*16+k.v('klm_clients'),3));chosen=bytes(u.mem_read(MS*16+k.v('klm_layout'),1))
   imm=bytes(u.mem_read(MS*16,k.v('klf_table')));ext=bytes(u.mem_read(MS*16+k.v('klf_table')+768,1024))
   mb('klm_dead',b'\xef');mb('klm_prefix',b'\x01')
   newseg,newbase=0xe000,0x500;fresh=bytes((i*11+3)&255 for i in range(760));u.mem_write(newseg*16+newbase,fresh)
   macro=f'%macro FONT_PICK 1\n mov dx,{newseg}\n mov bp,{newbase}\n%endmacro\n'
   code=assemble(k,block,macro);u.mem_write(0x50000,code);invoke(u,0x5000,0)
   assert bytes(u.mem_read(MS*16,k.v('klf_table')))==imm,'module code/header overwritten'
   assert bytes(u.mem_read(MS*16+k.v('klf_table'),760))==fresh,'ASCII not refreshed'
   assert bytes(u.mem_read(MS*16+k.v('klf_table')+768,1024))==ext,'extended glyphs changed'
   assert bytes(u.mem_read(MS*16+k.v('klm_clients'),3))==holds and bytes(u.mem_read(MS*16+k.v('klm_layout'),1))==chosen,'holds/layout changed'
   assert bytes(u.mem_read(MS*16+k.v('klm_dead'),1))==b'\0' and bytes(u.mem_read(MS*16+k.v('klm_prefix'),1))==b'\0','compose/prefix survived'
   assert bytes(u.mem_read(MS*16+k.v('klm_mods'),1))==b'\x42','BDA modifiers not seeded'
   assert bytes(u.mem_read(KS*16+k.v('font_last'),2))==b'\xff\xdf'
   assert bytes(u.mem_read(MS*16+k.v('klf_active'),1))==b'\x01'
 # Negative control: omit suspend and trap the old heap repair's header write.
 u,kb,mb,old=setup(k);control(k,u,0x0201)
 bad=block.replace('    call far [KLFP+KL_CMD]      ; original face for repair; no load/drop','    ; missing suspend')
 u.mem_write(0xe0500,bytes(760));u.mem_write(0x50000,assemble(k,bad,'%macro FONT_PICK 1\n mov dx,0xe000\n mov bp,0x500\n%endmacro'))
 def guard(uc,access,address,size,value,_):
  if MS*16<=address<MS*16+18:raise AssertionError('header corruption')
 u.hook_add(UC_HOOK_MEM_WRITE,guard)
 try:invoke(u,0x5000,0)
 except AssertionError as e:assert 'header corruption' in str(e),e
 else:raise AssertionError('negative missing suspend not detected')
 print('HIBER actual repair: explicit/glyph-only x heap/ROM PASS; header-corruption negative PASS')
def owner(k,broken=False):
 for who in ((0,) if broken else (0,1,0xff)):
  u,kb,mb,_=setup(k);va,off,n=k.sec['.cold'];payload=bytearray(k.image[off:off+n]);v=lambda name:k.v(name)-va
  payload[v('mod_need'):v('mod_need')+2]=b'\xf8\xc3'
  if broken:
   begin=payload.index(b'\x50\x52\x8b\x16',v('kl_control_x.load'));end=payload.index(b'\x5a\x58',begin)
   assert end<v('kl_control_x.call');payload[begin+1]=0x90;payload[end]=0x90
  u.mem_write(0x80000+va,bytes(payload))
  # Real-alias adapter returns a paragraph whose DH differs from every owner.
  cseg=k.eq['COLD_SEG'];u.mem_write(cseg*16+k.v('osapi_seg'),b'\xba\x00\x9b\xf8\xcb')
  kb('mod_r_keyboard',w(MS));invoke(u,0x8000,k.v('kl_control_x'),2,who<<8)
  expected=0 if who==255 else 1<<who
  assert int.from_bytes(u.mem_read(MS*16+k.v('klm_clients'),2),'little')==expected,'HOLD owner lost across SEG_REAL'
  assert bytes(u.mem_read(MS*16+k.v('klm_builtin'),1))==bytes([int(who==255)]),'built-in owner lost'
 print('Mega actual kl_control_x: HOLD owner0/1/privateFF survives SEG_REAL PASS')
def builtins(k):
 for layout in (0,2):
  u,kb,mb,_=setup(k)
  if layout:control(k,u,0x0201)
  control(k,u,2,255)
  target=k.eq['COLD_SEG']*16+k.v('kl_drop_x');u.mem_write(target,b'\x31\xc0\xcb');drops=[]
  def trap(uc,address,size,_):
   if address==target:drops.append(address)
  u.hook_add(UC_HOOK_CODE,trap)
  control(k,u,3,255)
  assert bytes(u.mem_read(MS*16+k.v('klm_builtin'),1))==b'\0'
  assert bytes(u.mem_read(MS*16+k.v('klf_active'),1))==bytes([int(bool(layout))]),'built-in release entered suspend'
  assert len(drops)==int(not layout),'built-in final release missed drop'
 print('Private built-in release: explicit layout retained / last glyph-only dropped PASS')
def fsx(k):
 text=(ROOT/'kernel/fsx.inc').read_text();extra='\n'.join(f'{n} equ {k.v(n)}' for n in ('fsx_rfont','fsx_rfbase','fsx_rmsseg'))+'\nrms_lo equ 0x2000\nMEGA_SEL_CODE equ 0x9000\nmega_ishif equ 0\n'
 size=text[text.index("    xor dx, dx                  ; ...and THE GLYPH TABLE'S COPY last"):text.index('; fsx_rxrows -')]
 copy=text[text.index('    mov di, [ss:fsx_rfont]'):text.index('; fsx_rsvcsz -')];copy=copy[:copy.index('.nf:')]+'.nf:\n'
 drop=text[text.index('    mov ax, [ss:fsx_rfont]      ; THE GLYPH TABLE'):];drop=drop[:drop.index('%ifdef VID_EXT')]
 up=text[text.index('    mov ax, [ss:fsx_rfont]      ; even copy offset',text.index('fsx_rupf:')):];up=up[:up.index('    mov byte [fsx_rmode]')]
 for marker in (0,0x200,0xffff):
  u,kb,mb,original=setup(k);u.mem_write(SS*16+k.v('fsx_rfont'),w(marker));u.mem_write(SS*16+k.v('fsx_rmsseg'),w(0x5500));kb('font_base',w(0x300));kb('font_seg',w(0x4000));u.mem_write(0x40300,original)
  if marker==0xffff:kb('font_last',b'\xff\xdf');kb('font_seg',w(MS));kb('font_base',w(k.v('klf_table')));u.mem_write(MS*16+k.v('klf_table'),original)
  before=bytes(u.mem_read(0x55000,0x1000));u.mem_write(0x50000,assemble(k,copy,extra));invoke(u,0x5000,0,es=0x5500)
  if marker in (0,0xffff):assert bytes(u.mem_read(0x55000,0x1000))==before,'copied sentinel/default'
  else:assert bytes(u.mem_read(0x55200,760))==original
  base=int.from_bytes(u.mem_read(KS*16+k.v('font_base'),2),'little');seg=int.from_bytes(u.mem_read(KS*16+k.v('font_seg'),2),'little')
  u.mem_write(0x51000,assemble(k,drop,extra));invoke(u,0x5100,0)
  if marker==0xffff:
   assert bytes(u.mem_read(KS*16+k.v('font_last'),2))==b'\x7e\x5e'
   kb('KLFP',struct.pack('<HHHH',0,0,0,0x1337));invoke(u,KS,k.v('osapi_font_glyphs'),near=True)
   assert u.reg_read(X.UC_X86_REG_DX)==MS and u.reg_read(X.UC_X86_REG_SI)==base,'RM FONT_GLYPHS followed protected selector'
  elif marker:assert int.from_bytes(u.mem_read(KS*16+k.v('font_base'),2),'little')==0x2200, (hex(marker),bytes(u.mem_read(KS*16+k.v('font_base'),2)).hex(),bytes(u.mem_read(SS*16+k.v('fsx_rfont'),2)).hex())
  else:assert int.from_bytes(u.mem_read(KS*16+k.v('font_base'),2),'little')==base
  # The actual selector walk restores font_seg before this actual return tail.
  kb('font_seg',w(seg));u.mem_write(0x52000,assemble(k,up,extra));invoke(u,0x5200,0)
  assert int.from_bytes(u.mem_read(KS*16+k.v('font_base'),2),'little')==base,'return lost font base'
  assert bytes(u.mem_read(KS*16+k.v('font_last'),2))==(b'\xff\xdf' if marker==0xffff else b'\x7e\x5e')
 for active,pool in ((False,False),(False,True),(True,False)):
  u,kb,mb,_=setup(k);kb('font_last',b'\xff\xdf' if active else b'\x7e\x5e');u.mem_write(0x90000,b'\xf8\xcb' if pool else b'\xf9\xcb');u.mem_write(0x50000,assemble(k,size,extra));invoke(u,0x5000,0,ax=0x1201,near=True)
  got=int.from_bytes(u.mem_read(SS*16+k.v('fsx_rfont'),2),'little');assert got==(0xffff if active else (0x1202 if pool else 0)),hex(got)
 u,kb,mb,_=setup(k);control(k,u,0x0201);u.mem_write(SS*16+k.v('fsx_rfont'),w(0xffff));kb('KLFP',struct.pack('<HHHH',0,0,0,0x1337))
 bad=drop.replace('    mov word [font_last], ((FONT_LAST-FONT_FIRST) << 8) | FONT_LAST','    nop ; missing ASCII clamp')
 u.mem_write(0x50000,assemble(k,bad,extra));invoke(u,0x5000,0);invoke(u,KS,k.v('osapi_font_glyphs'),near=True)
 assert (u.reg_read(X.UC_X86_REG_DX),u.reg_read(X.UC_X86_REG_SI))!=(MS,k.v('klf_table')),'missing clamp was not detected'
 print('FSX actual size/copy/drop/return: default/pool/provider PASS; missing-clamp negative PASS')
if __name__=='__main__':
 hibernate(Image(''));owner(Image('megak'));builtins(Image(''));builtins(Image('megak'));fsx(Image('megak'))
 try:owner(Image('megak'),True)
 except AssertionError as e:assert 'owner lost' in str(e),e;print('Mega missing DX-save negative control PASS')
 else:raise AssertionError('missing owner save negative control not detected')

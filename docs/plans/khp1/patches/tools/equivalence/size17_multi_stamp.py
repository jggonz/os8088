import random
from unicorn import *
from unicorn.x86_const import *
def run(code,di,dx,drv,sig,si):
    mu=Uc(UC_ARCH_X86,UC_MODE_16)
    mu.mem_map(0,0x20000)
    CS=0x1000
    mu.mem_write(CS*16,code)
    mu.mem_write(0x800,bytes([drv,0])+sig.to_bytes(2,'little'))
    mu.mem_write(0x1000,b'\xAA'*400)
    for r,v in ((UC_X86_REG_DS,0),(UC_X86_REG_ES,0x1800),(UC_X86_REG_SS,0x0100),(UC_X86_REG_SP,0x800),
                (UC_X86_REG_DI,di),(UC_X86_REG_DX,dx),(UC_X86_REG_SI,si),(UC_X86_REG_CS,CS)):
        mu.reg_write(r,v)
    mu.emu_start(CS*16,CS*16+len(code)-1)
    return bytes(mu.mem_read(0x1000,400)), mu.reg_read(UC_X86_REG_SI), mu.reg_read(UC_X86_REG_DX), mu.reg_read(UC_X86_REG_SP)
o=open('old.bin','rb').read(); n=open('new.bin','rb').read()
bad=0
for t in range(3000):
    di=random.randrange(0,65536); dx=random.randrange(10,9*20); drv=random.randrange(0,0x80); sig=random.randrange(65536)
    a=run(o,di,dx,drv,sig,0x1000); b=run(n,di,dx,drv,sig,0x1000)
    if a!=b: bad+=1
print('mismatches',bad,'of 3000')

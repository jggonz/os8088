#!/usr/bin/env python3
"""Run the shipped keyboard module's raw entry against independent scan vectors.

The built .modq image runs at a foreign segment under Unicorn. No generator
or host keyboard supplies expected characters. Every input checks register
preservation and stack bounds; the BIOS ring is inspected word for word.
Negative controls corrupt French Q and the Spain/Latin-American distinction.
"""
from pathlib import Path
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn import x86_const as X
from keymap import Kernel

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/'tools'))
import os88build
RAW_PEAK_STACK = 0
KSEG, MSEG, SSEG = 0x60, 0x7000, 0x6000
RET = 0xfff0
REG = [X.UC_X86_REG_AX, X.UC_X86_REG_BX, X.UC_X86_REG_CX,
       X.UC_X86_REG_DX, X.UC_X86_REG_SI, X.UC_X86_REG_DI,
       X.UC_X86_REG_BP, X.UC_X86_REG_DS, X.UC_X86_REG_ES]
SCANS = (list(range(2,14)), list(range(16,28)),
         list(range(30,41)), list(range(44,54)))
# Character facts authored separately from tools/keyboardmaps.py. Dead keys
# are tested below as sequences rather than pretending they emit a byte.
ROWS = {
 1: ('1234567890-=', 'qwertyuiop[]', "asdfghjkl;'", 'zxcvbnm,./'),
 2: ("1234567890'¡", 'qwertyuiop +', 'asdfghjklñ ', 'zxcvbnm,.-'),
 3: ("1234567890'¿", 'qwertyuiop +', 'asdfghjklñ{', 'zxcvbnm,.-'),
 4: ('&é"\'(-è_çà)=', 'azertyuiop $', 'qsdfghjklmù', 'wxcvbn,;:!')}


SHIFT_ROWS = {
 1: ('!@#$%^&*()_+', 'QWERTYUIOP{}', 'ASDFGHJKL:"', 'ZXCVBNM<>?'),
 2: ('!"·$%&/()=?¿', 'QWERTYUIOP *', 'ASDFGHJKLÑ ', 'ZXCVBNM;:_'),
 3: ('!"#$%&/()=?¡', 'QWERTYUIOP *', 'ASDFGHJKLÑ[', 'ZXCVBNM;:_'),
 4: ('1234567890°+', 'AZERTYUIOP £', 'QSDFGHJKLM%', 'WXCVBN?./§')}
ALTGR = {2: {0x29:'\\',2:'|',3:'@',4:'#',7:'¬',0x1a:'[',0x1b:']',0x28:'{',0x2b:'}'},
         3: {0x29:'¬',0x0c:'\\',0x10:'@',0x1b:'~'},
         4: {4:'#',5:'{',6:'[',7:'|',9:'\\',10:'^',11:'@',12:']',13:'}',0x1b:'¤'}}


def require(ok, why):
    if not ok:
        raise AssertionError(why)


class Machine:
    def __init__(self, k, layout=1, patch=None):
        self.k = k
        self.u = u = Uc(UC_ARCH_X86, UC_MODE_16)
        u.mem_map(0, 0x110000)
        start, n = k.sec['.text']
        u.mem_write(KSEG*16, k.image[start:start+n])
        start, n = k.sec['.modq']
        payload = bytearray(k.image[start:start+n])
        if patch:
            off, value = patch
            payload[off] = value
        u.mem_write(MSEG*16, bytes(payload))
        u.mem_write(MSEG*16+RET, b'\xf4')
        self.mb('klm_layout', layout)
        self.mb('klm_focus', 255)
        self.ring()
        self.low = 0xff00
        self.insns = 0
        def trace(uc, address, size, data):
            self.low = min(self.low, uc.reg_read(X.UC_X86_REG_SP))
            self.insns += 1
        u.hook_add(UC_HOOK_CODE, trace)

    def mb(self, name, value):
        self.u.mem_write(MSEG*16+self.k.v(name), bytes([value]))

    def kw(self, name, data):
        self.u.mem_write(KSEG*16+self.k.v(name), bytes(data))

    def ring(self, head=0x1e, tail=0x1e):
        self.u.mem_write(0x41a, struct.pack('<HH', head, tail))
        self.u.mem_write(0x41e, b'\xcc'*32)

    def words(self, head=0x1e):
        tail = int.from_bytes(self.u.mem_read(0x41c,2), 'little')
        out=[]
        while head != tail and len(out)<16:
            out.append(int.from_bytes(self.u.mem_read(0x400+head,2),'little'))
            head = 0x1e if head==0x3c else head+2
        return out

    def feed(self, scan):
        global RAW_PEAK_STACK
        u=self.u
        physical=bytes(u.mem_read(KSEG*16+self.k.v('kbd_dnmap'),16))
        values=[0xa500|scan,0x1234,0x2345,0x3456,0x4567,0x5678,0x6789,KSEG,0x4000]
        for reg,val in zip(REG, values):
            u.reg_write(reg,val)
        u.reg_write(X.UC_X86_REG_CS,MSEG)
        u.reg_write(X.UC_X86_REG_SS,SSEG)
        u.reg_write(X.UC_X86_REG_SP,0xfefc)
        u.reg_write(X.UC_X86_REG_EFLAGS,0x202)
        u.mem_write(SSEG*16+0xfefc,struct.pack('<HH',RET,MSEG))
        u.emu_start(MSEG*16+self.k.v('klm_scan'), MSEG*16+RET,count=10000)
        require(u.reg_read(X.UC_X86_REG_IP)==RET,'scan did not return')
        require([u.reg_read(r) for r in REG]==values,'scan corrupted registers')
        require(u.reg_read(X.UC_X86_REG_SP)==0xff00,'scan stack imbalance')
        require(self.low>=0xfed0,'raw entry exceeds 48-byte stack budget')
        RAW_PEAK_STACK=max(RAW_PEAK_STACK,0xff00-self.low)
        require(bytes(u.mem_read(KSEG*16+self.k.v('kbd_dnmap'),16))==physical,
                'translation changed the physical game key map')
        return bool(u.reg_read(X.UC_X86_REG_EFLAGS)&1)

    def tap(self, scan):
        eaten=not self.feed(scan)
        self.feed(scan|0x80)
        return eaten

    def chars(self, scans, expected):
        self.ring()
        for scan in scans:
            self.tap(scan)
        words=self.words()
        actual=bytes(w&255 for w in words)
        require(actual==expected.encode('cp850'),
                f'{scans}: expected {expected!r}, got {actual.hex()}')
        return words


def check_publication(k, broken=False):
    m=Machine(k)
    u=m.u
    off=k.sym['mod_need.arm'][0]
    data=bytearray(k.image[off:off+100])
    at=data.index(b'\x9c\xfa')
    end=data.index(0x9d,at)+1
    data=data[:end]
    if broken:data[at+1]=0x90
    u.mem_write(0x80000,bytes(data))
    u.reg_write(X.UC_X86_REG_CS,0x8000)
    u.reg_write(X.UC_X86_REG_DS,KSEG)
    u.reg_write(X.UC_X86_REG_ES,MSEG)
    u.reg_write(X.UC_X86_REG_SS,SSEG)
    u.reg_write(X.UC_X86_REG_SP,0xff00)
    u.reg_write(X.UC_X86_REG_SI,EQUATES['MOD_H_ENT'])
    u.reg_write(X.UC_X86_REG_DI,k.v('KLFP'))
    u.reg_write(X.UC_X86_REG_DX,EQUATES['MOD_H_ENT'])
    u.reg_write(X.UC_X86_REG_BP,EQUATES['MODQ_SIZE'])
    u.reg_write(X.UC_X86_REG_EFLAGS,0x202)
    writes=[]
    def watch(uc, access, address, size, value, user):
        if KSEG*16+k.v('KLFP') <= address < KSEG*16+k.v('KLFP')+4:
            writes.append(bool(uc.reg_read(X.UC_X86_REG_EFLAGS)&0x200))
    u.hook_add(UC_HOOK_MEM_WRITE,watch)
    u.emu_start(0x80000,0x80000+end,count=1000)
    require(writes==[False,False],'module FAR slot publication is interruptible')
    require(u.reg_read(X.UC_X86_REG_EFLAGS)&0x200,'slot guard leaked IF clear')
    require(bytes(u.mem_read(KSEG*16+k.v('KLFP'),4))==
            struct.pack('<HH',k.v('klm_scan'),MSEG),'published wrong slot pair')


def check_controls(k):
    m=Machine(k,0);u=m.u
    start,n=k.sec['.cold']
    # .cold starts after the private header; its map gives its own vstart.
    import re
    match=re.search(r'^\s*([0-9A-F]+)\s+[0-9A-F]+\s+[0-9A-F]+\s+[0-9A-F]+\s+progbits\s+\.cold$',
                    Path(k.mapf).read_text(),re.M)
    u.mem_write(0x80000+int(match[1],16),k.image[start:start+n])
    u.mem_write(0x90000,b'\xf9\xcb')       # unarmed mod_gone: STC / RETF
    u.mem_write(0xa0000,b'\xf4')
    m.kw('mod_r_keyboard',struct.pack('<H',MSEG))
    u.mem_write(KSEG*16+k.v('KLFP')+4,struct.pack('<HH',0,0x9000))
    m.kw('sch_cur',[1])
    def call(ax,dx=0x0134):
        values=[ax,0x1234,0x2345,dx,0x4567,0x5678,0x6789,KSEG,0x4000]
        for reg,val in zip(REG,values):u.reg_write(reg,val)
        u.reg_write(X.UC_X86_REG_CS,0x8000)
        u.reg_write(X.UC_X86_REG_SS,SSEG)
        u.reg_write(X.UC_X86_REG_SP,0xfefc)
        u.reg_write(X.UC_X86_REG_EFLAGS,0x202)
        u.mem_write(SSEG*16+0xfefc,struct.pack('<HH',0,0xa000))
        u.emu_start(0x80000+k.v('kl_control_x'),0xa0000,count=10000)
        require(u.reg_read(X.UC_X86_REG_CS)==0xa000,'control failed to return')
        require([u.reg_read(r) for r in REG[1:]]==values[1:],'control corrupted registers')
        require(u.reg_read(X.UC_X86_REG_SP)==0xff00,'control stack imbalance')
        return u.reg_read(X.UC_X86_REG_AX),bool(u.reg_read(X.UC_X86_REG_EFLAGS)&1)
    # Row allocated, slots still unarmed: a worker QUERY is read-only BIOS0.
    before=bytes(u.mem_read(MSEG*16,EQUATES['MODQ_SIZE']))
    require(call(0)==(0,False),'worker QUERY during activation did not return BIOS0')
    require(bytes(u.mem_read(MSEG*16,EQUATES['MODQ_SIZE']))==before,
            'QUERY mutated an unpublished provider')
    require(call(0x0401)==(3,True),'worker SET did not refuse before loading')
    flags=KSEG*16+k.v('inst_tab')+EQUATES['I_RECSZ']+EQUATES['I_FLAGS']
    writes=[]
    def watch(uc,access,address,size,value,user):
        if address==flags:writes.append(value)
    u.hook_add(UC_HOOK_MEM_WRITE,watch)
    for ax,expected in ((0x0105,8),(0x0105,8),(5,0)):
        writes.clear()
        require(call(ax)==(0,False),'RAW policy update failed')
        require(writes==[expected],'RAW policy edge used multiple stores')
    print('  control: worker activation query, mutation refusal, atomic RAW; slot IRQ guard')


def exercise(k):
    count=0
    for layout, rows in ROWS.items():
        for scans, chars in zip(SCANS,rows):
            for scan,char in zip(scans,chars):
                if char==' ':
                    continue
                m=Machine(k,layout)
                words=m.chars([scan],char)
                require(words[0]>>8==scan,'BIOS word lost its physical scan')
                count+=1
    for layout, rows in SHIFT_ROWS.items():
        for scans, chars in zip(SCANS, rows):
            for scan, char in zip(scans, chars):
                if char == ' ':
                    continue
                m = Machine(k, layout); m.feed(0x2a)
                m.chars([scan], char); count += 1
    for layout, pairs in ALTGR.items():
        for scan, char in pairs.items():
            m=Machine(k,layout);m.feed(0xe0);m.feed(0x38)
            m.chars([scan],char);count+=1
    for layout, accent in ((2,0x28),(3,0x1a)):
        for scan,char in ((0x1e,'á'),(0x12,'é'),(0x17,'í'),(0x18,'ó'),(0x16,'ú')):
            Machine(k,layout).chars([accent,scan],char)
        m=Machine(k,layout); m.feed(0x2a); m.chars([0x27],'Ñ');m.feed(0xaa)
        m=Machine(k,layout); m.feed(0x2a); m.tap(accent);m.feed(0xaa);m.chars([0x16],'ü')
        m=Machine(k,layout);m.chars([accent,0x39],'´')
        m=Machine(k,layout);m.chars([accent,accent],'´')
        m=Machine(k,layout);m.chars([accent,0x2d],'´x')
        m=Machine(k,layout);m.chars([accent,0x0e,0x12],'e')
    Machine(k,4).chars([0x1a,0x12],'ê')
    m=Machine(k,4);m.feed(0x2a);m.tap(0x1a);m.feed(0xaa);m.chars([0x12],'ë')
    # AltGr is right Alt OR Ctrl+left Alt on an XT. Plain left Alt passes.
    for layout,scan in ((2,3),(3,0x10),(4,0x0b)):
        for prefix in ((0xe0,0x38),(0x1d,0x38)):
            m=Machine(k,layout)
            for s in prefix:m.feed(s)
            m.chars([scan],'@')
        m=Machine(k,layout);m.feed(0x38)
        require(m.feed(scan) and not m.words(),'ordinary Alt shortcut was translated')
    # Caps edge, typematic and Shift xor Caps; non-letter punctuation unchanged.
    m=Machine(k,2);m.tap(0x3a);m.chars([0x27],'Ñ');m.feed(0x2a);m.chars([0x27],'ñ')
    m=Machine(k,4);m.tap(0x3a);m.chars([3],'2')
    m=Machine(k,4);m.feed(0x1d);m.chars([0x10],'\x01')
    for layout in (2,3,4):
        m=Machine(k,layout);m.chars([0x56],'<');m.feed(0x2a);m.chars([0x56],'>')
        for scan in (0x3b,0x47,0x1c,0x0f):
            require(m.feed(scan),'special key did not pass to BIOS')
        m=Machine(k,layout)
        for scan in (0xe0,0x4b,0xe0,0xcb):
            require(m.feed(scan),'grey key did not pass to BIOS')
    # Ring wrap and full-ring fallback are atomic, including invalid composition.
    m=Machine(k,2);m.ring(0x2a,0x3c);m.tap(0x27)
    require(m.words(0x3c)==[0x27a4],'ring wrap')
    m=Machine(k,2);m.ring(0x20,0x1e);m.tap(0x27)
    require(int.from_bytes(m.u.mem_read(0x41c,2),'little')==0x1e,'full ring advanced')
    m=Machine(k,2);m.tap(0x28);m.ring(0x22,0x1e);m.tap(0x2d)
    require(bytes(m.u.mem_read(0x41e,4))==b'\xcc'*4,'two-byte fallback partially wrote')
    # A RAW-marked front instance receives BIOS input; no name-based identity.
    m=Machine(k,4);m.kw('wm_zn',[1]);m.kw('wm_zord',[0]);m.kw('wm_owner',[0])
    m.u.mem_write(KSEG*16+k.v('wm_wins')+EQUATES['W_FLAGS'],struct.pack('<H',2))
    m.u.mem_write(KSEG*16+k.v('inst_tab')+EQUATES['I_FLAGS'],b'\x08')
    require(m.feed(0x10) and not m.words(),'RAW foreground translated')
    m.u.mem_write(KSEG*16+k.v('inst_tab')+EQUATES['I_FLAGS'],b'\x00')
    m.chars([0x1a,0x12],'ê')  # first native key after RAW may itself be dead
    print(f'  {k.tree}: {count} independent ordinary keys + accents/modifiers/ISO/RAW/ring')


EQUATES={}
def main():
    global EQUATES, RAW_PEAK_STACK
    trees=sys.argv[1:] or ['build','build/smallk','build/emuk','build/megak']
    for tree in trees:
        k=Kernel(str(Path(os88build.at(tree)).resolve()))
        RAW_PEAK_STACK=0
        require(k.ok, f'missing built kernel: {tree}')
        # Equates are emitted in the real NASM map, not remembered strides.
        import re
        EQUATES={n:int(v,16) for v,n in re.findall(r'^([0-9A-F]+)\s+(\w+)$',Path(k.mapf).read_text(),re.M)}
        exercise(k)
        print('  raw stack peak including FAR frame:',RAW_PEAK_STACK,'bytes')
        check_controls(k)
        check_publication(k)
        try:check_publication(k,broken=True)
        except AssertionError as error:
            require('interruptible' in str(error),'slot negative control failed elsewhere')
        else:raise AssertionError('slot negative control did not fail')
        for symbol,scan,bad,expected in (('klm_map_fr',0x10,ord('q'),'a'),
                                         ('klm_map_la',0x0d,0xad,'¿')):
            m=Machine(k,4 if symbol.endswith('fr') else 3,
                      (k.v(symbol)+scan*2,bad))
            try:m.chars([scan],expected)
            except AssertionError:pass
            else:raise AssertionError('negative control did not fail: '+symbol)
        m=Machine(k,4,(k.v('klm_map_fr')+87*4+0x0b*2,ord('#')))
        m.feed(0xe0);m.feed(0x38)
        try:m.chars([0x0b],'@')
        except AssertionError:pass
        else:raise AssertionError('negative control did not fail: AltGr')
        print('  negative controls: French Q, Spanish variant and AltGr caught')
    print('kbdlayouts: PASS')


if __name__=='__main__':
    main()

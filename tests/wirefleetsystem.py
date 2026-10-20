#!/usr/bin/env python3
"""WireFleet adapter, serial lifetime, persistence and sound integration.

Registered as wirefleetsystem in soak. Real keyboard/UI except the pure
snapshot validator, whose CRC-valid negative fixtures run in the shipped
assembly on an 8088. No copy of its game logic lives in this test.
"""
import binascii
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
import os88marty as M
import os88ui
import dispapps
from wirefleet import artifact


def uart(m, base):
    lcr = m.inb(base+3)
    try:
        m.outb(base+3, lcr & 127)
        ier, mcr = m.inb(base+1), m.inb(base+4)
        m.outb(base+3, lcr | 128)
        return lcr, ier, m.inb(base), m.inb(base+1), mcr
    finally:
        m.outb(base+3, lcr)


def call(m, seg, sy, name):
    """Call a pure near routine, preserving the stopped guest's context."""
    saved = m.regs()
    regs = ('ax','bx','cx','dx','si','di','bp','sp','ss','ds','es','flags')
    sp = (saved['sp']-2)&65535
    address = (saved['ss']<<4)+sp
    old = m.read(address,2)
    target = sy['wf_entry.out']
    try:
        m.cmd(cmd='park',cs=seg,ip=sy['wf_'+name])
        for reg in regs:
            m.setreg(reg,saved[reg])
        m.setreg('ds',seg)
        m.setreg('es',seg)
        m.setreg('flags',saved['flags'] & ~0x600)
        m.setreg('sp',sp)
        m.write(address,struct.pack('<H',target))
        m.bp_exec((seg<<4)+target)
        start = m.status()['cycles']
        m.run()
        assert m.wait_stop(20)=='breakpoint', name
        return m.regs()['flags'] & 1, m.status()['cycles']-start
    finally:
        m.bp_exec()
        m.cmd(cmd='park',cs=saved['cs'],ip=saved['ip'])
        for reg in regs:
            m.setreg(reg,saved[reg])
        m.write(address,old)


def systems():
    from PIL import Image
    sy = dispapps._map('wirefleet')
    with os88ui.boot(artifact('os8088-360.img'), apps=artifact('wireflet360.img'),
                    machine='os8088_5150_cga_gla') as ui:
        m = ui.m
        # Before the game opens: SPEC.md 114.9 leases a free port by itself
        # on the window's first tick, so "as it was" has to be read first -
        # but AFTER the mouse contest settles, because settling is what
        # silences the losing UART (mou_lockon writes its IER).
        ui.mo.to(120,100)
        M.until(m,lambda _:m.read(m.sym('mou_hpst'),1)==b'\2','mouse settled',guest=10,poll=.05)
        m.pause()
        saved = uart(m,0x2f8)
        pic = m.inb(0x21)
        m.outb(0x2ff,0x5a)
        assert m.inb(0x2ff)!=0x5a,'needs original 8250'
        m.run()
        w = ui.path('B:/WIREFLET.O88')
        ui.settle()
        seg = dispapps.pkg_seg(m,0)[1]
        def data(name,n): return m.readseg(dispapps.pkg_seg(m,0)[1],sy['wf_'+name],n)
        def val(name,n=1): return int.from_bytes(data(name,n),'little')
        def key(name):
            m.key(name)
            ui.settle()
        def wait(cond,what): M.until(m,lambda _:cond(),what,guest=10,poll=.02)
        wait(lambda:val('linkopen')==1 and val('curport')==2,'automatic search leased the free COM2')
        ui.menu_pick('Link','Connect COM1')
        assert val('linkopen')==0,'mouse port was leased'
        ui.menu_pick('Link','Connect COM2')
        wait(lambda:val('linkopen')==1,'lease free original 8250')
        m.pause()
        active = uart(m,0x2f8)
        assert active[0]==3 and active[2:]==(48,0,11),active   # 2400 baud (SPEC.md 114.3)
        assert active[1]&1,'RX interrupt disabled'
        assert m.read(m.sym('ser_row'),2)==b'\2\0'
        m.run()
        ui.mo.to(120,100)
        assert struct.unpack('<HH',m.read(m.sym('mouse_x'),4))==(120,100)
        other=ui.open('WIREFLET.O88')
        ui.settle()
        otherseg=dispapps.pkg_seg(m,1)[1]
        ui.menu_pick('Link','Connect COM2')
        ui.settle()
        assert m.readseg(otherseg,sy['wf_linkopen'],1)==b'\0','second captain stole lease'
        assert int.from_bytes(m.readseg(otherseg,sy['wf_message'],2),'little')==sy['wf_linkbusy']
        ui.close(other)
        assert m.read(m.sym('ser_row'),2)==b'\2\0','closing non-owner released lease'
        ui.raise_window(w)
        # Mask both serial IRQs, so neither the UART nor a residual mouse
        # packet can service this byte. Only the UI's bounded service can.
        M.pace(m,.2)
        m.pause()
        livepic=m.inb(0x21)
        rxhead=data('serialbuf',1)
        m.outb(0x21,livepic|0x18)
        m.outb(0x2fc,0x1b)
        m.run()
        wait(lambda:data('serialbuf',1)!=rxhead,'8250 RX despite masked UART IRQ')
        m.pause()
        m.outb(0x2fc,0x0b)
        m.outb(0x21,livepic)
        m.run()
        # Fullscreen uses the same lease and the same private fleet.
        key('KeyA')
        board = data('board',100)
        m.key('KeyF')
        wait(lambda:val('infs')==1,'CGA four-color bracket')
        M.pace(m,.8)
        assert m.read(m.sym('ser_row'),2)==b'\2\0'
        assert data('board',100)==board
        old_target=val('target')
        m.key('ArrowRight')
        wait(lambda:val('target')==old_target+1,'fullscreen keyboard target')
        m.key('KeyY')
        wait(lambda:val('ready')==1,'fullscreen Ready')
        m.key('KeyT')
        wait(lambda:val('chatmode')==1,'fullscreen chat')
        for name in ('KeyR','KeyA','KeyM','KeyF'):
            m.key(name)
            M.pace(m,.1)
        assert data('input',5)==b'ramf\0' and val('muted')==0
        m.key('Escape')
        wait(lambda:val('chatmode')==0,'chat cancel')
        m.pause()
        fsi=data('fsi',16)
        assert struct.unpack_from('<H',fsi)[0]==0xb800
        raw=m.read(0xb8000,16384)
        pixels=[(raw[(y&1)*8192+(y//2)*80+x//4] >> (6-2*(x&3)))&3
                for y in range(200) for x in range(320)]
        assert {0,1,3} <= set(pixels),'CGA sea/grid/hull forms collapsed'
        im=Image.new('P',(320,200))
        im.putpalette([0,0,0,85,255,255,255,85,255,255,255,255]+[0]*756)
        im.putdata(pixels)
        im.save(artifact('wirefleet-cga-color-acceptance.png'))
        m.run()
        m.key('KeyF')
        wait(lambda:val('infs')==0 and m.read(m.sym('fsx_task'),1)==b'\xff','fullscreen restoration')
        ui.settle()
        assert data('board',100)==board and val('ready')==1
        ui.menu_pick('Link','Disconnect')
        m.pause()
        assert uart(m,0x2f8)==saved,'UART registers not restored'
        assert m.inb(0x21)&8==pic&8,'IRQ3 mask not restored'
        m.run()
        # Actual file dialog/write/read, followed by semantic negative controls.
        key('KeyS')
        m.key('Enter')
        ui.settle()
        assert val('message',2)==sy['wf_savedmessage']
        record=data('savebuf',512)
        assert record[:8]==b'WFLT\1\0\0\2'
        assert binascii.crc_hqx(record[:510],65535)==int.from_bytes(record[510:],'little')
        key('KeyN')
        assert data('board',100)==bytes(100)
        key('KeyO')
        ui.chooser_select('FLEET.WFL')
        m.key('Enter')
        ui.settle()
        assert val('message',2)==sy['wf_loadedmessage'], (
            [k for k,v in sy.items() if v==val('message',2)], val('linkmode'), val('linkopen'), val('cands'))
        assert data('board',100)==board and val('ready')==1 and val('connected')==0
        seg=dispapps.pkg_seg(m,0)[1]
        m.pause()
        # Park only at a complete callback boundary. Resetting the CPU's
        # prefetch/REP state in the middle of an arbitrary kernel instruction
        # is not equivalent to restoring its visible register file.
        m.bp_exec((seg<<4)+sy['wf_tick'])
        m.run()
        assert m.wait_stop(20)=='breakpoint'
        m.bp_exec()
        assert call(m,seg,sy,'snapshot_check')[0]==0,'valid snapshot refused'
        for offset,value in ((4,2),(393,1),(44,99),(49,2),(54,6),(42,4),
                             (13,2),(28,1),(31,31),(8,1),(358,1)):
            bad=bytearray(record)
            bad[offset]=value
            bad[510:]=struct.pack('<H',binascii.crc_hqx(bad[:510],65535))
            m.write((seg<<4)+sy['wf_savebuf'],bad)
            assert call(m,seg,sy,'snapshot_check')[0]==1,('invalid snapshot accepted',offset)
            assert data('board',100)==board,'validator changed live board'
        m.write((seg<<4)+sy['wf_savebuf'],record)
        m.run()
        # Bypass the app negotiator to exercise the kernel's forced teardown.
        ui.menu_pick('Link','Connect COM2')
        wait(lambda:val('linkopen')==1,'reopen lease after load')
        assert val('linkopen')==1, (data('board',100).hex(),
            m.readseg(dispapps.pkg_seg(m,0)[1],val('message',2),80),
            m.read(m.sym('ser_row'),2).hex(),m.read(m.sym('mou_hpst'),1).hex())
        m.pause()
        import os88geom as G
        import os88sym
        m.write(m.sym('wm_wins')+w.i*G.WIN_SIZE+os88sym.equates()['W_ONCL'],b'\0\0')
        m.run()
        ui.close(w)
        m.pause()
        assert m.read(m.sym('ser_row'),2)==b'\xff\xff'
        assert uart(m,0x2f8)==saved,'forced close retained UART'
        m.run()
        ui.mo.to(100,90)
        assert struct.unpack('<HH',m.read(m.sym('mouse_x'),4))==(100,90)
        print('wirefleetsystem: 8250 lease/refusal/forced restore, CGA color/input/chat/return, real save/load and CRC-valid invalid records')


def sounds():
    for backend,machine,size in (
            ('speaker','os8088_5150_cga_gla','360'),
            ('adlib','os8088_5150_herc_adlib_720_gla','720'),
            ('sb','os8088_5150_herc_sb_gla','360')):
        with os88ui.boot(artifact('os8088-'+size+'.img'),
                        apps=artifact('wireflet'+size+'.img'),machine=machine) as ui:
            w=ui.path('B:/WIREFLET.O88')
            ui.settle()
            m=ui.m
            sy=dispapps._map('wirefleet')
            seg=dispapps.pkg_seg(m,0)[1]
            def val(name,n=1):return int.from_bytes(m.readseg(seg,sy['wf_'+name],n),'little')
            m.key('KeyA')
            ui.settle()
            M.pace(m,.5)
            m.pause()
            returned=(seg<<4)+sy['wf_soundnote.out']
            m.bp_exec(m.sym('osapi_snd_tone'),returned)
            m.key('KeyY')
            m.run()
            notes=[]
            for hz in (660,880):
                assert m.wait_stop(20)=='breakpoint','Ready cue absent'
                r=m.regs()
                notes.append(r['ax'])
                assert r['ax']==hz and r['cx'] in (1,2),r
                m.run()
                assert m.wait_stop(20)=='breakpoint' and m.regs()['cs']==seg
                assert m.read(m.sym('snd_town_act'),1)==b'\1','router refused cue'
                if backend=='speaker':
                    assert m.inb(0x61)&3==3,'speaker gate never opened'
                else:
                    import os88drvsym
                    ds=os88drvsym.symbols('sound.bin')
                    driverseg=int.from_bytes(m.read(m.sym('drv_tab')+2,2),'little')
                    assert driverseg,'sound driver not loaded'
                    assert m.readseg(driverseg,ds['opl_b0']+8,1)[0]&32,'FM tone never keyed'
                m.run()
            m.pause()
            m.bp_exec()
            m.run()
            M.pace(m,.6)
            ui.settle()
            assert val('soundactive')==0,'cue did not finish'
            assert m.read(m.sym('snd_town_act'),1)==b'\0','sound route retained tone'
            caps=int.from_bytes(m.read(m.sym('drv_svc'),2),'little')
            assert bool(caps)==(backend!='speaker'),(backend,caps)
            m.key('KeyM')
            ui.settle()
            assert val('muted')==1
            m.key('KeyN')
            m.key('KeyA')
            ui.settle()
            assert val('soundactive')==0,'muted placement sounded'
            ui.close(w)
            assert m.read(m.sym('snd_town_act'),1)==b'\0'
            print('wirefleetsystem:',backend,'Ready tones',notes,'finite, mute and close')


if __name__=='__main__':
    systems()
    sounds()

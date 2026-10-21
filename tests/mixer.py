#!/usr/bin/env python3
"""SPEC.md 34.15: volume popup and optional Mixer on the actual desktop.

    make mixertest && python3 tools/os88test.py soak -k 'mixer*'

MartyPC supplies three real display adapters and an 8088. Its Sound Blaster
model stores mixer registers but does not attenuate analog audio; those runs
prove gestures and pixels, not sound loudness. --adapter sb16 uses QEMU's
SB16/CT1745 (docs/TESTING.md's mixer exception) and the same shared session.
The independent CPU/card model row validates the three real register maps.
"""
import argparse
import shutil
import struct
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
import os88marty as M
import os88ui
import os88sym
import os88drvsym
import os88geom as G
import os88qemu
import heapmap
import shot
from cycweb import Pkg, u16

MACHINES={'cga':'os8088_5150_sb_gla','herc':'os8088_5150_herc_sb_gla',
          'vga':'os8088_xt_vga_sb'}
EVIDENCE=ROOT/'build'/'mixer-evidence'


def check(cond,what):
    if not cond: raise AssertionError(what)
    print('  ok '+what,flush=True)


class Session:
    def __init__(self,ui,adapter):
        self.ui,self.m,self.mo,self.adapter=ui,ui.m,ui.mo,adapter
        self.qemu=adapter=='sb16'
        self.seg=ui._word('drv_fseg')
        origin=os88sym.equates()['DRV_HMA_ORG'] if self.seg>=0xF000 else 0
        self.d=Pkg(self.m,self.seg+origin//16,os88drvsym.symbols('sound.bin'))
        self.hx=ui._word('vid_clk_hx')

    def wait(self,cond,why):
        if self.qemu:
            check(os88qemu.acted(self.m,cond,secs=60,what=why),why)
        else:
            M.until(self.m,lambda _:cond(),why,limit=120)

    def pace(self,n=1):
        if self.qemu: os88qemu.pace(self.m,n)
        else: M.guest_sleep(self.m,n)

    def key(self,k):
        if self.qemu: self.m.hmp('sendkey '+k)
        else: self.m.key({'esc':'Escape','home':'Home','end':'End','space':'Space'}[k])
        self.pace(.3)

    def click(self,x,y):
        if self.qemu:
            self.mo.to(x,y);self.mo._edge(True);self.mo._edge(False)
            self.pace(.4)
        else: self.mo.click(x,y)

    def open(self):
        self.click(self.hx+12,9)
        self.wait(lambda:self.d.rb('volume_live')==1,'volume popup opened')
        self.pace(.3)
        return self.d.rw('volume_x')

    def close(self):
        self.key('esc')
        self.wait(lambda:self.d.rb('volume_live')==0,'volume popup closed')
        self.pace(.2)

    def owned(self):
        t=self.m.read(self.ui._S('mem_tab'),heapmap.MEM_MAX*heapmap.MC_SIZE)
        c=[heapmap.Claim(t[k:k+heapmap.MC_SIZE]) for k in range(0,len(t),heapmap.MC_SIZE)]
        return [(x.seg,x.para) for x in c if x.seg and x.own==self.seg]

    def pixels(self):
        if self.qemu:
            path=EVIDENCE/'qemu.ppm'
            self.m.hmp('screendump '+str(path))
            w,h,p=shot.read_ppm(path)
            return w,h,bytes(1 if any(p[k:k+3]) else 0 for k in range(0,len(p),3))
        if self.adapter in ('cga','herc'):
            w,h,rows=self.m.vram()
            return w,h,bytes(v for r in rows for v in r)
        w,h,p=self.m.fbuf()
        return w,h,bytes(1 if any(p[k:k+3]) else 0 for k in range(0,len(p),3))

    def screenshot(self,name):
        path=EVIDENCE/(self.adapter+'-'+name+'.png')
        if self.qemu:
            ppm=path.with_suffix('.ppm')
            self.m.hmp('screendump '+str(ppm))
            shot.png(str(path),*shot.read_ppm(ppm));ppm.unlink()
        elif self.adapter in ('cga','herc'):
            M.write_png(str(path),*self.m.vram())
        else: M.write_png_rgb(str(path),*self.m.fbuf())

    def crop(self,rect):
        w,h,p=self.pixels();x0,y0,x1,y1=rect
        return b''.join(
            p[y*w+x0:y*w+x1+1] for y in range(y0,y1+1))


def session(ui,adapter):
    s=Session(ui,adapter);d=s.d
    check(s.seg and d.rb('mix_kind')>0,'readable mixer publishes the speaker')
    check(ui._word('menu_clkx')==s.hx+24,'speaker reserves exactly three clock cells')
    check((ui._word('vid_pw')-8-ui._word('menu_clkx'))//8>=25,
          'speaker preserves the full clock and maximum toast field')
    check(not s.owned(),'idle mixer holds no scratch claim')
    x=s.open()
    check(d.rb('volume_found')==1,'SYSTEM/MIXER.O88 discovered on opening')
    check(ui._byte('fpg_on')==0,'file activity ends while popup remains open')
    check(ui._byte('cur_shape')!=os88sym.equates()["CUR_BUSYSH"],'popup restores the arrow')
    claims=s.owned()
    expected=10 if adapter in ('vga','sb16') else 3
    check(sum(p for _,p in claims)==expected*64,'save-under has the adapter-sized claim')
    s.key('home');s.wait(lambda:d.rb('volume_levelv')==0,'Home reaches silence')
    s.key('end');s.wait(lambda:d.rb('volume_levelv')==100,'End reaches full volume')
    s.screenshot('popup')
    # Track's two endpoints, including a held drag beyond its bounds.
    s.mo.to(x+28,58);s.mo._edge(True)
    s.wait(lambda:d.rb('volume_levelv')==0,'track press changes live volume')
    s.mo.to(min(ui._word('vid_pw')-1,x+202),58,l=True)
    s.wait(lambda:d.rb('volume_levelv')==100,'drag clamps to the top endpoint')
    s.mo._edge(False)
    s.wait(lambda:d.rb('volume_live')==0,'drag release dismisses the popup')
    check(not s.owned(),'drag dismissal releases the save-under')
    # Exact restore over a quiet desktop, with the pointer on the bar both times.
    s.mo.to(s.hx+12,9);s.pace(.5)
    rect=(x,20,x+207,115)
    before=s.crop(rect)
    s.open();s.close()
    check(before==s.crop(rect),'Escape restores every covered desktop pixel')
    check(not s.owned(),'Escape releases the save-under')
    # Launch after the tracker has restored the screen and dropped its lock.
    x=s.open();s.click(x+64,102)
    w=ui.wait_window('Mixer')
    p=Pkg(s.m,u16(s.m.read(G.winptr(s.m,w.i,ui.sym)+G.W_SEG,2)),
          os88drvsym.symbols('mixer.bin'))
    s.wait(lambda:p.rw('mx_mask')>0,'Mixer queried the live channel mask')
    check(d.rb('volume_live')==0 and not s.owned(),'package launch leaves no modal scratch')
    check(p.rb('mx_level')==100,'full Mixer reads volume written by the popup')
    x0,y0,_,_=w.content
    s.click(x0+240,y0+44)
    s.wait(lambda:0<p.rb('mx_level')<100,'full Mixer level slider writes hardware')
    if adapter=='sb16':
        check(p.rw('mx_mask')==0x1FF,'SB16 exposes all nine channels')
        s.click(x0+288,y0+104)
        s.wait(lambda:p.rb('mx_balance')>50,'stereo balance moves toward the right')
        a=p.rw('mx_pair');check((a&255)<(a>>8),'hardware readback preserves independent stereo')
        s.click(x0+32,y0+14+7*13+4)
        s.wait(lambda:p.rb('mx_sel')==7,'Bass source selected')
        check(p.rb('mx_flags')&2,'tone controls identify their cut/boost semantics')
    else:
        check(p.rw('mx_mask')==15,'MartyPC register bank exercises the mono family')
        s.click(x0+32,y0+31)
        s.wait(lambda:p.rb('mx_sel')==1,'PCM source selected')
        check(not p.rb('mx_flags')&1,'mono source offers no stereo balance')
    s.screenshot('mixer')
    ui.menu_pick('Mixer','About Mixer')
    s.wait(lambda:p.rb('mx_abon')==1,'About handler is registered')
    s.key('space');s.wait(lambda:p.rb('mx_abon')==0,'About card dismisses')
    ui.close(ui.window('Mixer'))
    check(not s.owned(),'closing Mixer leaves no driver scratch')
    # Click-away must consume the click rather than open the desktop below.
    s.open();s.click(70,150)
    s.wait(lambda:d.rb('volume_live')==0,'outside click dismisses')
    check(not s.owned(),'outside dismissal releases the save-under')
    print('mixer '+adapter+': PASS',flush=True)


def absent():
    # The quick UI works when the optional package is not present.
    with os88ui.boot('build/mixernone360.img',machine=MACHINES['cga'],verbose=False) as ui:
        s=Session(ui,'cga');s.open()
        check(s.d.rb('volume_found')==0,'missing package removes Open Mixer')
        check(s.d.rw('volume_bottom')==91,'missing package removes its command row')
        s.key('end');s.wait(lambda:s.d.rb('volume_levelv')==100,'quick volume works without Mixer')
        s.close();check(not s.owned(),'optional-package absence leaves no claim')
    with os88ui.boot('build/os8088-360.img',machine='os8088_5150_cga_gla',verbose=False) as ui:
        check(ui._word('menu_mixcap')==0,'no mixer leaves the speaker absent')
        check(ui._word('menu_clkx')==ui._word('vid_clk_hx'),'no mixer restores full clock width')


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--adapter',choices=(*MACHINES,'sb16'),default='cga')
    a=ap.parse_args();EVIDENCE.mkdir(exist_ok=True)
    if a.adapter=='sb16':
        # MartyPC has a raw register bank, not a CT1745. QEMU owns this leg.
        import hmacold,vidxms,xmcheck
        for memory in (1,128):
            with tempfile.TemporaryDirectory(prefix='mixer-qemu-',dir=ROOT/'build') as t:
                sy=Path(t)/'system.img';apps=Path(t)/'apps.img'
                shutil.copy(ROOT/'build/mixer360.img',sy)
                shutil.copy(ROOT/'build/apps360.img',apps)
                q=vidxms.Q(t,str(sy),str(apps),memory,
                    ('-audiodev','none,id=snd','-device','sb16,audiodev=snd'))
                os88qemu.own(q.pid,q.sock)
                try:
                    xmcheck.wait_desktop(q.sock,'mixer')
                    ui=hmacold.QUI(q,mouse=hmacold.QMouse(q),sym=os88sym.linear,verbose=False)
                    os88qemu.pace(q,2)
                    print('SB16 memory:',memory,flush=True)
                    session(ui,'sb16')
                finally:q.close()
    else:
        with os88ui.boot('build/mixer360.img',machine=MACHINES[a.adapter],verbose=False) as ui:
            session(ui,a.adapter)
        if a.adapter=='cga':absent()

if __name__=='__main__':main()

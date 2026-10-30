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
import subprocess
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
import os88build
import heapmap
import shot
from cycweb import Pkg, u16

MACHINES={'cga':'os8088_5150_sb_gla','herc':'os8088_5150_herc_sb_gla',
          'vga':'os8088_xt_vga_sb'}
EVIDENCE=ROOT/'build'/'mixer-evidence'
BOX86=(bool(__import__('os').environ.get('OS88_MEGA')) and
       __import__('os').environ.get('OS88_MEGA_EMU','').lower()=='86box')


def check(cond,what):
    if not cond: raise AssertionError(what)
    print('  ok '+what,flush=True)


class Session:
    def __init__(self,ui,adapter,card=None):
        self.ui,self.m,self.mo,self.adapter=ui,ui.m,ui.mo,adapter
        self.qemu=adapter in ('sb16','vesa')
        # THE CARD, which is not always the harness: on kern_mega under 86Box
        # (BOX86) the SB16 leg runs on the Marty path, a VGA beside 86Box's
        # SB16/CT1745, and every expectation about the CARD is QEMU's
        self.sb16=(card or adapter) in ('sb16','vesa')
        self.seg=ui._word('drv_fseg')
        origin=os88sym.equates()['DRV_HMA_ORG'] if self.seg>=0xF000 else 0
        self.d=Pkg(self.m,self.seg+origin//16,os88drvsym.symbols('sound.bin'))
        self.hx=ui._word('menu_mixx')     # the speaker: the clock's right end

    def wait(self,cond,why):
        if self.qemu:
            check(os88qemu.acted(self.m,cond,secs=60,what=why),why)
        else:
            M.until(self.m,lambda _:cond(),why,limit=120)

    def pace(self,n=1):
        if self.qemu: os88qemu.pace(self.m,n)
        else: M.guest_sleep(self.m,n)

    def done(self,n):
        """QEMU: the UI finished with the input, capped at the old pause
        (SOAK-SPEED 14) - a crop after it sees what the handler drew"""
        os88qemu.ui_done(self.m,os88sym.linear,cap=n)

    def key(self,k):
        if self.qemu: self.m.hmp('sendkey '+{'space':'spc'}.get(k,k))
        else: self.m.key({'esc':'Escape','home':'Home','end':'End','space':'Space',
                              'left':'ArrowLeft','right':'ArrowRight'}[k])
        if self.qemu: self.done(.3)   # (MartyPC: every key is followed by a
                                      # wait on the state it changes)

    def click(self,x,y):
        if self.qemu:
            self.mo.to(x,y);self.mo._edge(True);self.mo._edge(False)
            self.done(.4)
        else: self.mo.click(x,y,settle=0)   # (every click is followed by a
                                            # wait on the state it changes)

    def open(self):
        self.hx=self.ui._word('menu_mixx')
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

    def icon(self):
        # Compare all pixels with the actual bar paper and rule ink, including
        # gray/color paper which a binary lit/unlit comparison cannot see.
        self.hx=self.ui._word('menu_mixx')
        data=self.m.read(self.d.addr('mix_speaker'),48)
        if self.qemu:
            path=EVIDENCE/'icon.ppm'
            self.m.hmp('screendump '+str(path))
            w,h,p=shot.read_ppm(path);stride=3
        elif self.adapter in ('cga','herc'):
            w,h,p=self.pixels();stride=1
        else:
            w,h,p=self.m.fbuf();stride=3
        def pixel(x,y):
            at=(y*w+x)*stride
            return bytes(p[at:at+stride])
        paper,ink=pixel(w-4,2),pixel(w-4,19)
        expected=b''.join(paper if (b>>bit)&1 else ink
                          for b in data for bit in range(7,-1,-1))
        actual=b''.join(pixel(self.hx+x,2+y) for y in range(16) for x in range(24))
        check(actual==expected,'speaker bitmap uses the menu-bar paper and ink')
        check(self.hx==self.ui._word('vid_wm8')-24,
              'speaker is the last 24 pixels before the bar margin')
        # The clock is right-aligned against the speaker by leading spaces
        # (SPEC.md 34.15), so the cells between the field's left edge and the
        # first glyph are lettered paper - which is what erases a disk-
        # progress mark left in the field.
        text=self.m.read(self.ui._S('clk_str'),32).split(bytes([0]),1)[0]
        padding=b''.join(pixel(x,y) for y in range(6,14)
                         for x in range(self.ui._word('vid_clk_hx'),self.hx-8*len(text)))
        check(padding==paper*(len(padding)//stride),
              'clock padding erases disk-progress marks in the field')

    def themes(self):
        import dispthm
        dispthm.theme_page(self.m,self.mo,self.ui._S,M.settle)
        names=('Dark','Color','Windows','Bright') if self.adapter=='vga' else ('Dark','Windows','Bright')
        for name in names:
            got=dispthm.pick(self.m,self.mo,self.ui._S,M.settle,name)
            check(got=={'Bright':0,'Dark':1,'Color':2,'Windows':3}[name],name+' theme applied')
            self.mo.to(80,150);self.pace(.3);self.icon()
            self.open();self.screenshot(name.lower()+'-popup');self.close()
            self.mo.to(80,150);self.pace(.3);self.icon()
        self.ui.close(self.ui.window('Control Panel'))
        # A longer clock form widens the field LEFT; the speaker and its hit
        # target stay at the bar margin (SPEC.md 34.15).
        flags=[self.ui._byte(n) for n in ('clk_h12','clk_secs')]
        self.m.write(self.ui._S('clk_h12'),bytes([1]))
        self.m.write(self.ui._S('clk_secs'),bytes([1]))
        def long_clock_ready():
            text=self.m.read(self.ui._S('clk_str'),32).split(bytes([0]),1)[0]
            # clk_fmt stages its string before menu_sound publishes the new
            # field edge. Wait for both, not an intermediate guest state.
            pw=self.ui._word('vid_pw')
            return (len(text)>=23 and self.ui._word('menu_mixx')==pw-32 and
                    self.ui._word('vid_clk_hx')==((pw-206)&0xFFF8)-8*(len(text)-22))
        self.wait(long_clock_ready,'long clock format and speaker reached the bar')
        self.mo.to(80,150);self.pace(.3);self.icon()
        self.open();self.close()
        for n,v in zip(('clk_h12','clk_secs'),flags):self.m.write(self.ui._S(n),bytes([v]))
        self.pace(1.5)
        self.mo.to(80,150);self.icon()


def toast(s,pw):
    """A toast that missed the gap HIDES the speaker and leaves [vid_clk_hx]
    where it is (SPEC.md 34.15), so a progress widget armed against that edge
    is never under the strip. It used to widen the field three cells left."""
    ui=s.ui
    # The gap runs to the clock's first GLYPH (SPEC.md 59.9.1), so on a bare
    # desktop even a TOAST_MAX strip fits it at 640px. A Disk window's four
    # menus narrow it to 21 cells there - and leave 31 at 720, which is the
    # other branch, asserted rather than skipped.
    w=ui.open_drive('A')
    hx=ui._word('vid_clk_hx')
    msg=b'Toast covers the speaker'           # TOAST_MAX characters
    s.m.write(ui._S('toast_buf'),msg+b'\0')
    # FIVE SECONDS, not two: the checks below come .3 s after `wait` SEES
    # the strip, and `until` backs off to a quarter of a HOST second - a guest
    # second and more on MartyPC, nearly two on kern_mega's 86Box running
    # fast-forwarded, where a two-second toast had gone by the check 5 runs
    # in 8 (toast_on 0, so toast_scov 0). Expiry is still waited for below
    s.m.write(ui._S('toast_ttl'),struct.pack('<H',91))
    s.m.write(ui._S('toast_inst'),b'\0')
    s.m.write(ui._S('toast_want'),b'\1')
    s.wait(lambda:ui._byte('toast_on')==1,'toast is up')
    s.pace(.3)
    if pw==640:
        check(ui._byte('toast_scov'),'a TOAST_MAX strip misses a Disk window\'s gap at 640px')
        check(ui._word('menu_mixcap')==0,'a toast in the clock field hides the speaker')
        check(ui._word('vid_clk_hx')==hx,'...and the clock edge does not move for it')
    else:
        check(not ui._byte('toast_scov'),'...and fits it at %dpx'%pw)
        check(ui._word('menu_mixcap')!=0,'a toast in the gap leaves the speaker up')
        check(ui._word('vid_clk_hx')==hx,'a toast in the gap moves nothing')
    s.wait(lambda:ui._byte('toast_on')==0,'toast expires')
    s.wait(lambda:ui._word('menu_mixcap')!=0,'the speaker comes back')
    ui.close(w)
    s.pace(.3);s.icon()


def session(ui,adapter,card=None):
    if adapter not in ('sb16','vesa'): ui.still()   # (a quick boot: the desktop drawn)
    s=Session(ui,adapter,card);d=s.d
    check(s.seg and d.rb('mix_kind')>0,'readable mixer publishes the speaker')
    pw=ui._word('vid_pw')
    check(s.hx==pw-32,'speaker sits right of the clock, at the bar margin')
    check(ui._word('vid_clk_hx')==(pw-206)&0xFFF8,
          'speaker leaves the menus segment as it is without one')
    check((s.hx-ui._word('vid_clk_hx'))//8>=22,
          'the default clock keeps its whole line left of the speaker')
    check(not s.owned(),'idle mixer holds no scratch claim')
    s.mo.to(80,150);s.pace(.3);s.icon()
    if not s.qemu: toast(s,pw)   # QEMU's monitor here writes no memory
    x=s.open()
    check(d.rw('volume_bottom')==91,'popup contains only the master-volume control')
    check(ui._byte('fpg_on')==0,'file activity ends while popup remains open')
    check(ui._byte('cur_shape')!=os88sym.equates()["CUR_BUSYSH"],'popup restores the arrow')
    claims=s.owned()
    expected=30 if adapter=='vesa' else (8 if adapter in ('vga','sb16') else 2)
    check(sum(p for _,p in claims)==expected*64,'save-under has the adapter-sized claim')
    s.key('home');s.wait(lambda:d.rb('volume_levelv')==0,'Home reaches silence')
    s.key('right');s.wait(lambda:d.rb('volume_levelv')==1,'arrow moves by one percent on a coarse card')
    s.key('left');s.wait(lambda:d.rb('volume_levelv')==0,'opposite arrow returns by one percent')
    s.key('end');s.wait(lambda:d.rb('volume_levelv')==100,'End reaches full volume')
    s.screenshot('popup')
    # Track's two endpoints, including a held drag beyond its bounds.
    s.mo.to(x+28,58);s.mo._edge(True)
    s.wait(lambda:d.rb('volume_levelv')==0,'track press changes live volume')
    for px,want in ((88,40),(90,41),(91,42)):
        s.mo.to(x+px,58,l=True)
        s.wait(lambda:d.rb('volume_levelv')==want,'drag follows requested '+str(want)+' percent')
    s.mo.to(min(ui._word('vid_pw')-1,x+202),58,l=True)
    s.wait(lambda:d.rb('volume_levelv')==100,'drag clamps to the top endpoint')
    s.mo.to(x+28,150,l=True)
    s.wait(lambda:d.rb('volume_levelv')==0,'captured drag continues outside the panel vertically')
    s.mo._edge(False)
    s.wait(lambda:d.rb('volume_live')==0,'drag release dismisses the popup')
    check(not s.owned(),'drag dismissal releases the save-under')
    # Exact restore over a quiet desktop, with the pointer on the bar both times.
    s.mo.to(s.hx+12,9);s.pace(.5)
    rect=(x,20,x+207,91)
    before=s.crop(rect)
    s.open();s.close()
    check(before==s.crop(rect),'Escape restores every covered desktop pixel')
    check(not s.owned(),'Escape releases the save-under')
    # Mixer is an ordinary app launched from the separate apps disk.
    s.open();s.key('end');s.close()
    w=ui.path('B:/MIXER.O88')
    p=Pkg(s.m,u16(s.m.read(G.winptr(s.m,w.i,ui.sym)+G.W_SEG,2)),
          os88drvsym.symbols('mixer.bin'))
    s.wait(lambda:p.rw('mx_mask')>0,'Mixer queried the live channel mask')
    check(d.rb('volume_live')==0 and not s.owned(),'separate app launch leaves no modal scratch')
    check(p.rb('mx_level')==100,'full Mixer reads volume written by the popup')
    x0,y0,_,_=w.content
    s.click(x0+240,y0+44)
    s.wait(lambda:0<p.rb('mx_level')<100,'full Mixer level slider writes hardware')
    s.key('home');s.wait(lambda:p.rb('mx_level')==0,'full Mixer keyboard reaches silence')
    s.key('right');s.wait(lambda:p.rb('mx_level')==1,'Mixer moves smoothly below the first hardware step')
    check(p.rw('mx_pair')==0,'hardware readback remains quantized independently of the thumb')
    s.pace(1.5)
    check(p.rb('mx_level')==1,'idle polling retains the requested slider position')
    s.open();s.key('end');s.close()
    s.wait(lambda:p.rb('mx_level')==100,'Mixer notices another control changing the hardware')
    s.key('end');s.wait(lambda:p.rb('mx_level')==100,'full Mixer keyboard reaches full volume')
    if s.sb16:
        check(p.rw('mx_mask')==0x1FF,'SB16 exposes all nine channels')
        mark=(x0+136,y0+18,x0+143,y0+25)
        check(0 in s.crop(mark),'keyboard focus is visible beside Level')
        s.click(x0+288,y0+104)
        s.wait(lambda:p.rb('mx_balance')>50,'stereo balance moves toward the right')
        check(s.crop(mark)==bytes([1])*64,'changing mouse focus erases the old Level marker')
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
    s.icon()
    # Click-away must consume the click rather than open the desktop below.
    s.open();s.click(70,150)
    s.wait(lambda:d.rb('volume_live')==0,'outside click dismisses')
    check(not s.owned(),'outside dismissal releases the save-under')
    if not s.qemu:s.themes()
    print('mixer '+(card or adapter)+': PASS',flush=True)


def absent():
    with os88ui.boot('build/os8088-360.img',machine='os8088_5150_cga_gla',verbose=False,quick=True) as ui:
        check(ui._word('menu_mixcap')==0,'no mixer leaves the speaker absent')
        check(ui._word('vid_clk_hx')==(ui._word('vid_pw')-206)&0xFFF8,
              'no mixer keeps the full clock field')


def background(ui,adapter='sb16'):
    """A fully staged stream still drains while the modal popup owns the UI."""
    s=Session(ui,adapter,'sb16');d=s.d
    ui.path('B:/MIXPLAY.O88')
    w=ui.wait_window('SB Test')
    x,y,_,_=w.content
    s.mo.to(x+40,y+30);s.mo._edge(True);s.mo._edge(False)
    s.wait(lambda:d.rb('sbl_str_act')==1,'background stream opened')
    s.mo.to(s.hx+12,9);s.mo._edge(True);s.mo._edge(False)
    s.wait(lambda:d.rb('volume_live')==1,'volume popup opened during playback')
    consumed=d.rw('sbl_consumed')
    check(consumed<19200,'volume popup opened before the stream finished')
    s.key('home')
    s.wait(lambda:d.rb('volume_levelv')==0,'volume changes during background playback')
    s.wait(lambda:d.rw('sbl_consumed')==19200,'card consumed the entire staged stream')
    check(d.rb('volume_live')==1,'audio finished while the volume popup remained open')
    s.key('end');s.close()
    ui.close(ui.window('SB Test'))
    s.wait(lambda:d.rb('sbl_str_act')==0,'closing the player releases its stream')
    check(not s.owned(),'background playback leaves no driver scratch or stream buffer')


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--adapter',choices=(*MACHINES,'sb16','vesa'),default='cga')
    a=ap.parse_args();EVIDENCE.mkdir(exist_ok=True)
    if a.adapter=='sb16' and BOX86:
        # KERN_MEGA ON 86BOX (tests/megaport.txt, OS88_MEGA_SND=sb16): the
        # card QEMU stands in for is 86Box's own SB16 with its CT1745, so the
        # leg runs on the Marty path with the VGA's pixels - the fixture's
        # system disk rebuilt on mega.img (os88qmpmega.mega_twin), the
        # driver's selector through m.lin (cycweb.Pkg)
        import os88qmpmega
        with os88ui.boot(os88qmpmega.mega_twin(os88build.at('build/mixer360.img')),
                         apps=os88build.at('build/mixplay.img'),
                         machine='os8088_xt_vga_sb',verbose=False,quick=True) as ui:
            session(ui,'vga','sb16')
            background(ui,'vga')
        return
    if a.adapter in ('sb16','vesa'):
        # MartyPC has a raw register bank, not a CT1745. QEMU owns this leg.
        import hmacold,vidxms,xmcheck
        class QMouse(hmacold.QMouse):
            def to(self,x,y,l=False,**kw):
                if a.adapter!='vesa':return super().to(x,y,l=l,**kw)
                for _ in range(4):
                    subprocess.run([sys.executable,str(ROOT/'tools/mouse.py'),
                                    '--screen','1024x768',self.q.sock,'to',str(x),str(y)],
                                   cwd=ROOT,check=True,capture_output=True)
                    if os88qemu.acted(self.q,lambda:self.where()[:2]==(x,y),secs=3,
                                     what='VESA pointer reached its target'):return
                raise os88ui.UIError('VESA pointer did not reach its target')
            def click(self,x,y,**_kw):
                # UI.close also uses click; pace it on QEMU's guest ticks.
                self.to(x,y);self._edge(True);self._edge(False)
                os88qemu.ui_done(self.q,os88sym.linear,cap=.2)
        class QUI(hmacold.QUI):
            def scroll_to(self,entry,win=None):
                # Both files fit in the fixture's root listing. Avoid the
                # base UI's keyboard scroller, which requires MartyPC cycles.
                check(self.scroll(win)==0 and len(self.listing(win))==2,
                      'both apps-fixture rows fit without scrolling')
                return entry
        for memory in ((128,) if a.adapter=='vesa' else (1,128)):
            with tempfile.TemporaryDirectory(prefix='mixer-qemu-',dir=ROOT/'build') as t:
                sy=Path(t)/'system.img';apps=Path(t)/'apps.img'
                system='build/mixervesa.img' if a.adapter=='vesa' else 'build/mixer360.img'
                shutil.copy(os88build.at(system),sy)
                shutil.copy(os88build.at('build/mixplay.img'),apps)
                q=vidxms.Q(t,str(sy),str(apps),memory,
                    ('-audiodev','none,id=snd','-device','sb16,audiodev=snd'))
                os88qemu.own(q.pid,q.sock)
                try:
                    xmcheck.wait_desktop(q.sock,'mixer')
                    ui=QUI(q,mouse=QMouse(q),sym=os88sym.linear,verbose=False)
                    os88qemu.ui_done(q,os88sym.linear,cap=2)
                    if a.adapter=='vesa':
                        check(ui._byte('vid_kind')==G.VID_VESA and ui._byte('vid_planes')==16,
                              'packed desktop reached 1024x768x16')
                    print(a.adapter+' memory:',memory,flush=True)
                    session(ui,a.adapter)
                    if a.adapter=='sb16':background(ui)
                finally:q.close()
    else:
        with os88ui.boot('build/mixer360.img',apps='build/mixplay.img',
                         machine=MACHINES[a.adapter],verbose=False,quick=True) as ui:
            session(ui,a.adapter)
        if a.adapter=='cga':absent()

if __name__=='__main__':main()

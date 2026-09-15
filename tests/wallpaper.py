#!/usr/bin/env python3
"""Wallpaper UI, pixels, damage, persistence and the CGA category scroller.

The negative case is a renderer that updates wp_kind but leaves old pixels:
selection-byte assertions alone cannot pass this test. Real mouse selections
are checked against independent pattern bytes and the committed die bitmap.
Three synthetic page-name registrations exercise scrolling even on a machine
with no hard disk or network card; no driver callbacks are invoked.
"""
import re
import sys
from pathlib import Path
sys.path[:0] = [str(Path(__file__).resolve().parents[1] / 'tools')]
import os88marty as M
import os88mouse
import os88sym
import os88build
import dispcp

S = os88sym.linear
PATTERNS = [
    [0xEE,0xFF,0xBF,0xF9,0xF9,0xFF,0xDF,0xFF],
    [0x7E,0xBD,0xDB,0xE7,0x7E,0xBD,0xDB,0xE7],
    [0xF0]*4+[0x0F]*4,
    [0x77,0xBB,0xDD,0xEE]*2,
    [0x00]+[0x7F]*7,
    [0x7F,0xFF,0xFF,0xFF,0xF7,0xFF,0xFF,0xFF],
    [0xE3,0xDD,0xDD,0xE3,0xEF,0xEF,0xEF,0x0F],
    [0x7F,0xBF,0xDF,0xEF,0xF7,0xFB,0xFD,0xFE],
]
OUT = Path(os88build.at('build/wallpaper-test'))


def byte(m, name):
    return m.read(S(name), 1)[0]


def ready(m, **kw):
    M.settle(m, **kw)


def shot(m, name, card=0):
    w,h,fb = m.fbuf(card=card)
    M.write_png_rgb(str(OUT / (name+'.png')), w,h,fb)
    return w,h,fb


def patch(m, x,y,w,h,card=0):
    fw,fh,fb = m.fbuf(card=card)
    # docs/HERCULES-TESTING.md: rendered Hercules has a (-16,+2) offset.
    if (fw,fh)==(720,350):
        x,y=x-16,y+2
    return [fb[((y+r)*fw+x)*3:((y+r)*fw+x+w)*3] for r in range(h)]


def pick(m, mo, kind):
    dispcp.open_panel(m,mo,S,ready,page=6)
    wx,wy = dispcp._cp_win(m,S)
    while byte(m,'cp_wp_page') != kind//6:
        right = byte(m,'cp_wp_page') < kind//6
        mo.click(wx+97+(140 if right else 30), wy+19+123,settle=0)
        ready(m)
    mo.click(wx+97+40,wy+19+22+(kind%6)*14+4,settle=0)
    ready(m)
    assert byte(m,'wp_kind') == kind, ('selection',kind,byte(m,'wp_kind'))


def pattern(m, kind, card=0):
    # Clear of the panel and volume icons, with deliberately unaligned origin.
    x,y,w,h = 23,31,37,19
    rows=patch(m,x,y,w,h,card)
    for r,row in enumerate(rows):
        for c in range(w):
            lit=any(row[c*3:c*3+3])
            expect=bool(PATTERNS[kind-9][(y+r)&7] & (0x80 >> ((x+c)&7)))
            assert lit == expect, ('pattern pixel',kind,card,x+c,y+r)


def die(m, card=0):
    fw,fh,fb=m.fbuf(card=card)
    # VGA raster and CGA raster use their native logical dimensions here.
    x=((fw-128)//2)&~7
    y=((fh-128)//2)&~7
    if (fw,fh)==(720,350):
        x,y=x-16,y+2
    bits=[int(v,16) for v in re.findall(r'0x([0-9A-F]{2})',
          Path('kernel/wallpaper-die.inc').read_text())]
    assert len(bits)==2048
    for r in range(128):
        for c in range(128):
            b=bits[((r//8)*16+c//8)*8+r%8]
            expect=bool(b & (0x80>>(c%8)))
            at=((y+r)*fw+x+c)*3
            assert any(fb[at:at+3]) == expect, ('die pixel',card,c,r)


def scroll(m, mo):
    # Model three published driver page names. Preserve every table byte and
    # do not dispatch their pages; the selected static Wallpaper row is real.
    base=S('drv_svc')
    eq=os88sym.equates()
    stride=eq['DSV_SIZE']
    saved=m.read(base,stride*4)
    try:
        for i in range(3):
            m.write(base+i*stride+eq['DSV_CPNAME'], b'\x01\x00')
        dispcp.close_panel(m,mo,S,ready)
        dispcp.open_panel(m,mo,S,ready,page=None)
        wx,wy=dispcp._cp_win(m,S)
        last=byte(m,'cp_nst')+3-8
        assert last>0
        for expected in range(1,last+1):
            mo.click(wx+66,wy+19+123,settle=0)
            ready(m)
            assert byte(m,'cp_top')==expected
        mo.click(wx+66,wy+19+123,settle=0)
        ready(m)
        assert byte(m,'cp_top')==last, 'scroll past end'
        dispcp.open_panel(m,mo,S,ready,page=6)
        assert byte(m,'cp_sel')==6, 'scrolled ordinal selected wrong record'
        shot(m,'scroll')
        dispcp.open_panel(m,mo,S,ready,page=0)
        assert byte(m,'cp_top')==0
    finally:
        m.write(base,saved)
    dispcp.close_panel(m,mo,S,ready)


def run(machine, all_choices):
    print('wallpaper:',machine,flush=True)
    with M.launch(os88build.at('build/os8088-360.img'),
                  apps=os88build.at('build/apps360.img'),machine=machine) as m:
        M.no_saver(m)
        mo=os88mouse.Mouse(marty=m)
        colors=[]
        choices=range(18) if all_choices else [4,9,13,17]
        for kind in choices:
            pick(m,mo,kind)
            if 1<=kind<=8 and all_choices:
                rows=patch(m,23,31,37,19)
                pixels=[r[i:i+3] for r in rows for i in range(0,len(r),3)]
                assert len(set(pixels))==1, ('solid is patterned',kind)
                colors.append(pixels[0])
            if 9<=kind<=16:
                pattern(m,kind)
            print(' choice',kind,'ok',flush=True)
        if all_choices:
            assert len(set(colors))==8, 'solid colors are not distinct'
        dispcp.close_panel(m,mo,S,ready)
        ready(m)
        die(m)
        fw,fh,_=m.fbuf(card=0)
        dx=((fw-128)//2)&~7
        dy=((fh-128)//2)&~7
        before=patch(m,dx,dy,128,128)
        dispcp.open_panel(m,mo,S,ready,page=6)
        dispcp.close_panel(m,mo,S,ready)
        ready(m)
        assert patch(m,dx,dy,128,128)==before, 'closing a window damaged wallpaper'
        shot(m,'die-'+machine)
        m.reset()
        m.run()
        M.settle(m,gate=M.desktop_up)
        assert byte(m,'wp_kind')==17, 'wallpaper did not survive reboot'
        mo.to(20,10)  # reset centers the pointer directly over the die
        ready(m)
        die(m)
        scroll(m,mo)
        print(' die pixels, damage, reboot, scrolling ok',flush=True)


def dual():
    print('wallpaper: extended VGA + Hercules',flush=True)
    with M.launch(os88build.at('build/os8088-360.img'),
                  apps=os88build.at('build/apps360.img'),
                  machine='os8088_xt_vga_herc') as m:
        M.no_saver(m)
        mo=os88mouse.Mouse(marty=m)
        dispcp.open_panel(m,mo,S,ready,page=4)
        dispcp.set_mode(m,mo,S,ready,'right')
        pick(m,mo,9)
        pattern(m,9,card=1)
        pick(m,mo,17)
        dispcp.close_panel(m,mo,S,ready)
        ready(m)
        die(m,card=0)
        die(m,card=1)
        shot(m,'die-hercules',card=1)
        print(' secondary pattern and die pixels ok',flush=True)


if __name__=='__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    run('os8088_xt_vga',True)
    run('os8088_5150_cga_gla',False)
    dual()

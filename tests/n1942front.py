#!/usr/bin/env python3
"""Native desktop splash pixels, menu input and canvas reuse on three adapters."""
import argparse
from PIL import Image
import n1942 as N


def key(ui, name):
    ui.m.key(name, up=False)
    N.M.pace(ui.m, .04)
    ui.m.key(name, down=False)
    N.M.pace(ui.m, .15)
    N.M.ui_done(ui.m)


def revealed(ui,g,tag):
    N.M.until(ui.m,lambda _: g.get('frontready')==(12 if tag=='cga' else 24),
              'splash reveal finishes',guest=10)
    ui.settle()


def shot(ui, tag, name):
    ui.mo.to(2,2);ui.settle()
    path=N.ROOT/f'build/1942-proof/{tag}-splash-{name}.png'
    path.parent.mkdir(parents=True,exist_ok=True)
    if tag=='vga':
        w,h,pixels=ui.m.fbuf(0);N.M.write_png_rgb(str(path),w,h,pixels)
    else:
        w,h,pixels=ui.m.vram();N.M.write_png(str(path),w,h,pixels)
    return Image.open(path).convert('RGB')


def logo(image,g,tag,ui=None):
    x,y=g.get('frontx'),g.get('fronty')
    if ui:
        w=ui.window('1942');x,y=w.x+1,w.y+N.G.TITLE_H
    expected=Image.open(N.ROOT/f'build/1942-splash-{tag}.png').convert('RGB')
    actual=image.crop((x+8,y,x+440,y+expected.height))
    assert actual.tobytes()==expected.tobytes(), ('title pixels',tag)
    assert len(actual.getcolors(100000))==(4 if tag=='vga' else 2)
    scale=g.get('frontscale',1)
    assert image.crop((x+8,y+74*scale,x+440,y+78*scale)).getbbox() is None, 'splash background must be black'


def run(tag,off,code):
    machine={'vga':'os8088_xt_vga','cga':'os8088_5150_cga_gla','herc':'os8088_5150_herc_gla'}[tag]
    with N.os88ui.boot('build/os8088-360.img',apps='build/1942-360.img',machine=machine) as ui:
        m=ui.m;ui.open_drive('B');ui.open('1942.O88');ui.settle()
        g=N.Game(ui,off,code);revealed(ui,g,tag)
        assert g.get('frontkind',1)=={'vga':1,'herc':2,'cga':3}[tag]
        assert g.get('state',1)==0 and g.get('players')==1
        # Time real guest calls while stopped at the locked key callback.
        m.pause();m.bp_exec(g.base+code['n_key']);m.key('KeyH');m.run()
        assert m.wait_stop(30)=='breakpoint'
        def timed(name):
            start=m.status()['cycles'];g.call(name)
            return (m.status()['cycles']-start)/N.M.GUEST_HZ*1000
        g.put('frontready',0)
        initial_ms=timed('n_paint')
        steps=[timed('n_fronttimer') for _ in range(12 if tag=='cga' else 24)]
        paint=timed('n_paint')
        print(tag+' splash guest ms: initial %.2f, worst reveal %.2f, cached paint %.2f'%(initial_ms,max(steps),paint),flush=True)
        assert max(steps)<55, ('splash reveal blocks one XT tick',steps)
        assert paint<250, ('cached splash repaint',paint)
        saved_ax=m.regs()['ax'];menus=[]
        for value in (ord('2'),ord('1'),ord('h'),27):
            m.setreg('ax',value);menus.append(timed('n_key'))
        m.setreg('ax',saved_ax)
        print(tag+' worst menu response: %.2f ms'%max(menus),flush=True)
        assert max(menus[:2])<20, ('player pointer response on XT',menus)
        assert max(menus)<100, ('menu response on XT',menus)
        print(tag+' player selection: %.2f ms'%max(menus[:2]),flush=True)
        # Kernels lacking timers use the same art cache, prepared at entry.
        cache=m.read(g.get('canvas')<<4,62208 if tag=='vga' else 8064 if tag=='herc' else 4032)
        g.put('frontstatic',1,1);g.put('frontready',0)
        timed('n_frontprepare');g.put('frontstatic',0,1)
        assert m.read(g.get('canvas')<<4,len(cache))==cache
        # About owns its overlay while the reveal is paused.
        g.put('frontready',5);timed('n_about');assert g.get('abon',1)==1
        timed('n_fronttimer');assert g.get('frontready')==5
        m.setreg('ax',27);timed('n_key');m.setreg('ax',saved_ax)
        assert g.get('abon',1)==0
        m.bp_exec();m.run();N.M.ui_done(m);key(ui,'Escape');revealed(ui,g,tag)
        initial=shot(ui,tag,'title');logo(initial,g,tag)
        canvas=m.read(g.get('canvas')<<4,62208 if tag=='vga' else 8064 if tag=='herc' else 4032)
        key(ui,'ArrowDown');assert g.get('players')==2
        selected=shot(ui,tag,'two');logo(selected,g,tag)
        assert selected.tobytes()!=initial.tobytes(), 'aircraft selector did not move'
        key(ui,'ArrowUp');assert g.get('players')==1
        key(ui,'Digit2');assert g.get('players')==2
        key(ui,'KeyH');assert g.get('fronthelp',1)==1
        help_image=shot(ui,tag,'help');logo(help_image,g,tag)
        assert help_image.tobytes()!=selected.tobytes()
        x,y=g.get('frontx'),g.get('fronty');scale=g.get('frontscale',1)
        for row in (80,90,100,110):
            assert help_image.crop((x+32,y+row*scale,x+400,y+row*scale+8)).getbbox(), ('missing help row',row)
        assert m.read(g.get('canvas')<<4,len(canvas))==canvas,'menu decoded/changed artwork'
        key(ui,'Escape');assert g.get('fronthelp',1)==0
        ui.move_window(ui.window('1942'),127,21 if tag=='cga' else 28);ui.settle()
        moved=shot(ui,tag,'moved');logo(moved,g,tag,ui)
        w=ui.window('1942');x,y=w.x+1,w.y+N.G.TITLE_H;scale=g.get('frontscale',1)
        if tag=='herc':
            key(ui,'Enter');assert g.get('infs',1)==0 and g.get('error',1)==1
            assert g.get('state',1)==0
            logo(shot(ui,tag,'unsupported'),g,tag)
        else:
            ui.mo.to(x+160,y+104*scale)
            m.pause();m.bp_exec(g.base+code['n_frame_end'])
            m.mouse(l=True);m.run();assert m.wait_stop(30)=='breakpoint'
            m.mouse(l=False)
            assert g.get('infs',1)==1 and g.get('players')==2
            assert g.get('state',1)==1 and g.get('flightphase')==1
            g.put('scorelo',1234)
            m.bp_exec();m.run();key(ui,'Escape');ui.settle()
            assert g.get('infs',1)==0 and g.get('scorelo')==1234
            revealed(ui,g,tag)
            logo(shot(ui,tag,'resume'),g,tag)
            m.pause();m.bp_exec(g.base+code['n_frame_end']);m.key('Enter');m.run()
            assert m.wait_stop(30)=='breakpoint'
            assert g.get('scorelo')==1234 and g.get('players')==2
            m.bp_exec();m.run();key(ui,'Escape');revealed(ui,g,tag)
            key(ui,'KeyN');assert g.get('state',1)==0
            key(ui,'Digit1');assert g.get('players')==1
            logo(shot(ui,tag,'new'),g,tag)
        ui.close(ui.window('1942'));ui.settle()
        print(tag+' splash: exact art pixels, selection, help, drag, launch/resume and close OK',flush=True)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--adapter',choices=('vga','cga','herc','all'),default='all')
    args=ap.parse_args();off,code=N.symbols()
    for tag in (('vga','cga','herc') if args.adapter=='all' else (args.adapter,)):run(tag,off,code)

if __name__=='__main__':main()
